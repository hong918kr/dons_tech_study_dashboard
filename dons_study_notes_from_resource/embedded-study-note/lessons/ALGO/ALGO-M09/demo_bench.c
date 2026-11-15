/* ALGO-M09 demo: 구간 그리디의 실행 시간 — 정렬이 얼마나 차지하나, O(n^2) 깊이와 비교
 * solution.c 의 함수를 그대로 쓴다 (MAX_IV 를 키워서 include). 각 측정 = 워밍업 1회 + 7회의 중앙값.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_M09_NO_MAIN
#define MAX_IV 65535
#define MAX_CH 64
#include "solution.c"
#include <stdlib.h>
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static uint32_t rng = 777u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static ival_t iv[MAX_IV], out[MAX_IV];
static uint16_t work[MAX_IV], pick[MAX_IV];
static uint32_t sb[MAX_IV], eb[MAX_IV];
static uint8_t chan[MAX_IV];
static volatile unsigned long sink;

static unsigned naive_depth(size_t n) {          /* 각 시작점에서 겹침 수를 센다: O(n^2) */
    unsigned best = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = 0;
        for (size_t j = 0; j < n; j++) c += (iv[j].s <= iv[i].s && iv[i].s < iv[j].e);
        if (c > best) best = c;
    }
    return best;
}

enum { T_SORT, T_SELECT, T_MERGE, T_DEPTH, T_DMA, T_NAIVE, T_COUNT };
static const char *const tname[T_COUNT] = { "sort_idx only", "max_nonoverlap", "merge_intervals", "max_depth sweep",
                                            "dma_assign heap", "naive depth O(n^2)" };
static double run_once(int what, size_t n) {
    uint32_t at;
    double t0 = now_ns();
    switch (what) {
    case T_SORT:   sort_idx(iv, n, work, BY_START); sink += work[n / 2]; break;
    case T_SELECT: sink += max_nonoverlap(iv, n, work, pick); break;
    case T_MERGE:  sink += merge_intervals(iv, n, work, out); break;
    case T_DEPTH:  sink += max_depth(iv, n, sb, eb, &at); break;
    case T_DMA:    sink += (unsigned long)dma_assign(iv, n, MAX_CH, work, chan); break;
    default:       sink += naive_depth(n); break;
    }
    return now_ns() - t0;
}
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(void) {
    static const size_t ns[] = { 1000, 10000, 60000 };
    printf("random intervals, average overlap ~8, MAX_CH=%d: s uniform in [0, 1000*n/8), len 1..2000\n", MAX_CH);
    printf("%-20s %10s %12s %12s\n", "function", "n", "median us", "ns/interval");
    for (size_t k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        size_t n = ns[k];
        uint32_t span = (uint32_t)(1000 * n / 8);
        for (size_t i = 0; i < n; i++) { uint32_t s = xs() % span, len = 1 + xs() % 2000; iv[i] = (ival_t){ s, s + len }; }
        uint32_t at;
        unsigned d = max_depth(iv, n, sb, eb, &at);
        int used = dma_assign(iv, n, MAX_CH, work, chan);
        printf("-- n=%zu: depth=%u, dma channels=%d, max_nonoverlap=%zu\n", n, d, used, max_nonoverlap(iv, n, work, pick));
        for (int w = 0; w < T_COUNT; w++) {
            if (w == T_NAIVE && n > 10000) { printf("%-20s %10zu %12s %12s\n", tname[w], n, "(skipped)", "-"); continue; }
            double t[7];
            run_once(w, n);
            for (int r = 0; r < 7; r++) t[r] = run_once(w, n);
            qsort(t, 7, sizeof t[0], cmp_d);
            printf("%-20s %10zu %12.1f %12.1f\n", tname[w], n, t[3] / 1e3, t[3] / (double)n);
        }
    }
    printf("(sink %lu)\n", (unsigned long)sink);
    return 0;
}
