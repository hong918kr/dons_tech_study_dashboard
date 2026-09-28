/* ap_table.c — YOUR code (practice stub).
 *
 *   ./main.sh           build main.c + this file and run the checks
 *   ./main.sh sol       same harness against ap_table_solution.c
 *
 * Read question_note first, answer its 10 questions on paper, THEN code.
 * As shipped this compiles, runs, never hangs, and fails most checks.
 *
 * Budget: ~20 min for Part 1, ~25 min for Part 2.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ap_table.h"
#include "wifi.h"

/* TODO: shared state.
 *
 * Part 1 — the published value is a VARIABLE-LENGTH COLLECTION (a count plus
 *   up to WIFI_MAX_APS entries, ~1.6 KB), not a single word. So:
 *     - where does one complete scan live so a reader can copy it while the
 *       scanner is already filling in the next one?
 *     - how many such buffers do you need, and why is two not obviously
 *       enough when a reader can be slow?
 *     - what one atomic operation makes a whole filled buffer visible at
 *       once, and what tells the writer that no reader is still inside the
 *       buffer it wants to reuse?
 *   Before writing code, decide against the alternatives: mutex + memcpy
 *   under the lock / buffer pool + refcount / RCU-style pointer swap. Be
 *   able to say out loud why you rejected two of them.
 *
 * Part 2 — the table:
 *     - key = the 6 BSSID bytes. Hash them (FNV-1a is 4 lines) and index a
 *       fixed array; collisions resolved by probing, not by malloc.
 *     - an entry carries the EWMA of RSSI, times_seen and last_seen_us.
 *     - entries older than AP_STALE_US must disappear from readers WITHOUT a
 *       timer thread. What is the cheapest place to notice they are dead,
 *       and what makes them safe to overwrite without breaking a probe chain?
 *
 * Plus: the scanner thread handle and a "keep running" flag.
 */

/* TODO: the ONLY place wifi_scan() may be called.
 * Loop:  wifi_scan() -> 0  : publish the snapshot, then fold it into the table
 *                      -> -1 : radio busy, nothing was written; back off a few
 *                              ms (it returned instantly — do not spin) and retry
 * Remember *found may exceed the buffer size you passed in. */
static void *scanner_main(void *arg)
{
    (void)arg;
    return NULL;
}

int wifi_table_init(void)
{
    /* TODO:
     *  1. run ONE scan synchronously, so a reader that calls a getter
     *     immediately after init already sees a complete snapshot
     *  2. start the scanner thread
     *  3. be idempotent */
    (void)scanner_main;
    return 0;
}

void wifi_table_deinit(void)
{
    /* TODO: clear the running flag and join the thread. It may be parked
     * inside wifi_scan() for a whole scan — you cannot cancel that safely,
     * so you wait. Leave the snapshot and the table readable afterwards. */
}

/* ---------------- Part 1 ---------------- */

size_t wifi_get_snapshot(struct ApInfo *out, size_t max)
{
    /* TODO: copy the newest COMPLETE scan into out.
     *  - never call the radio here
     *  - never mix entries from two scans (the harness memcmp's your result
     *    against the exact bytes one scan produced)
     *  - return min(scan size, max); 0 is a legal answer (empty scan, or no
     *    scan has finished yet) */
    (void)out;
    (void)max;
    return 0;
}

/* ---------------- Part 2 ---------------- */

void wifi_table_observe(const struct ApInfo *aps, size_t n)
{
    /* TODO: for each AP — insert if new (seed rssi_smoothed with the raw
     * value), otherwise update: EWMA per AP_EWMA_ALPHA, times_seen++,
     * last_seen_us = wifi_now_us(). If the table has no room and nothing in
     * it is stale, count the drop instead of throwing out a live entry. */
    (void)aps;
    (void)n;
}

size_t wifi_top_k(struct ApEntry *out, size_t k)
{
    /* TODO: the k strongest live entries, strongest first.
     * Use a size-k min-heap over the table, not a sort of all N — and be
     * ready to say why. Ties in rssi_smoothed break by BSSID ascending. */
    (void)out;
    (void)k;
    return 0;
}

int wifi_lookup(const uint8_t bssid[6], struct ApEntry *out)
{
    /* TODO: O(1) average. Probe from the hash of the BSSID; a stale entry
     * counts as a miss. Return 0 on a hit, -1 otherwise. */
    (void)bssid;
    (void)out;
    return -1;
}

size_t wifi_table_count(void)
{
    /* TODO: live (non-stale) entries. */
    return 0;
}

size_t wifi_table_dropped(void)
{
    /* TODO: APs refused because the table was full of live entries. */
    return 0;
}
