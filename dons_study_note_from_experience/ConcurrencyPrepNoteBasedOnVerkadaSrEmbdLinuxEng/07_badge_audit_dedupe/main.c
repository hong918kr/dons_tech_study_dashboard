/* main.c — test harness + fake AC42 vendor library.  GIVEN; do not modify.
 *
 *   ./main.sh          builds main.c + audit_log.c          (your code)
 *   ./main.sh sol      builds main.c + audit_log_solution.c (reference)
 *
 * Everything is checked against ground truth that the fake vendor records as it
 * runs — there are no hard-coded expected values, so the result is the same on
 * macOS and Linux. Randomness is a hand-rolled xorshift with a fixed per-door
 * seed (never rand(): its sequence differs per platform).
 *
 * Phases
 *   0  empty state right after audit_init()
 *   A  load: 4 readers produce faster than the audit store can append, while 3
 *      harness threads hammer the getters (thread safety + getter latency)
 *   B  park the readers, wait for the queue to drain
 *   C  accounting + per-door ordering of everything that reached the store
 *   D  audit_is_duplicate() vs brute force over the store's contents
 *   E  audit_recent_for_door() vs the store's contents
 *   F  dedupe table: lazy age eviction, then capacity pressure, then latency
 *   G  audit_deinit() while all 4 producers are parked in reader_wait_badge()
 *
 * Scaled delays (real AC42 numbers in parentheses):
 *   reader_wait_badge()  2-9 ms, or ~3 s when "parked"        (seconds..minutes)
 *   audit_store_write()  8 ms, 0 during the harness fill phase        (~1 s)
 */
/* glibc hides usleep() and clock_gettime() when the compiler is in strict ISO C
 * mode, which -std=c11 is, so ask for the full set on Linux. NOT on Apple: there
 * _XOPEN_SOURCE / _POSIX_C_SOURCE would *hide* the _np condvar call below.
 * Must come before any #include. */
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "audit_log.h"
#include "reader.h"

/* ------------------------------------------------------------------ */
/* knobs                                                               */
/* ------------------------------------------------------------------ */

#define LOAD_PHASE_US      1200000ull   /* phase A duration                   */
#define STORE_WRITE_US     8000ull      /* scaled audit_store_write() cost     */
#define READER_MIN_US      2000ull      /* scaled reader_wait_badge() wait     */
#define READER_SPAN_US     7000ull
#define READER_PARK_US     3000000ull   /* "nobody is at the door" wait        */
#define READER_ERR_ONE_IN  64u          /* transient read errors               */

#define BADGE_ID_BASE      4000u        /* the badge population at this site   */
#define BADGE_ID_POOL      64u

#define PROD_MAX           2048         /* ground truth per door               */
#define SINK_MAX           8192         /* records the fake store keeps        */
#define FILL_MAX           4096         /* phase F synthetic submissions       */
#define AGE_BATCH          500          /* phase F1 batch size                 */

#define STRESS_THREADS     3
#define GETTER_AVG_LIMIT_US 500ull      /* a getter must not wait on the store */
#define GETTER_MAX_LIMIT_US 200000ull   /* ...and must never wait on a reader  */

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

static int g_fails;

static void check(bool cond, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  %s  ", cond ? "PASS" : "FAIL");
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
    if (!cond)
        g_fails++;
}

static uint32_t rng32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static uint64_t now_us(void) { return reader_now_us(); }

/* Portable "wait on a condvar for at most us microseconds": macOS has no
 * pthread_condattr_setclock, so branch on the platform. */
static int cv_wait_us(pthread_cond_t *cv, pthread_mutex_t *m, uint64_t us)
{
#ifdef __APPLE__
    struct timespec rel;
    rel.tv_sec = (time_t)(us / 1000000ull);
    rel.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    return pthread_cond_timedwait_relative_np(cv, m, &rel);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(us / 1000000ull);
    ts.tv_nsec += (long)((us % 1000000ull) * 1000ull);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec += 1;
    }
    return pthread_cond_timedwait(cv, m, &ts);
#endif
}

static void sleep_us(uint64_t us)
{
    while (us > 500000ull) {
        usleep(500000);
        us -= 500000ull;
    }
    if (us)
        usleep((useconds_t)us);
}

/* ------------------------------------------------------------------ */
/* ground truth: what the readers produced, what the store received    */
/* ------------------------------------------------------------------ */

static pthread_mutex_t g_truth_lock = PTHREAD_MUTEX_INITIALIZER;

static struct Badge g_prod[AC42_DOORS][PROD_MAX];
static size_t       g_prod_n[AC42_DOORS];
static uint64_t     g_produced_total;
static uint64_t     g_prod_overflow;

static struct Badge g_sink[SINK_MAX];
static size_t       g_sink_n;
static uint64_t     g_sink_total;
static uint64_t     g_sink_overflow;

static uint64_t g_same_door_violations;   /* vendor per-door contract broken   */
static uint64_t g_store_threads_extra;    /* store called from >1 thread       */
static uint64_t g_store_concurrent;       /* two threads inside it at once     */
static pthread_t g_store_owner;
static bool      g_store_owner_set;
static uint64_t  g_read_errors;
static int       g_cancelled[AC42_DOORS];

static uint64_t g_t_start;                /* set just before audit_init()      */

typedef struct {
    uint64_t produced, logged, dropped_vendor_err;
    uint64_t prod_overflow, sink_overflow;
    uint64_t same_door, store_foreign, store_concurrent;
    int      cancelled_doors;
} snap_t;

static snap_t snapshot(void)
{
    snap_t s;
    memset(&s, 0, sizeof s);
    pthread_mutex_lock(&g_truth_lock);
    s.produced = g_produced_total;
    s.logged = g_sink_total;
    s.dropped_vendor_err = g_read_errors;
    s.prod_overflow = g_prod_overflow;
    s.sink_overflow = g_sink_overflow;
    s.same_door = g_same_door_violations;
    s.store_foreign = g_store_threads_extra;
    s.store_concurrent = g_store_concurrent;
    for (int d = 0; d < AC42_DOORS; d++)
        s.cancelled_doors += g_cancelled[d];
    pthread_mutex_unlock(&g_truth_lock);
    return s;
}

static void truth_record_produced(const struct Badge *b)
{
    pthread_mutex_lock(&g_truth_lock);
    if (g_prod_n[b->door] < PROD_MAX)
        g_prod[b->door][g_prod_n[b->door]++] = *b;
    else
        g_prod_overflow++;
    g_produced_total++;
    pthread_mutex_unlock(&g_truth_lock);
}

static uint64_t sink_total(void)
{
    pthread_mutex_lock(&g_truth_lock);
    uint64_t v = g_sink_total;
    pthread_mutex_unlock(&g_truth_lock);
    return v;
}

/* newest logged sighting of `id` in the store */
static bool sink_last_seen(uint32_t id, uint64_t *out_ts)
{
    bool found = false;
    uint64_t best = 0;
    pthread_mutex_lock(&g_truth_lock);
    for (size_t i = 0; i < g_sink_n; i++) {
        if (g_sink[i].badge_id == id && (!found || g_sink[i].timestamp > best)) {
            best = g_sink[i].timestamp;
            found = true;
        }
    }
    pthread_mutex_unlock(&g_truth_lock);
    if (found && out_ts)
        *out_ts = best;
    return found;
}

/* ------------------------------------------------------------------ */
/* ========= fake AC42 vendor library (given by the interviewer) ===== */
/* ------------------------------------------------------------------ */

static _Atomic uint64_t g_reader_park;      /* 1 = nobody is at the doors      */
static _Atomic uint64_t g_store_delay_us = STORE_WRITE_US;

static struct DoorSim {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    bool            cancelled;
    _Atomic int     inflight;
    uint32_t        rng;
} g_door[AC42_DOORS];

uint64_t reader_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

static void door_sim_init(void)
{
    for (int d = 0; d < AC42_DOORS; d++) {
        pthread_mutex_init(&g_door[d].m, NULL);
        pthread_cond_init(&g_door[d].cv, NULL);
        g_door[d].cancelled = false;
        atomic_store(&g_door[d].inflight, 0);
        g_door[d].rng = 0x9e3779b9u + 0x85ebca6bu * (uint32_t)(d + 1);
    }
}

static int reader_wait_inner(uint8_t door, struct Badge *out)
{
    struct DoorSim *d = &g_door[door];
    bool parked = atomic_load(&g_reader_park) != 0;
    uint64_t want = parked ? READER_PARK_US
                           : READER_MIN_US + rng32(&d->rng) % READER_SPAN_US;
    uint64_t deadline = now_us() + want;

    pthread_mutex_lock(&d->m);
    for (;;) {
        if (d->cancelled)
            break;
        uint64_t n = now_us();
        if (n >= deadline)
            break;
        cv_wait_us(&d->cv, &d->m, deadline - n);
    }
    bool cancelled = d->cancelled;
    pthread_mutex_unlock(&d->m);

    if (cancelled)
        return -1;                          /* interrupted by reader_cancel()  */
    if (atomic_load(&g_reader_park) != 0)
        return -1;                          /* nobody showed up                */
    if (rng32(&d->rng) % READER_ERR_ONE_IN == 0u) {
        pthread_mutex_lock(&g_truth_lock);
        g_read_errors++;
        pthread_mutex_unlock(&g_truth_lock);
        return -1;                          /* transient read error            */
    }

    struct Badge b;
    b.badge_id = BADGE_ID_BASE + rng32(&d->rng) % BADGE_ID_POOL;
    b.door = door;
    b.timestamp = now_us();
    truth_record_produced(&b);              /* harness only                    */
    *out = b;
    return 0;
}

int reader_wait_badge(uint8_t door, struct Badge *out)
{
    if (door >= AC42_DOORS || out == NULL)
        return -1;
    struct DoorSim *d = &g_door[door];
    if (atomic_exchange(&d->inflight, 1) != 0) {
        /* two threads inside the same door: the vendor calls this UB */
        pthread_mutex_lock(&g_truth_lock);
        g_same_door_violations++;
        pthread_mutex_unlock(&g_truth_lock);
    }
    int rc = reader_wait_inner(door, out);
    atomic_store(&d->inflight, 0);
    return rc;
}

void reader_cancel(uint8_t door)
{
    if (door >= AC42_DOORS)
        return;
    struct DoorSim *d = &g_door[door];
    pthread_mutex_lock(&d->m);
    d->cancelled = true;                    /* sticky */
    pthread_cond_broadcast(&d->cv);
    pthread_mutex_unlock(&d->m);
    pthread_mutex_lock(&g_truth_lock);
    g_cancelled[door] = 1;
    pthread_mutex_unlock(&g_truth_lock);
}

int audit_store_write(const struct Badge *b)
{
    static _Atomic int inflight;

    if (b == NULL)
        return -1;
    if (atomic_exchange(&inflight, 1) != 0) {
        pthread_mutex_lock(&g_truth_lock);
        g_store_concurrent++;
        pthread_mutex_unlock(&g_truth_lock);
    }

    pthread_mutex_lock(&g_truth_lock);
    if (!g_store_owner_set) {
        g_store_owner = pthread_self();
        g_store_owner_set = true;
    } else if (!pthread_equal(g_store_owner, pthread_self())) {
        g_store_threads_extra++;
    }
    if (g_sink_n < SINK_MAX)
        g_sink[g_sink_n++] = *b;
    else
        g_sink_overflow++;
    g_sink_total++;
    pthread_mutex_unlock(&g_truth_lock);

    uint64_t delay = atomic_load(&g_store_delay_us);
    if (delay)
        sleep_us(delay);

    atomic_store(&inflight, 0);
    return 0;
}

/* ================= end of the fake vendor library ================== */

/* ------------------------------------------------------------------ */
/* phase A: getter stress                                              */
/* ------------------------------------------------------------------ */

static _Atomic int g_stress_stop;

typedef struct {
    uint64_t calls, bad, max_us, sum_us;
} stress_t;

static void *stress_main(void *arg)
{
    stress_t *st = arg;
    struct Badge tmp[AUDIT_RECENT_PER_DOOR];
    uint32_t rng = 0xdeadbeefu ^ (uint32_t)(uintptr_t)arg;

    while (!atomic_load(&g_stress_stop)) {
        uint8_t door = (uint8_t)(rng32(&rng) % (uint32_t)AC42_DOORS);
        uint32_t id = BADGE_ID_BASE + rng32(&rng) % BADGE_ID_POOL;

        uint64_t t0 = now_us();
        int dup = audit_is_duplicate(id, t0, 50000ull);
        size_t k = audit_recent_for_door(door, tmp, AUDIT_RECENT_PER_DOOR);
        uint64_t t1 = now_us();
        uint64_t dt = t1 - t0;

        if (dt > st->max_us)
            st->max_us = dt;
        st->sum_us += dt;
        st->calls++;
        (void)dup;

        /* No torn or invented records: every field in range, the door must be
         * the one we asked for, newest first. The exact comparison against
         * ground truth happens later, when the pipeline is quiescent. */
        if (k > AUDIT_RECENT_PER_DOOR) {
            st->bad++;
            continue;
        }
        for (size_t i = 0; i < k; i++) {
            if (tmp[i].door != door)
                st->bad++;
            if (tmp[i].badge_id < BADGE_ID_BASE ||
                tmp[i].badge_id >= BADGE_ID_BASE + BADGE_ID_POOL)
                st->bad++;
            if (tmp[i].timestamp < g_t_start || tmp[i].timestamp > t1)
                st->bad++;
            if (i + 1 < k && tmp[i].timestamp < tmp[i + 1].timestamp)
                st->bad++;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* helpers for phases B and F                                          */
/* ------------------------------------------------------------------ */

/* Wait until the store has been idle for 200 ms (queue drained), max 1.5 s. */
static void drain_wait(void)
{
    uint64_t deadline = now_us() + 1500000ull;
    uint64_t last = sink_total();
    uint64_t stable_since = now_us();
    while (now_us() < deadline) {
        usleep(20000);
        uint64_t c = sink_total();
        if (c != last) {
            last = c;
            stable_since = now_us();
            continue;
        }
        if (now_us() - stable_since > 200000ull)
            return;
    }
}

static uint32_t g_fill_ids[FILL_MAX];
static size_t   g_fill_n;

/* Push synthetic badges straight into the pipeline (audit_submit is the
 * producer API, so the harness may use it too). Returns #accepted and, when
 * `record` is set, leaves the accepted ids in g_fill_ids in submission order. */
static size_t fill_submit(uint32_t first_id, size_t count, uint64_t first_ts, bool record)
{
    size_t accepted = 0, consecutive_fail = 0;
    if (record)
        g_fill_n = 0;
    for (size_t i = 0; i < count; i++) {
        struct Badge b;
        b.badge_id = first_id + (uint32_t)i;
        b.door = 0;
        b.timestamp = first_ts + (uint64_t)i;
        bool ok = false;
        for (int attempt = 0; attempt < 5 && !ok; attempt++) {
            if (audit_submit(&b) == 0)
                ok = true;
            else
                usleep(200);
        }
        if (ok) {
            accepted++;
            consecutive_fail = 0;
            if (record && g_fill_n < FILL_MAX)
                g_fill_ids[g_fill_n++] = b.badge_id;
        } else if (++consecutive_fail >= 20) {
            break;      /* nothing is consuming: give up instead of hanging */
        }
    }
    drain_wait();
    return accepted;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    door_sim_init();

    printf("== Phase 0: state right after audit_init() ==\n");
    g_t_start = now_us();
    if (audit_init() != 0) {
        printf("  FAIL  audit_init() returned non-zero\n");
        printf("\n1 CHECK(S) FAILED\n");
        return 1;
    }
    {
        struct Badge tmp[AUDIT_RECENT_PER_DOOR];
        size_t k = audit_recent_for_door(0, tmp, AUDIT_RECENT_PER_DOOR);
        check(k == 0, "no history yet: audit_recent_for_door(0) = %zu, want 0", k);
        int dup = audit_is_duplicate(BADGE_ID_BASE, g_t_start, 1000000ull);
        check(dup == 0, "nothing is a duplicate yet: got %d, want 0", dup);
        check(audit_dropped() == 0, "audit_dropped() = %llu, want 0",
              (unsigned long long)audit_dropped());
        check(audit_recent_for_door((uint8_t)AC42_DOORS, tmp, AUDIT_RECENT_PER_DOOR) == 0,
              "out-of-range door returns 0");
        check(audit_recent_for_door(0, NULL, 4) == 0, "NULL out returns 0");
        check(audit_recent_for_door(0, tmp, 0) == 0, "max == 0 returns 0");
    }

    printf("\n== Phase A: %llu ms of load, 4 readers vs one %llu ms store ==\n",
           (unsigned long long)(LOAD_PHASE_US / 1000),
           (unsigned long long)(STORE_WRITE_US / 1000));
    pthread_t th[STRESS_THREADS];
    stress_t st[STRESS_THREADS];
    memset(st, 0, sizeof st);
    atomic_store(&g_stress_stop, 0);
    for (int i = 0; i < STRESS_THREADS; i++)
        pthread_create(&th[i], NULL, stress_main, &st[i]);
    sleep_us(LOAD_PHASE_US);
    atomic_store(&g_stress_stop, 1);
    uint64_t calls = 0, bad = 0, max_us = 0, sum_us = 0;
    for (int i = 0; i < STRESS_THREADS; i++) {
        pthread_join(th[i], NULL);
        calls += st[i].calls;
        bad += st[i].bad;
        sum_us += st[i].sum_us;
        if (st[i].max_us > max_us)
            max_us = st[i].max_us;
    }
    double avg_us = calls ? (double)sum_us / (double)calls : 1e9;
    printf("  %d reader threads made %llu getter calls, avg %.2f us, worst %llu us\n",
           STRESS_THREADS, (unsigned long long)calls, avg_us, (unsigned long long)max_us);
    check(calls > 0 && bad == 0, "no torn / out-of-range / misordered records (%llu bad)",
          (unsigned long long)bad);
    check(calls > 0 && avg_us < (double)GETTER_AVG_LIMIT_US,
          "getters do not queue behind the %llu ms store append (avg %.2f us < %llu us)",
          (unsigned long long)(STORE_WRITE_US / 1000), avg_us,
          (unsigned long long)GETTER_AVG_LIMIT_US);
    check(calls > 0 && max_us < GETTER_MAX_LIMIT_US,
          "no getter ever waited on a reader (worst %llu us < %llu us)",
          (unsigned long long)max_us, (unsigned long long)GETTER_MAX_LIMIT_US);

    printf("\n== Phase B: park the readers and drain ==\n");
    atomic_store(&g_reader_park, 1);
    drain_wait();
    snap_t s = snapshot();
    uint64_t dropped = audit_dropped();
    printf("  produced %llu, logged %llu, dropped %llu, vendor read errors %llu\n",
           (unsigned long long)s.produced, (unsigned long long)s.logged,
           (unsigned long long)dropped, (unsigned long long)s.dropped_vendor_err);
    check(s.prod_overflow == 0 && s.sink_overflow == 0,
          "harness ground-truth buffers did not overflow");

    printf("\n== Phase C: accounting + per-door ordering ==\n");
    check(s.produced > 0, "the readers were actually used (%llu badges)",
          (unsigned long long)s.produced);
    check(s.logged > 0, "the audit store was actually written (%llu records)",
          (unsigned long long)s.logged);
    check(s.produced == s.logged + dropped,
          "every badge accounted for: produced %llu == logged %llu + dropped %llu",
          (unsigned long long)s.produced, (unsigned long long)s.logged,
          (unsigned long long)dropped);
    check(dropped > 0, "a %u-slot queue in front of an %llu ms store did drop (%llu)",
          (unsigned)AUDIT_QUEUE_CAP, (unsigned long long)(STORE_WRITE_US / 1000),
          (unsigned long long)dropped);
    check(audit_logged() == s.logged, "audit_logged() %llu == store appends %llu",
          (unsigned long long)audit_logged(), (unsigned long long)s.logged);
    check(s.same_door == 0,
          "reader_wait_badge() never called twice for one door (%llu violations)",
          (unsigned long long)s.same_door);
    check(s.store_foreign == 0 && s.store_concurrent == 0,
          "audit_store_write() only ever entered by one thread (%llu foreign, %llu overlapping)",
          (unsigned long long)s.store_foreign, (unsigned long long)s.store_concurrent);

    /* Per-door ordering: for each door, the records that reached the store must
     * be a SUBSEQUENCE of what that door produced, in production order. Drops
     * may punch holes; reordering, duplication and invention are failures. */
    {
        size_t matched = 0, order_errors = 0, ts_errors = 0;
        pthread_mutex_lock(&g_truth_lock);
        for (int d = 0; d < AC42_DOORS; d++) {
            size_t pi = 0;
            uint64_t prev_ts = 0;
            for (size_t i = 0; i < g_sink_n; i++) {
                if (g_sink[i].door != d)
                    continue;
                if (g_sink[i].timestamp < prev_ts)
                    ts_errors++;
                prev_ts = g_sink[i].timestamp;
                while (pi < g_prod_n[d] &&
                       !(g_prod[d][pi].badge_id == g_sink[i].badge_id &&
                         g_prod[d][pi].timestamp == g_sink[i].timestamp))
                    pi++;
                if (pi == g_prod_n[d]) {
                    order_errors++;     /* reordered, duplicated or invented */
                } else {
                    pi++;
                    matched++;
                }
            }
        }
        pthread_mutex_unlock(&g_truth_lock);
        check(order_errors == 0 && matched == (size_t)s.logged,
              "per-door order preserved: %zu/%llu records matched in order, %zu out of order",
              matched, (unsigned long long)s.logged, order_errors);
        check(ts_errors == 0, "timestamps non-decreasing within each door (%zu inversions)",
              ts_errors);
    }

    printf("\n== Phase D: audit_is_duplicate() vs brute force ==\n");
    {
        uint64_t t_q = now_us();
        const uint64_t windows[] = {0ull, 1000ull, 20000ull, 100000ull, 500000ull, 2000000ull};
        size_t mismatch = 0, dup_true = 0, dup_false = 0, shown = 0;
        for (uint32_t id = BADGE_ID_BASE; id < BADGE_ID_BASE + BADGE_ID_POOL; id++) {
            uint64_t last = 0;
            bool seen = sink_last_seen(id, &last);
            for (size_t w = 0; w < sizeof windows / sizeof windows[0]; w++) {
                bool want = seen && last <= t_q && (t_q - last) <= windows[w];
                int got = audit_is_duplicate(id, t_q, windows[w]);
                if ((got != 0) != want) {
                    mismatch++;
                    if (shown++ < 3)
                        printf("        id %u window %llu us: got %d want %d (last seen: %s)\n",
                               id, (unsigned long long)windows[w], got, (int)want,
                               seen ? "yes" : "never");
                }
                if (want)
                    dup_true++;
                else
                    dup_false++;
            }
        }
        check(mismatch == 0 && dup_true > 0 && dup_false > 0,
              "%zu queries agree with brute force (%zu duplicates, %zu not)",
              dup_true + dup_false, dup_true, dup_false);

        uint32_t ghost = BADGE_ID_BASE + BADGE_ID_POOL + 7u;
        check(audit_is_duplicate(ghost, t_q, 2000000ull) == 0,
              "a badge that was never presented is not a duplicate");

        /* exactly at the window edge, one microsecond inside it, and a now_us
         * that predates the sighting */
        uint32_t probe = 0;
        uint64_t probe_ts = 0;
        for (uint32_t id = BADGE_ID_BASE; id < BADGE_ID_BASE + BADGE_ID_POOL; id++) {
            if (sink_last_seen(id, &probe_ts)) {
                probe = id;
                break;
            }
        }
        if (probe == 0) {
            check(false, "window-edge cases: no logged badge to probe with");
        } else {
            uint64_t age = t_q - probe_ts;
            check(audit_is_duplicate(probe, t_q, age) == 1,
                  "exactly at the window edge counts as a duplicate (age %llu us)",
                  (unsigned long long)age);
            check(age == 0 || audit_is_duplicate(probe, t_q, age - 1) == 0,
                  "one microsecond inside the edge does not");
            check(audit_is_duplicate(probe, probe_ts, 0) == 1,
                  "window 0 at the exact sighting time is a duplicate");
            check(probe_ts == 0 || audit_is_duplicate(probe, probe_ts - 1, 1000000ull) == 0,
                  "a now_us before the sighting is not a duplicate");
        }
    }

    printf("\n== Phase E: audit_recent_for_door() ==\n");
    {
        size_t bad_doors = 0;
        for (int d = 0; d < AC42_DOORS; d++) {
            struct Badge want[AUDIT_RECENT_PER_DOOR];
            size_t want_n = 0;
            pthread_mutex_lock(&g_truth_lock);
            for (size_t i = g_sink_n; i-- > 0 && want_n < AUDIT_RECENT_PER_DOOR;)
                if (g_sink[i].door == d)
                    want[want_n++] = g_sink[i];      /* newest first */
            pthread_mutex_unlock(&g_truth_lock);

            struct Badge got[AUDIT_RECENT_PER_DOOR + 8];
            memset(got, 0, sizeof got);
            size_t got_n = audit_recent_for_door((uint8_t)d, got, AUDIT_RECENT_PER_DOOR + 8);
            bool ok = (got_n == want_n);
            for (size_t i = 0; ok && i < got_n; i++)
                ok = got[i].badge_id == want[i].badge_id &&
                     got[i].timestamp == want[i].timestamp && got[i].door == want[i].door;
            if (!ok) {
                bad_doors++;
                printf("        door %d: got %zu records, want %zu\n", d, got_n, want_n);
            }

            /* a smaller max must return the NEWEST 3, not the oldest 3 */
            size_t few = audit_recent_for_door((uint8_t)d, got, 3);
            if (few != (want_n < 3 ? want_n : 3) ||
                (few > 0 && (got[0].timestamp != want[0].timestamp ||
                             got[0].badge_id != want[0].badge_id)))
                bad_doors++;
        }
        check(bad_doors == 0, "all %d doors return the newest %u records, newest first",
              AC42_DOORS, (unsigned)AUDIT_RECENT_PER_DOOR);
    }

    printf("\n== Phase F: dedupe table under age and capacity pressure ==\n");
    atomic_store(&g_store_delay_us, 0);    /* fast store so the fill is quick */
    {
        uint64_t vt = now_us() + 1000ull;
        size_t live0 = audit_dedupe_live();

        /* F1 — lazy age eviction. AGE_BATCH badges, then AGE_BATCH more a full
         * retention period later: the second batch must recycle the first
         * batch's aged-out slots instead of forcing live evictions. */
        uint64_t ev_before = audit_dedupe_evictions();
        size_t a1 = fill_submit(100000u, AGE_BATCH, vt, false);
        uint64_t vt2 = vt + AUDIT_DEDUPE_RETENTION_US + 1000000ull;
        size_t a2 = fill_submit(200000u, AGE_BATCH, vt2, true);
        uint64_t ev_after = audit_dedupe_evictions();
        size_t live1 = audit_dedupe_live();
        printf("  batch1 %zu accepted, batch2 (+%llu s) %zu accepted; live %zu -> %zu,"
               " stale reuse %llu, forced evictions +%llu\n",
               a1, (unsigned long long)(AUDIT_DEDUPE_RETENTION_US / 1000000ull + 1), a2,
               live0, live1, (unsigned long long)audit_dedupe_stale_reuse(),
               (unsigned long long)(ev_after - ev_before));
        check(a1 >= AGE_BATCH - 50 && a2 >= AGE_BATCH - 50,
              "both batches went through (%zu, %zu of %d)", a1, a2, AGE_BATCH);
        check(live1 <= AUDIT_DEDUPE_CAP, "live slots %zu <= capacity %u", live1,
              (unsigned)AUDIT_DEDUPE_CAP);
        check(live1 <= AUDIT_DEDUPE_LOAD_LIMIT,
              "lazy eviction kept the table inside its %u-slot budget "
              "(%zu live after %zu distinct badges)",
              (unsigned)AUDIT_DEDUPE_LOAD_LIMIT, live1, a1 + a2);
        check(ev_after - ev_before < 50,
              "aged-out slots were recycled instead of evicting live ones (%llu forced)",
              (unsigned long long)(ev_after - ev_before));
        if (g_fill_n > 0)
            check(audit_is_duplicate(g_fill_ids[g_fill_n - 1], vt2 + AGE_BATCH,
                                     1000000ull) == 1,
                  "the newest badge of batch 2 is still known");
        else
            check(false, "batch 2 accepted nothing");

        /* F2 — capacity pressure: far more distinct recent badges than slots. */
        uint64_t vt3 = vt2 + 2ull * AUDIT_DEDUPE_RETENTION_US;
        uint64_t ev2_before = audit_dedupe_evictions();
        size_t a3 = fill_submit(300000u, FILL_MAX, vt3, true);
        uint64_t vt_now = vt3 + (uint64_t)FILL_MAX + 1000ull;
        size_t live2 = audit_dedupe_live();
        printf("  %zu distinct recent badges into %u slots: live %zu, forced evictions %llu\n",
               a3, (unsigned)AUDIT_DEDUPE_CAP, live2,
               (unsigned long long)(audit_dedupe_evictions() - ev2_before));
        check(a3 > FILL_MAX / 2, "the fill went through (%zu of %d)", a3, FILL_MAX);
        check(live2 <= AUDIT_DEDUPE_CAP, "live slots %zu never exceed capacity %u", live2,
              (unsigned)AUDIT_DEDUPE_CAP);
        check(audit_dedupe_evictions() > ev2_before,
              "capacity pressure is reported as evictions instead of silently corrupting");

        if (g_fill_n >= 2) {
            check(audit_is_duplicate(g_fill_ids[g_fill_n - 1], vt_now, 5000000ull) == 1 &&
                      audit_is_duplicate(g_fill_ids[g_fill_n - 2], vt_now, 5000000ull) == 1,
                  "a just-inserted badge is never its own eviction victim");
        } else {
            check(false, "fill phase produced nothing to probe");
        }

        if (g_fill_n >= AUDIT_DEDUPE_CAP / 2) {
            size_t n = AUDIT_DEDUPE_CAP / 2, hit = 0;
            for (size_t i = g_fill_n - n; i < g_fill_n; i++)
                if (audit_is_duplicate(g_fill_ids[i], vt_now, 5000000ull))
                    hit++;
            check(hit * 100 >= n * 70,
                  "eviction prefers the oldest: %zu/%zu of the most recent badges kept",
                  hit, n);
        } else {
            check(false, "not enough accepted badges to test eviction preference");
        }

        size_t fp = 0;
        for (uint32_t i = 0; i < 256; i++)
            if (audit_is_duplicate(900000u + i, vt_now, 5000000ull))
                fp++;
        check(fp == 0, "no false positives among 256 never-seen badges (%zu)", fp);

        /* O(1)-ish lookups: an unbounded probe loop or a full scan shows here. */
        uint32_t rng = 0x12345677u;
        uint64_t t0 = now_us();
        int hits = 0;
        for (int i = 0; i < 20000; i++)
            hits += audit_is_duplicate(300000u + rng32(&rng) % 8000u, vt_now, 5000000ull);
        uint64_t elapsed = now_us() - t0;
        printf("  20000 random lookups in %llu us (%.3f us each), %d hits\n",
               (unsigned long long)elapsed, (double)elapsed / 20000.0, hits);
        check(elapsed < 2000000ull, "20000 lookups stay well under 2 s (%llu us)",
              (unsigned long long)elapsed);
    }
    atomic_store(&g_store_delay_us, STORE_WRITE_US);

    printf("\n== Phase G: shutdown while all 4 producers are parked ==\n");
    sleep_us(100000);                       /* make sure everyone is parked    */
    uint64_t sink_before = sink_total();
    uint64_t t_deinit = now_us();
    audit_deinit();
    uint64_t deinit_us = now_us() - t_deinit;
    printf("  audit_deinit() returned in %llu us (the readers were parked for up to %llu us)\n",
           (unsigned long long)deinit_us, (unsigned long long)READER_PARK_US);
    check(deinit_us < 250000ull,
          "deinit cancelled the vendor call instead of waiting it out (%llu us)",
          (unsigned long long)deinit_us);
    s = snapshot();
    check(s.cancelled_doors == AC42_DOORS, "reader_cancel() called for all %d doors (%d)",
          AC42_DOORS, s.cancelled_doors);
    {
        struct Badge b = {.badge_id = 1u, .door = 0, .timestamp = now_us()};
        check(audit_submit(&b) < 0, "audit_submit() after deinit is rejected, not queued");
    }
    sleep_us(150000);
    uint64_t sink_after = sink_total();
    check(sink_after == sink_before, "no store writes after deinit (%llu -> %llu)",
          (unsigned long long)sink_before, (unsigned long long)sink_after);
    s = snapshot();
    check(s.same_door == 0 && s.store_concurrent == 0 && s.store_foreign == 0,
          "vendor contracts still intact after shutdown");

    if (g_fails)
        printf("\n%d CHECK(S) FAILED\n", g_fails);
    else
        printf("\nALL CHECKS PASSED (0 failed)\n");
    return g_fails ? 1 : 0;
}
