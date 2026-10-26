/*
 * E2  Extract Packets from a Byte Buffer
 *
 * The UART driver appends received bytes to an accumulation buffer. Write
 * extract_packet(), which looks at the bytes currently in the buffer and finds the
 * FIRST complete, valid frame. The caller removes *consumed bytes from the front,
 * keeps the unprocessed tail, and calls again after more bytes arrive.
 *
 * Frame format (agreed with the interviewer):
 *   [0xAA] [0x55] [TYPE] [LEN] [PAYLOAD: LEN bytes] [CRC_LO] [CRC_HI]
 *   - 0 <= LEN <= MAX_PAYLOAD (32). A larger LEN means a false sync.
 *   - CRC = crc16() (CRC-16/CCITT-FALSE, given library) over TYPE, LEN and PAYLOAD,
 *     sent little-endian (low byte first).
 *   - 0xAA 0x55 may also appear inside a payload (framing is length-based).
 *
 * Return values and *consumed (bytes the caller may drop from the front of buf):
 *   PKT_FOUND     *out filled; consumed = garbage before the frame + the whole frame.
 *   PKT_NEED_MORE no complete frame yet; consumed = garbage only (a partial frame and
 *                 a trailing 0xAA are kept). Call again after appending more bytes.
 *   PKT_BAD_LEN   LEN > MAX_PAYLOAD;  consumed = garbage + 1 (skip the 0xAA only).
 *   PKT_BAD_CRC   CRC mismatch;       consumed = garbage + 1 (skip the 0xAA only).
 *   *out is written only on PKT_FOUND. No malloc.
 *
 * Part B: decode_temp() decodes a TYPE 0x01 packet: int16 temp (0.01 degC) LE,
 *         then uint32 timestamp (ms) LE. LEN must be exactly 6.
 * Part C: rx_feed() is the driver loop: append bytes into a fixed 64-byte buffer,
 *         extract until NEED_MORE, then memmove the unprocessed tail to the front.
 *
 * Reference solution.  Build & run:  ./run.sh E2 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define SYNC0       0xAAu
#define SYNC1       0x55u
#define HDR_LEN     4u                         /* AA 55 TYPE LEN */
#define CRC_LEN     2u
#define MAX_PAYLOAD 32u
#define MAX_FRAME   (HDR_LEN + MAX_PAYLOAD + CRC_LEN)   /* 38 */
#define TYPE_TEMP   0x01u

typedef enum { PKT_NEED_MORE, PKT_FOUND, PKT_BAD_CRC, PKT_BAD_LEN } PktStatus;

typedef struct { uint8_t type; uint8_t len; uint8_t payload[MAX_PAYLOAD]; } Packet;
typedef struct { int16_t temp_c100; uint32_t t_ms; } TempReading;   /* 2534 = 25.34 degC */

/* ---- given library (like a provided CRC.c): CRC-16/CCITT-FALSE, check("123456789") = 0x29B1 ---- */
uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*data++ << 8);
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ---- Part A ---- */
PktStatus extract_packet(const uint8_t *buf, size_t len, Packet *out, size_t *consumed)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != SYNC0) continue;                  /* garbage */
        if (i + 1 == len) { *consumed = i; return PKT_NEED_MORE; }   /* lone AA at end: keep */
        if (buf[i + 1] != SYNC1) continue;              /* AA not followed by 55 */

        /* sync at i */
        if (len - i < HDR_LEN) { *consumed = i; return PKT_NEED_MORE; }
        uint8_t plen = buf[i + 3];
        if (plen > MAX_PAYLOAD) { *consumed = i + 1; return PKT_BAD_LEN; }
        size_t frame = HDR_LEN + plen + CRC_LEN;
        if (len - i < frame) { *consumed = i; return PKT_NEED_MORE; }   /* keep partial frame */

        const uint8_t *f = buf + i;
        uint16_t rx_crc = (uint16_t)(f[HDR_LEN + plen] | (f[HDR_LEN + plen + 1] << 8));
        if (crc16(f + 2, 2u + plen) != rx_crc) { *consumed = i + 1; return PKT_BAD_CRC; }

        out->type = f[2];
        out->len = plen;
        memcpy(out->payload, f + HDR_LEN, plen);
        *consumed = i + frame;
        return PKT_FOUND;
    }
    *consumed = len;                                    /* all garbage (or empty) */
    return PKT_NEED_MORE;
}

/* ---- Part B ---- */
bool decode_temp(const Packet *p, TempReading *r)
{
    if (p->type != TYPE_TEMP || p->len != 6) return false;
    const uint8_t *b = p->payload;
    r->temp_c100 = (int16_t)(uint16_t)(b[0] | (b[1] << 8));
    r->t_ms = (uint32_t)b[2] | ((uint32_t)b[3] << 8) | ((uint32_t)b[4] << 16) | ((uint32_t)b[5] << 24);
    return true;
}

/* ---- Part C ---- */
#define ACC_CAP 64u
_Static_assert(ACC_CAP >= MAX_FRAME, "a max frame must fit, or a partial frame blocks forever");

typedef struct {
    uint8_t buf[ACC_CAP];
    size_t len;                       /* valid bytes in buf */
    uint32_t bad_crc, bad_len;        /* diagnostics */
} RxAcc;

/* Append n bytes (n may exceed free space), extract every packet, compact.
 * Writes at most out_cap packets, returns the total number found. */
size_t rx_feed(RxAcc *rx, const uint8_t *data, size_t n, Packet *out, size_t out_cap)
{
    size_t found = 0;
    while (n > 0) {
        size_t room = ACC_CAP - rx->len;
        size_t take = n < room ? n : room;
        memcpy(rx->buf + rx->len, data, take);
        rx->len += take; data += take; n -= take;

        size_t pos = 0;                               /* start of unprocessed bytes */
        for (;;) {
            Packet pk;
            size_t used = 0;
            PktStatus st = extract_packet(rx->buf + pos, rx->len - pos, &pk, &used);
            pos += used;
            if (st == PKT_NEED_MORE) break;
            if (st == PKT_FOUND) { if (found < out_cap) out[found] = pk; found++; }
            else if (st == PKT_BAD_CRC) rx->bad_crc++;
            else rx->bad_len++;
        }
        memmove(rx->buf, rx->buf + pos, rx->len - pos);   /* keep the tail, at the front */
        rx->len -= pos;
    }
    return found;
}

/* ---- test helpers (not part of the answer) ---- */
static size_t make_frame(uint8_t *dst, uint8_t type, const uint8_t *pl, uint8_t n)
{
    dst[0] = SYNC0; dst[1] = SYNC1; dst[2] = type; dst[3] = n;
    if (n) memcpy(dst + 4, pl, n);
    uint16_t c = crc16(dst + 2, 2u + n);
    dst[4 + n] = (uint8_t)c; dst[5 + n] = (uint8_t)(c >> 8);
    return 6u + n;
}
static size_t temp_frame(uint8_t *dst, int16_t temp_c100, uint32_t t_ms)
{
    uint16_t t = (uint16_t)temp_c100;
    const uint8_t pl[6] = { (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)t_ms, (uint8_t)(t_ms >> 8),
                            (uint8_t)(t_ms >> 16), (uint8_t)(t_ms >> 24) };
    return make_frame(dst, TYPE_TEMP, pl, 6);
}
static uint32_t rng = 1;
static uint32_t xorshift(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void)
{
    Packet pk;
    TempReading r;
    size_t used;
    PktStatus st;

    /* 0. the given library matches the standard check value */
    CHECK(crc16((const uint8_t *)"123456789", 9) == 0x29B1);

    /* 1. README example: garbage, false sync (bad CRC), valid frame, partial frame */
    const uint8_t ex[] = { 0x00, 0xAA, 0x13,                                  /* garbage      */
                           0xAA, 0x55, 0x09, 0x04,                            /* false sync   */
                           0xAA, 0x55, 0x01, 0x06, 0xE6, 0x09, 0xE8, 0x03, 0x00, 0x00, 0x14, 0x21,
                           0xAA, 0x55, 0x01, 0x06, 0xF0 };                    /* partial      */
    size_t pos = 0;
    used = 99; st = extract_packet(ex + pos, sizeof ex - pos, &pk, &used);
    CHECK(st == PKT_BAD_CRC && used == 4);              /* skip 3 garbage + the false 0xAA */
    pos += used;
    used = 99; st = extract_packet(ex + pos, sizeof ex - pos, &pk, &used);
    CHECK(st == PKT_FOUND && used == 15 && pk.type == 0x01 && pk.len == 6);
    CHECK(st == PKT_FOUND && decode_temp(&pk, &r) && r.temp_c100 == 2534 && r.t_ms == 1000);
    pos += used;
    used = 99; st = extract_packet(ex + pos, sizeof ex - pos, &pk, &used);
    CHECK(st == PKT_NEED_MORE && used == 0 && pos == 19);   /* keep the 5-byte tail */

    /* 1b. caller compacts: memmove the 5-byte tail to the front, append 7 new bytes, call again */
    uint8_t acc[64];
    memcpy(acc, ex, sizeof ex);
    size_t acc_len = sizeof ex;
    memmove(acc, acc + pos, acc_len - pos);
    acc_len -= pos;
    const uint8_t rest[] = { 0x0A, 0xD0, 0x07, 0x00, 0x00, 0x49, 0x2D };
    memcpy(acc + acc_len, rest, sizeof rest);
    acc_len += sizeof rest;
    used = 99; st = extract_packet(acc, acc_len, &pk, &used);
    CHECK(acc_len == 12 && st == PKT_FOUND && used == 12);
    CHECK(st == PKT_FOUND && decode_temp(&pk, &r) && r.temp_c100 == 2800 && r.t_ms == 2000);

    /* 2. NEED_MORE: what may be dropped and what must be kept */
    const uint8_t h1[] = { 0xAA, 0x55, 0x01 };              /* header not complete */
    const uint8_t h2[] = { 0x00, 0x01, 0xAA, 0x55, 0x01 };  /* garbage + partial header */
    const uint8_t h3[] = { 0x01, 0x02, 0xAA };              /* lone AA could be a sync */
    const uint8_t h4[] = { 0x01, 0x02, 0x03 };              /* pure garbage */
    used = 99; CHECK(extract_packet(h1, 3, &pk, &used) == PKT_NEED_MORE && used == 0);
    used = 99; CHECK(extract_packet(h2, 5, &pk, &used) == PKT_NEED_MORE && used == 2);
    used = 99; CHECK(extract_packet(h3, 3, &pk, &used) == PKT_NEED_MORE && used == 2);
    used = 99; CHECK(extract_packet(h4, 3, &pk, &used) == PKT_NEED_MORE && used == 3);
    used = 99; CHECK(extract_packet(NULL, 0, &pk, &used) == PKT_NEED_MORE && used == 0);

    /* 3. LEN limits: 33 is a false sync, 32 and 0 are valid */
    uint8_t big[64], pl32[32];
    const uint8_t bl[] = { 0xAA, 0x55, 0x01, 0x21, 0x00 };
    used = 99; CHECK(extract_packet(bl, 5, &pk, &used) == PKT_BAD_LEN && used == 1);
    for (int i = 0; i < 32; i++) pl32[i] = (uint8_t)(i * 7);
    size_t n32 = make_frame(big, 0x04, pl32, 32);
    used = 99; st = extract_packet(big, n32, &pk, &used);
    CHECK(st == PKT_FOUND && used == 38 && pk.len == 32 && pk.payload[31] == 217);
    const uint8_t hb[] = { 0xAA, 0x55, 0x03, 0x00, 0x5C, 0x48 };   /* heartbeat, LEN 0 */
    used = 99; st = extract_packet(hb, 6, &pk, &used);
    CHECK(st == PKT_FOUND && used == 6 && pk.type == 0x03 && pk.len == 0);

    /* 4. two packets back to back, then nothing left */
    uint8_t two[24];
    size_t n2 = temp_frame(two, 2534, 1000);
    n2 += temp_frame(two + n2, 2800, 2000);
    pos = 0;
    st = extract_packet(two, n2, &pk, &used); pos += used;
    CHECK(st == PKT_FOUND && used == 12 && decode_temp(&pk, &r) && r.temp_c100 == 2534);
    st = extract_packet(two + pos, n2 - pos, &pk, &used); pos += used;
    CHECK(st == PKT_FOUND && used == 12 && decode_temp(&pk, &r) && r.temp_c100 == 2800 && r.t_ms == 2000);
    st = extract_packet(two + pos, n2 - pos, &pk, &used);
    CHECK(st == PKT_NEED_MORE && used == 0 && pos == 24);

    /* 5. AA 55 inside a valid payload is data, not a sync */
    uint8_t fs[16];
    const uint8_t plfs[] = { 0xAA, 0x55, 0x07 };
    size_t nfs = make_frame(fs, 0x02, plfs, 3);
    used = 99; st = extract_packet(fs, nfs, &pk, &used);
    CHECK(st == PKT_FOUND && used == 9 && pk.type == 0x02 && pk.payload[1] == 0x55);

    /* 6. decode_temp: negative temp, wrong type, wrong length */
    uint8_t neg[16];
    st = extract_packet(neg, temp_frame(neg, -150, 4000), &pk, &used);
    CHECK(st == PKT_FOUND && decode_temp(&pk, &r) && r.temp_c100 == -150 && r.t_ms == 4000);
    pk.len = 5;    CHECK(!decode_temp(&pk, &r));
    pk.len = 6; pk.type = 0x02; CHECK(!decode_temp(&pk, &r));

    /* 7. driver loop: same stream in random chunk sizes -> exact same packets */
    uint8_t s[160];
    size_t n = 0;
    const uint8_t junk[] = { 0x00, 0xAA, 0x13, 0xAA, 0x55, 0x09, 0x04 };   /* garbage + false sync */
    memcpy(s, junk, sizeof junk); n += sizeof junk;
    n += temp_frame(s + n, 2534, 1000);
    const uint8_t badlen[] = { 0xAA, 0x55, 0x01, 0xFF };
    memcpy(s + n, badlen, 4); n += 4;
    n += temp_frame(s + n, 2800, 2000);
    n += make_frame(s + n, 0x03, NULL, 0);
    size_t c0 = n; n += temp_frame(s + n, 9999, 3000); s[c0 + 5] ^= 0x01;   /* corrupted */
    n += make_frame(s + n, 0x02, plfs, 3);
    n += temp_frame(s + n, -150, 4000);
    n += make_frame(s + n, 0x04, pl32, 32);
    CHECK(n == 112);

    const uint8_t want_type[] = { 0x01, 0x01, 0x03, 0x02, 0x01, 0x04 };
    const int16_t want_temp[] = { 2534, 2800, 0, 0, -150, 0 };
    for (uint32_t seed = 0; seed <= 200; seed++) {
        RxAcc rx = {0};
        Packet got[8];
        size_t k = 0;
        rng = seed * 2654435761u + 1;
        for (size_t off = 0; off < n;) {
            size_t chunk = (seed == 0) ? n : 1 + xorshift() % 20;     /* seed 0: one big write */
            if (chunk > n - off) chunk = n - off;
            k += rx_feed(&rx, s + off, chunk, got + (k < 8 ? k : 8), k < 8 ? 8 - k : 0);
            off += chunk;
            if (rx.len > ACC_CAP) break;
        }
        bool ok = (k == 6 && rx.bad_crc == 2 && rx.bad_len == 1 && rx.len == 0);
        for (size_t i = 0; ok && i < 6; i++) {
            ok = got[i].type == want_type[i];
            if (ok && got[i].type == TYPE_TEMP)
                ok = decode_temp(&got[i], &r) && r.temp_c100 == want_temp[i];
        }
        CHECK(ok);
        if (!ok) { printf("  seed %u: k=%zu bad_crc=%u bad_len=%u\n", seed, k, rx.bad_crc, rx.bad_len); break; }
    }

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
