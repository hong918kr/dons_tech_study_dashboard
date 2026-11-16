/* ENV: qemu */
/* ARM-A01 공용 스타트업 — starter / solution / practice 모두에 링크된다 (TODO 없음).
 * 벡터 테이블(C 배열, 16 + IRQ 32) + Reset_Handler(.data 복사, .bss 0, M4F 면 CPACR 로 FPU 켜기)
 * + 세미호스팅 + 테스트 카운터 + 기본/HardFault 핸들러.  (ARM-M09 shared_startup.c 축약판)
 * SVC_Handler / PendSV_Handler 는 각 변형의 .S, SysTick_Handler 는 shared_os.c, IRQ6 는 shared_tasks.S.
 */
#include "a01.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

/* ---------------- 세미호스팅 (BKPT 0xAB → QEMU 가 호스트에서 처리) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
void sh_puts(const char *s) { semi(0x04, s); }                       /* SYS_WRITE0 */
void sh_putc(char c) { semi(0x03, &c); }                             /* SYS_WRITEC */
void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit */
    semi(0x20, blk);                                                 /* SYS_EXIT_EXTENDED → qemu exit = code */
    for (;;) { }
}

/* ---------------- 테스트 카운터 ---------------- */
unsigned g_pass, g_total;
void check(int ok, const char *expr, int line) {
    g_total++;
    if (ok) g_pass++;
    sh_puts(ok ? "PASS " : "FAIL ");
    sh_puts("line "); sh_dec((uint32_t)line); sh_puts(": "); sh_puts(expr); sh_puts("\n");
}

/* ---------------- 기본 핸들러: 예상 못 한 예외는 정보를 찍고 exit 3 ---------------- */
void Default_Handler(void) {
    uint32_t ipsr, lr, psp, msp;
    __asm__ volatile("mrs %0, ipsr\n mov %1, lr\n mrs %2, psp\n mrs %3, msp" : "=r"(ipsr), "=r"(lr), "=r"(psp), "=r"(msp));
    sh_puts("\nunexpected exception IPSR="); sh_dec(ipsr);
    sh_puts(" lr="); sh_hex(lr); sh_puts(" CFSR="); sh_hex(SCB_CFSR); sh_puts(" HFSR="); sh_hex(SCB_HFSR);
    sh_puts(" msp="); sh_hex(msp); sh_puts(" psp="); sh_hex(psp);
    const uint32_t *f = (const uint32_t *)(uintptr_t)((lr & 4u) ? psp : msp);
    sh_puts(" stacked pc="); sh_hex(f[6]); sh_puts("\n");
    sh_exit(3);
}
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void);                      /* starter.S / solution.S / practice.S (TODO) */
void PendSV_Handler(void);                   /* starter.S / solution.S / practice.S (TODO) */
void SysTick_Handler(void);                  /* shared_os.c (워치독 + os_tick) */
void IRQ6_Handler(void);                     /* shared_tasks.S (바쁜 IRQ, Part B) */
__attribute__((noreturn)) void Reset_Handler(void);

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used, aligned(256)))
void (*const g_vectors[48])(void) = {
    (vector_t)_estack,      /* [0]  초기 MSP */
    Reset_Handler,          /* [1] */
    NMI_Handler,            /* [2] */
    HardFault_Handler,      /* [3] */
    MemManage_Handler,      /* [4] */
    BusFault_Handler,       /* [5] */
    UsageFault_Handler,     /* [6] */
    0, 0, 0, 0,             /* [7..10] */
    SVC_Handler,            /* [11] SVCall */
    DebugMon_Handler,       /* [12] */
    0,                      /* [13] */
    PendSV_Handler,         /* [14] PendSV */
    SysTick_Handler,        /* [15] SysTick */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,     /* IRQ0..3 */
    Default_Handler, Default_Handler, IRQ6_Handler, Default_Handler,        /* IRQ4..7 (IRQ6 = 바쁜 IRQ) */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};

__attribute__((noreturn)) void Reset_Handler(void) {
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata; ) *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss; ) *d++ = 0u;
#ifdef HAS_FPU
    REG32(0xE000ED88u) |= 0xFu << 20;              /* CPACR: CP10/CP11 full access (안 켜면 FP 명령 → NOCP) */
    __asm__ volatile("dsb\n isb" ::: "memory");
#endif
    sh_exit(main());        /* main 의 반환값 = QEMU 종료 코드 */
}
