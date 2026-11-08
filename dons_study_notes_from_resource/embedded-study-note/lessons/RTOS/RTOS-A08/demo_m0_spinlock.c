/* RTOS-A08 demo: 스핀락이 Cortex-M 에서 무엇으로 컴파일되나 (asm 만 본다, 링크하지 않는다)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4      -mthumb -O2 -S demo_m0_spinlock.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus  -mthumb -O2 -S demo_m0_spinlock.c -o -
 *
 * ① C11 atomic 스핀락 (LDREX/STREX 가 있어야 lock-free)
 * ② RP2040 SIO 하드웨어 스핀락: 레지스터를 "읽으면" 잡기 시도 (0 이 아니면 성공), "쓰면" 해제.
 *    주소는 RP2040 데이터시트의 SIO_BASE 0xd0000000 + SPINLOCK0 오프셋 0x100 (데이터시트 기준).
 * ③ FreeRTOS SMP 식 임계 구역: 자기 코어 인터럽트 끄기 → 스핀락
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* ① */
atomic_flag g_flag = ATOMIC_FLAG_INIT;
void c11_lock(void)   { while (atomic_flag_test_and_set_explicit(&g_flag, memory_order_acquire)) { } }
void c11_unlock(void) { atomic_flag_clear_explicit(&g_flag, memory_order_release); }

/* ② */
#define SIO_SPINLOCK(n) (*(volatile uint32_t *)(0xd0000100u + 4u * (n)))
static inline void dmb(void) { __asm__ volatile("dmb" ::: "memory"); }
void sio_lock(void)   { while (SIO_SPINLOCK(31) == 0u) { } dmb(); }   /* 읽기 = 잡기 시도 */
void sio_unlock(void) { dmb(); SIO_SPINLOCK(31) = 1u; }              /* 쓰기(아무 값) = 해제 */

/* ③ */
static inline uint32_t irq_save(void) {
    uint32_t pm;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
    return pm;
}
static inline void irq_restore(uint32_t pm) { __asm__ volatile("msr primask, %0" :: "r"(pm) : "memory"); }
volatile uint32_t g_shared;
void smp_crit_inc(void) {
    uint32_t pm = irq_save();      /* 1) 이 코어의 ISR 차단 */
    sio_lock();                    /* 2) 다른 코어 차단 */
    g_shared = g_shared + 1u;
    sio_unlock();                  /* 3) 락 먼저 풀고 */
    irq_restore(pm);               /* 4) 인터럽트 복원 */
}
