/* ENV: qemu */
/* BM-B08 앱 쪽 핸들러 — startup 의 weak alias 를 "강한(strong) 정의" 로 덮어쓴다.
 * 이 파일이 링크에 들어오면 링커는 weak 인 SysTick_Handler / HardFault_Handler 대신 여기 것을 고른다.
 * (같은 번역 단위에서 weak alias 와 정의를 둘 다 쓰면 redefinition 에러 → 그래서 파일을 나눈다.)
 */
#include <stdint.h>

extern void sh_puts(const char *s);
extern void sh_hex(uint32_t v);
extern __attribute__((noreturn)) void sh_exit(int code);
extern volatile uint32_t g_ticks;

void SysTick_Handler(void) { g_ticks++; }

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* 예외 프레임: r0 r1 r2 r3 r12 lr pc xpsr (하드웨어가 자동으로 push) */
__attribute__((used, noreturn)) void hardfault_report(const uint32_t *frame) {
    sh_puts("HardFault! CFSR=");   sh_hex(REG32(0xE000ED28u));
    sh_puts(" HFSR=");             sh_hex(REG32(0xE000ED2Cu));
    sh_puts("\n  stacked pc=");    sh_hex(frame[6]);
    sh_puts(" xpsr=");             sh_hex(frame[7]);
    sh_puts(" lr=");               sh_hex(frame[5]);
    sh_puts("\n");
    sh_exit(3);
}

/* EXC_RETURN bit2 로 MSP/PSP 중 어느 스택에 프레임이 있는지 고른 뒤 C 로 넘긴다 */
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b hardfault_report \n");
}
