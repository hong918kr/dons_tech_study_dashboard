/* ARCH-A10 추가 연습 — 한 번에 검증
 *
 * solution.c 의 시뮬레이터를 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  "그랜트 횟수" 로 센 가중 RR (credit WRR) → 대역폭 몫 = w·L / Σ w·L  (DRR 과 비교)
 *   연습 2  hard 규제기 → work-conserving 이 깨진다 (자원이 노는데 요청이 있음), 대신 NPU 상한이 정확해진다
 *   연습 3  NPU 버스트 길이 스윕 → CAM 마감을 지킬 수 있는 최대 L 을 식으로 예측하고 시뮬레이터로 확인
 *   연습 4  Little 의 법칙: drain 실행에서 ∫(큐 길이) dt == Σ wait  (통계 회계 검증)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: credit WRR (가중치 = 한 프레임에 받을 그랜트 수) ---------- */
static int g_w[MAXM], g_credit[MAXM];
static int arb_credit_wrr(sim_t *s) {
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 1; k <= s->nm; k++) {
            int m = (s->rr_last + k) % s->nm;
            if (pending(s, m) && g_credit[m] > 0) { g_credit[m]--; s->rr_last = m; return m; }
        }
        for (int m = 0; m < s->nm; m++) g_credit[m] = g_w[m];   /* 요청 있는 마스터의 크레딧이 다 떨어짐 → 새 프레임 */
    }
    return -1;
}

static void ex1(void) {
    printf("== 연습 1: credit WRR vs DRR, 가중치 3:3:4, 셋 다 backlog (L = 2 / 16 / 8) ==\n");
    const int64_t T = 200000;
    mcfg_t c[3] = {
        { "CPU", TG_BACKLOG, 0, 0, 2, 0, 0, NULL, 0 },
        { "DMA", TG_BACKLOG, 0, 0, 16, 0, 0, NULL, 0 },
        { "NPU", TG_BACKLOG, 0, 0, 8, 0, 0, NULL, 0 },
    };
    static const int w[3] = {3, 3, 4};
    double wl = 0; for (int m = 0; m < 3; m++) wl += w[m] * c[m].len;     /* 86 */
    for (int m = 0; m < 3; m++) { g_w[m] = w[m]; g_credit[m] = w[m]; }
    sim_init(S, 3, c, ARB_EXT, T, 1); S->ext = arb_credit_wrr; sim_run(S, false);
    double cw[3]; bool ok = S->wc_viol == 0;
    for (int m = 0; m < 3; m++) {
        cw[m] = achieved(S, m);
        if (fabs(cw[m] - w[m] * c[m].len / wl) > 2.0 * wl / T) ok = false;
    }
    sim_init(S, 3, c, ARB_DRR, T, 1);
    for (int m = 0; m < 3; m++) S->quantum[m] = 16 * w[m];
    sim_run(S, false);
    printf("         credit-WRR  (w*L/sum)   DRR    (w/sum)\n");
    for (int m = 0; m < 3; m++)
        printf("  %-4s   %.4f     (%.4f)     %.4f (%.4f)\n", c[m].name, cw[m], w[m] * c[m].len / wl, achieved(S, m), w[m] / 10.0);
    CHECK(ok);
    CHECK(fabs(achieved(S, CPU) - 0.3) < 0.001 && cw[CPU] < 0.08);
}

/* ---------- 연습 2: soft vs hard 규제기 ---------- */
static void ex2(void) {
    printf("== 연습 2: QOS(CPU lvl 8) 에서 NPU 규제 soft vs hard (토큰 0.5 beat/cycle, cap 64) ==\n");
    const int64_t T = 200000;
    double npu[2]; int64_t wc[2], idle[2], miss[2];
    for (int h = 0; h < 2; h++) {
        run_soc(S, POL_QOS, 32, false, h == 1, 2024, T);
        npu[h] = achieved(S, CNPU); wc[h] = S->wc_viol; idle[h] = S->idle; miss[h] = S->m[CAM].misses;
        printf("  %-4s  NPU ach %.4f  idle %6lld  wc_viol %6lld  CAM miss %lld\n", h ? "hard" : "soft",
               npu[h], (long long)idle[h], (long long)wc[h], (long long)miss[h]);
    }
    CHECK(wc[0] == 0 && wc[1] > 0);                          /* hard 는 의도적으로 non-work-conserving */
    CHECK(npu[1] <= 0.5 + 64.0 / T && npu[1] > 0.45);        /* 장기 상한 = rate + cap/T (보장은 아님) */
    CHECK(npu[0] > npu[1] && miss[0] == 0 && miss[1] == 0);
}

/* ---------- 연습 3: NPU 버스트 길이와 CAM 마감 ---------- */
static int64_t bound_total(int pol, int64_t lmax_other, int urg) {
    if (pol == 0) return (lmax_other - 1) + 8;               /* FP CAM 최상위: 비선점 대기 ≤ L − 1 */
    return (urg - 1) + lmax_other + 8;                       /* QOS: 승격 전 (urg−1) + 진행 중 버스트 */
}
static void ex3(void) {
    printf("== 연습 3: NPU 버스트 L 스윕 — CAM 최악 총지연 예측 vs 측정 (마감 48) ==\n");
    printf("  L_npu | FP_A bound  max  miss | QOS u=8 bound  max  miss | QOS u=adapt  u bound  max  miss\n");
    static const int LS[6] = {16, 24, 32, 40, 48, 64};
    bool sound = true; int64_t miss64[3] = {0, 0, 0};
    for (int i = 0; i < 6; i++) {
        int L = LS[i]; int64_t lo = L > 16 ? L : 16;
        int ua = (int)(48 - 8 - lo + 1); if (ua < 0) ua = 0;
        int64_t bd[3] = { bound_total(0, lo, 0), bound_total(1, lo, 8), bound_total(1, lo, ua) };
        int64_t mx[3], ms[3];
        for (int k = 0; k < 3; k++) {
            run_soc(S, k == 0 ? POL_FP_A : POL_QOS, L, false, false, 2024, 200000);
            if (k == 2) { /* 같은 설정을 u 만 바꿔 다시 */
                mcfg_t c[4]; make_soc(c, L, false);
                sim_init(S, 4, c, ARB_QOS, 200000, 2024);
                for (int m = 0; m < 4; m++) S->qos[m] = 4;
                S->qos[CCPU] = 8; S->urg_after[CAM] = ua;
                S->regulated[CNPU] = true; S->tb[CNPU] = (tb_t){ 64, 0.5, 64 };
                sim_run(S, false);
            }
            mx[k] = S->m[CAM].max_tot; ms[k] = S->m[CAM].misses;
            if (S->m[CAM].max_tot > bd[k]) sound = false;          /* 측정은 언제나 상한 이하 */
            if (bd[k] <= 48 && ms[k] != 0) sound = false;           /* 상한 ≤ 마감 → 미스 0 */
            if (L == 64) miss64[k] = ms[k];
        }
        printf("  %5d | %10lld %4lld %5lld | %13lld %4lld %5lld | %11d %6lld %4lld %5lld\n", L,
               (long long)bd[0], (long long)mx[0], (long long)ms[0], (long long)bd[1], (long long)mx[1],
               (long long)ms[1], ua, (long long)bd[2], (long long)mx[2], (long long)ms[2]);
    }
    CHECK(sound);
    CHECK(miss64[0] > 0 && miss64[1] > 0 && miss64[2] > 0);   /* L 64 면 어떤 중재기도 못 지킨다 */
}

/* ---------- 연습 4: Little 의 법칙 ---------- */
static void ex4(void) {
    printf("== 연습 4: Little 의 법칙 (x = 0.9, T = 100000 후 drain) ==\n");
    for (int a = ARB_FIXED; a <= ARB_DRR; a++) {
        mcfg_t c[3]; make_mix(c, 0.9);
        sim_init(S, 3, c, (arb_t)a, 100000, 3); cfg_policy(S, (arb_t)a); sim_run(S, true);
        int64_t sw = 0, n = 0, drop = 0;
        for (int m = 0; m < 3; m++) { sw += S->m[m].sum_wait; n += S->m[m].grants; drop += S->m[m].dropped; }
        double Lbar = (double)S->q_area / S->t, lam = (double)n / S->t, W = (double)sw / n;
        printf("  %-3s cycles %lld  area %lld  sum wait %lld  | Lq %.4f = lambda %.5f x Wq %.3f = %.4f\n",
               ARB_NAME[a], (long long)S->t, (long long)S->q_area, (long long)sw, Lbar, lam, W, lam * W);
        CHECK(S->q_area == sw && drop == 0 && fabs(Lbar - lam * W) < 1e-9);
    }
}

int main(void) {
    S = calloc(1, sizeof *S);
    if (!S) return 1;
    ex1(); ex2(); ex3(); ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    free(S);
    return g_pass == g_total ? 0 : 1;
}
