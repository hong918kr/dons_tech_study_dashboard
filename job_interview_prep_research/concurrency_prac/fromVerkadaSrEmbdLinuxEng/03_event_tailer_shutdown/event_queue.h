#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

#include "evsrc.h"

/* ------------------------------------------------------------------ *
 * The API you implement. The harness (main.c) only calls these.
 * ------------------------------------------------------------------ */

/* --- tunables (the harness reads them too, so they live here) ------ */

/* Bounded queue capacity. Deliberately tiny so overflow is normal, not
 * exotic: a burst from one vendor bridge must not be able to grow memory
 * without bound on a gateway with 64 MiB of RAM. */
#ifndef EVQ_CAPACITY
#define EVQ_CAPACITY 16u
#endif

/* Part 2 counting window: EVQ_NBUCKETS buckets of EVQ_BUCKET_US each.
 * Default = 300 x 1 s = the last 5 minutes. */
#ifndef EVQ_BUCKET_US
#define EVQ_BUCKET_US 1000000ull
#endif
#ifndef EVQ_NBUCKETS
#define EVQ_NBUCKETS 300u
#endif
#define EVQ_WINDOW_US ((uint64_t)(EVQ_BUCKET_US) * (uint64_t)(EVQ_NBUCKETS))

/* evq_count_since(type, ...) with this type counts every type at once. */
#define EV_TYPE_ANY (-1)

/* --- statistics ---------------------------------------------------- */

struct EvqStats {
    uint64_t received;  /* events taken from the vendor (whatever happened next) */
    uint64_t dropped;   /* oldest events thrown away because the queue was full  */
    uint64_t popped;    /* events handed to a consumer                            */
    uint64_t errors;    /* evsrc_read_blocking() returned -1                      */
    uint32_t depth;     /* events sitting in the queue right now                  */
};

/* --- lifecycle ----------------------------------------------------- */

/* Start the tailer. Returns 0 on success, -1 on failure.
 * Must not block waiting for the first event. */
int evq_init(void);

/* Stop the tailer and release everything.
 *
 * The sampler thread is almost certainly parked inside
 * evsrc_read_blocking(), which has no timeout: this call still has to
 * return promptly. Idempotent; safe to call twice.
 * Any consumer blocked in evq_pop_timed() is released with -1. */
void evq_deinit(void);

/* --- Part 1: non-blocking consumer API ----------------------------- */

/* Take the oldest queued event. Never blocks, never touches the vendor.
 *    0  *out filled
 *   -1  queue empty (or not initialised / shutting down) */
int evq_try_pop(struct Event *out);

/* Same, but wait up to timeout_us for an event to show up.
 *    0  *out filled
 *   -1  timed out, or shutting down
 * timeout_us == 0 behaves like evq_try_pop(). */
int evq_pop_timed(struct Event *out, uint64_t timeout_us);

/* Snapshot of the counters. Never blocks on the vendor. */
void evq_stats(struct EvqStats *out);

/* --- Part 2: time-bucketed counting -------------------------------- */

/* How many events of `type` (or EV_TYPE_ANY) arrived at or after
 * since_us, within the last EVQ_WINDOW_US.
 *
 * Counted at ingest, so an event still counts here even if the queue
 * dropped it: "what happened" and "what we managed to process" are
 * different questions.
 *
 * Resolution is one bucket: the answer includes the whole bucket that
 * contains since_us. Rules:
 *   - since_us > now                      -> 0
 *   - since_us older than the window      -> clamped to the window start
 *   - unknown type                        -> 0
 * Must run in O(EVQ_NBUCKETS), never O(number of events), and must not
 * block on the vendor. */
uint32_t evq_count_since(int type, uint64_t since_us);

#endif /* EVENT_QUEUE_H */
