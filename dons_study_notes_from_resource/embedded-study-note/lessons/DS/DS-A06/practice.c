/* DS-A06 추가 연습 — 한 번에 검증
 *   연습 1  tlsf_resize_inplace: 제자리 축소/확장 (꼬리를 뒤 free 와 합치기)
 *   연습 2  tlsf_memalign: DMA 버퍼용 정렬 할당 (앞쪽 gap 은 free 블록으로)
 *   연습 3  tlsf_add_pool: 떨어져 있는 메모리 영역 추가 (풀 경계에서는 병합 금지)
 *   연습 4  tlsf_malloc_peek: "맞는데 실패" 를 줄이는 O(1) 보조 탐색과 그 한계
 *   연습 5  SLI 를 1..5 로 바꿔 보기: 리스트 수, 제어 구조 크기, 미달 폭(shortfall) 전수 계산
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAYBE_UNUSED __attribute__((unused))

#define ALIGN_LOG2  3u
#define ALIGN_SZ    (1u << ALIGN_LOG2)              /* payload 정렬 8 B */
#define SLI         4u                              /* FL 하나를 2^SLI 개로 선형 분할 */
#define SL_COUNT    (1u << SLI)                     /* 16 */
#define FL_SHIFT    (SLI + ALIGN_LOG2)              /* 7 */
#define SMALL_LIMIT (1u << FL_SHIFT)                /* 128 미만은 fl=0 의 8 B 간격 선형 구간 */
#define MAX_LOG2    18u                             /* 블록 크기 < 2^18 */
#define MAX_HEAP    (1u << MAX_LOG2)                /* 256 KB */
#define FL_COUNT    (MAX_LOG2 - FL_SHIFT + 1u)      /* 12: fl=0 은 [0,128), fl=k 는 [2^(k+6), 2^(k+7)) */
#define NIL         0xFFFFFFFFu
#define HDR         8u                              /* 블록 헤더 */
#define MIN_PAYLOAD 8u                              /* free 링크 2개가 들어갈 크기 */
#define MIN_BLOCK   (HDR + MIN_PAYLOAD)             /* 16 B: 이보다 작게는 쪼개지 않는다 */
#define F_FREE      1u

_Static_assert(FL_COUNT < 32u && SL_COUNT <= 32u, "bitmaps are uint32_t; (fl + 1) shift must stay < 32");

typedef struct { uint32_t bit, list, split, merge; } tlsf_work_t;

typedef struct {
    uint8_t    *mem;
    uint32_t    size;                               /* 힙 전체 (sentinel 포함) */
    uint32_t    fl_bitmap;                          /* bit i = sl_bitmap[i] != 0 */
    uint32_t    sl_bitmap[FL_COUNT];                /* bit j = head[i][j] != NIL */
    uint32_t    head[FL_COUNT][SL_COUNT];
    tlsf_work_t w;
} tlsf_t;

typedef enum { TF_OK = 0, TF_RANGE, TF_ALIGN, TF_CORRUPT, TF_DOUBLE, TF_NOTIMPL } tf_err;
typedef enum {
    TC_OK = 0, TC_WALK, TC_PREV, TC_ADJ, TC_SENTINEL,   /* 물리 순회에서 발견 */
    TC_LINK, TC_NOTFREE, TC_CLASS,                      /* free list 순회에서 발견 */
    TC_BITMAP, TC_COUNT, TC_NOTIMPL                     /* 교차 검사에서 발견 */
} tcheck_t;

typedef struct { uint32_t free_bytes, largest, nfree, nbusy; double frag; } tlsf_stats_t;

/* ---------- 주어진 도구: 바이트 접근 (memcpy 라서 strict aliasing·정렬 UB 없음, -O2 에서 load/store 한 줄) ---------- */
static inline uint32_t rd32(const tlsf_t *t, uint32_t off) { uint32_t v; memcpy(&v, t->mem + off, 4); return v; }
static inline void     wr32(tlsf_t *t, uint32_t off, uint32_t v) { memcpy(t->mem + off, &v, 4); }

static inline MAYBE_UNUSED uint32_t fls32(uint32_t x) { return 31u - (uint32_t)__builtin_clz(x); }   /* x == 0 은 UB — 호출자가 보장 */
static inline MAYBE_UNUSED uint32_t ctz32(uint32_t x) { return (uint32_t)__builtin_ctz(x); }         /* x == 0 은 UB — 호출자가 보장 */

static inline uint32_t b_prev(const tlsf_t *t, uint32_t b)        { return rd32(t, b); }
static inline void     b_set_prev(tlsf_t *t, uint32_t b, uint32_t p) { wr32(t, b, p); }
static inline uint32_t b_size(const tlsf_t *t, uint32_t b)        { return rd32(t, b + 4u) & ~(ALIGN_SZ - 1u); }
static inline bool     b_is_free(const tlsf_t *t, uint32_t b)     { return (rd32(t, b + 4u) & F_FREE) != 0u; }
static inline void     b_set(tlsf_t *t, uint32_t b, uint32_t size, bool is_free) { wr32(t, b + 4u, size | (is_free ? F_FREE : 0u)); }
static inline MAYBE_UNUSED uint32_t b_next(const tlsf_t *t, uint32_t b)        { return b + HDR + b_size(t, b); }
static inline uint32_t l_next(const tlsf_t *t, uint32_t b)        { return rd32(t, b + HDR); }
static inline uint32_t l_prev(const tlsf_t *t, uint32_t b)        { return rd32(t, b + HDR + 4u); }
static inline void     l_set_next(tlsf_t *t, uint32_t b, uint32_t v) { wr32(t, b + HDR, v); }
static inline MAYBE_UNUSED void l_set_prev(tlsf_t *t, uint32_t b, uint32_t v) { wr32(t, b + HDR + 4u, v); }

/* ======================= Part A — 매핑, 비트맵, free list, malloc ======================= */

/* 삽입용 매핑(내림): 크기 size 인 블록이 들어갈 리스트. size >= 8 가정.
 * size < SMALL_LIMIT 이면 fl=0, sl=size/8. 아니면 f=fls(size), sl=최상위 비트 다음 SLI 비트, fl=f-(FL_SHIFT-1). */
static void map_insert(uint32_t size, uint32_t *fl, uint32_t *sl) {
    if (size < SMALL_LIMIT) {
        *fl = 0u;
        *sl = size >> ALIGN_LOG2;
    } else {
        uint32_t f = fls32(size);
        *sl = (size >> (f - SLI)) ^ SL_COUNT;      /* 최상위 1 을 지운다 */
        *fl = f - (FL_SHIFT - 1u);
    }
}

/* 탐색용 매핑(올림): 그 리스트의 "어떤 블록이든" size 이상이 되는 가장 작은 (fl, sl).
 * 표 범위를 넘으면 false. size 는 이미 8 의 배수이고 >= 8. */
static bool map_search(uint32_t size, uint32_t *fl, uint32_t *sl) {
    if (size >= SMALL_LIMIT)
        size += (1u << (fls32(size) - SLI)) - 1u;  /* 다음 class 경계로 올림. -1 이 없으면 경계값이 한 칸 밀린다 */
    map_insert(size, fl, sl);
    return *fl < FL_COUNT;                         /* 올림이 표 끝을 넘을 수 있다 */
}

/* 리스트 (fl,sl) 의 맨 앞에 free 블록 b 를 넣고 두 비트맵을 켠다. O(1). w.list +1, w.bit +2. */
static void list_insert(tlsf_t *t, uint32_t b, uint32_t fl, uint32_t sl) {
    uint32_t h = t->head[fl][sl];
    l_set_next(t, b, h);
    l_set_prev(t, b, NIL);
    if (h != NIL) l_set_prev(t, h, b);
    t->head[fl][sl] = b;
    t->sl_bitmap[fl] |= 1u << sl;
    t->fl_bitmap |= 1u << fl;
    t->w.bit += 2u;
    t->w.list++;
}

/* 리스트 (fl,sl) 에서 b 를 뗀다. 리스트가 비면 sl 비트를, sl 워드가 0 이 되면 fl 비트를 지운다.
 * w.list +1, 실제로 지운 비트마다 w.bit +1. */
static void list_remove(tlsf_t *t, uint32_t b, uint32_t fl, uint32_t sl) {
    uint32_t n = l_next(t, b), p = l_prev(t, b);
    if (n != NIL) l_set_prev(t, n, p);
    if (p != NIL) {
        l_set_next(t, p, n);
    } else {
        t->head[fl][sl] = n;
        if (n == NIL) {
            t->sl_bitmap[fl] &= ~(1u << sl);
            t->w.bit++;
            if (t->sl_bitmap[fl] == 0u) {
                t->fl_bitmap &= ~(1u << fl);
                t->w.bit++;
            }
        }
    }
    t->w.list++;
}

/* (*fl,*sl) 이상에서 비어 있지 않은 가장 작은 리스트를 찾아 그 head 를 돌려주고 (*fl,*sl) 을 갱신한다.
 * 없으면 NIL. 비트맵 워드를 볼 때마다 w.bit +1 (최대 3). 루프 금지. */
static uint32_t find_suitable(tlsf_t *t, uint32_t *fl, uint32_t *sl) {
    uint32_t f = *fl;
    uint32_t sl_map = t->sl_bitmap[f] & (~0u << *sl);
    t->w.bit++;
    if (sl_map == 0u) {
        uint32_t fl_map = t->fl_bitmap & (~0u << (f + 1u));   /* f+1 <= FL_COUNT < 32 이라 시프트 안전 */
        t->w.bit++;
        if (fl_map == 0u) return NIL;
        f = ctz32(fl_map);
        sl_map = t->sl_bitmap[f];
        t->w.bit++;
    }
    *fl = f;
    *sl = ctz32(sl_map);
    return t->head[f][*sl];
}

/* n 바이트 할당. n==0 이나 너무 크면 NULL. n 은 8 의 배수(최소 8)로 올린 뒤 map_search → find_suitable → 분할.
 * 남는 쪽의 블록 전체(헤더 포함)가 MIN_BLOCK 이상일 때만 쪼갠다. 쪼개면 w.split +1. */
static void *tlsf_malloc(tlsf_t *t, size_t n) {
    if (n == 0u || n > MAX_HEAP) return NULL;                  /* 올림 연산의 overflow 도 여기서 막는다 */
    uint32_t r = (uint32_t)((n + ALIGN_SZ - 1u) & ~(size_t)(ALIGN_SZ - 1u));
    if (r < MIN_PAYLOAD) r = MIN_PAYLOAD;
    uint32_t fl, sl;
    if (!map_search(r, &fl, &sl)) return NULL;
    uint32_t b = find_suitable(t, &fl, &sl);
    if (b == NIL) return NULL;
    list_remove(t, b, fl, sl);
    uint32_t sz = b_size(t, b);
    if (sz - r >= MIN_BLOCK) {
        uint32_t rem = b + HDR + r, rem_sz = sz - r - HDR, after = b + HDR + sz;
        b_set(t, rem, rem_sz, true);
        b_set_prev(t, rem, b);
        b_set_prev(t, after, rem);
        map_insert(rem_sz, &fl, &sl);
        list_insert(t, rem, fl, sl);
        t->w.split++;
        sz = r;
    }
    b_set(t, b, sz, false);
    return t->mem + b + HDR;
}

/* 주어짐: 힙 초기화. 맨 앞 free 블록 하나 + 끝 sentinel. bytes 는 8 배수로 내림, 최대 MAX_HEAP. */
static bool tlsf_init(tlsf_t *t, void *mem, uint32_t bytes) {
    if (((uintptr_t)mem & (ALIGN_SZ - 1u)) != 0u) return false;
    bytes &= ~(ALIGN_SZ - 1u);
    if (bytes < 2u * HDR + MIN_PAYLOAD || bytes > MAX_HEAP) return false;
    memset(t, 0, sizeof *t);
    t->mem = mem;
    t->size = bytes;
    for (uint32_t i = 0; i < FL_COUNT; i++)
        for (uint32_t j = 0; j < SL_COUNT; j++) t->head[i][j] = NIL;
    uint32_t payload = bytes - 2u * HDR, end = bytes - HDR, fl, sl;
    b_set_prev(t, 0u, NIL);
    b_set(t, 0u, payload, true);
    b_set_prev(t, end, 0u);
    b_set(t, end, 0u, false);
    map_insert(payload, &fl, &sl);
    list_insert(t, 0u, fl, sl);
    return true;
}

static uint32_t tlsf_usable(const tlsf_t *t, const void *p) {
    return p ? b_size(t, (uint32_t)((const uint8_t *)p - t->mem) - HDR) : 0u;
}

/* ======================= Part B — free 와 불변식 검사기 ======================= */

/* 블록 해제. 순서: NULL → OK, 범위/정렬 → TF_RANGE/TF_ALIGN, 크기와 앞뒤 연결 검사 → TF_CORRUPT,
 * 이미 free → TF_DOUBLE, 그다음에야 힙을 고친다 (실패하면 힙은 그대로). 앞/뒤 이웃이 free 면 병합(각 w.merge +1).
 * *out_size(있으면) = 해제한 블록의 payload 크기(성공 시), 실패 시 0. */
static tf_err tlsf_free(tlsf_t *t, void *p, uint32_t *out_size) {
    if (out_size) *out_size = 0u;
    if (p == NULL) return TF_OK;
    uintptr_t up = (uintptr_t)p, base = (uintptr_t)t->mem;
    if (up < base + HDR || up >= base + t->size - HDR) return TF_RANGE;
    if (((up - base) & (ALIGN_SZ - 1u)) != 0u) return TF_ALIGN;

    uint32_t b = (uint32_t)(up - base) - HDR, end = t->size - HDR;
    uint32_t sz = b_size(t, b), pv = b_prev(t, b);
    if (sz < MIN_PAYLOAD || sz > end - b - HDR) return TF_CORRUPT;
    uint32_t nx = b + HDR + sz;
    if (b_prev(t, nx) != b) return TF_CORRUPT;                 /* 뒤 블록이 나를 가리켜야 한다 */
    if (pv != NIL && (pv >= b || (pv & (ALIGN_SZ - 1u)) != 0u || pv + HDR + b_size(t, pv) != b))
        return TF_CORRUPT;                                     /* 앞 블록이 나까지 닿아야 한다 (NIL = 풀의 첫 블록) */
    if (b_is_free(t, b)) return TF_DOUBLE;
    if (out_size) *out_size = sz;

    uint32_t start = b, total = sz, fl, sl;
    if (pv != NIL && b_is_free(t, pv)) {
        uint32_t psz = b_size(t, pv);
        map_insert(psz, &fl, &sl);
        list_remove(t, pv, fl, sl);
        start = pv;
        total += psz + HDR;
        t->w.merge++;
    }
    if (b_is_free(t, nx)) {                                    /* sentinel 은 항상 busy */
        uint32_t nsz = b_size(t, nx);
        map_insert(nsz, &fl, &sl);
        list_remove(t, nx, fl, sl);
        total += nsz + HDR;
        nx = nx + HDR + nsz;
        t->w.merge++;
    }
    b_set(t, start, total, true);
    b_set_prev(t, nx, start);
    map_insert(total, &fl, &sl);
    list_insert(t, start, fl, sl);
    return TF_OK;
}


/* ======================= 공통: 다중 풀 불변식 검사 ======================= */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static _Alignas(8) uint8_t g_heap[MAX_HEAP];
static tlsf_t g_t;
typedef struct { uint32_t start, bytes; } pool_t;
static pool_t g_pools[4];
static uint32_t g_npools;

static uint32_t rng32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static uint32_t blk_of(const tlsf_t *t, const void *p) { return (uint32_t)((const uint8_t *)p - t->mem) - HDR; }
static uint32_t adjust(size_t n) { uint32_t r = (uint32_t)((n + ALIGN_SZ - 1u) & ~(size_t)(ALIGN_SZ - 1u)); return r < MIN_PAYLOAD ? MIN_PAYLOAD : r; }

static bool p_check(const tlsf_t *t) {
    uint32_t nfree = 0u, nlist = 0u;
    for (uint32_t i = 0; i < g_npools; i++) {
        uint32_t b = g_pools[i].start, end = g_pools[i].start + g_pools[i].bytes - HDR, prev = NIL;
        bool prev_free = false;
        for (;;) {
            if (b > end || (b & 7u) != 0u || b_prev(t, b) != prev) return false;
            if (b == end) break;
            uint32_t sz = b_size(t, b);
            if (sz < MIN_PAYLOAD || sz > end - b - HDR) return false;
            bool f = b_is_free(t, b);
            if (f && prev_free) return false;
            nfree += f ? 1u : 0u;
            prev = b; prev_free = f;
            b += HDR + sz;
        }
        if (b_size(t, end) != 0u || b_is_free(t, end)) return false;
    }
    for (uint32_t fl = 0; fl < FL_COUNT; fl++) {
        for (uint32_t sl = 0; sl < SL_COUNT; sl++) {
            uint32_t n = t->head[fl][sl], lprev = NIL, f2, s2;
            if ((((t->sl_bitmap[fl] >> sl) & 1u) != 0u) != (n != NIL)) return false;
            while (n != NIL) {
                if (!b_is_free(t, n) || l_prev(t, n) != lprev) return false;
                map_insert(b_size(t, n), &f2, &s2);
                if (f2 != fl || s2 != sl || ++nlist > nfree) return false;
                lprev = n;
                n = l_next(t, n);
            }
        }
        if ((((t->fl_bitmap >> fl) & 1u) != 0u) != (t->sl_bitmap[fl] != 0u)) return false;
    }
    return nlist == nfree;
}

static void use_single_pool(tlsf_t *t, uint32_t bytes) {
    (void)tlsf_init(t, g_heap, bytes);
    g_pools[0] = (pool_t){ 0u, bytes };
    g_npools = 1u;
}

/* ======================= 연습 1: 제자리 realloc ======================= */

/* busy 블록 b (payload sz) 를 r 로 줄인다. 잘라 낸 꼬리는 free 로 — 뒤 이웃이 free 면 합쳐서. O(1) */
static void shrink_tail(tlsf_t *t, uint32_t b, uint32_t sz, uint32_t r) {
    if (sz - r < MIN_BLOCK) { b_set(t, b, sz, false); return; }
    uint32_t tail = b + HDR + r, tsz = sz - r - HDR, nx = b + HDR + sz, fl, sl;
    if (b_is_free(t, nx)) {                        /* 이걸 빠뜨리면 free 블록 두 개가 붙는다 */
        uint32_t nsz = b_size(t, nx);
        map_insert(nsz, &fl, &sl);
        list_remove(t, nx, fl, sl);
        tsz += HDR + nsz;
        nx += HDR + nsz;
    }
    b_set(t, b, r, false);
    b_set(t, tail, tsz, true);
    b_set_prev(t, tail, b);
    b_set_prev(t, nx, tail);
    map_insert(tsz, &fl, &sl);
    list_insert(t, tail, fl, sl);
}

/* p 를 옮기지 않고 n 바이트로 바꾼다. 불가능하면 false (블록 그대로). */
static bool tlsf_resize_inplace(tlsf_t *t, void *p, size_t n) {
    if (p == NULL || n == 0u || n > MAX_HEAP) return false;
    uint32_t b = blk_of(t, p), sz = b_size(t, b), r = adjust(n);
    if (r <= sz) { shrink_tail(t, b, sz, r); return true; }
    uint32_t nx = b + HDR + sz;
    if (!b_is_free(t, nx)) return false;
    uint32_t nsz = b_size(t, nx), total = sz + HDR + nsz, fl, sl;
    if (total < r) return false;
    map_insert(nsz, &fl, &sl);
    list_remove(t, nx, fl, sl);
    b_set(t, b, total, false);
    b_set_prev(t, nx + HDR + nsz, b);
    shrink_tail(t, b, total, r);
    return true;
}

static void practice1(void) {
    puts("== 연습 1: resize in place");
    tlsf_t *t = &g_t;
    use_single_pool(t, 4096u);
    uint8_t *a = tlsf_malloc(t, 100u), *g = tlsf_malloc(t, 64u);
    memset(a, 0x77, 100u);

    CHECK(tlsf_resize_inplace(t, a, 40u) && tlsf_usable(t, a) == 40u && p_check(t));          /* 축소: 꼬리는 free */
    CHECK(tlsf_resize_inplace(t, a, 96u) && tlsf_usable(t, a) == 104u && p_check(t));         /* 방금 만든 꼬리로 확장, 남는 8 은 못 쪼갬 */
    CHECK(a[0] == 0x77 && a[39] == 0x77);                                                      /* 데이터 유지 */
    CHECK(tlsf_resize_inplace(t, a, 104u) && tlsf_usable(t, a) == 104u && p_check(t));
    CHECK(!tlsf_resize_inplace(t, a, 112u) && tlsf_usable(t, a) == 104u && p_check(t));       /* 뒤가 busy: 불가, 그대로 */
    CHECK(tlsf_resize_inplace(t, a, 100u) && tlsf_usable(t, a) == 104u && p_check(t));        /* 8 배수로 올리면 같은 크기 */

    /* 꼬리를 자른 자리 뒤가 free 면 합쳐져야 한다 */
    CHECK(tlsf_free(t, g, NULL) == TF_OK);
    uint32_t tail_old = b_size(t, blk_of(t, a) + HDR + 104u);
    CHECK(tlsf_resize_inplace(t, a, 32u) && p_check(t));
    uint32_t tail_new = b_size(t, blk_of(t, a) + HDR + 32u);
    CHECK(tail_new == tail_old + 104u - 32u && p_check(t));
}

/* ======================= 연습 2: 정렬 할당 ======================= */

static void *tlsf_memalign(tlsf_t *t, size_t align, size_t n) {
    if (align < ALIGN_SZ || (align & (align - 1u)) != 0u || n == 0u || n > MAX_HEAP) return NULL;
    if (align == ALIGN_SZ) return tlsf_malloc(t, n);
    uint32_t r = adjust(n);
    void *raw = tlsf_malloc(t, (size_t)r + align + MIN_BLOCK);      /* gap 이 아무리 커도 r 이 남는 크기 */
    if (raw == NULL) return NULL;

    uint32_t b = blk_of(t, raw), sz = b_size(t, b);
    uintptr_t up = (uintptr_t)raw, ap = (up + align - 1u) & ~(uintptr_t)(align - 1u);
    if (ap != up) {
        if (ap - up < MIN_BLOCK) ap += align;                       /* 앞 조각이 free 블록이 될 수 없으면 한 칸 더 */
        uint32_t gap = (uint32_t)(ap - up), nb = b + gap, nsz = sz - gap, nx = b + HDR + sz, fl, sl;
        b_set(t, nb, nsz, false);
        b_set_prev(t, nb, b);
        b_set_prev(t, nx, nb);
        b_set(t, b, gap - HDR, true);                               /* b 앞 블록은 busy 였으므로 병합 불필요 */
        map_insert(gap - HDR, &fl, &sl);
        list_insert(t, b, fl, sl);
        b = nb;
        sz = nsz;
    }
    shrink_tail(t, b, sz, r);
    return t->mem + b + HDR;
}

static void practice2(void) {
    puts("== 연습 2: memalign");
    tlsf_t *t = &g_t;
    use_single_pool(t, 65536u);
    CHECK(tlsf_memalign(t, 3u, 64u) == NULL);
    CHECK(tlsf_memalign(t, 4u, 64u) == NULL);
    void *p[6];
    static const uint32_t al[6] = { 16u, 32u, 64u, 256u, 1024u, 4096u };
    bool aligned = true, sized = true, inv = true;
    (void)tlsf_malloc(t, 24u);                                        /* 시작 위치를 어긋나게 */
    for (int i = 0; i < 6; i++) {
        p[i] = tlsf_memalign(t, al[i], 100u + 40u * (uint32_t)i);
        if (p[i] == NULL || ((uintptr_t)p[i] & (al[i] - 1u)) != 0u) aligned = false;
        if (p[i] != NULL && tlsf_usable(t, p[i]) < 100u + 40u * (uint32_t)i) sized = false;
        if (!p_check(t)) inv = false;
    }
    CHECK(aligned);
    CHECK(sized);
    CHECK(inv);
    uint32_t s = 0xABCD1234u;
    bool all = true;
    for (int i = 5; i >= 0; i--) { if (tlsf_free(t, p[i], NULL) != TF_OK || !p_check(t)) all = false; }
    CHECK(all);

    /* 랜덤 정렬/크기로 많이 — 앞/뒤 조각이 모두 돌아와야 한다 */
    use_single_pool(t, 65536u);
    void *live[64];
    uint32_t n = 0u, nulls = 0u, bad = 0u;
    for (uint32_t i = 0; i < 20000u; i++) {
        if (n == 0u || (n < 64u && (rng32(&s) & 1u))) {
            uint32_t a = 8u << (rng32(&s) % 9u);
            uint32_t sz = 1u + rng32(&s) % 1024u;
            void *q = tlsf_memalign(t, a, sz);
            if (q == NULL) nulls++;
            else { if (((uintptr_t)q & (a - 1u)) != 0u || tlsf_usable(t, q) < sz) bad++; live[n++] = q; }
        } else {
            uint32_t k = rng32(&s) % n;
            if (tlsf_free(t, live[k], NULL) != TF_OK) bad++;
            live[k] = live[--n];
        }
        if (!p_check(t)) { bad++; break; }
    }
    while (n > 0u) if (tlsf_free(t, live[--n], NULL) != TF_OK) bad++;
    printf("memalign stress: NULL %u, bad %u\n", nulls, bad);
    CHECK(bad == 0u && p_check(t));
    CHECK(b_size(t, 0u) == 65536u - 16u && b_is_free(t, 0u));         /* 조각 없이 전부 복원 */
}

/* ======================= 연습 3: 풀 추가 ======================= */

static void tlsf_init_empty(tlsf_t *t, void *mem, uint32_t arena) {
    memset(t, 0, sizeof *t);
    t->mem = mem;
    t->size = arena;
    for (uint32_t i = 0; i < FL_COUNT; i++)
        for (uint32_t j = 0; j < SL_COUNT; j++) t->head[i][j] = NIL;
    g_npools = 0u;
}

static bool tlsf_add_pool(tlsf_t *t, uint32_t start, uint32_t bytes) {
    bytes &= ~(ALIGN_SZ - 1u);
    if ((start & 7u) != 0u || bytes < 2u * HDR + MIN_PAYLOAD || start + bytes > t->size || g_npools == 4u) return false;
    for (uint32_t i = 0; i < g_npools; i++)
        if (start < g_pools[i].start + g_pools[i].bytes && g_pools[i].start < start + bytes) return false;
    uint32_t payload = bytes - 2u * HDR, end = start + bytes - HDR, fl, sl;
    b_set_prev(t, start, NIL);
    b_set(t, start, payload, true);
    b_set_prev(t, end, start);
    b_set(t, end, 0u, false);                                          /* busy sentinel: 풀 경계에서 병합이 멈춘다 */
    map_insert(payload, &fl, &sl);
    list_insert(t, start, fl, sl);
    g_pools[g_npools++] = (pool_t){ start, bytes };
    return true;
}

static void practice3(void) {
    puts("== 연습 3: add pool (SRAM + CCM 모델)");
    tlsf_t *t = &g_t;
    tlsf_init_empty(t, g_heap, 65536u);
    CHECK(tlsf_add_pool(t, 0u, 8192u));
    CHECK(tlsf_add_pool(t, 8192u, 8192u));                             /* 맞닿은 풀 */
    CHECK(tlsf_add_pool(t, 32768u, 8192u));                            /* 떨어진 풀 */
    CHECK(!tlsf_add_pool(t, 4096u, 8192u));                            /* 겹침 거절 */
    CHECK(p_check(t));

    uint8_t *a = tlsf_malloc(t, 7000u), *b = tlsf_malloc(t, 7000u), *c = tlsf_malloc(t, 7000u);
    uint32_t oa = a ? (uint32_t)(a - g_heap) : NIL, ob = b ? (uint32_t)(b - g_heap) : NIL, oc = c ? (uint32_t)(c - g_heap) : NIL;
    printf("offsets: %u %u %u\n", oa, ob, oc);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(((oa < 8192u) + (ob < 8192u) + (oc < 8192u)) == 1 && ((oa >= 32768u) + (ob >= 32768u) + (oc >= 32768u)) == 1);
    CHECK(tlsf_malloc(t, 7000u) == NULL && p_check(t));
    CHECK(tlsf_free(t, a, NULL) == TF_OK && tlsf_free(t, b, NULL) == TF_OK && tlsf_free(t, c, NULL) == TF_OK && p_check(t));
    bool three = true;
    for (uint32_t i = 0; i < 3u; i++) if (!b_is_free(t, g_pools[i].start) || b_size(t, g_pools[i].start) != 8192u - 16u) three = false;
    CHECK(three);                                                      /* 맞닿은 두 풀도 합쳐지지 않는다 */

    uint32_t s = 0x51EED5u, n = 0u, bad = 0u;
    void *live[128];
    for (uint32_t i = 0; i < 20000u; i++) {
        if (n == 0u || (n < 128u && (rng32(&s) & 1u))) {
            void *q = tlsf_malloc(t, 1u + rng32(&s) % 1500u);
            if (q) live[n++] = q;
        } else {
            uint32_t k = rng32(&s) % n;
            if (tlsf_free(t, live[k], NULL) != TF_OK) bad++;
            live[k] = live[--n];
        }
        if (!p_check(t)) { bad++; break; }
    }
    while (n > 0u) if (tlsf_free(t, live[--n], NULL) != TF_OK) bad++;
    CHECK(bad == 0u && p_check(t));
}

/* ======================= 연습 4: O(1) 보조 탐색 ======================= */

static void *take_block(tlsf_t *t, uint32_t b, uint32_t fl, uint32_t sl, uint32_t r) {
    list_remove(t, b, fl, sl);
    uint32_t sz = b_size(t, b);
    if (sz - r >= MIN_BLOCK) {
        uint32_t rem = b + HDR + r, rem_sz = sz - r - HDR, after = b + HDR + sz;
        b_set(t, rem, rem_sz, true);
        b_set_prev(t, rem, b);
        b_set_prev(t, after, rem);
        map_insert(rem_sz, &fl, &sl);
        list_insert(t, rem, fl, sl);
        sz = r;
    }
    b_set(t, b, sz, false);
    return t->mem + b + HDR;
}

/* 정규 탐색이 실패하면 r 이 속한 리스트의 head 하나만 본다. 루프 없음. */
static void *tlsf_malloc_peek(tlsf_t *t, size_t n) {
    void *p = tlsf_malloc(t, n);
    if (p != NULL || n == 0u || n > MAX_HEAP) return p;
    uint32_t r = adjust(n), fl, sl;
    map_insert(r, &fl, &sl);
    if (fl >= FL_COUNT) return NULL;
    uint32_t h = t->head[fl][sl];
    if (h == NIL || b_size(t, h) < r) return NULL;
    return take_block(t, h, fl, sl, r);
}

static void h_fill(tlsf_t *t) { while (tlsf_malloc(t, 8u) != NULL) { } }

static void practice4(void) {
    puts("== 연습 4: peek");
    tlsf_t *t = &g_t;
    use_single_pool(t, 16384u);
    void *big = tlsf_malloc(t, 5880u);
    h_fill(t);
    (void)tlsf_free(t, big, NULL);
    CHECK(tlsf_malloc(t, 5640u) == NULL);
    void *p = tlsf_malloc_peek(t, 5640u);
    CHECK(p == big && tlsf_usable(t, p) == 5640u && p_check(t));

    /* 한계: 같은 리스트에 큰 블록이 있어도 head 가 작으면 못 찾는다 */
    use_single_pool(t, 32768u);
    void *b1 = tlsf_malloc(t, 5880u), *g1 = tlsf_malloc(t, 8u), *b2 = tlsf_malloc(t, 5640u), *g2 = tlsf_malloc(t, 8u);
    h_fill(t);
    (void)g1; (void)g2;
    (void)tlsf_free(t, b1, NULL);
    (void)tlsf_free(t, b2, NULL);                                      /* b2(5640) 가 head, b1(5880) 은 뒤 */
    CHECK(tlsf_malloc_peek(t, 5760u) == NULL && p_check(t));           /* 5880 이 있어도 O(1) 로는 못 본다 */
    CHECK(tlsf_malloc_peek(t, 5640u) == b2 && p_check(t));

    /* 같은 난수 요청열에서 정규 vs peek 의 실패 수 (peek 은 작은 칸 하나 더 볼 뿐) */
    uint32_t fails[2] = { 0u, 0u };
    for (int mode = 0; mode < 2; mode++) {
        use_single_pool(t, 65536u);
        uint32_t s = 0xC0FFEE11u, n = 0u;
        void *live[512];
        for (uint32_t i = 0; i < 200000u; i++) {
            if (n == 0u || (n < 512u && rng32(&s) % 100u < 55u)) {
                uint32_t sz = 16u + rng32(&s) % 2000u;
                void *q = mode ? tlsf_malloc_peek(t, sz) : tlsf_malloc(t, sz);
                if (q) live[n++] = q; else fails[mode]++;
            } else {
                uint32_t k = rng32(&s) % n;
                (void)tlsf_free(t, live[k], NULL);
                live[k] = live[--n];
            }
        }
        CHECK(p_check(t));
    }
    printf("random workload failures: plain %u, peek %u\n", fails[0], fails[1]);
    CHECK(fails[1] <= fails[0]);
}

/* ======================= 연습 5: SLI 스윕 ======================= */

static uint32_t shift_of(uint32_t sli) { return sli + ALIGN_LOG2; }
static void map_ins_k(uint32_t size, uint32_t sli, uint32_t *fl, uint32_t *sl) {
    uint32_t sh = shift_of(sli);
    if (size < (1u << sh)) { *fl = 0u; *sl = size >> ALIGN_LOG2; return; }
    uint32_t f = fls32(size);
    *sl = (size >> (f - sli)) ^ (1u << sli);
    *fl = f - (sh - 1u);
}
static void map_srch_k(uint32_t size, uint32_t sli, uint32_t *fl, uint32_t *sl) {
    if (size >= (1u << shift_of(sli))) size += (1u << (fls32(size) - sli)) - 1u;
    map_ins_k(size, sli, fl, sl);
}
static uint32_t lo_k(uint32_t fl, uint32_t sl, uint32_t sli) {
    uint32_t sh = shift_of(sli);
    return fl == 0u ? sl << ALIGN_LOG2 : ((1u << sli) + sl) << (fl + sh - 1u - sli);
}

static void practice5(void) {
    puts("== 연습 5: SLI sweep");
    printf("SLI  FL  lists  control_bytes  worst_shortfall@[4096,8192)  G(f=12)\n");
    bool shortfall_ok = true, maps_ok = true;
    for (uint32_t sli = 1u; sli <= 5u; sli++) {
        uint32_t fl, sl, flcount = MAX_LOG2 - shift_of(sli) + 1u, lists = flcount << sli;
        uint32_t control = 4u + flcount * 4u + lists * 4u, worst = 0u;
        for (uint32_t s = 8u; s < MAX_HEAP; s += 8u) {
            map_ins_k(s, sli, &fl, &sl);
            if (lo_k(fl, sl, sli) > s) maps_ok = false;
            map_srch_k(s, sli, &fl, &sl);
            if (fl < flcount && lo_k(fl, sl, sli) < s) maps_ok = false;
        }
        for (uint32_t bsz = 4096u; bsz < 8192u; bsz += 8u) {
            uint32_t reach = 0u;
            for (uint32_t r = bsz; r >= 8u; r -= 8u) {
                map_srch_k(r, sli, &fl, &sl);
                if (lo_k(fl, sl, sli) <= bsz) { reach = r; break; }
            }
            if (bsz - reach > worst) worst = bsz - reach;
        }
        uint32_t g = 1u << (12u - sli);
        printf("%3u %3u %6u %14u %28u %8u\n", sli, flcount, lists, control, worst, g);
        if (worst != g - 8u) shortfall_ok = false;
    }
    CHECK(maps_ok);
    CHECK(shortfall_ok);                                               /* 미달 폭의 최댓값 = G - 8 (8 정렬일 때) */
}

int main(void) {
    practice1();
    practice2();
    practice3();
    practice4();
    practice5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
