#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Loop: pop the low bit of x, push it into r. 32 iterations, always. */
uint32_t rev_bits32_loop(uint32_t x)
{
    uint32_t r = 0;
    for (unsigned i = 0; i < 32u; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

/* Mask-swap: swap adjacent 1-, 2-, 4-, 8-, 16-bit groups. 5 steps, no branch. */
uint32_t rev_bits32_fast(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    x = (x >> 16) | (x << 16);
    return x;
}
/* END MODEL */

int main(void)
{
    assert(rev_bits32_loop(0u) == 0u);
    assert(rev_bits32_loop(1u) == 0x80000000u);
    assert(rev_bits32_loop(0x80000000u) == 1u);
    assert(rev_bits32_loop(0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(rev_bits32_loop(0x12345678u) == 0x1E6A2C48u);
    assert(rev_bits32_loop(0x0000000Fu) == 0xF0000000u);
    assert(rev_bits32_fast(0u) == 0u);
    assert(rev_bits32_fast(1u) == 0x80000000u);
    assert(rev_bits32_fast(0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(rev_bits32_fast(0x12345678u) == 0x1E6A2C48u);
    uint32_t s = 0xC0FFEEu;
    for (int i = 0; i < 100000; i++) {
        s = s * 1664525u + 1013904223u;          /* LCG */
        assert(rev_bits32_loop(s) == rev_bits32_fast(s));
        assert(rev_bits32_fast(rev_bits32_fast(s)) == s);   /* round-trip */
    }
    puts("A1 PASS");
    return 0;
}
