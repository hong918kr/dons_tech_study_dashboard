/*
 * T2-10  Top-K Hottest Sensors
 *
 * The thermal manager polls N temperature sensors (motor drivers, battery cells, SoC ...)
 * and must report the K hottest to the operator display.
 *
 * Rules (agreed with the interviewer):
 *   - "Hotter" order: higher temp_c10 first; on equal temperature the SMALLER id first.
 *   - Write min(n, k) readings into out[], sorted hottest first, and return that count.
 *   - out has room for k readings; out may be NULL when k == 0.
 *   - Use a size-K min-heap built inside out[] itself (no malloc, no extra array):
 *     O(n log k) time, O(1) extra memory. The input array must not be modified.
 *
 * Reference solution.  Build & run:  ./run.sh T2-10 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

typedef struct { uint16_t id; int16_t temp_c10; } Reading;   /* temp in 0.1 degC */

/* true if a ranks strictly hotter than b */
static bool hotter(Reading a, Reading b)
{
    if (a.temp_c10 != b.temp_c10) return a.temp_c10 > b.temp_c10;
    return a.id < b.id;
}

/* min-heap by "hotter": h[0] is the COLDEST of the kept readings */
static void sift_down(Reading *h, size_t n, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < n && hotter(h[m], h[l])) m = l;      /* pick the colder child */
        if (r < n && hotter(h[m], h[r])) m = r;
        if (m == i) return;
        Reading t = h[i]; h[i] = h[m]; h[m] = t;
        i = m;
    }
}

static void sift_up(Reading *h, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!hotter(h[p], h[i])) return;             /* parent already colder */
        Reading t = h[i]; h[i] = h[p]; h[p] = t;
        i = p;
    }
}

size_t top_k_hottest(const Reading *r, size_t n, size_t k, Reading *out)
{
    size_t sz = 0;
    for (size_t i = 0; i < n; i++) {
        if (sz < k) {                                /* heap not full: push */
            out[sz] = r[i];
            sift_up(out, sz++);
        } else if (k > 0 && hotter(r[i], out[0])) {  /* beats the coldest kept: replace root */
            out[0] = r[i];
            sift_down(out, sz, 0);
        }
    }
    /* in-place heap sort: move the coldest to the back each round -> hottest first */
    for (size_t end = sz; end > 1; end--) {
        Reading t = out[0]; out[0] = out[end - 1]; out[end - 1] = t;
        sift_down(out, end - 1, 0);
    }
    return sz;
}

int main(void)
{
    Reading out[8];
    size_t k;

    /* 1. basic: K = 3 of 8, tie at 745 goes to the smaller id */
    const Reading a[] = { {0,612}, {1,745}, {2,598}, {3,801}, {4,745}, {5,690}, {6,812}, {7,701} };
    k = top_k_hottest(a, 8, 3, out);
    CHECK(k == 3);
    CHECK(k == 3 && out[0].id == 6 && out[1].id == 3 && out[2].id == 1);
    CHECK(k == 3 && out[0].temp_c10 == 812 && out[2].temp_c10 == 745);

    /* 2. k == 0 and n == 0 */
    CHECK(top_k_hottest(a, 8, 0, NULL) == 0);
    CHECK(top_k_hottest(NULL, 0, 3, out) == 0);

    /* 3. k > n: everything, sorted */
    const Reading b[] = { {9,100}, {4,300}, {7,200} };
    k = top_k_hottest(b, 3, 8, out);
    CHECK(k == 3 && out[0].id == 4 && out[1].id == 7 && out[2].id == 9);

    /* 4. all equal temperatures: smallest ids win, ascending */
    const Reading c[] = { {5,500}, {2,500}, {8,500}, {1,500}, {3,500} };
    k = top_k_hottest(c, 5, 3, out);
    CHECK(k == 3 && out[0].id == 1 && out[1].id == 2 && out[2].id == 3);

    /* 5. negative temperatures (cold start) and k == 1 is the max */
    const Reading d[] = { {0,-250}, {1,-100}, {2,-300}, {3,-100} };
    k = top_k_hottest(d, 4, 2, out);
    CHECK(k == 2 && out[0].id == 1 && out[1].id == 3);
    k = top_k_hottest(d, 4, 1, out);
    CHECK(k == 1 && out[0].id == 1 && out[0].temp_c10 == -100);

    /* 6. input is not modified; ascending input (worst case for replacements) */
    const Reading e[] = { {0,10}, {1,20}, {2,30}, {3,40}, {4,50}, {5,60} };
    Reading copy[6];
    memcpy(copy, e, sizeof e);
    k = top_k_hottest(e, 6, 4, out);
    CHECK(k == 4 && out[0].id == 5 && out[1].id == 4 && out[2].id == 3 && out[3].id == 2);
    CHECK(memcmp(copy, e, sizeof e) == 0);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
