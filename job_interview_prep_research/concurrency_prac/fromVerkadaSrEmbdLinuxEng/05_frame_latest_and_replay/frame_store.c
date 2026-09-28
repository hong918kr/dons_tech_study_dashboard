/* frame_store.c — YOUR solution (practice stub).
 *
 *   ./main.sh           build main.c + this file and run the checks
 *   ./main.sh sol       same harness against frame_store_solution.c
 *
 * Read question_note first, answer its 10 questions on paper, THEN write code.
 * As given, this file compiles and runs (it must never hang) and fails most
 * checks.
 *
 * Budget: ~20 min for Part 1, ~20 min for Part 2.
 */
/* glibc hides usleep()/nanosleep() when the compiler is in strict -std=c11
 * mode; Apple's libc does not care. Set the feature macro before any
 * include so this builds warning-free on Linux and macOS alike. */
#define _DEFAULT_SOURCE 1

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "capture.h"
#include "frame_store.h"

/* TODO: Part 1 state — the published frames.
 *  - How many frame buffers? Two? Three? What does the extra one buy you, and
 *    what does it cost? Write the sentence you would say out loud.
 *  - Per buffer you need: the FRAME_BYTES of data, its timestamp, and a count
 *    of how many readers are currently borrowing it.
 *  - Plus: which buffer is the newest complete one.
 *  - A buffer is reusable by the writer only when nobody borrows it AND it is
 *    not the one readers would acquire next.
 *  - What guards all this, and how long may the critical section be?
 *    (Hint: if a memcpy of FRAME_BYTES happens inside it, you got it wrong.)
 */

/* TODO: Part 2 state — the replay history.
 *  - Do the arithmetic first: frames/second * window seconds * FRAME_BYTES.
 *    Then pick a capacity that fits FRAME_REPLAY_BYTES_MAX and say why.
 *  - Ring of (timestamp, frame) kept sorted by timestamp so a lookup is a
 *    binary search, not a scan.
 *  - Eviction: what exactly do you drop, and what must you KEEP so that a
 *    query right at the edge of the window still has an answer?
 *  - What if the slot you are about to reuse is being read right now?
 */

/* TODO: counters. dropped frames, sensor errors, bad releases. */

/* TODO: the one and only caller of capture_next_frame().
 *  Loop while running:
 *    pick a buffer the writer may fill  -> none free? capture somewhere
 *                                          disposable and count a drop, but
 *                                          NEVER wait for a reader
 *    capture_next_frame() == -1         -> count it, back off, retry
 *    otherwise                          -> publish it (Part 1) and append a
 *                                          copy to the history (Part 2)
 */
static void *writer_main(void *arg)
{
    (void)arg;
    return NULL;
}

int frame_store_init(void)
{
    /* TODO:
     *  1. publish the first frame before returning (one ~25 ms blocking call)
     *     so a reader right after init is not handed NULL
     *  2. start the writer thread */
    (void)writer_main;
    return 0;
}

void frame_store_deinit(void)
{
    /* TODO: clear the running flag and join the writer. It may be parked inside
     * capture_next_frame() for up to one frame period. */
}

/* ---------------- Part 1 ---------------- */

const uint8_t *frame_acquire_latest(uint64_t *ts_out)
{
    /* TODO: hand back a pointer to the newest COMPLETE frame and take a
     * reference on it. No copy, no sensor call, no waiting. NULL if nothing has
     * been published yet. */
    (void)ts_out;
    return NULL;
}

void frame_release(const uint8_t *frame)
{
    /* TODO: find which buffer this pointer is, drop the reference.
     * NULL -> no-op. Unknown pointer or one release too many -> count it in
     * frame_release_errors() and do NOT decrement below zero. */
    (void)frame;
}

/* ---------------- Part 2 ---------------- */

int frame_at(uint64_t t, uint8_t *dst, uint64_t *actual_ts)
{
    /* TODO:
     *  - t in the future, or older than FRAME_REPLAY_WINDOW_US -> -1
     *  - binary search for the newest stored frame with ts <= t -> copy it out
     *  - nothing at or before t -> -1
     *  - keep the lock off the memcpy (pin the slot instead) */
    (void)t; (void)dst; (void)actual_ts;
    return -1;
}

size_t frame_count_in(uint64_t t0, uint64_t t1)
{
    /* TODO: two binary searches, subtract. t0 > t1 -> 0. */
    (void)t0; (void)t1;
    return 0;
}

/* ---------------- bookkeeping ---------------- */

uint64_t frame_dropped_count(void)   { return 0; }   /* TODO */
uint64_t frame_error_count(void)     { return 0; }   /* TODO */
uint64_t frame_release_errors(void)  { return 0; }   /* TODO */
size_t   frame_borrowed_count(void)  { return 0; }   /* TODO */
size_t   frame_replay_bytes(void)    { return 0; }   /* TODO */
