/* OS-B05 demo_asm.c — 공유 카운터 RMW(fetch_add) vs 단일 writer 슬롯(relaxed load + store) 명령 비교
 * (freestanding: libc 없음. counter++ 자체의 ldr/add/str 은 OS-B04 demo_asm.c)
 * arm64 host : cc -std=c11 -O2 -S demo_asm.c -o -
 * Cortex-M4  : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * Cortex-M0+ : clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_asm.c -o -   (경고 3개: lock-free 아님)
 */
#include <stdatomic.h>

_Atomic unsigned long shared_cnt;      /* 모든 스레드가 같이 증가 (Part A ac_add) */
_Atomic unsigned long slot_cnt;        /* 한 스레드만 쓰는 슬롯 (Part C bump) */

void shared_inc(void) {                /* 원자 RMW: 여러 writer 에도 안전 */
    atomic_fetch_add_explicit(&shared_cnt, 1ul, memory_order_relaxed);
}
void slot_inc(void) {                  /* writer 가 하나뿐일 때만 안전 */
    atomic_store_explicit(&slot_cnt,
        atomic_load_explicit(&slot_cnt, memory_order_relaxed) + 1ul, memory_order_relaxed);
}
