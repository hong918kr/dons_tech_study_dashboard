/* PCIE-B04 추가 연습 — 한 번에 검증
 *
 * 연습 1: Linux 식 legacy cap 찾기 (TTL 48, 하위 2비트 마스크, ID 0xFF 에서 중단) vs 엄격한 워커
 * 연습 2: Linux 식 extended cap 찾기 (TTL 480, 헤더 0 에서 중단, next & 0xFFC) + 같은 ID 의 "다음" 인스턴스
 * 연습 3: dmesg 의 "X Gb/s available PCIe bandwidth, limited by ..." 줄을 config 덤프 체인으로 재현
 * 연습 4: MSI / MSI-X capability 디코드 (벡터 수, Table/PBA 위치, 둘 다 켜짐 = 오류)
 *
 * 규칙 출처: Linux 6.18.55 drivers/pci/pci.h PCI_FIND_NEXT_CAP / PCI_FIND_NEXT_EXT_CAP,
 *           drivers/pci/pci.c pcie_bandwidth_available() / __pcie_print_link_status(),
 *           include/uapi/linux/pci_regs.h (상수 값).
 * 덤프: 본문과 같은 가상 Gen4 x4 SSD / 가상 루트 포트 (축약 빌더) + QEMU 10.2.1 캡처의 0 아닌 행.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PCI_CFG_SPACE_SIZE      256
#define PCI_CFG_SPACE_EXP_SIZE  4096
#define PCI_STD_HEADER_SIZEOF   64
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CAPABILITY_LIST     0x34
#define PCI_FIND_CAP_TTL        48
#define  PCI_CAP_ID_MSI         0x05
#define  PCI_CAP_ID_EXP         0x10
#define  PCI_CAP_ID_MSIX        0x11
#define  PCI_CAP_ID_SATA        0x12
#define PCI_MSI_FLAGS_ENABLE    0x0001
#define PCI_MSI_FLAGS_QMASK     0x000e
#define PCI_MSI_FLAGS_QSIZE     0x0070
#define PCI_MSI_FLAGS_64BIT     0x0080
#define PCI_MSI_FLAGS_MASKBIT   0x0100
#define PCI_MSIX_FLAGS_QSIZE    0x07FF
#define PCI_MSIX_FLAGS_MASKALL  0x4000
#define PCI_MSIX_FLAGS_ENABLE   0x8000
#define PCI_MSIX_TABLE_BIR      0x00000007
#define PCI_MSIX_TABLE_OFFSET   0xfffffff8
#define PCI_EXP_LNKCAP          0x0c
#define PCI_EXP_LNKSTA          0x12
#define PCI_EXP_LNKCAP2         0x2c
#define PCI_EXT_CAP_ID(h)       ((h) & 0x0000ffff)
#define PCI_EXT_CAP_NEXT(h)     (((h) >> 20) & 0xffc)    /* Linux 와 같이 하위 2비트 마스크 */
#define  PCI_EXT_CAP_ID_ERR     0x01
#define  PCI_EXT_CAP_ID_DSN     0x03
#define  PCI_EXT_CAP_ID_VNDR    0x0B
#define  PCI_EXT_CAP_ID_LTR     0x18
#define  PCI_EXT_CAP_ID_SECPCI  0x19
#define  PCI_EXT_CAP_ID_L1SS    0x1E
#define  PCI_EXT_CAP_ID_PL_16GT 0x26

static uint16_t rd16(const uint8_t *c, unsigned o) { return (uint16_t)(c[o] | (c[o + 1] << 8)); }
static uint32_t rd32(const uint8_t *c, unsigned o) {
    return (uint32_t)c[o] | ((uint32_t)c[o + 1] << 8) | ((uint32_t)c[o + 2] << 16) | ((uint32_t)c[o + 3] << 24);
}
static void w16(uint8_t *c, unsigned o, uint16_t v) { c[o] = (uint8_t)v; c[o + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *c, unsigned o, uint32_t v) { for (int i = 0; i < 4; i++) c[o + (unsigned)i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ext_hdr(uint16_t id, uint8_t ver, uint16_t next) { return (uint32_t)id | ((uint32_t)ver << 16) | ((uint32_t)next << 20); }

/* ---------- 본문 가상 덤프 (PCIE-B04 solution.c 빌더 축약판: 연습에 쓰는 필드만) ---------- */
static void build_ssd(uint8_t c[PCI_CFG_SPACE_EXP_SIZE]) {
    memset(c, 0, PCI_CFG_SPACE_EXP_SIZE);
    w16(c, 0x00, 0xF00D); w16(c, 0x02, 0x5404); w16(c, 0x04, 0x0406); w16(c, 0x06, 0x0010);
    c[0x34] = 0x40;
    c[0x40] = 0x01; c[0x41] = 0x50; w16(c, 0x42, 0x0003);
    c[0x50] = 0x05; c[0x51] = 0x70; w16(c, 0x52, 0x0180);
    c[0x70] = 0x10; c[0x71] = 0xB0; w16(c, 0x72, 0x0002);
    w32(c, 0x7C, 0x00070844); w16(c, 0x80, 0x0142); w16(c, 0x82, 0x1024); w32(c, 0x9C, 0x1E); w16(c, 0xA0, 0x4);
    c[0xB0] = 0x11; c[0xB1] = 0xC0; w16(c, 0xB2, 0x801F); w32(c, 0xB4, 0x00002000); w32(c, 0xB8, 0x00003000);
    c[0xC0] = 0x09; c[0xC1] = 0x00; c[0xC2] = 0x10;
    w32(c, 0x100, ext_hdr(PCI_EXT_CAP_ID_ERR, 2, 0x148));
    w32(c, 0x148, ext_hdr(PCI_EXT_CAP_ID_LTR, 1, 0x150));
    w32(c, 0x150, ext_hdr(PCI_EXT_CAP_ID_L1SS, 1, 0x160));
    w32(c, 0x160, ext_hdr(PCI_EXT_CAP_ID_DSN, 1, 0x170));
    w32(c, 0x170, ext_hdr(PCI_EXT_CAP_ID_SECPCI, 1, 0x1A0));
    w32(c, 0x1A0, ext_hdr(PCI_EXT_CAP_ID_PL_16GT, 1, 0x000));
}
/* 링크 체인용 최소 포트: PCIe cap 만 0x40 에 (LnkCap/LnkSta/LnkCap2) */
static void build_port(uint8_t c[PCI_CFG_SPACE_SIZE], uint8_t type, uint32_t lnkcap, uint16_t lnksta) {
    memset(c, 0, PCI_CFG_SPACE_SIZE);
    w16(c, 0x00, 0xF00D); w16(c, 0x06, 0x0010); c[0x34] = 0x40;
    c[0x40] = 0x10; c[0x41] = 0x00; w16(c, 0x42, (uint16_t)(0x0002 | (type << 4)));
    w32(c, 0x40 + PCI_EXP_LNKCAP, lnkcap); w16(c, 0x40 + PCI_EXP_LNKSTA, lnksta);
    w32(c, 0x40 + PCI_EXP_LNKCAP2, 0x1E);
}
/* QEMU 10.2.1 캡처(qemu_capture_xxxx.txt)의 0 아닌 행 */
typedef struct { uint16_t off; uint8_t b[16]; } row16;
static const row16 QN_ROWS[] = {   /* 01:00.0 nvme */
    { 0x00, { 0x36,0x1b,0x10,0x00,0x00,0x00,0x10,0x00,0x02,0x02,0x08,0x01,0x00,0x00,0x00,0x00 } },
    { 0x10, { 0x04,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0x20, { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xf4,0x1a,0x00,0x11 } },
    { 0x30, { 0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00 } },
    { 0x40, { 0x11,0x80,0x40,0x00,0x00,0x20,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0x60, { 0x01,0x00,0x03,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0x80, { 0x10,0x60,0x02,0x00,0x20,0x80,0x00,0x10,0x00,0x00,0x00,0x00,0x11,0x04,0x00,0x00 } },
    { 0x90, { 0x00,0x00,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0xa0, { 0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
};
static const row16 QR_ROWS[] = {   /* 00:02.0 pcie-root-port */
    { 0x00, { 0x36,0x1b,0x0c,0x00,0x07,0x04,0x10,0x00,0x00,0x00,0x04,0x06,0x00,0x00,0x01,0x00 } },
    { 0x10, { 0x00,0x10,0x28,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x10,0x10,0x00,0x00 } },
    { 0x20, { 0x00,0x10,0x10,0x10,0x01,0x00,0x11,0x00,0x80,0x00,0x00,0x00,0x80,0x00,0x00,0x00 } },
    { 0x30, { 0x00,0x00,0x00,0x00,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x01,0x02,0x00 } },
    { 0x40, { 0x0d,0x00,0x00,0x00,0x36,0x1b,0x00,0x00,0x11,0x40,0x00,0x80,0x00,0x00,0x00,0x00 } },
    { 0x50, { 0x00,0x08,0x00,0x00,0x10,0x48,0x42,0x01,0x20,0x80,0x00,0x00,0x0f,0x00,0x00,0x00 } },
    { 0x60, { 0x04,0x06,0x30,0x00,0x00,0x00,0x11,0x20,0x7b,0x00,0x0a,0x00,0xc0,0x03,0x50,0x00 } },
    { 0x70, { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x30,0x00,0x00,0x00,0x00,0x00 } },
    { 0x80, { 0x1e,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0x100,{ 0x01,0x00,0x82,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x02,0x30,0x20,0x46,0x00 } },
    { 0x110,{ 0x00,0x00,0x00,0x00,0x00,0xe0,0x00,0x00,0xa0,0x02,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { 0x120,{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00 } },
    { 0x140,{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d,0x00,0x01,0x00,0x5f,0x00,0x1d,0x00 } },
};
static void load_rows(uint8_t *c, const row16 *r, size_t n) {
    memset(c, 0, PCI_CFG_SPACE_EXP_SIZE);
    for (size_t i = 0; i < n; i++) memcpy(c + r[i].off, r[i].b, 16);
}

/* ---------- 엄격한 워커 (본문 cap_walk_legacy 축약판: status 만) ---------- */
enum { CW_OK = 0, CW_NONE = 1, CW_ERR_LOOP = -1, CW_ERR_PTR_LOW = -2, CW_ERR_ALIGN = -3, CW_ERR_ALLONES = -5 };
static int strict_walk(const uint8_t *c, int *n) {
    uint32_t vis[2] = { 0, 0 };
    *n = 0;
    if (!(rd16(c, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return CW_NONE;
    unsigned p = c[PCI_CAPABILITY_LIST];
    while (p) {
        if (p < PCI_STD_HEADER_SIZEOF) return CW_ERR_PTR_LOW;
        if (p & 3) return CW_ERR_ALIGN;
        unsigned s = (p - PCI_STD_HEADER_SIZEOF) / 4;
        if (vis[s >> 5] & (1u << (s & 31))) return CW_ERR_LOOP;
        vis[s >> 5] |= 1u << (s & 31);
        if (c[p] == 0xFF) return CW_ERR_ALLONES;
        (*n)++;
        p = c[p + 1];
    }
    return CW_OK;
}

/* ================= 연습 1: Linux 식 legacy 찾기 ================= */
/* __pci_bus_find_cap_start + PCI_FIND_NEXT_CAP 와 같은 규칙. *reads = cap 헤더 읽은 횟수 */
static unsigned linux_find_cap(const uint8_t *c, uint8_t cap, int *reads) {
    *reads = 0;
    if (!(rd16(c, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return 0;
    int ttl = PCI_FIND_CAP_TTL;
    unsigned pos = c[PCI_CAPABILITY_LIST];
    while (ttl--) {
        if (pos < PCI_STD_HEADER_SIZEOF) break;
        pos &= ~3u;                                   /* ALIGN_DOWN(pos, 4) */
        uint16_t ent = rd16(c, pos); (*reads)++;
        uint8_t id = (uint8_t)(ent & 0xff);
        if (id == 0xff) break;
        if (id == cap) return pos;
        pos = ent >> 8;
    }
    return 0;
}

/* ================= 연습 2: Linux 식 extended 찾기 ================= */
static unsigned linux_find_ext_cap(const uint8_t *c, unsigned start, uint16_t cap, int *reads) {
    unsigned pos = start ? start : PCI_CFG_SPACE_SIZE;
    int ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;   /* 480 */
    *reads = 0;
    while (ttl-- > 0 && pos >= PCI_CFG_SPACE_SIZE) {
        uint32_t h = rd32(c, pos); (*reads)++;
        if (h == 0) break;
        if (PCI_EXT_CAP_ID(h) == cap && pos != start) return pos;
        pos = PCI_EXT_CAP_NEXT(h);
    }
    return 0;
}

/* ================= 연습 3: dmesg 대역폭 줄 ================= */
static const char *lx_speed(unsigned code) {     /* pci_speed_string() 의 PCIe 부분 */
    static const char *t[] = { "Unknown", "2.5 GT/s PCIe", "5.0 GT/s PCIe", "8.0 GT/s PCIe",
                               "16.0 GT/s PCIe", "32.0 GT/s PCIe", "64.0 GT/s PCIe" };
    return code <= 6 ? t[code] : "Unknown";
}
static unsigned mbps_enc(unsigned code) {         /* PCIE_SPEED2MBS_ENC: 레인당 인코딩 후 Mb/s (정수 나눗셈) */
    switch (code) {
    case 1: return 2500 * 8 / 10;   case 2: return 5000 * 8 / 10;
    case 3: return 8000 * 128 / 130; case 4: return 16000 * 128 / 130;
    case 5: return 32000 * 128 / 130; case 6: return 64000;
    default: return 0;
    }
}
static unsigned exp_off(const uint8_t *c) { int r; return linux_find_cap(c, PCI_CAP_ID_EXP, &r); }
static unsigned speed_cap(const uint8_t *c, unsigned e) {   /* 지원 벡터의 최고 비트 */
    uint32_t lc = rd32(c, e + PCI_EXP_LNKCAP);
    unsigned v = rd32(c, e + PCI_EXP_LNKCAP2) & 0xFE, mx = lc & 0xF;
    v &= (2u << mx) - 1u;
    if (!v) v = mx == 2 ? 0x06 : mx == 1 ? 0x02 : 0;
    unsigned k = 0; while (v >>= 1) k++;
    return k;
}
/* devs[0] = 장치 자신, devs[1..] = 위쪽 브리지들 (루트 포트까지). 출력 없으면 "" */
static void bw_line(const uint8_t *const *devs, const char *const *names, int n, bool verbose, char *out, size_t cap) {
    out[0] = '\0';
    unsigned e0 = exp_off(devs[0]);
    unsigned sc = speed_cap(devs[0], e0), wc = (rd32(devs[0], e0 + PCI_EXP_LNKCAP) >> 4) & 0x3F;
    unsigned bw_cap = wc * mbps_enc(sc);
    unsigned bw = 0, ls = 0, lw = 0; int lim = -1;
    for (int i = 0; i < n; i++) {
        unsigned e = exp_off(devs[i]);
        uint16_t st = rd16(devs[i], e + PCI_EXP_LNKSTA);
        unsigned s = st & 0xF, w = (st >> 4) & 0x3F, nb = w * mbps_enc(s);
        if (!bw || nb <= bw) { bw = nb; lim = i; ls = s; lw = w; }   /* 같으면 더 위쪽이 이긴다 (<=) */
    }
    if (bw >= bw_cap && verbose)
        snprintf(out, cap, "%u.%03u Gb/s available PCIe bandwidth (%s x%u link)", bw_cap / 1000, bw_cap % 1000, lx_speed(sc), wc);
    else if (bw < bw_cap)
        snprintf(out, cap, "%u.%03u Gb/s available PCIe bandwidth, limited by %s x%u link at %s (capable of %u.%03u Gb/s with %s x%u link)",
                 bw / 1000, bw % 1000, lx_speed(ls), lw, lim >= 0 ? names[lim] : "<unknown>",
                 bw_cap / 1000, bw_cap % 1000, lx_speed(sc), wc);
}

/* ================= 연습 4: MSI / MSI-X ================= */
typedef struct {
    bool msi, msi_en, msi64, msi_mask; unsigned msi_cap_vec, msi_en_vec;
    bool msix, msix_en, msix_maskall; unsigned msix_vec, tbl_bir, tbl_off, pba_bir, pba_off;
    bool conflict;   /* MSI 와 MSI-X 를 동시에 켬 — 스펙상 금지 */
} irq_caps;
static irq_caps irq_decode(const uint8_t *c) {
    irq_caps q; memset(&q, 0, sizeof q);
    int r;
    unsigned m = linux_find_cap(c, PCI_CAP_ID_MSI, &r);
    if (m) {
        uint16_t f = rd16(c, m + 2);
        q.msi = true; q.msi_en = f & PCI_MSI_FLAGS_ENABLE; q.msi64 = f & PCI_MSI_FLAGS_64BIT; q.msi_mask = f & PCI_MSI_FLAGS_MASKBIT;
        q.msi_cap_vec = 1u << ((f & PCI_MSI_FLAGS_QMASK) >> 1);       /* Multiple Message Capable: 2^n */
        q.msi_en_vec  = 1u << ((f & PCI_MSI_FLAGS_QSIZE) >> 4);       /* Multiple Message Enable */
    }
    unsigned x = linux_find_cap(c, PCI_CAP_ID_MSIX, &r);
    if (x) {
        uint16_t f = rd16(c, x + 2);
        uint32_t t = rd32(c, x + 4), p = rd32(c, x + 8);
        q.msix = true; q.msix_en = f & PCI_MSIX_FLAGS_ENABLE; q.msix_maskall = f & PCI_MSIX_FLAGS_MASKALL;
        q.msix_vec = (f & PCI_MSIX_FLAGS_QSIZE) + 1u;                  /* N-1 인코딩 */
        q.tbl_bir = t & PCI_MSIX_TABLE_BIR; q.tbl_off = t & PCI_MSIX_TABLE_OFFSET;
        q.pba_bir = p & PCI_MSIX_TABLE_BIR; q.pba_off = p & PCI_MSIX_TABLE_OFFSET;
    }
    q.conflict = q.msi_en && q.msix_en;
    return q;
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t ssd[4096], m[4096], qn[4096], qr[4096];

int main(void) {
    int reads, n;
    build_ssd(ssd);
    load_rows(qn, QN_ROWS, sizeof QN_ROWS / sizeof QN_ROWS[0]);
    load_rows(qr, QR_ROWS, sizeof QR_ROWS / sizeof QR_ROWS[0]);

    /* 연습 1 */
    CHECK(linux_find_cap(ssd, PCI_CAP_ID_MSIX, &reads) == 0xB0 && reads == 4);
    CHECK(linux_find_cap(ssd, PCI_CAP_ID_SATA, &reads) == 0 && reads == 5);       /* 끝(next 0)까지 5개 */
    memcpy(m, ssd, 4096); m[0x51] = 0x40;                                          /* 루프 */
    CHECK(linux_find_cap(m, PCI_CAP_ID_SATA, &reads) == 0 && reads == 48);        /* TTL 이 다 닳을 때까지 */
    CHECK(strict_walk(m, &n) == CW_ERR_LOOP && n == 2);                            /* 엄격 워커는 3번째 칸에서 */
    printf("practice 1: loop dump, absent ID -> Linux reads=%d, strict walker stops after %d caps (LOOP)\n", reads, n);
    memcpy(m, ssd, 4096); m[0x41] = 0x51;                                          /* 예약 비트가 켜진 포인터 */
    CHECK(linux_find_cap(m, PCI_CAP_ID_MSI, &reads) == 0x50);                      /* Linux: 마스크하고 찾는다 */
    CHECK(strict_walk(m, &n) == CW_ERR_ALIGN);                                     /* 검증 도구: 하드웨어 버그로 보고 */
    CHECK(linux_find_cap(qn, PCI_CAP_ID_EXP, &reads) == 0x80 && linux_find_cap(qr, PCI_CAP_ID_EXP, &reads) == 0x54);

    /* 연습 2 */
    memcpy(m, ssd, 4096); memset(m + 0x100, 0, 0xF00);
    w32(m, 0x100, ext_hdr(PCI_EXT_CAP_ID_VNDR, 1, 0x140));
    w32(m, 0x140, ext_hdr(PCI_EXT_CAP_ID_ERR, 2, 0x180));
    w32(m, 0x180, ext_hdr(PCI_EXT_CAP_ID_VNDR, 1, 0x000));
    unsigned v1 = linux_find_ext_cap(m, 0, PCI_EXT_CAP_ID_VNDR, &reads);
    unsigned v2 = linux_find_ext_cap(m, v1, PCI_EXT_CAP_ID_VNDR, &reads);
    unsigned v3 = linux_find_ext_cap(m, v2, PCI_EXT_CAP_ID_VNDR, &reads);
    CHECK(v1 == 0x100 && v2 == 0x180 && v3 == 0);
    CHECK(linux_find_ext_cap(ssd, 0, PCI_EXT_CAP_ID_PL_16GT, &reads) == 0x1A0 && reads == 6);
    memcpy(m, ssd, 4096); w32(m, 0x170, ext_hdr(PCI_EXT_CAP_ID_SECPCI, 1, 0x100));  /* 루프 */
    CHECK(linux_find_ext_cap(m, 0, 0x0010, &reads) == 0 && reads == 480);
    printf("practice 2: ext loop, absent ID -> Linux reads=%d\n", reads);
    memcpy(m, ssd, 4096); w32(m, 0x148, ext_hdr(PCI_EXT_CAP_ID_LTR, 1, 0x152));      /* next 하위 비트 */
    CHECK(linux_find_ext_cap(m, 0, PCI_EXT_CAP_ID_L1SS, &reads) == 0x150);           /* & 0xffc 로 0x150 */
    CHECK(linux_find_ext_cap(qn, 0, PCI_EXT_CAP_ID_ERR, &reads) == 0 && reads == 1); /* 0x100 = 0 → 바로 끝 */
    CHECK(linux_find_ext_cap(qr, 0, 0x000D, &reads) == 0x148);

    /* 연습 3 */
    char line[256];
    uint8_t rp[256], dsp[256], usp[256];
    build_port(rp, 4, 0x01380C44, 0x3024);                                         /* 루트 포트: Gen4 x2 로 학습 */
    const uint8_t *chain1[] = { ssd, rp };
    const char *names1[] = { "0000:01:00.0", "0000:00:1c.0" };
    bw_line(chain1, names1, 2, false, line, sizeof line);
    printf("practice 3a: %s\n", line);
    CHECK(strcmp(line, "31.506 Gb/s available PCIe bandwidth, limited by 16.0 GT/s PCIe x2 link at 0000:00:1c.0 "
                       "(capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)") == 0);
    memcpy(m, ssd, 4096); w16(m, 0x82, 0x1044);                                   /* SSD 링크는 Gen4 x4 정상 */
    build_port(dsp, 6, 0x00300844, 0x2044);                                        /* 스위치 DSP: Gen4 x4 */
    build_port(usp, 5, 0x00000844, 0x0043);                                        /* 스위치 USP: Gen3 x4 로 학습 */
    build_port(rp, 4, 0x00380C43, 0x3043);                                         /* 루트 포트: Gen3 x4 */
    const uint8_t *chain2[] = { m, dsp, usp, rp };
    const char *names2[] = { "0000:03:00.0", "0000:02:00.0", "0000:01:00.0", "0000:00:01.0" };
    bw_line(chain2, names2, 4, false, line, sizeof line);
    printf("practice 3b: %s\n", line);
    CHECK(strcmp(line, "31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at 0000:00:01.0 "
                       "(capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)") == 0);
    const uint8_t *chain3[] = { qn, qr };
    const char *names3[] = { "0000:01:00.0", "0000:00:02.0" };
    bw_line(chain3, names3, 2, false, line, sizeof line);
    CHECK(line[0] == '\0');                                                         /* 제한 없음 → 출력 없음 */
    bw_line(chain3, names3, 2, true, line, sizeof line);
    printf("practice 3c: %s\n", line);
    CHECK(strcmp(line, "2.000 Gb/s available PCIe bandwidth (2.5 GT/s PCIe x1 link)") == 0);
    CHECK(mbps_enc(3) == 7876 && mbps_enc(4) == 15753 && mbps_enc(5) == 31507);

    /* 연습 4 */
    irq_caps q = irq_decode(ssd);
    CHECK(q.msi && !q.msi_en && q.msi64 && q.msi_mask && q.msi_cap_vec == 1 && q.msi_en_vec == 1);
    CHECK(q.msix && q.msix_en && q.msix_vec == 32 && q.tbl_bir == 0 && q.tbl_off == 0x2000 && q.pba_off == 0x3000 && !q.conflict);
    q = irq_decode(qn);
    CHECK(!q.msi && q.msix && !q.msix_en && q.msix_vec == 65 && q.tbl_off == 0x2000 && q.pba_off == 0x3000);
    q = irq_decode(qr);
    CHECK(q.msix && q.msix_en && q.msix_vec == 1 && q.tbl_off == 0 && q.pba_bir == 0 && q.pba_off == 0x800);
    memcpy(m, ssd, 4096); w16(m, 0x52, 0x0181);                                    /* MSI 도 켬 */
    CHECK(irq_decode(m).conflict);
    memcpy(m, ssd, 4096); w16(m, 0x52, 0x018B);                                    /* MMC=5 → 32개 가능 */
    CHECK(irq_decode(m).msi_cap_vec == 32);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
