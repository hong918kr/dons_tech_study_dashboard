/* ARCH-A05 추가 연습 — solution.c 의 하네스를 그대로 가져와(main 이름만 바꿔 포함) 연습 1~4 의 답을 검증한다.
 *   연습 1  IRIW · 2+2W: SC 인터리빙 수와 결과 히스토그램, 실제 스레드 seq_cst 는 이상한 결과 0
 *   연습 2  "0 번 관찰" 의 통계: 0/n 일 때 95% 상한 (rule of three), 필요한 반복 수
 *   연습 3  하네스 함정: 배치 사이에 셀을 안 지우면 (fresh variable 아님) 결과가 가려진다 — 강제 스케줄로 정확히
 *   연습 4  회귀 게이트에 이 맥에서 실측한 숫자(arm64 · Rosetta 2)를 넣기, 그리고 "모델을 바꾸면 버그"
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main
#include <math.h>

/* ---- 연습 2 ---- */
/* n 번 중 0 번 나왔을 때, 진짜 확률 p 의 95% 신뢰 상한: (1-p)^n = 0.05 → p = 1 - 0.05^(1/n) ≈ 3/n */
static double upper95_zero(double n) { return 1.0 - pow(0.05, 1.0 / n); }
/* 확률 p 인 사건을 conf 확률로 최소 1 번 보려면 몇 번 돌려야 하나: 1-(1-p)^n >= conf */
static double iters_needed(double p, double conf) { return ceil(log(1.0 - conf) / log1p(-p)); }

/* ---- 연습 4: 실측 (demo_litmus 1000000 time 3 회 합 = 300만 회, uptime load 7.5–8.8) ---- */
static const lit_obs ARM_M6[] = {
    {T_SB, V_RLX, 1037561, 3000000}, {T_SB, V_ACQREL, 1553, 3000000}, {T_SB, V_SC, 0, 3000000}, {T_SB, V_FENCE, 0, 3000000},
    {T_MP, V_RLX, 102463, 3000000},  {T_MP, V_ACQREL, 0, 3000000},    {T_MP, V_SC, 0, 3000000}, {T_MP, V_FENCE, 0, 3000000},
    {T_LB, V_RLX, 0, 3000000},       {T_LB, V_ACQREL, 0, 3000000},    {T_LB, V_SC, 0, 3000000}, {T_LB, V_FENCE, 0, 3000000},
    {T_IRIW, V_RLX, 4650, 3000000},  {T_IRIW, V_ACQREL, 0, 3000000},  {T_IRIW, V_SC, 0, 3000000}, {T_IRIW, V_FENCE, 0, 3000000},
    {T_22W, V_RLX, 244801, 3000000}, {T_22W, V_ACQREL, 0, 3000000},   {T_22W, V_SC, 0, 3000000}, {T_22W, V_FENCE, 0, 3000000},
};
static const lit_obs X86_ROSETTA[] = {
    {T_SB, V_RLX, 2322063, 3000000}, {T_SB, V_ACQREL, 2869751, 3000000}, {T_SB, V_SC, 0, 3000000}, {T_SB, V_FENCE, 0, 3000000},
    {T_MP, V_RLX, 0, 3000000},       {T_MP, V_ACQREL, 0, 3000000},       {T_MP, V_SC, 0, 3000000}, {T_MP, V_FENCE, 0, 3000000},
    {T_LB, V_RLX, 0, 3000000},       {T_LB, V_ACQREL, 0, 3000000},       {T_LB, V_SC, 0, 3000000}, {T_LB, V_FENCE, 0, 3000000},
    {T_IRIW, V_RLX, 0, 3000000},     {T_IRIW, V_ACQREL, 0, 3000000},     {T_IRIW, V_SC, 0, 3000000}, {T_IRIW, V_FENCE, 0, 3000000},
    {T_22W, V_RLX, 0, 3000000},      {T_22W, V_ACQREL, 0, 3000000},      {T_22W, V_SC, 0, 3000000}, {T_22W, V_FENCE, 0, 3000000},
};
#define NOBS ((int)(sizeof ARM_M6 / sizeof ARM_M6[0]))
static int count_verdict(const lit_obs *o, int n, int hw, verdict_t want) {
    int c = 0;
    for (int i = 0; i < n; i++)
        c += lit_verdict(o[i].weird, ALLOW[o[i].test][o[i].V][MD_C11], ALLOW[o[i].test][o[i].V][hw]) == want;
    return c;
}

int main(void) {
    /* 연습 1: IRIW (4 스레드) · 2+2W (최종값) */
    static uint8_t sc[MAXSCHED][MAXSTEPS];
    hist_t iriw = {{0}, 0}, w22 = {{0}, 0};
    CHECK(lit_enum_scheds(&TESTS[T_IRIW], sc, MAXSCHED) == 180);
    CHECK(lit_sc_hist(&TESTS[T_IRIW], &iriw) == 180 && iriw.total == 180);
    CHECK(iriw.cnt[TESTS[T_IRIW].weird] == 0 && distinct(&iriw) == 15);          /* 16 개 중 weird 하나만 빠짐 */
    CHECK(lit_sc_hist(&TESTS[T_22W], &w22) == 6);
    CHECK(w22.cnt[K2(2, 1)] == 1 && w22.cnt[K2(2, 2)] == 4 && w22.cnt[K2(1, 2)] == 1 && w22.cnt[K2(1, 1)] == 0);
    for (int test = T_IRIW; test <= T_22W; test++) {                              /* C11 이 금지: seq_cst */
        hist_t h = {{0}, 0}, scs = {{0}, 0};
        lit_sc_hist(&TESTS[test], &scs);
        CHECK(run_simple(test, V_SC, M_TIME, N_CHECK, &h) == 0 && h.total == N_CHECK &&
              h.cnt[TESTS[test].weird] == 0 && outside_sc(&h, &scs) == 0);
    }
    hist_t ia = {{0}, 0};                     /* IRIW acq/rel: C11 은 허용, ARMv8(MCA) 은 금지 → 정보만 */
    CHECK(run_simple(T_IRIW, V_ACQREL, M_TIME, N_CHECK, &ia) == 0);
    printf("info  IRIW acq/rel weird=%llu / %u (C11 allows, ARMv8 other-multi-copy-atomic forbids)\n",
           (unsigned long long)ia.cnt[TESTS[T_IRIW].weird], N_CHECK);

    /* 연습 2: 0/n 의 의미 */
    double u = upper95_zero(1e6);
    CHECK(u > 2.99e-6 && u < 3.00e-6);                                           /* ≈ 3/n */
    CHECK(fabs(upper95_zero(1e7) * 1e7 - 2.9957) < 1e-3);                        /* LB 1000만 번 0 → p < 3e-7 */
    CHECK(iters_needed(1e-6, 0.95) == 2995731.0);                                /* 확률 1e-6 을 95% 로 보려면 */
    CHECK(iters_needed(5.2e-4, 0.99) == 8854.0);                                 /* SB acq/rel 실측 비율 정도면 */

    /* 연습 3: 배치 사이 셀을 안 지우면 — 첫 배치만 정상, 나머지는 옛 값 1 이 남아 전부 (1,1) */
    {
        size_t ns = lit_enum_scheds(&TESTS[T_SB], sc, MAXSCHED);                  /* 6 */
        hist_t h = {{0}, 0};
        lit_run_t R = { .T = &TESTS[T_SB], .V = V_SC, .mode = M_FORCED, .reset = false, .n = ns * 100, .batch = ns * 10,
                        .sched = (const uint8_t (*)[MAXSTEPS])sc, .nsched = ns };
        CHECK(lit_run(&R, &h) == 0 && h.total == 600);
        CHECK(h.cnt[K2(1, 1)] == 40 + 540 && h.cnt[K2(0, 1)] == 10 && h.cnt[K2(1, 0)] == 10);
        hist_t m = {{0}, 0};                                                     /* MP: 옛 flag=1, data=1 */
        lit_run_t R2 = { .T = &TESTS[T_MP], .V = V_SC, .mode = M_FORCED, .reset = false, .n = ns * 100, .batch = ns * 10,
                         .sched = (const uint8_t (*)[MAXSTEPS])sc, .nsched = ns };
        size_t nm = lit_enum_scheds(&TESTS[T_MP], sc, MAXSCHED);
        CHECK(nm == 6 && lit_run(&R2, &m) == 0 && m.cnt[K2(1, 1)] == 10 + 540 && m.cnt[K2(0, 1)] == 40 && m.cnt[K2(0, 0)] == 10);
    }

    /* 연습 4: 실측을 게이트에 */
    int first = -1;
    CHECK(lit_gate(ARM_M6, NOBS, MD_ARM, &first) == 0);
    CHECK(lit_gate(X86_ROSETTA, NOBS, MD_TSO, &first) == 0);
    CHECK(count_verdict(ARM_M6, NOBS, MD_ARM, VD_SEEN) == 5 && count_verdict(ARM_M6, NOBS, MD_ARM, VD_UNSEEN) == 1);
    CHECK(count_verdict(X86_ROSETTA, NOBS, MD_TSO, VD_SEEN) == 2);
    first = -1;
    CHECK(lit_gate(ARM_M6, NOBS, MD_TSO, &first) == 3 && first == 4);            /* "x86 에서 되던 코드" 기준이면 */
    printf("info  ARM data judged by TSO model: first bug = %s/%s weird=%llu\n", TESTS[ARM_M6[first].test].name,
           VNAME[ARM_M6[first].V], (unsigned long long)ARM_M6[first].weird);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
