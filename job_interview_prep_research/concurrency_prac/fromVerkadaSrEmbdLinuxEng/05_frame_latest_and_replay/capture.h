#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

/* ------------------------------------------------------------------------
 * capture.h — vendor header for the camera SoC's capture unit (GIVEN).
 * Do not modify. You only get to write the wrapper around it.
 * ------------------------------------------------------------------------ */

/* One frame. Tiny on purpose so the harness can memcmp it; a real 4K frame is
 * ~8 MB, which is exactly why the reader must NOT get a copy. */
#define FRAME_BYTES 4096

/* Nominal time between frames, microseconds (~40 fps). */
#define CAPTURE_PERIOD_US 25000

/*
 * Blocks until the next frame is ready (~CAPTURE_PERIOD_US, with jitter) and
 * copies FRAME_BYTES bytes into dst, then writes the capture time (us, same
 * clock as capture_now_us()) into *timestamp.
 *
 *   return 0   a frame was written into dst and *timestamp was set
 *   return -1  sensor glitch: nothing was written, *timestamp untouched.
 *              Returns immediately (no ~25 ms wait) in this case.
 *
 * NOT thread-safe: it keeps static state. Exactly one thread may call it.
 * dst must be at least FRAME_BYTES and must stay untouched by anybody else
 * for the whole duration of the call (the sensor DMAs into it in pieces).
 */
int capture_next_frame(uint8_t *dst, uint64_t *timestamp);

/* Monotonic-ish wall clock in microseconds (gettimeofday under the hood).
 * Same clock as the timestamps capture_next_frame() hands out. */
uint64_t capture_now_us(void);

#endif /* CAPTURE_H */
