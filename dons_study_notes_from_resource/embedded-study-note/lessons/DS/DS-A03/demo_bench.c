/* DS-A03 demo_bench.c — 처리량: Vyukov MPMC vs pthread_mutex 링 vs mutex+condvar 링
 *
 *   P = C ∈ {1, 2, 4, 6}, 용량 1024, 메시지 총 N 개 (기본 2,000,000). 각 설정 R 번(기본 3) → 중앙값 Mops/s.
 *   vyukov / mutex : try 연산 + 실패 시 같은 재시도 정책 (yield 명령, 64 번마다 sched_yield)
 *   condvar        : 가득/빔이면 잔다 (OS-B06 의 bounded buffer: while + 두 condvar)
 *   각 생산자는 N/P 개 보내고, 각 소비자는 N/C 개 받는다. 받은 값의 합으로 무손실을 확인한다.
 *
 *   vyukpad        : Vyukov 인데 슬롯 하나를 128 B 로 패딩 (이웃 슬롯 false sharing 제거 효과)
 *   첫 줄 "solo"   : 스레드 하나가 enqueue+dequeue 를 번갈아 (경합 없음) → ns/쌍
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench
 *        cc -std=c11 -Wall -Wextra -Werror -O2 -pthread -mcpu=apple-m1+nolse demo_bench.c -o bench_llsc  (CAS = LDXR/STXR 루프)
 * run  : ./bench [vyukov|vyukpad|mutex|condvar|all] [N] [R] [W]   (all 은 ~10-20 초)
 *        W = 메시지마다 생산자·소비자가 하는 가짜 일 (빈 루프 W 번, 기본 0). "큐만 두드리기" 와 "일도 하는 큐" 비교.
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

#define CAP 1024u
#define LINE 128

static inline void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
}
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + ts.tv_nsec * 1e-9; }

/* ---------------- Vyukov (solution.c 와 같은 코드, 훅 없음) ---------------- */
typedef struct { atomic_size_t seq; uint64_t data; } cell_t;
typedef struct { _Alignas(LINE) atomic_size_t seq; uint64_t data; } pcell_t;   /* 128 B 슬롯 */
#define DEFINE_VQ(NAME, CELL)                                                                         \
typedef struct {                                                                                     \
    CELL buf[CAP];                                                                                   \
    _Alignas(LINE) atomic_size_t enq;                                                                \
    _Alignas(LINE) atomic_size_t deq;                                                                \
    char pad[LINE - sizeof(atomic_size_t)];                                                          \
} NAME##_t;                                                                                          \
static void NAME##_init(NAME##_t *q) {                                                               \
    for (size_t i = 0; i < CAP; i++) { atomic_init(&q->buf[i].seq, i); q->buf[i].data = 0; }         \
    atomic_init(&q->enq, 0); atomic_init(&q->deq, 0);                                                \
}                                                                                                    \
static bool NAME##_try_enq(NAME##_t *q, uint64_t v) {                                                \
    size_t pos = atomic_load_explicit(&q->enq, memory_order_relaxed);                                \
    CELL *c;                                                                                         \
    for (;;) {                                                                                       \
        c = &q->buf[pos & (CAP - 1)];                                                                \
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - pos);        \
        if (dif == 0) {                                                                              \
            if (atomic_compare_exchange_weak_explicit(&q->enq, &pos, pos + 1,                        \
                                                      memory_order_relaxed, memory_order_relaxed))   \
                break;                                                                               \
        } else if (dif < 0) return false;                                                            \
        else pos = atomic_load_explicit(&q->enq, memory_order_relaxed);                              \
    }                                                                                                \
    c->data = v;                                                                                     \
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);                                   \
    return true;                                                                                     \
}                                                                                                    \
static bool NAME##_try_deq(NAME##_t *q, uint64_t *out) {                                             \
    size_t pos = atomic_load_explicit(&q->deq, memory_order_relaxed);                                \
    CELL *c;                                                                                         \
    for (;;) {                                                                                       \
        c = &q->buf[pos & (CAP - 1)];                                                                \
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - (pos + 1));  \
        if (dif == 0) {                                                                              \
            if (atomic_compare_exchange_weak_explicit(&q->deq, &pos, pos + 1,                        \
                                                      memory_order_relaxed, memory_order_relaxed))   \
                break;                                                                               \
        } else if (dif < 0) return false;                                                            \
        else pos = atomic_load_explicit(&q->deq, memory_order_relaxed);                              \
    }                                                                                                \
    *out = c->data;                                                                                  \
    atomic_store_explicit(&c->seq, pos + CAP, memory_order_release);                                 \
    return true;                                                                                     \
}
DEFINE_VQ(vq, cell_t)
DEFINE_VQ(pq, pcell_t)

/* ---------------- mutex 링 (+ condvar 판) ---------------- */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t not_full, not_empty;
    size_t head, tail;
    uint64_t buf[CAP];
} mq_t;
static void mq_init(mq_t *q) {
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL); pthread_cond_init(&q->not_empty, NULL);
    q->head = q->tail = 0;
}
static bool mq_try_enq(mq_t *q, uint64_t v) {
    pthread_mutex_lock(&q->m);
    bool ok = q->tail - q->head < CAP;
    if (ok) q->buf[q->tail++ & (CAP - 1)] = v;
    pthread_mutex_unlock(&q->m);
    return ok;
}
static bool mq_try_deq(mq_t *q, uint64_t *out) {
    pthread_mutex_lock(&q->m);
    bool ok = q->tail != q->head;
    if (ok) *out = q->buf[q->head++ & (CAP - 1)];
    pthread_mutex_unlock(&q->m);
    return ok;
}
static void cq_enq(mq_t *q, uint64_t v) {
    pthread_mutex_lock(&q->m);
    while (q->tail - q->head == CAP) pthread_cond_wait(&q->not_full, &q->m);
    q->buf[q->tail++ & (CAP - 1)] = v;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
}
static uint64_t cq_deq(mq_t *q) {
    pthread_mutex_lock(&q->m);
    while (q->tail == q->head) pthread_cond_wait(&q->not_empty, &q->m);
    uint64_t v = q->buf[q->head++ & (CAP - 1)];
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);
    return v;
}

/* ---------------- 하네스 ---------------- */
enum { K_VYUKOV, K_VYUKPAD, K_MUTEX, K_CONDVAR, K_N };
static const char *kname[] = { "vyukov", "vyukpad", "mutex", "condvar" };
static vq_t g_vq;
static pq_t g_pq;
static mq_t g_mq;
static int g_kind;
static atomic_int g_go;
static unsigned g_work;
static void fake_work(void) { for (unsigned k = 0; k < g_work; k++) __asm__ volatile("" ::: "memory"); }
typedef struct { unsigned id; size_t count; uint64_t sum; } arg_t;

static void backoff(unsigned *f) { if ((++*f & 63u) == 0) sched_yield(); else cpu_relax(); }
static void *producer(void *p) {
    arg_t *a = p;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax();
    for (size_t i = 0; i < a->count; i++) {
        uint64_t v = ((uint64_t)a->id << 40) | i;
        unsigned f = 0;
        if (g_kind == K_VYUKOV) while (!vq_try_enq(&g_vq, v)) backoff(&f);
        else if (g_kind == K_VYUKPAD) while (!pq_try_enq(&g_pq, v)) backoff(&f);
        else if (g_kind == K_MUTEX) while (!mq_try_enq(&g_mq, v)) backoff(&f);
        else cq_enq(&g_mq, v);
        a->sum += v;
        fake_work();
    }
    return NULL;
}
static void *consumer(void *p) {
    arg_t *a = p;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax();
    for (size_t i = 0; i < a->count; i++) {
        uint64_t v = 0;
        unsigned f = 0;
        if (g_kind == K_VYUKOV) while (!vq_try_deq(&g_vq, &v)) backoff(&f);
        else if (g_kind == K_VYUKPAD) while (!pq_try_deq(&g_pq, &v)) backoff(&f);
        else if (g_kind == K_MUTEX) while (!mq_try_deq(&g_mq, &v)) backoff(&f);
        else v = cq_deq(&g_mq);
        a->sum += v;
        fake_work();
    }
    return NULL;
}
static double run_once(int kind, unsigned pc, size_t n, bool *ok) {
    g_kind = kind;
    if (kind == K_VYUKOV) vq_init(&g_vq); else if (kind == K_VYUKPAD) pq_init(&g_pq); else mq_init(&g_mq);
    atomic_store(&g_go, 0);
    pthread_t th[32];
    arg_t args[32];
    size_t per = n / pc;
    for (unsigned k = 0; k < 2 * pc; k++) {
        args[k] = (arg_t){ k < pc ? k : k - pc, per, 0 };
        pthread_create(&th[k], NULL, k < pc ? producer : consumer, &args[k]);
    }
    double t0 = now_s();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (unsigned k = 0; k < 2 * pc; k++) pthread_join(th[k], NULL);
    double dt = now_s() - t0;
    uint64_t ps = 0, cs = 0;
    for (unsigned k = 0; k < pc; k++) { ps += args[k].sum; cs += args[pc + k].sum; }
    *ok = ps == cs;
    if (kind >= K_MUTEX) { pthread_mutex_destroy(&g_mq.m); pthread_cond_destroy(&g_mq.not_full); pthread_cond_destroy(&g_mq.not_empty); }
    return (double)(per * pc) / dt / 1e6;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    size_t n = argc > 2 ? (size_t)atol(argv[2]) : 2000000u;
    int runs = argc > 3 ? atoi(argv[3]) : 3;
    g_work = argc > 4 ? (unsigned)atoi(argv[4]) : 0u;
    if (runs < 1 || runs > 9) runs = 3;
    unsigned cfg[] = { 1, 2, 4, 6 };
    printf("N=%zu msgs, cap %u, work W=%u, %d runs per cell, median Mops/s (min-max)\n", n, CAP, g_work, runs);
    {   /* 경합 없음: 한 스레드가 enq+deq 를 번갈아 */
        const size_t m = 20000000u;
        uint64_t v, sum = 0;
        vq_init(&g_vq);
        double t0 = now_s();
        for (size_t i = 0; i < m; i++) { vq_try_enq(&g_vq, i); vq_try_deq(&g_vq, &v); sum += v; }
        double tv = (now_s() - t0) / (double)m * 1e9;
        mq_init(&g_mq);
        t0 = now_s();
        for (size_t i = 0; i < m; i++) { mq_try_enq(&g_mq, i); mq_try_deq(&g_mq, &v); sum += v; }
        double tm = (now_s() - t0) / (double)m * 1e9;
        printf("solo     vyukov %.2f ns/pair, mutex %.2f ns/pair (sum %llu)\n", tv, tm, (unsigned long long)(sum & 0xFFu));
    }
    for (int kind = 0; kind < K_N; kind++) {
        if (strcmp(which, "all") && strcmp(which, kname[kind])) continue;
        printf("%-8s", kname[kind]);
        for (size_t c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
            double r[9];
            bool all_ok = true;
            for (int i = 0; i < runs; i++) { bool ok; r[i] = run_once(kind, cfg[c], n, &ok); all_ok = all_ok && ok; }
            qsort(r, (size_t)runs, sizeof r[0], cmpd);
            printf("  %ux%u %6.1f (%5.1f-%5.1f)%s", cfg[c], cfg[c], r[runs / 2], r[0], r[runs - 1], all_ok ? "" : " SUM-MISMATCH");
            fflush(stdout);
        }
        printf("\n");
    }
    return 0;
}
