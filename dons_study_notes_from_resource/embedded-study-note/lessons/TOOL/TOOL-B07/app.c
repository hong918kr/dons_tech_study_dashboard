/* app.c — tiny "firmware main" shared by every build variant (do not edit).
 * It only uses the OUTPUT macros of board_config.h. On the Mac it runs as a host
 * program (stubs print instead of touching registers) so check.sh can verify the
 * variant at run time too.
 */
#include <stdio.h>
#include "board_config.h"

static unsigned char rx_buf[RX_BUF_SIZE];
static rx_idx_t rx_head;

void assert_fail(const char *file, int line, const char *expr)
{
    printf("ASSERT FAILED %s:%d %s\n", file, line, expr);
}

static void led_init(int pin) { printf("led_init pin=%d\n", pin); }
static void uart_init(long baud) { printf("uart_init baud=%ld\n", baud); }

#if HAS_USB
static void usb_init(void) { puts("usb_init"); }
#endif

#if HAS_SHELL
static void shell_start(void) { puts("shell_start"); }
#endif

static void rx_push(unsigned char byte)
{
    rx_buf[rx_head] = byte;
    rx_head = (rx_idx_t)((rx_head + 1u) & (RX_BUF_SIZE - 1u));
}

int main(void)
{
    printf("board=%s build=%s led=%d baud=%ld log=%d usb=%d shell=%d rx=%d\n",
           BOARD_NAME, BUILD_NAME, LED_PIN, (long)UART_BAUD, LOG_LEVEL,
           HAS_USB, HAS_SHELL, RX_BUF_SIZE);
    led_init(LED_PIN);
    uart_init(UART_BAUD);
    ASSERT(sizeof rx_buf == RX_BUF_SIZE);
    rx_push(0x55);
#if HAS_USB
    usb_init();
#endif
#if HAS_SHELL
    shell_start();
#endif
#if LOG_LEVEL >= 4
    puts("dbg: boot done");
#endif
    return 0;
}
