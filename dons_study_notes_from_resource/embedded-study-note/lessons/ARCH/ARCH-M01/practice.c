/* ARCH-M01 추가 연습 — 한 번에 검증
 *
 *   연습 1  분기를 ID 에서 해결 (penalty 1): 배열 합 사이클, JAL → 목표 RAW 가 새로 스톨을 만든다
 *   연습 2  split-phase 레지스터 파일이 없다면: 거리 1/2/3/4 → 스톨 3/2/1/0
 *   연습 3  손으로 스톨 세기 (스톨이 거리를 벌려 준다)
 *   연습 4  피보나치 루프 스케줄링: 반복당 스톨 1 (+ 머리말 1) → 0
 *   연습 5  Iron law: 파이프라인이 단일 사이클보다 빠를 조건 CPI < Tsingle / Tpipe
 *
 * solution.c 의 시뮬레이터 + 설정 스위치 2개 (split_phase, branch_in_id). 기본값이면 solution.c 와 똑같이 동작.
 * 각 설정마다 시뮬레이터 == 해석 공식 == 단일 사이클 모델(최종 상태) 을 랜덤 프로그램으로 확인한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };
typedef struct { uint8_t op, rd, rs, rt; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}
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
#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_JAL(rd, off)      ((uint16_t)((0x5u << 12) | ((rd) << 9) | ((unsigned)(off) & 0x1FFu)))
#define I_HALT              ((uint16_t)0xF000u)
#define I_NOP               ((uint16_t)0x0000u)

static unsigned insn_dst(const insn_t *d) {
    switch (d->op) {
    case OP_ADD: case OP_ADDI: case OP_LD: case OP_JAL: return d->rd;
    default: return 0;
    }
}
static void insn_srcs(const insn_t *d, unsigned s[2]) {
    s[0] = s[1] = 0;
    switch (d->op) {
    case OP_ADD: case OP_ST: case OP_BEQ: s[0] = d->rs; s[1] = d->rt; break;
    case OP_ADDI: case OP_LD:             s[0] = d->rs; break;
    default: break;
    }
}

/* ---------------- 설정 ---------------- */
typedef struct {
    bool split_phase;     /* true: WB 전반부 쓰기 / ID 후반부 읽기 (M01 기본). false: WB 에 있는 생산자도 해저드 */
    bool branch_in_id;    /* true: BEQ 비교·목표와 JAL 을 ID 에서 해결 → IF 하나만 flush (penalty 1) */
} cfg_t;
static const cfg_t CFG_M01 = { true, false };

/* ---------------- 시뮬레이터 (solution.c 와 같은 구조) ---------------- */
enum { S_IF, S_ID, S_EX, S_MEM, S_WB, NSTAGE };
typedef struct { bool valid, bad, wb, redirected; uint16_t pc, word; insn_t d; uint16_t a, b, val; } slot_t;
typedef struct {
    slot_t st[NSTAGE];
    uint16_t pc;
    bool fetch_on, halted, error;
    uint16_t reg[NREG], imem[IMEM_WORDS], dmem[DMEM_WORDS];
    uint64_t cycle, retired, stalls, flushes;
    cfg_t cfg;
} pipe_t;

static bool hazard_stall(const pipe_t *p) {
    const slot_t *id = &p->st[S_ID];
    if (!id->valid) return false;
    unsigned s[2];
    insn_srcs(&id->d, s);
    int last = p->cfg.split_phase ? S_MEM : S_WB;          /* 연습 2: split-phase 가 없으면 WB 도 검사 */
    for (int k = 0; k < 2; k++) {
        if (!s[k]) continue;
        for (int st = S_EX; st <= last; st++)
            if (p->st[st].valid && insn_dst(&p->st[st].d) == s[k]) return true;
    }
    return false;
}

static void kill(slot_t *x) { memset(x, 0, sizeof *x); }

static void pipe_cycle(pipe_t *p) {
    p->cycle++;
    if (p->fetch_on && !p->st[S_IF].valid) {
        slot_t *f = &p->st[S_IF]; kill(f);
        f->valid = true; f->pc = p->pc; f->word = p->pc < IMEM_WORDS ? p->imem[p->pc] : I_NOP;
        p->pc = (uint16_t)(p->pc + 1u);
    }
    slot_t *w = &p->st[S_WB];
    if (w->valid) {
        if (w->wb && w->d.rd) p->reg[w->d.rd] = w->val;
        p->retired++;
        if (w->d.op == OP_HALT) { p->halted = true; return; }
    }
    slot_t *m = &p->st[S_MEM];
    if (m->valid && (m->d.op == OP_LD || m->d.op == OP_ST)) {
        if (m->val >= DMEM_WORDS) { p->error = true; return; }
        if (m->d.op == OP_LD) m->val = p->dmem[m->val]; else p->dmem[m->val] = m->b;
    }
    slot_t *x = &p->st[S_EX];
    bool redirect = false, halt_ex = false;
    uint16_t target = 0;
    if (x->valid) {
        if (x->bad) { p->error = true; return; }
        uint16_t next = (uint16_t)(x->pc + 1u);
        switch (x->d.op) {
        case OP_ADD:  x->val = (uint16_t)(x->a + x->b); x->wb = true; break;
        case OP_ADDI: case OP_LD: case OP_ST:
            x->val = (uint16_t)(x->a + (uint16_t)x->d.imm); x->wb = x->d.op != OP_ST; break;
        case OP_BEQ:  if (!p->cfg.branch_in_id && x->a == x->b) { redirect = true; target = (uint16_t)(next + (uint16_t)x->d.imm); } break;
        case OP_JAL:  x->val = next; x->wb = true;
                      if (!p->cfg.branch_in_id) { redirect = true; target = (uint16_t)(next + (uint16_t)x->d.imm); } break;
        case OP_HALT: halt_ex = true; break;
        }
    }
    slot_t *id = &p->st[S_ID];
    if (id->valid) {
        id->bad = !decode(id->word, &id->d);
        id->a = p->reg[id->d.rs]; id->b = p->reg[id->d.rt];
    }
    bool stall = hazard_stall(p);
    /* 연습 1: ID 에서 분기 해결 (스톨이 없을 때만 — 피연산자가 준비된 뒤) */
    bool redirect_id = false;
    uint16_t target_id = 0;
    if (p->cfg.branch_in_id && id->valid && !id->bad && !stall && !halt_ex) {
        uint16_t next = (uint16_t)(id->pc + 1u);
        if ((id->d.op == OP_BEQ && id->a == id->b) || id->d.op == OP_JAL) {
            redirect_id = true; target_id = (uint16_t)(next + (uint16_t)id->d.imm);
        }
    }
    /* 클럭 에지 */
    p->st[S_WB] = p->st[S_MEM];
    p->st[S_MEM] = p->st[S_EX];
    if (redirect || halt_ex) {
        kill(&p->st[S_ID]); kill(&p->st[S_IF]); kill(&p->st[S_EX]);
        if (redirect) { p->pc = target; p->flushes++; } else p->fetch_on = false;
    } else if (stall) {
        kill(&p->st[S_EX]); p->stalls++;
    } else {
        p->st[S_EX] = p->st[S_ID];
        kill(&p->st[S_ID]);
        if (redirect_id) { kill(&p->st[S_IF]); p->pc = target_id; p->flushes++; }
        else { p->st[S_ID] = p->st[S_IF]; kill(&p->st[S_IF]); }
    }
}

static bool pipe_run_prog(pipe_t *p, cfg_t cfg, const uint16_t *prog, size_t n, const uint16_t *dm) {
    memset(p, 0, sizeof *p);
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) p->imem[i] = prog[i];
    memcpy(p->dmem, dm, sizeof p->dmem);
    p->fetch_on = true; p->cfg = cfg;
    while (!p->halted && !p->error && p->cycle < 100000) pipe_cycle(p);
    return p->halted;
}

/* ---------------- 단일 사이클 정답지 + 트레이스 ---------------- */
typedef struct { insn_t d; bool redirect; } trace_t;
typedef struct { uint16_t reg[NREG], dmem[DMEM_WORDS]; uint64_t retired; bool ok; } ref_t;
static size_t ref_run(const uint16_t *imem, size_t n, const uint16_t *dm, ref_t *r, trace_t *tr, size_t cap) {
    memset(r, 0, sizeof *r);
    memcpy(r->dmem, dm, sizeof r->dmem);
    uint16_t pc = 0; size_t k = 0;
    for (int step = 0; step < 100000; step++) {
        insn_t d;
        if (pc >= IMEM_WORDS || !decode(pc < n ? imem[pc] : I_NOP, &d)) return k;
        uint16_t a = r->reg[d.rs], b = r->reg[d.rt], next = (uint16_t)(pc + 1u), v = 0;
        bool wb = false, redir = false;
        switch (d.op) {
        case OP_ADD:  v = (uint16_t)(a + b); wb = true; break;
        case OP_ADDI: v = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
        case OP_LD: case OP_ST: {
            uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
            if (ea >= DMEM_WORDS) return k;
            if (d.op == OP_LD) { v = r->dmem[ea]; wb = true; } else r->dmem[ea] = b;
            break;
        }
        case OP_BEQ:  if (a == b) { redir = true; next = (uint16_t)(next + (uint16_t)d.imm); } break;
        case OP_JAL:  v = next; wb = true; redir = true; next = (uint16_t)(next + (uint16_t)d.imm); break;
        default: break;
        }
        if (wb && d.rd) r->reg[d.rd] = v;
        r->retired++;
        if (k < cap) { tr[k].d = d; tr[k].redirect = redir; }
        k++;
        if (d.op == OP_HALT) { r->ok = true; break; }
        pc = next;
    }
    return k;
}

/* ---------------- 해석 공식 (설정별) ----------------
 * RAW: EX(k) >= EX(p) + raw_gap,  raw_gap = 3 (split-phase) / 4 (없음)
 * 리다이렉트 뒤: EX(k) >= EX(k-1) + 1 + pen,  pen = 2 (EX 해결) / 1 (ID 해결) */
static uint64_t analytic_cycles(const trace_t *t, size_t n, cfg_t cfg) {
    uint64_t ready[NREG] = { 0 }, ex = 2;
    uint64_t gap = cfg.split_phase ? 3 : 4, pen = cfg.branch_in_id ? 1 : 2;
    bool prev = false;
    for (size_t k = 0; k < n; k++) {
        ex += 1 + (prev ? pen : 0);
        unsigned s[2];
        insn_srcs(&t[k].d, s);
        for (int j = 0; j < 2; j++) if (s[j] && ready[s[j]] > ex) ex = ready[s[j]];
        unsigned dst = insn_dst(&t[k].d);
        if (dst) ready[dst] = ex + gap;
        prev = t[k].redirect;
    }
    return n ? ex + 2 : 0;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
static uint32_t rng_state = 0x9E3779B9u;
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* 시뮬레이터 == 단일 사이클(상태) == 해석 공식(사이클). 성공하면 사이클 수, 실패면 0 */
static uint64_t run_checked(cfg_t cfg, const uint16_t *prog, size_t n, const uint16_t *dm, pipe_t *p) {
    static ref_t r; static trace_t tr[4096];
    size_t k = ref_run(prog, n, dm, &r, tr, NELEM(tr));
    if (!r.ok || !pipe_run_prog(p, cfg, prog, n, dm)) return 0;
    if (memcmp(p->reg, r.reg, sizeof r.reg) || memcmp(p->dmem, r.dmem, sizeof r.dmem) || p->retired != r.retired) return 0;
    uint64_t a = analytic_cycles(tr, k, cfg);
    return a == p->cycle ? p->cycle : 0;
}

static bool fuzz(cfg_t cfg, int count) {
    static uint16_t prog[IMEM_WORDS], dm[DMEM_WORDS];
    static pipe_t p;
    for (int t = 0; t < count; t++) {
        size_t L = 8 + xs32() % 113;
        for (size_t i = 0; i + 1 < L; i++) {
            unsigned room = (unsigned)(L - 2 - i), k = xs32() % 16, rs = xs32() & 7, rt = xs32() & 7;
            unsigned rd = xs32() % 7; if (rd == 6) rd = 7;
            unsigned base = (xs32() & 1) ? 6 : 0, off5 = xs32() % 32;
            int imm6 = (int)(xs32() % 64) - 32;
            if (k < 4)       prog[i] = I_ADD(rd, rs, rt);
            else if (k < 7)  prog[i] = I_ADDI(rd, rs, imm6);
            else if (k < 8)  prog[i] = I_ADDI(6, 0, off5);
            else if (k < 10) prog[i] = I_LD(rd, base, off5);
            else if (k < 12) prog[i] = I_ST(rt, base, off5);
            else if (k < 14) prog[i] = I_BEQ(rs, rt, xs32() % ((room < 31 ? room : 31) + 1));
            else if (k < 15) prog[i] = I_JAL(rd, xs32() % (room + 1));
            else             prog[i] = I_NOP;
        }
        prog[L - 1] = I_HALT;
        for (unsigned i = 0; i < DMEM_WORDS; i++) dm[i] = (uint16_t)xs32();
        if (!run_checked(cfg, prog, L, dm, &p)) { printf("  fuzz mismatch at %d\n", t); return false; }
    }
    return true;
}

static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0), I_ADDI(2, 0, 16), I_ADD(3, 0, 0),
    I_BEQ(1, 0, 5), I_LD(4, 2, 0), I_ADD(3, 3, 4), I_ADDI(2, 2, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -6),
    I_ST(3, 0, 1), I_HALT,
};

static uint64_t sum_cycles(cfg_t cfg, unsigned N, pipe_t *p) {
    static uint16_t dm[DMEM_WORDS];
    memset(dm, 0, sizeof dm);
    dm[0] = (uint16_t)N;
    for (unsigned i = 0; i < N; i++) dm[16 + i] = (uint16_t)(i * 7 + 1);
    return run_checked(cfg, prog_sum, NELEM(prog_sum), dm, p);
}

/* 연습 5 */
static double breakeven_cpi(uint32_t t_single, uint32_t t_pipe) { return (double)t_single / (double)t_pipe; }

int main(void) {
    static pipe_t p;
    static const uint16_t z[DMEM_WORDS];
    const cfg_t ID_BR = { true, true }, NO_SPLIT = { false, false };

    printf("기준(M01) 설정 — solution.c 와 같은 숫자\n");
    CHECK(sum_cycles(CFG_M01, 10, &p) == 112 && p.stalls == 20 && p.flushes == 11);
    CHECK(fuzz(CFG_M01, 500));

    printf("연습 1 — 분기를 ID 에서\n");
    uint64_t c1 = sum_cycles(ID_BR, 10, &p);
    printf("  sum N=10: cycles=%llu stalls=%llu flushes=%llu\n", (unsigned long long)c1,
           (unsigned long long)p.stalls, (unsigned long long)p.flushes);
    CHECK(c1 == 101);                                        /* 9N + 11 */
    CHECK(sum_cycles(ID_BR, 100, &p) == 911);
    /* JAL r7 → 목표가 r7 을 읽기: EX 해결에선 스톨 0, ID 해결에선 스톨 1 */
    static const uint16_t p_jal[] = { I_JAL(7, 0), I_ADD(1, 7, 7), I_HALT };
    CHECK(run_checked(CFG_M01, p_jal, 3, z, &p) == 3 + 4 + 2 && p.stalls == 0);
    CHECK(run_checked(ID_BR, p_jal, 3, z, &p) == 3 + 4 + 1 + 1 && p.stalls == 1 && p.reg[1] == 2);
    CHECK(fuzz(ID_BR, 500));

    printf("연습 2 — split-phase 없음\n");
    static const uint64_t want[] = { 3, 2, 1, 0 };
    bool d_ok = true;
    for (unsigned dist = 1; dist <= 4; dist++) {
        uint16_t prog[8]; size_t n = 0;
        prog[n++] = I_ADDI(1, 0, 5);
        for (unsigned i = 1; i < dist; i++) prog[n++] = I_NOP;
        prog[n++] = I_ADD(2, 1, 1); prog[n++] = I_HALT;
        d_ok = d_ok && run_checked(NO_SPLIT, prog, n, z, &p) && p.stalls == want[dist - 1];
    }
    CHECK(d_ok);
    uint64_t c2 = sum_cycles(NO_SPLIT, 10, &p);
    printf("  sum N=10: cycles=%llu stalls=%llu\n", (unsigned long long)c2, (unsigned long long)p.stalls);
    CHECK(c2 == 123 && p.stalls == 31);                     /* 3N 스톨 + 머리말 LD r1 → BEQ r1 (거리 3) 1 */
    CHECK(fuzz(NO_SPLIT, 500));

    printf("연습 3 — 손으로 세기\n");
    static const uint16_t p3[] = {
        I_LD(1, 0, 0), I_LD(2, 0, 1),
        I_ADD(3, 1, 2),       /* r2 거리 1 → 2, r1 거리 2 는 그 사이 해결 */
        I_ST(3, 0, 2),        /* r3 거리 1 → 2 */
        I_ADDI(4, 3, 1),      /* r3 거리 2 지만 ST 가 이미 2 사이클 기다림 → 0 */
        I_HALT,
    };
    uint16_t dm3[DMEM_WORDS] = { 0 };
    dm3[0] = 40; dm3[1] = 2;
    CHECK(run_checked(CFG_M01, p3, NELEM(p3), dm3, &p) == 14 && p.stalls == 4 && p.dmem[2] == 42 && p.reg[4] == 43);

    printf("연습 4 — 피보나치 스케줄링\n");
    static const uint16_t fib[] = {
        I_LD(1, 0, 0), I_ADDI(2, 0, 0), I_ADDI(3, 0, 1), I_ADDI(5, 0, 16),
        I_BEQ(1, 0, 7), I_ST(2, 5, 0), I_ADD(4, 2, 3), I_ADD(2, 3, 0), I_ADD(3, 4, 0),
        I_ADDI(5, 5, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -8), I_HALT,
    };
    static const uint16_t fib_sched[] = {   /* 머리말도 재배치: r5 를 먼저 → 첫 ST r2, 0(r5) 의 거리 2 스톨 제거 */
        I_LD(1, 0, 0), I_ADDI(5, 0, 16), I_ADDI(2, 0, 0), I_ADDI(3, 0, 1),
        I_BEQ(1, 0, 7), I_ST(2, 5, 0), I_ADD(4, 2, 3), I_ADD(2, 3, 0), I_ADDI(5, 5, 1),
        I_ADD(3, 4, 0), I_ADDI(1, 1, -1), I_BEQ(0, 0, -8), I_HALT,
    };
    bool fib_ok = true;
    for (unsigned n = 0; n <= 20; n++) {
        uint16_t dm[DMEM_WORDS] = { 0 };
        dm[0] = (uint16_t)n;
        uint64_t a = run_checked(CFG_M01, fib, NELEM(fib), dm, &p), sa = p.stalls;
        uint64_t b = run_checked(CFG_M01, fib_sched, NELEM(fib_sched), dm, &p), sb = p.stalls;
        if (n == 20) printf("  fib n=20: %llu cycles (stalls %llu) -> sched %llu cycles (stalls %llu)\n",
                            (unsigned long long)a, (unsigned long long)sa, (unsigned long long)b, (unsigned long long)sb);
        uint64_t first = n > 0;               /* 원본: 첫 반복의 ST r2, 0(r5) 가 ADDI r5 와 거리 2 → 1 */
        fib_ok = fib_ok && a == 11ull * n + 12 + first && sa == n + first && b == 10ull * n + 12 && sb == 0;
    }
    CHECK(fib_ok);

    printf("연습 5 — Iron law 손익분기 CPI\n");
    static const uint32_t ovh[] = { 20, 50, 100 };
    for (unsigned i = 0; i < NELEM(ovh); i++)
        printf("  overhead %3u ps: single %u ps, pipe %u ps, break-even CPI %.2f\n", ovh[i],
               800 + ovh[i], 200 + ovh[i], breakeven_cpi(800 + ovh[i], 200 + ovh[i]));
    CHECK(breakeven_cpi(850, 250) == 3.4 && breakeven_cpi(900, 300) == 3.0);
    /* 배열 합 N=100 (CPI 1012/606 = 1.67) 은 overhead 100 에서도 이긴다 */
    CHECK(1012.0 / 606.0 < breakeven_cpi(900, 300));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
