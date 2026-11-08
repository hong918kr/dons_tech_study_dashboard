/* SIV-M04 PCIe 링크 검증 플로우
 *
 * "링크 올라왔어요" 를 숫자로 확인하는 흐름 세 단계:
 *   Part A  lspci -xxx 텍스트 덤프 → config space 바이트 → capability 체인(0x34 부터) → PCIe cap(ID 0x10)
 *           → Link Capabilities / Link Status 해석 → 기대 Gen/xN 과 비교 (다운그레이드 판정)
 *   Part B  체인 파싱 방어(루프 · 범위 · 잘린 덤프 · 장치 없음) +
 *           mock LTSSM 디버그 레지스터를 가짜 시계로 샘플링하는 링크업 모니터 (재시작 · 속도 미달 · 샘플링 사각지대)
 *   Part C  루트 포트 mock 에서 Retrain Link 를 N 회 반복하는 스트레스 + 다운그레이드/고착/LBMS 누락 검출 + 리포트
 *
 * 데이터 파일(dump_*.txt)은 레슨 디렉터리에서 실행한다고 가정한다 (cwd = lessons/SIV/SIV-M04,
 * 저장소 루트에서 실행해도 찾도록 경로 하나를 더 시도한다).
 * 결정성: 실제 시간 대신 가짜 시계(g_sim.now) — 레지스터 읽기 1회 = 1us, delay_us 는 시계만 전진 (SIV-B09).
 * LTSSM 상태 인코딩은 DesignWare 계열 디버그 레지스터(Linux enum dw_pcie_ltssm)를 본떴다 — 표준 config space 에는
 * LTSSM 상태 레지스터가 없다. 실제 칩은 IP 데이터북 값을 쓴다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 공통: config space 와 표준 오프셋 ======================= */
#define CFG_MAX 4096u
typedef struct { uint8_t b[CFG_MAX]; size_t len; } cfg_t;   /* len = 덤프에 실제로 있던 바이트 수 */

static uint16_t cfg16(const cfg_t *c, size_t off) { return (uint16_t)(c->b[off] | (c->b[off + 1] << 8)); }
static uint32_t cfg32(const cfg_t *c, size_t off) {
    return (uint32_t)c->b[off] | (uint32_t)c->b[off + 1] << 8 | (uint32_t)c->b[off + 2] << 16 | (uint32_t)c->b[off + 3] << 24;
}

#define PCI_VENDOR_ID       0x00
#define PCI_STATUS          0x06
#define PCI_STATUS_CAPLIST  (1u << 4)      /* Status[4] Capabilities List */
#define PCI_CAP_PTR         0x34           /* 헤더 type 0/1 공통 */
#define CAP_ID_PM    0x01
#define CAP_ID_MSI   0x05
#define CAP_ID_PCIE  0x10
#define CAP_ID_MSIX  0x11
/* PCI Express Capability 안의 오프셋 (cap 시작 기준) */
#define PCIE_CAPREG   0x02   /* [3:0] cap version, [7:4] device/port type */
#define PCIE_LNKCAP   0x0C   /* [3:0] Max Link Speed, [9:4] Max Link Width, [20] DLLLA 보고 가능, [21] BW 알림 가능 */
#define PCIE_LNKCTL   0x10
#define PCIE_LNKSTA   0x12
#define PCIE_LNKCTL2  0x30   /* [3:0] Target Link Speed */
#define LNKCTL_RL     0x0020u   /* Retrain Link — 루트/다운스트림 포트에서만, 읽으면 항상 0 */
#define LNKSTA_SPEED  0x000Fu   /* Current Link Speed */
#define LNKSTA_WIDTH  0x03F0u   /* Negotiated Link Width */
#define LNKSTA_LT     0x0800u   /* Link Training */
#define LNKSTA_SCC    0x1000u   /* Slot Clock Configuration */
#define LNKSTA_DLLLA  0x2000u   /* Data Link Layer Link Active */
#define LNKSTA_LBMS   0x4000u   /* Link Bandwidth Management Status (RW1C) */
#define LNKSTA_LABS   0x8000u   /* Link Autonomous Bandwidth Status (RW1C) */

static bool width_valid(unsigned w) { return w == 1 || w == 2 || w == 4 || w == 8 || w == 12 || w == 16 || w == 32; }
static const char *const SPEED_GT[] = { "?", "2.5", "5.0", "8.0", "16.0", "32.0", "64.0" };
static const char *speed_gt(unsigned s) { return s >= 1 && s <= 6 ? SPEED_GT[s] : "?"; }

/* ======================= Part A: 덤프 파싱 · cap 체인 · 링크 해석 ======================= */
static int hexval(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* A1. lspci -x / -xxx / -xxxx 텍스트 → cfg.
 * 데이터 줄 = "<2~3자리 hex offset>: " + 16개 " hh". 그 밖의 줄(장치 이름 줄 "01:00.0 Non-Volatile..." , 빈 줄)은 건너뛴다.
 * 반환: 읽은 바이트 수 (64 / 256 / 4096), 오류(offset 불연속, 16바이트 미만 줄, 잘못된 hex, 4096 초과)는 -1. */
static int parse_lspci_dump(const char *text, cfg_t *c) {
    memset(c, 0, sizeof *c);
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        size_t i = 0; unsigned off = 0; int nd = 0;
        while (i < n && nd < 4 && hexval(p[i]) >= 0) { off = off * 16u + (unsigned)hexval(p[i]); i++; nd++; }
        if (nd >= 2 && nd <= 3 && i + 1 < n && p[i] == ':' && p[i + 1] == ' ') {     /* 데이터 줄 */
            if (off != c->len || c->len + 16 > CFG_MAX) return -1;
            i++;                                                                      /* ':' 다음 */
            for (int k = 0; k < 16; k++) {
                if (i + 3 > n || p[i] != ' ') return -1;
                int hi = hexval(p[i + 1]), lo = hexval(p[i + 2]);
                if (hi < 0 || lo < 0) return -1;
                c->b[c->len + (size_t)k] = (uint8_t)(hi << 4 | lo);
                i += 3;
            }
            for (; i < n; i++) if (p[i] != ' ' && p[i] != '\r') return -1;          /* 17번째 바이트 같은 쓰레기 */
            c->len += 16;
        }
        p += n;
        if (*p == '\n') p++;
    }
    return (int)c->len;
}

typedef enum { CW_OK, CW_NOT_PRESENT, CW_NO_CAPLIST, CW_BAD_PTR, CW_LOOP, CW_TRUNCATED } cw_status_t;
static const char *const CW_NAME[] = { "OK", "NOT_PRESENT", "NO_CAPLIST", "BAD_PTR", "LOOP", "TRUNCATED" };
typedef struct { uint8_t off, id; } cap_ent_t;
typedef struct {
    cw_status_t status;
    int n; cap_ent_t cap[48];      /* 0x40..0xFC 의 dword 는 48개 → 서로 다른 cap 은 최대 48개 */
    uint8_t bad;                   /* 문제가 된 포인터 값 (BAD_PTR / LOOP / TRUNCATED) */
    uint8_t from;                  /* 그 포인터를 담고 있던 곳 (0x34 = 헤더, 아니면 cap 시작 오프셋) */
} cap_walk_t;

/* A2. 0x34 에서 시작해 [id, next] 를 따라간다. 반드시 끝나고(루프 방지), 왜 멈췄는지 남긴다.
 * 순서: len < 0x40 → TRUNCATED, Vendor ID 0xFFFF → NOT_PRESENT, Status[4]==0 → NO_CAPLIST,
 *       각 포인터: 하위 2비트 마스크, 0 → 끝(OK), < 0x40 → BAD_PTR, ≥ len → TRUNCATED, 이미 방문 → LOOP. */
static cw_status_t cap_walk(const cfg_t *c, cap_walk_t *w) {
    memset(w, 0, sizeof *w);
    if (c->len < 0x40) { w->bad = 0; w->from = 0; return w->status = CW_TRUNCATED; }
    if (cfg16(c, PCI_VENDOR_ID) == 0xFFFF) return w->status = CW_NOT_PRESENT;
    if (!(cfg16(c, PCI_STATUS) & PCI_STATUS_CAPLIST)) return w->status = CW_NO_CAPLIST;
    uint64_t seen = 0;                                     /* dword 단위 방문 비트맵 (256B / 4 = 64) */
    uint8_t from = PCI_CAP_PTR;
    uint8_t ptr = c->b[PCI_CAP_PTR] & 0xFCu;               /* 하위 2비트는 reserved → 마스크 */
    for (;;) {
        if (ptr == 0) return w->status = CW_OK;
        w->bad = ptr; w->from = from;
        if (ptr < 0x40) return w->status = CW_BAD_PTR;     /* 표준 헤더 안을 가리킴 */
        if ((size_t)ptr + 2 > c->len) return w->status = CW_TRUNCATED;
        if (seen & (1ull << (ptr >> 2))) return w->status = CW_LOOP;
        seen |= 1ull << (ptr >> 2);
        w->cap[w->n].off = ptr; w->cap[w->n].id = c->b[ptr]; w->n++;
        from = ptr;
        ptr = c->b[ptr + 1] & 0xFCu;
    }
}
static uint8_t cap_find(const cap_walk_t *w, uint8_t id) {
    for (int i = 0; i < w->n; i++) if (w->cap[i].id == id) return w->cap[i].off;
    return 0;
}

typedef struct {
    uint8_t  port_type;              /* 0 = Endpoint, 4 = Root Port, 5/6 = Switch Up/Down ... */
    uint8_t  max_speed, max_width;   /* Link Capabilities */
    uint8_t  cur_speed, cur_width;   /* Link Status */
    uint8_t  target_speed;           /* Link Control 2 (cap version ≥ 2), 아니면 0 */
    bool     training, dll_active, lbms, labs;
    uint32_t lnkcap; uint16_t lnksta;
} link_info_t;

/* A3. PCIe cap(off) 에서 링크 필드를 해석. cap 이 덤프 범위를 넘으면 false. */
static bool decode_link(const cfg_t *c, uint8_t off, link_info_t *li) {
    memset(li, 0, sizeof *li);
    if (off < 0x40 || (size_t)off + PCIE_LNKSTA + 2 > c->len) return false;
    uint16_t capreg = cfg16(c, off + PCIE_CAPREG);
    li->port_type  = (uint8_t)((capreg >> 4) & 0xF);
    li->lnkcap     = cfg32(c, off + PCIE_LNKCAP);
    li->lnksta     = cfg16(c, off + PCIE_LNKSTA);
    li->max_speed  = (uint8_t)(li->lnkcap & 0xF);
    li->max_width  = (uint8_t)((li->lnkcap >> 4) & 0x3F);
    li->cur_speed  = (uint8_t)(li->lnksta & LNKSTA_SPEED);
    li->cur_width  = (uint8_t)((li->lnksta & LNKSTA_WIDTH) >> 4);
    li->training   = (li->lnksta & LNKSTA_LT) != 0;
    li->dll_active = (li->lnksta & LNKSTA_DLLLA) != 0;
    li->lbms       = (li->lnksta & LNKSTA_LBMS) != 0;
    li->labs       = (li->lnksta & LNKSTA_LABS) != 0;
    if ((capreg & 0xF) >= 2 && (size_t)off + PCIE_LNKCTL2 + 2 <= c->len)
        li->target_speed = (uint8_t)(cfg16(c, off + PCIE_LNKCTL2) & 0xF);
    return true;
}

typedef struct { uint8_t gen, width; } link_expect_t;
enum { LV_OK = 0, LV_SPEED_DOWN = 1, LV_WIDTH_DOWN = 2, LV_CAP_BELOW = 4, LV_LINK_DOWN = 8,
       LV_INCONSISTENT = 16, LV_BAD_ENC = 32 };
static const char *const LV_NAME[] = { "SPEED_DOWN", "WIDTH_DOWN", "CAP_BELOW", "LINK_DOWN", "INCONSISTENT", "BAD_ENC" };

/* A4. 판정 (비트 OR).
 *  cur_width == 0              → LINK_DOWN 만 (나머지 필드는 의미 없음)
 *  속도 1..6 / 폭 {1,2,4,8,12,16,32} 밖 (cur 또는 max) → BAD_ENC 만
 *  max < 기대                  → CAP_BELOW   (링크가 아니라 부품/strap/펌웨어 문제)
 *  cur > max                   → INCONSISTENT (레지스터·모델 버그)
 *  cur < min(기대, max)        → SPEED_DOWN / WIDTH_DOWN (달성 가능한 값보다 낮게 붙음 = 링크 문제) */
static unsigned link_verdict(const link_info_t *li, link_expect_t e) {
    if (li->cur_width == 0) return LV_LINK_DOWN;
    if (li->cur_speed < 1 || li->cur_speed > 6 || li->max_speed < 1 || li->max_speed > 6 ||
        !width_valid(li->cur_width) || !width_valid(li->max_width)) return LV_BAD_ENC;
    unsigned v = LV_OK;
    if (li->max_speed < e.gen || li->max_width < e.width) v |= LV_CAP_BELOW;
    if (li->cur_speed > li->max_speed || li->cur_width > li->max_width) v |= LV_INCONSISTENT;
    unsigned want_s = e.gen < li->max_speed ? e.gen : li->max_speed;
    unsigned want_w = e.width < li->max_width ? e.width : li->max_width;
    if (li->cur_speed < want_s) v |= LV_SPEED_DOWN;
    if (li->cur_width < want_w) v |= LV_WIDTH_DOWN;
    return v;
}

static int link_report(char *buf, size_t n, const char *bdf, const link_info_t *li, link_expect_t e, unsigned v) {
    char flags[96] = "";
    for (int i = 0; i < 6; i++)
        if (v & (1u << i)) { if (flags[0]) strcat(flags, "|"); strcat(flags, LV_NAME[i]); }
    return snprintf(buf, n, "%s %s exp=Gen%u x%u cap=Gen%u(%sGT/s) x%u cur=Gen%u(%sGT/s) x%u [%s] lnkcap=0x%08X lnksta=0x%04X",
                    v ? "FAIL" : "PASS", bdf, e.gen, e.width, li->max_speed, speed_gt(li->max_speed), li->max_width,
                    li->cur_speed, speed_gt(li->cur_speed), li->cur_width, v ? flags : "OK",
                    (unsigned)li->lnkcap, (unsigned)li->lnksta);
}

/* ======================= Part B/C 공통: LTSSM 인코딩, 포트 버스 ops ======================= */
enum { MAJ_DETECT, MAJ_POLLING, MAJ_CONFIG, MAJ_L0, MAJ_RECOVERY, MAJ_L0S, MAJ_LOWPWR, MAJ_DISABLED,
       MAJ_LOOPBACK, MAJ_HOTRESET, MAJ_UNKNOWN };
static const char *const MAJ_NAME[] = { "DETECT", "POLLING", "CONFIG", "L0", "RECOVERY", "L0S", "LOWPWR", "DISABLED",
                                        "LOOPBACK", "HOTRESET", "UNKNOWN" };
enum { DQ = 0x00, DA = 0x01, PA = 0x02, PCOMP = 0x03, PCFG = 0x04, CLS = 0x07, CLA = 0x08, CNW = 0x09, CNA = 0x0A,
       CC = 0x0B, CI = 0x0C, RL = 0x0D, RS = 0x0E, RC = 0x0F, RI = 0x10, L0 = 0x11, EQ0 = 0x20, EQ1 = 0x21,
       EQ2 = 0x22, EQ3 = 0x23 };
static const struct { const char *name; uint8_t major; } LTSSM[0x24] = {
    [0x00] = { "DETECT_QUIET", MAJ_DETECT },      [0x01] = { "DETECT_ACT", MAJ_DETECT },
    [0x02] = { "POLL_ACTIVE", MAJ_POLLING },      [0x03] = { "POLL_COMPLIANCE", MAJ_POLLING },
    [0x04] = { "POLL_CONFIG", MAJ_POLLING },      [0x05] = { "PRE_DETECT_QUIET", MAJ_DETECT },
    [0x06] = { "DETECT_WAIT", MAJ_DETECT },       [0x07] = { "CFG_LINKWD_START", MAJ_CONFIG },
    [0x08] = { "CFG_LINKWD_ACEPT", MAJ_CONFIG },  [0x09] = { "CFG_LANENUM_WAI", MAJ_CONFIG },
    [0x0A] = { "CFG_LANENUM_ACEPT", MAJ_CONFIG }, [0x0B] = { "CFG_COMPLETE", MAJ_CONFIG },
    [0x0C] = { "CFG_IDLE", MAJ_CONFIG },          [0x0D] = { "RCVRY_LOCK", MAJ_RECOVERY },
    [0x0E] = { "RCVRY_SPEED", MAJ_RECOVERY },     [0x0F] = { "RCVRY_RCVRCFG", MAJ_RECOVERY },
    [0x10] = { "RCVRY_IDLE", MAJ_RECOVERY },      [0x11] = { "L0", MAJ_L0 },
    [0x12] = { "L0S", MAJ_L0S },                  [0x13] = { "L123_SEND_EIDLE", MAJ_LOWPWR },
    [0x14] = { "L1_IDLE", MAJ_LOWPWR },           [0x15] = { "L2_IDLE", MAJ_LOWPWR },
    [0x16] = { "L2_WAKE", MAJ_LOWPWR },           [0x17] = { "DISABLED_ENTRY", MAJ_DISABLED },
    [0x18] = { "DISABLED_IDLE", MAJ_DISABLED },   [0x19] = { "DISABLED", MAJ_DISABLED },
    [0x1A] = { "LPBK_ENTRY", MAJ_LOOPBACK },      [0x1B] = { "LPBK_ACTIVE", MAJ_LOOPBACK },
    [0x1C] = { "LPBK_EXIT", MAJ_LOOPBACK },       [0x1D] = { "LPBK_EXIT_TIMEOUT", MAJ_LOOPBACK },
    [0x1E] = { "HOT_RESET_ENTRY", MAJ_HOTRESET }, [0x1F] = { "HOT_RESET", MAJ_HOTRESET },
    [0x20] = { "RCVRY_EQ0", MAJ_RECOVERY },       [0x21] = { "RCVRY_EQ1", MAJ_RECOVERY },
    [0x22] = { "RCVRY_EQ2", MAJ_RECOVERY },       [0x23] = { "RCVRY_EQ3", MAJ_RECOVERY },
};
static uint8_t ltssm_major(uint32_t raw) {
    return (raw & ~0x3Fu) || raw >= 0x24 ? MAJ_UNKNOWN : LTSSM[raw].major;   /* [31:6] reserved = 0 */
}
static const char *ltssm_name(uint32_t raw) { return ltssm_major(raw) == MAJ_UNKNOWN ? "?" : LTSSM[raw].name; }

#define V_LTSSM      0x104u   /* 벤더 디버그: [5:0] LTSSM 상태 */
#define V_RCVRY_CNT  0x108u   /* 벤더 디버그: Recovery 진입 횟수 (free-running, wrap) */

typedef struct {
    uint16_t (*cfg_read16)(void *ctx, uint32_t off);
    void     (*cfg_write16)(void *ctx, uint32_t off, uint16_t v);
    uint32_t (*vread32)(void *ctx, uint32_t off);
    uint32_t (*now_us)(void *ctx);
    void     (*delay_us)(void *ctx, uint32_t us);
    void     *ctx;
} port_ops_t;

/* ======================= Part B: 링크업 모니터 ======================= */
typedef enum { LM_OK, LM_TIMEOUT, LM_BAD_STATE, LM_LINK_DOWN, LM_RETRY, LM_UNSTABLE } lm_status_t;
static const char *const LM_NAME[] = { "OK", "TIMEOUT", "BAD_STATE", "LINK_DOWN", "RETRY", "UNSTABLE" };
typedef struct {
    uint32_t timeout_us;     /* 링크업(목표 속도의 L0)까지 */
    uint32_t interval_us;    /* 샘플 간격 */
    uint32_t observe_us;     /* 링크업 후 안정성 관찰 창 */
    uint8_t  target_speed;   /* 이 속도 이상의 L0 를 봐야 "업" */
    uint32_t max_restarts;   /* 링크업 중 Detect 로 되돌아간 횟수 허용치 */
} lm_spec_t;
typedef struct { uint8_t code; uint32_t t_first, t_last, samples; } trace_ent_t;
#define TR_MAX 48
typedef struct {
    lm_status_t status;
    uint32_t samples, elapsed_us, last_raw;
    bool     l0_seen, up;
    uint32_t t_l0_first, t_up;
    uint8_t  speed, width;                     /* 마지막으로 L0 에서 읽은 Link Status */
    uint32_t restarts;                         /* 링크업 중 X → DETECT */
    uint32_t speed_changes;                    /* 링크업 중 L0 → RECOVERY (속도 변경/EQ) */
    uint32_t rcvry_seen;                       /* 관찰 창: 샘플로 본 RECOVERY 진입 */
    uint32_t rcvry_cnt;                        /* 관찰 창: HW 카운터 증가분 */
    trace_ent_t tr[TR_MAX]; int ntr; bool tr_overflow;
} lm_result_t;

static void trace_add(lm_result_t *r, uint8_t code, uint32_t t) {
    if (r->ntr > 0 && r->tr[r->ntr - 1].code == code) { r->tr[r->ntr - 1].t_last = t; r->tr[r->ntr - 1].samples++; return; }
    if (r->ntr == TR_MAX) { r->tr_overflow = true; return; }
    r->tr[r->ntr++] = (trace_ent_t){ code, t, t, 1 };
}

/* B1. LTSSM 을 interval 마다 샘플링.
 * 링크업 단계: 읽은 값이 reserved 비트 1 / 알 수 없는 코드 → 즉시 BAD_STATE. trace_add.
 *   DETECT 진입(직전 major 가 DETECT 아님, 첫 샘플 제외) → restarts++, L0 → RECOVERY → speed_changes++.
 *   L0 이면 Link Status 를 읽어 speed/width 기록, speed ≥ target 이면 up (t_up, RCVRY_CNT 기준값).
 *   업 전에 elapsed ≥ timeout → TIMEOUT.
 * 관찰 단계: DETECT → LINK_DOWN, RECOVERY 진입 → rcvry_seen++. observe_us 가 지나면 rcvry_cnt = 카운터 증가분,
 *   restarts > max → RETRY, rcvry_cnt 나 rcvry_seen > 0 → UNSTABLE, 아니면 OK. */
static lm_status_t ltssm_monitor(const port_ops_t *o, uint8_t cap, const lm_spec_t *s, lm_result_t *r) {
    memset(r, 0, sizeof *r);
    uint32_t start = o->now_us(o->ctx), c0 = 0;
    uint8_t prev = MAJ_UNKNOWN;
    for (;;) {
        uint32_t v = o->vread32(o->ctx, V_LTSSM);
        uint32_t t = o->now_us(o->ctx) - start;
        r->samples++; r->last_raw = v; r->elapsed_us = t;
        uint8_t maj = ltssm_major(v);
        if (maj == MAJ_UNKNOWN) return r->status = LM_BAD_STATE;
        trace_add(r, (uint8_t)v, t);
        if (!r->up) {
            if (maj == MAJ_DETECT && prev != MAJ_DETECT && prev != MAJ_UNKNOWN) r->restarts++;
            if (maj == MAJ_RECOVERY && prev == MAJ_L0) r->speed_changes++;
            if (maj == MAJ_L0) {
                uint16_t sta = o->cfg_read16(o->ctx, cap + PCIE_LNKSTA);
                r->speed = (uint8_t)(sta & LNKSTA_SPEED); r->width = (uint8_t)((sta & LNKSTA_WIDTH) >> 4);
                if (!r->l0_seen) { r->l0_seen = true; r->t_l0_first = t; }
                if (r->speed >= s->target_speed) {
                    r->up = true; r->t_up = t; c0 = o->vread32(o->ctx, V_RCVRY_CNT);
                }
            }
            if (!r->up && t >= s->timeout_us) return r->status = LM_TIMEOUT;
        } else {
            if (maj == MAJ_DETECT) return r->status = LM_LINK_DOWN;
            if (maj == MAJ_RECOVERY && prev != MAJ_RECOVERY) r->rcvry_seen++;
            if (t - r->t_up >= s->observe_us) {
                r->rcvry_cnt = o->vread32(o->ctx, V_RCVRY_CNT) - c0;          /* unsigned 뺄셈 = wrap 안전 */
                if (r->restarts > s->max_restarts) return r->status = LM_RETRY;
                if (r->rcvry_cnt || r->rcvry_seen) return r->status = LM_UNSTABLE;
                return r->status = LM_OK;
            }
        }
        prev = maj;
        o->delay_us(o->ctx, s->interval_us);
    }
}

/* trace 를 major 상태 열로 접기: "DETECT>POLLING>CONFIG>L0>RECOVERY>L0" */
static void trace_majors(const lm_result_t *r, char *buf, size_t n) {
    buf[0] = '\0';
    uint8_t prev = MAJ_UNKNOWN;
    for (int i = 0; i < r->ntr; i++) {
        uint8_t m = ltssm_major(r->tr[i].code);
        if (m == prev) continue;
        size_t len = strlen(buf);
        snprintf(buf + len, n - len, "%s%s", len ? ">" : "", MAJ_NAME[m]);
        prev = m;
    }
}
static int lm_report(char *buf, size_t n, const lm_result_t *r, const lm_spec_t *s) {
    const trace_ent_t *last = r->ntr ? &r->tr[r->ntr - 1] : NULL;
    return snprintf(buf, n, "%s ltssm=%s(0x%02X) dwell=%uus l0=Gen%u x%u target=Gen%u up_at=%uus restarts=%u "
                    "speed_changes=%u rcvry_seen=%u rcvry_cnt=%u samples=%u elapsed=%uus/%uus",
                    LM_NAME[r->status], ltssm_name(r->last_raw), (unsigned)r->last_raw,
                    last ? (unsigned)(last->t_last - last->t_first) : 0u, r->speed, r->width, s->target_speed,
                    (unsigned)r->t_up, (unsigned)r->restarts, (unsigned)r->speed_changes, (unsigned)r->rcvry_seen,
                    (unsigned)r->rcvry_cnt, (unsigned)r->samples, (unsigned)r->elapsed_us, (unsigned)s->timeout_us);
}

/* ======================= Part C: Retrain 스트레스 ======================= */
typedef enum { RT_OK, RT_PRECOND_TIMEOUT, RT_TIMEOUT, RT_DLL_DOWN, RT_SPEED_DOWN, RT_WIDTH_DOWN, RT_NO_LBMS, RT_NSIG } rt_sig_t;
static const char *const RT_NAME[] = { "OK", "PRECOND_TIMEOUT", "TIMEOUT", "DLL_DOWN", "SPEED_DOWN", "WIDTH_DOWN", "NO_LBMS" };
typedef struct { rt_sig_t sig; uint16_t lnksta; uint8_t speed, width, ltssm; uint32_t rcvry_delta, elapsed_us; } rt_result_t;
#define RETRAIN_TIMEOUT_US  1000000u   /* Linux PCIE_LINK_RETRAIN_TIMEOUT_MS 와 같은 1 s */
#define LT_POLL_US          10u

/* Link Status 의 (sta & mask) == val 을 기다린다 (SIV-B09 축약: 마감 후 한 번 더 읽기 포함) */
static bool wait_lnksta(const port_ops_t *o, uint8_t cap, uint16_t mask, uint16_t val, uint32_t timeout_us, uint16_t *last) {
    uint32_t start = o->now_us(o->ctx);
    bool last_look = false;
    for (;;) {
        *last = o->cfg_read16(o->ctx, cap + PCIE_LNKSTA);
        if ((*last & mask) == val) return true;
        if (last_look) return false;
        if (o->now_us(o->ctx) - start >= timeout_us) { last_look = true; continue; }
        o->delay_us(o->ctx, LT_POLL_US);
    }
}

/* C1. retrain 1회.
 * 1) LT==0 대기 (이전 training 진행 중이면 RL 을 쓰지 않는다)   실패 → PRECOND_TIMEOUT
 * 2) LBMS|LABS W1C 로 지우기, RCVRY_CNT 기준값
 * 3) Link Control RMW 로 RL 세트
 * 4) LT==0 대기 (timeout)                                      실패 → TIMEOUT (ltssm 기록)
 * 5) 판정 우선순위: DLLLA==0 → DLL_DOWN, speed < 기대 → SPEED_DOWN, width < 기대 → WIDTH_DOWN,
 *    LBMS==0 → NO_LBMS (retrain 이 끝났는데 스펙상 서야 할 비트가 안 섬), 아니면 OK.
 * r 에는 언제나 lnksta / speed / width / ltssm / rcvry_delta / elapsed 가 채워진다. */
static rt_sig_t retrain_once(const port_ops_t *o, uint8_t cap, link_expect_t e, uint32_t timeout_us, rt_result_t *r) {
    memset(r, 0, sizeof *r);
    uint32_t start = o->now_us(o->ctx), c0 = 0;
    bool ok = wait_lnksta(o, cap, LNKSTA_LT, 0, timeout_us, &r->lnksta);
    if (ok) {
        o->cfg_write16(o->ctx, cap + PCIE_LNKSTA, LNKSTA_LBMS | LNKSTA_LABS);
        c0 = o->vread32(o->ctx, V_RCVRY_CNT);
        uint16_t ctl = o->cfg_read16(o->ctx, cap + PCIE_LNKCTL);
        o->cfg_write16(o->ctx, cap + PCIE_LNKCTL, (uint16_t)(ctl | LNKCTL_RL));
        ok = wait_lnksta(o, cap, LNKSTA_LT, 0, timeout_us, &r->lnksta);
        r->rcvry_delta = o->vread32(o->ctx, V_RCVRY_CNT) - c0;
        if (!ok) r->sig = RT_TIMEOUT;
    } else {
        r->sig = RT_PRECOND_TIMEOUT;
    }
    r->ltssm = (uint8_t)(o->vread32(o->ctx, V_LTSSM) & 0x3F);
    r->speed = (uint8_t)(r->lnksta & LNKSTA_SPEED);
    r->width = (uint8_t)((r->lnksta & LNKSTA_WIDTH) >> 4);
    r->elapsed_us = o->now_us(o->ctx) - start;
    if (!ok) return r->sig;
    if (!(r->lnksta & LNKSTA_DLLLA)) return r->sig = RT_DLL_DOWN;
    if (r->speed < e.gen)            return r->sig = RT_SPEED_DOWN;
    if (r->width < e.width)          return r->sig = RT_WIDTH_DOWN;
    if (!(r->lnksta & LNKSTA_LBMS))  return r->sig = RT_NO_LBMS;
    return r->sig = RT_OK;
}

/* (비교용) 흔한 잘못된 구현: RL 을 쓰고 곧바로 Link Status 를 읽어 판정 — training 이 끝나기 전 값을 본다 */
static rt_sig_t retrain_naive(const port_ops_t *o, uint8_t cap, link_expect_t e) {
    uint16_t ctl = o->cfg_read16(o->ctx, cap + PCIE_LNKCTL);
    o->cfg_write16(o->ctx, cap + PCIE_LNKCTL, (uint16_t)(ctl | LNKCTL_RL));
    uint16_t sta = o->cfg_read16(o->ctx, cap + PCIE_LNKSTA);
    if ((sta & LNKSTA_SPEED) < e.gen) return RT_SPEED_DOWN;
    if (((sta & LNKSTA_WIDTH) >> 4) < e.width) return RT_WIDTH_DOWN;
    return RT_OK;
}

typedef struct { int count, first, last; rt_result_t ex; } sig_stat_t;
#define FAIL_MAX 256
typedef struct {
    int iters_req, iters_run, pass;
    bool aborted; int abort_iter;
    sig_stat_t sig[RT_NSIG];
    int nfail; int fail_iter[FAIL_MAX]; rt_sig_t fail_sig[FAIL_MAX];
} stress_t;

/* C2. retrain_once 를 n 회. 시그니처별 count/first/last/첫 예시(ex) 를 모으고 실패 목록을 남긴다(FAIL_MAX 까지).
 * TIMEOUT / PRECOND_TIMEOUT 은 링크가 갇힌 것 → 더 돌려도 의미 없으니 중단(aborted, abort_iter). */
static void retrain_stress(const port_ops_t *o, uint8_t cap, link_expect_t e, int n, stress_t *st) {
    memset(st, 0, sizeof *st);
    st->iters_req = n;
    for (int i = 1; i <= n; i++) {
        rt_result_t r;
        rt_sig_t sg = retrain_once(o, cap, e, RETRAIN_TIMEOUT_US, &r);
        st->iters_run = i;
        if (sg == RT_OK) { st->pass++; continue; }
        sig_stat_t *ss = &st->sig[sg];
        if (ss->count++ == 0) { ss->first = i; ss->ex = r; }
        ss->last = i;
        if (st->nfail < FAIL_MAX) { st->fail_iter[st->nfail] = i; st->fail_sig[st->nfail] = sg; st->nfail++; }
        if (sg == RT_TIMEOUT || sg == RT_PRECOND_TIMEOUT) { st->aborted = true; st->abort_iter = i; break; }
    }
}

/* first 부터 마지막 iteration 까지 한 번도 빠짐없이 실패 = PERSISTENT (고착), 아니면 INTERMITTENT */
static bool sig_persistent(const stress_t *st, int s) {
    const sig_stat_t *ss = &st->sig[s];
    return ss->count > 0 && ss->last == st->iters_run && ss->count == st->iters_run - ss->first + 1;
}
static void stress_report(FILE *f, const char *port, link_expect_t e, const stress_t *st) {
    fprintf(f, "RETRAIN_STRESS port=%s exp=Gen%u x%u iters=%d/%d pass=%d fail=%d%s\n", port, e.gen, e.width,
            st->iters_run, st->iters_req, st->pass, st->iters_run - st->pass, st->aborted ? " ABORTED" : "");
    for (int s = 1; s < RT_NSIG; s++) {
        const sig_stat_t *ss = &st->sig[s];
        if (!ss->count) continue;
        fprintf(f, "  %-15s count=%-3d first=%-3d last=%-3d %-12s e.g. lnksta=0x%04X Gen%u x%u ltssm=%s rcvry+=%u t=%uus\n",
                RT_NAME[s], ss->count, ss->first, ss->last, sig_persistent(st, s) ? "PERSISTENT" : "INTERMITTENT",
                (unsigned)ss->ex.lnksta, ss->ex.speed, ss->ex.width, ltssm_name(ss->ex.ltssm),
                (unsigned)ss->ex.rcvry_delta, (unsigned)ss->ex.elapsed_us);
    }
}

/* ======================= 테스트 환경: 루트 포트 mock + 가짜 시계 ======================= */
typedef struct { uint32_t dur_us; uint8_t code, speed, width; } seg_t;   /* dur_us 동안 이 상태 (마지막 seg 는 영원히) */
typedef struct {
    uint32_t seed; uint16_t width_drop_permille;   /* retrain 마다 이 확률로 x4 → x2 (간헐 레인 불량) */
    int speed_drop_from;                           /* 이 retrain 번호부터 Gen4 → Gen3 고착 (0 = 없음) */
    int stuck_at;                                  /* 이 retrain 에서 RCVRY_LOCK 에 갇힘 (0 = 없음) */
    bool no_lbms;                                  /* retrain 완료 후 LBMS 를 안 세움 (스펙 위반) */
} rp_fault_t;
#define SEG_MAX 96
typedef struct {
    uint32_t now;
    const seg_t *seg; int nseg; uint32_t t0;       /* 재생 중인 스크립트 */
    seg_t dyn[6];                                  /* retrain 이 만든 스크립트 */
    uint32_t rcvry_base;                           /* 교체된 스크립트에서 센 Recovery 진입 */
    uint8_t cfg[256];
    uint16_t sticky;                               /* LBMS / LABS */
    bool retrain_pending;
    rp_fault_t f; uint32_t xs; int retrains;
    int injected[FAIL_MAX]; int ninjected;         /* 폭 다운을 주입한 retrain 번호 */
} sim_t;
static sim_t g_sim;
#define RP_CAP 0x60u                               /* 루트 포트 PCIe cap 위치 (PM cap 0x40 다음) */

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int seg_at(const sim_t *m, uint32_t *start_out) {
    uint32_t t = m->now - m->t0, acc = 0;
    for (int i = 0; i < m->nseg - 1; i++) {
        if (t < acc + m->seg[i].dur_us) { if (start_out) *start_out = acc; return i; }
        acc += m->seg[i].dur_us;
    }
    if (start_out) *start_out = acc;
    return m->nseg - 1;
}
static uint32_t rcvry_entries(const sim_t *m) {                 /* 현재 스크립트에서 지금까지 Recovery 진입 횟수 */
    int cur = seg_at(m, NULL); uint32_t n = 0;
    for (int i = 0; i <= cur; i++)
        if (ltssm_major(m->seg[i].code) == MAJ_RECOVERY && (i == 0 || ltssm_major(m->seg[i - 1].code) != MAJ_RECOVERY)) n++;
    return n;
}
static uint16_t lnksta_now(sim_t *m) {
    const seg_t *s = &m->seg[seg_at(m, NULL)];
    uint8_t maj = ltssm_major(s->code);
    if (m->retrain_pending && maj == MAJ_L0) { m->retrain_pending = false; if (!m->f.no_lbms) m->sticky |= LNKSTA_LBMS; }
    uint16_t v = (uint16_t)(s->speed | (s->width << 4) | LNKSTA_SCC | m->sticky);
    if (maj == MAJ_CONFIG || maj == MAJ_RECOVERY) v |= LNKSTA_LT;
    if (maj == MAJ_L0 || maj == MAJ_RECOVERY || maj == MAJ_L0S || maj == MAJ_LOWPWR) v |= LNKSTA_DLLLA;
    return v;
}
static void start_retrain(sim_t *m) {
    const seg_t cur = m->seg[seg_at(m, NULL)];
    m->rcvry_base += rcvry_entries(m);
    int k = ++m->retrains;
    uint8_t sp = (m->f.speed_drop_from && k >= m->f.speed_drop_from) ? 3 : 4, w = 4;
    if (m->f.width_drop_permille && xorshift32(&m->xs) % 1000u < m->f.width_drop_permille) {
        w = 2;
        if (m->ninjected < FAIL_MAX) m->injected[m->ninjected++] = k;
    }
    m->dyn[0] = (seg_t){ 20, RL, cur.speed, cur.width };
    if (m->f.stuck_at == k) { m->nseg = 1; }
    else {
        m->dyn[1] = (seg_t){ 10, RC, cur.speed, cur.width };
        m->dyn[2] = (seg_t){ 5, RI, sp, w };
        m->dyn[3] = (seg_t){ 0, L0, sp, w };
        m->nseg = 4;
    }
    m->seg = m->dyn; m->t0 = m->now; m->retrain_pending = true;
}
static uint16_t sim_cfg_read16(void *ctx, uint32_t off) {
    sim_t *m = ctx; m->now += 1;
    off &= 0xFEu;
    if (off == RP_CAP + PCIE_LNKSTA) return lnksta_now(m);
    return (uint16_t)(m->cfg[off] | (m->cfg[off + 1] << 8));
}
static void sim_cfg_write16(void *ctx, uint32_t off, uint16_t v) {
    sim_t *m = ctx; m->now += 1;
    off &= 0xFEu;
    if (off == RP_CAP + PCIE_LNKSTA) { m->sticky &= (uint16_t)~(v & (LNKSTA_LBMS | LNKSTA_LABS)); return; }
    if (off == RP_CAP + PCIE_LNKCTL) {
        uint16_t keep = (uint16_t)(v & ~LNKCTL_RL);                 /* RL 은 저장 안 됨 (읽으면 0) */
        m->cfg[off] = (uint8_t)keep; m->cfg[off + 1] = (uint8_t)(keep >> 8);
        if (v & LNKCTL_RL) start_retrain(m);
    }
}
static uint32_t sim_vread32(void *ctx, uint32_t off) {
    sim_t *m = ctx; m->now += 1;
    if (off == V_LTSSM) return m->seg[seg_at(m, NULL)].code;
    if (off == V_RCVRY_CNT) return m->rcvry_base + rcvry_entries(m);
    return 0;
}
static uint32_t sim_now(void *ctx) { return ((sim_t *)ctx)->now; }
static void sim_delay(void *ctx, uint32_t us) { ((sim_t *)ctx)->now += us; }
static const port_ops_t g_port = { sim_cfg_read16, sim_cfg_write16, sim_vread32, sim_now, sim_delay, &g_sim };

static void sim_power_on(const seg_t *script, int n, const rp_fault_t *f) {
    memset(&g_sim, 0, sizeof g_sim);
    g_sim.now = 0xFFFFF000u;                                        /* 32비트 us 시계가 곧 wrap — 뺄셈 비교 확인 */
    g_sim.seg = script; g_sim.nseg = n; g_sim.t0 = g_sim.now;
    if (f) g_sim.f = *f;
    g_sim.xs = g_sim.f.seed ? g_sim.f.seed : 1u;
    uint8_t *c = g_sim.cfg;                                         /* 루트 포트 config space (type 1 헤더) */
    c[0x00] = 0x34; c[0x01] = 0x12; c[0x02] = 0x10; c[0x03] = 0x7A; /* 1234:7a10 */
    c[0x06] = 0x10;                                                 /* Status: Capabilities List */
    c[0x0A] = 0x04; c[0x0B] = 0x06; c[0x0E] = 0x01;                 /* class 0604 PCI bridge, header type 1 */
    c[0x34] = 0x40;
    c[0x40] = CAP_ID_PM; c[0x41] = RP_CAP; c[0x42] = 0x03;
    c[RP_CAP] = CAP_ID_PCIE; c[RP_CAP + 1] = 0x00; c[RP_CAP + 2] = 0x42; /* version 2, type 4 = Root Port */
    uint32_t lnkcap = 0x01300044u;                                  /* port 1, BW 알림/DLLLA 보고 가능, Gen4 x4 */
    for (int i = 0; i < 4; i++) c[RP_CAP + PCIE_LNKCAP + i] = (uint8_t)(lnkcap >> (8 * i));
    c[RP_CAP + PCIE_LNKCTL] = 0x40;                                 /* Common Clock Configuration */
    c[RP_CAP + PCIE_LNKCTL2] = 0x04;                                /* Target Link Speed = 16 GT/s */
}
/* config space 256B 를 16비트 읽기로 떠서 Part A 파서가 쓰는 cfg_t 로 */
static void snapshot_cfg(const port_ops_t *o, cfg_t *c) {
    memset(c, 0, sizeof *c);
    for (uint32_t off = 0; off < 256; off += 2) {
        uint16_t v = o->cfg_read16(o->ctx, off);
        c->b[off] = (uint8_t)v; c->b[off + 1] = (uint8_t)(v >> 8);
    }
    c->len = 256;
}

/* ---- 스크립트: 전원 인가 후 링크업 (시간은 크기 감각만 흉내낸 mock 값) ---- */
#define GEN1_UP  { 200, DQ, 1, 0 }, { 20, DA, 1, 0 }, { 100, PA, 1, 0 }, { 40, PCFG, 1, 0 }, { 20, CLS, 1, 0 }, \
                 { 10, CLA, 1, 0 }, { 10, CNW, 1, 0 }, { 10, CNA, 1, 0 }, { 10, CC, 1, 4 }, { 10, CI, 1, 4 }, { 50, L0, 1, 4 }
#define TO_GEN(g, from) { 20, RL, from, 4 }, { 30, RS, from, 4 }, { 20, RL, g, 4 }, { 20, EQ0, g, 4 }, { 100, EQ1, g, 4 }, \
                 { 300, EQ2, g, 4 }, { 300, EQ3, g, 4 }, { 20, RL, g, 4 }, { 20, RC, g, 4 }, { 10, RI, g, 4 }
static const seg_t LINKUP_GOOD[] = { GEN1_UP, TO_GEN(3, 1), { 50, L0, 3, 4 }, TO_GEN(4, 3), { 0, L0, 4, 4 } };
static const seg_t LINKUP_EQ_FAIL[] = { GEN1_UP, { 20, RL, 1, 4 }, { 30, RS, 1, 4 }, { 20, RL, 3, 4 }, { 20, EQ0, 3, 4 },
                                        { 3000, EQ1, 3, 4 }, { 30, RS, 3, 4 }, { 20, RL, 1, 4 }, { 20, RC, 1, 4 },
                                        { 10, RI, 1, 4 }, { 0, L0, 1, 4 } };
#define RESTART  { 200, DQ, 1, 0 }, { 20, DA, 1, 0 }, { 500, PA, 1, 0 }
static const seg_t LINKUP_RESTART3[] = { RESTART, RESTART, RESTART, GEN1_UP, TO_GEN(3, 1), { 50, L0, 3, 4 }, TO_GEN(4, 3),
                                         { 0, L0, 4, 4 } };
static const seg_t LINKUP_BADCODE[] = { { 200, DQ, 1, 0 }, { 20, DA, 1, 0 }, { 100, PA, 1, 0 }, { 0, 0x2A, 1, 0 } };
static const seg_t LINKUP_HIDDEN_RCVRY[] = { GEN1_UP, TO_GEN(3, 1), { 50, L0, 3, 4 }, TO_GEN(4, 3), { 500, L0, 4, 4 },
                                             { 3, RL, 4, 4 }, { 2, RC, 4, 4 }, { 1, RI, 4, 4 }, { 0, L0, 4, 4 } };
static const seg_t LINKUP_DROP[] = { GEN1_UP, TO_GEN(3, 1), { 50, L0, 3, 4 }, TO_GEN(4, 3), { 300, L0, 4, 4 },
                                     { 400, RL, 4, 4 }, { 0, DQ, 4, 0 } };
#define NSEG(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char *load_text(const char *name) {
    char path[256];
    FILE *f = fopen(name, "rb");
    if (!f) { snprintf(path, sizeof path, "lessons/SIV/SIV-M04/%s", name); f = fopen(path, "rb"); }
    if (!f) { printf("cannot open %s (run from the lesson directory)\n", name); return NULL; }
    static char buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *s = malloc(n + 1);
    if (s) memcpy(s, buf, n + 1);
    return s;
}
static int load_dump(const char *name, cfg_t *c) {
    char *t = load_text(name);
    if (!t) { memset(c, 0, sizeof *c); return -1; }
    int n = parse_lspci_dump(t, c);
    free(t);
    return n;
}
/* 덤프 하나를 끝까지: 체인 → PCIe cap → 해석 → 판정 */
static unsigned check_dump(const cfg_t *c, link_expect_t e, link_info_t *li, char *line, size_t n) {
    cap_walk_t w;
    memset(li, 0, sizeof *li);
    if (cap_walk(c, &w) != CW_OK) { snprintf(line, n, "FAIL cap_walk=%s", CW_NAME[w.status]); return 0xFFu; }
    uint8_t pc = cap_find(&w, CAP_ID_PCIE);
    if (!pc || !decode_link(c, pc, li)) { snprintf(line, n, "FAIL no PCIe capability"); return 0xFFu; }
    unsigned v = link_verdict(li, e);
    link_report(line, n, "01:00.0", li, e, v);
    return v;
}

int main(void) {
    static cfg_t good, c;
    cap_walk_t w;
    link_info_t li;
    char line[512];
    const link_expect_t GEN4X4 = { 4, 4 };

    /* ===== Part A ===== */
    puts("-- Part A --");
    CHECK(load_dump("dump_gen4x4_good.txt", &good) == 256);
    CHECK(good.b[0] == 0x34 && good.b[1] == 0x12 && good.b[0xFF] == 0x00 && cfg16(&good, 0x82) == 0x1044);
    CHECK(cap_walk(&good, &w) == CW_OK && w.n == 4);
    CHECK(w.cap[0].off == 0x40 && w.cap[0].id == CAP_ID_PM && w.cap[1].off == 0x50 && w.cap[1].id == CAP_ID_MSI &&
          w.cap[2].off == 0x70 && w.cap[2].id == CAP_ID_PCIE && w.cap[3].off == 0xB0 && w.cap[3].id == CAP_ID_MSIX);
    CHECK(cap_find(&w, CAP_ID_PCIE) == 0x70 && cap_find(&w, 0x09) == 0);
    CHECK(decode_link(&good, 0x70, &li) && li.port_type == 0 && li.max_speed == 4 && li.max_width == 4 &&
          li.cur_speed == 4 && li.cur_width == 4 && li.target_speed == 4 && li.lnkcap == 0x00430844u);
    CHECK(check_dump(&good, GEN4X4, &li, line, sizeof line) == LV_OK);
    puts(line);
    CHECK(load_dump("dump_gen1x4_downgrade.txt", &c) == 256);
    CHECK(check_dump(&c, GEN4X4, &li, line, sizeof line) == LV_SPEED_DOWN && li.cur_speed == 1 && li.cur_width == 4);
    CHECK(strstr(line, "cur=Gen1(2.5GT/s) x4 [SPEED_DOWN]") != NULL);
    puts(line);
    c = good; c.b[0x82] = 0x14;                                       /* Gen4 x1 */
    CHECK(check_dump(&c, GEN4X4, &li, line, sizeof line) == LV_WIDTH_DOWN);
    puts(line);
    CHECK(load_dump("dump_cap_loop.txt", &c) == 256);
    CHECK(cap_walk(&c, &w) == CW_LOOP && w.bad == 0x40 && w.from == 0x50 && w.n == 2);
    /* 판정 표 (덤프는 Gen4 x4 cap 고정, cur 와 기대만 바꿈) */
    static const struct { uint8_t sta_lo, sta_hi; link_expect_t e; unsigned v; } VT[] = {
        { 0x44, 0x10, { 4, 4 }, LV_OK },
        { 0x44, 0x10, { 5, 4 }, LV_CAP_BELOW },                       /* Gen5 기대인데 부품이 Gen4 */
        { 0x43, 0x10, { 5, 4 }, LV_CAP_BELOW | LV_SPEED_DOWN },       /* 거기다 Gen3 로 붙음 */
        { 0x44, 0x10, { 4, 8 }, LV_CAP_BELOW },                       /* x8 슬롯에 x4 부품: 링크 문제 아님 */
        { 0x24, 0x10, { 4, 4 }, LV_WIDTH_DOWN },                      /* x2 */
        { 0x21, 0x10, { 4, 4 }, LV_SPEED_DOWN | LV_WIDTH_DOWN },      /* Gen1 x2 */
        { 0x84, 0x10, { 4, 4 }, LV_INCONSISTENT },                    /* x8 > cap x4 */
        { 0x05, 0x10, { 4, 4 }, LV_LINK_DOWN },                       /* 폭 0 */
        { 0x47, 0x10, { 4, 4 }, LV_BAD_ENC },                         /* 속도 코드 7 */
        { 0x34, 0x10, { 4, 4 }, LV_BAD_ENC },                         /* 폭 3 */
    };
    for (size_t i = 0; i < sizeof VT / sizeof VT[0]; i++) {
        c = good; c.b[0x82] = VT[i].sta_lo; c.b[0x83] = VT[i].sta_hi;
        unsigned v = check_dump(&c, VT[i].e, &li, line, sizeof line);
        CHECK(v == VT[i].v);
        if (v != VT[i].v) printf("   case %zu: got 0x%X want 0x%X\n", i, v, VT[i].v);
    }

    /* ===== Part B-1: 파서 · 체인 방어 ===== */
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff\n", &c) == 16 && c.b[15] == 0xFF);
    CHECK(parse_lspci_dump("01:00.0 Bridge: x\n0000:01:00.0 y\n\n", &c) == 0);
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77\n", &c) == -1);                     /* 16 바이트 미만 */
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff\n"
                           "20: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff\n", &c) == -1);  /* 0x10 줄 빠짐 */
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee fg\n", &c) == -1);  /* 잘못된 hex */
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff 12\n", &c) == -1);
    CHECK(parse_lspci_dump("00: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff\r\n", &c) == 16);   /* CRLF */
    static const struct { const char *what; uint16_t off; uint8_t val; size_t len; cw_status_t st; uint8_t bad, from; int n; } CT[] = {
        { "cap ptr low bits",  0x34, 0x43, 256, CW_OK,        0x00, 0x00, 4 },
        { "no cap list bit",   0x06, 0x00, 256, CW_NO_CAPLIST, 0x00, 0x00, 0 },
        { "ptr into header",   0x34, 0x20, 256, CW_BAD_PTR,   0x20, 0x34, 0 },
        { "next into header",  0x51, 0x3C, 256, CW_BAD_PTR,   0x3C, 0x50, 2 },
        { "self loop",         0x71, 0x70, 256, CW_LOOP,      0x70, 0x70, 3 },
        { "non-root 64B dump", 0x00, 0x34, 64,  CW_TRUNCATED, 0x40, 0x34, 0 },
        { "ptr 0 = no caps",   0x34, 0x00, 256, CW_OK,        0x00, 0x00, 0 },
    };
    for (size_t i = 0; i < sizeof CT / sizeof CT[0]; i++) {
        c = good; c.b[CT[i].off] = CT[i].val; c.len = CT[i].len;
        cw_status_t st = cap_walk(&c, &w);
        bool ok = st == CT[i].st && w.n == CT[i].n && (st == CW_OK || (w.bad == CT[i].bad && w.from == CT[i].from));
        CHECK(ok);
        if (!ok) printf("   %s: got %s n=%d bad=0x%02X from=0x%02X\n", CT[i].what, CW_NAME[st], w.n, w.bad, w.from);
    }
    c = good; memset(c.b, 0xFF, 256);
    CHECK(cap_walk(&c, &w) == CW_NOT_PRESENT);                                        /* 장치 없음 = all-ones */
    c = good; c.b[0x34] = 0x40;                                                       /* 최장 체인 47개 + 끝 */
    for (int i = 0; i < 48; i++) { uint8_t o = (uint8_t)(0x40 + 4 * i); c.b[o] = 0x09; c.b[o + 1] = (uint8_t)(i == 47 ? 0 : o + 4); }
    CHECK(cap_walk(&c, &w) == CW_OK && w.n == 48);
    c.b[0xFD] = 0x40;                                                                 /* 마지막이 처음으로 → 루프 */
    CHECK(cap_walk(&c, &w) == CW_LOOP && w.n == 48 && w.from == 0xFC && w.bad == 0x40);

    /* ===== Part B-2: LTSSM 링크업 모니터 ===== */
    puts("-- Part B --");
    lm_result_t r;
    char maj[256];
    lm_spec_t sp = { 100000, 10, 1000, 4, 1 };
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &sp, &r) == LM_OK && r.up && r.speed == 4 && r.width == 4);
    trace_majors(&r, maj, sizeof maj);
    CHECK(strcmp(maj, "DETECT>POLLING>CONFIG>L0>RECOVERY>L0>RECOVERY>L0") == 0);
    CHECK(r.restarts == 0 && r.speed_changes == 2 && r.rcvry_cnt == 0 && r.t_up > r.t_l0_first && r.t_up < 2400);
    lm_report(line, sizeof line, &r, &sp); puts(line); printf("   trace %s\n", maj);

    sim_power_on(LINKUP_EQ_FAIL, NSEG(LINKUP_EQ_FAIL), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &sp, &r) == LM_TIMEOUT && r.l0_seen && !r.up);
    CHECK(r.last_raw == L0 && r.speed == 1 && r.width == 4 && r.speed_changes == 1 && r.elapsed_us >= 100000);
    lm_report(line, sizeof line, &r, &sp); puts(line);

    static const struct { uint32_t max_restarts; lm_status_t want; } RT3[] = { { 1, LM_RETRY }, { 3, LM_OK } };
    for (size_t i = 0; i < 2; i++) {
        lm_spec_t s2 = sp; s2.max_restarts = RT3[i].max_restarts;
        sim_power_on(LINKUP_RESTART3, NSEG(LINKUP_RESTART3), NULL);
        CHECK(ltssm_monitor(&g_port, RP_CAP, &s2, &r) == RT3[i].want && r.restarts == 3 && r.up);
        if (i == 0) { lm_report(line, sizeof line, &r, &s2); puts(line); }
    }

    sim_power_on(LINKUP_BADCODE, NSEG(LINKUP_BADCODE), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &sp, &r) == LM_BAD_STATE && r.last_raw == 0x2A && r.elapsed_us < 400);
    lm_report(line, sizeof line, &r, &sp); puts(line);

    lm_spec_t slow = sp; slow.interval_us = 100;                                       /* 거친 샘플링 */
    sim_power_on(LINKUP_HIDDEN_RCVRY, NSEG(LINKUP_HIDDEN_RCVRY), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &slow, &r) == LM_UNSTABLE && r.rcvry_seen == 0 && r.rcvry_cnt == 1);
    lm_report(line, sizeof line, &r, &slow); puts(line);
    lm_spec_t fast = sp; fast.interval_us = 1;                                         /* 촘촘한 샘플링 */
    sim_power_on(LINKUP_HIDDEN_RCVRY, NSEG(LINKUP_HIDDEN_RCVRY), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &fast, &r) == LM_UNSTABLE && r.rcvry_seen == 1 && r.rcvry_cnt == 1);

    sim_power_on(LINKUP_DROP, NSEG(LINKUP_DROP), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &sp, &r) == LM_LINK_DOWN && r.last_raw == DQ && r.rcvry_seen == 1);
    lm_report(line, sizeof line, &r, &sp); puts(line);

    /* ===== Part C: 링크업 → 스냅샷 판정 → retrain 스트레스 ===== */
    puts("-- Part C --");
    stress_t st;
    const int N = 200;
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), NULL);
    CHECK(ltssm_monitor(&g_port, RP_CAP, &sp, &r) == LM_OK);
    snapshot_cfg(&g_port, &c);
    CHECK(cap_walk(&c, &w) == CW_OK && cap_find(&w, CAP_ID_PCIE) == RP_CAP);
    CHECK(decode_link(&c, RP_CAP, &li) && li.port_type == 4 && link_verdict(&li, GEN4X4) == LV_OK && li.dll_active);
    retrain_stress(&g_port, RP_CAP, GEN4X4, N, &st);
    CHECK(st.iters_run == N && st.pass == N && !st.aborted && g_sim.retrains == N);
    stress_report(stdout, "RP0(good)", GEN4X4, &st);

    rp_fault_t f1 = { .seed = 0x5EED1234u, .width_drop_permille = 20 };
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), &f1);
    ltssm_monitor(&g_port, RP_CAP, &sp, &r);
    retrain_stress(&g_port, RP_CAP, GEN4X4, N, &st);
    bool same = st.nfail == g_sim.ninjected && g_sim.ninjected > 0;
    for (int i = 0; same && i < st.nfail; i++) same = st.fail_iter[i] == g_sim.injected[i] && st.fail_sig[i] == RT_WIDTH_DOWN;
    CHECK(same);                                                    /* 주입한 iteration 과 정확히 같은 목록 */
    CHECK(!sig_persistent(&st, RT_WIDTH_DOWN) && st.sig[RT_WIDTH_DOWN].ex.width == 2);
    stress_report(stdout, "RP0(lane-marginal)", GEN4X4, &st);
    printf("   injected:");
    for (int i = 0; i < g_sim.ninjected; i++) printf(" %d", g_sim.injected[i]);
    printf("\n");
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), &f1);              /* 같은 결함, 순진한 검사 */
    ltssm_monitor(&g_port, RP_CAP, &sp, &r);
    int naive_fail = 0;
    for (int i = 0; i < N; i++) naive_fail += retrain_naive(&g_port, RP_CAP, GEN4X4) != RT_OK;
    CHECK(g_sim.ninjected > 0 && naive_fail == 0);                  /* 순진한 검사는 하나도 못 잡는다 */
    printf("   naive: injected=%d detected=%d\n", g_sim.ninjected, naive_fail);

    rp_fault_t f2 = { .speed_drop_from = 57 };
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), &f2);
    ltssm_monitor(&g_port, RP_CAP, &sp, &r);
    retrain_stress(&g_port, RP_CAP, GEN4X4, N, &st);
    CHECK(st.pass == 56 && st.sig[RT_SPEED_DOWN].count == N - 56 && st.sig[RT_SPEED_DOWN].first == 57 &&
          sig_persistent(&st, RT_SPEED_DOWN) && st.sig[RT_SPEED_DOWN].ex.speed == 3);
    stress_report(stdout, "RP0(gen4-fallback)", GEN4X4, &st);

    rp_fault_t f3 = { .stuck_at = 33 };
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), &f3);
    ltssm_monitor(&g_port, RP_CAP, &sp, &r);
    retrain_stress(&g_port, RP_CAP, GEN4X4, N, &st);
    CHECK(st.aborted && st.abort_iter == 33 && st.iters_run == 33 && st.pass == 32 && st.sig[RT_TIMEOUT].count == 1);
    CHECK(st.sig[RT_TIMEOUT].ex.ltssm == RL && (st.sig[RT_TIMEOUT].ex.lnksta & LNKSTA_LT) &&
          st.sig[RT_TIMEOUT].ex.elapsed_us >= RETRAIN_TIMEOUT_US);
    stress_report(stdout, "RP0(stuck-recovery)", GEN4X4, &st);

    rp_fault_t f4 = { .no_lbms = true };
    sim_power_on(LINKUP_GOOD, NSEG(LINKUP_GOOD), &f4);
    ltssm_monitor(&g_port, RP_CAP, &sp, &r);
    retrain_stress(&g_port, RP_CAP, GEN4X4, N, &st);
    CHECK(st.pass == 0 && st.sig[RT_NO_LBMS].count == N && st.sig[RT_NO_LBMS].first == 1 &&
          st.sig[RT_NO_LBMS].ex.rcvry_delta == 1 && sig_persistent(&st, RT_NO_LBMS));
    stress_report(stdout, "RP0(no-lbms)", GEN4X4, &st);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
