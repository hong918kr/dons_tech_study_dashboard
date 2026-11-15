/* OS-M05 malloc 구현 2: segregated free lists와 단편화 측정
 *
 * 힙 = mmap 한 64 MiB 영역 하나, mem_sbrk 가 그 안에서 break 를 올린다 (CS:APP 3e §9.9 memlib 방식).
 * 블록 = [헤더 8 B | 페이로드 ... | 푸터 8 B], 헤더 = 푸터 = size | alloc(bit0). 페이로드 16 B 정렬, 최소 블록 32 B.
 * free 블록 페이로드 앞 16 B 에 next/prev 포인터 (명시적 이중 연결 free list, LIFO 삽입).
 *
 *   주어짐  OS-M04 할당기 축약판: 명시적 리스트 + 경계 태그(boundary tag) + 즉시 병합 4경우 + 분할 + realloc,
 *           정책 POL_FIRST / POL_BEST (리스트 1개), mm_check 기본 불변식, 트레이스 4개(T1~T4) 생성기와 재생기.
 *   Part A  POL_SEG: 크기 클래스 17개 (32..128 은 16 B 간격 정확 클래스 7개, 그 위는 2의 거듭제곱 구간 10개).
 *           A1 size_class, A2 seg_find (자기 클래스 first-fit → 위 클래스는 첫 블록), A3 mm_check 클래스 불변식.
 *   Part B  계측: B1 내부 단편화 Σ(블록 − 요청), B2 외부 단편화 E = 1 − 최대 free / 전체 free,
 *           B3 "전체 free 는 충분한데 맞는 블록이 없어서 힙을 늘린" 횟수(ext_fail).
 *   Part C  펌웨어: C1 클래스별 free 보고서(어느 클래스에 조각이 쌓였나), C2 요청 크기 반올림 정책
 *           (내부 단편화를 내주고 외부 단편화를 없앤다 — T4 에서 448 B / 512 B 프레임).
 *
 * 체크는 결정적인 것만 (활용률·탐색 노드 수·단편화는 트레이스가 고정이라 결정적). 시간 측정은 demo_bench.c.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so     (starter.c 도 같은 명령)
 * 데모·연습이 엔진만 쓸 때: #define MM_NO_MAIN 후 #include "solution.c"
 */
#define _DARWIN_C_SOURCE            /* macOS: MAP_ANON */
#define _DEFAULT_SOURCE             /* glibc: MAP_ANON */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define MM_GIVEN __attribute__((unused))

/* ======================= memlib (주어짐) ======================= */
enum { MEM_MAX = 64 << 20 };                  /* 64 MiB 를 한 번만 mmap, mm_init 마다 break 를 처음으로 */
static char *mem_start, *mem_brk;
static size_t mem_limit = MEM_MAX;            /* 테스트가 줄일 수 있는 상한 (MCU 힙 흉내) */

static int mem_init(void) {
    if (!mem_start) {
        void *p = mmap(NULL, MEM_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) return -1;
        mem_start = p;
    }
    mem_brk = mem_start;
    return 0;
}
static void *mem_sbrk(size_t incr) {          /* 실패하면 NULL (힙은 그대로) */
    if (incr > mem_limit - (size_t)(mem_brk - mem_start)) { errno = ENOMEM; return NULL; }
    char *old = mem_brk;
    mem_brk += incr;
    return old;
}
static size_t mem_heapsize(void) { return (size_t)(mem_brk - mem_start); }   /* break 는 안 내려감 = high-water */
MM_GIVEN static void mem_set_limit(size_t b) { mem_limit = b < (size_t)MEM_MAX ? b : (size_t)MEM_MAX; }

/* ======================= 블록 형식 (주어짐) ======================= */
enum { WSIZE = 8, DSIZE = 16, MINBLK = 32 };
static inline size_t rd(const void *p) { return *(const size_t *)p; }
static inline void wr(void *p, size_t v) { *(size_t *)p = v; }
static inline size_t pack(size_t sz, int a) { return sz | (size_t)a; }
static inline char *hdrp(void *bp) { return (char *)bp - WSIZE; }
static inline size_t bsize(void *bp) { return rd(hdrp(bp)) & ~(size_t)0xF; }
static inline int balloc(void *bp) { return (int)(rd(hdrp(bp)) & 1); }
static inline char *next_blk(void *bp) { return (char *)bp + bsize(bp); }
static inline char *prev_blk(void *bp) { return (char *)bp - (rd((char *)bp - DSIZE) & ~(size_t)0xF); }
static inline void set_blk(void *bp, size_t sz, int a) {          /* 헤더와 푸터를 같이 쓴다 */
    wr(hdrp(bp), pack(sz, a));
    wr((char *)bp + sz - DSIZE, pack(sz, a));
}
static inline void *nextf(void *bp) { return ((void **)bp)[0]; }  /* free 블록 페이로드 = 링크 */
static inline void *prevf(void *bp) { return ((void **)bp)[1]; }
static inline void set_nextf(void *bp, void *v) { ((void **)bp)[0] = v; }
static inline void set_prevf(void *bp, void *v) { ((void **)bp)[1] = v; }

/* ======================= 정책 · 클래스 · 통계 ======================= */
enum { POL_FIRST, POL_BEST, POL_SEG, NPOL };
MM_GIVEN static const char *const pol_name[NPOL] = { "first", "best", "seg" };
enum { NCLASS = 17 };                         /* 0..6: 32,48,..,128 정확  7..15: (128,256]..(32K,64K]  16: > 64K */
static int g_policy = POL_SEG;                /* mm_init 전에 mm_set_policy 로 고른다 */
static void *g_head[NCLASS];                  /* 클래스별 free list 머리. POL_FIRST/BEST 는 [0] 만 씀 */
static char *heap_listp;                      /* 프롤로그 블록 bp */

typedef struct {
    size_t free_bytes, nfree;                 /* free list 에 든 바이트·블록 수 (삽입/제거 때 갱신) */
    unsigned long nmalloc, visits, max_visits; /* find_fit 이 본 free 노드 수 (결정적 '속도' 지표) */
    unsigned long ext_fail;                   /* B3: free_bytes >= asize 인데 맞는 블록이 없어서 힙을 늘림 */
    unsigned long realloc_inplace, realloc_move, oom;
} mm_stats_t;
static mm_stats_t g_st;

MM_GIVEN static void mm_set_policy(int p) { g_policy = p; }
MM_GIVEN static size_t mm_blksize(void *p) { return bsize(p); }

/* A1: 블록 크기(asize, 16 의 배수, >= 32) → 클래스 번호. POL_SEG 가 아니면 늘 0 (리스트 하나). */
static int size_class(size_t asize) {
    if (g_policy != POL_SEG) return 0;
    if (asize <= 128) return (int)(asize / DSIZE) - 2;   /* 32→0, 48→1, … 128→6 : 크기 하나짜리 클래스 */
    int c = 7;
    size_t hi = 256;
    while (asize > hi && c < NCLASS - 1) { hi <<= 1; c++; }   /* (128,256]→7 … (32K,64K]→15, 그 위→16 */
    return c;
}
MM_GIVEN static size_t class_hi(int c) {      /* 보고용: 클래스 c 의 최대 블록 크기 (16 = 무한) */
    if (c < 7) return (size_t)(c + 2) * DSIZE;
    return c == NCLASS - 1 ? SIZE_MAX : (size_t)256 << (c - 7);
}

static void list_insert(void *bp) {           /* LIFO: 자기 클래스 리스트의 머리에 */
    int c = size_class(bsize(bp));
    set_nextf(bp, g_head[c]);
    set_prevf(bp, NULL);
    if (g_head[c]) set_prevf(g_head[c], bp);
    g_head[c] = bp;
    g_st.free_bytes += bsize(bp);
    g_st.nfree++;
}
static void list_remove(void *bp) {           /* 주의: 크기를 바꾸기 '전에' 불러야 클래스가 맞는다 */
    int c = size_class(bsize(bp));
    void *n = nextf(bp), *p = prevf(bp);
    if (p) set_nextf(p, n); else g_head[c] = n;
    if (n) set_prevf(n, p);
    g_st.free_bytes -= bsize(bp);
    g_st.nfree--;
}

/* A2: 자기 클래스에서 first-fit, 없으면 위 클래스로. 본 노드 수를 *visits 에 더한다. */
static void *seg_find(size_t asize, unsigned long *visits) {
    for (int c = size_class(asize); c < NCLASS; c++)
        for (void *bp = g_head[c]; bp; bp = nextf(bp)) {
            ++*visits;
            if (bsize(bp) >= asize) return bp;        /* 위 클래스는 모든 블록이 asize 보다 커서 첫 노드에서 끝 */
        }
    return NULL;
}

static void *find_fit(size_t asize) {
    unsigned long v = 0;
    void *hit = NULL;
    if (g_policy == POL_FIRST) {
        for (void *bp = g_head[0]; bp; bp = nextf(bp)) { v++; if (bsize(bp) >= asize) { hit = bp; break; } }
    } else if (g_policy == POL_BEST) {
        size_t best = SIZE_MAX;
        for (void *bp = g_head[0]; bp; bp = nextf(bp)) {
            v++;
            size_t s = bsize(bp);
            if (s >= asize && s < best) { best = s; hit = bp; if (s == asize) break; }
        }
    } else {
        hit = seg_find(asize, &v);
    }
    g_st.visits += v;
    if (v > g_st.max_visits) g_st.max_visits = v;
    return hit;
}

/* ======================= 할당기 본체 (주어짐: OS-M04 축약판) ======================= */
static void *coalesce(void *bp) {             /* bp: free 표시됨, 리스트에는 아직 없음. 4경우 */
    char *pv = prev_blk(bp), *nx = next_blk(bp);
    size_t s = bsize(bp);
    if (!balloc(nx)) { list_remove(nx); s += bsize(nx); }
    if (!balloc(pv)) { list_remove(pv); s += bsize(pv); bp = pv; }
    set_blk(bp, s, 0);
    list_insert(bp);
    return bp;
}
static void *extend_heap(size_t bytes) {      /* 옛 에필로그 자리가 새 블록의 헤더가 된다 */
    char *bp = mem_sbrk(bytes);
    if (!bp) return NULL;
    set_blk(bp, bytes, 0);
    wr(hdrp(next_blk(bp)), pack(0, 1));       /* 새 에필로그 */
    return coalesce(bp);
}
static void place(void *bp, size_t asize) {   /* 앞쪽을 쓰고 남는 게 32 B 이상이면 쪼갠다 */
    size_t s = bsize(bp);
    list_remove(bp);
    if (s - asize >= MINBLK) {
        set_blk(bp, asize, 1);
        void *r = next_blk(bp);
        set_blk(r, s - asize, 0);
        list_insert(r);
    } else {
        set_blk(bp, s, 1);
    }
}
static size_t adjust(size_t size) {           /* 요청 → 블록 크기: +헤더/푸터 16, 16 배수, 최소 32 */
    if (size > (size_t)MEM_MAX) return 0;
    size_t a = (size + DSIZE + 15) & ~(size_t)15;
    return a < MINBLK ? MINBLK : a;
}

int mm_init(void) {
    if (mem_init() < 0) return -1;
    memset(g_head, 0, sizeof g_head);
    memset(&g_st, 0, sizeof g_st);
    char *p = mem_sbrk(4 * WSIZE);            /* [패딩][프롤로그 헤더][프롤로그 푸터][에필로그 헤더] */
    if (!p) return -1;
    wr(p, 0);
    wr(p + WSIZE, pack(DSIZE, 1));
    wr(p + 2 * WSIZE, pack(DSIZE, 1));
    wr(p + 3 * WSIZE, pack(0, 1));
    heap_listp = p + 2 * WSIZE;
    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = adjust(size);
    if (!asize) return NULL;
    g_st.nmalloc++;
    char *bp = find_fit(asize);
    if (!bp) {
        /* B3: 전체 free 는 충분한데 한 덩어리가 없다 = 외부 단편화 때문에 힙을 늘린다 */
        if (g_st.free_bytes >= asize) g_st.ext_fail++;
        size_t need = asize;
        char *last = prev_blk(mem_brk);       /* 에필로그 바로 앞 블록. free 면 모자란 만큼만 늘린다 */
        if (!balloc(last)) need -= bsize(last);
        bp = extend_heap(need);
        if (!bp) { g_st.oom++; return NULL; }
    }
    place(bp, asize);
    return bp;
}

void mm_free(void *p) {
    if (!p) return;
    set_blk(p, bsize(p), 0);
    coalesce(p);
}

void *mm_realloc(void *p, size_t size) {      /* realloc(NULL,n)=malloc, realloc(p,0)=free+NULL (우리 선택) */
    if (!p) return mm_malloc(size);
    if (size == 0) { mm_free(p); return NULL; }
    size_t asize = adjust(size);
    if (!asize) return NULL;
    size_t cur = bsize(p);
    if (asize <= cur) {                       /* 줄이기: 남는 꼬리가 32 B 이상이면 떼어 free (뒤와 병합) */
        if (cur - asize >= MINBLK) {
            set_blk(p, asize, 1);
            char *r = next_blk(p);
            set_blk(r, cur - asize, 0);
            coalesce(r);
        }
        g_st.realloc_inplace++;
        return p;
    }
    char *nx = next_blk(p);
    int nfree = !balloc(nx);
    size_t avail = cur + (nfree ? bsize(nx) : 0);
    if (avail >= asize) {                     /* 뒤 free 블록을 먹고 자리에서 키운다 */
        if (nfree) list_remove(nx);
        if (avail - asize >= MINBLK) {
            set_blk(p, asize, 1);
            char *r = next_blk(p);
            set_blk(r, avail - asize, 0);
            list_insert(r);                   /* r 뒤는 할당 블록이나 에필로그 (nx 가 병합된 free 였으므로) */
        } else {
            set_blk(p, avail, 1);
        }
        g_st.realloc_inplace++;
        return p;
    }
    char *after = nfree ? next_blk(nx) : nx;
    if (bsize(after) == 0 && mem_sbrk(asize - avail)) {   /* 마지막 블록이면 힙 끝을 늘려 자리에서 */
        if (nfree) list_remove(nx);
        set_blk(p, asize, 1);
        wr(hdrp(next_blk(p)), pack(0, 1));
        g_st.realloc_inplace++;
        return p;
    }
    void *q = mm_malloc(size);                /* 옮기기 */
    if (!q) return NULL;                      /* 실패하면 p 는 그대로 유효 */
    memcpy(q, p, cur - DSIZE);
    mm_free(p);
    g_st.realloc_move++;
    return q;
}

/* ======================= 힙 검사기 ======================= */
static int g_check_verbose;
#define BAD(...) do { bad++; if (g_check_verbose) { printf("  mm_check: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int mm_check(int verbose) {
    int bad = 0;
    g_check_verbose = verbose;
    char *lo = mem_start, *hi = mem_brk;
    if (rd(heap_listp - WSIZE) != pack(DSIZE, 1) || rd(heap_listp) != pack(DSIZE, 1)) BAD("prologue broken");
    size_t nfree = 0, fbytes = 0, nblk = 0;
    int prev_free = 0;
    char *bp = next_blk(heap_listp);
    for (; bsize(bp) > 0; bp = next_blk(bp)) {          /* 힙을 주소순으로 */
        size_t s = bsize(bp);
        if (((uintptr_t)bp & 15) != 0) { BAD("block %p payload not 16-aligned", (void *)bp); break; }
        if (s < MINBLK || (s & 15) || bp + s > hi) { BAD("block %p bad size %zu", (void *)bp, s); break; }
        if (rd(hdrp(bp)) != rd(bp + s - DSIZE)) BAD("block %p header != footer", (void *)bp);
        nblk++;
        if (!balloc(bp)) {
            if (prev_free) BAD("block %p and previous block both free (missed coalesce)", (void *)bp);
            nfree++; fbytes += s;
        }
        prev_free = !balloc(bp);
    }
    if (bp != hi || rd(hi - WSIZE) != pack(0, 1)) BAD("epilogue not at break");
    size_t lfree = 0, lbytes = 0;
    for (int c = 0; c < NCLASS; c++) {
        size_t guard = 0;
        void *prev = NULL;
        for (void *f = g_head[c]; f; prev = f, f = nextf(f)) {
            if (++guard > nblk + 1) { BAD("class %d list has a cycle", c); break; }
            if ((char *)f < lo + 4 * WSIZE || (char *)f >= hi) { BAD("class %d node %p outside heap", c, f); break; }
            if (balloc(f)) BAD("class %d node %p is marked allocated", c, f);
            if (prevf(f) != prev) BAD("class %d node %p prev link broken", c, f);
            /* A3: 블록이 자기 크기의 클래스 리스트에 들어 있나 */
            if (size_class(bsize(f)) != c)
                BAD("class %d node %p size %zu belongs to class %d", c, f, bsize(f), size_class(bsize(f)));
            lfree++; lbytes += bsize(f);
        }
    }
    if (lfree != nfree) BAD("free blocks in heap %zu != in lists %zu", nfree, lfree);
    if (lbytes != fbytes || g_st.free_bytes != fbytes || g_st.nfree != nfree)
        BAD("free bytes: heap %zu lists %zu counter %zu", fbytes, lbytes, g_st.free_bytes);
    return bad;
}

/* ======================= Part B: 단편화 계측 ======================= */
/* B1: 내부 단편화 = 살아 있는 블록마다 (블록 크기 − 요청 크기) 의 합. 헤더/푸터·정렬 패딩·안 쪼갠 꼬리 포함. */
static size_t frag_internal(void *const *ptr, const uint32_t *req, int n) {
    size_t s = 0;
    for (int i = 0; i < n; i++)
        if (ptr[i]) s += bsize(ptr[i]) - req[i];
    return s;
}

typedef struct { size_t free_bytes, largest, nfree; double ext; } ext_frag_t;
/* B2: 힙을 주소순으로 걸으며 free 블록 합·최대·개수. E = 1 − largest/free (free 가 0 이면 0). */
static ext_frag_t frag_external(void) {
    ext_frag_t e = { 0, 0, 0, 0.0 };
    for (char *bp = next_blk(heap_listp); bsize(bp) > 0; bp = next_blk(bp))
        if (!balloc(bp)) {
            e.free_bytes += bsize(bp);
            e.nfree++;
            if (bsize(bp) > e.largest) e.largest = bsize(bp);
        }
    e.ext = e.free_bytes ? 1.0 - (double)e.largest / (double)e.free_bytes : 0.0;
    return e;
}

/* ======================= Part C: 펌웨어 도구 ======================= */
typedef struct { size_t nfree, bytes, minsz, maxsz; } class_rep_t;
/* C1: 클래스 리스트마다 블록 수 · 바이트 · 최소/최대 크기 (비어 있으면 전부 0). */
MM_GIVEN static void mm_class_report(class_rep_t rep[NCLASS]) {
    for (int c = 0; c < NCLASS; c++) {
        class_rep_t r = { 0, 0, 0, 0 };
        for (void *f = g_head[c]; f; f = nextf(f)) {
            size_t s = bsize(f);
            if (r.nfree == 0 || s < r.minsz) r.minsz = s;
            if (s > r.maxsz) r.maxsz = s;
            r.nfree++;
            r.bytes += s;
        }
        rep[c] = r;
    }
}
/* C2: 요청 반올림 — 128 B 이하는 그대로(정확 클래스), 그 위는 다음 2의 거듭제곱 페이로드로. */
static size_t fw_round(size_t n) {
    if (n <= 128 || n > (SIZE_MAX >> 1)) return n;
    size_t p = 256;
    while (p < n) p <<= 1;
    return p;
}

/* ======================= 트레이스 (주어짐: OS-M04 의 생성기를 그대로 — M04/M05/M06 이 같은 연산열) ======================= */
enum { MAX_OPS = 1 << 16, MAX_IDS = 1 << 14, NTRACE = 4 };
typedef struct { char op; int id; uint32_t size; } top_t;
typedef struct { const char *name; int n, nids; top_t op[MAX_OPS]; } trace_t;
static trace_t g_tr[NTRACE];
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void tpush(trace_t *t, char op, int id, uint32_t size) {   /* M04 의 tpush 를 정적 배열로 */
    t->op[t->n++] = (top_t){ op, id, size };
    if (id + 1 > t->nids) t->nids = id + 1;
}
/* T1/T2: live < 50 이면 무조건 alloc, 아니면 p=0.6 alloc / 0.4 임의 live free. 끝에 남은 것 전부 free (live 배열 뒤에서부터). */
static void gen_mix(trace_t *t, uint32_t seed, int nops, int mixed) {
    static int live[MAX_IDS];
    uint32_t s = seed;
    int nlive = 0, next_id = 0;
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
}
/* T3: 버퍼 200개 (id 0..199) 를 +1..+512 씩 realloc 으로 목표(1..32 KiB, 버퍼마다 임의)까지 키운다.
 *     realloc 한 번마다 25% 작은 alloc(1..64), 20% 작은 것 하나 free. 끝에 작은 것(뒤에서부터) → 버퍼 0..199 순으로 free. */
static void gen_realloc(trace_t *t, uint32_t seed) {
    enum { NB = 200 };
    static int small[MAX_IDS];
    uint32_t s = seed, cur[NB], target[NB];
    int grow[NB], ngrow = 0, next_id = NB, nsmall = 0;
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
        if (r < 25 && nsmall < MAX_IDS) {
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
}
/* T4: CS:APP binary-bal 스타일 — 64,448 을 500쌍 → 448 전부 free → 512 를 500개 (id 1000..1499) → 전부 free */
static void gen_binary(trace_t *t) {
    for (int k = 0; k < 500; k++) { tpush(t, 'a', 2 * k, 64); tpush(t, 'a', 2 * k + 1, 448); }
    for (int k = 0; k < 500; k++) tpush(t, 'f', 2 * k + 1, 0);
    for (int k = 0; k < 500; k++) tpush(t, 'a', 1000 + k, 512);
    for (int k = 0; k < 500; k++) tpush(t, 'f', 2 * k, 0);
    for (int k = 0; k < 500; k++) tpush(t, 'f', 1000 + k, 0);
}
MM_GIVEN static void gen_traces(void) {
    if (g_tr[0].n) return;
    g_tr[0].name = "T1 small-uniform"; gen_mix(&g_tr[0], 1, 2000, 0);
    g_tr[1].name = "T2 mixed";         gen_mix(&g_tr[1], 2, 2000, 1);
    g_tr[2].name = "T3 realloc-grow";  gen_realloc(&g_tr[2], 3);
    g_tr[3].name = "T4 binary-pattern"; gen_binary(&g_tr[3]);
}

/* ======================= 재생기 (주어짐) ======================= */
typedef struct {
    int check_every;      /* > 0 이면 K 연산마다 mm_check + E 표본 */
    bool fill;            /* 페이로드를 id 패턴으로 채우고 free 전에 확인 */
    bool round;           /* C2: 할당 요청을 fw_round 로 키운다 (요청 크기 기록은 원래 값) */
    int stop_at;          /* > 0 이면 그 연산 수까지만 재생 (힙은 그 상태로 남김) */
} ropt_t;
typedef struct {
    size_t peak_req, heap, blk_at_peak;       /* U = peak_req / heap */
    double util, ifrag;                       /* ifrag = (blk − req)/blk, 최고점에서 */
    double ext_mean, ext_max;                 /* 표본 E 의 평균·최대 */
    unsigned long fails, corrupt, check_bad, ifrag_mismatch, nsample;
    mm_stats_t st;
} rres_t;
static void *g_p[MAX_IDS];
static uint32_t g_req[MAX_IDS];
static unsigned char pat(int id, size_t i) { return (unsigned char)((unsigned)id * 131u + (unsigned)i * 7u + 1u); }
static void fill(unsigned char *p, int id, size_t from, size_t to) { for (size_t i = from; i < to; i++) p[i] = pat(id, i); }
static bool same(const unsigned char *p, int id, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) if (p[i] != pat(id, i)) return false;
    return true;
}

MM_GIVEN static rres_t replay(const trace_t *t, int policy, ropt_t o) {
    rres_t r;
    memset(&r, 0, sizeof r);
    mm_set_policy(policy);
    mm_init();
    memset(g_p, 0, sizeof(void *) * (size_t)t->nids);
    memset(g_req, 0, sizeof(uint32_t) * (size_t)t->nids);
    size_t live_req = 0, live_blk = 0;
    int n = o.stop_at > 0 && o.stop_at < t->n ? o.stop_at : t->n;
    for (int k = 0; k < n; k++) {
        const top_t *op = &t->op[k];
        int id = op->id;
        if (op->op == 'a') {
            unsigned char *p = mm_malloc(o.round ? fw_round(op->size) : op->size);
            if (!p) { r.fails++; continue; }
            g_p[id] = p; g_req[id] = op->size;
            live_req += op->size; live_blk += bsize(p);
            if (o.fill) fill(p, id, 0, op->size);
        } else if (op->op == 'f') {
            unsigned char *p = g_p[id];
            if (!p) continue;
            if (o.fill && !same(p, id, 0, g_req[id])) r.corrupt++;
            live_req -= g_req[id]; live_blk -= bsize(p);
            mm_free(p);
            g_p[id] = NULL;
        } else {
            unsigned char *p = g_p[id];
            if (!p) continue;
            size_t old = g_req[id], oblk = bsize(p);
            unsigned char *q = mm_realloc(p, o.round ? fw_round(op->size) : op->size);
            if (!q) { r.fails++; continue; }
            size_t keep = old < op->size ? old : op->size;
            if (o.fill && keep && (q[0] != pat(id, 0) || q[keep - 1] != pat(id, keep - 1))) r.corrupt++;
            if (o.fill) fill(q, id, keep, op->size);
            live_req = live_req - old + op->size;
            live_blk = live_blk - oblk + bsize(q);
            g_p[id] = q; g_req[id] = op->size;
        }
        if (live_req > r.peak_req) { r.peak_req = live_req; r.blk_at_peak = live_blk; }
        if (o.check_every > 0 && (k + 1) % o.check_every == 0) {
            r.check_bad += (unsigned long)mm_check(0);
            ext_frag_t e = frag_external();
            r.ext_mean += e.ext; r.nsample++;
            if (e.ext > r.ext_max) r.ext_max = e.ext;
            if (frag_internal(g_p, g_req, t->nids) != live_blk - live_req) r.ifrag_mismatch++;
        }
    }
    if (r.nsample) r.ext_mean /= (double)r.nsample;
    r.heap = mem_heapsize();
    r.util = r.heap ? (double)r.peak_req / (double)r.heap : 0.0;
    r.ifrag = r.blk_at_peak ? (double)(r.blk_at_peak - r.peak_req) / (double)r.blk_at_peak : 0.0;
    r.st = g_st;
    return r;
}

#ifndef MM_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void push_raw(void *bp, int c) {      /* 테스트용: 통계 안 건드리고 리스트 c 머리에 끼움 */
    set_nextf(bp, g_head[c]); set_prevf(bp, NULL);
    if (g_head[c]) set_prevf(g_head[c], bp);
    g_head[c] = bp;
}
static unsigned long visits_of(size_t n, void **out) {
    unsigned long v0 = g_st.visits;
    *out = mm_malloc(n);
    return g_st.visits - v0;
}
static void print_row(const char *tn, const char *pn, const rres_t *r) {
    printf("  %-18s %-9s U=%.3f heap=%8zu peak=%8zu  I=%.3f  Emean=%.3f Emax=%.3f ext_fail=%4lu  visits avg=%7.1f max=%5lu\n",
           tn, pn, r->util, r->heap, r->peak_req, r->ifrag, r->ext_mean, r->ext_max, r->st.ext_fail,
           r->st.nmalloc ? (double)r->st.visits / (double)r->st.nmalloc : 0.0, r->st.max_visits);
}

int main(void) {
    gen_traces();
    printf("-- given: OS-M04 baseline behaviour (passes already in the starter)\n");
    mm_set_policy(POL_FIRST); mm_init();
    CHECK(mm_malloc(0) == NULL && mm_check(0) == 0);
    void *z = mm_malloc(1);
    CHECK(z && ((uintptr_t)z & 15) == 0 && mm_blksize(z) == 32);
    CHECK(mm_realloc(z, 0) == NULL && g_st.nfree == 1 && mm_check(0) == 0);
    mem_set_limit(4096); mm_init();
    CHECK(mm_malloc(5000) == NULL && g_st.oom == 1 && mm_check(0) == 0 && mm_malloc(100) != NULL);
    mem_set_limit(MEM_MAX);

    printf("-- Part A: size classes, segregated fit, per-class heap checker\n");
    mm_set_policy(POL_SEG);
    CHECK(size_class(32) == 0 && size_class(48) == 1 && size_class(128) == 6);
    CHECK(size_class(144) == 7 && size_class(256) == 7 && size_class(272) == 8 && size_class(512) == 8);
    CHECK(size_class(528) == 9 && size_class(65536) == 15 && size_class(65552) == 16 && size_class(1u << 25) == 16);
    for (int pol = POL_FIRST; pol <= POL_SEG; pol += POL_SEG) {   /* first 와 seg 에 같은 손 시나리오 */
        mm_set_policy(pol); mm_init();
        char *a = mm_malloc(100), *g1 = mm_malloc(1), *b = mm_malloc(200), *g2 = mm_malloc(1);
        mm_free(a); mm_free(b);
        char *c = mm_malloc(40);              /* asize 64: first-fit 은 LIFO 머리 b(224), seg 는 a(128) */
        if (pol == POL_FIRST) CHECK(c == b && mm_check(1) == 0);
        else {
            CHECK(c == a && mm_blksize(c) == 64 && g_head[size_class(64)] == a + 64);
            CHECK(g_head[size_class(224)] == b && mm_check(1) == 0);
        }
        (void)g1; (void)g2;
    }
    for (int pol = POL_FIRST; pol <= POL_SEG; pol += POL_SEG) {   /* 정확 클래스는 한 노드 만에 */
        mm_set_policy(pol); mm_init();
        void *big = mm_malloc(20), *small[20], *guard[21], *got;
        guard[0] = mm_malloc(1);
        for (int i = 0; i < 20; i++) { small[i] = mm_malloc(1); guard[i + 1] = mm_malloc(1); }
        mm_free(big);
        for (int i = 0; i < 20; i++) mm_free(small[i]);
        unsigned long v = visits_of(20, &got);
        if (pol == POL_FIRST) CHECK(v == 21 && got == big);
        else CHECK(v == 1 && got == big && mm_check(1) == 0);
        (void)guard;
    }
    mm_set_policy(POL_SEG); mm_init();                /* 검사기: 잘못된 클래스 리스트에 든 블록 */
    {
        char *a = mm_malloc(100), *g = mm_malloc(1);
        mm_free(a);
        list_remove(a); push_raw(a, 3); g_st.free_bytes += 128; g_st.nfree++;
        CHECK(mm_check(0) == 1);
        (void)g;
    }
    mm_init();                                        /* 검사기 (주어진 부분): 병합 누락, 헤더≠푸터 */
    {
        char *a = mm_malloc(10), *b = mm_malloc(10), *g = mm_malloc(10);
        mm_free(a);
        set_blk(b, 32, 0); list_insert(b);
        CHECK(mm_check(0) >= 1);
        mm_init();
        a = mm_malloc(10); g = mm_malloc(10);
        wr(a + 32 - DSIZE, pack(48, 1));
        CHECK(mm_check(0) >= 1);
        (void)g;
    }
    ropt_t chk = { 64, true, false, 0 };
    rres_t R[NTRACE][NPOL];
    for (int t = 0; t < NTRACE; t++)
        for (int p = 0; p < NPOL; p++) R[t][p] = replay(&g_tr[t], p, chk);
    for (int t = 0; t < NTRACE; t++) {
        const rres_t *s = &R[t][POL_SEG];
        CHECK(s->fails == 0 && s->corrupt == 0 && s->check_bad == 0 && s->st.oom == 0);
    }
    {
        ext_frag_t e = frag_external();
        CHECK(e.nfree == 1 && g_st.nfree == 1 && e.free_bytes == mem_heapsize() - 32);   /* 전부 free → 한 덩어리 */
    }
    printf("  trace              policy    utilization / internal / external / search cost (deterministic)\n");
    for (int t = 0; t < NTRACE; t++)
        for (int p = 0; p < NPOL; p++) print_row(g_tr[t].name, pol_name[p], &R[t][p]);
    CHECK(R[1][POL_SEG].util > R[1][POL_FIRST].util + 0.1);                 /* T2: seg 가 first 보다 촘촘 */
    for (int t = 0; t < 3; t++)                                              /* best 만큼 촘촘, 탐색은 1/4 미만 */
        CHECK(R[t][POL_SEG].util > R[t][POL_BEST].util - 0.01 && R[t][POL_SEG].st.visits * 4 < R[t][POL_BEST].st.visits);
    CHECK(R[1][POL_SEG].st.max_visits * 5 < R[1][POL_FIRST].st.max_visits); /* T2: 최악 탐색 5배 이상 짧음 */
    CHECK(R[3][POL_SEG].st.visits * 100 < R[3][POL_FIRST].st.visits);      /* T4: 448 구멍 500개를 안 훑음 */

    printf("-- Part B: internal / external fragmentation on a hand-built heap\n");
    mm_set_policy(POL_SEG); mm_init();
    {
        void *ptr[6]; uint32_t req[6] = { 16, 48, 16, 112, 16, 1 };
        for (int i = 0; i < 6; i++) ptr[i] = mm_malloc(req[i]);   /* 블록 32,64,32,128,32,32 */
        char *hole = ptr[3];
        mm_free(ptr[1]); mm_free(ptr[3]); ptr[1] = ptr[3] = NULL;
        CHECK(frag_internal(ptr, req, 6) == 16 + 16 + 16 + 31);
        ext_frag_t e = frag_external();
        CHECK(e.free_bytes == 192 && e.largest == 128 && e.nfree == 2);
        CHECK(e.ext > 0.33333 && e.ext < 0.33334);                /* 1 − 128/192 = 1/3 */
        size_t h0 = mem_heapsize();
        void *x = mm_malloc(150);                                 /* asize 176 > 128 이지만 free 192 >= 176 */
        CHECK(x && g_st.ext_fail == 1 && mem_heapsize() == h0 + 176);
        void *y = mm_malloc(500);                                 /* asize 528 > free 192: 단편화 아님 */
        CHECK(y && g_st.ext_fail == 1);
        void *w = mm_malloc(100);                                 /* asize 128: 구멍에 딱 맞음 */
        e = frag_external();
        CHECK(w == hole && e.nfree == 1 && e.ext == 0.0);
        CHECK(mm_check(1) == 0);
    }
    for (int t = 0; t < NTRACE; t++) CHECK(R[t][POL_SEG].ifrag_mismatch == 0 && R[t][POL_SEG].nsample > 0);
    CHECK(R[3][POL_FIRST].st.ext_fail == 500 && R[3][POL_BEST].st.ext_fail == 500 && R[3][POL_SEG].st.ext_fail == 500);
    CHECK(R[3][POL_SEG].heap == 535568 && R[3][POL_SEG].peak_req == 288000);   /* 손으로 계산한 값 */

    printf("-- Part C: class report and request rounding (firmware)\n");
    {
        ropt_t mid = { 0, true, false, 1500 };                    /* T4: 448 을 다 푼 직후에서 멈춤 */
        replay(&g_tr[3], POL_SEG, mid);
        class_rep_t rep[NCLASS];
        mm_class_report(rep);
        size_t n = 0, b = 0;
        for (int c = 0; c < NCLASS; c++) {
            n += rep[c].nfree; b += rep[c].bytes;
            if (rep[c].nfree)
                printf("  class %2d (<= %5zu B): %4zu free blocks, %7zu B, sizes %zu..%zu\n",
                       c, class_hi(c), rep[c].nfree, rep[c].bytes, rep[c].minsz, rep[c].maxsz);
        }
        CHECK(n == g_st.nfree && b == g_st.free_bytes);
        CHECK(rep[8].nfree == 500 && rep[8].bytes == 232000 && rep[8].minsz == 464 && rep[8].maxsz == 464);
        CHECK(size_class(528) == 9 && rep[9].nfree == 0);         /* 512 요청(블록 528)은 클래스 9 에서 찾음 */
    }
    CHECK(fw_round(1) == 1 && fw_round(128) == 128 && fw_round(129) == 256);
    CHECK(fw_round(448) == 512 && fw_round(512) == 512 && fw_round(513) == 1024);
    {
        ropt_t rnd = { 64, true, true, 0 };
        rres_t r4 = replay(&g_tr[3], POL_SEG, rnd);
        rres_t r2 = replay(&g_tr[1], POL_SEG, rnd);
        print_row("T4 + fw_round", "seg", &r4);
        print_row("T2 + fw_round", "seg", &r2);
        CHECK(r4.st.ext_fail == 0 && r4.heap == 304032 && r4.corrupt == 0 && r4.check_bad == 0);
        CHECK(r4.util > 0.94 && R[3][POL_SEG].util < 0.54);
        CHECK(r2.ifrag > R[1][POL_SEG].ifrag && r2.corrupt == 0 && r2.check_bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
