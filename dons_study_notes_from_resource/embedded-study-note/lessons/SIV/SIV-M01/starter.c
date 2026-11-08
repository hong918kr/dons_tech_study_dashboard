/* SIV-M01 ATE vs SLT vs 시스템 레벨 검증의 역할
 *
 * Part A — 결함 시나리오 20개를 데이터(결함 종류 + "보려면 필요한 것" 비트마스크)로 적고,
 *          어느 단계가 책임지는지 분류한다: CP(웨이퍼 프로브 ATE) / FT(패키지 ATE) / SLT / SYSVAL(보드·시스템 검증).
 *          제조 결함(일부 유닛)은 "모든 유닛을 테스트하는" screen 단계 중 가장 이른 곳,
 *          설계 결함(모든 유닛)은 샘플 기반 validation 단계가 맡는다. 아무도 못 보면 ESCAPE.
 * Part B — 테스트 시간·비용·커버리지 계산기: 단계별 유닛당 비용, Williams–Brown 결함 수준(DL),
 *          간헐 결함 재현 횟수, 플로우(CP+FT vs CP+FT+SLT)별 출하 DPPM·비용·손익분기 필드 비용.
 * Part C — RX7 센싱 SoC 양산 중 돌아온 RMA(고객 반품) 14건을 분류: STAT_ESCAPE / TEST_HOLE / FLOW_GAP /
 *          NO_SCREEN / VALIDATION_ESCAPE. FT 테스트 프로그램 P1.3 에 심어 둔 구멍(온도 insertion 누락)을 찾아
 *          "FT 에 [TEMP] 복구 — 반품 5건" 같은 실행 가능한 액션으로 묶는다.
 *
 * 숫자(비용·ppm·검출 확률)는 모두 교육용 가정 값이다. 방법이 핵심이고 값은 제품마다 다르다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int near(double a, double b, double rel) { return fabs(a - b) <= rel * fmax(fabs(b), 1e-12); }

/* ======================= Part A: 단계와 결함 ======================= */
enum {                      /* 결함을 "보려면" 필요한 것 = 단계가 가진 능력 */
    C_STRUCT  = 1u << 0,    /* DFT 접근: scan·MBIST·IDDQ·PMU 파라메트릭 (결함 모델 + 커버리지 숫자)  */
    C_ATSPEED = 1u << 1,    /* 실제 클럭 속도의 launch→capture (at-speed scan, 실속도 기능 동작)       */
    C_TEMP    = 1u << 2,    /* hot/cold 온도 insertion                                                 */
    C_PKG     = 1u << 3,    /* 패키지된 칩: 본드/범프/기판, 패키지 전원망                              */
    C_MISSION = 1u << 4,    /* 미션 모드: 실제 FW/OS, 실제 DRAM, 실워크로드의 di/dt                     */
    C_PARTNER = 1u << 5,    /* 실제 상대 장치·보드 채널·고객 보드 구성의 다양성                         */
    C_LONG    = 1u << 6,    /* 수 시간~수일, 수백 번 반복 (드문 간헐 현상)                              */
    C_STRESS  = 1u << 7,    /* 가속 스트레스 (번인, 고전압 스트레스) — 본문 단계에는 없다               */
    C_VOLUME  = 1u << 8,    /* 모든 유닛을 테스트 (per-unit screen)                                     */
};
#define NCAPS 9
static const char *const CAP_NAME[NCAPS] = {
    "STRUCT", "ATSPEED", "TEMP", "PKG", "MISSION", "PARTNER", "LONG", "STRESS", "VOLUME" };

typedef enum { ST_ESCAPE = -1, ST_CP, ST_FT, ST_SLT, ST_SYSVAL, ST_COUNT } stage_t;
typedef enum { K_SCREEN, K_VALIDATION } stage_kind_t;
typedef struct { const char *name; stage_kind_t kind; } stage_info_t;
static const stage_info_t STAGES[ST_COUNT] = {
    { "CP", K_SCREEN }, { "FT", K_SCREEN }, { "SLT", K_SCREEN }, { "SYSVAL", K_VALIDATION } };
/* 단계별 능력 (명목값). Part C 에서는 "실제 테스트 프로그램이 하는 것"으로 바꿔 끼운다 */
static const uint32_t NOMINAL_CAPS[ST_COUNT] = {
    /* CP     */ C_STRUCT | C_VOLUME,                                    /* 모델 단순화: 단일 온도, 저속 */
    /* FT     */ C_STRUCT | C_ATSPEED | C_TEMP | C_PKG | C_VOLUME,
    /* SLT    */ C_ATSPEED | C_TEMP | C_PKG | C_MISSION | C_VOLUME,
    /* SYSVAL */ C_STRUCT | C_ATSPEED | C_TEMP | C_PKG | C_MISSION | C_PARTNER | C_LONG,
};
#define FLOW(s) (1u << (s))
#define FLOW_ALL  (FLOW(ST_CP) | FLOW(ST_FT) | FLOW(ST_SLT) | FLOW(ST_SYSVAL))
#define FLOW_CONS (FLOW(ST_CP) | FLOW(ST_FT) | FLOW(ST_SYSVAL))      /* SLT 없는 플로우 */

typedef enum { DC_MFG, DC_MARGINAL, DC_DESIGN } dclass_t;
static const char *const DCLASS_NAME[3] = { "MFG", "MARGINAL", "DESIGN" };
typedef struct {
    const char *tag, *desc;
    dclass_t cls;
    uint32_t needs;     /* 보려면 필요한 능력 (VOLUME 은 cls 로부터 자동으로 붙는다) */
    double ppm;         /* 투입 다이 100만 개당 이 결함을 가진 개수 (설계 결함은 0: DPPM 이 아니다) */
    double p_obs;       /* 책임 단계 테스트 1회가 이 결함을 잡을 확률 (커버리지 / 간헐성) */
} defect_t;
enum { D_SA0, D_BRIDGE, D_SRAM_HARD, D_TDF, D_BOND, D_SRAM_HOT, D_VMIN, D_PLL_COLD, D_SERDES_LB,
       D_PDN_DROOP, D_SCAN_HOLE, D_DDR_IF, D_OS_HANG, D_OXIDE, D_LINK_RARE,
       D_PCIE_EQ, D_ROM_ERRATA, D_COHERENCY, D_THERM_CAL, D_PWR_SEQ, NDEF };
static const defect_t DEFECTS[NDEF] = {
    { "SA0",       "ALU NAND 출력 stuck-at-0",                 DC_MFG,      C_STRUCT,                       50000, 0.999  },
    { "BRIDGE",    "인접 메탈 배선 short(bridging)",           DC_MFG,      C_STRUCT,                       30000, 0.998  },
    { "SRAM_HARD", "SRAM 셀 하드 결함 (MBIST→repair)",         DC_MFG,      C_STRUCT,                       20000, 0.9999 },
    { "TDF",       "저항성 via → 작은 지연 결함",              DC_MFG,      C_STRUCT | C_ATSPEED,            3000, 0.98   },
    { "BOND",      "와이어본드/범프 open (조립)",              DC_MFG,      C_STRUCT | C_PKG,                2000, 0.9995 },
    { "SRAM_HOT",  "고온에서만 실패하는 SRAM 약한 셀",         DC_MFG,      C_STRUCT | C_TEMP,                800, 0.99   },
    { "VMIN",      "느린 유닛 critical path 마진 부족(Vmin)",  DC_MARGINAL, C_ATSPEED | C_TEMP,              1500, 0.99   },
    { "PLL_COLD",  "저온 PLL lock 실패 (일부 유닛)",           DC_MARGINAL, C_TEMP | C_PKG,                   300, 0.99   },
    { "SERDES_LB", "SerDes TX jitter 큰 유닛 (loopback BER)",  DC_MARGINAL, C_ATSPEED | C_PKG,                400, 0.98   },
    { "PDN_DROOP", "기판 decap 누락 → 실워크로드 droop",       DC_MFG,      C_ATSPEED | C_PKG | C_MISSION,    100, 0.90   },
    { "SCAN_HOLE", "scan 불가 경로(비동기 경계)의 지연 결함",  DC_MFG,      C_ATSPEED | C_MISSION,            150, 0.80   },
    { "DDR_IF",    "DDR PHY 마진 부족: 실제 DRAM training 실패", DC_MARGINAL, C_ATSPEED | C_PKG | C_MISSION,  120, 0.95   },
    { "OS_HANG",   "hot 멀티코어 부하에서만 hang (상호작용)",  DC_MFG,      C_ATSPEED | C_TEMP | C_MISSION,    80, 0.75   },
    { "OXIDE",     "게이트 산화막 약점 → 초기 고장",           DC_MFG,      C_STRESS,                          30, 0.95   },
    { "LINK_RARE", "몇 시간에 한 번 PCIe 링크 down (일부 유닛)", DC_MFG,    C_MISSION | C_LONG,                 5, 0.50   },
    { "PCIE_EQ",   "특정 EP 와 Gen4 EQ interop 실패 (1/50 부팅)", DC_DESIGN, C_MISSION | C_PARTNER | C_LONG,     0, 0.02   },
    { "ROM_ERRATA","Boot ROM 이 QSPI timeout 후 재시도 안 함", DC_DESIGN,   C_MISSION,                          0, 1.0    },
    { "COHERENCY", "특정 워크로드에서 캐시 일관성 deadlock",   DC_DESIGN,   C_MISSION | C_LONG,                 0, 0.01   },
    { "THERM_CAL", "온도 센서 오프셋 → throttle 지연",         DC_DESIGN,   C_TEMP | C_MISSION,                 0, 1.0    },
    { "PWR_SEQ",   "고객 보드 PMIC 순서(IO 먼저) → POR 글리치", DC_DESIGN,  C_PARTNER,                          0, 1.0    },
};

/* A1: 실제로 필요한 능력. 제조·마진 결함은 일부 유닛에만 있으므로 모든 유닛을 봐야 한다(+VOLUME) */
static uint32_t eff_needs(const defect_t *d) {
    (void)d;
    return 0; /* TODO: 설계 결함은 needs 그대로, 제조·마진 결함은 needs | C_VOLUME */
}
/* A2: flow 에 있는 단계 중, 결함 종류에 맞는 kind(제조→SCREEN, 설계→VALIDATION)이고
 *     caps ⊇ eff_needs 인 가장 이른 단계. 없으면 ST_ESCAPE */
static stage_t classify(const defect_t *d, const uint32_t caps[ST_COUNT], uint32_t flow) {
    (void)d; (void)caps; (void)flow;
    return ST_ESCAPE; /* TODO: flow 에 있고 kind 가 맞는 단계를 앞에서부터, caps ⊇ eff_needs 인 첫 단계 */
}
/* A3: 단계 s 가 이 결함을 못 보는 이유 = 필요하지만 s 에 없는 능력 */
static uint32_t why_not(stage_t s, const defect_t *d, const uint32_t caps[ST_COUNT]) {
    (void)s; (void)d; (void)caps;
    return 0; /* TODO */
}
/* A4: 명목 능력·전체 플로우로 단계별 개수를 hist[] 에, ESCAPE 개수를 반환 */
static int stage_histogram(const defect_t *ds, int n, int hist[ST_COUNT]) {
    (void)ds; (void)n;
    for (int s = 0; s < ST_COUNT; s++) hist[s] = 0;
    return 0; /* TODO: classify(…, NOMINAL_CAPS, FLOW_ALL) 로 세고 ESCAPE 개수를 돌려준다 */
}

static const char *caps_str(uint32_t m, char *buf, size_t cap) {
    size_t len = 0; buf[0] = '\0';
    len += (size_t)snprintf(buf + len, cap - len, "[");
    for (int b = 0, first = 1; b < NCAPS && len < cap; b++)
        if (m & (1u << b)) { len += (size_t)snprintf(buf + len, cap - len, "%s%s", first ? "" : ",", CAP_NAME[b]); first = 0; }
    if (len < cap) snprintf(buf + len, cap - len, "]");
    return buf;
}
static const char *stage_name(stage_t s) { return s == ST_ESCAPE ? "ESCAPE" : STAGES[s].name; }

/* ======================= Part B: 시간·비용·커버리지 계산기 ======================= */
typedef struct { double rate_per_hr; int sites; double secs; } econ_t;   /* 장비 시간당 비용, 동시 site 수, 유닛당 시간 */
static const econ_t ECON[ST_COUNT] = {        /* 가정 값 (교육용) */
    { 100.0,  16,   3.0 },                    /* CP : 프로버+테스터, 16 site, 3 s       */
    { 150.0,   8,   6.0 },                    /* FT : 핸들러+테스터, 8 site, 6 s (온도 포함) */
    { 400.0, 128, 600.0 },                    /* SLT: 128 슬롯 랙, 10 분 부팅+워크로드    */
    {   0.0,   1,   0.0 },                    /* SYSVAL: 유닛당 비용이 아니라 프로그램 비용 */
};
/* B1: 유닛 1개당 테스트 비용 ($) */
static double unit_cost(const econ_t *e) {
    (void)e;
    return 0.0; /* TODO */
}
/* B2: Williams–Brown 결함 수준 DL = 1 - Y^(1-T)   (Y: 수율, T: 결함 커버리지) */
static double defect_level(double Y, double T) {
    (void)Y; (void)T;
    return 0.0; /* TODO */
}
/* B3: 목표 DL 을 얻기 위한 커버리지 T = 1 - ln(1-DL)/ln(Y) */
static double coverage_for_dl(double Y, double DL) {
    (void)Y; (void)DL;
    return 0.0; /* TODO */
}
/* B4: 1회 관측 확률 p 인 간헐 결함을 k 번 돌려 한 번 이상 볼 확률 */
static double p_detect_runs(double p, int k) {
    (void)p; (void)k;
    return 0.0; /* TODO */
}
/* B5: 신뢰도 conf 로 한 번 이상 보려면 필요한 최소 반복 수. 불가능하면 -1 */
static int runs_needed(double p, double conf) {
    (void)p; (void)conf;
    return -1; /* TODO: 입력 검사 → p>=1 이면 1 → ceil(ln(1-conf)/ln(1-p)) (부동소수 경계 주의) */
}

typedef struct {
    double start, entered[ST_COUNT], caught[ST_COUNT];
    double shipped, escaped, yield, dppm;
    double test_cost_per_ship, escape_cost_per_ship, total_per_ship;
} flow_result_t;
/* B6: 투입 100만 다이 기준 기댓값 계산. 결함 하나는 자기 책임 단계에서만 p_obs 로 잡힌다
 *     (커버리지 구멍은 결정적이라 같은 종류 테스트를 반복해도 같은 결함을 놓친다는 보수적 가정).
 *     설계 결함은 DPPM 에 넣지 않는다. 유닛당 결함은 최대 1개(낮은 결함률 근사). */
static void flow_eval(const defect_t *ds, int n, const uint32_t caps[ST_COUNT], uint32_t flow,
                      double field_cost, flow_result_t *r) {
    (void)ds; (void)n; (void)caps; (void)flow; (void)field_cost;
    memset(r, 0, sizeof *r);
    r->start = 1e6;
    /* TODO: ① 결함마다 책임 단계에서 caught/escaped 누적 (설계 결함 제외, ESCAPE 면 전부 유출)
     *       ② flow 의 screen 단계를 순서대로: entered = 남은 수, 비용 += 남은 수 × unit_cost, 남은 수 -= caught
     *       ③ shipped, yield, dppm, 유닛당 비용들 */
}
/* B7: b(스크린 추가)가 a 보다 이득이 되는 필드 고장 1건당 비용. DPPM 이 줄지 않으면 -1 */
static double breakeven_field_cost(const flow_result_t *a, const flow_result_t *b) {
    (void)a; (void)b;
    return 0.0; /* TODO */
}

/* ======================= Part C: RMA triage ======================= */
typedef enum { RV_STAT_ESCAPE, RV_TEST_HOLE, RV_FLOW_GAP, RV_NO_SCREEN, RV_VALIDATION_ESCAPE, RV_COUNT } rverdict_t;
static const char *const RV_NAME[RV_COUNT] = {
    "STAT_ESCAPE", "TEST_HOLE", "FLOW_GAP", "NO_SCREEN", "VALIDATION_ESCAPE" };
typedef struct { rverdict_t v; stage_t stage; uint32_t missing; } rma_result_t;
typedef struct { const char *id; int defect; } rma_t;   /* defect = 고장 분석(FA)이 밝힌 원인 */

/* C1: 반품 1건 판정.
 *   설계 결함                         → VALIDATION_ESCAPE (stage = 책임 validation 단계)
 *   어떤 단계도 못 봄(전체 카탈로그)  → NO_SCREEN
 *   볼 수 있는 단계가 이 플로우에 없음 → FLOW_GAP (stage = 카탈로그상 책임 단계)
 *   실제 프로그램이 책임 단계에서 능력을 잃음 → TEST_HOLE (missing = 잃어버린 필요 능력)
 *   책임 단계가 제대로 돌았는데 놓침  → STAT_ESCAPE (커버리지·간헐성 < 100%) */
static rma_result_t rma_verdict(const defect_t *d, const uint32_t actual[ST_COUNT], uint32_t flow) {
    rma_result_t r = { RV_STAT_ESCAPE, ST_ESCAPE, 0 };
    (void)d; (void)actual; (void)flow;
    return r; /* TODO: 위 순서대로 판정 (카탈로그 → 이 플로우의 명목 → 실제 프로그램) */
}

typedef struct { stage_t stage; uint32_t missing; int count; int per_defect[NDEF]; } hole_t;
/* C2: 반품 목록 → 판정별 개수(counts) + TEST_HOLE 을 (단계, 잃은 능력)으로 묶은 hole 목록. 반환 = hole 개수 */
static int rma_holes(const rma_t *rmas, int n, const uint32_t actual[ST_COUNT], uint32_t flow,
                     int counts[RV_COUNT], hole_t *holes, int max_holes) {
    int nh = 0;
    for (int v = 0; v < RV_COUNT; v++) counts[v] = 0;
    (void)rmas; (void)n; (void)actual; (void)flow; (void)holes; (void)max_holes;
    return nh; /* TODO: 판정 개수 세기 + TEST_HOLE 을 (stage, missing) 키로 묶고 결함별 개수 */
}

/* ======================= 출력 (주어짐) ======================= */
static void print_classification(void) {
    char b1[96], b2[96];
    printf("== Part A: 20 defect scenarios -> owner stage\n");
    for (int i = 0; i < NDEF; i++) {
        const defect_t *d = &DEFECTS[i];
        stage_t s = classify(d, NOMINAL_CAPS, FLOW_ALL);
        uint32_t ft_gap = why_not(ST_FT, d, NOMINAL_CAPS);
        printf("  %-10s %-8s needs=%-28s -> %-6s  FT-missing=%s\n", d->tag, DCLASS_NAME[d->cls],
               caps_str(eff_needs(d), b1, sizeof b1), stage_name(s), caps_str(ft_gap, b2, sizeof b2));
    }
}
static void print_flow(const char *name, const flow_result_t *r) {
    printf("  %-12s yield=%.4f%%  DPPM=%7.1f  test$/unit=%.4f  escape$/unit=%.4f  total=%.4f  (CP out %.0f, FT out %.0f, SLT out %.0f)\n",
           name, r->yield * 100.0, r->dppm, r->test_cost_per_ship, r->escape_cost_per_ship, r->total_per_ship,
           r->caught[ST_CP], r->caught[ST_FT], r->caught[ST_SLT]);
}
static void print_rma(const char *title, const rma_t *rmas, int n, const uint32_t actual[ST_COUNT], uint32_t flow) {
    char b[96];
    int counts[RV_COUNT]; hole_t holes[8];
    printf("== %s\n", title);
    for (int i = 0; i < n; i++) {
        rma_result_t r = rma_verdict(&DEFECTS[rmas[i].defect], actual, flow);
        printf("  %-9s %-10s %-17s %-6s", rmas[i].id, DEFECTS[rmas[i].defect].tag, RV_NAME[r.v], stage_name(r.stage));
        if (r.v == RV_TEST_HOLE) printf(" missing=%s", caps_str(r.missing, b, sizeof b));
        printf("\n");
    }
    int nh = rma_holes(rmas, n, actual, flow, counts, holes, 8);
    printf("  summary:");
    for (int v = 0; v < RV_COUNT; v++) printf(" %s=%d", RV_NAME[v], counts[v]);
    printf("\n");
    for (int h = 0; h < nh; h++) {
        printf("  ACTION %s: restore %s in test program -> %d returns (", STAGES[holes[h].stage].name,
               caps_str(holes[h].missing, b, sizeof b), holes[h].count);
        for (int i = 0, first = 1; i < NDEF; i++)
            if (holes[h].per_defect[i]) { printf("%s%s x%d", first ? "" : ", ", DEFECTS[i].tag, holes[h].per_defect[i]); first = 0; }
        printf(")\n");
    }
}

int main(void) {
    /* ---- Part A ---- */
    static const stage_t EXP[NDEF] = {
        ST_CP, ST_CP, ST_CP,                                  /* SA0 BRIDGE SRAM_HARD           */
        ST_FT, ST_FT, ST_FT, ST_FT, ST_FT, ST_FT,             /* TDF BOND SRAM_HOT VMIN PLL SERDES */
        ST_SLT, ST_SLT, ST_SLT, ST_SLT,                       /* PDN SCAN_HOLE DDR_IF OS_HANG    */
        ST_ESCAPE, ST_ESCAPE,                                 /* OXIDE LINK_RARE                */
        ST_SYSVAL, ST_SYSVAL, ST_SYSVAL, ST_SYSVAL, ST_SYSVAL /* 설계 결함 5개                  */
    };
    int a_ok = 0;
    for (int i = 0; i < NDEF; i++) {
        stage_t s = classify(&DEFECTS[i], NOMINAL_CAPS, FLOW_ALL);
        if (s == EXP[i]) a_ok++;
        else printf("  mismatch %-10s got=%s exp=%s\n", DEFECTS[i].tag, stage_name(s), stage_name(EXP[i]));
    }
    CHECK(a_ok == NDEF);
    CHECK(eff_needs(&DEFECTS[D_SA0]) == (C_STRUCT | C_VOLUME));
    CHECK(eff_needs(&DEFECTS[D_ROM_ERRATA]) == C_MISSION);              /* 설계 결함: VOLUME 불필요 */
    CHECK(why_not(ST_CP, &DEFECTS[D_TDF], NOMINAL_CAPS) == C_ATSPEED);
    CHECK(why_not(ST_FT, &DEFECTS[D_PDN_DROOP], NOMINAL_CAPS) == C_MISSION);
    CHECK(why_not(ST_SLT, &DEFECTS[D_SA0], NOMINAL_CAPS) == C_STRUCT);  /* SLT 에는 스캔이 없다 */
    CHECK(why_not(ST_SLT, &DEFECTS[D_LINK_RARE], NOMINAL_CAPS) == C_LONG);
    CHECK(why_not(ST_SYSVAL, &DEFECTS[D_LINK_RARE], NOMINAL_CAPS) == C_VOLUME);
    CHECK(why_not(ST_SLT, &DEFECTS[D_ROM_ERRATA], NOMINAL_CAPS) == 0);  /* SLT 도 볼 수는 있다 — kind 가 다르다 */
    CHECK(classify(&DEFECTS[D_PDN_DROOP], NOMINAL_CAPS, FLOW_CONS) == ST_ESCAPE);
    CHECK(classify(&DEFECTS[D_SA0], NOMINAL_CAPS, FLOW(ST_FT)) == ST_FT);   /* CP 를 건너뛰면 FT 가 맡는다 */
    CHECK(classify(&DEFECTS[D_ROM_ERRATA], NOMINAL_CAPS, FLOW(ST_CP) | FLOW(ST_FT) | FLOW(ST_SLT)) == ST_ESCAPE);
    {
        defect_t rare_design = { "X", "x", DC_DESIGN, C_STRESS, 0, 1.0 };
        CHECK(classify(&rare_design, NOMINAL_CAPS, FLOW_ALL) == ST_ESCAPE);
        int hist[ST_COUNT];
        CHECK(stage_histogram(DEFECTS, NDEF, hist) == 2);
        CHECK(hist[ST_CP] == 3 && hist[ST_FT] == 6 && hist[ST_SLT] == 4 && hist[ST_SYSVAL] == 5);
    }

    /* ---- Part B ---- */
    CHECK(near(unit_cost(&ECON[ST_CP]), 100.0 / 3600.0 * 3.0 / 16.0, 1e-12));
    CHECK(near(unit_cost(&ECON[ST_FT]), 0.03125, 1e-12));
    CHECK(near(unit_cost(&ECON[ST_SLT]), 400.0 / 3600.0 * 600.0 / 128.0, 1e-12));
    CHECK(near(defect_level(0.9, 0.99), 1.0 - exp(0.01 * log(0.9)), 1e-12));
    CHECK(near(defect_level(0.9, 0.99) * 1e6, 1053.0, 1e-3));          /* ≈ 1053 DPPM */
    CHECK(defect_level(0.9, 1.0) == 0.0 && near(defect_level(0.9, 0.0), 0.1, 1e-12));
    CHECK(near(coverage_for_dl(0.9, 100e-6), 0.999051, 1e-6));
    CHECK(near(defect_level(0.9, coverage_for_dl(0.9, 100e-6)), 100e-6, 1e-9));
    CHECK(p_detect_runs(0.02, 149) >= 0.95 && p_detect_runs(0.02, 148) < 0.95);
    CHECK(runs_needed(0.02, 0.95) == 149);
    CHECK(runs_needed(0.5, 0.75) == 2 && runs_needed(0.2, 0.36) == 2);  /* 0.8^2 = 0.64: 몫이 2.0000000000000004 */
    CHECK(runs_needed(1.0, 0.99) == 1 && runs_needed(0.0, 0.95) == -1 && runs_needed(0.1, 1.0) == -1);
    {
        defect_t one = { "ONE", "one", DC_MFG, C_STRUCT | C_ATSPEED, 1000, 0.9 };
        flow_result_t r;
        flow_eval(&one, 1, NOMINAL_CAPS, FLOW(ST_FT), 0.0, &r);
        CHECK(near(r.caught[ST_FT], 900.0, 1e-12) && near(r.escaped, 100.0, 1e-12));
        CHECK(near(r.dppm, 100.0 / (1e6 - 900.0) * 1e6, 1e-12));
        CHECK(near(r.test_cost_per_ship, 1e6 * 0.03125 / (1e6 - 900.0), 1e-12));
        flow_eval(&one, 1, NOMINAL_CAPS, FLOW(ST_CP), 0.0, &r);           /* CP 만: 보지 못해 전부 유출 */
        CHECK(near(r.escaped, 1000.0, 1e-12) && r.caught[ST_CP] == 0.0);
    }
    flow_result_t cons_c, auto_c, cons_a, auto_a;
    flow_eval(DEFECTS, NDEF, NOMINAL_CAPS, FLOW_CONS, 30.0, &cons_c);     /* 소비자: 필드 고장 1건 $30   */
    flow_eval(DEFECTS, NDEF, NOMINAL_CAPS, FLOW_ALL, 30.0, &auto_c);
    flow_eval(DEFECTS, NDEF, NOMINAL_CAPS, FLOW_CONS, 5000.0, &cons_a);   /* 차량용: 필드 고장 1건 $5000 */
    flow_eval(DEFECTS, NDEF, NOMINAL_CAPS, FLOW_ALL, 5000.0, &auto_a);
    CHECK(near(cons_c.dppm, 775.6048, 1e-6) && near(auto_c.dppm, 345.3600, 1e-6));
    CHECK(near(cons_c.caught[ST_CP], 99888.0, 1e-9) && near(auto_c.caught[ST_SLT], 384.0, 1e-9));
    CHECK(near(breakeven_field_cost(&cons_c, &auto_c), 1211.11, 1e-5));
    CHECK(cons_c.total_per_ship < auto_c.total_per_ship);               /* $30 이면 SLT 는 손해 */
    CHECK(auto_a.total_per_ship < cons_a.total_per_ship);               /* $5000 이면 SLT 가 이득 */
    CHECK(breakeven_field_cost(&auto_c, &cons_c) == -1.0);

    /* ---- Part C: RX7 양산, 플로우 CP+FT+SLT, FT 프로그램 P1.3 (온도 insertion 누락이 심어져 있다) ---- */
    static const rma_t RMAS[] = {
        { "RMA-2601", D_SRAM_HOT }, { "RMA-2602", D_SA0 },       { "RMA-2603", D_PCIE_EQ },
        { "RMA-2604", D_SRAM_HOT }, { "RMA-2605", D_OXIDE },     { "RMA-2606", D_PLL_COLD },
        { "RMA-2607", D_PDN_DROOP },{ "RMA-2608", D_VMIN },      { "RMA-2609", D_ROM_ERRATA },
        { "RMA-2610", D_LINK_RARE },{ "RMA-2611", D_SRAM_HOT },  { "RMA-2612", D_TDF },
        { "RMA-2613", D_BRIDGE },   { "RMA-2614", D_SERDES_LB },
    };
    enum { NRMA = (int)(sizeof RMAS / sizeof RMAS[0]) };
    uint32_t p13[ST_COUNT];
    memcpy(p13, NOMINAL_CAPS, sizeof p13);
    p13[ST_FT] &= ~C_TEMP;                                              /* 심어 둔 결함: hot/cold insertion 빠짐 */
    {
        static const rverdict_t EXPV[NRMA] = {
            RV_TEST_HOLE, RV_STAT_ESCAPE, RV_VALIDATION_ESCAPE, RV_TEST_HOLE, RV_NO_SCREEN, RV_TEST_HOLE,
            RV_STAT_ESCAPE, RV_TEST_HOLE, RV_VALIDATION_ESCAPE, RV_NO_SCREEN, RV_TEST_HOLE, RV_STAT_ESCAPE,
            RV_STAT_ESCAPE, RV_STAT_ESCAPE };
        int c_ok = 0;
        for (int i = 0; i < NRMA; i++) c_ok += rma_verdict(&DEFECTS[RMAS[i].defect], p13, FLOW_ALL).v == EXPV[i];
        CHECK(c_ok == NRMA);
        rma_result_t r = rma_verdict(&DEFECTS[D_VMIN], p13, FLOW_ALL);
        CHECK(r.stage == ST_FT && r.missing == C_TEMP);                 /* VMIN 은 SLT 로 밀렸지만 책임은 FT */
        r = rma_verdict(&DEFECTS[D_PDN_DROOP], NOMINAL_CAPS, FLOW_CONS);
        CHECK(r.v == RV_FLOW_GAP && r.stage == ST_SLT);                 /* SLT 없는 플로우의 구멍 */
        r = rma_verdict(&DEFECTS[D_PCIE_EQ], p13, FLOW_ALL);
        CHECK(r.stage == ST_SYSVAL);

        int counts[RV_COUNT]; hole_t holes[8];
        int nh = rma_holes(RMAS, NRMA, p13, FLOW_ALL, counts, holes, 8);
        CHECK(nh == 1 && holes[0].stage == ST_FT && holes[0].missing == C_TEMP && holes[0].count == 5);
        CHECK(nh >= 1 && holes[0].per_defect[D_SRAM_HOT] == 3 && holes[0].per_defect[D_PLL_COLD] == 1 &&
              holes[0].per_defect[D_VMIN] == 1);
        CHECK(counts[RV_STAT_ESCAPE] == 5 && counts[RV_NO_SCREEN] == 2 && counts[RV_VALIDATION_ESCAPE] == 2);
        nh = rma_holes(RMAS, NRMA, NOMINAL_CAPS, FLOW_ALL, counts, holes, 8);   /* 정상 프로그램: 구멍 없음 */
        CHECK(nh == 0 && counts[RV_TEST_HOLE] == 0 && counts[RV_STAT_ESCAPE] == 10);
        uint32_t p14[ST_COUNT];
        memcpy(p14, NOMINAL_CAPS, sizeof p14);
        p14[ST_SLT] &= ~C_MISSION;                                       /* 두 번째 결함: SLT 가 부팅만 하고 워크로드 생략 */
        nh = rma_holes(RMAS, NRMA, p14, FLOW_ALL, counts, holes, 8);
        CHECK(nh == 1 && holes[0].stage == ST_SLT && holes[0].missing == C_MISSION && holes[0].count == 1);
    }

    print_classification();
    printf("== Part B: flow comparison (per 1e6 dies started)\n");
    print_flow("CP+FT  $30", &cons_c);  print_flow("+SLT   $30", &auto_c);
    print_flow("CP+FT  $5k", &cons_a);  print_flow("+SLT   $5k", &auto_a);
    printf("  SLT break-even field cost = $%.1f per failure\n", breakeven_field_cost(&cons_c, &auto_c));
    printf("  DL(Y=0.90,T=0.99)=%.1f DPPM  T for 100 DPPM=%.6f  runs for p=1/50 @95%% = %d\n",
           defect_level(0.9, 0.99) * 1e6, coverage_for_dl(0.9, 100e-6), runs_needed(0.02, 0.95));
    print_rma("RMA triage: RX7, flow CP+FT+SLT, FT program P1.3", RMAS, NRMA, p13, FLOW_ALL);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
