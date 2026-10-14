/*
 * T1-2  Streaming Overheat Detector (hysteresis + N-sample confirmation)
 *
 * Samples arrive one at a time. Report an event only on the sample that causes it.
 *
 * Definitions (agreed with the interviewer):
 *   - NORMAL -> OVERHEATED after n_confirm CONSECUTIVE samples with temp_c10 >= high.
 *     That sample returns EV_OVERHEAT.
 *   - OVERHEATED -> NORMAL after n_confirm CONSECUTIVE samples with temp_c10 <= low.
 *     That sample returns EV_RECOVERED.
 *   - Any sample that breaks the streak resets the counter. Every other sample returns EV_NONE
 *     (no duplicate EV_OVERHEAT while already overheated).
 *   - TEMP_INVALID (INT16_MIN) is a sensor error: it returns EV_NONE and resets the counter.
 *   - det_init returns false (and leaves the detector unusable) if low >= high or n_confirm == 0.
 *   - O(1) state, no malloc.
 *
 * Build & run:  ./run.sh T1-2
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define TEMP_INVALID INT16_MIN   /* sensor error code */

typedef enum { EV_NONE, EV_OVERHEAT, EV_RECOVERED } Event;

typedef struct {
    int todo;   /* TODO: design your state (thresholds, counter, current state, ...) */
} Detector;

bool det_init(Detector *d, int16_t high, int16_t low, uint8_t n_confirm)
{
    /* TODO: your code here */
    (void)d; (void)high; (void)low; (void)n_confirm;
    return true;
}

Event det_update(Detector *d, int16_t temp_c10)
{
    /* TODO: your code here */
    (void)d; (void)temp_c10;
    return EV_NONE;
}

/* test helper: feed n samples, record every event */
static void feed(Detector *d, const int16_t *x, size_t n, Event *ev)
{
    for (size_t i = 0; i < n; i++) ev[i] = det_update(d, x[i]);
}

int main(void)
{
    Detector d;
    Event ev[16];

    /* 1. basic: overheat confirmed at index 3, recovery confirmed at index 8 */
    const int16_t a[] = { 840, 860, 870, 880, 860, 820, 790, 780, 770 };
    CHECK(det_init(&d, 850, 800, 3));
    feed(&d, a, 9, ev);
    for (size_t i = 0; i < 9; i++)
        CHECK(ev[i] == (i == 3 ? EV_OVERHEAT : i == 8 ? EV_RECOVERED : EV_NONE));

    /* 2. spikes never reach 3 in a row -> no events */
    const int16_t b[] = { 900, 700, 900, 700 };
    det_init(&d, 850, 800, 3);
    feed(&d, b, 4, ev);
    CHECK(ev[0] == EV_NONE && ev[1] == EV_NONE && ev[2] == EV_NONE && ev[3] == EV_NONE);

    /* 3. stays hot: exactly one EV_OVERHEAT, no duplicates */
    const int16_t c[] = { 900, 900, 900, 900, 900, 900, 900 };
    det_init(&d, 850, 800, 3);
    feed(&d, c, 7, ev);
    int n_over = 0;
    for (size_t i = 0; i < 7; i++) n_over += (ev[i] == EV_OVERHEAT);
    CHECK(n_over == 1 && ev[2] == EV_OVERHEAT);

    /* 4. hysteresis band: 820 (between low and high) resets the recovery streak */
    const int16_t e[] = { 900, 900, 900, 790, 790, 820, 790, 790, 790 };
    det_init(&d, 850, 800, 3);
    feed(&d, e, 9, ev);
    CHECK(ev[2] == EV_OVERHEAT && ev[4] == EV_NONE && ev[5] == EV_NONE && ev[8] == EV_RECOVERED);

    /* 5. sensor error value breaks the streak */
    const int16_t f[] = { 900, 900, TEMP_INVALID, 900, 900, 900 };
    det_init(&d, 850, 800, 3);
    feed(&d, f, 6, ev);
    CHECK(ev[2] == EV_NONE && ev[4] == EV_NONE && ev[5] == EV_OVERHEAT);

    /* 6. n_confirm == 1 and exact boundaries (>= high, <= low) */
    det_init(&d, 850, 800, 1);
    CHECK(det_update(&d, 849) == EV_NONE);
    CHECK(det_update(&d, 850) == EV_OVERHEAT);
    CHECK(det_update(&d, 801) == EV_NONE);
    CHECK(det_update(&d, 800) == EV_RECOVERED);

    /* 7. invalid configuration is rejected and the detector never fires */
    CHECK(!det_init(&d, 800, 800, 3));
    CHECK(!det_init(&d, 800, 850, 3));
    CHECK(!det_init(&d, 850, 800, 0));
    CHECK(det_update(&d, 900) == EV_NONE);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
