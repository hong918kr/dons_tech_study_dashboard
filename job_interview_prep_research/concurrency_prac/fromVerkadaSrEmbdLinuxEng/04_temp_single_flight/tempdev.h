#ifndef TEMPDEV_H
#define TEMPDEV_H

#include <stdbool.h>
#include <stdint.h>

/* ==================================================================
 * Vendor API — GIVEN. Do not modify, do not re-implement.
 * ==================================================================
 *
 * The temperature probe on the air-quality sensor hangs off a slow I2C bus
 * behind a vendor blob. One conversion is a long, blocking transaction.
 *
 *   tempdev_read(&c)
 *      blocks for ~tempdev_read_us() microseconds (the real part takes
 *      ~600 ms; this harness scales it to 60 ms so the suite finishes in
 *      seconds — see question_note)
 *       0  -> *celsius holds a fresh reading
 *      -1  -> the device NACKed the transfer; *celsius is left untouched.
 *             The NACK is only discovered when the transaction times out,
 *             so a failing read blocks just as long as a good one.
 *
 *   STRICTLY ONE CALLER AT A TIME.
 *      Two threads inside tempdev_read() at the same moment interleave
 *      their I2C transactions and corrupt the bus. The fake implementation
 *      in main.c DETECTS concurrent entry and fails the whole run. There is
 *      no internal lock for you to lean on: serialising callers is your job.
 */
int tempdev_read(float *celsius);

/* Microsecond wall clock (gettimeofday). Same clock every timestamp in this
 * exercise uses. Not strictly monotonic — NTP can step it. */
uint64_t tempdev_now_us(void);

/* How long one tempdev_read() blocks for, in microseconds. */
unsigned tempdev_read_us(void);

/* ==================================================================
 * Harness hooks — NOT part of the vendor API.
 * temp_cache.c must never call anything below this line.
 * ==================================================================*/

/* Total tempdev_read() calls since process start. */
uint64_t tempdev_calls(void);

/* True if two threads were ever inside tempdev_read() simultaneously. */
bool tempdev_concurrent_entry_seen(void);

/* Highest number of threads seen inside tempdev_read() at once (want: 1). */
unsigned tempdev_max_concurrency(void);

/* Force every read to NACK (simulate a wedged bus). */
void tempdev_force_nack(bool on);

/* NACK every n-th call (0 = never). Deterministic error injection. */
void tempdev_set_nack_every(unsigned n);

/* Change the simulated transaction time. */
void tempdev_set_read_us(unsigned us);

#endif /* TEMPDEV_H */
