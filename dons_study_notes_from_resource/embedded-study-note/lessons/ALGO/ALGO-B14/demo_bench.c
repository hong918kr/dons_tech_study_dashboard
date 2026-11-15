/* ALGO-B14 처리량 비교 (이 맥, -O2): 64 KiB 버퍼를 각 체크섬으로 반복 계산, 9회 중앙값.
 * 절대 숫자보다 "비율"을 볼 것. 컴파일러가 계산을 지우지 못하게 결과를 volatile sink 에 모은다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LEN  65536
#define REPS 200

static uint32_t b_sum8(const uint8_t *p, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); return s; }
static uint32_t b_sum8_wide(const uint8_t *p, size_t n) { uint32_t s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s & 0xFFu; }
static uint32_t b_xor8(const uint8_t *p, size_t n) { uint8_t x = 0; for (size_t i = 0; i < n; i++) x ^= p[i]; return x; }
static uint32_t b_inet(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i + 1 < n; i += 2) s += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}
static uint32_t b_f16_naive(const uint8_t *p, size_t n) {
    uint32_t a = 0, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 255u; b = (b + a) % 255u; }
    return b << 8 | a;
}
static uint32_t fold255(uint32_t x) {
    x = (x & 0xFFu) + (x >> 8); x = (x & 0xFFu) + (x >> 8);
    x = (x & 0xFFu) + (x >> 8); x = (x & 0xFFu) + (x >> 8);
    return x >= 255u ? x - 255u : x;
}
static uint32_t b_f16_fast(const uint8_t *p, size_t n) {
    uint32_t a = 0, b = 0;
    while (n) {
        size_t blk = n < 5802 ? n : 5802;
        n -= blk;
        do { a += *p++; b += a; } while (--blk);
        a = fold255(a); b = fold255(b);
    }
    return b << 8 | a;
}
static uint32_t b_f32(const uint8_t *p, size_t n) {
    uint32_t a = 0, b = 0;
    for (size_t i = 0; i < n; i += 2) {
        uint32_t w = p[i] | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0u);
        a = (a + w) % 65535u; b = (b + a) % 65535u;
    }
    return b << 16 | a;
}
static uint32_t b_crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

typedef uint32_t (*fn_t)(const uint8_t *, size_t);
static volatile uint32_t sink;

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(void) {
    static uint8_t buf[LEN];
    uint32_t s = 12345u;
    for (int i = 0; i < LEN; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; buf[i] = (uint8_t)s; }
    const fn_t F[] = { b_sum8, b_sum8_wide, b_xor8, b_inet, b_f16_naive, b_f16_fast, b_f32, b_crc16 };
    const char *N[] = { "sum8 (uint8_t acc)", "sum8 (uint32 acc)", "xor8", "inet16 (RFC 1071)",
                        "fletcher16 naive %255", "fletcher16 deferred", "fletcher32 %65535", "crc16 bitwise" };
    if (b_f16_fast(buf, LEN) != b_f16_naive(buf, LEN)) { printf("mismatch\n"); return 1; }
    printf("%-24s %10s %10s\n", "checksum (64 KiB)", "ns/byte", "GB/s");
    for (size_t f = 0; f < sizeof F / sizeof F[0]; f++) {
        double t[9];
        for (int r = 0; r < 2; r++) sink += F[f](buf, LEN);           /* warm-up */
        for (int k = 0; k < 9; k++) {
            double t0 = now_ns();
            for (int r = 0; r < REPS; r++) { buf[r & (LEN - 1)] ^= 1u; sink += F[f](buf, LEN); }
            t[k] = (now_ns() - t0) / ((double)REPS * LEN);
        }
        qsort(t, 9, sizeof t[0], cmpd);
        printf("%-24s %10.3f %10.2f\n", N[f], t[4], 1.0 / t[4]);
    }
    return 0;
}
