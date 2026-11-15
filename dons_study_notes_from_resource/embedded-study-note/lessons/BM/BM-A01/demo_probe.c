/* ENV: qemu */
/* BM-A01 demo_probe.c — QEMU mps2-an386 (Cortex-M4F) 사실 확인 펌웨어.
 * 레슨 본문의 "QEMU 는 이렇게 한다" 는 문장은 전부 이 탐침의 실제 출력에서 왔다.
 *   P1 CPUID / MVFR0 / MVFR1          P2 CPACR·FPCCR·FPCAR·FPDSCR·CONTROL 리셋값
 *   P3 CPACR 에 0xFFFFFFFF 쓰기        P4 CPACR=0 에서 VFP 명령 (USGFAULTENA=0 → ?)
 *   P5 같은 것, USGFAULTENA=1 → ?      P6 FP 사용 전/후 EXC_RETURN, CONTROL.FPCA, FPCCR.LSPACT
 *   P7 VTOR 쓰기 마스크                P8 외부 IRQ 수 (ICTR, ISER0/1)
 *   P9 CP10 만 켜고 CP11 은 끔         P10 FPCA=1 인데 CPACR=0 으로 예외 진입/복귀 (LSPEN=1, 0)
 * 빌드/실행: bash run_qemu.sh probe
 */
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ---------------- 세미호스팅 (BM-B08 축약판) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }
static void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };
    semi(0x20, blk);
    for (;;) { }
}

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define ICTR        REG32(0xE000E004u)
#define SYST_CSR    REG32(0xE000E010u)
#define NVIC_ISER0  REG32(0xE000E100u)
#define NVIC_ISER1  REG32(0xE000E104u)
#define NVIC_ICER0  REG32(0xE000E180u)
#define NVIC_ICER1  REG32(0xE000E184u)
#define SCB_CPUID   REG32(0xE000ED00u)
#define SCB_ICSR    REG32(0xE000ED04u)
#define SCB_VTOR    REG32(0xE000ED08u)
#define SCB_SHCSR   REG32(0xE000ED24u)
#define SCB_CFSR    REG32(0xE000ED28u)
#define SCB_HFSR    REG32(0xE000ED2Cu)
#define SCB_CPACR   REG32(0xE000ED88u)
#define FPU_FPCCR   REG32(0xE000EF34u)
#define FPU_FPCAR   REG32(0xE000EF38u)
#define FPU_FPDSCR  REG32(0xE000EF3Cu)
#define FPU_MVFR0   REG32(0xE000EF40u)
#define FPU_MVFR1   REG32(0xE000EF44u)
#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

static uint32_t get_control(void) { uint32_t c; __asm__ volatile("mrs %0, control" : "=r"(c)); return c; }

/* ---------------- fault 기록 + "고치고 다시" 정책 ---------------- */
typedef struct { uint32_t n, ipsr, cfsr, hfsr, pc, exc_return, fpccr; } frec_t;
static volatile frec_t g_f;

__attribute__((used)) void fault_c(uint32_t *frame, uint32_t exc_return) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_f.n++;
    g_f.ipsr = ipsr & 0x1FFu;
    g_f.cfsr = SCB_CFSR;
    g_f.hfsr = SCB_HFSR;
    g_f.pc = frame[6];
    g_f.exc_return = exc_return;
    g_f.fpccr = FPU_FPCCR;
    SCB_CFSR = g_f.cfsr;                     /* W1C */
    SCB_HFSR = g_f.hfsr;                     /* W1C */
    if (g_f.cfsr & (1u << 19)) {             /* NOCP: FPU 켜고 같은 명령 재실행 */
        SCB_CPACR |= 0xFu << 20;
    } else {
        frame[6] += 4u;                      /* 나머지: 32-bit 명령 건너뛰기 (탐침 전용) */
    }
    dsb(); isb();
    if (g_f.n > 20u) sh_exit(3);             /* 폭주 방지 */
}
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile(
        "tst   lr, #4      \n"
        "ite   eq          \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "mov   r1, lr      \n"
        "push  {r4, lr}    \n"
        "bl    fault_c     \n"
        "pop   {r4, pc}    \n");
}

static volatile uint32_t g_tick_excret, g_tick_fpccr, g_tick_n;
__attribute__((used)) void systick_c(uint32_t exc_return) {
    g_tick_excret = exc_return;
    g_tick_fpccr = FPU_FPCCR;
    g_tick_n++;
}
__attribute__((naked)) void SysTick_Handler(void) {
    __asm__ volatile("mov r0, lr\n push {r4, lr}\n bl systick_c\n pop {r4, pc}\n");
}

void Reset_Handler(void);
void Default_Handler(void) { sh_puts("unexpected exception\n"); sh_exit(2); }
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, Fault_Entry, Fault_Entry, Fault_Entry, Fault_Entry,
    0, 0, 0, 0, D, D, 0, D, SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};

extern const char pc_p4[], pc_p5[], pc_p9[];
__attribute__((noinline)) static uint32_t vfp_p4(uint32_t x) {
    uint32_t r;
    __asm__ volatile(".global pc_p4\npc_p4: vmov s0, %1\n vadd.f32 s0, s0, s0\n vmov %0, s0" : "=r"(r) : "r"(x) : "s0", "memory");
    return r;
}
__attribute__((noinline)) static uint32_t vfp_p5(uint32_t x) {
    uint32_t r;
    __asm__ volatile(".global pc_p5\npc_p5: vmov s0, %1\n vadd.f32 s0, s0, s0\n vmov %0, s0" : "=r"(r) : "r"(x) : "s0", "memory");
    return r;
}
__attribute__((noinline)) static uint32_t vfp_p9(uint32_t x) {
    uint32_t r;
    __asm__ volatile(".global pc_p9\npc_p9: vmov s0, %1\n vadd.f32 s0, s0, s0\n vmov %0, s0" : "=r"(r) : "r"(x) : "s0", "memory");
    return r;
}
static void show_f(const char *tag, uint32_t expect_pc) {
    sh_puts(tag); sh_puts(" faults="); sh_hex(g_f.n); sh_puts(" IPSR="); sh_hex(g_f.ipsr);
    sh_puts(" CFSR="); sh_hex(g_f.cfsr); sh_puts(" HFSR="); sh_hex(g_f.hfsr);
    sh_puts(" pc="); sh_hex(g_f.pc); sh_puts(g_f.pc == expect_pc ? " (=label)" : " (!=label)");
    sh_puts(" EXC_RETURN="); sh_hex(g_f.exc_return); sh_puts("\n");
}
static void pend_systick(void) { SCB_ICSR = 1u << 26; dsb(); isb(); }   /* PENDSTSET */

int main(void) {
    uint32_t cpacr0 = SCB_CPACR, fpccr0 = FPU_FPCCR, fpcar0 = FPU_FPCAR, fpdscr0 = FPU_FPDSCR;
    uint32_t ctrl0 = get_control(), vtor0 = SCB_VTOR, shcsr0 = SCB_SHCSR;
    sh_puts("P1 CPUID="); sh_hex(SCB_CPUID); sh_puts(" MVFR0="); sh_hex(FPU_MVFR0);
    sh_puts(" MVFR1="); sh_hex(FPU_MVFR1); sh_puts("\n");
    sh_puts("P2 reset: CPACR="); sh_hex(cpacr0); sh_puts(" FPCCR="); sh_hex(fpccr0);
    sh_puts(" FPCAR="); sh_hex(fpcar0); sh_puts(" FPDSCR="); sh_hex(fpdscr0);
    sh_puts(" CONTROL="); sh_hex(ctrl0); sh_puts(" VTOR="); sh_hex(vtor0); sh_puts(" SHCSR="); sh_hex(shcsr0); sh_puts("\n");

    /* P6a: FP 사용 전 예외의 EXC_RETURN */
    pend_systick();
    sh_puts("P6a before any FP: SysTick EXC_RETURN="); sh_hex(g_tick_excret);
    sh_puts(" CONTROL="); sh_hex(get_control()); sh_puts("\n");

    SCB_CPACR = 0xFFFFFFFFu; dsb(); isb();
    sh_puts("P3 CPACR after write 0xFFFFFFFF -> "); sh_hex(SCB_CPACR); sh_puts("\n");
    SCB_CPACR = 0u; dsb(); isb();

    /* P4: UsageFault 꺼진 채 (리셋 기본) */
    uint32_t r = vfp_p4(0x3FC00000u);                 /* 1.5f + 1.5f = 3.0f = 0x40400000 */
    show_f("P4 CPACR=0 USGFAULTENA=0:", (uint32_t)(uintptr_t)pc_p4);
    sh_puts("   result after fix-up retry="); sh_hex(r); sh_puts(" CPACR now="); sh_hex(SCB_CPACR);
    sh_puts(" CONTROL="); sh_hex(get_control()); sh_puts("\n");

    /* P6b: FP 사용 후 예외 */
    pend_systick();
    sh_puts("P6b after FP use: SysTick EXC_RETURN="); sh_hex(g_tick_excret);
    sh_puts(" FPCCR in handler="); sh_hex(g_tick_fpccr); sh_puts(" FPCAR="); sh_hex(FPU_FPCAR); sh_puts("\n");

    /* P5: UsageFault 켜고 */
    SCB_SHCSR |= 1u << 18;
    SCB_CPACR = 0u; dsb(); isb();
    g_f.n = 0;
    r = vfp_p5(0x3FC00000u);
    show_f("P5 CPACR=0 USGFAULTENA=1:", (uint32_t)(uintptr_t)pc_p5);
    sh_puts("   result after fix-up retry="); sh_hex(r); sh_puts("\n");

    /* P9: CP10 만 (CP11=00) */
    SCB_CPACR = 3u << 20; dsb(); isb();
    sh_puts("P9 CPACR write 0x00300000 -> "); sh_hex(SCB_CPACR);
    g_f.n = 0;
    r = vfp_p9(0x3FC00000u);
    sh_puts(" result="); sh_hex(r); sh_puts("\n");
    show_f("   ", (uint32_t)(uintptr_t)pc_p9);

    /* P10: FPCA=1 인 상태에서 CPACR=0 으로 만든 뒤 예외 (LSPEN=1 기본 → 지연 보존) */
    (void)vfp_p9(0x3FC00000u);                        /* FPCA=1 확보 */
    SCB_CPACR |= 0xFu << 20; dsb(); isb();
    (void)vfp_p9(0x3FC00000u);
    g_f.n = 0;
    SCB_CPACR = 0u; dsb(); isb();
    pend_systick();
    sh_puts("P10 LSPEN=1, CPACR=0, FPCA=1, SysTick: EXC_RETURN="); sh_hex(g_tick_excret);
    sh_puts(" faults="); sh_hex(g_f.n); sh_puts(" CPACR now="); sh_hex(SCB_CPACR); sh_puts("\n");
    SCB_CPACR |= 0xFu << 20; dsb(); isb();
    (void)vfp_p9(0x3FC00000u);
    FPU_FPCCR &= ~(1u << 30);                          /* LSPEN=0: 진입 때 바로 FP 레지스터 저장 */
    SCB_CPACR = 0u; dsb(); isb();
    g_f.n = 0;
    pend_systick();
    sh_puts("P10b LSPEN=0, CPACR=0, FPCA=1, SysTick: faults="); sh_hex(g_f.n);
    sh_puts(" IPSR="); sh_hex(g_f.ipsr); sh_puts(" CFSR="); sh_hex(g_f.cfsr);
    sh_puts(" FPCCR="); sh_hex(FPU_FPCCR); sh_puts(" CPACR now="); sh_hex(SCB_CPACR); sh_puts("\n");
    FPU_FPCCR |= 1u << 30;
    SCB_CPACR |= 0xFu << 20; dsb(); isb();

    /* P7: VTOR 쓰기 마스크 */
    SCB_VTOR = 0xFFFFFFFFu; sh_puts("P7 VTOR write 0xFFFFFFFF -> "); sh_hex(SCB_VTOR);
    SCB_VTOR = 0x20000040u; sh_puts(", 0x20000040 -> "); sh_hex(SCB_VTOR);
    SCB_VTOR = 0x20000080u; sh_puts(", 0x20000080 -> "); sh_hex(SCB_VTOR);
    SCB_VTOR = 0u; sh_puts("\n");

    /* P8: 외부 IRQ 수 */
    __asm__ volatile("cpsid i" ::: "memory");
    NVIC_ISER0 = 0xFFFFFFFFu; NVIC_ISER1 = 0xFFFFFFFFu;
    uint32_t e0 = NVIC_ISER0, e1 = NVIC_ISER1;
    NVIC_ICER0 = 0xFFFFFFFFu; NVIC_ICER1 = 0xFFFFFFFFu;
    __asm__ volatile("cpsie i" ::: "memory");
    sh_puts("P8 ICTR="); sh_hex(ICTR); sh_puts(" ISER0="); sh_hex(e0); sh_puts(" ISER1="); sh_hex(e1); sh_puts("\n");
    (void)SYST_CSR;
    return 0;
}

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
