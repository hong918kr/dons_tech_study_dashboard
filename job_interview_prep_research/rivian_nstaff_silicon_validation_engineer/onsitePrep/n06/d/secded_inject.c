/*
 * secded_inject.c - N06D D2/D3: (72,64) SEC-DED ECC model + error-injection validation.
 *
 * Code: extended Hamming. Data bit j sits at Hamming position pos[j] (the 64
 * non-power-of-two positions in 3..71), check bits c0..c6 at positions 1,2,4..64,
 * c7 = overall parity over all 72 bits. Same structure a DDR/SRAM controller uses
 * for 64+8 ECC (real controllers often use Hsiao codes - same SEC-DED guarantees).
 *
 * What it validates (the rows of the ECC part of the N06D matrix):
 *   T1 every single-bit flip (72 positions, data AND check bits) -> corrected (CE)
 *   T2 every double-bit flip (C(72,2)=2556 pairs)                -> detected (UE), never "fixed"
 *   T3 random triple-bit flips -> show that SEC-DED can MIS-correct (beyond spec)
 *   T4 controller-style counters: CE count, first-error address log, UE interrupt
 *   T5 patrol scrub interval vs UE count when single-bit upsets accumulate
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined secded_inject.c
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int pos[64];                       /* Hamming position of data bit j */

static void build_map(void)
{
    int j = 0;
    for (int p = 3; p <= 71 && j < 64; p++)
        if (p & (p - 1)) pos[j++] = p;     /* skip powers of two */
}

static int par64(uint64_t x) { x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return (int)(x & 1); }
static int par8(uint8_t x) { return par64(x); }

static uint8_t hamming7(uint64_t d)        /* c0..c6 */
{
    uint8_t c = 0;
    for (int j = 0; j < 64; j++)
        if ((d >> j) & 1) c ^= (uint8_t)(pos[j] & 0x7F);   /* XOR of positions = syndrome form */
    return c;
}
static uint8_t encode(uint64_t d)
{
    uint8_t c = hamming7(d);
    uint8_t overall = (uint8_t)(par64(d) ^ par8(c));
    return (uint8_t)(c | (overall << 7));
}

enum { OK = 0, CE = 1, UE = 2 };
/* decode in place; returns OK / CE (corrected) / UE (detected, uncorrectable) */
static int decode(uint64_t *d, uint8_t *chk)
{
    uint8_t syn = (uint8_t)((hamming7(*d) ^ *chk) & 0x7F);
    int odd = par64(*d) ^ par8(*chk);       /* overall parity over all 72 bits */
    if (!syn && !odd) return OK;
    if (!odd) return UE;                    /* even # of flips, nonzero syndrome */
    if (!syn) { *chk ^= 0x80; return CE; }  /* overall parity bit itself */
    if (!(syn & (syn - 1))) { *chk ^= syn; return CE; }   /* a check bit */
    for (int j = 0; j < 64; j++)
        if (pos[j] == syn) { *d ^= 1ull << j; return CE; }
    return UE;                              /* syndrome points outside the word */
}

static void flip(uint64_t *d, uint8_t *c, int bit) { if (bit < 64) *d ^= 1ull << bit; else *c ^= (uint8_t)(1u << (bit - 64)); }

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }

/* ---- controller model for T4/T5: memory + ECC + counters + log ---- */
enum { WORDS = 4096 };
static uint64_t md[WORDS]; static uint8_t mc[WORDS];
static struct { uint32_t ce_cnt, ue_cnt; int ce_addr_valid; uint32_t ce_addr; int ue_irq; } reg;

static void mem_init(void)                /* "ECC init": write every word once */
{
    for (int i = 0; i < WORDS; i++) { md[i] = rnd(); mc[i] = encode(md[i]); }
    memset(&reg, 0, sizeof reg);
}
static int mem_read(uint32_t a, uint64_t *out)   /* read path: correct, count, log, write back on CE */
{
    uint64_t d = md[a]; uint8_t c = mc[a];
    int r = decode(&d, &c);
    if (r == CE) {
        reg.ce_cnt++;
        if (!reg.ce_addr_valid) { reg.ce_addr_valid = 1; reg.ce_addr = a; }
        md[a] = d; mc[a] = c;              /* corrected write-back */
    } else if (r == UE) { reg.ue_cnt++; reg.ue_irq = 1; }
    *out = d;
    return r;
}

int main(void)
{
    build_map();
    int all = 1;

    /* T1: exhaustive single-bit */
    long t1 = 0, t1_ok = 0;
    for (int w = 0; w < 1000; w++) {
        uint64_t d0 = rnd(); uint8_t c0 = encode(d0);
        for (int b = 0; b < 72; b++) {
            uint64_t d = d0; uint8_t c = c0; flip(&d, &c, b);
            int r = decode(&d, &c);
            t1++; t1_ok += (r == CE && d == d0 && c == c0);
        }
    }
    printf("T1 single-bit : %ld injections, corrected+exact %ld  %s\n", t1, t1_ok, t1 == t1_ok ? "PASS" : "FAIL");
    all &= t1 == t1_ok;

    /* T2: exhaustive double-bit */
    long t2 = 0, t2_ue = 0, t2_mis = 0;
    for (int w = 0; w < 200; w++) {
        uint64_t d0 = rnd(); uint8_t c0 = encode(d0);
        for (int a = 0; a < 72; a++)
            for (int b = a + 1; b < 72; b++) {
                uint64_t d = d0; uint8_t c = c0; flip(&d, &c, a); flip(&d, &c, b);
                int r = decode(&d, &c);
                t2++; t2_ue += r == UE; t2_mis += r != UE;
            }
    }
    printf("T2 double-bit : %ld injections (200 words x 2556 pairs), UE %ld, not-UE %ld  %s\n",
           t2, t2_ue, t2_mis, t2_mis == 0 ? "PASS" : "FAIL");
    all &= t2_mis == 0;

    /* T3: triple-bit (beyond the guarantee) */
    long t3 = 0, mis = 0, ue = 0, silent = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t d0 = rnd(); uint8_t c0 = encode(d0), c;
        int a = (int)(rnd() % 72), b, e;
        do b = (int)(rnd() % 72); while (b == a);
        do e = (int)(rnd() % 72); while (e == a || e == b);
        uint64_t d = d0; c = c0; flip(&d, &c, a); flip(&d, &c, b); flip(&d, &c, e);
        int r = decode(&d, &c);
        t3++;
        if (r == UE) ue++;
        else if (r == CE) mis++;            /* "corrected" into a wrong word */
        else silent++;
    }
    printf("T3 triple-bit : %ld injections -> mis-corrected (reported CE, data wrong) %.1f%%, detected UE %.1f%%, silent %.1f%%  INFO\n",
           t3, 100.0 * (double)mis / (double)t3, 100.0 * (double)ue / (double)t3, 100.0 * (double)silent / (double)t3);

    /* T4: controller counters / address log / UE interrupt via "poison" */
    mem_init();
    uint64_t v;
    int k1 = 0, k2 = 0, k3 = 0;
    flip(&md[100], &mc[100], 5);                       /* poison 1 bit @100 */
    flip(&md[2000], &mc[2000], 70);                    /* 1 check bit @2000 */
    k1 = mem_read(100, &v) == CE && reg.ce_cnt == 1 && reg.ce_addr_valid && reg.ce_addr == 100;
    k2 = mem_read(2000, &v) == CE && reg.ce_cnt == 2 && reg.ce_addr == 100;   /* log keeps FIRST */
    int again = mem_read(100, &v) == OK && reg.ce_cnt == 2;                     /* write-back fixed it */
    flip(&md[300], &mc[300], 1); flip(&md[300], &mc[300], 40);                  /* poison 2 bits */
    k3 = mem_read(300, &v) == UE && reg.ue_cnt == 1 && reg.ue_irq;
    int t4 = k1 && k2 && again && k3;
    printf("T4 counters   : CE@100 logged=%d, 2nd CE keeps first addr=%d, re-read after write-back clean=%d, 2-bit -> UE irq=%d  %s\n",
           k1, k2, again, k3, t4 ? "PASS" : "FAIL");
    all &= t4;

    /* T5: patrol scrub. 1 random single-bit upset per epoch, 4000 epochs.
     * No scrub: words are only cleaned when read. With scrub every S epochs,
     * a full pass reads (and fixes) every word. Count UE seen by the final full read. */
    static const int scrub_every[] = {0, 400, 100, 20, 5};
    printf("T5 patrol scrub (4096 words, 1 upset/epoch, 4000 epochs):\n");
    long ue_by[5];
    for (int k = 0; k < 5; k++) {
        s = 0x1234567ull + 0;                             /* same upset sequence each run */
        mem_init();
        long ue_total = 0;
        for (int ep = 1; ep <= 4000; ep++) {
            uint32_t a = (uint32_t)(rnd() % WORDS); int b = (int)(rnd() % 72);
            flip(&md[a], &mc[a], b);
            if (scrub_every[k] && ep % scrub_every[k] == 0)
                for (uint32_t i = 0; i < WORDS; i++) if (mem_read(i, &v) == UE) { ue_total++; md[i] = 0; mc[i] = encode(0); }
        }
        for (uint32_t i = 0; i < WORDS; i++) if (mem_read(i, &v) == UE) ue_total++;
        ue_by[k] = ue_total;
        if (scrub_every[k]) printf("  scrub every %3d epochs: CE=%5u  UE=%ld\n", scrub_every[k], reg.ce_cnt, ue_total);
        else                printf("  no scrub             : CE=%5u  UE=%ld\n", reg.ce_cnt, ue_total);
    }
    int t5 = ue_by[0] > ue_by[2] && ue_by[2] > ue_by[4];
    printf("T5 trend (more scrub -> fewer UE): %s\n", t5 ? "PASS" : "FAIL");
    all &= t5;

    printf("RESULT secded_inject %s\n", all ? "PASS" : "FAIL");
    return all ? 0 : 1;
}
