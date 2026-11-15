/* BM-B11 demo: 같은 드라이버를 타깃(Cortex-M4)용으로 — UNIT_TEST 없이 REG_* = volatile MMIO
 * 빌드(어셈블리 보기): clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * 호스트 컴파일 확인:   cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_target.c -o /tmp/BM-B11_dt.o
 */
#include <stdint.h>

#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#define GPIO_IDR  0x10u
#define GPIO_ODR  0x14u
#define GPIO_BSRR 0x18u

/* 핀 번호가 컴파일 때 정해지는 흔한 경우 (LED = PA5) */
void led_on(void)  { REG_WRITE(0x40020000u + GPIO_BSRR, 1u << 5); }
void led_off(void) { REG_WRITE(0x40020000u + GPIO_BSRR, 1u << (5 + 16)); }

/* 정답: ODR 읽고 BSRR 로 그 핀만 */
void led_toggle_bsrr(void) {
    uint32_t odr = REG_READ(0x40020000u + GPIO_ODR);
    REG_WRITE(0x40020000u + GPIO_BSRR, (odr & (1u << 5)) ? (1u << 21) : (1u << 5));
}
/* 비교: ODR ^= — 16비트 전체를 다시 쓰는 RMW (다른 핀 경쟁은 BM-B04) */
void led_toggle_xor(void) {
    REG_WRITE(0x40020000u + GPIO_ODR, REG_READ(0x40020000u + GPIO_ODR) ^ (1u << 5));
}
/* 런타임 핀 번호 */
void gpio_write_rt(uint32_t port, unsigned pin, int level) {
    REG_WRITE(port + GPIO_BSRR, level ? (1u << pin) : (1u << (pin + 16u)));
}
int gpio_read_rt(uint32_t port, unsigned pin) {
    return (int)((REG_READ(port + GPIO_IDR) >> pin) & 1u);
}
