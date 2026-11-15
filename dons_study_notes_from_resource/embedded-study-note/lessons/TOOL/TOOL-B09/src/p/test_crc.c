/* libtests.a member: registers itself from a constructor */
#include "registry.h"

static int test_crc_poly(void)
{
	return (0x04C11DB7u >> 31) == 0u ? 0 : 1;
}

__attribute__((constructor)) static void reg_crc(void)
{
	test_register("crc_poly", test_crc_poly);
}
