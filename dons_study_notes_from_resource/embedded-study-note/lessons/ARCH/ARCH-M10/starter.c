/* ARCH-M10 예외와 트랩: 벡터 테이블과 복귀 주소
 *
 *   ISA  = ARCH-B08 ISA 의 엄격한 상위집합(strict superset): B08 에서 합법이던 인코딩은 뜻이 그대로이고,
 *          비어 있던 opcode 0110~1100 에 DIV · LDP · STP · SYSCALL · ERET · MFSR · MTSR 을 넣었다. 1101/1110 은 여전히 illegal.
 *   코어 = ARCH-B09 단일 사이클 모델을 축소해 넣고, 폴트가 "멈춤" 대신 "예외 진입" 이 되도록 바꿨다.
 *   Part A  확장 디코드(65536 워드 전수 상위집합 검사) · DIV/LDP/STP/MFSR/MTSR · SYSCALL → 벡터 → 핸들러 → ERET
 *   Part B  정밀 예외: 원인별 EPC/CAUSE/BADADDR, 부작용 0, 원인 우선순위, VBAR 미설정 = B09 동작, 이중 폴트, 퍼즈
 *   Part C  핸들러 3종: 건너뛰기(EPC+1) · 피연산자 고치고 재시도(EPC) · SYSCALL 디스패처(손 어셈블)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 방법)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== ISA =====================
 * bits     15..12  11..9  8..6  5..3  2..0
 * ADD      0000    rd     rs    rt    000      rd <- rs + rt
 * ADDI     0001    rd     rs    imm6           rd <- rs + sext(imm6)                 (-32..31)
 * LD       0010    rd     rs    imm6           rd <- dmem[rs + sext(imm6)]
 * ST       0011    rt     rs    imm6           dmem[rs + sext(imm6)] <- rt           (11..9 = 데이터 원본)
 * BEQ      0100    rs     rt    imm6           if rs == rt: PC <- PC + 1 + sext(imm6)
 * JAL      0101    rd     imm9                 rd <- PC + 1; PC <- PC + 1 + sext(imm9)
 * ---- M10 확장 (B08 에서 illegal 이던 자리) ----
 * DIV      0110    rd     rs    rt    000      rd <- rs / rt (부호 없음, 버림). rt == 0 → DIV0 폴트
 * LDP      0111    rd     rs    imm6           rd <- dmem[ea], rd+1 <- dmem[ea+1]. rd 홀수 → illegal, ea 홀수 → ALIGN
 * STP      1000    rt     rs    imm6           dmem[ea] <- rt, dmem[ea+1] <- rt+1. rt 홀수 → illegal, ea 홀수 → ALIGN
 * SYSCALL  1001    0000   imm8                 트랩: EPC <- PC+1, CAUSE <- imm8 << 8 | 6
 * ERET     1010    0000 0000 0000              PC <- EPC, STATUS.EXL <- 0   (EXL == 0 에서 실행하면 ILLEGAL)
 * MFSR     1011    rd     000   000   sr       rd <- SR[sr]                 (sr 0..4, 그 밖은 illegal)
 * MTSR     1100    000    rs    000   sr       SR[sr] <- rs                 (STATUS 는 bit0 EXL 만 저장)
 * (1101, 1110 = 예약 → illegal)
 * HALT     1111    0000 0000 0000              stop
 */
enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5,
       OP_DIV = 0x6, OP_LDP = 0x7, OP_STP = 0x8, OP_SYSCALL = 0x9, OP_ERET = 0xA, OP_MFSR = 0xB, OP_MTSR = 0xC,
       OP_HALT = 0xF };
/* 시스템 레지스터 (SR) */
enum { SR_EPC = 0, SR_CAUSE = 1, SR_BADADDR = 2, SR_VBAR = 3, SR_STATUS = 4, NSR = 5 };
enum { STATUS_EXL = 1u };                      /* 1 = 예외 처리 중 (handler mode) */
/* 원인 코드 = 벡터 슬롯 번호. 슬롯 0 은 비워 둔다 (리셋 자리). */
enum { EXC_ILLEGAL = 1, EXC_IFETCH = 2, EXC_DIV0 = 3, EXC_ALIGN = 4, EXC_DMEM = 5, EXC_SYSCALL = 6, NCAUSE = 7 };
#define VBAR_NONE 0xFFFFu                      /* 리셋 값: 벡터 테이블 없음 → 예외 = 정지 (B09 와 같은 동작) */

static const char *const cause_name[NCAUSE] = { "-", "ILLEGAL", "IFETCH", "DIV0", "ALIGN", "DMEM", "SYSCALL" };

typedef struct { uint8_t op, rd, rs, rt, sr; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}

/* 디코더. 읽는 레지스터는 rs/rt, 쓰는 레지스터는 rd 로 정규화 (B09 와 같은 규칙). */
static bool decode(uint16_t w, insn_t *d) {
    unsigned a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u, s = w & 7u;
    memset(d, 0, sizeof *d);
    d->op = (uint8_t)(w >> 12);
    switch (d->op) {
    case OP_ADD:  if (w & 7u) return false;
                  d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->rt = (uint8_t)c; return true;
    case OP_ADDI:
    case OP_LD:   d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_ST:   d->rt = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_BEQ:  d->rs = (uint8_t)a; d->rt = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_JAL:  d->rd = (uint8_t)a; d->imm = sext(w, 9); return true;
    case OP_HALT: return (w & 0x0FFFu) == 0;
    /* ---- M10 확장 (TODO 1): 예약 비트 != 0, 홀수 페어 레지스터, sr >= NSR → false ---- */
    /* (여기에 OP_DIV ~ OP_MTSR case 를 채운다) */
    (void)c; (void)s;
    default:      return false;
    }
}

/* 손 어셈블용 매크로 (범위 검사 없음 — 오프셋은 주석의 라벨 계산 + 테스트로 확인) */
#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_JAL(rd, off)      ((uint16_t)((0x5u << 12) | ((rd) << 9) | ((unsigned)(off) & 0x1FFu)))
#define I_DIV(rd, rs, rt)   ((uint16_t)((0x6u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_LDP(rd, rs, imm)  ((uint16_t)((0x7u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_STP(rt, rs, imm)  ((uint16_t)((0x8u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_SYSCALL(n)        ((uint16_t)((0x9u << 12) | ((unsigned)(n) & 0xFFu)))
#define I_ERET              ((uint16_t)0xA000u)
#define I_MFSR(rd, sr)      ((uint16_t)((0xBu << 12) | ((rd) << 9) | (sr)))
#define I_MTSR(sr, rs)      ((uint16_t)((0xCu << 12) | ((rs) << 6) | (sr)))
#define I_HALT              ((uint16_t)0xF000u)

/* ===================== CPU 상태 ===================== */
typedef enum { CPU_RUNNING, CPU_HALTED, CPU_UNHANDLED, CPU_DOUBLE_FAULT, CPU_STEP_LIMIT } cpu_status_t;

typedef struct {
    uint16_t reg[NREG];
    uint16_t pc;
    uint16_t imem[IMEM_WORDS];
    uint16_t dmem[DMEM_WORDS];
    uint16_t sr[NSR];             /* EPC, CAUSE, BADADDR, VBAR, STATUS */
    cpu_status_t status;
    uint64_t retired;             /* 완료된 명령 수 (폴트 난 명령은 제외, SYSCALL 은 포함) */
    uint64_t op_hist[16];
    uint64_t exc_count[NCAUSE];   /* 받아들인 예외 수 (원인별) */
    uint8_t  df_cause;            /* 이중 폴트: 두 번째 예외의 원인 */
    uint16_t df_epc;              /* 이중 폴트: 두 번째 예외의 EPC 후보 */
} cpu_t;

static void cpu_load(cpu_t *c, const uint16_t *prog, size_t n) {
    memset(c, 0, sizeof *c);                       /* 빈 imem = 0x0000 = ADD r0,r0,r0 = NOP */
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) c->imem[i] = prog[i];
    c->sr[SR_VBAR] = VBAR_NONE;
    c->status = CPU_RUNNING;
}

/* ---------------- TODO 2: 예외 진입 (하드웨어가 하는 일) ----------------
 * cause = 원인 코드, badaddr = BADADDR 에 넣을 값, epc = 복귀 주소 (폴트: 그 명령의 pc / 트랩: pc + 1),
 * iss = 원인별 부가 정보 (SYSCALL 번호; 나머지는 0) → CAUSE = iss << 8 | cause.
 *  1) 이미 핸들러 안(STATUS.EXL = 1)이면 이중 폴트: df_cause/df_epc 만 기록하고 CPU_DOUBLE_FAULT 로 멈춘다.
 *     EPC/CAUSE/BADADDR 는 첫 예외 값 그대로 (디버거가 원래 원인을 볼 수 있게).
 *  2) EPC, CAUSE, BADADDR 를 쓰고 exc_count[cause]++.
 *  3) VBAR == VBAR_NONE 이면 갈 곳이 없다: pc <- epc, CPU_UNHANDLED 로 멈춘다 (B09 의 정밀 폴트 정지와 같은 모양).
 *  4) STATUS.EXL <- 1, pc <- VBAR + cause, CPU_RUNNING 을 돌려준다.
 * r0..r7 · dmem · retired 는 절대 건드리지 않는다. */
static cpu_status_t take_exception(cpu_t *c, unsigned cause, uint16_t badaddr, uint16_t epc, uint8_t iss) {
    /* TODO 2: 위 1)~4) */
    (void)cause; (void)badaddr; (void)iss;
    c->pc = epc;
    return c->status = CPU_UNHANDLED;
}

/* ---------------- 한 명령 실행 ----------------
 * 1) fetch      pc >= IMEM_WORDS → IFETCH (BADADDR = pc)
 * 2) decode     illegal → ILLEGAL (BADADDR = 명령 워드: Harvard 라 핸들러가 imem 을 읽을 수 없으므로)
 * 3) execute    LD/ST 범위 → DMEM, DIV 제수 0 → DIV0, LDP/STP 홀수 ea → ALIGN (범위보다 먼저), ERET in thread → ILLEGAL
 * 4) writeback  r0 고정. LDP 는 rd, rd+1 두 개를 쓴다.
 * 폴트는 정밀: 검사는 모두 상태를 쓰기 "전" 에 하고, 폴트 난 명령은 retire 하지 않는다 (EPC = 그 명령).
 * SYSCALL 은 트랩: 명령이 완료(retire)된 "뒤" 예외로 들어간다 (EPC = 다음 명령). */
static cpu_status_t cpu_step(cpu_t *c) {
    if (c->status != CPU_RUNNING) return c->status;
    const uint16_t pc = c->pc;
    /* 1) fetch */
    if (pc >= IMEM_WORDS) return take_exception(c, EXC_IFETCH, pc, pc, 0);
    const uint16_t w = c->imem[pc];
    /* 2) decode + register read */
    insn_t d;
    if (!decode(w, &d)) return take_exception(c, EXC_ILLEGAL, w, pc, 0);
    const uint16_t a = c->reg[d.rs], b = c->reg[d.rt], b2 = c->reg[(d.rt + 1u) & 7u];
    uint16_t next_pc = (uint16_t)(pc + 1u), wb_val = 0, wb2_val = 0;
    bool wb = false, wb2 = false, trap = false;
    /* 3) execute / memory */
    switch (d.op) {
    case OP_ADD:  wb_val = (uint16_t)(a + b); wb = true; break;
    case OP_ADDI: wb_val = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
    case OP_LD:
    case OP_ST: {
        uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
        if (ea >= DMEM_WORDS) return take_exception(c, EXC_DMEM, ea, pc, 0);
        if (d.op == OP_LD) { wb_val = c->dmem[ea]; wb = true; }
        else               { c->dmem[ea] = b; }
        break;
    }
    case OP_BEQ:  if (a == b) next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_JAL:  wb_val = next_pc; wb = true; next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_HALT: c->status = CPU_HALTED; next_pc = pc; break;
    /* ---- M10 확장 (TODO 3): DIV LDP STP SYSCALL ERET MFSR MTSR ---- */
    /* (여기에 OP_DIV ~ OP_MTSR case 를 채운다. 예외는 return take_exception(...) 으로, 상태를 쓰기 전에) */
    default: (void)b2; (void)wb2_val; break;
    }
    /* 4) writeback — r0 고정. LDP 의 rd+1 은 항상 1..7 */
    if (wb && d.rd != 0) c->reg[d.rd] = wb_val;
    if (wb2) c->reg[d.rd + 1u] = wb2_val;
    /* retire */
    c->retired++;
    c->op_hist[d.op]++;
    c->pc = next_pc;
    /* 트랩은 완료 "뒤" 에: EPC = 다음 명령, CAUSE 상위 8비트 = SYSCALL 번호 */
    if (trap) return take_exception(c, EXC_SYSCALL, 0, next_pc, (uint8_t)d.imm);
    return c->status;
}

static cpu_status_t cpu_run(cpu_t *c, uint64_t max_steps) {
    for (uint64_t i = 0; i < max_steps; i++) {
        cpu_status_t s = cpu_step(c);
        if (s != CPU_RUNNING) return s;
    }
    return c->status == CPU_RUNNING ? CPU_STEP_LIMIT : c->status;
}

/* ===================== 이미지 배치 도우미 (주어짐) ===================== */
static void place(cpu_t *c, uint16_t at, const uint16_t *w, size_t n) {
    for (size_t i = 0; i < n && at + i < IMEM_WORDS; i++) c->imem[at + i] = w[i];
}
/* VBAR 설정 + 슬롯 0..6 을 HALT 로 채운다 (CMSIS 시작 코드의 Default_Handler 'b .' 대신 멈춤). */
static void vec_init(cpu_t *c, uint16_t vbar) {
    c->sr[SR_VBAR] = vbar;
    for (unsigned k = 0; k < NCAUSE && vbar + k < IMEM_WORDS; k++) c->imem[vbar + k] = I_HALT;
}
/* 슬롯 VBAR + cause ← JAL r0, handler (imm9 = ±256 이라 imem 어디든 닿는다) */
static void vec_jump(cpu_t *c, unsigned cause, uint16_t handler) {
    uint16_t slot = (uint16_t)(c->sr[SR_VBAR] + cause);
    c->imem[slot] = I_JAL(0, (int)handler - (int)(slot + 1));
}
#define VBAR 0x40u
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ===================== 프로그램 (손 어셈블) ===================== */
/* Part A: 프로그램이 스스로 VBAR 를 설정하고 DIV · LDP · STP · SYSCALL 을 쓴다.
 * 입력 dmem[0] = 1000, dmem[1] = 7. SYSCALL 핸들러는 벡터 슬롯 6 (= 0x46) 에 "그 자리에서" 시작한다. */
static const uint16_t prog_a[] = {
    I_ADDI(1, 0, 31),       /*  0: ADDI r1, r0, 31                              */
    I_ADDI(1, 1, 31),       /*  1: ADDI r1, r1, 31                              */
    I_ADDI(1, 1, 2),        /*  2: ADDI r1, r1, 2      ; r1 = 64 = 0x40         */
    I_MTSR(SR_VBAR, 1),     /*  3: MTSR VBAR, r1       ; 벡터 테이블 = imem[0x40] */
    I_LDP(2, 0, 0),         /*  4: LDP  r2, 0(r0)      ; r2 = 1000, r3 = 7       */
    I_DIV(4, 2, 3),         /*  5: DIV  r4, r2, r3     ; r4 = 142                */
    I_SYSCALL(5),           /*  6: SYSCALL 5           ; → 0x46, EPC = 7         */
    I_STP(4, 0, 2),         /*  7: STP  r4, 2(r0)      ; dmem[2] = r4, dmem[3] = r5 */
    I_MFSR(7, SR_STATUS),   /*  8: MFSR r7, STATUS     ; 0 (ERET 이 EXL 을 내림)  */
    I_HALT,                 /*  9: HALT                                         */
};
static const uint16_t handler_a[] = {   /* 0x46 = VBAR + 6 (SYSCALL 슬롯이 마지막이라 점프 없이 바로 시작) */
    I_MFSR(5, SR_CAUSE),    /* 0x46: MFSR r5, CAUSE    ; 5 << 8 | 6 = 0x0506     */
    I_MFSR(6, SR_EPC),      /* 0x47: MFSR r6, EPC      ; 7                       */
    I_MFSR(1, SR_STATUS),   /* 0x48: MFSR r1, STATUS   ; 1 (EXL)                 */
    I_ERET,                 /* 0x49: ERET              ; pc ← 7, EXL ← 0         */
};

/* B09 의 배열 합 (그대로). VBAR 미설정 코어에서 B09 와 같은 결과인지 본다. 명령 수 6N + 6 */
static const uint16_t prog_sum_b09[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 16), I_ADD(3, 0, 0), I_BEQ(1, 0, 5), I_LD(4, 2, 0),
    I_ADD(3, 3, 4), I_ADDI(2, 2, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -6), I_ST(3, 0, 1), I_HALT,
};

/* Part C: 버킷 평균. dmem[0] = n (<= 8), sum[i] = dmem[32+i], cnt[i] = dmem[48+i], avg[i] = dmem[40+i].
 * 커널(핸들러) 영역: dmem[24..31] (r0 기준 imm6 로 닿는 곳). 명령 수 = 9n + 5 (+ 핸들러) */
static const uint16_t prog_avg[] = {
    I_LD(1, 0, 0),          /*  0:       LD   r1, 0(r0)      ; n                     */
    I_ADDI(2, 0, 31),       /*  1:       ADDI r2, r0, 31                             */
    I_ADDI(2, 2, 1),        /*  2:       ADDI r2, r2, 1      ; p = 32                */
    I_BEQ(1, 0, 8),         /*  3: loop: BEQ  r1, r0, done   ; done=12 → 12-4 = +8   */
    I_LD(3, 2, 0),          /*  4:       LD   r3, 0(r2)      ; sum[i]                */
    I_LD(4, 2, 16),         /*  5:       LD   r4, 16(r2)     ; cnt[i] (ABI: 제수는 r4) */
    I_ADDI(5, 0, -1),       /*  6:       ADDI r5, r0, -1     ; 센티널 0xFFFF          */
    I_DIV(5, 3, 4),         /*  7:       DIV  r5, r3, r4     ; ← cnt == 0 이면 DIV0   */
    I_ST(5, 2, 8),          /*  8:       ST   r5, 8(r2)      ; avg[i]                */
    I_ADDI(2, 2, 1),        /*  9:       ADDI r2, r2, 1                              */
    I_ADDI(1, 1, -1),       /* 10:       ADDI r1, r1, -1                             */
    I_BEQ(0, 0, -9),        /* 11:       BEQ  r0, r0, loop   ; 3-12 = -9             */
    I_HALT,                 /* 12: done: HALT                                        */
};
/* C1 건너뛰기 핸들러 (0x48): 에러 수 세고, EPC + 1 → DIV 는 없던 일이 되고 r5 에 센티널이 남는다 */
static const uint16_t h_skip[] = {
    I_STP(4, 0, 28),        /* 0x48: STP  r4, 28(r0)     ; r4, r5 저장 (dmem[28..29]) */
    I_LD(4, 0, 27),         /* 0x49: LD   r4, 27(r0)     ; errcnt                     */
    I_ADDI(4, 4, 1),        /* 0x4A: ADDI r4, r4, 1                                   */
    I_ST(4, 0, 27),         /* 0x4B: ST   r4, 27(r0)                                  */
    I_MFSR(4, SR_EPC),      /* 0x4C: MFSR r4, EPC                                     */
    I_ADDI(4, 4, 1),        /* 0x4D: ADDI r4, r4, 1      ; 폴트 난 명령 다음으로       */
    I_MTSR(SR_EPC, 4),      /* 0x4E: MTSR EPC, r4                                     */
    I_LDP(4, 0, 28),        /* 0x4F: LDP  r4, 28(r0)     ; r4, r5 복원                 */
    I_ERET,                 /* 0x50: ERET                                             */
};
/* C2 재시도 핸들러 (0x48): 피연산자를 고치고(제수 r4 ← 1) EPC 그대로 → 같은 DIV 를 다시 실행 */
static const uint16_t h_retry[] = {
    I_ST(6, 0, 30),         /* 0x48: ST   r6, 30(r0)     ; r6 저장                     */
    I_LD(6, 0, 27),         /* 0x49: LD   r6, 27(r0)                                  */
    I_ADDI(6, 6, 1),        /* 0x4A: ADDI r6, r6, 1                                   */
    I_ST(6, 0, 27),         /* 0x4B: ST   r6, 27(r0)     ; errcnt++                    */
    I_LD(6, 0, 30),         /* 0x4C: LD   r6, 30(r0)     ; r6 복원                     */
    I_ADDI(4, 0, 1),        /* 0x4D: ADDI r4, r0, 1      ; 고침: 제수 = 1 (빈 버킷 → sum/1 = 0) */
    I_ERET,                 /* 0x4E: ERET                ; EPC = DIV → 재실행          */
};
/* 원인을 고치지 않는 재시도 핸들러 = 재시도 폭주 */
static const uint16_t h_storm[] = { I_ERET };

/* Part C3: SYSCALL 사용자 프로그램. 규약: 번호 = SYSCALL imm8, 인자 r2, 반환 r1, 그 밖의 레지스터는 보존 */
static const uint16_t prog_sys_user[] = {
    I_LD(2, 0, 16),         /*  0: LD   r2, 16(r0)     ; 'H'                     */
    I_SYSCALL(0),           /*  1: SYSCALL 0           ; putc                    */
    I_LD(2, 0, 17),         /*  2: LD   r2, 17(r0)     ; 'I'                     */
    I_SYSCALL(0),           /*  3: SYSCALL 0           ; putc                    */
    I_ADDI(3, 0, 5),        /*  4: ADDI r3, r0, 5      ; 호출을 건너 살아 있어야 할 값 */
    I_SYSCALL(9),           /*  5: SYSCALL 9           ; 없는 번호 → r1 = 0xFFFF  */
    I_ADD(6, 1, 0),         /*  6: ADD  r6, r1, r0                               */
    I_SYSCALL(1),           /*  7: SYSCALL 1           ; count → r1 = 지금까지 호출 수 */
    I_ADDI(2, 0, 3),        /*  8: ADDI r2, r0, 3                                */
    I_SYSCALL(2),           /*  9: SYSCALL 2           ; exit(3) — 돌아오지 않는다 */
    I_HALT,                 /* 10: HALT                ; 도달하면 안 됨           */
};

/* ---------------- TODO 4: SYSCALL 디스패처 손 어셈블 (VBAR + 6 = 0x46 에서 시작) ----------------
 * 커널 데이터: dmem[24] = putc 출력 포인터, dmem[25] = 호출 수, dmem[26] = exit 코드, dmem[28..29] = r4/r5 저장
 * 0x46 sys:   STP  r4, 28(r0)      ; r4, r5 저장
 * 0x47        LD   r5, 25(r0)
 * 0x48        ADDI r5, r5, 1
 * 0x49        ST   r5, 25(r0)      ; 호출 수++
 * 0x4A        MFSR r4, CAUSE       ; 번호 << 8 | 6
 * 0x4B        ADDI r5, r0, 16
 * 0x4C        ADD  r5, r5, r5      ; 32
 * 0x4D        ADD  r5, r5, r5      ; 64
 * 0x4E        ADD  r5, r5, r5      ; 128
 * 0x4F        ADD  r5, r5, r5      ; 256
 * 0x50        DIV  r4, r4, r5      ; r4 = CAUSE >> 8 = 번호 (시프트가 없어서 나눗셈으로)
 * 0x51        BEQ  r4, r0, putc
 * 0x52        ADDI r4, r4, -1
 * 0x53        BEQ  r4, r0, count
 * 0x54        ADDI r4, r4, -1
 * 0x55        BEQ  r4, r0, exit
 * 0x56        ADDI r1, r0, -1      ; 모르는 번호: r1 = 0xFFFF (ENOSYS 같은 것)
 * 0x57        BEQ  r0, r0, ret
 * 0x58 putc:  LD   r4, 24(r0)
 * 0x59        ST   r2, 0(r4)
 * 0x5A        ADDI r4, r4, 1
 * 0x5B        ST   r4, 24(r0)
 * 0x5C        ADD  r1, r0, r0      ; 반환 0
 * 0x5D        BEQ  r0, r0, ret
 * 0x5E count: LD   r1, 25(r0)
 * 0x5F        BEQ  r0, r0, ret
 * 0x60 exit:  ST   r2, 26(r0)
 * 0x61        HALT                 ; 핸들러 안에서 멈춘다 (EXL = 1 로 남음)
 * 0x62 ret:   LDP  r4, 28(r0)      ; r4, r5 복원
 * 0x63        ERET                 ; EPC = SYSCALL 다음 명령
 * 오프셋 = 목표 - (PC + 1). */
static const uint16_t prog_sys_handler[] = {
    I_HALT, /* TODO 4: 위 30줄을 I_* 매크로로 */
};

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x2545F491u;                  /* 고정 시드 xorshift32 → 재현 가능 */
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static uint64_t exc_total(const cpu_t *c) {
    uint64_t t = 0;
    for (unsigned k = 0; k < NCAUSE; k++) t += c->exc_count[k];
    return t;
}

/* 예외 진입마다 한 줄, 핸들러 경로 길이(진입 ~ ERET 또는 HALT 까지 retire 수)를 찍으며 실행 */
static cpu_status_t run_logged(cpu_t *c, uint64_t max_steps, bool verbose) {
    uint64_t seen = exc_total(c), entry_ret = 0;
    for (uint64_t i = 0; i < max_steps; i++) {
        bool in_handler = c->sr[SR_STATUS] & STATUS_EXL;
        cpu_status_t s = cpu_step(c);
        if (exc_total(c) != seen) {
            seen = exc_total(c);
            entry_ret = c->retired;
            unsigned cause = c->sr[SR_CAUSE] & 0xFFu;
            if (verbose)
                printf("    exc %-7s n=%-3u EPC=0x%04X BADADDR=0x%04X -> pc 0x%04X",
                       cause_name[cause], c->sr[SR_CAUSE] >> 8, c->sr[SR_EPC], c->sr[SR_BADADDR], c->pc);
        } else if (in_handler && (!(c->sr[SR_STATUS] & STATUS_EXL) || s != CPU_RUNNING)) {
            if (verbose) printf("  | handler %llu instr%s\n", (unsigned long long)(c->retired - entry_ret),
                                s == CPU_HALTED ? " (HALT)" : "");
        }
        if (s != CPU_RUNNING) return s;
    }
    return CPU_STEP_LIMIT;
}

/* ---- 비교 기준 1: B08 디코더 (확장 전) 그대로 ---- */
static bool ref_decode_b08(uint16_t w, insn_t *d) {
    unsigned a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u;
    memset(d, 0, sizeof *d);
    d->op = (uint8_t)(w >> 12);
    switch (d->op) {
    case OP_ADD:  if (w & 7u) return false;
                  d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->rt = (uint8_t)c; return true;
    case OP_ADDI: case OP_LD: d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_ST:   d->rt = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_BEQ:  d->rs = (uint8_t)a; d->rt = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_JAL:  d->rd = (uint8_t)a; d->imm = sext(w, 9); return true;
    case OP_HALT: return (w & 0x0FFFu) == 0;
    default:      return false;
    }
}
/* ---- 비교 기준 2: B09 한 스텝 (예외 없음: 폴트면 false) ---- */
static bool ref_step_b09(cpu_t *c) {
    if (c->pc >= IMEM_WORDS) return false;
    insn_t d;
    if (!ref_decode_b08(c->imem[c->pc], &d)) return false;
    uint16_t a = c->reg[d.rs], b = c->reg[d.rt], next = (uint16_t)(c->pc + 1u), v = 0;
    bool wb = false;
    switch (d.op) {
    case OP_ADD:  v = (uint16_t)(a + b); wb = true; break;
    case OP_ADDI: v = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
    case OP_LD: case OP_ST: {
        uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
        if (ea >= DMEM_WORDS) return false;
        if (d.op == OP_LD) { v = c->dmem[ea]; wb = true; } else c->dmem[ea] = b;
        break;
    }
    case OP_BEQ:  if (a == b) next = (uint16_t)(next + (uint16_t)d.imm); break;
    case OP_JAL:  v = next; wb = true; next = (uint16_t)(next + (uint16_t)d.imm); break;
    case OP_HALT: next = c->pc; c->status = CPU_HALTED; break;
    default: break;
    }
    if (wb && d.rd) c->reg[d.rd] = v;
    c->retired++;
    c->pc = next;
    return true;
}

static bool same_arch(const cpu_t *x, const cpu_t *y) {     /* 사용자에게 보이는 상태: 레지스터 + dmem */
    return memcmp(x->reg, y->reg, sizeof x->reg) == 0 && memcmp(x->dmem, y->dmem, sizeof x->dmem) == 0;
}

/* ===================== Part A ===================== */
static void test_part_a(void) {
    /* A1: 엄격한 상위집합 — 65536 워드 전수. B08 합법 워드는 같은 필드로 디코드되어야 한다 */
    unsigned b08 = 0, fresh = 0, illegal = 0, per_op[16] = {0};
    bool same = true;
    for (uint32_t w = 0; w < 65536u; w++) {
        insn_t r, d;
        bool lr = ref_decode_b08((uint16_t)w, &r), ld = decode((uint16_t)w, &d);
        if (lr) {
            b08++;
            same = same && ld && r.op == d.op && r.rd == d.rd && r.rs == d.rs && r.rt == d.rt && r.imm == d.imm;
        } else if (ld) { fresh++; per_op[w >> 12]++; }
        else illegal++;
    }
    CHECK(same && b08 == 20993);
    CHECK(fresh == 4945 && illegal == 39598);
    CHECK(per_op[OP_DIV] == 512 && per_op[OP_LDP] == 2048 && per_op[OP_STP] == 2048 && per_op[OP_SYSCALL] == 256);
    CHECK(per_op[OP_ERET] == 1 && per_op[OP_MFSR] == 40 && per_op[OP_MTSR] == 40 && per_op[0xD] == 0 && per_op[0xE] == 0);
    printf("  65536 words: B08-legal %u (unchanged), newly legal %u (DIV %u LDP %u STP %u SYSCALL %u ERET %u MFSR %u MTSR %u), illegal %u\n",
           b08, fresh, per_op[OP_DIV], per_op[OP_LDP], per_op[OP_STP], per_op[OP_SYSCALL], per_op[OP_ERET],
           per_op[OP_MFSR], per_op[OP_MTSR], illegal);

    cpu_t c;
    /* A2: DIV — 부호 없는 버림 나눗셈, r0 목적지는 버려짐 */
    static const uint16_t p_div[] = {
        I_LD(1, 0, 0), I_LD(2, 0, 1), I_DIV(3, 1, 2),       /* 1000 / 7 = 142          */
        I_LD(4, 0, 2), I_ADDI(5, 0, 2), I_DIV(6, 4, 5),     /* 0xFFFF / 2 = 0x7FFF     */
        I_DIV(7, 5, 1),                                     /* 2 / 1000 = 0            */
        I_DIV(0, 1, 2), I_HALT,                             /* r0 에 쓰기 → 버림       */
    };
    cpu_load(&c, p_div, NELEM(p_div));
    c.dmem[0] = 1000; c.dmem[1] = 7; c.dmem[2] = 0xFFFF;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.reg[3] == 142 && c.reg[6] == 0x7FFF && c.reg[7] == 0 && c.reg[0] == 0);
    CHECK(c.op_hist[OP_DIV] == 4 && c.retired == 9);

    /* A3: LDP/STP — 페어, r0 페어(r0 은 버리고 r1 만), dmem 끝 (254, 255) */
    static const uint16_t p_pair[] = {
        I_LDP(2, 0, 4),         /* r2 = dmem[4], r3 = dmem[5]        */
        I_STP(2, 0, 10),        /* dmem[10] = r2, dmem[11] = r3      */
        I_LDP(0, 0, 4),         /* r0 버림, r1 = dmem[5]             */
        I_LD(7, 0, 6),          /* r7 = 254                          */
        I_LDP(4, 7, 0),         /* r4 = dmem[254], r5 = dmem[255]    */
        I_STP(2, 7, 0),         /* dmem[254] = r2, dmem[255] = r3    */
        I_HALT,
    };
    cpu_load(&c, p_pair, NELEM(p_pair));
    c.dmem[4] = 0x1111; c.dmem[5] = 0x2222; c.dmem[6] = 254; c.dmem[254] = 0xAAAA; c.dmem[255] = 0xBBBB;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.reg[2] == 0x1111 && c.reg[3] == 0x2222 && c.reg[1] == 0x2222 && c.reg[0] == 0);
    CHECK(c.dmem[10] == 0x1111 && c.dmem[11] == 0x2222 && c.reg[4] == 0xAAAA && c.reg[5] == 0xBBBB);
    CHECK(c.dmem[254] == 0x1111 && c.dmem[255] == 0x2222 && exc_total(&c) == 0);

    /* A4: SYSCALL → 슬롯 6 → 핸들러 → ERET. EPC = PC+1, CAUSE = 번호 << 8 | 6, 핸들러 안 STATUS = 1 */
    cpu_load(&c, prog_a, NELEM(prog_a));
    place(&c, VBAR + EXC_SYSCALL, handler_a, NELEM(handler_a));
    c.dmem[0] = 1000; c.dmem[1] = 7;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.sr[SR_VBAR] == VBAR);
    CHECK(c.reg[4] == 142 && c.reg[5] == 0x0506 && c.reg[6] == 7 && c.reg[1] == 1 && c.reg[7] == 0);
    CHECK(c.dmem[2] == 142 && c.dmem[3] == 0x0506 && c.pc == 9);
    CHECK(c.retired == 14 && c.op_hist[OP_SYSCALL] == 1 && c.op_hist[OP_ERET] == 1 && c.exc_count[EXC_SYSCALL] == 1);
    CHECK(c.sr[SR_EPC] == 7 && c.sr[SR_BADADDR] == 0 && (c.sr[SR_STATUS] & STATUS_EXL) == 0);
    printf("  prog_a: r4=%u CAUSE=0x%04X EPC=%u retired=%llu (user 10 + handler 4)\n",
           c.reg[4], c.reg[5], c.reg[6], (unsigned long long)c.retired);
}

/* ===================== Part B ===================== */
typedef struct {
    const char *name;
    uint16_t prog[3]; unsigned n, pre;   /* pre = 폴트 명령 앞의 준비 명령 수 */
    uint16_t dmem0;                      /* dmem[0] 초기값 */
    unsigned cause; uint16_t badaddr;
} fault_case_t;

static void test_part_b(void) {
    cpu_t c, snap;
    static const fault_case_t fc[] = {
        { "opcode 1101",      { I_ADDI(1, 0, 9), 0xD000 }, 2, 1, 0, EXC_ILLEGAL, 0xD000 },
        { "ADD funct!=0",     { 0x0001 }, 1, 0, 0, EXC_ILLEGAL, 0x0001 },
        { "LDP odd rd",       { 0x7600 }, 1, 0, 0, EXC_ILLEGAL, 0x7600 },
        { "SYSCALL rsv bits", { 0x9100 }, 1, 0, 0, EXC_ILLEGAL, 0x9100 },
        { "MFSR sr=5",        { 0xB205 }, 1, 0, 0, EXC_ILLEGAL, 0xB205 },
        { "ERET in thread",   { I_ERET }, 1, 0, 0, EXC_ILLEGAL, 0xA000 },
        { "BEQ to 0xFFFF",    { I_BEQ(0, 0, -2) }, 1, 1, 0, EXC_IFETCH, 0xFFFF },
        { "DIV by r0",        { I_ADDI(1, 0, 5), I_DIV(2, 1, 0) }, 2, 1, 0, EXC_DIV0, 0 },
        { "LDP ea=3",         { I_ADDI(1, 0, 3), I_LDP(2, 1, 0) }, 2, 1, 0, EXC_ALIGN, 3 },
        { "STP ea=255",       { I_LD(1, 0, 0), I_STP(2, 1, 0) }, 2, 1, 255, EXC_ALIGN, 255 },
        { "LDP ea=0x101",     { I_LD(1, 0, 0), I_LDP(2, 1, 0) }, 2, 1, 0x101, EXC_ALIGN, 0x101 },
        { "STP ea=0x100",     { I_LD(1, 0, 0), I_STP(2, 1, 0) }, 2, 1, 0x100, EXC_DMEM, 0x100 },
        { "LDP ea=0xFFFE",    { I_ADDI(1, 0, -2), I_LDP(2, 1, 0) }, 2, 1, 0, EXC_DMEM, 0xFFFE },
        { "LD -1(r0)",        { I_LD(3, 0, -1) }, 1, 0, 0, EXC_DMEM, 0xFFFF },
    };
    bool all = true;
    printf("  %-17s %-7s %6s %7s %7s %7s  %s\n", "case", "cause", "EPC", "BADADDR", "pc", "retired", "state");
    for (unsigned k = 0; k < NELEM(fc); k++) {
        cpu_load(&c, fc[k].prog, fc[k].n);
        vec_init(&c, VBAR);
        c.dmem[0] = fc[k].dmem0;
        for (unsigned r = 1; r < NREG; r++) c.reg[r] = (uint16_t)(0xA0A0u + r);   /* "건드리면 티 나는" 값 */
        c.dmem[255] = 0x5555;
        for (unsigned i = 0; i < fc[k].pre; i++) cpu_step(&c);
        snap = c;
        uint16_t fpc = c.pc;
        cpu_status_t s = cpu_step(&c);
        bool ok = s == CPU_RUNNING && same_arch(&c, &snap) && c.retired == snap.retired
               && c.sr[SR_EPC] == fpc && c.sr[SR_CAUSE] == fc[k].cause && c.sr[SR_BADADDR] == fc[k].badaddr
               && c.pc == VBAR + fc[k].cause && (c.sr[SR_STATUS] & STATUS_EXL) && c.exc_count[fc[k].cause] == 1;
        printf("  %-17s %-7s 0x%04X  0x%04X  0x%04X %7llu  %s\n", fc[k].name, cause_name[c.sr[SR_CAUSE] & 0xFF],
               c.sr[SR_EPC], c.sr[SR_BADADDR], c.pc, (unsigned long long)c.retired,
               ok ? "regs/dmem unchanged" : "MISMATCH");
        all = all && ok;
    }
    CHECK(all);

    /* SYSCALL 은 트랩: retire 하고 EPC = PC + 1 */
    static const uint16_t p_sc[] = { I_ADDI(1, 0, 1), I_SYSCALL(0xAB), I_HALT };
    cpu_load(&c, p_sc, NELEM(p_sc));
    vec_init(&c, VBAR);
    cpu_step(&c); cpu_step(&c);
    CHECK(c.sr[SR_EPC] == 2 && c.sr[SR_CAUSE] == 0xAB06 && c.retired == 2 && c.pc == VBAR + EXC_SYSCALL);

    /* VBAR 미설정 = B09 동작: 정밀 폴트에서 멈춤, B09 프로그램은 같은 결과 */
    cpu_load(&c, prog_sum_b09, NELEM(prog_sum_b09));
    c.dmem[0] = 10;
    for (unsigned i = 0; i < 10; i++) c.dmem[16 + i] = (uint16_t)(1000 * (i + 1));
    CHECK(cpu_run(&c, 10000) == CPU_HALTED && c.dmem[1] == 55000 && c.retired == 66);
    static const uint16_t p_dz[] = { I_ADDI(1, 0, 7), I_ADDI(2, 0, 9), I_DIV(2, 1, 0), I_HALT };
    cpu_load(&c, p_dz, NELEM(p_dz));
    CHECK(cpu_run(&c, 100) == CPU_UNHANDLED && c.pc == 2 && c.reg[2] == 9 && c.retired == 2 && c.sr[SR_CAUSE] == EXC_DIV0);
    /* B09 에서 illegal 이던 0x6000 은 이제 DIV r0,r0,r0 → DIV0. 0xD000 은 여전히 ILLEGAL */
    static const uint16_t w6 = 0x6000, wd = 0xD000;
    cpu_load(&c, &w6, 1);
    CHECK(cpu_run(&c, 10) == CPU_UNHANDLED && c.sr[SR_CAUSE] == EXC_DIV0);
    cpu_load(&c, &wd, 1);
    CHECK(cpu_run(&c, 10) == CPU_UNHANDLED && c.sr[SR_CAUSE] == EXC_ILLEGAL && c.sr[SR_BADADDR] == 0xD000);

    /* 이중 폴트 1: DIV0 핸들러가 다시 폴트 → 멈춤. EPC/CAUSE 는 첫 예외 그대로 */
    static const uint16_t p_df[] = { I_ADDI(1, 0, 1), I_DIV(2, 1, 0), I_HALT };
    static const uint16_t h_bad[] = { I_LD(1, 0, -1), I_ERET };
    cpu_load(&c, p_df, NELEM(p_df));
    vec_init(&c, VBAR); vec_jump(&c, EXC_DIV0, 0x48); place(&c, 0x48, h_bad, NELEM(h_bad));
    CHECK(cpu_run(&c, 100) == CPU_DOUBLE_FAULT && c.sr[SR_EPC] == 1 && c.sr[SR_CAUSE] == EXC_DIV0);
    CHECK(c.df_cause == EXC_DMEM && c.df_epc == 0x48 && c.pc == 0x48 && c.reg[1] == 1);
    /* 이중 폴트 2: SYSCALL 핸들러 안의 SYSCALL */
    static const uint16_t p_sc2[] = { I_SYSCALL(1), I_HALT };
    static const uint16_t h_sc2[] = { I_SYSCALL(2), I_ERET };
    cpu_load(&c, p_sc2, NELEM(p_sc2));
    vec_init(&c, VBAR); place(&c, VBAR + EXC_SYSCALL, h_sc2, NELEM(h_sc2));
    CHECK(cpu_run(&c, 100) == CPU_DOUBLE_FAULT && c.sr[SR_CAUSE] == 0x0106 && c.df_cause == EXC_SYSCALL);
    /* 이중 폴트 3: 벡터가 imem 밖 (VBAR = 250, SYSCALL → 256) → 벡터 fetch 가 IFETCH */
    cpu_load(&c, p_sc2, NELEM(p_sc2));
    c.sr[SR_VBAR] = 250;
    CHECK(cpu_run(&c, 100) == CPU_DOUBLE_FAULT && c.df_cause == EXC_IFETCH && c.df_epc == 256 && c.sr[SR_EPC] == 1);

    /* 퍼즈: 무작위 상태 + 무작위 명령 1개. 예외면 정밀 불변식, B08 합법이면 B09 기준 모델과 같아야 한다 */
    unsigned seen[NCAUSE] = {0}, n_df = 0, n_cmp = 0, n_bad = 0;
    for (unsigned k = 0; k < 200000u; k++) {
        cpu_load(&c, NULL, 0);
        for (unsigned r = 1; r < NREG; r++) {
            uint32_t x = xs32(), sel = x & 7u;
            c.reg[r] = (uint16_t)(sel == 0 ? 0 : sel < 5 ? (x >> 8) & 0xFF : sel < 7 ? (x >> 8) & 0x1FF : x >> 16);
        }
        for (unsigned i = 0; i < DMEM_WORDS; i++) c.dmem[i] = (uint16_t)xs32();
        c.pc = (uint16_t)(xs32() % 264u);
        uint16_t w = (uint16_t)(((xs32() & 15u) << 12) | (xs32() & 0x0FFFu));
        if (c.pc < IMEM_WORDS) c.imem[c.pc] = w;
        c.sr[SR_VBAR] = VBAR;
        c.sr[SR_STATUS] = (xs32() & 7u) == 0 ? STATUS_EXL : 0;
        c.sr[SR_EPC] = 0x1234; c.sr[SR_CAUSE] = 0x0F0F; c.sr[SR_BADADDR] = 0x4321;
        snap = c;
        cpu_status_t s = cpu_step(&c);
        bool ok = true;
        if (s == CPU_DOUBLE_FAULT) {
            n_df++;
            unsigned rt = c.df_cause == EXC_SYSCALL ? 1u : 0u;
            ok = same_arch(&c, &snap) && c.retired == rt && memcmp(c.sr, snap.sr, sizeof c.sr) == 0;
        } else if (exc_total(&c) == 1) {
            unsigned cause = c.sr[SR_CAUSE] & 0xFFu, sc = cause == EXC_SYSCALL;
            if (cause < NCAUSE) seen[cause]++;
            ok = cause >= 1 && cause < NCAUSE && same_arch(&c, &snap) && c.retired == sc
              && c.sr[SR_EPC] == (uint16_t)(snap.pc + sc) && c.pc == VBAR + cause && (c.sr[SR_STATUS] & STATUS_EXL);
        } else {
            insn_t d;
            if (snap.pc < IMEM_WORDS && ref_decode_b08(w, &d)) {
                n_cmp++;
                cpu_t ref = snap;
                ok = ref_step_b09(&ref) && same_arch(&c, &ref) && c.pc == ref.pc && c.retired == ref.retired;
            }
            ok = ok && exc_total(&c) == 0 && c.retired == 1;
        }
        if (!ok && n_bad++ == 0) printf("  fuzz first mismatch: k=%u pc=0x%04X w=0x%04X\n", k, snap.pc, w);
    }
    CHECK(n_bad == 0);
    CHECK(seen[EXC_ILLEGAL] && seen[EXC_IFETCH] && seen[EXC_DIV0] && seen[EXC_ALIGN] && seen[EXC_DMEM] && seen[EXC_SYSCALL] && n_df);
    printf("  fuzz 200000: ILLEGAL %u IFETCH %u DIV0 %u ALIGN %u DMEM %u SYSCALL %u double %u, B08 vs B09 ref %u, mismatches %u\n",
           seen[EXC_ILLEGAL], seen[EXC_IFETCH], seen[EXC_DIV0], seen[EXC_ALIGN], seen[EXC_DMEM], seen[EXC_SYSCALL],
           n_df, n_cmp, n_bad);
}

/* ===================== Part C ===================== */
static void load_avg(cpu_t *c, const uint16_t *h, size_t hn) {
    static const uint16_t sum[6] = { 100, 0, 90, 7, 0, 65535 }, cnt[6] = { 10, 0, 4, 0, 0, 3 };
    cpu_load(c, prog_avg, NELEM(prog_avg));
    vec_init(c, VBAR); vec_jump(c, EXC_DIV0, 0x48); place(c, 0x48, h, hn);
    c->dmem[0] = 6;
    for (unsigned i = 0; i < 6; i++) { c->dmem[32 + i] = sum[i]; c->dmem[48 + i] = cnt[i]; }
}

static void test_part_c(void) {
    cpu_t c;
    /* C1 건너뛰기: 빈 버킷의 DIV 는 없던 일 → avg 에 센티널 0xFFFF, errcnt = 3 */
    printf("  C1 skip (EPC+1):\n");
    load_avg(&c, h_skip, NELEM(h_skip));
    CHECK(run_logged(&c, 10000, true) == CPU_HALTED);
    static const uint16_t want_skip[6] = { 10, 0xFFFF, 22, 0xFFFF, 0xFFFF, 21845 };
    CHECK(memcmp(&c.dmem[40], want_skip, sizeof want_skip) == 0 && c.dmem[27] == 3 && c.exc_count[EXC_DIV0] == 3);
    CHECK(c.op_hist[OP_DIV] == 3 && c.retired == 9u * 6 + 5 - 3 + 3 * (1 + 9));   /* 건너뛴 DIV 3개는 retire 안 함, 슬롯 JAL 1 + 핸들러 9 */
    printf("    avg = %u %u %u %u %u %u, errcnt %u, retired %llu\n", c.dmem[40], c.dmem[41], c.dmem[42],
           c.dmem[43], c.dmem[44], c.dmem[45], c.dmem[27], (unsigned long long)c.retired);

    /* C2 재시도: 제수를 1 로 고치고 같은 DIV 를 다시 → avg = sum/1, DIV 는 6 번 모두 retire */
    printf("  C2 fix + retry (EPC):\n");
    load_avg(&c, h_retry, NELEM(h_retry));
    CHECK(run_logged(&c, 10000, true) == CPU_HALTED);
    static const uint16_t want_retry[6] = { 10, 0, 22, 7, 0, 21845 };
    CHECK(memcmp(&c.dmem[40], want_retry, sizeof want_retry) == 0 && c.dmem[27] == 3);
    CHECK(c.op_hist[OP_DIV] == 6 && c.exc_count[EXC_DIV0] == 3 && c.retired == 9u * 6 + 5 + 3 * (1 + 7));
    printf("    avg = %u %u %u %u %u %u, errcnt %u, retired %llu\n", c.dmem[40], c.dmem[41], c.dmem[42],
           c.dmem[43], c.dmem[44], c.dmem[45], c.dmem[27], (unsigned long long)c.retired);

    /* C2' 원인을 안 고치는 재시도 핸들러 → 같은 DIV 에서 영원히 폴트 (재시도 폭주) */
    load_avg(&c, h_storm, NELEM(h_storm));
    CHECK(cpu_run(&c, 3000) == CPU_STEP_LIMIT && c.op_hist[OP_DIV] == 1 && c.exc_count[EXC_DIV0] > 900);
    printf("  C2' storm: 3000 steps -> DIV0 x%llu, DIV retired %llu, pc 0x%04X\n",
           (unsigned long long)c.exc_count[EXC_DIV0], (unsigned long long)c.op_hist[OP_DIV], c.pc);

    /* C3 SYSCALL 디스패처 */
    printf("  C3 syscall dispatcher:\n");
    cpu_load(&c, prog_sys_user, NELEM(prog_sys_user));
    vec_init(&c, VBAR);
    place(&c, VBAR + EXC_SYSCALL, prog_sys_handler, NELEM(prog_sys_handler));
    c.dmem[16] = 'H'; c.dmem[17] = 'I'; c.dmem[24] = 0x80;
    c.reg[4] = 0x4444; c.reg[5] = 0x5555; c.reg[7] = 0x7777;
    CHECK(run_logged(&c, 10000, true) == CPU_HALTED);
    CHECK(c.dmem[0x80] == 'H' && c.dmem[0x81] == 'I' && c.dmem[24] == 0x82);
    CHECK(c.reg[6] == 0xFFFF && c.reg[1] == 4 && c.dmem[25] == 5 && c.dmem[26] == 3);
    CHECK(c.reg[3] == 5 && c.reg[7] == 0x7777 && c.dmem[28] == 0x4444 && c.dmem[29] == 0x5555);   /* exit 은 복원 없이 멈춤 */
    CHECK(c.pc == 0x61 && (c.sr[SR_STATUS] & STATUS_EXL) && c.exc_count[EXC_SYSCALL] == 5 && c.sr[SR_EPC] == 10);
    CHECK(c.retired == 10 + 2 * 20 + 20 + 18 + 18);
    printf("    out=\"%c%c\" r6=0x%04X r1=%u exit=%u calls=%u retired=%llu\n", c.dmem[0x80], c.dmem[0x81],
           c.reg[6], c.reg[1], c.dmem[26], c.dmem[25], (unsigned long long)c.retired);
}

int main(void) {
    printf("Part A — 확장 디코드 · DIV/LDP/STP · SYSCALL/ERET\n");
    test_part_a();
    printf("Part B — 정밀 예외 · 우선순위 · VBAR 미설정 · 이중 폴트 · 퍼즈\n");
    test_part_b();
    printf("Part C — 건너뛰기 · 재시도 · SYSCALL 디스패처\n");
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
