/* TOOL-M02 demo firmware: vendor-style legacy file, built with -fcommon.
   Tentative definitions (no initializer, no extern) become COMMON symbols. */
#include <stdint.h>

int board_rev;
uint32_t board_scratch[64];

void legacy_board_init(void)
{
	board_rev = 3;
	for (int i = 0; i < 64; i++)
		board_scratch[i] = (uint32_t)i * 0x01010101u;
}
