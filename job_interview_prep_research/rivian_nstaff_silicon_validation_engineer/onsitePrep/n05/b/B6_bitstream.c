/* ==== answer ==== */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buf; size_t cap; size_t pos;        /* pos = bytes emitted */
    uint64_t acc; unsigned nacc;                 /* pending bits, nacc < 8 between calls */
} bitwriter_t;

bool bw_put(bitwriter_t *w, uint32_t val, unsigned nbits)        /* MSB-first, nbits 1..32 */
{
    if (nbits == 0 || nbits > 32)
        return false;
    if ((w->pos * 8 + w->nacc + nbits + 7) / 8 > w->cap)
        return false;                            /* would not fit even after flush */
    w->acc = (w->acc << nbits) | (val & (0xFFFFFFFFu >> (32 - nbits)));
    w->nacc += nbits;
    while (w->nacc >= 8) {
        w->nacc -= 8;
        w->buf[w->pos++] = (uint8_t)(w->acc >> w->nacc);
    }
    w->acc &= (1ull << w->nacc) - 1u;            /* keep only the pending bits */
    return true;
}

size_t bw_flush(bitwriter_t *w)                  /* zero-pad last byte, return total bytes */
{
    if (w->nacc) {
        w->buf[w->pos++] = (uint8_t)(w->acc << (8 - w->nacc));
        w->acc = 0;
        w->nacc = 0;
    }
    return w->pos;
}

typedef struct {
    const uint8_t *buf; size_t len; size_t pos;
    uint64_t acc; unsigned nacc;
} bitreader_t;

bool br_get(bitreader_t *r, unsigned nbits, uint32_t *out)       /* MSB-first, nbits 1..32 */
{
    if (nbits == 0 || nbits > 32 || (r->len - r->pos) * 8 + r->nacc < nbits)
        return false;
    while (r->nacc < nbits) {
        r->acc = (r->acc << 8) | r->buf[r->pos++];
        r->nacc += 8;
    }
    r->nacc -= nbits;
    *out = (uint32_t)(r->acc >> r->nacc) & (0xFFFFFFFFu >> (32 - nbits));
    r->acc &= (1ull << r->nacc) - 1u;
    return true;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t buf[16] = { 0 };
    bitwriter_t w = { .buf = buf, .cap = sizeof buf };
    assert(bw_put(&w, 0x5, 3));                          /* 101 */
    assert(bw_put(&w, 0x3, 2));                          /* 11 */
    assert(bw_put(&w, 0x1FF, 9));                        /* 111111111 */
    assert(bw_flush(&w) == 2);
    assert(buf[0] == 0xBF && buf[1] == 0xFC);

    bitreader_t r = { .buf = buf, .len = 2 };
    uint32_t v;
    assert(br_get(&r, 3, &v) && v == 0x5);
    assert(br_get(&r, 2, &v) && v == 0x3);
    assert(br_get(&r, 9, &v) && v == 0x1FF);
    assert(br_get(&r, 2, &v) && v == 0);                 /* padding */
    assert(!br_get(&r, 1, &v));                          /* exhausted */

    /* extra high bits in val are masked off */
    uint8_t b2[4] = { 0 };
    bitwriter_t w2 = { .buf = b2, .cap = sizeof b2 };
    assert(bw_put(&w2, 0xFFFFFFF0u, 4) && bw_flush(&w2) == 1 && b2[0] == 0x00);

    /* 32-bit field after a 1-bit field (crosses 5 bytes) */
    uint8_t b3[5] = { 0 };
    bitwriter_t w3 = { .buf = b3, .cap = sizeof b3 };
    assert(bw_put(&w3, 1, 1) && bw_put(&w3, 0xDEADBEEFu, 32));
    assert(!bw_put(&w3, 0xFF, 8));                       /* 41 bits > 40: no room */
    assert(bw_put(&w3, 0x7F, 7));                        /* exactly 40 bits */
    assert(bw_flush(&w3) == 5);
    bitreader_t r3 = { .buf = b3, .len = 5 };
    assert(br_get(&r3, 1, &v) && v == 1);
    assert(br_get(&r3, 32, &v) && v == 0xDEADBEEFu);
    assert(br_get(&r3, 7, &v) && v == 0x7F);
    /* bad widths */
    assert(!bw_put(&w3, 0, 0) && !bw_put(&w3, 0, 33));
    assert(!br_get(&r3, 0, &v) && !br_get(&r3, 33, &v));

    /* round-trip: many widths */
    uint8_t big[600] = { 0 };
    bitwriter_t wb = { .buf = big, .cap = sizeof big };
    for (unsigned i = 0; i < 128; i++)
        assert(bw_put(&wb, i * 0x9E3779B9u, 1 + (i % 32)));
    size_t n = bw_flush(&wb);
    bitreader_t rb = { .buf = big, .len = n };
    for (unsigned i = 0; i < 128; i++) {
        unsigned nb = 1 + (i % 32);
        assert(br_get(&rb, nb, &v));
        assert(v == ((i * 0x9E3779B9u) & (0xFFFFFFFFu >> (32 - nb))));
    }
    printf("B6 PASS\n");
    return 0;
}
