// msi_cap_decode.c - walk a PCI config-space buffer, find MSI (ID 0x05) and
// MSI-X (ID 0x11) capabilities, decode Message Control / address / data /
// table+PBA location, then decode MSI-X table entries (16 B each).
//
// Bit/offset names follow include/uapi/linux/pci_regs.h (PCI_MSI_*, PCI_MSIX_*).
// Input bytes:
//   cfg_e1000e : REAL bytes, QEMU e1000e (8086:10d3) captured 2026-10-31 in the
//                Lima VM lab (onsitePrep/lp07/qemu_lab/guest_output_2026-10-31.txt)
//   cfg_vzblk  : REAL bytes 0x80..0x8f, Apple VZ virtio-blk 00:06.0 in Lima VM
//   tbl_vzblk  : REAL MSI-X table entries read via resource2 mmap (same device)
//   cfg_synth  : SYNTHETIC enabled 64-bit MSI with 8 of 32 vectors + masking
//
// macOS build (spec flags):
//   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
//      -Wall -Wextra -Werror -fsanitize=address,undefined msi_cap_decode.c -o /tmp/msi && /tmp/msi
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- from pci_regs.h --- */
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CAPABILITY_LIST     0x34
#define PCI_CAP_ID_PM           0x01
#define PCI_CAP_ID_MSI          0x05
#define PCI_CAP_ID_EXP          0x10
#define PCI_CAP_ID_MSIX         0x11
#define PCI_MSI_FLAGS           0x02
#define  PCI_MSI_FLAGS_ENABLE   0x0001
#define  PCI_MSI_FLAGS_QMASK    0x000e  /* Multiple Message Capable (log2) */
#define  PCI_MSI_FLAGS_QSIZE    0x0070  /* Multiple Message Enable  (log2) */
#define  PCI_MSI_FLAGS_64BIT    0x0080
#define  PCI_MSI_FLAGS_MASKBIT  0x0100
#define PCI_MSI_ADDRESS_LO      0x04
#define PCI_MSI_ADDRESS_HI      0x08
#define PCI_MSI_DATA_32         0x08
#define PCI_MSI_MASK_32         0x0c
#define PCI_MSI_DATA_64         0x0c
#define PCI_MSI_MASK_64         0x10
#define PCI_MSI_PENDING_64      0x14
#define PCI_MSIX_FLAGS          2
#define  PCI_MSIX_FLAGS_QSIZE   0x07FF  /* Table Size - 1 */
#define  PCI_MSIX_FLAGS_MASKALL 0x4000
#define  PCI_MSIX_FLAGS_ENABLE  0x8000
#define PCI_MSIX_TABLE          4
#define PCI_MSIX_PBA            8
#define  PCI_MSIX_BIR           0x00000007
#define  PCI_MSIX_OFFSET        0xfffffff8
#define PCI_MSIX_ENTRY_SIZE         16
#define PCI_MSIX_ENTRY_LOWER_ADDR   0x0
#define PCI_MSIX_ENTRY_UPPER_ADDR   0x4
#define PCI_MSIX_ENTRY_DATA         0x8
#define PCI_MSIX_ENTRY_VECTOR_CTRL  0xc
#define  PCI_MSIX_ENTRY_CTRL_MASKBIT 0x1

static uint16_t rd16(const uint8_t *c, unsigned o) { return (uint16_t)(c[o] | c[o + 1] << 8); }
static uint32_t rd32(const uint8_t *c, unsigned o) { return (uint32_t)rd16(c, o) | (uint32_t)rd16(c, o + 2) << 16; }

struct msi_info  { int found, enabled, is64, maskable; unsigned mmc, mme; uint64_t addr; uint16_t data; uint32_t mask; };
struct msix_info { int found, enabled, maskall; unsigned tsize, tbir, pbir; uint32_t toff, poff; };

static unsigned find_cap(const uint8_t *cfg, uint8_t id)
{
    if (!(rd16(cfg, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return 0;
    unsigned pos = cfg[PCI_CAPABILITY_LIST] & ~3u;
    for (int ttl = 48; pos >= 0x40 && ttl--; pos = cfg[pos + 1] & ~3u)   /* ttl guards loops */
        if (cfg[pos] == id) return pos;
    return 0;
}

static struct msi_info decode_msi(const uint8_t *cfg)
{
    struct msi_info m = {0};
    unsigned p = find_cap(cfg, PCI_CAP_ID_MSI);
    if (!p) return m;
    uint16_t mc = rd16(cfg, p + PCI_MSI_FLAGS);
    m.found = 1;
    m.enabled  = !!(mc & PCI_MSI_FLAGS_ENABLE);
    m.mmc      = 1u << ((mc & PCI_MSI_FLAGS_QMASK) >> 1);   /* vectors the device CAN use */
    m.mme      = 1u << ((mc & PCI_MSI_FLAGS_QSIZE) >> 4);   /* vectors SW ALLOWED */
    m.is64     = !!(mc & PCI_MSI_FLAGS_64BIT);
    m.maskable = !!(mc & PCI_MSI_FLAGS_MASKBIT);
    m.addr = rd32(cfg, p + PCI_MSI_ADDRESS_LO);
    if (m.is64) m.addr |= (uint64_t)rd32(cfg, p + PCI_MSI_ADDRESS_HI) << 32;
    m.data = rd16(cfg, p + (m.is64 ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32));
    if (m.maskable) m.mask = rd32(cfg, p + (m.is64 ? PCI_MSI_MASK_64 : PCI_MSI_MASK_32));
    printf("  MSI   @0x%02x: MsgCtl=0x%04x Enable%c Count=%u/%u %s%s\n", p, mc,
           m.enabled ? '+' : '-', m.mme, m.mmc, m.is64 ? "64bit+" : "64bit-",
           m.maskable ? " Maskable+" : " Maskable-");
    printf("               Address=0x%016llx Data=0x%04x", (unsigned long long)m.addr, m.data);
    if (m.maskable) printf(" MaskBits=0x%08x", m.mask);
    printf("\n");
    if (m.enabled && m.mme > 1)   /* device ORs the vector number into the low log2(MME) data bits */
        for (unsigned v = 0; v < m.mme; v++)
            printf("               vector %u -> MWr 0x%llx data 0x%04x%s\n", v,
                   (unsigned long long)m.addr, (unsigned)(m.data | v),
                   (m.maskable && (m.mask >> v & 1)) ? "  (masked)" : "");
    return m;
}

static struct msix_info decode_msix(const uint8_t *cfg)
{
    struct msix_info x = {0};
    unsigned p = find_cap(cfg, PCI_CAP_ID_MSIX);
    if (!p) return x;
    uint16_t mc = rd16(cfg, p + PCI_MSIX_FLAGS);
    uint32_t t = rd32(cfg, p + PCI_MSIX_TABLE), b = rd32(cfg, p + PCI_MSIX_PBA);
    x.found = 1;
    x.enabled = !!(mc & PCI_MSIX_FLAGS_ENABLE);
    x.maskall = !!(mc & PCI_MSIX_FLAGS_MASKALL);
    x.tsize = (mc & PCI_MSIX_FLAGS_QSIZE) + 1;              /* field is N-1 */
    x.tbir = t & PCI_MSIX_BIR; x.toff = t & PCI_MSIX_OFFSET;
    x.pbir = b & PCI_MSIX_BIR; x.poff = b & PCI_MSIX_OFFSET;
    printf("  MSI-X @0x%02x: MsgCtl=0x%04x Enable%c Count=%u Masked%c\n", p, mc,
           x.enabled ? '+' : '-', x.tsize, x.maskall ? '+' : '-');
    printf("               Vector table: BAR=%u offset=%08x  (%u B)\n", x.tbir, x.toff,
           x.tsize * PCI_MSIX_ENTRY_SIZE);
    printf("               PBA: BAR=%u offset=%08x  (%u B, 1 bit/vector)\n", x.pbir, x.poff,
           ((x.tsize + 63) / 64) * 8);
    return x;
}

static void decode_msix_table(const uint8_t *tbl, unsigned n, uint64_t v2m_base)
{
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *e = tbl + i * PCI_MSIX_ENTRY_SIZE;
        uint64_t a = rd32(e, PCI_MSIX_ENTRY_LOWER_ADDR) |
                     (uint64_t)rd32(e, PCI_MSIX_ENTRY_UPPER_ADDR) << 32;
        uint32_t d = rd32(e, PCI_MSIX_ENTRY_DATA), vc = rd32(e, PCI_MSIX_ENTRY_VECTOR_CTRL);
        printf("    entry %u: addr=0x%llx data=0x%08x vctl=0x%08x %s", i,
               (unsigned long long)a, d, vc, (vc & PCI_MSIX_ENTRY_CTRL_MASKBIT) ? "MASKED" : "unmasked");
        if (v2m_base && a == v2m_base + 0x40)   /* GICv2m MSI_SETSPI_NS (irq-gic-v2m.c) */
            printf("  -> GICv2m doorbell, data = SPI %u", d);
        printf("\n");
    }
}

int main(void)
{
    /* REAL: QEMU e1000e config space (first 0x100 bytes, zeros elided) */
    uint8_t cfg_e1000e[256] = {0};
    const uint8_t hdr[] = {0x86,0x80,0xd3,0x10,0x06,0x04,0x10,0x00};
    memcpy(cfg_e1000e, hdr, sizeof hdr);
    cfg_e1000e[0x34] = 0xc8;
    const uint8_t a0[] = {0x11,0x00,0x04,0x80,0x03,0x00,0x00,0x00,0x03,0x20,0x00,0x00};
    const uint8_t c8[] = {0x01,0xd0,0x22,0x00,0,0,0,0, 0x05,0xe0,0x80,0x00};
    const uint8_t e0[] = {0x10,0xa0,0x01,0x00,0x20,0x80,0x00,0x00,0,0,0,0,0x11,0x04,0x00,0x00};
    memcpy(cfg_e1000e + 0xa0, a0, sizeof a0);
    memcpy(cfg_e1000e + 0xc8, c8, sizeof c8);
    memcpy(cfg_e1000e + 0xe0, e0, sizeof e0);

    /* REAL: Apple VZ virtio-blk 00:06.0 — cap ptr 0x40, vendor caps 40/50/60/74, MSI-X at 0x84 */
    uint8_t cfg_vzblk[256] = {0};
    cfg_vzblk[0x06] = 0x10; cfg_vzblk[0x34] = 0x40;
    cfg_vzblk[0x40] = 0x09; cfg_vzblk[0x41] = 0x50;   /* vendor-specific (virtio) caps */
    cfg_vzblk[0x50] = 0x09; cfg_vzblk[0x51] = 0x60;
    cfg_vzblk[0x60] = 0x09; cfg_vzblk[0x61] = 0x74;
    cfg_vzblk[0x74] = 0x09; cfg_vzblk[0x75] = 0x84;
    const uint8_t vz80[] = {0x3c,0x00,0x00,0x00, 0x11,0x00,0x01,0x80, 0x02,0x00,0x00,0x00, 0x22,0x00,0x00,0x00};
    memcpy(cfg_vzblk + 0x80, vz80, sizeof vz80);
    /* REAL: MSI-X table read via /sys/.../resource2 mmap */
    const uint8_t tbl_vzblk[32] = {0x40,0x00,0xff,0x1f, 0,0,0,0, 0x8d,0,0,0, 0x00,0,0,0,
                                   0x40,0x00,0xff,0x1f, 0,0,0,0, 0x8e,0,0,0, 0x01,0,0,0};

    /* SYNTHETIC: enabled 64-bit MSI, MMC=32, MME=8, per-vector mask, ITS doorbell */
    uint8_t cfg_synth[256] = {0};
    cfg_synth[0x06] = 0x10; cfg_synth[0x34] = 0x50;
    const uint8_t s50[] = {0x05,0x00, 0xbb,0x01,              /* MsgCtl 0x01bb */
                           0x40,0x00,0x09,0x08, 0x00,0x00,0x00,0x00,   /* addr 0x08090040 */
                           0x20,0x00,0x00,0x00,                        /* data 0x0020 */
                           0xf0,0x00,0x00,0x00,                        /* mask vectors 4..7 */
                           0x00,0x00,0x00,0x00};
    memcpy(cfg_synth + 0x50, s50, sizeof s50);

    printf("[e1000e 8086:10d3, QEMU, real bytes]\n");
    struct msi_info m1 = decode_msi(cfg_e1000e);
    struct msix_info x1 = decode_msix(cfg_e1000e);
    assert(m1.found && !m1.enabled && m1.is64 && m1.mmc == 1 && !m1.maskable);
    assert(x1.found && x1.enabled && x1.tsize == 5 && x1.tbir == 3 && x1.toff == 0 &&
           x1.pbir == 3 && x1.poff == 0x2000);

    printf("[virtio-blk 1af4:1042, Apple VZ, real bytes]\n");
    struct msi_info m2 = decode_msi(cfg_vzblk);
    struct msix_info x2 = decode_msix(cfg_vzblk);
    assert(!m2.found && x2.found && x2.enabled && x2.tsize == 2 && x2.tbir == 2 && x2.poff == 0x20);
    decode_msix_table(tbl_vzblk, x2.tsize, 0x1fff0000ull);

    printf("[synthetic MSI, 8 of 32 vectors]\n");
    struct msi_info m3 = decode_msi(cfg_synth);
    assert(m3.enabled && m3.mmc == 32 && m3.mme == 8 && m3.is64 && m3.maskable &&
           m3.addr == 0x08090040ull && m3.data == 0x20 && m3.mask == 0xf0);
    assert(find_cap(cfg_synth, PCI_CAP_ID_MSIX) == 0);

    printf("ALL PASS\n");
    return 0;
}
