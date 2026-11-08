/* C-A11 demo — 스니펫 1 의 대기 루프: volatile 이 없으면 -O2 에서 무엇이 되나 (관찰용, main 없음)
 *
 *   cc -std=c11 -O2 -S demo_volatile.c -o -                                                   # 호스트 arm64
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_volatile.c -o -    # Cortex-M4
 *
 * 타깃 전용이라 <stdint.h> 만 include 한다.
 */
#include <stdint.h>

#define RX_SIZE 8u
uint8_t           rx_buf[RX_SIZE];
uint32_t          rx_head, rx_tail;        /* 원본: volatile 없음 */
volatile uint32_t vrx_head, vrx_tail;      /* 고친 판 */

int getc_wait_plain(void) {                /* while ((c = uart_getc()) < 0) {} 를 인라인한 모양 */
    while (rx_head == rx_tail) { }
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1u) % RX_SIZE;
    return c;
}

int getc_wait_volatile(void) {
    while (vrx_head == vrx_tail) { }
    uint32_t t = vrx_tail;
    uint8_t c = rx_buf[t];
    vrx_tail = (t + 1u) % RX_SIZE;
    return c;
}
