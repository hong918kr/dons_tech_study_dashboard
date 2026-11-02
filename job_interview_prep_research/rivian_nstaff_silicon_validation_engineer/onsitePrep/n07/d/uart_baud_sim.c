/*
 * N07D - UART baud-error calculator + receiver sampling simulator (8N1).
 *
 * Part 1  divisor table   : fclk, target baud, oversampling (16x/8x), optional
 *                           4-bit fractional divider -> divisor, actual baud,
 *                           error %, one-sided theory limit, verdict (vs file).
 * Part 2  analytic window : derive the tolerance window from the sampling model.
 * Part 3  sampling sim    : a mis-clocked TX drives a 16x/8x receiver
 *                           (center sample or 3-sample majority); an oracle
 *                           tells which bit slot each sample REALLY hit, so we
 *                           can print the first bit the receiver gets wrong
 *                           even when the data value happens to hide it.
 * Part 4  sweep           : empirical zero-error window vs the analytic one.
 *
 * Model assumptions (stated in the note):
 *   - clean, instantaneous edges (no rise time / threshold skew / noise)
 *   - receiver hunts for the start edge on every sample tick -> detection delay
 *     delta in [0, 1/osr) bit; then samples at osr/2 ticks after detection and
 *     every osr ticks after that (start, d0..d7, stop)
 *   - frame = 10 bits; stop sampled 0 -> framing error (FE); after the stop
 *     sample the receiver immediately hunts again (resync like 16550 FE note)
 *   - error e = baud_tx/baud_rx - 1  (e > 0: TX fast, receiver "slow")
 * Self-checking: every case prints PASS/FAIL against an expected diagnosis.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_BITS 10
#define MAXB 64

static int g_pass, g_fail;

static void check(int ok, const char *what) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) g_pass++; else g_fail++;
}

/* ---------- analytic limits (fractions, e.g. 0.0457) ---------- */
/* receiver slow (TX fast, e>0): stop sample (F-0.5+delta) must stay < F, worst delta -> 1/osr */
static double lim_pos(int osr, int extra_ticks) {
    return (double)FRAME_BITS / (FRAME_BITS - 0.5 + (1.0 + extra_ticks) / osr) - 1.0;
}
/* receiver fast (TX slow, e<0): stop sample must stay > F-1, worst delta = 0 */
static double lim_neg(int osr, int extra_ticks) {
    return (double)(FRAME_BITS - 1) / (FRAME_BITS - 0.5 - (double)extra_ticks / osr) - 1.0;
}

/* ---------- Part 1: divisor calculator ---------- */
static const char *verdict_of(double err, int osr) {
    double a = fabs(err);
    double lim = err >= 0 ? lim_pos(osr, 0) : -lim_neg(osr, 0);
    if (a <= 0.020) return "OK";
    if (a < lim) return "MARGINAL";
    return "FAIL";
}

static int part1(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return -1; }
    printf("== Part 1: divisor / baud error table (8N1, partner assumed exact) ==\n");
    printf("%-18s %9s %9s %8s %3s %4s %9s %11s %8s %8s %-8s %s\n", "case", "fclk_drv", "fclk_real",
           "target", "osr", "frac", "divisor", "actual", "err%", "limit%", "verdict", "bit_us(meas/ideal)");
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[64], exp[16];
        double fa, fr, baud;
        int osr, frac;
        if (sscanf(line, "%63s %lf %lf %lf %d %d %15s", name, &fa, &fr, &baud, &osr, &frac, exp) != 7) continue;
        double ideal = fa / (osr * baud);
        double div;
        if (frac > 0) {
            double q = (double)(1 << frac);
            div = floor(ideal * q + 0.5) / q;
        } else {
            div = floor(ideal + 0.5);
        }
        if (div < 1.0) div = 1.0;
        double actual = fr / (osr * div);
        double err = actual / baud - 1.0;
        double lim = err >= 0 ? lim_pos(osr, 0) : lim_neg(osr, 0);
        const char *v = verdict_of(err, osr);
        printf("%-18s %9.0f %9.0f %8.0f %3d %4d %9.4f %11.1f %+8.3f %+8.3f %-8s %.3f/%.3f\n", name, fa, fr, baud, osr,
               frac, div, actual, err * 100, lim * 100, v, 1e6 / actual, 1e6 / baud);
        char msg[128];
        snprintf(msg, sizeof msg, "%s verdict %s (expected %s)", name, v, exp);
        check(strcmp(v, exp) == 0, msg);
    }
    fclose(f);
    return 0;
}

/* ---------- Part 3: sampling simulator ---------- */
typedef struct {
    double baud_tx, baud_rx;
    int osr, maj3, gap_bits;
    double phase; /* receiver tick phase, fraction of one tick */
    const unsigned char *data;
    int n;
} SimCfg;

typedef struct {
    int rx_frames, byte_err, fe, noise;
    int slip_frame, slip_k; /* first frame (time order) whose sample hit the wrong slot, and that k */
    int min_slip_k;         /* smallest k that slipped in any frame */
    int vis_frame, vis_k;   /* first visible wrong bit value */
    unsigned char rx[MAXB];
} SimRes;

static double s_t0, s_ttx, s_period;
static const SimCfg *s_cfg;

static int expected_bit(int f, int k) {
    if (k == 0) return 0;
    if (k == 9) return 1;
    return (s_cfg->data[f] >> (k - 1)) & 1;
}

static int line_at(double t) {
    if (t < s_t0) return 1;
    int i = (int)floor((t - s_t0) / s_period);
    if (i >= s_cfg->n) return 1;
    int b = (int)floor((t - (s_t0 + i * s_period)) / s_ttx);
    if (b >= FRAME_BITS) return 1; /* idle gap */
    return expected_bit(i, b);
}

static int slot_in_frame(double t, int f) { return (int)floor((t - (s_t0 + f * s_period)) / s_ttx); }

static SimRes simulate(const SimCfg *c) {
    SimRes r;
    memset(&r, 0, sizeof r);
    r.slip_frame = r.slip_k = r.min_slip_k = r.vis_frame = r.vis_k = -1;
    s_cfg = c;
    s_ttx = 1.0 / c->baud_tx;
    s_t0 = 2.0 * s_ttx;
    s_period = (FRAME_BITS + c->gap_bits) * s_ttx;
    double tt = 1.0 / (c->osr * c->baud_rx);
    double t_end = s_t0 + c->n * s_period + 3 * s_ttx;
    long n = 0;
#define TK(i) (((double)(i) + c->phase) * tt)
    while (TK(n) < t_end) {
        if (line_at(TK(n)) != 0) { n++; continue; }
        long ctr = n + c->osr / 2;
        int v[FRAME_BITS];
        for (int k = 0; k < FRAME_BITS; k++) {
            long s = ctr + (long)k * c->osr;
            int b = line_at(TK(s));
            if (c->maj3) {
                int a = line_at(TK(s - 1)), z = line_at(TK(s + 1));
                if (a != b || z != b) r.noise++;
                b = (a + b + z) >= 2;
            }
            v[k] = b;
            if (k == 0 && b != 0) break; /* false start */
        }
        if (v[0] != 0) { n = ctr + 1; continue; }
        int f = r.rx_frames;
        if (f < c->n) {
            for (int k = 0; k < FRAME_BITS; k++) {
                long s = ctr + (long)k * c->osr;
                if (slot_in_frame(TK(s), f) != k) {
                    if (r.slip_frame < 0) { r.slip_frame = f; r.slip_k = k; }
                    if (r.min_slip_k < 0 || k < r.min_slip_k) r.min_slip_k = k;
                    break;
                }
            }
            for (int k = 0; k < FRAME_BITS; k++)
                if (v[k] != expected_bit(f, k)) {
                    if (r.vis_frame < 0) { r.vis_frame = f; r.vis_k = k; }
                    break;
                }
        }
        unsigned char byte = 0;
        for (int k = 1; k <= 8; k++) byte |= (unsigned char)(v[k] << (k - 1));
        if (v[9] == 0) r.fe++;
        if (f < MAXB) r.rx[f] = byte;
        if (f >= c->n || byte != c->data[f]) r.byte_err++;
        r.rx_frames++;
        n = ctr + (long)9 * c->osr + 1;
    }
#undef TK
    if (r.rx_frames != c->n) r.byte_err += abs(r.rx_frames - c->n);
    return r;
}

static const char *kname(int k) {
    static const char *nm[] = {"start", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "stop"};
    return (k >= 0 && k < FRAME_BITS) ? nm[k] : "-";
}

static void print_res(const char *tag, const SimCfg *c, const SimRes *r) {
    printf("  [%s] e=%+.2f%% osr=%d %s gap=%d  frames tx=%d rx=%d byte_err=%d FE=%d noise=%d\n", tag,
           (c->baud_tx / c->baud_rx - 1) * 100, c->osr, c->maj3 ? "maj3" : "center", c->gap_bits, c->n,
           r->rx_frames, r->byte_err, r->fe, r->noise);
    printf("        first slot slip: frame %d bit %s (min over frames: %s)   first VISIBLE wrong bit: frame %d bit %s\n",
           r->slip_frame, kname(r->slip_k), kname(r->min_slip_k), r->vis_frame, kname(r->vis_k));
}

static void print_bytes(const char *tag, const unsigned char *d, int n) {
    printf("        %s:", tag);
    for (int i = 0; i < n && i < MAXB; i++) printf(" %02X", d[i]);
    printf("  \"");
    for (int i = 0; i < n && i < MAXB; i++) putchar((d[i] >= 0x20 && d[i] < 0x7f) ? d[i] : '.');
    printf("\"\n");
}

static void part3(void) {
    const double B = 115200.0;
    unsigned char u55[16], uff[16];
    memset(u55, 0x55, sizeof u55);
    memset(uff, 0xFF, sizeof uff);
    const unsigned char hello[] = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'R', 'i', 'v', 'i', 'a', 'n', '\r', '\n'};
    SimRes r;
    SimCfg c;

    printf("\n== Part 3: sampling simulator (115200 nominal, 16 x 0x55 back-to-back unless noted) ==\n");

    printf("S1 good divisor: 24 MHz/16 -> +0.16%%\n");
    c = (SimCfg){B * 1.0016, B, 16, 0, 0, 0.37, u55, 16};
    r = simulate(&c); print_res("S1", &c, &r);
    check(r.byte_err == 0 && r.fe == 0 && r.slip_k < 0, "S1 no errors, no slot slip");

    printf("S2 25 MHz/16 divisor -> -3.12%%, partner exact\n");
    c = (SimCfg){B * (1 - 0.03125), B, 16, 0, 0, 0.37, u55, 16};
    r = simulate(&c); print_res("S2", &c, &r);
    check(r.byte_err == 0 && r.fe == 0, "S2 alone it still works (inside -5.26% window)");

    printf("S3 same TX (-3.12%%) but partner RX clock +2.5%% fast (RC osc drift)\n");
    c = (SimCfg){B * (1 - 0.03125), B * 1.025, 16, 0, 0, 0.37, u55, 16};
    r = simulate(&c); print_res("S3", &c, &r);
    check(r.fe > 0 && r.slip_k == 9, "S3 combined error breaks the budget: stop bit slips first -> FE");

    printf("S4 driver assumed 24 MHz, UART clock is 26 MHz -> +8.51%% (\"console garbage\")\n");
    c = (SimCfg){26e6 / (16 * 13.0), B, 16, 0, 0, 0.37, hello, (int)sizeof hello};
    r = simulate(&c); print_res("S4", &c, &r);
    print_bytes("sent", hello, (int)sizeof hello);
    print_bytes("recv", r.rx, r.rx_frames);
    /* only frame 0 is a clean measurement: after the first FE the receiver resyncs on a data edge,
       frame alignment is lost (rx count != tx count) and later "slips" are cascade effects */
    check(r.byte_err > 0 && r.slip_frame == 0 && r.slip_k >= 5 && r.slip_k <= 6 && r.rx_frames != c.n,
          "S4 garbage; frame 0 first slips at d4 or d5 (drift > half a bit), then frames are lost");

    printf("S5 +5.0%% just past the +4.57%% limit: back-to-back vs 1 idle bit between frames\n");
    int fe_b2b = 0, fr_b2b = 0, fe_gap = 0, be_gap = 0, runs_with_fe = 0;
    for (int j = 0; j < 64; j++) {
        c = (SimCfg){B * 1.05, B, 16, 0, 0, (j + 0.5) / 64, u55, 16};
        r = simulate(&c); fe_b2b += r.fe; fr_b2b += r.rx_frames; runs_with_fe += r.fe > 0;
        c.gap_bits = 1;
        r = simulate(&c); fe_gap += r.fe; be_gap += r.byte_err;
    }
    printf("  [S5] 64 phases: back-to-back FE frames %d/%d (runs with FE %d/64) ; with 1-bit gap FE=%d byte_err=%d\n",
           fe_b2b, fr_b2b, runs_with_fe, fe_gap, be_gap);
    check(fe_b2b > 0 && fe_gap == 0 && be_gap == 0,
          "S5 near the limit FE depends on traffic: back-to-back fails, idle gap hides it");

    printf("S6 +8.51%% clock error, 2 idle bits between frames: 0xFF hides it, 0x55 exposes it\n");
    c = (SimCfg){B * 1.0851, B, 16, 0, 2, 0.37, uff, 16};
    r = simulate(&c); print_res("S6-FF", &c, &r);
    int ff_ok = r.byte_err == 0 && r.fe == 0 && r.slip_k >= 0;
    c.data = u55;
    r = simulate(&c); print_res("S6-55", &c, &r);
    check(ff_ok && r.byte_err > 0, "S6 slot slips under 0xFF are invisible; 0x55 shows them -> test with 0x55/0xAA");

    printf("S7 8x oversampling, +4.17%% (100 MHz/8/div4 for 3 Mbaud, scaled to 115200)\n");
    c = (SimCfg){B * (1 + 1.0 / 24), B, 8, 0, 0, 0.37, u55, 16};
    int bad8 = 0;
    for (int j = 0; j < 32; j++) { c.phase = (j + 0.5) / 32; r = simulate(&c); bad8 += r.fe + r.byte_err; }
    c.osr = 16;
    int bad16 = 0;
    for (int j = 0; j < 32; j++) { c.phase = (j + 0.5) / 32; r = simulate(&c); bad16 += r.fe + r.byte_err; }
    printf("  [S7] errors over 32 phases: 8x=%d  16x=%d\n", bad8, bad16);
    check(bad8 > 0 && bad16 == 0, "S7 same +4.17% error: 16x survives, 8x (coarser start detection) fails");
}

/* ---------- Part 4: sweep ---------- */
static int clean(double e, int osr, int maj3, int count_noise) {
    unsigned char u55[16];
    memset(u55, 0x55, sizeof u55);
    for (int j = 0; j < 32; j++) {
        SimCfg c = {115200.0 * (1 + e), 115200.0, osr, maj3, 0, (j + 0.5) / 32, u55, 16};
        SimRes r = simulate(&c);
        if (r.fe || r.byte_err || (count_noise && r.noise)) return 0;
    }
    return 1;
}

static double edge(int dir, int osr, int maj3, int count_noise) {
    double last = 0;
    for (double e = 0; e < 0.12; e += 0.0005) {
        if (!clean(dir * e, osr, maj3, count_noise)) break;
        last = dir * e;
    }
    return last;
}

static void part4(void) {
    printf("\n== Part 2+4: tolerance window, analytic vs simulated (8N1, clean edges) ==\n");
    printf("%-26s %10s %10s %10s %10s\n", "receiver", "an. TX-", "sim TX-", "an. TX+", "sim TX+");
    struct { const char *nm; int osr, maj3, noise, extra; } m[] = {
        {"16x center", 16, 0, 0, 0},
        {"16x maj3 (data/FE)", 16, 1, 0, 0},
        {"16x maj3 noise-flag-free", 16, 1, 1, 1},
        {"8x center", 8, 0, 0, 0},
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        double an_n = lim_neg(m[i].osr, m[i].extra), an_p = lim_pos(m[i].osr, m[i].extra);
        double sn = edge(-1, m[i].osr, m[i].maj3, m[i].noise), sp = edge(+1, m[i].osr, m[i].maj3, m[i].noise);
        printf("%-26s %+9.3f%% %+9.3f%% %+9.3f%% %+9.3f%%\n", m[i].nm, an_n * 100, sn * 100, an_p * 100, sp * 100);
        char msg[96];
        snprintf(msg, sizeof msg, "%s: simulated edges within 0.15%% of analytic", m[i].nm);
        check(fabs(sn - an_n) <= 0.0015 && fabs(sp - an_p) <= 0.0015, msg);
    }
    printf("  combined budget 16x center = %.2f%% total span; practical rule: each side within +/-2%%\n",
           (lim_pos(16, 0) - lim_neg(16, 0)) * 100);
}

int main(int argc, char **argv) {
    const char *cases = argc > 1 ? argv[1] : "divisor_cases.txt";
    if (part1(cases) != 0) return 2;
    part3();
    part4();
    printf("\nSUMMARY: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
