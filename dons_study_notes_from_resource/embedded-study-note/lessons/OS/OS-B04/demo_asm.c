/* OS-B04 demo_asm.c — counter++ 가 실제로 어떤 명령이 되나 (freestanding: libc 없음)
 * arm64 host : cc -std=c11 -O0 -S demo_asm.c -o -      /  cc -std=c11 -O2 -S demo_asm.c -o -
 * Cortex-M4  : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * Cortex-M0+ : clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdatomic.h>

unsigned counter;                 /* 공유 카운터 (일반 변수) */
volatile unsigned vcounter;       /* volatile: 매번 메모리 접근, 그래도 원자적이지 않다 */
_Atomic unsigned acounter;        /* 비교용: 원자 RMW (고치는 법은 OS-B05) */

void inc(void)                { counter++; }
void inc_n(unsigned n)        { for (unsigned i = 0; i < n; i++) counter++; }
void inc_n_volatile(unsigned n) { for (unsigned i = 0; i < n; i++) vcounter++; }
void inc_atomic(void)         { atomic_fetch_add_explicit(&acounter, 1u, memory_order_relaxed); }
