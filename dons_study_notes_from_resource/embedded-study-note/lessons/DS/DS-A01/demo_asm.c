/* DS-A01 demo_asm.c — SPSC push/pop 이 어떤 명령으로 내려가나 (freestanding: libc 없음, -S 로만 본다)
 *
 *   push_acqrel / pop_acqrel     : 정답 순서 (relaxed 자기 인덱스, acquire 상대 인덱스, release 내 인덱스)
 *   push_relaxed / pop_relaxed   : 전부 relaxed (깨진 버전)
 *   push_sigfence / pop_sigfence : 단일 코어 ISR↔main 용 — relaxed + atomic_signal_fence (컴파일러 장벽만)
 *
 * arm64 (이 맥):  cc -std=c11 -O2 -S demo_asm.c -o - | grep -E 'ld|st|dmb|^_'
 *                 cc -std=c11 -O2 -S -march=armv8-a demo_asm.c -o -       (RCpc 없는 기본 v8.0 → ldar)
 * Cortex-M4:      clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 * Cortex-M0+:     clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define CAP 64u
typedef struct {
    _Alignas(32) atomic_uint tail;
    _Alignas(32) atomic_uint head;
    _Alignas(32) uint32_t buf[CAP];
} ring_t;

bool push_acqrel(ring_t *q, uint32_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (t - h == CAP) return false;
    q->buf[t & (CAP - 1u)] = v;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return true;
}
bool pop_acqrel(ring_t *q, uint32_t *out) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (t == h) return false;
    *out = q->buf[h & (CAP - 1u)];
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
    return true;
}

bool push_relaxed(ring_t *q, uint32_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    if (t - h == CAP) return false;
    q->buf[t & (CAP - 1u)] = v;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_relaxed);
    return true;
}
bool pop_relaxed(ring_t *q, uint32_t *out) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t == h) return false;
    *out = q->buf[h & (CAP - 1u)];
    atomic_store_explicit(&q->head, h + 1u, memory_order_relaxed);
    return true;
}

/* 단일 코어 전용: ISR 과 main 은 같은 코어 → CPU 는 자기 프로그램 순서를 지킨다. 막아야 할 것은 컴파일러뿐.
 * atomic_signal_fence 는 "같은 스레드의 시그널 핸들러" 와의 순서만 보장(C11 7.17.4.2) = ISR 모델. */
bool push_sigfence(ring_t *q, uint32_t v) {          /* ISR 에서 호출 */
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);
    if (t - h == CAP) return false;
    q->buf[t & (CAP - 1u)] = v;
    atomic_signal_fence(memory_order_release);
    atomic_store_explicit(&q->tail, t + 1u, memory_order_relaxed);
    return true;
}
bool pop_sigfence(ring_t *q, uint32_t *out) {        /* main 에서 호출 */
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);
    if (t == h) return false;
    *out = q->buf[h & (CAP - 1u)];
    atomic_signal_fence(memory_order_release);
    atomic_store_explicit(&q->head, h + 1u, memory_order_relaxed);
    return true;
}
