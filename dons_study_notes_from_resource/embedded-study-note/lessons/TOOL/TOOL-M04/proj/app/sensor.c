/* sensor.c — fake 8-bit ADC: deterministic LCG noise around a slow ramp (from TOOL-B08). */
#include "sensor.h"

static uint32_t g_state;
static uint32_t g_tick;

void sensor_init(uint32_t seed)
{
    g_state = seed;
    g_tick = 0;
}

uint8_t sensor_read(void)
{
    g_state = g_state * 1664525u + 1013904223u;
    uint32_t noise = (g_state >> 16) & 0x0Fu;
    uint32_t ramp = (g_tick++ % 100u) * 2u;
    return (uint8_t)((ramp + noise) & 0xFFu);
}
