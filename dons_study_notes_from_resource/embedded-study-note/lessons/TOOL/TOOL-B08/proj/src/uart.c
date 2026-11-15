/* uart.c — host stand-in for a UART driver: writes to stdout, counts bytes. */
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "board.h"

static uint32_t g_sent;

void uart_init(uint32_t baud)
{
    printf("uart: %u baud on %s\n", (unsigned)baud, BOARD_NAME);
}

void uart_write(const char *s)
{
    fputs(s, stdout);
    g_sent += (uint32_t)strlen(s);
}

uint32_t uart_bytes_sent(void)
{
    return g_sent;
}
