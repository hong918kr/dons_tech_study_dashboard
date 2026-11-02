#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* BEGIN MODEL */
/* Mask for bits [pos+width-1 : pos]. Valid: 1 <= width, pos + width <= 32.
 * Invalid -> 0 (callers then read 0 / leave reg unchanged). */
uint32_t field_mask(unsigned pos, unsigned width)
{
    if (width == 0u || pos >= 32u || width > 32u - pos)  /* no pos+width overflow */
        return 0u;
    uint32_t low = (width == 32u) ? 0xFFFFFFFFu        /* 1u << 32 is UB */
                                  : ((1u << width) - 1u);
    return low << pos;
}

uint32_t field_get(uint32_t reg, unsigned pos, unsigned width)
{
    uint32_t m = field_mask(pos, width);
    return m ? (reg & m) >> pos : 0u;
}

/* Clear the field, then OR the shifted value masked to the field (oversized val is cut). */
uint32_t field_set(uint32_t reg, unsigned pos, unsigned width, uint32_t val)
{
    uint32_t m = field_mask(pos, width);
    if (m == 0u)
        return reg;
    return (reg & ~m) | ((val << pos) & m);
}
/* END MODEL */

int main(void)
{
    assert(field_mask(4, 12) == 0x0000FFF0u);
    assert(field_mask(0, 32) == 0xFFFFFFFFu);
    assert(field_mask(31, 1) == 0x80000000u);
    assert(field_mask(0, 0) == 0u);
    assert(field_mask(1, 32) == 0u);
    assert(field_mask(32, 1) == 0u);
    assert(field_mask(0xFFFFFFFFu, 2) == 0u);
    assert(field_mask(4, 0xFFFFFFFFu) == 0u);
    assert(field_get(0xDEADBEEFu, 4, 12) == 0xBEEu);
    assert(field_get(0xDEADBEEFu, 28, 4) == 0xDu);
    assert(field_get(0xDEADBEEFu, 0, 32) == 0xDEADBEEFu);
    assert(field_get(0xDEADBEEFu, 31, 1) == 1u);
    assert(field_get(0xDEADBEEFu, 30, 4) == 0u);              /* invalid */
    assert(field_set(0xDEADBEEFu, 28, 4, 0x1u) == 0x1EADBEEFu);
    assert(field_set(0xDEADBEEFu, 4, 12, 0x123u) == 0xDEAD123Fu);
    assert(field_set(0xDEADBEEFu, 4, 12, 0xFFFF5u) == 0xDEADFF5Fu); /* val cut to 12 bits */
    assert(field_set(0xDEADBEEFu, 0, 32, 0x12345678u) == 0x12345678u);
    assert(field_set(0xDEADBEEFu, 0, 1, 0u) == 0xDEADBEEEu);
    assert(field_set(0xDEADBEEFu, 30, 4, 0u) == 0xDEADBEEFu);  /* invalid: unchanged */
    for (unsigned pos = 0; pos < 32; pos++)
        for (unsigned w = 1; pos + w <= 32; w++) {
            uint32_t r = field_set(0xA5A5A5A5u, pos, w, 0x3C3C3C3Cu);
            assert(field_get(r, pos, w) == (0x3C3C3C3Cu & field_mask(0, w)));
            assert((r & ~field_mask(pos, w)) == (0xA5A5A5A5u & ~field_mask(pos, w)));
        }
    puts("A8 PASS");
    return 0;
}
