/* ARM-B03 demo_exhaustive32.c — 32비트 값 2^32 개 "전부" 를 is_logical_imm32 에 넣어 본다.
 *   cc -std=c11 -O2 -pthread demo_exhaustive32.c -o ex32 && ./ex32
 * 논리: (true 라고 답한 값) 이 모두 DecodeBitMasks 집합 안에 있고, 그 개수 == 집합 크기(1302) 이면
 *       두 집합은 같다 -> 양방향 전수 검증. (immr/imms 도 디코드해서 원래 값으로 돌아오는지 확인)
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* ---- solution.c 와 같은 코드 ---- */
static unsigned popcount64(uint64_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }
static uint64_t ror_e(uint64_t x, unsigned r, unsigned e) {
    uint64_t m = (e == 64) ? ~0ull : ((1ull << e) - 1);
    x &= m; r %= e;
    return r ? (((x >> r) | (x << (e - r))) & m) : x;
}
static bool is_logical_imm64(uint64_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    if (v == 0 || v == ~0ull) return false;
    unsigned e = 64;
    while (e > 2) {
        unsigned h = e / 2; uint64_t m = (1ull << h) - 1;
        if ((v & m) != ((v >> h) & m)) break;
        e = h;
    }
    uint64_t elem = (e == 64) ? v : (v & ((1ull << e) - 1));
    unsigned s = popcount64(elem);
    uint64_t run = (1ull << s) - 1;
    for (unsigned r = 0; r < e; r++)
        if (ror_e(run, r, e) == elem) {
            *N = (e == 64); *immr = r; *imms = ((~(e - 1) << 1) & 0x3Fu) | (s - 1);
            return true;
        }
    return false;
}
static bool is_logical_imm32(uint32_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    return is_logical_imm64((uint64_t)v << 32 | v, N, immr, imms) && *N == 0;
}
/* ---- Arm ARM DecodeBitMasks (M = 32) ---- */
static bool decode32(unsigned N, unsigned immr, unsigned imms, uint32_t *out) {
    unsigned x = (N << 6) | (~imms & 0x3Fu); int len = -1;
    for (int b = 6; b >= 0; b--) if (x >> b & 1u) { len = b; break; }
    if (len < 1 || len > 5) return false;
    unsigned levels = (1u << len) - 1, esize = 1u << len;
    if ((imms & levels) == levels) return false;
    uint64_t rot = ror_e((1ull << ((imms & levels) + 1)) - 1, immr & levels, esize), w = 0;
    for (unsigned i = 0; i < 32; i += esize) w |= rot << i;
    *out = (uint32_t)w;
    return true;
}

#define T 16
static uint64_t g_true[T], g_bad[T];
static void *worker(void *arg) {
    unsigned t = (unsigned)(uintptr_t)arg;
    uint64_t lo = ((uint64_t)1 << 32) / T * t, hi = lo + ((uint64_t)1 << 32) / T;
    for (uint64_t v = lo; v < hi; v++) {
        unsigned N, r, s; uint32_t back;
        if (is_logical_imm32((uint32_t)v, &N, &r, &s)) {
            g_true[t]++;
            if (!decode32(N, r, s, &back) || back != (uint32_t)v) g_bad[t]++;
        }
    }
    return NULL;
}
int main(void) {
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    pthread_t th[T];
    for (unsigned t = 0; t < T; t++) pthread_create(&th[t], NULL, worker, (void *)(uintptr_t)t);
    uint64_t tot = 0, bad = 0;
    for (unsigned t = 0; t < T; t++) { pthread_join(th[t], NULL); tot += g_true[t]; bad += g_bad[t]; }
    clock_gettime(CLOCK_MONOTONIC, &b);
    /* 집합 크기: 서로 다른 디코드 결과 개수 (정규형 immr < esize 만 세면 중복 없음) */
    unsigned setsize = 0;
    for (unsigned imms = 0; imms < 64; imms++)
        for (unsigned immr = 0; immr < 64; immr++) {
            uint32_t w; unsigned x = (~imms & 0x3Fu), len = 0;
            for (int bb = 5; bb >= 0; bb--) if (x >> bb & 1u) { len = (unsigned)bb; break; }
            if (decode32(0, immr, imms, &w) && immr < (1u << len)) setsize++;
        }
    printf("checked 2^32 values: encodable=%llu, roundtrip-bad=%llu, DecodeBitMasks set=%u, %.1f s (%d threads)\n",
           (unsigned long long)tot, (unsigned long long)bad, setsize,
           (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9, T);
    return (tot == setsize && bad == 0) ? 0 : 1;
}
