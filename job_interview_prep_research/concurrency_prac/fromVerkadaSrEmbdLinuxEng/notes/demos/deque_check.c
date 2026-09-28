/* deque_check.c — monotonic deque (time window min/max/avg) vs brute force.
 * cc -std=c11 -O2 -Wall -Wextra -o deque_check deque_check.c && ./deque_check
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_US 1000000ull          /* 1 s window */
#define NONE   (-32768)

/* ---------- the thing under test: ring + two monotonic deques ---------- */

#ifndef CAP
#define CAP 1024u
#endif
#define MASK (CAP - 1u)

typedef struct { uint64_t ts; int v; } sample_t;

static struct {
    sample_t s[CAP];
    uint32_t head, tail;
    uint32_t minq[CAP]; uint32_t min_f, min_b;
    uint32_t maxq[CAP]; uint32_t max_f, max_b;
    int64_t  sum;
    uint64_t pushes, pops;          /* amortised-O(1) evidence */
} W;

static uint32_t wcount(void) { return W.head - W.tail; }

static void drop_oldest(void)
{
    uint32_t idx = W.tail;
    W.sum -= W.s[idx & MASK].v;
    W.tail++;
    if (W.min_f != W.min_b && W.minq[W.min_f & MASK] == idx) { W.min_f++; W.pops++; }
    if (W.max_f != W.max_b && W.maxq[W.max_f & MASK] == idx) W.max_f++;
}

static void evict(uint64_t now)
{
    while (W.head != W.tail) {
        uint64_t ts = W.s[W.tail & MASK].ts;
        if (ts > now || now - ts <= WIN_US) break;
        drop_oldest();
    }
}

static void push(uint64_t ts, int v)
{
    if (wcount() > 0) {                       /* keep ts non-decreasing */
        uint64_t last = W.s[(W.head - 1u) & MASK].ts;
        if (ts < last) ts = last;
    }
    evict(ts);
    if (wcount() == CAP) drop_oldest();

    uint32_t idx = W.head;
    W.s[idx & MASK] = (sample_t){ .ts = ts, .v = v };
    W.head++;
    W.sum += v;
    W.pushes++;

    while (W.min_f != W.min_b &&
           W.s[W.minq[(W.min_b - 1u) & MASK] & MASK].v >= v) { W.min_b--; W.pops++; }
    W.minq[W.min_b & MASK] = idx; W.min_b++;

    while (W.max_f != W.max_b &&
           W.s[W.maxq[(W.max_b - 1u) & MASK] & MASK].v <= v) W.max_b--;
    W.maxq[W.max_b & MASK] = idx; W.max_b++;
}

static int qmin(uint64_t now)
{
    evict(now);
    return (W.min_f == W.min_b) ? NONE : W.s[W.minq[W.min_f & MASK] & MASK].v;
}
static int qmax(uint64_t now)
{
    evict(now);
    return (W.max_f == W.max_b) ? NONE : W.s[W.maxq[W.max_f & MASK] & MASK].v;
}
static double qavg(uint64_t now)
{
    evict(now);
    uint32_t n = wcount();
    return n ? (double)W.sum / (double)n : 0.0;
}

/* ---------- brute force reference: keep everything, rescan ---------- */

#define NMAX 200000
static sample_t all[NMAX];
static int nall;
static uint64_t bf_scanned;

static void bf_push(uint64_t ts, int v)
{
    if (nall > 0 && ts < all[nall - 1].ts) ts = all[nall - 1].ts;
    all[nall].ts = ts; all[nall].v = v; nall++;
}

/* min/max/avg over {ts : now-ts <= WIN}, then only the newest CAP of them
 * (the ring can hold no more, so the code under test drops the rest). */
static void bf_stats(uint64_t now, int *mn, int *mx, double *avg, uint32_t *cnt)
{
    int lo = nall;                            /* first index inside window */
    while (lo > 0 && !(all[lo - 1].ts > now) && now - all[lo - 1].ts <= WIN_US) {
        lo--; bf_scanned++;
    }
    if ((uint32_t)(nall - lo) > CAP) lo = nall - (int)CAP;

    *mn = NONE; *mx = NONE; *cnt = (uint32_t)(nall - lo);
    int64_t s = 0;
    for (int i = lo; i < nall; i++) {
        if (*mn == NONE || all[i].v < *mn) *mn = all[i].v;
        if (*mx == NONE || all[i].v > *mx) *mx = all[i].v;
        s += all[i].v;
    }
    *avg = *cnt ? (double)s / (double)*cnt : 0.0;
}

/* ---------- driver ---------- */

static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }

int main(void)
{
    long fail = 0, checks = 0;
    uint64_t t = 0;

    /* phase 1: fixed hand-checkable sequence, window = 1 s, 200 ms steps */
    const int seq[] = { -70, -85, -60, -95, -75, -75, -110, -65 };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        t += 200000;
        push(t, seq[i]); bf_push(t, seq[i]);
        int a = qmin(t), b = qmax(t); double c = qavg(t);
        int ea, eb; double ec; uint32_t en;
        bf_stats(t, &ea, &eb, &ec, &en);
        printf("t=%4llu ms  push %4d  ->  min=%4d max=%4d avg=%7.2f  n=%u%s\n",
               (unsigned long long)(t / 1000), seq[i], a, b, c, en,
               (a == ea && b == eb && c == ec) ? "" : "   MISMATCH");
        checks++;
        if (a != ea || b != eb || c != ec) fail++;
    }

    /* phase 2: randomised — 120k pushes, random gaps, queries in between,
     * plus quiet periods long enough that the window empties out. */
    for (int i = 0; i < 120000 && nall < NMAX - 4; i++) {
        uint64_t gap = (rnd() % 30000);                 /* 0..30 ms */
        if (rnd() % 4000 == 0) gap += 3 * WIN_US;       /* radio went quiet */
        t += gap;
        int v = -120 + (int)(rnd() % 70);
        push(t, v); bf_push(t, v);

        if (rnd() % 3 == 0) {
            t += (rnd() % 400000);                       /* clock only moves forward */
            uint64_t qt = t;                             /* query with no push */
            int a = qmin(qt), b = qmax(qt); double c = qavg(qt);
            int ea, eb; double ec; uint32_t en;
            bf_stats(qt, &ea, &eb, &ec, &en); (void)en;
            checks++;
            if (a != ea || b != eb || c != ec) {
                if (fail < 5)
                    printf("MISMATCH i=%d qt=%llu: got(%d,%d,%.3f) want(%d,%d,%.3f)\n",
                           i, (unsigned long long)qt, a, b, c, ea, eb, ec);
                fail++;
            }
        }
    }

    printf("\nrandom phase: %d samples, %ld checks, %ld mismatches\n",
           nall, checks, fail);
    printf("deque work: %llu pushes, %llu min-deque pops"
           "  (pops/push = %.3f  <= 1 means amortised O(1))\n",
           (unsigned long long)W.pushes, (unsigned long long)W.pops,
           (double)W.pops / (double)W.pushes);

    /* phase 3: cost at a steady 100 Hz, query after every sample. */
    memset(&W, 0, sizeof W);
    nall = 0; bf_scanned = 0; t = 0;
    uint64_t q = 0;
    for (int i = 0; i < 20000; i++) {
        t += 10000;                                      /* 100 Hz */
        int v = -120 + (int)(rnd() % 70);
        push(t, v); bf_push(t, v);
        int a = qmin(t), b = qmax(t); double c = qavg(t);
        int ea, eb; double ec; uint32_t en;
        bf_stats(t, &ea, &eb, &ec, &en);
        q++; checks++;
        if (a != ea || b != eb || c != ec) fail++;
        if (i == 19999)
            printf("\n100 Hz phase: window holds %u samples\n", en);
    }
    printf("  %llu queries: deque did %llu pushes + %llu pops total,"
           " brute force scanned %llu samples (%.0fx)\n",
           (unsigned long long)q, (unsigned long long)W.pushes,
           (unsigned long long)W.pops, (unsigned long long)bf_scanned,
           (double)bf_scanned / (double)(W.pushes + W.pops));

    printf("RESULT: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
