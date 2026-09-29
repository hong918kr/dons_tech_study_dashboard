/* temp_cache_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol
 *
 * Why this shape
 *   Part 1 is NOT "one sampler thread forever". The probe is read on demand,
 *   so the thing to get right is DEMAND COALESCING ("single flight"):
 *   mutex + condvar + an `inflight` flag + a generation counter. One caller
 *   becomes the leader, drops the mutex, does the slow vendor read, then
 *   publishes and broadcasts; everyone who arrived meanwhile took the
 *   waiter path and consumes the leader's result. The mutex is NEVER held
 *   across tempdev_read() — holding it would serialise 8 callers into 8
 *   sequential 60 ms reads, which is both slow and the classic mistake.
 *   An atomic/seqlock cannot replace the condvar here because waiters must
 *   SLEEP until a specific event (this refresh finished), not just read a
 *   value; the generation counter is what makes "finished" unambiguous and
 *   immune to spurious wakeups.
 *
 *   Part 2 is a fixed grid of 60 one-minute buckets, each carrying
 *   min/max/sum/count, plus a ring of the raw samples so the two PARTIAL
 *   buckets at the ends of a range are exact. A query folds whole buckets in
 *   O(1) each (<= 60 of them) and scans raw samples only inside the two edge
 *   minutes, so cost is bounded by the WINDOW, never by the sample count.
 *
 * Failure policy (documented, because the harness checks it)
 *   A waiter consumes the result of the refresh it waited on and returns.
 *   If that refresh NACKed it gets TEMP_STALE with the last good value (or
 *   TEMP_NODATA if there never was one). It does NOT promote itself to
 *   leader and start another read — otherwise 8 waiters on a wedged bus
 *   would turn into 8 more vendor reads, i.e. a retry storm. Retrying is
 *   the caller's decision, one read per call.
 */
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tempdev.h"
#include "temp_cache.h"

/* ------------------------------------------------------------------ */
/* Part 1 state                                                        */
/* ------------------------------------------------------------------ */

static struct {
    pthread_mutex_t lock;
    pthread_cond_t  cv;

    float    value;      /* last good reading                              */
    uint64_t stamp;      /* when it was taken (us, tempdev_now_us clock)    */
    bool     have;       /* a reading ever succeeded                        */

    bool     inflight;   /* somebody is inside tempdev_read() right now     */
    uint64_t gen;        /* ++ every time a refresh COMPLETES (ok or not)   */
    bool     last_ok;    /* did the most recently completed refresh work?   */

    bool     up;         /* between init and deinit                        */

    /* counters, all mutated under g.lock so there is nothing to race on   */
    uint64_t st_vendor_calls, st_hits, st_leaders, st_coalesced, st_nacks;
} g = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cv   = PTHREAD_COND_INITIALIZER,
};

/* ------------------------------------------------------------------ */
/* Part 2 state                                                        */
/* ------------------------------------------------------------------ */

/* Raw sample ring. Sized for the worst rate we promise to keep exactly at
 * the range edges: 16384 samples / 3600 s ~= 4.5 Hz, 16 B each = 256 KiB.
 * Beyond that rate the oldest raw samples get overwritten before they age
 * out; the per-minute AGGREGATES are never lost, so min/max/avg of whole
 * minutes stay exact and only the two partial edge minutes degrade. Those
 * queries are counted in stats.raw_lost instead of being silently wrong. */
#define RAW_CAP 16384u
_Static_assert((RAW_CAP & (RAW_CAP - 1u)) == 0u, "RAW_CAP must be a power of 2");
#define RAW_MASK (RAW_CAP - 1u)

typedef struct {
    uint64_t ts;
    float    c;
} sample_t;

typedef struct {
    uint64_t minute;   /* ABSOLUTE minute index this slot describes.        */
                       /* Index is minute % TEMP_NBUCKETS, so a slot from   */
                       /* an earlier hour is recognised by minute != m and  */
                       /* needs no separate eviction pass.                  */
    uint64_t first;    /* free-running raw index of its first sample        */
    uint32_t count;
    float    min, max;
    double   sum;      /* double: 3600 s of floats would lose bits in float */
} bucket_t;

static struct {
    pthread_mutex_t lock;
    sample_t raw[RAW_CAP];
    uint64_t head;             /* free-running: next raw slot to write      */
    bucket_t b[TEMP_NBUCKETS];
    uint64_t newest_minute;
    uint64_t last_ts;
    bool     any;
    /* stats */
    uint64_t range_queries, last_scanned, raw_lost;
} h = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int temp_cache_init(void)
{
    pthread_mutex_lock(&g.lock);
    if (g.up) {
        pthread_mutex_unlock(&g.lock);
        return 0;
    }
    g.value = NAN;
    g.stamp = 0;
    g.have = false;
    g.inflight = false;
    g.gen = 0;
    g.last_ok = false;
    g.up = true;
    g.st_vendor_calls = g.st_hits = g.st_leaders = g.st_coalesced = g.st_nacks = 0;
    pthread_mutex_unlock(&g.lock);

    /* Deliberately no device access here: the whole point of this API is
     * that the bus is touched only when somebody actually wants a value. */

    pthread_mutex_lock(&h.lock);
    memset(h.b, 0, sizeof h.b);
    for (unsigned i = 0; i < TEMP_NBUCKETS; i++)
        h.b[i].minute = UINT64_MAX;          /* UINT64_MAX == "never used"  */
    h.head = 0;
    h.newest_minute = 0;
    h.last_ts = 0;
    h.any = false;
    h.range_queries = h.last_scanned = h.raw_lost = 0;
    pthread_mutex_unlock(&h.lock);

    return 0;
}

void temp_cache_deinit(void)
{
    pthread_mutex_lock(&g.lock);
    if (!g.up) {
        pthread_mutex_unlock(&g.lock);
        return;
    }
    g.up = false;
    pthread_cond_broadcast(&g.cv);           /* release parked waiters      */

    /* A leader is somewhere inside the vendor read; it still owns the bus.
     * Wait for it, or we would return while the device is mid-transaction. */
    while (g.inflight)
        pthread_cond_wait(&g.cv, &g.lock);

    g.have = false;
    g.value = NAN;
    pthread_mutex_unlock(&g.lock);
}

/* ------------------------------------------------------------------ */
/* Part 1 — single flight                                              */
/* ------------------------------------------------------------------ */

/* Called with g.lock held. "fresher than max_age_us" is strict, so
 * max_age_us == 0 always means "refresh". */
static bool fresh_enough(uint64_t max_age_us)
{
    if (!g.have)
        return false;
    uint64_t now = tempdev_now_us();
    if (now < g.stamp)          /* clock stepped back: treat as fresh, the  */
        return true;            /* sample cannot be older than "now"        */
    return (now - g.stamp) < max_age_us;
}

int temp_get(float *out, uint64_t max_age_us)
{
    if (out == NULL)
        return TEMP_NODATA;

    pthread_mutex_lock(&g.lock);

    if (!g.up) {
        pthread_mutex_unlock(&g.lock);
        return TEMP_NODATA;
    }

    /* (1) cache hit — the common case, and it must not touch the bus. */
    if (fresh_enough(max_age_us)) {
        *out = g.value;
        g.st_hits++;
        pthread_mutex_unlock(&g.lock);
        return TEMP_OK;
    }

    /* (2) a refresh is already running — ride along instead of starting a
     *     second one. Remember WHICH refresh we are waiting for: the
     *     generation counter turns "it finished" into a plain comparison,
     *     so spurious wakeups and a second refresh racing past us are both
     *     harmless. */
    if (g.inflight) {
        uint64_t seen = g.gen;
        g.st_coalesced++;
        while (g.gen == seen && g.up)
            pthread_cond_wait(&g.cv, &g.lock);

        int rc;
        if (g.have && g.last_ok) {           /* the read we waited on worked */
            *out = g.value;
            rc = TEMP_OK;
        } else if (g.have) {                 /* it NACKed: hand back stale   */
            *out = g.value;
            rc = TEMP_STALE;
        } else {
            rc = TEMP_NODATA;                /* never had a good reading     */
        }
        pthread_mutex_unlock(&g.lock);
        return rc;
    }

    /* (3) nobody is reading: I am the leader. Claim the slot, then DROP THE
     *     MUTEX before the slow call. Holding it here is the classic bug —
     *     it would serialise every caller behind a 60 ms bus transaction and
     *     also block the cheap cache-hit path. */
    g.inflight = true;
    g.st_leaders++;
    g.st_vendor_calls++;
    pthread_mutex_unlock(&g.lock);

    float v = 0.0f;
    int rc = tempdev_read(&v);
    uint64_t t = tempdev_now_us();

    pthread_mutex_lock(&g.lock);
    if (rc == 0) {
        g.value   = v;
        g.stamp   = t;
        g.have    = true;
        g.last_ok = true;
    } else {
        g.last_ok = false;
        g.st_nacks++;
    }
    g.inflight = false;
    g.gen++;                                 /* publish BEFORE broadcasting */
    pthread_cond_broadcast(&g.cv);

    int ret;
    if (rc == 0) {
        *out = g.value;
        ret = TEMP_OK;
    } else if (g.have) {
        *out = g.value;
        ret = TEMP_STALE;
    } else {
        ret = TEMP_NODATA;
    }
    pthread_mutex_unlock(&g.lock);

    /* History is a different lock; take it AFTER releasing g.lock so the two
     * are never nested and there is no lock-order to get wrong. */
    if (rc == 0)
        temp_history_add(t, v);

    return ret;
}

/* ------------------------------------------------------------------ */
/* Part 2 — bucketed history                                           */
/* ------------------------------------------------------------------ */

int temp_history_add(uint64_t ts, float c)
{
    if (isnan(c))
        return -1;

    pthread_mutex_lock(&h.lock);

    /* The clock is gettimeofday: NTP can step it backwards. Clamp so the
     * ring stays sorted — a bucket scan and the minute index both rely on
     * non-decreasing timestamps. */
    if (h.any && ts < h.last_ts)
        ts = h.last_ts;

    uint64_t m = ts / TEMP_BUCKET_US;
    bucket_t *b = &h.b[m % TEMP_NBUCKETS];

    if (b->minute != m) {                    /* slot belongs to an older    */
        b->minute = m;                       /* hour (or was never used):   */
        b->first  = h.head;                  /* recycle it. That IS the     */
        b->count  = 0;                       /* eviction — no pass needed.  */
        b->sum    = 0.0;
        b->min = b->max = c;
    }

    h.raw[h.head & RAW_MASK] = (sample_t){ .ts = ts, .c = c };
    h.head++;

    if (b->count == 0) {
        b->min = b->max = c;
    } else {
        if (c < b->min) b->min = c;
        if (c > b->max) b->max = c;
    }
    b->count++;
    b->sum += (double)c;

    if (!h.any || m > h.newest_minute)
        h.newest_minute = m;
    h.last_ts = ts;
    h.any = true;

    pthread_mutex_unlock(&h.lock);
    return 0;
}

int temp_range_stats(uint64_t t0, uint64_t t1, float *out_min, float *out_max,
                     float *out_avg)
{
    if (t0 > t1)
        return -1;

    pthread_mutex_lock(&h.lock);
    if (!h.any) {
        pthread_mutex_unlock(&h.lock);
        return -1;
    }

    /* Clamp to the window FIRST. Without this, t0 = 0 would make the loop
     * below walk ~28 million minute indices. */
    uint64_t newest = h.newest_minute;
    uint64_t oldest = (newest >= TEMP_NBUCKETS - 1u) ? newest - (TEMP_NBUCKETS - 1u) : 0;
    uint64_t m0 = t0 / TEMP_BUCKET_US;
    uint64_t m1 = t1 / TEMP_BUCKET_US;
    if (m1 < oldest || m0 > newest) {        /* entirely outside history     */
        h.range_queries++;
        h.last_scanned = 0;
        pthread_mutex_unlock(&h.lock);
        return -1;
    }
    if (m0 < oldest) m0 = oldest;
    if (m1 > newest) m1 = newest;

    uint64_t avail_from = (h.head > RAW_CAP) ? h.head - RAW_CAP : 0;
    uint64_t n = 0, scanned = 0;
    bool lost = false;
    double sum = 0.0;
    float lo = 0.0f, hi = 0.0f;

    for (uint64_t m = m0; m <= m1; m++) {    /* at most TEMP_NBUCKETS steps  */
        bucket_t *b = &h.b[m % TEMP_NBUCKETS];
        if (b->minute != m || b->count == 0)
            continue;

        uint64_t bs = m * TEMP_BUCKET_US;
        uint64_t be = bs + TEMP_BUCKET_US - 1u;

        if (t0 <= bs && be <= t1) {
            /* whole minute inside the range: O(1), this is the whole point */
            if (n == 0) { lo = b->min; hi = b->max; }
            else {
                if (b->min < lo) lo = b->min;
                if (b->max > hi) hi = b->max;
            }
            n   += b->count;
            sum += b->sum;
            continue;
        }

        /* partial minute (only ever the first and/or last of the range):
         * walk this bucket's raw samples, bounded by one minute of data. */
        uint64_t from = b->first;
        uint64_t to   = b->first + b->count;
        if (from < avail_from) {             /* overwritten: degraded, said so */
            from = avail_from;
            lost = true;
        }
        for (uint64_t i = from; i < to; i++) {
            const sample_t *s = &h.raw[i & RAW_MASK];
            scanned++;
            if (s->ts < t0 || s->ts > t1)
                continue;
            if (n == 0) { lo = s->c; hi = s->c; }
            else {
                if (s->c < lo) lo = s->c;
                if (s->c > hi) hi = s->c;
            }
            n++;
            sum += (double)s->c;
        }
    }

    h.range_queries++;
    h.last_scanned = scanned;
    if (lost) h.raw_lost++;
    pthread_mutex_unlock(&h.lock);

    if (n == 0)
        return -1;
    if (out_min) *out_min = lo;
    if (out_max) *out_max = hi;
    if (out_avg) *out_avg = (float)(sum / (double)n);
    return 0;
}

/* Why buckets, and when the alternatives win
 *
 * (a) scan all samples: one array, O(1) append, zero bookkeeping, and the
 *     query is O(n) — at 4.5 Hz that is 16k floats per query while a mutex
 *     is held. Right answer when queries are RARE compared to appends, or
 *     when the window is small. Wrong here: a dashboard polls min/max far
 *     more often than the probe produces samples, and the lock hold time
 *     grows with history length.
 *
 * (b) sparse table / segment tree over the samples: O(1) (sparse) or
 *     O(log n) (segment) range min/max regardless of range width. But a
 *     sparse table is built on an IMMUTABLE array — it must be rebuilt
 *     O(n log n) after every append, so it fits offline analysis, not a
 *     live ring. A segment tree does support O(log n) append, but it costs
 *     2n nodes, cannot carry a mean without extra sums, and eviction from
 *     the front means either rebuilding or a tombstoned tree. Worth it when
 *     the window is huge and queries need arbitrary sub-ranges exactly.
 *
 * (c) fixed buckets (this file): O(1) append, O(window/bucket + samples in
 *     two edge buckets) query, fixed memory, and eviction is free because a
 *     slot is recycled the moment its absolute minute changes. The cost is
 *     resolution: exactness at the edges only survives while the raw ring
 *     still holds those minutes. This is what real telemetry does (RRDtool,
 *     Prometheus recording rules, Graphite whisper) for exactly this shape:
 *     high-rate appends, bounded window, coarse range queries.
 */

/* ------------------------------------------------------------------ */
/* stats                                                               */
/* ------------------------------------------------------------------ */

void temp_cache_stats(temp_stats_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);

    pthread_mutex_lock(&g.lock);
    out->vendor_calls = g.st_vendor_calls;
    out->hits         = g.st_hits;
    out->leaders      = g.st_leaders;
    out->coalesced    = g.st_coalesced;
    out->nacks        = g.st_nacks;
    pthread_mutex_unlock(&g.lock);

    /* Two locks, taken one after the other, never nested. */
    pthread_mutex_lock(&h.lock);
    out->range_queries = h.range_queries;
    out->last_scanned  = h.last_scanned;
    out->raw_lost      = h.raw_lost;
    pthread_mutex_unlock(&h.lock);
}
