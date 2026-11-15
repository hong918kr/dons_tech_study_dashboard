/* test_main.c — 호스트에서 인터럽트를 "쏴 보고" 누가 받았는지 본다 (TOOL-B05 s4) */
#include <stdio.h>
#include "irq.h"
#include "events.h"

int main(void)
{
    nvic_fire(VEC_USART1);
    nvic_fire(VEC_TIM2);
    nvic_fire(VEC_TIM2);
    printf("uart_rx=%u tim2=%u default_hits=%u last_unhandled=%d\n",
           event_count(EV_UART_RX), event_count(EV_TIM2_TICK),
           g_default_hits, g_unhandled_vec);
    return 0;
}
