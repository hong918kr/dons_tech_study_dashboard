#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Sign-extend the low `bits` bits (1..32) of x as an N-bit two's-complement value.
 * Bits above the field are ignored (masked off first). */
int32_t sign_extend32(uint32_t x, unsigned bits)
{
    if (bits == 0u || bits >= 32u)
        return (int32_t)x;           /* 32: reinterpret (impl-defined, 2's compl. everywhere) */
    uint32_t field = x & ((1u << bits) - 1u);
    uint32_t m = 1u << (bits - 1u);  /* sign bit of the field */
    /* (field ^ m) moves the range to 0..2^bits-1 with sign flipped; subtracting m
     * recenters it. Both operands fit in int32_t, so no overflow, no impl-defined cast. */
    return (int32_t)(field ^ m) - (int32_t)m;
}
/* END MODEL */

int main(void)
{
    /* 12-bit ADC */
    assert(sign_extend32(0x7FFu, 12) == 2047);
    assert(sign_extend32(0x800u, 12) == -2048);
    assert(sign_extend32(0xFFFu, 12) == -1);
    assert(sign_extend32(0x000u, 12) == 0);
    assert(sign_extend32(0x001u, 12) == 1);
    assert(sign_extend32(0xABCDEFFFu, 12) == -1);     /* garbage above field ignored */
    assert(sign_extend32(0xFF80u, 8) == -128);
    assert(sign_extend32(0x7Fu, 8) == 127);
    assert(sign_extend32(1u, 1) == -1);
    assert(sign_extend32(0u, 1) == 0);
    assert(sign_extend32(0x8000u, 16) == -32768);
    assert(sign_extend32(0x40000000u, 31) == -1073741824);
    assert(sign_extend32(0x3FFFFFFFu, 31) == 1073741823);
    assert(sign_extend32(0xFFFFFFFFu, 32) == -1);
    assert(sign_extend32(0x7FFFFFFFu, 32) == INT32_MAX);
    for (int v = -2048; v <= 2047; v++)                  /* round-trip all 12-bit values */
        assert(sign_extend32((uint32_t)v & 0xFFFu, 12) == v);
    puts("A6 PASS");
    return 0;
}
