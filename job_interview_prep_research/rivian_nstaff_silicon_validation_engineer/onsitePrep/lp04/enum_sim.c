/* enum_sim.c - bare-metal style PCIe enumeration on a FAKE ECAM (LP04).
 *
 * What it shows (the same job U-Boot / UEFI / a bring-up test does before Linux):
 *   1. depth-first bus scan: Vendor ID read -> 0xFFFF means "nobody there"
 *   2. RRS (old name CRS) retry: a slow device answers Vendor ID 0x0001 until ready
 *   3. bridges: program primary / secondary / subordinate (subordinate=0xFF while scanning below)
 *   4. BAR sizing: write all 1s, read back, decode type bits, size = ~(val & mask) + 1, restore
 *   5. resource assignment: each BAR aligned to its size, each bridge window 1 MiB (mem) / 4 KiB (I/O)
 *      aligned, windows programmed bottom-up after the subtree is placed
 *
 * The "hardware" is emulated: config reads are routed like a real Type 1 bridge would route them
 * (bus == secondary -> Type 0 to the device on the link, sec < bus <= sub -> forward further down).
 * A downstream port whose link is down returns all 1s (Unsupported Request -> 0xFFFFFFFF).
 *
 * Build/run (macOS, see LP_NOTES_SPEC.md):
 *   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
 *      -fsanitize=address,undefined enum_sim.c -o /tmp/enum_sim && /tmp/enum_sim
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- standard header offsets (same names as include/uapi/linux/pci_regs.h) ---- */
#define PCI_VENDOR_ID            0x00
#define PCI_DEVICE_ID            0x02
#define PCI_COMMAND              0x04
#define  PCI_COMMAND_IO          0x1
#define  PCI_COMMAND_MEMORY      0x2
#define  PCI_COMMAND_MASTER      0x4
#define PCI_CLASS_REVISION       0x08
#define PCI_HEADER_TYPE          0x0e
#define  PCI_HEADER_TYPE_MFD     0x80
#define PCI_BASE_ADDRESS_0       0x10
#define  PCI_BASE_ADDRESS_SPACE_IO      0x01
#define  PCI_BASE_ADDRESS_MEM_TYPE_64   0x04
#define  PCI_BASE_ADDRESS_MEM_PREFETCH  0x08
#define PCI_PRIMARY_BUS          0x18
#define PCI_SECONDARY_BUS        0x19
#define PCI_SUBORDINATE_BUS      0x1a
#define PCI_IO_BASE              0x1c   /* 8 bit, [7:4] = addr[15:12] */
#define PCI_IO_LIMIT             0x1d
#define PCI_MEMORY_BASE          0x20   /* 16 bit, [15:4] = addr[31:20] */
#define PCI_MEMORY_LIMIT         0x22
#define PCI_PREF_MEMORY_BASE     0x24
#define PCI_PREF_MEMORY_LIMIT    0x26
#define PCI_PREF_BASE_UPPER32    0x28
#define PCI_PREF_LIMIT_UPPER32   0x2c
#define PCI_VENDOR_ID_PCI_SIG    0x0001 /* RRS completion with SW visibility returns this */

/* ---------------- emulated hardware ---------------- */
typedef struct node {
    const char *name;
    uint16_t vendor, device;
    uint32_t class_rev;
    bool is_bridge;          /* Type 1 header */
    bool downstream_port;    /* root port / switch DSP: only device 0 lives on its link */
    bool link_up;            /* for downstream ports */
    int  rrs_reads;          /* Vendor ID reads answered with RRS before the device is ready */
    /* BARs: size 0 = not implemented. For 64-bit BARs, entry i holds the size, i+1 is the upper half. */
    uint64_t bar_size[6];
    uint8_t  bar_flags[6];   /* low type bits returned in the BAR: IO / 64 / PREF */
    uint32_t bar_reg[6];     /* current register value */
    uint16_t command;
    uint8_t  pri, sec, sub;  /* bridges */
    uint8_t  io_base, io_limit;
    uint16_t mem_base, mem_limit, pref_base, pref_limit;
    uint32_t pref_base_hi, pref_limit_hi;
    struct node *child[32];  /* devices on the secondary bus, indexed by device number */
} node_t;

static node_t *root_bus[32];

/* Route a config access like real hardware. Returns the target or NULL (master abort / UR). */
static node_t *route(node_t *const *list, unsigned bus_of_list, unsigned bus, unsigned dev)
{
    if (bus == bus_of_list)
        return list[dev];
    for (unsigned d = 0; d < 32; d++) {
        node_t *n = list[d];
        if (!n || !n->is_bridge || n->sec == 0)
            continue;
        if (bus >= n->sec && bus <= n->sub) {
            if (n->downstream_port && !n->link_up)
                return NULL;                   /* link down: nothing answers below this port */
            if (n->downstream_port && bus == n->sec && dev != 0)
                return NULL;                   /* only device 0 on a point-to-point link */
            return route(n->child, n->sec, bus, dev);
        }
    }
    return NULL;
}

static unsigned cfg_reads;   /* statistics */

static uint32_t cfg_read32(unsigned bus, unsigned dev, unsigned fn, unsigned off)
{
    cfg_reads++;
    node_t *n = fn == 0 ? route(root_bus, 0, bus, dev) : NULL;   /* single-function devices only */
    if (!n)
        return 0xFFFFFFFFu;                    /* "no device" */
    switch (off) {
    case PCI_VENDOR_ID:
        if (n->rrs_reads > 0) {                /* not ready yet: Request Retry Status */
            n->rrs_reads--;
            return 0xFFFF0000u | PCI_VENDOR_ID_PCI_SIG;
        }
        return (uint32_t)n->device << 16 | n->vendor;
    case PCI_COMMAND:   return n->command;
    case PCI_CLASS_REVISION: return n->class_rev;
    case 0x0c:          return (uint32_t)(n->is_bridge ? 1 : 0) << 16;   /* header type byte at 0x0e */
    case PCI_PRIMARY_BUS:
        return n->is_bridge ? ((uint32_t)n->sub << 16 | (uint32_t)n->sec << 8 | n->pri) : n->bar_reg[2];
    case PCI_IO_BASE:
        return n->is_bridge ? ((uint32_t)n->io_limit << 8 | n->io_base) : n->bar_reg[3];
    case PCI_MEMORY_BASE:
        return n->is_bridge ? ((uint32_t)n->mem_limit << 16 | n->mem_base) : n->bar_reg[4];
    case PCI_PREF_MEMORY_BASE:
        return n->is_bridge ? ((uint32_t)n->pref_limit << 16 | n->pref_base) : n->bar_reg[5];
    case PCI_PREF_BASE_UPPER32:  return n->is_bridge ? n->pref_base_hi : 0;
    case PCI_PREF_LIMIT_UPPER32: return n->is_bridge ? n->pref_limit_hi : 0;
    default:
        if (off >= 0x10 && off < 0x18)             /* BAR0/1 exist for both header types */
            return n->bar_reg[(off - 0x10) / 4];
        return 0;
    }
}

static uint16_t cfg_read16(unsigned b, unsigned d, unsigned f, unsigned off)
{
    return (uint16_t)(cfg_read32(b, d, f, off & ~3u) >> ((off & 2) * 8));
}

/* BAR register write semantics: address bits below the size are hard-wired to 0, type bits read-only. */
static void bar_write(node_t *n, unsigned i, uint32_t v)
{
    if (i > 0 && (n->bar_flags[i - 1] & PCI_BASE_ADDRESS_MEM_TYPE_64) && n->bar_size[i - 1]) {
        uint64_t mask = ~(n->bar_size[i - 1] - 1);           /* upper half of a 64-bit BAR */
        n->bar_reg[i] = v & (uint32_t)(mask >> 32);
        return;
    }
    if (!n->bar_size[i]) { n->bar_reg[i] = 0; return; }       /* unimplemented BAR reads 0 */
    uint32_t mask = (uint32_t)~(n->bar_size[i] - 1);
    unsigned type_bits = (n->bar_flags[i] & PCI_BASE_ADDRESS_SPACE_IO) ? 0x3u : 0xFu;
    n->bar_reg[i] = (v & mask & ~type_bits) | n->bar_flags[i];
}

static void cfg_write32(unsigned bus, unsigned dev, unsigned fn, unsigned off, uint32_t v)
{
    node_t *n = fn == 0 ? route(root_bus, 0, bus, dev) : NULL;
    if (!n)
        return;                                /* write to nowhere is silently dropped */
    if (off == PCI_COMMAND) { n->command = (uint16_t)v; return; }
    if (off >= 0x10 && off < (n->is_bridge ? 0x18u : 0x28u)) { bar_write(n, (off - 0x10) / 4, v); return; }
    if (!n->is_bridge)
        return;
    switch (off) {
    case PCI_PRIMARY_BUS: n->pri = v & 0xFF; n->sec = (v >> 8) & 0xFF; n->sub = (v >> 16) & 0xFF; break;
    case PCI_IO_BASE:     n->io_base = v & 0xF0; n->io_limit = (v >> 8) & 0xF0; break;
    case PCI_MEMORY_BASE: n->mem_base = v & 0xFFF0; n->mem_limit = (v >> 16) & 0xFFF0; break;
    case PCI_PREF_MEMORY_BASE: n->pref_base = (v & 0xFFF0) | 1; n->pref_limit = ((v >> 16) & 0xFFF0) | 1; break;
    case PCI_PREF_BASE_UPPER32:  n->pref_base_hi = v; break;
    case PCI_PREF_LIMIT_UPPER32: n->pref_limit_hi = v; break;
    default: break;
    }
}

/* ---------------- the "firmware" (what you would write bare-metal) ---------------- */
typedef struct {
    unsigned bus, dev;
    uint16_t vendor, device;
    bool bridge;
    unsigned depth;
    unsigned sec, sub;
    int parent;                 /* index into found[] or -1 */
    uint64_t bar_addr[6], bar_sz[6];
    uint8_t bar_kind[6];        /* 0 none, 1 mem32, 2 mem64, 3 io; |0x80 prefetchable */
    uint64_t win_mem_base, win_mem_lim, win_pref_base, win_pref_lim, win_io_base, win_io_lim;
    bool win_mem_used, win_pref_used, win_io_used;
} found_t;

static found_t found[32];
static unsigned nfound;
static unsigned max_bus;

#define RRS_TIMEOUT_POLLS 16

/* Vendor ID read with RRS handling. Returns 0xFFFF for "no device". */
static uint16_t read_vendor(unsigned bus, unsigned dev)
{
    for (int tries = 0; tries < RRS_TIMEOUT_POLLS; tries++) {
        uint16_t v = cfg_read16(bus, dev, 0, PCI_VENDOR_ID);
        if (v != PCI_VENDOR_ID_PCI_SIG)
            return v;
        printf("    %02x:%02x.0 answered RRS (Vendor ID 0x0001) - device not ready, retry %d\n", bus, dev, tries + 1);
        /* real code: delay (e.g. 1, 2, 4 ... ms like pci_bus_wait_rrs()) */
    }
    printf("    %02x:%02x.0 not ready after %d polls, giving up\n", bus, dev, RRS_TIMEOUT_POLLS);
    return 0xFFFF;
}

static void size_bars(found_t *f)
{
    unsigned nbars = f->bridge ? 2 : 6;
    uint16_t cmd = cfg_read16(f->bus, f->dev, 0, PCI_COMMAND);
    cfg_write32(f->bus, f->dev, 0, PCI_COMMAND, cmd & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY)); /* decode off while sizing */
    for (unsigned i = 0; i < nbars; i++) {
        unsigned off = PCI_BASE_ADDRESS_0 + 4 * i;
        uint32_t orig = cfg_read32(f->bus, f->dev, 0, off);
        cfg_write32(f->bus, f->dev, 0, off, 0xFFFFFFFFu);
        uint32_t rb = cfg_read32(f->bus, f->dev, 0, off);
        cfg_write32(f->bus, f->dev, 0, off, orig);
        if (rb == 0)
            continue;                               /* BAR not implemented */
        if (rb & PCI_BASE_ADDRESS_SPACE_IO) {
            f->bar_kind[i] = 3;
            f->bar_sz[i] = (uint32_t)(~(rb & ~0x3u) + 1) & 0xFFFF;   /* I/O BARs decode 16 bits here */
        } else if (rb & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            uint32_t orig_hi = cfg_read32(f->bus, f->dev, 0, off + 4);
            cfg_write32(f->bus, f->dev, 0, off + 4, 0xFFFFFFFFu);
            uint32_t rb_hi = cfg_read32(f->bus, f->dev, 0, off + 4);
            cfg_write32(f->bus, f->dev, 0, off + 4, orig_hi);
            uint64_t v = (uint64_t)rb_hi << 32 | (rb & ~0xFu);
            f->bar_kind[i] = 2 | ((rb & PCI_BASE_ADDRESS_MEM_PREFETCH) ? 0x80 : 0);
            f->bar_sz[i] = ~v + 1;
            printf("    %02x:%02x.0 BAR%u: wrote ffffffff:ffffffff, read %08x:%08x -> 64-bit%s size 0x%llx\n",
                   f->bus, f->dev, i, rb_hi, rb, (rb & 8) ? " pref" : "", (unsigned long long)f->bar_sz[i]);
            i++;                                    /* upper half consumed */
            continue;
        } else {
            f->bar_kind[i] = 1 | ((rb & PCI_BASE_ADDRESS_MEM_PREFETCH) ? 0x80 : 0);
            f->bar_sz[i] = (uint32_t)(~(rb & ~0xFu) + 1);
        }
        printf("    %02x:%02x.0 BAR%u: wrote ffffffff, read %08x -> %s size 0x%llx\n", f->bus, f->dev, i, rb,
               f->bar_kind[i] == 3 ? "I/O" : "mem32", (unsigned long long)f->bar_sz[i]);
    }
    cfg_write32(f->bus, f->dev, 0, PCI_COMMAND, cmd);
}

/* Depth-first scan. Returns the highest bus number found below `bus`. */
static unsigned scan_bus(unsigned bus, unsigned depth, int parent, bool below_downstream_port)
{
    unsigned last_dev = below_downstream_port ? 1 : 32;   /* PCIe link: only device 0 (Linux only_one_child()) */
    for (unsigned dev = 0; dev < last_dev; dev++) {
        uint16_t vid = read_vendor(bus, dev);
        if (vid == 0xFFFF)
            continue;                                      /* nothing at this BDF */
        found_t *f = &found[nfound];
        int me = (int)nfound++;
        memset(f, 0, sizeof *f);
        f->bus = bus; f->dev = dev; f->depth = depth; f->parent = parent;
        f->vendor = vid;
        f->device = cfg_read16(bus, dev, 0, PCI_DEVICE_ID);
        f->bridge = (cfg_read32(bus, dev, 0, 0x0c) >> 16 & 0x7F) == 1;
        size_bars(f);
        if (f->bridge) {
            /* Tell the bridge: everything from `next` up to 0xFF is behind you (for now). */
            unsigned next = ++max_bus;
            cfg_write32(bus, dev, 0, PCI_PRIMARY_BUS, 0xFFu << 16 | next << 8 | bus);
            node_t *hw = route(root_bus, 0, bus, dev);
            bool dsp = hw && hw->downstream_port;
            unsigned sub = scan_bus(next, depth + 1, me, dsp);
            /* Now we know the real end: shrink subordinate. */
            cfg_write32(bus, dev, 0, PCI_PRIMARY_BUS, sub << 16 | next << 8 | bus);
            found[me].sec = next;
            found[me].sub = sub;
        }
    }
    return max_bus;
}

/* ---- resource assignment: one depth-first pass (simple firmware style) ---- */
typedef struct { uint64_t mem, pref, io; } cursor_t;

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

static void assign(int idx, cursor_t *c)
{
    found_t *f = &found[idx];
    /* 1) this function's own BARs: each aligned to its own size */
    for (unsigned i = 0; i < 6; i++) {
        if (!f->bar_kind[i])
            continue;
        uint64_t sz = f->bar_sz[i];
        uint64_t *cur = (f->bar_kind[i] == 3) ? &c->io : (f->bar_kind[i] & 0x80) ? &c->pref : &c->mem;
        uint64_t a = align_up(*cur, sz);
        f->bar_addr[i] = a;
        *cur = a + sz;
        unsigned off = PCI_BASE_ADDRESS_0 + 4 * i;
        uint32_t flags = (f->bar_kind[i] == 3) ? 1u : ((f->bar_kind[i] & 0x7F) == 2 ? 4u : 0u) | ((f->bar_kind[i] & 0x80) ? 8u : 0u);
        cfg_write32(f->bus, f->dev, 0, off, (uint32_t)a | flags);
        if ((f->bar_kind[i] & 0x7F) == 2)
            cfg_write32(f->bus, f->dev, 0, off + 4, (uint32_t)(a >> 32));
    }
    if (!f->bridge)
        return;
    /* 2) bridge: open windows at 1 MiB (mem) / 4 KiB (I/O) alignment, place the subtree, close windows */
    cursor_t start = { align_up(c->mem, 1u << 20), align_up(c->pref, 1u << 20), align_up(c->io, 1u << 12) };
    *c = start;
    for (unsigned k = 0; k < nfound; k++)
        if (found[k].parent == idx)
            assign((int)k, c);
    cursor_t end = { align_up(c->mem, 1u << 20), align_up(c->pref, 1u << 20), align_up(c->io, 1u << 12) };
    f->win_mem_used = end.mem > start.mem;
    f->win_pref_used = end.pref > start.pref;
    f->win_io_used = end.io > start.io;
    f->win_mem_base = start.mem;   f->win_mem_lim = end.mem - 1;
    f->win_pref_base = start.pref; f->win_pref_lim = end.pref - 1;
    f->win_io_base = start.io;     f->win_io_lim = end.io - 1;
    /* Unused window: program base > limit, which means "closed". */
    uint32_t mb = f->win_mem_used ? (uint32_t)(start.mem >> 16) & 0xFFF0 : 0xFFF0;
    uint32_t ml = f->win_mem_used ? (uint32_t)(f->win_mem_lim >> 16) & 0xFFF0 : 0x0000;
    cfg_write32(f->bus, f->dev, 0, PCI_MEMORY_BASE, ml << 16 | mb);
    uint32_t pb = f->win_pref_used ? (uint32_t)(start.pref >> 16) & 0xFFF0 : 0xFFF0;
    uint32_t pl = f->win_pref_used ? (uint32_t)(f->win_pref_lim >> 16) & 0xFFF0 : 0x0000;
    cfg_write32(f->bus, f->dev, 0, PCI_PREF_MEMORY_BASE, pl << 16 | pb);
    cfg_write32(f->bus, f->dev, 0, PCI_PREF_BASE_UPPER32, f->win_pref_used ? (uint32_t)(start.pref >> 32) : 0xFFFFFFFFu);
    cfg_write32(f->bus, f->dev, 0, PCI_PREF_LIMIT_UPPER32, f->win_pref_used ? (uint32_t)(f->win_pref_lim >> 32) : 0);
    uint32_t ib = f->win_io_used ? (uint32_t)(start.io >> 8) & 0xF0 : 0xF0;
    uint32_t il = f->win_io_used ? (uint32_t)(f->win_io_lim >> 8) & 0xF0 : 0x00;
    cfg_write32(f->bus, f->dev, 0, PCI_IO_BASE, il << 8 | ib);
    /* enable forwarding + bus mastering on the bridge */
    cfg_write32(f->bus, f->dev, 0, PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
}

/* ---------------- topology: RC -> RP1 -> switch (USP + 3 DSPs) -> 2 EPs ; RP2 empty ---------------- */
static node_t hb   = { .name = "Host bridge",       .vendor = 0x1b36, .device = 0x0008, .class_rev = 0x06000000 };
static node_t rp1  = { .name = "Root Port 1",       .vendor = 0x1b36, .device = 0x000c, .class_rev = 0x06040000,
                       .is_bridge = true, .downstream_port = true, .link_up = true };
static node_t rp2  = { .name = "Root Port 2 (empty slot, link down)", .vendor = 0x1b36, .device = 0x000c,
                       .class_rev = 0x06040000, .is_bridge = true, .downstream_port = true, .link_up = false };
static node_t usp  = { .name = "Switch USP",        .vendor = 0x104c, .device = 0x8232, .class_rev = 0x06040000, .is_bridge = true };
static node_t dsp0 = { .name = "Switch DSP0",       .vendor = 0x104c, .device = 0x8233, .class_rev = 0x06040000,
                       .is_bridge = true, .downstream_port = true, .link_up = true };
static node_t dsp1 = { .name = "Switch DSP1",       .vendor = 0x104c, .device = 0x8233, .class_rev = 0x06040000,
                       .is_bridge = true, .downstream_port = true, .link_up = true };
static node_t dsp2 = { .name = "Switch DSP2 (link down)", .vendor = 0x104c, .device = 0x8233, .class_rev = 0x06040000,
                       .is_bridge = true, .downstream_port = true, .link_up = false };
/* EP A: accelerator-like. BAR0 = 1 MiB 32-bit regs, BAR2/3 = 256 MiB 64-bit prefetchable memory */
static node_t epa  = { .name = "EP A (accel)",      .vendor = 0x1d0f, .device = 0xa000, .class_rev = 0x12000001,
                       .bar_size = { 1u << 20, 0, 256u << 20, 0, 0, 0 },
                       .bar_flags = { 0, 0, PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH, 0, 0, 0 } };
/* EP B: NIC-like, slow firmware -> answers RRS 3 times. BAR0 = 128 KiB mem32, BAR1 = 32 B I/O,
 * BAR2/3 = 16 KiB 64-bit non-prefetchable (like the QEMU nvme BAR0 we sized by hand in LP04 §3) */
static node_t epb  = { .name = "EP B (nic, slow FW)", .vendor = 0x8086, .device = 0x10d3, .class_rev = 0x02000000,
                       .rrs_reads = 3,
                       .bar_size = { 128u << 10, 32, 16u << 10, 0, 0, 0 },
                       .bar_flags = { 0, PCI_BASE_ADDRESS_SPACE_IO, PCI_BASE_ADDRESS_MEM_TYPE_64, 0, 0, 0 } };

static void build_topology(void)
{
    root_bus[0] = &hb;
    root_bus[1] = &rp1;
    root_bus[2] = &rp2;
    rp1.child[0] = &usp;
    usp.child[0] = &dsp0;
    usp.child[1] = &dsp1;
    usp.child[2] = &dsp2;
    dsp0.child[0] = &epa;
    dsp1.child[0] = &epb;
    /* reset values of BAR registers: type bits only */
    node_t *all[] = { &hb, &rp1, &rp2, &usp, &dsp0, &dsp1, &dsp2, &epa, &epb };
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++)
        for (unsigned i = 0; i < 6; i++)
            all[k]->bar_reg[i] = all[k]->bar_size[i] ? all[k]->bar_flags[i] : 0;
}

static const char *name_of(unsigned bus, unsigned dev)
{
    node_t *n = route(root_bus, 0, bus, dev);
    return n ? n->name : "?";
}

int main(void)
{
    build_topology();

    /* Sanity: before enumeration only bus 0 is reachable. */
    CHECK(cfg_read16(0, 1, 0, PCI_VENDOR_ID) == 0x1b36);
    CHECK(cfg_read16(1, 0, 0, PCI_VENDOR_ID) == 0xFFFF);      /* bridge not programmed yet */

    printf("== 1. depth-first scan + BAR sizing\n");
    max_bus = 0;
    unsigned last = scan_bus(0, 0, -1, false);

    printf("\n== 2. resource assignment (mem32 from 0x10000000, pref64 from 0x8000000000, io from 0x1000)\n");
    cursor_t c = { 0x10000000u, 0x8000000000ull, 0x1000u };
    for (unsigned k = 0; k < nfound; k++)
        if (found[k].parent == -1)
            assign((int)k, &c);

    printf("\n== 3. result (like lspci -tv + Region / behind-bridge lines)\n");
    for (unsigned k = 0; k < nfound; k++) {
        found_t *f = &found[k];
        printf("%*s%02x:%02x.0 [%04x:%04x] %s", (int)f->depth * 4, "", f->bus, f->dev, f->vendor, f->device,
               name_of(f->bus, f->dev));
        if (f->bridge)
            printf("  bus: primary=%02x secondary=%02x subordinate=%02x", f->bus, f->sec, f->sub);
        printf("\n");
        for (unsigned i = 0; i < 6; i++)
            if (f->bar_kind[i])
                printf("%*s  Region %u: %s at %llx [size=0x%llx]%s\n", (int)f->depth * 4, "", i,
                       f->bar_kind[i] == 3 ? "I/O ports" : "Memory", (unsigned long long)f->bar_addr[i],
                       (unsigned long long)f->bar_sz[i],
                       f->bar_kind[i] == 3 ? "" : (f->bar_kind[i] & 0x7F) == 2 ? ((f->bar_kind[i] & 0x80) ? " 64-bit pref" : " 64-bit")
                                                                              : " 32-bit");
        if (f->bridge) {
            if (f->win_io_used)
                printf("%*s  I/O behind bridge: %llx-%llx\n", (int)f->depth * 4, "",
                       (unsigned long long)f->win_io_base, (unsigned long long)f->win_io_lim);
            if (f->win_mem_used)
                printf("%*s  Memory behind bridge: %llx-%llx\n", (int)f->depth * 4, "",
                       (unsigned long long)f->win_mem_base, (unsigned long long)f->win_mem_lim);
            if (f->win_pref_used)
                printf("%*s  Prefetchable memory behind bridge: %llx-%llx\n", (int)f->depth * 4, "",
                       (unsigned long long)f->win_pref_base, (unsigned long long)f->win_pref_lim);
            if (!f->win_io_used && !f->win_mem_used && !f->win_pref_used)
                printf("%*s  (no windows: nothing behind this bridge)\n", (int)f->depth * 4, "");
        }
    }
    printf("\nconfig reads issued: %u, highest bus: %u\n", cfg_reads, last);

    /* ---------------- self-checks ---------------- */
    CHECK(nfound == 9);                 /* hb, rp1, usp, dsp0, epa, dsp1, epb, dsp2, rp2 */
    CHECK(last == 6);                   /* buses 0..6: rp1->1, usp->2, dsp0->3, dsp1->4, dsp2->5, rp2->6 */
    CHECK(rp1.sec == 1 && rp1.sub == 5);
    CHECK(usp.pri == 1 && usp.sec == 2 && usp.sub == 5);
    CHECK(dsp0.sec == 3 && dsp0.sub == 3);
    CHECK(dsp1.sec == 4 && dsp1.sub == 4);
    CHECK(dsp2.sec == 5 && dsp2.sub == 5);   /* bus number given even though link is down */
    CHECK(rp2.sec == 6 && rp2.sub == 6);
    CHECK(cfg_read16(3, 0, 0, PCI_VENDOR_ID) == 0x1d0f);      /* now reachable through 2 bridges */
    CHECK(cfg_read16(5, 0, 0, PCI_VENDOR_ID) == 0xFFFF);      /* link down -> all 1s */
    CHECK(cfg_read16(3, 1, 0, PCI_VENDOR_ID) == 0xFFFF);      /* point-to-point link: only dev 0 */
    CHECK(epb.rrs_reads == 0);                                /* RRS retried until ready */
    /* BAR sizes decoded from readback */
    found_t *fa = NULL, *fb = NULL;
    for (unsigned k = 0; k < nfound; k++) {
        if (found[k].vendor == 0x1d0f) fa = &found[k];
        if (found[k].vendor == 0x8086) fb = &found[k];
    }
    CHECK(fa && fb);
    if (fa && fb) {
        CHECK(fa->bar_sz[0] == (1u << 20) && fa->bar_kind[0] == 1);
        CHECK(fa->bar_sz[2] == (256u << 20) && fa->bar_kind[2] == (2 | 0x80));
        CHECK(fb->bar_sz[0] == (128u << 10) && fb->bar_sz[1] == 32 && fb->bar_kind[1] == 3);
        CHECK(fb->bar_sz[2] == (16u << 10) && fb->bar_kind[2] == 2);
        /* natural alignment of every BAR */
        for (unsigned i = 0; i < 6; i++) {
            if (fa->bar_kind[i]) CHECK(fa->bar_addr[i] % fa->bar_sz[i] == 0);
            if (fb->bar_kind[i]) CHECK(fb->bar_addr[i] % fb->bar_sz[i] == 0);
        }
        CHECK(fa->bar_addr[2] >= 0x8000000000ull);              /* pref 64-bit BAR placed in the high window */
        /* registers really hold the programmed addresses (64-bit BAR split across two dwords) */
        CHECK((epa.bar_reg[2] & ~0xFu) == (uint32_t)fa->bar_addr[2] && epa.bar_reg[3] == (uint32_t)(fa->bar_addr[2] >> 32));
    }
    /* every bridge window contains its children's windows (checked via the register encoding) */
    CHECK(((uint64_t)rp1.mem_base << 16) <= ((uint64_t)usp.mem_base << 16));
    CHECK(((uint64_t)rp1.mem_limit << 16 | 0xFFFFF) >= ((uint64_t)dsp1.mem_limit << 16 | 0xFFFFF));
    CHECK(((uint64_t)rp1.pref_base_hi << 32 | ((uint64_t)(rp1.pref_base & 0xFFF0) << 16)) == 0x8000000000ull);
    CHECK(rp2.mem_base > rp2.mem_limit);                       /* empty slot: window closed */

    printf("%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
