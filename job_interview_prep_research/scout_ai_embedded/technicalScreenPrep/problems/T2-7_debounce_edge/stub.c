/*
 * T2-7  Debounce an Over-Temp Alert Pin + Edge Detection
 *
 * A temperature sensor drives an active-high ALERT pin. A 1 kHz timer ISR samples the pin
 * and calls deb_update() once per sample. Motor EMI causes short glitches on the line.
 *
 * Rules (agreed with the interviewer):
 *   - The debounced (stable) level changes only after the raw input has been DIFFERENT from
 *     the stable level for n_required CONSECUTIVE samples. Any sample equal to the stable
 *     level resets the count.
 *   - On the sample where the stable level changes, return EDGE_RISING (0 -> 1) or
 *     EDGE_FALLING (1 -> 0) exactly once. Otherwise return EDGE_NONE.
 *   - raw is 0 or non-zero (non-zero means high). n_required == 0 is treated as 1.
 *   - deb_init() sets the starting stable level (no edge is reported for it).
 *   - O(1) time and memory per sample, no malloc.
 *
 * Build & run:  ./run.sh T2-7
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef enum { EDGE_NONE = 0, EDGE_RISING, EDGE_FALLING } Edge;

typedef struct {
    uint8_t  stable;       /* debounced level, 0 or 1 */
    uint16_t count;        /* consecutive samples that disagree with stable */
    uint16_t n_required;   /* samples needed to accept a new level */
} Debouncer;

void deb_init(Debouncer *d, uint16_t n_required, uint8_t initial)
{
    /* TODO: your code here */
    (void)d; (void)n_required; (void)initial;
}

Edge deb_update(Debouncer *d, uint8_t raw)
{
    /* TODO: your code here */
    (void)d; (void)raw;
    return EDGE_NONE;
}

/* test helper: feed raw[] and record (index, edge) pairs; returns number of edges */
static size_t run(const uint8_t *raw, size_t n, uint16_t req, uint8_t init,
                  size_t *at, Edge *ev, size_t cap)
{
    Debouncer d;
    size_t k = 0;
    deb_init(&d, req, init);
    for (size_t i = 0; i < n; i++) {
        Edge e = deb_update(&d, raw[i]);
        if (e != EDGE_NONE) {
            if (k < cap) { at[k] = i; ev[k] = e; }
            k++;
        }
    }
    return k;
}

int main(void)
{
    size_t at[16];
    Edge ev[16];
    size_t k;

    /* 1. basic: glitches ignored, one RISING at i=5, one FALLING at i=12 */
    const uint8_t a[] = { 0,1,0,1,1,1,1,0,1,1,0,0,0,1,0,0 };
    k = run(a, 16, 3, 0, at, ev, 16);
    CHECK(k == 2);
    CHECK(k >= 2 && at[0] == 5 && ev[0] == EDGE_RISING && at[1] == 12 && ev[1] == EDGE_FALLING);

    /* 2. alternating noise never becomes stable */
    const uint8_t b[] = { 1,0,1,0,1,0,1,0,1,0 };
    CHECK(run(b, 10, 2, 0, at, ev, 16) == 0);

    /* 3. a long high level reports RISING once, not every sample */
    const uint8_t c[] = { 1,1,1,1,1,1,1,1,1,1 };
    k = run(c, 10, 3, 0, at, ev, 16);
    CHECK(k == 1 && at[0] == 2 && ev[0] == EDGE_RISING);

    /* 4. initial level already high: no edge for steady high input */
    CHECK(run(c, 10, 3, 1, at, ev, 16) == 0);

    /* 5. a glitch in the middle restarts the count: 1 1 0 1 1 1 -> RISING at i=5, not i=2 */
    const uint8_t e[] = { 1,1,0,1,1,1 };
    k = run(e, 6, 3, 0, at, ev, 16);
    CHECK(k == 1 && at[0] == 5);

    /* 6. n_required 0 or 1 -> every change is an edge; non-zero raw counts as high */
    const uint8_t f[] = { 0,7,7,0,1 };
    k = run(f, 5, 0, 0, at, ev, 16);
    CHECK(k == 3 && at[0] == 1 && ev[0] == EDGE_RISING && at[1] == 3 && ev[1] == EDGE_FALLING && at[2] == 4);
    CHECK(run(f, 5, 1, 0, at, ev, 16) == 3);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
