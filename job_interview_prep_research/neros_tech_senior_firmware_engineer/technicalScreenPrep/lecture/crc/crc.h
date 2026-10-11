/* L10 CRC 예제 — 비트 방식 · 테이블 생성 · 테이블 조회 (CRC-8 / CRC-16 CCITT-FALSE / CRC-32) */
#ifndef CRC_H
#define CRC_H
#include <stddef.h>
#include <stdint.h>

uint8_t  crc8(const uint8_t *data, size_t len);            /* crc8_table.c: 256 테이블 리터럴 (전형적인 CRC.c) */
uint8_t  crc8_bitwise(const uint8_t *data, size_t len);
void     crc8_make_table(uint8_t table[256]);

uint16_t crc16_ccitt_bitwise(const uint16_t init, const uint8_t *data, size_t len);
void     crc16_ccitt_make_table(uint16_t table[256]);
uint16_t crc16_ccitt_table(const uint16_t table[256], uint16_t init, const uint8_t *data, size_t len);
uint16_t crc16_ccitt_nibble(const uint8_t *data, size_t len);  /* 16칸 테이블 (메모리 절약) */

uint32_t crc32_bitwise(const uint8_t *data, size_t len);
void     crc32_make_table(uint32_t table[256]);
uint32_t crc32_table(const uint32_t table[256], const uint8_t *data, size_t len);
#endif
