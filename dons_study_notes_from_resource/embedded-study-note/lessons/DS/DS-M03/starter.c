/* DS-M03 간소화 슬랩 할당기
 * 정적 아레나(4KB 정렬) → 4KB 슬랩 64개. 크기 클래스 16/32/64/128B.
 * 슬랩 = [64B 헤더 | 객체 x nobj | 남는 꼬리]. free 객체 리스트는 객체 안에 내장(첫 8바이트 = next).
 * 캐시(클래스)마다 partial / full / empty 리스트, 아무 캐시에도 없는 슬랩은 전역 풀.
 * 할당기 안에서는 malloc 을 쓰지 않는다. 포인터 → 슬랩은 주소 마스킹 (p & ~0xFFF).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SLAB_SHIFT  12u
#define SLAB_SIZE   (1u << SLAB_SHIFT)                 /* 4096 */
#define SLAB_MASK   ((uintptr_t)SLAB_SIZE - 1u)
#define NSLABS      64u                                /* 아레나 256KB */
#define SLAB_HDR    64u                                /* 객체는 슬랩 오프셋 64부터 */
#define NCLASS      4u                                 /* 16, 32, 64, 128 */
#define MIN_SHIFT   4u                                 /* 가장 작은 클래스 = 1 << 4 */
#define MAX_SMALL   128u
#define EMPTY_KEEP  1u                                 /* 캐시마다 남겨 둘 빈 슬랩 수 */
#define SLAB_MAGIC  0x51AB0000u                        /* | cls */
#define FREE_COOKIE 0xF4EEF4EEDEADBEEFull              /* free 객체의 두 번째 워드 */

enum { ST_POOL = 0, ST_PARTIAL, ST_FULL, ST_EMPTY };  /* 슬랩이 어느 리스트에 있나 */
enum { SF_OK = 0, SF_BAD_PTR, SF_DOUBLE };             /* slab_free 결과 */

typedef struct slab {
    struct slab *prev, *next;      /* 소속 리스트 (원형 + sentinel, DS-B06) */
    uint8_t     *free;             /* 이 슬랩의 free 객체 리스트 머리 */
    uint32_t     magic;            /* SLAB_MAGIC | cls, 풀에 있으면 0 */
    uint16_t     inuse;            /* 할당된 객체 수 */
    uint16_t     nobj;             /* 슬랩당 객체 수 */
    uint8_t      cls;              /* 크기 클래스 0..3 */
    uint8_t      state;            /* ST_* */
} slab_t;
_Static_assert(sizeof(slab_t) <= SLAB_HDR, "slab header must fit in SLAB_HDR");

typedef union {                    /* 슬랩 한 장: 앞 64B 는 헤더, 나머지는 객체 */
    slab_t  hdr;
    uint8_t bytes[SLAB_SIZE];
} slab_page_t;
_Static_assert(sizeof(slab_page_t) == SLAB_SIZE, "one slab = one 4KB page");

typedef struct {
    slab_t   partial, full, empty; /* sentinel 헤드 (prev/next 만 사용) */
    uint32_t size, nobj;
    uint32_t n_partial, n_full, n_empty;
    uint32_t live;                 /* 할당된 객체 수 */
    uint8_t  cls;
} kcache_t;

static _Alignas(SLAB_SIZE) slab_page_t g_arena[NSLABS];   /* 정적 아레나 — 시작 주소가 4KB 배수 */
static slab_t   g_pool;            /* 아무 캐시에도 속하지 않은 슬랩 */
static uint32_t g_pool_n;
static kcache_t g_cache[NCLASS];

/* ---------------- 주어진 도구: 리스트 (DS-B06) · 객체 워드 · 풀 ---------------- */
static void l_init(slab_t *h) { h->prev = h->next = h; }
static bool l_empty(const slab_t *h) { return h->next == h; }
static void l_add(slab_t *h, slab_t *s) { s->next = h->next; s->prev = h; h->next->prev = s; h->next = s; }
static void l_del(slab_t *s) { s->prev->next = s->next; s->next->prev = s->prev; s->prev = s->next = NULL; }

/* free 객체 안의 워드 읽기/쓰기 — memcpy 로 해서 strict aliasing 문제를 피한다 (-O2 에서 load/store 1개) */
static uint8_t *obj_next(const uint8_t *o) { uint8_t *n; memcpy(&n, o, sizeof n); return n; }
static void obj_set_next(uint8_t *o, uint8_t *n) { memcpy(o, &n, sizeof n); }
static uint64_t obj_cookie(const uint8_t *o) { uint64_t c; memcpy(&c, o + 8, sizeof c); return c; }
static void obj_set_cookie(uint8_t *o, uint64_t c) { memcpy(o + 8, &c, sizeof c); }

static uint8_t *slab_obj0(slab_t *s) { return (uint8_t *)s + SLAB_HDR; }

static slab_t *pool_take(void) {
    if (l_empty(&g_pool)) return NULL;
    slab_t *s = g_pool.next;
    l_del(s);
    g_pool_n--;
    return s;
}
static void pool_put(slab_t *s) {
    s->state = ST_POOL; s->magic = 0; s->inuse = 0; s->free = NULL;
    l_add(&g_pool, s);
    g_pool_n++;
}

/* 슬랩을 캐시의 다른 리스트로 옮긴다 (카운터 포함). 풀에서 막 꺼낸 슬랩(ST_POOL)도 받는다 */
static void slab_move(kcache_t *c, slab_t *s, uint8_t to) {
    if (s->state == ST_PARTIAL) c->n_partial--;
    else if (s->state == ST_FULL) c->n_full--;
    else if (s->state == ST_EMPTY) c->n_empty--;
    if (s->state != ST_POOL) l_del(s);
    slab_t *h = to == ST_PARTIAL ? &c->partial : to == ST_FULL ? &c->full : &c->empty;
    l_add(h, s);
    s->state = to;
    if (to == ST_PARTIAL) c->n_partial++;
    else if (to == ST_FULL) c->n_full++;
    else c->n_empty++;
}
/* 캐시에 있던 슬랩을 전역 풀로 돌려준다 */
static void slab_to_pool(kcache_t *c, slab_t *s) {
    if (s->state == ST_PARTIAL) c->n_partial--;
    else if (s->state == ST_FULL) c->n_full--;
    else if (s->state == ST_EMPTY) c->n_empty--;
    l_del(s);
    pool_put(s);
}

/* 슬랩의 free 리스트에 p 가 있나 — O(nobj). 이중 해제 확인용 느린 경로 */
static bool freelist_contains(slab_t *s, const uint8_t *p) {
    uint32_t k = 0;
    for (const uint8_t *o = s->free; o && k <= s->nobj; o = obj_next(o), k++)
        if (o == p) return true;
    return false;
}

static void slab_sys_init(void) {
    l_init(&g_pool);
    g_pool_n = 0;
    for (uint32_t i = NSLABS; i-- > 0;) pool_put(&g_arena[i].hdr);        /* 슬랩 0 이 맨 앞 */
    for (uint32_t c = 0; c < NCLASS; c++) {
        kcache_t *k = &g_cache[c];
        l_init(&k->partial); l_init(&k->full); l_init(&k->empty);
        k->size = 1u << (MIN_SHIFT + c);
        k->nobj = (SLAB_SIZE - SLAB_HDR) / k->size;                          /* 252, 126, 63, 31 */
        k->n_partial = k->n_full = k->n_empty = k->live = 0;
        k->cls = (uint8_t)c;
    }
}

/* ---------------- Part A: 크기 클래스 · 주소 마스킹 · 슬랩 하나 ---------------- */
/* 요청 크기 → 클래스: 1..16→0, 17..32→1, 33..64→2, 65..128→3. 0 이나 128 초과는 -1 */
static int size_to_class(size_t n) {
    (void)n;
    /* TODO: 0 이나 MAX_SMALL 초과면 -1. 아니면 (1 << (MIN_SHIFT + c)) >= n 인 가장 작은 c */
    return -1;
}

/* 객체 포인터 → 그 객체를 담은 슬랩의 시작(= 헤더). 슬랩이 4KB 정렬이라 하위 12비트만 지우면 된다 */
static slab_t *slab_of(const void *p) {
    (void)p;
    /* TODO: 하위 SLAB_SHIFT 비트를 지운 주소 (uintptr_t 로 마스킹) */
    return NULL;
}

/* 빈 슬랩을 클래스 cls 용으로 포맷: 헤더 채우고, 모든 객체를 주소 순서로 free 리스트에 잇고 쿠키 표시 */
static void slab_format(slab_t *s, uint8_t cls) {
    (void)s; (void)cls; (void)obj_set_next; (void)obj_set_cookie;
    /* TODO: magic/cls/inuse/nobj 설정. 객체 i(주소 slab_obj0(s) + i*size)를 i+1 에 잇고(마지막은 NULL),
     *       모든 객체에 FREE_COOKIE. s->free = 첫 객체 */
}

/* 슬랩 free 리스트에서 객체 하나 꺼내기 (없으면 NULL) */
static void *slab_obj_alloc(slab_t *s) {
    (void)s; (void)obj_next;
    /* TODO: free 머리를 꺼내 free = next, 쿠키 지우기(0), inuse++. 비었으면 NULL */
    return NULL;
}

/* 객체를 슬랩 free 리스트 머리에 되돌리기 (LIFO — 방금 해제된 객체가 캐시에 따뜻하다) */
static void slab_obj_free(slab_t *s, void *p) {
    (void)s; (void)p;
    /* TODO: p 를 free 머리에 push (next = 옛 머리), 쿠키 표시, inuse-- */
}

/* ---------------- Part B: 캐시 — partial/full/empty 와 해제 검증 ---------------- */
/* p 가 "살아 있는 슬랩의 객체 시작 주소"면 그 슬랩, 아니면 NULL */
static slab_t *slab_lookup(const void *p) {
    (void)p; (void)slab_obj0;
    /* TODO: 아레나 밖 → NULL. s = slab_of(p). 풀 슬랩/magic 불일치 → NULL.
     *       헤더 영역, 객체 경계가 아님(off % size), 꼬리(off / size >= nobj) → NULL */
    return NULL;
}

static void *kc_alloc(kcache_t *c) {
    (void)c; (void)pool_take; (void)slab_move;
    /* TODO: partial 머리 → 없으면 empty 를 partial 로 → 없으면 pool_take + slab_format (없으면 NULL).
     *       객체 하나 꺼내고 live++, 슬랩이 꽉 차면 full 로 */
    return NULL;
}

/* 어떤 클래스의 객체든 해제 (크기 인자 없음 — 마스킹으로 슬랩 헤더에서 클래스를 안다) */
static int slab_free(void *p) {
    (void)p; (void)freelist_contains; (void)slab_to_pool;
    /* TODO: NULL → SF_OK. slab_lookup 실패 → SF_BAD_PTR. 쿠키 + freelist_contains → SF_DOUBLE.
     *       객체 반납, live--. inuse == 0 이면 empty(EMPTY_KEEP 개까지) 또는 풀로, full 이었으면 partial 로 */
    return SF_BAD_PTR;
}

/* 모든 캐시의 빈 슬랩을 전역 풀로 반납 (kmem_cache_shrink). 반납한 슬랩 수 */
static uint32_t slab_reap(void) {
    /* TODO: 모든 캐시의 empty 리스트 슬랩을 slab_to_pool 로 반납하고 개수를 돌려준다 */
    return 0;
}

/* ---------------- Part C: kmalloc 스타일 앞단 ---------------- */
static void *slab_malloc(size_t n) {
    (void)n;
    /* TODO: size_to_class → 해당 캐시에서 kc_alloc. 클래스가 없으면 NULL */
    return NULL;
}

/* 할당된 객체의 실제 사용 가능 크기 (malloc_usable_size). 우리 객체가 아니면 0 */
static size_t slab_usable_size(const void *p) {
    (void)p;
    /* TODO: slab_lookup 으로 우리 객체인지 확인하고 그 클래스 크기, 아니면 0 */
    return 0;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
static bool check_list(kcache_t *c, slab_t *h, uint8_t st, uint32_t want, uint32_t *live, uint32_t *nslab) {
    uint32_t n = 0;
    for (slab_t *s = h->next; s != h; s = s->next) {
        if (++n > NSLABS) return false;                                     /* 고리 깨짐 */
        if (s->next->prev != s || s->prev->next != s) return false;
        if (((uintptr_t)s & SLAB_MASK) != 0) return false;
        if (s->state != st || s->cls != c->cls || s->magic != (SLAB_MAGIC | c->cls) || s->nobj != c->nobj)
            return false;
        if (st == ST_PARTIAL && !(s->inuse > 0 && s->inuse < s->nobj)) return false;
        if (st == ST_FULL && s->inuse != s->nobj) return false;
        if (st == ST_EMPTY && s->inuse != 0) return false;
        uint32_t f = 0;                       /* free 리스트: 길이, 객체 경계, 쿠키 */
        for (uint8_t *o = s->free; o; o = obj_next(o)) {
            if (++f > s->nobj || slab_of(o) != s) return false;
            if ((uintptr_t)o < (uintptr_t)slab_obj0(s)) return false;
            uintptr_t off = (uintptr_t)o - (uintptr_t)slab_obj0(s);
            if (off % c->size != 0 || off / c->size >= c->nobj) return false;
            if (obj_cookie(o) != FREE_COOKIE) return false;
        }
        if (f != (uint32_t)(s->nobj - s->inuse)) return false;
        *live += s->inuse;
    }
    *nslab += n;
    return n == want;
}

/* 전체 불변식: 모든 슬랩은 풀 또는 정확히 한 캐시 리스트에, 상태·카운터·free 리스트가 일치 */
static bool slab_check(void) {
    uint32_t nslab = 0;
    for (slab_t *s = g_pool.next; s != &g_pool; s = s->next)
        if (++nslab > NSLABS || s->state != ST_POOL || s->next->prev != s) return false;
    if (nslab != g_pool_n) return false;
    for (uint32_t i = 0; i < NCLASS; i++) {
        kcache_t *c = &g_cache[i];
        uint32_t live = 0;
        if (!check_list(c, &c->partial, ST_PARTIAL, c->n_partial, &live, &nslab)) return false;
        if (!check_list(c, &c->full, ST_FULL, c->n_full, &live, &nslab)) return false;
        if (!check_list(c, &c->empty, ST_EMPTY, c->n_empty, &live, &nslab)) return false;
        if (live != c->live || c->n_empty > EMPTY_KEEP) return false;
    }
    return nslab == NSLABS;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static bool in_arena(const void *p) {
    uintptr_t a = (uintptr_t)p, base = (uintptr_t)g_arena;
    return a >= base && a < base + sizeof g_arena;
}
static bool bytes_are(const uint8_t *p, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++) if (p[i] != v) return false;
    return true;
}
/* kc_alloc 이 NULL 을 돌려도 되는 유일한 경우: 이 캐시에 남는 객체도, 풀에 슬랩도 없을 때 */
static bool may_fail(const kcache_t *c) { return g_pool_n == 0 && c->n_partial == 0 && c->n_empty == 0; }

typedef struct { uint8_t *p; uint16_t n; uint8_t pat; uint8_t kind; uint32_t seq; } live_t;
static live_t g_live[8192];

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    slab_sys_init();
    CHECK(((uintptr_t)g_arena & SLAB_MASK) == 0 && slab_check());

    /* Part A: 크기 클래스 경계 */
    {
        static const size_t n[] = { 1, 16, 17, 32, 33, 64, 65, 128, 0, 129, (size_t)-1 };
        static const int want[] = { 0, 0, 1, 1, 2, 2, 3, 3, -1, -1, -1 };
        bool ok = true;
        for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++) ok = ok && size_to_class(n[i]) == want[i];
        CHECK(ok);
    }
    /* Part A: 슬랩 하나 — 64B 클래스(63개) 포맷, 전부 할당, 마스킹, 해제 후 LIFO 재사용 */
    {
        slab_t *s = pool_take();
        slab_format(s, 2);
        CHECK(s->nobj == 63 && s->inuse == 0 && s->free == slab_obj0(s));
        uint8_t *p[63];
        bool ok = true;
        for (int i = 0; i < 63 && ok; i++) {
            p[i] = slab_obj_alloc(s);
            ok = p[i] != NULL && p[i] == slab_obj0(s) + 64 * i                  /* 주소 순서 */
                 && ((uintptr_t)p[i] & 63u) == 0
                 && slab_of(p[i]) == s && slab_of(p[i] + 63) == s;             /* 객체 끝도 같은 슬랩 */
        }
        CHECK(ok && s->inuse == 63);
        CHECK(slab_obj_alloc(s) == NULL && s->inuse == 63);                    /* 꼬리 4096-64-63*64 = 0 */
        if (ok) {
            slab_obj_free(s, p[10]); slab_obj_free(s, p[40]); slab_obj_free(s, p[0]);
            CHECK(s->inuse == 60 && obj_cookie(p[40]) == FREE_COOKIE);
            CHECK(slab_obj_alloc(s) == p[0] && slab_obj_alloc(s) == p[40] && slab_obj_alloc(s) == p[10]);
        } else {
            CHECK(false); CHECK(false);
        }
        CHECK(slab_of(&g_arena[7].bytes[4095]) == &g_arena[7].hdr && slab_of(&g_arena[8]) == &g_arena[8].hdr);
        pool_put(s);
        CHECK(slab_check());
    }

    /* Part B: 상태 전이 empty → partial → full → partial → empty → pool */
    {
        slab_sys_init();
        kcache_t *c = &g_cache[0];                                             /* 16B, 252개/슬랩 */
        static uint8_t *p[600];
        bool ok = true;
        p[0] = kc_alloc(c);
        CHECK(p[0] != NULL && c->n_partial == 1 && g_pool_n == NSLABS - 1 && slab_check());
        for (int i = 1; i < 252 && ok; i++) ok = (p[i] = kc_alloc(c)) != NULL;
        CHECK(ok && c->n_full == 1 && c->n_partial == 0 && slab_check());
        p[252] = kc_alloc(c);                                                  /* 두 번째 슬랩 */
        CHECK(p[252] != NULL && slab_of(p[252]) != slab_of(p[0]) && c->n_full == 1 && c->n_partial == 1);
        CHECK(slab_free(p[5]) == SF_OK && c->n_full == 0 && c->n_partial == 2 && slab_check());
        CHECK(slab_free(p[252]) == SF_OK && c->n_empty == 1 && c->n_partial == 1 && slab_check());
        p[5] = kc_alloc(c);                                                    /* partial 이 empty 보다 먼저 */
        CHECK(p[5] != NULL && slab_of(p[5]) == slab_of(p[0]) && c->n_empty == 1 && c->n_full == 1);
        for (int i = 0; i < 252 && ok; i++) ok = slab_free(p[i]) == SF_OK;
        CHECK(ok && c->live == 0 && c->n_empty == EMPTY_KEEP && g_pool_n == NSLABS - EMPTY_KEEP && slab_check());
        CHECK(slab_reap() == 1 && g_pool_n == NSLABS && c->n_empty == 0 && slab_check());
    }
    /* Part B: 잘못된 해제 — 아레나 밖, 객체 중간, 헤더, 꼬리, 풀 슬랩, 이중 해제, 쿠키 오탐 */
    {
        slab_sys_init();
        uint8_t local[32];
        uint8_t *a = kc_alloc(&g_cache[3]);                                    /* 128B, 31개/슬랩 */
        uint8_t *b = kc_alloc(&g_cache[3]);
        CHECK(a != NULL && b != NULL);
        if (a && b) {
            slab_t *s = slab_of(a);
            CHECK(slab_free(NULL) == SF_OK);
            CHECK(slab_free(local) == SF_BAD_PTR);
            CHECK(slab_free(a + 1) == SF_BAD_PTR && slab_free(a + 64) == SF_BAD_PTR);
            CHECK(slab_free((uint8_t *)s + 8) == SF_BAD_PTR);                   /* 헤더 안 */
            CHECK(slab_free((uint8_t *)s + SLAB_HDR + 31 * 128) == SF_BAD_PTR);  /* 꼬리 (4032) */
            CHECK(slab_free(g_arena[NSLABS - 1].bytes + SLAB_HDR) == SF_BAD_PTR); /* 풀에 있는 슬랩 */
            CHECK(slab_free(a) == SF_OK);
            CHECK(slab_free(a) == SF_DOUBLE && g_cache[3].live == 1 && slab_check());
            obj_set_cookie(b, FREE_COOKIE);                 /* 사용자 데이터가 우연히 쿠키와 같아도 */
            CHECK(slab_free(b) == SF_OK && g_cache[3].live == 0 && slab_check());   /* 느린 확인으로 통과 */
            CHECK(slab_free(b) == SF_DOUBLE);
        } else {
            for (int i = 0; i < 10; i++) CHECK(false);
        }
    }
    /* Part B: 소진 → 다른 클래스로 회수 (empty 정책 + reap) */
    {
        slab_sys_init();
        static uint8_t *p[NSLABS * 31];
        uint32_t n = 0;
        while (n < NSLABS * 31 && (p[n] = kc_alloc(&g_cache[3])) != NULL) n++;
        CHECK(n == NSLABS * 31 && kc_alloc(&g_cache[3]) == NULL && g_pool_n == 0);
        CHECK(kc_alloc(&g_cache[0]) == NULL && slab_check());                 /* 다른 클래스도 슬랩 없음 */
        bool ok = true;
        for (uint32_t i = 0; i < n; i++) ok = ok && slab_free(p[i]) == SF_OK;
        CHECK(ok && g_cache[3].n_empty == EMPTY_KEEP && g_pool_n == NSLABS - EMPTY_KEEP);
        uint32_t m = 0;
        while (kc_alloc(&g_cache[0]) != NULL) m++;
        CHECK(m == (NSLABS - EMPTY_KEEP) * 252 && slab_check());               /* 128B 쪽 빈 슬랩 1개는 묶여 있음 */
        CHECK(slab_reap() == 1 && kc_alloc(&g_cache[0]) != NULL && g_pool_n == 0 && slab_check());
    }
    /* Part B: 랜덤 alloc/free 60000회 vs 참조 모델 (내용 패턴으로 겹침 검출) */
    {
        slab_sys_init();
        uint32_t nlive = 0, nulls = 0, dbl = 0;
        bool ok = true;
        rng_state = 0x5EED5AB1u;
        for (uint32_t op = 0; op < 60000 && ok; op++) {
            uint32_t r = xorshift32();
            if (nlive < 4000 && (r & 7u) < 5) {
                kcache_t *c = &g_cache[(r >> 3) & 3u];
                bool can_fail = may_fail(c);
                uint8_t *p = kc_alloc(c);
                if (p == NULL) { ok = can_fail; nulls++; }
                else {
                    ok = in_arena(p) && slab_lookup(p) == slab_of(p) && slab_usable_size(p) == c->size;
                    uint8_t pat = (uint8_t)(r >> 8);
                    memset(p, pat, c->size);
                    g_live[nlive++] = (live_t){ p, (uint16_t)c->size, pat, 0, 0 };
                }
            } else if (nlive > 0) {
                uint32_t i = (r >> 3) % nlive;
                live_t L = g_live[i];
                g_live[i] = g_live[--nlive];
                ok = bytes_are(L.p, L.n, L.pat) && slab_free(L.p) == SF_OK;   /* 누가 덮어썼으면 패턴이 깨짐 */
                if ((r >> 24) == 0) { ok = ok && slab_free(L.p) == SF_DOUBLE; dbl++; }
            }
            if (op < 4000 || op % 32 == 0) ok = ok && slab_check();
        }
        uint32_t sum = 0;
        for (uint32_t i = 0; i < NCLASS; i++) sum += g_cache[i].live;
        ok = ok && sum == nlive && slab_check();
        for (uint32_t i = 0; i < nlive && ok; i++) ok = bytes_are(g_live[i].p, g_live[i].n, g_live[i].pat) && slab_free(g_live[i].p) == SF_OK;
        slab_reap();
        CHECK(ok && g_pool_n == NSLABS && slab_check());
        printf("[B] random 60000 ops vs model: %s (NULL at exhaustion %u, double-free caught %u)\n",
               ok ? "match" : "MISMATCH", nulls, dbl);
    }

    /* Part C: kmalloc 앞단 — 경계 */
    {
        slab_sys_init();
        uint8_t local[16];
        uint8_t *p = slab_malloc(17), *q = slab_malloc(128);
        CHECK(slab_malloc(0) == NULL && slab_malloc(129) == NULL);
        CHECK(p != NULL && q != NULL && slab_usable_size(p) == 32 && slab_usable_size(q) == 128);
        CHECK(slab_usable_size(NULL) == 0 && slab_usable_size(local) == 0 && slab_usable_size(p ? p + 8 : NULL) == 0);
        CHECK(slab_free(p) == SF_OK && slab_free(q) == SF_OK && slab_usable_size(p) == 32);  /* free 객체도 경계는 유효 */
    }
    /* Part C: 센서 허브 메시지 파이프라인 (FIFO 큐) + 내부 단편화 측정 */
    {
        static const struct { const char *name; uint16_t size; uint8_t weight; } kind[] = {
            { "BLE notify", 20, 20 }, { "IMU sample", 24, 40 }, { "log record", 48, 15 },
            { "CAN-FD frame", 72, 15 }, { "NMEA sentence", 82, 10 },
        };
        enum { NKIND = 5, QCAP = 1500 };
        uint32_t head = 0, tail = 0, seq = 0, cnt[NKIND] = { 0 }, nulls = 0;
        uint64_t req = 0, got = 0;
        uint32_t peak_slabs = 0, peak_req_live = 0, req_live = 0;
        bool ok = true;
        slab_sys_init();
        rng_state = 20261107u;
        for (uint32_t op = 0; op < 100000 && ok; op++) {
            uint32_t r = xorshift32();
            bool produce = (tail - head) < QCAP && ((r & 15u) < 9 || tail == head);  /* 9:7 → 큐가 점점 찬다 */
            if (produce) {
                uint32_t w = (r >> 4) % 100u, k = 0;
                while (w >= kind[k].weight) { w -= kind[k].weight; k++; }
                int ci = size_to_class(kind[k].size);
                kcache_t *c = &g_cache[ci < 0 ? 0 : ci];
                bool can_fail = may_fail(c);
                uint8_t *p = slab_malloc(kind[k].size);
                if (p == NULL) { ok = can_fail; nulls++; continue; }
                size_t us = slab_usable_size(p);
                ok = us >= kind[k].size && us < 2u * kind[k].size + 16u;
                uint8_t pat = (uint8_t)(seq * 31u + k);
                memset(p, pat, kind[k].size);
                g_live[tail % QCAP] = (live_t){ p, kind[k].size, pat, (uint8_t)k, seq++ };
                tail++;
                cnt[k]++; req += kind[k].size; got += us; req_live += kind[k].size;
                uint32_t used = NSLABS - g_pool_n;
                if (used > peak_slabs) { peak_slabs = used; peak_req_live = req_live; }
            } else {
                live_t L = g_live[head % QCAP];
                head++;
                req_live -= L.n;
                ok = bytes_are(L.p, L.n, L.pat) && slab_free(L.p) == SF_OK;
            }
            if (op % 64 == 0) ok = ok && slab_check();
        }
        while (ok && head != tail) { live_t L = g_live[head++ % QCAP]; ok = bytes_are(L.p, L.n, L.pat) && slab_free(L.p) == SF_OK; }
        slab_reap();
        CHECK(ok && nulls == 0 && g_pool_n == NSLABS && slab_check());
        uint32_t total = 0;
        for (int k = 0; k < NKIND; k++) total += cnt[k];
        printf("[C] kind           size  class  count\n");
        for (int k = 0; k < NKIND; k++) {
            int ci = size_to_class(kind[k].size);
            printf("[C] %-13s  %4u  %5u  %5u\n", kind[k].name, kind[k].size, ci < 0 ? 0u : 16u << ci, cnt[k]);
        }
        double frag = got ? 100.0 * (double)(got - req) / (double)got : 0.0;
        double frag128 = total ? 100.0 * (double)(128ull * total - req) / (128.0 * total) : 0.0;
        printf("[C] internal frag: size classes %.1f%% | single 128B pool %.1f%% (allocs %u)\n", frag, frag128, total);
        printf("[C] peak: %u slabs (%u B) holding %u B requested -> utilization %.1f%%\n",
               peak_slabs, peak_slabs * SLAB_SIZE, peak_req_live, peak_slabs ? 100.0 * peak_req_live / (peak_slabs * (double)SLAB_SIZE) : 0.0);
        CHECK(total > 40000 && frag > 0.0 && frag < 50.0 && frag < frag128);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
