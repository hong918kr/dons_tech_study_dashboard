/*
 * J4  Clock Offset from Request/Response Timestamps (NTP-style)  --  Reference solution
 *
 * An MCU (local clock, microseconds) pings the host (host clock, microseconds):
 *   t[0] = t1  MCU send      (MCU clock)
 *   t[1] = t2  host receive  (host clock)
 *   t[2] = t3  host send     (host clock)
 *   t[3] = t4  MCU receive   (MCU clock)
 *
 *   offset = ((t2 - t1) + (t3 - t4)) / 2      host_time = mcu_time + offset
 *   delay  = (t4 - t1) - (t3 - t2)            round trip minus host processing
 *
 * Rounding (agreed with the interviewer): the /2 rounds toward NEGATIVE infinity
 * (floor), so +1.5 -> 1 and -1.5 -> -2. Plain C `/` truncates toward zero instead.
 *
 * Part A  sync_sample(): one sample -> offset/delay. Return false (outputs untouched)
 *         if t4 < t1, t3 < t2, or delay < 0.
 * Part B  sync_best(): among n samples pick the VALID one with the smallest delay
 *         (least queuing noise). Ties -> the earliest index. false if none is valid.
 * Part C  clock_apply(): move the applied offset toward a new measurement.
 *         - not synced yet, or |measured - applied| > STEP_THRESHOLD_US -> STEP (jump).
 *         - otherwise SLEW by at most MAX_SLEW_PPM * elapsed_local_us / 1e6
 *           (integer floor; elapsed <= 0 allows no movement).
 *         Returns the applied offset. to_host_time() = local_us + applied offset.
 *
 * No malloc, int64_t microseconds, integer math only.
 * Build & run:  ./run.sh J4 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define STEP_THRESHOLD_US 128000   /* 128 ms: bigger error -> jump instead of slew */
#define MAX_SLEW_PPM      500      /* at most 500 us of correction per second    */

typedef struct {
    int64_t offset_us;   /* applied offset: host = local + offset_us */
    bool    synced;      /* false until the first clock_apply()      */
} ClockSync;

/* floor(x / 2) without relying on >> of a negative number (impl-defined before C23) */
static int64_t floor_div2(int64_t x)
{
    int64_t q = x / 2;               /* truncates toward zero          */
    if (x < 0 && (x % 2) != 0) q--;  /* negative odd: one more step down */
    return q;
}

bool sync_sample(const int64_t t[4], int64_t *offset_us, int64_t *delay_us)
{
    int64_t rtt  = t[3] - t[0];      /* t4 - t1, MCU clock  */
    int64_t proc = t[2] - t[1];      /* t3 - t2, host clock */
    if (rtt < 0 || proc < 0) return false;
    int64_t delay = rtt - proc;
    if (delay < 0) return false;     /* host held it longer than the round trip: bad stamps */

    *offset_us = floor_div2((t[1] - t[0]) + (t[2] - t[3]));
    *delay_us  = delay;
    return true;
}

bool sync_best(const int64_t (*samples)[4], size_t n, int64_t *offset_us, int64_t *delay_us)
{
    bool found = false;
    int64_t best_off = 0, best_del = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t off, del;
        if (!sync_sample(samples[i], &off, &del)) continue;
        if (!found || del < best_del) {          /* strict '<' keeps the earliest on ties */
            found = true; best_off = off; best_del = del;
        }
    }
    if (found) { *offset_us = best_off; *delay_us = best_del; }
    return found;
}

int64_t clock_apply(ClockSync *cs, int64_t measured_offset_us, int64_t elapsed_local_us)
{
    int64_t err = measured_offset_us - cs->offset_us;
    int64_t mag = err < 0 ? -err : err;

    if (!cs->synced || mag > STEP_THRESHOLD_US) {          /* STEP */
        cs->offset_us = measured_offset_us;
        cs->synced = true;
        return cs->offset_us;
    }
    int64_t max_slew = elapsed_local_us > 0
                     ? elapsed_local_us * MAX_SLEW_PPM / 1000000 : 0;
    if (err >  max_slew) err =  max_slew;                  /* SLEW, clamped */
    if (err < -max_slew) err = -max_slew;
    cs->offset_us += err;
    return cs->offset_us;
}

int64_t to_host_time(const ClockSync *cs, int64_t local_us)
{
    return local_us + cs->offset_us;
}

int main(void)
{
    int64_t off = -1, del = -1;

    /* 1. symmetric path: true offset +5000, 200 us each way, host holds 100 us */
    const int64_t a[4] = { 10000, 15200, 15300, 10500 };
    CHECK(sync_sample(a, &off, &del));
    CHECK(off == 5000 && del == 400);

    /* 2. asymmetric path: up 300, down 100, true offset 5000 -> measured 5100 (error 100 <= delay/2) */
    const int64_t b[4] = { 20000, 25300, 25400, 20500 };
    CHECK(sync_sample(b, &off, &del));
    CHECK(off == 5100 && del == 400);

    /* 3. negative offset: MCU is 2000 us AHEAD of the host */
    const int64_t c[4] = { 100000, 98150, 98200, 100350 };
    CHECK(sync_sample(c, &off, &del));
    CHECK(off == -2000 && del == 300);

    /* 4. rounding of odd sums: floor, not truncation */
    const int64_t r1[4] = { 0, 1000, 1000, 1 };      /* sum  1999 ->  999 */
    const int64_t r2[4] = { 1000, 0, 0, 1001 };      /* sum -2001 -> -1001 */
    CHECK(sync_sample(r1, &off, &del) && off == 999 && del == 1);
    CHECK(sync_sample(r2, &off, &del) && off == -1001 && del == 1);

    /* 5. invalid samples are rejected and outputs are left untouched */
    const int64_t bad_rtt[4]   = { 100, 200, 300, 50 };    /* t4 < t1 */
    const int64_t bad_proc[4]  = { 0, 500, 400, 1000 };    /* t3 < t2 */
    const int64_t bad_delay[4] = { 0, 100, 600, 300 };     /* delay = 300 - 500 < 0 */
    off = 777; del = 777;
    CHECK(!sync_sample(bad_rtt, &off, &del));
    CHECK(!sync_sample(bad_proc, &off, &del));
    CHECK(!sync_sample(bad_delay, &off, &del));
    CHECK(off == 777 && del == 777);

    /* 6. best-of-N: minimum delay wins, invalid skipped, tie -> earliest */
    const int64_t s[5][4] = {
        { 10000, 15200, 15300, 10500 },   /* off 5000, delay  400 */
        { 20000, 25900, 26000, 21300 },   /* off 5300, delay 1200 (queued) */
        { 30000, 35080, 35100, 30180 },   /* off 5000, delay  160 <- best */
        { 40000, 45000, 44000, 41000 },   /* invalid: t3 < t2 */
        { 50000, 55100, 55120, 50180 },   /* off 5020, delay  160 (tie, later) */
    };
    CHECK(sync_best(s, 5, &off, &del));
    CHECK(off == 5000 && del == 160);
    CHECK(!sync_best(s + 3, 1, &off, &del));   /* only the invalid one */
    CHECK(!sync_best(NULL, 0, &off, &del));

    /* 7. slew: first call steps, then at most 500 us per second of local time */
    ClockSync cs = { 0, false };
    CHECK(clock_apply(&cs, 5000, 0) == 5000);          /* first sync -> step */
    CHECK(clock_apply(&cs, 7000, 1000000) == 5500);
    CHECK(clock_apply(&cs, 7000, 1000000) == 6000);
    CHECK(clock_apply(&cs, 7000, 1000000) == 6500);
    CHECK(clock_apply(&cs, 7000, 1000000) == 7000);
    CHECK(clock_apply(&cs, 7000, 1000000) == 7000);    /* converged: no overshoot */
    CHECK(clock_apply(&cs, 9000, 0) == 7000);          /* no elapsed time -> no move */
    CHECK(to_host_time(&cs, 1000000) == 1007000);

    /* 8. step on a large jump, then slew down; threshold is strict '>' */
    CHECK(clock_apply(&cs, 300000, 1000000) == 300000);   /* |293000| > 128000 -> step */
    CHECK(clock_apply(&cs, 299000, 400000) == 299800);    /* -1000 clamped to -200 */
    CHECK(clock_apply(&cs, 427800, 1000000) == 300300);   /* diff == 128000 -> slew +500 */
    CHECK(clock_apply(&cs, 428301, 1000000) == 428301);   /* diff == 128001 -> step */
    CHECK(to_host_time(&cs, -428301) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
