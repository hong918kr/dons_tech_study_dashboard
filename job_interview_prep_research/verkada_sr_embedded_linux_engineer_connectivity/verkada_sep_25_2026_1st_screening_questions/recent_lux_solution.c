/* recent_lux_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol       normal run
 *   ./main.sh window    same code with a 2-second window to exercise eviction
 *
 * Shape
 *   one sampler thread  ── the ONLY caller of read_next_sample() (it blocks,
 *                          and the vendor code is not thread-safe: static
 *                          first_run + rand()).
 *        │ VALID
 *        ├──► g_latest_bits  (Part 1)  _Atomic uint32_t holding the float's bits.
 *        │                             Readers: one atomic load. Wait-free.
 *        └──► g_hist         (Part 2)  fixed ring of (ts, lux) sorted by ts,
 *                                      guarded by a mutex held for O(1)/O(log n).
 *   any number of reader threads call the getters; none of them can ever wait
 *   on the sensor, only (Part 2) on a lock held for well under a microsecond.
 *
 * Details and alternatives: solutions_with_opus.md
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "als.h"
#include "recent_lux.h"

#ifndef LUX_WINDOW_US
#define LUX_WINDOW_US (10ull * 60ull * 1000000ull)   /* 10 minutes */
#endif

/* Worst case we size for: a real ALS integrates for >= 100 ms -> <= 10 Hz ->
 * 6000 samples / 10 min. 8192 * 16 B = 128 KiB. This fake averages ~0.67 Hz. */
#ifndef LUX_HIST_CAP
#define LUX_HIST_CAP 8192u
#endif
_Static_assert((LUX_HIST_CAP & (LUX_HIST_CAP - 1u)) == 0u, "LUX_HIST_CAP must be a power of 2");
#define HIST_MASK (LUX_HIST_CAP - 1u)

#define ERROR_BACKOFF_US 10000u

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t ts;   /* us, get_timestamp() clock */
    float    lux;
} lux_sample_t;

/* Part 1: IEEE-754 bits of the latest lux. 0x7FC00000 = quiet NaN = "none yet". */
static _Atomic uint32_t g_latest_bits = 0x7FC00000u;

/* Part 2: ring buffer, oldest at tail, newest at head-1, ts non-decreasing. */
static struct {
    pthread_mutex_t lock;
    lux_sample_t    buf[LUX_HIST_CAP];
    uint32_t        head;       /* free-running: next slot to write      */
    uint32_t        tail;       /* free-running: oldest valid slot       */
    uint32_t        overflow;   /* samples dropped because buf was full  */
} g_hist = {.lock = PTHREAD_MUTEX_INITIALIZER};

static _Atomic uint32_t g_errors;
static _Atomic bool     g_running;
static bool             g_started;          /* only touched by init/deinit */
static pthread_t        g_thread;

/* ------------------------------------------------------------------ */
/* writer side (sampler thread only)                                   */
/* ------------------------------------------------------------------ */

static void publish_latest(float lux)
{
    uint32_t bits;
    memcpy(&bits, &lux, sizeof bits);      /* type-pun without UB */
    atomic_store_explicit(&g_latest_bits, bits, memory_order_release);
}

static void hist_append(uint64_t ts, float lux)
{
    uint64_t now = get_timestamp();

    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;

    /* Binary search needs sorted ts; gettimeofday can step backwards (NTP). */
    if (n > 0) {
        uint64_t last = g_hist.buf[(g_hist.head - 1u) & HIST_MASK].ts;
        if (ts < last)
            ts = last;
    }

    /* Evict the oldest sample only if the NEXT one is also outside the window:
     * the value in effect at (now - window) comes from a sample older than the
     * window, and a query at the window's edge still needs it. */
    while (n >= 2) {
        uint64_t next_ts = g_hist.buf[(g_hist.tail + 1u) & HIST_MASK].ts;
        if (next_ts > now || now - next_ts < LUX_WINDOW_US)
            break;
        g_hist.tail++;
        n--;
    }

    if (n == LUX_HIST_CAP) {                /* full even after eviction: sensor too chatty */
        g_hist.tail++;
        g_hist.overflow++;
    }

    g_hist.buf[g_hist.head & HIST_MASK] = (lux_sample_t){.ts = ts, .lux = lux};
    g_hist.head++;
    pthread_mutex_unlock(&g_hist.lock);
}

static void handle_reading(const SensorReading *r)
{
    switch (r->status) {
    case VALID:
        publish_latest(r->lux);     /* Part 1 first: cheapest, most readers care */
        hist_append(r->timestamp, r->lux);
        break;
    case NO_CHANGE:
        break;                      /* the stored value is still the truth */
    default:                        /* ERROR: lux/timestamp are garbage */
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);   /* ERROR returns instantly; don't spin */
        break;
    }
}

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        SensorReading r = read_next_sample();
        handle_reading(&r);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int init_recent_lux(void)
{
    if (g_started)
        return 0;

    /* First call never blocks: take it here so readers have a value the moment
     * init returns. Retry a few times in case it came back ERROR. */
    for (int tries = 0; tries < 3; tries++) {
        SensorReading r = read_next_sample();
        handle_reading(&r);
        if (r.status == VALID)
            break;
    }

    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;
    }
    g_started = true;
    return 0;
}

void deinit_recent_lux(void)
{
    if (!g_started)
        return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    pthread_join(g_thread, NULL);           /* up to ~1 s: the sensor call must return */
    g_started = false;
}

/* ------------------------------------------------------------------ */
/* Part 1                                                              */
/* ------------------------------------------------------------------ */

float get_most_recent_lux(void)
{
    uint32_t bits = atomic_load_explicit(&g_latest_bits, memory_order_acquire);
    float lux;
    memcpy(&lux, &bits, sizeof lux);
    return lux;
}

/* ------------------------------------------------------------------ */
/* Part 2                                                              */
/* ------------------------------------------------------------------ */

float get_lux_at(uint64_t t)
{
    uint64_t now = get_timestamp();
    if (t > now || now - t > LUX_WINDOW_US)
        return NAN;

    float result = NAN;
    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;

    /* upper_bound on offsets [0, n): first sample with ts > t.
     * Offsets from tail, not raw indices, so uint32 wrap of head/tail is harmless. */
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (g_hist.buf[(g_hist.tail + mid) & HIST_MASK].ts <= t)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo > 0)                              /* lo == 0: t is before the first sample we hold */
        result = g_hist.buf[(g_hist.tail + lo - 1u) & HIST_MASK].lux;
    pthread_mutex_unlock(&g_hist.lock);
    return result;
}
