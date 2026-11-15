/* BUILD: -pthread */
/* OS-M06 스레드별 캐시 할당기: arena + tcache
 *
 * 한 파일로 끝난다 (다른 레슨 #include 없음).
 *   백엔드 = "OS-M05 축약판": arena 마다 자기 mmap 영역(32 MiB) + mem_sbrk, boundary tag(헤더·푸터 8 B),
 *            segregated free list 17 클래스(블록 32..128 B 는 16 B 간격 정확 클래스, 그 위는 2의 거듭제곱 구간),
 *            클래스 안 first-fit → 위 클래스로, 즉시 병합(4 경우), 분할(최소 블록 32 B), realloc(축소·다음 블록 흡수·이동).
 *   헤더 워드(8 B): bit0 = alloc, bit1..3 = 0, bit4..47 = 블록 크기(16 의 배수, 헤더+푸터 포함),
 *                   bit48..55 = 주인(owner) arena id, bit56..63 = 0.   payload 는 16 B 정렬.
 *   Part A  모드 GLOBAL(arena 1개 = 전역 락 하나) / ARENAS(N개, 스레드 → arena 라운드로빈)
 *           TODO A1 my_arena (바인딩), A2 owner_arena (헤더 → 주인 arena + 검증)
 *   Part B  모드 TCACHE: _Thread_local bin 63개(블록 32..1024 B, 16 B 간격), bin 당 최대 7개,
 *           가득 차면 오래된 3개를 락 한 번에 arena 로 flush, 비면 락 한 번에 1+3개 refill.
 *           remote free: 주인이 아닌 스레드의 free → 주인 arena 의 lock-free 스택에 CAS push,
 *           주인 arena 를 쓰는 스레드가 다음 slow path(락 잡은 김에)에서 atomic_exchange 로 통째로 가져가 병합.
 *           TODO B1 tc_get/tc_flush/tc_put, B2 tc_refill, B3 remote_push/remote_drain
 *   Part C  패킷 파이프라인(RX 스레드가 할당 → 워커 3개가 해제) + 종료 경로
 *           TODO C1 mm_thread_exit (tcache 반납), C2 mm_drain_all (remote 스택 비우기)
 * 체크는 결정적인 것만 본다(락 획득 횟수·bin 개수·remote 카운터·패턴·소유 비트맵). 처리량은 demo_bench.c.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모가 엔진만 쓸 때: #define MM_NO_MAIN 후 #include "solution.c"
 */
#define _DARWIN_C_SOURCE            /* MAP_ANON (macOS) */
#define _DEFAULT_SOURCE             /* MAP_ANONYMOUS (glibc) */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MM_GIVEN __attribute__((unused))   /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */

/* ======================= 엔진 (주어짐): 상수 · 헤더 · arena · 백엔드 ======================= */
enum { MT_GLOBAL = 0, MT_ARENAS = 1, MT_TCACHE = 2 };          /* 모드 */
enum { RF_LOCK = 0, RF_STACK = 1 };                            /* remote free 정책 */
enum { MAX_ARENAS = 8, NCLASS = 17, TC_BINS = 63, TC_COUNT = 7, TC_FLUSH = 3, TC_REFILL = 3 };
#define ARENA_BYTES  ((size_t)32 << 20)      /* arena 하나의 최대 힙 (mmap 예약, 실제로는 건드린 만큼만) */
#define CHUNK        ((size_t)4096)          /* 힙을 늘릴 때 최소 단위 */
#define MINBLK       ((size_t)32)            /* 헤더 8 + pred 8 + succ 8 + 푸터 8 */
#define TC_MAX_BLOCK ((size_t)1024)          /* tcache 가 받는 최대 블록 크기 */
#define MM_MAX_REQ   ((size_t)8 << 20)

#define PACK(sz, al, own) ((uint64_t)(sz) | (uint64_t)(al) | ((uint64_t)(own) << 48))
#define GET(p)        (*(uint64_t *)(void *)(p))
#define PUT(p, v)     (*(uint64_t *)(void *)(p) = (v))
#define SIZE_OF(w)    ((size_t)((w) & 0x0000FFFFFFFFFFF0ull))
#define ALLOC_OF(w)   ((int)((w) & 1u))
#define OWNER_OF(w)   ((int)(((w) >> 48) & 0xFFu))
#define HDRP(bp)      ((char *)(bp) - 8)
#define FTRP(bp)      ((char *)(bp) + SIZE_OF(GET(HDRP(bp))) - 16)
#define NEXT_BLKP(bp) ((char *)(bp) + SIZE_OF(GET(HDRP(bp))))
#define PREV_BLKP(bp) ((char *)(bp) - SIZE_OF(GET((char *)(bp) - 16)))
#define PRED(bp)      (((void **)(void *)(bp))[0])     /* free 블록: payload 첫 두 워드가 리스트 링크 */
#define SUCC(bp)      (((void **)(void *)(bp))[1])
#define LINK(bp)      (((void **)(void *)(bp))[0])     /* tcache bin / remote 스택: payload 첫 워드 = next */
#define TC_IDX(sz)    ((int)((sz) / 16) - 2)           /* 블록 32 → 0, 48 → 1, …, 1024 → 62 */

typedef struct arena {
    _Alignas(128) pthread_mutex_t lock;  /* 128 B 정렬: 이 맥의 캐시 라인(hw.cachelinesize = 128) */
    int id;
    char *lo, *brk, *end;                /* [lo, end) 예약 영역, brk = 현재 끝 = 높이 기록(줄지 않음) */
    void *seg[NCLASS];                   /* 클래스별 free list 머리 */
    long n_lock, n_malloc, n_free, live_bytes, remote_drained, tc_flushed, tc_refilled;  /* 모두 lock 아래 */
    _Alignas(128) _Atomic(void *) remote;  /* 다른 스레드가 CAS 하는 줄은 따로 둔다 (false sharing 방지) */
    _Atomic long remote_pushed;
} arena_t;

static struct {
    int mode, narenas, policy;
    unsigned gen;                        /* mm_init 마다 +1 → 스레드의 낡은 tcache/바인딩 무효화 */
    atomic_uint rr;                      /* 라운드로빈 카운터 */
    atomic_long bad_free;                /* 거절한 free (잘못된 포인터, 이중 해제) */
    atomic_long st_hit, st_miss, st_put; /* 스레드가 끝날 때 모은 tcache 통계 */
    arena_t arena[MAX_ARENAS];
} G;

typedef struct { void *head; int count; } tc_bin_t;
static _Thread_local struct {
    unsigned gen;
    arena_t *arena;                      /* 이 스레드의 arena (NULL = 아직 안 정함) */
    tc_bin_t bin[TC_BINS];
    long hit, miss, put;
} T;

static void tls_sync(void) { if (T.gen != G.gen) { memset(&T, 0, sizeof T); T.gen = G.gen; } }
static void arena_lock(arena_t *a) { pthread_mutex_lock(&a->lock); a->n_lock++; }
static void arena_unlock(arena_t *a) { pthread_mutex_unlock(&a->lock); }
static size_t adjust(size_t n) { return n <= 16 ? MINBLK : (n + 16 + 15) & ~(size_t)15; }

static void *mem_sbrk(arena_t *a, size_t incr) {   /* CS:APP memlib: 영역 안에서 break 만 민다 */
    if (incr > (size_t)(a->end - a->brk)) return NULL;
    char *old = a->brk;
    a->brk += incr;
    return old;
}
static int seg_class(size_t sz) {
    if (sz <= 128) return (int)(sz / 16) - 2;             /* 32,48,…,128 → 0..6 */
    int c = 7; size_t lim = 256;                           /* (128,256] → 7, (256,512] → 8, …, > 64K → 16 */
    while (sz > lim && c < NCLASS - 1) { lim <<= 1; c++; }
    return c;
}
static void list_insert(arena_t *a, char *bp) {
    int c = seg_class(SIZE_OF(GET(HDRP(bp))));
    PRED(bp) = NULL; SUCC(bp) = a->seg[c];
    if (a->seg[c]) PRED(a->seg[c]) = bp;
    a->seg[c] = bp;
}
static void list_remove(arena_t *a, char *bp) {
    int c = seg_class(SIZE_OF(GET(HDRP(bp))));
    if (PRED(bp)) SUCC(PRED(bp)) = SUCC(bp); else a->seg[c] = SUCC(bp);
    if (SUCC(bp)) PRED(SUCC(bp)) = PRED(bp);
}
static char *coalesce(arena_t *a, char *bp) {          /* bp: 방금 free 표시, 리스트엔 아직 없음 */
    size_t size = SIZE_OF(GET(HDRP(bp)));
    int prev_alloc = ALLOC_OF(GET(bp - 16));           /* 앞 블록의 푸터 */
    char *nx = NEXT_BLKP(bp);
    if (!ALLOC_OF(GET(HDRP(nx)))) { list_remove(a, nx); size += SIZE_OF(GET(HDRP(nx))); }
    if (!prev_alloc) { char *pv = PREV_BLKP(bp); list_remove(a, pv); size += SIZE_OF(GET(HDRP(pv))); bp = pv; }
    PUT(HDRP(bp), PACK(size, 0, a->id)); PUT(FTRP(bp), PACK(size, 0, a->id));
    list_insert(a, bp);
    return bp;
}
static char *extend_heap(arena_t *a, size_t bytes) {
    char *bp = mem_sbrk(a, bytes);                     /* 옛 에필로그 자리가 새 블록의 헤더 */
    if (!bp) return NULL;
    PUT(HDRP(bp), PACK(bytes, 0, a->id)); PUT(FTRP(bp), PACK(bytes, 0, a->id));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1, a->id));        /* 새 에필로그 */
    return coalesce(a, bp);
}
static char *find_fit(arena_t *a, size_t asize) {      /* 자기 클래스부터 first-fit, 없으면 위 클래스 */
    for (int c = seg_class(asize); c < NCLASS; c++)
        for (char *bp = a->seg[c]; bp; bp = SUCC(bp))
            if (SIZE_OF(GET(HDRP(bp))) >= asize) return bp;
    return NULL;
}
static void place(arena_t *a, char *bp, size_t asize) {
    size_t cs = SIZE_OF(GET(HDRP(bp)));
    list_remove(a, bp);
    if (cs - asize >= MINBLK) {
        PUT(HDRP(bp), PACK(asize, 1, a->id)); PUT(FTRP(bp), PACK(asize, 1, a->id));
        char *rest = NEXT_BLKP(bp);
        PUT(HDRP(rest), PACK(cs - asize, 0, a->id)); PUT(FTRP(rest), PACK(cs - asize, 0, a->id));
        list_insert(a, rest);
    } else {
        PUT(HDRP(bp), PACK(cs, 1, a->id)); PUT(FTRP(bp), PACK(cs, 1, a->id));
    }
}
static void *arena_malloc(arena_t *a, size_t asize) {  /* 호출자가 a->lock 을 쥐고 있다 */
    char *bp = find_fit(a, asize);
    if (!bp && !(bp = extend_heap(a, asize > CHUNK ? asize : CHUNK))) return NULL;
    place(a, bp, asize);
    a->n_malloc++; a->live_bytes += (long)SIZE_OF(GET(HDRP(bp)));
    return bp;
}
static void arena_free(arena_t *a, void *p) {          /* 호출자가 a->lock 을 쥐고 있다 */
    char *bp = p;
    size_t sz = SIZE_OF(GET(HDRP(bp)));
    PUT(HDRP(bp), PACK(sz, 0, a->id)); PUT(FTRP(bp), PACK(sz, 0, a->id));
    a->n_free++; a->live_bytes -= (long)sz;
    coalesce(a, bp);
}
static void *arena_realloc(arena_t *a, char *bp, size_t asize) {   /* lock 아래 */
    size_t cs = SIZE_OF(GET(HDRP(bp)));
    if (asize <= cs) {                                 /* 축소: 남는 부분이 32 B 이상이면 잘라 free */
        if (cs - asize >= MINBLK) {
            PUT(HDRP(bp), PACK(asize, 1, a->id)); PUT(FTRP(bp), PACK(asize, 1, a->id));
            char *rest = NEXT_BLKP(bp);
            PUT(HDRP(rest), PACK(cs - asize, 0, a->id)); PUT(FTRP(rest), PACK(cs - asize, 0, a->id));
            a->live_bytes -= (long)(cs - asize);
            coalesce(a, rest);
        }
        return bp;
    }
    char *nx = NEXT_BLKP(bp);
    size_t ns = SIZE_OF(GET(HDRP(nx)));
    if (!ALLOC_OF(GET(HDRP(nx))) && cs + ns >= asize) { /* 다음 free 블록을 흡수해 제자리 확장 */
        list_remove(a, nx);
        size_t tot = cs + ns, keep = (tot - asize >= MINBLK) ? asize : tot;
        PUT(HDRP(bp), PACK(keep, 1, a->id)); PUT(FTRP(bp), PACK(keep, 1, a->id));
        if (keep < tot) {
            char *rest = NEXT_BLKP(bp);
            PUT(HDRP(rest), PACK(tot - keep, 0, a->id)); PUT(FTRP(rest), PACK(tot - keep, 0, a->id));
            list_insert(a, rest);
        }
        a->live_bytes += (long)(keep - cs);
        return bp;
    }
    char *nb = arena_malloc(a, asize);                 /* 이동 */
    if (!nb) return NULL;
    memcpy(nb, bp, cs - 16);
    arena_free(a, bp);
    return nb;
}
static int arena_init(arena_t *a, int id) {
    void *m = mmap(NULL, ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) return -1;
    memset(a, 0, sizeof *a);
    pthread_mutex_init(&a->lock, NULL);
    atomic_init(&a->remote, NULL);
    atomic_init(&a->remote_pushed, 0);
    a->id = id; a->lo = a->brk = m; a->end = a->lo + ARENA_BYTES;
    char *h = mem_sbrk(a, 32);                         /* 패딩 | 프롤로그 헤더 | 프롤로그 푸터 | 에필로그 */
    PUT(h, 0); PUT(h + 8, PACK(16, 1, id)); PUT(h + 16, PACK(16, 1, id)); PUT(h + 24, PACK(0, 1, id));
    return 0;
}
void mm_destroy(void) {
    for (int i = 0; i < G.narenas; i++) {
        munmap(G.arena[i].lo, ARENA_BYTES);
        pthread_mutex_destroy(&G.arena[i].lock);
    }
    G.narenas = 0;
    G.gen++;
}
int mm_init_cfg(int mode, int narenas, int policy) {   /* 다른 스레드가 없을 때만 부른다 */
    mm_destroy();
    if (mode == MT_GLOBAL) narenas = 1;
    if (narenas < 1 || narenas > MAX_ARENAS) return -1;
    G.mode = mode; G.policy = policy; G.gen++;
    atomic_store(&G.rr, 0); atomic_store(&G.bad_free, 0);
    atomic_store(&G.st_hit, 0); atomic_store(&G.st_miss, 0); atomic_store(&G.st_put, 0);
    for (int i = 0; i < narenas; i++) {
        if (arena_init(&G.arena[i], i)) { mm_destroy(); return -1; }
        G.narenas = i + 1;
    }
    return 0;
}
int mm_init(void) { return mm_init_cfg(MT_TCACHE, 4, RF_STACK); }

/* ======================= Part A — 스레드 → arena, 헤더 → 주인 arena ======================= */
static arena_t *my_arena(void) {
    /* A1: 처음 부를 때 라운드로빈(원자 카운터 % narenas)으로 정하고 _Thread_local 에 캐시 */
    if (!T.arena) {
        unsigned k = atomic_fetch_add_explicit(&G.rr, 1u, memory_order_relaxed);
        T.arena = &G.arena[k % (unsigned)G.narenas];
    }
    return T.arena;
}
static arena_t *owner_arena(void *p) {
    /* A2: 헤더의 owner 필드로 arena 를 찾고(O(1)), 그 arena 영역·alloc 비트·크기·푸터로 검증 */
    if ((uintptr_t)p & 15) return NULL;
    uint64_t h = GET(HDRP(p));
    int id = OWNER_OF(h);
    if (id >= G.narenas) return NULL;
    arena_t *a = &G.arena[id];
    if ((char *)p < a->lo + 32 || (char *)p >= a->end) return NULL;   /* lo/end 는 init 후 불변 → 락 불필요 */
    size_t sz = SIZE_OF(h);
    if (!ALLOC_OF(h) || sz < MINBLK || sz > (size_t)(a->end - (char *)p)) return NULL;
    if (GET(FTRP(p)) != h) return NULL;
    return a;
}

/* ======================= Part B — tcache, refill, remote free ======================= */
static void *tc_get(size_t asize) {
    /* B1: bin 머리에서 하나 꺼낸다 (LIFO = 방금 쓴 캐시가 뜨거운 블록) */
    tc_bin_t *b = &T.bin[TC_IDX(asize)];
    void *p = b->head;
    if (!p) return NULL;
    b->head = LINK(p);
    b->count--;
    return p;
}
MM_GIVEN static void tc_flush(arena_t *a, tc_bin_t *b, int k) {
    /* B1: 꼬리 쪽(오래된) k 개를 끊어서 락 한 번에 arena 로 돌려준다 */
    int keep = b->count - k;
    void *cut;
    if (keep <= 0) { cut = b->head; b->head = NULL; keep = 0; }
    else {
        void *q = b->head;
        for (int i = 1; i < keep; i++) q = LINK(q);
        cut = LINK(q);
        LINK(q) = NULL;
    }
    arena_lock(a);
    while (cut) { void *nx = LINK(cut); arena_free(a, cut); a->tc_flushed++; cut = nx; }
    arena_unlock(a);
    b->count = keep;
}
static bool tc_put(arena_t *me, void *p) {
    /* B1: 같은 bin 에 이미 있으면 이중 해제(거절), 가득 찼으면 오래된 TC_FLUSH 개 flush, 그다음 push */
    tc_bin_t *b = &T.bin[TC_IDX(SIZE_OF(GET(HDRP(p))))];
    for (void *q = b->head; q; q = LINK(q))
        if (q == p) { atomic_fetch_add(&G.bad_free, 1); return true; }
    if (b->count >= TC_COUNT) tc_flush(me, b, TC_FLUSH);
    LINK(p) = b->head;
    b->head = p;
    b->count++;
    T.put++;
    return true;
}
static void tc_refill(arena_t *a, size_t asize) {
    /* B2: (a->lock 아래) 같은 크기 블록을 TC_REFILL 개 더 잘라 bin 에 넣는다 → 다음 3번은 락 없이 */
    tc_bin_t *b = &T.bin[TC_IDX(asize)];
    int n = 0;
    while (n < TC_REFILL && b->count < TC_COUNT) {
        void *q = arena_malloc(a, asize);
        if (!q) break;
        if (SIZE_OF(GET(HDRP(q))) != asize) { arena_free(a, q); break; }   /* 꼬리 16 B 를 붙여 준 블록: bin 크기와 다름 */
        LINK(q) = b->head;
        b->head = q;
        b->count++;
        n++;
    }
    a->tc_refilled += n;
}
static void remote_push(arena_t *o, void *p) {
    /* B3: Treiber push. next 를 먼저 쓰고 release CAS 로 공개 → drain 쪽 acquire 가 next 를 본다 */
    void *old = atomic_load_explicit(&o->remote, memory_order_relaxed);
    do {
        LINK(p) = old;
    } while (!atomic_compare_exchange_weak_explicit(&o->remote, &old, p,
                                                    memory_order_release, memory_order_relaxed));
    atomic_fetch_add_explicit(&o->remote_pushed, 1, memory_order_relaxed);
}
static void remote_drain(arena_t *a) {
    /* B3: (a->lock 아래) 스택 전체를 exchange 로 떼어 와서 하나씩 arena_free. 한 개씩 pop 하지 않으니 ABA 없음 */
    if (!atomic_load_explicit(&a->remote, memory_order_relaxed)) return;
    void *p = atomic_exchange_explicit(&a->remote, NULL, memory_order_acquire);
    while (p) {
        void *nx = LINK(p);
        arena_free(a, p);
        a->remote_drained++;
        p = nx;
    }
}

/* ======================= 공개 API (주어짐: 위 TODO 들을 부르는 뼈대) ======================= */
void *mm_malloc(size_t n) {
    if (n == 0 || n > MM_MAX_REQ) return NULL;
    tls_sync();
    size_t asize = adjust(n);
    bool tc = (G.mode == MT_TCACHE && asize <= TC_MAX_BLOCK);
    if (tc) {
        void *p = tc_get(asize);
        if (p) { T.hit++; return p; }
        T.miss++;
    }
    arena_t *a = my_arena();
    arena_lock(a);
    remote_drain(a);                     /* 락을 잡은 김에 남들이 돌려준 블록부터 회수 */
    void *p = arena_malloc(a, asize);
    if (p && tc) tc_refill(a, asize);
    arena_unlock(a);
    return p;
}
void mm_free(void *p) {
    if (!p) return;
    tls_sync();
    arena_t *o = owner_arena(p);
    if (!o) { atomic_fetch_add(&G.bad_free, 1); return; }
    arena_t *me = my_arena();
    if (o == me) {
        if (G.mode == MT_TCACHE && SIZE_OF(GET(HDRP(p))) <= TC_MAX_BLOCK && tc_put(me, p)) return;
    } else if (G.policy == RF_STACK) {
        remote_push(o, p);               /* 주인 arena 의 락을 안 잡는다 */
        return;
    }
    arena_lock(o);                       /* 자기 arena, 또는 RF_LOCK 정책의 remote free (glibc 식) */
    arena_free(o, p);
    arena_unlock(o);
}
void *mm_realloc(void *p, size_t n) {    /* realloc(p, 0) = free + NULL (이 레슨의 선택) */
    if (!p) return mm_malloc(n);
    if (n == 0) { mm_free(p); return NULL; }
    if (n > MM_MAX_REQ) return NULL;
    tls_sync();
    arena_t *o = owner_arena(p);
    if (!o) { atomic_fetch_add(&G.bad_free, 1); return NULL; }
    if (o == my_arena()) {
        arena_lock(o);
        void *q = arena_realloc(o, p, adjust(n));
        arena_unlock(o);
        return q;
    }
    void *q = mm_malloc(n);              /* 남의 arena 블록: 내 arena 에 새로 받고 복사 후 remote free */
    if (!q) return NULL;
    size_t old = SIZE_OF(GET(HDRP(p))) - 16;
    memcpy(q, p, old < n ? old : n);
    mm_free(p);
    return q;
}

/* ======================= Part C — 종료 경로 ======================= */
void mm_thread_exit(void) {
    tls_sync();
    atomic_fetch_add(&G.st_hit, T.hit); atomic_fetch_add(&G.st_miss, T.miss); atomic_fetch_add(&G.st_put, T.put);
    T.hit = T.miss = T.put = 0;
    /* C1: 이 스레드 tcache 의 모든 블록을 자기 arena 로 돌려주고 바인딩을 푼다 */
    if (T.arena)
        for (int i = 0; i < TC_BINS; i++)
            if (T.bin[i].count) tc_flush(T.arena, &T.bin[i], T.bin[i].count);
    T.arena = NULL;
}
void mm_drain_all(void) {
    /* C2: 모든 arena 의 remote 스택을 (그 arena 락 아래에서) 비운다 */
    for (int i = 0; i < G.narenas; i++) {
        arena_t *a = &G.arena[i];
        arena_lock(a);
        remote_drain(a);
        arena_unlock(a);
    }
}

/* ======================= 검사기 · 통계 (주어짐) ======================= */
static int check_arena(arena_t *a, int verbose) {
    int bad = 0, id = a->id;
#define V(cond, ...) do { if (!(cond)) { bad++; if (verbose) { printf("  mm_check arena %d: ", id); printf(__VA_ARGS__); printf("\n"); } } } while (0)
    pthread_mutex_lock(&a->lock);                      /* arena_lock 이 아님: 검사는 n_lock 에 안 센다 */
    V(GET(a->lo + 8) == PACK(16, 1, id) && GET(a->lo + 16) == PACK(16, 1, id), "bad prologue");
    V(GET(a->brk - 8) == PACK(0, 1, id), "bad epilogue");
    long live = 0; int nfree_walk = 0, prev_free = 0;
    char *bp = a->lo + 32;
    while (bp < a->brk && SIZE_OF(GET(HDRP(bp))) > 0) {
        uint64_t h = GET(HDRP(bp));
        size_t sz = SIZE_OF(h);
        V(((uintptr_t)bp & 15) == 0, "payload %p not 16-aligned", (void *)bp);
        V(sz >= MINBLK && bp + sz - 8 <= a->brk, "block %p bad size %zu", (void *)bp, sz);
        if (!(sz >= MINBLK && bp + sz - 8 <= a->brk)) break;
        V(GET(FTRP(bp)) == h, "block %p header != footer", (void *)bp);
        V(OWNER_OF(h) == id, "block %p owner %d", (void *)bp, OWNER_OF(h));
        if (ALLOC_OF(h)) { live += (long)sz; prev_free = 0; }
        else { V(!prev_free, "two adjacent free blocks at %p", (void *)bp); nfree_walk++; prev_free = 1; }
        bp += sz;
    }
    V(live == a->live_bytes, "live bytes walk %ld != counter %ld", live, a->live_bytes);
    int nfree_list = 0;
    for (int c = 0; c < NCLASS; c++) {
        char *prev = NULL;
        for (char *q = a->seg[c]; q && nfree_list <= nfree_walk + 1; q = SUCC(q)) {
            nfree_list++;
            V(q >= a->lo + 32 && q < a->brk, "list %d node %p outside heap", c, (void *)q);
            if (!(q >= a->lo + 32 && q < a->brk)) break;
            V(!ALLOC_OF(GET(HDRP(q))), "list %d node %p marked allocated", c, (void *)q);
            V(seg_class(SIZE_OF(GET(HDRP(q)))) == c, "node %p size %zu in wrong class %d", (void *)q, SIZE_OF(GET(HDRP(q))), c);
            V(PRED(q) == prev, "node %p pred link broken", (void *)q);
            prev = q;
        }
    }
    V(nfree_list == nfree_walk, "free blocks: list %d != heap walk %d", nfree_list, nfree_walk);
    pthread_mutex_unlock(&a->lock);
#undef V
    return bad;
}
int mm_check(int verbose) {               /* 모든 arena + 부른 스레드 자신의 tcache */
    int bad = 0;
    for (int i = 0; i < G.narenas; i++) bad += check_arena(&G.arena[i], verbose);
    tls_sync();
    for (int i = 0; i < TC_BINS; i++) {
        int n = 0;
        for (void *q = T.bin[i].head; q && n <= TC_COUNT; q = LINK(q), n++) {
            uint64_t h = GET(HDRP(q));
            if (!ALLOC_OF(h) || TC_IDX(SIZE_OF(h)) != i || !T.arena || OWNER_OF(h) != T.arena->id) {
                bad++; if (verbose) printf("  mm_check tcache bin %d: bad block %p\n", i, q);
            }
        }
        if (n != T.bin[i].count || n > TC_COUNT) { bad++; if (verbose) printf("  mm_check tcache bin %d: count %d != %d\n", i, T.bin[i].count, n); }
    }
    return bad;
}
MM_GIVEN static void mm_bind(int id) { tls_sync(); T.arena = &G.arena[id % G.narenas]; }   /* 테스트용: 스레드 시작 때만 */
MM_GIVEN static int  mm_my_arena_id(void) { tls_sync(); return my_arena()->id; }
MM_GIVEN static int  tc_count(size_t req) { tls_sync(); return T.bin[TC_IDX(adjust(req))].count; }
MM_GIVEN static int  tc_total(void) { tls_sync(); int s = 0; for (int i = 0; i < TC_BINS; i++) s += T.bin[i].count; return s; }
MM_GIVEN static long sum_lock(void) { long s = 0; for (int i = 0; i < G.narenas; i++) s += G.arena[i].n_lock; return s; }
MM_GIVEN static long sum_live(void) { long s = 0; for (int i = 0; i < G.narenas; i++) s += G.arena[i].live_bytes; return s; }
MM_GIVEN static long sum_pushed(void) { long s = 0; for (int i = 0; i < G.narenas; i++) s += atomic_load(&G.arena[i].remote_pushed); return s; }
MM_GIVEN static long sum_drained(void) { long s = 0; for (int i = 0; i < G.narenas; i++) s += G.arena[i].remote_drained; return s; }
MM_GIVEN static long heap_hi(void) { long s = 0; for (int i = 0; i < G.narenas; i++) s += (long)(G.arena[i].brk - G.arena[i].lo); return s; }
MM_GIVEN static int  arena_free_blocks(arena_t *a) {   /* 힙을 걸으며 free 블록 수 (완전히 병합됐으면 0 또는 1) */
    int n = 0;
    pthread_mutex_lock(&a->lock);
    for (char *bp = a->lo + 32; SIZE_OF(GET(HDRP(bp))) > 0; bp = NEXT_BLKP(bp)) n += !ALLOC_OF(GET(HDRP(bp)));
    pthread_mutex_unlock(&a->lock);
    return n;
}

#ifndef MM_NO_MAIN
/* ======================= 테스트 하네스 (주어짐) ======================= */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint8_t pat(uint32_t id) { return (uint8_t)(id * 157u + 59u); }
static bool all_bytes(const void *v, size_t n, uint8_t b) {   /* n 바이트가 모두 b 인가 (memcmp 로 빠르게) */
    const uint8_t *p = v;
    return n == 0 || (p[0] == b && (n == 1 || memcmp(p, p + 1, n - 1) == 0));
}

/* ---- 공유 트레이스 T1..T4: OS-M04 solution.c 의 생성기를 그대로 복사 (op 열이 OS-M04/M05 와 같다) ---- */
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
static void free_traces(void) { for (int i = 0; i < NTRACE; i++) { free(g_tr[i].ops); g_tr[i] = (trace_t){ 0 }; } }
typedef struct { long errs, locks; double util; } replay_t;
static replay_t replay(const trace_t *t, int check_every) {
    int n = t->n;
    uint8_t **ptr = calloc((size_t)t->max_id + 1, sizeof *ptr);       /* 하네스: 진짜 malloc */
    uint32_t *sz = calloc((size_t)t->max_id + 1, sizeof *sz);
    if (!ptr || !sz) { fprintf(stderr, "out of memory\n"); exit(2); }
    replay_t r = { 0, 0, 0 };
    long live = 0, peak = 0, lock0 = sum_lock();
    for (int i = 0; i < n; i++) {
        op_t o = t->ops[i];
        if (o.op == 'a') {
            uint8_t *p = mm_malloc(o.size);
            if (!p || ((uintptr_t)p & 15)) { r.errs++; continue; }
            memset(p, pat((uint32_t)o.id), o.size);
            ptr[o.id] = p; sz[o.id] = o.size; live += o.size;
        } else if (o.op == 'f') {
            if (!ptr[o.id]) continue;
            if (!all_bytes(ptr[o.id], sz[o.id], pat((uint32_t)o.id))) r.errs++;
            mm_free(ptr[o.id]); live -= sz[o.id]; ptr[o.id] = NULL;
        } else {
            uint32_t old = sz[o.id];
            uint8_t *q = mm_realloc(ptr[o.id], o.size);
            if (!q) { r.errs++; continue; }
            if (!all_bytes(q, old < o.size ? old : o.size, pat((uint32_t)o.id))) r.errs++;
            if (o.size > old) memset(q + old, pat((uint32_t)o.id), o.size - old);
            ptr[o.id] = q; sz[o.id] = o.size; live += (long)o.size - (long)old;
        }
        if (live > peak) peak = live;
        if (check_every && (i + 1) % check_every == 0 && mm_check(1)) r.errs++;
    }
    r.locks = sum_lock() - lock0;
    r.util = (double)peak / (double)heap_hi();
    free(ptr); free(sz);
    return r;
}

/* ---- 스레드 도우미 · 소유 비트맵 · MPMC 큐 ---- */
static void run_thread(void *(*fn)(void *), void *arg) { pthread_t t; pthread_create(&t, NULL, fn, arg); pthread_join(t, NULL); }
static _Atomic uint64_t g_own[MAX_ARENAS][ARENA_BYTES / 16 / 64];   /* payload 16 B 단위 1 비트: 지금 누가 쥐고 있나 */
static atomic_long g_dup, g_unowned;
static void own_reset(void) {
    for (int i = 0; i < MAX_ARENAS; i++)
        for (size_t w = 0; w < ARENA_BYTES / 16 / 64; w++) atomic_store_explicit(&g_own[i][w], 0, memory_order_relaxed);
    atomic_store(&g_dup, 0); atomic_store(&g_unowned, 0);
}
static void own_mark(void *p, bool set) {     /* set: 받은 블록이 이미 누군가 쥔 것이면 g_dup (두 번 나감) */
    for (int i = 0; i < G.narenas; i++) {
        arena_t *a = &G.arena[i];
        if ((char *)p < a->lo || (char *)p >= a->end) continue;
        size_t bit = (size_t)((char *)p - a->lo) / 16;
        uint64_t m = 1ull << (bit % 64);
        if (set) { if (atomic_fetch_or(&g_own[i][bit / 64], m) & m) atomic_fetch_add(&g_dup, 1); }
        else if (!(atomic_fetch_and(&g_own[i][bit / 64], ~m) & m)) atomic_fetch_add(&g_unowned, 1);
        return;
    }
    atomic_fetch_add(&g_unowned, 1);
}
typedef struct { void *p; uint32_t n, id; } item_t;
enum { QCAP = 256 };
typedef struct { pthread_mutex_t m; pthread_cond_t ne, nf; item_t buf[QCAP]; int head, count; bool closed; } queue_t;
static void q_init(queue_t *q) {
    pthread_mutex_init(&q->m, NULL); pthread_cond_init(&q->ne, NULL); pthread_cond_init(&q->nf, NULL);
    q->head = q->count = 0; q->closed = false;
}
static void q_destroy(queue_t *q) { pthread_mutex_destroy(&q->m); pthread_cond_destroy(&q->ne); pthread_cond_destroy(&q->nf); }
static void q_push(queue_t *q, item_t it) {
    pthread_mutex_lock(&q->m);
    while (q->count == QCAP) pthread_cond_wait(&q->nf, &q->m);
    q->buf[(q->head + q->count++) % QCAP] = it;
    pthread_cond_signal(&q->ne);
    pthread_mutex_unlock(&q->m);
}
static bool q_pop(queue_t *q, item_t *it) {
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed) pthread_cond_wait(&q->ne, &q->m);
    bool ok = q->count > 0;
    if (ok) { *it = q->buf[q->head]; q->head = (q->head + 1) % QCAP; q->count--; pthread_cond_signal(&q->nf); }
    pthread_mutex_unlock(&q->m);
    return ok;
}
static void q_close(queue_t *q) { pthread_mutex_lock(&q->m); q->closed = true; pthread_cond_broadcast(&q->ne); pthread_mutex_unlock(&q->m); }

/* ---- 테스트 시나리오용 스레드 본문 ---- */
static int g_bind_seen[8];
static void *bind_probe(void *arg) {           /* A1: 처음 malloc 할 때 정해진 arena 를 기록 */
    int i = *(int *)arg;
    void *p = mm_malloc(32);
    g_bind_seen[i] = mm_my_arena_id();
    mm_free(p);
    mm_thread_exit();
    return NULL;
}
static void *g_xblk;
static void *free_from_arena1(void *arg) {     /* A2: arena 1 스레드가 arena 3 의 블록을 해제 (RF_LOCK) */
    (void)arg; mm_bind(1); mm_free(g_xblk); mm_thread_exit(); return NULL;
}
enum { RB_N = 100 };
static void *g_rblk[RB_N];
static int g_b_tc_after;
static void *remote_owner(void *arg) {         /* B3: arena 0 스레드가 100개 할당 */
    (void)arg; mm_bind(0);
    for (int i = 0; i < RB_N; i++) g_rblk[i] = mm_malloc(16 + (size_t)(i * 37) % 2000);
    mm_thread_exit();
    return NULL;
}
static void *remote_freer(void *arg) {         /* B3: arena 1 스레드가 그 100개를 해제 → 전부 remote */
    (void)arg; mm_bind(1);
    for (int i = 0; i < RB_N; i++) mm_free(g_rblk[i]);
    g_b_tc_after = tc_total();
    mm_thread_exit();
    return NULL;
}
typedef struct { queue_t *q; int idx, n; long remote, errs, got; } role_t;
static void *producer(void *arg) {             /* B 스트레스: 할당 → 패턴 → 소유 표시 → 큐 */
    role_t *r = arg;
    uint32_t s = 0x9E3779B9u ^ ((uint32_t)r->idx * 7919u);
    for (int k = 0; k < r->n; k++) {
        uint32_t c = xorshift32(&s) % 100;
        uint32_t n = c < 80 ? 1 + xorshift32(&s) % 256 : c < 95 ? 257 + xorshift32(&s) % 768 : 1025 + xorshift32(&s) % 7168;
        uint32_t id = (uint32_t)r->idx * 1000000u + (uint32_t)k;
        void *p = mm_malloc(n);
        if (!p) { r->errs++; continue; }
        own_mark(p, true);
        memset(p, pat(id), n);
        q_push(r->q, (item_t){ p, n, id });
    }
    mm_thread_exit();
    return NULL;
}
static void *consumer(void *arg) {             /* B 스트레스: 검증 → 소유 해제 → free(대개 remote) + 자기 할당 조금 */
    role_t *r = arg;
    item_t it;
    while (q_pop(r->q, &it)) {
        if (!all_bytes(it.p, it.n, pat(it.id))) r->errs++;
        own_mark(it.p, false);                 /* free 보다 먼저: free 뒤엔 다른 스레드가 같은 블록을 받을 수 있다 */
        if (OWNER_OF(GET(HDRP(it.p))) != mm_my_arena_id()) r->remote++;
        mm_free(it.p);
        size_t sn = 24 + it.n % 100;
        uint8_t *t = mm_malloc(sn);
        if (!t) { r->errs++; continue; }
        own_mark(t, true); memset(t, 0xA5, sn);
        if (!all_bytes(t, sn, 0xA5)) r->errs++;
        own_mark(t, false); mm_free(t);
        r->got++;
    }
    mm_thread_exit();
    return NULL;
}
typedef struct { long got, remote, errs, dup, unowned, pushed, drained, live, freeblk_bad, check; } stress_t;
static stress_t stress(int mode, int narenas, int policy, int np, int nc, int per) {
    stress_t st = { 0 };
    mm_init_cfg(mode, narenas, policy);
    own_reset();
    queue_t q; q_init(&q);
    pthread_t tp[8], tcn[8];
    role_t rp[8], rc[8];
    for (int i = 0; i < np; i++) { rp[i] = (role_t){ &q, i, per, 0, 0, 0 }; pthread_create(&tp[i], NULL, producer, &rp[i]); }
    for (int i = 0; i < nc; i++) { rc[i] = (role_t){ &q, 100 + i, 0, 0, 0, 0 }; pthread_create(&tcn[i], NULL, consumer, &rc[i]); }
    for (int i = 0; i < np; i++) { pthread_join(tp[i], NULL); st.errs += rp[i].errs; }
    q_close(&q);
    for (int i = 0; i < nc; i++) { pthread_join(tcn[i], NULL); st.got += rc[i].got; st.remote += rc[i].remote; st.errs += rc[i].errs; }
    q_destroy(&q);
    mm_drain_all();
    st.dup = atomic_load(&g_dup); st.unowned = atomic_load(&g_unowned);
    st.pushed = sum_pushed(); st.drained = sum_drained(); st.live = sum_live();
    for (int i = 0; i < G.narenas; i++) st.freeblk_bad += arena_free_blocks(&G.arena[i]) > 1;
    st.check = mm_check(1);
    return st;
}

/* ---- Part C: 패킷 파이프라인 (RX 1 → 워커 3) ---- */
enum { NPKT = 20000, NWORK = 3 };
typedef struct { uint32_t seq, len, csum, pad; } pkt_hdr_t;
static uint32_t pkt_csum(const pkt_hdr_t *h) {
    const uint8_t *b = (const uint8_t *)(h + 1);
    uint32_t s = h->seq * 2654435761u;
    for (uint32_t i = 0; i < h->len; i++) s = (s << 5) + s + b[i];
    return s;
}
static queue_t g_pq;
static long g_rx_stuck_before_exit;
static void *rx_thread(void *arg) {            /* NIC RX 콜백 흉내: 패킷 버퍼를 할당해 채우고 큐로 */
    (void)arg; mm_bind(0);
    uint32_t s = 0xC0FFEEu;
    static const uint32_t lens[3] = { 48, 240, 1500 };
    for (uint32_t seq = 0; seq < NPKT; seq++) {
        uint32_t c = xorshift32(&s) % 100, len = lens[c < 60 ? 0 : c < 85 ? 1 : 2];
        pkt_hdr_t *h = mm_malloc(sizeof *h + len);
        if (!h) continue;
        h->seq = seq; h->len = len; h->pad = 0;
        memset(h + 1, pat(seq), len);
        h->csum = pkt_csum(h);
        q_push(&g_pq, (item_t){ h, (uint32_t)(sizeof *h + len), seq });
    }
    mm_thread_exit();
    return NULL;
}
typedef struct { int w; long done, bad, remote; } wk_t;
static void *worker(void *arg) {               /* 워커: 검사 → 작업용 임시 버퍼(자기 arena) → 패킷 free(remote) */
    wk_t *k = arg;
    mm_bind(1 + k->w);
    void *keep[4] = { 0 }; int ki = 0;
    item_t it;
    while (q_pop(&g_pq, &it)) {
        pkt_hdr_t *h = it.p;
        if (h->seq != it.id || pkt_csum(h) != h->csum) k->bad++;
        void *ctx = mm_malloc(48);             /* 파싱 컨텍스트: 바로 free → tcache 왕복 */
        if (ctx) { memset(ctx, 0, 48); mm_free(ctx); }
        if (h->seq % 8 == 0) {                 /* 로그 레코드: 최근 4개만 보관 */
            mm_free(keep[ki]);
            keep[ki] = mm_malloc(100);
            ki = (ki + 1) % 4;
        }
        if (OWNER_OF(GET(HDRP(h))) != mm_my_arena_id()) k->remote++;
        mm_free(h);
        k->done++;
    }
    for (int i = 0; i < 4; i++) mm_free(keep[i]);
    mm_thread_exit();
    return NULL;
}

int main(void) {
    alarm(60);                                 /* 워치독: 무엇이 막혀도 60 초 뒤 SIGALRM 으로 끝난다 */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("-- Part A: header owner field, thread -> arena binding, global lock vs arenas\n");
    mm_init_cfg(MT_GLOBAL, 1, RF_LOCK);
    CHECK(mm_malloc(0) == NULL);
    mm_free(NULL);
    uint8_t *p1 = mm_malloc(1);
    CHECK(p1 && ((uintptr_t)p1 & 15) == 0 && SIZE_OF(GET(HDRP(p1))) == 32 && OWNER_OF(GET(HDRP(p1))) == 0);
    mm_free(p1);
    CHECK(G.arena[0].live_bytes == 0 && mm_check(1) == 0);

    mm_init_cfg(MT_ARENAS, 4, RF_LOCK);        /* A1: 6 스레드를 차례로 → 0,1,2,3,0,1 */
    int ids[6];
    for (int i = 0; i < 6; i++) { ids[i] = i; run_thread(bind_probe, &ids[i]); }
    printf("  bind order: %d %d %d %d %d %d\n", g_bind_seen[0], g_bind_seen[1], g_bind_seen[2], g_bind_seen[3], g_bind_seen[4], g_bind_seen[5]);
    CHECK(g_bind_seen[0] == 0 && g_bind_seen[1] == 1 && g_bind_seen[2] == 2 && g_bind_seen[3] == 3 && g_bind_seen[4] == 0 && g_bind_seen[5] == 1);

    mm_bind(2);                                /* A2: arena 2 의 블록 → 헤더 owner = 2, 자기 arena 로 반납 */
    uint8_t *p2 = mm_malloc(100);
    CHECK(p2 && OWNER_OF(GET(HDRP(p2))) == 2 && (char *)p2 > G.arena[2].lo && (char *)p2 < G.arena[2].brk);
    CHECK(G.arena[2].live_bytes == 128);
    mm_free(p2);
    CHECK(G.arena[2].live_bytes == 0 && atomic_load(&G.bad_free) == 0);
    uint64_t on_stack[4] = { 0, 0, 0, 0 };
    mm_free(&on_stack[2]);                     /* 힙이 아닌 포인터 → 거절 */
    mm_free(p2);                               /* 이중 해제 → 헤더 alloc 비트 0 → 거절 */
    CHECK(atomic_load(&G.bad_free) == 2 && mm_check(1) == 0);
    mm_bind(3);
    g_xblk = mm_malloc(200);                   /* arena 3 블록을 arena 1 스레드가 free (RF_LOCK: 주인 락을 잡고 반납) */
    long l3 = G.arena[3].live_bytes, nf1 = G.arena[1].n_free;
    run_thread(free_from_arena1, NULL);
    CHECK(l3 == 224 && G.arena[3].live_bytes == 0 && G.arena[1].n_free == nf1 && atomic_load(&G.bad_free) == 2);

    printf("  trace  mode     ops    locks   util\n");
    static const char *mname[3] = { "global", "arenas", "tcache" };
    long locks_t1[3] = { 0, 0, 0 };
    int ok_all = 1, n_t1 = 0;
    make_traces();
    for (int tr = 1; tr <= 4; tr++) {
        const trace_t *t = &g_tr[tr - 1];
        int n = t->n;
        if (tr == 1) n_t1 = n;
        for (int m = 0; m < 3; m++) {
            mm_init_cfg(m, 4, RF_STACK);
            replay_t r = replay(t, 500);
            mm_thread_exit();                  /* 메인의 tcache 반납 */
            int ok = r.errs == 0 && sum_live() == 0 && mm_check(1) == 0;
            ok_all &= ok;
            if (tr == 1) locks_t1[m] = r.locks;
            printf("  T%d     %-7s %6d %7ld  %5.3f%s\n", tr, mname[m], n, r.locks, r.util, ok ? "" : "  <-- errors");
        }
    }
    CHECK(ok_all);                             /* 4 트레이스 x 3 모드: 패턴·mm_check·live==0 */
    CHECK(locks_t1[0] == n_t1 && locks_t1[2] * 4 < locks_t1[0]);   /* T1: 전역 락은 op 마다 1번, tcache 는 1/4 미만 */
    free_traces();

    printf("-- Part B: tcache bound/flush/refill, remote free stack\n");
    mm_init_cfg(MT_TCACHE, 2, RF_STACK);
    mm_bind(0);
    void *b[10];
    long lk0 = sum_lock();
    for (int i = 0; i < 10; i++) b[i] = mm_malloc(40);          /* 블록 64 B: miss 는 1,5,9 번째뿐 */
    CHECK(sum_lock() - lk0 == 3 && tc_count(40) == 2 && G.arena[0].tc_refilled == 9);
    for (int i = 0; i < 10; i++) mm_free(b[i]);                 /* 2 → 7, 가득 → 오래된 3 flush → … */
    CHECK(tc_count(40) == 6 && G.arena[0].tc_flushed == 6 && sum_lock() - lk0 == 5);
    CHECK(G.arena[0].live_bytes == 6 * 64);                     /* tcache 안의 블록은 arena 입장에선 "할당됨" */
    CHECK(mm_malloc(40) == b[9] && tc_count(40) == 5);          /* LIFO: 방금 free 한 것이 먼저 */
    mm_free(b[9]);
    long lk1 = sum_lock();
    void *big = mm_malloc(2000);                                /* 블록 2016 B > 1024: tcache 안 씀 */
    mm_free(big);
    CHECK(sum_lock() - lk1 == 2 && tc_count(40) == 6);
    void *d = mm_malloc(24);
    mm_free(d); mm_free(d);                                     /* tcache 이중 해제 → bin 을 훑어 거절 */
    CHECK(atomic_load(&G.bad_free) == 1 && tc_count(24) == 4 && mm_check(1) == 0);   /* refill 3 + d 1 */
    mm_thread_exit();
    CHECK(G.arena[0].live_bytes == 0 && tc_total() == 0);

    mm_init_cfg(MT_TCACHE, 2, RF_STACK);       /* 순차 remote free: 주인 arena 0 / 해제 arena 1 */
    run_thread(remote_owner, NULL);
    long live0 = G.arena[0].live_bytes;
    run_thread(remote_freer, NULL);
    CHECK(atomic_load(&G.arena[0].remote_pushed) == RB_N && G.arena[0].remote_drained == 0);
    CHECK(G.arena[0].live_bytes == live0 && G.arena[1].n_free == 0 && g_b_tc_after == 0);  /* 남의 블록은 내 tcache 에 안 넣음 */
    mm_bind(0);
    void *trig = mm_malloc(5000);              /* 주인 쪽 slow path → 스택을 통째로 회수 */
    CHECK(G.arena[0].remote_drained == RB_N && G.arena[0].live_bytes == 5024);
    mm_free(trig);
    mm_thread_exit();
    CHECK(sum_live() == 0 && mm_check(1) == 0);

    stress_t s1 = stress(MT_TCACHE, 4, RF_STACK, 4, 4, 6000);
    printf("  stress tcache+stack: got=%ld remote=%ld pushed=%ld drained=%ld errs=%ld dup=%ld unowned=%ld live=%ld\n",
           s1.got, s1.remote, s1.pushed, s1.drained, s1.errs, s1.dup, s1.unowned, s1.live);
    CHECK(s1.got == 24000 && s1.errs == 0 && s1.dup == 0 && s1.unowned == 0);
    CHECK(s1.remote > 0 && s1.remote == s1.pushed && s1.pushed == s1.drained);
    CHECK(s1.live == 0 && s1.freeblk_bad == 0 && s1.check == 0);
    CHECK(atomic_load(&G.st_hit) > 0 && atomic_load(&G.st_put) > 0);
    stress_t s2 = stress(MT_ARENAS, 4, RF_LOCK, 4, 4, 3000);
    printf("  stress arenas+lock : got=%ld remote=%ld pushed=%ld errs=%ld dup=%ld live=%ld\n",
           s2.got, s2.remote, s2.pushed, s2.errs, s2.dup, s2.live);
    CHECK(s2.got == 12000 && s2.errs == 0 && s2.dup == 0 && s2.unowned == 0 && s2.pushed == 0);
    CHECK(s2.remote > 0 && s2.live == 0 && s2.freeblk_bad == 0 && s2.check == 0);

    printf("-- Part C: packet pipeline RX(arena 0) -> 3 workers(arenas 1..3), shutdown\n");
    mm_init_cfg(MT_TCACHE, 4, RF_STACK);
    q_init(&g_pq);
    pthread_t rx, wt[NWORK];
    wk_t wk[NWORK];
    pthread_create(&rx, NULL, rx_thread, NULL);
    for (int i = 0; i < NWORK; i++) { wk[i] = (wk_t){ i, 0, 0, 0 }; pthread_create(&wt[i], NULL, worker, &wk[i]); }
    pthread_join(rx, NULL);
    q_close(&g_pq);
    long done = 0, bad = 0, remote = 0;
    for (int i = 0; i < NWORK; i++) { pthread_join(wt[i], NULL); done += wk[i].done; bad += wk[i].bad; remote += wk[i].remote; }
    q_destroy(&g_pq);
    g_rx_stuck_before_exit = G.arena[1].live_bytes + G.arena[2].live_bytes + G.arena[3].live_bytes;
    long pushed = atomic_load(&G.arena[0].remote_pushed), drained_run = G.arena[0].remote_drained;
    printf("  packets=%ld bad=%ld remote=%ld pushed=%ld drained-during-run=%ld worker-arena live after exit=%ld\n",
           done, bad, remote, pushed, drained_run, g_rx_stuck_before_exit);
    CHECK(done == NPKT && bad == 0 && remote == NPKT && pushed == NPKT);
    CHECK(g_rx_stuck_before_exit == 0 && atomic_load(&G.st_put) > 0);   /* C1: 워커 tcache 가 비워졌다 */
    mm_drain_all();
    printf("  after drain_all: drained=%ld live=%ld bad_free=%ld\n", G.arena[0].remote_drained, sum_live(), (long)atomic_load(&G.bad_free));
    CHECK(G.arena[0].remote_drained == NPKT && sum_live() == 0 && atomic_load(&G.bad_free) == 0 && mm_check(1) == 0);
    mm_destroy();

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
