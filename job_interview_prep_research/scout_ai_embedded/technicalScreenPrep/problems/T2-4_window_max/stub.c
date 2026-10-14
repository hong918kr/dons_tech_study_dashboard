/*
 * T2-4  Peak Temperature over the Last W Samples   (LeetCode 239, streaming)
 *
 * The motor-driver thermal monitor gets one temperature sample per tick and must report
 * the PEAK of the last W samples after every sample (the fan controller reacts to the
 * peak, not the average). Brute force rescans W samples per tick; do better.
 *
 * Definitions (agreed with the interviewer):
 *   - wm_init(m, w) with 1 <= w <= W_MAX.
 *   - wm_push(m, temp) adds one sample and returns the max of the last w samples
 *     (or of all samples so far, if fewer than w have arrived).
 *   - Temperatures are int16_t in 0.1 degC (may be negative).
 *   - Fixed memory: the deque lives in a W_MAX-entry array inside the struct. No malloc.
 *   - Amortized O(1) per push.
 *
 * Build & run:  ./run.sh T2-4
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define W_MAX  16u                        /* power of two: deque index uses a mask */
#define DQ_MASK (W_MAX - 1u)

typedef struct { uint32_t seq; int16_t temp; } Entry;

typedef struct {
    uint32_t w;                           /* window length in samples         */
    uint32_t seq;                         /* sequence number of the NEXT sample */
    uint32_t front, back;                 /* free-running deque indices; size = back - front */
    Entry dq[W_MAX];                      /* temps strictly decreasing front -> back */
} WinMax;

void wm_init(WinMax *m, uint32_t w)
{
    /* TODO: your code here */
    (void)m; (void)w;
}

int16_t wm_push(WinMax *m, int16_t temp)
{
    /* TODO: your code here */
    (void)m; (void)temp;
    return 0;
}

/* ---- test helper: brute force over the same window ---- */
static int16_t brute_max(const int16_t *x, size_t i, uint32_t w)
{
    size_t lo = (i + 1 >= w) ? i + 1 - w : 0;
    int16_t m = x[lo];
    for (size_t j = lo + 1; j <= i; j++) if (x[j] > m) m = x[j];
    return m;
}

int main(void)
{
    WinMax m = {0};

    /* 1. basic: w = 3 */
    const int16_t a[] = { 810, 790, 830, 820, 800, 780, 850 };
    const int16_t want[] = { 810, 810, 830, 830, 830, 820, 850 };
    wm_init(&m, 3);
    int ok = 1;
    for (int i = 0; i < 7; i++) ok &= wm_push(&m, a[i]) == want[i];
    CHECK(ok);

    /* 2. w = 1: the max is just the current sample */
    wm_init(&m, 1);
    CHECK(wm_push(&m, 500) == 500 && wm_push(&m, 400) == 400 && wm_push(&m, 450) == 450);

    /* 3. strictly decreasing input with w = W_MAX: the deque fills to exactly w entries */
    wm_init(&m, W_MAX);
    ok = 1;
    for (int i = 0; i < 40; i++)
        ok &= wm_push(&m, (int16_t)(1000 - 10 * i)) == 1000 - 10 * (i >= (int)W_MAX ? i - (int)W_MAX + 1 : 0);
    CHECK(ok);

    /* 4. negative temperatures and ties */
    wm_init(&m, 2);
    CHECK(wm_push(&m, -200) == -200);
    CHECK(wm_push(&m, -200) == -200);
    CHECK(wm_push(&m, -350) == -200);
    CHECK(wm_push(&m, -400) == -350);

    /* 5. w = W_MAX, long pseudo-random stream vs brute force */
    static int16_t x[2000];
    uint32_t r = 12345;
    for (int i = 0; i < 2000; i++) { r = r * 1103515245u + 12345u; x[i] = (int16_t)((r >> 16) % 600) - 100; }
    for (uint32_t w = 1; w <= W_MAX; w += 5) {
        wm_init(&m, w);
        ok = 1;
        for (size_t i = 0; i < 2000; i++) ok &= wm_push(&m, x[i]) == brute_max(x, i, w);
        CHECK(ok);
    }

    /* 6. sequence counter wraps past UINT32_MAX */
    wm_init(&m, 3);
    m.seq = UINT32_MAX - 1u;
    ok = 1;
    for (int i = 0; i < 7; i++) ok &= wm_push(&m, a[i]) == want[i];
    CHECK(ok);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
