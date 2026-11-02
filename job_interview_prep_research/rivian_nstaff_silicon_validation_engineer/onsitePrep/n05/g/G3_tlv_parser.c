/* G3 - TLV parser: [type:1][len:1][value:len] ..., bounds-checked, unknown types skipped */
/* --- model answer --- */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum { T_TEMP = 0x01, T_SERIAL = 0x02, T_FWVER = 0x03, T_END = 0xFF };
enum { TLV_OK = 0, TLV_TRUNC = -1, TLV_BADLEN = -2 };

typedef struct {
    bool     has_temp, has_serial, has_fw;
    int16_t  temp_c10;          /* T_TEMP: 2 bytes big-endian, signed, 0.1 C units */
    uint32_t serial;            /* T_SERIAL: 4 bytes big-endian */
    uint8_t  fw[3];             /* T_FWVER: major, minor, patch */
    unsigned unknown;           /* how many unknown records were skipped */
} devinfo_t;

int tlv_parse(const uint8_t *buf, size_t n, devinfo_t *out)
{
    *out = (devinfo_t){0};
    size_t i = 0;
    while (i < n) {
        uint8_t type = buf[i];
        if (type == T_END) return TLV_OK;               /* END has no length byte */
        if (n - i < 2) return TLV_TRUNC;                /* header cut off */
        uint8_t len = buf[i + 1];
        if (len > n - i - 2) return TLV_TRUNC;          /* no i+2+len overflow */
        const uint8_t *v = &buf[i + 2];
        switch (type) {
        case T_TEMP:
            if (len != 2) return TLV_BADLEN;
            out->temp_c10 = (int16_t)(uint16_t)((v[0] << 8) | v[1]);
            out->has_temp = true;
            break;
        case T_SERIAL:
            if (len != 4) return TLV_BADLEN;
            out->serial = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                          ((uint32_t)v[2] << 8)  |  (uint32_t)v[3];
            out->has_serial = true;
            break;
        case T_FWVER:
            if (len != 3) return TLV_BADLEN;
            out->fw[0] = v[0]; out->fw[1] = v[1]; out->fw[2] = v[2];
            out->has_fw = true;
            break;
        default:
            out->unknown++;                             /* forward compatible: skip */
            break;
        }
        i += 2u + len;
    }
    return TLV_OK;                                      /* ran out of bytes cleanly */
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    devinfo_t d;

    /* normal: temp -12.5 C (0xFF83), serial 0xDEADBEEF, unknown 0x7E (3 bytes), fw 1.2.3, END */
    const uint8_t b1[] = {0x01, 2, 0xFF, 0x83,
                          0x02, 4, 0xDE, 0xAD, 0xBE, 0xEF,
                          0x7E, 3, 9, 9, 9,
                          0x03, 3, 1, 2, 3,
                          0xFF,
                          0x01, 2, 0x00, 0x00};   /* after END: ignored */
    assert(tlv_parse(b1, sizeof b1, &d) == TLV_OK);
    assert(d.has_temp && d.temp_c10 == -125);
    assert(d.has_serial && d.serial == 0xDEADBEEFu);
    assert(d.has_fw && d.fw[0] == 1 && d.fw[1] == 2 && d.fw[2] == 3);
    assert(d.unknown == 1);

    /* empty buffer: OK, nothing present */
    assert(tlv_parse(b1, 0, &d) == TLV_OK && !d.has_temp && d.unknown == 0);

    /* only a type byte (header truncated) */
    const uint8_t b2[] = {0x01};
    assert(tlv_parse(b2, sizeof b2, &d) == TLV_TRUNC);

    /* value truncated: len says 4, only 3 bytes left */
    const uint8_t b3[] = {0x02, 4, 0x11, 0x22, 0x33};
    assert(tlv_parse(b3, sizeof b3, &d) == TLV_TRUNC && !d.has_serial);

    /* hostile len 255 with tiny buffer (overflow-safe check) */
    const uint8_t b4[] = {0x7E, 255, 0};
    assert(tlv_parse(b4, sizeof b4, &d) == TLV_TRUNC);

    /* known type with wrong length */
    const uint8_t b5[] = {0x01, 3, 0, 0, 0};
    assert(tlv_parse(b5, sizeof b5, &d) == TLV_BADLEN);

    /* zero-length unknown record, then record ending exactly at n */
    const uint8_t b6[] = {0x40, 0, 0x03, 3, 4, 5, 6};
    assert(tlv_parse(b6, sizeof b6, &d) == TLV_OK && d.unknown == 1 && d.fw[2] == 6);

    /* END only */
    const uint8_t b7[] = {0xFF};
    assert(tlv_parse(b7, sizeof b7, &d) == TLV_OK);

    printf("G3 PASS\n");
    return 0;
}
