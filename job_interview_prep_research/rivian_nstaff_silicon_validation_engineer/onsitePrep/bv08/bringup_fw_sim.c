/*
 * BV08 lab: a host-runnable mini "bring-up test firmware".
 *
 *  - platform HAL: same tests run on "fpga_haps" (40x slower) and
 *    "silicon_a0"; timeouts are scaled by the platform slowdown factor
 *  - test registry: {name, fn, timeout_us, flags}
 *  - machine-parseable result lines: "@@TEST id=.. name=.. result=.. ..."
 *  - register tests against a MOCK register block with RO / W1C /
 *    self-clearing / reserved / read-side-effect bits:
 *      reg_reset_values, reg_rw_walk, reg_alias_unique
 *  - W1C status + IRQ test, a polled operation with a deadline (TIMEOUT
 *    result + recovery via self-clearing soft reset)
 *  - a tiny UART-style command shell fed from a scripted "RX" list
 *  - injectable HW bugs: stuck-at-0 bit, wrong reset value, address alias,
 *    operation that never completes
 *
 * Build (macOS): cc -isysroot <SDK> -std=c11 -Wall -Wextra -Werror \
 *                   -fsanitize=address,undefined bringup_fw_sim.c
 * On a real target hw_rd/hw_wr would be volatile MMIO accesses and
 * hal_delay_us a timer poll; everything above the HAL stays the same.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------ platform HAL ------------------------- */
struct platform {
    const char *name;
    unsigned slowdown; /* how many times slower than silicon */
};
static const struct platform PLAT_SI = {"silicon_a0", 1};
static const struct platform PLAT_FPGA = {"fpga_haps", 40};
static const struct platform *plat = &PLAT_SI;
static bool scale_timeouts = true;
static uint64_t now_us; /* simulated time */

static void hal_delay_us(unsigned us) { now_us += us; }
static uint64_t hal_deadline(unsigned budget_si_us)
{
    return now_us + (uint64_t)budget_si_us * (scale_timeouts ? plat->slowdown : 1);
}

/* ------------------------------ mock IP block "IPX" ------------------ */
enum { R_ID = 0x00, R_CTRL = 0x04, R_STATUS = 0x08, R_CFG = 0x0C,
       R_IRQ_EN = 0x10, R_DATA = 0x14, R_FIFO = 0x18, R_SCRATCH = 0x1C,
       NREGS = 8 };

#define CTRL_START (1u << 0)
#define CTRL_SOFT_RST (1u << 31) /* self-clearing */
#define ST_DONE (1u << 0)        /* W1C */
#define ST_ERR (1u << 1)         /* W1C */
#define ST_BUSY (1u << 4)        /* RO, volatile */

enum { F_NONE = 0, F_RD_SIDE_EFFECT = 1, F_VOLATILE = 2 };

struct regdesc {
    uint32_t off;
    const char *name;
    uint32_t reset, reset_mask; /* bits whose reset value is defined */
    uint32_t rw, ro, w1c, sc;   /* access types; anything else = reserved */
    unsigned flags;
};

static const struct regdesc regs[NREGS] = {
    /*  off       name      reset        rmask        rw           ro           w1c   sc            flags */
    {R_ID,      "ID",      0x1A2B0100u, 0xFFFFFFFFu, 0,           0xFFFFFFFFu, 0,    0,            F_NONE},
    {R_CTRL,    "CTRL",    0x00000000u, 0xFFFFFFFFu, 0x000000FEu, 0,           0,    0x80000001u,  F_NONE},
    {R_STATUS,  "STATUS",  0x00000000u, 0xFFFFFFEFu, 0,           ST_BUSY,     0x3u, 0,            F_VOLATILE},
    {R_CFG,     "CFG",     0x00000040u, 0xFFFFFFFFu, 0x00FFFFFFu, 0,           0,    0,            F_NONE},
    {R_IRQ_EN,  "IRQ_EN",  0x00000000u, 0xFFFFFFFFu, 0x0000000Fu, 0,           0,    0,            F_NONE},
    {R_DATA,    "DATA",    0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,           0,    0,            F_NONE},
    {R_FIFO,    "FIFO_POP",0x00000000u, 0,           0,           0xFFFFFFFFu, 0,    0,            F_RD_SIDE_EFFECT},
    {R_SCRATCH, "SCRATCH", 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,           0,    0,            F_NONE},
};

/* injectable bugs */
enum { BUG_CFG_BIT13_STUCK0 = 1, BUG_IRQEN_RESET = 2, BUG_ALIAS_A3 = 4,
       BUG_OP_NEVER_DONE = 8 };
static unsigned bugs;

static uint32_t hwreg[NREGS];
static uint64_t op_done_at; /* 0 = no op in flight */
static unsigned fifo_pops;
static bool irq_line;

static void hw_reset_block(void)
{
    for (int i = 0; i < NREGS; i++)
        hwreg[i] = regs[i].reset;
    if (bugs & BUG_IRQEN_RESET)
        hwreg[R_IRQ_EN / 4] = 0x1; /* silicon came out with IRQ0 enabled */
    op_done_at = 0;
    irq_line = false;
}

static unsigned idx_of(uint32_t off)
{
    if ((bugs & BUG_ALIAS_A3) && off == R_SCRATCH)
        off &= ~0x8u; /* address bit 3 not decoded -> SCRATCH hits DATA */
    return off / 4;
}

static void hw_tick(void)
{
    if (op_done_at && now_us >= op_done_at) {
        unsigned s = R_STATUS / 4;
        hwreg[s] = (hwreg[s] & ~ST_BUSY) | ST_DONE;
        op_done_at = 0;
    }
    irq_line = (hwreg[R_STATUS / 4] & 0x3u & hwreg[R_IRQ_EN / 4]) != 0;
}

static uint32_t hw_rd(uint32_t off)
{
    hw_tick();
    unsigned i = idx_of(off);
    if (i >= NREGS)
        return 0xDEADBEEFu; /* bus error pattern */
    if (regs[i].flags & F_RD_SIDE_EFFECT)
        fifo_pops++;
    return hwreg[i];
}

static void hw_wr(uint32_t off, uint32_t v)
{
    hw_tick();
    unsigned i = idx_of(off);
    if (i >= NREGS)
        return;
    const struct regdesc *d = &regs[i];
    uint32_t cur = hwreg[i];
    uint32_t nv = (cur & ~d->rw) | (v & d->rw); /* RW bits take new value */
    nv &= ~(v & d->w1c);                       /* W1C: 1 clears, 0 no-op */
    if (i == R_CFG / 4 && (bugs & BUG_CFG_BIT13_STUCK0))
        nv &= ~(1u << 13);
    if (i == R_CTRL / 4) {
        if (v & CTRL_SOFT_RST) { hw_reset_block(); return; }
        if (v & CTRL_START) {
            hwreg[R_STATUS / 4] |= ST_BUSY;
            op_done_at = (bugs & BUG_OP_NEVER_DONE)
                             ? 0
                             : now_us + 50u * plat->slowdown; /* 50 us on Si */
        }
        nv &= ~d->sc; /* self-clearing bits never read back as 1 */
    }
    hwreg[i] = nv;
    hw_tick();
}

/* ------------------------------ test framework ----------------------- */
enum result { R_PASS, R_FAIL, R_TIMEOUT, R_SKIP };
static const char *res_str[] = {"PASS", "FAIL", "TIMEOUT", "SKIP"};

struct tctx {
    char detail[160];
    unsigned nerr;
    unsigned timeout_us;
};

#define TFAIL(c, ...)                                                       \
    do {                                                                    \
        if ((c)->nerr++ == 0)                                               \
            snprintf((c)->detail, sizeof (c)->detail, __VA_ARGS__);         \
        printf("#   " __VA_ARGS__);                                         \
        printf("\n");                                                       \
    } while (0)

typedef enum result (*test_fn)(struct tctx *);
struct test {
    const char *name;
    test_fn fn;
    unsigned timeout_us; /* budget on silicon; scaled by HAL on FPGA */
};

/* T1: every register with a defined reset value reads it after reset */
static enum result t_reg_reset_values(struct tctx *c)
{
    hw_wr(R_CTRL, CTRL_SOFT_RST);
    for (int i = 0; i < NREGS; i++) {
        const struct regdesc *d = &regs[i];
        if (d->flags & F_RD_SIDE_EFFECT)
            continue; /* never read FIFO/clear-on-read regs in a sweep */
        uint32_t got = hw_rd(d->off) & d->reset_mask;
        uint32_t exp = d->reset & d->reset_mask;
        if (got != exp)
            TFAIL(c, "%s@0x%02X reset got 0x%08X exp 0x%08X (mask 0x%08X)",
                  d->name, d->off, got, exp, d->reset_mask);
    }
    return c->nerr ? R_FAIL : R_PASS;
}

/* T2: walking-1 / walking-0 over RW bits only; RO bits must not move */
static enum result t_reg_rw_walk(struct tctx *c)
{
    for (int i = 0; i < NREGS; i++) {
        const struct regdesc *d = &regs[i];
        if (!d->rw || (d->flags & F_RD_SIDE_EFFECT))
            continue;
        uint32_t save = hw_rd(d->off);
        uint32_t ro_before = save & d->ro;
        uint32_t stuck0 = 0, stuck1 = 0; /* accumulate, report once/reg */
        for (int b = 0; b < 32; b++) {
            uint32_t m = 1u << b;
            if (!(d->rw & m))
                continue; /* skip RO/W1C/SC/reserved bits */
            uint32_t pats[2] = {m, d->rw & ~m};
            for (int p = 0; p < 2; p++) {
                /* write only RW bits; W1C bits get 0 (no-op), SC bits 0 */
                hw_wr(d->off, pats[p]);
                uint32_t got = hw_rd(d->off) & d->rw;
                stuck0 |= pats[p] & ~got; /* wrote 1, read 0 */
                stuck1 |= ~pats[p] & got; /* wrote 0, read 1 */
            }
        }
        if (stuck0 || stuck1)
            TFAIL(c, "%s@0x%02X RW bits wrote1-read0=0x%08X wrote0-read1=0x%08X",
                  d->name, d->off, stuck0, stuck1);
        if ((hw_rd(d->off) & d->ro) != ro_before)
            TFAIL(c, "%s RO bits changed after writes", d->name);
        hw_wr(d->off, save & d->rw); /* restore */
    }
    return c->nerr ? R_FAIL : R_PASS;
}

/* T3: unique value per register, write ALL then read ALL -> catches
 * address aliasing that a per-register test cannot see */
static enum result t_reg_alias_unique(struct tctx *c)
{
    for (int i = 0; i < NREGS; i++)
        if (regs[i].rw && !(regs[i].flags & F_RD_SIDE_EFFECT))
            hw_wr(regs[i].off, (0xA5000000u | regs[i].off << 8 | regs[i].off) &
                                   regs[i].rw);
    for (int i = 0; i < NREGS; i++) {
        const struct regdesc *d = &regs[i];
        if (!d->rw || (d->flags & F_RD_SIDE_EFFECT))
            continue;
        uint32_t exp = (0xA5000000u | d->off << 8 | d->off) & d->rw;
        uint32_t got = hw_rd(d->off) & d->rw;
        if (got != exp)
            TFAIL(c, "%s@0x%02X got 0x%08X exp 0x%08X (aliased by a later write?)",
                  d->name, d->off, got, exp);
    }
    hw_wr(R_CTRL, CTRL_SOFT_RST);
    return c->nerr ? R_FAIL : R_PASS;
}

/* polled wait with a deadline from the HAL (never an unbounded while) */
static bool wait_bits(uint32_t off, uint32_t mask, unsigned budget_us)
{
    uint64_t dl = hal_deadline(budget_us);
    while (!(hw_rd(off) & mask)) {
        if (now_us >= dl)
            return false;
        hal_delay_us(5);
    }
    return true;
}

/* T4: start op -> DONE (W1C) -> IRQ line -> write 0 must NOT clear,
 * write 1 must clear -> IRQ deasserts */
static enum result t_irq_w1c(struct tctx *c)
{
    hw_wr(R_CTRL, CTRL_SOFT_RST);
    hw_wr(R_IRQ_EN, 0x1);
    hw_wr(R_CTRL, CTRL_START);
    if (hw_rd(R_CTRL) & CTRL_START)
        TFAIL(c, "CTRL.START did not self-clear");
    if (!wait_bits(R_STATUS, ST_DONE, c->timeout_us)) {
        TFAIL(c, "STATUS.DONE not set: budget %u us x scale %u, BUSY=%u",
              c->timeout_us, scale_timeouts ? plat->slowdown : 1,
              (hw_rd(R_STATUS) & ST_BUSY) ? 1u : 0u);
        hw_wr(R_CTRL, CTRL_SOFT_RST); /* recover the block for next test */
        return R_TIMEOUT;
    }
    hw_tick();
    if (!irq_line)
        TFAIL(c, "IRQ line not asserted with DONE=1 and IRQ_EN=1");
    hw_wr(R_STATUS, 0x0); /* writing 0 to W1C: no effect */
    if (!(hw_rd(R_STATUS) & ST_DONE))
        TFAIL(c, "DONE cleared by writing 0 (should be W1C)");
    hw_wr(R_STATUS, ST_DONE);
    if (hw_rd(R_STATUS) & ST_DONE)
        TFAIL(c, "DONE not cleared by W1C");
    hw_tick();
    if (irq_line)
        TFAIL(c, "IRQ still asserted after clearing DONE");
    return c->nerr ? R_FAIL : R_PASS;
}

/* T5: FIFO read has side effects -> test is SKIPPED in sweeps; here we
 * only check that the sweep tests did not pop it */
static enum result t_no_side_effect_reads(struct tctx *c)
{
    if (fifo_pops)
        TFAIL(c, "register sweep popped FIFO %u times", fifo_pops);
    return c->nerr ? R_FAIL : R_PASS;
}

static const struct test registry[] = {
    {"reg_reset_values", t_reg_reset_values, 100},
    {"reg_rw_walk", t_reg_rw_walk, 1000},
    {"reg_alias_unique", t_reg_alias_unique, 100},
    {"irq_w1c_done", t_irq_w1c, 200},
    {"no_side_effect_reads", t_no_side_effect_reads, 10},
};
#define NTESTS (sizeof registry / sizeof registry[0])

static unsigned cnt[4];

static void run_one(unsigned id)
{
    struct tctx c = {.detail = "", .nerr = 0, .timeout_us = registry[id].timeout_us};
    uint64_t t0 = now_us;
    enum result r = registry[id].fn(&c);
    cnt[r]++;
    printf("@@TEST id=%u name=%s result=%s dur_us=%llu errors=%u detail=\"%s\"\n",
           id, registry[id].name, res_str[r],
           (unsigned long long)(now_us - t0), c.nerr, c.detail);
}

static void run_all(void)
{
    memset(cnt, 0, sizeof cnt);
    printf("@@BEGIN plat=%s slowdown=%u scale_timeouts=%d bugs=0x%X ntests=%zu\n",
           plat->name, plat->slowdown, scale_timeouts, bugs, NTESTS);
    hw_reset_block();
    fifo_pops = 0;
    for (unsigned i = 0; i < NTESTS; i++)
        run_one(i);
    printf("@@SUMMARY total=%zu pass=%u fail=%u timeout=%u skip=%u verdict=%s\n",
           NTESTS, cnt[R_PASS], cnt[R_FAIL], cnt[R_TIMEOUT], cnt[R_SKIP],
           (cnt[R_FAIL] || cnt[R_TIMEOUT]) ? "FAIL" : "PASS");
    printf("@@END\n");
}

/* ------------------------------ UART-style shell --------------------- */
static void shell_cmd(const char *line)
{
    char cmd[16] = "", arg1[32] = "", arg2[32] = "";
    printf("bv> %s\n", line);
    if (sscanf(line, "%15s %31s %31s", cmd, arg1, arg2) < 1)
        return;
    if (!strcmp(cmd, "list")) {
        for (unsigned i = 0; i < NTESTS; i++)
            printf("  %u %-22s timeout=%uus\n", i, registry[i].name,
                   registry[i].timeout_us);
    } else if (!strcmp(cmd, "run")) {
        if (!strcmp(arg1, "all")) { run_all(); return; }
        for (unsigned i = 0; i < NTESTS; i++)
            if (!strcmp(arg1, registry[i].name)) { run_one(i); return; }
        printf("  ERR unknown test '%s'\n", arg1);
    } else if (!strcmp(cmd, "rd")) {
        uint32_t off = (uint32_t)strtoul(arg1, NULL, 0);
        printf("  [0x%02X] = 0x%08X\n", off, hw_rd(off));
    } else if (!strcmp(cmd, "wr")) {
        uint32_t off = (uint32_t)strtoul(arg1, NULL, 0);
        uint32_t v = (uint32_t)strtoul(arg2, NULL, 0);
        hw_wr(off, v);
        printf("  [0x%02X] <= 0x%08X ; readback 0x%08X\n", off, v, hw_rd(off));
    } else {
        printf("  cmds: list | run <name|all> | rd <off> | wr <off> <val>\n");
    }
}

int main(void)
{
    printf("===== 1) clean IP on silicon =====\n");
    plat = &PLAT_SI; bugs = 0; scale_timeouts = true;
    run_all();

    printf("\n===== 2) clean IP on FPGA, timeouts scaled by HAL =====\n");
    plat = &PLAT_FPGA;
    run_all();

    printf("\n===== 3) clean IP on FPGA, timeouts NOT scaled (common mistake) =====\n");
    scale_timeouts = false;
    run_all();

    printf("\n===== 4) A0 silicon with injected bugs =====\n");
    plat = &PLAT_SI; scale_timeouts = true;
    bugs = BUG_CFG_BIT13_STUCK0 | BUG_IRQEN_RESET | BUG_ALIAS_A3 | BUG_OP_NEVER_DONE;
    run_all();

    printf("\n===== 5) UART shell session on the buggy part =====\n");
    hw_reset_block();
    static const char *rx[] = {"help", "list", "wr 0x0c 0x2000", "wr 0x14 0x11111111",
                               "wr 0x1c 0x22222222", "rd 0x14", "run reg_reset_values", "run foo"};
    for (size_t i = 0; i < sizeof rx / sizeof rx[0]; i++)
        shell_cmd(rx[i]);
    return 0;
}
