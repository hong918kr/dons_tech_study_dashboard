/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 startup.c — 벡터 표 + Reset_Handler (.data 복사, .bss 0) + SysTick + 세미호스팅 종료.
 * 부팅 과정 설명은 BM-B08, 링커 스크립트 문법은 TOOL-M01. 이 레슨에서 중요한 것은 하나:
 *   vectors[] 는 아무 코드도 "참조" 하지 않는다 (CPU 가 리셋 때 주소 0 에서 읽을 뿐).
 *   그래서 --gc-sections 는 링커 스크립트의 KEEP() 없이는 이 표를 쓰레기로 본다.
 */
#include "fw.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

volatile uint32_t g_ticks;

#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

void Reset_Handler(void) {
    uint32_t *src = _sidata, *dst = _sdata;
    while (dst < _edata) *dst++ = *src++;       /* .data: Flash(LMA) → RAM(VMA) */
    for (dst = _sbss; dst < _ebss;) *dst++ = 0; /* .bss: 0 채우기 */
    board_exit(main());
}

static int semihost(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

void board_exit(int code) { /* QEMU -semihosting: SYS_EXIT_EXTENDED → qemu 종료 코드 = code */
    uint32_t blk[2] = {0x20026u, (uint32_t)code}; /* ADP_Stopped_ApplicationExit */
    semihost(0x20, blk);
    for (;;) {
    }
}

void Default_Handler(void) { board_exit(3); } /* 예상 못 한 예외 → 종료 코드 3 (멈추지 않게) */
void HardFault_Handler(void) { board_exit(4); }
void SysTick_Handler(void) { g_ticks++; }

void systick_start(uint32_t reload) {
    SYST_RVR = reload - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 7u; /* ENABLE | TICKINT | CLKSOURCE=CPU */
}

/* Cortex-M 벡터 표: [0] 초기 SP, [1] Reset, [2..15] 시스템 예외, [16..] IRQ 8개 */
__attribute__((section(".isr_vector"), used))
const uintptr_t vectors[24] = {
    (uintptr_t)_estack,           (uintptr_t)Reset_Handler,
    (uintptr_t)Default_Handler,   (uintptr_t)HardFault_Handler, /* NMI, HardFault */
    (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,   /* MemManage, BusFault */
    (uintptr_t)Default_Handler,   0, 0, 0, 0,                   /* UsageFault, reserved ×4 */
    (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,   /* SVCall, DebugMon */
    0,                            (uintptr_t)Default_Handler,   /* reserved, PendSV */
    (uintptr_t)SysTick_Handler,                                 /* SysTick */
    (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,   (uintptr_t)Default_Handler,   /* IRQ0..7 */
};
