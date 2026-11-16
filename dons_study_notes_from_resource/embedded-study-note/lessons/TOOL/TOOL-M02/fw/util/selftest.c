/* libutil.a member nobody references: never pulled out of the archive. */
#include "../fw.h"

const char util_selftest_banner[] = "libutil self-test v1";
int util_selftest(void)
{
	return crc16_ccitt((const uint8_t *)util_selftest_banner, 4) == 0;
}
