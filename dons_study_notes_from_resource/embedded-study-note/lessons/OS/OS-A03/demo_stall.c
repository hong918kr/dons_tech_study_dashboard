/* OS-A03 demo_stall.c — 멈춘 리더 한 명이 붙잡는 메모리 (EBR 의 약점) / QSBR 풀이 받는 역압
 *
 * 리더 4 명 (0 번은 t=50..250 ms 동안 읽기 구간 안에서 멈춤), writer 는 100 us 마다 새 버전 게시, 총 400 ms.
 * 25 ms 마다: 해제 대기 중인 버전 수(limbo), 붙잡힌 메모리, 막고 있는 리더, 거절된 갱신 수.
 *
 *   EBR, 상한 사실상 없음:  cc -std=c11 -O2 -pthread -DCFG_N=1024 -DLIMBO_CAP=100000 demo_stall.c -o stall && ./stall
 *   EBR, 상한 256:          cc -std=c11 -O2 -pthread -DCFG_N=1024 -DLIMBO_CAP=256 demo_stall.c -o stall256 && ./stall256
 *   QSBR 정적 풀 3 개:      cc -std=c11 -O2 -pthread -DCFG_N=1024 -DQS demo_stall.c -o stallq && ./stallq
 */
#define main os_a03_tests
#include "solution.c"
#undef main
#include <time.h>

#define NR 4
#ifdef QS
static qsbr_t g_sq;
#else
static cfgdb_t g_sdb;
#endif
static atomic_int g_stop;
static _Atomic uint64_t g_t0;

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static uint64_t elapsed_ms(void) { return (now_us() - atomic_load(&g_t0)) / 1000u; }
static void sleep_us(unsigned us) {
    struct timespec ts = { 0, (long)us * 1000L };
    nanosleep(&ts, NULL);
}

static void *rd(void *arg) {
    int r = (int)(intptr_t)arg;
    int64_t s = 0;
    bool stalled_once = false;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
#ifdef QS
        qsbr_quiescent(&g_sq, r);
        const cfg_t *c = qsbr_get(&g_sq);
#else
        const cfg_t *c = cfg_read_begin(&g_sdb, r);
#endif
        s += c->gain[0];
        if (r == 0 && !stalled_once && elapsed_ms() >= 50) {   /* 0 번: 읽기 구간(루프 한 바퀴) 안에서 200 ms 멈춤 */
            stalled_once = true;
            while (elapsed_ms() < 250) sleep_us(1000);
            s += c->gain[1];                                    /* 멈춘 동안 쥐고 있던 버전을 아직 쓴다 */
        }
#ifndef QS
        cfg_read_end(&g_sdb, r);
#endif
    }
    return (void *)(intptr_t)(s & 1);
}

int main(void) {
#ifdef QS
    qsbr_init(&g_sq, NR);
    printf("QSBR static pool (%d buffers x %zu B), reader 0 stalls 50..250 ms, publish every 100 us\n",
           NPOOL, sizeof(cfg_t));
#else
    cfgdb_init(&g_sdb, NR);
    printf("EBR, LIMBO_CAP=%d, cfg_t=%zu B, reader 0 stalls 50..250 ms, update every 100 us\n",
           LIMBO_CAP, sizeof(cfg_t));
#endif
    printf("%6s %8s %8s %10s %8s %8s\n", "t(ms)", "ok", "refused", "held(KB)", "blocker", "epoch");
    atomic_store(&g_t0, now_us());
    pthread_t th[NR];
    for (int r = 0; r < NR; r++) pthread_create(&th[r], NULL, rd, (void *)(intptr_t)r);
    uint64_t v = 2;
    unsigned long ok = 0, refused = 0;
    uint64_t next_print = 25;
    while (elapsed_ms() < 400) {
        sleep_us(100);
#ifdef QS
        if (qsbr_publish(&g_sq, v) == 0) { ok++; v++; } else refused++;
#else
        if (cfg_update(&g_sdb, v) == 0) { ok++; v++; } else refused++;
#endif
        if (elapsed_ms() >= next_print) {
#ifdef QS
            int held = 0;
            for (int i = 0; i < NPOOL; i++) held += g_sq.state[i] != BUF_FREE;
            printf("%6llu %8lu %8lu %10.1f %8s %8llu\n", (unsigned long long)next_print, ok, refused,
                   held * sizeof(cfg_t) / 1024.0, "-", (unsigned long long)atomic_load(&g_sq.gp));
#else
            pthread_mutex_lock(&g_sdb.wlock);
            int nl = g_sdb.ebr.nlimbo, b = ebr_blocker(&g_sdb.ebr);
            uint64_t e = atomic_load(&g_sdb.ebr.epoch);
            pthread_mutex_unlock(&g_sdb.wlock);
            printf("%6llu %8lu %8lu %10.1f %8d %8llu\n", (unsigned long long)next_print, ok, refused,
                   (nl + 1) * sizeof(cfg_t) / 1024.0, b, (unsigned long long)e);
#endif
            next_print += 25;
        }
    }
    atomic_store(&g_stop, 1);
    for (int r = 0; r < NR; r++) pthread_join(th[r], NULL);
#ifndef QS
    cfg_synchronize(&g_sdb, 100000);
    printf("after join + synchronize: limbo=%d, live versions=%ld\n", g_sdb.ebr.nlimbo, atomic_load(&g_live));
    cfgdb_destroy(&g_sdb);
#endif
    return 0;
}
