/* I4 - golden compare with abs/rel tolerance, report first N mismatches.
 * Context: NPU/ISP int32 output vs bit-accurate C model. Build/run: ./run.sh
 */
//@@BEGIN I4
#include <stdint.h>
#include <stddef.h>

typedef struct { size_t idx; int32_t got, exp; } mismatch_t;

static uint64_t abs_diff(int32_t a, int32_t b)
{
    int64_t d = (int64_t)a - (int64_t)b;      /* never overflows in 64-bit */
    return (uint64_t)(d < 0 ? -d : d);
}

/* pass if |got-exp| <= abs_tol + |exp| * rel_ppm / 1e6  (isclose style).
 * Stores the first max_out mismatches in out[], returns the TOTAL count. */
size_t golden_compare(const int32_t *got, const int32_t *exp, size_t n,
                      uint32_t abs_tol, uint32_t rel_ppm,
                      mismatch_t *out, size_t max_out)
{
    size_t bad = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t diff = abs_diff(got[i], exp[i]);     /* <= 2^32 - 1 */
        uint64_t mag = abs_diff(exp[i], 0);           /* <= 2^31     */
        /* multiply instead of divide: every term < 2^63, no overflow */
        if (diff * 1000000u <= (uint64_t)abs_tol * 1000000u + mag * rel_ppm)
            continue;
        if (bad < max_out) {
            out[bad].idx = i;
            out[bad].got = got[i];
            out[bad].exp = exp[i];
        }
        bad++;
    }
    return bad;
}
//@@END I4

#include <assert.h>
#include <stdio.h>

int main(void)
{
    mismatch_t m[4];
    /* example: abs_tol 1, rel 1% (10000 ppm) */
    const int32_t exp[] = { 100, -50, 1000, INT32_MIN };
    const int32_t got[] = { 101, -50, 1012, INT32_MAX };
    assert(golden_compare(got, exp, 4, 1, 10000, m, 4) == 2);
    assert(m[0].idx == 2 && m[0].got == 1012 && m[0].exp == 1000);   /* 12 > 1+10 */
    assert(m[1].idx == 3 && m[1].got == INT32_MAX && m[1].exp == INT32_MIN);

    /* boundary: 11 == 1 + 10 passes (<=) */
    const int32_t g2[] = { 1011 }, e2[] = { 1000 };
    assert(golden_compare(g2, e2, 1, 1, 10000, m, 4) == 0);

    /* exact compare (tol 0, rel 0) */
    const int32_t g3[] = { 5, 6, 7 }, e3[] = { 5, 6, 8 };
    assert(golden_compare(g3, e3, 3, 0, 0, m, 4) == 1 && m[0].idx == 2);

    /* max_out cap: total counted, only first 2 stored, out[2] untouched */
    const int32_t g4[] = { 1, 2, 3, 4, 5 }, e4[] = { 0, 0, 0, 0, 0 };
    m[2].idx = 999;
    assert(golden_compare(g4, e4, 5, 0, 0, m, 2) == 5);
    assert(m[0].idx == 0 && m[1].idx == 1 && m[2].idx == 999);

    /* max_out 0 with out == NULL is fine; n == 0 -> 0 */
    assert(golden_compare(g4, e4, 5, 0, 0, NULL, 0) == 5);
    assert(golden_compare(g4, e4, 0, 0, 0, NULL, 0) == 0);

    /* extreme tolerance values must not overflow */
    assert(golden_compare(got, exp, 4, UINT32_MAX, UINT32_MAX, m, 4) == 0);

    printf("I4 PASS\n");
    return 0;
}
