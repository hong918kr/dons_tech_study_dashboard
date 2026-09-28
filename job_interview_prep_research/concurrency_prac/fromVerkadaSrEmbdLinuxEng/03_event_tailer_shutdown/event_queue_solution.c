/* event_queue_solution.c — reference solution (Part 1 + Part 2).
 *
 *   ./main.sh sol        normal run
 *   ./main.sh fast sol   20 ms buckets / 32 buckets, so eviction really happens
 *
 * Shape
 *   one sampler thread  ── the ONLY caller of evsrc_read_blocking(). It has to
 *                          be exactly one: the vendor call is not thread-safe
 *                          and it blocks with no timeout, so it cannot live on
 *                          a consumer's thread.
 *        │ event
 *        ├──► g_cnt  (Part 2) ring of EVQ_NBUCKETS fixed-width time buckets,
 *        │                    per-type counters, advanced lazily. O(1) insert,
 *        │                    O(buckets) query, memory independent of rate.
 *        └──► g_q    (Part 1) bounded ring of EVQ_CAPACITY events + condvar.
 *                             Full => drop the OLDEST and bump a counter.
 *   consumers call evq_try_pop / evq_pop_timed / evq_count_since from any
 *   number of threads and never touch the vendor.
 *
 * Why these choices
 *   - mutex + condvar, not lock-free: a consumer must be able to *wait* for an
 *     event (evq_pop_timed), and a condvar is the only cheap way to sleep and
 *     be woken. The critical sections are a struct copy and a bounded loop, so
 *     the worst getter latency stays microseconds against a vendor that blocks
 *     for minutes.
 *   - two independent locks: a counting query must not queue up behind a
 *     consumer that is draining events, and vice versa. They protect
 *     unrelated state, so keeping them separate costs nothing.
 *   - drop-OLDEST on overflow: for a live gateway the newest events are the
 *     ones worth acting on, and dropping oldest keeps a slow consumer from
 *     blocking the sampler (which would block the vendor bridge). Nothing is
 *     silently lost: the drop is counted, and the *history* in Part 2 is
 *     recorded before the queue, so reporting stays correct even while the
 *     queue is overflowing.
 *   - shutdown: the sampler is parked in a call with no timeout, so a flag is
 *     not enough. Order is: stop flag -> evsrc_wake() (the vendor's escape
 *     hatch) -> wake any blocked consumer -> join. See evq_deinit().
 */
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "event_queue.h"
#include "evsrc.h"

_Static_assert((EVQ_CAPACITY & (EVQ_CAPACITY - 1u)) == 0u,
               "EVQ_CAPACITY must be a power of two (index masking)");
#define Q_MASK ((uint32_t)EVQ_CAPACITY - 1u)

/* A transient vendor error comes back instantly; without a pause we would
 * spin at 100% CPU on a broken bridge. */
#define ERROR_BACKOFF_US 2000u

/* ------------------------------------------------------------------ */
/* Part 1: bounded queue                                               */
/* ------------------------------------------------------------------ */

static struct {
    pthread_mutex_t lock;
    pthread_cond_t  cv;              /* signalled on every push, broadcast on stop */
    struct Event    buf[EVQ_CAPACITY];
    uint32_t        head;            /* free-running: next slot to write */
    uint32_t        tail;            /* free-running: oldest unread slot */
    bool            stopping;        /* true before init and after deinit */
    uint64_t        received, dropped, popped, errors;
} g_q = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cv = PTHREAD_COND_INITIALIZER,
    .stopping = true,                /* a getter before evq_init() must fail, not crash */
};

/* head/tail are free-running counters, so the difference is the depth and
 * uint32 wrap-around is harmless as long as capacity is a power of two. */
static uint32_t q_depth_locked(void) { return g_q.head - g_q.tail; }

static void q_push(const struct Event *e)
{
    pthread_mutex_lock(&g_q.lock);
    g_q.received++;
    if (q_depth_locked() == (uint32_t)EVQ_CAPACITY) {
        g_q.tail++;                  /* drop-oldest: the slot is about to be reused */
        g_q.dropped++;
    }
    g_q.buf[g_q.head & Q_MASK] = *e;
    g_q.head++;
    /* signal, not broadcast: one event can only satisfy one waiter. */
    pthread_cond_signal(&g_q.cv);
    pthread_mutex_unlock(&g_q.lock);
}

static bool q_pop_locked(struct Event *out)
{
    if (q_depth_locked() == 0u)
        return false;
    *out = g_q.buf[g_q.tail & Q_MASK];
    g_q.tail++;
    g_q.popped++;
    return true;
}

int evq_try_pop(struct Event *out)
{
    if (!out)
        return -1;
    pthread_mutex_lock(&g_q.lock);
    bool got = !g_q.stopping && q_pop_locked(out);
    pthread_mutex_unlock(&g_q.lock);
    return got ? 0 : -1;
}

/* Wait on cv until an absolute microsecond deadline.
 *
 * Portability: pthread_condattr_setclock() is not available on macOS, so the
 * two platforms are split. The deadline is absolute on purpose — a relative
 * wait restarted after a spurious wakeup would stretch the timeout. */
static int cond_wait_until(uint64_t deadline_us)
{
    uint64_t now = ev_now_us();
    if (now >= deadline_us)
        return ETIMEDOUT;
    uint64_t left = deadline_us - now;

#ifdef __APPLE__
    struct timespec rel;
    rel.tv_sec  = (time_t)(left / 1000000ull);
    rel.tv_nsec = (long)((left % 1000000ull) * 1000ull);
    return pthread_cond_timedwait_relative_np(&g_q.cv, &g_q.lock, &rel);
#else
    /* ev_now_us() is gettimeofday(), i.e. CLOCK_REALTIME — the same clock the
     * default condattr uses, so the absolute deadline needs no conversion. */
    struct timespec abs;
    abs.tv_sec  = (time_t)(deadline_us / 1000000ull);
    abs.tv_nsec = (long)((deadline_us % 1000000ull) * 1000ull);
    return pthread_cond_timedwait(&g_q.cv, &g_q.lock, &abs);
#endif
}

int evq_pop_timed(struct Event *out, uint64_t timeout_us)
{
    if (!out)
        return -1;
    uint64_t deadline = ev_now_us() + timeout_us;

    pthread_mutex_lock(&g_q.lock);
    for (;;) {
        if (g_q.stopping)
            break;                          /* deinit released us */
        if (q_pop_locked(out)) {
            pthread_mutex_unlock(&g_q.lock);
            return 0;
        }
        if (timeout_us == 0)
            break;                          /* degenerate case == evq_try_pop */
        /* Loop, do not trust a single wakeup: condvars wake spuriously, and
         * another consumer may have taken the event we were signalled for. */
        if (cond_wait_until(deadline) == ETIMEDOUT) {
            if (!g_q.stopping && q_pop_locked(out)) {
                pthread_mutex_unlock(&g_q.lock);
                return 0;                   /* raced in just before the deadline */
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_q.lock);
    return -1;
}

void evq_stats(struct EvqStats *out)
{
    if (!out)
        return;
    /* One lock for all five fields: a caller checking
     * received == popped + dropped + depth must see a consistent snapshot. */
    pthread_mutex_lock(&g_q.lock);
    out->received = g_q.received;
    out->dropped  = g_q.dropped;
    out->popped   = g_q.popped;
    out->errors   = g_q.errors;
    out->depth    = q_depth_locked();
    pthread_mutex_unlock(&g_q.lock);
}

/* ------------------------------------------------------------------ */
/* Part 2: ring of fixed-width time buckets                            */
/* ------------------------------------------------------------------ */
/*
 * Why not keep the events themselves?
 *   A per-event list (or the ring from the previous problem) makes
 *   count_since() O(number of events in the window), and the number of events
 *   is exactly the thing we do not control: a tamper storm on 8 doors can
 *   produce thousands of events per second, and a burst is when a monitoring
 *   query is most likely to be running. Memory would also grow with the rate,
 *   and evicting a long backlog under the lock is a latency spike.
 *
 * Bucketing flips both costs:
 *   memory  = EVQ_NBUCKETS * EV_TYPE_COUNT counters, fixed, whatever the rate
 *   insert  = O(1) increment (plus a lazy advance, bounded below)
 *   query   = O(EVQ_NBUCKETS) — 300 adds for a 5-minute window, ~1 us
 *   cost is the resolution: the answer is rounded to a whole bucket.
 *
 * Lazy advance: buckets are not cleared by a timer. When an event lands in a
 * newer bucket we zero only the buckets we skipped over — and if we skipped
 * more than a full lap (a gateway that saw nothing for an hour) we memset the
 * whole table once instead of looping over a million stale bucket indices.
 * That keeps the "first event after a quiet night" from stalling the sampler.
 */

static struct {
    pthread_mutex_t lock;
    uint32_t        n[EVQ_NBUCKETS][EV_TYPE_COUNT];
    uint64_t        cur;             /* absolute index of the newest bucket held */
    bool            primed;          /* false until the first event             */
    uint64_t        unknown;         /* events with a type we do not know        */
} g_cnt = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void cnt_add(const struct Event *e)
{
    uint64_t b = e->timestamp / (uint64_t)EVQ_BUCKET_US;

    pthread_mutex_lock(&g_cnt.lock);
    if (e->type < 0 || e->type >= EV_TYPE_COUNT) {
        g_cnt.unknown++;             /* never index an array with vendor data */
        pthread_mutex_unlock(&g_cnt.lock);
        return;
    }

    if (!g_cnt.primed) {
        g_cnt.cur = b;
        g_cnt.primed = true;
    } else if (b > g_cnt.cur) {
        if (b - g_cnt.cur >= (uint64_t)EVQ_NBUCKETS) {
            memset(g_cnt.n, 0, sizeof g_cnt.n);          /* skipped a whole lap */
        } else {
            for (uint64_t k = g_cnt.cur + 1; k <= b; k++)
                memset(g_cnt.n[k % (uint64_t)EVQ_NBUCKETS], 0, sizeof g_cnt.n[0]);
        }
        g_cnt.cur = b;
    }

    /* A stamp older than the whole window has no bucket to live in any more.
     * (b < cur - (NBUCKETS-1), written without underflow.) */
    if (b + (uint64_t)EVQ_NBUCKETS > g_cnt.cur)
        g_cnt.n[b % (uint64_t)EVQ_NBUCKETS][e->type]++;

    pthread_mutex_unlock(&g_cnt.lock);
}

uint32_t evq_count_since(int type, uint64_t since_us)
{
    if (type != EV_TYPE_ANY && (type < 0 || type >= EV_TYPE_COUNT))
        return 0;

    uint64_t now = ev_now_us();
    if (since_us > now)
        return 0;                    /* nothing has happened in the future yet */

    const uint64_t NB = (uint64_t)EVQ_NBUCKETS;
    uint64_t nowb = now / (uint64_t)EVQ_BUCKET_US;
    uint64_t lo   = since_us / (uint64_t)EVQ_BUCKET_US;

    /* Clamp to the window: older than EVQ_WINDOW_US is simply "the window". */
    uint64_t window_lo = (nowb + 1 >= NB) ? nowb - (NB - 1) : 0;
    if (lo < window_lo)
        lo = window_lo;

    uint32_t total = 0;
    pthread_mutex_lock(&g_cnt.lock);
    if (g_cnt.primed) {
        uint64_t hi = g_cnt.cur < nowb ? g_cnt.cur : nowb;   /* no future buckets */
        uint64_t ring_lo = (g_cnt.cur + 1 >= NB) ? g_cnt.cur - (NB - 1) : 0;
        if (lo < ring_lo)
            lo = ring_lo;            /* anything older was zeroed and reused */
        /* At most EVQ_NBUCKETS iterations, whatever the event rate was. */
        for (uint64_t b = lo; b <= hi; b++) {
            const uint32_t *row = g_cnt.n[b % NB];
            if (type == EV_TYPE_ANY) {
                for (int t = 0; t < EV_TYPE_COUNT; t++)
                    total += row[t];
            } else {
                total += row[type];
            }
        }
    }
    pthread_mutex_unlock(&g_cnt.lock);
    return total;
}

/* ------------------------------------------------------------------ */
/* sampler thread                                                      */
/* ------------------------------------------------------------------ */

static _Atomic bool g_running;
static bool         g_started;        /* only touched by init/deinit */
static pthread_t    g_thread;

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        struct Event e;
        int rc = evsrc_read_blocking(&e);

        if (rc == -2)
            break;                   /* evsrc_wake(): shutdown, nothing in *out */
        if (!atomic_load_explicit(&g_running, memory_order_acquire))
            break;                   /* raced with deinit; drop what we have */
        if (rc != 0) {
            pthread_mutex_lock(&g_q.lock);
            g_q.errors++;
            pthread_mutex_unlock(&g_q.lock);
            usleep(ERROR_BACKOFF_US);
            continue;
        }

        /* History before the queue: a full queue must not lose a statistic. */
        cnt_add(&e);
        q_push(&e);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int evq_init(void)
{
    if (g_started)
        return 0;

    /* Note what is NOT here: a synchronous first read. evsrc_read_blocking()
     * has no timeout, so priming the state in init could hang the caller for
     * as long as the site stays quiet. Consumers get "empty" instead, which is
     * the honest answer. */
    pthread_mutex_lock(&g_q.lock);
    g_q.head = g_q.tail = 0;
    g_q.received = g_q.dropped = g_q.popped = g_q.errors = 0;
    g_q.stopping = false;
    pthread_mutex_unlock(&g_q.lock);

    pthread_mutex_lock(&g_cnt.lock);
    memset(g_cnt.n, 0, sizeof g_cnt.n);
    g_cnt.cur = 0;
    g_cnt.primed = false;
    g_cnt.unknown = 0;
    pthread_mutex_unlock(&g_cnt.lock);

    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        pthread_mutex_lock(&g_q.lock);
        g_q.stopping = true;
        pthread_mutex_unlock(&g_q.lock);
        return -1;
    }
    g_started = true;
    return 0;
}

void evq_deinit(void)
{
    if (!g_started)
        return;                      /* idempotent */

    /* THE point of this problem. The sampler is sitting inside a vendor call
     * with no timeout, so clearing a flag it cannot see is useless — the
     * thread would only notice after the next event, which may never come.
     *
     * 1. publish the stop, so the sampler cannot start another read;
     * 2. evsrc_wake() — the vendor's escape hatch, which makes the pending
     *    read return -2 (and, because it latches, makes any read that starts
     *    in the gap return -2 too: that closes the "woke it a microsecond too
     *    early" race). If the vendor only affected a *pending* call, we would
     *    have to loop wake + timed-join instead;
     * 3. release consumers blocked in evq_pop_timed, or they would sit out
     *    their whole timeout on a queue that will never fill again;
     * 4. join — and only now is it safe to say the thread is gone. */
    atomic_store_explicit(&g_running, false, memory_order_release);
    evsrc_wake();

    pthread_mutex_lock(&g_q.lock);
    g_q.stopping = true;
    pthread_cond_broadcast(&g_q.cv);
    pthread_mutex_unlock(&g_q.lock);

    pthread_join(g_thread, NULL);
    g_started = false;
}
