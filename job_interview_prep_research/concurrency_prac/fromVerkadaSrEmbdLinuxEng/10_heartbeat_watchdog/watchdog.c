/* watchdog.c — YOUR solution (practice stub).
 *
 *   ./main.sh           build main.c + this file and run the checks
 *   ./main.sh sol       same harness against watchdog_solution.c
 *
 * Read question_note first, answer its 10 questions on paper, then code.
 * As delivered this file compiles and the harness runs to completion — it just
 * fails almost everything. It must never hang.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hwwdt.h"
#include "watchdog.h"

/* TODO: hot state — one timestamp slot per worker.
 *  - what type makes wd_heartbeat() a single wait-free store?
 *  - eight workers storing every few ms: what does that do to ONE cache line?
 *    how big must a slot be, and how do you force that size/alignment in C11?
 *  - which fields must NOT live next to it (they are read by the monitor only)? */

/* TODO: cold state — the registration table (name / deadline / "already
 * reported" flag), the min-heap of next-deadlines + the id->heap-index array,
 * the monitor thread handle, a running flag, the time of the last kick.
 * All of this is off the hot path, so one mutex + one condvar is fine. */

/* TODO: heap primitives (sift_up / sift_down / insert / remove-by-id).
 * Key = "when does this worker's deadline expire".
 * Keep the invariant that makes lazy updates work:
 *     heap key <= that worker's TRUE deadline  (it is a lower bound)
 * and write down why repairing only the ROOT is enough to get the exact
 * global minimum. */

/* TODO: the monitor thread.
 *  1. timed wait until the earliest deadline (or until the next kick is due)
 *  2. find the overdue workers WITHOUT scanning all of them
 *  3. hwwdt_report_fault() each one EXACTLY ONCE; clear the flag if it returns
 *  4. hwwdt_kick() only if every registered worker is healthy
 *     (a kick on a plain timer defeats the watchdog — say why out loud)
 *  Portability: macOS has no pthread_condattr_setclock, so branch on __APPLE__
 *  between pthread_cond_timedwait_relative_np() and the absolute form. */
static void *monitor_main(void *arg)
{
    (void)arg;
    return NULL;
}

int wd_init(void)
{
    /* TODO: initialise the table/heap and start the monitor thread. */
    (void)monitor_main;
    return 0;
}

void wd_deinit(void)
{
    /* TODO: clear the running flag, wake the monitor, join it.
     * After this returns, no hwwdt_* call may happen any more. */
}

int wd_register(const char *name, uint64_t deadline_us)
{
    /* TODO: take a free slot, store name/deadline, insert into the heap.
     * Reject deadline_us == 0 and a full table with -1.
     * Give the new worker a grace period — otherwise it is "dead" before its
     * first heartbeat. */
    (void)name;
    (void)deadline_us;
    return -1;
}

void wd_unregister(int id)
{
    /* TODO: remove from the heap in O(log n) using the id->index array,
     * free the slot, ignore out-of-range ids. */
    (void)id;
}

/* Part 1 — the hot path. WAIT-FREE: no lock, no loop, no allocation. */
void wd_heartbeat(uint32_t worker_id)
{
    /* TODO: bounds-check, then ONE atomic store of the current time into this
     * worker's own slot. Which memory order, and why is relaxed almost enough?
     * Why must you NOT check "is this id registered" here? */
    (void)worker_id;
}

/* Part 2 — queries. */
int wd_next_deadline(uint64_t *when_us)
{
    /* TODO: the EARLIEST true deadline among all registered workers, so the
     * monitor can sleep exactly that long. Do it without scanning everyone:
     * repair the heap root lazily until its key is exact, then return it.
     * -1 if nothing is registered. */
    (void)when_us;
    return -1;
}

size_t wd_overdue(uint32_t *ids_out, size_t max)
{
    /* TODO: the workers whose deadline has already passed. Walk only the part
     * of the heap whose keys are in the past (why can you prune the rest?) and
     * re-check each candidate against its atomic timestamp, because a key may
     * be stale. Write at most `max` ids; return how many. */
    (void)ids_out;
    (void)max;
    return 0;
}
