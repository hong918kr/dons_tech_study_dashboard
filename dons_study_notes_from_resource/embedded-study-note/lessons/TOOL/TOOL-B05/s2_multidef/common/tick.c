/* tick.c — SysTick ISR 흉내: 1 ms 마다 g_ticks++ */
#include "config.h"

void tick_isr(void)
{
    g_ticks++;
}
