/* ARM-B07 곱셈·나눗셈 — 테스트 하네스 (starter.S / solution.S 공용)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o /tmp/ARM-B07_solution
 *
 * 규칙: C 에서 UB 인 경우(0 나누기, INT64_MIN / -1, signed 오버플로)는 C 로 계산하지 않는다.
 *       asm 을 호출하고, 기대값은 손으로 쓴 상수(= ISA 가 정의한 결과)와 비교한다.
 *       참조 모델은 unsigned __int128 (wrap 이 정의된 unsigned 연산만 사용).
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned __int128 u128;

/* ---------- asm 함수 프로토타입 ---------- */
/* Part A */
void     mul128_u(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo);
void     mul128_s(int64_t a, int64_t b, int64_t *hi, uint64_t *lo);
uint64_t mulhi_u64(uint64_t a, uint64_t b);
uint64_t umull32(uint32_t a, uint32_t b);
int64_t  smull32(int32_t a, int32_t b);
uint64_t madd_u64(uint64_t a, uint64_t b, uint64_t c);   /* c + a*b */
uint64_t msub_u64(uint64_t a, uint64_t b, uint64_t c);   /* c - a*b */
int64_t  mneg_s64(int64_t a, int64_t b);                 /* -(a*b), wrap */
/* Part B */
uint64_t udiv_u64(uint64_t a, uint64_t b);
int64_t  sdiv_s64(int64_t a, int64_t b);
int32_t  sdiv_s32(int32_t a, int32_t b);
uint64_t mod_u64(uint64_t a, uint64_t b);
int64_t  mod_s64(int64_t a, int64_t b);
int64_t  mod_s64_bug(int64_t a, int64_t b);              /* 심어 둔 버그 */
int64_t  mulhi_s64_bug(int64_t a, int64_t b);            /* 심어 둔 버그 */
uint64_t umull32_bug(uint32_t a, uint32_t b);            /* 심어 둔 버그 */
/* Part C */
uint64_t q32_mul(uint64_t a, uint64_t b);                /* (a*b) >> 32 */
uint64_t calc_mult(uint64_t freq_hz);                    /* round(1e9 * 2^32 / freq) */
static inline uint64_t ticks_to_ns(uint64_t ticks, uint64_t mult) { return q32_mul(ticks, mult); }

/* ---------- 하네스 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;           /* 고정 seed → 매번 같은 입력 */
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int64_t  as_s64(uint64_t u) { int64_t s; memcpy(&s, &u, sizeof s); return s; }  /* 비트 그대로 */
static uint64_t as_u64(int64_t s)  { return (uint64_t)s; }                            /* 정의된 변환 */
/* 크기가 고르게 퍼진 피연산자: 무작위 64비트를 0~63 비트 오른쪽으로 민다 */
static uint64_t rnd_mag(void) { uint64_t r = rnd(); return r >> (rnd() & 63); }
static int64_t  rnd_s(void)   { uint64_t m = rnd_mag(); return as_s64((rnd() & 1) ? 0u - m : m); }

static char g_why[160];                                  /* 첫 불일치 기록 */
#define MISMATCH(...) do { if (!bad++) snprintf(g_why, sizeof g_why, __VA_ARGS__); } while (0)
static int report(int bad, const char *what) {           /* what 은 PASS/FAIL 줄에 찍히는 이름표 */
    (void)what; if (bad) printf("      %d mismatches, first: %s\n", bad, g_why); return bad == 0; }

#define N_RAND 200000

int main(void) {
    uint64_t hi, lo; int64_t shi; int bad;

    /* ===== Part A: 128비트 곱 ===== */
    hi = lo = 0xAAAA;
    mul128_u(UINT64_MAX, UINT64_MAX, &hi, &lo);          /* (2^64-1)^2 = 2^128 - 2^65 + 1 */
    CHECK(hi == 0xFFFFFFFFFFFFFFFEull && lo == 1u);
    mul128_u(1ull << 32, 1ull << 32, &hi, &lo);          /* 2^64 → hi=1, lo=0 */
    CHECK(hi == 1u && lo == 0u);
    mul128_u(0xDEADBEEFCAFEBABEull, 0x0123456789ABCDEFull, &hi, &lo);
    CHECK(hi == 0x00FD5BDEEEB2A01Dull && lo == 0x7EB689F4EA447D62ull);
    mul128_u(0, UINT64_MAX, &hi, &lo);
    CHECK(hi == 0u && lo == 0u);

    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = rnd(), b = (i & 1) ? rnd() : rnd_mag();
        u128 p = (u128)a * b;
        mul128_u(a, b, &hi, &lo);
        if (hi != (uint64_t)(p >> 64) || lo != (uint64_t)p || mulhi_u64(a, b) != (uint64_t)(p >> 64))
            MISMATCH("a=%016" PRIx64 " b=%016" PRIx64, a, b);
    }
    CHECK(report(bad, "mul128_u, mulhi_u64 vs unsigned __int128"));

    mul128_s(-1, -1, &shi, &lo);                          /* 1 */
    CHECK(shi == 0 && lo == 1u);
    mul128_s(-1, 1, &shi, &lo);                           /* -1 → hi 전부 1 */
    CHECK(shi == -1 && lo == UINT64_MAX);
    mul128_s(INT64_MIN, INT64_MIN, &shi, &lo);            /* 2^126 */
    CHECK(shi == 0x4000000000000000ll && lo == 0u);
    mul128_s(INT64_MIN, -1, &shi, &lo);                   /* +2^63: 64비트엔 안 들어가지만 128비트엔 들어간다 */
    CHECK(shi == 0 && lo == 0x8000000000000000ull);
    mul128_s(INT64_MAX, INT64_MAX, &shi, &lo);            /* 2^126 - 2^64 + 1 */
    CHECK(shi == 0x3FFFFFFFFFFFFFFFll && lo == 1u);

    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        int64_t a = (i & 1) ? as_s64(rnd()) : rnd_s(), b = rnd_s();
        /* signed 128비트 곱 mod 2^128 == unsigned 곱(부호 확장한 비트 패턴) mod 2^128 */
        u128 p = (u128)(__int128)a * (u128)(__int128)b;
        mul128_s(a, b, &shi, &lo);
        if (as_u64(shi) != (uint64_t)(p >> 64) || lo != (uint64_t)p)
            MISMATCH("a=%" PRId64 " b=%" PRId64, a, b);
    }
    CHECK(report(bad, "mul128_s vs __int128"));

    CHECK(umull32(0xFFFFFFFFu, 0xFFFFFFFFu) == 0xFFFFFFFE00000001ull);
    CHECK(smull32(INT32_MIN, INT32_MIN) == 0x4000000000000000ll);
    CHECK(smull32(INT32_MIN, -1) == 0x80000000ll);        /* 2^31 — int32 로는 표현 불가, int64 로는 OK */
    CHECK(smull32(-3, 7) == -21);
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint32_t a = (uint32_t)rnd(), b = (uint32_t)(rnd() >> 32);
        int32_t sa, sb; memcpy(&sa, &a, 4); memcpy(&sb, &b, 4);
        if (umull32(a, b) != (uint64_t)a * b || smull32(sa, sb) != (int64_t)sa * sb)
            MISMATCH("a=%08" PRIx32 " b=%08" PRIx32, a, b);
    }
    CHECK(report(bad, "umull32/smull32 vs C 64비트 곱"));

    CHECK(madd_u64(3, 5, 100) == 115u);                   /* 100 + 3*5 */
    CHECK(msub_u64(3, 5, 100) == 85u);                    /* 100 - 3*5 */
    CHECK(msub_u64(3, 5, 0) == UINT64_MAX - 14u);         /* 0 - 15 → wrap */
    CHECK(mneg_s64(6, 7) == -42);
    CHECK(mneg_s64(INT64_MIN, 1) == INT64_MIN);           /* -(INT64_MIN) 은 wrap → INT64_MIN (C 로는 UB) */
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = rnd(), b = rnd(), c = rnd();
        if (madd_u64(a, b, c) != c + a * b || msub_u64(a, b, c) != c - a * b ||
            as_u64(mneg_s64(as_s64(a), as_s64(b))) != 0u - a * b)
            MISMATCH("a=%016" PRIx64 " b=%016" PRIx64 " c=%016" PRIx64, a, b, c);
    }
    CHECK(report(bad, "madd/msub/mneg vs unsigned wrap 식"));

    /* ===== Part B: 나눗셈·나머지 ===== */
    CHECK(udiv_u64(100, 7) == 14u && mod_u64(100, 7) == 2u);
    CHECK(udiv_u64(UINT64_MAX, 1) == UINT64_MAX && mod_u64(UINT64_MAX, UINT64_MAX) == 0u);
    CHECK(sdiv_s64(-7, 2) == -3 && mod_s64(-7, 2) == -1); /* 0 쪽으로 버림, 나머지 부호 = 피제수 */
    CHECK(sdiv_s64(7, -2) == -3 && mod_s64(7, -2) == 1);
    CHECK(sdiv_s64(-7, -2) == 3 && mod_s64(-7, -2) == -1);
    /* ISA 가 정의한 결과 (C 에서는 전부 UB → 기대값을 손으로 쓴다) */
    CHECK(udiv_u64(12345, 0) == 0u);                      /* UDIV x/0 → 0 */
    CHECK(sdiv_s64(-12345, 0) == 0);                      /* SDIV x/0 → 0 */
    CHECK(sdiv_s64(INT64_MIN, -1) == INT64_MIN);          /* 오버플로 → wrap, 트랩 없음 */
    CHECK(sdiv_s32(INT32_MIN, -1) == INT32_MIN);
    CHECK(sdiv_s32(7, 0) == 0);
    CHECK(mod_u64(12345, 0) == 12345u);                   /* a - 0*0 = a */
    CHECK(mod_s64(-12345, 0) == -12345);
    CHECK(mod_s64(INT64_MIN, -1) == 0);                   /* INT64_MIN - INT64_MIN*(-1) = 0 (mod 2^64) */

    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = (i & 1) ? rnd() : rnd_mag(), b = rnd_mag();
        if (b == 0) continue;                             /* C 의 a/0 은 UB → 위에서 따로 */
        if (udiv_u64(a, b) != a / b || mod_u64(a, b) != a % b)
            MISMATCH("a=%" PRIu64 " b=%" PRIu64, a, b);
    }
    CHECK(report(bad, "udiv/mod_u64 vs C"));
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        int64_t a = (i & 1) ? as_s64(rnd()) : rnd_s(), b = rnd_s();
        if (b == 0 || (a == INT64_MIN && b == -1)) continue;   /* C UB 조합 제외 */
        if (sdiv_s64(a, b) != a / b || mod_s64(a, b) != a % b)
            MISMATCH("a=%" PRId64 " b=%" PRId64, a, b);
    }
    CHECK(report(bad, "sdiv/mod_s64 vs C"));

    /* 심어 둔 버그를 테스트가 잡는가? (불일치가 1개 이상 나와야 PASS) */
    CHECK(mod_s64_bug(-7, 2) != -1 && mod_s64_bug(7, -2) != 1);       /* udiv 를 쓴 나머지 */
    CHECK(mulhi_s64_bug(-1, 1) != -1);                                /* umulh ≠ smulh */
    CHECK(umull32_bug(0x10000u, 0x10000u) != 0x100000000ull);         /* 32비트 곱은 0 이 된다 */

    /* ===== Part C: Q32.32 고정소수점 ===== */
    CHECK(q32_mul(0x180000000ull, 0x200000000ull) == 0x300000000ull);   /* 1.5 * 2.0 = 3.0 */
    CHECK(q32_mul(0x80000000ull, 0x80000000ull) == 0x40000000ull);      /* 0.5 * 0.5 = 0.25 */
    CHECK(q32_mul(1, 1) == 0);                                          /* 2^-64 → 버림 */
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = rnd_mag(), b = rnd_mag();
        if (q32_mul(a, b) != (uint64_t)(((u128)a * b) >> 32))
            MISMATCH("a=%016" PRIx64 " b=%016" PRIx64, a, b);
    }
    CHECK(report(bad, "q32_mul vs (u128 곱) >> 32"));

    const uint64_t F = 24000000u;                         /* 24 MHz 타이머 (mach_absolute_time 의 tick) */
    uint64_t mult = calc_mult(F);
    CHECK(mult == 178956970667ull);                       /* 41.666.. ns * 2^32 = 178956970666.67 → 반올림 */
    CHECK(calc_mult(1000000000u) == 1ull << 32);          /* 1 GHz: tick 1개 = 1.0 ns */
    CHECK(calc_mult(0) == 0);                             /* 0 Hz: udiv x/0 → 0 (설정 실수 감지용 sentinel) */
    CHECK(ticks_to_ns(F, mult) == 1000000000u);           /* 1초 */
    CHECK(ticks_to_ns(3, mult) == 125u);                  /* 3 tick = 125 ns 정확히 */
    /* 오차 한계: |mult - 진짜값| <= 1/2  →  |근사 - 정확| <= t/2^33 + 1 (ns) */
    bad = 0;
    const uint64_t DAY = 86400ull * F;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t t = rnd() % (DAY + 1);
        uint64_t exact = (uint64_t)((u128)t * 1000000000u / F);
        uint64_t approx = ticks_to_ns(t, mult);
        uint64_t err = approx > exact ? approx - exact : exact - approx;
        if (err > (t >> 33) + 1) MISMATCH("t=%" PRIu64 " err=%" PRIu64, t, err);
    }
    CHECK(report(bad, "하루치 tick 범위 오차 한계"));
    uint64_t exact_day = (uint64_t)((u128)DAY * 1000000000u / F);
    printf("INFO  1 day = %" PRIu64 " ticks -> approx %" PRIu64 " ns, exact %" PRIu64 " ns, drift %+" PRId64 " ns\n",
           DAY, ticks_to_ns(DAY, mult), exact_day, as_s64(ticks_to_ns(DAY, mult) - exact_day));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
