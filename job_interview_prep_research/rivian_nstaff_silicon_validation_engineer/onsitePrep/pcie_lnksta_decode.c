/* Bare-metal view of what `lspci -vvv` prints for a link: walk the capability list in
 * config space, find the PCI Express capability (ID 0x10), decode LnkCap / LnkSta.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined pcie_lnksta_decode.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ECAM: each function has 4 KiB of config space at base + bus<<20 | dev<<15 | fn<<12. */
static inline uintptr_t ecam_addr(uintptr_t base, unsigned bus, unsigned dev, unsigned fn, unsigned off)
{
    return base + (((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) | ((uintptr_t)fn << 12) | off);
}

static inline uint16_t rd16(const uint8_t *cfg, unsigned off) { return (uint16_t)(cfg[off] | (cfg[off + 1] << 8)); }
static inline uint32_t rd32(const uint8_t *cfg, unsigned off) { return rd16(cfg, off) | ((uint32_t)rd16(cfg, off + 2) << 16); }

#define PCI_VENDOR_ID     0x00
#define PCI_STATUS        0x06
#define PCI_STATUS_CAPLST (1u << 4)
#define PCI_CAP_PTR       0x34
#define PCI_CAP_ID_EXP    0x10
#define EXP_LNKCAP        0x0C
#define EXP_LNKSTA        0x12
#define LNKCAP_DLLLA_CAP  (1u << 20)   /* "LLActRep+" in lspci */
#define LNKSTA_TRAINING   (1u << 11)   /* "Train+"            */
#define LNKSTA_DLL_ACTIVE (1u << 13)   /* "DLActive+"         */

/* Returns the config offset of capability `id`, 0 if absent. Guard against a looping list. */
unsigned find_cap(const uint8_t *cfg, uint8_t id)
{
    if (rd16(cfg, PCI_VENDOR_ID) == 0xFFFF || !(rd16(cfg, PCI_STATUS) & PCI_STATUS_CAPLST))
        return 0;
    unsigned p = cfg[PCI_CAP_PTR] & 0xFCu;
    for (int guard = 48; p != 0 && guard > 0; guard--) {
        if (cfg[p] == id)
            return p;
        p = cfg[p + 1] & 0xFCu;
    }
    return 0;
}

typedef struct {
    unsigned cap_speed_x10, cap_width;   /* 160 = 16.0 GT/s */
    unsigned sta_speed_x10, sta_width;
    bool training, dll_active, dll_reporting;
} link_info_t;

static unsigned speed_x10(unsigned code)
{
    static const unsigned t[] = { 0, 25, 50, 80, 160, 320, 640 };   /* Gen1..Gen6 */
    return code < sizeof t / sizeof t[0] ? t[code] : 0;
}

bool decode_link(const uint8_t *cfg, link_info_t *li)
{
    unsigned e = find_cap(cfg, PCI_CAP_ID_EXP);
    if (e == 0)
        return false;
    uint32_t cap = rd32(cfg, e + EXP_LNKCAP);
    uint16_t sta = rd16(cfg, e + EXP_LNKSTA);
    li->cap_speed_x10 = speed_x10(cap & 0xFu);
    li->cap_width     = (cap >> 4) & 0x3Fu;
    li->sta_speed_x10 = speed_x10(sta & 0xFu);
    li->sta_width     = (sta >> 4) & 0x3Fu;
    li->training      = (sta & LNKSTA_TRAINING) != 0;
    li->dll_active    = (sta & LNKSTA_DLL_ACTIVE) != 0;
    li->dll_reporting = (cap & LNKCAP_DLLLA_CAP) != 0;
    return true;
}

static void wr16(uint8_t *cfg, unsigned off, uint16_t v) { cfg[off] = (uint8_t)v; cfg[off + 1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *cfg, unsigned off, uint32_t v) { wr16(cfg, off, (uint16_t)v); wr16(cfg, off + 2, (uint16_t)(v >> 16)); }

int main(void)
{
    CHECK(ecam_addr(0xE0000000u, 1, 0, 0, 0x12) == 0xE0100012u);
    CHECK(ecam_addr(0xE0000000u, 0, 2, 1, 0) == 0xE0011000u);

    /* Root port: caps 0x40 PM -> 0x50 MSI -> 0x60 PCIe. LnkCap Gen4 x4, LnkSta Gen3 x4 DLActive */
    uint8_t rp[256];
    memset(rp, 0, sizeof rp);
    wr16(rp, PCI_VENDOR_ID, 0x1234);
    wr16(rp, PCI_STATUS, PCI_STATUS_CAPLST);
    rp[PCI_CAP_PTR] = 0x40;
    rp[0x40] = 0x01; rp[0x41] = 0x50;
    rp[0x50] = 0x05; rp[0x51] = 0x60;
    rp[0x60] = 0x10; rp[0x61] = 0x00;
    wr32(rp, 0x60 + EXP_LNKCAP, 0x4u | (4u << 4) | LNKCAP_DLLLA_CAP);
    wr16(rp, 0x60 + EXP_LNKSTA, (uint16_t)(0x3u | (4u << 4) | LNKSTA_DLL_ACTIVE));

    link_info_t li;
    CHECK(find_cap(rp, PCI_CAP_ID_EXP) == 0x60);
    CHECK(decode_link(rp, &li));
    CHECK(li.cap_speed_x10 == 160 && li.cap_width == 4);
    CHECK(li.sta_speed_x10 == 80 && li.sta_width == 4);           /* downgraded speed */
    CHECK(li.dll_active && !li.training && li.dll_reporting);
    printf("LnkCap %u.%u GT/s x%u | LnkSta %u.%u GT/s x%u %s %s\n",
           li.cap_speed_x10 / 10, li.cap_speed_x10 % 10, li.cap_width,
           li.sta_speed_x10 / 10, li.sta_speed_x10 % 10, li.sta_width,
           li.dll_active ? "DLActive+" : "DLActive-",
           li.sta_speed_x10 < li.cap_speed_x10 ? "(downgraded)" : "(ok)");

    /* Link never reached L0: root port still enumerates, but DLActive- (maybe Train+) */
    wr16(rp, 0x60 + EXP_LNKSTA, (uint16_t)(0x1u | LNKSTA_TRAINING));
    CHECK(decode_link(rp, &li) && !li.dll_active && li.training && li.sta_width == 0);

    /* Endpoint behind a dead link: config read returns all ones -> "no device" */
    uint8_t dead[256];
    memset(dead, 0xFF, sizeof dead);
    CHECK(find_cap(dead, PCI_CAP_ID_EXP) == 0 && !decode_link(dead, &li));

    /* Looping capability list must not hang */
    rp[0x61] = 0x40;
    rp[0x60] = 0x11;
    CHECK(find_cap(rp, PCI_CAP_ID_EXP) == 0);

    puts(fails ? "FAIL" : "ALL PASS");
    return fails != 0;
}
