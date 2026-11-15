/* libcrc_ref.a member 1: bit-by-bit reference CRC-32 (slow, obviously correct) */
#include "crc.h"

uint32_t crc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t c = 0xFFFFFFFFu;

	while (len--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
	}
	return c ^ 0xFFFFFFFFu;
}

const char *crc32_impl(void)
{
	return "ref";
}
