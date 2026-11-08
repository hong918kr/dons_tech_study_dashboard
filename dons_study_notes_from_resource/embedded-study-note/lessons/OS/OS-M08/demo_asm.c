/* OS-M08 demo_asm.c — 이 레슨 패턴들이 실제 어떤 명령이 되나 (freestanding: 표준 헤더 3개만)
 *   cc -std=c11 -O2 -S demo_asm.c -o -                                                  (arm64, Apple M6)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -   (Cortex-M4)
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint32_t seq, a, b, c; } payload_t;
typedef struct { _Alignas(64) payload_t pl; _Alignas(64) atomic_uint flag; } slot_t;

void mp_post(slot_t *s, const payload_t *p, uint32_t v) {          /* Part A 게시 */
    s->pl = *p;
    atomic_store_explicit(&s->flag, v, memory_order_release);
}
void mp_post_relaxed(slot_t *s, const payload_t *p, uint32_t v) {  /* 버그 버전 */
    s->pl = *p;
    atomic_store_explicit(&s->flag, v, memory_order_relaxed);
}
bool mp_take(slot_t *s, uint32_t v, payload_t *out) {             /* Part A 받기 */
    if (atomic_load_explicit(&s->flag, memory_order_acquire) != v) return false;
    *out = s->pl;
    return true;
}
typedef struct { uint32_t magic; uint32_t tab[64]; } table_t;
table_t *lazy_peek(_Atomic(table_t *) *pp) {                       /* Part B 빠른 경로 */
    return atomic_load_explicit(pp, memory_order_acquire);
}
bool drv_claim(atomic_uint *state) {                               /* Part C: UNINIT(0) → BUSY(1) */
    unsigned s = 0;
    return atomic_compare_exchange_strong_explicit(state, &s, 1u, memory_order_acquire, memory_order_acquire);
}
