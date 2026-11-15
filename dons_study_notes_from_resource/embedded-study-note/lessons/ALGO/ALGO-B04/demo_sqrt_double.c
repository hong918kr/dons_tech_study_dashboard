/* ALGO-B04 demo: (uint)sqrt((double)x) / sqrtf((float)x) 는 언제 틀리나?
 *   1) 32비트 x 전 범위(2^32개): double 경로와 float 경로를 정답(증분 브루트포스)과 비교
 *   2) 64비트: k^2 - 1 이 처음 틀리는 k, 무작위 x 의 오차 방향(크게/작게), 대표 예
 *   3) 보정판: s = (uint64_t)sqrt((double)x) 뒤에 s*s > x 면 내리고 (s+1)^2 <= x 면 올림 → 오차 0
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sqrt_double.c -lm -o demo_sqrt && ./demo_sqrt
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint64_t isqrt64(uint64_t x) {                 /* solution.c 와 같은 이분 탐색 */
    uint64_t lo = 0, hi = x < 0xFFFFFFFFu ? x : 0xFFFFFFFFu;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid <= x) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static uint64_t sqrt_double(uint64_t x) {
    double r = sqrt((double)x);
    return r >= 18446744073709551616.0 ? UINT64_MAX : (uint64_t)r;   /* 2^64 이상을 그대로 변환하면 UB (C11 6.3.1.4) */
}

/* double 로 추정 → 정수로 보정. s <= 2^32 이므로 보정 비교는 넘침 없이 */
static uint64_t sqrt_double_fixed(uint64_t x) {
    uint64_t s = sqrt_double(x);
    if (s > 0xFFFFFFFFu) s = 0xFFFFFFFFu;
    while (s * s > x) s--;
    while (s < 0xFFFFFFFFu && (s + 1) * (s + 1) <= x) s++;
    return s;
}

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint64_t xs64(void) { uint64_t h = xs32(); return (h << 32) | xs32(); }

int main(void) {
    clock_t t0 = clock();
    /* 1) 32비트 전 범위 */
    uint64_t bad_d = 0, bad_f = 0, f_big = 0, f_small = 0;
    uint32_t first_f = 0;
    uint32_t s = 0;
    for (uint64_t x = 0; x <= UINT32_MAX; x++) {
        while ((uint64_t)(s + 1) * (s + 1) <= x) s++;
        uint32_t d = (uint32_t)sqrt((double)x);
        float rf = sqrtf((float)x);
        uint32_t f = rf >= 65536.0f ? 65536u : (uint32_t)rf;
        if (d != s) bad_d++;
        if (f != s) { if (!bad_f) first_f = (uint32_t)x; bad_f++; if (f > s) f_big++; else f_small++; }
    }
    printf("uint32 all 2^32 x: (uint32_t)sqrt((double)x) wrong = %llu\n", (unsigned long long)bad_d);
    printf("uint32 all 2^32 x: (uint32_t)sqrtf((float)x)  wrong = %llu (too big %llu, too small %llu), first x = %u\n",
           (unsigned long long)bad_f, (unsigned long long)f_big, (unsigned long long)f_small, first_f);
    printf("  e.g. x = %u: sqrtf -> %u, correct %u\n", first_f,
           (uint32_t)sqrtf((float)first_f), (uint32_t)isqrt64(first_f));
    printf("  (32-bit pass took %.1f s)\n", (double)(clock() - t0) / CLOCKS_PER_SEC);

    /* 2a) k^2 - 1 이 처음 틀리는 k */
    uint64_t kfail = 0;
    for (uint64_t k = 1; k <= 0xFFFFFFFFu; k++) {
        if (sqrt_double(k * k - 1) != k - 1) { kfail = k; break; }
    }
    printf("smallest k with sqrt_double(k^2-1) != k-1: k = %llu (k^2-1 = %llu, 2^53 = %llu)\n",
           (unsigned long long)kfail, (unsigned long long)(kfail * kfail - 1), 1ull << 53);
    /* 2b) 대표 예 */
    uint64_t ex[] = {(1ull << 54) - 1, 0xFFFFFFFE00000000ull, UINT64_MAX};
    for (unsigned i = 0; i < sizeof ex / sizeof ex[0]; i++)
        printf("  x = %llu: (uint64_t)sqrt((double)x) = %llu, correct = %llu\n",
               (unsigned long long)ex[i], (unsigned long long)sqrt_double(ex[i]), (unsigned long long)isqrt64(ex[i]));
    /* 2c) 무작위 64비트 */
    uint64_t n = 2000000, rb = 0, rbig = 0, rsmall = 0, near_bad = 0, fixed_bad = 0;
    for (uint64_t t = 0; t < n; t++) {
        uint64_t x = xs64();
        uint64_t e = isqrt64(x), d = sqrt_double(x);
        if (d != e) { rb++; if (d > e) rbig++; else rsmall++; }
        if (sqrt_double_fixed(x) != e) fixed_bad++;
        uint64_t k = 1 + xs32() % 0xFFFFFFFFu;          /* k^2 - 1: 정수 제곱근 경계 바로 아래 */
        if (sqrt_double(k * k - 1) != k - 1) near_bad++;
        if (sqrt_double_fixed(k * k - 1) != k - 1) fixed_bad++;
    }
    printf("random uniform 64-bit x (%llu): double wrong = %llu (too big %llu, too small %llu)\n",
           (unsigned long long)n, (unsigned long long)rb, (unsigned long long)rbig, (unsigned long long)rsmall);
    printf("random k in [1, 2^32): sqrt_double(k^2-1) wrong = %llu of %llu (%.1f%%)\n",
           (unsigned long long)near_bad, (unsigned long long)n, 100.0 * (double)near_bad / (double)n);
    printf("double + integer fix-up: wrong = %llu of %llu\n", (unsigned long long)fixed_bad, (unsigned long long)(2 * n));
    printf("total %.1f s\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
