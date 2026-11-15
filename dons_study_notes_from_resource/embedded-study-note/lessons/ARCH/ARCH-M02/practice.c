/* ARCH-M02 추가 연습 — 한 번에 검증
 *
 *   solution.c 에서 사이클 시뮬레이터와 4000개 무작위 프로그램으로 대조한 "식 모델(eq_cycles)" 을 확장해서 쓴다:
 *     bp = NT / 2BIT (본문과 같음) / 1BIT (16 엔트리 1비트, 초기 not-taken) / BTFN (뒤로 가면 taken, ID 에서 결정)
 *     memfwd = 1 : LD → 바로 다음 ST 의 "데이터" 입력을 MEM→MEM 으로 넘긴다 (베이스 주소는 여전히 load-use)
 *   연습 1  루프 스케줄링으로 load-use 숨기기 (배열 합)
 *   연습 2  BTFN 정적 예측 열 추가 (벤치 4개)
 *   연습 3  1비트 vs 2비트 예측기: 이중 루프 탈출 분기
 *   연습 4  MEM→MEM 포워딩: memcpy 의 LD → ST
 *   연습 5  손계산: 짧은 코드 조각의 사이클 수
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== ISA (ARCH-B08 ISA를 그대로 넣었다) ===================== */
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

static bool writes_rd(const insn_t *d) {
    return (d->op == OP_ADD || d->op == OP_ADDI || d->op == OP_LD || d->op == OP_JAL) && d->rd != 0;
}

/* ===================== 기준 모델 + 동적 흐름 ===================== */
typedef struct { uint16_t pc, w; bool taken; } dyn_t;
typedef struct { uint16_t reg[NREG]; uint16_t dmem[DMEM_WORDS]; uint64_t retired; } arch_t;

static bool iss_run(const uint16_t *prog, size_t np, arch_t *s, dyn_t *tr, size_t cap, size_t *ntr) {
    uint16_t imem[IMEM_WORDS] = { 0 }, pc = 0;
    size_t n = 0;
    for (size_t i = 0; i < np && i < IMEM_WORDS; i++) imem[i] = prog[i];
    for (uint32_t step = 0; step < 100000; step++) {
        insn_t d;
        if (pc >= IMEM_WORDS || !decode(imem[pc], &d)) return false;
        uint16_t a = s->reg[d.rs], b = s->reg[d.rt], npc = (uint16_t)(pc + 1u), val = 0;
        bool wb = false, taken = false;
        switch (d.op) {
        case OP_ADD:  val = (uint16_t)(a + b); wb = true; break;
        case OP_ADDI: val = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
        case OP_LD: case OP_ST: {
            uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
            if (ea >= DMEM_WORDS) return false;
            if (d.op == OP_LD) { val = s->dmem[ea]; wb = true; } else s->dmem[ea] = b;
            break;
        }
        case OP_BEQ:  taken = a == b; if (taken) npc = (uint16_t)(npc + (uint16_t)d.imm); break;
        case OP_JAL:  val = npc; wb = true; taken = true; npc = (uint16_t)(npc + (uint16_t)d.imm); break;
        default: break;
        }
        if (wb && d.rd) s->reg[d.rd] = val;
        s->retired++;
        if (n < cap) tr[n] = (dyn_t){ pc, imem[pc], taken };
        n++;
        if (d.op == OP_HALT) { *ntr = n; return true; }
        pc = npc;
    }
    return false;
}

/* ===================== 확장 식 모델 ===================== */
enum { BP_NT, BP_2BIT, BP_1BIT, BP_BTFN };
typedef struct { bool fwd; int bp; bool memfwd; } xcfg_t;
typedef struct { uint64_t cycles, stalls, flushes, mispred; uint32_t mp_pc[IMEM_WORDS]; } xres_t;

static xres_t g_r;

static const xres_t *xeq(const dyn_t *tr, size_t n, xcfg_t cfg) {
    int64_t ex_w[NREG] = { 0 };          /* 마지막 생산자의 EX 사이클 */
    bool ld_w[NREG] = { false }, has[NREG] = { false };
    uint8_t tab[16];
    memset(&g_r, 0, sizeof g_r);
    memset(tab, cfg.bp == BP_2BIT ? 1 : 0, sizeof tab);
    int64_t ex = 0;
    unsigned extra = 0;
    for (size_t k = 0; k < n; k++) {
        insn_t d;
        decode(tr[k].w, &d);
        int64_t t = k == 0 ? 3 : ex + 1 + (int64_t)extra, base = t;
        uint8_t src[2];
        bool is_data[2] = { false, false };
        int ns = 0;
        if (d.op == OP_ADD || d.op == OP_ST || d.op == OP_BEQ || d.op == OP_ADDI || d.op == OP_LD) {
            if (d.rs) src[ns++] = d.rs;
            if (d.op != OP_ADDI && d.op != OP_LD && d.rt) { is_data[ns] = d.op == OP_ST; src[ns++] = d.rt; }
        }
        for (int i = 0; i < ns; i++) {
            uint8_t r = src[i];
            if (!has[r]) continue;
            int64_t need;
            if (!cfg.fwd) need = ex_w[r] + 3;
            else if (!ld_w[r]) need = ex_w[r] + 1;
            else if (cfg.memfwd && is_data[i] && !(d.rs == r)) need = ex_w[r] + 1;   /* ST 데이터만 MEM→MEM */
            else need = ex_w[r] + 2;
            if (need > t) t = need;
        }
        g_r.stalls += (uint64_t)(t - base);
        ex = t;
        if (writes_rd(&d)) { ex_w[d.rd] = t; ld_w[d.rd] = d.op == OP_LD; has[d.rd] = true; }
        extra = 0;
        if (d.op == OP_JAL) extra = cfg.bp == BP_NT ? 2 : 1;
        else if (d.op == OP_BEQ) {
            bool taken = tr[k].taken, pred = false;
            unsigned i = tr[k].pc & 15u;
            switch (cfg.bp) {
            case BP_2BIT: pred = tab[i] >= 2; tab[i] = taken ? (tab[i] < 3 ? tab[i] + 1 : 3) : (tab[i] > 0 ? tab[i] - 1 : 0); break;
            case BP_1BIT: pred = tab[i]; tab[i] = taken; break;
            case BP_BTFN: pred = d.imm < 0; break;
            default: break;
            }
            if (pred != taken) { extra = 2; g_r.mispred++; g_r.mp_pc[tr[k].pc]++; }
            else if (taken) extra = 1;
        }
        g_r.flushes += extra;
    }
    g_r.cycles = (uint64_t)ex + 2;
    return &g_r;
}

static dyn_t g_tr[100000];
static size_t g_ntr;
static arch_t g_a;

/* 프로그램을 기준 모델로 돌리고 동적 흐름을 g_tr 에 남긴다 */
static bool trace(const uint16_t *prog, size_t n, const uint16_t *dmem) {
    memset(&g_a, 0, sizeof g_a);
    if (dmem) memcpy(g_a.dmem, dmem, sizeof g_a.dmem);
    return iss_run(prog, n, &g_a, g_tr, sizeof g_tr / sizeof g_tr[0], &g_ntr);
}
static uint64_t cyc(xcfg_t c) { return xeq(g_tr, g_ntr, c)->cycles; }

static const xcfg_t NOFWD_NT = { false, BP_NT, false }, FWD_NT = { true, BP_NT, false },
                    FWD_2B = { true, BP_2BIT, false }, FWD_1B = { true, BP_1BIT, false },
                    FWD_BTFN = { true, BP_BTFN, false }, FWD_NT_MF = { true, BP_NT, true };

/* ===================== 프로그램 ===================== */
static const uint16_t prog_sum[] = {          /* 본문 그대로 */
    I_LD(1, 0, 0), I_ADDI(2, 0, 16), I_ADD(3, 0, 0),
    I_BEQ(1, 0, 5), I_LD(4, 2, 0), I_ADD(3, 3, 4), I_ADDI(2, 2, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -6),
    I_ST(3, 0, 1), I_HALT,
};
static const uint16_t prog_sum_sched[] = {    /* 연습 1: ADDI r2 를 LD 와 ADD 사이로 */
    I_LD(1, 0, 0),        /*  0                                         */
    I_ADDI(2, 0, 16),     /*  1                                         */
    I_ADD(3, 0, 0),       /*  2                                         */
    I_BEQ(1, 0, 5),       /*  3: loop: BEQ  r1, r0, done                */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                   */
    I_ADDI(2, 2, 1),      /*  5:       ADDI r2, r2, 1   ← 독립 명령을 끼움 */
    I_ADD(3, 3, 4),       /*  6:       ADD  r3, r3, r4  ← 이제 거리 2    */
    I_ADDI(1, 1, -1),     /*  7                                         */
    I_BEQ(0, 0, -6),      /*  8:       BEQ  r0, r0, loop                */
    I_ST(3, 0, 1),        /*  9: done                                   */
    I_HALT,
};
static const uint16_t prog_hash[] = {
    I_LD(1, 0, 0), I_LD(5, 0, 1),
    I_BEQ(5, 0, 7), I_ADD(2, 1, 1), I_ADD(3, 2, 2), I_ADD(3, 3, 3), I_ADD(1, 3, 2), I_ADDI(1, 1, 7),
    I_ADDI(5, 5, -1), I_BEQ(0, 0, -8),
    I_ST(1, 0, 2), I_HALT,
};
static const uint16_t prog_count[] = {
    I_LD(1, 0, 0), I_LD(2, 0, 1), I_ADDI(3, 0, 16), I_ADD(4, 0, 0),
    I_BEQ(1, 0, 7), I_LD(5, 3, 0), I_ADDI(3, 3, 1), I_ADDI(1, 1, -1), I_BEQ(5, 2, 1), I_JAL(0, -6),
    I_ADDI(4, 4, 1), I_JAL(0, -8),
    I_ST(4, 0, 2), I_HALT,
};
#define NEST_M 4
#define NEST_K 5
static const uint16_t prog_nest[] = {
    I_ADDI(1, 0, NEST_M), I_BEQ(1, 0, 7), I_ADDI(2, 0, NEST_K), I_BEQ(2, 0, 3), I_ADDI(3, 3, 1),
    I_ADDI(2, 2, -1), I_BEQ(0, 0, -4), I_ADDI(1, 1, -1), I_BEQ(0, 0, -8), I_ST(3, 0, 0), I_HALT,
};
static const uint16_t prog_memcpy[] = {       /* ARCH-B09 prog_memcpy 그대로. dmem[0]=n, [1]=src, [2]=dst */
    I_LD(1, 0, 0), I_LD(2, 0, 1), I_LD(3, 0, 2),
    I_BEQ(1, 0, 6), I_LD(4, 2, 0), I_ST(4, 3, 0), I_ADDI(2, 2, 1), I_ADDI(3, 3, 1), I_ADDI(1, 1, -1), I_BEQ(0, 0, -7),
    I_HALT,
};
static const uint16_t prog_hand[] = {         /* 연습 5 */
    I_ADDI(1, 0, 3),      /* 0: ADDI r1, r0, 3   */
    I_LD(2, 1, 0),        /* 1: LD   r2, 0(r1)   */
    I_ADD(3, 2, 1),       /* 2: ADD  r3, r2, r1  */
    I_BEQ(3, 0, 1),       /* 3: BEQ  r3, r0, +1  */
    I_ADDI(4, 0, 1),      /* 4: ADDI r4, r0, 1   */
    I_HALT,               /* 5                   */
};

/* ===================== 테스트 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static uint32_t rng_state = 0x2545F491u;
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(void) {
    static uint16_t dm[DMEM_WORDS];

    printf("-- 연습 1: 스케줄링으로 load-use 숨기기 --\n");
    bool s_ok = true;
    for (unsigned N = 1; N <= 20; N++) {
        memset(dm, 0, sizeof dm);
        dm[0] = (uint16_t)N;
        for (unsigned i = 0; i < N; i++) dm[16 + i] = (uint16_t)xs32();
        trace(prog_sum, NELEM(prog_sum), dm);
        uint16_t want = g_a.dmem[1];
        uint64_t a_m01 = cyc(NOFWD_NT), a_fwd = cyc(FWD_NT), a_2b = cyc(FWD_2B), a_st = xeq(g_tr, g_ntr, FWD_NT)->stalls;
        trace(prog_sum_sched, NELEM(prog_sum_sched), dm);
        s_ok = s_ok && g_a.dmem[1] == want
            && a_m01 == 10 * N + 12 && cyc(NOFWD_NT) == 9 * N + 12
            && a_fwd == 9 * N + 12 && cyc(FWD_NT) == 8 * N + 12
            && a_2b == 8 * N + 13 && cyc(FWD_2B) == 7 * N + 13
            && a_st == N && xeq(g_tr, g_ntr, FWD_NT)->stalls == 0;
        if (N == 16)
            printf("      N=16  original noFwd/Fwd/Fwd+2BIT = %llu/%llu/%llu   scheduled = %llu/%llu/%llu\n",
                   (unsigned long long)a_m01, (unsigned long long)a_fwd, (unsigned long long)a_2b,
                   (unsigned long long)cyc(NOFWD_NT), (unsigned long long)cyc(FWD_NT), (unsigned long long)cyc(FWD_2B));
    }
    CHECK(s_ok);

    printf("-- 연습 2: BTFN 열 (모두 포워딩 ON) --\n");
    static uint16_t bdm[4][DMEM_WORDS];
    const uint16_t *bp[4] = { prog_sum, prog_hash, prog_count, prog_count };
    const size_t bn[4] = { NELEM(prog_sum), NELEM(prog_hash), NELEM(prog_count), NELEM(prog_count) };
    const char *bname[4] = { "sum N=64", "hash K=40", "count random", "count sorted" };
    memset(bdm, 0, sizeof bdm);
    rng_state = 0xB5297A4Du;                                     /* solution.c make_benches 와 같은 시드·순서 */
    bdm[0][0] = 64;
    for (int i = 0; i < 64; i++) bdm[0][16 + i] = (uint16_t)xs32();
    bdm[1][0] = 1; bdm[1][1] = 40;
    unsigned hits = 0;
    for (int k = 2; k < 4; k++) { bdm[k][0] = 200; bdm[k][1] = 7; }
    for (int i = 0; i < 200; i++) { bool m = xs32() & 1; hits += m; bdm[2][16 + i] = m ? 7 : 3; }
    for (int i = 0; i < 200; i++) bdm[3][16 + i] = (unsigned)i < hits ? 7 : 3;
    uint64_t c_nt[4], c_2b[4], c_bt[4], m_2b[4], m_bt[4];
    printf("      %-14s %8s %8s %8s   mispred 2BIT/BTFN\n", "program", "NT", "2BIT", "BTFN");
    for (int k = 0; k < 4; k++) {
        trace(bp[k], bn[k], bdm[k]);
        c_nt[k] = cyc(FWD_NT); c_2b[k] = cyc(FWD_2B); m_2b[k] = g_r.mispred;
        c_bt[k] = cyc(FWD_BTFN); m_bt[k] = g_r.mispred;
        printf("      %-14s %8llu %8llu %8llu   %llu/%llu\n", bname[k], (unsigned long long)c_nt[k],
               (unsigned long long)c_2b[k], (unsigned long long)c_bt[k], (unsigned long long)m_2b[k], (unsigned long long)m_bt[k]);
    }
    printf("      (count: hits = %u)\n", hits);
    CHECK(c_bt[0] == 8 * 64 + 12 && c_bt[1] == 9 * 40 + 12);     /* 웜업 미스 1번이 없어진다 */
    CHECK(m_bt[2] == hits + 1 && c_bt[2] < c_2b[2]);              /* 무작위: BTFN 이 2BIT 보다 낫다 */
    CHECK(c_2b[3] < c_bt[3]);                                     /* 정렬: 2BIT 가 낫다 */

    printf("-- 연습 3: 1비트 vs 2비트 (이중 루프 M=%d K=%d) --\n", NEST_M, NEST_K);
    trace(prog_nest, NELEM(prog_nest), NULL);
    const xres_t *r1 = xeq(g_tr, g_ntr, FWD_1B);
    uint32_t in1 = r1->mp_pc[3];
    uint64_t t1 = r1->mispred;
    const xres_t *r2 = xeq(g_tr, g_ntr, FWD_2B);
    printf("      inner-exit mispred 1BIT=%u 2BIT=%u, total 1BIT=%llu 2BIT=%llu\n",
           in1, r2->mp_pc[3], (unsigned long long)t1, (unsigned long long)r2->mispred);
    CHECK(in1 == 2 * NEST_M - 1 && t1 == 2 * NEST_M + 2);
    CHECK(r2->mp_pc[3] == NEST_M && r2->mispred == NEST_M + 3);

    printf("-- 연습 4: MEM->MEM 포워딩 (memcpy) --\n");
    bool m_ok = true;
    for (unsigned N = 1; N <= 16; N++) {
        memset(dm, 0, sizeof dm);
        dm[0] = (uint16_t)N; dm[1] = 32; dm[2] = 64;
        for (unsigned i = 0; i < N; i++) dm[32 + i] = (uint16_t)xs32();
        trace(prog_memcpy, NELEM(prog_memcpy), dm);
        m_ok = m_ok && memcmp(&g_a.dmem[64], &g_a.dmem[32], N * sizeof(uint16_t)) == 0
            && cyc(FWD_NT) == 10 * N + 11 && cyc(FWD_NT_MF) == 9 * N + 11;
        if (N == 16) printf("      N=16  Fwd+NT %llu, +MEM->MEM %llu\n",
                            (unsigned long long)cyc(FWD_NT), (unsigned long long)cyc(FWD_NT_MF));
    }
    CHECK(m_ok);
    /* ST 의 베이스가 LD 결과면 MEM→MEM 으로 못 넘긴다: 여전히 1사이클 */
    static const uint16_t base_ld[] = { I_LD(1, 0, 0), I_ST(2, 1, 0), I_HALT };
    memset(dm, 0, sizeof dm);
    dm[0] = 9;
    trace(base_ld, NELEM(base_ld), dm);
    CHECK(xeq(g_tr, g_ntr, FWD_NT_MF)->stalls == 1);

    printf("-- 연습 5: 손계산 --\n");
    memset(dm, 0, sizeof dm);
    dm[3] = 5;                                                   /* r3 = 8 → not taken */
    trace(prog_hand, NELEM(prog_hand), dm);
    CHECK(g_ntr == 6 && cyc(NOFWD_NT) == 16 && cyc(FWD_NT) == 11 && cyc(FWD_2B) == 11);
    CHECK(xeq(g_tr, g_ntr, NOFWD_NT)->stalls == 6 && xeq(g_tr, g_ntr, FWD_NT)->stalls == 1);
    dm[3] = 0xFFFD;                                              /* r3 = 0 → taken */
    trace(prog_hand, NELEM(prog_hand), dm);
    CHECK(g_ntr == 5 && cyc(NOFWD_NT) == 17 && cyc(FWD_NT) == 12 && cyc(FWD_2B) == 12 && g_a.reg[4] == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
