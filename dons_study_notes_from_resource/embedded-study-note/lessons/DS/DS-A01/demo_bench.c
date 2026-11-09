/* DS-A01 demo_bench.c — "수억 개" 2스레드 실행 + 상대 인덱스 캐싱 전/후 처리량 (관찰용, 체크 아님)
 *
 *   plain  : push 마다 head 를, pop 마다 tail 을 acquire 로 읽는다 (Part A)
 *   cached : head_cache / tail_cache 를 믿고, 가득/빔으로 보일 때만 다시 읽는다 (Part B)
 * 두 버전 모두 head/tail 은 128 B 줄로 분리되어 있다 (패딩·배치·슬롯 플래그 비교는 LLAT-M01).
 * 소비자는 값이 0,1,2,... 인지 검사한다 (bad 가 0 이 아니면 결과 무효).
 *
 * build: cc -std=c11 -O2 -pthread demo_bench.c -o db && ./db [N=200000000] [runs=3] [cap=1024]
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    _Alignas(128) atomic_uint tail;
    uint32_t head_cache;
    _Alignas(128) atomic_uint head;
    uint32_t tail_cache;
    _Alignas(128) uint32_t mask;
    uint64_t *buf;
} ring_t;

static inline bool push_plain(ring_t *q, uint64_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (t - h == q->mask + 1u) return false;
    q->buf[t & q->mask] = v;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return true;
}
static inline bool pop_plain(ring_t *q, uint64_t *v) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (t == h) return false;
    *v = q->buf[h & q->mask];
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
    return true;
}
static inline bool push_cached(ring_t *q, uint64_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t - q->head_cache == q->mask + 1u) {
        q->head_cache = atomic_load_explicit(&q->head, memory_order_acquire);
        if (t - q->head_cache == q->mask + 1u) return false;
    }
    q->buf[t & q->mask] = v;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return true;
}
static inline bool pop_cached(ring_t *q, uint64_t *v) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    if (q->tail_cache == h) {
        q->tail_cache = atomic_load_explicit(&q->tail, memory_order_acquire);
        if (q->tail_cache == h) return false;
    }
    *v = q->buf[h & q->mask];
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
    return true;
}

static ring_t g_q;
static uint64_t g_n;
static unsigned long long g_prod_spins;
static bool g_cached;

static void *producer(void *arg) {
    (void)arg;
    unsigned long long spins = 0;
    for (uint64_t i = 0; i < g_n; i++) {
        if (g_cached) { while (!push_cached(&g_q, i)) spins++; }
        else          { while (!push_plain(&g_q, i)) spins++; }
    }
    g_prod_spins = spins;
    return NULL;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double run(bool cached, uint32_t cap, uint64_t *bad_out, unsigned long long *cspins) {
    static uint64_t *buf;
    if (!buf) buf = aligned_alloc(128, (size_t)cap * sizeof *buf);
    g_q.mask = cap - 1u; g_q.buf = buf;
    atomic_store(&g_q.tail, 0u); atomic_store(&g_q.head, 0u);
    g_q.head_cache = 0u; g_q.tail_cache = 0u;
    g_cached = cached;
    pthread_t p;
    double t0 = now_s();
    pthread_create(&p, NULL, producer, NULL);
    uint64_t bad = 0, v;
    unsigned long long spins = 0;
    for (uint64_t e = 0; e < g_n; e++) {
        if (cached) { while (!pop_cached(&g_q, &v)) spins++; }
        else        { while (!pop_plain(&g_q, &v)) spins++; }
        bad += (v != e);
    }
    pthread_join(p, NULL);
    double dt = now_s() - t0;
    *bad_out = bad; *cspins = spins;
    return (double)g_n / dt / 1e6;
}

int main(int argc, char **argv) {
    g_n = argc > 1 ? strtoull(argv[1], NULL, 10) : 200000000ull;
    int runs = argc > 2 ? atoi(argv[2]) : 3;
    uint32_t cap = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 1024u;
    if (cap == 0 || (cap & (cap - 1u))) { fprintf(stderr, "cap must be a power of 2\n"); return 2; }
    double la[3];
    getloadavg(la, 3);
    printf("N=%llu cap=%u runs=%d  loadavg %.2f %.2f %.2f\n", (unsigned long long)g_n, cap, runs, la[0], la[1], la[2]);
    for (int r = 0; r < runs; r++) {
        for (int c = 0; c < 2; c++) {
            uint64_t bad; unsigned long long cs;
            double mops = run(c == 1, cap, &bad, &cs);
            printf("run %d %-6s %7.1f Mops/s  bad=%llu  producer full-spins=%llu consumer empty-spins=%llu\n",
                   r, c ? "cached" : "plain", mops, (unsigned long long)bad, g_prod_spins, cs);
            fflush(stdout);
        }
    }
    getloadavg(la, 3);
    printf("loadavg after %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    return 0;
}
