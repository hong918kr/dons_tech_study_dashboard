/* OS-A04 관찰용: solution.c 엔진으로 타임라인·스윕을 출력한다 (CHECK 없음).
 *   ① 1 초 잔 C 가 깨어난 직후 타임라인 — 배치 有 / 無
 *   ② nice 차이 k (0 vs k) 별 CPU 몫 비 = 1.25^k 근사
 *   ③ tick(HZ) 별 lag · 퍼짐 · 문맥 전환 수
 *   ④ CFS 시절 period/slice, 6.18 base_slice 의 CPU 수 배율
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace
 */
#define main solution_main
#include "solution.c"
#undef main

static void timeline(int place, uint64_t from, uint64_t step, int cols)
{
    static char buf[2000000];
    spec_t sp[3] = { { .nice = 0 }, { .nice = 0 }, { .nice = 0, .beh = BEH_SLEEP_THEN_HOG, .phase_us = 1000000 } };
    cfg_t c = cfs_defaults(); c.sim_us = 2000000; c.place = place;
    g_trace = buf; g_trace_from = from; g_trace_len = (uint64_t)cols * step;
    sim_init(&S, &c, sp, 3); simulate(&S);
    g_trace = NULL;
    printf("  %-5s |", place == PLACE_CFS ? "CFS" : "NONE");
    for (int k = 0; k < cols; k++) {                 /* 칸 안에서 90% 이상 돈 태스크, 섞였으면 '*' */
        int cnt[4] = { 0 };
        for (uint64_t u = 0; u < step; u++) { char ch = buf[(uint64_t)k * step + u]; cnt[ch == '.' ? 3 : ch - 'A']++; }
        char out = '*';
        for (int j = 0; j < 4; j++) if (cnt[j] * 10 >= (int)step * 9) out = j == 3 ? '.' : (char)('A' + j);
        putchar(out);
    }
    printf("|  C max run %.1f ms, A max wait %.1f ms\n", S.t[2].max_run_us / 1e3, S.t[0].max_wait_us / 1e3);
}

int main(void)
{
    printf("① C(1 초 잠) 깨어난 뒤: 1 글자 = 1 ms, t = 995 .. 1065 ms (칸의 90%% 이상 돈 태스크, 섞이면 *)\n");
    timeline(PLACE_CFS, 995000, 1000, 70);
    timeline(PLACE_NONE, 995000, 1000, 70);
    printf("   1 글자 = 10 ms, t = 0.95 .. 1.65 s\n");
    timeline(PLACE_CFS, 950000, 10000, 70);
    timeline(PLACE_NONE, 950000, 10000, 70);

    printf("\n② nice 0 vs nice k (hog 2 개, 3 s): 몫 비 = w(0)/w(k)\n");
    printf("   k   w(k)   share0  sharek   ratio   w-ratio  1.25^k\n");
    const int ks[] = { 1, 2, 5, 10, 15, 19 };
    for (size_t j = 0; j < sizeof ks / sizeof ks[0]; j++) {
        spec_t sp[2] = { { .nice = 0 }, { .nice = ks[j] } };
        cfg_t c = cfs_defaults();
        sim_init(&S, &c, sp, 2); simulate(&S);
        double p = 1; for (int i = 0; i < ks[j]; i++) p *= 1.25;
        printf("  %2d  %5u   %.4f  %.4f  %7.2f  %7.2f  %6.2f\n", ks[j], S.t[1].lw.weight,
               share(&S, 0), share(&S, 1), share(&S, 0) / share(&S, 1), 1024.0 / S.t[1].lw.weight, p);
    }

    printf("\n③ tick 별 (nice {-5,0,5}, 3 s): HZ 가 낮으면 slice 를 넘겨 달린다\n");
    printf("   tick_us  max|lag|_us  spread_ms  switches  share(-5)\n");
    const uint32_t ticks[] = { 4000, 1000, 250, 100 };
    for (size_t j = 0; j < 4; j++) {
        spec_t sp[3] = { { .nice = -5 }, { .nice = 0 }, { .nice = 5 } };
        cfg_t c = cfs_defaults(); c.tick_us = ticks[j]; c.track_lag = true;
        sim_init(&S, &c, sp, 3); simulate(&S);
        printf("   %6u  %11.0f  %9.2f  %8u  %.4f\n", ticks[j], S.max_lag_us, S.max_spread_ns / 1e6,
               S.switches, share(&S, 0));
    }

    printf("\n④ CFS 시절 period (latency 6 ms, min_gran 0.75 ms, factor 1) 와 nice-0 slice\n   nr :");
    cfg_t c = cfs_defaults();
    for (uint32_t nr = 1; nr <= 12; nr++) printf(" %5u", nr);
    printf("\n   per:");
    for (uint32_t nr = 1; nr <= 12; nr++) printf(" %5.2f", sched_period_us(&c, nr) / 1e3);
    printf("\n   sl :");
    for (uint32_t nr = 1; nr <= 12; nr++) printf(" %5.2f", sched_period_us(&c, nr) / 1e3 / nr);
    printf("  (ms)\n   CPU 수별 배율 1+ilog2(min(n,8)) → 6.18 base_slice = 0.7 ms × 배율:\n  ");
    for (unsigned n = 1; n <= 12; n++) printf(" n=%u:%.1f", n, 0.7 * sysctl_factor(n));
    printf(" (ms)\n");
    return 0;
}
