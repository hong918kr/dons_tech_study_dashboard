/* ALGO-B12 추가 연습 — 답 모음과 검증
 *
 *   연습 1  곱셈 없는 SWAR popcount (shift-add)  — 곱셈기가 느린 코어(Cortex-M0+ 32사이클 옵션) 용
 *   연습 2  넘침 없는 ALIGN_UP (패딩 = -x & (a-1)), ALIGN_DOWN, is_aligned
 *   연습 3  floor/ceil log2 와 "가장 높은 우선순위 준비 태스크" (clz 로)
 *   연습 4  de Bruijn 곱셈 ctz (clang 이 Cortex-M0+ 용으로 실제로 낸 방식)
 *   연습 5  비트맵에서 연속 k 칸 빈 자리 찾기 (한 워드 안)
 *   연습 6  0..n 의 popcount 표를 O(n) 에 (LeetCode 338 Counting Bits)
 *
 * 모든 답은 __builtin_* 없이. 테스트 쪽에서만 builtin/무식한 방법과 대조. 고정 시드 xorshift32.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static unsigned pc_swar32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
static unsigned clz32(uint32_t x) {
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return 32u - pc_swar32(x);
}

/* ---------- 연습 1: 곱셈 없는 SWAR ---------- */
static unsigned pc_swar32_nomul(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    x += x >> 8;                 /* 바이트 0 = b0+b1, 바이트 2 = b2+b3 (각 ≤ 16, 넘침 없음) */
    x += x >> 16;                /* 바이트 0 = 네 바이트 합 ≤ 32 */
    return x & 0x3Fu;            /* 32 = 0b100000 → 6비트 필요 */
}

/* ---------- 연습 2: 정렬 도우미 ---------- */
static uint32_t align_down32(uint32_t x, uint32_t a) { return x & ~(a - 1u); }
static bool     is_aligned32(uint32_t x, uint32_t a) { return (x & (a - 1u)) == 0; }
static uint32_t pad_to(uint32_t x, uint32_t a)       { return (0u - x) & (a - 1u); }   /* (−x) mod a */
static bool align_up_pad(uint32_t x, uint32_t a, uint32_t *out) {
    uint32_t r = x + pad_to(x, a);
    if (r < x) return false;     /* 감김 = 결과가 2^32 이상 */
    *out = r;
    return true;
}

/* ---------- 연습 3: log2 와 우선순위 ---------- */
static unsigned ilog2_floor(uint32_t x) { return 31u - clz32(x); }                 /* x ≥ 1 */
static unsigned ilog2_ceil(uint32_t x)  { return x <= 1 ? 0u : 32u - clz32(x - 1u); }
/* ready 비트 i = 우선순위 i 의 태스크가 준비됨 (숫자가 클수록 높은 우선순위). 없으면 -1. */
static int top_ready(uint32_t ready) { return ready ? (int)(31u - clz32(ready)) : -1; }

/* ---------- 연습 4: de Bruijn ctz ---------- */
static uint8_t db_tab[32];
static void db_init(void) {
    for (unsigned i = 0; i < 32; i++) db_tab[((1u << i) * 0x077CB531u) >> 27] = (uint8_t)i;
}
static unsigned ctz_debruijn(uint32_t x) {
    if (x == 0) return 32;
    return db_tab[((x & (0u - x)) * 0x077CB531u) >> 27];
}

/* ---------- 연습 5: 연속 k 칸 빈 자리 (1 = 사용 중). 시작 번호, 없으면 -1. 1 ≤ k ≤ 32 ---------- */
static int find_free_run(uint32_t used, unsigned k) {
    uint32_t f = ~used;                          /* 빈 칸 = 1 */
    for (unsigned i = 1; i < k; i++) f &= f >> 1; /* 비트 j 가 남음 ⟺ j..j+k-1 이 전부 빈 칸 */
    if (f == 0) return -1;
    uint32_t low = f & (0u - f);
    return (int)pc_swar32(low - 1u);             /* ctz */
}

/* ---------- 연습 6: Counting Bits ---------- */
static void count_bits(unsigned n, uint8_t *out) {
    out[0] = 0;
    for (unsigned i = 1; i <= n; i++) out[i] = (uint8_t)(out[i & (i - 1u)] + 1u);   /* 최하위 1 하나 뺀 수 + 1 */
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x1234567u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    db_init();

    /* 연습 1 */
    unsigned bad1 = 0;
    for (uint32_t x = 0; x <= 0xFFFFu; x++)
        if (pc_swar32_nomul(x) != (unsigned)__builtin_popcount(x) ||
            pc_swar32_nomul(~x) != (unsigned)__builtin_popcount(~x)) bad1++;
    for (int k = 0; k < (1 << 20); k++) { uint32_t x = xorshift32(); if (pc_swar32_nomul(x) != (unsigned)__builtin_popcount(x)) bad1++; }
    CHECK(pc_swar32_nomul(0xFFFFFFFFu) == 32 && pc_swar32_nomul(0) == 0);
    CHECK(bad1 == 0);

    /* 연습 2 */
    uint32_t r = 0;
    CHECK(align_down32(0x2000001Fu, 32) == 0x20000000u && is_aligned32(0x20000040u, 64) && !is_aligned32(0x20000020u, 64));
    CHECK(pad_to(0x1001u, 0x1000u) == 0xFFFu && pad_to(0x1000u, 0x1000u) == 0);
    CHECK(align_up_pad(0x1001u, 0x1000u, &r) && r == 0x2000u);
    CHECK(!align_up_pad(0xFFFFFFC1u, 64, &r) && align_up_pad(0xFFFFFFC0u, 64, &r) && r == 0xFFFFFFC0u);
    unsigned bad2 = 0;
    for (int k = 0; k < 200000; k++) {
        uint32_t x = xorshift32(), a = 1u << (xorshift32() % 32u);
        if (k & 1) x |= 0xFFFFFF00u;                                   /* 꼭대기 근처 */
        uint64_t want = ((uint64_t)x + a - 1u) / a * a;
        uint32_t got = 0;
        bool ok = align_up_pad(x, a, &got);
        if (ok != (want <= UINT32_MAX) || (ok && got != want)) bad2++;
        if (align_down32(x, a) != x / a * a) bad2++;
        if (is_aligned32(x, a) != (x % a == 0)) bad2++;
    }
    CHECK(bad2 == 0);

    /* 연습 3 */
    CHECK(ilog2_floor(1) == 0 && ilog2_floor(1024) == 10 && ilog2_floor(1025) == 10 && ilog2_floor(0xFFFFFFFFu) == 31);
    CHECK(ilog2_ceil(1) == 0 && ilog2_ceil(2) == 1 && ilog2_ceil(1024) == 10 && ilog2_ceil(1025) == 11 && ilog2_ceil(0x80000001u) == 32);
    unsigned bad3 = 0;
    for (int k = 0; k < 200000; k++) {
        uint32_t x = xorshift32() >> (xorshift32() % 32u);
        if (x == 0) continue;
        unsigned f = 0; while ((x >> f) > 1u) f++;                     /* 무식한 floor */
        unsigned c = 0; while (((uint64_t)1 << c) < x) c++;            /* 무식한 ceil */
        if (ilog2_floor(x) != f || ilog2_ceil(x) != c) bad3++;
    }
    CHECK(bad3 == 0);
    CHECK(top_ready(0) == -1 && top_ready(0x00000001u) == 0 && top_ready(0x00010080u) == 16 && top_ready(0x80000000u) == 31);

    /* 연습 4 */
    unsigned bad4 = 0;
    for (unsigned i = 0; i < 32; i++) if (ctz_debruijn(1u << i) != i || ctz_debruijn(0xFFFFFFFFu << i) != i) bad4++;
    for (int k = 0; k < (1 << 20); k++) { uint32_t x = xorshift32(); if (x && ctz_debruijn(x) != (unsigned)__builtin_ctz(x)) bad4++; }
    CHECK(ctz_debruijn(0) == 32);
    CHECK(bad4 == 0);

    /* 연습 5 */
    CHECK(find_free_run(0x00000000u, 4) == 0 && find_free_run(0xFFFFFFFFu, 1) == -1);
    CHECK(find_free_run(0x0000000Fu, 3) == 4 && find_free_run(0xF0F0F0F0u, 4) == 0 && find_free_run(0xF0F0F0F0u, 5) == -1);
    CHECK(find_free_run(0x7FFFFFFFu, 1) == 31 && find_free_run(0x00000000u, 32) == 0 && find_free_run(0x80000000u, 32) == -1);
    unsigned bad5 = 0;
    for (int t = 0; t < 100000; t++) {
        uint32_t used = xorshift32() | xorshift32();                    /* 빈 칸 25% */
        if (t & 1) used &= xorshift32();                               /* 빈 칸 더 많게 */
        unsigned k = 1 + xorshift32() % 8u;
        int want = -1;
        for (unsigned j = 0; j + k <= 32 && want < 0; j++) {
            bool ok = true;
            for (unsigned q = j; q < j + k; q++) if (used >> q & 1u) { ok = false; break; }
            if (ok) want = (int)j;
        }
        if (find_free_run(used, k) != want) bad5++;
    }
    CHECK(bad5 == 0);

    /* 연습 6 */
    static uint8_t cb[65536];
    count_bits(65535, cb);
    unsigned bad6 = 0;
    for (unsigned i = 0; i < 65536; i++) if (cb[i] != (unsigned)__builtin_popcount(i)) bad6++;
    CHECK(cb[0] == 0 && cb[5] == 2 && cb[255] == 8);
    CHECK(bad6 == 0);

    printf("bad: ex1=%u ex2=%u ex3=%u ex4=%u ex5=%u ex6=%u\n", bad1, bad2, bad3, bad4, bad5, bad6);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
