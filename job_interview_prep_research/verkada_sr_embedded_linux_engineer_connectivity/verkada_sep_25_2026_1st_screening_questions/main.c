/* main.c — Verkada 1st screening harness, reconstructed (2026-09-25).
 *
 * Bottom half (random_float / get_timestamp / read_next_sample) and the
 * lux[3] + test_points block are what the CoderPad showed. Typos from the pad
 * are fixed: RAND_MAC, return_vale, missing comma in test_points, and the
 * "format:" / "useconds:" labels (those were IDE inlay hints, not code).
 *
 * Added for self-checking (not on the pad):
 *   - ground truth: every VALID value the fake sensor produced, so PASS/FAIL
 *     works on macOS too (macOS rand() != glibc rand()).
 *   - reader stress: 4 threads hammer the getters while the sensor thread
 *     writes; checks no torn/invented values and reports worst call latency.
 *   - WINDOW_TEST: build with -DLUX_WINDOW_US=2000000 -DWINDOW_TEST to check
 *     the 10-minute window logic with a 2-second window.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "als.h"
#include "recent_lux.h"

/* ------------------------------------------------------------------ */
/* ground truth (harness only)                                         */
/* ------------------------------------------------------------------ */

#define TRUTH_MAX 4096
static struct { uint64_t ts; float lux; } truth[TRUTH_MAX];
static int truth_n;
static pthread_mutex_t truth_lock = PTHREAD_MUTEX_INITIALIZER;

static void truth_record(uint64_t ts, float lux)
{
    pthread_mutex_lock(&truth_lock);
    if (truth_n < TRUTH_MAX) {
        truth[truth_n].ts = ts;
        truth[truth_n].lux = lux;
        truth_n++;
    }
    pthread_mutex_unlock(&truth_lock);
}

static float truth_latest(void)
{
    pthread_mutex_lock(&truth_lock);
    float v = truth_n ? truth[truth_n - 1].lux : NAN;
    pthread_mutex_unlock(&truth_lock);
    return v;
}

static float truth_at(uint64_t t)
{
    float v = NAN;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && truth[i].ts <= t; i++)
        v = truth[i].lux;
    pthread_mutex_unlock(&truth_lock);
    return v;
}

#ifdef WINDOW_TEST
static uint64_t truth_ts_at(uint64_t t)
{
    uint64_t ts = 0;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && truth[i].ts <= t; i++)
        ts = truth[i].ts;
    pthread_mutex_unlock(&truth_lock);
    return ts;
}
#endif

static bool truth_contains(float lux)
{
    bool found = false;
    pthread_mutex_lock(&truth_lock);
    for (int i = 0; i < truth_n && !found; i++)
        found = (memcmp(&truth[i].lux, &lux, sizeof lux) == 0);
    pthread_mutex_unlock(&truth_lock);
    return found;
}

static int fails;

static void check(const char *label, float got, float want)
{
    bool ok = isnan(want) ? isnan(got) : (got == want);
    if (!ok) fails++;
    printf("  %-28s got %-10f want %-10f %s\n", label, got, want, ok ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* reader stress: thread-safety + "non-blocking" evidence              */
/* ------------------------------------------------------------------ */

static _Atomic bool stress_stop;
static uint64_t stress_start;

typedef struct { uint64_t calls, bad, max_us; } stress_stat_t;

static void *stress_reader(void *arg)
{
    stress_stat_t *st = arg;
    while (!stress_stop) {
        uint64_t t0 = get_timestamp();
        float a = get_most_recent_lux();
        float b = get_lux_at(stress_start + (t0 - stress_start) / 2);
        uint64_t dt = get_timestamp() - t0;
        if (dt > st->max_us) st->max_us = dt;
        if (!isnan(a) && !truth_contains(a)) st->bad++;
        if (!isnan(b) && !truth_contains(b)) st->bad++;
        st->calls++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* test                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    if (init_recent_lux() != 0) {
        printf("init_recent_lux failed\n");
        return 1;
    }
    uint64_t start = get_timestamp();
    float lux;

    printf("== Part 1: get_most_recent_lux ==\n");
    if (isnan(truth_latest()))
        printf("  (sensor never read during init — first sample not taken synchronously)\n");

    lux = get_most_recent_lux();
    printf("lux [1] = %f, should be 3.94383 (glibc)\n", lux);
    check("lux[1] right after init", lux, truth_latest());

    usleep(1500 * 1000);
    lux = get_most_recent_lux();
    printf("lux [2] = %f, should be 3.35223 (glibc)\n", lux);
    check("lux[2] after 1.5 s", lux, truth_latest());

    usleep(1000 * 1000);
    lux = get_most_recent_lux();
    printf("lux [3] = %f, should be 5.5397 (glibc)\n", lux);
    check("lux[3] after 2.5 s", lux, truth_latest());

    usleep(1000 * 1000);    /* now ~3.5 s: every test point below is in the past */

    if (isnan(truth_latest())) {
        fails++;
        printf("  FAIL: read_next_sample() was never called — nothing to test against\n");
    }

    printf("\n== Part 2: get_lux_at ==\n");
    int test_points[5] = {[0] = 0, [1] = 200 * 1000, [2] = 1600 * 1000,
                          [3] = 2600 * 1000, [4] = 3100 * 1000};
    for (int i = 0; i < 5; i++) {
        uint64_t time_at = start + test_points[i];
        lux = get_lux_at(time_at);
        printf("t: %i, lux: %f\n", i, lux);
        char label[64];
        snprintf(label, sizeof label, "t%d = start + %d ms", i, test_points[i] / 1000);
#ifdef WINDOW_TEST
        uint64_t now = get_timestamp();
        check(label, lux, (now - time_at > LUX_WINDOW_US) ? NAN : truth_at(time_at));
#else
        check(label, lux, truth_at(time_at));
#endif
    }

    printf("\n== Part 2: edge cases ==\n");
    check("future (now + 10 s)", get_lux_at(get_timestamp() + 10 * 1000000ull), NAN);
    check("before first sample", get_lux_at(start - 60 * 1000000ull), NAN);
#ifdef WINDOW_TEST
    {
        /* window = 2 s. Right at the window's edge the value in effect came from a
         * sample that is OLDER than the window — it must not have been evicted. */
        uint64_t now = get_timestamp();
        uint64_t edge = now - LUX_WINDOW_US + 50 * 1000;
        check("window edge", get_lux_at(edge), truth_at(edge));
        printf("    (value at the edge came from a sample %s the window%s)\n",
               truth_ts_at(edge) < now - LUX_WINDOW_US ? "OLDER than" : "inside",
               truth_ts_at(edge) < now - LUX_WINDOW_US ? " — the keep-one-older rule was exercised" : "");
        check("older than window", get_lux_at(get_timestamp() - LUX_WINDOW_US - 100 * 1000), NAN);
    }
#endif

    printf("\n== Thread safety: 4 readers for 2 s while the sampler writes ==\n");
    stress_start = start;
    stress_stop = false;
    pthread_t th[4];
    stress_stat_t st[4];
    memset(st, 0, sizeof st);
    for (int i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, stress_reader, &st[i]);
    usleep(2000 * 1000);
    stress_stop = true;
    uint64_t calls = 0, bad = 0, max_us = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        calls += st[i].calls;
        bad += st[i].bad;
        if (st[i].max_us > max_us) max_us = st[i].max_us;
    }
    printf("  calls %llu, invented/torn values %llu, worst getter latency %llu us %s\n",
           (unsigned long long)calls, (unsigned long long)bad, (unsigned long long)max_us,
           (bad == 0 && calls > 0 && max_us < 100000) ? "PASS" : "FAIL");
    if (!(bad == 0 && calls > 0 && max_us < 100000)) fails++;

    deinit_recent_lux();
    printf("\n%s (%d failed)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* given by the interviewer: fake sensor                               */
/* ------------------------------------------------------------------ */

float random_float() { return (float)rand() / (float)RAND_MAX; }

uint64_t get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000ull * tv.tv_sec + tv.tv_usec;
}

struct SensorReading read_next_sample(void)
{
    static bool first_run = true;
    struct SensorReading return_value;

    uint64_t max_wait = 1 * 1000000;

    if (random_float() < 0.0001) {
        return_value.status = ERROR;       /* lux/timestamp left uninitialized */
        return return_value;
    }

    if (first_run) {
        first_run = false;
        return_value.lux = random_float() * 10;
        return_value.status = VALID;
        return_value.timestamp = get_timestamp();
        truth_record(return_value.timestamp, return_value.lux);   /* harness only */
        return return_value;
    }
    uint64_t next_value_change = 2 * 1000000 * random_float();
    if (max_wait <= next_value_change) {
        usleep(max_wait);
        return_value.status = NO_CHANGE;
        return return_value;
    }
    usleep(next_value_change);
    return_value.lux = random_float() * 10;
    return_value.status = VALID;
    return_value.timestamp = get_timestamp();
    truth_record(return_value.timestamp, return_value.lux);       /* harness only */
    return return_value;
}
