/* main.c — test harness + fake vendor event source (GIVEN; do not modify).
 *
 *   ./main.sh          build main.c + event_queue.c          (your code)
 *   ./main.sh sol      build main.c + event_queue_solution.c (reference)
 *   ./main.sh fast     tiny buckets/window, to exercise bucket eviction
 *
 * What is faked in here, and why it is safe to test against:
 *
 *  - The vendor is a single-threaded blocking reader with NO timeout. Real
 *    gaps of minutes are compressed to 20-200 ms so the whole run fits in a
 *    few seconds (see question_note).
 *  - Randomness is a hand-rolled xorshift32 with a fixed seed, so the run is
 *    the same on macOS and Linux (rand() is not).
 *  - Every event the vendor hands out is recorded in `truth[]` FIRST, so the
 *    harness never compares against hard-coded numbers: it compares against
 *    what actually happened.
 *  - Two knobs only the harness can reach make the timing tests deterministic:
 *      fake_pause(true)        the site goes quiet: no more events, the vendor
 *                              call just parks (this is what "blocking with no
 *                              timeout" feels like)
 *      fake_force_park(us)     force one long quiet period, so the shutdown
 *                              test is guaranteed to run while the sampler
 *                              thread is stuck inside evsrc_read_blocking()
 *  - Event timestamps are forced strictly increasing, so a (timestamp) lookup
 *    identifies an event uniquely and the harness can detect duplicated or
 *    re-ordered pops.
 */
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "event_queue.h"
#include "evsrc.h"

/* ------------------------------------------------------------------ */
/* check plumbing                                                      */
/* ------------------------------------------------------------------ */

static int fails;

static void okf(bool cond, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void okf(bool cond, const char *fmt, ...)
{
    va_list ap;
    if (!cond)
        fails++;
    printf("  [%s] ", cond ? "PASS" : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void info(const char *fmt, ...)
{
    va_list ap;
    printf("         ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

#define U64(x) ((unsigned long long)(x))

/* ------------------------------------------------------------------ */
/* ground truth                                                        */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 32768
static struct Event     truth[TRUTH_MAX];
static int              truth_n;
static bool             truth_overflow;
static pthread_mutex_t  truth_lock = PTHREAD_MUTEX_INITIALIZER;

static void truth_record(const struct Event *e)
{
    pthread_mutex_lock(&truth_lock);
    if (truth_n < TRUTH_MAX)
        truth[truth_n++] = *e;
    else
        truth_overflow = true;
    pthread_mutex_unlock(&truth_lock);
}

static int truth_size(void)
{
    pthread_mutex_lock(&truth_lock);
    int n = truth_n;
    pthread_mutex_unlock(&truth_lock);
    return n;
}

static bool truth_get(int i, struct Event *out)
{
    bool ok = false;
    pthread_mutex_lock(&truth_lock);
    if (i >= 0 && i < truth_n) {
        *out = truth[i];
        ok = true;
    }
    pthread_mutex_unlock(&truth_lock);
    return ok;
}

static bool ev_same(const struct Event *a, const struct Event *b)
{
    return a->type == b->type && a->device_id == b->device_id &&
           a->timestamp == b->timestamp;
}

/* truth[] is append-ordered and timestamps are strictly increasing, so a
 * binary search on the timestamp uniquely identifies an event. */
static int truth_index_of(const struct Event *e)
{
    int found = -1;
    pthread_mutex_lock(&truth_lock);
    int lo = 0, hi = truth_n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (truth[mid].timestamp < e->timestamp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < truth_n && truth[lo].timestamp == e->timestamp && ev_same(&truth[lo], e))
        found = lo;
    pthread_mutex_unlock(&truth_lock);
    return found;
}

/* Brute-force reference for Part 2: O(number of events), which is exactly
 * what the implementation is NOT allowed to do. Same bucket rounding rules
 * as the spec in event_queue.h. */
static uint32_t brute_count(int type, uint64_t since_us, uint64_t now)
{
    if (since_us > now)
        return 0;
    if (type != EV_TYPE_ANY && (type < 0 || type >= EV_TYPE_COUNT))
        return 0;

    uint64_t nowb = now / (uint64_t)EVQ_BUCKET_US;
    uint64_t lob  = since_us / (uint64_t)EVQ_BUCKET_US;
    uint64_t lowest = (nowb + 1 >= (uint64_t)EVQ_NBUCKETS)
                          ? nowb - ((uint64_t)EVQ_NBUCKETS - 1)
                          : 0;
    if (lob < lowest)
        lob = lowest;

    uint32_t c = 0;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n; i++) {
        if (type != EV_TYPE_ANY && truth[i].type != type)
            continue;
        uint64_t b = truth[i].timestamp / (uint64_t)EVQ_BUCKET_US;
        if (b >= lob && b <= nowb)
            c++;
    }
    pthread_mutex_unlock(&truth_lock);
    return c;
}

/* The implementation reads its own ev_now_us() inside the call, so a bucket
 * boundary can fall between our two reads of the clock. Accept either. */
static void check_count(const char *what, int type, uint64_t since_us)
{
    uint64_t n0 = ev_now_us();
    uint32_t got = evq_count_since(type, since_us);
    uint64_t n1 = ev_now_us();
    uint32_t w0 = brute_count(type, since_us, n0);
    uint32_t w1 = brute_count(type, since_us, n1);
    okf(got == w0 || got == w1, "%-34s type=%-2d got %u want %u", what, type, got, w0);
}

/* ------------------------------------------------------------------ */
/* fake vendor event source                                            */
/* ------------------------------------------------------------------ */

static _Atomic bool     v_woken;        /* evsrc_wake() latch                      */
static _Atomic bool     v_paused;       /* harness knob: the site goes quiet       */
static _Atomic uint64_t v_park_until;   /* harness knob: one forced long quiet gap */
static _Atomic uint64_t v_calls;        /* how many times the vendor was entered   */

/* Touched only by the one thread allowed inside evsrc_read_blocking(). */
static uint32_t v_rng = 0x2f6e2b1u;
static int      v_burst_left;
static uint64_t v_last_ts;
static unsigned v_gaps;         /* idle gaps so far; every 6th one errors out */

static uint32_t xs32(void)
{
    uint32_t x = v_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v_rng = x;
    return x;
}

uint64_t ev_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

static void fake_pause(bool on)     { atomic_store(&v_paused, on); }
static void fake_force_park(uint64_t us) { atomic_store(&v_park_until, ev_now_us() + us); }

/* Block for `us`, but park indefinitely while the site is quiet.
 * Returns 0 if the wait completed, -2 if evsrc_wake() was latched.
 *
 * The slicing exists only so that evsrc_wake() can work at all; from the
 * caller's point of view this is one blocking call with no timeout. */
static int v_block(uint64_t us)
{
    const uint64_t SLICE = 500;
    uint64_t left = us;
    for (;;) {
        if (atomic_load(&v_woken))
            return -2;
        if (atomic_load(&v_paused) || ev_now_us() < atomic_load(&v_park_until)) {
            usleep((useconds_t)SLICE);
            continue;
        }
        if (left == 0)
            return 0;
        uint64_t chunk = left < SLICE ? left : SLICE;
        usleep((useconds_t)chunk);
        left -= chunk;
    }
}

int evsrc_read_blocking(struct Event *out)
{
    atomic_fetch_add(&v_calls, 1u);
    if (atomic_load(&v_woken))
        return -2;

    if (v_burst_left == 0) {
        /* Gap between bursts. 80% short, 20% "quiet site". */
        uint64_t gap = (xs32() % 100u < 80u) ? (20000u + xs32() % 40000u)
                                             : (120000u + xs32() % 80000u);
        if (v_block(gap) == -2)
            return -2;
        /* Deterministic, not random: every 6th quiet gap ends in a transient
         * error instead of an event, so the error path is always exercised. */
        if (++v_gaps % 6u == 0u)
            return -1;
        v_burst_left = 3 + (int)(xs32() % 6u);  /* a burst of 3..8 events */
    } else {
        /* Inside a burst: 0.1-0.5 ms apart, several per millisecond. */
        if (v_block(100u + xs32() % 400u) == -2)
            return -2;
    }

    v_burst_left--;

    struct Event e;
    e.type      = (int)(xs32() % (uint32_t)EV_TYPE_COUNT);
    e.device_id = 100u + xs32() % 8u;
    e.timestamp = ev_now_us();
    if (e.timestamp <= v_last_ts)       /* keep stamps strictly increasing */
        e.timestamp = v_last_ts + 1;
    v_last_ts = e.timestamp;

    truth_record(&e);                   /* ground truth BEFORE the caller sees it */
    *out = e;
    return 0;
}

void evsrc_wake(void)
{
    atomic_store(&v_woken, true);
}

/* ------------------------------------------------------------------ */
/* multi-reader stress                                                 */
/* ------------------------------------------------------------------ */

static _Atomic bool          stress_stop;
static _Atomic unsigned char popped_seen[TRUTH_MAX];   /* duplicate detector */

typedef struct {
    uint32_t seed;
    uint64_t calls, pops, bad_event, dup, out_of_order, bad_count, max_us;
} reader_stat_t;

static void *stress_reader(void *arg)
{
    reader_stat_t *st = arg;
    uint32_t rng = st->seed;
    int last_idx = -1;

    while (!atomic_load(&stress_stop)) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;

        struct Event e;
        uint64_t t0 = ev_now_us();
        int rc = (rng & 1u) ? evq_try_pop(&e) : -1;
        uint64_t dt = ev_now_us() - t0;
        if (dt > st->max_us)
            st->max_us = dt;

        if (rc != 0 && !(rng & 1u))
            rc = evq_pop_timed(&e, 2000);       /* not timed: it is meant to wait */

        if (rc == 0) {
            st->pops++;
            int idx = truth_index_of(&e);
            if (idx < 0) {
                st->bad_event++;                /* an event the vendor never produced */
            } else {
                if (atomic_exchange(&popped_seen[idx], 1u) != 0u)
                    st->dup++;                  /* handed to two consumers */
                if (idx <= last_idx)
                    st->out_of_order++;         /* FIFO broken for this reader */
                last_idx = idx;
            }
        }

        int type = (int)(rng >> 8) % (EV_TYPE_COUNT + 1) - 1;   /* -1..3 */
        uint64_t since = ev_now_us() - (uint64_t)((rng >> 16) % 4u) * (uint64_t)EVQ_BUCKET_US;
        t0 = ev_now_us();
        uint32_t c = evq_count_since(type, since);
        dt = ev_now_us() - t0;
        if (dt > st->max_us)
            st->max_us = dt;
        if (c > (uint32_t)truth_size())
            st->bad_count++;                    /* counted more than ever happened */

        st->calls++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* tests                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct Event e;
    struct EvqStats s;
    uint64_t t0, dt;
    int rc;

    printf("== 03_event_tailer_shutdown ==\n");
    printf("   queue capacity %u, buckets %u x %llu us (window %llu us)\n",
           (unsigned)EVQ_CAPACITY, (unsigned)EVQ_NBUCKETS,
           U64(EVQ_BUCKET_US), U64(EVQ_WINDOW_US));

    /* ---- init while the site is quiet: nothing exists yet ---------- */
    printf("\n-- init and empty state (vendor parked, no events at all) --\n");
    fake_pause(true);
    if (evq_init() != 0) {
        printf("evq_init() failed\n");
        return 1;
    }

    t0 = ev_now_us();
    rc = evq_try_pop(&e);
    dt = ev_now_us() - t0;
    okf(rc == -1, "try_pop on an empty queue returns -1 (rc=%d)", rc);
    okf(dt < 20000, "try_pop did not block (%llu us)", U64(dt));

    okf(evq_count_since(EV_TYPE_ANY, 0) == 0, "count_since on empty state is 0");
    okf(evq_count_since(EV_MOTION, 0) == 0, "count_since(MOTION) on empty state is 0");

    evq_stats(&s);
    okf(s.received == 0 && s.popped == 0 && s.dropped == 0 && s.depth == 0,
        "stats all zero at start (recv %llu pop %llu drop %llu depth %u)",
        U64(s.received), U64(s.popped), U64(s.dropped), (unsigned)s.depth);

    t0 = ev_now_us();
    rc = evq_pop_timed(&e, 80000);
    dt = ev_now_us() - t0;
    okf(rc == -1, "pop_timed times out on an empty queue (rc=%d)", rc);
    okf(dt >= 70000 && dt < 400000, "pop_timed waited about 80 ms (%llu us)", U64(dt));

    /* ---- FIFO order and no loss while we keep up ------------------- */
    printf("\n-- FIFO order and content (draining as fast as events arrive) --\n");
    fake_pause(false);

    static struct Event got[512];
    int gn = 0;
    uint64_t end = ev_now_us() + 400000;
    while (ev_now_us() < end && gn < 512) {
        if (evq_pop_timed(&got[gn], 20000) == 0)
            gn++;
    }
    evq_stats(&s);
    okf(gn >= 8, "drained %d events in 400 ms", gn);
    okf(s.dropped == 0, "no drops while the consumer keeps up (dropped %llu)", U64(s.dropped));
    okf(s.popped == (uint64_t)gn, "stats.popped %llu == events we received %d",
        U64(s.popped), gn);
    okf(s.received == s.popped + s.dropped + s.depth,
        "accounting: received %llu == popped %llu + dropped %llu + depth %u",
        U64(s.received), U64(s.popped), U64(s.dropped), (unsigned)s.depth);

    bool prefix_ok = (gn > 0), ts_ok = true;
    for (int i = 0; i < gn; i++) {
        struct Event t;
        if (!truth_get(i, &t) || !ev_same(&t, &got[i]))
            prefix_ok = false;
        if (i > 0 && got[i].timestamp <= got[i - 1].timestamp)
            ts_ok = false;
    }
    okf(prefix_ok, "popped sequence == the vendor's first %d events, in order", gn);
    okf(ts_ok, "timestamps strictly increasing across pops");

    /* ---- drop-oldest policy and the dropped counter ---------------- */
    printf("\n-- drop-oldest policy (consumer stalls for 700 ms) --\n");
    usleep(700 * 1000);                 /* nobody pops: the queue must overflow */
    fake_pause(true);
    usleep(150 * 1000);                 /* let the sampler settle in the park   */

    int produced = truth_size();
    evq_stats(&s);
    uint64_t popped_before = s.popped, dropped_before = s.dropped;
    uint32_t depth_before = s.depth;

    okf(s.dropped > 0, "the queue really overflowed (dropped %llu)", U64(s.dropped));
    okf(s.depth <= EVQ_CAPACITY, "depth %u <= capacity %u",
        (unsigned)s.depth, (unsigned)EVQ_CAPACITY);
    okf(s.received == (uint64_t)produced,
        "received %llu == events the vendor produced %d", U64(s.received), produced);
    okf(s.popped + s.dropped + s.depth == (uint64_t)produced,
        "accounting: popped %llu + dropped %llu + depth %u == produced %d",
        U64(s.popped), U64(s.dropped), (unsigned)s.depth, produced);
    info("errors reported by the vendor so far: %llu", U64(s.errors));

    /* Drop-OLDEST, not drop-newest: what is left must be the newest
     * `depth` events, i.e. truth[popped + dropped .. produced-1]. */
    int drained = 0;
    bool tail_ok = true;
    while (evq_try_pop(&e) == 0) {
        struct Event t;
        int want_idx = (int)(popped_before + dropped_before) + drained;
        if (!truth_get(want_idx, &t) || !ev_same(&t, &e))
            tail_ok = false;
        drained++;
    }
    okf(drained == (int)depth_before, "drained %d events == reported depth %u",
        drained, (unsigned)depth_before);
    okf(tail_ok, "survivors are the NEWEST events (oldest were the ones dropped)");
    okf(evq_try_pop(&e) == -1, "try_pop returns -1 once drained");
    evq_stats(&s);
    okf(s.depth == 0 && s.popped == popped_before + (uint64_t)drained,
        "stats after drain: depth %u, popped %llu", (unsigned)s.depth, U64(s.popped));

    /* ---- pop_timed behaviour -------------------------------------- */
    printf("\n-- pop_timed --\n");
    t0 = ev_now_us();
    rc = evq_pop_timed(&e, 0);
    dt = ev_now_us() - t0;
    okf(rc == -1 && dt < 20000, "pop_timed(0) on empty is a try_pop (rc=%d, %llu us)",
        rc, U64(dt));

    t0 = ev_now_us();
    rc = evq_pop_timed(&e, 150000);
    dt = ev_now_us() - t0;
    okf(rc == -1, "pop_timed(150 ms) times out while the site is quiet (rc=%d)", rc);
    okf(dt >= 135000 && dt < 500000, "timeout accuracy: waited %llu us for 150000",
        U64(dt));

    fake_pause(false);                  /* events start flowing again */
    t0 = ev_now_us();
    rc = evq_pop_timed(&e, 1000000);
    dt = ev_now_us() - t0;
    okf(rc == 0, "pop_timed returns an event once one arrives (rc=%d)", rc);
    okf(dt < 600000, "and returns as soon as it arrives, not at the deadline (%llu us)",
        U64(dt));

    /* ---- Part 2: counts vs brute force ---------------------------- */
    printf("\n-- Part 2: count_since vs brute force over ground truth --\n");
    usleep(300 * 1000);                 /* accumulate a few more bursts */
    fake_pause(true);
    usleep(150 * 1000);

    uint64_t now = ev_now_us();
    info("ground truth holds %d events", truth_size());
    okf(evq_count_since(EV_TYPE_ANY, 0) > 0,
        "count_since sees the events that arrived (nothing recorded == nothing to count)");
    for (int ty = -1; ty < EV_TYPE_COUNT; ty++) {
        check_count("since 0 (whole window)", ty, 0);
        check_count("since now - 200 ms", ty, now - 200000);
        check_count("since now - 1 bucket", ty,
                    now > (uint64_t)EVQ_BUCKET_US ? now - (uint64_t)EVQ_BUCKET_US : 0);
    }

    printf("\n-- Part 2: bucket edge cases --\n");
    now = ev_now_us();
    okf(evq_count_since(EV_TYPE_ANY, now + 5000000ull) == 0, "since in the future is 0");
    check_count("since older than the window", EV_TYPE_ANY,
                now > EVQ_WINDOW_US + 60000000ull ? now - EVQ_WINDOW_US - 60000000ull : 0);
    check_count("since exactly on a bucket edge", EV_TYPE_ANY,
                (now / (uint64_t)EVQ_BUCKET_US) * (uint64_t)EVQ_BUCKET_US);
    check_count("since one us before that edge", EV_TYPE_ANY,
                (now / (uint64_t)EVQ_BUCKET_US) * (uint64_t)EVQ_BUCKET_US - 1);
    check_count("since now", EV_TYPE_ANY, now);
    okf(evq_count_since(99, 0) == 0, "unknown type is 0, not a wild read");
    okf(evq_count_since(EV_TYPE_COUNT, 0) == 0, "type == EV_TYPE_COUNT is 0");

    {   /* O(buckets), not O(events): the cost must not depend on `since`. */
        uint64_t worst = 0, total = 0;
        const int N = 4000;
        for (int i = 0; i < N; i++) {
            uint64_t a = ev_now_us();
            (void)evq_count_since(EV_TYPE_ANY, i & 1 ? 0 : ev_now_us() - 200000);
            uint64_t d = ev_now_us() - a;
            total += d;
            if (d > worst)
                worst = d;
        }
        okf(worst < 20000, "count_since worst case %llu us over %d calls (mean %llu us)",
            U64(worst), N, U64(total / (uint64_t)N));
    }

    /* ---- multi-reader stress -------------------------------------- */
    printf("\n-- 3 readers for 1.2 s while the sampler writes --\n");
    fake_pause(false);
    for (int i = 0; i < TRUTH_MAX; i++)
        atomic_store(&popped_seen[i], 0u);

    pthread_t th[3];
    reader_stat_t st[3];
    memset(st, 0, sizeof st);
    atomic_store(&stress_stop, false);
    for (int i = 0; i < 3; i++) {
        st[i].seed = 0x9e3779b9u + 0x1000u * (uint32_t)(i + 1);
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    }
    usleep(1200 * 1000);
    atomic_store(&stress_stop, true);

    uint64_t calls = 0, pops = 0, bad = 0, dup = 0, oo = 0, badc = 0, max_us = 0;
    for (int i = 0; i < 3; i++) {
        pthread_join(th[i], NULL);
        calls += st[i].calls;
        pops += st[i].pops;
        bad += st[i].bad_event;
        dup += st[i].dup;
        oo += st[i].out_of_order;
        badc += st[i].bad_count;
        if (st[i].max_us > max_us)
            max_us = st[i].max_us;
    }
    info("calls %llu, pops %llu", U64(calls), U64(pops));
    okf(calls > 0 && pops > 0, "readers made progress");
    okf(bad == 0, "no invented events (%llu)", U64(bad));
    okf(dup == 0, "no event delivered twice (%llu)", U64(dup));
    okf(oo == 0, "each reader saw events in FIFO order (%llu violations)", U64(oo));
    okf(badc == 0, "no count larger than the number of events produced (%llu)", U64(badc));
    okf(max_us < 50000,
        "worst non-blocking getter latency %llu us (vendor blocks without bound)",
        U64(max_us));

    evq_stats(&s);
    okf(s.errors > 0, "transient vendor errors were seen and counted (%llu)",
        U64(s.errors));
    okf(s.received == s.popped + s.dropped + s.depth,
        "accounting still holds after the stress: %llu == %llu + %llu + %u",
        U64(s.received), U64(s.popped), U64(s.dropped), (unsigned)s.depth);

    /* ---- the point of this problem: shutdown while parked ---------- */
    printf("\n-- graceful shutdown while the sampler is inside the vendor call --\n");
    fake_force_park(2500000);           /* 2.5 s of silence, starting now */
    usleep(250 * 1000);                 /* the sampler is now stuck in evsrc_read_blocking */

    int produced_before = truth_size();
    uint64_t calls_before = atomic_load(&v_calls);
    t0 = ev_now_us();
    evq_deinit();
    dt = ev_now_us() - t0;
    okf(dt < 400000,
        "evq_deinit() returned in %llu us while the vendor call was parked (bound 400000)",
        U64(dt));
    info("the vendor had no event to give for another %llu us",
         U64(2500000 - 250000));

    uint64_t calls_at_exit = atomic_load(&v_calls);
    usleep(200 * 1000);
    int produced_after = truth_size();
    okf(produced_after == produced_before,
        "no events produced after deinit: the sampler thread is gone (%d -> %d)",
        produced_before, produced_after);
    okf(atomic_load(&v_calls) == calls_at_exit,
        "nobody entered evsrc_read_blocking() after deinit returned "
        "(%llu calls, %llu during the parked wait)",
        U64(calls_at_exit), U64(calls_at_exit - calls_before));

    okf(evq_try_pop(&e) == -1, "try_pop after deinit returns -1");
    t0 = ev_now_us();
    rc = evq_pop_timed(&e, 100000);
    dt = ev_now_us() - t0;
    okf(rc == -1 && dt < 300000, "pop_timed after deinit returns -1 quickly (%llu us)",
        U64(dt));

    t0 = ev_now_us();
    evq_deinit();                       /* must be idempotent */
    dt = ev_now_us() - t0;
    okf(dt < 50000, "second evq_deinit() is a no-op (%llu us)", U64(dt));

    okf(!truth_overflow, "harness ground-truth buffer did not overflow");

    if (fails)
        printf("\n%d CHECK(S) FAILED\n", fails);
    else
        printf("\nALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}
