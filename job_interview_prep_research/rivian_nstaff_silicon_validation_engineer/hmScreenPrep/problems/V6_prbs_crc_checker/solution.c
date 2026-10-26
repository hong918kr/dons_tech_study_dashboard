/*
 * V6  PRBS Generator/Checker + CRC-32 for Link / Loopback Validation
 *
 * Implement the pieces of a bit-error-rate test (BERT) that runs over a loopback:
 *
 *   prbs_init(p, order, seed)    order 7  -> PRBS7  = x^7  + x^6  + 1
 *                                order 31 -> PRBS31 = x^31 + x^28 + 1
 *                                seed is masked to `order` bits; 0 (after masking) or any
 *                                other order -> -EINVAL. Returns 0.
 *   prbs_next_bit(p)             Fibonacci LFSR, state bit 0 = newest bit:
 *                                  new   = state[order-1] XOR state[tap-1]   (tap = 6 or 28)
 *                                  state = ((state << 1) | new) & mask
 *                                returns `new` (0/1). This is the ITU-T O.150 style PRBS.
 *   prbs_fill(p, buf, n)         n bytes, MSB-FIRST: the first generated bit is bit 7 of buf[0].
 *   prbs_check(order, rx, n, &bits_checked)
 *                                Self-synchronizing checker: load the first `order` received
 *                                bits into the state (they are the seed, assumed error-free),
 *                                then free-run a local generator and compare every remaining
 *                                bit. Count errors as popcount(rx XOR expected).
 *                                *bits_checked = 8n - order (0 if the buffer is too short).
 *                                Returns the number of bit errors.
 *   crc32_ieee(data, n)          CRC-32 (IEEE 802.3): reflected poly 0xEDB88320,
 *                                init 0xFFFFFFFF, final XOR 0xFFFFFFFF. "123456789" -> 0xCBF43926.
 *   ber(errors, bits)            errors / bits as a double; bits == 0 -> -1.0 (undefined).
 *
 * No malloc. data may be NULL when n == 0.
 *
 * Reference solution.  Build & run:  ./run.sh V6 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct {
    uint32_t state;    /* last `order` bits, bit 0 = newest */
    uint32_t mask;     /* (1 << order) - 1 */
    uint8_t  order;    /* 7 or 31 */
    uint8_t  tap;      /* 6 or 28 (the middle term of the polynomial) */
} Prbs;

int prbs_init(Prbs *p, unsigned order, uint32_t seed)
{
    if (p == NULL) return -EINVAL;
    if (order == 7)       p->tap = 6;
    else if (order == 31) p->tap = 28;
    else return -EINVAL;
    p->order = (uint8_t)order;
    p->mask  = (1u << order) - 1u;           /* order <= 31: no 32-bit shift UB */
    p->state = seed & p->mask;
    return p->state ? 0 : -EINVAL;           /* all-zero state locks up forever */
}

unsigned prbs_next_bit(Prbs *p)
{
    uint32_t nb = ((p->state >> (p->order - 1u)) ^ (p->state >> (p->tap - 1u))) & 1u;
    p->state = ((p->state << 1) | nb) & p->mask;
    return (unsigned)nb;
}

void prbs_fill(Prbs *p, uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = 0;
        for (int j = 0; j < 8; j++) b = (uint8_t)((b << 1) | prbs_next_bit(p));   /* MSB first */
        buf[i] = b;
    }
}

static unsigned popcount8(uint8_t x)
{
    unsigned c = 0;
    while (x) { x &= (uint8_t)(x - 1u); c++; }   /* clear lowest set bit */
    return c;
}

static unsigned rx_bit(const uint8_t *rx, size_t i)
{
    return (rx[i >> 3] >> (7u - (i & 7u))) & 1u;
}

uint64_t prbs_check(unsigned order, const uint8_t *rx, size_t n, uint64_t *bits_checked)
{
    if (bits_checked) *bits_checked = 0;
    if ((order != 7 && order != 31) || rx == NULL) return 0;
    size_t nbits = n * 8u;
    if (nbits <= order) return 0;

    /* 1) seed: shift the first `order` received bits into the state */
    Prbs p;
    uint32_t seed = 0;
    for (size_t i = 0; i < order; i++) seed = (seed << 1) | rx_bit(rx, i);
    if (prbs_init(&p, order, seed) != 0) p.state = 0;   /* all-zero seed: every '1' counts */

    /* 2) free-run the local generator; expected byte vs received byte, popcount the XOR */
    uint64_t errors = 0;
    for (size_t b = order / 8u; b < n; b++) {
        uint8_t exp = 0;
        for (unsigned j = 0; j < 8; j++) {
            size_t i = b * 8u + j;
            unsigned e = (i < order) ? rx_bit(rx, i) : prbs_next_bit(&p);   /* seed bits: no compare */
            exp = (uint8_t)((exp << 1) | e);
        }
        errors += popcount8((uint8_t)(exp ^ rx[b]));
    }
    if (bits_checked) *bits_checked = nbits - order;
    return errors;
}

uint32_t crc32_ieee(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));   /* branch-free */
    }
    return crc ^ 0xFFFFFFFFu;
}

double ber(uint64_t errors, uint64_t bits)
{
    return bits ? (double)errors / (double)bits : -1.0;
}

/* =====================================================================
 * Tests (do not edit)
 * ===================================================================== */
static void flip(uint8_t *buf, size_t bit) { buf[bit >> 3] ^= (uint8_t)(0x80u >> (bit & 7u)); }
static unsigned tbit(const uint8_t *buf, size_t bit) { return (buf[bit >> 3] >> (7u - (bit & 7u))) & 1u; }

int main(void)
{
    Prbs p = { 0, 0, 0, 0 };
    uint8_t buf[256] = { 0 }, ref[256] = { 0 };
    uint64_t chk = 0;

    /* 1. PRBS7 from seed 0x7F: period 127, 64 ones per period, known first bytes */
    CHECK(prbs_init(&p, 7, 0x7F) == 0);
    unsigned period = 0, ones = 0;
    do { ones += prbs_next_bit(&p); period++; } while (p.state != 0x7F && period < 1000);
    CHECK(period == 127 && ones == 64);
    prbs_init(&p, 7, 0x7F);
    prbs_fill(&p, buf, 4);
    CHECK(buf[0] == 0x02 && buf[1] == 0x0C && buf[2] == 0x28 && buf[3] == 0xF2);
    prbs_init(&p, 7, 0x7F);
    prbs_fill(&p, buf, 32);
    bool periodic = true;
    for (size_t i = 0; i + 127 < 256; i++) periodic &= (tbit(buf, i) == tbit(buf, i + 127));
    CHECK(periodic);

    /* 2. clean loopback: 0 errors, bits_checked = 8n - order */
    prbs_init(&p, 7, 0x7F);  prbs_fill(&p, ref, 256);
    CHECK(prbs_check(7, ref, 256, &chk) == 0 && chk == 2041);
    prbs_init(&p, 31, 0x12345678); prbs_fill(&p, buf, 256);
    CHECK(prbs_check(31, buf, 256, &chk) == 0 && chk == 2017);
    /*    checker locks from ANY phase: start in the middle of the stream */
    CHECK(prbs_check(7, ref + 37, 100, &chk) == 0 && chk == 793);

    /* 3. injected errors are counted exactly (single, 8-bit burst, scattered) */
    memcpy(buf, ref, sizeof buf);
    flip(buf, 100);
    CHECK(prbs_check(7, buf, 256, &chk) == 1);
    for (size_t k = 0; k < 8; k++) flip(buf, 320 + k);      /* whole byte 40 */
    CHECK(prbs_check(7, buf, 256, &chk) == 9);
    /*    an error inside the 7 seed bits locks the checker to the wrong phase (~50% errors) */
    memcpy(buf, ref, sizeof buf);
    flip(buf, 3);
    CHECK(prbs_check(7, buf, 256, &chk) == 1027);
    prbs_init(&p, 31, 0xACE1u); prbs_fill(&p, buf, 256);
    const size_t at[5] = { 31, 500, 501, 1234, 2047 };       /* first checked bit .. last bit */
    for (size_t k = 0; k < 5; k++) flip(buf, at[k]);
    CHECK(prbs_check(31, buf, 256, &chk) == 5 && chk == 2017);

    /* 4. CRC-32: check value, empty buffer, residue of message || CRC (little-endian) */
    const uint8_t msg[13] = { '1','2','3','4','5','6','7','8','9' };
    uint32_t c = crc32_ieee(msg, 9);
    CHECK(c == 0xCBF43926u);
    CHECK(crc32_ieee(NULL, 0) == 0x00000000u);
    uint8_t frame[13];
    memcpy(frame, msg, 9);
    for (int k = 0; k < 4; k++) frame[9 + k] = (uint8_t)(c >> (8 * k));
    CHECK(crc32_ieee(frame, 13) == 0x2144DF1Cu);
    frame[3] ^= 0x10;
    CHECK(crc32_ieee(frame, 13) != 0x2144DF1Cu);

    /* 5. BER */
    CHECK(ber(9, 2041) > 4.40e-3 && ber(9, 2041) < 4.41e-3);
    CHECK(ber(0, 1000) == 0.0);
    CHECK(ber(3, 0) == -1.0);

    /* 6. edge cases: empty / too short buffer, bad init */
    CHECK(prbs_check(7, buf, 0, &chk) == 0 && chk == 0);
    CHECK(prbs_check(31, buf, 3, &chk) == 0 && chk == 0);     /* 24 bits < 31-bit seed */
    CHECK(prbs_check(7, buf, 1, &chk) == 0 && chk == 1);      /* 8 bits: 1 compared */
    CHECK(prbs_init(&p, 9, 1) == -EINVAL);
    CHECK(prbs_init(&p, 7, 0x80) == -EINVAL);                /* masks to 0 */
    memset(buf, 0xEE, 8);
    prbs_init(&p, 7, 1); prbs_fill(&p, buf, 0);
    CHECK(buf[0] == 0xEE);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
