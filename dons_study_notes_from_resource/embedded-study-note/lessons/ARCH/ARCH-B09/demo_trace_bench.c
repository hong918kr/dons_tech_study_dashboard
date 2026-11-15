/* ARCH-B09 demo — (1) 한 명령씩 실행 추적(trace) (2) 인터프리터 속도: 매번 디코드 vs 미리 디코드
 *
 * ISA = ARCH-B08 ISA를 그대로 넣었다 (solution.c 의 인터프리터를 줄여서 복사; 폴트 검사는 dmem 범위만).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace_bench.c -o tb && ./tb
 * 시간은 C11 timespec_get 으로 잰다. 숫자는 기계·부하에 따라 달라지는 참고값이다 (CHECK 하지 않음).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };
typedef struct { uint8_t op, rd, rs, rt; int16_t imm; bool ok; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}
static insn_t decode(uint16_t w) {
    insn_t d = { .op = (uint8_t)(w >> 12), .ok = true };
    uint8_t a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u;
    switch (d.op) {
    case OP_ADD:  d.rd = a; d.rs = b; d.rt = c; d.ok = (w & 7u) == 0; break;
    case OP_ADDI: case OP_LD: d.rd = a; d.rs = b; d.imm = sext(w, 6); break;
    case OP_ST:   d.rt = a; d.rs = b; d.imm = sext(w, 6); break;
    case OP_BEQ:  d.rs = a; d.rt = b; d.imm = sext(w, 6); break;
    case OP_JAL:  d.rd = a; d.imm = sext(w, 9); break;
    case OP_HALT: d.ok = (w & 0x0FFFu) == 0; break;
    default:      d.ok = false;
    }
    return d;
}
static void disasm(uint16_t w, char *out, size_t n) {
    insn_t d = decode(w);
    static const char *nm[16] = { "ADD", "ADDI", "LD", "ST", "BEQ", "JAL", [15] = "HALT" };
    if (!d.ok) { snprintf(out, n, ".word 0x%04X", w); return; }
    switch (d.op) {
    case OP_ADD:  snprintf(out, n, "ADD  r%u, r%u, r%u", d.rd, d.rs, d.rt); break;
    case OP_ADDI: snprintf(out, n, "ADDI r%u, r%u, %d", d.rd, d.rs, d.imm); break;
    case OP_LD:   snprintf(out, n, "LD   r%u, %d(r%u)", d.rd, d.imm, d.rs); break;
    case OP_ST:   snprintf(out, n, "ST   r%u, %d(r%u)", d.rt, d.imm, d.rs); break;
    case OP_BEQ:  snprintf(out, n, "BEQ  r%u, r%u, %+d", d.rs, d.rt, d.imm); break;
    case OP_JAL:  snprintf(out, n, "JAL  r%u, %+d", d.rd, d.imm); break;
    default:      snprintf(out, n, "%s", nm[d.op]); break;
    }
}

#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_HALT              ((uint16_t)0xF000u)

static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 16), I_ADD(3, 0, 0),
    I_BEQ(1, 0, 5), I_LD(4, 2, 0), I_ADD(3, 3, 4), I_ADDI(2, 2, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -6),
    I_ST(3, 0, 1), I_HALT,
};
static const uint16_t prog_fib[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 0), I_ADDI(3, 0, 1), I_ADDI(5, 0, 16),
    I_BEQ(1, 0, 7), I_ST(2, 5, 0), I_ADD(4, 2, 3), I_ADD(2, 3, 0), I_ADD(3, 4, 0),
    I_ADDI(5, 5, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -8), I_HALT,
};

typedef struct { uint16_t reg[NREG], pc, imem[IMEM_WORDS], dmem[DMEM_WORDS]; uint64_t retired; bool halted; } cpu_t;

/* 한 명령 실행. pre != NULL 이면 미리 디코드한 표를 쓴다. 반환: false = 멈춤(HALT 또는 폴트) */
static bool step(cpu_t *c, const insn_t *pre) {
    if (c->pc >= IMEM_WORDS) return false;
    insn_t d = pre ? pre[c->pc] : decode(c->imem[c->pc]);
    if (!d.ok) return false;
    uint16_t a = c->reg[d.rs], b = c->reg[d.rt], npc = (uint16_t)(c->pc + 1u), v = 0;
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
    case OP_BEQ:  if (a == b) npc = (uint16_t)(npc + (uint16_t)d.imm); break;
    case OP_JAL:  v = npc; wb = true; npc = (uint16_t)(npc + (uint16_t)d.imm); break;
    case OP_HALT: c->halted = true; c->retired++; return false;
    }
    if (wb && d.rd) c->reg[d.rd] = v;
    c->retired++;
    c->pc = npc;
    return true;
}

static void load(cpu_t *c, const uint16_t *p, size_t n) {
    memset(c, 0, sizeof *c);
    memcpy(c->imem, p, n * sizeof *p);
}

static double now_s(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void) {
    /* (1) trace: fib n = 2 */
    cpu_t c;
    load(&c, prog_fib, sizeof prog_fib / sizeof prog_fib[0]);
    c.dmem[0] = 2;
    printf("trace: fib n=2  (변경된 레지스터/메모리만 표시)\n");
    printf("step  pc  word  instruction        effect\n");
    for (unsigned s = 1;; s++) {
        uint16_t before[NREG], pc = c.pc, w = c.imem[c.pc], dm[DMEM_WORDS];
        memcpy(before, c.reg, sizeof before);
        memcpy(dm, c.dmem, sizeof dm);
        char txt[32];
        disasm(w, txt, sizeof txt);
        bool more = step(&c, NULL);
        char eff[96] = "";
        size_t len = 0;
        for (unsigned r = 1; r < NREG; r++)
            if (c.reg[r] != before[r]) len += (size_t)snprintf(eff + len, sizeof eff - len, "r%u=%u ", r, c.reg[r]);
        for (unsigned i = 0; i < DMEM_WORDS; i++)
            if (c.dmem[i] != dm[i]) len += (size_t)snprintf(eff + len, sizeof eff - len, "dmem[%u]=%u ", i, c.dmem[i]);
        if (c.pc != (uint16_t)(pc + 1u) && more) snprintf(eff + len, sizeof eff - len, "-> pc=%u", c.pc);
        printf("%4u  %2u  %04X  %-18s %s\n", s, pc, w, txt, eff);
        if (!more) break;
    }
    printf("retired=%llu (8n+6 = %u), halted=%d\n\n", (unsigned long long)c.retired, 8u * 2 + 6, c.halted);

    /* (2) 속도: 배열 합 N = 240 (1446 명령) 을 여러 번 */
    enum { N = 240, REPS = 200000 };
    for (int variant = 0; variant < 2; variant++) {
        insn_t pre[IMEM_WORDS];
        uint64_t total = 0;
        uint16_t chk = 0;
        load(&c, prog_sum, sizeof prog_sum / sizeof prog_sum[0]);
        for (unsigned i = 0; i < IMEM_WORDS; i++) pre[i] = decode(c.imem[i]);   /* 한 번만 (imem 은 안 바뀐다: Harvard) */
        double t0 = now_s();
        for (int r = 0; r < REPS; r++) {
            load(&c, prog_sum, sizeof prog_sum / sizeof prog_sum[0]);
            c.dmem[0] = N;
            for (unsigned i = 0; i < N; i++) c.dmem[16 + i] = (uint16_t)(i * 7u + (unsigned)r);
            while (step(&c, variant ? pre : NULL)) { }
            total += c.retired;
            chk ^= c.dmem[1];
        }
        double dt = now_s() - t0;
        printf("%-22s %llu instr in %.3f s = %.1f M instr/s (%.2f ns/instr)  chk=%04X\n",
               variant ? "predecoded table:" : "decode every step:", (unsigned long long)total, dt,
               (double)total / dt / 1e6, dt * 1e9 / (double)total, chk);
    }
    return 0;
}
