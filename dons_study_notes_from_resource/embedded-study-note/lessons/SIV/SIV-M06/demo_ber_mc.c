/* SIV-M06 demo: "무오류 N=λ0/target 비트 시험"의 OC 곡선(통과 확률 vs 실제 BER)을 Monte-Carlo 로 재고 e^{-N·BER} 와 비교.
 * 오류 수를 비트마다 뽑지 않고 "다음 오류까지의 간격(기하분포)"으로 건너뛰어 빠르게 시뮬레이션한다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_ber_mc.c -o demo_ber_mc && ./demo_ber_mc
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t g = 0xD1B54A32D192ED03ull;
static double urand(void) { g ^= g << 13; g ^= g >> 7; g ^= g << 17; return ((double)(g >> 11) + 0.5) * 0x1.0p-53; }

int main(void) {
    const double target = 1e-12, N = -log(0.05) / target;        /* 2.9957e12 비트 */
    const double ratio[] = { 0.1, 0.3, 0.5, 1.0, 2.0, 3.0 };
    const int T = 100000;
    printf("zero-error test, N=%.4e bits, %d trials per row\n", N, T);
    printf("true BER      pass(MC)   pass(theory e^-N*BER)\n");
    for (int r = 0; r < 6; r++) {
        double ber = target * ratio[r];
        int pass = 0;
        for (int t = 0; t < T; t++) {
            double gap = floor(log(urand()) / log1p(-ber));        /* 첫 오류 전까지의 정상 비트 수 ~ Geometric(ber) */
            pass += gap >= N;
        }
        printf("%.1e      %6.2f%%    %6.2f%%\n", ber, 100.0 * pass / T, 100.0 * exp(-N * ber));
    }
    return 0;
}
