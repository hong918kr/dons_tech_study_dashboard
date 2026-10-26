/*
 * V12  Multi-Camera Frame Timestamp & Sync Checker
 *
 * Each camera delivers frame records {frame_counter, timestamp_ns}. The counter is a 16-bit
 * hardware counter that wraps 65535 -> 0. The timestamp comes from a PTP-synced 64-bit clock
 * (ns), taken at start-of-frame. Nominal period: 33,333,333 ns (30 fps).
 *
 * Implement:
 *   stream_stats  per-stream health (one pass, integer math, no malloc):
 *     d = (uint16_t)(cur.counter - prev.counter)        (wrap-safe)
 *       d == 0        -> duplicates++                    (baseline unchanged)
 *       d >= 0x8000   -> out_of_order++ (counter went back; baseline unchanged)
 *       otherwise     -> dropped += d - 1; if cur.ts <= prev.ts -> out_of_order++;
 *                        else if d == 1 -> timing pair: dt = cur.ts - prev.ts;
 *                        baseline = cur
 *     Over timing pairs: pairs, dt_min, dt_max, dt_mean (floor), max_jitter = max |dt - period|.
 *     All dt fields are 0 when pairs == 0.
 *   sync_check    camera 0 is the reference. For each reference frame i, find in every other
 *     camera the frame with the nearest timestamp; it matches only if |diff| <= period / 2.
 *     skew(i) = max - min timestamp over the reference + matched frames.
 *     complete / incomplete = all cameras matched or not. Track max skew (first index wins).
 *     Flag i if skew(i) > limit_ns: write up to flagged_cap indices, return the total flagged.
 *     Each stream's timestamps are non-decreasing (O(total frames) two-pointer walk).
 *   Bad args (NULL, ncam == 0 or > MAX_CAM, period == 0) -> -EINVAL.
 *
 * Build & run:  ./run.sh V12
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define MAX_CAM   8
#define PERIOD_NS 33333333ull          /* 30 fps */

typedef struct { uint16_t frame_counter; uint64_t ts_ns; } FrameRec;

typedef struct {
    uint32_t frames, dropped, duplicates, out_of_order;
    uint32_t pairs;                    /* consecutive (+1) pairs used for timing */
    uint64_t dt_min_ns, dt_max_ns, dt_mean_ns;
    uint64_t max_jitter_ns;            /* max |dt - period| */
} StreamStats;

typedef struct {
    uint32_t ref_frames, complete, incomplete;
    uint64_t max_skew_ns;
    uint32_t max_skew_idx;             /* reference frame index of the max skew */
} SyncStats;

int stream_stats(const FrameRec *f, size_t n, uint64_t period_ns, StreamStats *st);
int sync_check(const FrameRec *const cam[], const size_t n[], size_t ncam,
               uint64_t period_ns, uint64_t limit_ns,
               SyncStats *ss, uint32_t *flagged, size_t flagged_cap);

/* ---------------- your code ---------------- */
int stream_stats(const FrameRec *f, size_t n, uint64_t period_ns, StreamStats *st)
{
    /* TODO: one pass; wrap-safe 16-bit counter delta; dt stats over +1 pairs */
    (void)f; (void)n; (void)period_ns; (void)st;
    return 0;
}

int sync_check(const FrameRec *const cam[], const size_t n[], size_t ncam,
               uint64_t period_ns, uint64_t limit_ns,
               SyncStats *ss, uint32_t *flagged, size_t flagged_cap)
{
    /* TODO: per reference frame, nearest match per camera within period/2; skew = max - min */
    (void)cam; (void)n; (void)ncam; (void)period_ns; (void)limit_ns;
    (void)ss; (void)flagged; (void)flagged_cap;
    return 0;
}

/* =====================================================================
 * Test harness (do not edit)
 * ===================================================================== */
#define BASE_NS 5000000000ull                                     /* 5 s after PTP epoch */
static uint64_t gen(uint32_t k) { return BASE_NS + 1000000000ull * k / 30u; }  /* exact 30 fps */

/* camera with frames 0..cnt-1, counter starting at c0, timestamp = gen(k) + off + drift*k */
static size_t make_cam(FrameRec *out, uint32_t cnt, uint16_t c0, int64_t off, int64_t drift, int skip)
{
    size_t m = 0;
    for (uint32_t k = 0; k < cnt; k++) {
        if ((int)k == skip) continue;
        out[m].frame_counter = (uint16_t)(c0 + k);
        out[m].ts_ns = (uint64_t)((int64_t)gen(k) + off + drift * (int64_t)k);
        m++;
    }
    return m;
}

int main(void)
{
    static FrameRec c0[16], c1[16], c2[16];
    StreamStats st = { 0 };
    SyncStats ss = { 0 };
    uint32_t fl[8] = { 0 };

    /* 1. clean: 3 cameras, 10 frames, small fixed offsets */
    size_t n0 = make_cam(c0, 10, 100, 0, 0, -1);
    size_t n1 = make_cam(c1, 10, 7, 50000, 0, -1);
    size_t n2 = make_cam(c2, 10, 60000, -120000, 0, -1);
    CHECK(stream_stats(c0, n0, PERIOD_NS, &st) == 0);
    CHECK(st.frames == 10 && st.dropped == 0 && st.duplicates == 0 && st.out_of_order == 0);
    CHECK(st.pairs == 9 && st.dt_min_ns == 33333333 && st.dt_max_ns == 33333334);
    CHECK(st.dt_mean_ns == 33333333 && st.max_jitter_ns == 1);
    const FrameRec *cams[3] = { c0, c1, c2 };
    size_t ns[3] = { n0, n1, n2 };
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, fl, 8) == 0);
    CHECK(ss.ref_frames == 10 && ss.complete == 10 && ss.incomplete == 0);
    CHECK(ss.max_skew_ns == 170000 && ss.max_skew_idx == 0);

    /* 2. one dropped frame (counter 104 missing) */
    n0 = make_cam(c0, 10, 100, 0, 0, 4);
    CHECK(stream_stats(c0, n0, PERIOD_NS, &st) == 0);
    CHECK(st.frames == 9 && st.dropped == 1 && st.pairs == 7 && st.dt_max_ns == 33333334);

    /* 3. counter wrap 65535 -> 0: no drop; wrap + gap: one drop */
    n0 = make_cam(c0, 6, 65533, 0, 0, -1);
    CHECK(stream_stats(c0, n0, PERIOD_NS, &st) == 0);
    CHECK(st.dropped == 0 && st.out_of_order == 0 && st.pairs == 5);
    FrameRec w[3] = { { 65534, gen(0) }, { 65535, gen(1) }, { 1, gen(3) } };
    CHECK(stream_stats(w, 3, PERIOD_NS, &st) == 0);
    CHECK(st.dropped == 1 && st.pairs == 1 && st.dt_min_ns == 33333333);
    n0 = make_cam(c0, 7, 65533, 0, 0, 4);                       /* 65533 65534 65535 0 _ 2 3 */
    CHECK(stream_stats(c0, n0, PERIOD_NS, &st) == 0);
    CHECK(n0 == 6 && st.dropped == 1 && st.pairs == 4 && st.out_of_order == 0);

    /* 4. duplicate, late (out-of-order) frame, timestamp going backwards */
    FrameRec q[6] = { { 0, gen(0) }, { 1, gen(1) }, { 1, gen(1) + 1000 },
                      { 3, gen(3) }, { 2, gen(2) }, { 4, gen(4) } };
    CHECK(stream_stats(q, 6, PERIOD_NS, &st) == 0);
    CHECK(st.frames == 6 && st.duplicates == 1 && st.dropped == 1 && st.out_of_order == 1);
    CHECK(st.pairs == 2 && st.dt_min_ns == 33333333 && st.dt_max_ns == 33333333);
    FrameRec tb[3] = { { 0, gen(0) }, { 1, gen(1) }, { 2, gen(1) } };
    CHECK(stream_stats(tb, 3, PERIOD_NS, &st) == 0);
    CHECK(st.out_of_order == 1 && st.pairs == 1 && st.dropped == 0);

    /* 5. jitter outlier: frame 5 arrives 2 ms late */
    n0 = make_cam(c0, 10, 100, 0, 0, -1);
    c0[5].ts_ns += 2000000;
    CHECK(stream_stats(c0, n0, PERIOD_NS, &st) == 0);
    CHECK(st.dt_min_ns == 31333334 && st.dt_max_ns == 35333333);
    CHECK(st.dt_mean_ns == 33333333 && st.max_jitter_ns == 2000000);

    /* 6. camera 2 has a constant +2 ms offset -> every frame over the 1 ms limit */
    n0 = make_cam(c0, 10, 100, 0, 0, -1);
    n1 = make_cam(c1, 10, 7, 50000, 0, -1);
    n2 = make_cam(c2, 10, 60000, 2000000, 0, -1);
    ns[0] = n0; ns[1] = n1; ns[2] = n2;
    memset(fl, 0xFF, sizeof fl);
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, fl, 4) == 10);   /* count > cap */
    CHECK(fl[0] == 0 && fl[3] == 3 && fl[4] == 0xFFFFFFFFu);                 /* nothing past cap */
    CHECK(ss.max_skew_ns == 2000000 && ss.max_skew_idx == 0 && ss.complete == 10);
    n2 = make_cam(c2, 10, 60000, 1000000, 0, -1);                            /* exactly at limit */
    ns[2] = n2;
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, NULL, 0) == 0);

    /* 7. example: cam1 drops frame 4, cam2 drifts +0.4 ms per frame (not PTP-locked) */
    n0 = make_cam(c0, 6, 100, 0, 0, -1);
    n1 = make_cam(c1, 6, 7, 50000, 0, 4);
    n2 = make_cam(c2, 6, 60000, 0, 400000, -1);
    ns[0] = n0; ns[1] = n1; ns[2] = n2;
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, fl, 8) == 3);
    CHECK(fl[0] == 3 && fl[1] == 4 && fl[2] == 5);
    CHECK(ss.ref_frames == 6 && ss.complete == 5 && ss.incomplete == 1);
    CHECK(ss.max_skew_ns == 2000000 && ss.max_skew_idx == 5);
    CHECK(stream_stats(c2, n2, PERIOD_NS, &st) == 0);
    CHECK(st.dt_min_ns == 33733333 && st.dt_max_ns == 33733334 && st.max_jitter_ns == 400001);
    CHECK(stream_stats(c1, n1, PERIOD_NS, &st) == 0);
    CHECK(st.frames == 5 && st.dropped == 1 && st.pairs == 3);

    /* 8. empty streams and bad arguments */
    st.frames = 99;
    CHECK(stream_stats(NULL, 0, PERIOD_NS, &st) == 0);
    CHECK(st.frames == 0 && st.pairs == 0 && st.dt_min_ns == 0 && st.dt_mean_ns == 0);
    ns[1] = 0;                                                    /* camera 1 delivered nothing */
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, fl, 8) == 3);
    CHECK(ss.ref_frames == 6 && ss.incomplete == 6 && ss.complete == 0);
    ns[0] = 0;                                                    /* reference empty */
    CHECK(sync_check(cams, ns, 3, PERIOD_NS, 1000000, &ss, fl, 8) == 0 && ss.ref_frames == 0);
    CHECK(sync_check(cams, ns, 0, PERIOD_NS, 1000000, &ss, fl, 8) == -EINVAL);
    CHECK(stream_stats(c0, n0, 0, &st) == -EINVAL);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
