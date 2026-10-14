/*
 * T1-6  Sensor Fault Detection
 *
 * Classify every sample of a temperature stream with a fault BITMASK (several faults
 * can apply to the same sample), then make the overheat decision using only clean
 * samples.
 *
 * Fault rules (agreed with the interviewer), evaluated for sample i:
 *   F_RANGE   temp < cfg->lo or temp > cfg->hi            (e.g. -400 .. 1500 in 0.1 degC)
 *   F_DROPOUT i > 0 and t[i] - t[i-1] > 2 * cfg->period_ms  (flag the sample AFTER the gap;
 *             wrap-safe uint32_t subtraction)
 *   F_SPIKE   |temp - ref| > cfg->max_step, where ref is the last sample that was neither
 *             F_RANGE nor F_SPIKE. Not checked when the sample is F_RANGE or F_DROPOUT
 *             (a delta across a gap means nothing), or when there is no ref yet.
 *   F_STUCK   the same raw value has now appeared cfg->stuck_n times in a row
 *             (flag the stuck_n-th sample and every later one in that run).
 *
 *   classify_faults() writes flags[i] for every i and returns how many samples are faulty
 *   (flags != 0).
 *   first_valid_overheat() returns the index of the first sample with flags == 0 and
 *   temp >= thresh, or -1 if there is none.
 *
 *   No malloc. O(n) time, O(1) extra memory.
 *
 * Build & run:  ./run.sh T1-6
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;   /* temp in 0.1 degC */

enum { F_RANGE = 1u << 0, F_DROPOUT = 1u << 1, F_SPIKE = 1u << 2, F_STUCK = 1u << 3 };

typedef struct {
    uint32_t period_ms;   /* nominal sample period          */
    uint16_t stuck_n;     /* identical values in a row >= 2 */
    int16_t  max_step;    /* max plausible |delta| per sample */
    int16_t  lo, hi;      /* valid range, inclusive          */
} FaultCfg;

size_t classify_faults(const Sample *s, size_t n, const FaultCfg *cfg, uint8_t *flags)
{
    /* TODO: your code here */
    (void)s; (void)cfg;
    for (size_t i = 0; i < n; i++) flags[i] = 0xFF;   /* dummy value */
    return 0;
}

int32_t first_valid_overheat(const Sample *s, size_t n, const uint8_t *flags, int16_t thresh)
{
    /* TODO: your code here */
    (void)s; (void)n; (void)flags; (void)thresh;
    return 0;
}

int main(void)
{
    const FaultCfg cfg = { .period_ms = 100, .stuck_n = 3, .max_step = 50, .lo = -400, .hi = 1500 };
    uint8_t fl[16];

    /* 1. one stream with every fault type */
    const Sample a[] = {
        {0, 250}, {100, 255}, {200, 900}, {300, 260}, {400, 262}, {700, 265},
        {800, 265}, {900, 265}, {1000, 265}, {1100, 1600}, {1200, 270} };
    size_t k = classify_faults(a, 11, &cfg, fl);
    CHECK(k == 5);
    CHECK(fl[0] == 0 && fl[1] == 0 && fl[3] == 0 && fl[4] == 0 && fl[6] == 0 && fl[10] == 0);
    CHECK(fl[2] == F_SPIKE);            /* 900 vs 255 */
    CHECK(fl[5] == F_DROPOUT);          /* gap 300 > 200 */
    CHECK(fl[7] == F_STUCK && fl[8] == F_STUCK);
    CHECK(fl[9] == F_RANGE);            /* 160.0 degC */
    /* both hot samples (900 spike, 1600 out of range) are faulty -> no overheat */
    CHECK(first_valid_overheat(a, 11, fl, 800) == -1);

    /* 2. empty input */
    CHECK(classify_faults(NULL, 0, &cfg, fl) == 0);
    CHECK(first_valid_overheat(NULL, 0, fl, 800) == -1);

    /* 3. faults co-occur: a sensor stuck at an out-of-range value */
    const Sample b[] = { {0, 2000}, {100, 2000}, {200, 2000} };
    k = classify_faults(b, 3, &cfg, fl);
    CHECK(k == 3 && fl[0] == F_RANGE && fl[1] == F_RANGE && fl[2] == (F_RANGE | F_STUCK));

    /* 4. gap of exactly 2 * period is not a dropout; wrap-around timestamps */
    const Sample c[] = { {0xFFFFFF9Cu, 300}, {100, 310}, {300, 320}, {601, 330} };
    k = classify_faults(c, 4, &cfg, fl);
    CHECK(k == 1 && fl[0] == 0 && fl[1] == 0 && fl[2] == 0 && fl[3] == F_DROPOUT);

    /* 5. spike reference skips bad samples; a real overheat after a fault is found */
    const Sample d[] = { {0, 760}, {100, -500}, {200, 790}, {300, 810}, {400, 850} };
    k = classify_faults(d, 5, &cfg, fl);
    CHECK(k == 1 && fl[1] == F_RANGE && fl[2] == 0 && fl[3] == 0);
    CHECK(first_valid_overheat(d, 5, fl, 800) == 3);

    /* 6. first sample: no delta, no gap; a single value is not stuck */
    const Sample e[] = { {5000, 1500} };
    k = classify_faults(e, 1, &cfg, fl);
    CHECK(k == 0 && fl[0] == 0);
    CHECK(first_valid_overheat(e, 1, fl, 1500) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
