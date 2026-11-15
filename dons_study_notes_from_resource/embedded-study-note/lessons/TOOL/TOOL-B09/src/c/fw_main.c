/* Part C firmware: uses uart_init() directly, printf() indirectly needs _write() */
#include <stdio.h>
#include "uart.h"

int main(void)
{
	uart_init(115200u);
	printf("boot ok, fw v%d.%d\n", 1, 4);
	for (;;)
		;
}
