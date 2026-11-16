/* libutil.a member: table-driven CRC-16/CCITT. */
#include "../fw.h"

#define T(i) (uint16_t)((i) * 0x1021u ^ ((i) << 7))
#define T4(i) T(i), T((i) + 1), T((i) + 2), T((i) + 3)
#define T16(i) T4(i), T4((i) + 4), T4((i) + 8), T4((i) + 12)
#define T64(i) T16(i), T16((i) + 16), T16((i) + 32), T16((i) + 48)
const uint16_t crc16_table[256] = { T64(0), T64(64), T64(128), T64(192) };

uint16_t crc16_ccitt(const uint8_t *p, size_t n)
{
	uint16_t crc = 0xffff;
	while (n--)
		crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ *p++) & 0xff]);
	return crc;
}
