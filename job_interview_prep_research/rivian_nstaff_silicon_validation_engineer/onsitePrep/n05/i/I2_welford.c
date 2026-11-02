/* I2 - online mean/variance (Welford), no sample storage.
 * Context: 10^6 DDR read-latency or PLL lock-time samples on a target with
 * a few KB of RAM. Build/run: ./run.sh
 */
//@@BEGIN I2
#include <stdint.h>

typedef struct {
    uint32_t n;
    double mean;
    double m2;   /* sum of squared deviations from the running mean */
} welford_t;

void welford_init(welford_t *w)
{
    w->n = 0;
    w->mean = 0.0;
    w->m2 = 0.0;
}

void welford_add(welford_t *w, double x)
{
    w->n++;
    double d = x - w->mean;      /* deviation from the OLD mean */
    w->mean += d / w->n;
    w->m2 += d * (x - w->mean);  /* times deviation from the NEW mean */
}

/* Sample variance (divide by n-1); 0 when fewer than 2 samples. */
double welford_var(const welford_t *w)
{
    return (w->n < 2) ? 0.0 : w->m2 / (w->n - 1);
}
//@@END I2

//@@BEGIN I2_int
/* Follow-up: integer-only (no FPU / no FP in ISR). Shift every sample by the
 * first one (k) so the sums stay small. Assumes |x - k| < 2^20 and n < 2^20. */
typedef struct { uint32_t n, k; int64_t s; uint64_t ss; } istat_t;

void istat_add(istat_t *st, uint32_t x)
{
    if (st->n == 0) st->k = x;
    int64_t d = (int64_t)x - st->k;
    st->n++;
    st->s += d;
    st->ss += (uint64_t)(d * d);
}

/* floor-ish: mean = k + s/n ; var = (ss - (s/n)*s) / (n-1) */
uint32_t istat_mean(const istat_t *st) { return st->k + (uint32_t)(st->s / st->n); }
uint64_t istat_var(const istat_t *st)
{
    if (st->n < 2) return 0;
    int64_t md = st->s / (int64_t)st->n;
    return (uint64_t)((int64_t)st->ss - md * st->s) / (st->n - 1);
}
//@@END I2_int

//@@BEGIN I2_merge
/* Follow-up: merge two accumulators (e.g. per-core stats) - Chan et al. */
void welford_merge(welford_t *a, const welford_t *b)
{
    if (b->n == 0) return;
    uint32_t n = a->n + b->n;
    double d = b->mean - a->mean;
    a->m2 += b->m2 + d * d * ((double)a->n * b->n / n);
    a->mean += d * b->n / n;
    a->n = n;
}
//@@END I2_merge

#include <assert.h>
#include <stdio.h>

static int near(double a, double b, double tol) { double d = a - b; return (d < 0 ? -d : d) <= tol; }

int main(void)
{
    welford_t w;
    welford_init(&w);
    assert(w.n == 0 && welford_var(&w) == 0.0);

    const double xs[] = { 2, 4, 4, 4, 5, 5, 7, 9 };
    for (int i = 0; i < 8; i++) welford_add(&w, xs[i]);
    assert(w.n == 8 && near(w.mean, 5.0, 1e-12));
    assert(near(welford_var(&w), 32.0 / 7.0, 1e-12));   /* population var = 4 */

    welford_t one; welford_init(&one); welford_add(&one, 42.0);
    assert(near(one.mean, 42.0, 0) && welford_var(&one) == 0.0);

    /* big offset: latency ~1e9 ticks, deviations -6,-3,3,6 -> var 90/3 = 30 */
    welford_t big; welford_init(&big);
    const double bx[] = { 1e9 + 4, 1e9 + 7, 1e9 + 13, 1e9 + 16 };
    for (int i = 0; i < 4; i++) welford_add(&big, bx[i]);
    assert(near(big.mean, 1e9 + 10, 1e-6) && near(welford_var(&big), 30.0, 1e-6));

    /* naive sum/sumsq in float on the same data: catastrophic cancellation */
    float s = 0, ss = 0;
    for (int i = 0; i < 4; i++) { float x = (float)bx[i]; s += x; ss += x * x; }
    float naive = (ss - s * s / 4) / 3;
    printf("  I2 naive float var = %g (true 30)\n", (double)naive);
    assert(!near((double)naive, 30.0, 1.0));

    /* integer version */
    istat_t is = { 0, 0, 0, 0 };
    const uint32_t ix[] = { 1000004, 1000007, 1000013, 1000016 };
    for (int i = 0; i < 4; i++) istat_add(&is, ix[i]);
    assert(istat_mean(&is) == 1000010 && istat_var(&is) == 30);

    /* merge: split xs into two halves -> same as one pass */
    welford_t a, b; welford_init(&a); welford_init(&b);
    for (int i = 0; i < 3; i++) welford_add(&a, xs[i]);
    for (int i = 3; i < 8; i++) welford_add(&b, xs[i]);
    welford_merge(&a, &b);
    assert(a.n == 8 && near(a.mean, 5.0, 1e-12) && near(welford_var(&a), 32.0 / 7.0, 1e-12));

    printf("I2 PASS\n");
    return 0;
}
