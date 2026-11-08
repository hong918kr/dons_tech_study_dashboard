/* C-M14 demo_m4.c — 32비트 Cortex-M 에서 64비트 공유 변수와 임계 구역이 어떤 명령어가 되나 (asm 관찰용, main 없음)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -   # Cortex-M4
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_m4.c -o - # Cortex-M0+
 *   clang -std=c11 -O2 -S demo_m4.c -o -                                               # 호스트 arm64 (inline asm 부분 제외)
 * 타깃에 <stdio.h> 가 없으므로 <stdint.h> 만 쓴다. _Atomic 은 키워드라 헤더가 필요 없다.
 */
#include <stdint.h>

volatile uint64_t v_ts;                 /* ISR 이 갱신하는 64비트 타임스탬프 */
_Atomic uint64_t a_ts;                  /* 같은 것을 C11 원자 타입으로 */
_Atomic uint32_t a_cnt;

uint64_t read_v64(void)  { return v_ts; }           /* ① volatile 64비트 읽기 */
void     isr_v64(void)   { v_ts = v_ts + 1000u; }   /* ② ISR 쪽 갱신 */
uint64_t read_a64(void)  { return a_ts; }           /* ③ _Atomic 64비트 읽기 */
uint32_t take_a32(void)  { return __c11_atomic_exchange(&a_cnt, 0u, __ATOMIC_RELAXED); } /* ④ 32비트 exchange */

#if defined(__ARM_ARCH) && !defined(__aarch64__)
/* ⑤ PRIMASK 저장/복원 임계 구역 (CMSIS: __get_PRIMASK / __disable_irq / __set_PRIMASK) */
uint64_t read_v64_crit(void) {
    uint32_t pm;
    __asm volatile ("mrs %0, primask" : "=r"(pm));
    __asm volatile ("cpsid i" ::: "memory");
    uint64_t v = v_ts;
    __asm volatile ("msr primask, %0" :: "r"(pm) : "memory");
    return v;
}
#if __ARM_ARCH_7EM__ || __ARM_ARCH_7M__
/* ⑥ BASEPRI: 우선순위 값 >= 0x40 인 인터럽트만 막는다 (ARMv7-M 이상, M0 에는 없음) */
uint64_t read_v64_basepri(void) {
    uint32_t old;
    __asm volatile ("mrs %0, basepri" : "=r"(old));
    __asm volatile ("msr basepri_max, %0" :: "r"(0x40u) : "memory");
    uint64_t v = v_ts;
    __asm volatile ("msr basepri, %0" :: "r"(old) : "memory");
    return v;
}
#endif
#endif
