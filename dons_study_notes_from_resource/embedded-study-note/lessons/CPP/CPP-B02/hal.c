/* CPP-B02 demo — mock C HAL 구현 (cc 로 컴파일 → 심볼은 C 이름 그대로) */
#include "hal.h"
#include <stdio.h>

static uint32_t    s_odr;          /* 가짜 GPIO ODR 레지스터 */
static hal_rx_cb_t s_rx_cb;
static void       *s_rx_ctx;

void hal_gpio_write(uint32_t pin, bool level) {
    if (level) s_odr |= 1u << pin; else s_odr &= ~(1u << pin);
}
uint32_t hal_gpio_read_odr(void) { return s_odr; }

size_t hal_uart_write(const uint8_t *buf, size_t len) {
    printf("[hal] uart_write: %.*s", (int)len, (const char *)buf);
    return len;
}

void hal_uart_set_rx_callback(hal_rx_cb_t cb, void *ctx) { s_rx_cb = cb; s_rx_ctx = ctx; }
void hal_sim_rx_byte(uint8_t b) { if (s_rx_cb) s_rx_cb(s_rx_ctx, b); }
