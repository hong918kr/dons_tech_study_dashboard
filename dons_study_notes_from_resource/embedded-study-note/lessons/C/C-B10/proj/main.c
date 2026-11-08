/* main.c — uart/gpio 모듈을 헤더로만 안다. */
#include <stdio.h>
#include "gpio.h"
#include "uart.h"

int main(void) {
    unsigned n1 = uart_init(9600);
    unsigned n2 = uart_init(57600);     /* 두 번째 호출: 카운터만 증가, 설정은 그대로 */
    gpio_init();
    gpio_write(5, 1);
    uart_send("hello");

    printf("init calls=%u,%u baud=%u\n", n1, n2, (unsigned)uart_baud());
    printf("uart_tx_bytes=%u log_level=%d gpio=0x%X\n",
           (unsigned)uart_tx_bytes, uart_log_level, gpio_state());
    return 0;
}
