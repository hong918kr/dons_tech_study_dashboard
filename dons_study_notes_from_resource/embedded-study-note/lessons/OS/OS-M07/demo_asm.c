/* OS-M07 demo_asm.c — 세 락의 몸통만 모은 파일. libc 없음 (thumbv7em 에서도 -S 가능).
 *
 * arm64 (이 맥):
 *   cc -std=c11 -O2 -S demo_asm.c -o -                              # Apple 기본 (apple-m1 계열 CPU)
 *   cc -std=c11 -O2 -mno-outline-atomics -S demo_asm.c -o -
 *   cc -std=c11 -O2 -march=armv8-a -S demo_asm.c -o -
 *   cc -std=c11 -O2 -mcpu=generic -S demo_asm.c -o -                 # LSE 없는 CPU 로 가정
 *   cc -std=c11 -O2 -mcpu=generic -march=armv8-a+lse -S demo_asm.c -o -
 * Cortex-M4:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

static inline void cpu_relax(void) {
#if defined(__aarch64__)
    __builtin_arm_yield();          /* -> yield */
#elif defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#endif
}
static inline void cpu_relax_isb(void) {
#if defined(__aarch64__)
    __builtin_arm_isb(15);          /* -> isb (SY) */
#endif
}

typedef struct { atomic_uint v; } spin_t;
typedef struct { atomic_uint next, serving; } ticket_t;

void tas_lock(spin_t *l) {
    while (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) { }
}
void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }

atomic_flag g_flag = ATOMIC_FLAG_INIT;
void flag_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_flag, memory_order_acquire)) { }
}

void ttas_lock(spin_t *l) {
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed)) cpu_relax();
        if (!atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) return;
    }
}

void ttas_lock_isb(spin_t *l) {
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed)) cpu_relax_isb();
        if (!atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) return;
    }
}

void ticket_lock(ticket_t *t) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) cpu_relax();
}
void ticket_unlock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);
    atomic_store_explicit(&t->serving, s + 1u, memory_order_release);
}
bool ticket_trylock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);
    unsigned n = s;
    return atomic_compare_exchange_strong_explicit(&t->next, &n, s + 1u,
                                                   memory_order_acquire, memory_order_relaxed);
}
