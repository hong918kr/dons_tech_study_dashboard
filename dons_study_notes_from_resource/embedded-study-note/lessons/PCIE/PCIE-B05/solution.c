/* PCIE-B05 BAR sizing과 디코드: all-1s 쓰기 알고리즘
 *
 * mock 디바이스 = "실리콘": config space 256B 바이트 배열 + 바이트별 쓰기 마스크(wmask, QEMU 방식).
 *   BAR 에 0xFFFFFFFF 를 써도 wmask 비트(크기에 맞춰 정렬된 주소 비트)만 바뀌고, 하위 type 비트는 하드와이어.
 *   가상 장치 BAR 구성: BAR0 IO 256B / BAR1 Mem32 NP 4KB / BAR2+3 Mem64 prefetchable 256MB / BAR4 Mem32 NP 64KB / BAR5 없음
 *   mock 은 config write 가 끝날 때마다 "디코드가 켜진 채 sizing 중간 상태인 BAR" 와 그 창에 걸린 다른 에이전트의
 *   MMIO 접근(traffic)을 기록한다 → 왜 sizing 중에 Command.Memory/IO 를 꺼야 하는지 숫자로 보인다.
 *
 * Part A: bar_kind_of (type 비트), bar_size_from_readback (마스크 → 크기), bar_read_masks (Linux 6.18 순서:
 *         decode off → [save, write ~0, read, restore] × 6 → Command 복원), bar_decode_all (64비트 쌍 결합)
 * Part B: 고전 버그 재현(하위 비트 안 지움, 64비트 상위 절반을 별도 BAR 로 취급), decode 켠 채 sizing,
 *         8GB(>4GB) BAR, BAR5 의 64비트, reserved type, 16비트 IO 디코드, 비연속 마스크, 없는 장치(0xFFFF)
 * Part C: bar_assign (창에 자연 정렬 배치 → BAR 쓰기 → decode 켬 → mock 주소 디코드로 확인),
 *         ep_bar_program (SSD 컨트롤러 엔드포인트 FW 쪽: DWC 스타일 BAR mask = size-1) 전수 왕복
 *
 * 상수 이름/값: Linux include/uapi/linux/pci_regs.h 기준 (맥에서 빌드되도록 같은 이름으로 다시 정의).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PCI_VENDOR_ID                  0x00
#define PCI_COMMAND                    0x04
#define PCI_COMMAND_IO                 0x1
#define PCI_COMMAND_MEMORY             0x2
#define PCI_COMMAND_MASTER             0x4
#define PCI_BASE_ADDRESS_0             0x10
#define PCI_STD_NUM_BARS               6
#define PCI_BASE_ADDRESS_SPACE         0x01   /* 0 = memory, 1 = I/O */
#define PCI_BASE_ADDRESS_SPACE_IO      0x01
#define PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define PCI_BASE_ADDRESS_MEM_TYPE_32   0x00
#define PCI_BASE_ADDRESS_MEM_TYPE_1M   0x02   /* obsolete, Linux 는 32비트로 취급 */
#define PCI_BASE_ADDRESS_MEM_TYPE_64   0x04
#define PCI_BASE_ADDRESS_MEM_PREFETCH  0x08
#define PCI_BASE_ADDRESS_MEM_MASK      (~0x0fU)  /* 헤더는 UL — 여기선 32비트 dword 용으로 U */
#define PCI_BASE_ADDRESS_IO_MASK       (~0x03U)
#define PCI_COMMAND_DECODE_ENABLE      (PCI_COMMAND_MEMORY | PCI_COMMAND_IO)  /* drivers/pci/probe.c */

/* ---------------- config space 접근 (리틀엔디언 바이트 배열) ---------------- */
static uint16_t cfg_rd16(const uint8_t *c, uint16_t off) { return (uint16_t)(c[off] | (c[off + 1] << 8)); }
static uint32_t cfg_rd32(const uint8_t *c, uint16_t off) {
    return (uint32_t)c[off] | (uint32_t)c[off + 1] << 8 | (uint32_t)c[off + 2] << 16 | (uint32_t)c[off + 3] << 24;
}

/* 드라이버가 보는 config 접근 = pci_read_config_word/dword 같은 함수 포인터 묶음 */
typedef struct {
    void *ctx;
    uint16_t (*rd16)(void *ctx, uint16_t off);
    uint32_t (*rd32)(void *ctx, uint16_t off);
    void (*wr16)(void *ctx, uint16_t off, uint16_t v);
    void (*wr32)(void *ctx, uint16_t off, uint32_t v);
} cfg_ops;

typedef enum { BAR_NONE, BAR_IO, BAR_MEM32, BAR_MEM64, BAR_UPPER, BAR_BAD } bar_kind;
enum { BS_OK = 0, BS_UNIMPL = 1, BS_UPPER = 2, BS_BADMASK = -1, BS_BADTYPE = -2, BS_64_AT_LAST = -3 };
enum { BR_OK = 0, BR_ERR_NODEV = -1 };
enum { BA_OK = 0, BA_ERR_SPACE = -1 };
enum { EP_OK = 0, EP_ERR_KIND = -1, EP_ERR_INDEX = -2, EP_ERR_SIZE = -3 };
enum { SP_MEM = 0, SP_IO = 1 };

typedef struct {
    uint8_t  index;
    bar_kind kind;
    bool     prefetch;
    int      status;   /* BS_* */
    uint64_t addr;     /* type 비트를 지운 버스 주소 */
    uint64_t size;
} bar_info;

/* ======================= mock 디바이스 ("실리콘") ======================= */
#define MOCK_TRAFFIC_MAX 4
#define MOCK_HIT_MAX     16
#define MOCK_WLOG_MAX    64

typedef struct { uint8_t space; uint64_t addr; } mock_access;
typedef struct { uint8_t bar; uint8_t space; uint64_t addr; uint32_t after_write; } mock_hit;

typedef struct {
    uint8_t  cfg[256];
    uint8_t  wmask[256];
    bar_kind kind[PCI_STD_NUM_BARS];        /* 실리콘이 아는 BAR 종류 (주소 디코드용) */
    uint32_t bar_wm[PCI_STD_NUM_BARS];      /* dword 별 쓰기 가능 비트 */
    mock_access traffic[MOCK_TRAFFIC_MAX];  /* 다른 에이전트가 계속 날리는 접근 */
    uint32_t ntraffic;
    uint32_t cfg_writes;
    uint32_t transient_on;                  /* (write, BAR) 쌍: 디코드 켜진 채 sizing 중간 상태 */
    mock_hit hits[MOCK_HIT_MAX];
    uint32_t nhits;
    uint16_t wlog_off[MOCK_WLOG_MAX];
    uint32_t wlog_val[MOCK_WLOG_MAX];
    uint32_t nwlog;
} mock_dev;

static void mock_init(mock_dev *d, uint16_t vid, uint16_t did, uint16_t cmd) {
    memset(d, 0, sizeof *d);
    d->cfg[0] = (uint8_t)vid; d->cfg[1] = (uint8_t)(vid >> 8);
    d->cfg[2] = (uint8_t)did; d->cfg[3] = (uint8_t)(did >> 8);
    d->cfg[4] = (uint8_t)cmd; d->cfg[5] = (uint8_t)(cmd >> 8);
    d->cfg[0x09] = 0x02; d->cfg[0x0A] = 0x08; d->cfg[0x0B] = 0x01;  /* class 01:08:02 NVMe */
    d->wmask[4] = 0x47; d->wmask[5] = 0x05;  /* Command: IO MEM MASTER PARITY SERR INTX_DISABLE = 0x0547 */
}

static void mock_put32(mock_dev *d, uint16_t off, uint32_t v, uint32_t wm) {
    for (int b = 0; b < 4; b++) { d->cfg[off + b] = (uint8_t)(v >> (8 * b)); d->wmask[off + b] = (uint8_t)(wm >> (8 * b)); }
}

/* 저수준: hw = 하드와이어 하위 비트, wm = 쓰기 가능 비트, assigned = 펌웨어가 이미 써 둔 주소 */
static void mock_set_bar_raw(mock_dev *d, int i, bar_kind k, uint32_t hw, uint32_t wm, uint32_t assigned) {
    d->kind[i] = k; d->bar_wm[i] = wm;
    mock_put32(d, (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i), (assigned & wm) | hw, wm);
}

/* 고수준: 종류/크기로 BAR 하나(64비트면 쌍) 구성. 크기는 2의 거듭제곱이라고 가정 (실리콘 설계값) */
static void mock_set_bar(mock_dev *d, int i, bar_kind k, bool pf, uint64_t size, uint64_t assigned) {
    if (k == BAR_IO) {
        mock_set_bar_raw(d, i, k, PCI_BASE_ADDRESS_SPACE_IO, (uint32_t)~(size - 1) & PCI_BASE_ADDRESS_IO_MASK, (uint32_t)assigned);
    } else if (k == BAR_MEM32) {
        mock_set_bar_raw(d, i, k, pf ? PCI_BASE_ADDRESS_MEM_PREFETCH : 0, (uint32_t)~(size - 1) & PCI_BASE_ADDRESS_MEM_MASK, (uint32_t)assigned);
    } else if (k == BAR_MEM64) {
        uint64_t wm = ~(size - 1) & ~(uint64_t)0xF;
        mock_set_bar_raw(d, i, k, PCI_BASE_ADDRESS_MEM_TYPE_64 | (pf ? PCI_BASE_ADDRESS_MEM_PREFETCH : 0), (uint32_t)wm, (uint32_t)assigned);
        mock_set_bar_raw(d, i + 1, BAR_UPPER, 0, (uint32_t)(wm >> 32), (uint32_t)(assigned >> 32));
    }
}

static uint32_t mock_bar_val(const mock_dev *d, int i) { return cfg_rd32(d->cfg, (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i)); }

/* 지금 BAR i 가 "sizing 중간 상태"(쓰기 가능 비트가 전부 1)인가 — 64비트 쌍은 어느 한쪽이라도 */
static bool mock_transient(const mock_dev *d, int i) {
    bool t = d->bar_wm[i] != 0 && (mock_bar_val(d, i) & d->bar_wm[i]) == d->bar_wm[i];
    if (d->kind[i] == BAR_MEM64 && i + 1 < PCI_STD_NUM_BARS)
        t = t || (d->bar_wm[i + 1] != 0 && (mock_bar_val(d, i + 1) & d->bar_wm[i + 1]) == d->bar_wm[i + 1]);
    return t;
}

/* 이 장치가 (space, addr) 를 자기 것으로 받는가 → BAR 번호, 아니면 -1. Command 의 Memory/IO 비트가 켜져야 받는다 */
static int mock_decode(const mock_dev *d, uint8_t space, uint64_t addr, uint64_t *offset) {
    uint16_t cmd = cfg_rd16(d->cfg, PCI_COMMAND);
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        bar_kind k = d->kind[i];
        if (k != BAR_IO && k != BAR_MEM32 && k != BAR_MEM64) continue;
        uint64_t wm = d->bar_wm[i], base = mock_bar_val(d, i) & d->bar_wm[i];
        if (k == BAR_MEM64) {
            if (i + 1 >= PCI_STD_NUM_BARS) continue;
            wm |= (uint64_t)d->bar_wm[i + 1] << 32;
            base |= (uint64_t)(mock_bar_val(d, i + 1) & d->bar_wm[i + 1]) << 32;
        }
        uint8_t sp = (k == BAR_IO) ? SP_IO : SP_MEM;
        if (sp != space) continue;
        if (sp == SP_IO && !(cmd & PCI_COMMAND_IO)) continue;
        if (sp == SP_MEM && !(cmd & PCI_COMMAND_MEMORY)) continue;
        if (wm == 0) continue;
        uint64_t size = wm & (~wm + 1);                              /* 실리콘 쪽 크기 = 쓰기 가능 비트 중 최하위 */
        if (addr >= base && addr - base < size) { if (offset) *offset = addr - base; return i; }
    }
    return -1;
}

/* config write 하나가 끝날 때마다: 디코드 켜진 채 중간 상태인 BAR 와, 그 창에 걸린 traffic 기록 */
static void mock_after_write(mock_dev *d, uint16_t off, uint32_t v) {
    d->cfg_writes++;
    if (d->nwlog < MOCK_WLOG_MAX) { d->wlog_off[d->nwlog] = off; d->wlog_val[d->nwlog] = v; d->nwlog++; }
    uint16_t cmd = cfg_rd16(d->cfg, PCI_COMMAND);
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        bar_kind k = d->kind[i];
        if (k != BAR_IO && k != BAR_MEM32 && k != BAR_MEM64) continue;
        bool on = (k == BAR_IO) ? (cmd & PCI_COMMAND_IO) : (cmd & PCI_COMMAND_MEMORY);
        if (on && mock_transient(d, i)) d->transient_on++;
    }
    for (uint32_t t = 0; t < d->ntraffic; t++) {
        int b = mock_decode(d, d->traffic[t].space, d->traffic[t].addr, NULL);
        if (b >= 0 && mock_transient(d, b) && d->nhits < MOCK_HIT_MAX) {
            d->hits[d->nhits++] = (mock_hit){ (uint8_t)b, d->traffic[t].space, d->traffic[t].addr, d->cfg_writes };
        }
    }
}

static uint16_t m_rd16(void *ctx, uint16_t off) { return cfg_rd16(((mock_dev *)ctx)->cfg, off); }
static uint32_t m_rd32(void *ctx, uint16_t off) { return cfg_rd32(((mock_dev *)ctx)->cfg, off); }
static void m_wr(mock_dev *d, uint16_t off, uint32_t v, int n) {
    for (int b = 0; b < n; b++) {
        uint8_t m = d->wmask[off + b];
        d->cfg[off + b] = (uint8_t)((d->cfg[off + b] & ~m) | ((uint8_t)(v >> (8 * b)) & m));
    }
    mock_after_write(d, off, v);
}
static void m_wr16(void *ctx, uint16_t off, uint16_t v) { m_wr((mock_dev *)ctx, off, v, 2); }
static void m_wr32(void *ctx, uint16_t off, uint32_t v) { m_wr((mock_dev *)ctx, off, v, 4); }
static cfg_ops mock_ops(mock_dev *d) { return (cfg_ops){ d, m_rd16, m_rd32, m_wr16, m_wr32 }; }

/* ======================= Part A: 디코드와 sizing ======================= */

/* BAR 하위 비트 → 종류. bit0=1 → IO. 아니면 bits 2:1 = 00 Mem32, 01 (1M, obsolete) Mem32, 10 Mem64, 11 reserved → BAR_BAD.
 * Memory 면 bit3 = prefetchable. */
static bar_kind bar_kind_of(uint32_t lo, bool *prefetch) {
    *prefetch = false;
    if ((lo & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO) return BAR_IO;
    *prefetch = (lo & PCI_BASE_ADDRESS_MEM_PREFETCH) != 0;
    switch (lo & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
    case PCI_BASE_ADDRESS_MEM_TYPE_32: return BAR_MEM32;
    case PCI_BASE_ADDRESS_MEM_TYPE_1M: return BAR_MEM32;
    case PCI_BASE_ADDRESS_MEM_TYPE_64: return BAR_MEM64;
    default:                           return BAR_BAD;
    }
}

/* all-1s 를 쓴 뒤 읽은 값(64비트면 hi<<32 | lo) → 크기.
 * type 비트를 지운 m 의 "가장 낮은 1 비트" = 크기. 주소 폭 안에서 그 위 비트는 전부 1 이어야 한다(연속 마스크).
 * IO 는 상위 16비트가 0 으로 하드와이어된 16비트 디코드 장치도 허용(폭 0xFFFF). 반환 BS_OK / BS_UNIMPL / BS_BADMASK */
static int bar_size_from_readback(uint64_t rb, bar_kind k, uint64_t *size) {
    uint64_t m, width;
    *size = 0;
    if (k == BAR_IO) {
        m = rb & PCI_BASE_ADDRESS_IO_MASK & 0xFFFFFFFFull;
        width = (m >> 16) == 0 ? 0xFFFFull : 0xFFFFFFFFull;
    } else if (k == BAR_MEM32) {
        m = rb & PCI_BASE_ADDRESS_MEM_MASK & 0xFFFFFFFFull;
        width = 0xFFFFFFFFull;
    } else if (k == BAR_MEM64) {
        m = rb & ~(uint64_t)0xF;
        width = ~(uint64_t)0;
    } else {
        return BS_BADMASK;
    }
    if (m == 0) return BS_UNIMPL;
    uint64_t s = m & (~m + 1);                 /* 가장 낮은 1 비트 */
    if ((m | (s - 1)) != width) return BS_BADMASK;
    *size = s;
    return BS_OK;
}

/* Linux 6.18 pci_read_bases() + __pci_size_bars() 와 같은 순서:
 *   VID==0xFFFF 면 아무것도 쓰지 않고 BR_ERR_NODEV.
 *   Command 저장 → (disable_decode && decode 비트 켜짐) 이면 Memory/IO 만 끈다 (Bus Master 는 그대로)
 *   BAR 0..5: orig = 읽기, 0xFFFFFFFF 쓰기, mask = 읽기, orig 다시 쓰기
 *   decode 를 껐었다면 Command 원래 값으로 복원.  (disable_decode=false 는 Part B 버그 재현용) */
static int bar_read_masks(const cfg_ops *ops, uint32_t orig[PCI_STD_NUM_BARS], uint32_t mask[PCI_STD_NUM_BARS], bool disable_decode) {
    if (ops->rd16(ops->ctx, PCI_VENDOR_ID) == 0xFFFF) return BR_ERR_NODEV;
    uint16_t cmd = ops->rd16(ops->ctx, PCI_COMMAND);
    bool turned_off = disable_decode && (cmd & PCI_COMMAND_DECODE_ENABLE);
    if (turned_off) ops->wr16(ops->ctx, PCI_COMMAND, (uint16_t)(cmd & ~PCI_COMMAND_DECODE_ENABLE));
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        uint16_t off = (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i);
        orig[i] = ops->rd32(ops->ctx, off);
        ops->wr32(ops->ctx, off, 0xFFFFFFFFu);
        mask[i] = ops->rd32(ops->ctx, off);
        ops->wr32(ops->ctx, off, orig[i]);
    }
    if (turned_off) ops->wr16(ops->ctx, PCI_COMMAND, cmd);
    return BR_OK;
}

/* orig/mask 6개 → bar_info 6개. 64비트면 i+1 을 상위 절반으로 결합하고 out[i+1] 은 BAR_UPPER.
 * mask==0 → BAR_NONE/BS_UNIMPL, mask==0xFFFFFFFF(응답 없음) → BS_BADMASK, type 11b → BS_BADTYPE,
 * BAR5 가 64비트 → BS_64_AT_LAST. 반환 = BS_OK 인 BAR 개수 */
static int bar_decode_all(const uint32_t orig[PCI_STD_NUM_BARS], const uint32_t mask[PCI_STD_NUM_BARS], bar_info out[PCI_STD_NUM_BARS]) {
    int n_ok = 0;
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) out[i] = (bar_info){ .index = (uint8_t)i, .kind = BAR_NONE, .status = BS_UNIMPL };
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        bar_info *b = &out[i];
        if (mask[i] == 0) continue;
        if (mask[i] == 0xFFFFFFFFu) { b->kind = BAR_BAD; b->status = BS_BADMASK; continue; }
        bool pf;
        bar_kind k = bar_kind_of(orig[i], &pf);
        if (k == BAR_BAD) { b->kind = BAR_BAD; b->status = BS_BADTYPE; continue; }
        uint64_t rb = mask[i], addr = orig[i];
        if (k == BAR_MEM64) {
            if (i == PCI_STD_NUM_BARS - 1) { b->kind = BAR_BAD; b->status = BS_64_AT_LAST; continue; }
            rb |= (uint64_t)mask[i + 1] << 32;
            addr |= (uint64_t)orig[i + 1] << 32;
            out[i + 1].kind = BAR_UPPER; out[i + 1].status = BS_UPPER;
        }
        b->kind = k; b->prefetch = pf;
        b->addr = addr & (k == BAR_IO ? ~(uint64_t)0x3 : ~(uint64_t)0xF);
        b->status = bar_size_from_readback(rb, k, &b->size);
        if (b->status == BS_OK) n_ok++;
        if (k == BAR_MEM64) i++;
    }
    return n_ok;
}

/* 드라이버가 부르는 한 방: 올바른 순서(decode off)로 마스크를 읽고 디코드 */
static int bar_probe_all(const cfg_ops *ops, bar_info out[PCI_STD_NUM_BARS]) {
    uint32_t orig[PCI_STD_NUM_BARS], mask[PCI_STD_NUM_BARS];
    if (bar_read_masks(ops, orig, mask, true) != BR_OK) return -1;
    return bar_decode_all(orig, mask, out);
}

/* ======================= Part B: 고전 버그 (일부러 틀린 코드, 비교용) ======================= */

/* 버그 1: type 비트를 안 지우고 ~rb + 1 */
static uint32_t naive_size_nomask(uint32_t rb) { return ~rb + 1; }

/* 버그 2: 모든 dword 를 독립 32비트 BAR 로 취급 (64비트 상위 절반도 "BAR" 로 디코드) */
static void naive_decode_each_dword(const uint32_t mask[PCI_STD_NUM_BARS], bar_kind kind[PCI_STD_NUM_BARS], uint32_t size[PCI_STD_NUM_BARS]) {
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        uint32_t m = mask[i];
        kind[i] = m == 0 ? BAR_NONE : (m & 1) ? BAR_IO : BAR_MEM32;
        uint32_t a = (m & 1) ? (m & PCI_BASE_ADDRESS_IO_MASK) : (m & PCI_BASE_ADDRESS_MEM_MASK);
        size[i] = a ? ~a + 1 : 0;
        if (a == 0) kind[i] = BAR_NONE;
    }
}

/* ======================= Part C: 배치 + 엔드포인트 FW ======================= */

typedef struct {
    uint64_t io_base, io_limit;    /* IO 창 */
    uint64_t mem_base, mem_limit;  /* 32비트 non-prefetchable 창 (64비트 NP BAR 도 여기, 4GB 아래) */
    uint64_t pf_base, pf_limit;    /* 64비트 prefetchable 창 */
} bar_windows;

static uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1) & ~(a - 1); }

/* status==BS_OK 인 BAR 를 index 순서로 창에 자연 정렬(addr % size == 0)로 배치.
 *   IO → io 창, Mem64+prefetch → pf 창, 나머지 Memory → mem 창.
 *   전부 들어가는지 먼저 계산 → 하나라도 limit 을 넘으면 BA_ERR_SPACE (config 쓰기 0번, out 불변)
 *   들어가면: decode 끈 상태에서 BAR(64비트는 lo, hi 둘 다) 쓰기 → Command 에 필요한 decode 비트 켜기 (나머지 비트 유지)
 *   out[i].addr 갱신 */
static int bar_assign(const cfg_ops *ops, bar_info out[PCI_STD_NUM_BARS], const bar_windows *w) {
    uint64_t cur[3] = { w->io_base, w->mem_base, w->pf_base };
    const uint64_t lim[3] = { w->io_limit, w->mem_limit, w->pf_limit };
    uint64_t addr[PCI_STD_NUM_BARS] = { 0 };
    uint16_t need = 0;
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        const bar_info *b = &out[i];
        if (b->status != BS_OK) continue;
        int win = b->kind == BAR_IO ? 0 : (b->kind == BAR_MEM64 && b->prefetch) ? 2 : 1;
        uint64_t a = align_up(cur[win], b->size);
        if (a < cur[win] || a + (b->size - 1) < a || a + (b->size - 1) > lim[win]) return BA_ERR_SPACE;
        if (win == 1 && a + (b->size - 1) > 0xFFFFFFFFull) return BA_ERR_SPACE;
        addr[i] = a;
        cur[win] = a + b->size;
        need |= b->kind == BAR_IO ? PCI_COMMAND_IO : PCI_COMMAND_MEMORY;
    }
    uint16_t cmd = ops->rd16(ops->ctx, PCI_COMMAND);
    if (cmd & PCI_COMMAND_DECODE_ENABLE) ops->wr16(ops->ctx, PCI_COMMAND, (uint16_t)(cmd & ~PCI_COMMAND_DECODE_ENABLE));
    for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
        bar_info *b = &out[i];
        if (b->status != BS_OK) continue;
        uint16_t off = (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i);
        ops->wr32(ops->ctx, off, (uint32_t)addr[i]);
        if (b->kind == BAR_MEM64) ops->wr32(ops->ctx, (uint16_t)(off + 4), (uint32_t)(addr[i] >> 32));
        b->addr = addr[i];
    }
    ops->wr16(ops->ctx, PCI_COMMAND, (uint16_t)((cmd & ~PCI_COMMAND_DECODE_ENABLE) | need));
    return BA_OK;
}

/* 엔드포인트(SSD 컨트롤러) FW 쪽: DWC 스타일 — BAR 레지스터(dbi)엔 type 비트, BAR mask 레지스터(dbi2)엔 size-1.
 * 규칙: size 는 2의 거듭제곱, IO 4..256 (prefetch 불가), Mem 16 이상, Mem32 ≤ 2GB, Mem64 ≤ 2^63,
 *       Mem64 는 bar ≤ 4 이고 짝수 번호 (DWC 는 홀수 시작 쌍 불가 — pcie-designware-ep.c), bar ≤ 5. */
typedef struct { uint32_t dbi_lo, dbi_hi, dbi2_lo, dbi2_hi; } ep_bar_regs;

static int ep_bar_program(unsigned bar, bar_kind k, bool pf, uint64_t size, ep_bar_regs *r) {
    if (k != BAR_IO && k != BAR_MEM32 && k != BAR_MEM64) return EP_ERR_KIND;
    if (k == BAR_IO && pf) return EP_ERR_KIND;
    if (bar >= PCI_STD_NUM_BARS) return EP_ERR_INDEX;
    if (k == BAR_MEM64 && ((bar & 1) || bar > 4)) return EP_ERR_INDEX;
    if (size == 0 || (size & (size - 1))) return EP_ERR_SIZE;
    if (k == BAR_IO && (size < 4 || size > 256)) return EP_ERR_SIZE;
    if (k != BAR_IO && size < 16) return EP_ERR_SIZE;
    if (k == BAR_MEM32 && size > 0x80000000ull) return EP_ERR_SIZE;
    if (k == BAR_MEM64 && size > 0x8000000000000000ull) return EP_ERR_SIZE;
    r->dbi_lo = k == BAR_IO ? PCI_BASE_ADDRESS_SPACE_IO
              : (k == BAR_MEM64 ? PCI_BASE_ADDRESS_MEM_TYPE_64 : 0) | (pf ? PCI_BASE_ADDRESS_MEM_PREFETCH : 0);
    r->dbi_hi = 0;
    r->dbi2_lo = (uint32_t)(size - 1);
    r->dbi2_hi = k == BAR_MEM64 ? (uint32_t)((size - 1) >> 32) : 0;
    return EP_OK;
}

/* 실리콘 쪽: dbi2 마스크 → 쓰기 마스크 (주소 비트 = ~mask, 하위 type 비트는 항상 읽기 전용) */
static void mock_bar_from_ep(mock_dev *d, int i, bar_kind k, const ep_bar_regs *r) {
    uint32_t low = k == BAR_IO ? 0x3u : 0xFu;
    mock_set_bar_raw(d, i, k, r->dbi_lo, ~r->dbi2_lo & ~low, 0);
    if (k == BAR_MEM64) mock_set_bar_raw(d, i + 1, BAR_UPPER, 0, ~r->dbi2_hi, 0);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { printf("FAIL %s  (line %d)\n", #expr, __LINE__); } } while (0)

static const char *kind_name(bar_kind k) {
    static const char *n[] = { "none", "IO", "Mem32", "Mem64", "upper", "BAD" };
    return n[k];
}

static void fmt_size(uint64_t x, char *buf, size_t cap) {  /* lspci 의 [size=...] 방식: 1024 로 나눠지는 동안 나눈다 */
    static const char *suf[] = { "", "K", "M", "G", "T" };
    unsigned i = 0;
    while (i < 4 && x && x % 1024 == 0) { x /= 1024; i++; }
    snprintf(buf, cap, "%" PRIu64 "%s", x, suf[i]);
}

/* 이 레슨의 가상 장치: 펌웨어(UEFI 등)가 이미 주소를 써 둔 상태, Command = IO|MEM|MASTER */
static void make_main_dev(mock_dev *d) {
    mock_init(d, 0x1E0F, 0x0001, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    mock_set_bar(d, 0, BAR_IO, false, 256, 0x1000);
    mock_set_bar(d, 1, BAR_MEM32, false, 4096, 0x10010000);
    mock_set_bar(d, 2, BAR_MEM64, true, 256ull << 20, 0x8000000000ull);
    mock_set_bar(d, 4, BAR_MEM32, false, 64 * 1024, 0x10000000);
    /* BAR5: 구현 안 함 → 0 고정 */
}

static void print_table(const uint32_t orig[6], const uint32_t mask[6], const bar_info b[6]) {
    printf("  BAR off  orig       after_all1s  kind   pf  size        addr\n");
    for (int i = 0; i < 6; i++) {
        char s[16]; fmt_size(b[i].size, s, sizeof s);
        printf("  %d   0x%02X 0x%08" PRIX32 " 0x%08" PRIX32 "   %-6s %-3s %-11s 0x%" PRIX64 "\n", i, PCI_BASE_ADDRESS_0 + 4 * i,
               orig[i], mask[i], kind_name(b[i].kind), b[i].prefetch ? "yes" : "no", b[i].status == BS_OK ? s : "-", b[i].addr);
    }
}

/* 없는 장치: 모든 읽기가 all-1s, 쓰기는 세기만 */
static uint32_t g_absent_writes;
static uint16_t a_rd16(void *c, uint16_t o) { (void)c; (void)o; return 0xFFFF; }
static uint32_t a_rd32(void *c, uint16_t o) { (void)c; (void)o; return 0xFFFFFFFFu; }
static void a_wr16(void *c, uint16_t o, uint16_t v) { (void)c; (void)o; (void)v; g_absent_writes++; }
static void a_wr32(void *c, uint16_t o, uint32_t v) { (void)c; (void)o; (void)v; g_absent_writes++; }

int main(void) {
    /* ---------- Part A: 종류/크기 디코드 단위 테스트 ---------- */
    printf("== Part A: bar_kind_of / bar_size_from_readback ==\n");
    bool pf; uint64_t sz;
    CHECK(bar_kind_of(0xFFFFFF01u, &pf) == BAR_IO && !pf);
    CHECK(bar_kind_of(0xFFFFF000u, &pf) == BAR_MEM32 && !pf);
    CHECK(bar_kind_of(0xF000000Cu, &pf) == BAR_MEM64 && pf);
    CHECK(bar_kind_of(0x00000008u, &pf) == BAR_MEM32 && pf);
    CHECK(bar_kind_of(0x00000002u, &pf) == BAR_MEM32);          /* 1M type: 32비트로 취급 */
    CHECK(bar_kind_of(0x00000006u, &pf) == BAR_BAD);            /* type 11b reserved */
    CHECK(bar_size_from_readback(0xFFFFFF01u, BAR_IO, &sz) == BS_OK && sz == 256);
    CHECK(bar_size_from_readback(0xFFFFF000u, BAR_MEM32, &sz) == BS_OK && sz == 4096);
    CHECK(bar_size_from_readback(0xFFFF0000u, BAR_MEM32, &sz) == BS_OK && sz == 65536);
    CHECK(bar_size_from_readback(0xFFFFFFFFF000000Cull, BAR_MEM64, &sz) == BS_OK && sz == (256ull << 20));
    CHECK(bar_size_from_readback(0xFFFFFFFFFFFFC004ull, BAR_MEM64, &sz) == BS_OK && sz == 0x4000);  /* QEMU nvme 실측 */
    CHECK(bar_size_from_readback(0, BAR_MEM32, &sz) == BS_UNIMPL && sz == 0);

    /* 전수: 모든 합법 크기에서 "~(size-1) | type" 을 읽으면 그 size 가 나온다 */
    int bad = 0;
    for (int b = 2; b <= 31; b++) {
        uint64_t s = 1ull << b;
        if (b <= 8 && (bar_size_from_readback(((uint32_t)~(s - 1) & ~3u) | 1u, BAR_IO, &sz) != BS_OK || sz != s)) bad++;
        if (b >= 4 && (bar_size_from_readback(((uint32_t)~(s - 1) & ~0xFu) | 8u, BAR_MEM32, &sz) != BS_OK || sz != s)) bad++;
    }
    for (int b = 4; b <= 63; b++) {
        uint64_t s = 1ull << b;
        if (bar_size_from_readback((~(s - 1) & ~0xFull) | 0xC, BAR_MEM64, &sz) != BS_OK || sz != s) bad++;
    }
    CHECK(bad == 0);

    /* ---------- Part A: 가상 장치 전체 sizing ---------- */
    printf("== Part A: sizing routine on the mock device ==\n");
    mock_dev d; make_main_dev(&d);
    cfg_ops ops = mock_ops(&d);
    uint8_t before[256]; memcpy(before, d.cfg, 256);
    uint32_t orig[6], mask[6]; bar_info bi[6];
    CHECK(bar_read_masks(&ops, orig, mask, true) == BR_OK);
    int nok = bar_decode_all(orig, mask, bi);
    print_table(orig, mask, bi);
    CHECK(nok == 4);
    CHECK(mask[0] == 0xFFFFFF01u && mask[1] == 0xFFFFF000u && mask[2] == 0xF000000Cu && mask[3] == 0xFFFFFFFFu);
    CHECK(mask[4] == 0xFFFF0000u && mask[5] == 0);
    CHECK(bi[0].kind == BAR_IO && bi[0].size == 256 && bi[0].addr == 0x1000);
    CHECK(bi[1].kind == BAR_MEM32 && !bi[1].prefetch && bi[1].size == 4096 && bi[1].addr == 0x10010000);
    CHECK(bi[2].kind == BAR_MEM64 && bi[2].prefetch && bi[2].size == (256ull << 20) && bi[2].addr == 0x8000000000ull);
    CHECK(bi[3].kind == BAR_UPPER && bi[3].status == BS_UPPER);
    CHECK(bi[4].kind == BAR_MEM32 && bi[4].size == 65536 && bi[4].addr == 0x10000000);
    CHECK(bi[5].kind == BAR_NONE && bi[5].status == BS_UNIMPL);
    CHECK(memcmp(before, d.cfg, 256) == 0);                       /* BAR·Command 전부 원래대로 */
    CHECK(cfg_rd16(d.cfg, PCI_COMMAND) == 0x0007);
    /* 쓰기 순서: Command(decode off, Master 유지) → BAR 12번 → Command 복원 */
    CHECK(d.cfg_writes == 14 && d.wlog_off[0] == PCI_COMMAND && d.wlog_val[0] == PCI_COMMAND_MASTER);
    CHECK(d.wlog_off[13] == PCI_COMMAND && d.wlog_val[13] == 0x0007);
    int order_ok = 1;
    for (int i = 0; i < 6; i++) {
        uint16_t off = (uint16_t)(PCI_BASE_ADDRESS_0 + 4 * i);
        if (d.wlog_off[1 + 2 * i] != off || d.wlog_val[1 + 2 * i] != 0xFFFFFFFFu) order_ok = 0;
        if (d.wlog_off[2 + 2 * i] != off || d.wlog_val[2 + 2 * i] != orig[i]) order_ok = 0;
    }
    CHECK(order_ok);
    CHECK(d.transient_on == 0 && d.nhits == 0);

    /* Command 가 원래 0 (드라이버 없음, QEMU nvme 실측과 같은 상황) → Command 는 건드리지 않는다 */
    mock_dev d0; make_main_dev(&d0); d0.cfg[PCI_COMMAND] = 0;
    cfg_ops ops0 = mock_ops(&d0);
    CHECK(bar_probe_all(&ops0, bi) == 4 && d0.cfg_writes == 12 && cfg_rd16(d0.cfg, PCI_COMMAND) == 0);

    /* 실측 데이터: QEMU 10.2.1 nvme (capture_qemu_bar_sizing.txt) */
    const uint32_t q_orig[6] = { 0x10000004u, 0, 0, 0, 0, 0 };
    const uint32_t q_mask[6] = { 0xffffc004u, 0xffffffffu, 0, 0, 0, 0 };
    CHECK(bar_decode_all(q_orig, q_mask, bi) == 1);
    CHECK(bi[0].kind == BAR_MEM64 && !bi[0].prefetch && bi[0].size == 0x4000 && bi[0].addr == 0x10000000ull);
    CHECK(bi[1].kind == BAR_UPPER && bi[2].kind == BAR_NONE);
    /* 실측: Lima VM 00:06.0 BAR0/1 원래 값 0x80020004 / 0x00000003 → 64비트 주소 0x380020000 */
    CHECK(bar_kind_of(0x80020004u, &pf) == BAR_MEM64 && !pf);
    CHECK((((uint64_t)0x00000003u << 32 | 0x80020004u) & ~0xFull) == 0x380020000ull);

    /* ---------- Part B: 오류·버그 재현 ---------- */
    printf("== Part B: classic bugs and faults ==\n");
    /* 버그 1: 하위 비트 안 지움 */
    printf("  naive ~rb+1: IO 0xFFFFFF01 -> 0x%08" PRIX32 ", Mem64 lo 0xF000000C -> 0x%08" PRIX32 ", Mem32 pf 0xFFFFF008 -> 0x%08" PRIX32 "\n",
           naive_size_nomask(0xFFFFFF01u), naive_size_nomask(0xF000000Cu), naive_size_nomask(0xFFFFF008u));
    CHECK(naive_size_nomask(0xFFFFFF01u) == 0xFF);                /* 정답 256 */
    CHECK(naive_size_nomask(0xF000000Cu) == 0x0FFFFFF4u);         /* 정답 0x10000000 */
    CHECK(naive_size_nomask(0xFFFFF000u) == 0x1000);              /* Mem32 NP 는 type 비트가 0 이라 우연히 맞음 */
    /* 버그 2: 상위 절반을 별도 BAR 로 */
    bar_kind nk[6]; uint32_t ns[6];
    naive_decode_each_dword(mask, nk, ns);
    printf("  naive per-dword: BAR3 (upper half 0xFFFFFFFF) -> %s size %" PRIu32 "\n", kind_name(nk[3]), ns[3]);
    CHECK(nk[3] == BAR_IO && ns[3] == 4);                         /* 4바이트짜리 유령 IO BAR */
    CHECK(nk[2] == BAR_MEM32 && ns[2] == 0x10000000u);            /* 크기는 우연히 맞지만 주소 상위 32비트를 잃음 */

    /* 8GB prefetchable 64비트 BAR: 하위 dword 주소 비트가 전부 0 → 상위 절반 없이는 "구현 안 됨"으로 보인다 */
    mock_dev g; mock_init(&g, 0x10DE, 0x2330, 0);
    mock_set_bar(&g, 0, BAR_MEM32, false, 16u << 20, 0);
    mock_set_bar(&g, 2, BAR_MEM64, true, 8ull << 30, 0);
    cfg_ops gops = mock_ops(&g);
    CHECK(bar_read_masks(&gops, orig, mask, true) == BR_OK);
    CHECK(mask[2] == 0x0000000Cu && mask[3] == 0xFFFFFFFEu);
    CHECK(bar_decode_all(orig, mask, bi) == 2 && bi[2].kind == BAR_MEM64 && bi[2].size == (8ull << 30));
    naive_decode_each_dword(mask, nk, ns);
    printf("  8GB BAR naive per-dword: BAR2 -> %s, BAR3 -> %s size %" PRIu32 "\n", kind_name(nk[2]), kind_name(nk[3]), ns[3]);
    CHECK(nk[2] == BAR_NONE && nk[3] == BAR_MEM32 && ns[3] == 16); /* 진짜 BAR 는 사라지고 16바이트 유령 */

    /* decode 를 켠 채 sizing: 다른 에이전트의 접근이 반쯤 쓰인 BAR 에 걸린다 */
    mock_dev t; make_main_dev(&t);
    t.traffic[0] = (mock_access){ SP_MEM, 0xFFFFF000ull };     /* 4GB 바로 아래 — 다른 장치/펌웨어 영역이라 가정 */
    t.traffic[1] = (mock_access){ SP_MEM, 0x80F0000000ull };   /* 이웃 장치의 64비트 prefetch 영역이라 가정 */
    t.ntraffic = 2;
    cfg_ops tops = mock_ops(&t);
    CHECK(bar_read_masks(&tops, orig, mask, false) == BR_OK);
    for (uint32_t h = 0; h < t.nhits; h++)
        printf("  decode ON: after cfg write #%" PRIu32 " BAR%u claimed 0x%" PRIX64 "\n", t.hits[h].after_write, t.hits[h].bar, t.hits[h].addr);
    printf("  decode ON: transient (write,BAR) events = %" PRIu32 ", hits = %" PRIu32 "\n", t.transient_on, t.nhits);
    CHECK(t.transient_on == 5 && t.nhits == 3);
    CHECK(t.hits[0].bar == 1 && t.hits[0].addr == 0xFFFFF000ull && t.hits[0].after_write == 3);
    CHECK(t.hits[1].bar == 2 && t.hits[1].addr == 0x80F0000000ull && t.hits[1].after_write == 5);
    CHECK(t.hits[2].bar == 4 && t.hits[2].addr == 0xFFFFF000ull && t.hits[2].after_write == 9);
    mock_dev t2; make_main_dev(&t2); t2.traffic[0] = t.traffic[0]; t2.traffic[1] = t.traffic[1]; t2.ntraffic = 2;
    cfg_ops t2ops = mock_ops(&t2);
    CHECK(bar_probe_all(&t2ops, bi) == 4 && t2.transient_on == 0 && t2.nhits == 0);

    /* BAR5 가 64비트, reserved type, 비연속 마스크, 16비트 IO 디코드 */
    mock_dev e; mock_init(&e, 0x1234, 0x5678, PCI_COMMAND_MEMORY);
    mock_set_bar_raw(&e, 0, BAR_IO, 0x1, 0x0000FF00u, 0);            /* IO 256B, 상위 16비트 0 하드와이어 */
    mock_set_bar_raw(&e, 1, BAR_BAD, 0x6, 0xFFFFF000u, 0);           /* type 11b */
    mock_set_bar_raw(&e, 2, BAR_MEM32, 0x0, 0xFFFF0F00u, 0);         /* 고장: 비연속 마스크 */
    mock_set_bar_raw(&e, 5, BAR_MEM64, 0x4, 0xFFFFF000u, 0);         /* 64비트인데 짝이 없음 */
    cfg_ops eops = mock_ops(&e);
    uint8_t ebefore[256]; memcpy(ebefore, e.cfg, 256);
    CHECK(bar_read_masks(&eops, orig, mask, true) == BR_OK);
    CHECK(mask[0] == 0x0000FF01u);
    CHECK(bar_decode_all(orig, mask, bi) == 1);
    CHECK(bi[0].status == BS_OK && bi[0].size == 256);
    CHECK(naive_size_nomask(mask[0] & PCI_BASE_ADDRESS_IO_MASK) == 0xFFFF0100u);  /* ~m+1 은 16비트 IO 에서 틀림 */
    CHECK(bi[1].status == BS_BADTYPE && bi[2].status == BS_BADMASK && bi[5].status == BS_64_AT_LAST);
    CHECK(memcmp(ebefore, e.cfg, 256) == 0);

    /* 없는 장치: VID 0xFFFF → 쓰기 0번 */
    cfg_ops aops = { NULL, a_rd16, a_rd32, a_wr16, a_wr32 };
    g_absent_writes = 0;
    CHECK(bar_read_masks(&aops, orig, mask, true) == BR_ERR_NODEV && g_absent_writes == 0);
    /* 마스크가 통째로 all-1s (응답 없음) 인 dword 는 BADMASK */
    const uint32_t f_orig[6] = { 0xFFFFFFFFu, 0, 0, 0, 0, 0 }, f_mask[6] = { 0xFFFFFFFFu, 0, 0, 0, 0, 0 };
    CHECK(bar_decode_all(f_orig, f_mask, bi) == 0 && bi[0].status == BS_BADMASK);

    /* ---------- Part C: 배치 + 확인 ---------- */
    printf("== Part C: assign + decode check ==\n");
    mock_dev c; make_main_dev(&c);
    cfg_ops cops = mock_ops(&c);
    CHECK(bar_probe_all(&cops, bi) == 4);
    const bar_windows win = { 0x1000, 0xFFFF, 0x10000000, 0x3EFEFFFF, 0x8000000000ull, 0xFFFFFFFFFFull };
    CHECK(bar_assign(&cops, bi, &win) == BA_OK);
    for (int i = 0; i < 6; i++) {
        if (bi[i].status != BS_OK) continue;
        char s[16]; fmt_size(bi[i].size, s, sizeof s);
        if (bi[i].kind == BAR_IO) printf("  Region %d: I/O ports at %04" PRIx64 " [size=%s]\n", i, bi[i].addr, s);
        else printf("  Region %d: Memory at %" PRIx64 " (%s, %s) [size=%s]\n", i, bi[i].addr, bi[i].kind == BAR_MEM64 ? "64-bit" : "32-bit",
                    bi[i].prefetch ? "prefetchable" : "non-prefetchable", s);
    }
    CHECK(bi[0].addr == 0x1000 && bi[1].addr == 0x10000000 && bi[2].addr == 0x8000000000ull && bi[4].addr == 0x10010000);
    CHECK(mock_bar_val(&c, 1) == 0x10000000u && mock_bar_val(&c, 2) == 0x0000000Cu && mock_bar_val(&c, 3) == 0x80u);
    CHECK(cfg_rd16(c.cfg, PCI_COMMAND) == 0x0007);
    uint64_t off = 0;
    CHECK(mock_decode(&c, SP_MEM, 0x10010000ull + 0x1234, &off) == 4 && off == 0x1234);
    CHECK(mock_decode(&c, SP_MEM, 0x8000000000ull + 0xFFFFFFF, &off) == 2 && off == 0xFFFFFFF);
    CHECK(mock_decode(&c, SP_IO, 0x10FF, &off) == 0 && off == 0xFF);
    CHECK(mock_decode(&c, SP_MEM, 0x10001000ull, &off) == -1);     /* BAR1(4K) 과 BAR4 사이 정렬 빈틈 */
    /* 창이 모자람 → 아무것도 안 쓴다 */
    mock_dev c2; make_main_dev(&c2); cfg_ops c2ops = mock_ops(&c2);
    CHECK(bar_probe_all(&c2ops, bi) == 4);
    uint32_t w0 = c2.cfg_writes;
    const bar_windows small = { 0x1000, 0xFFFF, 0x10000000, 0x1000FFFF, 0x8000000000ull, 0x800FFFFFFFull };
    CHECK(bar_assign(&c2ops, bi, &small) == BA_ERR_SPACE && c2.cfg_writes == w0 && bi[4].addr == 0x10000000);

    /* 엔드포인트 FW: 값 확인 + 전수 왕복 (EP 설정 → 실리콘 → 호스트 sizing) */
    ep_bar_regs r;
    CHECK(ep_bar_program(0, BAR_MEM64, false, 16 * 1024, &r) == EP_OK && r.dbi_lo == 0x4 && r.dbi2_lo == 0x3FFF && r.dbi2_hi == 0);
    CHECK(ep_bar_program(2, BAR_MEM64, true, 8ull << 30, &r) == EP_OK && r.dbi_lo == 0xC && r.dbi2_lo == 0xFFFFFFFFu && r.dbi2_hi == 1);
    CHECK(ep_bar_program(1, BAR_MEM64, false, 4096, &r) == EP_ERR_INDEX);
    CHECK(ep_bar_program(5, BAR_MEM32, false, 3000, &r) == EP_ERR_SIZE);
    CHECK(ep_bar_program(0, BAR_IO, false, 512, &r) == EP_ERR_SIZE);
    CHECK(ep_bar_program(0, BAR_IO, true, 256, &r) == EP_ERR_KIND);
    CHECK(ep_bar_program(6, BAR_MEM32, false, 4096, &r) == EP_ERR_INDEX);
    CHECK(ep_bar_program(0, BAR_MEM32, false, 8, &r) == EP_ERR_SIZE);
    int rt_cases = 0, rt_bad = 0;
    for (int kk = 0; kk < 3; kk++) {
        bar_kind k = kk == 0 ? BAR_IO : kk == 1 ? BAR_MEM32 : BAR_MEM64;
        for (int p = 0; p < 2; p++) for (unsigned bar = 0; bar < 6; bar++) for (int b = 2; b <= 63; b++) {
            if (ep_bar_program(bar, k, p, 1ull << b, &r) != EP_OK) continue;
            mock_dev x; mock_init(&x, 0x1E0F, 0x0007, PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
            mock_bar_from_ep(&x, (int)bar, k, &r);
            cfg_ops xo = mock_ops(&x);
            rt_cases++;
            if (bar_probe_all(&xo, bi) != 1 || bi[bar].kind != k || bi[bar].prefetch != (bool)p || bi[bar].size != (1ull << b)) rt_bad++;
        }
    }
    printf("  EP -> silicon -> host round trip: %d cases, %d bad\n", rt_cases, rt_bad);
    CHECK(rt_cases == 7 * 6 + 2 * 28 * 6 + 2 * 60 * 3 && rt_bad == 0);   /* IO 7크기×6 + Mem32 28×2×6 + Mem64 60×2×3 = 738 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
