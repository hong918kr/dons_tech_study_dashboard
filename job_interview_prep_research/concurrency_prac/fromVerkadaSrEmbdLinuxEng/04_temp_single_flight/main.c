/* main.c — test harness + fake temperature probe.  GIVEN; do not modify.
 *
 * Used unchanged for both ./main.sh (your temp_cache.c) and ./main.sh sol.
 *
 * What is in here
 *   - the fake vendor blob (bottom of the file). It blocks for
 *     TEMPDEV_READ_US_DEFAULT us per call, and it DETECTS two threads being
 *     inside it at the same time: that is the bug this exercise is about,
 *     so it is reported as a hard failure, not a warning.
 *   - ground truth: every value the fake probe ever handed out, so a
 *     "value that never existed" can be spotted; every sample the harness
 *     injected into the history, so range queries are checked against a
 *     brute-force scan instead of hard-coded numbers.
 *   - a fixed-seed xorshift PRNG (never rand(): its sequence differs
 *     between glibc and macOS libc).
 *
 * Last line is exactly "ALL CHECKS PASSED (0 failed)" or "N CHECK(S) FAILED".
 */
#if defined(__linux__)
#  define _XOPEN_SOURCE 700
#endif

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "tempdev.h"
#include "temp_cache.h"

#define TEMPDEV_READ_US_DEFAULT 60000u   /* 60 ms: the scaled-down probe    */
#define SLOW_READ_US            250000u  /* 250 ms while testing coalescing */

/* ------------------------------------------------------------------ */
/* fixed-seed PRNG (xorshift32)                                        */
/* ------------------------------------------------------------------ */

static uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static float xs_u01(uint32_t *s)
{
    return (float)(xs32(s) >> 8) / (float)(1u << 24);
}

/* ------------------------------------------------------------------ */
/* check bookkeeping                                                   */
/* ------------------------------------------------------------------ */

static int fails;

static void ok(const char *label, bool cond)
{
    if (!cond) fails++;
    printf("  %-52s %s\n", label, cond ? "PASS" : "FAIL");
}

static const char *rcname(int rc)
{
    switch (rc) {
    case TEMP_OK:     return "TEMP_OK";
    case TEMP_STALE:  return "TEMP_STALE";
    case TEMP_NODATA: return "TEMP_NODATA";
    default:          return "???";
    }
}

/* ------------------------------------------------------------------ */
/* ground truth 1: every value the fake probe produced                 */
/* ------------------------------------------------------------------ */

#define VT_MAX 16384
static struct {
    pthread_mutex_t lk;
    float v[VT_MAX];
    int   n;
} vt = { .lk = PTHREAD_MUTEX_INITIALIZER };

static void vt_record(float v)
{
    pthread_mutex_lock(&vt.lk);
    if (vt.n < VT_MAX) vt.v[vt.n++] = v;
    pthread_mutex_unlock(&vt.lk);
}

static bool vt_contains(float v)
{
    bool found = false;
    pthread_mutex_lock(&vt.lk);
    for (int i = 0; i < vt.n && !found; i++)
        found = (memcmp(&vt.v[i], &v, sizeof v) == 0);   /* exact bits */
    pthread_mutex_unlock(&vt.lk);
    return found;
}

/* ------------------------------------------------------------------ */
/* ground truth 2: the history the harness injected                    */
/* ------------------------------------------------------------------ */

#define INJ_OLD 5        /* samples deliberately older than the window */
#define INJ_N   4000
static struct { uint64_t ts; float c; } inj[INJ_OLD + INJ_N];
static int inj_n;

/* Brute force over the injected samples, mirroring the documented
 * semantics: inclusive range, restricted to minutes the 1-hour window
 * still covers. Returns the sample count (0 == "expect -1"). */
static uint64_t bf_stats(uint64_t t0, uint64_t t1, float *mn, float *mx, double *avg)
{
    if (t0 > t1 || inj_n == 0) return 0;

    uint64_t newest_m = inj[inj_n - 1].ts / TEMP_BUCKET_US;
    uint64_t oldest_m = (newest_m >= TEMP_NBUCKETS - 1u)
                            ? newest_m - (TEMP_NBUCKETS - 1u) : 0;

    uint64_t n = 0;
    double sum = 0.0;
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < inj_n; i++) {
        uint64_t ts = inj[i].ts;
        uint64_t m = ts / TEMP_BUCKET_US;
        if (ts < t0 || ts > t1) continue;
        if (m < oldest_m || m > newest_m) continue;
        float c = inj[i].c;
        if (n == 0) { lo = c; hi = c; }
        else { if (c < lo) lo = c; if (c > hi) hi = c; }
        n++;
        sum += (double)c;
    }
    if (n == 0) return 0;
    if (mn) *mn = lo;
    if (mx) *mx = hi;
    if (avg) *avg = sum / (double)n;
    return n;
}

static bool close_enough(double got, double want)
{
    return fabs(got - want) <= 1e-3 * (1.0 + fabs(want));
}

/* ------------------------------------------------------------------ */
/* a single-use barrier (pthread_barrier_t does not exist on macOS)    */
/* ------------------------------------------------------------------ */

typedef struct {
    pthread_mutex_t lk;
    pthread_cond_t  cv;
    int need, arrived;
} barrier_t;

static void bar_init(barrier_t *b, int need)
{
    pthread_mutex_init(&b->lk, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->need = need;
    b->arrived = 0;
}

static void bar_wait(barrier_t *b)
{
    pthread_mutex_lock(&b->lk);
    if (++b->arrived >= b->need)
        pthread_cond_broadcast(&b->cv);
    else
        while (b->arrived < b->need)
            pthread_cond_wait(&b->cv, &b->lk);
    pthread_mutex_unlock(&b->lk);
}

static void bar_destroy(barrier_t *b)
{
    pthread_mutex_destroy(&b->lk);
    pthread_cond_destroy(&b->cv);
}

/* ------------------------------------------------------------------ */
/* the 8-thread simultaneous-demand wave                               */
/* ------------------------------------------------------------------ */

#define WAVE_N 8

typedef struct {
    barrier_t *bar;
    int        rc;
    float      v;
    uint64_t   lat_us;
} wave_arg_t;

static void *wave_thread(void *p)
{
    wave_arg_t *a = p;
    bar_wait(a->bar);
    uint64_t t0 = tempdev_now_us();
    a->rc = temp_get(&a->v, 0);          /* 0 == "must be fresher than now" */
    a->lat_us = tempdev_now_us() - t0;
    return NULL;
}

/* Runs one wave of WAVE_N simultaneous temp_get(_, 0). */
static void run_wave(wave_arg_t out[WAVE_N])
{
    barrier_t bar;
    pthread_t th[WAVE_N];
    bar_init(&bar, WAVE_N);
    for (int i = 0; i < WAVE_N; i++) {
        out[i].bar = &bar;
        out[i].rc = -999;
        out[i].v = NAN;
        out[i].lat_us = 0;
        pthread_create(&th[i], NULL, wave_thread, &out[i]);
    }
    for (int i = 0; i < WAVE_N; i++)
        pthread_join(th[i], NULL);
    bar_destroy(&bar);
}

/* ------------------------------------------------------------------ */
/* stress readers                                                      */
/* ------------------------------------------------------------------ */

static _Atomic bool stress_stop;
static uint64_t stress_r0, stress_r1;   /* a stable range in the old history */
static float    stress_min, stress_max;
static double   stress_avg;

typedef struct {
    int      id;
    uint64_t gets, ranges, hits, stales, nodata;
    uint64_t bad_value, bad_range;
    uint64_t max_get_us, max_range_us;
} stress_stat_t;

static void *stress_thread(void *p)
{
    stress_stat_t *s = p;
    int k = 0;
    while (!atomic_load(&stress_stop)) {
        uint64_t max_age;
        switch ((s->id + k) % 3) {
        case 0:  max_age = 0;                  break;  /* force a refresh  */
        case 1:  max_age = 5000;               break;  /* 5 ms: mostly miss */
        default: max_age = 3600ull * 1000000ull; break; /* always a hit     */
        }
        k++;

        float v = NAN;
        uint64_t t0 = tempdev_now_us();
        int rc = temp_get(&v, max_age);
        uint64_t dt = tempdev_now_us() - t0;
        if (dt > s->max_get_us) s->max_get_us = dt;
        s->gets++;
        if (rc == TEMP_OK) s->hits++;
        else if (rc == TEMP_STALE) s->stales++;
        else s->nodata++;
        /* Any value handed out must be one the probe really produced. */
        if ((rc == TEMP_OK || rc == TEMP_STALE) && !vt_contains(v))
            s->bad_value++;

        float mn = NAN, mx = NAN, av = NAN;
        t0 = tempdev_now_us();
        int r2 = temp_range_stats(stress_r0, stress_r1, &mn, &mx, &av);
        dt = tempdev_now_us() - t0;
        if (dt > s->max_range_us) s->max_range_us = dt;
        s->ranges++;
        if (r2 != 0 || mn != stress_min || mx != stress_max ||
            !close_enough((double)av, stress_avg))
            s->bad_range++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* deinit while a read is in flight                                    */
/* ------------------------------------------------------------------ */

static int late_rc;
static void *late_thread(void *p)
{
    (void)p;
    float v = NAN;
    late_rc = temp_get(&v, 0);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    temp_stats_t s0, s1;
    uint64_t c0, c1;
    float v = NAN;
    int rc;

    printf("fake probe: one tempdev_read() blocks %u us\n", tempdev_read_us());

    /* ============ Phase 0: cold start, and the bus is wedged ============ */
    printf("\n== Phase 0: init does not touch the bus; cold NACK ==\n");
    c0 = tempdev_calls();
    if (temp_cache_init() != 0) {
        printf("temp_cache_init failed\n");
        return 1;
    }
    ok("init performs zero vendor reads", tempdev_calls() == c0);

    tempdev_force_nack(true);
    v = NAN;
    rc = temp_get(&v, 0);
    printf("    temp_get on a wedged bus with no cached value -> %s\n", rcname(rc));
    ok("cold + NACK -> TEMP_NODATA", rc == TEMP_NODATA);
    ok("cold + NACK issued exactly 1 vendor read", tempdev_calls() == c0 + 1);
    tempdev_force_nack(false);

    /* ============ Phase 1: single flight ============ */
    printf("\n== Phase 1: 8 threads, stale cache -> ONE vendor read ==\n");
    tempdev_set_read_us(SLOW_READ_US);   /* widen the window so the test is
                                          * about coalescing, not scheduling */
    temp_cache_stats(&s0);
    c0 = tempdev_calls();
    wave_arg_t w[WAVE_N];
    run_wave(w);
    c1 = tempdev_calls();
    temp_cache_stats(&s1);

    bool all_ok = true, all_same = true, all_real = true;
    for (int i = 0; i < WAVE_N; i++) {
        if (w[i].rc != TEMP_OK) all_ok = false;
        if (memcmp(&w[i].v, &w[0].v, sizeof(float)) != 0) all_same = false;
        if (!vt_contains(w[i].v)) all_real = false;
    }
    printf("    vendor reads during the wave: %llu   (want 1)\n",
           (unsigned long long)(c1 - c0));
    printf("    value seen by all 8: %f   leaders=%llu coalesced=%llu\n",
           (double)w[0].v,
           (unsigned long long)(s1.leaders - s0.leaders),
           (unsigned long long)(s1.coalesced - s0.coalesced));
    ok("8 simultaneous callers -> exactly 1 vendor read", c1 - c0 == 1);
    ok("all 8 returned TEMP_OK", all_ok);
    ok("all 8 got the same value", all_same);
    ok("that value really came from the probe", all_real);
    ok("stats: exactly 1 leader", s1.leaders - s0.leaders == 1);
    ok("stats: 7 callers coalesced", s1.coalesced - s0.coalesced == WAVE_N - 1);
    ok("stats: vendor_calls matches the device", s1.vendor_calls - s0.vendor_calls == 1);

    /* ============ Phase 2: cache hit ============ */
    printf("\n== Phase 2: fresh cache -> no bus traffic ==\n");
    c0 = tempdev_calls();
    temp_cache_stats(&s0);
    float hitv = NAN;
    rc = temp_get(&hitv, 3600ull * 1000000ull);
    temp_cache_stats(&s1);
    ok("cache hit -> TEMP_OK", rc == TEMP_OK);
    ok("cache hit -> zero vendor reads", tempdev_calls() == c0);
    ok("cache hit returns the cached value",
       memcmp(&hitv, &w[0].v, sizeof(float)) == 0);
    ok("stats: counted as a hit", s1.hits - s0.hits == 1);

    /* ============ Phase 3: the refresh fails, 8 at a time ============ */
    printf("\n== Phase 3: 8 threads, wedged bus -> ONE read, stale value ==\n");
    tempdev_force_nack(true);
    temp_cache_stats(&s0);
    c0 = tempdev_calls();
    wave_arg_t wn[WAVE_N];
    run_wave(wn);
    c1 = tempdev_calls();
    temp_cache_stats(&s1);
    tempdev_force_nack(false);

    bool all_stale = true, all_last = true;
    for (int i = 0; i < WAVE_N; i++) {
        if (wn[i].rc != TEMP_STALE) all_stale = false;
        if (memcmp(&wn[i].v, &w[0].v, sizeof(float)) != 0) all_last = false;
    }
    printf("    vendor reads during the failed wave: %llu   (want 1)\n",
           (unsigned long long)(c1 - c0));
    ok("failed refresh does not fan out into 8 reads", c1 - c0 == 1);
    ok("all 8 returned TEMP_STALE", all_stale);
    ok("all 8 got the previous good value", all_last);
    ok("stats: 1 NACK recorded", s1.nacks - s0.nacks == 1);
    ok("waiters did not hang", true);   /* reaching here at all proves it */

    /* ============ Phase 4: recovery ============ */
    printf("\n== Phase 4: bus recovers, next wave gets a new value ==\n");
    c0 = tempdev_calls();
    wave_arg_t wr[WAVE_N];
    run_wave(wr);
    c1 = tempdev_calls();
    bool fresh_all_ok = true, fresh_same = true;
    for (int i = 0; i < WAVE_N; i++) {
        if (wr[i].rc != TEMP_OK) fresh_all_ok = false;
        if (memcmp(&wr[i].v, &wr[0].v, sizeof(float)) != 0) fresh_same = false;
    }
    ok("recovered wave -> exactly 1 vendor read", c1 - c0 == 1);
    ok("recovered wave -> TEMP_OK everywhere", fresh_all_ok);
    ok("recovered wave -> one shared value", fresh_same);
    ok("the new value differs from the stale one",
       memcmp(&wr[0].v, &w[0].v, sizeof(float)) != 0);
    tempdev_set_read_us(TEMPDEV_READ_US_DEFAULT);

    /* ============ Phase 5: cache hits are cheap ============ */
    printf("\n== Phase 5: hit latency vs a vendor read ==\n");
    c0 = tempdev_calls();
    uint64_t worst = 0, total = 0;
    const int NHIT = 2000;
    for (int i = 0; i < NHIT; i++) {
        float x = NAN;
        uint64_t t0 = tempdev_now_us();
        (void)temp_get(&x, 3600ull * 1000000ull);
        uint64_t dt = tempdev_now_us() - t0;
        total += dt;
        if (dt > worst) worst = dt;
    }
    printf("    %d hits: worst %llu us, mean %.2f us, vendor read %u us\n",
           NHIT, (unsigned long long)worst, (double)total / NHIT, tempdev_read_us());
    ok("hit loop caused no vendor reads", tempdev_calls() == c0);
    ok("worst hit is far cheaper than a vendor read",
       worst < tempdev_read_us() / 4u);

    /* ============ Phase 6: Part 2, range stats ============ */
    printf("\n== Phase 6: range stats over an hour of history ==\n");
    temp_cache_deinit();
    if (temp_cache_init() != 0) {
        printf("re-init failed\n");
        return 1;
    }
    ok("after re-init the history is empty",
       temp_range_stats(0, tempdev_now_us(), NULL, NULL, NULL) == -1);

    uint64_t now0 = tempdev_now_us();
    uint32_t hrng = 0x9E3779B9u;
    inj_n = 0;
    /* 5 samples 70 minutes old: outside the 1-hour window on purpose. */
    for (int i = 0; i < INJ_OLD; i++) {
        inj[inj_n].ts = now0 - 4200ull * 1000000ull + (uint64_t)i * 1000000ull;
        inj[inj_n].c  = 10.0f + 5.0f * xs_u01(&hrng);
        inj_n++;
    }
    /* 4000 samples spread over the last 50 minutes, jittered but sorted. */
    uint64_t base = now0 - 3000ull * 1000000ull;
    for (int i = 0; i < INJ_N; i++) {
        inj[inj_n].ts = base + (uint64_t)i * 750000ull
                        + (uint64_t)(xs32(&hrng) % 500000u);
        inj[inj_n].c  = 15.0f + 20.0f * xs_u01(&hrng);
        inj_n++;
    }
    bool add_ok = true;
    for (int i = 0; i < inj_n; i++)
        if (temp_history_add(inj[i].ts, inj[i].c) != 0) add_ok = false;
    ok("temp_history_add accepted every sample", add_ok);

    uint64_t newest_m = inj[inj_n - 1].ts / TEMP_BUCKET_US;
    uint64_t oldest_m = newest_m - (TEMP_NBUCKETS - 1u);

    /* whole live history, both ends deliberately mid-bucket */
    {
        uint64_t t0 = oldest_m * TEMP_BUCKET_US + 17ull * 1000000ull;
        uint64_t t1 = inj[inj_n - 1].ts;
        float mn = NAN, mx = NAN, av = NAN, emn = 0, emx = 0;
        double eav = 0;
        uint64_t en = bf_stats(t0, t1, &emn, &emx, &eav);
        int r = temp_range_stats(t0, t1, &mn, &mx, &av);
        temp_cache_stats(&s1);
        printf("    full window: n=%llu min=%f max=%f avg=%f "
               "(brute force %f/%f/%f), raw samples scanned=%llu\n",
               (unsigned long long)en, (double)mn, (double)mx, (double)av,
               (double)emn, (double)emx, eav,
               (unsigned long long)s1.last_scanned);
        ok("full window: returns 0", r == 0 && en > 0);
        ok("full window: min matches brute force", mn == emn);
        ok("full window: max matches brute force", mx == emx);
        ok("full window: avg matches brute force", close_enough((double)av, eav));
        ok("full window: bounded work, not a full scan",
           s1.last_scanned < (uint64_t)inj_n / 4);
    }

    /* samples older than the window must be gone */
    {
        uint64_t t0 = now0 - 4300ull * 1000000ull;
        uint64_t t1 = now0 - 4100ull * 1000000ull;
        ok("samples older than the window are evicted",
           temp_range_stats(t0, t1, NULL, NULL, NULL) == -1);
    }

    /* random ranges vs brute force */
    {
        uint32_t qrng = 0xB5297A4Du;
        int mism_rc = 0, mism_min = 0, mism_max = 0, mism_avg = 0, nonempty = 0;
        uint64_t span_lo = now0 - 4400ull * 1000000ull;
        uint64_t span    = 4700ull * 1000000ull;   /* reaches into the future */
        for (int q = 0; q < 500; q++) {
            uint64_t a = span_lo + (uint64_t)(xs32(&qrng) % (uint32_t)(span / 1000ull)) * 1000ull;
            uint64_t b = span_lo + (uint64_t)(xs32(&qrng) % (uint32_t)(span / 1000ull)) * 1000ull;
            uint64_t t0 = a < b ? a : b;
            uint64_t t1 = a < b ? b : a;
            float emn = 0, emx = 0, mn = NAN, mx = NAN, av = NAN;
            double eav = 0;
            uint64_t en = bf_stats(t0, t1, &emn, &emx, &eav);
            int r = temp_range_stats(t0, t1, &mn, &mx, &av);
            if (en == 0) {
                if (r != -1) mism_rc++;
                continue;
            }
            nonempty++;
            if (r != 0) { mism_rc++; continue; }
            if (mn != emn) mism_min++;
            if (mx != emx) mism_max++;
            if (!close_enough((double)av, eav)) mism_avg++;
        }
        printf("    500 random ranges (%d non-empty): rc %d, min %d, max %d, avg %d mismatches\n",
               nonempty, mism_rc, mism_min, mism_max, mism_avg);
        ok("500 random ranges match brute force",
           nonempty > 100 && mism_rc == 0 && mism_min == 0 && mism_max == 0 &&
           mism_avg == 0);
    }

    /* boundaries */
    printf("\n== Phase 6b: range boundaries ==\n");
    ok("t0 > t1 -> -1",
       temp_range_stats(now0, now0 - 1000000ull, NULL, NULL, NULL) == -1);
    ok("entirely in the future -> -1",
       temp_range_stats(now0 + 10000000ull, now0 + 20000000ull, NULL, NULL, NULL) == -1);
    ok("inside the window but before the first sample -> -1",
       temp_range_stats(now0 - 3300ull * 1000000ull,
                        now0 - 3100ull * 1000000ull, NULL, NULL, NULL) == -1);
    {
        /* a single sample */
        uint64_t ts = inj[inj_n / 2].ts;
        float c = inj[inj_n / 2].c;
        float mn = NAN, mx = NAN, av = NAN;
        int r = temp_range_stats(ts, ts, &mn, &mx, &av);
        ok("t0 == t1 on an existing sample", r == 0 && mn == c && mx == c);
    }
    {
        /* a range that only partly covers the NEWEST bucket */
        uint64_t bs = newest_m * TEMP_BUCKET_US;
        uint64_t t0 = bs + 7ull * 1000000ull;
        uint64_t t1 = inj[inj_n - 1].ts;
        float emn = 0, emx = 0, mn = NAN, mx = NAN, av = NAN;
        double eav = 0;
        uint64_t en = bf_stats(t0, t1, &emn, &emx, &eav);
        int r = temp_range_stats(t0, t1, &mn, &mx, &av);
        printf("    partial newest bucket: n=%llu -> %s\n",
               (unsigned long long)en, r == 0 ? "0" : "-1");
        ok("partial newest bucket is exact",
           (en == 0) ? (r == -1)
                     : (r == 0 && mn == emn && mx == emx &&
                        close_enough((double)av, eav)));
    }
    {
        /* one whole bucket exactly */
        uint64_t m = newest_m - 10;
        uint64_t t0 = m * TEMP_BUCKET_US;
        uint64_t t1 = t0 + TEMP_BUCKET_US - 1;
        float emn = 0, emx = 0, mn = NAN, mx = NAN, av = NAN;
        double eav = 0;
        uint64_t en = bf_stats(t0, t1, &emn, &emx, &eav);
        int r = temp_range_stats(t0, t1, &mn, &mx, &av);
        ok("one whole bucket is exact",
           en > 0 && r == 0 && mn == emn && mx == emx &&
           close_enough((double)av, eav));
    }
    {
        /* a range narrower than one bucket, in the middle of it */
        uint64_t m = newest_m - 20;
        uint64_t t0 = m * TEMP_BUCKET_US + 20ull * 1000000ull;
        uint64_t t1 = t0 + 10ull * 1000000ull;
        float emn = 0, emx = 0, mn = NAN, mx = NAN, av = NAN;
        double eav = 0;
        uint64_t en = bf_stats(t0, t1, &emn, &emx, &eav);
        int r = temp_range_stats(t0, t1, &mn, &mx, &av);
        ok("sub-bucket range is exact",
           en > 0 && r == 0 && mn == emn && mx == emx &&
           close_enough((double)av, eav));
    }

    /* ============ Phase 7: stress ============ */
    printf("\n== Phase 7: 4 threads hammer both APIs for 1.2 s ==\n");
    stress_r0 = now0 - 2000ull * 1000000ull;
    stress_r1 = now0 - 1000ull * 1000000ull;
    {
        double a = 0;
        uint64_t n = bf_stats(stress_r0, stress_r1, &stress_min, &stress_max, &a);
        stress_avg = a;
        ok("stress range has ground truth", n > 0);
    }
    tempdev_set_nack_every(7);        /* keep the error path warm, on purpose */
    atomic_store(&stress_stop, false);
    pthread_t sth[4];
    stress_stat_t sst[4];
    memset(sst, 0, sizeof sst);
    temp_cache_stats(&s0);
    c0 = tempdev_calls();
    for (int i = 0; i < 4; i++) {
        sst[i].id = i;
        pthread_create(&sth[i], NULL, stress_thread, &sst[i]);
    }
    usleep(1200 * 1000);
    atomic_store(&stress_stop, true);
    uint64_t gets = 0, ranges = 0, hits = 0, stales = 0, nodata = 0;
    uint64_t badv = 0, badr = 0, mg = 0, mr = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(sth[i], NULL);
        gets += sst[i].gets;
        ranges += sst[i].ranges;
        hits += sst[i].hits;
        stales += sst[i].stales;
        nodata += sst[i].nodata;
        badv += sst[i].bad_value;
        badr += sst[i].bad_range;
        if (sst[i].max_get_us > mg) mg = sst[i].max_get_us;
        if (sst[i].max_range_us > mr) mr = sst[i].max_range_us;
    }
    c1 = tempdev_calls();
    temp_cache_stats(&s1);
    tempdev_set_nack_every(0);

    printf("    temp_get %llu (ok %llu, stale %llu, nodata %llu), "
           "vendor reads %llu\n",
           (unsigned long long)gets, (unsigned long long)hits,
           (unsigned long long)stales, (unsigned long long)nodata,
           (unsigned long long)(c1 - c0));
    printf("    temp_range_stats %llu, worst get %llu us, worst range %llu us\n",
           (unsigned long long)ranges, (unsigned long long)mg,
           (unsigned long long)mr);
    ok("stress: the threads actually ran", gets > 50 && ranges > 50);
    ok("stress: no value the probe never produced", badv == 0);
    ok("stress: every range query matched ground truth", badr == 0);
    ok("stress: demand was coalesced (reads < calls)", c1 - c0 < gets);
    ok("stress: worst temp_get <= one vendor read + slack",
       mg < (uint64_t)tempdev_read_us() * 4u);
    ok("stress: range query stays well under a vendor read",
       mr < (uint64_t)tempdev_read_us() / 4u);
    ok("stress: no partial/overwritten history", s1.raw_lost == 0);

    /* ============ Phase 8: the bus contract, and shutdown ============ */
    printf("\n== Phase 8: bus contract + deinit while a read is in flight ==\n");
    printf("    max threads ever inside tempdev_read(): %u\n",
           tempdev_max_concurrency());
    ok("tempdev_read() was never entered concurrently",
       !tempdev_concurrent_entry_seen() && tempdev_max_concurrency() <= 1);

    pthread_t lt;
    late_rc = -999;
    pthread_create(&lt, NULL, late_thread, NULL);
    usleep(10 * 1000);                /* let it get inside the vendor read */
    temp_cache_deinit();              /* must wait for the bus, not abandon it */
    pthread_join(lt, NULL);
    printf("    the in-flight caller returned %s\n", rcname(late_rc));
    ok("deinit waited for the in-flight read",
       late_rc == TEMP_OK || late_rc == TEMP_STALE || late_rc == TEMP_NODATA);
    ok("no concurrent bus entry after shutdown",
       !tempdev_concurrent_entry_seen());
    temp_cache_deinit();              /* idempotent */
    ok("deinit is idempotent", true);
    v = NAN;
    ok("temp_get after deinit -> TEMP_NODATA", temp_get(&v, 0) == TEMP_NODATA);

    if (fails)
        printf("\n%d CHECK(S) FAILED\n", fails);
    else
        printf("\nALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}

/* ================================================================== */
/* the fake vendor blob                                                */
/* ================================================================== */

static _Atomic unsigned v_inside;
static _Atomic unsigned v_maxconc;
static _Atomic bool     v_conc_seen;
static _Atomic bool     v_force_nack;
static _Atomic unsigned v_nack_every;
static _Atomic unsigned v_read_us = TEMPDEV_READ_US_DEFAULT;
static _Atomic uint64_t v_calls;

/* Only one thread may be in tempdev_read() at a time, so this needs no
 * lock in principle — it gets one anyway so that a broken implementation
 * trips the detector below instead of tripping the sanitizer here. */
static pthread_mutex_t v_rng_lk = PTHREAD_MUTEX_INITIALIZER;
static uint32_t v_rng = 0xC0FFEEu;

uint64_t tempdev_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

unsigned tempdev_read_us(void)     { return atomic_load(&v_read_us); }
uint64_t tempdev_calls(void)       { return atomic_load(&v_calls); }
bool tempdev_concurrent_entry_seen(void) { return atomic_load(&v_conc_seen); }
unsigned tempdev_max_concurrency(void)   { return atomic_load(&v_maxconc); }
void tempdev_force_nack(bool on)   { atomic_store(&v_force_nack, on); }
void tempdev_set_nack_every(unsigned n)  { atomic_store(&v_nack_every, n); }
void tempdev_set_read_us(unsigned us)    { atomic_store(&v_read_us, us); }

int tempdev_read(float *celsius)
{
    unsigned now_inside = atomic_fetch_add(&v_inside, 1u) + 1u;
    if (now_inside > atomic_load(&v_maxconc))
        atomic_store(&v_maxconc, now_inside);
    if (now_inside > 1u)
        atomic_store(&v_conc_seen, true);     /* THE bug this exercise is about */

    uint64_t seq = atomic_fetch_add(&v_calls, 1u) + 1u;

    usleep(atomic_load(&v_read_us));          /* the slow I2C transaction */

    bool nack = atomic_load(&v_force_nack);
    unsigned every = atomic_load(&v_nack_every);
    if (!nack && every != 0u && (seq % every) == 0u)
        nack = true;

    int rc;
    if (nack) {
        rc = -1;                              /* *celsius untouched */
    } else {
        pthread_mutex_lock(&v_rng_lk);
        float c = 15.0f + 20.0f * xs_u01(&v_rng);
        pthread_mutex_unlock(&v_rng_lk);
        vt_record(c);                         /* harness ground truth */
        *celsius = c;
        rc = 0;
    }

    atomic_fetch_sub(&v_inside, 1u);
    return rc;
}
