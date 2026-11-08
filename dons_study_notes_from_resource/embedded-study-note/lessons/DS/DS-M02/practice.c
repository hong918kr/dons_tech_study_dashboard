/* DS-M02 추가 연습 — 한 번에 검증
 * 1. 소프트웨어 포이즌: free 블록을 0x6B 로 채우고 alloc 때 검사 (ASan 없는 보드에서 UAF 쓰기 검출)
 * 2. O(1) 초기화: lazy carving (free list + 아직 안 쓴 블록 커서), O(1) reset
 * 3. 나눗셈 없는 포인터 검사: stride 가 2의 거듭제곱이면 shift/mask (Cortex-M0 에는 UDIV 가 없다)
 * 4. 크기별 풀 세트: 32/64/128 B, 작은 풀이 비면 큰 풀로 fallback, free 는 주소로 주인 찾기
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ALIGN ((size_t)_Alignof(max_align_t))

static void *blk_next(const void *b) { void *n; memcpy(&n, b, sizeof n); return n; }
static void blk_set_next(void *b, void *n) { memcpy(b, &n, sizeof n); }
static size_t stride_of(size_t s) {
    if (s < sizeof(void *)) s = sizeof(void *);
    return (s + ALIGN - 1u) & ~(ALIGN - 1u);
}

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- 연습 1: 소프트웨어 포이즌 ---------- */
#define POISON_FREE  0x6Bu                   /* Linux include/linux/poison.h 의 POISON_FREE 와 같은 값 */
#define POISON_INUSE 0x5Au                   /* 새로 할당한 블록: 초기화 안 하고 읽으면 0x5A5A… 가 보인다 */
typedef struct {
    uint8_t *base; size_t stride; uint32_t n, used;
    void *head;
    uint32_t poison_hits, last_bad;          /* 포이즌 깨진 블록 수 / 마지막 블록 번호 */
    bool corrupt;                            /* free list 포인터 자체가 망가짐 → 풀 정지 */
} ppool_t;

static bool pp_owns_start(const ppool_t *p, const void *b) {
    uintptr_t a = (uintptr_t)b, b0 = (uintptr_t)p->base;
    return a >= b0 && a - b0 < (uintptr_t)p->n * p->stride && (a - b0) % p->stride == 0;
}
static void pp_init(ppool_t *p, void *buf, size_t bytes, size_t bs) {
    memset(p, 0, sizeof *p);
    p->base = buf; p->stride = stride_of(bs); p->n = (uint32_t)(bytes / p->stride);
    for (uint32_t i = p->n; i-- > 0;) {
        uint8_t *b = p->base + (size_t)i * p->stride;
        memset(b, POISON_FREE, p->stride);
        blk_set_next(b, p->head);
        p->head = b;
    }
}
static void *pp_alloc(ppool_t *p) {
    uint8_t *b = p->head;
    if (b == NULL || p->corrupt) return NULL;
    void *nx = blk_next(b);
    if (nx != NULL && !pp_owns_start(p, nx)) {      /* next 포인터가 덮어써짐 — 따라가면 안 된다 */
        p->corrupt = true; p->head = NULL;
        p->last_bad = (uint32_t)((size_t)(b - p->base) / p->stride);
        return NULL;
    }
    p->head = nx;
    for (size_t k = sizeof(void *); k < p->stride; k++)
        if (b[k] != POISON_FREE) { p->poison_hits++; p->last_bad = (uint32_t)((size_t)(b - p->base) / p->stride); break; }
    memset(b, POISON_INUSE, p->stride);
    p->used++;
    return b;
}
static void pp_free(ppool_t *p, void *b) {
    memset(b, POISON_FREE, p->stride);
    blk_set_next(b, p->head);
    p->head = b;
    p->used--;
}

/* ---------- 연습 2: lazy carving — init/reset O(1) ---------- */
typedef struct {
    uint8_t *base; size_t stride; uint32_t n;
    void *head;                              /* 한 번 이상 쓰였다가 반납된 블록 */
    uint32_t fresh;                          /* [fresh, n) = 한 번도 안 나간 블록 */
    uint32_t used;
} lpool_t;

static void lp_init(lpool_t *p, void *buf, size_t bytes, size_t bs) {
    p->base = buf; p->stride = stride_of(bs); p->n = (uint32_t)(bytes / p->stride);
    p->head = NULL; p->fresh = 0; p->used = 0;          /* 블록을 하나도 건드리지 않는다 */
}
static void *lp_alloc(lpool_t *p) {
    void *b = p->head;
    if (b != NULL) p->head = blk_next(b);
    else if (p->fresh < p->n) b = p->base + (size_t)p->fresh++ * p->stride;
    else return NULL;
    p->used++;
    return b;
}
static void lp_free(lpool_t *p, void *b) { blk_set_next(b, p->head); p->head = b; p->used--; }
static void lp_reset(lpool_t *p) { p->head = NULL; p->fresh = 0; p->used = 0; }   /* 전부 반납 O(1) */

/* 비교용 eager 풀 (본문 방식) */
typedef struct { uint8_t *base; size_t stride; uint32_t n, used; void *head; uint32_t init_writes; } epool_t;
static void ep_init(epool_t *p, void *buf, size_t bytes, size_t bs) {
    p->base = buf; p->stride = stride_of(bs); p->n = (uint32_t)(bytes / p->stride);
    p->head = NULL; p->used = 0; p->init_writes = 0;
    for (uint32_t i = p->n; i-- > 0;) {
        uint8_t *b = p->base + (size_t)i * p->stride;
        blk_set_next(b, p->head); p->head = b; p->init_writes++;
    }
}
static void *ep_alloc(epool_t *p) {
    void *b = p->head;
    if (b == NULL) return NULL;
    p->head = blk_next(b); p->used++;
    return b;
}
static void ep_free(epool_t *p, void *b) { blk_set_next(b, p->head); p->head = b; p->used--; }

/* ---------- 연습 3: 나눗셈 없는 블록 번호 계산 ---------- */
typedef enum { V_OK, V_RANGE, V_INTERIOR } vres_t;
static vres_t idx_div(uintptr_t base, size_t stride, uint32_t n, uintptr_t a, uint32_t *idx) {
    if (a < base || a - base >= (uintptr_t)n * stride) return V_RANGE;
    if ((a - base) % stride) return V_INTERIOR;
    *idx = (uint32_t)((a - base) / stride);
    return V_OK;
}
/* stride = 1 << shift. 나머지 = off & (stride-1), 몫 = off >> shift — 곱셈·나눗셈 0 */
static vres_t idx_shift(uintptr_t base, unsigned shift, uint32_t n, uintptr_t a, uint32_t *idx) {
    uintptr_t off = a - base;                          /* a < base 면 매우 큰 값으로 감김 → 아래에서 RANGE */
    if (a < base || (off >> shift) >= n) return V_RANGE;
    if (off & (((uintptr_t)1 << shift) - 1u)) return V_INTERIOR;
    *idx = (uint32_t)(off >> shift);
    return V_OK;
}

/* ---------- 연습 4: 크기별 풀 세트 ---------- */
typedef struct { uint8_t *base; size_t stride; uint32_t n, used, hwm; void *head; } spool_t;
#define NCLASS 3
static const size_t k_class[NCLASS] = { 32, 64, 128 };
static const uint32_t k_count[NCLASS] = { 16, 8, 4 };
static _Alignas(max_align_t) uint8_t g_set_mem[16 * 32 + 8 * 64 + 4 * 128];
static spool_t g_set[NCLASS];

static void set_init(void) {
    uint8_t *m = g_set_mem;
    for (int c = 0; c < NCLASS; c++) {
        spool_t *p = &g_set[c];
        p->base = m; p->stride = k_class[c]; p->n = k_count[c]; p->used = p->hwm = 0; p->head = NULL;
        for (uint32_t i = p->n; i-- > 0;) {
            uint8_t *b = p->base + (size_t)i * p->stride;
            blk_set_next(b, p->head); p->head = b;
        }
        m += (size_t)p->n * p->stride;
    }
}
/* size 이상인 가장 작은 클래스부터, 비었으면 다음 클래스로 */
static void *set_alloc(size_t size, int *cls) {
    for (int c = 0; c < NCLASS; c++) {
        spool_t *p = &g_set[c];
        if (size > p->stride || p->head == NULL) continue;
        void *b = p->head;
        p->head = blk_next(b);
        if (++p->used > p->hwm) p->hwm = p->used;
        if (cls) *cls = c;
        return b;
    }
    return NULL;
}
/* 주소 범위로 주인 풀을 찾는다 — 호출자가 크기를 기억할 필요 없음 */
static bool set_free(void *b) {
    uintptr_t a = (uintptr_t)b;
    for (int c = 0; c < NCLASS; c++) {
        spool_t *p = &g_set[c];
        uintptr_t b0 = (uintptr_t)p->base;
        if (a >= b0 && a - b0 < (uintptr_t)p->n * p->stride) {
            if ((a - b0) % p->stride) return false;
            blk_set_next(b, p->head); p->head = b; p->used--;
            return true;
        }
    }
    return false;
}

static _Alignas(max_align_t) uint8_t g_mem[64 * 64];
static _Alignas(max_align_t) uint8_t g_mem2[64 * 64];
static void *g_hold[256];

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1: UAF 쓰기 — 데이터 영역(포이즌 깨짐)과 next 포인터(free list 오염) 두 경우 */
    {
        ppool_t p;
        pp_init(&p, g_mem, 8 * 32, 32);
        uint8_t *a = pp_alloc(&p);
        CHECK(a != NULL && a[0] == POISON_INUSE && a[31] == POISON_INUSE);   /* 미초기화 읽기 = 0x5A */
        pp_free(&p, a);
        a[20] = 0x00;                                                        /* 버그: free 후 쓰기 */
        uint8_t *b = pp_alloc(&p);                                           /* LIFO → 같은 블록 */
        CHECK(b == a && p.poison_hits == 1 && p.last_bad == 0);
        uint32_t bad_data = p.last_bad;
        uint8_t *c = pp_alloc(&p);
        pp_free(&p, c);
        memset(c, 0x41, 8);                                                  /* 버그: next 포인터 덮어씀 */
        CHECK(pp_alloc(&p) == NULL && p.corrupt && p.last_bad == 1);         /* 쓰레기 주소를 따라가지 않음 */
        printf("[1] UAF data write -> poison_hits=%u (block %u) | UAF next-ptr write -> corrupt=%d (block %u), alloc returns NULL\n",
               p.poison_hits, bad_data, p.corrupt, p.last_bad);
        pp_init(&p, g_mem, 8 * 32, 32);                                      /* 정상 사용은 히트 0 */
        bool ok = true;
        uint32_t nh = 0;
        rng = 7u;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32();
            if ((r & 1u) && nh < p.n) { g_hold[nh] = pp_alloc(&p); ok = g_hold[nh] != NULL; if (ok) memset(g_hold[nh++], (int)r, 32); }
            else if (nh > 0) { uint32_t k = (r >> 8) % nh; pp_free(&p, g_hold[k]); g_hold[k] = g_hold[--nh]; }
        }
        CHECK(ok && p.poison_hits == 0 && !p.corrupt);
    }

    /* 연습 2: lazy 풀은 eager 풀과 정확히 같은 블록 순서를 내준다 + init 이 블록을 안 건드림 */
    {
        lpool_t L; epool_t E;
        lp_init(&L, g_mem, sizeof g_mem, 64);
        ep_init(&E, g_mem2, sizeof g_mem2, 64);
        CHECK(L.n == 64 && E.n == 64 && E.init_writes == 64);
        bool ok = true;
        uint32_t nh = 0;
        static uint32_t idx_l[64], idx_e[64];
        rng = 99u;
        for (int op = 0; op < 50000 && ok; op++) {
            uint32_t r = xorshift32();
            if ((r % 100u) < 55u) {
                uint8_t *l = lp_alloc(&L), *e = ep_alloc(&E);
                ok = (l == NULL) == (e == NULL);
                if (ok && l) {
                    ok = (size_t)(l - g_mem) == (size_t)(e - g_mem2);              /* 같은 블록 번호 */
                    idx_l[nh] = (uint32_t)((size_t)(l - g_mem) / 64u); idx_e[nh] = (uint32_t)((size_t)(e - g_mem2) / 64u); nh++;
                }
            } else if (nh > 0) {
                uint32_t k = (r >> 8) % nh;
                lp_free(&L, g_mem + (size_t)idx_l[k] * 64u);
                ep_free(&E, g_mem2 + (size_t)idx_e[k] * 64u);
                idx_l[k] = idx_l[--nh]; idx_e[k] = idx_e[nh];
            }
            ok = ok && L.used == E.used && L.used == nh;
        }
        CHECK(ok);
        lp_reset(&L);
        CHECK(L.used == 0 && lp_alloc(&L) == g_mem && lp_alloc(&L) == g_mem + 64);  /* reset 후 다시 0, 1 … */
    }

    /* 연습 3: shift/mask 버전 == 나눗셈 버전, 모든 오프셋 [-2*stride, span + 2*stride) */
    {
        bool ok = true;
        uint32_t cases = 0;
        uintptr_t base = (uintptr_t)g_mem + 256u;
        for (unsigned sh = 3; sh <= 7; sh++) {                                   /* 8 … 128 B */
            size_t stride = (size_t)1 << sh;
            uint32_t n = 16;
            for (uintptr_t a = base - 2u * stride; a < base + (n + 2u) * stride; a++) {
                uint32_t i1 = 0xFFFFFFFFu, i2 = 0xFFFFFFFFu;
                vres_t r1 = idx_div(base, stride, n, a, &i1), r2 = idx_shift(base, sh, n, a, &i2);
                ok = ok && r1 == r2 && i1 == i2;
                cases++;
            }
        }
        CHECK(ok);
        printf("[3] shift/mask vs divide: %u addresses, %s\n", cases, ok ? "identical" : "DIFFER");
        CHECK(stride_of(24) == 24 && (stride_of(24) & (stride_of(24) - 1u)) != 0);  /* 24 는 2의 거듭제곱 아님 → 32 로 올리면 25% 낭비 */
    }

    /* 연습 4: 크기별 풀 세트 */
    {
        set_init();
        int c = -1;
        void *a = set_alloc(10, &c);  CHECK(a && c == 0);
        void *b = set_alloc(33, &c);  CHECK(b && c == 1);
        void *d = set_alloc(128, &c); CHECK(d && c == 2);
        CHECK(set_alloc(129, &c) == NULL);                                       /* 최대 클래스 초과 */
        CHECK(set_free(a) && set_free(b) && set_free(d) && !set_free(g_mem));   /* 주인 없는 주소 거절 */
        /* 32 B 풀(16개) 소진 → 17번째 작은 요청은 64 B 풀에서 */
        uint32_t nh = 0;
        for (int i = 0; i < 16; i++) g_hold[nh++] = set_alloc(8, NULL);
        CHECK(set_alloc(8, &c) != NULL && c == 1 && g_set[0].used == 16 && g_set[1].used == 1);
        while (nh > 0) set_free(g_hold[--nh]);
        /* 랜덤: 각 클래스 used == 모델, 전부 반납 후 0 */
        int cls_of[256];
        uint32_t model[NCLASS] = { 0, 0, 0 };
        nh = 0;
        set_init();
        model[0] = model[1] = model[2] = 0;
        bool ok = true;
        rng = 4u;
        for (int op = 0; op < 30000 && ok; op++) {
            uint32_t r = xorshift32();
            if ((r & 1u) && nh < 256) {
                int cc = -1;
                void *x = set_alloc(1u + (r >> 8) % 128u, &cc);
                if (x) { g_hold[nh] = x; cls_of[nh++] = cc; model[cc]++; }
            } else if (nh > 0) {
                uint32_t k = (r >> 8) % nh;
                ok = set_free(g_hold[k]);
                model[cls_of[k]]--;
                g_hold[k] = g_hold[--nh]; cls_of[k] = cls_of[nh];
            }
            for (int q = 0; q < NCLASS; q++) ok = ok && g_set[q].used == model[q];
        }
        while (ok && nh > 0) ok = set_free(g_hold[--nh]);
        CHECK(ok && g_set[0].used == 0 && g_set[1].used == 0 && g_set[2].used == 0);
        printf("[4] size-class set hwm: 32B %u/%u, 64B %u/%u, 128B %u/%u\n",
               g_set[0].hwm, g_set[0].n, g_set[1].hwm, g_set[1].n, g_set[2].hwm, g_set[2].n);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
