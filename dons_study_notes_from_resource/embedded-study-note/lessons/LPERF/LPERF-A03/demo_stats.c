/* LPERF-A03 demo_stats.c — "스레드마다 자기 통계만 올린다" 는 코드에 숨은 false sharing
 *
 * VARIANT 0 hidden : 원래 코드. slot[] 이 24 B 씩 붙어 있고 limit/total_ops 도 같은 라인에 있다
 * VARIANT 1 padded : 쓰는 주체별로 라인을 나눈다 (total_ops 의 true sharing 은 일부러 남긴다)
 * VARIANT 2 tlagg  : padded + 스레드 로컬 집계. 공유 라인에 쓰는 일이 없고 주기적으로 자기 칸에만 발행
 * -DCL=64|128  패딩 단위 (기본 128, ARCH-M08 측정값)
 * -DUSE_TOTAL=0  total_ops 갱신을 뺀다 (true sharing 없이 false sharing 만 재기)
 * -DTRACE=1    샘플링된 접근을 stdout 에 trace 로 찍는다 (solution.py report 의 입력)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread -DVARIANT=0 demo_stats.c -o hidden
 * run:   ./hidden [threads=4] [iters=2^24] [sample_shift=14]
 *        sample_shift: TRACE 빌드에서 접근 하나마다 독립으로 1/2^shift 확률로 기록 (PMU 샘플링 흉내)
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

#ifndef VARIANT
#define VARIANT 0
#endif
#ifndef CL
#define CL 128
#endif
#ifndef TRACE
#define TRACE 0
#endif
#ifndef USE_TOTAL
#define USE_TOTAL 1
#endif
#define MAX_THREADS 8
#define PUBLISH_EVERY 65536u /* tlagg: 자기 칸에 발행하는 주기 */
#define LIMIT 1024u         /* 길이가 이보다 크면 drop */

#if VARIANT == 0
static const char *const g_name = "hidden";
typedef struct { /* 24 B: 슬롯 경계가 라인 경계와 맞지 않는다 */
    _Atomic uint64_t pkts, bytes, drops;
} worker_slot_t;
typedef struct {
    _Alignas(128) uint32_t limit; /* 초기화 뒤 읽기 전용, 매 반복 읽는다 (재현을 위해 시작 주소만 고정) */
    uint32_t flags;
    _Atomic uint64_t total_ops;   /* 모든 워커가 RMW */
    worker_slot_t slot[MAX_THREADS];
} stats_hub_t;
#else
static const char *const g_name = VARIANT == 1 ? "padded" : "tlagg";
typedef struct {
    _Alignas(CL) _Atomic uint64_t pkts;
    _Atomic uint64_t bytes, drops, ops;
} worker_slot_t;
_Static_assert(sizeof(worker_slot_t) == CL, "slot must fill exactly one line");
typedef struct {
    _Alignas(CL) uint32_t limit;       /* 읽기 위주 라인 */
    uint32_t flags;
    _Alignas(CL) _Atomic uint64_t total_ops; /* tlagg 에서는 쓰지 않는다 */
    worker_slot_t slot[MAX_THREADS];
} stats_hub_t;
_Static_assert(offsetof(stats_hub_t, total_ops) % CL == 0, "total_ops starts a line");
_Static_assert(offsetof(stats_hub_t, slot) % CL == 0, "slot[] starts a line");
#endif

static stats_hub_t g_hub;
static uint64_t g_iters;
static uint64_t g_sample_mask;

/* ---- start barrier (macOS 에 pthread_barrier_t 가 없다) ---- */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_waiting, g_need;
static void start_wait(void)
{
    pthread_mutex_lock(&g_mu);
    if (++g_waiting == g_need)
        pthread_cond_broadcast(&g_cv);
    while (g_waiting < g_need)
        pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
}

typedef struct {
    uint64_t seq;
    const volatile void *addr;
    char op;
    int line;
} rec_t;

typedef struct {
    int id;
    pthread_t th;
    uint64_t seed;
    rec_t *buf;
    size_t cap, n, dropped;
    uint64_t sx; /* TRACE: 샘플링용 PRNG 상태 (카운터 값과 무관) */
    double t_start, t_end;
} thr_t;

#if TRACE
static _Atomic uint64_t g_seq; /* trace 전역 순서: 샘플링된 접근에서만 건드린다 */

static void trace_rec(thr_t *t, const volatile void *addr, char op, int line)
{
    if (t->n == t->cap) { /* 버퍼가 차면 버리고 센다: 멈추지 않는다 */
        t->dropped++;
        return;
    }
    rec_t *r = &t->buf[t->n++];
    r->seq = atomic_fetch_add_explicit(&g_seq, 1, memory_order_relaxed);
    r->addr = addr;
    r->op = op;
    r->line = line;
}
static inline int sample_hit(thr_t *t)
{
    uint64_t x = t->sx;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    t->sx = x;
    return (x & g_sample_mask) == 0;
}
#define T_R(p) do { if (sample_hit(t)) trace_rec(t, (p), 'R', __LINE__); } while (0)
#define T_W(p) do { if (sample_hit(t)) trace_rec(t, (p), 'W', __LINE__); } while (0)
#define T_PUB(p) trace_rec(t, (p), 'W', __LINE__) /* 공유 메모리 쓰기는 전부 남긴다: HITM 0 이 샘플링 탓이 아니게 */
#else
#define T_R(p) ((void)t)
#define T_W(p) ((void)t)
#define T_PUB(p) ((void)t)
#endif

/* 단일 작성자 카운터: 읽고 더해 쓴다 (RMW 가 아니다, 작성자가 하나라서 안전) */
#define BUMP(p, v) do {                                                  \
        T_R(p); T_W(p);                                                  \
        atomic_store_explicit((p), atomic_load_explicit((p), memory_order_relaxed) + (v), \
                              memory_order_relaxed);                     \
    } while (0)

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void *worker(void *arg)
{
    thr_t *t = arg;
    worker_slot_t *s = &g_hub.slot[t->id];
    uint64_t x = t->seed;
#if defined(__APPLE__)
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
#if VARIANT == 2
    uint64_t l_pkts = 0, l_bytes = 0, l_drops = 0, l_ops = 0;
#endif
    start_wait();
    t->t_start = now_s();
    for (uint64_t i = 0; i < g_iters; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint32_t len = 64u + (uint32_t)((x >> 20) & 1023u);
        T_R(&g_hub.limit);
        int accepted = len <= g_hub.limit;
#if VARIANT == 2
        if (accepted) {
            l_pkts++;
            l_bytes += len;
        } else {
            l_drops++;
        }
        l_ops++;
        if ((i + 1) % PUBLISH_EVERY == 0 || i + 1 == g_iters) {
            /* 다른 스레드와 공유하는 라인이 아니라 내 칸에만 쓴다 */
            T_PUB(&s->pkts);
            atomic_store_explicit(&s->pkts, l_pkts, memory_order_relaxed);
            T_PUB(&s->bytes);
            atomic_store_explicit(&s->bytes, l_bytes, memory_order_relaxed);
            T_PUB(&s->drops);
            atomic_store_explicit(&s->drops, l_drops, memory_order_relaxed);
            T_PUB(&s->ops);
            atomic_store_explicit(&s->ops, l_ops, memory_order_relaxed);
        }
#else
        if (accepted) {
            BUMP(&s->pkts, 1);
            BUMP(&s->bytes, len);
        } else {
            BUMP(&s->drops, 1);
        }
#if USE_TOTAL
        T_R(&g_hub.total_ops);
        T_W(&g_hub.total_ops);
        atomic_fetch_add_explicit(&g_hub.total_ops, 1, memory_order_relaxed);
#endif
#endif
    }
    t->t_end = now_s();
    return NULL;
}

#if TRACE
static int cmp_seq(const void *a, const void *b)
{
    uint64_t x = ((const rec_t *)a)->seq, y = ((const rec_t *)b)->seq;
    return (x > y) - (x < y);
}
#endif

int main(int argc, char **argv)
{
    int nthr = argc > 1 ? atoi(argv[1]) : 4;
    g_iters = argc > 2 ? strtoull(argv[2], NULL, 0) : (1ull << 24);
    int shift = argc > 3 ? atoi(argv[3]) : 14;
    if (nthr < 1 || nthr > MAX_THREADS || shift < 0 || shift > 40) {
        fprintf(stderr, "usage: %s [threads 1..%d] [iters] [sample_shift]\n", argv[0], MAX_THREADS);
        return 2;
    }
    g_sample_mask = (1ull << shift) - 1;
    g_hub.limit = LIMIT;
    g_need = nthr + 1;

    thr_t thr[MAX_THREADS] = {0};
    size_t cap = TRACE ? (size_t)((g_iters >> shift) * 16 + (g_iters / PUBLISH_EVERY + 2) * 4 + 256) : 0;
    for (int i = 0; i < nthr; i++) {
        thr[i].id = i;
        thr[i].seed = 0x9E3779B97F4A7C15ull * (uint64_t)(i + 1);
        thr[i].sx = 0xD1B54A32D192ED03ull * (uint64_t)(i + 1);
        thr[i].cap = cap;
        thr[i].buf = cap ? calloc(cap, sizeof(rec_t)) : NULL;
        if (cap && !thr[i].buf) {
            fprintf(stderr, "out of memory\n");
            return 2;
        }
        if (pthread_create(&thr[i].th, NULL, worker, &thr[i]) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return 2;
        }
    }
    start_wait();
    double t0 = 1e30, t1 = 0;
    for (int i = 0; i < nthr; i++) {
        pthread_join(thr[i].th, NULL);
        if (thr[i].t_start < t0)
            t0 = thr[i].t_start;
        if (thr[i].t_end > t1)
            t1 = thr[i].t_end;
    }
    double sec = t1 - t0; /* 가장 먼저 시작한 워커 ~ 가장 늦게 끝난 워커 */

    uint64_t ops = 0, want = (uint64_t)nthr * g_iters;
    for (int i = 0; i < nthr; i++) {
        worker_slot_t *s = &g_hub.slot[i];
        uint64_t p = atomic_load(&s->pkts), d = atomic_load(&s->drops);
        ops += p + d;
    }
    int ok = ops == want;
#if VARIANT != 2 && USE_TOTAL
    ok = ok && atomic_load(&g_hub.total_ops) == want;
#endif
    fprintf(stderr, "variant=%s total=%d CL=%d threads=%d iters=%llu sec=%.3f Mops=%.1f check=%s\n",
            g_name, USE_TOTAL, CL, nthr, (unsigned long long)g_iters, sec,
            (double)want / sec / 1e6, ok ? "ok" : "BAD");

#if TRACE
    size_t total = 0, dropped = 0;
    for (int i = 0; i < nthr; i++) {
        total += thr[i].n;
        dropped += thr[i].dropped;
    }
    rec_t *all = calloc(total ? total : 1, sizeof(rec_t));
    if (!all) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    size_t k = 0;
    for (int i = 0; i < nthr; i++) {
        for (size_t j = 0; j < thr[i].n; j++) {
            all[k] = thr[i].buf[j];
            all[k].line = thr[i].buf[j].line * 16 + i; /* qsort 뒤에도 tid 를 알도록 line 에 함께 싣는다 */
            k++;
        }
    }
    qsort(all, total, sizeof(rec_t), cmp_seq);
    printf("# LPERF-A03 trace variant=%s CL=%d threads=%d iters=%llu shift=%d dropped=%zu\n",
           g_name, CL, nthr, (unsigned long long)g_iters, shift, dropped);
    printf("# base=%p limit=+0x%zx total_ops=+0x%zx slot0=+0x%zx sizeof_slot=%zu\n",
           (void *)&g_hub, offsetof(stats_hub_t, limit), offsetof(stats_hub_t, total_ops),
           offsetof(stats_hub_t, slot), sizeof(worker_slot_t));
    for (size_t i = 0; i < total; i++)
        printf("%llu %d %p %c demo_stats.c:%d\n", (unsigned long long)all[i].seq,
               all[i].line % 16, (const void *)all[i].addr, all[i].op, all[i].line / 16);
    free(all);
    for (int i = 0; i < nthr; i++)
        free(thr[i].buf);
#endif
    return ok ? 0 : 1;
}
