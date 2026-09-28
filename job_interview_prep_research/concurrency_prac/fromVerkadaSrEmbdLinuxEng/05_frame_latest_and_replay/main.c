/* main.c — test harness + fake capture unit for 05_frame_latest_and_replay.
 *
 *   ./main.sh          build main.c + frame_store.c          (your code)
 *   ./main.sh sol      build main.c + frame_store_solution.c (reference)
 *
 * GIVEN — do not edit. The same file builds against the stub and the solution.
 *
 * What is faked here (bottom of the file):
 *   capture_next_frame() blocks ~25 ms, then fills the frame with a pattern
 *   derived from the frame's sequence number, IN TWO HALVES with a 0.5 ms gap
 *   the way a DMA engine would. So a reader that is handed a buffer the sensor
 *   is still writing sees a frame whose body does not match its header, and
 *   frame_check() catches it. That is the tearing test.
 *
 * Ground truth: every frame the fake sensor produced (seq, ts) is recorded
 * here, and every PASS/FAIL is computed against that — never against
 * hard-coded numbers (timings differ per machine).
 *
 * Randomness is a fixed-seed xorshift, never rand(): rand() differs between
 * glibc and Apple libc.
 */
/* glibc hides usleep()/nanosleep() when the compiler is in strict -std=c11
 * mode; Apple's libc does not care. Set the feature macro before any
 * include so this builds warning-free on Linux and macOS alike. */
#define _DEFAULT_SOURCE 1

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "capture.h"
#include "frame_store.h"

#define STRESS_READERS   4
#define STRESS_US        2000000     /* 2 s of 4 readers hammering the getters */
#define WARMUP_US        900000      /* build ~1 s of replay history first     */
#define PIN_HOLD_US      400000      /* how long we pin frames on the writer   */
#define LATENCY_ITERS    2000
#define LATENCY_LIMIT_US 5000        /* 1/5 of one vendor blocking call        */

/* ------------------------------------------------------------------ */
/* the sensor's byte pattern (harness knows it, so tearing is visible) */
/* ------------------------------------------------------------------ */

#define SEQ_BYTES ((size_t)sizeof(uint64_t))

static uint8_t frame_byte(uint64_t seq, size_t i)
{
    return (uint8_t)(seq * 131u + i * 17u + 0x5Au);
}

static void frame_fill_header(uint8_t *dst, uint64_t seq)
{
    memcpy(dst, &seq, SEQ_BYTES);
}

static void frame_fill_range(uint8_t *dst, uint64_t seq, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++)
        dst[i] = frame_byte(seq, i);
}

/* true if the frame is internally consistent: body matches the seq in its
 * header. *seq_out gets that seq. A torn frame fails this. */
static bool frame_check(const uint8_t *f, uint64_t *seq_out)
{
    uint64_t seq;
    memcpy(&seq, f, SEQ_BYTES);
    if (seq == 0)
        return false;                       /* never-written buffer */
    for (size_t i = SEQ_BYTES; i < FRAME_BYTES; i++)
        if (f[i] != frame_byte(seq, i))
            return false;
    if (seq_out)
        *seq_out = seq;
    return true;
}

/* ------------------------------------------------------------------ */
/* ground truth                                                        */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 8192
static struct { uint64_t seq, ts; } g_truth[TRUTH_MAX];
static size_t g_truth_n;
static pthread_mutex_t g_truth_lock = PTHREAD_MUTEX_INITIALIZER;

static void truth_record(uint64_t seq, uint64_t ts)
{
    pthread_mutex_lock(&g_truth_lock);
    if (g_truth_n < TRUTH_MAX) {
        g_truth[g_truth_n].seq = seq;
        g_truth[g_truth_n].ts  = ts;
        g_truth_n++;
    }
    pthread_mutex_unlock(&g_truth_lock);
}

static size_t truth_count(void)
{
    pthread_mutex_lock(&g_truth_lock);
    size_t n = g_truth_n;
    pthread_mutex_unlock(&g_truth_lock);
    return n;
}

static bool truth_nth(size_t i, uint64_t *seq, uint64_t *ts)
{
    bool ok = false;
    pthread_mutex_lock(&g_truth_lock);
    if (i < g_truth_n) {
        if (seq) *seq = g_truth[i].seq;
        if (ts)  *ts  = g_truth[i].ts;
        ok = true;
    }
    pthread_mutex_unlock(&g_truth_lock);
    return ok;
}

/* seq numbers are handed out 1,2,3,... and recorded in order */
static bool truth_ts_of_seq(uint64_t seq, uint64_t *ts)
{
    if (seq == 0)
        return false;
    return truth_nth((size_t)(seq - 1), NULL, ts);
}

/* newest frame with ts <= t */
static bool truth_at(uint64_t t, uint64_t *seq, uint64_t *ts)
{
    bool ok = false;
    pthread_mutex_lock(&g_truth_lock);
    for (size_t i = 0; i < g_truth_n && g_truth[i].ts <= t; i++) {
        if (seq) *seq = g_truth[i].seq;
        if (ts)  *ts  = g_truth[i].ts;
        ok = true;
    }
    pthread_mutex_unlock(&g_truth_lock);
    return ok;
}

/* The newest frame strictly older than `cut`, plus the timestamp of the first
 * frame at or after `cut`. Used to build a window-edge query whose answer is a
 * frame that is itself older than the window. */
static bool truth_edge(uint64_t cut, uint64_t *fseq, uint64_t *fts, uint64_t *gts)
{
    bool ok = false;
    pthread_mutex_lock(&g_truth_lock);
    for (size_t i = 0; i < g_truth_n; i++) {
        if (g_truth[i].ts < cut) {
            if (fseq) *fseq = g_truth[i].seq;
            if (fts)  *fts  = g_truth[i].ts;
            ok = true;
        } else {
            if (gts) *gts = g_truth[i].ts;
            break;
        }
    }
    pthread_mutex_unlock(&g_truth_lock);
    return ok;
}

static size_t truth_count_in(uint64_t t0, uint64_t t1)
{
    size_t c = 0;
    if (t0 > t1)
        return 0;
    pthread_mutex_lock(&g_truth_lock);
    for (size_t i = 0; i < g_truth_n; i++)
        if (g_truth[i].ts >= t0 && g_truth[i].ts <= t1)
            c++;
    pthread_mutex_unlock(&g_truth_lock);
    return c;
}

/* ------------------------------------------------------------------ */
/* check plumbing                                                      */
/* ------------------------------------------------------------------ */

static int g_fails;

static void okf(bool cond, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void okf(bool cond, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("  %-66s %s\n", buf, cond ? "PASS" : "FAIL");
    if (!cond)
        g_fails++;
}

/* ------------------------------------------------------------------ */
/* stress readers: tearing, invented values, latency                   */
/* ------------------------------------------------------------------ */

static _Atomic bool g_stress_stop;

typedef struct {
    int      id;
    uint64_t acquires, nulls, torn, bad_ts, replay_ok, replay_torn, replay_bad;
    uint64_t max_acquire_us, max_at_us;
} reader_stat_t;

static void *stress_reader(void *arg)
{
    reader_stat_t *st = arg;
    uint32_t rng = 0x1234567u + (uint32_t)st->id * 0x9E3779B9u;
    uint8_t  copy[FRAME_BYTES];

    while (!atomic_load(&g_stress_stop)) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;

        /* ---- Part 1: borrow the latest frame ---- */
        uint64_t ts = 0, seq = 0, t0 = capture_now_us();
        const uint8_t *f = frame_acquire_latest(&ts);
        uint64_t t1 = capture_now_us();
        if (t1 - t0 > st->max_acquire_us)
            st->max_acquire_us = t1 - t0;

        if (!f) {
            st->nulls++;
        } else {
            st->acquires++;
            if (!frame_check(f, &seq)) {
                st->torn++;                      /* saw a half-written frame */
            } else {
                uint64_t real_ts = 0;
                if (!truth_ts_of_seq(seq, &real_ts) || real_ts != ts)
                    st->bad_ts++;                /* invented frame or wrong ts */
            }
            /* hold it a while: the writer must keep going without us */
            if (st->id > 0)
                usleep(200u * (unsigned)st->id);
            /* re-check after the hold: the borrow must still be stable */
            uint64_t seq2 = 0;
            if (!frame_check(f, &seq2) || seq2 != seq)
                st->torn++;
            frame_release(f);
        }

        /* ---- Part 2: replay a point 200..800 ms in the past ---- */
        uint64_t now = capture_now_us();
        uint64_t qt = now - (200000u + rng % 600000u);
        uint64_t ats = 0;
        t0 = capture_now_us();
        int r = frame_at(qt, copy, &ats);
        t1 = capture_now_us();
        if (t1 - t0 > st->max_at_us)
            st->max_at_us = t1 - t0;
        if (r == 0) {
            uint64_t rseq = 0, real_ts = 0;
            if (!frame_check(copy, &rseq))
                st->replay_torn++;
            else if (ats > qt || !truth_ts_of_seq(rseq, &real_ts) || real_ts != ats)
                st->replay_bad++;
            else
                st->replay_ok++;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint8_t  buf[FRAME_BYTES];
    uint64_t ats = 0, seq = 0, ts = 0;
    int r;

    printf("== Lifecycle ==\n");
    uint64_t t_init = capture_now_us();
    if (frame_store_init() != 0) {
        printf("  frame_store_init() failed\n");
        return 1;
    }
    uint64_t init_us = capture_now_us() - t_init;
    uint64_t start = capture_now_us();
    okf(truth_count() >= 1, "init published a frame before returning (took %llu ms)",
        (unsigned long long)(init_us / 1000));

    printf("\n== Part 1: frame_acquire_latest / frame_release ==\n");
    const uint8_t *f = frame_acquire_latest(&ts);
    okf(f != NULL, "a frame is available the moment init returns");
    if (f) {
        bool pat = frame_check(f, &seq);
        okf(pat, "frame is internally consistent (seq %llu)", (unsigned long long)seq);
        uint64_t real_ts = 0;
        okf(pat && truth_ts_of_seq(seq, &real_ts) && real_ts == ts,
            "its timestamp is the one the sensor reported");

        /* borrowed, not copied: two acquires of the same frame must alias */
        uint64_t ts2 = 0;
        const uint8_t *g = frame_acquire_latest(&ts2);
        okf(g != NULL && (ts2 != ts || g == f),
            "acquire hands out a borrowed pointer, not a per-reader copy");
        okf(frame_borrowed_count() == 2, "two borrows outstanding (refcount = %zu)",
            frame_borrowed_count());
        if (g)
            frame_release(g);
        frame_release(f);
        okf(frame_borrowed_count() == 0, "refcount back to 0 after both releases");
    }

    /* Part 2 boundary that only exists at start-up: t inside the window but
     * before the very first frame. */
    uint64_t first_ts = 0;
    if (truth_nth(0, NULL, &first_ts))
        okf(frame_at(first_ts - 100000, buf, &ats) == -1,
            "frame_at() before the first frame ever captured -> -1");
    else
        okf(false, "frame_at() before the first frame ever captured -> -1");

    okf(frame_dropped_count() == 0,
        "no frames dropped while readers release promptly (dropped %llu)",
        (unsigned long long)frame_dropped_count());

    usleep(WARMUP_US);              /* ~1 s of history to replay over */

    printf("\n== Part 2: frame_at / frame_count_in vs ground truth ==\n");
    uint64_t now = capture_now_us();
    uint64_t q = now - 300000;
    uint64_t eseq = 0, ets = 0;
    bool have = truth_at(q, &eseq, &ets);
    r = frame_at(q, buf, &ats);
    okf(have && r == 0, "frame_at(now - 300 ms) finds a frame");
    if (r == 0) {
        uint64_t gseq = 0;
        bool pat = frame_check(buf, &gseq);
        okf(pat, "replayed frame is internally consistent");
        okf(pat && gseq == eseq && ats == ets,
            "it is the frame in effect at t (got seq %llu, truth seq %llu)",
            (unsigned long long)gseq, (unsigned long long)eseq);
        okf(ats <= q, "actual_ts <= requested t");
    }

    /* exact timestamp match, and a t strictly between two frames */
    size_t n = truth_count();
    uint64_t xseq = 0, xts = 0, nseq = 0, nts = 0;
    if (n >= 12 && truth_nth(n - 10, &xseq, &xts) && truth_nth(n - 9, &nseq, &nts)) {
        uint64_t gseq = 0;
        r = frame_at(xts, buf, &ats);
        okf(r == 0 && ats == xts && frame_check(buf, &gseq) && gseq == xseq,
            "exact timestamp match returns exactly that frame (seq %llu)",
            (unsigned long long)xseq);
        if (nts > xts + 1) {
            gseq = 0;
            r = frame_at(xts + 1, buf, &ats);
            okf(r == 0 && ats == xts && frame_check(buf, &gseq) && gseq == xseq,
                "t between two frames returns the older one (the one in effect)");
        }
    } else {
        okf(false, "exact timestamp match (not enough frames captured: %zu)", n);
    }

    okf(frame_at(now + 1000000, buf, &ats) == -1, "t one second in the future -> -1");

    uint64_t c0 = now - 700000, c1 = now - 200000;
    size_t got_c  = frame_count_in(c0, c1);
    size_t want_c = truth_count_in(c0, c1);
    okf(got_c == want_c, "frame_count_in(now-700ms, now-200ms) = %zu, truth %zu",
        got_c, want_c);
    okf(frame_count_in(c1, c0) == 0, "reversed range (t0 > t1) -> 0");
    okf(frame_count_in(now + 1000000, now + 2000000) == 0, "range in the future -> 0");
    if (xts)
        okf(frame_count_in(xts, xts) == 1, "single-point range on a real timestamp -> 1");
    okf(frame_replay_bytes() <= FRAME_REPLAY_BYTES_MAX,
        "replay history %zu B is within the %u B budget",
        frame_replay_bytes(), (unsigned)FRAME_REPLAY_BYTES_MAX);

    printf("\n== No tearing: %d readers borrow frames for %d s while the writer runs ==\n",
           STRESS_READERS, (int)(STRESS_US / 1000000));
    pthread_t th[STRESS_READERS];
    reader_stat_t st[STRESS_READERS];
    memset(st, 0, sizeof st);
    atomic_store(&g_stress_stop, false);
    for (int i = 0; i < STRESS_READERS; i++) {
        st[i].id = i;
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    }
    usleep(STRESS_US);
    atomic_store(&g_stress_stop, true);
    reader_stat_t tot;
    memset(&tot, 0, sizeof tot);
    for (int i = 0; i < STRESS_READERS; i++) {
        pthread_join(th[i], NULL);
        tot.acquires    += st[i].acquires;
        tot.nulls       += st[i].nulls;
        tot.torn        += st[i].torn;
        tot.bad_ts      += st[i].bad_ts;
        tot.replay_ok   += st[i].replay_ok;
        tot.replay_torn += st[i].replay_torn;
        tot.replay_bad  += st[i].replay_bad;
        if (st[i].max_acquire_us > tot.max_acquire_us) tot.max_acquire_us = st[i].max_acquire_us;
        if (st[i].max_at_us      > tot.max_at_us)      tot.max_at_us      = st[i].max_at_us;
    }
    printf("    acquires %llu, NULLs %llu, replay hits %llu, dropped %llu, sensor errors %llu\n",
           (unsigned long long)tot.acquires, (unsigned long long)tot.nulls,
           (unsigned long long)tot.replay_ok, (unsigned long long)frame_dropped_count(),
           (unsigned long long)frame_error_count());
    okf(tot.acquires > 0, "readers got frames (%llu acquires)", (unsigned long long)tot.acquires);
    okf(tot.torn == 0, "no torn frame ever observed (%llu)", (unsigned long long)tot.torn);
    okf(tot.bad_ts == 0, "no invented frame / wrong timestamp (%llu)",
        (unsigned long long)tot.bad_ts);
    okf(tot.nulls == 0, "acquire never returned NULL after the first frame (%llu)",
        (unsigned long long)tot.nulls);
    okf(tot.replay_ok > 0, "replay queries succeeded (%llu)", (unsigned long long)tot.replay_ok);
    okf(tot.replay_torn == 0, "no torn replayed frame (%llu)", (unsigned long long)tot.replay_torn);
    okf(tot.replay_bad == 0, "every replayed frame is a real frame with ts <= t (%llu bad)",
        (unsigned long long)tot.replay_bad);
    okf(frame_borrowed_count() == 0, "no borrows left outstanding after the stress");
    okf(frame_release_errors() == 0, "no release errors during the stress");

    printf("\n== Part 2: window eviction and budget ==\n");
    okf(capture_now_us() - start > FRAME_REPLAY_WINDOW_US,
        "the run is now longer than the %llu s window, so eviction has happened",
        (unsigned long long)(FRAME_REPLAY_WINDOW_US / 1000000));
    okf(frame_replay_bytes() <= FRAME_REPLAY_BYTES_MAX,
        "replay history %zu B still within the %u B budget",
        frame_replay_bytes(), (unsigned)FRAME_REPLAY_BYTES_MAX);
    okf(frame_at(capture_now_us() - FRAME_REPLAY_WINDOW_US - 300000, buf, &ats) == -1,
        "t older than the window -> -1");
    {
        /* The oldest still-legal query time is cut = now - window. The frame in
         * effect at cut was captured BEFORE cut, so it is itself older than the
         * window: a store that evicts everything older than the window throws
         * away the answer to a legal query. Build exactly that query.
         *
         * Eviction is lazy (it runs when a frame is appended), so wait until a
         * frame has just been published AND the frame covering the edge is
         * comfortably older than the window -- otherwise a store with the wrong
         * rule looks correct simply because it has not swept yet. */
        uint64_t cut = 0, fseq = 0, fts = 0, gts = 0, prev_ts = 0;
        bool setup = false;
        const uint8_t *p = frame_acquire_latest(&prev_ts);
        if (p)
            frame_release(p);
        for (int i = 0; i < 800 && !setup; i++) {
            usleep(500);
            uint64_t cur_ts = 0;
            p = frame_acquire_latest(&cur_ts);
            if (p)
                frame_release(p);
            if (cur_ts == prev_ts)
                continue;                   /* no new frame -> no eviction pass */
            prev_ts = cur_ts;
            usleep(300);                    /* let the append/evict finish */
            fseq = fts = gts = 0;
            cut = capture_now_us() - FRAME_REPLAY_WINDOW_US;
            setup = truth_edge(cut, &fseq, &fts, &gts) &&
                    gts > cut + 4000 && cut > fts + 6000;
        }
        if (setup) {
            uint64_t t = cut + 2000;        /* inside the window ...           */
            uint64_t ge = 0;                /* ... the frame in effect is not  */
            r = frame_at(t, buf, &ats);
            bool borderline = (capture_now_us() - t) > FRAME_REPLAY_WINDOW_US;
            bool exact = (frame_dropped_count() == 0);
            okf((r == 0 && frame_check(buf, &ge) && ats <= t &&
                 (!exact || (ats == fts && ge == fseq))) ||
                    (r == -1 && borderline),
                "keep-one-older: the frame covering the window edge (captured "
                "%llu us before it) is still there",
                (unsigned long long)(cut - fts));
        } else {
            printf("    (skipped: could not line up a window-edge query)\n");
        }
    }

    printf("\n== Writer never blocks: pin distinct frames and watch the sensor drain ==\n");
    const uint8_t *held[4];
    uint64_t held_ts[4];
    int nheld = 0;
    bool starved = false;
    for (int i = 0; i < 4; i++) {
        uint64_t hts = 0;
        const uint8_t *p = frame_acquire_latest(&hts);
        if (!p)
            break;
        bool dup = false;
        for (int j = 0; j < nheld; j++)
            if (held_ts[j] == hts)
                dup = true;
        if (dup) {                  /* no new frame appeared: every buffer is pinned */
            frame_release(p);
            starved = true;
            break;
        }
        held[nheld] = p;
        held_ts[nheld] = hts;
        nheld++;
        usleep(3 * CAPTURE_PERIOD_US);
    }
    size_t   truth_before = truth_count();
    uint64_t drop_before  = frame_dropped_count();
    usleep(PIN_HOLD_US);
    size_t   truth_after = truth_count();
    uint64_t drop_after  = frame_dropped_count();
    printf("    pinned %d distinct frames, writer%s starved for a buffer\n",
           nheld, starved ? " WAS" : " was NOT");
    okf(truth_after - truth_before >= 8,
        "sensor kept being drained while frames were pinned (+%zu frames in %d ms)",
        truth_after - truth_before, (int)(PIN_HOLD_US / 1000));
    if (starved)
        okf(drop_after > drop_before,
            "dropped counter grows instead of the writer waiting (+%llu)",
            (unsigned long long)(drop_after - drop_before));
    else
        printf("    (buffers never all pinned; dropped +%llu)\n",
               (unsigned long long)(drop_after - drop_before));
    bool pins_intact = true;
    for (int i = 0; i < nheld; i++) {
        uint64_t sq = 0;
        if (!frame_check(held[i], &sq) || sq == 0)
            pins_intact = false;
    }
    okf(pins_intact && nheld > 0,
        "every pinned frame is still intact - the writer did not recycle it");
    okf(frame_borrowed_count() == (size_t)nheld,
        "refcount matches the %d frames still held", nheld);
    uint64_t last_pinned_ts = nheld ? held_ts[nheld - 1] : 0;
    for (int i = 0; i < nheld; i++)
        frame_release(held[i]);
    okf(frame_borrowed_count() == 0, "refcount back to 0 after releasing all of them");
    usleep(4 * CAPTURE_PERIOD_US);
    ts = 0;
    f = frame_acquire_latest(&ts);
    okf(f != NULL && ts > last_pinned_ts, "new frames flow again once readers let go");
    if (f)
        frame_release(f);

    printf("\n== Refcount hygiene ==\n");
    uint64_t re0 = frame_release_errors();
    ts = 0;
    f = frame_acquire_latest(&ts);
    if (f) {
        frame_release(f);
        frame_release(f);                       /* one release too many */
    }
    okf(frame_release_errors() == re0 + 1, "double release is detected, not absorbed");
    {
        static uint8_t not_ours[FRAME_BYTES];
        frame_release(not_ours);                /* never handed out */
    }
    okf(frame_release_errors() == re0 + 2, "releasing a foreign pointer is detected");
    frame_release(NULL);
    okf(frame_release_errors() == re0 + 2, "frame_release(NULL) is a no-op");
    okf(frame_borrowed_count() == 0, "still no borrows outstanding");

    printf("\n== Getter latency (vendor call blocks %d ms) ==\n", CAPTURE_PERIOD_US / 1000);
    uint64_t worst_acq = 0, worst_at = 0;
    for (int i = 0; i < LATENCY_ITERS; i++) {
        uint64_t a = capture_now_us();
        uint64_t lts = 0;
        const uint8_t *p = frame_acquire_latest(&lts);
        if (p)
            frame_release(p);
        uint64_t b = capture_now_us();
        if (b - a > worst_acq) worst_acq = b - a;
        frame_at(b - 250000, buf, &ats);
        uint64_t c = capture_now_us();
        if (c - b > worst_at) worst_at = c - b;
    }
    okf(worst_acq < LATENCY_LIMIT_US && worst_at < LATENCY_LIMIT_US,
        "worst acquire %llu us, worst frame_at %llu us (limit %d us)",
        (unsigned long long)worst_acq, (unsigned long long)worst_at, LATENCY_LIMIT_US);

    printf("\n== Shutdown ==\n");
    uint64_t d0 = capture_now_us();
    frame_store_deinit();
    uint64_t dus = capture_now_us() - d0;
    okf(dus < 300000, "deinit joined the writer thread in %llu ms",
        (unsigned long long)(dus / 1000));
    okf(frame_error_count() >= 1,
        "the sensor glitch path was exercised (%llu errors, %zu frames)",
        (unsigned long long)frame_error_count(), truth_count());

    if (g_fails)
        printf("\n%d CHECK(S) FAILED\n", g_fails);
    else
        printf("\nALL CHECKS PASSED (0 failed)\n");
    return g_fails ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* fake vendor capture unit (stands in for the SoC's ISP + DMA)         */
/* ------------------------------------------------------------------ */

static uint32_t xs_state = 0x9E3779B9u;

static uint32_t xs_next(void)
{
    uint32_t x = xs_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    xs_state = x;
    return x;
}

uint64_t capture_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

int capture_next_frame(uint8_t *dst, uint64_t *timestamp)
{
    static uint64_t calls, seq;         /* static state: ONE caller only */

    calls++;
    if (calls % 37 == 0) {              /* deterministic "sensor glitch" */
        usleep(300);
        return -1;
    }

    usleep(CAPTURE_PERIOD_US - 3500u + xs_next() % 6000u);   /* 21.5 .. 27.5 ms */

    /* The DMA lands the frame in two bursts. A buffer that a reader can see
     * during this window tears. */
    seq++;
    frame_fill_header(dst, seq);
    frame_fill_range(dst, seq, SEQ_BYTES, FRAME_BYTES / 2);
    usleep(500);
    frame_fill_range(dst, seq, FRAME_BYTES / 2, FRAME_BYTES);

    uint64_t ts = capture_now_us();
    *timestamp = ts;
    truth_record(seq, ts);              /* harness only */
    return 0;
}
