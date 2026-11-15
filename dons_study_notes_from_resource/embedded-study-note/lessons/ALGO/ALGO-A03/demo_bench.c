/* ALGO-A03 demo: Held-Karp 시간·메모리 vs n, 그리고 순열 브루트포스 시간 (n! 의 벽).
 * 체비쇼프 무작위 보드(100 x 80 mm, 10 µm 단위), open. 각 n: warm-up 1회 + 7회 → 중앙값과 최소·최대.
 * 브루트포스: n=8..11 은 3회 중앙값, n=12 는 1회. 결과(비용)가 DP 와 같은지도 확인한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_A03_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"
#include <stdlib.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static uint32_t rng = 2026u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static uint64_t best;
static void bf(const tsp_t *t, uint32_t used, int last, int depth, uint64_t acc) {
    if (depth == t->n) { if (acc < best) best = acc; return; }
    for (int j = 0; j < t->n; j++)
        if (!((used >> j) & 1u)) bf(t, used | (1u << j), j, depth + 1, acc + t->d[last][j]);
}

int main(void) {
    printf("memory (bytes): n  states=2^n*n  dp uint32  par uint8  total     folded uint16 (n*2^(n-1)*2, no par)\n");
    for (int n = 8; n <= 16; n++) {
        uint64_t st = (uint64_t)n << n;
        printf("               %2d  %10llu  %9llu  %9llu  %9llu  %9llu\n", n, (unsigned long long)st,
               (unsigned long long)(st * 4), (unsigned long long)st, (unsigned long long)(st * 5),
               (unsigned long long)((uint64_t)n << (n - 1)) * 2);
    }
    printf("\nHeld-Karp (open, chebyshev): n  transitions   median ms  [min, max]   ns/transition  ratio to n-1\n");
    double prev = 0;
    static tsp_t t;
    uint8_t o[MAXN];
    for (int n = 8; n <= 16; n++) {
        pt_t p[MAXN];
        for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs() % 10001u); p[i].y = (int32_t)(xs() % 8001u); }
        build_board(&t, p, n, (pt_t){-500, -500}, MET_CHEBYSHEV);
        volatile cost_t sink = tsp_solve(&t, 0, o);
        double ms[7];
        for (int r = 0; r < 7; r++) { double a = now_ms(); sink = tsp_solve(&t, 0, o); ms[r] = now_ms() - a; }
        (void)sink;
        qsort(ms, 7, sizeof ms[0], cmpd);
        printf("                               %2d  %10llu  %9.3f  [%.3f, %.3f]  %8.2f  %s",
               n, (unsigned long long)g_trans, ms[3], ms[0], ms[6], ms[3] * 1e6 / (double)g_trans, prev > 0 ? "" : "-\n");
        if (prev > 0) printf("x%.2f\n", ms[3] / prev);
        prev = ms[3];
    }
    printf("\nbrute force (all n! orders): n  n!          median ms   DP ms     same cost\n");
    for (int n = 8; n <= 12; n++) {
        pt_t p[MAXN];
        for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs() % 10001u); p[i].y = (int32_t)(xs() % 8001u); }
        build_board(&t, p, n, (pt_t){-500, -500}, MET_CHEBYSHEV);
        int runs = n <= 11 ? 3 : 1;
        double ms[3];
        for (int r = 0; r < runs; r++) { double a = now_ms(); best = UINT64_MAX; bf(&t, 0, n, 0, 0); ms[r] = now_ms() - a; }
        qsort(ms, (size_t)runs, sizeof ms[0], cmpd);
        double a = now_ms();
        cost_t c = tsp_solve(&t, 0, o);
        double dpms = now_ms() - a;
        uint64_t f = 1;
        for (int k = 2; k <= n; k++) f *= (uint64_t)k;
        printf("                                %2d  %10llu  %10.2f  %8.3f   %s\n", n, (unsigned long long)f, ms[runs / 2], dpms,
               (uint64_t)c == best ? "yes" : "NO");
    }
    return 0;
}
