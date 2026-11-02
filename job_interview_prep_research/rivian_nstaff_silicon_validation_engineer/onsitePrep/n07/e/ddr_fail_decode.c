/*
 * N07E — DDR failure-log decoder.
 *
 * Input: a text log of failure records (offset, expected64, actual64, optional temp/age/t/src),
 * plus header lines: #case, #expect, #bus <bits>, #pattern addr, #tested_at <temps...>.
 * It does NOT run a memory test (that is V1 / BV09 6.1). It READS the results:
 *   1. XOR = expected ^ actual  -> which logical bits flipped, which direction (0->1 / 1->0)
 *   2. fold logical bit -> physical DQ (dq = bit % bus) -> byte lane (dq / 8)
 *   3. address-as-data logs: XOR of two addresses -> aliasing address bit
 *   4. decode offset -> bank/row/col with a STATED mock address map
 *   5. temperature / age / time dependence
 * and prints one diagnosis "CLASS key=val ..." which is checked against #expect (PASS/FAIL).
 *
 * Mock address map (assumption, 4 GB, 32-bit bus, BL16 = 64 B burst):
 *   row = off[31:19]  bank = off[18:16]  col(burst idx) = off[15:6]  byte-in-burst = off[5:0]
 * Real silicon: read the controller's address-map registers (e.g. Synopsys DDRC ADDRMAPx,
 * which Linux synopsys_edac reads for poison setup) and the board DQ swizzle table.
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror ddr_fail_decode.c -o ddr_fail_decode
 * Run:   ./ddr_fail_decode logs/M0_clean.log logs/M1_dq13_stuck.log ...   (or ./run.sh)
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXREC 256
#define MAXTEMP 16

typedef struct {
    uint64_t off, exp, act;
    int temp;      /* -999 = unknown */
    int age_ms;    /* -1 = unknown */
    double t;      /* -1 = unknown */
} Rec;

typedef struct {
    char name[160];
    char expect[160];
    int bus;           /* data bits per beat */
    int addr_pattern;  /* 1 = expected value is the address itself */
    int tested[MAXTEMP];
    int ntested;
    Rec r[MAXREC];
    int n;
} Log;

static unsigned row_of(uint64_t o) { return (unsigned)((o >> 19) & 0x1FFF); }
static unsigned bank_of(uint64_t o) { return (unsigned)((o >> 16) & 0x7); }
static unsigned col_of(uint64_t o) { return (unsigned)((o >> 6) & 0x3FF); }

static int popcount64(uint64_t v)
{
    int c = 0;
    while (v) { v &= v - 1; c++; }
    return c;
}

static int ctz64(uint64_t v)
{
    int i = 0;
    while (i < 64 && !((v >> i) & 1)) i++;
    return i;
}

static int load(const char *path, Log *L)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    memset(L, 0, sizeof *L);
    L->bus = 32;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#') {
            if (!strncmp(line, "#case ", 6)) snprintf(L->name, sizeof L->name, "%s", line + 6);
            else if (!strncmp(line, "#expect ", 8)) snprintf(L->expect, sizeof L->expect, "%s", line + 8);
            else if (!strncmp(line, "#bus ", 5)) L->bus = atoi(line + 5);
            else if (!strncmp(line, "#pattern addr", 13)) L->addr_pattern = 1;
            else if (!strncmp(line, "#tested_at ", 11)) {
                char *p = line + 11, *end;
                while (L->ntested < MAXTEMP) {
                    long v = strtol(p, &end, 10);
                    if (end == p) break;
                    L->tested[L->ntested++] = (int)v;
                    p = end;
                }
            }
            continue;
        }
        if (!line[0]) continue;
        if (L->n >= MAXREC) { fprintf(stderr, "%s: too many records\n", path); break; }
        Rec *r = &L->r[L->n];
        r->temp = -999; r->age_ms = -1; r->t = -1;
        char *p = line, *end;
        r->off = strtoull(p, &end, 0); if (end == p) continue; p = end;
        r->exp = strtoull(p, &end, 0); if (end == p) continue; p = end;
        r->act = strtoull(p, &end, 0); if (end == p) continue; p = end;
        for (char *tok = strtok(p, " \t"); tok; tok = strtok(NULL, " \t")) {
            if (!strncmp(tok, "temp=", 5)) r->temp = atoi(tok + 5);
            else if (!strncmp(tok, "age=", 4)) r->age_ms = atoi(tok + 4);
            else if (!strncmp(tok, "t=", 2)) r->t = atof(tok + 2);
        }
        L->n++;
    }
    fclose(f);
    if (L->bus <= 0 || L->bus > 64 || L->bus % 8) { fprintf(stderr, "%s: bad #bus\n", path); return -1; }
    return 0;
}

/* expected "CLASS k=v k=v" must have same CLASS and every k=v present in diag */
static int match_expect(const char *expect, const char *diag)
{
    char e[160], d[256];
    snprintf(e, sizeof e, "%s", expect);
    snprintf(d, sizeof d, "%s", diag);
    char *save1 = NULL;
    char *ecls = strtok_r(e, " ", &save1);
    size_t dl = strcspn(d, " ");
    if (!ecls || strlen(ecls) != dl || strncmp(ecls, d, dl)) return 0;
    for (char *tok = strtok_r(NULL, " ", &save1); tok; tok = strtok_r(NULL, " ", &save1)) {
        char pat[80];
        snprintf(pat, sizeof pat, " %s", tok);
        const char *hit = strstr(d, pat);
        if (!hit) return 0;
        char after = hit[strlen(pat)];
        if (after != 0 && after != ' ') return 0;
    }
    return 1;
}

static int analyze(const Log *L)
{
    int bus = L->bus, lanes = bus / 8;
    int bit_hist[64] = {0}, dq_hist[64] = {0}, lane_hist[8] = {0};
    int up = 0, down = 0;               /* 0->1, 1->0 */
    int dq_up[64] = {0}, dq_down[64] = {0};
    char diag[256];

    printf("== %s\n", L->name);
    printf("   records=%d  bus=x%d (%d byte lanes)  %s\n", L->n, bus, lanes,
           L->addr_pattern ? "pattern=address-as-data" : "pattern=data");
    if (L->n == 0) {
        snprintf(diag, sizeof diag, "CLEAN");
        goto verdict;
    }

    /* ---- address-as-data: XOR of expected address vs returned address ---- */
    if (L->addr_pattern) {
        uint64_t d0 = L->r[0].exp ^ L->r[0].act;
        int same = 1, hi_reads_lo = 0;
        for (int i = 0; i < L->n; i++) {
            uint64_t d = L->r[i].exp ^ L->r[i].act;
            if (d != d0) same = 0;
            if ((L->r[i].off & d) && !(L->r[i].act & d)) hi_reads_lo++;
        }
        printf("   addr-xor (expected addr ^ returned addr) = 0x%09" PRIx64 "  same for all records: %s\n",
               d0, same ? "yes" : "no");
        if (same && popcount64(d0) == 1) {
            int k = ctz64(d0);
            const char *field = k >= 19 ? "row" : k >= 16 ? "bank" : k >= 6 ? "col" : "byte-in-burst";
            printf("   -> offset bit %d (mock map field: %s) is not decoded; %d/%d reads at bit%d=1 returned bit%d=0 data\n",
                   k, field, hi_reads_lo, L->n, k, k);
            snprintf(diag, sizeof diag, "ADDR_ALIAS bit=%d field=%s dir=%s", k, field,
                     hi_reads_lo == L->n ? "hi_reads_lo" : hi_reads_lo == 0 ? "lo_reads_hi" : "mixed");
        } else {
            snprintf(diag, sizeof diag, "ADDR_MULTI xor=0x%" PRIx64, d0);
        }
        goto verdict;
    }

    /* ---- data XOR: bit / DQ / lane / direction ---- */
    uint64_t first_off = L->r[0].off;
    int same_addr = 1;
    for (int i = 0; i < L->n; i++) {
        const Rec *r = &L->r[i];
        uint64_t x = r->exp ^ r->act;
        if (r->off != first_off) same_addr = 0;
        for (int b = 0; b < 64; b++) {
            if (!((x >> b) & 1)) continue;
            int dq = b % bus;
            bit_hist[b]++; dq_hist[dq]++; lane_hist[dq / 8]++;
            if ((r->act >> b) & 1) { up++; dq_up[dq]++; } else { down++; dq_down[dq]++; }
        }
    }
    printf("   logical bits:");
    for (int b = 0; b < 64; b++) if (bit_hist[b]) printf(" b%d:%d", b, bit_hist[b]);
    printf("\n   folded DQ   :");
    int ndq = 0, dq_one = -1;
    for (int q = 0; q < bus; q++) if (dq_hist[q]) { printf(" DQ%d:%d(+%d/-%d)", q, dq_hist[q], dq_up[q], dq_down[q]); ndq++; dq_one = q; }
    printf("\n   byte lanes  :");
    int nlane = 0, lane_one = -1;
    for (int l = 0; l < lanes; l++) { printf(" L%d:%d", l, lane_hist[l]); if (lane_hist[l]) { nlane++; lane_one = l; } }
    printf("\n   direction   : 0->1=%d  1->0=%d\n", up, down);

    /* ---- address decode with the stated mock map ---- */
    {
        unsigned rows[MAXREC]; int nrows = 0; int bank_hit[8] = {0};
        for (int i = 0; i < L->n; i++) {
            unsigned rw = row_of(L->r[i].off);
            int seen = 0;
            for (int j = 0; j < nrows; j++) if (rows[j] == rw) seen = 1;
            if (!seen) rows[nrows++] = rw;
            bank_hit[bank_of(L->r[i].off)]++;
        }
        int nbanks = 0;
        for (int b = 0; b < 8; b++) if (bank_hit[b]) nbanks++;
        printf("   decode(mock): distinct rows=%d banks=%d  first: off=0x%09" PRIx64 " bank=%u row=0x%x col=0x%x\n",
               nrows, nbanks, first_off, bank_of(first_off), row_of(first_off), col_of(first_off));
    }

    /* ---- temperature / age / time ---- */
    int hot_from = 9999, have_temp = 0, aged = 0, have_age = 0;
    if (L->ntested) {
        printf("   fails/temp  :");
        for (int k = 0; k < L->ntested; k++) {
            int c = 0;
            for (int i = 0; i < L->n; i++) if (L->r[i].temp == L->tested[k]) c++;
            printf(" %dC:%d", L->tested[k], c);
        }
        printf("\n");
    }
    for (int i = 0; i < L->n; i++) {
        if (L->r[i].temp != -999) { have_temp = 1; if (L->r[i].temp < hot_from) hot_from = L->r[i].temp; }
        if (L->r[i].age_ms >= 0) { have_age = 1; if (L->r[i].age_ms >= 64) aged++; }
    }
    int coolest_tested = 9999;
    for (int k = 0; k < L->ntested; k++) if (L->tested[k] < coolest_tested) coolest_tested = L->tested[k];
    int temp_dependent = have_temp && L->ntested > 1 && hot_from > coolest_tested;
    if (have_age) printf("   age>=64ms   : %d/%d records\n", aged, L->n);
    {
        int bursts = 0; double last = -1e9;
        int have_t = 0;
        for (int i = 0; i < L->n; i++) {
            if (L->r[i].t < 0) continue;
            have_t = 1;
            if (L->r[i].t - last > 60.0) bursts++;
            last = L->r[i].t;
        }
        if (have_t) {
            double span = L->r[L->n - 1].t - L->r[0].t;
            printf("   time        : span=%.0fs  bursts(gap>60s)=%d  rate=%.1f/h\n", span, bursts,
                   span > 0 ? L->n * 3600.0 / span : 0.0);
        }
    }

    /* ---- rules (ordered: most specific first) ---- */
    if (ndq == 1 && same_addr) {
        snprintf(diag, sizeof diag, "SINGLE_CELL addr=0x%09" PRIx64 " dq=%d lane=%d dir=%s count=%d",
                 first_off, dq_one, dq_one / 8, up ? (down ? "both" : "0to1") : "1to0", L->n);
    } else if (ndq == 1) {
        if (up && !down) snprintf(diag, sizeof diag, "DQ_STUCK dq=%d lane=%d dir=0to1", dq_one, dq_one / 8);
        else if (down && !up) snprintf(diag, sizeof diag, "DQ_STUCK dq=%d lane=%d dir=1to0", dq_one, dq_one / 8);
        else snprintf(diag, sizeof diag, "DQ_MARGIN dq=%d lane=%d", dq_one, dq_one / 8);
    } else if (temp_dependent && down * 10 >= (up + down) * 8 && (!have_age || aged == L->n)) {
        snprintf(diag, sizeof diag, "TEMP_RETENTION hot_from=%d dir=mostly_1to0 lanes=%d", hot_from, nlane);
    } else if (nlane == 1) {
        snprintf(diag, sizeof diag, "LANE_MARGIN lane=%d dqs=%d ndq=%d", lane_one, lane_one, ndq);
    } else if (temp_dependent) {
        snprintf(diag, sizeof diag, "TEMP_MARGIN hot_from=%d lanes=%d", hot_from, nlane);
    } else {
        snprintf(diag, sizeof diag, "MULTI_LANE_MARGIN lanes=%d ndq=%d", nlane, ndq);
    }

verdict:;
    int ok = match_expect(L->expect, diag);
    printf("   diagnosis   : %s\n", diag);
    printf("   expect      : %s  -> %s\n\n", L->expect, ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s log...\n", argv[0]); return 2; }
    static Log L;  /* large; keep off the stack */
    int pass = 0, total = 0;
    for (int i = 1; i < argc; i++) {
        if (load(argv[i], &L)) return 2;
        total++;
        pass += analyze(&L);
    }
    printf("summary: %d/%d PASS\n", pass, total);
    return pass == total ? 0 : 1;
}
