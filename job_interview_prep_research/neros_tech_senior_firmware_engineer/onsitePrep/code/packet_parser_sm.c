/* Byte-at-a-time packet parser state machine (SYNC|LEN|PAYLOAD|CRC8): ISR-friendly follow-up to the screen's extract_packet. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYNC        0xAAu
#define MAX_PAYLOAD 32u

typedef enum { WAIT_SYNC, WAIT_LEN, IN_PAYLOAD, WAIT_CRC } pstate_t;

typedef struct {
    pstate_t state;
    uint8_t  len, idx, crc;
    uint8_t  payload[MAX_PAYLOAD];
    uint32_t frames_ok, crc_errors, len_errors;
} parser_t;

static uint8_t crc8_step(uint8_t crc, uint8_t b)          /* CRC-8/SMBUS, one byte (bitwise; table version in L10) */
{
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    return crc;
}

static void parser_init(parser_t *p) { memset(p, 0, sizeof *p); }

/* Feed one byte. Returns true exactly when a complete, CRC-valid frame has just finished (payload in p->payload, length p->len).
 * No buffering of the stream, O(1) per byte → can run in the UART RX ISR or on bytes popped from the ring buffer. */
static bool parser_feed(parser_t *p, uint8_t b)
{
    switch (p->state) {
    case WAIT_SYNC:
        if (b == SYNC)
            p->state = WAIT_LEN;
        return false;
    case WAIT_LEN:
        if (b > MAX_PAYLOAD) {                                /* impossible length → resync, never wait forever */
            p->len_errors++;
            p->state = (b == SYNC) ? WAIT_LEN : WAIT_SYNC;
            return false;
        }
        p->len = b;
        p->idx = 0;
        p->crc = crc8_step(0, b);                             /* CRC covers LEN + PAYLOAD */
        p->state = (b == 0) ? WAIT_CRC : IN_PAYLOAD;
        return false;
    case IN_PAYLOAD:
        p->payload[p->idx++] = b;
        p->crc = crc8_step(p->crc, b);
        if (p->idx == p->len)
            p->state = WAIT_CRC;
        return false;
    case WAIT_CRC:
        p->state = WAIT_SYNC;
        if (b != p->crc) {
            p->crc_errors++;
            return false;
        }
        p->frames_ok++;
        return true;
    }
    return false;
}

/* ---- tests ---- */
static size_t make_frame(uint8_t *f, const uint8_t *pl, uint8_t n)
{
    f[0] = SYNC;
    f[1] = n;
    memcpy(&f[2], pl, n);
    uint8_t c = crc8_step(0, n);
    for (uint8_t i = 0; i < n; i++)
        c = crc8_step(c, pl[i]);
    f[2 + n] = c;
    return (size_t)n + 3;
}

static int feed_all(parser_t *p, const uint8_t *s, size_t n)    /* returns frames completed */
{
    int got = 0;
    for (size_t i = 0; i < n; i++)
        got += parser_feed(p, s[i]);
    return got;
}

int main(void)
{
    parser_t p;
    uint8_t f[64], g[64];
    const uint8_t a[] = {1, 2, 3}, b[] = {SYNC, SYNC};            /* payload containing SYNC bytes */

    parser_init(&p);                                              /* one frame */
    assert(feed_all(&p, f, make_frame(f, a, 3)) == 1 && p.len == 3 && p.payload[2] == 3);

    parser_init(&p);                                              /* garbage, then frame with SYNC inside payload */
    const uint8_t junk[] = {0x00, 0x13, 0x37};
    feed_all(&p, junk, 3);
    assert(feed_all(&p, f, make_frame(f, b, 2)) == 1 && p.payload[0] == SYNC);

    parser_init(&p);                                              /* corrupted CRC, then good frame */
    size_t n = make_frame(f, a, 3);
    f[n - 1] ^= 0xFF;
    feed_all(&p, f, n);
    assert(p.crc_errors == 1 && feed_all(&p, g, make_frame(g, a, 3)) == 1);

    parser_init(&p);                                              /* impossible LEN → no deadlock */
    const uint8_t fake[] = {SYNC, 200};
    feed_all(&p, fake, 2);
    assert(p.len_errors == 1 && feed_all(&p, f, make_frame(f, a, 3)) == 1);

    parser_init(&p);                                              /* zero-length payload, back-to-back frames */
    size_t n1 = make_frame(f, a, 0), n2 = make_frame(g, a, 3);
    assert(feed_all(&p, f, n1) == 1 && feed_all(&p, g, n2) == 1 && p.frames_ok == 2);

    puts("packet_parser_sm: PASS");
    return 0;
}
