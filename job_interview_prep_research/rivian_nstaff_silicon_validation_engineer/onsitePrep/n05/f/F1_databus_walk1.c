/* F1 - data bus walking-1 test: report stuck / shorted data lines as bit masks */
#include <assert.h>
#include <stdio.h>
#include "fake_mem.h"

/* --- MODEL ANSWER --- */
#include <stdint.h>
#include <stdbool.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */

/* Walk a single 1 across all 32 data lines at ONE word offset.
 * *missing: lines that read 0 while driven 1   (stuck-low, wired-AND short)
 * *extra  : lines that read 1 while not driven (stuck-high, wired-OR short)
 * Returns true if the data bus looks clean. */
bool databus_walk1(uint32_t off, uint32_t *missing, uint32_t *extra)
{
    uint32_t miss = 0, ext = 0;

    for (unsigned i = 0; i < 32; i++) {
        uint32_t pat = 1u << i;          /* 1u: 1 << 31 on int is UB */
        mem_wr(off, pat);
        uint32_t r = mem_rd(off);
        miss |= pat & ~r;                /* driven, did not come back */
        ext  |= r & ~pat;                /* came back, was not driven */
    }
    *missing = miss;
    *extra   = ext;
    return (miss | ext) == 0;
}
/* --- END MODEL ANSWER --- */

int main(void)
{
    uint32_t m, e;

    fake_reset();                                   /* clean bus */
    assert(databus_walk1(0, &m, &e) && m == 0 && e == 0);
    assert(F.wr == 32 && F.rd == 32);               /* exactly 32 write/read pairs */

    fake_reset(); F.d_hi = 1u << 5;                 /* D5 stuck-high */
    assert(!databus_walk1(0, &m, &e) && m == 0 && e == (1u << 5));

    fake_reset(); F.d_lo = 1u << 31;                /* D31 stuck-low (top bit) */
    assert(!databus_walk1(7, &m, &e) && m == 0x80000000u && e == 0);

    fake_reset(); F.d_lo = 1u << 0;                 /* D0 stuck-low (bottom bit) */
    assert(!databus_walk1(7, &m, &e) && m == 1u && e == 0);

    fake_reset(); F.d_or = (1u << 3) | (1u << 4);   /* D3-D4 wired-OR short */
    assert(!databus_walk1(0, &m, &e) && m == 0 && e == 0x18u);

    fake_reset(); F.d_and = (1u << 3) | (1u << 4);  /* D3-D4 wired-AND short */
    assert(!databus_walk1(0, &m, &e) && m == 0x18u && e == 0);

    fake_reset(); F.a_lo = 1u << 4;                 /* ADDRESS fault: invisible here */
    assert(databus_walk1(0x10, &m, &e) && m == 0 && e == 0);

    fake_reset(); F.cell = 9; F.c_hi = 1u << 12;    /* one bad cell, other word tested */
    assert(databus_walk1(0, &m, &e));
    assert(!databus_walk1(9, &m, &e) && e == (1u << 12));

    printf("F1 PASS\n");
    return 0;
}
