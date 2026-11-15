/* BM-B04 demo: 같은 드라이버를 타깃(MMIO) 형태로 — 명령어 수준에서 RMW 창 보기
 *
 * 맥에서 Cortex-M4 asm 만 뽑는다 (링크·실행 없음):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_rmw_asm.c -o -
 * 호스트 컴파일 확인용:
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_rmw_asm.c -o /tmp/BM-B04_demo.o
 * 주소는 RM0090(STM32F4) 기준 대표값 — RM 확인 권장.
 */
#include <stdint.h>

#define REG(addr)   (*(volatile uint32_t *)(uintptr_t)(addr))
#define GPIOA_ODR   REG(0x40020014u)
#define GPIOA_BSRR  REG(0x40020018u)
#define EXTI_PR     REG(0x40013C14u)
#define TIM2_SR     REG(0x40000010u)
#define TIM_SR_UIF  (1u << 0)

void odr_set_rmw(uint32_t pins)   { GPIOA_ODR |= pins; }          /* LDR / ORR / STR — 창 있음 */
void odr_set_bsrr(uint32_t pins)  { GPIOA_BSRR = pins; }          /* STR 하나 */
void odr_field_bsrr(uint32_t mask, uint32_t v) {                   /* mask 안 핀들을 v 로, STR 하나 */
    GPIOA_BSRR = ((mask & ~v) << 16) | (mask & v);
}
void exti_clear_wrong(uint32_t line) { EXTI_PR |= 1u << line; }   /* W1C 에 |=: 읽은 펜딩 전부 지움 */
void exti_clear_right(uint32_t line) { EXTI_PR = 1u << line; }    /* 그 비트만 */
void tim_clear_wrong(void) { TIM2_SR &= ~TIM_SR_UIF; }             /* rc_w0 에 &=: 창 있음 */
void tim_clear_right(void) { TIM2_SR = ~TIM_SR_UIF; }              /* 그 비트만 0 */

#if defined(__arm__)
/* CMSIS __get_PRIMASK / __disable_irq / __set_PRIMASK 와 같은 일 */
static inline uint32_t cs_enter(void) {
    uint32_t p;
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r"(p) :: "memory");
    return p;
}
static inline void cs_exit(uint32_t p) { __asm volatile ("msr primask, %0" :: "r"(p) : "memory"); }
void odr_set_cs(uint32_t pins) { uint32_t p = cs_enter(); GPIOA_ODR |= pins; cs_exit(p); }
#endif
