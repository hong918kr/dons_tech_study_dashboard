/*
 * tapsim.c -- IEEE 1149.1 TAP state machine + scan-chain simulator (BV07)
 *
 * What it shows (all simulated, no hardware):
 *   A. TMS sequence -> state trace (16-state TAP controller)
 *   B. proof: from every one of the 16 states, 5 x TMS=1 reaches Test-Logic-Reset
 *   C. IDCODE autoprobe after reset (what OpenOCD "scan_chain"/autoprobe does)
 *   D. total IR length measurement + Capture-IR "...01" check per TAP
 *   E. select one TAP's IDCODE while others are in BYPASS (DR = 32 + #bypass bits)
 *   F. boundary-scan interconnect test (EXTEST, walking 1 / walking 0)
 *      on 4 nets with an injected open and an injected short
 *   G. chain faults: TDO stuck-high, TDO stuck-low, wrong declared IR length
 *   decode mode:  ./tapsim decode 0x4BA00477 ...   -> IDCODE field decoder
 *
 * Chain (TDI -> ... -> TDO):  dev0 CPLD(no IDCODE) -> dev1 FPGA -> dev2 SoC BS TAP -> dev3 Arm JTAG-DP -> TDO
 * Device list is printed "closest to TDO first" (OpenOCD convention).
 * Only the Arm JTAG-DP IDCODE 0x4BA00477 is a real-world value; the others are fictitious.
 *
 * Build (macOS): cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
 *                   -fsanitize=address,undefined tapsim.c -o /tmp/tapsim && /tmp/tapsim
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- TAP controller ---------------- */
typedef enum {
    TLR, RTI, SEL_DR, CAP_DR, SH_DR, EX1_DR, PAU_DR, EX2_DR, UPD_DR,
    SEL_IR, CAP_IR, SH_IR, EX1_IR, PAU_IR, EX2_IR, UPD_IR, NSTATES
} tap_state;

static const char *sname[NSTATES] = {
    "Test-Logic-Reset", "Run-Test/Idle", "Select-DR-Scan", "Capture-DR", "Shift-DR",
    "Exit1-DR", "Pause-DR", "Exit2-DR", "Update-DR",
    "Select-IR-Scan", "Capture-IR", "Shift-IR", "Exit1-IR", "Pause-IR", "Exit2-IR", "Update-IR"
};

/* next[state][tms] -- IEEE 1149.1 state diagram */
static const tap_state next_state[NSTATES][2] = {
    [TLR]    = {RTI, TLR},       [RTI]    = {RTI, SEL_DR},
    [SEL_DR] = {CAP_DR, SEL_IR}, [CAP_DR] = {SH_DR, EX1_DR},
    [SH_DR]  = {SH_DR, EX1_DR},  [EX1_DR] = {PAU_DR, UPD_DR},
    [PAU_DR] = {PAU_DR, EX2_DR}, [EX2_DR] = {SH_DR, UPD_DR},
    [UPD_DR] = {RTI, SEL_DR},
    [SEL_IR] = {CAP_IR, TLR},    [CAP_IR] = {SH_IR, EX1_IR},
    [SH_IR]  = {SH_IR, EX1_IR},  [EX1_IR] = {PAU_IR, UPD_IR},
    [PAU_IR] = {PAU_IR, EX2_IR}, [EX2_IR] = {SH_IR, UPD_IR},
    [UPD_IR] = {RTI, SEL_DR},
};

/* ---------------- board nets for the boundary-scan demo ---------------- */
#define NNETS 4
enum { FAULT_NONE, FAULT_OPEN, FAULT_SHORT_WITH_NEXT };
static int net_fault[NNETS];          /* injected manufacturing defects */

/* ---------------- devices ---------------- */
enum { REG_BYPASS, REG_IDCODE, REG_BSR };
enum { CELL_OUT, CELL_IN };
#define MAX_BSR 8

typedef struct {
    const char *name;
    unsigned irlen;
    uint32_t ir_capture;     /* loaded in Capture-IR; IEEE: two LSBs must be 01 */
    uint32_t idcode;         /* 0 => device has no IDCODE register */
    uint32_t op_idcode, op_extest, op_sample;  /* BYPASS is always all-ones */
    /* boundary register: cell i role and net */
    unsigned bsr_len;
    int cell_role[MAX_BSR];
    int cell_net[MAX_BSR];
    uint8_t bsr_update[MAX_BSR];   /* update (output) latch */
    /* live state */
    uint32_t ir;                   /* active instruction (after Update-IR) */
    uint64_t ir_sh;                /* IR shift register */
    uint64_t dr_sh;                /* DR shift register (bit0 = next out on TDO side) */
    unsigned dr_len;
} jtag_dev;

#define NDEV 4
static jtag_dev chain[NDEV];       /* index 0 = closest to TDI */
static tap_state st = TLR;
static int tdo_stuck = -1;         /* -1 none, 0 stuck-low, 1 stuck-high */
static int trace_on = 0;

static int dev_reg(const jtag_dev *d)
{
    uint32_t ones = (uint32_t)((1u << d->irlen) - 1u);
    if (d->ir == ones) return REG_BYPASS;
    if (d->idcode && d->ir == d->op_idcode) return REG_IDCODE;
    if (d->bsr_len && (d->ir == d->op_extest || d->ir == d->op_sample)) return REG_BSR;
    return REG_BYPASS;             /* unused opcodes behave as BYPASS (common practice) */
}

static int in_extest(const jtag_dev *d) { return d->bsr_len && d->ir == d->op_extest; }

/* resolve board nets: driver = BSR update latch of an OUT cell in EXTEST, else functional 0 */
static void resolve_nets(int val[NNETS])
{
    int drv[NNETS];
    for (int n = 0; n < NNETS; n++) drv[n] = 0;
    for (int i = 0; i < NDEV; i++) {
        const jtag_dev *d = &chain[i];
        if (!in_extest(d)) continue;
        for (unsigned c = 0; c < d->bsr_len; c++)
            if (d->cell_role[c] == CELL_OUT) drv[d->cell_net[c]] = d->bsr_update[c];
    }
    for (int n = 0; n < NNETS; n++) val[n] = drv[n];
    for (int n = 0; n < NNETS; n++) {
        if (net_fault[n] == FAULT_SHORT_WITH_NEXT && n + 1 < NNETS) {
            int w = drv[n] & drv[n + 1];          /* wired-AND short */
            val[n] = val[n + 1] = w;
        }
    }
    for (int n = 0; n < NNETS; n++)
        if (net_fault[n] == FAULT_OPEN) val[n] = 1; /* receiver floats high via pull-up */
}

static void on_enter(tap_state s)
{
    for (int i = 0; i < NDEV; i++) {
        jtag_dev *d = &chain[i];
        switch (s) {
        case TLR:   /* reset: IDCODE if present, else BYPASS */
            d->ir = d->idcode ? d->op_idcode : (uint32_t)((1u << d->irlen) - 1u);
            break;
        case CAP_IR:
            d->ir_sh = d->ir_capture;
            break;
        case CAP_DR: {
            int r = dev_reg(d);
            if (r == REG_BYPASS) { d->dr_sh = 0; d->dr_len = 1; }          /* BYPASS captures 0 */
            else if (r == REG_IDCODE) { d->dr_sh = d->idcode; d->dr_len = 32; }
            else {
                int nets[NNETS];
                resolve_nets(nets);
                d->dr_sh = 0; d->dr_len = d->bsr_len;
                for (unsigned c = 0; c < d->bsr_len; c++) {
                    int v = d->cell_role[c] == CELL_IN ? nets[d->cell_net[c]] : d->bsr_update[c];
                    d->dr_sh |= (uint64_t)(v & 1) << c;
                }
            }
            break;
        }
        case UPD_IR:
            d->ir = (uint32_t)(d->ir_sh & ((1u << d->irlen) - 1u));
            break;
        case UPD_DR:
            if (dev_reg(d) == REG_BSR)                 /* PRELOAD/SAMPLE and EXTEST load latch */
                for (unsigned c = 0; c < d->bsr_len; c++) d->bsr_update[c] = (uint8_t)((d->dr_sh >> c) & 1);
            break;
        default: break;
        }
    }
}

/* one TCK: shift (if in a Shift state) then move by TMS. returns TDO. */
static int tck(int tms, int tdi)
{
    int tdo = 1;                    /* TDO is high-Z (pulled up) outside Shift states */
    tap_state before = st;
    if (st == SH_DR || st == SH_IR) {
        int bit = tdi & 1;
        for (int i = 0; i < NDEV; i++) {          /* TDI -> dev0 -> ... -> dev3 -> TDO */
            jtag_dev *d = &chain[i];
            unsigned len = (st == SH_IR) ? d->irlen : d->dr_len;
            uint64_t *r = (st == SH_IR) ? &d->ir_sh : &d->dr_sh;
            int out = (int)(*r & 1);
            *r = (*r >> 1) | ((uint64_t)bit << (len - 1));
            bit = out;
        }
        tdo = bit;
    }
    if (tdo_stuck >= 0) tdo = tdo_stuck;
    st = next_state[st][tms & 1];
    if (trace_on) printf("    TMS=%d  %-16s -> %s\n", tms & 1, sname[before], sname[st]);
    on_enter(st);
    return tdo;
}

static void tap_reset(void) { for (int i = 0; i < 5; i++) tck(1, 1); tck(0, 1); /* -> RTI */ }

/* shift n bits through IR or DR starting and ending in Run-Test/Idle */
static void shift(int ir, const uint8_t *in, uint8_t *out, unsigned n)
{
    tck(1, 1);                      /* RTI -> Select-DR */
    if (ir) tck(1, 1);              /* -> Select-IR */
    tck(0, 1);                      /* -> Capture-xR */
    tck(0, 1);                      /* -> Shift-xR */
    for (unsigned i = 0; i < n; i++) {
        int b = tck(i == n - 1, in ? in[i] : 1);  /* last bit with TMS=1 -> Exit1 */
        if (out) out[i] = (uint8_t)b;
    }
    tck(1, 1);                      /* Exit1 -> Update */
    tck(0, 1);                      /* -> RTI */
}

static uint32_t bits_to_u32(const uint8_t *b) { uint32_t v = 0; for (int i = 0; i < 32; i++) v |= (uint32_t)b[i] << i; return v; }

/* ---------------- IDCODE decoder ---------------- */
static const char *jep106_name(unsigned bank_cc, unsigned id)
{
    /* tiny subset; full table: OpenOCD src/helper/jep106.inc */
    if (bank_cc == 0 && id == 0x49) return "Xilinx (AMD)";
    if (bank_cc == 0 && id == 0x6E) return "Altera (Intel PSG)";
    if (bank_cc == 0 && id == 0x17) return "Texas Instruments";
    if (bank_cc == 4 && id == 0x3B) return "Arm Ltd";
    return "(not in this mini table)";
}

static void decode_idcode(uint32_t v)
{
    unsigned version = v >> 28, part = (v >> 12) & 0xFFFF, mfr = (v >> 1) & 0x7FF;
    unsigned bank_cc = (mfr >> 7) & 0xF, id = mfr & 0x7F;
    printf("  IDCODE 0x%08X: version=0x%X part=0x%04X mfr=0x%03X (JEP106 bank %u, id 0x%02X = %s) bit0=%u",
           v, version, part, mfr, bank_cc + 1, id, jep106_name(bank_cc, id), v & 1);
    if (v == 0xFFFFFFFFu) printf("  -> INVALID: all ones (TDO stuck high / no device / end of chain)");
    else if (v == 0) printf("  -> INVALID: all zeros (TDO stuck low)");
    else if (!(v & 1)) printf("  -> INVALID: bit0 must be 1 for IDCODE");
    else if (id == 0 || id == 0x7F) printf("  -> SUSPICIOUS: JEP106 id 0x00/0x7F is not a manufacturer");
    else printf("  -> OK");
    printf("\n");
}

/* ---------------- setup ---------------- */
static void setup_chain(void)
{
    memset(chain, 0, sizeof chain);
    /* dev0: small CPLD-like part without IDCODE (BYPASS after reset), IR 3 */
    chain[0] = (jtag_dev){ .name = "CPLD (no IDCODE)", .irlen = 3, .ir_capture = 0x1 };
    /* dev1: FPGA, IR 6, fictitious part 0xBEEF, Xilinx manufacturer code */
    chain[1] = (jtag_dev){ .name = "FPGA (fictitious part)", .irlen = 6, .ir_capture = 0x11,
                           .idcode = 0x1BEEF093, .op_idcode = 0x09,
                           .bsr_len = 4, .op_extest = 0x26, .op_sample = 0x01 };
    for (unsigned c = 0; c < 4; c++) { chain[1].cell_role[c] = CELL_IN; chain[1].cell_net[c] = (int)c; }
    /* dev2: SoC boundary-scan TAP, IR 5, fictitious IDCODE */
    chain[2] = (jtag_dev){ .name = "SoC BS TAP (fictitious)", .irlen = 5, .ir_capture = 0x01,
                           .idcode = 0x2A5C1A2B, .op_idcode = 0x01,
                           .bsr_len = 4, .op_extest = 0x00, .op_sample = 0x02 };
    for (unsigned c = 0; c < 4; c++) { chain[2].cell_role[c] = CELL_OUT; chain[2].cell_net[c] = (int)c; }
    /* dev3: Arm CoreSight JTAG-DP, IR 4, IDCODE=0xE, real-world IDCODE value 0x4BA00477 */
    chain[3] = (jtag_dev){ .name = "Arm JTAG-DP", .irlen = 4, .ir_capture = 0x1,
                           .idcode = 0x4BA00477, .op_idcode = 0xE };
    for (int i = 0; i < NDEV; i++) chain[i].dr_len = 1;
    st = SH_DR;                     /* power-up in an arbitrary state */
    tdo_stuck = -1;
    memset(net_fault, 0, sizeof net_fault);
}

/* ---------------- scenarios ---------------- */
static void scen_A(void)
{
    printf("== A. TMS sequence -> state trace (start: arbitrary state Shift-DR) ==\n");
    trace_on = 1;
    const int seq[] = {1, 1, 1, 1, 1, 0, 1, 1, 0, 0};
    printf("  TMS = 1 1 1 1 1 0 1 1 0 0   (reset, idle, go to Shift-IR)\n");
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) tck(seq[i], 1);
    trace_on = 0;
    printf("  final state: %s\n\n", sname[st]);
}

static void scen_B(void)
{
    printf("== B. from every state, TMS=1 x5 reaches Test-Logic-Reset ==\n");
    int maxsteps = 0, ok = 0;
    for (int s = 0; s < NSTATES; s++) {
        tap_state x = (tap_state)s; int k = 0;
        while (x != TLR) { x = next_state[x][1]; k++; if (k > 5) break; }
        if (x == TLR && k <= 5) ok++;
        if (k > maxsteps) maxsteps = k;
        printf("  %-16s : %d clock(s)\n", sname[s], k);
    }
    printf("  result: %d/16 states reach TLR, worst case %d clocks\n\n", ok, maxsteps);
}

static void scen_C(const char *title)
{
    printf("== %s ==\n", title);
    tap_reset();
    uint8_t out[32 * 6];
    shift(0, NULL, out, sizeof out);            /* TDI=1 throughout */
    unsigned pos = 0, n = 0;
    while (pos + 32 <= sizeof out) {
        if (out[pos] == 0) {                     /* BYPASS register captured 0 */
            if (n >= 8) { printf("  ... >8 BYPASS-looking TAPs in a row: implausible -> TDO stuck low?\n"); break; }
            printf("  TAP %u (from TDO): 1-bit BYPASS (no IDCODE)\n", n++);
            pos += 1;
            continue;
        }
        uint32_t v = bits_to_u32(&out[pos]);
        if (v == 0xFFFFFFFFu) {
            printf("  end-of-chain marker (all ones from TDI) at bit %u%s\n", pos,
                   pos == 0 ? "  -> no TAP answered at all" : "");
            break;
        }
        printf("  TAP %u (from TDO):", n++); decode_idcode(v);
        pos += 32;
    }
    printf("  TAPs found: %u\n\n", n);
}

static void scen_D(const unsigned *declared, int ndecl)
{
    printf("== D. IR length: total + Capture-IR check (declared, TDO-first:");
    for (int i = 0; i < ndecl; i++) printf(" %u", declared[i]);
    printf(") ==\n");
    tap_reset();
    /* (1) look at what Capture-IR put on TDO, (2) then flush ones and time a single 0 */
    uint8_t in[128], out[128];
    for (unsigned i = 0; i < 128; i++) in[i] = 1;
    in[64] = 0;
    shift(1, in, out, 128);
    printf("  first 18 TDO bits after Capture-IR (TDO-first): ");
    for (int i = 0; i < 18; i++) printf("%u", out[i]);
    printf("\n");
    unsigned total = 0;
    for (unsigned i = 65; i < 128; i++) if (out[i] == 0) { total = i - 64; break; }
    printf("  single 0 injected at bit 64 came out at bit %u -> total IR length = %u\n", 64 + total, total);
    unsigned sum = 0, p = 0; int okall = 1;
    for (int i = 0; i < ndecl; i++) {
        int ok = out[p] == 1 && out[p + 1] == 0;   /* IEEE: captured IR LSBs = 01 (LSB first: 1 then 0) */
        printf("  TAP %d declared irlen %u: capture LSBs at bit %u = %u%u (expect 1,0) %s\n",
               i, declared[i], p, out[p], out[p + 1], ok ? "ok" : "MISMATCH");
        okall &= ok; p += declared[i]; sum += declared[i];
    }
    printf("  declared sum %u vs measured %u -> %s\n\n", sum, total, (sum == total && okall) ? "chain config OK" : "CONFIG ERROR (fix -irlen)");
    shift(1, NULL, NULL, total ? total : 18);   /* leave all TAPs in BYPASS */
}

static void scen_E(void)
{
    printf("== E. select SoC BS TAP (dev2) IDCODE, all others BYPASS ==\n");
    tap_reset();
    /* the FIRST bit shifted in travels furthest, i.e. ends in the TDO-side TAP (dev3) bit0.
       So build the IR stream TDO-side device first, each opcode LSB first. */
    uint8_t ir[18]; unsigned k = 0;
    const jtag_dev *order[NDEV] = { &chain[3], &chain[2], &chain[1], &chain[0] };   /* TDO-first */
    for (int i = 0; i < NDEV; i++) {
        uint32_t op = (order[i] == &chain[2]) ? chain[2].op_idcode : (uint32_t)((1u << order[i]->irlen) - 1u);
        for (unsigned b = 0; b < order[i]->irlen; b++) ir[k++] = (uint8_t)((op >> b) & 1);
    }
    shift(1, ir, NULL, k);
    uint8_t out[40];
    shift(0, NULL, out, 40);
    /* DR chain: TDO <- dev3 BYPASS(1) <- dev2 IDCODE(32) <- dev1 BYPASS(1) <- dev0 BYPASS(1) */
    printf("  DR length = 1 + 32 + 1 + 1 = 35; bit0 (dev3 bypass) = %u\n", out[0]);
    printf("  dev2 IDCODE taken from bits 1..32:"); decode_idcode(bits_to_u32(&out[1]));
    printf("\n");
}

static void scen_F(void)
{
    printf("== F. boundary-scan interconnect test: SoC OUT cells -> 4 nets -> FPGA IN cells ==\n");
    printf("  injected: net0<->net1 short (wired-AND), net2 open (receiver pulled up)\n");
    net_fault[0] = FAULT_SHORT_WITH_NEXT; net_fault[2] = FAULT_OPEN;
    tap_reset();
    /* IR: dev3 BYPASS, dev2 EXTEST, dev1 EXTEST(sample of inputs), dev0 BYPASS (TDO-first) */
    uint8_t ir[18]; unsigned k = 0;
    uint32_t ops[NDEV] = { 0xF, chain[2].op_extest, chain[1].op_extest, 0x7 };
    unsigned lens[NDEV] = { 4, 5, 6, 3 };
    for (int i = 0; i < NDEV; i++) for (unsigned b = 0; b < lens[i]; b++) ir[k++] = (uint8_t)((ops[i] >> b) & 1);
    /* PRELOAD would be done first on real parts so outputs don't glitch; skipped for brevity */
    shift(1, ir, NULL, k);
    int bad[NNETS] = {0};
    const char *pname[] = { "walk1 0001", "walk1 0010", "walk1 0100", "walk1 1000",
                            "walk0 1110", "walk0 1101", "walk0 1011", "walk0 0111" };
    printf("  pattern      driven  received  result\n");
    for (int p = 0; p < 8; p++) {
        unsigned drive = p < 4 ? (1u << p) : (0xFu & ~(1u << (p - 4)));
        /* DR chain TDO-first: dev3 bypass(1), dev2 BSR(4), dev1 BSR(4), dev0 bypass(1) = 10 bits */
        uint8_t in[10] = {0}, out[10];
        for (int c = 0; c < 4; c++) in[1 + c] = (uint8_t)((drive >> c) & 1);  /* dev2 BSR */
        shift(0, in, NULL, 10);       /* Update-DR: SoC pins now drive 'drive' */
        shift(0, in, out, 10);        /* Capture-DR: FPGA captures nets; read back */
        unsigned rx = 0;
        for (int c = 0; c < 4; c++) rx |= (unsigned)out[5 + c] << c;            /* dev1 BSR */
        printf("  %s   %u%u%u%u    %u%u%u%u      %s\n", pname[p],
               (drive >> 3) & 1, (drive >> 2) & 1, (drive >> 1) & 1, drive & 1,
               (rx >> 3) & 1, (rx >> 2) & 1, (rx >> 1) & 1, rx & 1, rx == drive ? "ok" : "FAIL");
        for (int n = 0; n < NNETS; n++) if (((rx ^ drive) >> n) & 1) bad[n]++;
    }
    printf("  diagnosis per net (net3..net0 shown above left->right):\n");
    for (int n = 0; n < NNETS; n++)
        printf("    net%d: %d failing pattern(s)%s\n", n, bad[n],
               bad[n] == 0 ? "" : (n == 2 ? "  -> always reads 1: open / stuck-high" : "  -> follows neighbour: short"));
    printf("\n");
    memset(net_fault, 0, sizeof net_fault);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "decode") == 0) {
        for (int i = 2; i < argc; i++) decode_idcode((uint32_t)strtoul(argv[i], NULL, 16));
        return 0;
    }
    setup_chain();
    scen_A();
    scen_B();
    scen_C("C. IDCODE autoprobe after reset (TDI=1, 192 clocks in Shift-DR)");
    const unsigned good[NDEV] = { 4, 5, 6, 3 };       /* TDO-first */
    scen_D(good, NDEV);
    scen_E();
    scen_F();

    printf("== G. chain faults ==\n");
    tdo_stuck = 1; scen_C("G1. TDO stuck high (e.g. TDO not connected / target unpowered, pull-up)");
    tdo_stuck = 0; scen_C("G2. TDO stuck low (e.g. shorted / VTref missing on level shifter)");
    tdo_stuck = -1;
    const unsigned wrong[NDEV] = { 4, 4, 6, 3 };      /* user typed -irlen 4 for the SoC BS TAP */
    scen_D(wrong, NDEV);
    return 0;
}
