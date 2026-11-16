/* dsp.h — portable DSP helpers (CRC-16/CCITT-FALSE + moving average).
 * Only <stdint.h>/<stddef.h>: the SAME source builds for the Mac host (unit tests)
 * and for a bare-metal Cortex-M4 with no C library (firmware). */
#ifndef DSP_H
#define DSP_H

#include <stddef.h>
#include <stdint.h>

#ifndef FILTER_TAPS
#define FILTER_TAPS 4
#endif

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
