/* BM-A10 데모 — 모델 스윕 표 (사이클, 모델 값 — 측정 아님).
 * solution.c 의 모델·시뮬레이터를 그대로 가져다 쓴다 (같은 레슨 안의 #include).
 * 빌드 (레슨 디렉터리에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/BM-A10_demo && /tmp/BM-A10_demo
 */
#define main solution_main
#include "solution.c"
#undef main

static void timeline(const char *title, const cm_cfg_t *c, const irq_ev_t *ev, int n)
{
    sim_out_t o;
    if (sim_run(&CM4, c, ev, n, &o)) { printf("%s: sim failed\n", title); return; }
    printf("%s\n", title);
    for (int k = 0; k < o.n_done; ++k) {
        int i = o.order[k];
        printf("   IRQ%d prio %u arrive %3u  start %3u (%-6s) latency %2u  end %3u\n", i, (unsigned)ev[i].prio,
               (unsigned)ev[i].t_arrive, (unsigned)o.t_start[i], HOW_NAME[o.how[i]],
               (unsigned)(o.t_start[i] - ev[i].t_arrive), (unsigned)o.t_end[i]);
    }
    printf("   thread resumes at %u, exits=%u tail=%u late=%u pop=%u max depth=%u\n", (unsigned)o.t_thread,
           (unsigned)o.n_exit, (unsigned)o.n_tail, (unsigned)o.n_late, (unsigned)o.n_pop, (unsigned)o.max_depth);
}

int main(void)
{
    cm_cfg_t c0 = cfg(0, 0, 0, 0, 1), c5 = cfg(5, 0, 0, 0, 1);
    printf("== late arrival: low (prio 2) arrives t=10, high (prio 1) at t=10+d — high's latency / who starts first\n");
    printf("    d | WS0 lat  order | WS5-miss lat  order\n");
    for (uint32_t d = 0; d <= 26; d += 2) {
        irq_ev_t ev[2] = { { 10, 2, 20, 0 }, { 10 + d, 1, 20, 0 } };
        sim_out_t a, b;
        sim_run(&CM4, &c0, ev, 2, &a); sim_run(&CM4, &c5, ev, 2, &b);
        printf("   %2u | %3u     %-6s | %3u          %-6s\n", (unsigned)d,
               (unsigned)(a.t_start[1] - ev[1].t_arrive), HOW_NAME[a.how[1]],
               (unsigned)(b.t_start[1] - ev[1].t_arrive), HOW_NAME[b.how[1]]);
    }
    printf("\n== pop pre-emption: B arrives d cycles after A's BX LR (A alone: exit takes 12)\n");
    printf("    d | B latency  how    | without pop (wait exit, then entry)\n");
    for (uint32_t d = 0; d <= 14; d += 2) {
        uint32_t lat = latency_model(&CM4, &c0, SC_POP, d);
        uint32_t nopop = d < 12 ? (12 - d) + 12 : 12;
        irq_ev_t ev[2] = { { 10, 1, 20, 0 }, { 42 + d, 1, 20, 0 } };
        sim_out_t o;
        sim_run(&CM4, &c0, ev, 2, &o);
        printf("   %2u | %3u        %-6s | %3u\n", (unsigned)d, (unsigned)lat, HOW_NAME[o.how[1]], (unsigned)nopop);
    }
    printf("\n== timelines (WS0)\n");
    irq_ev_t t1[3] = { { 10, 1, 20, 0 }, { 10, 1, 20, 0 }, { 10, 1, 20, 0 } };
    timeline("three same-priority IRQs pended together (tail-chaining):", &c0, t1, 3);
    irq_ev_t t2[2] = { { 10, 2, 30, 0 }, { 15, 1, 20, 0 } };
    timeline("low at 10, high at 15 (late arrival):", &c0, t2, 2);
    irq_ev_t t3[2] = { { 10, 2, 30, 0 }, { 30, 1, 20, 0 } };
    timeline("low at 10, high at 30 (nested pre-emption):", &c0, t3, 2);
    cm_cfg_t lz = cfg(0, 0, 0, 1, 1), full = cfg(0, 0, 0, 1, 0);
    irq_ev_t t4[1] = { { 10, 1, 20, 1 } };
    timeline("FPCA=1, LSPEN=1, ISR uses FP (lazy save on first FP instr):", &lz, t4, 1);
    timeline("FPCA=1, LSPEN=0, ISR uses FP (full save at entry):", &full, t4, 1);
    return 0;
}
