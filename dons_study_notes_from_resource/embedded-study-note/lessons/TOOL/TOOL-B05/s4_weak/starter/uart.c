/* uart.c — USART1 수신 인터럽트 핸들러 */
#include "irq.h"
#include "events.h"

void USART1_IRQHandle(void)
{
    event_post(EV_UART_RX);       /* 실제: DR 읽기 → RX 링 버퍼 push */
}
