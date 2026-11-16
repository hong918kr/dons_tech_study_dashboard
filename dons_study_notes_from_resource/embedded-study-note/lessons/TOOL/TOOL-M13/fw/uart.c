/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 uart.c — CMSDK APB UART0 (mps2-an386: 0x40004000, QEMU 의 첫 -serial). libdrv.a 의 멤버.
 * uart_read_line + g_dma_rx[2048] 는 아무도 안 쓴다 → -fdata-sections 없으면 .bss 2 KB 가 남는다.
 */
#include "fw.h"

#define UART0_DATA  (*(volatile uint32_t *)0x40004000u)
#define UART0_STATE (*(volatile uint32_t *)0x40004004u) /* bit0 TX full, bit1 RX full */
#define UART0_CTRL  (*(volatile uint32_t *)0x40004008u) /* bit0 TX enable, bit1 RX enable */
#define UART0_BAUD  (*(volatile uint32_t *)0x40004010u)

void uart_init(void) {
    UART0_BAUD = 16u;
    UART0_CTRL = 3u;
}

void uart_putc(char c) {
    for (uint32_t n = 0; (UART0_STATE & 1u) && n < 100000u; n++) {
    }
    UART0_DATA = (uint8_t)c;
}

void uart_write(const char *s, size_t n) {
    while (n--) uart_putc(*s++);
}

/* ---- dead: DMA 수신 버퍼와 줄 읽기 (기능을 빼면서 호출만 지웠다) ---- */
uint8_t g_dma_rx[2048];

size_t uart_read_line(char *buf, size_t n) {
    size_t i = 0;
    while (i + 1 < n) {
        while (!(UART0_STATE & 2u)) {
        }
        char c = (char)UART0_DATA;
        g_dma_rx[i % sizeof g_dma_rx] = (uint8_t)c;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

#ifdef USE_PRINTF
/* newlib 의 printf → _write_r → _write. nosys.specs 의 스텁(항상 실패)보다 이 정의가 이긴다. */
int _write(int fd, const char *s, int n);
int _write(int fd, const char *s, int n) {
    (void)fd;
    uart_write(s, (size_t)n);
    return n;
}
#endif
