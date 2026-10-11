#include "crc.h"

uint8_t crc8(const uint8_t *data, size_t len)    /* CRC-8/SMBUS: poly 0x07, init 0 — check("123456789") = 0xF4 */
{
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
