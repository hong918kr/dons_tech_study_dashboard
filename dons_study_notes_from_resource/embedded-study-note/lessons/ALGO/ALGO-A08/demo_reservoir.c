/* ALGO-A08 demo: (1) 맞는 Algorithm R 과 흔한 틀린 변형 4개를 카이제곱 두 종류로 검사
 *               (2) 샘플당 비용: Welford · 정수 이동 누산 · 고정소수점 EMA · Algorithm R (ns/sample, 5회 중앙값)
 * build: cc -std=c11 -O2 demo_reservoir.c -o demo_reservoir -lm && ./demo_reservoir
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_A08_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"
#include <time.h>

typedef bool (*push_fn)(reservoir_t *, int32_t);

static void slot(reservoir_t *r, uint32_t j, int32_t x) { r->item[j] = x; r->pos[j] = r->seen; }
/* B: j = U[0, i−1] (off-by-one) */
static bool push_offby1(reservoir_t *r, int32_t x) {
    if (r->seen < r->k) slot(r, r->seen, x);
    else { uint32_t j = rand_below(xs_word, &r->rng, r->seen, 32); if (j < r->k) slot(r, j, x); }
    r->seen++; return true;
}
/* C: 8-bit 난수 + 나머지(%) — seen+1 ≤ 256 일 때만 의미 */
static bool push_mod8(reservoir_t *r, int32_t x) {
    if (r->seen < r->k) slot(r, r->seen, x);
    else { uint32_t j = (xs32(&r->rng) >> 24) % (r->seen + 1); if (j < r->k) slot(r, j, x); }
    r->seen++; return true;
}
/* D: 8-bit 난수 + 거부 샘플링 (bits = 8) — 단어 폭이 작아도 거부하면 균일 */
static uint32_t word8(void *ctx) { return xs32((uint32_t *)ctx) >> 24; }
static bool push_rej8(reservoir_t *r, int32_t x) {
    if (r->seen < r->k) slot(r, r->seen, x);
    else { uint32_t j = rand_below(word8, &r->rng, r->seen + 1, 8); if (j < r->k) slot(r, j, x); }
    r->seen++; return true;
}
/* E: 교체 확률은 k/(i+1) 로 맞게, 하지만 칸은 돌아가며(round-robin) 고른다 */
static uint32_t rr;
static bool push_roundrobin(reservoir_t *r, int32_t x) {
    if (r->seen < r->k) { slot(r, r->seen, x); rr = 0; }
    else if (rand_below(xs_word, &r->rng, r->seen + 1, 32) < r->k) { slot(r, rr, x); rr = (rr + 1) % r->k; }
    r->seen++; return true;
}

static double subset_p(push_fn push) {
    enum { N7 = 7, K3 = 3, T = 70000 };
    int idx[128], nc = 0;
    for (int m = 0; m < 128; m++) idx[m] = __builtin_popcount((unsigned)m) == K3 ? nc++ : -1;
    uint32_t cnt[35] = {0};
    reservoir_t r; reservoir_init(&r, K3, 31337u);
    for (int t = 0; t < T; t++) {
        reservoir_reset(&r);
        for (int i = 0; i < N7; i++) push(&r, i);
        unsigned m = 0;
        for (int j = 0; j < K3; j++) m |= 1u << r.item[j];
        cnt[idx[m]]++;
    }
    double E = (double)T / 35, X = 0;
    for (int c = 0; c < 35; c++) X += (cnt[c] - E) * (cnt[c] - E) / E;
    return chi2_sf_even(X, 34);
}
static double incl_p(push_fn push, double *Xc_out) {
    enum { N = 201, K = 10, T = 20000 };
    uint32_t cnt[N] = {0};
    reservoir_t r; reservoir_init(&r, K, 4711u);
    for (int t = 0; t < T; t++) {
        reservoir_reset(&r);
        for (int i = 0; i < N; i++) push(&r, i);
        for (int j = 0; j < K; j++) cnt[r.item[j]]++;
    }
    double E = (double)T * K / N, X = 0;
    for (int i = 0; i < N; i++) X += (cnt[i] - E) * (cnt[i] - E) / E;
    *Xc_out = X * (N - 1) / (N - K);
    return chi2_sf_even(*Xc_out, N - 1);
}

static double now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e9 + ts.tv_nsec; }
#define NT 10000000
static int32_t in[NT];
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(void) {
    static const struct { const char *name; push_fn f; } S[] = {
        {"A  Algorithm R, rejection 32-bit", reservoir_push},
        {"B  off-by-one j=U[0,i-1]", push_offby1},
        {"C  8-bit word % (i+1)", push_mod8},
        {"D  8-bit word + rejection", push_rej8},
        {"E  right prob, round-robin slot", push_roundrobin},
    };
    printf("%-34s | subsets n=7 k=3 (df 34) p | inclusion n=201 k=10: corrected X (df 200), p\n", "sampler");
    for (int s = 0; s < 5; s++) {
        double Xc, ps = subset_p(S[s].f), pi = incl_p(S[s].f, &Xc);
        printf("%-34s | %25.3g | %10.1f  %.3g\n", S[s].name, ps, Xc, pi);
    }

    uint32_t sd = 1u;
    for (int i = 0; i < NT; i++) in[i] = 2000 + (int32_t)(xs32(&sd) % 4096u);
    const char *names[] = {"welford_push (double)", "welford_f_push (float)", "istats_push (int64 sums)", "ema_fx_step (k=4)", "reservoir_push (k=64)"};
    printf("\nper-sample cost, N=%d, median of 5 runs after 1 warm-up (ns/sample, this host):\n", NT);
    for (int f = 0; f < 5; f++) {
        double t[6]; volatile double sink = 0;
        for (int run = 0; run < 6; run++) {
            welford_t w = {0}; welford_f_t wf = {0}; istats_t is = {0}; ema_fx_t e; reservoir_t r;
            ema_fx_init(&e, in[0], 4); reservoir_init(&r, 64, 7u);
            double t0 = now_ns();
            switch (f) {
            case 0: for (int i = 0; i < NT; i++) welford_push(&w, in[i]); sink += w.m2; break;
            case 1: for (int i = 0; i < NT; i++) welford_f_push(&wf, (float)in[i]); sink += wf.m2; break;
            case 2: for (int i = 0; i < NT; i++) istats_push(&is, in[i]); sink += (double)is.s2; break;
            case 3: for (int i = 0; i < NT; i++) ema_fx_step(&e, in[i]); sink += e.acc; break;
            case 4: for (int i = 0; i < NT; i++) reservoir_push(&r, in[i]); sink += r.item[0]; break;
            }
            t[run] = (now_ns() - t0) / NT;
        }
        qsort(t + 1, 5, sizeof t[0], cmpd);
        printf("  %-26s %6.2f ns  (min %.2f, max %.2f)\n", names[f], t[3], t[1], t[5]);
        (void)sink;
    }
    return 0;
}
