/* DS-A06 demo_bench.c — TLSF vs first-fit vs 고정 풀 vs 시스템 malloc (malloc 은 여기서만 사용)
 *
 * 같은 난수 요청열(슬롯 800개, 슬롯이 비었으면 alloc / 차 있으면 free)을 각 할당기에 돌린다.
 *   크기 분포: 70% 8..96, 25% 128..1024, 5% 2048..8192
 *   힙: TLSF / first-fit 은 256 KB. 풀은 같은 256 KB 를 정적으로 나눔. 시스템 malloc 은 제한 없음.
 * 보고: 연산별 시간 분포(cntvct_el0: 값의 단위는 1/cntfrq = 1 ns 이지만 실제로는 41.67 ns 씩 뛴다 — 양자화 주의,
 *       타이머 자체 비용 포함), 타이머 없는 평균 ns/op, 작업량, 단편화, 실패 수.
 *
 * build: cc -std=gnu11 -O2 demo_bench.c -o db && ./db   (cntvct_el0 읽기에 asm, 집계에 statement expression 사용)
 */
#define _DARWIN_C_SOURCE
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


#include <malloc/malloc.h>
#include <mach/mach_time.h>
#include <pthread.h>

#ifndef __aarch64__
#error "demo_bench reads cntvct_el0; arm64 only"
#endif

#define HEAP_BYTES MAX_HEAP
#define OPS        1000000u
#define SLOTS      800u
#define RUNS       5
#define WARMUP     100000u
#define SAMPLE     8192u

enum { A_TLSF, A_FIRSTFIT, A_POOL1, A_POOL3, A_SYS, A_N };
static const char *const NAMES[A_N] = { "TLSF", "first-fit", "pool-1class", "pool-3class", "sys malloc" };

static _Alignas(8) uint8_t g_mem[HEAP_BYTES];
static tlsf_t g_t;

static inline uint64_t ticks(void) { uint64_t t; __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : [v] "=r"(t)); return t; }
static uint32_t rng32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static uint32_t rand_size(uint32_t *s) {
    uint32_t r = rng32(s) % 100u;
    if (r < 70u) return 8u + rng32(s) % 89u;
    if (r < 95u) return 128u + rng32(s) % 897u;
    return 2048u + rng32(s) % 6145u;
}

/* ---------- first-fit: 주소순 free list + 병합 (FreeRTOS heap_4 와 같은 계열) ---------- */
typedef struct { uint32_t size, next; } ff_hdr;
#define FF_HDR 8u
#define FF_NIL NIL
static uint32_t g_ff_head;
static uint64_t g_ff_steps;
static uint32_t g_ff_steps_last;

static inline ff_hdr ff_get(uint32_t o) { ff_hdr h; memcpy(&h, g_mem + o, sizeof h); return h; }
static inline void   ff_put(uint32_t o, ff_hdr h) { memcpy(g_mem + o, &h, sizeof h); }

static void ff_init(void) {
    ff_hdr h = { HEAP_BYTES, FF_NIL };
    ff_put(0u, h);
    g_ff_head = 0u;
    g_ff_steps = 0u;
}
static void *ff_malloc(uint32_t n) {
    uint32_t need = ((n + 7u) & ~7u) + FF_HDR, prev = FF_NIL, b = g_ff_head, steps = 0u;
    while (b != FF_NIL) {
        steps++;
        ff_hdr h = ff_get(b);
        if (h.size >= need) {
            uint32_t nx = h.next;
            if (h.size - need >= 16u) {
                uint32_t r = b + need;
                ff_put(r, (ff_hdr){ h.size - need, h.next });
                nx = r;
                h.size = need;
            }
            if (prev == FF_NIL) g_ff_head = nx;
            else { ff_hdr ph = ff_get(prev); ph.next = nx; ff_put(prev, ph); }
            ff_put(b, (ff_hdr){ h.size, 0u });
            g_ff_steps += steps;
            g_ff_steps_last = steps;
            return g_mem + b + FF_HDR;
        }
        prev = b;
        b = h.next;
    }
    g_ff_steps += steps;
    g_ff_steps_last = steps;
    return NULL;
}
static void ff_free(void *p) {
    uint32_t b = (uint32_t)((uint8_t *)p - g_mem) - FF_HDR, prev = FF_NIL, cur = g_ff_head, steps = 0u;
    ff_hdr h = ff_get(b);
    while (cur != FF_NIL && cur < b) { steps++; prev = cur; cur = ff_get(cur).next; }
    h.next = cur;
    if (cur != FF_NIL && b + h.size == cur) { ff_hdr c = ff_get(cur); h.size += c.size; h.next = c.next; }
    ff_put(b, h);
    if (prev == FF_NIL) g_ff_head = b;
    else {
        ff_hdr ph = ff_get(prev);
        if (prev + ph.size == b) { ph.size += h.size; ph.next = h.next; }
        else ph.next = b;
        ff_put(prev, ph);
    }
    g_ff_steps += steps;
    g_ff_steps_last = steps;
}
static void ff_stats(uint32_t *free_b, uint32_t *largest, uint32_t *n) {
    *free_b = 0u; *largest = 0u; *n = 0u;
    for (uint32_t b = g_ff_head; b != FF_NIL; ) { ff_hdr h = ff_get(b); *free_b += h.size - FF_HDR; if (h.size - FF_HDR > *largest) *largest = h.size - FF_HDR; (*n)++; b = h.next; }
}

/* ---------- 고정 풀: LIFO 스택 (DS-M02) ---------- */
typedef struct { uint32_t block, count, base, head; } pool_cls;
static pool_cls g_cls[3];
static uint32_t g_ncls;
#define POOL_NIL NIL
static uint32_t pool_rd(uint32_t o) { uint32_t v; memcpy(&v, g_mem + o, 4); return v; }
static void pool_wr(uint32_t o, uint32_t v) { memcpy(g_mem + o, &v, 4); }
static void pool_setup(uint32_t n, const uint32_t *block, const uint32_t *count) {
    uint32_t base = 0u;
    g_ncls = n;
    for (uint32_t i = 0; i < n; i++) {
        g_cls[i] = (pool_cls){ block[i], count[i], base, POOL_NIL };
        for (uint32_t k = count[i]; k > 0u; k--) {
            uint32_t o = base + (k - 1u) * block[i];
            pool_wr(o, g_cls[i].head);
            g_cls[i].head = o;
        }
        base += count[i] * block[i];
    }
    if (base > HEAP_BYTES) { fprintf(stderr, "pool too big\n"); exit(2); }
}
static void *pool_alloc(uint32_t n, uint32_t *held) {
    for (uint32_t i = 0; i < g_ncls; i++) {
        if (n <= g_cls[i].block) {
            uint32_t o = g_cls[i].head;
            if (o == POOL_NIL) return NULL;           /* 더 큰 class 로 넘기지 않는다: 풀의 전형적 동작 */
            g_cls[i].head = pool_rd(o);
            *held = g_cls[i].block;
            return g_mem + o;
        }
    }
    return NULL;
}
static void pool_free(void *p) {
    uint32_t o = (uint32_t)((uint8_t *)p - g_mem);
    for (uint32_t i = 0; i < g_ncls; i++) {
        if (o >= g_cls[i].base && o < g_cls[i].base + g_cls[i].count * g_cls[i].block) {
            pool_wr(o, g_cls[i].head);
            g_cls[i].head = o;
            return;
        }
    }
}

/* ---------- 측정 ---------- */
typedef struct { void *p; uint32_t n; uint32_t held; } slot_t;
static slot_t g_slots[SLOTS];
static uint32_t g_ta[OPS], g_tf[OPS];

typedef struct {
    double a_p50, a_p99, a_p999, a_max, f_p50, f_p99, f_p999, f_max;
    uint32_t attempts, fails, fails_large;
    double frag_ext_avg, frag_ext_max, frag_int_avg;
    uint64_t work_max_alloc, work_max_free;
    double work_avg_alloc, work_avg_free;
    uint32_t free_blocks_max;
} result_t;

static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }
static double pct(uint32_t *v, uint32_t n, double p, double ns) {
    if (n == 0u) return 0.0;
    uint32_t i = (uint32_t)(p * (double)(n - 1u) + 0.5);
    return (double)v[i] * ns;
}

static result_t run_one(int kind, double ns_per_tick, bool timed) {
    result_t res;
    memset(&res, 0, sizeof res);
    memset(g_slots, 0, sizeof g_slots);
    static const uint32_t b1[1] = { 8192u }, c1[1] = { 32u };
    static const uint32_t b3[3] = { 128u, 1024u, 8192u }, c3[3] = { 209u, 84u, 18u };
    switch (kind) {
    case A_TLSF:     (void)tlsf_init(&g_t, g_mem, HEAP_BYTES); break;
    case A_FIRSTFIT: ff_init(); break;
    case A_POOL1:    pool_setup(1u, b1, c1); break;
    case A_POOL3:    pool_setup(3u, b3, c3); break;
    default: break;
    }
    uint32_t s = 0x2545F491u, na = 0u, nf = 0u, samples = 0u;
    uint64_t live_req = 0u, live_held = 0u, wsum_a = 0u, wsum_f = 0u;
    double ext_sum = 0.0, int_sum = 0.0;

    for (uint32_t i = 0; i < OPS; i++) {
        uint32_t k = rng32(&s) % SLOTS;
        uint32_t sz = rand_size(&s);
        slot_t *sl = &g_slots[k];
        if (sl->p == NULL) {
            uint32_t held = 0u;
            tlsf_work_t w0 = g_t.w;
            uint64_t t0 = timed ? ticks() : 0u;
            void *p = NULL;
            switch (kind) {
            case A_TLSF:     p = tlsf_malloc(&g_t, sz); break;
            case A_FIRSTFIT: p = ff_malloc(sz); break;
            case A_POOL1:
            case A_POOL3:    p = pool_alloc(sz, &held); break;
            default:         p = malloc(sz); break;
            }
            uint64_t t1 = timed ? ticks() : 0u;
            uint64_t dt = t1 - t0;
            g_ta[na++] = (uint32_t)dt;
            uint64_t w = 0u;
            if (kind == A_TLSF) { w = (g_t.w.bit - w0.bit) + (g_t.w.list - w0.list) + (g_t.w.split - w0.split) + (g_t.w.merge - w0.merge); }
            else if (kind == A_FIRSTFIT) { w = g_ff_steps_last; }
            if (w > res.work_max_alloc) res.work_max_alloc = w;
            wsum_a += w;
            res.attempts++;
            if (p == NULL) {
                res.fails++;
                if (sz >= 2048u) res.fails_large++;
                continue;
            }
            if (kind == A_TLSF) held = tlsf_usable(&g_t, p) + HDR;
            else if (kind == A_FIRSTFIT) held = ff_get((uint32_t)((uint8_t *)p - g_mem) - FF_HDR).size;
            else if (kind == A_SYS) held = (uint32_t)malloc_size(p);
            if (timed) memset(p, 0xA5, sz);
            *sl = (slot_t){ p, sz, held };
            live_req += sz;
            live_held += held;
        } else {
            void *p = sl->p;
            live_req -= sl->n;
            live_held -= sl->held;
            tlsf_work_t w0 = g_t.w;
            uint64_t t0 = timed ? ticks() : 0u;
            switch (kind) {
            case A_TLSF:     (void)tlsf_free(&g_t, p, NULL); break;
            case A_FIRSTFIT: ff_free(p); break;
            case A_POOL1:
            case A_POOL3:    pool_free(p); break;
            default:         free(p); break;
            }
            uint64_t t1 = timed ? ticks() : 0u;
            uint64_t dt = t1 - t0;
            g_tf[nf++] = (uint32_t)dt;
            uint64_t w = 0u;
            if (kind == A_TLSF) { w = (g_t.w.bit - w0.bit) + (g_t.w.list - w0.list) + (g_t.w.split - w0.split) + (g_t.w.merge - w0.merge); }
            else if (kind == A_FIRSTFIT) { w = g_ff_steps_last; }
            if (w > res.work_max_free) res.work_max_free = w;
            wsum_f += w;
            sl->p = NULL;
        }
        if (i >= WARMUP && (i % SAMPLE) == 0u) {
            uint32_t fb = 0u, lg = 0u, cnt = 0u;
            if (kind == A_TLSF) {
                for (uint32_t b = 0u; b != g_t.size - HDR; b = b_next(&g_t, b)) {
                    if (b_is_free(&g_t, b)) { fb += b_size(&g_t, b); cnt++; if (b_size(&g_t, b) > lg) lg = b_size(&g_t, b); }
                }
            } else if (kind == A_FIRSTFIT) {
                ff_stats(&fb, &lg, &cnt);
            }
            double ext = fb ? 1.0 - (double)lg / (double)fb : 0.0;
            double intern = live_held ? 1.0 - (double)live_req / (double)live_held : 0.0;
            ext_sum += ext;
            int_sum += intern;
            if (ext > res.frag_ext_max) res.frag_ext_max = ext;
            if (cnt > res.free_blocks_max) res.free_blocks_max = cnt;
            samples++;
        }
    }
    for (uint32_t i = 0; i < SLOTS; i++) {
        if (g_slots[i].p == NULL) continue;
        switch (kind) {
        case A_TLSF:     (void)tlsf_free(&g_t, g_slots[i].p, NULL); break;
        case A_FIRSTFIT: ff_free(g_slots[i].p); break;
        case A_POOL1:
        case A_POOL3:    pool_free(g_slots[i].p); break;
        default:         free(g_slots[i].p); break;
        }
    }
    qsort(g_ta, na, sizeof g_ta[0], cmp_u32);
    qsort(g_tf, nf, sizeof g_tf[0], cmp_u32);
    res.a_p50 = pct(g_ta, na, 0.50, ns_per_tick); res.a_p99 = pct(g_ta, na, 0.99, ns_per_tick);
    res.a_p999 = pct(g_ta, na, 0.999, ns_per_tick); res.a_max = pct(g_ta, na, 1.0, ns_per_tick);
    res.f_p50 = pct(g_tf, nf, 0.50, ns_per_tick); res.f_p99 = pct(g_tf, nf, 0.99, ns_per_tick);
    res.f_p999 = pct(g_tf, nf, 0.999, ns_per_tick); res.f_max = pct(g_tf, nf, 1.0, ns_per_tick);
    res.work_avg_alloc = na ? (double)wsum_a / (double)na : 0.0;
    res.work_avg_free = nf ? (double)wsum_f / (double)nf : 0.0;
    if (samples) { res.frag_ext_avg = ext_sum / samples; res.frag_int_avg = int_sum / samples; }
    return res;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
#define MED(field) ({ double v_[RUNS]; for (int r_ = 0; r_ < RUNS; r_++) v_[r_] = R[k][r_].field; qsort(v_, RUNS, sizeof v_[0], cmp_d); v_[RUNS / 2]; })
#define MINV(field) ({ double m_ = R[k][0].field; for (int r_ = 1; r_ < RUNS; r_++) if (R[k][r_].field < m_) m_ = R[k][r_].field; m_; })
#define MAXV(field) ({ double m_ = R[k][0].field; for (int r_ = 1; r_ < RUNS; r_++) if (R[k][r_].field > m_) m_ = R[k][r_].field; m_; })

int main(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : [v] "=r"(cntfrq));
    double ns = 1e9 / (double)cntfrq;                        /* cntvct_el0 값 1 단위 = ns (이 맥은 cntfrq 1 GHz) */
    printf("cntfrq_el0 = %llu Hz, mach timebase %u/%u = %.2f ns (mach tick); cntvct_el0 는 실제로 약 41.67 ns 단위로 증가\n",
           (unsigned long long)cntfrq, tb.numer, tb.denom, (double)tb.numer / (double)tb.denom);
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    /* 타이머 자체 비용 */
    uint32_t emp[100000];
    for (uint32_t i = 0; i < 100000u; i++) { uint64_t a = ticks(); uint64_t b = ticks(); emp[i] = (uint32_t)(b - a); }
    uint64_t tot = 0u; uint32_t emax = 0u;
    for (uint32_t i = 0; i < 100000u; i++) { tot += emp[i]; if (emp[i] > emax) emax = emp[i]; }
    printf("tick = %.2f ns. empty timer pair: avg %.2f ticks = %.1f ns (max %u ticks), included in every timing below\n",
           ns, (double)tot / 100000.0, ns * (double)tot / 100000.0, emax);
    printf("ops per run %u, slots %u, runs %d, heap %u KB\n\n", OPS, SLOTS, RUNS, HEAP_BYTES / 1024u);

    static result_t R[A_N][RUNS];
    for (int k = 0; k < A_N; k++)
        for (int r = 0; r < RUNS; r++) R[k][r] = run_one(k, ns, true);

    /* 타이머 없는 평균: 전체 시간 / 연산 수. 타이머 오버헤드와 페이로드 memset 을 뺀 할당기 비용 */
    double mean_ns[A_N][RUNS];
    for (int k = 0; k < A_N; k++)
        for (int r = 0; r < RUNS; r++) {
            uint64_t m0 = mach_absolute_time();
            (void)run_one(k, ns, false);
            uint64_t m1 = mach_absolute_time();
            mean_ns[k][r] = (double)(m1 - m0) * (double)tb.numer / (double)tb.denom / (double)OPS;
        }

    printf("mean ns per op, no per-op timers, no payload memset (median of %d runs, min..max)\n", RUNS);
    for (int k = 0; k < A_N; k++) {
        double v[RUNS];
        for (int r = 0; r < RUNS; r++) v[r] = mean_ns[k][r];
        qsort(v, RUNS, sizeof v[0], cmp_d);
        printf("%-12s %7.1f  (%.1f..%.1f)\n", NAMES[k], v[RUNS / 2], v[0], v[RUNS - 1]);
    }
    puts("");

    printf("wall-clock per op, ns (median of %d runs; each cell includes the timer pair)\n", RUNS);
    printf("%-12s | alloc: p50  p99   p99.9   max (run min..max)     | free: p50  p99   p99.9   max (run min..max)\n", "allocator");
    for (int k = 0; k < A_N; k++)
        printf("%-12s | %8.0f %5.0f %7.0f %6.0f (%6.0f..%6.0f) | %8.0f %5.0f %7.0f %6.0f (%6.0f..%6.0f)\n", NAMES[k],
               MED(a_p50), MED(a_p99), MED(a_p999), MED(a_max), MINV(a_max), MAXV(a_max),
               MED(f_p50), MED(f_p99), MED(f_p999), MED(f_max), MINV(f_max), MAXV(f_max));

    printf("\nwork per op (deterministic: TLSF = bit+list+split+merge, first-fit = free-list nodes visited)\n");
    for (int k = 0; k < 2; k++)
        printf("%-12s | alloc avg %7.2f max %6llu | free avg %7.2f max %6llu | free blocks max %u\n", NAMES[k],
               R[k][0].work_avg_alloc, (unsigned long long)R[k][0].work_max_alloc,
               R[k][0].work_avg_free, (unsigned long long)R[k][0].work_max_free, R[k][0].free_blocks_max);

    printf("\nfragmentation and failures (same in every run: the request stream is fixed)\n");
    printf("%-12s | attempts   NULL   (large) | external avg/max (1-largest/free) | internal avg (held-req)/held\n", "allocator");
    for (int k = 0; k < A_N; k++) {
        result_t *r = &R[k][0];
        if (k == A_POOL1 || k == A_POOL3)
            printf("%-12s | %8u %6u %7u | %16s                  | %5.3f\n", NAMES[k], r->attempts, r->fails, r->fails_large, "(n/a: class only)", r->frag_int_avg);
        else if (k == A_SYS)
            printf("%-12s | %8u %6u %7u | %16s                  | %5.3f (malloc_size)\n", NAMES[k], r->attempts, r->fails, r->fails_large, "(n/a: opaque)", r->frag_int_avg);
        else
            printf("%-12s | %8u %6u %7u | %8.3f / %6.3f                 | %5.3f\n", NAMES[k], r->attempts, r->fails, r->fails_large, r->frag_ext_avg, r->frag_ext_max, r->frag_int_avg);
    }
    return 0;
}
