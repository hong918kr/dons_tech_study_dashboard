/* I8 - binary search for max passing frequency (monotone assumption),
 * flaky points handled by requiring k consecutive passes.
 * Context: NPU / CPU cluster fmax at one voltage, each run = a stress pattern.
 * Build/run: ./run.sh
 */
//@@BEGIN I8
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef bool (*freq_test_fn)(void *ctx, uint32_t mhz);

/* A point counts as pass only if k runs in a row pass (flaky -> fail). */
static bool stable_pass(freq_test_fn t, void *ctx, uint32_t mhz, uint32_t k)
{
    for (uint32_t i = 0; i < k; i++)
        if (!t(ctx, mhz))
            return false;
    return true;
}

/* Grid f = f_lo + i*step, i in [0, n). Assumes pass below fmax, fail above.
 * Returns the highest stable-pass frequency, or 0 if f_lo itself fails. */
uint32_t find_fmax(freq_test_fn t, void *ctx, uint32_t f_lo, uint32_t step,
                   uint32_t n, uint32_t k)
{
    if (t == NULL || n == 0 || k == 0)
        return 0;
    if (!stable_pass(t, ctx, f_lo, k))
        return 0;
    uint32_t lo = 0, hi = n;     /* invariant: lo passes, hi fails (n = virtual) */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (stable_pass(t, ctx, f_lo + mid * step, k))
            lo = mid;
        else
            hi = mid;
    }
    return f_lo + lo * step;
}
//@@END I8

#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t fmax;        /* true limit */
    uint32_t flaky_mhz;   /* passes on odd calls, fails on even calls (0 = none) */
    uint32_t flaky_calls;
    uint32_t calls;
} dut_t;

static bool fake_test(void *ctx, uint32_t mhz)
{
    dut_t *d = ctx;
    d->calls++;
    if (d->flaky_mhz && mhz == d->flaky_mhz)
        return (d->flaky_calls++ % 2) == 0;
    return mhz <= d->fmax;
}

int main(void)
{
    /* grid 800..2000 MHz step 10 -> n = 121 */
    dut_t d = { 1430, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 1) == 1430);
    printf("  I8 k=1 clean: %u calls\n", d.calls);
    assert(d.calls <= 1 + 7);                 /* 1 + ceil(log2 121) */

    d = (dut_t){ 1430, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 3) == 1430);
    printf("  I8 k=3 clean: %u calls\n", d.calls);
    assert(d.calls <= 3 * (1 + 7));

    /* flaky point just above the real limit: k=1 can be fooled, k=3 is not */
    d = (dut_t){ 1430, 1440, 0, 0 };
    uint32_t f1 = find_fmax(fake_test, &d, 800, 10, 121, 1);
    printf("  I8 flaky@1440 k=1 -> %u\n", f1);
    d = (dut_t){ 1430, 1440, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 3) == 1430);

    /* edges: everything passes / nothing passes / single point / bad args */
    d = (dut_t){ 5000, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 1) == 2000);
    d = (dut_t){ 700, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 1) == 0);
    d = (dut_t){ 1430, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 1, 1) == 800);
    assert(find_fmax(NULL, &d, 800, 10, 121, 1) == 0);
    assert(find_fmax(fake_test, &d, 800, 10, 0, 1) == 0);
    assert(find_fmax(fake_test, &d, 800, 10, 121, 0) == 0);
    /* fmax exactly on the second grid point */
    d = (dut_t){ 810, 0, 0, 0 };
    assert(find_fmax(fake_test, &d, 800, 10, 121, 1) == 810);

    printf("I8 PASS\n");
    return 0;
}
