#ifndef FRAME_STORE_H
#define FRAME_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "capture.h"

/* ------------------------------------------------------------------------
 * frame_store.h — the API you implement (harness calls exactly this).
 * ------------------------------------------------------------------------ */

/* Part 2 window: "the last ~2 seconds". (The real ask would be 10 s of
 * pre-roll; 2 s keeps the harness under 10 seconds of wall clock.) */
#ifndef FRAME_REPLAY_WINDOW_US
#define FRAME_REPLAY_WINDOW_US (2ull * 1000000ull)
#endif

/* Hard memory budget for the replay history. Do the arithmetic before you
 * pick a capacity: 40 fps * 2 s * 4096 B = 320 KiB of pixels. */
#ifndef FRAME_REPLAY_BYTES_MAX
#define FRAME_REPLAY_BYTES_MAX (768u * 1024u)
#endif

/* Lifecycle. init() returns only after the first frame has been published, so
 * a reader that calls frame_acquire_latest() right after init gets a frame
 * (it blocks ~25 ms once). Returns 0 on success. */
int  frame_store_init(void);
void frame_store_deinit(void);

/* ---------------- Part 1: the latest frame, borrowed ---------------- */

/*
 * Borrow the most recent complete frame. Never blocks on the sensor, never
 * copies FRAME_BYTES, never hands back a half-written frame.
 *   returns a pointer to FRAME_BYTES of stable frame data, or NULL if no
 *   frame has been published yet. *ts_out (may be NULL) gets its capture time.
 * The pointer stays valid until the matching frame_release(). The caller must
 * not write through it.
 */
const uint8_t *frame_acquire_latest(uint64_t *ts_out);

/* Give a borrowed frame back. frame_release(NULL) is a no-op. Releasing a
 * pointer that was never handed out, or releasing twice, must be *detected*
 * (see frame_release_errors()) and must not corrupt the store. */
void frame_release(const uint8_t *frame);

/* ---------------- Part 2: short replay buffer ---------------- */

/*
 * The frame in effect at time t: the newest frame whose timestamp <= t.
 * Copies FRAME_BYTES into dst (dst may be NULL if you only want the time) and
 * writes its real capture time into *actual_ts (may be NULL).
 *   return 0   found
 *   return -1  t is in the future, older than FRAME_REPLAY_WINDOW_US, or
 *              before the oldest frame the store still holds.
 */
int frame_at(uint64_t t, uint8_t *dst, uint64_t *actual_ts);

/* How many stored frames have t0 <= timestamp <= t1. 0 if t0 > t1. Counts
 * what the store still holds (frames outside the window are gone). */
size_t frame_count_in(uint64_t t0, uint64_t t1);

/* ---------------- bookkeeping the harness reads ---------------- *
 * These exist so the harness can prove the writer never blocked and the
 * refcounting is honest. Be ready to talk about every one of them.         */

/* Frames thrown away because every publish buffer was borrowed. A dropped
 * frame is dropped completely: it does NOT show up in the replay history. */
uint64_t frame_dropped_count(void);

/* capture_next_frame() returned -1 this many times. */
uint64_t frame_error_count(void);

/* Bad frame_release() calls seen (unknown pointer, or one release too many). */
uint64_t frame_release_errors(void);

/* Borrows outstanding right now (sum of the per-buffer refcounts). */
size_t frame_borrowed_count(void);

/* Bytes the replay history occupies right now. Must never exceed
 * FRAME_REPLAY_BYTES_MAX. */
size_t frame_replay_bytes(void);

#endif /* FRAME_STORE_H */
