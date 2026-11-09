/* OS-A05 demo_asm — Chase–Lev 세 연산이 어떤 명령이 되나 (libc 없음, 양쪽 타깃에서 컴파일)
 *
 * arm64 (이 맥):
 *   cc -std=c11 -O2 -S demo_asm.c -o - | grep -v '^\s*[;.]'
 * Cortex-M4 (인덱스 64비트 = 기본, 32비트 = -DIDX32):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S -DIDX32 demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef IDX32
typedef int32_t idx_t;          /* MCU: 32비트. 부호 있는 오버플로를 피하려면 실제로는 uint32_t + (int32_t)(b - t) 비교 */
#else
typedef int64_t idx_t;
#endif
#define CAP 64
typedef struct {
    _Alignas(64) _Atomic(idx_t) top;
    _Alignas(64) _Atomic(idx_t) bottom;
    _Atomic(void *) buf[CAP];
} dq_t;

bool dq_push(dq_t *q, void *x) {
    idx_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    idx_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    if (b - t >= CAP) return false;
    atomic_store_explicit(&q->buf[b & (CAP - 1)], x, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_release);
    return true;
}
void *dq_take(dq_t *q) {
    idx_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    idx_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    void *x = 0;
    if (t <= b) {
        x = atomic_load_explicit(&q->buf[b & (CAP - 1)], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                x = 0;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else {
        atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    }
    return x;
}
void *dq_steal(dq_t *q) {
    idx_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    idx_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);
    if (t >= b) return 0;
    void *x = atomic_load_explicit(&q->buf[t & (CAP - 1)], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        return (void *)1;
    return x;
}
bool idx_lock_free(dq_t *q) { return atomic_is_lock_free(&q->top); }
