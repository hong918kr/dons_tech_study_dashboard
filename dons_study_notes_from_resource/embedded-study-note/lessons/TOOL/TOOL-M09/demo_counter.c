/* TOOL-M09 demo_counter.c — 버그 1: 공유 카운터 레이스 (counter++ 를 락 없이)
 *
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_counter.c -o dc_t && ./dc_t           # 전역 변수
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DHEAP demo_counter.c -o dc_h && ./dc_h    # 힙 블록
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DHALF demo_counter.c -o dc_hf && ./dc_hf  # 반만 고침: 한쪽만 락
 *   cc -std=c11 -O2 -pthread demo_counter.c -o dc && ./dc                                    # TSan 없이: 잃은 증가 수
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DFIX demo_counter.c -o dc_ft && ./dc_ft   # 고친 버전 (atomic): 보고 없음
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define NT    2
#define ITERS 1000000

typedef struct {
#ifdef FIX
    atomic_ulong samples;            /* 고친 버전: 원자 RMW 하나 */
#else
    unsigned long samples;           /* 버그: 평범한 변수를 두 스레드가 ++ */
#endif
    unsigned long pad;
} stats_t;

#ifdef HEAP
static stats_t *g_stats;             /* 힙에 둔 경우 → "Location is heap block" */
#define STATS (g_stats)
#else
static stats_t g_stats_obj;          /* 전역에 둔 경우 → "Location is global" */
#define STATS (&g_stats_obj)
#endif

#ifdef HALF
static pthread_mutex_t g_stats_lock; /* main 에서 pthread_mutex_init → "Mutex M0 created at" */
#endif

__attribute__((noinline)) static void stats_bump(stats_t *s) {
#ifdef FIX
    atomic_fetch_add_explicit(&s->samples, 1, memory_order_relaxed);
#elif defined(HALF)
    pthread_mutex_lock(&g_stats_lock);
    s->samples++;                    /* 여기는 락 안 */
    pthread_mutex_unlock(&g_stats_lock);
#else
    s->samples++;                    /* load → add → store : 원자적이지 않다 (OS-B04) */
#endif
}

__attribute__((noinline)) static void record_sample(int v) {
    if (v >= 0) stats_bump(STATS);   /* 호출 깊이를 하나 더 둬서 스택이 보이게 */
}

static void *sensor_task(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) record_sample(i);
    return NULL;
}

#ifdef HALF
/* 텔레메트리 덤프: 주기적으로 읽고 0 으로 되돌린다 — 그런데 락을 깜빡했다 */
__attribute__((noinline)) static unsigned long stats_read_and_clear(stats_t *s) {
    unsigned long v = s->samples;
    s->samples = 0;
    return v;
}
static void *telemetry_task(void *arg) {
    unsigned long *total = arg;
    for (int i = 0; i < 1000; i++) *total += stats_read_and_clear(STATS);
    return NULL;
}
#endif

int main(void) {
#ifdef HEAP
    g_stats = calloc(1, sizeof *g_stats);
    if (!g_stats) return 1;
#endif
#ifdef HALF
    pthread_mutex_init(&g_stats_lock, NULL);
    unsigned long dumped = 0;
    pthread_t tel;
    pthread_create(&tel, NULL, telemetry_task, &dumped);
#endif
    pthread_t t[NT];
    for (int i = 0; i < NT; i++) pthread_create(&t[i], NULL, sensor_task, NULL);
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
#ifdef FIX
    unsigned long got = atomic_load(&STATS->samples);
#elif defined(HALF)
    pthread_join(tel, NULL);
    unsigned long got = dumped + STATS->samples;
#else
    unsigned long got = STATS->samples;
#endif
    printf("samples=%lu expect=%d lost=%ld\n", got, NT * ITERS, (long)NT * ITERS - (long)got);
#ifdef HEAP
    free(g_stats);
#endif
    return 0;
}
