/*
 * L3  RFC 2544 Throughput Search for an Ethernet / IO Path -- Reference solution
 *
 * A traffic generator offers frames to a device under test (DUT) at a chosen rate for a
 * fixed trial time and counts how many come back. The DUT here is a MOCK at the bottom of
 * this file; your code only sees it through a trial callback:
 *
 *     uint64_t trial(void *ctx, uint64_t rate_fps, uint32_t frame_len, uint64_t n_frames)
 *         -> frames received (zero loss <=> received == sent)
 *
 * Rates are expressed in 0.01 % of line rate ("pct_x100", 0..10000).
 *     rate_fps(pct_x100) = line_fps * pct_x100 / 10000          (floor)
 *     frames per trial   = rate_fps * trial_ms / 1000            (floor, at least 1)
 *
 * Implement:
 *   line_rate_fps       max frames/s on the wire. frame_len includes the FCS (64..9216);
 *                       every frame also costs 20 bytes on the wire (preamble 7 + SFD 1 +
 *                       IFG 12). Floor. Bad args (link_bps == 0, len out of range) -> 0.
 *   rfc2544_throughput  RFC 2544 sec 26.1: highest rate with ZERO frame loss.
 *                       1) trial at 100 %. Zero loss -> candidate = 100 %.
 *                       2) else binary search: lo = 0 (pass), hi = 10000 (fail);
 *                          while hi - lo > res_x100: mid = lo + (hi - lo) / 2, trial,
 *                          zero loss -> lo = mid, else hi = mid. candidate = lo.
 *                       3) confirm: one more trial at the candidate. Zero loss -> done.
 *                          Loss -> hi = candidate, lo = 0, go back to the search in 2).
 *                          (candidate 0 = nothing passed: done without a confirmation.)
 *                       At most max_trials trials in total; budget gone -> -ETIMEDOUT.
 *                       Bad args -> -EINVAL. out->trials is always set.
 *   rfc2544_loss_curve  RFC 2544 sec 26.3: trials at 100 %, 90 %, ..., 10 % of line rate.
 *                       Record one LossPoint per step. Stop after two CONSECUTIVE steps with
 *                       zero lost frames (or after the 10 % step). Returns the number of
 *                       points, -ENOSPC if pts[] is too small, -EINVAL on bad args.
 *
 * Integer math only (no float), no malloc.
 *
 * Build & run:  ./run.sh L3 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define WIRE_OVERHEAD   20u        /* preamble 7 + SFD 1 + IFG 12 bytes per frame */
#define FRAME_MIN       64u
#define FRAME_MAX       9216u
#define PCT_FULL        10000u     /* 100.00 % in pct_x100 units */
#define LOSS_STEP       1000u      /* 10 % step for the loss curve */

typedef uint64_t (*trial_fn)(void *ctx, uint64_t rate_fps, uint32_t frame_len, uint64_t n_frames);

typedef struct {
    uint32_t res_x100;    /* search resolution, 1..10000 (10 = 0.1 % of line rate) */
    uint32_t max_trials;  /* total trial budget, >= 1 */
    uint32_t trial_ms;    /* trial duration, >= 1 (RFC 2544: 60000) */
} TputCfg;

typedef struct {
    uint64_t rate_fps;    /* zero-loss throughput, frames/s */
    uint32_t pct_x100;    /* same, in 0.01 % of line rate */
    uint32_t trials;      /* trials used, including the confirmation(s) */
} TputResult;

typedef struct {
    uint32_t pct_x100;    /* offered load */
    uint64_t rate_fps;
    uint64_t sent, lost;  /* lost = sent - received (0 if received >= sent) */
    uint32_t loss_x100;   /* lost * 10000 / sent, floor (0.01 % units) */
} LossPoint;

uint64_t line_rate_fps(uint64_t link_bps, uint32_t frame_len);
int rfc2544_throughput(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       const TputCfg *cfg, TputResult *out);
int rfc2544_loss_curve(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       uint32_t trial_ms, LossPoint *pts, int cap);

/* ---------------- your code ---------------- */
uint64_t line_rate_fps(uint64_t link_bps, uint32_t frame_len)
{
    if (link_bps == 0 || frame_len < FRAME_MIN || frame_len > FRAME_MAX) return 0;
    return link_bps / ((uint64_t)(frame_len + WIRE_OVERHEAD) * 8u);   /* bits per frame slot */
}

static uint64_t rate_at(uint64_t line, uint32_t pct) { return line * pct / PCT_FULL; }

static uint64_t frames_for(uint64_t rate, uint32_t trial_ms)
{
    uint64_t n = rate * trial_ms / 1000u;
    return n ? n : 1u;
}

/* One trial at pct. Returns 1 = zero loss, 0 = loss, -ETIMEDOUT = budget used up. */
static int run_trial(trial_fn fn, void *ctx, uint64_t line, uint32_t len,
                     const TputCfg *cfg, uint32_t pct, uint32_t *trials)
{
    if (*trials >= cfg->max_trials) return -ETIMEDOUT;
    (*trials)++;
    uint64_t rate = rate_at(line, pct);
    uint64_t n = frames_for(rate, cfg->trial_ms);
    return fn(ctx, rate, len, n) == n;      /* more than sent (duplicates) is also a fail */
}

int rfc2544_throughput(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       const TputCfg *cfg, TputResult *out)
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof *out);
    uint64_t line = line_rate_fps(link_bps, frame_len);
    if (fn == NULL || cfg == NULL || line == 0 || cfg->res_x100 == 0 ||
        cfg->res_x100 > PCT_FULL || cfg->max_trials == 0 || cfg->trial_ms == 0)
        return -EINVAL;

    uint32_t trials = 0, lo = 0, hi = PCT_FULL, cand = PCT_FULL;
    int ok = run_trial(fn, ctx, line, frame_len, cfg, PCT_FULL, &trials);
    if (ok < 0) goto timeout;
    bool search = !ok;                      /* 100 % passed -> skip straight to confirming it */
    for (;;) {
        if (search) {
            while (hi - lo > cfg->res_x100) {
                uint32_t mid = lo + (hi - lo) / 2;
                ok = run_trial(fn, ctx, line, frame_len, cfg, mid, &trials);
                if (ok < 0) goto timeout;
                if (ok) lo = mid; else hi = mid;
            }
            cand = lo;
            if (cand == 0) break;           /* nothing passes: nothing to confirm */
        }
        ok = run_trial(fn, ctx, line, frame_len, cfg, cand, &trials);   /* confirmation */
        if (ok < 0) goto timeout;
        if (ok) break;
        hi = cand; lo = 0; search = true;   /* candidate was a false pass: search below it */
    }
    out->pct_x100 = cand;
    out->rate_fps = rate_at(line, cand);
    out->trials = trials;
    return 0;

timeout:
    out->trials = trials;
    return -ETIMEDOUT;
}

int rfc2544_loss_curve(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       uint32_t trial_ms, LossPoint *pts, int cap)
{
    uint64_t line = line_rate_fps(link_bps, frame_len);
    if (fn == NULL || pts == NULL || cap < 1 || line == 0 || trial_ms == 0) return -EINVAL;

    int n = 0, zero_run = 0;
    for (uint32_t pct = PCT_FULL; pct >= LOSS_STEP && zero_run < 2; pct -= LOSS_STEP) {
        if (n == cap) return -ENOSPC;
        uint64_t rate = rate_at(line, pct), sent = frames_for(rate, trial_ms);
        uint64_t rx = fn(ctx, rate, frame_len, sent);
        uint64_t lost = rx >= sent ? 0 : sent - rx;
        pts[n] = (LossPoint){ pct, rate, sent, lost, (uint32_t)(lost * PCT_FULL / sent) };
        n++;
        zero_run = lost == 0 ? zero_run + 1 : 0;   /* decide on frame count, not rounded % */
    }
    return n;
}

/* =====================================================================
 * Test harness: mock DUT (do not edit)
 * A path limited by per-packet processing: it forwards up to pps_max frames/s.
 * Offered above that, it forwards n * pps_max / rate of the n frames (floor).
 * ===================================================================== */
enum { FLAKY_NONE, FLAKY_BOOST, FLAKY_DROP };

typedef struct {
    uint64_t pps_max;       /* forwarding capacity, frames/s */
    uint32_t trials;        /* trials run so far */
    uint32_t flaky_at;      /* 1-based trial index with a transient event; 0 = off */
    int      flaky_kind;    /* BOOST: that trial loses nothing (e.g. a competing bus
                               master was idle); DROP: that trial loses 1 frame (bit error) */
    uint64_t max_rate;      /* highest rate ever offered */
    uint64_t rate_log[64];  /* offered rate per trial */
} MockDut;

static uint64_t mock_trial(void *ctx, uint64_t rate_fps, uint32_t frame_len, uint64_t n)
{
    MockDut *d = ctx;
    (void)frame_len;
    d->trials++;
    if (d->trials <= 64) d->rate_log[d->trials - 1] = rate_fps;
    if (rate_fps > d->max_rate) d->max_rate = rate_fps;
    uint64_t rx = n;
    if (rate_fps > d->pps_max) rx = n * d->pps_max / rate_fps;
    if (d->trials == d->flaky_at) {
        if (d->flaky_kind == FLAKY_BOOST) rx = n;
        if (d->flaky_kind == FLAKY_DROP && rx > 0) rx--;
    }
    return rx;
}

static MockDut mk(uint64_t pps_max) { MockDut d; memset(&d, 0, sizeof d); d.pps_max = pps_max; return d; }

#define G10  10000000000ull
#define G1   1000000000ull
#define G25  25000000000ull

int main(void)
{
    const TputCfg cfg = { 10, 32, 60000 };    /* 0.1 % resolution, 32 trials, 60 s */
    TputResult r = { 0 };
    LossPoint pts[10] = { { 0 } };
    MockDut d;

    /* 1. line rate known answers (floor) */
    CHECK(line_rate_fps(G10, 64) == 14880952u);
    CHECK(line_rate_fps(G10, 512) == 2349624u);
    CHECK(line_rate_fps(G10, 1518) == 812743u);
    CHECK(line_rate_fps(G1, 64) == 1488095u);
    CHECK(line_rate_fps(G1, 512) == 234962u);
    CHECK(line_rate_fps(G1, 1518) == 81274u);
    CHECK(line_rate_fps(G25, 64) == 37202380u);
    CHECK(line_rate_fps(G25, 512) == 5874060u);
    CHECK(line_rate_fps(G25, 1518) == 2031859u);
    CHECK(line_rate_fps(G10, 9216) == 135339u);           /* jumbo */
    CHECK(line_rate_fps(G10, 63) == 0 && line_rate_fps(G10, 9217) == 0 && line_rate_fps(0, 64) == 0);

    /* 2. DUT that forwards at line rate: 100 % + one confirmation = 2 trials */
    d = mk(UINT64_MAX);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 10000 && r.rate_fps == 14880952u && r.trials == 2 && d.trials == 2);
    d = mk(UINT64_MAX);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 1518, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 10000 && r.rate_fps == 812743u && r.trials == 2);

    /* 3. path capped at 9.1 Mfps: 64 B tops out at 61.12 %, 1518 B reaches line rate */
    d = mk(9100000u);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 6112 && r.rate_fps == 9095237u && r.trials == 12);
    CHECK(d.rate_log[0] == 14880952u && d.rate_log[1] == 7440476u && d.rate_log[2] == 11160714u);
    CHECK(d.rate_log[11] == r.rate_fps);                    /* last trial = confirmation */
    CHECK(d.max_rate == 14880952u);                         /* never offered above line rate */
    d = mk(9100000u);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 1518, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 10000 && r.trials == 2);

    /* 4. frame-size sweep on a 3.0 Mfps path (RFC 2544 frame sizes) */
    {
        static const uint32_t sz[7]  = { 64, 128, 256, 512, 1024, 1280, 1518 };
        static const uint32_t exp[7] = { 2011, 3544, 6620, 10000, 10000, 10000, 10000 };
        int ok = 0;
        for (int i = 0; i < 7; i++) {
            d = mk(3000000u);
            ok += rfc2544_throughput(mock_trial, &d, G10, sz[i], &cfg, &r) == 0 && r.pct_x100 == exp[i];
        }
        CHECK(ok == 7);
    }

    /* 5. flaky false PASS at trial 3 (75 %): the confirmation catches it */
    d = mk(9100000u); d.flaky_at = 3; d.flaky_kind = FLAKY_BOOST;
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 6114 && r.rate_fps == 9098214u && r.trials == 23);
    CHECK(r.rate_fps <= 9100000u);                          /* never above the real capacity */
    d = mk(9100000u); d.flaky_at = 3; d.flaky_kind = FLAKY_BOOST;
    const TputCfg small = { 10, 20, 60000 };
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &small, &r) == -ETIMEDOUT);
    CHECK(r.trials == 20 && d.trials == 20 && r.pct_x100 == 0);

    /* 6. flaky false FAIL (1 frame dropped at trial 2, 50 %): NOT caught -> low result */
    d = mk(9100000u); d.flaky_at = 2; d.flaky_kind = FLAKY_DROP;
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 4990 && r.rate_fps == 7425595u && r.trials == 12);

    /* 7. dead path (forwards nothing): 0 %, no confirmation trial */
    d = mk(0);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, &r) == 0);
    CHECK(r.pct_x100 == 0 && r.rate_fps == 0 && r.trials == 11);

    /* 8. frame-loss curve, 9.1 Mfps path at 64 B: stops after 60 % and 50 % are clean */
    d = mk(9100000u);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 64, 60000, pts, 10) == 6);
    CHECK(pts[0].pct_x100 == 10000 && pts[0].loss_x100 == 3884 && pts[0].sent == 892857120u);
    CHECK(pts[1].loss_x100 == 3205 && pts[2].loss_x100 == 2355 && pts[3].loss_x100 == 1263);
    CHECK(pts[4].lost == 0 && pts[5].lost == 0 && pts[5].pct_x100 == 5000);
    d = mk(UINT64_MAX);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 1518, 60000, pts, 10) == 2);
    CHECK(pts[1].pct_x100 == 9000 && pts[1].lost == 0);
    /* one dropped frame at the 50 % step rounds to 0.00 % but is NOT zero loss */
    d = mk(9100000u); d.flaky_at = 6; d.flaky_kind = FLAKY_DROP;
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 64, 60000, pts, 10) == 8);
    CHECK(pts[5].lost == 1 && pts[5].loss_x100 == 0 && pts[7].pct_x100 == 3000);
    /* path that loses at every step (1 Mfps < 10 % of 64 B line rate): all 10 steps */
    d = mk(1000000u);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 64, 60000, pts, 10) == 10);
    CHECK(pts[9].pct_x100 == 1000 && pts[9].lost > 0);

    /* 9. bad arguments */
    d = mk(UINT64_MAX);
    const TputCfg bad_res = { 0, 32, 60000 }, bad_tr = { 10, 0, 60000 }, bad_ms = { 10, 32, 0 };
    CHECK(rfc2544_throughput(NULL, &d, G10, 64, &cfg, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 63, &cfg, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, 0, 64, &cfg, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &bad_res, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &bad_tr, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &bad_ms, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, NULL, &r) == -EINVAL);
    CHECK(rfc2544_throughput(mock_trial, &d, G10, 64, &cfg, NULL) == -EINVAL);
    CHECK(d.trials == 0);                                   /* no trial on bad args */
    d = mk(9100000u);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 64, 60000, pts, 3) == -ENOSPC);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 64, 60000, pts, 0) == -EINVAL);
    CHECK(rfc2544_loss_curve(mock_trial, &d, G10, 9217, 60000, pts, 10) == -EINVAL);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
