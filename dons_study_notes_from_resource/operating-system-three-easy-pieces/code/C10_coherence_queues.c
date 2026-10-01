/* C10_coherence_queues.c — 멀티프로세서 스케줄링의 두 가지 비용을 macOS 에서 직접 잰다.
 *  Part 1: 캐시 일관성(coherence) 비용 — 두 스레드가 "같은 캐시 라인" 의 서로 다른 변수를
 *          증가시킬 때(false sharing) vs 128B 떨어뜨렸을 때. (Apple M2 cache line = 128B)
 *  Part 2: SQMS vs MQMS — 모든 스레드가 락 하나로 보호되는 큐 하나를 쓸 때 vs
 *          스레드마다 자기 큐(+자기 락)를 쓸 때의 pop/push 처리량.
 * macOS 는 스레드를 특정 코어에 고정하는 API 가 없으므로(affinity 는 힌트뿐) 배치는 OS 에 맡긴다. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---------------- Part 1: false sharing ---------------- */
#define ITERS 20000000L
struct same_line { long a; long b; };                                         /* 16B 안에 둘 다 */
struct padded    { long a; char pad[120]; long b; } __attribute__((aligned(128)));

static struct same_line S __attribute__((aligned(128)));
static struct padded    P;

static void *inc_long(void *arg) {
    long *p = arg;   /* atomic add: 매번 그 캐시 라인을 '독점(M 상태)' 으로 가져와야 한다 */
    for (long i = 0; i < ITERS; i++) __atomic_fetch_add(p, 1, __ATOMIC_RELAXED);
    return NULL;
}

static double run_pair(long *x, long *y) {
    pthread_t t1, t2;
    double t0 = now_sec();
    pthread_create(&t1, NULL, inc_long, x);
    pthread_create(&t2, NULL, inc_long, y);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return now_sec() - t0;
}

/* ---------------- Part 2: one shared queue vs per-thread queues ---------------- */
#define QCAP 1024
#define OPS_PER_THREAD 2000000L
typedef struct __attribute__((aligned(128))) {   /* 큐끼리 false sharing 방지 */
    pthread_mutex_t lock;
    int buf[QCAP];
    int head, tail, count;
} queue_t;

static void q_init(queue_t *q, int njobs) {
    pthread_mutex_init(&q->lock, NULL);
    q->head = q->tail = q->count = 0;
    for (int i = 0; i < njobs; i++) { q->buf[q->tail] = i; q->tail = (q->tail + 1) % QCAP; q->count++; }
}

/* "스케줄러 한 번" = 큐에서 job 하나 꺼내고(pick next) 다시 뒤에 넣기(time slice 끝) */
static void sched_once(queue_t *q) {
    pthread_mutex_lock(&q->lock);
    int job = q->buf[q->head]; q->head = (q->head + 1) % QCAP;
    q->buf[q->tail] = job;     q->tail = (q->tail + 1) % QCAP;
    pthread_mutex_unlock(&q->lock);
}

typedef struct { queue_t *q; } warg_t;
static void *worker(void *arg) {
    queue_t *q = ((warg_t *)arg)->q;
    for (long i = 0; i < OPS_PER_THREAD; i++) sched_once(q);
    return NULL;
}

static double run_queues(int nthreads, int per_cpu) {
    queue_t *qs = aligned_alloc(128, sizeof(queue_t) * (size_t)nthreads);  /* size 는 128 의 배수여야 함 */
    if (!qs) { perror("aligned_alloc"); exit(1); }
    pthread_t th[16];
    warg_t args[16];
    for (int i = 0; i < nthreads; i++) q_init(&qs[i], 8);
    double t0 = now_sec();
    for (int i = 0; i < nthreads; i++) {
        args[i].q = per_cpu ? &qs[i] : &qs[0];
        pthread_create(&th[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double dt = now_sec() - t0;
    free(qs);
    return (double)nthreads * OPS_PER_THREAD / dt / 1e6;      /* Mops/s */
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("== Part 1: cache-line ping-pong (2 threads x 20M atomic increments) ==");
    printf("  sizeof(same_line)=%zu, offset of b in padded=%zu bytes\n",
           sizeof(struct same_line), (size_t)((char *)&P.b - (char *)&P.a));
    for (int r = 0; r < 2; r++) {
        double t_same = run_pair(&S.a, &S.b);
        double t_pad  = run_pair(&P.a, &P.b);
        printf("  run %d: same line %.3f s   padded %.3f s   (x%.1f slower when sharing)\n",
               r + 1, t_same, t_pad, t_same / t_pad);
    }

    puts("\n== Part 2: scheduler ops/s — single shared queue (SQMS) vs per-thread queues (MQMS) ==");
    puts("  threads   SQMS Mops/s   MQMS Mops/s");
    int ns[] = { 1, 2, 4, 8 };
    for (int k = 0; k < 4; k++) {
        double sq = run_queues(ns[k], 0);
        double mq = run_queues(ns[k], 1);
        printf("  %7d   %11.1f   %11.1f\n", ns[k], sq, mq);
    }
    return 0;
}
