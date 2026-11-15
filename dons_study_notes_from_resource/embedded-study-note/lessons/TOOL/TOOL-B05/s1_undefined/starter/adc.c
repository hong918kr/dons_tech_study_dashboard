/* adc.c — 12비트 ADC (호스트에서는 고정값 2048 을 돌려준다) */
#include "adc.h"

static int adc_read_raw(void)    /* 파일 안에서만 쓰려던 도우미 */
{
    return 2048;
}

int adc_read_mv(void)
{
    return adc_read_raw() * 3300 / 4096;
}
