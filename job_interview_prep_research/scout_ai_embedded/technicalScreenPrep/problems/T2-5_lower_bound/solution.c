/*
 * T2-5  Find a Sample by Time   (LeetCode 35 / 34: lower_bound / upper_bound)
 *
 * The flight recorder keeps a time-sorted array of temperature samples. An engineer
 * asks "what was the temperature at t = 300 ms?" Implement the two lookups below
 * with binary search.
 *
 * Definitions (agreed with the interviewer):
 *   - Timestamps are NON-DECREASING (duplicates allowed: two samples may share a t_ms).
 *   - first_at_or_after(s, n, t): index of the FIRST sample with t_ms >= t,
 *     or n if there is none.                                  (lower_bound)
 *   - last_at_or_before(s, n, t): index of the LAST sample with t_ms <= t,
 *     or NOT_FOUND if there is none.                          (upper_bound - 1)
 *   - O(log n). s may be NULL when n == 0. No overflow in the midpoint.
 *
 * Reference solution.  Build & run:  ./run.sh T2-5 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define NOT_FOUND SIZE_MAX

typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;   /* temp in 0.1 degC */

size_t first_at_or_after(const Sample *s, size_t n, uint32_t t)
{
    size_t lo = 0, hi = n;                     /* answer is in [lo, hi] ; search range [lo, hi) */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;       /* never (lo + hi) / 2 */
        if (s[mid].t_ms < t) lo = mid + 1;     /* mid is too early: answer is right of it */
        else                 hi = mid;         /* mid qualifies: answer is mid or left of it */
    }
    return lo;                                 /* lo == hi */
}

size_t last_at_or_before(const Sample *s, size_t n, uint32_t t)
{
    size_t lo = 0, hi = n;                     /* find first index with t_ms > t (upper_bound) */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s[mid].t_ms <= t) lo = mid + 1;
        else                  hi = mid;
    }
    return lo == 0 ? NOT_FOUND : lo - 1;
}

int main(void)
{
    const Sample s[] = { {100,780}, {200,790}, {200,795}, {200,801}, {350,812}, {500,830}, {800,845} };
    const size_t n = 7;

    /* 1. basic: t between samples */
    CHECK(first_at_or_after(s, n, 300) == 4);
    CHECK(last_at_or_before(s, n, 300) == 3);

    /* 2. duplicates: first of the run vs last of the run (LeetCode 34) */
    CHECK(first_at_or_after(s, n, 200) == 1);
    CHECK(last_at_or_before(s, n, 200) == 3);

    /* 3. before the first sample / after the last sample */
    CHECK(first_at_or_after(s, n, 50) == 0);
    CHECK(last_at_or_before(s, n, 50) == NOT_FOUND);
    CHECK(first_at_or_after(s, n, 900) == n);
    CHECK(last_at_or_before(s, n, 900) == 6);

    /* 4. exact first and last timestamps */
    CHECK(first_at_or_after(s, n, 100) == 0 && last_at_or_before(s, n, 100) == 0);
    CHECK(first_at_or_after(s, n, 800) == 6 && last_at_or_before(s, n, 800) == 6);

    /* 5. empty array and single element */
    CHECK(first_at_or_after(NULL, 0, 123) == 0);
    CHECK(last_at_or_before(NULL, 0, 123) == NOT_FOUND);
    const Sample one[] = { {42, 700} };
    CHECK(first_at_or_after(one, 1, 42) == 0 && first_at_or_after(one, 1, 43) == 1);
    CHECK(last_at_or_before(one, 1, 41) == NOT_FOUND && last_at_or_before(one, 1, 42) == 0);

    /* 6. extreme t values and an exhaustive check against a linear scan */
    const Sample big[] = { {0,0}, {7,0}, {UINT32_MAX,0} };
    CHECK(first_at_or_after(big, 3, UINT32_MAX) == 2 && last_at_or_before(big, 3, UINT32_MAX) == 2);
    CHECK(first_at_or_after(big, 3, 0) == 0 && last_at_or_before(big, 3, 0) == 0);
    int ok = 1;
    for (uint32_t t = 0; t <= 900; t++) {
        size_t lin = 0;
        while (lin < n && s[lin].t_ms < t) lin++;
        size_t last = NOT_FOUND;
        for (size_t i = 0; i < n; i++) if (s[i].t_ms <= t) last = i;
        ok &= first_at_or_after(s, n, t) == lin && last_at_or_before(s, n, t) == last;
    }
    CHECK(ok);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
