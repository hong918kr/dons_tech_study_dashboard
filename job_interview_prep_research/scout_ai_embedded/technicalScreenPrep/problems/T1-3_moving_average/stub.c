/*
 * T1-3  Moving Average + Rise-Rate Overheat
 *
 * Keep the average of the last WIN samples in O(1) per sample, then flag overheat when
 * the average is at or above avg_thresh OR the rise across the window is at or above rise_thresh.
 *
 * Definitions (agreed with the interviewer):
 *   - Ring buffer of WIN int16_t samples + running sum in int32_t (WIN * INT16 range fits easily).
 *   - While the window is still filling, divide by count (number of samples so far), not WIN.
 *   - Average is ROUNDED HALF AWAY FROM ZERO:  15/2 -> 8,  -15/2 -> -8,  5/3 -> 2,  -5/3 -> -2.
 *     (Plain C division truncates toward zero: -15/2 == -7. That is NOT what we want.)
 *   - rise = newest - oldest sample currently in the window (0 with one sample).
 *   - ma_overheat() on an empty window returns false.
 *   - No malloc, no floating point.
 *
 * Build & run:  ./run.sh T1-3
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define WIN 4   /* 8 in the original spec; 4 keeps the tests short */

typedef struct {
    int16_t buf[WIN];
    uint8_t idx;     /* next slot to write (= oldest slot once full) */
    uint8_t count;   /* samples in the window, 0..WIN */
    int32_t sum;     /* sum of the samples in the window */
} MovAvg;

void ma_init(MovAvg *m)
{
    /* TODO: your code here */
    (void)m;
}

/* current average of the window, rounded half away from zero (0 if empty) */
int16_t ma_avg(const MovAvg *m)
{
    /* TODO: your code here */
    (void)m;
    return 0;
}

/* add x, evicting the oldest sample when full; returns ma_avg() */
int16_t ma_push(MovAvg *m, int16_t x)
{
    /* TODO: your code here */
    (void)m; (void)x;
    return 0;
}

/* true if avg >= avg_thresh or (newest - oldest) >= rise_thresh; false if empty */
bool ma_overheat(const MovAvg *m, int16_t avg_thresh, int16_t rise_thresh)
{
    /* TODO: your code here */
    (void)m; (void)avg_thresh; (void)rise_thresh;
    return false;
}

int main(void)
{
    MovAvg m;
    memset(&m, 0, sizeof m);

    /* 1. basic: filling then sliding (window becomes 20,30,40,50) */
    ma_init(&m);
    CHECK(ma_push(&m, 10) == 10);
    CHECK(ma_push(&m, 20) == 15);
    CHECK(ma_push(&m, 30) == 20);
    CHECK(ma_push(&m, 40) == 25);
    CHECK(ma_push(&m, 50) == 35);
    CHECK(m.sum == 140 && m.count == WIN && m.idx == 1);

    /* 2. positive rounding: half rounds up, below half rounds down */
    ma_init(&m);
    ma_push(&m, 1);
    CHECK(ma_push(&m, 2) == 2);          /* 3/2 = 1.5  -> 2 */
    CHECK(ma_push(&m, 2) == 2);          /* 5/3 = 1.67 -> 2 */
    CHECK(ma_push(&m, 0) == 1);          /* 5/4 = 1.25 -> 1 */

    /* 3. negative rounding is symmetric (C truncation would give -7 and -1) */
    ma_init(&m);
    ma_push(&m, -10);
    CHECK(ma_push(&m, -5) == -8);        /* -15/2 = -7.5  -> -8 */
    ma_init(&m);
    ma_push(&m, -1); ma_push(&m, -2);
    CHECK(ma_push(&m, -2) == -2);        /* -5/3 = -1.67 -> -2 */

    /* 4. int16 extremes: the sum must not overflow */
    ma_init(&m);
    for (int i = 0; i < WIN; i++) ma_push(&m, INT16_MAX);
    CHECK(ma_avg(&m) == INT16_MAX);
    CHECK(ma_push(&m, INT16_MIN) == 16383);   /* (3*32767 - 32768)/4 = 16383.25 */
    for (int i = 0; i < WIN - 1; i++) ma_push(&m, INT16_MIN);
    CHECK(ma_avg(&m) == INT16_MIN);

    /* 5. overheat by average or by rise (window 20,30,40,50: avg 35, rise 30) */
    ma_init(&m);
    CHECK(!ma_overheat(&m, 0, 0));            /* empty window never alarms */
    for (int16_t v = 10; v <= 50; v += 10) ma_push(&m, v);
    CHECK(ma_overheat(&m, 35, 100));          /* avg 35 >= 35 */
    CHECK(!ma_overheat(&m, 36, 31));          /* avg 35 < 36, rise 30 < 31 */
    CHECK(ma_overheat(&m, 36, 30));           /* rise 30 >= 30 */

    /* 6. rise while filling uses the real oldest; a falling window has negative rise */
    ma_init(&m);
    ma_push(&m, 100); ma_push(&m, 130);
    CHECK(ma_overheat(&m, 999, 30) && !ma_overheat(&m, 999, 31));
    ma_init(&m);
    ma_push(&m, 500); ma_push(&m, 400); ma_push(&m, 300);
    CHECK(!ma_overheat(&m, 999, 0));          /* rise = -200 */
    ma_init(&m);
    ma_push(&m, INT16_MIN); ma_push(&m, INT16_MAX);
    CHECK(ma_overheat(&m, INT16_MAX, 30000)); /* rise 65535 needs int32 */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
