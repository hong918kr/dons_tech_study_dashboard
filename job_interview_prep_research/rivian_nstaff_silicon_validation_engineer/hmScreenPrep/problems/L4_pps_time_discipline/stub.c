/*
 * L4  GPS PPS Time Discipline + LiDAR/Camera Matching (sensor box, validation harness)
 *
 * A sensor box has one free-running 32-bit counter (nominal 100 MHz, wraps every ~42.9 s).
 * On every GPS PPS rising edge the FPGA latches the counter into a FIFO; every camera frame
 * and every LiDAR packet is stamped with the same counter. Turn raw counter values into
 * GPS time and pair camera frames with LiDAR sweeps.
 *
 * Implement:
 *   pps_analyze        Unwrap the latches (consecutive latches are < 2^32 ticks apart).
 *                      Measure each latch from the LAST GOOD edge (d ticks), with
 *                      w = nominal_hz * tol_ppm / 1e6 (allowed error per second):
 *                        d < nominal - w                         -> GLITCH (drop the edge)
 *                        k = round(d / nominal), |d - k*nominal| <= k*w:
 *                          k == 1 -> OK,  k >= 2 -> MISSED (k - 1 pulses missing)
 *                        otherwise                               -> BAD (drop the edge)
 *                      latch[0] is trusted as the reference edge (GPS second offset 0).
 *                      Accepted edges (REF/OK/MISSED) go into r->model with their unwrapped
 *                      tick and GPS second offset. freq_hz = round(span ticks / span seconds)
 *                      over first..last accepted edge; err_ppb = (ticks - secs*nominal) * 1e9
 *                      / (secs*nominal), truncated toward zero. Fewer than 2 accepted edges
 *                      -> -ENODATA. Bad args -> -EINVAL.
 *   counter_to_gps_ns  ns since the reference edge for a raw stamp ts: unwrap ts against the
 *                      last accepted edge (|ts - last| < 2^31 ticks), pick the latest accepted
 *                      edge at or before it, add elapsed * 1e9 / freq_hz (rounded).
 *                      *holdover = elapsed > PPS_HOLDOVER_S seconds. ts before the reference
 *                      edge, or a bad model -> PPS_TS_INVALID.
 *   match_nearest      cam_ns / lidar_ns sorted ascending. Cameras in time order, each takes
 *                      the nearest still-unused LiDAR sweep if |dt| <= tol_ns (tie -> earlier
 *                      sweep). Two pointers, O(n_cam + n_lidar). Writes up to cap pairs,
 *                      returns the TOTAL number of pairs; MatchStats counts the unmatched.
 *
 * Integer math only, no malloc.
 * Build & run:  ./run.sh L4
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define PPS_MAX          64           /* latches per analysis window */
#define PPS_TOL_MAX_PPM  1000u
#define PPS_NOMINAL_MAX  1000000000u  /* 1 GHz */
#define PPS_HOLDOVER_S   2            /* > 2 s since the last PPS used -> holdover */
#define PPS_TS_INVALID   INT64_MIN
#define NS_PER_S         1000000000LL

enum { PPS_REF = 0, PPS_OK, PPS_MISSED, PPS_GLITCH, PPS_BAD };

typedef struct {
    int64_t  tick[PPS_MAX];   /* unwrapped counter at each accepted edge (tick[0] = latch[0]) */
    int64_t  sec[PPS_MAX];    /* GPS seconds since the reference edge: 0, 1, 2, 4, ... */
    uint32_t n;               /* accepted edges */
    uint32_t last_raw;        /* raw latch of the last accepted edge */
    uint32_t freq_hz;         /* measured oscillator frequency */
} PpsModel;

typedef struct {
    uint32_t ok, missed, glitch, bad;   /* missed = number of missing pulses (sum of k - 1) */
    uint8_t  cls[PPS_MAX];              /* class of each latch (PPS_REF for latch[0]) */
    uint32_t freq_hz;
    int32_t  err_ppb;                   /* + = oscillator runs fast */
    PpsModel model;
} PpsReport;

typedef struct { uint32_t cam, lidar; int64_t dt_ns; } Pair;   /* dt = lidar - cam */
typedef struct { uint32_t pairs, cam_unmatched, lidar_unmatched; int64_t max_abs_dt_ns; } MatchStats;

int     pps_analyze(const uint32_t *latch, size_t n, uint32_t nominal_hz, uint32_t tol_ppm,
                    PpsReport *r);
int64_t counter_to_gps_ns(const PpsModel *m, uint32_t ts, bool *holdover);
int     match_nearest(const int64_t *cam_ns, size_t n_cam, const int64_t *lidar_ns, size_t n_lidar,
                      int64_t tol_ns, Pair *pairs, size_t cap, MatchStats *ms);

/* ---------------- your code ---------------- */
int pps_analyze(const uint32_t *latch, size_t n, uint32_t nominal_hz, uint32_t tol_ppm,
                PpsReport *r)
{
    /* TODO: validate args, unwrap latches, classify each edge against the last good edge,
     *       fill r->model with accepted edges, then freq_hz and err_ppb over the good span */
    (void)latch; (void)n; (void)nominal_hz; (void)tol_ppm; (void)r;
    return 0;
}

int64_t counter_to_gps_ns(const PpsModel *m, uint32_t ts, bool *holdover)
{
    /* TODO: unwrap ts against m->last_raw, find the latest accepted edge at or before it,
     *       convert elapsed ticks with m->freq_hz, set *holdover */
    (void)m; (void)ts; (void)holdover;
    return 0;
}

int match_nearest(const int64_t *cam_ns, size_t n_cam, const int64_t *lidar_ns, size_t n_lidar,
                  int64_t tol_ns, Pair *pairs, size_t cap, MatchStats *ms)
{
    /* TODO: two pointers; each camera frame takes the nearest unused LiDAR sweep within tol */
    (void)cam_ns; (void)n_cam; (void)lidar_ns; (void)n_lidar; (void)tol_ns;
    (void)pairs; (void)cap; (void)ms;
    return 0;
}

/* ---------------- test harness ---------------- */
#define F25   100002500u        /* +25 ppm oscillator: ticks per GPS second */
#define NOM   100000000u
#define TOL   100u              /* ppm */
#define MS    1000000LL

/* latch at GPS second s (may be fractional via extra ticks), counter starts at base */
static uint32_t at(uint32_t base, uint32_t f, uint32_t s, uint32_t extra)
{
    return (uint32_t)(base + (uint64_t)s * f + extra);
}

int main(void)
{
    PpsReport r;
    bool ho = false;
    memset(&r, 0, sizeof r);      /* stub-safe: never read indeterminate fields */

    /* 1. clean 10 s at +25 ppm: 11 edges, all OK, frequency and ppb exact */
    uint32_t c1[11];
    for (uint32_t s = 0; s <= 10; s++) c1[s] = at(1000u, F25, s, 0);
    CHECK(pps_analyze(c1, 11, NOM, TOL, &r) == 0);
    CHECK(r.ok == 10 && r.missed == 0 && r.glitch == 0 && r.bad == 0);
    CHECK(r.cls[0] == PPS_REF && r.cls[10] == PPS_OK && r.model.n == 11);
    CHECK(r.freq_hz == 100002500u && r.err_ppb == 25000);
    /*    a slow oscillator (-40 ppm) and PPS edge jitter of a few ticks */
    for (uint32_t s = 0; s <= 10; s++) c1[s] = at(1000u, 99996000u, s, 0);
    CHECK(pps_analyze(c1, 11, NOM, TOL, &r) == 0);
    CHECK(r.ok == 10 && r.freq_hz == 99996000u && r.err_ppb == -40000);
    static const int jit[11] = { 0, 3, -2, 1, -3, 2, 0, -1, 3, -2, 2 };
    for (uint32_t s = 0; s <= 10; s++) c1[s] = (uint32_t)(at(1000u, F25, s, 0) + jit[s]);
    CHECK(pps_analyze(c1, 11, NOM, TOL, &r) == 0);
    CHECK(r.ok == 10 && r.freq_hz == 100002500u && r.err_ppb == 25002);

    /* 2. missed PPS: second 5 missing -> one MISSED interval (k = 2); then 3 in a row (k = 4) */
    uint32_t c2[12], m2 = 0;
    for (uint32_t s = 0; s <= 10; s++) if (s != 5) c2[m2++] = at(1000u, F25, s, 0);
    CHECK(pps_analyze(c2, m2, NOM, TOL, &r) == 0);
    CHECK(r.ok == 8 && r.missed == 1 && r.cls[5] == PPS_MISSED);
    CHECK(r.model.n == 10 && r.model.sec[5] == 6 && r.freq_hz == 100002500u && r.err_ppb == 25000);
    m2 = 0;
    for (uint32_t s = 0; s <= 10; s++) if (s < 3 || s > 5) c2[m2++] = at(1000u, F25, s, 0);
    CHECK(pps_analyze(c2, m2, NOM, TOL, &r) == 0);
    CHECK(r.ok == 6 && r.missed == 3 && r.cls[3] == PPS_MISSED && r.model.sec[3] == 6);
    /*    +60 ppm, 4 s gap: error 24,000 ticks > w (10,000) but <= 4w -> still MISSED, not BAD */
    m2 = 0;
    for (uint32_t s = 0; s <= 7; s++) if (s < 3 || s > 5) c2[m2++] = at(1000u, 100006000u, s, 0);
    CHECK(pps_analyze(c2, m2, NOM, TOL, &r) == 0);
    CHECK(r.cls[3] == PPS_MISSED && r.missed == 3 && r.bad == 0 && r.freq_hz == 100006000u);

    /* 3. glitch: extra edge at 3.25 s and a ringing double edge 50 us after second 6 */
    uint32_t c3[13], m3 = 0;
    for (uint32_t s = 0; s <= 10; s++) {
        c3[m3++] = at(1000u, F25, s, 0);
        if (s == 3) c3[m3++] = at(1000u, F25, s, 25000625u);
        if (s == 6) c3[m3++] = at(1000u, F25, s, 5000u);
    }
    CHECK(pps_analyze(c3, m3, NOM, TOL, &r) == 0);
    CHECK(r.glitch == 2 && r.ok == 10 && r.cls[4] == PPS_GLITCH && r.cls[5] == PPS_OK);
    CHECK(r.cls[8] == PPS_GLITCH && r.model.n == 11 && r.freq_hz == 100002500u);

    /* 4. BAD: second 2 missing and a stray edge at 2.5 s (1.5 s after the last good edge),
     *    then an edge at 4 s that is 300 ppm late. A dropped edge costs its second:
     *    the next good edge is MISSED (k = 2). */
    uint32_t c4[6] = { at(0, F25, 0, 0), at(0, F25, 1, 0), at(0, F25, 2, 50001250u),
                       at(0, F25, 3, 0), at(0, F25, 4, 30000u), at(0, F25, 5, 0) };
    CHECK(pps_analyze(c4, 6, NOM, TOL, &r) == 0);
    CHECK(r.bad == 2 && r.cls[2] == PPS_BAD && r.cls[3] == PPS_MISSED && r.cls[4] == PPS_BAD);
    CHECK(r.cls[5] == PPS_MISSED && r.missed == 2 && r.ok == 1 && r.glitch == 0);
    CHECK(r.model.n == 4 && r.model.sec[3] == 5 && r.freq_hz == 100002500u);

    /* 5. combined example with a counter wrap in the middle (README / chart1):
     *    base 4,000,000,000, +25 ppm, second 5 missing, glitch at 7.25 s; wrap between 2 and 3 */
    const uint32_t B = 4000000000u;
    uint32_t ex[11] = { at(B, F25, 0, 0), at(B, F25, 1, 0), at(B, F25, 2, 0), at(B, F25, 3, 0),
                        at(B, F25, 4, 0), at(B, F25, 6, 0), at(B, F25, 7, 0),
                        at(B, F25, 7, 25000625u), at(B, F25, 8, 0), at(B, F25, 9, 0),
                        at(B, F25, 10, 0) };
    CHECK(ex[2] == 4200005000u && ex[3] == 5040204u && ex[7] == 430050829u && ex[10] == 705057704u);
    CHECK(pps_analyze(ex, 11, NOM, TOL, &r) == 0);
    CHECK(r.ok == 8 && r.missed == 1 && r.glitch == 1 && r.bad == 0);
    CHECK(r.cls[3] == PPS_OK && r.cls[5] == PPS_MISSED && r.cls[7] == PPS_GLITCH && r.cls[8] == PPS_OK);
    CHECK(r.model.n == 10 && r.model.tick[3] == 4300007500LL && r.model.tick[9] == 5000025000LL);
    CHECK(r.model.sec[9] == 10 && r.model.last_raw == 705057704u);
    CHECK(r.freq_hz == 100002500u && r.err_ppb == 25000);

    /* 6. counter -> GPS ns with the model from test 5 */
    PpsModel M = r.model;
    ho = true;
    CHECK(counter_to_gps_ns(&M, at(B, F25, 3, 50001250u), &ho) == 3500000000LL && !ho);
    CHECK(counter_to_gps_ns(&M, 3040154u, &ho) == 2980000000LL && !ho);    /* across the wrap */
    CHECK(counter_to_gps_ns(&M, at(B, F25, 4, 150003750u), &ho) == 5500000000LL && !ho); /* gap */
    CHECK(counter_to_gps_ns(&M, at(B, F25, 10, 0), &ho) == 10000000000LL && !ho);
    CHECK(counter_to_gps_ns(&M, at(B, F25, 10, 200005000u), &ho) == 12000000000LL && !ho);
    CHECK(counter_to_gps_ns(&M, at(B, F25, 10, 250006250u), &ho) == 12500000000LL && ho);
    CHECK(counter_to_gps_ns(&M, B - 1000u, &ho) == PPS_TS_INVALID);        /* before ref edge */
    /*    same stamps with the NOMINAL frequency (no discipline): 12.5 us and 62.5 us off */
    PpsModel Mn = M;
    Mn.freq_hz = NOM;
    CHECK(counter_to_gps_ns(&Mn, at(B, F25, 3, 50001250u), &ho) == 3500012500LL);
    CHECK(counter_to_gps_ns(&Mn, at(B, F25, 10, 250006250u), &ho) == 12500062500LL && ho);

    /* 7. camera (10 Hz) vs LiDAR (10 Hz) matching, tol 5 ms (README / chart3) */
    const int64_t cam[9] = { 0, 100 * MS, 200 * MS + 300000, 300 * MS, 400 * MS,
                             600 * MS, 700 * MS - 400000, 800 * MS, 900 * MS };
    const int64_t lid[9] = { 1200000, 101200000, 301200000, 409000000, 501200000,
                             601200000, 701200000, 801200000, 901200000 };
    Pair p[16];
    MatchStats st;
    memset(p, 0, sizeof p);
    memset(&st, 0, sizeof st);
    CHECK(match_nearest(cam, 9, lid, 9, 5 * MS, p, 16, &st) == 7);
    CHECK(st.pairs == 7 && st.cam_unmatched == 2 && st.lidar_unmatched == 2);
    CHECK(st.max_abs_dt_ns == 1600000);
    CHECK(p[0].cam == 0 && p[0].lidar == 0 && p[0].dt_ns == 1200000);
    CHECK(p[2].cam == 3 && p[2].lidar == 2);
    CHECK(p[3].cam == 5 && p[3].lidar == 5 && p[4].cam == 6 && p[4].dt_ns == 1600000);
    CHECK(p[6].cam == 8 && p[6].lidar == 8);
    /*    cap smaller than the pair count -> still returns the total */
    memset(p, 0, sizeof p);
    CHECK(match_nearest(cam, 9, lid, 9, 5 * MS, p, 3, &st) == 7 && p[3].cam == 0 && p[2].cam == 3);
    /*    a duplicated camera stamp cannot reuse a sweep */
    const int64_t cd[2] = { 0, 500000 };
    const int64_t ld[1] = { 1200000 };
    CHECK(match_nearest(cd, 2, ld, 1, 5 * MS, p, 16, &st) == 1);
    CHECK(p[0].cam == 0 && st.cam_unmatched == 1 && st.lidar_unmatched == 0);
    /*    the later sweep is nearer -> it wins, the skipped one stays unmatched */
    const int64_t cn[1] = { 100 * MS };
    const int64_t ln[2] = { 96 * MS, 101 * MS };
    CHECK(match_nearest(cn, 1, ln, 2, 5 * MS, p, 16, &st) == 1 && p[0].lidar == 1);
    CHECK(st.lidar_unmatched == 1 && p[0].dt_ns == 1 * MS);
    /*    empty LiDAR stream */
    CHECK(match_nearest(cam, 9, NULL, 0, 5 * MS, p, 16, &st) == 0 && st.cam_unmatched == 9);

    /* 8. bad arguments and degenerate input */
    CHECK(pps_analyze(NULL, 11, NOM, TOL, &r) == -EINVAL);
    CHECK(pps_analyze(ex, 1, NOM, TOL, &r) == -EINVAL);
    CHECK(pps_analyze(ex, PPS_MAX + 1, NOM, TOL, &r) == -EINVAL);
    CHECK(pps_analyze(ex, 11, 0, TOL, &r) == -EINVAL);
    CHECK(pps_analyze(ex, 11, NOM, 0, &r) == -EINVAL);
    CHECK(pps_analyze(ex, 11, NOM, TOL, NULL) == -EINVAL);
    uint32_t gl[3] = { 0, 1000u, 2000u };                  /* only glitches after the ref edge */
    CHECK(pps_analyze(gl, 3, NOM, TOL, &r) == -ENODATA && r.glitch == 2 && r.freq_hz == 0);
    ho = true;
    CHECK(counter_to_gps_ns(NULL, 0, &ho) == PPS_TS_INVALID && !ho);
    PpsModel z;
    memset(&z, 0, sizeof z);
    CHECK(counter_to_gps_ns(&z, 0, &ho) == PPS_TS_INVALID);
    CHECK(match_nearest(NULL, 3, lid, 9, 5 * MS, p, 16, &st) == -EINVAL);
    CHECK(match_nearest(cam, 9, lid, 9, -1, p, 16, &st) == -EINVAL);
    CHECK(match_nearest(cam, 9, lid, 9, 5 * MS, NULL, 16, &st) == -EINVAL);
    CHECK(match_nearest(cam, 9, lid, 9, 5 * MS, p, 16, NULL) == -EINVAL);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
