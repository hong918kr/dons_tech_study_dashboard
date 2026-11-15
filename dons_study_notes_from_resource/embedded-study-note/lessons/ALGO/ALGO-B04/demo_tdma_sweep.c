/* ALGO-B04 demo: Part C 시나리오에서 슬롯 수 D 를 바꾸면 최소 슬롯 페이로드·보드레이트가 어떻게 바뀌나
 *   + 판정 함수 호출 횟수(이분 탐색 probe 수) vs 선형 탐색(max 부터 1씩 올리기)의 호출 횟수
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_tdma_sweep.c -o demo_sweep && ./demo_sweep
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static size_t frames_needed(const uint32_t *sz, size_t n, uint64_t cap) {
    size_t frames = 0; uint64_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (sz[i] > cap) return SIZE_MAX;
        if (frames == 0 || used + sz[i] > cap) { frames++; used = 0; }
        used += sz[i];
    }
    return frames;
}
static unsigned long g_calls;
static bool fits(const uint32_t *sz, size_t n, size_t D, uint64_t cap) { g_calls++; return frames_needed(sz, n, cap) <= D; }
static uint64_t min_frame_capacity(const uint32_t *sz, size_t n, size_t D) {
    uint64_t lo = 0, hi = 0;
    for (size_t i = 0; i < n; i++) { if (sz[i] > lo) lo = sz[i]; hi += sz[i]; }
    while (lo < hi) { uint64_t mid = lo + (hi - lo) / 2; if (fits(sz, n, D, mid)) hi = mid; else lo = mid + 1; }
    return lo;
}
static uint64_t slot_bytes(uint32_t baud, uint32_t slot_us) { return (uint64_t)baud * slot_us / 10000000u; }

int main(void) {
    static const uint32_t rec[] = {28, 12, 28, 72, 28, 16, 28, 12, 28, 64, 28, 16,
                                   28, 12, 28, 40, 28, 16, 28, 12, 28, 72, 28, 16};
    const size_t n = sizeof rec / sizeof rec[0];
    static const uint32_t std_baud[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000, 3000000};
    const size_t nb = sizeof std_baud / sizeof std_baud[0];
    const uint32_t slot_us = 1000;
    uint64_t sum = 0; for (size_t i = 0; i < n; i++) sum += rec[i];
    printf("records n=%zu, sum=%llu B, max=72 B, slot=%u us, UART 8N1\n", n, (unsigned long long)sum, slot_us);
    printf("  D | cap B | probes | linear probes | min baud | std pick | slot B @pick\n");
    for (size_t D = 1; D <= 24; D++) {
        if (!(D <= 4 || D == 6 || D == 7 || D == 8 || D == 10 || D == 12 || D == 16 || D == 24)) continue;
        g_calls = 0;
        uint64_t cap = min_frame_capacity(rec, n, D);
        unsigned long probes = g_calls;
        unsigned long lin = 0;
        for (uint64_t c = 72;; c++) { lin++; if (frames_needed(rec, n, c) <= D) break; }
        uint64_t mb = (cap * 10000000u + slot_us - 1) / slot_us;
        size_t i = 0; while (i < nb && slot_bytes(std_baud[i], slot_us) < cap) i++;
        printf(" %2zu | %5llu | %6lu | %13lu | %8llu | %8u | %llu\n", D, (unsigned long long)cap, probes, lin,
               (unsigned long long)mb, i < nb ? std_baud[i] : 0u, i < nb ? (unsigned long long)slot_bytes(std_baud[i], slot_us) : 0ull);
    }
    return 0;
}
