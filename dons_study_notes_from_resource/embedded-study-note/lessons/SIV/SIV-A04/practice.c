/* SIV-A04 추가 연습 — 한 번에 검증
 * P1 1노드 해석해로 trip 시각 계산 · P2 누설 열폭주 임계 Rth · P3 soak 2점 트림과 자기발열 편향
 * P4 교차 센서 타당성 검사(stuck 값 잡기, 공통모드는 못 잡음) · P5 chatter 없는 최소 hysteresis
 * 모든 숫자는 대표값. 결정적, 난수·벽시계 없음.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int g_total, g_pass;
#define CHECK(cond, ...) do { g_total++; if (cond) { g_pass++; printf("PASS  "); } else printf("FAIL  "); \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

/* ---- P1: 1노드 RC 해석해 ---- T(t) = Tinf + (T0 - Tinf) exp(-t/tau) */
static double t_reach(double t0, double tinf, double tau, double target) {
    if (target >= tinf) return INFINITY;              /* 점근선 위로는 절대 못 간다 */
    if (target <= t0) return 0.0;
    return -tau * log((tinf - target) / (tinf - t0));
}
static double sim_reach(double t0, double tinf, double tau, double target, double dt) {
    double T = t0, t = 0;
    for (long i = 0; i < 40000000; i++) { if (T >= target) return t; T += dt * (tinf - T) / tau; t += dt; }
    return INFINITY;
}
static void p1(void) {
    double t0 = 45, tinf = 120, tau = 90;
    double base = t_reach(t0, tinf, tau, 90);
    static const double OF[] = { 0, 5, 10, 15, 20, 25, 29 };
    double prev = 0;
    bool mono = true;
    for (size_t i = 0; i < sizeof OF / sizeof OF[0]; i++) {
        double t = t_reach(t0, tinf, tau, 90 + OF[i]);
        double dbg = sim_reach(t0, tinf, tau, 90 + OF[i], 0.01);
        /* 허용 0.1 s: dt 0.01 s Euler 의 첫 도달 시각 양자화 + O(dt) */
        CHECK(fabs(t - dbg) < 0.1, "P1 reach %.0f C: analytic %.2f s, Euler %.2f s", 90 + OF[i], t, dbg);
        if (i && t - prev < 0) mono = false;
        prev = t;
    }
    /* 같은 5 ℃ 오프셋이라도 뒤로 갈수록 지연이 커진다 (기울기가 줄어서) */
    double d5_low = t_reach(t0, tinf, tau, 95) - base;
    double d5_high = t_reach(t0, tinf, tau, 115) - t_reach(t0, tinf, tau, 110);
    CHECK(mono && d5_high > 2.5 * d5_low, "P1 same 5 C offset: delay %.1f s near 90 C vs %.1f s near 115 C", d5_low, d5_high);
    CHECK(isinf(t_reach(t0, tinf, tau, 121)), "P1 target above asymptote -> never");
}

/* ---- P2: 누설 포함 열폭주 임계 ---- T = Ta + R (Pd + P0 exp(k (T-25))) */
static bool settles(double ta, double r, double pd, double p0, double k, double *out) {
    double T = ta;
    for (long i = 0; i < 2000000; i++) {
        double nt = ta + r * (pd + p0 * exp(k * (T - 25.0)));
        if (nt > 400.0) return false;
        if (fabs(nt - T) < 1e-9) { *out = nt; return true; }
        T = nt;
    }
    return false;
}
static double r_crit_scan(double ta, double pd, double p0, double k) {
    double lo = 0.5, hi = 20.0, tmp;
    for (int i = 0; i < 60; i++) { double m = (lo + hi) / 2; if (settles(ta, m, pd, p0, k, &tmp)) lo = m; else hi = m; }
    return lo;
}
/* 접선 조건: R k P0 e^{k(T*-25)} = 1, T* = Ta + R Pd + 1/k */
static double r_crit_tangent(double ta, double pd, double p0, double k) {
    double lo = 0.01, hi = 50.0;
    for (int i = 0; i < 200; i++) {
        double r = (lo + hi) / 2;
        double f = 1.0 / (r * k) - p0 * exp(k * (ta + r * pd + 1.0 / k - 25.0));
        if (f > 0) lo = r; else hi = r;
    }
    return (lo + hi) / 2;
}
static void p2(void) {
    double ta = 45, pd = 30, p0 = 0.8, k = 0.02, T;
    double rs = r_crit_scan(ta, pd, p0, k), rt = r_crit_tangent(ta, pd, p0, k);
    CHECK(fabs(rs - rt) < 0.01, "P2 critical R: fixed-point scan %.4f, tangent condition %.4f K/W (tol 0.01)", rs, rt);
    CHECK(settles(ta, 2.5, pd, p0, k, &T), "P2 nominal R=2.5 settles at %.1f C", T);
    double lg = 2.5 * p0 * k * exp(k * (T - 25.0));
    CHECK(lg < 1.0, "P2 thermal loop gain at nominal = %.3f < 1", lg);
    CHECK(!settles(ta, rs * 1.05, pd, p0, k, &T), "P2 R 5%% above critical runs away");
}

/* ---- P3: 챔버 soak 2점 트림 ---- reading = true*(1+g) + o */
static void trim2(double r1, double t1, double r2, double t2, double *g, double *o) {
    *g = (r2 - r1) / (t2 - t1) - 1.0;
    *o = r1 - t1 * (1.0 + *g);
}
static void p3(void) {
    double gi = 0.012, oi = -2.5, g, o;
    double r1 = 25 * (1 + gi) + oi, r2 = 105 * (1 + gi) + oi;
    trim2(r1, 25, r2, 105, &g, &o);
    CHECK(fabs(g - gi) < 1e-9 && fabs(o - oi) < 1e-9, "P3 ideal soak recovers g=%+.4f o=%+.3f", g, o);
    /* 자기발열: idle 2 W × Rja 2.5 K/W → die = chamber + 5 K 인데 chamber 를 정답으로 썼다 */
    double dh = 2.0 * 2.5;
    double rh1 = (25 + dh) * (1 + gi) + oi, rh2 = (105 + dh) * (1 + gi) + oi, gh, oh;
    trim2(rh1, 25, rh2, 105, &gh, &oh);
    CHECK(fabs(gh - gi) < 1e-9 && fabs((oh - oi) - dh * (1 + gi)) < 1e-6, "P3 self-heat %.1f K biases offset by %+.3f K, gain unaffected", dh, oh - oi);
    /* 곡률: 2점 트림의 잔차는 중간에서 최대 */
    double c = 6e-4;
    double f1 = 25 * (1 + gi) + oi + c * (25 - 65) * (25 - 65), f2 = 105 * (1 + gi) + oi + c * (105 - 65) * (105 - 65), gc, oc;
    trim2(f1, 25, f2, 105, &gc, &oc);
    double r65 = 65 * (1 + gi) + oi;           /* 진짜 곡률 항은 65 ℃ 에서 0 */
    double res = r65 - (65 * (1 + gc) + oc);
    CHECK(fabs(res - (-c * 40 * 40)) < 1e-6, "P3 curvature %.1e: residual at mid %.3f K", c, res);
}

/* ---- P4: 교차 센서 타당성 ---- */
static int cross_check(const int32_t *a, const int32_t *b, int n, int32_t lim, int persist) {
    int run = 0;
    for (int i = 0; i < n; i++) {
        run = labs(a[i] - b[i]) > lim ? run + 1 : 0;
        if (run >= persist) return i;
    }
    return -1;
}
#define N4 1000
static int32_t A4[N4], B4[N4];
static void p4(void) {
    for (int i = 0; i < N4; i++) { A4[i] = 50000 + i * 40; B4[i] = 49000 + i * 40; }   /* 정상: 1 ℃ 기울기 */
    CHECK(cross_check(A4, B4, N4, 3000, 5) == -1, "P4 normal gradient 1 C: no alarm");
    for (int i = 0; i < N4; i++) A4[i] = i < 200 ? 50000 + i * 40 : 58000;              /* A 가 값 고정, seq 는 살아 있음 */
    int k = cross_check(A4, B4, N4, 3000, 5);
    CHECK(k >= 301 && k < 320, "P4 value-stuck sensor flagged at sample %d (stuck from 200, needs |a-b|>3 C first)", k);
    for (int i = 0; i < N4; i++) { A4[i] = 50000 + i * 40 - 10000; B4[i] = 49000 + i * 40 - 10000; }
    CHECK(cross_check(A4, B4, N4, 3000, 5) == -1, "P4 LIMIT: common-mode -10 C offset on both sensors is not detected");
}

/* ---- P5: chatter 없는 최소 hysteresis (1노드, 두 OPP) ---- */
static int rev_count(double hyst, double ta, double r, double c, double p_hi, double p_lo, double thr, double t_end) {
    double T = thr - 0.5, t = 0;
    bool slow = false;
    int rev = 0;
    while (t < t_end) {
        double p = slow ? p_lo : p_hi;
        T += 0.001 * (p * r + ta - T) / (r * c);
        t += 0.001;
        if (!slow && T >= thr) { slow = true; rev++; }
        else if (slow && T < thr - hyst) { slow = false; rev++; }
    }
    return rev;
}
static void p5(void) {
    double ta = 45, r = 2.5, c = 53, p_hi = 22, p_lo = 15, thr = 90;
    double t_lo = ta + r * p_lo;
    double h_analytic = thr - t_lo;
    double found = -1;
    for (double h = 0.25; h < 30; h += 0.25) { if (rev_count(h, ta, r, c, p_hi, p_lo, thr, 3000) <= 1) { found = h; break; } }
    CHECK(found >= 0 && fabs(found - h_analytic) <= 0.5, "P5 min hysteresis: scan %.2f K, analytic thr-Tss(L_low)=%.2f K (tol 0.5 = scan step x2)", found, h_analytic);
    CHECK(rev_count(1.0, ta, r, c, p_hi, p_lo, thr, 3000) > 10, "P5 hysteresis 1 K limit-cycles (%d reversals in 3000 s)", rev_count(1.0, ta, r, c, p_hi, p_lo, thr, 3000));
}

int main(void) {
    p1(); p2(); p3(); p4(); p5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
