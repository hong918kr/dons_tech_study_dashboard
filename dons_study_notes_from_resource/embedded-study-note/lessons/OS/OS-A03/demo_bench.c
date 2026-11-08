/* OS-A03 demo_bench.c — 읽기 처리량: EBR vs QSBR vs pthread_rwlock_t vs pthread_mutex_t
 *
 * 리더 N 명이 RUN_MS 동안 "설정 테이블에서 계수 4 개 + version 읽기" 를 반복하고, writer 1 명이
 * WRITE_US 마이크로초마다 새 버전을 게시한다. 리더 처리량 합계(Mops/s)와 writer 가 실제로 게시한 횟수를 찍는다.
 *   EBR   : solution.c 의 cfg_read_begin / cfg_read_end, writer = cfg_update (malloc + retire)
 *   QSBR  : solution.c 의 qsbr_quiescent + qsbr_get (한 번 읽을 때마다 quiescent = QSBR 에 가장 불리한 경우)
 *   RWLOCK: 테이블 1 개를 rdlock 으로 읽고, writer 는 wrlock 안에서 제자리 갱신 (cfg_fill)
 *   MUTEX : 같은 것을 뮤텍스로
 *
 * build: cc -std=c11 -O2 -pthread -DNTASK=12 demo_bench.c -o bench && ./bench [WRITE_US] [RUN_MS]
 * 숫자는 이 기계의 다른 작업(다른 벤치마크)과 같이 돈 값이다 → 배수만 믿을 것.
 */
#define main os_a03_tests
#include "solution.c"
#undef main
#include <time.h>

enum { S_EBR, S_QSBR, S_RW, S_MTX, S_N };
static const char *g_name[S_N] = { "EBR", "QSBR", "rwlock", "mutex" };

static int g_scheme;
static atomic_int g_stop;
static cfgdb_t g_bdb;
static qsbr_t g_bq;
static cfg_t g_tbl;                         /* rwlock / mutex 가 지키는 단일 테이블 */
static pthread_rwlock_t g_rw = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
typedef struct { _Alignas(CL) unsigned long ops; int64_t sink; } rstat_t;
static rstat_t g_rs[16];
static unsigned g_write_us = 1000;
static unsigned long g_writes;

static inline int64_t use(const cfg_t *c, unsigned long n) {
    return (int64_t)c->version + c->gain[n & 31] + c->gain[(n + 7) & 31] + c->gain[(n + 13) & 31] + c->gain[(n + 21) & 31];
}
static void *bench_reader(void *arg) {
    int r = (int)(intptr_t)arg;
    unsigned long n = 0;
    int64_t s = 0;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        switch (g_scheme) {
        case S_EBR: { const cfg_t *c = cfg_read_begin(&g_bdb, r); s += use(c, n); cfg_read_end(&g_bdb, r); break; }
        case S_QSBR: { qsbr_quiescent(&g_bq, r); s += use(qsbr_get(&g_bq), n); break; }
        case S_RW: pthread_rwlock_rdlock(&g_rw); s += use(&g_tbl, n); pthread_rwlock_unlock(&g_rw); break;
        default: pthread_mutex_lock(&g_mx); s += use(&g_tbl, n); pthread_mutex_unlock(&g_mx); break;
        }
        n++;
    }
    g_rs[r].ops = n;
    g_rs[r].sink = s;
    return NULL;
}
static void sleep_us(unsigned us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}
static void *bench_writer(void *arg) {
    (void)arg;
    uint64_t v = 2;
    unsigned long w = 0;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        sleep_us(g_write_us);
        int ok;
        switch (g_scheme) {
        case S_EBR: ok = cfg_update(&g_bdb, v) == 0; break;
        case S_QSBR: ok = qsbr_publish(&g_bq, v) == 0; break;
        case S_RW: pthread_rwlock_wrlock(&g_rw); cfg_fill(&g_tbl, v); pthread_rwlock_unlock(&g_rw); ok = 1; break;
        default: pthread_mutex_lock(&g_mx); cfg_fill(&g_tbl, v); pthread_mutex_unlock(&g_mx); ok = 1; break;
        }
        if (ok) { v++; w++; }
    }
    g_writes = w;
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) g_write_us = (unsigned)atoi(argv[1]);
    unsigned run_ms = argc > 2 ? (unsigned)atoi(argv[2]) : 150;
    static const int nthr[] = { 1, 2, 4, 6, 8, 10 };
    printf("writer every %u us, %u ms per cell; reader Mops/s total (writes done)\n", g_write_us, run_ms);
    printf("%-7s", "readers");
    for (int s = 0; s < S_N; s++) printf(" %16s", g_name[s]);
    printf("\n");
    for (size_t k = 0; k < sizeof nthr / sizeof nthr[0]; k++) {
        int n = nthr[k];
        printf("%-7d", n);
        for (int s = 0; s < S_N; s++) {
            g_scheme = s;
            cfgdb_init(&g_bdb, n);
            qsbr_init(&g_bq, n);
            cfg_fill(&g_tbl, 1);
            atomic_store(&g_stop, 0);
            pthread_t th[16], wt;
            for (int r = 0; r < n; r++) pthread_create(&th[r], NULL, bench_reader, (void *)(intptr_t)r);
            pthread_create(&wt, NULL, bench_writer, NULL);
            sleep_us(run_ms * 1000u);
            atomic_store(&g_stop, 1);
            for (int r = 0; r < n; r++) pthread_join(th[r], NULL);
            pthread_join(wt, NULL);
            unsigned long tot = 0;
            for (int r = 0; r < n; r++) tot += g_rs[r].ops;
            char cell[32];
            snprintf(cell, sizeof cell, "%.1f (%lu)", (double)tot / (run_ms * 1000.0), g_writes);
            printf(" %16s", cell);
            fflush(stdout);
            cfg_synchronize(&g_bdb, 100000);
            cfgdb_destroy(&g_bdb);
        }
        printf("\n");
    }
    return 0;
}
