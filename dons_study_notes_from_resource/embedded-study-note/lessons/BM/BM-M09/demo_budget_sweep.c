/* BM-M09 demo — 처리 비용 스윕 + 실제 단위 예산 표
 *
 * solution.c 의 모델·드라이버를 그대로 쓴다 (같은 레슨 디렉터리의 파일을 #include, main 만 이름 바꿈).
 * 빌드 (레슨 디렉터리에서):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_budget_sweep.c -o demo_budget_sweep && ./demo_budget_sweep
 */
#define main solution_tests_main
#include "solution.c"
#undef main

int main(void) {
    /* 1) 같은 N=32, S=16 (반쪽 512 클럭) 에서 처리 비용만 바꾼다. 1000 반쪽 주기 동안. */
    const uint32_t n = 32u, s = 16u, halves = 1000u;
    static const uint32_t COST[] = { 480u, 511u, 512u, 513u, 514u, 520u, 527u, 528u, 560u, 640u,
                                     768u, 1023u, 1024u, 1100u, 1536u };
    printf("N=%u S=%u half=%u clk, %u half periods, cost = pre + N*cps\n", (unsigned)n, (unsigned)s,
           (unsigned)(n * s), (unsigned)halves);
    printf(" cost  load%%  pred1st  meas1st  records   OVR  dropped  torn  clean%%  late_entry\n");
    for (unsigned i = 0; i < sizeof COST / sizeof COST[0]; i++) {
        uint32_t c = COST[i], cps = c / n, pre = c - cps * n;
        sim_reset(n, s, pre, cps);
        pipe_start(&g_pipe, n);
        run_until((uint64_t)n * s * halves + 1u, 0u);
        verdict_t v = verify_records(n);
        int32_t first = -1;
        for (uint32_t r = 0; r < g_nrec; r++) if (g_rec[r].flags & REC_OVR) { first = (int32_t)r; break; }
        uint32_t clean = g_nrec - v.ovr;
        printf("%5u  %5.1f  %7d  %7d  %7u  %4u  %7u  %4u  %6.1f  %10u%s\n", (unsigned)c, 100.0 * c / (n * s),
               (int)predict_first_ovr(n, s, c), (int)first, (unsigned)g_nrec, (unsigned)v.ovr,
               (unsigned)(halves - g_nrec), (unsigned)v.torn, 100.0 * clean / halves, (unsigned)g_pipe.late_entries,
               verdict_clean(v) ? "" : "  <-- VERIFY FAIL");
    }

    /* 2) 실제 단위: 168 MHz Cortex-M4, 샘플당 40 사이클 + 반쪽당 600 사이클 (ISR 진입·출력·로그) */
    printf("\nbudget @168 MHz, 40 cyc/sample + 600 cyc/half\n");
    printf("   fs[Hz]     N   half[us]   proc[us]  load[%%]  latency[us]  ok\n");
    static const uint32_t FS[] = { 8000u, 48000u, 200000u, 1000000u, 2400000u };
    static const uint32_t NN[] = { 32u, 256u, 1024u };
    for (int a = 0; a < 5; a++) for (int b = 0; b < 3; b++) {
        budget_in_t in = { FS[a], NN[b], 40u, 600u, 168000000u };
        budget_out_t o;
        budget_calc(&in, &o);
        printf("%9u  %4u  %9.1f  %9.2f  %7.1f  %11.1f  %s\n", (unsigned)FS[a], (unsigned)NN[b], o.half_ns / 1000.0,
               o.proc_ns / 1000.0, o.load_permille / 10.0, o.latency_ns / 1000.0, o.ok ? "yes" : "NO");
    }
    return 0;
}
