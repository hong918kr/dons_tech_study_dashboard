/* ARCH-A08 추가 연습 — 한 번에 검증
 *
 * solution.c 의 모델을 그대로 가져와(main 이름만 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  DVFS 없는 MCU (고정 전압, µA/MHz + 바닥 전류) + 플래시 wait state → 에너지 최적 클럭
 *   연습 2  배터리 2 년 목표 → 허용되는 최소 깨어남 주기 (닫힌 식) 와 node_life_days 로 역검증
 *   연습 3  누설-온도 되먹임: Tj = T_amb + θ·P(Tj) 고정점, 열폭주가 시작되는 θ (반복법 vs 접점 식)
 *   연습 4  에너지 최적 OPP vs EDP / ED²P 최적 OPP
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: 고정 전압 MCU ---------- */
typedef struct {
    double v, ua_per_mhz, i_base, i_sleep;   /* 전압, 활성 전류 기울기, 바닥 전류, 슬립 전류 */
    double n, deadline;                      /* 작업 사이클 (CPI 1 기준), 마감 */
    double ws_step_hz, cpi_per_ws;           /* f 가 ws_step 을 넘을 때마다 wait state 1 개, WS 당 CPI 증가분 */
} mcu_t;

static int wait_states(const mcu_t *u, double f) {   /* ws = ceil(f / step) − 1 */
    int ws = (int)ceil(f / u->ws_step_hz - 1e-9) - 1;
    return ws < 0 ? 0 : ws;
}
static double mcu_job_energy(const mcu_t *u, double f) {
    double cpi = 1.0 + u->cpi_per_ws * wait_states(u, f);
    double t = u->n * cpi / f;
    double i = u->ua_per_mhz * 1e-6 * (f / 1e6) + u->i_base;
    return u->v * i * t + u->v * u->i_sleep * (u->deadline - t);
}

/* ---------- 연습 3: 열 되먹임 ---------- */
static double p_busy(const pmodel_t *m) { return p_dyn(m, 1.0, 1800e6) + p_leak(m, 1.0) + m->p_static; }
/* T_amb 에서 시작해 T ← T_amb + θ·P(T) 반복. 수렴하면 고정점, 200 °C 를 넘으면 NAN (열폭주) */
static double thermal_fixed_point(pmodel_t m, double t_amb, double theta) {
    double t = t_amb;
    for (int it = 0; it < 100000; it++) {
        m.temp_c = t;
        double tn = t_amb + theta * p_busy(&m);
        if (tn > 200.0) return NAN;
        if (fabs(tn - t) < 1e-10) return tn;
        t = tn;
    }
    return NAN;
}

int main(void) {
    /* 연습 1 */
    printf("== 연습 1: 고정 전압 MCU, 플래시 wait state ==\n");
    mcu_t u = {.v = 1.8, .ua_per_mhz = 50, .i_base = 0.3e-3, .i_sleep = 2e-6, .n = 1e6, .deadline = 0.1,
               .ws_step_hz = 24e6, .cpi_per_ws = 0.15};
    double fs[] = {8e6, 16e6, 24e6, 32e6, 48e6, 64e6, 80e6};
    int best = 0, best_nows = 0;
    mcu_t u0 = u; u0.cpi_per_ws = 0;
    printf("   f[MHz]  WS  CPI   E_job[µJ]  (WS 무시 시)\n");
    for (int k = 0; k < 7; k++) {
        double e = mcu_job_energy(&u, fs[k]), e0 = mcu_job_energy(&u0, fs[k]);
        printf("   %5.0f   %d   %.2f  %8.3f   %8.3f\n", fs[k] / 1e6, wait_states(&u, fs[k]),
               1 + u.cpi_per_ws * wait_states(&u, fs[k]), e * 1e6, e0 * 1e6);
        if (e < mcu_job_energy(&u, fs[best])) best = k;
        if (e0 < mcu_job_energy(&u0, fs[best_nows])) best_nows = k;
    }
    printf("   최적: WS 반영 %.0f MHz, WS 무시 %.0f MHz\n", fs[best] / 1e6, fs[best_nows] / 1e6);
    CHECK(wait_states(&u, 24e6) == 0 && wait_states(&u, 24.1e6) == 1 && wait_states(&u, 48e6) == 1 &&
          wait_states(&u, 64e6) == 2);
    CHECK(fs[best] == 24e6 && fs[best_nows] == 80e6);

    /* 연습 2 */
    printf("== 연습 2: 2 년 수명 → 최소 깨어남 주기 ==\n");
    node_t nd = node_default();
    node_split_t s = node_split(&nd);
    double e_act_batt = (s.p[T_MCU] + s.p[T_SENSOR] + s.p[T_TX] + s.p[T_RX]) * nd.period_s;   /* 주기와 무관 */
    double ps = nd.v_load * nd.sleep_i_a / nd.eta_sleep;
    double A = e_act_batt - ps * s.t_awake;                   /* P(T) = A / T + B */
    double B = ps + s.p[T_IQ] + s.p[T_SELFDIS];
    double p_star = s.e_usable / (730.0 * 86400.0);
    double t_min = A / (p_star - B);
    node_t n2 = nd; n2.period_s = t_min;
    printf("   E_active(배터리쪽)=%.3f µJ/회  A=%.3f µJ  B=%.3f µW  P*=%.3f µW  → T_min = %.3f s, 검산 수명 %.3f 일\n",
           e_act_batt * 1e6, A * 1e6, B * 1e6, p_star * 1e6, t_min, node_life_days(&n2));
    CHECK(t_min > 0 && near(node_life_days(&n2), 730.0, 1e-9));
    double p_floor = s.e_usable / (B * 86400.0);
    printf("   어떤 주기로도 넘을 수 없는 상한 = E_usable / B = %.1f 일\n", p_floor);
    CHECK(p_floor > 2900 && p_floor < 3000);

    /* 연습 3 */
    printf("== 연습 3: 누설-온도 되먹임 (최고 OPP 100%% 부하, T_amb 25 °C) ==\n");
    pmodel_t m = model_default();
    double thetas[] = {10, 20, 30, 40, 44, 46, 50};
    for (int k = 0; k < 7; k++) {
        double t = thermal_fixed_point(m, 25.0, thetas[k]);
        if (isnan(t)) printf("   θ = %4.0f °C/W : 고정점 없음 (열폭주)\n", thetas[k]);
        else {
            pmodel_t mt = m; mt.temp_c = t;
            printf("   θ = %4.0f °C/W : Tj = %6.2f °C, P = %.4f W (누설 %.4f W)\n", thetas[k], t, p_busy(&mt), p_leak(&mt, 1.0));
        }
    }
    /* 반복법으로 찾은 경계 */
    double lo = 1, hi = 200;
    for (int it = 0; it < 80; it++) {
        double mid = 0.5 * (lo + hi);
        if (isnan(thermal_fixed_point(m, 25.0, mid))) hi = mid; else lo = mid;
    }
    /* 접점 조건: θ·b·e^((T−T0)/Ts)/Ts = 1 와 T = T_amb + θ(a + b e^(..)) → T0 + Ts·ln(Ts/(θb)) = T_amb + θa + Ts */
    pmodel_t m0 = m; m0.temp_c = m0.t0;
    double a = p_dyn(&m, 1.0, 1800e6) + m.p_static, b = p_leak(&m0, 1.0);
    double l2 = 1, h2 = 200;
    for (int it = 0; it < 200; it++) {
        double th = 0.5 * (l2 + h2);
        double g = m.t0 + m.t_s * log(m.t_s / (th * b)) - (25.0 + th * a + m.t_s);
        if (g > 0) l2 = th; else h2 = th;
    }
    double t_tan = 25.0 + 0.5 * (l2 + h2) * a + m.t_s;
    printf("   열폭주 경계 θ: 반복법 %.4f, 접점 식 %.4f °C/W (접점 온도 %.2f °C)\n", lo, 0.5 * (l2 + h2), t_tan);
    CHECK(!isnan(thermal_fixed_point(m, 25.0, 40)) && isnan(thermal_fixed_point(m, 25.0, 50)));
    CHECK(fabs(lo - 0.5 * (l2 + h2)) < 1e-3);

    /* 연습 4 */
    printf("== 연습 4: E vs EDP vs ED²P (실행만, m=0) ==\n");
    job_t j = job_default();
    int bE = 0, bEDP = 0, bED2P = 0;
    double vE = 1e9, vEDP = 1e9, vED2P = 1e9;
    printf("   OPP  E_run[J]  t_run[s]  EDP[J·s]  ED²P[J·s²]\n");
    for (int i = 0; i < NOPP; i++) {
        plan_t p = eval_opp(&m, OPP5, NOPP, i, &j, POL_BEST);
        double e = p.e_run, t = p.t_run;
        printf("   %d    %.5f   %.4f    %.5f   %.5f\n", i, e, t, e * t, e * t * t);
        if (e < vE) { vE = e; bE = i; }
        if (e * t < vEDP) { vEDP = e * t; bEDP = i; }
        if (e * t * t < vED2P) { vED2P = e * t * t; bED2P = i; }
    }
    printf("   최적 OPP: E %d, EDP %d, ED²P %d\n", bE, bEDP, bED2P);
    CHECK(bE == critical_opp(&m, OPP5, NOPP) && bE == 1 && bEDP == 4 && bED2P == 4);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
