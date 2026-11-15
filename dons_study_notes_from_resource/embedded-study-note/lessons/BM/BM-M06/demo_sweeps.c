/* BM-M06 데모 — 무거운 스윕 (solution.c 의 모델·드라이버를 그대로 쓴다, main 만 뺌)
 *   1) PSC 트레이드오프: tick 분해능 vs overflow 인터럽트 빈도 vs 한 주기/평균 측정 오차 (ppm)
 *   2) ISR 지연 한계: 랩 경계 전수 (CCR 값 65536 개 × 지연 L) → 처음 틀리는 L
 *   3) ISR 예산: ISR 1회 비용 C 클럭일 때 오버캡처 없이 잴 수 있는 최소 주기 (듀티 / 주파수만 /1 / /8)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweeps.c -o demo_sweeps && ./demo_sweeps   (레슨 폴더에서)
 */
#define BM_M06_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

static double g_worst_ppm;
static uint32_t g_nper;
static void isr_err(void) {
    uint32_t before = g_ic.seq;
    TIM3_IRQHandler();
    if (g_ic.seq != before && g_ic.valid && g_ic.rises >= 2u) {
        double clocks = (double)g_ic.period * ((double)g_cur_psc + 1.0) / (double)g_cur_div;
        double truth = (double)g_cur_F / (double)g_cur_T;
        double ppm = (clocks - truth) / truth * 1e6;
        if (ppm < 0) ppm = -ppm;
        if (ppm > g_worst_ppm) g_worst_ppm = ppm;
        g_nper++;
    }
}

static void psc_table(void) {
    const uint64_t F = 1000ull * F_TIM;
    const uint32_t pscs[3] = { 0u, 7u, 83u };
    const uint64_t fs[5] = { 1234u, 52346u, 1234567u, 20987654u, 98765432u };   /* mHz */
    printf("== 1) PSC 트레이드오프 (f_tim 84 MHz, 신호 듀티 50 %%, 위상 임의) ==\n");
    printf(" PSC  tick[ns]  랩[ms]   UIF/s  |  f_sig[Hz]   주기[tick]  한주기 최악[ppm]  1/주기[ppm]  평균(전체)[ppm]\n");
    for (int pi = 0; pi < 3; pi++) {
        uint32_t psc = pscs[pi];
        double tick_ns = (psc + 1.0) * 1e9 / F_TIM, wrap_ms = tick_ns * 65536.0 / 1e6;
        for (int fi = 0; fi < 5; fi++) {
            uint64_t T = fs[fi];
            uint32_t n = fi == 0 ? 6u : 200u;
            sim_reset_all();
            ic_init(psc, 0u, true, 0u);
            g_cur_F = F; g_cur_T = T; g_cur_psc = psc; g_cur_div = 1u;
            g_irq[TIM3_IRQn].handler = isr_err;
            g_worst_ppm = 0; g_nper = 0;
            in_add_pwm(1000u, F, T, 777u % T, 5000u, n);
            ic_meas_t a, b;
            uint64_t P = F / T;
            sim_run_until(1000u + P + P / 2u);
            ic_get(&a);
            sim_run_until(1000u + (uint64_t)n * P);
            ic_get(&b);
            uint64_t fa = ic_avg_mHz(&a, &b, F_TIM, psc, 1u);
            double avg_ppm = fa ? (double)err_dppm(fa, T) / 10.0 : 0.0;
            double ticks = (double)F / (double)T / (psc + 1.0);
            printf(" %3u  %8.1f  %6.2f  %6.1f  | %10.3f  %11.1f  %16.1f  %11.1f  %+14.2f   (n=%u)\n",
                   psc, tick_ns, wrap_ms, 1000.0 / wrap_ms, (double)T / 1000.0, ticks, g_worst_ppm, 1e6 / ticks,
                   avg_ppm, g_nper);
        }
    }
}

static void latency_sweep(void) {
    printf("\n== 2) ISR 지연 한계: CCR 값 전부(캡처가 overflow 전 65535개 + 후 65536개) × 지연 L ==\n");
    printf("      L[tick]   틀림(overflow 전 캡처)   틀림(overflow 후 캡처)   PSC=0 일 때 L[us]\n");
    const uint64_t Ls[] = { 0u, 16384u, 32766u, 32767u, 32768u, 32769u, 40000u, 65535u };
    for (size_t i = 0; i < sizeof Ls / sizeof Ls[0]; i++) {
        uint64_t L = Ls[i];
        int before = 0, after = 0;
        for (uint64_t d = 1u; d <= 65535u; d++)              /* W=65536: overflow 는 클럭 65536, 캡처는 그 전 */
            if (wrap_case(TIM3_IRQHandler, 65536u, d, L) != 0) before++;
        for (uint64_t d = 1u; d <= 65536u; d++)              /* W=1: overflow 는 클럭 1, 캡처는 그 후 (CCR = d−1) */
            if (wrap_case(TIM3_IRQHandler, 1u, d, L) != 0) after++;
        printf("   %8llu   %22d   %22d   %10.1f\n", (unsigned long long)L, before, after, L * 1e6 / F_TIM);
    }
}

static int overcap_run(uint32_t cost, uint64_t P, uint32_t icpsc, bool duty, double *load) {
    const uint64_t F = 1000ull * F_TIM, T = F / P;
    sim_reset_all();
    g_isr_cost = cost;
    ic_init(0u, icpsc, duty, 0u);
    in_add_pwm(1000u, F, T, 0u, 5000u, 400u);
    sim_run_until(1000u + 400u * P);
    if (load) *load = (double)g_irq[TIM3_IRQn].calls * cost / (double)(400u * P) * 100.0;
    return (int)g_ic.ovc;
}
static void budget_table(void) {
    printf("\n== 3) ISR 예산: 오버캡처 0 으로 400 주기를 잴 수 있는 최소 주기 P [클럭 @84 MHz] ==\n");
    printf("  C[클럭] (us) |  듀티(CH1+CH2)        주파수만 /1          주파수만 IC1PSC=/8   | 20 kHz 에서 CPU: 듀티   /8\n");
    const uint32_t costs[4] = { 42u, 84u, 168u, 336u };
    for (int ci = 0; ci < 4; ci++) {
        uint32_t C = costs[ci];
        printf("  %5u (%4.2f) |", C, C * 1e6 / F_TIM);
        const struct { uint32_t icpsc; bool duty; } modes[3] = { { 0u, true }, { 0u, false }, { 3u, false } };
        for (int mi = 0; mi < 3; mi++) {
            uint64_t Pmin = 0;
            for (uint64_t P = 4u * C; P >= 4u; P--) {          /* 위에서 내려오며 처음 실패하기 직전 */
                if (overcap_run(C, P, modes[mi].icpsc, modes[mi].duty, NULL) != 0) break;
                Pmin = P;
            }
            printf("  P≥%4llu (%7.1f kHz)", (unsigned long long)Pmin, F_TIM / 1000.0 / (double)Pmin);
        }
        double l_duty = 0, l_8 = 0;
        overcap_run(C, 4200u, 0u, true, &l_duty);
        overcap_run(C, 4200u, 3u, false, &l_8);
        printf(" |  %5.1f%%  %5.2f%%\n", l_duty, l_8);
    }
    printf("  (P≥ 칸 = 그 주기에서 CPU 는 이미 ~100%% — 실제 한계는 다른 ISR 몫을 빼고 잡는다)\n");
}

int main(void) {
    psc_table();
    latency_sweep();
    budget_table();
    return 0;
}
