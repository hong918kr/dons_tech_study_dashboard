/* F7 - Hamming(7,4): encode, decode with single-bit correction (syndrome = error position) */
#include <assert.h>
#include <stdio.h>

/* --- MODEL ANSWER --- */
#include <stdint.h>

/* Codeword bit (pos-1) holds position pos = 1..7:  p1 p2 d1 p4 d2 d3 d4
 * Parity positions 1, 2, 4; data d1 = bit 0 of the nibble. */
static unsigned pos_xor(uint8_t cw)          /* XOR of positions of all 1 bits */
{
    unsigned s = 0;
    for (unsigned pos = 1; pos <= 7; pos++)
        if ((cw >> (pos - 1)) & 1u) s ^= pos;
    return s;                                 /* 0 for every valid codeword */
}

uint8_t ham74_encode(uint8_t d)
{
    uint8_t cw = (uint8_t)(((d & 1u) << 2)            /* d1 -> pos 3      */
                         | (((d >> 1) & 7u) << 4));   /* d2..d4 -> pos 5..7 */
    unsigned s = pos_xor(cw);                         /* choose p1,p2,p4 so the */
    cw |= (uint8_t)((s & 3u) | ((s & 4u) << 1));      /* total XOR becomes 0    */
    return cw;
}

/* Returns 0 = clean, 1 = one bit corrected. *d = decoded nibble. */
int ham74_decode(uint8_t cw, uint8_t *d)
{
    cw &= 0x7Fu;
    unsigned s = pos_xor(cw);                         /* syndrome = bad position */
    if (s) cw ^= (uint8_t)(1u << (s - 1));
    *d = (uint8_t)(((cw >> 2) & 1u) | (((cw >> 4) & 7u) << 1));
    return s != 0;
}
/* --- END MODEL ANSWER --- */

/* follow-up: SECDED (8,4) = Hamming(7,4) + overall parity in bit 7.
 * returns 0 clean, 1 corrected single, -1 double error detected (uncorrectable) */
static int secded_decode(uint8_t cw8, uint8_t *d)
{
    unsigned s = pos_xor((uint8_t)(cw8 & 0x7Fu));
    unsigned odd = (unsigned)__builtin_parity(cw8);       /* 1 = odd number of flips */
    if (s && !odd) return -1;                              /* syndrome but parity OK: 2 bits */
    ham74_decode(cw8, d);                                  /* s == 0 && odd: bit 7 itself */
    return (s || odd) ? 1 : 0;
}
static uint8_t secded_encode(uint8_t d)
{
    uint8_t cw = ham74_encode(d);
    return (uint8_t)(cw | ((unsigned)__builtin_parity(cw) << 7));
}

int main(void)
{
    uint8_t d;
    for (unsigned v = 0; v < 16; v++) {
        uint8_t cw = secded_encode((uint8_t)v);
        assert(secded_decode(cw, &d) == 0 && d == v);
        for (unsigned i = 0; i < 8; i++) {
            assert(secded_decode((uint8_t)(cw ^ (1u << i)), &d) == 1 && d == v);
            for (unsigned j = i + 1; j < 8; j++)
                assert(secded_decode((uint8_t)(cw ^ (1u << i) ^ (1u << j)), &d) == -1);
        }
    }

    /* Wikipedia example: data 1011 (d1=1 d2=0 d3=1 d4=1) -> 0110011 (p1 p2 d1 p4 d2 d3 d4)
     * our nibble: d1 = bit0 -> 0b1101 = 0xD; codeword bits 0..6 = 0,1,1,0,0,1,1 -> 0x66 */
    assert(ham74_encode(0xD) == 0x66);
    assert(ham74_encode(0x0) == 0x00);
    assert(ham74_encode(0xF) == 0x7F);

    /* syndrome 5 example: flip position 5 of 0x66 */
    assert(ham74_decode(0x66 ^ (1u << 4), &d) == 1 && d == 0xD);

    /* exhaustive: every nibble, no error and every single-bit error decodes right */
    for (unsigned v = 0; v < 16; v++) {
        uint8_t cw = ham74_encode((uint8_t)v);
        assert(pos_xor(cw) == 0);
        assert(ham74_decode(cw, &d) == 0 && d == v);
        for (unsigned b = 0; b < 7; b++)
            assert(ham74_decode((uint8_t)(cw ^ (1u << b)), &d) == 1 && d == v);
    }

    /* minimum distance 3: any two distinct codewords differ in >= 3 bits */
    for (unsigned a = 0; a < 16; a++)
        for (unsigned b = a + 1; b < 16; b++)
            assert(__builtin_popcount(ham74_encode((uint8_t)a) ^ ham74_encode((uint8_t)b)) >= 3);

    /* double error is MIS-corrected every time (looks like a single error) */
    for (unsigned v = 0; v < 16; v++) {
        uint8_t cw = ham74_encode((uint8_t)v);
        for (unsigned i = 0; i < 7; i++)
            for (unsigned j = i + 1; j < 7; j++) {
                uint8_t bad = (uint8_t)(cw ^ (1u << i) ^ (1u << j));
                assert(ham74_decode(bad, &d) == 1 && d != v);
            }
    }

    /* bit 7 of the byte is ignored */
    assert(ham74_decode((uint8_t)(0x66 | 0x80), &d) == 0 && d == 0xD);

    printf("F7 PASS\n");
    return 0;
}
