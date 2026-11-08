/* OS-M07 demo_bench.c — 스레드 수별 처리량 + 공정성 (스레드별 획득 횟수 분산).
 *
 * 락: TAS, TTAS(yield), TTAS+지수 백오프(yield), TTAS+지수 백오프(isb), ticket, pthread_mutex, os_unfair_lock
 * 작업: 임계 구역 = 공유 라인 하나에 일반 RMW 2개. 바깥 = 로컬 계산 NCS 번 (스레드끼리 독립).
 * 한 칸 = W ms 동안 모두 돌린 뒤 멈춤 → 총 획득/초, 스레드별 획득 횟수 → CoV(표준편차/평균), min/max, Jain.
 *
 *   LSE 판(기본):  cc -std=c11 -O2 -pthread demo_bench.c -o bench_lse
 *   LL/SC 판:      cc -std=c11 -O2 -pthread -mcpu=apple-m1+nolse demo_bench.c -o bench_llsc
 *   실행:          ./bench_lse [W_ms=80] [reps=3] [NCS=32]
 * 출력 = 칸마다 reps 회 중 처리량 중앙값 (그 회의 CoV·min/max 도 같이). 시간 숫자는 CHECK 하지 않는다.
 */
#define _DARWIN_C_SOURCE
#include <math.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void relax_yield(void) { __asm__ volatile("yield" ::: "memory"); }
static inline void relax_isb(void)   { __asm__ volatile("isb" ::: "memory"); }

typedef struct { _Alignas(128) atomic_uint v; } spin_t;
typedef struct { _Alignas(128) atomic_uint next; atomic_uint serving; } ticket_t;

static unsigned tas_lock(spin_t *l) {
    unsigned f = 0;
    while (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) f++;
    return f;
}
static unsigned ttas_lock(spin_t *l, unsigned bo_max, bool isb) {
    unsigned f = 0, d = 0;
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed)) { if (isb) relax_isb(); else relax_yield(); }
        if (!atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) return f;
        f++;
        if (bo_max) {
            d = d == 0 ? 1 : (d >= bo_max / 2 ? bo_max : d * 2);
            for (unsigned i = 0; i < d; i++) { if (isb) relax_isb(); else relax_yield(); }
        }
    }
}
static void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }
static void ticket_lock(ticket_t *t) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) relax_yield();
}
static void ticket_unlock(ticket_t *t) {
    atomic_store_explicit(&t->serving, atomic_load_explicit(&t->serving, memory_order_relaxed) + 1u,
                          memory_order_release);
}

enum { L_TAS, L_TTAS, L_TTAS_BO, L_TTAS_BO_ISB, L_TICKET, L_MUTEX, L_UNFAIR, L_N };
static const char *const lname[L_N] = { "TAS", "TTAS", "TTAS+bo", "TTAS+bo(isb)", "ticket", "pthread_mutex",
                                        "os_unfair_lock" };

#define MAXT 12
static int g_kind, g_ncs = 32;
static spin_t g_spin;
static ticket_t g_tk;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static os_unfair_lock g_ul = OS_UNFAIR_LOCK_INIT;
static struct { _Alignas(128) uint64_t a, b; } g_data;
static _Alignas(128) atomic_int g_go, g_stop;
typedef struct { _Alignas(128) uint64_t n, fails; } tstat_t;
static tstat_t g_st[MAXT];

static void *worker(void *arg) {
    tstat_t *st = arg;
    uint64_t n = 0, fails = 0, x = (uint64_t)(uintptr_t)arg;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        switch (g_kind) {
        case L_TAS:         fails += tas_lock(&g_spin); break;
        case L_TTAS:        fails += ttas_lock(&g_spin, 0, false); break;
        case L_TTAS_BO:     fails += ttas_lock(&g_spin, 64, false); break;
        case L_TTAS_BO_ISB: fails += ttas_lock(&g_spin, 64, true); break;
        case L_TICKET:      ticket_lock(&g_tk); break;
        case L_MUTEX:       pthread_mutex_lock(&g_mx); break;
        default:            os_unfair_lock_lock(&g_ul); break;
        }
        g_data.a += 1; g_data.b += x;                  /* 임계 구역 */
        switch (g_kind) {
        case L_TICKET: ticket_unlock(&g_tk); break;
        case L_MUTEX:  pthread_mutex_unlock(&g_mx); break;
        case L_UNFAIR: os_unfair_lock_unlock(&g_ul); break;
        default:       spin_unlock(&g_spin); break;
        }
        n++;
        for (int k = 0; k < g_ncs; k++) { x = x * 6364136223846793005u + 1442695040888963407u; __asm__ volatile("" : "+r"(x)); }
    }
    st->n = n; st->fails = fails;
    return NULL;
}

static double now_s(void) { return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW) * 1e-9; }

typedef struct { double mops, cov, jain, fails_per; uint64_t mn, mx; } res_t;
static res_t run_one(int kind, int nt, int w_ms) {
    g_kind = kind;
    memset(g_st, 0, sizeof g_st);
    g_data.a = g_data.b = 0;
    atomic_store(&g_go, 0); atomic_store(&g_stop, 0);
    pthread_t th[MAXT];
    for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, worker, &g_st[i]);
    struct timespec ts = { 0, 2000000 };
    nanosleep(&ts, NULL);
    double t0 = now_s();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    ts.tv_sec = w_ms / 1000; ts.tv_nsec = (long)(w_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    double t1 = now_s();
    res_t r = { 0, 0, 0, 0, UINT64_MAX, 0 };
    double sum = 0, sq = 0, fl = 0;
    for (int i = 0; i < nt; i++) {
        double v = (double)g_st[i].n;
        sum += v; sq += v * v; fl += (double)g_st[i].fails;
        if (g_st[i].n < r.mn) r.mn = g_st[i].n;
        if (g_st[i].n > r.mx) r.mx = g_st[i].n;
    }
    if (g_data.a != (uint64_t)sum) { fprintf(stderr, "LOST UPDATE %s\n", lname[kind]); exit(2); }
    double mean = sum / nt, var = sq / nt - mean * mean;
    r.mops = sum / (t1 - t0) / 1e6;
    r.cov = mean > 0 ? sqrt(var > 0 ? var : 0) / mean : 0;
    r.jain = sq > 0 ? sum * sum / (nt * sq) : 1;
    r.fails_per = sum > 0 ? fl / sum : 0;
    return r;
}
static int cmp_res(const void *a, const void *b) {
    double x = ((const res_t *)a)->mops, y = ((const res_t *)b)->mops;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int w_ms = argc > 1 ? atoi(argv[1]) : 80, reps = argc > 2 ? atoi(argv[2]) : 3;
    if (argc > 3) g_ncs = atoi(argv[3]);
    if (reps < 1 || reps > 9) reps = 3;
    static const int tl[] = { 1, 2, 4, 6, 8, 10, 12 };
    enum { NTL = sizeof tl / sizeof tl[0] };

    /* pause 명령 한 개 비용 (단일 스레드, 1e7 회) */
    double t0 = now_s();
    for (int i = 0; i < 10000000; i++) relax_yield();
    double t1 = now_s();
    for (int i = 0; i < 10000000; i++) relax_isb();
    double t2 = now_s();
    printf("pause cost: yield %.2f ns, isb %.2f ns  (W=%d ms, reps=%d, NCS=%d)\n",
           (t1 - t0) * 100.0, (t2 - t1) * 100.0, w_ms, reps, g_ncs);

    res_t tab[L_N][NTL];
    for (int ti = 0; ti < NTL; ti++)
        for (int k = 0; k < L_N; k++) {
            res_t rr[9];
            for (int r = 0; r < reps; r++) rr[r] = run_one(k, tl[ti], w_ms);
            qsort(rr, (size_t)reps, sizeof rr[0], cmp_res);
            tab[k][ti] = rr[reps / 2];
        }
    printf("\nthroughput Mops/s (median of %d)\n%-15s", reps, "lock\\threads");
    for (int ti = 0; ti < NTL; ti++) printf("%8d", tl[ti]);
    printf("\n");
    for (int k = 0; k < L_N; k++) {
        printf("%-15s", lname[k]);
        for (int ti = 0; ti < NTL; ti++) printf("%8.2f", tab[k][ti].mops);
        printf("\n");
    }
    printf("\nfairness CoV = stddev/mean of per-thread acquires (0 = equal)\n%-15s", "lock\\threads");
    for (int ti = 1; ti < NTL; ti++) printf("%8d", tl[ti]);
    printf("\n");
    for (int k = 0; k < L_N; k++) {
        printf("%-15s", lname[k]);
        for (int ti = 1; ti < NTL; ti++) printf("%8.3f", tab[k][ti].cov);
        printf("\n");
    }
    printf("\nmin/max per-thread acquires and Jain index at 12 threads\n");
    for (int k = 0; k < L_N; k++)
        printf("%-15s min %9llu  max %9llu  max/min %7.1f  jain %.3f\n", lname[k],
               (unsigned long long)tab[k][NTL - 1].mn, (unsigned long long)tab[k][NTL - 1].mx,
               tab[k][NTL - 1].mn ? (double)tab[k][NTL - 1].mx / (double)tab[k][NTL - 1].mn : INFINITY,
               tab[k][NTL - 1].jain);
    printf("\nfailed exchanges per acquire (TAS family)\n%-15s", "lock\\threads");
    for (int ti = 0; ti < NTL; ti++) printf("%8d", tl[ti]);
    printf("\n");
    for (int k = L_TAS; k <= L_TTAS_BO_ISB; k++) {
        printf("%-15s", lname[k]);
        for (int ti = 0; ti < NTL; ti++) printf("%8.2f", tab[k][ti].fails_per);
        printf("\n");
    }
    return 0;
}
