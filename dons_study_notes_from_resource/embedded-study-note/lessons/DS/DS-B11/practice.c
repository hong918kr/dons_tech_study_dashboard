/* DS-B11 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAP_MAX 1024u
enum { EMPTY = 0, FULL = 1, TOMB = 2 };
typedef struct { uint32_t key, val; uint8_t state; } slot_t;
typedef struct {
    slot_t   s[CAP_MAX];
    uint32_t cap, mask, bits, used, tombs, probes;
    bool     fib;                        /* true: Fibonacci 해싱, false: key & mask */
} ht_t;

static void ht_init(ht_t *t, uint32_t bits, bool fib) {
    memset(t->s, 0, sizeof t->s);
    t->bits = bits; t->cap = 1u << bits; t->mask = t->cap - 1u;
    t->used = t->tombs = t->probes = 0; t->fib = fib;
}

/* 연습 3: Fibonacci 해싱 — 2^32/φ 를 곱하고 "상위" bits 비트를 쓴다 */
static uint32_t ht_home(const ht_t *t, uint32_t key) {
    return t->fib ? (key * 2654435769u) >> (32u - t->bits) : key & t->mask;
}

static int32_t ht_locate(ht_t *t, uint32_t key) {
    uint32_t i = ht_home(t, key);
    t->probes = 0;
    for (uint32_t n = 0; n < t->cap; n++, i = (i + 1u) & t->mask) {
        t->probes++;
        if (t->s[i].state == EMPTY) return -1;
        if (t->s[i].state == FULL && t->s[i].key == key) return (int32_t)i;
    }
    return -1;
}

static bool ht_insert(ht_t *t, uint32_t key, uint32_t val) {
    uint32_t i = ht_home(t, key);
    int32_t tomb = -1, target = -1;
    for (uint32_t n = 0; n < t->cap; n++, i = (i + 1u) & t->mask) {
        slot_t *s = &t->s[i];
        if (s->state == FULL) { if (s->key == key) { s->val = val; return true; } }
        else if (s->state == TOMB) { if (tomb < 0) tomb = (int32_t)i; }
        else { target = (int32_t)i; break; }
    }
    if (tomb >= 0) target = tomb;
    if (target < 0) return false;
    if (t->s[target].state == TOMB) t->tombs--;
    t->s[target] = (slot_t){ key, val, FULL };
    t->used++;
    return true;
}

static bool ht_delete_tomb(ht_t *t, uint32_t key) {
    int32_t i = ht_locate(t, key);
    if (i < 0) return false;
    t->s[i].state = TOMB; t->used--; t->tombs++;
    return true;
}

/* 연습 1: 제자리 재구성(rehash). 살아 있는 항목만 정적 scratch 로 옮겼다가 다시 넣는다 */
static slot_t g_scratch[CAP_MAX];
static void ht_rebuild(ht_t *t) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->cap; i++) if (t->s[i].state == FULL) g_scratch[n++] = t->s[i];
    ht_init(t, t->bits, t->fib);
    for (uint32_t i = 0; i < n; i++) ht_insert(t, g_scratch[i].key, g_scratch[i].val);
}
/* 정책: EMPTY 가 부족해지면(used+tombs > 3/4 cap) 재구성 */
static bool ht_needs_rebuild(const ht_t *t) { return (t->used + t->tombs) * 4u > t->cap * 3u; }

/* 연습 2: backward-shift 삭제 — tombstone 없이, 뒤 항목을 당겨 빈칸을 메운다.
 * 빈칸 i 뒤의 j 에 있는 항목(home h)은 h 가 (i, j] 구간 "밖" 에 있을 때만 i 로 옮길 수 있다. */
static bool ht_delete_shift(ht_t *t, uint32_t key) {
    int32_t found = ht_locate(t, key);
    if (found < 0) return false;
    uint32_t i = (uint32_t)found, j = i;
    for (;;) {
        j = (j + 1u) & t->mask;
        if (t->s[j].state == EMPTY) break;
        uint32_t h = ht_home(t, t->s[j].key);
        bool h_in_i_j = ((j - h) & t->mask) < ((j - i) & t->mask);   /* h ∈ (i, j] ? */
        if (h_in_i_j) continue;                                       /* 제자리가 빈칸 뒤 → 그대로 */
        t->s[i] = t->s[j];
        i = j;
    }
    t->s[i].state = EMPTY;
    t->used--;
    return true;
}

static bool ht_check(const ht_t *t) {
    uint32_t full = 0, tomb = 0;
    for (uint32_t i = 0; i < t->cap; i++) {
        if (t->s[i].state == TOMB) tomb++;
        if (t->s[i].state != FULL) continue;
        full++;
        for (uint32_t j = ht_home(t, t->s[i].key); j != i; j = (j + 1u) & t->mask)
            if (t->s[j].state == EMPTY || (t->s[j].state == FULL && t->s[j].key == t->s[i].key))
                return false;
    }
    return full == t->used && tomb == t->tombs;
}

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 없는 무작위 키 n개의 평균 탐사 수 */
static double avg_miss(ht_t *t, uint32_t n) {
    uint64_t sum = 0;
    for (uint32_t m = 0; m < n; ) {
        uint32_t k = xorshift32();
        if (ht_locate(t, k) >= 0) continue;
        sum += t->probes; m++;
    }
    return (double)sum / n;
}
/* 넣은 키 전체의 평균 탐사 수 */
static double avg_hit(ht_t *t, const uint32_t *keys, uint32_t n) {
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) { ht_locate(t, keys[i]); sum += t->probes; }
    return (double)sum / n;
}

/* 연습 4: 삼각수 탐사 i_n = home + n(n+1)/2 가 2^k 표의 모든 칸을 한 번씩 도는가 */
static bool covers_all(uint32_t cap, bool triangular) {
    static bool seen[CAP_MAX];
    memset(seen, 0, sizeof seen);
    uint32_t distinct = 0;
    for (uint32_t n = 0; n < cap; n++) {
        uint32_t off = triangular ? n * (n + 1u) / 2u : n * n;
        uint32_t i = (7u + off) & (cap - 1u);
        if (!seen[i]) { seen[i] = true; distinct++; }
    }
    return distinct == cap;
}

static ht_t g_t;
static uint32_t g_live[CAP_MAX];

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    ht_t *t = &g_t;

    /* 연습 1: 삽입/삭제를 반복(churn)하면 묘비가 쌓여 실패 조회가 느려진다 → 재구성 */
    {
        ht_init(t, 10, false);                       /* cap 1024, 부하율 0.5 유지 */
        rng = 2024u;
        uint32_t n = 0;
        while (n < 512u) { uint32_t k = xorshift32(); if (ht_locate(t, k) < 0) { ht_insert(t, k, 0); g_live[n++] = k; } }
        double miss0 = avg_miss(t, 20000);
        for (uint32_t c = 0; c < 20000u; c++) {      /* 하나 지우고 새 키 하나 넣기 */
            uint32_t idx = xorshift32() % n;
            ht_delete_tomb(t, g_live[idx]);
            uint32_t k;
            do { k = xorshift32(); } while (ht_locate(t, k) >= 0);
            ht_insert(t, k, 0);
            g_live[idx] = k;
        }
        uint32_t tombs = t->tombs;
        double miss1 = avg_miss(t, 20000);
        bool need = ht_needs_rebuild(t);
        ht_rebuild(t);
        double miss2 = avg_miss(t, 20000);
        printf("[1] miss probes: fresh %.2f | after churn %.2f (tombs=%u) | after rebuild %.2f (tombs=%u)\n",
               miss0, miss1, tombs, miss2, t->tombs);
        CHECK(tombs > 100 && miss1 > 2.0 * miss0);
        CHECK(need && t->tombs == 0 && t->used == 512 && ht_check(t));
        CHECK(miss2 < 1.3 * miss0);
    }

    /* 연습 2: backward-shift 삭제 — 같은 5/21/37 시나리오 + 참조 모델 대조 */
    {
        ht_init(t, 4, false);
        ht_insert(t, 5, 1); ht_insert(t, 21, 2); ht_insert(t, 37, 3);
        CHECK(ht_delete_shift(t, 21));
        CHECK(t->s[6].state == FULL && t->s[6].key == 37 && t->s[7].state == EMPTY);
        CHECK(ht_locate(t, 37) == 6 && t->probes == 2 && ht_check(t));
        /* 랩어라운드 + 제자리 항목: 15(→15), 31(→0), 1(→1 자기 자리) 에서 15 삭제 */
        ht_init(t, 4, false);
        ht_insert(t, 15, 1); ht_insert(t, 31, 2); ht_insert(t, 1, 3);  /* 15→15, 31→0, 1→1 */
        CHECK(ht_delete_shift(t, 15) && t->s[15].key == 31 && t->s[0].state == EMPTY);
        CHECK(t->s[1].key == 1 && ht_check(t));             /* 1 은 제자리라 당겨오지 않는다 */

        static bool has[48];
        bool ok = true;
        ht_init(t, 6, false);
        memset(has, 0, sizeof has);
        rng = 777u;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32(), idx = r % 48u, key = idx * 8u;
            if ((r >> 16) & 1u) { ok = ht_insert(t, key, r); has[idx] = true; }
            else { ok = ht_delete_shift(t, key) == has[idx]; has[idx] = false; }
            for (uint32_t i = 0; i < 48 && ok; i++) ok = (ht_locate(t, i * 8u) >= 0) == has[i];
            ok = ok && ht_check(t) && t->tombs == 0;
        }
        CHECK(ok);
    }

    /* 연습 3: 64바이트 정렬 주소(하위 6비트 = 0) 512개 — mask 해싱 vs Fibonacci 해싱 */
    {
        static uint32_t keys[512];
        for (uint32_t i = 0; i < 512; i++) keys[i] = 0x20000000u + i * 64u;   /* SRAM 버퍼 주소 */
        ht_init(t, 10, false);
        for (uint32_t i = 0; i < 512; i++) ht_insert(t, keys[i], i);
        double mask_hit = avg_hit(t, keys, 512);
        ht_init(t, 10, true);
        for (uint32_t i = 0; i < 512; i++) ht_insert(t, keys[i], i);
        double fib_hit = avg_hit(t, keys, 512);
        printf("[3] 64B-aligned keys, alpha 0.5: avg hit probes  mask %.2f | fibonacci %.2f\n",
               mask_hit, fib_hit);
        CHECK(mask_hit > 10.0);                      /* home 이 16곳뿐 → 32칸짜리 클러스터 16개 */
        CHECK(fib_hit < 2.0);
    }

    /* 연습 4: 삼각수 탐사는 2^k 표 전체를 덮고, n^2 탐사는 못 덮는다 */
    CHECK(covers_all(16, true) && covers_all(64, true) && covers_all(1024, true));
    CHECK(!covers_all(16, false) && !covers_all(1024, false));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
