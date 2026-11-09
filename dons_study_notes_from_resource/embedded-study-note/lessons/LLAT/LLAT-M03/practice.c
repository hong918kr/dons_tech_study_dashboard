/* LLAT-M03 추가 연습 — 한 번에 검증
 *
 * 앞부분(노드·할당기·HP 도메인·M-S 큐)은 solution.c 와 같다. 연습 답:
 *   연습 1  Treiber 스택 pop 에 HP → ABA 도 막힌다 (강제 스케줄; HP 없는 버전은 해제된 노드가 top 이 된다)
 *   연습 2  scan 을 정렬 + 이분 탐색으로 (O(R·H) → O((R+H) log H)), 선형 scan 과 결과 동일 (PRNG 1000 회)
 *   연습 3  동적 등록: HP 레코드 획득/반납, 나갈 때 남은 retire 노드는 고아(orphan) 목록 → 다음 scan 이 입양
 *   연습 4  R 선택: H 개가 모두 보호 중일 때 R = H+1 vs R = 2H 의 scan 횟수 (상각 비용)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "need lock-free pointer atomics");

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NODE_MALLOC 1          /* ASan: 진짜 malloc/free → 해제 후 읽기 = ASan 보고 */
#endif
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAN_SCALE 2            /* 새니타이저 빌드는 스트레스 메시지 수를 1/2 로 (20 초 안에 끝나게) */
#endif
#endif
#ifdef USE_MALLOC
#define NODE_MALLOC 1
#endif
#ifndef SAN_SCALE
#define SAN_SCALE 1
#endif

#define CL 128                 /* Apple M 계열 캐시 라인 (sysctl hw.cachelinesize = 128) */
#define MAX_THREADS 16
#define HP_K 2                 /* 스레드당 HP 개수: M-S 큐 dequeue 는 head 와 next 두 개가 필요 */
#define RMAX 128               /* retire 목록 배열 크기 (R 의 최댓값) */

/* ======================= 공통: 노드와 할당기 (하네스, TODO 아님) ======================= */
enum { NODE_LIVE = 1, NODE_FREED = 2 };
typedef struct node {
    _Atomic(struct node *) next;
    uint64_t val;
    atomic_int state;          /* 정적 풀 모드에서 "해제된 노드를 읽었나" 감지용 */
    int id;                    /* 할당 순번 (재사용돼도 매번 새 번호) */
} node_t;

#define POOL_N (1 << 18)
#define FLOG 4096
#define POISON 0xDEADDEADDEADDEADull
static atomic_long g_live;     /* 할당 - 해제 */
static atomic_long g_limit = POOL_N;   /* 동시에 살아 있을 수 있는 노드 수 (Part C 가 줄인다) */
static atomic_long g_alloc_fail, g_uaf;
static atomic_int g_seq;
static int g_freed_id[FLOG];
static atomic_int g_nfreed_log;
#ifndef NODE_MALLOC
static node_t g_pool[POOL_N];
static int g_freeq[POOL_N];    /* 해제된 풀 인덱스 FIFO: 가장 늦게 재사용 → 늦은 읽기를 오래 감지 */
static long g_fq_head, g_fq_tail, g_pool_next;
static pthread_mutex_t g_pool_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static node_t *node_alloc(uint64_t v) {
    if (atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed) >= atomic_load(&g_limit)) {
        atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_alloc_fail, 1, memory_order_relaxed);
        return NULL;
    }
#ifdef NODE_MALLOC
    node_t *n = malloc(sizeof *n);
    if (!n) { perror("malloc"); exit(2); }
#else
    pthread_mutex_lock(&g_pool_mu);
    node_t *n = (g_fq_head != g_fq_tail) ? &g_pool[g_freeq[g_fq_head++ % POOL_N]]
              : (g_pool_next < POOL_N ? &g_pool[g_pool_next++] : NULL);
    pthread_mutex_unlock(&g_pool_mu);
    if (!n) { fprintf(stderr, "pool exhausted\n"); exit(2); }
#endif
    atomic_init(&n->next, NULL);
    n->val = v;
    n->id = atomic_fetch_add_explicit(&g_seq, 1, memory_order_relaxed) + 1;
    atomic_store_explicit(&n->state, NODE_LIVE, memory_order_relaxed);
    return n;
}
static void node_free(node_t *n) {
    int i = atomic_fetch_add_explicit(&g_nfreed_log, 1, memory_order_relaxed);
    if (i < FLOG) g_freed_id[i] = n->id;
    atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
#ifdef NODE_MALLOC
    free(n);
#else
    atomic_store_explicit(&n->state, NODE_FREED, memory_order_relaxed);
    atomic_store_explicit(&n->next, NULL, memory_order_relaxed);   /* 독: 늦은 리더는 NULL/POISON 을 본다 */
    n->val = POISON;
    pthread_mutex_lock(&g_pool_mu);
    g_freeq[g_fq_tail++ % POOL_N] = (int)(n - g_pool);
    pthread_mutex_unlock(&g_pool_mu);
#endif
}
static bool was_freed(int id) {
    int n = atomic_load(&g_nfreed_log);
    for (int i = 0; i < n && i < FLOG; i++) if (g_freed_id[i] == id) return true;
    return false;
}
static void log_reset(void) { atomic_store(&g_nfreed_log, 0); atomic_store(&g_uaf, 0); }

/* 큐 코드는 노드를 반드시 이 접근자로 읽는다 ("ASan-lite": 해제된 노드를 읽으면 g_uaf++) */
static void nd_check(const node_t *n) {
    if (atomic_load_explicit(&((node_t *)n)->state, memory_order_relaxed) != NODE_LIVE)
        atomic_fetch_add_explicit(&g_uaf, 1, memory_order_relaxed);
}
static node_t *nd_next(node_t *n) { nd_check(n); return atomic_load_explicit(&n->next, memory_order_acquire); }
static uint64_t nd_val(node_t *n) { nd_check(n); return n->val; }

/* 강제 스케줄용 훅: 테스트가 "여기서 멈추고 다른 스레드를 먼저 돌려라" 를 끼워 넣는다 */
enum { HK_LOADED, HK_DEQ_HEAD, HK_DEQ_NEXT, HK_N };
static void (*g_hook)(int point, int tid);
#define HOOK(pt, tid) do { if (g_hook) g_hook((pt), (tid)); } while (0)

/* ======================= Part A: hazard pointer 도메인 ======================= */
typedef struct {
    _Alignas(CL) _Atomic(node_t *) hp[HP_K];   /* 남이 읽는 칸 → 스레드마다 캐시 라인 1 개 */
} hp_slot_t;
typedef struct {
    _Alignas(CL) node_t *list[RMAX];           /* retire 목록: 이 스레드만 만진다 */
    int n, nmax;                               /* 현재 길이, 관찰된 최대 길이 */
    long nretired, nfreed, nscan;
} hp_rec_t;
typedef struct {
    hp_slot_t slot[MAX_THREADS];
    hp_rec_t rec[MAX_THREADS];
    int nthreads;                              /* H = nthreads * HP_K */
    int R;                                     /* scan 문턱: 목록 길이 R 이 되면 scan.  R > H 여야 진행 보장 */
    void (*reclaim)(node_t *);
} hp_dom_t;

static bool hp_init(hp_dom_t *d, int nthreads, int R, void (*reclaim)(node_t *)) {
    if (nthreads < 1 || nthreads > MAX_THREADS || R <= nthreads * HP_K || R > RMAX) return false;
    memset(d, 0, sizeof *d);
    for (int t = 0; t < MAX_THREADS; t++)
        for (int k = 0; k < HP_K; k++) atomic_init(&d->slot[t].hp[k], NULL);
    d->nthreads = nthreads; d->R = R; d->reclaim = reclaim;
    return true;
}
static void hp_clear(hp_dom_t *d, int tid, int k) {
    atomic_store_explicit(&d->slot[tid].hp[k], NULL, memory_order_release);   /* 앞의 읽기 → 해제 순서 */
}
static void hp_clear_all(hp_dom_t *d, int tid) { for (int k = 0; k < HP_K; k++) hp_clear(d, tid, k); }
static int hp_scan(hp_dom_t *d, int tid);

/* TODO A1: p 를 내 HP 칸 k 에 게시한다. 다른 스레드의 scan(load) 이 이 store 를 "못 볼" 수 없어야 한다. */
static void hp_set(hp_dom_t *d, int tid, int k, node_t *p) {
    atomic_store_explicit(&d->slot[tid].hp[k], p, memory_order_seq_cst);
}

/* TODO A2: *src 가 가리키는 노드를 HP 칸 k 로 보호하고 돌려준다 (게시 → 재확인, 바뀌었으면 반복).
 *          load 와 게시 사이에 HOOK(HK_LOADED, tid) 를 부른다 (테스트가 여기에 다른 스레드를 끼워 넣는다). */
static node_t *hp_protect(hp_dom_t *d, int tid, int k, _Atomic(node_t *) *src) {
    node_t *p = atomic_load_explicit(src, memory_order_relaxed);
    for (;;) {
        HOOK(HK_LOADED, tid);
        hp_set(d, tid, k, p);                                            /* ① 게시 */
        node_t *q = atomic_load_explicit(src, memory_order_seq_cst);     /* ② 재확인 (게시보다 뒤에 보여야) */
        if (q == p) return p;           /* 게시 시점에 아직 *src 에 있었다 → 이제 아무도 해제 못 한다 */
        p = q;
    }
}

/* TODO A3: 모든 스레드의 HP 칸을 모아, 내 retire 목록에서 보호되지 않은 노드를 reclaim 하고
 *          보호된 것만 남긴다. 해제한 개수를 돌려준다. */
static int hp_scan(hp_dom_t *d, int tid) {
    node_t *plist[MAX_THREADS * HP_K];
    int np = 0;
    for (int t = 0; t < d->nthreads; t++)
        for (int k = 0; k < HP_K; k++) {
            node_t *h = atomic_load_explicit(&d->slot[t].hp[k], memory_order_seq_cst);
            if (h) plist[np++] = h;
        }
    hp_rec_t *r = &d->rec[tid];
    int keep = 0, freed = 0;
    for (int i = 0; i < r->n; i++) {
        node_t *p = r->list[i];
        bool hazard = false;
        for (int j = 0; j < np; j++) if (plist[j] == p) { hazard = true; break; }
        if (hazard) r->list[keep++] = p;
        else { d->reclaim(p); freed++; }
    }
    r->n = keep; r->nfreed += freed; r->nscan++;
    return freed;
}

/* TODO A4: 큐에서 떼어낸 노드 p 를 내 retire 목록에 넣고, 길이가 R 이 되면 scan 한다. */
static void hp_retire(hp_dom_t *d, int tid, node_t *p) {
    hp_rec_t *r = &d->rec[tid];
    r->list[r->n++] = p;
    r->nretired++;
    if (r->n > r->nmax) r->nmax = r->n;
    if (r->n >= d->R) hp_scan(d, tid);
}

/* ======================= Part A: Michael-Scott 큐 ======================= */
typedef struct {
    _Alignas(CL) _Atomic(node_t *) head;   /* 더미 노드를 가리킨다: 첫 값은 head->next 에 */
    _Alignas(CL) _Atomic(node_t *) tail;
    hp_dom_t *d;
} msq_t;

static void msq_init(msq_t *q, hp_dom_t *d) {
    node_t *dummy = node_alloc(0);
    if (!dummy) { fprintf(stderr, "no dummy\n"); exit(2); }
    atomic_init(&q->head, dummy); atomic_init(&q->tail, dummy); q->d = d;
}
/* (주어짐) enqueue: tail 노드를 hp0 으로 보호해야 t->next 를 안전하게 읽고 CAS 할 수 있다 */
static bool msq_enqueue(msq_t *q, int tid, uint64_t v) {
    node_t *n = node_alloc(v);
    if (!n) return false;
    for (;;) {
        node_t *t = hp_protect(q->d, tid, 0, &q->tail);
        node_t *next = nd_next(t);
        if (atomic_load_explicit(&q->tail, memory_order_acquire) != t) continue;
        if (next != NULL) {            /* tail 이 뒤처짐 → 도와서 한 칸 민다 */
            atomic_compare_exchange_strong(&q->tail, &t, next);
            continue;
        }
        node_t *expect = NULL;
        if (atomic_compare_exchange_strong(&t->next, &expect, n)) {     /* 연결 = 게시 (seq_cst ⊇ release) */
            atomic_compare_exchange_strong(&q->tail, &t, n);             /* 실패해도 됨: 누가 이미 밀었다 */
            break;
        }
    }
    hp_clear(q->d, tid, 0);
    return true;
}

/* TODO A5: dequeue 에 HP 두 개를 넣어라. hp0 = head(h), hp1 = h->next(next).
 *          next 를 게시한 뒤에는 "h 가 아직 head 인가" 로 재확인해야 한다 (왜 h->next 재확인으론 안 되나?).
 *          성공하면 HP 를 지우고 h 를 retire. (starter 의 본문은 HP 없는 순진한 버전) */
static bool msq_dequeue(msq_t *q, int tid, uint64_t *out) {
    hp_dom_t *d = q->d;
    for (;;) {
        node_t *h = hp_protect(d, tid, 0, &q->head);
        HOOK(HK_DEQ_HEAD, tid);
        node_t *t = atomic_load_explicit(&q->tail, memory_order_acquire);
        node_t *next = nd_next(h);
        HOOK(HK_DEQ_NEXT, tid);
        hp_set(d, tid, 1, next);
        if (atomic_load_explicit(&q->head, memory_order_seq_cst) != h) continue;
        if (next == NULL) { hp_clear_all(d, tid); return false; }   /* 비었음 */
        if (h == t) {                  /* tail 이 뒤처짐 → 돕고 다시 */
            atomic_compare_exchange_strong(&q->tail, &t, next);
            continue;
        }
        uint64_t v = nd_val(next);     /* CAS 전에 읽는다: CAS 뒤엔 next 가 남의 더미가 되어 곧 해제될 수 있다 */
        node_t *expect = h;
        if (atomic_compare_exchange_strong(&q->head, &expect, next)) {
            hp_clear_all(d, tid);      /* 내 HP 부터 지운다 — 안 그러면 내 scan 이 h 를 계속 붙잡는다 */
            hp_retire(d, tid, h);
            *out = v;
            return true;
        }
    }
}

/* ======================= 하네스 ======================= */
static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static long live(void) { return atomic_load(&g_live); }
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ======================= 연습 1: Treiber 스택 + HP (ABA) ======================= */
typedef struct { _Alignas(CL) _Atomic(node_t *) top; hp_dom_t *d; } tstack_t;
static void ts_push(tstack_t *s, uint64_t v) {
    node_t *n = node_alloc(v);
    node_t *t = atomic_load_explicit(&s->top, memory_order_relaxed);
    do atomic_store_explicit(&n->next, t, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&s->top, &t, n, memory_order_release, memory_order_relaxed));
}
static bool ts_pop_hp(tstack_t *s, int tid, uint64_t *out) {
    for (;;) {
        node_t *t = hp_protect(s->d, tid, 0, &s->top);       /* t 는 이제 해제도, 재사용도 안 된다 */
        if (!t) { hp_clear(s->d, tid, 0); return false; }
        node_t *next = nd_next(t);
        HOOK(HK_DEQ_HEAD, tid);
        node_t *e = t;
        if (atomic_compare_exchange_strong(&s->top, &e, next)) {
            *out = nd_val(t);
            hp_clear(s->d, tid, 0);
            hp_retire(s->d, tid, t);
            return true;
        }
    }
}
static bool ts_pop_naive(tstack_t *s, int tid, uint64_t *out) {   /* HP 없음 + 즉시 해제 */
    for (;;) {
        node_t *t = atomic_load_explicit(&s->top, memory_order_acquire);
        if (!t) return false;
        node_t *next = nd_next(t);
        HOOK(HK_DEQ_HEAD, tid);
        node_t *e = t;
        if (atomic_compare_exchange_strong(&s->top, &e, next)) { *out = nd_val(t); node_free(t); return true; }
    }
}
static tstack_t *g_ts;
static bool g_naive;
static int g_fire;
static uint64_t g_t1v[3];
static void aba_hook(int pt, int tid) {        /* 스레드 0 이 top=A, next=B 를 읽고 멈춘 사이: pop, pop, push 9 */
    if (tid != 0 || pt != HK_DEQ_HEAD || g_fire == 0) return;
    g_fire = 0;
    if (g_naive) { ts_pop_naive(g_ts, 1, &g_t1v[0]); ts_pop_naive(g_ts, 1, &g_t1v[1]); }
    else { ts_pop_hp(g_ts, 1, &g_t1v[0]); ts_pop_hp(g_ts, 1, &g_t1v[1]); }
    ts_push(g_ts, 9);
}
#ifndef NODE_MALLOC
static void pool_fifo_reset(void) { pthread_mutex_lock(&g_pool_mu); g_fq_head = g_fq_tail; pthread_mutex_unlock(&g_pool_mu); }
#endif

/* ======================= 연습 2: 정렬 scan ======================= */
static int cmp_ptr(const void *a, const void *b) {
    uintptr_t x = (uintptr_t)*(node_t *const *)a, y = (uintptr_t)*(node_t *const *)b;
    return (x > y) - (x < y);
}
static int hp_scan_sorted(hp_dom_t *d, int tid) {
    node_t *plist[MAX_THREADS * HP_K];
    int np = 0;
    for (int t = 0; t < d->nthreads; t++)
        for (int k = 0; k < HP_K; k++) {
            node_t *h = atomic_load_explicit(&d->slot[t].hp[k], memory_order_seq_cst);
            if (h) plist[np++] = h;
        }
    qsort(plist, (size_t)np, sizeof plist[0], cmp_ptr);
    hp_rec_t *r = &d->rec[tid];
    int keep = 0, freed = 0;
    for (int i = 0; i < r->n; i++) {
        node_t *p = r->list[i];
        if (np && bsearch(&p, plist, (size_t)np, sizeof plist[0], cmp_ptr)) r->list[keep++] = p;
        else { d->reclaim(p); freed++; }
    }
    r->n = keep; r->nfreed += freed; r->nscan++;
    return freed;
}
static node_t g_mock[64];
static int g_mock_freed[64], g_nmock;
static void mock_reclaim(node_t *n) { g_mock_freed[g_nmock++] = (int)(n - g_mock); }
/* 같은 seed 로 같은 상태를 두 번 만들어 선형/정렬 scan 결과(해제된 노드 집합)를 비교 */
static void build(hp_dom_t *d, uint32_t seed) {
    hp_init(d, 4, 40, mock_reclaim);
    g_rng = seed;
    for (int t = 0; t < 4; t++)
        for (int k = 0; k < HP_K; k++)
            if (xs32() % 3) atomic_store(&d->slot[t].hp[k], &g_mock[xs32() % 64]);
    int n = (int)(xs32() % 30);
    for (int i = 0; i < n; i++) d->rec[0].list[d->rec[0].n++] = &g_mock[(i * 7 + (int)(seed % 64)) % 64];   /* 서로 다른 노드 */
}
static uint64_t freed_mask(void) { uint64_t m = 0; for (int i = 0; i < g_nmock; i++) m |= 1ull << g_mock_freed[i]; return m; }

/* ======================= 연습 3: 동적 등록 + 고아 목록 ======================= */
static atomic_int g_active[MAX_THREADS];
static node_t *g_orphan[RMAX * MAX_THREADS];
static int g_norphan;
static pthread_mutex_t g_orphan_mu = PTHREAD_MUTEX_INITIALIZER;
static int hp_register(hp_dom_t *d) {               /* 빈 레코드를 CAS 로 차지 */
    for (int t = 0; t < d->nthreads; t++) {
        int z = 0;
        if (atomic_compare_exchange_strong(&g_active[t], &z, 1)) return t;
    }
    return -1;
}
static void hp_unregister(hp_dom_t *d, int tid) {
    hp_clear_all(d, tid);
    hp_scan(d, tid);
    hp_rec_t *r = &d->rec[tid];
    pthread_mutex_lock(&g_orphan_mu);              /* 아직 남이 보호 중인 노드는 고아로 넘긴다 (누수 방지) */
    for (int i = 0; i < r->n; i++) g_orphan[g_norphan++] = r->list[i];
    pthread_mutex_unlock(&g_orphan_mu);
    r->n = 0;
    atomic_store(&g_active[tid], 0);
}
static void hp_adopt(hp_dom_t *d, int tid) {        /* 고아를 내 목록으로 가져와 scan */
    hp_rec_t *r = &d->rec[tid];
    pthread_mutex_lock(&g_orphan_mu);
    while (g_norphan > 0 && r->n < RMAX) r->list[r->n++] = g_orphan[--g_norphan];
    pthread_mutex_unlock(&g_orphan_mu);
    hp_scan(d, tid);
}

static hp_dom_t g_d;
static msq_t g_q;

int main(void) {
    uint64_t v = 0;
    /* ---------- 연습 1 ---------- */
    CHECK(hp_init(&g_d, 2, 5, node_free));
    tstack_t s; atomic_init(&s.top, NULL); s.d = &g_d; g_ts = &s;
#ifndef NODE_MALLOC
    log_reset(); pool_fifo_reset();
    ts_push(&s, 3); ts_push(&s, 2); ts_push(&s, 1);          /* top: A(1) → B(2) → C(3) */
    node_t *A = atomic_load(&s.top), *B = atomic_load(&A->next);
    g_naive = true; g_fire = 1; g_hook = aba_hook;
    bool ok = ts_pop_naive(&s, 0, &v);
    g_hook = NULL;
    node_t *top = atomic_load(&s.top);
    CHECK(g_t1v[0] == 1 && g_t1v[1] == 2);
    CHECK(ok && v == 9);                                       /* 9 는 A 와 같은 주소에 들어왔다 (FIFO 재사용) */
    CHECK(top == B && atomic_load(&B->state) == NODE_FREED);   /* ABA: CAS 가 성공해 top = 해제된 B → 3 은 사라짐 */
    atomic_store(&s.top, NULL);                                /* C(3) 는 잃어버렸다 (누수 1 개) */
    pool_fifo_reset();
#endif
    long leaked = live();
    log_reset();
    ts_push(&s, 3); ts_push(&s, 2); ts_push(&s, 1);
    g_naive = false; g_fire = 1; g_hook = aba_hook;
    for (int i = 0; i < 4; i++) hp_retire(&g_d, 1, node_alloc(70));   /* 스레드 1 의 다음 retire 가 scan */
    bool ok2 = ts_pop_hp(&s, 0, &v);
    g_hook = NULL;
    CHECK(g_t1v[0] == 1 && g_t1v[1] == 2 && ok2 && v == 9);    /* A 가 재사용 안 됨 → CAS 실패 → 재시도로 9 */
    CHECK(ts_pop_hp(&s, 0, &v) && v == 3 && !ts_pop_hp(&s, 0, &v) && atomic_load(&g_uaf) == 0);
    for (int t = 0; t < 2; t++) hp_scan(&g_d, t);

    /* ---------- 연습 2 ---------- */
    int same = 0;
    for (uint32_t seed = 1; seed <= 1000; seed++) {
        hp_dom_t *d1 = malloc(sizeof *d1);
        if (!d1) return 2;
        build(d1, seed * 2654435761u | 1); g_nmock = 0; int f1 = hp_scan(d1, 0); uint64_t m1 = freed_mask(); int k1 = d1->rec[0].n;
        build(d1, seed * 2654435761u | 1); g_nmock = 0; int f2 = hp_scan_sorted(d1, 0); uint64_t m2 = freed_mask(); int k2 = d1->rec[0].n;
        if (f1 == f2 && m1 == m2 && k1 == k2) same++;
        free(d1);
    }
    CHECK(same == 1000);

    /* ---------- 연습 3 ---------- */
    CHECK(hp_init(&g_d, 3, 7, node_free));
    for (int t = 0; t < MAX_THREADS; t++) atomic_store(&g_active[t], 0);
    log_reset();
    int ta = hp_register(&g_d), tb = hp_register(&g_d), tc = hp_register(&g_d);
    CHECK(ta == 0 && tb == 1 && tc == 2 && hp_register(&g_d) == -1);   /* 3 개 다 참 */
    msq_init(&g_q, &g_d);
    for (uint64_t i = 1; i <= 3; i++) msq_enqueue(&g_q, ta, i);
    node_t *x = atomic_load(&g_q.head);                      /* 더미 */
    int idx = x->id, idy = atomic_load(&x->next)->id;        /* 값 1 노드 (꺼내면 더미가 되고, 다음 꺼냄에서 retire) */
    hp_protect(&g_d, tc, 0, &g_q.head);                      /* tc 가 더미 x 를 쥔 채 멈춤 */
    CHECK(msq_dequeue(&g_q, ta, &v) && v == 1 && msq_dequeue(&g_q, ta, &v) && v == 2);   /* x, y retire */
    hp_unregister(&g_d, ta);                                 /* y 는 해제, x 는 고아로 */
    CHECK(was_freed(idy));
    CHECK(g_norphan == 1 && !was_freed(idx) && atomic_load(&g_active[0]) == 0);
    int td = hp_register(&g_d);
    CHECK(td == 0 && g_d.rec[td].n == 0);                    /* 반납된 레코드 재사용 */
    hp_adopt(&g_d, td);
    CHECK(g_norphan == 0 && g_d.rec[td].n == 1 && !was_freed(idx));   /* 아직 보호 중이라 남음 */
    hp_clear(&g_d, tc, 0);
    hp_adopt(&g_d, td);
    CHECK(was_freed(idx) && g_d.rec[td].n == 0);
    CHECK(msq_dequeue(&g_q, td, &v) && v == 3 && !msq_dequeue(&g_q, td, &v));
    hp_unregister(&g_d, td); hp_unregister(&g_d, tb); hp_unregister(&g_d, tc);
    CHECK(g_norphan == 0);
    node_free(atomic_load(&g_q.head));                       /* 큐를 버린다: 마지막 더미 해제 */

    /* ---------- 연습 4 ---------- */
    long sc[2]; int Rs[2] = { 4 * HP_K + 1, 2 * 4 * HP_K };
    for (int c = 0; c < 2; c++) {
        CHECK(hp_init(&g_d, 4, Rs[c], node_free));
        node_t *P[8]; _Atomic(node_t *) ps;
        for (int i = 0; i < 8; i++) {
            P[i] = node_alloc(100 + (uint64_t)i); atomic_init(&ps, P[i]);
            hp_protect(&g_d, i / HP_K, i % HP_K, &ps);       /* H = 8 칸이 전부 서로 다른 노드를 보호 */
        }
        for (int i = 0; i < 8; i++) hp_retire(&g_d, 0, P[i]);
        long s0 = g_d.rec[0].nscan;
        for (int i = 0; i < 1000; i++) hp_retire(&g_d, 0, node_alloc(1));
        sc[c] = g_d.rec[0].nscan - s0;
        CHECK(g_d.rec[0].nfreed == 1000 - (g_d.rec[0].n - 8) && g_d.rec[0].n >= 8);
        for (int t = 0; t < 4; t++) hp_clear_all(&g_d, t);
        hp_scan(&g_d, 0);
    }
    CHECK(sc[0] == 1000 && sc[1] == 125);                    /* R=H+1: 매 retire 마다 scan, R=2H: H 개마다 */
    printf("ex4: scans R=%d -> %ld (slot reads %ld), R=%d -> %ld (slot reads %ld)\n",
           Rs[0], sc[0], sc[0] * 8, Rs[1], sc[1], sc[1] * 8);
    printf("live at end %ld (leaked by the naive ABA run: %ld)\n", live(), leaked);
    CHECK(atomic_load(&g_uaf) == 0 && live() == leaked);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
