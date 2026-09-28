#ifndef GPS_CACHE_H
#define GPS_CACHE_H

#include <stdint.h>

#include "gps.h"

/* ------------------------------------------------------------------------
 * The API you implement.  Wraps the blocking, non-thread-safe vendor call in
 * something a dozen application threads can poll without ever stalling.
 * ---------------------------------------------------------------------- */

/* How far back gps_fix_at() / gps_distance_travelled() can look.
 * Overridable at build time so the harness can exercise eviction. */
#ifndef GPS_WINDOW_US
#define GPS_WINDOW_US (10ull * 60ull * 1000000ull)   /* 10 minutes */
#endif

/* Start sampling.  Call once before any getter.  Returns 0 on success, <0 on
 * failure.  Must be safe to call twice (second call is a no-op returning 0). */
int  gps_cache_init(void);

/* Stop the sampler thread.  May wait up to one vendor block (~100 ms) for the
 * in-flight gps_wait_for_fix() to return.  Cached data stays readable. */
void gps_cache_deinit(void);

/* ---- Part 1 ---------------------------------------------------------- */

/* Snapshot of the most recent GPS_VALID fix.  NEVER blocks on the module.
 * Thread-safe: any number of threads, concurrently, while the sampler writes.
 *
 * Returns  0  and fills *out  -- a coherent copy of ONE fix
 *         -1  and leaves *out untouched -- no valid fix yet
 *
 * "Coherent" is the whole point: *out must be a single fix as the module
 * emitted it.  Handing back last second's lat with this second's lon is the
 * bug that makes a geofence alert fire in the wrong county. */
int  gps_get_last_fix(struct GpsFix *out);

/* ---- Part 2 ---------------------------------------------------------- */

/* The fix in effect at time t: the most recent GPS_VALID fix with
 * timestamp <= t.  (No fix after it means the trailer had not moved, or we
 * would have been told.)  NEVER blocks on the module.
 *
 * Returns  0  and fills *out
 *         -1  if t is in the future, older than GPS_WINDOW_US, or before the
 *             first fix we hold.  *out untouched. */
int  gps_fix_at(uint64_t t, struct GpsFix *out);

/* Metres travelled between t0 and t1, along the polyline of recorded fixes.
 *
 * Definition (no interpolation to the endpoints -- keep it simple):
 *   let k0 = first recorded fix with timestamp >= t0
 *       k1 = last  recorded fix with timestamp <= t1
 *   result = sum over k in [k0, k1) of segment_m(fix[k], fix[k+1])
 *   ...and 0.0 if k0 >= k1 (fewer than two fixes inside the range).
 *
 * segment_m() is the equirectangular approximation -- flat-earth over a few
 * km, which is all a parked-and-towed trailer ever covers, and it is cheap
 * and deterministic (no haversine trig soup):
 *
 *   #define DEG2RAD 0.017453292519943295
 *   #define EARTH_R_M 6371008.8
 *   double dlat = (lat2 - lat1) * DEG2RAD;
 *   double dlon = (lon2 - lon1) * DEG2RAD;
 *   double latm = (lat1 + lat2) * 0.5 * DEG2RAD;
 *   double x    = dlon * cos(latm);
 *   segment_m   = EARTH_R_M * sqrt(dlat * dlat + x * x);
 *
 * Use exactly that formula -- the harness computes its ground truth with it.
 *
 * Returns NAN if t0 > t1, t1 is in the future, or t0 is older than
 * GPS_WINDOW_US.  Returns 0.0 when t0 == t1 (and t0 is a legal time).
 *
 * Should be O(log n), not O(n): the dashboard polls it once a second per
 * trailer and there can be 6000 fixes in the window. */
double gps_distance_travelled(uint64_t t0, uint64_t t1);

#endif /* GPS_CACHE_H */
