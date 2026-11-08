/* OS-M07 demo_convoy.c — 코어보다 스레드가 많을 때 (oversubscription) ticket lock 의 convoy.
 *
 * ticket lock 은 "다음 번호" 만 들어갈 수 있다. 그 스레드가 선점(preempt)되어 CPU 에 없으면,
 * 락이 풀려 있어도 아무도 못 들어가고 나머지는 모두 헛돈다 → 스케줄러가 그 스레드를 다시 올릴 때까지 정지.
 * TAS/TTAS 는 "아무나" 들어갈 수 있어서 이 문제가 없다 (대신 불공정). 뮤텍스는 잠들어 CPU 를 양보한다.
 *
 *   cc -std=c11 -O2 -pthread demo_convoy.c -o convoy && ./convoy [W_ms=150] [reps=3]
 * 이 맥 = 12 코어 (sysctl hw.ncpu). 스레드 6 / 12 / 24 / 48.
 * 열: Mops/s 중앙값, 획득 하나의 최대 대기(ms, 그 회 전체 스레드 중 최댓값).
 */
#define _DARWIN_C_SOURCE
#include <os/lock.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void relax(void) { __asm__ volatile("isb" ::: "memory"); }
typedef struct { _Alignas(128) atomic_uint v; } spin_t;
typedef struct { _Alignas(128) atomic_uint next; atomic_uint serving; } ticket_t;

static void ttas_lock(spin_t *l) {
    unsigned d = 0;
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed)) relax();
        if (!atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) return;
        d = d == 0 ? 1 : (d >= 32 ? 64 : d * 2);
        for (unsigned i = 0; i < d; i++) relax();
    }
}
static void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }
/* yield_after = 0 이면 순수 스핀, N 이면 N 번 돈 뒤마다 sched_yield() (선점된 차례 주인에게 CPU 를 양보) */
static void ticket_lock(ticket_t *t, unsigned yield_after) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed), spins = 0;
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) {
        relax();
        if (yield_after && ++spins >= yield_after) { spins = 0; sched_yield(); }
    }
}
static void ticket_unlock(ticket_t *t) {
    atomic_store_explicit(&t->serving, atomic_load_explicit(&t->serving, memory_order_relaxed) + 1u,
                          memory_order_release);
}

enum { L_TTAS, L_TICKET, L_TICKET_Y, L_MUTEX, L_UNFAIR, L_N };
static const char *const lname[L_N] = { "TTAS+bo(isb)", "ticket", "ticket+yield", "pthread_mutex", "os_unfair_lock" };
#define MAXT 48
static int g_kind;
static spin_t g_spin;
static ticket_t g_tk;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static os_unfair_lock g_ul = OS_UNFAIR_LOCK_INIT;
static _Alignas(128) uint64_t g_cs;
static _Alignas(128) atomic_int g_go, g_stop;
typedef struct { _Alignas(128) uint64_t n, max_wait_ns; } tstat_t;
static tstat_t g_st[MAXT];

static void *worker(void *arg) {
    tstat_t *st = arg;
    uint64_t n = 0, mw = 0, x = (uint64_t)(uintptr_t)arg;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) sched_yield();
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        switch (g_kind) {
        case L_TTAS:     ttas_lock(&g_spin); break;
        case L_TICKET:   ticket_lock(&g_tk, 0); break;
        case L_TICKET_Y: ticket_lock(&g_tk, 64); break;
        case L_MUTEX:    pthread_mutex_lock(&g_mx); break;
        default:         os_unfair_lock_lock(&g_ul); break;
        }
        uint64_t w = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
        if (w > mw) mw = w;
        g_cs += 1;
        switch (g_kind) {
        case L_TICKET: case L_TICKET_Y: ticket_unlock(&g_tk); break;
        case L_MUTEX:  pthread_mutex_unlock(&g_mx); break;
        case L_UNFAIR: os_unfair_lock_unlock(&g_ul); break;
        default:       spin_unlock(&g_spin); break;
        }
        n++;
        for (int k = 0; k < 64; k++) { x = x * 6364136223846793005u + 1442695040888963407u; __asm__ volatile("" : "+r"(x)); }
    }
    st->n = n; st->max_wait_ns = mw;
    return NULL;
}

typedef struct { double mops, max_ms; } res_t;
static res_t run_one(int kind, int nt, int w_ms) {
    g_kind = kind; g_cs = 0;
    memset(g_st, 0, sizeof g_st);
    atomic_store(&g_go, 0); atomic_store(&g_stop, 0);
    pthread_t th[MAXT];
    for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, worker, &g_st[i]);
    struct timespec ts = { 0, 5000000 };
    nanosleep(&ts, NULL);
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    atomic_store_explicit(&g_go, 1, memory_order_release);
    ts.tv_sec = 0; ts.tv_nsec = (long)w_ms * 1000000L;
    nanosleep(&ts, NULL);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    res_t r = { 0, 0 };
    uint64_t sum = 0, mw = 0;
    for (int i = 0; i < nt; i++) { sum += g_st[i].n; if (g_st[i].max_wait_ns > mw) mw = g_st[i].max_wait_ns; }
    if (sum != g_cs) { fprintf(stderr, "LOST UPDATE\n"); exit(2); }
    r.mops = (double)sum / ((double)(t1 - t0) * 1e-9) / 1e6;
    r.max_ms = (double)mw * 1e-6;
    return r;
}
static int cmp(const void *a, const void *b) {
    double x = ((const res_t *)a)->mops, y = ((const res_t *)b)->mops;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int w_ms = argc > 1 ? atoi(argv[1]) : 150, reps = argc > 2 ? atoi(argv[2]) : 3;
    if (w_ms < 10 || w_ms > 900) w_ms = 150;
    if (reps < 1 || reps > 9) reps = 3;
    static const int tl[] = { 6, 12, 24, 48 };
    printf("W=%d ms, reps=%d; cell = Mops/s (max single-acquire wait ms)\n%-15s", w_ms, reps, "lock\\threads");
    for (int i = 0; i < 4; i++) printf("%18d", tl[i]);
    printf("\n");
    for (int k = 0; k < L_N; k++) {
        printf("%-15s", lname[k]);
        for (int i = 0; i < 4; i++) {
            res_t rr[9];
            for (int r = 0; r < reps; r++) rr[r] = run_one(k, tl[i], w_ms);
            qsort(rr, (size_t)reps, sizeof rr[0], cmp);
            printf("%9.2f (%6.2f)", rr[reps / 2].mops, rr[reps / 2].max_ms);
        }
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
