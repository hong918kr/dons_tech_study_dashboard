/* PCIE-B08 추가 연습 — 한 번에 검증
 *
 * 연습 1: ECAM 물리 주소 → (seg, bus, dev, fn, reg) 역변환 — dmesg/devmem 주소를 읽는 법
 * 연습 2: CF8/CFC 는 두 단계라 경쟁(race)이 있다 — outl 과 inl 사이 인터럽트 → 엉뚱한 레지스터. 락(irq save)으로 고치기
 * 연습 3: AMD 식 CF8 확장(비트 27:24 = reg[11:8]) + 확장 영역 aliasing 판별 (Linux pci_cfg_space_size_ext 흉내)
 * 연습 4: 32비트 쓰기만 되는 컨트롤러 — 16비트 Command 쓰기를 RMW 로 흉내 낼 때 Status W1C 를 지키는 법
 * 연습 5: ECAM 창 크기 ↔ 버스 수 (Linux pci_ecam_create 처럼 창이 작으면 bus-range 를 줄인다)
 *
 * mock 과 드라이버 함수는 solution.c 의 축약판 (PCIE-B08 모델 축약판). 상수는 Linux pci_regs.h / pci-ecam.h / drivers/pci/pci.h 기준.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PCI_VENDOR_ID            0x00
#define PCI_COMMAND              0x04
#define  PCI_COMMAND_MEMORY      0x2
#define  PCI_COMMAND_MASTER      0x4
#define PCI_STATUS               0x06
#define  PCI_STATUS_REC_MASTER_ABORT 0x2000
#define  PCI_STATUS_DETECTED_PARITY  0x8000
#define PCI_CFG_SPACE_SIZE       256
#define PCI_CFG_SPACE_EXP_SIZE   4096
#define PCIE_ECAM_BUS_SHIFT      20
#define PCIE_ECAM_DEVFN_SHIFT    12
#define PCI_CONF1_ENABLE         0x80000000u
#define PCI_CONF1_REG_MASK       0xfc
#define PCI_CONF_ADDR_PORT       0xCF8
#define PCI_CONF_DATA_PORT       0xCFC

enum { CFG_OK = 0, CFG_ERR_BDF = -1, CFG_ERR_REG = -2, CFG_ERR_ALIGN = -3, CFG_ERR_WIDTH = -4, ECAM_ERR_SIZE = -6 };
static uint32_t ones(unsigned w) { return w == 1 ? 0xFFu : w == 2 ? 0xFFFFu : 0xFFFFFFFFu; }

/* ---------------- mock (축약판): 버스 0..1, 함수 4개 ---------------- */
#define MOCK_NBUS 2u
static uint8_t  g_cfg[MOCK_NBUS][32][8][PCI_CFG_SPACE_EXP_SIZE];
static bool     g_present[MOCK_NBUS][32][8];
static uint8_t  g_wmask[PCI_CFG_SPACE_EXP_SIZE], g_w1c[PCI_CFG_SPACE_EXP_SIZE];
static bool     g_cf8_ext;            /* 칩셋이 CF8 비트 27:24 를 reg[11:8] 로 해석하는가 */
static bool     g_only32;             /* 이 컨트롤러는 32비트 config 쓰기만 지원 (연습 4) */
static uint32_t g_cf8_latch, g_hw_bad;
static bool     g_irq_masked, g_irq_pending;
static void   (*g_isr)(void);

static uint32_t hw_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned w) {
    if ((reg & (w - 1)) || reg + w > PCI_CFG_SPACE_EXP_SIZE) { g_hw_bad++; return ones(w); }
    if (bus >= MOCK_NBUS || dev > 31 || fn > 7 || !g_present[bus][dev][fn]) return ones(w);
    uint32_t v = 0;
    for (unsigned i = 0; i < w; i++) v |= (uint32_t)g_cfg[bus][dev][fn][reg + i] << (8 * i);
    return v;
}
static void hw_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned w, uint32_t val) {
    if ((reg & (w - 1)) || reg + w > PCI_CFG_SPACE_EXP_SIZE || (g_only32 && w != 4)) { g_hw_bad++; return; }
    if (bus >= MOCK_NBUS || dev > 31 || fn > 7 || !g_present[bus][dev][fn]) return;
    for (unsigned i = 0; i < w; i++) {
        unsigned r = reg + i; uint8_t nb = (uint8_t)(val >> (8 * i)), *p = &g_cfg[bus][dev][fn][r];
        *p = (uint8_t)((*p & ~g_wmask[r]) | (nb & g_wmask[r]));
        *p = (uint8_t)(*p & ~(nb & g_w1c[r]));
    }
}
static uint32_t mmio_read(uint32_t off, unsigned w) {
    if (off >= (MOCK_NBUS << 20)) { g_hw_bad++; return ones(w); }
    return hw_read(off >> 20, (off >> 15) & 31u, (off >> 12) & 7u, off & 0xFFFu, w);
}
static void mmio_write(uint32_t off, unsigned w, uint32_t v) {
    if (off >= (MOCK_NBUS << 20)) { g_hw_bad++; return; }
    hw_write(off >> 20, (off >> 15) & 31u, (off >> 12) & 7u, off & 0xFFFu, w, v);
}
static unsigned latch_reg(uint32_t l) { return (l & PCI_CONF1_REG_MASK) | (g_cf8_ext ? ((l >> 24) & 0xFu) << 8 : 0u); }
static void io_out(uint16_t port, unsigned w, uint32_t v) {
    if (port == PCI_CONF_ADDR_PORT && w == 4) {
        g_cf8_latch = v;
        if (g_irq_pending && !g_irq_masked && g_isr) { g_irq_pending = false; g_isr(); }   /* 여기서 인터럽트가 끼어든다 */
        return;
    }
    if (port >= PCI_CONF_DATA_PORT && port <= PCI_CONF_DATA_PORT + 3 && (g_cf8_latch & PCI_CONF1_ENABLE))
        hw_write((g_cf8_latch >> 16) & 0xFFu, (g_cf8_latch >> 11) & 31u, (g_cf8_latch >> 8) & 7u,
                 latch_reg(g_cf8_latch) + (port - PCI_CONF_DATA_PORT), w, v);
}
static uint32_t io_in(uint16_t port, unsigned w) {
    if (port >= PCI_CONF_DATA_PORT && port <= PCI_CONF_DATA_PORT + 3 && (g_cf8_latch & PCI_CONF1_ENABLE))
        return hw_read((g_cf8_latch >> 16) & 0xFFu, (g_cf8_latch >> 11) & 31u, (g_cf8_latch >> 8) & 7u,
                       latch_reg(g_cf8_latch) + (port - PCI_CONF_DATA_PORT), w);
    return ones(w);
}
static void put32(unsigned b, unsigned d, unsigned f, unsigned r, uint32_t v) {
    for (int i = 0; i < 4; i++) g_cfg[b][d][f][r + (unsigned)i] = (uint8_t)(v >> (8 * i));
}
static void mock_reset(void) {
    memset(g_cfg, 0, sizeof g_cfg); memset(g_present, 0, sizeof g_present);
    memset(g_wmask, 0, sizeof g_wmask); memset(g_w1c, 0, sizeof g_w1c);
    g_wmask[PCI_COMMAND] = 0x47; g_wmask[PCI_COMMAND + 1] = 0x05; g_w1c[PCI_STATUS + 1] = 0xF9;
    for (unsigned r = 0x40; r < PCI_CFG_SPACE_EXP_SIZE; r++) g_wmask[r] = 0xFF;
    g_cf8_ext = g_only32 = g_irq_masked = g_irq_pending = false; g_isr = NULL; g_cf8_latch = 0; g_hw_bad = 0;
    /* 00:00.0 호스트 브리지, 00:06.0 virtio-blk (VM 실캡처의 앞부분), 00:1f.0 가상, 01:00.0 가상 NVMe (0x100 = AER 헤더) */
    put32(0, 0x00, 0, 0x00, 0x1A05106Bu); put32(0, 0x00, 0, 0x08, 0x06000000u); g_present[0][0x00][0] = true;
    put32(0, 0x06, 0, 0x00, 0x10421AF4u); put32(0, 0x06, 0, 0x04, 0x00100416u); put32(0, 0x06, 0, 0x08, 0x01800001u);
    put32(0, 0x06, 0, 0x10, 0x80020004u); put32(0, 0x06, 0, 0x3c, 0x000001FFu); g_present[0][0x06][0] = true;
    put32(0, 0x1f, 0, 0x00, 0xABC01234u); put32(0, 0x1f, 0, 0x0c, 0x00800000u); g_present[0][0x1f][0] = true;
    put32(1, 0x00, 0, 0x00, 0x00101B36u); put32(1, 0x00, 0, 0x08, 0x01080202u); put32(1, 0x00, 0, 0x100, 0x00020001u);
    g_present[1][0x00][0] = true;
}

/* ---------------- 드라이버 (solution.c 축약판) ---------------- */
typedef struct { uint64_t phys; uint16_t seg; uint8_t bus_start, bus_end; } ecam_win;
static uint32_t ecam_offset(unsigned b, unsigned d, unsigned f, unsigned r) {
    return ((uint32_t)b << PCIE_ECAM_BUS_SHIFT) | ((uint32_t)d << 15) | ((uint32_t)f << PCIE_ECAM_DEVFN_SHIFT) | r;
}
static int cfg_check(unsigned d, unsigned f, unsigned r, unsigned w, unsigned limit) {
    if (w != 1 && w != 2 && w != 4) return CFG_ERR_WIDTH;
    if (d > 31 || f > 7) return CFG_ERR_BDF;
    if (r >= limit) return CFG_ERR_REG;
    if (r & (w - 1)) return CFG_ERR_ALIGN;
    return CFG_OK;
}
static int ecam_read(const ecam_win *wn, unsigned b, unsigned d, unsigned f, unsigned r, unsigned w, uint32_t *v) {
    int rc = cfg_check(d, f, r, w, PCI_CFG_SPACE_EXP_SIZE);
    if (rc == CFG_OK && (b < wn->bus_start || b > wn->bus_end)) rc = CFG_ERR_BDF;
    if (rc) { *v = ones(w); return rc; }
    *v = mmio_read(ecam_offset(b - wn->bus_start, d, f, r), w);
    return CFG_OK;
}
static uint32_t conf1_address(unsigned b, unsigned d, unsigned f, unsigned r) {
    return PCI_CONF1_ENABLE | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (r & PCI_CONF1_REG_MASK);
}
/* 락 없는 CF8 읽기 (solution.c 의 conf1_read 와 같다) */
static int conf1_read(unsigned b, unsigned d, unsigned f, unsigned r, unsigned w, uint32_t *v) {
    int rc = cfg_check(d, f, r, w, PCI_CFG_SPACE_SIZE);
    if (rc == CFG_OK && b > 255) rc = CFG_ERR_BDF;
    if (rc) { *v = ones(w); return rc; }
    io_out(PCI_CONF_ADDR_PORT, 4, conf1_address(b, d, f, r));
    *v = io_in((uint16_t)(PCI_CONF_DATA_PORT + (r & 3u)), w);
    return CFG_OK;
}
static const ecam_win MOCK_WIN = { 0x40000000u, 0, 0, MOCK_NBUS - 1 };

/* ======================= 연습 1: 물리 주소 → BDF ======================= */
typedef struct { uint16_t seg; uint8_t bus, dev, fn; uint16_t reg; } ecam_loc;
static int ecam_decode(const ecam_win *w, uint64_t phys, ecam_loc *out) {
    uint64_t span = (uint64_t)(w->bus_end - w->bus_start + 1u) << PCIE_ECAM_BUS_SHIFT;
    if (phys < w->phys || phys - w->phys >= span) return CFG_ERR_BDF;
    uint32_t off = (uint32_t)(phys - w->phys);
    out->seg = w->seg;
    out->bus = (uint8_t)(w->bus_start + (off >> 20));
    out->dev = (uint8_t)((off >> 15) & 31u);
    out->fn  = (uint8_t)((off >> 12) & 7u);
    out->reg = (uint16_t)(off & 0xFFFu);
    return CFG_OK;
}

/* ======================= 연습 2: CF8/CFC 경쟁 ======================= */
static uint32_t g_isr_val;
static void isr_reads_other_device(void) { conf1_read(0, 0x1f, 0, PCI_VENDOR_ID, 4, &g_isr_val); }
static void irq_save(bool *flags)   { *flags = g_irq_masked; g_irq_masked = true; }
static void irq_restore(bool flags) {
    g_irq_masked = flags;
    if (!g_irq_masked && g_irq_pending && g_isr) { g_irq_pending = false; g_isr(); }   /* 미뤄 둔 인터럽트 */
}
/* Linux arch/x86/pci/direct.c 처럼: raw_spin_lock_irqsave(&pci_config_lock) 로 두 단계를 한 덩어리로 */
static int conf1_read_locked(unsigned b, unsigned d, unsigned f, unsigned r, unsigned w, uint32_t *v) {
    bool flags; int rc;
    irq_save(&flags);
    rc = conf1_read(b, d, f, r, w, v);
    irq_restore(flags);
    return rc;
}

/* ======================= 연습 3: AMD 식 CF8 확장 + aliasing 판별 ======================= */
/* Linux arch/x86/pci/direct.c 의 PCI_CONF1_ADDRESS: (reg & 0xF00) << 16 을 비트 27:24 에 넣는다 */
static uint32_t conf1_ext_address(unsigned b, unsigned d, unsigned f, unsigned r) {
    return conf1_address(b, d, f, r) | (((uint32_t)r & 0xF00u) << 16);
}
static int conf1_ext_read32(unsigned b, unsigned d, unsigned f, unsigned r, uint32_t *v) {
    int rc = cfg_check(d, f, r, 4, PCI_CFG_SPACE_EXP_SIZE);
    if (rc == CFG_OK && b > 255) rc = CFG_ERR_BDF;
    if (rc) { *v = ones(4); return rc; }
    io_out(PCI_CONF_ADDR_PORT, 4, conf1_ext_address(b, d, f, r));
    *v = io_in(PCI_CONF_DATA_PORT, 4);
    return CFG_OK;
}
typedef int (*rd32_fn)(unsigned b, unsigned d, unsigned f, unsigned r, uint32_t *v);
static int ecam_rd32(unsigned b, unsigned d, unsigned f, unsigned r, uint32_t *v) { return ecam_read(&MOCK_WIN, b, d, f, r, 4, v); }
/* Linux pci_cfg_space_size_ext + pci_ext_cfg_is_aliased: 0x100 이 all-1s 거나, 0x100/0x200/…/0xF00 이 전부 헤더(0x00)와
 * 같으면 확장 영역이 "없다"(256). 아니면 4096. (커널은 호스트 브리지·PCIe·PCI-X 장치일 때만 이 검사를 한다.) */
static int cfg_space_size_ext(rd32_fn rd, unsigned b, unsigned d, unsigned f) {
    uint32_t v, hdr, t;
    if (rd(b, d, f, PCI_CFG_SPACE_SIZE, &v) != CFG_OK || v == 0xFFFFFFFFu) return PCI_CFG_SPACE_SIZE;
    if (rd(b, d, f, PCI_VENDOR_ID, &hdr) != CFG_OK) return PCI_CFG_SPACE_SIZE;
    for (unsigned pos = PCI_CFG_SPACE_SIZE; pos < PCI_CFG_SPACE_EXP_SIZE; pos += PCI_CFG_SPACE_SIZE) {
        if (rd(b, d, f, pos, &t) != CFG_OK || t != hdr) return PCI_CFG_SPACE_EXP_SIZE;   /* 하나라도 다르면 진짜 */
    }
    return PCI_CFG_SPACE_SIZE;                                                            /* 전부 헤더 = aliasing */
}

/* ======================= 연습 4: 32비트 쓰기 전용 컨트롤러 ======================= */
/* 나쁜 예 (Linux pci_generic_config_write32 와 같은 RMW): 옆 바이트의 W1C 비트를 읽은 그대로(1) 써서 지워 버린다 */
static void write16_via32_naive(unsigned b, unsigned d, unsigned f, unsigned r, uint16_t val) {
    uint32_t off = ecam_offset(b, d, f, r & ~3u), sh = (r & 2u) * 8u;
    uint32_t t = mmio_read(off, 4);
    t = (t & ~(0xFFFFu << sh)) | ((uint32_t)val << sh);
    mmio_write(off, 4, t);
}
/* 좋은 예: 건드리지 않을 바이트의 W1C 비트는 0 으로 써서 보존한다. w1c_mask32 = 이 dword 의 W1C 비트 (알아야만 가능) */
static void write16_via32_safe(unsigned b, unsigned d, unsigned f, unsigned r, uint16_t val, uint32_t w1c_mask32) {
    uint32_t off = ecam_offset(b, d, f, r & ~3u), sh = (r & 2u) * 8u, mine = 0xFFFFu << sh;
    uint32_t t = mmio_read(off, 4);
    t = (t & ~mine & ~w1c_mask32) | ((uint32_t)val << sh);
    mmio_write(off, 4, t);
}

/* ======================= 연습 5: 창 크기 ↔ 버스 수 ======================= */
/* Linux drivers/pci/ecam.c pci_ecam_create: bus_range_max = size >> 20, bus-range 가 더 크면 잘라서 경고 */
static int ecam_fit(uint64_t size, unsigned lo, unsigned hi, unsigned *new_hi) {
    if (lo > hi || hi > 255) return CFG_ERR_BDF;
    uint64_t maxb = size >> PCIE_ECAM_BUS_SHIFT;
    if (maxb == 0) return ECAM_ERR_SIZE;
    unsigned want = hi - lo + 1u;
    *new_hi = (want > maxb) ? lo + (unsigned)maxb - 1u : hi;
    return (want > maxb) ? 1 : 0;   /* 1 = 잘렸다 */
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(void) {
    mock_reset();

    /* 연습 1 */
    {
        const ecam_win vm = { 0x40000000u, 0, 0x00, 0xff };     /* VM MCFG 실캡처 */
        ecam_loc L;
        CHECK(ecam_decode(&vm, 0x40030010u, &L) == CFG_OK && L.bus == 0 && L.dev == 6 && L.fn == 0 && L.reg == 0x10);
        CHECK(ecam_decode(&vm, 0x4003003Cu, &L) == CFG_OK && L.dev == 6 && L.reg == 0x3c);
        CHECK(ecam_decode(&vm, 0x4FFFFFFFu, &L) == CFG_OK && L.bus == 0xff && L.dev == 31 && L.fn == 7 && L.reg == 0xfff);
        CHECK(ecam_decode(&vm, 0x50000000u, &L) == CFG_ERR_BDF && ecam_decode(&vm, 0x3FFFFFFFu, &L) == CFG_ERR_BDF);
        const ecam_win hi = { 0x8001000000ull, 1, 0x10, 0x1f };
        CHECK(ecam_decode(&hi, 0x8001000000ull + 0x1FB00Eu, &L) == CFG_OK && L.seg == 1 && L.bus == 0x11 &&
              L.dev == 0x1f && L.fn == 3 && L.reg == 0x0e);
        uint32_t s = 0x0ECA0001u; int bad = 0;
        for (int i = 0; i < 100000; i++) {
            unsigned b = xs32(&s) % 256u, d = xs32(&s) % 32u, f = xs32(&s) % 8u, r = xs32(&s) % 4096u;
            if (ecam_decode(&vm, vm.phys + ecam_offset(b, d, f, r), &L) != CFG_OK ||
                L.bus != b || L.dev != d || L.fn != f || L.reg != r) bad++;
        }
        CHECK(bad == 0);
        static const uint64_t probes[3] = { 0x40030010u, 0x400F8000u, 0x4FFFFFFFu };
        for (int i = 0; i < 3; i++)
            if (ecam_decode(&vm, probes[i], &L) == CFG_OK)
                printf("practice 1: 0x%llx -> %04x:%02x:%02x.%u reg 0x%03x\n", (unsigned long long)probes[i],
                       (unsigned)L.seg, (unsigned)L.bus, (unsigned)L.dev, (unsigned)L.fn, (unsigned)L.reg);
    }

    /* 연습 2 */
    {
        uint32_t v = 0;
        g_isr = isr_reads_other_device; g_irq_pending = true; g_isr_val = 0;
        conf1_read(0, 6, 0, PCI_VENDOR_ID, 4, &v);
        printf("practice 2: no lock  -> main read 0x%08X (wanted 0x10421AF4), ISR read 0x%08X\n", (unsigned)v, (unsigned)g_isr_val);
        CHECK(v == 0xABC01234u && g_isr_val == 0xABC01234u);        /* 메인이 ISR 의 레지스터를 읽었다 */
        g_irq_pending = true; g_isr_val = 0; v = 0;
        conf1_read_locked(0, 6, 0, PCI_VENDOR_ID, 4, &v);
        printf("practice 2: locked   -> main read 0x%08X, ISR read 0x%08X (after unlock)\n", (unsigned)v, (unsigned)g_isr_val);
        CHECK(v == 0x10421AF4u && g_isr_val == 0xABC01234u && !g_irq_pending);
        g_irq_pending = true; v = 0;
        ecam_read(&MOCK_WIN, 0, 6, 0, PCI_VENDOR_ID, 4, &v);         /* ECAM: 한 번의 load — 끼어들 틈이 없다 */
        CHECK(v == 0x10421AF4u && g_irq_pending);
        g_irq_pending = false; g_isr = NULL;
    }

    /* 연습 3 */
    {
        uint32_t v = 0;
        CHECK(conf1_ext_address(1, 0, 0, 0x100) == 0x81010000u && conf1_ext_address(0, 6, 0, 0xF3C) == 0x8F00303Cu);
        g_cf8_ext = true;
        CHECK(conf1_ext_read32(1, 0, 0, 0x100, &v) == CFG_OK && v == 0x00020001u);
        g_cf8_ext = false;                                            /* 칩셋이 27:24 를 무시하면 0x100 → 0x00 으로 접힌다 */
        CHECK(conf1_ext_read32(1, 0, 0, 0x100, &v) == CFG_OK && v == 0x00101B36u);
        int s_alias = cfg_space_size_ext(conf1_ext_read32, 1, 0, 0);
        int s_ecam  = cfg_space_size_ext(ecam_rd32, 1, 0, 0);
        g_cf8_ext = true;
        int s_ext   = cfg_space_size_ext(conf1_ext_read32, 1, 0, 0);
        g_cf8_ext = false;
        int s_host  = cfg_space_size_ext(ecam_rd32, 0, 0, 0);        /* 0x100 = 0 → 진짜 확장 영역 (VM: config 4096B) */
        printf("practice 3: cfg size 01:00.0 via CF8(no ext)=%d, CF8(ext)=%d, ECAM=%d; host bridge 00:00.0 via ECAM=%d\n",
               s_alias, s_ext, s_ecam, s_host);
        CHECK(s_alias == 256 && s_ext == 4096 && s_ecam == 4096 && s_host == 4096);
        CHECK(cfg_space_size_ext(ecam_rd32, 0, 2, 0) == 256);         /* 없는 장치: 0x100 이 all-1s */
    }

    /* 연습 4 */
    {
        g_only32 = true;
        uint32_t bad0 = g_hw_bad;
        mmio_write(ecam_offset(0, 6, 0, PCI_COMMAND), 2, 0x0412);    /* 16비트 쓰기는 이 컨트롤러에서 안 된다 */
        CHECK(g_hw_bad == bad0 + 1 && mmio_read(ecam_offset(0, 6, 0, PCI_COMMAND), 2) == 0x0416u);
        g_cfg[0][6][0][PCI_STATUS + 1] |= 0xA0;                       /* Master Abort + Detected Parity 발생 */
        write16_via32_naive(0, 6, 0, PCI_COMMAND, 0x0412);
        uint32_t st_naive = mmio_read(ecam_offset(0, 6, 0, PCI_STATUS), 2);
        g_cfg[0][6][0][PCI_STATUS + 1] |= 0xA0;
        write16_via32_safe(0, 6, 0, PCI_COMMAND, 0x0416, 0xF9000000u);
        uint32_t st_safe = mmio_read(ecam_offset(0, 6, 0, PCI_STATUS), 2);
        uint32_t cmd = mmio_read(ecam_offset(0, 6, 0, PCI_COMMAND), 2);
        printf("practice 4: Status after naive RMW 0x%04X, after safe RMW 0x%04X, Command 0x%04X\n",
               (unsigned)st_naive, (unsigned)st_safe, (unsigned)cmd);
        CHECK(st_naive == 0x0010u && st_safe == 0xA010u && cmd == 0x0416u);
        write16_via32_safe(0, 6, 0, PCI_STATUS, PCI_STATUS_REC_MASTER_ABORT, 0xF9000000u);   /* Status 만 지우기: Command 보존 */
        CHECK(mmio_read(ecam_offset(0, 6, 0, PCI_STATUS), 2) == 0x8010u && mmio_read(ecam_offset(0, 6, 0, PCI_COMMAND), 2) == 0x0416u);
        g_only32 = false;
    }

    /* 연습 5 */
    {
        unsigned nh = 0;
        CHECK(ecam_fit(0x10000000u, 0x00, 0xff, &nh) == 0 && nh == 0xff);    /* VM (Apple VZ) 256MB, 버스 00-ff */
        CHECK(ecam_fit(0x01000000u, 0x00, 0xff, &nh) == 1 && nh == 0x0f);    /* 16MB → 버스 16개로 잘림 */
        CHECK(ecam_fit(0x00200000u, 0x40, 0x7f, &nh) == 1 && nh == 0x41);    /* 2MB, 첫 버스 0x40 → 0x40-0x41 */
        CHECK(ecam_fit(0x00080000u, 0, 0, &nh) == ECAM_ERR_SIZE && ecam_fit(0x100000u, 3, 2, &nh) == CFG_ERR_BDF);
        CHECK((32u * 8u * 4096u) == (1u << 20) && (256ull << 20) == 0x10000000ull);   /* 버스 1개 = 1MB, 256개 = 256MB */
    }

    CHECK(g_hw_bad == 1);   /* 연습 4 의 일부러 한 16비트 쓰기 하나뿐 */
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
