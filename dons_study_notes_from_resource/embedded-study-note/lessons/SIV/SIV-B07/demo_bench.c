/* SIV-B07 demo_bench — 호스트 DRAM 위에서 MATS+ (5n) vs March C- (10n) 실행 시간 측정
 * 결함 없는 메모리라서 '시간 비율'만 본다 (커버리지 측정은 solution.c).
 * build: cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WORDS (4u * 1024u * 1024u)          /* 16 MiB = 4Mi x 32bit */

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* volatile: 컴파일러가 쓰기/읽기를 합치거나 없애지 못하게 (실제 SRAM 테스트와 같은 조건) */
static unsigned mats_plus(volatile uint32_t *m, uint32_t n, uint32_t d) {
    unsigned fail = 0;
    for (uint32_t i = 0; i < n; i++) m[i] = d;
    for (uint32_t i = 0; i < n; i++) { fail += m[i] != d; m[i] = ~d; }
    for (uint32_t i = n; i-- > 0;)   { fail += m[i] != ~d; m[i] = d; }
    return fail;
}
static unsigned march_c_minus(volatile uint32_t *m, uint32_t n, uint32_t d) {
    unsigned fail = 0;
    for (uint32_t i = 0; i < n; i++) m[i] = d;
    for (uint32_t i = 0; i < n; i++) { fail += m[i] != d;  m[i] = ~d; }
    for (uint32_t i = 0; i < n; i++) { fail += m[i] != ~d; m[i] = d; }
    for (uint32_t i = n; i-- > 0;)   { fail += m[i] != d;  m[i] = ~d; }
    for (uint32_t i = n; i-- > 0;)   { fail += m[i] != ~d; m[i] = d; }
    for (uint32_t i = 0; i < n; i++) fail += m[i] != d;
    return fail;
}

int main(void) {
    uint32_t *buf = malloc(WORDS * sizeof *buf);
    if (!buf) return 1;
    volatile uint32_t *m = buf;
    double best_m = 1e9, best_c = 1e9;
    unsigned fail = 0;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_s();
        fail += mats_plus(m, WORDS, 0u);
        double t1 = now_s();
        fail += march_c_minus(m, WORDS, 0u);
        double t2 = now_s();
        if (t1 - t0 < best_m) best_m = t1 - t0;
        if (t2 - t1 < best_c) best_c = t2 - t1;
    }
    printf("words=%u (16 MiB), best of 5, fails=%u\n", WORDS, fail);
    printf("MATS+    5n: %7.2f ms  %.3f ns/access\n", best_m * 1e3, best_m * 1e9 / (5.0 * WORDS));
    printf("March C- 10n: %7.2f ms  %.3f ns/access\n", best_c * 1e3, best_c * 1e9 / (10.0 * WORDS));
    printf("ratio C-/MATS+ = %.2f (theory 2.00)\n", best_c / best_m);
    free(buf);
    return 0;
}
