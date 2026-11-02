/* F5 - CRC-8, poly 0x07 (x^8 + x^2 + x + 1), init 0, MSB-first, no reflect, xorout 0
 *      = CRC-8/SMBUS in the reveng catalogue (check "123456789" -> 0xF4, residue 0x00) */
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* --- MODEL ANSWER --- */
#include <stdint.h>
#include <stddef.h>

uint8_t crc8_07(const uint8_t *p, size_t n)
{
    uint8_t crc = 0x00;                         /* init */
    while (n--) {
        crc ^= *p++;                            /* byte enters at the MSB end */
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80u)                    /* top bit out = 1 -> subtract poly */
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;                                 /* xorout 0 */
}
/* --- END MODEL ANSWER --- */

/* follow-up: table-driven version (256-byte table, one lookup per byte) */
static uint8_t crc8_tab[256];
static void crc8_tab_init(void)
{
    for (unsigned i = 0; i < 256; i++) { uint8_t b = (uint8_t)i; crc8_tab[i] = crc8_07(&b, 1); }
}
static uint8_t crc8_fast(const uint8_t *p, size_t n)
{
    uint8_t crc = 0;
    while (n--) crc = crc8_tab[crc ^ *p++];
    return crc;
}

static void flip(uint8_t *m, unsigned bit) { m[bit / 8] ^= (uint8_t)(0x80u >> (bit % 8)); }

int main(void)
{
    assert(crc8_07((const uint8_t *)"123456789", 9) == 0xF4);   /* catalogue check value */
    assert(crc8_07(NULL, 0) == 0x00);                          /* empty */

    crc8_tab_init();
    assert(crc8_fast((const uint8_t *)"123456789", 9) == 0xF4);
    assert(crc8_tab[0x01] == 0x07 && crc8_tab[0x80] == 0x89);

    /* paper drills: one byte */
    const uint8_t one = 0x01, c2 = 0xC2, ff = 0xFF, d31 = 0x31;
    assert(crc8_07(&one, 1) == 0x07);           /* x^8 mod g = x^2+x+1 */
    assert(crc8_07(&c2, 1) == 0x40);
    assert(crc8_07(&ff, 1) == 0xF3);
    assert(crc8_07(&d31, 1) == 0x97);           /* '1' -- first step of the check string */

    /* residue: append the CRC, CRC of the whole frame is 0 */
    uint8_t fr[10];
    memcpy(fr, "123456789", 9);
    fr[9] = crc8_07(fr, 9);
    assert(crc8_07(fr, 10) == 0x00);

    /* every 1-, 2- and 3-bit error in a 4-byte data + 1-byte CRC frame is detected */
    uint8_t cw[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0};
    cw[4] = crc8_07(cw, 4);
    unsigned undetected = 0, tried = 0;
    for (unsigned i = 0; i < 40; i++)
        for (unsigned j = i; j < 40; j++)
            for (unsigned k = j; k < 40; k++) {
                if (j == i && k != i) continue;      /* weights: (i,i,i)=1 (i,j,j)=2 (i,j,k)=3 */
                uint8_t t[5];
                memcpy(t, cw, 5);
                flip(t, i);
                if (j != i) flip(t, j);
                if (k != j) flip(t, k);
                tried++;
                if (crc8_07(t, 5) == 0) undetected++;
            }
    assert(tried == 40 + 780 + 9880);           /* C(40,1) + C(40,2) + C(40,3) */
    assert(undetected == 0);

    /* HD limit: two flips 127 bits apart in a 16+1 byte frame go UNDETECTED
     * (x^127 = 1 mod g, because g = (x+1) * primitive degree-7 poly) */
    uint8_t lf[17] = {0};
    for (int i = 0; i < 16; i++) lf[i] = (uint8_t)(i * 37 + 1);
    lf[16] = crc8_07(lf, 16);
    for (unsigned i = 0; i < 136; i++)           /* all double flips in the 136-bit frame */
        for (unsigned j = i + 1; j < 136; j++) {
            uint8_t t[17];
            memcpy(t, lf, 17);
            flip(t, i);
            flip(t, j);
            assert((crc8_07(t, 17) == 0) == (j - i == 127));   /* missed iff 127 apart */
        }

    printf("F5 PASS\n");
    return 0;
}
