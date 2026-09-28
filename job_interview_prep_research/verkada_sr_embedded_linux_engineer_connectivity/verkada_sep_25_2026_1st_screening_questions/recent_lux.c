/* recent_lux.c — YOUR solution (practice stub).
 *
 *   ./main.sh           build main.c + this file and run the checks
 *   ./main.sh sol       same harness against recent_lux_solution.c
 *
 * The pad's stub had `#include <cstddef>` — that is a C++ header and does
 * not compile as C. Use <stddef.h>.
 *
 * Read question_note first. Answer its 10 questions on paper, then code.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "als.h"
#include "recent_lux.h"

#ifndef LUX_WINDOW_US
#define LUX_WINDOW_US (10ull * 60ull * 1000000ull)   /* 10 minutes */
#endif

/* TODO: shared state.
 *  - Part 1: where does the latest lux live, and how is it read without blocking?
 *  - Part 2: history of (timestamp, lux). Fixed capacity — how big, and why?
 *  - the sampler thread handle + a "keep running" flag
 */

/* TODO: the only place read_next_sample() is called.
 * Loop: read -> VALID: publish latest + append to history
 *              NO_CHANGE: nothing changed (the old value still holds)
 *              ERROR: fields are garbage — count it, back off, retry */
static void *sampler_main(void *arg)
{
    (void)arg;
    return NULL;
}

int init_recent_lux(void)
{
    /* TODO:
     *  1. take the first sample synchronously (it never blocks), so a getter
     *     called right after init already has a value
     *  2. start the sampler thread */
    (void)sampler_main;
    return 0;
}

void deinit_recent_lux(void)
{
    /* TODO: clear the running flag, join the thread (it may sit in the sensor for up to 1 s) */
}

// Part 1
float get_most_recent_lux(void)
{
    /* TODO: never touch the sensor here. Return NAN until the first VALID sample. */
    return NAN;
}

// Part 2
float get_lux_at(uint64_t timestamp)
{
    /* TODO:
     *  - timestamp in the future, or older than LUX_WINDOW_US -> NAN
     *  - otherwise: lux of the last VALID sample with ts <= timestamp (binary search)
     *  - before the first sample -> NAN */
    (void)timestamp;
    return NAN;
}
