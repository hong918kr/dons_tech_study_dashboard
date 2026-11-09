/* LLAT-M03 demo_bench.c — hazard pointer vs 최소 EBR vs mutex 큐: 읽기 경로 비용 · 처리량 · 멈춘 스레드의 메모리.
 *
 *   B1  읽기 경로 1 회 (단일 스레드): plain load / HP protect+clear / EBR enter+load+exit  (ns/op)
 *       + 64 노드 리스트 순회 1 회: plain / HP 노드마다 protect / EBR 순회당 enter 1 번       (ns/순회)
 *   B2  enqueue+dequeue 1 쌍 (단일 스레드): HP / EBR / mutex 큐                             (ns/쌍)
 *   B3  P 생산자 x P 소비자 처리량 (P = 1, 2, 4), 150 ms x 5 회: HP / EBR / mutex            (Mops/s)
 *   B4  2P2C + 멈춘 스레드 1 개 (t = 50..250 ms 동안 HP 1 개 쥠 / EBR 읽기 구간 안): 미해제 노드 수 시계열
 *
 * EBR 은 OS-A03 의 전체 구현이 아니라 비교용 최소판 (전역 epoch, 스레드 슬롯 (e<<1)|1, 해제 조건 e+2 <= G).
 * cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench          (전체 ~10 초)
 */
#define USE_MALLOC 1
#define main solution_main
#include "solution.c"
#undef main

static double now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median5(double *v) { qsort(v, 5, sizeof *v, cmp_d); return v[2]; }

/* ---- 스레드별 카운터 (패딩) : retire 수, 해제 수 ---- */
typedef struct { _Alignas(CL) atomic_long v; } pcount_t;
static pcount_t C_ret[MAX_THREADS], C_free[MAX_THREADS];
static _Thread_local int t_tid;
static void bench_free(node_t *n) { free(n); atomic_fetch_add_explicit(&C_free[t_tid].v, 1, memory_order_relaxed); }
static long unreclaimed(void) {
    long s = 0;
    for (int t = 0; t < MAX_THREADS; t++) s += atomic_load(&C_ret[t].v) - atomic_load(&C_free[t].v);
    return s;
}
static node_t *mk(uint64_t v) { node_t *n = malloc(sizeof *n); atomic_init(&n->next, NULL); n->val = v; atomic_init(&n->state, NODE_LIVE); return n; }

/* ---- HP 큐: enqueue 는 미리 만든 노드를 받는다 (solution 의 node_alloc 전역 카운터를 피하려고) ---- */
static void hpq_enq(msq_t *q, int tid, node_t *n) {
    for (;;) {
        node_t *t = hp_protect(q->d, tid, 0, &q->tail);
        node_t *next = atomic_load_explicit(&t->next, memory_order_acquire);
        if (atomic_load_explicit(&q->tail, memory_order_acquire) != t) continue;
        if (next) { atomic_compare_exchange_strong(&q->tail, &t, next); continue; }
        node_t *e = NULL;
        if (atomic_compare_exchange_strong(&t->next, &e, n)) { atomic_compare_exchange_strong(&q->tail, &t, n); break; }
    }
    hp_clear(q->d, tid, 0);
}
static bool hpq_deq(msq_t *q, int tid, uint64_t *v) {
    if (!msq_dequeue(q, tid, v)) return false;
    atomic_fetch_add_explicit(&C_ret[tid].v, 1, memory_order_relaxed);
    return true;
}

/* ---- 최소 EBR ---- */
typedef struct { _Alignas(CL) atomic_uint_fast64_t w; } eslot_t;
typedef struct { _Alignas(CL) node_t **p; uint64_t *e; long n, cap, since; } elimbo_t;
static eslot_t E_slot[MAX_THREADS];
static _Alignas(CL) atomic_uint_fast64_t E_G;
static elimbo_t E_lim[MAX_THREADS];
static int E_nthreads, E_R = 20;
static void ebr_enter(int t) {
    uint64_t e = atomic_load_explicit(&E_G, memory_order_relaxed);
    atomic_store_explicit(&E_slot[t].w, (e << 1) | 1, memory_order_seq_cst);   /* 뒤의 seq_cst load 와 SB 를 막는다 */
}
static void ebr_exit(int t) { atomic_store_explicit(&E_slot[t].w, 0, memory_order_release); }
static void ebr_try_advance(void) {
    uint64_t g = atomic_load(&E_G);
    for (int t = 0; t < E_nthreads; t++) {
        uint64_t w = atomic_load_explicit(&E_slot[t].w, memory_order_seq_cst);
        if ((w & 1) && (w >> 1) != g) return;
    }
    atomic_compare_exchange_strong(&E_G, &g, g + 1);
}
static void ebr_reclaim(int t) {
    elimbo_t *L = &E_lim[t];
    uint64_t g = atomic_load(&E_G);
    long k = 0;
    for (long i = 0; i < L->n; i++) {
        if (L->e[i] + 2 <= g) bench_free(L->p[i]);
        else { L->p[k] = L->p[i]; L->e[k] = L->e[i]; k++; }
    }
    L->n = k;
}
static void ebr_retire(int t, node_t *p) {
    elimbo_t *L = &E_lim[t];
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 1024;
        if (L->cap > (16L << 20)) { fprintf(stderr, "EBR limbo > 16M\n"); exit(3); }
        L->p = realloc(L->p, (size_t)L->cap * sizeof *L->p); L->e = realloc(L->e, (size_t)L->cap * sizeof *L->e);
        if (!L->p || !L->e) { perror("realloc"); exit(2); }
    }
    L->p[L->n] = p; L->e[L->n] = atomic_load(&E_G); L->n++;
    atomic_fetch_add_explicit(&C_ret[t].v, 1, memory_order_relaxed);
    if (++L->since < E_R) return;
    L->since = 0;
    ebr_try_advance();
    ebr_reclaim(t);
}
static void ebrq_enq(msq_t *q, int tid, node_t *n) {
    ebr_enter(tid);
    for (;;) {
        node_t *t = atomic_load_explicit(&q->tail, memory_order_seq_cst);
        node_t *next = atomic_load_explicit(&t->next, memory_order_acquire);
        if (atomic_load_explicit(&q->tail, memory_order_acquire) != t) continue;
        if (next) { atomic_compare_exchange_strong(&q->tail, &t, next); continue; }
        node_t *e = NULL;
        if (atomic_compare_exchange_strong(&t->next, &e, n)) { atomic_compare_exchange_strong(&q->tail, &t, n); break; }
    }
    ebr_exit(tid);
}
static bool ebrq_deq(msq_t *q, int tid, uint64_t *out) {
    ebr_enter(tid);
    for (;;) {
        node_t *h = atomic_load_explicit(&q->head, memory_order_seq_cst);
        node_t *t = atomic_load_explicit(&q->tail, memory_order_acquire);
        node_t *next = nd_next(h);
        if (atomic_load_explicit(&q->head, memory_order_acquire) != h) continue;
        if (!next) { ebr_exit(tid); return false; }
        if (h == t) { atomic_compare_exchange_strong(&q->tail, &t, next); continue; }
        uint64_t v = nd_val(next);
        node_t *e = h;
        if (atomic_compare_exchange_strong(&q->head, &e, next)) { ebr_exit(tid); ebr_retire(tid, h); *out = v; return true; }
    }
}

/* ---- mutex 큐 (뻔한 락 버전) ---- */
typedef struct { pthread_mutex_t mu; node_t *head, *tail; } mq_t;
static void mq_init(mq_t *m) { pthread_mutex_init(&m->mu, NULL); m->head = m->tail = NULL; }
static void mq_enq(mq_t *m, node_t *n) {
    pthread_mutex_lock(&m->mu);
    if (m->tail) atomic_store_explicit(&m->tail->next, n, memory_order_relaxed); else m->head = n;
    m->tail = n;
    pthread_mutex_unlock(&m->mu);
}
static bool mq_deq(mq_t *m, uint64_t *v) {
    pthread_mutex_lock(&m->mu);
    node_t *h = m->head;
    if (h) { m->head = atomic_load_explicit(&h->next, memory_order_relaxed); if (!m->head) m->tail = NULL; }
    pthread_mutex_unlock(&m->mu);
    if (!h) return false;
    *v = h->val; free(h);       /* 락 밖에서 바로 해제 가능: 다른 누구도 h 를 볼 수 없다 */
    return true;
}

enum { V_HP, V_EBR, V_MUTEX };
static const char *VN[] = { "HP", "EBR", "mutex" };
static hp_dom_t HD;
static msq_t HQ, EQ;
static mq_t MQ;
static void setup(int nthreads) {
    hp_init(&HD, nthreads, 2 * nthreads * HP_K, bench_free);
    node_t *d1 = mk(0), *d2 = mk(0);
    atomic_store(&HQ.head, d1); atomic_store(&HQ.tail, d1); HQ.d = &HD;
    atomic_store(&EQ.head, d2); atomic_store(&EQ.tail, d2); EQ.d = NULL;
    E_nthreads = nthreads; atomic_store(&E_G, 0);
    for (int t = 0; t < MAX_THREADS; t++) {
        atomic_store(&E_slot[t].w, 0); E_lim[t].n = 0; E_lim[t].since = 0;
        atomic_store(&C_ret[t].v, 0); atomic_store(&C_free[t].v, 0);
    }
    mq_init(&MQ);
}
static void enq(int var, int tid, node_t *n) { if (var == V_HP) hpq_enq(&HQ, tid, n); else if (var == V_EBR) ebrq_enq(&EQ, tid, n); else mq_enq(&MQ, n); }
static bool deq(int var, int tid, uint64_t *v) { return var == V_HP ? hpq_deq(&HQ, tid, v) : var == V_EBR ? ebrq_deq(&EQ, tid, v) : mq_deq(&MQ, v); }

/* ================= B1: 읽기 경로 ================= */
#define LISTN 64
static _Atomic(node_t *) g_src;
static node_t *g_list;
volatile uint64_t g_sink;
__attribute__((noinline)) static double b1_one(int which, long iters) {
    uint64_t s = 0;
    double t0 = now_ns();
    for (long i = 0; i < iters; i++) {
        node_t *p;
        if (which == 0) { p = atomic_load_explicit(&g_src, memory_order_acquire); s += p->val; }
        else if (which == 1) { p = hp_protect(&HD, 0, 0, &g_src); s += p->val; hp_clear(&HD, 0, 0); }
        else { ebr_enter(0); p = atomic_load_explicit(&g_src, memory_order_seq_cst); s += p->val; ebr_exit(0); }
    }
    g_sink = s;
    return (now_ns() - t0) / (double)iters;
}
__attribute__((noinline)) static double b1_walk(int which, long iters) {
    uint64_t s = 0;
    _Atomic(node_t *) root; atomic_init(&root, g_list);
    double t0 = now_ns();
    for (long i = 0; i < iters; i++) {
        if (which == 2) ebr_enter(0);
        int k = 0;
        node_t *p = which == 1 ? hp_protect(&HD, 0, 0, &root) : atomic_load_explicit(&root, memory_order_acquire);
        while (p) {
            s += p->val;
            if (which == 1) { k ^= 1; p = hp_protect(&HD, 0, k, &p->next); }   /* 다음 노드를 다른 칸에 보호 (손 바꿔 잡기) */
            else p = atomic_load_explicit(&p->next, memory_order_acquire);
        }
        if (which == 1) hp_clear_all(&HD, 0);
        if (which == 2) ebr_exit(0);
    }
    g_sink = s;
    return (now_ns() - t0) / (double)iters;
}

/* ================= B2/B3: 큐 처리량 ================= */
static atomic_int g_stop, g_inflight;
static atomic_long g_deqs;
typedef struct { int var, tid; bool prod; } barg_t;
static void *bworker(void *a) {
    barg_t *b = a; t_tid = b->tid;
    long n = 0; uint64_t v;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        if (b->prod) {
            if (atomic_load_explicit(&g_inflight, memory_order_relaxed) > 1024) { sched_yield(); continue; }
            atomic_fetch_add_explicit(&g_inflight, 1, memory_order_relaxed);
            enq(b->var, b->tid, mk(1));
        } else if (deq(b->var, b->tid, &v)) {
            atomic_fetch_sub_explicit(&g_inflight, 1, memory_order_relaxed); n++;
        }
    }
    atomic_fetch_add(&g_deqs, n);
    return NULL;
}
static void drain_all(int var, int nthreads) {
    uint64_t v; t_tid = 0;
    while (deq(var, 0, &v)) {}
    if (var == V_HP) for (int t = 0; t < nthreads; t++) { t_tid = t; hp_clear_all(&HD, t); hp_scan(&HD, t); }
    if (var == V_EBR) { for (int r = 0; r < 3; r++) ebr_try_advance(); for (int t = 0; t < nthreads; t++) { t_tid = t; ebr_reclaim(t); } }
}
static double b3_run(int var, int P, int ms) {
    pthread_t th[MAX_THREADS]; barg_t ba[MAX_THREADS];
    setup(2 * P);
    atomic_store(&g_stop, 0); atomic_store(&g_inflight, 0); atomic_store(&g_deqs, 0);
    for (int i = 0; i < 2 * P; i++) { ba[i] = (barg_t){ var, i, i < P }; pthread_create(&th[i], NULL, bworker, &ba[i]); }
    struct timespec ts = { 0, (long)ms * 1000000L }; nanosleep(&ts, NULL);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < 2 * P; i++) pthread_join(th[i], NULL);
    drain_all(var, 2 * P);
    return (double)atomic_load(&g_deqs) / (ms * 1e3);
}

/* ================= B4: 멈춘 스레드 ================= */
static atomic_int g_stalling;
static void *staller(void *a) {
    int var = *(int *)a; t_tid = 4;
    while (!atomic_load(&g_stalling)) sched_yield();
    if (var == V_HP) hp_protect(&HD, 4, 0, &HQ.head); else ebr_enter(4);   /* 쥐고(들어가고) 멈춘다 */
    while (atomic_load(&g_stalling) == 1) { struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); }
    if (var == V_HP) hp_clear(&HD, 4, 0); else ebr_exit(4);
    return NULL;
}
static void b4_run(int var, long *series, int nsamp) {
    pthread_t th[6]; barg_t ba[4]; int vv = var;
    setup(5);
    atomic_store(&g_stop, 0); atomic_store(&g_inflight, 0); atomic_store(&g_deqs, 0); atomic_store(&g_stalling, 0);
    for (int i = 0; i < 4; i++) { ba[i] = (barg_t){ var, i, i < 2 }; pthread_create(&th[i], NULL, bworker, &ba[i]); }
    pthread_create(&th[4], NULL, staller, &vv);
    for (int s = 0; s < nsamp; s++) {                /* 25 ms 간격, 50 ms 에 멈춤 시작, 250 ms 에 풀림 */
        if (s == 2) atomic_store(&g_stalling, 1);
        if (s == 10) atomic_store(&g_stalling, 2);
        struct timespec ts = { 0, 25000000 }; nanosleep(&ts, NULL);
        series[s] = unreclaimed();
    }
    atomic_store(&g_stalling, 2); atomic_store(&g_stop, 1);
    for (int i = 0; i < 5; i++) pthread_join(th[i], NULL);
    drain_all(var, 5);
}

int main(int argc, char **argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;        /* 0 = 전부, 1..4 = 그 실험만 */
    double r[5];
    if (which == 0 || which == 1) {
        setup(1);
        node_t *one = mk(7); atomic_store(&g_src, one);
        g_list = NULL; for (int i = 0; i < LISTN; i++) { node_t *n = mk((uint64_t)i); atomic_store(&n->next, g_list); g_list = n; }
        printf("B1 read path (single thread, median of 5)\n");
        const char *nm[] = { "plain acquire load", "HP protect + clear", "EBR enter + load + exit" };
        for (int w = 0; w < 3; w++) { for (int k = 0; k < 5; k++) r[k] = b1_one(w, 20000000); printf("  %-26s %6.2f ns/op\n", nm[w], median5(r)); }
        const char *nw[] = { "walk 64: plain", "walk 64: HP per node", "walk 64: EBR per walk" };
        for (int w = 0; w < 3; w++) { for (int k = 0; k < 5; k++) r[k] = b1_walk(w, 200000); printf("  %-26s %6.1f ns/walk (%.2f ns/node)\n", nw[w], median5(r), median5(r) / LISTN); }
    }
    if (which == 0 || which == 2) {
        printf("B2 enqueue+dequeue pair (single thread, median of 5)\n");
        for (int var = 0; var < 3; var++) {
            for (int k = 0; k < 5; k++) {
                setup(1); uint64_t v; t_tid = 0;
                double t0 = now_ns();
                for (long i = 0; i < 2000000; i++) { enq(var, 0, mk((uint64_t)i)); deq(var, 0, &v); }
                r[k] = (now_ns() - t0) / 2e6;
                drain_all(var, 1);
            }
            printf("  %-6s %6.1f ns/pair\n", VN[var], median5(r));
        }
    }
    if (which == 0 || which == 3) {
        printf("B3 throughput PxP, 150 ms each (dequeues Mops/s, median of 5)\n");
        int Ps[] = { 1, 2, 4 };
        for (int pi = 0; pi < 3; pi++)
            for (int var = 0; var < 3; var++) {
                double x[5]; for (int k = 0; k < 5; k++) x[k] = b3_run(var, Ps[pi], 150);
                qsort(x, 5, sizeof *x, cmp_d);
                printf("  %dP%dC %-6s %7.2f Mops/s  (min %.2f max %.2f)\n", Ps[pi], Ps[pi], VN[var], x[2], x[0], x[4]);
            }
    }
    if (which == 0 || which == 4) {
        printf("B4 unreclaimed nodes, 2P2C + 1 stalled thread (stall 50..250 ms), HP R=%d -> bound N*R=%d\n", 2 * 5 * HP_K, 5 * 2 * 5 * HP_K);
        long sh[16], se[16];
        b4_run(V_HP, sh, 16); b4_run(V_EBR, se, 16);
        printf("  t(ms)        HP        EBR\n");
        for (int s = 0; s < 16; s++) printf("  %5d %9ld %10ld\n", (s + 1) * 25, sh[s], se[s]);
    }
    return 0;
}
