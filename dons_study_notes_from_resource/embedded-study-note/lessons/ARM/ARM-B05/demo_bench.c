/* ARM-B05 demo: 분기판 vs branchless(csel)판 — 데이터가 예측 가능할 때와 무작위일 때
 *   cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-B05_bench && /tmp/ARM-B05_bench
 * 함수 호출(bl/ret) 비용이 포함된 "호출당 ns" 다. 맥은 다른 작업도 돌므로 7회 중앙값, 대략적인 값으로만 볼 것.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int64_t smax_br(int64_t a, int64_t b);
int64_t smax_sel(int64_t a, int64_t b);

#define N    (1 << 20)
#define REPS 8
#define RUNS 7

static int64_t A[N], B[N];
static volatile int64_t g_sink;

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
static int cmp_d(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }

static double bench(int64_t (*f)(int64_t, int64_t)) {
    double t[RUNS];
    for (int r = 0; r < RUNS; r++) {
        int64_t acc = 0;
        double t0 = now_ns();
        for (int rep = 0; rep < REPS; rep++)
            for (int i = 0; i < N; i++) acc += f(A[i], B[i]);
        t[r] = (now_ns() - t0) / ((double)REPS * N);
        g_sink = acc;
    }
    qsort(t, RUNS, sizeof t[0], cmp_d);
    return t[RUNS / 2];
}

int main(void) {
    uint64_t s = 12345;
    const char *name[3] = {"predictable (a > b always)", "periodic (a>b, a<b, ...)  ", "random 50/50              "};
    printf("pattern                      smax_br ns/call  smax_sel ns/call\n");
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < N; i++) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            int64_t big = (int64_t)(s >> 2), small = -(int64_t)(s >> 40);
            int a_wins = p == 0 ? 1 : p == 1 ? (i & 1) : (int)(s >> 63);
            A[i] = a_wins ? big : small;
            B[i] = a_wins ? small : big;
        }
        printf("%s   %8.2f         %8.2f\n", name[p], bench(smax_br), bench(smax_sel));
    }
    return 0;
}
