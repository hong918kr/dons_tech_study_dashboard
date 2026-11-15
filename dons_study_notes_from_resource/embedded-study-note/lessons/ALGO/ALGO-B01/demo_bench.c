/* ALGO-B01 demo_bench.c — 세 가지 중복 검사의 시간·메모리 실측 + 로그-로그 기울기
 *
 *   cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench -lm && ./demo_bench
 *
 * 입력: 서로 다른 32비트 값 n 개 (중복 없음 = 세 방법 모두 끝까지 일하는 최악 경우).
 * 시간: 한 번 워밍업 → "20 ms 이상 될 때까지 반복한 평균" 을 5번 → 중앙값. 결과는 volatile sink 로.
 * 메모리: 방법별 작업 메모리(코드가 실제로 잡는 바이트) + fork 한 자식의 ru_maxrss(peak RSS).
 * 해시 캐시 스윕: 적재율을 0.375 로 고정하고 테이블 크기만 8 KiB → 128 MiB 로 키운다.
 * 약 1분 걸린다. 절대값(ns)보다 비율과 기울기를 본다.
 */
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

/* ---- solution.c 와 같은 세 함수 (자급자족을 위해 복사) ---- */
static bool has_dup_pairwise(const uint32_t *a, size_t n) {
    for (size_t i = 0; i + 1 < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (a[i] == a[j]) return true;
    return false;
}
static int cmp_u32(const void *pa, const void *pb) {
    uint32_t x = *(const uint32_t *)pa, y = *(const uint32_t *)pb;
    return (x > y) - (x < y);
}
static bool has_dup_sort(const uint32_t *a, size_t n, uint32_t *scratch) {
    if (n < 2) return false;
    memcpy(scratch, a, n * sizeof *a);
    qsort(scratch, n, sizeof *scratch, cmp_u32);
    for (size_t i = 1; i < n; i++)
        if (scratch[i - 1] == scratch[i]) return true;
    return false;
}
static unsigned log2_cap_for(size_t n) {
    unsigned k = 1;
    while (((uint64_t)1 << k) < 2 * (uint64_t)n) k++;
    return k;
}
static int has_dup_hash(const uint32_t *a, size_t n, uint32_t *buf, unsigned k) {
    uint32_t mask = (1u << k) - 1u, shift = 32u - k, count = 0;
    bool has_zero = false;
    memset(buf, 0, ((size_t)1 << k) * sizeof *buf);
    for (size_t j = 0; j < n; j++) {
        uint32_t x = a[j];
        if (x == 0) { if (has_zero) return 1; has_zero = true; continue; }
        uint32_t i = (x * 2654435769u) >> shift;
        while (buf[i] != 0) {
            if (buf[i] == x) return 1;
            i = (i + 1) & mask;
        }
        if (count >= mask) return -1;
        buf[i] = x;
        count++;
    }
    return 0;
}

/* ---- 입력 ---- */
static uint32_t fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
static void fill_distinct(uint32_t *a, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = fmix32((uint32_t)i * 0x9E3779B1u + 12345u);
}

/* ---- 시간 ---- */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static volatile long g_sink;

enum { PAIR, SORT, HASH, NMETH };
static const char *const mname[NMETH] = { "pairwise", "sort", "hash" };

static uint32_t *g_a, *g_scratch, *g_table;

static void run_once(int m, size_t n) {
    long r = 0;
    if (m == PAIR) r = has_dup_pairwise(g_a, n);
    if (m == SORT) r = has_dup_sort(g_a, n, g_scratch);
    if (m == HASH) r = has_dup_hash(g_a, n, g_table, log2_cap_for(n));
    g_sink += r + 1;
}
static int cmp_double(const void *pa, const void *pb) {
    double x = *(const double *)pa, y = *(const double *)pb;
    return (x > y) - (x < y);
}
/* 한 번 워밍업 후, 20 ms 이상 될 때까지 반복한 평균을 5번 → 중앙값 (ns / 호출) */
static double time_call(int m, size_t n) {
    run_once(m, n);
    double s[5];
    for (int k = 0; k < 5; k++) {
        uint64_t t0 = now_ns(), t1;
        long reps = 0;
        do { run_once(m, n); reps++; t1 = now_ns(); } while (t1 - t0 < 20000000ull);
        s[k] = (double)(t1 - t0) / (double)reps;
    }
    qsort(s, 5, sizeof s[0], cmp_double);
    return s[2];
}
/* 최소제곱: y = c + p*x 의 p */
static double fit_slope(const double *x, const double *y, int m) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < m; i++) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    return (m * sxy - sx * sy) / (m * sxx - sx * sx);
}

/* ---- 메모리: fork 한 자식에서 한 방법만 돌리고 wait4 로 그 자식의 ru_maxrss ---- */
static double maxrss_mib(long v) {
#ifdef __APPLE__
    return (double)v / (1024.0 * 1024.0);          /* macOS: 바이트 */
#else
    return (double)v / 1024.0;                       /* Linux: 킬로바이트 */
#endif
}
static long child_maxrss(int m, size_t n) {          /* m = -1 → 입력만 */
    pid_t pid = fork();
    if (pid == 0) {
        uint32_t *a = malloc(n * sizeof *a);
        if (!a) _exit(2);
        fill_distinct(a, n);
        long r = 0;
        if (m == SORT) { uint32_t *s = malloc(n * sizeof *s); if (!s) _exit(2); r = has_dup_sort(a, n, s); }
        if (m == HASH) { unsigned k = log2_cap_for(n); uint32_t *t = malloc(((size_t)1 << k) * sizeof *t);
                         if (!t) _exit(2); r = has_dup_hash(a, n, t, k); }
        if (m == PAIR) r = has_dup_pairwise(a, n < 20000 ? n : 20000);   /* 시간 절약: 메모리는 0 */
        _exit(r == 0 ? 0 : 3);
    }
    int st = 0;
    struct rusage ru;
    if (pid < 0 || wait4(pid, &st, 0, &ru) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1;
    return ru.ru_maxrss;
}

#define NN 13
static const size_t NS[NN] = { 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000,
                               500000, 1000000, 2000000, 5000000, 10000000 };
#define PAIR_CAP 100000      /* 이보다 큰 n 은 쌍 비교를 돌리지 않는다 (1회 수 초) */

int main(void) {
    /* 0. 기계 정보와 타이머 해상도 */
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   /* P 코어 선호 (보장은 아님) */
    char brand[128] = "?"; size_t bl = sizeof brand; uint64_t l2 = 0, l1 = 0; size_t sl = sizeof l2;
    sysctlbyname("machdep.cpu.brand_string", brand, &bl, NULL, 0);
    sysctlbyname("hw.perflevel0.l2cachesize", &l2, &sl, NULL, 0); sl = sizeof l1;
    sysctlbyname("hw.perflevel0.l1dcachesize", &l1, &sl, NULL, 0);
    printf("machine: %s, P-core L1d %llu KiB, L2 %llu MiB\n", brand,
           (unsigned long long)(l1 >> 10), (unsigned long long)(l2 >> 20));
#endif
    struct timespec res;
    clock_getres(CLOCK_MONOTONIC, &res);
    printf("clock_getres: CLOCK_MONOTONIC %ld ns,", res.tv_nsec);
    clock_getres(CLOCK_MONOTONIC_RAW, &res);
    printf(" CLOCK_MONOTONIC_RAW %ld ns\n", res.tv_nsec);

    /* 1. 메모리 (큰 버퍼를 잡기 전에 fork) */
    printf("\n[memory] work bytes the code allocates, and child peak RSS (ru_maxrss)\n");
    printf("%9s | %12s %12s %12s | %10s %10s %10s %10s\n", "n", "pair B", "sort B", "hash B",
           "rss input", "+pair", "+sort", "+hash");
    static const size_t MN[] = { 1000, 100000, 1000000, 10000000 };
    for (int i = 0; i < 4; i++) {
        size_t n = MN[i];
        double base = maxrss_mib(child_maxrss(-1, n));
        printf("%9zu | %12d %12zu %12llu | %8.1f M %+8.1f M %+8.1f M %+8.1f M\n", n, 0, 4 * n,
               (unsigned long long)4 << log2_cap_for(n), base,
               maxrss_mib(child_maxrss(PAIR, n)) - base, maxrss_mib(child_maxrss(SORT, n)) - base,
               maxrss_mib(child_maxrss(HASH, n)) - base);
    }

    size_t nmax = NS[NN - 1], amax = (size_t)3 << 22;   /* 스윕의 최대 n = 3/8 * 2^25 */
    g_a = malloc(amax * sizeof *g_a);
    g_scratch = malloc(nmax * sizeof *g_scratch);
    g_table = malloc(((size_t)1 << log2_cap_for(nmax)) * sizeof *g_table);
    if (!g_a || !g_scratch || !g_table) { puts("OOM"); return 1; }
    fill_distinct(g_a, amax);

    /* 2. 작은 n 에서 한 번만 재면: 타이머 해상도와 잡음 */
    printf("\n[single shot, n=1000, 8 runs each, ns]\n");
    for (int m = 0; m < NMETH; m++) {
        printf("%-8s:", mname[m]);
        run_once(m, 1000);
        for (int k = 0; k < 8; k++) { uint64_t t0 = now_ns(); run_once(m, 1000); printf(" %6llu", (unsigned long long)(now_ns() - t0)); }
        printf("\n");
    }

    /* 3. 시간 표 */
    double t[NMETH][NN];
    printf("\n[time] median ns per call (no duplicates = worst case)\n");
    printf("%9s | %14s %14s %14s | %9s %9s %9s\n", "n", "pairwise", "sort(qsort)", "hash",
           "pair/n^2", "sort/n", "hash/n");
    for (int i = 0; i < NN; i++) {
        size_t n = NS[i];
        for (int m = 0; m < NMETH; m++)
            t[m][i] = (m == PAIR && n > PAIR_CAP) ? NAN : time_call(m, n);
        printf("%9zu | %14.0f %14.0f %14.0f | %9.3f %9.2f %9.2f\n", n, t[PAIR][i], t[SORT][i], t[HASH][i],
               t[PAIR][i] / ((double)n * (double)n), t[SORT][i] / (double)n, t[HASH][i] / (double)n);
        fflush(stdout);
    }

    /* 4. 기울기: log10(시간) vs log10(n) 최소제곱 */
    printf("\n[slope] least squares of log10(t) on log10(n)\n");
    for (int m = 0; m < NMETH; m++) {
        double x[NN], y[NN], x4[NN], y4[NN]; int c = 0, c4 = 0;
        for (int i = 0; i < NN; i++) {
            if (isnan(t[m][i])) continue;
            x[c] = log10((double)NS[i]); y[c] = log10(t[m][i]); c++;
            if (NS[i] >= 10000) { x4[c4] = x[c - 1]; y4[c4] = y[c - 1]; c4++; }
        }
        printf("%-8s: all n (%2d pts) %.3f   n>=1e4 (%2d pts) %.3f\n", mname[m], c, fit_slope(x, y, c), c4,
               fit_slope(x4, y4, c4));
    }
    printf("local slopes (between neighbouring n):\n");
    for (int i = 1; i < NN; i++) {
        printf("%8zu->%-8zu", NS[i - 1], NS[i]);
        for (int m = 0; m < NMETH; m++) {
            double s = log(t[m][i] / t[m][i - 1]) / log((double)NS[i] / (double)NS[i - 1]);
            if (isnan(s)) printf("  %8s=   -  ", mname[m]); else printf("  %8s=%5.2f", mname[m], s);
        }
        printf("\n");
    }

    /* 5. 쌍 비교를 10^7 로 외삽 */
    {
        double x[NN], y[NN]; int c = 0, last = 0;
        for (int i = 0; i < NN; i++) if (!isnan(t[PAIR][i])) { x[c] = log10((double)NS[i]); y[c] = log10(t[PAIR][i]); c++; last = i; }
        double p = fit_slope(x, y, c);
        double t7 = t[PAIR][last] * pow(1e7 / (double)NS[last], p);
        printf("\n[extrapolate] pairwise slope %.3f, measured %.3f s at n=%zu -> n=1e7 ~ %.0f s (%.1f h)\n",
               p, t[PAIR][last] / 1e9, NS[last], t7 / 1e9, t7 / 3.6e12);
        printf("              (exact n^2 scaling from the last point: %.0f s)\n",
               t[PAIR][last] / 1e9 * (1e7 / (double)NS[last]) * (1e7 / (double)NS[last]));
    }

    /* 6. 해시 캐시 스윕: 적재율 0.375 고정, 테이블 크기만 바꾼다 */
    printf("\n[hash cache sweep] load factor fixed at 0.375 (n = 3/8 cap)\n");
    printf("%10s %10s %12s %10s\n", "table", "n", "ns/call", "ns/elem");
    for (unsigned k = 11; k <= 25; k++) {
        size_t n = (size_t)3 << (k - 3);
        double tt;
        tt = time_call(HASH, n);            /* 2n = 3/4 cap → log2_cap_for(n) == k */
        size_t tb = (size_t)4 << k;
        if (tb >= ((size_t)1 << 20)) printf("%7zu MiB", tb >> 20); else printf("%7zu KiB", tb >> 10);
        printf(" %10zu %12.0f %10.2f\n", n, tt, tt / (double)n);
        fflush(stdout);
    }

    /* 7. 그래프용 CSV */
    printf("\nCSV,n,pairwise_ns,sort_ns,hash_ns\n");
    for (int i = 0; i < NN; i++) printf("CSV,%zu,%.0f,%.0f,%.0f\n", NS[i], t[PAIR][i], t[SORT][i], t[HASH][i]);
    printf("sink=%ld\n", (long)g_sink);
    free(g_a); free(g_scratch); free(g_table);
    return 0;
}
