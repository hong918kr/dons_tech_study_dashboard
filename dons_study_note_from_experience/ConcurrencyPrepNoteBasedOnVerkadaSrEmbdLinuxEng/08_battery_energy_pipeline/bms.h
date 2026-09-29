#ifndef BMS_H
#define BMS_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * bms.h — vendor header for the MT81 trailer battery management system.
 *         GIVEN. Do not modify. Both functions are implemented in main.c
 *         (the fake BMS), exactly the way the real vendor blob behaves.
 * ------------------------------------------------------------------------- */

/* status field of struct BmsSample */
enum {
    BMS_ERROR = 0,   /* transport glitch: ALL other fields are garbage      */
    BMS_STALE = 1,   /* "same as last time": volts/amps/timestamp NOT written */
    BMS_OK    = 2,   /* volts/amps/timestamp are valid                      */
};

struct BmsSample {
    int      status;
    float    volts;
    float    amps;      /* + = discharging into the load, - = solar charging */
    uint64_t timestamp; /* microseconds, same clock as bms_now_us()          */
};

/* Read one sample from the BMS.
 *
 *   - BLOCKS. The BMS reports when it feels like it: the call returns after an
 *     irregular interval (see question_note for the scaled numbers).
 *   - The FIRST call returns immediately.
 *   - NOT thread-safe: static state inside the vendor blob. Exactly one thread
 *     may be inside bms_read() at a time.
 *   - BMS_STALE means the pack reading has not changed; volts/amps/timestamp
 *     are left untouched, so reading them is a bug.
 *   - BMS_ERROR is returned immediately with every field uninitialised.
 */
struct BmsSample bms_read(void);

/* "Upload" one batch of samples to the fleet backend.
 *
 *   - BLOCKS ~3 ms (network).
 *   - NOT thread-safe: exactly one thread may be inside bms_upload().
 *   - Returns 0 on success. (The fake backend in main.c always succeeds; a
 *     non-zero return would be a transient failure worth retrying.)
 *   - n must be >= 1.
 */
int bms_upload(const struct BmsSample *batch, int n);

/* Monotonically increasing wall clock in microseconds (gettimeofday). */
uint64_t bms_now_us(void);

#endif /* BMS_H */
