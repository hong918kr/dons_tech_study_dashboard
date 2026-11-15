/* DS-A06 TLSF 할당기: 실시간 O(1) malloc
 *
 * 256 KB 까지의 고정 힙(호출자 버퍼) 위에 TLSF(Two-Level Segregated Fit)를 만든다. malloc 없음.
 *   Part A  FL/SL 매핑(삽입은 내림, 탐색은 올림), 2단계 비트맵, free list, tlsf_malloc(분할)
 *   Part B  tlsf_free(물리 이웃 병합, 잘못된 해제 거절), 불변식 검사기(물리 순회 vs free list vs 비트맵)
 *   Part C  작업량 상한(O(1)을 "센 값"으로 증명), 단편화 통계, 임계 구역을 두른 펌웨어 힙, 100만 회 스트레스
 *
 * 블록 배치 (오프셋은 mem 기준 uint32_t — 32비트 MCU 와 호스트의 레이아웃이 같다):
 *   +0  prev_phys   물리적으로 앞 블록의 오프셋 (맨 앞 블록은 NIL)
 *   +4  size|flags  payload 바이트 수 (8의 배수), bit0 = 이 블록이 free
 *   +8  payload     free 일 때만 앞 8바이트에 next_free / prev_free
 * 힙의 맨 끝 8바이트는 크기 0, busy 인 sentinel 헤더다. 그래서 "다음 블록"은 항상 존재한다.
 *
 * 작업량 카운터 규칙 (t->w, Part C 가 이 값으로 O(1)을 검사한다):
 *   w.bit   비트맵 워드를 탐색하거나 갱신할 때마다 +1
 *           (find_suitable: 워드 하나 볼 때마다 / insert: sl·fl 갱신 +2 / remove: 실제로 비트를 지울 때만 +1씩)
 *   w.list  free list 삽입/제거 1회마다 +1
 *   w.split 분할 1회, w.merge 병합 1회마다 +1
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
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

/* 힙 전체 무결성 검사. 순서를 지켜 첫 위반의 코드를 돌려준다.
 * 1) 물리 순회: 범위·정렬·크기 → TC_WALK, prev_phys → TC_PREV, 연속 free → TC_ADJ, 끝 sentinel → TC_SENTINEL
 * 2) 모든 리스트: 노드가 진짜 블록 시작이고 prev 링크 맞음(사이클 포함) → TC_LINK, free 표시 → TC_NOTFREE,
 *    map_insert(size) == (fl,sl) → TC_CLASS, sl 비트 == 리스트 비어있지 않음 → TC_BITMAP
 * 3) fl 비트 == sl 워드 != 0, 범위 밖 비트 없음 → TC_BITMAP. 물리 free 수 == 리스트 노드 수 → TC_COUNT */
static tcheck_t tlsf_check(const tlsf_t *t) {
    static uint32_t starts[MAX_HEAP / ALIGN_SZ / 32u];
    memset(starts, 0, ((t->size / ALIGN_SZ + 31u) / 32u) * sizeof starts[0]);
    const uint32_t end = t->size - HDR;
    uint32_t b = 0u, prev = NIL, nfree = 0u;
    bool prev_free = false;
    for (;;) {
        if (b > end || (b & (ALIGN_SZ - 1u)) != 0u) return TC_WALK;
        if (b_prev(t, b) != prev) return TC_PREV;
        if (b == end) break;
        uint32_t sz = b_size(t, b);
        if (sz < MIN_PAYLOAD || sz > end - b - HDR) return TC_WALK;
        bool f = b_is_free(t, b);
        if (f && prev_free) return TC_ADJ;
        starts[(b / ALIGN_SZ) / 32u] |= 1u << ((b / ALIGN_SZ) % 32u);
        nfree += f ? 1u : 0u;
        prev = b;
        prev_free = f;
        b += HDR + sz;
    }
    if (b_size(t, end) != 0u || b_is_free(t, end)) return TC_SENTINEL;

    uint32_t nlist = 0u;
    for (uint32_t fl = 0; fl < FL_COUNT; fl++) {
        for (uint32_t sl = 0; sl < SL_COUNT; sl++) {
            uint32_t n = t->head[fl][sl], lprev = NIL, f2, s2;
            if ((((t->sl_bitmap[fl] >> sl) & 1u) != 0u) != (n != NIL)) return TC_BITMAP;
            while (n != NIL) {
                if (n >= end || (n & (ALIGN_SZ - 1u)) != 0u) return TC_LINK;
                if (((starts[(n / ALIGN_SZ) / 32u] >> ((n / ALIGN_SZ) % 32u)) & 1u) == 0u) return TC_LINK;
                if (!b_is_free(t, n)) return TC_NOTFREE;
                map_insert(b_size(t, n), &f2, &s2);
                if (f2 != fl || s2 != sl) return TC_CLASS;
                if (l_prev(t, n) != lprev) return TC_LINK;
                if (++nlist > nfree) return TC_LINK;           /* 사이클 방어 */
                lprev = n;
                n = l_next(t, n);
            }
        }
        if ((t->sl_bitmap[fl] >> SL_COUNT) != 0u) return TC_BITMAP;
        if ((((t->fl_bitmap >> fl) & 1u) != 0u) != (t->sl_bitmap[fl] != 0u)) return TC_BITMAP;
    }
    if ((t->fl_bitmap >> FL_COUNT) != 0u) return TC_BITMAP;
    if (nlist != nfree) return TC_COUNT;
    return TC_OK;
}

/* ======================= Part C — 통계와 펌웨어 힙 ======================= */

/* 물리 순회로 free_bytes(free payload 합), largest, nfree, nbusy, frag = 1 - largest/free_bytes (free 없으면 0) 계산 */
static void tlsf_stats(const tlsf_t *t, tlsf_stats_t *out) {
    memset(out, 0, sizeof *out);
    const uint32_t end = t->size - HDR;
    for (uint32_t b = 0u; b != end; b = b_next(t, b)) {
        if (b_is_free(t, b)) {
            uint32_t sz = b_size(t, b);
            out->free_bytes += sz;
            out->nfree++;
            if (sz > out->largest) out->largest = sz;
        } else {
            out->nbusy++;
        }
    }
    out->frag = out->free_bytes ? 1.0 - (double)out->largest / (double)out->free_bytes : 0.0;
}

/* 호스트 흉내: 인터럽트 잠금. 실제 펌웨어에서는 PRIMASK/BASEPRI 저장·복원 */
static uint32_t g_irq_depth, g_irq_entries;
static MAYBE_UNUSED void irq_lock(void)   { g_irq_depth++; g_irq_entries++; }
static MAYBE_UNUSED void irq_unlock(void) { g_irq_depth--; }

typedef struct {
    tlsf_t     *t;
    uint32_t    used, peak;                         /* 점유 블록 바이트(헤더 포함) 현재/최대 */
    uint32_t    fails, bad_frees;
    tlsf_work_t max_alloc, max_free;                /* 임계 구역 한 번의 최대 작업량 */
} fw_heap_t;

static MAYBE_UNUSED void work_track(tlsf_work_t *max, const tlsf_work_t *before, const tlsf_work_t *now) {
    uint32_t d;
    d = now->bit   - before->bit;   if (d > max->bit)   max->bit   = d;
    d = now->list  - before->list;  if (d > max->list)  max->list  = d;
    d = now->split - before->split; if (d > max->split) max->split = d;
    d = now->merge - before->merge; if (d > max->merge) max->merge = d;
}

/* 임계 구역 안에서 tlsf_malloc. 성공 → used/peak 갱신, 실패 → fails++. 한 번의 작업량을 max_alloc 에 반영 */
static void *fw_alloc(fw_heap_t *h, size_t n) {
    irq_lock();
    tlsf_work_t w0 = h->t->w;
    void *p = tlsf_malloc(h->t, n);
    if (p != NULL) {
        h->used += tlsf_usable(h->t, p) + HDR;
        if (h->used > h->peak) h->peak = h->used;
    } else {
        h->fails++;
    }
    work_track(&h->max_alloc, &w0, &h->t->w);
    irq_unlock();
    return p;
}

/* 임계 구역 안에서 tlsf_free. p==NULL 은 true. 실패하면 bad_frees++ 하고 false — 어느 경로든 lock 을 풀 것 */
static bool fw_free(fw_heap_t *h, void *p) {
    irq_lock();
    tlsf_work_t w0 = h->t->w;
    uint32_t sz;
    tf_err e = tlsf_free(h->t, p, &sz);
    if (e == TF_OK) {
        if (p != NULL) h->used -= sz + HDR;
    } else {
        h->bad_frees++;
    }
    work_track(&h->max_free, &w0, &h->t->w);
    irq_unlock();
    return e == TF_OK;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
static int fails_so_far(void) { return g_total - g_pass; }

static _Alignas(8) uint8_t g_heap[MAX_HEAP];
static _Alignas(8) uint8_t g_snap[MAX_HEAP];
static tlsf_t g_t, g_t_snap;

static uint32_t rng32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static uint32_t class_lo(uint32_t fl, uint32_t sl) {
    return fl == 0u ? sl << ALIGN_LOG2 : (SL_COUNT + sl) << (fl + FL_SHIFT - 1u - SLI);
}
static uint32_t class_width(uint32_t fl) { return fl == 0u ? ALIGN_SZ : 1u << (fl + FL_SHIFT - 1u - SLI); }
static uint32_t blk_of(const tlsf_t *t, const void *p) { return (uint32_t)((const uint8_t *)p - t->mem) - HDR; }

/* 하네스 전용: 병합 없이 busy 블록을 free 표시하고 올바른 리스트에 넣는다 (Part A 가 free 없이 상황을 만들 때) */
static void h_make_free(tlsf_t *t, void *p) {
    uint32_t b = blk_of(t, p), sz = b_size(t, b), fl, sl;
    b_set(t, b, sz, true);
    map_insert(sz, &fl, &sl);
    list_insert(t, b, fl, sl);
}
/* 하네스 전용: 물리 블록 수 세기 (헤더가 깨져도 끝나도록 단계 제한) */
static void h_count(const tlsf_t *t, uint32_t *blocks, uint32_t *nfree) {
    uint32_t b = 0u, end = t->size - HDR, n = 0u, f = 0u, steps = 0u;
    while (b < end && steps++ < t->size) {
        uint32_t sz = b_size(t, b);
        if (sz == 0u) break;
        n++;
        if (b_is_free(t, b)) f++;
        b += HDR + sz;
    }
    *blocks = n;
    *nfree = f;
}
static void h_fill_all(tlsf_t *t) { while (tlsf_malloc(t, 8u) != NULL) { } }

static void test_mapping(void) {
    uint32_t fl, sl;
    map_insert(8u, &fl, &sl);     CHECK(fl == 0u && sl == 1u);
    map_insert(120u, &fl, &sl);   CHECK(fl == 0u && sl == 15u);
    map_insert(128u, &fl, &sl);   CHECK(fl == 1u && sl == 0u);
    map_insert(136u, &fl, &sl);   CHECK(fl == 1u && sl == 1u);
    map_insert(255u, &fl, &sl);   CHECK(fl == 1u && sl == 15u);
    map_insert(256u, &fl, &sl);   CHECK(fl == 2u && sl == 0u);
    map_insert(460u, &fl, &sl);   CHECK(fl == 2u && sl == 12u);      /* 논문 예: f=8, s=12 (SLI=4) */
    map_insert(0x3FFF8u, &fl, &sl); CHECK(fl == 11u && sl == 15u);

    bool ok;
    ok = map_search(8u, &fl, &sl);    CHECK(ok && fl == 0u && sl == 1u);
    ok = map_search(120u, &fl, &sl);  CHECK(ok && fl == 0u && sl == 15u);
    ok = map_search(128u, &fl, &sl);  CHECK(ok && fl == 1u && sl == 0u);
    ok = map_search(256u, &fl, &sl);  CHECK(ok && fl == 2u && sl == 0u);   /* 경계값은 그대로 */
    ok = map_search(264u, &fl, &sl);  CHECK(ok && fl == 2u && sl == 1u);   /* 264 는 [256,272) 안 → 다음 class */
    ok = map_search(504u, &fl, &sl);  CHECK(ok && fl == 3u && sl == 0u);   /* 올림이 FL 경계를 넘는다 */
    ok = map_search(253952u, &fl, &sl); CHECK(ok && fl == 11u && sl == 15u);   /* 표에 들어가는 최대 요청 */
    ok = map_search(253960u, &fl, &sl); CHECK(!ok);
    ok = map_search(MAX_HEAP, &fl, &sl); CHECK(!ok);

    bool ins_ok = true, srch_ok = true, srch_min = true;
    for (uint32_t s = 8u; s < MAX_HEAP; s += 8u) {
        map_insert(s, &fl, &sl);
        if (fl >= FL_COUNT || sl >= SL_COUNT || class_lo(fl, sl) > s || s >= class_lo(fl, sl) + class_width(fl)) ins_ok = false;
    }
    for (uint32_t r = 8u; r <= 253952u; r += 8u) {
        if (!map_search(r, &fl, &sl) || sl >= SL_COUNT || class_lo(fl, sl) < r) { srch_ok = false; continue; }
        if (class_lo(fl, sl) - r >= class_width(fl)) srch_min = false;       /* 너무 많이 올리면 안 된다 */
    }
    CHECK(ins_ok);      /* 모든 8배수 크기: class_lo <= size < class_hi */
    CHECK(srch_ok);     /* 모든 요청: 찾은 class 의 하한 >= 요청 (너무 작은 블록이 나올 수 없다) */
    CHECK(srch_min);    /* 모든 요청: 필요 이상으로 위 class 로 가지 않는다 */
}

static void test_lists_and_find(void) {
    tlsf_t *t = &g_t;
    uint32_t fl, sl, b;
    CHECK(tlsf_init(t, g_heap, 65536u));
    map_insert(65520u, &fl, &sl);
    bool one = t->head[9][15] == 0u && t->fl_bitmap == (1u << 9) && t->sl_bitmap[9] == (1u << 15);
    CHECK(fl == 9u && sl == 15u && one);

    list_insert(t, 1024u, 2u, 5u);
    list_insert(t, 2048u, 2u, 5u);
    CHECK(t->head[2][5] == 2048u && l_next(t, 2048u) == 1024u && l_prev(t, 1024u) == 2048u && l_prev(t, 2048u) == NIL);
    CHECK(t->sl_bitmap[2] == (1u << 5) && (t->fl_bitmap & (1u << 2)) != 0u);

    fl = 1u; sl = 0u; b = find_suitable(t, &fl, &sl);  CHECK(b == 2048u && fl == 2u && sl == 5u);   /* 위 FL 로 점프 */
    fl = 2u; sl = 0u; b = find_suitable(t, &fl, &sl);  CHECK(b == 2048u && fl == 2u && sl == 5u);   /* 같은 FL 안 */
    fl = 2u; sl = 5u; b = find_suitable(t, &fl, &sl);  CHECK(b == 2048u && fl == 2u && sl == 5u);   /* 정확히 그 칸 */
    fl = 2u; sl = 6u; b = find_suitable(t, &fl, &sl);  CHECK(b == 0u && fl == 9u && sl == 15u);     /* 그 FL 을 넘어야 할 때 */
    fl = 10u; sl = 0u; b = find_suitable(t, &fl, &sl); CHECK(b == NIL);

    list_remove(t, 1024u, 2u, 5u);                       /* 꼬리 제거: 비트 유지 */
    CHECK(t->head[2][5] == 2048u && l_next(t, 2048u) == NIL && (t->sl_bitmap[2] & (1u << 5)) != 0u);
    list_remove(t, 2048u, 2u, 5u);                       /* 마지막 제거: 비트 둘 다 꺼짐 */
    CHECK(t->head[2][5] == NIL && t->sl_bitmap[2] == 0u && (t->fl_bitmap & (1u << 2)) == 0u);

    list_insert(t, 1024u, 2u, 5u);
    list_insert(t, 2048u, 2u, 6u);
    list_remove(t, 1024u, 2u, 5u);                       /* 같은 FL 에 다른 SL 이 남음: fl 비트 유지 */
    CHECK(t->sl_bitmap[2] == (1u << 6) && (t->fl_bitmap & (1u << 2)) != 0u);
}

static void test_malloc(void) {
    tlsf_t *t = &g_t;
    uint32_t blocks, nf;
    (void)tlsf_init(t, g_heap, 4096u);
    uint8_t *p = tlsf_malloc(t, 100u);
    CHECK(p == t->mem + 8u && tlsf_usable(t, p) == 104u);
    CHECK(b_is_free(t, 112u) && b_size(t, 112u) == 3968u && b_prev(t, 112u) == 0u && b_prev(t, 4088u) == 112u);
    uint8_t *q = tlsf_malloc(t, 1u);
    CHECK(q == t->mem + 120u && tlsf_usable(t, q) == 8u);
    CHECK(tlsf_malloc(t, 0u) == NULL);
    CHECK(tlsf_malloc(t, SIZE_MAX) == NULL);             /* 올림 overflow */
    CHECK(tlsf_malloc(t, MAX_HEAP + 1u) == NULL);
    CHECK(tlsf_malloc(t, 8192u) == NULL);                /* 4 KB 힙에 없는 크기 */

    /* 쪼갤 수 없는 나머지는 그대로 준다: 64 블록에 56 요청 → 나머지 8 < MIN_BLOCK */
    (void)tlsf_init(t, g_heap, 4096u);
    uint8_t *a = tlsf_malloc(t, 64u), *g = tlsf_malloc(t, 8u);
    CHECK(a != NULL && g != NULL);
    if (a && g) {
        h_make_free(t, a);
        uint8_t *x = tlsf_malloc(t, 56u);
        CHECK(x == a && tlsf_usable(t, x) == 64u && b_prev(t, blk_of(t, g)) == blk_of(t, a));
    }
    /* 나머지가 정확히 MIN_BLOCK 이면 쪼갠다: 72 블록에 56 요청 → 나머지 16 */
    (void)tlsf_init(t, g_heap, 4096u);
    a = tlsf_malloc(t, 72u); g = tlsf_malloc(t, 8u);
    if (a && g) {
        h_make_free(t, a);
        uint8_t *x = tlsf_malloc(t, 56u);
        uint32_t rem = blk_of(t, a) + HDR + 56u;
        CHECK(x == a && tlsf_usable(t, x) == 56u && b_is_free(t, rem) && b_size(t, rem) == 8u && b_prev(t, blk_of(t, g)) == rem);
    } else CHECK(false);

    /* 올림의 핵심: 128 블록은 130 요청을 받을 수 없다. 삽입 매핑을 그대로 쓰면 여기서 128 을 내준다 */
    (void)tlsf_init(t, g_heap, 4096u);
    a = tlsf_malloc(t, 128u); g = tlsf_malloc(t, 8u);
    if (a && g) {
        h_make_free(t, a);
        uint8_t *x = tlsf_malloc(t, 130u);
        CHECK(x != NULL && x != a && tlsf_usable(t, x) == 136u);
        uint8_t *y = tlsf_malloc(t, 128u);
        CHECK(y == a);                                   /* 경계값은 그 class 에서 바로 찾아야 한다 */
    } else CHECK(false);

    /* 소진: 72 B 씩 4088 B 를 쓴다 → 56 개, 남은 48 은 40 요청으로 통째로 */
    (void)tlsf_init(t, g_heap, 4096u);
    uint32_t n = 0u;
    bool ordered = true;
    uint8_t *last = NULL;
    while ((p = tlsf_malloc(t, 64u)) != NULL && n < 1000u) {
        if (last && p != last + 72u) ordered = false;
        if (((uintptr_t)p & 7u) != 0u) ordered = false;
        last = p;
        n++;
    }
    CHECK(n == 56u && ordered);
    p = tlsf_malloc(t, 40u);
    CHECK(p != NULL && tlsf_usable(t, p) == 48u);
    h_count(t, &blocks, &nf);
    CHECK(blocks == 57u && nf == 0u && t->fl_bitmap == 0u);
}

static void test_free_merge(void) {
    tlsf_t *t = &g_t;
    uint32_t b, nb, sz;
    (void)tlsf_init(t, g_heap, 4096u);
    uint8_t *a = tlsf_malloc(t, 64u), *bb = tlsf_malloc(t, 64u), *c = tlsf_malloc(t, 64u), *d = tlsf_malloc(t, 64u), *e = tlsf_malloc(t, 64u);

    CHECK(tlsf_free(t, bb, &sz) == TF_OK && sz == 64u);               /* 양쪽 busy: 병합 없음 */
    h_count(t, &nb, &b);
    CHECK(nb == 6u && b == 2u && tlsf_check(t) == TC_OK);

    CHECK(tlsf_free(t, a, NULL) == TF_OK);                            /* 뒤 이웃과 병합 */
    CHECK(b_size(t, 0u) == 136u && b_is_free(t, 0u) && b_prev(t, blk_of(t, c)) == 0u && tlsf_check(t) == TC_OK);

    CHECK(tlsf_free(t, d, NULL) == TF_OK);                            /* 병합 없음 */
    CHECK(tlsf_check(t) == TC_OK);
    CHECK(tlsf_free(t, c, NULL) == TF_OK);                            /* 앞뒤 모두 병합 */
    CHECK(b_size(t, 0u) == 136u + 8u + 64u + 8u + 64u && b_prev(t, blk_of(t, e)) == 0u && tlsf_check(t) == TC_OK);

    CHECK(tlsf_free(t, e, NULL) == TF_OK);                            /* 끝의 큰 블록까지 합쳐 하나로 */
    h_count(t, &nb, &b);
    CHECK(nb == 1u && b == 1u && b_size(t, 0u) == 4080u && b_prev(t, 4088u) == 0u && tlsf_check(t) == TC_OK);
}

static void test_free_errors(void) {
    tlsf_t *t = &g_t;
    uint32_t sz;
    (void)tlsf_init(t, g_heap, 4096u);
    uint8_t *a = tlsf_malloc(t, 64u), *bb = tlsf_malloc(t, 64u), *c = tlsf_malloc(t, 64u);
    memset(a, 0x5A, 64u);
    CHECK(tlsf_free(t, NULL, &sz) == TF_OK && sz == 0u);
    CHECK(tlsf_free(t, g_heap + 4u, NULL) == TF_RANGE);               /* 첫 payload 보다 앞 */
    CHECK(tlsf_free(t, g_heap + 4096u, NULL) == TF_RANGE);            /* 힙 끝 */
    CHECK(tlsf_free(t, a + 1, NULL) == TF_ALIGN);
    CHECK(tlsf_free(t, a + 8, NULL) == TF_CORRUPT);                   /* 블록 내부 포인터: 가짜 헤더 */

    CHECK(tlsf_free(t, bb, NULL) == TF_OK);
    CHECK(tlsf_free(t, bb, NULL) == TF_DOUBLE);                       /* 양쪽 busy 인 블록의 이중 해제 */
    CHECK(tlsf_check(t) == TC_OK);
    CHECK(tlsf_free(t, a, NULL) == TF_OK);                            /* a 가 bb 와 합쳐짐 */
    CHECK(tlsf_free(t, bb, NULL) != TF_OK);                           /* 병합으로 사라진 블록의 이중 해제 */
    CHECK(tlsf_check(t) == TC_OK);

    /* payload overrun 이 다음 헤더를 덮으면 free 가 발견하고 힙을 건드리지 않는다 */
    (void)tlsf_init(t, g_heap, 4096u);
    a = tlsf_malloc(t, 64u); bb = tlsf_malloc(t, 64u); c = tlsf_malloc(t, 64u);
    memset(a, 0xEE, 64u + 4u);                                        /* bb 의 prev_phys 까지 덮음 */
    uint32_t save_fl = t->fl_bitmap;
    CHECK(tlsf_free(t, a, NULL) == TF_CORRUPT);
    CHECK(tlsf_free(t, bb, NULL) == TF_CORRUPT);
    CHECK(t->fl_bitmap == save_fl && !b_is_free(t, 0u));
    (void)c;
}

static void *g_m[8];

static void build_mixed(tlsf_t *t) {
    (void)tlsf_init(t, g_heap, 16384u);
    static const uint32_t sz[7] = { 64u, 128u, 256u, 512u, 64u, 1024u, 64u };
    for (int i = 0; i < 7; i++) { g_m[i] = tlsf_malloc(t, sz[i]); if (g_m[i]) memset(g_m[i], 0x11 * (i + 1), sz[i]); }
    (void)tlsf_free(t, g_m[1], NULL);
    (void)tlsf_free(t, g_m[3], NULL);
    (void)tlsf_free(t, g_m[5], NULL);
    memcpy(g_snap, g_heap, 16384u);
    g_t_snap = *t;
}
static void restore_mixed(tlsf_t *t) { memcpy(g_heap, g_snap, 16384u); *t = g_t_snap; }

static void test_checker_catches(void) {
    tlsf_t *t = &g_t;
    uint32_t fl, sl;
    build_mixed(t);
    uint32_t a = blk_of(t, g_m[0]), bfree = blk_of(t, g_m[1]), c = blk_of(t, g_m[2]), dfree = blk_of(t, g_m[3]), end = t->size - HDR;
    CHECK(tlsf_check(t) == TC_OK);

    b_set(t, c, b_size(t, c) + 8u, false);                  CHECK(tlsf_check(t) == TC_PREV);     restore_mixed(t);
    b_set(t, c, 0u, false);                                 CHECK(tlsf_check(t) == TC_WALK);     restore_mixed(t);
    b_set_prev(t, c, a);                                    CHECK(tlsf_check(t) == TC_PREV);     restore_mixed(t);
    b_set(t, end, 8u, false);                               CHECK(tlsf_check(t) == TC_SENTINEL); restore_mixed(t);
    b_set(t, c, b_size(t, c), true);                        CHECK(tlsf_check(t) == TC_ADJ);      restore_mixed(t);   /* 병합을 빠뜨린 모양 */
    b_set(t, dfree, b_size(t, dfree), false);               CHECK(tlsf_check(t) == TC_NOTFREE);  restore_mixed(t);

    map_insert(b_size(t, bfree), &fl, &sl);
    list_remove(t, bfree, fl, sl); list_insert(t, bfree, fl, sl + 1u);
    CHECK(tlsf_check(t) == TC_CLASS);                                                            restore_mixed(t);   /* 잘못된 class */

    map_insert(b_size(t, bfree), &fl, &sl);
    t->sl_bitmap[fl] &= ~(1u << sl);                        CHECK(tlsf_check(t) == TC_BITMAP);   restore_mixed(t);   /* 켜져야 할 비트가 꺼짐 */
    t->sl_bitmap[0] |= 1u << 3;                             CHECK(tlsf_check(t) == TC_BITMAP);   restore_mixed(t);   /* 빈 리스트에 비트 */
    t->fl_bitmap ^= 1u;                                     CHECK(tlsf_check(t) == TC_BITMAP);   restore_mixed(t);   /* fl 과 sl 불일치 */

    map_insert(b_size(t, bfree), &fl, &sl);
    l_set_next(t, bfree, bfree);                            CHECK(tlsf_check(t) == TC_LINK);     restore_mixed(t);   /* 사이클 */
    l_set_next(t, bfree, a + 16u);                          CHECK(tlsf_check(t) == TC_LINK);     restore_mixed(t);   /* 블록 시작이 아닌 곳 */

    map_insert(b_size(t, bfree), &fl, &sl);
    list_remove(t, bfree, fl, sl);                          CHECK(tlsf_check(t) == TC_COUNT);    restore_mixed(t);   /* 리스트에서 샌 free 블록 */
    CHECK(tlsf_check(t) == TC_OK);
}

typedef struct { uint8_t *p; uint32_t n; uint8_t tag; } live_t;

static bool pattern_ok(const live_t *l) {
    for (uint32_t i = 0; i < l->n; i++) if (l->p[i] != l->tag) return false;
    return true;
}

/* 작은 힙: 매 연산마다 전체 불변식 + 바이트 단위 소유 맵 */
static void test_random_every_op(void) {
    enum { HB = 8192, OPS = 30000, CAP = 256 };
    static uint8_t owner[HB];
    static live_t live[CAP];
    tlsf_t *t = &g_t;
    uint32_t n = 0u, s = 0x1234ABCDu, allocs = 0u, nulls = 0u;
    bool inv = true, range = true, overlap = true, pat = true, freed = true;
    memset(owner, 0, sizeof owner);
    (void)tlsf_init(t, g_heap, HB);
    for (uint32_t i = 0; i < OPS; i++) {
        if (n == 0u || (n < CAP && (rng32(&s) & 1u))) {
            uint32_t sz = 1u + rng32(&s) % 600u;
            uint8_t *p = tlsf_malloc(t, sz);
            if (p == NULL) { nulls++; }
            else {
                allocs++;
                uint32_t off = (uint32_t)(p - t->mem);
                if (off + sz > HB || (off & 7u) != 0u || tlsf_usable(t, p) < sz) { range = false; break; }
                for (uint32_t j = 0; j < sz; j++) if (owner[off + j]) overlap = false;
                memset(owner + off, 1, sz);
                uint8_t tag = (uint8_t)(1u + i % 251u);
                memset(p, tag, sz);
                live[n++] = (live_t){ p, sz, tag };
            }
        } else {
            uint32_t k = rng32(&s) % n;
            if (!pattern_ok(&live[k])) pat = false;
            memset(owner + (live[k].p - t->mem), 0, live[k].n);
            if (tlsf_free(t, live[k].p, NULL) != TF_OK) freed = false;
            live[k] = live[--n];
        }
        if (tlsf_check(t) != TC_OK) inv = false;
        if (!inv || !overlap || !range) break;
    }
    while (n > 0u) { n--; if (!pattern_ok(&live[n])) pat = false; if (tlsf_free(t, live[n].p, NULL) != TF_OK) freed = false; }
    uint32_t nb, nf;
    h_count(t, &nb, &nf);
    printf("random-every-op: %u allocs, %u NULLs over %u ops (tlsf_check after every op)\n", allocs, nulls, (uint32_t)OPS);
    CHECK(inv);
    CHECK(range);
    CHECK(overlap);
    CHECK(pat);
    CHECK(freed);
    CHECK(nb == 1u && nf == 1u && tlsf_check(t) == TC_OK);          /* 전부 해제하면 블록 하나로 복원 */
    CHECK(nulls > 0u && allocs > 1000u);                            /* 스트레스가 실제로 꽉 찬 상태를 지났다 */
}

/* 작업량 상한이 실제로 도달됨을 보이는 시나리오 (상한이 헐겁지 않다) */
static void test_work_bounds(void) {
    tlsf_t *t = &g_t;
    tlsf_work_t w;
    (void)tlsf_init(t, g_heap, MAX_HEAP);

    /* malloc 최악: 큰 블록 하나뿐일 때 작은 요청 → sl 빈칸, fl 점프, sl, 리스트 비움, 나머지 삽입 */
    w = t->w;
    void *p = tlsf_malloc(t, 40u);
    CHECK(p != NULL);
    CHECK(t->w.bit - w.bit == 7u && t->w.list - w.list == 2u && t->w.split - w.split == 1u && t->w.merge - w.merge == 0u);

    /* free 최악: 앞뒤 이웃이 모두 free 이고 각각 자기 FL 의 유일한 블록 → 비트 지우기 2+2, 삽입 2 */
    (void)tlsf_init(t, g_heap, MAX_HEAP);
    void *a = tlsf_malloc(t, 64u), *b = tlsf_malloc(t, 512u), *c = tlsf_malloc(t, 4096u), *d = tlsf_malloc(t, 8u);
    CHECK(tlsf_free(t, a, NULL) == TF_OK && tlsf_free(t, c, NULL) == TF_OK);
    w = t->w;
    CHECK(tlsf_free(t, b, NULL) == TF_OK);
    CHECK(t->w.bit - w.bit == 6u && t->w.list - w.list == 3u && t->w.merge - w.merge == 2u && t->w.split - w.split == 0u);
    CHECK(tlsf_check(t) == TC_OK);
    (void)d;
}

/* 총량은 충분한데 실패하는 경우: good-fit 의 대가 (Masmano 2004 §8.1 의 구조, 수치는 이 설정 SLI=4) */
static void test_fits_but_fails(void) {
    tlsf_t *t = &g_t;
    tlsf_stats_t st;
    (void)tlsf_init(t, g_heap, 16384u);
    void *big = tlsf_malloc(t, 5880u);
    CHECK(big != NULL);
    h_fill_all(t);
    CHECK(tlsf_free(t, big, NULL) == TF_OK);
    tlsf_stats(t, &st);
    CHECK(st.free_bytes == 5880u && st.largest == 5880u && st.nfree == 1u);
    CHECK(tlsf_malloc(t, 5640u) == NULL);             /* 5880 >= 5640 인데 실패: 5640 은 [5888,..) 부터 찾는다 */
    void *ok = tlsf_malloc(t, 5632u);                 /* class 하한은 받아 준다 */
    CHECK(ok == big && tlsf_usable(t, ok) == 5632u);
    tlsf_stats(t, &st);
    CHECK(st.free_bytes == 240u && st.largest == 240u && st.frag == 0.0);
}

static void test_stats_and_fw(void) {
    tlsf_t *t = &g_t;
    tlsf_stats_t st;
    build_mixed(t);
    tlsf_stats(t, &st);
    uint32_t tail = 16384u - 16u - (64u + 128u + 256u + 512u + 64u + 1024u + 64u) - 7u * HDR;
    CHECK(st.nfree == 4u && st.nbusy == 4u);
    CHECK(st.free_bytes == 128u + 512u + 1024u + tail && st.largest == tail);
    CHECK(st.frag == 1.0 - (double)tail / (double)st.free_bytes && st.frag > 0.104 && st.frag < 0.105);

    fw_heap_t h;
    memset(&h, 0, sizeof h);
    h.t = t;
    (void)tlsf_init(t, g_heap, 4096u);
    g_irq_depth = 0u;
    g_irq_entries = 0u;
    void *p = fw_alloc(&h, 100u), *q = fw_alloc(&h, 200u);
    CHECK(p != NULL && q != NULL && h.used == 104u + 8u + 200u + 8u && h.peak == h.used);
    CHECK(fw_alloc(&h, 100000u) == NULL && h.fails == 1u && g_irq_depth == 0u);       /* 실패 경로도 lock 해제 */
    CHECK(fw_free(&h, p) && h.used == 208u && h.peak == 320u);
    CHECK(!fw_free(&h, (uint8_t *)q + 1) && h.bad_frees == 1u && g_irq_depth == 0u);  /* 실패 경로도 lock 해제 */
    CHECK(fw_free(&h, NULL) && h.bad_frees == 1u);
    CHECK(fw_free(&h, q) && h.used == 0u && g_irq_depth == 0u && g_irq_entries == 7u);
}

static int cmp_live(const void *x, const void *y) {
    const uint8_t *a = ((const live_t *)x)->p, *b = ((const live_t *)y)->p;
    return a < b ? -1 : a > b;
}

static uint32_t rand_fw_size(uint32_t *s) {
    uint32_t r = rng32(s) % 100u;
    if (r < 70u) return 8u + rng32(s) % 89u;            /* 8..96: 메시지 헤더, 작은 제어 객체 */
    if (r < 95u) return 128u + rng32(s) % 897u;         /* 128..1024: 패킷 */
    return 2048u + rng32(s) % 6145u;                    /* 2048..8192: 큰 프레임 */
}

#define STRESS_OPS 1000000u
#define STRESS_K   4096u
#define LIVE_CAP   4096u

/* 큰 힙: 연산 STRESS_OPS 번. 전체 불변식은 STRESS_K 연산마다 + 끝에서 */
static void test_stress_fw(void) {
    static live_t live[LIVE_CAP];
    tlsf_t *t = &g_t;
    fw_heap_t h;
    tlsf_stats_t st, worst;
    uint32_t n = 0u, s = 0x9E3779B9u, allocs = 0u, frees = 0u;
    bool inv = true, shape = true, overlap = true, pat = true, freed = true, irq = true;
    memset(&h, 0, sizeof h);
    memset(&worst, 0, sizeof worst);
    h.t = t;
    g_irq_depth = 0u;
    (void)tlsf_init(t, g_heap, MAX_HEAP);

    for (uint32_t i = 0; i < STRESS_OPS; i++) {
        uint32_t want = (h.used < MAX_HEAP / 10u * 9u) ? 60u : 30u;
        if (n == 0u || (n < LIVE_CAP && rng32(&s) % 100u < want)) {
            uint32_t sz = rand_fw_size(&s);
            uint8_t *p = fw_alloc(&h, sz);
            if (p != NULL) {
                allocs++;
                if (p < t->mem || p + sz > t->mem + t->size || ((uintptr_t)p & 7u) != 0u || tlsf_usable(t, p) < sz) shape = false;
                uint8_t tag = (uint8_t)(1u + i % 251u);
                memset(p, tag, sz);
                live[n++] = (live_t){ p, sz, tag };
            }
        } else {
            uint32_t k = rng32(&s) % n;
            if (!pattern_ok(&live[k])) pat = false;
            if (!fw_free(&h, live[k].p)) freed = false;
            frees++;
            live[k] = live[--n];
        }
        if (g_irq_depth != 0u) irq = false;
        if ((i + 1u) % STRESS_K == 0u) {
            if (tlsf_check(t) != TC_OK) { inv = false; break; }
            qsort(live, n, sizeof live[0], cmp_live);
            for (uint32_t j = 1; j < n; j++)
                if (live[j - 1].p + tlsf_usable(t, live[j - 1].p) > live[j].p) overlap = false;
            tlsf_stats(t, &st);
            if (st.frag > worst.frag) worst = st;
        }
    }
    uint32_t live_end = n;
    tlsf_stats(t, &st);
    printf("stress: %u ops, allocs %u, frees %u, NULL %u, peak block bytes %u of %u\n",
           STRESS_OPS, allocs, frees, h.fails, h.peak, MAX_HEAP);
    printf("stress: worst external frag at a checkpoint %.3f (free %u, largest %u, free blocks %u)\n",
           worst.frag, worst.free_bytes, worst.largest, worst.nfree);
    printf("stress: at end, live %u, free %u, largest %u, free blocks %u, frag %.3f\n",
           live_end, st.free_bytes, st.largest, st.nfree, st.frag);
    printf("stress: max work per malloc bit=%u list=%u split=%u merge=%u; per free bit=%u list=%u split=%u merge=%u\n",
           h.max_alloc.bit, h.max_alloc.list, h.max_alloc.split, h.max_alloc.merge,
           h.max_free.bit, h.max_free.list, h.max_free.split, h.max_free.merge);

    while (n > 0u) { n--; if (!pattern_ok(&live[n])) pat = false; if (!fw_free(&h, live[n].p)) freed = false; }
    uint32_t nb, nf;
    h_count(t, &nb, &nf);
    CHECK(inv);
    CHECK(shape);
    CHECK(overlap);
    CHECK(pat);
    CHECK(freed && h.bad_frees == 0u && irq);
    CHECK(h.max_alloc.bit <= 7u && h.max_alloc.list <= 2u && h.max_alloc.split <= 1u && h.max_alloc.merge == 0u);
    CHECK(h.max_free.bit <= 6u && h.max_free.list <= 3u && h.max_free.split == 0u && h.max_free.merge <= 2u);
    CHECK(h.used == 0u && nb == 1u && nf == 1u && tlsf_check(t) == TC_OK);
    CHECK(b_size(t, 0u) == MAX_HEAP - 16u);
    CHECK(h.fails > 0u);                                /* 스트레스가 실제로 힙을 소진했다 */
}

int main(void) {
    printf("sizeof(tlsf_t) = %zu bytes, %u lists\n", sizeof(tlsf_t), FL_COUNT * SL_COUNT);

    puts("== Part A: mapping / bitmaps / malloc");
    int f0 = fails_so_far();
    test_mapping();
    test_lists_and_find();
    test_malloc();
    bool a_ok = fails_so_far() == f0;

    bool b_ok = false;
    puts("== Part B: free / merge / invariant checker");
    if (a_ok) {
        f0 = fails_so_far();
        test_free_merge();
        test_free_errors();
        test_checker_catches();
        test_random_every_op();
        b_ok = fails_so_far() == f0;
    } else {
        puts("SKIP Part B: Part A failed");
    }

    puts("== Part C: work bounds / fragmentation / firmware heap");
    if (a_ok && b_ok) {
        test_work_bounds();
        test_fits_but_fails();
        test_stats_and_fw();
        test_stress_fw();
    } else {
        puts("SKIP Part C: Part A/B failed");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
