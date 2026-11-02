/*
 * i2c_capture_decode.c -- N07B: decode a 2-threshold I2C capture and diagnose it.
 *
 * Input (one file per case, see gen_captures.py):
 *   "# mode: sm|fm|fmp", "# expect: <DIAG>", optional "# expect_read: 0xNN",
 *   optional "# ctrl_timeout_us: N", then rows "<t_ns> <scl> <sda>" where each line state is
 *   0 (< 0.3 VDD), m (between) or 1 (> 0.7 VDD).  Rows are emitted only on change.
 *
 * What it does
 *   1. per line: logical level with hysteresis (Schmitt: 'm' keeps the previous level),
 *      30 % / 70 % crossing times for every edge -> tr, tf.
 *   2. glitch filter: a logical pulse narrower than glitch_thr(mode) is removed from the
 *      protocol stream and reported with its width at the 30 % (low pulse) level;
 *      >= tSP 50 ns means a spec-compliant receiver (UM10204 Table 10) would count it.
 *   3. protocol FSM: START / Sr / STOP, bits sampled at SCL rise, committed at SCL fall,
 *      9th bit = ACK/NACK, address+R/W, data bytes; abort mid-byte; end-of-capture stuck lines.
 *   4. timing vs UM10204 Rev.7 Table 11: fSCL, tLOW, tHIGH, tr, tf, tSU;DAT, tHD;STA, tSU;STA,
 *      tSU;STO, tBUF; plus "SDA indeterminate (30-70 %) when SCL crossed 30 % going high".
 *   5. clock stretching: SCL low > 4 x tLOW(min) -> stretch; compared with the controller
 *      timeout from the header and SMBus tTIMEOUT(min) 25 ms (SMBus 3.1 Table 2).
 *   6. diagnosis by fixed priority, PASS/FAIL against "# expect".
 *
 * Measurement convention (model assumption, close to UM10204 Fig. 38):
 *   tLOW  = SCL fall 30 % -> next rise 30 %      tHIGH  = SCL rise 70 % -> fall 70 %
 *   tSU;DAT = SDA reaches its new level -> SCL rise 30 %
 *   tHD;STA = SDA fall 30 % (START) -> SCL fall 70 %    tSU;STA = SCL rise 70 % -> SDA fall 70 %
 *   tSU;STO = SCL rise 70 % -> SDA rise 30 %            tBUF  = SDA rise 70 % (STOP) -> SDA fall 70 %
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_capture_decode.c
 * Usage: ./a.out cap_*.txt        (exit status = number of FAIL cases)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXR 8192
#define MAXE 4096

enum { SCL = 0, SDA = 1 };

typedef struct {
    const char *name;
    double fmax_khz;
    long thdsta, tlow, thigh, tsusta, tsudat, tr, tf, tsusto, tbuf; /* ns */
    long glitch_thr;                                              /* ns, model choice */
} i2c_mode_t;

/* NXP UM10204 Rev.7.0 Table 11 (min unless tr/tf = max). glitch_thr = tHIGH(min)/4. */
static const i2c_mode_t MODES[] = {
    {"sm",  100.0, 4000, 4700, 4000, 4700, 250, 1000, 300, 4000, 4700, 1000},
    {"fm",  400.0,  600, 1300,  600,  600, 100,  300, 300,  600, 1300,  150},
    {"fmp", 1000.0, 260,  500,  260,  260,  50,  120, 120,  260,  500,   65},
};
#define TSP_NS 50L          /* UM10204 Table 10: spikes < 50 ns must be suppressed (Fm, Fm+) */
#define SMBUS_TTIMEOUT_MIN_NS 25000000L

typedef struct { long t; char s; } raw_t;
typedef struct { long t30, t70; int lvl; int line; } ev_t;   /* logical edge */

static raw_t raw[2][MAXR];
static int nraw[2];
static ev_t lev[2][MAXE];
static int nlev[2];
static ev_t mev[2 * MAXE];
static int nmev;
static long t_end;

/* ---- diagnosis flags ------------------------------------------------------ */
enum {
    F_ARB_LOST_GLITCH, F_STRETCH_TIMEOUT, F_SLOW_RISE, F_WRITE_STOP_READ, F_STUCK_SDA,
    F_STUCK_SCL, F_NACK_ADDR, F_SCL_GLITCH, F_SDA_GLITCH, F_ABORT_MIDBYTE, F_TIMING, F_READ_MISMATCH,
    F_COUNT
};
static const char *FLAG_NAME[F_COUNT] = {
    "ARB_LOST_GLITCH", "STRETCH_TIMEOUT", "SLOW_RISE", "WRITE_STOP_READ", "STUCK_SDA",
    "STUCK_SCL", "NACK_ADDR", "SCL_GLITCH", "SDA_GLITCH", "ABORT_MIDBYTE", "TIMING", "READ_MISMATCH",
};
static int flag[F_COUNT];

static char lvl_of(char s, int prev) { return s == '1' ? 1 : s == '0' ? 0 : (char)prev; }

static char raw_state(int line, long t)
{
    char s = raw[line][0].s;
    for (int i = 0; i < nraw[line] && raw[line][i].t <= t; i++) s = raw[line][i].s;
    return s;
}

static long ev_time(const ev_t *e) { return e->lvl ? e->t70 : e->t30; } /* logical time */

static int cmp_ev(const void *a, const void *b)
{
    long ta = ev_time(a), tb = ev_time(b);
    if (ta != tb) return ta < tb ? -1 : 1;
    return ((const ev_t *)a)->line - ((const ev_t *)b)->line;
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

/* timing statistics */
typedef struct { long min, max; int n, viol; } stat_t;
static void st_init(stat_t *s) { s->min = 1L << 60; s->max = -(1L << 60); s->n = 0; s->viol = 0; }
static void st_add(stat_t *s, long v, int bad)
{
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->n++;
    if (bad) s->viol++;
}

static int decode_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    char line[256], mode_s[16] = "fm", expect[48] = "OK", ccase[160] = "";
    long ctrl_timeout_ns = -1;
    int expect_read = -1;
    memset(nraw, 0, sizeof nraw);
    memset(flag, 0, sizeof flag);
    t_end = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            char *v;
            line[strcspn(line, "\n")] = 0;
            if ((v = strstr(line, "mode: "))) snprintf(mode_s, sizeof mode_s, "%s", v + 6);
            else if ((v = strstr(line, "expect_read: "))) expect_read = (int)strtol(v + 13, NULL, 16);
            else if ((v = strstr(line, "expect: "))) snprintf(expect, sizeof expect, "%s", v + 8);
            else if ((v = strstr(line, "ctrl_timeout_us: "))) ctrl_timeout_ns = atol(v + 17) * 1000L;
            else if ((v = strstr(line, "case: "))) snprintf(ccase, sizeof ccase, "%s", v + 6);
            continue;
        }
        long t; char a, b;
        if (sscanf(line, "%ld %c %c", &t, &a, &b) != 3) continue;
        char st[2] = {a, b};
        for (int L = 0; L < 2; L++)
            if (nraw[L] == 0 || raw[L][nraw[L] - 1].s != st[L]) {
                if (nraw[L] >= MAXR) { fclose(f); printf("too many rows\n"); return 1; }
                raw[L][nraw[L]++] = (raw_t){t, st[L]};
            }
        t_end = t;
    }
    fclose(f);

    const i2c_mode_t *M = NULL;
    for (size_t i = 0; i < sizeof MODES / sizeof MODES[0]; i++)
        if (!strcmp(MODES[i].name, mode_s)) M = &MODES[i];
    if (!M || nraw[0] == 0) { printf("bad header/data in %s\n", path); return 1; }

    const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    printf("== %s  [mode %s, %.0f kHz max]\n   case: %s\n", base, M->name, M->fmax_khz, ccase);

    /* 1. logical edges with 30/70 % crossing times */
    stat_t s_tr, s_tf;
    st_init(&s_tr); st_init(&s_tf);
    for (int L = 0; L < 2; L++) {
        nlev[L] = 0;
        int lv = lvl_of(raw[L][0].s, 1);
        for (int i = 1; i < nraw[L]; i++) {
            int nl = lvl_of(raw[L][i].s, lv);
            if (nl == lv) continue;
            long tm = (raw[L][i - 1].s == 'm') ? raw[L][i - 1].t : raw[L][i].t; /* entered 'm' */
            ev_t e = {0, 0, nl, L};
            if (nl) { e.t30 = tm; e.t70 = raw[L][i].t; }
            else    { e.t70 = tm; e.t30 = raw[L][i].t; }
            if (nlev[L] < MAXE) lev[L][nlev[L]++] = e;
            lv = nl;
        }
    }

    /* 2. glitch filter */
    nmev = 0;
    for (int L = 0; L < 2; L++) {
        for (int i = 0; i < nlev[L]; i++) {
            if (i + 1 < nlev[L] && ev_time(&lev[L][i + 1]) - ev_time(&lev[L][i]) < M->glitch_thr) {
                const ev_t *a = &lev[L][i], *b = &lev[L][i + 1];
                long w = a->lvl == 0 ? b->t30 - a->t30 : b->t70 - a->t70; /* below 30 % / above 70 % */
                int counts = w >= TSP_NS;
                printf("   glitch: %s %s pulse %ld ns @ t=%ld ns -> %s\n", L == SCL ? "SCL" : "SDA",
                       a->lvl == 0 ? "low" : "high", w, a->t70,
                       counts ? ">= tSP 50 ns: spec input filters pass it, receivers COUNT it"
                              : "< tSP 50 ns: filtered by compliant inputs");
                if (counts) flag[L == SCL ? F_SCL_GLITCH : F_SDA_GLITCH] = 1;
                i++;
                continue;
            }
            mev[nmev++] = lev[L][i];
            const ev_t *e = &lev[L][i];
            if (e->lvl) st_add(&s_tr, e->t70 - e->t30, e->t70 - e->t30 > M->tr);
            else        st_add(&s_tf, e->t30 - e->t70, e->t30 - e->t70 > M->tf);
        }
    }
    qsort(mev, (size_t)nmev, sizeof mev[0], cmp_ev);

    /* 3+4+5. protocol FSM with timing */
    stat_t s_tlow, s_thigh, s_tsudat, s_thdsta, s_tsusta, s_tsusto, s_tbuf;
    st_init(&s_tlow); st_init(&s_thigh); st_init(&s_tsudat); st_init(&s_thdsta);
    st_init(&s_tsusta); st_init(&s_tsusto); st_init(&s_tbuf);
    static long periods[MAXE];
    int nper = 0;
    int scl = lvl_of(raw[SCL][0].s, 1), sda = lvl_of(raw[SDA][0].s, 1);
    int intxn = 0, bits = 0, bytei = 0, byteval = 0, indet_mask = 0, n_indet = 0;
    int pending = 0, pend_v = 0, pend_indet = 0, pend_su_ok = 1;
    long pend_su = 0;
    int have_fall = 0, have_rise = 0, have_stop = 0, first_fall_after_start = 0;
    ev_t lastfall = {0}, lastrise = {0}, laststop = {0}, startev = {0};
    long last_sda_t = -1, last_scl_change = 0, last_sda_change = 0;
    long stretch_max = 0; char stretch_where[96] = "";
    int seg_addr = -1, seg_rw = 0, seg_ndata = 0;
    int prev_seg_addr = -1, prev_seg_rw = 0, prev_seg_ndata = 0; char prev_seg_end = 0;
    int last_read_val = -1;
    char tok[512] = "";
    long stretch_thr = 4 * M->tlow;
#define TOK(...) do { size_t _l = strlen(tok); snprintf(tok + _l, sizeof tok - _l, __VA_ARGS__); } while (0)

    for (int k = 0; k < nmev; k++) {
        const ev_t *e = &mev[k];
        if (e->line == SCL) {
            last_scl_change = ev_time(e);
            if (e->lvl) {                                         /* SCL rise */
                if (have_fall) {
                    long tl = e->t30 - lastfall.t30;
                    if (tl > stretch_thr) {
                        if (tl > stretch_max) {
                            stretch_max = tl;
                            snprintf(stretch_where, sizeof stretch_where, "before bit %d of byte %d", bits + 1, bytei + 1);
                        }
                    } else st_add(&s_tlow, tl, tl < M->tlow);
                }
                if (have_rise && intxn && bits > 0 && e->t30 - lastfall.t30 <= stretch_thr && nper < MAXE)
                    periods[nper++] = e->t70 - lastrise.t70;
                if (intxn) {
                    pending = 1;
                    pend_v = sda;
                    pend_indet = raw_state(SDA, e->t30) == 'm';
                    pend_su_ok = !(last_sda_t > lastfall.t30 && have_fall);
                    pend_su = pend_su_ok ? 0 : e->t30 - last_sda_t;
                }
                lastrise = *e; have_rise = 1; scl = 1;
            } else {                                              /* SCL fall */
                if (have_rise) st_add(&s_thigh, e->t70 - lastrise.t70, e->t70 - lastrise.t70 < M->thigh);
                if (first_fall_after_start) {
                    long h = e->t70 - startev.t30;
                    st_add(&s_thdsta, h, h < M->thdsta);
                    first_fall_after_start = 0;
                }
                if (intxn && pending) {                           /* commit the sampled bit */
                    pending = 0;
                    if (!pend_su_ok) st_add(&s_tsudat, pend_su, pend_su < M->tsudat);
                    bits++;
                    if (bits <= 8) {
                        byteval = (byteval << 1) | pend_v;
                        if (pend_indet) { indet_mask |= 1 << (8 - bits); n_indet++; }
                    } else {                                      /* ACK slot */
                        int ack = !pend_v;
                        if (bytei == 0) {
                            seg_addr = byteval >> 1; seg_rw = byteval & 1; seg_ndata = 0;
                            TOK(" %02X%c %s", seg_addr, seg_rw ? 'R' : 'W', ack ? "A" : "N");
                            if (!ack) flag[F_NACK_ADDR] = 1;
                        } else {
                            TOK(" %02X%s %s", byteval, indet_mask ? "?" : "", ack ? "A" : "N");
                            if (indet_mask) TOK("(indeterminate bits mask 0x%02X)", indet_mask);
                            seg_ndata++;
                            if (seg_rw) last_read_val = byteval;
                        }
                        bytei++; bits = 0; byteval = 0; indet_mask = 0;
                    }
                }
                lastfall = *e; have_fall = 1; scl = 0;
            }
        } else {
            last_sda_change = ev_time(e);
            if (scl == 1) {
                if (e->lvl == 0) {                                /* START or Sr */
                    if (intxn) {
                        st_add(&s_tsusta, e->t70 - lastrise.t70, e->t70 - lastrise.t70 < M->tsusta);
                        if (bits) TOK(" <START after %d bits!>", bits);
                        TOK(" Sr");
                        prev_seg_addr = seg_addr; prev_seg_rw = seg_rw; prev_seg_ndata = seg_ndata; prev_seg_end = 'r';
                    } else {
                        if (have_stop) st_add(&s_tbuf, e->t70 - laststop.t70, e->t70 - laststop.t70 < M->tbuf);
                        TOK(" S");
                    }
                    intxn = 1; bits = 0; bytei = 0; byteval = 0; indet_mask = 0; pending = 0;
                    startev = *e; first_fall_after_start = 1;
                } else {                                          /* STOP */
                    st_add(&s_tsusto, e->t30 - lastrise.t70, e->t30 - lastrise.t70 < M->tsusto);
                    if (bits) { TOK(" <STOP after %d bits!>", bits); flag[F_ABORT_MIDBYTE] = 1; }
                    TOK(" P |");
                    prev_seg_addr = seg_addr; prev_seg_rw = seg_rw; prev_seg_ndata = seg_ndata; prev_seg_end = 'P';
                    intxn = 0; pending = 0; laststop = *e; have_stop = 1;
                }
            } else {
                last_sda_t = ev_time(e);
            }
            sda = e->lvl;
        }
        /* write-pointer, STOP, then read of the same address */
        if (bytei == 1 && bits == 0 && seg_rw == 1 && prev_seg_end == 'P' && prev_seg_rw == 0 &&
            prev_seg_ndata == 1 && prev_seg_addr == seg_addr) {
            flag[F_WRITE_STOP_READ] = 1;
            prev_seg_end = 0;
        }
    }
    if (intxn && (bits || pending)) {
        TOK(" <abandoned: byte %d after %d clocks, SDA=%d on last clock>", bytei + 1, bits + pending, pend_v);
        flag[F_ABORT_MIDBYTE] = 1;
    }
    long stuck_thr = 20L * M->tlow;
    if (sda == 0 && t_end - last_sda_change > stuck_thr) {
        flag[F_STUCK_SDA] = 1;
        TOK(" <SDA held low %.1f us at end, SCL=%d>", (t_end - last_sda_change) / 1000.0, scl);
    }
    if (scl == 0 && t_end - last_scl_change > stuck_thr) {
        flag[F_STUCK_SCL] = 1;
        TOK(" <SCL held low %.1f us at end>", (t_end - last_scl_change) / 1000.0);
    }
    printf("   decode:%s\n", tok);

    /* timing report */
    double fscl = 0;
    if (nper) {
        qsort(periods, (size_t)nper, sizeof periods[0], cmp_long);
        fscl = 1e6 / (double)periods[nper / 2];
    }
    int fscl_bad = fscl > M->fmax_khz * 1.0001;
    printf("   timing: fSCL(median) %.0f kHz%s | tLOW min %ld ns (>= %ld)%s | tHIGH min %ld ns (>= %ld)%s\n",
           fscl, fscl_bad ? " VIOL" : "", s_tlow.min, M->tlow, s_tlow.viol ? " VIOL" : "",
           s_thigh.min, M->thigh, s_thigh.viol ? " VIOL" : "");
    printf("           tr max %ld ns (<= %ld)%s x%d/%d | tf max %ld ns (<= %ld)%s",
           s_tr.max, M->tr, s_tr.viol ? " VIOL" : "", s_tr.viol, s_tr.n, s_tf.max, M->tf, s_tf.viol ? " VIOL" : "");
    if (s_tsudat.n) printf(" | tSU;DAT min %ld ns (>= %ld)%s x%d", s_tsudat.min, M->tsudat, s_tsudat.viol ? " VIOL" : "", s_tsudat.viol);
    printf("\n           tHD;STA min %ld (>= %ld)%s", s_thdsta.min, M->thdsta, s_thdsta.viol ? " VIOL" : "");
    if (s_tsusta.n) printf(" | tSU;STA min %ld (>= %ld)%s", s_tsusta.min, M->tsusta, s_tsusta.viol ? " VIOL" : "");
    if (s_tsusto.n) printf(" | tSU;STO min %ld (>= %ld)%s", s_tsusto.min, M->tsusto, s_tsusto.viol ? " VIOL" : "");
    if (s_tbuf.n) printf(" | tBUF min %ld (>= %ld)%s", s_tbuf.min, M->tbuf, s_tbuf.viol ? " VIOL" : "");
    printf("\n");
    if (n_indet) printf("   sample: %d bit(s) had SDA between 30-70 %% when SCL crossed 30 %% rising -> value depends on the receiver\n", n_indet);
    if (stretch_max) {
        printf("   stretch: longest SCL low %.3f ms %s", stretch_max / 1e6, stretch_where);
        if (ctrl_timeout_ns > 0) printf(" | controller timeout %.3f ms -> %s", ctrl_timeout_ns / 1e6,
                                        stretch_max > ctrl_timeout_ns ? "EXCEEDED" : "ok");
        printf(" | SMBus tTIMEOUT(min) 25 ms -> %s\n", stretch_max > SMBUS_TTIMEOUT_MIN_NS ? "EXCEEDED" : "within");
        if (ctrl_timeout_ns > 0 && stretch_max > ctrl_timeout_ns) flag[F_STRETCH_TIMEOUT] = 1;
    }
    if (s_tr.viol) flag[F_SLOW_RISE] = 1;
    if (fscl_bad || s_tlow.viol || s_thigh.viol || s_tf.viol || s_tsudat.viol || s_thdsta.viol ||
        s_tsusta.viol || s_tsusto.viol || s_tbuf.viol) flag[F_TIMING] = 1;
    if (flag[F_SCL_GLITCH] && flag[F_ABORT_MIDBYTE]) flag[F_ARB_LOST_GLITCH] = 1;
    if (expect_read >= 0 && last_read_val >= 0 && last_read_val != expect_read) {
        flag[F_READ_MISMATCH] = 1;
        printf("   read  : last read byte 0x%02X, expected 0x%02X\n", last_read_val, expect_read);
    }

    /* 6. diagnosis */
    const char *diag = "OK";
    int di = -1;
    for (int i = 0; i < F_COUNT; i++) if (flag[i]) { diag = FLAG_NAME[i]; di = i; break; }
    printf("   flags :");
    int any = 0;
    for (int i = 0; i < F_COUNT; i++) if (flag[i]) { printf(" %s", FLAG_NAME[i]); any = 1; }
    printf("%s\n", any ? "" : " (none)");
    static const char *HINT[F_COUNT] = {
        "SCL glitch >= tSP counted by the target -> bit-count slip -> target drives SDA early -> controller sees 0 while sending 1",
        "target stretches longer than the controller's SCL-low timeout -> controller aborts mid-byte -> bus left owned by target",
        "rise time over Table 11 limit -> fix Rp/Cb (or lower fSCL), measure at 30/70 %",
        "pointer write ended with STOP; device resets its pointer on STOP -> use repeated START (combined format)",
        "SDA low at idle -> bus clear (9 clocks + STOP)", "SCL low at idle -> target reset / power cycle",
        "address phase NACK", "SCL glitch", "SDA glitch", "transfer abandoned mid-byte", "timing violation",
        "read value mismatch",
    };
    printf("   diag  : %s%s%s\n", diag, di >= 0 ? " -- " : "", di >= 0 ? HINT[di] : "");
    int pass = !strcmp(diag, expect);
    printf("   %s (expect %s, got %s)\n\n", pass ? "PASS" : "FAIL", expect, diag);
    return !pass;
}

int main(int argc, char **argv)
{
    int fails = 0;
    for (int i = 1; i < argc; i++) fails += decode_file(argv[i]);
    printf("summary: %d case(s), %d FAIL\n", argc - 1, fails);
    return fails;
}
