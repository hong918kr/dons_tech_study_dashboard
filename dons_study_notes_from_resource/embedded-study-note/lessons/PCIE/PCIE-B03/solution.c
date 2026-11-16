/* PCIE-B03 Configuration Space 해부: Type 0 헤더
 *
 * lspci -xxx (256B) / -xxxx (4KB) 텍스트 → config 바이트 배열 → Type 0 헤더 디코드 → 진단 문장.
 *   config space = little-endian 바이트 배열. 읽기는 바이트를 조립한다 (정렬 안 된 포인터 캐스트 금지).
 *   상수 이름·값은 Linux include/uapi/linux/pci_regs.h 와 같다 (맥에서 헤더 없이 빌드되도록 직접 정의).
 *
 * Part A: cfg_rd16/32, 덤프 한 줄 파서, Type 0 디코드(VID/DID/Command/Status/Class/Header Type/BAR raw/
 *         Subsystem/CapPtr/INTx), lspci 와 같은 Control:/Status: 플래그 문자열
 * Part B: 블록 파서의 오류 처리(빈 입력·잘림·문법·offset·4KB 초과·용량), 진단 비트 (Mem-/BusMaster-/에러 비트 …)
 * Part C: lspci -xxx 전체 출력(여러 장치) 스캔 + class 로 NVMe 찾기, 드라이버 probe 의 Command 활성화(W1C 함정)
 *
 * 테스트 데이터: Lima VM(Apple Virtualization.framework) 실측 lspci 출력, QEMU 게스트(pcie-root-port + nvme) 실측
 *   sysfs config 바이트, 손으로 만든 가상 덤프. 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- pci_regs.h 와 같은 이름/값 ---- */
#define PCI_CFG_SPACE_SIZE          256
#define PCI_CFG_SPACE_EXP_SIZE      4096
#define PCI_STD_HEADER_SIZEOF       64
#define PCI_STD_NUM_BARS            6
#define PCI_VENDOR_ID               0x00
#define PCI_DEVICE_ID               0x02
#define PCI_COMMAND                 0x04
#define  PCI_COMMAND_IO             0x1
#define  PCI_COMMAND_MEMORY         0x2
#define  PCI_COMMAND_MASTER         0x4
#define  PCI_COMMAND_SPECIAL        0x8
#define  PCI_COMMAND_INVALIDATE     0x10
#define  PCI_COMMAND_VGA_PALETTE    0x20
#define  PCI_COMMAND_PARITY         0x40
#define  PCI_COMMAND_WAIT           0x80
#define  PCI_COMMAND_SERR           0x100
#define  PCI_COMMAND_FAST_BACK      0x200
#define  PCI_COMMAND_INTX_DISABLE   0x400
#define PCI_STATUS                  0x06
#define  PCI_STATUS_INTERRUPT       0x08
#define  PCI_STATUS_CAP_LIST        0x10
#define  PCI_STATUS_66MHZ           0x20
#define  PCI_STATUS_UDF             0x40
#define  PCI_STATUS_FAST_BACK       0x80
#define  PCI_STATUS_PARITY          0x100
#define  PCI_STATUS_DEVSEL_MASK     0x600
#define  PCI_STATUS_DEVSEL_FAST     0x000
#define  PCI_STATUS_DEVSEL_MEDIUM   0x200
#define  PCI_STATUS_DEVSEL_SLOW     0x400
#define  PCI_STATUS_SIG_TARGET_ABORT 0x800
#define  PCI_STATUS_REC_TARGET_ABORT 0x1000
#define  PCI_STATUS_REC_MASTER_ABORT 0x2000
#define  PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define  PCI_STATUS_DETECTED_PARITY 0x8000
#define PCI_CLASS_REVISION          0x08
#define PCI_REVISION_ID             0x08
#define PCI_CLASS_PROG              0x09
#define PCI_CLASS_DEVICE            0x0a
#define PCI_CACHE_LINE_SIZE         0x0c
#define PCI_LATENCY_TIMER           0x0d
#define PCI_HEADER_TYPE             0x0e
#define  PCI_HEADER_TYPE_MASK       0x7f
#define  PCI_HEADER_TYPE_NORMAL     0
#define  PCI_HEADER_TYPE_BRIDGE     1
#define  PCI_HEADER_TYPE_CARDBUS    2
#define  PCI_HEADER_TYPE_MFD        0x80
#define PCI_BIST                    0x0f
#define PCI_BASE_ADDRESS_0          0x10
#define  PCI_BASE_ADDRESS_SPACE_IO  0x01
#define  PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define  PCI_BASE_ADDRESS_MEM_TYPE_32   0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_1M   0x02
#define  PCI_BASE_ADDRESS_MEM_TYPE_64   0x04
#define  PCI_BASE_ADDRESS_MEM_PREFETCH  0x08
#define  PCI_BASE_ADDRESS_MEM_MASK  (~0x0fU)
#define  PCI_BASE_ADDRESS_IO_MASK   (~0x03U)
#define PCI_CARDBUS_CIS             0x28
#define PCI_SUBSYSTEM_VENDOR_ID     0x2c
#define PCI_SUBSYSTEM_ID            0x2e
#define PCI_ROM_ADDRESS             0x30
#define PCI_CAPABILITY_LIST         0x34
#define PCI_INTERRUPT_LINE          0x3c
#define PCI_INTERRUPT_PIN           0x3d
#define PCI_MIN_GNT                 0x3e
#define PCI_MAX_LAT                 0x3f
#define PCI_CLASS_STORAGE_EXPRESS   0x010802   /* include/linux/pci_ids.h, nvme 드라이버가 class 로 매칭 */

/* 이 레슨의 모델 상수 (pci_regs.h 에 없는 것) */
#define STATUS_W1C_MASK  (PCI_STATUS_PARITY | PCI_STATUS_SIG_TARGET_ABORT | PCI_STATUS_REC_TARGET_ABORT | \
                          PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY) /* 0xF900 */
#define COMMAND_RW_MASK  0x07FFu   /* 15:11 reserved */

enum { CFG_OK = 0, CFG_ERR_ARG = -1, CFG_ERR_EMPTY = -2, CFG_ERR_SYNTAX = -3, CFG_ERR_OFFSET = -4,
       CFG_ERR_TRUNC = -5, CFG_ERR_RANGE = -6, CFG_ERR_CAP = -7 };
enum { HDR_ERR_ARG = -1, HDR_OK = 0, HDR_NO_DEVICE = 1, HDR_TYPE1 = 2, HDR_TYPE2 = 3, HDR_BAD_TYPE = 4 };

/* ================= Part A: 바이트 조립 ================= */

static uint8_t cfg_rd8(const uint8_t *c, uint16_t off) { return c[off]; }

static uint16_t cfg_rd16(const uint8_t *c, uint16_t off) {
    return (uint16_t)(c[off] | (c[off + 1] << 8));                  /* TODO A1: little-endian 2바이트 조립 */
}

static uint32_t cfg_rd32(const uint8_t *c, uint16_t off) {
    return (uint32_t)c[off] | ((uint32_t)c[off + 1] << 8) |
           ((uint32_t)c[off + 2] << 16) | ((uint32_t)c[off + 3] << 24); /* TODO A1: little-endian 4바이트 조립 */
}

/* ================= 텍스트 줄 도우미 (주어짐) ================= */

typedef struct { const char *p; size_t n; } span;

/* txt[*pos..len) 에서 한 줄을 꺼낸다. 끝의 \r, 공백, 탭은 잘라낸다. len 을 절대 넘지 않는다. */
static bool next_line(const char *txt, size_t len, size_t *pos, span *ln) {
    if (*pos >= len) return false;
    size_t s = *pos, e = s;
    while (e < len && txt[e] != '\n') e++;
    ln->p = txt + s;
    ln->n = e - s;
    *pos = (e < len) ? e + 1 : e;
    while (ln->n > 0 && (ln->p[ln->n - 1] == '\r' || ln->p[ln->n - 1] == ' ' || ln->p[ln->n - 1] == '\t')) ln->n--;
    return true;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex2(const char *p) {             /* 두 글자 → 0..255, 아니면 -1 */
    int h = hexval(p[0]), l = hexval(p[1]);
    return (h < 0 || l < 0) ? -1 : (h << 4 | l);
}

typedef struct { bool valid; uint16_t domain; uint8_t bus, dev, fn; } pci_bdf;

/* 장치 제목 줄 "[dddd:]bb:dd.f ..." 인지 본다. 1 = 제목(bdf 채움), 0 = 제목 아님, -1 = 제목 꼴인데 값이 틀림.
 * 덤프 줄 "00: f4 1a ..." 는 [3] 이 ' ' 이고, "100: ..." 는 [3] 이 ':' 이라 "bb:dd." 꼴과 겹치지 않는다. */
static int parse_bdf_header(span ln, pci_bdf *b) {
    const char *p = ln.p;
    size_t n = ln.n, k = 0;
    uint16_t dom = 0;
    if (n >= 5 && p[4] == ':' && hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && hexval(p[2]) >= 0 && hexval(p[3]) >= 0) {
        dom = (uint16_t)(hex2(p) << 8 | hex2(p + 2));
        k = 5;
    }
    if (n < k + 7 || p[k + 2] != ':' || p[k + 5] != '.') return 0;
    int bus = hex2(p + k), dev = hex2(p + k + 3);
    if (bus < 0 || dev < 0) return 0;
    char f = p[k + 6];
    if (dev > 0x1f || f < '0' || f > '7') return -1;          /* device 0..31, function 0..7 */
    if (n > k + 7 && p[k + 7] != ' ') return -1;
    b->valid = true; b->domain = dom; b->bus = (uint8_t)bus; b->dev = (uint8_t)dev; b->fn = (uint8_t)(f - '0');
    return 1;
}

/* ================= Part A: 덤프 한 줄 ================= */

/* "XX: b0 b1 ... b15" (offset 은 hex 2~4자리, 콜론, 그 뒤 정확히 " hh" × 16). 성공 0, 아니면 CFG_ERR_SYNTAX. */
static int parse_dump_line(span ln, uint16_t *off, uint8_t out[16]) {
    size_t i = 0;
    unsigned v = 0;                                           /* TODO A2 */
    while (i < ln.n && i < 4 && hexval(ln.p[i]) >= 0) v = v << 4 | (unsigned)hexval(ln.p[i++]);
    if (i < 2 || i >= ln.n || ln.p[i] != ':') return CFG_ERR_SYNTAX;
    i++;
    for (int k = 0; k < 16; k++) {
        if (i + 3 > ln.n || ln.p[i] != ' ') return CFG_ERR_SYNTAX;
        int b = hex2(ln.p + i + 1);
        if (b < 0) return CFG_ERR_SYNTAX;
        out[k] = (uint8_t)b;
        i += 3;
    }
    if (i != ln.n) return CFG_ERR_SYNTAX;                    /* 17번째 바이트나 쓰레기 */
    *off = (uint16_t)v;
    return 0;
}

/* ================= Part B: 블록 파서 ================= */

typedef struct {
    size_t   n;          /* 채운 config 바이트 수 (성공이면 256 또는 4096) */
    size_t   consumed;   /* 이 블록이 쓴 텍스트 길이 — 다음 장치는 txt + consumed 부터 */
    unsigned line;       /* 오류면 그 줄 번호(1부터), 성공이면 읽은 줄 수 */
    pci_bdf  bdf;        /* 제목 줄이 있었으면 valid */
} cfg_info;

/* 장치 하나 분량의 lspci -xxx/-xxxx 텍스트를 cfg[0..n) 에 채운다. txt 는 NUL 종료가 아니어도 된다(len 까지만 읽음).
 * 블록 = [빈 줄들] [제목 줄] [탭으로 시작하는 -v 줄들] 덤프 줄들, 그리고 빈 줄 / 다음 제목 줄 / 입력 끝에서 끝난다.
 * 반환: CFG_OK, CFG_ERR_ARG / EMPTY / SYNTAX / OFFSET(순서·누락·중복) / TRUNC(256·4096 이 아님) / RANGE(0x1000 이상) / CAP */
static int cfg_parse_text(const char *txt, size_t len, uint8_t *cfg, size_t cap, cfg_info *info) {
    if (!txt || !cfg || !info) return CFG_ERR_ARG;
    memset(info, 0, sizeof *info);
    size_t pos = 0, n = 0;                                   /* TODO B1 */
    unsigned lineno = 0;
    bool started = false;                                    /* 제목 또는 덤프 줄을 봤나 */
    span ln;
    for (;;) {
        size_t line_start = pos;
        if (!next_line(txt, len, &pos, &ln)) break;
        lineno++;
        if (ln.n == 0) {                                     /* 빈 줄: 시작 전이면 건너뛰고, 시작 후면 블록 끝 */
            if (started) break;
            continue;
        }
        pci_bdf b;
        memset(&b, 0, sizeof b);
        int h = parse_bdf_header(ln, &b);
        if (h < 0) { info->line = lineno; return CFG_ERR_SYNTAX; }
        if (h > 0) {
            if (started) { pos = line_start; lineno--; break; }   /* 다음 장치의 제목 → 여기서 끝, 그 줄은 안 먹음 */
            info->bdf = b;
            started = true;
            continue;
        }
        if (ln.p[0] == '\t' && n == 0) { started = true; continue; }   /* lspci -v 정보 줄 */
        uint16_t off;
        uint8_t row[16];
        if (parse_dump_line(ln, &off, row) != 0) { info->line = lineno; return CFG_ERR_SYNTAX; }
        if (off != n) { info->line = lineno; return CFG_ERR_OFFSET; }
        if (n + 16 > PCI_CFG_SPACE_EXP_SIZE) { info->line = lineno; return CFG_ERR_RANGE; }
        if (n + 16 > cap) { info->line = lineno; return CFG_ERR_CAP; }
        memcpy(cfg + n, row, 16);
        n += 16;
        started = true;
    }
    info->n = n;
    info->consumed = pos;
    info->line = lineno;
    if (n == 0) return CFG_ERR_EMPTY;
    if (n != PCI_CFG_SPACE_SIZE && n != PCI_CFG_SPACE_EXP_SIZE) return CFG_ERR_TRUNC;
    return CFG_OK;
}

/* ================= Part A: Type 0 디코드 ================= */

typedef enum { BAR_UNUSED, BAR_IO, BAR_MEM32, BAR_MEM1M, BAR_MEM64, BAR_MEM64_HI, BAR_BAD } bar_kind;

typedef struct {
    uint32_t raw;        /* 덤프에 있는 값 그대로 (사이징은 PCIE-B05) */
    bar_kind kind;
    bool     prefetch;
    uint64_t base;       /* 타입 비트를 지운 주소. MEM64 는 위 BAR 와 합친 64비트 */
} pci_bar;

typedef struct {
    uint16_t vid, did, cmd, sts;
    uint8_t  rev, prog_if, subclass, base_class;
    uint8_t  cache_line, latency, header_raw, bist;
    uint8_t  layout;           /* header_raw & 0x7f : 0 = Type 0, 1 = Type 1, 2 = CardBus */
    bool     multifunction;    /* header_raw bit 7 */
    bool     ext;              /* 4KB 덤프 (PCIe / PCI-X Mode 2) */
    /* 아래는 Type 0 일 때만 채운다 */
    pci_bar  bar[PCI_STD_NUM_BARS];
    uint32_t cardbus_cis, rom;
    uint16_t ss_vid, ss_id;
    uint8_t  cap_ptr, int_line, int_pin, min_gnt, max_lat;
} pci_t0;

/* 0x00..0x0F 공통 필드는 항상 채운다. VID 0xFFFF/0x0000 → HDR_NO_DEVICE, layout 1 → HDR_TYPE1 (PCIE-B07),
 * 2 → HDR_TYPE2, 3..0x7f → HDR_BAD_TYPE (이들은 0x10 이후를 해석하지 않는다). Type 0 이면 BAR·나머지까지 → HDR_OK. */
static int pci_decode(const uint8_t *cfg, size_t n, pci_t0 *h) {
    if (!h) return HDR_ERR_ARG;
    memset(h, 0, sizeof *h);
    if (!cfg || n < PCI_STD_HEADER_SIZEOF) return HDR_ERR_ARG;
    h->vid = cfg_rd16(cfg, PCI_VENDOR_ID);                   /* TODO A3 */
    h->did = cfg_rd16(cfg, PCI_DEVICE_ID);
    h->cmd = cfg_rd16(cfg, PCI_COMMAND);
    h->sts = cfg_rd16(cfg, PCI_STATUS);
    h->rev = cfg_rd8(cfg, PCI_REVISION_ID);
    h->prog_if = cfg_rd8(cfg, PCI_CLASS_PROG);
    h->subclass = cfg_rd8(cfg, PCI_CLASS_DEVICE);
    h->base_class = cfg_rd8(cfg, PCI_CLASS_DEVICE + 1);
    h->cache_line = cfg_rd8(cfg, PCI_CACHE_LINE_SIZE);
    h->latency = cfg_rd8(cfg, PCI_LATENCY_TIMER);
    h->header_raw = cfg_rd8(cfg, PCI_HEADER_TYPE);
    h->bist = cfg_rd8(cfg, PCI_BIST);
    h->layout = h->header_raw & PCI_HEADER_TYPE_MASK;
    h->multifunction = (h->header_raw & PCI_HEADER_TYPE_MFD) != 0;
    h->ext = n >= PCI_CFG_SPACE_EXP_SIZE;
    if (h->vid == 0xFFFF || h->vid == 0x0000) return HDR_NO_DEVICE;
    if (h->layout == PCI_HEADER_TYPE_BRIDGE) return HDR_TYPE1;
    if (h->layout == PCI_HEADER_TYPE_CARDBUS) return HDR_TYPE2;
    if (h->layout != PCI_HEADER_TYPE_NORMAL) return HDR_BAD_TYPE;
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        pci_bar *b = &h->bar[i];
        if (i > 0 && h->bar[i - 1].kind == BAR_MEM64) continue;        /* 위 절반: 앞 BAR 가 이미 채움 */
        b->raw = cfg_rd32(cfg, (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i));
        if (b->raw == 0) { b->kind = BAR_UNUSED; continue; }
        if (b->raw & PCI_BASE_ADDRESS_SPACE_IO) { b->kind = BAR_IO; b->base = b->raw & PCI_BASE_ADDRESS_IO_MASK; continue; }
        b->prefetch = (b->raw & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0;
        b->base = b->raw & PCI_BASE_ADDRESS_MEM_MASK;
        switch (b->raw & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
        case PCI_BASE_ADDRESS_MEM_TYPE_32: b->kind = BAR_MEM32; break;
        case PCI_BASE_ADDRESS_MEM_TYPE_1M: b->kind = BAR_MEM1M; break;
        case PCI_BASE_ADDRESS_MEM_TYPE_64:
            if (i == PCI_STD_NUM_BARS - 1) { b->kind = BAR_BAD; break; }   /* 위 절반이 없다 */
            b->kind = BAR_MEM64;
            h->bar[i + 1].raw = cfg_rd32(cfg, (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * (i + 1)));
            h->bar[i + 1].kind = BAR_MEM64_HI;
            b->base |= (uint64_t)h->bar[i + 1].raw << 32;
            break;
        default: b->kind = BAR_BAD; break;                               /* 11 = reserved */
        }
    }
    h->cardbus_cis = cfg_rd32(cfg, PCI_CARDBUS_CIS);
    h->ss_vid = cfg_rd16(cfg, PCI_SUBSYSTEM_VENDOR_ID);
    h->ss_id = cfg_rd16(cfg, PCI_SUBSYSTEM_ID);
    h->rom = cfg_rd32(cfg, PCI_ROM_ADDRESS);
    h->cap_ptr = cfg_rd8(cfg, PCI_CAPABILITY_LIST);
    h->int_line = cfg_rd8(cfg, PCI_INTERRUPT_LINE);
    h->int_pin = cfg_rd8(cfg, PCI_INTERRUPT_PIN);
    h->min_gnt = cfg_rd8(cfg, PCI_MIN_GNT);
    h->max_lat = cfg_rd8(cfg, PCI_MAX_LAT);
    return HDR_OK;
}

/* class 이름 표 (주어짐). sub/pi 가 -1 이면 "아무 값". 위에서부터 첫 일치. */
static const struct { uint8_t base; int16_t sub, pi; const char *name; } k_class[] = {
    { 0x01, 0x08, 0x02, "Non-Volatile memory controller: NVM Express (NVMe)" },
    { 0x01, 0x08,   -1, "Non-Volatile memory controller" },
    { 0x01, 0x06, 0x01, "SATA controller: AHCI" },
    { 0x01, 0x00,   -1, "SCSI storage controller" },
    { 0x01, 0x80,   -1, "Mass storage controller (other)" },
    { 0x01,   -1,   -1, "Mass storage controller" },
    { 0x02, 0x00,   -1, "Ethernet controller" },
    { 0x02,   -1,   -1, "Network controller" },
    { 0x03, 0x00,   -1, "VGA compatible controller" },
    { 0x03,   -1,   -1, "Display controller" },
    { 0x05,   -1,   -1, "Memory controller" },
    { 0x06, 0x00,   -1, "Host bridge" },
    { 0x06, 0x01,   -1, "ISA bridge" },
    { 0x06, 0x04,   -1, "PCI bridge" },
    { 0x06,   -1,   -1, "Bridge" },
    { 0x07,   -1,   -1, "Communication controller" },
    { 0x0c, 0x03, 0x30, "USB controller: xHCI" },
    { 0x0c, 0x03,   -1, "USB controller" },
    { 0x0c,   -1,   -1, "Serial bus controller" },
    { 0x10,   -1,   -1, "Encryption controller" },
    { 0x12,   -1,   -1, "Processing accelerator" },
    { 0xff,   -1,   -1, "Unassigned class" },
};

static const char *pci_class_name(uint8_t base, uint8_t sub, uint8_t pi) {
    for (size_t i = 0; i < sizeof k_class / sizeof k_class[0]; i++)
        if (k_class[i].base == base && (k_class[i].sub < 0 || k_class[i].sub == sub) &&
            (k_class[i].pi < 0 || k_class[i].pi == pi))
            return k_class[i].name;
    return "Unknown class";
}

/* lspci -vvv 의 "Control:" / "Status:" 뒤 문자열과 같은 모양 (pciutils 3.14 실측 출력과 비교해 테스트한다). */
static const struct { uint16_t bit; const char *name; } k_cmd_flags[] = {
    { PCI_COMMAND_IO, "I/O" }, { PCI_COMMAND_MEMORY, "Mem" }, { PCI_COMMAND_MASTER, "BusMaster" },
    { PCI_COMMAND_SPECIAL, "SpecCycle" }, { PCI_COMMAND_INVALIDATE, "MemWINV" }, { PCI_COMMAND_VGA_PALETTE, "VGASnoop" },
    { PCI_COMMAND_PARITY, "ParErr" }, { PCI_COMMAND_WAIT, "Stepping" }, { PCI_COMMAND_SERR, "SERR" },
    { PCI_COMMAND_FAST_BACK, "FastB2B" }, { PCI_COMMAND_INTX_DISABLE, "DisINTx" },
};
static const struct { uint16_t bit; const char *name; } k_sts_lo[] = {   /* DEVSEL 앞 */
    { PCI_STATUS_CAP_LIST, "Cap" }, { PCI_STATUS_66MHZ, "66MHz" }, { PCI_STATUS_UDF, "UDF" },
    { PCI_STATUS_FAST_BACK, "FastB2B" }, { PCI_STATUS_PARITY, "ParErr" },
};
static const struct { uint16_t bit; const char *name; } k_sts_hi[] = {   /* DEVSEL 뒤 */
    { PCI_STATUS_SIG_TARGET_ABORT, ">TAbort" }, { PCI_STATUS_REC_TARGET_ABORT, "<TAbort" },
    { PCI_STATUS_REC_MASTER_ABORT, "<MAbort" }, { PCI_STATUS_SIG_SYSTEM_ERROR, ">SERR" },
    { PCI_STATUS_DETECTED_PARITY, "<PERR" }, { PCI_STATUS_INTERRUPT, "INTx" },
};

/* 둘 다 snprintf 로만 쓴다. 반환: 쓴 길이 (잘리면 cap-1 까지). */
static size_t pci_cmd_str(uint16_t cmd, char *out, size_t cap) {
    size_t w = 0;                                            /* TODO A4 */
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof k_cmd_flags / sizeof k_cmd_flags[0]; i++) {
        int r = snprintf(out + w, cap - w, "%s%s%c", i ? " " : "", k_cmd_flags[i].name, (cmd & k_cmd_flags[i].bit) ? '+' : '-');
        if (r < 0 || (size_t)r >= cap - w) return cap - 1;
        w += (size_t)r;
    }
    return w;
}

static size_t pci_sts_str(uint16_t sts, char *out, size_t cap) {
    size_t w = 0;                                            /* TODO A4 */
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    static const char *devsel[4] = { "fast", "medium", "slow", "??" };
    for (size_t i = 0; i < sizeof k_sts_lo / sizeof k_sts_lo[0]; i++) {
        int r = snprintf(out + w, cap - w, "%s%s%c", i ? " " : "", k_sts_lo[i].name, (sts & k_sts_lo[i].bit) ? '+' : '-');
        if (r < 0 || (size_t)r >= cap - w) return cap - 1;
        w += (size_t)r;
    }
    int r = snprintf(out + w, cap - w, " DEVSEL=%s", devsel[(sts & PCI_STATUS_DEVSEL_MASK) >> 9]);
    if (r < 0 || (size_t)r >= cap - w) return cap - 1;
    w += (size_t)r;
    for (size_t i = 0; i < sizeof k_sts_hi / sizeof k_sts_hi[0]; i++) {
        r = snprintf(out + w, cap - w, " %s%c", k_sts_hi[i].name, (sts & k_sts_hi[i].bit) ? '+' : '-');
        if (r < 0 || (size_t)r >= cap - w) return cap - 1;
        w += (size_t)r;
    }
    return w;
}

/* ================= Part B: 진단 ================= */

enum {
    DIAG_MEM_OFF  = 1u << 0,  DIAG_IO_OFF  = 1u << 1,  DIAG_BM_OFF  = 1u << 2,  DIAG_INTX_OFF = 1u << 3,
    DIAG_MDPE     = 1u << 4,  DIAG_STA     = 1u << 5,  DIAG_RTA     = 1u << 6,  DIAG_RMA      = 1u << 7,
    DIAG_SSE      = 1u << 8,  DIAG_DPE     = 1u << 9,  DIAG_CAP_BAD = 1u << 10, DIAG_NO_CAPS  = 1u << 11,
    DIAG_BAR_BAD  = 1u << 12, DIAG_PIN_BAD = 1u << 13,
};
#define DIAG_ERRBITS (DIAG_MDPE | DIAG_STA | DIAG_RTA | DIAG_RMA | DIAG_SSE | DIAG_DPE)

static bool has_bar_kind(const pci_t0 *h, bar_kind a, bar_kind b) {
    for (int i = 0; i < PCI_STD_NUM_BARS; i++)
        if (h->bar[i].kind == a || h->bar[i].kind == b) return true;
    return false;
}

/* pci_decode 가 HDR_OK 를 준 h 에 대해 진단 비트를 돌려준다.
 *  MEM_OFF  : Command.Memory = 0                    BM_OFF : Command.BusMaster = 0
 *  IO_OFF   : Command.IO = 0 이고 IO BAR 가 있음     INTX_OFF : DisINTx = 1 이고 Interrupt Pin 1..4
 *  MDPE/STA/RTA/RMA/SSE/DPE : Status 에러 비트 각각 (0x100/0x800/0x1000/0x2000/0x4000/0x8000)
 *  CAP_BAD  : Status.Cap = 1 인데 (CapPtr & ~3) < 0x40    NO_CAPS : 4KB 덤프인데 Status.Cap = 0
 *  BAR_BAD  : BAR 타입 11(reserved) 또는 BAR5 의 64비트      PIN_BAD : Interrupt Pin > 4 */
static uint32_t pci_diagnose(const pci_t0 *h) {
    uint32_t d = 0;                                          /* TODO B2 */
    if (!(h->cmd & PCI_COMMAND_MEMORY)) d |= DIAG_MEM_OFF;
    if (!(h->cmd & PCI_COMMAND_MASTER)) d |= DIAG_BM_OFF;
    if (!(h->cmd & PCI_COMMAND_IO) && has_bar_kind(h, BAR_IO, BAR_IO)) d |= DIAG_IO_OFF;
    if ((h->cmd & PCI_COMMAND_INTX_DISABLE) && h->int_pin >= 1 && h->int_pin <= 4) d |= DIAG_INTX_OFF;
    if (h->sts & PCI_STATUS_PARITY) d |= DIAG_MDPE;
    if (h->sts & PCI_STATUS_SIG_TARGET_ABORT) d |= DIAG_STA;
    if (h->sts & PCI_STATUS_REC_TARGET_ABORT) d |= DIAG_RTA;
    if (h->sts & PCI_STATUS_REC_MASTER_ABORT) d |= DIAG_RMA;
    if (h->sts & PCI_STATUS_SIG_SYSTEM_ERROR) d |= DIAG_SSE;
    if (h->sts & PCI_STATUS_DETECTED_PARITY) d |= DIAG_DPE;
    if ((h->sts & PCI_STATUS_CAP_LIST) && (h->cap_ptr & ~3u) < PCI_STD_HEADER_SIZEOF) d |= DIAG_CAP_BAD;
    if (!(h->sts & PCI_STATUS_CAP_LIST) && h->ext) d |= DIAG_NO_CAPS;
    if (has_bar_kind(h, BAR_BAD, BAR_BAD)) d |= DIAG_BAR_BAD;
    if (h->int_pin > 4) d |= DIAG_PIN_BAD;
    return d;
}

/* 진단 문장 표 (주어짐) — 출력에 이 문장이 그대로 들어간다 */
static const struct { uint32_t bit; const char *text; } k_diag_text[] = {
    { DIAG_MEM_OFF,  "Mem- : Memory Space 꺼짐 → BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF" },
    { DIAG_IO_OFF,   "I/O- : IO BAR 가 있는데 IO Space 꺼짐 → IO 포트 접근에 응답 없음" },
    { DIAG_BM_OFF,   "BusMaster- : 장치가 DMA/MSI 못 보냄 (MSI 도 장치가 보내는 메모리 쓰기)" },
    { DIAG_INTX_OFF, "DisINTx+ : INTx 꺼짐 → MSI/MSI-X 를 쓰는 중일 것 (PCIE-B04, PCIE-M10)" },
    { DIAG_MDPE,     "ParErr+ : Master Data Parity Error — 요청자로서 poisoned 완료를 받았거나 poisoned 쓰기를 보냄" },
    { DIAG_STA,      ">TAbort+ : Signaled Target Abort — 이 장치가 요청을 CA(Completer Abort)로 거절함" },
    { DIAG_RTA,      "<TAbort+ : Received Target Abort — 이 장치의 요청이 CA 완료로 돌아옴" },
    { DIAG_RMA,      "<MAbort+ : Received Master Abort — 이 장치의 요청이 UR 로 끝남 (잘못된 DMA 주소?)" },
    { DIAG_SSE,      ">SERR+ : Signaled System Error — ERR_FATAL/ERR_NONFATAL 을 보냄, AER 확인 (PCIE-M12)" },
    { DIAG_DPE,      "<PERR+ : Detected Parity Error — poisoned TLP 를 받음" },
    { DIAG_CAP_BAD,  "Cap+ 인데 CapPtr < 0x40 → 깨진 포인터, capability 순회 금지 (PCIE-B04)" },
    { DIAG_NO_CAPS,  "4KB(PCIe) 덤프인데 Cap- → PCIe Capability 필수 위반 또는 덤프 오류" },
    { DIAG_BAR_BAD,  "BAR 타입 비트 이상 (reserved 11 또는 BAR5 의 64비트) → 덤프/장치 확인" },
    { DIAG_PIN_BAD,  "Interrupt Pin > 4 → 잘못된 값 (0 = 없음, 1..4 = INTA..INTD)" },
};

/* ---- 보고서 (주어짐): snprintf 누적, 넘치면 -1 ---- */
typedef struct { char *buf; size_t cap, len; bool overflow; } sbuf;

__attribute__((format(printf, 2, 3)))
static void sb_printf(sbuf *s, const char *fmt, ...) {
    if (s->overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= s->cap - s->len) { s->overflow = true; s->buf[s->cap - 1] = '\0'; return; }
    s->len += (size_t)r;
}

static void bar_desc(const pci_bar *b, int i, const pci_bar *prev, sbuf *s) {
    sb_printf(s, "  BAR%d    : 0x%08x  ", i, (unsigned)b->raw);
    switch (b->kind) {
    case BAR_UNUSED:   sb_printf(s, "unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)\n"); break;
    case BAR_IO:       sb_printf(s, "I/O ports at %04llx\n", (unsigned long long)b->base); break;
    case BAR_MEM32:    sb_printf(s, "Memory at %08llx (32-bit, %s)\n", (unsigned long long)b->base, b->prefetch ? "prefetchable" : "non-prefetchable"); break;
    case BAR_MEM1M:    sb_printf(s, "Memory at %08llx (below-1M, obsolete)\n", (unsigned long long)b->base); break;
    case BAR_MEM64:    sb_printf(s, "Memory at %08llx (64-bit, %s)\n", (unsigned long long)b->base, b->prefetch ? "prefetchable" : "non-prefetchable"); break;
    case BAR_MEM64_HI: sb_printf(s, "(BAR%d 의 상위 32비트%s)\n", i - 1, prev && prev->kind == BAR_MEM64 ? "" : "?"); break;
    case BAR_BAD:      sb_printf(s, "BAD type bits\n"); break;
    }
}

static int pci_report(const uint8_t *cfg, size_t n, const pci_bdf *bdf, char *out, size_t cap) {
    if (!out || cap == 0) return CFG_ERR_ARG;
    sbuf s = { out, cap, 0, false };
    out[0] = '\0';
    pci_t0 h;
    int r = pci_decode(cfg, n, &h);
    if (bdf && bdf->valid) sb_printf(&s, "[%02x:%02x.%x] ", bdf->bus, bdf->dev, bdf->fn);
    if (r == HDR_ERR_ARG) { sb_printf(&s, "덤프 없음/64B 미만\n"); return s.overflow ? CFG_ERR_CAP : (int)s.len; }
    if (r == HDR_NO_DEVICE) {
        sb_printf(&s, "VID 0x%04x → 장치 없음 (%s)\n", h.vid, h.vid == 0xFFFF ?
                  "all-ones 읽기: 빈 슬롯, 링크 다운, 또는 응답 없는 config 요청" : "0x0000 은 유효한 VID 가 아님");
        return s.overflow ? CFG_ERR_CAP : (int)s.len;
    }
    sb_printf(&s, "%04x:%04x rev %02x  class %02x:%02x:%02x %s\n", h.vid, h.did, h.rev,
              h.base_class, h.subclass, h.prog_if, pci_class_name(h.base_class, h.subclass, h.prog_if));
    sb_printf(&s, "  Header  : 0x%02x = ", h.header_raw);
    if (r == HDR_TYPE1) { sb_printf(&s, "Type 1 (PCI-to-PCI bridge)%s → Type 1, PCIE-B07 참고 (여기서 멈춤)\n", h.multifunction ? ", multi-function" : ""); return s.overflow ? CFG_ERR_CAP : (int)s.len; }
    if (r == HDR_TYPE2) { sb_printf(&s, "Type 2 (CardBus bridge) → 이 파서 범위 밖 (여기서 멈춤)\n"); return s.overflow ? CFG_ERR_CAP : (int)s.len; }
    if (r == HDR_BAD_TYPE) { sb_printf(&s, "layout %u = 정의 안 된 Header Type → 덤프 확인 (여기서 멈춤)\n", h.layout); return s.overflow ? CFG_ERR_CAP : (int)s.len; }
    sb_printf(&s, "Type 0, %s\n", h.multifunction ? "multi-function (fn1..7 도 probe)" : "single-function");
    char fl[160];
    pci_cmd_str(h.cmd, fl, sizeof fl);
    sb_printf(&s, "  Control : 0x%04x  %s\n", h.cmd, fl);
    pci_sts_str(h.sts, fl, sizeof fl);
    sb_printf(&s, "  Status  : 0x%04x  %s\n", h.sts, fl);
    sb_printf(&s, "  CLS/Lat : 0x%02x / %u, BIST 0x%02x\n", h.cache_line, h.latency, h.bist);
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) bar_desc(&h.bar[i], i, i ? &h.bar[i - 1] : NULL, &s);
    sb_printf(&s, "  ROM     : 0x%08x (%s)\n", (unsigned)h.rom, (h.rom & 1) ? "enabled" : "disabled");
    sb_printf(&s, "  Subsys  : %04x:%04x\n", h.ss_vid, h.ss_id);
    sb_printf(&s, "  CapPtr  : 0x%02x%s\n", h.cap_ptr, (h.sts & PCI_STATUS_CAP_LIST) ? " (순회는 PCIE-B04)" : " (Cap- : 리스트 없음)");
    if (h.int_pin == 0) sb_printf(&s, "  INTx    : pin 없음, line 0x%02x\n", h.int_line);
    else if (h.int_pin <= 4) sb_printf(&s, "  INTx    : pin %c, line 0x%02x\n", 'A' + h.int_pin - 1, h.int_line);
    else sb_printf(&s, "  INTx    : pin %u(?), line 0x%02x\n", h.int_pin, h.int_line);
    if (h.ext) sb_printf(&s, "  Ext     : 4KB 덤프, 0x100 = 0x%08x (확장 cap 순회는 PCIE-B04)\n", (unsigned)cfg_rd32(cfg, 0x100));
    uint32_t d = pci_diagnose(&h);
    sb_printf(&s, "  진단    :%s\n", d ? "" : " 이상 없음");
    for (size_t i = 0; i < sizeof k_diag_text / sizeof k_diag_text[0]; i++)
        if (d & k_diag_text[i].bit) {
            sb_printf(&s, "   - %s", k_diag_text[i].text);
            if (k_diag_text[i].bit == DIAG_MEM_OFF && !has_bar_kind(&h, BAR_MEM32, BAR_MEM64) && !has_bar_kind(&h, BAR_MEM1M, BAR_MEM1M))
                sb_printf(&s, " (이 장치는 메모리 BAR 가 없어 영향 없음)");
            sb_printf(&s, "\n");
        }
    if (d & DIAG_ERRBITS) sb_printf(&s, "   - Status 에러 비트는 RW1C: 1 을 써야 지워진다 (BM-B04). 로그 남긴 뒤 지울 것\n");
    return s.overflow ? CFG_ERR_CAP : (int)s.len;
}

/* ================= Part C: 여러 장치 스캔 / probe ================= */

typedef struct {
    pci_bdf  bdf;
    uint16_t vid, did;
    uint32_t class24;    /* base<<16 | sub<<8 | prog_if */
    uint8_t  rev;
    int      hdr;        /* pci_decode 결과 */
    uint32_t diag;       /* HDR_OK 일 때만 */
    size_t   n;          /* 256 / 4096 */
} pci_summary;

/* lspci -xxx 전체 출력(장치 여러 개)을 앞에서부터 읽어 out[] 에 요약을 쌓는다. malloc 없음 (cfg 버퍼는 4KB 지역 배열).
 * 반환 CFG_OK (*count = 장치 수), 블록 하나라도 실패하면 그 오류 코드 (*count = 그 앞까지), max 초과 → CFG_ERR_CAP.
 * 끝에 빈 줄만 남으면 정상 종료. */
static int lspci_scan(const char *txt, size_t len, pci_summary *out, size_t max, size_t *count) {
    if (!txt || !out || !count) return CFG_ERR_ARG;
    *count = 0;                                              /* TODO C1 */
    size_t pos = 0;
    uint8_t cfg[PCI_CFG_SPACE_EXP_SIZE];
    while (pos < len) {
        cfg_info info;
        int rc = cfg_parse_text(txt + pos, len - pos, cfg, sizeof cfg, &info);
        if (rc == CFG_ERR_EMPTY && !info.bdf.valid) return CFG_OK;      /* 남은 건 빈 줄뿐 */
        if (rc != CFG_OK) return rc;
        if (*count >= max) return CFG_ERR_CAP;
        pci_summary *s = &out[*count];
        pci_t0 h;
        memset(s, 0, sizeof *s);
        s->bdf = info.bdf;
        s->n = info.n;
        s->hdr = pci_decode(cfg, info.n, &h);
        s->vid = h.vid; s->did = h.did; s->rev = h.rev;
        s->class24 = (uint32_t)h.base_class << 16 | (uint32_t)h.subclass << 8 | h.prog_if;
        if (s->hdr == HDR_OK) s->diag = pci_diagnose(&h);
        (*count)++;
        pos += info.consumed;
    }
    return CFG_OK;
}

static int find_by_class(const pci_summary *s, size_t n, uint32_t class24) {
    for (size_t i = 0; i < n; i++)
        if (s[i].hdr == HDR_OK && s[i].class24 == class24) return (int)i;
    return -1;
}

/* ---- config 쓰기 모델 (주어짐): Status 의 에러 비트는 RW1C, Command 는 15:11 reserved, 0x00..0x0F 의 나머지는 RO ---- */
static void cfg_wr16(uint8_t *cfg, uint16_t off, uint16_t val) {
    if (off & 1) return;                                     /* 정렬 안 된 16비트 쓰기는 무시 (모델) */
    if (off == PCI_STATUS) {
        uint16_t st = (uint16_t)(cfg[off] | cfg[off + 1] << 8);
        st = (uint16_t)(st & ~(val & STATUS_W1C_MASK));      /* 1 을 쓴 에러 비트만 지워진다 */
        cfg[off] = (uint8_t)st; cfg[off + 1] = (uint8_t)(st >> 8);
        return;
    }
    if (off == PCI_COMMAND) val &= COMMAND_RW_MASK;
    else if (off < PCI_BASE_ADDRESS_0) return;               /* VID/DID/class/header 등은 RO (모델 단순화) */
    cfg[off] = (uint8_t)val; cfg[off + 1] = (uint8_t)(val >> 8);
}

static void cfg_wr32(uint8_t *cfg, uint16_t off, uint32_t val) {  /* 32비트 쓰기 = 바이트 인에이블 4개 모두 켠 쓰기 */
    cfg_wr16(cfg, off, (uint16_t)val);
    cfg_wr16(cfg, (uint16_t)(off + 2), (uint16_t)(val >> 16));
}

/* 드라이버 probe 의 "pci_enable_device_mem() + pci_set_master()" 에 해당:
 * Type 0 장치(HDR_OK)만, want 는 IO|MEMORY|MASTER 의 부분집합만 허용. Command 를 16비트 RMW 로 켠다
 * (Status 를 건드리지 않도록 — 32비트 RMW 는 에러 비트를 지운다). 반환: 새 Command 값, 거절이면 -1. */
static int probe_enable(uint8_t *cfg, size_t n, uint16_t want) {
    pci_t0 h;                                                /* TODO C2 */
    if (want & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) return -1;
    if (pci_decode(cfg, n, &h) != HDR_OK) return -1;
    uint16_t cmd = cfg_rd16(cfg, PCI_COMMAND);
    if ((cmd | want) != cmd) cfg_wr16(cfg, PCI_COMMAND, (uint16_t)(cmd | want));
    return cfg_rd16(cfg, PCI_COMMAND);
}

/* 흔한 버그 (주어짐, 테스트에서 재현): 0x04 를 32비트로 읽어 OR 해서 다시 쓰면 Status 의 1 인 에러 비트가 그대로 1 로 써져 지워진다 */
static int probe_enable_buggy(uint8_t *cfg, size_t n, uint16_t want) {
    (void)n;
    uint32_t v = cfg_rd32(cfg, PCI_COMMAND);
    cfg_wr32(cfg, PCI_COMMAND, v | want);
    return cfg_rd16(cfg, PCI_COMMAND);
}

/* ================= 테스트 하네스 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* --- 실측 데이터 1: Lima VM (Apple Virtualization.framework), sudo lspci -xxx 출력에서 그대로 발췌 (pciutils 3.14) --- */
static const char VM_00_00[] =
    "00:00.0 Host bridge: Apple Inc. Device 1a05\n"
    "00: 6b 10 05 1a 10 00 00 00 00 00 00 06 00 00 00 00\n"
    "10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "30: 00 00 00 00 00 00 00 00 00 00 00 00 ff 00 00 00\n"
    "40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "\n";
static const char VM_00_06[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 09 50 10 01 00 00 00 00 00 00 00 00 38 00 00 00\n"
    "50: 09 60 10 03 00 00 00 00 00 10 00 00 01 00 00 00\n"
    "60: 09 74 14 02 00 00 00 00 00 40 00 00 04 00 00 00\n"
    "70: 04 00 00 00 09 84 10 04 00 00 00 00 00 80 00 00\n"
    "80: 3c 00 00 00 11 00 01 80 02 00 00 00 22 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "\n";
static const char VM_00_0C[] =
    "00:0c.0 USB controller: Apple Inc. Device 1a06\n"
    "00: 6b 10 06 1a 16 04 10 00 00 30 03 0c 10 40 00 00\n"
    "10: 00 00 00 50 00 10 00 50 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 11 4c 1f 80 01 00 00 00 01 02 00 00 05 00 8a 00\n"
    "50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "\n";
/* sudo lspci -vvv -xxx -s 00:06.0 실측 (탭으로 시작하는 -v 줄 + 덤프) */
static const char VM_VX_00_06[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "\tSubsystem: Red Hat, Inc. Device 0042\n"
    "\tControl: I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+\n"
    "\tStatus: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-\n"
    "\tLatency: 64\n"
    "\tInterrupt: pin A routed to IRQ 16\n"
    "\tRegion 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]\n"
    "\tRegion 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]\n"
    "\tCapabilities: [40] Vendor Specific Information: VirtIO: CommonCfg\n"
    "\t\tBAR=0 offset=00000000 size=00000038\n"
    "\tCapabilities: [50] Vendor Specific Information: VirtIO: ISR\n"
    "\t\tBAR=0 offset=00001000 size=00000001\n"
    "\tCapabilities: [60] Vendor Specific Information: VirtIO: Notify\n"
    "\t\tBAR=0 offset=00004000 size=00000004 multiplier=00000004\n"
    "\tCapabilities: [74] Vendor Specific Information: VirtIO: DeviceCfg\n"
    "\t\tBAR=0 offset=00008000 size=0000003c\n"
    "\tCapabilities: [84] MSI-X: Enable+ Count=2 Masked-\n"
    "\t\tVector table: BAR=2 offset=00000000\n"
    "\t\tPBA: BAR=2 offset=00000020\n"
    "\tKernel driver in use: virtio-pci\n"
    "\tKernel modules: virtio_pci\n"
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 09 50 10 01 00 00 00 00 00 00 00 00 38 00 00 00\n"
    "50: 09 60 10 03 00 00 00 00 00 10 00 00 01 00 00 00\n"
    "60: 09 74 14 02 00 00 00 00 00 40 00 00 04 00 00 00\n"
    "70: 04 00 00 00 09 84 10 04 00 00 00 00 00 80 00 00\n"
    "80: 3c 00 00 00 11 00 01 80 02 00 00 00 22 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "\n";
/* --- 실측 데이터 2: QEMU 게스트 (virt + pcie-root-port + nvme, Linux 6.18.55, nvme 드라이버 로드 전)
 *     /sys/bus/pci/devices/<BDF>/config 를 od 로 덤프 → offset 을 붙여 lspci -xxxx 형식으로 변환 (바이트는 그대로) --- */
static const char QEMU_00_01[] =     /* virtio-net (legacy 1af4:1000): IO BAR + Mem32 + 4GB 위 Mem64 prefetchable */
    "0000:00:01.0 QEMU guest sysfs config (class=0x020000 vendor=0x1af4 device=0x1000), converted from od\n"
    "00: f4 1a 00 10 07 04 10 00 00 00 00 02 00 00 00 00\n"
    "10: 01 20 00 00 00 00 28 10 00 00 00 00 00 00 00 00\n"
    "20: 0c 00 20 00 80 00 00 00 00 00 00 00 f4 1a 01 00\n"
    "30: 00 00 00 00 98 00 00 00 00 00 00 00 11 01 00 00\n"
    "40: 09 00 10 01 04 00 00 00 00 00 00 00 00 10 00 00\n"
    "50: 09 40 10 03 04 00 00 00 00 10 00 00 00 10 00 00\n"
    "60: 09 50 10 04 04 00 00 00 00 20 00 00 00 10 00 00\n"
    "70: 09 60 14 02 04 00 00 00 00 30 00 00 00 10 00 00\n"
    "80: 04 00 00 00 09 70 14 05 00 00 00 00 00 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 11 84 03 80 01 00 00 00\n"
    "a0: 01 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
static const char QEMU_00_02_256[] = /* pcie-root-port, Type 1 — 실제 4KB 중 첫 256B */
    "0000:00:02.0 QEMU guest sysfs config (class=0x060400 vendor=0x1b36 device=0x000c), converted from od\n"
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
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
static const char QEMU_01_00_256[] = /* nvme 1b36:0010 — 실제 4KB 중 첫 256B (0x100..0xFFF 는 실측 전부 0) */
    "0000:01:00.0 QEMU guest sysfs config (class=0x010802 vendor=0x1b36 device=0x0010), converted from od\n"
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
    "a0: 00 00 00 00 00 00 30 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";

static span sp(const char *s) { span r = { s, strlen(s) }; return r; }

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static size_t append(char *dst, size_t at, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (at + n >= cap) return at;
    memcpy(dst + at, src, n + 1);
    return at + n;
}

/* 바이트 → lspci -xxx/-xxxx 형식 텍스트 (오프셋 "%02x:" — 0x100 이상은 자연히 3자리). 테스트 입력 생성용 */
static size_t fmt_dump(const uint8_t *cfg, size_t from, size_t to, const char *title, char *out, size_t at, size_t cap) {
    char line[80];
    if (title) at = append(out, at, cap, title);
    for (size_t off = from; off < to; off += 16) {
        int w = snprintf(line, sizeof line, "%02zx:", off);
        for (int k = 0; k < 16; k++) w += snprintf(line + w, sizeof line - (size_t)w, " %02x", cfg[off + (size_t)k]);
        snprintf(line + w, sizeof line - (size_t)w, "\n");
        at = append(out, at, cap, line);
    }
    return at;
}

typedef struct {
    uint16_t vid, did, cmd, sts, ss_vid, ss_id;
    uint32_t cls24, bar[6];
    uint8_t  rev, hdr, cap_ptr, int_line, int_pin;
} t0_spec;

static void put16(uint8_t *c, size_t o, uint16_t v) { c[o] = (uint8_t)v; c[o + 1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *c, size_t o, uint32_t v) { put16(c, o, (uint16_t)v); put16(c, o + 2, (uint16_t)(v >> 16)); }

static void mk_cfg(uint8_t *c, size_t n, const t0_spec *s) {     /* 손으로 만드는 가상 덤프 */
    memset(c, 0, n);
    put16(c, 0x00, s->vid); put16(c, 0x02, s->did); put16(c, 0x04, s->cmd); put16(c, 0x06, s->sts);
    c[0x08] = s->rev; c[0x09] = (uint8_t)s->cls24; c[0x0a] = (uint8_t)(s->cls24 >> 8); c[0x0b] = (uint8_t)(s->cls24 >> 16);
    c[0x0e] = s->hdr;
    for (int i = 0; i < 6; i++) put32(c, (size_t)(0x10 + 4 * i), s->bar[i]);
    put16(c, 0x2c, s->ss_vid); put16(c, 0x2e, s->ss_id);
    c[0x34] = s->cap_ptr; c[0x3c] = s->int_line; c[0x3d] = s->int_pin;
}

/* 가상 덤프: Gen4 NVMe SSD 라고 가정한 장치 (VID 0xC0DE 는 지어낸 값). 드라이버가 Mem+BM 만 켰고(IO BAR 는 꺼진 채),
 * SSD 펌웨어의 DMA 하나가 매핑 안 된 호스트 주소로 가서 UR 을 받아 <MAbort+ 가 남은 상태 */
static const t0_spec SSD_SPEC = {
    .vid = 0xC0DE, .did = 0x5501, .cmd = 0x0406, .sts = 0x2010, .ss_vid = 0xC0DE, .ss_id = 0x0001,
    .cls24 = 0x010802, .bar = { 0xFE600004, 0x00000000, 0x0000E001, 0, 0, 0 },
    .rev = 0x01, .hdr = 0x00, .cap_ptr = 0x40, .int_line = 0x00, .int_pin = 1,
};

static char g_txt[1 << 16];
static char g_rep[4096];

/* 텍스트를 버퍼 끝에 붙여 두고 그 위치를 넘긴다 → 파서가 len 을 넘어 읽으면 ASan 이 잡는다 */
static char g_tail[1 << 16];
static const char *at_end(const char *src, size_t n) {
    memcpy(g_tail + sizeof g_tail - n, src, n);
    return g_tail + sizeof g_tail - n;
}

static int parse_str(const char *s, uint8_t *cfg, size_t cap, cfg_info *info) {
    size_t n = strlen(s);
    return cfg_parse_text(at_end(s, n), n, cfg, cap, info);
}

int main(void) {
    static uint8_t cfg[PCI_CFG_SPACE_EXP_SIZE], cfg2[PCI_CFG_SPACE_EXP_SIZE];
    cfg_info info = { 0 };
    pci_t0 h = { 0 };
    char s[200] = "";

    /* ---------- Part A: 바이트 조립 ---------- */
    {
        const uint8_t b[8] = { 0xf4, 0x1a, 0x42, 0x10, 0x16, 0x04, 0x10, 0x00 };
        CHECK(cfg_rd16(b, 0) == 0x1af4 && cfg_rd16(b, 2) == 0x1042 && cfg_rd16(b, 4) == 0x0416);
        CHECK(cfg_rd32(b, 0) == 0x10421af4u && cfg_rd32(b, 4) == 0x00100416u);
        CHECK(cfg_rd16(b, 1) == 0x421a);                    /* 바이트 배열이라 홀수 offset 도 안전 */
    }

    /* ---------- Part A: 덤프 한 줄 ---------- */
    {
        uint16_t off = 0xFFFF;
        uint8_t row[16];
        span ln = sp("10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00");
        CHECK(parse_dump_line(ln, &off, row) == 0 && off == 0x10 && row[0] == 0x04 && row[3] == 0x80 && row[15] == 0x00);
        span l3 = sp("ff0: FF Ff fF ff 00 00 00 00 00 00 00 00 00 00 00 01");
        CHECK(parse_dump_line(l3, &off, row) == 0 && off == 0xff0 && row[0] == 0xff && row[2] == 0xff && row[15] == 0x01);
        span bad15 = sp("10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00");
        span bad17 = sp("10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00 00");
        span badhex = sp("10: 04 00 02 80 03 00 00 00 40 15 00 50 00 0g 00 00");
        span nocolon = sp("10 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00");
        CHECK(parse_dump_line(bad15, &off, row) == CFG_ERR_SYNTAX && parse_dump_line(bad17, &off, row) == CFG_ERR_SYNTAX);
        CHECK(parse_dump_line(badhex, &off, row) == CFG_ERR_SYNTAX && parse_dump_line(nocolon, &off, row) == CFG_ERR_SYNTAX);
    }

    /* ---------- Part A: 실측 VM 00:06.0 (virtio-blk) ---------- */
    CHECK(parse_str(VM_00_06, cfg, sizeof cfg, &info) == CFG_OK && info.n == 256 && info.bdf.valid &&
          info.bdf.bus == 0 && info.bdf.dev == 6 && info.bdf.fn == 0);
    CHECK(pci_decode(cfg, info.n, &h) == HDR_OK);
    CHECK(h.vid == 0x1af4 && h.did == 0x1042 && h.rev == 0x01 && h.base_class == 0x01 && h.subclass == 0x80 && h.prog_if == 0x00);
    CHECK(h.cmd == 0x0416 && h.sts == 0x0010 && h.latency == 0x40 && h.layout == 0 && !h.multifunction);
    CHECK(h.bar[0].kind == BAR_MEM64 && !h.bar[0].prefetch && h.bar[0].base == 0x380020000ull &&
          h.bar[1].kind == BAR_MEM64_HI && h.bar[1].raw == 3);
    CHECK(h.bar[2].kind == BAR_MEM32 && h.bar[2].base == 0x50001540u && h.bar[3].kind == BAR_UNUSED && h.bar[5].kind == BAR_UNUSED);
    CHECK(h.ss_vid == 0x1af4 && h.ss_id == 0x0042 && h.cap_ptr == 0x40 && h.int_pin == 1 && h.int_line == 0xff);
    /* 실측 lspci -vvv 의 Control:/Status: 줄과 글자 그대로 같아야 한다 */
    pci_cmd_str(h.cmd, s, sizeof s);
    CHECK(strcmp(s, "I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+") == 0);
    pci_sts_str(h.sts, s, sizeof s);
    CHECK(strcmp(s, "Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-") == 0);
    CHECK(strcmp(pci_class_name(0x01, 0x08, 0x02), "Non-Volatile memory controller: NVM Express (NVMe)") == 0 &&
          strcmp(pci_class_name(0x06, 0x04, 0x01), "PCI bridge") == 0 && strcmp(pci_class_name(0x42, 0, 0), "Unknown class") == 0);
    CHECK(pci_report(cfg, info.n, &info.bdf, g_rep, sizeof g_rep) > 0 &&
          strstr(g_rep, "Memory at 380020000 (64-bit, non-prefetchable)") &&     /* lspci -vvv 실측 Region 0 */
          strstr(g_rep, "Memory at 50001540 (32-bit, non-prefetchable)"));       /* lspci -vvv 실측 Region 2 */
    printf("--- report: Lima VM 00:06.0 (실측) ---\n%s", g_rep);

    /* -vvv -xxx 실측 텍스트 (탭 줄 섞임) → 같은 바이트 */
    CHECK(parse_str(VM_VX_00_06, cfg2, sizeof cfg2, &info) == CFG_OK && info.n == 256 && memcmp(cfg, cfg2, 256) == 0);

    /* 실측 VM 00:00.0 (host bridge): Mem- BusMaster- */
    CHECK(parse_str(VM_00_00, cfg, sizeof cfg, &info) == CFG_OK && pci_decode(cfg, info.n, &h) == HDR_OK);
    pci_cmd_str(h.cmd, s, sizeof s);
    CHECK(strcmp(s, "I/O- Mem- BusMaster- SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-") == 0);
    pci_sts_str(h.sts, s, sizeof s);
    CHECK(strcmp(s, "Cap- 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-") == 0);

    /* 실측 QEMU virtio-net: IO BAR, Mem32, 4GB 위 64비트 prefetchable */
    CHECK(parse_str(QEMU_00_01, cfg, sizeof cfg, &info) == CFG_OK && info.bdf.valid && info.bdf.domain == 0 && info.bdf.dev == 1);
    CHECK(pci_decode(cfg, info.n, &h) == HDR_OK && h.bar[0].kind == BAR_IO && h.bar[0].base == 0x2000 &&
          h.bar[1].kind == BAR_MEM32 && h.bar[1].base == 0x10280000u && h.bar[2].kind == BAR_UNUSED);
    CHECK(h.bar[4].kind == BAR_MEM64 && h.bar[4].prefetch && h.bar[4].base == 0x8000200000ull && h.bar[5].kind == BAR_MEM64_HI);

    /* 4KB: 실측 nvme 첫 256B + 0 으로 채운 0x100..0xFFF (실측도 전부 0) */
    {
        static const uint8_t zero[PCI_CFG_SPACE_EXP_SIZE];
        size_t at = append(g_txt, 0, sizeof g_txt, QEMU_01_00_256);
        at = fmt_dump(zero, 0x100, 0x1000, NULL, g_txt, at, sizeof g_txt);
        CHECK(parse_str(g_txt, cfg, sizeof cfg, &info) == CFG_OK && info.n == 4096 && info.bdf.bus == 1);
        CHECK(pci_decode(cfg, info.n, &h) == HDR_OK && h.ext && h.vid == 0x1b36 && h.did == 0x0010 &&
              (uint32_t)(h.base_class << 16 | h.subclass << 8 | h.prog_if) == PCI_CLASS_STORAGE_EXPRESS);
        CHECK(h.bar[0].kind == BAR_MEM64 && h.bar[0].base == 0x10000000u && h.cmd == 0x0000 && h.rev == 0x02);
        CHECK(pci_report(cfg, info.n, &info.bdf, g_rep, sizeof g_rep) > 0 && strstr(g_rep, "Ext     : 4KB") &&
              strstr(g_rep, "BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF") && strstr(g_rep, "장치가 DMA/MSI 못 보냄"));
        printf("--- report: QEMU 01:00.0 nvme 4KB (실측, nvme 드라이버 로드 전) ---\n%s", g_rep);
    }

    /* ---------- Part A: 손으로 만든 가상 덤프 ---------- */
    mk_cfg(cfg, 256, &SSD_SPEC);
    CHECK(pci_decode(cfg, 256, &h) == HDR_OK && h.bar[0].kind == BAR_MEM64 && h.bar[0].base == 0xFE600000u &&
          h.bar[2].kind == BAR_IO && h.bar[2].base == 0xE000);
    {
        t0_spec mf = SSD_SPEC;
        mf.cls24 = 0x020000; mf.hdr = 0x80;                  /* 2포트 NIC 의 fn0 */
        mk_cfg(cfg, 256, &mf);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && h.multifunction && h.layout == 0 &&
              strcmp(pci_class_name(h.base_class, h.subclass, h.prog_if), "Ethernet controller") == 0);
        t0_spec bars = SSD_SPEC;                              /* BAR 타입 비트 경계 */
        bars.bar[0] = 0x00000006; bars.bar[1] = 0x000C0002; bars.bar[2] = 0x0000E003;
        bars.bar[3] = 0xF000000C; bars.bar[4] = 0x00000001; bars.bar[5] = 0x00000004;
        mk_cfg(cfg, 256, &bars);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && h.bar[0].kind == BAR_BAD && h.bar[1].kind == BAR_MEM1M &&
              h.bar[2].kind == BAR_IO && h.bar[2].base == 0xE000 && h.bar[3].kind == BAR_MEM64 && h.bar[3].prefetch &&
              h.bar[4].kind == BAR_MEM64_HI && h.bar[3].base == 0x1F0000000ull && h.bar[5].kind == BAR_BAD);
    }

    /* ---------- Part B: 헤더 종류 / 장치 없음 ---------- */
    memset(cfg, 0xFF, 256);
    CHECK(pci_decode(cfg, 256, &h) == HDR_NO_DEVICE && h.vid == 0xFFFF);
    CHECK(pci_report(cfg, 256, NULL, g_rep, sizeof g_rep) > 0 && strstr(g_rep, "장치 없음"));
    memset(cfg, 0x00, 256);
    CHECK(pci_decode(cfg, 256, &h) == HDR_NO_DEVICE);
    CHECK(parse_str(QEMU_00_02_256, cfg, sizeof cfg, &info) == CFG_OK && pci_decode(cfg, info.n, &h) == HDR_TYPE1 &&
          h.vid == 0x1b36 && h.did == 0x000c && h.base_class == 0x06 && h.subclass == 0x04);   /* 실측 root port */
    CHECK(pci_report(cfg, info.n, &info.bdf, g_rep, sizeof g_rep) > 0 && strstr(g_rep, "Type 1, PCIE-B07 참고") &&
          !strstr(g_rep, "BAR0"));                           /* Type 1 은 BAR/Type 0 필드 해석 전에 멈춤 */
    printf("--- report: QEMU 00:02.0 (실측 root port) ---\n%s", g_rep);
    {
        t0_spec t = SSD_SPEC;
        int want[5][3] = { { 0x81, HDR_TYPE1, 1 }, { 0x01, HDR_TYPE1, 0 }, { 0x02, HDR_TYPE2, 0 },
                           { 0x05, HDR_BAD_TYPE, 0 }, { 0xff, HDR_BAD_TYPE, 1 } };
        int ok = 1;
        for (int i = 0; i < 5; i++) {
            t.hdr = (uint8_t)want[i][0];
            mk_cfg(cfg, 256, &t);
            if (pci_decode(cfg, 256, &h) != want[i][1] || h.multifunction != (want[i][2] != 0)) ok = 0;
        }
        CHECK(ok);                                           /* layout = bit6:0, multi-function = bit7 따로 */
    }

    /* ---------- Part B: 진단 ---------- */
    CHECK(parse_str(VM_00_06, cfg, sizeof cfg, &info) == CFG_OK && pci_decode(cfg, 256, &h) == HDR_OK &&
          pci_diagnose(&h) == DIAG_INTX_OFF);
    CHECK(parse_str(VM_00_00, cfg, sizeof cfg, &info) == CFG_OK && pci_decode(cfg, 256, &h) == HDR_OK &&
          pci_diagnose(&h) == (DIAG_MEM_OFF | DIAG_BM_OFF));
    CHECK(pci_report(cfg, 256, &info.bdf, g_rep, sizeof g_rep) > 0 &&
          strstr(g_rep, "BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF") && strstr(g_rep, "장치가 DMA/MSI 못 보냄") &&
          strstr(g_rep, "메모리 BAR 가 없어 영향 없음"));
    CHECK(parse_str(VM_00_0C, cfg, sizeof cfg, &info) == CFG_OK && pci_decode(cfg, 256, &h) == HDR_OK &&
          pci_diagnose(&h) == DIAG_INTX_OFF && strcmp(pci_class_name(h.base_class, h.subclass, h.prog_if), "USB controller: xHCI") == 0);
    mk_cfg(cfg, 256, &SSD_SPEC);
    CHECK(pci_decode(cfg, 256, &h) == HDR_OK && pci_diagnose(&h) == (DIAG_IO_OFF | DIAG_INTX_OFF | DIAG_RMA));
    CHECK(pci_report(cfg, 256, NULL, g_rep, sizeof g_rep) > 0 && strstr(g_rep, "Received Master Abort") && strstr(g_rep, "RW1C"));
    printf("--- report: 가상 NVMe SSD (손으로 만든 덤프) ---\n%s", g_rep);
    {
        t0_spec t = SSD_SPEC;
        t.sts = 0xF910;                                       /* 에러 비트 6개 전부 + Cap */
        mk_cfg(cfg, 256, &t);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && (pci_diagnose(&h) & DIAG_ERRBITS) == DIAG_ERRBITS);
        pci_sts_str(h.sts, s, sizeof s);
        CHECK(strcmp(s, "Cap+ 66MHz- UDF- FastB2B- ParErr+ DEVSEL=fast >TAbort+ <TAbort+ <MAbort+ >SERR+ <PERR+ INTx-") == 0);
        t.sts = 0x0610;                                       /* DEVSEL 11 = 정의 안 됨 */
        mk_cfg(cfg, 256, &t);
        pci_decode(cfg, 256, &h);
        pci_sts_str(h.sts, s, sizeof s);
        CHECK(strstr(s, "DEVSEL=??") != NULL);
        t = SSD_SPEC; t.cap_ptr = 0x20;
        mk_cfg(cfg, 256, &t);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && (pci_diagnose(&h) & DIAG_CAP_BAD));
        t.cap_ptr = 0x43;                                     /* 하위 2비트는 무시 (Linux 도 ALIGN_DOWN) → 0x40 OK */
        mk_cfg(cfg, 256, &t);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && !(pci_diagnose(&h) & DIAG_CAP_BAD));
        t = SSD_SPEC; t.sts = 0x0000;                         /* 4KB 인데 Cap- */
        mk_cfg(cfg, 4096, &t);
        CHECK(pci_decode(cfg, 4096, &h) == HDR_OK && (pci_diagnose(&h) & DIAG_NO_CAPS) && pci_decode(cfg, 256, &h) == HDR_OK &&
              !(pci_diagnose(&h) & DIAG_NO_CAPS));
        t = SSD_SPEC; t.int_pin = 5; t.bar[5] = 0x4;
        mk_cfg(cfg, 256, &t);
        CHECK(pci_decode(cfg, 256, &h) == HDR_OK && (pci_diagnose(&h) & DIAG_PIN_BAD) && (pci_diagnose(&h) & DIAG_BAR_BAD));
    }

    /* ---------- Part B: 깨진 텍스트 ---------- */
    CHECK(cfg_parse_text(NULL, 0, cfg, sizeof cfg, &info) == CFG_ERR_ARG);
    CHECK(parse_str("", cfg, sizeof cfg, &info) == CFG_ERR_EMPTY && parse_str("\n\n", cfg, sizeof cfg, &info) == CFG_ERR_EMPTY);
    {
        size_t n = strlen(VM_00_06);
        const char *l11 = VM_00_06;                           /* 제목 + 덤프 10줄만 */
        for (int k = 0; k < 11; k++) l11 = strchr(l11, '\n') + 1;
        CHECK(cfg_parse_text(at_end(VM_00_06, (size_t)(l11 - VM_00_06)), (size_t)(l11 - VM_00_06), cfg, sizeof cfg, &info) == CFG_ERR_TRUNC &&
              info.n == 160);
        char t[2048];
        memcpy(t, VM_00_06, n + 1);
        char *p = strstr(t, "10: 04 00 02 80"); p[10] = 'g';               /* 비 hex */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_SYNTAX && info.line == 3);
        memcpy(t, VM_00_06, n + 1);
        p = strstr(t, "10: 04 00 02 80"); memmove(p + 4, p + 7, strlen(p + 7) + 1);   /* 15바이트 줄 */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_SYNTAX && info.line == 3);
        memcpy(t, VM_00_06, n + 1);
        p = strstr(t, "20: 00 00"); memmove(p, p + 52, strlen(p + 52) + 1);           /* 0x20 줄 빠짐 */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_OFFSET && info.line == 4);
        memcpy(t, VM_00_06, n + 1);
        p = strstr(t, "20: 00 00"); p[0] = '1';                              /* 0x10 중복 */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_OFFSET);
        memcpy(t, VM_00_06, n + 1);
        p = strstr(t, "00:06.0"); p[3] = '2'; p[4] = '0';                   /* device 0x20 > 31 */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_SYNTAX && info.line == 1);
        memcpy(t, VM_00_06, n + 1);
        p = strstr(t, "00:06.0"); p[6] = '8';                                /* function 8 */
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_ERR_SYNTAX);
        /* CRLF + 대문자 hex 도 같은 바이트 */
        size_t w = 0;
        for (size_t i = 0; i < n; i++) {
            char c = VM_00_06[i];
            if (c == '\n') t[w++] = '\r';
            t[w++] = (c >= 'a' && c <= 'f' && i > 8) ? (char)(c - 32) : c;
        }
        t[w] = '\0';
        CHECK(parse_str(t, cfg, sizeof cfg, &info) == CFG_OK && parse_str(VM_00_06, cfg2, sizeof cfg2, &info) == CFG_OK &&
              memcmp(cfg, cfg2, 256) == 0);
    }
    {   /* 4KB + "1000:" 줄 → RANGE, 4KB 를 256B 버퍼에 → CAP */
        static const uint8_t zero[PCI_CFG_SPACE_EXP_SIZE + 16];
        size_t at = append(g_txt, 0, sizeof g_txt, QEMU_01_00_256);
        at = fmt_dump(zero, 0x100, 0x1010, NULL, g_txt, at, sizeof g_txt);
        CHECK(parse_str(g_txt, cfg, sizeof cfg, &info) == CFG_ERR_RANGE && info.line == 258);
        at = append(g_txt, 0, sizeof g_txt, QEMU_01_00_256);
        at = fmt_dump(zero, 0x100, 0x1000, NULL, g_txt, at, sizeof g_txt);
        CHECK(parse_str(g_txt, cfg, 256, &info) == CFG_ERR_CAP && info.line == 18);
    }
    {   /* 모든 접두사(prefix): 마지막 바이트까지 들어 있어야만 OK, 나머지는 전부 오류 (그리고 ASan 이 범위 밖 읽기 없음을 본다) */
        size_t n = strlen(VM_00_06);
        size_t full = (size_t)(strstr(VM_00_06, "f0: ") - VM_00_06) + 51;   /* "f0: " + 16×3 - 1 → 마지막 "00" 끝 */
        int bad = 0, nok = 0;
        for (size_t L = 0; L <= n; L++) {
            int rc = cfg_parse_text(at_end(VM_00_06, L), L, cfg, sizeof cfg, &info);
            if (rc == CFG_OK) nok++;
            if ((rc == CFG_OK) != (L >= full)) bad++;
        }
        printf("prefix sweep: %zu prefixes, OK=%d, mismatches=%d\n", n + 1, nok, bad);
        CHECK(bad == 0 && nok == (int)(n + 1 - full));
    }
    {   /* 무작위 변형 3000개: 절대 죽지 않고, OK 면 256/4096 바이트 */
        uint32_t seed = 0xB03B03u;
        size_t n = strlen(VM_00_06);
        int ok_cnt = 0, err_cnt = 0, bad = 0;
        static const char alphabet[] = "0123456789abcdefg :.\n\t\rX";
        for (int it = 0; it < 3000; it++) {
            char t[2048];
            memcpy(t, VM_00_06, n);
            int muts = 1 + (int)(xs32(&seed) % 3u);
            for (int m = 0; m < muts; m++) t[xs32(&seed) % n] = alphabet[xs32(&seed) % (sizeof alphabet - 1)];
            size_t L = (xs32(&seed) % 4u == 0) ? xs32(&seed) % (n + 1) : n;
            int rc = cfg_parse_text(at_end(t, L), L, cfg, sizeof cfg, &info);
            if (rc == CFG_OK) { ok_cnt++; if (info.n != 256 && info.n != 4096) bad++; }
            else if (rc < 0) err_cnt++;
            else bad++;
        }
        printf("fuzz: 3000 mutants → OK %d, rejected %d, bad %d\n", ok_cnt, err_cnt, bad);
        CHECK(bad == 0 && err_cnt > 0);
    }
    {   /* 왕복: 무작위 바이트 → 텍스트 → 파서 → 같은 바이트 (256 과 4096 각각) */
        uint32_t seed = 0x5EEDu;
        int bad = 0;
        for (int it = 0; it < 200; it++) {
            size_t n = (it & 1) ? 4096 : 256;
            for (size_t i = 0; i < n; i++) cfg2[i] = (uint8_t)xs32(&seed);
            size_t at = fmt_dump(cfg2, 0, n, "07:1f.7 random\n", g_txt, 0, sizeof g_txt);
            if (cfg_parse_text(at_end(g_txt, at), at, cfg, sizeof cfg, &info) != CFG_OK || info.n != n ||
                memcmp(cfg, cfg2, n) != 0 || info.bdf.bus != 7 || info.bdf.dev != 0x1f || info.bdf.fn != 7) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------- Part C: 여러 장치 스캔 ---------- */
    {
        pci_summary sm[16];
        memset(sm, 0, sizeof sm);
        size_t cnt = 0, at = 0;
        at = append(g_txt, at, sizeof g_txt, VM_00_00);
        at = append(g_txt, at, sizeof g_txt, VM_00_06);
        at = append(g_txt, at, sizeof g_txt, VM_00_0C);
        CHECK(lspci_scan(at_end(g_txt, at), at, sm, 16, &cnt) == CFG_OK && cnt == 3);
        /* 실측 lspci -nn: "00:00.0 Host bridge [0600]: ... [106b:1a05]", "00:06.0 ... [0180]: ... [1af4:1042] (rev 01)",
         *                 "00:0c.0 USB controller [0c03]: ... [106b:1a06]" */
        CHECK(sm[0].bdf.dev == 0x00 && sm[0].vid == 0x106b && sm[0].did == 0x1a05 && (sm[0].class24 >> 8) == 0x0600 && sm[0].rev == 0);
        CHECK(sm[1].bdf.dev == 0x06 && sm[1].vid == 0x1af4 && sm[1].did == 0x1042 && (sm[1].class24 >> 8) == 0x0180 && sm[1].rev == 1);
        CHECK(sm[2].bdf.dev == 0x0c && sm[2].vid == 0x106b && sm[2].did == 0x1a06 && sm[2].class24 == 0x0c0330 &&
              sm[0].diag == (DIAG_MEM_OFF | DIAG_BM_OFF) && sm[2].diag == DIAG_INTX_OFF);
        CHECK(lspci_scan(at_end(g_txt, at), at, sm, 2, &cnt) == CFG_ERR_CAP && cnt == 2);

        /* QEMU 게스트: virtio-net(256B) + root port(Type 1) + nvme(4KB), 장치 사이 빈 줄 없이 이어 붙임 */
        static const uint8_t zero[PCI_CFG_SPACE_EXP_SIZE];
        at = 0;
        at = append(g_txt, at, sizeof g_txt, QEMU_00_01);
        at = append(g_txt, at, sizeof g_txt, QEMU_00_02_256);
        at = append(g_txt, at, sizeof g_txt, QEMU_01_00_256);
        at = fmt_dump(zero, 0x100, 0x1000, NULL, g_txt, at, sizeof g_txt);
        at = append(g_txt, at, sizeof g_txt, "\n\n");
        CHECK(lspci_scan(at_end(g_txt, at), at, sm, 16, &cnt) == CFG_OK && cnt == 3 && sm[1].hdr == HDR_TYPE1 &&
              sm[2].n == 4096 && sm[2].bdf.bus == 1);
        int k = find_by_class(sm, cnt, PCI_CLASS_STORAGE_EXPRESS);
        CHECK(k == 2 && sm[k].diag == (DIAG_MEM_OFF | DIAG_BM_OFF));        /* nvme 드라이버 로드 전: 꺼져 있음 */
        /* 중간 장치가 깨지면 그 앞까지만 */
        char *p = strstr(g_txt, "0000:00:02.0");
        p = strstr(p, "30: 00 00"); p[4] = 'z';
        CHECK(lspci_scan(at_end(g_txt, at), at, sm, 16, &cnt) == CFG_ERR_SYNTAX && cnt == 1);
    }

    /* ---------- Part C: probe — Command 켜기, Status 에러 비트 보존 ---------- */
    {
        CHECK(parse_str(QEMU_01_00_256, cfg, sizeof cfg, &info) == CFG_OK);
        CHECK(probe_enable(cfg, 256, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) == 0x0006 &&
              pci_decode(cfg, 256, &h) == HDR_OK && pci_diagnose(&h) == 0);      /* 실제 Linux: 0x0000 → 0x0406 (MSI-X 라 DisINTx 도) */
        mk_cfg(cfg, 256, &SSD_SPEC);
        put16(cfg, PCI_COMMAND, 0x0000);
        CHECK(probe_enable(cfg, 256, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) == 0x0006 && cfg_rd16(cfg, PCI_STATUS) == 0x2010);
        memcpy(cfg2, cfg, 256);
        put16(cfg2, PCI_COMMAND, 0x0000);
        CHECK(probe_enable_buggy(cfg2, 256, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) == 0x0006 &&
              cfg_rd16(cfg2, PCI_STATUS) == 0x0010);                            /* 버그: <MAbort 증거가 사라짐 */
        CHECK(probe_enable(cfg, 256, 0x0100) == -1);                              /* SERR 는 이 함수 범위 밖 */
        CHECK(parse_str(QEMU_00_02_256, cfg, sizeof cfg, &info) == CFG_OK && probe_enable(cfg, 256, PCI_COMMAND_MEMORY) == -1);
        memset(cfg, 0xFF, 256);
        CHECK(probe_enable(cfg, 256, PCI_COMMAND_MEMORY) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
