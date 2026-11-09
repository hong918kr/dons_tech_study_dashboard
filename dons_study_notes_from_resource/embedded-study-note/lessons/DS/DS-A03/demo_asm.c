/* DS-A03 demo_asm.c — Vyukov enqueue 와 weak/strong CAS 가 어떤 명령이 되나 (freestanding: libc 없음)
 *
 *   cc -std=c11 -O2 -S demo_asm.c -o -                                     # arm64 기본 (apple-m1, LSE)
 *   cc -std=c11 -O2 -S -mcpu=apple-m1+nolse demo_asm.c -o -                # LSE 끔 → LDXR/STXR
 *   clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 *   clang -std=c11 --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cas_weak_relaxed(atomic_size_t *p, size_t *expected, size_t desired) {
    return atomic_compare_exchange_weak_explicit(p, expected, desired, memory_order_relaxed, memory_order_relaxed);
}
bool cas_strong_relaxed(atomic_size_t *p, size_t *expected, size_t desired) {
    return atomic_compare_exchange_strong_explicit(p, expected, desired, memory_order_relaxed, memory_order_relaxed);
}

typedef struct { atomic_size_t seq; uint32_t data; } cell_t;
typedef struct { cell_t *buf; size_t mask; atomic_size_t enq_pos; } q_t;

bool vq_try_enqueue(q_t *q, uint32_t v) {
    size_t pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        size_t seq = atomic_load_explicit(&c->seq, memory_order_acquire);
        intptr_t dif = (intptr_t)(seq - pos);
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&q->enq_pos, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (dif < 0) {
            return false;
        } else {
            pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
        }
    }
    c->data = v;
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);
    return true;
}
