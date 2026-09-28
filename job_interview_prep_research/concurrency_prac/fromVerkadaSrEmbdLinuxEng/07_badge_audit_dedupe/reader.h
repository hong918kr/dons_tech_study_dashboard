#ifndef READER_H
#define READER_H

#include <stdint.h>

/* Vendor header for the AC42 four-door access controller. DO NOT MODIFY.
 * The fake implementation lives in main.c. */

#define AC42_DOORS 4

struct Badge {
    uint32_t badge_id;   /* credential number presented at the reader      */
    uint8_t  door;       /* 0 .. AC42_DOORS-1                              */
    uint64_t timestamp;  /* microseconds, same clock as reader_now_us()    */
};

/* Blocks until a badge is presented at `door`, then fills *out.
 *
 *   returns 0   a badge was read; *out is valid
 *   returns -1  nothing to hand you. The vendor does NOT tell you why:
 *               it is either a transient read error (retry) or the wait was
 *               interrupted by reader_cancel(door). Decide with your own
 *               shutdown flag.
 *
 * PER-DOOR STATE. Each reader has its own I2C/Wiegand state machine inside
 * the library, so:
 *     - calling it concurrently for DIFFERENT doors is safe,
 *     - calling it twice concurrently for the SAME door is undefined
 *       behaviour (the harness detects and reports this).
 * This is the constraint that fixes the thread model: one thread per door.
 *
 * How long it blocks: in the field, until a human walks up (seconds). In this
 * harness the wait is scaled down to ~2-9 ms so the tests finish; the shutdown
 * test switches the readers into a long "parked" wait (~3 s) instead.
 */
int reader_wait_badge(uint8_t door, struct Badge *out);

/* Makes the in-flight reader_wait_badge(door) — and every later call for that
 * door — return -1 promptly. Sticky: there is no un-cancel. Safe to call from
 * any thread, including while a wait is in flight. */
void reader_cancel(uint8_t door);

/* Append one record to the tamper-evident audit store (flash + cloud spool).
 *
 *   returns 0 on success.
 *
 * SLOW and NOT THREAD-SAFE: it owns a single append cursor, so exactly one
 * thread may be inside it at a time. In the field an append costs ~1 s; here
 * it is scaled to ~8 ms. This is why the pipeline has a single consumer and
 * why a burst of badges has to be queued rather than written inline.
 * (The harness reports it if more than one thread ever calls this.)
 */
int audit_store_write(const struct Badge *b);

/* Wall-clock microseconds (gettimeofday). Not monotonic. */
uint64_t reader_now_us(void);

#endif /* READER_H */
