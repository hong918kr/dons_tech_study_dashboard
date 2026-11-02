#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
#define MA_MAX 64

typedef struct {
    uint16_t buf[MA_MAX];
    uint32_t sum;            /* 64 * 65535 < 2^32: cannot overflow */
    uint8_t  window;         /* N, 1..MA_MAX */
    uint8_t  idx;            /* next slot to overwrite */
    uint8_t  count;          /* samples seen, saturates at window */
} movavg_t;

bool ma_init(movavg_t *m, unsigned window)
{
    if (window == 0 || window > MA_MAX)
        return false;
    memset(m, 0, sizeof *m);
    m->window = (uint8_t)window;
    return true;
}

/* Push one sample, return rounded mean of the last min(count, N) samples. */
uint16_t ma_update(movavg_t *m, uint16_t x)
{
    if (m->count < m->window)
        m->count++;                       /* warm-up: slot was empty (0) */
    else
        m->sum -= m->buf[m->idx];         /* drop oldest */
    m->buf[m->idx] = x;
    m->sum += x;
    m->idx = (uint8_t)((m->idx + 1u) % m->window);
    return (uint16_t)((m->sum + m->count / 2u) / m->count);
}
/* ---- end model answer ---- */

int main(void)
{
    movavg_t m;
    assert(!ma_init(&m, 0));
    assert(!ma_init(&m, MA_MAX + 1));

    /* page example, N = 4 */
    assert(ma_init(&m, 4));
    assert(ma_update(&m, 10) == 10);      /* 10/1 */
    assert(ma_update(&m, 20) == 15);      /* 30/2 */
    assert(ma_update(&m, 30) == 20);      /* 60/3 */
    assert(ma_update(&m, 40) == 25);      /* 100/4 */
    assert(ma_update(&m, 50) == 35);      /* 140/4: 10 dropped */
    assert(ma_update(&m, 0) == 30);       /* 120/4: 20 dropped */
    assert(m.sum == 120 && m.count == 4);

    /* rounding: (1+2)/2 = 1.5 -> 2 */
    assert(ma_init(&m, 2));
    assert(ma_update(&m, 1) == 1);
    assert(ma_update(&m, 2) == 2);

    /* N = 1 -> output == input */
    assert(ma_init(&m, 1));
    for (unsigned v = 0; v < 70000; v += 997)
        assert(ma_update(&m, (uint16_t)v) == (uint16_t)v);

    /* max values, max window: no overflow, steady state = 65535 */
    assert(ma_init(&m, MA_MAX));
    for (int i = 0; i < 3 * MA_MAX; i++)
        assert(ma_update(&m, 0xFFFF) == 0xFFFF);
    assert(m.sum == (uint32_t)MA_MAX * 0xFFFFu);

    /* step response then back: sum returns exactly to 0 (no drift) */
    for (int i = 0; i < MA_MAX; i++)
        ma_update(&m, 0);
    assert(m.sum == 0 && ma_update(&m, 0) == 0);

    puts("D8 PASS");
    return 0;
}
