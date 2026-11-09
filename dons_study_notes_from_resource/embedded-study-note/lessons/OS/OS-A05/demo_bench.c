/* OS-A05 demo_bench — 스케줄러 3 종의 확장성 (1..12 워커)
 *
 *   cl     : 워커별 Chase–Lev deque + 무작위 steal (solution.c 와 같은 코드, 훅 없음)
 *   lockdq : 워커별 deque 를 pthread_mutex 로 보호 + 무작위 steal  (분산 효과 vs lock-free 효과를 가르기 위해)
 *   global : 전역 큐 하나(LIFO 스택) + pthread_mutex 하나  ("가장 뻔한" 스레드 풀)
 *
 * 워크로드: fib(n) cutoff c (c 가 작을수록 태스크가 잘다 = 스케줄러 부담 ↑),
 *          psum(2^24 개 uint32, 원소마다 fmix32 해시 16 번의 합, grain 4096 — 해시를 넣어 대역폭이 아니라 CPU 위주로).
 * 종료 판정 2 가지: TERM_MODE=pending (기본, 전역 atomic 카운터를 spawn/완료마다 ±1) / TERM_MODE=root (루트 태스크의 done 만 본다 —
 *   fork-join 에서는 모든 태스크가 루트의 자손이고 join 되므로 이것으로 충분).  예: TERM_MODE=root ./demo_bench fib 34 6
 * 각 칸 = 3 회 중 중앙값(ms). 결과는 매번 직렬 값과 비교한다 (틀리면 BAD).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench
 * run  : ./demo_bench fib 36 18 | ./demo_bench fib 32 6 | ./demo_bench psum     (각 30 초 안쪽)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
}
static int64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct worker worker_t;
typedef struct task task_t;
struct task { void (*fn)(worker_t *, task_t *); int64_t a, b, result; atomic_int done; };

#define CAP 1024
#define MASK (CAP - 1)
typedef struct {
    _Alignas(128) _Atomic(int64_t) top;
    _Alignas(128) _Atomic(int64_t) bottom;
    _Alignas(128) _Atomic(task_t *) buf[CAP];
} deque_t;
static bool dq_push(deque_t *q, task_t *x) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    if (b - t >= CAP) return false;
    atomic_store_explicit(&q->buf[b & MASK], x, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_release);
    return true;
}
static task_t *dq_take(deque_t *q) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    task_t *x = NULL;
    if (t <= b) {
        x = atomic_load_explicit(&q->buf[b & MASK], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                x = NULL;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    return x;
}
static task_t *dq_steal(deque_t *q, bool *abort_) {
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);
    if (t >= b) return NULL;
    task_t *x = atomic_load_explicit(&q->buf[t & MASK], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed)) {
        *abort_ = true; return NULL;
    }
    return x;
}

typedef struct { _Alignas(128) pthread_mutex_t m; int64_t top, bottom; task_t *buf[CAP]; } ldq_t;  /* 락 deque */
typedef struct { _Alignas(128) pthread_mutex_t m; int64_t n; task_t *buf[1 << 16]; } gq_t;        /* 전역 스택 */

enum { M_CL, M_LOCKDQ, M_GLOBAL };
static int g_mode;
static bool g_term_pending = true;
static gq_t g_gq;

typedef struct pool pool_t;
struct worker {
    deque_t dq; ldq_t lq; pool_t *pool; int id; uint32_t rng;
    long executed, steals, aborts, overflow;
};
struct pool { worker_t w[16]; int n; task_t *root; _Alignas(128) atomic_long pending; };

static bool w_push(worker_t *w, task_t *t) {
    if (g_mode == M_CL) return dq_push(&w->dq, t);
    if (g_mode == M_LOCKDQ) {
        ldq_t *q = &w->lq; bool ok;
        pthread_mutex_lock(&q->m);
        if ((ok = q->bottom - q->top < CAP)) q->buf[q->bottom++ & MASK] = t;
        pthread_mutex_unlock(&q->m);
        return ok;
    }
    bool ok;
    pthread_mutex_lock(&g_gq.m);
    if ((ok = g_gq.n < (int64_t)(sizeof g_gq.buf / sizeof g_gq.buf[0]))) g_gq.buf[g_gq.n++] = t;
    pthread_mutex_unlock(&g_gq.m);
    return ok;
}
static task_t *w_take(worker_t *w) {
    task_t *x = NULL;
    if (g_mode == M_CL) return dq_take(&w->dq);
    if (g_mode == M_LOCKDQ) {
        ldq_t *q = &w->lq;
        pthread_mutex_lock(&q->m);
        if (q->bottom > q->top) x = q->buf[--q->bottom & MASK];
        pthread_mutex_unlock(&q->m);
        return x;
    }
    pthread_mutex_lock(&g_gq.m);
    if (g_gq.n > 0) x = g_gq.buf[--g_gq.n];
    pthread_mutex_unlock(&g_gq.m);
    return x;
}
static task_t *w_steal(worker_t *v, bool *ab) {
    if (g_mode == M_CL) return dq_steal(&v->dq, ab);
    task_t *x = NULL;
    ldq_t *q = &v->lq;
    pthread_mutex_lock(&q->m);
    if (q->bottom > q->top) x = q->buf[q->top++ & MASK];
    pthread_mutex_unlock(&q->m);
    return x;
}

static void run_task(worker_t *w, task_t *t) {
    t->fn(w, t);
    w->executed++;
    pool_t *p = w->pool;               /* t 는 done 뒤에 사라질 수 있다 */
    atomic_store_explicit(&t->done, 1, memory_order_release);
    if (g_term_pending) atomic_fetch_sub_explicit(&p->pending, 1, memory_order_release);
}
static void idle_wait(unsigned *idle) { ++*idle; cpu_relax(); if ((*idle & 63u) == 0) sched_yield(); }
static void task_spawn(worker_t *w, task_t *t) {
    if (g_term_pending) atomic_fetch_add_explicit(&w->pool->pending, 1, memory_order_relaxed);
    if (!w_push(w, t)) { w->overflow++; run_task(w, t); }
}
static task_t *find_work(worker_t *w) {
    task_t *t = w_take(w);
    if (t || g_mode == M_GLOBAL) return t;
    pool_t *p = w->pool;
    for (int i = 1; i < p->n; i++) {
        int v = (int)(xorshift32(&w->rng) % (uint32_t)(p->n - 1));
        if (v >= w->id) v++;
        bool ab = false;
        t = w_steal(&p->w[v], &ab);
        if (ab) { w->aborts++; continue; }
        if (t) { w->steals++; return t; }
    }
    return NULL;
}
static void task_join(worker_t *w, task_t *t) {
    unsigned idle = 0;
    while (!atomic_load_explicit(&t->done, memory_order_acquire)) {
        task_t *x = find_work(w);
        if (x) { run_task(w, x); idle = 0; } else idle_wait(&idle);
    }
}
static void worker_loop(worker_t *w) {
    unsigned idle = 0;
    for (;;) {
        if (g_term_pending ? atomic_load_explicit(&w->pool->pending, memory_order_acquire) == 0
                           : atomic_load_explicit(&w->pool->root->done, memory_order_acquire) != 0) break;
        task_t *t = find_work(w);
        if (t) { run_task(w, t); idle = 0; } else idle_wait(&idle);
    }
}
static void *worker_main(void *a) { worker_loop(a); return NULL; }

static void task_init(task_t *t, void (*fn)(worker_t *, task_t *), int64_t a, int64_t b) {
    t->fn = fn; t->a = a; t->b = b; t->result = 0; atomic_init(&t->done, 0);
}
static int64_t fib_serial(int64_t n) { return n < 2 ? n : fib_serial(n - 1) + fib_serial(n - 2); }
static void fib_task(worker_t *w, task_t *t) {
    if (t->a <= t->b) { t->result = fib_serial(t->a); return; }
    task_t c1, c2;
    task_init(&c1, fib_task, t->a - 1, t->b); task_init(&c2, fib_task, t->a - 2, t->b);
    task_spawn(w, &c1); fib_task(w, &c2); task_join(w, &c1);
    t->result = c1.result + c2.result;
}
static uint32_t *g_data;
static inline uint32_t fmix32(uint32_t h) {    /* murmur3 마무리 함수 — 원소마다 CPU 일을 조금 (메모리 대역폭이 아니라 스케줄러를 보려고) */
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16; return h;
}
static inline uint32_t work(uint32_t x) {     /* 원소 하나에 해시 16 번 (벡터화돼도 수십 ms 가 되게) */
    uint32_t acc = 0;
    for (uint32_t r = 0; r < 16; r++) acc += fmix32(x ^ (r * 0x9E3779B9u));
    return acc;
}
static void sum_task(worker_t *w, task_t *t) {
    if (t->b - t->a <= 4096) { uint64_t s = 0; for (int64_t i = t->a; i < t->b; i++) s += work(g_data[i]); t->result = (int64_t)s; return; }
    int64_t mid = t->a + (t->b - t->a) / 2;
    task_t l, r;
    task_init(&l, sum_task, t->a, mid); task_init(&r, sum_task, mid, t->b);
    task_spawn(w, &r); sum_task(w, &l); task_join(w, &r);
    t->result = l.result + r.result;
}

typedef struct { double ms; int64_t result; long steals, aborts, emin, emax; } res_t;
static res_t run_once(int n, void (*fn)(worker_t *, task_t *), int64_t a, int64_t b) {
    pool_t *p = aligned_alloc(128, sizeof(pool_t));
    memset(p, 0, sizeof *p);
    p->n = n;
    for (int i = 0; i < n; i++) {
        p->w[i].pool = p; p->w[i].id = i; p->w[i].rng = 0x9E3779B9u * (uint32_t)(i + 1);
        pthread_mutex_init(&p->w[i].lq.m, NULL);
    }
    g_gq.n = 0;
    task_t root; task_init(&root, fn, a, b);
    p->root = &root;
    atomic_store(&p->pending, 1);
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20);
    int64_t t0 = now_ns();
    w_push(&p->w[0], &root);
    pthread_t th[16];
    for (int i = 1; i < n; i++) pthread_create(&th[i], &at, worker_main, &p->w[i]);
    worker_loop(&p->w[0]);
    for (int i = 1; i < n; i++) pthread_join(th[i], NULL);
    int64_t t1 = now_ns();
    res_t r = { (double)(t1 - t0) / 1e6, root.result, 0, 0, 1L << 62, 0 };
    for (int i = 0; i < n; i++) {
        r.steals += p->w[i].steals; r.aborts += p->w[i].aborts;
        if (p->w[i].executed < r.emin) r.emin = p->w[i].executed;
        if (p->w[i].executed > r.emax) r.emax = p->w[i].executed;
        pthread_mutex_destroy(&p->w[i].lq.m);
    }
    pthread_attr_destroy(&at);
    free(p);
    return r;
}
static int cmp_res(const void *x, const void *y) {
    double a = ((const res_t *)x)->ms, b = ((const res_t *)y)->ms; return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    const char *wl = argc > 1 ? argv[1] : "fib";
    int64_t a = 0, b = 0, want = 0;
    void (*fn)(worker_t *, task_t *) = fib_task;
    if (strcmp(wl, "psum") == 0) {
        fn = sum_task; a = 0; b = 1 << 24;
        g_data = malloc((size_t)b * sizeof *g_data);
        uint32_t s = 2463534242u; uint64_t sum = 0;
        for (int64_t i = 0; i < b; i++) { g_data[i] = xorshift32(&s) & 0xFFFFu; sum += work(g_data[i]); }
        want = (int64_t)sum;
        printf("workload psum 2^24 uint32 (64 MB), sum of 16 x fmix32 per element, grain 4096 -> 4096 leaf tasks\n");
    } else {
        a = argc > 2 ? atoll(argv[2]) : 36; b = argc > 3 ? atoll(argv[3]) : 18;
        want = fib_serial(a);
        printf("workload fib(%lld) cutoff %lld\n", (long long)a, (long long)b);
    }
    pthread_mutex_init(&g_gq.m, NULL);
    const char *term = getenv("TERM_MODE");
    g_term_pending = !(term && strcmp(term, "root") == 0);
    printf("termination: %s\n", g_term_pending ? "pending counter (global atomic)" : "root done flag");
    double la[3]; getloadavg(la, 3);
    printf("load average before: %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    static const int ws[] = { 1, 2, 4, 6, 8, 10, 12 };
    static const char *names[] = { "cl", "lockdq", "global" };
    double base = 0;
    printf("%-7s %3s %9s %8s %9s %9s %13s %s\n", "sched", "W", "ms(med)", "speedup", "steals", "aborts", "tasks min/max", "ok");
    for (int m = 0; m < 3; m++) {
        g_mode = m;
        for (size_t k = 0; k < sizeof ws / sizeof ws[0]; k++) {
            res_t r[3];
            for (int i = 0; i < 3; i++) r[i] = run_once(ws[k], fn, a, b);
            bool ok = r[0].result == want && r[1].result == want && r[2].result == want;
            qsort(r, 3, sizeof r[0], cmp_res);
            if (m == 0 && k == 0) base = r[1].ms;
            printf("%-7s %3d %9.2f %8.2f %9ld %9ld %6ld/%-6ld %s\n", names[m], ws[k], r[1].ms, base / r[1].ms,
                   r[1].steals, r[1].aborts, r[1].emin, r[1].emax, ok ? "OK" : "BAD");
            fflush(stdout);
        }
    }
    getloadavg(la, 3);
    printf("load average after:  %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    return 0;
}
