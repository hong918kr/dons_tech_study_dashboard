/* RTOS-M10 추가 연습 — 한 번에 검증
 *  연습 1. heap_1: 범프 할당기 (free 불가, 헤더 없음, '<' 비교 때문에 마지막 정렬 단위는 못 씀)
 *  연습 2. heap_5: 영역 2개 = heap_4 코드 + vPortDefineHeapRegions. 영역 끝 표시가 free list 에 남는다
 *          (demo_freertos_heap5.c 의 진짜 heap_5 출력과 같은 숫자, hdr=16)
 *  연습 3. free 가 거절하는 이유 분류 — 힙 오버플로가 다음 블록 헤더를 덮으면 configASSERT 가 잡는 방식
 *  연습 4. heap_2 가 괜찮은 경우: 같은 크기만 만들고 지우는 태스크 생성/삭제 반복
 *  연습 5. xMinimumEverFreeBytesRemaining 으로 configTOTAL_HEAP_SIZE 정하기 (+ 단편화 여유)
 * 아래 앞부분은 solution.c 의 모델(정답 채운 상태)을 그대로 옮기고 HEAP5 만 더했다.
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

typedef enum { HEAP4, HEAP2, HEAP5 } heap_kind_t;   /* HEAP5 = heap_4 코드 + 여러 영역 (연습 2) */

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

/* ---------------- heap_4 할당 (solution.c TODO 1 과 같음) ---------------- */
/* free list 를 xStart 부터 주소순으로 걸어 처음으로 size >= wanted 인 블록을 쓴다.
 * 남는 조각이 2*hdr 보다 크면 쪼개서 뒤쪽을 같은 자리에 다시 연결한다. h->walk = 방문한 노드 수. */
static void *h4_malloc(heap_t *h, size_t n) {
    uint32_t w = wanted_size(h, n);
    h->walk = 0;
    if (w == 0 || w > h->free_bytes) return NULL;          /* 빠른 거절 (heap_4.c:240) */
    uint32_t prev = NIL, b = h->start_next;
    h->walk = 1;
    while (blk_size(h, b) < w && blk_next(h, b) != NIL) {  /* 끝 표시는 size 0, next NIL */
        prev = b; b = blk_next(h, b); h->walk++;
    }
    if (h->walk > h->max_walk) h->max_walk = h->walk;
    if (b == h->span) return NULL;                         /* 끝 표시까지 왔다 = 맞는 블록 없음 */
    set_link(h, prev, blk_next(h, b));                     /* 리스트에서 뺀다 */
    if (blk_size(h, b) - w > 2u * h->hdr) {                /* heapMINIMUM_BLOCK_SIZE 보다 "크면" */
        uint32_t nb = b + w;
        set_size(h, nb, blk_size(h, b) - w);
        set_next(h, nb, get_link(h, prev));                /* 같은 자리(주소순 유지)에 꽂는다 */
        set_link(h, prev, nb);
        set_size(h, b, w);
    }
    note_alloc(h, b);
    return h->base + b + h->hdr;
}

/* ---------------- heap_4 반납 (solution.c TODO 2 와 같음) ---------------- */
/* 1) 주소가 b 보다 작은 마지막 노드(it)를 찾는다 (it == NIL 이면 xStart).
 * 2) it 끝 == b 이면 it 에 흡수 (b = it).
 * 3) b 끝 == 다음 노드 이면, 다음이 끝 표시(span)가 아닐 때만 흡수. 끝 표시와는 합치지 않는다.
 * 4) it != b 일 때만 it->next = b (앞과 합쳐졌으면 자기 자신을 가리키게 되므로). */
static void h4_insert_free(heap_t *h, uint32_t b) {
    uint32_t it = NIL;
    h->walk = 0;
    while (get_link(h, it) < b) { it = get_link(h, it); h->walk++; }
    if (h->walk > h->max_walk) h->max_walk = h->walk;
    if (it != NIL && it + blk_size(h, it) == b) {          /* 앞 블록과 병합 */
        set_size(h, it, blk_size(h, it) + blk_size(h, b));
        b = it;
    }
    uint32_t nx = get_link(h, it);
    if (b + blk_size(h, b) == nx) {                        /* 뒤 블록과 병합 */
        if (nx != h->span) {
            set_size(h, b, blk_size(h, b) + blk_size(h, nx));
            set_next(h, b, blk_next(h, nx));
        } else {
            set_next(h, b, h->span);
        }
    } else {
        set_next(h, b, nx);
    }
    if (it != b) set_link(h, it, b);
}

/* ---------------- heap_2 (solution.c TODO 3 과 같음) ---------------- */
/* 크기 오름차순 위치에 꽂는다. 같은 크기가 이미 있으면 그 "앞" 에 (FreeRTOS 의 '<' 비교 → 같은 크기는 LIFO). */
static void h2_insert_free(heap_t *h, uint32_t b) {
    uint32_t it = NIL, bs = blk_size(h, b);
    h->walk = 0;
    while (get_link(h, it) != NIL && blk_size(h, get_link(h, it)) < bs) { it = get_link(h, it); h->walk++; }
    if (h->walk > h->max_walk) h->max_walk = h->walk;
    set_next(h, b, get_link(h, it));
    set_link(h, it, b);
}
/* 작은 블록부터 걸어 처음 맞는 것 = 가장 작은 맞는 블록 (best fit). 쪼갠 뒷조각은 크기순 자리에 다시 꽂는다. */
static void *h2_malloc(heap_t *h, size_t n) {
    uint32_t w = wanted_size(h, n);
    h->walk = 0;
    if (w == 0 || w > h->free_bytes) return NULL;
    uint32_t prev = NIL, b = h->start_next;
    h->walk = 1;
    while (b != NIL && blk_size(h, b) < w) { prev = b; b = blk_next(h, b); h->walk++; }
    if (h->walk > h->max_walk) h->max_walk = h->walk;
    if (b == NIL) return NULL;                             /* xEnd 까지 왔다 */
    set_link(h, prev, blk_next(h, b));
    if (blk_size(h, b) - w > 2u * h->hdr) {
        uint32_t nb = b + w, walk = h->walk;
        set_size(h, nb, blk_size(h, b) - w);
        set_size(h, b, w);
        h2_insert_free(h, nb);
        h->walk += walk;
    }
    note_alloc(h, b);
    return h->base + b + h->hdr;
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
    if (h->kind == HEAP2) h2_insert_free(h, b); else h4_insert_free(h, b);
    h->n_free++;
    return 0;
}
static void *heap_malloc(heap_t *h, size_t n) { return h->kind == HEAP2 ? h2_malloc(h, n) : h4_malloc(h, n); }

/* ---------------- vPortGetHeapStats (solution.c TODO 4 + heap_5 의 0 제외 규칙) ---------------- */
/* free list 를 끝(heap_4: span, heap_2: NIL)까지 걸으며 블록 수·최대·최소를 센다. free 블록이 없으면
 * smallest = UINT32_MAX (FreeRTOS 의 SIZE_MAX 초기값 그대로). 나머지 4개는 카운터를 복사. */
static void heap_get_stats(const heap_t *h, heap_stats_t *s) {
    uint32_t end = h->kind == HEAP2 ? NIL : h->span;
    memset(s, 0, sizeof *s);
    s->smallest = UINT32_MAX;
    for (uint32_t b = h->start_next; b != end; b = blk_next(h, b)) {
        s->n_blocks++;                                     /* heap_5: 영역 끝 표시(size 0)도 센다 */
        if (blk_size(h, b) > s->largest)  s->largest = blk_size(h, b);
        if (blk_size(h, b) != 0 && blk_size(h, b) < s->smallest) s->smallest = blk_size(h, b);  /* heap_5.c 처럼 0 제외 */
    }
    s->available = h->free_bytes; s->min_ever = h->min_ever;
    s->n_alloc = h->n_alloc; s->n_free = h->n_free;
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

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ================= 연습 1: heap_1 ================= */
typedef struct { uint8_t *base; uint32_t adjusted, next; } heap1_t;
static void h1_init(heap1_t *h, uint8_t *mem, uint32_t total) {
    h->base = (uint8_t *)(((uintptr_t)mem + ALIGN - 1u) & ~(uintptr_t)ALIGN_MASK);   /* heap_1.c: &ucHeap[ALIGN-1] & ~mask */
    h->adjusted = total - ALIGN;                                                     /* configADJUSTED_HEAP_SIZE */
    h->next = 0;
}
static void *h1_malloc(heap1_t *h, size_t n) {
    if (n == 0 || n > h->adjusted) return NULL;
    uint32_t w = (uint32_t)n;                                 /* 헤더 없음 — 정렬만 */
    if (w & ALIGN_MASK) w += ALIGN - (w & ALIGN_MASK);
    if (h->next + w < h->adjusted) {                          /* 원본도 '<' : 딱 맞는 마지막 요청은 실패 */
        void *p = h->base + h->next; h->next += w; return p;
    }
    return NULL;
}
static int h1_free(heap1_t *h, void *p) { (void)h; return p == NULL ? 0 : -1; }   /* configASSERT( pv == NULL ) */
static uint32_t h1_free_bytes(const heap1_t *h) { return h->adjusted - h->next; }

/* ================= 연습 2: heap_5 ================= */
typedef struct { uint32_t off, size; } region_t;           /* HeapRegion_t (주소 오름차순, mem 기준 오프셋) */
static int h5_define(heap_t *h, uint8_t *mem, const region_t *r, int n, uint32_t hdr) {
    memset(h, 0, sizeof *h);
    h->kind = HEAP5; h->hdr = hdr; h->base = mem;              /* mem 은 8 B 정렬이라고 가정 */
    uint32_t prev_end = NIL, total = 0;
    for (int i = 0; i < n; i++) {
        uint32_t start = (r[i].off + ALIGN_MASK) & ~ALIGN_MASK, size = r[i].size - (start - r[i].off);
        if (prev_end != NIL && start <= prev_end) return -1;   /* configASSERT( xAddress > pxEnd ) */
        uint32_t end = (start + size - hdr) & ~ALIGN_MASK;
        if (prev_end == NIL) h->start_next = start; else set_next(h, prev_end, start);  /* 앞 영역 끝 표시 → 이 영역 */
        set_next(h, end, NIL); set_size(h, end, 0);
        set_next(h, start, end); set_size(h, start, end - start);
        total += end - start; prev_end = end;
    }
    h->span = prev_end;                                        /* pxEnd = 마지막 영역의 끝 표시 */
    h->free_bytes = h->min_ever = total;
    return 0;
}

/* ================= 연습 3: free 거절 이유 ================= */
enum { FR_OK, FR_NULL, FR_RANGE, FR_NOT_ALLOCATED, FR_NEXT_NOT_NULL };
static int free_reason(const heap_t *h, const void *p) {
    if (p == NULL) return FR_NULL;
    uintptr_t a = (uintptr_t)p, lo = (uintptr_t)h->base + h->hdr, hi = (uintptr_t)h->base + h->span;
    if (a < lo || a >= hi || ((a - lo) & ALIGN_MASK)) return FR_RANGE;           /* heapVALIDATE_BLOCK_POINTER */
    uint32_t b = (uint32_t)(a - (uintptr_t)h->base) - h->hdr;
    if (!(blk_raw(h, b) & ALLOC_BIT)) return FR_NOT_ALLOCATED;                     /* heap_4.c:369 */
    if (blk_next(h, b) != NIL) return FR_NEXT_NOT_NULL;                            /* heap_4.c:370 */
    return FR_OK;
}

/* ================= 연습 4·5: 태스크 생성/삭제 반복 ================= */
/* 최대 6개 태스크. 생성 = 스택(고정 512 B 또는 256/512/768/1024 중 하나) 먼저, TCB 84 B 나중 (tasks.c:1681/1689).
 * 삭제 = TCB, 스택 순으로 반납. 반환: 첫 실패 step (-1 = 실패 없음). max_blocks = free 블록 수 최대값. */
static int task_churn(heap_t *h, int variable, int steps, uint32_t *max_blocks) {
    static const uint32_t stk_sz[4] = { 256, 512, 768, 1024 };
    void *stk[6], *tcb[6];
    int n = 0, first_fail = -1;
    uint32_t s = 7;
    heap_stats_t q;
    *max_blocks = 0;
    for (int i = 0; i < steps; i++) {
        uint32_t u = xorshift32(&s), v = xorshift32(&s);
        if (n == 0 || (n < 6 && (u & 1))) {
            void *a = heap_malloc(h, variable ? stk_sz[v % 4] : 512), *b = a ? heap_malloc(h, 84) : NULL;
            if (!a || !b) { heap_free(h, a); if (first_fail < 0) first_fail = i; continue; }
            stk[n] = a; tcb[n] = b; n++;
        } else {
            int k = (int)(v % (uint32_t)n);
            heap_free(h, tcb[k]); heap_free(h, stk[k]);
            stk[k] = stk[n - 1]; tcb[k] = tcb[n - 1]; n--;
        }
        heap_get_stats(h, &q);
        if (q.n_blocks > *max_blocks) *max_blocks = q.n_blocks;
        if (heap_check(h) != INV_OK) return -99;
    }
    return first_fail;
}
/* 연습 5: 관측한 최대 사용량(usable - min_ever) + 여유 % + 끝 표시(hdr) + 정렬 손실(ALIGN) → 256 B 단위 올림 */
static uint32_t advise_total(uint32_t usable, uint32_t min_ever, uint32_t hdr, uint32_t margin_pct) {
    if (min_ever > usable) return 0;
    uint32_t peak = usable - min_ever;
    uint32_t need = peak + (peak * margin_pct + 99u) / 100u + hdr + ALIGN;
    return (need + 255u) & ~255u;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static _Alignas(16) uint8_t g_arena[ARENA_MAX];

int main(void) {
    heap_t h; heap_stats_t st;

    /* 연습 1: heap_1 */
    heap1_t h1;
    h1_init(&h1, g_arena + 3, 1024);
    CHECK(((uintptr_t)h1.base & 7) == 0 && h1.adjusted == 1016);
    int cnt = 0;
    while (h1_malloc(&h1, 8) != NULL) cnt++;
    printf("ex1 heap_1: %d x 8 B allocations, then xPortGetFreeHeapSize()=%u but malloc(8) fails\n", cnt, h1_free_bytes(&h1));
    CHECK(cnt == 126 && h1_free_bytes(&h1) == 8);           /* 8 B 가 "남았다" 고 보고하지만 8 B 요청도 실패 */
    CHECK(h1_malloc(&h1, 0) == NULL && h1_free(&h1, h1.base) == -1 && h1_free(&h1, NULL) == 0);
    h1_init(&h1, g_arena, 1024);
    uint8_t *x1 = h1_malloc(&h1, 1), *x2 = h1_malloc(&h1, 1);
    CHECK(x2 - x1 == 8);                                     /* 헤더 없음: 1 B 요청 = 8 B (heap_4 는 16 B) */

    /* 연습 2: heap_5 — 진짜 heap_5 (demo_freertos_heap5.c) 와 같은 숫자 */
    static const region_t regs[2] = { { 0, 1024 }, { 2048, 512 } };
    CHECK(h5_define(&h, g_arena, regs, 2, HDR_HOST64) == 0);
    heap_get_stats(&h, &st);
    CHECK(st.available == 1504 && st.n_blocks == 3 && st.largest == 1008 && st.smallest == 496);
    CHECK(heap_malloc(&h, 1200) == NULL);                    /* 합계 1504 B 지만 한 영역에 안 들어감 */
    uint8_t *a = heap_malloc(&h, 900), *b = heap_malloc(&h, 400);
    CHECK(a - g_arena == 16 && b - g_arena == 2064);
    heap_get_stats(&h, &st);
    CHECK(st.available == 168 && st.n_blocks == 3 && st.largest == 88 && st.smallest == 80);
    heap_free(&h, a); heap_free(&h, b);
    heap_get_stats(&h, &st);
    CHECK(st.available == 1504 && st.n_blocks == 3);
    a = heap_malloc(&h, 992);                                /* 영역 A 전체 */
    CHECK(a - g_arena == 16);
    heap_free(&h, a);                                        /* 뒤가 영역 A 끝 표시(size 0) → 흡수 */
    heap_get_stats(&h, &st);
    CHECK(st.available == 1504 && st.n_blocks == 2 && st.largest == 1008);
    CHECK(h5_define(&h, g_arena, (const region_t[]){ { 2048, 512 }, { 0, 1024 } }, 2, HDR_HOST64) == -1);  /* 주소 역순 거절 */

    /* 연습 3: 오버플로가 다음 블록 헤더를 덮는다 (아레나 안 쓰기 — 진짜 OOB 아님) */
    h4_init(&h, g_arena, 1024, HDR_CM4);
    a = heap_malloc(&h, 24); b = heap_malloc(&h, 24);
    CHECK(free_reason(&h, b) == FR_OK && free_reason(&h, NULL) == FR_NULL && free_reason(&h, b + 4) == FR_RANGE);
    memset(a, 0xAA, 24 + 4);                                 /* 4 B 넘침: b 의 next 를 덮음 */
    CHECK(free_reason(&h, b) == FR_NEXT_NOT_NULL);
    memset(a, 0xAA, 24 + 8);                                 /* 8 B 넘침: size 도 0xAAAAAAAA (MSB=1 → "할당됨" 처럼 보임) */
    CHECK(free_reason(&h, b) == FR_NEXT_NOT_NULL && heap_check(&h) != INV_OK);
    memset(a, 0x00, 24 + 8);                                 /* 0 으로 넘침: size 0 → 할당 표시 없음 */
    CHECK(free_reason(&h, b) == FR_NOT_ALLOCATED);
    set_next(&h, (uint32_t)(b - h.base) - 8, NIL); set_size(&h, (uint32_t)(b - h.base) - 8, 32u | ALLOC_BIT);
    CHECK(free_reason(&h, b) == FR_OK && heap_check(&h) == INV_OK);   /* 헤더를 "맞게" 덮으면 못 잡는다 */
    CHECK(heap_free(&h, a) == 0 && free_reason(&h, a) == FR_NOT_ALLOCATED);   /* 이중 해제 */

    /* 연습 4: heap_2 는 크기가 고정이면 괜찮다 */
    uint32_t mb2f, mb4f, mb2v, mb4v;
    h2_init(&h, g_arena, 8192, HDR_CM4); int f2f = task_churn(&h, 0, 20000, &mb2f);
    h4_init(&h, g_arena, 8192, HDR_CM4); int f4f = task_churn(&h, 0, 20000, &mb4f);
    h2_init(&h, g_arena, 8192, HDR_CM4); int f2v = task_churn(&h, 1, 20000, &mb2v);
    h4_init(&h, g_arena, 8192, HDR_CM4); int f4v = task_churn(&h, 1, 20000, &mb4v);
    printf("ex4 churn fixed : heap_2 first_fail=%d max_blocks=%u | heap_4 first_fail=%d max_blocks=%u\n", f2f, mb2f, f4f, mb4f);
    printf("ex4 churn varied: heap_2 first_fail=%d max_blocks=%u | heap_4 first_fail=%d max_blocks=%u\n", f2v, mb2v, f4v, mb4v);
    CHECK(f2f == -1 && f4f == -1 && mb2f == 13 && mb4f == 4);
    CHECK(f2v == 83 && f4v == -1 && mb2v == 32 && mb4v == 8);

    /* 연습 5: min-ever 로 크기 정하기 */
    uint32_t mb, us = 0, me[2] = { 0, 0 };
    int ok_fixed0 = 0, first_ok_margin = -1;
    for (int var = 0; var < 2; var++) {
        h4_init(&h, g_arena, 8192, HDR_CM4); us = h.span;
        task_churn(&h, var, 20000, &mb); me[var] = h.min_ever;
    }
    h4_init(&h, g_arena, advise_total(us, me[0], HDR_CM4, 0), HDR_CM4);
    ok_fixed0 = task_churn(&h, 0, 20000, &mb) == -1;
    for (uint32_t m = 0; m <= 50 && first_ok_margin < 0; m += 5) {
        h4_init(&h, g_arena, advise_total(us, me[1], HDR_CM4, m), HDR_CM4);
        if (task_churn(&h, 1, 20000, &mb) == -1) first_ok_margin = (int)m;
    }
    printf("ex5 fixed: peak %u -> total %u (margin 0) ok=%d | varied: peak %u, first margin with no failure = %d%% -> total %u\n",
           us - me[0], advise_total(us, me[0], HDR_CM4, 0), ok_fixed0, us - me[1], first_ok_margin,
           advise_total(us, me[1], HDR_CM4, (uint32_t)first_ok_margin));
    CHECK(us - me[0] == 3696 && advise_total(us, me[0], HDR_CM4, 0) == 3840 && ok_fixed0);
    CHECK(us - me[1] == 6512 && first_ok_margin == 15 && advise_total(us, me[1], HDR_CM4, 25) == 8192);
    CHECK(advise_total(100, 200, 8, 0) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
