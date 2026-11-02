/* ==== answer ==== */
#include <stdint.h>

uint32_t bin_to_gray(uint32_t b)
{
    return b ^ (b >> 1);
}

uint32_t gray_to_bin(uint32_t g)                 /* b[i] = XOR of g[31..i] (prefix XOR) */
{
    g ^= g >> 16;
    g ^= g >> 8;
    g ^= g >> 4;
    g ^= g >> 2;
    g ^= g >> 1;
    return g;
}

uint32_t gray_to_bin_loop(uint32_t g)            /* paper version: O(width) */
{
    uint32_t b = 0;
    for (; g != 0; g >>= 1)
        b ^= g;
    return b;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

static unsigned pop(uint32_t x) { unsigned n = 0; while (x) { x &= x - 1; n++; } return n; }

int main(void)
{
    assert(bin_to_gray(0) == 0);
    assert(bin_to_gray(5) == 7);                         /* 101 -> 111 */
    assert(bin_to_gray(7) == 4);                         /* 111 -> 100 */
    assert(bin_to_gray(8) == 12);                        /* 1000 -> 1100 */
    assert(gray_to_bin(4) == 7 && gray_to_bin(12) == 8);
    assert(bin_to_gray(0xFFFFFFFFu) == 0x80000000u);
    assert(gray_to_bin(0x80000000u) == 0xFFFFFFFFu);
    for (uint32_t b = 0; b < (1u << 20); b++) {
        uint32_t g = bin_to_gray(b);
        assert(gray_to_bin(g) == b);
        assert(gray_to_bin_loop(g) == b);
        assert(pop(g ^ bin_to_gray(b + 1)) == 1);        /* neighbours differ by 1 bit */
    }
    for (uint32_t i = 0; i < 200000u; i++) {             /* high values */
        uint32_t b = 0xFFFFFFFFu - i * 7919u;
        assert(gray_to_bin(bin_to_gray(b)) == b);
        assert(gray_to_bin_loop(bin_to_gray(b)) == b);
    }
    /* 32-bit wrap: 0xFFFFFFFF -> 0 is also a 1-bit change */
    assert(pop(bin_to_gray(0xFFFFFFFFu) ^ bin_to_gray(0)) == 1);
    /* 4-bit counter wrap (FIFO pointer of depth 16): 15 -> 0 */
    assert(pop((bin_to_gray(15) ^ bin_to_gray(0)) & 0xFu) == 1);
    printf("B5 PASS\n");
    return 0;
}
