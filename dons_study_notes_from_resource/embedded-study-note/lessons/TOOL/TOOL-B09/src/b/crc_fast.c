/* libcrc_fast.a: table-driven CRC-32 (256-entry table built on first use) */
#include "crc.h"

static uint32_t table[256];
static int table_ready;

static void build_table(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
		table[i] = c;
	}
	table_ready = 1;
}

uint32_t crc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t c = 0xFFFFFFFFu;

	if (!table_ready)
		build_table();
	while (len--)
		c = table[(c ^ *p++) & 0xFFu] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

const char *crc32_impl(void)
{
	return "fast";
}
