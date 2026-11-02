#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Swap bit 0<->1, 2<->3, ... : 0x55555555 selects even bits, 0xAAAAAAAA odd bits. */
uint32_t swap_odd_even(uint32_t x)
{
    return ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
}

/* Swap the two nibbles inside every byte: 0x12345678 -> 0x21436587. */
uint32_t swap_nibbles_in_bytes(uint32_t x)
{
    return ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
}

/* Single byte version. Cast back: b << 4 is promoted to int. */
uint8_t swap_nibbles8(uint8_t b)
{
    return (uint8_t)((b << 4) | (b >> 4));
}
/* END MODEL */

int main(void)
{
    assert(swap_odd_even(0xAAAAAAAAu) == 0x55555555u);
    assert(swap_odd_even(0x55555555u) == 0xAAAAAAAAu);
    assert(swap_odd_even(0x00000001u) == 0x00000002u);
    assert(swap_odd_even(0x80000000u) == 0x40000000u);
    assert(swap_odd_even(0x0000000Bu) == 0x00000007u);   /* 1011 -> 0111 */
    assert(swap_odd_even(0u) == 0u);
    assert(swap_odd_even(0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(swap_nibbles_in_bytes(0x12345678u) == 0x21436587u);
    assert(swap_nibbles_in_bytes(0xF00F0FF0u) == 0x0FF0F00Fu);
    assert(swap_nibbles_in_bytes(0u) == 0u);
    assert(swap_nibbles8(0xABu) == 0xBAu);
    assert(swap_nibbles8(0x0Fu) == 0xF0u);
    assert(swap_nibbles8(0x00u) == 0x00u);
    uint32_t s = 99u;
    for (int i = 0; i < 10000; i++) {
        s = s * 1664525u + 1013904223u;
        assert(swap_odd_even(swap_odd_even(s)) == s);
        assert(swap_nibbles_in_bytes(swap_nibbles_in_bytes(s)) == s);
    }
    puts("A7 PASS");
    return 0;
}
