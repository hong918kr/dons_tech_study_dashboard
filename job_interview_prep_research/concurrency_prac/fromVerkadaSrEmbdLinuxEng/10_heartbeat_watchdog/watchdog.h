#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stddef.h>
#include <stdint.h>

/* watchdog.h — the API the harness calls (you implement watchdog.c).
 *
 * Shape of the problem (note it is the MIRROR IMAGE of the usual one):
 *   MANY WRITERS (every worker thread calls wd_heartbeat on its hot path)
 *   ONE READER   (the monitor thread, plus the two query functions below).
 */

#define WD_MAX_WORKERS 32
#define WD_NAME_MAX    24

/* Start the monitor thread. Call once, before anything else. 0 on success. */
int  wd_init(void);

/* Stop the monitor thread and join it. After this no more hwwdt_kick() /
 * hwwdt_report_fault() calls may happen. */
void wd_deinit(void);

/* Register a worker. `deadline_us` is how long this worker may go without a
 * heartbeat before it is considered dead; it must be > 0.
 * Returns a worker id in [0, WD_MAX_WORKERS) or -1 (table full / bad args).
 * A freshly registered worker counts as having just checked in. */
int  wd_register(const char *name, uint64_t deadline_us);

/* Remove a worker. Safe to call while the monitor is running. Ignores bad ids. */
void wd_unregister(int id);

/* "I am alive." Called from every worker's hot path, so it must be WAIT-FREE:
 * no lock, no loop, no allocation, no system call that can block.
 * Safe to call with an id that is not registered (it is simply ignored). */
void wd_heartbeat(uint32_t worker_id);

/* Part 2: the earliest deadline among all registered workers, i.e. the instant
 * at which the monitor next has something to do. Lets the monitor sleep exactly
 * that long instead of polling.
 *   returns 0 and writes *when_us  (absolute, hwwdt_now_us() clock)
 *   returns -1 if no worker is registered (*when_us untouched)
 * If a worker is already overdue, the value returned is in the past. */
int  wd_next_deadline(uint64_t *when_us);

/* Part 2: ids of the workers that are overdue right now. Writes at most `max`
 * ids (unspecified order) and returns how many it wrote. */
size_t wd_overdue(uint32_t *ids_out, size_t max);

#endif /* WATCHDOG_H */
