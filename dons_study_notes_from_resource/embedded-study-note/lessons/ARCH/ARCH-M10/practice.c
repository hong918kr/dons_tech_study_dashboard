/* ARCH-M10 추가 연습 — 한 번에 검증
 *
 *   연습 1  원인별 벡터 vs 단일 진입점(direct) + 소프트웨어 CAUSE 디스패치: 예외당 명령 수 비교
 *   연습 2  중첩 예외를 허용하는 SYSCALL 핸들러 (EPC 저장 → EXL 해제 → … → EXL 설정 → EPC 복원) vs EPC 를 잃는 버그
 *   연습 3  STP(전부 아니면 없음) vs ST 두 번(절반만 써짐): 페어 정렬 규칙이 주는 것
 *   연습 4  예외 테이블 fixup (Linux __ex_table 방식) — LDP 로 (fault_pc, fixup_pc) 표 검색
 *
 * CPU = solution.c 와 같은 모델 (+ 연습 1 의 vec_direct 스위치).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
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
    /* ---- M10 확장 ---- */
    case OP_DIV:  if (w & 7u) return false;
                  d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->rt = (uint8_t)c; return true;
    case OP_LDP:  if (a & 1u) return false;
                  d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_STP:  if (a & 1u) return false;
                  d->rt = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_SYSCALL: if (w & 0x0F00u) return false;
                  d->imm = (int16_t)(w & 0xFFu); return true;
    case OP_ERET: return (w & 0x0FFFu) == 0;
    case OP_MFSR: if ((w & 0x01F8u) || s >= NSR) return false;
                  d->rd = (uint8_t)a; d->sr = (uint8_t)s; return true;
    case OP_MTSR: if ((w & 0x0E38u) || s >= NSR) return false;
                  d->rs = (uint8_t)b; d->sr = (uint8_t)s; return true;
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
    bool     vec_direct;          /* 연습 1: true 면 모든 예외가 VBAR 한 곳으로 (RISC-V mtvec direct 모드처럼) */
} cpu_t;

static void cpu_load(cpu_t *c, const uint16_t *prog, size_t n) {
    memset(c, 0, sizeof *c);                       /* 빈 imem = 0x0000 = ADD r0,r0,r0 = NOP */
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) c->imem[i] = prog[i];
    c->sr[SR_VBAR] = VBAR_NONE;
    c->status = CPU_RUNNING;
}

/* ---------------- 예외 진입 (본문과 같음 + 연습 1 의 direct 모드) ----------------
 * cause = 원인 코드, badaddr = BADADDR 에 넣을 값, epc = 복귀 주소 (폴트: 그 명령의 pc / 트랩: pc + 1),
 * iss = 원인별 부가 정보 (SYSCALL 번호; 나머지는 0) → CAUSE = iss << 8 | cause.
 *  1) 이미 핸들러 안(STATUS.EXL = 1)이면 이중 폴트: df_cause/df_epc 만 기록하고 CPU_DOUBLE_FAULT 로 멈춘다.
 *     EPC/CAUSE/BADADDR 는 첫 예외 값 그대로 (디버거가 원래 원인을 볼 수 있게).
 *  2) EPC, CAUSE, BADADDR 를 쓰고 exc_count[cause]++.
 *  3) VBAR == VBAR_NONE 이면 갈 곳이 없다: pc <- epc, CPU_UNHANDLED 로 멈춘다 (B09 의 정밀 폴트 정지와 같은 모양).
 *  4) STATUS.EXL <- 1, pc <- VBAR + cause, CPU_RUNNING 을 돌려준다.
 * r0..r7 · dmem · retired 는 절대 건드리지 않는다. */
static cpu_status_t take_exception(cpu_t *c, unsigned cause, uint16_t badaddr, uint16_t epc, uint8_t iss) {
    if (c->sr[SR_STATUS] & STATUS_EXL) {
        c->df_cause = (uint8_t)cause;
        c->df_epc = epc;
        return c->status = CPU_DOUBLE_FAULT;
    }
    c->sr[SR_EPC] = epc;
    c->sr[SR_CAUSE] = (uint16_t)((unsigned)iss << 8 | cause);
    c->sr[SR_BADADDR] = badaddr;
    c->exc_count[cause]++;
    if (c->sr[SR_VBAR] == VBAR_NONE) { c->pc = epc; return c->status = CPU_UNHANDLED; }
    c->sr[SR_STATUS] |= STATUS_EXL;
    c->pc = (uint16_t)(c->sr[SR_VBAR] + (c->vec_direct ? 0u : cause));
    return CPU_RUNNING;
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
    /* ---- M10 확장 ---- */
    case OP_DIV:
        if (b == 0) return take_exception(c, EXC_DIV0, 0, pc, 0);
        wb_val = (uint16_t)(a / b); wb = true;
        break;
    case OP_LDP:
    case OP_STP: {
        uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
        if (ea & 1u)          return take_exception(c, EXC_ALIGN, ea, pc, 0);   /* 정렬 검사가 범위 검사보다 먼저 */
        if (ea >= DMEM_WORDS) return take_exception(c, EXC_DMEM, ea, pc, 0);    /* 짝수 ea <= 254 → ea+1 <= 255 도 안전 */
        if (d.op == OP_LDP) { wb_val = c->dmem[ea]; wb2_val = c->dmem[ea + 1u]; wb = wb2 = true; }
        else                { c->dmem[ea] = b; c->dmem[ea + 1u] = b2; }
        break;
    }
    case OP_SYSCALL: trap = true; break;
    case OP_ERET:
        if (!(c->sr[SR_STATUS] & STATUS_EXL)) return take_exception(c, EXC_ILLEGAL, w, pc, 0);
        next_pc = c->sr[SR_EPC];
        c->sr[SR_STATUS] &= (uint16_t)~STATUS_EXL;
        break;
    case OP_MFSR: wb_val = c->sr[d.sr]; wb = true; break;
    case OP_MTSR: c->sr[d.sr] = d.sr == SR_STATUS ? (uint16_t)(a & STATUS_EXL) : a; break;
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

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 공용 핸들러: DIV0 건너뛰기 (본문 C1 과 같음, 위치만 인자로) */
static const uint16_t h_skip[] = {
    I_STP(4, 0, 28), I_LD(4, 0, 27), I_ADDI(4, 4, 1), I_ST(4, 0, 27),
    I_MFSR(4, SR_EPC), I_ADDI(4, 4, 1), I_MTSR(SR_EPC, 4), I_LDP(4, 0, 28), I_ERET,
};

/* ---------------- 연습 1: 원인별 벡터 vs 단일 진입점(direct) + CAUSE 디스패치 ---------------- */
static const uint16_t prog_mix[] = {
    I_ADDI(1, 0, 5),        /* 0: ADDI r1, r0, 5        */
    I_ADDI(5, 0, -1),       /* 1: ADDI r5, r0, -1       */
    I_DIV(5, 1, 0),         /* 2: DIV  r5, r1, r0  → DIV0 (건너뜀) */
    I_SYSCALL(0),           /* 3: SYSCALL 0             */
    I_SYSCALL(0),           /* 4: SYSCALL 0             */
    I_DIV(6, 1, 0),         /* 5: DIV  r6, r1, r0  → DIV0 (건너뜀) */
    I_HALT,                 /* 6                        */
};
/* 원인별 벡터: SYSCALL 슬롯(0x46)에서 바로 시작하는 호출 카운터 */
static const uint16_t h_sys_count[] = {
    I_STP(4, 0, 28), I_LD(4, 0, 25), I_ADDI(4, 4, 1), I_ST(4, 0, 25), I_LDP(4, 0, 28), I_ERET,
};
/* direct 모드: 모든 예외가 VBAR(0x40) 한 곳으로 → 소프트웨어가 CAUSE 로 갈라 준다.
 * (SYSCALL 0 만 다루므로 CAUSE == 6 정확히. 번호가 있으면 CAUSE 하위 8비트만 비교해야 한다) */
static const uint16_t h_direct[] = {
    I_STP(4, 0, 28),        /* 0x40: STP  r4, 28(r0)                        */
    I_MFSR(4, SR_CAUSE),    /* 0x41: MFSR r4, CAUSE                         */
    I_ADDI(4, 4, -3),       /* 0x42: ADDI r4, r4, -3                        */
    I_BEQ(4, 0, 3),         /* 0x43: BEQ  r4, r0, div0  ; 0x47 - 0x44 = +3  */
    I_ADDI(4, 4, -3),       /* 0x44: ADDI r4, r4, -3                        */
    I_BEQ(4, 0, 9),         /* 0x45: BEQ  r4, r0, sys   ; 0x4F - 0x46 = +9  */
    I_HALT,                 /* 0x46: 모르는 원인 → 멈춤                     */
    I_LD(4, 0, 27), I_ADDI(4, 4, 1), I_ST(4, 0, 27),               /* 0x47 div0: errcnt++  */
    I_MFSR(4, SR_EPC), I_ADDI(4, 4, 1), I_MTSR(SR_EPC, 4),         /* 0x4A: EPC + 1        */
    I_LDP(4, 0, 28), I_ERET,                                       /* 0x4D: 복원, 복귀     */
    I_LD(4, 0, 25), I_ADDI(4, 4, 1), I_ST(4, 0, 25),               /* 0x4F sys: 호출 수++  */
    I_LDP(4, 0, 28), I_ERET,                                       /* 0x52                 */
};

static void test_ex1(void) {
    cpu_t c;
    uint64_t r_vec, r_dir;
    /* 원인별 벡터 */
    cpu_load(&c, prog_mix, NELEM(prog_mix));
    vec_init(&c, VBAR); vec_jump(&c, EXC_DIV0, 0x50);
    place(&c, 0x50, h_skip, NELEM(h_skip));
    place(&c, VBAR + EXC_SYSCALL, h_sys_count, NELEM(h_sys_count));
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[27] == 2 && c.dmem[25] == 2 && c.reg[5] == 0xFFFF);
    r_vec = c.retired;
    CHECK(r_vec == 5 + 2 * (1 + 9) + 2 * 6);
    /* direct */
    cpu_load(&c, prog_mix, NELEM(prog_mix));
    c.vec_direct = true;
    c.sr[SR_VBAR] = VBAR;
    place(&c, VBAR, h_direct, NELEM(h_direct));
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[27] == 2 && c.dmem[25] == 2 && c.reg[5] == 0xFFFF);
    r_dir = c.retired;
    CHECK(r_dir == 5 + 2 * (4 + 8) + 2 * (6 + 5));
    printf("  ex1: vectored %llu retired (DIV0 10, SYSCALL 6 each), direct %llu retired (DIV0 12, SYSCALL 11 each)\n",
           (unsigned long long)r_vec, (unsigned long long)r_dir);
}

/* ---------------- 연습 2: 중첩 예외를 허용하는 SYSCALL 핸들러 ----------------
 * SYSCALL 0 = r1 ← r2 / r3 (제수 0 이면 0xFFFF). 핸들러 안의 DIV 가 DIV0 를 낼 수 있다 → 중첩.
 * 규약: r4 는 커널이 써도 되는 레지스터. dmem[31] = 저장한 EPC. */
static const uint16_t prog_sdiv[] = {
    I_LD(2, 0, 0), I_LD(3, 0, 1), I_SYSCALL(0), I_ST(1, 0, 2), I_HALT,
};
static const uint16_t h_nest_ok[] = {      /* 0x46 */
    I_MFSR(4, SR_EPC),      /* 0x46: MFSR r4, EPC                               */
    I_ST(4, 0, 31),         /* 0x47: ST   r4, 31(r0)   ; 중첩 예외가 덮기 전에 저장 */
    I_MTSR(SR_STATUS, 0),   /* 0x48: MTSR STATUS, r0   ; EXL = 0 → 중첩 허용      */
    I_ADDI(1, 0, -1),       /* 0x49: ADDI r1, r0, -1                            */
    I_DIV(1, 2, 3),         /* 0x4A: DIV  r1, r2, r3   ; ← 중첩 DIV0 가능        */
    I_ADDI(4, 0, 1),        /* 0x4B: ADDI r4, r0, 1                             */
    I_MTSR(SR_STATUS, 4),   /* 0x4C: MTSR STATUS, r4   ; EXL = 1 을 "먼저"       */
    I_LD(4, 0, 31),         /* 0x4D: LD   r4, 31(r0)                            */
    I_MTSR(SR_EPC, 4),      /* 0x4E: MTSR EPC, r4      ; 그다음 EPC 복원         */
    I_ERET,                 /* 0x4F                                             */
};
static const uint16_t h_nest_bug[] = {     /* EPC 를 저장/복원하지 않는 버전 */
    I_MTSR(SR_STATUS, 0), I_ADDI(1, 0, -1), I_DIV(1, 2, 3), I_ADDI(4, 0, 1), I_MTSR(SR_STATUS, 4), I_ERET,
};
static void load_sdiv(cpu_t *c, const uint16_t *h, size_t hn, uint16_t a, uint16_t b) {
    cpu_load(c, prog_sdiv, NELEM(prog_sdiv));
    vec_init(c, VBAR); vec_jump(c, EXC_DIV0, 0x60);
    place(c, 0x60, h_skip, NELEM(h_skip));
    place(c, VBAR + EXC_SYSCALL, h, hn);
    c->dmem[0] = a; c->dmem[1] = b;
}
static void test_ex2(void) {
    cpu_t c;
    load_sdiv(&c, h_nest_ok, NELEM(h_nest_ok), 100, 7);
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[2] == 14 && c.pc == 4 && c.exc_count[EXC_DIV0] == 0);
    load_sdiv(&c, h_nest_ok, NELEM(h_nest_ok), 100, 0);
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[2] == 0xFFFF && c.pc == 4 && c.dmem[27] == 1);
    CHECK(c.exc_count[EXC_DIV0] == 1 && c.exc_count[EXC_SYSCALL] == 1 && !(c.sr[SR_STATUS] & STATUS_EXL));
    uint16_t nested_val = c.dmem[2];
    /* 버그 버전: 중첩이 없으면 멀쩡하다 */
    load_sdiv(&c, h_nest_bug, NELEM(h_nest_bug), 100, 7);
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[2] == 14);
    /* 중첩 DIV0 가 EPC 를 덮어써서 ERET 가 핸들러 안으로 돌아간다 → 빠져나오지 못함 */
    load_sdiv(&c, h_nest_bug, NELEM(h_nest_bug), 100, 0);
    cpu_status_t s = cpu_run(&c, 1000);
    CHECK(s == CPU_STEP_LIMIT && c.sr[SR_EPC] == 0x49 && c.dmem[2] == 0);
    printf("  ex2: nested ok -> dmem[2]=0x%04X; buggy handler: %s, EPC stuck at 0x%04X (inside handler)\n",
           nested_val, s == CPU_STEP_LIMIT ? "STEP_LIMIT" : "?", c.sr[SR_EPC]);
}

/* ---------------- 연습 3: STP(전부 또는 없음) vs ST 두 번(절반만 써질 수 있음) ---------------- */
static void test_ex3(void) {
    cpu_t c;
    static const uint16_t p_stp[] = { I_LD(1, 0, 0), I_ADDI(2, 0, 1), I_ADDI(3, 0, 2), I_STP(2, 1, 0), I_HALT };
    static const uint16_t p_2st[] = { I_LD(1, 0, 0), I_ADDI(2, 0, 1), I_ADDI(3, 0, 2), I_ST(2, 1, 0), I_ST(3, 1, 1), I_HALT };
    cpu_load(&c, p_stp, NELEM(p_stp)); c.dmem[0] = 255; c.dmem[255] = 0xEEEE;
    CHECK(cpu_run(&c, 100) == CPU_UNHANDLED && c.sr[SR_CAUSE] == EXC_ALIGN && c.dmem[255] == 0xEEEE);
    const char *n1 = cause_name[c.sr[SR_CAUSE] & 0xFF];
    cpu_load(&c, p_2st, NELEM(p_2st)); c.dmem[0] = 255; c.dmem[255] = 0xEEEE;
    CHECK(cpu_run(&c, 100) == CPU_UNHANDLED && c.sr[SR_CAUSE] == EXC_DMEM && c.sr[SR_BADADDR] == 256 && c.dmem[255] == 1);
    const char *n2 = cause_name[c.sr[SR_CAUSE] & 0xFF];
    cpu_load(&c, p_stp, NELEM(p_stp)); c.dmem[0] = 254;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.dmem[254] == 1 && c.dmem[255] == 2);
    printf("  ex3: STP @255 -> %s, dmem[255] untouched; ST,ST @255 -> %s @256 after dmem[255]=1 (torn)\n", n1, n2);
}

/* ---------------- 연습 4: 예외 테이블 fixup (Linux __ex_table 과 같은 생각) ----------------
 * dmem[16..]: (fault_pc, fixup_pc) 쌍, 끝 = (0xFFFF, -). DMEM 폴트가 표에 있는 pc 에서 나면 EPC ← fixup, r1 = 0xFFFF.
 * 표에 없으면 oops (핸들러 안에서 HALT). 핸들러는 r3..r6 을 쓴다 (커널 규약). */
static const uint16_t h_fixup[] = {
    I_MFSR(3, SR_EPC),      /* 0x48: MFSR r3, EPC                            */
    I_ADDI(6, 0, 16),       /* 0x49: ADDI r6, r0, 16    ; 표 시작           */
    I_LDP(4, 6, 0),         /* 0x4A: loop: LDP r4, 0(r6) ; r4 = fault_pc, r5 = fixup */
    I_BEQ(4, 3, 5),         /* 0x4B: BEQ  r4, r3, found ; 0x51 - 0x4C = +5  */
    I_ADDI(4, 4, 1),        /* 0x4C: ADDI r4, r4, 1     ; 0xFFFF + 1 = 0    */
    I_BEQ(4, 0, 2),         /* 0x4D: BEQ  r4, r0, oops  ; 0x50 - 0x4E = +2  */
    I_ADDI(6, 6, 2),        /* 0x4E: ADDI r6, r6, 2                         */
    I_BEQ(0, 0, -6),        /* 0x4F: BEQ  r0, r0, loop  ; 0x4A - 0x50 = -6  */
    I_HALT,                 /* 0x50: oops                                   */
    I_MTSR(SR_EPC, 5),      /* 0x51: found: MTSR EPC, r5                    */
    I_ADDI(1, 0, -1),       /* 0x52: ADDI r1, r0, -1    ; -EFAULT 같은 것    */
    I_ERET,                 /* 0x53                                         */
};
static const uint16_t prog_getuser[] = {
    I_LD(2, 0, 0),          /* 0: LD  r2, 0(r0)   ; 좋은 포인터            */
    I_ADD(1, 0, 0),         /* 1                                          */
    I_LD(1, 2, 0),          /* 2: get_user #1     ; 표: 2 → 3              */
    I_ST(1, 0, 2),          /* 3: ST  r1, 2(r0)                           */
    I_LD(2, 0, 1),          /* 4: LD  r2, 1(r0)   ; 나쁜 포인터            */
    I_LD(1, 2, 0),          /* 5: get_user #2     ; 표: 5 → 6              */
    I_ST(1, 0, 3),          /* 6: ST  r1, 3(r0)                           */
    I_LD(1, 0, -1),         /* 7: 표에 없는 폴트 → oops                    */
    I_HALT,
};
static void test_ex4(void) {
    cpu_t c;
    cpu_load(&c, prog_getuser, NELEM(prog_getuser));
    vec_init(&c, VBAR); vec_jump(&c, EXC_DMEM, 0x48); place(&c, 0x48, h_fixup, NELEM(h_fixup));
    c.dmem[0] = 40; c.dmem[1] = 0xFFF0; c.dmem[40] = 0xCAFE;
    static const uint16_t table[] = { 2, 3, 5, 6, 0xFFFF, 0 };
    memcpy(&c.dmem[16], table, sizeof table);
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[2] == 0xCAFE && c.dmem[3] == 0xFFFF);
    CHECK(c.pc == 0x50 && (c.sr[SR_STATUS] & STATUS_EXL) && c.sr[SR_EPC] == 7 && c.sr[SR_BADADDR] == 0xFFFF);
    CHECK(c.exc_count[EXC_DMEM] == 2);
    printf("  ex4: get_user good=0x%04X bad=0x%04X, unlisted fault -> oops at pc 0x%04X (EPC %u)\n",
           c.dmem[2], c.dmem[3], c.pc, c.sr[SR_EPC]);
}

int main(void) {
    test_ex1();
    test_ex2();
    test_ex3();
    test_ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
