/* DS-B08 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BM_WORDS(n) (((n) + 31u) / 32u)

static void bm_set(uint32_t *bm, unsigned i)   { bm[i / 32u] |= 1u << (i % 32u); }
static bool bm_test(const uint32_t *bm, unsigned i) { return (bm[i / 32u] >> (i % 32u)) & 1u; }
static void fill(uint32_t *bm, unsigned words, uint32_t v) { for (unsigned w = 0; w < words; w++) bm[w] = v; }

/* 하위 len 비트 마스크 (len = 0..32) — 1u << 32 는 UB 라 분기 */
static uint32_t lomask(unsigned len) { return len >= 32u ? 0xFFFFFFFFu : (1u << len) - 1u; }

/* 연습 1: 범위 set/clear — 워드 단위로 한 번에 */
static void bm_range(uint32_t *bm, unsigned start, unsigned len, bool val) {
    while (len) {
        unsigned w = start / 32u, b = start % 32u;
        unsigned take = 32u - b < len ? 32u - b : len;       /* 이 워드에서 처리할 비트 수 */
        uint32_t m = lomask(take) << b;
        if (val) bm[w] |= m; else bm[w] &= ~m;
        start += take; len -= take;
    }
}

/* 연습 2: 사용 중 비트 수 — popcount (꼬리 비트 제외) */
static unsigned bm_weight(const uint32_t *bm, unsigned n) {
    unsigned c = 0, full = n / 32u;
    for (unsigned w = 0; w < full; w++) c += (unsigned)__builtin_popcount(bm[w]);
    if (n % 32u) c += (unsigned)__builtin_popcount(bm[full] & lomask(n % 32u));
    return c;
}
static unsigned weight_kernighan(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }

/* 연습 3: 가장 높은 1비트 (fls) — RTOS 준비 비트맵에서 최고 우선순위 찾기 */
static unsigned bm_last_set(const uint32_t *bm, unsigned n) {
    for (unsigned w = BM_WORDS(n); w-- > 0;) {
        uint32_t x = bm[w];
        if (w == n / 32u && n % 32u) x &= lomask(n % 32u);   /* 꼬리 쓰레기 제거 */
        if (x) return w * 32u + 31u - (unsigned)__builtin_clz(x);   /* clz(0) 도 UB */
    }
    return n;
}

/* 연습 4: 블록 할당기 — 연속 k 블록 first fit */
static unsigned fnz(const uint32_t *bm, unsigned n, unsigned from) {
    if (from >= n) return n;
    unsigned w = from / 32u;
    uint32_t x = ~bm[w] & (0xFFFFFFFFu << (from % 32u));
    for (;;) {
        if (x) { unsigned i = w * 32u + (unsigned)__builtin_ctz(x); return i < n ? i : n; }
        if (++w >= BM_WORDS(n)) return n;
        x = ~bm[w];
    }
}
static unsigned fns(const uint32_t *bm, unsigned n, unsigned from) {
    if (from >= n) return n;
    unsigned w = from / 32u;
    uint32_t x = bm[w] & (0xFFFFFFFFu << (from % 32u));
    for (;;) {
        if (x) { unsigned i = w * 32u + (unsigned)__builtin_ctz(x); return i < n ? i : n; }
        if (++w >= BM_WORDS(n)) return n;
        x = bm[w];
    }
}
static unsigned blk_alloc(uint32_t *bm, unsigned n, unsigned k) {
    for (unsigned s = fnz(bm, n, 0); s < n; s = fnz(bm, n, fns(bm, n, s))) {
        if (fns(bm, n, s) - s >= k) { bm_range(bm, s, k, true); return s; }
    }
    return n;                                                /* 실패: 단편화 또는 공간 부족 */
}
static void blk_free(uint32_t *bm, unsigned s, unsigned k) { bm_range(bm, s, k, false); }

/* 연습 5: 비용 측정 — 비트 검사 횟수 vs 워드 읽기 횟수 */
static unsigned ffz_loop_steps(const uint32_t *bm, unsigned n, unsigned *steps) {
    for (unsigned i = 0; i < n; i++) { (*steps)++; if (!bm_test(bm, i)) return i; }
    return n;
}
static unsigned ffz_ctz_steps(const uint32_t *bm, unsigned n, unsigned *steps) {
    for (unsigned w = 0; w < BM_WORDS(n); w++) {
        (*steps)++;
        uint32_t x = ~bm[w];
        if (x) { unsigned i = w * 32u + (unsigned)__builtin_ctz(x); return i < n ? i : n; }
    }
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x5EEDu;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    uint32_t a[BM_WORDS(200)], b[BM_WORDS(200)];

    /* 연습 1: 범위 연산을 비트 하나씩 한 결과와 대조 */
    unsigned bad = 0;
    for (unsigned t = 0; t < 2000; t++) {
        fill(a, BM_WORDS(200), 0); fill(b, BM_WORDS(200), 0);
        unsigned s = xs32() % 200u, len = xs32() % (200u - s + 1u);
        bm_range(a, s, len, true);
        for (unsigned i = s; i < s + len; i++) bm_set(b, i);
        for (unsigned w = 0; w < BM_WORDS(200); w++) bad += a[w] != b[w];
    }
    CHECK(bad == 0);
    fill(a, BM_WORDS(200), 0xFFFFFFFFu);
    bm_range(a, 28, 40, false);                              /* 워드 3개에 걸침: 28..67 */
    CHECK(a[0] == 0x0FFFFFFFu && a[1] == 0 && a[2] == 0xFFFFFFF0u);

    /* 연습 2 */
    fill(a, BM_WORDS(200), 0xFFFFFFFFu);
    CHECK(bm_weight(a, 200) == 200);                         /* 꼬리 24비트는 세지 않는다 */
    unsigned bad_w = 0;
    for (unsigned t = 0; t < 1000; t++) { uint32_t x = xs32(); bad_w += (unsigned)__builtin_popcount(x) != weight_kernighan(x); }
    CHECK(bad_w == 0);

    /* 연습 3 */
    fill(a, BM_WORDS(200), 0);
    CHECK(bm_last_set(a, 200) == 200);
    bm_set(a, 5); bm_set(a, 77);
    CHECK(bm_last_set(a, 200) == 77);
    a[6] = 0xFF000000u;                                      /* 꼬리 쓰레기만 → 무시 */
    CHECK(bm_last_set(a, 200) == 77);
    uint32_t ready = (1u << 3) | (1u << 17);                 /* 우선순위 3, 17 태스크 준비 */
    CHECK(31u - (unsigned)__builtin_clz(ready) == 17);       /* FreeRTOS 포트 최적화 방식 */

    /* 연습 4: 단편화 시나리오 (블록 32개) */
    uint32_t blk[1] = {0};
    unsigned x0 = blk_alloc(blk, 32, 4), x1 = blk_alloc(blk, 32, 4), x2 = blk_alloc(blk, 32, 4);
    CHECK(x0 == 0 && x1 == 4 && x2 == 8);
    blk_free(blk, x1, 4);                                    /* 가운데 구멍 [4,8) */
    CHECK(blk_alloc(blk, 32, 8) == 12);                      /* 구멍에 안 맞음 → 뒤로 */
    CHECK(blk_alloc(blk, 32, 3) == 4);                       /* 구멍에 맞음 (first fit) */
    CHECK(blk_alloc(blk, 32, 13) == 32);                     /* 남은 빈칸: 7, [20,32)=12 → 실패 */
    CHECK(blk_alloc(blk, 32, 12) == 20);

    /* 연습 5: 4096비트, 빈칸은 4000번 하나 */
    static uint32_t big[BM_WORDS(4096)];
    fill(big, BM_WORDS(4096), 0xFFFFFFFFu);
    big[4000 / 32] &= ~(1u << (4000 % 32));
    unsigned s1 = 0, s2 = 0;
    unsigned r1 = ffz_loop_steps(big, 4096, &s1), r2 = ffz_ctz_steps(big, 4096, &s2);
    printf("ffz on 4096 bits (free bit 4000): loop %u bit tests, ctz %u word reads\n", s1, s2);
    CHECK(r1 == 4000 && r2 == 4000);
    CHECK(s1 == 4001 && s2 == 126);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
