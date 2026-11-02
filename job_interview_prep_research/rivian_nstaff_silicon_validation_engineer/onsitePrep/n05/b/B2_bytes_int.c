/* ==== answer ==== */
#include <stdint.h>
#include <stddef.h>

/* n = 1..8 bytes. Byte loads/stores only -> alignment-safe, host-endian independent. */
uint64_t load_le(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = n; i-- > 0; )                /* MSB is the LAST byte */
        v = (v << 8) | p[i];
    return v;
}

uint64_t load_be(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)               /* MSB is the FIRST byte */
        v = (v << 8) | p[i];
    return v;
}

void store_le(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = 0; i < n; i++, v >>= 8)
        p[i] = (uint8_t)v;
}

void store_be(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = n; i-- > 0; v >>= 8)
        p[i] = (uint8_t)v;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t buf[] = { 0xEE, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11 };
    /* unaligned offset 1 */
    assert(load_le(&buf[1], 4) == 0x78563412u);
    assert(load_be(&buf[1], 4) == 0x12345678u);
    assert(load_be(&buf[1], 3) == 0x123456u);          /* 24-bit ADC sample */
    assert(load_le(&buf[1], 3) == 0x563412u);
    assert(load_le(&buf[1], 1) == 0x12 && load_be(&buf[1], 1) == 0x12);
    assert(load_be(&buf[1], 8) == 0x123456789ABCDEF0ull);
    assert(load_le(&buf[1], 8) == 0xF0DEBC9A78563412ull);
    assert(load_le(buf, 0) == 0 && load_be(buf, 0) == 0);
    const uint8_t ff[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    assert(load_le(ff, 8) == ~0ull && load_be(ff, 8) == ~0ull);
    assert(load_be(ff, 4) == 0xFFFFFFFFu);              /* no sign extension */

    uint8_t out[11];
    memset(out, 0, sizeof out);
    store_be(&out[3], 0x123456u, 3);
    assert(out[3] == 0x12 && out[4] == 0x34 && out[5] == 0x56 && out[2] == 0 && out[6] == 0);
    store_le(&out[3], 0x123456u, 3);
    assert(out[3] == 0x56 && out[4] == 0x34 && out[5] == 0x12 && out[6] == 0);
    store_le(&out[1], 0xAABBCCDDu, 2);                  /* truncation: only low 2 bytes */
    assert(out[1] == 0xDD && out[2] == 0xCC);
    for (size_t n = 1; n <= 8; n++) {                   /* round-trip at odd offset */
        uint64_t mask = (n == 8) ? ~0ull : ((1ull << (8 * n)) - 1);
        uint64_t v = 0x0123456789ABCDEFull & mask;
        store_le(&out[1], v, n); assert(load_le(&out[1], n) == v);
        store_be(&out[1], v, n); assert(load_be(&out[1], n) == v);
    }
    printf("B2 PASS\n");
    return 0;
}
