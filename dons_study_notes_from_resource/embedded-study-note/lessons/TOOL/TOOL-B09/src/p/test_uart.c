/* libtests.a member: registers itself from a constructor */
#include "registry.h"

static int test_uart_baud(void)
{
	return (84000000u + 57600u) / 115200u == 729u ? 0 : 1;
}

__attribute__((constructor)) static void reg_uart(void)
{
	test_register("uart_baud", test_uart_baud);
}
