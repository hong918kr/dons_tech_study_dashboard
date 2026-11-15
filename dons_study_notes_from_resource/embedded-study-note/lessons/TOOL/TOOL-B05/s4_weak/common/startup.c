/* startup.c — Cortex-M 스타트업 파일 흉내 (벡터 테이블 + weak 기본 핸들러)
 * 실제 startup_stm32xxx.s 처럼, 앱이 같은 이름의 함수를 정의하면 그게 이긴다. */
#include "irq.h"

volatile unsigned g_default_hits;
volatile int g_unhandled_vec = -1;
static volatile int g_active_vec = -1;     /* 실제 칩: IPSR / SCB->ICSR.VECTACTIVE */

void Default_Handler(void)
{
    g_default_hits++;
    g_unhandled_vec = g_active_vec;
    /* 실제 펌웨어: for (;;) {}  — 여기서 멈추고 워치독/디버거를 기다린다 */
}

#if defined(__APPLE__)
/* Mach-O 에는 alias 속성이 없다 → weak "정의" 로 같은 효과 */
#define WEAK_DEFAULT(name) \
    __attribute__((weak)) void name(void) { Default_Handler(); }
#else
/* ELF (arm-none-eabi-gcc, CMSIS startup 의 C 버전): 이름만 Default_Handler 에 붙인다 */
#define WEAK_DEFAULT(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")));
#endif

WEAK_DEFAULT(NMI_Handler)
WEAK_DEFAULT(HardFault_Handler)
WEAK_DEFAULT(SysTick_Handler)
WEAK_DEFAULT(USART1_IRQHandler)
WEAK_DEFAULT(TIM2_IRQHandler)

void Reset_Handler(void)
{
    /* 실제: .data 복사, .bss 0 채우기, SystemInit, main (BM-B08) */
    for (;;) {
    }
}

__attribute__((used))
void (*const g_vectors[VEC_COUNT])(void) = {
    Reset_Handler, NMI_Handler, HardFault_Handler, SysTick_Handler,
    USART1_IRQHandler, TIM2_IRQHandler,
};

void nvic_fire(int vec)
{
    g_active_vec = vec;
    g_vectors[vec]();
    g_active_vec = -1;
}
