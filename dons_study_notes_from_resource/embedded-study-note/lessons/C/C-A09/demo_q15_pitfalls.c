/* C-A09 demo: Q15 코드에서 흔한 함정 6개 + 오차 통계 (일부러 UB/구현 정의를 포함 — 하네스와 별개)
 *   cc -std=c11 -O0 demo_q15_pitfalls.c -o dp0 && ./dp0
 *   cc -std=c11 -O2 demo_q15_pitfalls.c -o dp2 && ./dp2
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_q15_pitfalls.c -o dpu && ./dpu
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef int16_t q15_t;
static volatile double  v_one = 1.0, v_tiny = 0x1.fffffffffffffp-17;   /* volatile: 상수 접기 방지 */
static volatile int16_t v_min = INT16_MIN, v_a = -113, v_b = 145;
static volatile uint16_t v_raw = 100;                                   /* ADC 값 (중앙 2048 보다 작음) */

static q15_t mul_naive(q15_t a, q15_t b) { return (q15_t)(((int32_t)a * b + 0x4000) >> 15); }
static q15_t mul_div(q15_t a, q15_t b)   { return (q15_t)(((int32_t)a * b + 0x4000) / 32768); }
static q15_t from_double_cast(double x)  { return (q15_t)(x * 32768.0); }        /* 반올림·포화 없음 */
static q15_t from_double_plus_half(double x) {                                   /* 고전적 +0.5 */
    double s = x * 32768.0;
    return (q15_t)(s >= 0 ? (int32_t)(s + 0.5) : -(int32_t)(-s + 0.5));
}
static q15_t adc_shift(uint16_t raw)     { return (q15_t)(((int32_t)raw - 2048) << 4); }
static int32_t mix_acc32(q15_t m, q15_t gm, q15_t t, q15_t gt) {
    return (int32_t)m * gm + (int32_t)t * gt;                                    /* 2^30 + 2^30 */
}

static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t rng(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static q15_t rq(void) { return (q15_t)((int32_t)(rng() & 0xFFFFu) - 32768); }
static int32_t fl15(int64_t p) { int64_t q = p / 32768; if (p % 32768 < 0) q--; return (int32_t)q; }
static int32_t sat(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }

int main(void) {
    printf("1) naive  (-1)*(-1)            = %d\n", mul_naive(v_min, v_min));
    printf("2) div    (-113)*145 (-0.50003 LSB) = %d   (floor 버전은 -1)\n", mul_div(v_a, v_b));
    printf("3) cast   q15(1.0)             = %d\n", from_double_cast(v_one));
    printf("4) +0.5   q15(0.5 LSB - 2^-69) = %d   (정답 0)\n", from_double_plus_half(v_tiny));
    printf("5) adc    (100-2048)<<4        = %d\n", adc_shift(v_raw));
    printf("6) acc32  (-1)(-1)+(-1)(-1)    = %d   (참값 2^31 = 2147483648)\n",
           mix_acc32(v_min, v_min, v_min, v_min));

    /* 오차 통계: 100만 쌍. 단위 = LSB(2^-15) */
    const int N = 1000000;
    double s_tr = 0, s_rd = 0, w2 = 0, w1 = 0;
    for (int i = 0; i < N; i++) {
        q15_t a = rq(), b = rq();
        double ex = (double)a * b / 32768.0;
        s_tr += fl15((int64_t)a * b) - ex;                 /* 절단 */
        s_rd += sat(fl15((int64_t)a * b + 0x4000)) - ex;   /* 반올림 (+포화) */
        q15_t m = rq(), t = rq(), gm = rq(), gt = rq();
        double exm = ((double)m * gm + (double)t * gt) / 32768.0;
        if (exm > -32768.0 && exm < 32767.0) {
            double y1 = fl15((int64_t)m * gm + (int64_t)t * gt + 0x4000);
            double y2 = sat(sat(fl15((int64_t)m * gm + 0x4000)) + sat(fl15((int64_t)t * gt + 0x4000)));
            if (fabs(y1 - exm) > w1) w1 = fabs(y1 - exm);
            if (fabs(y2 - exm) > w2) w2 = fabs(y2 - exm);
        }
    }
    printf("7) mean err: trunc %+.4f LSB, round %+.4f LSB  (N=%d)\n", s_tr / N, s_rd / N, N);
    printf("8) mix worst err: 1 rounding %.4f LSB, 2 roundings %.4f LSB\n", w1, w2);
    return 0;
}
