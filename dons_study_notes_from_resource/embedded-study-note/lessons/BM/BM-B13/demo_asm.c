/* BM-B13 demo: 타깃(Cortex-M4)에서 이 레슨의 코드가 어떤 명령어가 되나 — 관찰용, 실행하지 않는다.
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * (맥 호스트 확인: cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_asm.c -o /tmp/BM-B13_demo.o)
 */
#include <stdbool.h>
#include <stdint.h>

#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

volatile uint32_t g_ticks;          /* 올바름: ISR 이 바꾸는 변수 */
uint32_t g_ticks_nv;                /* 버그: volatile 빠짐 */

void SysTick_Handler(void) { g_ticks++; }

void systick_init_1k_168m(void) {
    SYST_CSR = 0u;
    SYST_RVR = 168000000u / 1000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 7u;                  /* CLKSOURCE | TICKINT | ENABLE */
}

bool time_after_eq(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
bool time_after_eq_naive(uint32_t now, uint32_t deadline) { return now >= deadline; }

void wait_ticks(uint32_t wait) {    /* delay_ms 의 루프 부분 (wait = ms + 1), volatile g_ticks */
    uint32_t start = g_ticks;
    while ((uint32_t)(g_ticks - start) < wait) { }
}
void delay_ms_nonvolatile(uint32_t ms) {   /* volatile 이 없으면? */
    uint32_t start = g_ticks_nv;
    while ((uint32_t)(g_ticks_nv - start) < ms) { }
}
