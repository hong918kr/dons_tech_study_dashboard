/* BM-M07 추가 연습 — 한 번에 검증
 *
 * 연습 1: 분해능(RES)별 변환 시간 · 최대 샘플률 · 스캔 시간 · 오버샘플링 예산
 * 연습 2: 온도 센서 2점 보정 (TS_CAL1 @30 °C, TS_CAL2 @110 °C) — 정수, 0.01 °C, 부호 있는 반올림
 * 연습 3: 아날로그 워치독(AWD) 임계값: mV → LTR/HTR 코드 (전수 brute force 와 동치)
 * 연습 4: 외부 커패시터로 고임피던스 소스 받치기: 전하 분배 오차 → 최소 C_ext, 그 대가(최대 샘플률)
 *
 * 수치·주소는 STM32F4 (RM0090 / 데이터시트) 기준 대표값 — 확인 권장. 정답은 이 파일의 정의.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s   (line %d)\n", #expr, __LINE__); } while (0)

/* ---------------- 연습 1 ---------------- */
static const uint16_t smp_cyc[8] = { 3, 15, 28, 56, 84, 112, 144, 480 };
/* T_conv = SMP + RES 비트수 (12/10/8/6 → 12/10/8/6 cycle 의 SAR 단계). 잘못된 인자 → 0 */
uint32_t conv_cycles_res(unsigned smp, unsigned res_bits) {
    if (smp > 7 || (res_bits != 12 && res_bits != 10 && res_bits != 8 && res_bits != 6)) return 0;
    return smp_cyc[smp] + res_bits;
}
uint32_t max_sps(uint32_t f_adc_hz, unsigned smp, unsigned res_bits) {
    uint32_t c = conv_cycles_res(smp, res_bits);
    return c ? f_adc_hz / c : 0;
}
/* 스캔 한 바퀴 시간 [ns], 반올림 */
uint32_t seq_time_ns(uint32_t f_adc_hz, const uint8_t *smp, unsigned n) {
    uint64_t cyc = 0;
    for (unsigned i = 0; i < n; i++) cyc += conv_cycles_res(smp[i], 12);
    return (uint32_t)((cyc * 1000000000ull + f_adc_hz / 2) / f_adc_hz);
}
/* 4^n 오버샘플 결과 1개/초 (12비트, 폴링 오버헤드 무시) */
uint32_t os_results_per_s(uint32_t f_adc_hz, unsigned smp, unsigned n) {
    return f_adc_hz / (conv_cycles_res(smp, 12) << (2 * n));
}

/* ---------------- 연습 2 ---------------- */
/* raw 를 VDDA 에서 3.3 V 기준으로 환산한 뒤 두 보정점 사이 직선:
 * T = 30 + 80 · (raw·VDDA/3300 − CAL1) / (CAL2 − CAL1)   [°C] → 0.01 °C 정수, 0 에서 멀어지는 쪽 반올림 */
int32_t ts_centi(uint32_t raw, uint32_t vdda_mv, uint16_t cal1, uint16_t cal2) {
    if (cal2 <= cal1) return INT32_MIN;
    int64_t num = 8000 * ((int64_t)raw * vdda_mv - (int64_t)cal1 * 3300);
    int64_t den = (int64_t)(cal2 - cal1) * 3300;
    int64_t q = (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);   /* 대칭 반올림 */
    return (int32_t)(3000 + q);
}
/* 흔한 오답: 음수에서 C 의 정수 나눗셈(0 쪽 절삭) + 양수용 반올림 */
int32_t ts_centi_naive(uint32_t raw, uint32_t vdda_mv, uint16_t cal1, uint16_t cal2) {
    int64_t num = 8000 * ((int64_t)raw * vdda_mv - (int64_t)cal1 * 3300);
    int64_t den = (int64_t)(cal2 - cal1) * 3300;
    return (int32_t)(3000 + (num + den / 2) / den);
}

/* ---------------- 연습 3 ---------------- */
/* AWD 는 code > HTR 또는 code < LTR 이면 플래그. 코드 c 의 전압 = c·VDDA/4096 (이 레슨 모델의 bin 중심).
 * 원하는 것: (c > HTR) ⇔ c·VDDA > hi·4096,  (c < LTR) ⇔ c·VDDA < lo·4096 */
void awd_thresholds(uint32_t lo_mv, uint32_t hi_mv, uint32_t vdda_mv, uint32_t *ltr, uint32_t *htr) {
    uint64_t h = (uint64_t)hi_mv * 4096 / vdda_mv;                       /* floor */
    uint64_t l = ((uint64_t)lo_mv * 4096 + vdda_mv - 1) / vdda_mv;       /* ceil */
    *htr = (uint32_t)(h > 4095 ? 4095 : h);
    *ltr = (uint32_t)(l > 4095 ? 4095 : l);
}

/* ---------------- 연습 4 ---------------- */
/* 핀에 C_ext 를 달면 샘플링 순간 C_ADC 와 전하를 나눠 갖는다: ΔV = step · C_ADC/(C_ADC + C_ext).
 * ΔV ≤ VDDA/(4096·den) (den=4 → ¼ LSB) 을 만족하는 최소 C_ext [pF] (올림) */
uint64_t cext_min_pf(uint32_t c_adc_pf, uint32_t step_mv, uint32_t vdda_mv, uint32_t den) {
    /* C_ext ≥ C_ADC · (step·4096·den / VDDA − 1) */
    uint64_t a = (uint64_t)step_mv * 4096u * den;                         /* 비교: C_ADC·a ≤ (C_ADC+C_ext)·VDDA */
    uint64_t need = (uint64_t)c_adc_pf * a;                                /* (C_ADC+C_ext)·VDDA ≥ need */
    uint64_t tot = (need + vdda_mv - 1) / vdda_mv;                         /* ceil */
    return tot > c_adc_pf ? tot - c_adc_pf : 0;
}
/* 그 대가: C_ext 가 소스 R 로 다시 차오르는 시간 ≈ R·C_ext·ln(2^14) → 그보다 자주 샘플하면 처진다 */
double cext_max_rate_hz(double r_ohm, double cext_f) { return 1.0 / (r_ohm * cext_f * 14.0 * log(2.0)); }

int main(void) {
    printf("== 연습 1: 변환 시간 · 샘플률 ==\n");
    CHECK(conv_cycles_res(0, 12) == 15 && conv_cycles_res(0, 10) == 13 && conv_cycles_res(0, 8) == 11 && conv_cycles_res(0, 6) == 9);
    CHECK(conv_cycles_res(7, 12) == 492 && conv_cycles_res(8, 12) == 0 && conv_cycles_res(0, 11) == 0);
    CHECK(max_sps(36000000, 0, 12) == 2400000 && max_sps(36000000, 0, 6) == 4000000);   /* 2.4 MSPS 의 출처 */
    CHECK(max_sps(21000000, 0, 12) == 1400000);
    {
        uint8_t board[4] = { 7, 3, 1, 1 };                                 /* solution 의 보드 모니터 */
        uint32_t t = seq_time_ns(21000000, board, 4);
        printf("      board scan 614 cyc @21 MHz = %u ns\n", t);
        CHECK(t == 29238);
    }
    CHECK(os_results_per_s(21000000, 1, 0) == 777777 && os_results_per_s(21000000, 1, 4) == 3038);

    printf("== 연습 2: 온도 센서 2점 보정 ==\n");
    {
        const uint16_t c1 = 943, c2 = 1191;                                /* 예시 보정값 (가상의 칩) */
        CHECK(ts_centi(943, 3300, c1, c2) == 3000 && ts_centi(1191, 3300, c1, c2) == 11000);
        /* VDDA 3.0 V 에서 같은 30 °C → raw 는 943·3300/3000 ≈ 1037 */
        CHECK(ts_centi(1037, 3000, c1, c2) >= 2990 && ts_centi(1037, 3000, c1, c2) <= 3010);
        unsigned bad = 0, naive_bad = 0, n = 0;
        const uint32_t vd[3] = { 2800, 3000, 3300 };
        for (int k = 0; k < 3; k++)
            for (uint32_t raw = 0; raw < 4096; raw++, n++) {
                double t = 30.0 + 80.0 * ((double)raw * vd[k] / 3300.0 - c1) / (c2 - c1);
                int32_t ref = (int32_t)(t >= 0 ? floor(t * 100 + 0.5) : -floor(-t * 100 + 0.5));
                bad += ts_centi(raw, vd[k], c1, c2) != ref;
                naive_bad += ts_centi_naive(raw, vd[k], c1, c2) != ref;
            }
        printf("      %u cases: mine wrong %u, naive (truncating negatives) wrong %u\n", n, bad, naive_bad);
        CHECK(bad == 0 && naive_bad > 0);
        CHECK(ts_centi(100, 3300, 5, 5) == INT32_MIN);
    }

    printf("== 연습 3: AWD 임계값 ==\n");
    {
        unsigned bad = 0, n = 0;
        const uint32_t vd[2] = { 3000, 3300 };
        for (int k = 0; k < 2; k++)
            for (uint32_t mv = 0; mv <= 3400; mv += 7) {
                uint32_t ltr, htr;
                awd_thresholds(mv, mv, vd[k], &ltr, &htr);
                for (uint32_t c = 0; c < 4096; c++, n++) {
                    bool over = (uint64_t)c * vd[k] > (uint64_t)mv * 4096;
                    bool under = (uint64_t)c * vd[k] < (uint64_t)mv * 4096;
                    if (mv * 4096u < 4095u * vd[k]) bad += (c > htr) != over;   /* HTR 이 4095 로 포화되지 않는 범위 */
                    if (mv * 4096u <= 4095u * vd[k]) bad += (c < ltr) != under;
                }
            }
        printf("      %u (mV, code) pairs vs brute force: %u bad\n", n, bad);
        CHECK(bad == 0);
        uint32_t l, h;
        awd_thresholds(1500, 1800, 3300, &l, &h);                          /* 배터리 3.0..3.6 V 를 1/2 분압 → 1500..1800 mV */
        printf("      1500..1800 mV @3300: LTR=%u HTR=%u\n", l, h);
        CHECK(l == 1862 && h == 2234);
    }

    printf("== 연습 4: 외부 커패시터 ==\n");
    {
        uint64_t c = cext_min_pf(4, 3300, 3300, 4);
        printf("      full-scale step, 1/4 LSB: C_ext >= %llu pF\n", (unsigned long long)c);
        CHECK(c == 65532);
        /* 최소값 바로 아래는 오차 > ¼ LSB, 최소값은 ≤ ¼ LSB (실수로 확인) */
        double e_ok = 3300.0 * 4 / (4 + (double)c) * 4096 / 3300.0;
        double e_bad = 3300.0 * 4 / (4 + (double)c - 1) * 4096 / 3300.0;
        CHECK(e_ok <= 0.25 && e_bad > 0.25);
        CHECK(cext_min_pf(4, 100, 3300, 4) == 1982);                       /* 계단이 작으면(인접 채널 비슷) 훨씬 작아도 됨 */
        double f = cext_max_rate_hz(50e3, 100e-9);
        printf("      50k divider + 100 nF: recharge-limited rate ~ %.1f Hz\n", f);
        CHECK(f > 20.0 && f < 22.0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
