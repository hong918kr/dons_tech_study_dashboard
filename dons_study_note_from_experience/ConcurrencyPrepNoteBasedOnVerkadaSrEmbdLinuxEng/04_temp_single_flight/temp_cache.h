#ifndef TEMP_CACHE_H
#define TEMP_CACHE_H

#include <stdint.h>

/* ==================================================================
 * The API you implement (temp_cache.c). The harness calls only this.
 * ==================================================================*/

/* temp_get() return codes. */
#define TEMP_OK       0    /* *out is a reading no older than max_age_us   */
#define TEMP_STALE    1    /* the refresh failed; *out is the last good    */
                           /* value, which is OLDER than max_age_us        */
#define TEMP_NODATA (-1)   /* nothing usable: no reading has ever          */
                           /* succeeded (or the cache is not initialised)  */

/* History geometry (Part 2). The harness uses these to build ground truth. */
#define TEMP_BUCKET_US (60ull * 1000000ull)                 /* 1 minute    */
#define TEMP_NBUCKETS  60u                                  /* 60 minutes  */
#define TEMP_WINDOW_US (TEMP_BUCKET_US * TEMP_NBUCKETS)      /* 1 hour      */

/* Bring the cache up. No device access, no background thread: this API is
 * purely on demand. Resets the cached value, the history and the stats.
 * Returns 0 on success. Calling it twice without a deinit is a no-op. */
int temp_cache_init(void);

/* Tear down. Must wait for an in-flight vendor read to finish and must
 * release any thread parked inside temp_get(). Safe to call twice. */
void temp_cache_deinit(void);

/* ------------------------------------------------------------------
 * Part 1 — on-demand cached read with request coalescing.
 * ------------------------------------------------------------------
 * If the cached reading is fresher than max_age_us (strictly: age <
 * max_age_us), return it immediately without touching the device.
 * Otherwise refresh. If N threads ask at the same time and all of them
 * need a refresh, EXACTLY ONE tempdev_read() may happen; the other N-1
 * wait for that result instead of queueing reads of their own.
 *
 * max_age_us == 0 therefore means "force one refresh and give me its
 * result" — never "read the device N times".
 *
 * Returns TEMP_OK / TEMP_STALE / TEMP_NODATA (see above). *out is written
 * for TEMP_OK and TEMP_STALE only. Never blocks longer than one vendor
 * read plus scheduling.
 */
int temp_get(float *out, uint64_t max_age_us);

/* ------------------------------------------------------------------
 * Part 2 — range statistics over the last hour.
 * ------------------------------------------------------------------
 * min / max / mean of every recorded sample with t0 <= ts <= t1
 * (both ends inclusive), restricted to what is still inside the 1-hour
 * window. Any of min/max/avg may be NULL.
 *
 * Returns 0 and fills the outputs, or -1 if t0 > t1 or the range holds no
 * sample the cache still keeps. Cost must be bounded by the WINDOW, not by
 * the number of samples in it.
 */
int temp_range_stats(uint64_t t0, uint64_t t1, float *min, float *max, float *avg);

/* Record a sample. temp_get()'s refresh path calls this itself; the harness
 * also calls it directly to load an hour of history without waiting an hour.
 * Returns 0 if stored, -1 if dropped. */
int temp_history_add(uint64_t t_us, float celsius);

/* ------------------------------------------------------------------
 * Counters. The harness uses them to prove coalescing really happened
 * and that a range query did not just scan everything.
 * ------------------------------------------------------------------*/
typedef struct {
    uint64_t vendor_calls;   /* tempdev_read() calls this cache issued      */
    uint64_t hits;           /* temp_get() served from cache, no waiting    */
    uint64_t leaders;        /* temp_get() that performed the vendor read   */
    uint64_t coalesced;      /* temp_get() that rode along on someone       */
                             /* else's in-flight read                       */
    uint64_t nacks;          /* refreshes that came back -1                 */
    uint64_t range_queries;  /* temp_range_stats() calls                    */
    uint64_t last_scanned;   /* raw samples inspected by the LAST range     */
                             /* query (aggregated buckets cost 0)           */
    uint64_t raw_lost;       /* range queries that hit overwritten raw data */
} temp_stats_t;

void temp_cache_stats(temp_stats_t *out);

#endif /* TEMP_CACHE_H */
