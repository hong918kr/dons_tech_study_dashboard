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
 * Build & run:  ./run.sh V6
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
    /* TODO: pick tap/mask for order 7 or 31, load the seed, reject a zero state */
    (void)p; (void)order; (void)seed;
    return 0;
}

unsigned prbs_next_bit(Prbs *p)
{
    /* TODO: one LFSR step, return the new bit */
    (void)p;
    return 0;
}

void prbs_fill(Prbs *p, uint8_t *buf, size_t n)
{
    /* TODO: n bytes, first generated bit -> bit 7 of buf[0] */
    (void)p; (void)buf; (void)n;
}

uint64_t prbs_check(unsigned order, const uint8_t *rx, size_t n, uint64_t *bits_checked)
{
    /* TODO: seed from the first `order` bits, free-run, popcount(rx ^ expected) */
    (void)order; (void)rx; (void)n;
    if (bits_checked) *bits_checked = 0;
    return 0;
}

uint32_t crc32_ieee(const uint8_t *data, size_t n)
{
    /* TODO: reflected 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF */
    (void)data; (void)n;
    return 0;
}

double ber(uint64_t errors, uint64_t bits)
{
    /* TODO */
    (void)errors; (void)bits;
    return 0.0;
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
