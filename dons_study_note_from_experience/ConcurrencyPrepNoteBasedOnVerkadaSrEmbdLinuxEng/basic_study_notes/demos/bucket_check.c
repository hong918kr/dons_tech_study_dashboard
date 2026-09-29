/* bucket_check.c — time buckets vs brute force.
 *   variant A: ring + lazy advance      (03_event_tailer_shutdown style)
 *   variant B: absolute bucket number   (04_temp_single_flight style)
 * cc -std=c11 -O2 -Wall -Wextra -o bucket_check bucket_check.c && ./bucket_check
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NB      8u                  /* buckets */
#define BW      100000ull           /* 100 ms per bucket -> 800 ms window */
#define NTYPES  3

/* ---------------- variant A: lazy advance ---------------- */

static struct {
    uint32_t n[NB][NTYPES];
    uint64_t cur;
    bool     primed;
    uint64_t zeroed_rows, full_wipes;
} A;

static void a_add(uint64_t ts, int type)
{
    uint64_t b = ts / BW;
    if (!A.primed) { A.cur = b; A.primed = true; }
    else if (b > A.cur) {
        if (b - A.cur >= NB) { memset(A.n, 0, sizeof A.n); A.full_wipes++; }
        else for (uint64_t k = A.cur + 1; k <= b; k++) {
            memset(A.n[k % NB], 0, sizeof A.n[0]); A.zeroed_rows++;
        }
        A.cur = b;
    }
    if (b + NB > A.cur)                       /* not older than the window */
        A.n[b % NB][type]++;
}

static uint32_t a_count(uint64_t now, uint64_t since, int type)  /* type<0 = any */
{
    uint64_t nowb = now / BW, lo = since / BW;
    uint64_t wlo = (nowb + 1 >= NB) ? nowb - (NB - 1) : 0;
    if (lo < wlo) lo = wlo;
    uint32_t tot = 0;
    if (!A.primed) return 0;
    uint64_t hi = A.cur < nowb ? A.cur : nowb;
    uint64_t rlo = (A.cur + 1 >= NB) ? A.cur - (NB - 1) : 0;
    if (lo < rlo) lo = rlo;
    for (uint64_t b = lo; b <= hi; b++) {
        const uint32_t *row = A.n[b % NB];
        if (type < 0) for (int t = 0; t < NTYPES; t++) tot += row[t];
        else tot += row[type];
    }
    return tot;
}

/* ---------------- variant B: absolute bucket number ---------------- */

static struct {
    struct { uint64_t idx; uint32_t n[NTYPES]; } b[NB];
    uint64_t newest;
    bool any;
} B;

static void b_init(void) { for (unsigned i = 0; i < NB; i++) B.b[i].idx = UINT64_MAX; }

static void b_add(uint64_t ts, int type)
{
    uint64_t m = ts / BW;
    if (B.any && m + NB <= B.newest) return;  /* older than the window */
    unsigned slot = (unsigned)(m % NB);
    if (B.b[slot].idx != m) {                 /* stale slot: recycle, no pass */
        B.b[slot].idx = m;
        memset(B.b[slot].n, 0, sizeof B.b[slot].n);
    }
    B.b[slot].n[type]++;
    if (!B.any || m > B.newest) B.newest = m;
    B.any = true;
}

static uint32_t b_count(uint64_t now, uint64_t since, int type)
{
    if (!B.any) return 0;
    uint64_t nowb = now / BW, lo = since / BW;
    uint64_t wlo = (nowb + 1 >= NB) ? nowb - (NB - 1) : 0;
    if (lo < wlo) lo = wlo;
    uint64_t hi = B.newest < nowb ? B.newest : nowb;
    uint32_t tot = 0;
    for (uint64_t m = lo; m <= hi; m++) {
        unsigned s = (unsigned)(m % NB);
        if (B.b[s].idx != m) continue;        /* stale -> skip, that IS eviction */
        if (type < 0) for (int t = 0; t < NTYPES; t++) tot += B.b[s].n[t];
        else tot += B.b[s].n[type];
    }
    return tot;
}

/* ---------------- brute force ---------------- */

#define NMAX 400000
static struct { uint64_t ts; int type; } ev[NMAX];
static int nev;
static uint64_t bf_bytes_touched;

static void bf_add(uint64_t ts, int type) { ev[nev].ts = ts; ev[nev].type = type; nev++; }

/* bucket-rounded truth: every event whose BUCKET is in [lo,hi] */
static uint32_t bf_bucket(uint64_t now, uint64_t since, int type)
{
    uint64_t nowb = now / BW, lo = since / BW;
    uint64_t wlo = (nowb + 1 >= NB) ? nowb - (NB - 1) : 0;
    if (lo < wlo) lo = wlo;
    uint32_t tot = 0;
    for (int i = 0; i < nev; i++) {
        uint64_t b = ev[i].ts / BW;
        bf_bytes_touched++;
        if (b >= lo && b <= nowb && (type < 0 || ev[i].type == type)) tot++;
    }
    return tot;
}

/* exact truth: ts >= since, and inside the window */
static uint32_t bf_exact(uint64_t now, uint64_t since, int type)
{
    uint64_t nowb = now / BW;
    uint64_t wstart = ((nowb + 1 >= NB) ? nowb - (NB - 1) : 0) * BW;
    uint64_t s = since < wstart ? wstart : since;
    uint32_t tot = 0;
    for (int i = 0; i < nev; i++)
        if (ev[i].ts >= s && ev[i].ts <= now && (type < 0 || ev[i].type == type)) tot++;
    return tot;
}

static uint32_t rs = 777u;
static uint32_t rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }

int main(void)
{
    long failA = 0, failB = 0, checks = 0;
    long round_hi = 0, round_lo = 0, round_eq = 0, worst = 0;
    uint64_t t = 0;
    b_init();

    /* phase 1: a hand-checkable trace, 100 ms buckets */
    printf("bucket width 100 ms, %u buckets (800 ms window)\n\n", NB);
    const struct { uint64_t ms; int ty; } seq[] = {
        {  10, 0 }, {  20, 0 }, {  90, 1 }, { 150, 0 },
        { 260, 2 }, { 270, 0 }, { 830, 1 }, { 840, 1 },
    };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        t = seq[i].ms * 1000;
        a_add(t, seq[i].ty); b_add(t, seq[i].ty); bf_add(t, seq[i].ty);
        uint32_t a = a_count(t, 0, -1), b = b_count(t, 0, -1), e = bf_exact(t, 0, -1);
        printf("t=%4llu ms type=%d -> bucket %llu | count_since(0,ANY): A=%u B=%u"
               "  exact=%u  %s\n",
               (unsigned long long)seq[i].ms, seq[i].ty,
               (unsigned long long)(t / BW), a, b, e,
               (a == b) ? "" : "A!=B");
        if (a != b) failB++;
    }

    /* phase 2: randomised, with bursts and quiet gaps */
    for (int i = 0; i < 200000 && nev < NMAX - 4; i++) {
        uint64_t gap = rnd() % 40000;                  /* 0..40 ms */
        if (rnd() % 500 == 0) gap += 20 * NB * BW;     /* quiet night */
        t += gap;
        int ty = (int)(rnd() % NTYPES);
        a_add(t, ty); b_add(t, ty); bf_add(t, ty);

        if (rnd() % 200 == 0) {
            int ty2 = (rnd() % 4 == 0) ? -1 : (int)(rnd() % NTYPES);
            uint64_t since = (t > 5 * NB * BW) ? t - (rnd() % (5 * NB * BW)) : 0;
            uint32_t ga = a_count(t, since, ty2), gb = b_count(t, since, ty2);
            uint32_t want = bf_bucket(t, since, ty2);
            uint32_t exact = bf_exact(t, since, ty2);
            checks++;
            if (ga != want) { if (failA < 4) printf("A MISMATCH i=%d: %u vs %u\n", i, ga, want); failA++; }
            if (gb != want) { if (failB < 4) printf("B MISMATCH i=%d: %u vs %u\n", i, gb, want); failB++; }
            if (ga > exact) { round_hi++; if ((long)(ga - exact) > worst) worst = ga - exact; }
            else if (ga < exact) round_lo++;
            else round_eq++;
        }
    }

    printf("\n%d events, %ld queries\n", nev, checks);
    printf("variant A (lazy advance)  mismatches vs brute force: %ld\n", failA);
    printf("variant B (absolute idx)  mismatches vs brute force: %ld\n", failB);
    printf("lazy advance work: %llu rows zeroed, %llu full wipes"
           "  (%.3f rows per event)\n",
           (unsigned long long)A.zeroed_rows, (unsigned long long)A.full_wipes,
           (double)A.zeroed_rows / (double)nev);
    printf("bucket rounding vs exact: equal %ld, over %ld (worst +%ld), under %ld\n",
           round_eq, round_hi, worst, round_lo);
    printf("memory: A = %zu B, B = %zu B, brute force list = %zu B for %d events\n",
           sizeof A.n, sizeof B.b, nev * sizeof ev[0], nev);
    printf("RESULT: %s\n", (failA || failB) ? "FAIL" : "PASS");
    (void)bf_bytes_touched;
    return (failA || failB) ? 1 : 0;
}
