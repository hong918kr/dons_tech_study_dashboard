/* F3 - address-in-address fill + verify, then inverted pass (Barr "device test" flavor) */
#include <assert.h>
#include <stdio.h>
#include "fake_mem.h"

/* --- MODEL ANSWER --- */
#include <stdint.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */
#define MEM_BASE 0x80000000u            /* bus address of word offset 0 */

/* Each word holds its own bus address, then its inverse.
 * Returns the number of failing reads; *first_bad = word offset of the first one
 * (UINT32_MAX if none). 3N accesses: N writes, N read+writes, N reads. */
uint32_t own_addr_test(uint32_t nwords, uint32_t *first_bad)
{
    uint32_t errs = 0;
    *first_bad = UINT32_MAX;

    for (uint32_t i = 0; i < nwords; i++)            /* pass 1: fill with own address */
        mem_wr(i, MEM_BASE + 4u * i);

    for (int pass = 0; pass < 2; pass++) {           /* pass 2: verify + invert */
        for (uint32_t i = 0; i < nwords; i++) {      /* pass 3: verify inverse  */
            uint32_t exp = MEM_BASE + 4u * i;
            if (pass == 1) exp = ~exp;
            if (mem_rd(i) != exp) {
                if (errs++ == 0) *first_bad = i;
            }
            if (pass == 0) mem_wr(i, ~exp);
        }
    }
    return errs;
}
/* --- END MODEL ANSWER --- */

/* follow-up: board version, the data IS the pointer value (truncated to 32 bits) */
#include <stdint.h>
static uint32_t own_addr_ptr(volatile uint32_t *p, size_t nwords)
{
    uint32_t errs = 0;
    for (size_t i = 0; i < nwords; i++) p[i] = (uint32_t)(uintptr_t)&p[i];
    for (size_t i = 0; i < nwords; i++) if (p[i] != (uint32_t)(uintptr_t)&p[i]) errs++;
    return errs;
}

int main(void)
{
    uint32_t fb;
    static volatile uint32_t ram[64];
    assert(own_addr_ptr(ram, 64) == 0);

    fake_reset();                                       /* clean */
    assert(own_addr_test(256, &fb) == 0 && fb == UINT32_MAX);
    assert(F.wr == 512 && F.rd == 512);

    fake_reset();
    assert(own_addr_test(0, &fb) == 0 && fb == UINT32_MAX && F.wr == 0);  /* empty */

    /* Stuck-at-1 on bit 31 of word 5: own address already has bit 31 = 1,
     * so pass 2 (true data) PASSES -- only the inverted pass catches it. */
    fake_reset(); F.cell = 5; F.c_hi = 1u << 31;
    assert(own_addr_test(256, &fb) == 1 && fb == 5);

    /* Stuck-at-0 on bit 31 of word 5: caught in the true-data pass. */
    fake_reset(); F.cell = 5; F.c_lo = 1u << 31;
    assert(own_addr_test(256, &fb) == 1 && fb == 5);

    /* Stuck-at-1 on bit 2 of word 0 (address 0x80000000 has bit 2 = 0). */
    fake_reset(); F.cell = 0; F.c_hi = 1u << 2;
    assert(own_addr_test(256, &fb) == 1 && fb == 0);

    /* Address line A5 stuck-low in a 64-word region: words 32..63 alias 0..31.
     * every read of 0..31 fails in both passes, 32..63 fail in pass 2 only. */
    fake_reset(); F.a_lo = 1u << 5;
    assert(own_addr_test(64, &fb) == 96 && fb == 0);

    /* Address lines A2-A3 wired-OR: offsets with bit 2 or 3 set land on ...11.. */
    fake_reset(); F.a_or = (1u << 2) | (1u << 3);
    assert(own_addr_test(16, &fb) == 20 && fb == 4);   /* 4 groups x (3 + 2) */

    /* Data line D7 stuck-high everywhere: every word fails once (whichever pass has bit 7 = 0). */
    fake_reset(); F.d_hi = 1u << 7;
    assert(own_addr_test(256, &fb) == 256 && fb == 0);

    printf("F3 PASS\n");
    return 0;
}
