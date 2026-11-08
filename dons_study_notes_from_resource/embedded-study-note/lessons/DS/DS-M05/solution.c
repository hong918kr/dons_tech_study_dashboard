/* DS-M05 LRU 캐시: 해시 + 이중 연결 리스트
 * FTL 매핑 캐시(CMT, Cached Mapping Table) 가정: LBA → PBA.
 * 침입형(intrusive) 이중 리스트 = 최근 사용 순서, 분리 체이닝 해시 = O(1) 조회.
 * 엔트리·버킷은 호출자가 준 정적 배열에서만 — malloc 금지.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- 침입형 이중 연결 리스트 (DS-B06, DS-M01에서 자세히) — 주어진 코드 ---------- */
typedef struct ListNode { struct ListNode *prev, *next; } ListNode;
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

static void list_init(ListNode *h) { h->next = h->prev = h; }
static bool list_empty(const ListNode *h) { return h->next == h; }
static void list_insert_between(ListNode *n, ListNode *p, ListNode *q) {
    n->prev = p; n->next = q; p->next = n; q->prev = n;
}
static void list_add_head(ListNode *h, ListNode *n) { list_insert_between(n, h, h->next); }
static void list_del(ListNode *n) {
    n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = NULL;
}
static void list_move_head(ListNode *h, ListNode *n) { list_del(n); list_add_head(h, n); }

/* ---------- 캐시 자료구조 ---------- */
typedef struct LruEntry {
    ListNode node;            /* LRU 리스트 또는 free 리스트에 걸리는 고리 */
    struct LruEntry *hnext;   /* 같은 버킷의 다음 엔트리 (분리 체이닝, DS-B10) */
    uint32_t lba, pba;
    bool dirty;               /* 캐시 값이 플래시의 맵보다 새롭다 → 쫓겨날 때 write-back */
} LruEntry;

typedef struct { bool valid; uint32_t lba, pba; bool dirty; } LruVictim;

typedef struct {
    ListNode lru;             /* sentinel: lru.next = MRU(가장 최근), lru.prev = LRU(가장 오래됨) */
    ListNode free_list;       /* 아직 안 쓴 엔트리 */
    LruEntry *pool;
    LruEntry **bucket;
    uint32_t cap, size, nbuckets, shift;
    uint32_t hits, misses, evictions;
} LruCache;

/* cap ≥ 1, nbuckets = 2 이상의 2의 거듭제곱 (shift 가 32 가 되면 UB) */
static void lru_init(LruCache *c, LruEntry *pool, uint32_t cap, LruEntry **bucket, uint32_t nbuckets) {
    memset(c, 0, sizeof *c);
    list_init(&c->lru);
    list_init(&c->free_list);
    c->pool = pool; c->cap = cap; c->bucket = bucket; c->nbuckets = nbuckets;
    uint32_t bits = 0;
    while ((1u << bits) < nbuckets) bits++;
    c->shift = 32u - bits;
    for (uint32_t i = 0; i < nbuckets; i++) bucket[i] = NULL;
    for (uint32_t i = 0; i < cap; i++) {
        pool[i] = (LruEntry){0};
        list_add_head(&c->free_list, &pool[i].node);
    }
}
/* Fibonacci 해싱: 황금비 상수를 곱한 뒤 상위 bits 비트 (DS-B09) */
static uint32_t lba_hash(const LruCache *c, uint32_t lba) { return (lba * 0x9E3779B9u) >> c->shift; }

/* ---------------- Part A: 조회와 삽입 ---------------- */
/* 해시만 본다. 순서(최근성)는 바꾸지 않는다. 없으면 NULL. */
static LruEntry *lru_find(LruCache *c, uint32_t lba) {
    for (LruEntry *e = c->bucket[lba_hash(c, lba)]; e; e = e->hnext)
        if (e->lba == lba) return e;
    return NULL;
}
/* hit: *pba 에 값, 엔트리를 MRU 로, hits++, true.  miss: misses++, false. */
static bool lru_get(LruCache *c, uint32_t lba, uint32_t *pba) {
    LruEntry *e = lru_find(c, lba);
    if (!e) { c->misses++; return false; }
    list_move_head(&c->lru, &e->node);
    *pba = e->pba;
    c->hits++;
    return true;
}
/* e 를 자기 버킷 체인에서 떼어낸다 (체인 머리·가운데·끝 모두 같은 코드) */
static void hash_unlink(LruCache *c, LruEntry *e) {
    LruEntry **pp = &c->bucket[lba_hash(c, e->lba)];
    while (*pp != e) pp = &(*pp)->hnext;
    *pp = e->hnext;
    e->hnext = NULL;
}
/* 있으면 갱신(dirty 는 OR) + MRU. 없으면 free 엔트리, 없으면 LRU 꼬리를 쫓아내고 재사용.
 * 쫓겨난 엔트리는 *v 에 (valid=true). 아니면 v->valid = false. */
static void lru_put(LruCache *c, uint32_t lba, uint32_t pba, bool dirty, LruVictim *v) {
    v->valid = false;
    LruEntry *e = lru_find(c, lba);
    if (e) {
        e->pba = pba;
        e->dirty = e->dirty || dirty;
        list_move_head(&c->lru, &e->node);
        return;
    }
    if (!list_empty(&c->free_list)) {
        ListNode *n = c->free_list.next;
        list_del(n);
        e = container_of(n, LruEntry, node);
    } else {
        ListNode *n = c->lru.prev;                       /* LRU 꼬리 = 가장 오래 안 쓴 것 */
        e = container_of(n, LruEntry, node);
        *v = (LruVictim){true, e->lba, e->pba, e->dirty};
        hash_unlink(c, e);
        list_del(n);
        c->size--;
        c->evictions++;
    }
    e->lba = lba; e->pba = pba; e->dirty = dirty;
    uint32_t b = lba_hash(c, lba);
    e->hnext = c->bucket[b];
    c->bucket[b] = e;
    list_add_head(&c->lru, &e->node);
    c->size++;
}

/* ---------------- Part B: 삭제와 불변식 ---------------- */
/* 있으면 해시·LRU 에서 떼어 free 리스트로, 지운 내용을 *out 에, true. 없으면 false. */
static bool lru_remove(LruCache *c, uint32_t lba, LruVictim *out) {
    out->valid = false;
    LruEntry *e = lru_find(c, lba);
    if (!e) return false;
    *out = (LruVictim){true, e->lba, e->pba, e->dirty};
    hash_unlink(c, e);
    list_del(&e->node);
    list_add_head(&c->free_list, &e->node);
    c->size--;
    return true;
}

static bool in_pool(const LruCache *c, const LruEntry *e) {
    uintptr_t p = (uintptr_t)e, lo = (uintptr_t)c->pool, hi = (uintptr_t)(c->pool + c->cap);
    return p >= lo && p < hi && (p - lo) % sizeof(LruEntry) == 0;
}
/* 리스트 걷기: 모든 링크 p->next->prev == p, NULL 없음, 최대 cap 개. 개수를 *n 에. */
static bool walk_ok(const LruCache *c, const ListNode *h, uint32_t *n) {
    *n = 0;
    for (const ListNode *p = h;;) {
        const ListNode *nx = p->next;
        if (!nx || nx->prev != p) return false;
        if (nx == h) return true;
        if (++*n > c->cap || !in_pool(c, container_of(nx, LruEntry, node))) return false;
        p = nx;
    }
}
/* 불변식: ① LRU·free 리스트 링크 일관 ② LRU 개수 == size, free 개수 == cap - size
 * ③ LRU 의 모든 엔트리가 자기 버킷에서 정확히 1번 찾힘 (같은 lba 중복 없음)
 * ④ 체인 전체 엔트리 수 == size, 각 체인 엔트리의 해시 == 버킷 번호. 망가져도 반드시 멈춘다. */
static bool lru_check(const LruCache *c) {
    uint32_t nl, nf;
    if (!walk_ok(c, &c->lru, &nl) || !walk_ok(c, &c->free_list, &nf)) return false;
    if (nl != c->size || nf != c->cap - c->size) return false;
    for (const ListNode *p = c->lru.next; p != &c->lru; p = p->next) {
        const LruEntry *e = container_of(p, LruEntry, node);
        uint32_t found = 0, same = 0, steps = 0;
        for (const LruEntry *q = c->bucket[lba_hash(c, e->lba)]; q; q = q->hnext) {
            if (++steps > c->cap) return false;
            if (q == e) found++;
            if (q->lba == e->lba) same++;
        }
        if (found != 1 || same != 1) return false;
    }
    uint32_t total = 0;
    for (uint32_t b = 0; b < c->nbuckets; b++)
        for (const LruEntry *q = c->bucket[b]; q; q = q->hnext)
            if (++total > c->size || !in_pool(c, q) || lba_hash(c, q->lba) != b) return false;
    return total == c->size;
}

/* ---------------- Part C: FTL 매핑 캐시 (write-back) ---------------- */
typedef struct {
    LruCache cache;
    uint32_t *flash_map;      /* 플래시에 저장된 전체 L2P 테이블 (시뮬레이션) */
    uint32_t map_loads;       /* miss → 맵 페이지 읽기 (NAND read) */
    uint32_t map_writebacks;  /* dirty 엔트리를 플래시 맵에 반영 (NAND program) */
} Ftl;

/* 쫓겨난 엔트리가 dirty 면 플래시 맵에 반영. clean 이면 버리면 끝 (플래시와 같으니까). */
static void ftl_writeback(Ftl *f, const LruVictim *v) {
    if (v->valid && v->dirty) {
        f->flash_map[v->lba] = v->pba;
        f->map_writebacks++;
    }
}
/* 호스트 읽기: LBA 의 현재 PBA. miss 면 플래시 맵에서 읽어 clean 으로 캐시. */
static uint32_t ftl_read(Ftl *f, uint32_t lba) {
    uint32_t pba;
    if (lru_get(&f->cache, lba, &pba)) return pba;
    pba = f->flash_map[lba];
    f->map_loads++;
    LruVictim v;
    lru_put(&f->cache, lba, pba, false, &v);
    ftl_writeback(f, &v);
    return pba;
}
/* 호스트 쓰기: 데이터는 new_pba 에 out-of-place 로 써졌다. 매핑을 dirty 로 갱신.
 * 옛 PBA 를 돌려준다 (GC 의 valid 카운트를 깎는 데 필요 → miss 면 맵을 읽어 온다). */
static uint32_t ftl_write(Ftl *f, uint32_t lba, uint32_t new_pba) {
    uint32_t old;
    if (!lru_get(&f->cache, lba, &old)) {
        old = f->flash_map[lba];
        f->map_loads++;
    }
    LruVictim v;
    lru_put(&f->cache, lba, new_pba, true, &v);
    ftl_writeback(f, &v);
    return old;
}
/* Flush/셧다운: dirty 엔트리를 전부 플래시 맵에 반영하고 clean 으로. 순서는 건드리지 않는다. */
static uint32_t ftl_flush(Ftl *f) {
    uint32_t n = 0;
    for (ListNode *p = f->cache.lru.next; p != &f->cache.lru; p = p->next) {
        LruEntry *e = container_of(p, LruEntry, node);
        if (e->dirty) {
            f->flash_map[e->lba] = e->pba;
            e->dirty = false;
            f->map_writebacks++;
            n++;
        }
    }
    return n;
}

/* ================= 테스트 하네스 (수정하지 않는다) ================= */
/* 참조 모델: MRU 순서 배열 + 선형 탐색 + memmove. 느리지만 틀리기 어렵다. */
#define REF_MAX 1024u
typedef struct {
    uint32_t lba[REF_MAX], pba[REF_MAX];
    bool dirty[REF_MAX];
    uint32_t n, cap, hits, misses;
} RefLru;
static int ref_find(const RefLru *r, uint32_t lba) {
    for (uint32_t i = 0; i < r->n; i++) if (r->lba[i] == lba) return (int)i;
    return -1;
}
static void ref_to_front(RefLru *r, int i) {
    uint32_t l = r->lba[i], p = r->pba[i]; bool d = r->dirty[i];
    memmove(&r->lba[1], &r->lba[0], (size_t)i * sizeof r->lba[0]);
    memmove(&r->pba[1], &r->pba[0], (size_t)i * sizeof r->pba[0]);
    memmove(&r->dirty[1], &r->dirty[0], (size_t)i * sizeof r->dirty[0]);
    r->lba[0] = l; r->pba[0] = p; r->dirty[0] = d;
}
static bool ref_get(RefLru *r, uint32_t lba, uint32_t *pba) {
    int i = ref_find(r, lba);
    if (i < 0) { r->misses++; return false; }
    ref_to_front(r, i); *pba = r->pba[0]; r->hits++;
    return true;
}
static void ref_put(RefLru *r, uint32_t lba, uint32_t pba, bool dirty, LruVictim *v) {
    v->valid = false;
    int i = ref_find(r, lba);
    if (i >= 0) { ref_to_front(r, i); r->pba[0] = pba; r->dirty[0] = r->dirty[0] || dirty; return; }
    if (r->n == r->cap) { r->n--; *v = (LruVictim){true, r->lba[r->n], r->pba[r->n], r->dirty[r->n]}; }
    r->lba[r->n] = lba; r->pba[r->n] = pba; r->dirty[r->n] = dirty; r->n++;
    ref_to_front(r, (int)r->n - 1);
}
static bool ref_remove(RefLru *r, uint32_t lba, LruVictim *out) {
    out->valid = false;
    int i = ref_find(r, lba);
    if (i < 0) return false;
    *out = (LruVictim){true, r->lba[i], r->pba[i], r->dirty[i]};
    for (uint32_t k = (uint32_t)i; k + 1 < r->n; k++) {
        r->lba[k] = r->lba[k + 1]; r->pba[k] = r->pba[k + 1]; r->dirty[k] = r->dirty[k + 1];
    }
    r->n--;
    return true;
}
/* 캐시의 MRU→LRU 순서가 모델과 같은가 (lba, pba, dirty 모두) */
static bool same_order(const LruCache *c, const RefLru *r) {
    uint32_t i = 0;
    for (const ListNode *p = c->lru.next; p && p != &c->lru; p = p->next, i++) {
        const LruEntry *e = container_of(p, LruEntry, node);
        if (i >= r->n || e->lba != r->lba[i] || e->pba != r->pba[i] || e->dirty != r->dirty[i]) return false;
    }
    return i == r->n && c->size == r->n;
}
static bool same_victim(const LruVictim *a, const LruVictim *b) {
    if (a->valid != b->valid) return false;
    return !a->valid || (a->lba == b->lba && a->pba == b->pba && a->dirty == b->dirty);
}
/* MRU→LRU 의 lba 목록 비교 (망가진 리스트에서도 max 에서 멈춘다) */
static bool order_is(const LruCache *c, const uint32_t *want, uint32_t n) {
    uint32_t i = 0;
    for (const ListNode *p = c->lru.next; p && p != &c->lru && i <= n; p = p->next, i++)
        if (i == n || container_of(p, LruEntry, node)->lba != want[i]) return false;
    return i == n && c->size == n;
}

static uint32_t rng = 0xC0FFEEu;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* Part B 랜덤 테스트: 작은 캐시(cap 8, 버킷 2 → 긴 체인)에 get/put/remove 를 섞고
 * 매 연산 뒤 lru_check + 모델과 반환값·victim·순서 비교. */
static bool random_test(int ops) {
    static LruEntry pool[8];
    static LruEntry *bk[2];
    static RefLru ref;
    LruCache c;
    lru_init(&c, pool, 8, bk, 2);
    memset(&ref, 0, sizeof ref); ref.cap = 8;
    for (int step = 0; step < ops; step++) {
        uint32_t r = xs32(), lba = (r >> 8) % 24, op = r % 10;
        bool ok;
        if (op < 4) {                                         /* get */
            uint32_t p1 = 0, p2 = 0;
            bool h1 = lru_get(&c, lba, &p1), h2 = ref_get(&ref, lba, &p2);
            ok = h1 == h2 && (!h1 || p1 == p2);
        } else if (op < 8) {                                  /* put (clean/dirty) */
            LruVictim v1, v2;
            uint32_t pba = r >> 12; bool d = (r >> 7) & 1;
            lru_put(&c, lba, pba, d, &v1); ref_put(&ref, lba, pba, d, &v2);
            ok = same_victim(&v1, &v2);
        } else {                                              /* remove */
            LruVictim v1, v2;
            bool a = lru_remove(&c, lba, &v1), b = ref_remove(&ref, lba, &v2);
            ok = a == b && same_victim(&v1, &v2);
        }
        if (!ok || !lru_check(&c) || !same_order(&c, &ref) || c.hits != ref.hits || c.misses != ref.misses) {
            printf("  random_test: step %d op %u lba %u 에서 불일치\n", step, op, lba);
            return false;
        }
    }
    return true;
}

/* ---------- Part C: 접근 트레이스 + 참조 FTL 과 lockstep ---------- */
enum { NLBA = 4096, NOPS = 50000, HOT_BASE = 1024, HOT_N = 205, LOOP_N = 240 };
typedef enum { T_SEQ, T_LOOP, T_RAND, T_HOT, T_COUNT } TraceKind;
static const char *trace_name[T_COUNT] = {"seq-4096", "loop-240", "random", "hotspot"};

static uint32_t trace_lba(TraceKind k, uint32_t i) {
    uint32_t r;
    switch (k) {
    case T_SEQ:  return i % NLBA;                             /* 드라이브 전체 순차 스캔 반복 */
    case T_LOOP: return i % LOOP_N;                           /* 작은 영역을 순차로 반복 */
    case T_RAND: return xs32() % NLBA;                        /* 균등 랜덤 */
    default:
        r = xs32();                                           /* 90% 는 5% 영역(205 LBA) 에 */
        return (r % 100u < 90u) ? HOT_BASE + (r >> 8) % HOT_N : (r >> 8) % NLBA;
    }
}

typedef struct { uint32_t hits, misses, loads, wbacks, flushed, writes; } TraceResult;

static uint32_t truth[NLBA], fmap[NLBA], ref_fmap[NLBA];
static LruEntry tpool[REF_MAX];
static LruEntry *tbk[REF_MAX];
static RefLru tref;

/* 같은 연산을 (캐시 FTL) 과 (참조 모델 FTL) 에 넣고, 진짜 매핑 truth[] 와도 비교 */
static bool run_trace(TraceKind k, uint32_t cap, TraceResult *res) {
    Ftl f = {0};
    uint32_t ref_loads = 0, ref_wb = 0, next_pba = 0x100000u;
    lru_init(&f.cache, tpool, cap, tbk, cap);
    f.flash_map = fmap;
    memset(&tref, 0, sizeof tref); tref.cap = cap;
    for (uint32_t l = 0; l < NLBA; l++) truth[l] = fmap[l] = ref_fmap[l] = 0x10000u + l;
    rng = 0xC0FFEEu + (uint32_t)k;
    memset(res, 0, sizeof *res);
    for (uint32_t i = 0; i < NOPS; i++) {
        bool w = xs32() % 100u < 30u;                          /* 30% 쓰기 */
        uint32_t lba = trace_lba(k, i), got, rp = 0;
        LruVictim rv;
        bool ok;
        if (w) {
            uint32_t np = next_pba++;
            got = ftl_write(&f, lba, np);
            if (!ref_get(&tref, lba, &rp)) { rp = ref_fmap[lba]; ref_loads++; }
            ref_put(&tref, lba, np, true, &rv);
            ok = got == truth[lba] && rp == truth[lba];
            truth[lba] = np;
            res->writes++;
        } else {
            got = ftl_read(&f, lba);
            if (!ref_get(&tref, lba, &rp)) {
                rp = ref_fmap[lba]; ref_loads++;
                ref_put(&tref, lba, rp, false, &rv);
            } else rv.valid = false;
            ok = got == truth[lba] && rp == truth[lba];
        }
        if (rv.valid && rv.dirty) { ref_fmap[rv.lba] = rv.pba; ref_wb++; }
        ok = ok && f.cache.hits == tref.hits && f.cache.misses == tref.misses
                && f.map_loads == ref_loads && f.map_writebacks == ref_wb;
        if (ok && (i % 1024u == 0)) ok = lru_check(&f.cache) && same_order(&f.cache, &tref);
        if (!ok) { printf("  %s cap %u: op %u (lba %u) 에서 불일치\n", trace_name[k], cap, i, lba); return false; }
    }
    res->hits = f.cache.hits; res->misses = f.cache.misses;
    res->loads = f.map_loads; res->wbacks = f.map_writebacks;
    res->flushed = ftl_flush(&f);                              /* 셧다운: 남은 dirty 를 전부 반영 */
    if (!lru_check(&f.cache) || memcmp(fmap, truth, sizeof truth) != 0) {
        printf("  %s cap %u: flush 후 플래시 맵 != truth\n", trace_name[k], cap);
        return false;
    }
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    LruEntry pool[4];
    LruEntry *bk[4];
    LruCache c;
    LruVictim v;
    uint32_t p = 0;

    /* Part A: cap 4 */
    lru_init(&c, pool, 4, bk, 4);
    CHECK(!lru_get(&c, 7, &p) && c.misses == 1 && c.size == 0);
    for (uint32_t l = 1; l <= 4; l++) { lru_put(&c, l, 100 + l, false, &v); CHECK(!v.valid); }
    CHECK(order_is(&c, (uint32_t[]){4, 3, 2, 1}, 4) && lru_check(&c));
    CHECK(lru_get(&c, 1, &p) && p == 101 && order_is(&c, (uint32_t[]){1, 4, 3, 2}, 4));
    lru_put(&c, 5, 105, false, &v);                            /* 꽉 참 → LRU(2) 쫓겨남, 1 은 살아남음 */
    CHECK(v.valid && v.lba == 2 && v.pba == 102 && !v.dirty);
    CHECK(order_is(&c, (uint32_t[]){5, 1, 4, 3}, 4) && lru_find(&c, 2) == NULL && lru_check(&c));
    lru_put(&c, 3, 333, true, &v);                             /* 이미 있음 → 갱신 + MRU, 쫓아내지 않음 */
    CHECK(!v.valid && c.size == 4 && order_is(&c, (uint32_t[]){3, 5, 1, 4}, 4));
    CHECK(lru_find(&c, 3) && lru_find(&c, 3)->pba == 333 && lru_find(&c, 3)->dirty);
    CHECK(!lru_get(&c, 2, &p) && order_is(&c, (uint32_t[]){3, 5, 1, 4}, 4));   /* miss 는 순서 불변 */
    lru_put(&c, 6, 106, false, &v); lru_put(&c, 7, 107, false, &v); lru_put(&c, 8, 108, false, &v);
    CHECK(v.valid && v.lba == 5);                              /* 4, 1, 5 순서로 나감 */
    lru_put(&c, 9, 109, false, &v);
    CHECK(v.valid && v.lba == 3 && v.pba == 333 && v.dirty);   /* dirty victim → 호출자가 write-back */
    CHECK(c.hits == 1 && c.misses == 2 && c.evictions == 5 && lru_check(&c));

    /* Part B: 충돌 체인 가운데 삭제 (버킷 2개) */
    lru_init(&c, pool, 4, bk, 2);
    uint32_t k3[3], nk = 0;
    for (uint32_t l = 0; nk < 3; l++) if (lba_hash(&c, l) == 0) k3[nk++] = l;
    for (int i = 0; i < 3; i++) lru_put(&c, k3[i], 200 + (uint32_t)i, i == 1, &v);   /* 체인: k2 → k1 → k0 */
    CHECK(lru_remove(&c, k3[1], &v) && v.valid && v.pba == 201 && v.dirty);         /* 체인 가운데 */
    CHECK(lru_find(&c, k3[0]) && lru_find(&c, k3[2]) && !lru_find(&c, k3[1]) && c.size == 2 && lru_check(&c));
    CHECK(!lru_remove(&c, k3[1], &v) && !v.valid);                                  /* 두 번째는 false */
    CHECK(lru_remove(&c, k3[2], &v) && lru_remove(&c, k3[0], &v));                  /* 체인 머리, 마지막 */
    CHECK(c.size == 0 && bk[0] == NULL && list_empty(&c.lru) && lru_check(&c));
    for (uint32_t l = 10; l < 14; l++) { lru_put(&c, l, l, false, &v); CHECK(!v.valid); }  /* free 재사용 */
    CHECK(c.size == 4 && lru_check(&c));

    /* Part B: cap 1 */
    LruEntry one[1];
    lru_init(&c, one, 1, bk, 2);
    lru_put(&c, 40, 1, false, &v);
    lru_put(&c, 41, 2, true, &v);
    CHECK(v.valid && v.lba == 40 && !lru_get(&c, 40, &p) && lru_get(&c, 41, &p) && p == 2 && lru_check(&c));

    /* Part B: 검사기가 망가진 상태를 잡는다 */
    lru_init(&c, pool, 4, bk, 4);
    for (uint32_t l = 1; l <= 4; l++) lru_put(&c, l, l, false, &v);
    CHECK(lru_check(&c));
    c.size++;                                                  /* 개수 불일치 */
    CHECK(!lru_check(&c));
    c.size--;
    LruEntry *e2 = lru_find(&c, 2);
    uint32_t b2 = lba_hash(&c, 2);
    LruEntry *saved = bk[b2];
    if (e2) bk[b2] = e2->hnext;                                /* 해시에서만 빠짐 (리스트엔 있음) */
    CHECK(!lru_check(&c));
    bk[b2] = saved;
    CHECK(lru_check(&c));
    if (e2) e2->lba = 3;                                       /* 중복 키 / 엉뚱한 버킷 */
    CHECK(!lru_check(&c));
    if (e2) e2->lba = 2;
    if (e2) e2->node.next->prev = NULL;                        /* 역방향 링크 끊김 */
    CHECK(!lru_check(&c));
    if (e2) e2->node.next->prev = &e2->node;
    CHECK(lru_check(&c));

    CHECK(random_test(30000));

    /* Part C: 작은 FTL — write-back 의미 확인 (cap 2) */
    uint32_t fm[16];
    for (uint32_t l = 0; l < 16; l++) fm[l] = 1000 + l;
    LruEntry fp[2];
    LruEntry *fb[2];
    Ftl f = {0};
    lru_init(&f.cache, fp, 2, fb, 2);
    f.flash_map = fm;
    CHECK(ftl_read(&f, 3) == 1003 && f.map_loads == 1);
    CHECK(ftl_write(&f, 3, 50) == 1003 && f.map_loads == 1);   /* hit: 맵 로드 없음, 옛 PBA 반환 */
    CHECK(fm[3] == 1003 && ftl_read(&f, 3) == 50);             /* 플래시는 아직 옛 값 (write-back) */
    ftl_read(&f, 4);
    ftl_read(&f, 5);                                           /* 3(dirty) 쫓겨남 → 반영 */
    CHECK(fm[3] == 50 && f.map_writebacks == 1);
    ftl_read(&f, 6);                                           /* 4(clean) 쫓겨남 → 쓰기 없음 */
    CHECK(f.map_writebacks == 1 && f.map_loads == 4);
    ftl_write(&f, 6, 60);
    CHECK(ftl_flush(&f) == 1 && fm[6] == 60 && ftl_flush(&f) == 0);
    CHECK(ftl_read(&f, 3) == 50 && lru_check(&f.cache));

    /* Part C: 트레이스 4종 × 캐시 크기 3종, 참조 모델과 lockstep */
    static const uint32_t caps[3] = {64, 256, 1024};
    TraceResult tr[T_COUNT][3];
    for (int k = 0; k < T_COUNT; k++)
        for (int j = 0; j < 3; j++) CHECK(run_trace((TraceKind)k, caps[j], &tr[k][j]));
    CHECK(tr[T_SEQ][2].hits == 0);                             /* 캐시보다 큰 순차 반복 → 0% */
    CHECK(tr[T_LOOP][0].hits == 0 && tr[T_LOOP][1].misses == LOOP_N);   /* LRU 절벽 */

    printf("\nLBA 공간 %d, 연산 %d (쓰기 30%%), sizeof(LruEntry) = %zu B\n", NLBA, NOPS, sizeof(LruEntry));
    printf("%-9s %5s %8s %8s %8s %10s %8s\n", "trace", "cap", "hit%", "loads", "writes", "writeback", "flush");
    for (int k = 0; k < T_COUNT; k++)
        for (int j = 0; j < 3; j++) {
            const TraceResult *t = &tr[k][j];
            uint32_t n = t->hits + t->misses;
            printf("%-9s %5u %7.2f%% %8u %8u %10u %8u\n", trace_name[k], caps[j],
                   n ? 100.0 * t->hits / n : 0.0, t->loads, t->writes, t->wbacks, t->flushed);
        }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
