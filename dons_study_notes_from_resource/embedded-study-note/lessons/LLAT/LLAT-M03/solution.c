/* LLAT-M03 lock-free 메모리 회수: Hazard Pointer
 *
 * Michael-Scott 큐(연결 리스트 기반 MPMC 큐)에서 꺼낸 노드를 "언제 free 해도 되나" 를 hazard pointer(HP)로 푼다.
 *   Part A  HP 도메인: 스레드마다 HP 슬롯 2 개(128 B 패딩), 게시-재확인 protect, retire 목록,
 *           목록 길이 R 에서 scan(모든 슬롯을 모아 보호 안 된 노드만 해제). M-S 큐 dequeue 에 HP 2 개 적용.
 *   Part B  강제 스케줄(훅)로 use-after-free 창 3 개를 연다: (1) head 를 쥔 채 멈춤, (2) load 와 게시 사이,
 *           (3) next 를 읽은 뒤 게시 전. 멈춘 스레드가 있어도 미해제 노드 수 <= R 로 묶인다.
 *   Part C  펌웨어: malloc 없는 정적 노드 풀. HP 상한으로 풀 크기를 "컴파일 시점에" 정하고, 소비자가 수시로
 *           멈춰도 할당 실패 0 을 보인다 (EBR 은 이런 상한이 없다 — demo_bench.c).
 *
 * 해제 감지: 기본 빌드는 정적 풀(해제 = 상태 FREED + 독 칠하기, 메모리는 남음)이라 해제된 노드를 읽으면
 * 하네스 접근자(nd_next/nd_val)가 g_uaf 를 센다. ASan 빌드(-fsanitize=address)는 진짜 malloc/free 를 쓰므로
 * 같은 읽기가 ASan heap-use-after-free 로 잡힌다 → "ASan 깨끗" = UAF 없음의 증거.
 * 체크는 올바른 구현이면 매 실행 항상 성립하는 것만 본다. 각 Part 의 스트레스는 단위 테스트가 다 통과해야 돈다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
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

/* ======================= Part C: 정적 풀 + 상한 있는 큐 ======================= */
/* TODO C1: 노드 풀 크기 상한 = 동시에 살아 있을 수 있는 노드 수의 최댓값 (큐 안 / retire 목록 안 을 세어 보라).
 *          R <= H(= nthreads*k) 이면 scan 이 진행을 보장 못 하므로 0(잘못된 설정)을 돌려준다. */
static long hp_pool_bound(int nthreads, int k, int R, int qcap) {
    if (R <= nthreads * k) return 0;
    return 1 + (long)qcap + (long)nthreads * R;
}

typedef struct { msq_t q; _Alignas(CL) atomic_int inflight; int cap; } bq_t;   /* inflight = 크레딧 */
/* TODO C2: 크레딧(inflight < cap)을 먼저 얻고 넣는다. 크레딧이 없거나 할당 실패면 크레딧을 돌려주고 false. */
static bool bq_push(bq_t *b, int tid, uint64_t v) {
    if (atomic_fetch_add_explicit(&b->inflight, 1, memory_order_relaxed) >= b->cap) {
        atomic_fetch_sub_explicit(&b->inflight, 1, memory_order_relaxed);
        return false;
    }
    if (!msq_enqueue(&b->q, tid, v)) {
        atomic_fetch_sub_explicit(&b->inflight, 1, memory_order_relaxed);
        return false;
    }
    return true;
}
/* TODO C3: 꺼내는 데 성공했을 때만 크레딧을 돌려준다. */
static bool bq_pop(bq_t *b, int tid, uint64_t *out) {
    if (!msq_dequeue(&b->q, tid, out)) return false;
    atomic_fetch_sub_explicit(&b->inflight, 1, memory_order_relaxed);
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}
static long live(void) { return atomic_load(&g_live); }

/* --- 강제 스케줄: 스레드 0 이 훅 지점에 오면 "스레드 1" 의 동작을 그 자리에서 끝까지 돌린다 --- */
static msq_t *g_tq;
static int g_fire_pt = -1, g_fire_left, g_t1_deqs, g_hook_calls[HK_N];
static uint64_t g_t1_got[4];
static void test_hook(int pt, int tid) {
    if (tid != 0) return;
    g_hook_calls[pt]++;
    if (pt == g_fire_pt && g_fire_left > 0) {
        g_fire_left--;
        for (int i = 0; i < g_t1_deqs; i++) if (!msq_dequeue(g_tq, 1, &g_t1_got[i])) g_t1_got[i] = 999;
    }
}
static void arm(msq_t *q, int pt, int t1_deqs) {
    g_tq = q; g_fire_pt = pt; g_fire_left = 1; g_t1_deqs = t1_deqs;
    memset(g_hook_calls, 0, sizeof g_hook_calls);
    g_hook = test_hook;
}
static void drain(msq_t *q, int tid) { uint64_t v; while (msq_dequeue(q, tid, &v)) {} }

/* --- 스트레스: P 생산자 x C 소비자, 모든 메시지 정확히 한 번, 생산자별 순서 유지 --- */
#define SP 4
#define SC 4
#define SMSG (50000 / SAN_SCALE)
static atomic_uchar g_seen[SP * SMSG];
static atomic_int g_prod_done, g_order_bad, g_consumed;
typedef struct { msq_t *q; bq_t *b; int tid, idx, nprod, nmsg; } targ_t;

static void *producer(void *a) {
    targ_t *t = a;
    for (int s = 0; s < t->nmsg; s++) {
        uint64_t v = ((uint64_t)t->idx << 32) | (uint32_t)s;
        while (!(t->b ? bq_push(t->b, t->tid, v) : msq_enqueue(t->q, t->tid, v))) sched_yield();
    }
    atomic_fetch_add(&g_prod_done, 1);
    return NULL;
}
static void *consumer(void *a) {
    targ_t *t = a;
    long last[SP];
    for (int i = 0; i < SP; i++) last[i] = -1;
    for (;;) {
        uint64_t v;
        bool got = t->b ? bq_pop(t->b, t->tid, &v) : msq_dequeue(t->q, t->tid, &v);
        if (got) {
            int p = (int)(v >> 32); long s = (long)(uint32_t)v;
            if (p < 0 || p >= t->nprod || s >= t->nmsg) { atomic_fetch_add(&g_order_bad, 1); continue; }
            if (s <= last[p]) atomic_fetch_add(&g_order_bad, 1);   /* FIFO: 한 생산자의 값은 증가해야 */
            last[p] = s;
            atomic_fetch_add_explicit(&g_seen[p * t->nmsg + s], 1, memory_order_relaxed);
            atomic_fetch_add(&g_consumed, 1);
        } else if (atomic_load(&g_prod_done) == t->nprod) {
            if (!(t->b ? bq_pop(t->b, t->tid, &v) : msq_dequeue(t->q, t->tid, &v))) break;
            /* 마지막 확인에서 하나 더 나왔으면 정상 처리 (드묾) */
            int p = (int)(v >> 32); long s = (long)(uint32_t)v;
            if (p >= 0 && p < t->nprod && s < t->nmsg) {
                if (s <= last[p]) atomic_fetch_add(&g_order_bad, 1);
                last[p] = s;
                atomic_fetch_add_explicit(&g_seen[p * t->nmsg + s], 1, memory_order_relaxed);
                atomic_fetch_add(&g_consumed, 1);
            }
        } else {
            sched_yield();
        }
    }
    return NULL;
}
/* Part C: 소비자 tid 4 는 64 번에 한 번 dequeue 도중(hp0 = head, next 읽은 직후) 50 us 멈춘다 */
static atomic_int g_stall_ops;
static void stall_hook(int pt, int tid) {
    if (tid != 4 || pt != HK_DEQ_NEXT) return;
    if (atomic_fetch_add_explicit(&g_stall_ops, 1, memory_order_relaxed) % 64 == 0) {
        struct timespec ts = {0, 50000};
        nanosleep(&ts, NULL);
    }
}
static int run_stress(msq_t *q, bq_t *b, hp_dom_t *d, int nprod, int ncons, int nmsg) {
    pthread_t th[MAX_THREADS];
    targ_t ta[MAX_THREADS];
    for (int i = 0; i < nprod * nmsg; i++) atomic_store(&g_seen[i], 0);
    atomic_store(&g_prod_done, 0); atomic_store(&g_order_bad, 0); atomic_store(&g_consumed, 0);
    for (int i = 0; i < nprod + ncons; i++) {
        ta[i] = (targ_t){ q, b, i, i, nprod, nmsg };
        pthread_create(&th[i], NULL, i < nprod ? producer : consumer, &ta[i]);
    }
    for (int i = 0; i < nprod + ncons; i++) pthread_join(th[i], NULL);
    int bad_count = 0;
    for (int i = 0; i < nprod * nmsg; i++) if (atomic_load(&g_seen[i]) != 1) bad_count++;
    for (int t = 0; t < d->nthreads; t++) { hp_clear_all(d, t); hp_scan(d, t); }   /* 끝: 남은 retire 정리 */
    return bad_count;
}
static int max_nmax(hp_dom_t *d) { int m = 0; for (int t = 0; t < d->nthreads; t++) if (d->rec[t].nmax > m) m = d->rec[t].nmax; return m; }

static hp_dom_t g_d, g_d2;
static msq_t g_q, g_q2;
static bq_t g_b;

int main(void) {
    /* ---------- Part A 단위: protect / retire / scan ---------- */
    CHECK(!hp_init(&g_d, 2, 4, node_free));                  /* R=4 <= H=4: 진행 보장 없음 → 거절 */
    CHECK(hp_init(&g_d, 2, 5, node_free));
    log_reset();
    node_t *a = node_alloc(10), *b = node_alloc(11), *c = node_alloc(12), *x = node_alloc(13), *y = node_alloc(14);
    int ida = a->id, idx = x->id;
    _Atomic(node_t *) src; atomic_init(&src, a);
    CHECK(hp_protect(&g_d, 1, 0, &src) == a && atomic_load(&g_d.slot[1].hp[0]) == a);   /* 게시됨 */
    hp_retire(&g_d, 0, a); hp_retire(&g_d, 0, b); hp_retire(&g_d, 0, c); hp_retire(&g_d, 0, x);
    CHECK(g_d.rec[0].n == 4 && atomic_load(&g_nfreed_log) == 0);        /* R=5 전에는 아무것도 해제 안 함 */
    hp_retire(&g_d, 0, y);                                               /* 길이 5 = R → scan */
    CHECK(g_d.rec[0].nscan == 1 && g_d.rec[0].n == 1 && g_d.rec[0].list[0] == a);   /* a 만 남음 */
    CHECK(!was_freed(ida) && was_freed(idx) && atomic_load(&g_nfreed_log) == 4);
    hp_clear(&g_d, 1, 0);
    CHECK(atomic_load(&g_d.slot[1].hp[0]) == NULL && hp_scan(&g_d, 0) == 1 && was_freed(ida) && g_d.rec[0].n == 0);
    CHECK(live() == 0);

    /* ---------- Part A 단위: 큐 FIFO ---------- */
    CHECK(hp_init(&g_d, 2, 5, node_free));
    msq_init(&g_q, &g_d);
    uint64_t v = 0;
    CHECK(!msq_dequeue(&g_q, 0, &v));
    for (uint64_t i = 1; i <= 6; i++) msq_enqueue(&g_q, 0, i);
    bool fifo = true;
    for (uint64_t i = 1; i <= 6; i++) fifo = fifo && msq_dequeue(&g_q, 0, &v) && v == i;
    CHECK(fifo && !msq_dequeue(&g_q, 0, &v));
    CHECK(g_d.rec[0].nretired == 6 && g_d.rec[0].n == 1 && live() == 2);   /* 더미 1 + 목록 1 (5 개는 scan 으로) */
    CHECK(atomic_load(&g_d.slot[0].hp[0]) == NULL && atomic_load(&g_d.slot[0].hp[1]) == NULL);
    hp_scan(&g_d, 0);
    CHECK(live() == 1 && atomic_load(&g_uaf) == 0);
    int unitA = FAILS();

    /* ---------- Part A 스트레스: 4 생산자 x 4 소비자 ---------- */
    if (unitA == 0) {
        g_hook = NULL;
        CHECK(hp_init(&g_d2, SP + SC, 2 * (SP + SC) * HP_K, node_free));    /* R = 2H = 32 */
        long live0 = live();
        msq_init(&g_q2, &g_d2);
        int bad = run_stress(&g_q2, NULL, &g_d2, SP, SC, SMSG);
        CHECK(bad == 0 && atomic_load(&g_order_bad) == 0 && atomic_load(&g_consumed) == SP * SMSG);
        CHECK(atomic_load(&g_uaf) == 0);                                  /* 해제된 노드를 읽은 적 0 */
        CHECK(max_nmax(&g_d2) <= g_d2.R);                                 /* 목록 길이 상한 */
        CHECK(live() == live0 + 1);                                       /* 남은 것 = 더미 1 개 */
        printf("stress A: %d msgs, retired %ld, scans %ld (thread 4)\n", SP * SMSG,
               g_d2.rec[SP].nretired, g_d2.rec[SP].nscan);
    } else skip(5, "Part A");

    /* ---------- Part B: 강제 스케줄 3 개 (단일 OS 스레드, "스레드 1" 은 훅 안에서 끝까지 돈다) ---------- */
    /* B1: 스레드 0 이 head(더미 A) 를 hp0 에 쥔 채 멈춤 → 스레드 1 이 A 를 꺼내 retire → scan */
    CHECK(hp_init(&g_d, 2, 5, node_free));
    log_reset();
    msq_init(&g_q, &g_d);
    int idA = atomic_load(&g_q.head)->id;
    for (uint64_t i = 1; i <= 4; i++) msq_enqueue(&g_q, 0, i);
    for (int i = 0; i < 4; i++) hp_retire(&g_d, 1, node_alloc(70));   /* 스레드 1 목록 = R-1 → 다음 retire 가 scan */
    arm(&g_q, HK_DEQ_HEAD, 1);
    bool ok = msq_dequeue(&g_q, 0, &v);
    g_hook = NULL;
    CHECK(g_t1_got[0] == 1 && ok && v == 2);                 /* 스레드 1 이 1, 스레드 0 은 재시도 후 2 */
    CHECK(g_d.rec[1].nscan == 1 && !was_freed(idA));         /* 스레드 1 의 scan 이 A 를 남겼다 (hp0) */
    CHECK(atomic_load(&g_uaf) == 0);                          /* 스레드 0 의 A->next 읽기는 안전했다 */
    hp_scan(&g_d, 1);                                         /* 이제 스레드 0 의 hp 는 지워졌다 */
    CHECK(was_freed(idA) && g_d.rec[1].n == 0);
    drain(&g_q, 0);

    /* B2: load 와 게시 사이(HK_LOADED)에 스레드 1 이 head 를 꺼내고 scan → 아직 게시 전이라 A 는 진짜 해제된다 */
    log_reset();
    for (uint64_t i = 1; i <= 4; i++) msq_enqueue(&g_q, 0, i);
    int idA2 = atomic_load(&g_q.head)->id;
    for (int i = 0; i < 4; i++) hp_retire(&g_d, 1, node_alloc(77));
    arm(&g_q, HK_LOADED, 1);
    ok = msq_dequeue(&g_q, 0, &v);
    g_hook = NULL;
    CHECK(was_freed(idA2));                                   /* 스레드 0 이 쥐었던 포인터는 이미 해제 */
    CHECK(ok && v == 2 && g_t1_got[0] == 1 && atomic_load(&g_uaf) == 0);
    CHECK(g_hook_calls[HK_LOADED] == 2);                      /* 재확인 실패 → 한 번 더 load·게시 */
    drain(&g_q, 0);

    /* B3: next(B) 를 읽은 뒤 hp1 게시 전(HK_DEQ_NEXT)에 스레드 1 이 두 개를 꺼낸다 → B 도 해제된다 */
    log_reset();
    for (uint64_t i = 1; i <= 4; i++) msq_enqueue(&g_q, 0, i);
    int idB3 = atomic_load(&atomic_load(&g_q.head)->next)->id;
    for (int i = 0; i < 3; i++) hp_retire(&g_d, 1, node_alloc(90));   /* 목록 3 → 스레드 1 의 두 번째 retire 에서 scan */
    arm(&g_q, HK_DEQ_NEXT, 2);
    ok = msq_dequeue(&g_q, 0, &v);
    g_hook = NULL;
    CHECK(g_t1_got[0] == 1 && g_t1_got[1] == 2 && ok && v == 3);
    CHECK(was_freed(idB3) && atomic_load(&g_uaf) == 0);       /* "h 가 아직 head?" 재확인이 해제된 B 읽기를 막았다 */
    drain(&g_q, 0);
    for (int t = 0; t < 2; t++) hp_scan(&g_d, t);

    /* B4: 멈춘 스레드가 있어도 미해제 노드 수 <= R */
    log_reset();
    long base = live();                                       /* = 더미 1 */
    node_t *held = hp_protect(&g_d, 0, 0, &g_q.head);         /* 스레드 0: 더미를 쥔 채 영원히 멈춤 */
    int idH = held->id;
    long peak = 0;
    for (uint64_t i = 0; i < 1000; i++) {
        msq_enqueue(&g_q, 1, i);
        msq_dequeue(&g_q, 1, &v);
        if (live() - base > peak) peak = live() - base;
    }
    CHECK(!was_freed(idH) && g_d.rec[1].nretired >= 1000);
    CHECK(peak <= g_d.R && g_d.rec[1].nmax <= g_d.R);        /* 1000 개를 retire 해도 목록은 R(=5) 이하 */
    hp_clear(&g_d, 0, 0);
    hp_scan(&g_d, 1);
    CHECK(was_freed(idH) && live() == base);
    int unitB = FAILS() - unitA;
    (void)unitB;

    /* ---------- Part C 단위: 풀 상한 + 크레딧 큐 ---------- */
    int nC = 6, RC = 2 * nC * HP_K, capC = 64;
    CHECK(hp_pool_bound(6, 2, 12, 64) == 0);                  /* R=12 <= H=12: 잘못된 설정 */
    CHECK(hp_pool_bound(nC, HP_K, RC, capC) == 1 + 64 + 6 * 24);
    CHECK(hp_init(&g_d2, nC, RC, node_free));
    msq_init(&g_b.q, &g_d2); atomic_init(&g_b.inflight, 0); g_b.cap = 3;
    CHECK(bq_push(&g_b, 0, 1) && bq_push(&g_b, 0, 2) && bq_push(&g_b, 0, 3) && !bq_push(&g_b, 0, 4));
    CHECK(bq_pop(&g_b, 3, &v) && v == 1 && bq_push(&g_b, 0, 4) && atomic_load(&g_b.inflight) == 3);
    while (bq_pop(&g_b, 3, &v)) {}
    CHECK(atomic_load(&g_b.inflight) == 0 && !bq_pop(&g_b, 3, &v) && atomic_load(&g_b.inflight) == 0);
    int unitC = FAILS() - unitA - unitB;

    /* ---------- Part C 스트레스: 풀 = 상한 정확히, 소비자 하나가 수시로 멈춤 ---------- */
    if (unitC == 0 && unitA == 0) {
        hp_scan(&g_d2, 3);
        g_b.cap = capC;
        long bound = hp_pool_bound(nC, HP_K, RC, capC);
        atomic_store(&g_limit, live() - 1 + bound);               /* 다른 큐의 더미 + 이 큐의 상한(더미 포함) */
        atomic_store(&g_alloc_fail, 0);
        g_hook = stall_hook;
        int bad = run_stress(NULL, &g_b, &g_d2, 3, 3, SMSG / 2);
        g_hook = NULL;
        CHECK(bad == 0 && atomic_load(&g_order_bad) == 0 && atomic_load(&g_consumed) == 3 * (SMSG / 2));
        CHECK(atomic_load(&g_alloc_fail) == 0);                  /* 상한대로 잡은 풀이 한 번도 모자라지 않았다 */
        CHECK(max_nmax(&g_d2) <= RC && atomic_load(&g_uaf) == 0);
        printf("stress C: pool bound %ld nodes, %d msgs, stalls %d\n", bound, 3 * (SMSG / 2),
               atomic_load(&g_stall_ops) / 64 + 1);
        atomic_store(&g_limit, POOL_N);
    } else skip(3, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
