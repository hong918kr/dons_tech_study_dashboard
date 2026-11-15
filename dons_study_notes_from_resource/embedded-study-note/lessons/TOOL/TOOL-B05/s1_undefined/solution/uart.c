/* uart.c — 디버그 UART */
#include <stdio.h>
#include "uart.h"

void uart_putc(char c)           /* 헤더와 같은 이름 */
{
    putchar(c);
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}
