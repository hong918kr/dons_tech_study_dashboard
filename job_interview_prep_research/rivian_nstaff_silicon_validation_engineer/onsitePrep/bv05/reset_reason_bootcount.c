/*
 * BV05 lab: reset-reason decode + boot counter in a "noinit" RAM record +
 * strap-pin decode. Host-runnable simulation (no real board).
 *
 * Part A  Decode the real Zynq UltraScale+ CRL_APB.RESET_REASON layout
 *         (UG1087; same bit names as U-Boot arch/arm/mach-zynqmp hardware.h).
 * Part B  A generic simulated SoC ("SoC-X") across many resets:
 *         - RSTCAUSE register: sticky, write-1-to-clear (W1C), cleared by POR
 *         - on-chip SRAM: random after POR, retained across warm resets,
 *           first 64 bytes scribbled by the BootROM on every boot (its stack)
 *         - firmware keeps a {magic, ~magic, counters, checksum} record in a
 *           "noinit" region -> boot counter + watchdog-loop detection
 *         Scenarios: good FW, FW that forgets to clear W1C bits, and a noinit
 *         record placed inside the BootROM scratch area.
 * Part C  Strap pins: electrical model (pull resistor vs another driver /
 *         leakage / IO rail not up at sampling) -> logic level -> boot mode
 *         (ZynqMP MODE[3:0] values as in U-Boot hardware.h).
 *
 * Build (macOS): cc -isysroot <SDK> -std=c11 -Wall -Wextra -Werror \
 *                   -fsanitize=address,undefined reset_reason_bootcount.c
 * On a real target, "struct noinit_rec g_rec" would be
 *   __attribute__((section(".noinit"))) and the linker script would give
 *   .noinit (NOLOAD) so startup code neither zeroes nor copies it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== Part A: ZynqMP RESET_REASON ===================== */
/* CRL_APB base 0xFF5E0000, RESET_REASON offset 0x220 -> 0xFF5E0220.
 * Bits 6:0 are W1C status, reset only by POR (UG1087). */
#define ZU_RR_EXTERNAL_POR (1u << 0) /* PS_POR_B pin asserted          */
#define ZU_RR_INTERNAL_POR (1u << 1) /* system error escalated to POR  */
#define ZU_RR_PMU_SYS_RST  (1u << 2) /* PMU system reset (sys error)   */
#define ZU_RR_PSONLY       (1u << 3) /* PS-only reset request          */
#define ZU_RR_SRST         (1u << 4) /* PS_SRST_B pin asserted         */
#define ZU_RR_SOFT         (1u << 5) /* SW system reset via RESET_CTRL */
#define ZU_RR_DEBUG_SYS    (1u << 6) /* debugger-requested system rst  */

static const struct {
    uint32_t bit;
    const char *name;
    const char *hint;
} zu_rr[] = {
    {ZU_RR_DEBUG_SYS, "DEBUG", "debugger issued a system reset"},
    {ZU_RR_SOFT, "SOFT", "software wrote RESET_CTRL (reboot cmd)"},
    {ZU_RR_SRST, "SRST", "PS_SRST_B pin: button / supervisor / BMC"},
    {ZU_RR_PSONLY, "PS-ONLY", "PS-only reset (PL kept)"},
    {ZU_RR_PMU_SYS_RST, "PMU", "PMU escalated a system error (e.g. WDT)"},
    {ZU_RR_INTERNAL_POR, "INTERNAL", "system error escalated to internal POR"},
    {ZU_RR_EXTERNAL_POR, "EXTERNAL", "PS_POR_B: real power-on / power cycle"},
};

static void zu_decode(uint32_t v)
{
    printf("  RESET_REASON=0x%04x ->", v);
    int n = 0;
    for (size_t i = 0; i < sizeof zu_rr / sizeof zu_rr[0]; i++) {
        if (v & zu_rr[i].bit) {
            printf(" %s", zu_rr[i].name);
            n++;
        }
    }
    uint32_t rsvd = v & ~0x7Fu;
    if (n == 0)
        printf(" (none)");
    if (rsvd)
        printf(" [reserved bits set 0x%x!]", rsvd);
    printf("\n");
    for (size_t i = 0; i < sizeof zu_rr / sizeof zu_rr[0]; i++)
        if (v & zu_rr[i].bit)
            printf("      %-8s : %s\n", zu_rr[i].name, zu_rr[i].hint);
    if (n > 1)
        printf("      note     : >1 bit set = sticky bits accumulated since "
               "last POR (nobody cleared them)\n");
}

/* ===================== Part B: simulated SoC-X ========================= */
enum {
    RC_POR = 1u << 0,  /* power-on reset                 */
    RC_PIN = 1u << 1,  /* external reset pin             */
    RC_SW = 1u << 2,   /* software (AIRCR.SYSRESETREQ..) */
    RC_WDT = 1u << 3,  /* watchdog timeout               */
    RC_BOR = 1u << 4,  /* brown-out (rail dipped)        */
    RC_DBG = 1u << 5,  /* debugger                       */
    RC_THERM = 1u << 6 /* thermal shutdown/reset         */
};
static const char *rc_name[] = {"POR", "PIN", "SW", "WDT", "BOR", "DBG", "THERM"};

#define SRAM_SIZE 256
#define BOOTROM_SCRATCH 64 /* BootROM uses SRAM[0..63] as stack each boot */

struct soc {
    uint32_t rstcause;        /* sticky W1C; cleared only by POR */
    uint8_t sram[SRAM_SIZE];  /* on-chip SRAM */
    uint32_t prng;
};

static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static void hw_reset(struct soc *s, uint32_t cause)
{
    if (cause & (RC_POR | RC_BOR)) {
        /* power went away: register cleared, SRAM content is garbage */
        s->rstcause = 0;
        for (int i = 0; i < SRAM_SIZE; i++)
            s->sram[i] = (uint8_t)xorshift(&s->prng);
    }
    s->rstcause |= cause; /* set the cause bit (sticky) */
    /* BootROM always runs first and scribbles its scratch area */
    for (int i = 0; i < BOOTROM_SCRATCH; i++)
        s->sram[i] = (uint8_t)(0xA5 ^ i);
}

/* ---- firmware side ---- */
#define REC_MAGIC 0xB007C0DEu
struct noinit_rec {
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t boot_count;   /* boots since last POR */
    uint32_t wdt_streak;   /* consecutive WDT resets without a "healthy" mark */
    uint32_t last_cause;
    uint32_t checksum;     /* FNV-1a over the fields above */
};

static uint32_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t rec_sum(const struct noinit_rec *r)
{
    return fnv1a(r, offsetof(struct noinit_rec, checksum));
}

struct fw_cfg {
    const char *label;
    size_t rec_offset;     /* where the linker put .noinit inside SRAM */
    bool clear_w1c;        /* does FW clear RSTCAUSE after reading it? */
    uint32_t wdt_limit;    /* enter safe mode at this many WDTs in a row */
};

static void cause_str(uint32_t v, char *buf, size_t n)
{
    buf[0] = 0;
    for (int i = 0; i < 7; i++)
        if (v & (1u << i)) {
            if (buf[0])
                strncat(buf, "|", n - strlen(buf) - 1);
            strncat(buf, rc_name[i], n - strlen(buf) - 1);
        }
    if (!buf[0])
        strncat(buf, "NONE", n - 1);
}

/* naive decoder: report the first bit found, POR checked first */
static const char *naive_primary(uint32_t v)
{
    for (int i = 0; i < 7; i++)
        if (v & (1u << i))
            return rc_name[i];
    return "NONE";
}

/* runs at every boot; returns true if FW chose SAFE MODE */
static bool fw_boot(struct soc *s, const struct fw_cfg *c, int seq)
{
    uint32_t raw = s->rstcause;            /* 1. read cause first thing */
    if (c->clear_w1c)
        s->rstcause &= ~raw;               /* W1C: write back what we read */

    struct noinit_rec r;                   /* 2. validate noinit record */
    memcpy(&r, &s->sram[c->rec_offset], sizeof r);
    bool valid = r.magic == REC_MAGIC && r.magic_inv == ~REC_MAGIC &&
                 r.checksum == rec_sum(&r);
    const char *init = "warm";
    if (!valid || (raw & (RC_POR | RC_BOR))) {
        init = valid ? "cold(POR)" : "cold(invalid rec)";
        memset(&r, 0, sizeof r);
        r.magic = REC_MAGIC;
        r.magic_inv = ~REC_MAGIC;
    }
    r.boot_count++;                        /* 3. update counters */
    r.wdt_streak = (raw & RC_WDT) ? r.wdt_streak + 1 : 0;
    r.last_cause = raw;
    bool safe = r.wdt_streak >= c->wdt_limit;
    r.checksum = rec_sum(&r);
    memcpy(&s->sram[c->rec_offset], &r, sizeof r);

    char cs[64];
    cause_str(raw, cs, sizeof cs);
    printf("  #%-2d raw=0x%02x %-14s naive=%-4s rec=%-17s boot_count=%u "
           "wdt_streak=%u -> %s\n",
           seq, raw, cs, naive_primary(raw), init, r.boot_count,
           r.wdt_streak, safe ? "SAFE MODE (golden image, no app)" : "normal");
    return safe;
}

/* app reached "healthy" (e.g. ran 60 s, self-test passed) -> clear streak */
static void fw_mark_healthy(struct soc *s, const struct fw_cfg *c)
{
    struct noinit_rec r;
    memcpy(&r, &s->sram[c->rec_offset], sizeof r);
    r.wdt_streak = 0;
    r.checksum = rec_sum(&r);
    memcpy(&s->sram[c->rec_offset], &r, sizeof r);
}

static void run_scenario(const struct fw_cfg *c)
{
    struct soc s = {.rstcause = 0, .prng = 0x1234567u};
    printf("\n[B] %s (rec @SRAM+%zu, clear_w1c=%d, wdt_limit=%u)\n", c->label,
           c->rec_offset, c->clear_w1c, c->wdt_limit);
    /* scripted life of a board */
    static const struct { uint32_t cause; bool healthy; const char *what; } ev[] = {
        {RC_POR, true, "bench PSU on"},
        {RC_SW, false, "FW update -> reboot; new FW hangs"},
        {RC_WDT, false, "watchdog fires"},
        {RC_WDT, false, "watchdog fires"},
        {RC_WDT, false, "watchdog fires"},
        {RC_PIN, true, "engineer presses reset button"},
        {RC_BOR, true, "load step -> rail droop -> brown-out"},
        {RC_POR, true, "power cycle"},
    };
    for (size_t i = 0; i < sizeof ev / sizeof ev[0]; i++) {
        hw_reset(&s, ev[i].cause);
        printf("     (%s)\n", ev[i].what);
        bool safe = fw_boot(&s, c, (int)i + 1);
        if (ev[i].healthy && !safe)
            fw_mark_healthy(&s, c);
    }
}

/* ===================== Part C: strap pins ============================== */
/* LVCMOS input thresholds as a fraction of VCCIO (typical JESD8 style;
 * check the SoC datasheet for real VIH/VIL). */
#define VIH_FRAC 0.65
#define VIL_FRAC 0.35

struct strap_pin {
    const char *name;
    bool pull_up;          /* designer's intent: 1 = pull-up, 0 = pull-down */
    double r_pull;         /* ohms */
    double i_leak;         /* amps; >0 = current pulled OUT of the node (sink) */
    double r_other;        /* ohms; another chip driving this net, 0 = none */
    double v_other;        /* level that other chip drives */
};

/* returns 1/0, or -1 if the voltage is between VIL and VIH (undefined) */
static int strap_sample(const struct strap_pin *p, double vccio_at_sample,
                        double vccio_nom, double *v_out)
{
    double vpull = p->pull_up ? vccio_at_sample : 0.0;
    double v;
    if (p->r_other > 0) /* two resistors fighting: divider (+ leakage) */
        v = (vpull / p->r_pull + p->v_other / p->r_other - p->i_leak) /
            (1.0 / p->r_pull + 1.0 / p->r_other);
    else
        v = vpull - p->i_leak * p->r_pull;
    if (v < 0)
        v = 0;
    *v_out = v;
    /* the input buffer's thresholds track the rail it is powered from */
    double vih = VIH_FRAC * vccio_nom, vil = VIL_FRAC * vccio_nom;
    if (vccio_at_sample < 0.8 * vccio_nom) /* buffer not really powered */
        return v > vih ? 1 : 0;            /* typically reads 0 */
    if (v >= vih) return 1;
    if (v <= vil) return 0;
    return -1;
}

static const char *zu_boot_mode_name(unsigned m)
{
    switch (m) { /* values per U-Boot arch/arm/mach-zynqmp hardware.h */
    case 0x0: return "JTAG";
    case 0x1: return "QSPI24";
    case 0x2: return "QSPI32";
    case 0x3: return "SD0";
    case 0x4: return "NAND";
    case 0x5: return "SD1";
    case 0x6: return "eMMC";
    case 0x7: return "USB0";
    case 0xE: return "SD1-LS";
    default:  return "(other/reserved - see UG1085 boot mode table)";
    }
}

static void strap_case(const char *title, struct strap_pin pins[4],
                       double vccio_at_sample, unsigned expect)
{
    const double vnom = 1.8;
    unsigned mode = 0;
    bool undefined = false;
    printf("\n[C] %s (VCCIO at sample = %.2f V)\n", title, vccio_at_sample);
    for (int i = 0; i < 4; i++) {
        double v;
        int b = strap_sample(&pins[i], vccio_at_sample, vnom, &v);
        printf("    %-7s %s %5.1fk  leak=%5.1fuA  other=%s  V=%.3f V -> %s\n",
               pins[i].name, pins[i].pull_up ? "PU" : "PD",
               pins[i].r_pull / 1e3, pins[i].i_leak * 1e6,
               pins[i].r_other > 0 ? "DRIVING" : "none   ", v,
               b < 0 ? "UNDEFINED (between VIL/VIH)" : (b ? "1" : "0"));
        if (b < 0)
            undefined = true;
        if (b > 0)
            mode |= 1u << i;
    }
    printf("    MODE[3:0]=0x%X (%s)  expected 0x%X (%s)  => %s\n", mode,
           zu_boot_mode_name(mode), expect, zu_boot_mode_name(expect),
           undefined ? "UNRELIABLE (board-dependent!)"
                     : (mode == expect ? "OK" : "WRONG BOOT MODE"));
}

int main(void)
{
    printf("=== Part A: Zynq UltraScale+ RESET_REASON decode ===\n");
    zu_decode(0x0001);              /* fresh power-on */
    zu_decode(0x0010);              /* reset button / supervisor on SRST */
    zu_decode(0x0021);              /* POR then SW reboot, never cleared */
    zu_decode(0x0004);              /* PMU system reset */

    printf("\n=== Part B: boot counter in noinit SRAM across resets ===\n");
    struct fw_cfg good = {"good FW", 128, true, 3};
    struct fw_cfg sticky = {"FW forgets to clear W1C cause bits", 128, false, 3};
    struct fw_cfg badplace = {"noinit record inside BootROM scratch", 16, true, 3};
    run_scenario(&good);
    run_scenario(&sticky);
    run_scenario(&badplace);

    printf("\n=== Part C: strap sampling -> ZynqMP boot mode ===\n");
    /* intent: QSPI32 = 0x2 = MODE1 high, others low */
    struct strap_pin ok[4] = {
        {"MODE0", false, 4.7e3, 0, 0, 0},
        {"MODE1", true, 4.7e3, 0, 0, 0},
        {"MODE2", false, 4.7e3, 0, 0, 0},
        {"MODE3", false, 4.7e3, 0, 0, 0},
    };
    strap_case("4.7k straps, nothing else on the nets", ok, 1.8, 0x2);

    struct strap_pin weak[4];
    memcpy(weak, ok, sizeof ok);
    weak[1].r_pull = 100e3;   /* someone 'saved power' with 100k */
    weak[1].i_leak = 10e-6;   /* 10 uA leakage from a shared device's input */
    strap_case("MODE1 pull-up 100k + 10uA leakage", weak, 1.8, 0x2);

    struct strap_pin fight[4];
    memcpy(fight, ok, sizeof ok);
    fight[1].r_other = 2e3;   /* a CPLD/FPGA drives this net low during reset */
    fight[1].v_other = 0.0;
    strap_case("MODE1 shared with a CPLD output driving low", fight, 1.8, 0x2);

    strap_case("IO rail still ramping when reset released", ok, 0.9, 0x2);
    return 0;
}
