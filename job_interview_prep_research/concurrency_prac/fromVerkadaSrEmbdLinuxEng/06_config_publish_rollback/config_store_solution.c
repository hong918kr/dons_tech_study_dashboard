/* config_store_solution.c — reference answer.
 *
 *   ./main.sh sol
 *
 * ---------------------------------------------------------------------------
 * WHY THIS SHAPE  (the 60 seconds of talking the interviewer is waiting for)
 *
 * 1. IMMUTABLE OBJECTS + POINTER SWAP.  A config is written once, by the
 *    fetch thread, before anybody can see it, and never touched again.  So a
 *    reader needs no lock to READ the fields — there is no writer to race
 *    with.  Publishing a new config is one pointer store, not a field-by-field
 *    update of shared state, so nobody can ever see half of an update (an old
 *    APN with a new upload period would silently mis-provision the modem).
 *
 * 2. REFERENCE COUNTING for reclamation.  The publisher cannot free the old
 *    object at swap time: readers may still be inside it.  Each object counts
 *    its owners (the `current` slot, each history slot, each borrowed
 *    reference) and frees itself when the count hits zero.  Whoever drops the
 *    last reference does the free — usually a reader, sometimes the publisher.
 *
 * 3. THE USE-AFTER-FREE WINDOW — the whole point of this problem.
 *       step A:   p = g_current;                  load the pointer
 *       step B:   atomic_fetch_add(&p->refs, 1);  claim a reference
 *    Between A and B the publisher can swap in a new config, drop the old
 *    one's last reference and free() it.  Step B then writes into freed
 *    memory and the reader goes on to read a dead object.  The race is tiny
 *    and it WILL ship.  The three known fixes:
 *
 *      - a tiny mutex around (A,B) and around the swap  <-- what we do
 *      - hazard pointers: publish "I am about to use p" into a per-thread
 *        slot, re-check p, and make the reclaimer scan all slots before it
 *        frees.  Lock-free, but needs thread registration and a scan.
 *      - RCU / epoch reclamation: readers mark a critical section, the
 *        reclaimer waits for a grace period.  Cheapest possible read side
 *        (Linux uses it for exactly this kind of config pointer), but it
 *        needs quiescent-state bookkeeping and deferred-free machinery.
 *
 *    On this gateway the lock wins: it is held for one load and one atomic
 *    add (tens of nanoseconds, no syscall on an uncontended pthread mutex),
 *    the publisher holds it only across a pointer store — never across the
 *    fetch, the malloc or the free — and it is ~10 lines instead of ~200 that
 *    a reviewer has to trust.  "Non-blocking" here means the reader never
 *    waits on the NETWORK; it does not mean lock-free.  If profiling ever
 *    showed this lock hot, RCU is the next step, not a smarter lock.
 *
 * 4. HISTORY = bounded ring of the last CONFIG_HISTORY_MAX versions, each one
 *    holding a reference.  Fixed memory, O(1) publish, O(N) lookup with N=8
 *    (a hash or a tree buys nothing at this size and costs allocation).
 *    Eviction is safe precisely BECAUSE of the refcount: evicting only drops
 *    the ring's reference, so a version a reader is still holding stays alive
 *    and is freed later, by that reader.  See the notes on hist_insert().
 *
 * 5. LOCK ORDER.  Two locks (publish + history) and we never hold both at
 *    once, so there is no order to get wrong and no deadlock to argue about.
 * ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/* the refcounted object                                                     */
/* ------------------------------------------------------------------------- */

struct cfg_node {
    struct Config cfg;       /* immutable once published; first member so we
                              * can go back and forth with a container cast */
    _Atomic int   refs;      /* owners: current slot + ring slots + borrows  */
};

static _Atomic size_t g_allocs;
static _Atomic size_t g_frees;

static struct cfg_node *node_new(const struct RawConfig *raw)
{
    struct cfg_node *n = malloc(sizeof *n);
    if (!n) return NULL;

    /* Fill it in COMPLETELY before anyone can reach it.  After the publish
     * below, these bytes are read-only for the rest of the object's life. */
    memset(n, 0, sizeof *n);
    memcpy(n->cfg.apn, raw->apn, sizeof n->cfg.apn);
    n->cfg.apn[sizeof n->cfg.apn - 1] = '\0';
    n->cfg.upload_period_ms = raw->upload_period_ms;
    n->cfg.wan_priority     = raw->wan_priority;
    n->cfg.version          = raw->version;
    n->cfg.checksum         = config_checksum(&n->cfg);

    atomic_store_explicit(&n->refs, 1, memory_order_relaxed);  /* caller owns it */
    atomic_fetch_add_explicit(&g_allocs, 1, memory_order_relaxed);
    return n;
}

static void node_ref(struct cfg_node *n)
{
    /* Always called either under g_pub_lock (reader path) or under
     * g_hist_lock (history path), i.e. we already know n is alive. */
    atomic_fetch_add_explicit(&n->refs, 1, memory_order_relaxed);
}

static void node_unref(struct cfg_node *n)
{
    if (!n) return;
    /* acq_rel: everything this thread did with the object must happen before
     * the free that some other thread may perform, and the thread that sees
     * the count reach 0 must see all of that. */
    if (atomic_fetch_sub_explicit(&n->refs, 1, memory_order_acq_rel) == 1) {
        atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
        /* Poison so a use-after-free shows up as a bad checksum in testing
         * even without a sanitizer. */
        memset(n, 0xA5, sizeof *n);
        free(n);
    }
}

/* ------------------------------------------------------------------------- */
/* current config                                                            */
/* ------------------------------------------------------------------------- */

/* g_pub_lock guards g_current.  It is held ONLY for:
 *   - reader: load g_current + bump its refcount   (closes the A/B window)
 *   - publisher: store g_current                    (one pointer write)
 * Never across cloudcfg_fetch(), malloc() or free(). */
static pthread_mutex_t g_pub_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cfg_node *g_current;            /* plain pointer: lock-guarded */

/* A separate atomic so config_version() needs neither the lock nor a
 * borrow/release pair.  It is written inside the lock, next to the swap, so it
 * can never disagree with g_current for more than the swap itself. */
static _Atomic uint32_t g_current_version;

/* Replace the current config.  Consumes one reference to `n` (the one the
 * caller owns) and returns the node that was displaced, still holding its
 * reference — the caller unrefs it OUTSIDE the lock so free() is never done
 * with the lock held. */
static void publish_current(struct cfg_node *n)
{
    struct cfg_node *old;

    pthread_mutex_lock(&g_pub_lock);
    old = g_current;
    g_current = n;
    atomic_store_explicit(&g_current_version, n ? n->cfg.version : 0u,
                          memory_order_relaxed);
    pthread_mutex_unlock(&g_pub_lock);

    node_unref(old);
}

const struct Config *config_acquire(void)
{
    struct cfg_node *n;

    pthread_mutex_lock(&g_pub_lock);
    n = g_current;
    if (n) node_ref(n);          /* load and claim are ONE atomic step now */
    pthread_mutex_unlock(&g_pub_lock);

    return n ? &n->cfg : NULL;
}

void config_release(const struct Config *c)
{
    if (!c) return;
    /* cfg is the first member, so the cast is exact.  Deliberately usable
     * after deinit: releasing only touches the object's own refcount. */
    node_unref((struct cfg_node *)(void *)c);
}

uint32_t config_version(void)
{
    return atomic_load_explicit(&g_current_version, memory_order_relaxed);
}

/* ------------------------------------------------------------------------- */
/* Part 2: bounded history ring + rollback                                   */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t g_hist_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cfg_node *g_hist[CONFIG_HISTORY_MAX];  /* oldest at g_hist_head */
static size_t g_hist_head;                           /* index of the oldest   */
static size_t g_hist_count;

/* Remember `n` as a known version.
 *
 * Eviction vs. borrowed references — the part worth saying out loud:
 *   evicting does NOT free anything.  It drops the RING's reference.  If a
 *   reader borrowed that version, or it is still the current config after a
 *   rollback, its count is still > 0 and the object lives until the last owner
 *   lets go.  That is what makes a fixed-size history safe to overwrite while
 *   readers roam around inside it.
 *
 *   "What if the ring is full of versions readers still hold?"  We still
 *   evict, and we still publish — configuration has to move forward, and a
 *   slow or stuck reader must never be able to block the publisher.  The only
 *   cost is that live memory is briefly CONFIG_HISTORY_MAX + (outstanding
 *   borrows) objects instead of CONFIG_HISTORY_MAX, which is bounded by the
 *   number of reader threads because each holds one reference at a time.
 *   The evicted-but-still-held version simply stops being findable:
 *   config_get_version() returns NULL for it while the reader that already
 *   has it keeps reading it happily.  The alternative — refusing to publish
 *   until a slot frees up (backpressure) — turns a reader bug into a gateway
 *   that can never be reprovisioned, which is worse.
 */
static void hist_insert(struct cfg_node *n)
{
    struct cfg_node *evicted = NULL;

    pthread_mutex_lock(&g_hist_lock);
    if (g_hist_count == CONFIG_HISTORY_MAX) {
        evicted = g_hist[g_hist_head];
        g_hist[g_hist_head] = NULL;
        g_hist_head = (g_hist_head + 1) % CONFIG_HISTORY_MAX;
        g_hist_count--;
    }
    g_hist[(g_hist_head + g_hist_count) % CONFIG_HISTORY_MAX] = n;
    g_hist_count++;
    node_ref(n);                 /* the ring is now an owner */
    pthread_mutex_unlock(&g_hist_lock);

    node_unref(evicted);         /* outside the lock: may free */
}

/* Borrow a remembered version.  Same load-then-ref window as config_acquire(),
 * closed the same way: the search and the refcount bump both happen under
 * g_hist_lock, and eviction (the only thing that can drop the ring's
 * reference) also takes that lock. */
const struct Config *config_get_version(uint32_t version)
{
    struct cfg_node *found = NULL;

    pthread_mutex_lock(&g_hist_lock);
    for (size_t i = 0; i < g_hist_count; i++) {
        struct cfg_node *n = g_hist[(g_hist_head + i) % CONFIG_HISTORY_MAX];
        if (n && n->cfg.version == version) {
            found = n;
            node_ref(found);
            break;
        }
    }
    pthread_mutex_unlock(&g_hist_lock);

    return found ? &found->cfg : NULL;
}

int config_rollback_to(uint32_t version)
{
    /* Reuse the borrow above: it hands us a reference that cannot be freed
     * under us, which is exactly the reference publish_current() wants to
     * consume.  Note we are NOT holding g_hist_lock when we take g_pub_lock —
     * the two locks are never nested, in either direction. */
    const struct Config *c = config_get_version(version);
    if (!c) return -1;

    publish_current((struct cfg_node *)(void *)c);

    /* No new history entry: a rollback republishes an existing version, it
     * does not invent one.  (A product might instead mint version N+1 with
     * the old payload so the audit log is append-only; then rollback would
     * also hist_insert().  Say which one you chose and why.) */
    return 0;
}

size_t config_history(uint32_t *versions_out, size_t max)
{
    size_t out = 0;

    if (!versions_out || max == 0) return 0;

    pthread_mutex_lock(&g_hist_lock);
    /* newest first: walk the ring backwards from the newest slot */
    for (size_t i = g_hist_count; i > 0 && out < max; i--) {
        struct cfg_node *n = g_hist[(g_hist_head + i - 1) % CONFIG_HISTORY_MAX];
        if (n) versions_out[out++] = n->cfg.version;
    }
    pthread_mutex_unlock(&g_hist_lock);

    /* Only version numbers leave the lock — no pointers, so the caller cannot
     * end up holding something we then evict.  If it wants the payload it asks
     * for it by version and gets a counted reference. */
    return out;
}

/* ------------------------------------------------------------------------- */
/* the one and only caller of cloudcfg_fetch()                               */
/* ------------------------------------------------------------------------- */

static pthread_t       g_thread;
static bool            g_thread_started;
static _Atomic bool    g_running;
static uint32_t        g_last_version;   /* fetch thread private */

/* Build + publish + remember.  Order matters: insert into the history FIRST,
 * then swap it in as current.  That way the current version is always findable
 * through config_get_version() (a reader that acquires the current config and
 * then asks for its version by number never gets a surprise NULL).  The other
 * order would leave a window where the newest version is current but unknown
 * to the history. */
static int publish_new(const struct RawConfig *raw)
{
    struct cfg_node *n = node_new(raw);
    if (!n) return -1;

    hist_insert(n);         /* ring takes its own reference   */
    publish_current(n);     /* consumes the creation reference */
    return 0;
}

static void *fetch_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        struct RawConfig raw;

        /* The ONLY place this is called, from the ONLY thread that calls it —
         * the vendor call is not reentrant.  Nothing is locked while we sit
         * here for up to 200 ms. */
        if (cloudcfg_fetch(&raw) != 0) {
            /* Link down.  There is nothing to publish: the last good config
             * stays exactly as it is, which is the whole reason the gateway
             * keeps working in a tunnel.  Back off a little so a burst of
             * failures does not become a busy loop. */
            usleep(2 * 1000);
            continue;
        }

        if (raw.version == g_last_version)
            continue;           /* same version: publishing again would churn
                                 * memory and wake every watcher for nothing */

        if (publish_new(&raw) == 0)
            g_last_version = raw.version;
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

int config_store_init(void)
{
    struct RawConfig raw;
    int tries;

    atomic_store(&g_allocs, 0);
    atomic_store(&g_frees, 0);
    g_last_version = 0;
    g_hist_head = g_hist_count = 0;
    memset(g_hist, 0, sizeof g_hist);

    /* Take the first config synchronously so that a caller which does
     * init(); config_acquire();  already has something real.  The alternative
     * (return immediately, let readers see NULL) pushes a "not provisioned
     * yet" state into every call site. */
    for (tries = 0; tries < 5; tries++) {
        if (cloudcfg_fetch(&raw) == 0) break;
        usleep(5 * 1000);
    }
    if (tries == 5) return -1;              /* never provisioned — fail loudly */

    if (publish_new(&raw) != 0) return -1;
    g_last_version = raw.version;

    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, NULL, fetch_main, NULL) != 0) {
        atomic_store(&g_running, false);
        return -1;
    }
    g_thread_started = true;
    return 0;
}

void config_store_deinit(void)
{
    if (g_thread_started) {
        atomic_store(&g_running, false);
        /* The thread may be parked inside cloudcfg_fetch() for up to one
         * vendor timeout; join waits that out.  No cancellation: we would be
         * killing a thread in the middle of vendor session state. */
        pthread_join(g_thread, NULL);
        g_thread_started = false;
    }

    /* Drop the store's own references.  Objects a reader still holds survive
     * until it calls config_release() — which is legal after deinit. */
    publish_current(NULL);

    pthread_mutex_lock(&g_hist_lock);
    struct cfg_node *dead[CONFIG_HISTORY_MAX];
    size_t n_dead = 0;
    for (size_t i = 0; i < g_hist_count; i++) {
        size_t idx = (g_hist_head + i) % CONFIG_HISTORY_MAX;
        if (g_hist[idx]) dead[n_dead++] = g_hist[idx];
        g_hist[idx] = NULL;
    }
    g_hist_head = g_hist_count = 0;
    pthread_mutex_unlock(&g_hist_lock);

    for (size_t i = 0; i < n_dead; i++)
        node_unref(dead[i]);     /* frees outside the lock */
}

size_t config_alloc_count(void) { return atomic_load(&g_allocs); }
size_t config_free_count(void)  { return atomic_load(&g_frees); }
