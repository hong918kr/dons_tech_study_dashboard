/* ALGO-M02 처리량 비교 (이 맥, Apple Silicon, -O2): CRC-32/ISO-HDLC 구현별 MB/s 와 테이블 메모리.
 * 버퍼 크기 64 B / 1 KiB / 64 KiB, 크기마다 약 2 MiB 를 한 표본으로, 워밍업 후 9 표본 중앙값.
 * 절대 숫자보다 "비율"을 볼 것. 결과는 volatile sink 에 모아 컴파일러가 계산을 지우지 못하게 한다.
 * 모든 구현은 시작 전에 zlib crc32() 와 값이 같은지 확인한다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -lz -o demo_bench && ./demo_bench
 * (ARMv8 CRC32 명령 행은 __ARM_FEATURE_CRC32 가 정의된 경우만 — Apple arm64 기본값에서 정의됨)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <zlib.h>
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

#define POLY_R 0xEDB88320u
static uint32_t T[8][256], TN[16];

static void init_tables(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (POLY_R & (0u - (c & 1u)));
        T[0][i] = c;
        if (i < 16) { c = i; for (int k = 0; k < 4; k++) c = (c >> 1) ^ (POLY_R & (0u - (c & 1u))); TN[i] = c; }
    }
    for (int k = 1; k < 8; k++)
        for (int i = 0; i < 256; i++) T[k][i] = (T[k - 1][i] >> 8) ^ T[0][T[k - 1][i] & 0xFFu];
}
static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t c_bitwise(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (POLY_R & (0u - (c & 1u))); }
    return ~c;
}
static uint32_t c_nibble(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; c = TN[c & 0xFu] ^ (c >> 4); c = TN[c & 0xFu] ^ (c >> 4); }
    return ~c;
}
static uint32_t c_table(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}
static uint32_t c_slice4(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    for (; n >= 4; p += 4, n -= 4) {
        c ^= le32(p);
        c = T[3][c & 0xFFu] ^ T[2][(c >> 8) & 0xFFu] ^ T[1][(c >> 16) & 0xFFu] ^ T[0][c >> 24];
    }
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}
static uint32_t c_slice8(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    for (; n >= 8; p += 8, n -= 8) {
        uint32_t lo = c ^ le32(p), hi = le32(p + 4);
        c = T[7][lo & 0xFFu] ^ T[6][(lo >> 8) & 0xFFu] ^ T[5][(lo >> 16) & 0xFFu] ^ T[4][lo >> 24]
          ^ T[3][hi & 0xFFu] ^ T[2][(hi >> 8) & 0xFFu] ^ T[1][(hi >> 16) & 0xFFu] ^ T[0][hi >> 24];
    }
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}
static uint32_t c_zlib(const uint8_t *p, size_t n) { return (uint32_t)crc32(0, p, (uInt)n); }
#if defined(__ARM_FEATURE_CRC32)
static uint32_t c_hw_b(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while (n--) c = __crc32b(c, *p++);
    return ~c;
}
static uint32_t c_hw_d(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    for (; n >= 8; p += 8, n -= 8) {
        uint64_t w = (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
        c = __crc32d(c, w);
    }
    while (n--) c = __crc32b(c, *p++);
    return ~c;
}
#endif

typedef uint32_t (*fn_t)(const uint8_t *, size_t);
static volatile uint32_t sink;
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(void) {
    static uint8_t buf[65536];
    uint32_t s = 0xBEEF02u;
    for (size_t i = 0; i < sizeof buf; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; buf[i] = (uint8_t)s; }
    init_tables();
    struct { fn_t f; const char *name; unsigned tab; } F[] = {
        { c_bitwise, "bitwise (8 steps/byte)", 0 },
        { c_nibble,  "nibble table (16 ent)",  sizeof TN },
        { c_table,   "byte table (Sarwate)",   sizeof T[0] },
        { c_slice4,  "slicing-by-4",           4 * sizeof T[0] },
        { c_slice8,  "slicing-by-8",           sizeof T },
        { c_zlib,    "zlib crc32()",           0 },
#if defined(__ARM_FEATURE_CRC32)
        { c_hw_b,    "ARMv8 __crc32b",         0 },
        { c_hw_d,    "ARMv8 __crc32d",         0 },
#endif
    };
    const size_t NF = sizeof F / sizeof F[0];
    for (size_t f = 0; f < NF; f++)
        for (size_t n = 0; n <= 300; n += 7)
            if (F[f].f(buf + 3, n) != c_zlib(buf + 3, n)) { printf("MISMATCH %s n=%zu\n", F[f].name, n); return 1; }
    printf("zlib %s, all %zu implementations agree with zlib crc32() on len 0..300\n", zlibVersion(), NF);

    const size_t SZ[3] = { 64, 1024, 65536 };
    printf("%-24s %9s %10s %10s %10s\n", "implementation", "table B", "64 B MB/s", "1 KiB MB/s", "64 KiB MB/s");
    for (size_t f = 0; f < NF; f++) {
        double mbps[3];
        for (int z = 0; z < 3; z++) {
            size_t n = SZ[z], reps = (2u << 20) / n;
            double t[9];
            for (size_t r = 0; r < reps / 4 + 1; r++) sink += F[f].f(buf, n);      /* warm-up */
            for (int k = 0; k < 9; k++) {
                double t0 = now_ns();
                for (size_t r = 0; r < reps; r++) { buf[r & 63] ^= 1u; sink += F[f].f(buf, n); }
                t[k] = now_ns() - t0;
            }
            qsort(t, 9, sizeof t[0], cmpd);
            mbps[z] = (double)(reps * n) / (t[4] / 1e9) / 1e6;
        }
        printf("%-24s %9u %10.0f %10.0f %10.0f\n", F[f].name, F[f].tab, mbps[0], mbps[1], mbps[2]);
    }
    return 0;
}
