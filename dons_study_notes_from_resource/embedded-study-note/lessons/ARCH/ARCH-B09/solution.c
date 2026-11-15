/* ARCH-B09 단일 사이클 CPU 시뮬레이터 — fetch · decode · execute · writeback
 *
 *   ISA = ARCH-B08 ISA를 그대로 넣었다 (16비트 고정 길이, r0~r7 중 r0 = 0 고정,
 *         Harvard: imem[256] / dmem[256] uint16_t, 둘 다 워드 주소, PC 는 명령 단위, 산술은 mod 2^16).
 *   Part A  cpu_step (한 명령 = 한 사이클) + cpu_run → 배열 합 · 피보나치: 최종 레지스터/메모리, 명령 수, opcode 히스토그램
 *   Part B  r0 고정, mod 2^16, read-before-write, JAL 링크, illegal / PC 범위 / dmem 범위 = 정밀 폴트, step limit
 *   Part C  memcpy (겹침 함정, RAM 끝 넘기) · 선형 검색 손 어셈블, PC 프로파일 → 핫 루프, 단일 사이클 vs 가변 클럭 시간
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== ISA (ARCH-B08 ISA를 그대로 넣었다) =====================
 * bits   15..12  11..9  8..6  5..3  2..0
 * ADD    0000    rd     rs    rt    000     rd <- rs + rt
 * ADDI   0001    rd     rs    imm6          rd <- rs + sext(imm6)            (-32..31)
 * LD     0010    rd     rs    imm6          rd <- dmem[rs + sext(imm6)]
 * ST     0011    rt     rs    imm6          dmem[rs + sext(imm6)] <- rt      (11..9 = 데이터 원본)
 * BEQ    0100    rs     rt    imm6          if rs == rt: PC <- PC + 1 + sext(imm6)
 * JAL    0101    rd     imm9                rd <- PC + 1; PC <- PC + 1 + sext(imm9)   (-256..255)
 * HALT   1111    0000 0000 0000             stop
 * 그 밖의 opcode, ADD 의 하위 3비트 != 0, HALT 하위 12비트 != 0 → illegal.
 */
enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };

typedef struct { uint8_t op, rd, rs, rt; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {               /* bits = 6 또는 9 */
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}

/* 디코더 (B08 과 같은 규칙). 필드를 "역할" 이름으로 정규화한다: 읽는 레지스터는 항상 rs/rt, 쓰는 건 rd. */
static bool decode(uint16_t w, insn_t *d) {
    unsigned a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u;
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
    default:      return false;
    }
}

/* 손 어셈블용 매크로 (어셈블러는 B08 몫). 음수 imm 은 (unsigned) 변환 = mod 2^n 으로 잘 정의된다.
 * 범위 검사는 하지 않으므로 오프셋은 주석의 라벨 계산 + 테스트로 확인한다. */
#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_JAL(rd, off)      ((uint16_t)((0x5u << 12) | ((rd) << 9) | ((unsigned)(off) & 0x1FFu)))
#define I_HALT              ((uint16_t)0xF000u)

/* ===================== CPU 상태 ===================== */
typedef enum { CPU_RUNNING, CPU_HALTED, CPU_ILLEGAL, CPU_PC_FAULT, CPU_DMEM_FAULT, CPU_STEP_LIMIT } cpu_status_t;

typedef struct {
    uint16_t reg[NREG];
    uint16_t pc;
    uint16_t imem[IMEM_WORDS];
    uint16_t dmem[DMEM_WORDS];
    cpu_status_t status;          /* RUNNING 이 아니면 멈춘 상태 (STEP_LIMIT 은 cpu_run 의 반환값일 뿐, 저장 안 함) */
    uint16_t fault_info;          /* ILLEGAL: 명령 워드 / PC_FAULT: 잘못된 PC / DMEM_FAULT: 계산된 주소 */
    uint64_t retired;             /* 완료된 명령 수 (HALT 포함, 폴트 난 명령은 제외) */
    uint64_t op_hist[16];         /* opcode 별 완료 수 */
    uint32_t pc_prof[IMEM_WORDS]; /* PC 별 완료 수 (프로파일) */
} cpu_t;

static void cpu_load(cpu_t *c, const uint16_t *prog, size_t n) {
    memset(c, 0, sizeof *c);                       /* 빈 imem = 0x0000 = ADD r0,r0,r0 (= NOP!) */
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) c->imem[i] = prog[i];
    c->status = CPU_RUNNING;
}

/* ---------------- Part A + B: 한 명령 실행 (단일 사이클) ----------------
 * 1) fetch      pc >= IMEM_WORDS 이면 CPU_PC_FAULT (fault_info = pc)
 * 2) decode     illegal 이면 CPU_ILLEGAL (fault_info = 워드). 레지스터 rs/rt 를 "먼저" 읽는다
 * 3) execute    ALU / 유효 주소 / 분기 비교 / 다음 PC. LD/ST 주소 >= DMEM_WORDS 이면 CPU_DMEM_FAULT (fault_info = 주소)
 * 4) writeback  rd != 0 일 때만 쓴다 (r0 고정)
 * 폴트는 "정밀(precise)": 레지스터·메모리·pc·retired·히스토그램 하나도 바꾸지 않고 status 만 바꾼다.
 * HALT: 완료로 센다(retired++), pc 는 HALT 자리에 그대로. 멈춘 뒤 다시 부르면 아무것도 안 하고 status 반환. */
static cpu_status_t cpu_step(cpu_t *c) {
    if (c->status != CPU_RUNNING) return c->status;
    /* 1) fetch */
    if (c->pc >= IMEM_WORDS) { c->fault_info = c->pc; return c->status = CPU_PC_FAULT; }
    uint16_t w = c->imem[c->pc];
    /* 2) decode + register read */
    insn_t d;
    if (!decode(w, &d)) { c->fault_info = w; return c->status = CPU_ILLEGAL; }
    uint16_t a = c->reg[d.rs], b = c->reg[d.rt];
    uint16_t next_pc = (uint16_t)(c->pc + 1u);
    uint16_t wb_val = 0;
    bool wb = false;
    /* 3) execute / memory */
    switch (d.op) {
    case OP_ADD:  wb_val = (uint16_t)(a + b); wb = true; break;
    case OP_ADDI: wb_val = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
    case OP_LD:
    case OP_ST: {
        uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
        if (ea >= DMEM_WORDS) { c->fault_info = ea; return c->status = CPU_DMEM_FAULT; }
        if (d.op == OP_LD) { wb_val = c->dmem[ea]; wb = true; }
        else               { c->dmem[ea] = b; }
        break;
    }
    case OP_BEQ:  if (a == b) next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_JAL:  wb_val = next_pc; wb = true; next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_HALT: c->status = CPU_HALTED; next_pc = c->pc; break;
    }
    /* 4) writeback — r0 고정 */
    if (wb && d.rd != 0) c->reg[d.rd] = wb_val;
    /* retire */
    c->retired++;
    c->op_hist[d.op]++;
    c->pc_prof[c->pc]++;
    c->pc = next_pc;
    return c->status;
}

/* 최대 max_steps 개를 실행. 멈추면 그 status, 다 썼는데도 RUNNING 이면 CPU_STEP_LIMIT (c->status 는 RUNNING 유지 → 이어서 실행 가능). */
static cpu_status_t cpu_run(cpu_t *c, uint64_t max_steps) {
    for (uint64_t i = 0; i < max_steps; i++) {
        cpu_status_t s = cpu_step(c);
        if (s != CPU_RUNNING) return s;
    }
    return c->status == CPU_RUNNING ? CPU_STEP_LIMIT : c->status;
}

/* ===================== 프로그램 (손 어셈블) ===================== */
/* 배열 합. 입력: dmem[0] = N, a[] = dmem[16..16+N-1]. 출력: dmem[1] = sum (mod 2^16), r3 = sum. 명령 수 = 6N + 6 */
static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; r1 = N            */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16     ; r2 = p = &a[0]    */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0     ; sum = 0           */
    I_BEQ(1, 0, 5),       /*  3: loop: BEQ  r1, r0, done   ; done=9 → 9-4 = +5 */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ADD(3, 3, 4),       /*  5:       ADD  r3, r3, r4                         */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1                          */
    I_ADDI(1, 1, -1),     /*  7:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -6),      /*  8:       BEQ  r0, r0, loop   ; 3-9 = -6          */
    I_ST(3, 0, 1),        /*  9: done: ST   r3, 1(r0)                          */
    I_HALT,               /* 10:       HALT                                    */
};

/* 피보나치. 입력: dmem[0] = n (<= 240). 출력: dmem[16+i] = F(i) (i < n), r2 = F(n), r3 = F(n+1) (mod 2^16). 명령 수 = 8n + 6 */
static const uint16_t prog_fib[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; r1 = n            */
    I_ADDI(2, 0, 0),      /*  1:       ADDI r2, r0, 0      ; a = F(0)          */
    I_ADDI(3, 0, 1),      /*  2:       ADDI r3, r0, 1      ; b = F(1)          */
    I_ADDI(5, 0, 16),     /*  3:       ADDI r5, r0, 16     ; p = &out[0]       */
    I_BEQ(1, 0, 7),       /*  4: loop: BEQ  r1, r0, done   ; done=12 → 12-5 = +7 */
    I_ST(2, 5, 0),        /*  5:       ST   r2, 0(r5)      ; *p = a            */
    I_ADD(4, 2, 3),       /*  6:       ADD  r4, r2, r3     ; t = a + b         */
    I_ADD(2, 3, 0),       /*  7:       ADD  r2, r3, r0     ; a = b             */
    I_ADD(3, 4, 0),       /*  8:       ADD  r3, r4, r0     ; b = t             */
    I_ADDI(5, 5, 1),      /*  9:       ADDI r5, r5, 1                          */
    I_ADDI(1, 1, -1),     /* 10:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -8),      /* 11:       BEQ  r0, r0, loop   ; 4-12 = -8         */
    I_HALT,               /* 12: done: HALT                                    */
};

/* memcpy (앞에서부터 한 워드씩). 입력: dmem[0] = n, dmem[1] = src, dmem[2] = dst. 명령 수 = 7n + 5 */
static const uint16_t prog_memcpy[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; n                 */
    I_LD(2, 0, 1),        /*  1:       LD   r2, 1(r0)      ; src               */
    I_LD(3, 0, 2),        /*  2:       LD   r3, 2(r0)      ; dst               */
    I_BEQ(1, 0, 6),       /*  3: loop: BEQ  r1, r0, done   ; done=10 → 10-4 = +6 */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ST(4, 3, 0),        /*  5:       ST   r4, 0(r3)                          */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1                          */
    I_ADDI(3, 3, 1),      /*  7:       ADDI r3, r3, 1                          */
    I_ADDI(1, 1, -1),     /*  8:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -7),      /*  9:       BEQ  r0, r0, loop   ; 3-10 = -7         */
    I_HALT,               /* 10: done: HALT                                    */
};

/* ---------------- Part C: 선형 검색 손 어셈블 ----------------
 * 입력: dmem[0] = n, dmem[1] = key, a[] = dmem[16..16+n-1]. 출력: dmem[2] = 첫 일치 인덱스, 없으면 0xFFFF.
 *   0:        LD   r1, 0(r0)      ; r1 = 남은 개수
 *   1:        LD   r2, 1(r0)      ; r2 = key
 *   2:        ADDI r3, r0, 16     ; r3 = p = &a[0]
 *   3:        ADD  r4, r0, r0     ; r4 = i = 0
 *   4: loop:  BEQ  r1, r0, nf
 *   5:        LD   r5, 0(r3)
 *   6:        BEQ  r5, r2, found
 *   7:        ADDI r3, r3, 1
 *   8:        ADDI r4, r4, 1
 *   9:        ADDI r1, r1, -1
 *  10:        BEQ  r0, r0, loop
 *  11: nf:    ADDI r4, r0, -1     ; i = 0xFFFF
 *  12: found: ST   r4, 2(r0)
 *  13:        HALT
 * 오프셋 = 목표 - (PC + 1). */
static const uint16_t prog_find[] = {
    I_LD(1, 0, 0), I_LD(2, 0, 1), I_ADDI(3, 0, 16), I_ADD(4, 0, 0),
    I_BEQ(1, 0, 6),       /*  4: nf=11    → 11-5 = +6  */
    I_LD(5, 3, 0),
    I_BEQ(5, 2, 5),       /*  6: found=12 → 12-7 = +5  */
    I_ADDI(3, 3, 1), I_ADDI(4, 4, 1), I_ADDI(1, 1, -1),
    I_BEQ(0, 0, -7),      /* 10: loop=4   → 4-11 = -7  */
    I_ADDI(4, 0, -1), I_ST(4, 0, 2), I_HALT,
};

/* ---------------- Part C: 프로파일과 시간 모델 ----------------
 * 단계별 지연 (P&H 교과서 단일 사이클 예제의 값, ps): imem 200, reg read 100, ALU 200, dmem 200, reg write 100.
 * ADD/ADDI = 200+100+200+100, LD = +dmem 200, ST = 쓰기 없음, BEQ = fetch+read+ALU, JAL = fetch+adder+write, HALT = fetch. */
static const uint32_t lat_ps[16] = {
    [OP_ADD] = 600, [OP_ADDI] = 600, [OP_LD] = 800, [OP_ST] = 700,
    [OP_BEQ] = 500, [OP_JAL] = 500, [OP_HALT] = 200,
};

/* 가장 많이 완료된 PC (동률이면 작은 PC). */
static unsigned hottest_pc(const cpu_t *c) {
    unsigned best = 0;
    for (unsigned pc = 1; pc < IMEM_WORDS; pc++)
        if (c->pc_prof[pc] > c->pc_prof[best]) best = pc;
    return best;
}

/* 단일 사이클 CPU: 클럭 주기 = ISA 에서 가장 느린 명령의 지연 (프로그램이 그 명령을 안 써도!). 총 시간 = retired × Tclk. */
static uint64_t single_cycle_ps(const cpu_t *c) {
    uint32_t tclk = 0;
    for (unsigned op = 0; op < 16; op++)
        if (lat_ps[op] > tclk) tclk = lat_ps[op];
    return c->retired * tclk;
}

/* 이상적인 "명령마다 다른 길이의 클럭": Σ op_hist[op] × lat_ps[op]. */
static uint64_t variable_clock_ps(const cpu_t *c) {
    uint64_t t = 0;
    for (unsigned op = 0; op < 16; op++) t += c->op_hist[op] * lat_ps[op];
    return t;
}

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static uint32_t rng_state = 0x2545F491u;                  /* 고정 시드 xorshift32 → 재현 가능 */
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static void report(const char *name, const cpu_t *c) {
    printf("  %-12s retired=%-5llu LD=%-4llu ST=%-3llu ADD=%-4llu ADDI=%-4llu BEQ=%-4llu JAL=%llu HALT=%llu"
           "  single=%-7llu var=%-7llu ps\n", name, (unsigned long long)c->retired,
           (unsigned long long)c->op_hist[OP_LD], (unsigned long long)c->op_hist[OP_ST],
           (unsigned long long)c->op_hist[OP_ADD], (unsigned long long)c->op_hist[OP_ADDI],
           (unsigned long long)c->op_hist[OP_BEQ], (unsigned long long)c->op_hist[OP_JAL],
           (unsigned long long)c->op_hist[OP_HALT],
           (unsigned long long)single_cycle_ps(c), (unsigned long long)variable_clock_ps(c));
}

static void test_part_a(void) {
    cpu_t c;
    /* 배열 합 N = 10, a[i] = 1000(i+1) → 55000 */
    cpu_load(&c, prog_sum, NELEM(prog_sum));
    c.dmem[0] = 10;
    for (unsigned i = 0; i < 10; i++) c.dmem[16 + i] = (uint16_t)(1000 * (i + 1));
    CHECK(cpu_run(&c, 10000) == CPU_HALTED);
    CHECK(c.dmem[1] == 55000 && c.reg[3] == 55000);
    CHECK(c.retired == 66);                                   /* 6N + 6 */
    CHECK(c.op_hist[OP_LD] == 11 && c.op_hist[OP_ADD] == 11 && c.op_hist[OP_ADDI] == 21);
    CHECK(c.op_hist[OP_BEQ] == 21 && c.op_hist[OP_ST] == 1 && c.op_hist[OP_HALT] == 1 && c.op_hist[OP_JAL] == 0);
    CHECK(c.pc == 10);                                        /* HALT 자리에 멈춤 */
    CHECK(c.reg[0] == 0 && c.reg[1] == 0 && c.reg[2] == 26 && c.reg[4] == 10000);
    report("sum N=10", &c);

    /* N = 0: 루프 0번 */
    cpu_load(&c, prog_sum, NELEM(prog_sum));
    CHECK(cpu_run(&c, 10000) == CPU_HALTED && c.dmem[1] == 0 && c.retired == 6);

    /* mod 2^16: 0xFFFF + 2 + 0x8000 = 0x18001 → 0x8001 */
    cpu_load(&c, prog_sum, NELEM(prog_sum));
    c.dmem[0] = 3; c.dmem[16] = 0xFFFF; c.dmem[17] = 2; c.dmem[18] = 0x8000;
    CHECK(cpu_run(&c, 10000) == CPU_HALTED && c.dmem[1] == 0x8001);

    /* 고정 시드 랜덤 N = 200 → C 기준값과 비교, 명령 수 6N+6 */
    cpu_load(&c, prog_sum, NELEM(prog_sum));
    uint16_t ref = 0;
    c.dmem[0] = 200;
    for (unsigned i = 0; i < 200; i++) { c.dmem[16 + i] = (uint16_t)xs32(); ref = (uint16_t)(ref + c.dmem[16 + i]); }
    CHECK(cpu_run(&c, 100000) == CPU_HALTED && c.dmem[1] == ref && c.retired == 6u * 200 + 6);
    report("sum N=200", &c);

    /* 피보나치 n = 10 */
    static const uint16_t fib10[10] = { 0, 1, 1, 2, 3, 5, 8, 13, 21, 34 };
    cpu_load(&c, prog_fib, NELEM(prog_fib));
    c.dmem[0] = 10;
    CHECK(cpu_run(&c, 10000) == CPU_HALTED);
    CHECK(memcmp(&c.dmem[16], fib10, sizeof fib10) == 0 && c.dmem[26] == 0);
    CHECK(c.reg[2] == 55 && c.reg[3] == 89 && c.retired == 86);   /* 8n + 6 */
    report("fib n=10", &c);

    /* 피보나치 n = 0..24 전부 C 기준값과 비교 (n = 24 에서 F(25) = 75025 가 16비트를 넘는다) */
    bool all = true;
    for (unsigned n = 0; n <= 24; n++) {
        cpu_load(&c, prog_fib, NELEM(prog_fib));
        c.dmem[0] = (uint16_t)n;
        uint16_t fa = 0, fb = 1;
        bool ok = cpu_run(&c, 10000) == CPU_HALTED && c.retired == 8u * n + 6;
        for (unsigned i = 0; i < n; i++) {
            ok = ok && c.dmem[16 + i] == fa;
            uint16_t t = (uint16_t)(fa + fb); fa = fb; fb = t;
        }
        ok = ok && c.reg[2] == fa && c.reg[3] == fb;
        if (!ok && all) printf("  fib: first mismatch at n=%u\n", n);
        all = all && ok;
    }
    CHECK(all);
    CHECK(c.reg[2] == 46368 && c.reg[3] == 9489);             /* F(24), F(25) mod 65536 */
    report("fib n=24", &c);
}

static void test_part_b(void) {
    cpu_t c;
    /* r0 고정: 쓰기는 버려지고, 읽으면 항상 0 */
    static const uint16_t p_r0[] = {
        I_ADDI(0, 0, 5),      /* 0: ADDI r0, r0, 5   → 무시      */
        I_ADD(1, 0, 0),       /* 1: ADD  r1, r0, r0  → 0         */
        I_LD(0, 0, 0),        /* 2: LD   r0, 0(r0)   → 무시      */
        I_ADD(2, 0, 0),       /* 3: ADD  r2, r0, r0  → 0         */
        I_JAL(0, 0),          /* 4: JAL  r0, +0      → 그냥 다음 */
        I_HALT,
    };
    cpu_load(&c, p_r0, NELEM(p_r0));
    c.dmem[0] = 0x1234;
    CHECK(cpu_run(&c, 100) == CPU_HALTED);
    CHECK(c.reg[0] == 0 && c.reg[1] == 0 && c.reg[2] == 0 && c.retired == 6);

    /* mod 2^16 와 부호 확장 */
    static const uint16_t p_wrap[] = {
        I_LD(1, 0, 0),        /* r1 = 0x7FFF                 */
        I_ADDI(1, 1, 1),      /* r1 = 0x8000 (부호 없는 덧셈)  */
        I_ADDI(2, 0, -32),    /* r2 = 0xFFE0                 */
        I_ADDI(3, 0, 31),     /* r3 = 31                     */
        I_ADDI(4, 0, -1),     /* r4 = 0xFFFF                 */
        I_ADDI(4, 4, 1),      /* r4 = 0                      */
        I_HALT,
    };
    cpu_load(&c, p_wrap, NELEM(p_wrap));
    c.dmem[0] = 0x7FFF;
    CHECK(cpu_run(&c, 100) == CPU_HALTED);
    CHECK(c.reg[1] == 0x8000 && c.reg[2] == 0xFFE0 && c.reg[3] == 31 && c.reg[4] == 0);

    /* read-before-write: 같은 레지스터를 읽고 쓰는 명령 */
    static const uint16_t p_rbw[] = {
        I_ADDI(1, 0, 5),      /* r1 = 5                  */
        I_LD(1, 1, 0),        /* r1 = dmem[r1] = 7       */
        I_ADD(1, 1, 1),       /* r1 = 14                 */
        I_ST(1, 1, 0),        /* dmem[14] = 14           */
        I_HALT,
    };
    cpu_load(&c, p_rbw, NELEM(p_rbw));
    c.dmem[5] = 7;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.reg[1] == 14 && c.dmem[14] == 14);

    /* JAL: 링크 = PC + 1, 목표 = PC + 1 + off */
    static const uint16_t p_jal[] = {
        I_JAL(7, 2),          /* 0: JAL r7, +2 → r7 = 1, pc = 3 */
        I_ADDI(1, 0, 1),      /* 1: (건너뜀) */
        I_ADDI(1, 0, 2),      /* 2: (건너뜀) */
        I_HALT,               /* 3 */
    };
    cpu_load(&c, p_jal, NELEM(p_jal));
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.reg[7] == 1 && c.reg[1] == 0 && c.pc == 3 && c.retired == 2);

    /* step limit: BEQ r0, r0, -1 = 제자리 무한 루프. STEP_LIMIT 은 상태를 망치지 않는다 → 이어서 실행 가능 */
    static const uint16_t p_spin[] = { I_BEQ(0, 0, -1) };
    cpu_load(&c, p_spin, NELEM(p_spin));
    CHECK(cpu_run(&c, 1000) == CPU_STEP_LIMIT && c.retired == 1000 && c.pc == 0 && c.status == CPU_RUNNING);
    CHECK(cpu_run(&c, 5) == CPU_STEP_LIMIT && c.retired == 1005);

    /* step limit 경계: 3번째 명령이 HALT 이면 max=2 → LIMIT, 이어서 1 → HALTED. 멈춘 뒤 step 은 아무것도 안 함 */
    static const uint16_t p_three[] = { I_ADDI(1, 0, 1), I_ADDI(1, 1, 1), I_HALT };
    cpu_load(&c, p_three, NELEM(p_three));
    CHECK(cpu_run(&c, 2) == CPU_STEP_LIMIT && c.retired == 2);
    CHECK(cpu_run(&c, 1) == CPU_HALTED && c.retired == 3 && c.pc == 2);
    CHECK(cpu_step(&c) == CPU_HALTED && c.retired == 3 && c.op_hist[OP_HALT] == 1);

    /* illegal 은 정밀 폴트: pc 는 그 명령, 레지스터·카운터 변화 없음, fault_info = 워드 */
    static const uint16_t p_ill[] = { I_ADDI(1, 0, 9), 0x6000, I_ADDI(1, 0, 3), I_HALT };
    cpu_load(&c, p_ill, NELEM(p_ill));
    CHECK(cpu_run(&c, 100) == CPU_ILLEGAL && c.pc == 1 && c.fault_info == 0x6000);
    CHECK(c.reg[1] == 9 && c.retired == 1 && c.pc_prof[1] == 0);
    CHECK(cpu_step(&c) == CPU_ILLEGAL && c.retired == 1);     /* 폴트는 끈적하다(sticky) */

    /* 그 밖의 illegal 워드: ADD funct != 0, HALT 하위 비트 != 0, 지워진 플래시 0xFFFF, opcode 6..14 전부 */
    static const uint16_t bad[] = { 0x0001, 0x0E07, 0xF001, 0xF800, 0xFFFF };
    bool all_bad = true;
    for (unsigned i = 0; i < NELEM(bad); i++) {
        cpu_load(&c, &bad[i], 1);
        all_bad = all_bad && cpu_run(&c, 10) == CPU_ILLEGAL && c.fault_info == bad[i] && c.retired == 0;
    }
    for (unsigned op = 6; op <= 14; op++) {
        uint16_t w = (uint16_t)(op << 12);
        cpu_load(&c, &w, 1);
        all_bad = all_bad && cpu_run(&c, 10) == CPU_ILLEGAL;
    }
    CHECK(all_bad);

    /* 프로그램 끝에서 떨어지기: 빈 imem 은 0x0000 = ADD r0,r0,r0 = NOP → 255개 NOP 후 pc = 256 에서 PC_FAULT */
    static const uint16_t p_fall[] = { I_ADDI(1, 0, 3) };
    cpu_load(&c, p_fall, NELEM(p_fall));
    CHECK(cpu_run(&c, 100000) == CPU_PC_FAULT && c.fault_info == 256 && c.pc == 256);
    CHECK(c.retired == 256 && c.op_hist[OP_ADD] == 255 && c.reg[1] == 3);
    /* 같은 상황에서 빈 자리를 지워진 플래시(0xFFFF)로 채우면 즉시 ILLEGAL */
    cpu_load(&c, p_fall, NELEM(p_fall));
    for (unsigned i = 1; i < IMEM_WORDS; i++) c.imem[i] = 0xFFFF;
    CHECK(cpu_run(&c, 100000) == CPU_ILLEGAL && c.pc == 1 && c.retired == 1);

    /* 음수 쪽으로 분기: pc = 0 + 1 - 2 = 0xFFFF (mod 2^16) → 다음 fetch 에서 PC_FAULT. 분기 자체는 완료됨 */
    static const uint16_t p_neg[] = { I_BEQ(0, 0, -2) };
    cpu_load(&c, p_neg, NELEM(p_neg));
    CHECK(cpu_run(&c, 10) == CPU_PC_FAULT && c.fault_info == 0xFFFF && c.retired == 1);

    /* dmem 범위: 250 + 6 = 256 → DMEM_FAULT, 저장은 일어나지 않음. 0 + (-1) = 0xFFFF 도 폴트 */
    static const uint16_t p_dm[] = { I_LD(1, 0, 0), I_ST(1, 1, 6), I_HALT };
    cpu_load(&c, p_dm, NELEM(p_dm));
    c.dmem[0] = 250;
    CHECK(cpu_run(&c, 10) == CPU_DMEM_FAULT && c.fault_info == 256 && c.pc == 1 && c.retired == 1);
    static const uint16_t p_dm2[] = { I_ADDI(2, 0, 7), I_LD(2, 0, -1), I_HALT };
    cpu_load(&c, p_dm2, NELEM(p_dm2));
    CHECK(cpu_run(&c, 10) == CPU_DMEM_FAULT && c.fault_info == 0xFFFF && c.reg[2] == 7 && c.pc == 1);
}

static void test_part_c(void) {
    cpu_t c;
    /* memcpy 8 워드 16 → 64, 주변 센티널은 그대로 */
    cpu_load(&c, prog_memcpy, NELEM(prog_memcpy));
    c.dmem[0] = 8; c.dmem[1] = 16; c.dmem[2] = 64;
    for (unsigned i = 0; i < 8; i++) c.dmem[16 + i] = (uint16_t)xs32();
    c.dmem[63] = 0xBEEF; c.dmem[72] = 0xBEEF;
    CHECK(cpu_run(&c, 10000) == CPU_HALTED);
    CHECK(memcmp(&c.dmem[64], &c.dmem[16], 8 * sizeof(uint16_t)) == 0 && c.dmem[63] == 0xBEEF && c.dmem[72] == 0xBEEF);
    CHECK(c.retired == 7u * 8 + 5);
    report("memcpy n=8", &c);

    /* 겹치는 복사 dst = src + 1: 앞에서부터 복사하면 a[0] 이 번진다 (memmove 가 아님) */
    cpu_load(&c, prog_memcpy, NELEM(prog_memcpy));
    c.dmem[0] = 4; c.dmem[1] = 16; c.dmem[2] = 17;
    c.dmem[16] = 1; c.dmem[17] = 2; c.dmem[18] = 3; c.dmem[19] = 4;
    CHECK(cpu_run(&c, 10000) == CPU_HALTED);
    CHECK(c.dmem[16] == 1 && c.dmem[17] == 1 && c.dmem[18] == 1 && c.dmem[19] == 1 && c.dmem[20] == 1);

    /* RAM 끝을 넘는 복사: 254, 255 는 써지고 256 에서 정밀 폴트 (pc = ST 자리) */
    cpu_load(&c, prog_memcpy, NELEM(prog_memcpy));
    c.dmem[0] = 4; c.dmem[1] = 16; c.dmem[2] = 254;
    c.dmem[16] = 0xA1; c.dmem[17] = 0xA2; c.dmem[18] = 0xA3;
    CHECK(cpu_run(&c, 10000) == CPU_DMEM_FAULT && c.fault_info == 256 && c.pc == 5);
    CHECK(c.dmem[254] == 0xA1 && c.dmem[255] == 0xA2 && c.retired == 19 && c.reg[4] == 0xA3);

    /* 선형 검색 (손 어셈블) */
    static const uint16_t arr[10] = { 9, 4, 7, 7, 0xFFFF, 3, 8, 3, 1, 2 };
    struct { uint16_t n, key, want; uint64_t steps; } fc[] = {
        { 10, 3, 5, 7 * 5 + 9 },        /* 첫 번째 3 (인덱스 5, 7 도 3) */
        { 10, 9, 0, 9 },                /* 맨 앞 */
        { 10, 0xFFFF, 4, 7 * 4 + 9 },   /* 키 자체가 0xFFFF */
        { 0, 9, 0xFFFF, 8 },            /* 빈 배열 */
        { 10, 42, 0xFFFF, 7 * 10 + 8 }, /* 없음 (마지막 → 아래 report) */
    };
    bool find_ok = true;
    for (unsigned k = 0; k < NELEM(fc); k++) {
        cpu_load(&c, prog_find, NELEM(prog_find));
        c.dmem[0] = fc[k].n; c.dmem[1] = fc[k].key;
        memcpy(&c.dmem[16], arr, sizeof arr);
        bool ok = cpu_run(&c, 10000) == CPU_HALTED && c.dmem[2] == fc[k].want && c.retired == fc[k].steps;
        if (!ok) printf("  find case %u: dmem[2]=0x%04X retired=%llu\n", k, c.dmem[2], (unsigned long long)c.retired);
        find_ok = find_ok && ok;
    }
    CHECK(find_ok);
    report("find miss", &c);   /* n=10, 키 없음 */

    /* 프로파일: 배열 합 N=10 → 루프 머리 BEQ(pc 3) 가 N+1 = 11 번으로 가장 뜨겁다. PC 별 합 = retired */
    cpu_load(&c, prog_sum, NELEM(prog_sum));
    c.dmem[0] = 10;
    cpu_run(&c, 10000);
    uint64_t prof_sum = 0;
    for (unsigned pc = 0; pc < IMEM_WORDS; pc++) prof_sum += c.pc_prof[pc];
    CHECK(hottest_pc(&c) == 3 && c.pc_prof[3] == 11 && c.pc_prof[4] == 10 && prof_sum == c.retired);

    /* 시간 모델: Tclk = 800 ps (LD) → 66 × 800 = 52800 ps, 가변 클럭 = 39400 ps */
    CHECK(single_cycle_ps(&c) == 52800);
    CHECK(variable_clock_ps(&c) == 39400);
    /* LD 가 하나도 없는 프로그램이어도 Tclk 는 800 ps 그대로 */
    static const uint16_t p_alu[] = { I_ADDI(1, 0, 1), I_ADD(2, 1, 1), I_HALT };
    cpu_load(&c, p_alu, NELEM(p_alu));
    cpu_run(&c, 10);
    CHECK(single_cycle_ps(&c) == 3 * 800 && variable_clock_ps(&c) == 600 + 600 + 200);
}

int main(void) {
    printf("Part A — 배열 합 · 피보나치\n");
    test_part_a();
    printf("Part B — r0 · wrap · 폴트 · step limit\n");
    test_part_b();
    printf("Part C — memcpy · 선형 검색 · 프로파일 · 시간 모델\n");
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
