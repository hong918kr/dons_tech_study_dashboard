/* F4 - three checksums: 8-bit sum, two's-complement checksum, Fletcher-16 */
#include <assert.h>
#include <stdio.h>

/* --- MODEL ANSWER --- */
#include <stdint.h>
#include <stddef.h>

uint8_t sum8(const uint8_t *p, size_t n)            /* sum mod 256 */
{
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *p++);            /* wraps mod 256 by design */
    return s;
}

uint8_t cksum8_twos(const uint8_t *p, size_t n)     /* byte that makes the total 0 */
{
    return (uint8_t)(0u - sum8(p, n));              /* = ~sum + 1 */
}

uint16_t fletcher16(const uint8_t *p, size_t n)
{
    uint16_t s1 = 0, s2 = 0;                        /* both stay < 255 */
    while (n--) {
        s1 = (uint16_t)((s1 + *p++) % 255u);        /* data sum     */
        s2 = (uint16_t)((s2 + s1) % 255u);          /* sum of sums: position-weighted */
    }
    return (uint16_t)((s2 << 8) | s1);
}
/* --- END MODEL ANSWER --- */

int main(void)
{
    const uint8_t s9[] = "123456789";
    assert(sum8(s9, 9) == 0xDD);                       /* 0x31+..+0x39 = 477 = 0x1DD */
    assert(cksum8_twos(s9, 9) == 0x23);
    assert((uint8_t)(sum8(s9, 9) + cksum8_twos(s9, 9)) == 0);   /* receiver: total == 0 */

    /* Intel HEX record :0300300002337A1E -> checksum 0x1E (Wikipedia example) */
    const uint8_t hex[] = {0x03, 0x00, 0x30, 0x00, 0x02, 0x33, 0x7A};
    assert(cksum8_twos(hex, sizeof hex) == 0x1E);
    const uint8_t hexfull[] = {0x03, 0x00, 0x30, 0x00, 0x02, 0x33, 0x7A, 0x1E};
    assert(sum8(hexfull, sizeof hexfull) == 0);

    /* empty input */
    assert(sum8(NULL, 0) == 0 && cksum8_twos(NULL, 0) == 0 && fletcher16(NULL, 0) == 0);

    /* Fletcher-16 public test vectors (Wikipedia) */
    assert(fletcher16((const uint8_t *)"abcde", 5) == 0xC8F0);
    assert(fletcher16((const uint8_t *)"abcdef", 6) == 0x2057);
    assert(fletcher16((const uint8_t *)"abcdefgh", 8) == 0x0627);

    /* sum8 is blind to byte order; Fletcher is not */
    const uint8_t a[] = {0x12, 0x34}, b[] = {0x34, 0x12};
    assert(sum8(a, 2) == sum8(b, 2));
    assert(fletcher16(a, 2) != fletcher16(b, 2));

    /* mod-255 blind spot: 0x00 and 0xFF are the same value mod 255 */
    const uint8_t z[] = {0x00, 0x00}, f[] = {0xFF, 0xFF};
    assert(fletcher16(z, 2) == fletcher16(f, 2));

    /* no overflow on long input (s1, s2 kept < 255 every step) */
    static uint8_t big[100000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = 0xFE;
    uint16_t fb = fletcher16(big, sizeof big);
    assert((fb & 0xFF) < 255 && (fb >> 8) < 255);

    printf("F4 PASS\n");
    return 0;
}
