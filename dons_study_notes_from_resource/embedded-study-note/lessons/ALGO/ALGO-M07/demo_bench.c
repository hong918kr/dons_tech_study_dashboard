/* ALGO-M07 demo_bench: 측정 세 가지 (이 맥, Apple Silicon, -O2). 절대 숫자보다 비율을 볼 것.
 *   1) LIS O(n²) vs O(n log n) 시간 (warm-up 1회 + 5회 중앙값)
 *   2) 무작위 순열의 LIS 길이 vs 2√n (Vershik–Kerov / Logan–Shepp 점근식)
 *   3) 동전 최소 개수: 하향식 memo vs 상향식 tab vs 링 — 같은 답, 다른 비용
 *   4) 경우의 수가 uint64 를 처음 넘는 지점: 순열(지수 증가) vs 조합(다항 증가)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NMAX 1000000
static int32_t A[NMAX];
static size_t len_[NMAX], tail_[NMAX];
static volatile uint64_t sink;

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static size_t lis_n2(const int32_t *a, size_t n) {
    size_t best = 0;
    for (size_t i = 0; i < n; i++) {
        len_[i] = 1;
        for (size_t j = 0; j < i; j++) if (a[j] < a[i] && len_[j] + 1 > len_[i]) len_[i] = len_[j] + 1;
        if (len_[i] > best) best = len_[i];
    }
    return best;
}
static size_t lis_nlogn(const int32_t *a, size_t n) {
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        size_t lo = 0, hi = len;
        while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (a[tail_[mid]] < a[i]) lo = mid + 1; else hi = mid; }
        tail_[lo] = i;
        if (lo == len) len++;
    }
    return len;
}

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
typedef size_t (*lis_fn)(const int32_t *, size_t);
static double median_time(lis_fn f, size_t n) {
    double t[5];
    sink += f(A, n);                                  /* warm-up */
    for (int r = 0; r < 5; r++) { double t0 = now_s(); sink += f(A, n); t[r] = now_s() - t0; }
    qsort(t, 5, sizeof t[0], cmpd);
    return t[2];
}
static void random_perm(size_t n) {
    for (size_t i = 0; i < n; i++) A[i] = (int32_t)i;
    for (size_t i = n; i > 1; i--) { size_t j = xorshift32() % i; int32_t t = A[i - 1]; A[i - 1] = A[j]; A[j] = t; }
}

/* ---- 동전 최소 개수 세 가지 (solution.c 와 같은 코드, 금액 상한만 크게) ---- */
#define AMT 2000
static int32_t memo_[AMT + 1];
static const uint32_t *C_; static size_t K_;
static int32_t g_(uint32_t a) {
    if (a == 0) return 0;
    if (memo_[a] != -2) return memo_[a];
    int32_t best = -1;
    for (size_t j = 0; j < K_; j++) {
        if (C_[j] > a) continue;
        int32_t s = g_(a - C_[j]);
        if (s >= 0 && (best < 0 || s + 1 < best)) best = s + 1;
    }
    return memo_[a] = best;
}
static int32_t coin_memo(const uint32_t *c, size_t k, uint32_t amt) { C_ = c; K_ = k; for (uint32_t a = 0; a <= amt; a++) memo_[a] = -2; return g_(amt); }
static uint16_t dp_[AMT + 1];
static int32_t coin_tab(const uint32_t *c, size_t k, uint32_t amt) {
    dp_[0] = 0;
    for (uint32_t a = 1; a <= amt; a++) {
        uint16_t b = UINT16_MAX;
        for (size_t j = 0; j < k; j++) if (c[j] <= a && dp_[a - c[j]] != UINT16_MAX && dp_[a - c[j]] + 1 < b) b = (uint16_t)(dp_[a - c[j]] + 1);
        dp_[a] = b;
    }
    return dp_[amt] == UINT16_MAX ? -1 : dp_[amt];
}
static int32_t coin_ring(const uint32_t *c, size_t k, uint32_t amt) {
    uint32_t r[64]; r[0] = 0;
    for (uint32_t a = 1; a <= amt; a++) {
        uint32_t b = UINT32_MAX;
        for (size_t j = 0; j < k; j++) if (c[j] <= a) { uint32_t s = r[(a - c[j]) & 63]; if (s != UINT32_MAX && s + 1 < b) b = s + 1; }
        r[a & 63] = b;
    }
    return r[amt & 63] == UINT32_MAX ? -1 : (int32_t)r[amt & 63];
}
typedef int32_t (*coin_fn)(const uint32_t *, size_t, uint32_t);
static double coin_time(coin_fn f, const uint32_t *c, size_t k, uint32_t amt, int reps) {
    double t[5];
    sink += (uint64_t)f(c, k, amt);
    for (int r = 0; r < 5; r++) {
        double t0 = now_s();
        for (int q = 0; q < reps; q++) sink += (uint64_t)f(c, k, amt);
        t[r] = (now_s() - t0) / reps;
    }
    qsort(t, 5, sizeof t[0], cmpd);
    return t[2];
}

/* ---- 경우의 수 성장: 128비트로 정확히 세며 2^64 를 처음 넘는 금액 ---- */
typedef unsigned __int128 u128;
#define GROW_MAX 600000
static u128 W[GROW_MAX + 1];
static long first_overflow_combo(const uint32_t *c, size_t k) {
    memset(W, 0, sizeof W); W[0] = 1;
    for (size_t j = 0; j < k; j++) for (uint32_t a = c[j]; a <= GROW_MAX; a++) W[a] += W[a - c[j]];
    for (long a = 0; a <= GROW_MAX; a++) if (W[a] > (u128)UINT64_MAX) return a;
    return -1;
}
static long first_overflow_perm(const uint32_t *c, size_t k) {
    memset(W, 0, sizeof W); W[0] = 1;
    for (uint32_t a = 1; a <= GROW_MAX; a++) {
        u128 w = 0;
        for (size_t j = 0; j < k; j++) if (c[j] <= a) w += W[a - c[j]];
        W[a] = w;
        if (w > (u128)UINT64_MAX) return a;
    }
    return -1;
}

int main(void) {
    printf("1) LIS time, random permutation (median of 5, ms)\n");
    printf("%8s | %10s | %10s | %8s | %s\n", "n", "O(n^2)", "O(nlogn)", "ratio", "LIS");
    static const size_t ns[] = { 1000, 2000, 4000, 8000, 16000 };
    for (size_t q = 0; q < sizeof ns / sizeof ns[0]; q++) {
        size_t n = ns[q];
        random_perm(n);
        size_t l1 = lis_n2(A, n), l2 = lis_nlogn(A, n);
        double t1 = median_time(lis_n2, n), t2 = median_time(lis_nlogn, n);
        printf("%8zu | %10.3f | %10.4f | %7.0fx | %zu%s\n", n, t1 * 1e3, t2 * 1e3, t1 / t2, l2, l1 == l2 ? "" : " MISMATCH");
    }

    printf("\n2) LIS length of a random permutation vs 2*sqrt(n) (O(n log n) only, 5 seeds each)\n");
    printf("%8s | %8s | %8s | %6s | %s\n", "n", "mean LIS", "2sqrt(n)", "ratio", "time/run ms");
    static const size_t ms[] = { 1000, 10000, 100000, 1000000 };
    for (size_t q = 0; q < sizeof ms / sizeof ms[0]; q++) {
        size_t n = ms[q];
        double sum = 0, tt = 0;
        for (int s = 0; s < 5; s++) { random_perm(n); double t0 = now_s(); sum += (double)lis_nlogn(A, n); tt += now_s() - t0; }
        double mean = sum / 5, pred = 2 * sqrt((double)n);
        printf("%8zu | %8.1f | %8.1f | %6.3f | %.2f\n", n, mean, pred, mean / pred, tt / 5 * 1e3);
    }

    printf("\n3) min coins, amount=%d, coins {1,7,13,29,31,47} (median of 5, us per call)\n", AMT);
    const uint32_t cs[] = { 1, 7, 13, 29, 31, 47 };
    int32_t r1 = coin_memo(cs, 6, AMT), r2 = coin_tab(cs, 6, AMT), r3 = coin_ring(cs, 6, AMT);
    double tm = coin_time(coin_memo, cs, 6, AMT, 200), tt = coin_time(coin_tab, cs, 6, AMT, 200), tr = coin_time(coin_ring, cs, 6, AMT, 200);
    printf("  memo  : answer=%d  %.2f us  (table %zu B + recursion)\n", r1, tm * 1e6, sizeof memo_);
    printf("  tab   : answer=%d  %.2f us  (table %zu B)\n", r2, tt * 1e6, sizeof dp_);
    printf("  ring  : answer=%d  %.2f us  (ring 256 B)\n", r3, tr * 1e6);
    printf("  memo/tab = %.2fx\n", tm / tt);

    printf("\n4) first amount whose number of ways exceeds UINT64_MAX (exact 128-bit count)\n");
    const uint32_t s12[] = { 1, 2 }, s123[] = { 1, 2, 3 }, us[] = { 1, 5, 10, 25, 50, 100 };
    printf("  perm  {1,2}                 : %ld\n", first_overflow_perm(s12, 2));
    printf("  perm  {1,2,3}               : %ld\n", first_overflow_perm(s123, 3));
    printf("  perm  {1,5,10,25,50,100}    : %ld\n", first_overflow_perm(us, 6));
    printf("  combo {1,5,10,25,50,100}    : %ld\n", first_overflow_combo(us, 6));
    printf("  combo {1,2,3}               : %ld (closed form (n+3)^2/12 -> about 1.49e10)\n", first_overflow_combo(s123, 3));
    printf("(sink %llu)\n", (unsigned long long)(sink & 0xFF));
    return 0;
}
