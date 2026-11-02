#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* 1) Naive: test every bit. Stops after the top set bit (<= 32 iterations). */
unsigned popcnt_naive(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        n += x & 1u;
        x >>= 1;
    }
    return n;
}

/* 2) Kernighan: x & (x-1) clears the lowest set bit. Iterations = number of 1s. */
unsigned popcnt_kernighan(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x - 1u;
        n++;
    }
    return n;
}

/* 3) SWAR: add bits in parallel inside 2-, 4-, 8-bit lanes, then sum bytes. */
unsigned popcnt_swar(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);                  /* 2-bit counts 0..2 */
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);  /* 4-bit counts 0..4 */
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;                  /* 8-bit counts 0..8 */
    return (x * 0x01010101u) >> 24;                    /* top byte = sum    */
}

/* Parity: 1 if odd number of 1s. XOR-fold halves down to bit 0. */
unsigned parity32(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}
/* END MODEL */

int main(void)
{
    const uint32_t v[] = {0u, 1u, 0x80000000u, 0xFFFFFFFFu, 0xF0F0u, 0x12345678u, 0x7u};
    const unsigned c[] = {0, 1, 1, 32, 8, 13, 3};
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        assert(popcnt_naive(v[i]) == c[i]);
        assert(popcnt_kernighan(v[i]) == c[i]);
        assert(popcnt_swar(v[i]) == c[i]);
        assert(parity32(v[i]) == (c[i] & 1u));
    }
    uint32_t s = 12345u;
    for (int i = 0; i < 100000; i++) {
        s = s * 1664525u + 1013904223u;
        unsigned ref = (unsigned)__builtin_popcount(s);
        assert(popcnt_naive(s) == ref);
        assert(popcnt_kernighan(s) == ref);
        assert(popcnt_swar(s) == ref);
        assert(parity32(s) == (ref & 1u));
    }
    puts("A2 PASS");
    return 0;
}
