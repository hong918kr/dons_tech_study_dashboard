/* C-B13 demo: 시프트로 쓴 bswap / 바이트 배열 읽기가 어떤 명령으로 컴파일되는가 */
#include <stdint.h>

uint32_t bswap32_shift(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}
uint16_t bswap16_shift(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
uint32_t rev16_shift(uint32_t x) { return ((x & 0x00FF00FFu) << 8) | ((x & 0xFF00FF00u) >> 8); }
uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint32_t read_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
