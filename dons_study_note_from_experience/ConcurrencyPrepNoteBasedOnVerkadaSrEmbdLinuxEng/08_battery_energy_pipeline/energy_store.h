#ifndef ENERGY_STORE_H
#define ENERGY_STORE_H

#include <stdint.h>

#include "bms.h"

/* ---------------------------------------------------------------------------
 * energy_store.h — the API the harness calls. YOU implement it in
 *                  energy_store.c (energy_store_solution.c is the reference).
 *
 * Two-stage pipeline:
 *
 *     bms_read()  --> [stage 1: sampler] --> latest sample  --> bms_get_latest()
 *                          |                                     (never blocks)
 *                          +--> bounded queue --> [stage 2: uploader] --> bms_upload()
 *                          |                      (token bucket, <= N/s)
 *                          +--> 30-min history --> energy_joules_between()
 *
 * The tuning knobs below are shared by the harness and the implementation, so
 * the harness can assert the rate limit it configured. Do not change them.
 * ------------------------------------------------------------------------- */

#define UPLOAD_RATE_PER_SEC   5          /* token bucket refill: <= 5 uploads/s  */
#define UPLOAD_BURST          2          /* bucket capacity, in tokens           */
#define UPLOAD_BATCH_MAX      2          /* samples handed to one bms_upload()    */
#define UPLOAD_QUEUE_CAP      8          /* stage 1 -> stage 2 bounded queue      */

#ifndef ENERGY_WINDOW_US
#define ENERGY_WINDOW_US      (30ull * 60ull * 1000000ull)   /* 30 minutes */
#endif

/* Counters. Monotonic; never reset while the pipeline runs. */
struct BmsStats {
    uint64_t sampled;         /* BMS_OK readings accepted into the pipeline      */
    uint64_t uploaded;        /* samples successfully handed to bms_upload()     */
    uint64_t dropped;         /* samples the queue had to discard (back-pressure)*/
    uint64_t batches;         /* bms_upload() calls that returned 0              */
    uint64_t upload_wait_us;  /* time stage 2 spent waiting for a rate-limit
                               * token while it had work to do                  */
    uint64_t max_publish_us;  /* worst time stage 1 spent publishing ONE sample,
                               * i.e. everything except the bms_read() call.
                               * This is the evidence that the uploader never
                               * blocks the sampler.                            */
    uint64_t stale;           /* BMS_STALE readings                             */
    uint64_t errors;          /* BMS_ERROR readings                             */
};

/* Take the first sample (bms_read()'s first call does not block) and start the
 * sampler + uploader threads. Returns 0 on success, -1 otherwise.
 * Safe to call again after bms_pipeline_deinit(). */
int bms_pipeline_init(void);

/* Stop both threads and join them. Must not wait out a full rate-limit
 * interval: the uploader sits in a timed wait and has to be woken.
 * The 30-minute history stays readable after this returns. */
void bms_pipeline_deinit(void);

/* Part 1: copy the most recent BMS_OK sample into *out.
 * NEVER blocks on the BMS or on the uploader.
 * Returns 0 if a sample exists, -1 if none has been seen yet (also before
 * bms_pipeline_init(), which must not crash). */
int bms_get_latest(struct BmsSample *out);

/* Counters. Never blocks. */
struct BmsStats bms_stats(void);

/* Part 2: energy in joules delivered between t0 and t1 (microsecond stamps in
 * the bms_now_us() clock), integrating instantaneous power p = volts * amps
 * over the retained history.
 *
 * Conventions the harness checks:
 *   - t1 <= t0                      -> 0.0
 *   - fewer than 2 samples retained -> 0.0  (one point has no interval)
 *   - the query is CLAMPED to the retained span [oldest_ts, newest_ts];
 *     energy outside the measured span is unknown, not assumed. A range that
 *     lies entirely before the oldest sample or entirely after the newest one
 *     therefore yields 0.0.
 *   - inside the span the edges are EXACT: power is interpolated at t0 and at
 *     t1 so a query that starts and ends in the middle of one segment still
 *     returns that sliver's energy.
 * Never blocks on the BMS or on the uploader; safe from many threads. */
double energy_joules_between(uint64_t t0, uint64_t t1);

#endif /* ENERGY_STORE_H */
