/* "CRC.c" 라이브러리 (재구성): 문제에서 주어진다고 했던 것. 실제 다항식 · 폭은 미확인 [추정: CRC-8, poly 0x07]. */
#ifndef CRC_H
#define CRC_H
#include <stddef.h>
#include <stdint.h>
uint8_t crc8(const uint8_t *data, size_t len);
#endif
