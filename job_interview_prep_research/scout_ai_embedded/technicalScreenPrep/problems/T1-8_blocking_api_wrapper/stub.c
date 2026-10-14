/*
 * T1-8  Blocking API Wrapper
 *
 * The vendor driver gives you one function:
 *
 *     int16_t read_temp(void);   // temp in 0.1 degC, or TEMP_ERR on a bus error
 *
 * It BLOCKS for up to 1 s (one I2C conversion) and is NOT thread-safe: two threads
 * inside it at the same time corrupt the bus. Many application threads want the
 * temperature. Build a small service on top of it:
 *
 *   int  sampler_start(void);           start ONE background sampler thread.
 *                                       0 on success, -1 if already running / error.
 *   void sampler_stop(void);            stop it and join; safe to call twice.
 *   bool get_latest_temp(int16_t *out); NEVER waits for the sensor. false if no good
 *                                       reading exists yet, else *out = latest reading.
 *   bool is_overheating(int16_t thresh); true if the MAX of the readings taken within
 *                                       the last 5000 ms (now - t <= 5000) is > thresh.
 *                                       false if there is no reading in that window.
 *
 * Rules: pthreads; read_temp() is called only by the sampler thread; never hold a lock
 * across read_temp(); TEMP_ERR readings are dropped (keep the last good value); keep a
 * fixed-size ring buffer of (t_ms, temp) history -- no malloc. Use now_ms() for time.
 * start/stop are called from one control thread.
 *
 * The harness below fakes the sensor and the clock so the test runs in < 1 s:
 * read_temp() waits (nanosleep polling) until the test feeds a value, or returns TEMP_ERR
 * after READ_TIMEOUT_MS (a scaled-down "1 s"). now_ms() only moves when the test says so.
 *
 * Build & run:  ./run.sh T1-8
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ===================== GIVEN: fake sensor + fake clock (do not modify) ============== */
#define TEMP_ERR        INT16_MIN
#define READ_TIMEOUT_MS 200            /* real part: up to 1000 ms */

static atomic_uint     g_now_ms;                              /* fake monotonic clock */
static uint32_t now_ms(void) { return (uint32_t)atomic_load(&g_now_ms); }

static pthread_mutex_t g_dev_mx = PTHREAD_MUTEX_INITIALIZER;  /* harness only */
static bool            g_dev_has;
static int16_t         g_dev_val;
static atomic_int      g_dev_inside;
static atomic_bool     g_dev_overlap;                         /* concurrent entry seen */

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static uint64_t real_ms(void)           /* real monotonic time, harness + tests only */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int16_t read_temp(void)                 /* blocking, NOT thread-safe */
{
    if (atomic_fetch_add(&g_dev_inside, 1) != 0) atomic_store(&g_dev_overlap, true);
    int16_t v = TEMP_ERR;
    uint64_t start = real_ms();
    while (real_ms() - start < READ_TIMEOUT_MS) {
        sleep_ms(5);                                          /* "conversion time" */
        pthread_mutex_lock(&g_dev_mx);
        bool got = g_dev_has;
        if (got) { v = g_dev_val; g_dev_has = false; }
        pthread_mutex_unlock(&g_dev_mx);
        if (got) break;
    }
    atomic_fetch_sub(&g_dev_inside, 1);
    return v;                                                 /* TEMP_ERR = timeout/NACK */
}

static void sensor_feed(int16_t v)      /* test: the next read_temp() returns v */
{
    pthread_mutex_lock(&g_dev_mx);
    g_dev_val = v; g_dev_has = true;
    pthread_mutex_unlock(&g_dev_mx);
}
/* ===================================================================================== */

#define WINDOW_MS 5000u
#define HIST_CAP  16u        /* >= readings in 5 s: one read takes ~1 s, plus margin */

typedef struct { uint32_t t_ms; int16_t temp; } Rec;

/* TODO: your state here (mutex, latest value, ring buffer, thread handle, run flag) */

int sampler_start(void)
{
    /* TODO: your code here */
    return 0;
}

void sampler_stop(void)
{
    /* TODO: your code here */
}

bool get_latest_temp(int16_t *out)
{
    /* TODO: your code here */
    (void)out;
    return false;
}

bool is_overheating(int16_t thresh)
{
    /* TODO: your code here (use now_ms() for the current time) */
    (void)thresh; (void)now_ms;
    return false;
}

/* ------------------------------------ tests ------------------------------------------ */

static bool wait_latest(int16_t want, int timeout_ms)   /* poll the public API */
{
    for (int i = 0; i < timeout_ms; i += 2) {
        int16_t v;
        if (get_latest_temp(&v) && v == want) return true;
        sleep_ms(2);
    }
    return false;
}

int main(void)
{
    int16_t v = 0;

    /* 1. before start: no data, not overheating; start once only */
    CHECK(get_latest_temp(&v) == false);
    CHECK(is_overheating(800) == false);
    CHECK(sampler_start() == 0);
    CHECK(sampler_start() == -1);

    /* 2. never blocks: the sampler is parked inside read_temp() right now */
    uint64_t t0 = real_ms();
    CHECK(get_latest_temp(&v) == false);
    CHECK(real_ms() - t0 < 50);

    /* 3. readings flow through; window max is strictly greater than thresh */
    atomic_store(&g_now_ms, 1000); sensor_feed(250);
    CHECK(wait_latest(250, 300));
    CHECK(is_overheating(800) == false);
    atomic_store(&g_now_ms, 2000); sensor_feed(900);
    CHECK(wait_latest(900, 300));
    CHECK(is_overheating(800) == true);
    CHECK(is_overheating(900) == false);          /* 900 > 900 is false */

    /* 4. a cool latest value does not hide a hot reading still inside the 5 s window */
    atomic_store(&g_now_ms, 3000); sensor_feed(300);
    CHECK(wait_latest(300, 300));
    CHECK(is_overheating(800) == true);
    atomic_store(&g_now_ms, 7000);                /* 7000 - 2000 = 5000: still inside */
    CHECK(is_overheating(800) == true);
    atomic_store(&g_now_ms, 7001);                /* 5001: the 900 reading expired */
    CHECK(is_overheating(800) == false);

    /* 5. a bus error (read timeout) keeps the last good value */
    sleep_ms(READ_TIMEOUT_MS + 50);
    CHECK(get_latest_temp(&v) && v == 300);

    /* 6. ring buffer wraps (20 readings > HIST_CAP 16); window still correct */
    bool flowed = true;
    for (int16_t k = 0; k < 20 && flowed; k++) {
        atomic_store(&g_now_ms, 10000u + 100u * (uint32_t)k);
        sensor_feed((int16_t)(400 + k));
        flowed = wait_latest((int16_t)(400 + k), 300);
    }
    CHECK(flowed);
    CHECK(is_overheating(418) == true);           /* max in window = 419 */
    CHECK(is_overheating(419) == false);

    /* 7. stop is bounded by one read, idempotent; the sensor never saw two callers */
    t0 = real_ms();
    sampler_stop();
    CHECK(real_ms() - t0 < READ_TIMEOUT_MS + 150);
    sampler_stop();
    CHECK(get_latest_temp(&v) && v == 419);
    CHECK(atomic_load(&g_dev_overlap) == false);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
