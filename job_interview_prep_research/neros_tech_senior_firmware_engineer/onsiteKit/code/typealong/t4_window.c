/* T4 last-N moving average + K-of-N alarm -- "battery reading every 10 ms, sometimes garbage" */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX_N 32

typedef struct {
    unsigned n, k;
    float low;
    float vals[MAX_N];        /* ring of valid values */
    unsigned vhead, vcount;
    float total;
    bool flags[MAX_N];        /* ring of "bad?" flags */
    unsigned fhead, fcount, bad;
    unsigned invalid;
} monitor_t;

bool monitor_init(monitor_t *m, unsigned n, unsigned k, float low)
{
    if (n == 0 || n > MAX_N || k == 0 || k > n)
        return false;
    *m = (monitor_t){.n = n, .k = k, .low = low};
    return true;
}

/* returns alarm; *avg = NAN when no valid sample yet */
bool monitor_add(monitor_t *m, float x, float *avg)
{
    bool ok = !isnan(x);                      /* NaN check FIRST: NaN < low is false */
    if (!ok) {
        m->invalid++;
    } else {
        if (m->vcount == m->n) {
            m->total -= m->vals[m->vhead];    /* O(1): subtract what falls out */
            m->vcount--;
        }
        m->vals[m->vhead] = x;
        m->vhead = (m->vhead + 1) % m->n;
        m->vcount++;
        m->total += x;
    }
    bool is_bad = !ok || x < m->low;
    if (m->fcount == m->n) {
        if (m->flags[m->fhead])
            m->bad--;                         /* oldest flag falls out */
        m->fcount--;
    }
    m->flags[m->fhead] = is_bad;
    m->fhead = (m->fhead + 1) % m->n;
    m->fcount++;
    m->bad += is_bad;
    *avg = m->vcount ? m->total / (float)m->vcount : NAN;
    return m->bad >= m->k;
}

int main(void)
{
    monitor_t m;
    float avg;
    assert(!monitor_init(&m, 3, 4, 10.0f));                 /* k > n rejected */
    assert(monitor_init(&m, 3, 2, 10.0f));
    assert(!monitor_add(&m, NAN, &avg) && isnan(avg) && m.invalid == 1);  /* no divide by zero */
    assert(!monitor_add(&m, 12.0f, &avg) && avg == 12.0f);  /* warm-up */
    assert(monitor_add(&m, 9.0f, &avg));                     /* NaN + 9.0 = 2 bad of last 3 */
    assert(!monitor_add(&m, 12.0f, &avg) && avg == 11.0f);   /* NaN fell out -> 1 bad */
    for (int i = 0; i < 3; i++)
        monitor_add(&m, 12.0f, &avg);
    assert(avg == 12.0f);                                    /* window fully replaced */
    puts("t4 OK");
    return 0;
}
