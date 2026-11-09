/* SOCX-B03 demo — 코어 쪽: 어떤 명령이 버스의 "배타 읽기/쓰기"(AxLOCK=1)가 될 수 있나
 * freestanding (stdatomic/stdint/stdbool 만). 실행하지 않고 asm 만 본다:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_core_asm.c -o -
 *   cc -O2 -S demo_core_asm.c -o -                                   (arm64 macOS 기본 = LSE)
 *   cc -O2 -mcpu=apple-m1+nolse -S demo_core_asm.c -o -              (LL/SC 루프)
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* 스핀락 한 번 시도: 0 이면 1 로 (Part B 의 RX → WX 와 같은 모양) */
bool lock_try(_Atomic uint32_t *lock) {
    uint32_t expected = 0;
    return atomic_compare_exchange_strong_explicit(lock, &expected, 1u,
                                                   memory_order_acquire, memory_order_relaxed);
}
/* 해제: 일반 store (release) */
void lock_release(_Atomic uint32_t *lock) {
    atomic_store_explicit(lock, 0u, memory_order_release);
}
/* 카운터 증가 (Part B S2) */
uint32_t counter_inc(_Atomic uint32_t *c) {
    return atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}
