/*
 * T2-2  UART Temperature Packet Parser
 *
 * A temperature board streams packets to the robot's main MCU over UART. The UART RX
 * interrupt hands you ONE byte at a time. Write a parser that emits each complete,
 * valid packet and silently resynchronizes after noise or corrupted packets.
 *
 * Frame format (agreed with the interviewer):
 *   [0xAA SYNC] [LEN] [PAYLOAD: LEN bytes] [CHK]
 *   - 1 <= LEN <= MAX_PAYLOAD. Any other LEN is a bad frame.
 *   - CHK = XOR of LEN and every payload byte (SYNC is not included).
 *   - Payload of a temperature packet: sensor_id, then int16 temps (0.1 degC) little-endian.
 *     The parser itself does not care: it emits the raw payload.
 *   - Bad LEN or bad CHK: count it in bad_frames and go back to hunting for SYNC.
 *     If the bad LEN byte is itself 0xAA, treat it as a new SYNC.
 *   - 0xAA may appear inside the payload or as the CHK byte (length-based framing).
 *   - parser_feed returns true exactly when the byte completes a valid packet,
 *     and only then writes *out. No malloc.
 *
 * Build & run:  ./run.sh T2-2
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define SYNC_BYTE   0xAAu
#define MAX_PAYLOAD 16u

typedef enum { ST_SYNC, ST_LEN, ST_PAYLOAD, ST_CHK } ParseState;

typedef struct { uint8_t len; uint8_t data[MAX_PAYLOAD]; } Packet;

typedef struct {
    ParseState st;
    uint8_t len, idx, chk;               /* expected length, bytes received, running XOR */
    uint8_t data[MAX_PAYLOAD];
    uint32_t bad_frames;                 /* diagnostics: bad LEN or bad CHK */
} Parser;

void parser_init(Parser *p)
{
    /* TODO: your code here */
    (void)p;
}

bool parser_feed(Parser *p, uint8_t b, Packet *out)
{
    /* TODO: your code here */
    (void)p; (void)b; (void)out;
    return false;
}

/* ---- test helpers (not part of the answer) ---- */
static size_t feed_all(Parser *p, const uint8_t *s, size_t n, Packet *out, size_t cap)
{
    size_t got = 0;
    for (size_t i = 0; i < n; i++) {
        Packet pk;
        if (parser_feed(p, s[i], &pk)) { if (got < cap) out[got] = pk; got++; }
    }
    return got;
}
static int16_t temp_at(const Packet *pk, size_t k)     /* k-th temp after sensor_id */
{
    return (int16_t)(pk->data[1 + 2 * k] | (pk->data[2 + 2 * k] << 8));
}

int main(void)
{
    Parser p = {0};
    Packet out[4];
    size_t k;

    /* 1. basic: noise, good packet, bad-CHK packet, good packet with two temps */
    const uint8_t s1[] = { 0x00, 0x13,
                           0xAA, 0x03, 0x01, 0x2C, 0x03, 0x2D,               /* id 1: 812      */
                           0xAA, 0x03, 0x01, 0x2C, 0x03, 0x00,               /* bad CHK        */
                           0xAA, 0x05, 0x02, 0x2C, 0x03, 0x38, 0x03, 0x13 }; /* id 2: 812, 824 */
    parser_init(&p);
    k = feed_all(&p, s1, sizeof s1, out, 4);
    CHECK(k == 2 && p.bad_frames == 1);
    CHECK(k >= 1 && out[0].len == 3 && out[0].data[0] == 1 && temp_at(&out[0], 0) == 812);
    CHECK(k >= 2 && out[1].len == 5 && out[1].data[0] == 2 && temp_at(&out[1], 1) == 824);

    /* 2. packet split across two UART chunks: state survives between calls */
    parser_init(&p);
    k  = feed_all(&p, s1 + 2, 3, out, 4);           /* AA 03 01          */
    CHECK(k == 0);
    k += feed_all(&p, s1 + 5, 3, out, 4);           /* 2C 03 2D          */
    CHECK(k == 1 && temp_at(&out[0], 0) == 812);

    /* 3. bad LEN (0 and 17) then double SYNC: AA AA must still lock on */
    const uint8_t s3[] = { 0xAA, 0x00, 0xAA, 0x11, 0xAA, 0xAA, 0x03, 0x01, 0x2C, 0x03, 0x2D };
    parser_init(&p);
    k = feed_all(&p, s3, sizeof s3, out, 4);
    CHECK(k == 1 && p.bad_frames == 3 && temp_at(&out[0], 0) == 812);

    /* 4. 0xAA inside the payload and as CHK is data, not a SYNC */
    const uint8_t s4[] = { 0xAA, 0x03, 0xAA, 0x01, 0x02, 0xAA };   /* CHK = 03^AA^01^02 = AA */
    parser_init(&p);
    k = feed_all(&p, s4, sizeof s4, out, 4);
    CHECK(k == 1 && out[0].data[0] == 0xAA && temp_at(&out[0], 0) == 513);

    /* 5. maximum length (16) is accepted */
    uint8_t s5[2 + 16 + 1] = { 0xAA, 16 };
    uint8_t chk = 16;
    for (int i = 0; i < 16; i++) { s5[2 + i] = (uint8_t)(i * 7); chk ^= s5[2 + i]; }
    s5[18] = chk;
    parser_init(&p);
    k = feed_all(&p, s5, sizeof s5, out, 4);
    CHECK(k == 1 && out[0].len == 16 && out[0].data[15] == 105 && p.bad_frames == 0);

    /* 6. no output and no state change on pure noise or empty input */
    const uint8_t s6[] = { 0x00, 0x55, 0xFF, 0x13 };
    parser_init(&p);
    CHECK(feed_all(&p, s6, sizeof s6, out, 4) == 0 && p.bad_frames == 0 && p.st == ST_SYNC);
    CHECK(feed_all(&p, NULL, 0, out, 4) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
