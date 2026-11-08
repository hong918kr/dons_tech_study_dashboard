/* C-A09 추가 연습 — 한 번에 검증
 *   1) q31_mul_sat     : Q31 × Q31 (64비트 곱, 반올림, -1×-1 포화)
 *   2) q15_mul_rne     : 동점을 짝수 쪽으로 (round half to even, convergent rounding)
 *   3) q15_div_sat     : Q15 나눗셈 (|num| >= |den| 이면 포화, den == 0 처리)
 *   4) adc_to_q15      : 8~16비트 offset-binary ADC → Q15 (일반화)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef int16_t q15_t;
typedef int32_t q31_t;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0x9E3779B9u;
static uint32_t rng(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static int64_t floor_div64(int64_t x, int64_t d) {      /* d > 0 */
    int64_t q = x / d;
    if (x % d < 0) q--;
    return q;
}

/* ---- 1) Q31 곱: Q62 곱은 int64 에 들어간다 (|p| <= 2^62) ---- */
static q31_t q31_mul_sat(q31_t a, q31_t b) {
    int64_t p = (int64_t)a * b;                           /* Q62 */
    int64_t r = floor_div64(p + ((int64_t)1 << 30), (int64_t)1 << 31);
    if (r > INT32_MAX) return INT32_MAX;                  /* 오직 -1 × -1 */
    return (q31_t)r;                                      /* 아래쪽은 넘칠 수 없다 */
}

/* ---- 2) round half to even ---- */
static q15_t q15_mul_rne(q15_t a, q15_t b) {
    int32_t p = (int32_t)a * b;
    int32_t r = (int32_t)floor_div64(p, 32768);
    int32_t rem = p - r * 32768;                          /* 0 .. 32767 */
    if (rem > 16384 || (rem == 16384 && (r & 1)))          /* r & 1: 음수도 2의 보수라 홀짝 판정 OK */
        r++;
    return (q15_t)(r > INT16_MAX ? INT16_MAX : r);
}
static q15_t q15_mul_hup(q15_t a, q15_t b) {               /* 본문의 round half up (비교용) */
    int32_t r = (int32_t)floor_div64((int32_t)a * b + 0x4000, 32768);
    return (q15_t)(r > INT16_MAX ? INT16_MAX : r);
}

/* ---- 3) Q15 나눗셈: 결과 = num/den, 가장 가까운 값(동점은 0 에서 먼 쪽), 포화 ---- */
static q15_t q15_div_sat(q15_t num, q15_t den) {
    if (den == 0) return num > 0 ? INT16_MAX : num < 0 ? INT16_MIN : 0;
    int32_t n = (int32_t)num * 32768;                     /* |n| <= 2^30 */
    int32_t q = n / den, r = n % den;                     /* 0 쪽으로 버림 */
    int32_t ar = r < 0 ? -r : r, ad = den < 0 ? -den : den;
    if (2 * ar >= ad) q += ((n < 0) != (den < 0)) ? -1 : 1;
    return (q15_t)(q > INT16_MAX ? INT16_MAX : q < INT16_MIN ? INT16_MIN : q);
}

/* ---- 4) bits 비트 offset-binary ADC → Q15 (8 <= bits <= 16) ---- */
static q15_t adc_to_q15(uint16_t raw, unsigned bits) {
    uint32_t mask = (1u << bits) - 1u;
    int32_t v = (int32_t)(raw & mask) - (int32_t)(1u << (bits - 1));   /* -2^(b-1) .. 2^(b-1)-1 */
    return (q15_t)(v * (int32_t)(1u << (16 - bits)));                   /* << 대신 곱 */
}

int main(void) {
    /* 1) Q31 */
    CHECK(q31_mul_sat(INT32_MIN, INT32_MIN) == INT32_MAX);
    CHECK(q31_mul_sat(1 << 30, 1 << 30) == 1 << 29);                   /* 0.5 × 0.5 */
    CHECK(q31_mul_sat(INT32_MIN, INT32_MAX) == -INT32_MAX);
    CHECK(q31_mul_sat(3, 1 << 30) == 2 && q31_mul_sat(-3, 1 << 30) == -1);   /* ±1.5 LSB 동점 → 위로 */
    {
        int bad = 0;
        for (int i = 0; i < 100000; i++) {
            q31_t a = (q31_t)(int64_t)((int64_t)rng() - 2147483648LL);
            q31_t b = (q31_t)(int64_t)((int64_t)rng() - 2147483648LL);
            if (a == INT32_MIN && b == INT32_MIN) continue;
            int64_t p = (int64_t)a * b + ((int64_t)1 << 30), r = q31_mul_sat(a, b);
            /* floor 정의 그대로: r*2^31 <= p < (r+1)*2^31 (모두 int64 안) */
            if (!(r * ((int64_t)1 << 31) <= p && p < (r + 1) * ((int64_t)1 << 31))) bad++;
        }
        CHECK(bad == 0);
    }

    /* 2) RNE */
    CHECK(q15_mul_rne(3, 16384) == 2 && q15_mul_rne(5, 16384) == 2);    /* 1.5 → 2, 2.5 → 2 */
    CHECK(q15_mul_rne(1, 16384) == 0 && q15_mul_rne(-1, 16384) == 0);   /* ±0.5 → 0 */
    CHECK(q15_mul_rne(-3, 16384) == -2 && q15_mul_rne(-5, 16384) == -2);
    CHECK(q15_mul_rne(INT16_MIN, INT16_MIN) == INT16_MAX);
    {   /* 동점만 모아서: half-up 은 +0.5 LSB 치우침, RNE 는 0 */
        long s_up = 0, s_rne = 0, n = 0;                                 /* 단위: 0.5 LSB */
        for (int a = -32767; a <= 32767; a += 2) {                       /* a 홀수 → a*16384 는 동점 */
            s_up += 2L * q15_mul_hup((q15_t)a, 16384) - a;
            s_rne += 2L * q15_mul_rne((q15_t)a, 16384) - a;
            n++;
        }
        CHECK(s_up == n && s_rne == 0);
    }
    {   /* 동점이 아니면 두 방식은 같다 */
        int diff = 0;
        for (int i = 0; i < 100000; i++) {
            q15_t a = (q15_t)((int32_t)(rng() & 0xFFFFu) - 32768), b = (q15_t)((int32_t)(rng() & 0xFFFFu) - 32768);
            bool tie = (((int32_t)a * b) & 0x7FFF) == 0x4000;
            if (!tie && q15_mul_rne(a, b) != q15_mul_hup(a, b)) diff++;
        }
        CHECK(diff == 0);
    }

    /* 3) 나눗셈 */
    CHECK(q15_div_sat(8192, 16384) == 16384);                            /* 0.25 / 0.5 = 0.5 */
    CHECK(q15_div_sat(1, 3) == 10923 && q15_div_sat(-1, 3) == -10923 && q15_div_sat(1, -3) == -10923);
    CHECK(q15_div_sat(-16384, 16384) == INT16_MIN);                      /* -1.0 은 표현 가능 */
    CHECK(q15_div_sat(16384, 16384) == INT16_MAX);                       /* +1.0 은 포화 */
    CHECK(q15_div_sat(INT16_MIN, INT16_MIN) == INT16_MAX && q15_div_sat(INT16_MIN, -1) == INT16_MAX);
    CHECK(q15_div_sat(5, 0) == INT16_MAX && q15_div_sat(-5, 0) == INT16_MIN && q15_div_sat(0, 0) == 0);
    CHECK(q15_div_sat(1, 2) == 16384 && q15_div_sat(1, 65536 / 2 - 1) == 1);   /* 32768/32767 → 1.00003 */

    /* 4) ADC 일반화 */
    CHECK(adc_to_q15(0, 12) == INT16_MIN && adc_to_q15(2048, 12) == 0 && adc_to_q15(4095, 12) == 32752);
    CHECK(adc_to_q15(0, 8) == INT16_MIN && adc_to_q15(255, 8) == 32512 && adc_to_q15(0x1FF, 8) == 32512);
    CHECK(adc_to_q15(0, 16) == INT16_MIN && adc_to_q15(65535, 16) == INT16_MAX && adc_to_q15(32768, 16) == 0);
    CHECK(adc_to_q15(512, 10) == 0 && adc_to_q15(1023, 10) == 32704);
    {
        int bad = 0;
        for (unsigned b = 8; b <= 16; b++)
            for (int i = 0; i < 2000; i++) {
                uint16_t raw = (uint16_t)rng();
                double mid = (double)(1u << (b - 1));
                double want = ((double)(raw & ((1u << b) - 1u)) - mid) / mid;   /* 정확 */
                if (adc_to_q15(raw, b) / 32768.0 != want) bad++;
            }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
