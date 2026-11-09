/* LLAT-A06 demo_cortexm — 펌웨어판 hot path: "id + 시각 + 원시 인자" 10 B 를 링에 (Trice/RTT 와 같은 생각)
 * 포맷 문자열은 칩(플래시)에 없다 — 호스트 디코더의 사전에만 있다.
 * build (asm 만 본다, libc 없음):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cortexm.c -o -
 */
#include <stdatomic.h>
#include <stdint.h>

#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)   /* Cortex-M3/M4/M7 사이클 카운터 (M0 에는 DWT CYCCNT 없음) */
#define RING 1024u                                         /* 2^n 바이트 */

static uint8_t ring[RING];
static _Atomic uint32_t tail, head;                        /* tail = 생산자, head = 소비자(main 루프 / 디버그 프로브) */
static uint32_t dropped;

static inline void put8(uint32_t pos, uint8_t b) { ring[pos & (RING - 1u)] = b; }   /* 바이트 단위 → 끝에서 쪼개져도 됨 (DS-M10) */

/* 생산자가 하나일 때 (예: 한 ISR 만 로그) — SPSC 그대로 */
int log_id1(uint16_t id, uint32_t a) {
    uint32_t t = atomic_load_explicit(&tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&head, memory_order_acquire);
    if (RING - (t - h) < 10u) { dropped++; return 0; }
    uint32_t ts = DWT_CYCCNT;
    put8(t + 0, (uint8_t)id); put8(t + 1, (uint8_t)(id >> 8));
    for (int i = 0; i < 4; i++) put8(t + 2u + (uint32_t)i, (uint8_t)(ts >> (8 * i)));
    for (int i = 0; i < 4; i++) put8(t + 6u + (uint32_t)i, (uint8_t)(a >> (8 * i)));
    atomic_store_explicit(&tail, t + 10u, memory_order_release);
    return 1;
}

/* 소비자 (main 루프에서 UART/SWO 로 내보내기): 최대 n 바이트를 꺼낸다.
 * 이 함수가 같은 파일에 있어야 컴파일러가 ring/head 를 "아무도 안 읽는 static" 으로 지우지 않는다 (실제로 지웠다). */
uint32_t log_drain(uint8_t *out, uint32_t n) {
    uint32_t h = atomic_load_explicit(&head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&tail, memory_order_acquire);
    uint32_t k = 0;
    while (h != t && k < n) out[k++] = ring[h++ & (RING - 1u)];
    atomic_store_explicit(&head, h, memory_order_release);
    return k;
}

/* 여러 문맥(main + 여러 ISR)이 같은 링에 쓸 때 — 단일 코어라면 예약~commit 동안 인터럽트를 막는 게 가장 짧다 */
static inline uint32_t irq_save(void) { uint32_t p; __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(p) : : "memory"); return p; }
static inline void irq_restore(uint32_t p) { __asm volatile("msr primask, %0" : : "r"(p) : "memory"); }
int log_id1_any(uint16_t id, uint32_t a) {
    uint32_t pm = irq_save();
    int ok = log_id1(id, a);
    irq_restore(pm);
    return ok;
}
