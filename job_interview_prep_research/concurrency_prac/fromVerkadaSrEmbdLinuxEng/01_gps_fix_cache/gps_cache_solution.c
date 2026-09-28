/* gps_cache_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol            normal run  (10-minute window)
 *   ./main.sh window sol     1.5-second window, exercises eviction
 *
 * Shape
 *   one sampler thread  ── the ONLY caller of gps_wait_for_fix(): it blocks up
 *                          to 100 ms and keeps static state, so it must be
 *                          single-threaded.
 *        │ GPS_VALID
 *        ├──► g_pub   (Part 1) seqlock-published struct GpsFix
 *        └──► g_hist  (Part 2) fixed ring of fixes + PREFIX SUM of segment
 *                             metres, under a mutex held for O(log n)
 *
 * ---------------------------------------------------------------------------
 * Part 1: why NOT one atomic, and why a seqlock rather than a mutex
 *
 * struct GpsFix is 40 bytes.  There is no 40-byte atomic store, and a
 * `_Atomic struct GpsFix` would silently become a compiler-managed lock (check
 * atomic_is_lock_free), so the choice is a real lock or a versioning scheme.
 * Whatever we pick, the invariant is: a reader must get lat, lon, hdop and
 * timestamp *from the same fix* -- never last second's lat with this second's
 * lon, which would put a geofence breach in the wrong county.
 *
 *   A MUTEX SNAPSHOT IS THE RIGHT DEFAULT.  lock / memcpy / unlock is ~10
 *   lines, obviously correct, needs no fence reasoning, and at one write per
 *   ~75 ms the contention is nil.  If I had 20 minutes on a whiteboard that is
 *   what I would ship, and I would only revisit it with a profile in hand.
 *
 *   I chose a SEQLOCK here because of the read/write asymmetry this product
 *   has: one writer at ~10 Hz, many readers polling much faster, and the
 *   writer must never be made to wait behind a reader.  With a mutex, a reader
 *   descheduled while holding the lock stalls the sampler thread, and on an RT
 *   kernel that is straight-up priority inversion -- the GPS falls behind
 *   because the web UI thread got preempted.  A seqlock makes the WRITER
 *   wait-free and never lets a reader block anything; readers pay only a retry
 *   when they happen to land inside an update.
 *
 *   Price of the seqlock, said out loud: readers can in principle spin (a
 *   writer storm starves them -- fine at 10 Hz, not fine at 10 MHz); every
 *   field must be an atomic or the torn read a reader is *allowed* to perform
 *   is UB and ThreadSanitizer will flag it; and it only works because the
 *   payload is small, copyable and side-effect free, so re-reading is cheap.
 *
 * Part 2: ring buffer + binary search + prefix sums
 *
 * Fixed-size ring: bounded memory on an embedded box, O(1) append and evict,
 * and timestamps are non-decreasing so it is a sorted array -> binary search
 * gives O(log n) for "fix in effect at t".
 *
 * Distance needs more than the ring.  Summing segments over the range is O(n)
 * -- 6000 fixes per dashboard poll per trailer.  So each slot also carries
 * `cum`, the running total of metres from the first fix ever recorded.  Then
 *   distance(t0, t1) = cum[last fix <= t1] - cum[first fix >= t0]
 * which is two binary searches and one subtraction: O(log n).
 * `cum` is absolute (never rebased), so evicting the tail costs nothing --
 * only differences are ever read.
 *
 * Eviction keeps ONE fix older than the window: the position in effect at
 * (now - window) comes from a fix taken before the window opened, and a query
 * at the window's edge still needs it.
 */
/* glibc hides usleep()/gettimeofday() when -std=c11 defines __STRICT_ANSI__. */
#define _DEFAULT_SOURCE 1

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "gps.h"
#include "gps_cache.h"

#define DEG2RAD   0.017453292519943295
#define EARTH_R_M 6371008.8

/* A real receiver tops out at 10 Hz -> 6000 fixes in 10 minutes.  Round up to
 * a power of two so the index wrap is a mask: 8192 * 48 B = 384 KiB. */
#ifndef GPS_HIST_CAP
#define GPS_HIST_CAP 8192u
#endif
_Static_assert((GPS_HIST_CAP & (GPS_HIST_CAP - 1u)) == 0u,
               "GPS_HIST_CAP must be a power of two");
#define HIST_MASK (GPS_HIST_CAP - 1u)

/* GPS_ERROR comes back instantly; without a back-off the sampler would spin. */
#define ERROR_BACKOFF_US 5000u

/* ------------------------------------------------------------------ */
/* Part 1 state: seqlock-published fix                                 */
/* ------------------------------------------------------------------ */

/* Every field is an _Atomic because a seqlock reader is *expected* to read
 * fields while the writer is mid-update and then throw the result away.  With
 * plain scalars that read is a data race = undefined behaviour, and TSan says
 * so.  Relaxed atomics make it defined and generate the same instructions;
 * the two fences below are what actually orders things. */
static _Atomic uint32_t g_seq;              /* even = stable, odd = writing */
static _Atomic int      g_status = GPS_NO_FIX;   /* GPS_VALID once we have one */
static _Atomic double   g_lat, g_lon;
static _Atomic float    g_hdop;
static _Atomic uint64_t g_ts;

static void publish_fix(const struct GpsFix *f)
{
    /* Single writer, so a plain load of the sequence is enough. */
    uint32_t s = atomic_load_explicit(&g_seq, memory_order_relaxed);

    atomic_store_explicit(&g_seq, s + 1u, memory_order_relaxed);   /* -> odd  */
    atomic_thread_fence(memory_order_release);   /* odd seq visible BEFORE fields */

    atomic_store_explicit(&g_status, f->status,    memory_order_relaxed);
    atomic_store_explicit(&g_lat,    f->lat,       memory_order_relaxed);
    atomic_store_explicit(&g_lon,    f->lon,       memory_order_relaxed);
    atomic_store_explicit(&g_hdop,   f->hdop,      memory_order_relaxed);
    atomic_store_explicit(&g_ts,     f->timestamp, memory_order_relaxed);

    atomic_thread_fence(memory_order_release);   /* fields visible BEFORE even seq */
    atomic_store_explicit(&g_seq, s + 2u, memory_order_relaxed);   /* -> even */
}

/* ------------------------------------------------------------------ */
/* Part 2 state: ring + prefix sums                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    struct GpsFix fix;
    double        cum;    /* metres from the first fix ever recorded, to here */
} hist_entry_t;

static struct {
    pthread_mutex_t lock;
    hist_entry_t    buf[GPS_HIST_CAP];
    uint32_t        head;       /* free-running index of the next slot to write */
    uint32_t        tail;       /* free-running index of the oldest live slot   */
    uint32_t        overflow;   /* fixes dropped because the ring was full      */
} g_hist = { .lock = PTHREAD_MUTEX_INITIALIZER };

static _Atomic uint32_t g_errors;
static _Atomic bool     g_running;
static bool             g_started;      /* init/deinit only, single-threaded */
static pthread_t        g_thread;

/* Equirectangular ("flat earth") distance.  Good to well under a metre over
 * the few km a trailer moves, it is 1 cos + 1 sqrt instead of haversine's
 * trig soup, and it is deterministic across libms because no atan2 is
 * involved.  Documented in gps_cache.h; the harness uses the same formula. */
static double segment_m(const struct GpsFix *a, const struct GpsFix *b)
{
    double dlat = (b->lat - a->lat) * DEG2RAD;
    double dlon = (b->lon - a->lon) * DEG2RAD;
    double latm = (a->lat + b->lat) * 0.5 * DEG2RAD;
    double x    = dlon * cos(latm);
    return EARTH_R_M * sqrt(dlat * dlat + x * x);
}

static void hist_append(const struct GpsFix *in)
{
    struct GpsFix f = *in;
    uint64_t now = gps_now_us();

    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;   /* unsigned: wrap-safe */

    /* Binary search needs a sorted array, and gps_now_us() is wall clock --
     * NTP on the trailer's modem can step it backwards.  Clamp so the array
     * stays non-decreasing rather than corrupting every future lookup. */
    if (n > 0) {
        uint64_t last = g_hist.buf[(g_hist.head - 1u) & HIST_MASK].fix.timestamp;
        if (f.timestamp < last)
            f.timestamp = last;
    }

    /* Drop the oldest only while the NEXT one is also outside the window, so
     * exactly one pre-window fix survives to answer edge queries. */
    while (n >= 2) {
        uint64_t next_ts = g_hist.buf[(g_hist.tail + 1u) & HIST_MASK].fix.timestamp;
        if (next_ts > now || now - next_ts < GPS_WINDOW_US)
            break;
        g_hist.tail++;
        n--;
    }

    if (n == GPS_HIST_CAP) {       /* still full: the module is faster than we sized for */
        g_hist.tail++;
        g_hist.overflow++;
        n--;
    }

    double cum = 0.0;
    if (n > 0) {
        const hist_entry_t *prev = &g_hist.buf[(g_hist.head - 1u) & HIST_MASK];
        cum = prev->cum + segment_m(&prev->fix, &f);
    }
    g_hist.buf[g_hist.head & HIST_MASK].fix = f;
    g_hist.buf[g_hist.head & HIST_MASK].cum = cum;
    g_hist.head++;
    pthread_mutex_unlock(&g_hist.lock);
}

/* ------------------------------------------------------------------ */
/* sampler thread                                                      */
/* ------------------------------------------------------------------ */

static void handle_fix(const struct GpsFix *f)
{
    switch (f->status) {
    case GPS_VALID:
        publish_fix(f);      /* Part 1 first: cheapest, and most callers want it */
        hist_append(f);
        break;
    case GPS_NO_FIX:
        break;               /* not moving / sky blocked: the old fix still holds */
    default:                 /* GPS_ERROR: every field is garbage, touch none of them */
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);
        break;
    }
}

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        struct GpsFix f = gps_wait_for_fix();
        handle_fix(&f);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int gps_cache_init(void)
{
    if (g_started)
        return 0;

    /* The first vendor call returns immediately, so take it on this thread:
     * a caller that polls right after init already has a position instead of
     * waiting out a receiver timeout.  No overlap with the sampler thread --
     * it does not exist yet, which is the only reason this is safe. */
    struct GpsFix f = gps_wait_for_fix();
    handle_fix(&f);

    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;
    }
    g_started = true;
    return 0;
}

void gps_cache_deinit(void)
{
    if (!g_started)
        return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    /* The thread may be parked inside gps_wait_for_fix() for up to one
     * receiver timeout; there is no way to cancel it safely, so we wait. */
    pthread_join(g_thread, NULL);
    g_started = false;
    /* State is left intact on purpose: the getters stay valid after deinit. */
}

/* ------------------------------------------------------------------ */
/* Part 1                                                              */
/* ------------------------------------------------------------------ */

int gps_get_last_fix(struct GpsFix *out)
{
    if (out == NULL)
        return -1;

    struct GpsFix f;
    for (;;) {
        uint32_t s1 = atomic_load_explicit(&g_seq, memory_order_acquire);
        if (s1 & 1u)                  /* writer mid-update: nothing to copy yet */
            continue;

        f.status    = atomic_load_explicit(&g_status, memory_order_relaxed);
        f.lat       = atomic_load_explicit(&g_lat,    memory_order_relaxed);
        f.lon       = atomic_load_explicit(&g_lon,    memory_order_relaxed);
        f.hdop      = atomic_load_explicit(&g_hdop,   memory_order_relaxed);
        f.timestamp = atomic_load_explicit(&g_ts,     memory_order_relaxed);

        /* Field loads must not be reordered after this re-check, or the
         * "sequence unchanged" conclusion would be about the wrong values. */
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&g_seq, memory_order_relaxed) == s1)
            break;                    /* no update overlapped: f is one whole fix */
    }

    if (f.status != GPS_VALID)        /* nothing valid has ever been published */
        return -1;
    *out = f;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Part 2                                                              */
/* ------------------------------------------------------------------ */

/* Offsets from tail, not raw indices, so the uint32 wrap of head/tail is
 * invisible to the search. */
static const hist_entry_t *hist_at(uint32_t off)
{
    return &g_hist.buf[(g_hist.tail + off) & HIST_MASK];
}

/* first offset in [0, n) with timestamp >= t  (n if none) */
static uint32_t hist_lower_bound(uint32_t n, uint64_t t)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (hist_at(mid)->fix.timestamp < t) lo = mid + 1u;
        else                                 hi = mid;
    }
    return lo;
}

/* number of offsets in [0, n) with timestamp <= t; minus one is the last such */
static uint32_t hist_upper_bound(uint32_t n, uint64_t t)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (hist_at(mid)->fix.timestamp <= t) lo = mid + 1u;
        else                                  hi = mid;
    }
    return lo;
}

int gps_fix_at(uint64_t t, struct GpsFix *out)
{
    if (out == NULL)
        return -1;

    uint64_t now = gps_now_us();
    if (t > now)                       /* we do not know the future */
        return -1;
    if (now - t > GPS_WINDOW_US)       /* outside the window we promise to keep */
        return -1;

    int rc = -1;
    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;
    uint32_t k = hist_upper_bound(n, t);
    if (k > 0) {                       /* k == 0: t precedes every fix we hold */
        *out = hist_at(k - 1u)->fix;
        rc = 0;
    }
    pthread_mutex_unlock(&g_hist.lock);
    return rc;
}

double gps_distance_travelled(uint64_t t0, uint64_t t1)
{
    uint64_t now = gps_now_us();
    if (t0 > t1)                       /* caller bug, not an empty range */
        return NAN;
    if (t1 > now)
        return NAN;
    if (now - t0 > GPS_WINDOW_US)      /* we no longer hold the start of the range */
        return NAN;
    if (t0 == t1)
        return 0.0;

    double d = 0.0;
    pthread_mutex_lock(&g_hist.lock);
    uint32_t n  = g_hist.head - g_hist.tail;
    uint32_t k0 = hist_lower_bound(n, t0);          /* first fix at or after t0 */
    uint32_t k1 = hist_upper_bound(n, t1);          /* one past the last fix <= t1 */
    /* Needs at least two fixes inside the range to have travelled anything.
     * Prefix sums are absolute, so one subtraction answers any range. */
    if (k1 > k0 + 1u)
        d = hist_at(k1 - 1u)->cum - hist_at(k0)->cum;
    pthread_mutex_unlock(&g_hist.lock);
    return d;
}
