/* ALGO-M15 demo_bench — 이 맥(Apple Silicon)에서 ns/op 와 GCD 단계 수.
 * 입력 1M 개(xorshift32 고정 시드) 배열을 한 번 훑는 시간을 7번 재서 중앙값. 워밍업 1번.
 * 결과는 volatile sink 로 모아 컴파일러가 지우지 못하게. 처리량(throughput) 측정이다.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N (1u << 20)
#define REPS 7
static uint32_t A[N], B[N];
static volatile uint32_t sink;
static volatile uint32_t vten = 10;

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }

static uint32_t div10(uint32_t x) { return (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35); }
static uint32_t divu10(uint32_t n) {
    uint32_t q = (n >> 1) + (n >> 2);
    q += q >> 4; q += q >> 8; q += q >> 16; q >>= 3;
    uint32_t r = n - (((q << 2) + q) << 1);
    return q + (r > 9);
}
static uint32_t isqrt32(uint32_t x) {
    uint32_t rem = x, res = 0, one = 1u << 30;
    while (one > rem) one >>= 2;
    while (one) {
        if (rem >= res + one) { rem -= res + one; res = (res >> 1) + one; } else res >>= 1;
        one >>= 2;
    }
    return res;
}
static uint32_t isqrt_newton(uint32_t x) {
    if (x < 2) return x;
    unsigned bits = 0;
    for (uint32_t t = x; t; t >>= 1) bits++;
    uint32_t r = 1u << ((bits + 1) / 2);
    for (;;) { uint32_t y = (r + x / r) >> 1; if (y >= r) return r; r = y; }
}
static uint32_t isqrt_bsearch(uint32_t x) {          /* ALGO-B02: 마지막으로 r² <= x 인 r */
    uint32_t lo = 0, hi = 65536;                     /* [lo, hi): lo² <= x, hi² > x */
    while (hi - lo > 1) { uint32_t mid = lo + (hi - lo) / 2; if ((uint64_t)mid * mid <= x) lo = mid; else hi = mid; }
    return lo;
}
static uint32_t isqrt_dbl(uint32_t x) { return (uint32_t)sqrt((double)x); }

static unsigned g_steps;
static uint32_t gcd_euclid(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = a % b; a = b; b = t; g_steps++; }
    return a;
}
static uint32_t gcd_binary(uint32_t a, uint32_t b) {
    if (a == 0) return b;
    if (b == 0) return a;
    unsigned k = 0;
    while (((a | b) & 1u) == 0) { a >>= 1; b >>= 1; k++; }
    while ((a & 1u) == 0) a >>= 1;
    do {
        while ((b & 1u) == 0) b >>= 1;
        if (a > b) { uint32_t t = a; a = b; b = t; }
        b -= a; g_steps++;
    } while (b);
    return a << k;
}
static uint32_t gcd_binary_ctz(uint32_t a, uint32_t b) {   /* 비교용: __builtin_ctz (arm64: rbit + clz) */
    if (a == 0) return b;
    if (b == 0) return a;
    unsigned k = (unsigned)__builtin_ctz(a | b);
    a >>= __builtin_ctz(a);
    do {
        b >>= __builtin_ctz(b);
        if (a > b) { uint32_t t = a; a = b; b = t; }
        b -= a;
    } while (b);
    return a << k;
}
static uint32_t powmod32(uint32_t base, uint32_t e, uint32_t m) {
    uint64_t r = 1u % m, b = base % m;
    while (e) { if (e & 1u) r = r * b % m; b = b * b % m; e >>= 1; }
    return (uint32_t)r;
}

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

#define BENCH1(label, expr) do { double t[REPS]; uint32_t acc = 0; \
    for (uint32_t i = 0; i < N; i++) { uint32_t x = A[i]; uint32_t y = B[i]; (void)y; acc += (expr); } \
    for (int r = 0; r < REPS; r++) { double t0 = now(); \
        for (uint32_t i = 0; i < N; i++) { uint32_t x = A[i]; uint32_t y = B[i]; (void)y; acc += (expr); } \
        t[r] = now() - t0; } \
    qsort(t, REPS, sizeof t[0], cmpd); sink = acc; \
    printf("%-34s %8.2f ns/op\n", label, t[REPS / 2] * 1e9 / N); } while (0)

int main(void) {
    uint32_t s = 0xC0FFEEu;
    for (uint32_t i = 0; i < N; i++) { A[i] = xs32(&s); B[i] = xs32(&s); }
    uint32_t d = vten;                       /* 컴파일러가 값을 모름 → 진짜 udiv */
    uint32_t pm = 4294967291u; pm += vten - 10;

    printf("--- divide by 10 (random uint32) ---\n");
    BENCH1("x / 10   (compiler: umull + lsr)", x / 10);
    BENCH1("x / d    (d unknown: udiv)", x / d);
    BENCH1("div10    (0xCCCCCCCD >> 35)", div10(x));
    BENCH1("divu10   (shift-add, no mul)", divu10(x));

    printf("--- isqrt (random uint32) ---\n");
    BENCH1("isqrt32 bitwise (16 steps)", isqrt32(x));
    BENCH1("isqrt newton (x / r)", isqrt_newton(x));
    BENCH1("isqrt binary search (B02)", isqrt_bsearch(x));
    BENCH1("(uint32_t)sqrt((double)x)", isqrt_dbl(x));

    printf("--- gcd (random uint32 pairs) ---\n");
    BENCH1("gcd_euclid (a % b)", gcd_euclid(x, y));
    BENCH1("gcd_binary (Stein, bit loop)", gcd_binary(x, y));
    BENCH1("gcd_binary (Stein, __builtin_ctz)", gcd_binary_ctz(x, y));

    printf("--- powmod32 (random base/exp, m = 2^32-5 not const) ---\n");
    BENCH1("powmod32", powmod32(x, y, pm));

    /* GCD 단계 수: Euclid = 나머지 연산 횟수, Stein = 뺄셈 횟수 */
    unsigned emax = 0, bmax = 0; unsigned long long esum = 0, bsum = 0;
    for (uint32_t i = 0; i < N; i++) {
        g_steps = 0; gcd_euclid(A[i], B[i]); esum += g_steps; if (g_steps > emax) emax = g_steps;
        g_steps = 0; gcd_binary(A[i], B[i]); bsum += g_steps; if (g_steps > bmax) bmax = g_steps;
    }
    printf("--- gcd steps over %u random pairs ---\n", N);
    printf("euclid: mean %.2f  max %u   |  stein: mean %.2f  max %u (subtractions)\n",
           (double)esum / N, emax, (double)bsum / N, bmax);
    g_steps = 0; gcd_euclid(2971215073u, 1836311903u); unsigned ef = g_steps;
    g_steps = 0; gcd_binary(2971215073u, 1836311903u); unsigned bf = g_steps;
    printf("F47, F46 = (2971215073, 1836311903): euclid %u mod-steps, stein %u subtractions\n", ef, bf);
    g_steps = 0; gcd_euclid(UINT32_MAX, 1); unsigned e1 = g_steps;
    g_steps = 0; gcd_binary(UINT32_MAX, 1); unsigned b1 = g_steps;
    printf("(2^32-1, 1): euclid %u mod-steps, stein %u subtractions\n", e1, b1);
    return 0;
}
