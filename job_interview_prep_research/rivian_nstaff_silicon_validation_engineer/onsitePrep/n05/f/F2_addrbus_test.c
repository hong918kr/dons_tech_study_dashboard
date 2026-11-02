/* F2 - address bus test (Barr power-of-two offsets), report ALL suspect address lines */
#include <assert.h>
#include <stdio.h>
#include "fake_mem.h"

/* --- MODEL ANSWER --- */
#include <stdint.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */
#define PAT  0xAAAAAAAAu
#define ANTI 0x55555555u

/* nwords: power of two, region base aligned to nwords (offsets are word offsets).
 * Returns a mask of suspect address lines: bit k set = word-address line k. 0 = pass.
 * Run the data bus test first: a bad data line makes every line look bad here. */
uint32_t addrbus_test(uint32_t nwords)
{
    uint32_t bad = 0;

    for (uint32_t off = 1; off < nwords; off <<= 1)      /* 1) PAT at every 2^k */
        mem_wr(off, PAT);

    mem_wr(0, ANTI);                                     /* 2) stuck-high check */
    for (uint32_t off = 1; off < nwords; off <<= 1)
        if (mem_rd(off) != PAT) bad |= off;              /*    2^k aliases base */
    mem_wr(0, PAT);

    for (uint32_t t = 1; t < nwords; t <<= 1) {          /* 3) stuck-low / short */
        mem_wr(t, ANTI);
        if (mem_rd(0) != PAT) bad |= t;                  /*    t aliases base   */
        for (uint32_t off = 1; off < nwords; off <<= 1)
            if (off != t && mem_rd(off) != PAT)
                bad |= t | off;                          /*    t aliases off    */
        mem_wr(t, PAT);
    }
    return bad;
}
/* --- END MODEL ANSWER --- */

int main(void)
{
    fake_reset();                                  /* clean */
    assert(addrbus_test(256) == 0);
    /* access count is O(log N): 8 lines -> 1 + 8 writes, 8 + 8*(1+7) reads, + restores */
    assert(F.wr == 8 + 1 + 1 + 8 * 2 && F.rd == 8 + 8 * 8);

    fake_reset(); F.a_hi = 1u << 3;                /* A3 stuck-high */
    assert(addrbus_test(256) == (1u << 3));

    fake_reset(); F.a_lo = 1u << 3;                /* A3 stuck-low (offset 8 lands on 0) */
    assert(addrbus_test(256) == (1u << 3));

    fake_reset(); F.a_lo = 1u << 0;                /* A0 stuck-low: lowest line */
    assert(addrbus_test(256) == 1u);

    fake_reset(); F.a_hi = 1u << 7;                /* A7 stuck-high: top line of 256 */
    assert(addrbus_test(256) == (1u << 7));

    fake_reset(); F.a_or = (1u << 2) | (1u << 3);  /* A2-A3 wired-OR: 4 and 8 both -> 12 */
    assert(addrbus_test(256) == 0x0Cu);            /* only step 3 sees this one */

    fake_reset(); F.a_and = (1u << 5) | (1u << 6); /* A5-A6 wired-AND: 32 and 64 -> 0 */
    assert(addrbus_test(256) == 0x60u);

    fake_reset(); F.a_hi = 1u << 7;                /* fault outside tested range: */
    assert(addrbus_test(128) == 0);                /* 128 words only exercise A0..A6 */

    fake_reset(); F.d_lo = 1u << 1;                /* DATA fault poisons the address test */
    assert(addrbus_test(256) == 0xFFu);

    fake_reset();
    assert(addrbus_test(1) == 0 && F.wr == 2 && F.rd == 0);   /* 1 word: no address lines */

    printf("F2 PASS\n");
    return 0;
}
