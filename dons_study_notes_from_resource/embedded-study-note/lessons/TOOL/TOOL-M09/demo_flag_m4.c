/* TOOL-M09 demo_flag_m4.c — 같은 busy-wait 를 Cortex-M4 로 컴파일하면? (freestanding, 링크 안 함)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_flag_m4.c -o -
 *   cc -std=c11 -O2 -S demo_flag_m4.c -o -                          # arm64 호스트
 */
#include <stdatomic.h>
#include <stdbool.h>

bool g_ready_plain;                      /* ISR 이 세우는 플래그 — 평범한 bool */
atomic_bool g_ready_atomic;              /* C11 atomic */
int g_sample;

void uart_rx_isr(void) {                 /* "ISR": 데이터 쓰고 플래그 */
    g_sample = 42;
    g_ready_plain = true;
}

int wait_plain(void) {
    while (!g_ready_plain) { }           /* 데이터 레이스 → UB, 컴파일러는 루프를 "끝난다" 고 가정해도 된다 */
    return g_sample;
}

int wait_atomic(void) {
    while (!atomic_load_explicit(&g_ready_atomic, memory_order_acquire)) { }
    return g_sample;
}
