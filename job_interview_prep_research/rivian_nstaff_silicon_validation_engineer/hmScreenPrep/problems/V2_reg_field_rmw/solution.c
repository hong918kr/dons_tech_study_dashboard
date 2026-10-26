/*
 * V2  Register Access Layer + Register-Sanity Test (Reference solution)
 *
 * A new IP block ("SIF", a sensor-interface block) has just come up on FPGA / first silicon.
 * Its registers are reached through reg_read(off) / reg_write(off, val) (32-bit MMIO).
 * The HW model at the bottom emulates the block: RW bits, RO bits, W1C status bits,
 * a self-clearing SOFT_RST bit, reserved bits that read 0, and a FIFO register whose
 * read pops data (side effect). It can also inject HW bugs.
 *
 * Implement:
 *   field_mask(msb, lsb)        mask with bits msb..lsb set (0 <= lsb <= msb <= 31).
 *                               Must be correct for msb=31, lsb=0 (no UB shift by 32).
 *   field_get(reg, msb, lsb)    extract the field, right-aligned.
 *   field_prep(val, msb, lsb)   place val into the field (val truncated to the field width).
 *   field_write(rd, msb, lsb, v) read-modify-write one field of register rd->off:
 *        - -EINVAL if msb > 31 or lsb > msb; -ERANGE if v does not fit the field.
 *          On error, do not touch the register.
 *        - Bits outside the field are written back as read, EXCEPT W1C bits and
 *          self-clearing bits, which must be written as 0 (writing back a 1 that was
 *          read from a W1C bit would clear a pending status the driver never saw).
 *        - Bits inside the field are written as given (so writing 1 to a W1C field clears it).
 *   reg_sanity_test(tbl, n, log, log_cap)
 *        For every register in the description table (skip entries with REG_F_SKIP):
 *        1. RESET : read; must equal rd->reset.             (orig = value read)
 *        2. RO    : write orig on RW bits, ~orig on "RO" bits (= not RW, not W1C, not SC),
 *                   0 on W1C/SC bits; read back must equal orig.
 *        3. RW    : for each pattern p in { 0x55555555, 0xAAAAAAAA, then walking-1 over every
 *                   RW bit from bit 0 up }: write (p & rw) | (orig & RO bits); read back must
 *                   equal (p & rw) | (orig & ~rw).  (skip if rw_mask == 0)
 *        4. restore: write orig with W1C/SC bits cleared.
 *        Each failed check counts as one mismatch: kind MM_RW if the difference touches an
 *        RW bit, otherwise MM_RO (MM_RESET for step 1). Log the first log_cap mismatches
 *        into log (log may be NULL when log_cap == 0). Return the total mismatch count.
 *
 * Build & run:  ./run.sh V2 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- MMIO HAL (provided, implemented by the HW model below) ---------------- */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

/* ---------------- register description (from the IP spec / IP-XACT) ---------------- */
#define REG_F_SKIP  0x1u          /* side-effect register: never touch in generic tests */

typedef struct {
    const char *name;
    uint32_t off;
    uint32_t reset;               /* documented reset value */
    uint32_t rw_mask;             /* read-write bits */
    uint32_t w1c_mask;            /* write-1-to-clear status bits (HW sets them) */
    uint32_t sc_mask;             /* self-clearing / write-action bits (read as 0) */
    uint32_t flags;
} RegDesc;

#define SIF_ID       0x00u
#define SIF_CTRL     0x04u        /* [0] EN  [3:1] MODE  [15:8] DIV  [31] SOFT_RST (SC) */
#define SIF_IRQ      0x08u        /* [2:0] pending (W1C)  [8] BUSY (RO)  [18:16] IRQ_EN (RW) */
#define SIF_TIMEOUT  0x0Cu        /* [23:0] */
#define SIF_THRESH   0x10u        /* [11:0] LO  [27:16] HI */
#define SIF_SCRATCH  0x14u
#define SIF_FIFO     0x18u        /* RO, read pops one entry */

static const RegDesc SIF_REGS[] = {
    { "ID",      SIF_ID,      0x5A1D0102u, 0x00000000u, 0x0u, 0x00000000u, 0 },
    { "CTRL",    SIF_CTRL,    0x00000400u, 0x0000FF0Fu, 0x0u, 0x80000000u, 0 },
    { "IRQ",     SIF_IRQ,     0x00000000u, 0x00070000u, 0x7u, 0x00000000u, 0 },
    { "TIMEOUT", SIF_TIMEOUT, 0x0000FFFFu, 0x00FFFFFFu, 0x0u, 0x00000000u, 0 },
    { "THRESH",  SIF_THRESH,  0x08000100u, 0x0FFF0FFFu, 0x0u, 0x00000000u, 0 },
    { "SCRATCH", SIF_SCRATCH, 0x00000000u, 0xFFFFFFFFu, 0x0u, 0x00000000u, 0 },
    { "FIFO",    SIF_FIFO,    0x00000000u, 0x00000000u, 0x0u, 0x00000000u, REG_F_SKIP },
};
#define N_SIF_REGS (sizeof SIF_REGS / sizeof SIF_REGS[0])

typedef enum { MM_RESET = 1, MM_RO = 2, MM_RW = 3 } MmKind;
typedef struct {
    uint32_t off;
    MmKind   kind;
    uint32_t wrote;               /* value written before the read-back (0 for MM_RESET) */
    uint32_t expect, got;
} Mismatch;

/* ---------------- your code ---------------- */

uint32_t field_mask(unsigned msb, unsigned lsb)
{
    /* two shifts of at most 31 each: no UB at width 32 */
    return (0xFFFFFFFFu >> (31u - msb)) & (0xFFFFFFFFu << lsb);
}

uint32_t field_get(uint32_t reg, unsigned msb, unsigned lsb)
{
    return (reg & field_mask(msb, lsb)) >> lsb;
}

uint32_t field_prep(uint32_t val, unsigned msb, unsigned lsb)
{
    return (val << lsb) & field_mask(msb, lsb);
}

int field_write(const RegDesc *rd, unsigned msb, unsigned lsb, uint32_t val)
{
    if (msb > 31u || lsb > msb) return -EINVAL;
    uint32_t m = field_mask(msb, lsb);
    if (val > (m >> lsb)) return -ERANGE;

    uint32_t cur = reg_read(rd->off);
    uint32_t keep = cur & ~m & ~(rd->w1c_mask | rd->sc_mask);   /* never echo W1C/SC 1s */
    reg_write(rd->off, keep | field_prep(val, msb, lsb));
    return 0;
}

static size_t g_nmm;              /* mismatch counter for the current test run */

static void log_mm(Mismatch *log, size_t cap, uint32_t off, MmKind k,
                   uint32_t wrote, uint32_t expect, uint32_t got)
{
    if (g_nmm < cap) log[g_nmm] = (Mismatch){ off, k, wrote, expect, got };
    g_nmm++;
}

/* write, read back, compare; classify by which bits differ */
static void wr_check(const RegDesc *rd, uint32_t w, uint32_t expect, Mismatch *log, size_t cap)
{
    reg_write(rd->off, w);
    uint32_t got = reg_read(rd->off);
    uint32_t diff = got ^ expect;
    if (diff) log_mm(log, cap, rd->off, (diff & rd->rw_mask) ? MM_RW : MM_RO, w, expect, got);
}

size_t reg_sanity_test(const RegDesc *tbl, size_t n, Mismatch *log, size_t log_cap)
{
    g_nmm = 0;
    for (size_t i = 0; i < n; i++) {
        const RegDesc *rd = &tbl[i];
        if (rd->flags & REG_F_SKIP) continue;            /* FIFO pop, read-to-clear, ... */
        uint32_t rw = rd->rw_mask;
        uint32_t ro = ~(rw | rd->w1c_mask | rd->sc_mask); /* includes reserved bits */

        /* 1. reset value */
        uint32_t orig = reg_read(rd->off);
        if (orig != rd->reset) log_mm(log, log_cap, rd->off, MM_RESET, 0, rd->reset, orig);

        /* 2. RO bits must ignore writes */
        wr_check(rd, (orig & rw) | (~orig & ro), orig, log, log_cap);

        /* 3. RW bits: checkerboards, then walking-1 */
        if (rw) {
            static const uint32_t pats[] = { 0x55555555u, 0xAAAAAAAAu };
            for (size_t k = 0; k < 2; k++)
                wr_check(rd, (pats[k] & rw) | (orig & ro), (pats[k] & rw) | (orig & ~rw), log, log_cap);
            for (unsigned b = 0; b < 32; b++) {
                uint32_t p = 1u << b;
                if (!(rw & p)) continue;
                wr_check(rd, p | (orig & ro), p | (orig & ~rw), log, log_cap);
            }
        }

        /* 4. restore */
        reg_write(rd->off, orig & ~(rd->w1c_mask | rd->sc_mask));
    }
    return g_nmm;
}

/* =====================================================================
 * HW model of the SIF block + fault injection (do not edit)
 * ===================================================================== */
#define NREG 7
static struct {
    uint32_t val[NREG];
    uint32_t reset_bug[NREG];     /* XOR applied to the reset value (wrong reset) */
    uint32_t stuck0[NREG], stuck1[NREG];
    uint32_t ro_leak[NREG];       /* RO bits that are wrongly writable */
    uint32_t writes, bad_access, soft_resets, fifo_pops;
} H;

static uint32_t hw_rw(size_t i)  { return SIF_REGS[i].rw_mask; }
static uint32_t hw_w1c(size_t i) { return SIF_REGS[i].w1c_mask; }

static uint32_t hw_fix(size_t i, uint32_t v) { return (v | H.stuck1[i]) & ~H.stuck0[i]; }

static void hw_block_reset(void)
{
    for (size_t i = 0; i < NREG; i++) H.val[i] = hw_fix(i, SIF_REGS[i].reset ^ H.reset_bug[i]);
}

static void hw_power_on(void)                 /* clears faults too */
{
    memset(&H, 0, sizeof H);
    hw_block_reset();
}

static void hw_raise(uint32_t off, uint32_t bits) { H.val[off / 4] |= bits; }   /* HW sets status */

static int hw_index(uint32_t off)
{
    if ((off & 3u) || off / 4 >= NREG) { H.bad_access++; return -1; }
    return (int)(off / 4);
}

uint32_t reg_read(uint32_t off)
{
    int i = hw_index(off);
    if (i < 0) return 0xDEADBEEFu;
    if (off == SIF_FIFO) H.fifo_pops++;          /* read side effect */
    return H.val[i];
}

void reg_write(uint32_t off, uint32_t val)
{
    int i = hw_index(off);
    if (i < 0) return;
    H.writes++;
    if (off == SIF_CTRL && (val & 0x80000000u)) { H.soft_resets++; hw_block_reset(); return; }
    uint32_t rw = hw_rw((size_t)i) | H.ro_leak[i];
    uint32_t nv = (H.val[i] & ~rw) | (val & rw);
    nv &= ~(val & hw_w1c((size_t)i));             /* W1C */
    H.val[i] = hw_fix((size_t)i, nv);             /* reserved bits never stored */
}

static bool regs_at_reset(void)
{
    for (size_t i = 0; i < NREG; i++)
        if (H.val[i] != hw_fix(i, SIF_REGS[i].reset ^ H.reset_bug[i])) return false;
    return true;
}

int main(void)
{
    const RegDesc *CTRL = &SIF_REGS[1], *IRQ = &SIF_REGS[2];
    Mismatch log[4];

    /* 1. field helpers, including the full-width and single-bit edges */
    CHECK(field_mask(15, 8) == 0x0000FF00u);
    CHECK(field_mask(0, 0) == 0x00000001u);
    CHECK(field_mask(31, 31) == 0x80000000u);
    CHECK(field_mask(31, 0) == 0xFFFFFFFFu);
    CHECK(field_get(0x0000AB0Fu, 15, 8) == 0xABu);
    CHECK(field_get(0x0000AB0Fu, 3, 1) == 0x7u);
    CHECK(field_get(0xDEADBEEFu, 31, 0) == 0xDEADBEEFu);
    CHECK(field_prep(0x3u, 3, 1) == 0x6u);
    CHECK(field_prep(0x1FFu, 15, 8) == 0xFF00u);          /* truncated to the field */

    /* 2. field_write: normal RMW keeps the other fields */
    hw_power_on();
    CHECK(field_write(CTRL, 0, 0, 1) == 0);               /* EN = 1 */
    CHECK(field_write(CTRL, 3, 1, 5) == 0);               /* MODE = 5 */
    CHECK(field_write(CTRL, 15, 8, 0x10) == 0);           /* DIV = 0x10 */
    CHECK(reg_read(SIF_CTRL) == 0x0000100Bu);
    CHECK(H.soft_resets == 0);

    /* 3. W1C pitfall: HW has pending bits set; changing IRQ_EN must not clear them */
    hw_power_on();
    hw_raise(SIF_IRQ, 0x7u);
    CHECK(field_write(IRQ, 18, 16, 0x5) == 0);
    CHECK(reg_read(SIF_IRQ) == 0x00050007u);              /* naive RMW gives 0x00050000 */
    CHECK(field_write(IRQ, 1, 1, 1) == 0);                /* ack only pending bit 1 */
    CHECK(reg_read(SIF_IRQ) == 0x00050005u);

    /* 4. bad arguments: error code and NO bus write */
    hw_power_on();
    CHECK(field_write(CTRL, 7, 8, 0) == -EINVAL);
    CHECK(field_write(CTRL, 32, 0, 0) == -EINVAL);
    CHECK(field_write(CTRL, 3, 1, 8) == -ERANGE);         /* 3-bit field max 7 */
    CHECK(H.writes == 0 && reg_read(SIF_CTRL) == 0x00000400u);
    CHECK(field_write(&SIF_REGS[5], 31, 0, 0xFFFFFFFFu) == 0 && reg_read(SIF_SCRATCH) == 0xFFFFFFFFu);

    /* 5. sanity test on good HW: 0 mismatches, no side effects, everything restored */
    hw_power_on();
    CHECK(reg_sanity_test(SIF_REGS, N_SIF_REGS, log, 4) == 0);
    CHECK(H.soft_resets == 0);                            /* SOFT_RST never written as 1 */
    CHECK(H.fifo_pops == 0);                              /* FIFO register skipped */
    CHECK(H.bad_access == 0);
    CHECK(regs_at_reset());

    /* 6. injected bugs: SCRATCH bit 13 stuck-at-0, THRESH reset value wrong (bit 7) */
    hw_power_on();
    H.stuck0[SIF_SCRATCH / 4] = 1u << 13;
    H.reset_bug[SIF_THRESH / 4] = 0x80u;
    hw_block_reset();
    memset(log, 0, sizeof log);
    CHECK(reg_sanity_test(SIF_REGS, N_SIF_REGS, log, 2) == 3);   /* total, even if log is small */
    CHECK(log[0].off == SIF_THRESH && log[0].kind == MM_RESET &&
          log[0].expect == 0x08000100u && log[0].got == 0x08000180u);
    CHECK(log[1].off == SIF_SCRATCH && log[1].kind == MM_RW &&
          log[1].wrote == 0xAAAAAAAAu && log[1].got == 0xAAAA8AAAu);
    CHECK(log[2].off == 0 && log[2].kind == 0);           /* nothing written past log_cap */
    CHECK(reg_sanity_test(SIF_REGS, N_SIF_REGS, NULL, 0) == 3);
    CHECK(regs_at_reset());

    /* 7. RO bit wrongly writable (ID bit 0): one MM_RO mismatch, ID restored */
    hw_power_on();
    H.ro_leak[SIF_ID / 4] = 1u;
    CHECK(reg_sanity_test(SIF_REGS, N_SIF_REGS, log, 4) == 1);
    CHECK(log[0].off == SIF_ID && log[0].kind == MM_RO &&
          log[0].wrote == 0xA5E2FEFDu && log[0].got == 0x5A1D0103u);
    CHECK(reg_read(SIF_ID) == 0x5A1D0102u);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
