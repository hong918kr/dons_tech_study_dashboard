/* ALGO-B13 demo: 방식별 속도(ns/op)와 표 메모리.
 *   chain = 앞 결과가 다음 입력 (지연시간, latency)   array = 독립 입력 4096개 (처리량, throughput)
 *   각 측정 = 워밍업 1회 + 9회 중 중앙값. 결과는 volatile sink 로 내보내 컴파일러가 지우지 못하게 한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 *        size -m demo_bench   (표가 들어간 __TEXT,__const 크기 확인)
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_B13_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"
#include <stdlib.h>
#include <time.h>

/* 추가 방식: 16-entry 니블 표 (16 B) — 연습 5 와 같은 것 */
static const uint8_t REV4[16] = { 0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF };
static uint32_t rev32_lut16(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 8; i++) { r = (r << 4) | REV4[x & 0xFu]; x >>= 4; }
    return r;
}
static uint32_t rev32_builtin(uint32_t x) { return __builtin_bitreverse32(x); }
static uint32_t rev_nibbles32_dc(uint32_t x) { return (uint32_t)(rev_nibbles64_dc(x) >> 32); }
static uint32_t par_loop(uint32_t x) { return parity32_loop(x); }
static uint32_t par_fold(uint32_t x) { return parity32_fold(x); }
static uint32_t par_lut(uint32_t x) { return parity32_lut(x); }
static uint32_t par_builtin(uint32_t x) { return (uint32_t)__builtin_parity(x); }

static uint32_t brng = 0x9E3779B9u;
static uint32_t xs32_bench(void) { brng ^= brng << 13; brng ^= brng >> 17; brng ^= brng << 5; return brng; }
static volatile uint32_t sink;
static uint32_t in[4096];
static uint8_t buf8[4096];

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define REPS 256            /* 4096 x 256 = 약 100만 op / 측정 */
/* 매크로로 펼쳐서 f 가 인라인되게 한다 (함수 포인터로 부르면 호출 비용이 섞인다) */
#define BENCH(name, f) do {                                                         \
    double tc[10], ta[10];                                                          \
    for (int run = 0; run < 10; run++) {                                            \
        uint32_t x = 0x12345678u;                                                   \
        double t0 = now_ns();                                                       \
        for (int r = 0; r < REPS; r++)                                              \
            for (int i = 0; i < 4096; i++) x = f(x) ^ (uint32_t)i;                  \
        double t1 = now_ns();                                                       \
        sink = x;                                                                   \
        uint32_t acc = 0;                                                           \
        double t2 = now_ns();                                                       \
        for (int r = 0; r < REPS; r++)                                              \
            for (int i = 0; i < 4096; i++) acc += f(in[i] ^ (uint32_t)r);           \
        double t3 = now_ns();                                                       \
        sink = acc;                                                                 \
        tc[run] = (t1 - t0) / (4096.0 * REPS);                                      \
        ta[run] = (t3 - t2) / (4096.0 * REPS);                                      \
    }                                                                               \
    qsort(tc + 1, 9, sizeof(double), cmpd);  /* run 0 = 워밍업, 버림 */              \
    qsort(ta + 1, 9, sizeof(double), cmpd);                                         \
    printf("  %-22s chain %6.2f ns/op   array %6.2f ns/op\n", name, tc[5], ta[5]);   \
} while (0)

/* SPI 버퍼 4 KiB 를 바이트마다 뒤집는 처리량 (Part C) */
#define BENCH_BUF(name, expr) do {                                                  \
    double t[10];                                                                   \
    for (int run = 0; run < 10; run++) {                                            \
        double t0 = now_ns();                                                       \
        for (int r = 0; r < REPS; r++) {                                            \
            for (int i = 0; i < 4096; i++) { uint8_t b = buf8[i]; buf8[i] = (expr); } \
            __asm__ volatile("" ::: "memory");                                      \
        }                                                                           \
        t[run] = (now_ns() - t0) / (4096.0 * REPS);                                 \
    }                                                                               \
    qsort(t + 1, 9, sizeof(double), cmpd);                                          \
    sink = buf8[17];                                                                \
    printf("  %-22s %6.3f ns/byte  (%.2f GB/s)\n", name, t[5], 1.0 / t[5]);         \
} while (0)

int main(void) {
    for (int i = 0; i < 4096; i++) { in[i] = xs32_bench(); buf8[i] = (uint8_t)in[i]; }
    printf("this Mac (Apple Silicon, -O2), median of 9 after warm-up; compare ratios, not absolute ns\n");
    printf("reverse bits 32:\n");
    BENCH("rev32_loop", rev32_loop);
    BENCH("rev32_dc", rev32_dc);
    BENCH("rev32_lut (256 B)", rev32_lut);
    BENCH("rev32_lut16 (16 B)", rev32_lut16);
    BENCH("__builtin_bitreverse32", rev32_builtin);
    printf("reverse nibbles 32:\n");
    BENCH("rev_nibbles32_loop", rev_nibbles32_loop);
    BENCH("rev_nibbles (dc)", rev_nibbles32_dc);
    printf("parity 32:\n");
    BENCH("parity32_loop", par_loop);
    BENCH("parity32_fold", par_fold);
    BENCH("parity32_lut (256 B)", par_lut);
    BENCH("__builtin_parity", par_builtin);
    printf("SPI buffer 4 KiB, reverse bits in every byte:\n");
    BENCH_BUF("rev8_loop", rev8_loop(b));
    BENCH_BUF("rev8_dc", rev8_dc(b));
    BENCH_BUF("REV8[b] (256 B)", REV8[b]);
    BENCH_BUF("__builtin_bitreverse8", __builtin_bitreverse8(b));
    printf("tables: REV8 %zu B, PARITY8 %zu B, REV4 %zu B (all static const)\n",
           sizeof REV8, sizeof PARITY8, sizeof REV4);
    return 0;
}
