/*
 * L2  PCIe Config Space Check - capability walk, link status, SR-IOV, AER, BAR sizing
 *
 * After enumeration, a bring-up / system-validation tool checks every endpoint behind a
 * PCIe switch (an "lspci -vvv" that returns pass/fail). Config space is reached only through
 *   uint32_t cfg_rd32(uint16_t rid, uint16_t off);   off must be 4-byte aligned, < 4096
 *   void     cfg_wr32(uint16_t rid, uint16_t off, uint32_t val);
 * rid = bus << 8 | dev << 3 | fn. A missing device reads all-ones. The devices are a FAKE at
 * the bottom of this file (little-endian 4 KiB config space per function; writable-bit masks,
 * RW1C Status bits, BARs that read back their size mask).
 *
 * Implement:
 *   pci_probe        Vendor ID 0xFFFF -> -ENODEV. Header Type [6:0] != 0 -> -ENOTSUP. Else 0.
 *   pci_find_cap     standard list: only if Status bit 4 (Capabilities List) is set; start at the
 *                    byte at 0x34; mask pointer low 2 bits; pointer < 0x40 -> -EINVAL; more than
 *                    48 hops -> -ELOOP; end of list -> -ENOENT. Returns the cap offset.
 *   pci_find_ext_cap extended list from 0x100: header ID[15:0] ver[19:16] next[31:20].
 *                    Header 0 or 0xFFFFFFFF at 0x100 -> -ENOENT (no ext caps); later -> -EINVAL.
 *                    next 0 -> end (-ENOENT); 0 < next < 0x100 -> -EINVAL; > 480 hops -> -ELOOP.
 *   pci_link_check   PCI Express cap (ID 0x10): Link Capabilities (+0x0C) max speed [3:0], max
 *                    width [9:4]; Link Status (+0x12, 16-bit) current speed [3:0], width [9:4].
 *                    Fill LinkInfo; return LINK_SPEED_DOWN | LINK_WIDTH_DOWN flags (0 = full),
 *                    -ENOLINK if current speed or width is 0, or the find_cap error.
 *   pcie_speed_mts   speed code 1..6 -> 2500, 5000, 8000, 16000, 32000, 64000 MT/s; else 0.
 *   pci_sriov_vfs    SR-IOV ext cap (ID 0x0010): TotalVFs +0x0E, NumVFs +0x10, First VF Offset
 *                    +0x14, VF Stride +0x16. VF n (1-based) RID = PF RID + offset + (n-1)*stride.
 *                    NumVFs > TotalVFs -> -EINVAL. Any VF bus > sub_bus (the bridge's
 *                    subordinate bus, so also > 255) -> -ERANGE. Else return NumVFs and fill
 *                    vf_rid[0 .. min(NumVFs, max)-1]. SriovInfo is filled in every non-ENOENT case.
 *   pci_aer_status   AER ext cap (ID 0x0001): Uncorrectable Status +0x04, Correctable Status
 *                    +0x10. Return 1 if either is non-zero, 0 if clean, -ENOENT if no AER.
 *   pci_size_bars    BAR0..5: save, write 0xFFFFFFFF, read back, restore. 64-bit BARs use a pair.
 *                    Memory and I/O decode (Command bits 1:0) OFF while sizing, restored after.
 *                    Fill bar[6]; returns the number of implemented BARs (a 64-bit pair = 1),
 *                    -EINVAL if BAR5 claims to be 64-bit. Config space must be unchanged after.
 *
 * Only aligned 32-bit config accesses exist: 8/16-bit fields are extracted from a dword.
 * Every function except pci_probe may assume the device passed pci_probe.
 *
 * Build & run:  ./run.sh L2
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- config space layout (PCI / PCIe base spec) ---------------- */
#define CFG_SPACE_SIZE    4096u
#define PCI_VENDOR_ID     0x00      /* [15:0] vendor, [31:16] device */
#define PCI_COMMAND       0x04      /* [15:0] Command, [31:16] Status (error bits are RW1C) */
#define  CMD_IO           (1u << 0)
#define  CMD_MEM          (1u << 1)
#define  STATUS_CAP_LIST  (1u << 4) /* in the 16-bit Status register */
#define PCI_HEADER_TYPE   0x0E      /* byte; [6:0] layout, [7] multi-function */
#define PCI_BAR0          0x10
#define PCI_CAP_PTR       0x34      /* byte */
#define CAP_ID_PCIE       0x10
#define PCIE_LNKCAP       0x0C
#define PCIE_LNKSTA       0x12      /* 16-bit, upper half of the dword at +0x10 */
#define EXT_CAP_START     0x100
#define EXT_ID_AER        0x0001
#define EXT_ID_SRIOV      0x0010
#define AER_UNCOR_STATUS  0x04
#define AER_COR_STATUS    0x10
#define SRIOV_TOTAL_VFS   0x0E
#define SRIOV_NUM_VFS     0x10
#define SRIOV_VF_OFFSET   0x14
#define SRIOV_VF_STRIDE   0x16
#define CAP_MAX_HOPS      48        /* (256 - 64) / 4 */
#define EXT_CAP_MAX_HOPS  480       /* (4096 - 256) / 8 */

/* ---------------- HAL (provided, implemented by the fake) ---------------- */
uint32_t cfg_rd32(uint16_t rid, uint16_t off);
void     cfg_wr32(uint16_t rid, uint16_t off, uint32_t val);

#define RID(b, d, f) ((uint16_t)((b) << 8 | (d) << 3 | (f)))

typedef struct {
    uint8_t max_speed, max_width;   /* Link Capabilities */
    uint8_t cur_speed, cur_width;   /* Link Status */
} LinkInfo;
#define LINK_SPEED_DOWN 1
#define LINK_WIDTH_DOWN 2

typedef struct {
    uint16_t total_vfs, num_vfs, first_off, stride;
    uint32_t first_rid, last_rid;   /* 32-bit on purpose: may exceed 0xFFFF */
} SriovInfo;

#define BAR_IO      1u
#define BAR_MEM64   2u
#define BAR_PREF    4u
#define BAR_UPPER   8u              /* upper half of the previous 64-bit BAR */
typedef struct { uint64_t base, size; uint32_t flags; } PciBar;

int      pci_probe(uint16_t rid);
int      pci_find_cap(uint16_t rid, uint8_t id);
int      pci_find_ext_cap(uint16_t rid, uint16_t id);
int      pci_link_check(uint16_t rid, LinkInfo *li);
uint32_t pcie_speed_mts(uint8_t code);
int      pci_sriov_vfs(uint16_t pf, uint8_t sub_bus, SriovInfo *si, uint16_t *vf_rid, int max);
int      pci_aer_status(uint16_t rid, uint32_t *uncor, uint32_t *cor);
int      pci_size_bars(uint16_t rid, PciBar bar[6]);

/* ---------------- your code ---------------- */
/* Tip: write cfg_rd16 / cfg_rd8 helpers on top of cfg_rd32 (aligned dword + shift). */

int pci_probe(uint16_t rid)
{
    /* TODO: Vendor ID 0xFFFF -> -ENODEV; Header Type [6:0] != 0 -> -ENOTSUP */
    (void)rid;
    return 0;
}

int pci_find_cap(uint16_t rid, uint8_t id)
{
    /* TODO: Status bit 4, start at byte 0x34, mask [1:0], < 0x40 -> -EINVAL, 48-hop guard */
    (void)rid; (void)id;
    return 0;
}

int pci_find_ext_cap(uint16_t rid, uint16_t id)
{
    /* TODO: walk from 0x100: ID[15:0] ver[19:16] next[31:20], 480-hop guard */
    (void)rid; (void)id;
    return 0;
}

int pci_link_check(uint16_t rid, LinkInfo *li)
{
    /* TODO: PCIe cap (0x10): LnkCap +0x0C vs LnkSta +0x12 -> SPEED_DOWN / WIDTH_DOWN flags */
    (void)rid; (void)li;
    return 0;
}

uint32_t pcie_speed_mts(uint8_t code)
{
    /* TODO: 1..6 -> 2500 .. 64000 MT/s, else 0 */
    (void)code;
    return 0;
}

int pci_sriov_vfs(uint16_t pf, uint8_t sub_bus, SriovInfo *si, uint16_t *vf_rid, int max)
{
    /* TODO: SR-IOV ext cap (0x0010): TotalVFs, NumVFs, offset, stride -> VF RIDs, bus check */
    (void)pf; (void)sub_bus; (void)si; (void)vf_rid; (void)max;
    return 0;
}

int pci_aer_status(uint16_t rid, uint32_t *uncor, uint32_t *cor)
{
    /* TODO: AER ext cap (0x0001): Uncorrectable +0x04, Correctable +0x10 */
    (void)rid; (void)uncor; (void)cor;
    return 0;
}

int pci_size_bars(uint16_t rid, PciBar bar[6])
{
    /* TODO: decode off, for each BAR save / write ones / read / restore, 64-bit pairs, decode back */
    (void)rid; (void)bar;
    return 0;
}

/* =====================================================================
 * Test harness: fake PCIe functions behind a switch (do not edit)
 * ===================================================================== */
typedef struct {
    uint16_t rid;
    uint32_t dw[CFG_SPACE_SIZE / 4];    /* config space, dword view (little-endian) */
    uint32_t wm[CFG_SPACE_SIZE / 4];    /* RW bits */
    uint32_t w1c[CFG_SPACE_SIZE / 4];   /* RW1C bits */
} FakeFn;

#define NFN 8
static struct {
    FakeFn fn[NFN];
    int nfn;
    uint32_t bad_access;        /* unaligned or out-of-range config accesses */
    uint32_t decode_on_sizing;  /* all-ones written to a BAR while IO/MEM decode was on */
} F;

static FakeFn *fake_find(uint16_t rid)
{
    for (int i = 0; i < F.nfn; i++) if (F.fn[i].rid == rid) return &F.fn[i];
    return NULL;
}

uint32_t cfg_rd32(uint16_t rid, uint16_t off)
{
    if ((off & 3u) || off >= CFG_SPACE_SIZE) { F.bad_access++; return 0xFFFFFFFFu; }
    FakeFn *d = fake_find(rid);
    return d ? d->dw[off >> 2] : 0xFFFFFFFFu;           /* no device: UR -> all-ones */
}

void cfg_wr32(uint16_t rid, uint16_t off, uint32_t val)
{
    if ((off & 3u) || off >= CFG_SPACE_SIZE) { F.bad_access++; return; }
    FakeFn *d = fake_find(rid);
    if (!d) return;
    uint32_t i = off >> 2u;
    if (off >= PCI_BAR0 && off <= PCI_BAR0 + 20 && val == 0xFFFFFFFFu && (d->dw[1] & (CMD_IO | CMD_MEM)))
        F.decode_on_sizing++;
    d->dw[i] = (d->dw[i] & ~d->wm[i] & ~(val & d->w1c[i])) | (val & d->wm[i]);
}

/* ---- builders ---- */
static void fk_set(FakeFn *d, uint16_t off, uint32_t val, uint32_t wm)
{
    d->dw[off >> 2] = val; d->wm[off >> 2] = wm;
}

static FakeFn *fk_new(uint16_t rid, uint32_t id, uint32_t class_rev, uint8_t htype, uint32_t cmdsts)
{
    FakeFn *d = &F.fn[F.nfn++];
    memset(d, 0, sizeof *d);
    d->rid = rid;
    fk_set(d, 0x00, id, 0);
    fk_set(d, 0x04, cmdsts, 0x00000547u);               /* IO MEM BME PERR SERR INTxDis */
    d->w1c[1] = 0xF9000000u;                            /* Status error bits: RW1C */
    fk_set(d, 0x08, class_rev, 0);
    fk_set(d, 0x0C, (uint32_t)htype << 16, 0);
    return d;
}

static void fk_cap(FakeFn *d, uint8_t off, uint8_t id, uint8_t next, uint16_t rest)
{
    fk_set(d, off, (uint32_t)id | (uint32_t)next << 8 | (uint32_t)rest << 16, 0);
}

static void fk_pcie(FakeFn *d, uint8_t off, uint8_t next, uint32_t lnkcap, uint16_t lnksta)
{
    fk_cap(d, off, CAP_ID_PCIE, next, 0x0002);          /* version 2, endpoint */
    fk_set(d, (uint16_t)(off + 0x0C), lnkcap, 0);
    fk_set(d, (uint16_t)(off + 0x10), (uint32_t)lnksta << 16 | 0x0040u, 0x0000FFFFu); /* LnkCtl CCC */
}

static void fk_ext(FakeFn *d, uint16_t off, uint16_t id, uint8_t ver, uint16_t next)
{
    fk_set(d, off, (uint32_t)id | (uint32_t)ver << 16 | (uint32_t)next << 20, 0);
}

static void fk_aer(FakeFn *d, uint16_t off, uint16_t next, uint32_t uncor, uint32_t cor)
{
    fk_ext(d, off, EXT_ID_AER, 2, next);
    fk_set(d, (uint16_t)(off + 0x04), uncor, 0); d->w1c[(off + 0x04) >> 2] = 0xFFFFFFFFu;
    fk_set(d, (uint16_t)(off + 0x10), cor, 0);   d->w1c[(off + 0x10) >> 2] = 0xFFFFFFFFu;
}

static void fk_sriov(FakeFn *d, uint16_t off, uint16_t next, uint16_t total, uint16_t num,
                     uint16_t first_off, uint16_t stride)
{
    fk_ext(d, off, EXT_ID_SRIOV, 1, next);
    fk_set(d, (uint16_t)(off + 0x08), 0x00000009u, 0x0000001Fu);       /* VF Enable | VF MSE */
    fk_set(d, (uint16_t)(off + 0x0C), (uint32_t)total << 16 | total, 0); /* Initial | Total */
    fk_set(d, (uint16_t)(off + 0x10), num, 0x0000FFFFu);               /* NumVFs */
    fk_set(d, (uint16_t)(off + 0x14), (uint32_t)stride << 16 | first_off, 0);
    fk_set(d, (uint16_t)(off + 0x18), 0x10011234u, 0);                 /* VF Device ID */
}

static void fk_nvme(uint16_t rid, uint32_t bar0, uint16_t lnksta, uint32_t cor)
{
    FakeFn *d = fk_new(rid, 0x00011234u, 0x01080201u, 0x00, 0x00100006u); /* NVMe, MEM|BME */
    fk_set(d, PCI_CAP_PTR, 0x40, 0);
    fk_cap(d, 0x40, 0x01, 0x50, 0x0003);                 /* Power Management */
    fk_cap(d, 0x50, 0x11, 0x70, 0x001F);                 /* MSI-X, 32 vectors */
    fk_pcie(d, 0x70, 0x00, 0x00400C44u, lnksta);         /* Gen4 x4 capable */
    fk_aer(d, 0x100, 0x148, 0, cor);
    fk_ext(d, 0x148, 0x0003, 1, 0x158);                  /* Device Serial Number */
    fk_ext(d, 0x158, 0x0019, 1, 0x000);                  /* Secondary PCIe (Gen3+) */
    fk_set(d, 0x10, bar0, 0xFFFFC000u);                  /* 64-bit, 16 KiB */
    fk_set(d, 0x14, 0, 0xFFFFFFFFu);
}

static void fake_reset(void)
{
    memset(&F, 0, sizeof F);
    FakeFn *d;
    /* 00:01.0 switch downstream port (Type 1) */
    fk_new(RID(0, 1, 0), 0xB0001234u, 0x06040000u, 0x01, 0x00100007u);
    /* 01:00.0 healthy NVMe Gen4 x4 */
    fk_nvme(RID(1, 0, 0), 0xA0000004u, 0x1044, 0);
    /* 02:00.0 same SSD, link trained to Gen2 x2, correctable errors logged */
    fk_nvme(RID(2, 0, 0), 0xA0100004u, 0x1022, 0x00000041u);
    /* 03:00.0 NIC with broken capability lists */
    d = fk_new(RID(3, 0, 0), 0x00021234u, 0x02000001u, 0x00, 0x00100006u);
    fk_set(d, PCI_CAP_PTR, 0x40, 0);
    fk_cap(d, 0x40, 0x01, 0x53, 0x0003);                 /* next has junk in [1:0] */
    fk_cap(d, 0x50, 0x05, 0x40, 0x0080);                 /* MSI, next -> 0x40: loop */
    fk_aer(d, 0x100, 0x110, 0, 0);
    fk_ext(d, 0x110, 0x0003, 1, 0x100);                  /* ext loop */
    /* 04:00.0 accelerator PF with SR-IOV, Gen5 x8, 8 GiB 64-bit prefetchable BAR */
    d = fk_new(RID(4, 0, 0), 0x10001234u, 0x12000001u, 0x00, 0x00100006u);
    fk_set(d, PCI_CAP_PTR, 0x40, 0);
    fk_pcie(d, 0x40, 0x80, 0x00400C85u, 0x1085);
    fk_cap(d, 0x80, 0x11, 0x00, 0x003F);
    fk_aer(d, 0x100, 0x148, 0x00004000u, 0);             /* Completion Timeout */
    fk_ext(d, 0x148, 0x000E, 1, 0x160);                  /* ARI */
    fk_sriov(d, 0x160, 0x000, 16, 8, 0x0080, 2);
    fk_set(d, 0x10, 0x0000000Cu, 0x00000000u);           /* 8 GiB: no writable low bits */
    fk_set(d, 0x14, 0x00000060u, 0xFFFFFFFEu);
    /* 05:00.0 multi-function accelerator: 64-bit pref 16 MiB, 32-bit 4 KiB, I/O 256 B */
    d = fk_new(RID(5, 0, 0), 0x20001234u, 0x12000001u, 0x80, 0x20100007u); /* RMA logged */
    fk_set(d, PCI_CAP_PTR, 0x40, 0);
    fk_pcie(d, 0x40, 0x00, 0x00400D04u, 0x1104);         /* Gen4 x16 */
    fk_set(d, 0x10, 0x0100000Cu, 0xFF000000u);
    fk_set(d, 0x14, 0x00000040u, 0xFFFFFFFFu);
    fk_set(d, 0x18, 0x90100000u, 0xFFFFF000u);
    fk_set(d, 0x20, 0x00002001u, 0x0000FF00u);           /* I/O: upper 16 bits hardwired 0 */
}

static void fake_poke(uint16_t rid, uint16_t off, uint32_t val)   /* raw, ignores masks */
{
    FakeFn *d = fake_find(rid);
    if (d) d->dw[off >> 2] = val;
}

int main(void)
{
    const uint16_t BR = RID(0, 1, 0), NVME = RID(1, 0, 0), DOWN = RID(2, 0, 0),
                   BAD = RID(3, 0, 0), PF = RID(4, 0, 0), ACC = RID(5, 0, 0), NONE = RID(6, 0, 0);
    LinkInfo li;
    PciBar bar[6];
    uint32_t ue, ce;

    /* 1. healthy Gen4 x4 NVMe endpoint */
    fake_reset();
    CHECK(pci_probe(NVME) == 0);
    CHECK(pci_find_cap(NVME, 0x10) == 0x70);
    CHECK(pci_find_cap(NVME, 0x11) == 0x50);
    CHECK(pci_find_cap(NVME, 0x05) == -ENOENT);          /* no MSI */
    CHECK(pci_find_ext_cap(NVME, EXT_ID_AER) == 0x100);
    CHECK(pci_find_ext_cap(NVME, 0x0019) == 0x158);
    CHECK(pci_find_ext_cap(NVME, EXT_ID_SRIOV) == -ENOENT);
    memset(&li, 0, sizeof li);
    CHECK(pci_link_check(NVME, &li) == 0);
    CHECK(li.max_speed == 4 && li.max_width == 4 && li.cur_speed == 4 && li.cur_width == 4);
    CHECK(pcie_speed_mts(li.cur_speed) == 16000);
    CHECK(pcie_speed_mts(1) == 2500 && pcie_speed_mts(6) == 64000);
    CHECK(pcie_speed_mts(0) == 0 && pcie_speed_mts(7) == 0);
    ue = ce = 0xDEAD;
    CHECK(pci_aer_status(NVME, &ue, &ce) == 0 && ue == 0 && ce == 0);
    memset(bar, 0xA5, sizeof bar);
    CHECK(pci_size_bars(NVME, bar) == 1);
    CHECK(bar[0].size == 0x4000 && bar[0].base == 0xA0000000u && bar[0].flags == BAR_MEM64);
    CHECK(bar[1].flags == BAR_UPPER && bar[1].size == 0 && bar[2].size == 0 && bar[5].size == 0);

    /* 2. downtrained link Gen4 x4 -> Gen2 x2, plus AER correctable errors */
    memset(&li, 0, sizeof li);
    CHECK(pci_link_check(DOWN, &li) == (LINK_SPEED_DOWN | LINK_WIDTH_DOWN));
    CHECK(li.max_speed == 4 && li.max_width == 4 && li.cur_speed == 2 && li.cur_width == 2);
    CHECK(pcie_speed_mts(li.cur_speed) == 5000);
    CHECK(pci_aer_status(DOWN, &ue, &ce) == 1 && ue == 0 && ce == 0x41);   /* RxErr | BadTLP */
    fake_poke(DOWN, 0x70 + 0x10, 0x10240040u);           /* Gen4 x2: width only */
    CHECK(pci_link_check(DOWN, &li) == LINK_WIDTH_DOWN);
    fake_poke(DOWN, 0x70 + 0x10, 0x00000040u);           /* link down: speed 0, width 0 */
    CHECK(pci_link_check(DOWN, &li) == -ENOLINK);

    /* 3. broken capability lists: masked pointer, loops, bad pointers, no list */
    CHECK(pci_probe(BAD) == 0);
    CHECK(pci_find_cap(BAD, 0x05) == 0x50);              /* via next = 0x53 & 0xFC */
    CHECK(pci_find_cap(BAD, 0x10) == -ELOOP);            /* 0x40 -> 0x50 -> 0x40 ... */
    CHECK(pci_link_check(BAD, &li) == -ELOOP);
    CHECK(pci_find_ext_cap(BAD, 0x0003) == 0x110);
    CHECK(pci_find_ext_cap(BAD, EXT_ID_SRIOV) == -ELOOP);
    fake_poke(BAD, 0x50, 0x00803F05u);                   /* MSI next = 0x3F -> 0x3C */
    CHECK(pci_find_cap(BAD, 0x10) == -EINVAL);
    fake_poke(BAD, 0x110, 0x0F010003u);                  /* ext next = 0x0F0 */
    CHECK(pci_find_ext_cap(BAD, EXT_ID_SRIOV) == -EINVAL);
    fake_poke(BAD, 0x110, 0xFFFFFFFFu);                  /* all-ones mid-list */
    CHECK(pci_find_ext_cap(BAD, EXT_ID_SRIOV) == -EINVAL);
    fake_poke(BAD, 0x04, 0x00000006u);                   /* Status.CapList = 0 */
    CHECK(pci_find_cap(BAD, 0x01) == -ENOENT);
    CHECK(pci_link_check(BAD, &li) == -ENOENT);

    /* 4. SR-IOV PF 04:00.0, 8 VFs, offset 0x80, stride 2, behind a bridge with sub_bus 4 */
    SriovInfo si;
    uint16_t vf[16];
    memset(vf, 0, sizeof vf);
    memset(&si, 0, sizeof si);
    CHECK(pci_find_ext_cap(PF, EXT_ID_SRIOV) == 0x160);
    CHECK(pci_sriov_vfs(PF, 4, &si, vf, 16) == 8);
    CHECK(si.total_vfs == 16 && si.num_vfs == 8 && si.first_off == 0x80 && si.stride == 2);
    CHECK(si.first_rid == 0x0480 && si.last_rid == 0x048E);
    CHECK(vf[0] == RID(4, 16, 0) && vf[1] == RID(4, 16, 2) && vf[4] == RID(4, 17, 0)
          && vf[7] == RID(4, 17, 6) && vf[8] == 0);
    CHECK(pci_sriov_vfs(PF, 4, &si, vf, 3) == 8);        /* caller buffer smaller: still count */
    CHECK(pci_aer_status(PF, &ue, &ce) == 1 && ue == 0x4000 && ce == 0);   /* Completion Timeout */
    fake_poke(PF, 0x160 + 0x14, 0x000200F8u);            /* offset 0xF8: VFs spill onto bus 5 */
    CHECK(pci_sriov_vfs(PF, 4, &si, vf, 16) == -ERANGE);
    CHECK(si.first_rid == 0x04F8 && si.last_rid == 0x0506);
    CHECK(pci_sriov_vfs(PF, 5, &si, vf, 16) == 8);       /* fine if the bridge covers bus 5 */
    fake_poke(PF, 0x160 + 0x14, 0x0002FC80u);            /* offset 0xFC80: past bus 255 */
    CHECK(pci_sriov_vfs(PF, 0xFF, &si, vf, 16) == -ERANGE);
    CHECK(si.first_rid == 0x10080 && si.last_rid == 0x1008E);
    fake_poke(PF, 0x160 + 0x10, 17);                     /* NumVFs 17 > TotalVFs 16 */
    CHECK(pci_sriov_vfs(PF, 0xFF, &si, vf, 16) == -EINVAL);
    fake_poke(PF, 0x160 + 0x10, 0);                      /* SR-IOV not enabled */
    CHECK(pci_sriov_vfs(PF, 4, &si, vf, 16) == 0 && si.first_rid == 0);
    CHECK(pci_sriov_vfs(NVME, 4, &si, vf, 16) == -ENOENT);

    CHECK(F.bad_access == 0);                            /* only aligned dword accesses */

    /* 5. BAR sizing: 64-bit pref 16 MiB + 32-bit 4 KiB + I/O 256 B, and an 8 GiB BAR */
    fake_reset();
    static uint32_t before[CFG_SPACE_SIZE / 4];
    memcpy(before, fake_find(ACC)->dw, sizeof before);
    CHECK(pci_probe(ACC) == 0);                          /* header type 0x80: MF bit set */
    memset(bar, 0xA5, sizeof bar);
    CHECK(pci_size_bars(ACC, bar) == 3);
    CHECK(bar[0].size == 0x01000000u && bar[0].base == 0x4001000000ull
          && bar[0].flags == (BAR_MEM64 | BAR_PREF));
    CHECK(bar[1].flags == BAR_UPPER && bar[1].size == 0);
    CHECK(bar[2].size == 0x1000 && bar[2].base == 0x90100000u && bar[2].flags == 0);
    CHECK(bar[3].size == 0 && bar[3].flags == 0);
    CHECK(bar[4].size == 0x100 && bar[4].base == 0x2000 && bar[4].flags == BAR_IO);
    CHECK(bar[5].size == 0);
    CHECK(memcmp(before, fake_find(ACC)->dw, sizeof before) == 0);   /* BARs, Command, Status */
    CHECK(F.decode_on_sizing == 0);
    memset(bar, 0, sizeof bar);
    CHECK(pci_size_bars(PF, bar) == 1);
    CHECK(bar[0].size == 0x200000000ull && bar[0].base == 0x6000000000ull
          && bar[0].flags == (BAR_MEM64 | BAR_PREF));
    CHECK(fake_find(PF)->dw[1] == 0x00100006u && F.decode_on_sizing == 0);

    /* 6. absent device and non-Type-0 header */
    CHECK(pci_probe(NONE) == -ENODEV);
    CHECK(pci_probe(BR) == -ENOTSUP);
    CHECK(pci_find_ext_cap(NONE, EXT_ID_AER) == -ENOENT);   /* 0x100 reads all-ones */
    CHECK(pci_find_ext_cap(ACC, EXT_ID_AER) == -ENOENT);    /* 0x100 reads 0 */
    CHECK(F.bad_access == 0);                               /* only aligned dword accesses */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
