/* BM-M11 demo_m4.c — 슬립 진입 패턴이 Cortex-M 명령어로 무엇이 되나 (타깃 전용, libc 없음)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_m4.c -o -
 *
 * CMSIS-Core 의 __disable_irq/__enable_irq/__WFI/__WFE/__SEV/__DSB/__ISB 와 같은 inline asm 을
 * 직접 정의한다 (CMSIS 헤더 없이). "memory" 클로버가 있어야 g_flag 읽기가 그 사이에 머문다.
 */
#include <stdint.h>

#define __disable_irq() __asm volatile ("cpsid i" ::: "memory")
#define __enable_irq()  __asm volatile ("cpsie i" ::: "memory")
#define __WFI()         __asm volatile ("wfi" ::: "memory")
#define __WFE()         __asm volatile ("wfe" ::: "memory")
#define __SEV()         __asm volatile ("sev" ::: "memory")
#define __DSB()         __asm volatile ("dsb 0xF" ::: "memory")
#define __ISB()         __asm volatile ("isb 0xF" ::: "memory")

#define SCB_SCR (*(volatile uint32_t *)0xE000ED10u)   /* System Control Register */
#define SCR_SLEEPONEXIT (1u << 1)
#define SCR_SLEEPDEEP   (1u << 2)
#define SCR_SEVONPEND   (1u << 4)

volatile uint32_t g_flag;           /* ISR 가 1 로 세운다 */

/* ① 레이스 있음: 확인과 WFI 사이에 IRQ 가 오면 다음 깨움까지 잠든다 */
void idle_naive(void) {
    if (!g_flag) __WFI();
}

/* ② 정석: 인터럽트를 끈 채 확인 → WFI (PRIMASK=1 이어도 펜딩 IRQ 가 WFI 를 끝낸다) → 켬 */
void idle_correct(void) {
    __disable_irq();
    if (!g_flag) {
        __DSB();                    /* 앞선 메모리 쓰기 완료 후 잠들기 (FreeRTOS 포트와 같은 습관) */
        __WFI();
        __ISB();
    }
    __enable_irq();                 /* 여기서 펜딩 ISR 이 실행된다 */
}

/* ③ 틀린 고침: 확인은 막고 했지만 WFI 전에 켜 버린다 → 창이 다시 열림 */
void idle_enable_first(void) {
    __disable_irq();
    uint32_t f = g_flag;
    __enable_irq();
    if (!f) __WFI();
}

/* ④ WFE 루프: ISR 진입/복귀가 이벤트 레지스터를 세우므로 확인~WFE 사이 IRQ 는 WFE 를 즉시 통과시킨다 */
void idle_wfe(void) {
    while (!g_flag) __WFE();
}

/* ⑤ Stop 같은 딥슬립: SLEEPDEEP 을 세운 뒤 같은 패턴 */
void enter_deepsleep(void) {
    SCB_SCR |= SCR_SLEEPDEEP;
    idle_correct();
    SCB_SCR &= ~SCR_SLEEPDEEP;
}

/* ⑥ 인터럽트 구동 앱: SLEEPONEXIT — ISR 이 끝나면 스레드로 돌아오지 않고 다시 잔다 */
void sleep_on_exit_forever(void) {
    SCB_SCR |= SCR_SLEEPONEXIT;
    for (;;) __WFI();
}

/* ⑦ ISR: 플래그 + (멀티코어/다른 마스터를 위해) SEV */
void EXTI0_IRQHandler(void) {
    g_flag = 1u;
    __SEV();
}
