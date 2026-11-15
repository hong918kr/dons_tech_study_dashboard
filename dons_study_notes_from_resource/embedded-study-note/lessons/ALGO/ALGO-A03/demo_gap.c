/* ALGO-A03 demo: 휴리스틱(최근접 이웃, NN + 2-opt)의 최적해 대비 격차(gap) — 무작위 보드 여러 장.
 * 보드 100 x 80 mm (10 µm 단위), 홈 (-5 mm, -5 mm). gap = (휴리스틱 − 최적) / 최적. 최적 = Held-Karp.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_gap.c -o demo_gap -lm && ./demo_gap
 */
#define ALGO_A03_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"
#include <stdlib.h>

static uint32_t rng = 777u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double g1[2000], g2[2000];

static void run(int n, int boards, metric_t m, int closed) {
    static tsp_t t;
    uint8_t o[MAXN], h[MAXN];
    int opt1 = 0, opt2 = 0, bad = 0;
    for (int b = 0; b < boards; b++) {
        pt_t p[MAXN];
        for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs() % 10001u); p[i].y = (int32_t)(xs() % 8001u); }
        build_board(&t, p, n, (pt_t){-500, -500}, m);
        cost_t op = tsp_solve(&t, closed, o), nn = nn_tour(&t, closed, h), tw = two_opt(&t, closed, h);
        if (!(op <= tw && tw <= nn)) bad++;
        g1[b] = 100.0 * (nn - op) / op; g2[b] = 100.0 * (tw - op) / op;
        opt1 += nn == op; opt2 += tw == op;
    }
    double s1 = 0, s2 = 0;
    for (int b = 0; b < boards; b++) { s1 += g1[b]; s2 += g2[b]; }
    qsort(g1, (size_t)boards, sizeof g1[0], cmpd);
    qsort(g2, (size_t)boards, sizeof g2[0], cmpd);
    int p95 = (int)(0.95 * (boards - 1));
    printf("%-9s %-6s n=%2d boards=%4d | NN  mean %5.2f%% med %5.2f%% p95 %5.2f%% max %5.2f%% opt %5.1f%% | NN+2opt mean %5.2f%% med %5.2f%% p95 %5.2f%% max %5.2f%% opt %5.1f%%%s\n",
           metric_name[m], closed ? "closed" : "open", n, boards,
           s1 / boards, g1[boards / 2], g1[p95], g1[boards - 1], 100.0 * opt1 / boards,
           s2 / boards, g2[boards / 2], g2[p95], g2[boards - 1], 100.0 * opt2 / boards, bad ? "  ORDER VIOLATION" : "");
}

int main(void) {
    run(8, 2000, MET_CHEBYSHEV, 0);
    run(12, 1000, MET_CHEBYSHEV, 0);
    run(16, 200, MET_CHEBYSHEV, 0);
    run(16, 200, MET_CHEBYSHEV, 1);
    run(16, 200, MET_EUCLID, 1);
    return 0;
}
