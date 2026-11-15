/* ALGO-M15 정수 산술 알고리즘: isqrt·거듭제곱·GCD·상수 나눗셈
 *
 * Part A: 비트 단위 정수 제곱근(digit-by-digit), 모듈러 빠른 거듭제곱(uint64 중간값),
 *         유클리드 GCD vs 이진 GCD(Stein), ÷10 / %10 을 곱셈+시프트(0xCCCCCCCD, >>35)로
 * Part B: 임의의 상수 d 에 대한 매직 넘버 계산 + 적용(33비트 곱수면 add 보정),
 *         128비트 없는 64비트 mulmod / powmod
 * Part C: 가속도계 크기 |a| (반올림 isqrt), 나눗셈 없는 uint32 → 10진 문자열(로그 출력),
 *         Park–Miller PRNG 점프(주파수 호핑 슬롯 k 의 상태), 클럭 비율 약분·LCM
 *
 * 테스트: 정의 그대로의 기준(r² ≤ x < (r+1)², g | a·g | b·베주 항등식, 컴파일러의 / 와 %,
 *         __uint128_t(테스트 쪽만), 직접 반복, snprintf) + 작은 범위 전수 + 경계 + xorshift32 랜덤.
 *         전체 2^32 스윕은 demo_exhaustive.c.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 네 가지 기본 연산 ---------------- */

/* floor(sqrt(x)). 2비트씩 내려가며 근의 비트를 하나씩 정한다(손으로 하는 개평법의 2진판).
 * 불변식(one = 4^j 일 때): res = P·2^(j+1), rem = x − P², P = 근의 j+1 번 비트 이상만 남긴 값 */
static uint16_t isqrt32(uint32_t x) {
    uint32_t rem = x, res = 0, one = 1u << 30;
    while (one > rem) one >>= 2;               /* 선택: 앞쪽 0 비트 건너뛰기 */
    while (one != 0) {
        if (rem >= res + one) {                /* (P + 2^j)² ≤ x ? → 근의 j 번 비트 = 1 */
            rem -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint16_t)res;
}

/* base^e mod m (m >= 1). 곱은 uint64 에서: (m−1)² < 2^64.
 * 불변식: r · b^e ≡ base^e0 (mod m) */
static uint32_t powmod32(uint32_t base, uint32_t e, uint32_t m) {
    uint64_t r = 1u % m, b = base % m;
    while (e != 0) {
        if (e & 1u) r = r * b % m;
        b = b * b % m;
        e >>= 1;
    }
    return (uint32_t)r;
}

/* 유클리드: gcd(a, b) = gcd(b, a mod b). gcd(0,0) = 0 */
static uint32_t gcd_euclid(uint32_t a, uint32_t b) {
    while (b != 0) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Stein(이진 GCD): 나눗셈 없이 시프트·뺄셈·비교만. gcd(0,0) = 0 */
static uint32_t gcd_binary(uint32_t a, uint32_t b) {
    if (a == 0) return b;
    if (b == 0) return a;
    unsigned k = 0;
    while (((a | b) & 1u) == 0) { a >>= 1; b >>= 1; k++; }   /* 공통 2^k */
    while ((a & 1u) == 0) a >>= 1;                         /* 이제 a 홀수 */
    do {
        while ((b & 1u) == 0) b >>= 1;                     /* b 도 홀수 */
        if (a > b) { uint32_t t = a; a = b; b = t; }       /* a <= b */
        b -= a;                                            /* 짝수(0 포함) */
    } while (b != 0);
    return a << k;
}

/* x / 10 = floor(x · 0xCCCCCCCD / 2^35) — 모든 uint32 x 에서 정확 (5. 풀이의 증명) */
static uint32_t div10(uint32_t x) {
    return (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35);
}

/* x % 10 = x − 10·(x / 10), 곱셈 대신 시프트 두 개 */
static uint32_t mod10(uint32_t x) {
    uint32_t q = div10(x);
    return x - ((q << 3) + (q << 1));
}

/* ---------------- Part B: 일반 매직 넘버, 64비트 mulmod ---------------- */

/* mode 0: q = x >> shift                         (d = 2^shift)
 * mode 1: q = (x · mul) >> shift                 (shift = p, 32 <= p <= 63, mul < 2^32)
 * mode 2: t = (x · mul) >> 32;
 *         q = (((x − t) >> 1) + t) >> shift      (진짜 곱수 = 2^32 + mul, shift = p − 33) */
typedef struct { uint32_t mul; uint8_t shift; uint8_t mode; } udiv_magic;

/* d >= 1. 가장 작은 p >= 32 를 찾는다: m = ceil(2^p / d), e = m·d − 2^p 일 때 e <= 2^(p−32) */
static udiv_magic magic_u32(uint32_t d) {
    udiv_magic r = { 0, 0, 0 };
    if ((d & (d - 1)) == 0) {                          /* 2의 거듭제곱 (d = 1 포함) */
        while ((1u << r.shift) != d) r.shift++;
        return r;
    }
    for (unsigned p = 32; p <= 64; p++) {
        uint64_t a = (p == 64) ? UINT64_MAX : (((uint64_t)1 << p) - 1);   /* 2^p − 1 */
        uint64_t m = a / d + 1;                        /* = ceil(2^p / d)   (d 가 2^p 를 못 나눔) */
        uint64_t e = d - 1 - a % d;                    /* = m·d − 2^p, 0 <= e < d */
        if (e <= ((uint64_t)1 << (p - 32))) {
            if (m < ((uint64_t)1 << 32)) {
                r.mul = (uint32_t)m; r.shift = (uint8_t)p; r.mode = 1;
            } else {                                   /* 2^32 <= m < 2^33 */
                r.mul = (uint32_t)(m - ((uint64_t)1 << 32)); r.shift = (uint8_t)(p - 33); r.mode = 2;
            }
            return r;
        }
    }
    return r;                                          /* 도달 안 함: p = 32 + ceil(log2 d) 에서 반드시 성립 */
}

static uint32_t magic_apply(udiv_magic g, uint32_t x) {
    if (g.mode == 0) return x >> g.shift;
    if (g.mode == 1) return (uint32_t)(((uint64_t)x * g.mul) >> g.shift);
    uint32_t t = (uint32_t)(((uint64_t)x * g.mul) >> 32);
    return (((x - t) >> 1) + t) >> g.shift;
}

/* a·b mod m (m >= 1) — 128비트 정수 없이: 두 배-더하기(double-and-add), 덧셈도 넘치지 않게 */
static uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t r = 0;
    a %= m;
    b %= m;
    while (b != 0) {
        if (b & 1u) r = (r >= m - a) ? r - (m - a) : r + a;   /* r = (r + a) mod m */
        a = (a >= m - a) ? a - (m - a) : a + a;              /* a = 2a mod m */
        b >>= 1;
    }
    return r;
}

static uint64_t powmod64(uint64_t base, uint64_t e, uint64_t m) {
    uint64_t r = 1u % m, b = base % m;
    while (e != 0) {
        if (e & 1u) r = mulmod64(r, b, m);
        b = mulmod64(b, b, m);
        e >>= 1;
    }
    return r;
}

/* ---------------- Part C: 펌웨어 쓰임 ---------------- */

/* 가속도계 3축 raw(int16) → |a| = round(sqrt(x² + y² + z²)). 최대 3·2^30 < 2^32 라 uint32 합 */
static uint16_t accel_mag(int16_t x, int16_t y, int16_t z) {
    uint32_t s = (uint32_t)((int32_t)x * x) + (uint32_t)((int32_t)y * y) + (uint32_t)((int32_t)z * z);
    uint32_t r = isqrt32(s);
    if (s - r * r > r) r++;                    /* sqrt(s) >= r + 1/2  ⇔  s − r² > r */
    return (uint16_t)r;
}

/* uint32 → 10진 문자열 (printf 없는 로그). '/' '%' 금지: div10/mod10 만. 반환 = 글자 수, buf >= 11 */
static size_t u32_to_dec(uint32_t x, char *buf) {
    char tmp[10];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + mod10(x));
        x = div10(x);
    } while (x != 0);
    for (size_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

/* Park–Miller "minimal standard": x' = 16807·x mod (2^31 − 1). 슬롯 k 의 상태를 O(log k) 로 */
#define PM_A 16807u
#define PM_M 2147483647u
static uint32_t lcg_jump(uint32_t x0, uint32_t k) {
    return (uint32_t)((uint64_t)powmod32(PM_A, k, PM_M) * x0 % PM_M);
}

/* 분수 num/den 을 기약분수로 (PLL 의 N/M, 샘플레이트 변환 비율). 둘 다 0 이면 그대로 */
static void ratio_reduce(uint32_t *num, uint32_t *den) {
    uint32_t g = gcd_binary(*num, *den);
    if (g > 1) { *num /= g; *den /= g; }
}

/* lcm(a, b) — 두 주기의 공통 주기(hyper-period). 0 이 섞이면 0, uint32 를 넘으면 0(오류) */
static uint32_t lcm32(uint32_t a, uint32_t b) {
    if (a == 0 || b == 0) return 0;
    uint64_t l = (uint64_t)(a / gcd_binary(a, b)) * b;
    return l > UINT32_MAX ? 0 : (uint32_t)l;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준: 정의 그대로 r² <= x < (r+1)² (uint64 라 넘침 없음) */
static bool is_floor_sqrt(uint32_t x, uint32_t r) {
    uint64_t r2 = (uint64_t)r * r, r12 = (uint64_t)(r + 1) * (r + 1);
    return r2 <= x && x < r12;
}
/* 기준: 왼쪽→오른쪽 이진 거듭제곱 (학생 코드와 다른 순서) */
static uint32_t ref_powmod(uint32_t b, uint32_t e, uint32_t m) {
    uint64_t r = 1u % m;
    for (int i = 31; i >= 0; i--) {
        r = r * r % m;
        if ((e >> i) & 1u) r = r * (b % m) % m;
    }
    return (uint32_t)r;
}
/* 기준: g 가 a, b 의 공약수 + 베주 항등식 s·a + t·b = g 가 성립하면 g = gcd (모든 공약수가 g 를 나눔) */
static bool is_gcd(uint32_t a, uint32_t b, uint32_t g) {
    if (a == 0 && b == 0) return g == 0;
    if (g == 0 || a % g != 0 || b % g != 0) return false;
    int64_t r0 = a, r1 = b, s0 = 1, s1 = 0, t0 = 0, t1 = 1;    /* 확장 유클리드, 계수 |s|,|t| <= 2^32 */
    while (r1 != 0) {
        int64_t q = r0 / r1, tmp;
        tmp = r0 - q * r1; r0 = r1; r1 = tmp;
        tmp = s0 - q * s1; s0 = s1; s1 = tmp;
        tmp = t0 - q * t1; t0 = t1; t1 = tmp;
    }
    __int128 lhs = (__int128)s0 * a + (__int128)t0 * b;
    return lhs == (__int128)g;
}
static bool magic_ok_on(uint32_t d, udiv_magic g, uint32_t x) { return magic_apply(g, x) == x / d; }

int main(void) {
    uint32_t seed = 0x2545F491u;

    /* ===== Part A: isqrt32 ===== */
    CHECK(isqrt32(0) == 0);
    CHECK(isqrt32(1) == 1);
    CHECK(isqrt32(8) == 2);                     /* LeetCode 69 예 */
    CHECK(isqrt32(15) == 3 && isqrt32(16) == 4 && isqrt32(17) == 4);
    CHECK(isqrt32(4294836224u) == 65534);       /* 65535² − 1 */
    CHECK(isqrt32(4294836225u) == 65535);       /* 65535² */
    CHECK(isqrt32(UINT32_MAX) == 65535);
    {
        uint32_t bad = 0;
        for (uint32_t x = 0; x < (1u << 20); x++) bad += !is_floor_sqrt(x, isqrt32(x));
        for (uint32_t k = 1; k <= 65535; k++) {          /* 모든 완전제곱 k² 과 그 양옆 */
            uint32_t s = k * k;
            bad += !is_floor_sqrt(s - 1, isqrt32(s - 1)) + !is_floor_sqrt(s, isqrt32(s));
            if (s != UINT32_MAX) bad += !is_floor_sqrt(s + 1, isqrt32(s + 1));
        }
        for (int i = 0; i < 1000000; i++) { uint32_t x = xs32(&seed); bad += !is_floor_sqrt(x, isqrt32(x)); }
        printf("isqrt32: x < 2^20 all + k²-1/k²/k²+1 (k<=65535) + 1e6 random, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part A: powmod32 ===== */
    CHECK(powmod32(2, 10, 1000) == 24);
    CHECK(powmod32(7, 0, 13) == 1);
    CHECK(powmod32(0, 0, 13) == 1);             /* 관례 0^0 = 1 */
    CHECK(powmod32(5, 3, 1) == 0);              /* mod 1 은 항상 0 */
    CHECK(powmod32(0, 5, 7) == 0);
    CHECK(powmod32(UINT32_MAX - 1, 2, UINT32_MAX) == 1);   /* (−1)² ≡ 1, (m−1)² 이 uint64 에 들어감 */
    CHECK(powmod32(3, 4294967290u, 4294967291u) == 1);     /* Fermat: p = 2^32 − 5 (최대 32비트 소수) */
    {
        uint32_t bad = 0;
        for (int i = 0; i < 20000; i++) {       /* 작은 지수: 직접 곱하기와 대조 */
            uint32_t m = xs32(&seed) | 1u, b = xs32(&seed), e = xs32(&seed) % 64;
            if (i % 4 == 0) m = xs32(&seed) % 100 + 1;
            uint64_t r = 1u % m;
            for (uint32_t k = 0; k < e; k++) r = r * (b % m) % m;
            bad += powmod32(b, e, m) != (uint32_t)r;
        }
        for (int i = 0; i < 200000; i++) {      /* 큰 지수: 왼→오 기준과 대조 */
            uint32_t m = xs32(&seed), b = xs32(&seed), e = xs32(&seed);
            if (m == 0) m = 1;
            bad += powmod32(b, e, m) != ref_powmod(b, e, m);
        }
        uint32_t ferm = 0;
        for (int i = 0; i < 20000; i++) {       /* 두 소수에서 a^(p−1) ≡ 1 */
            uint32_t a = xs32(&seed) % (PM_M - 1) + 1;
            ferm += powmod32(a, PM_M - 1, PM_M) != 1;
            a = xs32(&seed) % (4294967291u - 1) + 1;
            ferm += powmod32(a, 4294967290u, 4294967291u) != 1;
        }
        printf("powmod32: 20000 small-e vs repeated mult + 200000 vs left-to-right ref, bad=%u; Fermat fails=%u\n", bad, ferm);
        CHECK(bad == 0);
        CHECK(ferm == 0);
    }

    /* ===== Part A: GCD ===== */
    CHECK(gcd_euclid(0, 0) == 0 && gcd_binary(0, 0) == 0);
    CHECK(gcd_euclid(0, 12) == 12 && gcd_binary(0, 12) == 12);
    CHECK(gcd_euclid(12, 0) == 12 && gcd_binary(12, 0) == 12);
    CHECK(gcd_euclid(48000, 44100) == 300 && gcd_binary(48000, 44100) == 300);
    CHECK(gcd_binary(1u << 31, 1u << 30) == (1u << 30));
    CHECK(gcd_binary(UINT32_MAX, UINT32_MAX - 1) == 1);
    CHECK(gcd_euclid(2971215073u, 1836311903u) == 1 && gcd_binary(2971215073u, 1836311903u) == 1); /* F47, F46 */
    {
        uint32_t bad = 0;
        for (uint32_t a = 0; a <= 200; a++)     /* 전수: 가장 큰 공약수를 직접 찾기 */
            for (uint32_t b = 0; b <= 200; b++) {
                uint32_t g = 0;
                if (a == 0) g = b; else if (b == 0) g = a;
                else for (uint32_t d = 1; d <= a && d <= b; d++) if (a % d == 0 && b % d == 0) g = d;
                bad += (gcd_euclid(a, b) != g) + (gcd_binary(a, b) != g);
            }
        for (int i = 0; i < 300000; i++) {      /* 랜덤: 공통 2^k·인수를 섞어 큰 gcd 도 나오게 */
            uint32_t c = (xs32(&seed) & 0xFFu) + 1, a = xs32(&seed), b = xs32(&seed);
            if (i % 3 == 0) { a = (a >> 12) * c; b = (b >> 12) * c; }
            if (i % 5 == 0) { a <<= (xs32(&seed) % 8); b <<= (xs32(&seed) % 8); }
            uint32_t g1 = gcd_euclid(a, b), g2 = gcd_binary(a, b);
            bad += (g1 != g2) + !is_gcd(a, b, g2);
        }
        printf("gcd: all 0..200 pairs vs brute force + 300000 random (Bezout check), bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part A: div10 / mod10 ===== */
    CHECK(div10(0) == 0 && mod10(0) == 0);
    CHECK(div10(9) == 0 && div10(10) == 1 && mod10(19) == 9);
    CHECK(div10(UINT32_MAX) == 429496729u && mod10(UINT32_MAX) == 5);
    CHECK(div10(4294967289u) == 429496728u);    /* x ≡ 9 (mod 10) 중 최대: 오차가 가장 큰 곳 */
    {
        uint32_t bad = 0;
        for (uint32_t x = 0; x < (1u << 24); x++) bad += (div10(x) != x / 10) + (mod10(x) != x % 10);
        for (uint32_t x = UINT32_MAX - 1000000; x != 0; x++) bad += (div10(x) != x / 10) + (mod10(x) != x % 10);
        for (int i = 0; i < 2000000; i++) { uint32_t x = xs32(&seed); bad += (div10(x) != x / 10) + (mod10(x) != x % 10); }
        printf("div10/mod10: x < 2^24 all + top 1e6 + 2e6 random, bad=%u (full 2^32: demo_exhaustive)\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part B: magic_u32 ===== */
    {
        udiv_magic g;
        g = magic_u32(10);  CHECK(g.mode == 1 && g.mul == 0xCCCCCCCDu && g.shift == 35);
        g = magic_u32(3);   CHECK(g.mode == 1 && g.mul == 0xAAAAAAABu && g.shift == 33);
        g = magic_u32(7);   CHECK(g.mode == 2 && g.mul == 0x24924925u && g.shift == 2);
        g = magic_u32(1000); CHECK(g.mode == 1 && g.mul == 0x10624DD3u && g.shift == 38);
        g = magic_u32(641); CHECK(g.mode == 1 && g.mul == 6700417u && g.shift == 32);   /* 641·6700417 = 2^32 + 1 */
        g = magic_u32(1);   CHECK(g.mode == 0 && g.shift == 0);
        g = magic_u32(1u << 31); CHECK(g.mode == 0 && g.shift == 31);

        uint32_t bad = 0, ndiv = 0, nm[3] = { 0, 0, 0 };
        for (int i = 0; i < 6000; i++) {
            uint32_t d;
            if (i < 2000) d = (uint32_t)i + 1;                          /* 1..2000 전부 */
            else if (i < 2096) { unsigned k = (unsigned)(i - 2000) / 3; /* 2^k − 1, 2^k, 2^k + 1 */
                d = (1u << (k % 32)) + (uint32_t)((i - 2000) % 3) - 1u; if (d == 0) d = UINT32_MAX; }
            else d = xs32(&seed) >> (xs32(&seed) % 32);                 /* 크기가 고르게 퍼진 랜덤 */
            if (d == 0) d = 1;
            g = magic_u32(d);
            ndiv++; nm[g.mode]++;
            uint32_t top = UINT32_MAX / d * d;
            uint32_t xs[] = { 0, 1, d - 1, d, d + 1, 2 * d - 1, top, top - 1, UINT32_MAX, UINT32_MAX - 1, UINT32_MAX - d };
            for (size_t j = 0; j < sizeof xs / sizeof xs[0]; j++) bad += !magic_ok_on(d, g, xs[j]);
            for (int j = 0; j < 150; j++) bad += !magic_ok_on(d, g, xs32(&seed));
            for (int j = 0; j < 20; j++) { uint32_t q = (uint32_t)(xs32(&seed) % ((uint64_t)UINT32_MAX / d + 1));  /* 몫 경계 q·d − 1, q·d (d = 1 이면 2^32) */
                uint64_t x = (uint64_t)q * d; bad += !magic_ok_on(d, g, (uint32_t)x);
                if (x) bad += !magic_ok_on(d, g, (uint32_t)(x - 1)); }
        }
        printf("magic_u32: %u divisors (mode0 %u, mode1 %u, mode2 %u), ~190 x each, bad=%u\n", ndiv, nm[0], nm[1], nm[2], bad);
        CHECK(bad == 0);
    }

    /* ===== Part B: mulmod64 / powmod64 ===== */
    CHECK(mulmod64(UINT64_MAX - 1, UINT64_MAX - 1, UINT64_MAX) == 1);
    CHECK(mulmod64(123, 456, 1) == 0);
    CHECK(powmod64(2, 18446744073709551556u, 18446744073709551557u) == 1);   /* Fermat, p = 2^64 − 59 */
    CHECK(powmod64(3, (1ull << 61) - 2, (1ull << 61) - 1) == 1);             /* Fermat, p = 2^61 − 1 */
    {
        uint32_t bad = 0;
        for (int i = 0; i < 200000; i++) {      /* __uint128_t 는 테스트 쪽에서만 */
            uint64_t a = (uint64_t)xs32(&seed) << 32 | xs32(&seed), b = (uint64_t)xs32(&seed) << 32 | xs32(&seed);
            uint64_t m = (uint64_t)xs32(&seed) << 32 | xs32(&seed);
            if (i % 4 == 1) m = UINT64_MAX - (xs32(&seed) & 0xFF);
            if (i % 4 == 2) m >>= xs32(&seed) % 64;
            if (m == 0) m = 1;
            bad += mulmod64(a, b, m) != (uint64_t)((unsigned __int128)a * b % m);
        }
        uint32_t ferm = 0;
        for (int i = 0; i < 300; i++) {
            uint64_t a = ((uint64_t)xs32(&seed) << 32 | xs32(&seed)) % (18446744073709551557u - 1) + 1;
            ferm += powmod64(a, 18446744073709551556u, 18446744073709551557u) != 1;
        }
        printf("mulmod64: 200000 random vs __uint128_t, bad=%u; powmod64 Fermat (p=2^64-59) 300 bases, fails=%u\n", bad, ferm);
        CHECK(bad == 0);
        CHECK(ferm == 0);
    }

    /* ===== Part C: accel_mag ===== */
    CHECK(accel_mag(0, 0, 0) == 0);
    CHECK(accel_mag(3, 4, 0) == 5);
    CHECK(accel_mag(0, 0, 4096) == 4096);       /* ±8g 범위 4096 LSB/g 센서가 정지: 1 g */
    CHECK(accel_mag(-32768, -32768, -32768) == 56756);   /* sqrt(3·2^30) = 56755.84 → 반올림 */
    CHECK(accel_mag(1, 1, 0) == 1);             /* sqrt 2 = 1.41 */
    CHECK(accel_mag(1, 1, 1) == 2);             /* sqrt 3 = 1.73 */
    {
        uint32_t bad = 0;
        for (int i = 0; i < 1000000; i++) {
            int16_t x = (int16_t)xs32(&seed), y = (int16_t)xs32(&seed), z = (int16_t)xs32(&seed);
            if (i % 2) { x >>= 6; y >>= 6; z >>= 6; }         /* 작은 값도 */
            uint64_t s = (uint64_t)((int64_t)x * x + (int64_t)y * y + (int64_t)z * z);
            uint64_t r = accel_mag(x, y, z);
            /* round(sqrt s) = r  ⇔  (2r − 1)² <= 4s < (2r + 1)² */
            bool ok = (r == 0 ? true : (2 * r - 1) * (2 * r - 1) <= 4 * s) && 4 * s < (2 * r + 1) * (2 * r + 1);
            bad += !ok;
        }
        printf("accel_mag: 1e6 random int16 triples vs (2r-1)^2 <= 4s < (2r+1)^2, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part C: u32_to_dec ===== */
    {
        char a[16], b[16];
        CHECK(u32_to_dec(0, a) == 1 && strcmp(a, "0") == 0);
        CHECK(u32_to_dec(UINT32_MAX, a) == 10 && strcmp(a, "4294967295") == 0);
        CHECK(u32_to_dec(1000000000u, a) == 10 && strcmp(a, "1000000000") == 0);
        uint32_t bad = 0;
        for (int i = 0; i < 300000; i++) {
            uint32_t x = xs32(&seed) >> (xs32(&seed) % 32);
            if (i < 10) x = (uint32_t)i;
            size_t n = u32_to_dec(x, a);
            int m = snprintf(b, sizeof b, "%u", (unsigned)x);
            bad += (n != (size_t)m) || strcmp(a, b) != 0;
        }
        printf("u32_to_dec: 300000 values (all digit counts) vs snprintf, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part C: lcg_jump ===== */
    CHECK(lcg_jump(1, 1) == 16807);
    CHECK(lcg_jump(1, 10000) == 1043618065u);   /* Park & Miller (1988) 의 확인 값 */
    CHECK(lcg_jump(12345, PM_M - 1) == 12345);  /* 주기 = M − 1 (16807 은 원시근) */
    {
        uint32_t bad = 0, x = 42;
        for (uint32_t k = 1; k <= 200000; k++) {   /* 직접 반복과 대조 */
            x = (uint32_t)((uint64_t)x * PM_A % PM_M);
            if (k % 97 == 0 || k < 100) bad += lcg_jump(42, k) != x;
        }
        for (int i = 0; i < 20000; i++) {          /* 점프 합성: jump(jump(x, a), b) == jump(x, a + b) */
            uint32_t x0 = xs32(&seed) % (PM_M - 1) + 1, ka = xs32(&seed) >> 2, kb = xs32(&seed) >> 2;
            bad += lcg_jump(lcg_jump(x0, ka), kb) != lcg_jump(x0, ka + kb);
        }
        printf("lcg_jump: vs 200000-step iteration + 20000 jump compositions, bad=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part C: ratio_reduce / lcm32 ===== */
    {
        uint32_t n = 48000, d = 44100;
        ratio_reduce(&n, &d); CHECK(n == 160 && d == 147);
        n = 0; d = 0; ratio_reduce(&n, &d); CHECK(n == 0 && d == 0);
        n = 0; d = 25; ratio_reduce(&n, &d); CHECK(n == 0 && d == 1);
        CHECK(lcm32(48000, 44100) == 7056000u);
        CHECK(lcm32(0, 5) == 0);
        CHECK(lcm32(65536, 65537) == 0);        /* 2^32 + 2^16 → 넘침 = 0 */
        CHECK(lcm32(65535, 65537) == 4294967295u);  /* 서로소, 곱 = 2^32 − 1 딱 맞음 */
        uint32_t bad = 0;
        for (int i = 0; i < 100000; i++) {
            uint32_t c = (xs32(&seed) & 0xFFFu) + 1, a = (xs32(&seed) >> 12) * c, b = (xs32(&seed) >> 12) * c;
            uint32_t a2 = a, b2 = b;
            ratio_reduce(&a2, &b2);
            if (a == 0 && b == 0) bad += a2 != 0 || b2 != 0;
            else bad += gcd_euclid(a2, b2) != 1 || (uint64_t)a2 * b != (uint64_t)b2 * a;
            uint32_t a3 = xs32(&seed) >> (xs32(&seed) % 32), b3 = xs32(&seed) >> (xs32(&seed) % 32);
            uint32_t l = lcm32(a3, b3);
            if (a3 == 0 || b3 == 0) bad += l != 0;
            else {
                uint64_t L = (uint64_t)a3 / gcd_euclid(a3, b3) * b3;
                bad += (L > UINT32_MAX) ? (l != 0) : (l != L || l % a3 != 0 || l % b3 != 0);
            }
        }
        printf("ratio_reduce/lcm32: 100000 random (coprime + cross-multiply, lcm overflow -> 0), bad=%u\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
