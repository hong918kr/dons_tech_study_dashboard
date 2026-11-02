/* F8 - memory-test failure list -> summary masks -> guess the broken data bit / address line */
#include <assert.h>
#include <stdio.h>
#include "fake_mem.h"

/* --- MODEL ANSWER --- */
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t addr, exp, act; } MemFail;   /* addr = word offset */

typedef struct {
    uint32_t n;
    uint32_t x_or, x_and;   /* data bits that failed in ANY / in EVERY entry   */
    uint32_t rose, fell;    /* read 1 where 0 expected / read 0 where 1 expected */
    uint32_t a_and, a_or;   /* AND / OR of failing addresses                  */
} FailSum;

void fail_summarize(const MemFail *f, size_t n, FailSum *s)
{
    s->n = (uint32_t)n;
    s->x_or = s->rose = s->fell = s->a_or = 0;
    s->x_and = s->a_and = 0xFFFFFFFFu;               /* identity for AND */
    for (size_t i = 0; i < n; i++) {
        uint32_t x = f[i].exp ^ f[i].act;
        s->x_or |= x;              s->x_and &= x;
        s->rose |= x & f[i].act;   s->fell  |= x & f[i].exp;
        s->a_and &= f[i].addr;     s->a_or  |= f[i].addr;
    }
}

typedef enum { DX_PASS, DX_ONE_CELL, DX_DATA_HI, DX_DATA_LO, DX_ADDR, DX_OTHER } Dx;

/* Data pattern assumed: own byte address (exp = BASE + 4*addr, then ~exp),
 * so on an alias exp ^ act = (addr ^ alias_addr) << 2. */
Dx fail_diagnose(const FailSum *s, uint32_t *which)
{
    if (s->n == 0) return DX_PASS;
    if (s->a_and == s->a_or) { *which = s->a_and; return DX_ONE_CELL; } /* one address */
    *which = s->x_or;
    if (s->fell == 0) return DX_DATA_HI;          /* only ever extra 1s: stuck-high/OR short */
    if (s->rose == 0) return DX_DATA_LO;          /* only ever missing 1s                    */
    if ((s->rose & s->fell) == s->x_or && (s->x_or & 3u) == 0) {
        *which = s->x_or >> 2;                    /* byte-address bits -> word-address lines */
        return DX_ADDR;                           /* every bad bit seen both ways            */
    }
    return DX_OTHER;                              /* coupling / random: run March C- next   */
}
/* --- END MODEL ANSWER --- */

#define MEM_BASE 0x80000000u
#define MAXF 1024
static MemFail flist[MAXF];

/* harness: own-address test with SEPARATE fill / verify passes, logging every failure */
static size_t log_own_addr(uint32_t nwords)
{
    size_t n = 0;
    for (int inv = 0; inv < 2; inv++) {
        for (uint32_t i = 0; i < nwords; i++)
            mem_wr(i, inv ? ~(MEM_BASE + 4u * i) : MEM_BASE + 4u * i);
        for (uint32_t i = 0; i < nwords; i++) {
            uint32_t exp = inv ? ~(MEM_BASE + 4u * i) : MEM_BASE + 4u * i;
            uint32_t act = mem_rd(i);
            if (act != exp && n < MAXF) flist[n++] = (MemFail){i, exp, act};
        }
    }
    return n;
}

static Dx diag_fault(uint32_t nwords, uint32_t *which, FailSum *s)
{
    size_t n = log_own_addr(nwords);
    fail_summarize(flist, n, s);
    return fail_diagnose(s, which);
}

int main(void)
{
    FailSum s;
    uint32_t w = 0;

    /* hand-made paper example: 3 failures, bit 4 always reads 1 */
    const MemFail ex[] = { {0x10, 0x00000000, 0x00000010},
                           {0x24, 0xAAAAAAAA, 0xAAAAAABA},
                           {0x3C, 0x12345600, 0x12345610} };
    fail_summarize(ex, 3, &s);
    assert(s.x_or == 0x10 && s.x_and == 0x10 && s.rose == 0x10 && s.fell == 0);
    assert(s.a_and == 0x00 && s.a_or == 0x3C);
    assert(fail_diagnose(&s, &w) == DX_DATA_HI && w == 0x10);

    fail_summarize(NULL, 0, &s);                              /* empty list */
    assert(fail_diagnose(&s, &w) == DX_PASS);

    fake_reset();                                             /* clean memory */
    assert(diag_fault(256, &w, &s) == DX_PASS);

    fake_reset(); F.d_hi = 1u << 7;                           /* D7 stuck-high */
    assert(diag_fault(256, &w, &s) == DX_DATA_HI && w == (1u << 7) && s.x_and == (1u << 7));

    fake_reset(); F.d_lo = 1u << 20;                          /* D20 stuck-low */
    assert(diag_fault(256, &w, &s) == DX_DATA_LO && w == (1u << 20));

    fake_reset(); F.d_or = (1u << 3) | (1u << 4);             /* D3-D4 wired-OR */
    assert(diag_fault(256, &w, &s) == DX_DATA_HI && w == 0x18u && s.x_and == 0);

    fake_reset(); F.cell = 0x42; F.c_lo = 1u << 9;            /* one weak cell */
    assert(diag_fault(256, &w, &s) == DX_ONE_CELL && w == 0x42);

    fake_reset(); F.a_lo = 1u << 5;                           /* A5 stuck-low */
    assert(diag_fault(64, &w, &s) == DX_ADDR && w == (1u << 5));
    assert(s.x_or == (1u << 7) && s.a_or == 31 && s.a_and == 0);  /* only low half fails */

    fake_reset(); F.a_hi = 1u << 3;                           /* A3 stuck-high */
    assert(diag_fault(256, &w, &s) == DX_ADDR && w == (1u << 3));

    fake_reset(); F.a_or = (1u << 2) | (1u << 3);             /* A2-A3 wired-OR */
    assert(diag_fault(256, &w, &s) == DX_ADDR && w == 0x0Cu);

    /* follow-up claim: F3's COMBINED verify+invert loop muddies the XOR for an alias */
    fake_reset(); F.a_lo = 1u << 5;
    size_t n = 0;
    for (uint32_t i = 0; i < 64; i++) mem_wr(i, MEM_BASE + 4u * i);
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < 64; i++) {
            uint32_t exp = MEM_BASE + 4u * i;
            if (pass == 1) exp = ~exp;
            uint32_t act = mem_rd(i);
            if (act != exp) flist[n++] = (MemFail){i, exp, act};
            if (pass == 0) mem_wr(i, ~exp);
        }
    assert(n == 96);
    unsigned complement_seen = 0;
    for (size_t i = 0; i < n; i++) complement_seen += ((flist[i].exp ^ flist[i].act) == ~0x80u);
    assert(complement_seen == 32);                            /* i = 32..63, pass 2 */
    fail_summarize(flist, n, &s);
    assert(fail_diagnose(&s, &w) == DX_OTHER);                /* no clean address story */

    /* mixed / coupling-like garbage -> no clean story */
    const MemFail mix[] = { {1, 0x0, 0x1}, {2, 0x3, 0x0}, {5, 0x0, 0x4} };
    fail_summarize(mix, 3, &s);
    assert(fail_diagnose(&s, &w) == DX_OTHER);

    printf("F8 PASS\n");
    return 0;
}
