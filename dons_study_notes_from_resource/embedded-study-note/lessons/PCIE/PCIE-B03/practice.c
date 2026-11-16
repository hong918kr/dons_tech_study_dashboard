/* PCIE-B03 추가 연습 — 한 번에 검증
 *
 * 연습 1: 일반 사용자 lspci (64B) 허용      연습 2: lspci -n 한 줄 출력 (실측과 비교)
 * 연습 3: 드라이버 로드 전/후 덤프 비교      연습 4: Status 에러 비트 RW1C 지우기
 * 연습 5: pci_device_id 식 드라이버 매칭 (실측 "Kernel driver in use" 와 비교)
 *
 * 앞부분은 solution.c 의 파서/디코더/스캔 코드를 그대로 복사한 것 (파일 하나로 빌드되도록).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
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

static const struct { uint16_t bit; const char *name; } k_cmd_flags[] = {
    { PCI_COMMAND_IO, "I/O" }, { PCI_COMMAND_MEMORY, "Mem" }, { PCI_COMMAND_MASTER, "BusMaster" },
    { PCI_COMMAND_SPECIAL, "SpecCycle" }, { PCI_COMMAND_INVALIDATE, "MemWINV" }, { PCI_COMMAND_VGA_PALETTE, "VGASnoop" },
    { PCI_COMMAND_PARITY, "ParErr" }, { PCI_COMMAND_WAIT, "Stepping" }, { PCI_COMMAND_SERR, "SERR" },
    { PCI_COMMAND_FAST_BACK, "FastB2B" }, { PCI_COMMAND_INTX_DISABLE, "DisINTx" },
};
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


/* =====================================================================
 * 추가 연습 답
 * ===================================================================== */

/* 연습 1 — 일반 사용자 lspci (64B) 허용: 코어 파서는 그대로 두고, TRUNC 중 n == 64 만 받아 준다 */
static int cfg_parse_any(const char *txt, size_t len, uint8_t *cfg, size_t cap, cfg_info *info, bool allow64) {
    int rc = cfg_parse_text(txt, len, cfg, cap, info);
    if (rc == CFG_ERR_TRUNC && allow64 && info->n == PCI_STD_HEADER_SIZEOF) return CFG_OK;
    return rc;
}

/* 연습 2 — lspci -n 한 줄: "bb:dd.f cccc: vvvv:dddd" + rev 가 0 이 아니면 " (rev rr)" */
static int fmt_lspci_n(const pci_summary *s, char *out, size_t cap) {
    char rev[16] = "";
    if (s->rev) snprintf(rev, sizeof rev, " (rev %02x)", s->rev);
    int r = snprintf(out, cap, "%02x:%02x.%x %04x: %04x:%04x%s", s->bdf.bus, s->bdf.dev, s->bdf.fn,
                     (unsigned)(s->class24 >> 8), s->vid, s->did, rev);
    return (r < 0 || (size_t)r >= cap) ? -1 : r;
}

/* 연습 3 — 두 덤프 비교: 0x00..0x3F 의 바뀐 레지스터 이름 + Command 비트 증감 */
static const struct { uint8_t off, size; const char *name; } k_t0_regs[] = {
    { 0x00, 2, "Vendor ID" }, { 0x02, 2, "Device ID" }, { 0x04, 2, "Command" }, { 0x06, 2, "Status" },
    { 0x08, 1, "Revision" }, { 0x09, 3, "Class" }, { 0x0c, 1, "Cache Line" }, { 0x0d, 1, "Latency" },
    { 0x0e, 1, "Header Type" }, { 0x0f, 1, "BIST" }, { 0x10, 4, "BAR0" }, { 0x14, 4, "BAR1" }, { 0x18, 4, "BAR2" },
    { 0x1c, 4, "BAR3" }, { 0x20, 4, "BAR4" }, { 0x24, 4, "BAR5" }, { 0x28, 4, "CardBus CIS" },
    { 0x2c, 2, "Subsystem Vendor" }, { 0x2e, 2, "Subsystem ID" }, { 0x30, 4, "ROM" }, { 0x34, 1, "Cap Ptr" },
    { 0x3c, 1, "Int Line" }, { 0x3d, 1, "Int Pin" }, { 0x3e, 1, "Min Gnt" }, { 0x3f, 1, "Max Lat" },
};

static uint32_t rd_n(const uint8_t *c, uint8_t off, uint8_t size) {
    uint32_t v = 0;
    for (int i = size - 1; i >= 0; i--) v = v << 8 | c[off + i];
    return v;
}

/* 반환: 바뀐 레지스터 수. out 에 한 줄씩 */
static int cfg_diff(const uint8_t *a, const uint8_t *b, char *out, size_t cap) {
    size_t w = 0;
    int changed = 0;
    if (cap) out[0] = '\0';
    for (size_t i = 0; i < sizeof k_t0_regs / sizeof k_t0_regs[0]; i++) {
        uint32_t va = rd_n(a, k_t0_regs[i].off, k_t0_regs[i].size), vb = rd_n(b, k_t0_regs[i].off, k_t0_regs[i].size);
        if (va == vb) continue;
        changed++;
        int r = snprintf(out + w, cap - w, "%s 0x%0*x -> 0x%0*x", k_t0_regs[i].name,
                         k_t0_regs[i].size * 2, (unsigned)va, k_t0_regs[i].size * 2, (unsigned)vb);
        if (r < 0 || (size_t)r >= cap - w) return -1;
        w += (size_t)r;
        if (k_t0_regs[i].off == PCI_COMMAND) {
            const char *sep = " (";
            for (size_t k = 0; k < sizeof k_cmd_flags / sizeof k_cmd_flags[0]; k++) {
                uint16_t bit = k_cmd_flags[k].bit;
                if ((va ^ vb) & bit) {
                    r = snprintf(out + w, cap - w, "%s%c%s", sep, (vb & bit) ? '+' : '-', k_cmd_flags[k].name);
                    if (r < 0 || (size_t)r >= cap - w) return -1;
                    w += (size_t)r;
                    sep = " ";
                }
            }
            r = snprintf(out + w, cap - w, ")");
            if (r < 0 || (size_t)r >= cap - w) return -1;
            w += (size_t)r;
        }
        r = snprintf(out + w, cap - w, "\n");
        if (r < 0 || (size_t)r >= cap - w) return -1;
        w += (size_t)r;
    }
    return changed;
}

/* 연습 4 — Status 에러 비트 지우기 (RW1C): 읽은 값의 에러 비트만 1 로 다시 쓴다. 반환: 지운 비트 */
static uint16_t clear_status_errors(uint8_t *cfg) {
    uint16_t st = cfg_rd16(cfg, PCI_STATUS);
    uint16_t errs = st & STATUS_W1C_MASK;
    if (errs) cfg_wr16(cfg, PCI_STATUS, errs);           /* 16비트 쓰기 — Command 는 안 건드림 */
    return errs;
}

/* 연습 5 — pci_device_id 식 매칭 (Linux pci_match_one_device 의 vendor/device/class 부분만) */
#define PCI_ANY_ID 0xFFFFFFFFu
typedef struct { uint32_t vendor, device, class24, class_mask; const char *driver; } dev_id;
static const dev_id k_ids[] = {
    { PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_EXPRESS, 0xFFFFFF, "nvme" },      /* PCI_DEVICE_CLASS(…EXPRESS, 0xffffff) */
    { PCI_ANY_ID, PCI_ANY_ID, 0x0c0330, 0xFFFFFF, "xhci_hcd" },                   /* PCI_DEVICE_CLASS(…USB_XHCI, ~0) */
    { 0x1af4, PCI_ANY_ID, 0, 0, "virtio-pci" },                                    /* PCI_DEVICE(REDHAT_QUMRANET, ANY) */
};

static const char *match_driver(const pci_summary *s) {
    if (s->hdr != HDR_OK && s->hdr != HDR_TYPE1) return NULL;
    for (size_t i = 0; i < sizeof k_ids / sizeof k_ids[0]; i++) {
        const dev_id *d = &k_ids[i];
        if ((d->vendor == PCI_ANY_ID || d->vendor == s->vid) && (d->device == PCI_ANY_ID || d->device == s->did) &&
            ((d->class24 ^ s->class24) & d->class_mask) == 0)
            return d->driver;
    }
    return NULL;
}

/* ================= 테스트 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* Lima VM 실측: root 로 lspci -xxx (256B) / 일반 사용자 lspci -xxx (64B 만 나온다) */
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
static const char VM_00_06_NONROOT[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n";
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
/* QEMU 게스트 nvme 실측 첫 256B (드라이버 로드 전). 로드 후 0x04..0x07 은 실측 "06 04 10 00" (capture_qemu_nvme_probe_dmesg.txt) */
static const char QEMU_01_00_256[] =
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

static char g_buf[8192];

int main(void) {
    static uint8_t cfg[PCI_CFG_SPACE_EXP_SIZE], cfg2[PCI_CFG_SPACE_EXP_SIZE];
    cfg_info info = { 0 };
    pci_t0 h = { 0 }, h2 = { 0 };

    /* 연습 1 */
    CHECK(cfg_parse_any(VM_00_06_NONROOT, strlen(VM_00_06_NONROOT), cfg, sizeof cfg, &info, false) == CFG_ERR_TRUNC && info.n == 64);
    CHECK(cfg_parse_any(VM_00_06_NONROOT, strlen(VM_00_06_NONROOT), cfg, sizeof cfg, &info, true) == CFG_OK && info.n == 64);
    CHECK(pci_decode(cfg, 64, &h) == HDR_OK &&
          cfg_parse_text(VM_00_06, strlen(VM_00_06), cfg2, sizeof cfg2, &info) == CFG_OK && pci_decode(cfg2, 256, &h2) == HDR_OK &&
          memcmp(cfg, cfg2, 64) == 0 && h.bar[0].base == h2.bar[0].base && h.cap_ptr == 0x40);
    {
        char t[1024];                                       /* 128B (8줄) 는 여전히 잘림 */
        const char *p = VM_00_06;
        for (int k = 0; k < 9; k++) p = strchr(p, '\n') + 1;
        size_t n = (size_t)(p - VM_00_06);
        memcpy(t, VM_00_06, n);
        CHECK(cfg_parse_any(t, n, cfg, sizeof cfg, &info, true) == CFG_ERR_TRUNC && info.n == 128);
    }

    /* 연습 2 — lspci -n 실측 줄과 글자 그대로 */
    {
        pci_summary sm[8];
        size_t cnt = 0, at = 0;
        memset(sm, 0, sizeof sm);
        at += (size_t)snprintf(g_buf + at, sizeof g_buf - at, "%s%s%s", VM_00_00, VM_00_06, VM_00_0C);
        CHECK(lspci_scan(g_buf, at, sm, 8, &cnt) == CFG_OK && cnt == 3);
        const char *want[3] = { "00:00.0 0600: 106b:1a05", "00:06.0 0180: 1af4:1042 (rev 01)", "00:0c.0 0c03: 106b:1a06" };
        int ok = 1;
        char line[64];
        for (size_t i = 0; i < 3; i++) {
            if (fmt_lspci_n(&sm[i], line, sizeof line) < 0 || strcmp(line, want[i]) != 0) ok = 0;
            printf("lspci -n : %s\n", line);
        }
        CHECK(ok);

        /* 연습 5 — 드라이버 매칭: 실측 "Kernel driver in use" 와 같아야 */
        CHECK(match_driver(&sm[0]) == NULL);                                          /* host bridge: 드라이버 없음 */
        CHECK(match_driver(&sm[1]) && strcmp(match_driver(&sm[1]), "virtio-pci") == 0);   /* lspci -k: virtio-pci */
        CHECK(match_driver(&sm[2]) && strcmp(match_driver(&sm[2]), "xhci_hcd") == 0);     /* lspci -k: xhci_hcd */
        pci_summary q;
        memset(&q, 0, sizeof q);
        CHECK(cfg_parse_text(QEMU_01_00_256, strlen(QEMU_01_00_256), cfg, sizeof cfg, &info) == CFG_OK);
        q.hdr = pci_decode(cfg, 256, &h);
        q.vid = h.vid; q.did = h.did; q.class24 = (uint32_t)h.base_class << 16 | (uint32_t)h.subclass << 8 | h.prog_if;
        CHECK(match_driver(&q) && strcmp(match_driver(&q), "nvme") == 0);              /* dmesg: "nvme 0000:01:00.0" */
        q.class24 = 0x010801;                                                          /* prog-if 만 다르면 안 붙는다 */
        CHECK(match_driver(&q) == NULL);
    }

    /* 연습 3 — 드라이버 로드 전/후 비교 (QEMU nvme 실측) */
    {
        CHECK(cfg_parse_text(QEMU_01_00_256, strlen(QEMU_01_00_256), cfg, sizeof cfg, &info) == CFG_OK);
        memcpy(cfg2, cfg, 256);
        cfg2[4] = 0x06; cfg2[5] = 0x04; cfg2[6] = 0x10; cfg2[7] = 0x00;            /* 실측 after: 06 04 10 00 */
        int n = cfg_diff(cfg, cfg2, g_buf, sizeof g_buf);
        printf("diff before/after nvme probe:\n%s", g_buf);
        CHECK(n == 1 && strcmp(g_buf, "Command 0x0000 -> 0x0406 (+Mem +BusMaster +DisINTx)\n") == 0);
        CHECK(cfg_diff(cfg, cfg, g_buf, sizeof g_buf) == 0 && g_buf[0] == '\0');
        cfg2[0x10] = 0x00; cfg2[0x07] = 0x20;                                          /* BAR0 사라짐 + <MAbort */
        CHECK(cfg_diff(cfg, cfg2, g_buf, sizeof g_buf) == 3 && strstr(g_buf, "Status 0x0010 -> 0x2010") &&
              strstr(g_buf, "BAR0 0x10000004 -> 0x10000000"));
    }

    /* 연습 4 — RW1C 지우기 */
    {
        memset(cfg, 0, 256);
        cfg[0x04] = 0x06; cfg[0x06] = 0x10; cfg[0x07] = 0xF9;                          /* Command 0x0006, Status 0xF910 */
        CHECK(clear_status_errors(cfg) == 0xF900 && cfg_rd16(cfg, PCI_STATUS) == 0x0010 && cfg_rd16(cfg, PCI_COMMAND) == 0x0006);
        CHECK(clear_status_errors(cfg) == 0 && cfg_rd16(cfg, PCI_STATUS) == 0x0010);
        cfg[0x07] = 0x20;                                                              /* 0 쓰기는 아무것도 못 지운다 */
        cfg_wr16(cfg, PCI_STATUS, 0x0000);
        CHECK(cfg_rd16(cfg, PCI_STATUS) == 0x2010);
        cfg_wr16(cfg, PCI_STATUS, 0xFFFF);                                             /* 전부 1: 에러만 지워지고 Cap(RO) 는 남음 */
        CHECK(cfg_rd16(cfg, PCI_STATUS) == 0x0010);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
