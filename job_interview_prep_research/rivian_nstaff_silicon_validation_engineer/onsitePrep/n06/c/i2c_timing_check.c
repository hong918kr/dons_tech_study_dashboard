/*
 * i2c_timing_check.c -- N06C C1 practice: "does this I2C controller config meet UM10204 timing?"
 *
 * What it models (simplified, see assumptions):
 *   - Controller counts in ic_clk cycles: LCNT (SCL low), HCNT (SCL high), HDSTA (START hold),
 *     SUSTO (STOP setup), BUF (STOP->START idle), SDAHOLD (SDA change delay after SCL falls).
 *   - Clock synchronisation: after releasing SCL the controller waits until it SENSES SCL high
 *     (crossing 70% VDD) before it starts counting HCNT -- a common controller behaviour [assumption].
 *   - SCL/SDA fall: linear ramp, 70%->30% takes tf.  Rise: RC, 30%->70% takes tr (= 0.847*RC).
 *   - Measured values use the UM10204 Fig.38 convention: VIL=0.3VDD, VIH=0.7VDD reference levels.
 *     Exact threshold pairing per parameter is this model's assumption (documented below).
 *
 * Limits: NXP UM10204 Rev.7.0 (2021-10-01) Table 11 -- Sm / Fm / Fm+.
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_timing_check.c -lm
 */
#include <stdio.h>
#include <math.h>

typedef struct {
    const char *name;
    double fscl_max_khz;
    double thd_sta, tlow, thigh, tsu_sto, tbuf; /* minimums, ns */
    double tsu_dat;                              /* minimum, ns */
    double tr_max, tf_max;                       /* ns */
} mode_t;

/* UM10204 Rev.7 Table 11 (minimums unless noted) */
static const mode_t SM  = {"Sm 100k",  100.0, 4000, 4700, 4000, 4000, 4700, 250, 1000, 300};
static const mode_t FM  = {"Fm 400k",  400.0,  600, 1300,  600,  600, 1300, 100,  300, 300};
static const mode_t FMP = {"Fm+ 1M",  1000.0,  260,  500,  260,  260,  500,  50,  120, 120};

typedef struct {
    const char *name;
    const mode_t *mode;
    double clk_mhz;
    int lcnt, hcnt, hdsta, susto, buf, sdahold;
} cfg_t;

/* ramp offsets (fraction of tr / tf) */
#define FALL_70 0.75   /* linear fall 100%->0% over 2.5*tf: 70% crossed at 0.75 tf          */
#define FALL_30 1.75   /*                                    30% crossed at 1.75 tf          */
#define RISE_30 0.421  /* RC rise: 30% at RC*ln(1/0.7)=0.357RC = 0.421 tr (tr=0.847RC)     */
#define RISE_70 1.421  /*          70% at RC*ln(1/0.3)=1.204RC = 1.421 tr                   */

typedef struct { double fscl_khz, tlow, thigh, thd_sta, tsu_sto, tbuf, tsu_dat, thd_dat; } meas_t;

static meas_t measure(const cfg_t *c, double tr, double tf)
{
    double T = 1000.0 / c->clk_mhz; /* ns per count */
    meas_t m;
    /* SCL low: 30% on the falling edge -> 30% on the rising edge */
    m.tlow  = c->lcnt * T + RISE_30 * tr - FALL_30 * tf;
    /* SCL high: counting starts when SCL sensed at 70%; ends at 70% of the next fall */
    m.thigh = c->hcnt * T + FALL_70 * tf;
    /* period = drive-low + time to reach 70% + high count */
    double period = c->lcnt * T + RISE_70 * tr + c->hcnt * T;
    m.fscl_khz = 1e6 / period;
    /* START hold: SDA 30% (falling) -> SCL 70% (falling)           [threshold pairing assumed] */
    m.thd_sta = c->hdsta * T + FALL_70 * tf - FALL_30 * tf;
    /* STOP setup: SCL sensed 70% (rising) -> SDA 30% (rising)       [assumed] */
    m.tsu_sto = c->susto * T + RISE_30 * tr;
    /* bus free: count starts at SDA release; SDA 70% (rising) -> SDA 70% (next START falling) */
    m.tbuf    = c->buf * T - RISE_70 * tr + FALL_70 * tf;
    /* data setup, worst case = SDA rising (slow): SDA 70% -> SCL 30% rising */
    m.tsu_dat = (c->lcnt - c->sdahold) * T + RISE_30 * tr - RISE_70 * tr;
    /* data hold: SCL 30% (falling) -> SDA first leaves the valid band (70% on a fall) */
    m.thd_dat = c->sdahold * T + FALL_70 * tf - FALL_30 * tf;
    return m;
}

static int chk(const char *label, double v, double lim, int is_max, char *buf, size_t cap, int *fails)
{
    int ok = is_max ? (v <= lim) : (v >= lim);
    if (!ok) { (*fails)++; }
    return snprintf(buf, cap, " %s=%.0f%s", label, v, ok ? "" : "!");
}

static int run_cfg(const cfg_t *c, const double *trs, int ntr, const double *tfs, int ntf)
{
    const mode_t *md = c->mode;
    int total_fail_points = 0;
    printf("\n== %s  [%s, ic_clk %.0f MHz, LCNT=%d HCNT=%d HDSTA=%d SUSTO=%d BUF=%d SDAHOLD=%d]\n",
           c->name, md->name, c->clk_mhz, c->lcnt, c->hcnt, c->hdsta, c->susto, c->buf, c->sdahold);
    printf("   limits(ns): fSCL<=%.0fk tLOW>=%.0f tHIGH>=%.0f tHD;STA>=%.0f tSU;STO>=%.0f tBUF>=%.0f tSU;DAT>=%.0f tHD;DAT>=0\n",
           md->fscl_max_khz, md->tlow, md->thigh, md->thd_sta, md->tsu_sto, md->tbuf, md->tsu_dat);
    for (int i = 0; i < ntr; i++) {
        for (int j = 0; j < ntf; j++) {
            double tr = trs[i], tf = tfs[j];
            if (tr > md->tr_max || tf > md->tf_max) continue; /* outside the bus spec: not a controller test point */
            meas_t m = measure(c, tr, tf);
            char line[512]; int n = 0, fails = 0;
            n += chk("fSCL(k)", m.fscl_khz, md->fscl_max_khz, 1, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tLOW", m.tlow, md->tlow, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tHIGH", m.thigh, md->thigh, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tHD;STA", m.thd_sta, md->thd_sta, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tSU;STO", m.tsu_sto, md->tsu_sto, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tBUF", m.tbuf, md->tbuf, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tSU;DAT", m.tsu_dat, md->tsu_dat, 0, line + n, sizeof line - (size_t)n, &fails);
            n += chk("tHD;DAT", m.thd_dat, 0.0, 0, line + n, sizeof line - (size_t)n, &fails);
            (void)n;
            printf("   tr=%4.0f tf=%3.0f |%s  -> %s\n", tr, tf, line, fails ? "FAIL" : "ok");
            if (fails) total_fail_points++;
        }
    }
    return total_fail_points;
}

int main(void)
{
    const double trs[] = {20, 100, 300, 1000};
    const double tfs[] = {10, 120, 300};

    /* 100 MHz ic_clk -> 10 ns per count. Expected verdict in the comment; the program checks it. */
    const cfg_t cfgs[] = {
        /* naive 50% duty at 400k: 125+125 counts = 2.5 us nominal */
        {"F1 Fm naive 50/50 duty",            &FM, 100, 125, 125,  60,  60, 130, 30},
        /* tLOW/tHIGH = spec mins exactly, no margin for edges */
        {"F2 Fm spec-min counts (130/60)",    &FM, 100, 130,  60,  60,  60, 130, 30},
        /* sized for the whole legal edge envelope (tr 20..300, tf 10..300):
           L+H >= 248 (fSCL at fastest tr), L >= 182 (tLOW at tf=300), BUF >= 172 (tBUF at tr=300) */
        {"F3 Fm sized for edge envelope",      &FM, 100, 185,  65,  95,  65, 175, 35},
        /* Fm+ with L+H = 100 counts but tLOW only 600 ns: dies when tf grows */
        {"P1 Fm+ (60/40)",                    &FMP,100,  60,  40,  30,  30,  60, 10},
        /* Fm+ derived the same way: L>=71, H>=26, L+H>=98, HDSTA>=38, BUF>=67, HOLD>=12 */
        {"P2 Fm+ derived (71/27)",            &FMP,100,  71,  27,  38,  26,  67, 13},
        /* Sm with the same derivation (L+H>=998, L>=522, H>=400, BUF>=612) but SDA hold = 0 */
        {"S1 Sm SDAHOLD=0 (hold bug)",        &SM, 100, 540, 460, 430, 400, 615,  0},
        {"S2 Sm derived, SDAHOLD=30",         &SM, 100, 540, 460, 430, 400, 615, 30},
    };
    const int expect_fail[] = {1, 1, 0, 1, 0, 1, 0}; /* 1 = at least one tr/tf point must FAIL */
    int ncfg = (int)(sizeof cfgs / sizeof cfgs[0]);
    int bad = 0;

    for (int k = 0; k < ncfg; k++) {
        int fp = run_cfg(&cfgs[k], trs, 4, tfs, 3);
        int got_fail = fp > 0;
        int ok = (got_fail == expect_fail[k]);
        printf("RESULT %-36s failing_points=%d expected_%s -> %s\n", cfgs[k].name, fp,
               expect_fail[k] ? "FAIL" : "PASS", ok ? "PASS" : "MISMATCH");
        if (!ok) bad++;
    }
    printf("\nSUMMARY %d/%d configs matched expectation\n", ncfg - bad, ncfg);
    return bad ? 1 : 0;
}
