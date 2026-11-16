/* PCIE-B07 Type 1 헤더 디코드 + 스위치 라우터 (bus number, memory window)
 *
 * config space = little-endian 바이트 배열. 읽기는 cfg_rd8/16/32 로 바이트를 조립한다 (비정렬 캐스트 금지).
 * 오프셋·비트 이름은 Linux include/uapi/linux/pci_regs.h 와 같은 이름/값 (맥에서 헤더 없이 빌드되도록 직접 #define).
 *
 * Part A: t1_decode  — Primary/Secondary/Subordinate, IO base/limit(16/32), Memory base/limit,
 *                      Prefetchable base/limit(+upper 32), Bridge Control.  win_contains (빈 윈도우 = base > limit)
 * Part B: route_mem_down — USP 로 내려온 메모리 요청이 어느 DSP 로 가나 (NOT_MINE / UR / CONFLICT 포함)
 *         route_cfg      — bus B, dev D 로 가는 config 요청: 내부 버스 Type 0 / DSP Type 0 / DSP Type 1 / UR / NOT_MINE
 * Part C: route_mem_up   — DSP 아래 장치가 보낸 메모리 요청(DMA): 위로(UPSTREAM) / 옆 DSP(P2P) / UR
 *         diag_ep_bar    — "엔드포인트 BAR 가 0xFFFFFFFF 로 읽힌다" 브링업 진단
 *
 * 테스트 데이터: (1) QEMU 실캡처 (pcie-root-port → x3130 USP → xio3130 DSP ×3, NVMe 2개; qemu_switch_capture.txt)
 *               (2) 손으로 만든 "가상 스위치" (64비트 prefetchable 윈도우가 4GB 위, 빈 윈도우, 구멍, 버스 9 미할당)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- pci_regs.h 와 같은 이름/값 ---- */
#define PCI_COMMAND               0x04
#define  PCI_COMMAND_IO           0x1
#define  PCI_COMMAND_MEMORY       0x2
#define  PCI_COMMAND_MASTER       0x4
#define PCI_HEADER_TYPE           0x0e
#define  PCI_HEADER_TYPE_MASK     0x7f
#define  PCI_HEADER_TYPE_BRIDGE   1
#define PCI_PRIMARY_BUS           0x18
#define PCI_SECONDARY_BUS         0x19
#define PCI_SUBORDINATE_BUS       0x1a
#define PCI_SEC_LATENCY_TIMER     0x1b
#define PCI_IO_BASE               0x1c
#define PCI_IO_LIMIT              0x1d
#define  PCI_IO_RANGE_TYPE_MASK   0x0fu
#define  PCI_IO_RANGE_TYPE_16     0x00
#define  PCI_IO_RANGE_TYPE_32     0x01
#define  PCI_IO_RANGE_MASK        (~0x0fu)
#define PCI_MEMORY_BASE           0x20
#define PCI_MEMORY_LIMIT          0x22
#define  PCI_MEMORY_RANGE_MASK    (~0x0fu)
#define PCI_PREF_MEMORY_BASE      0x24
#define PCI_PREF_MEMORY_LIMIT     0x26
#define  PCI_PREF_RANGE_TYPE_MASK 0x0fu
#define  PCI_PREF_RANGE_TYPE_32   0x00
#define  PCI_PREF_RANGE_TYPE_64   0x01
#define  PCI_PREF_RANGE_MASK      (~0x0fu)
#define PCI_PREF_BASE_UPPER32     0x28
#define PCI_PREF_LIMIT_UPPER32    0x2c
#define PCI_IO_BASE_UPPER16       0x30
#define PCI_IO_LIMIT_UPPER16      0x32
#define PCI_BRIDGE_CONTROL        0x3e
#define  PCI_BRIDGE_CTL_PARITY    0x01
#define  PCI_BRIDGE_CTL_SERR      0x02
#define  PCI_BRIDGE_CTL_ISA       0x04
#define  PCI_BRIDGE_CTL_VGA       0x08
#define  PCI_BRIDGE_CTL_MASTER_ABORT 0x20
#define  PCI_BRIDGE_CTL_BUS_RESET 0x40

#define T1_HDR_LEN 64u   /* Type 1 헤더는 0x00..0x3F */

static uint8_t  cfg_rd8 (const uint8_t *c, uint16_t o) { return c[o]; }
static uint16_t cfg_rd16(const uint8_t *c, uint16_t o) { return (uint16_t)(c[o] | (c[o + 1] << 8)); }
static uint32_t cfg_rd32(const uint8_t *c, uint16_t o) {
    return (uint32_t)c[o] | ((uint32_t)c[o + 1] << 8) | ((uint32_t)c[o + 2] << 16) | ((uint32_t)c[o + 3] << 24);
}
static void cfg_wr16(uint8_t *c, uint16_t o, uint16_t v) { c[o] = (uint8_t)v; c[o + 1] = (uint8_t)(v >> 8); }
static void cfg_wr32(uint8_t *c, uint16_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) c[o + i] = (uint8_t)(v >> (8 * i));
}

/* ---- lspci -x / -xxx / -xxxx 텍스트 → 바이트 (PCIE-B03 파서 축약판, 주어짐) ----
 * 줄 형식 "xx: b0 b1 ... b15" (오프셋은 0 부터 16 씩 연속). 반환: 바이트 수(16의 배수) 또는 -1(형식 오류/용량 초과). */
static int hexval(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static int cfg_parse_lspci(const char *t, uint8_t *out, size_t cap) {
    size_t n = 0;
    while (*t) {
        if (*t == '\n' || *t == ' ') { t++; continue; }
        unsigned off = 0; int digits = 0;
        while (hexval(*t) >= 0) { off = off * 16u + (unsigned)hexval(*t); t++; digits++; if (digits > 3) return -1; }
        if (digits == 0 || *t != ':' || off != n || n + 16 > cap) return -1;
        t++;
        for (int i = 0; i < 16; i++) {
            if (*t != ' ') return -1;
            t++;
            int hi = hexval(t[0]), lo = hi < 0 ? -1 : hexval(t[1]);
            if (hi < 0 || lo < 0) return -1;
            out[n++] = (uint8_t)(hi * 16 + lo);
            t += 2;
        }
        if (*t != '\n' && *t != '\0') return -1;
    }
    return (int)n;
}

/* ---------------- Part A: Type 1 디코드 ---------------- */

typedef struct { uint64_t base, limit; bool valid; } win_t;   /* valid = (base <= limit). limit 은 포함(inclusive) */

typedef struct {
    uint16_t command;
    uint8_t  pri, sec, sub, sec_lat;
    bool     io32;   win_t io;     /* IO 윈도우: 4KB 단위, 16비트 또는 32비트 */
    win_t    mem;                  /* non-prefetchable: 1MB 단위, 항상 32비트 주소 */
    bool     pref64; win_t pref;   /* prefetchable: 1MB 단위, 32 또는 64비트 */
    uint16_t bridge_ctl;
} t1_hdr;

enum { T1_OK = 0, T1_ERR_SHORT = -1, T1_ERR_NOT_TYPE1 = -2, T1_ERR_IO_TYPE = -3, T1_ERR_PREF_TYPE = -4 };

static int t1_decode(const uint8_t *cfg, size_t len, t1_hdr *h) {
    memset(h, 0, sizeof *h);
    if (cfg == NULL || len < T1_HDR_LEN) return T1_ERR_SHORT;
    if ((cfg_rd8(cfg, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE) return T1_ERR_NOT_TYPE1;
    h->command = cfg_rd16(cfg, PCI_COMMAND);
    h->pri = cfg_rd8(cfg, PCI_PRIMARY_BUS);
    h->sec = cfg_rd8(cfg, PCI_SECONDARY_BUS);
    h->sub = cfg_rd8(cfg, PCI_SUBORDINATE_BUS);
    h->sec_lat = cfg_rd8(cfg, PCI_SEC_LATENCY_TIMER);
    /* IO: 레지스터 [7:4] = 주소 [15:12], [3:0] = 타입(0=16비트, 1=32비트). base/limit 타입 nibble 은 같아야 한다 */
    uint8_t iob = cfg_rd8(cfg, PCI_IO_BASE), iol = cfg_rd8(cfg, PCI_IO_LIMIT);
    unsigned iot = iob & PCI_IO_RANGE_TYPE_MASK;
    if (iot > PCI_IO_RANGE_TYPE_32 || iot != (iol & PCI_IO_RANGE_TYPE_MASK)) return T1_ERR_IO_TYPE;
    h->io32 = (iot == PCI_IO_RANGE_TYPE_32);
    h->io.base  = (uint64_t)(iob & PCI_IO_RANGE_MASK & 0xFFu) << 8;
    h->io.limit = ((uint64_t)(iol & PCI_IO_RANGE_MASK & 0xFFu) << 8) | 0xFFFu;
    if (h->io32) {
        h->io.base  |= (uint64_t)cfg_rd16(cfg, PCI_IO_BASE_UPPER16) << 16;
        h->io.limit |= (uint64_t)cfg_rd16(cfg, PCI_IO_LIMIT_UPPER16) << 16;
    }
    h->io.valid = h->io.base <= h->io.limit;
    /* Memory: 레지스터 [15:4] = 주소 [31:20], [3:0] 은 읽기전용 0 (마스크) */
    h->mem.base  = (uint64_t)(cfg_rd16(cfg, PCI_MEMORY_BASE)  & PCI_MEMORY_RANGE_MASK & 0xFFFFu) << 16;
    h->mem.limit = ((uint64_t)(cfg_rd16(cfg, PCI_MEMORY_LIMIT) & PCI_MEMORY_RANGE_MASK & 0xFFFFu) << 16) | 0xFFFFFu;
    h->mem.valid = h->mem.base <= h->mem.limit;
    /* Prefetchable: 같은 모양 + 타입 1 이면 0x28/0x2C 가 주소 [63:32] */
    uint16_t pb = cfg_rd16(cfg, PCI_PREF_MEMORY_BASE), pl = cfg_rd16(cfg, PCI_PREF_MEMORY_LIMIT);
    unsigned pt = pb & PCI_PREF_RANGE_TYPE_MASK;
    if (pt > PCI_PREF_RANGE_TYPE_64 || pt != (pl & PCI_PREF_RANGE_TYPE_MASK)) return T1_ERR_PREF_TYPE;
    h->pref64 = (pt == PCI_PREF_RANGE_TYPE_64);
    h->pref.base  = (uint64_t)(pb & PCI_PREF_RANGE_MASK & 0xFFFFu) << 16;
    h->pref.limit = ((uint64_t)(pl & PCI_PREF_RANGE_MASK & 0xFFFFu) << 16) | 0xFFFFFu;
    if (h->pref64) {
        h->pref.base  |= (uint64_t)cfg_rd32(cfg, PCI_PREF_BASE_UPPER32) << 32;
        h->pref.limit |= (uint64_t)cfg_rd32(cfg, PCI_PREF_LIMIT_UPPER32) << 32;
    }
    h->pref.valid = h->pref.base <= h->pref.limit;
    h->bridge_ctl = cfg_rd16(cfg, PCI_BRIDGE_CONTROL);
    return T1_OK;
}

static bool win_contains(win_t w, uint64_t a) {
    return w.valid && a >= w.base && a <= w.limit;
}

/* ---------------- Part B: 스위치 라우터 ---------------- */

#define SW_MAX_DSP 8
typedef struct {
    t1_hdr  usp;
    t1_hdr  dsp[SW_MAX_DSP];
    uint8_t dsp_dev[SW_MAX_DSP];   /* 내부 버스(usp.sec) 위에서 DSP 의 device 번호 */
    int     n_dsp;
} pcie_switch;

typedef enum { RT_NOT_MINE, RT_UPSTREAM, RT_DSP, RT_INTERNAL, RT_UR, RT_CONFLICT } rt_kind;
typedef struct { rt_kind kind; int port; int cfg_type; } route_t;   /* port = DSP 인덱스, cfg_type = 0/1 (config 만) */

static route_t mk_route(rt_kind k, int port, int type) { route_t r = { k, port, type }; return r; }

static bool in_mem_windows(const t1_hdr *b, uint64_t a) { return win_contains(b->mem, a) || win_contains(b->pref, a); }

/* USP 의 primary(위)로 들어온 메모리 요청 */
static route_t route_mem_down(const pcie_switch *sw, uint64_t addr) {
    if (!(sw->usp.command & PCI_COMMAND_MEMORY) || !in_mem_windows(&sw->usp, addr))
        return mk_route(RT_NOT_MINE, -1, -1);
    int hit = -1, nhit = 0;
    for (int i = 0; i < sw->n_dsp; i++) {
        const t1_hdr *d = &sw->dsp[i];
        if ((d->command & PCI_COMMAND_MEMORY) && in_mem_windows(d, addr)) { if (hit < 0) hit = i; nhit++; }
    }
    if (nhit == 0) return mk_route(RT_UR, -1, -1);          /* USP 는 받았는데 내부 버스에서 아무도 안 받음 */
    if (nhit > 1)  return mk_route(RT_CONFLICT, hit, -1);   /* 윈도우 겹침 = 설정 오류 */
    return mk_route(RT_DSP, hit, -1);
}

/* USP 의 primary 로 들어온 config 요청 (목표 bus, dev). bus == usp.pri 인 Type 0 (USP 자기 자신)은 여기로 안 온다 */
static route_t route_cfg(const pcie_switch *sw, uint8_t bus, uint8_t dev) {
    const t1_hdr *u = &sw->usp;
    if (u->sec > u->sub || bus < u->sec || bus > u->sub) return mk_route(RT_NOT_MINE, -1, -1);
    if (bus == u->sec) {                                    /* 내부 버스: Type 0 으로 바꿔 DSP 기능에게 */
        for (int i = 0; i < sw->n_dsp; i++)
            if (sw->dsp_dev[i] == dev) return mk_route(RT_INTERNAL, i, 0);
        return mk_route(RT_UR, -1, -1);
    }
    int hit = -1, nhit = 0;
    for (int i = 0; i < sw->n_dsp; i++) {
        const t1_hdr *d = &sw->dsp[i];
        if (d->sec <= d->sub && bus >= d->sec && bus <= d->sub) { if (hit < 0) hit = i; nhit++; }
    }
    if (nhit == 0) return mk_route(RT_UR, -1, -1);
    if (nhit > 1)  return mk_route(RT_CONFLICT, hit, -1);
    if (bus == sw->dsp[hit].sec) {                          /* 링크 바로 아래: Type 1 → Type 0, 링크엔 dev 0 뿐 */
        if (dev != 0) return mk_route(RT_UR, -1, -1);
        return mk_route(RT_DSP, hit, 0);
    }
    return mk_route(RT_DSP, hit, 1);                        /* 더 아래 버스: Type 1 그대로 */
}

/* ---------------- Part C: 위로 가는 요청 + 브링업 진단 ---------------- */

/* DSP ingress 의 secondary(아래)에서 올라온 메모리 요청 (예: NVMe 의 DMA 쓰기) */
static route_t route_mem_up(const pcie_switch *sw, int ingress, uint64_t addr) {
    if (ingress < 0 || ingress >= sw->n_dsp) return mk_route(RT_UR, -1, -1);
    const t1_hdr *in = &sw->dsp[ingress];
    if (!(in->command & PCI_COMMAND_MASTER)) return mk_route(RT_UR, -1, -1);   /* BME=0: 위로 안 보냄 */
    if (in_mem_windows(in, addr)) return mk_route(RT_UR, -1, -1);              /* 자기 아래 주소: 위로 안 보냄 */
    int hit = -1, nhit = 0;
    for (int i = 0; i < sw->n_dsp; i++) {
        if (i == ingress) continue;
        const t1_hdr *d = &sw->dsp[i];
        if ((d->command & PCI_COMMAND_MEMORY) && in_mem_windows(d, addr)) { if (hit < 0) hit = i; nhit++; }
    }
    if (nhit > 1) return mk_route(RT_CONFLICT, hit, -1);
    if (nhit == 1) return mk_route(RT_DSP, hit, -1);                           /* 스위치 안 P2P */
    if (in_mem_windows(&sw->usp, addr)) return mk_route(RT_UR, -1, -1);        /* USP 아래 구멍 */
    if (!(sw->usp.command & PCI_COMMAND_MASTER)) return mk_route(RT_UR, -1, -1);
    return mk_route(RT_UPSTREAM, -1, -1);
}

enum { DIAG_OK = 0, DIAG_BAD_ARG, DIAG_USP_MEM_OFF, DIAG_DSP_MEM_OFF, DIAG_NOT_IN_USP, DIAG_NOT_IN_DSP, DIAG_SHADOWED };

static const char *diag_str(int d) {
    switch (d) {
    case DIAG_OK:          return "OK: USP·DSP 윈도우가 BAR 전체를 덮고 디코드 켜짐";
    case DIAG_BAD_ARG:     return "인자 오류 (size 0 / 주소 넘침 / DSP 번호)";
    case DIAG_USP_MEM_OFF: return "USP Command.Memory=0 → 스위치가 안 받음 → UR → CPU 는 0xFFFFFFFF";
    case DIAG_DSP_MEM_OFF: return "DSP Command.Memory=0 → 내부 버스에서 아무도 안 받음 → UR";
    case DIAG_NOT_IN_USP:  return "BAR 가 USP 윈도우 밖 (또는 걸침) → 상위에서 이 스위치로 안 옴";
    case DIAG_NOT_IN_DSP:  return "BAR 가 DSP 윈도우 밖 (또는 걸침) → 스위치까지 와도 UR";
    case DIAG_SHADOWED:    return "다른 DSP 윈도우와 겹침 → 라우팅 모호 (설정 오류)";
    }
    return "?";
}

static bool win_covers(win_t w, uint64_t lo, uint64_t hi) { return w.valid && lo >= w.base && hi <= w.limit; }

/* DSP dsp 아래 엔드포인트의 BAR [bar, bar+size-1] 에 CPU 가 닿는가? */
static int diag_ep_bar(const pcie_switch *sw, int dsp, uint64_t bar, uint64_t size) {
    if (dsp < 0 || dsp >= sw->n_dsp || size == 0 || bar + (size - 1) < bar) return DIAG_BAD_ARG;
    uint64_t hi = bar + (size - 1);
    const t1_hdr *u = &sw->usp, *d = &sw->dsp[dsp];
    if (!(u->command & PCI_COMMAND_MEMORY)) return DIAG_USP_MEM_OFF;
    if (!(d->command & PCI_COMMAND_MEMORY)) return DIAG_DSP_MEM_OFF;
    if (!win_covers(u->mem, bar, hi) && !win_covers(u->pref, bar, hi)) return DIAG_NOT_IN_USP;
    if (!win_covers(d->mem, bar, hi) && !win_covers(d->pref, bar, hi)) return DIAG_NOT_IN_DSP;
    route_t r1 = route_mem_down(sw, bar), r2 = route_mem_down(sw, hi);
    if (r1.kind != RT_DSP || r1.port != dsp || r2.kind != RT_DSP || r2.port != dsp) return DIAG_SHADOWED;
    return DIAG_OK;
}

/* ---------------- 주어진 도우미: 스위치 조립, 출력 ---------------- */

static int sw_build(pcie_switch *sw, const uint8_t *usp, const uint8_t *const *dsps, const uint8_t *devs, int n) {
    memset(sw, 0, sizeof *sw);
    if (n < 0 || n > SW_MAX_DSP) return -10;
    int rc = t1_decode(usp, T1_HDR_LEN, &sw->usp);
    if (rc) return rc;
    for (int i = 0; i < n; i++) {
        if ((rc = t1_decode(dsps[i], T1_HDR_LEN, &sw->dsp[i])) != 0) return rc;
        sw->dsp_dev[i] = devs[i];
    }
    sw->n_dsp = n;
    return 0;
}

static void print_win(const char *name, win_t w) {
    if (w.valid) printf(" %s %#llx-%#llx", name, (unsigned long long)w.base, (unsigned long long)w.limit);
    else         printf(" %s (empty)", name);
}
static void t1_print(const char *bdf, const t1_hdr *h) {
    printf("%s bus %02x/%02x/%02x cmd=%04x%s%s%s", bdf, h->pri, h->sec, h->sub, h->command,
           (h->command & PCI_COMMAND_IO) ? " IO+" : " IO-", (h->command & PCI_COMMAND_MEMORY) ? " Mem+" : " Mem-",
           (h->command & PCI_COMMAND_MASTER) ? " BusMaster+" : " BusMaster-");
    printf("\n   "); print_win(h->io32 ? "io32" : "io16", h->io); print_win("mem", h->mem);
    print_win(h->pref64 ? "pref64" : "pref32", h->pref);
    printf(" bctl=%04x%s%s\n", h->bridge_ctl, (h->bridge_ctl & PCI_BRIDGE_CTL_SERR) ? " SERR" : "",
           (h->bridge_ctl & PCI_BRIDGE_CTL_BUS_RESET) ? " SecBusReset" : "");
}

/* ---------------- 테스트 데이터 ---------------- */

/* (1) QEMU 실캡처 (qemu_switch_capture.txt, 첫 64바이트만). QEMU 에뮬레이션 값 — 실칩 아님 */
static const char *RP_00_02_0 =
    "00: 36 1b 0c 00 07 04 10 00 00 00 04 06 00 00 01 00\n"
    "10: 00 10 78 10 00 00 00 00 00 01 05 00 10 40 00 00\n"
    "20: 00 10 60 10 01 00 81 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 54 00 00 00 00 00 00 00 0f 01 02 00\n";
static const char *USP_01_00_0 =
    "00: 4c 10 32 82 07 00 10 00 02 00 04 06 00 00 01 00\n"
    "10: 00 00 00 00 00 00 00 00 01 02 05 00 10 30 00 00\n"
    "20: 00 10 60 10 01 00 81 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 90 00 00 00 00 00 00 00 00 00 02 00\n";
static const char *DSP_02_00_0 =
    "00: 4c 10 33 82 07 04 10 00 01 00 04 06 00 00 01 00\n"
    "10: 00 00 00 00 00 00 00 00 02 03 03 00 10 10 00 00\n"
    "20: 00 10 10 10 01 00 11 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 90 00 00 00 00 00 00 00 00 00 02 00\n";
static const char *DSP_02_01_0 =
    "00: 4c 10 33 82 07 04 10 00 01 00 04 06 00 00 01 00\n"
    "10: 00 00 00 00 00 00 00 00 02 04 04 00 20 20 00 00\n"
    "20: 20 10 30 10 21 00 31 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 90 00 00 00 00 00 00 00 00 00 02 00\n";
static const char *DSP_02_02_0 =
    "00: 4c 10 33 82 07 04 10 00 01 00 04 06 00 00 01 00\n"
    "10: 00 00 00 00 00 00 00 00 02 05 05 00 30 30 00 00\n"
    "20: 40 10 50 10 41 00 51 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 90 00 00 00 00 00 00 00 00 00 02 00\n";
/* NVMe 03:00.0 (첫 32바이트만) — Type 0 → t1_decode 가 거절해야 함 */
static const char *NVME_03_00_0 =
    "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n"
    "10: 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00\n";

/* (2) 가상 스위치: 레지스터 값을 직접 써서 만든다 */
#define MEMREG(a) ((uint16_t)(((uint64_t)(a) >> 16) & 0xFFF0u))
static void t1_make(uint8_t *c, uint16_t cmd, uint8_t pri, uint8_t sec, uint8_t sub, uint8_t iob, uint8_t iol,
                    uint16_t memb, uint16_t meml, uint16_t pfb, uint16_t pfl, uint32_t pfbu, uint32_t pflu) {
    memset(c, 0, 256);
    cfg_wr16(c, 0x00, 0x1234); cfg_wr16(c, 0x02, 0xABCD);           /* 가상 VID:DID */
    cfg_wr16(c, PCI_COMMAND, cmd);
    c[0x0b] = 0x06; c[0x0a] = 0x04; c[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_BRIDGE;
    c[PCI_PRIMARY_BUS] = pri; c[PCI_SECONDARY_BUS] = sec; c[PCI_SUBORDINATE_BUS] = sub;
    c[PCI_IO_BASE] = iob; c[PCI_IO_LIMIT] = iol;
    cfg_wr16(c, PCI_MEMORY_BASE, memb); cfg_wr16(c, PCI_MEMORY_LIMIT, meml);
    cfg_wr16(c, PCI_PREF_MEMORY_BASE, pfb); cfg_wr16(c, PCI_PREF_MEMORY_LIMIT, pfl);
    cfg_wr32(c, PCI_PREF_BASE_UPPER32, pfbu); cfg_wr32(c, PCI_PREF_LIMIT_UPPER32, pflu);
    cfg_wr16(c, PCI_BRIDGE_CONTROL, PCI_BRIDGE_CTL_SERR);
}
static uint8_t V_USP[256], V_DSP[4][256];
static const uint8_t V_DEVS[4] = { 0, 1, 2, 3 };
static void make_virtual(void) {
    /*          cmd   pri sec sub  iob   iol   mem base           mem limit          pref lo  pref lo  upper32 */
    t1_make(V_USP,    7, 1, 2, 9, 0x10, 0x30, MEMREG(0xC0000000), MEMREG(0xC0FFFFFF), 0x0001, 0x3FF1, 0x40, 0x40);
    t1_make(V_DSP[0], 7, 2, 3, 3, 0x10, 0x10, MEMREG(0xC0000000), MEMREG(0xC00FFFFF), 0x0001, 0x0FF1, 0x40, 0x40);
    t1_make(V_DSP[1], 7, 2, 4, 6, 0x20, 0x20, MEMREG(0xC0100000), MEMREG(0xC03FFFFF), 0x1001, 0x1FF1, 0x40, 0x40);
    t1_make(V_DSP[2], 7, 2, 7, 7, 0xF0, 0x00, 0xFFF0,             0x0000,             0xFFF1, 0x0001, 0x00, 0x00);
    t1_make(V_DSP[3], 7, 2, 8, 8, 0x30, 0x30, MEMREG(0xC0800000), MEMREG(0xC08FFFFF), 0xFFF1, 0x0001, 0x00, 0x00);
}

/* ---- 독립 참조 모델 (테스트 전용): 원시 바이트에서 "MB 번호"로 비교 ---- */
static bool ref_in(const uint8_t *c, uint64_t a) {
    uint64_t mb = a >> 20;                                     /* 1MB 단위 번호 */
    unsigned mb_lo = cfg_rd16(c, 0x20) >> 4, mb_hi = cfg_rd16(c, 0x22) >> 4;
    if (a < (1ull << 32) && mb_lo <= mb_hi && mb >= mb_lo && mb <= mb_hi) return true;
    uint64_t p_lo = cfg_rd16(c, 0x24) >> 4, p_hi = cfg_rd16(c, 0x26) >> 4;
    if (c[0x24] & 1) { p_lo |= (uint64_t)cfg_rd32(c, 0x28) << 12; p_hi |= (uint64_t)cfg_rd32(c, 0x2c) << 12; }
    else if (a >= (1ull << 32)) return false;
    return p_lo <= p_hi && mb >= p_lo && mb <= p_hi;
}
static route_t ref_route_down(const uint8_t *usp, uint8_t (*dsp)[256], int n, uint64_t a) {
    if (!(usp[4] & 2) || !ref_in(usp, a)) return mk_route(RT_NOT_MINE, -1, -1);
    int hit = -1, cnt = 0;
    for (int i = 0; i < n; i++) if ((dsp[i][4] & 2) && ref_in(dsp[i], a)) { cnt++; if (hit < 0) hit = i; }
    return cnt == 0 ? mk_route(RT_UR, -1, -1) : cnt > 1 ? mk_route(RT_CONFLICT, hit, -1) : mk_route(RT_DSP, hit, -1);
}
static route_t ref_route_cfg(const uint8_t *usp, uint8_t (*dsp)[256], const uint8_t *devs, int n, unsigned b, unsigned d) {
    uint8_t owner[256]; int cnt[256];                         /* 버스 번호 → 주인 표를 먼저 칠한다 */
    memset(owner, 0xFF, sizeof owner); memset(cnt, 0, sizeof cnt);
    for (int i = 0; i < n; i++)
        for (unsigned x = dsp[i][0x19]; x <= dsp[i][0x1a] && x < 256; x++) { if (!cnt[x]) owner[x] = (uint8_t)i; cnt[x]++; }
    if (usp[0x19] > usp[0x1a] || b < usp[0x19] || b > usp[0x1a]) return mk_route(RT_NOT_MINE, -1, -1);
    if (b == usp[0x19]) { for (int i = 0; i < n; i++) if (devs[i] == d) return mk_route(RT_INTERNAL, i, 0);
                          return mk_route(RT_UR, -1, -1); }
    if (cnt[b] == 0) return mk_route(RT_UR, -1, -1);
    if (cnt[b] > 1) return mk_route(RT_CONFLICT, owner[b], -1);
    if (b == dsp[owner[b]][0x19]) return d ? mk_route(RT_UR, -1, -1) : mk_route(RT_DSP, owner[b], 0);
    return mk_route(RT_DSP, owner[b], 1);
}

static uint32_t rng = 0x2545F491u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static bool R(route_t r, rt_kind k, int port) { return r.kind == k && (port < 0 || r.port == port); }
static bool RC(route_t r, rt_kind k, int port, int type) { return R(r, k, port) && r.cfg_type == type; }

int main(void) {
    uint8_t rp[256], usp[256], dsp[3][256], nv[256];
    t1_hdr h;
    /* ---- Part A: 실캡처 디코드 (값은 게스트 /proc/iomem, /proc/ioports 와 대조) ---- */
    CHECK(cfg_parse_lspci(RP_00_02_0, rp, sizeof rp) == 64);
    CHECK(cfg_parse_lspci(USP_01_00_0, usp, sizeof usp) == 64);
    CHECK(cfg_parse_lspci(DSP_02_00_0, dsp[0], 256) == 64 && cfg_parse_lspci(DSP_02_01_0, dsp[1], 256) == 64
          && cfg_parse_lspci(DSP_02_02_0, dsp[2], 256) == 64);
    CHECK(cfg_parse_lspci(NVME_03_00_0, nv, sizeof nv) == 64);
    CHECK(cfg_parse_lspci("00: 36 1b\n", nv, sizeof nv) == -1);                     /* 잘린 줄 */
    CHECK(t1_decode(rp, 64, &h) == T1_OK);
    CHECK(h.pri == 0x00 && h.sec == 0x01 && h.sub == 0x05 && h.command == 0x0407);
    CHECK(!h.io32 && h.io.valid && h.io.base == 0x1000 && h.io.limit == 0x4FFF);     /* ioports 00001000-00004fff */
    CHECK(h.mem.valid && h.mem.base == 0x10000000 && h.mem.limit == 0x106FFFFF);     /* iomem 10000000-106fffff */
    CHECK(h.pref64 && h.pref.valid && h.pref.base == 0x8000000000ull && h.pref.limit == 0x80008FFFFFull);
    CHECK(h.bridge_ctl == PCI_BRIDGE_CTL_SERR);
    t1_print("00:02.0 RP ", &h);
    CHECK(t1_decode(usp, 64, &h) == T1_OK && h.pri == 1 && h.sec == 2 && h.sub == 5 && h.io.limit == 0x3FFF);
    t1_print("01:00.0 USP", &h);
    CHECK(t1_decode(dsp[0], 64, &h) == T1_OK && h.sec == 3 && h.sub == 3 && h.mem.base == 0x10000000
          && h.mem.limit == 0x101FFFFF && h.pref.base == 0x8000000000ull && h.pref.limit == 0x80001FFFFFull);
    t1_print("02:00.0 DSP", &h);
    CHECK(t1_decode(dsp[1], 64, &h) == T1_OK && h.sec == 4 && h.io.base == 0x2000 && h.io.limit == 0x2FFF
          && h.mem.base == 0x10200000 && h.mem.limit == 0x103FFFFF && h.pref.limit == 0x80003FFFFFull);
    t1_print("02:01.0 DSP", &h);
    CHECK(t1_decode(dsp[2], 64, &h) == T1_OK && h.sec == 5 && h.mem.base == 0x10400000
          && h.mem.limit == 0x105FFFFF && h.pref.base == 0x8000400000ull);
    t1_print("02:02.0 DSP", &h);
    /* ---- Part A: 손으로 만든 경우 + 오류 ---- */
    make_virtual();
    CHECK(t1_decode(V_DSP[2], 64, &h) == T1_OK && !h.mem.valid && !h.pref.valid && !h.io.valid); /* 빈 윈도우 */
    uint8_t t[256];
    memcpy(t, V_DSP[0], 256); t[PCI_IO_BASE] = 0x21; t[PCI_IO_LIMIT] = 0x31;
    cfg_wr16(t, PCI_IO_BASE_UPPER16, 0x0001); cfg_wr16(t, PCI_IO_LIMIT_UPPER16, 0x0001);
    CHECK(t1_decode(t, 64, &h) == T1_OK && h.io32 && h.io.base == 0x12000 && h.io.limit == 0x13FFF);
    memcpy(t, V_DSP[0], 256); cfg_wr16(t, PCI_PREF_MEMORY_BASE, 0x0000); cfg_wr16(t, PCI_PREF_MEMORY_LIMIT, 0x0FF0);
    CHECK(t1_decode(t, 64, &h) == T1_OK && !h.pref64 && h.pref.base == 0 && h.pref.limit == 0x0FFFFFFF); /* 32비트: upper 무시 */
    memcpy(t, V_DSP[0], 256); t[PCI_HEADER_TYPE] = 0x81;
    CHECK(t1_decode(t, 64, &h) == T1_OK && h.sec == 3);                              /* multi-function 비트는 무시 */
    CHECK(t1_decode(V_DSP[0], 63, &h) == T1_ERR_SHORT);
    CHECK(t1_decode(nv, 64, &h) == T1_ERR_NOT_TYPE1);                                /* 실 NVMe = Type 0 */
    memcpy(t, V_DSP[0], 256); t[PCI_PREF_MEMORY_BASE] = 0x02; t[PCI_PREF_MEMORY_LIMIT] = 0x02;
    CHECK(t1_decode(t, 64, &h) == T1_ERR_PREF_TYPE);                                 /* 타입 2 = 예약 */
    memcpy(t, V_DSP[0], 256); t[PCI_PREF_MEMORY_LIMIT] = 0xF0;
    CHECK(t1_decode(t, 64, &h) == T1_ERR_PREF_TYPE);                                 /* base 64비트, limit 32비트 */
    memcpy(t, V_DSP[0], 256); t[PCI_IO_BASE] = 0x11; t[PCI_IO_LIMIT] = 0x10;
    CHECK(t1_decode(t, 64, &h) == T1_ERR_IO_TYPE);
    win_t w = { 0x10000000, 0x101FFFFF, true }, e = { 0x10000000, 0x0FFFFFFF, false }, top = { 0xFFFFFFFFFFF00000ull, ~0ull, true };
    CHECK(!win_contains(w, 0x0FFFFFFF) && win_contains(w, 0x10000000) && win_contains(w, 0x101FFFFF) && !win_contains(w, 0x10200000));
    CHECK(!win_contains(e, 0x10000000) && win_contains(top, ~0ull) && !win_contains(top, 0xFFFFFFFFFFEFFFFFull));

    /* ---- Part B: 가상 스위치 라우팅 ---- */
    pcie_switch sw;
    const uint8_t *vd[4] = { V_DSP[0], V_DSP[1], V_DSP[2], V_DSP[3] };
    CHECK(sw_build(&sw, V_USP, vd, V_DEVS, 4) == 0);
    CHECK(R(route_mem_down(&sw, 0xC0000000), RT_DSP, 0) && R(route_mem_down(&sw, 0xC00FFFFF), RT_DSP, 0));
    CHECK(R(route_mem_down(&sw, 0xC0100000), RT_DSP, 1) && R(route_mem_down(&sw, 0xC03FFFFF), RT_DSP, 1));
    CHECK(R(route_mem_down(&sw, 0xC0400000), RT_UR, -1) && R(route_mem_down(&sw, 0xC07FFFFF), RT_UR, -1));   /* 구멍 */
    CHECK(R(route_mem_down(&sw, 0xC0800000), RT_DSP, 3) && R(route_mem_down(&sw, 0xC0900000), RT_UR, -1));
    CHECK(R(route_mem_down(&sw, 0xBFFFFFFF), RT_NOT_MINE, -1) && R(route_mem_down(&sw, 0xC1000000), RT_NOT_MINE, -1));
    CHECK(R(route_mem_down(&sw, 0x4000000000ull), RT_DSP, 0) && R(route_mem_down(&sw, 0x400FFFFFFFull), RT_DSP, 0));
    CHECK(R(route_mem_down(&sw, 0x4010000000ull), RT_DSP, 1) && R(route_mem_down(&sw, 0x4020000000ull), RT_UR, -1));
    CHECK(R(route_mem_down(&sw, 0x4040000000ull), RT_NOT_MINE, -1));
    CHECK(R(route_mem_down(&sw, 0x00100000), RT_NOT_MINE, -1));          /* upper32 를 버리면 DSP0 로 잘못 감 */
    CHECK(R(route_mem_down(&sw, 0x1C0000000ull), RT_NOT_MINE, -1));      /* non-pref 윈도우는 32비트: 별칭 없음 */
    sw.dsp[0].command &= (uint16_t)~PCI_COMMAND_MEMORY;
    CHECK(R(route_mem_down(&sw, 0xC0000000), RT_UR, -1));                /* DSP 디코드 꺼짐 */
    sw.dsp[0].command |= PCI_COMMAND_MEMORY; sw.usp.command &= (uint16_t)~PCI_COMMAND_MEMORY;
    CHECK(R(route_mem_down(&sw, 0xC0000000), RT_NOT_MINE, -1));          /* USP 디코드 꺼짐 */
    sw.usp.command |= PCI_COMMAND_MEMORY;
    /* config */
    CHECK(RC(route_cfg(&sw, 2, 1), RT_INTERNAL, 1, 0) && R(route_cfg(&sw, 2, 5), RT_UR, -1));
    CHECK(RC(route_cfg(&sw, 3, 0), RT_DSP, 0, 0) && R(route_cfg(&sw, 3, 1), RT_UR, -1));
    CHECK(RC(route_cfg(&sw, 4, 0), RT_DSP, 1, 0) && RC(route_cfg(&sw, 5, 7), RT_DSP, 1, 1) && RC(route_cfg(&sw, 6, 0), RT_DSP, 1, 1));
    CHECK(RC(route_cfg(&sw, 7, 0), RT_DSP, 2, 0) && RC(route_cfg(&sw, 8, 0), RT_DSP, 3, 0) && R(route_cfg(&sw, 9, 0), RT_UR, -1));
    CHECK(R(route_cfg(&sw, 1, 0), RT_NOT_MINE, -1) && R(route_cfg(&sw, 10, 0), RT_NOT_MINE, -1) && R(route_cfg(&sw, 0, 0), RT_NOT_MINE, -1));
    /* 전수: bus 0..255 × dev 0..31 를 참조 모델과 비교 */
    int bad = 0;
    for (unsigned b = 0; b < 256; b++)
        for (unsigned d = 0; d < 32; d++) {
            route_t a = route_cfg(&sw, (uint8_t)b, (uint8_t)d);
            route_t r = ref_route_cfg(V_USP, V_DSP, V_DEVS, 4, b, d);
            if (a.kind != r.kind || a.port != r.port || a.cfg_type != r.cfg_type) bad++;
        }
    CHECK(bad == 0);
    /* 경계 + 랜덤 주소 20000개를 참조 모델과 비교 */
    bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t a;
        if (i & 1) a = ((uint64_t)(xorshift32() & 0xFF) << 32) | xorshift32();
        else { static const uint64_t edges[] = { 0xC0000000, 0xC0100000, 0xC0400000, 0xC0800000, 0xC0900000, 0xC1000000,
                                                 0x4000000000ull, 0x4010000000ull, 0x4020000000ull, 0x4040000000ull };
               a = edges[xorshift32() % 10] + (uint64_t)((int64_t)(xorshift32() % 65) - 32); }
        route_t x = route_mem_down(&sw, a), r = ref_route_down(V_USP, V_DSP, 4, a);
        if (x.kind != r.kind || x.port != r.port) bad++;
    }
    CHECK(bad == 0);
    /* 설정 오류: 겹치는 윈도우 / 겹치는 버스 / sec > sub */
    memcpy(t, V_DSP[3], 256); cfg_wr16(t, PCI_MEMORY_BASE, MEMREG(0xC0000000)); t[PCI_SECONDARY_BUS] = 5; t[PCI_SUBORDINATE_BUS] = 5;
    const uint8_t *vd2[4] = { V_DSP[0], V_DSP[1], V_DSP[2], t };
    pcie_switch bad_sw;
    CHECK(sw_build(&bad_sw, V_USP, vd2, V_DEVS, 4) == 0);
    CHECK(R(route_mem_down(&bad_sw, 0xC0000000), RT_CONFLICT, 0) && R(route_cfg(&bad_sw, 5, 0), RT_CONFLICT, 1));
    t[PCI_SECONDARY_BUS] = 9; t[PCI_SUBORDINATE_BUS] = 8;
    CHECK(sw_build(&bad_sw, V_USP, vd2, V_DEVS, 4) == 0 && R(route_cfg(&bad_sw, 8, 0), RT_UR, -1) && R(route_cfg(&bad_sw, 9, 0), RT_UR, -1));

    /* ---- Part C: 실캡처 스위치 (USP 01:00.0, DSP 02:00.0 / 02:01.0 / 02:02.0) ---- */
    const uint8_t *qd[3] = { dsp[0], dsp[1], dsp[2] };
    static const uint8_t qdevs[3] = { 0, 1, 2 };
    pcie_switch q;
    CHECK(sw_build(&q, usp, qd, qdevs, 3) == 0);
    CHECK(R(route_mem_down(&q, 0x10000000), RT_DSP, 0));   /* NVMe 03:00.0 BAR0 */
    CHECK(R(route_mem_down(&q, 0x10400000), RT_DSP, 2));   /* NVMe 05:00.0 BAR0 */
    CHECK(R(route_mem_down(&q, 0x10200000), RT_DSP, 1));   /* 빈 슬롯도 윈도우는 예약됨 (hotplug 여유) */
    CHECK(R(route_mem_down(&q, 0x10600000), RT_UR, -1));   /* USP 윈도우 안, DSP 셋 다 아님 */
    CHECK(R(route_mem_down(&q, 0x10700000), RT_NOT_MINE, -1) && R(route_mem_down(&q, 0x10781000), RT_NOT_MINE, -1));
    CHECK(R(route_mem_down(&q, 0x8000000000ull), RT_DSP, 0) && R(route_mem_down(&q, 0x8000500000ull), RT_DSP, 2));
    CHECK(RC(route_cfg(&q, 3, 0), RT_DSP, 0, 0) && RC(route_cfg(&q, 4, 0), RT_DSP, 1, 0) && RC(route_cfg(&q, 2, 2), RT_INTERNAL, 2, 0));
    CHECK(R(route_cfg(&q, 6, 0), RT_NOT_MINE, -1) && R(route_cfg(&q, 1, 0), RT_NOT_MINE, -1));
    CHECK(R(route_mem_up(&q, 0, 0x40000000), RT_UPSTREAM, -1));   /* NVMe DMA → 시스템 RAM */
    CHECK(R(route_mem_up(&q, 0, 0x10400000), RT_DSP, 2));          /* P2P: NVMe → 옆 NVMe BAR */
    CHECK(R(route_mem_up(&q, 0, 0x10000000), RT_UR, -1));          /* 자기 아래 주소 */
    CHECK(R(route_mem_up(&q, 0, 0x10600000), RT_UR, -1));          /* USP 아래 구멍 */
    CHECK(R(route_mem_up(&q, 7, 0x40000000), RT_UR, -1));          /* 없는 포트 */
    q.dsp[0].command &= (uint16_t)~PCI_COMMAND_MASTER;
    CHECK(R(route_mem_up(&q, 0, 0x40000000), RT_UR, -1));          /* DSP BusMaster 꺼짐 → DMA 막힘 */
    q.dsp[0].command |= PCI_COMMAND_MASTER; q.usp.command &= (uint16_t)~PCI_COMMAND_MASTER;
    CHECK(R(route_mem_up(&q, 0, 0x40000000), RT_UR, -1) && R(route_mem_up(&q, 0, 0x10400000), RT_DSP, 2));
    q.usp.command |= PCI_COMMAND_MASTER;
    /* 브링업 진단 */
    CHECK(diag_ep_bar(&q, 0, 0x10000000, 0x4000) == DIAG_OK && diag_ep_bar(&q, 2, 0x10400000, 0x4000) == DIAG_OK);
    CHECK(diag_ep_bar(&q, 0, 0x10700000, 0x4000) == DIAG_NOT_IN_USP);
    CHECK(diag_ep_bar(&q, 0, 0x10400000, 0x4000) == DIAG_NOT_IN_DSP);          /* BAR 가 엉뚱한 DSP 윈도우 */
    CHECK(diag_ep_bar(&q, 0, 0x101FF000, 0x2000) == DIAG_NOT_IN_DSP);          /* 윈도우 끝에 걸침 */
    CHECK(diag_ep_bar(&q, 0, 0, 0) == DIAG_BAD_ARG && diag_ep_bar(&q, 0, ~0ull, 2) == DIAG_BAD_ARG && diag_ep_bar(&q, 3, 0, 1) == DIAG_BAD_ARG);
    q.dsp[0].command &= (uint16_t)~PCI_COMMAND_MEMORY;
    CHECK(diag_ep_bar(&q, 0, 0x10000000, 0x4000) == DIAG_DSP_MEM_OFF);
    q.dsp[0].command |= PCI_COMMAND_MEMORY; q.usp.command &= (uint16_t)~PCI_COMMAND_MEMORY;
    CHECK(diag_ep_bar(&q, 0, 0x10000000, 0x4000) == DIAG_USP_MEM_OFF);
    q.usp.command |= PCI_COMMAND_MEMORY;
    CHECK(diag_ep_bar(&bad_sw, 0, 0xC0000000, 0x1000) == DIAG_SHADOWED);     /* DSP3 윈도우가 DSP0 를 덮음 */
    CHECK(diag_ep_bar(&sw, 0, 0xC0000000, 0x1000) == DIAG_OK && diag_ep_bar(&sw, 0, 0x4000000000ull, 0x10000000) == DIAG_OK);
    CHECK(diag_ep_bar(&sw, 2, 0xC0000000, 0x1000) == DIAG_NOT_IN_DSP);       /* 빈 윈도우 DSP */
    printf("diag(03:00.0 BAR @0x10700000): %s\n", diag_str(diag_ep_bar(&q, 0, 0x10700000, 0x4000)));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
