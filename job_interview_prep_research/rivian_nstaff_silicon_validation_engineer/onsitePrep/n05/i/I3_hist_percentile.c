/* I3 - latency histogram buckets -> p50 / p99 (nearest rank) without storing
 * samples. Context: PCIe MMIO read round-trip or frame-done ISR latency.
 * Build/run: ./run.sh
 */
//@@BEGIN I3
#include <stdint.h>
#include <stddef.h>

/* bucket i holds samples in [i*width, (i+1)*width); last bucket = overflow */
void hist_add(uint32_t *hist, size_t nb, uint32_t width, uint32_t sample)
{
    if (hist == NULL || nb == 0 || width == 0)
        return;
    size_t i = sample / width;
    if (i >= nb)
        i = nb - 1;                        /* clamp into overflow bucket */
    hist[i]++;
}

/* pm = per-mille: 500 = p50, 990 = p99, 999 = p99.9 (valid 1..1000).
 * Returns the bucket index holding the nearest-rank percentile, or -1. */
int hist_percentile(const uint32_t *hist, size_t nb, uint32_t pm)
{
    if (hist == NULL || pm == 0 || pm > 1000)
        return -1;
    uint64_t total = 0;
    for (size_t i = 0; i < nb; i++)
        total += hist[i];
    if (total == 0)
        return -1;
    uint64_t rank = (total * pm + 999) / 1000;   /* ceil(p * N), >= 1 */
    uint64_t cum = 0;
    for (size_t i = 0; i < nb; i++) {
        cum += hist[i];
        if (cum >= rank)
            return (int)i;
    }
    return -1;                             /* unreachable */
}
//@@END I3

//@@BEGIN I3_log2
/* Follow-up: log2 buckets (0, 1, 2-3, 4-7, ...) cover ns..s in 33 buckets. */
size_t log2_bucket(uint32_t x)
{
    size_t b = 0;
    while (x) { b++; x >>= 1; }   /* b = bit length: 0 -> 0, 1 -> 1, 2..3 -> 2 */
    return b;
}
//@@END I3_log2

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* example: width 10 ns, N = 100 */
    const uint32_t h[] = { 0, 50, 40, 9, 1 };
    assert(hist_percentile(h, 5, 500) == 1);    /* rank 50 -> cum 50 at bucket 1 */
    assert(hist_percentile(h, 5, 990) == 3);    /* rank 99 -> cum 99 at bucket 3 */
    assert(hist_percentile(h, 5, 1000) == 4);   /* max */
    assert(hist_percentile(h, 5, 1) == 1);      /* rank 1 -> first non-empty */
    assert(hist_percentile(h, 5, 501) == 2);    /* ceil(50.1) = 51 */

    /* empty / bad args */
    const uint32_t z[] = { 0, 0, 0 };
    assert(hist_percentile(z, 3, 500) == -1);
    assert(hist_percentile(h, 0, 500) == -1);
    assert(hist_percentile(h, 5, 0) == -1);
    assert(hist_percentile(h, 5, 1001) == -1);
    assert(hist_percentile(NULL, 5, 500) == -1);

    /* hist_add + overflow clamp */
    uint32_t g[4] = { 0 };
    hist_add(g, 4, 100, 0);      /* bucket 0 */
    hist_add(g, 4, 100, 99);     /* bucket 0 */
    hist_add(g, 4, 100, 100);    /* bucket 1 */
    hist_add(g, 4, 100, 5000);   /* clamp -> 3 */
    hist_add(g, 4, 0, 5);        /* width 0 ignored */
    assert(g[0] == 2 && g[1] == 1 && g[2] == 0 && g[3] == 1);
    assert(hist_percentile(g, 4, 990) == 3);    /* p99 in overflow bucket */

    /* single sample: every percentile = that bucket */
    const uint32_t one[] = { 0, 0, 1 };
    assert(hist_percentile(one, 3, 1) == 2 && hist_percentile(one, 3, 1000) == 2);

    assert(log2_bucket(0) == 0 && log2_bucket(1) == 1 && log2_bucket(3) == 2 &&
           log2_bucket(4) == 3 && log2_bucket(UINT32_MAX) == 32);

    printf("I3 PASS\n");
    return 0;
}
