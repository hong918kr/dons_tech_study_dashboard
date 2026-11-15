/* main.c — 세 파일이 config.h 의 전역 3개를 "같이" 쓴다 (TOOL-B05 s2) */
#include <stdio.h>
#include "config.h"

void tick_isr(void);
void log_error(const char *msg);

int main(void)
{
    for (int i = 0; i < 5; i++)
        tick_isr();
    log_error("sensor timeout");
    log_error("crc mismatch");
    printf("ticks=%lu errs=%u level=%d\n", g_ticks, g_err_count, g_log_level);
    return 0;
}
