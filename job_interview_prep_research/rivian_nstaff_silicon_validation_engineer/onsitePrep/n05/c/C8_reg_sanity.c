/* C8 - table-driven register sanity test: reset values + walking-1 over RW bits,
 *      never writing RO/W1C registers, never touching side-effect registers. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only): 6 registers, each with real reset / writable mask / kind.
 * Fault injection: stuck-at-0 bits, wrong reset value. Counters prove the test never
 * wrote a W1C/RO register and never read the FIFO (read pops data). */
#define NREG 6
static const uint32_t m_off[NREG]   = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14 };
static const uint32_t m_reset[NREG] = { 0x00000000, 0x0000FF00, 0xC0DE0101, 0x00000003, 0, 0x00000010 };
static const uint32_t m_wmask[NREG] = { 0xFFFFFFFF, 0x0000FFFF, 0x00000000, 0, 0, 0x000000F0 };
static uint32_t m_val[NREG], m_stuck0[NREG], m_bad_writes, m_fifo_reads;
static int idx(uint32_t off) { for (int i = 0; i < NREG; i++) if (m_off[i] == off) return i; return -1; }
uint32_t reg_read(uint32_t off)
{
    int i = idx(off);
    if (off == 0x10) m_fifo_reads++;                       /* FIFO: read pops */
    return m_val[i] & ~m_stuck0[i];
}
void reg_write(uint32_t off, uint32_t v)
{
    int i = idx(off);
    if (off == 0x08 || off == 0x0C || off == 0x10) m_bad_writes++;  /* RO / W1C / FIFO */
    m_val[i] = (m_val[i] & ~m_wmask[i]) | (v & m_wmask[i]);
}
static void model_reset(void)
{
    memcpy(m_val, m_reset, sizeof m_val); memset(m_stuck0, 0, sizeof m_stuck0);
    m_bad_writes = m_fifo_reads = 0;
}

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

enum { REG_RW = 0, REG_RO = 1, REG_W1C = 2, REG_SKIP = 3 };
typedef struct { uint32_t off, reset, rw_mask; uint8_t type; } reg_desc_t;  /* type: REG_* */
typedef struct { uint32_t off, expect, got; uint8_t kind; } reg_fail_t;     /* kind: 0 reset, 1 rw */

static void log_fail(reg_fail_t *log, size_t cap, size_t *nf,
                     uint32_t off, uint32_t expect, uint32_t got, uint8_t kind)
{
    if (*nf < cap)
        log[*nf] = (reg_fail_t){ off, expect, got, kind };
    (*nf)++;                                    /* count all, log first `cap` */
}

size_t reg_sanity(const reg_desc_t *t, size_t n, reg_fail_t *log, size_t cap)
{
    size_t nf = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i].type == REG_SKIP)
            continue;                           /* FIFO / read-to-clear / self-clearing */
        uint32_t orig = reg_read(t[i].off);
        if (orig != t[i].reset)
            log_fail(log, cap, &nf, t[i].off, t[i].reset, orig, 0);
        if (t[i].type != REG_RW || t[i].rw_mask == 0)
            continue;                           /* RO, W1C: read-only check, never write */
        for (unsigned b = 0; b < 32; b++) {
            uint32_t bit = 1u << b;
            if (!(t[i].rw_mask & bit))
                continue;
            reg_write(t[i].off, bit);           /* walking-1 over RW bits only */
            uint32_t got = reg_read(t[i].off) & t[i].rw_mask;
            if (got != bit)
                log_fail(log, cap, &nf, t[i].off, bit, got, 1);
        }
        reg_write(t[i].off, orig & t[i].rw_mask);   /* restore; never write 1s outside RW */
    }
    return nf;
}
/* ==== ANSWER END ==== */

static const reg_desc_t tbl[] = {
    { 0x00, 0x00000000, 0xFFFFFFFF, REG_RW  },   /* SCRATCH */
    { 0x04, 0x0000FF00, 0x0000FFFF, REG_RW  },   /* CTRL: upper half RO */
    { 0x08, 0xC0DE0101, 0,          REG_RO  },   /* ID */
    { 0x0C, 0x00000003, 0,          REG_W1C },   /* INT_STATUS */
    { 0x10, 0,          0,          REG_SKIP},   /* RX_FIFO: read pops */
    { 0x14, 0x00000010, 0x000000F0, REG_RW  },   /* THRESH[7:4] */
};

int main(void)
{
    reg_fail_t log[4];
    size_t n = sizeof tbl / sizeof tbl[0];

    /* clean HW: 0 failures, side-effect registers untouched, state restored */
    model_reset();
    assert(reg_sanity(tbl, n, log, 4) == 0);
    assert(m_bad_writes == 0 && m_fifo_reads == 0);
    assert(memcmp(m_val, m_reset, sizeof m_val) == 0);

    /* SCRATCH bit 13 stuck at 0 -> exactly 1 rw failure */
    model_reset(); m_stuck0[0] = 1u << 13;
    assert(reg_sanity(tbl, n, log, 4) == 1);
    assert(log[0].off == 0x00 && log[0].kind == 1 && log[0].expect == (1u << 13) && log[0].got == 0);

    /* wrong reset on ID + stuck CTRL bit 0 + stuck THRESH bit 7, cap = 2: total 3, 2 logged */
    model_reset(); m_val[2] = 0xC0DE0102u; m_stuck0[1] = 1u; m_stuck0[5] = 1u << 7;
    memset(log, 0, sizeof log);
    assert(reg_sanity(tbl, n, log, 2) == 3);
    assert(log[0].off == 0x04 && log[0].kind == 1);      /* table order: CTRL first */
    assert(log[1].off == 0x08 && log[1].kind == 0 && log[1].got == 0xC0DE0102u);
    assert(log[2].off == 0 && log[2].kind == 0);         /* untouched beyond cap */

    /* cap = 0 with log = NULL is legal */
    model_reset(); m_stuck0[0] = 0xFu;
    assert(reg_sanity(tbl, n, NULL, 0) == 4);

    /* empty table */
    assert(reg_sanity(tbl, 0, NULL, 0) == 0);

    printf("C8 PASS\n");
    return 0;
}
