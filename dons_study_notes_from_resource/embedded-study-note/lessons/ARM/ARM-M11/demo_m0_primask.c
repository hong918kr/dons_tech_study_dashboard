/* ENV: qemu */
/* ARM-M11 — Cortex-M0 (ARMv6-M) 에는 LDREX/STREX 가 없다. 대안: PRIMASK 임계 구역.
 *   - C11 atomic_fetch_add 를 gcc 가 `bl __atomic_fetch_add_4` 로 내보내고, arm-none-eabi 의 libgcc 에는
 *     그 함수가 없다 (링크 에러 — run_qemu.sh m0 에서 실측). 그래서 펌웨어가 직접 제공한다: 아래.
 *   - main 은 C11 atomic_fetch_add 로, SysTick ISR 도 같은 카운터를 올린다 → 최종값 정확해야 한다.
 * build/run: run_qemu.sh m0   (qemu-system-arm -M microbit = nRF51, Cortex-M0)
 */
#include <stdatomic.h>
#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))
#define SYST_CSR REG32(0xE000E010u)
#define SYST_RVR REG32(0xE000E014u)
#define SYST_CVR REG32(0xE000E018u)

static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void puts_(const char *s) { semi(0x04, s); }
static void put_u(uint32_t v) {
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    puts_(&b[i]);
}
static void exit_(int ok) { semi(0x18, (const void *)(uintptr_t)(ok ? 0x20026u : 0x20024u)); for (;;) { } }

/* PRIMASK 저장 → cpsid i → ... → PRIMASK 복원 (중첩 안전: 원래 꺼져 있었으면 꺼진 채로 돌아간다) */
static inline uint32_t irq_save(void) {
    uint32_t p;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(p) : : "memory");
    return p;
}
static inline void irq_restore(uint32_t p) { __asm__ volatile("msr primask, %0" : : "r"(p) : "memory"); }

#ifndef NO_OWN_LIBCALL
/* gcc 가 v6-M 에서 부르는 libcall 을 직접 구현 (단일 코어 전제! 다른 코어/DMA 상대로는 원자적이지 않다) */
unsigned int __atomic_fetch_add_4(volatile void *mem, unsigned int val, int model) {
    (void)model;                          /* 단일 코어: 인터럽트 차단이 곧 순서 보장 (컴파일러 배리어는 "memory") */
    volatile uint32_t *p = (volatile uint32_t *)mem;
    uint32_t s = irq_save();
    uint32_t old = *p;
    *p = old + val;
    irq_restore(s);
    return old;
}
#endif

static _Atomic uint32_t g_counter;
static volatile uint32_t g_isr_hits;
void SysTick_Handler(void) { g_isr_hits++; atomic_fetch_add_explicit(&g_counter, 1u, memory_order_relaxed); }
void Default_Handler(void) { puts_("unexpected exception\n"); exit_(0); }

int main(void) {
    const uint32_t n = 100000u;
    uint32_t cpuid = REG32(0xE000ED00u);
    puts_("ARM-M11 Cortex-M0 PRIMASK fallback, CPUID=0x");
    for (int sh = 28; sh >= 0; sh -= 4) { char c[2] = { "0123456789abcdef"[(cpuid >> sh) & 15u], 0 }; puts_(c); }
    puts_("\n");
    SYST_RVR = 400u; SYST_CVR = 0u; SYST_CSR = 7u;
    for (uint32_t i = 0; i < n; i++) atomic_fetch_add_explicit(&g_counter, 1u, memory_order_relaxed);
    SYST_CSR = 0u;
    uint32_t c = atomic_load_explicit(&g_counter, memory_order_relaxed), e = n + g_isr_hits;
    puts_("counter="); put_u(c); puts_(" expected="); put_u(e); puts_(" isr_hits="); put_u(g_isr_hits);
    puts_(c == e ? "  PASS\n" : "  FAIL\n");
    exit_(c == e);
    return 0;
}

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0u;
    (void)main();
    exit_(0);
}
#define D ((uintptr_t)Default_Handler)
__attribute__((section(".vectors"), used))
const uintptr_t g_vectors[16 + 32] = {
    (uintptr_t)&_estack, (uintptr_t)Reset_Handler, D, D, 0, 0, 0, 0, 0, 0, 0, D, 0, 0, D,
    (uintptr_t)SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};
