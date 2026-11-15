/* dsp.h — moving average + CRC. Includes board.h for FILTER_TAPS. */
#ifndef DSP_H
#define DSP_H

#include <stddef.h>
#include <stdint.h>
#include "board.h"

typedef struct {
    int32_t  buf[FILTER_TAPS];
    uint32_t idx;
    uint32_t n;
    int32_t  sum;
} mavg_t;

void     mavg_init(mavg_t *m);
int32_t  mavg_push(mavg_t *m, int32_t x);
uint16_t crc16_ccitt(const uint8_t *p, size_t n);

#endif
