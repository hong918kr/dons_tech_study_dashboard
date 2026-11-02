/* F6 - PRBS7 (x^7 + x^6 + 1) generator + self-synchronizing (feed-through) checker */
#include <assert.h>
#include <stdio.h>

/* --- MODEL ANSWER --- */
#include <stdint.h>

/* Fibonacci LFSR, state = last 7 bits, bit 0 newest. new = s[6] ^ s[5] */
unsigned prbs7_next(uint8_t *s)
{
    unsigned b = ((*s >> 6) ^ (*s >> 5)) & 1u;
    *s = (uint8_t)(((*s << 1) | b) & 0x7Fu);
    return b;
}

typedef struct {
    uint8_t  sr;        /* last 7 RECEIVED bits                */
    unsigned fill;      /* bits loaded so far (sync after 7)   */
    uint32_t errs;      /* mismatches after sync               */
    uint32_t checked;   /* bits compared after sync            */
} Prbs7Chk;

/* Self-sync checker: predict each bit from the previous 7 received bits,
 * then shift the RECEIVED bit in (not the predicted one). */
void prbs7_chk_bit(Prbs7Chk *c, unsigned rx)
{
    rx &= 1u;
    if (c->fill < 7) {
        c->fill++;                                   /* still loading */
    } else {
        unsigned exp = ((c->sr >> 6) ^ (c->sr >> 5)) & 1u;
        c->errs += (exp != rx);
        c->checked++;
    }
    c->sr = (uint8_t)(((c->sr << 1) | rx) & 0x7Fu);
}
/* --- END MODEL ANSWER --- */

#define NBITS 1000
static unsigned tx[NBITS];

static Prbs7Chk run(const unsigned *bits, int n)
{
    Prbs7Chk c = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) prbs7_chk_bit(&c, bits[i]);
    return c;
}

int main(void)
{
    /* period 127 and 64 ones from seed 0x7F; max runs 7 ones / 6 zeros (m-sequence) */
    uint8_t s = 0x7F;
    unsigned ones = 0, run1 = 0, run0 = 0, max1 = 0, max0 = 0, period = 0;
    do {
        unsigned b = prbs7_next(&s);
        period++;
        ones += b;
        if (b) { run1++; run0 = 0; if (run1 > max1) max1 = run1; }
        else   { run0++; run1 = 0; if (run0 > max0) max0 = run0; }
    } while (s != 0x7F);
    assert(period == 127 && ones == 64 && max1 == 7 && max0 == 6);

    /* cross-check against the Wikipedia PRBS7 example (start 0x02, period 127) */
    uint8_t a = 0x02, mine = 0x02;
    for (int i = 1; i <= 127; i++) {
        int newbit = (((a >> 6) ^ (a >> 5)) & 1);
        a = (uint8_t)(((a << 1) | newbit) & 0x7f);
        prbs7_next(&mine);
        assert(a == mine);
        if (i < 127) assert(a != 0x02);
    }
    assert(a == 0x02);

    /* first 16 bits from seed 0x7F, MSB-first = 0x02 0x0C (same as hmScreenPrep V6) */
    s = 0x7F;
    uint32_t w = 0;
    for (int i = 0; i < 16; i++) w = (w << 1) | prbs7_next(&s);
    assert(w == 0x020Cu);

    /* zero state is a lock-up: stays 0 forever */
    s = 0;
    for (int i = 0; i < 10; i++) assert(prbs7_next(&s) == 0 && s == 0);

    s = 0x7F;
    for (int i = 0; i < NBITS; i++) tx[i] = prbs7_next(&s);

    Prbs7Chk c = run(tx, NBITS);                     /* clean link */
    assert(c.errs == 0 && c.checked == NBITS - 7);

    /* checker syncs at ANY phase (starts mid-stream) */
    c = run(tx + 333, NBITS - 333);
    assert(c.errs == 0);

    /* one flipped bit is counted 3 times: at k, k+6, k+7 */
    tx[500] ^= 1u;
    c = run(tx, NBITS);
    assert(c.errs == 3);
    tx[500] ^= 1u;

    /* error inside the 7 sync bits: bit 2 shows at 2+6 and 2+7 only */
    tx[2] ^= 1u;
    c = run(tx, NBITS);
    assert(c.errs == 2);
    tx[2] ^= 1u;

    /* burst of 20 garbage bits, then clean: checker recovers by itself */
    for (int i = 600; i < 620; i++) tx[i] ^= 1u;
    c = run(tx, NBITS);
    assert(c.errs > 0 && c.errs <= 20 + 7);
    Prbs7Chk tail = run(tx + 620, NBITS - 620);      /* after burst: clean again */
    assert(tail.errs == 0);
    for (int i = 600; i < 620; i++) tx[i] ^= 1u;

    /* polarity inversion (P/N swapped): EVERY checked bit fails */
    static unsigned inv[NBITS];
    for (int i = 0; i < NBITS; i++) inv[i] = tx[i] ^ 1u;
    c = run(inv, NBITS);
    assert(c.errs == c.checked);

    /* TRAP: dead link (all zeros) passes a self-sync checker with 0 errors */
    static unsigned zero[NBITS];
    c = run(zero, NBITS);
    assert(c.errs == 0 && c.checked == NBITS - 7);
    /* guard: a real PRBS7 window has 64 ones per 127 bits -> count ones, demand ~half */
    unsigned ones_rx = 0;
    for (int i = 0; i < NBITS; i++) ones_rx += zero[i];
    assert(ones_rx < NBITS / 4);          /* -> report LOS / no-lock, not "0 errors" */
    unsigned ones_tx = 0;
    for (int i = 0; i < NBITS; i++) ones_tx += tx[i];
    assert(ones_tx > NBITS * 2 / 5 && ones_tx < NBITS * 3 / 5);

    printf("F6 PASS\n");
    return 0;
}
