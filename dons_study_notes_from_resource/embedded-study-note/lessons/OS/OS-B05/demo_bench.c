/* OS-B05 demo_bench.c — 공유 카운터 처리량: 스레드 1~8, 구성마다 RUNS 번 실행해 중앙값(median)
 *
 *   실험 1  방식 비교 (순수 증가): mutex / atomic fetch_add / sloppy(S=1024) / per-thread 슬롯(Part C)
 *   실험 2  락 범위: 연산마다 "일(work) + 공유 갱신".  wide = 일까지 락 안 / narrow = 갱신만 락 안 /
 *           batch = 로컬에 64 개 모아 한 번에 락.  일의 크기 40 단계(짧음) / 400 단계(김) 두 가지
 *   실험 3  sloppy 임계값 S: 8 스레드에서 S = 1 … 4096, 처리량 vs 최대 오차 T*(S-1)
 *   실험 4  macOS 뮤텍스 정책: default / FAIRSHARE_NP / FIRSTFIT_NP
 *
 * 숫자는 이 머신·이 순간의 측정값이다 (CHECK 하지 않는다). macOS 에는 sched_setaffinity 가 없어서
 * 스레드를 코어에 고정할 수 없다 — 대신 QOS_CLASS_USER_INTERACTIVE 로 P 코어를 선호하게만 한다 (힌트).
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench && ./bench [runs=5]
 */
#define _DARWIN_C_SOURCE                    /* pthread_set_qos_class_self_np, pthread_mutexattr_*policy_np */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#define CACHE_LINE 128
#define MAXT 8
#define MAXRUNS 15
static int g_work = 40;                     /* 실험 2: xorshift 단계 수 = "일" 의 크기 (40 ≈ 50 ns, 400 ≈ 0.5 µs) */
#define BATCH 64

enum { V_MUTEX, V_ATOMIC, V_SLOPPY, V_SLOT, V_WIDE, V_NARROW, V_BATCH };
static const char *vname[] = { "mutex", "atomic", "sloppy", "slot", "wide", "narrow", "batch" };

/* ---- 공유 상태 ---- */
static pthread_mutex_t g_lock;
static long g_value;                        /* g_lock 으로 보호 */
static unsigned long g_sink;                /* 실험 2: 일의 결과도 공유 상태에 반영 (g_lock) */
static atomic_long g_atomic;
typedef struct { _Alignas(CACHE_LINE) pthread_mutex_t lock; long count; } sc_local_t;
static struct { pthread_mutex_t glock; long global; long threshold; sc_local_t local[MAXT]; } g_sc;
typedef struct { _Alignas(CACHE_LINE) atomic_long v; } slot_t;
static slot_t g_slot[MAXT];

static int g_variant, g_nthreads;
static long g_ops;
static atomic_int g_ready, g_go;

static inline uint32_t work(uint32_t x, int n) {
    for (int i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; }
    return x;
}
static void sc_update(int tid, long amt) {  /* solution.c 와 같은 OSTEP sloppy counter */
    sc_local_t *l = &g_sc.local[tid];
    pthread_mutex_lock(&l->lock);
    l->count += amt;
    if (l->count >= g_sc.threshold) {
        pthread_mutex_lock(&g_sc.glock);
        g_sc.global += l->count;
        pthread_mutex_unlock(&g_sc.glock);
        l->count = 0;
    }
    pthread_mutex_unlock(&l->lock);
}

static void *worker(void *arg) {
    int tid = (int)(intptr_t)arg;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    atomic_fetch_add(&g_ready, 1);
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
    long n = g_ops;
    uint32_t x = (uint32_t)tid * 2654435761u + 1u;
    switch (g_variant) {
    case V_MUTEX:
        for (long i = 0; i < n; i++) { pthread_mutex_lock(&g_lock); g_value++; pthread_mutex_unlock(&g_lock); }
        break;
    case V_ATOMIC:
        for (long i = 0; i < n; i++) atomic_fetch_add_explicit(&g_atomic, 1, memory_order_relaxed);
        break;
    case V_SLOPPY:
        for (long i = 0; i < n; i++) sc_update(tid, 1);
        break;
    case V_SLOT: {
        atomic_long *p = &g_slot[tid].v;
        for (long i = 0; i < n; i++)
            atomic_store_explicit(p, atomic_load_explicit(p, memory_order_relaxed) + 1, memory_order_relaxed);
        break;
    }
    case V_WIDE:                            /* 일까지 락 안에서 */
        for (long i = 0; i < n; i++) {
            pthread_mutex_lock(&g_lock);
            x = work(x, g_work);
            g_value++; g_sink += x;
            pthread_mutex_unlock(&g_lock);
        }
        break;
    case V_NARROW:                          /* 일은 락 밖, 공유 갱신만 락 안 */
        for (long i = 0; i < n; i++) {
            x = work(x, g_work);
            pthread_mutex_lock(&g_lock);
            g_value++; g_sink += x;
            pthread_mutex_unlock(&g_lock);
        }
        break;
    case V_BATCH: {                         /* 로컬에 모았다가 BATCH 개마다 한 번 */
        long cnt = 0; unsigned long sink = 0;
        for (long i = 0; i < n; i++) {
            x = work(x, g_work);
            cnt++; sink += x;
            if (cnt == BATCH || i == n - 1) {
                pthread_mutex_lock(&g_lock);
                g_value += cnt; g_sink += sink;
                pthread_mutex_unlock(&g_lock);
                cnt = 0; sink = 0;
            }
        }
        break;
    }
    }
    return NULL;
}

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static double cpu_s(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
}
static double sys_s(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
}

typedef struct { double mops, cpu_pct, sys_pct; } res_t;

/* 한 번 실행: 스레드를 다 만든 뒤 동시에 출발시키고, 전부 join 할 때까지의 벽시계 시간 */
static res_t run_once(int variant, int nt, long ops, long sloppy_s, int mutex_policy) {
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    if (mutex_policy) pthread_mutexattr_setpolicy_np(&ma, mutex_policy);
    pthread_mutex_init(&g_lock, &ma);
    pthread_mutexattr_destroy(&ma);
    g_value = 0; g_sink = 0;
    atomic_store(&g_atomic, 0);
    pthread_mutex_init(&g_sc.glock, NULL);
    g_sc.global = 0; g_sc.threshold = sloppy_s;
    for (int i = 0; i < MAXT; i++) {
        pthread_mutex_init(&g_sc.local[i].lock, NULL);
        g_sc.local[i].count = 0;
        atomic_store(&g_slot[i].v, 0);
    }
    g_variant = variant; g_nthreads = nt; g_ops = ops;
    atomic_store(&g_ready, 0);
    atomic_store(&g_go, 0);
    pthread_t th[MAXT];
    for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
    while (atomic_load(&g_ready) < nt) { }
    double c0 = cpu_s(), s0 = sys_s(), t0 = now_s();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    double t1 = now_s(), c1 = cpu_s(), s1 = sys_s();

    long total = 0;                          /* 정확성 확인: 하나라도 틀리면 측정 자체가 무의미 */
    switch (variant) {
    case V_ATOMIC: total = atomic_load(&g_atomic); break;
    case V_SLOPPY: total = g_sc.global; for (int i = 0; i < nt; i++) total += g_sc.local[i].count; break;
    case V_SLOT:   for (int i = 0; i < nt; i++) total += atomic_load(&g_slot[i].v); break;
    default:       total = g_value; break;
    }
    if (total != (long)nt * ops) { fprintf(stderr, "WRONG COUNT %s T=%d: %ld\n", vname[variant], nt, total); exit(1); }
    pthread_mutex_destroy(&g_lock);
    pthread_mutex_destroy(&g_sc.glock);
    for (int i = 0; i < MAXT; i++) pthread_mutex_destroy(&g_sc.local[i].lock);
    double wall = t1 - t0;
    return (res_t){ (double)nt * ops / wall / 1e6, (c1 - c0) / wall * 100.0, (s1 - s0) / wall * 100.0 };
}

static int g_runs = 5;
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
/* RUNS 번 실행 → 처리량 중앙값·최소·최대, CPU% 중앙값 */
static res_t measure(int variant, int nt, long ops, long s, int pol, double *mn, double *mx) {
    double m[MAXRUNS], c[MAXRUNS], y[MAXRUNS];
    run_once(variant, nt, ops / 4 + 1, s, pol);                 /* 워밍업 1 회 (버림) */
    for (int r = 0; r < g_runs; r++) { res_t x = run_once(variant, nt, ops, s, pol); m[r] = x.mops; c[r] = x.cpu_pct; y[r] = x.sys_pct; }
    qsort(m, (size_t)g_runs, sizeof m[0], cmpd);
    qsort(c, (size_t)g_runs, sizeof c[0], cmpd);
    qsort(y, (size_t)g_runs, sizeof y[0], cmpd);
    *mn = m[0]; *mx = m[g_runs - 1];
    return (res_t){ m[g_runs / 2], c[g_runs / 2], y[g_runs / 2] };
}

int main(int argc, char **argv) {
    if (argc > 1) g_runs = atoi(argv[1]);
    if (g_runs < 1 || g_runs > MAXRUNS) g_runs = 5;
    pthread_mutexattr_t ma;
    int pol = -1;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_getpolicy_np(&ma, &pol);
    printf("runs per config = %d (median reported), default mutex policy = %d (1=FAIRSHARE, 3=FIRSTFIT)\n", g_runs, pol);
    printf("sizeof(pthread_mutex_t)=%zu, sizeof(sc_local_t)=%zu, sizeof(slot_t)=%zu\n",
           sizeof(pthread_mutex_t), sizeof(sc_local_t), sizeof(slot_t));
    double mn, mx;

    printf("\n# exp1: pure increments, Mops/s median [min-max]  cpu%% sys%%\n");
    const int v1[] = { V_MUTEX, V_ATOMIC, V_SLOPPY, V_SLOT };
    const long ops1[] = { 500000, 2000000, 2000000, 20000000 };
    for (int k = 0; k < 4; k++)
        for (int t = 1; t <= MAXT; t++) {
            res_t r = measure(v1[k], t, ops1[k], 1024, 0, &mn, &mx);
            printf("CSV,exp1,%s,%d,%.1f,%.1f,%.1f,%.0f,%.0f\n", vname[v1[k]], t, r.mops, mn, mx, r.cpu_pct, r.sys_pct);
        }

    const int v2[] = { V_WIDE, V_NARROW, V_BATCH };
    const int works[] = { 40, 400 };
    for (int w = 0; w < 2; w++) {
        g_work = works[w];
        printf("\n# exp2: work(%d xorshift steps) + shared update, Mops/s\n", g_work);
        for (int k = 0; k < 3; k++)
            for (int t = 1; t <= MAXT; t++) {
                res_t r = measure(v2[k], t, g_work == 40 ? 300000 : 50000, 1024, 0, &mn, &mx);
                printf("CSV,exp2w%d,%s,%d,%.1f,%.1f,%.1f,%.0f,%.0f\n", g_work, vname[v2[k]], t, r.mops, mn, mx, r.cpu_pct, r.sys_pct);
            }
    }

    printf("\n# exp3: sloppy threshold S at T=8, Mops/s and max error bound T*(S-1)\n");
    const long S[] = { 1, 2, 4, 16, 64, 256, 1024, 4096 };
    for (int k = 0; k < 8; k++) {
        res_t r = measure(V_SLOPPY, 8, S[k] < 16 ? 500000 : 2000000, S[k], 0, &mn, &mx);
        printf("CSV,exp3,S=%ld,8,%.1f,%.1f,%.1f,%.0f,%.0f,bound=%ld\n", S[k], r.mops, mn, mx, r.cpu_pct, r.sys_pct, 8 * (S[k] - 1));
    }

    printf("\n# exp4: mutex policy, pure increments, Mops/s\n");
    const int pols[] = { 0, PTHREAD_MUTEX_POLICY_FAIRSHARE_NP, PTHREAD_MUTEX_POLICY_FIRSTFIT_NP };
    const char *pname[] = { "default", "fairshare", "firstfit" };
    const int ts[] = { 1, 2, 4, 8 };
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < 4; j++) {
            res_t r = measure(V_MUTEX, ts[j], 300000, 1024, pols[k], &mn, &mx);
            printf("CSV,exp4,%s,%d,%.1f,%.1f,%.1f,%.0f,%.0f\n", pname[k], ts[j], r.mops, mn, mx, r.cpu_pct, r.sys_pct);
        }
    return 0;
}
