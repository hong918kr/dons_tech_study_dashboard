/* ltssm_trace.c - turn raw LTSSM samples from a Synopsys DesignWare (DWC) PCIe controller into a
 * readable state trace + a verdict. (LP03)
 *
 * On real silicon you would poll the DBI register PCIE_PORT_DEBUG0 (offset 0x728, LTSSM state in
 * bits [5:0]) in a tight loop right after releasing PERST# / setting the app LTSSM-enable bit, and
 * store the samples. State codes are taken from the Linux kernel enum dw_pcie_ltssm in
 * drivers/pci/controller/dwc/pcie-designware.h (v6.18). Other IPs (Cadence, PLDA, Xilinx) use their
 * own encodings - only the decoding table changes.
 *
 * bare-metal sampling loop (pseudo, not compiled here):
 *     for (n = 0; n < N; n++) { s[n] = readl(dbi + 0x728) & 0x3f; udelay(10); }
 *
 * Build/run (macOS): cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra \
 *     -Werror -fsanitize=address,undefined ltssm_trace.c -o /tmp/ltssm_trace && /tmp/ltssm_trace
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define PCIE_PORT_DEBUG0            0x728
#define PORT_LOGIC_LTSSM_STATE_MASK 0x3f

static const char *const dwc_ltssm_name[] = {
    [0x00] = "DETECT_QUIET",     [0x01] = "DETECT_ACT",       [0x02] = "POLL_ACTIVE",
    [0x03] = "POLL_COMPLIANCE",  [0x04] = "POLL_CONFIG",      [0x05] = "PRE_DETECT_QUIET",
    [0x06] = "DETECT_WAIT",      [0x07] = "CFG_LINKWD_START", [0x08] = "CFG_LINKWD_ACEPT",
    [0x09] = "CFG_LANENUM_WAI",  [0x0a] = "CFG_LANENUM_ACEPT", [0x0b] = "CFG_COMPLETE",
    [0x0c] = "CFG_IDLE",         [0x0d] = "RCVRY_LOCK",       [0x0e] = "RCVRY_SPEED",
    [0x0f] = "RCVRY_RCVRCFG",    [0x10] = "RCVRY_IDLE",       [0x11] = "L0",
    [0x12] = "L0S",              [0x13] = "L123_SEND_EIDLE",  [0x14] = "L1_IDLE",
    [0x15] = "L2_IDLE",          [0x16] = "L2_WAKE",          [0x17] = "DISABLED_ENTRY",
    [0x18] = "DISABLED_IDLE",    [0x19] = "DISABLED",         [0x1a] = "LPBK_ENTRY",
    [0x1b] = "LPBK_ACTIVE",      [0x1c] = "LPBK_EXIT",        [0x1d] = "LPBK_EXIT_TIMEOUT",
    [0x1e] = "HOT_RESET_ENTRY",  [0x1f] = "HOT_RESET",        [0x20] = "RCVRY_EQ0",
    [0x21] = "RCVRY_EQ1",        [0x22] = "RCVRY_EQ2",        [0x23] = "RCVRY_EQ3",
};

static const char *ltssm_name(unsigned s)
{
    s &= PORT_LOGIC_LTSSM_STATE_MASK;
    return (s < sizeof dwc_ltssm_name / sizeof dwc_ltssm_name[0] && dwc_ltssm_name[s]) ? dwc_ltssm_name[s] : "UNKNOWN";
}

static int is_detect(unsigned s)  { return s == 0x00 || s == 0x01 || s == 0x05 || s == 0x06; }
static int is_polling(unsigned s) { return s >= 0x02 && s <= 0x04; }
static int is_config(unsigned s)  { return s >= 0x07 && s <= 0x0c; }
static int is_recovery(unsigned s){ return (s >= 0x0d && s <= 0x10) || (s >= 0x20 && s <= 0x23); }

typedef enum {
    V_NO_RECEIVER,        /* never left Detect: no termination seen (no card, PERST#, power, AC caps, lanes) */
    V_POLLING_LOOP,       /* Detect <-> Polling: receiver seen but no symbol lock (refclk, SI, polarity, lane) */
    V_COMPLIANCE,         /* fell into Polling.Compliance (often: only some lanes detected, or test load) */
    V_CONFIG_FAIL,        /* reached Configuration but bounced: lane/link number negotiation problem */
    V_L0_STABLE,          /* reached L0 and stayed */
    V_RECOVERY_STORM,     /* L0 <-> Recovery repeatedly: marginal link, check AER RxErr, eq presets */
    V_EQ_FAIL,            /* Recovery.Equalization entered then fell back (speed stuck low) */
} verdict_t;

static const char *verdict_str[] = {
    "NO_RECEIVER (stuck in Detect)", "POLLING_LOOP (Detect<->Polling)", "COMPLIANCE (Polling.Compliance)",
    "CONFIG_FAIL (Configuration bounce)", "L0_STABLE", "RECOVERY_STORM (L0<->Recovery)", "EQ_FAIL (Recovery.EqN then fallback)",
};

/* Collapse repeated samples into transitions, print them, and classify the run. */
static verdict_t analyze(const char *title, const uint32_t *raw, size_t n, int verbose)
{
    unsigned prev = 0xFF, l0_entries = 0, rec_from_l0 = 0, eq_seen = 0, eq_then_detect = 0;
    unsigned seen_poll = 0, seen_cfg = 0, seen_comp = 0, last = 0, ntrans = 0;
    if (verbose)
        printf("-- %s\n   ", title);
    for (size_t i = 0; i < n; i++) {
        unsigned s = raw[i] & PORT_LOGIC_LTSSM_STATE_MASK;
        if (s == prev)
            continue;
        if (verbose) {
            if (ntrans && ntrans % 5 == 0)
                printf("\n   ");
            printf("%s%s(0x%02x)", ntrans ? " -> " : "", ltssm_name(s), s);
        }
        ntrans++;
        if (is_polling(s)) seen_poll = 1;
        if (s == 0x03) seen_comp = 1;
        if (is_config(s)) seen_cfg = 1;
        if (s == 0x11) l0_entries++;
        if (is_recovery(s) && prev == 0x11) rec_from_l0++;
        if (s >= 0x20 && s <= 0x23) eq_seen = 1;
        if (eq_seen && is_detect(s)) eq_then_detect = 1;
        prev = s;
        last = s;
    }
    verdict_t v;
    if (!seen_poll)                          v = V_NO_RECEIVER;
    else if (seen_comp && !seen_cfg)         v = V_COMPLIANCE;
    else if (!seen_cfg)                      v = V_POLLING_LOOP;
    else if (!l0_entries)                    v = V_CONFIG_FAIL;
    else if (eq_then_detect)                 v = V_EQ_FAIL;
    else if (rec_from_l0 > 3)                v = V_RECOVERY_STORM;
    else if (last == 0x11)                   v = V_L0_STABLE;
    else                                     v = V_RECOVERY_STORM;
    if (verbose)
        printf("\n   transitions=%u  L0 entries=%u  L0->Recovery=%u  => %s\n", ntrans, l0_entries, rec_from_l0, verdict_str[v]);
    return v;
}

#define N(a) (sizeof(a) / sizeof((a)[0]))

int main(void)
{
    /* Raw samples include upper DEBUG0 bits that must be masked off (here 0xA5000000 noise). */
    /* 1. healthy Gen3 bring-up: Detect -> Polling -> Config -> L0 (Gen1) -> Recovery (speed + EQ) -> L0 */
    static const uint32_t good[] = {
        0x00, 0x00, 0x01, 0x02, 0x02, 0x04, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x11, 0x11,
        0x0d, 0x0e, 0xA5000020u, 0x21, 0x22, 0x23, 0x0d, 0x0f, 0x10, 0x11, 0x11, 0x11,
    };
    /* 2. empty slot / PERST# never released / no power: Detect.Quiet <-> Detect.Active forever */
    static const uint32_t no_rx[] = { 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01 };
    /* 3. receiver detected but no lock (bad refclk, SI): Detect -> Polling.Active -> timeout -> Detect ... */
    static const uint32_t poll_loop[] = { 0x00, 0x01, 0x02, 0x02, 0x00, 0x01, 0x02, 0x00, 0x01, 0x02, 0x02 };
    /* 4. Polling.Active timed out with only some lanes seeing exit from Electrical Idle -> Polling.Compliance */
    static const uint32_t comp[] = { 0x00, 0x01, 0x02, 0x03, 0x03, 0x03 };
    /* 5. Gen3 equalization fails: EQ phases then the link drops to Detect and retrains */
    static const uint32_t eqfail[] = {
        0x00, 0x01, 0x02, 0x04, 0x07, 0x0a, 0x0b, 0x0c, 0x11, 0x0d, 0x0e, 0x20, 0x21, 0x00, 0x01, 0x02, 0x04, 0x07, 0x0b, 0x0c, 0x11,
    };
    /* 6. marginal link at speed: L0 <-> Recovery repeatedly (AER Receiver Errors / replays would climb) */
    static const uint32_t storm[] = {
        0x00, 0x01, 0x02, 0x04, 0x07, 0x0b, 0x0c, 0x11, 0x0d, 0x0f, 0x10, 0x11, 0x0d, 0x0f, 0x10, 0x11,
        0x0d, 0x0f, 0x10, 0x11, 0x0d, 0x0f, 0x10, 0x11, 0x0d, 0x0f, 0x10, 0x11,
    };

    CHECK(analyze("healthy bring-up", good, N(good), 1) == V_L0_STABLE);
    CHECK(analyze("empty slot / PERST# held", no_rx, N(no_rx), 1) == V_NO_RECEIVER);
    CHECK(analyze("no symbol lock", poll_loop, N(poll_loop), 1) == V_POLLING_LOOP);
    CHECK(analyze("compliance", comp, N(comp), 1) == V_COMPLIANCE);
    CHECK(analyze("equalization failure", eqfail, N(eqfail), 1) == V_EQ_FAIL);
    CHECK(analyze("recovery storm", storm, N(storm), 1) == V_RECOVERY_STORM);

    CHECK(strcmp(ltssm_name(0x11), "L0") == 0);
    CHECK(strcmp(ltssm_name(0xA5000011u), "L0") == 0);       /* upper bits masked */
    CHECK(strcmp(ltssm_name(0x3f), "UNKNOWN") == 0);

    printf("%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
