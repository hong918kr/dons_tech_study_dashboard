/* ARCH-B13 추가 연습 — 한 번에 검증
 * 연습 1 useful skew (2단 파이프라인 가운데 레지스터 클럭 지연)
 * 연습 2 PVT 코너: setup 은 slow, hold 는 fast
 * 연습 3 클럭 불확실성(jitter/uncertainty)
 * 연습 4 목표 주파수에서의 논리 깊이 예산
 * 연습 5 2-FF vs 3-FF 동기화기 MTBF
 * 단위: ps (정수), MTBF 계산만 double.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int32_t tpcq, tccq, tpd, tcd, tsu, th, skew; } ff_path;

static int32_t setup_slack_ps(const ff_path *p, int32_t tclk) { return tclk + p->skew - (p->tpcq + p->tpd + p->tsu); }
static int32_t hold_slack_ps(const ff_path *p)                { return p->tccq + p->tcd - p->th - p->skew; }

/* ---- 연습 1: R1 →(s1)→ R2 →(s2)→ R3, R2 의 클럭만 d 만큼 늦춘다 (s1 skew = +d, s2 skew = −d).
 * s1, s2 는 이상적 클럭(skew 0) 기준 값. hold 를 지키는 범위에서 Tc 를 최소화하는 d 와 그 Tc. */
static int32_t best_mid_delay(const ff_path *s1, const ff_path *s2, int32_t *tc_out) {
    int32_t a1 = s1->tpcq + s1->tpd + s1->tsu;          /* Tc >= a1 − d */
    int32_t a2 = s2->tpcq + s2->tpd + s2->tsu;          /* Tc >= a2 + d */
    int32_t d_hi = s1->tccq + s1->tcd - s1->th;         /* s1 hold: h1 − d >= 0 */
    int32_t d_lo = -(s2->tccq + s2->tcd - s2->th);      /* s2 hold: h2 + d >= 0 */
    int32_t d = (a1 - a2) / 2;                          /* 두 단을 같게 */
    if (d > d_hi) d = d_hi;
    if (d < d_lo) d = d_lo;
    int32_t t1 = a1 - d, t2 = a2 + d;
    *tc_out = t1 > t2 ? t1 : t2;
    return d;
}

/* ---- 연습 2: 데이터 경로 지연(tpcq,tccq,tpd,tcd)만 pct% 로 스케일. tsu/th/skew 는 그대로 둔 단순 모델 */
static ff_path derate(const ff_path *p, int pct) {
    ff_path q = *p;
    q.tpcq = p->tpcq * pct / 100; q.tccq = p->tccq * pct / 100;
    q.tpd  = p->tpd  * pct / 100; q.tcd  = p->tcd  * pct / 100;
    return q;
}
typedef struct { int32_t setup_slow, hold_fast; bool ok; } corner_result;
static corner_result corner_check(const ff_path *p, int32_t tclk, int slow_pct, int fast_pct) {
    ff_path s = derate(p, slow_pct), f = derate(p, fast_pct);
    corner_result r = { setup_slack_ps(&s, tclk), hold_slack_ps(&f), false };
    r.ok = r.setup_slow >= 0 && r.hold_fast >= 0;
    return r;
}

/* ---- 연습 3: 불확실성 u_setup 은 setup 에서, u_hold 는 hold 에서 뺀다 */
static int32_t setup_slack_unc(const ff_path *p, int32_t tclk, int32_t u_setup) { return setup_slack_ps(p, tclk) - u_setup; }
static int32_t hold_slack_unc(const ff_path *p, int32_t u_hold)                  { return hold_slack_ps(p) - u_hold; }
static uint32_t fmax_khz_unc(const ff_path *p, int32_t u_setup) {
    int32_t t = p->tpcq + p->tpd + p->tsu - p->skew + u_setup;
    return t > 0 ? 1000000000u / (uint32_t)t : 0;
}

/* ---- 연습 4: 목표 주파수에서 조합 논리에 쓸 수 있는 최대 지연과 게이트 단 수 */
static int32_t tpd_budget_ps(uint32_t f_khz, int32_t tpcq, int32_t tsu, int32_t skew, int32_t u_setup) {
    int32_t tc = (int32_t)(1000000000u / f_khz);        /* 주기 내림 = 비관적 */
    return tc - tpcq - tsu + skew - u_setup;
}
static int logic_levels(int32_t budget_ps, int32_t t_gate_ps) { return budget_ps > 0 ? budget_ps / t_gate_ps : 0; }

/* ---- 연습 5: N-플롭 동기화기 MTBF = e^(tr/τ) / (T0 · fclk · fdata),  tr = (N−1)(Tc − tpcq − tsu) */
static double sync_mtbf_s(int n_ff, double tc_ps, double tpcq_ps, double tsu_ps,
                          double tau_ps, double t0_ps, double fclk_hz, double fdata_hz) {
    double tr = (n_ff - 1) * (tc_ps - tpcq_ps - tsu_ps);
    return exp(tr / tau_ps) / (t0_ps * 1e-12 * fclk_hz * fdata_hz);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    {   ff_path s1 = { 90, 40, 1900, 500, 60, 30, 0 };  /* a1 = 2050, h1 = 510 */
        ff_path s2 = { 90, 40, 1100, 200, 60, 30, 0 };  /* a2 = 1250, h2 = 210 */
        int32_t tc = 0;
        CHECK(best_mid_delay(&s1, &s2, &tc) == 400 && tc == 1650);   /* 이상적 클럭이면 2050 */
        s1.tcd = 160;                                    /* h1 = 170 → d 는 170 까지만 */
        CHECK(best_mid_delay(&s1, &s2, &tc) == 170 && tc == 1880);
        ff_path a = s1, b = s2; a.skew = 170; b.skew = -170;
        CHECK(setup_slack_ps(&a, 1880) == 0 && hold_slack_ps(&a) == 0 && hold_slack_ps(&b) == 380);
    }
    /* 연습 2 */
    {   ff_path p = { 100, 60, 1500, 200, 80, 50, 0 };
        CHECK(setup_slack_ps(&p, 2000) == 320);                      /* typical OK */
        corner_result r = corner_check(&p, 2000, 125, 70);
        CHECK(r.setup_slow == -80 && r.hold_fast == 132 && !r.ok);   /* slow 에서 setup 위반 */
        ff_path q = { 100, 60, 400, 40, 80, 50, 30 };
        CHECK(hold_slack_ps(&q) == 20);                              /* typical OK */
        r = corner_check(&q, 2000, 125, 70);
        CHECK(r.setup_slow > 0 && r.hold_fast == -10 && !r.ok);      /* fast 에서 hold 위반 */
    }
    /* 연습 3 */
    {   ff_path p = { 100, 60, 1500, 200, 80, 50, 0 };
        CHECK(setup_slack_unc(&p, 2000, 100) == 220 && hold_slack_unc(&p, 30) == 180);
        CHECK(fmax_khz_unc(&p, 0) == 595238u && fmax_khz_unc(&p, 100) == 561797u);
    }
    /* 연습 4 */
    {   int32_t b = tpd_budget_ps(1000000u, 50, 40, 0, 30);          /* 1 GHz */
        CHECK(b == 880 && logic_levels(b, 15) == 58);
        b = tpd_budget_ps(2000000u, 50, 40, 0, 30);                  /* 2 GHz: 380 ps */
        CHECK(b == 380 && logic_levels(b, 15) == 25);
        CHECK(logic_levels(tpd_budget_ps(10000000u, 50, 40, 0, 30), 15) == 0); /* 10 GHz: 예산 음수 */
    }
    /* 연습 5: 1 GHz, τ=30 ps, T0=100 ps, fdata=50 MHz (설명용 값) */
    {   double m2 = sync_mtbf_s(2, 1000, 90, 60, 30, 100, 1e9, 50e6);
        double m3 = sync_mtbf_s(3, 1000, 90, 60, 30, 100, 1e9, 50e6);
        CHECK(m2 > 1e5 && m2 < 1e6);                                 /* 며칠 → 제품으로 불가 */
        CHECK(m3 > 1e17);                                            /* 우주 나이(~4e17 s) 급 */
        CHECK(fabs(log(m3 / m2) - 850.0 / 30.0) < 1e-9);             /* 한 단 = e^(850/30) 배 */
        printf("MTBF 2-FF = %.3g s (%.1f days), 3-FF = %.3g s\n", m2, m2 / 86400.0, m3);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
