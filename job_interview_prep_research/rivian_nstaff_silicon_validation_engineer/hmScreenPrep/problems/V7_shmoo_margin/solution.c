/*
 * V7  Shmoo Sweep + Margin Analysis (PVT characterization)
 *
 * A mock DUT exposes one call:  bool run_test(uint16_t v_mv, uint16_t f_mhz)  -> pass/fail.
 * It is implemented at the bottom of this file by a hidden model (do not edit the fake):
 * a voltage/frequency wall, an optional deterministic "hole" and an optional flaky point.
 *
 * Implement:
 *   shmoo_sweep   Sweep every (V, F) point of the grid, run the test `repeats` times per point
 *                 and store one cell per point in map[vi * nf + fi] (vi = 0 is v_lo, fi = 0 is f_lo):
 *                 CELL_PASS (all runs passed), CELL_FAIL (all failed), CELL_MARGINAL (mixed).
 *                 Bad grid (step 0, hi < lo, repeats 0) -> -EINVAL; cap < nv * nf -> -ENOSPC.
 *   shmoo_fmax    For voltage row vi: the highest F such that F and EVERY lower grid frequency are
 *                 CELL_PASS ("contiguous pass from the bottom"). 0 if f_lo itself does not pass.
 *   shmoo_vmin    The lowest grid voltage whose fmax >= target_mhz. 0 if no row reaches it.
 *   shmoo_margin  At nominal_mv (must be a grid voltage, else -EINVAL):
 *                 margin_mhz = fmax - target_mhz (may be negative),
 *                 margin_pct10 = margin_mhz * 1000 / target_mhz (percent x10, truncated toward 0).
 *   shmoo_holes   A HOLE is a CELL_FAIL point that has a CELL_PASS point on both sides of it
 *                 in its row (lower F and higher F, same V) OR in its column (lower V and higher V,
 *                 same F). Report holes in map order; write at most cap, return the total.
 *   shmoo_render  ASCII plot, highest voltage first, one line per row: "%4u |" + one char per F
 *                 ('*' pass, '.' fail, '?' marginal) + '\n'. snprintf rules: never write past cap,
 *                 NUL-terminate when cap > 0, return the full length needed (without the NUL).
 *   No malloc. Integer math only.
 *
 * Reference solution.  Build & run:  ./run.sh V7 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- DUT interface (provided) ---------------- */
bool run_test(uint16_t v_mv, uint16_t f_mhz);   /* one functional test at (V, F): true = pass */

enum { CELL_FAIL = 0, CELL_PASS = 1, CELL_MARGINAL = 2 };

typedef struct {
    uint16_t v_lo_mv, v_hi_mv, v_step_mv;     /* voltage axis (rows)    */
    uint16_t f_lo_mhz, f_hi_mhz, f_step_mhz;  /* frequency axis (cols)  */
    uint8_t  repeats;                         /* runs per point          */
} ShmooCfg;

typedef struct { uint16_t v_mv, f_mhz; } ShmooPt;

/* Provided helpers: number of rows / columns, 0 if the axis is invalid. */
static size_t shmoo_nv(const ShmooCfg *c)
{
    if (c->v_step_mv == 0 || c->v_hi_mv < c->v_lo_mv) return 0;
    return (size_t)(c->v_hi_mv - c->v_lo_mv) / c->v_step_mv + 1;
}
static size_t shmoo_nf(const ShmooCfg *c)
{
    if (c->f_step_mhz == 0 || c->f_hi_mhz < c->f_lo_mhz) return 0;
    return (size_t)(c->f_hi_mhz - c->f_lo_mhz) / c->f_step_mhz + 1;
}
static uint16_t row_mv(const ShmooCfg *c, size_t vi)  { return (uint16_t)(c->v_lo_mv + vi * c->v_step_mv); }
static uint16_t col_mhz(const ShmooCfg *c, size_t fi) { return (uint16_t)(c->f_lo_mhz + fi * c->f_step_mhz); }

/* ---------------- your code ---------------- */

int shmoo_sweep(const ShmooCfg *c, uint8_t *map, size_t cap)
{
    if (c == NULL || c->repeats == 0) return -EINVAL;
    size_t nv = shmoo_nv(c), nf = shmoo_nf(c);
    if (nv == 0 || nf == 0) return -EINVAL;
    if (map == NULL || cap < nv * nf) return -ENOSPC;

    for (size_t vi = 0; vi < nv; vi++) {
        for (size_t fi = 0; fi < nf; fi++) {
            unsigned pass = 0;
            for (unsigned r = 0; r < c->repeats; r++)
                pass += run_test(row_mv(c, vi), col_mhz(c, fi)) ? 1u : 0u;
            map[vi * nf + fi] = (pass == c->repeats) ? CELL_PASS
                              : (pass == 0)          ? CELL_FAIL : CELL_MARGINAL;
        }
    }
    return 0;
}

uint16_t shmoo_fmax(const ShmooCfg *c, const uint8_t *map, size_t vi)
{
    size_t nf = shmoo_nf(c);
    if (vi >= shmoo_nv(c)) return 0;
    uint16_t fmax = 0;
    for (size_t fi = 0; fi < nf; fi++) {
        if (map[vi * nf + fi] != CELL_PASS) break;   /* first non-pass ends the run */
        fmax = col_mhz(c, fi);
    }
    return fmax;
}

uint16_t shmoo_vmin(const ShmooCfg *c, const uint8_t *map, uint16_t target_mhz)
{
    size_t nv = shmoo_nv(c);
    for (size_t vi = 0; vi < nv; vi++) {               /* lowest voltage first */
        uint16_t fm = shmoo_fmax(c, map, vi);
        if (fm != 0 && fm >= target_mhz) return row_mv(c, vi);
    }
    return 0;
}

int shmoo_margin(const ShmooCfg *c, const uint8_t *map, uint16_t nominal_mv, uint16_t target_mhz,
                 int32_t *margin_mhz, int32_t *margin_pct10)
{
    size_t nv = shmoo_nv(c);
    if (nv == 0 || target_mhz == 0 || nominal_mv < c->v_lo_mv ||
        (nominal_mv - c->v_lo_mv) % c->v_step_mv != 0)
        return -EINVAL;
    size_t vi = (size_t)(nominal_mv - c->v_lo_mv) / c->v_step_mv;
    if (vi >= nv) return -EINVAL;

    int32_t m = (int32_t)shmoo_fmax(c, map, vi) - (int32_t)target_mhz;
    *margin_mhz = m;
    *margin_pct10 = m * 1000 / (int32_t)target_mhz;    /* C division truncates toward 0 */
    return 0;
}

size_t shmoo_holes(const ShmooCfg *c, const uint8_t *map, ShmooPt *out, size_t cap)
{
    size_t nv = shmoo_nv(c), nf = shmoo_nf(c), cnt = 0;
    for (size_t vi = 0; vi < nv; vi++) {
        for (size_t fi = 0; fi < nf; fi++) {
            if (map[vi * nf + fi] != CELL_FAIL) continue;
            bool lo_f = false, hi_f = false, lo_v = false, hi_v = false;
            for (size_t k = 0; k < fi; k++)      lo_f |= map[vi * nf + k] == CELL_PASS;
            for (size_t k = fi + 1; k < nf; k++) hi_f |= map[vi * nf + k] == CELL_PASS;
            for (size_t k = 0; k < vi; k++)      lo_v |= map[k * nf + fi] == CELL_PASS;
            for (size_t k = vi + 1; k < nv; k++) hi_v |= map[k * nf + fi] == CELL_PASS;
            if ((lo_f && hi_f) || (lo_v && hi_v)) {
                if (cnt < cap) out[cnt] = (ShmooPt){ row_mv(c, vi), col_mhz(c, fi) };
                cnt++;
            }
        }
    }
    return cnt;
}

int shmoo_render(const ShmooCfg *c, const uint8_t *map, char *buf, size_t cap)
{
    size_t nv = shmoo_nv(c), nf = shmoo_nf(c), pos = 0;
    static const char glyph[3] = { '.', '*', '?' };

    for (size_t r = 0; r < nv; r++) {
        size_t vi = nv - 1 - r;                         /* highest voltage on top */
        char head[8];
        int hn = snprintf(head, sizeof head, "%4u |", (unsigned)row_mv(c, vi));
        for (int k = 0; k < hn; k++, pos++)
            if (pos + 1 < cap) buf[pos] = head[k];
        for (size_t fi = 0; fi < nf; fi++, pos++) {
            uint8_t cell = map[vi * nf + fi];
            if (pos + 1 < cap) buf[pos] = cell <= CELL_MARGINAL ? glyph[cell] : '#';
        }
        if (pos + 1 < cap) buf[pos] = '\n';
        pos++;
    }
    if (cap > 0) buf[pos < cap ? pos : cap - 1] = '\0';
    return (int)pos;
}

/* ======================================================================
 * Test harness: fake DUT (do not edit)
 *   pass  <=>  f <= fmax(v) = (v - v0_mv) * mhz_per_10mv / 10
 *   hole  : (hole_v, hole_f) always fails (e.g. a supply resonance / PLL lock problem)
 *   flaky : (flaky_v, flaky_f) fails on every 2nd call to that point
 * ====================================================================== */
static struct {
    int32_t  v0_mv, mhz_per_10mv;
    uint16_t hole_v, hole_f;       /* 0 = no hole        */
    uint16_t flaky_v, flaky_f;     /* 0 = no flaky point */
    uint32_t flaky_calls;
    uint32_t calls;                /* total run_test() calls */
} F;

static void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    F.v0_mv = 500; F.mhz_per_10mv = 20;            /* 2 MHz per mV above 500 mV */
}

bool run_test(uint16_t v_mv, uint16_t f_mhz)
{
    F.calls++;
    if (F.hole_v && v_mv == F.hole_v && f_mhz == F.hole_f) return false;
    if (F.flaky_v && v_mv == F.flaky_v && f_mhz == F.flaky_f && (++F.flaky_calls % 2) == 0)
        return false;
    int32_t fmax = ((int32_t)v_mv - F.v0_mv) * F.mhz_per_10mv / 10;
    return (int32_t)f_mhz <= fmax;
}

int main(void)
{
    static uint8_t map[64];
    static char txt[256];
    ShmooPt holes[4];
    int32_t mm = 0, mp = 0;
    const ShmooCfg g = { 700, 1000, 50,  200, 1000, 100,  1 };   /* 7 rows x 9 cols */

    /* 1. clean wall: fmax per row, Vmin, margin, plot */
    fake_reset();
    CHECK(shmoo_sweep(&g, map, sizeof map) == 0);
    CHECK(F.calls == 63);
    CHECK(shmoo_fmax(&g, map, 0) == 400 && shmoo_fmax(&g, map, 4) == 800 && shmoo_fmax(&g, map, 6) == 1000);
    CHECK(shmoo_vmin(&g, map, 700) == 850);
    CHECK(shmoo_margin(&g, map, 900, 700, &mm, &mp) == 0 && mm == 100 && mp == 142);
    CHECK(shmoo_holes(&g, map, holes, 4) == 0);
    const char *clean =
        "1000 |*********\n"
        " 950 |********.\n"
        " 900 |*******..\n"
        " 850 |******...\n"
        " 800 |*****....\n"
        " 750 |****.....\n"
        " 700 |***......\n";
    CHECK(shmoo_render(&g, map, txt, sizeof txt) == 112 && strcmp(txt, clean) == 0);

    /* 2. hole at (950 mV, 400 MHz): reported, and fmax at 950 collapses to 300 */
    fake_reset(); F.hole_v = 950; F.hole_f = 400;
    CHECK(shmoo_sweep(&g, map, sizeof map) == 0);
    CHECK(shmoo_holes(&g, map, holes, 4) == 1 && holes[0].v_mv == 950 && holes[0].f_mhz == 400);
    CHECK(shmoo_fmax(&g, map, 5) == 300);
    CHECK(shmoo_margin(&g, map, 950, 700, &mm, &mp) == 0 && mm == -400 && mp == -571);
    CHECK(shmoo_vmin(&g, map, 700) == 850);                 /* other rows unaffected */

    /* 3. flaky point (800 mV, 600 MHz): R=1 misses it, R=3 marks it MARGINAL */
    fake_reset(); F.flaky_v = 800; F.flaky_f = 600;
    CHECK(shmoo_sweep(&g, map, sizeof map) == 0);
    CHECK(map[2 * 9 + 4] == CELL_PASS);                     /* one run: looks fine */
    ShmooCfg g3 = g; g3.repeats = 3;
    fake_reset(); F.flaky_v = 800; F.flaky_f = 600;
    CHECK(shmoo_sweep(&g3, map, sizeof map) == 0);
    CHECK(F.calls == 189);
    CHECK(map[2 * 9 + 4] == CELL_MARGINAL);
    CHECK(shmoo_fmax(&g3, map, 2) == 500);                  /* marginal is not a pass */
    CHECK(shmoo_holes(&g3, map, holes, 4) == 0);            /* marginal is not a hole */
    CHECK(shmoo_render(&g3, map, txt, sizeof txt) == 112 && strstr(txt, " 800 |****?....\n") != NULL);

    /* 4. target unattainable, bad nominal, bad grid, small buffer */
    fake_reset();
    CHECK(shmoo_sweep(&g, map, sizeof map) == 0);
    CHECK(shmoo_vmin(&g, map, 1100) == 0);
    CHECK(shmoo_margin(&g, map, 900, 1100, &mm, &mp) == 0 && mm == -300 && mp == -272);
    CHECK(shmoo_margin(&g, map, 925, 700, &mm, &mp) == -EINVAL);
    CHECK(shmoo_margin(&g, map, 1050, 700, &mm, &mp) == -EINVAL);
    ShmooCfg bad = g; bad.f_step_mhz = 0;
    CHECK(shmoo_sweep(&bad, map, sizeof map) == -EINVAL);
    bad = g; bad.repeats = 0;
    CHECK(shmoo_sweep(&bad, map, sizeof map) == -EINVAL);
    CHECK(shmoo_sweep(&g, map, 62) == -ENOSPC);

    /* 5. single-row grid (one voltage) */
    fake_reset();
    const ShmooCfg g1 = { 900, 900, 50,  200, 1000, 100,  1 };
    CHECK(shmoo_sweep(&g1, map, 9) == 0 && F.calls == 9);
    CHECK(shmoo_fmax(&g1, map, 0) == 800 && shmoo_vmin(&g1, map, 800) == 900);
    CHECK(shmoo_render(&g1, map, txt, sizeof txt) == 16 && strcmp(txt, " 900 |*******..\n") == 0);

    /* 6. render into a tiny buffer: snprintf semantics */
    fake_reset();
    CHECK(shmoo_sweep(&g, map, sizeof map) == 0);
    char small[10]; memset(small, 'X', sizeof small);
    CHECK(shmoo_render(&g, map, small, sizeof small) == 112);
    CHECK(strcmp(small, "1000 |***") == 0);
    CHECK(shmoo_render(&g, map, NULL, 0) == 112);

    /* 7. hole + flaky together, R=3 (the plot in the README) */
    fake_reset(); F.hole_v = 950; F.hole_f = 400; F.flaky_v = 800; F.flaky_f = 600;
    CHECK(shmoo_sweep(&g3, map, sizeof map) == 0);
    const char *both =
        "1000 |*********\n"
        " 950 |**.*****.\n"
        " 900 |*******..\n"
        " 850 |******...\n"
        " 800 |****?....\n"
        " 750 |****.....\n"
        " 700 |***......\n";
    CHECK(shmoo_render(&g3, map, txt, sizeof txt) == 112 && strcmp(txt, both) == 0);
    CHECK(shmoo_vmin(&g3, map, 700) == 850);
    CHECK(shmoo_margin(&g3, map, 900, 700, &mm, &mp) == 0 && mm == 100 && mp == 142);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
