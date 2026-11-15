/* ARCH-B10 demo — 민감도 표 · 클럭 스케일링 표 · 설계 옵션 비교를 출력한다 (CHECK 없음, 표 출력용)
 *
 * 숫자는 교과서적 예시 파라미터다 (이 맥에서 잰 지연이 아님 — 실측은 MEM-B10).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sensitivity.c -o demo && ./demo
 */
#include <stdio.h>

typedef struct { const char *name; double t, h; } level_t;

static double amat(const level_t *L, int n) {
    double acc = L[n - 1].t;
    for (int k = n - 2; k >= 0; k--) acc = L[k].t + (1.0 - L[k].h) * acc;
    return acc;
}
static double reach(const level_t *L, int k) {
    double r = 1.0;
    for (int j = 0; j < k; j++) r *= 1.0 - L[j].h;
    return r;
}
static double with_h(const level_t *L, int n, int k, double h) {
    level_t c[8];
    for (int j = 0; j < n; j++) c[j] = L[j];
    c[k].h = h > 1.0 ? 1.0 : h;
    return amat(c, n);
}
static double with_t(const level_t *L, int n, int k, double t) {
    level_t c[8];
    for (int j = 0; j < n; j++) c[j] = L[j];
    c[k].t = t;
    return amat(c, n);
}

static void table(const char *title, const level_t *L, int n) {
    double a = amat(L, n);
    printf("\n== %s : AMAT = %.4f cycles ==\n", title, a);
    printf("level  t(cyc)  local_h  reach    global_miss  contrib  share  | dLocal+1pp  dGlobal+1pp  d(-1cyc)\n");
    for (int k = 0; k < n; k++) {
        double r = reach(L, k), gm = reach(L, k + 1), c = r * L[k].t;
        printf("%-5s  %6.0f  %7.3f  %7.4f  %11.4f  %7.3f  %4.1f%% |",
               L[k].name, L[k].t, L[k].h, r, gm, c, 100.0 * c / a);
        if (k < n - 1) {
            double dl = with_h(L, n, k, L[k].h + 0.01) - a;
            double dg = r > 0 ? with_h(L, n, k, L[k].h + 0.01 / r) - a : 0.0;
            printf("  %+9.4f  %+11.4f", dl, dg);
        } else {
            printf("  %9s  %11s", "-", "-");
        }
        printf("  %+8.4f\n", with_t(L, n, k, L[k].t - 1) - a);
    }
}

int main(void) {
    const level_t A[] = {{"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0}};
    const level_t B[] = {{"L1", 4, 0.50}, {"L2", 12, 0.95}, {"L3", 40, 0.50}, {"DRAM", 200, 1.0}};
    const level_t C[] = {{"L1", 4, 0.98}, {"L2", 12, 0.50}, {"L3", 40, 0.20}, {"DRAM", 200, 1.0}};
    table("A: typical", A, 4);
    table("B: working set > L1, fits L2", B, 4);
    table("C: hot loop + cold random tail", C, 4);

    /* 클럭 스케일링: 캐시는 cycle 고정, DRAM 은 100 ns 고정 */
    printf("\n== clock scaling (L1 4c/90%%, L2 12c/70%%, L3 40c/60%%, DRAM 100 ns; L1I 99%%; base CPI 1.0, 0.35 data refs/instr) ==\n");
    printf("f(GHz)  DRAM(cyc)  AMAT_D   AMAT_I   CPI     time(us,1M instr)  speedup_vs_1GHz\n");
    const double fs[] = {1.0, 0.5, 2.0, 3.0, 4.0};   /* 1 GHz 를 먼저 계산해 기준으로 */
    double t_ref = 0.0;
    for (int i = 0; i < 5; i++) {
        double f = fs[i], dram = 100.0 * f;
        level_t D[] = {{"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", dram, 1.0}};
        level_t I[] = {{"L1I", 4, 0.99}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", dram, 1.0}};
        double ad = amat(D, 4), ai = amat(I, 4);
        double cpi = 1.0 + 1.0 * (ai - 4) + 0.35 * (ad - 4);
        double us = 1e6 * cpi / (f * 1000.0);
        if (i == 0) t_ref = us;
        printf("%5.1f  %9.0f  %7.3f  %7.3f  %6.3f  %17.1f  %15.3f\n", f, dram, ad, ai, cpi, us, t_ref / us);
    }

    /* 설계 옵션: 같은 면적/비용이면 무엇을 고르나 (AMAT 기준) */
    printf("\n== design options vs A (AMAT %.3f) ==\n", amat(A, 4));
    struct { const char *what; level_t L[4]; } opt[] = {
        {"bigger L1: 5c / 93%",      {{"L1", 5, 0.93}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0}}},
        {"faster L2: 10c",           {{"L1", 4, 0.90}, {"L2", 10, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0}}},
        {"bigger L3: 75% local",     {{"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.75}, {"DRAM", 200, 1.0}}},
        {"faster DRAM: 160c",        {{"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 160, 1.0}}},
    };
    for (unsigned i = 0; i < sizeof opt / sizeof opt[0]; i++)
        printf("  %-24s AMAT %.3f  (%+.3f)\n", opt[i].what, amat(opt[i].L, 4), amat(opt[i].L, 4) - amat(A, 4));
    return 0;
}
