/* crc.h — CRC-8 (poly 0x07, init 0x00) */
#ifndef CRC_H
#define CRC_H
#include <stddef.h>
#include <stdint.h>
uint8_t crc8(const uint8_t *p, size_t n);
#endif
