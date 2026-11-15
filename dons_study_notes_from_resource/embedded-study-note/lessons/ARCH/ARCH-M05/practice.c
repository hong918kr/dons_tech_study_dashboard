/* ARCH-M05 추가 연습 — 한 번에 검증
 *
 *   연습 1  way lockdown (할당 마스크): 핫 셋을 way 0–1 에 미리 넣고 잠그면 스트림이 못 쫓아낸다
 *   연습 2  고전 Belady 이상: k 칸 FIFO 3 → 4 칸에서 미스 9 → 10, LRU 는 10 → 8
 *   연습 3  교체 정책의 상태 비트 수 (정확 LRU 최소 / 행렬 / 나이 카운터 / PLRU / FIFO)
 *   연습 4  stride 가 쓰는 집합 수와 상주 가능한 라인 수 (4096 vs 4160 B 간격)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* ---------- 연습 1: LRU + 할당 마스크 (PL310 류 "lockdown by way" 를 단순화) ---------- */
#define LK_S 32
#define LK_W 4
typedef struct { uint32_t tag; uint8_t valid; uint64_t stamp; } lk_way_t;
typedef struct { lk_way_t w[LK_S][LK_W]; uint64_t clock; uint32_t alloc_mask; } lk_cache_t;  /* 4 KB, 32 B */

/* 히트는 어느 way 에서나. 미스면 alloc_mask 에 켜진 way 중에서만 채운다 (빈 것 먼저, 없으면 LRU). */
static int lk_access(lk_cache_t *c, uint32_t addr) {
    uint32_t set = (addr >> 5) & (LK_S - 1), tag = addr >> 10;
    lk_way_t *L = c->w[set];
    c->clock++;
    for (int i = 0; i < LK_W; i++)
        if (L[i].valid && L[i].tag == tag) { L[i].stamp = c->clock; return 1; }
    int v = -1;
    for (int i = 0; i < LK_W; i++)
        if ((c->alloc_mask >> i & 1) && !L[i].valid) { v = i; break; }
    if (v < 0)
        for (int i = 0; i < LK_W; i++)
            if ((c->alloc_mask >> i & 1) && (v < 0 || L[i].stamp < L[v].stamp)) v = i;
    if (v < 0) return 0;                       /* 할당 가능한 way 없음 → 캐시 안 하고 통과 */
    L[v].valid = 1; L[v].tag = tag; L[v].stamp = c->clock;
    return 0;
}

/* 본문 gen_hot_stream 과 같은 패턴: tick 마다 핫 64 라인 → 새 스트림 96 라인. 핫 미스만 센다. */
static uint32_t hot_misses_locked(int lock) {
    static lk_cache_t c;
    memset(&c, 0, sizeof c);
    c.alloc_mask = 0xF;
    if (lock) {
        c.alloc_mask = 0x3;                                    /* way 0–1 만 열고 핫 셋을 만진다 */
        for (uint32_t i = 0; i < 64; i++) lk_access(&c, 0x20000000u + i * 32u);
        c.alloc_mask = 0xC;                                    /* 잠금: 이후 할당은 way 2–3 만 */
    }
    uint32_t m = 0;
    for (uint32_t k = 0; k < 16; k++) {
        for (uint32_t i = 0; i < 64; i++) m += !lk_access(&c, 0x20000000u + i * 32u);
        for (uint32_t i = 0; i < 96; i++) lk_access(&c, 0x30000000u + (k * 96u + i) * 32u);
    }
    return m;
}

/* ---------- 연습 2: k 칸 FIFO / LRU (2의 거듭제곱 제약 없음) ---------- */
static uint32_t frames_misses(int lru, uint32_t k, const uint32_t *s, size_t n) {
    uint32_t fr[16]; uint64_t st[16]; uint32_t used = 0, miss = 0;
    for (size_t i = 0; i < n; i++) {
        int hit = -1;
        for (uint32_t j = 0; j < used; j++) if (fr[j] == s[i]) hit = (int)j;
        if (hit >= 0) { if (lru) st[hit] = i + 1; continue; }        /* FIFO 는 히트에 도장 안 바꿈 */
        miss++;
        uint32_t v = 0;
        if (used < k) v = used++;
        else for (uint32_t j = 1; j < k; j++) if (st[j] < st[v]) v = j;
        fr[v] = s[i]; st[v] = i + 1;
    }
    return miss;
}

/* ---------- 연습 3: 집합당 교체 상태 비트 ---------- */
static unsigned ceil_log2_u64(uint64_t x) { unsigned b = 0; while ((1ull << b) < x) b++; return b; }
static uint64_t fact(unsigned n) { uint64_t f = 1; while (n > 1) f *= n--; return f; }
static unsigned lru_min_bits(unsigned W)    { return ceil_log2_u64(fact(W)); }       /* W! 가지 순서 */
static unsigned lru_matrix_bits(unsigned W) { return W * (W - 1) / 2; }               /* 쌍마다 "누가 최근" */
static unsigned lru_age_bits(unsigned W)    { return W * ceil_log2_u64(W); }         /* way 마다 순위 */
static unsigned plru_bits(unsigned W)       { return W - 1; }
static unsigned fifo_bits(unsigned W)       { return ceil_log2_u64(W); }             /* 라운드로빈 포인터 */

/* ---------- 연습 4: stride 가 닿는 집합 ---------- */
static uint32_t gcd_u32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
/* 주소 0, stride, 2*stride, ... 가 쓰는 서로 다른 집합 수 (stride 는 B 의 배수) */
static uint32_t sets_touched(uint32_t S, uint32_t B, uint32_t stride) {
    uint32_t step = (stride / B) % S;
    return S / gcd_u32(S, step ? step : S);
}
static uint32_t resident_lines(uint32_t C, uint32_t B, uint32_t W, uint32_t stride) {
    uint32_t S = C / (B * W);
    return W * sets_touched(S, B, stride);
}

int main(void) {
    printf("-- 연습 1 --\n");
    uint32_t unl = hot_misses_locked(0), lk = hot_misses_locked(1);
    printf("      hot misses: unlocked %u / 1024, ways 0-1 locked %u / 1024\n", unl, lk);
    CHECK(unl == 1024);
    CHECK(lk == 0);

    printf("-- 연습 2 --\n");
    const uint32_t b[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    CHECK(frames_misses(0, 3, b, 12) == 9 && frames_misses(0, 4, b, 12) == 10);   /* FIFO: 칸 늘렸는데 미스 증가 */
    CHECK(frames_misses(1, 3, b, 12) == 10 && frames_misses(1, 4, b, 12) == 8);   /* LRU: 포함 성질 → 감소 */

    printf("-- 연습 3 --\n");
    CHECK(lru_min_bits(4) == 5 && lru_matrix_bits(4) == 6 && lru_age_bits(4) == 8 && plru_bits(4) == 3 && fifo_bits(4) == 2);
    CHECK(lru_min_bits(8) == 16 && lru_matrix_bits(8) == 28 && lru_age_bits(8) == 24 && plru_bits(8) == 7);
    CHECK(lru_min_bits(16) == 45 && plru_bits(16) == 15);
    {   /* 32 KB, 8-way, 64 B → 64 집합: 집합당 비트 x 64 */
        CHECK(64 * lru_age_bits(8) == 1536 && 64 * plru_bits(8) == 448);
    }

    printf("-- 연습 4 --\n");
    CHECK(resident_lines(32768, 64, 8, 4096) == 8);            /* S*B = 4096 → 전부 한 집합 */
    CHECK(resident_lines(32768, 64, 8, 4160) == 512);          /* 4096 + 64: 모든 집합 → 캐시 전체 */
    CHECK(resident_lines(32768, 64, 8, 2048) == 16);           /* 2 집합 */
    CHECK(resident_lines(4096, 32, 1, 256) == 16);             /* 본문 colwalk 의 직접 사상 16 슬롯 */
    CHECK(resident_lines(4096, 32, 4, 256) == 16);             /* 4-way 여도 16 슬롯 (4 집합 x 4) */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
