/* log.c — "[tag] msg" lines through the UART. */
#include <stdio.h>
#include "hal.h"

void log_msg(const char *tag, const char *msg)
{
    char line[128];
    snprintf(line, sizeof line, "[%s] %s\n", tag, msg);
    uart_write(line);
}
