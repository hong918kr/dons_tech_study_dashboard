/* ALGO-A10 demo: 반복 Tarjan vs 반복 Kosaraju 실행 시간 (간선 10^5 ~ 10^6), 그리고 로그 10^6 줄 분석 시간.
 * 각 측정 = 5번 중 최솟값. 매 회 두 결과의 정규형이 같은지도 확인한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_A10_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static uint32_t rng = 12345u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef enum { RANDOM, CHAIN, RING } shape_t;
static void bench(const char *name, shape_t shape, int n, int m) {
    int *s = malloc((size_t)m * sizeof(int)), *d = malloc((size_t)m * sizeof(int));
    for (int k = 0; k < m; k++) {
        if (shape == RANDOM) { s[k] = (int)(xs() % (uint32_t)n); d[k] = (int)(xs() % (uint32_t)n); }
        else { s[k] = k; d[k] = (k + 1) % n; }
    }
    graph_t g; graph_build(&g, n, m, s, d);
    int *c1 = malloc((size_t)n * sizeof(int)), *c2 = malloc((size_t)n * sizeof(int));
    int *k1 = malloc((size_t)n * sizeof(int)), *k2 = malloc((size_t)n * sizeof(int));
    double bt = 1e30, bk = 1e30;
    int n1 = 0, n2 = 0, same = 1, biggest = 0;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_ms(); n1 = scc_tarjan(&g, c1);   double t1 = now_ms();
        n2 = scc_kosaraju(&g, c2);                        double t2 = now_ms();
        if (t1 - t0 < bt) bt = t1 - t0;
        if (t2 - t1 < bk) bk = t2 - t1;
        scc_canon(n, c1, n1, k1); scc_canon(n, c2, n2, k2);
        if (n1 != n2 || memcmp(k1, k2, (size_t)n * sizeof(int)) != 0) same = 0;
    }
    int *sz = calloc((size_t)n1, sizeof(int));
    for (int v = 0; v < n; v++) if (++sz[c1[v]] > biggest) biggest = sz[c1[v]];
    printf("%-8s V=%8d E=%8d  SCC=%8d largest=%8d  tarjan %7.2f ms  kosaraju %7.2f ms  ratio K/T %.2f  ns/(V+E) T %.1f K %.1f  %s\n",
           name, n, m, n1, biggest, bt, bk, bk / bt, bt * 1e6 / (n + m), bk * 1e6 / (n + m), same ? "same" : "DIFFERENT");
    free(sz); free(s); free(d); free(c1); free(c2); free(k1); free(k2); graph_free(&g);
}

/* 스레드 8개, 락 1000개, 잠금 구간 25만 개(구간마다 연속 번호 락 1~3개를 오름차순으로) + 끝에 역순 구간 1개 → 사이클 1개 */
static void bench_log(void) {
    size_t cap = 32u << 20, len = 0;
    char *text = malloc(cap);
    int lines = 0;
    for (int sec = 0; sec < 250000; sec++) {
        int t = (int)(xs() % 8u), k = 1 + (int)(xs() % 3u), lk[3];
        lk[0] = (int)(xs() % 998u);
        for (int i = 1; i < k; i++) lk[i] = lk[i - 1] + 1;          /* 연속 번호, 항상 오름차순 */
        for (int i = 0; i < k; i++) { len += (size_t)snprintf(text + len, cap - len, "t=T%d acq L%04d @f.c:%d\n", t, lk[i], 100 + i); lines++; }
        for (int i = k - 1; i >= 0; i--) { len += (size_t)snprintf(text + len, cap - len, "t=T%d rel L%04d\n", t, lk[i]); lines++; }
    }
    len += (size_t)snprintf(text + len, cap - len, "t=T7 acq L0901 @bug.c:1\nt=T7 acq L0900 @bug.c:2\nt=T7 rel L0900\nt=T7 rel L0901\n");
    lines += 4;
    lockgraph_t lg; analysis_t a;
    double best_p = 1e30, best_a = 1e30;
    for (int rep = 0; rep < 3; rep++) {
        lg_init(&lg);
        double t0 = now_ms(); int rc = lg_parse(&lg, text); double t1 = now_ms();
        int rc2 = analyze(&lg, &a);                       double t2 = now_ms();
        if (t1 - t0 < best_p) best_p = t1 - t0;
        if (t2 - t1 < best_a) best_a = t2 - t1;
        if (rep == 2)
            printf("log: %d lines (%.1f MB), rc=%d/%d, %d locks, %d edges, deadlock=%d single-thread=%d  parse %.1f ms (%.0f ns/line)  analyze %.2f ms\n",
                   lines, len / 1e6, rc, rc2, lg.locks.n, lg.ne, a.count[V_DEADLOCK], a.count[V_SINGLE_THREAD],
                   best_p, best_p * 1e6 / lines, best_a);
        analysis_free(&a); lg_free(&lg);
    }
    free(text);
}

int main(void) {
    bench("random", RANDOM, 100000, 100000);
    bench("random", RANDOM, 100000, 1000000);
    bench("random", RANDOM, 1000000, 1000000);
    bench("random", RANDOM, 300000, 1000000);
    bench("chain", CHAIN, 1000000, 999999);
    bench("ring", RING, 1000000, 1000000);
    bench_log();
    return 0;
}
