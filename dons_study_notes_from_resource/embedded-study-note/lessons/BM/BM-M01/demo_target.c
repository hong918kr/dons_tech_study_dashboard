/* BM-M01 demo — 같은 NVIC 코드를 Cortex-M4 타깃으로 컴파일해 명령 수준에서 본다 (링크·실행 없음).
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -S demo_target.c -o -
 */
#include <stdint.h>
#define NVIC_ISER ((volatile uint32_t *)0xE000E100u)
#define NVIC_ICER ((volatile uint32_t *)0xE000E180u)
#define NVIC_IPRW ((volatile uint32_t *)0xE000E400u)   /* 워드 배열로 본 IPR */
#define NVIC_IPRB ((volatile uint8_t  *)0xE000E400u)   /* 바이트 배열로 본 IPR (CMSIS NVIC->IP[n]) */
#define TIM2_SR   (*(volatile uint32_t *)0x40000010u)
extern volatile uint32_t g_ticks;

void enable_irq(uint32_t n)       { NVIC_ISER[n >> 5] = 1u << (n & 31u); }            /* STR 한 번 */
void disable_irq_bug(uint32_t n)  { NVIC_ICER[n >> 5] |= 1u << (n & 31u); }           /* LDR/ORR/STR */
void set_prio_byte(uint32_t n, uint32_t p) { NVIC_IPRB[n] = (uint8_t)(p << 4); }      /* STRB 한 번 */
void set_prio_word(uint32_t n, uint32_t p) {                                           /* 워드 RMW */
    uint32_t sh = 8u * (n & 3u);
    NVIC_IPRW[n >> 2] = (NVIC_IPRW[n >> 2] & ~(0xFFu << sh)) | ((p << 4) << sh);
}
void TIM2_IRQHandler_late(void) {                    /* 일하고 끝에서 지움 */
    if (TIM2_SR & 1u) { g_ticks++; TIM2_SR = ~1u; }
}
void TIM2_IRQHandler_dsb(void) {                     /* 끝에서 지우고 DSB */
    if (TIM2_SR & 1u) { g_ticks++; TIM2_SR = ~1u; __asm volatile("dsb" ::: "memory"); }
}
void TIM2_IRQHandler_fixed(void) {                   /* 먼저 지우고, 끝에서 read-back */
    if ((TIM2_SR & 1u) == 0u) return;
    TIM2_SR = ~1u;
    g_ticks++;
    (void)TIM2_SR;
}
