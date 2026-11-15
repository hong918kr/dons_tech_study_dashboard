/* ARCH-B09 추가 연습 — 한 번에 검증
 *
 *   연습 1  루프 2배 펼치기: 배열 합 6N+6 → 4N+6 (BNE 가 없어서 "루프 회전" 은 이득이 없다)
 *   연습 2  MMIO UART: 주소 0xFFFF 를 UART 로 매핑 (ST = 송신, LD = 상태), "OK\n" 출력 + 폴링
 *   연습 3  하드웨어 브레이크포인트: run_until(bp) + 브레이크포인트에서 이어가기(step over)
 *   연습 4  ISA 확장 SLTU (ADD funct=001) + 확장 없는 코어에서 illegal 트랩 → 소프트웨어 에뮬레이션
 *
 * ISA = ARCH-B08 ISA를 그대로 넣었다 (solution.c 와 같은 인터프리터 + 연습용 스위치 2개).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256, UART_ADDR = 0xFFFF };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };

typedef struct { uint8_t op, rd, rs, rt, funct; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}

/* allow_sltu: 연습 4 의 확장. 꺼져 있으면 B08 그대로 (ADD funct != 0 → illegal). */
static bool decode(uint16_t w, insn_t *d, bool allow_sltu) {
    unsigned a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u;
    memset(d, 0, sizeof *d);
    d->op = (uint8_t)(w >> 12);
    switch (d->op) {
    case OP_ADD:  d->funct = (uint8_t)(w & 7u);
                  if (!(d->funct == 0 || (allow_sltu && d->funct == 1))) return false;
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

#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_SLTU(rd, rs, rt)  ((uint16_t)(I_ADD(rd, rs, rt) | 1u))      /* 연습 4 확장: funct = 001 */
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_JAL(rd, off)      ((uint16_t)((0x5u << 12) | ((rd) << 9) | ((unsigned)(off) & 0x1FFu)))
#define I_HALT              ((uint16_t)0xF000u)

typedef enum { CPU_RUNNING, CPU_HALTED, CPU_ILLEGAL, CPU_PC_FAULT, CPU_DMEM_FAULT, CPU_STEP_LIMIT, CPU_BREAK } cpu_status_t;

typedef struct {
    uint16_t reg[NREG];
    uint16_t pc;
    uint16_t imem[IMEM_WORDS];
    uint16_t dmem[DMEM_WORDS];
    cpu_status_t status;
    uint16_t fault_info;
    uint64_t retired;
    uint64_t op_hist[16];
    uint32_t pc_prof[IMEM_WORDS];
    /* 연습용 스위치 */
    bool mmio_uart;               /* 연습 2: 0xFFFF = UART */
    bool ext_sltu;                /* 연습 4: SLTU 를 하드웨어가 지원 */
    char uart[32];
    unsigned uart_len;
} cpu_t;

static void cpu_load(cpu_t *c, const uint16_t *prog, size_t n) {
    memset(c, 0, sizeof *c);
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) c->imem[i] = prog[i];
    c->status = CPU_RUNNING;
}

static cpu_status_t cpu_step(cpu_t *c) {
    if (c->status != CPU_RUNNING) return c->status;
    if (c->pc >= IMEM_WORDS) { c->fault_info = c->pc; return c->status = CPU_PC_FAULT; }
    uint16_t w = c->imem[c->pc];
    insn_t d;
    if (!decode(w, &d, c->ext_sltu)) { c->fault_info = w; return c->status = CPU_ILLEGAL; }
    uint16_t a = c->reg[d.rs], b = c->reg[d.rt];
    uint16_t next_pc = (uint16_t)(c->pc + 1u);
    uint16_t wb_val = 0;
    bool wb = false;
    switch (d.op) {
    case OP_ADD:  wb_val = d.funct == 1 ? (uint16_t)(a < b) : (uint16_t)(a + b); wb = true; break;
    case OP_ADDI: wb_val = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
    case OP_LD:
    case OP_ST: {
        uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
        if (c->mmio_uart && ea == UART_ADDR) {                 /* 연습 2: 주소 디코더가 UART 로 보낸다 */
            if (d.op == OP_LD) { wb_val = 1; wb = true; }      /* 상태: bit0 = TX ready (항상 준비) */
            else if (c->uart_len + 1 < sizeof c->uart) c->uart[c->uart_len++] = (char)(b & 0xFF);
            break;
        }
        if (ea >= DMEM_WORDS) { c->fault_info = ea; return c->status = CPU_DMEM_FAULT; }
        if (d.op == OP_LD) { wb_val = c->dmem[ea]; wb = true; }
        else               { c->dmem[ea] = b; }
        break;
    }
    case OP_BEQ:  if (a == b) next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_JAL:  wb_val = next_pc; wb = true; next_pc = (uint16_t)(next_pc + (uint16_t)d.imm); break;
    case OP_HALT: c->status = CPU_HALTED; next_pc = c->pc; break;
    }
    if (wb && d.rd != 0) c->reg[d.rd] = wb_val;
    c->retired++;
    c->op_hist[d.op]++;
    c->pc_prof[c->pc]++;
    c->pc = next_pc;
    return c->status;
}

static cpu_status_t cpu_run(cpu_t *c, uint64_t max_steps) {
    for (uint64_t i = 0; i < max_steps; i++) {
        cpu_status_t s = cpu_step(c);
        if (s != CPU_RUNNING) return s;
    }
    return c->status == CPU_RUNNING ? CPU_STEP_LIMIT : c->status;
}

/* ---------------- 연습 3: 브레이크포인트 ----------------
 * bp 의 명령을 "실행하기 전에" 멈춘다 (Cortex-M FPB 의 명령 브레이크포인트처럼).
 * 이어가기: 지금 pc 가 이미 bp 면 첫 명령은 검사 없이 실행한다 (step over) — 안 그러면 영원히 같은 자리. */
static cpu_status_t run_until(cpu_t *c, uint16_t bp, uint64_t max_steps) {
    for (uint64_t i = 0; i < max_steps; i++) {
        if (i > 0 && c->pc == bp) return CPU_BREAK;
        cpu_status_t s = cpu_step(c);
        if (s != CPU_RUNNING) return s;
    }
    return CPU_STEP_LIMIT;
}

/* ---------------- 연습 4: illegal 트랩 → 소프트웨어 에뮬레이션 ----------------
 * 정밀 폴트라서 pc 는 SLTU 자리, 레지스터는 그 명령 직전 상태 → 핸들러가 대신 계산하고 pc+1 로 복귀. */
static cpu_status_t run_with_emulation(cpu_t *c, uint64_t max_steps, unsigned *traps) {
    *traps = 0;
    for (;;) {
        cpu_status_t s = cpu_run(c, max_steps);
        if (s != CPU_ILLEGAL) return s;
        uint16_t w = c->fault_info;
        if ((w >> 12) != OP_ADD || (w & 7u) != 1u) return s;         /* 진짜 illegal */
        unsigned rd = (w >> 9) & 7u, rs = (w >> 6) & 7u, rt = (w >> 3) & 7u;
        uint16_t v = (uint16_t)(c->reg[rs] < c->reg[rt]);
        if (rd != 0) c->reg[rd] = v;                                 /* 에뮬레이터도 r0 규칙을 지켜야 한다 */
        c->pc = (uint16_t)(c->pc + 1u);
        c->status = CPU_RUNNING;
        (*traps)++;
    }
}

/* ---------------- 프로그램 ---------------- */
/* 배열 합 (본문 Part A 와 같음): 6N + 6 */
static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 16), I_ADD(3, 0, 0),
    I_BEQ(1, 0, 5), I_LD(4, 2, 0), I_ADD(3, 3, 4), I_ADDI(2, 2, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -6),
    I_ST(3, 0, 1), I_HALT,
};
/* 연습 1: 2배 펼치기 (N 짝수). 반복 1회 = 원소 2개 = 8명령 → 4N + 6 */
static const uint16_t prog_sum2[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)                         */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16                        */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0                        */
    I_BEQ(1, 0, 7),       /*  3: loop: BEQ  r1, r0, done  ; done=11 → +7      */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                         */
    I_ADD(3, 3, 4),       /*  5:       ADD  r3, r3, r4                        */
    I_LD(4, 2, 1),        /*  6:       LD   r4, 1(r2)                         */
    I_ADD(3, 3, 4),       /*  7:       ADD  r3, r3, r4                        */
    I_ADDI(2, 2, 2),      /*  8:       ADDI r2, r2, 2                         */
    I_ADDI(1, 1, -2),     /*  9:       ADDI r1, r1, -2                        */
    I_BEQ(0, 0, -8),      /* 10:       BEQ  r0, r0, loop  ; 3-11 = -8         */
    I_ST(3, 0, 1),        /* 11: done: ST   r3, 1(r0)                         */
    I_HALT,               /* 12                                               */
};
/* 피보나치 (본문 Part A 와 같음): 루프 머리 = pc 4 */
static const uint16_t prog_fib[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 0), I_ADDI(3, 0, 1), I_ADDI(5, 0, 16),
    I_BEQ(1, 0, 7), I_ST(2, 5, 0), I_ADD(4, 2, 3), I_ADD(2, 3, 0), I_ADD(3, 4, 0),
    I_ADDI(5, 5, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -8), I_HALT,
};
/* 연습 2: UART 로 "OK\n". 'O' = 79 = 31+31+17, 'K' = 75, '\n' = 10. ST rX, -1(r0) → 주소 0xFFFF */
static const uint16_t prog_uart[] = {
    I_LD(3, 0, -1),       /*  0: poll: LD   r3, -1(r0)    ; UART 상태        */
    I_BEQ(3, 0, -2),      /*  1:       BEQ  r3, r0, poll  ; 0 이면 다시 읽기 */
    I_ADDI(1, 0, 31),     /*  2:       ADDI r1, r0, 31                        */
    I_ADDI(1, 1, 31),     /*  3:       ADDI r1, r1, 31                        */
    I_ADDI(1, 1, 17),     /*  4:       ADDI r1, r1, 17    ; 79 = 'O'          */
    I_ST(1, 0, -1),       /*  5:       ST   r1, -1(r0)                        */
    I_ADDI(1, 1, -4),     /*  6:       ADDI r1, r1, -4    ; 75 = 'K'          */
    I_ST(1, 0, -1),       /*  7:       ST   r1, -1(r0)                        */
    I_ADDI(2, 0, 10),     /*  8:       ADDI r2, r0, 10    ; '\n'              */
    I_ST(2, 0, -1),       /*  9:       ST   r2, -1(r0)                        */
    I_HALT,               /* 10                                               */
};
/* 연습 4: 최댓값 (부호 없는). 입력 dmem[0] = n, a[] = dmem[16..]. 출력 dmem[1] = max */
static const uint16_t prog_max[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)                         */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16                        */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0     ; max = 0          */
    I_BEQ(1, 0, 7),       /*  3: loop: BEQ  r1, r0, done   ; done=11 → +7     */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                         */
    I_SLTU(5, 3, 4),      /*  5:       SLTU r5, r3, r4     ; max < x ?        */
    I_BEQ(5, 0, 1),       /*  6:       BEQ  r5, r0, skip   ; skip=8 → +1      */
    I_ADD(3, 4, 0),       /*  7:       ADD  r3, r4, r0     ; max = x          */
    I_ADDI(2, 2, 1),      /*  8: skip: ADDI r2, r2, 1                         */
    I_ADDI(1, 1, -1),     /*  9:       ADDI r1, r1, -1                        */
    I_BEQ(0, 0, -8),      /* 10:       BEQ  r0, r0, loop   ; 3-11 = -8        */
    I_ST(3, 0, 1),        /* 11: done: ST   r3, 1(r0)                         */
    I_HALT,               /* 12                                               */
};

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static uint32_t rng_state = 0x2545F491u;
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(void) {
    cpu_t c;

    /* 연습 1: 같은 합, 명령 수 6N+6 vs 4N+6 (N = 0, 2, ..., 40) */
    bool same = true, f1 = true, f2 = true;
    for (unsigned n = 0; n <= 40; n += 2) {
        uint16_t data[40], ref = 0;
        for (unsigned i = 0; i < n; i++) { data[i] = (uint16_t)xs32(); ref = (uint16_t)(ref + data[i]); }
        cpu_load(&c, prog_sum, NELEM(prog_sum));
        c.dmem[0] = (uint16_t)n; memcpy(&c.dmem[16], data, n * sizeof data[0]);
        same = same && cpu_run(&c, 10000) == CPU_HALTED && c.dmem[1] == ref;
        f1 = f1 && c.retired == 6u * n + 6;
        cpu_load(&c, prog_sum2, NELEM(prog_sum2));
        c.dmem[0] = (uint16_t)n; memcpy(&c.dmem[16], data, n * sizeof data[0]);
        same = same && cpu_run(&c, 10000) == CPU_HALTED && c.dmem[1] == ref;
        f2 = f2 && c.retired == 4u * n + 6;
        if (n == 40) printf("  N=40: rolled %u, unrolled %llu instructions\n", 6u * n + 6, (unsigned long long)c.retired);
    }
    CHECK(same);
    CHECK(f1);
    CHECK(f2);

    /* 연습 2: UART. MMIO 가 켜져 있으면 "OK\n", 꺼져 있으면 첫 LD 에서 DMEM_FAULT (주소 0xFFFF) */
    cpu_load(&c, prog_uart, NELEM(prog_uart));
    c.mmio_uart = true;
    CHECK(cpu_run(&c, 100) == CPU_HALTED && c.uart_len == 3 && memcmp(c.uart, "OK\n", 3) == 0);
    CHECK(c.reg[3] == 1 && c.retired == 11);
    uint16_t zero[DMEM_WORDS] = { 0 };
    CHECK(memcmp(c.dmem, zero, sizeof zero) == 0);            /* dmem 은 하나도 안 바뀜 */
    cpu_load(&c, prog_uart, NELEM(prog_uart));
    CHECK(cpu_run(&c, 100) == CPU_DMEM_FAULT && c.fault_info == 0xFFFF && c.pc == 0);

    /* 연습 3: 피보나치 n=5, 루프 머리(pc 4) 브레이크포인트 → n+1 번 멈춘다 */
    cpu_load(&c, prog_fib, NELEM(prog_fib));
    c.dmem[0] = 5;
    CHECK(run_until(&c, 4, 1000) == CPU_BREAK && c.pc == 4 && c.retired == 4);
    CHECK(run_until(&c, 4, 1000) == CPU_BREAK && c.pc == 4 && c.retired == 12);  /* step over 후 한 바퀴 = 8 */
    unsigned hits = 2;
    cpu_status_t s;
    while ((s = run_until(&c, 4, 1000)) == CPU_BREAK) hits++;
    CHECK(s == CPU_HALTED && hits == 6 && c.reg[2] == 5 && c.retired == 8u * 5 + 6);

    /* 연습 4: max. 확장 코어 vs 기본 코어 + 트랩 에뮬레이션 vs 기본 코어(핸들러 없음) */
    static const uint16_t arr[8] = { 7, 0x8000, 3, 0xFFFE, 12, 0x7FFF, 0xFFFE, 1 };  /* 부호 없는 max = 0xFFFE */
    cpu_load(&c, prog_max, NELEM(prog_max));
    c.ext_sltu = true; c.dmem[0] = 8; memcpy(&c.dmem[16], arr, sizeof arr);
    CHECK(cpu_run(&c, 1000) == CPU_HALTED && c.dmem[1] == 0xFFFE);
    uint64_t native = c.retired;
    unsigned traps = 0;
    cpu_load(&c, prog_max, NELEM(prog_max));
    c.dmem[0] = 8; memcpy(&c.dmem[16], arr, sizeof arr);
    CHECK(run_with_emulation(&c, 1000, &traps) == CPU_HALTED && c.dmem[1] == 0xFFFE && traps == 8);
    CHECK(c.retired + traps == native);                       /* 트랩된 명령은 retired 에 안 들어간다 */
    printf("  max n=8: native %llu retired, emulated %llu retired + %u traps\n",
           (unsigned long long)native, (unsigned long long)c.retired, traps);
    cpu_load(&c, prog_max, NELEM(prog_max));
    c.dmem[0] = 8; memcpy(&c.dmem[16], arr, sizeof arr);
    CHECK(cpu_run(&c, 1000) == CPU_ILLEGAL && c.pc == 5 && c.fault_info == I_SLTU(5, 3, 4));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
