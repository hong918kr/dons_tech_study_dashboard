/* ==== answer ==== */
#include <stdint.h>
#include <stdbool.h>

static inline uint16_t bswap16(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));      /* promoted to int, cast truncates */
}

static inline uint32_t bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) |
           ((x << 8) & 0x00FF0000u) | (x << 24);
}

static inline uint64_t bswap64(uint64_t x)       /* swap bytes, then halves, then words */
{
    x = ((x & 0x00FF00FF00FF00FFull) << 8)  | ((x >> 8)  & 0x00FF00FF00FF00FFull);
    x = ((x & 0x0000FFFF0000FFFFull) << 16) | ((x >> 16) & 0x0000FFFF0000FFFFull);
    return (x << 32) | (x >> 32);
}

static bool host_is_little_endian(void)
{
    union { uint32_t u32; uint8_t b[4]; } probe = { .u32 = 0x01020304u };
    return probe.b[0] == 0x04;                   /* LSB at lowest address */
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(bswap16(0x1234) == 0x3412);
    assert(bswap16(0x00FF) == 0xFF00);
    assert(bswap16(0) == 0 && bswap16(0xFFFF) == 0xFFFF);
    assert(bswap32(0x12345678u) == 0x78563412u);
    assert(bswap32(0x000000FFu) == 0xFF000000u);
    assert(bswap32(0x80000001u) == 0x01000080u);
    assert(bswap64(0x0102030405060708ull) == 0x0807060504030201ull);
    assert(bswap64(0x1234567890abcdefull) == 0xefcdab9078563412ull);
    assert(bswap64(0xFFull) == 0xFF00000000000000ull);
    assert(bswap64(0) == 0 && bswap64(~0ull) == ~0ull);
    for (uint32_t i = 0; i < 100000u; i++) {      /* round-trip / involution */
        uint32_t v = i * 2654435761u;
        uint64_t w = ((uint64_t)v << 32) | (v ^ 0xA5A5A5A5u);
        assert(bswap32(bswap32(v)) == v);
        assert(bswap64(bswap64(w)) == w);
        assert(bswap16(bswap16((uint16_t)v)) == (uint16_t)v);
        /* bswap64 == two bswap32 swapped */
        assert(bswap64(w) == (((uint64_t)bswap32((uint32_t)w) << 32) | bswap32((uint32_t)(w >> 32))));
    }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    assert(host_is_little_endian());
#else
    assert(!host_is_little_endian());
#endif
    printf("host_is_little_endian = %d\n", host_is_little_endian());
    printf("B1 PASS\n");
    return 0;
}
