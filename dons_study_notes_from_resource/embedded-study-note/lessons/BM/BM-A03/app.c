/* ENV: qemu */
/* BM-A03 애플리케이션 — 앱 슬롯 0x00004000 에 링크 (app.ld). starter/solution 둘 다 이 같은 앱을 쓴다.
 *
 * 앱은 "부트로더를 믿지 않는다": 진입 첫 명령에서 레지스터 스냅샷을 찍어 계약을 검사하고,
 * 자기 SysTick 을 켜서 "내" 핸들러가 불리는지(부트로더 것이 아니라) 확인한다.
 * 관찰은 전부 메일박스(0x2000F800, boot_app.h)에 적고 SYSRESETREQ — 다음 부팅의 부트로더가 모아서 판정한다.
 *
 * 핸들러는 앱의 .data/.bss 를 쓰지 않는다: 준비(STAGE_READY) 전에 불릴 수 있기 때문 (부트로더가 정리를 잊으면).
 */
#include <stddef.h>
#include "boot_app.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
typedef void (*vector_t)(void);
extern const vector_t g_vectors[];

static uint32_t g_app_data = 0xA5A5A5A5u;   /* .data 가 제대로 복사됐나 확인용 */
static uint32_t g_app_bss;                  /* .bss 가 0 인가 확인용 */

/* ---------------- 핸들러 (메일박스만 만진다) ---------------- */
void SysTick_Handler(void) {
    if (MB->stage < STAGE_READY) { MB->early_ticks++; MB->early_stage = MB->stage; }
    else MB->app_ticks++;
}
void App_Irq_Handler(void) {                 /* 모든 외부 IRQ 공용: 앱은 아직 아무 IRQ 도 안 켰다 */
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    uint32_t irq = (ipsr & 0x1FFu) - 16u;
    NVIC_ICER(irq >> 5) = 1u << (irq & 31u); /* 폭주 방지: 들어온 줄은 끈다 */
    if (MB->stage < STAGE_READY) { MB->early_irqs++; MB->early_irq_num = irq; MB->early_stage = MB->stage; }
}
__attribute__((used, noreturn)) void app_fault(const uint32_t *frame, uint32_t ipsr) {
    MB->f_stage = MB->stage;
    MB->f_exc = ipsr & 0x1FFu;
    MB->f_cfsr = SCB_CFSR;
    MB->f_hfsr = SCB_HFSR;
    MB->f_pc = frame[6];
    MB->stage = STAGE_FAULT;
    dsb();
    MB->magic = MB_MAGIC;                    /* 내용 먼저, magic 은 마지막 (BM-M13 과 같은 순서) */
    dsb();
    sh_puts("  [app] FAULT exc="); sh_dec(MB->f_exc); sh_puts(" CFSR="); sh_hex(MB->f_cfsr);
    sh_puts(" HFSR="); sh_hex(MB->f_hfsr); sh_puts(" stacked PC="); sh_hex(MB->f_pc); sh_puts(" -> reset\n");
    system_reset();
}
__attribute__((naked)) void App_Fault_Entry(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mrs r1, ipsr\n b app_fault\n");
}

/* ---------------- 계약 검사 (practice.c 에 같은 함수의 호스트 판이 있다) ---------------- */
enum {
    V_VTOR = 1u << 0, V_MSP = 1u << 1, V_PRIMASK = 1u << 2, V_CONTROL = 1u << 3, V_SYSTICK = 1u << 4,
    V_PENDST = 1u << 5, V_NVIC_EN = 1u << 6, V_NVIC_PEND = 1u << 7, V_UART = 1u << 8, V_EARLY = 1u << 9,
};
static const char *const k_vname[10] = { "VTOR", "MSP", "PRIMASK", "CONTROL", "SYSTICK_ON", "PENDST",
                                         "NVIC_EN", "NVIC_PEND", "UART_ON", "EARLY_IRQ" };
static uint32_t contract_check(uint32_t own_vtor, uint32_t own_top) {
    uint32_t v = 0;
    if (MB->e_vtor != own_vtor) v |= V_VTOR;
    if (MB->e_msp != own_top) v |= V_MSP;
    if (MB->e_primask != 0u) v |= V_PRIMASK;
    if (MB->e_control != 0u) v |= V_CONTROL;
    if (MB->e_syst_csr & (SYST_ENABLE | SYST_TICKINT)) v |= V_SYSTICK;
    if (MB->e_icsr & ICSR_PENDSTSET) v |= V_PENDST;
    if (MB->e_iser0 != 0u) v |= V_NVIC_EN;
    if (MB->e_ispr0 != 0u) v |= V_NVIC_PEND;
    if (MB->e_uart_ctrl != 0u) v |= V_UART;
    if (MB->early_ticks + MB->early_irqs != 0u) v |= V_EARLY;
    return v;
}

static void app_main(void) {
    uint32_t own_vtor = (uint32_t)(uintptr_t)g_vectors, own_top = (uint32_t)(uintptr_t)_estack;
    sh_puts("  [app] entry: VTOR="); sh_hex(MB->e_vtor); sh_puts(" MSP="); sh_hex(MB->e_msp);
    sh_puts(" PRIMASK="); sh_dec(MB->e_primask); sh_puts(" CONTROL="); sh_dec(MB->e_control);
    sh_puts(" SYST_CSR="); sh_hex(MB->e_syst_csr); sh_puts(" ICSR="); sh_hex(MB->e_icsr);
    sh_puts("\n          ISER0="); sh_hex(MB->e_iser0); sh_puts(" ISPR0="); sh_hex(MB->e_ispr0);
    sh_puts(" UART0.CTRL="); sh_hex(MB->e_uart_ctrl); sh_puts(" early ticks/irqs="); sh_dec(MB->early_ticks);
    sh_puts("/"); sh_dec(MB->early_irqs); sh_puts(" .data="); sh_hex(g_app_data); sh_puts(" .bss=");
    sh_hex(g_app_bss); sh_puts("\n");
    uint32_t v = contract_check(own_vtor, own_top);
    sh_puts("  [app] contract: ");
    if (v == 0u) sh_puts("OK");
    for (uint32_t i = 0; i < 10u; ++i) if (v & (1u << i)) { sh_puts(k_vname[i]); sh_puts(" "); }
    sh_puts("\n");

    /* 내 SysTick: 리셋값에 기대지 않고 RVR/CVR/CSR 을 전부 쓴다. 핸들러가 "내 것" 인지는 메일박스 카운터로 */
    uint32_t boot0 = MB->boot_ticks;
    MB->app_ticks = 0u;
    SYST_RVR = TICK_RELOAD;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CLKSOURCE | SYST_TICKINT | SYST_ENABLE;
    for (uint32_t i = 0; i < 3000000u && MB->app_ticks < 3u; ++i) { __asm__ volatile("nop"); }
    SYST_CSR = 0u;
    SCB_ICSR = ICSR_PENDSTCLR;
    MB->boot_ticks_in_app = MB->boot_ticks - boot0;
    sh_puts("  [app] SysTick test: app handler "); sh_dec(MB->app_ticks); sh_puts("x, bootloader handler ");
    sh_dec(MB->boot_ticks_in_app); sh_puts("x\n");

    MB->stage = STAGE_DONE;
    dsb();
    MB->magic = MB_MAGIC;
    dsb();
    system_reset();                          /* 다음 부팅의 부트로더가 결과를 모은다 */
}

/* 진입: 레지스터를 "건드리기 전에" 읽어야 하므로 naked — 첫 세 명령이 MSP/PRIMASK/CONTROL 을 r0–r2 로 */
__attribute__((used, noreturn)) void app_entry(uint32_t msp, uint32_t primask, uint32_t control) {
    MB->stage = STAGE_ENTERED;
    MB->e_msp = msp;
    MB->e_primask = primask;
    MB->e_control = control;
    MB->e_vtor = SCB_VTOR;
    MB->e_syst_csr = SYST_CSR;
    MB->e_icsr = SCB_ICSR;
    MB->e_iser0 = NVIC_ISER(0);
    MB->e_ispr0 = NVIC_ISPR(0);
    MB->e_uart_ctrl = UART0_CTRL;
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    MB->stage = STAGE_READY;
    app_main();
    for (;;) { }
}
__attribute__((naked, noreturn)) void Reset_Handler(void) {
    __asm__ volatile("mrs r0, msp\n mrs r1, primask\n mrs r2, control\n b app_entry\n");
}

#define F App_Fault_Entry
#define I App_Irq_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[APP_NVEC] = {
    (vector_t)_estack, Reset_Handler, F, F, F, F, F, 0, 0, 0, 0, F, F, 0, F, SysTick_Handler,
    I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,
    I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,
};
