/* ENV: qemu */
/* BM-A03 demo_probe — 부트로더의 정리 절차를 짜기 "전에" QEMU 10.2.1 mps2-an385 가 실제로 어떻게 하는지 확인.
 *   P1 NVIC_ICTR: 외부 IRQ 줄이 몇 개인가 → ICER/ICPR 를 몇 워드 써야 하나
 *   P2 SysTick: CTRL=0 이 이미 걸린 pending(PENDSTSET)을 지우나? PENDSTCLR 은?
 *   P3 NVIC: ICER(끄기)가 pending 을 지우나? 끈 채 남은 pending 은 나중에 ISER 하는 순간 터지나?
 *   P4 VTOR: 어떤 비트가 쓰기 가능한가 (정렬 규칙과 비교)
 *   P5 CMSDK UART0 TX 인터럽트 = level 신호인가? 장치가 신호를 들고 있으면 ICPR 이 먹히나?
 *   P6 짝수 주소로 BX (Thumb 비트 0) → 무엇이 일어나나 (CFSR/HFSR/쌓인 PC)
 * 빌드/실행: run_qemu.sh probe   (boot.ld 사용, 부트로더 자리 0x00000000 에서 단독 실행)
 */
#include "boot_app.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

static volatile uint32_t g_tick, g_irq7, g_irq1;
void SysTick_Handler(void) { g_tick++; }
void Irq7_Handler(void) { g_irq7++; }
void Irq1_Handler(void) { g_irq1++; NVIC_ICER(0) = 1u << 1; }   /* 폭주 방지: 한 번 들어오면 끈다 */

__attribute__((used)) void probe_fault(const uint32_t *frame) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("P6 fault taken: IPSR="); sh_dec(ipsr); sh_puts(" CFSR="); sh_hex(SCB_CFSR);
    sh_puts(" HFSR="); sh_hex(SCB_HFSR); sh_puts(" stacked PC="); sh_hex(frame[6]);
    sh_puts(" stacked xPSR="); sh_hex(frame[7]); sh_puts("\n");
    sh_exit(0);
}
__attribute__((naked)) void Fault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b probe_fault\n");
}

static void wait_countflag(void) {
    for (uint32_t i = 0; i < 1000000u; ++i)
        if (SYST_CSR & SYST_COUNTFLAG) return;
    sh_puts("  (COUNTFLAG timeout)\n");
}
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; ++i) { } }
__attribute__((noinline)) static void target_fn(void) { sh_puts("P6 target_fn ran (no fault)\n"); }

int main(void) {
    sh_puts("== BM-A03 QEMU probe (mps2-an385) ==\n");
    /* P1 */
    uint32_t ictr = NVIC_ICTR;
    sh_puts("P1 ICTR="); sh_hex(ictr); sh_puts(" -> lines="); sh_dec(((ictr & 0xFu) + 1u) * 32u); sh_puts("\n");

    /* P2 */
    __asm__ volatile("cpsid i" ::: "memory");
    SYST_RVR = TICK_RELOAD; SYST_CVR = 0; SYST_CSR = SYST_CLKSOURCE | SYST_TICKINT | SYST_ENABLE;
    wait_countflag();
    sh_puts("P2 masked, COUNTFLAG seen: ICSR.PENDSTSET="); sh_dec((SCB_ICSR >> 26) & 1u);
    SYST_CSR = 0;
    sh_puts(" | after CTRL=0: PENDSTSET="); sh_dec((SCB_ICSR >> 26) & 1u);
    __asm__ volatile("cpsie i" ::: "memory"); isb();
    spin(1000);
    sh_puts(" | cpsie -> handler ran "); sh_dec(g_tick); sh_puts("x\n");
    __asm__ volatile("cpsid i" ::: "memory");
    SYST_CSR = SYST_CLKSOURCE | SYST_TICKINT | SYST_ENABLE;
    wait_countflag();
    SYST_CSR = 0;
    SCB_ICSR = ICSR_PENDSTCLR;
    sh_puts("P2 CTRL=0 + PENDSTCLR: PENDSTSET="); sh_dec((SCB_ICSR >> 26) & 1u);
    g_tick = 0;
    __asm__ volatile("cpsie i" ::: "memory"); isb();
    spin(1000);
    sh_puts(" | cpsie -> handler ran "); sh_dec(g_tick); sh_puts("x\n");

    /* P3 */
    __asm__ volatile("cpsid i" ::: "memory");
    NVIC_ISER(0) = 1u << BOOT_IRQ; NVIC_ISPR(0) = 1u << BOOT_IRQ;
    sh_puts("P3 IRQ7 en+pend (masked): ISPR0="); sh_hex(NVIC_ISPR(0));
    NVIC_ICER(0) = 1u << BOOT_IRQ;
    sh_puts(" | after ICER: ISER0="); sh_hex(NVIC_ISER(0)); sh_puts(" ISPR0="); sh_hex(NVIC_ISPR(0));
    __asm__ volatile("cpsie i" ::: "memory"); isb();
    spin(100);
    sh_puts(" | cpsie: ran "); sh_dec(g_irq7); sh_puts("x\n");
    NVIC_ISER(0) = 1u << BOOT_IRQ; dsb(); isb();
    sh_puts("P3 later ISER (stale pending): ran "); sh_dec(g_irq7); sh_puts("x");
    NVIC_ICER(0) = 1u << BOOT_IRQ;
    g_irq7 = 0;
    __asm__ volatile("cpsid i" ::: "memory");
    NVIC_ISPR(0) = 1u << BOOT_IRQ; NVIC_ICPR(0) = 1u << BOOT_IRQ;
    NVIC_ISER(0) = 1u << BOOT_IRQ;
    __asm__ volatile("cpsie i" ::: "memory"); isb();
    sh_puts(" | ISPR then ICPR then ISER: ran "); sh_dec(g_irq7); sh_puts("x\n");
    NVIC_ICER(0) = 1u << BOOT_IRQ;

    /* P4 */
    static const uint32_t vt_try[] = { 0x00004000u, 0x00004004u, 0x00004080u, 0x00004100u, 0xFFFFFFFFu };
    sh_puts("P4 VTOR write->read:");
    for (unsigned i = 0; i < sizeof vt_try / sizeof vt_try[0]; ++i) {
        SCB_VTOR = vt_try[i]; dsb();
        sh_puts(" "); sh_hex(vt_try[i]); sh_puts("->"); sh_hex(SCB_VTOR);
    }
    SCB_VTOR = 0; dsb(); isb();
    sh_puts("\n");

    /* P5 */
    __asm__ volatile("cpsid i" ::: "memory");
    UART0_BAUDDIV = 16u;
    UART0_CTRL = (1u << 0) | (1u << 2);              /* TX_EN | TX_INTEN */
    UART0_DATA = 'x';
    spin(1000);
    sh_puts("P5 UART0 TX: CTRL="); sh_hex(UART0_CTRL); sh_puts(" INTSTATUS="); sh_hex(UART0_INTSTATUS);
    NVIC_ISER(0) = 1u << 1;
    sh_puts(" | ISPR0="); sh_hex(NVIC_ISPR(0));
    NVIC_ICPR(0) = 1u << 1;
    sh_puts(" | after ICPR (line still high): ISPR0="); sh_hex(NVIC_ISPR(0)); sh_puts("\n");
    UART0_INTSTATUS = 1u;                             /* 장치 쪽 원인을 지운다 (W1C) */
    UART0_CTRL = 0;
    NVIC_ICPR(0) = 1u << 1;
    sh_puts("P5 UART INTCLEAR + CTRL=0, then ICPR: INTSTATUS="); sh_hex(UART0_INTSTATUS);
    sh_puts(" ISPR0="); sh_hex(NVIC_ISPR(0));
    __asm__ volatile("cpsie i" ::: "memory"); isb();
    spin(100);
    sh_puts(" | cpsie: IRQ1 ran "); sh_dec(g_irq1); sh_puts("x\n");
    NVIC_ICER(0) = 1u << 1;

    /* P6 */
    uint32_t even = (uint32_t)(uintptr_t)target_fn & ~1u;
    sh_puts("P6 BX to "); sh_hex(even); sh_puts(" (target_fn with bit0 cleared), SHCSR="); sh_hex(SCB_SHCSR);
    sh_puts("\n");
    __asm__ volatile("blx %0" :: "r"(even) : "memory", "lr");
    sh_puts("P6 returned?!\n");
    return 1;
}

void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: exception "); sh_dec(ipsr); sh_puts("\n");
    sh_exit(2);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler,
    0, 0, 0, 0, D, D, 0, D, SysTick_Handler,
    D, Irq1_Handler, D, D, D, D, D, Irq7_Handler, D, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};
__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
