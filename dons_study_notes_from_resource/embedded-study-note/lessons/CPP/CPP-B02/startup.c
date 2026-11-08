/* CPP-B02 demo — startup 파일 흉내: 벡터 테이블 + weak 기본 핸들러.
 * 실제 STM32 startup_*.s 는
 *     .weak      USART1_IRQHandler
 *     .thumb_set USART1_IRQHandler, Default_Handler
 * 로 "약한 별칭(weak alias)" 을 만든다. ELF(arm-none-eabi, Linux)에서는 같은 일을
 * __attribute__((weak, alias)) 로 하고, alias 를 지원하지 않는 Mach-O(macOS)에서는
 * Default_Handler 를 부르는 weak 함수로 같은 효과를 낸다. */
#include "hal.h"
#include <stdio.h>

void Default_Handler(void) {
    /* 실제 칩: while (1) {}  — 디버거 없이 보면 "보드가 멈췄다" */
    printf("[startup] Default_Handler: unexpected IRQ!\n");
}

#ifdef __ELF__
void SysTick_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
#else
__attribute__((weak)) void SysTick_Handler(void)   { Default_Handler(); }
__attribute__((weak)) void USART1_IRQHandler(void) { Default_Handler(); }
#endif

/* 벡터 테이블: 링커가 "이름"으로 주소를 채운다 */
static void (*const g_vectors[])(void) = {
    [SYSTICK_IRQn] = SysTick_Handler,
    [USART1_IRQn]  = USART1_IRQHandler,
};

void hal_sim_fire_irq(unsigned irqn) { g_vectors[irqn](); }   /* NVIC 흉내 */
