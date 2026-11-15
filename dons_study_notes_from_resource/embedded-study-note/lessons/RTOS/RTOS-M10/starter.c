/* RTOS-M10 RTOS 힙 전략: heap_1~heap_5 비교와 heap_4 coalescing
 *
 * 호스트 모델: FreeRTOS V11.3.2 의 heap_4.c / heap_2.c 를 "같은 크기의 정적 아레나" 위에 다시 만든다.
 *  - 아레나 = uint8_t 배열 (FreeRTOS 의 ucHeap). 블록 헤더(BlockLink_t)는 블록 맨 앞, 아레나 안에 있다.
 *  - 포인터 대신 "정렬된 시작 기준 오프셋(uint32_t)" 으로 연결한다. 헤더 앞 8 B = { next, size },
 *    size 의 MSB = 할당됨 (heapBLOCK_ALLOCATED_BITMASK). 읽기/쓰기는 memcpy → 정렬·aliasing UB 없음.
 *  - 헤더 크기 hdr 는 실행 시 인자: Cortex-M(포인터 4 B + size_t 4 B = 8 B), 64비트 호스트(16 B).
 *    demo_freertos_heap.c 가 hdr=16 으로 진짜 FreeRTOS(POSIX 포트)와 바이트 단위로 비교한다.
 *  - 크기 규칙: wanted = 요청 + hdr 을 8 의 배수로 올림. 남는 조각 > 2*hdr 일 때만 쪼갠다.
 *
 * 이 레슨에서 채우는 것 (TODO 1~5):
 *  1. h4_malloc()      : 주소순 free list 에서 first fit, 쪼개기, free/min-ever 갱신 (heap_4.c:173–351)
 *  2. h4_insert_free() : 주소순 자리에 넣으면서 앞·뒤 블록과 병합 (prvInsertBlockIntoFreeList, heap_4.c:504–569)
 *  3. h2_malloc() / h2_insert_free() : 크기순 free list = best fit, 병합 없음 (heap_2.c:135–153, 156–292)
 *  4. heap_get_stats() : vPortGetHeapStats 의 7개 값 (heap_4.c:572–621)
 *  5. heap_health()    : 펌웨어 힙 감시 — 워터마크 · 단편화 경보 (Part C)
 * 그 밖의 것(초기화, free 검증, 불변식 검사, 워크로드, 테스트)은 주어진 코드다.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ALIGN       8u                  /* portBYTE_ALIGNMENT (ARM_CM4F, Posix 둘 다 8) */
#define ALIGN_MASK  (ALIGN - 1u)
#define NIL         UINT32_MAX          /* NULL / xStart 대신 */
#define ALLOC_BIT   0x80000000u         /* heapBLOCK_ALLOCATED_BITMASK (32비트 size 의 MSB) */
#define HDR_CM4     8u
#define HDR_HOST64  16u
#define ARENA_MAX   65536u

typedef enum { HEAP4, HEAP2 } heap_kind_t;

typedef struct {
    heap_kind_t kind;
    uint8_t *base;          /* 정렬된 시작 = 첫 블록 */
    uint32_t hdr;           /* xHeapStructSize */
    uint32_t span;          /* heap_4: 끝 표시(pxEnd) 오프셋 / heap_2: configADJUSTED_HEAP_SIZE */
    uint32_t start_next;    /* xStart.pxNextFreeBlock */
    uint32_t free_bytes;    /* xFreeBytesRemaining */
    uint32_t min_ever;      /* xMinimumEverFreeBytesRemaining (heap_2 원본엔 없음 — 모델만) */
    uint32_t n_alloc, n_free;
    uint32_t walk, max_walk;  /* 이번/최대 free list 방문 노드 수 = 실행 시간 지표 */
} heap_t;

typedef struct {            /* HeapStats_t (portable.h:156–165) */
    uint32_t available, largest, smallest, n_blocks, min_ever, n_alloc, n_free;
} heap_stats_t;

/* ---------------- 주어진 코드: 헤더 접근 ---------------- */
static uint32_t rd32(const heap_t *h, uint32_t off, uint32_t field) {
    uint32_t v; memcpy(&v, h->base + off + 4u * field, 4); return v;
}
static void wr32(heap_t *h, uint32_t off, uint32_t field, uint32_t v) { memcpy(h->base + off + 4u * field, &v, 4); }
static uint32_t blk_next(const heap_t *h, uint32_t b) { return rd32(h, b, 0); }
static uint32_t blk_raw(const heap_t *h, uint32_t b)  { return rd32(h, b, 1); }
static uint32_t blk_size(const heap_t *h, uint32_t b) { return rd32(h, b, 1) & ~ALLOC_BIT; }
static void set_next(heap_t *h, uint32_t b, uint32_t v) { wr32(h, b, 0, v); }
static void set_size(heap_t *h, uint32_t b, uint32_t v) { wr32(h, b, 1, v); }
/* "앞 노드" 가 NIL 이면 xStart. FreeRTOS 의 pxPreviousBlock = &xStart 와 같다. */
static uint32_t get_link(const heap_t *h, uint32_t prev) { return prev == NIL ? h->start_next : blk_next(h, prev); }
static void set_link(heap_t *h, uint32_t prev, uint32_t v) { if (prev == NIL) h->start_next = v; else set_next(h, prev, v); }

/* 요청 n → 블록 크기. 0 = 불가 (n==0 또는 오버플로) — heap_4.c:182–215 */
static uint32_t wanted_size(const heap_t *h, size_t n) {
    if (n == 0 || n > (size_t)(0x7FFFFFFFu - h->hdr - ALIGN)) return 0;
    uint32_t w = (uint32_t)n + h->hdr;
    if (w & ALIGN_MASK) w += ALIGN - (w & ALIGN_MASK);
    return w;
}

static void h4_init(heap_t *h, uint8_t *mem, uint32_t total, uint32_t hdr) {     /* prvHeapInit, heap_4.c:456–501 */
    memset(h, 0, sizeof *h);
    h->kind = HEAP4; h->hdr = hdr;
    uint32_t adj = (uint32_t)((ALIGN - ((uintptr_t)mem & ALIGN_MASK)) & ALIGN_MASK);
    h->base = mem + adj; total -= adj;
    uint32_t end = (total - hdr) & ~ALIGN_MASK;       /* pxEnd 는 아레나 끝 "안쪽" 에 hdr 바이트를 차지 */
    h->span = end;
    set_next(h, end, NIL); set_size(h, end, 0);
    set_next(h, 0, end);   set_size(h, 0, end);
    h->start_next = 0;
    h->free_bytes = h->min_ever = end;
}

static void h2_init(heap_t *h, uint8_t *mem, uint32_t total, uint32_t hdr) {     /* prvHeapInit, heap_2.c:371–393 */
    memset(h, 0, sizeof *h);
    h->kind = HEAP2; h->hdr = hdr;
    h->base = (uint8_t *)(((uintptr_t)mem + ALIGN - 1u) & ~(uintptr_t)ALIGN_MASK);  /* &ucHeap[ALIGN-1] & ~mask */
    h->span = total - ALIGN;                          /* configADJUSTED_HEAP_SIZE: 정렬 여부와 무관하게 8 B 뺌 */
    set_next(h, 0, NIL); set_size(h, 0, h->span);     /* xEnd 는 아레나 밖 (모델에선 NIL) */
    h->start_next = 0;
    h->free_bytes = h->min_ever = h->span;
}

static void note_alloc(heap_t *h, uint32_t b) {
    h->free_bytes -= blk_size(h, b);
    if (h->free_bytes < h->min_ever) h->min_ever = h->free_bytes;
    set_size(h, b, blk_size(h, b) | ALLOC_BIT);       /* heapALLOCATE_BLOCK */
    set_next(h, b, NIL);                              /* 할당된 블록의 next = NULL (free 검증에 쓰임) */
    h->n_alloc++;
}

/* ---------------- TODO 1: heap_4 할당 — 주소순 first fit ---------------- */
/* free list 를 xStart 부터 주소순으로 걸어 처음으로 size >= wanted 인 블록을 쓴다.
 * 남는 조각이 2*hdr 보다 크면 쪼개서 뒤쪽을 같은 자리에 다시 연결한다. h->walk = 방문한 노드 수. */
static void *h4_malloc(heap_t *h, size_t n) {
    /* TODO 1: wanted_size → 빠른 거절 → first fit 걷기(h->walk) → 리스트에서 빼기 → 쪼개기 → note_alloc */
    (void)n; (void)note_alloc; (void)get_link; (void)wanted_size;
    h->walk = 0;
    return NULL;
}

/* ---------------- TODO 2: heap_4 반납 — 주소순 삽입 + 병합 ---------------- */
/* 1) 주소가 b 보다 작은 마지막 노드(it)를 찾는다 (it == NIL 이면 xStart).
 * 2) it 끝 == b 이면 it 에 흡수 (b = it).
 * 3) b 끝 == 다음 노드 이면, 다음이 끝 표시(span)가 아닐 때만 흡수. 끝 표시와는 합치지 않는다.
 * 4) it != b 일 때만 it->next = b (앞과 합쳐졌으면 자기 자신을 가리키게 되므로). */
static void h4_insert_free(heap_t *h, uint32_t b) {
    /* TODO 2: 주소순 위치 찾기 → 앞 블록과 병합 → 뒤 블록과 병합(끝 표시 제외) → it != b 이면 연결 */
    (void)h; (void)b; (void)set_link;
}

/* ---------------- TODO 3: heap_2 — 크기순 free list (best fit, 병합 없음) ---------------- */
/* 크기 오름차순 위치에 꽂는다. 같은 크기가 이미 있으면 그 "앞" 에 (FreeRTOS 의 '<' 비교 → 같은 크기는 LIFO). */
static void h2_insert_free(heap_t *h, uint32_t b) {
    /* TODO 3a: 크기 오름차순 자리에 꽂는다 (같은 크기면 그 앞에) */
    (void)h; (void)b;
}
/* 작은 블록부터 걸어 처음 맞는 것 = 가장 작은 맞는 블록 (best fit). 쪼갠 뒷조각은 크기순 자리에 다시 꽂는다. */
static void *h2_malloc(heap_t *h, size_t n) {
    /* TODO 3b: 작은 블록부터 걸어 첫 번째 맞는 블록 (best fit) → 빼기 → 쪼갠 뒷조각은 h2_insert_free */
    (void)n;
    h->walk = 0;
    return NULL;
}

/* ---------------- 주어진 코드: free (검증 → 해제 → 삽입) ---------------- */
/* 반환: 0 = OK, -1 = 거절 (아레나 밖 / 정렬 안 맞음 / 할당 표시 없음 / next != NULL).
 * FreeRTOS 는 configASSERT 로 멈추고, assert 가 꺼져 있으면 조용히 무시한다 (heap_4.c:368–374). */
static int heap_free(heap_t *h, void *p) {
    if (p == NULL) return 0;
    uintptr_t a = (uintptr_t)p, lo = (uintptr_t)h->base + h->hdr, hi = (uintptr_t)h->base + h->span;
    if (a < lo || a >= hi || ((a - lo) & ALIGN_MASK)) return -1;
    uint32_t b = (uint32_t)(a - (uintptr_t)h->base) - h->hdr;
    if (!(blk_raw(h, b) & ALLOC_BIT) || blk_next(h, b) != NIL) return -1;
    set_size(h, b, blk_size(h, b));                        /* heapFREE_BLOCK */
    h->free_bytes += blk_size(h, b);
    if (h->kind == HEAP4) h4_insert_free(h, b); else h2_insert_free(h, b);
    h->n_free++;
    return 0;
}
static void *heap_malloc(heap_t *h, size_t n) { return h->kind == HEAP4 ? h4_malloc(h, n) : h2_malloc(h, n); }

/* ---------------- TODO 4: vPortGetHeapStats ---------------- */
/* free list 를 끝(heap_4: span, heap_2: NIL)까지 걸으며 블록 수·최대·최소를 센다. free 블록이 없으면
 * smallest = UINT32_MAX (FreeRTOS 의 SIZE_MAX 초기값 그대로). 나머지 4개는 카운터를 복사. */
static void heap_get_stats(const heap_t *h, heap_stats_t *s) {
    /* TODO 4: free list 를 끝까지 걸으며 n_blocks / largest / smallest, 나머지는 카운터 복사 */
    (void)h;
    memset(s, 0, sizeof *s);
}

/* 단편화 (permille): 1000 * (1 - largest / free). 0 = 한 덩어리, 1000 에 가까울수록 잘게 쪼개짐 */
static uint32_t frag_permille(uint32_t largest, uint32_t avail) {
    return avail == 0 ? 0 : 1000u - (uint32_t)((uint64_t)largest * 1000u / avail);
}

/* ---------------- 주어진 코드: 불변식 검사 (매 연산 뒤) ---------------- */
enum { INV_OK = 0, INV_WALK, INV_ALIGN, INV_SPAN, INV_ADJ_FREE, INV_LIST_ORDER, INV_LIST_NOT_FREE,
       INV_LIST_NOT_BLOCK, INV_COUNT, INV_BYTES, INV_MINEVER };
static uint8_t g_mark[ARENA_MAX / ALIGN / 8];
static int heap_check(const heap_t *h) {
    memset(g_mark, 0, sizeof g_mark);
    uint32_t off = 0, phys_free = 0, phys_free_bytes = 0, prev_free = 0, steps = 0;
    while (off < h->span) {                                /* 물리 순서로 블록을 걷는다 */
        uint32_t sz = blk_size(h, off), fr = !(blk_raw(h, off) & ALLOC_BIT);
        if (++steps > h->span / ALIGN + 1 || sz == 0) return INV_WALK;
        if (sz & ALIGN_MASK) return INV_ALIGN;
        if (h->kind == HEAP4 && fr && prev_free) return INV_ADJ_FREE;   /* heap_4: 이웃 free 둘이 붙어 있으면 안 됨 */
        g_mark[(off / ALIGN) >> 3] |= (uint8_t)(1u << ((off / ALIGN) & 7));
        if (fr) { phys_free++; phys_free_bytes += sz; }
        prev_free = fr; off += sz;
    }
    if (off != h->span) return INV_SPAN;                   /* 크기 합 == 아레나 */
    uint32_t end = h->kind == HEAP4 ? h->span : NIL, n = 0, bytes = 0, last = 0;
    for (uint32_t b = h->start_next; b != end; b = blk_next(h, b)) {
        if (b >= h->span || ++n > phys_free) return INV_LIST_NOT_BLOCK;
        if (!(g_mark[(b / ALIGN) >> 3] & (1u << ((b / ALIGN) & 7)))) return INV_LIST_NOT_BLOCK;
        if (blk_raw(h, b) & ALLOC_BIT) return INV_LIST_NOT_FREE;
        uint32_t key = h->kind == HEAP4 ? b : blk_size(h, b);
        if (n > 1 && (h->kind == HEAP4 ? key <= last : key < last)) return INV_LIST_ORDER;
        last = key; bytes += blk_size(h, b);
    }
    if (n != phys_free) return INV_COUNT;
    if (bytes != phys_free_bytes || bytes != h->free_bytes) return INV_BYTES;
    if (h->min_ever > h->free_bytes) return INV_MINEVER;
    return INV_OK;
}

/* ---------------- 주어진 코드: 결정적 워크로드 ---------------- */
typedef struct {                       /* 할당기 인터페이스 — 데모가 같은 워크로드를 pvPortMalloc 에 돌린다 */
    void *(*alloc)(void *ctx, size_t n);
    int   (*release)(void *ctx, void *p);
    void  (*stats)(void *ctx, heap_stats_t *s);
    int   (*check)(void *ctx);         /* NULL = 검사 생략 */
    void *ctx;
    uintptr_t base;                    /* 트레이스 오프셋 기준 */
} alloc_if_t;

typedef struct { int8_t kind; int32_t off; uint32_t avail; } wl_trace_t;   /* kind: 0 없음 1 할당 2 실패 3 해제 */
typedef struct {
    int first_fail, fails, allocs, frees, inv_bad, corrupt;
    uint32_t free_at_fail, largest_at_fail, min_ever, end_avail, end_largest, end_blocks;
} wl_result_t;

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define WL_MAXLIVE 32
/* 매 step: 난수 3개를 "항상" 뽑는다 (결과와 무관하게 같은 요청 흐름).
 * 55 % 할당 (8–512 B, 1/8 은 오래 사는 객체, 동시에 최대 maxlive 개), 45 % 해제 (오래 사는 객체는 1/4 확률로만 해제).
 * 할당한 바이트는 태그로 채우고 해제 직전에 확인 → 블록이 겹치면 corrupt 로 잡힌다. */
static void run_workload(const alloc_if_t *a, uint32_t seed, int steps, int maxlive, wl_result_t *r, wl_trace_t *tr) {
    struct { uint8_t *p; uint32_t n; uint8_t tag, longlived; } live[WL_MAXLIVE];
    int nlive = 0;
    uint32_t s = seed ? seed : 1;
    heap_stats_t st;
    memset(r, 0, sizeof *r); r->first_fail = -1;
    for (int i = 0; i < steps; i++) {
        uint32_t u1 = xorshift32(&s), u2 = xorshift32(&s), u3 = xorshift32(&s);
        int8_t kind = 0; int32_t off = -1;
        if (nlive == 0 || (nlive < maxlive && u1 % 100 < 55)) {
            uint32_t n = 8 + u2 % 505;
            uint8_t *p = a->alloc(a->ctx, n);
            if (p) {
                uint8_t tag = (uint8_t)(i * 31 + 7);
                memset(p, tag, n);
                live[nlive].p = p; live[nlive].n = n; live[nlive].tag = tag; live[nlive].longlived = (u3 % 8 == 0);
                nlive++; r->allocs++; kind = 1; off = (int32_t)((uintptr_t)p - a->base);
            } else {
                r->fails++; kind = 2;
                if (r->first_fail < 0) {
                    a->stats(a->ctx, &st);
                    r->first_fail = i; r->free_at_fail = st.available; r->largest_at_fail = st.largest;
                }
            }
        } else {
            int k = (int)(u2 % (uint32_t)nlive);
            if (!live[k].longlived || u3 % 4 == 0) {
                for (uint32_t j = 0; j < live[k].n; j++) if (live[k].p[j] != live[k].tag) { r->corrupt++; break; }
                off = (int32_t)((uintptr_t)live[k].p - a->base);
                if (a->release(a->ctx, live[k].p) != 0) r->corrupt++;
                live[k] = live[--nlive]; r->frees++; kind = 3;
            }
        }
        if (a->check && a->check(a->ctx) != INV_OK) r->inv_bad++;
        if (tr) { a->stats(a->ctx, &st); tr[i].kind = kind; tr[i].off = off; tr[i].avail = st.available; }
    }
    a->stats(a->ctx, &st);
    r->min_ever = st.min_ever; r->end_avail = st.available; r->end_largest = st.largest; r->end_blocks = st.n_blocks;
}

static void *m_alloc(void *c, size_t n) { return heap_malloc((heap_t *)c, n); }
static int m_release(void *c, void *p) { return heap_free((heap_t *)c, p); }
static void m_stats(void *c, heap_stats_t *s) { heap_get_stats((const heap_t *)c, s); }
static int m_check(void *c) { return heap_check((const heap_t *)c); }

/* ---------------- Part C: 펌웨어 힙 감시 ---------------- */
enum { HH_OK = 0, HH_LOW_WATER = 1, HH_FRAGMENTED = 2, HH_EXHAUSTED = 4 };
typedef struct { uint32_t low_water; uint32_t need_contig; } heap_policy_t;

/* ---------------- TODO 5: heap_health ---------------- */
/* 플래그를 OR 해서 반환:
 *  HH_LOW_WATER  : min_ever < low_water (부팅 이후 한 번이라도 여유가 기준 아래로 내려갔다)
 *  HH_FRAGMENTED : available >= need_contig 인데 largest < need_contig (바이트는 있는데 한 덩어리가 없다)
 *  HH_EXHAUSTED  : available < need_contig (바이트 자체가 모자람) */
static int heap_health(const heap_stats_t *s, const heap_policy_t *pol) {
    /* TODO 5: HH_LOW_WATER / HH_EXHAUSTED / HH_FRAGMENTED 를 OR */
    (void)s; (void)pol;
    return HH_OK;
}

/* 주어진 시나리오: STM32F4 급 LTE 센서 노드, configTOTAL_HEAP_SIZE = 16 KB, Cortex-M4 기하 (hdr 8).
 * 부팅: xTaskCreate 4개 (스택 먼저, TCB 84 B 나중 — tasks.c:1681/1689), 큐 2개 (Queue_t 80 B + 저장소 한 번에).
 * 실행: 모뎀 응답 버퍼 버스트 (40–300 B 24개) → 파싱 후 모두 해제. 그 다음 OTA 가 8 KB 연속 버퍼를 요청.
 * log_at_boot=0 이면 1 KB 로그 버퍼를 "첫 로그 이벤트(버스트 16번째)" 때 지연 할당 → 버스트 한가운데 박힌다. */
static void *g_task_mem[8];
static int lte_node(heap_t *h, uint8_t *arena, int log_at_boot, heap_stats_t *after_burst, void **ota) {
    static const uint32_t stack_bytes[4] = { 1024, 768, 512, 1024 };
    int nk = 0;
    h4_init(h, arena, 16384, HDR_CM4);
    for (int t = 0; t < 4; t++) { g_task_mem[nk++] = heap_malloc(h, stack_bytes[t]); g_task_mem[nk++] = heap_malloc(h, 84); }
    void *q1 = heap_malloc(h, 80 + 8 * 16), *q2 = heap_malloc(h, 80 + 4 * 32);
    void *logbuf = log_at_boot ? heap_malloc(h, 1024) : NULL;
    void *rsp[24];
    uint32_t s = 2026;
    for (int i = 0; i < 24; i++) {
        rsp[i] = heap_malloc(h, 40 + xorshift32(&s) % 261);
        if (i == 16 && !logbuf) logbuf = heap_malloc(h, 1024);
    }
    for (int i = 0; i < 24; i++) heap_free(h, rsp[i]);
    heap_get_stats(h, after_burst);
    *ota = heap_malloc(h, 8192);
    return (q1 && q2 && logbuf) ? 0 : -1;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static _Alignas(16) uint8_t g_arena[ARENA_MAX];
static _Alignas(16) uint8_t g_arena2[ARENA_MAX];
#define OFF(h, p) ((long)((uint8_t *)(p) - (h)->base))

int main(void) {
    heap_t h, h2; heap_stats_t st;

    /* ---- Part A: heap_4 기본 동작 (Cortex-M 기하: hdr 8, 아레나 1024) ---- */
    h4_init(&h, g_arena, 1024, HDR_CM4);
    CHECK(h.span == 1016 && h.free_bytes == 1016);            /* 끝 표시 8 B 를 아레나 안에 둔다 */
    void *a = heap_malloc(&h, 1), *b = heap_malloc(&h, 24), *c = heap_malloc(&h, 100), *d = heap_malloc(&h, 8);
    CHECK(a && b && c && d);
    CHECK(OFF(&h, a) == 8 && OFF(&h, b) == 24 && OFF(&h, c) == 56 && OFF(&h, d) == 168);  /* 16, 32, 112, 16 B 블록 */
    CHECK(h.free_bytes == 1016 - 16 - 32 - 112 - 16 && h.min_ever == h.free_bytes);
    CHECK(((uintptr_t)a & 7) == 0 && ((uintptr_t)c & 7) == 0);
    CHECK(heap_check(&h) == INV_OK);
    heap_free(&h, b);                                          /* 앞뒤 모두 할당 → 병합 없음 */
    heap_get_stats(&h, &st);
    CHECK(st.n_blocks == 2 && st.largest == 840 && st.smallest == 32);
    heap_free(&h, d);                                          /* 뒤(나머지 큰 블록)와 병합 */
    heap_get_stats(&h, &st);
    CHECK(st.n_blocks == 2 && st.largest == 856 && heap_check(&h) == INV_OK);
    heap_free(&h, c);                                          /* 앞(b)·뒤 둘 다 병합 → 구멍 메움 */
    heap_get_stats(&h, &st);
    CHECK(st.n_blocks == 1 && st.largest == 1000 && heap_check(&h) == INV_OK);
    heap_free(&h, a);                                          /* 앞이 xStart, 뒤와 병합 → 처음 상태 */
    heap_get_stats(&h, &st);
    CHECK(st.n_blocks == 1 && st.largest == 1016 && st.available == 1016);
    CHECK(st.min_ever == 1016 - 176 && st.n_alloc == 4 && st.n_free == 4);
    /* 쪼개기 경계: 남는 조각 == 2*hdr(16) 이면 안 쪼갬, 24 면 쪼갬 */
    h4_init(&h, g_arena, 1024, HDR_CM4);
    a = heap_malloc(&h, 992);                                  /* wanted 1000, 남는 16 → 통째로 1016 */
    CHECK(a && h.free_bytes == 0 && heap_check(&h) == INV_OK);
    CHECK(heap_malloc(&h, 1) == NULL && h.walk == 0);           /* 빠른 거절: 리스트를 걷지도 않는다 */
    heap_free(&h, a);
    a = heap_malloc(&h, 984);                                  /* wanted 992, 남는 24 → 쪼갬 */
    CHECK(a && h.free_bytes == 24 && heap_check(&h) == INV_OK);
    heap_free(&h, a);
    CHECK(heap_malloc(&h, 0) == NULL && heap_malloc(&h, 1009) == NULL && heap_malloc(&h, (size_t)-1) == NULL);
    CHECK(heap_malloc(&h, 1008) != NULL && h.free_bytes == 0);  /* 1008+8 = 1016 = 전부 */
    /* 정렬 안 된 아레나: 시작을 8 로 올린 만큼 잃는다 */
    h4_init(&h, g_arena + 3, 1024, HDR_CM4);
    CHECK(OFF(&h, g_arena) == -8 && h.span == ((1024 - 5 - 8) & ~7u) && heap_check(&h) == INV_OK);

    /* ---- Part B: heap_2 (best fit, 병합 없음) vs heap_4 (first fit + 병합) ---- */
    for (int kind = 0; kind < 2; kind++) {                     /* first fit vs best fit */
        heap_t *x = kind ? &h2 : &h;
        if (kind) h2_init(x, g_arena2, 1024, HDR_CM4); else h4_init(x, g_arena, 1024, HDR_CM4);
        void *A = heap_malloc(x, 64), *g1 = heap_malloc(x, 8), *B = heap_malloc(x, 32), *g2 = heap_malloc(x, 8);
        heap_free(x, A); heap_free(x, B);
        void *r = heap_malloc(x, 24);                          /* 32 B 블록 필요: A(72) 와 B(40) 둘 다 맞음 */
        if (kind == 0) CHECK(r == A); else CHECK(r == B);      /* heap_4 = 주소순 처음, heap_2 = 가장 작은 것 */
        CHECK(heap_check(x) == INV_OK);
        (void)g1; (void)g2;
    }
    h2_init(&h2, g_arena2, 1024, HDR_CM4);                     /* 같은 크기는 LIFO */
    void *P = heap_malloc(&h2, 32), *Q = heap_malloc(&h2, 32), *G = heap_malloc(&h2, 8);
    heap_free(&h2, P); heap_free(&h2, Q);
    CHECK(heap_malloc(&h2, 32) == Q && heap_check(&h2) == INV_OK);
    (void)G;
    h2_init(&h2, g_arena2, 1024, HDR_CM4);                     /* 이웃 free 블록이 합쳐지지 않는다 */
    a = heap_malloc(&h2, 100); b = heap_malloc(&h2, 100);
    heap_free(&h2, a); heap_free(&h2, b);
    heap_get_stats(&h2, &st);
    CHECK(h2.span == 1016 && st.available == 1016 && st.n_blocks == 3 && st.largest == 1016 - 224);
    CHECK(heap_malloc(&h2, 900) == NULL && heap_check(&h2) == INV_OK);   /* 1016 B 가 비었는데 908 B 블록 실패 */
    h4_init(&h, g_arena, 1024, HDR_CM4);
    a = heap_malloc(&h, 100); b = heap_malloc(&h, 100);
    heap_free(&h, a); heap_free(&h, b);
    CHECK(heap_malloc(&h, 900) != NULL);                       /* heap_4 는 다시 한 덩어리 */
    /* 잘못된 free: 이중 해제 · 아레나 밖 · 블록 중간 */
    h4_init(&h, g_arena, 1024, HDR_CM4);
    a = heap_malloc(&h, 40); b = heap_malloc(&h, 40);
    CHECK(heap_free(&h, a) == 0 && heap_free(&h, a) == -1);
    CHECK(heap_free(&h, g_arena2 + 64) == -1 && heap_free(&h, (uint8_t *)b + 8) == -1 && heap_check(&h) == INV_OK);

    /* 병적 패턴: 살아 있는 블록은 늘 1개, 요청 크기만 16, 32, 48 ... 로 커진다 */
    int fail2 = -1, fail4 = -1;
    h2_init(&h2, g_arena2, 8192, HDR_CM4); h4_init(&h, g_arena, 8192, HDR_CM4);
    for (int k = 1; k <= 600 && (fail2 < 0 || fail4 < 0); k++) {
        void *p2 = fail2 < 0 ? heap_malloc(&h2, 16u * (uint32_t)k) : NULL;
        void *p4 = fail4 < 0 ? heap_malloc(&h, 16u * (uint32_t)k) : NULL;
        if (fail2 < 0 && !p2) fail2 = k;
        if (fail4 < 0 && !p4) fail4 = k;
        heap_free(&h2, p2); heap_free(&h, p4);
    }
    heap_get_stats(&h2, &st);
    printf("growing request, 1 live block: heap_2 fails at k=%d (%d B), free=%u in %u blocks, largest=%u | heap_4 fails at k=%d (%d B)\n",
           fail2, 16 * fail2, st.available, st.n_blocks, st.largest, fail4, 16 * fail4);
    CHECK(fail2 == 32 && st.available == 8184 && st.n_blocks == 31 && st.largest == 504);
    CHECK(fail4 == 512 && heap_check(&h) == INV_OK && heap_check(&h2) == INV_OK);

    /* 무작위 워크로드: 같은 8 KB 아레나, 같은 요청 흐름, 시드 8개 */
    printf("seed | heap_4 first_fail free@fail largest frag%% min_ever fails maxwalk | heap_2 first_fail free@fail largest frag%% min_ever fails maxwalk\n");
    int inv_bad = 0, corrupt = 0, h4_later = 0, sum4 = 0, sum2 = 0;
    wl_result_t r4[9], r2[9];
    for (uint32_t seed = 1; seed <= 8; seed++) {
        alloc_if_t i4 = { m_alloc, m_release, m_stats, m_check, &h, 0 }, i2 = { m_alloc, m_release, m_stats, m_check, &h2, 0 };
        h4_init(&h, g_arena, 8192, HDR_CM4); h2_init(&h2, g_arena2, 8192, HDR_CM4);
        i4.base = (uintptr_t)h.base; i2.base = (uintptr_t)h2.base;
        run_workload(&i4, seed, 5000, 20, &r4[seed], NULL);
        uint32_t mw4 = h.max_walk;
        run_workload(&i2, seed, 5000, 20, &r2[seed], NULL);
        printf("%4u | %6d %9u %7u %4u.%u %8u %5d %7u | %6d %9u %7u %4u.%u %8u %5d %7u\n", seed,
               r4[seed].first_fail, r4[seed].free_at_fail, r4[seed].largest_at_fail,
               frag_permille(r4[seed].largest_at_fail, r4[seed].free_at_fail) / 10, frag_permille(r4[seed].largest_at_fail, r4[seed].free_at_fail) % 10,
               r4[seed].min_ever, r4[seed].fails, mw4,
               r2[seed].first_fail, r2[seed].free_at_fail, r2[seed].largest_at_fail,
               frag_permille(r2[seed].largest_at_fail, r2[seed].free_at_fail) / 10, frag_permille(r2[seed].largest_at_fail, r2[seed].free_at_fail) % 10,
               r2[seed].min_ever, r2[seed].fails, h2.max_walk);
        inv_bad += r4[seed].inv_bad + r2[seed].inv_bad;
        corrupt += r4[seed].corrupt + r2[seed].corrupt;
        h4_later += r4[seed].first_fail >= r2[seed].first_fail;
        sum4 += r4[seed].fails; sum2 += r2[seed].fails;
    }
    CHECK(inv_bad == 0 && corrupt == 0);                       /* 매 연산 뒤 불변식 + 태그 확인 */
    CHECK(h4_later == 8 && sum4 < sum2);
    CHECK(r4[1].first_fail == 742 && r2[1].first_fail == 148);   /* 회귀값 = 진짜 FreeRTOS 와 같은 값 (데모) */
    int never = 0, fails2 = 0;                                 /* 동시 16개로 낮추면 heap_4 는 한 번도 실패 안 함 */
    for (uint32_t seed = 1; seed <= 8; seed++) {
        wl_result_t q4, q2;
        alloc_if_t i4 = { m_alloc, m_release, m_stats, m_check, &h, 0 }, i2 = { m_alloc, m_release, m_stats, m_check, &h2, 0 };
        h4_init(&h, g_arena, 8192, HDR_CM4); h2_init(&h2, g_arena2, 8192, HDR_CM4);
        run_workload(&i4, seed, 5000, 16, &q4, NULL); run_workload(&i2, seed, 5000, 16, &q2, NULL);
        never += q4.fails == 0 && q4.inv_bad == 0; fails2 += q2.fails;
    }
    printf("maxlive 16: heap_4 seeds with 0 failures = %d/8, heap_2 failed requests = %d (of 40000 steps)\n", never, fails2);
    CHECK(never == 8 && fails2 > 8000);

    /* ---- Part C: 펌웨어 힙 감시 ---- */
    heap_policy_t pol = { 1638, 8192 };                        /* 저수위 10 %, OTA 연속 8 KB */
    heap_stats_t s1, s2; void *ota1, *ota2;
    CHECK(lte_node(&h, g_arena, 0, &s1, &ota1) == 0 && heap_check(&h) == INV_OK);
    CHECK(lte_node(&h2, g_arena2, 1, &s2, &ota2) == 0 && heap_check(&h2) == INV_OK);
    printf("LTE node  lazy log : avail=%u largest=%u blocks=%u min_ever=%u ota=%s health=%d\n",
           s1.available, s1.largest, s1.n_blocks, s1.min_ever, ota1 ? "ok" : "FAIL", heap_health(&s1, &pol));
    printf("LTE node  boot log : avail=%u largest=%u blocks=%u min_ever=%u ota=%s health=%d\n",
           s2.available, s2.largest, s2.n_blocks, s2.min_ever, ota2 ? "ok" : "FAIL", heap_health(&s2, &pol));
    CHECK(s1.available == s2.available);                       /* 바이트 수는 같다 */
    CHECK(ota1 == NULL && heap_health(&s1, &pol) == HH_FRAGMENTED);
    CHECK(ota2 != NULL && heap_health(&s2, &pol) == HH_OK);
    heap_stats_t t = { 9000, 9000, 16, 1, 1000, 0, 0 };
    CHECK(heap_health(&t, &pol) == HH_LOW_WATER);
    t.available = 5000; t.largest = 5000;
    CHECK(heap_health(&t, &pol) == (HH_LOW_WATER | HH_EXHAUSTED));
    CHECK(frag_permille(0, 0) == 0 && frag_permille(500, 1000) == 500 && frag_permille(1000, 1000) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
