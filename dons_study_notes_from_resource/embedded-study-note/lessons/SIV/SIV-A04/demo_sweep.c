/* demo_sweep.c — 측정 표 생성기. solution.c 를 그대로 포함해 같은 플랜트·governor·checker 로 Part B/C 의 표를 찍는다.
 * 빌드: cc -std=c11 -O2 demo_sweep.c -o demo_sweep -lm && ./demo_sweep
 * 체크(PASS/FAIL)는 없다. 숫자는 전부 대표값 모델의 결과다.
 */
#define main solution_main
#include "solution.c"
#undef main

static const seg_t WL_PART[] = { {300000, 0.70} };

static double t1 = 0;
static void dt_stability(void) {
    printf("== T1: forward Euler stability (35 W step, 20 s sim, die temperature) ==\n");
    const therm_par_t *p = &PLANT_NOM;
    /* A 의 최대 고유값(거듭제곱법): dT/dt = -A T */
    double a[3][3] = {
        { (1/p->r[0])/p->c[0], -(1/p->r[0])/p->c[0], 0 },
        { -(1/p->r[0])/p->c[1], (1/p->r[0]+1/p->r[1])/p->c[1], -(1/p->r[1])/p->c[1] },
        { 0, -(1/p->r[1])/p->c[2], (1/p->r[1]+1/p->r[2])/p->c[2] } };
    double v[3] = {1, -1, 1}, lam = 0;
    for (int it = 0; it < 2000; it++) {
        double w[3] = {0,0,0};
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) w[i] += a[i][j] * v[j];
        lam = sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
        for (int i = 0; i < 3; i++) v[i] = w[i] / lam;
    }
    printf("lambda_max = %.3f 1/s -> exact Euler limit 2/lambda = %.4f s, Gershgorin bound = %.4f s\n", lam, 2/lam, therm_dt_limit(p));
    double dts[] = {0.001, 0.05, 0.1, 0.18, 0.2, 0.25};
    printf("%8s %12s %12s\n", "dt[s]", "T_die@20s", "max|dT| step");
    for (size_t i = 0; i < sizeof dts / sizeof dts[0]; i++) {
        therm_t s = { {45,45,45} };
        double mx = 0;
        int n = (int)(20.0/dts[i] + 0.5);
        for (int k = 0; k < n; k++) { double b = s.t[0]; therm_step(p, &s, 35.0, dts[i]); if (fabs(s.t[0]-b) > mx) mx = fabs(s.t[0]-b); if (fabs(s.t[0]) > 1e9) break; }
        printf("%8.3f %12.3e %12.3e\n", dts[i], s.t[0], mx);
    }
}
static int max_rev_10s(const trace_t *tr) {
    uint32_t ts[20000]; int n = 0, last = 0;
    for (uint32_t i = 0; i < tr->n; i++) {
        int d = tr->row[i].lv_after - tr->row[i].lv_before;
        if (!d) continue;
        int dir = d > 0 ? 1 : -1;
        if (last && dir != last && n < 20000) ts[n++] = tr->row[i].t_ms;
        last = dir;
    }
    int best = 0;
    for (int i = 0; i < n; i++) { int c = 0; for (int j = i; j < n && ts[j] - ts[i] <= CHAT_WIN_MS; j++) c++; if (c > best) best = c; }
    return best;
}
static void offset_table(const char *title, run_cfg_t base) {
    printf("\n== T2: time-to-throttle vs sensor offset (%s) ==\n", title);
    static const int32_t ofs[] = { 5000, 0, -2000, -5000, -8000, -10000, -15000, -20000, -30000 };
    printf("%8s %12s %10s %10s %10s %10s\n", "ofs[C]", "first_dn[s]", "delay[s]", "true@dn", "peak[C]", "over[s]");
    metrics_t m0; sim_run(&base, &g_tr); metrics_from_trace(&g_tr, 105000, &m0);
    for (size_t i = 0; i < sizeof ofs / sizeof ofs[0]; i++) {
        run_cfg_t rc = base; rc.sens.offset_mc = ofs[i];
        metrics_t m; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        if (m.throttled)
            printf("%8.1f %12.2f %+10.2f %10.2f %10.2f %10.1f\n", ofs[i]/1000.0, m.first_down_ms/1000.0, (m.first_down_ms - (int64_t)m0.first_down_ms)/1000.0, m.true_at_down_mc/1000.0, m.peak_true_mc/1000.0, m.over_ms/1000.0);
        else
            printf("%8.1f %12s %10s %10s %10.2f %10.1f\n", ofs[i]/1000.0, "never", "inf", "-", m.peak_true_mc/1000.0, m.over_ms/1000.0);
    }
}
static void lag_table(void) {
    printf("\n== T3: hot-step overshoot vs sensor lag (a=1.3, init die=84 C, thr=90, emg=98) ==\n");
    static const uint32_t lags[] = { 0, 10, 20, 50, 100, 200, 300, 500 };
    printf("%8s %12s %10s %10s %12s %s\n", "lag[ms]", "first_dn[ms]", "true@dn", "peak[C]", "peak-thr[K]", "max_level");
    for (size_t i = 0; i < sizeof lags / sizeof lags[0]; i++) {
        run_cfg_t rc = cfg_hot(); rc.sens.lag_ms = lags[i];
        metrics_t m; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        int maxl = 0; for (uint32_t k = 0; k < g_tr.n; k++) if (g_tr.row[k].lv_after > maxl) maxl = g_tr.row[k].lv_after;
        printf("%8u %12u %10.2f %10.2f %12.2f L%d\n", lags[i], m.first_down_ms, m.true_at_down_mc/1000.0, m.peak_true_mc/1000.0, (m.peak_true_mc-90000)/1000.0, maxl);
    }
}
static void stuck_table(void) {
    printf("\n== T4: stuck sensor at t=5 s (sustain scenario) ==\n");
    static const struct { const char *n; stuck_t k; uint32_t bug; } st[] = {
        {"none", STUCK_NONE, 0}, {"frozen(seq stops) good gov", STUCK_FROZEN, 0}, {"frozen, no stale check", STUCK_FROZEN, BUG_NO_STALE},
        {"value stuck, seq live", STUCK_VALUE, 0} };
    printf("%-28s %10s %10s %10s %8s\n", "case", "fsafe_ms", "peak[C]", "over[s]", "verdict");
    for (size_t i = 0; i < 4; i++) {
        run_cfg_t rc = cfg_sustain(); rc.sens.stuck = st[i].k; rc.sens.stuck_at_ms = 5000; rc.bugs = st[i].bug;
        metrics_t m; report_t r; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r); check_safety(&g_tr, &SPEC_NOM, &r);
        uint32_t f = 0; for (uint32_t k = 0; k < g_tr.n; k++) if (g_tr.row[k].lv_after == LV_MIN_PERF) { f = g_tr.row[k].t_ms; break; }
        printf("%-28s %10u %10.2f %10.1f %8s\n", st[i].n, f, m.peak_true_mc/1000.0, m.over_ms/1000.0, VERDICT_NAME[verdict_of(&r)]);
    }
}
static void guard_table(void) {
    printf("\n== T5: required guard band Tj_max - thr [C] (rel/emg/sd keep their distance from thr) ==\n");
    static const int32_t ofs[] = { 0, -2000, -5000, -10000 };
    static const uint32_t lags[] = { 0, 20, 100, 300 };
    printf("hot step (a=1.3, starts 6 C below thr):\n%10s", "ofs\\lag");
    for (int j = 0; j < 4; j++) printf(" %7ums", lags[j]);
    printf("\n");
    for (int i = 0; i < 4; i++) {
        printf("%10.1f", ofs[i]/1000.0);
        for (int j = 0; j < 4; j++) { run_cfg_t rc = cfg_hot(); rc.sens.offset_mc = ofs[i]; rc.sens.lag_ms = lags[j]; rc.dur_ms = 5000; printf(" %9.2f", guard_search(rc, &g_tr, 105000)/1000.0); }
        printf("\n");
    }
    printf("slow ramp (sustain, 60 s):\n%10s", "ofs\\lag");
    for (int j = 0; j < 2; j++) printf(" %7ums", lags[j]);
    printf("\n");
    for (int i = 0; i < 4; i++) {
        printf("%10.1f", ofs[i]/1000.0);
        for (int j = 0; j < 2; j++) { run_cfg_t rc = cfg_sustain(); rc.dur_ms = 60000; rc.sens.offset_mc = ofs[i]; rc.sens.lag_ms = lags[j]; printf(" %9.2f", guard_search(rc, &g_tr, 105000)/1000.0); }
        printf("\n");
    }
}
static void fan_sd_table(void) {
    printf("\n== T5b: fan failure, shutdown reading threshold sd vs true peak (sensor offset) ==\n");
    static const int32_t ofs[] = { 0, -2000, -5000 };
    printf("%8s %10s %10s %12s\n", "ofs[C]", "sd_read", "peak[C]", "shut@[s]");
    for (int i = 0; i < 3; i++) {
        run_cfg_t rc = cfg_fan(); rc.sens.offset_mc = ofs[i];
        metrics_t m; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        printf("%8.1f %10.1f %10.2f %12.1f\n", ofs[i]/1000.0, 105.0, m.peak_true_mc/1000.0, m.shut_ms/1000.0);
    }
}
static void guard_cost(void) {
    printf("\n== T6: cost of guard band (sustain 120 s, Ta=45; work = sum f*a*dt, normalized to thr=105) ==\n");
    static const int32_t thr[] = { 105000, 100000, 95000, 90000, 85000, 80000 };
    double w0 = 0;
    printf("%8s %10s %8s %8s\n", "thr[C]", "work[%]", "peak", "minL");
    for (int i = 0; i < 6; i++) {
        run_cfg_t rc = cfg_sustain(); rc.dur_ms = 120000; rc.gov.thr_mc = thr[i]; rc.gov.rel_mc = thr[i] - 6000; rc.gov.emg_mc = thr[i] + 8000; rc.gov.sd_mc = thr[i] + 15000;
        metrics_t m; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 200000, &m);
        if (i == 0) w0 = g_tr.work;
        int maxl = 0; for (uint32_t k = 0; k < g_tr.n; k++) if (g_tr.row[k].lv_after > maxl) maxl = g_tr.row[k].lv_after;
        printf("%8.1f %10.1f %8.2f %8d\n", thr[i]/1000.0, 100.0 * g_tr.work / w0, m.peak_true_mc/1000.0, maxl);
    }
}
static void burst_limit(void) {
    printf("\n== T7: short bursts vs sensor lag: what the sensor saw vs truth (die starts 80 C, one burst a=2.0, sensor lag 50 ms) ==\n");
    static const uint32_t widths[] = { 5, 10, 20, 50, 100, 200, 500 };
    printf("%8s %10s %10s %10s %8s\n", "burst", "peak_true", "peak_read", "miss[K]", "throttled");
    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        seg_t w[2] = { {widths[i], 2.0}, {5000, 0.05} };
        run_cfg_t rc = cfg_hot(); rc.wl = w; rc.nseg = 2; rc.dur_ms = 5000; rc.init = (therm_t){ {80, 79, 76} }; rc.sens.lag_ms = 50;
        metrics_t m; sim_run(&rc, &g_tr); metrics_from_trace(&g_tr, 105000, &m);
        int32_t pr = INT32_MIN; for (uint32_t k = 0; k < g_tr.n; k++) if (g_tr.row[k].read_mc > pr) pr = g_tr.row[k].read_mc;
        printf("%6ums %10.2f %10.2f %10.2f %8s\n", widths[i], m.peak_true_mc/1000.0, pr/1000.0, (m.peak_true_mc - pr)/1000.0, m.throttled ? "yes" : "no");
    }
}
static void chatter_table(void) {
    printf("\n== T8: level-direction reversals per 10 s, warm ambient scenario (limit CHAT_MAX=%d) ==\n", CHAT_MAX);
    static const struct { const char *n; uint32_t bug; int32_t rel; } t[] = { {"good (hyst 6 C)", 0, 84000}, {"hysteresis sign bug", BUG_HYST_SIGN, 84000}, {"good but hyst 1 C", 0, 89000}, {"good but hyst 0.5 C", 0, 89500} };
    printf("%-22s %8s %8s %8s\n", "case", "max_rev", "changes", "chatter");
    for (int i = 0; i < 4; i++) {
        run_cfg_t rc = cfg_warm(); rc.bugs = t[i].bug; rc.gov.rel_mc = t[i].rel;
        report_t r; sim_run(&rc, &g_tr); rep_init(&r); check_policy(&g_tr, &SPEC_NOM, &r);
        uint32_t chg = 0; for (uint32_t k = 0; k < g_tr.n; k++) if (g_tr.row[k].lv_after != g_tr.row[k].lv_before) chg++;
        printf("%-22s %8d %8u %8u\n", t[i].n, max_rev_10s(&g_tr), chg, r.count[V_CHATTER]);
    }
}
int main(void) {
    (void)t1;
    dt_stability();
    run_cfg_t s = cfg_sustain();
    offset_table("sustain a=1.0, Ta=45, from 45 C", s);
    run_cfg_t p = cfg_sustain(); p.wl = WL_PART; p.nseg = 1; p.dur_ms = 300000;
    offset_table("part load a=0.70, Ta=45, from 45 C", p);
    lag_table();
    stuck_table();
    guard_table();
    fan_sd_table();
    guard_cost();
    burst_limit();
    chatter_table();
    return 0;
}
