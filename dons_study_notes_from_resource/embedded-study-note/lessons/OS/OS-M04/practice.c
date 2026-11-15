/* OS-M04 추가 연습 — 한 번에 검증 (solution.c 의 할당기를 그대로 담고, 연습 1~4 답을 더했다)
 *
 *   연습 1  mm_calloc: n*size 곱셈 오버플로 → NULL, 재사용 블록도 0 으로
 *   연습 2  mm_realloc_prev: 앞 free 블록까지 흡수 (memmove) — 이동 없이 커지기
 *   연습 3  주소 순서 명시적 리스트: 암시적 first fit 과 같은 블록을 고르는지 (U 가 정확히 같아야 함)
 *   연습 4  OSTEP vm-freespace/malloc.py 모델 재현: FIRST/BEST/WORST, ADDRSORT, 병합 없음
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE           /* MAP_ANON (macOS, -std=c11) */
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
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
static bool g_addr_order;          /* 연습 3: true 면 주소 순서로 삽입 (O(리스트 길이)) */
static void fl_insert(char *bp) {
    if (g_kind != KIND_EXPLICIT) return;
    if (g_addr_order) {
        char *prev = NULL, *cur = free_head;
        while (cur && (uintptr_t)cur < (uintptr_t)bp) { prev = cur; cur = fnext(cur); }
        set_fnext(bp, cur);
        set_fprev(bp, prev);
        if (prev) set_fnext(prev, bp); else free_head = bp;
        if (cur) set_fprev(cur, bp);
        return;
    }
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

/* ===== B1: boundary tag 병합 =====
 * bp = 방금 free 표시된 블록 (리스트에 아직 없음). 이웃이 free 면 리스트에서 빼고 합친다.
 * 경우 1: 앞 할당·뒤 할당  2: 앞 할당·뒤 free  3: 앞 free·뒤 할당  4: 둘 다 free
 * g_st.coal[경우-1]++ 하고, 합쳐진 블록(리스트에 넣지 않은 상태)을 돌려준다. */
static char *coalesce(char *bp) {
    char *prev = prev_blkp(bp), *next = next_blkp(bp);
    int pa = blk_alloc(prev), na = blk_alloc(next);
    size_t size = blk_size(bp);
    if (pa && na) {
        g_st.coal[0]++;
        return bp;
    }
    if (pa && !na) {
        fl_remove(next);
        size += blk_size(next);
        g_st.coal[1]++;
    } else if (!pa && na) {
        fl_remove(prev);
        size += blk_size(prev);
        bp = prev;
        g_st.coal[2]++;
    } else {
        fl_remove(prev);
        fl_remove(next);
        size += blk_size(prev) + blk_size(next);
        bp = prev;
        g_st.coal[3]++;
    }
    set_block(bp, size, 0);
    return bp;
}

/* ===== A1: 암시적 리스트 first fit =====
 * prologue 다음 블록부터 epilogue(크기 0) 전까지 주소 순서로 걷는다. 할당된 블록도 지나가야 한다.
 * 본 블록마다 g_st.scans++. 처음 맞는 free 블록(크기 ≥ asize)을 돌려주고, 없으면 NULL. */
static char *implicit_find_fit(size_t asize) {
    for (char *bp = next_blkp(heap_listp); blk_size(bp) > 0; bp = next_blkp(bp)) {
        g_st.scans++;
        if (!blk_alloc(bp) && blk_size(bp) >= asize) return bp;
    }
    return NULL;
}

/* ===== B2: 명시적 리스트 fit — g_fit 에 따라 =====
 * FIT_FIRST: 머리부터 처음 맞는 것. FIT_BEST: 맞는 것 중 가장 작은 것 (같으면 먼저 본 것, 딱 맞으면 즉시).
 * FIT_NEXT : rover(없으면 머리)부터 끝까지, 다시 머리부터 rover 직전까지. 찾으면 rover = 찾은 블록의 다음.
 * 리스트 노드를 볼 때마다 g_st.scans++. 없으면 NULL. */
static char *explicit_find_fit(size_t asize) {
    char *bp;
    if (g_fit == FIT_FIRST) {
        for (bp = free_head; bp; bp = fnext(bp)) {
            g_st.scans++;
            if (blk_size(bp) >= asize) return bp;
        }
        return NULL;
    }
    if (g_fit == FIT_BEST) {
        char *best = NULL;
        size_t best_sz = 0;
        for (bp = free_head; bp; bp = fnext(bp)) {
            g_st.scans++;
            size_t s = blk_size(bp);
            if (s >= asize && (!best || s < best_sz)) {
                best = bp;
                best_sz = s;
                if (s == asize) break;
            }
        }
        return best;
    }
    char *start = rover ? rover : free_head;
    for (bp = start; bp; bp = fnext(bp)) {
        g_st.scans++;
        if (blk_size(bp) >= asize) goto found;
    }
    for (bp = free_head; bp && bp != start; bp = fnext(bp)) {
        g_st.scans++;
        if (blk_size(bp) >= asize) goto found;
    }
    return NULL;
found:
    rover = fnext(bp);
    return bp;
}

/* ===== A2: 배치 + 분할 =====
 * bp = free 블록 (호출자가 이미 리스트에서 뺐음), 크기 ≥ asize.
 * 남는 부분이 MIN_BLOCK 이상이면 앞 asize 를 할당, 뒤를 새 free 블록으로 만들어 fl_insert, g_st.split++.
 * 아니면 블록 전체를 할당 (내부 단편화로 남김). */
static void place(char *bp, size_t asize) {
    size_t csize = blk_size(bp);
    if (csize - asize >= MIN_BLOCK) {
        set_block(bp, asize, 1);
        char *rem = next_blkp(bp);
        set_block(rem, csize - asize, 0);
        fl_insert(rem);
        g_st.split++;
    } else {
        set_block(bp, csize, 1);
    }
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

/* ===== C1: realloc =====
 * ptr == NULL → mm_malloc(size). size == 0 → mm_free(ptr), NULL (이 레슨의 선택).
 * asize ≤ 현재 크기: 남는 부분 ≥ MIN_BLOCK 이면 잘라서 free (뒤 이웃과 병합), g_st.shrink_split++. 같은 ptr.
 * 다음 블록이 free 이고 합치면 충분: 리스트에서 빼고 흡수 → place 로 다시 분할, g_st.grow_next++. 같은 ptr.
 * 힙 끝(다음 = epilogue, 또는 다음 = free 이고 그 다음 = epilogue): 모자란 만큼만 mem_sbrk 해서
 *   제자리 확장 (최소 MIN_BLOCK), g_st.grow_extend++. 같은 ptr.
 * 그 밖: mm_malloc → memcpy(min(size, 옛 payload 용량)) → mm_free, g_st.moved++.
 * 실패하면 NULL 이고 옛 블록은 그대로 (C 표준 realloc 과 같은 약속). */
static void *mm_realloc(void *ptr, size_t size) {
    if (!ptr) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }
    if (size > MEM_MAX) return NULL;
    char *bp = ptr;
    size_t asize = adjust(size), csize = blk_size(bp);
    if (asize <= csize) {
        if (csize - asize >= MIN_BLOCK) {
            set_block(bp, asize, 1);
            char *rem = next_blkp(bp);
            set_block(rem, csize - asize, 0);
            fl_insert(coalesce(rem));
            g_st.shrink_split++;
        }
        return bp;
    }
    char *next = next_blkp(bp);
    size_t nsize = blk_size(next);
    int nfree = !blk_alloc(next);
    if (nfree && csize + nsize >= asize) {
        fl_remove(next);
        set_block(bp, csize + nsize, 1);
        place(bp, asize);
        g_st.grow_next++;
        return bp;
    }
    if (nsize == 0 || (nfree && blk_size(next_blkp(next)) == 0)) {
        size_t have = csize + (nfree ? nsize : 0);
        size_t need = asize - have;
        if (need < MIN_BLOCK) need = MIN_BLOCK;
        if (mem_sbrk((intptr_t)need)) {
            if (nfree) fl_remove(next);
            set_block(bp, have + need, 1);
            wr(hdrp(next_blkp(bp)), pack(0, 1));
            place(bp, asize);
            g_st.grow_extend++;
            return bp;
        }
    }
    void *np = mm_malloc(size);
    if (!np) return NULL;
    size_t old_payload = csize - DSIZE;
    memcpy(np, bp, size < old_payload ? size : old_payload);
    mm_free(bp);
    g_st.moved++;
    return np;
}

/* ===== B3: 힙 검사기 — 위반 개수를 돌려준다 (0 = 정상) =====
 * 힙 걷기: prologue (16|1), 블록마다 payload 16B 정렬 · 크기 16 배수 ≥ MIN_BLOCK · 힙 안 · hdr == ftr,
 *          free 블록 둘이 붙어 있으면 위반 (병합 누락), epilogue (0|1) 가 정확히 break 위치.
 *          크기가 망가져서 더 걸을 수 없으면 거기서 멈춘다 (절대 크래시·무한 루프 금지).
 * 명시적 모드: 리스트의 모든 노드가 힙 안 · 16B 정렬 · free 표시 · prev 링크 일치,
 *          노드 수 == 힙 걷기의 free 블록 수, 블록 수보다 많이 돌면 순환으로 보고 멈춘다.
 * verbose 면 위반마다 한 줄 출력. */
static int mm_check(int verbose) {
    int bad = 0;
#define BAD(...) do { bad++; if (verbose) { printf("  mm_check: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
    char *end = mem_heap_hi() + 1;
    uintptr_t lo_u = (uintptr_t)mem_heap_lo(), first_u = (uintptr_t)(heap_listp + DSIZE), end_u = (uintptr_t)end;
    if (size_at(hdrp(heap_listp)) != DSIZE || !alloc_at(hdrp(heap_listp)) ||
        rd(hdrp(heap_listp)) != rd(heap_listp))
        BAD("bad prologue");
    size_t nblocks = 0, nfree = 0;
    int prev_free = 0;
    char *bp = heap_listp + DSIZE;
    for (;;) {
        if ((uintptr_t)bp - WSIZE < lo_u || (uintptr_t)bp > end_u) { BAD("block %p outside heap", (void *)bp); return bad; }
        size_t s = blk_size(bp);
        if (s == 0) {
            if (!blk_alloc(bp)) BAD("epilogue not allocated");
            if (bp != end) BAD("epilogue at %p, break at %p", (void *)bp, (void *)end);
            break;
        }
        if ((uintptr_t)bp % DSIZE) BAD("payload %p not 16-aligned", (void *)bp);
        if (s % DSIZE || s < MIN_BLOCK || s > (size_t)(end - bp)) {
            BAD("block %p bad size %zu — stop walking", (void *)bp, s);
            return bad;
        }
        if (rd(hdrp(bp)) != rd(ftrp(bp))) BAD("block %p header != footer", (void *)bp);
        int f = !blk_alloc(bp);
        if (f && prev_free) BAD("block %p and previous are both free (not coalesced)", (void *)bp);
        prev_free = f;
        nfree += (size_t)f;
        nblocks++;
        bp = next_blkp(bp);
    }
    if (g_kind == KIND_EXPLICIT) {
        size_t nlist = 0;
        char *prev = NULL;
        for (char *p = free_head; p; p = fnext(p)) {
            if ((uintptr_t)p < first_u || (uintptr_t)p >= end_u || (uintptr_t)p % DSIZE) {
                BAD("list node %p outside heap", (void *)p);
                return bad;
            }
            if (blk_alloc(p)) BAD("list node %p is marked allocated", (void *)p);
            if (fprev(p) != prev) BAD("list node %p prev link broken", (void *)p);
            if (++nlist > nblocks) { BAD("free list longer than heap — cycle?"); return bad; }
            prev = p;
        }
        if (nlist != nfree) BAD("free list has %zu nodes, heap has %zu free blocks", nlist, nfree);
    }
#undef BAD
    return bad;
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


/* ===== 연습 1: calloc ===== */
static void *mm_calloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return NULL;        /* n*size 가 넘치면 거절 (작은 블록을 주면 큰일) */
    void *p = mm_malloc(n * size);
    if (p) memset(p, 0, n * size);                       /* 재사용 블록에는 옛 데이터가 있다 */
    return p;
}

/* ===== 연습 2: 앞 free 블록까지 흡수하는 realloc ===== */
static size_t g_grow_prev;
static void *mm_realloc_prev(void *ptr, size_t size) {
    if (!ptr || size == 0 || size > MEM_MAX) return mm_realloc(ptr, size);
    char *bp = ptr;
    size_t asize = adjust(size), csize = blk_size(bp);
    char *next = next_blkp(bp), *prev = prev_blkp(bp);
    size_t nfree = blk_alloc(next) ? 0 : blk_size(next);
    if (asize <= csize || csize + nfree >= asize || blk_alloc(prev)) return mm_realloc(ptr, size);  /* 기존 경로 */
    size_t total = blk_size(prev) + csize + nfree;
    if (total < asize) return mm_realloc(ptr, size);
    fl_remove(prev);
    if (nfree) fl_remove(next);
    memmove(prev, bp, csize - DSIZE);                    /* 겹치는 복사 → memcpy 금지 */
    set_block(prev, total, 1);                           /* hdr/ftr 는 복사가 끝난 뒤에 */
    place(prev, asize);
    g_grow_prev++;
    return prev;
}

/* ===== 연습 3: 리스트가 주소 순서인가 ===== */
static bool list_sorted_and_matches_heap(void) {
    char *walk = heap_listp + DSIZE, *node = free_head;
    for (; blk_size(walk) > 0; walk = next_blkp(walk)) {
        if (blk_alloc(walk)) continue;
        if (node != walk) return false;                  /* 힙 걷기의 free 블록 순서 == 리스트 순서 */
        node = fnext(node);
    }
    return node == NULL;
}

/* ===== 연습 4: OSTEP malloc.py 모델 (헤더 0, 정렬 없음, ADDRSORT, 병합 없음) ===== */
enum { OST_FIRST, OST_BEST, OST_WORST };
typedef struct { long addr, size; } seg_t;
typedef struct { seg_t fl[64]; int n; seg_t used[64]; int nused; int policy; } ost_t;
static void ost_init(ost_t *m, long base, long size, int policy) {
    m->fl[0] = (seg_t){ base, size }; m->n = 1; m->nused = 0; m->policy = policy;
}
static long ost_malloc(ost_t *m, long size, int *searched) {
    int best = -1;
    long best_sz = m->policy == OST_BEST ? LONG_MAX : -1;
    *searched = 0;
    for (int i = 0; i < m->n; i++) {                     /* malloc.py 처럼: BEST/WORST 는 끝까지 센다 */
        (*searched)++;
        long s = m->fl[i].size;
        if (s >= size && ((m->policy == OST_BEST && s < best_sz) || (m->policy == OST_WORST && s > best_sz) ||
                          m->policy == OST_FIRST)) {
            best = i; best_sz = s;
            if (m->policy == OST_FIRST) break;
        }
    }
    if (best < 0) return -1;
    long addr = m->fl[best].addr;
    if (best_sz > size) m->fl[best] = (seg_t){ addr + size, best_sz - size };
    else { for (int i = best; i + 1 < m->n; i++) m->fl[i] = m->fl[i + 1]; m->n--; }
    m->used[m->nused++] = (seg_t){ addr, size };
    return addr;
}
static void ost_free(ost_t *m, long addr) {
    for (int u = 0; u < m->nused; u++) {
        if (m->used[u].addr != addr) continue;
        seg_t s = m->used[u];
        m->used[u] = m->used[--m->nused];
        int i = m->n++;
        while (i > 0 && m->fl[i - 1].addr > s.addr) { m->fl[i] = m->fl[i - 1]; i--; }   /* 주소순 삽입 */
        m->fl[i] = s;
        return;
    }
}

int main(void) {
    make_traces();

    printf("== 연습 1: mm_calloc ==\n");
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    CHECK(mm_calloc(SIZE_MAX / 2 + 1, 2) == NULL && mm_calloc(2, SIZE_MAX / 2 + 1) == NULL);
    CHECK(mm_calloc(0, 8) == NULL && mm_calloc(8, 0) == NULL);   /* malloc(0) = NULL 과 같은 선택 */
    uint8_t *p = mm_malloc(100);
    memset(p, 0xAB, 100);
    mm_free(p);
    uint8_t *q = mm_calloc(10, 10);
    bool zero = q != NULL;
    for (int i = 0; q && i < 100; i++) zero = zero && q[i] == 0;
    CHECK(q == p && zero && mm_check(1) == 0);

    printf("== 연습 2: mm_realloc_prev ==\n");
    mm_init();
    uint8_t *a = mm_malloc(200), *b = mm_malloc(100), *g = mm_malloc(8);
    fill(b, 5, 0, 100);
    mm_free(a);                                          /* b 앞 = free 224 */
    uint8_t *nb = mm_realloc_prev(b, 250);               /* asize 272 ≤ 224 + 128 */
    CHECK(nb == a && g_grow_prev == 1 && fill_ok(nb, 5, 0, 100));
    CHECK(blk_size(nb) == 272 && !blk_alloc(nb + 272) && blk_size(nb + 272) == 80 && mm_check(1) == 0);
    (void)g;
    mm_init();
    uint8_t *c0 = mm_malloc(100), *c1 = mm_malloc(100), *c2 = mm_malloc(100), *c3 = mm_malloc(8);
    fill(c1, 6, 0, 100);
    mm_free(c0); mm_free(c2);                            /* 앞뒤 둘 다 free: 128 + 128 + 128 */
    uint8_t *nc = mm_realloc_prev(c1, 300);              /* asize 320 */
    CHECK(nc == c0 && fill_ok(nc, 6, 0, 100) && blk_size(nc) == 320 && blk_size(nc + 320) == 64);
    CHECK(mm_check(1) == 0 && list_len() == 1);
    (void)c3;
    mm_init();
    uint8_t *d0 = mm_malloc(100), *d1 = mm_malloc(100), *d2 = mm_malloc(8);
    fill(d1, 8, 0, 100);
    uint8_t *nd = mm_realloc_prev(d1, 300);              /* 앞이 할당 → 기존 경로 (이동) */
    CHECK(nd && nd != d1 && nd != d0 && fill_ok(nd, 8, 0, 100) && g_grow_prev == 2 && mm_check(1) == 0);
    (void)d2;
    replay_t rr;                                         /* T3 를 realloc_prev 로: 패턴 · mm_check 그대로 */
    {
        const trace_t *t = &g_tr[2];
        mm_config(KIND_EXPLICIT, FIT_FIRST);
        mm_init();
        uint8_t **ptr = calloc((size_t)t->max_id + 1, sizeof *ptr);
        size_t *sz = calloc((size_t)t->max_id + 1, sizeof *sz), live = 0, peak = 0;
        long bad = 0;
        g_grow_prev = 0;
        for (int i = 0; i < t->n; i++) {
            const op_t *o = &t->ops[i];
            if (o->op == 'a') { ptr[o->id] = mm_malloc(o->size); fill(ptr[o->id], o->id, 0, o->size); sz[o->id] = o->size; live += o->size; }
            else if (o->op == 'r') {
                size_t old = sz[o->id];
                uint8_t *np = mm_realloc_prev(ptr[o->id], o->size);
                if (!np || !fill_ok(np, o->id, 0, old)) { bad++; continue; }
                fill(np, o->id, old, o->size);
                ptr[o->id] = np; live += o->size - old; sz[o->id] = o->size;
            } else { if (!fill_ok(ptr[o->id], o->id, 0, sz[o->id])) bad++; mm_free(ptr[o->id]); live -= sz[o->id]; }
            if (live > peak) peak = live;
            if (i % 50 == 0) bad += mm_check(0);
        }
        rr.util = (double)peak / (double)mem_heapsize();
        replay_t base;
        replay(t, KIND_EXPLICIT, FIT_FIRST, 0, false, &base);
        printf("  T3 explicit first: U %.3f (mm_realloc) -> %.3f (mm_realloc_prev), grow_prev=%zu\n",
               base.util, rr.util, g_grow_prev);
        CHECK(bad == 0 && g_grow_prev > 0);
        free(ptr); free(sz);
    }

    printf("== 연습 3: address-ordered explicit list ==\n");
    g_addr_order = true;
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    char *blk[20];
    for (int i = 0; i < 20; i++) blk[i] = mm_malloc(16 + 16 * (size_t)i);
    uint32_t s = 7;
    for (int k = 0; k < 10; k++) {                       /* 홀수 블록을 뒤섞인 순서로 free */
        int i = 2 * (int)(xorshift32(&s) % 10) + 1;
        if (blk[i]) { mm_free(blk[i]); blk[i] = NULL; }
    }
    CHECK(list_sorted_and_matches_heap() && mm_check(1) == 0);
    g_addr_order = false;
    printf("  %-18s %8s %8s %8s   scans/malloc impl vs addr\n", "trace", "impl-1st", "LIFO-1st", "ADDR-1st");
    bool same = true, fewer = true, ok = true;
    for (int k = 0; k < NTRACE; k++) {
        replay_t ri, rl, ra;
        g_addr_order = false;
        replay(&g_tr[k], KIND_IMPLICIT, FIT_FIRST, 50, true, &ri);
        replay(&g_tr[k], KIND_EXPLICIT, FIT_FIRST, 50, true, &rl);
        g_addr_order = true;
        replay(&g_tr[k], KIND_EXPLICIT, FIT_FIRST, 50, true, &ra);
        g_addr_order = false;
        printf("  %-18s %8.3f %8.3f %8.3f   %.1f vs %.1f\n", g_tr[k].name, ri.util, rl.util, ra.util,
               ri.scans_per_malloc, ra.scans_per_malloc);
        same = same && ri.heap_hw == ra.heap_hw && ri.peak_live == ra.peak_live;
        fewer = fewer && ra.scans_per_malloc < ri.scans_per_malloc;
        ok = ok && replay_ok(&ri) && replay_ok(&rl) && replay_ok(&ra);
    }
    CHECK(ok);
    CHECK(same);                                         /* 같은 블록을 고른다 → 같은 high-water */
    CHECK(fewer);                                        /* 하지만 할당된 블록은 건너뛴다 */

    printf("== 연습 4: OSTEP malloc.py model (-S 100 -l ADDRSORT, no coalesce) ==\n");
    /* -A +10,+5,+30,+5,+15,+5,-0,-2,-4,+12,+8,+20,+25 를 malloc.py -c 로 돌린 실제 출력 (md 에 붙임) */
    static const long want_addr[3][4] = { { 1015, 1000, 1070, -1 },     /* FIRST */
                                          { 1050, 1000, 1015, 1070 },   /* BEST */
                                          { 1015, 1070, 1078, -1 } };   /* WORST */
    static const int want_searched[3][4] = { { 2, 1, 4, 4 }, { 4, 4, 4, 4 }, { 4, 4, 4, 4 } };
    static const char *const pname[3] = { "FIRST", "BEST", "WORST" };
    for (int pol = 0; pol < 3; pol++) {
        ost_t m;
        ost_init(&m, 1000, 100, pol);
        long ptrs[16];
        int np = 0, sr;
        const long sizes1[6] = { 10, 5, 30, 5, 15, 5 };
        for (int i = 0; i < 6; i++) ptrs[np++] = ost_malloc(&m, sizes1[i], &sr);
        ost_free(&m, ptrs[0]); ost_free(&m, ptrs[2]); ost_free(&m, ptrs[4]);
        const long sizes2[4] = { 12, 8, 20, 25 };
        bool match = true;
        printf("  %-5s:", pname[pol]);
        for (int i = 0; i < 4; i++) {
            long r = ost_malloc(&m, sizes2[i], &sr);
            printf(" Alloc(%ld)=%ld[%d]", sizes2[i], r, sr);
            match = match && r == want_addr[pol][i] && sr == want_searched[pol][i];
        }
        printf("\n");
        CHECK(match);
    }

    for (int k = 0; k < NTRACE; k++) free(g_tr[k].ops);
    mem_deinit();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
