/* DS-M14 관찰용: 더블 해싱의 거짓 양성 "바닥(floor)"
 * 질의 키 y 의 (h1, h2) 가 들어 있는 키 x 와 mod m 으로 같으면(확률 ~ 2/m^2: h1 1/m, 홀수 h2 2/m)
 * y 의 k 위치가 x 의 k 위치와 통째로 같다 → 다른 비트와 무관하게 거짓 양성.
 * 그래서 FP >= 대략 2n/m^2. 목표 FP 가 이보다 훨씬 크면 무시, 비슷하거나 작으면 문제.
 * 비교: double h1+i*h2 / k 개 독립 해시 / enhanced double hashing h1+i*h2+(i^3-i)/6
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_dh_floor.c -o demo_dh_floor && ./demo_dh_floor
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x00000100000001B3ull; }
    return h;
}
static uint64_t fmix64(uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return x;
}

enum { DOUBLE, INDEP, ENHANCED };
static const char *mode_name[] = { "double   ", "indep    ", "enhanced " };

static uint32_t pos(int mode, uint32_t key, uint32_t i) {
    uint64_t h = fmix64(fnv1a64(&key, sizeof key));
    uint32_t h1 = (uint32_t)h, h2 = (uint32_t)(h >> 32) | 1u;
    if (mode == DOUBLE) return h1 + i * h2;
    if (mode == ENHANCED) return h1 + i * h2 + (i * i * i - i) / 6u;   /* Dillinger & Manolios */
    return (uint32_t)fmix64(((uint64_t)i << 32 | key) * 0x9E3779B97F4A7C15ull + i);
}

static uint32_t g_w[65536 / 32];

int main(void) {
    static const uint32_t cfg[][3] = { { 500, 16384, 7 }, { 1000, 65536, 15 }, { 8192, 65536, 6 } };
    const uint32_t Q = 20000000;
    printf("    n      m   k | mode      | FP measured (hits/20M) | rho^k     | (1-e^(-kn/m))^k | 2n/m^2\n");
    for (unsigned c = 0; c < 3; c++) {
        uint32_t n = cfg[c][0], m = cfg[c][1], k = cfg[c][2];
        for (int mode = 0; mode < 3; mode++) {
            memset(g_w, 0, (m / 32u) * sizeof g_w[0]);
            for (uint32_t x = 0; x < n; x++)
                for (uint32_t i = 0; i < k; i++) { uint32_t g = pos(mode, x, i) & (m - 1u); g_w[g >> 5] |= 1u << (g & 31u); }
            uint32_t X = 0;
            for (uint32_t i = 0; i < m / 32u; i++) X += (uint32_t)__builtin_popcount(g_w[i]);
            uint32_t hit = 0;
            for (uint32_t q = 0; q < Q; q++) {
                uint32_t key = 100000000u + q, all = 1;
                for (uint32_t i = 0; i < k && all; i++) { uint32_t g = pos(mode, key, i) & (m - 1u); all = (g_w[g >> 5] >> (g & 31u)) & 1u; }
                hit += all;
            }
            char fp[32];
            snprintf(fp, sizeof fp, "%.3g (%u)", (double)hit / Q, hit);
            printf("%5u %6u %3u | %s | %-22s | %-9.3g | %-15.3g | %.3g\n", n, m, k, mode_name[mode], fp,
                   pow((double)X / m, k), pow(1.0 - exp(-(double)k * n / m), k), 2.0 * n / ((double)m * m));
        }
    }
    return 0;
}
