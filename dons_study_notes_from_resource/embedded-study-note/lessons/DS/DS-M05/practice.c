/* DS-M05 추가 연습 — 한 번에 검증
 * 1) 맵 페이지 단위 캐시 vs 엔트리 단위 캐시 (같은 메모리 예산)
 * 2) CLOCK(second-chance) vs LRU
 * 3) Clean-first 축출(CFLRU 창) — write-back 줄이기
 * 4) 같은 맵 페이지의 dirty 형제를 함께 반영 (batch update)
 * 5) TRIM 은 remove 가 아니라 "UNMAPPED 로 dirty put"
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- 본문과 같은 LRU (key/val/dirty) + 축출 창(window) 옵션 ---------- */
typedef struct ListNode { struct ListNode *prev, *next; } ListNode;
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
static void list_init(ListNode *h) { h->next = h->prev = h; }
static bool list_empty(const ListNode *h) { return h->next == h; }
static void list_add_head(ListNode *h, ListNode *n) {
    n->prev = h; n->next = h->next; h->next->prev = n; h->next = n;
}
static void list_del(ListNode *n) {
    n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = NULL;
}

typedef struct Ent { ListNode node; struct Ent *hnext; uint32_t key, val; bool dirty; } Ent;
typedef struct { bool valid; uint32_t key, val; bool dirty; } Victim;
#define MAXCAP 1024u
typedef struct {
    ListNode lru, free_list;
    Ent pool[MAXCAP];
    Ent *bucket[MAXCAP];
    uint32_t cap, size, shift, window;  /* window: LRU 꼬리에서 clean 을 찾아볼 칸 수 (0 = 순수 LRU) */
    uint32_t hits, misses;
} Lru;

static void lru_init(Lru *c, uint32_t cap, uint32_t window) {
    memset(c, 0, sizeof *c);
    list_init(&c->lru); list_init(&c->free_list);
    c->cap = cap; c->window = window;
    uint32_t bits = 0;
    while ((1u << bits) < cap) bits++;
    c->shift = 32u - (bits ? bits : 1u);           /* 버킷 = 2^bits ≥ cap (최소 2) */
    for (uint32_t i = 0; i < cap; i++) list_add_head(&c->free_list, &c->pool[i].node);
}
static uint32_t hsh(const Lru *c, uint32_t k) { return (k * 0x9E3779B9u) >> c->shift; }
static Ent *lru_find(Lru *c, uint32_t k) {
    for (Ent *e = c->bucket[hsh(c, k)]; e; e = e->hnext) if (e->key == k) return e;
    return NULL;
}
static void unlink_hash(Lru *c, Ent *e) {
    Ent **pp = &c->bucket[hsh(c, e->key)];
    while (*pp != e) pp = &(*pp)->hnext;
    *pp = e->hnext;
}
static bool lru_get(Lru *c, uint32_t k, uint32_t *v) {
    Ent *e = lru_find(c, k);
    if (!e) { c->misses++; return false; }
    list_del(&e->node); list_add_head(&c->lru, &e->node);
    *v = e->val; c->hits++;
    return true;
}
/* 연습 3: 꼬리에서 window 칸 안의 첫 clean 엔트리, 없으면 꼬리 */
static Ent *pick_victim(Lru *c) {
    ListNode *p = c->lru.prev;
    for (uint32_t i = 0; i < c->window && p != &c->lru; i++, p = p->prev)
        if (!container_of(p, Ent, node)->dirty) return container_of(p, Ent, node);
    return container_of(c->lru.prev, Ent, node);
}
static void lru_put(Lru *c, uint32_t k, uint32_t val, bool dirty, Victim *v) {
    v->valid = false;
    Ent *e = lru_find(c, k);
    if (e) {
        e->val = val; e->dirty = e->dirty || dirty;
        list_del(&e->node); list_add_head(&c->lru, &e->node);
        return;
    }
    if (!list_empty(&c->free_list)) {
        e = container_of(c->free_list.next, Ent, node);
    } else {
        e = pick_victim(c);
        *v = (Victim){true, e->key, e->val, e->dirty};
        unlink_hash(c, e);
        c->size--;
    }
    list_del(&e->node);
    e->key = k; e->val = val; e->dirty = dirty;
    uint32_t b = hsh(c, k);
    e->hnext = c->bucket[b]; c->bucket[b] = e;
    list_add_head(&c->lru, &e->node);
    c->size++;
}
static bool lru_remove(Lru *c, uint32_t k) {
    Ent *e = lru_find(c, k);
    if (!e) return false;
    unlink_hash(c, e); list_del(&e->node); list_add_head(&c->free_list, &e->node); c->size--;
    return true;
}

static uint32_t rng;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= 연습 1: 맵 페이지 단위 vs 엔트리 단위 ================= */
/* LBA 65536 개, 4 B 매핑 → 전체 맵 256 KiB = 4 KiB 맵 페이지 64장 (페이지당 1024 매핑).
 * 예산 32 KiB: 페이지 캐시 8장  vs  엔트리 캐시 1024개 (Cortex-M 기준 엔트리 24 B + 버킷 4 B ≈ 28 KiB) */
enum { BIG = 65536, PER_PAGE = 1024, HOTN = 3277, OPS = 50000 };
typedef enum { W_SEQ, W_RAND, W_HOT_CONTIG, W_HOT_SCATTER, W_N } Wk;
static const char *wk_name[W_N] = {"seq", "random", "hot-contig", "hot-scatter"};
static uint32_t big_lba(Wk k, uint32_t i) {
    uint32_t r;
    switch (k) {
    case W_SEQ:  return i % BIG;
    case W_RAND: return xs32() % BIG;
    case W_HOT_CONTIG:                              /* 90% 가 연속 5% (3277 LBA ≈ 3.2 페이지) */
        r = xs32();
        return r % 100u < 90u ? 8192u + (r >> 8) % HOTN : (r >> 8) % BIG;
    default:                                        /* 90% 가 흩어진 5% — j*40503 mod 2^16 은 전단사 */
        r = xs32();
        return r % 100u < 90u ? ((r >> 8) % HOTN * 40503u) % BIG : (r >> 8) % BIG;
    }
}
static double hit_rate(const Lru *c) { return 100.0 * c->hits / (c->hits + c->misses); }
static Lru A, B;                                    /* 큰 구조체는 static (스택 보호) */

static void ex1(void) {
    printf("[연습 1] 같은 32 KiB: 엔트리 캐시 1024개 vs 맵 페이지 캐시 8장 (LBA 65536)\n");
    printf("  %-12s %10s %10s\n", "trace", "entry hit", "page hit");
    double he[W_N], hp[W_N];
    for (int k = 0; k < W_N; k++) {
        lru_init(&A, 1024, 0);
        lru_init(&B, 8, 0);
        rng = 0x1234u + (uint32_t)k;
        for (uint32_t i = 0; i < OPS; i++) {
            uint32_t lba = big_lba((Wk)k, i), v;
            Victim vv;
            if (!lru_get(&A, lba, &v)) lru_put(&A, lba, 0, false, &vv);
            uint32_t page = lba / PER_PAGE;                  /* 키 = 맵 페이지 번호 */
            if (!lru_get(&B, page, &v)) lru_put(&B, page, 0, false, &vv);
        }
        he[k] = hit_rate(&A); hp[k] = hit_rate(&B);
        printf("  %-12s %9.2f%% %9.2f%%\n", wk_name[k], he[k], hp[k]);
    }
    CHECK(he[W_SEQ] == 0.0 && hp[W_SEQ] > 99.0);             /* 순차: 페이지 캐시 압승 */
    CHECK(hp[W_RAND] > 11.0 && hp[W_RAND] < 14.0);           /* ≈ 8/64 = 12.5% */
    CHECK(he[W_RAND] > 1.0 && he[W_RAND] < 2.5);             /* ≈ 1024/65536 = 1.56% */
    CHECK(hp[W_HOT_CONTIG] > he[W_HOT_CONTIG]);              /* 연속 핫셋: 페이지 */
    CHECK(he[W_HOT_SCATTER] > hp[W_HOT_SCATTER]);            /* 흩어진 핫셋: 엔트리 */
}

/* ================= 연습 2: CLOCK (second-chance) ================= */
enum { NL = 4096 };
typedef struct {
    uint32_t key[256];
    uint8_t ref[256];
    int16_t slot_of[NL];              /* lba → 슬롯 (-1 = 없음). 조회는 해시든 배열이든 상관없다 */
    uint32_t cap, used, hand, hits, misses;
} Clock;
static Clock K;
static void clock_init(Clock *c, uint32_t cap) {
    memset(c, 0, sizeof *c);
    c->cap = cap;
    for (uint32_t i = 0; i < NL; i++) c->slot_of[i] = -1;
}
/* hit 은 ref = 1 만 (리스트 조작 없음). miss 면 ref==1 은 0 으로 지우며 지나가고 ref==0 을 교체.
 * 새 엔트리는 ref = 0 으로 시작 (이 구현의 선택). 쫓겨난 lba 를 돌려준다 (-1 = 없음). */
static int32_t clock_access(Clock *c, uint32_t lba) {
    int16_t s = c->slot_of[lba];
    if (s >= 0) { c->ref[s] = 1; c->hits++; return -1; }
    c->misses++;
    if (c->used < c->cap) {
        uint32_t i = c->used++;
        c->key[i] = lba; c->ref[i] = 0; c->slot_of[lba] = (int16_t)i;
        return -1;
    }
    while (c->ref[c->hand]) { c->ref[c->hand] = 0; c->hand = (c->hand + 1) % c->cap; }
    uint32_t i = c->hand;
    int32_t old = (int32_t)c->key[i];
    c->slot_of[old] = -1;
    c->key[i] = lba; c->ref[i] = 0; c->slot_of[lba] = (int16_t)i;
    c->hand = (i + 1) % c->cap;
    return old;
}
static uint32_t small_lba(int k, uint32_t i) {
    uint32_t r;
    if (k == 0) return i % 240u;                              /* loop-240 */
    if (k == 1) return xs32() % NL;                           /* random */
    r = xs32();                                               /* hotspot (본문과 같은 모양) */
    return r % 100u < 90u ? 1024u + (r >> 8) % 205u : (r >> 8) % NL;
}
static void ex2(void) {
    /* 고정 예: cap 3, A B C A D → LRU 도 CLOCK 도 B 를 쫓아낸다 */
    Victim v;
    uint32_t x;
    clock_init(&K, 3);
    lru_init(&A, 3, 0);
    const uint32_t seq[5] = {10, 11, 12, 10, 13};
    int32_t cv = -1;
    for (int i = 0; i < 5; i++) {
        cv = clock_access(&K, seq[i]);
        if (!lru_get(&A, seq[i], &x)) lru_put(&A, seq[i], 0, false, &v);
    }
    CHECK(cv == 11 && v.valid && v.key == 11);

    printf("[연습 2] cap 256, LBA 4096: LRU vs CLOCK hit rate\n");
    printf("  %-9s %8s %8s\n", "trace", "LRU", "CLOCK");
    const char *nm[3] = {"loop-240", "random", "hotspot"};
    double hl[3], hc[3];
    for (int k = 0; k < 3; k++) {
        lru_init(&A, 256, 0);
        clock_init(&K, 256);
        rng = 0xBEEFu + (uint32_t)k;
        for (uint32_t i = 0; i < OPS; i++) {
            uint32_t lba = small_lba(k, i);
            if (!lru_get(&A, lba, &x)) lru_put(&A, lba, 0, false, &v);
            clock_access(&K, lba);
        }
        hl[k] = hit_rate(&A);
        hc[k] = 100.0 * K.hits / (K.hits + K.misses);
        printf("  %-9s %7.2f%% %7.2f%%\n", nm[k], hl[k], hc[k]);
    }
    CHECK(hl[0] == hc[0]);                                     /* 다 들어가면 둘 다 같다 */
    CHECK(hc[1] > hl[1] - 1.0 && hc[1] < hl[1] + 1.0);
    CHECK(hc[2] > hl[2] - 3.0 && hc[2] < hl[2] + 3.0);         /* 근사지만 가깝다 */
}

/* ================= 연습 3: clean-first 축출 창 ================= */
static uint32_t truth[NL], fmap[NL];
typedef struct { double hit; uint32_t wb; bool ok; } Ex3;
static Ex3 run_cflru(uint32_t window) {
    lru_init(&A, 256, window);
    for (uint32_t l = 0; l < NL; l++) truth[l] = fmap[l] = 0x10000u + l;
    uint32_t next = 0x100000u, wb = 0;
    bool ok = true;
    rng = 0xC0FFEEu + 3u;
    for (uint32_t i = 0; i < OPS; i++) {
        bool w = xs32() % 100u < 30u;
        uint32_t lba = small_lba(2, i), pba;
        Victim v = {0};
        if (!lru_get(&A, lba, &pba)) { pba = fmap[lba]; if (!w) lru_put(&A, lba, pba, false, &v); }
        if (w) { truth[lba] = next; lru_put(&A, lba, next++, true, &v); }
        else ok = ok && pba == truth[lba];
        if (v.valid && v.dirty) { fmap[v.key] = v.val; wb++; }
    }
    for (ListNode *p = A.lru.next; p != &A.lru; p = p->next) {   /* flush */
        Ent *e = container_of(p, Ent, node);
        if (e->dirty) { fmap[e->key] = e->val; e->dirty = false; }
    }
    ok = ok && memcmp(fmap, truth, sizeof truth) == 0;
    return (Ex3){hit_rate(&A), wb, ok};
}
static void ex3(void) {
    printf("[연습 3] hotspot, cap 256, 쓰기 30%%: 꼬리 window 안의 clean 을 먼저 쫓아내기\n");
    printf("  %6s %8s %10s\n", "window", "hit", "writeback");
    const uint32_t ws[4] = {0, 16, 64, 128};
    Ex3 r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = run_cflru(ws[i]);
        printf("  %6u %7.2f%% %10u\n", ws[i], r[i].hit, r[i].wb);
        CHECK(r[i].ok);                                         /* 축출 순서가 바뀌어도 데이터는 맞다 */
    }
    CHECK(r[0].wb > r[1].wb && r[1].wb > r[2].wb && r[2].wb > r[3].wb);   /* 창이 클수록 write-back 감소 */
    /* hit rate 는 이 트레이스에선 오히려 올랐다 — 차가운 1회성 읽기(clean)가 먼저 나가서. 일반 법칙은 아님 */
}

/* ================= 연습 4: 같은 맵 페이지 dirty 형제 함께 반영 ================= */
static uint32_t btruth[BIG], bfmap[BIG];
/* dirty victim 이 나가면 그 맵 페이지(4 KiB)를 한 번 program 한다.
 * batch = true 면 캐시 안의 같은 페이지 dirty 형제도 그 program 에 실어 clean 으로 만든다. */
static uint32_t run_batch(bool batch, bool *ok) {
    lru_init(&A, 1024, 0);
    for (uint32_t l = 0; l < BIG; l++) btruth[l] = bfmap[l] = l;
    uint32_t next = 0x100000u, programs = 0;
    rng = 0xFACEu;
    *ok = true;
    for (uint32_t i = 0; i < OPS; i++) {
        bool w = xs32() % 100u < 30u;
        uint32_t lba = big_lba(W_HOT_CONTIG, i), pba;
        Victim v = {0};
        if (!lru_get(&A, lba, &pba)) { pba = bfmap[lba]; if (!w) lru_put(&A, lba, pba, false, &v); }
        if (w) { btruth[lba] = next; lru_put(&A, lba, next++, true, &v); }
        else *ok = *ok && pba == btruth[lba];
        if (v.valid && v.dirty) {
            bfmap[v.key] = v.val;
            programs++;
            if (batch)
                for (ListNode *p = A.lru.next; p != &A.lru; p = p->next) {
                    Ent *e = container_of(p, Ent, node);
                    if (e->dirty && e->key / PER_PAGE == v.key / PER_PAGE) { bfmap[e->key] = e->val; e->dirty = false; }
                }
        }
    }
    /* 셧다운 flush: 페이지별로 묶어서 program. 64 페이지 비트맵 (DS-B08) */
    uint64_t pages = 0;
    for (ListNode *p = A.lru.next; p != &A.lru; p = p->next) {
        Ent *e = container_of(p, Ent, node);
        if (e->dirty) { bfmap[e->key] = e->val; e->dirty = false; pages |= 1ull << (e->key / PER_PAGE); }
    }
    programs += (uint32_t)__builtin_popcountll(pages);
    *ok = *ok && memcmp(bfmap, btruth, sizeof btruth) == 0;
    return programs;
}
static void ex4(void) {
    bool ok1, ok2;
    uint32_t naive = run_batch(false, &ok1), batched = run_batch(true, &ok2);
    printf("[연습 4] hot-contig, cap 1024, 쓰기 30%%: 맵 페이지 program 횟수 naive %u → batch %u\n",
           naive, batched);
    CHECK(ok1 && ok2);
    CHECK(batched < naive);
}

/* ================= 연습 5: TRIM ================= */
#define UNMAPPED 0xFFFFFFFFu
static void ex5(void) {
    uint32_t fm[8], p;
    Victim v;
    for (uint32_t l = 0; l < 8; l++) fm[l] = 500 + l;
    /* 틀린 TRIM: 캐시에서 지우기만 → 다시 읽으면 플래시의 옛 PBA 가 부활 */
    lru_init(&A, 2, 0);
    lru_put(&A, 3, 77, true, &v);                              /* LBA 3 을 새로 씀 (dirty) */
    lru_remove(&A, 3);                                         /* "TRIM" */
    if (!lru_get(&A, 3, &p)) p = fm[3];
    CHECK(p == 503);                                           /* 버그 재현: 지운 LBA 가 옛 데이터를 가리킴 */
    /* 옳은 TRIM: UNMAPPED 를 dirty 로 put → 쫓겨날 때 플래시 맵에도 반영 */
    lru_init(&A, 2, 0);
    lru_put(&A, 3, 77, true, &v);
    lru_put(&A, 3, UNMAPPED, true, &v);
    CHECK(lru_get(&A, 3, &p) && p == UNMAPPED);
    lru_put(&A, 4, fm[4], false, &v);
    lru_put(&A, 5, fm[5], false, &v);                          /* 3 이 쫓겨남 */
    if (v.valid && v.dirty) fm[v.key] = v.val;
    CHECK(v.valid && v.key == 3 && fm[3] == UNMAPPED);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
