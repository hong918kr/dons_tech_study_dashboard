/* ALGO-B08 demo_bench — 계수/기수 정렬 vs qsort: 속도와 추가 메모리
 *
 *   표 1  12비트 ADC (uint16, 0..4095)   : qsort vs 계수 정렬(카운트에서 다시 쓰기)
 *   표 2  uint32 무작위 타임스탬프        : qsort vs 8비트 LSD 기수 정렬(4 패스)
 *   표 3  계수 정렬이 지는 곳            : n 작음 × 키 범위 M 큼 (카운트 배열을 지우고 훑는 O(M) 비용)
 *   표 4  아주 작은 n                    : 삽입 정렬 vs qsort vs 기수 정렬
 *
 * 측정: 이 맥(Apple Silicon M 계열), -O2. 한 측정 = reps 번 (memcpy 로 입력 복원 + 정렬).
 *       입력은 서로 다른 배열 P 개(합 4M 원소)를 돌려 쓴다 — 같은 입력을 반복하면 분기 예측기가
 *       qsort 의 비교 결과를 외워 버려 작은 n 에서 qsort 가 비현실적으로 빨라진다 (5. 풀이 참고).
 *       워밍업 1 번 후 7 번 측정의 중앙값, 같은 reps 의 memcpy 만 한 시간을 빼서 ns/원소로 표시.
 *       결과 원소를 volatile sink 에 더해 컴파일러가 작업을 지우지 못하게 한다. 절대값보다 비율을 볼 것.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t g_rng = 0x12345678u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
#ifdef CLOCK_UPTIME_RAW
#define BENCH_CLOCK CLOCK_UPTIME_RAW   /* macOS: ns 단위. CLOCK_MONOTONIC 은 이 맥에서 1 µs 단위였다 */
#else
#define BENCH_CLOCK CLOCK_MONOTONIC
#endif
static double now_ns(void) { struct timespec ts; clock_gettime(BENCH_CLOCK, &ts); return ts.tv_sec * 1e9 + ts.tv_nsec; }
static volatile uint64_t g_sink;

static int cmp_u16(const void *p, const void *q) { uint16_t x = *(const uint16_t *)p, y = *(const uint16_t *)q; return (x > y) - (x < y); }
static int cmp_u32(const void *p, const void *q) { uint32_t x = *(const uint32_t *)p, y = *(const uint32_t *)q; return (x > y) - (x < y); }

/* ---- 정렬들 (solution.c 와 같은 알고리즘) ---- */
static uint32_t CNT[1u << 20];   /* 표 3 에서 M = 2^20 까지 → 4 MB */
static void count_sort_u16_12(uint16_t *a, size_t n) {
    memset(CNT, 0, 4096 * sizeof CNT[0]);
    for (size_t i = 0; i < n; i++) CNT[a[i]]++;
    size_t k = 0;
    for (uint32_t v = 0; v < 4096; v++) for (uint32_t c = CNT[v]; c > 0; c--) a[k++] = (uint16_t)v;
}
static size_t g_M;               /* 표 3 의 키 범위 */
static void count_sort_u32_M(uint32_t *a, size_t n) {
    memset(CNT, 0, g_M * sizeof CNT[0]);
    for (size_t i = 0; i < n; i++) CNT[a[i]]++;
    size_t k = 0;
    for (uint32_t v = 0; v < g_M; v++) for (uint32_t c = CNT[v]; c > 0; c--) a[k++] = v;
}
static uint32_t *g_tmp;
static void radix_sort_u32(uint32_t *a, size_t n) {
    uint32_t *src = a, *dst = g_tmp;
    for (unsigned sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0};
        for (size_t i = 0; i < n; i++) cnt[(src[i] >> sh) & 0xFFu]++;
        uint32_t sum = 0;
        for (unsigned d = 0; d < 256; d++) { uint32_t c = cnt[d]; cnt[d] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) dst[cnt[(src[i] >> sh) & 0xFFu]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
}
static void insertion_u32(uint32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) { uint32_t v = a[i]; size_t j = i; while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; }
}
static void qsort_u16(uint16_t *a, size_t n) { qsort(a, n, sizeof a[0], cmp_u16); }
static void qsort_u32(uint32_t *a, size_t n) { qsort(a, n, sizeof a[0], cmp_u32); }
static void nop_u16(uint16_t *a, size_t n) { (void)a; (void)n; }
static void nop_u32(uint32_t *a, size_t n) { (void)a; (void)n; }

/* ---- 타이머: reps 번 (복원 + 정렬), 7 번 중앙값 ---- */
#define RUNS 7
#define POOL 4000000u                  /* 입력 풀 = 4M 원소 */
static int cmp_d(const void *p, const void *q) { double x = *(const double *)p, y = *(const double *)q; return (x > y) - (x < y); }
static double med_u16(void (*f)(uint16_t *, size_t), const uint16_t *in, uint16_t *w, size_t n, int reps) {
    double t[RUNS];
    size_t P = POOL / n;                       /* 서로 다른 입력 배열 수 */
    for (int r = -1; r < RUNS; r++) {
        double t0 = now_ns();
        for (int k = 0; k < reps; k++) { memcpy(w, in + ((size_t)k % P) * n, n * sizeof w[0]); f(w, n); g_sink += w[n / 2]; }
        double t1 = now_ns();
        if (r >= 0) t[r] = t1 - t0;
    }
    qsort(t, RUNS, sizeof t[0], cmp_d);
    return t[RUNS / 2];
}
static double med_u32(void (*f)(uint32_t *, size_t), const uint32_t *in, uint32_t *w, size_t n, int reps) {
    double t[RUNS];
    size_t P = POOL / n;                       /* 서로 다른 입력 배열 수 */
    for (int r = -1; r < RUNS; r++) {
        double t0 = now_ns();
        for (int k = 0; k < reps; k++) { memcpy(w, in + ((size_t)k % P) * n, n * sizeof w[0]); f(w, n); g_sink += w[n / 2]; }
        double t1 = now_ns();
        if (r >= 0) t[r] = t1 - t0;
    }
    qsort(t, RUNS, sizeof t[0], cmp_d);
    return t[RUNS / 2];
}
static double per_call_u32(void (*f)(uint32_t *, size_t), const uint32_t *in, uint32_t *w, size_t n, int reps) {
    return (med_u32(f, in, w, n, reps) - med_u32(nop_u32, in, w, n, reps)) / reps;
}
static int reps_for(size_t n) { size_t r = 4000000 / n; return r < 3 ? 3 : (int)r; }

int main(void) {
    enum { NBIG = 1000000 };
    uint16_t *in16 = malloc(POOL * sizeof *in16), *w16 = malloc(NBIG * sizeof *w16);
    uint32_t *in32 = malloc(POOL * sizeof *in32), *w32 = malloc(NBIG * sizeof *w32);
    g_tmp = malloc(NBIG * sizeof *g_tmp);
    if (!in16 || !w16 || !in32 || !w32 || !g_tmp) return 1;
    static const size_t ns[] = { 1000, 10000, 100000, 1000000 };

    printf("표 1: 12-bit ADC uint16 (0..4095), ns/elem (median of %d)\n", RUNS);
    printf("%9s %10s %10s %8s %16s\n", "n", "qsort", "counting", "speedup", "extra mem (cnt)");
    for (size_t s = 0; s < 4; s++) {
        size_t n = ns[s]; int reps = reps_for(n);
        for (size_t i = 0; i < POOL; i++) in16[i] = (uint16_t)(xs32() & 0xFFFu);
        double base = med_u16(nop_u16, in16, w16, n, reps);
        double q = (med_u16(qsort_u16, in16, w16, n, reps) - base) / ((double)reps * n);
        double c = (med_u16(count_sort_u16_12, in16, w16, n, reps) - base) / ((double)reps * n);
        printf("%9zu %10.2f %10.2f %7.1fx %13zu B\n", n, q, c, q / c, (size_t)4096 * sizeof(uint32_t));
    }

    printf("\n표 2: uint32 random, ns/elem (median of %d)\n", RUNS);
    printf("%9s %10s %10s %8s %22s\n", "n", "qsort", "radix8x4", "speedup", "extra mem (cnt+tmp)");
    for (size_t s = 0; s < 4; s++) {
        size_t n = ns[s]; int reps = reps_for(n);
        for (size_t i = 0; i < POOL; i++) in32[i] = xs32();
        double base = med_u32(nop_u32, in32, w32, n, reps);
        double q = (med_u32(qsort_u32, in32, w32, n, reps) - base) / ((double)reps * n);
        double r = (med_u32(radix_sort_u32, in32, w32, n, reps) - base) / ((double)reps * n);
        printf("%9zu %10.2f %10.2f %7.1fx %10zu + %9zu B\n", n, q, r, q / r, (size_t)256 * sizeof(uint32_t), n * sizeof(uint32_t));
    }

    printf("\n표 3: counting sort with key range M vs qsort, ns per SORT CALL (not per elem)\n");
    printf("%7s %9s %12s %12s %12s %10s\n", "n", "M", "counting", "qsort", "insertion", "cnt bytes");
    static const size_t tn[] = { 16, 256, 4096 };
    static const size_t tm[] = { 1u << 8, 1u << 12, 1u << 16, 1u << 20 };
    for (size_t a = 0; a < 3; a++) {
        for (size_t b = 0; b < 4; b++) {
            size_t n = tn[a]; g_M = tm[b];
            for (size_t i = 0; i < POOL; i++) in32[i] = xs32() % (uint32_t)g_M;
            /* 방법마다 reps 를 따로: 계수 정렬은 n+M 에, 비교 정렬은 n 에 비례하는 시간 */
            int rc = (int)(4000000 / (n + g_M)); if (rc < 20) rc = 20;
            int rq = (int)(400000 / n);
            int ri = n >= 4096 ? 5 : rq;   /* 삽입 정렬 n=4096 은 한 번에 ~1 ms → 반복 줄임 */
            double c = per_call_u32(count_sort_u32_M, in32, w32, n, rc);
            double q = per_call_u32(qsort_u32, in32, w32, n, rq);
            double ins = per_call_u32(insertion_u32, in32, w32, n, ri);
            printf("%7zu %9zu %12.0f %12.0f %12.0f %10zu\n", n, g_M, c, q, ins, g_M * sizeof(uint32_t));
        }
    }

    printf("\n표 4: tiny n, uint32 random, ns per SORT CALL\n");
    printf("%7s %12s %12s %12s\n", "n", "insertion", "qsort", "radix8x4");
    static const size_t sn[] = { 8, 32, 128, 512 };
    for (size_t s = 0; s < 4; s++) {
        size_t n = sn[s]; int reps = (int)(400000 / n);
        for (size_t i = 0; i < POOL; i++) in32[i] = xs32();
        double base = med_u32(nop_u32, in32, w32, n, reps);
        double ins = (med_u32(insertion_u32, in32, w32, n, reps) - base) / reps;
        double q = (med_u32(qsort_u32, in32, w32, n, reps) - base) / reps;
        double r = (med_u32(radix_sort_u32, in32, w32, n, reps) - base) / reps;
        printf("%7zu %12.0f %12.0f %12.0f\n", n, ins, q, r);
    }
    printf("\n(sink=%llu)\n", (unsigned long long)g_sink);
    free(in16); free(w16); free(in32); free(w32); free(g_tmp);
    return 0;
}
