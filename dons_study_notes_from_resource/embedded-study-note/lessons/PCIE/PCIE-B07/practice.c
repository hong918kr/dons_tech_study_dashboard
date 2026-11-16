/* PCIE-B07 추가 연습 — 한 번에 검증
 *
 * 본문(solution.c)의 디코더·라우터를 그대로 담고(자기완결), 연습 4개를 더한다.
 *   연습 1: 윈도우 인코더 t1_set_mem_window / t1_set_pref_window / t1_disable_mem_window (디코더와 왕복)
 *   연습 2: 스위치 설정 lint — DSP 버스 범위·윈도우가 USP 안에 있나, 서로 겹치나 (validation 체크리스트)
 *   연습 3: 여러 단 브리지를 건너는 경로 — bus 번호 / 메모리 주소 → 루트 포트부터의 브리지 목록 (루프 방지)
 *   연습 4: IO 윈도우 라우팅 + Bridge Control VGA Enable (0xA0000-0xBFFFF, 0x3B0-0x3BB, 0x3C0-0x3DF)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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


/* ======================= 추가 연습 ======================= */

/* ---- 연습 1: 윈도우 인코더 (펌웨어가 bridge 에 윈도우를 쓰는 쪽) ---- */
enum { W_OK = 0, W_ERR_ORDER = -1, W_ERR_ALIGN = -2, W_ERR_RANGE = -3 };
#define MB (1ull << 20)
static int t1_set_mem_window(uint8_t *cfg, uint64_t base, uint64_t limit) {
    if (base > limit) return W_ERR_ORDER;
    if ((base % MB) != 0 || ((limit + 1) % MB) != 0) return W_ERR_ALIGN;
    if (limit > 0xFFFFFFFFull) return W_ERR_RANGE;               /* non-prefetchable 윈도우는 32비트뿐 */
    cfg_wr16(cfg, PCI_MEMORY_BASE,  (uint16_t)((base  >> 16) & 0xFFF0u));
    cfg_wr16(cfg, PCI_MEMORY_LIMIT, (uint16_t)((limit >> 16) & 0xFFF0u));
    return W_OK;
}
static void t1_disable_mem_window(uint8_t *cfg) {             /* Linux 도 쓰는 관용구: base 0xFFF0 > limit 0x0000 */
    cfg_wr16(cfg, PCI_MEMORY_BASE, 0xFFF0); cfg_wr16(cfg, PCI_MEMORY_LIMIT, 0x0000);
}
static int t1_set_pref_window(uint8_t *cfg, uint64_t base, uint64_t limit) {
    unsigned type = cfg_rd16(cfg, PCI_PREF_MEMORY_BASE) & PCI_PREF_RANGE_TYPE_MASK;   /* HW 가 정한 읽기전용 nibble: 보존 */
    if (base > limit) return W_ERR_ORDER;
    if ((base % MB) != 0 || ((limit + 1) % MB) != 0) return W_ERR_ALIGN;           /* limit = 2^64-1 이면 +1 = 0 → 정렬 OK */
    if (type != PCI_PREF_RANGE_TYPE_64 && limit > 0xFFFFFFFFull) return W_ERR_RANGE;
    cfg_wr16(cfg, PCI_PREF_MEMORY_BASE,  (uint16_t)(((base  >> 16) & 0xFFF0u) | type));
    cfg_wr16(cfg, PCI_PREF_MEMORY_LIMIT, (uint16_t)(((limit >> 16) & 0xFFF0u) | type));
    if (type == PCI_PREF_RANGE_TYPE_64) {
        cfg_wr32(cfg, PCI_PREF_BASE_UPPER32, (uint32_t)(base >> 32));
        cfg_wr32(cfg, PCI_PREF_LIMIT_UPPER32, (uint32_t)(limit >> 32));
    }
    return W_OK;
}

/* ---- 연습 2: 스위치 설정 lint (validation 체크리스트) ---- */
enum { LINT_PRI_MISMATCH = 1, LINT_BUS_OUTSIDE = 2, LINT_BUS_OVERLAP = 4, LINT_WIN_OUTSIDE = 8, LINT_WIN_OVERLAP = 16 };
static bool win_overlap(win_t a, win_t b) { return a.valid && b.valid && a.base <= b.limit && b.base <= a.limit; }
static bool win_inside(win_t in, win_t out) { return !in.valid || (out.valid && in.base >= out.base && in.limit <= out.limit); }
static unsigned sw_lint(const pcie_switch *sw) {
    const t1_hdr *u = &sw->usp;
    unsigned f = 0;
    for (int i = 0; i < sw->n_dsp; i++) {
        const t1_hdr *d = &sw->dsp[i];
        if (d->pri != u->sec) f |= LINT_PRI_MISMATCH;
        if (d->sec <= d->sub && (d->sec <= u->sec || d->sub > u->sub)) f |= LINT_BUS_OUTSIDE;
        if (!win_inside(d->mem, u->mem) || !win_inside(d->pref, u->pref)) f |= LINT_WIN_OUTSIDE;
        for (int j = i + 1; j < sw->n_dsp; j++) {
            const t1_hdr *e = &sw->dsp[j];
            if (d->sec <= d->sub && e->sec <= e->sub && d->sec <= e->sub && e->sec <= d->sub) f |= LINT_BUS_OVERLAP;
            if (win_overlap(d->mem, e->mem) || win_overlap(d->pref, e->pref) ||
                win_overlap(d->mem, e->pref) || win_overlap(d->pref, e->mem)) f |= LINT_WIN_OVERLAP;
        }
    }
    return f;
}

/* ---- 연습 3: 여러 단 브리지를 건너는 경로 (root bus 0 에서 출발) ---- */
typedef struct { const char *bdf; t1_hdr h; } bridge_node;
#define PATH_ERR_NOROUTE (-1)
#define PATH_ERR_LOOP    (-2)
static int path_to_bus(const bridge_node *nd, int n, uint8_t bus, int *out, int cap) {
    unsigned cur = 0; int hops = 0;
    while (cur != bus) {
        int k = -1;
        for (int i = 0; i < n; i++)
            if (nd[i].h.pri == cur && nd[i].h.sec <= nd[i].h.sub && bus >= nd[i].h.sec && bus <= nd[i].h.sub) { k = i; break; }
        if (k < 0) return PATH_ERR_NOROUTE;
        if (hops >= cap || hops >= 256) return PATH_ERR_LOOP;     /* 버스 번호가 안 늘면 끝없이 돈다 */
        out[hops++] = k;
        cur = nd[k].h.sec;
    }
    return hops;
}
static int path_to_addr(const bridge_node *nd, int n, uint64_t a, int *out, int cap, uint8_t *final_bus) {
    unsigned cur = 0; int hops = 0;
    for (;;) {
        int k = -1;
        for (int i = 0; i < n; i++)
            if (nd[i].h.pri == cur && (nd[i].h.command & PCI_COMMAND_MEMORY) && in_mem_windows(&nd[i].h, a)) { k = i; break; }
        if (k < 0) { *final_bus = (uint8_t)cur; return hops; }   /* 이 버스 위의 누군가(BAR)가 받거나, 아무도 없음 */
        if (hops >= cap || hops >= 256) return PATH_ERR_LOOP;
        out[hops++] = k;
        cur = nd[k].h.sec;
    }
}

/* ---- 연습 4: IO 라우팅 + Bridge Control VGA Enable ---- */
static bool vga_io(uint32_t p)  { return (p >= 0x3B0 && p <= 0x3BB) || (p >= 0x3C0 && p <= 0x3DF); }
static bool vga_mem(uint64_t a) { return a >= 0xA0000 && a <= 0xBFFFF; }
static bool claims_io(const t1_hdr *b, uint32_t p) {
    if (!(b->command & PCI_COMMAND_IO)) return false;
    return win_contains(b->io, p) || ((b->bridge_ctl & PCI_BRIDGE_CTL_VGA) && vga_io(p));
}
static bool claims_mem_vga(const t1_hdr *b, uint64_t a) {
    if (!(b->command & PCI_COMMAND_MEMORY)) return false;
    return in_mem_windows(b, a) || ((b->bridge_ctl & PCI_BRIDGE_CTL_VGA) && vga_mem(a));
}
static route_t route_down_generic(const pcie_switch *sw, bool io, uint64_t a) {
    bool up = io ? claims_io(&sw->usp, (uint32_t)a) : claims_mem_vga(&sw->usp, a);
    if (!up) return mk_route(RT_NOT_MINE, -1, -1);
    int hit = -1, nhit = 0;
    for (int i = 0; i < sw->n_dsp; i++) {
        bool c = io ? claims_io(&sw->dsp[i], (uint32_t)a) : claims_mem_vga(&sw->dsp[i], a);
        if (c) { if (hit < 0) hit = i; nhit++; }
    }
    return nhit == 0 ? mk_route(RT_UR, -1, -1) : nhit > 1 ? mk_route(RT_CONFLICT, hit, -1) : mk_route(RT_DSP, hit, -1);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static bool R(route_t r, rt_kind k, int port) { return r.kind == k && (port < 0 || r.port == port); }

int main(void) {
    uint8_t c[256], usp[256], rp[256], dsp[3][256], nv[256];
    t1_hdr h;
    make_virtual();
    /* 연습 1: 인코더 ↔ 디코더 왕복 */
    int bad = 0;
    for (unsigned f = 0; f < 4096; f++) {                     /* base 필드 전수, limit = 같은 MB / 4GB 끝 */
        uint64_t b = (uint64_t)f << 20;
        uint64_t lims[2] = { b + MB - 1, 0xFFFFFFFFull };
        for (int k = 0; k < 2; k++) {
            memcpy(c, V_DSP[0], 256);
            if (t1_set_mem_window(c, b, lims[k]) != W_OK || t1_decode(c, 64, &h) != T1_OK ||
                !h.mem.valid || h.mem.base != b || h.mem.limit != lims[k]) bad++;
        }
    }
    CHECK(bad == 0);
    uint32_t s = 0x1234567u; bad = 0;
    for (int i = 0; i < 10000; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        uint64_t b = ((uint64_t)s << 20) & 0x00FFFFFFFFF00000ull;       /* 56비트 안의 MB 정렬 주소 */
        uint64_t len = ((uint64_t)(s % 4096) + 1) * MB;
        memcpy(c, V_DSP[0], 256);                                        /* V_DSP[0] 는 64비트 prefetchable */
        if (t1_set_pref_window(c, b, b + len - 1) != W_OK || t1_decode(c, 64, &h) != T1_OK ||
            !h.pref64 || h.pref.base != b || h.pref.limit != b + len - 1) bad++;
    }
    CHECK(bad == 0);
    memcpy(c, V_DSP[0], 256);
    CHECK(t1_set_mem_window(c, 0xC0080000, 0xC00FFFFF) == W_ERR_ALIGN && t1_set_mem_window(c, 0xC0000000, 0xC00FFFFE) == W_ERR_ALIGN);
    CHECK(t1_set_mem_window(c, 0xC0100000, 0xC00FFFFF) == W_ERR_ORDER && t1_set_mem_window(c, 0x100000000ull, 0x1000FFFFFull) == W_ERR_RANGE);
    CHECK(t1_set_pref_window(c, 0xFFFFFFFFFFF00000ull, ~0ull) == W_OK && t1_decode(c, 64, &h) == T1_OK && h.pref.limit == ~0ull);
    memcpy(c, V_DSP[3], 256); cfg_wr16(c, PCI_PREF_MEMORY_BASE, 0xFFF0); cfg_wr16(c, PCI_PREF_MEMORY_LIMIT, 0x0000); /* 32비트 pref 브리지 */
    CHECK(t1_set_pref_window(c, 0x100000000ull, 0x1000FFFFFull) == W_ERR_RANGE);
    CHECK(t1_set_pref_window(c, 0xE0000000, 0xE00FFFFF) == W_OK && t1_decode(c, 64, &h) == T1_OK && !h.pref64 && h.pref.base == 0xE0000000);
    memcpy(c, V_DSP[0], 256); t1_disable_mem_window(c);
    CHECK(t1_decode(c, 64, &h) == T1_OK && !h.mem.valid && h.mem.base == 0xFFF00000 && h.mem.limit == 0x000FFFFF);

    /* 연습 2: lint */
    CHECK(cfg_parse_lspci(USP_01_00_0, usp, 256) == 64 && cfg_parse_lspci(DSP_02_00_0, dsp[0], 256) == 64 &&
          cfg_parse_lspci(DSP_02_01_0, dsp[1], 256) == 64 && cfg_parse_lspci(DSP_02_02_0, dsp[2], 256) == 64 &&
          cfg_parse_lspci(RP_00_02_0, rp, 256) == 64 && cfg_parse_lspci(NVME_03_00_0, nv, 256) == 64);
    pcie_switch q, sw;
    const uint8_t *qd[3] = { dsp[0], dsp[1], dsp[2] };
    static const uint8_t qdevs[3] = { 0, 1, 2 };
    const uint8_t *vd[4] = { V_DSP[0], V_DSP[1], V_DSP[2], V_DSP[3] };
    CHECK(sw_build(&q, usp, qd, qdevs, 3) == 0 && sw_lint(&q) == 0);          /* 실캡처 (QEMU + Linux 할당) */
    CHECK(sw_build(&sw, V_USP, vd, V_DEVS, 4) == 0 && sw_lint(&sw) == 0);
    pcie_switch m = sw; m.dsp[3].sub = 10;               CHECK(sw_lint(&m) == LINT_BUS_OUTSIDE);
    m = sw; m.dsp[3].sec = 5; m.dsp[3].sub = 5;          CHECK(sw_lint(&m) == LINT_BUS_OVERLAP);
    m = sw; m.dsp[3].mem.base = 0xC0000000;              CHECK(sw_lint(&m) == LINT_WIN_OVERLAP);
    m = sw; m.dsp[0].mem.limit = 0xC10FFFFF;             CHECK(sw_lint(&m) == (LINT_WIN_OUTSIDE | LINT_WIN_OVERLAP));
    m = sw; m.dsp[0].pri = 1;                            CHECK(sw_lint(&m) == LINT_PRI_MISMATCH);
    m = sw; m.dsp[3].pref = m.dsp[1].pref; m.dsp[3].pref.limit = 0x4040000000ull;
    CHECK(sw_lint(&m) == (LINT_WIN_OUTSIDE | LINT_WIN_OVERLAP));

    /* 연습 3: 실캡처 5개 브리지로 경로 찾기 */
    bridge_node nd[5] = { { "00:02.0", {0} }, { "01:00.0", {0} }, { "02:00.0", {0} }, { "02:01.0", {0} }, { "02:02.0", {0} } };
    const uint8_t *raw[5] = { rp, usp, dsp[0], dsp[1], dsp[2] };
    int okd = 0;
    for (int i = 0; i < 5; i++) okd += t1_decode(raw[i], 64, &nd[i].h) == T1_OK;
    CHECK(okd == 5);
    int p[8], n;
    n = path_to_bus(nd, 5, 3, p, 8);    CHECK(n == 3 && p[0] == 0 && p[1] == 1 && p[2] == 2);
    n = path_to_bus(nd, 5, 5, p, 8);    CHECK(n == 3 && p[2] == 4);
    n = path_to_bus(nd, 5, 2, p, 8);    CHECK(n == 2 && p[1] == 1);
    CHECK(path_to_bus(nd, 5, 0, p, 8) == 0 && path_to_bus(nd, 5, 6, p, 8) == PATH_ERR_NOROUTE);
    uint8_t fb = 0xEE;
    n = path_to_addr(nd, 5, 0x10000000, p, 8, &fb);      CHECK(n == 3 && fb == 3 && p[2] == 2);
    n = path_to_addr(nd, 5, 0x8000400000ull, p, 8, &fb); CHECK(n == 3 && fb == 5);
    n = path_to_addr(nd, 5, 0x10600000, p, 8, &fb);      CHECK(n == 2 && fb == 2);     /* 스위치 내부 버스의 구멍 */
    n = path_to_addr(nd, 5, 0x10781000, p, 8, &fb);      CHECK(n == 0 && fb == 0);     /* 루트 포트 자신의 BAR (bus 0) */
    printf("path to bus 05:");
    n = path_to_bus(nd, 5, 5, p, 8);
    for (int i = 0; i < n; i++) printf(" %s ->", nd[p[i]].bdf);
    printf(" bus 05\n");
    bridge_node loop[2] = { { "00:01.0", {0} }, { "01:00.0", {0} } };
    loop[0].h.pri = 0; loop[0].h.sec = 1; loop[0].h.sub = 9;
    loop[1].h.pri = 1; loop[1].h.sec = 1; loop[1].h.sub = 9;              /* sec == pri: 버스 번호가 안 늘어남 */
    CHECK(path_to_bus(loop, 2, 5, p, 8) == PATH_ERR_LOOP);

    /* 연습 4: IO + VGA */
    pcie_switch v = sw;
    CHECK(R(route_down_generic(&v, true, 0x1000), RT_DSP, 0) && R(route_down_generic(&v, true, 0x2FFF), RT_DSP, 1));
    CHECK(R(route_down_generic(&v, true, 0x3000), RT_DSP, 3) && R(route_down_generic(&v, true, 0x4000), RT_NOT_MINE, -1));
    CHECK(R(route_down_generic(&v, true, 0x3C0), RT_NOT_MINE, -1) && R(route_down_generic(&v, false, 0xA0000), RT_NOT_MINE, -1));
    v.usp.bridge_ctl |= PCI_BRIDGE_CTL_VGA;
    CHECK(R(route_down_generic(&v, true, 0x3C0), RT_UR, -1));                 /* USP 는 받지만 아래 VGA 없음 */
    v.dsp[1].bridge_ctl |= PCI_BRIDGE_CTL_VGA;
    CHECK(R(route_down_generic(&v, true, 0x3C0), RT_DSP, 1) && R(route_down_generic(&v, true, 0x3DF), RT_DSP, 1));
    CHECK(R(route_down_generic(&v, true, 0x3BC), RT_NOT_MINE, -1) && R(route_down_generic(&v, true, 0x3E0), RT_NOT_MINE, -1));
    CHECK(R(route_down_generic(&v, false, 0xA0000), RT_DSP, 1) && R(route_down_generic(&v, false, 0xBFFFF), RT_DSP, 1));
    CHECK(R(route_down_generic(&v, false, 0xC0000), RT_NOT_MINE, -1) && R(route_down_generic(&v, false, 0xC0100000), RT_DSP, 1));
    v.dsp[1].command &= (uint16_t)~PCI_COMMAND_IO;
    CHECK(R(route_down_generic(&v, true, 0x3C0), RT_UR, -1) && R(route_down_generic(&v, true, 0x2000), RT_UR, -1));

    /* 본문 함수 재확인 (자기완결 파일이라 한 번씩 부른다) */
    CHECK(t1_decode(nv, 64, &h) == T1_ERR_NOT_TYPE1);
    CHECK(R(route_cfg(&q, 3, 0), RT_DSP, 0) && R(route_mem_up(&q, 0, 0x40000000), RT_UPSTREAM, -1));
    CHECK(diag_ep_bar(&q, 0, 0x10000000, 0x4000) == DIAG_OK && diag_str(DIAG_OK)[0] == 'O');
    t1_print("02:00.0 DSP", &q.dsp[0]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
