/* ARCH-B06 demo_lfsr — 16비트 LFSR 전수 실험 (시간 측정 포함, CHECK 없음)
 *   1) 몇 개 mask 의 사이클 구조 (cycle census)
 *   2) bit15 가 켜진 32768 개 Galois mask 전부: 주기 65535 인 것(최대 길이)이 몇 개인가 — 이론값 φ(65535)/16 = 2048
 *   3) Galois(0xB400) 출력 비트열 vs Fibonacci(탭 0x2D) 출력 비트열: 같은 m-sequence 의 위상 차이인가
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_lfsr.c -o demo_lfsr && ./demo_lfsr
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint16_t galois_step(uint16_t s, uint16_t m) { unsigned o = s & 1u; s = (uint16_t)(s >> 1); if (o) s ^= m; return s; }
static uint16_t fib_step(uint16_t s, uint16_t taps) {
    unsigned x = s & taps, fb = 0;
    while (x) { fb ^= x & 1u; x >>= 1; }
    return (uint16_t)((s >> 1) | (fb << 15));
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }

static uint8_t seen[65536];
static void census(const char *name, uint16_t m) {
    memset(seen, 0, sizeof seen);
    unsigned cycles = 0, longest = 0, hist_len[8] = {0}, hist_cnt[8] = {0}, nh = 0;
    for (uint32_t s0 = 0; s0 < 65536u; s0++) {
        if (seen[s0]) continue;
        uint16_t s = (uint16_t)s0; unsigned len = 0;
        do { seen[s] = 1; s = galois_step(s, m); len++; } while (s != (uint16_t)s0);
        cycles++; if (len > longest) longest = len;
        unsigned k; for (k = 0; k < nh && hist_len[k] != len; k++) {}
        if (k == nh && nh < 8) { hist_len[nh] = len; nh++; }
        if (k < 8) hist_cnt[k]++;
    }
    printf("%-28s mask 0x%04X  cycles %5u  longest %5u  [", name, m, cycles, longest);
    for (unsigned k = 0; k < nh; k++) printf("%s%u x%u", k ? ", " : "", hist_len[k], hist_cnt[k]);
    printf("]\n");
}

static uint8_t gb[65535], fb_[65535];

int main(void) {
    double t0 = now_s();
    census("x^16+x^14+x^13+x^11+1", 0xB400);
    census("CRC-16-CCITT x^16+x^12+x^5+1", 0x8810);
    census("x^16+1 (rotate)", 0x8000);
    census("x^16+x^15+1", 0xC000);
    printf("census of 4 masks (4 x 65536 states): %.2f ms\n\n", (now_s() - t0) * 1e3);

    t0 = now_s();
    unsigned maximal = 0; uint16_t first[6]; unsigned nf = 0;
    for (uint32_t m = 0x8000; m <= 0xFFFF; m++) {
        uint16_t s = 1; uint32_t n = 0;
        do { s = galois_step(s, (uint16_t)m); n++; } while (s != 1);
        if (n == 65535u) { maximal++; if (nf < 6) first[nf++] = (uint16_t)m; }
    }
    printf("masks 0x8000..0xFFFF with period 65535: %u (theory phi(65535)/16 = 2048)  %.2f s\n",
           maximal, now_s() - t0);
    printf("first few: ");
    for (unsigned k = 0; k < nf; k++) printf("0x%04X ", first[k]);
    printf("\n\n");

    uint16_t s = 1;
    for (unsigned i = 0; i < 65535u; i++) { gb[i] = (uint8_t)(s & 1u); s = galois_step(s, 0xB400); }
    s = 1;
    for (unsigned i = 0; i < 65535u; i++) { fb_[i] = (uint8_t)(s & 1u); s = fib_step(s, 0x002D); }
    long found = -1;
    for (unsigned k = 0; k < 65535u && found < 0; k++) {
        unsigned i;
        for (i = 0; i < 65535u; i++) if (gb[i] != fb_[(i + k) % 65535u]) break;
        if (i == 65535u) found = (long)k;
    }
    printf("Galois(0xB400, seed 1) output == Fibonacci(0x2D, seed 1) output shifted by %ld bits\n", found);
    unsigned ones = 0; for (unsigned i = 0; i < 65535u; i++) ones += gb[i];
    printf("ones in one period: %u, zeros: %u\n", ones, 65535u - ones);
    printf("first 32 output bits (Fibonacci, seed 1): ");
    for (unsigned i = 0; i < 32; i++) printf("%u", fb_[i]);
    printf("\n");
    return 0;
}
