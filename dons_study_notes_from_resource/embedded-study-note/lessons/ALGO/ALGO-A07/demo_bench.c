/* ALGO-A07 demo — 속도 비교 (이 맥, -O2). 절대값보다 비율을 볼 것.
 *   1) strlen: 바이트 루프 · 워드 단위(haszero, 4 B) · libc strlen
 *   2) 길이를 아는 버퍼에서 '\n' 찾기: 바이트 루프 · 워드 단위 find_byte · libc memchr
 *   3) Gray 역변환: 31번 루프 vs 5단계 / Gosper: 나눗셈판 vs ctz 시프트판
 * 바이트 루프는 clang 이 strlen/memchr 로 바꾸지 못하게 빈 asm 장벽을 넣는다 (B12 의 함정).
 * 각 항목: 워밍업 1회 + 7회 측정의 중앙값, 결과는 volatile sink 로.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile uint64_t sink;
#define BARRIER(p) __asm__ volatile("" : "+r"(p))

static uint32_t haszero32(uint32_t v) { return (v - 0x01010101u) & ~v & 0x80808080u; }

static size_t strlen_byte(const char *s) {
    const char *p = s;
    while (*p) { p++; BARRIER(p); }
    return (size_t)(p - s);
}
static size_t strlen_word(const char *s) {
    const char *p = s;
    while ((uintptr_t)p & 3u) { if (*p == 0) return (size_t)(p - s); p++; }
    for (;;) {
        uint32_t w; memcpy(&w, p, 4);
        uint32_t h = haszero32(w);
        if (h) return (size_t)(p - s) + ((unsigned)__builtin_ctz(h) >> 3);
        p += 4;
    }
}
static size_t find_byte_byte(const uint8_t *b, size_t len, uint8_t c) {
    size_t i = 0;
    for (; i < len; i++) { if (b[i] == c) return i; BARRIER(i); }
    return len;
}
static size_t find_byte_word(const uint8_t *buf, size_t len, uint8_t c) {
    size_t i = 0;
    while (i < len && ((uintptr_t)(buf + i) & 3u)) { if (buf[i] == c) return i; i++; }
    uint32_t rep = 0x01010101u * c;
    for (; i + 4 <= len; i += 4) {
        uint32_t w; memcpy(&w, buf + i, 4);
        uint32_t h = haszero32(w ^ rep);
        if (h) return i + ((unsigned)__builtin_ctz(h) >> 3);
    }
    for (; i < len; i++) if (buf[i] == c) return i;
    return len;
}
static size_t find_byte_libc(const uint8_t *b, size_t len, uint8_t c) {
    const uint8_t *p = memchr(b, c, len);
    return p ? (size_t)(p - b) : len;
}

static uint32_t gray_dec_loop(uint32_t g) {
    uint32_t b = g;
    for (int i = 0; i < 31; i++) { g >>= 1; b ^= g; BARRIER(g); }
    return b;
}
static uint32_t gray_dec_log(uint32_t g) { g ^= g >> 16; g ^= g >> 8; g ^= g >> 4; g ^= g >> 2; g ^= g >> 1; return g; }
static uint32_t gosper_div(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    if (x == 0 || r == 0) return 0;
    return (((r ^ x) >> 2) / c) | r;
}
static uint32_t gosper_ctz(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    if (x == 0 || r == 0) return 0;
    return (((r ^ x) >> 2) >> __builtin_ctz(x)) | r;
}

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median7(double *t) { qsort(t, 7, sizeof *t, cmpd); return t[3]; }

static _Alignas(64) char sbuf[65536 + 64];

typedef size_t (*slen_fn)(const char *);
typedef size_t (*find_fn)(const uint8_t *, size_t, uint8_t);

static double bench_strlen(slen_fn f, size_t len, double *lo, double *hi) {
    memset(sbuf, 'a', len); sbuf[len] = 0;
    long reps = (long)(4000000 / (len + 16)) + 1;
    double t[7];
    for (int r = -1; r < 7; r++) {
        double t0 = now(); uint64_t acc = 0;
        for (long i = 0; i < reps; i++) { const char *p = sbuf; BARRIER(p); acc += f(p); }
        sink += acc;
        if (r >= 0) t[r] = (now() - t0) / (double)reps * 1e9;
    }
    double m = median7(t); *lo = t[0]; *hi = t[6];
    return m;
}
static double bench_find(find_fn f, size_t len, double *lo, double *hi) {
    memset(sbuf, 'a', len); sbuf[len - 1] = '\n';
    long reps = (long)(4000000 / (len + 16)) + 1;
    double t[7];
    for (int r = -1; r < 7; r++) {
        double t0 = now(); uint64_t acc = 0;
        for (long i = 0; i < reps; i++) { const uint8_t *p = (const uint8_t *)sbuf; BARRIER(p); acc += f(p, len, '\n'); }
        sink += acc;
        if (r >= 0) t[r] = (now() - t0) / (double)reps * 1e9;
    }
    double m = median7(t); *lo = t[0]; *hi = t[6];
    return m;
}

int main(void) {
    static const size_t lens[4] = {15, 255, 4095, 65535};
    printf("strlen (ns/call, median of 7 [min..max])\n");
    printf("   len |        byte loop        |       word (4 B)        |       libc strlen       | byte/word  word/libc\n");
    for (int i = 0; i < 4; i++) {
        double a0, a1, b0, b1, c0, c1;
        double a = bench_strlen(strlen_byte, lens[i], &a0, &a1);
        double b = bench_strlen(strlen_word, lens[i], &b0, &b1);
        double c = bench_strlen((slen_fn)strlen, lens[i], &c0, &c1);
        printf("%6zu | %8.1f [%6.1f..%7.1f] | %8.1f [%6.1f..%7.1f] | %8.1f [%6.1f..%7.1f] | %8.2f  %8.2f\n",
               lens[i], a, a0, a1, b, b0, b1, c, c0, c1, a / b, b / c);
    }
    printf("find '\\n' at the last byte of a len-byte buffer (ns/call)\n");
    printf("   len |        byte loop        |      find_byte_word     |       libc memchr       | byte/word  word/libc\n");
    for (int i = 0; i < 4; i++) {
        size_t L = lens[i] + 1;
        double a0, a1, b0, b1, c0, c1;
        double a = bench_find(find_byte_byte, L, &a0, &a1);
        double b = bench_find(find_byte_word, L, &b0, &b1);
        double c = bench_find(find_byte_libc, L, &c0, &c1);
        printf("%6zu | %8.1f [%6.1f..%7.1f] | %8.1f [%6.1f..%7.1f] | %8.1f [%6.1f..%7.1f] | %8.2f  %8.2f\n",
               L, a, a0, a1, b, b0, b1, c, c0, c1, a / b, b / c);
    }

    const int N = 1 << 22;
    double t[4][7];
    for (int r = -1; r < 7; r++) {
        uint32_t x, acc;
        double t0 = now(); acc = 0; x = 0x12345u;
        for (int i = 0; i < N; i++) { acc += gray_dec_loop(x); x += 0x9E3779B9u; }
        sink += acc; if (r >= 0) t[0][r] = (now() - t0) / N * 1e9;
        t0 = now(); acc = 0; x = 0x12345u;
        for (int i = 0; i < N; i++) { acc += gray_dec_log(x); x += 0x9E3779B9u; BARRIER(x); }
        sink += acc; if (r >= 0) t[1][r] = (now() - t0) / N * 1e9;
        t0 = now(); x = 0xFFFu;                                        /* 12비트 조합 사슬을 따라간다 */
        for (int i = 0; i < N; i++) { x = gosper_div(x); if (!x) x = 0xFFFu; }
        sink += x; if (r >= 0) t[2][r] = (now() - t0) / N * 1e9;
        t0 = now(); x = 0xFFFu;
        for (int i = 0; i < N; i++) { x = gosper_ctz(x); if (!x) x = 0xFFFu; }
        sink += x; if (r >= 0) t[3][r] = (now() - t0) / N * 1e9;
    }
    const char *nm[4] = {"gray_dec loop(31)", "gray_dec 5-step  ", "gosper (udiv)    ", "gosper (ctz)     "};
    printf("per-value (ns, median of 7 [min..max]; gosper = dependent chain, latency-bound)\n");
    for (int k = 0; k < 4; k++) {
        double m = median7(t[k]);
        printf("  %s %6.2f [%5.2f..%5.2f]\n", nm[k], m, t[k][0], t[k][6]);
    }
    return 0;
}
