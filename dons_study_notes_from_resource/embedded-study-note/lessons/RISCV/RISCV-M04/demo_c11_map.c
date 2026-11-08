/* ENV: qemu */
/* RISCV-M04 demo_c11_map.c — C11 atomics 가 RISC-V 명령으로 어떻게 내려가나 (freestanding, libc 없음)
 * Lima VM elinux (clang 21):
 *   clang --target=riscv32 -march=rv32ia -O2 -S demo_c11_map.c -o - | grep -v '^\s*\.'   # A 있음
 *   clang --target=riscv32 -march=rv32i  -O2 -S demo_c11_map.c -o - | grep -v '^\s*\.'   # A 없음 → libcall
 */
#include <stdatomic.h>
#include <stdint.h>

atomic_uint g;

uint32_t fadd_relaxed(void) { return atomic_fetch_add_explicit(&g, 1u, memory_order_relaxed); }
uint32_t fadd_acq_rel(void) { return atomic_fetch_add_explicit(&g, 1u, memory_order_acq_rel); }
uint32_t fadd_seq_cst(void) { return atomic_fetch_add(&g, 1u); }
uint32_t xchg_acquire(uint32_t v) { return atomic_exchange_explicit(&g, v, memory_order_acquire); }
uint32_t load_acquire(void) { return atomic_load_explicit(&g, memory_order_acquire); }
uint32_t load_seq_cst(void) { return atomic_load(&g); }
void store_release(uint32_t v) { atomic_store_explicit(&g, v, memory_order_release); }
void store_seq_cst(uint32_t v) { atomic_store(&g, v); }
_Bool cas_seq_cst(uint32_t *exp, uint32_t des) { return atomic_compare_exchange_strong(&g, exp, des); }
uint32_t fmax_relaxed(uint32_t v) {            /* fetch_max 는 C11 에 없음 → CAS 루프 */
    uint32_t cur = atomic_load_explicit(&g, memory_order_relaxed);
    while (cur < v && !atomic_compare_exchange_weak_explicit(&g, &cur, v, memory_order_relaxed, memory_order_relaxed)) { }
    return cur;
}
