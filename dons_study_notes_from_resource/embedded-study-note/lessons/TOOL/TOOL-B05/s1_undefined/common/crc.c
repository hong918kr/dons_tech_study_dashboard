/* crc.c — 비트 단위 CRC-8, poly 0x07 ("123456789" → 0xF4) */
#include "crc.h"

uint8_t crc8(const uint8_t *p, size_t n)
{
    unsigned c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c & 0x80u) ? ((c << 1) ^ 0x07u) & 0xFFu : (c << 1) & 0xFFu;
    }
    return (uint8_t)c;
}
