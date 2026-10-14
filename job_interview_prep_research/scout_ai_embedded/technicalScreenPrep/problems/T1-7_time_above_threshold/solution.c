/*
 * T1-7  Time Above Threshold
 *
 * Given temperature samples sorted by time, return the total time inside the window
 * [t0_ms, t1_ms] during which the temperature was >= thresh. Support two models of the
 * signal between samples:
 *
 *   MODE_STEP  (sample-and-hold): sample i's value holds on [t_i, t_{i+1}).
 *   MODE_LERP  (linear interpolation): the value moves on a straight line between
 *              consecutive samples; crossings of thresh are computed exactly.
 *
 * Definitions (agreed with the interviewer):
 *   - The signal is known only between the first and the last sample. Window time before
 *     the first sample or after the last sample counts as NOT hot (no data).
 *   - Result unit: MICROSECONDS (uint64_t), so a crossing at 24.2857 ms is not lost.
 *     A crossing time is rounded to the nearest microsecond.
 *   - Integer math only: multiply before divide, 64-bit intermediates.
 *   - Return 0 if n < 2 or t1_ms <= t0_ms. Timestamps strictly increasing, no wraparound.
 *
 * Reference solution.  Build & run:  ./run.sh T1-7 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct { uint32_t t_ms; int16_t temp; } Sample;
typedef enum { MODE_STEP, MODE_LERP } Mode;

/* length of [a,b] ∩ [lo,hi], or 0 */
static int64_t overlap(int64_t a, int64_t b, int64_t lo, int64_t hi)
{
    int64_t s = a > lo ? a : lo;
    int64_t e = b < hi ? b : hi;
    return e > s ? e - s : 0;
}

/* time (us) where the line (ta,va)->(tb,vb) reaches th; requires va != vb */
static int64_t crossing_us(int64_t ta, int64_t tb, int32_t va, int32_t vb, int32_t th)
{
    int64_t num = (int64_t)(th - va) * (tb - ta);   /* multiply first */
    int64_t den = vb - va;
    if (den < 0) { num = -num; den = -den; }        /* num >= 0 for a real crossing */
    return ta + (num + den / 2) / den;              /* round to nearest us */
}

uint64_t time_above_us(const Sample *s, size_t n, int16_t thresh,
                       uint32_t t0_ms, uint32_t t1_ms, Mode mode)
{
    if (n < 2 || t1_ms <= t0_ms) return 0;
    const int64_t lo = (int64_t)t0_ms * 1000, hi = (int64_t)t1_ms * 1000;
    int64_t total = 0;

    for (size_t i = 0; i + 1 < n; i++) {
        int64_t ta = (int64_t)s[i].t_ms * 1000, tb = (int64_t)s[i + 1].t_ms * 1000;
        if (tb <= lo || ta >= hi) continue;          /* segment outside the window */
        int32_t va = s[i].temp, vb = s[i + 1].temp;
        bool ha = va >= thresh, hb = vb >= thresh;

        if (mode == MODE_STEP) {
            if (ha) total += overlap(ta, tb, lo, hi);
        } else if (ha && hb) {                       /* whole segment hot */
            total += overlap(ta, tb, lo, hi);
        } else if (ha != hb) {                       /* exactly one crossing */
            int64_t tc = crossing_us(ta, tb, va, vb, thresh);
            total += ha ? overlap(ta, tc, lo, hi)    /* falling: hot before tc */
                        : overlap(tc, tb, lo, hi);   /* rising:  hot after  tc */
        }
    }
    return (uint64_t)total;
}

int main(void)
{
    const Sample a[] = { {0, 70}, {10, 90}, {20, 95}, {30, 60} };

    /* 1. basic: whole data range. LERP crossings at t = 5 ms and t = 24.2857 ms */
    CHECK(time_above_us(a, 4, 80, 0, 30, MODE_STEP) == 20000);
    CHECK(time_above_us(a, 4, 80, 0, 30, MODE_LERP) == 19286);

    /* 2. window partly overlapping the hot part */
    CHECK(time_above_us(a, 4, 80, 15, 25, MODE_STEP) == 10000);
    CHECK(time_above_us(a, 4, 80, 15, 25, MODE_LERP) == 9286);
    CHECK(time_above_us(a, 4, 80, 0, 7, MODE_STEP) == 0);
    CHECK(time_above_us(a, 4, 80, 0, 7, MODE_LERP) == 2000);

    /* 3. window extends past the last sample: no data there counts as not hot */
    CHECK(time_above_us(a, 4, 80, 25, 100, MODE_STEP) == 5000);
    CHECK(time_above_us(a, 4, 80, 25, 100, MODE_LERP) == 0);

    /* 4. degenerate inputs */
    CHECK(time_above_us(NULL, 0, 80, 0, 30, MODE_LERP) == 0);
    CHECK(time_above_us(a, 1, 80, 0, 30, MODE_STEP) == 0);
    CHECK(time_above_us(a, 4, 80, 20, 20, MODE_STEP) == 0);
    CHECK(time_above_us(a, 4, 80, 30, 0, MODE_LERP) == 0);

    /* 5. exactly at the threshold counts as hot in both modes */
    const Sample b[] = { {0, 80}, {10, 80} };
    CHECK(time_above_us(b, 2, 80, 0, 10, MODE_STEP) == 10000);
    CHECK(time_above_us(b, 2, 80, 0, 10, MODE_LERP) == 10000);

    /* 6. 64-bit intermediates: (0 - -30000) * 4e12 us does not fit in 32 bits */
    const Sample c[] = { {0, -30000}, {4000000000u, 30000} };
    CHECK(time_above_us(c, 2, 0, 0, 4000000000u, MODE_LERP) == 2000000000000ull);
    CHECK(time_above_us(c, 2, 0, 0, 4000000000u, MODE_STEP) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
