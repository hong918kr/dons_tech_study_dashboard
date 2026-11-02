/*
 * bm_pcie_bringup.c -- LP16: bare-metal style PCIe Root Complex bring-up skeleton
 *
 * What Linux does for you (pci_host_probe -> pci_scan_root_bus_bridge ->
 * pci_bus_assign_resources -> pci_enable_device -> pci_alloc_irq_vectors),
 * written by hand the way you would on a bare-metal validation target
 * (e.g. Zynq UltraScale+ PS-PCIe as Root Port, or a new SoC on FPGA).
 *
 * Steps:
 *   1. poll the controller's link-up register (vendor specific)
 *   2. wait 100 ms after link-up before the first config request (PCIe rule)
 *   3. depth-first scan of bus 0..N through ECAM, program bridge bus numbers
 *   4. size every BAR (write all-1s / read back / restore), memory decode OFF
 *   5. assign BARs from the host bridge windows (largest first, natural alignment)
 *   6. program the root port's memory / prefetchable windows
 *   7. enable Memory Space + Bus Master
 *   8. program MSI (address/data) and check that a device-side MSI lands
 *
 * The hardware is FAKE (an in-memory ECAM + a fake controller register), so
 * the whole flow runs on a laptop. Swap plat_ops_t for real MMIO on target.
 *
 * Build/run (macOS, see LP_NOTES_SPEC.md):
 *   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
 *      -Wall -Wextra -Werror -fsanitize=address,undefined bm_pcie_bringup.c -o /tmp/bm && /tmp/bm
 *
 * Register offsets/bits below match include/uapi/linux/pci_regs.h.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- standard config space (pci_regs.h) ---------------- */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define  PCI_COMMAND_IO         0x1
#define  PCI_COMMAND_MEMORY     0x2
#define  PCI_COMMAND_MASTER     0x4
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CLASS_REVISION      0x08
#define PCI_HEADER_TYPE         0x0e
#define PCI_BASE_ADDRESS_0      0x10
#define PCI_PRIMARY_BUS         0x18
#define PCI_SECONDARY_BUS       0x19
#define PCI_SUBORDINATE_BUS     0x1a
#define PCI_MEMORY_BASE         0x20
#define PCI_MEMORY_LIMIT        0x22
#define PCI_PREF_MEMORY_BASE    0x24
#define PCI_PREF_MEMORY_LIMIT   0x26
#define PCI_PREF_BASE_UPPER32   0x28
#define PCI_PREF_LIMIT_UPPER32  0x2c
#define PCI_CAPABILITY_LIST     0x34
#define PCI_CAP_ID_MSI          0x05
#define PCI_CAP_ID_EXP          0x10
#define PCI_MSI_FLAGS           0x02
#define  PCI_MSI_FLAGS_ENABLE   0x0001
#define  PCI_MSI_FLAGS_QSIZE    0x0070
#define  PCI_MSI_FLAGS_64BIT    0x0080
#define PCI_MSI_ADDRESS_LO      0x04
#define PCI_MSI_ADDRESS_HI      0x08
#define PCI_MSI_DATA_32         0x08
#define PCI_MSI_DATA_64         0x0c
#define PCI_EXP_LNKCAP          0x0c
#define PCI_EXP_LNKSTA          0x12
#define  PCI_EXP_LNKSTA_CLS     0x000f
#define  PCI_EXP_LNKSTA_NLW     0x03f0
#define  PCI_EXP_LNKSTA_DLLLA   0x2000

/* -------- controller-specific link register (illustrative) --------
 * Modeled on the Xilinx NWL (ZynqMP PS-PCIe) driver: PS_LINKUP_OFFSET 0x238,
 * bit0 = PCIe link up, bit1 = PHY ready (drivers/pci/controller/pcie-xilinx-nwl.c).
 * A DesignWare core would use PORT_LINK_DEBUG1 (0x72C) bit 4 instead.        */
#define CTRL_LINK_STATUS        0x238
#define  CTRL_LINK_UP           0x1
#define LINK_WAIT_MAX_RETRIES   10   /* same as PCIE_LINK_WAIT_MAX_RETRIES */
#define LINK_WAIT_SLEEP_MS      90   /* same as PCIE_LINK_WAIT_SLEEP_MS     */
#define RESET_CONFIG_WAIT_MS    100  /* PCIE_RESET_CONFIG_WAIT_MS            */

/* ---------------- platform abstraction ---------------- */
typedef struct {
    uint32_t (*cfg_read)(uintptr_t addr, unsigned width);            /* width 1/2/4 */
    void     (*cfg_write)(uintptr_t addr, unsigned width, uint32_t v);
    uint32_t (*ctrl_read)(unsigned off);
    void     (*delay_ms)(unsigned ms);
} plat_ops_t;

#ifdef BAREMETAL_TARGET
/* On real silicon: config accesses are plain sized MMIO loads/stores into the
 * ECAM window. Never emulate a 16-bit write with a 32-bit read-modify-write:
 * Command(0x04) and Status(0x06) share a dword, and Status has RW1C bits.     */
#define CTRL_BASE 0xFD0E0000u /* e.g. ZynqMP "pcireg" -- check your DT/TRM */
uint32_t mmio_cfg_read(uintptr_t a, unsigned w)
{
    switch (w) {
    case 1:  return *(volatile uint8_t *)a;
    case 2:  return *(volatile uint16_t *)a;
    default: return *(volatile uint32_t *)a;
    }
}
void mmio_cfg_write(uintptr_t a, unsigned w, uint32_t v)
{
    switch (w) {
    case 1:  *(volatile uint8_t *)a = (uint8_t)v; break;
    case 2:  *(volatile uint16_t *)a = (uint16_t)v; break;
    default: *(volatile uint32_t *)a = v; break;
    }
    __asm__ volatile("dsb sy" ::: "memory"); /* Arm: make the store visible */
}
uint32_t mmio_ctrl_read(unsigned off) { return *(volatile uint32_t *)((uintptr_t)CTRL_BASE + off); }
#endif

/* ================================================================
 *                FAKE HARDWARE (host-test backend)
 * ================================================================ */
#define ECAM_BASE ((uintptr_t)0x10000000u)

typedef struct {
    const char *name;
    bool behind_rp;          /* false: on bus 0 (inside the RC); true: across the link */
    uint8_t dev, fn;
    uint8_t cfg[4096];
    uint8_t wmask[4096];     /* 1-bits are writable; everything else is read-only */
} fake_fn_t;

static struct {
    fake_fn_t fn[4];
    int nfn;
    int link_up_after;       /* number of polls before link up; <0 = never */
    int polls;
    unsigned delayed_ms;
    uint64_t doorbell_addr;  /* MSI target (e.g. GIC ITS GITS_TRANSLATER) */
    uint32_t doorbell_data;
    int doorbell_hits;
} hw;

static void p8(fake_fn_t *f, unsigned o, uint8_t v)   { f->cfg[o] = v; }
static void p16(fake_fn_t *f, unsigned o, uint16_t v) { f->cfg[o] = (uint8_t)v; f->cfg[o + 1] = (uint8_t)(v >> 8); }
static void p32(fake_fn_t *f, unsigned o, uint32_t v) { p16(f, o, (uint16_t)v); p16(f, o + 2, (uint16_t)(v >> 16)); }
static void wm(fake_fn_t *f, unsigned o, unsigned n, uint32_t m)
{
    for (unsigned i = 0; i < n; i++)
        f->wmask[o + i] = (uint8_t)(m >> (8 * i));
}
static uint32_t g(const fake_fn_t *f, unsigned o, unsigned n)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint32_t)f->cfg[o + i] << (8 * i);
    return v;
}

static bool fake_link_is_up(void) { return hw.link_up_after >= 0 && hw.polls >= hw.link_up_after; }

static fake_fn_t *fake_new(const char *name, bool behind, uint8_t dev, uint8_t fn,
                           uint16_t vid, uint16_t did, uint32_t class_rev, uint8_t hdr)
{
    fake_fn_t *f = &hw.fn[hw.nfn++];
    memset(f, 0, sizeof(*f));
    f->name = name; f->behind_rp = behind; f->dev = dev; f->fn = fn;
    p16(f, PCI_VENDOR_ID, vid);
    p16(f, PCI_DEVICE_ID, did);
    p32(f, PCI_CLASS_REVISION, class_rev);
    p8(f, PCI_HEADER_TYPE, hdr);
    wm(f, PCI_COMMAND, 2, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    return f;
}

/* Topology: 00:00.0 Root Port (Gen2 x4 capable, like ZynqMP PS-PCIe)
 *           01:00.0 EP fn0  NVMe-like : BAR0 64-bit non-pref 16 KiB, MSI(64-bit)
 *           01:00.1 EP fn1  accel-like: BAR0 64-bit pref 1 MiB, BAR2 32-bit 4 KiB  */
static void fake_reset(int link_up_after)
{
    memset(&hw, 0, sizeof(hw));
    hw.link_up_after = link_up_after;
    hw.doorbell_addr = 0x08080040ull;

    fake_fn_t *rp = fake_new("root port", false, 0, 0, 0x1234, 0x0100, 0x06040000u, 0x01);
    p16(rp, PCI_STATUS, PCI_STATUS_CAP_LIST);
    p8(rp, PCI_CAPABILITY_LIST, 0x40);
    p8(rp, 0x40, PCI_CAP_ID_EXP); p8(rp, 0x41, 0x00);
    p16(rp, 0x42, 0x0042);                       /* PCIe cap v2, type 4 = Root Port */
    p32(rp, 0x40 + PCI_EXP_LNKCAP, (4u << 4) | 2u | (1u << 20)); /* x4, 5 GT/s, DLLLA reporting */
    p16(rp, 0x40 + PCI_EXP_LNKSTA, 0);           /* link down until the PHY trains */
    wm(rp, PCI_PRIMARY_BUS, 3, 0xFFFFFF);
    wm(rp, PCI_MEMORY_BASE, 4, 0xFFF0FFF0u);
    p16(rp, PCI_PREF_MEMORY_BASE, 0x0001);       /* low nibble 1 = 64-bit pref window */
    p16(rp, PCI_PREF_MEMORY_LIMIT, 0x0001);
    wm(rp, PCI_PREF_MEMORY_BASE, 4, 0xFFF0FFF0u);
    wm(rp, PCI_PREF_BASE_UPPER32, 4, 0xFFFFFFFFu);
    wm(rp, PCI_PREF_LIMIT_UPPER32, 4, 0xFFFFFFFFu);

    fake_fn_t *f0 = fake_new("ep fn0 nvme", true, 0, 0, 0x1234, 0x0200, 0x01080200u, 0x80);
    p32(f0, PCI_BASE_ADDRESS_0, 0x4);            /* 64-bit, non-prefetchable */
    wm(f0, PCI_BASE_ADDRESS_0, 4, 0xFFFFC000u);  /* 16 KiB */
    wm(f0, PCI_BASE_ADDRESS_0 + 4, 4, 0xFFFFFFFFu);
    p16(f0, PCI_STATUS, PCI_STATUS_CAP_LIST);
    p8(f0, PCI_CAPABILITY_LIST, 0x50);
    p8(f0, 0x50, PCI_CAP_ID_MSI); p8(f0, 0x51, 0x70);
    p16(f0, 0x50 + PCI_MSI_FLAGS, PCI_MSI_FLAGS_64BIT);
    wm(f0, 0x50 + PCI_MSI_FLAGS, 2, PCI_MSI_FLAGS_ENABLE | PCI_MSI_FLAGS_QSIZE);
    wm(f0, 0x50 + PCI_MSI_ADDRESS_LO, 4, 0xFFFFFFFCu);
    wm(f0, 0x50 + PCI_MSI_ADDRESS_HI, 4, 0xFFFFFFFFu);
    wm(f0, 0x50 + PCI_MSI_DATA_64, 2, 0xFFFF);
    p8(f0, 0x70, PCI_CAP_ID_EXP); p8(f0, 0x71, 0x00);
    p16(f0, 0x72, 0x0002);                       /* type 0 = Endpoint */
    p32(f0, 0x70 + PCI_EXP_LNKCAP, (4u << 4) | 2u);

    fake_fn_t *f1 = fake_new("ep fn1 accel", true, 0, 1, 0x1234, 0x0201, 0x12000000u, 0x00);
    p32(f1, PCI_BASE_ADDRESS_0, 0xC);            /* 64-bit, prefetchable */
    wm(f1, PCI_BASE_ADDRESS_0, 4, 0xFFF00000u);  /* 1 MiB */
    wm(f1, PCI_BASE_ADDRESS_0 + 4, 4, 0xFFFFFFFFu);
    wm(f1, PCI_BASE_ADDRESS_0 + 8, 4, 0xFFFFF000u); /* BAR2: 32-bit 4 KiB */
}

/* ECAM decode + routing. Behind-the-link functions answer only when the link is
 * up AND the root port's secondary bus number matches (Type 1 -> Type 0). A
 * miss returns all-1s like a real RC that completes the failed request with UR.
 * (Some RCs instead raise an external abort or hang -- LP15 scenario 10.)      */
static fake_fn_t *fake_lookup(uintptr_t addr, unsigned *reg)
{
    uintptr_t off = addr - ECAM_BASE;
    unsigned bus = (unsigned)(off >> 20) & 0xFF, dev = (unsigned)(off >> 15) & 0x1F;
    unsigned fn = (unsigned)(off >> 12) & 0x7;
    *reg = (unsigned)off & 0xFFF;
    uint8_t sec = hw.fn[0].cfg[PCI_SECONDARY_BUS];
    for (int i = 0; i < hw.nfn; i++) {
        fake_fn_t *f = &hw.fn[i];
        if (f->dev != dev || f->fn != fn)
            continue;
        if (!f->behind_rp && bus == 0)
            return f;
        if (f->behind_rp && fake_link_is_up() && sec != 0 && bus == sec)
            return f;
    }
    return NULL;
}
static uint32_t fake_cfg_read(uintptr_t addr, unsigned w)
{
    unsigned reg;
    fake_fn_t *f = fake_lookup(addr, &reg);
    if (!f)
        return w == 4 ? 0xFFFFFFFFu : ((1u << (8 * w)) - 1u);
    return g(f, reg, w);
}
static void fake_cfg_write(uintptr_t addr, unsigned w, uint32_t v)
{
    unsigned reg;
    fake_fn_t *f = fake_lookup(addr, &reg);
    if (!f)
        return;                                  /* write to nowhere: dropped */
    for (unsigned i = 0; i < w; i++) {
        uint8_t m = f->wmask[reg + i], b = (uint8_t)(v >> (8 * i));
        f->cfg[reg + i] = (uint8_t)((f->cfg[reg + i] & ~m) | (b & m));
    }
}
static uint32_t fake_ctrl_read(unsigned off)
{
    if (off != CTRL_LINK_STATUS)
        return 0;
    hw.polls++;
    if (!fake_link_is_up())
        return 0;
    p16(&hw.fn[0], 0x40 + PCI_EXP_LNKSTA, PCI_EXP_LNKSTA_DLLLA | (4u << 4) | 2u); /* Gen2 x4 */
    return CTRL_LINK_UP;
}
static void fake_delay_ms(unsigned ms) { hw.delayed_ms += ms; }

/* The device side of MSI: a posted memory write of `data` to `address`.
 * Only possible when the function is allowed to master the bus.             */
static bool fake_device_fire_msi(int idx)
{
    const fake_fn_t *f = &hw.fn[idx];
    if (!(g(f, PCI_COMMAND, 2) & PCI_COMMAND_MASTER))
        return false;
    uint16_t flags = (uint16_t)g(f, 0x50 + PCI_MSI_FLAGS, 2);
    if (!(flags & PCI_MSI_FLAGS_ENABLE))
        return false;
    uint64_t a = g(f, 0x50 + PCI_MSI_ADDRESS_LO, 4) | ((uint64_t)g(f, 0x50 + PCI_MSI_ADDRESS_HI, 4) << 32);
    if (a != hw.doorbell_addr)
        return false;                            /* wrong address: lost (or SMMU fault) */
    hw.doorbell_data = g(f, 0x50 + PCI_MSI_DATA_64, 2);
    hw.doorbell_hits++;
    return true;
}

static const plat_ops_t fake_ops = { fake_cfg_read, fake_cfg_write, fake_ctrl_read, fake_delay_ms };

/* ================================================================
 *            BARE-METAL BRING-UP CODE (what you would ship)
 * ================================================================ */
typedef struct { uint64_t base, limit, next; } win_t;
typedef struct { uint8_t bus, dev, fn, bar; bool is64, pref; uint64_t size, addr; bool ok; } bar_req_t;
typedef struct { uint8_t bus, dev, fn, hdr; uint16_t vid, did; } fn_rec_t;

typedef struct {
    const plat_ops_t *ops;
    uintptr_t ecam;
    uint8_t last_bus;
    fn_rec_t fns[16]; int nfns;
    bar_req_t bars[32]; int nbars;
    win_t mem, pref;      /* from the host bridge DT "ranges" on a real board */
    int errors;
    bool link_up;
} pcie_ctx_t;

static uintptr_t ecam_addr(uintptr_t base, unsigned bus, unsigned dev, unsigned fn, unsigned off)
{
    return base + (((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) | ((uintptr_t)fn << 12) | off);
}
static uint32_t rd(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f, unsigned off, unsigned w)
{
    return c->ops->cfg_read(ecam_addr(c->ecam, b, d, f, off), w);
}
static void wr(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f, unsigned off, unsigned w, uint32_t v)
{
    c->ops->cfg_write(ecam_addr(c->ecam, b, d, f, off), w, v);
}

/* Walk the capability list with a loop guard (a broken/looping list must not hang us). */
static unsigned find_cap(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f, uint8_t id)
{
    if (!(rd(c, b, d, f, PCI_STATUS, 2) & PCI_STATUS_CAP_LIST))
        return 0;
    unsigned p = rd(c, b, d, f, PCI_CAPABILITY_LIST, 1) & 0xFCu;
    for (int guard = 48; p >= 0x40 && guard > 0; guard--) {
        if (rd(c, b, d, f, p, 1) == id)
            return p;
        p = rd(c, b, d, f, p + 1, 1) & 0xFCu;
    }
    return 0;
}

/* Step 1+2: poll vendor link-up, then wait before touching config across the link. */
static bool wait_link_up(pcie_ctx_t *c)
{
    for (int i = 0; i < LINK_WAIT_MAX_RETRIES; i++) {
        if (c->ops->ctrl_read(CTRL_LINK_STATUS) & CTRL_LINK_UP) {
            c->ops->delay_ms(RESET_CONFIG_WAIT_MS);
            return true;
        }
        c->ops->delay_ms(LINK_WAIT_SLEEP_MS);
    }
    printf("  pcie: PHY link never came up\n");
    return false;
}

/* Step 4: BAR sizing. Memory decode is turned off while all-1s sit in the BARs. */
static void size_bars(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f)
{
    uint16_t cmd = (uint16_t)rd(c, b, d, f, PCI_COMMAND, 2);
    wr(c, b, d, f, PCI_COMMAND, 2, cmd & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
    for (unsigned i = 0; i < 6; i++) {
        unsigned off = PCI_BASE_ADDRESS_0 + 4 * i;
        uint32_t orig = rd(c, b, d, f, off, 4);
        wr(c, b, d, f, off, 4, 0xFFFFFFFFu);
        uint32_t lo = rd(c, b, d, f, off, 4);
        wr(c, b, d, f, off, 4, orig);
        if (lo == 0)
            continue;                                   /* unimplemented BAR */
        if (lo & 1u) {
            printf("  %02x:%02x.%u BAR %u: I/O BAR ignored\n", b, d, f, i);
            continue;
        }
        bool is64 = ((lo >> 1) & 3u) == 2u;
        uint64_t mask = lo & ~0xFull;
        if (is64) {
            uint32_t ohi = rd(c, b, d, f, off + 4, 4);
            wr(c, b, d, f, off + 4, 4, 0xFFFFFFFFu);
            mask |= (uint64_t)rd(c, b, d, f, off + 4, 4) << 32;
            wr(c, b, d, f, off + 4, 4, ohi);
        } else {
            mask |= 0xFFFFFFFF00000000ull;
        }
        bar_req_t *r = &c->bars[c->nbars++];
        *r = (bar_req_t){ (uint8_t)b, (uint8_t)d, (uint8_t)f, (uint8_t)i, is64, (lo & 8u) != 0,
                          ~mask + 1, 0, false };
        if (is64)
            i++;                                        /* upper half consumed */
    }
    wr(c, b, d, f, PCI_COMMAND, 2, cmd);
}

/* Step 3: depth-first scan. Bus numbers must be programmed into a bridge BEFORE
 * anything behind it can be reached. Returns the subordinate bus number.       */
static uint8_t scan_bus(pcie_ctx_t *c, uint8_t bus)
{
    for (unsigned d = 0; d < 32; d++) {
        for (unsigned f = 0; f < 8; f++) {
            uint16_t vid = (uint16_t)rd(c, bus, d, f, PCI_VENDOR_ID, 2);
            if (vid == 0xFFFF) {
                if (f == 0)
                    break;                              /* no device: skip fn 1..7 */
                continue;
            }
            uint8_t hdr = (uint8_t)rd(c, bus, d, f, PCI_HEADER_TYPE, 1);
            fn_rec_t *r = &c->fns[c->nfns++];
            *r = (fn_rec_t){ bus, (uint8_t)d, (uint8_t)f, hdr, vid, (uint16_t)rd(c, bus, d, f, PCI_DEVICE_ID, 2) };
            printf("  pci 0000:%02x:%02x.%u: [%04x:%04x] type %02x class 0x%06x\n", bus, d, f,
                   r->vid, r->did, hdr & 0x7Fu, rd(c, bus, d, f, PCI_CLASS_REVISION, 4) >> 8);
            if ((hdr & 0x7Fu) == 1u) {
                uint8_t sec = ++c->last_bus;
                wr(c, bus, d, f, PCI_PRIMARY_BUS, 1, bus);
                wr(c, bus, d, f, PCI_SECONDARY_BUS, 1, sec);
                wr(c, bus, d, f, PCI_SUBORDINATE_BUS, 1, 0xFF); /* open while scanning */
                uint8_t sub = scan_bus(c, sec);
                wr(c, bus, d, f, PCI_SUBORDINATE_BUS, 1, sub);
            } else {
                size_bars(c, bus, d, f);
            }
            if (f == 0 && !(hdr & 0x80u))
                break;                                  /* not multi-function */
        }
    }
    return c->last_bus;
}

static int by_size_desc(const void *a, const void *b)
{
    uint64_t x = ((const bar_req_t *)a)->size, y = ((const bar_req_t *)b)->size;
    return (x < y) - (x > y);
}

/* Step 5: largest BAR first, each naturally aligned to its own size. */
static void assign_bars(pcie_ctx_t *c)
{
    qsort(c->bars, (size_t)c->nbars, sizeof(c->bars[0]), by_size_desc);
    for (int i = 0; i < c->nbars; i++) {
        bar_req_t *r = &c->bars[i];
        win_t *w = (r->pref && r->is64) ? &c->pref : &c->mem;
        uint64_t a = (w->next + r->size - 1) & ~(r->size - 1);
        unsigned off = PCI_BASE_ADDRESS_0 + 4u * r->bar;
        if (a + r->size - 1 > w->limit) {
            printf("  pci 0000:%02x:%02x.%u: BAR %u [mem size %#llx%s%s]: can't assign; no space\n",
                   r->bus, r->dev, r->fn, r->bar, (unsigned long long)r->size,
                   r->is64 ? " 64bit" : "", r->pref ? " pref" : "");
            c->errors++;
            continue;
        }
        w->next = a + r->size;
        uint32_t keep = rd(c, r->bus, r->dev, r->fn, off, 4) & 0xFu;  /* type bits are RO */
        wr(c, r->bus, r->dev, r->fn, off, 4, (uint32_t)a | keep);
        if (r->is64)
            wr(c, r->bus, r->dev, r->fn, off + 4, 4, (uint32_t)(a >> 32));
        r->addr = a;
        r->ok = true;
        printf("  pci 0000:%02x:%02x.%u: BAR %u [mem %#llx-%#llx%s%s]: assigned\n", r->bus, r->dev, r->fn,
               r->bar, (unsigned long long)a, (unsigned long long)(a + r->size - 1),
               r->is64 ? " 64bit" : "", r->pref ? " pref" : "");
    }
}

/* Step 6: bridge windows (1 MiB granularity). base > limit = window closed. */
static void program_rp_windows(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f)
{
    const uint64_t MiB = 1u << 20;
    if (c->mem.next > c->mem.base) {
        uint64_t lim = ((c->mem.next + MiB - 1) & ~(MiB - 1)) - 1;
        wr(c, b, d, f, PCI_MEMORY_BASE, 2, (uint32_t)(c->mem.base >> 16) & 0xFFF0u);
        wr(c, b, d, f, PCI_MEMORY_LIMIT, 2, (uint32_t)(lim >> 16) & 0xFFF0u);
    } else {
        wr(c, b, d, f, PCI_MEMORY_BASE, 2, 0xFFF0);
        wr(c, b, d, f, PCI_MEMORY_LIMIT, 2, 0x0000);
    }
    if (c->pref.next > c->pref.base) {
        uint64_t lim = ((c->pref.next + MiB - 1) & ~(MiB - 1)) - 1;
        wr(c, b, d, f, PCI_PREF_MEMORY_BASE, 2, (uint32_t)(c->pref.base >> 16) & 0xFFF0u);
        wr(c, b, d, f, PCI_PREF_MEMORY_LIMIT, 2, (uint32_t)(lim >> 16) & 0xFFF0u);
        wr(c, b, d, f, PCI_PREF_BASE_UPPER32, 4, (uint32_t)(c->pref.base >> 32));
        wr(c, b, d, f, PCI_PREF_LIMIT_UPPER32, 4, (uint32_t)(lim >> 32));
    } else {
        wr(c, b, d, f, PCI_PREF_MEMORY_BASE, 2, 0xFFF0);
        wr(c, b, d, f, PCI_PREF_MEMORY_LIMIT, 2, 0x0000);
    }
}

/* Step 7: enable decode + mastering, but never for a function with an unassigned BAR. */
static void enable_all(pcie_ctx_t *c)
{
    for (int i = 0; i < c->nfns; i++) {
        fn_rec_t *r = &c->fns[i];
        bool ok = true;
        for (int j = 0; j < c->nbars; j++)
            if (c->bars[j].bus == r->bus && c->bars[j].dev == r->dev && c->bars[j].fn == r->fn && !c->bars[j].ok)
                ok = false;
        if (!ok) {
            printf("  pci 0000:%02x:%02x.%u: not assigned; can't enable device\n", r->bus, r->dev, r->fn);
            continue;
        }
        uint16_t cmd = (uint16_t)rd(c, r->bus, r->dev, r->fn, PCI_COMMAND, 2);
        wr(c, r->bus, r->dev, r->fn, PCI_COMMAND, 2, cmd | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    }
}

/* Step 8: single-vector MSI. On Arm the address is the GIC ITS doorbell (or an
 * MSI frame in the host controller); data identifies the event (DeviceID/EventID). */
static bool msi_setup(pcie_ctx_t *c, unsigned b, unsigned d, unsigned f, uint64_t addr, uint16_t data)
{
    unsigned p = find_cap(c, b, d, f, PCI_CAP_ID_MSI);
    if (!p)
        return false;
    uint16_t flags = (uint16_t)rd(c, b, d, f, p + PCI_MSI_FLAGS, 2);
    wr(c, b, d, f, p + PCI_MSI_FLAGS, 2, flags & ~PCI_MSI_FLAGS_ENABLE);
    wr(c, b, d, f, p + PCI_MSI_ADDRESS_LO, 4, (uint32_t)addr);
    if (flags & PCI_MSI_FLAGS_64BIT) {
        wr(c, b, d, f, p + PCI_MSI_ADDRESS_HI, 4, (uint32_t)(addr >> 32));
        wr(c, b, d, f, p + PCI_MSI_DATA_64, 2, data);
    } else {
        wr(c, b, d, f, p + PCI_MSI_DATA_32, 2, data);
    }
    wr(c, b, d, f, p + PCI_MSI_FLAGS, 2, (flags & ~PCI_MSI_FLAGS_QSIZE) | PCI_MSI_FLAGS_ENABLE);
    return true;
}

static void report_link(pcie_ctx_t *c)
{
    unsigned e = find_cap(c, 0, 0, 0, PCI_CAP_ID_EXP);
    uint16_t sta = (uint16_t)rd(c, 0, 0, 0, e + PCI_EXP_LNKSTA, 2);
    printf("  root port LnkSta=0x%04x: Gen%u x%u DLActive%c\n", sta, sta & PCI_EXP_LNKSTA_CLS,
           (sta & PCI_EXP_LNKSTA_NLW) >> 4, (sta & PCI_EXP_LNKSTA_DLLLA) ? '+' : '-');
}

static int pcie_bringup(pcie_ctx_t *c)
{
    c->link_up = wait_link_up(c);      /* even if down, the RP itself is enumerable */
    c->last_bus = 0;
    (void)scan_bus(c, 0);
    assign_bars(c);
    program_rp_windows(c, 0, 0, 0);
    enable_all(c);
    report_link(c);
    return c->errors;
}

static void ctx_init(pcie_ctx_t *c, uint64_t pref_size)
{
    memset(c, 0, sizeof(*c));
    c->ops = &fake_ops;
    c->ecam = ECAM_BASE;
    c->mem  = (win_t){ 0xE1000000ull, 0xEFFFFFFFull, 0xE1000000ull };            /* 32-bit non-pref */
    c->pref = (win_t){ 0x600000000ull, 0x600000000ull + pref_size - 1, 0x600000000ull }; /* 64-bit pref */
}

/* ================================================================ tests */
static void test_happy_path(void)
{
    printf("[1] happy path: link comes up on the 3rd poll\n");
    fake_reset(3);
    pcie_ctx_t c;
    ctx_init(&c, 1ull << 30);
    assert(rd(&c, 1, 0, 0, PCI_VENDOR_ID, 2) == 0xFFFF);   /* unreachable before bus numbers */
    assert(pcie_bringup(&c) == 0);
    assert(c.link_up && hw.delayed_ms == 2 * LINK_WAIT_SLEEP_MS + RESET_CONFIG_WAIT_MS);
    assert(c.nfns == 3);
    assert(hw.fn[0].cfg[PCI_SECONDARY_BUS] == 1 && hw.fn[0].cfg[PCI_SUBORDINATE_BUS] == 1);
    assert(g(&hw.fn[2], PCI_BASE_ADDRESS_0, 4) == 0x0000000Cu && g(&hw.fn[2], 0x14, 4) == 0x6u); /* 0x6_0000_0000 pref */
    assert(g(&hw.fn[1], PCI_BASE_ADDRESS_0, 4) == 0xE1000004u);   /* 16 KiB first (bigger) */
    assert(g(&hw.fn[2], PCI_BASE_ADDRESS_0 + 8, 4) == 0xE1004000u); /* then 4 KiB */
    assert(g(&hw.fn[0], PCI_MEMORY_BASE, 2) == 0xE100 && g(&hw.fn[0], PCI_MEMORY_LIMIT, 2) == 0xE100);
    assert((g(&hw.fn[0], PCI_PREF_MEMORY_BASE, 2) & 0xFFF0) == 0x0000 && g(&hw.fn[0], PCI_PREF_BASE_UPPER32, 4) == 6);
    for (int i = 0; i < 3; i++)
        assert((g(&hw.fn[i], PCI_COMMAND, 2) & 0x6) == 0x6);
    assert(msi_setup(&c, 1, 0, 0, hw.doorbell_addr, 0x21));
    assert(fake_device_fire_msi(1) && hw.doorbell_hits == 1 && hw.doorbell_data == 0x21);
    printf("  -> PASS\n");
}

static void test_link_never_up(void)
{
    printf("[2] link never trains (Detect fails): only the root port is visible\n");
    fake_reset(-1);
    pcie_ctx_t c;
    ctx_init(&c, 1ull << 30);
    assert(pcie_bringup(&c) == 0);
    assert(!c.link_up && c.nfns == 1);
    assert(hw.delayed_ms == LINK_WAIT_MAX_RETRIES * LINK_WAIT_SLEEP_MS);
    assert((g(&hw.fn[0], 0x40 + PCI_EXP_LNKSTA, 2) & PCI_EXP_LNKSTA_DLLLA) == 0);
    assert(rd(&c, 1, 0, 0, PCI_VENDOR_ID, 2) == 0xFFFF);
    printf("  -> PASS\n");
}

static void test_window_too_small(void)
{
    printf("[3] prefetchable window 64 KiB < 1 MiB BAR: 'no space'\n");
    fake_reset(0);
    pcie_ctx_t c;
    ctx_init(&c, 64u << 10);
    assert(pcie_bringup(&c) == 1);
    assert((g(&hw.fn[2], PCI_COMMAND, 2) & PCI_COMMAND_MEMORY) == 0);  /* fn1 left disabled */
    assert((g(&hw.fn[1], PCI_COMMAND, 2) & PCI_COMMAND_MEMORY) != 0);  /* fn0 still fine */
    printf("  -> PASS\n");
}

static void test_msi_needs_bus_master(void)
{
    printf("[4] MSI is a memory write: no Bus Master -> no interrupt\n");
    fake_reset(0);
    pcie_ctx_t c;
    ctx_init(&c, 1ull << 30);
    assert(pcie_bringup(&c) == 0);
    assert(msi_setup(&c, 1, 0, 0, hw.doorbell_addr, 0x7));
    uint16_t cmd = (uint16_t)rd(&c, 1, 0, 0, PCI_COMMAND, 2);
    wr(&c, 1, 0, 0, PCI_COMMAND, 2, cmd & ~PCI_COMMAND_MASTER);
    assert(!fake_device_fire_msi(1) && hw.doorbell_hits == 0);
    wr(&c, 1, 0, 0, PCI_COMMAND, 2, cmd);
    assert(fake_device_fire_msi(1) && hw.doorbell_hits == 1);
    printf("  -> PASS\n");
}

static void test_cap_loop_guard(void)
{
    printf("[5] looping capability list terminates\n");
    fake_reset(0);
    pcie_ctx_t c;
    ctx_init(&c, 1ull << 30);
    p8(&hw.fn[0], 0x41, 0x40);                 /* PCIe cap 'next' points to itself */
    assert(find_cap(&c, 0, 0, 0, PCI_CAP_ID_MSI) == 0);
    printf("  -> PASS\n");
}

int main(void)
{
    test_happy_path();
    test_link_never_up();
    test_window_too_small();
    test_msi_needs_bus_master();
    test_cap_loop_guard();
    printf("ALL PASS\n");
    return 0;
}
