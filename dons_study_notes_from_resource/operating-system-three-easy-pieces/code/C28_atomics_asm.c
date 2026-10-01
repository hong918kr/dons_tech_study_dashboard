// C28_atomics_asm.c — C11 원자 연산이 Apple Silicon(arm64)에서 어떤 명령으로 바뀌는지 본다.
//   cc -O2 -S code/C28_atomics_asm.c -o -                    (기본: M1/M2 = ARMv8.5, LSE 원자 명령)
//   cc -O2 -S -mcpu=apple-a7 code/C28_atomics_asm.c -o -     (LSE 없는 ARMv8.0 → LL/SC 루프)
#include <stdatomic.h>

// test-and-set (atomic exchange)
int tas(atomic_int *p) {
    return atomic_exchange_explicit(p, 1, memory_order_acquire);
}

// compare-and-swap: *p == expected 이면 new 를 쓰고 1, 아니면 0
int cas(atomic_int *p, int expected, int new_value) {
    return atomic_compare_exchange_strong_explicit(p, &expected, new_value,
                                                   memory_order_acquire, memory_order_relaxed);
}

// fetch-and-add (ticket lock 의 핵심)
int faa(atomic_int *p) {
    return atomic_fetch_add_explicit(p, 1, memory_order_relaxed);
}

// unlock = release store
void release(atomic_int *p) {
    atomic_store_explicit(p, 0, memory_order_release);
}
