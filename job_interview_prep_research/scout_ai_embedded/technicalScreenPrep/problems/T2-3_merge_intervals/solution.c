/*
 * T2-3  Merge Overheat Intervals from Several Sensors   (LeetCode 56 style)
 *
 * Three temperature sensors on a robot arm each reported their own overheat intervals.
 * The fault log wants ONE list: "when was ANY part of the arm overheating?"
 * Merge all intervals in place and return the new count.
 *
 * Definitions (agreed with the interviewer):
 *   - Interval {start_ms, end_ms} is CLOSED: both ends are included, start_ms <= end_ms.
 *   - Input is in no particular order (sensor A's list, then B's, then C's).
 *   - Two intervals merge when they overlap OR touch: next.start_ms <= cur.end_ms.
 *     ([100,200] and [200,300] merge into [100,300]; [100,200] and [201,300] do not.)
 *   - Result is sorted by start_ms, written to iv[0 .. return-1]. iv may be NULL if n == 0.
 *   - No malloc. qsort is fine, but the comparator must not overflow.
 *
 * Reference solution.  Build & run:  ./run.sh T2-3 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct { uint32_t start_ms, end_ms; } Interval;

static int cmp_start(const void *pa, const void *pb)
{
    const Interval *a = pa, *b = pb;
    if (a->start_ms != b->start_ms) return (a->start_ms > b->start_ms) - (a->start_ms < b->start_ms);
    return (a->end_ms > b->end_ms) - (a->end_ms < b->end_ms);   /* never "a - b" on uint32_t */
}

size_t merge_intervals(Interval *iv, size_t n)
{
    if (n == 0) return 0;
    qsort(iv, n, sizeof iv[0], cmp_start);

    size_t w = 0;                                   /* iv[w] = interval being grown */
    for (size_t i = 1; i < n; i++) {
        if (iv[i].start_ms <= iv[w].end_ms) {       /* overlap or touch: extend */
            if (iv[i].end_ms > iv[w].end_ms) iv[w].end_ms = iv[i].end_ms;
        } else {
            iv[++w] = iv[i];                        /* gap: start a new one */
        }
    }
    return w + 1;
}

int main(void)
{
    /* 1. basic: three sensors, unsorted */
    Interval a[] = { {1000,1500}, {4000,4200},      /* sensor A */
                     {1400,2000}, {5000,5000},      /* sensor B */
                     {2000,2600}, {4100,4150} };    /* sensor C */
    size_t k = merge_intervals(a, 6);
    CHECK(k == 3);
    CHECK(k == 3 && a[0].start_ms == 1000 && a[0].end_ms == 2600);
    CHECK(k == 3 && a[1].start_ms == 4000 && a[1].end_ms == 4200);
    CHECK(k == 3 && a[2].start_ms == 5000 && a[2].end_ms == 5000);

    /* 2. empty and single */
    CHECK(merge_intervals(NULL, 0) == 0);
    Interval b[] = { {7,9} };
    CHECK(merge_intervals(b, 1) == 1 && b[0].start_ms == 7 && b[0].end_ms == 9);

    /* 3. touching merges, a 1 ms gap does not */
    Interval c[] = { {200,300}, {100,200}, {301,400} };
    k = merge_intervals(c, 3);
    CHECK(k == 2 && c[0].start_ms == 100 && c[0].end_ms == 300 && c[1].start_ms == 301);

    /* 4. nested: a long interval swallows shorter ones that start later */
    Interval d[] = { {100,1000}, {200,300}, {400,500}, {900,1200} };
    k = merge_intervals(d, 4);
    CHECK(k == 1 && d[0].start_ms == 100 && d[0].end_ms == 1200);

    /* 5. huge timestamps: a subtracting comparator would sort these wrongly */
    Interval e[] = { {4000000000u, 4000000100u}, {10, 20}, {3000000000u, 3000000005u} };
    k = merge_intervals(e, 3);
    CHECK(k == 3 && e[0].start_ms == 10 && e[1].start_ms == 3000000000u && e[2].start_ms == 4000000000u);

    /* 6. all identical */
    Interval f[] = { {50,60}, {50,60}, {50,60} };
    CHECK(merge_intervals(f, 3) == 1 && f[0].start_ms == 50 && f[0].end_ms == 60);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
