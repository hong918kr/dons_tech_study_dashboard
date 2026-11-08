/* C-A09 고정소수점 맛보기: Q15 곱셈과 포화 연산
 *
 * Q15 (= Q1.15, Q0.15): int16_t q 가 실수 q / 2^15 를 뜻한다. 범위 [-1, 1 - 2^-15], 1 LSB = 2^-15.
 * 이 파일의 규칙 (테스트가 이 규칙을 그대로 검사한다):
 *   - 반올림: 가장 가까운 값. 동점(정확히 0.5 LSB)은
 *       double → Q15 변환: 0 에서 먼 쪽 (lround 와 같음)
 *       곱셈/믹스: +무한대 쪽 (0x4000 을 더하고 floor) — DSP 에서 흔한 "round half up"
 *   - 포화: 범위를 넘으면 INT16_MAX / INT16_MIN 에 붙인다. NaN 은 0.
 *   - 구현 정의 동작(음수의 >>, 범위 밖 정수 → int16_t 변환)과 UB(음수의 <<, signed overflow,
 *     범위 밖 실수 → 정수 변환)에 기대지 않는다. 주어진 naive 함수만 예외 (_Static_assert 로 가정 명시).
 *
 * Part C 시나리오: 12비트 ADC(마이크, DMA 원형 버퍼) + Q15 사인 LUT(톤) → 게인·믹스 → 12비트 DAC.
 */
#include <math.h>      /* lround — 테스트 오라클(기준값)에서만 사용 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int16_t q15_t;
#define Q15_MAX INT16_MAX
#define Q15_MIN INT16_MIN

/* ---------------- 주어진 코드 (고치지 않는다) ---------------- */
static double q15_to_double(q15_t q) { return q / 32768.0; }   /* 정확: 2의 거듭제곱으로 나눔 */

/* 절단(truncate) 곱: 반올림 없이 >> 15. 결과는 언제나 아래로(-무한대 쪽) 치우친다. */
static q15_t q15_mul_trunc(q15_t a, q15_t b) {
    int32_t p = (int32_t)a * b;
    int32_t r = p < 0 ? ~(~p >> 15) : p >> 15;                  /* floor(p / 2^15), C-B03 sar32 */
    return (q15_t)(r > Q15_MAX ? Q15_MAX : r);
}

/* naive 곱: 흔히 보는 한 줄. 두 군데가 구현 정의라 가정을 정적으로 못 박는다. */
_Static_assert((-1 >> 1) == -1, "naive: 음수 >> 가 산술 시프트라고 가정 (C11 6.5.7p5 구현 정의)");
_Static_assert((int16_t)32768 == INT16_MIN, "naive: 범위 밖 → int16_t 가 모듈로 2^16 이라고 가정 (6.3.1.3p3)");
static q15_t q15_mul_naive(q15_t a, q15_t b) {
    return (q15_t)(((int32_t)a * b + 0x4000) >> 15);             /* 포화 없음 */
}

/* ---------------- Part A: 변환 · 곱셈 · 포화 덧셈 ---------------- */

/* A1. floor(x / 2^k), 1 <= k <= 30. 음수 >> (구현 정의)도, 0 쪽으로 버리는 / 하나만도 쓰지 않는다. */
static int32_t floor_shr(int32_t x, unsigned k) {
    int32_t d = (int32_t)1 << k;
    int32_t q = x / d;                 /* 6.5.5p6: 0 쪽으로 버림 */
    if (x % d < 0) q--;                /* 음수이고 나머지가 있으면 한 칸 아래 → floor */
    return q;
}

/* A2. int32 → Q15 포화 */
static q15_t sat_q15(int32_t x) {
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return (q15_t)x;
}

/* A3. double → Q15: 가장 가까운 값(동점은 0 에서 먼 쪽), 포화, NaN → 0 */
static q15_t q15_from_double(double x) {
    if (x != x) return 0;                          /* NaN */
    double s = x * 32768.0;                        /* 2^15 배: 정확 */
    if (s >= 32767.5) return Q15_MAX;              /* 변환 '전에' 범위 검사 (6.3.1.4p1) */
    if (s <= -32768.5) return Q15_MIN;
    int32_t t = (int32_t)s;                        /* 0 쪽으로 버림, 이제 범위 안 */
    double frac = s - t;                           /* 정확한 소수부 (|frac| < 1) */
    if (frac >= 0.5) t++;
    else if (frac <= -0.5) t--;
    return sat_q15(t);
}

/* A4. Q15 × Q15 → Q15: 32비트 곱(Q30) + 0x4000, floor >> 15, 포화 */
static q15_t q15_mul(q15_t a, q15_t b) {
    int32_t p = (int32_t)a * b;                    /* 16비트 int 플랫폼에서도 32비트 곱 */
    return sat_q15(floor_shr(p + 0x4000, 15));
}

/* A5. 포화 덧셈 */
static q15_t q15_add_sat(q15_t a, q15_t b) {
    return sat_q15((int32_t)a + b);
}

/* ---------------- Part B: 경계 — -1 의 짝이 없다 ---------------- */
static q15_t q15_sub_sat(q15_t a, q15_t b) {
    return sat_q15((int32_t)a - b);
}
static q15_t q15_neg_sat(q15_t a) {
    return sat_q15(-(int32_t)a);
}
static q15_t q15_abs_sat(q15_t a) {
    return a < 0 ? q15_neg_sat(a) : a;
}

/* ---------------- Part C: ADC → 게인·믹스 → DAC ---------------- */
typedef struct { q15_t g_mic, g_tone; } mix_cfg_t;      /* Q15 게인 */

/* C1. 12비트 오른쪽 정렬 offset-binary ADC 값 → Q15. 위 4비트는 쓰레기일 수 있다. */
static q15_t adc12_to_q15(uint16_t raw) {
    int32_t v = (int32_t)(raw & 0x0FFFu) - 2048;  /* -2048 .. 2047 */
    return (q15_t)(v * 16);                        /* << 4 대신 곱: 음수 << 는 UB (6.5.7p4) */
}

/* C2. Q15 → 12비트 offset-binary DAC 코드 (0..4095), 가장 가까운 값(동점은 위로), 포화 */
static uint16_t q15_to_dac12(q15_t q) {
    uint32_t u = (uint32_t)((int32_t)q + 32768);  /* 0 .. 65535 */
    uint32_t d = (u + 8u) >> 4;                    /* /16 반올림 */
    return (uint16_t)(d > 4095u ? 4095u : d);
}

/* C3. y = mic*g_mic + tone*g_tone. 곱은 32비트, 합은 64비트, 반올림은 '한 번만', 그다음 포화. */
static q15_t mix_sample(q15_t mic, q15_t tone, const mix_cfg_t *cfg, bool *clipped) {
    int32_t p1 = (int32_t)mic * cfg->g_mic;        /* |p| <= 2^30 : int32 OK */
    int32_t p2 = (int32_t)tone * cfg->g_tone;
    int64_t acc = (int64_t)p1 + p2 + 0x4000;       /* 합은 2^31 까지 → int32 면 overflow(UB) */
    int64_t y = acc / 32768;
    if (acc % 32768 < 0) y--;                      /* floor */
    *clipped = (y > Q15_MAX || y < Q15_MIN);
    return (q15_t)(y > Q15_MAX ? Q15_MAX : y < Q15_MIN ? Q15_MIN : y);
}

/* C4. DMA 블록 처리: n 샘플 변환·믹스·DAC 코드화, 포화된 샘플 수를 돌려준다 */
static size_t mix_block(const uint16_t *adc, const q15_t *tone, uint16_t *dac, size_t n,
                        const mix_cfg_t *cfg) {
    size_t clips = 0;
    for (size_t i = 0; i < n; i++) {
        bool c;
        q15_t y = mix_sample(adc12_to_q15(adc[i]), tone[i], cfg, &c);
        clips += c;
        dac[i] = q15_to_dac12(y);
    }
    return clips;
}

/* ---------------- 주어진 비교용: 곱마다 반올림하는 믹스 (반올림 2번) ---------------- */
static q15_t mix_sample_2round(q15_t mic, q15_t tone, const mix_cfg_t *cfg) {
    return q15_add_sat(q15_mul(mic, cfg->g_mic), q15_mul(tone, cfg->g_tone));
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE11u;               /* xorshift32, 고정 시드 → 매번 같은 수열 */
static uint32_t rng(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static q15_t rng_q15(void) { return (q15_t)((int32_t)(rng() & 0xFFFFu) - 32768); }

/* double 오라클 — 모든 중간값이 double 로 정확히 표현되는 범위라 '참값' 으로 쓸 수 있다 */
static long clampl(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }
static q15_t ref_from_double(double x) { return (q15_t)clampl(lround(x * 32768.0), Q15_MIN, Q15_MAX); }
static double ref_mul_exact(q15_t a, q15_t b) { return q15_to_double(a) * q15_to_double(b); }
static q15_t ref_mul(q15_t a, q15_t b) {
    return (q15_t)clampl((long)floor(ref_mul_exact(a, b) * 32768.0 + 0.5), Q15_MIN, Q15_MAX);
}
static double ref_mix_lsb(q15_t m, q15_t t, const mix_cfg_t *c) {   /* 참값, LSB 단위 */
    return ((double)m * c->g_mic + (double)t * c->g_tone) / 32768.0;
}

/* DMA 원형 버퍼 시뮬레이션: HT(절반 완료)·TC(전체 완료) 콜백이 각 절반을 처리 */
#define NBUF 64
static uint16_t g_adc_buf[NBUF], g_dac_buf[NBUF];
static q15_t g_tone_lut[NBUF];
static mix_cfg_t g_cfg;
static size_t g_clips;
static void dma_half_cplt_cb(void) { g_clips += mix_block(&g_adc_buf[0], &g_tone_lut[0], &g_dac_buf[0], NBUF / 2, &g_cfg); }
static void dma_cplt_cb(void) { g_clips += mix_block(&g_adc_buf[NBUF / 2], &g_tone_lut[NBUF / 2], &g_dac_buf[NBUF / 2], NBUF / 2, &g_cfg); }

int main(void) {
    /* ---- 주어진 naive 코드의 버그 재현 (starter 에서도 PASS) ---- */
    CHECK(q15_mul_naive(Q15_MIN, Q15_MIN) == Q15_MIN);   /* -1 × -1 = -1 ?! (32768 이 감겨서) */
    {   /* 절단 곱은 평균 -0.5 LSB 치우침 */
        double sum = 0; g_rng = 0xC0FFEE11u;
        for (int i = 0; i < 200000; i++) { q15_t a = rng_q15(), b = rng_q15();
            sum += q15_to_double(q15_mul_trunc(a, b)) * 32768.0 - ref_mul_exact(a, b) * 32768.0; }
        CHECK(sum / 200000 < -0.45);
    }

    /* ---- Part A ---- */
    CHECK(floor_shr(7, 1) == 3 && floor_shr(-7, 1) == -4 && floor_shr(-8, 1) == -4);
    CHECK(floor_shr(-1, 15) == -1 && floor_shr(-32768, 15) == -1 && floor_shr(-32769, 15) == -2);
    CHECK(floor_shr(INT32_MIN, 30) == -2 && floor_shr(INT32_MAX, 30) == 1);
    CHECK(sat_q15(40000) == Q15_MAX && sat_q15(-40000) == Q15_MIN && sat_q15(-123) == -123);
    CHECK(q15_from_double(0.5) == 16384 && q15_from_double(-0.5) == -16384);
    CHECK(q15_from_double(0.25) == 8192 && q15_from_double(-0.75) == -24576);
    CHECK(q15_from_double(0.1) == 3277);                 /* 3276.8 → 3277 */
    CHECK(q15_mul(16384, 16384) == 8192);                /* 0.5 × 0.5 = 0.25 */
    CHECK(q15_mul(16384, -16384) == -8192);
    CHECK(q15_mul(Q15_MAX, Q15_MAX) == 32766);           /* (1-ε)^2 = 1 - 2ε + ε^2 → 32766 */
    CHECK(q15_mul(3, 16384) == 2);                       /* 1.5 LSB → 동점 → 위로 2 */
    CHECK(q15_mul(-3, 16384) == -1);                     /* -1.5 LSB → 동점 → 위로 -1 */
    CHECK(q15_add_sat(16384, 8192) == 24576);
    CHECK(q15_add_sat(30000, 10000) == Q15_MAX && q15_add_sat(-30000, -10000) == Q15_MIN);
    {   /* 무작위 20만 쌍: 오라클과 비트 단위 일치, 오차 <= 0.5 LSB (-1×-1 제외), 평균 치우침 ~0 */
        int mism = 0, big = 0; double sum = 0, worst = 0; g_rng = 0xC0FFEE11u;
        for (int i = 0; i < 200000; i++) {
            q15_t a = rng_q15(), b = rng_q15(), r = q15_mul(a, b);
            if (r != ref_mul(a, b)) mism++;
            double e = q15_to_double(r) * 32768.0 - ref_mul_exact(a, b) * 32768.0;   /* LSB 단위 */
            sum += e;
            if (fabs(e) > worst) worst = fabs(e);
            if (fabs(e) > 0.5 && !(a == Q15_MIN && b == Q15_MIN)) big++;
        }
        CHECK(mism == 0);
        CHECK(big == 0 && worst <= 0.5);
        CHECK(fabs(sum / 200000) < 0.01);
    }
    {   /* double → Q15 무작위 10만 개: lround 오라클과 일치 */
        int mism = 0; g_rng = 0x2468ACE1u;
        for (int i = 0; i < 100000; i++) {
            double x = ((double)rng() / 4294967296.0) * 2.2 - 1.1;     /* [-1.1, 1.1) */
            if (q15_from_double(x) != ref_from_double(x)) mism++;
        }
        CHECK(mism == 0);
    }

    /* ---- Part B: 경계 ---- */
    CHECK(q15_mul(Q15_MIN, Q15_MIN) == Q15_MAX);         /* -1 × -1 = +1 → 표현 불가 → 포화 */
    CHECK(q15_mul(Q15_MIN, Q15_MAX) == -32767);          /* -1 × (1-ε) */
    CHECK(q15_mul(Q15_MIN, 1) == -1 && q15_mul(Q15_MAX, 1) == 1);
    CHECK(q15_mul(-113, 145) == -1);                     /* p = -16385 = -0.50003 LSB → -1 ('/' 만 쓰면 0) */
    CHECK(q15_mul(-128, 128) == 0);                      /* p = -16384 = 정확히 -0.5 LSB → 위로 0 */
    CHECK(q15_from_double(1.0) == Q15_MAX && q15_from_double(-1.0) == Q15_MIN);
    CHECK(q15_from_double(1e9) == Q15_MAX && q15_from_double(-1e9) == Q15_MIN);
    CHECK(q15_from_double(INFINITY) == Q15_MAX && q15_from_double(-INFINITY) == Q15_MIN);
    CHECK(q15_from_double(NAN) == 0 && q15_from_double(-0.0) == 0);
    CHECK(q15_from_double(0x1p-16) == 1 && q15_from_double(-0x1p-16) == -1);   /* 정확히 0.5 LSB */
    CHECK(q15_from_double(0x1.fffffffffffffp-17) == 0);  /* 0.5 LSB 바로 아래: (int)(s + 0.5) 면 1 */
    CHECK(q15_from_double(32767.5 / 32768) == Q15_MAX && q15_from_double(-32768.5 / 32768) == Q15_MIN);
    CHECK(q15_from_double(32767.25 / 32768) == Q15_MAX && q15_from_double(-32767.75 / 32768) == Q15_MIN);
    CHECK(q15_sub_sat(0, Q15_MIN) == Q15_MAX && q15_sub_sat(Q15_MIN, 1) == Q15_MIN);
    CHECK(q15_sub_sat(100, 300) == -200);
    CHECK(q15_neg_sat(Q15_MIN) == Q15_MAX && q15_neg_sat(Q15_MAX) == -32767 && q15_neg_sat(0) == 0);
    CHECK(q15_abs_sat(Q15_MIN) == Q15_MAX && q15_abs_sat(-5) == 5 && q15_abs_sat(7) == 7);
    {   /* add/sub 전수 검사 대신 무작위 10만 쌍: 64비트 기준과 일치 */
        int mism = 0; g_rng = 0x13579BDFu;
        for (int i = 0; i < 100000; i++) {
            q15_t a = rng_q15(), b = rng_q15();
            if (q15_add_sat(a, b) != (q15_t)clampl((long)a + b, Q15_MIN, Q15_MAX)) mism++;
            if (q15_sub_sat(a, b) != (q15_t)clampl((long)a - b, Q15_MIN, Q15_MAX)) mism++;
        }
        CHECK(mism == 0);
    }

    /* ---- Part C: ADC → 믹스 → DAC ---- */
    CHECK(adc12_to_q15(0) == Q15_MIN && adc12_to_q15(2048) == 0 && adc12_to_q15(4095) == 32752);
    CHECK(adc12_to_q15(1) == -32752 && adc12_to_q15(0xF800u | 2048u) == 0);   /* 위 4비트 무시 */
    CHECK(q15_to_dac12(Q15_MIN) == 0 && q15_to_dac12(0) == 2048);
    CHECK(q15_to_dac12(Q15_MAX) == 4095 && q15_to_dac12(32760) == 4095);   /* 4096 → 포화 */
    CHECK(q15_to_dac12(7) == 2048 && q15_to_dac12(8) == 2049);   /* 2048.4375 / 2048.5(동점→위) */
    {
        mix_cfg_t c = { Q15_MIN, Q15_MIN };                     /* 게인 -1, -1 */
        bool clip = false;
        CHECK(mix_sample(Q15_MIN, Q15_MIN, &c, &clip) == Q15_MAX && clip);   /* (+1)+(+1) = 2 → 포화 */
        c.g_mic = 16384; c.g_tone = 16384;                      /* 0.5, 0.5 */
        CHECK(mix_sample(Q15_MAX, Q15_MAX, &c, &clip) == 32767 && !clip);  /* 정확히 32767: 경계 바로 안 */
        CHECK(mix_sample(-3, 0, &c, &clip) == -1 && !clip);      /* -1.5 LSB → -1 */
    }
    {   /* 무작위 10만 샘플: 오라클과 비트 일치 · 클립 판정 일치 · 반올림 2번 버전은 0.5 LSB 를 넘는다 */
        int mism = 0, clipmis = 0; double worst1 = 0, worst2 = 0; g_rng = 0xBADC0DE5u;
        for (int i = 0; i < 100000; i++) {
            mix_cfg_t c; c.g_mic = rng_q15(); c.g_tone = rng_q15();   /* 순서 고정 (초기화 목록 평가 순서는 미지정) */
            q15_t m = rng_q15(); q15_t t = rng_q15();
            bool clip;
            q15_t y = mix_sample(m, t, &c, &clip);
            double ex = ref_mix_lsb(m, t, &c);
            long want = clampl((long)floor(ex + 0.5), Q15_MIN, Q15_MAX);
            if (y != want) mism++;
            if (clip != (ex + 0.5 >= 32768.0 || ex + 0.5 < -32768.0)) clipmis++;
            if (ex > -32768.0 && ex < 32767.0) {               /* 포화 근처 제외: 순수 반올림 오차 */
                double e1 = fabs(y - ex), e2 = fabs(mix_sample_2round(m, t, &c) - ex);
                if (e1 > worst1) worst1 = e1;
                if (e2 > worst2) worst2 = e2;
            }
        }
        CHECK(mism == 0 && clipmis == 0);
        CHECK(worst1 <= 0.5);
        CHECK(worst2 > 0.5);                                    /* 주어진 2회 반올림 버전의 한계 */
    }
    {   /* DMA HT/TC 콜백으로 원형 버퍼 두 절반 처리 */
        g_cfg.g_mic = 24576; g_cfg.g_tone = 16384;             /* 0.75, 0.5 */
        for (int i = 0; i < NBUF; i++) {
            g_adc_buf[i] = (uint16_t)(i < 8 ? 4095 : (rng() & 0x0FFFu));   /* 앞 8개는 최대 입력 */
            g_tone_lut[i] = (q15_t)(i < 8 ? Q15_MAX : rng_q15());
        }
        size_t want_clips = 0; int mism = 0;
        uint16_t want[NBUF];
        for (int i = 0; i < NBUF; i++) {
            q15_t m = (q15_t)(((int32_t)(g_adc_buf[i] & 0x0FFF) - 2048) * 16);
            double ex = ref_mix_lsb(m, g_tone_lut[i], &g_cfg);
            long y = clampl((long)floor(ex + 0.5), Q15_MIN, Q15_MAX);
            if (ex + 0.5 >= 32768.0 || ex + 0.5 < -32768.0) want_clips++;
            want[i] = (uint16_t)clampl((long)floor((double)(y + 32768) / 16.0 + 0.5), 0, 4095);
        }
        g_clips = 0;
        dma_half_cplt_cb();
        CHECK(g_clips >= 8);                                    /* 앞 절반: 최대 입력 8개는 클립 */
        dma_cplt_cb();
        for (int i = 0; i < NBUF; i++) if (g_dac_buf[i] != want[i]) mism++;
        CHECK(mism == 0 && g_clips == want_clips);
        CHECK(g_dac_buf[0] == 4095);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
