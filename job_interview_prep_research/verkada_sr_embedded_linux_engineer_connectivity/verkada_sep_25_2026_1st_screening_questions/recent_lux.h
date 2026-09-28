#ifndef RECENT_LUX_H
#define RECENT_LUX_H

#include <stdint.h>

/* Start sampling. Call once before any getter. Returns 0 on success. */
int init_recent_lux(void);

/* Stop the sampler thread (waits at most ~1 s for the blocked sensor call). */
void deinit_recent_lux(void);

/* Part 1: most recent VALID lux. Never blocks. NAN if no valid sample yet. */
float get_most_recent_lux(void);

/* Part 2: lux in effect at `timestamp` (us, get_timestamp() clock).
 * NAN if timestamp is in the future, older than the window, or before the first sample. */
float get_lux_at(uint64_t timestamp);

#endif // RECENT_LUX_H
