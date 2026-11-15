/* BM-M03 demo: exti_config 와 두 ISR 의 실제 레지스터 접근 순서를 mock 로그로 찍는다 (5. 풀이의 표).
 * 레슨 폴더에서: cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o /tmp/BM-M03_trace && /tmp/BM-M03_trace
 */
#define main solution_main
#include "solution.c"
#undef main
int main(void) {
    world_reset(false); g_nlog = 0;
    int rc = exti_config(PORT_C, 13, EXTI_BOTH);
    printf("rc=%d\n", rc);
    for (int i = 0; i < g_nlog; i++) printf("%c 0x%08X 0x%08X\n", g_log[i].op, (unsigned)g_log[i].addr, (unsigned)g_log[i].val);
    /* ISR trace on a clean press */
    sig_reset(3000, 1); sig_edge(1000, 0, 0);
    sim_start(RUN_EXTI, false); sim_run_until(999); g_nlog = 0; sim_step();
    printf("-- tick %u EXTI ISR\n", (unsigned)g_tick);
    for (int i = 0; i < g_nlog; i++) printf("%c 0x%08X 0x%08X\n", g_log[i].op, (unsigned)g_log[i].addr, (unsigned)g_log[i].val);
    sim_run_until(1199); g_nlog = 0; sim_step();
    printf("-- tick %u TIM7 ISR\n", (unsigned)g_tick);
    for (int i = 0; i < g_nlog; i++) printf("%c 0x%08X 0x%08X\n", g_log[i].op, (unsigned)g_log[i].addr, (unsigned)g_log[i].val);
    return 0;
}
