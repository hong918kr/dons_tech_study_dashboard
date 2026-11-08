/* LLAT-M02 demo_asm_m4.c — 같은 seqlock 을 Cortex-M4 (Thumb-2) 로: 무엇이 바뀌나
 * freestanding: <stdint.h>/<stdbool.h>/<stdatomic.h> 만. 32비트 seq + 32비트 데이터 워드 4개.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm_m4.c -o -
 *        cc -std=c11 -O2 -S demo_asm_m4.c -o -          (호스트 arm64 와 비교)
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { _Atomic uint32_t seq; _Atomic uint32_t w[4]; } sl32_t;

/* ISR (writer) — 단일 코어라면 main 이 ISR 도중에 끼어들 수 없다 */
void isr_write(sl32_t *s, const uint32_t *in) {
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    for (int k = 0; k < 4; k++) atomic_store_explicit(&s->w[k], in[k], memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 2, memory_order_release);
}
/* main (reader) — 복사 도중 ISR 이 오면 seq 가 바뀌어 재시도 */
bool main_read(sl32_t *s, uint32_t *out) {
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s1 & 1u) return false;
    uint32_t t[4];
    for (int k = 0; k < 4; k++) t[k] = atomic_load_explicit(&s->w[k], memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&s->seq, memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
/* 단일 코어 전용 판: CPU 재배치는 같은 코어의 ISR 에 보이지 않으므로 컴파일러 배리어(signal fence)만 */
bool main_read_1core(sl32_t *s, uint32_t *out) {
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);
    if (s1 & 1u) return false;
    uint32_t t[4];
    for (int k = 0; k < 4; k++) t[k] = atomic_load_explicit(&s->w[k], memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);
    if (atomic_load_explicit(&s->seq, memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
