/* main.c — test harness + fake BMS + fake backend for 08_battery_energy_pipeline.
 *
 * GIVEN. Used unchanged by both energy_store.c (your stub) and
 * energy_store_solution.c, so do not edit it to make a check pass.
 *
 * How it keeps itself honest (see SPEC.md §3):
 *   - the fake BMS records GROUND TRUTH for every BMS_OK reading it produced,
 *     so every expected value is derived, never hard-coded;
 *   - randomness is a fixed-seed xorshift, not rand(), so macOS and Linux see
 *     the same sequence;
 *   - the fake backend logs the wall-clock time of every bms_upload() call, so
 *     the rate limit is measured, not trusted;
 *   - the energy reference is a brute-force per-segment clip of the ground
 *     truth — a different formulation from the prefix-sum implementation, so
 *     the two agreeing means something.
 *
 * Timeline: 4 s warm-up + 2 s reader stress + deinit + instant checks ≈ 6.5 s.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "bms.h"
#include "energy_store.h"

/* the fake BMS blocks BMS_MIN_US .. BMS_MIN_US+BMS_JITTER_US per call */
#define BMS_MIN_US      20000
#define BMS_JITTER_US   40000

static int fails;

static void check_true(const char *label, bool ok)
{
    if (!ok) fails++;
    printf("  %-52s %s\n", label, ok ? "PASS" : "FAIL");
}

static void check_d(const char *label, double got, double want)
{
    double tol = 1e-6 + 1e-9 * fabs(want);
    bool ok = (fabs(got - want) <= tol);
    if (!ok) fails++;
    printf("  %-40s got %14.6f want %14.6f %s\n", label, got, want, ok ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* ground truth: every BMS_OK reading the fake BMS handed out          */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 8192
static struct { uint64_t ts; float volts; float amps; } truth[TRUTH_MAX];
static int truth_n;
static pthread_mutex_t truth_lock = PTHREAD_MUTEX_INITIALIZER;

static double truth_power(int i) { return (double)truth[i].volts * (double)truth[i].amps; }

static void truth_record(const struct BmsSample *s)
{
    pthread_mutex_lock(&truth_lock);
    /* same rule the implementation must apply: a timestamp that does not move
     * forward (clock jump) is not appended, or the history stops being sorted */
    if (truth_n < TRUTH_MAX && (truth_n == 0 || s->timestamp > truth[truth_n - 1].ts)) {
        truth[truth_n].ts    = s->timestamp;
        truth[truth_n].volts = s->volts;
        truth[truth_n].amps  = s->amps;
        truth_n++;
    }
    pthread_mutex_unlock(&truth_lock);
}

static int truth_count(void)
{
    pthread_mutex_lock(&truth_lock);
    int n = truth_n;
    pthread_mutex_unlock(&truth_lock);
    return n;
}

/* exact bit match against something the BMS really produced */
static bool truth_contains(const struct BmsSample *s)
{
    bool found = false;
    pthread_mutex_lock(&truth_lock);
    for (int i = truth_n - 1; i >= 0 && !found; i--)
        found = (truth[i].ts == s->timestamp)
             && (memcmp(&truth[i].volts, &s->volts, sizeof(float)) == 0)
             && (memcmp(&truth[i].amps,  &s->amps,  sizeof(float)) == 0);
    pthread_mutex_unlock(&truth_lock);
    return found;
}

/* power at time t inside segment i, linearly interpolated (caller holds lock) */
static double ref_p_at(int i, uint64_t t)
{
    double p0 = truth_power(i), p1 = truth_power(i + 1);
    uint64_t a = truth[i].ts, b = truth[i + 1].ts;
    if (t <= a) return p0;
    if (t >= b) return p1;
    return p0 + (p1 - p0) * ((double)(t - a) / (double)(b - a));
}

/* Brute-force reference for energy_joules_between(): walk EVERY segment, clip
 * it to [t0,t1], trapezoid the clipped piece. O(n) and dead simple on purpose. */
static double ref_energy(uint64_t t0, uint64_t t1)
{
    double e = 0.0;
    pthread_mutex_lock(&truth_lock);
    if (truth_n >= 2 && t1 > t0) {
        uint64_t first = truth[0].ts, last = truth[truth_n - 1].ts;
        if (t0 < first) t0 = first;
        if (t1 > last)  t1 = last;
        for (int i = 0; t1 > t0 && i + 1 < truth_n; i++) {
            uint64_t a = truth[i].ts, b = truth[i + 1].ts;
            if (b <= t0 || a >= t1) continue;
            uint64_t lo = (a > t0) ? a : t0;
            uint64_t hi = (b < t1) ? b : t1;
            e += 0.5 * (ref_p_at(i, lo) + ref_p_at(i, hi)) * ((double)(hi - lo) / 1e6);
        }
    }
    pthread_mutex_unlock(&truth_lock);
    return e;
}

/* ------------------------------------------------------------------ */
/* fake backend: logs when every upload happened                       */
/* ------------------------------------------------------------------ */

#define UPLOG_MAX 4096
static uint64_t uplog[UPLOG_MAX];
static int uplog_n;
static pthread_mutex_t uplog_lock = PTHREAD_MUTEX_INITIALIZER;

static _Atomic uint64_t bad_upload_samples;  /* uploaded something the BMS never said */
static _Atomic uint64_t bad_batch_size;      /* n < 1 or n > UPLOAD_BATCH_MAX          */
static _Atomic uint64_t vendor_misuse;       /* two threads inside a vendor call       */
static _Atomic bool read_inflight;
static _Atomic bool upload_inflight;

/* ------------------------------------------------------------------ */
/* reader stress                                                       */
/* ------------------------------------------------------------------ */

static _Atomic bool stress_stop;
static uint64_t stress_range_t0, stress_range_t1;   /* a closed range in the past */

typedef struct {
    uint64_t calls, bad, unstable, max_us;
    double   first_e;
    bool     have_e;
} stress_stat_t;

static void *stress_reader(void *arg)
{
    stress_stat_t *st = arg;
    while (!atomic_load(&stress_stop)) {
        uint64_t t0 = bms_now_us();
        struct BmsSample s;
        int rc = bms_get_latest(&s);
        double e = energy_joules_between(stress_range_t0, stress_range_t1);
        struct BmsStats k = bms_stats();
        uint64_t dt = bms_now_us() - t0;

        if (dt > st->max_us) st->max_us = dt;
        if (rc == 0 && !truth_contains(&s)) st->bad++;         /* torn or invented */
        if (!isfinite(e)) st->bad++;
        /* the range is entirely in the past, so its energy can never change */
        if (!st->have_e) { st->first_e = e; st->have_e = true; }
        else if (memcmp(&st->first_e, &e, sizeof e) != 0) st->unstable++;
        if (k.uploaded + k.dropped > k.sampled) st->bad++;     /* counters lying */
        st->calls++;
        /* A little pacing: 4 fully saturated spin loops measure the scheduler,
         * not the getters. ~5000 calls/s/thread still interleaves plenty. */
        usleep(100);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* rate-limit measurement                                             */
/* ------------------------------------------------------------------ */

static void check_rate_limit(void)
{
    int worst = 0;
    pthread_mutex_lock(&uplog_lock);
    int n = uplog_n;
    for (int i = 0; i < n; i++) {
        int c = 0;
        for (int j = i; j < n && uplog[j] - uplog[i] < 1000000ull; j++) c++;
        if (c > worst) worst = c;
    }
    pthread_mutex_unlock(&uplog_lock);

    /* A token bucket can spend at most (tokens in the bucket) + (tokens refilled
     * during the window) = UPLOAD_BURST + UPLOAD_RATE_PER_SEC uploads in any
     * 1-second window. +1 for scheduling slop. */
    int bound = UPLOAD_RATE_PER_SEC + UPLOAD_BURST + 1;
    printf("  uploads %d, worst 1-s window %d, bound %d (rate %d + burst %d + 1)\n",
           n, worst, bound, UPLOAD_RATE_PER_SEC, UPLOAD_BURST);
    check_true("rate limit holds in every 1-second window", n > 0 && worst <= bound);
    check_true("uploads actually happened (>= 5)", n >= 5);
}

/* ------------------------------------------------------------------ */
/* test                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct BmsSample s;

    printf("== phase 0: getters before init (static state must be safe) ==\n");
    check_d("empty history", energy_joules_between(0, UINT64_MAX), 0.0);
    check_true("get_latest before init returns != 0", bms_get_latest(&s) != 0);

    if (bms_pipeline_init() != 0) {
        printf("bms_pipeline_init failed\n");
        return 1;
    }
    uint64_t start = bms_now_us();

    printf("\n== phase 1: Part 1 publish, right after init ==\n");
    int n_before = truth_count();
    int rc = bms_get_latest(&s);
    check_true("get_latest has a sample right after init", rc == 0);
    if (rc == 0) {
        printf("    latest: %.3f V * %.3f A = %.3f W @ t=%llu\n",
               (double)s.volts, (double)s.amps, (double)s.volts * (double)s.amps,
               (unsigned long long)s.timestamp);
        check_true("that sample is one the BMS really produced", truth_contains(&s));
    }
    /* exactly one sample so far: one point has no interval, so 0 J */
    double e_single = energy_joules_between(0, bms_now_us());
    if (n_before == 1 && truth_count() == 1)
        check_d("single sample -> 0 J", e_single, 0.0);
    else
        printf("  (skip single-sample check: %d sample(s) recorded)\n", truth_count());

    printf("\n== phase 2: pipeline runs 4 s with the uploader rate limited ==\n");
    usleep(4000 * 1000);
    struct BmsStats st = bms_stats();
    double elapsed_s = (double)(bms_now_us() - start) / 1e6;
    printf("  sampled %llu  uploaded %llu  dropped %llu  batches %llu\n",
           (unsigned long long)st.sampled, (unsigned long long)st.uploaded,
           (unsigned long long)st.dropped, (unsigned long long)st.batches);
    printf("  stale %llu  errors %llu  upload_wait %llu us  max_publish %llu us\n",
           (unsigned long long)st.stale, (unsigned long long)st.errors,
           (unsigned long long)st.upload_wait_us, (unsigned long long)st.max_publish_us);

    check_true("sampled > 0", st.sampled > 0);
    check_true("uploaded > 0", st.uploaded > 0);
    check_true("nothing invented: sampled >= uploaded + dropped",
               st.sampled >= st.uploaded + st.dropped);
    check_true("sampled matches the BMS's own count",
               st.sampled <= (uint64_t)truth_count());
    check_true("every uploaded sample came from the BMS",
               atomic_load(&bad_upload_samples) == 0);
    check_true("batch size within 1..UPLOAD_BATCH_MAX",
               atomic_load(&bad_batch_size) == 0);
    check_true("no two threads inside a vendor call at once",
               atomic_load(&vendor_misuse) == 0);
    check_true("uploader really was throttled (upload_wait_us > 0)",
               st.upload_wait_us > 0);

    /* Back-pressure: only demand drops if the sampler genuinely out-ran the
     * uploader's ceiling on this machine. */
    uint64_t ceiling = (uint64_t)(elapsed_s * UPLOAD_RATE_PER_SEC * UPLOAD_BATCH_MAX)
                     + UPLOAD_QUEUE_CAP + UPLOAD_BURST * UPLOAD_BATCH_MAX;
    if (st.sampled > ceiling)
        check_true("back-pressure dropped instead of stalling stage 1", st.dropped > 0);
    else
        printf("  (skip drop check: sampled %llu <= uploader ceiling %llu)\n",
               (unsigned long long)st.sampled, (unsigned long long)ceiling);

    /* The sampler must never be dragged down to the uploader's pace. The BMS
     * allows ~1000000/(BMS_MIN_US+BMS_JITTER_US/2) readings per second; the
     * uploader can only absorb UPLOAD_RATE_PER_SEC*UPLOAD_BATCH_MAX of them. */
    double arrivals_per_s = (double)st.sampled / elapsed_s;
    double upload_ceiling_per_s = (double)(UPLOAD_RATE_PER_SEC * UPLOAD_BATCH_MAX);
    printf("  arrivals %.1f/s vs uploader ceiling %.1f/s\n",
           arrivals_per_s, upload_ceiling_per_s);
    check_true("stage 1 not clamped to the upload rate (> 12 samples/s)",
               arrivals_per_s > 12.0);
    check_true("worst sampler publish < 10 ms (never waits on the uploader)",
               st.max_publish_us < 10000);

    printf("\n== phase 3: 4 readers hammer the getters for 2 s ==\n");
    stress_range_t0 = start;
    stress_range_t1 = start + 1000 * 1000;   /* closed range, fully in the past */
    atomic_store(&stress_stop, false);
    pthread_t th[4];
    stress_stat_t sstat[4];
    memset(sstat, 0, sizeof sstat);
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, stress_reader, &sstat[i]);
    usleep(2000 * 1000);
    atomic_store(&stress_stop, true);
    uint64_t calls = 0, bad = 0, unstable = 0, max_us = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        calls    += sstat[i].calls;
        bad      += sstat[i].bad;
        unstable += sstat[i].unstable;
        if (sstat[i].max_us > max_us) max_us = sstat[i].max_us;
    }
    printf("  calls %llu, torn/invented %llu, changing past energy %llu, worst latency %llu us\n",
           (unsigned long long)calls, (unsigned long long)bad,
           (unsigned long long)unstable, (unsigned long long)max_us);
    check_true("readers made progress", calls > 0);
    check_true("no torn or invented values", bad == 0);
    check_true("energy over a past range never changed", unstable == 0);
    /* one rate-limit interval is 1e6/UPLOAD_RATE_PER_SEC us = 200 ms, and the
     * BMS blocks up to 60 ms: a getter must be nowhere near either */
    check_true("worst getter latency < 20 ms (BMS blocks 60, throttle 200)",
               max_us < 20000);

    printf("\n== phase 4: measured upload rate over the whole run ==\n");
    check_rate_limit();

    printf("\n== phase 5: shutdown ==\n");
    uint64_t d0 = bms_now_us();
    bms_pipeline_deinit();
    uint64_t dwell = bms_now_us() - d0;
    printf("  deinit took %llu us\n", (unsigned long long)dwell);
    check_true("deinit woke the timed wait instead of waiting it out (< 1.5 s)",
               dwell < 1500000);
    struct BmsStats a = bms_stats();
    printf("  final: sampled %llu uploaded %llu dropped %llu batches %llu"
           " stale %llu errors %llu\n",
           (unsigned long long)a.sampled, (unsigned long long)a.uploaded,
           (unsigned long long)a.dropped, (unsigned long long)a.batches,
           (unsigned long long)a.stale, (unsigned long long)a.errors);
    usleep(300 * 1000);
    struct BmsStats b = bms_stats();
    check_true("both threads really stopped (counters frozen)",
               a.sampled == b.sampled && a.uploaded == b.uploaded
               && a.dropped == b.dropped && a.batches == b.batches);

    printf("\n== phase 6: Part 2 energy vs brute force (history now frozen) ==\n");
    int n = truth_count();
    printf("  %d samples in the 30-minute history\n", n);
    if (n < 14) {
        fails++;
        printf("  FAIL: only %d samples recorded - Part 1 has to work first\n", n);
    } else {
        /* threads are joined, so reading truth[] directly here is safe */
        uint64_t first = truth[0].ts, last = truth[n - 1].ts;
        uint64_t seg5 = truth[6].ts - truth[5].ts;

        struct { const char *label; uint64_t t0, t1; } q[] = {
            { "whole span",                first,                                 last },
            { "sample-aligned [4..9]",      truth[4].ts,                           truth[9].ts },
            { "partial at both edges",     truth[2].ts + (truth[3].ts - truth[2].ts) / 3,
                                           truth[n - 4].ts + (truth[n - 3].ts - truth[n - 4].ts) * 2 / 3 },
            { "both ends in ONE segment",  truth[5].ts + seg5 / 4, truth[5].ts + (seg5 * 3) / 4 },
            { "1 ms slice inside a segment", truth[5].ts + seg5 / 2, truth[5].ts + seg5 / 2 + 1000 },
            { "t0 == t1",                  truth[7].ts,                           truth[7].ts },
            { "t0 > t1 (inverted)",        truth[9].ts,                           truth[4].ts },
            { "entirely before the first sample", first - 5000000ull,             first - 1000000ull },
            { "entirely after the last sample",   last + 1000000ull,              last + 5000000ull },
            { "starts before the first sample",   first - 1000000ull,             truth[6].ts },
            { "ends after the last sample",       truth[n - 6].ts,                last + 5000000ull },
            { "older than the 30-min window",     last - 2ull * ENERGY_WINDOW_US,
                                                  last - ENERGY_WINDOW_US - 1000000ull },
        };
        for (size_t i = 0; i < sizeof q / sizeof q[0]; i++)
            check_d(q[i].label, energy_joules_between(q[i].t0, q[i].t1),
                    ref_energy(q[i].t0, q[i].t1));

        /* additivity: splitting a range in the middle of a segment must not
         * change the total. This is what catches edge-interpolation bugs. */
        uint64_t mid = truth[8].ts + (truth[9].ts - truth[8].ts) / 2;
        double whole = energy_joules_between(truth[3].ts, truth[12].ts);
        double part1 = energy_joules_between(truth[3].ts, mid);
        double part2 = energy_joules_between(mid, truth[12].ts);
        printf("  additivity: %.6f + %.6f = %.6f vs %.6f\n",
               part1, part2, part1 + part2, whole);
        check_d("energy(a,m) + energy(m,b) == energy(a,b)", part1 + part2, whole);

        /* every single segment, one at a time, against the reference */
        int seg_bad = 0;
        for (int i = 0; i + 1 < n; i++) {
            double got  = energy_joules_between(truth[i].ts, truth[i + 1].ts);
            double want = ref_energy(truth[i].ts, truth[i + 1].ts);
            if (fabs(got - want) > 1e-6 + 1e-9 * fabs(want)) seg_bad++;
        }
        check_true("every individual segment matches the reference", seg_bad == 0);
    }

    printf("\n");
    if (fails) printf("%d CHECK(S) FAILED\n", fails);
    else       printf("ALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}

/* ================================================================== */
/* the vendor blob, faked                                             */
/* ================================================================== */

/* Fixed-seed PRNG of our own: same sequence on macOS and Linux (SPEC §3.2),
 * unlike rand(). A bare xorshift32 is not good enough here - bms_read() draws
 * several numbers per call, so each decision always lands on the same lane of
 * the sequence, and a plain xorshift's lanes are correlated enough that the
 * 4%-probability branch never fired at all. Weyl counter + murmur3 finalizer
 * fixes that and is still four lines.
 * Only ever touched from inside bms_read(), i.e. by one thread at a time (main
 * during init, then the sampler; pthread_create provides the handoff). */
static uint32_t rng_state = 0x2f6e2b1u;

static uint32_t rng_next(void)
{
    rng_state += 0x9E3779B9u;
    uint32_t z = rng_state;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static double rng_unit(void) { return (double)(rng_next() >> 8) / (double)(1u << 24); }

uint64_t bms_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

/* poison: what an uninitialised vendor struct looks like when you trust it */
static void poison(struct BmsSample *r)
{
    r->volts = -1.0e30f;
    r->amps  =  1.0e30f;
    r->timestamp = 1;
}

struct BmsSample bms_read(void)
{
    static bool first_call = true;
    struct BmsSample r;

    if (atomic_exchange(&read_inflight, true))
        atomic_fetch_add(&vendor_misuse, 1);

    if (first_call) {
        first_call = false;              /* the first call never blocks and is valid */
    } else {
        /* 1 in 40 calls: transport glitch, returns at once, fields are garbage */
        if (rng_unit() < 0.04) {
            r.status = BMS_ERROR;
            poison(&r);
            atomic_store(&read_inflight, false);
            return r;
        }
        /* the BMS reports when it feels like it: 20..60 ms */
        usleep((useconds_t)(BMS_MIN_US + (uint32_t)(rng_unit() * BMS_JITTER_US)));
        /* 1 in 4: the pack reading has not moved */
        if (rng_unit() < 0.25) {
            r.status = BMS_STALE;
            poison(&r);
            atomic_store(&read_inflight, false);
            return r;
        }
    }

    /* 24 V nominal pack; + = load draw, - = solar charging the pack */
    r.status = BMS_OK;
    r.volts  = (float)(24.0 + 5.0 * rng_unit());
    r.amps   = (float)(-5.0 + 20.0 * rng_unit());
    r.timestamp = bms_now_us();
    truth_record(&r);                       /* harness only */
    atomic_store(&read_inflight, false);
    return r;
}

int bms_upload(const struct BmsSample *batch, int n)
{
    if (atomic_exchange(&upload_inflight, true))
        atomic_fetch_add(&vendor_misuse, 1);

    pthread_mutex_lock(&uplog_lock);
    if (uplog_n < UPLOG_MAX) uplog[uplog_n++] = bms_now_us();
    pthread_mutex_unlock(&uplog_lock);

    if (n < 1 || n > UPLOAD_BATCH_MAX) atomic_fetch_add(&bad_batch_size, 1);
    if (batch == NULL) {
        atomic_fetch_add(&bad_batch_size, 1);
    } else {
        for (int i = 0; i < n && i < UPLOAD_BATCH_MAX; i++)
            if (!truth_contains(&batch[i])) atomic_fetch_add(&bad_upload_samples, 1);
    }

    usleep(3000);                           /* network */
    atomic_store(&upload_inflight, false);
    return 0;
}
