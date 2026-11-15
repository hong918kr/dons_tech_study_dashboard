/* ALGO-B03 demo_gen_table.c — 보정 테이블 생성기 (PC 에서 한 번 돌려 const 배열을 소스에 붙인다)
 *
 * 모델 (이 레슨이 정한 것):
 *   NTC 10 kΩ @25 °C, Beta = 3950 K (B25/85 류), 고정 저항 10 kΩ 를 Vref 쪽, NTC 를 GND 쪽에 둔 분압.
 *   12-bit 비율측정(ratiometric) ADC: 코드 c 의 중심 = (c + 0.5) / 4096 · Vref.
 *   ratio = R_ntc / (R_fix + R_ntc)  →  R_ntc = R_fix · ratio / (1 - ratio)
 *   Beta 식: 1/T = 1/T0 + ln(R/R0)/B   (T 는 켈빈)
 * 비균등 테이블: -40..125 °C 를 5 °C 간격으로 → 코드로 바꿔 반올림 → 그 코드에서 다시 온도를 계산(센티 °C 반올림).
 *   → 표의 각 점은 모델과 ±0.5 centi 이내. 코드 간격은 곡선이 휘는 양 끝에서 자동으로 촘촘해진다.
 * 균등 테이블: 코드 128 부터 64 코드 간격 62점 (128..4032), 값 = 모델 온도(센티 °C 반올림).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_gen_table.c -lm -o demo_gen_table && ./demo_gen_table
 */
#include <math.h>
#include <stdio.h>

static double t_of_code(double code) {
    double x = (code + 0.5) / 4096.0, r = 10000.0 * x / (1.0 - x);
    return 1.0 / (1.0 / 298.15 + log(r / 10000.0) / 3950.0) - 273.15;
}
static double code_of_t(double t) {
    double r = 10000.0 * exp(3950.0 * (1.0 / (t + 273.15) - 1.0 / 298.15));
    return r / (r + 10000.0) * 4096.0 - 0.5;
}

int main(void) {
    int n = 0;
    printf("/* 비균등: 5 °C 간격, 코드 오름차순 = 온도 내림차순 */\n");
    printf("static const int32_t NTC_CODE[NTC_N] = {");
    for (int t = 125; t >= -40; t -= 5, n++) printf("%s%ld", n ? ", " : "", lround(code_of_t(t)));
    printf("};\nstatic const int32_t NTC_CENTI[NTC_N] = {");
    n = 0;
    for (int t = 125; t >= -40; t -= 5, n++) printf("%s%ld", n ? ", " : "", lround(100.0 * t_of_code((double)lround(code_of_t(t)))));
    printf("};  /* NTC_N = %d */\n", n);
    printf("/* 균등: 코드 128 + 64j, j = 0..61 */\nstatic const int32_t ULUT[ULUT_N] = {");
    for (int j = 0; j < 62; j++)
        printf("%s%ld", j == 0 ? "\n    " : (j % 12 == 0) ? ",\n    " : ", ", lround(100.0 * t_of_code(128 + 64 * j)));
    printf("};\n");
    return 0;
}
