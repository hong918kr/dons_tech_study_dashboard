/* OS-M04 malloc 구현 1: 암시적/명시적 free list, split·coalesce
 *
 * 힙 = mmap(MAP_PRIVATE|MAP_ANON) 64 MiB 한 덩어리. mem_sbrk(incr) 가 그 안에서 break 를 올린다
 *   (CS:APP 3e §9.9 memlib). 할당기 안에서는 진짜 malloc 을 쓰지 않는다 (하네스 표는 써도 됨).
 * 블록 형식 (SHARED ALLOCATOR RULES — OS-M05/M06 과 같음):
 *   [hdr 8B: size|alloc(bit0)] [payload … 16B 정렬] [ftr 8B = hdr 복사]   최소 블록 32B
 *   free 블록의 payload 앞 16B = 명시적 리스트 포인터 next / prev.
 *   힙 = [pad 8][prologue hdr 16|1][prologue ftr 16|1] [블록들 …] [epilogue hdr 0|1]
 *
 *   Part A  암시적 리스트 first fit (기준선) — TODO A1 implicit_find_fit, A2 place (분할)
 *   Part B  명시적 이중 연결 free list + boundary tag
 *           — TODO B1 coalesce (4가지 경우), B2 explicit_find_fit (first/next/best), B3 mm_check
 *   Part C  realloc (제자리 축소+분할, 다음 free 블록 흡수, 힙 끝 확장, 아니면 이동) — TODO C1
 *           + 공유 트레이스 T1~T4 재생: 정확성(패턴) · mm_check · 활용률 U = peak live / heap high-water
 *
 * 처리량(ops/s)은 demo_bench.c 에서만 잰다. 이 파일의 체크는 결정적인 것만 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE           /* MAP_ANON (macOS, -std=c11) */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ===== memlib: mmap 영역 하나 + break (CS:APP memlib 와 같은 생각) ===== */
#define MEM_MAX ((size_t)64 << 20)
static char *mem_start, *mem_brk;
static size_t mem_limit = MEM_MAX;

static int mem_init(void) {
    if (!mem_start) {
        void *p = mmap(NULL, MEM_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) return -1;
        mem_start = p;
    }
    mem_brk = mem_start;              /* 영역은 재사용, break 만 처음으로 */
    mem_limit = MEM_MAX;
    return 0;
}
static void mem_deinit(void) { if (mem_start) munmap(mem_start, MEM_MAX); mem_start = mem_brk = NULL; }
static void mem_set_limit(size_t bytes) { mem_limit = bytes < MEM_MAX ? bytes : MEM_MAX; }  /* MCU 힙 흉내 */
static void *mem_sbrk(intptr_t incr) {
    if (incr < 0 || (size_t)incr > mem_limit - (size_t)(mem_brk - mem_start)) return NULL;  /* 깔끔히 실패 */
    char *old = mem_brk;
    mem_brk += incr;
    return old;
}
static char *mem_heap_lo(void) { return mem_start; }
static char *mem_heap_hi(void) { return mem_brk - 1; }          /* 마지막 유효 바이트 */
static size_t mem_heapsize(void) { return (size_t)(mem_heap_hi() + 1 - mem_heap_lo()); }  /* 줄지 않으므로 = high-water */

/* ===== 블록 형식 ===== */
#define WSIZE     8
#define DSIZE     16
#define MIN_BLOCK 32
static inline size_t rd(const void *p) { size_t w; memcpy(&w, p, sizeof w); return w; }  /* 타입 섞임 없이 */
static inline void wr(void *p, size_t w) { memcpy(p, &w, sizeof w); }
static inline size_t pack(size_t size, int alloc) { return size | (size_t)(alloc & 1); }
static inline size_t size_at(const void *p) { return rd(p) & ~(size_t)0xF; }
static inline int alloc_at(const void *p) { return (int)(rd(p) & 1); }
static inline char *hdrp(void *bp) { return (char *)bp - WSIZE; }
static inline size_t blk_size(void *bp) { return size_at(hdrp(bp)); }
static inline int blk_alloc(void *bp) { return alloc_at(hdrp(bp)); }
static inline char *ftrp(void *bp) { return (char *)bp + blk_size(bp) - DSIZE; }
static inline char *next_blkp(void *bp) { return (char *)bp + blk_size(bp); }
static inline char *prev_blkp(void *bp) { return (char *)bp - size_at((char *)bp - DSIZE); }
static inline void set_block(void *bp, size_t size, int alloc) {
    wr(hdrp(bp), pack(size, alloc));
    wr((char *)bp + size - DSIZE, pack(size, alloc));
}
static inline char *fnext(void *bp) { char *p; memcpy(&p, bp, sizeof p); return p; }
static inline char *fprev(void *bp) { char *p; memcpy(&p, (char *)bp + WSIZE, sizeof p); return p; }
static inline void set_fnext(void *bp, char *v) { memcpy(bp, &v, sizeof v); }
static inline void set_fprev(void *bp, char *v) { memcpy((char *)bp + WSIZE, &v, sizeof v); }

/* 요청 n 바이트 → 블록 크기: hdr+ftr 16B 더하고 16 배수로, 최소 32 */
static inline size_t adjust(size_t n) {
    return n <= MIN_BLOCK - DSIZE ? MIN_BLOCK : (n + DSIZE + 15) & ~(size_t)15;
}

enum { KIND_IMPLICIT, KIND_EXPLICIT };
enum { FIT_FIRST, FIT_NEXT, FIT_BEST, FIT_COUNT };
static const char *const fit_name[FIT_COUNT] = { "first", "next", "best" };

static int g_kind = KIND_EXPLICIT, g_fit = FIT_FIRST;
static char *heap_listp;          /* prologue 의 payload */
static char *free_head;           /* 명시적 리스트 머리 (LIFO 삽입) */
static char *rover;               /* next fit: 다음 탐색 시작점 (NULL = 머리부터) */

typedef struct {
    size_t extend, split, coal[4];                 /* coal[k]: 경우 1~4 횟수 */
    size_t shrink_split, grow_next, grow_extend, moved;
    size_t scans, mallocs;                         /* find_fit 이 본 블록 수 / malloc 호출 수 */
} mm_stats_t;
static mm_stats_t g_st;

static void mm_config(int kind, int fit) { g_kind = kind; g_fit = fit; }

/* ===== 명시적 free list (주어짐) — 암시적 모드에서는 아무것도 안 한다 ===== */
static void fl_insert(char *bp) {
    if (g_kind != KIND_EXPLICIT) return;
    set_fnext(bp, free_head);
    set_fprev(bp, NULL);
    if (free_head) set_fprev(free_head, bp);
    free_head = bp;
}
static void fl_remove(char *bp) {
    if (g_kind != KIND_EXPLICIT) return;
    char *n = fnext(bp), *p = fprev(bp);
    if (rover == bp) rover = n;                    /* next fit 포인터가 사라지는 블록을 가리키면 다음으로 */
    if (p) set_fnext(p, n); else free_head = n;
    if (n) set_fprev(n, p);
}

/* ===== TODO B1: boundary tag 병합 =====
 * bp = 방금 free 표시된 블록 (리스트에 아직 없음). 이웃이 free 면 리스트에서 빼고 합친다.
 * 경우 1: 앞 할당·뒤 할당  2: 앞 할당·뒤 free  3: 앞 free·뒤 할당  4: 둘 다 free
 * g_st.coal[경우-1]++ 하고, 합쳐진 블록(리스트에 넣지 않은 상태)을 돌려준다. */
static char *coalesce(char *bp) {
    /* TODO B1 */
    return bp;                                  /* 병합 안 함 — 틀렸지만 안전 */
}

/* ===== TODO A1: 암시적 리스트 first fit =====
 * prologue 다음 블록부터 epilogue(크기 0) 전까지 주소 순서로 걷는다. 할당된 블록도 지나가야 한다.
 * 본 블록마다 g_st.scans++. 처음 맞는 free 블록(크기 ≥ asize)을 돌려주고, 없으면 NULL. */
static char *implicit_find_fit(size_t asize) {
    /* TODO A1 */
    (void)asize;
    return NULL;                                /* 재사용 안 함 → 매번 힙이 자란다 */
}

/* ===== TODO B2: 명시적 리스트 fit — g_fit 에 따라 =====
 * FIT_FIRST: 머리부터 처음 맞는 것. FIT_BEST: 맞는 것 중 가장 작은 것 (같으면 먼저 본 것, 딱 맞으면 즉시).
 * FIT_NEXT : rover(없으면 머리)부터 끝까지, 다시 머리부터 rover 직전까지. 찾으면 rover = 찾은 블록의 다음.
 * 리스트 노드를 볼 때마다 g_st.scans++. 없으면 NULL. */
static char *explicit_find_fit(size_t asize) {
    /* TODO B2 */
    (void)asize;
    return NULL;
}

/* ===== TODO A2: 배치 + 분할 =====
 * bp = free 블록 (호출자가 이미 리스트에서 뺐음), 크기 ≥ asize.
 * 남는 부분이 MIN_BLOCK 이상이면 앞 asize 를 할당, 뒤를 새 free 블록으로 만들어 fl_insert, g_st.split++.
 * 아니면 블록 전체를 할당 (내부 단편화로 남김). */
static void place(char *bp, size_t asize) {
    /* TODO A2 */
    (void)asize;
    set_block(bp, blk_size(bp), 1);             /* 분할 안 함 */
}

/* ===== 공개 API (주어짐: init / malloc / free) ===== */
static int mm_init(void) {
    if (mem_init() < 0) return -1;
    char *p = mem_sbrk(4 * WSIZE);
    if (!p) return -1;
    wr(p, 0);                          /* 정렬용 패딩 */
    wr(p + WSIZE, pack(DSIZE, 1));     /* prologue hdr */
    wr(p + 2 * WSIZE, pack(DSIZE, 1)); /* prologue ftr */
    wr(p + 3 * WSIZE, pack(0, 1));     /* epilogue hdr */
    heap_listp = p + 2 * WSIZE;
    free_head = NULL;
    rover = NULL;
    memset(&g_st, 0, sizeof g_st);
    return 0;
}

static char *extend_heap(size_t bytes) {         /* bytes = 16 의 배수, ≥ MIN_BLOCK */
    char *bp = mem_sbrk((intptr_t)bytes);
    if (!bp) return NULL;
    set_block(bp, bytes, 0);                     /* 옛 epilogue hdr 자리가 새 블록 hdr */
    wr(hdrp(next_blkp(bp)), pack(0, 1));         /* 새 epilogue */
    g_st.extend++;
    bp = coalesce(bp);                           /* 힙 끝의 free 블록과 합친다 */
    fl_insert(bp);
    return bp;
}

static void *mm_malloc(size_t size) {
    if (size == 0 || size > MEM_MAX) return NULL;
    size_t asize = adjust(size);
    g_st.mallocs++;
    char *bp = (g_kind == KIND_IMPLICIT) ? implicit_find_fit(asize) : explicit_find_fit(asize);
    if (!bp && !(bp = extend_heap(asize))) return NULL;
    fl_remove(bp);
    place(bp, asize);
    return bp;
}

static void mm_free(void *ptr) {
    if (!ptr) return;
    char *bp = ptr;
    set_block(bp, blk_size(bp), 0);
    fl_insert(coalesce(bp));
}

/* ===== TODO C1: realloc =====
 * ptr == NULL → mm_malloc(size). size == 0 → mm_free(ptr), NULL (이 레슨의 선택).
 * asize ≤ 현재 크기: 남는 부분 ≥ MIN_BLOCK 이면 잘라서 free (뒤 이웃과 병합), g_st.shrink_split++. 같은 ptr.
 * 다음 블록이 free 이고 합치면 충분: 리스트에서 빼고 흡수 → place 로 다시 분할, g_st.grow_next++. 같은 ptr.
 * 힙 끝(다음 = epilogue, 또는 다음 = free 이고 그 다음 = epilogue): 모자란 만큼만 mem_sbrk 해서
 *   제자리 확장 (최소 MIN_BLOCK), g_st.grow_extend++. 같은 ptr.
 * 그 밖: mm_malloc → memcpy(min(size, 옛 payload 용량)) → mm_free, g_st.moved++.
 * 실패하면 NULL 이고 옛 블록은 그대로 (C 표준 realloc 과 같은 약속). */
static void *mm_realloc(void *ptr, size_t size) {
    /* TODO C1 */
    (void)ptr; (void)size;
    return NULL;
}

/* ===== TODO B3: 힙 검사기 — 위반 개수를 돌려준다 (0 = 정상) =====
 * 힙 걷기: prologue (16|1), 블록마다 payload 16B 정렬 · 크기 16 배수 ≥ MIN_BLOCK · 힙 안 · hdr == ftr,
 *          free 블록 둘이 붙어 있으면 위반 (병합 누락), epilogue (0|1) 가 정확히 break 위치.
 *          크기가 망가져서 더 걸을 수 없으면 거기서 멈춘다 (절대 크래시·무한 루프 금지).
 * 명시적 모드: 리스트의 모든 노드가 힙 안 · 16B 정렬 · free 표시 · prev 링크 일치,
 *          노드 수 == 힙 걷기의 free 블록 수, 블록 수보다 많이 돌면 순환으로 보고 멈춘다.
 * verbose 면 위반마다 한 줄 출력. */
static int mm_check(int verbose) {
    /* TODO B3 */
    (void)verbose;
    return 0;
}

/* ===== 공유 트레이스 (OS-M04/M05/M06 이 같은 생성기를 쓴다) ===== */
typedef struct { char op; int id; uint32_t size; } op_t;          /* op: 'a' alloc, 'f' free, 'r' realloc */
typedef struct { const char *name; op_t *ops; int n, cap, max_id; } trace_t;

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void tpush(trace_t *t, char op, int id, uint32_t size) {
    if (t->n == t->cap) {
        t->cap = t->cap ? 2 * t->cap : 1024;
        op_t *nops = realloc(t->ops, (size_t)t->cap * sizeof *t->ops);   /* 하네스: 진짜 realloc */
        if (!nops) { fprintf(stderr, "out of memory\n"); exit(2); }
        t->ops = nops;
    }
    t->ops[t->n++] = (op_t){ op, id, size };
    if (id > t->max_id) t->max_id = id;
}
/* T1/T2: live < 50 이면 무조건 alloc, 아니면 p=0.6 alloc / 0.4 임의 live free. 끝에 남은 것 전부 free. */
static void gen_mix(trace_t *t, uint32_t seed, int nops, int mixed) {
    uint32_t s = seed;
    int *live = malloc((size_t)nops * sizeof *live), nlive = 0, next_id = 0;
    for (int i = 0; i < nops; i++) {
        bool alloc = nlive < 50 || xorshift32(&s) % 100 < 60;
        if (alloc) {
            uint32_t size;
            if (!mixed) size = 1 + xorshift32(&s) % 128;
            else {
                uint32_t r = xorshift32(&s) % 100;
                if (r < 70) size = 1 + xorshift32(&s) % 256;
                else if (r < 95) size = 257 + xorshift32(&s) % (4096 - 256);
                else size = 4097 + xorshift32(&s) % (65536 - 4096);
            }
            tpush(t, 'a', next_id, size);
            live[nlive++] = next_id++;
        } else {
            int j = (int)(xorshift32(&s) % (uint32_t)nlive);
            tpush(t, 'f', live[j], 0);
            live[j] = live[--nlive];
        }
    }
    while (nlive) tpush(t, 'f', live[--nlive], 0);
    free(live);
}
/* T3: 버퍼 200개 (id 0..199) 를 +1..+512 씩 realloc 으로 목표(≤ 32 KiB)까지 키운다.
 *     realloc 한 번마다 25% 작은 alloc(1..64), 20% 작은 것 하나 free. 끝에 작은 것 → 버퍼 순으로 free. */
static void gen_realloc(trace_t *t, uint32_t seed) {
    enum { NB = 200 };
    uint32_t s = seed, cur[NB], target[NB];
    int grow[NB], ngrow = 0, next_id = NB;
    int *small = malloc(65536 * sizeof *small), nsmall = 0;
    for (int b = 0; b < NB; b++) {
        target[b] = 1 + xorshift32(&s) % 32768;
        uint32_t first = 1 + xorshift32(&s) % 512;
        cur[b] = first < target[b] ? first : target[b];
        tpush(t, 'a', b, cur[b]);
        if (cur[b] < target[b]) grow[ngrow++] = b;
    }
    while (ngrow) {
        int j = (int)(xorshift32(&s) % (uint32_t)ngrow), b = grow[j];
        uint32_t nsz = cur[b] + 1 + xorshift32(&s) % 512;
        cur[b] = nsz < target[b] ? nsz : target[b];
        tpush(t, 'r', b, cur[b]);
        if (cur[b] == target[b]) grow[j] = grow[--ngrow];
        uint32_t r = xorshift32(&s) % 100;
        if (r < 25 && nsmall < 65536) {
            tpush(t, 'a', next_id, 1 + xorshift32(&s) % 64);
            small[nsmall++] = next_id++;
        } else if (r < 45 && nsmall) {
            int k = (int)(xorshift32(&s) % (uint32_t)nsmall);
            tpush(t, 'f', small[k], 0);
            small[k] = small[--nsmall];
        }
    }
    while (nsmall) tpush(t, 'f', small[--nsmall], 0);
    for (int b = 0; b < NB; b++) tpush(t, 'f', b, 0);
    free(small);
}
/* T4: CS:APP binary-bal 스타일 — 64,448 을 500쌍 → 448 전부 free → 512 를 500개 → 전부 free */
static void gen_binary(trace_t *t) {
    for (int k = 0; k < 500; k++) { tpush(t, 'a', 2 * k, 64); tpush(t, 'a', 2 * k + 1, 448); }
    for (int k = 0; k < 500; k++) tpush(t, 'f', 2 * k + 1, 0);
    for (int k = 0; k < 500; k++) tpush(t, 'a', 1000 + k, 512);
    for (int k = 0; k < 500; k++) tpush(t, 'f', 2 * k, 0);
    for (int k = 0; k < 500; k++) tpush(t, 'f', 1000 + k, 0);
}
enum { NTRACE = 4 };
static trace_t g_tr[NTRACE];
static void make_traces(void) {
    if (g_tr[0].n) return;
    g_tr[0].name = "T1 small-uniform"; gen_mix(&g_tr[0], 1, 2000, 0);
    g_tr[1].name = "T2 mixed";         gen_mix(&g_tr[1], 2, 2000, 1);
    g_tr[2].name = "T3 realloc-grow";  gen_realloc(&g_tr[2], 3);
    g_tr[3].name = "T4 binary-pattern"; gen_binary(&g_tr[3]);
}

/* ===== 재생기: 패턴 채우기/검사 + mm_check 주기 + 활용률 ===== */
typedef struct {
    long ops, fails, corrupt, misaligned, check_viol;
    size_t peak_live, heap_hw;
    double util, scans_per_malloc;
} replay_t;

static inline uint8_t pat(int id, size_t i) { return (uint8_t)((unsigned)id * 167u + (unsigned)i * 13u + 1u); }
static bool fill_ok(const uint8_t *p, int id, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) if (p[i] != pat(id, i)) return false;
    return true;
}
static void fill(uint8_t *p, int id, size_t from, size_t to) { for (size_t i = from; i < to; i++) p[i] = pat(id, i); }

static void replay(const trace_t *t, int kind, int fit, int check_every, bool verify, replay_t *r) {
    memset(r, 0, sizeof *r);
    mm_config(kind, fit);
    if (mm_init() < 0) { r->fails = 1; return; }
    uint8_t **ptr = calloc((size_t)t->max_id + 1, sizeof *ptr);
    size_t *sz = calloc((size_t)t->max_id + 1, sizeof *sz);
    size_t live = 0;
    for (int i = 0; i < t->n; i++) {
        const op_t *o = &t->ops[i];
        if (o->op == 'a') {
            uint8_t *p = mm_malloc(o->size);
            if (!p) { r->fails++; continue; }
            if ((uintptr_t)p % 16) r->misaligned++;
            if (verify) fill(p, o->id, 0, o->size);
            ptr[o->id] = p; sz[o->id] = o->size; live += o->size;
        } else if (o->op == 'r') {
            if (!ptr[o->id]) { r->fails++; continue; }
            size_t old = sz[o->id];
            if (verify && !fill_ok(ptr[o->id], o->id, 0, old)) r->corrupt++;
            uint8_t *p = mm_realloc(ptr[o->id], o->size);
            if (!p) { r->fails++; continue; }               /* 옛 블록은 그대로 살아 있다 */
            if ((uintptr_t)p % 16) r->misaligned++;
            size_t keep = old < o->size ? old : o->size;
            if (verify) {
                if (!fill_ok(p, o->id, 0, keep)) r->corrupt++;
                fill(p, o->id, keep, o->size);
            }
            ptr[o->id] = p; live = live - old + o->size; sz[o->id] = o->size;
        } else {
            if (!ptr[o->id]) continue;
            if (verify && !fill_ok(ptr[o->id], o->id, 0, sz[o->id])) r->corrupt++;
            mm_free(ptr[o->id]);
            live -= sz[o->id]; ptr[o->id] = NULL; sz[o->id] = 0;
        }
        if (live > r->peak_live) r->peak_live = live;
        r->ops++;
        if (check_every && i % check_every == 0) r->check_viol += mm_check(0);
    }
    if (check_every) r->check_viol += mm_check(0);
    r->heap_hw = mem_heapsize();
    r->util = r->heap_hw ? (double)r->peak_live / (double)r->heap_hw : 0.0;
    r->scans_per_malloc = g_st.mallocs ? (double)g_st.scans / (double)g_st.mallocs : 0.0;
    free(ptr); free(sz);
}
static bool replay_ok(const replay_t *r) {
    return r->fails == 0 && r->corrupt == 0 && r->misaligned == 0 && r->check_viol == 0;
}

/* ===== 손으로 만든 힙 상황용 도우미 ===== */
static size_t list_len(void) { size_t n = 0; for (char *p = free_head; p && n < 100000; p = fnext(p)) n++; return n; }
static size_t heap_free_blocks(void) {
    size_t n = 0;
    for (char *bp = heap_listp + DSIZE; blk_size(bp) > 0; bp = next_blkp(bp)) n += !blk_alloc(bp);
    return n;
}

#ifndef MM_NO_MAIN
int main(void) {
    make_traces();

    printf("== Part A: implicit list, first fit ==\n");
    mm_config(KIND_IMPLICIT, FIT_FIRST);
    mm_init();
    CHECK(mm_malloc(0) == NULL);
    mm_free(NULL);                                         /* 아무 일도 없어야 한다 */
    char *a = mm_malloc(100), *b = mm_malloc(100), *c = mm_malloc(100), *d = mm_malloc(100);
    CHECK(a && b && c && d);
    CHECK((uintptr_t)a % 16 == 0 && (uintptr_t)b % 16 == 0);
    CHECK(blk_size(a) == 128 && b == a + 128 && adjust(1) == 32 && adjust(16) == 32 && adjust(17) == 48);
    CHECK(prev_blkp(b) == a && next_blkp(a) == b && rd(hdrp(a)) == rd(ftrp(a)));   /* boundary tag */
    mm_free(a);
    mm_free(c);
    size_t hw = mem_heapsize();
    char *e = mm_malloc(50);                               /* first fit → a 자리, 분할 */
    CHECK(e == a);
    CHECK(blk_size(e) == 80 && !blk_alloc(e + 80) && blk_size(e + 80) == 48);
    CHECK(mem_heapsize() == hw);                           /* 힙이 자라지 않았다 */
    char *f = mm_malloc(90);                               /* asize 112, c(128) 남는 16 < 32 → 분할 안 함 */
    CHECK(f == c && blk_size(f) == 128 && g_st.split == 1);
    CHECK(mm_check(0) == 0);
    replay_t ri[NTRACE];
    for (int k = 0; k < NTRACE; k++) {
        if (k == 2) continue;                              /* T3 은 realloc — Part C */
        replay(&g_tr[k], KIND_IMPLICIT, FIT_FIRST, 50, true, &ri[k]);
        printf("  [implicit first] %-18s ops=%-6ld U=%.3f heap=%zu B scans/malloc=%.1f\n",
               g_tr[k].name, ri[k].ops, ri[k].util, ri[k].heap_hw, ri[k].scans_per_malloc);
        CHECK(replay_ok(&ri[k]));
    }
    CHECK(ri[0].util > 0.65 && ri[1].util > 0.85);           /* 주소 순서 first fit 은 꽤 촘촘하다 */

    printf("== Part B: explicit list — coalesce 4 cases ==\n");
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    char *x[7];
    for (int i = 0; i < 7; i++) x[i] = mm_malloc(64);     /* 블록 80B 씩 이어 붙음 */
    CHECK(x[6] == x[0] + 6 * 80 && list_len() == 0);
    memset(&g_st, 0, sizeof g_st);                         /* extend_heap 의 병합(경우 1)은 빼고 센다 */
    mm_free(x[1]);                                         /* 경우 1 */
    CHECK(g_st.coal[0] == 1 && list_len() == 1 && blk_size(x[1]) == 80);
    mm_free(x[4]);                                         /* 경우 1 */
    mm_free(x[3]);                                         /* 경우 2: 뒤(x4) free */
    CHECK(g_st.coal[1] == 1 && list_len() == 2 && blk_size(x[3]) == 160);
    mm_free(x[5]);                                         /* 경우 3: 앞(x3+x4) free */
    CHECK(g_st.coal[2] == 1 && list_len() == 2 && blk_size(x[3]) == 240);
    mm_free(x[2]);                                         /* 경우 4: 앞 x1, 뒤 x3..x5 */
    CHECK(g_st.coal[3] == 1 && list_len() == 1 && blk_size(x[1]) == 400 && free_head == x[1]);
    CHECK(mm_check(1) == 0);

    printf("== Part B: fit policies (LIFO list) ==\n");
    char *chosen[FIT_COUNT][2];
    for (int fit = 0; fit < FIT_COUNT; fit++) {
        mm_config(KIND_EXPLICIT, fit);
        mm_init();
        char *f1 = mm_malloc(200), *g1 = mm_malloc(8), *f2 = mm_malloc(100), *g2 = mm_malloc(8);
        char *f3 = mm_malloc(300), *g3 = mm_malloc(8);
        (void)g1; (void)g2; (void)g3;
        mm_free(f3); mm_free(f2); mm_free(f1);             /* 리스트: f1(224) → f2(128) → f3(320) */
        chosen[fit][0] = mm_malloc(90);                    /* asize 112 */
        chosen[fit][1] = mm_malloc(90);
        CHECK(mm_check(0) == 0);
        if (fit == FIT_FIRST) CHECK(chosen[fit][0] == f1 && chosen[fit][1] == f1 + 112);
        if (fit == FIT_BEST)  CHECK(chosen[fit][0] == f2 && chosen[fit][1] == f1);
        if (fit == FIT_NEXT)  CHECK(chosen[fit][0] == f1 && chosen[fit][1] == f2);
    }

    printf("== Part B: mm_check catches injected corruption ==\n");
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    char *p[6];
    for (int i = 0; i < 6; i++) p[i] = mm_malloc(100);
    mm_free(p[1]); mm_free(p[3]);
    CHECK(mm_check(0) == 0);
    size_t saved = rd(ftrp(p[3]));
    wr(ftrp(p[3]), pack(64, 0));                           /* ① footer 망가뜨림 */
    CHECK(mm_check(1) > 0);
    wr(ftrp(p[3]), saved);
    set_block(p[3], blk_size(p[3]), 1);                    /* ② 리스트에 있는데 할당 표시 */
    CHECK(mm_check(1) >= 2);
    set_block(p[3], blk_size(p[3]), 0);
    set_block(p[2], blk_size(p[2]), 0);                    /* ③ free 표시만 하고 병합·삽입 안 함 */
    CHECK(mm_check(1) >= 2);
    set_block(p[2], blk_size(p[2]), 1);
    char *saved_next = fnext(p[3]);
    set_fnext(p[3], (char *)&saved);                       /* ④ 리스트 포인터가 힙 밖 (스택) */
    CHECK(mm_check(1) > 0);
    set_fnext(p[3], saved_next);
    set_fnext(p[1], p[3]); set_fnext(p[3], p[1]);          /* ⑤ 순환 — 멈추지 않고 보고해야 함 */
    CHECK(mm_check(1) > 0);
    free_head = NULL; set_fnext(p[3], NULL); set_fprev(p[3], NULL); fl_insert(p[3]); fl_insert(p[1]);
    CHECK(mm_check(1) == 0);                               /* 복구 후 다시 정상 */

    printf("== Part B: traces x fit policy (explicit) ==\n");
    replay_t re[FIT_COUNT][NTRACE];
    bool all_ok = true;
    for (int fit = 0; fit < FIT_COUNT; fit++)
        for (int k = 0; k < NTRACE; k++) {
            replay(&g_tr[k], KIND_EXPLICIT, fit, 50, true, &re[fit][k]);
            printf("  [explicit %-5s] %-18s ops=%-6ld U=%.3f heap=%zu B scans/malloc=%.1f\n",
                   fit_name[fit], g_tr[k].name, re[fit][k].ops, re[fit][k].util, re[fit][k].heap_hw,
                   re[fit][k].scans_per_malloc);
            all_ok = all_ok && replay_ok(&re[fit][k]);
        }
    CHECK(all_ok);
    CHECK(re[FIT_FIRST][1].scans_per_malloc < ri[1].scans_per_malloc);   /* free 블록만 본다 */
    CHECK(re[FIT_BEST][1].util >= re[FIT_FIRST][1].util && re[FIT_BEST][0].util >= re[FIT_NEXT][0].util);
    CHECK(ri[1].util > re[FIT_FIRST][1].util + 0.1);      /* LIFO first fit < 주소순 first fit (T2) */
    CHECK(re[FIT_FIRST][3].util < 0.60 && re[FIT_FIRST][3].util > 0.50);  /* binary 패턴 = 단편화 */

    printf("== Part C: realloc ==\n");
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    uint8_t *r0 = mm_realloc(NULL, 40);                    /* = malloc */
    CHECK(r0 && blk_size(r0) == 64);
    CHECK(mm_realloc(r0, 0) == NULL && list_len() == 1 && heap_free_blocks() == 1);   /* = free */
    mm_init();
    uint8_t *r1 = mm_malloc(1000), *g4 = mm_malloc(8);
    fill(r1, 7, 0, 1000);
    uint8_t *r2 = mm_realloc(r1, 100);                     /* 축소 + 분할: 1024 → 128 + free 896 */
    CHECK(r2 == r1 && blk_size(r1) == 128 && g_st.shrink_split == 1 && fill_ok(r1, 7, 0, 100));
    CHECK(!blk_alloc(r1 + 128) && blk_size(r1 + 128) == 896);
    CHECK(mm_realloc(r1, 90) == r1 && blk_size(r1) == 128);  /* asize 112, 남는 16 → 그대로 */
    uint8_t *r3 = mm_realloc(r1, 500);                     /* 다음 free 블록 흡수 후 다시 분할 */
    CHECK(r3 == r1 && g_st.grow_next == 1 && blk_size(r1) == 528 && fill_ok(r1, 7, 0, 90));
    CHECK(!blk_alloc(r1 + 528) && blk_size(r1 + 528) == 496 && mm_check(1) == 0);
    (void)g4;
    mm_init();
    uint8_t *tail = mm_malloc(200);                        /* 힙 끝 블록 (224) */
    fill(tail, 9, 0, 200);
    size_t before = mem_heapsize();
    uint8_t *t2 = mm_realloc(tail, 5000);                  /* 다음 = epilogue → 모자란 4800 B 만 sbrk */
    CHECK(t2 == tail && g_st.grow_extend == 1 && fill_ok(tail, 9, 0, 200));
    CHECK(mem_heapsize() - before == 4800 && blk_size(tail) == 5024 && mm_check(1) == 0);
    mm_init();
    uint8_t *m1 = mm_malloc(64), *m2 = mm_malloc(64);
    fill(m1, 3, 0, 64);
    uint8_t *m3 = mm_realloc(m1, 3000);                    /* 이웃이 할당, 힙 끝도 아님 → 이동 */
    CHECK(m3 && m3 != m1 && g_st.moved == 1 && fill_ok(m3, 3, 0, 64));
    CHECK(!blk_alloc(m1) && mm_check(1) == 0);
    (void)m2;

    printf("== Part C: out of memory on a small fixed heap (MCU-style) ==\n");
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    mem_set_limit(16 * 1024);                              /* _heap_start.._heap_end 가 16 KB 인 보드 */
    char *blk[64];
    int got = 0;
    while (got < 64 && (blk[got] = mm_malloc(1000))) got++;
    CHECK(got == 15 && mm_check(1) == 0);                   /* 32 + 15·1024 = 15392 ≤ 16384 < 16416 */
    fill((uint8_t *)blk[0], 1, 0, 1000);
    CHECK(mm_realloc(blk[0], 8000) == NULL && fill_ok((uint8_t *)blk[0], 1, 0, 1000));  /* 실패해도 원본 유지 */
    for (int i = 0; i < got; i++) mm_free(blk[i]);
    CHECK(list_len() == 1 && mm_malloc(15000) != NULL && mm_check(1) == 0);  /* 전부 병합 → 큰 요청 성공 */

    printf("== Part C: T3 realloc-grow x fit policy ==\n");
    replay_t r3t[FIT_COUNT];
    size_t grow_inplace[FIT_COUNT], moved[FIT_COUNT];
    for (int fit = 0; fit < FIT_COUNT; fit++) {
        replay(&g_tr[2], KIND_EXPLICIT, fit, 50, true, &r3t[fit]);
        grow_inplace[fit] = g_st.grow_next + g_st.grow_extend;
        moved[fit] = g_st.moved;
        printf("  [explicit %-5s] T3 ops=%ld U=%.3f grow_next+extend=%zu moved=%zu\n", fit_name[fit],
               r3t[fit].ops, r3t[fit].util, grow_inplace[fit], moved[fit]);
        CHECK(replay_ok(&r3t[fit]));
        CHECK(grow_inplace[fit] > moved[fit]);             /* 대부분 제자리로 커진다 */
    }
    replay(&g_tr[2], KIND_IMPLICIT, FIT_FIRST, 50, true, &ri[2]);   /* 기준선도 T3 를 돈다 */
    printf("  [implicit first] T3 ops=%ld U=%.3f grow_next+extend=%zu moved=%zu\n", ri[2].ops, ri[2].util,
           g_st.grow_next + g_st.grow_extend, g_st.moved);
    CHECK(replay_ok(&ri[2]) && ri[2].util > r3t[FIT_FIRST].util);  /* 주소순 > LIFO, 여기서 가장 크게 */

    printf("== utilization summary (U = peak live payload / heap high-water) ==\n");
    printf("  %-18s %9s %9s %9s %9s\n", "trace", "impl-1st", "exp-first", "exp-next", "exp-best");
    for (int k = 0; k < NTRACE; k++) {
        printf("  %-18s %9.3f", g_tr[k].name, ri[k].util);
        for (int fit = 0; fit < FIT_COUNT; fit++) printf(" %9.3f", re[fit][k].util);
        printf("\n");
    }

    for (int k = 0; k < NTRACE; k++) free(g_tr[k].ops);
    mem_deinit();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
