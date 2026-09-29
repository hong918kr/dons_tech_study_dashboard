#ifndef RSSI_WINDOW_H
#define RSSI_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

/* Sliding window length.
 *
 * The product spec asks for "the last 60 seconds". The practice harness builds
 * with a 1.5 second window so that eviction from the window really happens
 * inside a ~8 second test run. Everything else is identical; nothing in your
 * code may assume a particular value. */
#ifndef RSSI_WINDOW_US
#define RSSI_WINDOW_US (1500000ull)     /* 1.5 s for the practice harness */
#endif

/* Returned by rssi_min_in_window() / rssi_max_in_window() when the window
 * currently holds no sample (nothing sampled yet, or everything aged out).
 * No real cellular RSSI is anywhere near this value. */
#define RSSI_NONE (-32768)

/* Start sampling. Call once before any getter. Returns 0 on success. */
int rssi_init(void);

/* Stop the sampler thread. Must return promptly even though the modem call
 * can be blocked for up to 100 ms. Safe to call without rssi_init(). */
void rssi_deinit(void);

/* ---- Part 1: latest value + staleness -------------------------------- */

/* Most recent MODEM_OK reading, and how long ago it was taken.
 * Never blocks, safe from any number of threads.
 * Returns 0 and fills *dbm / *age_us (either may be NULL), or
 * returns -1 if no valid sample has ever been taken.
 * *dbm and *age_us always describe the SAME sample -- a reader must never be
 * able to pair a fresh value with a stale age or vice versa. */
int rssi_get_latest(int *dbm, uint64_t *age_us);

/* true if a valid sample exists and it is at most max_age_us old, i.e. the
 * sampler is still alive and the modem is still answering. */
bool rssi_is_healthy(uint64_t max_age_us);

/* ---- Part 2: sliding window statistics ------------------------------- */

/* Smallest / largest rssi_dbm among the samples taken within the last
 * RSSI_WINDOW_US. Amortised O(1), NOT O(window). RSSI_NONE if empty. */
int rssi_min_in_window(void);
int rssi_max_in_window(void);

/* Mean rssi_dbm over the same window. NAN if the window is empty. */
double rssi_avg_in_window(void);

#endif /* RSSI_WINDOW_H */
