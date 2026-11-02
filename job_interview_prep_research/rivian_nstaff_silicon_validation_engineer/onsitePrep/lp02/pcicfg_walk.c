/* pcicfg_walk.c - a tiny "lspci -vvv" for one function: decode the config header,
 * walk the standard capability list (0x34 -> ...) and the extended capability list
 * (0x100 -> ...), decode the PCI Express capability and AER/DSN.
 *
 *   pcicfg_walk 0000:00:01.0            read /sys/bus/pci/devices/<BDF>/config and decode
 *   pcicfg_walk /path/to/config         same, any file (e.g. a saved 4 KiB dump)
 *   pcicfg_walk -x 0000:00:01.0         print an `lspci -xxxx`-style hex dump (feed to `lspci -F`)
 *   pcicfg_walk --selftest              fake config spaces + asserts (works on macOS too)
 *
 * Offsets / IDs follow include/uapi/linux/pci_regs.h (Linux 6.18).
 * macOS: cc -isysroot <MacOSX26.4.sdk> -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined pcicfg_walk.c
 * Linux: gcc -std=c11 -O2 -Wall -Wextra -Werror pcicfg_walk.c -o pcicfg_walk   (add -static for an initramfs)
 * Reading past byte 63 of sysfs config needs root (CAP_SYS_ADMIN); otherwise the kernel returns 64 bytes.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- register offsets (pci_regs.h names) ---- */
#define PCI_VENDOR_ID        0x00
#define PCI_DEVICE_ID        0x02
#define PCI_COMMAND          0x04
#define PCI_STATUS           0x06
#define PCI_CLASS_REVISION   0x08
#define PCI_HEADER_TYPE      0x0e
#define PCI_BASE_ADDRESS_0   0x10
#define PCI_PRIMARY_BUS      0x18
#define PCI_SECONDARY_BUS    0x19
#define PCI_SUBORDINATE_BUS  0x1a
#define PCI_IO_BASE          0x1c
#define PCI_MEMORY_BASE      0x20
#define PCI_PREF_MEMORY_BASE 0x24
#define PCI_PREF_BASE_UPPER32 0x28
#define PCI_PREF_LIMIT_UPPER32 0x2c
#define PCI_CAPABILITY_LIST  0x34
#define PCI_INTERRUPT_PIN    0x3d
#define PCI_BRIDGE_CONTROL   0x3e

#define PCI_COMMAND_IO       0x0001
#define PCI_COMMAND_MEMORY   0x0002
#define PCI_COMMAND_MASTER   0x0004
#define PCI_COMMAND_SERR     0x0100
#define PCI_COMMAND_INTX_DISABLE 0x0400
#define PCI_STATUS_INTERRUPT 0x0008
#define PCI_STATUS_CAP_LIST  0x0010
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_DETECTED_PARITY  0x8000

#define PCI_CAP_ID_PM    0x01
#define PCI_CAP_ID_MSI   0x05
#define PCI_CAP_ID_VNDR  0x09
#define PCI_CAP_ID_SSVID 0x0d
#define PCI_CAP_ID_EXP   0x10
#define PCI_CAP_ID_MSIX  0x11

/* PCIe capability (offsets from the capability start) */
#define PCI_EXP_FLAGS   0x02
#define PCI_EXP_DEVCAP  0x04
#define PCI_EXP_DEVCTL  0x08
#define PCI_EXP_DEVSTA  0x0a
#define PCI_EXP_LNKCAP  0x0c
#define PCI_EXP_LNKCTL  0x10
#define PCI_EXP_LNKSTA  0x12
#define PCI_EXP_SLTCAP  0x14
#define PCI_EXP_SLTCTL  0x18
#define PCI_EXP_SLTSTA  0x1a
#define PCI_EXP_RTCTL   0x1c
#define PCI_EXP_DEVCAP2 0x24
#define PCI_EXP_DEVCTL2 0x28
#define PCI_EXP_LNKCAP2 0x2c
#define PCI_EXP_LNKCTL2 0x30
#define PCI_EXP_LNKSTA2 0x32

/* Extended capabilities */
#define PCI_CFG_SPACE_SIZE     256
#define PCI_CFG_SPACE_EXP_SIZE 4096
#define PCI_EXT_CAP_ID(h)   ((h) & 0x0000ffffu)
#define PCI_EXT_CAP_VER(h)  (((h) >> 16) & 0xfu)
#define PCI_EXT_CAP_NEXT(h) (((h) >> 20) & 0xffcu)
#define PCI_EXT_CAP_ID_ERR  0x0001
#define PCI_EXT_CAP_ID_DSN  0x0003
#define PCI_ERR_UNCOR_STATUS 0x04
#define PCI_ERR_UNCOR_MASK   0x08
#define PCI_ERR_UNCOR_SEVER  0x0c
#define PCI_ERR_COR_STATUS   0x10
#define PCI_ERR_COR_MASK     0x14

#define STD_CAP_TTL 48                                            /* same as kernel PCI_FIND_CAP_TTL */
#define EXT_CAP_TTL ((PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8) /* 480, as in kernel PCI_FIND_NEXT_EXT_CAP */

/* ---- bounded accessors: out of range reads as all-ones, like a master abort ---- */
typedef struct { const uint8_t *b; size_t len; } cfg_t;

static uint8_t rd8(cfg_t c, unsigned off) { return off < c.len ? c.b[off] : 0xff; }
static uint16_t rd16(cfg_t c, unsigned off) { return (uint16_t)(rd8(c, off) | (rd8(c, off + 1) << 8)); }
static uint32_t rd32(cfg_t c, unsigned off) { return rd16(c, off) | ((uint32_t)rd16(c, off + 2) << 16); }

/* ---- capability walks ---- */
typedef struct { unsigned off; unsigned id; unsigned ver; } cap_t;   /* ver only for ext caps */

/* Fill caps[] (up to max) in list order. Returns count, or -ENODEV / -EINVAL / -ELOOP. */
int walk_std_caps(cfg_t c, cap_t *caps, int max)
{
    if (rd16(c, PCI_VENDOR_ID) == 0xffff)
        return -ENODEV;
    if (!(rd16(c, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;
    unsigned p = rd8(c, PCI_CAPABILITY_LIST) & 0xfcu;          /* low 2 bits are reserved */
    int n = 0;
    for (int ttl = STD_CAP_TTL; p; ttl--) {
        if (ttl == 0)
            return -ELOOP;
        if (p < 0x40 || p > 0xfc)                                 /* caps live in 0x40..0xff */
            return -EINVAL;
        if (p + 2 > c.len)
            return -ERANGE;                                       /* truncated view (non-root read) */
        if (n < max)
            caps[n] = (cap_t){ p, rd8(c, p), 0 };
        n++;
        p = rd8(c, p + 1) & 0xfcu;
    }
    return n;
}

int walk_ext_caps(cfg_t c, cap_t *caps, int max)
{
    if (c.len < PCI_CFG_SPACE_EXP_SIZE)
        return 0;                                                  /* 256 B view: no ext space */
    unsigned p = PCI_CFG_SPACE_SIZE;
    uint32_t h = rd32(c, p);
    if (h == 0 || h == 0xffffffffu)
        return 0;                                                  /* no extended capabilities */
    int n = 0;
    for (int ttl = EXT_CAP_TTL; ; ttl--) {
        if (ttl == 0)
            return -ELOOP;
        if (n < max)
            caps[n] = (cap_t){ p, PCI_EXT_CAP_ID(h), PCI_EXT_CAP_VER(h) };
        n++;
        p = PCI_EXT_CAP_NEXT(h);
        if (p == 0)
            return n;
        if (p < PCI_CFG_SPACE_SIZE)
            return -EINVAL;
        h = rd32(c, p);
        if (h == 0 || h == 0xffffffffu)
            return -EINVAL;
    }
}

int find_cap(cfg_t c, unsigned id)
{
    cap_t caps[STD_CAP_TTL];
    int n = walk_std_caps(c, caps, STD_CAP_TTL);
    if (n < 0)
        return n;
    for (int i = 0; i < n; i++)
        if (caps[i].id == id)
            return (int)caps[i].off;
    return -ENOENT;
}

int find_ext_cap(cfg_t c, unsigned id)
{
    static cap_t caps[EXT_CAP_TTL];
    int n = walk_ext_caps(c, caps, EXT_CAP_TTL);
    if (n < 0)
        return n;
    for (int i = 0; i < n && i < EXT_CAP_TTL; i++)
        if (caps[i].id == id)
            return (int)caps[i].off;
    return -ENOENT;
}

/* BAR sizing helper: `mask` is what the BAR reads back after writing all-ones
 * (lo dword, and hi dword for a 64-bit BAR). Size = ~(mask & ~flags) + 1. */
uint64_t bar_size(uint32_t lo_mask, uint32_t hi_mask, bool is64, bool is_io)
{
    uint64_t m;
    if (is_io)
        m = 0xffffffffffff0000ull | (lo_mask & ~0x3u); /* I/O BARs: upper 16 bits may read 0 */
    else if (is64)
        m = ((uint64_t)hi_mask << 32) | (lo_mask & ~0xfu);
    else
        m = 0xffffffff00000000ull | (lo_mask & ~0xfu);
    if ((uint32_t)m == 0 && !is64)
        return 0;                                      /* BAR not implemented */
    return ~m + 1;
}

/* ---- names ---- */
static const char *cap_name(unsigned id)
{
    switch (id) {
    case 0x01: return "Power Management";
    case 0x03: return "VPD";
    case 0x05: return "MSI";
    case 0x09: return "Vendor-Specific";
    case 0x0d: return "Bridge Subsystem VID";
    case 0x10: return "PCI Express";
    case 0x11: return "MSI-X";
    case 0x12: return "SATA";
    case 0x13: return "Advanced Features";
    case 0x14: return "Enhanced Allocation";
    default:   return "?";
    }
}

static const char *ext_cap_name(unsigned id)
{
    switch (id) {
    case 0x01: return "AER";
    case 0x02: return "Virtual Channel";
    case 0x03: return "Device Serial Number";
    case 0x04: return "Power Budgeting";
    case 0x0b: return "Vendor-Specific (VSEC)";
    case 0x0d: return "ACS";
    case 0x0e: return "ARI";
    case 0x0f: return "ATS";
    case 0x10: return "SR-IOV";
    case 0x15: return "Resizable BAR";
    case 0x17: return "TPH";
    case 0x18: return "LTR";
    case 0x19: return "Secondary PCIe";
    case 0x1b: return "PASID";
    case 0x1d: return "DPC";
    case 0x1e: return "L1 PM Substates";
    case 0x1f: return "PTM";
    case 0x23: return "DVSEC";
    case 0x25: return "Data Link Feature";
    case 0x26: return "Physical Layer 16.0 GT/s";
    case 0x2a: return "Physical Layer 32.0 GT/s";
    case 0x2e: return "DOE";
    case 0x31: return "Physical Layer 64.0 GT/s";
    default:   return "?";
    }
}

static const char *port_type(unsigned t)
{
    static const char *n[] = { "Endpoint", "Legacy Endpoint", "?", "?", "Root Port",
                               "Upstream Port", "Downstream Port", "PCIe-to-PCI Bridge",
                               "PCI-to-PCIe Bridge", "RC Integrated Endpoint", "RC Event Collector" };
    return t < sizeof n / sizeof n[0] ? n[t] : "?";
}

static const char *speed(unsigned code)
{
    static const char *s[] = { "?", "2.5GT/s", "5GT/s", "8GT/s", "16GT/s", "32GT/s", "64GT/s" };
    return code < sizeof s / sizeof s[0] ? s[code] : "?";
}

#define PM(v, bit) (((v) & (bit)) ? '+' : '-')

/* ---- decoders ---- */
static void decode_bars(cfg_t c, unsigned nbars)
{
    for (unsigned i = 0; i < nbars; i++) {
        unsigned off = PCI_BASE_ADDRESS_0 + 4 * i;
        uint32_t v = rd32(c, off);
        if (v == 0)
            continue;
        if (v & 1) {
            printf("  BAR%u  [%02x] 0x%08x  I/O  base=0x%x\n", i, off, v, v & ~0x3u);
            continue;
        }
        unsigned type = (v >> 1) & 3;
        bool pref = v & 8;
        if (type == 2 && i + 1 < nbars) {
            uint64_t base = ((uint64_t)rd32(c, off + 4) << 32) | (v & ~0xfu);
            printf("  BAR%u  [%02x] 0x%08x  mem 64-bit %s  base=0x%llx  (BAR%u = upper half)\n", i, off, v,
                   pref ? "prefetchable" : "non-prefetchable", (unsigned long long)base, i + 1);
            i++;
        } else {
            printf("  BAR%u  [%02x] 0x%08x  mem 32-bit %s  base=0x%x\n", i, off, v,
                   pref ? "prefetchable" : "non-prefetchable", v & ~0xfu);
        }
    }
    printf("  (sizes need write-all-ones/readback; read them from sysfs 'resource' instead)\n");
}

static void decode_bridge(cfg_t c)
{
    printf("  Bus: primary=%02x secondary=%02x subordinate=%02x\n",
           rd8(c, PCI_PRIMARY_BUS), rd8(c, PCI_SECONDARY_BUS), rd8(c, PCI_SUBORDINATE_BUS));
    uint32_t mb = (uint32_t)(rd16(c, PCI_MEMORY_BASE) & 0xfff0u) << 16;
    uint32_t ml = ((uint32_t)(rd16(c, PCI_MEMORY_BASE + 2) & 0xfff0u) << 16) | 0xfffffu;
    if (mb <= ml)
        printf("  Memory window: 0x%08x-0x%08x\n", mb, ml);
    else
        printf("  Memory window: closed\n");
    uint64_t pb = (uint64_t)(rd16(c, PCI_PREF_MEMORY_BASE) & 0xfff0u) << 16;
    uint64_t pl = ((uint64_t)(rd16(c, PCI_PREF_MEMORY_BASE + 2) & 0xfff0u) << 16) | 0xfffffu;
    if (rd16(c, PCI_PREF_MEMORY_BASE) & 1) {                        /* 64-bit prefetchable window */
        pb |= (uint64_t)rd32(c, PCI_PREF_BASE_UPPER32) << 32;
        pl |= (uint64_t)rd32(c, PCI_PREF_LIMIT_UPPER32) << 32;
    }
    if (pb <= pl)
        printf("  Prefetchable window: 0x%llx-0x%llx\n", (unsigned long long)pb, (unsigned long long)pl);
    else
        printf("  Prefetchable window: closed\n");
    unsigned ib = (rd8(c, PCI_IO_BASE) & 0xf0u) << 8, il = ((rd8(c, PCI_IO_BASE + 1) & 0xf0u) << 8) | 0xfffu;
    printf("  I/O window: %s", ib <= il ? "" : "closed");
    if (ib <= il)
        printf("0x%04x-0x%04x", ib, il);
    uint16_t bc = rd16(c, PCI_BRIDGE_CONTROL);
    printf("   BridgeCtl=0x%04x SecBusReset%c\n", bc, PM(bc, 0x40));
}

static void decode_pcie(cfg_t c, unsigned e)
{
    uint16_t fl = rd16(c, e + PCI_EXP_FLAGS);
    unsigned ver = fl & 0xf, type = (fl >> 4) & 0xf;
    printf("    PCIe cap v%u, type %u = %s, Slot%c\n", ver, type, port_type(type), PM(fl, 0x100));
    uint32_t dc = rd32(c, e + PCI_EXP_DEVCAP);
    uint16_t dt = rd16(c, e + PCI_EXP_DEVCTL), ds = rd16(c, e + PCI_EXP_DEVSTA);
    printf("    DevCap [+04] 0x%08x  MaxPayload %u B, FLR%c\n", dc, 128u << (dc & 7), PM(dc, 1u << 28));
    printf("    DevCtl [+08] 0x%04x      MaxPayload %u B, MaxReadReq %u B, CorrErr%c NonFatal%c Fatal%c UnsupReq%c\n",
           dt, 128u << ((dt >> 5) & 7), 128u << ((dt >> 12) & 7), PM(dt, 1), PM(dt, 2), PM(dt, 4), PM(dt, 8));
    printf("    DevSta [+0a] 0x%04x      CorrErr%c NonFatal%c Fatal%c UnsupReq%c TransPend%c\n",
           ds, PM(ds, 1), PM(ds, 2), PM(ds, 4), PM(ds, 8), PM(ds, 0x20));
    uint32_t lc = rd32(c, e + PCI_EXP_LNKCAP);
    uint16_t lt = rd16(c, e + PCI_EXP_LNKCTL), ls = rd16(c, e + PCI_EXP_LNKSTA);
    printf("    LnkCap [+0c] 0x%08x  Port #%u, Speed %s, Width x%u, ASPM%s%s, DLActiveRep%c\n",
           lc, lc >> 24, speed(lc & 0xf), (lc >> 4) & 0x3f,
           (lc & 0x400) ? " L0s" : "", (lc & 0x800) ? " L1" : "", PM(lc, 1u << 20));
    printf("    LnkCtl [+10] 0x%04x      ASPM=%u LinkDisable%c RetrainLink%c CommClk%c\n",
           lt, lt & 3, PM(lt, 0x10), PM(lt, 0x20), PM(lt, 0x40));
    printf("    LnkSta [+12] 0x%04x      Speed %s, Width x%u, Train%c SlotClk%c DLActive%c BWMgmt%c ABWMgmt%c\n",
           ls, speed(ls & 0xf), (ls >> 4) & 0x3f, PM(ls, 0x800), PM(ls, 0x1000), PM(ls, 0x2000),
           PM(ls, 0x4000), PM(ls, 0x8000));
    if (fl & 0x100) {
        uint32_t sc = rd32(c, e + PCI_EXP_SLTCAP);
        uint16_t ss = rd16(c, e + PCI_EXP_SLTSTA);
        printf("    SltCap [+14] 0x%08x  HotPlug%c PhysSlot #%u | SltCtl [+18] 0x%04x | SltSta [+1a] 0x%04x PresDet%c DLLStateChg%c\n",
               sc, PM(sc, 0x40), sc >> 19, rd16(c, e + PCI_EXP_SLTCTL), ss, PM(ss, 0x40), PM(ss, 0x100));
    }
    if (type == 4 || type == 0xa)
        printf("    RootCtl[+1c] 0x%04x\n", rd16(c, e + PCI_EXP_RTCTL));
    if (ver >= 2) {
        uint32_t lc2 = rd32(c, e + PCI_EXP_LNKCAP2);
        printf("    DevCap2[+24] 0x%08x  DevCtl2[+28] 0x%04x (CompTimeout range %u)\n",
               rd32(c, e + PCI_EXP_DEVCAP2), rd16(c, e + PCI_EXP_DEVCTL2), rd16(c, e + PCI_EXP_DEVCTL2) & 0xf);
        printf("    LnkCap2[+2c] 0x%08x  SupportedSpeeds:", lc2);
        for (unsigned b = 1; b <= 6; b++)
            if (lc2 & (1u << b))
                printf(" %s", speed(b));
        uint16_t lt2 = rd16(c, e + PCI_EXP_LNKCTL2);
        printf("\n    LnkCtl2[+30] 0x%04x      TargetSpeed %s | LnkSta2[+32] 0x%04x\n",
               lt2, speed(lt2 & 0xf), rd16(c, e + PCI_EXP_LNKSTA2));
    }
}

static void decode_aer(cfg_t c, unsigned a)
{
    printf("      UncorSta 0x%08x  UncorMask 0x%08x  UncorSev 0x%08x\n",
           rd32(c, a + PCI_ERR_UNCOR_STATUS), rd32(c, a + PCI_ERR_UNCOR_MASK), rd32(c, a + PCI_ERR_UNCOR_SEVER));
    uint32_t cs = rd32(c, a + PCI_ERR_COR_STATUS);
    printf("      CorSta   0x%08x  (RxErr%c BadTLP%c BadDLLP%c Rollover%c Timeout%c)  CorMask 0x%08x\n",
           cs, PM(cs, 1), PM(cs, 0x40), PM(cs, 0x80), PM(cs, 0x100), PM(cs, 0x1000), rd32(c, a + PCI_ERR_COR_MASK));
}

void decode_all(cfg_t c, const char *label)
{
    printf("== %s  (%zu bytes of config space)\n", label, c.len);
    if (c.len < 64) {
        printf("  short read (<64 B)\n");
        return;
    }
    uint16_t vid = rd16(c, PCI_VENDOR_ID);
    if (vid == 0xffff) {
        printf("  Vendor ID 0xffff: no function here (or the link is down)\n");
        return;
    }
    uint32_t cr = rd32(c, PCI_CLASS_REVISION);
    uint8_t ht = rd8(c, PCI_HEADER_TYPE);
    printf("  [00] %04x:%04x  class %06x rev %02x  header type %u (%s)%s\n", vid, rd16(c, PCI_DEVICE_ID),
           cr >> 8, cr & 0xff, ht & 0x7f, (ht & 0x7f) == 0 ? "Type 0 / device" : (ht & 0x7f) == 1 ? "Type 1 / bridge" : "other",
           (ht & 0x80) ? " multi-function" : "");
    uint16_t cmd = rd16(c, PCI_COMMAND), sta = rd16(c, PCI_STATUS);
    printf("  [04] Command 0x%04x: I/O%c Mem%c BusMaster%c SERR%c DisINTx%c\n", cmd,
           PM(cmd, PCI_COMMAND_IO), PM(cmd, PCI_COMMAND_MEMORY), PM(cmd, PCI_COMMAND_MASTER),
           PM(cmd, PCI_COMMAND_SERR), PM(cmd, PCI_COMMAND_INTX_DISABLE));
    printf("  [06] Status  0x%04x: Cap%c INTx%c <MAbort%c >SERR%c DetParity%c\n", sta,
           PM(sta, PCI_STATUS_CAP_LIST), PM(sta, PCI_STATUS_INTERRUPT), PM(sta, PCI_STATUS_REC_MASTER_ABORT),
           PM(sta, PCI_STATUS_SIG_SYSTEM_ERROR), PM(sta, PCI_STATUS_DETECTED_PARITY));
    if ((ht & 0x7f) == 0)
        decode_bars(c, 6);
    else if ((ht & 0x7f) == 1) {
        decode_bars(c, 2);
        decode_bridge(c);
    }
    printf("  [3d] Interrupt pin %u (%s)\n", rd8(c, PCI_INTERRUPT_PIN),
           rd8(c, PCI_INTERRUPT_PIN) ? "INTA..INTD = 1..4" : "none");

    cap_t caps[STD_CAP_TTL];
    int n = walk_std_caps(c, caps, STD_CAP_TTL);
    if (n < 0) {
        printf("  capability list: %s\n", n == -ELOOP ? "LOOP (gave up after 48 hops)" :
               n == -ERANGE ? "beyond the bytes we could read (run as root)" : "bad pointer");
        n = 0;
    }
    bool pcie = false;
    for (int i = 0; i < n && c.len >= 256; i++) {
        unsigned nx = rd8(c, caps[i].off + 1);
        printf("  cap  [%02x] id 0x%02x %-18s next 0x%02x\n", caps[i].off, caps[i].id, cap_name(caps[i].id), nx);
        if (caps[i].id == PCI_CAP_ID_EXP) {
            decode_pcie(c, caps[i].off);
            pcie = true;
        }
    }
    static cap_t ecaps[EXT_CAP_TTL];
    int en = walk_ext_caps(c, ecaps, EXT_CAP_TTL);
    if (!pcie || c.len < PCI_CFG_SPACE_EXP_SIZE) {
        printf("  ext caps: none visible (%s)\n", !pcie ? "no PCIe capability -> kernel exposes 256 B" : "only 256 B readable");
        return;
    }
    if (en < 0)
        printf("  ext cap list: %s\n", en == -ELOOP ? "LOOP (gave up after 480 hops)" : "bad pointer/header");
    if (en == 0)
        printf("  ext caps: none (dword at 0x100 is 0)\n");
    for (int i = 0; i < en && i < EXT_CAP_TTL; i++) {
        uint32_t h = rd32(c, ecaps[i].off);
        printf("  ecap [%03x] id 0x%04x v%u %-24s next 0x%03x\n", ecaps[i].off, ecaps[i].id, ecaps[i].ver,
               ext_cap_name(ecaps[i].id), PCI_EXT_CAP_NEXT(h));
        if (ecaps[i].id == PCI_EXT_CAP_ID_ERR)
            decode_aer(c, ecaps[i].off);
        if (ecaps[i].id == PCI_EXT_CAP_ID_DSN)
            printf("      Serial %08x-%08x\n", rd32(c, ecaps[i].off + 8), rd32(c, ecaps[i].off + 4));
    }
}

/* lspci -xxxx style: "BB:DD.F label" then 16 bytes per line. `lspci -F file` can parse it. */
void hexdump_lspci(cfg_t c, const char *bdf)
{
    printf("%s Dump\n", bdf);
    for (size_t o = 0; o < c.len; o += 16) {
        printf("%02zx:", o);
        for (size_t i = 0; i < 16 && o + i < c.len; i++)
            printf(" %02x", c.b[o + i]);
        printf("\n");
    }
    printf("\n");
}

/* ---------------- self-test with fake config spaces ---------------- */
static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

static void w8(uint8_t *b, unsigned o, uint8_t v) { b[o] = v; }
static void w16(uint8_t *b, unsigned o, uint16_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *b, unsigned o, uint32_t v) { w16(b, o, (uint16_t)v); w16(b, o + 2, (uint16_t)(v >> 16)); }
static void wext(uint8_t *b, unsigned o, unsigned id, unsigned ver, unsigned next)
{
    w32(b, o, (uint32_t)id | ((uint32_t)ver << 16) | ((uint32_t)next << 20));
}

/* NVMe-like endpoint: PM@40 -> MSI-X@50 -> PCIe@70 ; AER@100 -> DSN@148 -> SecPCIe@158 */
static void fake_endpoint(uint8_t *b)
{
    memset(b, 0, PCI_CFG_SPACE_EXP_SIZE);
    w16(b, PCI_VENDOR_ID, 0x144d); w16(b, PCI_DEVICE_ID, 0xa808);
    w16(b, PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE);
    w16(b, PCI_STATUS, PCI_STATUS_CAP_LIST);
    w32(b, PCI_CLASS_REVISION, 0x01080200);                      /* NVMe class 010802, rev 00 */
    w32(b, 0x10, 0xfe000004); w32(b, 0x14, 0x00000000);          /* BAR0/1: 64-bit mem @ 0xfe000000 */
    w8(b, PCI_CAPABILITY_LIST, 0x40);
    w8(b, 0x40, PCI_CAP_ID_PM);   w8(b, 0x41, 0x50);
    w8(b, 0x50, PCI_CAP_ID_MSIX); w8(b, 0x51, 0x70);
    w8(b, 0x70, PCI_CAP_ID_EXP);  w8(b, 0x71, 0x00);
    w16(b, 0x70 + PCI_EXP_FLAGS, 0x0002);                        /* v2, type 0 Endpoint */
    w32(b, 0x70 + PCI_EXP_DEVCAP, 0x10000002);                   /* MPS 512, FLR+ */
    w16(b, 0x70 + PCI_EXP_DEVCTL, 0x2000 | 0x0020);              /* MRRS 512, MPS 256 */
    w32(b, 0x70 + PCI_EXP_LNKCAP, 0x00400c44);                   /* Gen4 x4, ASPM L0s L1 */
    w16(b, 0x70 + PCI_EXP_LNKSTA, 0x1044);                       /* Gen4 x4, SlotClk+ */
    w32(b, 0x70 + PCI_EXP_LNKCAP2, 0x1e);                        /* 2.5/5/8/16 */
    w16(b, 0x70 + PCI_EXP_LNKCTL2, 0x0004);
    wext(b, 0x100, PCI_EXT_CAP_ID_ERR, 2, 0x148);
    w32(b, 0x100 + PCI_ERR_COR_STATUS, 0x00000041);              /* RxErr + BadTLP logged */
    wext(b, 0x148, PCI_EXT_CAP_ID_DSN, 1, 0x158);
    w32(b, 0x14c, 0x89abcdef); w32(b, 0x150, 0x01234567);
    wext(b, 0x158, 0x19, 1, 0);
}

/* Root port (Type 1): bus 00 -> 01..01, PCIe@40 type 4 with slot, DLActive+ */
static void fake_root_port(uint8_t *b)
{
    memset(b, 0, PCI_CFG_SPACE_EXP_SIZE);
    w16(b, PCI_VENDOR_ID, 0x1b36); w16(b, PCI_DEVICE_ID, 0x000c);
    w16(b, PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_IO);
    w16(b, PCI_STATUS, PCI_STATUS_CAP_LIST);
    w32(b, PCI_CLASS_REVISION, 0x06040000);
    w8(b, PCI_HEADER_TYPE, 0x01);
    w8(b, PCI_PRIMARY_BUS, 0x00); w8(b, PCI_SECONDARY_BUS, 0x01); w8(b, PCI_SUBORDINATE_BUS, 0x01);
    w16(b, PCI_MEMORY_BASE, 0x1000); w16(b, PCI_MEMORY_BASE + 2, 0x101f);  /* 0x10000000-0x101fffff */
    w16(b, PCI_PREF_MEMORY_BASE, 0xfff1); w16(b, PCI_PREF_MEMORY_BASE + 2, 0x0001); /* closed, 64-bit */
    w8(b, PCI_IO_BASE, 0xf0); w8(b, PCI_IO_BASE + 1, 0x00);                          /* closed */
    w8(b, PCI_CAPABILITY_LIST, 0x40);
    w8(b, 0x40, PCI_CAP_ID_EXP); w8(b, 0x41, 0x00);
    w16(b, 0x40 + PCI_EXP_FLAGS, 0x0142);                         /* v2, Root Port, slot */
    w32(b, 0x40 + PCI_EXP_LNKCAP, 0x01100c44);                    /* port 1, Gen4 x4, DLLLARC */
    w16(b, 0x40 + PCI_EXP_LNKSTA, 0x2043);                        /* Gen3 x4 DLActive+ (downgraded) */
    w32(b, 0x40 + PCI_EXP_SLTCAP, 0x00080040);                    /* HPC+, slot #1 */
    w16(b, 0x40 + PCI_EXP_SLTSTA, 0x0040);                        /* presence detected */
    wext(b, 0x100, PCI_EXT_CAP_ID_ERR, 2, 0);
}

int selftest(void)
{
    static uint8_t b[PCI_CFG_SPACE_EXP_SIZE];
    cap_t caps[STD_CAP_TTL];
    cfg_t c = { b, sizeof b };

    fake_endpoint(b);
    CHECK(walk_std_caps(c, caps, STD_CAP_TTL) == 3);
    CHECK(caps[0].off == 0x40 && caps[1].off == 0x50 && caps[2].off == 0x70);
    CHECK(find_cap(c, PCI_CAP_ID_EXP) == 0x70);
    CHECK(find_cap(c, PCI_CAP_ID_MSI) == -ENOENT);
    CHECK(find_ext_cap(c, PCI_EXT_CAP_ID_ERR) == 0x100);
    CHECK(find_ext_cap(c, PCI_EXT_CAP_ID_DSN) == 0x148);
    CHECK(find_ext_cap(c, 0x19) == 0x158);
    CHECK(find_ext_cap(c, 0x10) == -ENOENT);                       /* no SR-IOV */
    decode_all(c, "fake 01:00.0 NVMe endpoint");

    /* same device seen through a 256-byte window (legacy CF8/CFC or non-PCIe kernel view) */
    cfg_t c256 = { b, 256 };
    CHECK(find_cap(c256, PCI_CAP_ID_EXP) == 0x70);
    CHECK(find_ext_cap(c256, PCI_EXT_CAP_ID_ERR) == -ENOENT);     /* AER invisible */

    /* non-root sysfs read: 64 bytes, header only */
    cfg_t c64 = { b, 64 };
    CHECK(rd16(c64, PCI_VENDOR_ID) == 0x144d);
    CHECK(find_cap(c64, PCI_CAP_ID_EXP) == -ERANGE);               /* pointer 0x40 is past the 64 B we got */

    /* reserved low bits in pointers must be masked */
    w8(b, PCI_CAPABILITY_LIST, 0x43);
    w8(b, 0x41, 0x52);
    CHECK(find_cap(c, PCI_CAP_ID_MSIX) == 0x50);
    fake_endpoint(b);

    /* looping standard list: 0x40 -> 0x50 -> 0x40 ... */
    w8(b, 0x51, 0x40);
    CHECK(walk_std_caps(c, caps, STD_CAP_TTL) == -ELOOP);
    CHECK(find_cap(c, PCI_CAP_ID_EXP) == -ELOOP);
    fake_endpoint(b);

    /* garbage pointer into the header */
    w8(b, 0x51, 0x10);
    CHECK(walk_std_caps(c, caps, STD_CAP_TTL) == -EINVAL);
    fake_endpoint(b);

    /* looping extended list: 0x158 -> 0x100 */
    wext(b, 0x158, 0x19, 1, 0x100);
    CHECK(find_ext_cap(c, 0x10) == -ELOOP);
    fake_endpoint(b);
    /* ext next pointing below 0x100 */
    wext(b, 0x148, PCI_EXT_CAP_ID_DSN, 1, 0x0c0);
    CHECK(find_ext_cap(c, 0x19) == -EINVAL);
    fake_endpoint(b);

    /* dead function (link down / nothing there) */
    memset(b, 0xff, sizeof b);
    CHECK(walk_std_caps(c, caps, STD_CAP_TTL) == -ENODEV);
    decode_all(c, "fake 02:00.0 dead link (all-ones)");

    fake_root_port(b);
    CHECK(rd8(c, PCI_HEADER_TYPE) == 1 && rd8(c, PCI_SECONDARY_BUS) == 1);
    CHECK(find_cap(c, PCI_CAP_ID_EXP) == 0x40);
    CHECK((rd16(c, 0x40 + PCI_EXP_LNKSTA) & 0x2000) != 0);
    decode_all(c, "fake 00:01.0 root port");

    /* BAR sizing math */
    CHECK(bar_size(0xfffffff0u, 0, false, false) == 16);
    CHECK(bar_size(0xffffc000u, 0, false, false) == 16 * 1024);
    CHECK(bar_size(0xff000004u, 0xffffffffu, true, false) == 16u * 1024 * 1024);
    CHECK(bar_size(0x0000000cu, 0xfffffffeu, true, false) == 8ull << 30);   /* 8 GiB 64-bit pref */
    CHECK(bar_size(0xffffff01u, 0, false, true) == 256);
    CHECK(bar_size(0, 0, false, false) == 0);

    puts(fails ? "SELFTEST FAIL" : "SELFTEST ALL PASS");
    return fails != 0;
}

/* ---------------- main ---------------- */
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return selftest();
    bool dump = argc == 3 && strcmp(argv[1], "-x") == 0;
    if (argc != 2 && !dump) {
        fprintf(stderr, "usage: %s [-x] <DDDD:BB:DD.F | config-file> | --selftest\n", argv[0]);
        return 2;
    }
    const char *arg = argv[dump ? 2 : 1];
    char path[512];
    if (strchr(arg, '/'))
        snprintf(path, sizeof path, "%s", arg);
    else
        snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/config", arg);
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    static uint8_t buf[PCI_CFG_SPACE_EXP_SIZE];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    cfg_t c = { buf, n };
    /* label: the BDF, or for ".../0000:01:00.0/config" the directory name */
    char bdf[512];
    snprintf(bdf, sizeof bdf, "%s", arg);
    if (strchr(arg, '/')) {
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%s", arg);
        char *slash = strrchr(tmp, '/');
        if (slash && strcmp(slash, "/config") == 0) {
            *slash = 0;
            char *d = strrchr(tmp, '/');
            snprintf(bdf, sizeof bdf, "%s", d ? d + 1 : tmp);
        }
    }
    if (dump) {
        const char *s = strncmp(bdf, "0000:", 5) == 0 ? bdf + 5 : bdf;   /* lspci -F wants BB:DD.F */
        hexdump_lspci(c, s);
    } else {
        decode_all(c, bdf);
    }
    return 0;
}
