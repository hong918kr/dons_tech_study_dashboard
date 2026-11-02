/* ==== answer ==== */
#include <stdint.h>

static uint32_t spread16(uint16_t v)             /* ...dcba -> .d.c.b.a (bit i -> bit 2i) */
{
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}

static uint16_t compact16(uint32_t x)            /* inverse: keep even bits, squeeze */
{
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0F0F0F0Fu;
    x = (x | (x >> 4)) & 0x00FF00FFu;
    x = (x | (x >> 8)) & 0x0000FFFFu;
    return (uint16_t)x;
}

uint32_t morton_encode(uint16_t x, uint16_t y)   /* x -> even bits, y -> odd bits */
{
    return spread16(x) | (spread16(y) << 1);
}

void morton_decode(uint32_t m, uint16_t *x, uint16_t *y)
{
    *x = compact16(m);
    *y = compact16(m >> 1);
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

static uint32_t naive(uint16_t x, uint16_t y)
{
    uint32_t m = 0;
    for (unsigned i = 0; i < 16; i++) {
        m |= (uint32_t)((x >> i) & 1u) << (2 * i);
        m |= (uint32_t)((y >> i) & 1u) << (2 * i + 1);
    }
    return m;
}

int main(void)
{
    assert(morton_encode(0, 0) == 0);
    assert(morton_encode(5, 3) == 27);                   /* 011011 */
    assert(morton_encode(1, 0) == 1 && morton_encode(0, 1) == 2 && morton_encode(1, 1) == 3);
    assert(morton_encode(0xFFFF, 0) == 0x55555555u);
    assert(morton_encode(0, 0xFFFF) == 0xAAAAAAAAu);
    assert(morton_encode(0xFFFF, 0xFFFF) == 0xFFFFFFFFu);
    assert(morton_encode(0x8000, 0) == 0x40000000u);
    for (uint32_t i = 0; i < 300000u; i++) {
        uint16_t x = (uint16_t)(i * 40503u), y = (uint16_t)((i >> 3) * 2654435761u >> 7);
        uint32_t m = morton_encode(x, y);
        assert(m == naive(x, y));
        uint16_t dx, dy;
        morton_decode(m, &dx, &dy);
        assert(dx == x && dy == y);
    }
    uint16_t dx, dy;
    morton_decode(27, &dx, &dy);
    assert(dx == 5 && dy == 3);
    printf("B8 PASS\n");
    return 0;
}
