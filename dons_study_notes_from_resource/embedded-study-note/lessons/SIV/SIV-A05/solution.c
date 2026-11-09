/* SIV-A05 Errata 관리와 칩 리비전별 워크어라운드
 *
 * 가상의 센싱 SoC "SNS-100" 의 mock 위에서 chip ID/revision 레지스터를 읽어 errata 테이블
 * {ID, 영향 리비전 범위, applies-if 술어, 워크어라운드 함수, 검증 probe, status} 을 적용하는 프레임워크를 만든다.
 *   Part A: ID 디코드, 범위 비교, A0/A1/B0 에서 "정확히 맞는 집합" 만 적용 + read-back
 *   Part B: 순서/의존성, 멱등성, 충돌, 미지의 리비전 정책(fail-safe), MMU·캐시·클럭 변경 전 적용
 *   Part C: ECO 후 제거 — WA 를 끈 상태로 probe 를 돌려 버그가 정말 사라졌는지 증명, stale/missing/마스킹 탐지
 * 모든 errata·리비전·레지스터는 교육용 가상의 것이다. mock 레지스터는 SIV-B02 를 축약해 넣었다.
 * 파일 내 버그 ground truth(soc_t.bugs)는 mock 전용이고 펌웨어(프레임워크)는 읽지 않는다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= mock SoC: 레지스터 맵 ================= */
#define CHIP_PART 0x5A10u          /* CHIP_ID[31:16] */
enum {
    R_CHIP_ID = 0x000, /* RO  [31:16] part, [15:12] major(0=A,1=B), [11:8] minor(메탈 스핀), [7:0] fab */
    R_FUSE = 0x004,    /* RO  SKU 퓨즈 */
    R_PLL_CFG = 0x010, /* PLL 이 ramp 된 뒤 쓰기 무시 */
    R_L2_CHICKEN = 0x020, /* 캐시 enable 뒤 쓰기 무시 */
    R_DMA_CTRL = 0x030,
    R_PCIE_PM = 0x040,
    R_ISP_CFG = 0x050,
    R_TRIM = 0x060,
    REG_SPACE = 0x70
};
#define REG_N (REG_SPACE / 4)
#define FUSE_HAS_L2 (1u << 0)
#define FUSE_HAS_PCIE (1u << 1)
#define F_ALL (FUSE_HAS_L2 | FUSE_HAS_PCIE)
#define PLL_LOCK_FILT (1u << 0)
#define PLL_SSC_OFF (1u << 1)
#define L2_ARB_SAFE (1u << 0)
#define DMA_BURST_MASK 0x1Fu
#define PCIE_L0S_EN (1u << 0)
#define ISP_SYNC_RD (1u << 0)

/* 실리콘에 실제로 있는 버그 (ground truth — mock 전용) */
enum { BUG_DMA4K = 1 << 0, BUG_L2ARB = 1 << 1, BUG_PLLCOLD = 1 << 2, BUG_L0S = 1 << 3,
       BUG_ISPTORN = 1 << 4, BUG_SSC = 1 << 5 };

typedef struct { uint32_t off, changed; } wr_rec_t;
typedef struct {
    uint32_t regs[REG_N];
    uint32_t bugs;       /* ground truth */
    int temp_c;          /* 코너 */
    uint32_t rng;
    uint32_t l2_den;     /* L2 버그 발현 확률 = 1/l2_den */
    bool clk_fast, cache_on;
    int late_writes, order_viol; /* mock 이 관측한 시퀀스 위반 */
    wr_rec_t wr_log[16];
    int wr_n;
} soc_t;

static uint32_t chip_raw_id(unsigned major, unsigned minor, unsigned fab) {
    return CHIP_PART << 16 | (major & 0xFu) << 12 | (minor & 0xFu) << 8 | (fab & 0xFFu);
}
static uint32_t rng_next(uint32_t *x) {
    uint32_t v = *x;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    return *x = v;
}
static void soc_init(soc_t *s, uint32_t raw_id, uint32_t fuse, uint32_t bugs, int temp_c, uint32_t seed) {
    memset(s, 0, sizeof *s);
    s->regs[R_CHIP_ID / 4] = raw_id;
    s->regs[R_FUSE / 4] = fuse;
    s->regs[R_DMA_CTRL / 4] = 16;           /* 리셋: 최대 burst 16 */
    s->regs[R_PCIE_PM / 4] = PCIE_L0S_EN;   /* 리셋: L0s 켜짐 */
    s->bugs = bugs;
    s->temp_c = temp_c;
    s->l2_den = 64;
    s->rng = (seed * 2654435761u) ^ 0x9E3779B9u;
    if (s->rng == 0) s->rng = 1;
    for (int i = 0; i < 8; i++) rng_next(&s->rng);
}
static uint32_t reg_read32(const soc_t *s, uint32_t off) {
    if (off >= REG_SPACE || (off & 3u)) return 0xDEADBEEFu;
    return s->regs[off / 4];
}
static void reg_write32(soc_t *s, uint32_t off, uint32_t v) {
    if (off >= REG_SPACE || (off & 3u)) return;
    if (off == R_CHIP_ID || off == R_FUSE) return;
    if (off == R_PLL_CFG && s->clk_fast) { s->late_writes++; return; }
    if (off == R_L2_CHICKEN && s->cache_on) { s->late_writes++; return; }
    uint32_t old = s->regs[off / 4];
    if (off == R_PLL_CFG && (v & ~old & PLL_SSC_OFF) && !(v & PLL_LOCK_FILT)) s->order_viol++;
    s->regs[off / 4] = v;
    if (s->wr_n < 16) s->wr_log[s->wr_n++] = (wr_rec_t){ off, old ^ v };
}
static void rmw32(soc_t *s, uint32_t off, uint32_t mask, uint32_t val) {
    reg_write32(s, off, (reg_read32(s, off) & ~mask) | (val & mask));
}
/* 보드/부트 시퀀스가 하는 일 (프레임워크 밖) */
static void soc_clock_ramp(soc_t *s) { s->clk_fast = true; }
static void soc_cache_enable(soc_t *s) { s->cache_on = true; }

/* 버그 동작 모델 — 트리거 조건에서만 실패가 관측된다 */
static int soc_dma_xfer(soc_t *s, uint32_t addr, uint32_t len) {
    bool crosses = (addr & 0xFFFu) + len > 0x1000u;
    bool long_burst = (reg_read32(s, R_DMA_CTRL) & DMA_BURST_MASK) > 8;
    return (s->bugs & BUG_DMA4K) && crosses && long_burst;
}
static int soc_l2_evict(soc_t *s) {
    if (!(s->bugs & BUG_L2ARB) || (reg_read32(s, R_L2_CHICKEN) & L2_ARB_SAFE)) return 0;
    return (rng_next(&s->rng) >> 8) % s->l2_den == 0;          /* 간헐적 */
}
static int soc_pll_lock(soc_t *s) {
    return (s->bugs & BUG_PLLCOLD) && s->temp_c < 0 && !(reg_read32(s, R_PLL_CFG) & PLL_LOCK_FILT);
}
static int soc_ssc_lock(soc_t *s) {
    return (s->bugs & BUG_SSC) && !(reg_read32(s, R_PLL_CFG) & PLL_SSC_OFF);
}
static int soc_isp_read(soc_t *s) {
    return (s->bugs & BUG_ISPTORN) && !(reg_read32(s, R_ISP_CFG) & ISP_SYNC_RD);
}

/* ================= errata 테이블 타입 ================= */
#define REV(maj, min) ((uint16_t)((maj) << 8 | (min)))
#define REV_A0 REV(0, 0)
#define REV_A1 REV(0, 1)
#define REV_A2 REV(0, 2)
#define REV_B0 REV(1, 0)
#define REV_C0 REV(2, 0)
#define REV_OPEN 0xFFFFu   /* rev_max: 아직 고쳐진 리비전이 없음 */
#define REV_NONE 0xFFFEu   /* eco_propose_rev_max: 어떤 모델에서도 관측 안 됨 */

typedef struct { uint16_t part; uint8_t major, minor; uint16_t rev; uint8_t fab; } chip_id_t;
typedef enum { PH_EARLY, PH_PRE_CACHE, PH_LATE, PH_N } phase_t; /* EARLY=클럭 올리기 전, PRE_CACHE=MMU·캐시 켜기 전 */
typedef enum { ST_OPEN, ST_WONTFIX, ST_FIXED } erratum_status_t;
typedef struct { int exercised, failures; } probe_t;
typedef struct erratum {
    uint32_t id;
    const char *title;
    uint16_t rev_min, rev_max;              /* 포함 범위, rev_max 는 REV_OPEN 가능 */
    bool (*applies_if)(const soc_t *s);     /* NULL 이면 항상 */
    int (*apply)(soc_t *s);                 /* 워크어라운드. 0=성공 */
    probe_t (*probe)(soc_t *s, int trials); /* 버그 트리거 시도 (WA on/off 양쪽에서 돌린다) */
    erratum_status_t status;
    phase_t phase;
    uint32_t reg, mask, val;                /* 선언된 footprint: 충돌·read-back·감사에 쓴다 */
    uint32_t after[2];                      /* 순서만: 둘 다 적용될 때 먼저 와야 할 ID */
    uint32_t requires_id;                   /* 필수 의존: 이 ID 가 적용되지 않으면 오류 */
} erratum_t;

enum { E_OK = 0, E_BUS = -1, E_WRONG_PART = -2, E_UNKNOWN_REV = -3, E_APPLY = -4, E_NOT_STUCK = -5,
       E_FOOTPRINT = -6, E_CYCLE = -7, E_PHASE_INV = -8, E_CONFLICT = -9, E_REQUIRES = -10 };
typedef enum { POLICY_HALT, POLICY_LAB } policy_t;

#define MAX_ERRATA 16
typedef struct {
    chip_id_t chip;
    bool tainted;              /* 미지의 리비전으로 degraded 진행 */
    int n;
    uint32_t applied[MAX_ERRATA]; /* 실제 적용 순서 */
    uint32_t fail_id, fail_id2;
    int status;
} errata_ctx_t;

/* ================= SNS-100 errata 항목 (가상) ================= */
static bool has_l2(const soc_t *s) { return reg_read32(s, R_FUSE) & FUSE_HAS_L2; }
static bool has_pcie(const soc_t *s) { return reg_read32(s, R_FUSE) & FUSE_HAS_PCIE; }

static int wa_1001(soc_t *s) { rmw32(s, R_DMA_CTRL, DMA_BURST_MASK, 8); return 0; }
static int wa_1002(soc_t *s) { rmw32(s, R_L2_CHICKEN, L2_ARB_SAFE, L2_ARB_SAFE); return 0; }
static int wa_1003(soc_t *s) { rmw32(s, R_PLL_CFG, PLL_LOCK_FILT, PLL_LOCK_FILT); return 0; }
static int wa_1004(soc_t *s) { rmw32(s, R_PCIE_PM, PCIE_L0S_EN, 0); return 0; }
static int wa_1005(soc_t *s) { rmw32(s, R_ISP_CFG, ISP_SYNC_RD, ISP_SYNC_RD); return 0; }
static int wa_1006(soc_t *s) { rmw32(s, R_PLL_CFG, PLL_SSC_OFF, PLL_SSC_OFF); return 0; }

static probe_t probe_dma4k(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    for (int i = 0; i < trials; i++) { r.exercised++; r.failures += soc_dma_xfer(s, 0xFC4u + (uint32_t)(i % 8) * 4, 64); }
    return r;
}
static probe_t probe_l2arb(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    soc_cache_enable(s);
    for (int i = 0; i < trials; i++) { r.exercised++; r.failures += soc_l2_evict(s); }
    return r;
}
static probe_t probe_pllcold(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    for (int i = 0; i < trials; i++) { r.exercised++; r.failures += soc_pll_lock(s); }
    return r;
}
/* L0s 를 끄는 WA 가 켜지면 트리거 자체가 사라진다 → exercised==0 (test 를 가리는 WA) */
static probe_t probe_l0s(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    for (int i = 0; i < trials; i++) {
        if (reg_read32(s, R_FUSE) & FUSE_HAS_PCIE && reg_read32(s, R_PCIE_PM) & PCIE_L0S_EN) {
            r.exercised++;
            r.failures += (s->bugs & BUG_L0S) != 0;
        }
    }
    return r;
}
static probe_t probe_isp(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    for (int i = 0; i < trials; i++) { r.exercised++; r.failures += soc_isp_read(s); }
    return r;
}
static probe_t probe_ssc(soc_t *s, int trials) {
    probe_t r = { 0, 0 };
    for (int i = 0; i < trials; i++) { r.exercised++; r.failures += soc_ssc_lock(s); }
    return r;
}

/* 테이블 순서는 적용 순서가 아니다 (E1006 이 E1003 보다 앞에 있지만 뒤에 적용되어야 한다) */
static const erratum_t TABLE[] = {
    { 1001, "DMA burst16 4K crossing corrupts data", REV_A0, REV_A1, NULL, wa_1001, probe_dma4k,
      ST_FIXED, PH_LATE, R_DMA_CTRL, DMA_BURST_MASK, 8, { 0, 0 }, 0 },
    { 1002, "L2 arbitration hazard on eviction", REV_A0, REV_A0, has_l2, wa_1002, probe_l2arb,
      ST_FIXED, PH_PRE_CACHE, R_L2_CHICKEN, L2_ARB_SAFE, L2_ARB_SAFE, { 0, 0 }, 0 },
    { 1006, "SSC jitter breaks lock", REV_A0, REV_A0, NULL, wa_1006, probe_ssc,
      ST_FIXED, PH_EARLY, R_PLL_CFG, PLL_SSC_OFF, PLL_SSC_OFF, { 0, 0 }, 1003 },
    { 1003, "PLL false lock below 0C", REV_A0, REV_A0, NULL, wa_1003, probe_pllcold,
      ST_FIXED, PH_EARLY, R_PLL_CFG, PLL_LOCK_FILT, PLL_LOCK_FILT, { 0, 0 }, 0 },
    { 1004, "PCIe L0s exit hang", REV_A0, REV_OPEN, has_pcie, wa_1004, probe_l0s,
      ST_OPEN, PH_LATE, R_PCIE_PM, PCIE_L0S_EN, 0, { 0, 0 }, 0 },
    { 1005, "ISP stats torn read", REV_B0, REV_B0, NULL, wa_1005, probe_isp,
      ST_FIXED, PH_LATE, R_ISP_CFG, ISP_SYNC_RD, ISP_SYNC_RD, { 0, 0 }, 0 },
};
#define TABLE_N ((int)(sizeof TABLE / sizeof TABLE[0]))
static const uint16_t KNOWN[] = { REV_A0, REV_A1, REV_B0 };
#define KNOWN_N ((int)(sizeof KNOWN / sizeof KNOWN[0]))

/* 칩 모델 (ground truth) */
typedef struct { const char *name; uint8_t major, minor; uint32_t bugs; } chip_model_t;
static const chip_model_t M_A0 = { "A0", 0, 0, BUG_DMA4K | BUG_L2ARB | BUG_PLLCOLD | BUG_L0S | BUG_SSC };
static const chip_model_t M_A1 = { "A1", 0, 1, BUG_DMA4K | BUG_L0S };
static const chip_model_t M_B0 = { "B0", 1, 0, BUG_ISPTORN | BUG_L0S };
static const chip_model_t M_B0_ECO = { "B0-ECO", 1, 0, BUG_ISPTORN };          /* L0s 버그까지 고친 B0 */
static const chip_model_t M_B0_BRANCH = { "B0-BR", 1, 0, BUG_ISPTORN | BUG_L0S | BUG_L2ARB }; /* A0 DB 에서 분기: A1 수정이 빠짐 */

static uint32_t model_raw(const chip_model_t *m) { return chip_raw_id(m->major, m->minor, 0x11); }
static void rev_name(uint16_t rev, char out[8]) {
    if (rev == REV_OPEN) { snprintf(out, 8, "OPEN"); return; }
    snprintf(out, 8, "%c%u", 'A' + (rev >> 8), rev & 0xFFu);
}

/* ================= Part A: ID 디코드와 정확한 적용 ================= */

/* TODO(A1): raw 를 디코드해 *c 를 채운다. 0x00000000 / 0xFFFFFFFF 는 E_BUS(버스 미준비/미프로그램).
 * part != CHIP_PART 이면 *c 는 채우되 E_WRONG_PART. fab 는 리비전이 아니다. rev = REV(major, minor). */
static int chip_decode(uint32_t raw, chip_id_t *c) {
    if (raw == 0 || raw == 0xFFFFFFFFu) return E_BUS;
    c->part = (uint16_t)(raw >> 16);
    c->major = (uint8_t)(raw >> 12 & 0xFu);
    c->minor = (uint8_t)(raw >> 8 & 0xFu);
    c->fab = (uint8_t)(raw & 0xFFu);
    c->rev = REV(c->major, c->minor);
    return c->part == CHIP_PART ? E_OK : E_WRONG_PART;
}

/* TODO(A2): rev 가 [rev_min, rev_max] 안인가 (양끝 포함, rev_max==REV_OPEN 이면 상한 없음). */
static bool rev_in_range(const erratum_t *e, uint16_t rev) {
    return rev >= e->rev_min && (e->rev_max == REV_OPEN || rev <= e->rev_max);
}

/* TODO(A3): 리비전 범위 안이고 applies_if 가 NULL 이거나 참이면 적용 대상. */
static bool erratum_matches(const erratum_t *e, const soc_t *s, const chip_id_t *c) {
    return rev_in_range(e, c->rev) && (e->applies_if == NULL || e->applies_if(s));
}

/* TODO(A4): 테이블에서 이 칩에 맞는 항목을 테이블 순서대로 out 에 담고 개수를 돌려준다 (max 초과 시 잘라낸다). */
static int errata_select(const erratum_t *t, int n, const soc_t *s, const chip_id_t *c, const erratum_t **out, int max) {
    int m = 0;
    for (int i = 0; i < n && m < max; i++)
        if (erratum_matches(&t[i], s, c)) out[m++] = &t[i];
    return m;
}

/* TODO(A5): 워크어라운드 하나를 적용한다.
 *   s->wr_n=0 → e->apply(s) (0 이 아니면 fail_id 기록, E_APPLY)
 *   → read-back: (reg & mask) != (val & mask) 면 fail_id 기록, E_NOT_STUCK
 *   → write 감사: wr_log 의 쓰기가 선언된 reg/mask 를 벗어나 비트를 바꿨으면 fail_id 기록, E_FOOTPRINT
 *   → 성공 시 ctx->applied[ctx->n++] 에 id 를 추가하고 E_OK. */
static int errata_apply_one(const erratum_t *e, soc_t *s, errata_ctx_t *ctx) {
    s->wr_n = 0;
    if (e->apply(s) != 0) { ctx->fail_id = e->id; return E_APPLY; }
    if ((reg_read32(s, e->reg) & e->mask) != (e->val & e->mask)) { ctx->fail_id = e->id; return E_NOT_STUCK; }
    for (int i = 0; i < s->wr_n; i++) {
        uint32_t allowed = s->wr_log[i].off == e->reg ? e->mask : 0;
        if (s->wr_log[i].changed & ~allowed) { ctx->fail_id = e->id; return E_FOOTPRINT; }
    }
    if (ctx->n < MAX_ERRATA) ctx->applied[ctx->n++] = e->id;
    return E_OK;
}

/* ================= Part B: 순서·충돌·멱등성·미지의 리비전·부트 ================= */

/* TODO(B1): c->rev 가 known 목록에 있으면 E_OK, 없으면 E_UNKNOWN_REV.
 * 범위 비교로 하면 안 된다: A2 는 A1 과 B0 사이지만 만들어진 적 없는 리비전이다. */
static int chip_classify(const chip_id_t *c, const uint16_t *known, int nk) {
    for (int i = 0; i < nk; i++)
        if (known[i] == c->rev) return E_OK;
    return E_UNKNOWN_REV;
}

static int sel_index(const erratum_t *const *sel, int n, uint32_t id) {
    for (int i = 0; i < n; i++) if (sel[i]->id == id) return i;
    return -1;
}
static int dep_count(const erratum_t *e, uint32_t out[3]) {
    int k = 0;
    if (e->requires_id) out[k++] = e->requires_id;
    for (int a = 0; a < 2; a++) if (e->after[a]) out[k++] = e->after[a];
    return k;
}

/* TODO(B2): sel[] 를 적용 순서로 out[] 에 정렬해 개수를 돌려준다.
 *   1) 의존 대상(after, requires_id)이 sel 안에 있는데 더 늦은 phase 면 E_PHASE_INV (fail_id 없음)
 *   2) phase 오름차순, 같은 phase 안에서는 의존 먼저(위상 정렬), 동률은 sel 의 순서
 *   3) 진행 불가(사이클)면 E_CYCLE. sel 에 없는 의존 ID 는 순서 제약이 없다(적용 안 되니까). */
static int errata_order(const erratum_t *const *sel, int n, const erratum_t **out) {
    uint32_t deps[3];
    for (int i = 0; i < n; i++) {
        int k = dep_count(sel[i], deps);
        for (int d = 0; d < k; d++) {
            int j = sel_index(sel, n, deps[d]);
            if (j >= 0 && sel[j]->phase > sel[i]->phase) return E_PHASE_INV;
        }
    }
    bool done[MAX_ERRATA] = { false };
    int m = 0;
    for (int ph = 0; ph < PH_N; ph++) {
        for (;;) {
            int pick = -1, remaining = 0;
            for (int i = 0; i < n; i++) {
                if (done[i] || (int)sel[i]->phase != ph) continue;
                remaining++;
                int k = dep_count(sel[i], deps);
                bool ready = true;
                for (int d = 0; d < k; d++) {
                    int j = sel_index(sel, n, deps[d]);
                    if (j >= 0 && !done[j]) ready = false;
                }
                if (ready && pick < 0) pick = i;
            }
            if (remaining == 0) break;
            if (pick < 0) return E_CYCLE;
            out[m++] = sel[pick];
            done[pick] = true;
        }
    }
    return m;
}

/* TODO(B3): 동시에 적용될 두 항목이 같은 레지스터의 겹치는 비트에 서로 다른 값을 쓰면 1 을 돌려주고
 * *a,*b 에 인덱스(a<b)를 쓴다. 없으면 0. (값이 같으면 충돌이 아니다.) */
static int errata_find_conflict(const erratum_t *const *sel, int n, int *a, int *b) {
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (sel[i]->reg != sel[j]->reg) continue;
            uint32_t overlap = sel[i]->mask & sel[j]->mask;
            if (overlap && ((sel[i]->val ^ sel[j]->val) & overlap)) { *a = i; *b = j; return 1; }
        }
    return 0;
}

/* TODO(B4): 새 soc 에서 e->apply 를 두 번 돌려 첫 번째 뒤와 두 번째 뒤의 레지스터가 같으면 1(멱등).
 * 다르면 0, *bad_off 에 처음 달라진 레지스터 오프셋. */
static int errata_check_idempotent(const erratum_t *e, uint32_t raw, uint32_t fuse, uint32_t bugs, uint32_t *bad_off) {
    soc_t s;
    soc_init(&s, raw, fuse, bugs, 25, 1);
    e->apply(&s);
    uint32_t snap[REG_N];
    memcpy(snap, s.regs, sizeof snap);
    e->apply(&s);
    for (int i = 0; i < REG_N; i++)
        if (s.regs[i] != snap[i]) { *bad_off = (uint32_t)i * 4; return 0; }
    return 1;
}

/* TODO(B5): 부트 흐름 전체.
 *   ctx 를 0 으로 비운다 → CHIP_ID 읽어 chip_decode (실패면 ctx->status 에 넣고 그대로 반환)
 *   → chip_classify. 미지의 리비전이면 POLICY_HALT: E_UNKNOWN_REV 반환 / POLICY_LAB: tainted=true,
 *     rev_max==REV_OPEN 이고 rev>=rev_min 이고 applies_if 를 만족하는 항목만 선택 (닫힌 범위 항목은 적용하지 않음).
 *     known 이면 errata_select.
 *   → requires_id 가 선택 집합에 없으면 fail_id=그 항목, E_REQUIRES
 *   → errata_find_conflict 면 fail_id/fail_id2, E_CONFLICT → errata_order (음수면 그대로 반환)
 *   → phase 별로 errata_apply_one. PH_EARLY 다음 soc_clock_ramp, PH_PRE_CACHE 다음 soc_cache_enable.
 *   실패는 ctx->status 에 저장하고 즉시 반환. 성공이면 E_OK. */
static int errata_boot(const erratum_t *t, int n, soc_t *s, const uint16_t *known, int nk, policy_t pol, errata_ctx_t *ctx) {
    memset(ctx, 0, sizeof *ctx);
    int rc = chip_decode(reg_read32(s, R_CHIP_ID), &ctx->chip);
    if (rc != E_OK) return ctx->status = rc;
    const erratum_t *sel[MAX_ERRATA], *ord[MAX_ERRATA];
    int m = 0;
    if (chip_classify(&ctx->chip, known, nk) == E_UNKNOWN_REV) {
        if (pol == POLICY_HALT) return ctx->status = E_UNKNOWN_REV;
        ctx->tainted = true;
        for (int i = 0; i < n && m < MAX_ERRATA; i++)
            if (t[i].rev_max == REV_OPEN && ctx->chip.rev >= t[i].rev_min && (t[i].applies_if == NULL || t[i].applies_if(s)))
                sel[m++] = &t[i];
    } else {
        m = errata_select(t, n, s, &ctx->chip, sel, MAX_ERRATA);
    }
    for (int i = 0; i < m; i++)
        if (sel[i]->requires_id && sel_index(sel, m, sel[i]->requires_id) < 0) {
            ctx->fail_id = sel[i]->id;
            return ctx->status = E_REQUIRES;
        }
    int a, b;
    if (errata_find_conflict(sel, m, &a, &b)) {
        ctx->fail_id = sel[a]->id; ctx->fail_id2 = sel[b]->id;
        return ctx->status = E_CONFLICT;
    }
    int k = errata_order(sel, m, ord);
    if (k < 0) return ctx->status = k;
    for (int ph = 0; ph < PH_N; ph++) {
        for (int i = 0; i < k; i++) {
            if ((int)ord[i]->phase != ph) continue;
            rc = errata_apply_one(ord[i], s, ctx);
            if (rc != E_OK) return ctx->status = rc;
        }
        if (ph == PH_EARLY) soc_clock_ramp(s);
        if (ph == PH_PRE_CACHE) soc_cache_enable(s);
    }
    return ctx->status = E_OK;
}

/* ================= Part C: ECO 후 제거 — stale/missing/마스킹 감사 ================= */
typedef enum { V_CLEAN, V_NEEDED, V_STALE, V_MISSING, V_WA_BROKEN, V_INCONCLUSIVE, V_NO_PROBE } verdict_t;
static const char *const V_NAME[] = { "CLEAN", "NEEDED", "STALE", "MISSING", "WA_BROKEN", "INCONCLUSIVE", "NO_PROBE" };
typedef struct { verdict_t v; bool masked; int off_ex, off_fail, on_ex, on_fail; } audit_t;
typedef struct { const int *temps; int nt; int trials; uint32_t seed; uint32_t fuse; } audit_opts_t;
typedef struct { int dup, bad_range, no_fn, status_mismatch; uint32_t first_id; } lint_t;

static const erratum_t *tbl_find(const erratum_t *t, int n, uint32_t id) {
    for (int i = 0; i < n; i++) if (t[i].id == id) return &t[i];
    return NULL;
}
/* requires 체인을 먼저 깔고 e 를 적용 (감사에서는 항목 하나와 그 필수 의존만 켠다) */
static void apply_chain(const erratum_t *t, int n, const erratum_t *e, soc_t *s, int depth) {
    if (depth > MAX_ERRATA) return;
    if (e->requires_id) {
        const erratum_t *dep = tbl_find(t, n, e->requires_id);
        if (dep) apply_chain(t, n, dep, s, depth + 1);
    }
    e->apply(s);
}

/* TODO(C1): 테이블 정적 검사. l 을 0 으로 비우고 항목별로
 *   dup: 앞선 항목과 id 중복 / bad_range: rev_max!=REV_OPEN 인데 rev_min>rev_max / no_fn: apply 또는 probe 가 NULL
 *   status_mismatch: ST_FIXED 인데 rev_max==REV_OPEN, 또는 ST_FIXED 가 아닌데 rev_max!=REV_OPEN
 * 를 센다. first_id 는 문제가 처음 발견된 항목의 id. 반환값 = 네 카운트의 합. */
static int errata_lint(const erratum_t *t, int n, lint_t *l) {
    memset(l, 0, sizeof *l);
    int total = 0;
    for (int i = 0; i < n; i++) {
        int before = total;
        for (int j = 0; j < i; j++) if (t[j].id == t[i].id) { l->dup++; total++; break; }
        if (t[i].rev_max != REV_OPEN && t[i].rev_min > t[i].rev_max) { l->bad_range++; total++; }
        if (t[i].apply == NULL || t[i].probe == NULL) { l->no_fn++; total++; }
        if ((t[i].status == ST_FIXED) != (t[i].rev_max != REV_OPEN)) { l->status_mismatch++; total++; }
        if (total > before && l->first_id == 0) l->first_id = t[i].id;
    }
    return total;
}

/* TODO(C2): 새 soc 를 만들고(model_raw, fuse, model->bugs, temp, seed) wa_on 이면 apply_chain 후
 * e->probe(&s, trials) 결과를 돌려준다. WA 가 꺼진 상태의 결과가 버그 존재의 증거다. */
static probe_t probe_run(const erratum_t *t, int n, const erratum_t *e, const chip_model_t *m,
                         int temp, uint32_t seed, int trials, uint32_t fuse, bool wa_on) {
    soc_t s;
    soc_init(&s, model_raw(m), fuse, m->bugs, temp, seed);
    if (wa_on) apply_chain(t, n, e, &s, 0);
    return e->probe(&s, trials);
}

/* TODO(C3): 온도 코너 전체에서 off/on probe 를 합산하고 판정한다. probe 가 NULL 이면 V_NO_PROBE.
 *   applies = erratum_matches(새 soc, decode 결과)
 *   masked  = applies && off_ex>0 && on_ex==0     (WA 가 트리거 자체를 없애 on 결과가 공허함)
 *   !applies: off_fail>0 → V_MISSING, 아니면 V_CLEAN
 *   applies:  off_ex==0 → V_INCONCLUSIVE, off_fail==0 → V_STALE, on_fail>0 → V_WA_BROKEN, 아니면 V_NEEDED */
static void audit_one(const erratum_t *t, int n, const erratum_t *e, const chip_model_t *m, const audit_opts_t *o, audit_t *a) {
    memset(a, 0, sizeof *a);
    if (e->probe == NULL) { a->v = V_NO_PROBE; return; }
    soc_t s;
    chip_id_t c;
    soc_init(&s, model_raw(m), o->fuse, m->bugs, 25, 1);
    chip_decode(reg_read32(&s, R_CHIP_ID), &c);
    bool applies = erratum_matches(e, &s, &c);
    for (int i = 0; i < o->nt; i++) {
        uint32_t seed = o->seed + (uint32_t)i * 977u;
        probe_t off = probe_run(t, n, e, m, o->temps[i], seed, o->trials, o->fuse, false);
        probe_t on = probe_run(t, n, e, m, o->temps[i], seed, o->trials, o->fuse, true);
        a->off_ex += off.exercised; a->off_fail += off.failures;
        a->on_ex += on.exercised; a->on_fail += on.failures;
    }
    a->masked = applies && a->off_ex > 0 && a->on_ex == 0;
    if (!applies) a->v = a->off_fail > 0 ? V_MISSING : V_CLEAN;
    else if (a->off_ex == 0) a->v = V_INCONCLUSIVE;
    else if (a->off_fail == 0) a->v = V_STALE;
    else if (a->on_fail > 0) a->v = V_WA_BROKEN;
    else a->v = V_NEEDED;
}

/* TODO(C4): models[] 를 리비전 오름차순이라 가정한다. e 의 버그가 관측된(WA off, off_fail>0) 가장 높은 리비전을 찾아
 *   마지막 모델에서도 관측되면 REV_OPEN(아직 못 고침), 어떤 모델에서도 안 보이면 REV_NONE, 그 외에는 그 리비전을
 *   제안한다. (감사 결과만으로 정하는 "새 rev_max 제안" — 사람이 errata 시트와 대조해서 커밋한다.) */
static uint16_t eco_propose_rev_max(const erratum_t *t, int n, const erratum_t *e, const chip_model_t *const *models, int nm, const audit_opts_t *o) {
    int last = -1;
    for (int i = 0; i < nm; i++) {
        audit_t a;
        audit_one(t, n, e, models[i], o, &a);
        if (a.off_fail > 0) last = i;
    }
    if (last < 0) return REV_NONE;
    if (last == nm - 1) return REV_OPEN;
    return REV(models[last]->major, models[last]->minor);
}

/* 주어진 코드: 사람이 읽는 리포트 */
static void audit_format(char *b, size_t cap, const erratum_t *e, const chip_model_t *m, const audit_t *a) {
    char lo[8], hi[8];
    rev_name(e->rev_min, lo);
    rev_name(e->rev_max, hi);
    bool bad = a->v == V_STALE || a->v == V_MISSING || a->v == V_WA_BROKEN || a->v == V_INCONCLUSIVE || a->v == V_NO_PROBE;
    snprintf(b, cap, "%s erratum=E%u chip=%s range=%s..%s verdict=%s off(ex=%d,fail=%d) on(ex=%d,fail=%d)%s",
             bad ? "FAIL" : "OK  ", (unsigned)e->id, m->name, lo, hi, V_NAME[a->v],
             a->off_ex, a->off_fail, a->on_ex, a->on_fail, a->masked ? " masked_by_wa" : "");
}
static void ctx_format(char *b, size_t cap, const errata_ctx_t *c) {
    char r[8];
    rev_name(c->chip.rev, r);
    int w = snprintf(b, cap, "SNS-100 rev=%s fab=0x%02X tainted=%d status=%d applied=[", r, c->chip.fab, c->tainted, c->status);
    for (int i = 0; i < c->n && w < (int)cap; i++)
        w += snprintf(b + w, cap - (size_t)w, "%sE%u", i ? "," : "", (unsigned)c->applied[i]);
    if (w < (int)cap) snprintf(b + w, cap - (size_t)w, "]");
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __func__, __LINE__, #expr); } } while (0)

static bool ids_eq(const uint32_t *a, int n, const uint32_t *b, int m) {
    if (n != m) return false;
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
static void boot_model(const chip_model_t *m, uint32_t fuse, soc_t *s, errata_ctx_t *c, policy_t pol) {
    soc_init(s, model_raw(m), fuse, m->bugs, 25, 7);
    errata_boot(TABLE, TABLE_N, s, KNOWN, KNOWN_N, pol, c);
}

/* ---- Part A ---- */
static void test_part_a(void) {
    printf("== Part A: decode, range, exact set ==\n");
    static const struct { uint32_t raw; int rc; uint16_t rev; uint8_t fab; } dec[] = {
        { 0x5A100011u, E_OK, REV_A0, 0x11 },
        { 0x5A1001A7u, E_OK, REV_A1, 0xA7 },
        { 0x5A101000u, E_OK, REV_B0, 0x00 },
        { 0x5A102300u, E_OK, REV(2, 3), 0x00 },
        { 0x5A110000u, E_WRONG_PART, REV_A0, 0x00 },
        { 0x00000000u, E_BUS, 0, 0 },
        { 0xFFFFFFFFu, E_BUS, 0, 0 },
    };
    int dok = 0;
    for (size_t i = 0; i < sizeof dec / sizeof dec[0]; i++) {
        chip_id_t c;
        memset(&c, 0, sizeof c);
        int rc = chip_decode(dec[i].raw, &c);
        dok += rc == dec[i].rc && (rc != E_OK || (c.rev == dec[i].rev && c.fab == dec[i].fab && c.part == CHIP_PART));
    }
    CHECK(dok == (int)(sizeof dec / sizeof dec[0]));

    /* major 가 minor 를 이긴다: A9 < B0 — 문자열·10진 비교 함정 */
    chip_id_t a9;
    memset(&a9, 0, sizeof a9);
    CHECK(chip_decode(chip_raw_id(0, 9, 0), &a9) == E_OK && a9.rev < REV_B0 && a9.rev > REV_A1);

    erratum_t e;
    memset(&e, 0, sizeof e);
    e.rev_min = REV_A1; e.rev_max = REV_B0;
    static const struct { uint16_t rev; bool in; } rc[] = {
        { REV_A0, false }, { REV_A1, true }, { REV_A2, true }, { REV_B0, true }, { REV(1, 1), false }, { REV_C0, false },
    };
    int rok = 0;
    for (size_t i = 0; i < sizeof rc / sizeof rc[0]; i++) rok += rev_in_range(&e, rc[i].rev) == rc[i].in;
    CHECK(rok == (int)(sizeof rc / sizeof rc[0]));
    e.rev_max = REV_OPEN;
    CHECK(rev_in_range(&e, REV_C0) && rev_in_range(&e, REV(15, 15)) && !rev_in_range(&e, REV_A0));

    /* 술어: 범위 안이어도 SKU 에 해당 기능이 없으면 적용 안 함 */
    soc_t s;
    chip_id_t c;
    memset(&c, 0, sizeof c);
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    chip_decode(reg_read32(&s, R_CHIP_ID), &c);
    CHECK(erratum_matches(tbl_find(TABLE, TABLE_N, 1002), &s, &c));
    soc_init(&s, model_raw(&M_A0), FUSE_HAS_PCIE, M_A0.bugs, 25, 1);
    CHECK(!erratum_matches(tbl_find(TABLE, TABLE_N, 1002), &s, &c));

    /* A0/A1/B0 × SKU: 정확히 이 집합 */
    static const struct { const chip_model_t *m; uint32_t fuse; uint32_t want[6]; int n; } sets[] = {
        { &M_A0, F_ALL, { 1001, 1002, 1006, 1003, 1004, 0 }, 5 },
        { &M_A0, FUSE_HAS_PCIE, { 1001, 1006, 1003, 1004, 0, 0 }, 4 },
        { &M_A0, FUSE_HAS_L2, { 1001, 1002, 1006, 1003, 0, 0 }, 4 },
        { &M_A1, F_ALL, { 1001, 1004, 0, 0, 0, 0 }, 2 },
        { &M_B0, F_ALL, { 1004, 1005, 0, 0, 0, 0 }, 2 },
        { &M_B0, FUSE_HAS_L2, { 1005, 0, 0, 0, 0, 0 }, 1 },
    };
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        soc_t chip;
        chip_id_t cid;
        const erratum_t *sel[MAX_ERRATA];
        uint32_t got[MAX_ERRATA];
        memset(&cid, 0, sizeof cid);
        soc_init(&chip, model_raw(sets[i].m), sets[i].fuse, sets[i].m->bugs, 25, 1);
        chip_decode(reg_read32(&chip, R_CHIP_ID), &cid);
        int n = errata_select(TABLE, TABLE_N, &chip, &cid, sel, MAX_ERRATA);
        for (int k = 0; k < n; k++) got[k] = sel[k]->id;
        CHECK(ids_eq(got, n, sets[i].want, sets[i].n));
    }

    /* 적용 결과: A0 에서만 바뀌는 레지스터, B0 에서는 안 바뀌는 레지스터 */
    errata_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    soc_t a0, b0;
    chip_id_t ca, cb;
    const erratum_t *sa[MAX_ERRATA], *sb[MAX_ERRATA];
    memset(&ca, 0, sizeof ca);
    memset(&cb, 0, sizeof cb);
    soc_init(&a0, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    chip_decode(reg_read32(&a0, R_CHIP_ID), &ca);
    int na = errata_select(TABLE, TABLE_N, &a0, &ca, sa, MAX_ERRATA);
    int okc = 0;
    for (int i = 0; i < na; i++) okc += errata_apply_one(sa[i], &a0, &ctx) == E_OK;
    CHECK(na == 5 && okc == 5 && ctx.n == 5);
    CHECK((reg_read32(&a0, R_DMA_CTRL) & DMA_BURST_MASK) == 8 && reg_read32(&a0, R_L2_CHICKEN) == L2_ARB_SAFE);
    CHECK(reg_read32(&a0, R_PLL_CFG) == (PLL_LOCK_FILT | PLL_SSC_OFF) && reg_read32(&a0, R_PCIE_PM) == 0);

    memset(&ctx, 0, sizeof ctx);
    soc_init(&b0, model_raw(&M_B0), F_ALL, M_B0.bugs, 25, 1);
    chip_decode(reg_read32(&b0, R_CHIP_ID), &cb);
    int nb = errata_select(TABLE, TABLE_N, &b0, &cb, sb, MAX_ERRATA);
    okc = 0;
    for (int i = 0; i < nb; i++) okc += errata_apply_one(sb[i], &b0, &ctx) == E_OK;
    CHECK(nb == 2 && okc == 2);
    CHECK(reg_read32(&b0, R_L2_CHICKEN) == 0 && reg_read32(&b0, R_PLL_CFG) == 0 &&
          (reg_read32(&b0, R_DMA_CTRL) & DMA_BURST_MASK) == 16 && reg_read32(&b0, R_ISP_CFG) == ISP_SYNC_RD);
}

/* ---- Part B ---- */
static int wa_toggle(soc_t *s) { reg_write32(s, R_ISP_CFG, reg_read32(s, R_ISP_CFG) ^ ISP_SYNC_RD); return 0; }
static int wa_trim_add(soc_t *s) { reg_write32(s, R_TRIM, reg_read32(s, R_TRIM) + 3); return 0; }
static int wa_fail(soc_t *s) { (void)s; return -1; }
static int wa_wide(soc_t *s) { rmw32(s, R_DMA_CTRL, DMA_BURST_MASK | 1u << 8, 8 | 1u << 8); return 0; }
static int wa_l0s_on(soc_t *s) { rmw32(s, R_PCIE_PM, PCIE_L0S_EN, PCIE_L0S_EN); return 0; }

static void copy_table(erratum_t *dst) { memcpy(dst, TABLE, sizeof TABLE); }
static erratum_t *tfind(erratum_t *t, int n, uint32_t id) {
    for (int i = 0; i < n; i++) if (t[i].id == id) return &t[i];
    return NULL;
}

static void test_part_b(void) {
    printf("== Part B: order, conflict, idempotency, unknown rev, boot ==\n");
    soc_t s;
    errata_ctx_t c;
    memset(&c, 0, sizeof c);

    /* 순서: 테이블 순서와 다르게 phase + 의존성으로 */
    boot_model(&M_A0, F_ALL, &s, &c, POLICY_HALT);
    static const uint32_t want_a0[] = { 1003, 1006, 1002, 1001, 1004 };
    CHECK(c.status == E_OK && ids_eq(c.applied, c.n, want_a0, 5));
    CHECK(s.late_writes == 0 && s.order_viol == 0 && s.clk_fast && s.cache_on);
    boot_model(&M_B0, F_ALL, &s, &c, POLICY_HALT);
    static const uint32_t want_b0[] = { 1004, 1005 };
    CHECK(c.status == E_OK && ids_eq(c.applied, c.n, want_b0, 2) && reg_read32(&s, R_PLL_CFG) == 0);

    /* 의존 선언을 지우면 mock 이 순서 위반을 본다 */
    erratum_t t[MAX_ERRATA];
    copy_table(t);
    tfind(t, TABLE_N, 1006)->requires_id = 0;
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c);
    CHECK(c.status == E_OK && s.order_viol == 1);

    /* 사이클 */
    copy_table(t);
    tfind(t, TABLE_N, 1003)->after[0] = 1006;
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_CYCLE && c.n == 0 && !s.clk_fast);

    /* phase 역전: E1003 을 LATE 로 잘못 옮김 → E1006(EARLY) 이 의존할 수 없음 */
    copy_table(t);
    tfind(t, TABLE_N, 1003)->phase = PH_LATE;
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_PHASE_INV && c.n == 0);

    /* 필수 의존이 이 칩에서 선택되지 않음 */
    copy_table(t);
    tfind(t, TABLE_N, 1006)->rev_max = REV_A1;
    soc_init(&s, model_raw(&M_A1), F_ALL, M_A1.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_REQUIRES && c.fail_id == 1006);

    /* 충돌: A0 에서만 동시에 선택된다 */
    copy_table(t);
    t[TABLE_N] = (erratum_t){ 1099, "conflicting L0s keep-on", REV_A0, REV_A0, NULL, wa_l0s_on, probe_l0s,
                              ST_FIXED, PH_LATE, R_PCIE_PM, PCIE_L0S_EN, PCIE_L0S_EN, { 0, 0 }, 0 };
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N + 1, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_CONFLICT &&
          ((c.fail_id == 1004 && c.fail_id2 == 1099) || (c.fail_id == 1099 && c.fail_id2 == 1004)) && c.n == 0);
    soc_init(&s, model_raw(&M_B0), F_ALL, M_B0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N + 1, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_OK && c.n == 2);

    /* 같은 값이면 충돌이 아니다 */
    {
        const erratum_t *p[2];
        erratum_t x = TABLE[0], y = TABLE[0];
        y.id = 2000;
        p[0] = &x; p[1] = &y;
        int a = -1, b = -1;
        CHECK(errata_find_conflict(p, 2, &a, &b) == 0);
        y.val = 4;
        CHECK(errata_find_conflict(p, 2, &a, &b) == 1 && a == 0 && b == 1);
    }

    /* 멱등성: 테이블 전 항목 통과, 심은 결함 2종 검출 + 오프셋 */
    uint32_t off = 0xFFFFu;
    int idem = 0;
    for (int i = 0; i < TABLE_N; i++)
        idem += errata_check_idempotent(&TABLE[i], model_raw(&M_A0), F_ALL, M_A0.bugs, &off);
    CHECK(idem == TABLE_N);
    erratum_t bad_toggle = TABLE[0], bad_trim = TABLE[0];
    bad_toggle.apply = wa_toggle;
    bad_trim.apply = wa_trim_add;
    off = 0;
    CHECK(errata_check_idempotent(&bad_toggle, model_raw(&M_A0), F_ALL, M_A0.bugs, &off) == 0 && off == R_ISP_CFG);
    off = 0;
    CHECK(errata_check_idempotent(&bad_trim, model_raw(&M_A0), F_ALL, M_A0.bugs, &off) == 0 && off == R_TRIM);

    /* 미지의 리비전: 표 기반 */
    static const struct { uint8_t maj, min; policy_t pol; int rc; bool taint; int n; } unk[] = {
        { 2, 0, POLICY_HALT, E_UNKNOWN_REV, false, 0 },    /* C0: 미래 실리콘 */
        { 2, 0, POLICY_LAB, E_OK, true, 1 },               /* open-ended E1004 만 */
        { 0, 2, POLICY_HALT, E_UNKNOWN_REV, false, 0 },    /* A2: 범위 사이지만 만들어진 적 없음 */
        { 0, 2, POLICY_LAB, E_OK, true, 1 },
        { 1, 0, POLICY_HALT, E_OK, false, 2 },             /* B0: 알려짐 */
    };
    int uok = 0;
    for (size_t i = 0; i < sizeof unk / sizeof unk[0]; i++) {
        soc_init(&s, chip_raw_id(unk[i].maj, unk[i].min, 0x11), F_ALL, 0, 25, 1);
        int rc = errata_boot(TABLE, TABLE_N, &s, KNOWN, KNOWN_N, unk[i].pol, &c);
        uok += rc == unk[i].rc && c.tainted == unk[i].taint && c.n == unk[i].n;
    }
    CHECK(uok == (int)(sizeof unk / sizeof unk[0]));
    /* HALT 은 클럭을 올리기 전에 멈춘다 */
    soc_init(&s, chip_raw_id(2, 0, 0x11), F_ALL, 0, 25, 1);
    errata_boot(TABLE, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c);
    CHECK(!s.clk_fast && !s.cache_on && c.status == E_UNKNOWN_REV);
    /* 잘못된 파트/버스 불량은 어떤 정책에서도 진행 금지 */
    soc_init(&s, 0x12340011u, F_ALL, 0, 25, 1);
    CHECK(errata_boot(TABLE, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_LAB, &c) == E_WRONG_PART && !c.tainted && c.n == 0);
    soc_init(&s, 0xFFFFFFFFu, F_ALL, 0, 25, 1);
    CHECK(errata_boot(TABLE, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_LAB, &c) == E_BUS && c.n == 0);

    /* 잘못된 phase: 캐시 enable 뒤 L2 chicken → read-back 이 잡는다 */
    copy_table(t);
    tfind(t, TABLE_N, 1002)->phase = PH_LATE;
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_NOT_STUCK && c.fail_id == 1002 && s.late_writes == 1);

    /* 선언 안 한 비트를 건드린 WA */
    copy_table(t);
    tfind(t, TABLE_N, 1001)->apply = wa_wide;
    soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_FOOTPRINT && c.fail_id == 1001);

    /* 실패하는 WA */
    copy_table(t);
    tfind(t, TABLE_N, 1005)->apply = wa_fail;
    soc_init(&s, model_raw(&M_B0), F_ALL, M_B0.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_APPLY && c.fail_id == 1005);

    /* 리포트 문자열 */
    boot_model(&M_A1, F_ALL, &s, &c, POLICY_HALT);
    char line[160];
    ctx_format(line, sizeof line, &c);
    CHECK(strstr(line, "rev=A1") != NULL && strstr(line, "applied=[E1001,E1004]") != NULL);
    printf("boot: %s\n", line);
}

/* ---- Part C ---- */
static probe_t probe_vacuous(soc_t *s, int trials) { (void)s; (void)trials; return (probe_t){ 0, 0 }; }
static int wa_weak(soc_t *s) { rmw32(s, R_DMA_CTRL, DMA_BURST_MASK, 12); return 0; }

static void test_part_c(void) {
    printf("== Part C: ECO removal audit ==\n");
    static const int ALL[] = { -40, 25, 105 };
    static const int ROOM[] = { 25 };
    audit_opts_t o = { ALL, 3, 1024, 0x51u, F_ALL };
    audit_t a;
    memset(&a, 0, sizeof a);

    /* 정적 lint */
    lint_t l;
    CHECK(errata_lint(TABLE, TABLE_N, &l) == 0);
    erratum_t t[MAX_ERRATA];
    copy_table(t);
    tfind(t, TABLE_N, 1002)->id = 1001;                 /* dup */
    tfind(t, TABLE_N, 1003)->rev_min = REV_B0;          /* min>max */
    tfind(t, TABLE_N, 1005)->probe = NULL;              /* 검증 없는 항목 */
    tfind(t, TABLE_N, 1004)->rev_max = REV_B0;          /* OPEN 상태인데 닫힌 범위 */
    CHECK(errata_lint(t, TABLE_N, &l) == 4 && l.dup == 1 && l.bad_range == 1 && l.no_fn == 1 && l.status_mismatch == 1);
    CHECK(l.first_id == 1001);

    /* 기대 판정 매트릭스 — task 의 "A0 에서는 필요, B0 에서는 사라짐" */
    static const struct { uint32_t id; verdict_t a0, a1, b0; bool masked; } mat[] = {
        { 1001, V_NEEDED, V_NEEDED, V_CLEAN, false },
        { 1002, V_NEEDED, V_CLEAN, V_CLEAN, false },
        { 1003, V_NEEDED, V_CLEAN, V_CLEAN, false },
        { 1004, V_NEEDED, V_NEEDED, V_NEEDED, true },
        { 1005, V_CLEAN, V_CLEAN, V_NEEDED, false },
        { 1006, V_NEEDED, V_CLEAN, V_CLEAN, false },
    };
    int mok = 0;
    for (size_t i = 0; i < sizeof mat / sizeof mat[0]; i++) {
        const erratum_t *e = tbl_find(TABLE, TABLE_N, mat[i].id);
        audit_t r0, r1, r2;
        audit_one(TABLE, TABLE_N, e, &M_A0, &o, &r0);
        audit_one(TABLE, TABLE_N, e, &M_A1, &o, &r1);
        audit_one(TABLE, TABLE_N, e, &M_B0, &o, &r2);
        mok += r0.v == mat[i].a0 && r1.v == mat[i].a1 && r2.v == mat[i].b0 &&
               r0.masked == mat[i].masked && r0.on_fail == 0;
    }
    CHECK(mok == (int)(sizeof mat / sizeof mat[0]));

    /* E1001: A0 에서는 버그가 실제로 있고, B0 에서는 정말 없다 — WA OFF 로 측정 */
    const erratum_t *e1001 = tbl_find(TABLE, TABLE_N, 1001);
    audit_one(TABLE, TABLE_N, e1001, &M_A0, &o, &a);
    CHECK(a.off_fail == 3072 && a.on_fail == 0 && a.off_ex == 3072);
    audit_one(TABLE, TABLE_N, e1001, &M_B0, &o, &a);
    CHECK(a.off_fail == 0 && a.off_ex == 3072 && a.v == V_CLEAN);

    /* 잊어버린 narrow: 범위가 B0 까지 남아 있으면 STALE */
    copy_table(t);
    tfind(t, TABLE_N, 1001)->rev_max = REV_B0;
    audit_one(t, TABLE_N, tfind(t, TABLE_N, 1001), &M_B0, &o, &a);
    CHECK(a.v == V_STALE && a.off_fail == 0 && a.on_fail == 0);
    char line[200];
    audit_format(line, sizeof line, tfind(t, TABLE_N, 1001), &M_B0, &a);
    CHECK(strstr(line, "FAIL erratum=E1001") != NULL && strstr(line, "verdict=STALE") != NULL);
    printf("stale : %s\n", line);

    /* B0-ECO: L0s 버그가 고쳐진 B0 → 표는 OPEN 이지만 STALE. WA 켠 쪽만 보면 구분 불가 */
    audit_t eco, buggy;
    const erratum_t *e1004 = tbl_find(TABLE, TABLE_N, 1004);
    audit_one(TABLE, TABLE_N, e1004, &M_B0_ECO, &o, &eco);
    audit_one(TABLE, TABLE_N, e1004, &M_B0, &o, &buggy);
    CHECK(eco.v == V_STALE && buggy.v == V_NEEDED);
    CHECK(eco.on_fail == 0 && buggy.on_fail == 0 && eco.on_ex == 0 && buggy.on_ex == 0);   /* WA on 결과는 동일 */
    CHECK(eco.off_fail == 0 && buggy.off_fail == 3072);                                    /* WA off 만 구별한다 */
    CHECK(eco.masked && buggy.masked);

    /* MISSING: B0 를 A0 DB 에서 분기해 A1 메탈 수정(L2 버그)이 되돌아옴 */
    const erratum_t *e1002 = tbl_find(TABLE, TABLE_N, 1002);
    audit_one(TABLE, TABLE_N, e1002, &M_B0_BRANCH, &o, &a);
    CHECK(a.v == V_MISSING && a.off_fail > 0);
    audit_format(line, sizeof line, e1002, &M_B0_BRANCH, &a);
    CHECK(strstr(line, "verdict=MISSING") != NULL);
    printf("miss  : %s\n", line);

    /* 효과 없는 WA: burst 12 는 여전히 >8 */
    copy_table(t);
    tfind(t, TABLE_N, 1001)->apply = wa_weak;
    tfind(t, TABLE_N, 1001)->val = 12;
    audit_one(t, TABLE_N, tfind(t, TABLE_N, 1001), &M_A0, &o, &a);
    CHECK(a.v == V_WA_BROKEN && a.on_fail == 3072);

    /* 공허한 probe: 트리거를 한 번도 안 건드리면 STALE 라고 말하면 안 된다 */
    copy_table(t);
    tfind(t, TABLE_N, 1001)->probe = probe_vacuous;
    audit_one(t, TABLE_N, tfind(t, TABLE_N, 1001), &M_A0, &o, &a);
    CHECK(a.v == V_INCONCLUSIVE);
    copy_table(t);
    tfind(t, TABLE_N, 1001)->probe = NULL;
    audit_one(t, TABLE_N, tfind(t, TABLE_N, 1001), &M_A0, &o, &a);
    CHECK(a.v == V_NO_PROBE);

    /* limits 1: 코너 누락 — 25C 만 돌리면 A0 의 저온 PLL 버그를 STALE 로 오판한다 */
    audit_opts_t room = { ROOM, 1, 1024, 0x51u, F_ALL };
    const erratum_t *e1003 = tbl_find(TABLE, TABLE_N, 1003);
    audit_one(TABLE, TABLE_N, e1003, &M_A0, &room, &a);
    CHECK(a.v == V_STALE && a.off_fail == 0);
    audit_one(TABLE, TABLE_N, e1003, &M_A0, &o, &a);
    CHECK(a.v == V_NEEDED && a.off_fail == 1024);

    /* limits 2: 간헐 버그 p=1/64. N 번 시도에서 0 번 관측할 확률 = (1-p)^N 과 2000 시드 측정 비교.
     * 허용오차 3 시그마(σ = sqrt(q(1-q)/2000), 약 0.011 → ±0.033 내외). */
    audit_opts_t im = { ROOM, 1, 32, 0, F_ALL };
    double q = 1.0;
    for (int i = 0; i < 32; i++) q *= 63.0 / 64.0;
    int missed = 0;
    for (int i = 0; i < 2000; i++) {
        im.seed = 0x1000u + (uint32_t)i;
        audit_one(TABLE, TABLE_N, e1002, &M_A0, &im, &a);
        missed += a.v == V_STALE;
    }
    double rate = missed / 2000.0;
    double sigma = 0.0;
    { double v = q * (1.0 - q) / 2000.0; sigma = v; for (int i = 0; i < 40; i++) sigma = 0.5 * (sigma + v / sigma); }
    CHECK(rate > q - 3.0 * sigma && rate < q + 3.0 * sigma);
    im.trials = 4096;
    missed = 0;
    for (int i = 0; i < 200; i++) {
        im.seed = 0x3000u + (uint32_t)i;
        audit_one(TABLE, TABLE_N, e1002, &M_A0, &im, &a);
        missed += a.v == V_STALE;
    }
    CHECK(missed == 0);
    printf("limit : p=1/64 N=32 false-STALE %.4f (theory %.4f)\n", rate, q);

    /* ECO 제거: 감사 → 새 rev_max 제안 → 커밋한 테이블에서 A0/A1 유지, B0-ECO 에서 제거, 여전히 깨끗 */
    const chip_model_t *lineup[] = { &M_A0, &M_A1, &M_B0_ECO };
    CHECK(eco_propose_rev_max(TABLE, TABLE_N, e1004, lineup, 3, &o) == REV_A1);
    const chip_model_t *still[] = { &M_A0, &M_A1, &M_B0 };
    CHECK(eco_propose_rev_max(TABLE, TABLE_N, e1004, still, 3, &o) == REV_OPEN);
    CHECK(eco_propose_rev_max(TABLE, TABLE_N, tbl_find(TABLE, TABLE_N, 1005), lineup, 2, &o) == REV_NONE);

    copy_table(t);
    erratum_t *n1004 = tfind(t, TABLE_N, 1004);
    n1004->rev_max = eco_propose_rev_max(TABLE, TABLE_N, e1004, lineup, 3, &o);
    n1004->status = ST_FIXED;
    CHECK(errata_lint(t, TABLE_N, &l) == 0);
    soc_t s;
    errata_ctx_t c;
    memset(&c, 0, sizeof c);
    soc_init(&s, model_raw(&M_B0_ECO), F_ALL, M_B0_ECO.bugs, 25, 1);
    CHECK(errata_boot(t, TABLE_N, &s, KNOWN, KNOWN_N, POLICY_HALT, &c) == E_OK && c.n == 1 && c.applied[0] == 1005);
    CHECK(reg_read32(&s, R_PCIE_PM) == PCIE_L0S_EN);        /* B0-ECO 는 L0s 를 다시 쓸 수 있다 */
    audit_one(t, TABLE_N, n1004, &M_B0_ECO, &o, &a);
    CHECK(a.v == V_CLEAN && a.off_fail == 0 && a.off_ex == 3072);
    audit_one(t, TABLE_N, n1004, &M_A0, &o, &a);
    CHECK(a.v == V_NEEDED);
    /* 그러나 표를 좁힌 뒤 예전 B0(버그 있음)을 다시 돌리면 MISSING 으로 잡혀야 한다 */
    audit_one(t, TABLE_N, n1004, &M_B0, &o, &a);
    CHECK(a.v == V_MISSING && a.off_fail == 3072);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
