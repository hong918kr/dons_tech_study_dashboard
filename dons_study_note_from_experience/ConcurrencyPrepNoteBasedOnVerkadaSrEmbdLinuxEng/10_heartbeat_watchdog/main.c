/* main.c — test harness + fake hardware watchdog (GIVEN; do not modify).
 *
 * It is used unchanged by both watchdog.c (your stub) and watchdog_solution.c.
 *
 * How it judges the implementation
 *   - the fake hwwdt records EVERY hwwdt_kick() and hwwdt_report_fault() with a
 *     timestamp, so the tests can ask "was the dog kicked between t0 and t1?"
 *     and "how many times was worker 3 reported?" instead of trusting a counter
 *     inside the implementation.
 *   - ground truth for Part 2 is recomputed by brute force from the harness's
 *     own record of when it last called wd_heartbeat() for each worker.
 *     Nothing is hard-coded, so macOS and Linux give the same verdict.
 *   - randomness is a fixed-seed xorshift (never rand(): its sequence differs
 *     per platform).
 *
 * Timing: the real hardware watchdog window is 2 s. hwwdt.h scales it 20x down
 * to 100 ms and every deadline in this file is scaled to match, so the whole run
 * takes about 4 s instead of 80.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "hwwdt.h"
#include "watchdog.h"

/* ThreadSanitizer inflates both an atomic store and a mutex round trip, so the
 * cost ratio asserted below is relaxed (but still checked) under -fsanitize=thread. */
#if defined(__SANITIZE_THREAD__)
#  define WD_TSAN 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define WD_TSAN 1
#  endif
#endif
#ifndef WD_TSAN
#  define WD_TSAN 0
#endif

/* ================================================================== */
/* fake vendor watchdog                                                */
/* ================================================================== */

uint64_t hwwdt_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

#define LOG_MAX 32768
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_kick_ts[LOG_MAX];
static int      g_kick_n;
static struct { uint64_t ts; uint32_t id; } g_flt[LOG_MAX];
static int      g_flt_n;
static int      g_kick_lost, g_flt_lost;

void hwwdt_kick(void)
{
    uint64_t t = hwwdt_now_us();
    pthread_mutex_lock(&g_log_lock);
    if (g_kick_n < LOG_MAX) g_kick_ts[g_kick_n++] = t;
    else g_kick_lost++;
    pthread_mutex_unlock(&g_log_lock);
}

void hwwdt_report_fault(uint32_t worker_id)
{
    uint64_t t = hwwdt_now_us();
    pthread_mutex_lock(&g_log_lock);
    if (g_flt_n < LOG_MAX) { g_flt[g_flt_n].ts = t; g_flt[g_flt_n].id = worker_id; g_flt_n++; }
    else g_flt_lost++;
    pthread_mutex_unlock(&g_log_lock);
}

/* ---- log queries (harness side) ---------------------------------- */

static int kicks_between(uint64_t a, uint64_t b)
{
    int n = 0;
    pthread_mutex_lock(&g_log_lock);
    for (int i = 0; i < g_kick_n; i++)
        if (g_kick_ts[i] >= a && g_kick_ts[i] <= b) n++;
    pthread_mutex_unlock(&g_log_lock);
    return n;
}

/* Worst interval in [a,b] during which the dog was not kicked — including the
 * stretch before the first kick and after the last one. */
static uint64_t max_kick_gap(uint64_t a, uint64_t b)
{
    uint64_t prev = a, worst = 0;
    pthread_mutex_lock(&g_log_lock);
    for (int i = 0; i < g_kick_n; i++) {
        uint64_t t = g_kick_ts[i];
        if (t < a || t > b) continue;
        if (t - prev > worst) worst = t - prev;
        prev = t;
    }
    pthread_mutex_unlock(&g_log_lock);
    if (b > prev && b - prev > worst) worst = b - prev;
    return worst;
}

static int faults_for(int id, uint64_t a, uint64_t b)
{
    int n = 0;
    pthread_mutex_lock(&g_log_lock);
    for (int i = 0; i < g_flt_n; i++)
        if (g_flt[i].ts >= a && g_flt[i].ts <= b &&
            (id < 0 || g_flt[i].id == (uint32_t)id)) n++;
    pthread_mutex_unlock(&g_log_lock);
    return n;
}

static uint64_t first_fault_ts(int id, uint64_t from)
{
    uint64_t r = 0;
    pthread_mutex_lock(&g_log_lock);
    for (int i = 0; i < g_flt_n; i++)
        if (g_flt[i].ts >= from && g_flt[i].id == (uint32_t)id) { r = g_flt[i].ts; break; }
    pthread_mutex_unlock(&g_log_lock);
    return r;
}

/* ================================================================== */
/* harness bookkeeping                                                 */
/* ================================================================== */

static int fails;

static void check(const char *msg, bool ok)
{
    if (!ok) fails++;
    printf("  %-64s %s\n", msg, ok ? "PASS" : "FAIL");
}

/* fixed-seed xorshift: identical sequence on macOS and Linux */
static uint32_t g_rng = 0x9e3779b9u;
static uint32_t xs32(void)
{
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rng = x;
    return x;
}

static uint64_t absdiff(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

/* Harness copy of "when did we last tell the watchdog this worker is alive".
 * The timestamp is taken BEFORE calling wd_heartbeat(), so it is never newer
 * than what the implementation stored -> the brute-force minimum is never later
 * than the true one, and the only error is the few ns between the two clock
 * reads (covered by ND_TOL_US). */
static _Atomic uint64_t g_hb_seen[WD_MAX_WORKERS];

static void hb(int id)
{
    uint64_t t = hwwdt_now_us();
    wd_heartbeat((uint32_t)id);
    /* guard: with the unfinished stub wd_register() returns -1, and the tests
     * below still call hb(-1). Writing g_hb_seen[-1] would corrupt the harness. */
    if (id >= 0 && id < WD_MAX_WORKERS)
        atomic_store_explicit(&g_hb_seen[id], t, memory_order_relaxed);
}

/* the set of workers the harness believes are registered (main thread only) */
typedef struct { int id; uint64_t deadline; } reg_t;
static reg_t g_reg[WD_MAX_WORKERS + 4];
static int   g_reg_n;

static int reg_add(const char *name, uint64_t deadline_us)
{
    uint64_t t = hwwdt_now_us();
    int id = wd_register(name, deadline_us);
    if (id >= 0 && id < WD_MAX_WORKERS) {
        atomic_store_explicit(&g_hb_seen[id], t, memory_order_relaxed);
        g_reg[g_reg_n].id = id;
        g_reg[g_reg_n].deadline = deadline_us;
        g_reg_n++;
    }
    return id;
}

static void reg_del(int id)
{
    wd_unregister(id);
    for (int i = 0; i < g_reg_n; i++)
        if (g_reg[i].id == id) { g_reg[i] = g_reg[--g_reg_n]; break; }
}

static void reg_del_all(void) { while (g_reg_n > 0) reg_del(g_reg[0].id); }

/* ground truth for Part 2 */
static uint64_t brute_min(void)
{
    uint64_t m = UINT64_MAX;
    for (int i = 0; i < g_reg_n; i++) {
        uint64_t d = atomic_load_explicit(&g_hb_seen[g_reg[i].id], memory_order_relaxed)
                     + g_reg[i].deadline;
        if (d < m) m = d;
    }
    return m;
}
static int brute_min_holder(void)
{
    uint64_t m = UINT64_MAX;
    int who = -1;
    for (int i = 0; i < g_reg_n; i++) {
        uint64_t d = atomic_load_explicit(&g_hb_seen[g_reg[i].id], memory_order_relaxed)
                     + g_reg[i].deadline;
        if (d < m) { m = d; who = g_reg[i].id; }
    }
    return who;
}

#define ND_TOL_US 3000ull      /* next-deadline comparison tolerance */

/* ================================================================== */
/* heartbeat cost benchmark                                            */
/* ================================================================== */

#define BENCH_THREADS 8
#define BENCH_ITERS   40000

/* Deliberately packed into one cache line and guarded by one mutex: this is the
 * "obvious" implementation the wait-free heartbeat has to beat. */
static pthread_mutex_t g_naive_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t        g_naive_slot[BENCH_THREADS];
static int             g_bench_id[BENCH_THREADS];

static void *bench_wait_free(void *a)
{
    int id = *(int *)a;
    for (int i = 0; i < BENCH_ITERS; i++) wd_heartbeat((uint32_t)id);
    return NULL;
}

static void *bench_shared_mutex(void *a)
{
    int k = (int)((const int *)a - g_bench_id);   /* its own slot */
    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t = hwwdt_now_us();
        pthread_mutex_lock(&g_naive_lock);
        g_naive_slot[k] = t;
        pthread_mutex_unlock(&g_naive_lock);
    }
    return NULL;
}

/* per-call nanoseconds; thread create/join overhead is identical for both
 * variants and is ~0.1% of the run, so the ratio is meaningful */
static double bench_run(void *(*fn)(void *), const int *ids)
{
    pthread_t th[BENCH_THREADS];
    uint64_t t0 = hwwdt_now_us();
    for (int i = 0; i < BENCH_THREADS; i++)
        pthread_create(&th[i], NULL, fn, (void *)&ids[i]);
    for (int i = 0; i < BENCH_THREADS; i++) pthread_join(th[i], NULL);
    uint64_t t1 = hwwdt_now_us();
    return (double)(t1 - t0) * 1000.0 / ((double)BENCH_THREADS * BENCH_ITERS);
}

/* ================================================================== */
/* 8-worker stress                                                     */
/* ================================================================== */

#define NSTRESS 8

typedef struct { int id; uint64_t period_us; } wk_t;
static wk_t g_wk[NSTRESS];
static _Atomic bool     g_wk_run;
static _Atomic bool     g_wk_quiet;
static _Atomic uint64_t g_beats;

static void *wk_main(void *a)
{
    wk_t *w = a;
    while (atomic_load_explicit(&g_wk_run, memory_order_relaxed)) {
        /* g_wk_quiet freezes the beats for a few ms so that the brute-force
         * minimum stands still while we compare it with wd_next_deadline(). */
        if (!atomic_load_explicit(&g_wk_quiet, memory_order_relaxed)) {
            hb(w->id);
            atomic_fetch_add_explicit(&g_beats, 1, memory_order_relaxed);
        }
        usleep((useconds_t)w->period_us);
    }
    return NULL;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(void)
{
    uint32_t ids[WD_MAX_WORKERS];
    uint64_t when = 0;

    printf("hardware watchdog window = %llu us (real part: 2 s, scaled 20x)\n",
           (unsigned long long)HWWDT_TIMEOUT_US);
    if (wd_init() != 0) { printf("wd_init() failed\n"); return 1; }
    uint64_t t_boot = hwwdt_now_us();

    /* -------------------------------------------------------------- */
    printf("\n== Empty state ==\n");
    check("wd_next_deadline() with nothing registered returns -1",
          wd_next_deadline(&when) == -1);
    check("wd_overdue() with nothing registered returns 0",
          wd_overdue(ids, WD_MAX_WORKERS) == 0);
    usleep(150 * 1000);
    {
        int k = kicks_between(t_boot, hwwdt_now_us());
        printf("    kicks in the first 150 ms: %d\n", k);
        check("hardware IS kicked when no worker is registered (vacuously healthy)", k >= 2);
        check("no fault reported when no worker is registered",
              faults_for(-1, t_boot, hwwdt_now_us()) == 0);
    }

    /* -------------------------------------------------------------- */
    printf("\n== Arguments and table limits ==\n");
    check("wd_register(name, deadline_us = 0) returns -1", wd_register("bad", 0) == -1);
    wd_unregister(-1);
    wd_unregister(WD_MAX_WORKERS);
    wd_unregister(9999);
    wd_heartbeat(WD_MAX_WORKERS);
    wd_heartbeat(0xFFFFFFFFu);
    check("out-of-range ids are ignored without crashing", true);
    {
        int filled = 0;
        for (int i = 0; i < WD_MAX_WORKERS; i++) {
            int id = reg_add("filler", 10ull * 1000000ull);   /* 10 s: cannot expire */
            if (id >= 0) filled++;
        }
        char m[96];
        snprintf(m, sizeof m, "registered WD_MAX_WORKERS (%d) workers, got %d",
                 WD_MAX_WORKERS, filled);
        check(m, filled == WD_MAX_WORKERS);
        check("one registration past the table returns -1",
              wd_register("overflow", 1000000ull) == -1);
        reg_del_all();
        check("after unregistering everything wd_next_deadline() returns -1",
              wd_next_deadline(&when) == -1);
    }

    /* -------------------------------------------------------------- */
    printf("\n== Part 1: is the heartbeat really cheaper than a lock? ==\n");
    {
        for (int i = 0; i < BENCH_THREADS; i++) {
            g_bench_id[i] = reg_add("bench", 10ull * 1000000ull);
            if (g_bench_id[i] < 0) g_bench_id[i] = i;   /* stub: still measurable */
        }
        double wf = bench_run(bench_wait_free, g_bench_id);
        double mx = bench_run(bench_shared_mutex, g_bench_id);
        reg_del_all();
        printf("    %d threads x %d calls:  wd_heartbeat %.1f ns/call   "
               "lock+store+unlock %.1f ns/call   (%.1fx)\n",
               BENCH_THREADS, BENCH_ITERS, wf, mx, wf > 0 ? mx / wf : 0.0);
#if WD_TSAN
        check("wd_heartbeat() is >= 1.5x cheaper than a shared-mutex write (TSan build)",
              wf * 1.5 <= mx);
#else
        check("wd_heartbeat() is >= 2.5x cheaper than a shared-mutex write",
              wf * 2.5 <= mx);
#endif
    }

    /* -------------------------------------------------------------- */
    printf("\n== Part 2: wd_next_deadline() vs a brute-force minimum (quiescent) ==\n");
    {
        static const uint64_t D[3] = { 150000, 160000, 400000 };
        static const char *NM[3] = { "modem", "uploader", "poe" };
        int id3[3];
        for (int i = 0; i < 3; i++) id3[i] = reg_add(NM[i], D[i]);
        check("three workers registered", id3[0] >= 0 && id3[1] >= 0 && id3[2] >= 0);
        for (int i = 0; i < 3; i++) { hb(id3[i]); usleep(5 * 1000); }

        int rc = wd_next_deadline(&when);
        uint64_t bm = brute_min();
        printf("    next = %llu us, brute force = %llu us, |diff| = %llu us\n",
               (unsigned long long)when, (unsigned long long)bm,
               (unsigned long long)(rc == 0 ? absdiff(when, bm) : 0));
        check("earliest deadline matches brute force", rc == 0 && absdiff(when, bm) <= ND_TOL_US);

        /* A heartbeat must move the answer WITHOUT the worker touching the heap:
         * beating the current minimum-holder hands the minimum to someone else,
         * which only shows up if the stale key is repaired on the read side. */
        usleep(30 * 1000);
        if (id3[0] >= 0) hb(id3[0]);
        usleep(2 * 1000);
        rc = wd_next_deadline(&when);
        bm = brute_min();
        check("after a heartbeat the earliest deadline is recomputed lazily",
              rc == 0 && absdiff(when, bm) <= ND_TOL_US);

        int who = brute_min_holder();
        reg_del(who);
        rc = wd_next_deadline(&when);
        bm = brute_min();
        check("unregistering the earliest worker moves the minimum to the next one",
              rc == 0 && absdiff(when, bm) <= ND_TOL_US);
        check("wd_overdue() empty while everyone is inside their deadline",
              wd_overdue(ids, WD_MAX_WORKERS) == 0);
        reg_del_all();
    }

    /* -------------------------------------------------------------- */
    printf("\n== Fault lifecycle: stop, report once, resume, stop again ==\n");
    {
        int v = reg_add("camera_prober", 80 * 1000);      /* victim */
        int s = reg_add("uploader", 500 * 1000);          /* stays alive */
        check("victim and control worker registered", v >= 0 && s >= 0);

        uint64_t t_h0 = hwwdt_now_us();
        uint64_t t_vlast = t_h0;        /* time of the victim's LAST heartbeat */
        for (int i = 0; i < 20; i++) { hb(v); t_vlast = hwwdt_now_us(); hb(s); usleep(10 * 1000); }
        uint64_t t_last = hwwdt_now_us();
        check("dog kicked while both workers are healthy",
              kicks_between(t_h0 + 20000, t_last) >= 4);
        check("no fault while both workers are healthy", faults_for(-1, t_h0, t_last) == 0);

        /* victim stops; the control worker keeps beating for 450 ms */
        for (int i = 0; i < 45; i++) { hb(s); usleep(10 * 1000); }
        uint64_t t_dead = hwwdt_now_us();

        int nf = faults_for(v, t_last, t_dead);
        char m[96];
        snprintf(m, sizeof m, "stopped worker reported EXACTLY once (got %d)", nf);
        check(m, nf == 1);

        uint64_t ft = first_fault_ts(v, t_last);
        if (ft) printf("    reported %llu us after its last heartbeat (deadline was 80000 us)\n",
                       (unsigned long long)(ft - t_vlast));
        check("reported within [deadline, deadline + 60 ms]",
              ft >= t_vlast + 80000 - 2000 && ft <= t_vlast + 80000 + 60000);
        check("the healthy worker was never reported", faults_for(s, t_last, t_dead) == 0);

        uint64_t quiet_from = t_vlast + 80000 + 40000;   /* deadline + monitor slack */
        int kq = kicks_between(quiet_from, t_dead);
        snprintf(m, sizeof m, "dog NOT kicked while a worker is overdue (%d kicks in %llu ms)",
                 kq, (unsigned long long)((t_dead - quiet_from) / 1000));
        check(m, kq == 0);

        size_t no = wd_overdue(ids, WD_MAX_WORKERS);
        check("wd_overdue() returns exactly the stopped worker",
              no == 1 && ids[0] == (uint32_t)v);

        /* resume */
        uint64_t t_res = hwwdt_now_us();
        uint64_t t_vlast2 = t_res;
        for (int i = 0; i < 30; i++) { hb(v); t_vlast2 = hwwdt_now_us(); hb(s); usleep(10 * 1000); }
        check("wd_overdue() empty again after the worker resumes",
              wd_overdue(ids, WD_MAX_WORKERS) == 0);
        check("dog kicked again once the worker resumed",
              kicks_between(t_res + 50000, hwwdt_now_us()) >= 2);

        /* and it must be reported again if it dies a second time */
        for (int i = 0; i < 45; i++) { hb(s); usleep(10 * 1000); }
        check("a second death is reported again (the fault flag was cleared)",
              faults_for(v, t_vlast2 + 1000, hwwdt_now_us()) == 1);
        reg_del_all();
    }

    /* -------------------------------------------------------------- */
    printf("\n== %d workers beating for ~1.6 s, with register/unregister churn ==\n", NSTRESS);
    {
        int ok_reg = 1;
        for (int i = 0; i < NSTRESS; i++) {
            char nm[WD_NAME_MAX];
            snprintf(nm, sizeof nm, "w%d", i);
            uint64_t d = 300000ull + (xs32() % 8u) * 25000ull;   /* 300..475 ms */
            g_wk[i].period_us = 4000ull + (xs32() % 6u) * 1000ull; /* 4..9 ms   */
            g_wk[i].id = reg_add(nm, d);
            if (g_wk[i].id < 0) { ok_reg = 0; g_wk[i].id = i; }
        }
        check("all stress workers registered", ok_reg == 1);

        uint64_t t_s0 = hwwdt_now_us();
        atomic_store(&g_wk_run, true);
        atomic_store(&g_wk_quiet, false);
        pthread_t th[NSTRESS];
        for (int i = 0; i < NSTRESS; i++) pthread_create(&th[i], NULL, wk_main, &g_wk[i]);

        int nd_checks = 0, nd_bad = 0, od_bad = 0, churn = 0;
        int transient = -1;
        uint64_t tr_t = 0;
        uint64_t worst_nd = 0;

        while (hwwdt_now_us() - t_s0 < 1600 * 1000) {
            usleep(35 * 1000);

            /* register/unregister while everything is running */
            if (transient < 0) {
                if (xs32() & 1u) {
                    transient = reg_add("transient", 300 * 1000);
                    tr_t = hwwdt_now_us();
                    churn++;
                }
            } else if (hwwdt_now_us() - tr_t > 90 * 1000) {
                reg_del(transient);
                transient = -1;
            }
            if (transient >= 0) hb(transient);

            if (wd_overdue(ids, WD_MAX_WORKERS) != 0) od_bad++;

            /* freeze the beats, compare, unfreeze */
            atomic_store(&g_wk_quiet, true);
            usleep(WD_TSAN ? 20 * 1000 : 10 * 1000);
            int rc = wd_next_deadline(&when);
            uint64_t bm = brute_min();
            nd_checks++;
            uint64_t d = (rc == 0) ? absdiff(when, bm) : ND_TOL_US * 1000;
            if (d > worst_nd) worst_nd = d;
            if (rc != 0 || d > ND_TOL_US) nd_bad++;
            atomic_store(&g_wk_quiet, false);
        }

        atomic_store(&g_wk_run, false);
        for (int i = 0; i < NSTRESS; i++) pthread_join(th[i], NULL);
        uint64_t t_s1 = hwwdt_now_us();
        if (transient >= 0) { reg_del(transient); transient = -1; }

        printf("    %llu heartbeats, %d next-deadline samples (worst |diff| %llu us), "
               "%d register/unregister cycles\n",
               (unsigned long long)atomic_load(&g_beats), nd_checks,
               (unsigned long long)worst_nd, churn);

        char m[96];
        snprintf(m, sizeof m, "wd_next_deadline() matched brute force on all %d samples", nd_checks);
        check(m, nd_checks >= 15 && nd_bad == 0);
        check("wd_overdue() stayed empty while every worker was alive", od_bad == 0);
        check("no fault reported during the run", faults_for(-1, t_s0 + 50000, t_s1) == 0);
        {
            uint64_t gap = max_kick_gap(t_s0 + 50000, t_s1);
            snprintf(m, sizeof m, "worst kick gap %llu us <= window %llu us",
                     (unsigned long long)gap, (unsigned long long)HWWDT_TIMEOUT_US);
            check(m, gap <= HWWDT_TIMEOUT_US);
        }
        check("at least one worker registered and one unregistered mid-flight", churn >= 1);
        reg_del_all();
    }

    /* -------------------------------------------------------------- */
    printf("\n== Shutdown ==\n");
    wd_deinit();
    {
        uint64_t t_off = hwwdt_now_us();
        usleep(150 * 1000);
        check("no kick after wd_deinit() (monitor thread was joined)",
              kicks_between(t_off + 1000, hwwdt_now_us()) == 0);
    }

    printf("\ntotal kicks %d, total fault reports %d, elapsed %llu ms\n",
           g_kick_n, g_flt_n, (unsigned long long)((hwwdt_now_us() - t_boot) / 1000));
    if (g_kick_lost || g_flt_lost) printf("(harness log overflowed)\n");

    if (fails) printf("\n%d CHECK(S) FAILED\n", fails);
    else       printf("\nALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}
