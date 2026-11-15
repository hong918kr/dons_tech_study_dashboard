/* ALGO-M15 추가 연습 — 한 번에 검증
 *
 * 연습 1: isqrt64 (uint64 → uint32, 비트 단위)
 * 연습 2: 뉴턴 정수 제곱근 (반복 횟수 측정, 비트 단위와 대조)
 * 연습 3: divu10 — 곱셈 없는 ÷10 (시프트·덧셈만, Cortex-M0 용)
 * 연습 4: 부호 있는 int32 ÷ 10 (0x66666667, >> 34, 0 쪽으로 버림 보정)
 * 연습 5: 모듈러 역원 — 확장 유클리드, 소수면 페르마(powmod)와 대조
 * 연습 6: 나머지 없이 "d 로 나누어떨어지나" — 2^32 위의 역원 곱 + 비교
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 본문과 같은 isqrt32 (자기 완결을 위해 복사) */
static uint32_t isqrt32(uint32_t x) {
    uint32_t rem = x, res = 0, one = 1u << 30;
    while (one > rem) one >>= 2;
    while (one) {
        if (rem >= res + one) { rem -= res + one; res = (res >> 1) + one; } else res >>= 1;
        one >>= 2;
    }
    return res;
}

/* ---- 연습 1: isqrt64 ---- */
static uint32_t isqrt64(uint64_t x) {
    uint64_t rem = x, res = 0, one = (uint64_t)1 << 62;
    while (one > rem) one >>= 2;
    while (one) {
        if (rem >= res + one) { rem -= res + one; res = (res >> 1) + one; } else res >>= 1;
        one >>= 2;
    }
    return (uint32_t)res;
}

/* ---- 연습 2: 뉴턴 (x/r 나눗셈 사용). *iters 에 나눗셈 횟수 ---- */
static uint32_t isqrt_newton(uint32_t x, unsigned *iters) {
    *iters = 0;
    if (x < 2) return x;
    unsigned bits = 0;
    for (uint32_t t = x; t; t >>= 1) bits++;
    uint32_t r = 1u << ((bits + 1) / 2);        /* 2^ceil(bits/2) > sqrt(x): 위에서 출발 */
    for (;;) {
        uint32_t y = (r + x / r) >> 1;
        (*iters)++;
        if (y >= r) return r;                   /* 더 안 줄면 r = floor(sqrt x) */
        r = y;
    }
}

/* ---- 연습 3: divu10 (Hacker's Delight 10-9 계열, 곱셈·나눗셈 없음) ---- */
static uint32_t divu10(uint32_t n) {
    uint32_t q = (n >> 1) + (n >> 2);           /* q ≈ 0.75 n  (0.8 n 의 근사 시작) */
    q += q >> 4;                                /* × (1 + 2^-4)   → 0.796875 n */
    q += q >> 8;                                /* × (1 + 2^-8) */
    q += q >> 16;                               /* × (1 + 2^-16)  → ≈ 0.8 n (1 − 2^-32) */
    q >>= 3;                                    /* ≈ n / 10, 작게 틀릴 수 있음 */
    uint32_t r = n - (((q << 2) + q) << 1);     /* r = n − 10q */
    return q + (r > 9);                         /* 한 번 보정 */
}

/* ---- 연습 4: 부호 있는 ÷10, C 의 / 처럼 0 쪽으로 버림 ---- */
static int32_t sdiv10(int32_t x) {
    int32_t q = (int32_t)(((int64_t)x * 0x66666667) >> 34);   /* floor(x · m / 2^34) — 음수 >> 는 구현 정의(clang: 산술) */
    return q + (int32_t)((uint32_t)x >> 31);                  /* 음수면 +1 → 0 쪽으로 */
}

/* ---- 연습 5: 모듈러 역원 (gcd(a, m) != 1 이면 0) ---- */
static uint32_t modinv32(uint32_t a, uint32_t m) {
    if (m <= 1) return 0;
    int64_t r0 = m, r1 = a % m, t0 = 0, t1 = 1;   /* 불변식: t_i · a ≡ r_i (mod m) */
    while (r1 != 0) {
        int64_t q = r0 / r1, tmp;
        tmp = r0 - q * r1; r0 = r1; r1 = tmp;
        tmp = t0 - q * t1; t0 = t1; t1 = tmp;
    }
    if (r0 != 1) return 0;
    if (t0 < 0) t0 += m;
    return (uint32_t)t0;
}
static uint32_t powmod32(uint32_t b, uint32_t e, uint32_t m) {
    uint64_t r = 1u % m, x = b % m;
    while (e) { if (e & 1u) r = r * x % m; x = x * x % m; e >>= 1; }
    return (uint32_t)r;
}

/* ---- 연습 6: 홀수 d 로 나누어떨어짐 — d^-1 mod 2^32 (뉴턴: 정확한 비트 수가 매번 두 배) ---- */
static uint32_t inv_mod2_32(uint32_t d) {      /* d 홀수 */
    uint32_t inv = d;                           /* d·d ≡ 1 (mod 8): 3비트 정확 */
    for (int i = 0; i < 4; i++) inv *= 2u - d * inv;   /* 3 → 6 → 12 → 24 → 48 비트 */
    return inv;
}
static bool divisible_odd(uint32_t x, uint32_t d_inv, uint32_t limit) {   /* limit = UINT32_MAX / d */
    return x * d_inv <= limit;
}

int main(void) {
    uint32_t seed = 0x9E3779B9u;

    /* 연습 1 */
    CHECK(isqrt64(0) == 0 && isqrt64(1) == 1 && isqrt64(UINT64_MAX) == 4294967295u);
    CHECK(isqrt64(18446744065119617025ull) == 4294967295u);      /* (2^32 − 1)² */
    CHECK(isqrt64(18446744065119617024ull) == 4294967294u);      /* (2^32 − 1)² − 1 */
    {
        uint32_t bad = 0;
        for (int i = 0; i < 1000000; i++) {
            uint64_t x = ((uint64_t)xs32(&seed) << 32 | xs32(&seed)) >> (xs32(&seed) % 64);
            if (i % 3 == 0) { uint64_t k = xs32(&seed); x = k * k - (i % 2); }   /* 완전제곱 근처 */
            unsigned __int128 r = isqrt64(x);
            bad += !(r * r <= x && x < (r + 1) * (r + 1));
        }
        printf("ex1 isqrt64: 1e6 random (incl. k^2, k^2-1) vs __int128 definition, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 2 */
    {
        uint32_t bad = 0; unsigned it, maxit = 0; uint64_t sumit = 0, cnt = 0;
        for (uint32_t x = 0; x < (1u << 20); x++) {
            bad += isqrt_newton(x, &it) != isqrt32(x);
            if (it > maxit) maxit = it;
        }
        for (int i = 0; i < 1000000; i++) {
            uint32_t x = xs32(&seed);
            if (i == 0) x = UINT32_MAX;
            bad += isqrt_newton(x, &it) != isqrt32(x);
            if (it > maxit) maxit = it;
            sumit += it; cnt++;
        }
        printf("ex2 newton: x < 2^20 all + 1e6 random vs isqrt32, bad=%u, max divisions=%u, mean (random 32-bit)=%.2f\n",
               bad, maxit, (double)sumit / (double)cnt);
        CHECK(bad == 0);
        CHECK(maxit <= 7);
    }

    /* 연습 3 */
    CHECK(divu10(0) == 0 && divu10(9) == 0 && divu10(10) == 1 && divu10(UINT32_MAX) == 429496729u);
    {
        uint32_t bad = 0;
        for (uint32_t x = 0; x < (1u << 22); x++) bad += divu10(x) != x / 10;
        for (int i = 0; i < 2000000; i++) { uint32_t x = xs32(&seed); bad += divu10(x) != x / 10; }
        printf("ex3 divu10: x < 2^22 all + 2e6 random, bad=%u (full 2^32: demo_exhaustive)\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 4 */
    CHECK(sdiv10(-1) == 0 && sdiv10(-9) == 0 && sdiv10(-10) == -1 && sdiv10(-11) == -1);
    CHECK(sdiv10(INT32_MIN) == -214748364 && sdiv10(INT32_MAX) == 214748364);
    {
        uint32_t bad = 0;
        for (int32_t x = -(1 << 21); x < (1 << 21); x++) bad += sdiv10(x) != x / 10;
        for (int i = 0; i < 2000000; i++) { int32_t x = (int32_t)xs32(&seed); bad += sdiv10(x) != x / 10; }
        for (int32_t k = 0; k < 1000; k++) { bad += sdiv10(INT32_MIN + k) != (INT32_MIN + k) / 10;
                                             bad += sdiv10(INT32_MAX - k) != (INT32_MAX - k) / 10; }
        printf("ex4 sdiv10: |x| < 2^21 all + 2e6 random + both ends, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* 연습 5 */
    CHECK(modinv32(3, 11) == 4);                 /* 3·4 = 12 ≡ 1 */
    CHECK(modinv32(10, 17) == 12);               /* 10·12 = 120 = 7·17 + 1 */
    CHECK(modinv32(6, 9) == 0);                  /* gcd 3 → 없음 */
    CHECK(modinv32(5, 1) == 0);
    {
        uint32_t bad = 0, none = 0;
        for (int i = 0; i < 200000; i++) {
            uint32_t m = xs32(&seed) >> (xs32(&seed) % 31), a = xs32(&seed);
            if (m < 2) m = 2;
            uint32_t inv = modinv32(a, m);
            uint32_t g = m, b = a % m;
            while (b) { uint32_t t = g % b; g = b; b = t; }
            if (g != 1) { none++; bad += inv != 0; }
            else bad += inv >= m || (uint64_t)a % m * inv % m != 1;
        }
        const uint32_t p = 4294967291u;          /* 소수: 페르마 역원 a^(p−2) 와 같아야 */
        for (int i = 0; i < 20000; i++) {
            uint32_t a = xs32(&seed) % (p - 1) + 1;
            bad += modinv32(a, p) != powmod32(a, p - 2, p);
        }
        printf("ex5 modinv32: 200000 random (%u non-invertible) + 20000 Fermat on p=2^32-5, bad=%u\n", none, bad);
        CHECK(bad == 0);
    }

    /* 연습 6 */
    CHECK(inv_mod2_32(3) == 0xAAAAAAABu);        /* 3 · 0xAAAAAAAB = 2^33 + 1 ≡ 1 */
    CHECK(inv_mod2_32(5) * 5u == 1u);
    {
        uint32_t bad = 0;
        for (uint32_t d = 1; d < 64; d += 2) {   /* 작은 홀수 d: x < 2^20 전수 */
            uint32_t inv = inv_mod2_32(d), lim = UINT32_MAX / d;
            bad += inv * d != 1u;
            for (uint32_t x = 0; x < (1u << 20); x++) bad += divisible_odd(x, inv, lim) != (x % d == 0);
        }
        for (int i = 0; i < 20000; i++) {        /* 랜덤 홀수 d: 배수 · 배수±1 · 랜덤 · 끝 */
            uint32_t d = (xs32(&seed) >> (xs32(&seed) % 32)) | 1u, inv = inv_mod2_32(d), lim = UINT32_MAX / d;
            uint32_t k = (uint32_t)(xs32(&seed) % ((uint64_t)lim + 1)), xs[] = { k * d, k * d + 1, k * d - 1, xs32(&seed), UINT32_MAX, lim * d };
            bad += inv * d != 1u;
            for (size_t j = 0; j < sizeof xs / sizeof xs[0]; j++) bad += divisible_odd(xs[j], inv, lim) != (xs[j] % d == 0);
        }
        printf("ex6 divisible_odd: odd d < 64 with x < 2^20 all + 20000 random d, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
