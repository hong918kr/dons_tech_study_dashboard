/* CPP-A04 demo_libatomic_m0.c — Cortex-M0/M0+ (ARMv6-M, 단일 코어)용 __atomic_* 라이브러리 함수 직접 구현
 *
 * 왜 필요한가: ARMv6-M 에는 LDREX/STREX 가 없어서 clang 은 std::atomic<uint32_t> 의 모든 연산을,
 * GCC 는 RMW(fetch_add/exchange/CAS)를 __atomic_*_4 호출로 내보낸다 (64비트는 M3/M4 도 __atomic_*_8).
 * arm-none-eabi 툴체인(libgcc/newlib)에는 이 함수들이 없다 → 링크 에러. 그래서 "보통 당신이" 쓴다.
 * 구현 = PRIMASK 저장 → cpsid i → 일반 접근 → PRIMASK 복원.  **단일 코어 전용** (RP2040 같은 듀얼 M0+ 는
 * 다른 코어를 못 막는다 → 하드웨어 스핀락이 필요).
 *
 *   clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -c demo_libatomic_m0.c
 *   arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -std=c11 -Wall -Wextra -Werror -O2 -c demo_libatomic_m0.c   (Lima VM)
 * 시그니처는 GCC libatomic ABI 의 크기별(sized) 함수와 같다: memorder 인자는 받지만 무시한다 —
 * 인터럽트를 끈 구간 + "memory" clobber 가 단일 코어에서 seq_cst 보다 강한 순서를 준다.
 */
#include <stdbool.h>
#include <stdint.h>

static inline unsigned int irq_save(void) {
    unsigned int pm;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) : : "memory");
    return pm;
}
static inline void irq_restore(unsigned int pm) {
    __asm volatile("msr primask, %0" : : "r"(pm) : "memory");
}

unsigned int __atomic_load_4(const volatile void *p, int mo) {
    (void)mo;
    return *(const volatile unsigned int *)p;      /* 정렬된 32비트 LDR 은 그 자체로 single-copy atomic */
}
void __atomic_store_4(volatile void *p, unsigned int v, int mo) {
    (void)mo;
    *(volatile unsigned int *)p = v;
}
unsigned int __atomic_exchange_4(volatile void *p, unsigned int v, int mo) {
    (void)mo;
    unsigned int pm = irq_save();
    unsigned int old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = v;
    irq_restore(pm);
    return old;
}
unsigned int __atomic_fetch_add_4(volatile void *p, unsigned int v, int mo) {
    (void)mo;
    unsigned int pm = irq_save();
    unsigned int old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = old + v;
    irq_restore(pm);
    return old;
}
/* CAS: 라이브러리 ABI 는 (ptr, expected*, desired, success, failure) — weak 인자가 없다 (호출 쪽 asm 실측).
 * 그런데 GCC 는 같은 이름의 *내장 함수* 를 weak 포함 6인자로 알고 있어서 이 이름으로 정의하면
 * -Werror=builtin-declaration-mismatch. → 다른 C 이름으로 정의하고 asm 라벨로 심볼 이름만 맞춘다. */
bool m0_cas_4(volatile void *p, void *expected, unsigned int desired, int smo, int fmo)
    __asm__("__atomic_compare_exchange_4");
bool m0_cas_4(volatile void *p, void *expected, unsigned int desired, int smo, int fmo) {
    (void)smo; (void)fmo;
    unsigned int pm = irq_save();
    unsigned int cur = *(volatile unsigned int *)p;
    bool ok = (cur == *(unsigned int *)expected);
    if (ok) *(volatile unsigned int *)p = desired;
    else    *(unsigned int *)expected = cur;
    irq_restore(pm);
    return ok;
}
/* 64비트: M0/M0+ 뿐 아니라 M3/M4/M7 도 이 함수를 부른다 (LDREXD 없음) */
unsigned long long __atomic_load_8(const volatile void *p, int mo) {
    (void)mo;
    unsigned int pm = irq_save();
    unsigned long long v = *(const volatile unsigned long long *)p;   /* LDR 2개 — 그래서 IRQ 차단 */
    irq_restore(pm);
    return v;
}
void __atomic_store_8(volatile void *p, unsigned long long v, int mo) {
    (void)mo;
    unsigned int pm = irq_save();
    *(volatile unsigned long long *)p = v;
    irq_restore(pm);
}
unsigned long long __atomic_fetch_add_8(volatile void *p, unsigned long long v, int mo) {
    (void)mo;
    unsigned int pm = irq_save();
    unsigned long long old = *(volatile unsigned long long *)p;
    *(volatile unsigned long long *)p = old + v;
    irq_restore(pm);
    return old;
}
