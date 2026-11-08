/* SIV-M05 demo — 호스트 DRAM 에서 스트레스 패턴 처리량 측정 (mock 없음, 체크 없음: 관찰용)
 *   sequential(solid / checker / own-address) vs LFSR 무작위 주소 순서
 * build: cc -std=c11 -Wall -Wextra -O2 demo_stress_bench.c -o demo_stress_bench && ./demo_stress_bench [log2_words]
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* Galois LFSR 마스크 (XAPP052 탭): n -> mask */
static uint32_t lfsr_mask(unsigned n) {
    switch (n) {
    case 20: return (1u << 19) | (1u << 16);
    case 22: return (1u << 21) | (1u << 20);
    case 24: return (1u << 23) | (1u << 22) | (1u << 21) | (1u << 16);
    case 26: return (1u << 25) | (1u << 5) | (1u << 1) | 1u;
    default: return 0;
    }
}

int main(int argc, char **argv) {
    unsigned nb = argc > 1 ? (unsigned)atoi(argv[1]) : 26u;
    if (!lfsr_mask(nb)) { fprintf(stderr, "log2_words must be 20/22/24/26\n"); return 2; }
    uint32_t n = 1u << nb, mask = lfsr_mask(nb), errs = 0;
    uint32_t *raw = malloc((size_t)n * 4u);
    if (!raw) return 2;
    volatile uint32_t *m = raw;   /* volatile: 컴파일러가 "방금 쓴 값" 이라며 읽기·비교를 지우지 못하게 (memtester 도 같은 방식) */
    printf("buffer = %" PRIu32 " words (%" PRIu32 " MiB), write pass + read/compare pass each, best of 3\n", n, n >> 18);

    for (uint32_t w = 0; w < n; w++) m[w] = 0x01010101u;     /* 첫 접근 page fault 를 측정에서 뺀다 (0 이면 calloc 으로 바뀔 수 있음) */
    double best[4] = { 1e9, 1e9, 1e9, 1e9 };
    uint32_t visited = 0;
    for (int rep = 0; rep < 3; rep++) {                       /* 패턴별 3회 중 최소값 */
        double t0 = now_s();
        for (uint32_t w = 0; w < n; w++) m[w] = 0xFFFFFFFFu;
        for (uint32_t w = 0; w < n; w++) errs += m[w] != 0xFFFFFFFFu;
        double t = now_s() - t0; if (t < best[0]) best[0] = t;

        t0 = now_s();
        for (uint32_t w = 0; w < n; w++) m[w] = (w & 1u) ? 0xAAAAAAAAu : 0x55555555u;
        for (uint32_t w = 0; w < n; w++) errs += m[w] != ((w & 1u) ? 0xAAAAAAAAu : 0x55555555u);
        t = now_s() - t0; if (t < best[1]) best[1] = t;

        t0 = now_s();
        for (uint32_t w = 0; w < n; w++) m[w] = 0x80000000u + (w << 2);
        for (uint32_t w = 0; w < n; w++) errs += m[w] != 0x80000000u + (w << 2);
        t = now_s() - t0; if (t < best[2]) best[2] = t;

        uint32_t ds = 0x2545F491u, s = 1u;
        visited = 1;
        t0 = now_s();
        m[0] = xorshift32(&ds);
        do { m[s] = xorshift32(&ds); s = (s >> 1) ^ ((0u - (s & 1u)) & mask); visited++; } while (s != 1u);
        ds = 0x2545F491u;
        errs += m[0] != xorshift32(&ds);
        do { errs += m[s] != xorshift32(&ds); s = (s >> 1) ^ ((0u - (s & 1u)) & mask); } while (s != 1u);
        t = now_s() - t0; if (t < best[3]) best[3] = t;
    }
    double t_solid = best[0], t_chk = best[1], t_oa = best[2], t_lfsr = best[3];

    double bytes = 2.0 * n * 4.0;   /* write + read */
    printf("%-10s %9s %9s\n", "pattern", "ms", "GB/s");
    printf("%-10s %9.1f %9.2f\n", "solid", t_solid * 1e3, bytes / t_solid / 1e9);
    printf("%-10s %9.1f %9.2f\n", "checker", t_chk * 1e3, bytes / t_chk / 1e9);
    printf("%-10s %9.1f %9.2f\n", "own-addr", t_oa * 1e3, bytes / t_oa / 1e9);
    printf("%-10s %9.1f %9.2f   (visited %" PRIu32 " words)\n", "lfsr", t_lfsr * 1e3, bytes / t_lfsr / 1e9, visited);
    printf("lfsr/sequential(solid) time ratio = %.1fx, errors=%" PRIu32 "\n", t_lfsr / t_solid, errs);
    free(raw);
    return 0;
}
