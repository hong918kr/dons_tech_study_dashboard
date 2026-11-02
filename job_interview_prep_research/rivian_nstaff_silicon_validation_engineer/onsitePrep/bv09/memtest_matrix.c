/* memtest_matrix.c - BV09: "which memory test catches which fault?"
 *
 * A simulated 256-word x 32-bit memory (8 address lines A0..A7, 32 data lines D0..D31) with ONE
 * injectable fault at a time. Nine bring-up tests run against every fault, each on a
 * freshly "powered-up" memory, and the program prints a detection matrix plus the first failure
 * record (addr / expected / actual / xor) for every hit.
 *
 * Tests (column order):
 *   NAIVE   write a word, read it right back (two patterns)            - what people write first
 *   DBUS    Barr data bus test: walking 1s at ONE address              - data lines
 *   ABUS    Barr address bus test: power-of-two offsets, 2 steps        - address lines
 *   DEVICE  Barr device test: fill increment, verify+invert, verify    - every cell holds 0 and 1
 *   MOVINV  moving inversions (memtest86 style): fill P, up(rP,w~P), down(r~P,wP)
 *   MARCHC- March C-: {any(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); any(r0)} = 10N
 *   MARCH-DB  March C- repeated over 6 data backgrounds (0000.., 5555.., 3333.., 0F0F.., 00FF.., 0000FFFF)
 *   RANDOM  xorshift32 fill + verify, then complement fill + verify    - unique values per address
 *   BITFADE fill 1s, wait, verify; fill 0s, wait, verify               - data retention
 *
 * Fault models (memory side is "the board"; tests only see mem_wr()/mem_rd()):
 *   cell stuck-at 0/1, transition fault (bit cannot rise 0->1), inversion coupling (CFin),
 *   idempotent coupling (CFid), data line stuck high, two data lines shorted (wired-AND),
 *   address line stuck low/high, two address lines shorted (wired-AND and wired-OR),
 *   missing chip on one byte lane (reads return the last value left on the bus = bus capacitance),
 *   weak cell that loses a stored 1 after a while (retention / bit fade).
 *
 * Build/run (macOS, per BV_NOTES_SPEC):
 *   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
 *      -fsanitize=address,undefined memtest_matrix.c -o /tmp/memtest_matrix && /tmp/memtest_matrix
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NWORDS      256u            /* word addressed: A0..A7 */
#define AMASK       (NWORDS - 1u)
#define GARBAGE     0xA5A5A5A5u     /* power-up content, like the V1 drill */
#define RET_TICKS   5000ul          /* weak cell: a stored 1 fades after this many bus cycles */
#define WAIT_TICKS  100000ul        /* BITFADE "sleep" (real test: minutes) */

/* ------------------------------------------------------------------ fault models */
typedef enum {
    F_NONE, F_CELL_SA0, F_CELL_SA1, F_TF_UP, F_CFIN, F_CFID,
    F_DLINE_SA1, F_DLINE_SHORT, F_ALINE_SA0, F_ALINE_SA1, F_ASHORT_AND, F_ASHORT_OR,
    F_FLOAT_LANE, F_RETENTION
} Kind;

typedef struct {
    Kind        kind;
    const char *name;
    uint32_t    a1; int b1;     /* cell / aggressor word+bit, or line index */
    uint32_t    a2; int b2;     /* victim word+bit, or second line index     */
    uint32_t    lane;           /* F_FLOAT_LANE: data bits with no chip behind them */
} Fault;

static const Fault faults[] = {
    { F_NONE,        "F00 clean (no fault)",    0, 0,  0, 0,  0 },
    { F_CELL_SA0,    "F01 cell w37.b5 SA0",    37, 5,  0, 0,  0 },
    { F_CELL_SA1,    "F02 cell w200.b30 SA1", 200, 30, 0, 0,  0 },
    { F_TF_UP,       "F03 TF w100.b4 no 0->1",100, 4,  0, 0,  0 },
    { F_CFIN,        "F04 CFin w20.b1^>w7.b3", 20, 1,  7, 3,  0 },
    { F_CFID,        "F05 CFid w9.b0^>w50.b12=0", 9, 0, 50, 12, 0 },
    { F_DLINE_SA1,   "F06 data D7 stuck-hi",    0, 7,  0, 0,  0 },
    { F_DLINE_SHORT, "F07 data D3-D4 short",    0, 3,  0, 4,  0 },
    { F_ALINE_SA0,   "F08 addr A4 stuck-lo",    0, 4,  0, 0,  0 },
    { F_ALINE_SA1,   "F09 addr A6 stuck-hi",    0, 6,  0, 0,  0 },
    { F_ASHORT_AND,  "F10 addr A2-A3 shrt AND", 0, 2,  0, 3,  0 },
    { F_ASHORT_OR,   "F11 addr A2-A3 shrt OR",  0, 2,  0, 3,  0 },
    { F_FLOAT_LANE,  "F12 no chip D31:24",      0, 0,  0, 0,  0xFF000000u },
    { F_RETENTION,   "F13 weak w64.b9 fades",  64, 9,  0, 0,  0 },
};
#define NFAULTS (sizeof faults / sizeof faults[0])

/* ------------------------------------------------------------------ simulated memory */
static struct {
    uint32_t      cell[NWORDS];
    unsigned long set_tick;     /* when the weak cell's bit was last written as 1 */
    uint32_t      bus;          /* last value driven on the data bus */
    unsigned long tick, rd, wr;
    const Fault  *f;
} M;

static void mem_powerup(const Fault *f)
{
    for (uint32_t i = 0; i < NWORDS; i++)
        M.cell[i] = GARBAGE;
    M.set_tick = 0; M.bus = 0; M.tick = M.rd = M.wr = 0;
    M.f = f;
}

static uint32_t short_lines(uint32_t v, int x, int y, bool wired_and)
{
    uint32_t bx = (v >> x) & 1u, by = (v >> y) & 1u;
    uint32_t b  = wired_and ? (bx & by) : (bx | by);
    v &= ~((1u << x) | (1u << y));
    return v | (b << x) | (b << y);
}

static uint32_t phys(uint32_t a)            /* address decoder + address-line faults */
{
    const Fault *f = M.f;
    a &= AMASK;
    switch (f->kind) {
    case F_ALINE_SA0:  return a & ~(1u << f->b1);
    case F_ALINE_SA1:  return a | (1u << f->b1);
    case F_ASHORT_AND: return short_lines(a, f->b1, f->b2, true);
    case F_ASHORT_OR:  return short_lines(a, f->b1, f->b2, false);
    default:           return a;
    }
}

static uint32_t dlines(uint32_t v)          /* data-line faults (at the pins, every address) */
{
    const Fault *f = M.f;
    if (f->kind == F_DLINE_SA1)   return v | (1u << f->b1);
    if (f->kind == F_DLINE_SHORT) return short_lines(v, f->b1, f->b2, true);
    return v;
}

static void mem_wr(uint32_t a, uint32_t v)
{
    const Fault *f = M.f;
    M.tick++; M.wr++;
    v = dlines(v);
    M.bus = v;
    uint32_t pa = phys(a), old = M.cell[pa], nv = v, m1 = 1u << f->b1;
    if (pa == f->a1) {
        if (f->kind == F_CELL_SA0) nv &= ~m1;
        if (f->kind == F_CELL_SA1) nv |= m1;
        if (f->kind == F_TF_UP && !(old & m1)) nv &= ~m1;      /* cannot rise */
        if (f->kind == F_RETENTION && (nv & m1)) M.set_tick = M.tick;
    }
    M.cell[pa] = nv;
    /* coupling: a rising transition on the aggressor bit disturbs the victim bit */
    if (pa == f->a1 && !(old & m1) && (nv & m1)) {
        if (f->kind == F_CFIN) M.cell[f->a2] ^= 1u << f->b2;
        if (f->kind == F_CFID) M.cell[f->a2] &= ~(1u << f->b2);
    }
}

static uint32_t mem_rd(uint32_t a)
{
    const Fault *f = M.f;
    M.tick++; M.rd++;
    uint32_t pa = phys(a), v = M.cell[pa], m1 = 1u << f->b1;
    if (pa == f->a1) {
        if (f->kind == F_CELL_SA0) v &= ~m1;
        if (f->kind == F_CELL_SA1) v |= m1;
        if (f->kind == F_RETENTION && (v & m1) && M.tick - M.set_tick > RET_TICKS)
            M.cell[pa] = (v &= ~m1);                          /* charge leaked away */
    }
    if (f->kind == F_FLOAT_LANE)                              /* nobody drives these bits */
        v = (v & ~f->lane) | (M.bus & f->lane);
    v = dlines(v);
    M.bus = v;
    return v;
}

/* ------------------------------------------------------------------ test engine */
typedef struct { bool fail; uint32_t addr, exp, act; int where; } Res;

static bool check(Res *r, uint32_t a, uint32_t exp, int where)
{
    uint32_t act = mem_rd(a);
    if (act == exp) return true;
    *r = (Res){ true, a, exp, act, where };
    return false;
}

static Res t_naive(void)
{
    Res r = { 0 };
    for (uint32_t a = 0; a < NWORDS; a++) {
        mem_wr(a, 0x5A5A5A5Au); if (!check(&r, a, 0x5A5A5A5Au, 0)) return r;
        mem_wr(a, 0xA5A5A5A5u); if (!check(&r, a, 0xA5A5A5A5u, 1)) return r;
    }
    return r;
}

static Res t_dbus(void)                      /* Barr memTestDataBus, at address 0 */
{
    Res r = { 0 };
    for (int b = 0; b < 32; b++) {
        mem_wr(0, 1u << b);
        if (!check(&r, 0, 1u << b, b)) return r;              /* where = data bit */
    }
    return r;
}

static Res t_abus(void)                      /* Barr memTestAddressBus, base 0, n = NWORDS */
{
    const uint32_t P = 0xAAAAAAAAu, AP = 0x55555555u;
    Res r = { 0 };
    for (uint32_t off = 1; off < NWORDS; off <<= 1)
        mem_wr(off, P);
    /* step 2: base gets the anti-pattern; no power-of-two offset may see it (stuck high) */
    mem_wr(0, AP);
    for (uint32_t off = 1; off < NWORDS; off <<= 1)
        if (!check(&r, off, P, 20 + __builtin_ctz(off))) return r;   /* 2k = step2, line k */
    mem_wr(0, P);
    /* step 3: each offset in turn gets the anti-pattern; nobody else may see it (low/short) */
    for (uint32_t t = 1; t < NWORDS; t <<= 1) {
        mem_wr(t, AP);
        if (!check(&r, 0, P, 30 + __builtin_ctz(t))) return r;
        for (uint32_t off = 1; off < NWORDS; off <<= 1)
            if (off != t && !check(&r, off, P, 30 + __builtin_ctz(t))) return r;
        mem_wr(t, P);
    }
    return r;
}

static Res t_device(void)                    /* Barr memTestDevice: increment / invert */
{
    Res r = { 0 };
    for (uint32_t a = 0; a < NWORDS; a++) mem_wr(a, a + 1u);
    for (uint32_t a = 0; a < NWORDS; a++) {
        if (!check(&r, a, a + 1u, 1)) return r;
        mem_wr(a, ~(a + 1u));
    }
    for (uint32_t a = 0; a < NWORDS; a++)
        if (!check(&r, a, ~(a + 1u), 2)) return r;
    return r;
}

static Res t_movinv(void)                    /* memtest86-style moving inversions, one pattern */
{
    const uint32_t P = 0x00000000u;
    Res r = { 0 };
    for (uint32_t a = 0; a < NWORDS; a++) mem_wr(a, P);
    for (uint32_t a = 0; a < NWORDS; a++) {
        if (!check(&r, a, P, 1)) return r;
        mem_wr(a, ~P);
    }
    for (uint32_t i = 0; i < NWORDS; i++) {
        uint32_t a = NWORDS - 1u - i;
        if (!check(&r, a, ~P, 2)) return r;
        mem_wr(a, P);
    }
    return r;
}

/* one March element: direction, optional read-expect, optional write */
static bool march_elem(Res *r, bool up, bool rd, uint32_t rv, bool wr, uint32_t wv, int el)
{
    for (uint32_t i = 0; i < NWORDS; i++) {
        uint32_t a = up ? i : NWORDS - 1u - i;     /* never "a >= 0" with unsigned */
        if (rd && !check(r, a, rv, el)) return false;
        if (wr) mem_wr(a, wv);
    }
    return true;
}

/* March C- with data background Z ("0") and ~Z ("1"); el_base offsets the element id */
static bool marchc_bg(Res *r, uint32_t Z, int el_base)
{
    const uint32_t O = ~Z;
    return march_elem(r, true,  false, 0, true,  Z, el_base + 0) &&   /* M0 any(w0)     */
           march_elem(r, true,  true,  Z, true,  O, el_base + 1) &&   /* M1 up(r0,w1)   */
           march_elem(r, true,  true,  O, true,  Z, el_base + 2) &&   /* M2 up(r1,w0)   */
           march_elem(r, false, true,  Z, true,  O, el_base + 3) &&   /* M3 down(r0,w1) */
           march_elem(r, false, true,  O, true,  Z, el_base + 4) &&   /* M4 down(r1,w0) */
           march_elem(r, true,  true,  Z, false, 0, el_base + 5);     /* M5 any(r0)     */
}

static Res t_marchc(void)
{
    Res r = { 0 };
    marchc_bg(&r, 0x00000000u, 0);
    return r;
}

/* word-oriented memory: solid 0/1 backgrounds cannot see two bits of the SAME word that are
 * bridged (they always carry equal values). Repeat March C- with log2(32)+1 = 6 backgrounds. */
static Res t_marchc_db(void)
{
    static const uint32_t bg[6] = { 0x00000000u, 0x55555555u, 0x33333333u,
                                    0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu };
    Res r = { 0 };
    for (int b = 0; b < 6; b++)
        if (!marchc_bg(&r, bg[b], 10 * b)) break;            /* where = 10*bg + element */
    return r;
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static Res t_random(void)
{
    Res r = { 0 };
    for (int pass = 0; pass < 2; pass++) {
        uint32_t inv = pass ? ~0u : 0u, s = 0x1234567u;
        for (uint32_t a = 0; a < NWORDS; a++) mem_wr(a, xs32(&s) ^ inv);
        s = 0x1234567u;
        for (uint32_t a = 0; a < NWORDS; a++)
            if (!check(&r, a, xs32(&s) ^ inv, pass)) return r;
    }
    return r;
}

static Res t_bitfade(void)
{
    Res r = { 0 };
    for (int pass = 0; pass < 2; pass++) {
        uint32_t v = pass ? 0u : ~0u;
        for (uint32_t a = 0; a < NWORDS; a++) mem_wr(a, v);
        M.tick += WAIT_TICKS;                               /* "sleep N minutes" */
        for (uint32_t a = 0; a < NWORDS; a++)
            if (!check(&r, a, v, pass)) return r;
    }
    return r;
}

typedef struct { const char *name; Res (*fn)(void); } Test;
static const Test tests[] = {
    { "NAIVE", t_naive }, { "DBUS", t_dbus }, { "ABUS", t_abus }, { "DEVICE", t_device },
    { "MOVINV", t_movinv }, { "MARCHC-", t_marchc }, { "MARCH-DB", t_marchc_db },
    { "RANDOM", t_random }, { "BITFADE", t_bitfade },
};
#define NTESTS (sizeof tests / sizeof tests[0])

static void where_str(char *buf, size_t n, int ti, const Res *r)
{
    if (!r->fail) { snprintf(buf, n, "-"); return; }
    switch (ti) {
    case 1:  snprintf(buf, n, "D%d", r->where); break;                       /* DBUS: bit */
    case 2:  snprintf(buf, n, "s%d:A%d", r->where / 10, r->where % 10); break; /* ABUS */
    case 5:  snprintf(buf, n, "M%d", r->where); break;                       /* March element */
    case 6:  snprintf(buf, n, "b%d.M%d", r->where / 10, r->where % 10); break; /* bg.element */
    default: snprintf(buf, n, "p%d", r->where); break;                       /* pass */
    }
}

int main(void)
{
    Res res[NFAULTS][NTESTS];
    unsigned long rd0[NTESTS], wr0[NTESTS];

    for (size_t fi = 0; fi < NFAULTS; fi++)
        for (size_t ti = 0; ti < NTESTS; ti++) {
            mem_powerup(&faults[fi]);
            res[fi][ti] = tests[ti].fn();
            if (fi == 0) { rd0[ti] = M.rd; wr0[ti] = M.wr; }
        }

    printf("BV09 memory-test detection matrix  (N = %u words x 32 bit, one fault per row)\n", NWORDS);
    printf("cell = where the FIRST mismatch was seen ('-' = test PASSED = fault missed)\n");
    printf("  Dk = data bit k | s2/s3:Ak = address test step 2/3, line k | Mk = March element | pk = pass k\n\n");
    printf("%-26s", "fault \\ test");
    for (size_t ti = 0; ti < NTESTS; ti++) printf(" %-8s", tests[ti].name);
    printf("\n");
    for (size_t fi = 0; fi < NFAULTS; fi++) {
        printf("%-26s", faults[fi].name);
        for (size_t ti = 0; ti < NTESTS; ti++) {
            char w[16];
            where_str(w, sizeof w, (int)ti, &res[fi][ti]);
            printf(" %-8s", w);
        }
        printf("\n");
    }

    printf("\naccesses on clean memory (reads + writes):\n");
    for (size_t ti = 0; ti < NTESTS; ti++)
        printf("  %-8s %5lu rd + %5lu wr = %5lu  (%.1fN)\n", tests[ti].name, rd0[ti], wr0[ti],
               rd0[ti] + wr0[ti], (double)(rd0[ti] + wr0[ti]) / NWORDS);

    printf("\nfirst-failure records (addr / expected / actual / xor = failing bits):\n");
    for (size_t fi = 1; fi < NFAULTS; fi++)
        for (size_t ti = 0; ti < NTESTS; ti++) {
            const Res *r = &res[fi][ti];
            if (!r->fail) continue;
            char w[16];
            where_str(w, sizeof w, (int)ti, r);
            printf("  %-26s %-8s %-7s addr=0x%03x exp=0x%08x act=0x%08x xor=0x%08x\n",
                   faults[fi].name, tests[ti].name, w, (unsigned)r->addr, (unsigned)r->exp,
                   (unsigned)r->act, (unsigned)(r->exp ^ r->act));
        }

    /* self-check: the clean row must pass everything, March C- must be exactly 10N */
    int bad = 0;
    for (size_t ti = 0; ti < NTESTS; ti++) bad += res[0][ti].fail;
    bad += (rd0[5] + wr0[5]) != 10ul * NWORDS;
    bad += (rd0[6] + wr0[6]) != 60ul * NWORDS;
    printf("\nself-check: %s\n", bad ? "FAIL" : "PASS (clean memory passes all 9 tests, March C- = 10N, 6-bg March = 60N)");
    return bad != 0;
}
