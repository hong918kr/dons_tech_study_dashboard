#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* n is taken mod 32. (32 - n) & 31 keeps n == 0 from becoming x >> 32 (UB). */
uint32_t rotl32(uint32_t x, unsigned n)
{
    n &= 31u;
    return (x << n) | (x >> ((32u - n) & 31u));
}

uint32_t rotr32(uint32_t x, unsigned n)
{
    n &= 31u;
    return (x >> n) | (x << ((32u - n) & 31u));
}
/* END MODEL */

int main(void)
{
    assert(rotl32(0x12345678u, 4) == 0x23456781u);
    assert(rotr32(0x12345678u, 4) == 0x81234567u);
    assert(rotl32(0x80000001u, 1) == 0x00000003u);
    assert(rotr32(0x80000001u, 1) == 0xC0000000u);
    assert(rotl32(0x12345678u, 0) == 0x12345678u);
    assert(rotr32(0x12345678u, 0) == 0x12345678u);
    assert(rotl32(0x12345678u, 32) == 0x12345678u);
    assert(rotl32(0x12345678u, 36) == 0x23456781u);
    assert(rotr32(0x12345678u, 31) == rotl32(0x12345678u, 1));
    assert(rotl32(0u, 7) == 0u);
    assert(rotl32(0xFFFFFFFFu, 13) == 0xFFFFFFFFu);
    for (unsigned n = 0; n < 70; n++)
        assert(rotr32(rotl32(0xDEADBEEFu, n), n) == 0xDEADBEEFu);
    puts("A5 PASS");
    return 0;
}
