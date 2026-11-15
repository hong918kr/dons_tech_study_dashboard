/* SIV-A01 추가 연습 — 본문의 DMA 블록을 채널 하나짜리 mini 모델로 줄여 코드만 따로 검증한다.
 * 1) 온라인 trace ring  2) 탐지 시퀀스 수 통계  3) 가중치 ↔ 실측 hit rate  4) 필드 축소  5) done-cycle cross coverage
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===== mini DUT: 채널 1개, sts[0]=DONE sts[1]=EVT ===== */
enum { M_NOP, M_START, M_EVT, M_W1C };
typedef struct { uint8_t k; uint32_t v; } mop_t;
typedef struct { uint32_t left, sts; } mini_t;
#define DONE 1u
#define EVTB 2u

static void mini_step(mini_t *m, const mop_t *o, bool bug) {
    uint32_t set = 0, clr = 0;
    bool busy = m->left != 0;
    if (m->left && --m->left == 0) set |= DONE;
    if (o->k == M_EVT) set |= EVTB;
    if (o->k == M_W1C) clr = o->v & 3u;
    if (o->k == M_START && !busy) m->left = o->v;
    if (bug && o->k == M_W1C && clr == EVTB) set &= ~DONE;    /* 본문의 BUG_RACE 를 축소 */
    m->sts = (m->sts & ~clr) | set;
}

static bool mini_mismatch(const mop_t *s, int n, int *first) {
    mini_t d = {0, 0}, g = {0, 0};
    for (int i = 0; i < n; i++) {
        mini_step(&d, &s[i], true);
        mini_step(&g, &s[i], false);
        if (d.sts != g.sts) { if (first) *first = i; return true; }
    }
    return false;
}

static uint32_t rnd32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

/* ===== 연습 1: 온라인 trace ring ===== */
#define RN 4
typedef struct { mop_t b[RN]; uint32_t total; } ring_t;
static void ring_push(ring_t *r, const mop_t *o) { r->b[r->total % RN] = *o; r->total++; }
static int ring_dump(const ring_t *r, mop_t *out, uint32_t *first_idx) {
    int n = r->total < RN ? (int)r->total : RN;
    *first_idx = r->total - (uint32_t)n;
    for (int i = 0; i < n; i++) out[i] = r->b[(*first_idx + (uint32_t)i) % RN];
    return n;
}

/* ===== 연습 2: 통계 ===== */
static double expected_seqs(double p) { return 1.0 / p; }
static int seqs_for_conf(double p, double cl) { return (int)ceil(log(1.0 - cl) / log(1.0 - p)); }

/* ===== 연습 3: 무작위 시퀀스 생성 + hit rate 측정 (seq_len, 가중치) ===== */
typedef struct { uint32_t w_nop, w_start, w_evt, w_w1c; } mix_t;

static void mini_gen(uint32_t *s, const mix_t *mx, int n, mop_t *out) {
    uint32_t tot = mx->w_nop + mx->w_start + mx->w_evt + mx->w_w1c;
    mini_t m = {0, 0};
    for (int i = 0; i < n; i++) {
        uint32_t r = rnd32(s) % tot;
        mop_t o = {M_NOP, 0};
        if (r < mx->w_nop) { o.k = M_NOP; }
        else if (r < mx->w_nop + mx->w_start) { o.k = M_START; o.v = 1u + rnd32(s) % 4u; }
        else if (r < mx->w_nop + mx->w_start + mx->w_evt) { o.k = M_EVT; }
        else {
            uint32_t pend = m.sts & 3u;
            if (pend) { o.k = M_W1C; o.v = (rnd32(s) & 1u) && (pend & EVTB) ? EVTB : pend; }
            else o.k = M_NOP;
        }
        out[i] = o;
        mini_step(&m, &o, false);
    }
}

static double measure_p(const mix_t *mx, int seq_len, int nseq, uint32_t seed) {
    mop_t s[64];
    uint32_t st = seed;
    int hits = 0;
    for (int i = 0; i < nseq; i++) {
        mini_gen(&st, mx, seq_len, s);
        if (mini_mismatch(s, seq_len, NULL)) hits++;
    }
    return (double)hits / nseq;
}

/* 평균 몇 시퀀스째에 처음 걸리는가 — 시드를 바꿔 trials 번 */
static double measure_mean_seqs(const mix_t *mx, int seq_len, int trials, uint32_t seed) {
    mop_t s[64];
    long total = 0;
    uint32_t st = seed;
    for (int t = 0; t < trials; t++) {
        int n = 0;
        do {
            n++;
            mini_gen(&st, mx, seq_len, s);
        } while (!mini_mismatch(s, seq_len, NULL) && n < 100000);
        total += n;
    }
    return (double)total / trials;
}

/* ===== 연습 4: 필드 축소 — LEN 과 필러를 같이 줄이기 ===== */
static int shrink_len_pair(mop_t *s, int n) {
    for (;;) {
        int st = -1, nop = -1;
        for (int i = 0; i < n; i++) { if (s[i].k == M_START && s[i].v > 1) st = i; }
        if (st < 0) return n;
        for (int i = st + 1; i < n; i++) { if (s[i].k == M_NOP) { nop = i; break; } }
        if (nop < 0) return n;
        mop_t c[64];
        int m = 0;
        for (int i = 0; i < n; i++) if (i != nop) c[m++] = s[i];
        c[st].v -= 1;
        if (!mini_mismatch(c, m, NULL)) return n;
        memcpy(s, c, (size_t)m * sizeof(mop_t));
        n = m;
    }
}

/* ===== 연습 5: done-cycle cross coverage: 완료 cycle 에 어떤 op 가 있었나 ===== */
#define COV_BINS 4       /* nop / W1C(EVT만) / W1C(그 외) / 그 밖 op */
static void cov_seq(const mop_t *s, int n, uint32_t *cov) {
    mini_t m = {0, 0};
    for (int i = 0; i < n; i++) {
        bool done_now = m.left == 1u;   /* 이 cycle 에 끝난다 */
        if (done_now) {
            const mop_t *o = &s[i];
            int b = o->k == M_NOP ? 0 : (o->k == M_W1C && o->v == EVTB) ? 1 : o->k == M_W1C ? 2 : 3;
            cov[b]++;
        }
        mini_step(&m, &s[i], false);
    }
}

static int main_checks(void) {
    /* 1: ring wrap */
    ring_t r;
    memset(&r, 0, sizeof r);
    mop_t o = {M_START, 0}, out[RN];
    uint32_t first;
    for (uint32_t i = 0; i < 6; i++) { o.v = i; ring_push(&r, &o); }
    int n = ring_dump(&r, out, &first);
    CHECK(n == RN && first == 2 && out[0].v == 2 && out[3].v == 5);
    ring_t r2;
    memset(&r2, 0, sizeof r2);
    o.v = 9;
    ring_push(&r2, &o);
    n = ring_dump(&r2, out, &first);
    CHECK(n == 1 && first == 0 && out[0].v == 9);

    /* 2: 통계 */
    CHECK(expected_seqs(0.125) == 8.0);
    CHECK(seqs_for_conf(0.05, 0.95) == 59);
    CHECK(seqs_for_conf(0.001, 0.95) == 2995);

    /* 3: 가중치 ↔ hit rate. 실측 평균 시퀀스 수와 1/p 비교 — 공차: 표본이 400 trial 이므로 ±25% */
    mix_t uni = {4, 2, 2, 2}, tuned = {1, 2, 2, 3};
    double pu = measure_p(&uni, 24, 3000, 1234);
    double pt = measure_p(&tuned, 24, 3000, 1234);
    CHECK(pu > 0 && pt > pu);
    double mean_t = measure_mean_seqs(&tuned, 24, 400, 77);
    CHECK(fabs(mean_t - expected_seqs(pt)) / expected_seqs(pt) < 0.25);

    /* 4: 필드 축소: EVT, START(5), NOP x4 (W1C 는 done 과 겹침) */
    mop_t s[64] = {{M_EVT, 0}, {M_START, 5}, {M_NOP, 0}, {M_NOP, 0}, {M_NOP, 0}, {M_NOP, 0}, {M_W1C, EVTB}};
    int f = -1;
    CHECK(mini_mismatch(s, 7, &f) && f == 6);
    int m = shrink_len_pair(s, 7);
    CHECK(m == 3 && s[0].k == M_EVT && s[1].k == M_START && s[1].v == 1 && s[2].k == M_W1C && mini_mismatch(s, m, NULL));

    /* 5: cross coverage: 가중치 0 이면 bin 이 영원히 hole, 가중치를 주면 W1C-EVT bin 이 채워진다 */
    mix_t no_w1c = {4, 2, 2, 0};
    uint32_t cv0[COV_BINS] = {0}, cv1[COV_BINS] = {0}, st = 42;
    mop_t q[64];
    for (int i = 0; i < 2000; i++) { mini_gen(&st, &no_w1c, 24, q); cov_seq(q, 24, cv0); }
    st = 42;
    for (int i = 0; i < 2000; i++) { mini_gen(&st, &tuned, 24, q); cov_seq(q, 24, cv1); }
    CHECK(cv0[0] > 0 && cv0[1] == 0 && cv0[2] == 0);
    CHECK(cv1[1] > 0 && cv1[2] > 0);
    printf("[practice] pu=%.4f pt=%.4f, mean_t=%.2f (1/pt=%.2f); done-cycle cov no_w1c: nop=%u w1c_evt=%u w1c_other=%u other=%u\n",
           pu, pt, mean_t, expected_seqs(pt), cv0[0], cv0[1], cv0[2], cv0[3]);
    printf("[practice] done-cycle cov tuned:  nop=%u w1c_evt=%u w1c_other=%u other=%u\n", cv1[0], cv1[1], cv1[2], cv1[3]);
    return 0;
}

int main(void) {
    main_checks();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
