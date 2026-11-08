/* DS-M09 추가 연습 — 한 번에 검증
 * 연습 1: 작은 번호 = 높은 우선순위 (Linux RT / µC/OS / Zephyr 관례) — CTZ 버전과 "뒤집어 저장 + CLZ" 버전
 * 연습 2: CLZ 없는 코어(Cortex-M0)용 이식 가능한 fls32 — 이진 탐색 / 256칸 표
 * 연습 3: 3단계 비트맵 32768 (32 × 32 × 32)
 * 연습 4: 1024블록 할당기 — 비트 1 = 빈 블록, 가장 낮은 빈 블록을 O(1) 에
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t fls32(uint32_t x) { return 31u - (uint32_t)__builtin_clz(x); }   /* x != 0 */
static uint32_t ffs32(uint32_t x) { return (uint32_t)__builtin_ctz(x); }         /* x != 0 */

typedef struct { uint32_t top; uint32_t leaf[32]; } hbm_t;

/* ---------------- 연습 1: 0 이 가장 급한 관례 ---------------- */
/* 1a: 비트 p 를 그대로 저장, 가장 작은 p 를 CTZ 로 (Cortex-M 에서는 RBIT + CLZ 두 명령) */
static void a_set(hbm_t *h, uint32_t p)   { h->leaf[p >> 5] |= 1u << (p & 31u); h->top |= 1u << (p >> 5); }
static void a_clear(hbm_t *h, uint32_t p) {
    uint32_t g = p >> 5;
    h->leaf[g] &= ~(1u << (p & 31u));
    if (h->leaf[g] == 0) h->top &= ~(1u << g);
}
static int a_best(const hbm_t *h) {
    if (!h->top) return -1;
    uint32_t g = ffs32(h->top);
    return (int)(g * 32u + ffs32(h->leaf[g]));
}
/* 1b: 우선순위 p 를 그룹 31-(p>>5) 의 비트 31-(p&31) 에 "뒤집어" 저장 (µC/OS-III 방식).
 *     가장 작은 p = 가장 높은 비트 → clz(top) 이 곧 p>>5, clz(그 leaf) 가 곧 p&31. CLZ 한 명령씩 */
static void b_set(hbm_t *h, uint32_t p) {
    uint32_t g = 31u - (p >> 5), b = 31u - (p & 31u);
    h->leaf[g] |= 1u << b; h->top |= 1u << g;
}
static void b_clear(hbm_t *h, uint32_t p) {
    uint32_t g = 31u - (p >> 5), b = 31u - (p & 31u);
    h->leaf[g] &= ~(1u << b);
    if (h->leaf[g] == 0) h->top &= ~(1u << g);
}
static int b_best(const hbm_t *h) {
    if (!h->top) return -1;
    uint32_t cg = (uint32_t)__builtin_clz(h->top);          /* = 우선순위 그룹 번호 (p >> 5) */
    uint32_t cb = (uint32_t)__builtin_clz(h->leaf[31u - cg]); /* = 그룹 안 번호 (p & 31) */
    return (int)(cg * 32u + cb);
}

/* ---------------- 연습 2: CLZ 없는 fls32 ---------------- */
/* 2a: 이진 탐색 5단계 — 분기 5번, 표 없음 */
static uint32_t fls32_bsearch(uint32_t x) {          /* x != 0 */
    uint32_t r = 0;
    if (x & 0xFFFF0000u) { x >>= 16; r += 16; }
    if (x & 0x0000FF00u) { x >>= 8;  r += 8; }
    if (x & 0x000000F0u) { x >>= 4;  r += 4; }
    if (x & 0x0000000Cu) { x >>= 2;  r += 2; }
    if (x & 0x00000002u) {           r += 1; }
    return r;
}
/* 2b: 바이트 표 256칸 (µC/OS-II OSUnMapTbl 와 같은 발상, 여기선 가장 높은 비트) */
static uint8_t g_fls8[256];
static void fls8_init(void) {
    for (uint32_t v = 1; v < 256; v++) { uint8_t r = 0; while ((v >> (r + 1u)) != 0) r++; g_fls8[v] = r; }
}
static uint32_t fls32_table(uint32_t x) {            /* x != 0 */
    if (x >> 16) return (x >> 24) ? 24u + g_fls8[x >> 24] : 16u + g_fls8[(x >> 16) & 0xFFu];
    return (x >> 8) ? 8u + g_fls8[x >> 8] : g_fls8[x];
}
static int hb_highest_portable(const hbm_t *h) {
    if (!h->top) return -1;
    uint32_t g = fls32_bsearch(h->top);
    return (int)(g * 32u + fls32_table(h->leaf[g]));
}
static int hb_highest(const hbm_t *h) {
    if (!h->top) return -1;
    uint32_t g = fls32(h->top);
    return (int)(g * 32u + fls32(h->leaf[g]));
}

/* ---------------- 연습 3: 3단계 32768 ---------------- */
#define L3_BITS 32768u
typedef struct {
    uint32_t l0;                /* 비트 i = mid[i] != 0 */
    uint32_t mid[32];           /* mid[i] 비트 j = leaf[i*32+j] != 0 */
    uint32_t leaf[1024];        /* leaf[w] 비트 b = 우선순위 w*32+b */
} hb3_t;                        /* 4 + 128 + 4096 = 4228 바이트 */

static void h3_set(hb3_t *h, uint32_t p) {
    uint32_t w = p >> 5, i = w >> 5;
    h->leaf[w] |= 1u << (p & 31u);
    h->mid[i]  |= 1u << (w & 31u);
    h->l0      |= 1u << i;
}
static void h3_clear(hb3_t *h, uint32_t p) {
    uint32_t w = p >> 5, i = w >> 5;
    h->leaf[w] &= ~(1u << (p & 31u));
    if (h->leaf[w]) return;                 /* 아래가 안 비었으면 위는 그대로 */
    h->mid[i] &= ~(1u << (w & 31u));
    if (h->mid[i]) return;
    h->l0 &= ~(1u << i);
}
static int h3_highest(const hb3_t *h) {
    if (!h->l0) return -1;
    uint32_t i = fls32(h->l0);
    uint32_t w = i * 32u + fls32(h->mid[i]);
    return (int)(w * 32u + fls32(h->leaf[w]));
}
static bool h3_check(const hb3_t *h) {
    for (uint32_t w = 0; w < 1024; w++)
        if ((((h->mid[w >> 5] >> (w & 31u)) & 1u) != 0) != (h->leaf[w] != 0)) return false;
    for (uint32_t i = 0; i < 32; i++)
        if ((((h->l0 >> i) & 1u) != 0) != (h->mid[i] != 0)) return false;
    return true;
}
static int h3_ref(const hb3_t *h) {          /* 참조: leaf 1024워드 선형 스캔 */
    for (uint32_t w = 1024; w-- > 0;) if (h->leaf[w]) return (int)(w * 32u + fls32(h->leaf[w]));
    return -1;
}

/* ---------------- 연습 4: 블록 할당기 (비트 1 = 빈 블록) ---------------- */
typedef struct { hbm_t freemap; } balloc_t;
static void ba_init(balloc_t *a) {             /* 전부 빈 상태: 모든 비트 1 */
    a->freemap.top = 0xFFFFFFFFu;
    for (int g = 0; g < 32; g++) a->freemap.leaf[g] = 0xFFFFFFFFu;
}
static int ba_alloc(balloc_t *a) {              /* 가장 낮은 빈 블록, 없으면 -1 */
    int p = a_best(&a->freemap);
    if (p >= 0) a_clear(&a->freemap, (uint32_t)p);
    return p;
}
static bool ba_free(balloc_t *a, uint32_t p) {  /* 범위 밖·이중 해제는 false */
    if (p >= 1024u || ((a->freemap.leaf[p >> 5] >> (p & 31u)) & 1u)) return false;
    a_set(&a->freemap, p);
    return true;
}

static bool g_ref[L3_BITS];
static hbm_t g_ha, g_hb;
static hb3_t g_h3;
static balloc_t g_ba;
static int g_owned[1024];

int main(void) {
    /* 연습 1: 두 저장 방식이 같은 답 (가장 작은 번호), bool 참조와 대조 */
    {
        memset(&g_ha, 0, sizeof g_ha); memset(&g_hb, 0, sizeof g_hb); memset(g_ref, 0, 1024);
        rng_state = 11u;
        bool ok = true;
        for (int op = 0; op < 40000 && ok; op++) {
            uint32_t r = xorshift32(), p = (r >> 8) % 1024u;
            if (r & 1u) { a_set(&g_ha, p); b_set(&g_hb, p); g_ref[p] = true; }
            else        { a_clear(&g_ha, p); b_clear(&g_hb, p); g_ref[p] = false; }
            int want = -1;
            for (int q = 0; q < 1024; q++) if (g_ref[q]) { want = q; break; }
            ok = a_best(&g_ha) == want && b_best(&g_hb) == want;
        }
        CHECK(ok);
        memset(&g_hb, 0, sizeof g_hb);
        b_set(&g_hb, 0);
        CHECK(g_hb.top == 0x80000000u && g_hb.leaf[31] == 0x80000000u && b_best(&g_hb) == 0);
        b_set(&g_hb, 1023);
        CHECK(g_hb.leaf[0] == 1u && b_best(&g_hb) == 0);
        b_clear(&g_hb, 0);
        CHECK(b_best(&g_hb) == 1023 && g_hb.top == 1u);
    }
    /* 연습 2: 이식 가능한 fls32 == 31 - clz, 모든 단일 비트 · 경계값 · 랜덤 100만 개 */
    {
        fls8_init();
        bool ok = true;
        for (uint32_t b = 0; b < 32; b++) {
            uint32_t x = 1u << b, y = x | (x - 1u);     /* 단일 비트, 그 아래 전부 1 */
            ok = ok && fls32_bsearch(x) == b && fls32_table(x) == b && fls32_bsearch(y) == b && fls32_table(y) == b;
        }
        CHECK(ok);
        rng_state = 22u;
        for (int i = 0; i < 1000000 && ok; i++) {
            uint32_t x = xorshift32() >> (i & 31);     /* 상위 비트 위치가 고르게 */
            if (x == 0) continue;
            ok = fls32_bsearch(x) == fls32(x) && fls32_table(x) == fls32(x);
        }
        CHECK(ok);
        rng_state = 33u;
        for (int t = 0; t < 2000 && ok; t++) {
            memset(&g_ha, 0, sizeof g_ha);
            int k = (int)(xorshift32() % 6u);
            for (int j = 0; j < k; j++) { uint32_t p = xorshift32() % 1024u; g_ha.leaf[p >> 5] |= 1u << (p & 31u); g_ha.top |= 1u << (p >> 5); }
            ok = hb_highest_portable(&g_ha) == hb_highest(&g_ha);
        }
        CHECK(ok);
    }
    /* 연습 3: 3단계 — 매 연산 후 두 요약 레벨 검사 + leaf 선형 스캔과 대조 */
    {
        hb3_t *h = &g_h3;
        memset(h, 0, sizeof *h); memset(g_ref, 0, sizeof g_ref);
        rng_state = 44u;
        bool ok = true;
        uint32_t mid_clears = 0;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32();
            /* 대부분 몇몇 leaf 워드에 몰리게: 상위 비트로 고른 64개 워드 × 4비트 */
            uint32_t w = (r & 7u) ? ((r >> 3) % 64u) * 509u % 1024u : (r >> 3) % 1024u;
            uint32_t p = w * 32u + ((r >> 13) & 3u) * 10u;
            uint32_t mids = 0;
            for (int i = 0; i < 32; i++) mids += (uint32_t)__builtin_popcount(h->mid[i]);
            if ((r >> 20) & 1u) { h3_set(h, p); g_ref[p] = true; }
            else                { h3_clear(h, p); g_ref[p] = false; }
            uint32_t mids_after = 0;
            for (int i = 0; i < 32; i++) mids_after += (uint32_t)__builtin_popcount(h->mid[i]);
            if (mids_after < mids) mid_clears++;
            ok = h3_check(h) && h3_highest(h) == h3_ref(h) && (((h->leaf[p >> 5] >> (p & 31u)) & 1u) != 0) == g_ref[p];
        }
        for (uint32_t p = 0; p < L3_BITS && ok; p++) if (g_ref[p]) { h3_clear(h, p); ok = h3_check(h); }
        ok = ok && h->l0 == 0 && h3_highest(h) == -1;
        CHECK(ok);
        h3_set(h, 32767); h3_set(h, 0);
        CHECK(h3_highest(h) == 32767 && h->l0 == 0x80000001u);
        h3_clear(h, 32767);
        CHECK(h3_highest(h) == 0 && h->l0 == 1u && h->mid[31] == 0 && h3_check(h));
        printf("[3] 3-level 32768: random 20000 ops %s (mid-level summary clears=%u), sizeof=%zu bytes\n",
               ok ? "match" : "MISMATCH", mid_clears, sizeof(hb3_t));
    }
    /* 연습 4: 블록 할당기 — 가장 낮은 빈 블록, 이중 해제 거절, 선형 first-zero 와 같은 결과 */
    {
        balloc_t *a = &g_ba;
        ba_init(a);
        CHECK(ba_alloc(a) == 0 && ba_alloc(a) == 1 && ba_alloc(a) == 2);
        CHECK(ba_free(a, 1) && !ba_free(a, 1) && !ba_free(a, 1024) && ba_alloc(a) == 1);
        ba_init(a);
        bool ok = true;
        for (int i = 0; i < 1024 && ok; i++) ok = ba_alloc(a) == i;
        ok = ok && ba_alloc(a) == -1 && a->freemap.top == 0;
        CHECK(ok);
        /* 랜덤 alloc/free vs 선형 참조(가장 낮은 0 찾기) */
        ba_init(a);
        bool used[1024] = { false };
        int nown = 0;
        rng_state = 55u;
        ok = true;
        for (int op = 0; op < 50000 && ok; op++) {
            uint32_t r = xorshift32();
            if ((r & 1u) || nown == 0) {
                int want = -1;
                for (int q = 0; q < 1024; q++) if (!used[q]) { want = q; break; }
                int got = ba_alloc(a);
                ok = got == want;
                if (got >= 0) { used[got] = true; g_owned[nown++] = got; }
            } else {
                int k = (int)((r >> 8) % (uint32_t)nown), p = g_owned[k];
                g_owned[k] = g_owned[--nown];
                ok = ba_free(a, (uint32_t)p) && !ba_free(a, (uint32_t)p);
                used[p] = false;
            }
        }
        CHECK(ok);
        printf("[4] block allocator: random 50000 alloc/free vs linear first-free: %s (in use at end=%d)\n",
               ok ? "match" : "MISMATCH", nown);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
