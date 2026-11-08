/* C-M02 demo — 상태 레지스터 클리어가 실제로 몇 번의 버스 접근이 되는가 (관찰용, main 없음)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 *
 * 주소는 STM32F4 (RM0090): EXTI_PR = 0x40013C14 (rc_w1), TIM2_SR = 0x40000010 (rc_w0).
 * 대상 전용 코드라 <stdint.h> 외에는 아무것도 include 하지 않는다.
 */
#include <stdint.h>

#define EXTI_PR (*(volatile uint32_t *)0x40013C14u)
#define TIM2_SR (*(volatile uint32_t *)0x40000010u)

void exti_clear_bad(void)  { EXTI_PR |= 1u << 3; }    /* 읽기 + 쓰기: 모든 pending 을 지움 */
void exti_clear_good(void) { EXTI_PR = 1u << 3; }     /* 쓰기 1번: line 3 만 지움          */

void tim_clear_bad(void)   { TIM2_SR &= ~1u; }        /* 읽기~쓰기 사이에 생긴 플래그 유실  */
void tim_clear_good(void)  { TIM2_SR = ~1u; }         /* ST HAL __HAL_TIM_CLEAR_FLAG 와 같은 형태 */

uint32_t exti_ack_seen(void) {                        /* 본 것만 ack (Linux AHCI 식)      */
    uint32_t s = EXTI_PR;
    EXTI_PR = s;
    return s;
}
