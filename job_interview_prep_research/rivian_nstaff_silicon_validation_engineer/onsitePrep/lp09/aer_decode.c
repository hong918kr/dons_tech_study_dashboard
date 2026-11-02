// aer_decode.c - decode PCIe AER registers into human-readable names, the way
// a validation script (or drivers/pci/pcie/aer.c) would.
//   * bit names / offsets: include/uapi/linux/pci_regs.h (PCI_ERR_*)
//   * short names ("RxErr", "MalfTLP"...) and layer/agent mapping: drivers/pci/pcie/aer.c
//   * extra "hint" column = what the error usually points at (validation heuristic, not spec)
// Inputs:
//   case A: REAL  - Jetson Orin NX + Samsung NVMe, kernel 5.10 (NVIDIA forum, cited in LP09 §8)
//   case B: REAL  - QEMU pcie-root-port, kernel 6.18.55, injected with
//                   "pcie_aer_inject_error rp1 0x100000" (onsitePrep/lp07/qemu_lab/)
//   case C: REAL  - same, fatal "0x40000" + the AER capability bytes dumped from config space
//   case D: DOC   - TLP header from Documentation/PCI/pcieaer-howto.rst example
//
// macOS build (spec flags):
//   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
//      -Wall -Wextra -Werror -fsanitize=address,undefined aer_decode.c -o /tmp/aer && /tmp/aer
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* AER extended capability register offsets (from the cap header) */
#define PCI_ERR_UNCOR_STATUS 0x04
#define PCI_ERR_UNCOR_MASK   0x08
#define PCI_ERR_UNCOR_SEVER  0x0c
#define PCI_ERR_COR_STATUS   0x10
#define PCI_ERR_COR_MASK     0x14
#define PCI_ERR_CAP          0x18
#define  PCI_ERR_CAP_FEP(x)  ((x) & 0x1f)
#define PCI_ERR_HEADER_LOG   0x1c
#define PCI_ERR_ROOT_COMMAND 0x2c
#define PCI_ERR_ROOT_STATUS  0x30
#define PCI_ERR_ROOT_ERR_SRC 0x34

struct bitname { int bit; const char *name, *shortname, *layer, *hint; };

/* Uncorrectable Error Status bits (PCI_ERR_UNC_*) */
static const struct bitname unc[] = {
    { 0, "Undefined",                  "Undefined",   "Transaction", "legacy bit; ignore"},
    { 4, "Data Link Protocol",         "DLP",         "Data Link",   "DLL bug/SI: bad ACK/NAK seq"},
    { 5, "Surprise Down",              "SDES",        "Data Link",   "link dropped: PERST/power/SI/hot-unplug"},
    {12, "Poisoned TLP",               "TLP",         "Transaction", "upstream data corruption (ECC upstream)"},
    {13, "Flow Control Protocol",      "FCP",         "Transaction", "credit bug in a controller (RTL/FW)"},
    {14, "Completion Timeout",         "CmpltTO",     "Transaction", "device hung / link down / wrong address"},
    {15, "Completer Abort",            "CmpltAbrt",   "Transaction", "completer refused (device FW/HW)"},
    {16, "Unexpected Completion",      "UnxCmplt",    "Transaction", "tag/ID mismatch, routing bug"},
    {17, "Receiver Overflow",          "RxOF",        "Transaction", "sender ignored credits (RTL)"},
    {18, "Malformed TLP",              "MalfTLP",     "Transaction", "bad TLP format: RTL/IP bug, MPS mismatch"},
    {19, "ECRC Error",                 "ECRC",        "Transaction", "end-to-end CRC: corruption inside a switch/fabric"},
    {20, "Unsupported Request",        "UnsupReq",    "Transaction", "SW bug: access to unmapped addr / disabled fn"},
    {21, "ACS Violation",              "ACSViol",     "Transaction", "P2P/isolation policy hit"},
    {22, "Uncorrectable Internal",     "UncorrIntErr","General",     "device internal (SRAM ECC etc.)"},
    {23, "MC Blocked TLP",             "BlockedTLP",  "Transaction", "multicast"},
    {24, "AtomicOp Egress Blocked",    "AtomicOpBlocked", "Transaction", "AtomicOp not supported on path"},
    {25, "TLP Prefix Blocked",         "TLPBlockedErr","Transaction","E2E prefix not supported on path"},
    {26, "Poisoned TLP Egress Blocked","PoisonTLPBlocked","Transaction","DPC-related"},
    {27, "DMWr Request Egress Blocked","DMWrReqBlocked","Transaction","Gen6 deferrable write"},
    {28, "IDE Check Failed",           "IDECheck",    "Transaction", "link encryption (IDE) integrity"},
    {29, "Misrouted IDE TLP",          "MisIDETLP",   "Transaction", "IDE stream routing"},
    {30, "PCRC Check Failed",          "PCRC_CHECK",  "Transaction", "IDE PCRC"},
    {31, "TLP Translation Egress Blocked","TLPXlatBlocked","Transaction","ATS/translation"},
};
/* Correctable Error Status bits (PCI_ERR_COR_*) */
static const struct bitname cor[] = {
    { 0, "Receiver Error",        "RxErr",       "Physical",  "SI: 8b10b/128b130b symbol, framing, lane deskew"},
    { 6, "Bad TLP",               "BadTLP",      "Data Link", "LCRC fail -> NAK+replay (SI)"},
    { 7, "Bad DLLP",              "BadDLLP",     "Data Link", "DLLP CRC fail (SI)"},
    { 8, "REPLAY_NUM Rollover",   "Rollover",    "Data Link", "4 replays in a row: bad link or partner not ACKing"},
    {12, "Replay Timer Timeout",  "Timeout",     "Data Link", "no ACK in time: SI, or partner stalled (ASPM exit?)"},
    {13, "Advisory Non-Fatal",    "NonFatalErr", "",          "an uncorrectable err downgraded (e.g. UR on probe)"},
    {14, "Corrected Internal",    "CorrIntErr",  "General",   "device internal ECC corrected"},
    {15, "Header Log Overflow",   "HeaderOF",    "General",   "too many errors to log"},
};

static int print_bits(const char *what, uint32_t status, uint32_t mask, uint32_t sev,
                      const struct bitname *t, size_t n, int first, int is_unc)
{
    int printed = 0;
    printf("  %s status/mask=%08x/%08x\n", what, status, mask);
    for (size_t i = 0; i < n; i++) {
        uint32_t b = 1u << t[i].bit;
        if (!(status & b)) continue;
        printf("    [%2d] %-14s %-12s %-11s %s%s%s\n", t[i].bit, t[i].shortname,
               is_unc ? ((sev & b) ? "Fatal" : "NonFatal") : "Correctable",
               t[i].layer, (mask & b) ? "(MASKED) " : "", t[i].hint,
               (is_unc && first == t[i].bit) ? "  (First)" : "");
        printed++;
    }
    return printed;
}

/* minimal TLP header DW0..DW2 decode (enough for AER header log reading) */
static void decode_tlp(const uint32_t h[4])
{
    unsigned fmt = h[0] >> 29 & 7, type = h[0] >> 24 & 0x1f, len = h[0] & 0x3ff;
    const char *n = "?";
    if (type == 0x00) n = (fmt & 2) ? "MWr" : "MRd";
    else if (type == 0x04) n = (fmt & 2) ? "CfgWr0" : "CfgRd0";
    else if (type == 0x05) n = (fmt & 2) ? "CfgWr1" : "CfgRd1";
    else if (type == 0x0a) n = (fmt & 2) ? "CplD" : "Cpl";
    else if ((type & 0x18) == 0x10) n = "Msg";
    printf("  TLP Header: 0x%08x 0x%08x 0x%08x 0x%08x\n", h[0], h[1], h[2], h[3]);
    printf("    DW0 fmt=%u type=0x%02x -> %s%s, length=%u DW\n", fmt, type, n,
           (fmt & 1) ? " (4DW hdr/64-bit)" : "", len ? len : 1024);
    unsigned rid = h[1] >> 16;
    printf("    DW1 requester=%02x:%02x.%u tag=0x%02x lastBE=0x%x firstBE=0x%x\n",
           rid >> 8, rid >> 3 & 0x1f, rid & 7, h[1] >> 8 & 0xff, h[1] >> 4 & 0xf, h[1] & 0xf);
    if (type == 0x04 || type == 0x05) {
        unsigned tgt = h[2] >> 16, reg = h[2] & 0xffc;
        printf("    DW2 target=%02x:%02x.%u register=0x%03x\n", tgt >> 8, tgt >> 3 & 0x1f, tgt & 7, reg);
    } else if (type == 0x00) {
        printf("    DW2 address=0x%08x\n", h[2] & ~3u);
    }
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void)
{
    puts("[A] Jetson Orin NX nvme 0004:01:00.0 (real dmesg, 5.10): status/mask=00000040/0000e000");
    int n = print_bits("COR", 0x00000040, 0x0000e000, 0, cor, sizeof cor / sizeof cor[0], -1, 0);
    assert(n == 1);
    puts("    note: mask e000 = bits 13..15 (ANFE, CorrIntErr, HeaderOF) masked = spec default");

    puts("[B] QEMU root port 00:01.0 (real, 6.18.55, injected UR)");
    n = print_bits("UNC", 0x00100000, 0x02400000, 0x00462030, unc, sizeof unc / sizeof unc[0], 20, 1);
    assert(n == 1);

    puts("[C] QEMU root port AER capability bytes @0x100 (real dump) + injected MalfTLP");
    const uint8_t aer[0x38] = {
        0x01,0x00,0x82,0x14, 0x00,0x00,0x00,0x00, 0x00,0x00,0x40,0x02, 0x30,0x20,0x46,0x00,
        0x00,0x00,0x00,0x00, 0x00,0xe0,0x00,0x00, 0xa0,0x02,0x00,0x00, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0x07,0x00,0x00,0x00, 0,0,0,0, 0,0,0,0 };
    uint32_t hdr = le32(aer);
    printf("  ext cap header=0x%08x id=0x%04x ver=%u next=0x%03x\n", hdr, hdr & 0xffff, hdr >> 16 & 0xf, hdr >> 20);
    uint32_t umask = le32(aer + PCI_ERR_UNCOR_MASK), usev = le32(aer + PCI_ERR_UNCOR_SEVER);
    uint32_t cmask = le32(aer + PCI_ERR_COR_MASK), capc = le32(aer + PCI_ERR_CAP);
    uint32_t rcmd = le32(aer + PCI_ERR_ROOT_COMMAND);
    assert((hdr & 0xffff) == 0x0001 && (hdr >> 20) == 0x148);
    assert(umask == 0x02400000 && usev == 0x00462030 && cmask == 0x0000e000 && rcmd == 7);
    printf("  UNC mask=%08x severity=%08x  COR mask=%08x  Cap&Ctl=%08x (FEP=%u, ECRC gen/chk capable=%c/%c, enabled=%c/%c)\n",
           umask, usev, cmask, capc, PCI_ERR_CAP_FEP(capc), capc & 0x20 ? 'y' : 'n',
           capc & 0x80 ? 'y' : 'n', capc & 0x40 ? 'y' : 'n', capc & 0x100 ? 'y' : 'n');
    printf("  default-Fatal bits in severity:");
    for (size_t i = 0; i < sizeof unc / sizeof unc[0]; i++)
        if (usev & 1u << unc[i].bit) printf(" %s", unc[i].shortname);
    printf("\n  Root Error Command=%x (COR_EN=%u NONFATAL_EN=%u FATAL_EN=%u)\n",
           rcmd, rcmd & 1, rcmd >> 1 & 1, rcmd >> 2 & 1);
    n = print_bits("UNC", 0x00040000, umask, usev, unc, sizeof unc / sizeof unc[0], 18, 1);
    assert(n == 1 && (usev & 0x00040000));   /* MalfTLP is Fatal by default severity */

    puts("[C2] Root Error Status / Error Source decode (synthetic values)");
    uint32_t rsta = 0x00000055, esrc = 0x01000100;   /* ERR_COR + ERR_FATAL rcvd, first uncor is fatal */
    printf("  RootErrSta=%08x: %s%s%s%s%s  AER IRQ msg#=%u\n", rsta,
           rsta & 0x01 ? "ERR_COR_rcvd " : "", rsta & 0x02 ? "multiple_COR " : "",
           rsta & 0x04 ? "ERR_FATAL/NONFATAL_rcvd " : "", rsta & 0x10 ? "first_uncor_is_FATAL " : "",
           rsta & 0x40 ? "FATAL_msg_rcvd" : "", rsta >> 27);
    printf("  ErrSrcID=%08x: ERR_COR from %02x:%02x.%u, ERR_FATAL/NONFATAL from %02x:%02x.%u\n", esrc,
           (esrc & 0xffff) >> 8, (esrc >> 3) & 0x1f, esrc & 7,
           esrc >> 24, (esrc >> 19) & 0x1f, (esrc >> 16) & 7);

    puts("[D] Documentation/PCI/pcieaer-howto.rst example header (UR, Fatal)");
    const uint32_t tlp[4] = {0x04000001, 0x00200a03, 0x05010000, 0x00050100};
    decode_tlp(tlp);
    assert((tlp[0] >> 24 & 0x1f) == 0x04);

    puts("ALL PASS");
    return 0;
}
