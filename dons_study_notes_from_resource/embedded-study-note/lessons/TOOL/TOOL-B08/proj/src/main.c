/* main.c — sample 64 values, filter, CRC the filtered stream, report. */
#include <stdio.h>
#include "hal.h"
#include "dsp.h"

#define NSAMP 64u

static mavg_t  g_filt;            /* layout depends on FILTER_TAPS (board.h via dsp.h) */
static uint8_t g_out[2u * NSAMP]; /* filtered samples, little-endian */

int main(void)
{
    char msg[96];

    uart_init(UART_BAUD);
    sensor_init(12345u);
    mavg_init(&g_filt);
    for (uint32_t i = 0; i < NSAMP; i++) {
        int32_t y = mavg_push(&g_filt, sensor_read());
        g_out[2u * i] = (uint8_t)(y & 0xFF);
        g_out[2u * i + 1u] = (uint8_t)((y >> 8) & 0xFF);
    }
    snprintf(msg, sizeof msg, "taps=%u samples=%u crc=0x%04X",
             (unsigned)FILTER_TAPS, (unsigned)NSAMP, (unsigned)crc16_ccitt(g_out, sizeof g_out));
    log_msg("app", msg);
    return 0;
}
