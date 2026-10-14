/*
 * T2-8  Running Median Filter (spike rejection)
 *
 * A thermistor channel occasionally returns a single bogus reading (ADC spike from motor
 * PWM noise). Filter it with the MEDIAN of the last W samples.
 *
 * Rules (agreed with the interviewer):
 *   - med_init(m, w): 1 <= w <= MED_MAX_W, otherwise return false.
 *   - med_push(m, x): add sample x, drop the oldest sample if the window already holds w,
 *     and return the median of the samples currently in the window.
 *   - Warm-up (fewer than w samples so far): use the samples available.
 *   - Even count: return the LOWER median, sorted[(count - 1) / 2]
 *     (always a real sample, no averaging, no overflow).
 *   - Values are int16_t in 0.1 degC. No malloc; O(W) per push is fine for small W.
 *
 * Reference solution.  Build & run:  ./run.sh T2-8 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define MED_MAX_W 15

typedef struct {
    int16_t ring[MED_MAX_W];     /* samples in arrival order (to know which one is oldest) */
    int16_t sorted[MED_MAX_W];   /* same samples, kept sorted ascending */
    uint8_t w;                   /* window size */
    uint8_t head;                /* next write position in ring = oldest sample when full */
    uint8_t count;               /* samples currently in the window (<= w) */
} MedianFilter;

bool med_init(MedianFilter *m, uint8_t w)
{
    if (w == 0 || w > MED_MAX_W) return false;
    memset(m, 0, sizeof *m);
    m->w = w;
    return true;
}

int16_t med_push(MedianFilter *m, int16_t x)
{
    if (m->count == m->w) {                     /* full: remove the oldest from sorted[] */
        int16_t old = m->ring[m->head];
        uint8_t i = 0;
        while (m->sorted[i] != old) i++;        /* it is there; remove ONE copy */
        for (; i + 1 < m->count; i++) m->sorted[i] = m->sorted[i + 1];
        m->count--;
    }
    m->ring[m->head] = x;                       /* overwrite oldest / append */
    m->head = (uint8_t)((m->head + 1u) % m->w);

    uint8_t j = m->count;                       /* insertion sort step */
    while (j > 0 && m->sorted[j - 1] > x) { m->sorted[j] = m->sorted[j - 1]; j--; }
    m->sorted[j] = x;
    m->count++;

    return m->sorted[(m->count - 1u) / 2u];
}

int main(void)
{
    MedianFilter m;

    /* 1. basic: W = 5, one spike of 900 is rejected */
    const int16_t in[]   = { 250, 252, 251, 900, 253, 254, 250, 249, 251 };
    const int16_t want[] = { 250, 250, 251, 251, 252, 253, 253, 253, 251 };
    CHECK(med_init(&m, 5));
    for (int i = 0; i < 9; i++) CHECK(med_push(&m, in[i]) == want[i]);

    /* 2. invalid window sizes */
    CHECK(!med_init(&m, 0));
    CHECK(!med_init(&m, MED_MAX_W + 1));

    /* 3. W = 1 is a pass-through */
    CHECK(med_init(&m, 1));
    CHECK(med_push(&m, 7) == 7 && med_push(&m, -3) == -3 && med_push(&m, 100) == 100);

    /* 4. duplicates: removing the oldest must delete only one copy */
    CHECK(med_init(&m, 3));
    med_push(&m, 5); med_push(&m, 5); med_push(&m, 9);   /* window {5,5,9} */
    CHECK(med_push(&m, 9) == 9);                         /* window {5,9,9} */
    CHECK(med_push(&m, 1) == 9);                         /* window {9,9,1} */
    CHECK(med_push(&m, 1) == 1);                         /* window {9,1,1} */

    /* 5. negative and extreme values, no overflow */
    CHECK(med_init(&m, 3));
    med_push(&m, INT16_MIN); med_push(&m, INT16_MAX);
    CHECK(med_push(&m, -400) == -400);
    CHECK(med_push(&m, INT16_MAX) == INT16_MAX);         /* window {32767,-400,32767} */

    /* 6. long run on a full window equals a brute-force median */
    CHECK(med_init(&m, 7));
    int16_t hist[64];
    uint32_t seed = 12345u;
    bool ok = true;
    for (int i = 0; i < 64; i++) {
        seed = seed * 1103515245u + 12345u;
        hist[i] = (int16_t)((seed >> 16) % 50u) - 25;
        int16_t got = med_push(&m, hist[i]);
        int lo = i - 6 < 0 ? 0 : i - 6, cnt = i - lo + 1, less, eq;
        int16_t ref = 0;
        for (int a = lo; a <= i; a++) {          /* value with rank (cnt-1)/2 */
            less = eq = 0;
            for (int b = lo; b <= i; b++) { less += hist[b] < hist[a]; eq += hist[b] == hist[a]; }
            if (less <= (cnt - 1) / 2 && (cnt - 1) / 2 < less + eq) { ref = hist[a]; break; }
        }
        if (got != ref) ok = false;
    }
    CHECK(ok);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
