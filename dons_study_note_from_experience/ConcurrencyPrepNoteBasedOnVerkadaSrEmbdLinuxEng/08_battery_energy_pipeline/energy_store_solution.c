/* energy_store_solution.c — reference solution.
 *
 *   ./main.sh sol     build main.c + this file and run the checks
 *
 * ---------------------------------------------------------------------------
 * WHY THIS SHAPE  (the paragraph to say out loud in the interview)
 *
 * 1. bms_read() blocks and is not thread-safe, so exactly ONE thread ever
 *    touches it: the sampler (stage 1). Everything a caller sees is a copy of
 *    state the sampler published, so no caller ever waits on the BMS.
 *
 * 2. THE SAMPLER MUST NEVER BE BLOCKED BY THE UPLOADER. The BMS reports on its
 *    own irregular schedule; the samples it produces are only available in the
 *    window between one report and the next. If stage 1 ever waited for stage 2
 *    - a full queue, a rate-limit token, a slow network call - the sampler
 *    would not be sitting in bms_read() when the BMS reports, and the reading
 *    is simply gone: unlike a file, a sensor cannot be re-read later. So the
 *    hand-off from stage 1 to stage 2 is a BOUNDED queue with a DROP-OLDEST
 *    policy: pushing is O(1), never waits, and can never fail. Uploads are
 *    best-effort telemetry; the 30-minute local history is the source of truth
 *    for energy, and it is written before the queue push, so a dropped upload
 *    never costs us an energy sample.
 *
 * 3. RATE LIMIT WITHOUT BURSTS AND WITHOUT STARVATION: a token bucket driven by
 *    pthread_cond_timedwait(). The bucket holds at most UPLOAD_BURST tokens and
 *    refills continuously at UPLOAD_RATE_PER_SEC, so the number of uploads in
 *    any one-second window is at most burst + rate - that is the anti-burst
 *    half, and it is provable rather than measured. The anti-starvation half is
 *    the TIMED wait: the uploader does not spin and does not sleep for a fixed
 *    slice, it waits for exactly as long as the bucket needs to hold one token
 *    and no longer, and the same condvar is signalled when work arrives or when
 *    we are shutting down. A plain pthread_cond_wait() cannot express "wake me
 *    when either new work arrives OR enough time has passed"; a usleep() loop
 *    cannot be woken early, so deinit would have to wait out a whole interval.
 *
 * 4. Locks: three small ones, each held for a few instructions and NEVER across
 *    a blocking vendor call. latest_lock (publish/read one struct), hist_lock
 *    (append/query the history), q_lock (queue + token bucket). Separate locks
 *    mean a throttled uploader sitting in its timed wait on q_lock cannot delay
 *    a reader in energy_joules_between(), and cannot delay the sampler's
 *    publish either. A seqlock or a double buffer would also work for "latest";
 *    a mutex is enough here because the writer is one thread holding it for a
 *    16-byte copy.
 *
 * 5. Part 2 = ring buffer of (timestamp, power) + PREFIX SUMS of trapezoidal
 *    energy. Append is O(1) (one add), an arbitrary range query is O(log n):
 *    binary search for the segment holding t0 and the one holding t1, subtract
 *    two prefix sums for everything in between, and interpolate the two partial
 *    edge slivers exactly. Fixed memory, no allocation on the sampling path.
 * ------------------------------------------------------------------------- */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "bms.h"
#include "energy_store.h"

/* ------------------------------------------------------------------ */
/* counters                                                            */
/* ------------------------------------------------------------------ */

static _Atomic uint64_t c_sampled, c_uploaded, c_dropped, c_batches;
static _Atomic uint64_t c_wait_us, c_max_publish_us, c_stale, c_errors;

/* ------------------------------------------------------------------ */
/* stage 1 output A: the latest sample (Part 1)                         */
/* ------------------------------------------------------------------ */

static pthread_mutex_t latest_lock = PTHREAD_MUTEX_INITIALIZER;
static struct BmsSample latest;
static bool latest_valid;        /* guarded by latest_lock */

/* ------------------------------------------------------------------ */
/* stage 1 output B: 30-minute history + prefix sums (Part 2)           */
/* ------------------------------------------------------------------ */

/* Capacity: the BMS can report as often as every ~20 ms, so 30 min needs
 * 1800 s / 0.020 s = 90 000 entries. Round up to a power of two so the ring
 * index is a mask instead of a modulo: 2^17 = 131 072 entries * 24 B = 3.1 MiB
 * of BSS, allocated once at start-up. No malloc on the sampling path. */
#define HIST_CAP   (1u << 17)
#define HIST_MASK  (HIST_CAP - 1u)

struct hist_entry {
    uint64_t ts;    /* microseconds                                          */
    double   p;     /* instantaneous power, watts = volts * amps              */
    double   cum;   /* energy in joules from the very first sample up to ts   */
};

static pthread_mutex_t hist_lock = PTHREAD_MUTEX_INITIALIZER;
static struct hist_entry hist[HIST_CAP];
static unsigned hist_head;    /* index of the OLDEST entry */
static unsigned hist_count;

static struct hist_entry *H(unsigned i) { return &hist[(hist_head + i) & HIST_MASK]; }

/* ------------------------------------------------------------------ */
/* stage 1 -> stage 2: bounded queue + token bucket                     */
/* ------------------------------------------------------------------ */

static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_cv   = PTHREAD_COND_INITIALIZER;
static struct BmsSample q[UPLOAD_QUEUE_CAP];
static unsigned q_head, q_count;      /* guarded by q_lock */
static double   tokens;               /* guarded by q_lock */
static uint64_t tokens_last_us;       /* guarded by q_lock */

static _Atomic bool running;
static pthread_t sampler_th, uploader_th;
static bool threads_up;               /* only touched by init/deinit (one thread) */

/* one token every TOKEN_INTERVAL_US microseconds */
#define TOKEN_INTERVAL_US (1000000ull / UPLOAD_RATE_PER_SEC)

/* ------------------------------------------------------------------ */
/* portable relative timed wait (SPEC §6: no pthread_condattr_setclock) */
/* ------------------------------------------------------------------ */

static void cond_wait_us(pthread_cond_t *cv, pthread_mutex_t *m, uint64_t us)
{
#ifdef __APPLE__
    /* macOS has no condattr clock selection, but it does have a relative wait,
     * which is what we actually want and is immune to wall-clock jumps. */
    struct timespec rel;
    rel.tv_sec  = (time_t)(us / 1000000ull);
    rel.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    pthread_cond_timedwait_relative_np(cv, m, &rel);
#else
    /* A default condvar waits on CLOCK_REALTIME, the same clock gettimeofday()
     * reads, so build the absolute deadline from gettimeofday(). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t deadline = 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec + us;
    struct timespec abs;
    abs.tv_sec  = (time_t)(deadline / 1000000ull);
    abs.tv_nsec = (long)((deadline % 1000000ull) * 1000ull);
    pthread_cond_timedwait(cv, m, &abs);
#endif
}

/* ------------------------------------------------------------------ */
/* history append (called by the sampler only)                          */
/* ------------------------------------------------------------------ */

/* THE TRAPEZOID, derived.
 *
 * The BMS gives us power only at the instants it reports: p(t_i) = V_i * I_i.
 * Energy is the integral of power: E = integral p(t) dt. Between two reports we
 * have no information, so we assume power moves linearly from p_i to p_{i+1}
 * (the cheapest assumption that is continuous and exact when the load ramps).
 * The integral of a straight line over [t_i, t_{i+1}] is the area of a
 * trapezoid with parallel sides p_i and p_{i+1} and width dt:
 *
 *     E_i = (p_i + p_{i+1}) / 2 * dt          [ watts * seconds = joules ]
 *
 * dt must be in SECONDS, so divide the microsecond difference by 1e6.
 * (Zero-order hold - "power stayed at p_i" - would be E_i = p_i * dt and is
 * what you want if the sensor reports on change only; the BMS here reports
 * samples of a continuously varying load, so trapezoid is the better model and
 * it is second-order accurate instead of first-order.)
 *
 * PREFIX SUMS AND PRECISION.
 * cum_i = sum of E_0..E_{i-1}, i.e. energy since the first sample ever, so any
 * range's energy is one subtraction: cum_j - cum_i. That subtraction is exactly
 * where precision bites - cum grows forever while the differences we want stay
 * small, which is textbook catastrophic cancellation:
 *   - float has a 24-bit mantissa: once cum passes ~1.7e7 J (about 5 kWh, i.e.
 *     a few hours on this trailer) the spacing between representable values is
 *     larger than 1 J and short windows return garbage. Never accumulate energy
 *     in a float.
 *   - double has a 53-bit mantissa: at 1e12 J the spacing is still ~2e-4 J, so
 *     for a trailer that is fine for centuries. That is why cum is a double
 *     even though the samples arrive as floats.
 *   - if that were not enough, the alternatives are (a) Kahan/Neumaier
 *     compensated summation, which carries the rounding error in a second
 *     variable and buys back ~2 digits per add, or (b) rebasing: when the ring
 *     wraps, subtract cum[head] from every stored cum so the running total
 *     never grows without bound. Both are strictly more code; the reason we do
 *     not need them is the 30-minute window, which bounds the interesting range
 *     while cum only has to stay accurate in its own units.
 */
static void hist_append(const struct BmsSample *s)
{
    double p = (double)s->volts * (double)s->amps;

    pthread_mutex_lock(&hist_lock);

    /* The ring must stay sorted or the binary search is meaningless. The BMS
     * timestamps come from a wall clock, which can step backwards (NTP): a
     * sample that does not move time forward is dropped rather than inserted
     * out of order. With a monotonic clock this branch never fires. */
    if (hist_count > 0 && s->timestamp <= H(hist_count - 1)->ts) {
        pthread_mutex_unlock(&hist_lock);
        return;
    }

    double cum = 0.0;
    if (hist_count > 0) {
        struct hist_entry *prev = H(hist_count - 1);
        double dt_s = (double)(s->timestamp - prev->ts) / 1e6;
        cum = prev->cum + 0.5 * (prev->p + p) * dt_s;      /* the trapezoid */
    }

    if (hist_count == HIST_CAP) {          /* full: overwrite the oldest */
        hist_head = (hist_head + 1u) & HIST_MASK;
        hist_count--;
    }
    struct hist_entry *e = H(hist_count);
    e->ts = s->timestamp;
    e->p = p;
    e->cum = cum;
    hist_count++;

    /* Evict everything older than the window, but KEEP ONE sample older than
     * the cut: the power at the window's edge is interpolated between the last
     * sample before the edge and the first one after it, so throwing that
     * older sample away would silently truncate the oldest part of every query.
     * Condition: drop entry 0 only while entry 1 is still <= the cutoff. */
    if (s->timestamp > ENERGY_WINDOW_US) {
        uint64_t cutoff = s->timestamp - ENERGY_WINDOW_US;
        while (hist_count >= 2 && H(1)->ts <= cutoff) {
            hist_head = (hist_head + 1u) & HIST_MASK;
            hist_count--;
        }
    }
    pthread_mutex_unlock(&hist_lock);
}

/* ------------------------------------------------------------------ */
/* stage 1: the sampler                                                */
/* ------------------------------------------------------------------ */

static void publish(const struct BmsSample *s)
{
    uint64_t t0 = bms_now_us();

    /* (a) Part 1: latest wins immediately - this is the freshest thing callers
     *     can see and it must not depend on the uploader at all. */
    pthread_mutex_lock(&latest_lock);
    latest = *s;
    latest_valid = true;
    pthread_mutex_unlock(&latest_lock);

    /* (b) Part 2: the local history. Written BEFORE the upload queue, so the
     *     energy integral is complete even when uploads are being dropped. */
    hist_append(s);

    /* (c) count the sample BEFORE handing it to stage 2. Order matters: a
     *     reader that sees "dropped" or "uploaded" must never see a smaller
     *     "sampled", or the invariant sampled >= uploaded + dropped breaks. */
    atomic_fetch_add(&c_sampled, 1);

    /* (d) stage 2 hand-off: bounded, drop-oldest, never waits. */
    pthread_mutex_lock(&q_lock);
    if (q_count == UPLOAD_QUEUE_CAP) {
        q_head = (q_head + 1u) % UPLOAD_QUEUE_CAP;   /* drop the oldest */
        q_count--;
        atomic_fetch_add(&c_dropped, 1);
    }
    q[(q_head + q_count) % UPLOAD_QUEUE_CAP] = *s;
    q_count++;
    pthread_cond_signal(&q_cv);          /* wake the uploader if it is idle */
    pthread_mutex_unlock(&q_lock);

    /* evidence for the interviewer: how long stage 1 was away from the BMS */
    uint64_t d = bms_now_us() - t0;
    if (d > atomic_load(&c_max_publish_us))
        atomic_store(&c_max_publish_us, d);   /* single writer: no CAS needed */
}

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load(&running)) {
        struct BmsSample s = bms_read();          /* the only caller, ever */
        /* Publish even if deinit flipped the flag while we were parked in the
         * vendor call: that reading cost a full BMS interval and it is the
         * newest thing anyone will ever know about the pack. The loop condition
         * is what ends the thread, not a mid-loop bail-out that throws data
         * away. (Nothing downstream minds: the queue push cannot block, and the
         * uploader is only joined after this thread is.) */
        if (s.status == BMS_OK) {
            publish(&s);
        } else if (s.status == BMS_STALE) {
            /* nothing changed: volts/amps/timestamp are NOT valid, so touching
             * them would publish garbage. The previous sample still stands. */
            atomic_fetch_add(&c_stale, 1);
        } else {
            /* BMS_ERROR: every field is garbage and it returned instantly, so
             * a tight retry loop would spin a core. Back off a little. */
            atomic_fetch_add(&c_errors, 1);
            usleep(2000);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* stage 2: the rate-limited uploader                                  */
/* ------------------------------------------------------------------ */

/* Continuous refill: tokens += elapsed * rate, capped at the bucket size.
 * Capping is what makes it a *bucket*: an uploader that was idle for a minute
 * may not then fire sixty uploads back to back. */
static void bucket_refill_locked(uint64_t now)
{
    if (now > tokens_last_us) {
        tokens += (double)(now - tokens_last_us) * (double)UPLOAD_RATE_PER_SEC / 1e6;
        if (tokens > (double)UPLOAD_BURST) tokens = (double)UPLOAD_BURST;
        tokens_last_us = now;
    }
}

static void *uploader_main(void *arg)
{
    (void)arg;
    struct BmsSample batch[UPLOAD_BATCH_MAX];

    pthread_mutex_lock(&q_lock);
    while (atomic_load(&running)) {

        /* (1) nothing to do: plain wait, no timeout - do not burn a token or a
         *     timer when the queue is empty. */
        if (q_count == 0) {
            pthread_cond_wait(&q_cv, &q_lock);
            continue;
        }

        /* (2) work is waiting: buy a token. This is the throttle, and it is a
         *     TIMED wait so that (a) we sleep exactly until the bucket has a
         *     token instead of polling, and (b) deinit can wake us instantly
         *     with a broadcast instead of us waiting the interval out. */
        uint64_t wait_t0 = bms_now_us();
        for (;;) {
            bucket_refill_locked(bms_now_us());
            if (tokens >= 1.0) break;
            uint64_t need = (uint64_t)((1.0 - tokens) * (double)TOKEN_INTERVAL_US) + 1;
            cond_wait_us(&q_cv, &q_lock, need);
            if (!atomic_load(&running)) break;
        }
        if (!atomic_load(&running)) break;
        uint64_t waited = bms_now_us() - wait_t0;
        tokens -= 1.0;

        /* (3) take a batch out of the queue while we still hold the lock */
        int n = 0;
        while (n < UPLOAD_BATCH_MAX && q_count > 0) {
            batch[n++] = q[q_head];
            q_head = (q_head + 1u) % UPLOAD_QUEUE_CAP;
            q_count--;
        }
        pthread_mutex_unlock(&q_lock);

        /* (4) the network call goes OUTSIDE the lock. bms_upload() blocks for
         *     milliseconds; holding q_lock across it would make the sampler's
         *     push - and therefore the BMS reading - wait on the network. */
        atomic_fetch_add(&c_wait_us, waited);
        if (n > 0 && bms_upload(batch, n) == 0) {
            atomic_fetch_add(&c_uploaded, (uint64_t)n);
            atomic_fetch_add(&c_batches, 1);
        }

        pthread_mutex_lock(&q_lock);
    }
    pthread_mutex_unlock(&q_lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int bms_pipeline_init(void)
{
    pthread_mutex_lock(&q_lock);
    q_head = q_count = 0;
    tokens = (double)UPLOAD_BURST;        /* start full: one burst is allowed */
    tokens_last_us = bms_now_us();
    pthread_mutex_unlock(&q_lock);

    atomic_store(&running, true);

    /* The vendor's FIRST call returns immediately, so take it here, on the
     * caller's thread, before any thread exists. A getter called one
     * microsecond after init then already has a real value instead of -1, and
     * we did not have to block to get it. (Two threads never touch bms_read():
     * this call happens-before pthread_create.) */
    struct BmsSample s = bms_read();
    if (s.status == BMS_OK) publish(&s);
    else if (s.status == BMS_STALE) atomic_fetch_add(&c_stale, 1);
    else atomic_fetch_add(&c_errors, 1);

    if (pthread_create(&sampler_th, NULL, sampler_main, NULL) != 0) {
        atomic_store(&running, false);
        return -1;
    }
    if (pthread_create(&uploader_th, NULL, uploader_main, NULL) != 0) {
        atomic_store(&running, false);
        pthread_cond_broadcast(&q_cv);
        pthread_join(sampler_th, NULL);
        return -1;
    }
    threads_up = true;
    return 0;
}

void bms_pipeline_deinit(void)
{
    if (!threads_up) return;
    threads_up = false;

    atomic_store(&running, false);

    /* Wake the uploader wherever it is: idle in pthread_cond_wait(), or parked
     * in the token bucket's timed wait. Without this broadcast deinit would
     * block for up to one full token interval. The signal has to be sent with
     * the lock held, otherwise the uploader can re-check `running` (still true),
     * decide to wait, and only then miss the wakeup - the classic lost-wakeup
     * race between "set the flag" and "signal the condvar". */
    pthread_mutex_lock(&q_lock);
    pthread_cond_broadcast(&q_cv);
    pthread_mutex_unlock(&q_lock);

    /* The sampler may be parked inside bms_read() for up to one BMS interval;
     * nothing can shorten that, which is exactly why the vendor call lives on
     * its own thread and not in a getter. */
    pthread_join(sampler_th, NULL);
    pthread_join(uploader_th, NULL);

    /* History and counters are deliberately kept: energy_joules_between() still
     * answers after shutdown, and the counters still explain what happened. */
}

/* ------------------------------------------------------------------ */
/* Part 1 getter                                                       */
/* ------------------------------------------------------------------ */

int bms_get_latest(struct BmsSample *out)
{
    if (out == NULL) return -1;
    int rc = -1;
    pthread_mutex_lock(&latest_lock);
    if (latest_valid) {
        *out = latest;          /* one struct copy: readers never see a mix of
                                 * two samples, which a volatile struct or
                                 * field-by-field copying would not guarantee */
        rc = 0;
    }
    pthread_mutex_unlock(&latest_lock);
    return rc;
}

struct BmsStats bms_stats(void)
{
    struct BmsStats s;
    /* Eight independent atomic loads are not one atomic snapshot, so the READ
     * ORDER is chosen to keep the invariant that matters true: read the things
     * that only ever go up *after* `sampled` does first, and read `sampled`
     * last. Then sampled is at least as new as uploaded+dropped and
     * "sampled >= uploaded + dropped" can never appear violated. */
    s.uploaded       = atomic_load(&c_uploaded);
    s.dropped        = atomic_load(&c_dropped);
    s.batches        = atomic_load(&c_batches);
    s.upload_wait_us = atomic_load(&c_wait_us);
    s.max_publish_us = atomic_load(&c_max_publish_us);
    s.stale          = atomic_load(&c_stale);
    s.errors         = atomic_load(&c_errors);
    s.sampled        = atomic_load(&c_sampled);
    return s;
}

/* ------------------------------------------------------------------ */
/* Part 2: energy over an arbitrary interval                           */
/* ------------------------------------------------------------------ */

/* largest i with H(i)->ts <= t, assuming H(0)->ts <= t. Caller holds hist_lock.
 * Plain lower-bound binary search: O(log n) instead of walking 90 000 samples,
 * which is the whole reason the ring is kept sorted. */
static unsigned floor_index(uint64_t t)
{
    unsigned lo = 0, hi = hist_count - 1;      /* invariant: H(lo)->ts <= t */
    while (lo < hi) {
        unsigned mid = lo + (hi - lo + 1u) / 2u;    /* bias up: lo may equal hi-1 */
        if (H(mid)->ts <= t) lo = mid;
        else hi = mid - 1u;
    }
    return lo;
}

/* power at t inside segment [i, i+1], linearly interpolated. Caller holds the
 * lock and guarantees i + 1 < hist_count. */
static double p_at(unsigned i, uint64_t t)
{
    struct hist_entry *a = H(i), *b = H(i + 1);
    if (t <= a->ts) return a->p;
    if (t >= b->ts) return b->p;
    return a->p + (b->p - a->p) * ((double)(t - a->ts) / (double)(b->ts - a->ts));
}

double energy_joules_between(uint64_t t0, uint64_t t1)
{
    if (t1 <= t0) return 0.0;              /* empty or inverted range */

    pthread_mutex_lock(&hist_lock);

    if (hist_count < 2) {                  /* one point has no interval */
        pthread_mutex_unlock(&hist_lock);
        return 0.0;
    }

    uint64_t first = H(0)->ts, last = H(hist_count - 1)->ts;
    /* Clamp to what we actually measured. Energy before the oldest retained
     * sample or after the newest one is unknown, and inventing it (e.g. holding
     * the last power forever) would quietly turn a gap into a number. */
    if (t0 < first) t0 = first;
    if (t1 > last)  t1 = last;
    if (t1 <= t0) {                        /* range was entirely outside */
        pthread_mutex_unlock(&hist_lock);
        return 0.0;
    }

    /* t0 < t1 <= last, so t0 < last, so i <= hist_count - 2: segment [i, i+1]
     * exists and contains t0. Same for j with t1. */
    unsigned i = floor_index(t0);
    unsigned j = floor_index(t1);

    double e;
    if (i == j) {
        /* BOTH ENDS IN ONE SEGMENT. There is no whole segment to sum and no
         * prefix sum to subtract: the answer is a single trapezoid whose two
         * parallel sides are the INTERPOLATED powers at t0 and t1. Getting this
         * case wrong (e.g. returning cum_j - cum_i = 0) is the classic bug:
         * short queries would silently return zero. */
        e = 0.5 * (p_at(i, t0) + p_at(i, t1)) * ((double)(t1 - t0) / 1e6);
    } else {
        /* head sliver: from t0 to the next real sample */
        struct hist_entry *ip1 = H(i + 1);
        double head = 0.5 * (p_at(i, t0) + ip1->p)
                    * ((double)(ip1->ts - t0) / 1e6);

        /* everything whole in between, in one subtraction: O(1) */
        double mid = H(j)->cum - ip1->cum;

        /* tail sliver: from the last sample at or before t1 up to t1. Zero when
         * t1 landed exactly on a sample (including t1 == last, where segment
         * [j, j+1] does not exist - hence the guard). */
        double tail = 0.0;
        struct hist_entry *jp = H(j);
        if (t1 > jp->ts && j + 1 < hist_count)
            tail = 0.5 * (jp->p + p_at(j, t1)) * ((double)(t1 - jp->ts) / 1e6);

        e = head + mid + tail;
    }

    pthread_mutex_unlock(&hist_lock);
    return e;
}
