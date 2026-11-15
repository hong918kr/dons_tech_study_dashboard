/* libuart.a member 1: register-level driver (STM32F4 USART1 @ 0x40011000) */
#include "uart.h"

#define USART1_BASE 0x40011000u
#define USART_SR  (*(volatile uint32_t *)(USART1_BASE + 0x00u))
#define USART_DR  (*(volatile uint32_t *)(USART1_BASE + 0x04u))
#define USART_BRR (*(volatile uint32_t *)(USART1_BASE + 0x08u))
#define USART_CR1 (*(volatile uint32_t *)(USART1_BASE + 0x0Cu))
#define SR_TXE    (1u << 7)
#define PCLK2_HZ  84000000u

void uart_init(uint32_t baud)
{
	USART_BRR = (PCLK2_HZ + baud / 2u) / baud;
	USART_CR1 = (1u << 13) | (1u << 3);     /* UE | TE */
}

void uart_putc(char c)
{
	while (!(USART_SR & SR_TXE))
		;
	USART_DR = (uint8_t)c;
}
