/*
 * N07C - SPI mode-mismatch + sample-delay simulator (self-checking).
 *
 * Part 1: master mode (0..3) x slave mode (0..3) = 16 combos.
 *         A generic shift-register slave; both directions (MOSI, MISO).
 *         Two independent implementations (edge-by-edge event loop vs.
 *         closed-form "count launches before each sample") must agree,
 *         plus hand-derived golden bytes for named combos.
 * Part 2: master mode x SPI-NOR flash model (samples rising, launches
 *         falling, answers only a correctly received 0x9F) -> JEDEC ID.
 * Part 3: timing model of a JEDEC ID read: SCLK frequency x RX sample
 *         delay x PVT corner -> shmoo, plus the "half-clock" (sample on
 *         the opposite edge) column. Simulated pass/fail must match the
 *         closed-form window, and named cells must match hand expectations.
 *
 * Model assumptions (see N07C note section 8):
 *  - Ideal edges; at one edge, receivers sample BEFORE senders launch
 *    (= hold time met, the normal flip-flop behaviour).
 *  - MSB first. MOSI idles low before the master drives it; MISO idles
 *    high (pull-up / hi-Z) before the slave drives it.
 *  - Timing numbers: W25Q128JV tCLQV max 6 ns, tCLQX min 1.5 ns (datasheet);
 *    board+pad one-way delay 1.0 ns and controller setup/hold 1.0 ns are
 *    assumptions, not from any datasheet.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { RISE = 0, FALL = 1 };
#define MAXB 8
#define MISO_IDLE 1
#define MOSI_IDLE 0

static int g_fail = 0;
static void check(int ok, const char *what) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static int cpol(int m) { return (m >> 1) & 1; }
static int cpha(int m) { return m & 1; }
/* modes 0 and 3 sample on rising, 1 and 2 on falling */
static int samp_edge(int m) { return (cpol(m) ^ cpha(m)) ? FALL : RISE; }
static int launch_edge(int m) { return samp_edge(m) == RISE ? FALL : RISE; }
/* edge e (1-based) type for a clock that idles at level idle */
static int edge_type(int idle, int e) {
    int odd = e & 1;
    if (idle == 0) return odd ? RISE : FALL;
    return odd ? FALL : RISE;
}
static int bit_of(const uint8_t *b, int i) { return (b[i / 8] >> (7 - i % 8)) & 1; }
static void put_bit(uint8_t *b, int i, int v) {
    if (v) b[i / 8] |= (uint8_t)(1u << (7 - i % 8));
}
static void hex(const uint8_t *b, int n, char *out) { /* out must hold 3*n bytes */
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(out);
        snprintf(out + len, 4, "%s%02X", i ? " " : "", b[i]);
    }
}

/* ---------- implementation A: edge-by-edge event loop ---------- */
typedef struct { int mode, next, level, idle; const uint8_t *tx; int nbits; } Sender;
static void sender_cs(Sender *s) {
    if (cpha(s->mode) == 0) { s->level = bit_of(s->tx, 0); s->next = 1; }
    else { s->level = s->idle; s->next = 0; }
}
static void sender_edge(Sender *s, int type) {
    if (type != launch_edge(s->mode)) return;
    s->level = (s->next < s->nbits) ? bit_of(s->tx, s->next) : s->idle;
    s->next++;
}

static void sim_event(int mm, int sm, const uint8_t *mtx, const uint8_t *stx, int nbytes,
                      uint8_t *mrx, uint8_t *srx) {
    int nbits = nbytes * 8, idle = cpol(mm), mi = 0, si = 0;
    Sender mosi = { mm, 0, 0, MOSI_IDLE, mtx, nbits };
    Sender miso = { sm, 0, 0, MISO_IDLE, stx, nbits };
    memset(mrx, 0, (size_t)nbytes); memset(srx, 0, (size_t)nbytes);
    sender_cs(&mosi); sender_cs(&miso);
    for (int e = 1; e <= 2 * nbits; e++) {
        int t = edge_type(idle, e);
        /* sample first (old values), then launch */
        if (t == samp_edge(mm) && mi < nbits) put_bit(mrx, mi++, miso.level);
        if (t == samp_edge(sm) && si < nbits) put_bit(srx, si++, mosi.level);
        sender_edge(&mosi, t); sender_edge(&miso, t);
    }
}

/* ---------- implementation B: closed form ---------- */
static void sim_count(int sender_mode, int recv_mode, int clk_idle, const uint8_t *tx,
                      int nbytes, int line_idle, uint8_t *rx) {
    int nbits = nbytes * 8, j = 0, launches_before = cpha(sender_mode) == 0 ? 1 : 0;
    memset(rx, 0, (size_t)nbytes);
    for (int e = 1; e <= 2 * nbits && j < nbits; e++) {
        int t = edge_type(clk_idle, e);
        if (t == samp_edge(recv_mode)) {
            int L = launches_before, v;
            if (L == 0 || L - 1 >= nbits) v = line_idle; else v = bit_of(tx, L - 1);
            put_bit(rx, j++, v);
        }
        if (t == launch_edge(sender_mode)) launches_before++;
    }
}

static const char *classify(const uint8_t *got, const uint8_t *exp, int n, int idle) {
    static char buf[32];
    if (!memcmp(got, exp, (size_t)n)) return "OK";
    /* right shift by one: got bit i = exp bit i-1, bit 0 = idle */
    int nb = n * 8, rs = bit_of(got, 0) == idle, ls = 1;
    for (int i = 1; i < nb && rs; i++) rs = bit_of(got, i) == bit_of(exp, i - 1);
    for (int i = 0; i < nb - 1 && ls; i++) ls = bit_of(got, i) == bit_of(exp, i + 1);
    snprintf(buf, sizeof buf, "%s", rs ? "SHIFT>>1" : ls ? "SHIFT<<1" : "GARBLED");
    return buf;
}

static void part1(void) {
    const uint8_t mtx[3] = { 0x9F, 0xA5, 0x3C }, stx[3] = { 0xEF, 0x40, 0x18 };
    uint8_t mrx[3], srx[3], b1[3], b2[3];
    char h1[16], h2[16];
    int agree = 1;
    printf("=== Part 1: master mode x slave mode (generic shift-register slave) ===\n");
    printf("master sends 9F A5 3C on MOSI, slave sends EF 40 18 on MISO, CS held for 3 bytes\n");
    printf("same-edge = receiver samples on the edge where the sender launches (hold-margin race on real HW)\n\n");
    printf("M S | master got (MISO)  | slave got (MOSI)   | same-edge (race on real HW)\n");
    for (int m = 0; m < 4; m++) {
        for (int s = 0; s < 4; s++) {
            sim_event(m, s, mtx, stx, 3, mrx, srx);
            sim_count(s, m, cpol(m), stx, 3, MISO_IDLE, b1);
            sim_count(m, s, cpol(m), mtx, 3, MOSI_IDLE, b2);
            if (memcmp(mrx, b1, 3) || memcmp(srx, b2, 3)) agree = 0;
            hex(mrx, 3, h1); hex(srx, 3, h2);
            char note[64] = "";
            int a = samp_edge(m) == launch_edge(s), b = samp_edge(s) == launch_edge(m);
            snprintf(note, sizeof note, "%s", a && b ? "MISO+MOSI" : a ? "MISO" : b ? "MOSI" : "-");
            printf("%d %d | %s %-9s | %s %-9s | %s\n", m, s, h1, classify(mrx, stx, 3, MISO_IDLE),
                   h2, classify(srx, mtx, 3, MOSI_IDLE), note);
        }
    }
    printf("\n");
    check(agree, "part1: event-loop sim == closed-form count for all 16 combos x 2 directions");
    /* hand-derived golden cases (see note 8.3) */
    struct { int m, s; uint8_t mrx[3], srx[3]; const char *why; } g[] = {
        { 0, 0, { 0xEF, 0x40, 0x18 }, { 0x9F, 0xA5, 0x3C }, "m0/s0 matched -> exact" },
        { 3, 3, { 0xEF, 0x40, 0x18 }, { 0x9F, 0xA5, 0x3C }, "m3/s3 matched -> exact" },
        { 0, 3, { 0xF7, 0xA0, 0x0C }, { 0x9F, 0xA5, 0x3C }, "m0/s3: slave (CPHA=1) never pre-drives bit7 -> MISO >>1, MOSI ok" },
        { 0, 1, { 0xF7, 0xA0, 0x0C }, { 0x9F, 0xA5, 0x3C }, "m0/s1: MOSI ok only because of same-edge old value; MISO >>1" },
        { 1, 0, { 0xEF, 0x40, 0x18 }, { 0x4F, 0xD2, 0x9E }, "m1/s0: MISO ok (same-edge), MOSI >>1 with idle-low MSB" },
    };
    for (size_t i = 0; i < sizeof g / sizeof g[0]; i++) {
        sim_event(g[i].m, g[i].s, mtx, stx, 3, mrx, srx);
        char what[160];
        snprintf(what, sizeof what, "part1 golden %s", g[i].why);
        check(!memcmp(mrx, g[i].mrx, 3) && !memcmp(srx, g[i].srx, 3), what);
    }
    int diag_ok = 1;
    for (int m = 0; m < 4; m++) {
        sim_event(m, m, mtx, stx, 3, mrx, srx);
        if (memcmp(mrx, stx, 3) || memcmp(srx, mtx, 3)) diag_ok = 0;
    }
    check(diag_ok, "part1: every matched mode (diagonal) is exact in both directions");
}

/* ---------- Part 2: SPI-NOR flash model, JEDEC ID (9Fh) ---------- */
static void flash_jedec(int mm, uint8_t *id, uint8_t *cmd_seen) {
    const uint8_t mtx[4] = { 0x9F, 0, 0, 0 }, rom[3] = { 0xEF, 0x40, 0x18 };
    int nbits = 32, idle = cpol(mm), mi = 0, fsamp = 0, fout = 0, miso = MISO_IDLE;
    uint8_t mrx[4] = { 0 };
    Sender mosi = { mm, 0, 0, MOSI_IDLE, mtx, nbits };
    sender_cs(&mosi);
    *cmd_seen = 0;
    for (int e = 1; e <= 2 * nbits; e++) {
        int t = edge_type(idle, e);
        if (t == samp_edge(mm) && mi < nbits) put_bit(mrx, mi++, miso);
        if (t == RISE && fsamp < 8) { /* flash samples MOSI on rising */
            if (mosi.level) *cmd_seen |= (uint8_t)(1u << (7 - fsamp));
            fsamp++;
        }
        sender_edge(&mosi, t);
        if (t == FALL && fsamp == 8 && *cmd_seen == 0x9F) /* flash launches on falling */
            miso = fout < 24 ? bit_of(rom, fout++) : MISO_IDLE;
    }
    memcpy(id, mrx + 1, 3);
}

static void part2(void) {
    const uint8_t exp[4][3] = { { 0xEF, 0x40, 0x18 }, { 0xFF, 0xFF, 0xFF },
                                { 0xF7, 0xA0, 0x0C }, { 0xEF, 0x40, 0x18 } };
    const uint8_t exp_cmd[4] = { 0x9F, 0x4F, 0x9F, 0x9F };
    printf("\n=== Part 2: master mode x SPI-NOR flash (mode 0/3 device) - JEDEC ID read 9Fh ===\n");
    printf("flash: samples MOSI on rising, drives MISO on falling, answers only if it received 9F\n\n");
    printf("master mode | flash saw cmd | JEDEC ID read | reading\n");
    for (int m = 0; m < 4; m++) {
        uint8_t id[3], cmd; char h[16];
        flash_jedec(m, id, &cmd);
        hex(id, 3, h);
        const char *r = !memcmp(id, exp[0], 3) ? "OK (Winbond W25Q128JV)"
                        : !memcmp(id, exp[1], 3) ? "MISO never driven: command not recognised"
                                                 : "ID shifted right by 1 bit";
        printf("     %d      |      %02X       |   %s    | %s\n", m, cmd, h, r);
        char what[96];
        snprintf(what, sizeof what, "part2 mode %d: cmd=%02X id=%02X %02X %02X", m, exp_cmd[m],
                 exp[m][0], exp[m][1], exp[m][2]);
        check(cmd == exp_cmd[m] && !memcmp(id, exp[m], 3), what);
    }
}

/* ---------- Part 3: timing - frequency x sample delay x corner ---------- */
/* all times in picoseconds (integers, no float equality problems) */
#define TF 1000    /* one-way board + pad delay [assumption] */
#define TSU 1000   /* controller input setup [assumption] */
#define TH 1000    /* controller input hold  [assumption] */
static const int corner_tact[3] = { 1500, 3500, 6000 }; /* fast = tCLQX min, typ [assumed], slow = tCLQV max */
static uint32_t lcg = 1;
static int rnd_bit(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 16) & 1; }

/* returns number of setup/hold violations; fills got[3] */
static int read_id_timed(int T, int d, int tact, uint8_t *got) {
    const uint8_t rom[3] = { 0xEF, 0x40, 0x18 };
    int viol = 0;
    memset(got, 0, 3);
    for (int k = 0; k < 24; k++) {
        long ts = (long)k * T + d;           /* master sample instant */
        int v = MISO_IDLE, bad = 0, j;
        for (j = 0; j < 24; j++) {           /* transition of bit j at master pin */
            long sj = (long)j * T - T / 2 + 2 * TF + tact;
            if (sj > ts - TSU && sj < ts + TH) bad = 1;
            if (sj <= ts) v = bit_of(rom, j);
        }
        if (bad) { viol++; v = rnd_bit(); }  /* metastable: either value */
        put_bit(got, k, v);
    }
    return viol;
}
static int window_pass(int T, int d, int tact) { /* closed form */
    return d >= -T / 2 + 2 * TF + tact + TSU && d <= T / 2 + 2 * TF + tact - TH;
}

static void part3(void) {
    const int periods[6] = { 40000, 20000, 15000, 12500, 10000, 7500 };
    const char *fname[6] = { " 25.0", " 50.0", " 66.7", " 80.0", "100.0", "133.3" };
    const uint8_t exp[3] = { 0xEF, 0x40, 0x18 };
    int agree = 1;
    printf("\n=== Part 3: JEDEC ID read timing - SCLK x RX sample delay x PVT corner ===\n");
    printf("model: flash drives MISO at falling edge + tCLQV(actual); round trip adds 2 x %d ps;\n", TF);
    printf("master samples at rising edge + delay d; needs %d ps setup / %d ps hold\n", TSU, TH);
    printf("corners: fast tact=%d ps, typ %d ps, slow %d ps (W25Q128JV tCLQX min 1.5 ns, tCLQV max 6 ns)\n",
           corner_tact[0], corner_tact[1], corner_tact[2]);
    printf("cell: '#' = all 3 corners pass, 't' = typ only, 'f' = fast only, 'T' = typ+fast, '.' = fail\n");
    printf("column 'h' = half-clock (sample on the opposite edge, d = T/2)\n\n");
    printf(" MHz  | d(ns) 0.0 .. 10.0 step 0.5               | h | intersect window\n");
    for (int p = 0; p < 6; p++) {
        int T = periods[p];
        char row[64]; int n = 0;
        for (int d = 0; d <= 10000; d += 500) {
            int pc[3]; uint8_t got[3];
            for (int c = 0; c < 3; c++) {
                int viol = read_id_timed(T, d, corner_tact[c], got);
                pc[c] = viol == 0 && !memcmp(got, exp, 3);
                if (pc[c] != window_pass(T, d, corner_tact[c])) agree = 0;
            }
            row[n++] = (pc[0] && pc[1] && pc[2]) ? '#' : (pc[1] && pc[0]) ? 'T' : pc[1] ? 't' : pc[0] ? 'f' : '.';
            row[n++] = ' ';
        }
        row[n] = 0;
        int ph = 1; uint8_t got[3];
        for (int c = 0; c < 3; c++) {
            int viol = read_id_timed(T, T / 2, corner_tact[c], got);
            ph &= viol == 0 && !memcmp(got, exp, 3);
        }
        int lo = -T / 2 + 2 * TF + corner_tact[2] + TSU, hi = T / 2 + 2 * TF + corner_tact[0] - TH;
        printf("%s | %s| %c | ", fname[p], row, ph ? '#' : '.');
        if (hi >= lo) printf("[%5.2f, %5.2f] ns width %4.2f\n", lo / 1000.0, hi / 1000.0, (hi - lo) / 1000.0);
        else printf("none\n");
    }
    printf("\n");
    check(agree, "part3: simulated pass/fail == closed-form window for every cell (6 freq x 21 delays x 3 corners)");

    /* named cells */
    uint8_t got[3]; char h[16]; int v;
    struct { int T, d, c, pass; const char *why; } cells[] = {
        { 40000, 0, 2, 1, "25 MHz, d=0, slow corner passes (lots of margin)" },
        { 20000, 0, 2, 1, "50 MHz, d=0, slow corner passes (edge of window: lo=-1.0 ns)" },
        { 15000, 0, 1, 1, "66.7 MHz, d=0, typ corner passes (bench at room temp)" },
        { 15000, 0, 2, 0, "66.7 MHz, d=0, slow corner FAILS (hot / low V) -> PVT escape" },
        { 10000, 5000, 2, 1, "100 MHz, half-clock d=5 ns, slow corner passes" },
        { 10000, 5000, 0, 1, "100 MHz, half-clock d=5 ns, fast corner passes" },
        { 7500, 3750, 2, 0, "133 MHz, half-clock d=3.75 ns, slow corner FAILS -> need fine taps" },
        { 7500, 5500, 2, 1, "133 MHz, d=5.5 ns tap, slow corner passes" },
        { 7500, 5500, 0, 1, "133 MHz, d=5.5 ns tap, fast corner passes" },
    };
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        v = read_id_timed(cells[i].T, cells[i].d, corner_tact[cells[i].c], got);
        int pass = v == 0 && !memcmp(got, exp, 3);
        char what[128];
        snprintf(what, sizeof what, "part3 %s", cells[i].why);
        check(pass == cells[i].pass, what);
    }
    /* signatures of the failing cells */
    printf("\nfailing-cell signatures (what the engineer sees):\n");
    v = read_id_timed(10000, 0, corner_tact[2], got); hex(got, 3, h);
    printf("  100 MHz d=0 slow : %s  violations=%2d  -> %s\n", h, v, classify(got, exp, 3, MISO_IDLE));
    int shifted = v == 0 && !strcmp(classify(got, exp, 3, MISO_IDLE), "SHIFT>>1");
    for (int s = 0; s < 3; s++) {
        lcg = (uint32_t)(7 + s * 101);
        v = read_id_timed(10000, 0, corner_tact[1], got); hex(got, 3, h);
        printf("  100 MHz d=0 typ  : %s  violations=%2d  (seed %d) -> %s\n", h, v, 7 + s * 101,
               classify(got, exp, 3, MISO_IDLE));
    }
    v = read_id_timed(10000, 10000, corner_tact[0], got); hex(got, 3, h);
    printf("  100 MHz d=10 fast: %s  violations=%2d  -> %s\n", h, v, classify(got, exp, 3, MISO_IDLE));
    int lshift = v == 0 && !strcmp(classify(got, exp, 3, MISO_IDLE), "SHIFT<<1");
    printf("\n");
    check(shifted, "part3: sampling too EARLY at slow corner = clean 1-bit right shift F7 A0 0C (looks like a mode bug)");
    check(lshift, "part3: sampling too LATE at fast corner = clean 1-bit left shift");
}

int main(void) {
    part1();
    part2();
    part3();
    printf("\nSUMMARY: %s (%d failure%s)\n", g_fail ? "FAIL" : "ALL PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
