/* ==== answer ==== */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* bit = 1 means allocated. nbits need not be a multiple of 32. */
static unsigned ctz32(uint32_t x)                /* precondition: x != 0 */
{
    unsigned n = 0;
    while ((x & 1u) == 0) {
        x >>= 1;
        n++;
    }
    return n;
}

int bm_alloc(uint32_t *map, size_t nbits)
{
    size_t nwords = (nbits + 31) / 32;
    for (size_t w = 0; w < nwords; w++) {
        uint32_t free_bits = ~map[w];
        if (free_bits == 0)
            continue;                            /* word full: skip 32 at once */
        size_t idx = w * 32 + ctz32(free_bits);
        if (idx >= nbits)
            return -1;                           /* only tail padding was free */
        map[w] |= 1u << (idx % 32);
        return (int)idx;
    }
    return -1;
}

bool bm_free(uint32_t *map, size_t nbits, size_t idx)
{
    if (idx >= nbits)
        return false;
    uint32_t bit = 1u << (idx % 32);
    if ((map[idx / 32] & bit) == 0)
        return false;                            /* double free */
    map[idx / 32] &= ~bit;
    return true;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint32_t map[2] = { 0xFFFFFFFFu, 0x00000007u };
    assert(bm_alloc(map, 40) == 35);                     /* example */
    assert(map[1] == 0x0000000Fu);

    /* fill everything, nbits = 40 (not a multiple of 32) */
    uint32_t m2[2] = { 0, 0 };
    for (int i = 0; i < 40; i++)
        assert(bm_alloc(m2, 40) == i);                   /* lowest-first */
    assert(bm_alloc(m2, 40) == -1);                      /* full: tail bits 40..63 not handed out */
    assert(m2[1] == 0x000000FFu);

    /* free + reuse lowest */
    assert(bm_free(m2, 40, 17));
    assert(bm_free(m2, 40, 3));
    assert(bm_alloc(m2, 40) == 3);
    assert(bm_alloc(m2, 40) == 17);
    /* errors */
    assert(!bm_free(m2, 40, 40));                        /* out of range */
    assert(bm_free(m2, 40, 39));
    assert(!bm_free(m2, 40, 39));                        /* double free */
    assert(bm_alloc(m2, 40) == 39);
    /* exactly 32 bits, bit 31 */
    uint32_t m3[1] = { 0x7FFFFFFFu };
    assert(bm_alloc(m3, 32) == 31 && m3[0] == 0xFFFFFFFFu);
    assert(bm_alloc(m3, 32) == -1);
    /* nbits = 0 */
    uint32_t m4[1] = { 0 };
    assert(bm_alloc(m4, 0) == -1);
    printf("B4 PASS\n");
    return 0;
}
