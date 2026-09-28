/* audit_log_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol
 *
 * SHAPE
 *   door 0 ─ thread ─┐
 *   door 1 ─ thread ─┤  audit_submit()      ┌──────────────┐   audit_store_write()
 *   door 2 ─ thread ─┼──────────────────────► bounded queue ├──► logger thread ──► flash
 *   door 3 ─ thread ─┘   (mutex + 2 cv)     │  16 slots     │        │
 *                                           └──────────────┘        ├─► dedupe hash map
 *                                                                   └─► per-door ring
 *
 * WHY 4 PRODUCERS. reader_wait_badge() has per-door state: safe for different
 * doors, undefined for the same door twice. That is the whole thread model —
 * exactly one thread per door, and no thread may serve two doors (it would
 * block on door 0 while someone badges at door 1). So: 4 producers.
 *
 * WHY 1 CONSUMER. audit_store_write() owns a single append cursor and costs
 * ~8 ms. One writer thread keeps the store's contract and makes the log order
 * well defined; the queue absorbs bursts so a door thread never waits on flash.
 *
 * WHY PER-DOOR ORDER SURVIVES BUT GLOBAL ORDER DOES NOT.
 *   Per door: one thread reads that door and pushes under the queue mutex, so
 *   its events enter the FIFO in reader order and the single consumer pops in
 *   FIFO order. Ordering is transitive along "same producer -> same FIFO ->
 *   one consumer", so a door's events can only be thinned by drops, never
 *   reordered.
 *   Globally: two doors run concurrently, and which of two nearly simultaneous
 *   badges grabs the queue mutex first is up to the scheduler. Timestamps are
 *   taken inside the vendor before that race, so a record with a later
 *   timestamp can legitimately be logged first. Promising a global order would
 *   mean a total order over 4 independent readers — that needs either one
 *   thread doing all the reading (impossible, the calls block) or a sort with
 *   a watermark delay. Anti-passback does not need it, so we do not pay for it.
 *
 * WHY close() BROADCASTS INSTEAD OF SIGNALLING.
 *   At shutdown there can be up to 4 producers asleep on not_full and the
 *   logger asleep on not_empty. `running` is the predicate all five re-check
 *   when they wake. pthread_cond_signal wakes exactly ONE waiter, so three
 *   producers would keep sleeping until their own timeout expired (and an
 *   unbounded wait would never wake at all) while deinit sits in
 *   pthread_join — a self-inflicted stall, or a deadlock. A state change that
 *   invalidates the predicate for EVERY waiter must be a broadcast. During
 *   normal operation the opposite is true: one pop frees exactly one slot, so
 *   signal is correct there and avoids a thundering herd.
 *
 * PART 2 CHOICES
 *   Duplicate suppression: open-addressing hash map keyed by badge_id, fixed
 *   1024 slots, no malloc, linear probing inside a 16-slot window. See the
 *   block comment above the map for collisions, the load-factor budget, the
 *   no-tombstone argument and why eviction is lazy rather than swept.
 *   Recent history: one fixed 16-entry ring per door, O(1) append, O(k) read,
 *   and each door's ring is naturally ordered because one thread appends to it.
 *
 * Both Part 2 structures live under one mutex that is held only for O(1) /
 * O(16) work — never across a vendor call. That is what keeps the getters at
 * ~1 us while the store is busy for 8 ms at a time.
 */
/* glibc hides usleep() and clock_gettime() when the compiler is in strict ISO C
 * mode, which -std=c11 is, so ask for the full set on Linux. NOT on Apple: there
 * _XOPEN_SOURCE / _POSIX_C_SOURCE would *hide* the _np condvar call below.
 * Must come before any #include. */
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audit_log.h"
#include "reader.h"

#define READ_ERROR_BACKOFF_US 1000u

_Static_assert((AUDIT_QUEUE_CAP & (AUDIT_QUEUE_CAP - 1u)) == 0u, "queue cap must be 2^n");
_Static_assert((AUDIT_DEDUPE_CAP & (AUDIT_DEDUPE_CAP - 1u)) == 0u, "dedupe cap must be 2^n");
#define QMASK (AUDIT_QUEUE_CAP - 1u)
#define HMASK (AUDIT_DEDUPE_CAP - 1u)

/* ------------------------------------------------------------------ */
/* portable bounded wait                                               */
/* ------------------------------------------------------------------ */

/* macOS has no pthread_condattr_setclock, Linux has no
 * pthread_cond_timedwait_relative_np: branch once, here. */
static int cv_wait_us(pthread_cond_t *cv, pthread_mutex_t *m, uint64_t us)
{
#ifdef __APPLE__
    struct timespec rel;
    rel.tv_sec = (time_t)(us / 1000000ull);
    rel.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    return pthread_cond_timedwait_relative_np(cv, m, &rel);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(us / 1000000ull);
    ts.tv_nsec += (long)((us % 1000000ull) * 1000ull);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec += 1;
    }
    return pthread_cond_timedwait(cv, m, &ts);
#endif
}

/* ------------------------------------------------------------------ */
/* Part 1: bounded MPSC queue                                          */
/* ------------------------------------------------------------------ */

static struct {
    pthread_mutex_t m;
    pthread_cond_t  not_empty;      /* logger waits here                      */
    pthread_cond_t  not_full;       /* door threads wait here                 */
    struct Badge    slot[AUDIT_QUEUE_CAP];
    unsigned        head, tail;     /* free-running; count = head - tail      */
    bool            running;
} q = {.m = PTHREAD_MUTEX_INITIALIZER,
       .not_empty = PTHREAD_COND_INITIALIZER,
       .not_full = PTHREAD_COND_INITIALIZER};

/* Counters are atomics, not mutex state: the getters must never queue behind
 * the logger, and a monotonic counter needs no consistency with anything else. */
static _Atomic uint64_t g_logged;
static _Atomic uint64_t g_dropped;

static _Atomic bool g_running;           /* producer loops watch this         */
static bool         g_started;           /* init/deinit only, one thread      */
static pthread_t    g_logger;
static pthread_t    g_producer[AC42_DOORS];
static int          g_producers_made;

uint64_t audit_logged(void) { return atomic_load_explicit(&g_logged, memory_order_relaxed); }
uint64_t audit_dropped(void) { return atomic_load_explicit(&g_dropped, memory_order_relaxed); }

int audit_submit(const struct Badge *b)
{
    if (b == NULL || b->door >= AC42_DOORS) {
        atomic_fetch_add_explicit(&g_dropped, 1u, memory_order_relaxed);
        return -1;
    }

    pthread_mutex_lock(&q.m);

    /* Bounded wait, not an unbounded one: a door thread that sleeps here is a
     * door that stops reading badges. Better to lose an audit record (counted)
     * than to stop noticing people. */
    uint64_t deadline = reader_now_us() + AUDIT_SUBMIT_WAIT_US;
    while (q.running && (unsigned)(q.head - q.tail) == AUDIT_QUEUE_CAP) {
        uint64_t now = reader_now_us();
        if (now >= deadline)
            break;                                  /* deadline, not spurious */
        cv_wait_us(&q.not_full, &q.m, deadline - now);
    }

    if (!q.running || (unsigned)(q.head - q.tail) == AUDIT_QUEUE_CAP) {
        pthread_mutex_unlock(&q.m);
        atomic_fetch_add_explicit(&g_dropped, 1u, memory_order_relaxed);
        return -1;
    }

    q.slot[q.head & QMASK] = *b;
    q.head++;
    /* One slot filled wakes exactly one consumer: signal, not broadcast. */
    pthread_cond_signal(&q.not_empty);
    pthread_mutex_unlock(&q.m);
    return 0;
}

/* Pop one event. false = the queue is empty and closed, so the logger is done. */
static bool queue_pop(struct Badge *out)
{
    pthread_mutex_lock(&q.m);
    while (q.head == q.tail && q.running)
        pthread_cond_wait(&q.not_empty, &q.m);      /* predicate in a loop */
    if (q.head == q.tail) {                         /* closed and drained */
        pthread_mutex_unlock(&q.m);
        return false;
    }
    *out = q.slot[q.tail & QMASK];
    q.tail++;
    pthread_cond_signal(&q.not_full);               /* one slot freed */
    pthread_mutex_unlock(&q.m);
    return true;
}

/* ------------------------------------------------------------------ */
/* Part 2: dedupe hash map + per-door recent ring                      */
/* ------------------------------------------------------------------ */

/* DEDUPE TABLE — open addressing, fixed 1024 slots, no malloc.
 *
 * Collisions: badge_id is a dense counter from the card printer, so id & mask
 * would put a whole batch of cards in one place. Multiply by the 2^32 golden
 * ratio first (Knuth/Fibonacci hashing) and take the low bits: consecutive ids
 * land 433 slots apart, and 433 is coprime with 1024, so a run of cards is a
 * permutation of the table instead of a cluster. Collisions are then resolved
 * by LINEAR PROBING inside a fixed window of 16 slots: 16 entries are 256
 * bytes, four cache lines walked forwards, which the prefetcher loves. Chaining
 * would need a free list and pointer chasing; robin-hood would keep probes even
 * shorter but has to move entries around, and the win does not show up at a
 * load factor we control. The window is what makes the lookup O(1) in the WORST
 * case, not just on average — that matters more here than the average, because
 * this call sits on the door-open path.
 *
 * Load factor: the budget is AUDIT_DEDUPE_LOAD_LIMIT = 0.75 * CAP = 768 live
 * slots, i.e. 768 distinct badges seen within the retention window. It is a
 * SIZING RULE, not something the table enforces: with no malloc there is no
 * rehash to grow into. Above it, probe windows start coming up full and inserts
 * have to overwrite a live entry; audit_dedupe_evictions() counts exactly that,
 * so the fleet dashboard sees "this door controller needs a bigger table"
 * instead of silently losing anti-passback.
 *
 * NO TOMBSTONES. Deleting from a linear-probed table normally needs a tombstone
 * so later probes do not stop early. We never delete: a slot that ages out is
 * REUSED IN PLACE by the next insert that probes over it, so a slot goes from
 * empty to occupied once and never back. There is no "deleted" state to mark,
 * and no slow accumulation of tombstones that a real implementation would have
 * to rehash away.
 *   The price: an insert may pick an aged-out slot that sits AFTER an empty one
 *   in the same window, so "stop probing at the first empty slot" would be
 *   wrong. Lookups therefore always walk the full 16-slot window. Bounded,
 *   branch-predictable, and it buys the no-tombstone invariant.
 *
 * LAZY EVICTION, NOT A BACKGROUND SWEEP. An entry is dead once
 * now - last_seen > AUDIT_DEDUPE_RETENTION_US, but nobody needs to be told:
 *   - a lookup on a dead entry already answers "not a duplicate", because the
 *     anti-passback window is always smaller than the retention window, so a
 *     stale entry cannot produce a false positive;
 *   - the only cost of leaving it there is a slot, and a slot is only wanted by
 *     an insert — which is exactly the code that walks that window anyway.
 *   So eviction is free when folded into insert: same cache lines, zero extra
 *   passes. A sweeper thread would instead wake up periodically to touch all
 *   16 KB of the table (cold cache, on a CPU shared with the RTOS-ish door
 *   logic), take the same mutex the door path needs, and add a thread whose
 *   only job is deleting things nobody is looking at. On an access controller
 *   with four doors that is pure jitter for no benefit. A sweeper earns its
 *   keep only if you must reclaim memory promptly or iterate the live set —
 *   neither applies to a fixed array.
 */
typedef struct {
    uint32_t id;
    uint64_t last_seen;     /* newest logged sighting, vendor clock */
    bool     used;
} dedupe_slot_t;

static struct {
    pthread_mutex_t m;
    dedupe_slot_t   map[AUDIT_DEDUPE_CAP];
    size_t          live;
    uint64_t        evictions;      /* live entry overwritten: table too small */
    uint64_t        stale_reuse;    /* aged-out slot recycled                  */
    struct Badge    ring[AC42_DOORS][AUDIT_RECENT_PER_DOOR];
    uint64_t        rcount[AC42_DOORS];   /* free-running append counter       */
} idx = {.m = PTHREAD_MUTEX_INITIALIZER};

static uint32_t dedupe_home(uint32_t id)
{
    return (uint32_t)(id * 2654435761u) & HMASK;     /* Fibonacci hashing */
}

static bool slot_is_stale(const dedupe_slot_t *s, uint64_t now)
{
    /* Written as an addition so a clock that stepped backwards (gettimeofday is
     * not monotonic) cannot underflow into "stale". */
    return s->last_seen + AUDIT_DEDUPE_RETENTION_US <= now;
}

/* Called by the logger thread only, with idx.m held. */
static void dedupe_note(uint32_t id, uint64_t ts)
{
    uint32_t home = dedupe_home(id);
    long first_stale = -1, first_empty = -1, oldest = -1;

    for (uint32_t i = 0; i < AUDIT_DEDUPE_WINDOW; i++) {
        uint32_t s = (home + i) & HMASK;
        dedupe_slot_t *e = &idx.map[s];

        if (!e->used) {
            if (first_empty < 0)
                first_empty = (long)s;
            continue;
        }
        if (e->id == id) {                       /* the common case: a re-badge */
            if (ts > e->last_seen)
                e->last_seen = ts;               /* keep the NEWEST sighting    */
            return;
        }
        if (first_stale < 0 && slot_is_stale(e, ts))
            first_stale = (long)s;
        if (oldest < 0 || e->last_seen < idx.map[oldest].last_seen)
            oldest = (long)s;
    }

    long victim;
    if (first_stale >= 0) {
        victim = first_stale;                    /* lazy age eviction           */
        idx.stale_reuse++;
    } else if (first_empty >= 0) {
        victim = first_empty;
        idx.live++;
    } else {
        /* Window full of live badges: the table is over budget. Drop the
         * least recently seen one — it is the one whose anti-passback window
         * is closest to having expired anyway — and say so in the counter. */
        victim = oldest;
        idx.evictions++;
    }
    idx.map[victim].id = id;
    idx.map[victim].last_seen = ts;
    idx.map[victim].used = true;
}

/* Called by the logger thread only. One short critical section for both
 * structures: the getters can never be stuck behind the audit store, because
 * this lock is never held across audit_store_write(). */
static void index_record(const struct Badge *b)
{
    pthread_mutex_lock(&idx.m);
    uint8_t d = b->door;
    idx.ring[d][idx.rcount[d] % AUDIT_RECENT_PER_DOOR] = *b;
    idx.rcount[d]++;
    dedupe_note(b->badge_id, b->timestamp);
    pthread_mutex_unlock(&idx.m);
}

int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us)
{
    uint32_t home = dedupe_home(badge_id);
    int dup = 0;

    pthread_mutex_lock(&idx.m);
    for (uint32_t i = 0; i < AUDIT_DEDUPE_WINDOW; i++) {
        /* No early exit on an empty slot — see the NO TOMBSTONES note. */
        const dedupe_slot_t *e = &idx.map[(home + i) & HMASK];
        if (e->used && e->id == badge_id) {
            uint64_t t = e->last_seen;
            dup = (t <= now_us && now_us - t <= window_us) ? 1 : 0;
            break;
        }
    }
    pthread_mutex_unlock(&idx.m);
    return dup;
}

size_t audit_recent_for_door(uint8_t door, struct Badge *out, size_t max)
{
    if (door >= AC42_DOORS || out == NULL || max == 0)
        return 0;

    pthread_mutex_lock(&idx.m);
    uint64_t have = idx.rcount[door];
    if (have > AUDIT_RECENT_PER_DOOR)
        have = AUDIT_RECENT_PER_DOOR;
    size_t n = (max < (size_t)have) ? max : (size_t)have;
    for (size_t i = 0; i < n; i++)      /* newest first */
        out[i] = idx.ring[door][(idx.rcount[door] - 1u - i) % AUDIT_RECENT_PER_DOOR];
    pthread_mutex_unlock(&idx.m);
    return n;
}

size_t   audit_dedupe_live(void)
{
    pthread_mutex_lock(&idx.m);
    size_t v = idx.live;
    pthread_mutex_unlock(&idx.m);
    return v;
}

uint64_t audit_dedupe_evictions(void)
{
    pthread_mutex_lock(&idx.m);
    uint64_t v = idx.evictions;
    pthread_mutex_unlock(&idx.m);
    return v;
}

uint64_t audit_dedupe_stale_reuse(void)
{
    pthread_mutex_lock(&idx.m);
    uint64_t v = idx.stale_reuse;
    pthread_mutex_unlock(&idx.m);
    return v;
}

/* ------------------------------------------------------------------ */
/* threads                                                             */
/* ------------------------------------------------------------------ */

/* One per door, because reader_wait_badge() is per-door state and blocks. */
static void *producer_main(void *arg)
{
    uint8_t door = (uint8_t)(uintptr_t)arg;

    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        struct Badge b;
        if (reader_wait_badge(door, &b) != 0) {
            /* -1 does not say whether this was a transient read error or our
             * own reader_cancel(). Our shutdown flag is the authority. */
            if (!atomic_load_explicit(&g_running, memory_order_acquire))
                break;
            usleep(READ_ERROR_BACKOFF_US);      /* do not spin on a sick reader */
            continue;
        }
        (void)audit_submit(&b);                 /* a drop is counted inside */
    }
    return NULL;
}

/* The only caller of audit_store_write(), which is single-threaded and slow. */
static void *logger_main(void *arg)
{
    (void)arg;
    struct Badge b;
    while (queue_pop(&b)) {
        /* Index first: anti-passback must not be delayed by flash I/O. */
        index_record(&b);
        (void)audit_store_write(&b);            /* no lock held here */
        atomic_fetch_add_explicit(&g_logged, 1u, memory_order_relaxed);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int audit_init(void)
{
    if (g_started)
        return 0;

    pthread_mutex_lock(&q.m);
    q.head = q.tail = 0;
    q.running = true;
    pthread_mutex_unlock(&q.m);
    atomic_store_explicit(&g_running, true, memory_order_release);

    if (pthread_create(&g_logger, NULL, logger_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        q.running = false;
        return -1;
    }

    g_producers_made = 0;
    for (int d = 0; d < AC42_DOORS; d++) {
        if (pthread_create(&g_producer[d], NULL, producer_main,
                           (void *)(uintptr_t)d) != 0)
            break;                              /* partial start: deinit cleans up */
        g_producers_made++;
    }
    g_started = true;
    if (g_producers_made != AC42_DOORS) {
        audit_deinit();
        return -1;
    }
    return 0;
}

void audit_deinit(void)
{
    if (!g_started)
        return;

    /* 1. tell the producer loops to stop */
    atomic_store_explicit(&g_running, false, memory_order_release);

    /* 2. unblock the ones parked inside the vendor call. Without this, deinit
     *    would take as long as the longest badge wait — minutes, in the field. */
    for (int d = 0; d < AC42_DOORS; d++)
        reader_cancel((uint8_t)d);

    /* 3. close the queue and wake EVERY waiter: up to 4 producers on not_full
     *    and the logger on not_empty all have to re-check `running`. */
    pthread_mutex_lock(&q.m);
    q.running = false;
    pthread_cond_broadcast(&q.not_full);
    pthread_cond_broadcast(&q.not_empty);
    pthread_mutex_unlock(&q.m);

    /* 4. producers first, so nothing can be submitted after this point */
    for (int d = 0; d < g_producers_made; d++)
        pthread_join(g_producer[d], NULL);

    /* 5. the logger drains whatever is still queued (those badges are already
     *    read from the readers; dropping them now would lose audit records),
     *    then sees empty+closed and returns. */
    pthread_join(g_logger, NULL);

    g_producers_made = 0;
    g_started = false;
}
