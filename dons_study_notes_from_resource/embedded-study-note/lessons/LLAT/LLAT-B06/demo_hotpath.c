/* LLAT-B06 demo_hotpath.c — hot path 에서 원자 연산을 줄이는 기법의 효과를 잰다 (8 스레드 기본)
 *
 *   H1 카운터: 이벤트마다 공유 fetch_add (relaxed / seq_cst) vs 배치 N=16·256 vs 스레드별 칸 + 합산
 *   H2 플래그: CAS 루프로 OR vs fetch_or(ldset) vs "먼저 읽고 없을 때만 fetch_or"
 *   H3 최대값 워터마크: 매번 CAS vs "먼저 읽고 클 때만 CAS" (read-before-CAS = TTAS 와 같은 생각)
 *
 * 모든 변형이 이벤트마다 같은 "일" (xorshift 한 번 = 값 생성) 을 한다. 결과 합계/OR/최대값은 매 실행
 * 정답과 비교한다 (틀리면 exit 2). 처리량은 CHECK 하지 않는다 — 이 머신·이 순간의 측정값.
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_hotpath.c -o hot && ./hot [runs=5] [threads=8]
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LINE 128
#define MAXT 12
#define MAXRUNS 15
#define EVENTS 2000000u                     /* 스레드당 이벤트 수 */

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

enum { H1_SHARED_RLX, H1_SHARED_SC, H1_BATCH16, H1_BATCH256, H1_SLOT,
       H2_CASOR, H2_FETCHOR, H2_READFIRST,
       H3_CAS_ALWAYS, H3_READFIRST, NV };
static const char *vname[NV] = {
    "H1 shared fetch_add relaxed", "H1 shared fetch_add seq_cst", "H1 batch N=16 (relaxed)",
    "H1 batch N=256 (relaxed)", "H1 per-thread slot + sum",
    "H2 CAS loop OR", "H2 fetch_or (ldset)", "H2 load, fetch_or if missing",
    "H3 CAS every event", "H3 load, CAS only if bigger" };

_Alignas(LINE) _Atomic uint64_t g_count;
_Alignas(LINE) _Atomic uint32_t g_flags;
_Alignas(LINE) _Atomic uint32_t g_max;
static struct { _Alignas(LINE) _Atomic uint64_t v; } g_slot[MAXT];
_Alignas(LINE) atomic_int g_go;

typedef struct { int kind, tid; uint32_t seed; uint32_t local_max; } targ_t;

static inline uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void *body(void *vp) {
    targ_t *a = vp;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint32_t s = a->seed, lmax = 0;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
    switch (a->kind) {
    case H1_SHARED_RLX:
        for (uint32_t i = 0; i < EVENTS; i++) { (void)xs(&s); atomic_fetch_add_explicit(&g_count, 1, memory_order_relaxed); }
        break;
    case H1_SHARED_SC:
        for (uint32_t i = 0; i < EVENTS; i++) { (void)xs(&s); atomic_fetch_add(&g_count, 1); }
        break;
    case H1_BATCH16: case H1_BATCH256: {
        const uint32_t N = a->kind == H1_BATCH16 ? 16 : 256;
        uint64_t local = 0;
        for (uint32_t i = 0; i < EVENTS; i++) {
            (void)xs(&s);
            if (++local == N) { atomic_fetch_add_explicit(&g_count, local, memory_order_relaxed); local = 0; }
        }
        if (local) atomic_fetch_add_explicit(&g_count, local, memory_order_relaxed);   /* 마지막 flush */
        break; }
    case H1_SLOT: {
        _Atomic uint64_t *p = &g_slot[a->tid].v;
        for (uint32_t i = 0; i < EVENTS; i++) {
            (void)xs(&s);
            atomic_store_explicit(p, atomic_load_explicit(p, memory_order_relaxed) + 1, memory_order_relaxed);
        }
        break; }
    case H2_CASOR:
        for (uint32_t i = 0; i < EVENTS; i++) {
            uint32_t bit = 1u << (xs(&s) & 7);
            uint32_t old = atomic_load_explicit(&g_flags, memory_order_relaxed);
            while (!atomic_compare_exchange_weak_explicit(&g_flags, &old, old | bit,
                                                          memory_order_relaxed, memory_order_relaxed)) { }
        }
        break;
    case H2_FETCHOR:
        for (uint32_t i = 0; i < EVENTS; i++) {
            uint32_t bit = 1u << (xs(&s) & 7);
            atomic_fetch_or_explicit(&g_flags, bit, memory_order_relaxed);
        }
        break;
    case H2_READFIRST:
        for (uint32_t i = 0; i < EVENTS; i++) {
            uint32_t bit = 1u << (xs(&s) & 7);
            if ((atomic_load_explicit(&g_flags, memory_order_relaxed) & bit) != bit)
                atomic_fetch_or_explicit(&g_flags, bit, memory_order_relaxed);
        }
        break;
    case H3_CAS_ALWAYS:
        for (uint32_t i = 0; i < EVENTS; i++) {
            uint32_t v = xs(&s) >> 12; if (v > lmax) lmax = v;
            uint32_t cur = atomic_load_explicit(&g_max, memory_order_relaxed);
            while (!atomic_compare_exchange_weak_explicit(&g_max, &cur, cur > v ? cur : v,
                                                          memory_order_relaxed, memory_order_relaxed)) { }
        }
        break;
    case H3_READFIRST:
        for (uint32_t i = 0; i < EVENTS; i++) {
            uint32_t v = xs(&s) >> 12; if (v > lmax) lmax = v;
            uint32_t cur = atomic_load_explicit(&g_max, memory_order_relaxed);
            while (v > cur && !atomic_compare_exchange_weak_explicit(&g_max, &cur, v,
                                                                     memory_order_relaxed, memory_order_relaxed)) { }
        }
        break;
    }
    a->local_max = lmax;
    return NULL;
}

static double run(int kind, int T) {
    pthread_t th[MAXT]; targ_t a[MAXT];
    atomic_store(&g_count, 0); atomic_store(&g_flags, 0); atomic_store(&g_max, 0); atomic_store(&g_go, 0);
    for (int t = 0; t < MAXT; t++) atomic_store(&g_slot[t].v, 0);
    for (int t = 0; t < T; t++) {
        a[t] = (targ_t){ kind, t, 0x9E3779B9u * (uint32_t)(t + 1), 0 };
        pthread_create(&th[t], NULL, body, &a[t]);
    }
    uint64_t t0 = now_ns();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int t = 0; t < T; t++) pthread_join(th[t], NULL);
    uint64_t t1 = now_ns();
    /* 결과 검증 (정확해야 측정할 의미가 있다) */
    int ok = 1;
    if (kind <= H1_SHARED_SC || kind == H1_BATCH16 || kind == H1_BATCH256) ok = atomic_load(&g_count) == (uint64_t)T * EVENTS;
    else if (kind == H1_SLOT) { uint64_t sum = 0; for (int t = 0; t < T; t++) sum += atomic_load(&g_slot[t].v); ok = sum == (uint64_t)T * EVENTS; }
    else if (kind <= H2_READFIRST) ok = atomic_load(&g_flags) == 0xFFu;
    else { uint32_t m = 0; for (int t = 0; t < T; t++) if (a[t].local_max > m) m = a[t].local_max; ok = atomic_load(&g_max) == m; }
    if (!ok) { fprintf(stderr, "WRONG RESULT %s\n", vname[kind]); exit(2); }
    return (double)T * EVENTS / (double)(t1 - t0) * 1e3;      /* Mevents/s */
}

static int cmp_d(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 5, T = argc > 2 ? atoi(argv[2]) : 8;
    if (runs < 1 || runs > MAXRUNS) runs = 5;
    if (T < 1 || T > MAXT) T = 8;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    double la[3];
    if (getloadavg(la, 3) == 3) printf("load average at start: %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    printf("threads=%d, %u events/thread, warm-up 1 + %d runs, Mevents/s median [min-max]\n", T, EVENTS, runs);
    const int base_of[NV] = { 0, 0, 0, 0, 0, 5, 5, 5, 8, 8 };
    double med[NV];
    for (int k = 0; k < NV; k++) {
        double r[MAXRUNS];
        (void)run(k, T);
        for (int i = 0; i < runs; i++) r[i] = run(k, T);
        qsort(r, (size_t)runs, sizeof r[0], cmp_d);
        med[k] = r[runs / 2];
        printf("%-34s %9.1f [%.1f-%.1f]  x%.2f\n", vname[k], med[k], r[0], r[runs - 1], med[k] / med[base_of[k]]);
    }
    if (getloadavg(la, 3) == 3) printf("load average at end:   %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    return 0;
}
