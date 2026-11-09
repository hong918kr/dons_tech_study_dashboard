/* ENV: linux */
/* LPERF-M07 demo_server — 전역 mutex 맵 서버와 그 개선판. 측정 전용 (CHECK 없음)
 *
 *   mode  global   맵 전역 mutex + stats 전역 mutex  ← 문제 상황
 *         rwlock   맵을 pthread_rwlock 으로 (흔한 첫 번째 "개선")
 *         striped  스트라이프 64 개 mutex + stats 전역 mutex
 *         seq      스트라이프 + seqlock 읽기 + stats 전역 mutex
 *         fixed    스트라이프 + seqlock 읽기 + 스레드별 stats
 *
 * 스레드: worker N 개(요청 처리: get 90 % / put 10 %, 요청마다 stats 갱신) + flusher 1 개(2 ms 마다 맵 전체 순회)
 * 시작할 때 LOCK / TID 줄을 찍는다 — bpftrace 가 본 주소·tid 를 이름으로 바꾸는 표다.
 *
 * usage: ./server <mode> <workers> <seconds>
 * build: gcc -std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pthread demo_server.c -o server
 *        (-fno-omit-frame-pointer: bpftrace 의 ustack 은 frame pointer 를 따라간다)
 */
#define LPERF_M07_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"   /* solution.c 의 Part A 함수는 demo_analyze 가 쓴다 */
#include "solution.c"
#include <stddef.h>
#include <sys/syscall.h>
#include <time.h>

#define NKEYS 30000u
#define GCAP_LOG2 16
#define NSTRIPES 64u
#define SCAP_LOG2 11              /* 스트라이프당 2048, 전체 슬롯 131072 — 키 30000 이면 여유 있음 */
#define SCAN_PERIOD_US 2000
#define THINK_NS 300              /* 요청 사이 락 밖 작업 */

typedef enum { M_GLOBAL, M_RWLOCK, M_STRIPED, M_SEQ, M_FIXED } mode_t_;
static mode_t_ g_mode;
static gmap_t g_map;              /* global */
static pthread_rwlock_t g_rw = PTHREAD_RWLOCK_INITIALIZER;   /* rwlock 모드의 맵 락 */
static smap_t g_smap;             /* striped/seq/fixed */
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { uint64_t requests, hits, puts; } g_stats;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_stop;

typedef struct {
    _Alignas(128) int id;
    int tid;
    uint64_t rng;
    uint64_t requests, hits, puts, seq_retries, seq_fallback;
    lat_hist_t hist;
} worker_t;

static inline uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static inline uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }
static inline void busy_ns(uint64_t ns) { uint64_t t0 = now_ns(); while (now_ns() - t0 < ns) { } }

/* 함수를 인라인하지 않아야 ustack 에 이름이 남는다 */
__attribute__((noinline)) static bool map_get(uint64_t k, val_t *o, worker_t *w) {
    switch (g_mode) {
    case M_GLOBAL: return gmap_get(&g_map, k, o);
    case M_RWLOCK: {
        pthread_rwlock_rdlock(&g_rw);
        slot_t *e = table_find(&g_map.t, k, false);
        if (e) slot_read(e, o);
        pthread_rwlock_unlock(&g_rw);
        return e != NULL;
    }
    case M_STRIPED: return smap_get_locked(&g_smap, k, o);
    default: {
        unsigned r = 0;
        bool ok = smap_get_seq(&g_smap, k, o, &r);
        w->seq_retries += r;
        if (r >= SEQ_MAX_RETRY) w->seq_fallback++;
        return ok;
    }
    }
}
__attribute__((noinline)) static void map_put(uint64_t k, const val_t *v) {
    switch (g_mode) {
    case M_GLOBAL: (void)gmap_put(&g_map, k, v); break;
    case M_RWLOCK:
        pthread_rwlock_wrlock(&g_rw);
        { slot_t *e = table_find(&g_map.t, k, true); if (e) slot_write(e, v); }
        pthread_rwlock_unlock(&g_rw);
        break;
    default: (void)smap_put(&g_smap, k, v); break;
    }
}
__attribute__((noinline)) static void stats_add(worker_t *w, bool hit, bool put) {
    if (g_mode == M_FIXED) return;              /* fixed: 스레드 지역 카운터만 (요청 루프에서 이미 셈) */
    pthread_mutex_lock(&g_stats_lock);
    g_stats.requests++;
    g_stats.hits += hit;
    g_stats.puts += put;
    pthread_mutex_unlock(&g_stats_lock);
    (void)w;
}
__attribute__((noinline)) static void audit_log(uint64_t k) {
    char buf[128];
    pthread_mutex_lock(&g_log_lock);
    int n = snprintf(buf, sizeof buf, "req key=%" PRIu64 " t=%" PRIu64 "\n", k, now_ns());
    (void)n;
    pthread_mutex_unlock(&g_log_lock);
}

static void *worker(void *ap) {
    worker_t *w = ap;
    w->tid = (int)syscall(SYS_gettid);
    pthread_setname_np(pthread_self(), "worker");
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        uint64_t r = xs64(&w->rng);
        uint64_t k = 1 + (r >> 20) % NKEYS;
        bool put = (r & 0xff) < 26;             /* ≈10 % put */
        uint64_t t0 = now_ns();
        bool hit = true;
        if (put) { val_t v; for (int i = 0; i < VAL_WORDS; i++) v.w[i] = r + (uint64_t)i; map_put(k, &v); }
        else { val_t o; hit = map_get(k, &o, w); }
        stats_add(w, hit, put);
        if ((r & 0xfff00) == 0) audit_log(k);
        lat_record(&w->hist, now_ns() - t0);
        w->requests++; w->hits += hit; w->puts += put;
        busy_ns(THINK_NS);
    }
    return NULL;
}

static uint64_t g_scans, g_scan_last_ns;
static int g_flusher_tid;
__attribute__((noinline)) static uint64_t flush_scan(void) {
    uint64_t c;
    if (g_mode == M_GLOBAL) return gmap_scan(&g_map, &c);
    if (g_mode == M_RWLOCK) {
        uint64_t sum = 0;
        pthread_rwlock_rdlock(&g_rw);
        for (uint32_t i = 0; i <= g_map.t.mask; i++) if (rd(&g_map.t.slots[i].key)) sum += rd(&g_map.t.slots[i].w[0]);
        pthread_rwlock_unlock(&g_rw);
        return sum;
    }
    return smap_scan(&g_smap, &c);
}
static void *flusher(void *ap) {
    (void)ap;
    g_flusher_tid = (int)syscall(SYS_gettid);
    pthread_setname_np(pthread_self(), "flusher");
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        uint64_t t0 = now_ns();
        (void)flush_scan();
        g_scan_last_ns = now_ns() - t0;
        g_scans++;
        usleep(SCAN_PERIOD_US);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s global|rwlock|striped|seq|fixed workers seconds\n", argv[0]); return 2; }
    const char *ms = argv[1];
    if (!strcmp(ms, "global")) g_mode = M_GLOBAL; else if (!strcmp(ms, "rwlock")) g_mode = M_RWLOCK;
    else if (!strcmp(ms, "striped")) g_mode = M_STRIPED; else if (!strcmp(ms, "seq")) g_mode = M_SEQ;
    else if (!strcmp(ms, "fixed")) g_mode = M_FIXED; else return 2;
    int nw = atoi(argv[2]), secs = atoi(argv[3]);
    if (nw < 1 || nw > 32 || secs < 1 || secs > 30) return 2;

    if (g_mode <= M_RWLOCK) { if (!gmap_init(&g_map, GCAP_LOG2)) return 3; } else if (!smap_init(&g_smap, NSTRIPES, SCAP_LOG2)) return 3;
    for (uint64_t k = 1; k <= NKEYS; k++) {
        val_t v; for (int i = 0; i < VAL_WORDS; i++) v.w[i] = k + (uint64_t)i;
        if (g_mode <= M_RWLOCK) (void)gmap_put(&g_map, k, &v); else (void)smap_put(&g_smap, k, &v);
    }

    printf("pid %d mode %s workers %d secs %d\n", (int)getpid(), ms, nw, secs);
    printf("mutex __owner offset %zu\n", offsetof(pthread_mutex_t, __data.__owner));
    if (g_mode == M_GLOBAL) printf("LOCK map_lock %" PRIu64 " %zu 1\n", (uint64_t)(uintptr_t)&g_map.lock, sizeof(pthread_mutex_t));
    if (g_mode == M_RWLOCK) printf("LOCK map_rwlock %" PRIu64 " %zu 1\n", (uint64_t)(uintptr_t)&g_rw, sizeof(g_rw));
    if (g_mode >= M_STRIPED) printf("LOCK stripe %" PRIu64 " %zu %u\n", (uint64_t)(uintptr_t)g_smap.s, sizeof(stripe_t), NSTRIPES);
    printf("LOCK stats_lock %" PRIu64 " %zu 1\n", (uint64_t)(uintptr_t)&g_stats_lock, sizeof(pthread_mutex_t));
    printf("LOCK log_lock %" PRIu64 " %zu 1\n", (uint64_t)(uintptr_t)&g_log_lock, sizeof(pthread_mutex_t));
    fflush(stdout);

    pthread_t th[32], fl;
    static worker_t W[32];
    uint64_t t0 = now_ns();
    for (int i = 0; i < nw; i++) { W[i].id = i; W[i].rng = 0x9E3779B97F4A7C15ull * (uint64_t)(i + 1); pthread_create(&th[i], NULL, worker, &W[i]); }
    pthread_create(&fl, NULL, flusher, NULL);
    usleep(50000);
    for (int i = 0; i < nw; i++) printf("TID worker %d\n", W[i].tid);
    printf("TID flusher %d\nTID main %d\nREADY\n", g_flusher_tid, (int)syscall(SYS_gettid));
    fflush(stdout);

    sleep((unsigned)secs);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < nw; i++) pthread_join(th[i], NULL);
    pthread_join(fl, NULL);
    uint64_t elapsed = now_ns() - t0;

    static lat_hist_t all;
    uint64_t req = 0, retr = 0, fb = 0;
    for (int i = 0; i < nw; i++) {
        for (unsigned b = 0; b < LAT_BUCKETS; b++) all.n[b] += W[i].hist.n[b];
        all.total += W[i].hist.total;
        req += W[i].requests; retr += W[i].seq_retries; fb += W[i].seq_fallback;
    }
    printf("RESULT mode=%s req=%" PRIu64 " kreq/s=%.0f p50=%" PRIu64 " p90=%" PRIu64 " p99=%" PRIu64 " p99.9=%" PRIu64 " ns",
           ms, req, (double)req * 1e6 / (double)elapsed, lat_percentile(&all, 500), lat_percentile(&all, 900),
           lat_percentile(&all, 990), lat_percentile(&all, 999));
    printf(" scans=%" PRIu64 " last_scan_us=%" PRIu64 " seq_retries=%" PRIu64 " fallback=%" PRIu64 "\n",
           g_scans, g_scan_last_ns / 1000, retr, fb);
    return 0;
}
