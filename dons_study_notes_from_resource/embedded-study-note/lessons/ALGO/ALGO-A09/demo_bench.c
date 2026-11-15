/* ALGO-A09 demo_bench — 스위프가 왜 필요한가를 숫자로.
 *   1) 겹침 쌍: 스위프 O(n log n + k) vs 모든 쌍 O(n^2) — 성긴 맵(k ≈ n)과 빽빽한 맵(k = n(n-1)/2)
 *   2) 직사각형 합집합: 슬랩 O(n^2 log n) vs 세그먼트 트리 O(n log n)
 * solution.c 를 그대로 include 한다 (MAX 만 키움, main 제외). 각 칸 = 워밍업 1회 + 5회 측정의 중앙값 [최소–최대].
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define MM_MAX 65536
#define RECT_MAX 4096
#define ALGO_A09_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"   /* 보고서·ISR 함수는 여기서 안 쓴다 */
#include "solution.c"
#include <stdlib.h>
#include <time.h>

static uint32_t g_r = 12345u;
static uint32_t rnd(void) { uint32_t x = g_r; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_r = x; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static region_t R[MM_MAX];
static rect_t RR[RECT_MAX];
static ovpair_t OUT[1 << 16];
static volatile uint64_t sink;

static size_t brute_count(const region_t *r, size_t n) {
    size_t k = 0;
    for (size_t a = 0; a < n; a++)
        for (size_t b = a + 1; b < n; b++)
            if (r[a].size && r[b].size && r[a].base < reg_end(&r[b]) && r[b].base < reg_end(&r[a])) k++;
    return k;
}

#define RUNS 5
typedef enum { F_SWEEP, F_SWEEP_COUNT, F_BRUTE, F_SLAB, F_SEG } fn_t;
static double timeit(fn_t f, size_t n, double *lo, double *hi, uint64_t *res) {
    double t[RUNS];
    for (int i = -1; i < RUNS; i++) {
        double t0 = now_ms();
        uint64_t v = 0;
        switch (f) {
        case F_SWEEP: v = mm_overlaps(R, n, OUT, sizeof OUT / sizeof OUT[0]); break;
        case F_SWEEP_COUNT: v = mm_overlaps(R, n, OUT, 0); break;
        case F_BRUTE: v = brute_count(R, n); break;
        case F_SLAB: v = rect_union_slab(RR, n, NULL); break;
        case F_SEG: v = rect_union_segtree(RR, n); break;
        }
        double dt = now_ms() - t0;
        sink += v;
        *res = v;
        if (i >= 0) t[i] = dt;
    }
    qsort(t, RUNS, sizeof t[0], cmpd);
    *lo = t[0]; *hi = t[RUNS - 1];
    return t[RUNS / 2];
}

int main(void) {
    printf("1) overlapping pairs, sparse map (sizes ~ 2 * span/n -> k ~ n)\n");
    printf("%7s %9s | %22s | %22s | %8s\n", "n", "k", "sweep ms [min-max]", "brute ms [min-max]", "brute/sw");
    size_t ns[] = {1000, 4000, 16000, 64000};
    for (size_t q = 0; q < 4; q++) {
        size_t n = ns[q];
        uint32_t span = 1u << 31, avg = (uint32_t)(span / n);
        for (size_t i = 0; i < n; i++) R[i] = (region_t){"r", rnd() % span, 1 + rnd() % (2 * avg), RK_SEC};
        double lo, hi, lo2 = 0, hi2 = 0, b = 0;
        uint64_t k, kb = 0;
        double s = timeit(F_SWEEP, n, &lo, &hi, &k);
        if (n <= 16000) b = timeit(F_BRUTE, n, &lo2, &hi2, &kb);
        if (n <= 16000)
            printf("%7zu %9" PRIu64 " | %7.3f [%6.3f-%6.3f] | %7.2f [%6.2f-%6.2f] | %8.0f%s\n", n, k, s, lo, hi, b, lo2, hi2, b / s,
                   kb == k ? "" : "  MISMATCH");
        else
            printf("%7zu %9" PRIu64 " | %7.3f [%6.3f-%6.3f] | %22s |\n", n, k, s, lo, hi, "(skipped: n^2/2 = 2e9)");
    }
    printf("\n   dense map (all regions identical -> k = n(n-1)/2), count only (cap = 0)\n");
    size_t nd[] = {1000, 2000, 4000};
    for (size_t q = 0; q < 3; q++) {
        size_t n = nd[q];
        for (size_t i = 0; i < n; i++) R[i] = (region_t){"r", 0x1000, 0x100, RK_SEC};
        double lo, hi, lo2, hi2;
        uint64_t k, kb;
        double s = timeit(F_SWEEP_COUNT, n, &lo, &hi, &k), b = timeit(F_BRUTE, n, &lo2, &hi2, &kb);
        printf("%7zu %9" PRIu64 " | %7.3f [%6.3f-%6.3f] | %7.2f [%6.2f-%6.2f] | %8.1f  ns/pair sweep=%.2f\n",
               n, k, s, lo, hi, b, lo2, hi2, b / s, s * 1e6 / (double)k);
    }

    printf("\n2) rectangle union area, random rects in [0, 2^20)^2, side < 2^17\n");
    printf("%6s | %22s | %22s | %8s\n", "n", "slab ms [min-max]", "segtree ms [min-max]", "slab/seg");
    size_t nr[] = {125, 250, 500, 1000, 2000, 4000};
    for (size_t q = 0; q < 6; q++) {
        size_t n = nr[q];
        for (size_t i = 0; i < n; i++) {
            int32_t x = (int32_t)(rnd() % (1u << 20)), y = (int32_t)(rnd() % (1u << 20));
            RR[i] = (rect_t){x, y, x + 1 + (int32_t)(rnd() % (1u << 17)), y + 1 + (int32_t)(rnd() % (1u << 17))};
        }
        double lo, hi, lo2, hi2;
        uint64_t a1, a2;
        double s = timeit(F_SLAB, n, &lo, &hi, &a1), g = timeit(F_SEG, n, &lo2, &hi2, &a2);
        printf("%6zu | %7.3f [%6.3f-%6.3f] | %7.3f [%6.3f-%6.3f] | %8.1f%s\n", n, s, lo, hi, g, lo2, hi2, s / g,
               a1 == a2 ? "" : "  MISMATCH");
    }
    printf("(sink %" PRIu64 ")\n", (uint64_t)sink);
    return 0;
}
