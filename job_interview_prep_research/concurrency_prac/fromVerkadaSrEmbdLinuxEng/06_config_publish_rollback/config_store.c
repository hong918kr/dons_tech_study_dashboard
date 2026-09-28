/* config_store.c — YOUR solution (practice stub).
 *
 *   ./main.sh           build main.c + this file and run the checks
 *   ./main.sh sol       same harness against config_store_solution.c
 *
 * Read question_note first.  Answer its 10 questions on paper, then code.
 * As shipped this file compiles, runs and FAILS most checks — it must never
 * hang.
 *
 * Suggested order:
 *   1. the refcounted node + alloc/free counters
 *   2. publish path (fetch thread -> new object -> swap current)
 *   3. config_acquire / config_release  <- think hard about the race here
 *   4. history ring, config_get_version, config_history
 *   5. config_rollback_to
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cloudcfg.h"
#include "config_store.h"

/* TODO: the object readers hand around.
 *  - a struct Config the publisher fills in BEFORE anyone can see it, and
 *    nobody ever modifies afterwards (why does immutability remove the need
 *    for a lock on the read side?)
 *  - a refcount: how many owners does one config have?  Name them.
 *  - remember to set cfg.checksum = config_checksum(&cfg) when you build it.
 */

/* TODO: counters behind config_alloc_count() / config_free_count(). */

/* TODO: the current-config slot.
 *  - one pointer, swapped atomically-as-seen-by-readers on publish
 *  - plus whatever you need so config_version() does not have to borrow
 */

/* TODO: the history.
 *  - fixed ring of CONFIG_HISTORY_MAX entries, newest published last
 *  - each slot is an OWNER of its config; what does eviction have to do?
 *  - what must NOT happen when you evict a version a reader is still using?
 */

/* TODO: the only place cloudcfg_fetch() may be called.
 * Loop while running:
 *    fetch fails (-1)     -> link down in a burst: keep the last good config,
 *                            back off briefly, retry.  Publish nothing.
 *    same version as last -> nothing changed: publish nothing.
 *    new version          -> build an immutable config, remember it in the
 *                            history, make it current, drop the reference the
 *                            old current config was holding.
 * Nothing may be locked while you are inside cloudcfg_fetch().
 */
static void *fetch_main(void *arg)
{
    (void)arg;
    return NULL;
}

int config_store_init(void)
{
    /* TODO:
     *  1. fetch the first config synchronously (retry a few times if the link
     *     is down) so a getter called right after init already has a value
     *  2. start the fetch thread
     *  return -1 if no config could be obtained at all */
    (void)fetch_main;
    return 0;
}

void config_store_deinit(void)
{
    /* TODO: stop the fetch thread and join it (it may be parked in the vendor
     * call), then drop the store's own references: the current slot and every
     * history slot.  Anything a reader still holds must stay alive. */
}

/* --- Part 1 ------------------------------------------------------------- */

const struct Config *config_acquire(void)
{
    /* TODO: borrow the current config without ever touching the network.
     *
     * The trap: reading the pointer and claiming a reference on what it points
     * at are TWO steps.  What can the publisher do in between?  What is the
     * cheapest correct way to make those two steps one?  Name the lock-free
     * alternatives and say why you did not use them. */
    return NULL;
}

void config_release(const struct Config *c)
{
    /* TODO: drop one reference; free when the last owner leaves.
     * Must still work after config_store_deinit(). */
    (void)c;
}

uint32_t config_version(void)
{
    /* TODO: current version, no borrow, no blocking. 0 if there is none. */
    return 0;
}

/* --- Part 2 ------------------------------------------------------------- */

const struct Config *config_get_version(uint32_t version)
{
    /* TODO: search the history ring; return a COUNTED reference so eviction
     * cannot free it under the caller.  NULL if unknown or already evicted. */
    (void)version;
    return NULL;
}

int config_rollback_to(uint32_t version)
{
    /* TODO: find that version, then make it current again.
     * Careful: whose reference does the current slot end up holding?
     * Careful: do not hold two locks at once. */
    (void)version;
    return -1;
}

size_t config_history(uint32_t *versions_out, size_t max)
{
    /* TODO: newest first, at most `max`, return how many you wrote.
     * Why is it safer to hand back version numbers than pointers? */
    (void)versions_out;
    (void)max;
    return 0;
}

/* --- accounting --------------------------------------------------------- */

size_t config_alloc_count(void) { return 0; }
size_t config_free_count(void)  { return 0; }
