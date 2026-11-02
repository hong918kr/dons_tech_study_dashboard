#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Exactly one bit set. 0 is NOT a power of two. */
bool is_pow2(uint32_t x)
{
    return x != 0u && (x & (x - 1u)) == 0u;
}

/* Smallest power of two >= x. 0 -> 1. Returns 0 if x > 2^31 (no fit). */
uint32_t next_pow2(uint32_t x)
{
    if (x <= 1u)
        return 1u;
    if (x > 0x80000000u)
        return 0u;
    x--;                 /* so an exact power of two maps to itself */
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;        /* now every bit below the top 1 is set */
    return x + 1u;
}

/* a must be a power of two. align_up may wrap to 0 near UINT32_MAX. */
uint32_t align_down(uint32_t x, uint32_t a) { return x & ~(a - 1u); }
uint32_t align_up(uint32_t x, uint32_t a)   { return (x + a - 1u) & ~(a - 1u); }
/* END MODEL */

int main(void)
{
    assert(!is_pow2(0u));
    assert(is_pow2(1u));
    assert(is_pow2(64u));
    assert(!is_pow2(96u));
    assert(is_pow2(0x80000000u));
    assert(!is_pow2(0xFFFFFFFFu));
    assert(next_pow2(0u) == 1u);
    assert(next_pow2(1u) == 1u);
    assert(next_pow2(2u) == 2u);
    assert(next_pow2(5u) == 8u);
    assert(next_pow2(8u) == 8u);
    assert(next_pow2(1000u) == 1024u);
    assert(next_pow2(0x80000000u) == 0x80000000u);
    assert(next_pow2(0x80000001u) == 0u);
    assert(next_pow2(0xFFFFFFFFu) == 0u);
    assert(align_up(13u, 8u) == 16u);
    assert(align_up(16u, 8u) == 16u);
    assert(align_up(0u, 4096u) == 0u);
    assert(align_down(13u, 8u) == 8u);
    assert(align_down(0x40001FFFu, 0x1000u) == 0x40001000u);
    assert(align_up(0x40001001u, 0x1000u) == 0x40002000u);
    assert(align_up(5u, 1u) == 5u);
    assert(align_up(0xFFFFFFF9u, 8u) == 0u);      /* documented wrap */
    for (uint32_t x = 2; x < 70000u; x++) {
        uint32_t p = next_pow2(x);
        assert(is_pow2(p) && p >= x && (p >> 1) < x);
    }
    puts("A3 PASS");
    return 0;
}
