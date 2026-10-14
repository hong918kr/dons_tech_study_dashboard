/*
 * T1-4  Multi-Sensor Overheat
 *
 * Readings from up to MAX_SENSORS sensors arrive interleaved in one array (input order = arrival
 * order). Run an independent T1-2 style detector per sensor and report every overheat START as
 * an Alert {id, t_ms}, in input order.
 *
 * Definitions (agreed with the interviewer):
 *   - Per sensor: NORMAL -> OVERHEATED after N_CONFIRM consecutive readings >= HIGH_C10 (alert on
 *     that reading); OVERHEATED -> NORMAL after N_CONFIRM consecutive readings <= LOW_C10 (no alert).
 *     A reading that breaks the streak resets that sensor's counter.
 *   - id >= MAX_SENSORS: ignored and counted in *dropped.
 *   - A reading whose t_ms is not AFTER that sensor's previous accepted reading (older or equal,
 *     compared wrap-safe) is ignored and counted in *dropped. It does not touch the streak.
 *   - Write at most out_cap alerts into out, but ALWAYS return the total number of alerts.
 *     out may be NULL when out_cap == 0. dropped may be NULL.
 *   - No malloc. O(n) time, O(MAX_SENSORS) state.
 *
 * Build & run:  ./run.sh T1-4
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define MAX_SENSORS 16
#define HIGH_C10    850
#define LOW_C10     800
#define N_CONFIRM   3

typedef struct { uint8_t id; uint32_t t_ms; int16_t temp_c10; } Reading;
typedef struct { uint8_t id; uint32_t t_ms; } Alert;

size_t process(const Reading *r, size_t n, Alert *out, size_t out_cap, size_t *dropped)
{
    /* TODO: your code here (design a per-sensor state struct) */
    (void)r; (void)n; (void)out; (void)out_cap; (void)dropped;
    return 0;
}

int main(void)
{
    Alert out[8];
    size_t dropped;

    /* 1. basic: three sensors interleaved, one bad id, one stale reading */
    const Reading a[] = {
        {0,   0, 860}, {1,   0, 700}, {2,   0, 900},
        {0, 100, 870}, {1, 100, 900}, {2, 100, 700},
        {0, 200, 880}, {1, 200, 910}, {20, 200, 999}, {2, 200, 900},
        {1, 300, 920}, {0, 300, 890}, {2, 300, 900},
        {1, 250, 950},                                  /* older than sensor 1's t=300 */
        {2, 400, 900},
    };
    dropped = 99;
    size_t k = process(a, sizeof a / sizeof a[0], out, 8, &dropped);
    CHECK(k == 3 && dropped == 2);
    CHECK(k >= 3 && out[0].id == 0 && out[0].t_ms == 200);
    CHECK(k >= 3 && out[1].id == 1 && out[1].t_ms == 300);
    CHECK(k >= 3 && out[2].id == 2 && out[2].t_ms == 400);

    /* 2. empty input */
    dropped = 99;
    CHECK(process(NULL, 0, out, 8, &dropped) == 0 && dropped == 0);

    /* 3. stale (older or equal) timestamps are dropped and do not count toward the streak */
    const Reading b[] = { {3,100,900}, {3,200,900}, {3,150,900}, {3,200,900}, {3,300,900} };
    dropped = 99;
    k = process(b, 5, out, 8, &dropped);
    CHECK(k == 1 && dropped == 2 && out[0].id == 3 && out[0].t_ms == 300);

    /* 4. recover then overheat again -> a second alert for the same sensor */
    const Reading c[] = { {5,0,900}, {5,10,900}, {5,20,900}, {5,30,700}, {5,40,700},
                          {5,50,700}, {5,60,900}, {5,70,900}, {5,80,900} };
    k = process(c, 9, out, 8, NULL);
    CHECK(k == 2 && out[0].t_ms == 20 && out[1].t_ms == 80);

    /* 5. out_cap smaller than the result: exact count, nothing written past cap */
    Alert one[2] = { {0, 0}, {0xEE, 0xDEAD} };
    k = process(a, sizeof a / sizeof a[0], one, 1, NULL);
    CHECK(k == 3 && one[0].id == 0 && one[0].t_ms == 200 && one[1].id == 0xEE);
    CHECK(process(a, sizeof a / sizeof a[0], NULL, 0, NULL) == 3);

    /* 6. timestamps wrap around 2^32 and are still "newer" */
    const Reading d[] = { {7,0xFFFFFF00u,900}, {7,0xFFFFFFF0u,900}, {7,0x10u,900} };
    dropped = 99;
    k = process(d, 3, out, 8, &dropped);
    CHECK(k == 1 && dropped == 0 && out[0].t_ms == 0x10u);

    /* 7. id boundary: 15 is valid, 16 is not */
    const Reading e[] = { {15,0,900}, {16,0,900}, {15,1,900}, {16,1,900}, {15,2,900}, {16,2,900} };
    dropped = 99;
    k = process(e, 6, out, 8, &dropped);
    CHECK(k == 1 && dropped == 3 && out[0].id == 15 && out[0].t_ms == 2);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
