/* uart.h — 디버그 UART (호스트에서는 stdout 으로 흉내) */
#ifndef UART_H
#define UART_H
void uart_putc(char c);
void uart_puts(const char *s);
#endif
