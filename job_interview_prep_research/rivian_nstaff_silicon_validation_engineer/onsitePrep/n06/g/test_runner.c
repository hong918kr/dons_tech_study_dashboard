/*
 * N06G mini bare-metal-style test runner (host-simulated).
 *
 * "Write the test once, run it everywhere": the tests below never touch a
 * platform constant. Addresses come from plat->map, timeouts are written in
 * SILICON microseconds and multiplied by plat->timeout_scale, blocks missing
 * on a platform are SKIPped via plat->caps or served by a stub driver
 * (result tagged stub=1 = no coverage credit).
 *
 * Everything hardware-ish is faked: a virtual time base (g_now_ns) that only
 * the device model advances, a tiny MMIO bus that decodes addresses against
 * the platform address map, and device models for SYSCTL / DMA / ECC scrub /
 * Ethernet PHY / PLL / temp sensor.  Results leave the "chip" as one
 * machine-parseable line per test on stdout (= UART) plus a results mailbox
 * (= shared memory a JTAG host could read if the UART is dead).
 *
 * Usage: test_runner --plat fpga|silicon [--seed N] [--repeat N] [--build TAG]
 *                    [--inject dma_bitflip] [--mutate skip_compare]
 *                    [--no-timeout-scale] [--only TEST_ID]
 * Exit: 0 = all executed tests PASS, 1 = any FAIL/TIMEOUT, 2 = usage error
 *       (0/1 match `git bisect run` good/bad; 125 would mean "skip build").
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* fake time base: only the device model / hal_delay advance it        */
/* ------------------------------------------------------------------ */
static uint64_t g_now_ns;

/* ------------------------------------------------------------------ */
/* platform HAL                                                        */
/* ------------------------------------------------------------------ */
enum {
    CAP_PLL         = 1u << 0, /* real PLL (FPGA: fixed MMCM clock)          */
    CAP_ETH_PHY     = 1u << 1, /* analog PHY (FPGA: absent)                   */
    CAP_CLK_GATING  = 1u << 2, /* per-block clock gates actually gate         */
    CAP_TEMP_ANALOG = 1u << 3, /* on-die temp sensor (FPGA: stub driver)      */
};

typedef struct {
    uint32_t sysctl, dma, ecc, eth; /* block base addresses */
} addr_map_t;

typedef struct platform {
    const char *name;
    uint32_t slowdown;      /* HW time multiplier vs silicon (FPGA ~20x slower) */
    uint32_t timeout_scale; /* silicon-us timeouts x this (slowdown x 2 margin) */
    addr_map_t map;         /* address map differs per platform                 */
    uint32_t caps;
    uint32_t sysctl_ctrl_reset; /* reset value as built (FPGA init vs Si tree)  */
    int (*temp_read_mc)(const struct platform *p, int32_t *mc, int *is_stub);
} platform_t;

static int temp_real(const platform_t *p, int32_t *mc, int *is_stub);
static int temp_stub(const platform_t *p, int32_t *mc, int *is_stub);

static const platform_t PLAT_FPGA = {
    .name = "FPGA", .slowdown = 20, .timeout_scale = 40,
    .map = { .sysctl = 0x40000000u, .dma = 0x40010000u, .ecc = 0x40020000u, .eth = 0 },
    .caps = 0, /* no PLL, no PHY, clock gates not modeled, no analog */
    .sysctl_ctrl_reset = 0x0,
    .temp_read_mc = temp_stub,
};
static const platform_t PLAT_SILICON = {
    .name = "SILICON", .slowdown = 1, .timeout_scale = 1,
    .map = { .sysctl = 0x1A000000u, .dma = 0x1A100000u, .ecc = 0x1A200000u, .eth = 0x1A300000u },
    .caps = CAP_PLL | CAP_ETH_PHY | CAP_CLK_GATING | CAP_TEMP_ANALOG,
    .sysctl_ctrl_reset = 0x1, /* A0 reset tree: bit0 comes up set (spec says 0) */
    .temp_read_mc = temp_real,
};

static const platform_t *g_plat;
static bool g_scale_timeouts = true;
static bool g_inject_bitflip; /* campaign-wide fault injection               */
static bool g_inject_once;    /* used by the built-in checker self-test      */
static bool g_mutate_skip_compare;

/* register offsets (invented for this mock, not any real IP) */
enum { SYS_ID = 0x00, SYS_CTRL = 0x04, SYS_SCRATCH = 0x08, SYS_CLKEN = 0x0C,
       SYS_PLL_CTRL = 0x10, SYS_PLL_STAT = 0x14, SYS_TEMP = 0x18 };
enum { DMA_SRC = 0x00, DMA_DST = 0x04, DMA_LEN = 0x08, DMA_CTRL = 0x0C, DMA_STAT = 0x10 };
enum { ECC_CTRL = 0x00, ECC_STAT = 0x04 };
enum { ETH_CTRL = 0x00, ETH_STAT = 0x04 };
enum { CLKEN_DMA = 1u << 0, CLKEN_ECC = 1u << 1 };
#define CHIP_ID 0x0C0FFEE1u
#define BUS_DECODE_ERR 0xDEADBEEFu

/* ------------------------------------------------------------------ */
/* rng (xorshift32) - every random choice derives from the test seed    */
/* ------------------------------------------------------------------ */
static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ------------------------------------------------------------------ */
/* device model                                                        */
/* ------------------------------------------------------------------ */
#define DRAM_SIZE (64u * 1024u)
static uint8_t g_dram[DRAM_SIZE];

static struct {
    uint32_t sys_ctrl, scratch, clken, pll_ctrl;
    uint64_t pll_lock_at;
    uint32_t dma_src, dma_dst, dma_len, dma_ctrl, dma_stat;
    uint64_t dma_done_at;
    uint32_t ecc_ctrl, ecc_stat;
    uint64_t ecc_done_at;
    uint32_t eth_ctrl, eth_stat;
    uint64_t eth_up_at;
    uint32_t hw_rng; /* models analog randomness, seeded per test */
} dev;

static void dev_reset(uint32_t seed)
{
    memset(&dev, 0, sizeof dev);
    dev.sys_ctrl = g_plat->sysctl_ctrl_reset;
    dev.hw_rng = seed ^ 0xA5A5A5A5u;
}

static uint64_t hw_ns(uint64_t silicon_ns) { return silicon_ns * g_plat->slowdown; }

static void dev_update(void)
{
    if ((dev.dma_ctrl & 1u) && !(dev.dma_stat & 1u) && g_now_ns >= dev.dma_done_at) {
        if (dev.dma_src + dev.dma_len > DRAM_SIZE || dev.dma_dst + dev.dma_len > DRAM_SIZE) {
            dev.dma_stat = 1u | 2u; /* done + error */
        } else {
            memmove(&g_dram[dev.dma_dst], &g_dram[dev.dma_src], dev.dma_len);
            if ((g_inject_bitflip || g_inject_once) && dev.dma_len) {
                uint32_t bit = rng_next(&dev.hw_rng) % (dev.dma_len * 8u);
                g_dram[dev.dma_dst + bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
                g_inject_once = false;
            }
            dev.dma_stat = 1u;
        }
        dev.dma_ctrl &= ~1u;
    }
    if ((dev.ecc_ctrl & 1u) && g_now_ns >= dev.ecc_done_at) {
        /* on silicon the scrub engine only runs if its clock is enabled */
        bool clk_ok = !(g_plat->caps & CAP_CLK_GATING) || (dev.clken & CLKEN_ECC);
        if (clk_ok) { dev.ecc_stat = 1u; dev.ecc_ctrl &= ~1u; }
    }
    if ((dev.eth_ctrl & 1u) && g_now_ns >= dev.eth_up_at) dev.eth_stat = 1u;
}

static void hal_delay_ns(uint64_t ns) { g_now_ns += ns; dev_update(); }

/* MMIO: decode against the CURRENT platform map; a hard-coded address from
 * another platform's map lands nowhere and reads BUS_DECODE_ERR. */
static int decode(uint32_t addr, char *blk, uint32_t *off)
{
    const addr_map_t *m = &g_plat->map;
    const struct { char b; uint32_t base; } t[] = {
        { 'S', m->sysctl }, { 'D', m->dma }, { 'E', m->ecc }, { 'P', m->eth } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].base && addr >= t[i].base && addr < t[i].base + 0x100u) {
            *blk = t[i].b; *off = addr - t[i].base; return 0;
        }
    return -1;
}

static uint32_t mmio_read(uint32_t addr)
{
    char b; uint32_t o;
    hal_delay_ns(hw_ns(100)); /* bus access cost */
    if (decode(addr, &b, &o)) return BUS_DECODE_ERR;
    switch (b) {
    case 'S':
        switch (o) {
        case SYS_ID: return CHIP_ID;
        case SYS_CTRL: return dev.sys_ctrl;
        case SYS_SCRATCH: return dev.scratch;
        case SYS_CLKEN: return dev.clken;
        case SYS_PLL_STAT: return (dev.pll_ctrl & 1u) && g_now_ns >= dev.pll_lock_at;
        case SYS_TEMP: return 48000u + rng_next(&dev.hw_rng) % 2000u; /* mC */
        default: return 0;
        }
    case 'D':
        switch (o) {
        case DMA_SRC: return dev.dma_src;  case DMA_DST: return dev.dma_dst;
        case DMA_LEN: return dev.dma_len;  case DMA_CTRL: return dev.dma_ctrl;
        case DMA_STAT: return dev.dma_stat; default: return 0;
        }
    case 'E': return o == ECC_STAT ? dev.ecc_stat : (o == ECC_CTRL ? dev.ecc_ctrl : 0);
    case 'P': return o == ETH_STAT ? dev.eth_stat : (o == ETH_CTRL ? dev.eth_ctrl : 0);
    default: return BUS_DECODE_ERR;
    }
}

static void mmio_write(uint32_t addr, uint32_t v)
{
    char b; uint32_t o;
    hal_delay_ns(hw_ns(100));
    if (decode(addr, &b, &o)) return;
    switch (b) {
    case 'S':
        if (o == SYS_CTRL) dev.sys_ctrl = v;
        else if (o == SYS_SCRATCH) dev.scratch = v;
        else if (o == SYS_CLKEN) dev.clken = v;
        else if (o == SYS_PLL_CTRL) { dev.pll_ctrl = v; dev.pll_lock_at = g_now_ns + hw_ns(30000); }
        break;
    case 'D':
        if (o == DMA_SRC) dev.dma_src = v;
        else if (o == DMA_DST) dev.dma_dst = v;
        else if (o == DMA_LEN) dev.dma_len = v;
        else if (o == DMA_STAT) dev.dma_stat &= ~v; /* W1C */
        else if (o == DMA_CTRL) {
            dev.dma_ctrl = v;
            if (v & 1u) { /* 500 ns setup + 4 B/ns on silicon */
                dev.dma_stat = 0;
                dev.dma_done_at = g_now_ns + hw_ns(500 + dev.dma_len / 4u);
            }
        }
        break;
    case 'E':
        if (o == ECC_CTRL) { dev.ecc_ctrl = v; if (v & 1u) dev.ecc_done_at = g_now_ns + hw_ns(20000); }
        break;
    case 'P':
        if (o == ETH_CTRL) { /* link-up 2..6 ms: analog lottery -> occasionally slow */
            dev.eth_ctrl = v;
            if (v & 1u) dev.eth_up_at = g_now_ns + 2000000ull + (rng_next(&dev.hw_rng) % 4000u) * 1000ull;
        }
        break;
    default: break;
    }
}

/* timeouts are given in SILICON us; the HAL scales them per platform */
static uint64_t scaled_us(uint32_t si_us)
{
    return (uint64_t)si_us * (g_scale_timeouts ? g_plat->timeout_scale : 1u);
}

/* poll until (reg & mask) == want; 0 = ok, -1 = timeout */
static int hal_poll(uint32_t addr, uint32_t mask, uint32_t want, uint32_t si_timeout_us)
{
    uint64_t deadline = g_now_ns + scaled_us(si_timeout_us) * 1000u;
    for (;;) {
        if ((mmio_read(addr) & mask) == want) return 0;
        if (g_now_ns >= deadline) return -1;
        hal_delay_ns(hw_ns(200)); /* poll interval is "hardware time" */
    }
}

/* temp sensor: real driver on silicon, stub on FPGA (no analog there) */
static int temp_real(const platform_t *p, int32_t *mc, int *is_stub)
{
    *mc = (int32_t)mmio_read(p->map.sysctl + SYS_TEMP);
    *is_stub = 0;
    return 0;
}
static int temp_stub(const platform_t *p, int32_t *mc, int *is_stub)
{
    (void)p; *mc = 25000; *is_stub = 1; return 0;
}

/* ------------------------------------------------------------------ */
/* test framework                                                      */
/* ------------------------------------------------------------------ */
typedef enum { TR_PASS = 0, TR_FAIL, TR_TIMEOUT, TR_SKIP } tr_t;
static const char *const TR_NAME[] = { "PASS", "FAIL", "TIMEOUT", "SKIP" };

typedef struct {
    const platform_t *plat;
    uint32_t seed, rng;
    int stub;
    char detail[160]; /* "code=... k=v" appended to the RESULT line */
} test_ctx_t;

typedef struct {
    const char *id;
    uint32_t requires;       /* caps the test needs; missing -> SKIP          */
    uint32_t budget_si_us;   /* whole-test budget, silicon us (scaled)        */
    int (*setup)(test_ctx_t *);
    tr_t (*run)(test_ctx_t *);
    void (*teardown)(test_ctx_t *); /* ALWAYS runs if setup succeeded        */
} test_t;

#define DETAIL(c, ...) snprintf((c)->detail, sizeof (c)->detail, __VA_ARGS__)

/* the data checker; mutation "skip_compare" turns it into a blind checker */
static int check_buf(const uint8_t *exp, const uint8_t *got, uint32_t n, uint32_t *bad_off)
{
    if (g_mutate_skip_compare) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (exp[i] != got[i]) { *bad_off = i; return -1; }
    return 0;
}

/* --- REG_RESET_VALUES ---------------------------------------------- */
static tr_t t_reg_reset(test_ctx_t *c)
{
    const struct { const char *name; uint32_t addr, exp; } r[] = {
        { "SYS_ID",   c->plat->map.sysctl + SYS_ID,   CHIP_ID },
        { "SYS_CTRL", c->plat->map.sysctl + SYS_CTRL, 0x0 }, /* spec reset value */
        { "DMA_CTRL", c->plat->map.dma + DMA_CTRL,    0x0 },
        { "DMA_STAT", c->plat->map.dma + DMA_STAT,    0x0 },
        { "ECC_STAT", c->plat->map.ecc + ECC_STAT,    0x0 },
    };
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
        uint32_t got = mmio_read(r[i].addr);
        if (got != r[i].exp) {
            DETAIL(c, "code=RESET_VAL reg=%s exp=0x%x got=0x%x", r[i].name, r[i].exp, got);
            return TR_FAIL;
        }
    }
    return TR_PASS;
}

/* --- REG_RW_WALK ----------------------------------------------------- */
static tr_t t_reg_walk(test_ctx_t *c)
{
    uint32_t a = c->plat->map.sysctl + SYS_SCRATCH;
    for (int b = 0; b < 32; b++) {
        uint32_t v = 1u << b;
        mmio_write(a, v);
        uint32_t got = mmio_read(a);
        if (got != v) { DETAIL(c, "code=RW_MISMATCH reg=SYS_SCRATCH bit=%d got=0x%x", b, got); return TR_FAIL; }
    }
    return TR_PASS;
}

/* --- DMA helpers ------------------------------------------------------ */
static int dma_setup(test_ctx_t *c)
{
    mmio_write(c->plat->map.sysctl + SYS_CLKEN, CLKEN_DMA); /* enable the block clock */
    for (uint32_t i = 0; i < DRAM_SIZE / 2; i++) g_dram[i] = (uint8_t)rng_next(&c->rng);
    memset(&g_dram[DRAM_SIZE / 2], 0, DRAM_SIZE / 2);
    return 0;
}
static void dma_teardown(test_ctx_t *c)
{
    mmio_write(c->plat->map.dma + DMA_CTRL, 0);      /* abort / idle      */
    mmio_write(c->plat->map.dma + DMA_STAT, 0x3);    /* W1C done+err      */
    mmio_write(c->plat->map.sysctl + SYS_CLKEN, 0);  /* gate clocks again */
}
static tr_t dma_xfer(test_ctx_t *c, uint32_t src, uint32_t dst, uint32_t len)
{
    const addr_map_t *m = &c->plat->map;
    mmio_write(m->dma + DMA_SRC, src);
    mmio_write(m->dma + DMA_DST, dst);
    mmio_write(m->dma + DMA_LEN, len);
    mmio_write(m->dma + DMA_CTRL, 1);
    uint32_t to_us = 2u + len / 1024u * 2u + 8u; /* silicon budget: ~4x expected */
    if (hal_poll(m->dma + DMA_STAT, 1u, 1u, to_us)) {
        DETAIL(c, "code=POLL_TIMEOUT reg=DMA_STAT len=%u limit_si_us=%u", len, to_us);
        return TR_TIMEOUT;
    }
    if (mmio_read(m->dma + DMA_STAT) & 2u) { DETAIL(c, "code=DMA_ERR len=%u", len); return TR_FAIL; }
    uint32_t off = 0;
    if (check_buf(&g_dram[src], &g_dram[dst], len, &off)) {
        DETAIL(c, "code=DATA_MISMATCH len=%u off=%u exp=0x%02x got=0x%02x",
               len, off, g_dram[src + off], g_dram[dst + off]);
        return TR_FAIL;
    }
    mmio_write(m->dma + DMA_STAT, 1u);
    return TR_PASS;
}

/* --- DMA_COPY_4K ----------------------------------------------------- */
static tr_t t_dma_4k(test_ctx_t *c) { return dma_xfer(c, 0, DRAM_SIZE / 2, 4096); }

/* --- DMA_RANDOM_SIZES (seeded sizes + alignments) --------------------- */
static tr_t t_dma_rand(test_ctx_t *c)
{
    for (int k = 0; k < 8; k++) {
        uint32_t len = 1u + rng_next(&c->rng) % 8192u;
        uint32_t so = rng_next(&c->rng) % 64u, d = DRAM_SIZE / 2 + rng_next(&c->rng) % 64u;
        tr_t r = dma_xfer(c, so, d, len);
        if (r != TR_PASS) return r;
    }
    return TR_PASS;
}

/* --- SELFTEST_CHECKER: negative control that runs in EVERY regression.
 * Inject exactly one bit flip; the checker MUST report a mismatch.
 * PASS means "the checker can still fail". */
static tr_t t_selftest(test_ctx_t *c)
{
    g_inject_once = true;
    tr_t r = dma_xfer(c, 0, DRAM_SIZE / 2, 256);
    g_inject_once = false;
    if (r == TR_FAIL && strstr(c->detail, "DATA_MISMATCH")) { c->detail[0] = 0; return TR_PASS; }
    DETAIL(c, "code=CHECKER_BLIND injected=1 detected=0");
    return TR_FAIL;
}

/* --- ECC_SCRUB_DONE: the test forgets to enable the ECC clock.
 * FPGA ignores clock gates -> PASS; silicon gates the engine -> TIMEOUT. */
static tr_t t_ecc_scrub(test_ctx_t *c)
{
    const addr_map_t *m = &c->plat->map;
    mmio_write(m->ecc + ECC_CTRL, 1);
    if (hal_poll(m->ecc + ECC_STAT, 1u, 1u, 50)) {
        DETAIL(c, "code=POLL_TIMEOUT reg=ECC_STAT clken=0x%x", mmio_read(m->sysctl + SYS_CLKEN));
        return TR_TIMEOUT;
    }
    return TR_PASS;
}
static void ecc_teardown(test_ctx_t *c) { mmio_write(c->plat->map.ecc + ECC_CTRL, 0); }

/* --- PLL_LOCK (needs a real PLL) ------------------------------------- */
static tr_t t_pll(test_ctx_t *c)
{
    mmio_write(c->plat->map.sysctl + SYS_PLL_CTRL, 1);
    if (hal_poll(c->plat->map.sysctl + SYS_PLL_STAT, 1u, 1u, 100)) {
        DETAIL(c, "code=POLL_TIMEOUT reg=PLL_STAT");
        return TR_TIMEOUT;
    }
    return TR_PASS;
}

/* --- ETH_PHY_LINK_UP (needs the analog PHY; link-up time varies) ------ */
static tr_t t_eth(test_ctx_t *c)
{
    mmio_write(c->plat->map.eth + ETH_CTRL, 1);
    if (hal_poll(c->plat->map.eth + ETH_STAT, 1u, 1u, 5000)) { /* 5 ms spec */
        DETAIL(c, "code=POLL_TIMEOUT reg=ETH_STAT limit_si_us=5000");
        return TR_TIMEOUT;
    }
    return TR_PASS;
}
static void eth_teardown(test_ctx_t *c) { mmio_write(c->plat->map.eth + ETH_CTRL, 0); }

/* --- TEMP_SENSOR_RANGE (stub on FPGA -> result tagged stub=1) --------- */
static tr_t t_temp(test_ctx_t *c)
{
    int32_t mc = 0;
    if (c->plat->temp_read_mc(c->plat, &mc, &c->stub)) { DETAIL(c, "code=READ_ERR"); return TR_FAIL; }
    if (mc < -40000 || mc > 125000) { DETAIL(c, "code=OUT_OF_RANGE mc=%d", (int)mc); return TR_FAIL; }
    DETAIL(c, "mc=%d", (int)mc);
    return TR_PASS;
}

/* static registration table (Zephyr ztest / Unity put these in a linker
 * section instead; a table keeps it portable and obvious) */
static const test_t TESTS[] = {
    { "SELFTEST_CHECKER", 0, 20, dma_setup, t_selftest, dma_teardown },
    { "REG_RESET_VALUES", 0, 10, NULL, t_reg_reset, NULL },
    { "REG_RW_WALK", 0, 20, NULL, t_reg_walk, NULL },
    { "DMA_COPY_4K", 0, 30, dma_setup, t_dma_4k, dma_teardown },
    { "DMA_RANDOM_SIZES", 0, 400, dma_setup, t_dma_rand, dma_teardown },
    { "ECC_SCRUB_DONE", 0, 100, NULL, t_ecc_scrub, ecc_teardown },
    { "PLL_LOCK", CAP_PLL, 200, NULL, t_pll, NULL },
    { "ETH_PHY_LINK_UP", CAP_ETH_PHY, 8000, NULL, t_eth, eth_teardown },
    { "TEMP_SENSOR_RANGE", 0, 10, NULL, t_temp, NULL },
};
#define NTESTS (sizeof TESTS / sizeof TESTS[0])

/* results mailbox: what a JTAG host reads at a fixed address */
typedef struct { uint32_t magic, pass, fail, timeout, skip, crc; } mbox_t;
static mbox_t g_mbox;

static uint32_t crc32(const void *p, size_t n)
{
    const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *b++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

static uint32_t mix_seed(uint32_t base, uint32_t iter, uint32_t idx)
{
    uint32_t s = base ^ (iter * 0x9E3779B9u) ^ (idx * 0x85EBCA6Bu);
    s ^= s >> 16; s *= 0x7FEB352Du; s ^= s >> 15;
    return s ? s : 1u;
}

static tr_t run_one(const test_t *t, uint32_t seed, const char *build, uint32_t iter)
{
    test_ctx_t c = { .plat = g_plat, .seed = seed, .rng = seed };
    tr_t r;
    uint64_t t0 = g_now_ns;
    uint64_t limit_us = scaled_us(t->budget_si_us);

    dev_reset(seed);
    if (t->requires & ~g_plat->caps) {
        r = TR_SKIP;
        DETAIL(&c, "reason=missing_block caps_needed=0x%x", t->requires);
    } else if (t->setup && t->setup(&c)) {
        r = TR_FAIL;
        DETAIL(&c, "code=SETUP_FAIL");
    } else {
        r = t->run(&c);
        if (t->teardown) t->teardown(&c); /* runs on PASS and on FAIL */
        uint64_t el_us = (g_now_ns - t0) / 1000u;
        if (r == TR_PASS && el_us > limit_us) { r = TR_TIMEOUT; DETAIL(&c, "code=OVER_BUDGET"); }
    }
    printf("RESULT %s %s plat=%s build=%s iter=%u seed=0x%08x t_us=%llu limit_us=%llu%s%s%s\n",
           t->id, TR_NAME[r], g_plat->name, build, iter, seed,
           (unsigned long long)((g_now_ns - t0) / 1000u), (unsigned long long)limit_us,
           c.stub ? " stub=1" : "", c.detail[0] ? " " : "", c.detail);
    switch (r) {
    case TR_PASS: g_mbox.pass++; break;    case TR_FAIL: g_mbox.fail++; break;
    case TR_TIMEOUT: g_mbox.timeout++; break; case TR_SKIP: g_mbox.skip++; break;
    }
    return r;
}

static void usage(void)
{
    fprintf(stderr, "usage: test_runner --plat fpga|silicon [--seed N] [--repeat N] [--build TAG]\n"
                    "       [--inject dma_bitflip] [--mutate skip_compare] [--no-timeout-scale] [--only ID]\n");
}

int main(int argc, char **argv)
{
    uint32_t seed = 1, repeat = 1;
    const char *build = "local", *only = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--plat") && v) {
            if (!strcmp(v, "fpga")) g_plat = &PLAT_FPGA;
            else if (!strcmp(v, "silicon")) g_plat = &PLAT_SILICON;
            else { usage(); return 2; }
            i++;
        } else if (!strcmp(a, "--seed") && v) { seed = (uint32_t)strtoul(v, NULL, 0); i++; }
        else if (!strcmp(a, "--repeat") && v) { repeat = (uint32_t)strtoul(v, NULL, 0); i++; }
        else if (!strcmp(a, "--build") && v) { build = v; i++; }
        else if (!strcmp(a, "--only") && v) { only = v; i++; }
        else if (!strcmp(a, "--inject") && v && !strcmp(v, "dma_bitflip")) { g_inject_bitflip = true; i++; }
        else if (!strcmp(a, "--mutate") && v && !strcmp(v, "skip_compare")) { g_mutate_skip_compare = true; i++; }
        else if (!strcmp(a, "--no-timeout-scale")) g_scale_timeouts = false;
        else { usage(); return 2; }
    }
    if (!g_plat || repeat == 0) { usage(); return 2; }

    printf("RUN plat=%s build=%s seed=0x%08x repeat=%u slowdown=%u timeout_scale=%u%s%s%s\n",
           g_plat->name, build, seed, repeat, g_plat->slowdown,
           g_scale_timeouts ? g_plat->timeout_scale : 1u,
           g_inject_bitflip ? " inject=dma_bitflip" : "",
           g_mutate_skip_compare ? " mutate=skip_compare" : "",
           g_scale_timeouts ? "" : " timeout_scaling=OFF");

    int bad = 0;
    for (uint32_t it = 0; it < repeat; it++)
        for (size_t k = 0; k < NTESTS; k++) {
            if (only && strcmp(only, TESTS[k].id)) continue;
            /* --repeat 1 --seed <printed seed> --only <id> reproduces exactly */
            uint32_t s = (repeat == 1 && only) ? seed : mix_seed(seed, it, (uint32_t)k);
            tr_t r = run_one(&TESTS[k], s, build, it);
            bad |= (r == TR_FAIL || r == TR_TIMEOUT);
        }

    g_mbox.magic = 0x52534C54u; /* "RSLT" */
    g_mbox.crc = crc32(&g_mbox, offsetof(mbox_t, crc));
    printf("MAILBOX magic=0x%08x pass=%u fail=%u timeout=%u skip=%u crc=0x%08x\n",
           g_mbox.magic, g_mbox.pass, g_mbox.fail, g_mbox.timeout, g_mbox.skip, g_mbox.crc);
    return bad ? 1 : 0;
}
