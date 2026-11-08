/* DS-M04 버디 할당기: 분할과 병합
 * 64 KB 정적 아레나, order 0 (64 B) ~ order 10 (64 KB = 아레나 전체). malloc 없음.
 * 구성: order 별 free list (sentinel 원형 이중 리스트, 링크는 free 블록 안에 내장 — DS-B06)
 *     + 비트맵 2개: freemap  = "이 블록이 free list 에 있다"
 *                  allocmap = "이 블록이 할당된 블록의 머리다" (free(ptr) 가 order 를 찾는 데 사용)
 * 비트 번호 = 완전 이진 트리 노드 번호 (DS-B12 힙과 같은 1-based 번호):
 *     order k, unit 오프셋 u 인 블록의 노드 = (NUNITS >> k) + (u >> k)   → 루트(order 10) = 1
 *     버디 노드 = node ^ 1,  부모 노드 = node >> 1  (unit 으로는 버디 = u ^ (1 << k))
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MIN_SHIFT   6u                        /* order 0 블록 = 64 B */
#define MAX_ORDER   10u                       /* order 10 = 64 KB = 아레나 전체 */
#define NUNITS      (1u << MAX_ORDER)         /* 64 B 단위(unit) 개수 = 1024 */
#define ARENA_SIZE  (NUNITS << MIN_SHIFT)     /* 65536 B */
#define NNODES      (2u * NUNITS)             /* 트리 노드 번호 1..2047 (0 은 미사용) */

typedef struct fblk { struct fblk *next, *prev; } fblk_t;     /* free 블록의 앞 16 B 에 내장 */
typedef union {                                                /* 아레나의 64 B 한 칸 */
    fblk_t      link;
    uint8_t     bytes[1u << MIN_SHIFT];
    max_align_t align;
} unit_t;
_Static_assert(sizeof(unit_t) == (1u << MIN_SHIFT), "unit must be exactly one order-0 block");

typedef struct {
    unit_t   mem[NUNITS];                     /* 64 KB 아레나 (.bss) */
    fblk_t   head[MAX_ORDER + 1];             /* order 별 free list sentinel */
    uint32_t nfree[MAX_ORDER + 1];            /* order 별 free 블록 개수 */
    uint32_t avail;                           /* bit k = free list k 가 비어 있지 않음 */
    uint32_t freemap[NNODES / 32];            /* 노드 비트 = free list 에 있음 (256 B) */
    uint32_t allocmap[NNODES / 32];           /* 노드 비트 = 할당된 블록의 머리 (256 B) */
} buddy_t;

typedef struct {
    uint32_t free_bytes;                      /* free 블록 크기의 합 */
    uint32_t largest_free;                    /* 가장 큰 free 블록 (바이트). 꽉 차면 0 */
    uint32_t nblocks;                         /* free 블록 개수 */
    double   frag;                            /* 외부 단편화 = 1 - largest/free (free == 0 이면 0) */
} bstats_t;

static uint32_t g_listops;                    /* free list push/remove 횟수 (Part C 결정성 측정) */

/* ---------------- 주어진 도구: 노드 번호, 비트맵, free list ---------------- */
static uint32_t node_of(uint32_t order, uint32_t u) { return (NUNITS >> order) + (u >> order); }
static bool bm_test(const uint32_t *m, uint32_t i) { return (m[i >> 5] >> (i & 31u)) & 1u; }
static void bm_set(uint32_t *m, uint32_t i) { m[i >> 5] |= 1u << (i & 31u); }
static void bm_clr(uint32_t *m, uint32_t i) { m[i >> 5] &= ~(1u << (i & 31u)); }

static fblk_t *blk_at(buddy_t *b, uint32_t u) { return &b->mem[u].link; }
static uint32_t blk_unit(const buddy_t *b, const fblk_t *f) {
    return (uint32_t)((const unit_t *)(const void *)f - b->mem);   /* link 는 union 의 멤버 → 같은 주소 */
}

static void fl_push(buddy_t *b, uint32_t order, uint32_t u) {
    fblk_t *f = blk_at(b, u), *h = &b->head[order];
    f->next = h->next; f->prev = h;
    h->next->prev = f; h->next = f;
    b->nfree[order]++;
    b->avail |= 1u << order;
    bm_set(b->freemap, node_of(order, u));
    g_listops++;
}
static void fl_remove(buddy_t *b, uint32_t order, uint32_t u) {
    fblk_t *f = blk_at(b, u);
    f->prev->next = f->next; f->next->prev = f->prev;
    if (--b->nfree[order] == 0) b->avail &= ~(1u << order);
    bm_clr(b->freemap, node_of(order, u));
    g_listops++;
}
static uint32_t fl_pop(buddy_t *b, uint32_t order) {          /* 호출 전 nfree[order] > 0 보장 */
    uint32_t u = blk_unit(b, b->head[order].next);
    fl_remove(b, order, u);
    return u;
}

static void buddy_init(buddy_t *b) {
    for (uint32_t k = 0; k <= MAX_ORDER; k++) {
        b->head[k].next = b->head[k].prev = &b->head[k];
        b->nfree[k] = 0;
    }
    b->avail = 0;
    memset(b->freemap, 0, sizeof b->freemap);
    memset(b->allocmap, 0, sizeof b->allocmap);
    fl_push(b, MAX_ORDER, 0);                                  /* 처음엔 64 KB 블록 하나 */
}

/* ---------------- Part A: size → order, 할당 (분할) ---------------- */
/* 요청 크기를 담는 가장 작은 order. size == 0 이거나 아레나보다 크면 -1 */
static int size_to_order(size_t size) {
    (void)size;
    /* TODO: size == 0 또는 ARENA_SIZE 초과 → -1.
     *       units = ceil(size / 64), order = ceil(log2(units)) (units == 1 이면 0 — clz(0) 은 UB) */
    return -1;
}

/* order 블록 하나를 할당. 가장 작은 비어 있지 않은 order k >= order 에서 꺼내
 * k 가 order 가 될 때까지 반으로 쪼개고, 위쪽 절반(버디)은 free list 로 돌려준다 */
static void *buddy_alloc_order(buddy_t *b, uint32_t order) {
    (void)b; (void)order; (void)fl_pop;
    /* TODO: order > MAX_ORDER 면 NULL.
     *       avail 에서 order 이상인 비트 중 가장 낮은 것 = k (없으면 NULL)  — __builtin_ctz
     *       u = fl_pop(b, k); k > order 인 동안 k-- 하고 위 절반 (u + 2^k) 을 fl_push(b, k, ...)
     *       allocmap 에 node_of(order, u) 표시, &b->mem[u] 반환 */
    return NULL;
}

static void *buddy_alloc(buddy_t *b, size_t size) {
    int o = size_to_order(size);
    return o < 0 ? NULL : buddy_alloc_order(b, (uint32_t)o);
}

/* ---------------- Part B: 해제 (버디 병합) ---------------- */
/* p 가 할당된 블록의 머리가 아니면 (NULL, 범위 밖, 정렬 안 됨, 블록 내부, 이중 해제) false.
 * 맞으면 버디가 free 인 동안 합치며 올라가고, 최종 블록을 free list 에 넣는다 */
static bool buddy_free(buddy_t *b, void *p) {
    (void)b; (void)p; (void)fl_remove; (void)bm_clr;
    /* TODO: ① NULL / 아레나 밖 / 64 B 경계 아님 → false   (포인터 비교 대신 uintptr_t 로)
     *       ② u 에서 시작하는 할당 블록의 order k 를 allocmap 에서 찾기 (u 가 order k 로 정렬돼야 함)
     *          없으면 false (이중 해제, 블록 내부 포인터)
     *       ③ allocmap 비트 지우고, k < MAX_ORDER 인 동안: 버디 bu = u ^ (1 << k) 가
     *          freemap 에 있으면 fl_remove(b, k, bu), u &= ~(1 << k), k++ — 없으면 멈춤
     *       ④ fl_push(b, k, u), true */
    return false;
}

/* ---------------- Part C: 단편화 통계 ---------------- */
static void buddy_stats(const buddy_t *b, bstats_t *st) {
    (void)b;
    /* TODO: free_bytes = Σ nfree[k] * (64 << k), nblocks = Σ nfree[k],
     *       largest_free = avail 의 최상위 비트 order 의 블록 크기 (avail == 0 이면 0),
     *       frag = 1 - largest/free (free == 0 이면 0) */
    st->free_bytes = 0; st->largest_free = 0; st->nblocks = 0; st->frag = 0.0;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 불변식: ① 리스트 prev/next 일관 ② free 블록은 자기 크기로 정렬 ③ freemap 비트 == 리스트 소속
 * ④ 같은 order 의 free 버디 쌍이 없음 (= 병합이 끝까지 됨) ⑤ avail/nfree 일치
 * ⑥ free 블록 ∪ 할당 블록이 1024 unit 을 정확히 한 번씩 덮음 */
static bool buddy_check(const buddy_t *b) {
    static uint8_t cover[NUNITS];
    memset(cover, 0, sizeof cover);
    uint32_t avail = 0, total_free = 0;
    for (uint32_t k = 0; k <= MAX_ORDER; k++) {
        uint32_t cnt = 0;
        const fblk_t *h = &b->head[k];
        if (h->next->prev != h || h->prev->next != h) return false;
        for (const fblk_t *f = h->next; f != h; f = f->next) {
            if (f->next->prev != f || f->prev->next != f || ++cnt > NUNITS) return false;
            uint32_t u = blk_unit(b, f);
            if (u >= NUNITS || (u & ((1u << k) - 1u))) return false;              /* ② */
            if (!bm_test(b->freemap, node_of(k, u))) return false;                 /* ③ */
            if (k < MAX_ORDER && bm_test(b->freemap, node_of(k, u ^ (1u << k)))) return false;   /* ④ */
            for (uint32_t x = u; x < u + (1u << k); x++) if (cover[x]++) return false;
        }
        if (cnt != b->nfree[k]) return false;                                      /* ⑤ */
        if (cnt) avail |= 1u << k;
        total_free += cnt;
    }
    if (avail != b->avail) return false;
    uint32_t bits = 0;
    for (uint32_t w = 0; w < NNODES / 32; w++) bits += (uint32_t)__builtin_popcount(b->freemap[w]);
    if (bits != total_free || bm_test(b->freemap, 0) || bm_test(b->allocmap, 0)) return false;   /* ③ */
    for (uint32_t n = 1; n < NNODES; n++) {
        if (!bm_test(b->allocmap, n)) continue;
        if (bm_test(b->freemap, n)) return false;
        uint32_t k = MAX_ORDER - (31u - (uint32_t)__builtin_clz(n));              /* 노드 깊이 → order */
        uint32_t u = (n - (NUNITS >> k)) << k;
        for (uint32_t x = u; x < u + (1u << k); x++) if (cover[x]++) return false;
    }
    for (uint32_t x = 0; x < NUNITS; x++) if (cover[x] != 1) return false;        /* ⑥ */
    return true;
}

static bool only_max_block(const buddy_t *b) {                 /* 완전 복원 = order 10 블록 하나뿐 */
    for (uint32_t k = 0; k < MAX_ORDER; k++) if (b->nfree[k]) return false;
    return b->nfree[MAX_ORDER] == 1 && b->avail == (1u << MAX_ORDER);
}

static void print_counts(const char *tag, const buddy_t *b) {  /* /proc/buddyinfo 같은 order 별 개수 */
    printf("%s", tag);
    for (uint32_t k = 0; k <= MAX_ORDER; k++) printf(" %u", b->nfree[k]);
    printf("\n");
}

/* 참조 모델: unit 별 소유자. order k 할당이 가능한가 = k 정렬된 2^k 칸이 모두 비어 있는 곳이 있는가 */
static int16_t g_owner[NUNITS];
static bool ref_can_alloc(uint32_t k) {
    for (uint32_t s = 0; s < NUNITS; s += 1u << k) {
        uint32_t x = s;
        while (x < s + (1u << k) && g_owner[x] < 0) x++;
        if (x == s + (1u << k)) return true;
    }
    return false;
}
static uint32_t ref_largest_free(void) {
    for (uint32_t k = MAX_ORDER + 1u; k-- > 0;) if (ref_can_alloc(k)) return 1u << (k + MIN_SHIFT);
    return 0;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static buddy_t g_b;
typedef struct { void *p; uint32_t order, req, expire; uint8_t tag; } live_t;
static live_t g_live[NUNITS];

static uint32_t unit_off(const buddy_t *b, const void *p) { return (uint32_t)((const unit_t *)p - b->mem); }
static bool tag_ok(const live_t *l) {                          /* 다른 블록이 덮어썼는지 */
    const uint8_t *q = l->p;
    for (uint32_t i = 0; i < (1u << (l->order + MIN_SHIFT)); i++) if (q[i] != l->tag) return false;
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    buddy_t *b = &g_b;

    /* Part A: size → order */
    {
        static const size_t sz[]  = { 0, 1, 64, 65, 128, 129, 1500, 4096, 4097, 65536, 65537, SIZE_MAX };
        static const int    want[] = { -1, 0, 0, 1, 1, 2, 5, 6, 7, 10, -1, -1 };
        bool ok = true;
        for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) ok = ok && size_to_order(sz[i]) == want[i];
        CHECK(ok);
    }
    /* Part A: 초기 상태 + 64 B 하나 할당 → order 9..0 에 버디가 하나씩 남는다 */
    {
        buddy_init(b);
        CHECK(buddy_check(b) && only_max_block(b));
        print_counts("[A] init        nfree[0..10] =", b);
        void *p = buddy_alloc(b, 64);
        print_counts("[A] alloc 64B   nfree[0..10] =", b);
        bool ok = p == &b->mem[0] && buddy_check(b) && b->nfree[MAX_ORDER] == 0;
        for (uint32_t k = 0; k < MAX_ORDER; k++) ok = ok && b->nfree[k] == 1;
        CHECK(ok);
        /* 이어서 100 B(order 1) → unit 2, 64 B → unit 1, 1000 B(order 4) → unit 16 */
        void *q = buddy_alloc(b, 100), *r = buddy_alloc(b, 64), *s = buddy_alloc(b, 1000);
        printf("[A] alloc 100B/64B/1000B -> unit %u / %u / %u\n",
               q ? unit_off(b, q) : 9999u, r ? unit_off(b, r) : 9999u, s ? unit_off(b, s) : 9999u);
        print_counts("[A] after 4     nfree[0..10] =", b);
        CHECK(q == &b->mem[2] && r == &b->mem[1] && s == &b->mem[16] && buddy_check(b));
        /* Part B: 위 4개를 풀면 order 10 블록 하나로 돌아온다 */
        bool fr = buddy_free(b, q) && buddy_check(b) && buddy_free(b, p) && buddy_check(b)
               && buddy_free(b, s) && buddy_check(b) && buddy_free(b, r) && buddy_check(b);
        CHECK(fr && only_max_block(b));
    }
    /* Part A: 64 KB 통째로 / 64 B 1024 개로 꽉 채우기 */
    {
        buddy_init(b);
        void *p = buddy_alloc(b, ARENA_SIZE);
        CHECK(p == &b->mem[0] && b->avail == 0 && buddy_alloc(b, 1) == NULL && buddy_check(b));
        CHECK(buddy_free(b, p) && only_max_block(b));
        bool ok = true;
        for (uint32_t i = 0; i < NUNITS && ok; i++) {
            void *q = buddy_alloc(b, 1);
            ok = q != NULL && !bm_test(b->freemap, node_of(0, unit_off(b, q)));
        }
        CHECK(ok && buddy_alloc(b, 1) == NULL && b->avail == 0 && buddy_check(b));
    }

    /* Part B: 병합 순서 추적 — 64 B 4개 (unit 0..3) 를 1, 2, 0, 3 순서로 해제 */
    {
        buddy_init(b);
        void *u[4];
        for (int i = 0; i < 4; i++) u[i] = buddy_alloc_order(b, 0);
        CHECK(u[0] == &b->mem[0] && u[3] == &b->mem[3]);
        static const int order_[4] = { 1, 2, 0, 3 };
        bool ok = true;
        for (int i = 0; i < 4; i++) {
            uint32_t before = g_listops;
            ok = ok && buddy_free(b, u[order_[i]]) && buddy_check(b);
            printf("[B] free unit %d: list ops %u, nfree[0..3] = %u %u %u %u, top order %u\n", order_[i],
                   g_listops - before, b->nfree[0], b->nfree[1], b->nfree[2], b->nfree[3],
                   b->avail ? 31u - (uint32_t)__builtin_clz(b->avail) : 0u);
        }
        CHECK(ok && only_max_block(b));
    }
    /* Part B: 잘못된 해제는 거절하고 상태를 바꾸지 않는다 */
    {
        buddy_init(b);
        void *p = buddy_alloc_order(b, 1);                     /* unit 0..1 */
        void *q = buddy_alloc_order(b, 0);                     /* unit 2 */
        int local = 0;
        uint32_t snap[MAX_ORDER + 1];
        memcpy(snap, b->nfree, sizeof snap);
        CHECK(!buddy_free(b, NULL) && !buddy_free(b, &local) && !buddy_free(b, (uint8_t *)p + 32));
        CHECK(!buddy_free(b, &b->mem[1]));                     /* order 1 블록의 내부 (unit 1) */
        CHECK(!buddy_free(b, &b->mem[3]) && memcmp(snap, b->nfree, sizeof snap) == 0 && buddy_check(b));
        CHECK(buddy_free(b, q) && !buddy_free(b, q) && buddy_check(b));   /* 이중 해제 */
        CHECK(buddy_free(b, p) && only_max_block(b));
    }
    /* Part B: 랜덤 alloc/free 30000 회 — 참조 모델 대조 + 블록 내용 보존 + 매번 불변식 */
    {
        buddy_init(b);
        memset(g_owner, 0xFF, sizeof g_owner);
        uint32_t nlive = 0, nalloc = 0, nfail = 0;
        bool ok = true;
        rng_state = 0xB0DD1u;
        for (int op = 0; op < 30000 && ok; op++) {
            uint32_t r = xorshift32();
            if (nlive == 0 || (r % 100u) < 55u) {
                uint32_t pick = (r >> 8) % 100u;               /* 작은 블록 위주 + 가끔 큰 블록 */
                uint32_t k = pick < 60u ? pick % 3u : pick < 90u ? 3u + pick % 3u : 6u + pick % 5u;
                bool can = ref_can_alloc(k);
                void *p = buddy_alloc_order(b, k);
                ok = (p != NULL) == can;
                if (p) {
                    uint32_t u0 = unit_off(b, p);
                    ok = ok && (u0 & ((1u << k) - 1u)) == 0;   /* 자기 크기로 정렬 */
                    for (uint32_t x = u0; x < u0 + (1u << k); x++) {   /* 참조 모델: 겹치면 안 된다 */
                        ok = ok && g_owner[x] < 0;
                        g_owner[x] = (int16_t)nlive;
                    }
                    live_t *l = &g_live[nlive++];
                    l->p = p; l->order = k; l->tag = (uint8_t)(r >> 24);
                    memset(p, l->tag, 1u << (k + MIN_SHIFT));
                    nalloc++;
                } else nfail++;
            } else {
                uint32_t i = (r >> 8) % nlive;
                live_t *l = &g_live[i];
                uint32_t u0 = unit_off(b, l->p);
                ok = tag_ok(l) && buddy_free(b, l->p);
                for (uint32_t x = u0; x < u0 + (1u << l->order); x++) g_owner[x] = -1;
                *l = g_live[--nlive];                          /* 마지막 원소로 메움 → 소유자 번호 갱신 */
                if (i < nlive) {
                    uint32_t v0 = unit_off(b, l->p);
                    for (uint32_t x = v0; x < v0 + (1u << l->order); x++) g_owner[x] = (int16_t)i;
                }
            }
            ok = ok && buddy_check(b);
        }
        printf("[B] random 30000 ops: %s (allocs %u, refused %u, live at end %u)\n",
               ok ? "match" : "MISMATCH", nalloc, nfail, nlive);
        CHECK(ok && nfail > 0);
        /* 남은 블록을 무작위 순서로 전부 해제 → order 10 블록 하나로 완전 복원 */
        while (ok && nlive) {
            uint32_t i = xorshift32() % nlive;
            ok = tag_ok(&g_live[i]) && buddy_free(b, g_live[i].p) && buddy_check(b);
            g_live[i] = g_live[--nlive];
        }
        CHECK(ok && only_max_block(b));
    }

    /* Part C: 패킷 버퍼 + DMA 프레임 워크로드 — 단편화 지표 */
    {
        buddy_init(b);
        memset(g_owner, 0xFF, sizeof g_owner);
        uint32_t nlive = 0, fails = 0, frag_fails = 0, max_alloc_ops = 0, max_free_ops = 0;
        bool ok = true;
        bstats_t st;
        rng_state = 20261107u;
        printf("[C]  step | live | used B | req B | int.frag | free B | largest | ext.frag | fail (free>=blk)\n");
        for (uint32_t step = 0; step < 20000 && ok; step++) {
            for (uint32_t i = 0; i < nlive;) {                 /* 수명이 다한 버퍼 반납 */
                if (g_live[i].expire != step) { i++; continue; }
                uint32_t u0 = unit_off(b, g_live[i].p), before = g_listops;
                ok = ok && buddy_free(b, g_live[i].p);
                if (g_listops - before > max_free_ops) max_free_ops = g_listops - before;
                for (uint32_t x = u0; x < u0 + (1u << g_live[i].order); x++) g_owner[x] = -1;
                g_live[i] = g_live[--nlive];
            }
            uint32_t r = xorshift32();
            uint32_t req, life;
            if (r % 1000u < 6u) { req = 4096u + (r >> 10) % 12289u; life = 100u + (r >> 20) % 300u; } /* DMA 4~16 KB */
            else                { req = 64u + (r >> 10) % 1455u;   life = 1u + (r >> 20) % 48u;   }  /* 패킷 64~1518 B */
            uint32_t before = g_listops;
            void *p = buddy_alloc(b, req);
            if (g_listops - before > max_alloc_ops) max_alloc_ops = g_listops - before;
            if (p) {
                live_t *l = &g_live[nlive++];
                l->p = p; l->order = (uint32_t)size_to_order(req); l->req = req; l->expire = step + life;
                uint32_t u0 = unit_off(b, p);
                for (uint32_t x = u0; x < u0 + (1u << l->order); x++) g_owner[x] = 0;
            } else {
                buddy_stats(b, &st);
                fails++;
                if (st.free_bytes >= (1u << (size_to_order(req) + (int)MIN_SHIFT))) frag_fails++;   /* 총량은 충분한데 연속 블록이 없음 */
            }
            if ((step + 1u) % 2500u == 0) {
                uint32_t used = 0, reqsum = 0;
                for (uint32_t i = 0; i < nlive; i++) { used += 1u << (g_live[i].order + MIN_SHIFT); reqsum += g_live[i].req; }
                buddy_stats(b, &st);
                ok = ok && buddy_check(b) && st.free_bytes + used == ARENA_SIZE && st.largest_free == ref_largest_free()
                        && st.frag >= 0.0 && st.frag < 1.0;
                printf("[C] %5u | %4u | %6u | %5u |   %5.1f%% | %6u | %7u |   %5.1f%% | %u (%u)\n", step + 1u, nlive, used,
                       reqsum, used ? 100.0 * (1.0 - (double)reqsum / used) : 0.0, st.free_bytes, st.largest_free,
                       100.0 * st.frag, fails, frag_fails);
            }
        }
        CHECK(ok && frag_fails > 0);
        printf("[C] worst list ops per call: alloc %u, free %u (bound %u)\n", max_alloc_ops, max_free_ops, MAX_ORDER + 1u);
        CHECK(max_alloc_ops <= MAX_ORDER + 1u && max_free_ops <= MAX_ORDER + 1u);
        while (nlive) ok = ok && buddy_free(b, g_live[--nlive].p);
        buddy_stats(b, &st);
        CHECK(ok && only_max_block(b) && st.free_bytes == ARENA_SIZE && st.largest_free == ARENA_SIZE
              && st.frag == 0.0 && st.nblocks == 1);
    }
    /* Part C: 체커보드 — 절반이 비어 있어도 128 B 를 못 준다 */
    {
        static void *cell[NUNITS];
        bstats_t st;
        buddy_init(b);
        for (uint32_t i = 0; i < NUNITS; i++) cell[i] = buddy_alloc_order(b, 0);
        bool ok = true;
        for (uint32_t i = 0; i < NUNITS; i += 2) ok = ok && buddy_free(b, cell[i]);
        buddy_stats(b, &st);
        printf("[C] checkerboard: free %u B in %u blocks, largest %u B, ext.frag %.6f, alloc(128) %s\n",
               st.free_bytes, st.nblocks, st.largest_free, st.frag, buddy_alloc(b, 128) ? "ok" : "NULL");
        CHECK(ok && st.free_bytes == ARENA_SIZE / 2u && st.largest_free == 64u && st.nblocks == 512u
              && st.frag == 1.0 - 64.0 / 32768.0 && buddy_alloc(b, 128) == NULL && buddy_check(b));
        for (uint32_t i = 1; i < NUNITS; i += 2) ok = ok && buddy_free(b, cell[i]);
        buddy_stats(b, &st);
        CHECK(ok && only_max_block(b) && st.frag == 0.0 && buddy_check(b));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
