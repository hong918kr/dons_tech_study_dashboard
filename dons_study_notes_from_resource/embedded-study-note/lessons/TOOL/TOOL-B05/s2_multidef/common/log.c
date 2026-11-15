/* log.c — 에러 로그: 레벨 이상이면 출력하고 g_err_count++ */
#include <stdio.h>
#include "config.h"

void log_error(const char *msg)
{
    g_err_count++;
    if (g_log_level >= 1)
        printf("E[%lu] %s\n", g_ticks, msg);
}
