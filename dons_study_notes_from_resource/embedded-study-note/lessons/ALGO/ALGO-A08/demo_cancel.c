/* ALGO-A08 demo: 평균이 커질수록(κ = 1 + |평균|/σ 가 커질수록) 각 분산 공식의 상대오차가 어떻게 자라나.
 * 기준값 = n·Σx² − (Σx)² 를 __int128 로 정확히 계산 (데이터가 정수라 가능).
 * build: cc -std=c11 -O2 demo_cancel.c -o demo_cancel -lm && ./demo_cancel
 */
#define ALGO_A08_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"

#define N 100000
static int64_t x[N];

static double var_exact(const int64_t *v, uint32_t n) {
    __int128 s1 = 0, s2 = 0;
    for (uint32_t i = 0; i < n; i++) { s1 += v[i]; s2 += (__int128)v[i] * v[i]; }
    return (double)((__int128)n * s2 - s1 * s1) / ((double)n * (double)(n - 1));
}
static double re(double got, double want) { return fabs(got - want) / want; }

int main(void) {
    const double u = ldexp(1.0, -53), uf = ldexp(1.0, -24);
    printf("double, n=%d, x = offset + U{-100..100} (sigma ~ 58)\n", N);
    printf("%-12s %9s %9s | %10s %10s %10s %10s %10s\n", "offset", "u*k", "u*k^2", "naive", "welford", "two-pass", "merge x8", "int-shift");
    static const double offs[] = {0, 1e3, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10};
    for (int c = 0; c < 8; c++) {
        uint32_t s = 2024u;
        for (uint32_t i = 0; i < N; i++) x[i] = (int64_t)offs[c] + (int64_t)(xs32(&s) % 201u) - 100;
        double ex = var_exact(x, N), m = 0;
        for (uint32_t i = 0; i < N; i++) m += (double)x[i];
        m /= N;
        double kap = 1 + fabs(m) / sqrt(ex);
        naive_t a = {0}; welford_t w = {0}, all = {0}; istats_t is = {0};
        bool fits = true;
        for (uint32_t i = 0; i < N; i++) {
            naive_push(&a, (double)x[i]); welford_push(&w, (double)x[i]);
            if (x[i] >= INT32_MIN && x[i] <= INT32_MAX) fits &= istats_push(&is, (int32_t)x[i]); else fits = false;
        }
        for (int part = 0; part < 8; part++) {             /* 8 조각(코어 8개라 치고) → Chan 병합 */
            welford_t p = {0};
            for (uint32_t i = (uint32_t)part * (N / 8); i < (uint32_t)(part + 1) * (N / 8); i++) welford_push(&p, (double)x[i]);
            welford_merge(&all, &p);
        }
        double tp = 0;
        for (uint32_t i = 0; i < N; i++) { double d = (double)x[i] - m; tp += d * d; }
        tp /= (N - 1);
        char ist[16];
        if (fits) snprintf(ist, sizeof ist, "%.1e", re(istats_var(&is), ex)); else snprintf(ist, sizeof ist, "(>int32)");
        printf("%-12.0f %9.1e %9.1e | %10.1e %10.1e %10.1e %10.1e %10s%s\n", offs[c], u * kap, u * kap * kap,
               re(naive_var(&a), ex), re(welford_var(&w), ex), re(tp, ex), re(welford_var(&all), ex), ist,
               naive_var(&a) < 0 ? "  (naive < 0!)" : "");
    }
    printf("\nfloat (Cortex-M4F style), n=%d, x = offset + U{-8..8} (sigma ~ 4.9), all x exact in float\n", N);
    printf("%-12s %9s %9s | %10s %10s %10s\n", "offset", "uf*k", "uf*k^2", "naive f32", "welford f32", "int-shift");
    static const double offf[] = {0, 1e2, 1e3, 1e4, 1e5, 1e6, 8e6};
    for (int c = 0; c < 7; c++) {
        uint32_t s = 99u;
        for (uint32_t i = 0; i < N; i++) x[i] = (int64_t)offf[c] + (int64_t)(xs32(&s) % 17u) - 8;
        double ex = var_exact(x, N), m = 0;
        for (uint32_t i = 0; i < N; i++) m += (double)x[i];
        m /= N;
        double kap = 1 + fabs(m) / sqrt(ex);
        naive_f_t a = {0}; welford_f_t w = {0}; istats_t is = {0};
        for (uint32_t i = 0; i < N; i++) { naive_f_push(&a, (float)x[i]); welford_f_push(&w, (float)x[i]); istats_push(&is, (int32_t)x[i]); }
        printf("%-12.0f %9.1e %9.1e | %10.1e %10.1e %10.1e\n", offf[c], uf * kap, uf * kap * kap,
               re(naive_f_var(&a), ex), re(welford_f_var(&w), ex), re(istats_var(&is), ex));
    }
    return 0;
}
