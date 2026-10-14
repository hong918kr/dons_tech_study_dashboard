/*
 * T1-1  Overheat Intervals
 *
 * You are given temperature samples sorted by time. Return every interval in which
 * the temperature stayed at or above `thresh` for at least `min_ms`.
 *
 * Definitions (agreed with the interviewer):
 *   - A sample is HOT when temp_c10 >= thresh.
 *   - An interval starts at the first HOT sample of a run and ends at the LAST HOT
 *     sample of that run (not at the first cool sample).
 *   - Duration = end_ms - start_ms. Keep the interval only if duration >= min_ms.
 *   - Write at most out_cap intervals into out, but ALWAYS return the total number found
 *     (so the caller can detect truncation). out may be NULL when out_cap == 0.
 *   - No malloc. O(n) time, O(1) extra memory.
 *
 * Reference solution.  Build & run:  ./run.sh T1-1 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;   /* temp in 0.1 degC */
typedef struct { uint32_t start_ms, end_ms; } Interval;

size_t find_overheat(const Sample *s, size_t n, int16_t thresh, uint32_t min_ms,
                     Interval *out, size_t out_cap)
{
    size_t cnt = 0;
    bool in_hot = false;              /* is an interval currently open? */
    uint32_t start = 0, last_hot = 0;

    for (size_t i = 0; i < n; i++) {
        bool hot = s[i].temp_c10 >= thresh;
        if (hot) {
            if (!in_hot) { in_hot = true; start = s[i].t_ms; }   /* open */
            last_hot = s[i].t_ms;                               /* extend */
        } else if (in_hot) {                                    /* close */
            in_hot = false;
            if (last_hot - start >= min_ms) {
                if (cnt < out_cap) out[cnt] = (Interval){ start, last_hot };
                cnt++;
            }
        }
    }
    if (in_hot && last_hot - start >= min_ms) {   /* still hot at the end */
        if (cnt < out_cap) out[cnt] = (Interval){ start, last_hot };
        cnt++;
    }
    return cnt;
}

int main(void)
{
    Interval out[8];

    /* 1. basic: one long run kept, one zero-length run dropped */
    const Sample a[] = { {0,750}, {500,810}, {1000,820}, {1500,830}, {2000,790}, {2500,850}, {3000,700} };
    size_t k = find_overheat(a, 7, 800, 1000, out, 8);
    CHECK(k == 1);
    CHECK(k >= 1 && out[0].start_ms == 500 && out[0].end_ms == 1500);

    /* 2. empty input */
    CHECK(find_overheat(NULL, 0, 800, 0, out, 8) == 0);

    /* 3. hot until the end -> the open interval must be closed */
    const Sample b[] = { {0,900}, {400,900}, {800,900}, {1200,900} };
    k = find_overheat(b, 4, 800, 1000, out, 8);
    CHECK(k == 1 && out[0].start_ms == 0 && out[0].end_ms == 1200);

    /* 4. exactly at the threshold counts as hot */
    const Sample c[] = { {0,800}, {1000,800}, {2000,799} };
    k = find_overheat(c, 3, 800, 1000, out, 8);
    CHECK(k == 1 && out[0].start_ms == 0 && out[0].end_ms == 1000);

    /* 5. min_ms == 0 keeps a single hot sample */
    const Sample d[] = { {0,700}, {10,900}, {20,700}, {30,900} };
    k = find_overheat(d, 4, 800, 0, out, 8);
    CHECK(k == 2 && out[0].start_ms == 10 && out[1].start_ms == 30 && out[1].end_ms == 30);

    /* 6. out_cap smaller than the result: count is still exact, nothing written past cap */
    Interval one[1] = { {0xDEAD, 0xBEEF} };
    k = find_overheat(d, 4, 800, 0, one, 1);
    CHECK(k == 2 && one[0].start_ms == 10);
    CHECK(find_overheat(d, 4, 800, 0, NULL, 0) == 2);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
