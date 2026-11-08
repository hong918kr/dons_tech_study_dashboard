/* CPP-B02 demo — mock C HAL 헤더 (C와 C++ 양쪽에서 include 된다)
 * 핵심: __cplusplus 가드 안의 extern "C" { ... } 블록.
 * -DHAL_FORGET_EXTERN_C 로 빌드하면 가드가 빠진 "실수한 헤더"가 된다 (build.sh 시나리오 2). */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus) && !defined(HAL_FORGET_EXTERN_C)
extern "C" {
#endif

void     hal_gpio_write(uint32_t pin, bool level);
uint32_t hal_gpio_read_odr(void);
size_t   hal_uart_write(const uint8_t *buf, size_t len);

/* C HAL이 C++ 코드를 "다시 부르는" 콜백. ctx 로 객체 포인터를 돌려준다. */
typedef void (*hal_rx_cb_t)(void *ctx, uint8_t byte);
void hal_uart_set_rx_callback(hal_rx_cb_t cb, void *ctx);

/* 테스트용: 바이트 수신 / NVIC가 벡터 테이블로 점프하는 상황을 흉내낸다 */
void hal_sim_rx_byte(uint8_t b);
void hal_sim_fire_irq(unsigned irqn);
enum { SYSTICK_IRQn = 0, USART1_IRQn = 1 };

#if defined(__cplusplus) && !defined(HAL_FORGET_EXTERN_C)
}  /* extern "C" */
#endif

#endif /* HAL_H */
