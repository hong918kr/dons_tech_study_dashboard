/* ALGO-B06 demo: 입력 모양별 비교 횟수 · 최대 재귀 깊이 · 시간 (solution.c 의 계측 버전 그대로)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 * 시간 = 워밍업 1회 + 5회 측정의 중앙값. 계측(카운터 증가)이 들어간 코드라 절대값보다 비율을 볼 것.
 */
#define _POSIX_C_SOURCE 200809L
#define main b06_solution_main
#include "solution.c"
#undef main
#include <time.h>

static const char *const KIND[] = {"random", "0..9", "sorted", "reversed", "all-equal", "organ-pipe", "saw i%16", "INT_MIN/MAX"};
static int src[1 << 20], work[1 << 20];
static volatile int g_sink;

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp_dbl(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}
static void qsort_wrap(int *a, int n, qs_stats_t *st) { *st = (qs_stats_t){0}; qsort(a, (size_t)n, sizeof(int), cmp_int); }

/* 워밍업 1 + 측정 5 → 중앙값(ms). st 는 마지막 실행의 계측 */
static double time_sort(sort_fn f, int n, qs_stats_t *st) {
    double t[5];
    for (int r = -1; r < 5; r++) {
        memcpy(work, src, (size_t)n * sizeof(int));
        double t0 = now_ms();
        f(work, n, st);
        double t1 = now_ms();
        g_sink += work[n / 2];
        if (r >= 0) t[r] = t1 - t0;
    }
    qsort(t, 5, sizeof(double), cmp_dbl);
    return t[2];
}

int main(void) {
    static const sort_fn F[] = {quicksort_lomuto, quicksort_hoare, quicksort_3way, quicksort_safe, qsort_wrap};
    static const char *const NAME[] = {"Lomuto(last)", "Hoare(first)", "3-way(first)", "safe(m3+small)", "libc qsort"};
    const int n = 10000;
    printf("n = %d, comparisons / max depth / median ms (5 runs)\n", n);
    printf("%-12s", "input");
    for (int s = 0; s < 4; s++) printf(" | %-28s", NAME[s]);
    printf(" | %s\n", NAME[4]);
    for (int k = 0; k < K_COUNT; k++) {
        g_seed = 0xB06u;
        fill(src, n, k);
        printf("%-12s", KIND[k]);
        for (int s = 0; s < 5; s++) {
            qs_stats_t st;
            double ms = time_sort(F[s], n, &st);
            if (s < 4) printf(" | %9llu %5d %9.3f ms", (unsigned long long)st.cmp, st.max_depth, ms);
            else       printf(" | %8.3f ms", ms);
        }
        printf("\n");
    }

    printf("\nsafe: max depth vs floor(log2 n)\n%-9s %6s %8s %8s %8s %8s\n", "n", "bound", "random", "sorted", "equal", "organ");
    for (int n2 = 1 << 10; n2 <= 1 << 20; n2 <<= 2) {
        int m = n2 - 1;                            /* 2^k - 1: 경계 바로 아래 */
        printf("%-9d %6d", m, ilog2_floor((uint32_t)m));
        static const int kinds[] = {K_RANDOM, K_SORTED, K_EQUAL, K_ORGAN};
        for (int i = 0; i < 4; i++) {
            qs_stats_t st;
            g_seed = 0xB06u;
            fill(work, m, kinds[i]);
            quicksort_safe(work, m, &st);
            printf(" %8d", st.max_depth);
        }
        printf("\n");
    }
    printf("(sink %d)\n", g_sink & 1);
    return 0;
}
