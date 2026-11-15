/* ARM-B11 추가 연습 — 한 번에 검증 (asm 답 = practice.S, 연습 5 의 C 답 = 이 파일)
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_abi.S -o pr && ./pr
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint32_t f2i_flags(float x, int32_t *out);
extern float    fmin_arm(float a, float b);
extern float    fminnm_arm(float a, float b);
extern uint32_t duty_to_ccr(float duty, uint32_t arr);
extern float    fabs_gpr(float x);
extern float    copysign_gpr(float mag, float sgn);
extern int32_t  fcvtzs_hw(float x);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static float  rnd_f(float lo, float hi) { return lo + (hi - lo) * (float)(xs() >> 40) * 0x1p-24f; }
static float  bits_f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

enum { IOC = 1u << 0, IXC = 1u << 4 };

/* 연습 5: FCVTZS (float → int32, 0 쪽 자름, 포화, NaN → 0) 의 소프트웨어 모델. FP 명령 없이 비트만으로. */
static int32_t soft_fcvtzs(uint32_t u) {
    uint32_t sign = u >> 31, exp = (u >> 23) & 0xFFu, frac = u & 0x7FFFFFu;
    if (exp == 0xFFu) return frac ? 0 : (sign ? INT32_MIN : INT32_MAX);   /* NaN → 0, ±Inf → 포화 */
    if (exp < 127u) return 0;                          /* |x| < 1: ±0, 비정규수 포함 */
    int e = (int)exp - 127;                            /* x = 1.frac × 2^e,  e ≥ 0 */
    if (e >= 31) return sign ? INT32_MIN : INT32_MAX;  /* |x| ≥ 2^31 (−2^31 은 정확히 MIN) */
    uint32_t m = frac | 0x800000u;                     /* 숨은 1 붙인 24비트 유효숫자 */
    uint32_t mag = e >= 23 ? m << (e - 23) : m >> (23 - e);   /* 소수부 버림 = 0 쪽 자름 */
    return sign ? -(int32_t)mag : (int32_t)mag;        /* mag < 2^31 → 오버플로 없음 */
}

int main(void) {
    int bad;
    int32_t r = 123;

    /* 연습 1: FPSR 누적 예외 */
    CHECK(f2i_flags(2.0f, &r) == 0 && r == 2);
    CHECK(f2i_flags(2.5f, &r) == IXC && r == 2);           /* 소수부 버림 = inexact */
    CHECK(f2i_flags(NAN, &r) == IOC && r == 0);            /* NaN = invalid */
    CHECK(f2i_flags(3e9f, &r) == IOC && r == INT32_MAX);   /* 포화 = invalid (inexact 아님) */
    CHECK(f2i_flags(-INFINITY, &r) == IOC && r == INT32_MIN);
    CHECK(f2i_flags(-0x1p31f, &r) == 0 && r == INT32_MIN); /* 정확히 표현됨 → 플래그 없음 */
    CHECK(f2i_flags(0x1p-149f, &r) == IXC && r == 0);      /* 비정규수 (FPCR.FZ = 0 가정) */
    CHECK(f2i_flags(-0.0f, &r) == 0 && r == 0);

    /* 연습 2: FMIN vs FMINNM */
    CHECK(isnan(fmin_arm(NAN, 1.0f)) && isnan(fmin_arm(1.0f, NAN)));
    CHECK(fminnm_arm(NAN, 1.0f) == 1.0f && fminnm_arm(1.0f, NAN) == 1.0f);
    CHECK(isnan(fminnm_arm(NAN, NAN)));
    CHECK(signbit(fmin_arm(0.0f, -0.0f)) && signbit(fmin_arm(-0.0f, 0.0f)));    /* −0 < +0 취급 */
    CHECK(signbit(fminnm_arm(0.0f, -0.0f)) && signbit(fminnm_arm(-0.0f, 0.0f)));
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        float a = (i % 7 == 0) ? NAN : rnd_f(-1e3f, 1e3f), b = (i % 11 == 0) ? NAN : rnd_f(-1e3f, 1e3f);
        float m = fminnm_arm(a, b), c = fminf(a, b);
        if (!((isnan(m) && isnan(c)) || m == c)) bad++;
        float n = fmin_arm(a, b);
        if ((isnan(a) || isnan(b)) ? !isnan(n) : n != c) bad++;
    }
    CHECK(bad == 0);

    /* 연습 3: 타이머 비교값 (CCR), ARR = 999 → 1 kHz PWM 같은 설정 */
    CHECK(duty_to_ccr(0.5f, 999) == 500);                  /* 499.5 → ties away → 500 */
    CHECK(duty_to_ccr(0.25f, 1000) == 250);
    CHECK(duty_to_ccr(-0.2f, 999) == 0);                   /* 음수 → 0 (fcvtau 포화) */
    CHECK(duty_to_ccr(1.7f, 999) == 999);                  /* 넘치면 ARR 로 */
    CHECK(duty_to_ccr(NAN, 999) == 0);
    CHECK(duty_to_ccr(INFINITY, 65535) == 65535);
    bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint32_t arr = 1u + (uint32_t)(xs() % 65535u);
        float duty = rnd_f(-0.5f, 1.5f);
        float p = duty * (float)arr;
        uint32_t ref;
        if (p < 0.5f) ref = 0;
        else if (p >= (float)arr) ref = arr;
        else { ref = (uint32_t)roundf(p); if (ref > arr) ref = arr; }
        if (duty_to_ccr(duty, arr) != ref) bad++;
    }
    CHECK(bad == 0);

    /* 연습 4: 부호 비트 조작 (NaN 포함 임의 비트 패턴, 비트 단위 비교) */
    CHECK(f_bits(fabs_gpr(-0.0f)) == 0u);
    CHECK(copysign_gpr(3.0f, -0.0f) == -3.0f);
    bad = 0;
    for (int i = 0; i < 1000000; i++) {
        uint64_t rr = xs();
        float a = bits_f((uint32_t)rr), b = bits_f((uint32_t)(rr >> 32));
        if (f_bits(fabs_gpr(a)) != f_bits(fabsf(a))) bad++;
        if (f_bits(copysign_gpr(a, b)) != f_bits(copysignf(a, b))) bad++;
    }
    CHECK(bad == 0);

    /* 연습 5: soft_fcvtzs vs 하드웨어 — 2^31 근처 지수(156~158) 전수 + 전체 공간 띄엄띄엄 */
    bad = 0;
    for (uint32_t s = 0; s < 2; s++)
        for (uint32_t e = 156; e <= 158; e++)
            for (uint32_t f = 0; f < (1u << 23); f++) {
                uint32_t u = (s << 31) | (e << 23) | f;
                if (soft_fcvtzs(u) != fcvtzs_hw(bits_f(u))) bad++;
            }
    CHECK(bad == 0);
    bad = 0;
    for (uint64_t u = 0; u <= 0xFFFFFFFFull; u += 251)          /* 약 1710만 개, NaN/Inf/비정규 포함 */
        if (soft_fcvtzs((uint32_t)u) != fcvtzs_hw(bits_f((uint32_t)u))) bad++;
    CHECK(bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
