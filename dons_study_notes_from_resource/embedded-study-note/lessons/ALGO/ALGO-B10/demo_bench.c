/* ALGO-B10 demo: O(1) 누적합 vs O(N) 매번 다시 더하기, O(n) 슬라이딩 vs O(n^2) 브루트포스
 *
 * 각 측정 = 워밍업 1회 + 7회 중 중앙값. 결과는 체크섬으로 volatile sink 에 넣어 컴파일러가 지우지 못하게.
 * 이 맥(Apple Silicon, -O2) 기준 — 절대 ns 보다 비율을 볼 것.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static volatile uint64_t g_sink;

#define LEN 1000000u
static uint16_t g_x[LEN];
static uint16_t g_ring[4096];

/* O(1)/샘플: 링 버퍼 + 누적합 */
static uint64_t run_running(uint32_t n) {
    uint32_t sum = 0, head = 0, count = 0; uint64_t chk = 0;
    for (uint32_t i = 0; i < LEN; i++) {
        if (count == n) sum -= g_ring[head]; else count++;
        g_ring[head] = g_x[i]; sum += g_x[i];
        if (++head == n) head = 0;
        chk += (sum + count / 2) / count;
    }
    return chk;
}
/* O(N)/샘플: 링 버퍼는 같고 합은 매번 처음부터 */
static uint64_t run_recompute(uint32_t n) {
    uint32_t head = 0, count = 0; uint64_t chk = 0;
    for (uint32_t i = 0; i < LEN; i++) {
        if (count < n) count++;
        g_ring[head] = g_x[i];
        if (++head == n) head = 0;
        uint32_t sum = 0;
        for (uint32_t j = 0; j < count; j++) sum += g_ring[j];
        chk += (sum + count / 2) / count;
    }
    return chk;
}

static size_t sum_le_window(const uint16_t *a, size_t n, uint32_t S) {
    uint64_t sum = 0; size_t l = 0, best = 0;
    for (size_t r = 0; r < n; r++) {
        sum += a[r];
        while (sum > S) sum -= a[l++];
        if (r + 1 - l > best) best = r + 1 - l;
    }
    return best;
}
static size_t sum_le_brute(const uint16_t *a, size_t n, uint32_t S) {
    size_t best = 0;
    for (size_t l = 0; l < n; l++) {
        uint64_t s = 0;
        for (size_t r = l; r < n; r++) { s += a[r]; if (s <= S && r + 1 - l > best) best = r + 1 - l; }
    }
    return best;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

#define MEDIAN_OF(expr, out) do { double t_[7]; (void)(expr); \
    for (int k_ = 0; k_ < 7; k_++) { double t0_ = now_ns(); g_sink += (uint64_t)(expr); t_[k_] = now_ns() - t0_; } \
    qsort(t_, 7, sizeof t_[0], cmp_d); out = t_[3]; } while (0)

int main(void) {
    uint32_t seed = 0xBE7Cu;
    for (uint32_t i = 0; i < LEN; i++) g_x[i] = (uint16_t)(xs32(&seed) & 0x0FFF);

    printf("moving average over %u samples (ns per sample, median of 7)\n", LEN);
    printf("%6s %12s %14s %8s\n", "N", "running O(1)", "recompute O(N)", "ratio");
    const uint32_t ns[] = { 4, 16, 64, 256, 1024, 4096 };
    for (size_t t = 0; t < sizeof ns / sizeof ns[0]; t++) {
        double a, b;
        MEDIAN_OF(run_running(ns[t]), a);
        MEDIAN_OF(run_recompute(ns[t]), b);
        if (run_running(ns[t]) != run_recompute(ns[t])) printf("MISMATCH at N=%u\n", ns[t]);
        printf("%6u %12.2f %14.2f %8.1f\n", ns[t], a / LEN, b / LEN, b / a);
    }

    printf("\nlongest sum<=S (values 0..4095, S = 50*2048): total ms, median of 7\n");
    printf("%7s %12s %12s %8s\n", "n", "window O(n)", "brute O(n^2)", "ratio");
    const size_t sz[] = { 1000, 4000, 16000 };
    for (size_t t = 0; t < 3; t++) {
        double a, b;
        uint32_t S = 50u * 2048u;
        MEDIAN_OF(sum_le_window(g_x, sz[t], S), a);
        MEDIAN_OF(sum_le_brute(g_x, sz[t], S), b);
        if (sum_le_window(g_x, sz[t], S) != sum_le_brute(g_x, sz[t], S)) printf("MISMATCH n=%zu\n", sz[t]);
        printf("%7zu %12.4f %12.2f %8.0f\n", sz[t], a / 1e6, b / 1e6, b / a);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
