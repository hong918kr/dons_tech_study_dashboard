/* DS-A02 demo_asm.c — 단일 코어 ISR 링의 "소비자(태스크) 쪽" 이 타깃·배리어별로 무엇이 되나 (freestanding, 링크 안 함)
 *
 *   Cortex-M4:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *   Cortex-M7:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *   Cortex-M0+: clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *
 * 함수는 모두 같은 일을 한다: head != tail 이 될 때까지 돌고, buf[tail] 을 읽고, tail 을 +1 한다.
 * 차이는 "무엇으로 컴파일러(와 CPU)를 묶었나" 뿐이다.
 */
#include <stdatomic.h>
#include <stdint.h>

#define N 256u
#define M (N - 1u)

/* (1) 아무것도 안 함 — 스핀이 지워지고, Cortex-M7 스케줄링에서는 tail 저장이 칸 읽기보다 앞선다 */
uint32_t p_head, p_tail;
uint8_t  p_buf[N];
uint8_t get_plain(void) {
    while (p_head == p_tail) { }
    uint8_t v = p_buf[p_tail & M];
    p_tail = p_tail + 1u;
    return v;
}

/* (2) 인덱스만 volatile — 스핀은 살아나지만, 칸(일반 변수)은 volatile 과 순서가 묶이지 않는다 */
volatile uint32_t v_head, v_tail;
uint8_t v_buf[N];
uint8_t get_volatile(void) {
    while (v_head == v_tail) { }
    uint8_t v = v_buf[v_tail & M];
    v_tail = v_tail + 1u;
    return v;
}

/* (3) volatile + 컴파일러 배리어 — M0+ 처럼 _Atomic 이 라이브러리 호출이 되는 타깃에서도 쓰는 형태 */
volatile uint32_t f_head, f_tail;
uint8_t f_buf[N];
uint8_t get_volatile_fence(void) {
    while (f_head == f_tail) { }
    uint32_t t = f_tail;
    atomic_signal_fence(memory_order_acquire);
    uint8_t v = f_buf[t & M];
    atomic_signal_fence(memory_order_release);
    f_tail = t + 1u;
    return v;
}

/* (4) relaxed atomic + signal fence — 이 레슨 solution.c 의 형태. 단일 코어 ISR 전용 */
_Atomic uint32_t s_head, s_tail;
uint8_t s_buf[N];
uint8_t get_relaxed_fence(void) {
    uint32_t t = atomic_load_explicit(&s_tail, memory_order_relaxed);
    while (atomic_load_explicit(&s_head, memory_order_relaxed) == t) { }
    atomic_signal_fence(memory_order_acquire);
    uint8_t v = s_buf[t & M];
    atomic_signal_fence(memory_order_release);
    atomic_store_explicit(&s_tail, t + 1u, memory_order_relaxed);
    return v;
}

/* (5) acquire/release — 코어가 둘 이상이어도 맞는 형태 (DS-A01) */
_Atomic uint32_t a_head, a_tail;
uint8_t a_buf[N];
uint8_t get_acq_rel(void) {
    uint32_t t = atomic_load_explicit(&a_tail, memory_order_relaxed);
    while (atomic_load_explicit(&a_head, memory_order_acquire) == t) { }
    uint8_t v = a_buf[t & M];
    atomic_store_explicit(&a_tail, t + 1u, memory_order_release);
    return v;
}

/* (6) ISR 쪽 생산자: 일반 변수 2번 게시 → 첫 게시가 사라지는가 */
void put2_plain(uint8_t a, uint8_t b) {
    uint32_t h = p_head;
    p_buf[h & M] = a;
    p_head = h + 1u;
    p_buf[(h + 1u) & M] = b;
    p_head = h + 2u;
}

/* (7) 표준 relaxed 생산자 (ISR 쪽) */
void isr_put_relaxed(uint8_t b) {
    uint32_t h = atomic_load_explicit(&s_head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&s_tail, memory_order_relaxed);
    if (h - t == N) return;
    s_buf[h & M] = b;
    atomic_signal_fence(memory_order_release);
    atomic_store_explicit(&s_head, h + 1u, memory_order_relaxed);
}

/* (8) bip buffer commit: wm 을 w 보다 먼저 게시 */
_Atomic uint32_t b_w, b_wm;
void bip_commit_wrap_relaxed(uint32_t old_w, uint32_t k) {
    atomic_store_explicit(&b_wm, old_w, memory_order_relaxed);
    atomic_signal_fence(memory_order_release);
    atomic_store_explicit(&b_w, k, memory_order_relaxed);
}
void bip_commit_wrap_release(uint32_t old_w, uint32_t k) {
    atomic_store_explicit(&b_wm, old_w, memory_order_relaxed);
    atomic_store_explicit(&b_w, k, memory_order_release);
}
