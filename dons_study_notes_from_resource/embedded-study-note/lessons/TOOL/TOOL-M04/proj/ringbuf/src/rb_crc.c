/* rb_crc.c — CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
 * Compiled ONLY when the FEATURE_CRC option is ON. */
#include "rb_internal.h"

uint16_t rb_crc_step(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)(b << 8);
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t rb_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--)
        crc = rb_crc_step(crc, *p++);
    return crc;
}

uint16_t rb_running_crc(const rb_t *rb)
{
    return rb->crc;
}
