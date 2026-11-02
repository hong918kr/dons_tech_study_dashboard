/* I1 - wrap-safe 32-bit timestamp deltas -> frame period min/max/mean/jitter.
 * Context: camera SOF (start-of-frame) timestamps latched from a free-running
 * 32-bit counter. Build/run: ./run.sh
 */
//@@BEGIN I1
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t min, max;   /* shortest / longest period (ticks) */
    uint32_t mean;       /* floor(sum of deltas / count)      */
    uint32_t jitter_pp;  /* max - min (peak-to-peak)           */
} period_stats_t;

static inline uint32_t tick_delta(uint32_t a, uint32_t b)
{
    return (uint32_t)(b - a);   /* modulo 2^32: wrap-safe if real gap < 2^32 */
}

/* Returns 0 on success, -1 on bad args (NULL or fewer than 2 stamps). */
int period_stats(const uint32_t *ts, size_t n, period_stats_t *out)
{
    if (ts == NULL || out == NULL || n < 2)
        return -1;
    uint32_t mn = UINT32_MAX, mx = 0;
    uint64_t sum = 0;           /* n-1 deltas, each < 2^32: no overflow */
    for (size_t i = 1; i < n; i++) {
        uint32_t d = tick_delta(ts[i - 1], ts[i]);
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    out->min = mn;
    out->max = mx;
    out->mean = (uint32_t)(sum / (n - 1));
    out->jitter_pp = mx - mn;
    return 0;
}
//@@END I1

//@@BEGIN I1_dev
/* Follow-up: worst deviation from the nominal period, |d - nominal|. */
uint32_t max_dev_from_nominal(const uint32_t *ts, size_t n, uint32_t nominal)
{
    uint32_t worst = 0;
    for (size_t i = 1; i < n; i++) {
        uint32_t d = tick_delta(ts[i - 1], ts[i]);
        uint32_t dev = (d > nominal) ? d - nominal : nominal - d;
        if (dev > worst) worst = dev;
    }
    return worst;
}
//@@END I1_dev

//@@BEGIN I1_u16
/* Follow-up trap: 16-bit counter. b - a is done in int (promotion) and can be
 * negative, so the cast back to uint16_t is REQUIRED. */
static inline uint16_t tick_delta16(uint16_t a, uint16_t b)
{
    return (uint16_t)(b - a);
}
//@@END I1_u16

#include <assert.h>
#include <stdio.h>

int main(void)
{
    period_stats_t s;
    /* example: 1 MHz tick, ~30 fps (33333 us), wraps between [0] and [1] */
    const uint32_t ts[] = { 4294940000u, 6037u, 39372u, 72700u };
    assert(period_stats(ts, 4, &s) == 0);
    assert(s.min == 33328 && s.max == 33335 && s.mean == 33332 && s.jitter_pp == 7);
    assert(max_dev_from_nominal(ts, 4, 33333) == 5);

    /* wrap exactly at 0xFFFFFFFF -> 0 */
    const uint32_t w[] = { 0xFFFFFFFFu, 0u, 1u };
    assert(period_stats(w, 3, &s) == 0);
    assert(s.min == 1 && s.max == 1 && s.mean == 1 && s.jitter_pp == 0);

    /* duplicate stamp (delta 0) and the biggest representable gap */
    const uint32_t d[] = { 100u, 100u, 99u };   /* 99 - 100 == 2^32 - 1 */
    assert(period_stats(d, 3, &s) == 0);
    assert(s.min == 0 && s.max == UINT32_MAX && s.jitter_pp == UINT32_MAX);
    assert(s.mean == (uint32_t)((uint64_t)UINT32_MAX / 2));

    /* bad args */
    assert(period_stats(ts, 1, &s) == -1);
    assert(period_stats(ts, 0, &s) == -1);
    assert(period_stats(NULL, 4, &s) == -1);
    assert(period_stats(ts, 4, NULL) == -1);

    /* 16-bit trap: without the cast, (b - a) is int -65535 */
    uint16_t a16 = 65535, b16 = 0;
    assert((b16 - a16) == -65535);
    assert(tick_delta16(a16, b16) == 1);

    printf("I1 PASS\n");
    return 0;
}
