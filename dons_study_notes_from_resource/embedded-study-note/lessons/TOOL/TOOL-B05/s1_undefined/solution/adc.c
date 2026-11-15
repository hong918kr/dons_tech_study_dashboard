/* adc.c — 12비트 ADC (호스트에서는 고정값 2048 을 돌려준다) */
#include "adc.h"

int adc_read_raw(void)           /* static 제거 → 외부 링크(external linkage) */
{
    return 2048;
}

int adc_read_mv(void)
{
    return adc_read_raw() * 3300 / 4096;
}
