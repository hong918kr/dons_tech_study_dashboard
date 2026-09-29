/* main.c — test harness + fake GPS module.  GIVEN; do not modify.
 *
 *   ./main.sh            gps_cache.c          (your code)
 *   ./main.sh sol        gps_cache_solution.c (reference)
 *   ./main.sh window ... 1.5 s window, adds eviction / window-edge checks
 *
 * How it stays honest
 *   - the fake module records every GPS_VALID fix it emits ("ground truth"),
 *     together with a running prefix sum of segment distances.  Every PASS/FAIL
 *     compares against that, never against a hard-coded number, so the same
 *     source passes on macOS and Linux.
 *   - randomness is a fixed-seed xorshift32, not rand(): rand() gives a
 *     different sequence per libc.
 *   - the fake module can be QUIESCED (fake_gps_quiesce): it then reports
 *     GPS_NO_FIX and emits no new fixes.  That lets the harness freeze the
 *     ground truth and compare exact field-for-field values without racing
 *     the sampler thread.  The code under test cannot tell the difference --
 *     a blocked sky looks exactly like this.
 *   - the module's 1 s receiver timeout is scaled down to 100 ms so the whole
 *     run fits in ~7 s.
 *
 * Detecting torn reads
 *   The fake module only ever emits fixes that satisfy
 *       lon  == -(lat + 85.0)
 *       hdop == (float)(1.0 + 100.0 * (lat - 37.0))
 *   so ANY mix of fields from two different fixes is detectable by arithmetic
 *   alone.  It also checks (timestamp, lat) against the recorded set, which
 *   catches a torn 64-bit timestamp.
 */
/* glibc hides usleep()/gettimeofday() when -std=c11 defines __STRICT_ANSI__. */
#define _DEFAULT_SOURCE 1

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "gps.h"
#include "gps_cache.h"

#define DEG2RAD    0.017453292519943295
#define EARTH_R_M  6371008.8

/* fake module tuning */
#define VENDOR_TIMEOUT_US   100000u   /* full receiver timeout -> GPS_NO_FIX   */
#define VENDOR_MIN_WAIT_US   20000u   /* fastest a valid fix can arrive        */
#define VENDOR_SPAN_WAIT_US  70000u   /* ...plus up to this much               */
#define VENDOR_P_NO_FIX        0.25   /* sky blocked                           */
#define VENDOR_P_ERROR         0.02   /* raised from ~0 so the path is tested  */
#define QUIESCE_SETTLE_US   250000u   /* > one vendor block: truth is frozen   */

/* the geometry invariant every emitted fix obeys */
#define LON_OFFSET 85.0
#define LAT_BASE   37.0

static double expect_lon(double lat)  { return -(lat + LON_OFFSET); }
static float  expect_hdop(double lat) { return (float)(1.0 + 100.0 * (lat - LAT_BASE)); }

static double segment_m(const struct GpsFix *a, const struct GpsFix *b)
{
    double dlat = (b->lat - a->lat) * DEG2RAD;
    double dlon = (b->lon - a->lon) * DEG2RAD;
    double latm = (a->lat + b->lat) * 0.5 * DEG2RAD;
    double x    = dlon * cos(latm);
    return EARTH_R_M * sqrt(dlat * dlat + x * x);
}

/* ------------------------------------------------------------------ */
/* ground truth (harness only)                                         */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 8192
static struct { struct GpsFix fix; double cum; } truth[TRUTH_MAX];
static int truth_n;
static pthread_mutex_t truth_lock = PTHREAD_MUTEX_INITIALIZER;

static void truth_record(const struct GpsFix *f)
{
    pthread_mutex_lock(&truth_lock);
    if (truth_n < TRUTH_MAX) {
        truth[truth_n].fix = *f;
        truth[truth_n].cum = (truth_n == 0)
            ? 0.0
            : truth[truth_n - 1].cum + segment_m(&truth[truth_n - 1].fix, f);
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

/* copy recorded fix #i; false if out of range */
static bool truth_get(int i, struct GpsFix *out)
{
    bool ok = false;
    pthread_mutex_lock(&truth_lock);
    if (i >= 0 && i < truth_n) { *out = truth[i].fix; ok = true; }
    pthread_mutex_unlock(&truth_lock);
    return ok;
}

static bool truth_latest(struct GpsFix *out)
{
    return truth_get(truth_count() - 1, out);
}

/* index of the last recorded fix with timestamp <= t, or -1 */
static int truth_index_at(uint64_t t)
{
    int idx = -1;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && truth[i].fix.timestamp <= t; i++)
        idx = i;
    pthread_mutex_unlock(&truth_lock);
    return idx;
}

/* fix in effect at t, per the spec's definition */
static bool truth_fix_at(uint64_t t, struct GpsFix *out)
{
    int i = truth_index_at(t);
    return (i >= 0) && truth_get(i, out);
}

/* metres over [t0, t1], same definition the API must implement */
static double truth_distance(uint64_t t0, uint64_t t1)
{
    double d = 0.0;
    pthread_mutex_lock(&truth_lock);
    int k0 = -1, k1 = -1;
    for (int i = 0; i < truth_n; i++) {
        if (k0 < 0 && truth[i].fix.timestamp >= t0) k0 = i;
        if (truth[i].fix.timestamp <= t1) k1 = i;
    }
    if (k0 >= 0 && k1 > k0)
        d = truth[k1].cum - truth[k0].cum;
    pthread_mutex_unlock(&truth_lock);
    return d;
}

static double truth_total(void)
{
    pthread_mutex_lock(&truth_lock);
    double d = truth_n ? truth[truth_n - 1].cum : 0.0;
    pthread_mutex_unlock(&truth_lock);
    return d;
}

/* did the module ever emit exactly this (ts, lat, lon, hdop)? */
static bool truth_contains(const struct GpsFix *f)
{
    bool found = false;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && !found; i++) {
        const struct GpsFix *t = &truth[i].fix;
        found = (t->timestamp == f->timestamp && t->lat == f->lat &&
                 t->lon == f->lon && t->hdop == f->hdop);
    }
    pthread_mutex_unlock(&truth_lock);
    return found;
}

/* algebraic self-consistency: catches a torn struct with no table lookup */
static bool fix_coherent(const struct GpsFix *f)
{
    return f->status == GPS_VALID &&
           f->timestamp != 0 &&
           f->lon  == expect_lon(f->lat) &&
           f->hdop == expect_hdop(f->lat);
}

/* ------------------------------------------------------------------ */
/* check helpers                                                       */
/* ------------------------------------------------------------------ */

static int fails;

static void ok(const char *label, bool cond, const char *detail)
{
    if (!cond) fails++;
    printf("  %-36s %s%s%s\n", label, cond ? "PASS" : "FAIL",
           detail && !cond ? "  <- " : "", detail && !cond ? detail : "");
}

/* distances are floating point sums; the reference adds them in a different
 * order than the harness, so compare with a tolerance instead of == */
static void check_dist(const char *label, double got, double want)
{
    bool good = isnan(want) ? isnan(got)
                            : (!isnan(got) &&
                               fabs(got - want) <= 1e-6 * (1.0 + fabs(want)));
    if (!good) fails++;
    printf("  %-36s got %12.4f  want %12.4f  %s\n",
           label, got, want, good ? "PASS" : "FAIL");
}

static bool fix_eq(const struct GpsFix *a, const struct GpsFix *b)
{
    return a->status == b->status && a->lat == b->lat && a->lon == b->lon &&
           a->hdop == b->hdop && a->timestamp == b->timestamp;
}

/* want_rc == 0: *want must come back exactly.  want_rc == -1: must fail. */
static void check_fix(const char *label, int rc, const struct GpsFix *got,
                      int want_rc, const struct GpsFix *want)
{
    bool good;
    const char *why = "";
    if (want_rc != 0) {
        good = (rc != 0);
        why = "expected no fix, got one";
    } else if (rc != 0) {
        good = false;
        why = "expected a fix, got none";
    } else if (!fix_coherent(got)) {
        good = false;
        why = "TORN / incoherent struct";
    } else if (!fix_eq(got, want)) {
        good = false;
        why = "wrong fix";
    } else {
        good = true;
    }
    if (!good) fails++;
    printf("  %-36s %s", label, good ? "PASS" : "FAIL");
    if (!good) {
        printf("  <- %s", why);
        if (rc == 0)
            printf("  got ts=%llu lat=%.7f lon=%.7f hdop=%.4f",
                   (unsigned long long)got->timestamp, got->lat, got->lon,
                   (double)got->hdop);
        if (want_rc == 0)
            printf("  want ts=%llu lat=%.7f",
                   (unsigned long long)want->timestamp, want->lat);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* reader stress                                                       */
/* ------------------------------------------------------------------ */

#define N_READERS 4

static _Atomic bool stress_stop;
static uint64_t     stress_t0;

#define SLOW_CALL_US 5000u    /* a getter batch this slow is a scheduling hiccup */

typedef struct {
    uint64_t calls, good, torn, unknown, bad_dist, slow, max_us;
} stat_t;

static void *stress_reader(void *arg)
{
    stat_t *s = arg;
    uint64_t base = stress_t0;
    uint64_t spin = 0;

    while (!atomic_load(&stress_stop)) {
        struct GpsFix a, b;
        uint64_t t_now = gps_now_us();
        uint64_t t_mid = base + (t_now - base) / 2;

        uint64_t m0 = gps_now_us();
        int ra = gps_get_last_fix(&a);
        int rb = gps_fix_at(t_mid, &b);
        double d = gps_distance_travelled(base, t_now - 200000ull);
        uint64_t dt = gps_now_us() - m0;
        if (dt > s->max_us) s->max_us = dt;
        if (dt > SLOW_CALL_US) s->slow++;

        if (ra == 0) {
            if (!fix_coherent(&a))      s->torn++;
            else if (!truth_contains(&a)) s->unknown++;
            else s->good++;
        }
        if (rb == 0) {
            if (!fix_coherent(&b))      s->torn++;
            else if (!truth_contains(&b)) s->unknown++;
            else s->good++;
        }
        if (!isnan(d) && (d < 0.0 || d > truth_total() + 1e-3))
            s->bad_dist++;

        s->calls++;
        /* keep the readers from starving the sampler on a small box */
        if ((++spin & 0x3f) == 0) usleep(200);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* fake GPS module                                                     */
/* ------------------------------------------------------------------ */

static uint32_t rng_state = 0x02F6E2B1u;      /* fixed seed: same run everywhere */

static uint32_t xs32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static double rnd01(void) { return (double)(xs32() >> 8) / 16777216.0; }

static _Atomic bool vendor_paused;

static void fake_gps_quiesce(void)
{
    atomic_store(&vendor_paused, true);
    usleep(QUIESCE_SETTLE_US);   /* outlast the in-flight block + publish */
}

static void fake_gps_resume(void) { atomic_store(&vendor_paused, false); }

/* what GPS_ERROR / GPS_NO_FIX leave in the payload: plausible-looking garbage,
 * so a wrapper that ignores ::status gets caught by fix_coherent(). */
static void fill_garbage(struct GpsFix *f, int status)
{
    f->status    = status;
    f->lat       = -999.0;
    f->lon       =  999.0;
    f->hdop      =  99.0f;
    f->timestamp = 0;
}

uint64_t gps_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

struct GpsFix gps_wait_for_fix(void)
{
    static bool   first_call = true;
    static double lat = LAT_BASE;
    struct GpsFix f;

    if (!first_call) {
        if (rnd01() < VENDOR_P_ERROR) {       /* immediate, no block */
            fill_garbage(&f, GPS_ERROR);
            return f;
        }
        if (rnd01() < VENDOR_P_NO_FIX) {      /* sky blocked: full timeout */
            usleep(VENDOR_TIMEOUT_US);
            fill_garbage(&f, GPS_NO_FIX);
            return f;
        }
        usleep(VENDOR_MIN_WAIT_US + (uint32_t)(rnd01() * VENDOR_SPAN_WAIT_US));
        if (atomic_load(&vendor_paused)) {    /* harness froze the world */
            fill_garbage(&f, GPS_NO_FIX);
            return f;
        }
        /* dead-reckon the trailer a few metres, clamped so hdop stays sane */
        lat += (rnd01() * 2.0 - 1.0) * 1e-4;
        if (lat >  LAT_BASE + 0.005) lat = LAT_BASE + 0.005;
        if (lat <  LAT_BASE - 0.005) lat = LAT_BASE - 0.005;
    } else {
        first_call = false;                   /* cold-start fix, no block */
    }

    f.status    = GPS_VALID;
    f.lat       = lat;
    f.lon       = expect_lon(lat);
    f.hdop      = expect_hdop(lat);
    f.timestamp = gps_now_us();
    truth_record(&f);                         /* harness only */
    return f;
}

/* ------------------------------------------------------------------ */
/* test                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct GpsFix got, want;
    int rc;

    printf("window = %llu us, vendor timeout = %u us\n\n",
           (unsigned long long)GPS_WINDOW_US, VENDOR_TIMEOUT_US);

    if (gps_cache_init() != 0) {
        printf("gps_cache_init() failed\n");
        return 1;
    }
    uint64_t start = gps_now_us();

    /* -------------------------------------------------------------- */
    printf("== Part 1: gps_get_last_fix ==\n");

    /* The very first vendor call never blocks, so a correct init() has taken
     * exactly one fix by the time it returns and nothing else can have
     * arrived yet (the next one needs >= 20 ms). */
    ok("init() sampled the module itself", truth_count() >= 1,
       "init() returned without taking a fix");

    rc = gps_get_last_fix(&got);
    if (truth_latest(&want))
        check_fix("fix available right after init", rc, &got, 0, &want);
    else
        ok("fix available right after init", false, "module never read");

    usleep(900 * 1000);
    fake_gps_quiesce();
    rc = gps_get_last_fix(&got);
    if (truth_latest(&want))
        check_fix("latest fix after ~1 s of sampling", rc, &got, 0, &want);
    else
        ok("latest fix after ~1 s of sampling", false, "no fixes recorded");
    printf("    (%d fixes emitted so far)\n", truth_count());
    fake_gps_resume();

    /* -------------------------------------------------------------- */
    usleep(1600 * 1000);                    /* build up some history   */
    fake_gps_quiesce();                     /* freeze the ground truth */

    int n = truth_count();
    uint64_t now = gps_now_us();
    printf("\n== Part 2: gps_fix_at / gps_distance_travelled  (%d fixes) ==\n", n);

    if (n < 6) {
        fails++;
        printf("  FAIL: only %d fixes -- the sampler thread is not running\n", n);
    }

    /* Pick three reference fixes that are guaranteed to still be inside the
     * window (matters only for the short-window build).  i0 == 0 means the
     * whole recorded history is queryable, which unlocks the "before the very
     * first fix" checks. */
    int i0 = 0;
    {
        uint64_t oldest_ok = (now > GPS_WINDOW_US) ? now - GPS_WINDOW_US + 300000ull : 0;
        while (i0 < n) {
            struct GpsFix f;
            if (!truth_get(i0, &f) || f.timestamp >= oldest_ok) break;
            i0++;
        }
    }
    struct GpsFix f0, fk, fl;
    bool have0 = truth_get(i0, &f0);
    bool havek = truth_get(i0 + (n - 1 - i0) / 2, &fk);
    bool havel = truth_get(n - 1, &fl);
    bool full_history = (i0 == 0);

    if (have0 && havek && havel && fk.timestamp > f0.timestamp &&
        fl.timestamp > fk.timestamp) {
        rc = gps_fix_at(f0.timestamp, &got);
        check_fix("t == first fix timestamp", rc, &got, 0, &f0);

        rc = gps_fix_at(fk.timestamp, &got);
        check_fix("t == middle fix timestamp", rc, &got, 0, &fk);

        /* 10 ms after fix k, before fix k+1: still fix k */
        uint64_t between = fk.timestamp + 10000ull;
        if (truth_fix_at(between, &want)) {
            rc = gps_fix_at(between, &got);
            check_fix("t between two fixes -> older one", rc, &got, 0, &want);
        }

        rc = gps_fix_at(fl.timestamp, &got);
        check_fix("t == newest fix timestamp", rc, &got, 0, &fl);

        if (truth_fix_at(now, &want)) {
            rc = gps_fix_at(now, &got);
            check_fix("t == now -> newest fix", rc, &got, 0, &want);
        }

        printf("\n  -- gps_fix_at boundaries --\n");
        rc = gps_fix_at(now + 10 * 1000000ull, &got);
        check_fix("t 10 s in the future", rc, &got, -1, NULL);

        if (full_history) {
            rc = gps_fix_at(f0.timestamp - 1, &got);
            check_fix("t 1 us before the first fix", rc, &got, -1, NULL);
        }

        rc = gps_fix_at(now - GPS_WINDOW_US - 1000000ull, &got);
        check_fix("t older than the window", rc, &got, -1, NULL);

        printf("\n  -- gps_distance_travelled --\n");
        check_dist("whole recorded path",
                   gps_distance_travelled(f0.timestamp, fl.timestamp),
                   truth_distance(f0.timestamp, fl.timestamp));
        check_dist("first half",
                   gps_distance_travelled(f0.timestamp, fk.timestamp),
                   truth_distance(f0.timestamp, fk.timestamp));
        check_dist("second half",
                   gps_distance_travelled(fk.timestamp, fl.timestamp),
                   truth_distance(fk.timestamp, fl.timestamp));
        check_dist("halves sum to the whole",
                   gps_distance_travelled(f0.timestamp, fk.timestamp) +
                   gps_distance_travelled(fk.timestamp, fl.timestamp),
                   truth_distance(f0.timestamp, fl.timestamp));
        if (full_history)
            check_dist("range wider than the data",
                       gps_distance_travelled(f0.timestamp - 500000ull, now),
                       truth_distance(f0.timestamp, fl.timestamp));

        printf("\n  -- gps_distance_travelled boundaries --\n");
        check_dist("t0 == t1 (a real fix time)",
                   gps_distance_travelled(fk.timestamp, fk.timestamp), 0.0);
        check_dist("t0 == t1 == now",
                   gps_distance_travelled(now, now), 0.0);
        check_dist("t0 > t1 (reversed)",
                   gps_distance_travelled(fl.timestamp, f0.timestamp), NAN);
        check_dist("t1 10 s in the future",
                   gps_distance_travelled(f0.timestamp, now + 10 * 1000000ull), NAN);
        check_dist("t0 older than the window",
                   gps_distance_travelled(now - GPS_WINDOW_US - 1000000ull, now), NAN);
        if (full_history)
            check_dist("range entirely before the first fix",
                       gps_distance_travelled(f0.timestamp - 400000ull,
                                              f0.timestamp - 200000ull), 0.0);
        check_dist("range holding exactly one fix",
                   gps_distance_travelled(fk.timestamp, fk.timestamp + 1), 0.0);

#ifdef WINDOW_TEST
        printf("\n  -- eviction (window = %llu us) --\n",
               (unsigned long long)GPS_WINDOW_US);
        {
            uint64_t inside = now - GPS_WINDOW_US / 2;
            if (truth_fix_at(inside, &want)) {
                rc = gps_fix_at(inside, &got);
                check_fix("fix at now - window/2 survived", rc, &got, 0, &want);
            }
            uint64_t edge = now - GPS_WINDOW_US + 20000ull;
            if (truth_fix_at(edge, &want)) {
                rc = gps_fix_at(edge, &got);
                check_fix("fix at the window edge survived", rc, &got, 0, &want);
                int ei = truth_index_at(edge);
                struct GpsFix ef;
                if (truth_get(ei, &ef))
                    printf("    (the fix in effect at the edge is %s the window%s)\n",
                           ef.timestamp + GPS_WINDOW_US < now ? "OLDER than" : "inside",
                           ef.timestamp + GPS_WINDOW_US < now
                               ? " -- keep-one-older was exercised" : "");
            }
            rc = gps_fix_at(now - GPS_WINDOW_US - 300000ull, &got);
            check_fix("fix just outside the window", rc, &got, -1, NULL);

            uint64_t w0 = now - GPS_WINDOW_US / 2, w1 = now - 200000ull;
            check_dist("distance inside the window",
                       gps_distance_travelled(w0, w1), truth_distance(w0, w1));
        }
#endif
    } else {
        fails++;
        printf("  FAIL: not enough distinct fixes in the window to test Part 2\n");
    }

    fake_gps_resume();

    /* -------------------------------------------------------------- */
    printf("\n== Thread safety: %d readers for 2 s while the sampler writes ==\n",
           N_READERS);
    stress_t0 = start;
    atomic_store(&stress_stop, false);
    pthread_t th[N_READERS];
    stat_t st[N_READERS];
    memset(st, 0, sizeof st);
    for (int i = 0; i < N_READERS; i++)
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    usleep(2000 * 1000);
    atomic_store(&stress_stop, true);

    stat_t tot;
    memset(&tot, 0, sizeof tot);
    for (int i = 0; i < N_READERS; i++) {
        pthread_join(th[i], NULL);
        tot.calls    += st[i].calls;
        tot.good     += st[i].good;
        tot.torn     += st[i].torn;
        tot.unknown  += st[i].unknown;
        tot.bad_dist += st[i].bad_dist;
        tot.slow     += st[i].slow;
        if (st[i].max_us > tot.max_us) tot.max_us = st[i].max_us;
    }
    printf("  %llu iterations, %llu coherent snapshots\n",
           (unsigned long long)tot.calls, (unsigned long long)tot.good);
    ok("no torn struct ever observed", tot.torn == 0, "a reader saw mixed fields");
    ok("no fix the module never emitted", tot.unknown == 0, "invented (ts,lat)");
    ok("no impossible distance", tot.bad_dist == 0, "negative or > total path");
    ok("readers actually got data", tot.calls > 0 && tot.good > 0,
       "every call returned -1 -- nothing was tested");

    /* Getters must not wait on the module.  An implementation that does would
     * be slow on essentially every call, so judge on the rate of slow batches
     * (a loaded laptop is allowed the odd 5 ms scheduling hiccup) and cap the
     * absolute worst case at one vendor block.  A blocking getter also cannot
     * reach anywhere near this iteration count in 2 s. */
    printf("  3 getter calls in a row: worst %llu us, %llu batches over %u us"
           " (vendor block %u us)\n",
           (unsigned long long)tot.max_us, (unsigned long long)tot.slow,
           SLOW_CALL_US, VENDOR_TIMEOUT_US);
    ok("getters never block on the module",
       tot.calls > 10000 && tot.max_us < VENDOR_TIMEOUT_US &&
       tot.slow * 1000ull <= tot.calls,
       "a getter waited like the vendor call does");

    /* -------------------------------------------------------------- */
    printf("\n== Shutdown ==\n");
    gps_cache_deinit();
    int after = truth_count();
    usleep(400 * 1000);
    ok("sampler thread stopped after deinit", truth_count() == after,
       "the module was still being read");
    rc = gps_get_last_fix(&got);
    ok("getter still safe after deinit", rc != 0 || fix_coherent(&got),
       "returned a torn fix");

    printf("\n");
    if (fails) printf("%d CHECK(S) FAILED\n", fails);
    else       printf("ALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}
