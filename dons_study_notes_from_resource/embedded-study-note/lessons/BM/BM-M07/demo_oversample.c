/* BM-M07 demo: 4^n 오버샘플링 → +n 비트? 노이즈(디더) 크기에 따른 실제 RMS 오차
 *
 * solution.c 와 같은 양자화기: code = clamp(round(V·4096/VDDA + σ·g)), g ~ N(0,1) (Irwin–Hall 12),
 * 결과 R = (Σ code + 2^(n−1)) >> n  (12+n 비트), 오차 = R/2^n − V·4096/VDDA  [12비트 LSB 단위].
 * 입력: 고정 시드 xorshift32 로 뽑은 2000 개의 DC 전압 (0.1..3.2 V).
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_oversample.c -o demo_os && ./demo_os
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t rng = 0x1234567u;
static uint32_t xs32(void) { uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng = x; }
static double gauss(void) { double s = 0; for (int i = 0; i < 12; i++) s += xs32() / 4294967296.0; return s - 6.0; }
static uint32_t conv(double v, double sigma) {
    double x = floor(v * 4096.0 / 3.3 + sigma * gauss() + 0.5);
    return (uint32_t)(x < 0 ? 0 : x > 4095 ? 4095 : x);
}

int main(void) {
    const double sig[] = { 0.0, 0.1, 0.3, 0.5, 1.0, 2.0 };
    const int LEVELS = 2000;
    printf("RMS error [LSB12] of the (12+n)-bit result, %d DC levels\n", LEVELS);
    printf("  sigma |    n=0    n=1    n=2    n=3    n=4   (samples 1/4/16/64/256)\n");
    for (unsigned si = 0; si < sizeof sig / sizeof sig[0]; si++) {
        printf("  %5.1f |", sig[si]);
        for (unsigned n = 0; n <= 4; n++) {
            uint32_t vseed = 0xADC0FFEEu;
            double se = 0;
            for (int t = 0; t < LEVELS; t++) {
                vseed ^= vseed << 13; vseed ^= vseed >> 17; vseed ^= vseed << 5;
                double v = 0.1 + 3.1 * (vseed / 4294967296.0);
                uint32_t sum = 0;
                for (uint32_t i = 0; i < (1u << (2 * n)); i++) sum += conv(v, sig[si]);
                uint32_t r = n ? (sum + (1u << (n - 1))) >> n : sum;
                double e = r / (double)(1u << n) - v * 4096.0 / 3.3;
                se += e * e;
            }
            printf(" %6.3f", sqrt(se / LEVELS));
        }
        printf("\n");
    }
    printf("\nideal (12+n)-bit quantizer alone: ");
    for (unsigned n = 0; n <= 4; n++) printf("n=%u %.3f  ", n, 1.0 / sqrt(12.0) / (1u << n));
    printf("\n\nfixed input 1.0004 V (code boundary not crossed), sigma=0: 16 samples ->");
    for (int i = 0; i < 16; i++) printf(" %u", conv(1.0004, 0));
    printf("\n\ntime per (12+n)-bit result @21 MHz ADCCLK, SMP=15 cyc (27 cyc/conv):\n");
    for (unsigned n = 0; n <= 4; n++) {
        double us = (1u << (2 * n)) * 27 / 21.0;
        printf("  n=%u  %3u conv  %8.2f us  -> max %8.0f results/s\n", n, 1u << (2 * n), us, 1e6 / us);
    }
    return 0;
}
