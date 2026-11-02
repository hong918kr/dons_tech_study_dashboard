#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
typedef struct {
    size_t   first_mismatch;   /* == n when buffers are equal */
    size_t   byte_errors;      /* bytes that differ */
    uint64_t bit_errors;       /* total flipped bits = sum popcount(a^b) */
} cmp_result_t;

static unsigned popcount8(uint8_t x)
{
    unsigned c = 0;
    while (x) {
        x &= (uint8_t)(x - 1);  /* clear lowest set bit */
        c++;
    }
    return c;
}

/* Returns true when the buffers are identical. */
bool buf_compare(const uint8_t *exp, const uint8_t *got, size_t n,
                 cmp_result_t *r)
{
    r->first_mismatch = n;
    r->byte_errors = 0;
    r->bit_errors = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t diff = exp[i] ^ got[i];
        if (diff == 0)
            continue;
        if (r->byte_errors == 0)
            r->first_mismatch = i;
        r->byte_errors++;
        r->bit_errors += popcount8(diff);
    }
    return r->byte_errors == 0;
}
/* ---- end model answer ---- */

int main(void)
{
    cmp_result_t r;
    uint8_t e[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t g[8];

    /* equal */
    memcpy(g, e, 8);
    assert(buf_compare(e, g, 8, &r));
    assert(r.first_mismatch == 8 && r.byte_errors == 0 && r.bit_errors == 0);

    /* page example: byte 2 has 1 flipped bit, byte 5 has 0x55^0xAA = 8 bits */
    g[2] = 0x23;
    g[5] = 0xAA;
    assert(!buf_compare(e, g, 8, &r));
    assert(r.first_mismatch == 2 && r.byte_errors == 2 && r.bit_errors == 9);

    /* first byte / last byte */
    memcpy(g, e, 8);
    g[0] = 0xFF;
    g[7] = 0x76;
    assert(!buf_compare(e, g, 8, &r));
    assert(r.first_mismatch == 0 && r.byte_errors == 2 && r.bit_errors == 9);

    /* n == 0 */
    assert(buf_compare(e, g, 0, &r));
    assert(r.first_mismatch == 0 && r.byte_errors == 0);

    /* all bits inverted */
    for (int i = 0; i < 8; i++)
        g[i] = (uint8_t)~e[i];
    assert(!buf_compare(e, g, 8, &r));
    assert(r.first_mismatch == 0 && r.byte_errors == 8 && r.bit_errors == 64);

    /* popcount8 table check */
    for (unsigned v = 0; v < 256; v++)
        assert(popcount8((uint8_t)v) == (unsigned)__builtin_popcount(v));

    puts("D2 PASS");
    return 0;
}
