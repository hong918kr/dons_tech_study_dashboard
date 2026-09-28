/* rssi_window.c — YOUR solution (practice stub).
 *
 *   ./main.sh          build main.c + this file and run the checks
 *   ./main.sh sol      same harness against rssi_window_solution.c
 *
 * Read question_note first and answer its 10 questions on paper. Only then code.
 * This stub compiles and runs to completion; it just fails almost everything.
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

/* TODO: shared state.
 *  Part 1: where does (latest rssi + its timestamp) live so that a reader gets
 *          BOTH from one indivisible read? Pick one and write down why:
 *          packed atomic word / seqlock / double buffer / mutex.
 *  Part 2: ring buffer of (ts, rssi) + what else, so that min and max are
 *          amortised O(1) instead of a scan? Plus a running sum for the average.
 *  Lifecycle: sampler thread handle + a "keep running" flag. */

/* TODO: the ONLY place modem_poll_rssi() may be called.
 *  MODEM_OK    -> publish latest, append to the window
 *  MODEM_BUSY  -> nothing new; the value already published is still current
 *  MODEM_ERROR -> payload is garbage; count it and back off (it returns
 *                 instantly, so a bare retry loop pegs a core) */
static void *sampler_main(void *arg)
{
    (void)arg;
    return NULL;
}

int rssi_init(void)
{
    /* TODO:
     *  1. take the first sample synchronously — it never blocks — so a getter
     *     called right after init already has a value
     *  2. start the sampler thread */
    (void)sampler_main;
    return 0;
}

void rssi_deinit(void)
{
    /* TODO: clear the running flag and join the thread. It may be parked inside
     * the vendor call for up to 100 ms; do not try to kill it. */
}

/* ---- Part 1 ---------------------------------------------------------- */

int rssi_get_latest(int *dbm, uint64_t *age_us)
{
    /* TODO: never touch the modem here. One indivisible read of (value, ts),
     * then age = now - ts. Return -1 until the first MODEM_OK sample. */
    (void)dbm;
    (void)age_us;
    return -1;
}

bool rssi_is_healthy(uint64_t max_age_us)
{
    /* TODO: a value exists AND it is at most max_age_us old. */
    (void)max_age_us;
    return false;
}

/* ---- Part 2 ---------------------------------------------------------- */

int rssi_min_in_window(void)
{
    /* TODO: amortised O(1). Evict expired samples here too — they age out even
     * when the modem has gone quiet. RSSI_NONE if the window is empty. */
    return RSSI_NONE;
}

int rssi_max_in_window(void)
{
    /* TODO: mirror image of the min case. */
    return RSSI_NONE;
}

double rssi_avg_in_window(void)
{
    /* TODO: running sum / count, O(1). NAN if the window is empty. */
    return NAN;
}
