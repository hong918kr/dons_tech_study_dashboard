/* uart.c — 가짜 UART 드라이버. 밖에 보일 것만 non-static, 나머지는 static. */
#include <stdbool.h>
#include "uart.h"

uint32_t uart_tx_bytes;                 /* 외부 연결, 0 초기화      → Mach-O S / ELF B */
int      uart_log_level = 2;            /* 외부 연결, 0 아닌 초기값  → D */

static uint32_t s_baud = 115200;        /* 내부 연결, 초기값 있음    → d */
static uint8_t  s_rxbuf[64];            /* 내부 연결, 0 초기화       → b */

#ifdef DEMO_COLLISION
void hw_reset(void) {                   /* static 을 빠뜨림 → 외부 연결 (T) */
#else
static void hw_reset(void) {            /* 내부 연결 함수            → t */
#endif
    s_rxbuf[0] = 0;
}

static void hw_putc(char c) {           /* → t */
    (void)c;
    uart_tx_bytes++;
}

unsigned uart_init(uint32_t baud) {     /* 외부 연결 함수            → T */
    static unsigned calls;              /* 함수 내 static: 연결 없음, 수명은 프로그램 전체 → b */
    static bool     done;
    calls++;
    if (!done) {                        /* init-once */
        hw_reset();
        s_baud = baud;
        done = true;
    }
    return calls;
}

void uart_send(const char *s) {
    while (*s) hw_putc(*s++);
}

uint32_t uart_baud(void) { return s_baud; }

#ifdef DEMO_TENTATIVE
int debug_flags;                        /* tentative definition — gpio.c 에도 똑같이 있다 */
#endif
