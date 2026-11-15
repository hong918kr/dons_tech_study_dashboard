/* ARCH-A08 demo — 사이클당 에너지 곡선 E/cycle(f) 와 연속 critical frequency
 *
 * solution.c 의 모델을 그대로 가져와(main 이름만 바꿔 포함), OPP 사이의 전압을 선형 보간(가정)해
 * 300–1800 MHz 를 10 MHz 간격으로 훑는다. 출력:
 *   1) 곡선별 연속 최소점 (critical frequency) 과 그 점의 E/cycle
 *   2) 레슨 md 의 svg 에 그대로 붙인 polyline 좌표 (x: 300→1800 MHz = 60→700 px, y: 0→0.9 nJ = 300→30 px)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_energy_curve.c -o demo && ./demo   (solution.c 와 같은 폴더)
 */
#define main solution_main
#include "solution.c"
#undef main

static double v_interp(double f) {               /* OPP 사이 선형 보간 (가정: 실제 V-f 곡선은 칩마다 측정·퓨즈) */
    if (f <= OPP5[0].f_hz) return OPP5[0].v;
    for (int i = 1; i < NOPP; i++)
        if (f <= OPP5[i].f_hz) {
            double t = (f - OPP5[i - 1].f_hz) / (OPP5[i].f_hz - OPP5[i - 1].f_hz);
            return OPP5[i - 1].v + t * (OPP5[i].v - OPP5[i - 1].v);
        }
    return OPP5[NOPP - 1].v;
}

static double px_x(double f) { return 60.0 + (f - 300e6) / 1500e6 * 640.0; }
static double px_y(double e) { return 300.0 - e / 0.9e-9 * 270.0; }

int main(void) {
    pmodel_t base = model_default();
    pmodel_t ideal = base; ideal.leak_scale = 0; ideal.p_static = 0;
    pmodel_t hot = base; hot.temp_c = 85;
    pmodel_t hot2 = base; hot2.temp_c = 105;
    const char *name[4] = {"leak=0 static=0", "base 25C", "base 85C", "base 105C"};
    const pmodel_t *ms[4] = {&ideal, &base, &hot, &hot2};
    printf("curve             f_crit[MHz]  V_crit  E/cycle_min[nJ]  E/cycle@1800[nJ]  E/cycle@300[nJ]\n");
    for (int k = 0; k < 4; k++) {
        double best_f = 0, best_e = 1;
        for (double f = 300e6; f <= 1800e6 + 1; f += 10e6) {
            double e = e_per_cycle(ms[k], v_interp(f), f);
            if (e < best_e) { best_e = e; best_f = f; }
        }
        printf("%-16s  %8.0f     %.3f   %.4f           %.4f            %.4f\n", name[k], best_f / 1e6, v_interp(best_f),
               best_e * 1e9, e_per_cycle(ms[k], 1.0, 1800e6) * 1e9, e_per_cycle(ms[k], 0.6, 300e6) * 1e9);
    }
    printf("\nsvg polylines (step 50 MHz, 105C 는 0.9 nJ 를 넘어 생략):\n");
    for (int k = 0; k < 3; k++) {
        printf("%s: ", name[k]);
        for (double f = 300e6; f <= 1800e6 + 1; f += 50e6)
            printf("%.0f,%.0f ", px_x(f), px_y(e_per_cycle(ms[k], v_interp(f), f)));
        printf("\n");
    }
    return 0;
}
