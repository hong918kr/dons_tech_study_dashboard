/* filter.c — moving average over FILTER_TAPS samples (no libc: plain loop, no memset). */
#include "dsp.h"

void mavg_init(mavg_t *m)
{
    for (int i = 0; i < FILTER_TAPS; i++)
        m->buf[i] = 0;
    m->idx = 0;
    m->n = 0;
    m->sum = 0;
}

int32_t mavg_push(mavg_t *m, int32_t x)
{
    m->sum += x - m->buf[m->idx];
    m->buf[m->idx] = x;
    m->idx = (m->idx + 1u) % FILTER_TAPS;
    if (m->n < FILTER_TAPS)
        m->n++;
    return m->sum / (int32_t)m->n;
}
