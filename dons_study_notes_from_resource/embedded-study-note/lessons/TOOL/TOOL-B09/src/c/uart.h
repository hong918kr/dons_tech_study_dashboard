/* Part C: tiny UART driver shipped as libuart.a (STM32-style USART1 registers) */
#ifndef UART_H
#define UART_H
#include <stdint.h>

void uart_init(uint32_t baud);
void uart_putc(char c);
#endif
