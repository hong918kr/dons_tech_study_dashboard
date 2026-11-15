/* BM-M03 demo: 두 디바운서의 한계를 쓸어 본다 (solution.c 의 모델·드라이버 그대로).
 *   (1) 글리치 길이 × 위상(0.1 ms 단위 10가지) → 누름으로 잘못 잡힌 비율
 *   (2) 바운스 길이(무작위 튐) → 이벤트 열이 틀린 비율 + 평균 지연
 * 레슨 폴더에서: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/BM-M03_sweep && /tmp/BM-M03_sweep
 */
#define main solution_main
#include "solution.c"
#undef main

int main(void) {
    static const uint32_t Ls[] = { 5, 10, 50, 89, 90, 91, 92, 95, 99, 100, 150, 199, 200, 201, 250 };
    printf("(1) 글리치 (놓인 버튼이 L 동안 0) — 위상 10가지 중 PRESS 가 나온 수\n");
    printf("   L(ms)   EXTI+재확인(20 ms)   샘플링(1 ms x 10)\n");
    for (unsigned i = 0; i < sizeof Ls / sizeof Ls[0]; i++) {
        uint32_t L = Ls[i];
        int de = 0, ds = 0;
        for (uint32_t ph = 0; ph < 10; ph++) {
            uint32_t t0 = 1000 + ph;
            sig_reset(t0 + L + 800, 1);
            sig_edge(t0, 0, 0); sig_edge(t0 + L, 1, 0);
            sim_start(RUN_EXTI | RUN_SAMP, false);
            sim_run_until(g_sig_len - 1);
            de += ev_count('E', EV_PRESS) > 0;
            ds += ev_count('S', EV_PRESS) > 0;
        }
        printf("   %5.1f        %2d/10                %2d/10\n", L / 10.0, de, ds);
    }

    static const uint32_t Bs[] = { 0, 20, 50, 80, 100, 150, 190, 200, 250, 300, 400, 600 };
    printf("(2) 바운스 길이 B (0.1 ms 마다 무작위 0/1, 누름·뗌 둘 다) — 600 ms 누름 300회\n");
    printf("   B(ms)  E 틀림  S 틀림  E 지연 평균(ms)  S 지연 평균(ms)  E ISR/회  (지연 = 첫 엣지 → PRESS)\n");
    for (unsigned i = 0; i < sizeof Bs / sizeof Bs[0]; i++) {
        uint32_t B = Bs[i];
        int be = 0, bs = 0, ne = 0, ns = 0;
        double le = 0, ls = 0, isr = 0;
        t_rng = 0xB0B0u + B;
        for (int it = 0; it < 300; it++) {
            uint32_t t0 = 1000 + rnd(10);
            sig_reset(t0 + 6000 + B + 1500, 1);
            sig_edge(t0, 0, B); sig_edge(t0 + 6000, 1, B);
            sim_start(RUN_EXTI | RUN_SAMP, false);
            sim_run_until(g_sig_len - 1);
            const ev_t *e0 = ev_nth('E', 0), *e1 = ev_nth('E', 1), *s0 = ev_nth('S', 0), *s1 = ev_nth('S', 1);
            bool eok = ev_count('E', 0) == 2 && e0->type == EV_PRESS && e1->type == EV_RELEASE;
            bool sok = ev_count('S', 0) == 2 && s0->type == EV_PRESS && s1->type == EV_RELEASE;
            if (!eok) be++; else { le += e0->t - t0; ne++; }
            if (!sok) bs++; else { ls += s0->t - t0; ns++; }
            isr += g_isr_calls[IRQ_EXTI15_10] + g_isr_calls[IRQ_TIM7];
        }
        printf("   %5.1f  %3d     %3d     %8.2f         %8.2f        %5.2f\n", B / 10.0, be, bs,
               ne ? le / ne / 10.0 : 0.0, ns ? ls / ns / 10.0 : 0.0, isr / 300.0);
    }
    return 0;
}
