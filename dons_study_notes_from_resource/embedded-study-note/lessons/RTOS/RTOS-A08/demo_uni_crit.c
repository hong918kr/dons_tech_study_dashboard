/* RTOS-A08 demo: "인터럽트 끄기 = 상호 배제" 가 두 코어에서 깨지는 것을 진짜 스레드로 본다.
 *
 *   core1 스레드 = UART RX ISR : 임계 구역 안에서 count = count + 1   (바이트 도착)
 *   core0 스레드 = 파서 태스크  : 임계 구역 안에서 count > 0 이면 count = count - 1   (바이트 소비)
 *
 *   ./demo_uni_crit uni   → 임계 구역 = 자기 코어 인터럽트 마스크만 (단일 코어 RTOS 의 taskENTER_CRITICAL)
 *   ./demo_uni_crit smp   → 마스크 + 커널 스핀락 (FreeRTOS SMP 의 taskENTER_CRITICAL)
 *
 * 끝나면 count 는 반드시 pushed - popped 여야 한다. uni 는 의도적으로 데이터 레이스(UB)를 만든다 —
 * TSan 으로 보라:  cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_uni_crit.c -o d && ./d uni
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 2000000
static bool g_irq_masked[2];             /* 코어마다 자기 PRIMASK */
static atomic_int g_lock;                /* 커널 스핀락 */
static int g_smp;
static volatile uint32_t g_count;        /* 링 버퍼의 count (공유) */
static uint32_t g_popped;

static void crit_enter(int c) {
    g_irq_masked[c] = true;              /* __disable_irq() — 이 코어만 */
    if (g_smp)
        while (atomic_exchange_explicit(&g_lock, 1, memory_order_acquire))
            while (atomic_load_explicit(&g_lock, memory_order_relaxed)) { }
}
static void crit_exit(int c) {
    if (g_smp) atomic_store_explicit(&g_lock, 0, memory_order_release);
    g_irq_masked[c] = false;             /* __enable_irq() */
}
static void *uart_isr_core1(void *a) {
    (void)a;
    for (int i = 0; i < N; i++) { crit_enter(1); g_count = g_count + 1; crit_exit(1); }
    return NULL;
}
static void *parser_core0(void *a) {
    (void)a;
    for (int i = 0; i < N; i++) {
        crit_enter(0);
        uint32_t n = g_count;
        if (n > 0) { g_count = n - 1; g_popped++; }
        crit_exit(0);
    }
    return NULL;
}
int main(int argc, char **argv) {
    g_smp = argc > 1 && strcmp(argv[1], "smp") == 0;
    pthread_t a, b;
    pthread_create(&a, NULL, parser_core0, NULL);
    pthread_create(&b, NULL, uart_isr_core1, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    long expect = (long)N - (long)g_popped;
    printf("%s: pushed=%d popped=%u count=%u expected=%ld  -> %s (diff %ld)\n",
           g_smp ? "smp" : "uni", N, g_popped, g_count, expect,
           (long)g_count == expect ? "OK" : "CORRUPTED", (long)g_count - expect);
    return 0;
}
