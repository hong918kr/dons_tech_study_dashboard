/* ARM-A07 demo_bench — Q15 FIR 처리량: C 기준(32비트 포화 연산) / NEON intrinsics / 손 asm
 *   cc -std=c11 -O2 demo_bench.c solution.S shared_bugs.S -o /tmp/ARM-A07_bench && /tmp/ARM-A07_bench
 * solution.c 를 #include 해서 같은 함수를 잰다 (테스트 main 은 이름만 바꿔 둔다).
 * 샘플 하나 = 약 64M MAC, 샘플 9개의 중앙값. 타이머 CLOCK_UPTIME_RAW. QoS USER_INTERACTIVE 는 힌트일 뿐 → 비율을 보라.
 */
#define _DARWIN_C_SOURCE
#define main solution_harness_main
#include "solution.c"
#undef main
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmp_d(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
static volatile int16_t sink;

enum { SAMPLES = 9, N = 4096 };
static double mmac_per_s(fir_fn f, const int16_t *x, const int16_t *h, size_t T, int16_t *y) {
    size_t reps = (64u << 20) / (N * T); if (reps < 2) reps = 2;
    double t[SAMPLES];
    for (int s = 0; s < SAMPLES; s++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) { fir_fn volatile g = f; g(x, N, h, T, y); }
        uint64_t t1 = now_ns();
        sink = y[r16() & (N - 1)];
        t[s] = (double)(t1 - t0) / (double)reps;                    /* ns / call */
    }
    qsort(t, SAMPLES, sizeof t[0], cmp_d);
    return (double)N * (double)T / t[SAMPLES / 2] * 1e3;          /* MAC / µs = M MAC/s */
}

int main(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    static int16_t x[N + FIR_MAXT], y[N], h[FIR_MAXT];
    for (size_t i = 0; i < N + FIR_MAXT; i++) x[i] = r16();
    static const size_t Ts[] = { 8, 32, 64 };
    printf("%-6s %12s %12s %12s %12s   (M MAC/s, median of %d, n = %d)\n", "taps", "C ref", "C oracle64", "NEON intr", "asm", SAMPLES, N);
    for (size_t ti = 0; ti < 3; ti++) {
        size_t T = Ts[ti];
        make_filter(h, T, FILT_HOT);
        double a = mmac_per_s(fir_q15_ref, x, h, T, y), b = mmac_per_s(o_fir, x, h, T, y);
        double c = mmac_per_s(fir_q15_neon, x, h, T, y), d = mmac_per_s(fir_q15_asm, x, h, T, y);
        printf("%-6zu %12.0f %12.0f %12.0f %12.0f   neon/ref %.1fx  asm/ref %.1fx\n", T, a, b, c, d, c / a, d / a);
    }
    return 0;
}
