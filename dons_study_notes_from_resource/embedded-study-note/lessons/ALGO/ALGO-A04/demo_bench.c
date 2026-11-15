/* ALGO-A04 demo_bench — 스트리밍 매처 처리량 (ns/바이트), 같은 push(byte) 인터페이스로 비교
 *   naive-ring : 마지막 m 바이트 링을 매 바이트 패턴과 비교 (O(m)/바이트)
 *   kmp-mp     : 실패 함수 (Morris–Pratt 링크)          kmp-strong : KMP 의 next (같은 비교 건너뜀)
 *   dfa        : (m+1)×256 표, 바이트당 표 1번         rk32 / rkp : 롤링 해시 + verify
 *   shift64    : m ≤ 8 전용 시프트 레지스터
 * 텍스트 3종: 무작위 바이트, MP 최악(p=a^32, (a^31 b)*), UART 잡음(0x55/0x7E 많음, 프리앰블 m=3).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 199309L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N (8u << 20)
#define RUNS 11
static uint8_t T[N];
static uint32_t g_rng = 12345u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static const uint8_t *P; static int M;
static uint8_t FAIL[256]; static int16_t STRONG[256]; static uint8_t DFA[257 * 256];
#define RK_P 2147483647u
static uint32_t mod_p(uint64_t x) { x = (x & RK_P) + (x >> 31); x = (x & RK_P) + (x >> 31); return (uint32_t)(x >= RK_P ? x - RK_P : x); }

static void build(void) {
    FAIL[0] = 0;
    for (int i = 1, k = 0; i < M; i++) { while (k > 0 && P[i] != P[k]) k = FAIL[k - 1]; if (P[i] == P[k]) k++; FAIL[i] = (uint8_t)k; }
    STRONG[0] = -1;
    for (int q = 1; q < M; q++) { int r = FAIL[q - 1]; STRONG[q] = (P[r] == P[q]) ? STRONG[r] : (int16_t)r; }
    memset(DFA, 0, 256); DFA[P[0]] = 1;
    for (int q = 1; q <= M; q++) { memcpy(DFA + 256 * q, DFA + 256 * FAIL[q - 1], 256); if (q < M) DFA[256 * q + P[q]] = (uint8_t)(q + 1); }
}

static uint64_t run_naive(void) {
    uint8_t ring[256] = {0}; unsigned h = 0; uint64_t c = 0;
    for (uint32_t i = 0; i < N; i++) {
        ring[h] = T[i]; h = (h + 1 == (unsigned)M) ? 0 : h + 1;
        if (i + 1 < (uint32_t)M) continue;
        unsigned j = h; int k = 0;
        while (k < M && ring[j] == P[k]) { k++; j = (j + 1 == (unsigned)M) ? 0 : j + 1; }
        c += (k == M);
    }
    return c;
}
static uint64_t run_mp(void) {
    unsigned q = 0; uint64_t c = 0;
    for (uint32_t i = 0; i < N; i++) {
        uint8_t b = T[i];
        while (q > 0 && b != P[q]) q = FAIL[q - 1];
        if (b == P[q]) q++;
        if (q == (unsigned)M) { c++; q = FAIL[q - 1]; }
    }
    return c;
}
static uint64_t run_strong(void) {
    int q = 0; uint64_t c = 0;
    for (uint32_t i = 0; i < N; i++) {
        uint8_t b = T[i];
        if (q == M) q = FAIL[M - 1];
        int x = q;
        while (x >= 0 && P[x] != b) x = STRONG[x];
        q = x + 1;
        c += (q == M);
    }
    return c;
}
static uint64_t run_dfa(void) {
    unsigned q = 0; uint64_t c = 0;
    for (uint32_t i = 0; i < N; i++) { q = DFA[256 * q + T[i]]; c += (q == (unsigned)M); }
    return c;
}
static uint64_t run_rk(bool modp) {
    const uint32_t B = modp ? 1000003u : 0x01000193u;
    uint32_t hp = 0, bm1 = 1, h = 0; uint8_t ring[256] = {0}; unsigned head = 0; uint64_t c = 0;
    for (int i = 0; i < M; i++) hp = modp ? mod_p((uint64_t)hp * B + P[i]) : hp * B + P[i];
    for (int i = 1; i < M; i++) bm1 = modp ? mod_p((uint64_t)bm1 * B) : bm1 * B;
    for (uint32_t i = 0; i < N; i++) {
        uint8_t out = ring[head], b = T[i];
        ring[head] = b; head = (head + 1 == (unsigned)M) ? 0 : head + 1;
        if (modp) h = mod_p((uint64_t)(h + RK_P - mod_p((uint64_t)out * bm1)) * B + b);
        else h = (h - (uint32_t)out * bm1) * B + b;
        if (i + 1 >= (uint32_t)M && h == hp) {
            unsigned j = head; int k = 0;
            while (k < M && ring[j] == P[k]) { k++; j = (j + 1 == (unsigned)M) ? 0 : j + 1; }
            c += (k == M);
        }
    }
    return c;
}
static uint64_t run_rk32(void) { return run_rk(false); }
static uint64_t run_rkp(void) { return run_rk(true); }
static uint64_t run_shift(void) {
    uint64_t w = 0, pat = 0, mask = M == 8 ? ~0ull : ((1ull << (8 * M)) - 1), c = 0;
    for (int i = 0; i < M; i++) pat = (pat << 8) | P[i];
    for (uint32_t i = 0; i < N; i++) { w = (w << 8) | T[i]; c += (i + 1 >= (uint32_t)M) & ((w & mask) == pat); }
    return c;
}

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static volatile uint64_t sink;

static void bench(const char *name, uint64_t (*f)(void), double *med_out) {
    double t[RUNS]; uint64_t c = f();                     /* 워밍업 */
    for (int r = 0; r < RUNS; r++) { double t0 = now(); uint64_t c2 = f(); t[r] = (now() - t0) * 1e9 / N; if (c2 != c) printf("!! count changed\n"); }
    sink += c;
    qsort(t, RUNS, sizeof t[0], cmpd);
    *med_out = t[RUNS / 2];
    printf("  %-11s matches=%-8llu ns/byte median=%.3f  (min %.3f, max %.3f)\n", name, (unsigned long long)c, t[RUNS / 2], t[0], t[RUNS - 1]);
}

int main(void) {
    struct { const char *name; uint64_t (*f)(void); } m[] = {
        {"naive-ring", run_naive}, {"kmp-mp", run_mp}, {"kmp-strong", run_strong}, {"dfa", run_dfa},
        {"rk32", run_rk32}, {"rkp", run_rkp}, {"shift64", run_shift}};
    static uint8_t pr[8], pa[32], pre[3] = {0x55, 0x55, 0x7E};
    for (int s = 0; s < 3; s++) {
        if (s == 0) { for (uint32_t i = 0; i < N; i++) T[i] = (uint8_t)xs32(); for (int i = 0; i < 8; i++) pr[i] = T[1000 + i]; P = pr; M = 8;
                      for (int r = 0; r < 64; r++) memcpy(T + (xs32() % (N - 8)), pr, 8);
                      printf("[random bytes, m=8 random pattern (+64 planted)]\n"); }
        if (s == 1) { memset(pa, 'a', 32); P = pa; M = 32;
                      for (uint32_t i = 0; i < N; i++) T[i] = (i % 32 == 31) ? 'b' : 'a';
                      printf("[MP worst: p=a^32, text=(a^31 b)*]\n"); }
        if (s == 2) { P = pre; M = 3;
                      for (uint32_t i = 0; i < N; i++) { uint32_t r = xs32() % 100; T[i] = r < 35 ? 0x55 : r < 45 ? 0x7E : (uint8_t)xs32(); }
                      printf("[UART noise 35%% 0x55, 10%% 0x7E, preamble 55 55 7E]\n"); }
        build();
        double base = 0, med;
        for (unsigned k = 0; k < sizeof m / sizeof m[0]; k++) {
            if (m[k].f == run_shift && M > 8) { printf("  %-11s (m > 8: n/a)\n", m[k].name); continue; }
            bench(m[k].name, m[k].f, &med);
            if (k == 3) base = med;
        }
        printf("  (dfa median = %.3f ns/byte is the ratio base)\n", base);
    }
    return 0;
}
