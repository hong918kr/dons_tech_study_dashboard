/* BUILD: -pthread */
/* OS-M06 demo_bench.c — 스레드 1/2/4/8 x {전역 락, arena, arena+tcache, 시스템 malloc} 처리량 (ops/s)
 *   W1 local : 스레드마다 슬롯 256 개, 매 op 마다 "비었으면 malloc(16..256), 차 있으면 free" (자기 블록만 해제)
 *   W2 remote: 생산자→소비자 짝(SPSC 링). 생산자 malloc(64..256) → 소비자 free = 모든 free 가 remote
 *              arena 모드는 remote free 정책 2개: lock(주인 arena 락을 잡고 반납) / stack(lock-free 스택에 push)
 *   W3 fan-in: 생산자 1 (RX 흉내) → 소비자 1/3/7 (링 하나씩, 라운드로빈 분배). 모든 free 가 arena 하나로 몰린다
 *   arena 수 = 8 (스레드 8 개까지 각자 자기 arena). 각 칸 = 5 회 중 최고값. 시간은 CHECK 에 쓰지 않는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
 */
#define MM_NO_MAIN
#include "solution.c"
#include <time.h>
#include <sched.h>

enum { W1_OPS = 400000, W2_N = 200000, W3_N = 100000, SLOTS = 256, RING = 1024, REPS = 5 };
static int g_sys;                                   /* 1 이면 libc malloc/free */
static void *xmalloc(size_t n) { return g_sys ? malloc(n) : mm_malloc(n); }
static void  xfree(void *p) { if (g_sys) free(p); else mm_free(p); }
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static atomic_int g_ready;                          /* macOS 에는 pthread_barrier 가 없다 → 원자 카운터로 출발선 */
static void start_line(int n) { atomic_fetch_add(&g_ready, 1); while (atomic_load(&g_ready) < n) sched_yield(); }

typedef struct { _Alignas(128) int idx, nthreads; long sink; } w1_t;
static void *w1_body(void *arg) {
    w1_t *w = arg;
    void *slot[SLOTS] = { 0 };
    uint32_t s = 0x1234567u + (uint32_t)w->idx * 977u;
    start_line(w->nthreads);
    for (int i = 0; i < W1_OPS; i++) {
        uint32_t r = xs(&s), k = r % SLOTS;
        if (slot[k]) { xfree(slot[k]); slot[k] = NULL; }
        else { char *p = xmalloc(16 + (r >> 8) % 241); p[0] = (char)r; slot[k] = p; }
    }
    for (int k = 0; k < SLOTS; k++) xfree(slot[k]);
    if (!g_sys) mm_thread_exit();
    return NULL;
}
typedef struct { _Alignas(128) _Atomic unsigned long head; _Alignas(128) _Atomic unsigned long tail; void *buf[RING]; } ring_t;
typedef struct { ring_t *r; int prod, nthreads; } w2_t;
static void *w2_body(void *arg) {
    w2_t *w = arg; ring_t *r = w->r;
    uint32_t s = 0xBEEFu + (uint32_t)w->prod;
    start_line(w->nthreads);
    for (int i = 0; i < W2_N; i++) {
        if (w->prod) {
            char *p = xmalloc(64 + xs(&s) % 193); p[0] = 1;
            unsigned long t = atomic_load_explicit(&r->tail, memory_order_relaxed);
            while (t - atomic_load_explicit(&r->head, memory_order_acquire) == RING) sched_yield();
            r->buf[t % RING] = p;
            atomic_store_explicit(&r->tail, t + 1, memory_order_release);
        } else {
            unsigned long h = atomic_load_explicit(&r->head, memory_order_relaxed);
            while (atomic_load_explicit(&r->tail, memory_order_acquire) == h) sched_yield();
            void *p = r->buf[h % RING];
            atomic_store_explicit(&r->head, h + 1, memory_order_release);
            xfree(p);
        }
    }
    if (!g_sys) mm_thread_exit();
    return NULL;
}
static ring_t g_r3[8];
typedef struct { int k, nthreads; } w3_t;
static void *w3_prod(void *arg) {                   /* 소비자 k 개의 링에 돌아가며 넣는다 */
    w3_t *w = arg;
    uint32_t s = 0xFACEu;
    start_line(w->nthreads);
    for (long i = 0; i < (long)W3_N * w->k; i++) {
        ring_t *r = &g_r3[i % w->k];
        char *p = xmalloc(64 + xs(&s) % 193); p[0] = 1;
        unsigned long t = atomic_load_explicit(&r->tail, memory_order_relaxed);
        while (t - atomic_load_explicit(&r->head, memory_order_acquire) == RING) sched_yield();
        r->buf[t % RING] = p;
        atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    }
    if (!g_sys) mm_thread_exit();
    return NULL;
}
static void *w3_cons(void *arg) {
    w2_t *w = arg; ring_t *r = w->r;
    start_line(w->nthreads);
    for (int i = 0; i < W3_N; i++) {
        unsigned long h = atomic_load_explicit(&r->head, memory_order_relaxed);
        while (atomic_load_explicit(&r->tail, memory_order_acquire) == h) sched_yield();
        void *p = r->buf[h % RING];
        atomic_store_explicit(&r->head, h + 1, memory_order_release);
        xfree(p);
    }
    if (!g_sys) mm_thread_exit();
    return NULL;
}
static double run_w3(int k) {
    pthread_t tp, t[8]; w2_t w[8]; w3_t wp = { k, k + 1 };
    atomic_store(&g_ready, 0);
    double t0 = now_s();
    pthread_create(&tp, NULL, w3_prod, &wp);
    for (int i = 0; i < k; i++) {
        atomic_store(&g_r3[i].head, 0); atomic_store(&g_r3[i].tail, 0);
        w[i] = (w2_t){ &g_r3[i], 0, k + 1 };
        pthread_create(&t[i], NULL, w3_cons, &w[i]);
    }
    pthread_join(tp, NULL);
    for (int i = 0; i < k; i++) pthread_join(t[i], NULL);
    return (double)k * W3_N * 2 / (now_s() - t0);
}
static double run_w1(int nt) {
    pthread_t t[8]; w1_t w[8];
    atomic_store(&g_ready, 0);
    double t0 = now_s();
    for (int i = 0; i < nt; i++) { w[i].idx = i; w[i].nthreads = nt; pthread_create(&t[i], NULL, w1_body, &w[i]); }
    for (int i = 0; i < nt; i++) pthread_join(t[i], NULL);
    return (double)nt * (W1_OPS + SLOTS) / (now_s() - t0);
}
static double run_w2(int pairs) {
    pthread_t t[8]; w2_t w[8]; static ring_t rings[4];
    atomic_store(&g_ready, 0);
    double t0 = now_s();
    for (int i = 0; i < pairs; i++) {
        atomic_store(&rings[i].head, 0); atomic_store(&rings[i].tail, 0);
        w[2 * i] = (w2_t){ &rings[i], 1, 2 * pairs }; w[2 * i + 1] = (w2_t){ &rings[i], 0, 2 * pairs };
        pthread_create(&t[2 * i], NULL, w2_body, &w[2 * i]); pthread_create(&t[2 * i + 1], NULL, w2_body, &w[2 * i + 1]);
    }
    for (int i = 0; i < 2 * pairs; i++) pthread_join(t[i], NULL);
    return (double)pairs * W2_N * 2 / (now_s() - t0);    /* malloc + free 각각 1 op */
}
typedef struct { const char *name; int sys, mode, narenas, policy; } cfg_t;
static double best(const cfg_t *c, int w, int n) {
    double b = 0;
    for (int r = 0; r < REPS; r++) {
        g_sys = c->sys;
        if (!g_sys) mm_init_cfg(c->mode, c->narenas, c->policy);
        double v = w == 1 ? run_w1(n) : w == 2 ? run_w2(n) : run_w3(n);
        if (!g_sys && (mm_drain_all(), sum_live() != 0 || mm_check(1) != 0)) printf("  !! %s: heap not clean\n", c->name);
        if (v > b) b = v;
    }
    return b;
}
int main(void) {
    double la[3];
    getloadavg(la, 3);
    printf("load average before: %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    static const cfg_t c1[] = {
        { "global lock (1 arena)", 0, MT_GLOBAL, 1, RF_LOCK },
        { "arenas x8",             0, MT_ARENAS, 8, RF_LOCK },
        { "arenas x8 + tcache",    0, MT_TCACHE, 8, RF_STACK },
        { "system malloc (libc)",  1, 0, 0, 0 },
    };
    printf("\nW1 local churn, Mops/s (malloc or free = 1 op), best of %d\n%-24s %8s %8s %8s %8s\n", REPS, "allocator", "1 thr", "2 thr", "4 thr", "8 thr");
    for (int i = 0; i < 4; i++) {
        printf("%-24s", c1[i].name);
        for (int n = 1; n <= 8; n *= 2) printf(" %8.1f", best(&c1[i], 1, n) / 1e6);
        printf("\n");
    }
    static const cfg_t c2[] = {
        { "global lock (1 arena)", 0, MT_GLOBAL, 1, RF_LOCK },
        { "arenas x8, remote=lock", 0, MT_ARENAS, 8, RF_LOCK },
        { "arenas x8, remote=stack", 0, MT_ARENAS, 8, RF_STACK },
        { "arenas+tcache, stack",  0, MT_TCACHE, 8, RF_STACK },
        { "system malloc (libc)",  1, 0, 0, 0 },
    };
    printf("\nW2 producer->consumer (every free is remote), Mops/s, best of %d\n%-24s %8s %8s %8s\n", REPS, "allocator", "1 pair", "2 pairs", "4 pairs");
    for (int i = 0; i < 5; i++) {
        printf("%-24s", c2[i].name);
        for (int n = 1; n <= 4; n *= 2) printf(" %8.1f", best(&c2[i], 2, n) / 1e6);
        printf("\n");
    }
    printf("\nW3 fan-in: 1 producer -> k consumers (all frees go to ONE arena), Mops/s, best of %d\n%-24s %8s %8s %8s\n", REPS, "allocator", "k=1", "k=3", "k=7");
    for (int i = 1; i < 5; i++) {
        printf("%-24s", c2[i].name);
        for (int k = 1; k <= 7; k = 2 * k + 1) printf(" %8.1f", best(&c2[i], 3, k) / 1e6);
        printf("\n");
    }
    mm_destroy();
    getloadavg(la, 3);
    printf("\nload average after: %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    return 0;
}
