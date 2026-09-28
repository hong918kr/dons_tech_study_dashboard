/* watchdog_solution.c — reference solution (Part 1 concurrency + Part 2 min-heap).
 *
 *   ./main.sh sol
 *
 * ============================== WHY THIS SHAPE ==============================
 *
 * This problem is the mirror image of the usual "wrap a blocking vendor call"
 * question: there are MANY WRITERS (every worker thread) and exactly ONE READER
 * (the monitor thread). So the cost has to be pushed onto the reader.
 *
 * 1. WHY HEARTBEATS ARE STORES AND NEVER LOCKS
 *    wd_heartbeat() sits on the hot path of the modem manager, the uploader,
 *    the PoE controller... It is called far more often than it is read. If it
 *    took a mutex then (a) every worker would serialise on one cache line and
 *    one futex, so the watchdog itself would become the scalability bottleneck,
 *    and (b) a worker could be *blocked inside the watchdog* — priority
 *    inversion against the very thread whose liveness we are measuring, and if
 *    the lock holder is descheduled the whole fleet stalls. A heartbeat is
 *    therefore ONE relaxed/release atomic store of a 64-bit timestamp into a
 *    slot owned by that worker: wait-free (bounded number of instructions, no
 *    matter what any other thread does), no allocation, no syscall.
 *    The only "call" inside it is the clock read, which is a vDSO/commpage read
 *    on Linux/macOS — a few nanoseconds of user-space work, no kernel entry.
 *
 * 2. WHY THE MONITOR IS THE ONLY READER
 *    Deciding "is this worker late?" needs the *set* of workers, the deadline
 *    table and the fault bookkeeping ("report each death exactly once"). That is
 *    mutable shared state, so it lives behind one mutex — and it is only ever
 *    touched off the hot path: by the monitor once per tick, and by
 *    register/unregister/the two query functions. Writers never take that lock,
 *    so a worker can never be delayed by the monitor and vice versa.
 *    Publication is release/acquire: the worker's store is a release, the
 *    monitor's load is an acquire, which is all the ordering we need because the
 *    timestamp is self-describing (nothing else is published with it).
 *
 * 3. FALSE SHARING / CACHE-LINE PADDING
 *    32 naked `_Atomic uint64_t` would pack 8 per 64-byte line. Eight workers
 *    storing every millisecond would then ping one line between cores: every
 *    store invalidates the other seven workers' copies (MESI RFO traffic), and
 *    the "independent" heartbeats cost as much as a shared lock even though
 *    they are logically disjoint. So each worker's timestamp gets its own
 *    64-byte, 64-byte-aligned slot (wd_hot_t below) and the cold fields (name,
 *    deadline, fault flag) live in a *separate* array that the writers never
 *    touch. The harness measures this: heartbeats from 4 threads versus the same
 *    4 threads going through one mutex.
 *
 * 4. THE "KICK ONLY IF EVERY WORKER IS HEALTHY" RULE
 *    The classic bug is a thread that does `for(;;){ hwwdt_kick(); sleep(1); }`.
 *    That thread keeps the board alive *precisely while the rest of the system
 *    is dead* — the hardware watchdog has been defeated and turned into a
 *    liveness lie. A hardware watchdog is only worth having if the kick is
 *    CONDITIONAL: the monitor kicks only when every registered worker has
 *    checked in inside its own deadline. If anything is overdue the monitor
 *    reports the fault and then simply stops kicking, and the SoC resets the
 *    board HWWDT_TIMEOUT_US later. Corollary: the monitor must itself be the
 *    only kicker (hwwdt_kick is an MMIO write with no synchronisation of its
 *    own), and the monitor's own liveness is covered by the hardware — if the
 *    monitor dies, nobody kicks, the board resets. That is the desired failure
 *    mode, which is why it is the monitor that owns the kick and not a timer.
 *
 * 5. PART 2: MIN-HEAP OVER NEXT-DEADLINE, WITH LAZY UPDATES
 *    A monitor that scans all N workers every tick is O(N) per tick and has to
 *    pick a polling period out of thin air. Instead keep a binary min-heap
 *    keyed by "when does this worker's deadline expire", plus g_pos[] so a
 *    worker's heap slot is found in O(1):
 *        register    O(log n)   insert
 *        unregister  O(log n)   remove at known position
 *        heartbeat   O(1)       ... and it does NOT touch the heap at all
 *        next        O(log n)   amortised: refresh the root, then read it
 *        overdue     O(k)       k = entries whose key is already in the past
 *    The heartbeat MUST NOT fix the heap: sifting is O(log n) *and* needs the
 *    lock, which would put a lock back on the hot path (see 1). So the key
 *    stored in the heap is allowed to be stale — but only ever in one direction:
 *
 *        INVARIANT:  heap key <= that worker's true deadline
 *                    (last_seen only moves forward, so the true deadline only
 *                     moves forward; the key is a LOWER BOUND.)
 *
 *    That one-sidedness is what makes lazy updates correct. To answer "what is
 *    the earliest true deadline?" you only have to repair the ROOT:
 *        while (true_deadline(root) > root.key) { root.key = true; sift_down; }
 *    When it stops, the root's key is exact, and for every other entry i the
 *    heap property gives key_i >= key_root and the invariant gives
 *    true_i >= key_i >= key_root. So the repaired root IS the global minimum —
 *    without looking at anybody else. Each repair pushes one entry down
 *    (O(log n)) and can only happen once per heartbeat, so the amortised cost is
 *    tiny and it is paid by the reader, never by the worker.
 *
 *    ALTERNATIVE: a timer wheel (hashed buckets of `deadline / tick`, as in the
 *    Linux kernel's classic timer_list or in a network stack's retransmit
 *    timers). It gives O(1) insert/remove/expire instead of O(log n) and is the
 *    better choice when there are thousands of timers, when they are constantly
 *    rearmed (exactly what heartbeats are!) and when a tick of slack is
 *    acceptable. Its costs: memory proportional to the wheel, quantisation to
 *    the tick, cascading between wheel levels, and an awkward answer to "what is
 *    the exact next expiry?" — you have to walk buckets. Here n <= 32, we want
 *    an exact next-deadline so the monitor can sleep precisely, and rearming is
 *    free because of the lazy trick, so the heap wins on simplicity. Third
 *    option, a sorted list: O(1) peek but O(n) insert and horrible under churn.
 *
 * 6. CLOCK CAVEAT
 *    hwwdt_now_us() is gettimeofday(), i.e. CLOCK_REALTIME, because that is the
 *    clock a default-initialised condvar waits on and macOS has no
 *    pthread_condattr_setclock(). It is NOT monotonic: an NTP step backwards
 *    makes every deadline look further away (we under-report deaths for the
 *    length of the step) and a step forwards can declare every worker dead at
 *    once and stop the kicks, i.e. reset the board. In production this belongs
 *    on CLOCK_MONOTONIC with a condvar created with that clock (Linux) or with
 *    pthread_cond_timedwait_relative_np() plus a monotonic clock (macOS); the
 *    relative wait below is already immune, only the stored timestamps are not.
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

/* Kick well inside the hardware window so one late wake-up cannot reset the
 * board. HWWDT_TIMEOUT_US/4 = 25 ms here (500 ms on the real 2 s part). */
#define WD_KICK_PERIOD_US (HWWDT_TIMEOUT_US / 4)

/* While something is overdue we cannot kick, but we still want to notice the
 * worker coming back quickly, so re-check on this period instead of spinning. */
#define WD_RECHECK_US 10000ull

#define WD_CACHELINE 64

/* ------------------------------------------------------------------ */
/* hot state: one cache line per worker, written by that worker only    */
/* ------------------------------------------------------------------ */

typedef struct {
    _Alignas(WD_CACHELINE) _Atomic uint64_t last_seen_us;
    char pad[WD_CACHELINE - sizeof(_Atomic uint64_t)];   /* no false sharing */
} wd_hot_t;

static wd_hot_t g_hot[WD_MAX_WORKERS];

_Static_assert(sizeof(wd_hot_t) == WD_CACHELINE, "slot must be exactly one line");

/* ------------------------------------------------------------------ */
/* cold state: monitor + register/unregister + queries, under g_lock    */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     in_use;
    bool     faulted;        /* we have already reported this death once */
    uint64_t deadline_us;
    char     name[WD_NAME_MAX];
} wd_cold_t;

static wd_cold_t g_cold[WD_MAX_WORKERS];

/* min-heap of (worker id, lower bound on its next deadline) */
typedef struct {
    uint64_t key;
    uint32_t id;
} wd_node_t;

static wd_node_t g_heap[WD_MAX_WORKERS];
static int       g_heap_n;
static int       g_pos[WD_MAX_WORKERS];      /* id -> heap index, -1 = absent */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv  = PTHREAD_COND_INITIALIZER;
static bool            g_running;            /* under g_lock */
static uint64_t        g_last_kick_us;       /* under g_lock */
static pthread_t       g_monitor;
static bool            g_started;            /* main thread only */

/* ------------------------------------------------------------------ */
/* heap primitives — all callers hold g_lock                           */
/* ------------------------------------------------------------------ */

static void heap_swap(int a, int b)
{
    wd_node_t t = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = t;
    g_pos[g_heap[a].id] = a;
    g_pos[g_heap[b].id] = b;
}

static void sift_up(int i)
{
    while (i > 0) {
        int p = (i - 1) / 2;
        if (g_heap[p].key <= g_heap[i].key) break;
        heap_swap(p, i);
        i = p;
    }
}

static void sift_down(int i)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < g_heap_n && g_heap[l].key < g_heap[m].key) m = l;
        if (r < g_heap_n && g_heap[r].key < g_heap[m].key) m = r;
        if (m == i) return;
        heap_swap(m, i);
        i = m;
    }
}

static void heap_insert(uint32_t id, uint64_t key)
{
    int i = g_heap_n++;
    g_heap[i].id = id;
    g_heap[i].key = key;
    g_pos[id] = i;
    sift_up(i);
}

/* O(log n) thanks to g_pos: no search for the victim. */
static void heap_remove(uint32_t id)
{
    int i = g_pos[id];
    if (i < 0) return;
    g_pos[id] = -1;
    int last = --g_heap_n;
    if (i != last) {
        g_heap[i] = g_heap[last];
        g_pos[g_heap[i].id] = i;
        sift_down(i);       /* the replacement may belong lower ... */
        sift_up(i);         /* ... or higher; only one of the two moves it */
    }
}

static uint64_t true_deadline(uint32_t id)
{
    /* acquire pairs with the worker's release store in wd_heartbeat() */
    return atomic_load_explicit(&g_hot[id].last_seen_us, memory_order_acquire)
           + g_cold[id].deadline_us;
}

/* The lazy-update heart of Part 2.
 *
 * Repair the root until its key equals its true deadline. Because every key is
 * a lower bound on its worker's true deadline, an exact root is the exact global
 * minimum (see note 5 at the top). Returns false only if the heap is empty.
 *
 * Side effect on purpose: a worker we find to be healthy again gets its
 * `faulted` flag cleared here, which is what makes "report each death exactly
 * once, and report it again if it dies again" work.
 */
static bool heap_refresh_root(uint64_t now, uint64_t *out_key, uint32_t *out_id)
{
    while (g_heap_n > 0) {
        uint32_t id = g_heap[0].id;
        uint64_t truth = true_deadline(id);

        if (truth > now && g_cold[id].faulted)
            g_cold[id].faulted = false;                 /* it came back */

        if (truth <= g_heap[0].key) {                   /* key is exact */
            *out_key = g_heap[0].key;
            *out_id  = id;
            return true;
        }
        g_heap[0].key = truth;                          /* keys only grow ... */
        sift_down(0);                                   /* ... so: sift down  */
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* portable "wait until an absolute time on the hwwdt_now_us clock"    */
/* ------------------------------------------------------------------ */

static void wd_wait_until(uint64_t abs_us)
{
#if defined(__APPLE__)
    /* macOS has no pthread_condattr_setclock; use the relative form. */
    uint64_t now = hwwdt_now_us();
    uint64_t rel = (abs_us > now) ? (abs_us - now) : 0;
    struct timespec ts;
    ts.tv_sec  = (time_t)(rel / 1000000ull);
    ts.tv_nsec = (long)((rel % 1000000ull) * 1000ull);
    (void)pthread_cond_timedwait_relative_np(&g_cv, &g_lock, &ts);
#else
    /* hwwdt_now_us() is gettimeofday(), i.e. CLOCK_REALTIME, which is exactly
     * the clock a default-initialised condvar uses. */
    struct timespec ts;
    ts.tv_sec  = (time_t)(abs_us / 1000000ull);
    ts.tv_nsec = (long)((abs_us % 1000000ull) * 1000ull);
    (void)pthread_cond_timedwait(&g_cv, &g_lock, &ts);
#endif
}

/* ------------------------------------------------------------------ */
/* the monitor thread — the only reader, the only kicker               */
/* ------------------------------------------------------------------ */

static void *monitor_main(void *arg)
{
    (void)arg;
    uint32_t stash[WD_MAX_WORKERS];

    pthread_mutex_lock(&g_lock);
    while (g_running) {
        uint64_t now = hwwdt_now_us();
        int      n_overdue = 0;
        uint64_t root_key = 0;
        bool     have_root = false;

        /* Walk only the entries whose key has already expired. Each one is
         * either stale (refresh pushes it down, cost O(log n), and we continue)
         * or genuinely overdue (report it once, then lift it out of the way so
         * we can look at the next one). Everything still in the heap after the
         * loop has key > now, hence true deadline > now, hence is healthy. */
        for (;;) {
            uint64_t k;
            uint32_t id;
            if (!heap_refresh_root(now, &k, &id)) break;     /* heap empty */
            if (k > now) { root_key = k; have_root = true; break; }

            if (!g_cold[id].faulted) {
                g_cold[id].faulted = true;
                /* Cheap vendor call, and the monitor is its only caller, so it
                 * needs no extra serialisation. */
                hwwdt_report_fault(id);
            }
            heap_remove(id);
            stash[n_overdue++] = id;
        }
        /* Put the overdue workers back with their exact (past-dated) keys, so
         * that next tick we look at them again and notice when they recover. */
        for (int i = 0; i < n_overdue; i++)
            heap_insert(stash[i], true_deadline(stash[i]));

        /* THE RULE: kick only when every registered worker is healthy. Zero
         * registered workers is vacuously healthy. */
        if (n_overdue == 0 && now - g_last_kick_us >= WD_KICK_PERIOD_US) {
            hwwdt_kick();
            g_last_kick_us = now;
        }

        uint64_t wake;
        if (n_overdue > 0) {
            wake = now + WD_RECHECK_US;         /* not kicking; just re-check */
        } else {
            wake = g_last_kick_us + WD_KICK_PERIOD_US;
            if (have_root && root_key < wake) wake = root_key;
            if (wake <= now) wake = now + 200;  /* never busy-spin */
        }
        wd_wait_until(wake);                    /* register/unregister/deinit
                                                 * signal g_cv to recompute */
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int wd_init(void)
{
    if (g_started) return -1;

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < WD_MAX_WORKERS; i++) {
        g_pos[i] = -1;
        g_cold[i].in_use = false;
        g_cold[i].faulted = false;
        atomic_store_explicit(&g_hot[i].last_seen_us, 0, memory_order_relaxed);
    }
    g_heap_n = 0;
    g_last_kick_us = 0;          /* kick on the very first tick */
    g_running = true;
    pthread_mutex_unlock(&g_lock);

    if (pthread_create(&g_monitor, NULL, monitor_main, NULL) != 0) {
        g_running = false;
        return -1;
    }
    g_started = true;
    return 0;
}

void wd_deinit(void)
{
    if (!g_started) return;
    pthread_mutex_lock(&g_lock);
    g_running = false;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_monitor, NULL);
    g_started = false;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

int wd_register(const char *name, uint64_t deadline_us)
{
    if (deadline_us == 0) return -1;

    pthread_mutex_lock(&g_lock);
    int id = -1;
    for (int i = 0; i < WD_MAX_WORKERS; i++) {
        if (!g_cold[i].in_use) { id = i; break; }
    }
    if (id >= 0) {
        uint64_t now = hwwdt_now_us();
        g_cold[id].in_use      = true;
        g_cold[id].faulted     = false;
        g_cold[id].deadline_us = deadline_us;
        snprintf(g_cold[id].name, sizeof g_cold[id].name, "%s",
                 name ? name : "worker");
        /* Grace period: a brand-new worker counts as having just checked in,
         * otherwise it would be declared dead before its first heartbeat. */
        atomic_store_explicit(&g_hot[id].last_seen_us, now, memory_order_release);
        heap_insert((uint32_t)id, now + deadline_us);
        pthread_cond_signal(&g_cv);      /* the next deadline may have moved in */
    }
    pthread_mutex_unlock(&g_lock);
    return id;
}

void wd_unregister(int id)
{
    if (id < 0 || id >= WD_MAX_WORKERS) return;
    pthread_mutex_lock(&g_lock);
    if (g_cold[id].in_use) {
        heap_remove((uint32_t)id);
        g_cold[id].in_use  = false;
        g_cold[id].faulted = false;
        pthread_cond_signal(&g_cv);
    }
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Part 1: the hot path                                                */
/* ------------------------------------------------------------------ */

void wd_heartbeat(uint32_t worker_id)
{
    if (worker_id >= WD_MAX_WORKERS) return;
    /* One release store into this worker's own cache line. Wait-free: no lock,
     * no CAS loop, no branch on other threads' state.
     *
     * We deliberately do NOT check in_use here — that would need the lock. A
     * store into a retired slot is ignored (the monitor only walks the heap).
     * Slot reuse: a retired thread with one heartbeat still in flight can land
     * it on the worker that just took the slot over. It writes "now", and
     * wd_register() already gave the new worker a grace period ending at
     * now + deadline_us, so the only effect is to shift its first deadline by
     * microseconds. If that were not acceptable the fix is a generation counter
     * packed into the same 64-bit word (store (gen << 48) | us) so a stale
     * writer's word is rejected on the read side — still one store. */
    atomic_store_explicit(&g_hot[worker_id].last_seen_us, hwwdt_now_us(),
                          memory_order_release);
}

/* ------------------------------------------------------------------ */
/* Part 2: the queries                                                 */
/* ------------------------------------------------------------------ */

int wd_next_deadline(uint64_t *when_us)
{
    pthread_mutex_lock(&g_lock);
    uint64_t k = 0;
    uint32_t id = 0;
    bool ok = heap_refresh_root(hwwdt_now_us(), &k, &id);
    pthread_mutex_unlock(&g_lock);

    if (!ok) return -1;                  /* empty: nothing to wait for */
    if (when_us) *when_us = k;
    return 0;
}

size_t wd_overdue(uint32_t *ids_out, size_t max)
{
    size_t n = 0;
    int    stack[WD_MAX_WORKERS];
    int    sp = 0;

    pthread_mutex_lock(&g_lock);
    uint64_t now = hwwdt_now_us();

    /* Pruned DFS instead of a full scan: a node with key > now has children
     * with key >= its key (heap property) and every true deadline is >= its key
     * (the invariant), so that whole subtree is healthy and can be skipped. */
    if (g_heap_n > 0) stack[sp++] = 0;
    while (sp > 0) {
        int i = stack[--sp];
        if (g_heap[i].key > now) continue;              /* prune subtree */
        uint32_t id = g_heap[i].id;
        if (true_deadline(id) <= now) {                 /* stale keys filtered */
            if (n < max) ids_out[n] = id;
            n++;
        }
        int l = 2 * i + 1, r = l + 1;
        if (l < g_heap_n) stack[sp++] = l;
        if (r < g_heap_n) stack[sp++] = r;
    }
    pthread_mutex_unlock(&g_lock);
    return (n < max) ? n : max;
}
