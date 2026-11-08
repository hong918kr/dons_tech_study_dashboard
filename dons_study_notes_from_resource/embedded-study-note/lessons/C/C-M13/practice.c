/* C-M13 추가 연습 — 한 번에 검증
 * 모든 비트 이동은 memcpy 또는 (C 에서 허용되는) union 으로만 한다.
 */
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t float_bits(float f)     { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static float    bits_to_float(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

/* 연습 1: 부호 비트만 만지는 연산 — 산술 없이, NaN 페이로드도 보존 */
static float fp_abs(float x)  { return bits_to_float(float_bits(x) & 0x7FFFFFFFu); }
static float fp_neg(float x)  { return bits_to_float(float_bits(x) ^ 0x80000000u); }
static float fp_copysign(float mag, float sgn) {
    return bits_to_float((float_bits(mag) & 0x7FFFFFFFu) | (float_bits(sgn) & 0x80000000u));
}

/* 연습 2: 두 float 사이의 ULP 거리. 부호-크기(sign-magnitude) → 순서 있는 정수로 매핑 */
static int64_t fp_order_key(float x) {
    uint32_t u = float_bits(x);
    return (u & 0x80000000u) ? -(int64_t)(u & 0x7FFFFFFFu) : (int64_t)u;   /* -0 과 +0 → 0 */
}
static uint32_t ulp_distance(float a, float b) {          /* 둘 다 NaN 이 아니라고 가정 */
    int64_t d = fp_order_key(a) - fp_order_key(b);
    return (uint32_t)(d < 0 ? -d : d);
}

/* 연습 3: nextafterf(x, +INFINITY) 를 비트로 */
static float fp_next_up(float x) {
    uint32_t u = float_bits(x);
    if ((u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu)) return x;   /* NaN */
    if (u == 0x7F800000u) return x;                                         /* +inf */
    if (u == 0x80000000u) return bits_to_float(0x00000001u);                /* -0 → 최소 서브노멀 */
    return bits_to_float((u & 0x80000000u) ? u - 1u : u + 1u);              /* 음수는 크기를 줄인다 */
}

/* 연습 4: float32 → bfloat16 (상위 16비트), round-to-nearest-even. NaN 은 quiet NaN 유지 */
static uint16_t f32_to_bf16(float x) {
    uint32_t u = float_bits(x);
    if ((u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu))
        return (uint16_t)((u >> 16) | 0x0040u);       /* 잘라서 inf 가 되지 않게 quiet 비트 세움 */
    u += 0x7FFFu + ((u >> 16) & 1u);                  /* 반올림, 동점이면 짝수(LSB 0) 쪽 */
    return (uint16_t)(u >> 16);
}
static float bf16_to_f32(uint16_t h) { return bits_to_float((uint32_t)h << 16); }

/* 연습 5: union 복합 리터럴 — C 에서는 정의된 동작(6.5.2.3 각주 95), C++ 에서는 UB */
#define F2U(x) (((union { float f; uint32_t u; }){ .f = (x) }).u)
#define U2F(x) (((union { float f; uint32_t u; }){ .u = (x) }).f)

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static bool is_nan_bits(uint32_t u) { return (u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu); }

int main(void) {
    /* 연습 1 */
    CHECK(float_bits(fp_abs(-0.0f)) == 0x00000000u);
    CHECK(float_bits(fp_neg(0.0f)) == 0x80000000u);
    CHECK(float_bits(fp_abs(bits_to_float(0xFFC12345u))) == 0x7FC12345u);   /* NaN 페이로드 보존 */
    CHECK(fp_copysign(2.5f, -0.0f) == -2.5f);
    {
        uint32_t s = 7u; int ok = 0;
        for (int k = 0; k < 10000; k++) {
            float a = bits_to_float(xorshift32(&s)), b = bits_to_float(xorshift32(&s));
            ok += float_bits(fp_abs(a)) == float_bits(fabsf(a))
               && float_bits(fp_copysign(a, b)) == float_bits(copysignf(a, b));
        }
        CHECK(ok == 10000);
    }

    /* 연습 2 */
    CHECK(ulp_distance(1.0f, nextafterf(1.0f, 2.0f)) == 1u);
    CHECK(ulp_distance(-0.0f, 0.0f) == 0u);
    CHECK(ulp_distance(-FLT_TRUE_MIN, FLT_TRUE_MIN) == 2u);
    CHECK(ulp_distance(FLT_MAX, INFINITY) == 1u);
    {
        float acc = 0.0f;
        for (int i = 0; i < 10; i++) acc += 0.1f;
        CHECK(acc != 1.0f && ulp_distance(acc, 1.0f) == 1u);   /* == 는 실패, "1 ULP 이내" 는 통과 */
    }

    /* 연습 3 */
    CHECK(float_bits(fp_next_up(-0.0f)) == 0x00000001u);
    CHECK(float_bits(fp_next_up(-FLT_TRUE_MIN)) == 0x80000000u);
    CHECK(fp_next_up(FLT_MAX) == INFINITY);
    CHECK(fp_next_up(-INFINITY) == -FLT_MAX);
    {
        uint32_t s = 99u; int ok = 0, n = 0;
        while (n < 10000) {
            uint32_t u = xorshift32(&s);
            if (is_nan_bits(u)) continue;
            n++;
            float x = bits_to_float(u);
            ok += float_bits(fp_next_up(x)) == float_bits(nextafterf(x, INFINITY));
        }
        CHECK(ok == 10000);
    }

    /* 연습 4 */
    CHECK(f32_to_bf16(1.0f) == 0x3F80u);
    CHECK(f32_to_bf16(bits_to_float(0x40490FDBu)) == 0x4049u);   /* pi: 아래 절반 → 버림 */
    CHECK(f32_to_bf16(bits_to_float(0x3F808000u)) == 0x3F80u);   /* 정확히 절반, LSB 0 → 그대로 */
    CHECK(f32_to_bf16(bits_to_float(0x3F818000u)) == 0x3F82u);   /* 정확히 절반, LSB 1 → 올림 */
    CHECK(f32_to_bf16(bits_to_float(0x3F80C000u)) == 0x3F81u);   /* 절반 초과 → 올림 */
    CHECK(f32_to_bf16(FLT_MAX) == 0x7F80u);                      /* 올림하다 inf (RNE 의 정상 동작) */
    CHECK(f32_to_bf16(bits_to_float(0x7F800001u)) == 0x7FC0u);   /* sNaN → inf 가 아니라 qNaN */
    CHECK(bf16_to_f32(0xC020u) == -2.5f);

    /* 연습 5 */
    CHECK(F2U(1.0f) == 0x3F800000u);
    CHECK(U2F(0xC0200000u) == -2.5f);
    {
        uint32_t s = 5u; int ok = 0;
        for (int k = 0; k < 1000; k++) {
            uint32_t u = xorshift32(&s);
            if (is_nan_bits(u)) { ok++; continue; }
            ok += F2U(U2F(u)) == u && F2U(bits_to_float(u)) == float_bits(bits_to_float(u));
        }
        CHECK(ok == 1000);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
