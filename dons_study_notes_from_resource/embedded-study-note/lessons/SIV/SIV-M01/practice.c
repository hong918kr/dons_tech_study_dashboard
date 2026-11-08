/* SIV-M01 추가 연습 — 한 번에 검증
 *
 * 연습 1: 번인(BI) 단계를 추가하면 OXIDE 가 ESCAPE → BI 로 옮겨 간다. 비용과 손익분기 필드 비용은?
 * 연습 2: 목표 DPPM 에 필요한 커버리지는 수율에 따라 어떻게 변하나 (Williams–Brown 역함수)
 * 연습 3: 멀티 사이트 효율(MSE)과 유닛당 테스트 시간, 목표 시간을 맞추는 최소 site 수
 * 연습 4: SLT 를 몇 초까지 늘려도 이득인가 — 단조 predicate 에 이진 탐색
 *
 * 본문 solution.c 의 모델을 단계 표를 데이터로 받는 일반형으로 축약해 넣었다 (단계 수가 바뀌므로).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int near(double a, double b, double rel) { return fabs(a - b) <= rel * fmax(fabs(b), 1e-12); }

enum { C_STRUCT = 1u << 0, C_ATSPEED = 1u << 1, C_TEMP = 1u << 2, C_PKG = 1u << 3, C_MISSION = 1u << 4,
       C_PARTNER = 1u << 5, C_LONG = 1u << 6, C_STRESS = 1u << 7, C_VOLUME = 1u << 8 };
typedef struct { double rate_per_hr; int sites; double secs; } econ_t;
typedef struct { const char *name; int screen; uint32_t caps; econ_t econ; } stage_t;
typedef enum { DC_MFG, DC_MARGINAL, DC_DESIGN } dclass_t;
typedef struct { const char *tag; dclass_t cls; uint32_t needs; double ppm, p_obs; } defect_t;

#define NDEF 20
static const defect_t DEFECTS[NDEF] = {     /* solution.c 와 같은 값 (설명 문자열만 뺐다) */
    { "SA0", DC_MFG, C_STRUCT, 50000, 0.999 },          { "BRIDGE", DC_MFG, C_STRUCT, 30000, 0.998 },
    { "SRAM_HARD", DC_MFG, C_STRUCT, 20000, 0.9999 },   { "TDF", DC_MFG, C_STRUCT | C_ATSPEED, 3000, 0.98 },
    { "BOND", DC_MFG, C_STRUCT | C_PKG, 2000, 0.9995 }, { "SRAM_HOT", DC_MFG, C_STRUCT | C_TEMP, 800, 0.99 },
    { "VMIN", DC_MARGINAL, C_ATSPEED | C_TEMP, 1500, 0.99 }, { "PLL_COLD", DC_MARGINAL, C_TEMP | C_PKG, 300, 0.99 },
    { "SERDES_LB", DC_MARGINAL, C_ATSPEED | C_PKG, 400, 0.98 },
    { "PDN_DROOP", DC_MFG, C_ATSPEED | C_PKG | C_MISSION, 100, 0.90 },
    { "SCAN_HOLE", DC_MFG, C_ATSPEED | C_MISSION, 150, 0.80 },
    { "DDR_IF", DC_MARGINAL, C_ATSPEED | C_PKG | C_MISSION, 120, 0.95 },
    { "OS_HANG", DC_MFG, C_ATSPEED | C_TEMP | C_MISSION, 80, 0.75 },
    { "OXIDE", DC_MFG, C_STRESS, 30, 0.95 },            { "LINK_RARE", DC_MFG, C_MISSION | C_LONG, 5, 0.50 },
    { "PCIE_EQ", DC_DESIGN, C_MISSION | C_PARTNER | C_LONG, 0, 0.02 }, { "ROM_ERRATA", DC_DESIGN, C_MISSION, 0, 1.0 },
    { "COHERENCY", DC_DESIGN, C_MISSION | C_LONG, 0, 0.01 },  { "THERM_CAL", DC_DESIGN, C_TEMP | C_MISSION, 0, 1.0 },
    { "PWR_SEQ", DC_DESIGN, C_PARTNER, 0, 1.0 },
};
enum { I_OXIDE = 13 };

/* ---- 본문 모델의 일반형: 단계 표를 인자로 ---- */
static uint32_t eff_needs(const defect_t *d) { return d->cls == DC_DESIGN ? d->needs : (d->needs | C_VOLUME); }
static int classify(const defect_t *d, const stage_t *st, int ns, uint32_t flow) {
    uint32_t need = eff_needs(d);
    int want_screen = d->cls != DC_DESIGN;
    for (int s = 0; s < ns; s++) {
        if (!(flow & (1u << s)) || st[s].screen != want_screen) continue;
        if ((st[s].caps & need) == need) return s;
    }
    return -1;
}
static double unit_cost(const econ_t *e) { return e->rate_per_hr / 3600.0 * e->secs / e->sites; }
typedef struct { double shipped, escaped, dppm, test_per_ship, total_per_ship; } flow_result_t;
static void flow_eval(const stage_t *st, int ns, uint32_t flow, double field_cost, flow_result_t *r) {
    double caught[8] = { 0 };
    memset(r, 0, sizeof *r);
    for (int i = 0; i < NDEF; i++) {
        if (DEFECTS[i].cls == DC_DESIGN) continue;
        int s = classify(&DEFECTS[i], st, ns, flow);
        if (s < 0) { r->escaped += DEFECTS[i].ppm; continue; }
        caught[s] += DEFECTS[i].ppm * DEFECTS[i].p_obs;
        r->escaped += DEFECTS[i].ppm * (1.0 - DEFECTS[i].p_obs);
    }
    double remaining = 1e6, cost = 0.0;
    for (int s = 0; s < ns; s++) {
        if (!(flow & (1u << s)) || !st[s].screen) continue;
        cost += remaining * unit_cost(&st[s].econ);
        remaining -= caught[s];
    }
    r->shipped = remaining;
    r->dppm = r->escaped / remaining * 1e6;
    r->test_per_ship = cost / remaining;
    r->total_per_ship = r->test_per_ship + r->dppm / 1e6 * field_cost;
}
static double breakeven_field_cost(const flow_result_t *a, const flow_result_t *b) {
    double d = a->dppm - b->dppm;
    return d <= 0.0 ? -1.0 : (b->test_per_ship - a->test_per_ship) / (d / 1e6);
}

/* 연습 1: CP, FT, BI, SLT, SYSVAL */
enum { S_CP, S_FT, S_BI, S_SLT, S_SYSVAL, NS };
static stage_t STAGES5[NS] = {
    { "CP",  1, C_STRUCT | C_VOLUME,                                  { 100.0,  16,     3.0 } },
    { "FT",  1, C_STRUCT | C_ATSPEED | C_TEMP | C_PKG | C_VOLUME,      { 150.0,   8,     6.0 } },
    { "BI",  1, C_STRESS | C_TEMP | C_PKG | C_VOLUME,                  {  50.0, 512, 28800.0 } },  /* 8 h, 512 소켓 보드 */
    { "SLT", 1, C_ATSPEED | C_TEMP | C_PKG | C_MISSION | C_VOLUME,     { 400.0, 128,   600.0 } },
    { "SYSVAL", 0, C_STRUCT | C_ATSPEED | C_TEMP | C_PKG | C_MISSION | C_PARTNER | C_LONG, { 0.0, 1, 0.0 } },
};
#define F(s) (1u << (s))

/* 연습 2 */
static double coverage_for_dl(double Y, double DL) { return 1.0 - log(1.0 - DL) / log(Y); }

/* 연습 3: MSE = 1 - (T_N - T_1) / ((N-1) T_1)  →  T_N = T_1 (1 + (N-1)(1-MSE)) */
static double per_unit_secs(double t1, int n, double mse) { return t1 * (1.0 + (n - 1) * (1.0 - mse)) / n; }
static int min_sites(double t1, double mse, double target, int nmax) {
    for (int n = 1; n <= nmax; n++)
        if (per_unit_secs(t1, n, mse) <= target + 1e-12) return n;   /* 정확히 같은 경계를 놓치지 않게 */
    return -1;
}

/* 연습 4: SLT 시간 t 에서 (CP+FT+SLT 총비용) < (CP+FT 총비용) 인가 — t 에 대해 단조 */
static int slt_pays(double t, double field_cost) {
    flow_result_t a, b;
    stage_t st[NS];
    memcpy(st, STAGES5, sizeof st);
    st[S_SLT].econ.secs = t;
    flow_eval(st, NS, F(S_CP) | F(S_FT) | F(S_SYSVAL), field_cost, &a);
    flow_eval(st, NS, F(S_CP) | F(S_FT) | F(S_SLT) | F(S_SYSVAL), field_cost, &b);
    return b.total_per_ship < a.total_per_ship;
}
static double slt_max_secs(double field_cost) {
    double lo = 0.0, hi = 86400.0;                   /* 불변식: slt_pays(lo) 참, slt_pays(hi) 거짓 */
    if (!slt_pays(lo, field_cost)) return 0.0;
    for (int it = 0; it < 60; it++) {
        double mid = 0.5 * (lo + hi);
        if (slt_pays(mid, field_cost)) lo = mid; else hi = mid;
    }
    return lo;
}

int main(void) {
    /* ---- 연습 1 ---- */
    uint32_t base = F(S_CP) | F(S_FT) | F(S_SLT) | F(S_SYSVAL), with_bi = base | F(S_BI);
    CHECK(classify(&DEFECTS[I_OXIDE], STAGES5, NS, base) == -1);
    CHECK(classify(&DEFECTS[I_OXIDE], STAGES5, NS, with_bi) == S_BI);
    CHECK(classify(&DEFECTS[0], STAGES5, NS, with_bi) == S_CP);          /* 다른 결함은 그대로 */
    CHECK(near(unit_cost(&STAGES5[S_BI].econ), 0.78125, 1e-12));
    flow_result_t nb, bi;
    flow_eval(STAGES5, NS, base, 5000.0, &nb);
    flow_eval(STAGES5, NS, with_bi, 5000.0, &bi);
    CHECK(near(nb.escaped - bi.escaped, 28.5, 1e-9));
    double be_bi = breakeven_field_cost(&nb, &bi);
    CHECK(be_bi > 20000.0 && be_bi < 40000.0);
    CHECK(bi.total_per_ship > nb.total_per_ship);                         /* $5000 에선 BI 가 손해 */
    printf("ex1: DPPM %.1f -> %.1f, BI $/unit=%.4f, break-even field cost=$%.0f\n",
           nb.dppm, bi.dppm, unit_cost(&STAGES5[S_BI].econ), be_bi);

    /* ---- 연습 2 ---- */
    double t95 = coverage_for_dl(0.95, 100e-6), t90 = coverage_for_dl(0.90, 100e-6), t80 = coverage_for_dl(0.80, 100e-6);
    CHECK(t95 < t90 && t90 < t80);                                        /* 수율이 낮을수록 더 높은 커버리지 필요 */
    CHECK(near(t95, 0.998050, 1e-6) && near(t80, 0.999552, 1e-6));
    CHECK(near(coverage_for_dl(0.90, 10e-6), 0.999905, 1e-6));
    printf("ex2: T for 100 DPPM: Y=0.95 %.6f  Y=0.90 %.6f  Y=0.80 %.6f ; 10 DPPM @Y=0.90 %.6f\n",
           t95, t90, t80, coverage_for_dl(0.90, 10e-6));

    /* ---- 연습 3 ---- */
    CHECK(near(per_unit_secs(6.0, 8, 0.9), 1.275, 1e-12));
    CHECK(near(per_unit_secs(6.0, 8, 1.0), 0.75, 1e-12));
    CHECK(min_sites(6.0, 0.90, 1.0, 1024) == 14);
    CHECK(min_sites(6.0, 0.85, 1.0, 1024) == 51);                         /* 51 에서 정확히 1.0 s */
    CHECK(min_sites(10.0, 0.95, 1.0, 1024) == 19);                        /* 19: 1.0000000000000004 — epsilon 없으면 20 */
    CHECK(min_sites(6.0, 0.80, 1.0, 1024) == -1);                         /* 점근선 T1(1-MSE)=1.2 s > 1.0 */
    printf("ex3: T/unit(8 sites,MSE .9)=%.3f s  min sites for 1.0 s: MSE .90 -> %d, .85 -> %d, .80 -> %d\n",
           per_unit_secs(6.0, 8, 0.9), min_sites(6.0, 0.90, 1.0, 1024), min_sites(6.0, 0.85, 1.0, 1024),
           min_sites(6.0, 0.80, 1.0, 1024));

    /* ---- 연습 4 ---- */
    double tmax = slt_max_secs(5000.0);
    CHECK(slt_pays(600.0, 5000.0) && !slt_pays(600.0, 30.0));
    CHECK(tmax > 2000.0 && tmax < 3000.0);
    CHECK(slt_pays(tmax - 1.0, 5000.0) && !slt_pays(tmax + 1.0, 5000.0));
    CHECK(slt_max_secs(30.0) < 20.0);
    printf("ex4: SLT break-even time @ $5000 = %.0f s (%.1f min), @ $30 = %.1f s\n",
           tmax, tmax / 60.0, slt_max_secs(30.0));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
