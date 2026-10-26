/* T7 binary record -- "seq u16, 3x int16 accel mg, vbat mV u16, flags u8, CRC-8 (poly 0x07, init 0) over 11 bytes" */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BODY 11
#define SIZE 12

typedef struct {
    uint16_t seq;
    int16_t accel[3];
    uint16_t mv;
    uint8_t flags;
} rec_t;

enum { P_OK = 0, P_CRC = 1, P_VBAT = 2, P_FLAGS = 4 };

uint8_t crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

/* little-endian by hand: never memcpy the bytes onto a struct (padding, host endianness) */
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int decode_one(const uint8_t *raw, rec_t *r)
{
    if (crc8(raw, BODY) != raw[BODY])
        return P_CRC;                          /* don't trust any field */
    r->seq = le16(raw);
    for (int i = 0; i < 3; i++)
        r->accel[i] = (int16_t)le16(raw + 2 + 2 * i);
    r->mv = le16(raw + 8);
    r->flags = raw[10];
    int p = P_OK;
    if (r->mv < 9000 || r->mv > 26000)
        p |= P_VBAT;
    if (r->flags & ~0x07)
        p |= P_FLAGS;
    return p;
}

static void make(uint8_t *out, uint16_t seq, int16_t ax, int16_t ay, int16_t az, uint16_t mv, uint8_t fl)
{
    uint16_t v[5] = {seq, (uint16_t)ax, (uint16_t)ay, (uint16_t)az, mv};
    for (int i = 0; i < 5; i++) {
        out[2 * i] = (uint8_t)(v[i] & 0xFF);
        out[2 * i + 1] = (uint8_t)(v[i] >> 8);
    }
    out[10] = fl;
    out[BODY] = crc8(out, BODY);
}

int main(void)
{
    assert(crc8((const uint8_t *)"123456789", 9) == 0xF4);  /* standard CRC-8 check value */
    uint8_t raw[SIZE];
    rec_t r;
    make(raw, 1, 0, -5, 1000, 16800, 1);
    assert(decode_one(raw, &r) == P_OK);
    assert(r.accel[1] == -5 && r.accel[2] == 1000 && (r.flags & 1));   /* sign kept */
    raw[3] ^= 1;
    assert(decode_one(raw, &r) == P_CRC);
    make(raw, 3, 0, 0, 0, 5000, 0x80);
    assert(decode_one(raw, &r) == (P_VBAT | P_FLAGS));
    size_t blob_len = 25;                                    /* 2 records + 1 trailing byte */
    assert(blob_len / SIZE == 2 && blob_len % SIZE == 1);   /* report the trailing byte */
    puts("t7 OK");
    return 0;
}
