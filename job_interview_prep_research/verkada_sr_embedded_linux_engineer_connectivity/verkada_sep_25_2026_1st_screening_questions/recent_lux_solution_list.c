/* recent_lux_solution_list.c — Part 2 step 2 of the interviewer's progression:
 *   circular queue  ->  LINKED LIST  ->  red-black tree
 *
 *   ./main.sh list            normal run
 *   ./main.sh window list     2-second window, exercises eviction
 *
 * What the linked list buys over the circular queue: no fixed capacity to
 * guess (the fake sensor has no rate limit). What it costs: a malloc per
 * sample, pointer chasing, and O(n) lookup. Queries are usually for recent
 * times, so the walk starts at the NEWEST node — O(k) where k = samples newer
 * than t, O(n) worst case.
 *
 * Part 1 and the sampler thread are identical to recent_lux_solution.c.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "als.h"
#include "recent_lux.h"

#ifndef LUX_WINDOW_US
#define LUX_WINDOW_US (10ull * 60ull * 1000000ull)   /* 10 minutes */
#endif

/* Dynamic, but still bounded: an unbounded list is a memory leak waiting for a
 * chatty sensor. 65536 nodes * 32 B = 2 MiB worst case. */
#ifndef LUX_MAX_NODES
#define LUX_MAX_NODES 65536u
#endif

#define ERROR_BACKOFF_US 10000u

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

typedef struct lux_node {
    uint64_t         ts;
    float            lux;
    struct lux_node *prev;   /* older */
    struct lux_node *next;   /* newer */
} lux_node_t;

static _Atomic uint32_t g_latest_bits = 0x7FC00000u;   /* Part 1, quiet NaN */

static struct {
    pthread_mutex_t lock;
    lux_node_t     *oldest;
    lux_node_t     *newest;
    uint32_t        count;
    uint32_t        overflow;
    uint32_t        alloc_fail;
} g_hist = {.lock = PTHREAD_MUTEX_INITIALIZER};

static _Atomic uint32_t g_errors;
static _Atomic bool     g_running;
static bool             g_started;
static pthread_t        g_thread;

/* ------------------------------------------------------------------ */
/* writer side                                                         */
/* ------------------------------------------------------------------ */

static void publish_latest(float lux)
{
    uint32_t bits;
    memcpy(&bits, &lux, sizeof bits);
    atomic_store_explicit(&g_latest_bits, bits, memory_order_release);
}

/* caller holds the lock; unlinked nodes go on *garbage, freed after unlock */
static void pop_oldest_locked(lux_node_t **garbage)
{
    lux_node_t *old = g_hist.oldest;
    g_hist.oldest = old->next;
    if (g_hist.oldest)
        g_hist.oldest->prev = NULL;
    else
        g_hist.newest = NULL;
    g_hist.count--;
    old->next = *garbage;
    *garbage = old;
}

static void hist_append(uint64_t ts, float lux)
{
    /* malloc/free never run under the lock: the allocator has its own lock and
     * unbounded latency, and readers would wait behind it. */
    lux_node_t *n = malloc(sizeof *n);
    if (n == NULL) {
        g_hist.alloc_fail++;                 /* sampler-only counter */
        return;
    }
    n->ts = ts;
    n->lux = lux;
    n->next = NULL;

    lux_node_t *garbage = NULL;
    uint64_t now = get_timestamp();

    pthread_mutex_lock(&g_hist.lock);
    if (g_hist.newest && n->ts < g_hist.newest->ts)   /* keep sorted if the clock steps back */
        n->ts = g_hist.newest->ts;

    /* keep-one-older: drop the oldest only when the NEXT one is also out of the window */
    while (g_hist.count >= 2) {
        uint64_t next_ts = g_hist.oldest->next->ts;
        if (next_ts > now || now - next_ts < LUX_WINDOW_US)
            break;
        pop_oldest_locked(&garbage);
    }
    if (g_hist.count == LUX_MAX_NODES) {
        pop_oldest_locked(&garbage);
        g_hist.overflow++;
    }

    n->prev = g_hist.newest;
    if (g_hist.newest)
        g_hist.newest->next = n;
    else
        g_hist.oldest = n;
    g_hist.newest = n;
    g_hist.count++;
    pthread_mutex_unlock(&g_hist.lock);

    while (garbage) {
        lux_node_t *next = garbage->next;
        free(garbage);
        garbage = next;
    }
}

static void handle_reading(const SensorReading *r)
{
    switch (r->status) {
    case VALID:
        publish_latest(r->lux);
        hist_append(r->timestamp, r->lux);
        break;
    case NO_CHANGE:
        break;
    default:
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);
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
    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_hist.lock);
    lux_node_t *p = g_hist.oldest;
    g_hist.oldest = g_hist.newest = NULL;
    g_hist.count = 0;
    pthread_mutex_unlock(&g_hist.lock);
    while (p) {
        lux_node_t *next = p->next;
        free(p);
        p = next;
    }
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
/* Part 2: walk back from the newest node                              */
/* ------------------------------------------------------------------ */

float get_lux_at(uint64_t t)
{
    uint64_t now = get_timestamp();
    if (t > now || now - t > LUX_WINDOW_US)
        return NAN;

    float result = NAN;
    pthread_mutex_lock(&g_hist.lock);
    const lux_node_t *p = g_hist.newest;
    while (p && p->ts > t)
        p = p->prev;
    if (p)
        result = p->lux;
    pthread_mutex_unlock(&g_hist.lock);
    return result;
}
