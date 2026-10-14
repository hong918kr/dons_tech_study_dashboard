/*
 * T2-6  Bit Ops on a Sensor Status Register
 *
 * A temperature-sensor IC exposes a 32-bit STATUS register:
 *   bit 0      OT_FAULT   over-temperature
 *   bit 1      UV_FAULT   under-voltage
 *   bit 2      CRC_ERR    last SPI frame had a bad CRC
 *   bit 3      DATA_RDY   new sample available
 *   bits 4..6  MODE       3-bit operating mode (0..7)
 *   bits 8..15 FIFO_LVL   8-bit FIFO fill level
 *   others     reserved (must be preserved on write)
 *
 * Implement:
 *   get_field(reg, pos, width)       -> value of bits [pos, pos+width) shifted down to bit 0
 *   set_field(reg, pos, width, val)  -> reg with those bits replaced by val (val is masked
 *                                       to width bits; all other bits unchanged)
 *   popcount32(x)                    -> number of 1 bits            (LeetCode 191)
 *   reverse_bits32(x)                -> bit 31..0 mirrored          (LeetCode 190)
 *   is_pow2(x)                       -> exactly one bit set          (LeetCode 231)
 *
 * Preconditions: 1 <= width <= 32 and pos + width <= 32.
 * All shifts must be free of undefined behavior (no shift by 32, no signed overflow).
 *
 * Reference solution.  Build & run:  ./run.sh T2-6 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* register map */
#define OT_FAULT     (1u << 0)
#define UV_FAULT     (1u << 1)
#define CRC_ERR      (1u << 2)
#define FAULT_MASK   (OT_FAULT | UV_FAULT | CRC_ERR)
#define MODE_POS     4u
#define MODE_W       3u
#define FIFO_POS     8u
#define FIFO_W       8u

/* width bits of ones; width == 32 handled separately because 1u << 32 is UB */
static uint32_t field_mask(unsigned width)
{
    return (width >= 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);
}

uint32_t get_field(uint32_t reg, unsigned pos, unsigned width)
{
    return (reg >> pos) & field_mask(width);      /* pos <= 31 here, so the shift is defined */
}

uint32_t set_field(uint32_t reg, unsigned pos, unsigned width, uint32_t val)
{
    uint32_t m = field_mask(width) << pos;        /* mask in place */
    return (reg & ~m) | ((val << pos) & m);       /* clear, then OR the masked new value */
}

unsigned popcount32(uint32_t x)
{
    unsigned c = 0;
    while (x) { x &= x - 1u; c++; }               /* Kernighan: drop the lowest set bit */
    return c;
}

uint32_t reverse_bits32(uint32_t x)
{
    x = (x >> 16) | (x << 16);                                      /* swap halves   */
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);        /* swap bytes    */
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);        /* swap nibbles  */
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);        /* swap pairs    */
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);        /* swap bits     */
    return x;
}

bool is_pow2(uint32_t x)
{
    return x != 0u && (x & (x - 1u)) == 0u;
}

int main(void)
{
    /* 1. basic: decode the example status register 0x00002A35 */
    uint32_t st = 0x00002A35u;
    CHECK(get_field(st, MODE_POS, MODE_W) == 3u);
    CHECK(get_field(st, FIFO_POS, FIFO_W) == 42u);
    CHECK(popcount32(st & FAULT_MASK) == 2u);               /* OT_FAULT + CRC_ERR */
    CHECK(set_field(st, MODE_POS, MODE_W, 6u) == 0x00002A65u);

    /* 2. set_field masks an oversized value and preserves every other bit */
    CHECK(set_field(st, MODE_POS, MODE_W, 9u) == 0x00002A15u);   /* 9 = 0b1001 -> 0b001 */
    CHECK(set_field(0xFFFFFFFFu, MODE_POS, MODE_W, 0u) == 0xFFFFFF8Fu);

    /* 3. full-width field at pos 0 (the 1u << 32 trap) */
    CHECK(get_field(0xDEADBEEFu, 0, 32) == 0xDEADBEEFu);
    CHECK(set_field(0x12345678u, 0, 32, 0xCAFEF00Du) == 0xCAFEF00Du);

    /* 4. top bit (pos 31, width 1) */
    CHECK(get_field(0x80000000u, 31, 1) == 1u);
    CHECK(set_field(0u, 31, 1, 1u) == 0x80000000u);

    /* 5. popcount edges */
    CHECK(popcount32(0u) == 0u);
    CHECK(popcount32(0xFFFFFFFFu) == 32u);
    CHECK(popcount32(st) == 7u);

    /* 6. reverse bits (LeetCode 190 example) and is_pow2 edges */
    CHECK(reverse_bits32(0x02941E9Cu) == 0x39782940u);
    CHECK(reverse_bits32(1u) == 0x80000000u);
    CHECK(reverse_bits32(0u) == 0u);
    CHECK(!is_pow2(0u) && is_pow2(1u) && is_pow2(0x80000000u) && !is_pow2(6u) && !is_pow2(0xFFFFFFFFu));

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
