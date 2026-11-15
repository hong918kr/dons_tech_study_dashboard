/* ARM-B07 추가 연습 — 테스트 (practice.S 와 함께 빌드)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B07_practice
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned __int128 u128;

int64_t  smulh_via_umulh(int64_t a, int64_t b);
uint64_t udiv10(uint64_t x);
int      safe_sdiv(int64_t a, int64_t b, int64_t *q, int64_t *r);
int      mul_overflow_u64(uint64_t a, uint64_t b, uint64_t *res);
int      mul_overflow_s64(int64_t a, int64_t b, int64_t *res);
uint64_t q32_mul_round(uint64_t a, uint64_t b);
uint64_t dot_u32(const uint32_t *a, const uint32_t *b, size_t n);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x243F6A8885A308D3ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int64_t  as_s64(uint64_t u) { int64_t s; memcpy(&s, &u, sizeof s); return s; }
static uint64_t rnd_mag(void) { uint64_t r = rnd(); return r >> (rnd() & 63); }
static int64_t  rnd_s(void)   { uint64_t m = rnd_mag(); return as_s64((rnd() & 1) ? 0u - m : m); }

static char g_why[160];
#define MISMATCH(...) do { if (!bad++) snprintf(g_why, sizeof g_why, __VA_ARGS__); } while (0)
static int report(int bad, const char *what) {
    (void)what; if (bad) printf("      %d mismatches, first: %s\n", bad, g_why); return bad == 0; }

#define N_RAND 200000

int main(void) {
    int bad;

    /* 연습 1: SMULH 를 UMULH + 보정으로 */
    CHECK(smulh_via_umulh(-1, 1) == -1);
    CHECK(smulh_via_umulh(INT64_MIN, INT64_MIN) == 0x4000000000000000ll);
    CHECK(smulh_via_umulh(INT64_MIN, -1) == 0);
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        int64_t a = (i & 1) ? as_s64(rnd()) : rnd_s(), b = rnd_s();
        u128 p = (u128)(__int128)a * (u128)(__int128)b;
        if ((uint64_t)smulh_via_umulh(a, b) != (uint64_t)(p >> 64)) MISMATCH("a=%" PRId64 " b=%" PRId64, a, b);
    }
    CHECK(report(bad, "smulh_via_umulh vs __int128"));

    /* 연습 2: x/10 magic number */
    CHECK(udiv10(0) == 0 && udiv10(9) == 0 && udiv10(10) == 1);
    CHECK(udiv10(UINT64_MAX) == 1844674407370955161ull);
    bad = 0;
    for (uint64_t x = 0; x < 2000000; x++) if (udiv10(x) != x / 10) MISMATCH("x=%" PRIu64, x);
    for (int i = 0; i < N_RAND; i++) { uint64_t x = rnd_mag(); if (udiv10(x) != x / 10) MISMATCH("x=%" PRIu64, x); }
    for (uint64_t k = 0; k < 64; k++)                     /* 2^k 근처 경계 */
        for (uint64_t d = 0; d < 3; d++) { uint64_t x = (1ull << k) - 1 + d; if (udiv10(x) != x / 10) MISMATCH("x=%" PRIu64, x); }
    CHECK(report(bad, "udiv10 exhaustive 0..2e6 + random + 2^k 경계"));

    /* 연습 3: safe_sdiv — 에러 경로는 C 로 나누지 않는다 */
    int64_t q = 111, r = 222;
    CHECK(safe_sdiv(5, 0, &q, &r) == -1 && q == 111 && r == 222);
    CHECK(safe_sdiv(INT64_MIN, -1, &q, &r) == -2 && q == 111 && r == 222);
    CHECK(safe_sdiv(INT64_MIN, 1, &q, &r) == 0 && q == INT64_MIN && r == 0);
    CHECK(safe_sdiv(-7, -1, &q, &r) == 0 && q == 7 && r == 0);
    CHECK(safe_sdiv(-7, 2, &q, &r) == 0 && q == -3 && r == -1);

    /* 연습 4: 곱셈 오버플로 검출 vs __builtin_mul_overflow */
    uint64_t ur; int64_t sr;
    CHECK(mul_overflow_u64(1ull << 32, 1ull << 32, &ur) == 1 && ur == 0);
    CHECK(mul_overflow_u64(0xFFFFFFFFull, 0x100000001ull, &ur) == 0 && ur == UINT64_MAX);
    CHECK(mul_overflow_s64(INT64_MIN, -1, &sr) == 1 && sr == INT64_MIN);
    CHECK(mul_overflow_s64(INT64_MIN, 1, &sr) == 0 && sr == INT64_MIN);
    CHECK(mul_overflow_s64(-1, -1, &sr) == 0 && sr == 1);
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = rnd_mag(), b = rnd_mag(), ue; int64_t sa = rnd_s(), sb = rnd_s(), se;
        int uo = __builtin_mul_overflow(a, b, &ue), so = __builtin_mul_overflow(sa, sb, &se);
        if (mul_overflow_u64(a, b, &ur) != uo || ur != ue || mul_overflow_s64(sa, sb, &sr) != so || sr != se)
            MISMATCH("a=%" PRIu64 " b=%" PRIu64 " sa=%" PRId64 " sb=%" PRId64, a, b, sa, sb);
    }
    CHECK(report(bad, "mul_overflow_u64/s64 vs __builtin_mul_overflow"));

    /* 연습 5: 반올림 Q32.32 곱 (ADDS/ADC 128비트 덧셈) */
    CHECK(q32_mul_round(1, 0x80000000ull) == 1);           /* 정확히 0.5 ulp → 올림 */
    CHECK(q32_mul_round(1, 0x7FFFFFFFull) == 0);
    CHECK(q32_mul_round(UINT64_MAX, UINT64_MAX) == 0xFFFFFFFE00000000ull);
    CHECK(q32_mul_round(UINT64_MAX, 0x80000000ull) == 0x8000000000000000ull);  /* lo+2^31 이 carry → hi (ADC 없으면 0x7FFFFFFF00000000) */
    bad = 0;
    for (int i = 0; i < N_RAND; i++) {
        uint64_t a = rnd_mag(), b = rnd_mag();
        if (q32_mul_round(a, b) != (uint64_t)(((u128)a * b + (1u << 31)) >> 32)) MISMATCH("a=%016" PRIx64 " b=%016" PRIx64, a, b);
    }
    CHECK(report(bad, "q32_mul_round vs u128"));

    /* 연습 6: UMADDL 누적 (전력계: 전압 x 전류 샘플 합) */
    static uint32_t va[300], ia[300];
    CHECK(dot_u32(va, ia, 0) == 0);
    va[0] = ia[0] = 0xFFFFFFFFu; va[1] = ia[1] = 0xFFFFFFFFu;
    CHECK(dot_u32(va, ia, 2) == 0xFFFFFFFC00000002ull);    /* 2 * (2^32-1)^2 — 32비트 곱이면 2 */
    bad = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = (size_t)(rnd() % 300);
        uint64_t ref = 0;
        for (size_t i = 0; i < n; i++) { va[i] = (uint32_t)rnd(); ia[i] = (uint32_t)rnd(); ref += (uint64_t)va[i] * ia[i]; }
        if (dot_u32(va, ia, n) != ref) MISMATCH("n=%zu", n);
    }
    CHECK(report(bad, "dot_u32 vs C (n 0..299)"));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
