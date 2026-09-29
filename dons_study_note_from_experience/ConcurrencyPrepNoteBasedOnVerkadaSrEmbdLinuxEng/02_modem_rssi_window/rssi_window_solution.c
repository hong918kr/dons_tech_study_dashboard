/* rssi_window_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol
 *
 * Shape
 *   one sampler thread ── the ONLY caller of modem_poll_rssi() (it blocks up to
 *                         100 ms and keeps static state, so it must be one
 *                         thread and it must not be a thread a caller owns).
 *        │ MODEM_OK
 *        ├──► g_pub   (Part 1)  ONE _Atomic uint64_t = 48-bit timestamp
 *        │                      (us since init) | 16-bit signed rssi_dbm.
 *        └──► g_win   (Part 2)  ring of (ts, rssi) + two monotonic deques,
 *                               guarded by a mutex held for amortised O(1).
 *
 * WHY PACK VALUE AND TIMESTAMP INTO ONE ATOMIC (Part 1)
 *   The contract is "value and its age must describe the same sample". With two
 *   separate variables (an atomic int and an atomic uint64) a reader can load
 *   the value, get preempted, and load a timestamp belonging to the NEXT sample:
 *   a stale -113 dBm then looks 0 ms old, which is exactly the failure this API
 *   exists to prevent (the LED shows full bars while the radio is dead, or the
 *   failover logic trusts a nine-minute-old number). Publishing both halves in
 *   ONE atomic store makes the pair indivisible: the reader does a single
 *   64-bit load, so it is wait-free, needs no lock, and cannot tear.
 *   Budget: 16 bits is plenty for dBm (-32768..32767 covers -115..-55 with room
 *   to spare) and 48 bits of microseconds since init is 8.9 years of uptime.
 *   If the payload ever grew past 64 bits (say rssi + sinr + band + ts) the
 *   packing stops working and the next tool is a SEQLOCK — writer bumps an even
 *   sequence to odd, writes the struct, bumps to even; reader retries while the
 *   sequence is odd or changed. Same wait-free-ish reader, no mutex, but the
 *   reader can spin, so it is strictly more expensive. A mutex would also be
 *   correct here and is ~20 ns, but then a reader can be descheduled holding it
 *   and every LED tick waits on it; an atomic load cannot be blocked at all.
 *
 * WHY MONOTONIC DEQUES (Part 2)
 *   min/max over a sliding window by scanning is O(n) per query. n is not small:
 *   the modem can produce a value every 10 ms, so a 60 s window is 6000 samples,
 *   and the installer UI asks for min and max several times a second while the
 *   sampler appends — that is millions of comparisons per minute inside a lock
 *   that the radio task also needs. The standard fix is one deque per statistic
 *   holding sample INDICES, kept monotonic: for the minimum, values along the
 *   deque are non-decreasing. A new sample smaller than the back of the deque
 *   deletes it permanently — a larger, OLDER sample can never be the minimum
 *   again, because it expires no later than the new one. The front is therefore
 *   always the answer; expiry pops the front. Each sample is pushed once and
 *   popped at most once, so a query is amortised O(1) and the worst case is a
 *   burst of pops that the appends already paid for.
 *   The average is a running int64 sum / count, also O(1). Because rssi_dbm is
 *   an integer the running sum is EXACT; if the samples were floats (like the
 *   ALS lux problem) an incrementally updated sum would accumulate rounding
 *   error that never washes out — add/subtract of unequal magnitudes is lossy —
 *   and the fixes are periodic recomputation from the ring or Kahan summation.
 *
 * Eviction happens both on append AND inside every getter: samples age out even
 * when the modem has stopped talking, and that is precisely the case the
 * staleness API exists for. Unlike the ALS "value in effect at time t" problem
 * there is no keep-one-older rule here: a sample outside the window is simply
 * not part of the window's statistics.
 */
/* glibc hides usleep()/gettimeofday() when -std=c11 defines __STRICT_ANSI__.
 * Harmless on macOS. Must come before any #include. */
#define _DEFAULT_SOURCE 1

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "modem.h"
#include "rssi_window.h"

/* Capacity: worst case the modem hands us a new value every 10 ms -> 100 Hz.
 * 60 s * 100 Hz = 6000 -> 8192 slots. 8192*16 B ring + 2*8192*4 B deques
 * = 192 KiB of BSS, fine on a gateway, and nothing is ever malloc'd. */
#ifndef RSSI_CAP
#define RSSI_CAP 8192u
#endif
_Static_assert((RSSI_CAP & (RSSI_CAP - 1u)) == 0u, "RSSI_CAP must be a power of two");
#define CAP_MASK (RSSI_CAP - 1u)

#define ERROR_BACKOFF_US 5000u

/* ------------------------------------------------------------------ */
/* Part 1 state: the packed publication word                           */
/* ------------------------------------------------------------------ */

#define PUB_NONE 0ull                /* rel is >= 1 for real samples, see below */
#define PUB_RSSI_BITS 16
#define PUB_RSSI_MASK 0xFFFFull

static _Atomic uint64_t g_pub = PUB_NONE;
static _Atomic uint64_t g_t0;         /* us; timestamps are published relative to it */

static void publish(uint64_t ts, int rssi_dbm)
{
    uint64_t t0 = atomic_load_explicit(&g_t0, memory_order_relaxed);
    uint64_t rel = (ts > t0) ? (ts - t0) : 0;
    rel += 1;                                     /* so a real sample is never 0 */
    if (rel > (UINT64_MAX >> PUB_RSSI_BITS))      /* 8.9 years of uptime; clamp   */
        rel = UINT64_MAX >> PUB_RSSI_BITS;

    uint64_t word = (rel << PUB_RSSI_BITS) | (uint64_t)(uint16_t)(int16_t)rssi_dbm;
    /* release: the ring/deque writes done before this store are visible to any
     * reader that observes the new word (not strictly needed for Part 1, which
     * is self-contained, but it makes g_t0 safe to read after a non-NONE load). */
    atomic_store_explicit(&g_pub, word, memory_order_release);
}

/* ------------------------------------------------------------------ */
/* Part 2 state: ring + two monotonic deques                           */
/* ------------------------------------------------------------------ */

typedef struct { uint64_t ts; int rssi; } sample_t;

static struct {
    pthread_mutex_t lock;
    sample_t s[RSSI_CAP];
    uint32_t head, tail;          /* free-running; count = head - tail        */
    uint32_t minq[RSSI_CAP];      /* sample indices, rssi non-decreasing      */
    uint32_t min_f, min_b;
    uint32_t maxq[RSSI_CAP];      /* sample indices, rssi non-increasing      */
    uint32_t max_f, max_b;
    int64_t  sum;                 /* exact: rssi_dbm is an integer            */
    uint32_t n_overflow;
} g_win = { .lock = PTHREAD_MUTEX_INITIALIZER };

static _Atomic uint32_t g_errors;
static _Atomic bool     g_running;
static bool             g_started;    /* only ever touched by init/deinit */
static pthread_t        g_thread;

/* all of the helpers below run with g_win.lock held */

static uint32_t win_count(void) { return g_win.head - g_win.tail; }

static void drop_oldest(void)
{
    uint32_t idx = g_win.tail;
    g_win.sum -= g_win.s[idx & CAP_MASK].rssi;
    g_win.tail++;
    /* deque indices are strictly increasing and all >= tail, so only the front
     * can be the sample we just dropped. */
    if (g_win.min_f != g_win.min_b && g_win.minq[g_win.min_f & CAP_MASK] == idx)
        g_win.min_f++;
    if (g_win.max_f != g_win.max_b && g_win.maxq[g_win.max_f & CAP_MASK] == idx)
        g_win.max_f++;
}

/* Amortised O(1): each sample can be dropped once, ever. */
static void evict_expired(uint64_t now)
{
    while (g_win.head != g_win.tail) {
        uint64_t ts = g_win.s[g_win.tail & CAP_MASK].ts;
        if (ts > now || now - ts <= RSSI_WINDOW_US)
            break;
        drop_oldest();
    }
}

static void win_append(uint64_t ts, int rssi)
{
    uint64_t now = modem_now_us();

    pthread_mutex_lock(&g_win.lock);

    /* gettimeofday can step backwards (NTP). The ring must stay sorted by ts or
     * eviction from the front stops being correct, so clamp. */
    if (win_count() > 0) {
        uint64_t last = g_win.s[(g_win.head - 1u) & CAP_MASK].ts;
        if (ts < last)
            ts = last;
    }

    evict_expired(now);

    if (win_count() == RSSI_CAP) {      /* modem chattier than we sized for:   */
        drop_oldest();                  /* prefer recent data over old data    */
        g_win.n_overflow++;
    }

    uint32_t idx = g_win.head;
    g_win.s[idx & CAP_MASK] = (sample_t){ .ts = ts, .rssi = rssi };
    g_win.head++;
    g_win.sum += rssi;

    /* min deque: drop every back entry that this sample makes useless. It is
     * >= rssi and older, so it expires first -- it can never be the minimum
     * again while this sample is alive. */
    while (g_win.min_f != g_win.min_b &&
           g_win.s[g_win.minq[(g_win.min_b - 1u) & CAP_MASK] & CAP_MASK].rssi >= rssi)
        g_win.min_b--;
    g_win.minq[g_win.min_b & CAP_MASK] = idx;
    g_win.min_b++;

    /* max deque: mirror image */
    while (g_win.max_f != g_win.max_b &&
           g_win.s[g_win.maxq[(g_win.max_b - 1u) & CAP_MASK] & CAP_MASK].rssi <= rssi)
        g_win.max_b--;
    g_win.maxq[g_win.max_b & CAP_MASK] = idx;
    g_win.max_b++;

    pthread_mutex_unlock(&g_win.lock);
}

/* ------------------------------------------------------------------ */
/* sampler thread                                                      */
/* ------------------------------------------------------------------ */

static void handle(const RssiSample *r)
{
    switch (r->status) {
    case MODEM_OK:
        publish(r->timestamp, r->rssi_dbm);   /* cheapest, and most readers only want this */
        win_append(r->timestamp, r->rssi_dbm);
        break;
    case MODEM_BUSY:
        break;                                /* no new value; the published one still holds */
    default:                                  /* MODEM_ERROR: payload is garbage */
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);             /* it returns instantly -- do not spin the CPU */
        break;
    }
}

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        RssiSample r = modem_poll_rssi();
        handle(&r);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int rssi_init(void)
{
    if (g_started)
        return 0;

    atomic_store_explicit(&g_t0, modem_now_us(), memory_order_relaxed);

    /* The first poll never blocks, so take it here: a getter called the
     * instant init() returns already has a value. Retry a couple of times in
     * case it came back ERROR. */
    for (int tries = 0; tries < 3; tries++) {
        RssiSample r = modem_poll_rssi();
        handle(&r);
        if (r.status == MODEM_OK)
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

void rssi_deinit(void)
{
    if (!g_started)
        return;
    /* No cancellation, no signals: just ask, then wait for the thread to come
     * back out of the vendor call (<= ~100 ms) and re-test the flag. */
    atomic_store_explicit(&g_running, false, memory_order_release);
    pthread_join(g_thread, NULL);
    g_started = false;
}

/* ------------------------------------------------------------------ */
/* Part 1 getters -- one atomic load, wait-free, never touch the modem */
/* ------------------------------------------------------------------ */

int rssi_get_latest(int *dbm, uint64_t *age_us)
{
    uint64_t word = atomic_load_explicit(&g_pub, memory_order_acquire);
    if (word == PUB_NONE)
        return -1;                          /* nothing valid has ever been read */

    int value = (int)(int16_t)(uint16_t)(word & PUB_RSSI_MASK);
    uint64_t ts = atomic_load_explicit(&g_t0, memory_order_relaxed)
                  + (word >> PUB_RSSI_BITS) - 1u;
    uint64_t now = modem_now_us();

    if (dbm)
        *dbm = value;
    if (age_us)
        *age_us = (now > ts) ? now - ts : 0;   /* clock stepped back -> "just now" */
    return 0;
}

bool rssi_is_healthy(uint64_t max_age_us)
{
    uint64_t age;
    if (rssi_get_latest(NULL, &age) != 0)
        return false;                        /* never sampled: not healthy */
    return age <= max_age_us;
}

/* ------------------------------------------------------------------ */
/* Part 2 getters -- amortised O(1) under a lock held for nanoseconds  */
/* ------------------------------------------------------------------ */

int rssi_min_in_window(void)
{
    uint64_t now = modem_now_us();
    pthread_mutex_lock(&g_win.lock);
    evict_expired(now);                      /* samples age out with no new input */
    int r = (g_win.min_f == g_win.min_b)
              ? RSSI_NONE
              : g_win.s[g_win.minq[g_win.min_f & CAP_MASK] & CAP_MASK].rssi;
    pthread_mutex_unlock(&g_win.lock);
    return r;
}

int rssi_max_in_window(void)
{
    uint64_t now = modem_now_us();
    pthread_mutex_lock(&g_win.lock);
    evict_expired(now);
    int r = (g_win.max_f == g_win.max_b)
              ? RSSI_NONE
              : g_win.s[g_win.maxq[g_win.max_f & CAP_MASK] & CAP_MASK].rssi;
    pthread_mutex_unlock(&g_win.lock);
    return r;
}

double rssi_avg_in_window(void)
{
    uint64_t now = modem_now_us();
    pthread_mutex_lock(&g_win.lock);
    evict_expired(now);
    uint32_t n = win_count();
    double r = n ? (double)g_win.sum / (double)n : NAN;
    pthread_mutex_unlock(&g_win.lock);
    return r;
}
