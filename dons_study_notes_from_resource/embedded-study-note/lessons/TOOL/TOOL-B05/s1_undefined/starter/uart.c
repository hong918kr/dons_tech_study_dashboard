/* uart.c — 디버그 UART */
#include <stdio.h>
#include "uart.h"

void uart_put_char(char c)       /* 이름이 헤더와 다르다 */
{
    putchar(c);
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}
