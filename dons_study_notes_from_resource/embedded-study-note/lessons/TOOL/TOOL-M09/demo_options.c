/* TOOL-M09 demo_options.c — TSAN_OPTIONS 와 suppressions 실험
 *
 * 레이스 2개: (1) "벤더 SDK" 의 legacy_crc_update (우리가 못 고치는 코드), (2) 우리 코드 rx_count++.
 * 기본: 1단계(CRC 스레드 2개) → join → 2단계(RX 스레드 2개). -DONE_LOOP: 한 루프에서 둘 다.
 *
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_options.c -o do_t
 *   ./do_t                                                     # 보고 2개, exit 134 (abort)
 *   TSAN_OPTIONS=halt_on_error=1 ./do_t                        # 첫 보고에서 즉시 종료
 *   TSAN_OPTIONS=abort_on_error=0 ./do_t                       # abort 대신 exitcode(66) 로 종료
 *   TSAN_OPTIONS=suppressions=tsan.supp:print_suppressions=1 ./do_t   # 벤더 레이스만 숨김
 *   TSAN_OPTIONS=log_path=/tmp/tsan_log ./do_t                 # 보고를 /tmp/tsan_log.<pid> 파일로
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DONE_LOOP demo_options.c -o do_1 && ./do_1   # 보고 1개뿐
 *
 * tsan.supp (이 디렉터리에 있음):
 *   race:legacy_crc_update
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

/* ---- "벤더 SDK" : 소스는 있지만 고칠 권한이 없다고 가정 ---- */
static uint32_t legacy_crc_state = 0xFFFFFFFFu;
__attribute__((noinline)) void legacy_crc_update(uint8_t b) {
    legacy_crc_state = (legacy_crc_state >> 8) ^ (uint32_t)(b * 0x01000193u);   /* 공유 상태, 락 없음 */
}

/* ---- 우리 코드 ---- */
static unsigned long rx_count;
__attribute__((noinline)) static void on_rx_byte(uint8_t b) {
#ifdef ONE_LOOP
    legacy_crc_update(b);
#else
    (void)b;
#endif
    rx_count++;                       /* 우리 버그 */
}

#ifndef ONE_LOOP
static void *crc_task(void *arg) {
    uint8_t base = (uint8_t)(uintptr_t)arg;
    for (int i = 0; i < 100000; i++) legacy_crc_update((uint8_t)(base + i));
    return NULL;
}
#endif
static void *uart_rx_task(void *arg) {
    uint8_t base = (uint8_t)(uintptr_t)arg;
    for (int i = 0; i < 100000; i++) on_rx_byte((uint8_t)(base + i));
    return NULL;
}

static void run_pair(void *(*fn)(void *)) {
    pthread_t a, b;
    pthread_create(&a, NULL, fn, (void *)(uintptr_t)1);
    pthread_create(&b, NULL, fn, (void *)(uintptr_t)2);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
}

int main(void) {
#ifndef ONE_LOOP
    run_pair(crc_task);               /* 1단계: 벤더 코드 레이스 */
#endif
    run_pair(uart_rx_task);           /* 2단계: 우리 코드 레이스 */
    printf("rx_count=%lu (expect 200000) crc=%08x\n", rx_count, legacy_crc_state);
    return 0;
}
