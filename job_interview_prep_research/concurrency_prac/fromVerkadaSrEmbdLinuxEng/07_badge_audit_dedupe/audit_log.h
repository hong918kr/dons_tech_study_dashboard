#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "reader.h"

/* ------------------------------------------------------------------ */
/* Tunables. The harness reads these, so they must stay #defines.      */
/* ------------------------------------------------------------------ */

/* Part 1 — bounded queue between the 4 door threads and the logger. */
#define AUDIT_QUEUE_CAP        16u          /* power of two            */
#define AUDIT_SUBMIT_WAIT_US   2000u        /* bounded wait before we drop */

/* Part 2 — per-door recent history (fixed ring, no malloc). */
#define AUDIT_RECENT_PER_DOOR  16u

/* Part 2 — duplicate-suppression table (open addressing, no malloc). */
#define AUDIT_DEDUPE_CAP        1024u       /* power of two            */
#define AUDIT_DEDUPE_WINDOW     16u         /* probe window, in slots  */
#define AUDIT_DEDUPE_LOAD_LIMIT 768u        /* 0.75 * CAP: design budget, not a hard cap */
#define AUDIT_DEDUPE_RETENTION_US (30ull * 1000000ull)  /* how long a badge is remembered */

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/* Start the pipeline: AC42_DOORS producer threads (one per reader, because
 * reader_wait_badge() has per-door state) plus ONE audit-logger thread (because
 * audit_store_write() is single-threaded). Returns 0 on success.
 * Idempotent: a second call while running is a no-op returning 0. */
int audit_init(void);

/* Stop everything and return quickly even though all producers are parked
 * inside reader_wait_badge(): cancel the readers, wake every condvar waiter,
 * join the producers, let the logger drain what is still queued, join it.
 * After this, audit_submit() returns -1 and no thread touches the vendor. */
void audit_deinit(void);

/* ------------------------------------------------------------------ */
/* Part 1 — producer side                                             */
/* ------------------------------------------------------------------ */

/* Hand one badge event to the logger. Callable from any thread (MPSC).
 * Blocks at most AUDIT_SUBMIT_WAIT_US while the queue is full, so a door
 * thread can never be stuck behind the audit store.
 *   returns 0   queued
 *   returns -1  dropped (queue still full when the wait expired, the pipeline
 *               is shutting down, or b == NULL). Every -1 bumps audit_dropped().
 * Events submitted by the SAME thread are logged in submission order; events
 * from different threads may interleave in any way. */
int audit_submit(const struct Badge *b);

/* Stats. Never block. */
uint64_t audit_logged(void);   /* records handed to audit_store_write()      */
uint64_t audit_dropped(void);  /* submissions rejected (see audit_submit)    */

/* ------------------------------------------------------------------ */
/* Part 2 — query side (must not block on the vendor)                 */
/* ------------------------------------------------------------------ */

/* Anti-passback: has `badge_id` already been logged recently?
 * Returns 1 if the badge's most recent logged sighting t satisfies
 *     t <= now_us  &&  now_us - t <= window_us          (both ends inclusive)
 * otherwise 0. O(1) average.
 *
 * Only the MOST RECENT sighting of a badge is remembered, so a now_us that is
 * earlier than that sighting returns 0. A badge last seen longer than
 * AUDIT_DEDUPE_RETENTION_US ago may already have been evicted, so keep
 * window_us well below that. */
int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us);

/* The last badges logged at `door`, NEWEST FIRST, into out[0..n-1].
 * Returns the number written: min(max, AUDIT_RECENT_PER_DOOR, #available).
 * Returns 0 for an out-of-range door, max == 0, or out == NULL. */
size_t audit_recent_for_door(uint8_t door, struct Badge *out, size_t max);

/* Dedupe-table health (the numbers you would export to the fleet dashboard). */
size_t   audit_dedupe_live(void);       /* slots in use                              */
uint64_t audit_dedupe_evictions(void);  /* live entries overwritten by capacity pressure */
uint64_t audit_dedupe_stale_reuse(void);/* slots reclaimed lazily because they aged out  */

#endif /* AUDIT_LOG_H */
