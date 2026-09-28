/* ap_table_solution.c — reference solution.
 *
 *   ./main.sh sol       build main.c + this file and run every check
 *   ./main.sh tsan      same, under -fsanitize=thread
 *
 * Shape
 *   one scanner thread ── the ONLY caller of wifi_scan() (it blocks 100-300 ms
 *                         and the vendor code is not thread-safe).
 *        │ on success
 *        ├──► g_snap[]   Part 1: THREE preallocated slots, each holding a whole
 *        │               variable-length scan (count + entries). An atomic
 *        │               index says which slot is current; an atomic per-slot
 *        │               refcount ("pin") says which slots a reader is inside.
 *        │               The writer fills a slot nobody is pinning, then
 *        │               release-stores the index. Readers pin, re-check,
 *        │               memcpy, unpin.
 *        └──► g_tab      Part 2: open-addressing hash map keyed by BSSID,
 *                        mutex-guarded, EWMA + lazy time eviction.
 *
 * ------------------------------------------------------------------------
 * PART 1 — why three slots and a refcount, and not the other two options
 *
 * The published value is not a word, it is a COLLECTION whose length changes
 * every scan (0..40 entries, ~1.6 KB). "Store one atomic and be done" does
 * not exist here, so the three real candidates are:
 *
 *   (a) mutex + memcpy under the lock.
 *       Correct and 10 lines. But every reader holds the same lock for a
 *       1.6 KB copy, so readers serialise against each other AND against the
 *       writer. On a gateway that publishes Wi-Fi state to several consumers
 *       (roaming logic, cloud telemetry, CLI) that is the wrong trade: the
 *       data is written 10x/second and read thousands of times a second.
 *       It also puts an unbounded-priority-inversion hazard on the hot path.
 *
 *   (b) double/triple buffer + refcount  <-- CHOSEN
 *       Writer touches only an idle slot, so it never waits on a reader for
 *       the copy itself; publishing is one release store. Readers never block
 *       each other at all — no mutual exclusion, only a shared counter.
 *       Cost: fixed memory (3 * 1.6 KB) and the retry loop, which is bounded
 *       in practice by the publish rate (one flip per ~100 ms).
 *       THREE slots, not two: with two, the writer's only candidate is the
 *       slot readers just stopped reading, so one slow reader stalls the
 *       writer. With three, the writer has the current slot plus two others
 *       and essentially always finds a free one.
 *
 *   (c) RCU-ish pointer swap (malloc a new buffer, atomically swap the
 *       pointer, free the old one "later").
 *       Cleanest reader (one pointer load) but the hard part is "later":
 *       without a grace period you free memory a reader is still in. Real
 *       answers are epochs, hazard pointers or call_rcu — that is a kernel
 *       facility, not something to hand-roll in a userspace daemon. It also
 *       mallocs on every scan, which an embedded gateway would rather not do.
 *       The refcount in (b) IS the poor man's grace period, with the malloc
 *       replaced by a fixed slot pool.
 *
 *   A seqlock is the fourth option and is worth mentioning: writer bumps an
 *   odd/even sequence around the copy, reader reads seq, copies, re-reads seq
 *   and retries if it changed. Wait-free for the writer, but the reader can
 *   be starved and it copies out of a buffer that is being written, which is
 *   a data race that only "works" because the reader throws the result away.
 *   Fine for an 8-byte struct, unpleasant for 1.6 KB. The slot+refcount
 *   scheme gives the reader a stable buffer instead.
 *
 * PART 2 — hash map + on-demand min-heap
 *
 *   wifi_lookup   O(1) average (open addressing, linear probing, FNV-1a over
 *                 the 6 BSSID bytes); O(capacity) worst case if the table is
 *                 saturated. No chaining, so no malloc and one cache line per
 *                 probe -- the right shape for an embedded target.
 *   wifi_top_k    O(N log k) time, O(1) extra space: keep a size-k MIN-heap
 *                 of the best-so-far in the caller's output buffer; the root
 *                 is the weakest survivor, so each of the N slots costs one
 *                 compare and only sometimes a log k sift. Sorting all N is
 *                 O(N log N) and needs an N-sized scratch copy (you cannot
 *                 sort the table in place - it is a hash table, moving
 *                 entries breaks the probe chains). With N=256 and k=8 that
 *                 is ~2000 compares versus ~256*3; the gap grows with N and
 *                 the heap's memory does not grow at all.
 *   eviction      lazy: nothing runs on a timer. An entry older than
 *                 AP_STALE_US is invisible to readers and is the preferred
 *                 victim when a new BSSID needs a slot. Deleting for real
 *                 would mean tombstones or back-shifting to keep probe chains
 *                 intact; overwriting a dead entry in place keeps the chain
 *                 and costs nothing.
 *   EWMA          s += alpha * (x - s), alpha = 1/4. One float per AP, no
 *                 history buffer, and it is the right statistic here: a
 *                 single scan's RSSI swings 5-10 dB from multipath and body
 *                 blocking, so "last value" makes the roaming logic flap
 *                 between APs. Averaging over a window would need per-AP
 *                 storage and a decision about window length; the EWMA
 *                 forgets exponentially with one multiply-add. alpha picks
 *                 the trade: 1/4 settles within ~10 scans (~1 s here) and
 *                 still rejects a single bad sample. First sighting seeds
 *                 with the raw value - starting from 0 dBm would make a new
 *                 AP look like the strongest thing in the building.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ap_table.h"
#include "wifi.h"

/* Power of two so the bucket index is a mask, not a division. */
_Static_assert((AP_TABLE_CAP & (AP_TABLE_CAP - 1u)) == 0u,
               "AP_TABLE_CAP must be a power of two");

#define SNAP_SLOTS        3
#define BUSY_BACKOFF_US   5000u   /* BUSY returns instantly: do not spin */
#define ACQUIRE_SPIN_US   50u

/* ------------------------------------------------------------------ */
/* Part 1: slot pool                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    _Atomic uint32_t pin;                 /* readers currently inside       */
    size_t           n;                   /* entries valid in ap[]          */
    struct ApInfo    ap[WIFI_MAX_APS];    /* payload, written by writer only */
} snap_slot_t;

static snap_slot_t   g_snap[SNAP_SLOTS];
static _Atomic int   g_cur = -1;          /* published slot, -1 = none yet  */
static _Atomic uint32_t g_stalls;         /* writer had to wait for a slot  */

/* ------------------------------------------------------------------ */
/* Part 2: hash map                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    bool            used;                 /* slot ever written (EMPTY ends a probe chain) */
    struct ApEntry  e;
} ap_slot_t;

static struct {
    pthread_mutex_t lock;
    ap_slot_t       slot[AP_TABLE_CAP];
    size_t          occupied;             /* used slots, live or stale      */
    size_t          dropped;              /* refused: table full of live APs */
} g_tab = {.lock = PTHREAD_MUTEX_INITIALIZER};

/* ------------------------------------------------------------------ */
/* scanner thread lifecycle                                            */
/* ------------------------------------------------------------------ */

static _Atomic bool g_running;
static bool         g_started;            /* only init/deinit touch this    */
static pthread_t    g_thread;

/* ================================================================== */
/* Part 1 writer side                                                  */
/* ================================================================== */

/* Pick a slot that is neither published nor pinned by any reader.
 * Only the scanner thread calls this, so g_cur cannot move underneath us. */
static snap_slot_t *slot_acquire(void)
{
    int cur = atomic_load_explicit(&g_cur, memory_order_relaxed);

    for (int spin = 0; spin < 2000; spin++) {
        for (int i = 0; i < SNAP_SLOTS; i++) {
            if (i == cur)
                continue;
            /* acquire: pairs with the reader's release on unpin, so the
             * reader's loads are ordered before our stores to this slot. */
            if (atomic_load_explicit(&g_snap[i].pin, memory_order_acquire) == 0u)
                return &g_snap[i];
        }
        atomic_fetch_add_explicit(&g_stalls, 1u, memory_order_relaxed);
        usleep(ACQUIRE_SPIN_US);          /* every reader is mid-memcpy; yield */
    }
    return NULL;                          /* only a stuck reader gets here */
}

static void snap_publish(const struct ApInfo *ap, size_t n)
{
    snap_slot_t *s = slot_acquire();
    if (!s)
        return;                           /* keep the old snapshot rather than tear it */

    s->n = n;
    if (n)
        memcpy(s->ap, ap, n * sizeof *ap);

    /* release: everything above is visible to any reader that acquire-loads
     * this index. This single store is the "commit" of the whole collection. */
    atomic_store_explicit(&g_cur, (int)(s - g_snap), memory_order_release);
}

/* ================================================================== */
/* Part 1 reader side                                                  */
/* ================================================================== */

size_t wifi_get_snapshot(struct ApInfo *out, size_t max)
{
    if (!out || max == 0)
        return 0;

    for (;;) {
        int i = atomic_load_explicit(&g_cur, memory_order_acquire);
        if (i < 0)
            return 0;                     /* no scan has completed yet */

        atomic_fetch_add_explicit(&g_snap[i].pin, 1u, memory_order_acq_rel);

        /* Re-check AFTER pinning. If the writer flipped away from this slot
         * in the window between the load and the increment, it may already
         * be refilling it -- drop the pin and start over. If the index still
         * says i, the writer cannot be inside slot i: it only ever writes a
         * slot that is not current and not pinned. */
        if (atomic_load_explicit(&g_cur, memory_order_acquire) != i) {
            atomic_fetch_sub_explicit(&g_snap[i].pin, 1u, memory_order_release);
            continue;
        }

        size_t n = g_snap[i].n;
        size_t c = n < max ? n : max;
        if (c)
            memcpy(out, g_snap[i].ap, c * sizeof *out);

        /* release: the writer's acquire-load of pin==0 will see that our
         * reads finished before it starts overwriting the slot. */
        atomic_fetch_sub_explicit(&g_snap[i].pin, 1u, memory_order_release);
        return c;
    }
}

/* ================================================================== */
/* Part 2: hash map keyed by BSSID                                     */
/* ================================================================== */

/* FNV-1a over the 6 MAC bytes, then an xor-fold.
 * Why fold: we index with `h & (CAP-1)`, i.e. the LOW bits, and FNV's low
 * bits are its weakest -- consecutive BSSIDs (a vendor hands out MACs in
 * runs) would collide in clumps. The fold mixes the high bits down.
 * Why not "just use the last 4 bytes of the MAC": the last bytes of a MAC
 * are sequential per vendor, so with CAP=256 a rack of 300 APs from one
 * vendor lands in a handful of buckets. */
static uint32_t bssid_hash(const uint8_t b[6])
{
    uint32_t h = 2166136261u;             /* FNV offset basis */
    for (int i = 0; i < 6; i++) {
        h ^= (uint32_t)b[i];
        h *= 16777619u;                   /* FNV prime */
    }
    h ^= h >> 16;
    return h;
}

static bool entry_stale(const struct ApEntry *e, uint64_t now)
{
    if (now < e->last_seen_us)            /* wall clock stepped backwards */
        return false;
    return (now - e->last_seen_us) > AP_STALE_US;
}

/* Find a live-or-stale entry for this BSSID. Caller holds the lock. */
static long tab_find(const uint8_t bssid[6])
{
    uint32_t start = bssid_hash(bssid) & (AP_TABLE_CAP - 1u);
    for (uint32_t p = 0; p < AP_TABLE_CAP; p++) {
        uint32_t j = (start + p) & (AP_TABLE_CAP - 1u);
        if (!g_tab.slot[j].used)
            return -1;                    /* EMPTY terminates the probe chain */
        if (memcmp(g_tab.slot[j].e.bssid, bssid, 6) == 0)
            return (long)j;
    }
    return -1;                            /* table saturated, key absent */
}

/* Insert or update one AP. Caller holds the lock. */
static void tab_upsert(const struct ApInfo *a, uint64_t now)
{
    uint32_t start = bssid_hash(a->bssid) & (AP_TABLE_CAP - 1u);
    long victim = -1, empty = -1;

    for (uint32_t p = 0; p < AP_TABLE_CAP; p++) {
        uint32_t j = (start + p) & (AP_TABLE_CAP - 1u);
        ap_slot_t *s = &g_tab.slot[j];

        if (!s->used) { empty = (long)j; break; }

        if (memcmp(s->e.bssid, a->bssid, 6) == 0) {
            /* known AP: smooth the RSSI. Three statements, not one
             * expression, so the compiler cannot fuse the multiply and the
             * add into an FMA -- that keeps the result bit-identical to the
             * harness's reference. */
            float d = (float)a->rssi_dbm - s->e.rssi_smoothed;
            d *= AP_EWMA_ALPHA;
            s->e.rssi_smoothed += d;
            memcpy(s->e.ssid, a->ssid, sizeof s->e.ssid);   /* SSID can change */
            s->e.ssid[sizeof s->e.ssid - 1] = '\0';
            if (s->e.times_seen != UINT32_MAX)
                s->e.times_seen++;
            s->e.last_seen_us = now;
            return;
        }

        /* Remember the first dead entry on the chain: overwriting it in
         * place keeps the chain intact, unlike a real delete. */
        if (victim < 0 && entry_stale(&s->e, now))
            victim = (long)j;
    }

    long j = (victim >= 0) ? victim : empty;
    if (j < 0) {
        /* No empty slot and nothing dead: the table is genuinely full of
         * APs we still care about. Refuse loudly instead of evicting
         * something live at random -- a non-zero counter tells the operator
         * AP_TABLE_CAP is undersized. */
        g_tab.dropped++;
        return;
    }
    if (victim < 0)
        g_tab.occupied++;                 /* consumed a fresh slot */

    ap_slot_t *s = &g_tab.slot[j];
    s->used = true;
    memset(&s->e, 0, sizeof s->e);
    memcpy(s->e.bssid, a->bssid, 6);
    memcpy(s->e.ssid, a->ssid, sizeof s->e.ssid);
    s->e.ssid[sizeof s->e.ssid - 1] = '\0';
    s->e.rssi_smoothed = (float)a->rssi_dbm;   /* seed, do not smooth from 0 */
    s->e.times_seen = 1;
    s->e.last_seen_us = now;
}

void wifi_table_observe(const struct ApInfo *aps, size_t n)
{
    if (!aps || n == 0)
        return;
    uint64_t now = wifi_now_us();
    pthread_mutex_lock(&g_tab.lock);
    for (size_t i = 0; i < n; i++)
        tab_upsert(&aps[i], now);
    pthread_mutex_unlock(&g_tab.lock);
}

int wifi_lookup(const uint8_t bssid[6], struct ApEntry *out)
{
    if (!bssid || !out)
        return -1;
    uint64_t now = wifi_now_us();
    int rc = -1;
    pthread_mutex_lock(&g_tab.lock);
    long j = tab_find(bssid);
    if (j >= 0 && !entry_stale(&g_tab.slot[j].e, now)) {
        *out = g_tab.slot[j].e;
        rc = 0;
    }
    pthread_mutex_unlock(&g_tab.lock);
    return rc;                            /* stale == unknown to the caller */
}

/* ------------------------------------------------------------------ */
/* top-k: size-k min-heap kept in the caller's buffer                  */
/* ------------------------------------------------------------------ */

/* "weaker" is the heap's ordering. The documented tie-break (equal smoothed
 * RSSI -> smaller BSSID wins) makes the whole ranking deterministic, which
 * matters: roaming logic that flips between two equally strong APs every
 * scan is a bug report. */
static bool weaker(const struct ApEntry *a, const struct ApEntry *b)
{
    if (a->rssi_smoothed != b->rssi_smoothed)
        return a->rssi_smoothed < b->rssi_smoothed;
    return memcmp(a->bssid, b->bssid, 6) > 0;
}

static void heap_swap(struct ApEntry *a, struct ApEntry *b)
{
    struct ApEntry t = *a; *a = *b; *b = t;
}

static void heap_up(struct ApEntry *h, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!weaker(&h[i], &h[p]))
            return;
        heap_swap(&h[i], &h[p]);
        i = p;
    }
}

static void heap_down(struct ApEntry *h, size_t m, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, w = i;
        if (l < m && weaker(&h[l], &h[w])) w = l;
        if (r < m && weaker(&h[r], &h[w])) w = r;
        if (w == i)
            return;
        heap_swap(&h[i], &h[w]);
        i = w;
    }
}

size_t wifi_top_k(struct ApEntry *out, size_t k)
{
    if (!out || k == 0)
        return 0;

    uint64_t now = wifi_now_us();
    size_t m = 0;

    pthread_mutex_lock(&g_tab.lock);
    for (uint32_t j = 0; j < AP_TABLE_CAP; j++) {
        const ap_slot_t *s = &g_tab.slot[j];
        if (!s->used || entry_stale(&s->e, now))
            continue;
        if (m < k) {
            out[m] = s->e;                /* fill the heap */
            heap_up(out, m);
            m++;
        } else if (weaker(&out[0], &s->e)) {
            out[0] = s->e;                /* beats the weakest survivor */
            heap_down(out, m, 0);
        }
        /* else: weaker than the whole heap -- one compare and we are done,
         * which is exactly why this is O(N log k) and not O(N log N). */
    }
    pthread_mutex_unlock(&g_tab.lock);

    /* The heap is only partially ordered. Sort strongest-first outside the
     * lock -- `out` is the caller's private buffer and k is small. */
    for (size_t i = 1; i < m; i++) {
        struct ApEntry v = out[i];
        size_t j = i;
        while (j > 0 && weaker(&out[j - 1], &v)) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    return m;
}

size_t wifi_table_count(void)
{
    uint64_t now = wifi_now_us();
    size_t n = 0;
    pthread_mutex_lock(&g_tab.lock);
    for (uint32_t j = 0; j < AP_TABLE_CAP; j++)
        if (g_tab.slot[j].used && !entry_stale(&g_tab.slot[j].e, now))
            n++;
    pthread_mutex_unlock(&g_tab.lock);
    return n;
}

size_t wifi_table_dropped(void)
{
    pthread_mutex_lock(&g_tab.lock);
    size_t n = g_tab.dropped;
    pthread_mutex_unlock(&g_tab.lock);
    return n;
}

/* ================================================================== */
/* the one thread allowed to touch the radio                           */
/* ================================================================== */

static bool scan_once(void)
{
    struct ApInfo buf[WIFI_MAX_APS];
    size_t found = 0;

    if (wifi_scan(buf, WIFI_MAX_APS, &found) != 0)
        return false;                     /* BUSY: buf and found are garbage */

    /* The vendor may report more APs than fit in the buffer. Trust the
     * buffer size, never `found`, when deciding how much to read. */
    size_t n = found < (size_t)WIFI_MAX_APS ? found : (size_t)WIFI_MAX_APS;

    snap_publish(buf, n);                 /* Part 1 first: cheapest, hottest */
    wifi_table_observe(buf, n);           /* Part 2 */
    return true;
}

static void *scanner_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        if (!scan_once())
            usleep(BUSY_BACKOFF_US);
    }
    return NULL;
}

int wifi_table_init(void)
{
    if (g_started)
        return 0;

    /* One scan up front so a getter called right after init already has a
     * complete snapshot. Costs one scan time; retry a couple of times in
     * case the radio came back BUSY. */
    for (int tries = 0; tries < 3; tries++)
        if (scan_once())
            break;

    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, scanner_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;
    }
    g_started = true;
    return 0;
}

void wifi_table_deinit(void)
{
    if (!g_started)
        return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    /* The thread may be parked inside wifi_scan() for up to one scan
     * (300 ms on real hardware). There is no way to cancel a vendor
     * blocking call safely, so we wait for it. */
    pthread_join(g_thread, NULL);
    g_started = false;
    /* The snapshot and the table stay valid and readable after deinit. */
}
