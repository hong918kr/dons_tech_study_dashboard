#ifndef GPS_H
#define GPS_H

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Vendor-supplied GPS module header.  DO NOT MODIFY.
 * ---------------------------------------------------------------------- */

/* Status values in struct GpsFix::status */
enum {
    GPS_ERROR  = 0,   /* module hiccup. lat/lon/hdop/timestamp are GARBAGE. */
    GPS_NO_FIX = 1,   /* sky blocked; no new position this cycle.  GARBAGE.  */
    GPS_VALID  = 2,   /* lat/lon/hdop/timestamp are meaningful.              */
};

/* The fix payload the module hands back.
 *
 * NOTE this is 40 bytes on a 64-bit target (int + pad + 2x double + float +
 * pad + uint64_t).  It does NOT fit in a single machine word, so you cannot
 * publish it with one atomic store the way you would a float.
 */
struct GpsFix {
    int      status;
    double   lat;        /* degrees, WGS-84, north positive */
    double   lon;        /* degrees, WGS-84, east  positive */
    float    hdop;       /* horizontal dilution of precision, smaller = better */
    uint64_t timestamp;  /* microseconds, same clock as gps_now_us() */
};

/* BLOCKS until the module emits its next fix.
 *
 *   - sky visible:  blocks 20..90 ms, returns GPS_VALID with a fresh position
 *   - sky blocked:  blocks the full 100 ms receiver timeout, returns GPS_NO_FIX
 *   - hiccup:       returns GPS_ERROR immediately, fields uninitialised
 *
 * The FIRST call returns a GPS_VALID fix immediately (the module has a fix
 * cached from its cold start).
 *
 * NOT THREAD-SAFE: it keeps static state (first-call flag, dead-reckoning
 * position, PRNG).  Exactly one thread may ever call it.
 */
struct GpsFix gps_wait_for_fix(void);

/* Wall-clock microseconds (gettimeofday).  Same clock as GpsFix::timestamp.
 * Not monotonic -- NTP on the trailer's LTE modem can step it. */
uint64_t gps_now_us(void);

#endif /* GPS_H */
