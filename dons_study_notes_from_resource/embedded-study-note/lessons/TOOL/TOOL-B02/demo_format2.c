/* demo_format2.c - TOOL-B02: what -Wformat=2 adds on top of -Wall's -Wformat */
#include <stdarg.h>
#include <stdio.h>

void log_raw(const char *msg);
void log_printf(const char *fmt, ...);

void log_raw(const char *msg)
{
    printf(msg);                        /* P1: msg may contain '%' (from UART!) */
}

void log_printf(const char *fmt, ...)   /* P2: no format(printf,1,2) attribute  */
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
