/* libuart.a member 2: newlib retarget -- printf() -> _write() -> UART.
 * Nothing in the APP calls _write(); only libc_nano's stdio does. */
#include "uart.h"

int _write(int fd, const char *buf, int len);

int _write(int fd, const char *buf, int len)
{
	(void)fd;
	for (int i = 0; i < len; i++) {
		if (buf[i] == '\n')
			uart_putc('\r');
		uart_putc(buf[i]);
	}
	return len;
}
