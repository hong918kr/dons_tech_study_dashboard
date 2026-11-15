/* ALGO-M15 demo_exhaustive — 모든 uint32 x (2^32 개)에 대해 검사. 8 스레드로 나눠 돈다.
 *
 * 1. div10 (0xCCCCCCCD >> 35) / mod10 vs  /  %
 * 2. 시프트를 하나 줄이면? (ceil(2^34/10) = 0x66666667, >> 34)  /  내림 곱수 0xCCCCCCCC >> 35
 * 3. divu10 (곱셈 없는 시프트·덧셈)
 * 4. magic_u32 결과 적용: d = 3, 7 (add 보정), 1000, 641
 * 5. isqrt32 vs 정의 r² <= x < (r+1)²,  (uint32_t)sqrt((double)x),  (uint32_t)sqrtf((float)x)
 * 6. 뉴턴 isqrt 의 최대 나눗셈 횟수
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive.c -o demo_exhaustive -lm && ./demo_exhaustive
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define NT 8

static uint32_t div10(uint32_t x) { return (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35); }
static uint32_t mod10(uint32_t x) { uint32_t q = div10(x); return x - ((q << 3) + (q << 1)); }
static uint32_t div10_p34(uint32_t x) { return (uint32_t)(((uint64_t)x * 0x66666667u) >> 34); }
static uint32_t div10_floor(uint32_t x) { return (uint32_t)(((uint64_t)x * 0xCCCCCCCCu) >> 35); }
static uint32_t divu10(uint32_t n) {
    uint32_t q = (n >> 1) + (n >> 2);
    q += q >> 4; q += q >> 8; q += q >> 16; q >>= 3;
    uint32_t r = n - (((q << 2) + q) << 1);
    return q + (r > 9);
}

typedef struct { uint32_t mul; uint8_t shift; uint8_t mode; } udiv_magic;
static udiv_magic magic_u32(uint32_t d) {
    udiv_magic r = { 0, 0, 0 };
    if ((d & (d - 1)) == 0) { while ((1u << r.shift) != d) r.shift++; return r; }
    for (unsigned p = 32; p <= 64; p++) {
        uint64_t a = (p == 64) ? UINT64_MAX : (((uint64_t)1 << p) - 1);
        uint64_t m = a / d + 1, e = d - 1 - a % d;
        if (e <= ((uint64_t)1 << (p - 32))) {
            if (m < ((uint64_t)1 << 32)) { r.mul = (uint32_t)m; r.shift = (uint8_t)p; r.mode = 1; }
            else { r.mul = (uint32_t)(m - ((uint64_t)1 << 32)); r.shift = (uint8_t)(p - 33); r.mode = 2; }
            return r;
        }
    }
    return r;
}
static uint32_t magic_apply(udiv_magic g, uint32_t x) {
    if (g.mode == 0) return x >> g.shift;
    if (g.mode == 1) return (uint32_t)(((uint64_t)x * g.mul) >> g.shift);
    uint32_t t = (uint32_t)(((uint64_t)x * g.mul) >> 32);
    return (((x - t) >> 1) + t) >> g.shift;
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
static unsigned newton_iters(uint32_t x, uint32_t *root) {
    if (x < 2) { *root = x; return 0; }
    unsigned bits = 0, it = 0;
    for (uint32_t t = x; t; t >>= 1) bits++;
    uint32_t r = 1u << ((bits + 1) / 2);
    for (;;) { uint32_t y = (r + x / r) >> 1; it++; if (y >= r) { *root = r; return it; } r = y; }
}

enum { T_DIV10, T_P34, T_FLOOR, T_DIVU10, T_MAGIC, T_ISQRT, T_DBL, T_FLT, T_NEWTON, T_COUNT };
static const char *names[T_COUNT] = {
    "div10 0xCCCCCCCD>>35 + mod10", "div10 0x66666667>>34 (one shift less)", "div10 0xCCCCCCCC>>35 (floor mult)",
    "divu10 shift-add (no mul)", "magic_u32 d=3,7,1000,641", "isqrt32 bitwise vs r^2<=x<(r+1)^2",
    "(uint32_t)sqrt((double)x)", "(uint32_t)sqrtf((float)x)", "newton isqrt (== isqrt32, max divs)" };

typedef struct { int test; uint64_t lo, hi; uint64_t bad; uint64_t first; unsigned maxit; } job;
static udiv_magic g_m[4]; static const uint32_t g_d[4] = { 3, 7, 1000, 641 };

static void *run(void *arg) {
    job *j = arg;
    j->bad = 0; j->first = UINT64_MAX; j->maxit = 0;
    for (uint64_t xx = j->lo; xx < j->hi; xx++) {
        uint32_t x = (uint32_t)xx;
        int bad = 0;
        switch (j->test) {
        case T_DIV10:  bad = div10(x) != x / 10 || mod10(x) != x % 10; break;
        case T_P34:    bad = div10_p34(x) != x / 10; break;
        case T_FLOOR:  bad = div10_floor(x) != x / 10; break;
        case T_DIVU10: bad = divu10(x) != x / 10; break;
        case T_MAGIC:  for (int k = 0; k < 4; k++) bad |= magic_apply(g_m[k], x) != x / g_d[k]; break;
        case T_ISQRT: { uint64_t r = isqrt32(x); bad = !(r * r <= x && x < (r + 1) * (r + 1)); break; }
        case T_DBL:    bad = (uint32_t)sqrt((double)x) != isqrt32(x); break;
        case T_FLT:    bad = (uint32_t)sqrtf((float)x) != isqrt32(x); break;
        case T_NEWTON: { uint32_t r; unsigned it = newton_iters(x, &r); if (it > j->maxit) j->maxit = it;
                         bad = r != isqrt32(x); break; }
        }
        if (bad) { if (j->first == UINT64_MAX) j->first = xx; j->bad++; }
    }
    return NULL;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }

int main(void) {
    for (int k = 0; k < 4; k++) g_m[k] = magic_u32(g_d[k]);
    printf("all 2^32 inputs, %d threads, -O2\n", NT);
    printf("%-40s %12s %12s %8s\n", "test", "mismatches", "first bad x", "sec");
    int fails = 0;
    double t_all = now();
    for (int t = 0; t < T_COUNT; t++) {
        pthread_t th[NT]; job jb[NT];
        double t0 = now();
        for (int i = 0; i < NT; i++) {
            jb[i].test = t;
            jb[i].lo = ((uint64_t)1 << 32) / NT * (uint64_t)i;
            jb[i].hi = ((uint64_t)1 << 32) / NT * (uint64_t)(i + 1);
            pthread_create(&th[i], NULL, run, &jb[i]);
        }
        uint64_t bad = 0, first = UINT64_MAX; unsigned maxit = 0;
        for (int i = 0; i < NT; i++) {
            pthread_join(th[i], NULL);
            bad += jb[i].bad;
            if (jb[i].first < first) first = jb[i].first;
            if (jb[i].maxit > maxit) maxit = jb[i].maxit;
        }
        double dt = now() - t0;
        if (first == UINT64_MAX) printf("%-40s %12llu %12s %8.2f", names[t], (unsigned long long)bad, "-", dt);
        else printf("%-40s %12llu %12llu %8.2f", names[t], (unsigned long long)bad, (unsigned long long)first, dt);
        if (t == T_NEWTON) printf("   max divisions = %u", maxit);
        printf("\n");
        int expect_bad = (t == T_P34 || t == T_FLOOR || t == T_FLT);
        if (!expect_bad && bad) fails++;
    }
    printf("total %.1f s, %s\n", now() - t_all, fails ? "UNEXPECTED FAILURES" : "all exact methods exact");
    return fails ? 1 : 0;
}
