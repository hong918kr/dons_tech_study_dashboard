/* gps_cache.c — YOUR solution (practice stub).
 *
 *   ./main.sh            build main.c + this file and run the checks
 *   ./main.sh sol        same harness against gps_cache_solution.c
 *   ./main.sh window     1.5-second window, exercises eviction
 *
 * Read question_note first.  Answer its 10 questions on paper, THEN code.
 * Part 1 target: 10-20 min.  Part 2 target: another 15-25 min.
 *
 * As shipped this compiles, runs, and fails most checks -- it must never hang.
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

/* TODO: Part 1 shared state.
 *  struct GpsFix is 40 bytes -- there is no single atomic store for it.
 *  Decide: mutex-protected copy, seqlock (odd/even version counter), or
 *  double buffer + atomic index?  Write down why, and what a reader must
 *  never be able to see.
 */

/* TODO: Part 2 shared state.
 *  Fixed-capacity ring of struct GpsFix, oldest at tail, newest at head-1.
 *   - how many slots, and from what worst-case fix rate?
 *   - what makes binary search legal here, and what breaks it?
 *   - eviction: which fix at the window's edge must you NOT throw away?
 *   - distance over a range must be O(log n), not O(n).  What do you store
 *     alongside each fix to get that?
 */

/* TODO: the ONE place gps_wait_for_fix() may be called.
 *  Loop while a "keep running" flag is set:
 *    GPS_VALID  -> publish as the latest fix, append to the history
 *    GPS_NO_FIX -> nothing happened; the previous fix is still the truth
 *    GPS_ERROR  -> every field is garbage.  Count it and back off; it returns
 *                  immediately, so a bare retry is a spin loop.
 */
static void *sampler_main(void *arg)
{
    (void)arg;
    return NULL;
}

/* Equirectangular segment distance in metres -- use exactly this; the harness
 * computes its ground truth with the same formula. */
static double segment_m(const struct GpsFix *a, const struct GpsFix *b)
{
    double dlat = (b->lat - a->lat) * DEG2RAD;
    double dlon = (b->lon - a->lon) * DEG2RAD;
    double latm = (a->lat + b->lat) * 0.5 * DEG2RAD;
    double x    = dlon * cos(latm);
    return EARTH_R_M * sqrt(dlat * dlat + x * x);
}

int gps_cache_init(void)
{
    /* TODO:
     *  1. take the first fix on THIS thread -- that call never blocks, so a
     *     caller polling right after init already has a position
     *  2. start the sampler thread
     *  3. be a no-op on a second call */
    (void)sampler_main;
    (void)segment_m;
    return 0;
}

void gps_cache_deinit(void)
{
    /* TODO: clear the running flag and join the thread.  It may be parked
     * inside gps_wait_for_fix() for up to one receiver timeout (~100 ms). */
}

/* ---- Part 1 ---------------------------------------------------------- */

int gps_get_last_fix(struct GpsFix *out)
{
    /* TODO: never touch the module here.  Fill *out with ONE coherent fix and
     * return 0; return -1 until the first GPS_VALID fix has arrived.
     * A mix of fields from two different fixes is the bug being tested. */
    (void)out;
    return -1;
}

/* ---- Part 2 ---------------------------------------------------------- */

int gps_fix_at(uint64_t t, struct GpsFix *out)
{
    /* TODO: most recent fix with timestamp <= t, via binary search.
     *  -1 if t is in the future, older than GPS_WINDOW_US, or before the
     *  oldest fix you still hold. */
    (void)t;
    (void)out;
    return -1;
}

double gps_distance_travelled(uint64_t t0, uint64_t t1)
{
    /* TODO: metres between t0 and t1 along the recorded fixes, in O(log n).
     *  NAN if t0 > t1, t1 is in the future, or t0 is older than the window.
     *  0.0 if t0 == t1 or fewer than two fixes fall inside the range. */
    (void)t0;
    (void)t1;
    return NAN;
}
