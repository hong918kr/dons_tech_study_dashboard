/* C-A03 demo_asm.c — memory_order 가 실제 명령어로 무엇이 되나 (freestanding: libc 없음)
 * host arm64 : cc -std=c11 -O2 -S demo_asm.c -o -
 * Cortex-M4  : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

atomic_flag lk = ATOMIC_FLAG_INIT;
atomic_uint cnt, ready, mx, mid;
uint32_t payload;

void lock(void)       { while (atomic_flag_test_and_set_explicit(&lk, memory_order_acquire)) { } }
void unlock(void)     { atomic_flag_clear_explicit(&lk, memory_order_release); }
unsigned inc_relaxed(void) { return atomic_fetch_add_explicit(&cnt, 1u, memory_order_relaxed); }
unsigned inc_seqcst(void)  { return atomic_fetch_add(&cnt, 1u); }
void publish(uint32_t v)   { payload = v; atomic_store_explicit(&ready, 1u, memory_order_release); }
uint32_t consume(void) {
    while (!atomic_load_explicit(&ready, memory_order_acquire)) { }
    return payload;
}
void store_seqcst(void)    { atomic_store(&ready, 1u); }
unsigned load_seqcst(void) { return atomic_load(&ready); }
unsigned swap_acqrel(unsigned v) { return atomic_exchange_explicit(&mid, v, memory_order_acq_rel); }
void max_cas(unsigned v) {
    unsigned cur = atomic_load_explicit(&mx, memory_order_relaxed);
    while (cur < v && !atomic_compare_exchange_weak_explicit(&mx, &cur, v,
                          memory_order_relaxed, memory_order_relaxed)) { }
}
