/* BM-M07 demo: 샘플링 시간 × 소스 임피던스 → 오차 표 (solution.c 의 RC 샘플링 모델 그대로)
 *
 * 모델: 샘플 커패시터 C_ADC 가 R_src + R_ADC 를 통해 충전. 시작 전압 = 이전 채널의 잔류 전압.
 *       V_hold = V_in + (V_prev − V_in) · exp(−t_s / τ),  τ = (R_src + R_ADC)·C_ADC,  t_s = SMP 사이클 / f_ADC
 *       C_ADC 4 pF, R_ADC 6 kΩ — 대표값, 데이터시트 확인 권장.
 * 표 2: 데이터시트식 R_AIN_max = (k − 0.5)/(f·C·ln 2^14) − R_ADC  (12비트, ¼ LSB 기준)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_smp_table.c -o demo_smp && ./demo_smp
 */
#include <math.h>
#include <stdio.h>

static const int cyc[8] = { 3, 15, 28, 56, 84, 112, 144, 480 };
static int code(double v, double vdda) {
    double x = floor(v * 4096.0 / vdda + 0.5);
    return (int)(x < 0 ? 0 : x > 4095 ? 4095 : x);
}

int main(void) {
    const double vdda = 3.3, C = 4e-12, Radc = 6000.0;
    const double rs[] = { 0, 1e3, 5e3, 10e3, 50e3, 100e3, 200e3 };
    const double fs[] = { 21e6, 36e6 };
    for (int fi = 0; fi < 2; fi++) {
        double f = fs[fi];
        printf("\n[1] error [LSB] reading 3.000 V right after a 0 V channel, f_ADC = %.0f MHz\n", f / 1e6);
        printf("  R_src   |");
        for (int k = 0; k < 8; k++) printf(" %6d", cyc[k]);
        printf("   (SMP cycles)\n  --------+");
        for (int k = 0; k < 8; k++) printf("-------");
        printf("\n");
        for (unsigned i = 0; i < sizeof rs / sizeof rs[0]; i++) {
            printf("  %6.0fk |", rs[i] / 1e3);
            for (int k = 0; k < 8; k++) {
                double tau = (rs[i] + Radc) * C, ts = cyc[k] / f;
                double vh = 3.0 + (0.0 - 3.0) * exp(-ts / tau);
                printf(" %6d", code(vh, vdda) - code(3.0, vdda));
            }
            printf("\n");
        }
    }
    printf("\n[2] datasheet-style R_AIN max [kOhm] (negative = not even with 0 ohm)\n");
    printf("  SMP cyc |  21 MHz   36 MHz\n");
    for (int k = 0; k < 8; k++) {
        double r21 = (cyc[k] - 0.5) / (21e6 * C * 14 * log(2.0)) - Radc;
        double r36 = (cyc[k] - 0.5) / (36e6 * C * 14 * log(2.0)) - Radc;
        printf("  %7d | %8.1f %8.1f\n", cyc[k], r21 / 1e3, r36 / 1e3);
    }
    printf("\n[3] crosstalk the other way: read 0.300 V right after a 3.0 V channel, 50k source, 21 MHz\n  ");
    for (int k = 0; k < 8; k++) {
        double tau = (50e3 + Radc) * C, ts = cyc[k] / 21e6;
        double vh = 0.3 + (3.0 - 0.3) * exp(-ts / tau);
        printf("%d:%+d  ", cyc[k], code(vh, vdda) - code(0.3, vdda));
    }
    printf("\n");
    return 0;
}
