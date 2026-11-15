/* BM-B10 demo_float.c — "float 로 == 비교하면 해를 놓친다" 실측
 *
 * 같은 PLL 열거를 세 가지로 한다:
 *   int    : hse·N == target·M·P            (이 레슨의 방식, 정확)
 *   double : (double)hse / M * N / P == target
 *   float  : (float)hse / M * N / P == target   (단정밀도 — 소형 MCU 의 FPU, 많은 Python 이전 툴)
 * missed = 정수로는 정확한 해인데 == 가 거짓 (해를 놓침)
 * false  = 진짜 값에서 1 Hz 이상 떨어진 정수 목표를 == 가 참이라고 한 (조합, 목표) 쌍의 수 (틀린 해를 받아들임)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_float.c -o demo_float && ./demo_float
 */
#include <stdint.h>
#include <stdio.h>

#define MHZ(x) ((uint32_t)(x) * 1000000u)

static int valid_mnp(uint32_t hse, uint32_t m, uint32_t n) {
    /* F40x 3.3V 제약 (solution.c 의 F40X 프리셋과 같음). P 는 호출 쪽 루프가 {2,4,6,8} 만 돈다 */
    if (m < 2 || m > 63 || n < 50 || n > 432) return 0;
    if ((uint64_t)MHZ(1) * m > hse || (uint64_t)MHZ(2) * m < hse) return 0;
    uint64_t hn = (uint64_t)hse * n;
    if (hn < (uint64_t)MHZ(100) * m || hn > (uint64_t)MHZ(432) * m) return 0;
    return 1;
}

int main(void) {
    const uint32_t xtal[] = { MHZ(8), MHZ(12), MHZ(16), MHZ(25), MHZ(26), 12288000u, 24576000u, 19200000u,
                              14745600u, 13560000u };
    const int NX = (int)(sizeof xtal / sizeof xtal[0]);
    long exact = 0, miss_d = 0, miss_f = 0, false_f = 0, false_d = 0;
    int shown_d = 0, shown_f = 0;
    uint64_t max_hz = 0; uint32_t mx_h = 0, mx_m = 0, mx_n = 0, mx_p = 0, mx_t = 0;
    printf("HSE(Hz)     int-solutions  double-missed  float-missed  float-false\n");
    for (int x = 0; x < NX; x++) {
        uint32_t hse = xtal[x];
        long e = 0, md = 0, mf = 0, ff = 0;
        for (uint32_t m = 2; m <= 63; m++)
            for (uint32_t n = 50; n <= 432; n++)
                for (uint32_t p = 2; p <= 8; p += 2) {
                    if (!valid_mnp(hse, m, n)) continue;
                    uint64_t num = (uint64_t)hse * n, den = (uint64_t)m * p;
                    if (num % den == 0u) {                       /* 정확한 정수 Hz 해: target 을 그 값으로 */
                        uint32_t target = (uint32_t)(num / den);
                        if (target > MHZ(168)) continue;
                        e++;
                        double d = (double)hse / m * n / p;
                        float  f = (float)hse / (float)m * (float)n / (float)p;
                        if (d != (double)target) {
                            md++;
                            if (shown_d < 3) { shown_d++;
                                printf("  double miss: HSE=%u M=%u N=%u P=%u target=%u got %.9f\n", hse, m, n, p, target, d); }
                        }
                        if (f != (float)target) {
                            mf++;
                            if (shown_f < 3) { shown_f++;
                                printf("  float  miss: HSE=%u M=%u N=%u P=%u target=%u got %.3f\n", hse, m, n, p, target, (double)f); }
                        }
                    }
                    /* 거짓 양성: 진짜 값에서 1 Hz 이상 떨어진 정수 목표 T 를 float 가 "정확" 으로 받아들이나 */
                    uint64_t lo = num / den;                     /* 진짜 값 = num/den, lo = 내림 */
                    for (uint64_t t = lo > 64u ? lo - 64u : 1u; t <= lo + 65u; t++) {
                        /* |num/den − t| ≥ 1  ⇔  |num − t·den| ≥ den */
                        uint64_t td = t * den, diff = td > num ? td - num : num - td;
                        if (diff < den || t > MHZ(168)) continue;
                        float  f = (float)hse / (float)m * (float)n / (float)p;
                        double d = (double)hse / m * n / p;
                        if (f == (float)(uint32_t)t) {
                            ff++;
                            uint64_t hz = (diff + den - 1u) / den;      /* 올림 Hz 거리 */
                            if (hz > max_hz) { max_hz = hz; mx_h = hse; mx_m = m; mx_n = n; mx_p = p; mx_t = (uint32_t)t; }
                        }
                        if (d == (double)(uint32_t)t) false_d++;
                    }
                }
        printf("%-10u  %13ld  %13ld  %12ld  %11ld\n", hse, e, md, mf, ff);
        exact += e; miss_d += md; miss_f += mf; false_f += ff;
    }
    printf("total       %13ld  %13ld  %12ld  %11ld   (double-false %ld)\n", exact, miss_d, miss_f, false_f, false_d);
    printf("worst float false accept: HSE=%u M=%u N=%u P=%u true=%.6f Hz accepted target=%u (off by >= %llu Hz)\n",
           mx_h, mx_m, mx_n, mx_p, (double)mx_h * mx_n / ((double)mx_m * mx_p), mx_t, (unsigned long long)max_hz);
    return 0;
}
