/* ENV: qemu */
/* BM-A10 QEMU 확인 펌웨어 — "보드 측정 코드를 QEMU 에서 돌리면 무엇이 나오나?"
 *
 * 대상: qemu-system-arm -M mps2-an386 (Cortex-M4F). 보드 펌웨어와 같은 dwt_cyccnt.h 를 쓰고,
 * 같은 측정 방식(트리거 직전 CYCCNT → NVIC_STIR 로 IRQ 31 펜딩 → ISR 첫 명령에서 CYCCNT)으로 1 표본을 잰다.
 * 결과는 세미호스팅으로 출력. 측정 숫자가 아니라 "QEMU 가 사이클을 안 센다" 는 사실 확인이 목적.
 * 빌드/실행: bash build_vm.sh qemu   (board/ 디렉터리, Lima VM 안)
 */
#include <stdint.h>
#include "dwt_cyccnt.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ---------- 세미호스팅 (BM-B08 축약판) ---------- */
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

#define NVIC_ISER0  A10_REG(0xE000E100u)
#define NVIC_STIR   A10_REG(0xE000EF00u)
#define SYST_CSR    A10_REG(0xE000E010u)
#define SYST_RVR    A10_REG(0xE000E014u)
#define SYST_CVR    A10_REG(0xE000E018u)
#define PROBE_IRQ   31u

volatile uint32_t g_t_isr, g_isr_runs;

/* 보드 펌웨어와 같은 모양: naked ISR 의 처음 두 명령이 CYCCNT 를 읽는다 */
__attribute__((naked)) void Irq31_Handler(void)
{
    __asm__ volatile(
        "ldr  r0, =0xE0001004 \n"      /* 명령 1: CYCCNT 주소 (리터럴 풀) */
        "ldr  r0, [r0]        \n"      /* 명령 2: 표본 */
        "ldr  r1, =g_t_isr    \n"
        "str  r0, [r1]        \n"
        "ldr  r1, =g_isr_runs \n"
        "ldr  r2, [r1]        \n"
        "adds r2, r2, #1      \n"
        "str  r2, [r1]        \n"
        "bx   lr              \n"
        ".ltorg               \n");
}

static void Default_Handler(void) { sh_puts("unexpected exception\n"); sh_exit(3); }
void Reset_Handler(void);

__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[16 + 32])(void) = {
    [0] = (void (*)(void))(uintptr_t)_estack, [1] = Reset_Handler,
    [2 ... 15] = Default_Handler, [16 ... 46] = Default_Handler, [16 + PROBE_IRQ] = Irq31_Handler,
};

static void line(const char *k, uint32_t v) { sh_puts(k); sh_hex(v); sh_puts("\n"); }

int main(void)
{
    sh_puts("BM-A10 QEMU CYCCNT probe (mps2-an386, Cortex-M4F)\n");
    line("DEMCR before        = ", DEMCR);
    line("DWT_CTRL before     = ", DWT_CTRL);
    line("DWT_LSR             = ", DWT_LSR);
    int rc = dwt_cyccnt_init();
    line("DEMCR after TRCENA  = ", DEMCR);
    line("DWT_CTRL after ENA  = ", DWT_CTRL);
    line("dwt_cyccnt_init rc  = ", (uint32_t)rc);

    /* 보드와 같은 1 표본 측정 */
    NVIC_ISER0 = 1u << PROBE_IRQ;
    uint32_t t0 = DWT_CYCCNT;
    NVIC_STIR = PROBE_IRQ;
    __asm__ volatile("dsb\n isb" ::: "memory");
    line("isr runs            = ", g_isr_runs);
    line("t0 (before STIR)    = ", t0);
    line("t_isr (ISR 1st ins) = ", g_t_isr);
    line("delta               = ", cyc_delta(t0, g_t_isr));

    /* 같은 루프 동안 SysTick (QEMU 가상 시간 기반) 은 움직이나? */
    SYST_RVR = 0xFFFFFFu; SYST_CVR = 0; SYST_CSR = 5u;    /* CLKSOURCE=core, ENABLE */
    uint32_t s0 = SYST_CVR, c0 = DWT_CYCCNT;
    for (volatile uint32_t i = 0; i < 200000u; ++i) { }
    uint32_t s1 = SYST_CVR, c1 = DWT_CYCCNT;
    line("200k-loop CYCCNT d  = ", c1 - c0);
    line("200k-loop SysTick d = ", (s0 - s1) & 0xFFFFFFu);
    sh_puts(rc == 2 ? "VERDICT: CYCCNT stuck -> QEMU has no cycle counter; latency must be measured on a board\n"
                    : "VERDICT: CYCCNT counts\n");
    return 0;
}

void Reset_Handler(void)
{
    uint32_t *s = _sidata, *d = _sdata;
    while (d < _edata) *d++ = *s++;
    for (d = _sbss; d < _ebss; ) *d++ = 0;
    sh_exit(main());
}
