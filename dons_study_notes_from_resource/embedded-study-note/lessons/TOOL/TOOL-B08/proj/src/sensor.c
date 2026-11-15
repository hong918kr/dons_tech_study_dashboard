/* sensor.c — fake ADC: deterministic LCG noise around a slow ramp. */
#include "hal.h"
#include "board.h"

static uint32_t g_state;
static uint32_t g_tick;

void sensor_init(uint32_t seed)
{
    g_state = seed;
    g_tick = 0;
}

int16_t sensor_read(void)
{
    g_state = g_state * 1664525u + 1013904223u;
    int32_t noise = (int32_t)((g_state >> 16) & 0xFFu) - 128;
    int32_t ramp = (int32_t)(g_tick++ % SENSOR_RATE_HZ) * 8;
    return (int16_t)(ramp + noise);
}
