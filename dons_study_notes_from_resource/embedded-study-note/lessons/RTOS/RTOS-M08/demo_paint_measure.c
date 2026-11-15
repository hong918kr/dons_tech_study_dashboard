/* BUILD: bash measure_su.sh && cc -std=c11 -O2 -Wall -Wextra -Werror demo_paint_measure.c /tmp/RTOS-M08_su/a64/fw_graph.o -o /tmp/RTOS-M08_paint && /tmp/RTOS-M08_paint
 *
 * RTOS-M08 데모 — 진짜 스택에서 "실측": ucontext 스택(64 KB)을 0xA5 로 칠하고, fw_graph.c 의 함수 하나를 그 위에서
 * 한 번 돌린 뒤, 바닥부터 0xA5 가 몇 바이트 남았나 센다 (FreeRTOS prvTaskCheckFreeStackSpace 와 같은 방법).
 *   measured(f) = (f 를 부르기 직전의 SP) − (가장 낮게 덮어쓰인 주소)
 * 같은 객체 파일(/tmp/RTOS-M08_su/a64/fw_graph.o, -fstack-usage 로 .su 를 낸 그 빌드)을 링크하므로
 * arm64 .su 의 정적 최악 경로와 바이트 단위로 비교할 수 있다. arm64(맥) 전용 — SP 를 inline asm 으로 읽는다.
 * (_XOPEN_SOURCE 600: macOS <ucontext.h> 요구. ucontext 는 deprecated 라 그 경고만 끈다 — RTOS-B03 참고)
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fw_graph.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if !defined(__aarch64__)
#error "arm64 전용 데모 (SP 읽기)"
#endif

#define STK (64 * 1024)
static unsigned char *g_stack;
static ucontext_t g_main, g_task;
static void (*g_fn)(void);
static uintptr_t g_sp_at_call, g_low;

static inline uintptr_t read_sp(void) { uintptr_t sp; __asm__ volatile("mov %0, sp" : "=r"(sp)); return sp; }
/* 호출 직전 SP 를 기록하고, 돌아온 "직후" 그 자리에서(함수 호출 없이, 레지스터만 쓰는 루프로) 칠을 스캔한다.
 * 스캔을 썽크가 끝난 뒤에 하면 ucontext 종료 경로(_ctx_done → setcontext)가 썽크 SP 아래 32 B 를 덮어
 * 프레임 0 인 잎 함수도 32 B 로 나온다 — 처음 그렇게 쟀다가 고친 것 (md 5. 풀이 Part C). 호출 뒤에 할 일이
 * 있으니 꼬리 호출(b)도 생기지 않는다. */
#define MEASURE_CALL(call) do { g_sp_at_call = read_sp(); call; size_t i_ = 0; \
        while (i_ < STK && g_stack[i_] == 0xA5) i_++; g_low = (uintptr_t)g_stack + i_; } while (0)

static ctl_state_t st;
static imu_raw_t raw;
static char txt[128];
static uint8_t pkt[64], enc[80];

/* 함수마다 얇은 썽크: 호출 직전 SP 를 기록하고 부른다 (noinline 이라 썽크 프레임은 측정 밖) */
__attribute__((noinline)) static void t_spi(void)    { MEASURE_CALL(spi_burst_read(0x3B, pkt, 14)); }
__attribute__((noinline)) static void t_imu(void)    { MEASURE_CALL((void)imu_read(&raw)); }
__attribute__((noinline)) static void t_filter(void) { MEASURE_CALL(filter_update(&st.att, &raw)); }
__attribute__((noinline)) static void t_pid(void)    { MEASURE_CALL((void)pid_step(&st.pid[0], 1.0f, 0.5f)); }
__attribute__((noinline)) static void t_fmt(void)    { MEASURE_CALL((void)fmt_u32(txt, 4294967295u, 10)); }
__attribute__((noinline)) static void t_uart(void)   { MEASURE_CALL(uart_write("hello", 5)); }
__attribute__((noinline)) static void t_log(void)    { MEASURE_CALL(log_printf("c=%u r=%d p=%d u=%x %s\n", 1u, -12345, 678, 0xbeefu, "ok")); }
__attribute__((noinline)) static void t_ctrl(void)   { MEASURE_CALL(ctrl_task_once(&st)); }
__attribute__((noinline)) static void t_crc(void)    { MEASURE_CALL((void)crc16_ccitt(pkt, 48)); }
__attribute__((noinline)) static void t_cobs(void)   { MEASURE_CALL((void)cobs_encode(pkt, 48, enc)); }
__attribute__((noinline)) static void t_comms(void)  { MEASURE_CALL(comms_task_once(&st)); }
__attribute__((noinline)) static void t_rx(void)     { MEASURE_CALL((void)rx_parse_once()); }
__attribute__((noinline)) static void t_uartisr(void){ MEASURE_CALL(isr_uart_rx()); }
__attribute__((noinline)) static void t_adc(void)    { MEASURE_CALL(isr_adc_dma()); }
__attribute__((noinline)) static void t_rec(void)    { MEASURE_CALL((void)fmt_u32_rec(txt, 4294967295u)); }

static void trampoline(void) { g_fn(); }

static long measure(void (*fn)(void)) {
    g_fn = fn;
    getcontext(&g_task);
    g_task.uc_stack.ss_sp = g_stack;
    g_task.uc_stack.ss_size = STK;
    g_task.uc_link = &g_main;
    makecontext(&g_task, trampoline, 0);
    memset(g_stack, 0xA5, STK);                          /* 칠하기 — makecontext 뒤에 (macOS makecontext 가 스택을 0 으로 지운다, RTOS-B03) */
    swapcontext(&g_main, &g_task);
    return (long)(g_sp_at_call - g_low);                 /* g_low = 바닥부터 칠이 끊긴 첫 주소 (여유의 끝) */
}

/* rx 링 버퍼에 유효한 프레임 하나: 'C' 1 2 3 + CRC16 + 0x00 → rx_parse_once 가 crc16_ccitt 까지 탄다 */
static void prime_rx(void) {
    uint8_t f[6] = { 'C', 1, 2, 3, 0, 0 };
    uint16_t c = crc16_ccitt(f, 4);
    f[4] = (uint8_t)c; f[5] = (uint8_t)(c >> 8);
    for (int i = 0; i < 6; i++) g_uart_rx_ring[i] = f[i];
    g_uart_rx_ring[6] = 0;
    g_uart_rx_tail = 0; g_uart_rx_head = 7;
}

int main(void) {
    g_stack = aligned_alloc(16, STK);
    if (!g_stack) return 1;
    for (int i = 0; i < 32; i++) g_spi_fifo[i] = (uint8_t)(i * 7 + 1);
    for (int i = 0; i < 48; i++) pkt[i] = (uint8_t)(i % 5);       /* 0 이 섞여 COBS 가 분기한다 */
    st.pid[0].hi = st.pid[1].hi = st.pid[2].hi = 10.0f;
    st.pid[0].lo = st.pid[1].lo = st.pid[2].lo = -10.0f;
    struct { const char *name; void (*fn)(void); } T[] = {
        { "spi_burst_read", t_spi }, { "imu_read", t_imu }, { "filter_update", t_filter },
        { "pid_step", t_pid }, { "fmt_u32", t_fmt }, { "uart_write", t_uart }, { "log_printf", t_log },
        { "ctrl_task_once", t_ctrl }, { "crc16_ccitt", t_crc }, { "cobs_encode", t_cobs },
        { "comms_task_once", t_comms }, { "rx_parse_once", t_rx }, { "isr_uart_rx", t_uartisr },
        { "isr_adc_dma", t_adc }, { "fmt_u32_rec(10 digits)", t_rec },
    };
    printf("painted-stack measurement on arm64 (same object as a64/fw_graph.su)\n");
    printf("%-24s %8s\n", "function (root)", "measured");
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        prime_rx();                                      /* rx 경로가 crc16_ccitt 까지 가게 (다른 함수엔 무관) */
        long a = measure(T[i].fn);
        prime_rx();
        long b = measure(T[i].fn);                       /* 두 번 재서 같은지 (결정적인가) */
        printf("%-24s %6ld B%s\n", T[i].name, a, a == b ? "" : "  (unstable!)");
    }
    free(g_stack);
    return 0;
}
