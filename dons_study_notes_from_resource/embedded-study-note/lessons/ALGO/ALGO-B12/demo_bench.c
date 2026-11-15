/* ALGO-B12 demo: popcount 방식별 속도 (이 맥, Apple Silicon, -O2)
 *
 * 방식: builtin(arm64 CNT) · SWAR · Kernighan · 256칸 바이트 LUT.
 * 함정: clang -O2 는 SWAR 와 Kernighan 루프를 "알아보고" CNT 명령으로 바꿔 버린다(asm 으로 확인).
 *       그러면 셋 다 같은 코드라 비교가 안 된다. 그래서 *_raw 판은 빈 inline asm 장벽
 *       (__asm__ volatile("" : "+r"(x))) 으로 패턴 인식을 막아 "CNT 없는 코어가 실제로 하는 일"을 재현한다.
 * 입력 3종 (각 65536 개, L1/L2 안): sparse(평균 ~4비트), random(~16비트), dense(~28비트).
 * 측정: 방식마다 루프를 따로 둬서 인라인(builtin 은 NEON 으로 벡터화될 수 있음 — 그것도 실제 결과),
 *       워밍업 1회 + 7회 중 중앙값, 결과 합을 volatile 에 저장(컴파일러가 일을 지우지 못하게).
 * 절대 ns 보다 비율을 볼 것. Cortex-M4 의 사이클 수와는 다르다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N     65536
#define PASS  200
#define RUNS  7

#define BARRIER(v) __asm__ volatile("" : "+r"(v))

static unsigned pc_builtin(uint32_t x) { return (unsigned)__builtin_popcount(x); }
static unsigned pc_kern_compiled(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }
static unsigned pc_kern_raw(uint32_t x) {
    unsigned c = 0;
    while (x) { x &= x - 1u; BARRIER(x); c++; }
    return c;
}
static unsigned pc_swar_raw(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u); BARRIER(x);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u); BARRIER(x);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu; BARRIER(x);
    return (x * 0x01010101u) >> 24;
}
static uint8_t lut[256];
static unsigned pc_lut8(uint32_t x) {
    return (unsigned)lut[x & 0xFF] + lut[(x >> 8) & 0xFF] + lut[(x >> 16) & 0xFF] + lut[x >> 24];
}

static uint32_t rng = 0x9E3779B9u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static volatile unsigned long long sink;

/* 방식마다 루프를 따로 만들어 함수가 인라인되게 한다 (함수 포인터 호출 비용이 결과를 덮지 않도록) */
#define DEFINE_BENCH(fn)                                                         \
static double bench_##fn(const uint32_t *v) {                                    \
    double t[RUNS];                                                              \
    for (int r = -1; r < RUNS; r++) {               /* r = -1 은 워밍업 */        \
        double t0 = now();                                                       \
        unsigned long long s = 0;                                                \
        for (int p = 0; p < PASS; p++)                                           \
            for (int i = 0; i < N; i++) s += fn(v[i]);                           \
        double dt = now() - t0;                                                  \
        sink += s;                                                               \
        if (r >= 0) t[r] = dt;                                                   \
    }                                                                            \
    qsort(t, RUNS, sizeof t[0], cmpd);                                           \
    return t[RUNS / 2] * 1e9 / ((double)N * PASS);                               \
}
DEFINE_BENCH(pc_builtin)
DEFINE_BENCH(pc_kern_compiled)
DEFINE_BENCH(pc_swar_raw)
DEFINE_BENCH(pc_kern_raw)
DEFINE_BENCH(pc_lut8)

int main(void) {
    for (int i = 1; i < 256; i++) lut[i] = (uint8_t)((i & 1) + lut[i >> 1]);
    static uint32_t sparse[N], dense[N], rnd[N];
    double avg[3] = {0, 0, 0};
    for (int i = 0; i < N; i++) {
        sparse[i] = xs() & xs() & xs();          /* 각 비트 1/8 확률 → 평균 4 */
        rnd[i] = xs();                           /* 평균 16 */
        dense[i] = ~(xs() & xs() & xs());        /* 평균 28 */
        avg[0] += pc_builtin(sparse[i]); avg[1] += pc_builtin(rnd[i]); avg[2] += pc_builtin(dense[i]);
    }
    printf("avg popcount: sparse %.2f  random %.2f  dense %.2f\n", avg[0] / N, avg[1] / N, avg[2] / N);

    struct { const char *name; double (*b)(const uint32_t *); } m[] = {
        {"builtin (CNT)", bench_pc_builtin}, {"kernighan as written", bench_pc_kern_compiled},
        {"swar (barrier)", bench_pc_swar_raw}, {"kernighan (barrier)", bench_pc_kern_raw}, {"lut8", bench_pc_lut8},
    };
    printf("%-22s %10s %10s %10s   (ns per value, median of %d)\n", "method", "sparse", "random", "dense", RUNS);
    for (unsigned k = 0; k < sizeof m / sizeof m[0]; k++) {
        double a = m[k].b(sparse), b = m[k].b(rnd), c = m[k].b(dense);
        printf("%-22s %10.3f %10.3f %10.3f\n", m[k].name, a, b, c);
    }
    printf("(checksum %llu)\n", sink);
    return 0;
}
