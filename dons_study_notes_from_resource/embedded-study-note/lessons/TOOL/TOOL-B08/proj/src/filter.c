/* filter.c — moving average over FILTER_TAPS samples. */
#include <string.h>
#include "dsp.h"

void mavg_init(mavg_t *m)
{
    memset(m, 0, sizeof *m);
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
