/* ENV: qemu */
/* BM-B09 데모 — printf → libgloss(rdimon) _write → 세미호스팅 SYS_WRITE (BKPT 0xAB) → QEMU 가 호스트 stdout 에 쓴다.
 * _write 를 우리가 안 쓴다: --specs=rdimon.specs 가 libc_nano + librdimon_nano(세미호스팅 syscall 들)를 링크.
 *   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror \
 *       --specs=nano.specs --specs=rdimon.specs -nostartfiles -T mps2_an385.ld demo_semihost.c -o semi.elf
 *   (1) 디버거/호스트 있음:  qemu-system-arm -M mps2-an385 ... -semihosting-config enable=on,target=native -kernel semi.elf
 *   (2) 디버거/호스트 없음:  같은 이미지를 -semihosting 없이 → BKPT 0xAB 가 HardFault 로 → UART0 로 덤프
 * -DBUG_NO_INIT_HANDLES : initialise_monitor_handles() 를 빼먹으면? (rdimon-crt0 를 안 쓰면 직접 불러야 한다)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define UART0_BASE      0x40004000u
#define UART_DATA       REG32(UART0_BASE + 0x00u)
#define UART_STATE      REG32(UART0_BASE + 0x04u)
#define UART_CTRL       REG32(UART0_BASE + 0x08u)
#define UART_BAUDDIV    REG32(UART0_BASE + 0x10u)
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SCB_DFSR        REG32(0xE000ED30u)
#define DHCSR           REG32(0xE000EDF0u)   /* bit0 C_DEBUGEN: 디버거가 halting debug 를 켰나 */

extern void initialise_monitor_handles(void);   /* librdimon: SYS_OPEN(":tt") 로 stdin/stdout/stderr 핸들 */

/* ---------- 폴트 덤프용 최소 UART (세미호스팅이 죽었을 때 유일한 출구) ---------- */
static void fault_putc(char c) {
    for (uint32_t n = 0; (UART_STATE & 1u) && n < 100000u; n++) { }
    UART_DATA = (uint8_t)c;
}
static void fault_puts(const char *s) {
    for (; *s; s++) { if (*s == '\n') fault_putc('\r'); fault_putc(*s); }
}
static void fault_hex(uint32_t v) {
    fault_puts("0x");
    for (int i = 28; i >= 0; i -= 4) fault_putc("0123456789ABCDEF"[(v >> i) & 0xFu]);
}
void hardfault_c(const uint32_t *frame) {
    UART_BAUDDIV = 217u;                          /* 25 MHz / 115200 */
    UART_CTRL = 1u;                               /* TX_EN */
    uint32_t pc = frame[6];
    uint16_t insn = *(const volatile uint16_t *)(uintptr_t)pc;
    fault_puts("\n!! HardFault  PC=");      fault_hex(pc);
    fault_puts(" insn=");                    fault_hex(insn);
    fault_puts("\n   HFSR=");               fault_hex(SCB_HFSR);
    fault_puts(" CFSR=");                    fault_hex(SCB_CFSR);
    fault_puts(" DFSR=");                    fault_hex(SCB_DFSR);
    fault_puts(" DHCSR=");                   fault_hex(DHCSR);
    fault_puts("\n   r0(op)=");             fault_hex(frame[0]);
    fault_puts(" r1(arg)=");                 fault_hex(frame[1]);
    fault_puts("\n");
    if (insn == 0xBEABu) fault_puts("   -> BKPT 0xAB = semihosting call, but no debugger/host answered it\n");
    SCB_AIRCR = 0x05FA0004u;                      /* SYSRESETREQ → -no-reboot 이면 QEMU 종료 */
    for (;;) { }
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardfault_c\n");
}
void Default_Handler(void) { for (;;) { } }

/* ---------- startup (BM-B08 축약판) ---------- */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
extern void __libc_init_array(void);
int main(void);
void _init(void) { }
void _fini(void) { }
void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;) *d++ = 0u;
#ifndef BUG_NO_INIT_HANDLES
    initialise_monitor_handles();                 /* rdimon-crt0 가 해 주던 일 (-nostartfiles 라 직접) */
#endif
    __libc_init_array();
    exit(main());                                 /* librdimon _exit → SYS_EXIT → QEMU 종료 코드 */
}
__attribute__((section(".isr_vector"), used)) const uintptr_t g_vectors[16] = {
    (uintptr_t)_estack, (uintptr_t)Reset_Handler, (uintptr_t)Default_Handler, (uintptr_t)HardFault_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};

int main(void) {
    int n = printf("Hello from printf -> rdimon _write -> semihosting SYS_WRITE\n");
    printf("dec=%d hex=0x%08lX str=%s (printf returned %d)\n", 42, 0xDEADBEEFul, "ok", n);
    fprintf(stderr, "this line went to stderr\n");
    printf("DHCSR=0x%08lX (C_DEBUGEN=%lu)\n", (unsigned long)DHCSR, (unsigned long)(DHCSR & 1u));
    return n == 60 ? 3 : 1;                       /* 3 = "정상" 표시용 종료 코드 (호스트 셸에 그대로 전달되나 확인) */
}
