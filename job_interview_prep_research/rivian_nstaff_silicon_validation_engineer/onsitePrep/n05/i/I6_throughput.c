/* I6 - bytes + cycles + clock Hz -> MB/s (x100), integer only, no overflow.
 * Context: PCIe / DDR DMA copy timed by a 32-bit cycle-counter delta.
 * Build/run: ./run.sh
 */
//@@BEGIN I6
#include <stdint.h>

/* MB/s x100 (1 MB = 1e6 B) for `bytes` moved in `cycles` ticks of clk_hz.
 * Exact floor; returns UINT64_MAX on overflow; cycles == 0 -> 0. */
uint64_t mbps_x100(uint64_t bytes, uint32_t cycles, uint32_t clk_hz)
{
    if (cycles == 0)
        return 0;
    uint64_t q = bytes / cycles, r = bytes % cycles;
    /* bytes*clk/cycles == q*clk + r*clk/cycles ; r < 2^32 so r*clk < 2^64 */
    if (clk_hz != 0 && q > UINT64_MAX / clk_hz)
        return UINT64_MAX;
    uint64_t hi = q * clk_hz;
    uint64_t lo = r * clk_hz / cycles;
    if (hi > UINT64_MAX - lo)
        return UINT64_MAX;
    return (hi + lo) / 10000u;          /* B/s -> (MB/s) * 100 */
}
//@@END I6

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    /* 4 MiB in 1,000,000 cycles @ 1 GHz = 1 ms -> 4194.30 MB/s */
    assert(mbps_x100(4194304u, 1000000u, 1000000000u) == 419430u);

    /* 256 MiB DMA, 4,000,000 ticks of a 100 MHz timer (40 ms) */
    uint64_t v = mbps_x100(268435456u, 4000000u, 100000000u);
    printf("  I6 256MiB/40ms = %" PRIu64 ".%02" PRIu64 " MB/s\n", v / 100, v % 100);
    assert(v == 671088u);

    /* bytes < cycles: naive bytes/cycles*clk would give 0 */
    assert((100u / 1000u) * 1000000000u == 0);
    assert(mbps_x100(100u, 1000u, 1000000000u) == 10000u);   /* 100.00 MB/s */

    /* naive bytes*clk overflows here (2^40 * ~4.29e9 > 2^64), ours is exact */
    uint64_t big = (uint64_t)1 << 40;                          /* 1 TiB */
    uint64_t r = mbps_x100(big, UINT32_MAX, UINT32_MAX);       /* ~ bytes per second == bytes */
    assert(r == big / 10000u);

    /* saturation: huge bytes in 1 cycle at max clock */
    assert(mbps_x100(UINT64_MAX, 1u, UINT32_MAX) == UINT64_MAX);
    /* degenerate inputs */
    assert(mbps_x100(12345u, 0u, 1000000000u) == 0);
    assert(mbps_x100(0u, 1000u, 1000000000u) == 0);
    assert(mbps_x100(12345u, 1000u, 0u) == 0);

    printf("I6 PASS\n");
    return 0;
}
