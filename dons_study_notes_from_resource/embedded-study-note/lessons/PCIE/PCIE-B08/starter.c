/* PCIE-B08 ECAM 주소 계산과 config 접근 방식
 *
 * Part A: ECAM(MMCONFIG) — ecam_offset(), cfg_check(), ecam_read()/ecam_write() 8/16/32비트.
 *         mock ECAM 창 = 버스 2개 × 32 dev × 8 fn × 4KB = 2MB (정적 배열). 없는 함수는 all-1s.
 * Part B: legacy CF8/CFC — conf1_address(), conf1_read()/conf1_write() (byte lane = 0xCFC + (reg & 3)).
 *         CF8 로는 0x00–0xFF 만 닿는다. 정렬·범위 오류, RO 쓰기 무시, Status W1C 함정, bus_start ≠ 0 창.
 * Part C: 펌웨어/드라이버 현실 — ACPI MCFG 파싱(Lima VM 실캡처 60바이트), DT reg → ECAM 창(QEMU virt),
 *         물리 주소 계산(VM 에서 devmem 으로 읽은 주소와 대조), RRS(옛 이름 CRS) 재시도 + multi-function 규칙 버스 스캔.
 *
 * 상수 이름·값: Linux include/uapi/linux/pci_regs.h, include/linux/pci-ecam.h, drivers/pci/pci.h (6.18) 기준.
 * mock 의 쓰기 규칙(어느 바이트가 RW/RO/W1C 인가)과 장치 목록은 이 레슨이 정한 것이다 — 아래 mock_reset() 참고.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- pci_regs.h 와 같은 이름·값 (Mac 에서 헤더 없이 빌드하려고 직접 정의) ---------- */
#define PCI_VENDOR_ID            0x00
#define PCI_DEVICE_ID            0x02
#define PCI_COMMAND              0x04
#define  PCI_COMMAND_IO          0x1
#define  PCI_COMMAND_MEMORY      0x2
#define  PCI_COMMAND_MASTER      0x4
#define  PCI_COMMAND_PARITY      0x40
#define  PCI_COMMAND_SERR        0x100
#define  PCI_COMMAND_INTX_DISABLE 0x400
#define PCI_STATUS               0x06
#define  PCI_STATUS_CAP_LIST     0x10
#define  PCI_STATUS_PARITY       0x100
#define  PCI_STATUS_SIG_TARGET_ABORT 0x800
#define  PCI_STATUS_REC_TARGET_ABORT 0x1000
#define  PCI_STATUS_REC_MASTER_ABORT 0x2000
#define  PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define  PCI_STATUS_DETECTED_PARITY  0x8000
#define PCI_CLASS_REVISION       0x08
#define PCI_CACHE_LINE_SIZE      0x0c
#define PCI_HEADER_TYPE          0x0e
#define  PCI_HEADER_TYPE_MASK    0x7f
#define  PCI_HEADER_TYPE_MFD     0x80
#define PCI_CAPABILITY_LIST      0x34
#define PCI_INTERRUPT_LINE       0x3c
#define PCI_INTERRUPT_PIN        0x3d
#define PCI_CFG_SPACE_SIZE       256
#define PCI_CFG_SPACE_EXP_SIZE   4096
#define PCI_VENDOR_ID_PCI_SIG    0x0001   /* include/linux/pci_ids.h — VID 0x0001 = RRS 완료 표시 */
/* include/linux/pci-ecam.h */
#define PCIE_ECAM_BUS_SHIFT      20
#define PCIE_ECAM_DEVFN_SHIFT    12
/* drivers/pci/pci.h — "CONF1" = CF8/CFC 방식 */
#define PCI_CONF1_ENABLE         0x80000000u
#define PCI_CONF1_BUS_SHIFT      16
#define PCI_CONF1_DEV_SHIFT      11
#define PCI_CONF1_FUNC_SHIFT     8
#define PCI_CONF1_REG_MASK       0xfc
#define PCI_CONF_ADDR_PORT       0xCF8
#define PCI_CONF_DATA_PORT       0xCFC

enum { CFG_OK = 0, CFG_ERR_BDF = -1, CFG_ERR_REG = -2, CFG_ERR_ALIGN = -3, CFG_ERR_WIDTH = -4,
       ECAM_ERR_FMT = -5, ECAM_ERR_SIZE = -6 };

static uint32_t ones(unsigned width) {            /* 폭만큼의 all-1s: 1→0xFF, 2→0xFFFF, 4(그 외)→0xFFFFFFFF */
    return width == 1 ? 0xFFu : width == 2 ? 0xFFFFu : 0xFFFFFFFFu;
}
static inline uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32le(const uint8_t *p) { return (uint32_t)rd16le(p) | ((uint32_t)rd16le(p + 2) << 16); }
static inline uint64_t rd64le(const uint8_t *p) { return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p + 4) << 32); }

/* ======================= mock 하드웨어 (주어짐) =======================
 * 루트 컴플렉스 하나: ECAM 창(MMIO) + x86 식 IO 포트 0xCF8/0xCFC 를 둘 다 가진다. 둘 다 같은 함수 저장소로 간다. */
#define MOCK_NBUS 2u
typedef struct { bool present; bool alias_fn0; uint32_t rrs_left; } mock_fn;
static uint8_t  g_cfg[MOCK_NBUS][32][8][PCI_CFG_SPACE_EXP_SIZE];   /* 2MB: 함수별 config "레지스터" */
static mock_fn  g_fn[MOCK_NBUS][32][8];
static uint8_t  g_wmask[PCI_CFG_SPACE_EXP_SIZE], g_w1c[PCI_CFG_SPACE_EXP_SIZE];
static unsigned g_bus_base;      /* 이 루트 컴플렉스 ECAM 창의 첫 버스 번호 (보통 0) */
static bool     g_rrs_sv;        /* Root Control 의 RRS Software Visibility Enable (PCI_EXP_RTCTL_RRS_SVE) */
static bool     g_cf8_ext;       /* CF8 비트 27:24 를 레지스터 11:8 로 해석하는가 (AMD 식 확장, 연습 3) */
static uint32_t g_hw_ops;        /* 하드웨어까지 내려간 config 접근 수 */
static uint32_t g_hw_bad;        /* 정렬 안 맞음 / 창 밖 / 잘못된 포트 — 드라이버가 미리 걸렀어야 한다 */
static uint32_t g_hw_stall;      /* SV 꺼짐에서 RC 가 하드웨어로 재시도한 횟수 (그동안 CPU 는 멈춘다) */
static uint32_t g_vid_reads;     /* reg 0 을 덮는 읽기 수 (RRS 재시도 세기용) */
static uint32_t g_cf8_latch;
static uint16_t g_last_port;

static bool hw_target(unsigned bus, unsigned dev, unsigned fn, unsigned *b, unsigned *f) {
    if (bus < g_bus_base || bus - g_bus_base >= MOCK_NBUS || dev > 31 || fn > 7) return false;
    *b = bus - g_bus_base; *f = fn;
    if (!g_fn[*b][dev][fn].present) {
        if (fn != 0 && g_fn[*b][dev][0].present && g_fn[*b][dev][0].alias_fn0) *f = 0;  /* 함수 번호 무시하는 버그 장치 */
        else return false;
    }
    return true;
}
static bool hw_bad_access(unsigned reg, unsigned width) {
    return (width != 1 && width != 2 && width != 4) || (reg & (width - 1)) || reg + width > PCI_CFG_SPACE_EXP_SIZE;
}
static uint32_t cfg_hw_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width) {
    unsigned b, f;
    g_hw_ops++;
    if (hw_bad_access(reg, width)) { g_hw_bad++; return ones(width); }
    if (!hw_target(bus, dev, fn, &b, &f)) return ones(width);   /* 받는 함수 없음(UR) → RC 가 all-1s 로 완료 */
    mock_fn *m = &g_fn[b][dev][f];
    if (reg == 0) g_vid_reads++;
    if (m->rrs_left) {                                          /* 장치가 아직 준비 안 됨 (예: SSD 펌웨어 로딩 중) */
        if (g_rrs_sv && reg == 0 && width >= 2) { m->rrs_left--; return width == 2 ? 0x0001u : 0xFFFF0001u; }
        g_hw_stall += m->rrs_left; m->rrs_left = 0;            /* SV 꺼짐 / VID 아닌 읽기: RC 가 알아서 재시도 */
    }
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v |= (uint32_t)g_cfg[b][dev][f][reg + i] << (8 * i);
    return v;
}
static void cfg_hw_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t val) {
    unsigned b, f;
    g_hw_ops++;
    if (hw_bad_access(reg, width)) { g_hw_bad++; return; }
    if (!hw_target(bus, dev, fn, &b, &f)) return;               /* 없는 함수로 가는 쓰기는 조용히 버려진다 */
    mock_fn *m = &g_fn[b][dev][f];
    if (m->rrs_left) { g_hw_stall += m->rrs_left; m->rrs_left = 0; }
    for (unsigned i = 0; i < width; i++) {
        unsigned r = reg + i;
        uint8_t nb = (uint8_t)(val >> (8 * i)), *p = &g_cfg[b][dev][f][r];
        *p = (uint8_t)((*p & ~g_wmask[r]) | (nb & g_wmask[r]));   /* RW 비트만 바뀐다 (RO 는 무시) */
        *p = (uint8_t)(*p & ~(nb & g_w1c[r]));                    /* W1C: 1 을 쓴 비트만 지워진다 */
    }
}
/* ECAM: 드라이버의 readb/readw/readl(ecam_base + off) 자리. RC 가 오프셋을 bus/dev/fn/reg 로 디코드한다. */
static uint32_t mmio_read(uint32_t off, unsigned width) {
    if (off >= (MOCK_NBUS << PCIE_ECAM_BUS_SHIFT)) { g_hw_ops++; g_hw_bad++; return ones(width); }  /* 창 밖 (실HW: 버스 에러) */
    return cfg_hw_read(g_bus_base + (off >> 20), (off >> 15) & 31u, (off >> 12) & 7u, off & 0xFFFu, width);
}
static void mmio_write(uint32_t off, unsigned width, uint32_t val) {
    if (off >= (MOCK_NBUS << PCIE_ECAM_BUS_SHIFT)) { g_hw_ops++; g_hw_bad++; return; }
    cfg_hw_write(g_bus_base + (off >> 20), (off >> 15) & 31u, (off >> 12) & 7u, off & 0xFFFu, width, val);
}
/* x86 IO 포트: outl(0xCF8) 로 주소를 래치, 0xCFC..0xCFF 로 데이터. 래치는 CPU 전체에 하나뿐인 전역 상태다. */
static unsigned conf1_reg_of(uint32_t latch) {
    return (latch & PCI_CONF1_REG_MASK) | (g_cf8_ext ? ((latch >> 24) & 0xFu) << 8 : 0u);
}
static void io_out(uint16_t port, unsigned width, uint32_t val) {
    g_last_port = port;
    if (port == PCI_CONF_ADDR_PORT && width == 4) { g_cf8_latch = val; return; }
    if (port >= PCI_CONF_DATA_PORT && port <= PCI_CONF_DATA_PORT + 3) {
        unsigned k = port - PCI_CONF_DATA_PORT;
        if ((width != 1 && width != 2 && width != 4) || (k & (width - 1))) { g_hw_bad++; return; }
        if (!(g_cf8_latch & PCI_CONF1_ENABLE)) return;          /* config 사이클 아님: 받는 장치 없음 */
        cfg_hw_write((g_cf8_latch >> 16) & 0xFFu, (g_cf8_latch >> 11) & 31u, (g_cf8_latch >> 8) & 7u,
                     conf1_reg_of(g_cf8_latch) + k, width, val);
        return;
    }
    g_hw_bad++;
}
static uint32_t io_in(uint16_t port, unsigned width) {
    g_last_port = port;
    if (port == PCI_CONF_ADDR_PORT && width == 4) return g_cf8_latch;
    if (port >= PCI_CONF_DATA_PORT && port <= PCI_CONF_DATA_PORT + 3) {
        unsigned k = port - PCI_CONF_DATA_PORT;
        if ((width != 1 && width != 2 && width != 4) || (k & (width - 1))) { g_hw_bad++; return ones(width); }
        if (!(g_cf8_latch & PCI_CONF1_ENABLE)) return ones(width);
        return cfg_hw_read((g_cf8_latch >> 16) & 0xFFu, (g_cf8_latch >> 11) & 31u, (g_cf8_latch >> 8) & 7u,
                           conf1_reg_of(g_cf8_latch) + k, width);
    }
    g_hw_bad++;
    return ones(width);
}

/* lspci -xxx 형식 텍스트("00: f4 1a ...")를 함수 하나의 config 로 싣는다 (파서 자체는 PCIE-B03 의 몫) */
static int load_lspci(unsigned b, unsigned d, unsigned f, const char *t) {
    int n = 0;
    while (*t) {
        unsigned off = 0, k = 0;
        while (*t == ' ' || *t == '\n') t++;
        if (!*t) break;
        for (; *t && *t != ':'; t++) {
            int h = (*t >= '0' && *t <= '9') ? *t - '0' : (*t >= 'a' && *t <= 'f') ? *t - 'a' + 10 : -1;
            if (h < 0) return -1;
            off = off * 16u + (unsigned)h;
        }
        if (*t != ':' || off > PCI_CFG_SPACE_EXP_SIZE - 16) return -1;
        t++;
        for (; k < 16; k++) {
            int hi, lo;
            while (*t == ' ') t++;
            hi = (*t >= '0' && *t <= '9') ? *t - '0' : (*t >= 'a' && *t <= 'f') ? *t - 'a' + 10 : -1;
            if (hi < 0) return -1;
            t++;
            lo = (*t >= '0' && *t <= '9') ? *t - '0' : (*t >= 'a' && *t <= 'f') ? *t - 'a' + 10 : -1;
            if (lo < 0) return -1;
            t++;
            g_cfg[b][d][f][off + k] = (uint8_t)(hi * 16 + lo);
            n++;
        }
    }
    return n;
}

/* Lima VM(Apple Virtualization.framework) 실캡처: sudo lspci -xxx -s 00:00.0 (0x40 이후는 0x3c=ff 외 전부 0이라 생략) */
static const char VM_HOSTBRIDGE[] =
    "00: 6b 10 05 1a 10 00 00 00 00 00 00 06 00 00 00 00\n"
    "30: 00 00 00 00 00 00 00 00 00 00 00 00 ff 00 00 00\n";
/* 같은 VM: sudo lspci -xxxx -s 00:06.0 (virtio-blk, 256B 장치 — 0x90 이후 전부 0) */
static const char VM_VIRTIO_BLK[] =
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 09 50 10 01 00 00 00 00 00 00 00 00 38 00 00 00\n"
    "50: 09 60 10 03 00 00 00 00 00 10 00 00 01 00 00 00\n"
    "60: 09 74 14 02 00 00 00 00 00 40 00 00 04 00 00 00\n"
    "70: 04 00 00 00 09 84 10 04 00 00 00 00 00 80 00 00\n"
    "80: 3c 00 00 00 11 00 01 80 02 00 00 00 22 00 00 00\n";
/* 가상 장치 (이 레슨이 만든 바이트) */
static const char FAKE_ROOTPORT[] =   /* 00:1c.0 Type 1 루트 포트 (QEMU pcie-root-port 와 같은 1b36:000c), sec=1 sub=1 */
    "00: 36 1b 0c 00 06 00 00 00 00 00 04 06 00 00 01 00\n"
    "10: 00 00 00 00 00 00 00 00 00 01 01 00 00 00 00 00\n";
static const char FAKE_MFD_F0[] =     /* 00:1f.0 multi-function 장치 fn0 (헤더 타입 0x80) */
    "00: 34 12 c0 ab 00 00 00 00 00 00 01 06 00 00 80 00\n";
static const char FAKE_MFD_F3[] =     /* 00:1f.3 같은 장치 fn3 (fn1, fn2 는 없음) */
    "00: 34 12 c3 ab 00 00 00 00 05 00 05 0c 00 00 80 00\n"
    "30: 00 00 00 00 00 00 00 00 00 00 00 00 ff 01 00 00\n";
static const char FAKE_NVME[] =       /* 01:00.0 NVMe SSD (QEMU nvme 와 같은 1b36:0010), 0x100 에 AER 확장 헤더 */
    "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 01 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "100: 01 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00\n";

static void mock_reset(void) {
    memset(g_cfg, 0, sizeof g_cfg); memset(g_fn, 0, sizeof g_fn);
    g_bus_base = 0; g_rrs_sv = true; g_cf8_ext = false;
    g_hw_ops = g_hw_bad = g_hw_stall = g_vid_reads = 0; g_cf8_latch = 0; g_last_port = 0;
    /* 쓰기 규칙 (모든 함수 공통, mock 정의): 기본 RO, 아래만 RW / W1C. BAR 쓰기 규칙은 PCIE-B05, Type 1 은 PCIE-B07. */
    memset(g_wmask, 0, sizeof g_wmask); memset(g_w1c, 0, sizeof g_w1c);
    g_wmask[PCI_COMMAND]     = (uint8_t)(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_PARITY);
    g_wmask[PCI_COMMAND + 1] = (uint8_t)((PCI_COMMAND_SERR | PCI_COMMAND_INTX_DISABLE) >> 8);
    g_w1c[PCI_STATUS + 1]    = (uint8_t)((PCI_STATUS_PARITY | PCI_STATUS_SIG_TARGET_ABORT | PCI_STATUS_REC_TARGET_ABORT |
                                          PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_SIG_SYSTEM_ERROR |
                                          PCI_STATUS_DETECTED_PARITY) >> 8);
    g_wmask[PCI_CACHE_LINE_SIZE] = 0xFF;
    g_wmask[PCI_INTERRUPT_LINE]  = 0xFF;
    for (unsigned r = 0x40; r < PCI_CFG_SPACE_EXP_SIZE; r++) g_wmask[r] = 0xFF;   /* 장치 고유 영역: RW 로 둔다 */
    load_lspci(0, 0x00, 0, VM_HOSTBRIDGE);  g_fn[0][0x00][0].present = true;
    load_lspci(0, 0x06, 0, VM_VIRTIO_BLK);  g_fn[0][0x06][0].present = true;
    load_lspci(0, 0x1c, 0, FAKE_ROOTPORT);  g_fn[0][0x1c][0].present = true;
    load_lspci(0, 0x1f, 0, FAKE_MFD_F0);    g_fn[0][0x1f][0].present = true;
    load_lspci(0, 0x1f, 3, FAKE_MFD_F3);    g_fn[0][0x1f][3].present = true;
    load_lspci(1, 0x00, 0, FAKE_NVME);      g_fn[1][0x00][0].present = true;
    g_fn[1][0x00][0].alias_fn0 = true;      /* 함수 번호를 디코드하지 않는 (버그 있는) 엔드포인트 흉내 */
}

/* ======================= 드라이버 쪽 (여기를 구현) ======================= */

typedef struct { uint64_t phys; uint16_t seg; uint8_t bus_start, bus_end; } ecam_win;   /* phys = bus_start 의 주소 */

/* A1: ECAM 오프셋. bus_rel = bus - 창의 bus_start. 인자 검사는 호출자 몫(cfg_check). */
static uint32_t ecam_offset(unsigned bus_rel, unsigned dev, unsigned fn, unsigned reg) {
    (void)bus_rel; (void)dev; (void)fn; (void)reg;
    return 0; /* TODO A1: bus_rel<<20 | dev<<15 | fn<<12 | reg */
}

/* A2: 공통 인자 검사. limit = 닿을 수 있는 config 크기 (ECAM 4096, CF8 256). 검사 순서: 폭 → BDF → 범위 → 정렬. */
static int cfg_check(unsigned dev, unsigned fn, unsigned reg, unsigned width, unsigned limit) {
    (void)dev; (void)fn; (void)reg; (void)width; (void)limit;
    return CFG_OK; /* TODO A2: 폭 {1,2,4} → dev ≤ 31, fn ≤ 7 → reg < limit → reg % width == 0 */
}

/* A3: ECAM 읽기. 오류면 하드웨어를 건드리지 않고 *val = all-1s (Linux PCI_SET_ERROR_RESPONSE 와 같은 약속). */
static int ecam_read(const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t *val) {
    (void)w; (void)bus; (void)dev; (void)fn; (void)reg;
    *val = ones(width);
    return CFG_ERR_BDF; /* TODO A3: cfg_check + 창 버스 범위 → 오류면 all-1s, 아니면 mmio_read(ecam_offset(bus - bus_start, ...)) */
}

/* A4: ECAM 쓰기. 오류면 아무것도 쓰지 않는다. */
static int ecam_write(const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t val) {
    (void)w; (void)bus; (void)dev; (void)fn; (void)reg; (void)width; (void)val;
    return CFG_ERR_BDF; /* TODO A4: ecam_read 와 같은 검사 → mmio_write */
}

/* B1: CF8 에 쓸 주소 값. bit31 enable, 23:16 bus, 15:11 dev, 10:8 fn, 7:2 reg (1:0 은 항상 0). */
static uint32_t conf1_address(unsigned bus, unsigned dev, unsigned fn, unsigned reg) {
    (void)bus; (void)dev; (void)fn; (void)reg;
    return 0; /* TODO B1: ENABLE | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC) */
}

/* B2: CF8/CFC 읽기. 0x00–0xFF 만. outl(CF8) → in{b,w,l}(0xCFC + (reg & 3)). */
static int conf1_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t *val) {
    (void)bus; (void)dev; (void)fn; (void)reg;
    *val = ones(width);
    return CFG_ERR_BDF; /* TODO B2: cfg_check(limit 256) + bus ≤ 255 → io_out(CF8, 4, addr) → io_in(CFC + (reg & 3), width) */
}

/* B3: CF8/CFC 쓰기. */
static int conf1_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t val) {
    (void)bus; (void)dev; (void)fn; (void)reg; (void)width; (void)val;
    return CFG_ERR_BDF; /* TODO B3: conf1_read 와 같은 검사 → io_out(CF8) → io_out(CFC + (reg & 3), width, val) */
}

/* C1: ACPI MCFG 파싱. 헤더 36B + 예약 8B, 그 뒤 16B 항목: u64 base(버스 0 기준!), u16 segment, u8 start, u8 end, u32 rsvd.
 * 검사: 길이, "MCFG", Length 필드, 항목 크기, 체크섬(전체 바이트 합 = 0), start ≤ end. 반환: 항목 수 또는 음수 오류.
 * out[i].phys = base + (start << 20) — 창의 첫 버스 주소로 정규화. */
static int mcfg_parse(const uint8_t *t, size_t len, ecam_win *out, int cap) {
    (void)t; (void)len; (void)out; (void)cap;
    return ECAM_ERR_FMT; /* TODO C1: 시그니처·Length·항목 크기·체크섬 검사 → 16B 항목마다 phys = base + (start << 20) */
}

/* C2: DT 'pci-host-ecam-generic' 노드 → 창. reg 는 부모의 #address-cells/#size-cells 로 읽고, 주소는 bus-range 의
 * 첫 버스 기준이다. 셀 수 1..2, bus_lo ≤ bus_hi ≤ 255 아니면 FMT, 크기 < 버스 수 × 1MB 면 SIZE. */
static int dt_ecam_window(const uint32_t *cells, size_t ncells, unsigned addr_cells, unsigned size_cells,
                          unsigned bus_lo, unsigned bus_hi, ecam_win *out) {
    (void)cells; (void)ncells; (void)addr_cells; (void)size_cells; (void)bus_lo; (void)bus_hi; (void)out;
    return ECAM_ERR_FMT; /* TODO C2: 셀 수·bus-range 검사 → 셀을 32비트씩 이어 base/size → size ≥ 버스 수 << 20 */
}

/* C3: CPU 물리 주소 (devmem 으로 찍어 볼 주소). 창 밖이거나 인자가 틀리면 0. */
static uint64_t ecam_phys(const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned reg) {
    (void)w; (void)bus; (void)dev; (void)fn; (void)reg;
    return 0; /* TODO C3: 창/인자 검사 → w->phys + ecam_offset(bus - bus_start, dev, fn, reg) */
}

typedef struct { uint8_t bus, dev, fn, hdr; uint16_t vid, did; uint32_t class_rev; } pci_fn_info;
typedef struct { int found, not_ready, rrs_retries; } scan_stats;

/* C4: 함수 하나가 있는가. 32비트 VID/DID 읽기 → VID 0x0001(RRS)이면 최대 max_retry 번 다시 읽기(st->rrs_retries++),
 * 그래도 RRS 면 st->not_ready++ 하고 -1. 0xFFFFFFFF / 0 / 0x0000FFFF / 0xFFFF0000 이면 없음(0). 있으면 1, *id = 값.
 * (Linux pci_bus_generic_read_dev_vendor_id 와 같은 판정. 실제 커널은 1 ms 부터 두 배씩 기다리며 최대 60 s.) */
static int probe_fn(const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned max_retry,
                    uint32_t *id, scan_stats *st) {
    (void)w; (void)bus; (void)dev; (void)fn; (void)max_retry; (void)id; (void)st;
    return 0; /* TODO C4: 32비트 VID/DID 읽기 → VID 0x0001 이면 재시도(예산 초과 시 not_ready, -1) → 없음 패턴 4개면 0 → 1 */
}

/* C5: 버스 하나 스캔. dev 0..31 의 fn0 을 probe, 있으면 기록하고 Header Type bit7(MFD)이 1일 때만 fn1..7 을 probe.
 * out 에 최대 cap 개 기록, 반환 = 기록한 수 (st->found 도 같은 값으로 누적). */
static int scan_bus(const ecam_win *w, unsigned bus, unsigned max_retry, pci_fn_info *out, int cap, scan_stats *st) {
    (void)w; (void)bus; (void)max_retry; (void)out; (void)cap; (void)st;
    return 0; /* TODO C5: dev 0..31 × fn0 probe → 있으면 기록, Header Type bit7 일 때만 fn1..7 */
}

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const ecam_win MOCK_WIN = { 0x40000000u, 0, 0, MOCK_NBUS - 1 };   /* mock 창: 버스 0..1 */

/* lspci -n 한 줄: "00:06.0 0180: 1af4:1042 (rev 01)" (rev 0 이면 생략 — lspci 와 같게) */
static void fmt_lspci_n(const pci_fn_info *p, char *buf, size_t cap) {
    int k = snprintf(buf, cap, "%02x:%02x.%u %04x: %04x:%04x", p->bus, p->dev, p->fn,
                     (unsigned)(p->class_rev >> 16), p->vid, p->did);
    if ((p->class_rev & 0xFFu) && k > 0 && (size_t)k < cap)
        snprintf(buf + k, cap - (size_t)k, " (rev %02x)", (unsigned)(p->class_rev & 0xFFu));
}
static uint32_t er(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned w) {
    uint32_t v = 0xDEADBEEFu; ecam_read(&MOCK_WIN, bus, dev, fn, reg, w, &v); return v;
}
static uint32_t cr(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned w) {
    uint32_t v = 0xDEADBEEFu; conf1_read(bus, dev, fn, reg, w, &v); return v;
}
/* 함수 번호를 무시하고 fn0..7 을 전부 probe 하는 잘못된 스캔 (비교용) */
static int scan_naive(const ecam_win *w, unsigned bus, scan_stats *st) {
    int n = 0; uint32_t id;
    for (unsigned dev = 0; dev < 32; dev++)
        for (unsigned fn = 0; fn < 8; fn++) if (probe_fn(w, bus, dev, fn, 8, &id, st) == 1) n++;
    return n;
}
/* MCFG 체크섬 바이트(오프셋 9)를 다시 맞춘다 */
static void acpi_fix_checksum(uint8_t *t, size_t L) {
    uint8_t s = 0; t[9] = 0;
    for (size_t i = 0; i < L; i++) s = (uint8_t)(s + t[i]);
    t[9] = (uint8_t)(0u - s);
}

/* Lima VM 실캡처: sudo od -An -tx1 -v /sys/firmware/acpi/tables/MCFG (60바이트) */
static const uint8_t VM_MCFG[60] = {
    0x4d,0x43,0x46,0x47,0x3c,0x00,0x00,0x00,0x01,0x71,0x41,0x50,0x50,0x4c,0x45,0x20,
    0x41,0x70,0x70,0x6c,0x65,0x20,0x56,0x7a,0x01,0x00,0x00,0x00,0x41,0x41,0x50,0x4c,
    0x27,0x04,0x18,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0x00,0x00,0x00 };

int main(void) {
    mock_reset();

    /* mock 자체 점검 (주어진 코드): devmem 처럼 원시 오프셋으로 ECAM 을 두드린다. 실캡처 MCFG 의 base 필드도 확인. */
    CHECK(mmio_read(0x30000, 4) == 0x10421AF4u && mmio_read(0x10000, 4) == 0xFFFFFFFFu);
    mmio_write(0x3000C, 1, 0x10);
    CHECK(mmio_read(0x3000C, 1) == 0x10u);
    mmio_write(0x3000C, 1, 0x00);
    CHECK(rd64le(VM_MCFG + 44) == 0x40000000u && VM_MCFG[54] == 0x00 && VM_MCFG[55] == 0xff);

    /* ---------- Part A: ECAM ---------- */
    CHECK(cfg_check(6, 0, 0x3c, 4, PCI_CFG_SPACE_SIZE) == CFG_OK && cfg_check(6, 0, 0x100, 4, PCI_CFG_SPACE_SIZE) == CFG_ERR_REG);
    CHECK(cfg_check(6, 0, 0x02, 4, PCI_CFG_SPACE_EXP_SIZE) == CFG_ERR_ALIGN && cfg_check(6, 0, 0x02, 3, 4096) == CFG_ERR_WIDTH);
    CHECK(ecam_offset(0, 0, 0, 0) == 0x0u);
    CHECK(ecam_offset(0, 6, 0, 0) == 0x30000u && ecam_offset(0, 2, 0, 0) == 0x10000u && ecam_offset(0, 0x1f, 0, 0) == 0xF8000u);
    CHECK(ecam_offset(1, 0, 0, 0x10) == 0x100010u && ecam_offset(0, 0x1f, 3, 0x0e) == 0xFB00Eu);
    CHECK(ecam_offset(0xff, 31, 7, 0xffc) == 0x0FFFFFFCu);
    {   /* 전수: 256 버스 × 32 × 8 × reg 3종 → 오프셋이 서로 다르고, 비트를 다시 잘라 내면 원래 값 */
        bool ok = true;
        for (unsigned b = 0; b < 256 && ok; b++)
            for (unsigned d = 0; d < 32; d++)
                for (unsigned f = 0; f < 8; f++) {
                    static const unsigned regs[3] = { 0x000, 0x03c, 0xfff };
                    for (int i = 0; i < 3; i++) {
                        uint32_t o = ecam_offset(b, d, f, regs[i]);
                        if ((o >> 20) != b || ((o >> 15) & 31u) != d || ((o >> 12) & 7u) != f || (o & 0xFFFu) != regs[i]) ok = false;
                    }
                }
        CHECK(ok);
    }
    CHECK(ecam_offset(0, 32, 0, 0) == ecam_offset(1, 0, 0, 0));   /* 검사 안 한 dev 32 = 다음 버스의 dev 0 (그래서 거른다) */

    /* VM 에서 devmem 으로 ECAM 을 직접 읽은 값(capture_ecam_vm.txt)과 mock(같은 VM 의 lspci 바이트)이 같아야 한다 */
    CHECK(er(0, 6, 0, PCI_VENDOR_ID, 4) == 0x10421AF4u);
    CHECK(er(0, 6, 0, PCI_DEVICE_ID, 2) == 0x1042u && er(0, 6, 0, PCI_COMMAND, 2) == 0x0416u);
    CHECK(er(0, 6, 0, PCI_CLASS_REVISION, 4) == 0x01800001u && er(0, 6, 0, PCI_HEADER_TYPE, 1) == 0x00u);
    CHECK(er(0, 6, 0, PCI_CAPABILITY_LIST, 4) == 0x40u && er(0, 6, 0, PCI_INTERRUPT_LINE, 1) == 0xFFu);
    CHECK(er(0, 0, 0, PCI_VENDOR_ID, 4) == 0x1A05106Bu && er(0, 0, 0, 0x100, 4) == 0x0u);
    {   /* 폭 일관성: 모든 함수·모든 dword 에서 read32 == (read16 hi:lo) == 바이트 4개 (리틀 엔디언) */
        static const unsigned fns[6][3] = { {0,0,0}, {0,6,0}, {0,0x1c,0}, {0,0x1f,0}, {0,0x1f,3}, {1,0,0} };
        int bad = 0;
        for (int i = 0; i < 6; i++)
            for (unsigned r = 0; r < PCI_CFG_SPACE_EXP_SIZE; r += 4) {
                unsigned b = fns[i][0], d = fns[i][1], f = fns[i][2];
                uint32_t d32 = er(b, d, f, r, 4);
                uint32_t w16 = er(b, d, f, r, 2) | (er(b, d, f, r + 2, 2) << 16);
                uint32_t b8 = er(b, d, f, r, 1) | (er(b, d, f, r + 1, 1) << 8) | (er(b, d, f, r + 2, 1) << 16) | (er(b, d, f, r + 3, 1) << 24);
                if (d32 != w16 || d32 != b8) bad++;
            }
        CHECK(bad == 0);
    }
    /* 없는 장치/함수: all-1s, 반환은 CFG_OK (접근 자체는 정상 — "없음"은 값으로 안다) */
    {
        uint32_t v = 0; int rc = ecam_read(&MOCK_WIN, 0, 2, 0, PCI_VENDOR_ID, 4, &v);
        CHECK(rc == CFG_OK && v == 0xFFFFFFFFu);
        CHECK(er(0, 2, 0, PCI_VENDOR_ID, 2) == 0xFFFFu && er(0, 2, 0, PCI_HEADER_TYPE, 1) == 0xFFu);
        CHECK(er(0, 0x1f, 1, PCI_VENDOR_ID, 2) == 0xFFFFu && er(0, 0x1f, 3, PCI_VENDOR_ID, 2) == 0x1234u);
    }
    /* 쓰기 */
    CHECK(ecam_write(&MOCK_WIN, 0, 6, 0, PCI_INTERRUPT_LINE, 1, 0x0B) == CFG_OK && er(0, 6, 0, PCI_INTERRUPT_LINE, 1) == 0x0Bu);
    CHECK(ecam_write(&MOCK_WIN, 1, 0, 0, PCI_COMMAND, 2, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) == CFG_OK &&
          er(1, 0, 0, PCI_COMMAND, 2) == 0x0006u);

    /* ---------- Part B: CF8/CFC ---------- */
    CHECK(conf1_address(0, 6, 0, 0x3c) == 0x8000303Cu && conf1_address(1, 0, 0, 0x10) == 0x80010010u);
    CHECK(conf1_address(0, 0x1f, 3, 0x0e) == 0x8000FB0Cu && conf1_address(0xff, 31, 7, 0xff) == 0x80FFFFFCu);
    {   /* 같은 레지스터를 두 경로로: 두 버스의 모든 dev/fn(없는 것 포함) × 0x00..0xFF × 폭 1/2/4 */
        unsigned cmp = 0, mism = 0, live = 0;
        for (unsigned b = 0; b < MOCK_NBUS; b++)
            for (unsigned d = 0; d < 32; d++)
                for (unsigned f = 0; f < 8; f++)
                    for (unsigned r = 0; r < PCI_CFG_SPACE_SIZE; r++)
                        for (unsigned w = 1; w <= 4; w *= 2) {
                            if (r & (w - 1)) continue;
                            cmp++;
                            uint32_t e = er(b, d, f, r, w);
                            if (e != cr(b, d, f, r, w)) mism++;
                            if (e != ones(w)) live++;
                        }
        printf("B same-register: %u reads compared (ECAM vs CF8/CFC), %u not all-1s, mismatches=%u\n", cmp, live, mism);
        CHECK(cmp == 2u * 32u * 8u * (256u + 128u + 64u) && mism == 0 && live > 0);
    }
    /* 바이트 레인: 데이터 포트 = 0xCFC + (reg & 3) */
    CHECK(cr(0, 0x1f, 0, PCI_HEADER_TYPE, 1) == 0x80u && g_last_port == 0xCFE);
    CHECK(cr(0, 6, 0, PCI_STATUS, 2) == 0x0010u && g_last_port == 0xCFE);
    CHECK(cr(0, 6, 0, PCI_INTERRUPT_PIN, 1) == 0x01u && g_last_port == 0xCFD);
    CHECK(cr(0, 6, 0, PCI_INTERRUPT_LINE, 4) == 0x0000010Bu && g_last_port == 0xCFC);  /* 0x3c 는 위에서 0x0B 로 씀 */
    CHECK(conf1_write(0, 6, 0, PCI_INTERRUPT_LINE, 1, 0x2A) == CFG_OK && g_last_port == 0xCFC && er(0, 6, 0, PCI_INTERRUPT_LINE, 1) == 0x2Au);
    /* CF8 로는 확장 영역(0x100~)에 못 간다. ECAM 은 간다. */
    {
        uint32_t v = 0; uint32_t ops0 = g_hw_ops;
        CHECK(conf1_read(1, 0, 0, 0x100, 4, &v) == CFG_ERR_REG && v == 0xFFFFFFFFu && g_hw_ops == ops0);
        CHECK(ecam_read(&MOCK_WIN, 1, 0, 0, 0x100, 4, &v) == CFG_OK && v == 0x00020001u);   /* AER 확장 헤더 (가상) */
    }
    /* 오류: 하드웨어에 닿기 전에 걸러야 한다 (g_hw_ops 그대로, g_hw_bad 0) */
    {
        uint32_t v, ops0 = g_hw_ops, bad0 = g_hw_bad;
        v = 0; CHECK(ecam_read(&MOCK_WIN, 0, 6, 0, 0x01, 2, &v) == CFG_ERR_ALIGN && v == 0xFFFFu);
        v = 0; CHECK(ecam_read(&MOCK_WIN, 0, 6, 0, 0x02, 4, &v) == CFG_ERR_ALIGN && v == 0xFFFFFFFFu);
        v = 0; CHECK(conf1_read(0, 6, 0, 0x03, 2, &v) == CFG_ERR_ALIGN && v == 0xFFFFu);
        CHECK(ecam_read(&MOCK_WIN, 0, 6, 0, 0x00, 3, &v) == CFG_ERR_WIDTH && conf1_read(0, 6, 0, 0, 8, &v) == CFG_ERR_WIDTH);
        CHECK(ecam_read(&MOCK_WIN, 0, 32, 0, 0, 4, &v) == CFG_ERR_BDF && ecam_read(&MOCK_WIN, 0, 0, 8, 0, 4, &v) == CFG_ERR_BDF);
        CHECK(conf1_read(0, 32, 0, 0, 4, &v) == CFG_ERR_BDF && conf1_read(0, 0, 8, 0, 4, &v) == CFG_ERR_BDF &&
              conf1_read(256, 0, 0, 0, 4, &v) == CFG_ERR_BDF);
        CHECK(ecam_read(&MOCK_WIN, 2, 0, 0, 0, 4, &v) == CFG_ERR_BDF && v == 0xFFFFFFFFu);    /* 창(버스 0..1) 밖 */
        CHECK(ecam_read(&MOCK_WIN, 0, 6, 0, 0x1000, 1, &v) == CFG_ERR_REG && ecam_read(&MOCK_WIN, 0, 6, 0, 0xFFE, 4, &v) == CFG_ERR_ALIGN);
        CHECK(ecam_write(&MOCK_WIN, 0, 6, 0, 0x05, 2, 0) == CFG_ERR_ALIGN && conf1_write(0, 6, 0, 0x100, 1, 0) == CFG_ERR_REG);
        CHECK(g_hw_ops == ops0 && g_hw_bad == bad0);
        CHECK(ecam_read(&MOCK_WIN, 0, 6, 0, 0xFFC, 4, &v) == CFG_OK && v == 0);              /* 마지막 dword 는 합법 */
    }
    /* RO 쓰기는 무시, 없는 장치로의 쓰기는 버려짐 */
    ecam_write(&MOCK_WIN, 0, 6, 0, PCI_VENDOR_ID, 2, 0xBEEF);
    ecam_write(&MOCK_WIN, 0, 6, 0, PCI_CLASS_REVISION, 4, 0x12345678u);
    conf1_write(0, 6, 0, PCI_INTERRUPT_PIN, 1, 0x04);
    CHECK(er(0, 6, 0, PCI_VENDOR_ID, 4) == 0x10421AF4u && er(0, 6, 0, PCI_CLASS_REVISION, 4) == 0x01800001u &&
          er(0, 6, 0, PCI_INTERRUPT_PIN, 1) == 0x01u);
    CHECK(ecam_write(&MOCK_WIN, 0, 2, 0, PCI_COMMAND, 2, PCI_COMMAND_MEMORY) == CFG_OK && er(0, 2, 0, PCI_COMMAND, 2) == 0xFFFFu);
    /* Status W1C 함정: 장치가 오류 비트를 세운 상태에서 Command 를 바꾼다 */
    {
        g_cfg[0][6][0][PCI_STATUS + 1] |= (uint8_t)((PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_DETECTED_PARITY) >> 8);
        uint32_t c16 = er(0, 6, 0, PCI_COMMAND, 2);
        ecam_write(&MOCK_WIN, 0, 6, 0, PCI_COMMAND, 2, c16 & ~(uint32_t)PCI_COMMAND_MASTER);       /* 올바름: 16비트 RMW */
        CHECK(er(0, 6, 0, PCI_COMMAND, 2) == 0x0412u && er(0, 6, 0, PCI_STATUS, 2) == 0xA010u);    /* 오류 비트 보존 */
        uint32_t d = er(0, 6, 0, PCI_COMMAND, 4);                                                 /* 0xA0100412 */
        ecam_write(&MOCK_WIN, 0, 6, 0, PCI_COMMAND, 4, d | PCI_COMMAND_MASTER);                  /* 버그: 32비트 RMW */
        printf("B W1C trap: 32-bit RMW of COMMAND read 0x%08X -> STATUS now 0x%04X (error bits lost)\n",
               (unsigned)d, (unsigned)er(0, 6, 0, PCI_STATUS, 2));
        CHECK(d == 0xA0100412u && er(0, 6, 0, PCI_COMMAND, 2) == 0x0416u && er(0, 6, 0, PCI_STATUS, 2) == 0x0010u);
        g_cfg[0][6][0][PCI_STATUS + 1] |= (uint8_t)((PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_DETECTED_PARITY) >> 8);
        ecam_write(&MOCK_WIN, 0, 6, 0, PCI_STATUS, 2, PCI_STATUS_REC_MASTER_ABORT);              /* 의도한 지우기: 그 비트만 */
        CHECK(er(0, 6, 0, PCI_STATUS, 2) == 0x8010u);
    }
    /* 창의 첫 버스가 0 이 아닐 때: 오프셋은 (bus - bus_start) 로 */
    {
        ecam_win w40 = { 0x40000000u, 0, 0x40, 0x41 };
        uint32_t v = 0;
        g_bus_base = 0x40;
        CHECK(ecam_read(&w40, 0x41, 0, 0, PCI_VENDOR_ID, 4, &v) == CFG_OK && v == 0x00101B36u);
        CHECK(ecam_read(&w40, 0x40, 6, 0, PCI_VENDOR_ID, 4, &v) == CFG_OK && v == 0x10421AF4u);
        CHECK(ecam_read(&w40, 0x3f, 0, 0, PCI_VENDOR_ID, 4, &v) == CFG_ERR_BDF && g_hw_bad == 0);
        g_bus_base = 0;
    }
    /* enable 비트(31)를 빼먹으면 CFC 는 config 사이클이 아니다 → all-1s */
    io_out(PCI_CONF_ADDR_PORT, 4, conf1_address(0, 6, 0, 0) & ~PCI_CONF1_ENABLE);
    CHECK(io_in(PCI_CONF_DATA_PORT, 4) == 0xFFFFFFFFu && conf1_address(0, 6, 0, 0) == 0x80003000u);

    /* ---------- Part C: 펌웨어/드라이버 현실 ---------- */
    {   /* ACPI MCFG (VM 실캡처) → ECAM 창, 물리 주소가 VM 에서 devmem 으로 읽은 주소와 같아야 한다 */
        ecam_win w[4]; memset(w, 0, sizeof w);
        int n = mcfg_parse(VM_MCFG, sizeof VM_MCFG, w, 4);
        printf("C MCFG: %d entry, base 0x%llx seg %u bus %02x-%02x\n", n, (unsigned long long)w[0].phys,
               (unsigned)w[0].seg, (unsigned)w[0].bus_start, (unsigned)w[0].bus_end);
        CHECK(n == 1 && w[0].phys == 0x40000000u && w[0].seg == 0 && w[0].bus_start == 0 && w[0].bus_end == 0xff);
        CHECK(ecam_phys(&w[0], 0, 6, 0, 0) == 0x40030000u && ecam_phys(&w[0], 0, 0x1f, 0, 0) == 0x400F8000u);
        CHECK(ecam_phys(&w[0], 0, 2, 0, 0) == 0x40010000u && ecam_phys(&w[0], 0, 0, 0, 0x100) == 0x40000100u);
        CHECK(ecam_phys(&w[0], 0, 6, 0, PCI_INTERRUPT_LINE) == 0x4003003Cu && ecam_phys(&w[0], 0xff, 31, 7, 0xfff) == 0x4FFFFFFFu);
        CHECK(ecam_phys(&w[0], 0, 32, 0, 0) == 0);

        uint8_t t[60 + 16];
        memcpy(t, VM_MCFG, 60); t[20] ^= 1;                                  /* 1바이트 손상 → 체크섬 실패 */
        CHECK(mcfg_parse(t, 60, w, 4) == ECAM_ERR_FMT);
        CHECK(mcfg_parse(VM_MCFG, 50, w, 4) == ECAM_ERR_FMT && mcfg_parse(VM_MCFG, 40, w, 4) == ECAM_ERR_FMT);
        memcpy(t, VM_MCFG, 60); t[0] = 'X';
        CHECK(mcfg_parse(t, 60, w, 4) == ECAM_ERR_FMT);
        /* 손으로 만든 2항목 MCFG: seg 1, 버스 0x10..0x1f, base 0x80_0000_0000 → 창 주소 = base + (0x10 << 20) */
        memcpy(t, VM_MCFG, 60); memset(t + 60, 0, 16);
        t[4] = 76;
        t[60 + 4] = 0x80; t[60 + 8] = 1; t[60 + 10] = 0x10; t[60 + 11] = 0x1f;
        acpi_fix_checksum(t, 76);
        n = mcfg_parse(t, sizeof t, w, 4);
        CHECK(n == 2 && w[1].seg == 1 && w[1].bus_start == 0x10 && w[1].phys == 0x8001000000ull);
        CHECK(ecam_phys(&w[1], 0x10, 0, 0, 0) == 0x8001000000ull && ecam_phys(&w[1], 0x0f, 0, 0, 0) == 0);
        CHECK(mcfg_parse(t, sizeof t, w, 1) == ECAM_ERR_SIZE);
    }
    {   /* DT: QEMU virt 의 pcie@10000000 (lab virt.dts) — 루트 #address-cells=2 #size-cells=2 */
        static const uint32_t virt_reg[4] = { 0x40, 0x10000000, 0x0, 0x10000000 };
        ecam_win w; memset(&w, 0, sizeof w);
        CHECK(dt_ecam_window(virt_reg, 4, 2, 2, 0x00, 0xff, &w) == CFG_OK && w.phys == 0x4010000000ull && w.bus_end == 0xff);
        CHECK(ecam_phys(&w, 1, 0, 0, 0x10) == 0x4010100010ull);
        /* DT 의 reg 는 bus-range 첫 버스 기준 (MCFG 와 다름!) */
        static const uint32_t r16[4] = { 0x0, 0x30000000, 0x0, 0x01000000 };
        CHECK(dt_ecam_window(r16, 4, 2, 2, 0x10, 0x1f, &w) == CFG_OK && ecam_phys(&w, 0x10, 0, 0, 0) == 0x30000000u);
        CHECK(dt_ecam_window(r16, 4, 2, 2, 0x00, 0xff, &w) == ECAM_ERR_SIZE);         /* 16MB 로는 버스 16개뿐 */
        CHECK(dt_ecam_window(r16, 3, 2, 2, 0, 15, &w) == ECAM_ERR_FMT && dt_ecam_window(r16, 4, 3, 1, 0, 15, &w) == ECAM_ERR_FMT &&
              dt_ecam_window(r16, 4, 2, 2, 5, 4, &w) == ECAM_ERR_FMT);
        static const uint32_t r32[2] = { 0x3f000000, 0x01000000 };                       /* #address-cells=1 #size-cells=1 */
        CHECK(dt_ecam_window(r32, 2, 1, 1, 0, 15, &w) == CFG_OK && ecam_phys(&w, 15, 31, 7, 0xffc) == 0x3FFFFFFCu);
    }
    {   /* 버스 스캔: multi-function 규칙 + RRS */
        pci_fn_info info[16]; scan_stats st; char line[64];
        memset(&st, 0, sizeof st);
        int n = scan_bus(&MOCK_WIN, 0, 8, info, 16, &st);
        printf("C scan bus 00:\n");
        for (int i = 0; i < n; i++) { fmt_lspci_n(&info[i], line, sizeof line); printf("   %s\n", line); }
        CHECK(n == 5 && st.found == 5 && st.not_ready == 0);
        CHECK(n == 5 && info[0].dev == 0x00 && info[1].dev == 0x06 && info[2].dev == 0x1c && info[3].dev == 0x1f &&
              info[3].fn == 0 && info[4].dev == 0x1f && info[4].fn == 3 && (info[2].hdr & PCI_HEADER_TYPE_MASK) == 1);
        if (n >= 2) {   /* VM 실출력 'lspci -n' 과 같은 줄 */
            fmt_lspci_n(&info[0], line, sizeof line); CHECK(strcmp(line, "00:00.0 0600: 106b:1a05") == 0);
            fmt_lspci_n(&info[1], line, sizeof line); CHECK(strcmp(line, "00:06.0 0180: 1af4:1042 (rev 01)") == 0);
        } else { CHECK(0); CHECK(0); }

        /* 01:00.0 SSD: 함수 번호를 무시하는 장치 — MFD 비트를 보지 않으면 8개로 보인다 */
        memset(&st, 0, sizeof st);
        int naive = scan_naive(&MOCK_WIN, 1, &st);
        memset(&st, 0, sizeof st);
        n = scan_bus(&MOCK_WIN, 1, 8, info, 16, &st);
        printf("C bus 01: naive (fn0..7 all) found %d, MFD-aware found %d\n", naive, n);
        CHECK(naive == 8 && n == 1 && info[0].vid == 0x1b36 && info[0].did == 0x0010 && info[0].class_rev == 0x01080202u);

        /* RRS: SSD 펌웨어가 아직 부팅 중 — VID 0x0001 을 5번 돌려준다 (SV 켜짐) */
        g_fn[1][0][0].rrs_left = 5; g_vid_reads = 0;
        memset(&st, 0, sizeof st);
        n = scan_bus(&MOCK_WIN, 1, 8, info, 16, &st);
        printf("C RRS (SV on, 5 not-ready replies): found %d, rrs_retries %d, VID reads %u\n", n, st.rrs_retries, (unsigned)g_vid_reads);
        CHECK(n == 1 && st.rrs_retries == 5 && st.not_ready == 0 && g_vid_reads == 6);
        g_fn[1][0][0].rrs_left = 5;
        memset(&st, 0, sizeof st);
        n = scan_bus(&MOCK_WIN, 1, 3, info, 16, &st);                   /* 재시도 예산 3: 포기 */
        CHECK(n == 0 && st.not_ready == 1 && st.rrs_retries == 3 && g_fn[1][0][0].rrs_left == 1);
        memset(&st, 0, sizeof st);
        n = scan_bus(&MOCK_WIN, 1, 3, info, 16, &st);                   /* 나중에 다시: 이제 준비됨 */
        CHECK(n == 1 && st.not_ready == 0 && st.rrs_retries == 1);
        /* SV 꺼짐: RC 가 하드웨어로 재시도 → 소프트웨어는 RRS 를 못 보고 CPU 가 그만큼 멈춘다 */
        g_rrs_sv = false; g_fn[1][0][0].rrs_left = 5; g_hw_stall = 0;
        memset(&st, 0, sizeof st);
        n = scan_bus(&MOCK_WIN, 1, 8, info, 16, &st);
        CHECK(n == 1 && st.rrs_retries == 0 && g_hw_stall == 5);
        g_rrs_sv = true;
    }
    CHECK(g_hw_bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
