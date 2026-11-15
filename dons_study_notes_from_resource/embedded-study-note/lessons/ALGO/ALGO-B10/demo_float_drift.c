/* ALGO-B10 demo: 실수(float) 누적합은 드리프트한다, 정수 누적합은 안 한다
 *
 * 12비트 ADC 스트림(xorshift32, 고정 시드)을 N=64 이동 합으로 유지한다.
 *   int   : 정수 코드(0..4095)를 uint32_t 로 더하고 뺀다              → 항상 정확
 *   float : 볼트로 바꾼 값 v = code * 3.3f/4095 를 float 로 더하고 뺀다 (s += v_new - v_old)
 *   double: 같은 일을 double 로
 * 기준 = 정수 합 * (3.3/4095) 를 double 로 (창을 매번 새로 더한 것과 같은 값).
 * 오차는 "평균" 기준(합/64)으로, 볼트와 ADC LSB(3.3/4095 V) 단위로 출력.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_float_drift.c -o demo_float_drift && ./demo_float_drift
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define N 64u

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(void) {
    static uint16_t ring[N];
    static float ringf[N];
    static double ringd[N];
    const float scale_f = 3.3f / 4095.0f;
    const double scale_d = 3.3 / 4095.0, lsb = 3.3 / 4095.0;
    uint32_t seed = 0x5EED1234u, head = 0, isum = 0;
    float fsum = 0.0f;
    double dsum = 0.0;
    int32_t level = 2048;
    uint64_t next = 1000;
    double fmax = 0.0;

    printf("N=%u, 12-bit ADC, Vref 3.3 V, 1 LSB = %.3e V\n", N, lsb);
    printf("%12s %14s %14s %14s %12s\n", "samples", "int err (V)", "float err (V)", "double err (V)", "float LSB");
    for (uint64_t i = 1; i <= 100000000ull; i++) {
        /* 느린 랜덤 워크 + 잡음: 실제 센서 비슷하게 */
        level += (int32_t)(xs32(&seed) % 3) - 1;
        if (level < 200) level = 200;
        if (level > 3900) level = 3900;
        uint16_t code = (uint16_t)(level + (int32_t)(xs32(&seed) % 41) - 20);
        float vf = (float)code * scale_f;
        double vd = (double)code * scale_d;
        if (i > N) { isum -= ring[head]; fsum -= ringf[head]; dsum -= ringd[head]; }
        ring[head] = code; ringf[head] = vf; ringd[head] = vd;
        isum += code; fsum += vf; dsum += vd;
        head = (head + 1u) % N;

        double ref = (double)isum * scale_d / N;           /* 정확한 평균 (볼트) */
        double fe = fabs((double)fsum / N - ref);
        if (fe > fmax) fmax = fe;
        if (i == next) {
            double ie = fabs((double)isum * scale_d / N - ref);
            double de = fabs(dsum / N - ref);
            printf("%12llu %14.3e %14.3e %14.3e %12.4f\n", (unsigned long long)i, ie, fe, de, fe / lsb);
            next *= 10;
        }
    }
    printf("max float error over the run: %.3e V = %.4f LSB\n", fmax, fmax / lsb);
    return 0;
}
