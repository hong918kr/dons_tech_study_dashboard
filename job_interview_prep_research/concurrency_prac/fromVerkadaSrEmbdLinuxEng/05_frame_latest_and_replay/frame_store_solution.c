/* frame_store_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol
 *
 * ---------------------------------------------------------------------------
 * Shape
 *
 *   one writer thread  ── the ONLY caller of capture_next_frame() (it blocks
 *                         ~25 ms and keeps static state, so it must be one
 *                         thread, and that thread must never wait on a reader)
 *        |
 *        |  Part 1: PUBLISH BY POINTER, not by copy
 *        +--> P.slot[0..2]   three frame buffers + a per-buffer reader
 *        |                   refcount; P.latest names the newest complete one.
 *        |                   frame_acquire_latest() = lock, refcount++, return
 *        |                   the pointer. No memcpy anywhere on the read path.
 *        |
 *        |  Part 2: bounded ring of COPIES, byte-budgeted
 *        +--> R.slot[0..127] ring of (ts, frame) sorted by ts, binary searched.
 *
 * ---------------------------------------------------------------------------
 * Why buffer ownership + refcounts, and why THREE buffers
 *
 *   A frame is 4 KiB here and ~8 MB on a real 4K sensor, so the reader must be
 *   handed the sensor's buffer, not a copy. The moment you do that, the reader
 *   *owns a reference* into memory the writer wants to reuse, and you need a
 *   rule for when the writer may reuse it. That rule is the refcount.
 *
 *   DOUBLE buffer (2): writer fills B while readers hold A, then flips. It is
 *   the minimum, but the writer has nowhere to go while a reader still holds
 *   the buffer it wants next -- so the writer must either block (forbidden) or
 *   drop every frame until the reader lets go. Fine when readers are
 *   guaranteed to be quick (a display flip), bad for "some client may stall".
 *
 *   TRIPLE buffer (3): one buffer published, one borrowed by a slow reader, one
 *   for the writer to fill. The writer keeps its full rate through a reader
 *   stall of one frame period, at the cost of one extra frame of memory
 *   (+4 KiB here, +8 MB on the real sensor). That memory buys *latency
 *   insulation*, not throughput: it does not make frames arrive sooner, it
 *   stops a slow consumer from forcing drops. More buffers = more stall
 *   absorbed = more memory and, if a reader always takes the newest, a longer
 *   worst-case age for what the writer is filling. Three is the standard
 *   answer for one producer + bursty consumers; N+2 for N simultaneous slow
 *   readers.
 *
 *   When every buffer IS pinned, the writer still calls the sensor (draining
 *   it is not optional: the vendor call is the clock) and captures into a
 *   scratch buffer it throws away, counting a drop. Never a wait on a reader.
 *
 * ---------------------------------------------------------------------------
 * Critical sections
 *
 *   Every lock here protects only index / refcount / head / tail bookkeeping:
 *   a handful of integer stores, no memcpy. The two 4 KiB copies that do exist
 *   (writer -> replay ring, replay ring -> caller's dst) happen with the lock
 *   released, protected instead by ownership: the writer owns the slot at head
 *   until it publishes it, and a reader's refcount stops that slot from being
 *   recycled underneath it. That is why the measured getter latency is a
 *   couple of microseconds against a 25 ms vendor call.
 *
 * Alternatives considered:
 *   - seqlock on a single buffer: readers copy and retry on a version change.
 *     Cheapest for a small struct, but here it would mean a 4 KiB (8 MB!) copy
 *     per read, and a reader can be starved by a fast writer. Rejected: the
 *     whole point is not copying.
 *   - RCU / atomic pointer swap: same idea as this, with the refcount replaced
 *     by a grace period. Better under heavy read load, much more machinery.
 * ---------------------------------------------------------------------------
 */
/* glibc hides usleep()/nanosleep() when the compiler is in strict -std=c11
 * mode; Apple's libc does not care. Set the feature macro before any
 * include so this builds warning-free on Linux and macOS alike. */
#define _DEFAULT_SOURCE 1

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "capture.h"
#include "frame_store.h"

/* ------------------------------------------------------------------ */
/* Part 1: publish buffers                                             */
/* ------------------------------------------------------------------ */

#define PUB_BUFS 3
#define ERROR_BACKOFF_US 2000u

typedef struct {
    uint8_t  data[FRAME_BYTES];
    uint64_t ts;
    unsigned readers;       /* borrows outstanding on this buffer */
} pub_slot_t;

static struct {
    pthread_mutex_t lock;
    pub_slot_t      slot[PUB_BUFS];
    int             latest;     /* newest complete frame, -1 = none yet */
} P = { .lock = PTHREAD_MUTEX_INITIALIZER, .latest = -1 };

/* Where a frame goes when every buffer is pinned: captured and discarded, so
 * the sensor keeps being drained and the writer never waits on a reader. */
static uint8_t P_scratch[FRAME_BYTES];

/* ------------------------------------------------------------------ */
/* Part 2: replay ring                                                 */
/* ------------------------------------------------------------------ */

/* Memory arithmetic, out loud:
 *   40 frames/s * 2 s window * 4096 B = 80 frames = 320 KiB of pixels.
 *   The sensor has jitter and can burst, so round the ring up to 128 slots:
 *   128 * sizeof(rep_slot_t) = 128 * 4112 B = 514 KiB, inside the 768 KiB
 *   budget, and 128 frames is 3.2 s of nominal rate -- comfortably more than
 *   the 2 s window, so the WINDOW is normally what evicts, not the budget.
 *   On a real 8 MB 4K frame the same arithmetic says 80 * 8 MB = 640 MB, which
 *   is why real pre-roll buffers hold encoded frames, not raw ones. Say that.
 *
 *   Two eviction rules, in this order:
 *     1. window: drop the oldest frame only when the NEXT one is also older
 *        than the window. The frame "in effect" at (now - 2 s) was captured
 *        BEFORE that instant, so it is itself older than the window and a
 *        legal query at the window edge still needs it. Keep exactly one.
 *     2. byte budget: if the ring is full anyway (sensor bursting), evict the
 *        oldest regardless. Bounded memory wins over history depth.
 *   And one hard rule on top: a slot a reader is currently copying out of
 *   (readers != 0) must not be recycled. The writer skips the append instead
 *   of waiting -- it is one frame out of a 2 s history, and a pin lasts about
 *   a microsecond.
 */
#define REPLAY_CAP  128u
#define REPLAY_MASK (REPLAY_CAP - 1u)

typedef struct {
    uint8_t  data[FRAME_BYTES];
    uint64_t ts;
    unsigned readers;
} rep_slot_t;

_Static_assert((REPLAY_CAP & REPLAY_MASK) == 0u, "REPLAY_CAP must be a power of two");
_Static_assert(REPLAY_CAP * sizeof(rep_slot_t) <= FRAME_REPLAY_BYTES_MAX,
               "replay ring does not fit in FRAME_REPLAY_BYTES_MAX");

static struct {
    pthread_mutex_t lock;
    rep_slot_t      slot[REPLAY_CAP];
    uint64_t        head;           /* next slot to fill   */
    uint64_t        tail;           /* oldest live slot    */
    uint64_t        evicted_budget; /* dropped by rule 2   */
    uint64_t        skipped_pinned; /* not appended: slot was being read */
} R = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* ------------------------------------------------------------------ */
/* counters + thread state                                             */
/* ------------------------------------------------------------------ */

static _Atomic uint64_t g_dropped;         /* no free publish buffer */
static _Atomic uint64_t g_errors;          /* capture_next_frame() == -1 */
static _Atomic uint64_t g_release_errors;  /* bad frame_release() */
static _Atomic bool     g_running;
static bool             g_started;         /* init/deinit only */
static pthread_t        g_thread;

/* ------------------------------------------------------------------ */
/* writer side                                                         */
/* ------------------------------------------------------------------ */

/* Free == nobody is borrowing it AND it is not the frame we are publishing.
 * `latest` is excluded even at refcount 0: a reader may acquire it any moment,
 * and overwriting it would tear the frame under that reader. */
static int pick_free_locked(void)
{
    for (int i = 0; i < PUB_BUFS; i++)
        if (i != P.latest && P.slot[i].readers == 0)
            return i;
    return -1;
}

static void replay_append(uint64_t ts, const uint8_t *src)
{
    uint64_t now = capture_now_us();

    pthread_mutex_lock(&R.lock);
    /* rule 1: window, keeping the one frame that covers the edge */
    while (R.head - R.tail >= 2) {
        uint64_t next_ts = R.slot[(R.tail + 1u) & REPLAY_MASK].ts;
        if (next_ts > now || now - next_ts < FRAME_REPLAY_WINDOW_US)
            break;
        R.tail++;
    }
    /* rule 2: hard byte budget */
    if (R.head - R.tail == REPLAY_CAP) {
        R.tail++;
        R.evicted_budget++;
    }
    uint64_t idx = R.head & REPLAY_MASK;
    if (R.slot[idx].readers != 0) {     /* borrowed: never recycle it */
        R.skipped_pinned++;
        pthread_mutex_unlock(&R.lock);
        return;
    }
    pthread_mutex_unlock(&R.lock);

    /* The slot is not in [tail, head), so no reader can find it by binary
     * search, and its refcount was 0 under the lock. We own it: copy without
     * the lock, then publish it by advancing head. */
    memcpy(R.slot[idx].data, src, FRAME_BYTES);

    pthread_mutex_lock(&R.lock);
    R.slot[idx].ts = ts;
    R.head++;
    pthread_mutex_unlock(&R.lock);
}

/* One turn of the writer loop. Returns true if a frame got published. */
static bool publish_one(void)
{
    pthread_mutex_lock(&P.lock);
    int w = pick_free_locked();
    pthread_mutex_unlock(&P.lock);

    /* No free buffer -> capture into scratch and throw the frame away. We must
     * still call the sensor: it is the only clock we have, and skipping it
     * would be "the writer waiting for a reader" by another name. */
    uint8_t *dst = (w >= 0) ? P.slot[w].data : P_scratch;
    uint64_t ts = 0;

    /* Blocking call, no lock held. w is safe to fill: it is not `latest`, so
     * no reader can start borrowing it while we are in here. */
    if (capture_next_frame(dst, &ts) != 0) {
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);       /* the error returns instantly: don't spin */
        return false;
    }
    if (w < 0) {
        atomic_fetch_add_explicit(&g_dropped, 1u, memory_order_relaxed);
        return false;
    }

    pthread_mutex_lock(&P.lock);
    /* Keep timestamps non-decreasing: the clock is gettimeofday, which NTP can
     * step backwards, and Part 2's binary search needs a sorted key. */
    if (P.latest >= 0 && ts < P.slot[P.latest].ts)
        ts = P.slot[P.latest].ts;
    P.slot[w].ts = ts;
    P.latest = w;                       /* the publish: one integer store */
    pthread_mutex_unlock(&P.lock);

    replay_append(ts, P.slot[w].data);
    return true;
}

static void *writer_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire))
        publish_one();
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int frame_store_init(void)
{
    if (g_started)
        return 0;

    atomic_store_explicit(&g_running, true, memory_order_release);

    /* Publish the first frame synchronously (~25 ms) so a reader that calls
     * frame_acquire_latest() right after init gets a frame instead of NULL.
     * The alternative -- return immediately and let readers see NULL -- is
     * also defensible; it just moves the problem to every caller. */
    for (int tries = 0; tries < 3; tries++)
        if (publish_one())
            break;

    if (pthread_create(&g_thread, NULL, writer_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;
    }
    g_started = true;
    return 0;
}

void frame_store_deinit(void)
{
    if (!g_started)
        return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    /* The thread may be parked inside capture_next_frame(): joining costs up to
     * one frame period. Nothing can cancel the vendor call safely. */
    pthread_join(g_thread, NULL);
    g_started = false;
}

/* ------------------------------------------------------------------ */
/* Part 1: reader side                                                 */
/* ------------------------------------------------------------------ */

const uint8_t *frame_acquire_latest(uint64_t *ts_out)
{
    const uint8_t *p = NULL;

    pthread_mutex_lock(&P.lock);
    int i = P.latest;
    if (i >= 0) {
        P.slot[i].readers++;            /* the borrow: writer will not touch it */
        if (ts_out)
            *ts_out = P.slot[i].ts;
        p = P.slot[i].data;
    }
    pthread_mutex_unlock(&P.lock);
    return p;                           /* no copy, no sensor wait */
}

void frame_release(const uint8_t *frame)
{
    if (!frame)
        return;                         /* like free(NULL) */

    int idx = -1;
    for (int i = 0; i < PUB_BUFS; i++)
        if (frame == P.slot[i].data) {
            idx = i;
            break;
        }
    if (idx < 0) {                      /* not one of ours */
        atomic_fetch_add_explicit(&g_release_errors, 1u, memory_order_relaxed);
        return;
    }

    pthread_mutex_lock(&P.lock);
    if (P.slot[idx].readers == 0) {
        /* One release too many. Decrementing here would let the writer reuse a
         * buffer somebody still holds -- exactly the bug this counter exists to
         * surface. Refuse it. */
        pthread_mutex_unlock(&P.lock);
        atomic_fetch_add_explicit(&g_release_errors, 1u, memory_order_relaxed);
        return;
    }
    P.slot[idx].readers--;
    pthread_mutex_unlock(&P.lock);
}

/* ------------------------------------------------------------------ */
/* Part 2: replay queries                                              */
/* ------------------------------------------------------------------ */

/* first offset in [0, n) whose ts > t  (call with R.lock held) */
static uint64_t upper_bound_locked(uint64_t t)
{
    uint64_t lo = 0, hi = R.head - R.tail;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2u;
        if (R.slot[(R.tail + mid) & REPLAY_MASK].ts <= t)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

/* first offset in [0, n) whose ts >= t  (call with R.lock held) */
static uint64_t lower_bound_locked(uint64_t t)
{
    uint64_t lo = 0, hi = R.head - R.tail;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2u;
        if (R.slot[(R.tail + mid) & REPLAY_MASK].ts < t)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

int frame_at(uint64_t t, uint8_t *dst, uint64_t *actual_ts)
{
    uint64_t now = capture_now_us();
    if (t > now)                                    /* the future */
        return -1;
    if (now - t > FRAME_REPLAY_WINDOW_US)           /* older than we promise */
        return -1;

    pthread_mutex_lock(&R.lock);
    uint64_t off = upper_bound_locked(t);
    if (off == 0) {                                 /* nothing at or before t */
        pthread_mutex_unlock(&R.lock);
        return -1;
    }
    uint64_t idx = (R.tail + off - 1u) & REPLAY_MASK;
    uint64_t ts = R.slot[idx].ts;
    R.slot[idx].readers++;                          /* pin: do not recycle */
    pthread_mutex_unlock(&R.lock);

    if (dst)
        memcpy(dst, R.slot[idx].data, FRAME_BYTES); /* the big copy, unlocked */
    if (actual_ts)
        *actual_ts = ts;

    pthread_mutex_lock(&R.lock);
    R.slot[idx].readers--;
    pthread_mutex_unlock(&R.lock);
    return 0;
}

size_t frame_count_in(uint64_t t0, uint64_t t1)
{
    if (t0 > t1)
        return 0;
    pthread_mutex_lock(&R.lock);
    uint64_t lo = lower_bound_locked(t0);
    uint64_t hi = upper_bound_locked(t1);
    size_t c = (size_t)(hi - lo);
    pthread_mutex_unlock(&R.lock);
    return c;
}

/* ------------------------------------------------------------------ */
/* bookkeeping                                                         */
/* ------------------------------------------------------------------ */

uint64_t frame_dropped_count(void)
{
    return atomic_load_explicit(&g_dropped, memory_order_relaxed);
}

uint64_t frame_error_count(void)
{
    return atomic_load_explicit(&g_errors, memory_order_relaxed);
}

uint64_t frame_release_errors(void)
{
    return atomic_load_explicit(&g_release_errors, memory_order_relaxed);
}

size_t frame_borrowed_count(void)
{
    size_t n = 0;
    pthread_mutex_lock(&P.lock);
    for (int i = 0; i < PUB_BUFS; i++)
        n += P.slot[i].readers;
    pthread_mutex_unlock(&P.lock);
    return n;
}

size_t frame_replay_bytes(void)
{
    pthread_mutex_lock(&R.lock);
    size_t n = (size_t)(R.head - R.tail);
    pthread_mutex_unlock(&R.lock);
    return n * sizeof(rep_slot_t);
}
