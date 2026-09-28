/* main.c — test harness + fake LTE modem for 02_modem_rssi_window.  (given)
 *
 * Same idea as the original Verkada screening harness: the fake vendor API
 * records every value it ever produced ("ground truth"), and every PASS/FAIL is
 * computed against that, never against a hard-coded number — the RNG sequence
 * must not have to match between macOS and Linux, so the RNG is a fixed-seed
 * xorshift instead of rand().
 *
 * What is checked, in order:
 *   0. getters before rssi_init()                -> "no data", no crash
 *   1. right after rssi_init()                   -> a value exists, age ~0
 *   2. latest value + age vs ground truth
 *   3. min / max / avg vs a brute-force O(n) reference, twice, the second time
 *      after samples have aged out of the window
 *   4. modem dies -> age grows, rssi_is_healthy() flips false, window drains
 *      to RSSI_NONE / NAN; modem recovers -> health comes back
 *   5. modem_poll_rssi() was never called concurrently (it is not thread-safe)
 *   6. 4 readers for 2 s: no invented values, worst getter latency << 100 ms
 *   7. after rssi_deinit() the modem stops being polled (thread really gone)
 *
 * Exact comparisons are done with the fake modem paused and at a moment when no
 * sample sits near the window edge, so that a sample expiring between the
 * getter and the reference cannot make a correct implementation look wrong.
 *
 * Do not modify: the same file is built against rssi_window.c and
 * rssi_window_solution.c.
 */
/* glibc hides usleep()/gettimeofday() when -std=c11 defines __STRICT_ANSI__.
 * Harmless on macOS. Must come before any #include. */
#define _DEFAULT_SOURCE 1

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "modem.h"
#include "rssi_window.h"

/* ------------------------------------------------------------------ */
/* fixed-seed RNG (no rand(): its sequence differs per platform)       */
/* ------------------------------------------------------------------ */

/* splitmix64, fixed seed: same sequence on macOS and Linux, and unlike a bare
 * xorshift it has no correlation between consecutive draws (this fake draws in
 * a fixed 2-5 call pattern per poll, which a weak generator shows up in).
 * Touched only by the single thread that is allowed to poll the modem. */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;

static uint64_t splitmix64(void)
{
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static double rnd01(void) { return (double)(splitmix64() >> 11) / 9007199254740992.0; }

/* usleep() is only specified for < 1 s; chunk anything longer. */
static void sleep_us(uint64_t us)
{
    while (us > 500000ull) { usleep(500000); us -= 500000ull; }
    if (us > 0) usleep((useconds_t)us);
}

/* ------------------------------------------------------------------ */
/* ground truth + fake modem state                                     */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 8192
static struct { uint64_t ts; int rssi; int sinr; } truth[TRUTH_MAX];
static int truth_n;
static pthread_mutex_t truth_lock = PTHREAD_MUTEX_INITIALIZER;

static _Atomic bool g_modem_dead;        /* "firmware wedged": only BUSY comes back */
static _Atomic int  g_in_call;           /* misuse detector: vendor API is 1-thread  */
static _Atomic int  g_concurrent;
static _Atomic int  g_n_ok, g_n_busy, g_n_err;

static void truth_record(uint64_t ts, int rssi, int sinr)
{
    pthread_mutex_lock(&truth_lock);
    if (truth_n < TRUTH_MAX) {
        truth[truth_n].ts = ts;
        truth[truth_n].rssi = rssi;
        truth[truth_n].sinr = sinr;
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

static bool truth_latest(int *rssi, uint64_t *ts)
{
    pthread_mutex_lock(&truth_lock);
    bool ok = truth_n > 0;
    if (ok) {
        *rssi = truth[truth_n - 1].rssi;
        *ts = truth[truth_n - 1].ts;
    }
    pthread_mutex_unlock(&truth_lock);
    return ok;
}

/* Did the modem ever hand out this rssi value? (catches torn / invented /
 * garbage-payload values, e.g. publishing on MODEM_BUSY or MODEM_ERROR.) */
static bool truth_contains(int rssi)
{
    bool found = false;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && !found; i++)
        found = (truth[i].rssi == rssi);
    pthread_mutex_unlock(&truth_lock);
    return found;
}

/* Brute-force O(n) reference for the window ending at `now`. */
typedef struct { int n; int mn, mx; double avg; } wstat_t;

static wstat_t truth_window(uint64_t now)
{
    wstat_t w = {0, RSSI_NONE, RSSI_NONE, NAN};
    long long sum = 0;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n; i++) {
        uint64_t ts = truth[i].ts;
        if (ts > now || now - ts > RSSI_WINDOW_US)
            continue;
        int v = truth[i].rssi;
        if (w.n == 0) { w.mn = v; w.mx = v; }
        else { if (v < w.mn) w.mn = v; if (v > w.mx) w.mx = v; }
        sum += v;
        w.n++;
    }
    pthread_mutex_unlock(&truth_lock);
    if (w.n > 0)
        w.avg = (double)sum / (double)w.n;
    return w;
}

/* ------------------------------------------------------------------ */
/* PASS/FAIL plumbing                                                  */
/* ------------------------------------------------------------------ */

static int fails;

static void ok_line(const char *label, bool ok, const char *detail)
{
    if (!ok) fails++;
    printf("  %-44s %-34s %s\n", label, detail ? detail : "", ok ? "PASS" : "FAIL");
}

static void check_true(const char *label, bool cond, const char *detail)
{
    ok_line(label, cond, detail);
}

static void check_int(const char *label, int got, int want)
{
    char d[80];
    snprintf(d, sizeof d, "got %d want %d", got, want);
    ok_line(label, got == want, d);
}

static void check_dbl(const char *label, double got, double want, double tol)
{
    char d[96];
    bool ok = isnan(want) ? isnan(got) : (!isnan(got) && fabs(got - want) <= tol);
    snprintf(d, sizeof d, "got %.4f want %.4f", got, want);
    ok_line(label, ok, d);
}

/* ------------------------------------------------------------------ */
/* helpers that make the exact comparisons race-free                   */
/* ------------------------------------------------------------------ */

#define EDGE_GUARD_US 30000ull      /* keep 30 ms away from the window edge */

static void modem_pause(bool paused) { atomic_store(&g_modem_dead, paused); }

/* Caller has paused the modem. Wait out any in-flight poll, then wait for an
 * instant where no recorded sample is within EDGE_GUARD_US of the window edge,
 * so getter and reference must agree. */
static void settle(void)
{
    sleep_us(120 * 1000);                     /* > a typical in-flight vendor poll */
    for (int tries = 0; tries < 200; tries++) {
        uint64_t now = modem_now_us();
        uint64_t edge = (now > RSSI_WINDOW_US) ? now - RSSI_WINDOW_US : 0;
        bool close = false;
        pthread_mutex_lock(&truth_lock);
        for (int i = 0; i < truth_n && !close; i++) {
            uint64_t ts = truth[i].ts;
            uint64_t d = ts > edge ? ts - edge : edge - ts;
            if (d < EDGE_GUARD_US) close = true;
        }
        pthread_mutex_unlock(&truth_lock);
        if (!close) return;
        usleep(5 * 1000);
    }
}

static void exact_checks(const char *tag, bool expect_eviction)
{
    char label[96];
    modem_pause(true);
    settle();

    uint64_t now = modem_now_us();
    wstat_t w = truth_window(now);

    int dbm = 0;
    uint64_t age = 0;
    int rc = rssi_get_latest(&dbm, &age);
    int gmin = rssi_min_in_window();
    int gmax = rssi_max_in_window();
    double gavg = rssi_avg_in_window();
    bool healthy = rssi_is_healthy(1000 * 1000ull);

    int t_rssi = 0;
    uint64_t t_ts = 0;
    bool have = truth_latest(&t_rssi, &t_ts);

    printf("  [%s] modem produced %d samples, %d inside the %llu ms window\n",
           tag, truth_count(), w.n, (unsigned long long)(RSSI_WINDOW_US / 1000));

    snprintf(label, sizeof label, "%s: modem was polled at all", tag);
    check_true(label, have, have ? "" : "modem_poll_rssi() never called");
    snprintf(label, sizeof label, "%s: window is non-trivial (>=3)", tag);
    check_true(label, w.n >= 3, "");

    snprintf(label, sizeof label, "%s: get_latest returns 0", tag);
    check_int(label, rc, have ? 0 : -1);
    if (rc == 0 && have) {
        snprintf(label, sizeof label, "%s: latest dbm", tag);
        check_int(label, dbm, t_rssi);
        uint64_t want_age = now > t_ts ? now - t_ts : 0;
        char d[96];
        snprintf(d, sizeof d, "got %llu want ~%llu us",
                 (unsigned long long)age, (unsigned long long)want_age);
        long long diff = (long long)age - (long long)want_age;
        if (diff < 0) diff = -diff;
        snprintf(label, sizeof label, "%s: age matches (+-5 ms)", tag);
        ok_line(label, diff <= 5000, d);
    }
    snprintf(label, sizeof label, "%s: is_healthy(1 s)", tag);
    check_true(label, healthy == have, healthy ? "healthy" : "unhealthy");

    snprintf(label, sizeof label, "%s: min_in_window", tag);
    check_int(label, gmin, w.mn);
    snprintf(label, sizeof label, "%s: max_in_window", tag);
    check_int(label, gmax, w.mx);
    snprintf(label, sizeof label, "%s: avg_in_window", tag);
    check_dbl(label, gavg, w.avg, 0.001);

    if (expect_eviction) {
        snprintf(label, sizeof label, "%s: eviction happened", tag);
        char d[80];
        snprintf(d, sizeof d, "%d of %d samples aged out", truth_count() - w.n, truth_count());
        check_true(label, truth_count() - w.n > 0, d);
    }
    modem_pause(false);
}

/* ------------------------------------------------------------------ */
/* reader stress                                                       */
/* ------------------------------------------------------------------ */

static _Atomic bool stress_stop;

typedef struct { uint64_t calls, bad, max_us, total_us; } stress_stat_t;

static void *stress_reader(void *arg)
{
    stress_stat_t *st = arg;
    while (!atomic_load(&stress_stop)) {
        int dbm = 0;
        uint64_t age = 0;

        uint64_t t0 = modem_now_us();
        int rc = rssi_get_latest(&dbm, &age);
        bool healthy = rssi_is_healthy(500 * 1000ull);
        int mn = rssi_min_in_window();
        int mx = rssi_max_in_window();
        double av = rssi_avg_in_window();
        uint64_t dt = modem_now_us() - t0;

        if (dt > st->max_us) st->max_us = dt;
        st->total_us += dt;

        if (rc == 0) {
            if (!truth_contains(dbm)) st->bad++;        /* value nobody produced */
            if (age > 30ull * 1000000ull) st->bad++;    /* nonsense age          */
            if (healthy && age > 500 * 1000ull) st->bad++;
        } else if (healthy) {
            st->bad++;                                  /* healthy with no value */
        }
        if (mn != RSSI_NONE && !truth_contains(mn)) st->bad++;
        if (mx != RSSI_NONE && !truth_contains(mx)) st->bad++;
        if (mn != RSSI_NONE && mx != RSSI_NONE && mn > mx) st->bad++;
        if (!isnan(av) && (av < -140.0 || av > -20.0)) st->bad++;
        st->calls++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* test                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    char d[96];

    printf("== Part 0: getters before rssi_init() ==\n");
    {
        int dbm = 12345;
        uint64_t age = 12345;
        check_int("get_latest before init -> -1", rssi_get_latest(&dbm, &age), -1);
        check_true("is_healthy before init -> false", !rssi_is_healthy(1000 * 1000ull), "");
        check_int("min before init -> RSSI_NONE", rssi_min_in_window(), RSSI_NONE);
        check_int("max before init -> RSSI_NONE", rssi_max_in_window(), RSSI_NONE);
        check_dbl("avg before init -> NAN", rssi_avg_in_window(), NAN, 0.0);
    }

    printf("\n== Part 1: rssi_init() takes the first (free) sample ==\n");
    if (rssi_init() != 0) {
        printf("rssi_init() failed\n");
        return 1;
    }
    {
        int dbm = 0;
        uint64_t age = 0;
        int rc = rssi_get_latest(&dbm, &age);
        check_int("get_latest right after init", rc, 0);
        if (rc == 0) {
            snprintf(d, sizeof d, "dbm %d", dbm);
            check_true("value came from the modem", truth_contains(dbm), d);
            snprintf(d, sizeof d, "age %llu us", (unsigned long long)age);
            check_true("age < 100 ms (sync first poll)", age < 100000ull, d);
        }
        check_true("is_healthy(200 ms) right after init",
                   rssi_is_healthy(200 * 1000ull), "");
    }

    printf("\n== Part 1+2: values and window vs ground truth ==\n");
    sleep_us(700 * 1000);
    exact_checks("early", false);

    sleep_us(700 * 1000);
    exact_checks("after eviction", true);   /* > one window has now elapsed */

    printf("\n== Staleness: the modem firmware wedges ==\n");
    modem_pause(true);                      /* every poll now returns MODEM_BUSY */
    sleep_us(120 * 1000);                   /* let the in-flight poll finish     */
    {
        int dbm0 = 0, dbm1 = 0;
        uint64_t age0 = 0, age1 = 0;
        int rc0 = rssi_get_latest(&dbm0, &age0);
        sleep_us(RSSI_WINDOW_US + 500000ull);          /* one window + 0.5 s */
        int rc1 = rssi_get_latest(&dbm1, &age1);

        check_int("last good value still readable", rc1, rc0);
        check_int("value did not change while dead", dbm1, dbm0);
        snprintf(d, sizeof d, "%llu -> %llu us", (unsigned long long)age0,
                 (unsigned long long)age1);
        check_true("age grew past the window", age1 > age0 && age1 > RSSI_WINDOW_US, d);
        check_true("is_healthy(500 ms) -> false", !rssi_is_healthy(500 * 1000ull), "");
        check_true("is_healthy(60 s) -> still true", rssi_is_healthy(60000000ull), "");
        check_int("window drained: min", rssi_min_in_window(), RSSI_NONE);
        check_int("window drained: max", rssi_max_in_window(), RSSI_NONE);
        check_dbl("window drained: avg", rssi_avg_in_window(), NAN, 0.0);
    }

    printf("\n== Recovery ==\n");
    modem_pause(false);
    sleep_us(400 * 1000);
    {
        check_true("is_healthy(500 ms) -> true again",
                   rssi_is_healthy(500 * 1000ull), "");
        int mn = rssi_min_in_window();
        snprintf(d, sizeof d, "min %d", mn);
        check_true("window refilled", mn != RSSI_NONE, d);
    }

    printf("\n== Thread safety: 4 readers for 2 s while the sampler writes ==\n");
    atomic_store(&stress_stop, false);
    pthread_t th[4];
    stress_stat_t st[4];
    memset(st, 0, sizeof st);
    for (int i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    sleep_us(2000 * 1000);
    atomic_store(&stress_stop, true);
    uint64_t calls = 0, bad = 0, max_us = 0, total_us = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        calls += st[i].calls;
        bad += st[i].bad;
        total_us += st[i].total_us;
        if (st[i].max_us > max_us) max_us = st[i].max_us;
    }
    double mean_us = calls ? (double)total_us / (double)calls : 1e9;
    snprintf(d, sizeof d, "%llu calls, %llu impossible", (unsigned long long)calls,
             (unsigned long long)bad);
    check_true("no impossible / torn values", bad == 0 && calls > 0, d);
    /* A getter that polled the modem itself would average tens of ms. The max is
     * only a sanity bound: with 4 spinning readers it is really a measure of how
     * long the OS descheduled us for, not of the getter. */
    snprintf(d, sizeof d, "mean %.2f us, worst %llu us (modem blocks 100000)",
             mean_us, (unsigned long long)max_us);
    check_true("getters never block on the modem",
               mean_us < 500.0 && max_us < 50000ull, d);

    snprintf(d, sizeof d, "%d overlapping calls", atomic_load(&g_concurrent));
    check_true("modem_poll_rssi() called from one thread",
               atomic_load(&g_concurrent) == 0, d);
    snprintf(d, sizeof d, "%d ERROR polls survived", atomic_load(&g_n_err));
    check_true("MODEM_ERROR path exercised", atomic_load(&g_n_err) > 0, d);

    printf("\n== Shutdown ==\n");
    {
        uint64_t t0 = modem_now_us();
        rssi_deinit();
        uint64_t dt = modem_now_us() - t0;
        snprintf(d, sizeof d, "%llu us", (unsigned long long)dt);
        check_true("deinit returns within 500 ms", dt < 500000ull, d);

        int before = truth_count();
        sleep_us(250 * 1000);
        int after = truth_count();
        snprintf(d, sizeof d, "%d -> %d samples", before, after);
        check_true("sampler thread stopped polling", before == after, d);
    }

    printf("\nfake modem: %d OK, %d BUSY, %d ERROR polls\n", atomic_load(&g_n_ok),
           atomic_load(&g_n_busy), atomic_load(&g_n_err));

    if (fails)
        printf("\n%d CHECK(S) FAILED\n", fails);
    else
        printf("\nALL CHECKS PASSED (0 failed)\n");
    return fails ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* given by the interviewer: the vendor modem API                      */
/* ------------------------------------------------------------------ */

uint64_t modem_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec;
}

struct RssiSample modem_poll_rssi(void)
{
    static bool first_run = true;
    static uint32_t g_polls;
    static int cur_rssi = -85;
    static int cur_sinr = 12;
    struct RssiSample r;

    /* harness only: the vendor API is not thread-safe, so catch two callers */
    if (atomic_fetch_add(&g_in_call, 1) != 0)
        atomic_fetch_add(&g_concurrent, 1);

    /* poison the payload: only MODEM_OK is allowed to be trusted */
    r.status = MODEM_BUSY;
    r.rssi_dbm = 9999;
    r.sinr_db = -9999;
    r.timestamp = 0xDEADBEEFDEADBEEFull;

    if (first_run) {                       /* first call: instant, always valid */
        first_run = false;
        r.status = MODEM_OK;
        r.rssi_dbm = cur_rssi;
        r.sinr_db = cur_sinr;
        r.timestamp = modem_now_us();
        truth_record(r.timestamp, r.rssi_dbm, r.sinr_db);
        atomic_fetch_add(&g_n_ok, 1);
        atomic_fetch_sub(&g_in_call, 1);
        return r;
    }

    /* AT command failed: returns instantly with a garbage payload. Every 17th
     * poll (~6%), deterministic so the ERROR path is always exercised. */
    if (++g_polls % 17u == 5u) {
        r.status = MODEM_ERROR;
        atomic_fetch_add(&g_n_err, 1);
        atomic_fetch_sub(&g_in_call, 1);
        return r;
    }

    usleep((useconds_t)(rnd01() * 100000.0));          /* blocks 0 .. 100 ms */

    if (atomic_load(&g_modem_dead) || rnd01() < 0.45) { /* nothing new to report */
        atomic_fetch_add(&g_n_busy, 1);
        atomic_fetch_sub(&g_in_call, 1);
        return r;
    }

    cur_rssi += (int)(rnd01() * 13.0) - 6;             /* random walk, clamped */
    if (cur_rssi < -115) cur_rssi = -115;
    if (cur_rssi > -55) cur_rssi = -55;
    cur_sinr += (int)(rnd01() * 5.0) - 2;
    if (cur_sinr < -10) cur_sinr = -10;
    if (cur_sinr > 30) cur_sinr = 30;

    r.status = MODEM_OK;
    r.rssi_dbm = cur_rssi;
    r.sinr_db = cur_sinr;
    r.timestamp = modem_now_us();
    truth_record(r.timestamp, r.rssi_dbm, r.sinr_db);
    atomic_fetch_add(&g_n_ok, 1);
    atomic_fetch_sub(&g_in_call, 1);
    return r;
}
