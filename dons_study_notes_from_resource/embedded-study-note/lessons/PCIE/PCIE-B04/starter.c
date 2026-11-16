/* PCIE-B04 Capability 리스트 순회와 PCIe Capability 디코드
 *
 * 입력: lspci -xxx (256B) / -xxxx (4KB) 텍스트 → uint8_t cfg[] (little-endian 바이트 배열)
 * Part A: legacy capability 리스트 (0x34 에서 시작, 8비트 ID + 8비트 next)
 *         extended capability 리스트 (0x100 에서 시작, 32비트 헤더: ID 15:0, Version 19:16, Next 31:20)
 *         PCIe Capability (ID 0x10) 디코드: Flags(버전·포트 타입), DevCap/DevCtl(MPS/MRRS),
 *         LnkCap(최대 속도·폭·ASPM·포트 번호), LnkCtl, LnkSta(현재 속도·폭·Training·DLL Active), LnkCap2/LnkCtl2
 * Part B: 순회 방어 — 방문 비트맵(루프), 홉 제한, 포인터 < 0x40 / < 0x100, 4바이트 비정렬, 구조체가 끝을 넘음,
 *         ID 0xFF(all-1s), Status.CapList=0, 256B 덤프(확장 공간 없음), 0x100 = 0xFFFFFFFF
 * Part C: 링크 트리아지 — 다운스트림 포트(루트 포트) + 엔드포인트 두 덤프로 기대 속도/폭 vs 실제
 *
 * 상수 이름/값은 Linux include/uapi/linux/pci_regs.h 와 같다 (맥에서 헤더 없이 빌드되도록 직접 정의).
 * 테스트 덤프: 가상 Gen4 x4 NVMe SSD(Gen4 x2 로 학습된 상태, "가상 덤프") + 가상 루트 포트 +
 *             QEMU 10.2.1 pcie-root-port / nvme 실제 캡처 (QEMU-emulated 값) + 손상 변형들.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-d: 가상 덤프 출력)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- pci_regs.h 에서 가져온 이름 (값 동일) ---- */
#define PCI_CFG_SPACE_SIZE        256
#define PCI_CFG_SPACE_EXP_SIZE    4096
#define PCI_STD_HEADER_SIZEOF     64
#define PCI_VENDOR_ID             0x00
#define PCI_STATUS                0x06
#define  PCI_STATUS_CAP_LIST      0x10
#define PCI_HEADER_TYPE           0x0e
#define  PCI_HEADER_TYPE_MASK     0x7f
#define  PCI_HEADER_TYPE_CARDBUS  2
#define PCI_CAPABILITY_LIST       0x34
#define PCI_CB_CAPABILITY_LIST    0x14
#define PCI_CAP_LIST_ID           0
#define PCI_CAP_LIST_NEXT         1
#define PCI_CAP_FLAGS             2
#define PCI_CAP_SIZEOF            4
#define  PCI_CAP_ID_PM            0x01
#define  PCI_CAP_ID_MSI           0x05
#define  PCI_CAP_ID_VNDR          0x09
#define  PCI_CAP_ID_SSVID         0x0D
#define  PCI_CAP_ID_EXP           0x10
#define  PCI_CAP_ID_MSIX          0x11
#define PCI_PM_SIZEOF             8
#define PCI_MSI_FLAGS             0x02
#define  PCI_MSI_FLAGS_64BIT      0x0080
#define  PCI_MSI_FLAGS_MASKBIT    0x0100
#define PCI_CAP_MSIX_SIZEOF       12
#define PCI_EXP_FLAGS             0x02
#define  PCI_EXP_FLAGS_VERS       0x000f
#define  PCI_EXP_FLAGS_TYPE       0x00f0
#define  PCI_EXP_FLAGS_SLOT       0x0100
#define PCI_EXP_DEVCAP            0x04
#define  PCI_EXP_DEVCAP_PAYLOAD   0x00000007
#define PCI_EXP_DEVCTL            0x08
#define  PCI_EXP_DEVCTL_PAYLOAD   0x00e0
#define  PCI_EXP_DEVCTL_READRQ    0x7000
#define PCI_EXP_LNKCAP            0x0c
#define  PCI_EXP_LNKCAP_SLS       0x0000000f
#define  PCI_EXP_LNKCAP_MLW       0x000003f0
#define  PCI_EXP_LNKCAP_ASPMS     0x00000c00
#define  PCI_EXP_LNKCAP_DLLLARC   0x00100000
#define  PCI_EXP_LNKCAP_PN        0xff000000
#define PCI_EXP_LNKCTL            0x10
#define  PCI_EXP_LNKCTL_ASPMC     0x0003
#define  PCI_EXP_LNKCTL_RCB       0x0008
#define  PCI_EXP_LNKCTL_LD        0x0010
#define  PCI_EXP_LNKCTL_CCC       0x0040
#define PCI_EXP_LNKSTA            0x12
#define  PCI_EXP_LNKSTA_CLS       0x000f
#define  PCI_EXP_LNKSTA_NLW       0x03f0
#define  PCI_EXP_LNKSTA_LT        0x0800
#define  PCI_EXP_LNKSTA_SLC       0x1000
#define  PCI_EXP_LNKSTA_DLLLA     0x2000
#define PCI_CAP_EXP_ENDPOINT_SIZEOF_V1 20     /* v1: LnkSta 까지 */
#define PCI_CAP_EXP_ENDPOINT_SIZEOF_V2 0x34   /* v2: LnkSta2 까지 */
#define PCI_EXP_LNKCAP2           0x2c
#define  PCI_EXP_LNKCAP2_SLS      0x000000fe
#define PCI_EXP_LNKCTL2           0x30
#define  PCI_EXP_LNKCTL2_TLS      0x000f
#define PCI_EXT_CAP_ID(h)         ((h) & 0x0000ffff)
#define PCI_EXT_CAP_VER(h)        (((h) >> 16) & 0xf)
#define PCI_EXT_CAP_NEXT_RAW(h)   (((h) >> 20) & 0xfff)  /* Linux 의 PCI_EXT_CAP_NEXT 는 & 0xffc (하위 2비트 마스크) */
#define  PCI_EXT_CAP_ID_ERR       0x01
#define  PCI_EXT_CAP_ID_DSN       0x03
#define  PCI_EXT_CAP_ID_ACS       0x0D
#define  PCI_EXT_CAP_ID_ARI       0x0E
#define  PCI_EXT_CAP_ID_ATS       0x0F
#define  PCI_EXT_CAP_ID_SRIOV     0x10
#define  PCI_EXT_CAP_ID_PRI       0x13
#define  PCI_EXT_CAP_ID_LTR       0x18
#define  PCI_EXT_CAP_ID_SECPCI    0x19
#define  PCI_EXT_CAP_ID_PASID     0x1B
#define  PCI_EXT_CAP_ID_L1SS      0x1E
#define  PCI_EXT_CAP_ID_PL_16GT   0x26
#define PCI_ERR_COR_STATUS        0x10

/* 순회 한계: legacy 는 0x40..0xFC 의 4바이트 정렬 위치 = 48개 (Linux PCI_FIND_CAP_TTL 도 48),
 * extended 는 0x100..0xFFC = 960개 (Linux 는 (4096-256)/8 = 480 을 TTL 로 쓴다). */
#define LEG_SLOTS   ((PCI_CFG_SPACE_SIZE - PCI_STD_HEADER_SIZEOF) / 4)          /* 48  */
#define EXT_SLOTS   ((PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 4)         /* 960 */

enum {                     /* 순회 결과 status */
    CW_OK          =  0,   /* 끝(next = 0)까지 정상 */
    CW_NONE        =  1,   /* Status.CapList = 0 → legacy 리스트 없음 (오류 아님) */
    CW_NO_EXT      =  2,   /* 확장 공간 없음: 256B 덤프이거나 0x100 이 0xFFFFFFFF */
    CW_ERR_LOOP    = -1,   /* 이미 방문한 오프셋으로 돌아옴 */
    CW_ERR_PTR_LOW = -2,   /* legacy < 0x40, extended < 0x100 (0 이 아닌데) */
    CW_ERR_ALIGN   = -3,   /* 하위 2비트가 0 이 아님 (스펙: 예약, 00b) */
    CW_ERR_RANGE   = -4,   /* 구조체가 config 공간 끝을 넘음 */
    CW_ERR_ALLONES = -5,   /* VID 0xFFFF / ID 0xFF / 헤더 0xFFFFFFFF — 장치 없음·사라짐 */
    CW_ERR_HOPS    = -6,   /* 홉 제한 초과 (방문 비트맵이 있으면 도달 불가 — 방어용) */
    CW_ERR_FULL    = -7,   /* 호출자 out[] 용량 부족 */
    CW_ERR_ARG     = -8,
};

typedef struct {
    uint16_t off;     /* capability 위치 */
    uint16_t id;      /* legacy: 8비트, extended: 16비트 */
    uint8_t  ver;     /* extended 만 (legacy 는 0) */
    uint16_t next;    /* 원시 next 값 (extended 는 12비트 그대로) */
} cap_ent;

typedef struct {
    int      status;  /* CW_* */
    int      n;       /* out[] 에 채운 개수 (오류 전까지의 정상 항목) */
    uint16_t err_ptr; /* 문제 된 포인터 값 (RANGE/ALLONES 는 그 cap 의 위치) */
    uint16_t err_from;/* 그 포인터를 읽은 곳: 0x34 / cap+1 (legacy) / cap 헤더 위치 (extended) */
} walk_result;

typedef struct {
    uint16_t off;
    uint8_t  ver, type;          /* Flags[3:0] 버전, Flags[7:4] 장치/포트 타입 */
    bool     slot;
    uint16_t mps_sup, mps, mrrs; /* 바이트 */
    uint32_t lnkcap;
    uint8_t  max_speed, max_width, aspm_sup, port_num;
    bool     dllla_cap;          /* LnkCap.DLLLARC: DLL Link Active 보고 가능 (보통 다운스트림 포트) */
    uint16_t lnkctl;
    uint8_t  aspm_ctl;
    bool     rcb128, link_disable, ccc;
    uint16_t lnksta;
    uint8_t  cur_speed, neg_width;
    bool     training, slot_clk, dll_active;
    bool     has_v2;             /* 버전 ≥ 2 → LnkCap2 / LnkCtl2 존재 */
    uint32_t lnkcap2;
    uint16_t lnkctl2;
    uint8_t  target_speed;
} pcie_info;

/* ---------------- config 공간 읽기 도우미 (정렬 안 된 포인터 캐스트 금지, 바이트 조립) ---------------- */
static uint8_t  cfg_rd8 (const uint8_t *c, uint16_t o) { return c[o]; }
static uint16_t cfg_rd16(const uint8_t *c, uint16_t o) { return (uint16_t)(c[o] | (c[o + 1] << 8)); }
static uint32_t cfg_rd32(const uint8_t *c, uint16_t o) {
    return (uint32_t)c[o] | ((uint32_t)c[o + 1] << 8) | ((uint32_t)c[o + 2] << 16) | ((uint32_t)c[o + 3] << 24);
}

/* ---------------- lspci -xxx/-xxxx 텍스트 파서 (PCIE-B03 축약판) ---------------- */
enum { LP_OK = 0, LP_ERR_SYNTAX = -1, LP_ERR_ORDER = -2, LP_ERR_LEN = -3, LP_ERR_CAP = -4 };

static int hexv(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
/* 한 줄 [s, e) 를 "OFF: b0 b1 ... b15" 로 해석. 성공 시 0 */
static int parse_row(const char *s, const char *e, unsigned *off, uint8_t row[16]) {
    unsigned v = 0; int nd = 0;
    while (s < e && hexv(*s) >= 0 && nd < 4) { v = v * 16 + (unsigned)hexv(*s); s++; nd++; }
    if (nd < 1 || nd > 3 || s >= e || *s != ':') return -1;
    s++;
    for (int i = 0; i < 16; i++) {
        if (s >= e || *s != ' ') return -1;
        s++;
        if (e - s < 2 || hexv(s[0]) < 0 || hexv(s[1]) < 0) return -1;
        row[i] = (uint8_t)(hexv(s[0]) * 16 + hexv(s[1]));
        s += 2;
    }
    while (s < e && (*s == ' ' || *s == '\r')) s++;
    if (s != e) return -1;
    *off = v;
    return 0;
}
/* 덤프 시작 전의 줄(장치 이름 줄 등)은 건너뛴다. 시작 뒤에는 모든 비어 있지 않은 줄이 다음 행이어야 한다.
 * 끝 길이는 256 또는 4096 만 허용. */
static int cfg_parse_lspci(const char *t, size_t tlen, uint8_t *cfg, size_t cap, size_t *out_len) {
    size_t expect = 0; bool started = false;
    const char *p = t, *end = t + tlen;
    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        if (!e) e = end;
        const char *q = p;
        while (q < e && (*q == ' ' || *q == '\t')) q++;
        if (q < e && !(e - q == 1 && *q == '\r')) {
            unsigned off; uint8_t row[16];
            int ok = parse_row(q, e, &off, row) == 0;
            if (!started && !(ok && off == 0)) { p = e + (e < end); continue; }   /* 머리 줄 */
            if (!ok) return LP_ERR_SYNTAX;
            if (off != expect) return LP_ERR_ORDER;
            if (expect + 16 > cap) return LP_ERR_CAP;
            memcpy(cfg + expect, row, 16);
            expect += 16; started = true;
        }
        p = e + (e < end);
    }
    if (expect != PCI_CFG_SPACE_SIZE && expect != PCI_CFG_SPACE_EXP_SIZE) return LP_ERR_LEN;
    *out_len = expect;
    return LP_OK;
}
/* cfg → lspci 형식 텍스트 (오프셋 %02x, 0x100 이상은 3자리 — lspci 와 같다). 반환: 길이 또는 -1 */
static int cfg_to_lspci(const uint8_t *cfg, size_t len, char *buf, size_t cap) {
    size_t w = 0;
    for (size_t o = 0; o < len; o += 16) {
        int k = snprintf(buf + w, cap - w, "%02zx:", o);
        if (k < 0 || (size_t)k >= cap - w) return -1;
        w += (size_t)k;
        for (int i = 0; i < 16; i++) {
            k = snprintf(buf + w, cap - w, " %02x", cfg[o + (size_t)i]);
            if (k < 0 || (size_t)k >= cap - w) return -1;
            w += (size_t)k;
        }
        if (w + 1 >= cap) return -1;
        buf[w++] = '\n'; buf[w] = '\0';
    }
    return (int)w;
}

/* ---------------- 이름 표 ---------------- */
static const char *cap_name(uint16_t id) {
    switch (id) {
    case 0x00: return "Null";
    case 0x01: return "Power Management";       case 0x02: return "AGP";
    case 0x03: return "VPD";                    case 0x04: return "Slot ID";
    case 0x05: return "MSI";                    case 0x06: return "CompactPCI HotSwap";
    case 0x07: return "PCI-X";                  case 0x08: return "HyperTransport";
    case 0x09: return "Vendor Specific";        case 0x0A: return "Debug Port";
    case 0x0B: return "CompactPCI CRC";         case 0x0C: return "PCI Hot-Plug (SHPC)";
    case 0x0D: return "Bridge Subsystem VID";   case 0x0E: return "AGP3";
    case 0x0F: return "Secure Device";          case 0x10: return "PCI Express";
    case 0x11: return "MSI-X";                  case 0x12: return "SATA";
    case 0x13: return "Advanced Features";      case 0x14: return "Enhanced Allocation";
    default:   return "Unknown";
    }
}
static const char *ext_cap_name(uint16_t id) {
    switch (id) {
    case 0x0000: return "Null";
    case 0x0001: return "Advanced Error Reporting";  case 0x0002: return "Virtual Channel";
    case 0x0003: return "Device Serial Number";      case 0x0004: return "Power Budgeting";
    case 0x000B: return "Vendor Specific (VSEC)";    case 0x000D: return "Access Control Services";
    case 0x000E: return "Alternative Routing-ID";    case 0x000F: return "Address Translation Services";
    case 0x0010: return "SR-IOV";                    case 0x0013: return "Page Request";
    case 0x0015: return "Resizable BAR";             case 0x0017: return "TPH Requester";
    case 0x0018: return "Latency Tolerance Reporting"; case 0x0019: return "Secondary PCI Express";
    case 0x001B: return "PASID";                     case 0x001D: return "Downstream Port Containment";
    case 0x001E: return "L1 PM Substates";           case 0x001F: return "Precision Time Measurement";
    case 0x0023: return "Designated Vendor-Specific (DVSEC)";
    case 0x0025: return "Data Link Feature";         case 0x0026: return "Physical Layer 16.0 GT/s";
    case 0x002A: return "Physical Layer 32.0 GT/s";  case 0x002E: return "Data Object Exchange";
    case 0x0031: return "Physical Layer 64.0 GT/s";
    default:     return "Unknown";
    }
}
static const char *port_type_name(uint8_t t) {
    switch (t) {
    case 0x0: return "Endpoint";            case 0x1: return "Legacy Endpoint";
    case 0x4: return "Root Port";           case 0x5: return "Upstream Port";
    case 0x6: return "Downstream Port";     case 0x7: return "PCIe-to-PCI Bridge";
    case 0x8: return "PCI-to-PCIe Bridge";  case 0x9: return "RC Integrated Endpoint";
    case 0xA: return "RC Event Collector";  default:  return "Reserved";
    }
}
/* 속도 코드 1..6 → GT/s (pci_regs.h PCI_EXP_LNKCAP_SLS_* / PCI_EXP_LNKSTA_CLS_*) */
static const char *speed_str(uint8_t code) {
    static const char *t[] = { "unknown", "2.5GT/s", "5GT/s", "8GT/s", "16GT/s", "32GT/s", "64GT/s" };
    return code <= 6 ? t[code] : "unknown";
}

/* 각 cap 이 차지하는 최소 바이트 (이보다 짧으면 공간 끝을 넘는다고 판정) */
static uint16_t cap_min_size(const uint8_t *cfg, uint16_t off) {
    switch (cfg_rd8(cfg, off)) {
    case PCI_CAP_ID_PM:    return PCI_PM_SIZEOF;
    case PCI_CAP_ID_MSI: {                                    /* 플래그에 따라 0x0A..0x18 */
        uint16_t f = cfg_rd16(cfg, (uint16_t)(off + PCI_MSI_FLAGS));
        uint16_t sz = (f & PCI_MSI_FLAGS_64BIT) ? 0x0E : 0x0A;
        if (f & PCI_MSI_FLAGS_MASKBIT) sz = (uint16_t)(sz + 0x0A);   /* 패딩 2 + Mask 4 + Pending 4 */
        return sz;
    }
    case PCI_CAP_ID_MSIX:  return PCI_CAP_MSIX_SIZEOF;
    case PCI_CAP_ID_EXP:   return (cfg_rd16(cfg, (uint16_t)(off + PCI_EXP_FLAGS)) & PCI_EXP_FLAGS_VERS) >= 2
                                  ? PCI_CAP_EXP_ENDPOINT_SIZEOF_V2 : PCI_CAP_EXP_ENDPOINT_SIZEOF_V1;
    case PCI_CAP_ID_VNDR: { uint8_t l = cfg_rd8(cfg, (uint16_t)(off + 2)); return l < 3 ? 3 : l; }
    case PCI_CAP_ID_SSVID: return 8;
    default:               return PCI_CAP_SIZEOF;
    }
}
static uint16_t ext_min_size(uint16_t id) {
    switch (id) {
    case PCI_EXT_CAP_ID_ERR:    return 0x2C;   /* 헤더 로그 끝까지 (루트 포트는 0x38) */
    case PCI_EXT_CAP_ID_DSN:    return 12;
    case PCI_EXT_CAP_ID_ACS:    return 8;
    case PCI_EXT_CAP_ID_ARI:    return 8;
    case PCI_EXT_CAP_ID_ATS:    return 8;
    case PCI_EXT_CAP_ID_SRIOV:  return 0x40;
    case PCI_EXT_CAP_ID_PRI:    return 16;
    case PCI_EXT_CAP_ID_LTR:    return 8;
    case PCI_EXT_CAP_ID_SECPCI: return 0x0C;
    case PCI_EXT_CAP_ID_PASID:  return 8;
    case PCI_EXT_CAP_ID_L1SS:   return 0x10;
    case PCI_EXT_CAP_ID_PL_16GT:return 0x20;
    default:                    return 4;
    }
}

/* 방문 비트맵: 4바이트 칸 하나당 1비트 */
static bool bm_test_set(uint32_t *bm, unsigned slot) {
    uint32_t m = 1u << (slot & 31);
    bool was = (bm[slot >> 5] & m) != 0;
    bm[slot >> 5] |= m;
    return was;
}

/* ================= Part A1 (+B): legacy capability 리스트 ================= */
static walk_result cap_walk_legacy(const uint8_t *cfg, size_t len, cap_ent *out, int cap) {
    walk_result r = { CW_OK, 0, 0, 0 };
    if (!cfg || len < PCI_CFG_SPACE_SIZE || (cap > 0 && !out)) { r.status = CW_ERR_ARG; return r; }
    /* TODO A1: 0x34 (CardBus 면 0x14) 에서 시작해 next 를 따라간다.
     *   - VID 0xFFFF → ALLONES, Status.CapList=0 → NONE
     *   - 포인터마다: < 0x40 → PTR_LOW, 하위 2비트 → ALIGN, 방문 → LOOP, 홉 > 48 → HOPS,
     *     ID 0xFF → ALLONES, off + cap_min_size > 256 → RANGE, 꽉 참 → FULL
     *   - err_ptr / err_from 기록 */
    (void)cap_min_size; (void)bm_test_set;
    return r;                       /* 스텁: "cap 없음" — 틀렸지만 안전 */
}

/* ================= Part A2 (+B): extended capability 리스트 ================= */
static walk_result cap_walk_ext(const uint8_t *cfg, size_t len, cap_ent *out, int cap) {
    walk_result r = { CW_OK, 0, 0, 0 };
    if (!cfg || len < PCI_CFG_SPACE_SIZE || (cap > 0 && !out)) { r.status = CW_ERR_ARG; return r; }
    /* TODO A2: 0x100 에서 시작. 256B 덤프 또는 0x100 == 0xFFFFFFFF → NO_EXT, 0 → 빈 리스트(OK).
     *   - 헤더: ID 15:0, Ver 19:16, Next 31:20 (12비트 원시값)
     *   - next 0 → 끝, 0 < next < 0x100 → PTR_LOW, 하위 2비트 → ALIGN, 방문 → LOOP, 홉 > 960 → HOPS
     *   - 헤더 0xFFFFFFFF → ALLONES, off + ext_min_size > 4096 → RANGE, 꽉 참 → FULL */
    (void)ext_min_size;
    return r;                       /* 스텁: 빈 리스트 */
}

/* ================= Part A3: PCI Express Capability 디코드 ================= */
/* off = PCIe cap 위치. 반환 0, ID 가 0x10 이 아니면 -1, 구조체가 256 을 넘으면 CW_ERR_RANGE */
static int pcie_decode(const uint8_t *cfg, size_t len, uint16_t off, pcie_info *pi) {
    if (!cfg || !pi || len < PCI_CFG_SPACE_SIZE) return CW_ERR_ARG;
    memset(pi, 0, sizeof *pi);
    /* TODO A3: Flags(버전·타입·slot), DevCap MPS 지원, DevCtl MPS/MRRS (128 << 코드),
     *          LnkCap(SLS, MLW 9:4, ASPMS 11:10, DLLLARC, PN 31:24), LnkCtl(ASPMC, RCB, LD, CCC),
     *          LnkSta(CLS, NLW 9:4, LT, SLC, DLLLA), v2 면 LnkCap2 / LnkCtl2(TLS) */
    (void)off;
    return -1;                      /* 스텁: 디코드 실패 */
}

/* legacy 리스트에서 PCIe cap 을 찾아 디코드 (편의 함수: 워커 + 디코더 조합) */
static int pcie_find_decode(const uint8_t *cfg, size_t len, pcie_info *pi) {
    cap_ent e[LEG_SLOTS];
    walk_result r = cap_walk_legacy(cfg, len, e, LEG_SLOTS);
    for (int i = 0; i < r.n; i++)
        if (e[i].id == PCI_CAP_ID_EXP) return pcie_decode(cfg, len, e[i].off, pi);
    return -1;
}

/* ================= Part C: 링크 트리아지 ================= */
enum {
    LT_OK            = 0,
    LT_SPEED_LOW     = 1u << 0,   /* 실제 속도 < min(공통 최대, Target Link Speed) */
    LT_WIDTH_LOW     = 1u << 1,   /* 실제 폭 < min(양쪽 MLW) */
    LT_LINK_DOWN     = 1u << 2,   /* DLLLARC 인데 DLLLA=0, 또는 협상 폭 0 */
    LT_TRAINING      = 1u << 3,   /* LnkSta.LT=1 (학습 중 — 오래 남아 있으면 이상) */
    LT_MISMATCH      = 1u << 4,   /* 양쪽 LnkSta 가 다름 */
    LT_TARGET_CAPPED = 1u << 5,   /* 다운스트림 포트 LnkCtl2.TLS 가 공통 최대보다 낮음 (설정, 결함 아님) */
    LT_ASPM_BAD      = 1u << 6,   /* 켜진 ASPM 이 양쪽 LnkCap 지원의 부분집합이 아님 */
    LT_NO_PCIE       = 1u << 7,   /* 한쪽에 PCIe cap 없음/디코드 실패 */
};
typedef struct {
    unsigned flags;
    uint8_t  common_speed;   /* 양쪽 지원 속도의 최고 공통값 (코드) */
    uint8_t  exp_speed, exp_width, act_speed, act_width;
} link_report;

/* 지원 속도 벡터 (bit k = 속도 코드 k). LnkCap2.SLS 를 LnkCap 최대값 이하로 자르고,
 * 0 이면 LnkCap 최대 속도(1 또는 2)로 합성 — Linux pcie_get_supported_speeds() 와 같은 규칙. */
static uint8_t pcie_speed_vector(const pcie_info *pi) {
    /* TODO C1 */
    (void)pi;
    return 0;                       /* 스텁: 지원 속도 없음 */
}
static uint8_t highest_bit(uint8_t v) { uint8_t k = 0; while (v >>= 1) k++; return k; }

/* dsp = 링크의 위쪽(루트 포트/스위치 다운스트림 포트), usp = 아래쪽(엔드포인트/스위치 업스트림 포트) */
static int link_triage(const uint8_t *dsp, size_t dlen, const uint8_t *usp, size_t ulen, link_report *r) {
    if (!r) return CW_ERR_ARG;
    memset(r, 0, sizeof *r);
    pcie_info d, u;
    if (!dsp || !usp || pcie_find_decode(dsp, dlen, &d) != 0 || pcie_find_decode(usp, ulen, &u) != 0) {
        r->flags = LT_NO_PCIE; return -1;
    }
    /* TODO C2: 기대값 = 공통 최고 속도(Target Link Speed 로 상한) / 양쪽 MLW 의 최소,
     *          실제값 = 다운스트림 포트의 LnkSta. 플래그를 규칙대로 세운다. */
    (void)highest_bit;
    return 0;                       /* 스텁: 판정 없음 (flags = 0) */
}

/* ---------------- 보고서 (lspci -vv 비슷한 줄 + 트리아지 문장) ---------------- */
typedef struct { char *b; size_t cap, w; } sbuf;
static void sb_printf(sbuf *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_printf(sbuf *s, const char *fmt, ...) {
    if (s->w >= s->cap) return;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(s->b + s->w, s->cap - s->w, fmt, ap);
    va_end(ap);
    if (k > 0) s->w = (s->w + (size_t)k < s->cap) ? s->w + (size_t)k : s->cap - 1;
}
static const char *cw_str(int st) {
    switch (st) {
    case CW_OK: return "ok"; case CW_NONE: return "no capability list (Status.CapList=0)";
    case CW_NO_EXT: return "no extended config space";
    case CW_ERR_LOOP: return "LOOP"; case CW_ERR_PTR_LOW: return "POINTER < base";
    case CW_ERR_ALIGN: return "UNALIGNED pointer"; case CW_ERR_RANGE: return "capability runs past end";
    case CW_ERR_ALLONES: return "all-ones (device gone?)"; case CW_ERR_HOPS: return "hop limit";
    case CW_ERR_FULL: return "output full"; default: return "bad argument";
    }
}
static const char *aspm_str(uint8_t a) {
    static const char *t[] = { "not supported", "L0s", "L1", "L0s L1" };
    return t[a & 3];
}
static void link_lines(sbuf *s, const pcie_info *p) {
    sb_printf(s, "        LnkCap: Port #%u, Speed %s, Width x%u, ASPM %s%s\n", p->port_num,
              speed_str(p->max_speed), p->max_width, aspm_str(p->aspm_sup), p->dllla_cap ? ", DLActive reporting+" : "");
    sb_printf(s, "        LnkCtl: ASPM %s; RCB %u bytes; %s CommClk%c\n",
              p->aspm_ctl ? aspm_str(p->aspm_ctl) : "Disabled", p->rcb128 ? 128u : 64u,
              p->link_disable ? "Disabled+" : "Disabled-", p->ccc ? '+' : '-');
    sb_printf(s, "        LnkSta: Speed %s%s, Width x%u%s, Train%c SlotClk%c DLActive%c\n",
              speed_str(p->cur_speed), p->cur_speed < p->max_speed ? " (below own cap)" : "",
              p->neg_width, p->neg_width < p->max_width ? " (below own cap)" : "",
              p->training ? '+' : '-', p->slot_clk ? '+' : '-', p->dll_active ? '+' : '-');
    if (p->has_v2)
        sb_printf(s, "        LnkCap2: SLS vector 0x%02x  LnkCtl2: Target Link Speed %s\n",
                  (unsigned)(p->lnkcap2 & PCI_EXP_LNKCAP2_SLS), speed_str(p->target_speed));
}
static int cap_report(const uint8_t *cfg, size_t len, char *buf, size_t cap) {
    sbuf s = { buf, cap, 0 };
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';
    cap_ent e[LEG_SLOTS];
    walk_result r = cap_walk_legacy(cfg, len, e, LEG_SLOTS);
    for (int i = 0; i < r.n; i++) {
        sb_printf(&s, "[%02x] %s (%02x)", e[i].off, cap_name(e[i].id), e[i].id);
        if (e[i].id == PCI_CAP_ID_EXP) {
            pcie_info p;
            if (pcie_decode(cfg, len, e[i].off, &p) == 0) {
                sb_printf(&s, " v%u %s%s, MaxPayload %u (sup %u), MaxReadReq %u\n", p.ver, port_type_name(p.type),
                          p.slot ? " (Slot+)" : "", p.mps, p.mps_sup, p.mrrs);
                link_lines(&s, &p);
            } else sb_printf(&s, " <decode failed>\n");
        } else if (e[i].id == PCI_CAP_ID_MSIX) {
            uint16_t f = cfg_rd16(cfg, (uint16_t)(e[i].off + 2));
            uint32_t t = cfg_rd32(cfg, (uint16_t)(e[i].off + 4)), pb = cfg_rd32(cfg, (uint16_t)(e[i].off + 8));
            sb_printf(&s, " Enable%c Count=%u Table: BAR%u+0x%x PBA: BAR%u+0x%x\n", (f & 0x8000) ? '+' : '-',
                      (f & 0x07FFu) + 1u, t & 7u, t & ~7u, pb & 7u, pb & ~7u);
        } else if (e[i].id == PCI_CAP_ID_MSI) {
            uint16_t f = cfg_rd16(cfg, (uint16_t)(e[i].off + 2));
            sb_printf(&s, " Enable%c 64bit%c Maskable%c\n", (f & 1) ? '+' : '-',
                      (f & PCI_MSI_FLAGS_64BIT) ? '+' : '-', (f & PCI_MSI_FLAGS_MASKBIT) ? '+' : '-');
        } else sb_printf(&s, "\n");
    }
    if (r.status != CW_OK)
        sb_printf(&s, "legacy walk: %s at 0x%02x (pointer read from 0x%02x)\n", cw_str(r.status), r.err_ptr, r.err_from);
    cap_ent x[64];
    walk_result rx = cap_walk_ext(cfg, len, x, 64);
    for (int i = 0; i < rx.n; i++) {
        sb_printf(&s, "[%03x v%u] %s (%04x)", x[i].off, x[i].ver, ext_cap_name(x[i].id), x[i].id);
        if (x[i].id == PCI_EXT_CAP_ID_ERR)
            sb_printf(&s, " CESta=0x%08x", (unsigned)cfg_rd32(cfg, (uint16_t)(x[i].off + PCI_ERR_COR_STATUS)));
        sb_printf(&s, "\n");
    }
    if (rx.status != CW_OK)
        sb_printf(&s, "extended walk: %s at 0x%03x (from 0x%03x)\n", cw_str(rx.status), rx.err_ptr, rx.err_from);
    return (int)s.w;
}
static int triage_text(const link_report *r, char *buf, size_t cap) {
    sbuf s = { buf, cap, 0 };
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';
    if (r->flags & LT_NO_PCIE) { sb_printf(&s, "PCIe capability 없음 — 링크 판정 불가\n"); return (int)s.w; }
    sb_printf(&s, "기대: %s x%u (공통 최고 %s) / 실제: %s x%u\n", speed_str(r->exp_speed), r->exp_width,
              speed_str(r->common_speed), speed_str(r->act_speed), r->act_width);
    if (r->flags & LT_LINK_DOWN) sb_printf(&s, "- 링크 다운: DLActive-/폭 0 → PERST#·레퍼런스 클럭·전원·LTSSM 상태부터 (PCIE-M07)\n");
    if (r->flags & LT_TRAINING)  sb_printf(&s, "- Link Training 비트가 남아 있음 → Recovery 반복 의심 (PCIE-M08)\n");
    if (r->flags & LT_SPEED_LOW) sb_printf(&s, "- 속도 저하 → 등화(EQ)·신호 품질·AER Correctable(RxErr/BadTLP) 확인\n");
    if (r->flags & LT_WIDTH_LOW) sb_printf(&s, "- 폭 저하 → 레인 단선/AC 커플링 캡/레인 반전·커넥터, 리셋 타이밍\n");
    if (r->flags & LT_TARGET_CAPPED) sb_printf(&s, "- Target Link Speed 가 상한을 낮춤 (설정 — 결함 아님)\n");
    if (r->flags & LT_MISMATCH)  sb_printf(&s, "- 양쪽 LnkSta 불일치 → 학습 중에 읽었거나 덤프 시점이 다름, 다시 읽기\n");
    if (r->flags & LT_ASPM_BAD)  sb_printf(&s, "- ASPM 이 양쪽 지원 범위 밖으로 켜짐 → 링크 불안정 원인 (PCIE-M09)\n");
    if (r->flags == LT_OK || r->flags == LT_TARGET_CAPPED) sb_printf(&s, "- 정상: 기대한 속도·폭으로 학습됨\n");
    return (int)s.w;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void w16(uint8_t *c, unsigned o, uint16_t v) { c[o] = (uint8_t)v; c[o + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *c, unsigned o, uint32_t v) { for (int i = 0; i < 4; i++) c[o + (unsigned)i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ext_hdr(uint16_t id, uint8_t ver, uint16_t next) { return (uint32_t)id | ((uint32_t)ver << 16) | ((uint32_t)next << 20); }

/* 가상 덤프 1: Gen4 x4 NVMe SSD 엔드포인트, Gen4 x2 로 학습됨 (VID 0xF00D 는 가짜 값) */
static void build_ssd(uint8_t c[PCI_CFG_SPACE_EXP_SIZE]) {
    memset(c, 0, PCI_CFG_SPACE_EXP_SIZE);
    w16(c, 0x00, 0xF00D); w16(c, 0x02, 0x5404);
    w16(c, 0x04, 0x0406);                      /* Mem+ BusMaster+ INTx-disable+ */
    w16(c, 0x06, 0x0010);                      /* Status.CapList */
    c[0x08] = 0x01; c[0x09] = 0x02; c[0x0A] = 0x08; c[0x0B] = 0x01;   /* class 01:08:02 NVMe */
    w32(c, 0x10, 0xFE000004);                  /* BAR0 64-bit mem */
    w16(c, 0x2C, 0xF00D); w16(c, 0x2E, 0x0001);
    c[0x34] = 0x40; c[0x3C] = 0xFF; c[0x3D] = 0x01;
    c[0x40] = 0x01; c[0x41] = 0x50; w16(c, 0x42, 0x0003); w16(c, 0x44, 0x0008);       /* PM */
    c[0x50] = 0x05; c[0x51] = 0x70; w16(c, 0x52, 0x0180);                              /* MSI 64bit+ Maskable+ */
    c[0x70] = 0x10; c[0x71] = 0xB0; w16(c, 0x72, 0x0002);                              /* PCIe v2 Endpoint */
    w32(c, 0x74, 0x10008022);                  /* DevCap: MPS 512 지원, ExtTag, RBER, FLR */
    w16(c, 0x78, 0x293F);                      /* DevCtl: MPS 256, MRRS 512, 오류 보고 4종, RO, ExtTag, NoSnoop */
    w32(c, 0x7C, 0x00070844);                  /* LnkCap: 16GT/s, x4, ASPM L1, L1 exit lat 6, ClockPM */
    w16(c, 0x80, 0x0142);                      /* LnkCtl: ASPM L1, CommClk, CLKREQ# */
    w16(c, 0x82, 0x1024);                      /* LnkSta: 16GT/s x2, SlotClk */
    w32(c, 0x94, 0x0000081F);                  /* DevCap2: CTO ranges, CTO disable, LTR */
    w16(c, 0x98, 0x0400);                      /* DevCtl2: LTR enable */
    w32(c, 0x9C, 0x0000001E);                  /* LnkCap2: 2.5/5/8/16 */
    w16(c, 0xA0, 0x0004);                      /* LnkCtl2: Target 16GT/s */
    c[0xB0] = 0x11; c[0xB1] = 0xC0; w16(c, 0xB2, 0x801F);                              /* MSI-X on, 32 vectors */
    w32(c, 0xB4, 0x00002000); w32(c, 0xB8, 0x00003000);
    c[0xC0] = 0x09; c[0xC1] = 0x00; c[0xC2] = 0x10; c[0xC3] = 0x01;                    /* Vendor, len 16 */
    w32(c, 0x100, ext_hdr(PCI_EXT_CAP_ID_ERR, 2, 0x148));
    w32(c, 0x110, 0x00000041);                 /* AER CESta: Receiver Error + Bad TLP */
    w32(c, 0x114, 0x00002000);                 /* CEMsk: Advisory Non-Fatal */
    w32(c, 0x148, ext_hdr(PCI_EXT_CAP_ID_LTR, 1, 0x150));
    w16(c, 0x14C, 0x1003); w16(c, 0x14E, 0x1003);
    w32(c, 0x150, ext_hdr(PCI_EXT_CAP_ID_L1SS, 1, 0x160));
    w32(c, 0x154, 0x0028281F);
    w32(c, 0x160, ext_hdr(PCI_EXT_CAP_ID_DSN, 1, 0x170));
    w32(c, 0x164, 0x01020304); w32(c, 0x168, 0x0A0B0C0D);
    w32(c, 0x170, ext_hdr(PCI_EXT_CAP_ID_SECPCI, 1, 0x1A0));
    w32(c, 0x1A0, ext_hdr(PCI_EXT_CAP_ID_PL_16GT, 1, 0x000));
}
/* 가상 덤프 2: 그 SSD 가 꽂힌 루트 포트 (Type 1, 256B = lspci -xxx 만 있음) */
static void build_rp(uint8_t c[PCI_CFG_SPACE_SIZE]) {
    memset(c, 0, PCI_CFG_SPACE_SIZE);
    w16(c, 0x00, 0xF00D); w16(c, 0x02, 0x1A04);
    w16(c, 0x04, 0x0407); w16(c, 0x06, 0x0010);
    c[0x0A] = 0x04; c[0x0B] = 0x06; c[0x0E] = 0x01;                                    /* 06:04 bridge, Type 1 */
    c[0x18] = 0x00; c[0x19] = 0x01; c[0x1A] = 0x01;
    w16(c, 0x20, 0xFE00); w16(c, 0x22, 0xFE00);
    c[0x34] = 0x40;
    c[0x40] = 0x10; c[0x41] = 0x80; w16(c, 0x42, 0x0142);                              /* PCIe v2 Root Port, Slot+ */
    w32(c, 0x44, 0x00008002); w16(c, 0x48, 0x203F);
    w32(c, 0x4C, 0x01380C44);                  /* LnkCap: 16GT/s x4, ASPM L0s L1, SDERC, DLLLARC, LBNC, Port #1 */
    w16(c, 0x50, 0x0042);                      /* LnkCtl: ASPM L1, CommClk */
    w16(c, 0x52, 0x3024);                      /* LnkSta: 16GT/s x2, SlotClk, DLActive */
    w32(c, 0x54, 0x00080060);
    w32(c, 0x64, 0x0000081F); w32(c, 0x6C, 0x0000001E); w16(c, 0x70, 0x0004);
    c[0x80] = 0x05; c[0x81] = 0x90; w16(c, 0x82, 0x0080);                              /* MSI 64bit */
    c[0x90] = 0x01; c[0x91] = 0xA0; w16(c, 0x92, 0x0003);                              /* PM */
    c[0xA0] = 0x0D; c[0xA1] = 0x00; w16(c, 0xA4, 0xF00D);                              /* SSVID */
}

/* QEMU 10.2.1 실제 캡처 (qemu_capture_xxxx.txt 에서 0 이 아닌 행 그대로; 나머지 행은 전부 0 이라 pad_text 가 채운다) */
static const char QEMU_NVME[] =
    "01:00.0 Non-Volatile memory controller: QEMU NVMe (header line, skipped)\n"
    "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n"
    "10: 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00\n"
    "40: 11 80 40 00 00 20 00 00 00 30 00 00 00 00 00 00\n"
    "50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "60: 01 00 03 00 08 00 00 00 00 00 00 00 00 00 00 00\n"
    "70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "80: 10 60 02 00 20 80 00 10 00 00 00 00 11 04 00 00\n"
    "90: 00 00 11 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 30 00 00 00 00 00 00 00 00 00\n";
static const char QEMU_RP[] =
    "00: 36 1b 0c 00 07 04 10 00 00 00 04 06 00 00 01 00\n"
    "10: 00 10 28 10 00 00 00 00 00 01 01 00 10 10 00 00\n"
    "20: 00 10 10 10 01 00 11 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 54 00 00 00 00 00 00 00 0f 01 02 00\n"
    "40: 0d 00 00 00 36 1b 00 00 11 40 00 80 00 00 00 00\n"
    "50: 00 08 00 00 10 48 42 01 20 80 00 00 0f 00 00 00\n"
    "60: 04 06 30 00 00 00 11 20 7b 00 0a 00 c0 03 50 00\n"
    "70: 00 00 00 00 00 00 00 00 20 00 30 00 00 00 00 00\n"
    "80: 1e 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "100: 01 00 82 14 00 00 00 00 00 00 40 02 30 20 46 00\n"
    "110: 00 00 00 00 00 e0 00 00 a0 02 00 00 00 00 00 00\n"
    "120: 00 00 00 00 00 00 00 00 00 00 00 00 07 00 00 00\n"
    "130: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "140: 00 00 00 00 00 00 00 00 0d 00 01 00 5f 00 1d 00\n";

static char g_txt[4096 * 4];
/* 캡처 텍스트 뒤에 0 행을 붙여 to 바이트 길이로 만든다 */
static size_t pad_text(const char *src, size_t to) {
    size_t n = strlen(src), next = 0;
    if (n >= sizeof g_txt) return 0;
    memcpy(g_txt, src, n + 1);
    for (const char *p = g_txt; *p; ) {             /* 마지막 행 오프셋 + 16 */
        const char *e = strchr(p, '\n');
        if (!e) e = p + strlen(p);
        unsigned off; uint8_t row[16];
        if (parse_row(p, e, &off, row) == 0 && off + 16u > next) next = off + 16u;
        p = *e ? e + 1 : e;
    }
    for (size_t o = next; o < to; o += 16) {
        int k = snprintf(g_txt + n, sizeof g_txt - n, "%02zx: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n", o);
        if (k < 0 || (size_t)k >= sizeof g_txt - n) return 0;
        n += (size_t)k;
    }
    return n;
}

static uint8_t g_ssd[4096], g_rp[256], g_qn[4096], g_qr[4096], g_m[4096], g_m2[4096];
static char g_rep[8192];
static cap_ent g_e[1024];

static walk_result wl(const uint8_t *c, size_t len) { return cap_walk_legacy(c, len, g_e, 1024); }
static walk_result we(const uint8_t *c, size_t len) { return cap_walk_ext(c, len, g_e, 1024); }
static bool ids_are(const cap_ent *e, int n, const uint16_t *ids, int m) {
    if (n != m) return false;
    for (int i = 0; i < n; i++) if (e[i].id != ids[i]) return false;
    return true;
}

int main(int argc, char **argv) {
    bool dump = argc > 1 && strcmp(argv[1], "-d") == 0;
    size_t len = 0;

    /* ---- 준비: 가상 덤프 → lspci 텍스트 → 파서 왕복, QEMU 캡처 파싱 ---- */
    {
        uint8_t raw[4096]; build_ssd(raw);
        CHECK(cfg_to_lspci(raw, 4096, g_txt, sizeof g_txt) > 0);
        CHECK(cfg_parse_lspci(g_txt, strlen(g_txt), g_ssd, sizeof g_ssd, &len) == LP_OK && len == 4096 && memcmp(raw, g_ssd, 4096) == 0);
        if (dump) {   /* 0 이 아닌 행만 출력 (md 의 덤프 발췌) */
            for (unsigned o = 0; o < 0x1B0; o += 16) {
                bool nz = false; for (int i = 0; i < 16; i++) nz |= g_ssd[o + (unsigned)i] != 0;
                if (!nz) continue;
                printf("%02x:", o); for (int i = 0; i < 16; i++) printf(" %02x", g_ssd[o + (unsigned)i]); printf("\n");
            }
        }
        uint8_t rr[256]; build_rp(rr);
        CHECK(cfg_to_lspci(rr, 256, g_txt, sizeof g_txt) > 0 &&
              cfg_parse_lspci(g_txt, strlen(g_txt), g_rp, sizeof g_rp, &len) == LP_OK && len == 256);
        size_t n = pad_text(QEMU_NVME, 4096);
        CHECK(n > 0 && cfg_parse_lspci(g_txt, n, g_qn, sizeof g_qn, &len) == LP_OK && len == 4096);
        n = pad_text(QEMU_RP, 4096);
        CHECK(n > 0 && cfg_parse_lspci(g_txt, n, g_qr, sizeof g_qr, &len) == LP_OK && len == 4096);
        /* 텍스트 오류: 잘림(255 바이트 아님), 순서 틀림, 깨진 hex */
        CHECK(cfg_parse_lspci(QEMU_NVME, strlen(QEMU_NVME), g_m, sizeof g_m, &len) == LP_ERR_LEN);
        static const char bad1[] = "00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n20: 00\n";
        static const char bad2[] = "00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
                                   "20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
        CHECK(cfg_parse_lspci(bad1, strlen(bad1), g_m, sizeof g_m, &len) == LP_ERR_SYNTAX);
        CHECK(cfg_parse_lspci(bad2, strlen(bad2), g_m, sizeof g_m, &len) == LP_ERR_ORDER);
    }

    /* ---- Part A: 정상 순회 ---- */
    {
        walk_result r = wl(g_ssd, 4096);
        const uint16_t want[] = { 0x01, 0x05, 0x10, 0x11, 0x09 };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, want, 5));
        CHECK(r.n == 5 && g_e[0].off == 0x40 && g_e[1].off == 0x50 && g_e[2].off == 0x70 && g_e[3].off == 0xB0 && g_e[4].off == 0xC0);
        r = we(g_ssd, 4096);
        const uint16_t wx[] = { 0x0001, 0x0018, 0x001E, 0x0003, 0x0019, 0x0026 };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, wx, 6));
        CHECK(r.n == 6 && g_e[0].off == 0x100 && g_e[0].ver == 2 && g_e[0].next == 0x148 && g_e[5].off == 0x1A0 && g_e[5].next == 0);

        pcie_info p;
        CHECK(pcie_decode(g_ssd, 4096, 0x70, &p) == 0);
        CHECK(p.ver == 2 && p.type == 0 && !p.slot);
        CHECK(p.max_speed == 4 && p.max_width == 4 && p.aspm_sup == 2 && p.port_num == 0 && !p.dllla_cap);
        CHECK(p.cur_speed == 4 && p.neg_width == 2 && !p.training && p.slot_clk && !p.dll_active);
        CHECK(p.aspm_ctl == 2 && p.ccc && !p.rcb128 && !p.link_disable);
        CHECK(p.mps_sup == 512 && p.mps == 256 && p.mrrs == 512);
        CHECK(p.has_v2 && (p.lnkcap2 & PCI_EXP_LNKCAP2_SLS) == 0x1E && p.target_speed == 4);
        CHECK(pcie_decode(g_ssd, 4096, 0x50, &p) == -1);              /* MSI 위치는 PCIe cap 아님 */

        /* 가상 루트 포트: Type 1 이어도 0x34 에서 같은 리스트 */
        r = wl(g_rp, 256);
        const uint16_t wr[] = { 0x10, 0x05, 0x01, 0x0D };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, wr, 4));
        CHECK(pcie_decode(g_rp, 256, 0x40, &p) == 0 && p.type == 4 && p.slot && p.dllla_cap && p.dll_active &&
              p.port_num == 1 && p.aspm_sup == 3 && p.max_width == 4 && p.neg_width == 2);
        CHECK(we(g_rp, 256).status == CW_NO_EXT);                     /* -xxx 256B 덤프 → 확장 공간 없음 */

        /* QEMU 실제 캡처 */
        r = wl(g_qn, 4096);
        const uint16_t qn[] = { 0x11, 0x10, 0x01 };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, qn, 3) && g_e[0].off == 0x40 && g_e[1].off == 0x80 && g_e[2].off == 0x60);
        CHECK(we(g_qn, 4096).status == CW_OK && we(g_qn, 4096).n == 0);   /* 0x100 = 0 → 확장 cap 없음 */
        CHECK(pcie_decode(g_qn, 4096, 0x80, &p) == 0 && p.type == 0 && p.max_speed == 1 && p.max_width == 1 &&
              p.cur_speed == 1 && p.neg_width == 1 && p.lnkcap2 == 0 && p.mps_sup == 128);
        r = wl(g_qr, 4096);
        const uint16_t qr[] = { 0x10, 0x11, 0x0D };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, qr, 3) && g_e[0].off == 0x54);
        r = we(g_qr, 4096);
        const uint16_t qx[] = { 0x0001, 0x000D };
        CHECK(r.status == CW_OK && ids_are(g_e, r.n, qx, 2) && g_e[0].ver == 2 && g_e[1].off == 0x148);
        CHECK(pcie_decode(g_qr, 4096, 0x54, &p) == 0 && p.type == 4 && p.slot && p.max_speed == 4 && p.max_width == 32 &&
              p.cur_speed == 1 && p.neg_width == 1 && p.dll_active && p.dllla_cap);

        /* 이름 표, 속도 문자열 */
        CHECK(strcmp(speed_str(1), "2.5GT/s") == 0 && strcmp(speed_str(4), "16GT/s") == 0 &&
              strcmp(speed_str(6), "64GT/s") == 0 && strcmp(speed_str(0), "unknown") == 0 && strcmp(speed_str(7), "unknown") == 0);
        CHECK(strcmp(ext_cap_name(PCI_EXT_CAP_ID_SRIOV), "SR-IOV") == 0 && strcmp(cap_name(PCI_CAP_ID_MSIX), "MSI-X") == 0);
    }

    /* ---- Part B: 손상 덤프 ---- */
    {
        walk_result r;
        memcpy(g_m, g_ssd, 4096); g_m[0x51] = 0x40;                  /* MSI.next → PM : 루프 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_LOOP && r.n == 2 && r.err_ptr == 0x40 && r.err_from == 0x51);
        memcpy(g_m, g_ssd, 4096); g_m[0x71] = 0x70;                  /* 자기 자신 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_LOOP && r.n == 3 && r.err_ptr == 0x70);
        memcpy(g_m, g_ssd, 4096); g_m[0x41] = 0x20;                  /* 헤더 안을 가리킴 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_PTR_LOW && r.n == 1 && r.err_ptr == 0x20 && r.err_from == 0x41);
        memcpy(g_m, g_ssd, 4096); g_m[0x34] = 0x3C;                  /* 첫 포인터부터 낮음 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_PTR_LOW && r.n == 0 && r.err_from == 0x34);
        memcpy(g_m, g_ssd, 4096); g_m[0x41] = 0x51;                  /* 비정렬 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_ALIGN && r.n == 1 && r.err_ptr == 0x51);
        memcpy(g_m, g_ssd, 4096); g_m[0xB1] = 0xD8;                  /* PCIe cap 을 0xD8 에 복사 → 0xD8+0x34 > 0x100 */
        memcpy(g_m + 0xD8, g_ssd + 0x70, 0x28); g_m[0xD9] = 0x00;
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_RANGE && r.n == 4 && r.err_ptr == 0xD8);
        pcie_info p;
        CHECK(pcie_decode(g_m, 4096, 0xD8, &p) == CW_ERR_RANGE);
        memcpy(g_m, g_ssd, 4096); memset(g_m + 0x70, 0xFF, 4);       /* cap 자리가 all-1s */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_ERR_ALLONES && r.n == 2 && r.err_ptr == 0x70);
        memset(g_m, 0xFF, 4096);                                      /* 장치 없음 */
        CHECK(wl(g_m, 4096).status == CW_ERR_ALLONES && we(g_m, 4096).status == CW_NO_EXT);
        memcpy(g_m, g_ssd, 4096); w16(g_m, 0x06, 0x0000);             /* Status.CapList=0 → 0x34 무시 */
        r = wl(g_m, 4096);
        CHECK(r.status == CW_NONE && r.n == 0);
        /* 합법적인 최장 체인: 0x40..0xFC 48개 Slot ID cap → 홉 제한(48)에 걸리지 않아야 */
        memcpy(g_m, g_ssd, 256); memset(g_m + 0x40, 0, 0xC0);
        for (unsigned o = 0x40; o <= 0xFC; o += 4) { g_m[o] = 0x04; g_m[o + 1] = (uint8_t)(o == 0xFC ? 0 : o + 4); }
        r = wl(g_m, 256);
        CHECK(r.status == CW_OK && r.n == 48);
        r = cap_walk_legacy(g_m, 256, g_e, 4);
        CHECK(r.status == CW_ERR_FULL && r.n == 4);
        g_m[0xFD] = 0x40;                                             /* 마지막이 처음으로 → 49번째에서 루프 */
        r = wl(g_m, 256);
        CHECK(r.status == CW_ERR_LOOP && r.n == 48 && r.err_from == 0xFD);

        /* extended */
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x170, ext_hdr(PCI_EXT_CAP_ID_SECPCI, 1, 0x100));   /* 0x170 → 0x100 루프 */
        r = we(g_m, 4096);
        CHECK(r.status == CW_ERR_LOOP && r.n == 5 && r.err_ptr == 0x100 && r.err_from == 0x170);
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x148, ext_hdr(PCI_EXT_CAP_ID_LTR, 1, 0x0F0));      /* < 0x100 */
        r = we(g_m, 4096);
        CHECK(r.status == CW_ERR_PTR_LOW && r.n == 2 && r.err_ptr == 0x0F0 && r.err_from == 0x148);
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x148, ext_hdr(PCI_EXT_CAP_ID_LTR, 1, 0x152));      /* 비정렬 */
        r = we(g_m, 4096);
        CHECK(r.status == CW_ERR_ALIGN && r.n == 2 && r.err_ptr == 0x152);
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x1A0, ext_hdr(PCI_EXT_CAP_ID_PL_16GT, 1, 0xFF0)); /* AER 를 0xFF0 에 → 끝 넘음 */
        w32(g_m, 0xFF0, ext_hdr(PCI_EXT_CAP_ID_ERR, 2, 0));
        r = we(g_m, 4096);
        CHECK(r.status == CW_ERR_RANGE && r.n == 6 && r.err_ptr == 0xFF0);
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x100, 0xFFFFFFFFu);       /* 확장 공간 접근 불가 (CF8 경로 등) */
        CHECK(we(g_m, 4096).status == CW_NO_EXT);
        memcpy(g_m, g_ssd, 4096); w32(g_m, 0x160, 0xFFFFFFFFu);       /* 중간에서 all-1s */
        r = we(g_m, 4096);
        CHECK(r.status == CW_ERR_ALLONES && r.n == 3 && r.err_ptr == 0x160);
        /* 확장 최장 체인 960개 + 끝을 처음으로 → 961번째에서 루프 */
        memcpy(g_m2, g_ssd, 256); memset(g_m2 + 256, 0, 4096 - 256);
        for (unsigned o = 0x100; o <= 0xFFC; o += 4) w32(g_m2, o, ext_hdr(0x000B, 1, (uint16_t)(o == 0xFFC ? 0 : o + 4)));
        r = we(g_m2, 4096);
        CHECK(r.status == CW_OK && r.n == 960);
        w32(g_m2, 0xFFC, ext_hdr(0x000B, 1, 0x100));
        r = we(g_m2, 4096);
        CHECK(r.status == CW_ERR_LOOP && r.n == 960 && r.err_from == 0xFFC);

        /* 무작위 바이트 1000개 덤프: 어떤 경우에도 끝나고, n ≤ 슬롯 수, 범위 밖을 읽지 않음 (ASan 으로 확인) */
        uint32_t s = 0xC0FFEEu; bool okall = true;
        for (int t = 0; t < 1000; t++) {
            for (int i = 0; i < 4096; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; g_m[i] = (uint8_t)s; }
            if (t & 1) { g_m[0x06] |= 0x10; g_m[0x00] = 0x86; }       /* 절반은 CapList 켜고 VID 정상 */
            walk_result a = wl(g_m, 4096), b = we(g_m, 4096);
            okall &= a.n <= LEG_SLOTS && b.n <= EXT_SLOTS && a.status != CW_ERR_HOPS && b.status != CW_ERR_HOPS;
        }
        CHECK(okall);
    }

    /* ---- Part C: 링크 트리아지 ---- */
    {
        link_report r;
        pcie_info p;
        CHECK(pcie_decode(g_ssd, 4096, 0x70, &p) == 0 && pcie_speed_vector(&p) == 0x1E);
        CHECK(pcie_decode(g_qn, 4096, 0x80, &p) == 0 && pcie_speed_vector(&p) == 0x02);   /* LnkCap2=0 → 합성 */
        p.lnkcap2 = 0; p.max_speed = 2; CHECK(pcie_speed_vector(&p) == 0x06);
        p.lnkcap2 = 0x7E; p.max_speed = 3; CHECK(pcie_speed_vector(&p) == 0x0E);           /* LnkCap 최대 이하로 자름 */

        CHECK(link_triage(g_rp, 256, g_ssd, 4096, &r) == 0);
        CHECK(r.flags == LT_WIDTH_LOW && r.exp_speed == 4 && r.exp_width == 4 && r.act_speed == 4 && r.act_width == 2);
        triage_text(&r, g_rep, sizeof g_rep);
        printf("triage(가상 RP + 가상 SSD):\n%s", g_rep);
        CHECK(strstr(g_rep, "폭 저하") != NULL);

        CHECK(link_triage(g_qr, 4096, g_qn, 4096, &r) == 0);
        CHECK(r.flags == LT_OK && r.common_speed == 1 && r.exp_width == 1 && r.act_speed == 1 && r.act_width == 1);
        triage_text(&r, g_rep, sizeof g_rep);
        printf("triage(QEMU root port + QEMU nvme):\n%s", g_rep);

        memcpy(g_m, g_rp, 256); w16(g_m, 0x52, 0x1044);              /* RP: 2.5GT/s x4, DLActive- */
        CHECK(link_triage(g_m, 256, g_ssd, 4096, &r) == 0 && r.flags == LT_LINK_DOWN && r.act_width == 0);
        memcpy(g_m, g_rp, 256); w16(g_m, 0x52, 0x3041);              /* Gen1 x4 로 학습 */
        memcpy(g_m2, g_ssd, 4096); w16(g_m2, 0x82, 0x1041);
        CHECK(link_triage(g_m, 256, g_m2, 4096, &r) == 0 && r.flags == LT_SPEED_LOW && r.act_speed == 1);
        w16(g_m, 0x70, 0x0003); w16(g_m, 0x52, 0x3043); w16(g_m2, 0x82, 0x1043);   /* TLS 8GT/s, 실제 8GT/s x4 */
        CHECK(link_triage(g_m, 256, g_m2, 4096, &r) == 0 && r.flags == LT_TARGET_CAPPED && r.exp_speed == 3 && r.common_speed == 4);
        memcpy(g_m, g_rp, 256); w16(g_m, 0x52, 0x3824);              /* LT 비트 남음 */
        CHECK(link_triage(g_m, 256, g_ssd, 4096, &r) == 0 && r.flags == (LT_WIDTH_LOW | LT_TRAINING));
        memcpy(g_m2, g_ssd, 4096); w16(g_m2, 0x82, 0x1044);           /* EP 는 x4 라고, RP 는 x2 라고 */
        CHECK(link_triage(g_rp, 256, g_m2, 4096, &r) == 0 && r.flags == (LT_WIDTH_LOW | LT_MISMATCH));
        memcpy(g_m2, g_ssd, 4096); w16(g_m2, 0x80, 0x0143);           /* EP 가 지원 안 하는 L0s 까지 켬 */
        CHECK(link_triage(g_rp, 256, g_m2, 4096, &r) == 0 && (r.flags & LT_ASPM_BAD));
        memcpy(g_m2, g_ssd, 4096); w16(g_m2, 0x06, 0);                /* EP 에 cap 리스트 없음 */
        CHECK(link_triage(g_rp, 256, g_m2, 4096, &r) == -1 && r.flags == LT_NO_PCIE);

        cap_report(g_ssd, 4096, g_rep, sizeof g_rep);
        printf("cap_report(가상 SSD):\n%s", g_rep);
        CHECK(strstr(g_rep, "LnkSta: Speed 16GT/s, Width x2 (below own cap)") != NULL);
        cap_report(g_qr, 4096, g_rep, sizeof g_rep);
        printf("cap_report(QEMU root port 00:02.0):\n%s", g_rep);
        CHECK(strstr(g_rep, "Width x32") != NULL && strstr(g_rep, "[148 v1] Access Control Services") != NULL);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
