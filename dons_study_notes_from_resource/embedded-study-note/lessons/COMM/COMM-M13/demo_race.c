/* COMM-M13 demo_race.c — spi_submit 에서 크리티컬 섹션을 빼면 무엇이 깨지나
 * solution.c 를 그대로 포함하고(main 이름만 바꿈), 마스크 없는 submit 을 하네스에 바꿔 끼운다.
 * (1) 결정적 재현: q_push 가운데서 완료 ISR 이 끼어드는 한 장면 → 큐에 "유령" 디스크립터
 * (2) 선점 확률별 랜덤 스트레스: 불변식 위반 비트, 콜백 수 불일치, 남는 콜백
 * 빌드: cc -std=c11 -O2 demo_race.c -o demo_race && ./demo_race
 */
#define main solution_main
#include "solution.c"
#undef main

static int submit_nocrit(SpiBus *b, SpiXfer *x) {          /* spi_submit 과 같고 crit_enter/exit 만 없다 */
    if (b == NULL || x == NULL || x->cb == NULL || x->len == 0u || x->len > SPI_MAX_LEN ||
        x->cs >= SPI_N_CS || x->prio >= SPI_NPRIO || x->timeout == 0u)
        return SPI_E_ARG;
    if (b->stopped) return SPI_E_STOPPED;
    if (x->state != XS_IDLE) return SPI_E_BUSY;
    if (!q_push(&b->q[x->prio], x)) { b->n_qfull++; return SPI_E_QFULL; }
    x->seq = b->seq_next++;
    x->state = XS_QUEUED;
    spi_log(b, EV_SUBMIT, x);
    spi_start_next(b);
    return SPI_OK;
}

static void one_scene(int use_crit) {
    world_reset();
    g_submit_fn = use_crit ? spi_submit : submit_nocrit;
    job_config(&g_jobs[0], 0, 1, 1, 100);       /* A: 1 바이트 → 3 tick */
    job_config(&g_jobs[1], 1, 1, 4, 100);       /* C: 대기 중 */
    job_config(&g_jobs[2], 2, 1, 4, 100);       /* B: 지금 제출할 것 */
    job_config(&g_jobs[9], 3, 1, 4, 100);       /* Z: 제출한 적 없음 — 링 슬롯에 남은 옛 포인터 역할 */
    (void)job_submit(&g_jobs[0]); (void)job_submit(&g_jobs[1]);
    g_bus.q[1].slot[3] = &g_jobs[9].x;          /* 예전에 쓰고 남은 슬롯 값 (링 버퍼는 지우지 않는다) */
    hw_tick(); hw_tick();                       /* A 는 1 tick 남음 */
    g_pre_permille = 1000; g_pre_seed = 7;      /* 다음 선점 지점에서 반드시 끼어든다 */
    (void)job_submit(&g_jobs[2]);
    g_pre_permille = 0;
    printf("[%s] after submit(B): q.head=%u q.count=%u active=%s\n", use_crit ? "crit  " : "nocrit",
           (unsigned)g_bus.q[1].head, (unsigned)g_bus.q[1].count,
           g_bus.active == &g_jobs[1].x ? "C" : g_bus.active == &g_jobs[0].x ? "A" : "?");
    run_until_idle(1000);
    printf("         DONE order:");
    for (int i = 0; i < g_order_n; i++) printf(" %c", "ACB      Z"[g_order[i]]);
    printf("   Z: accepted=%u callbacks=%u  extra_cb=%u inv=0x%03x\n", (unsigned)g_jobs[9].accepted,
           (unsigned)g_jobs[9].cbs, (unsigned)g_extra_cb, (unsigned)g_inv_bad);
    g_submit_fn = spi_submit;
}

int main(void) {
    printf("== (1) one deterministic interleaving ==\n");
    one_scene(1);
    one_scene(0);
    printf("\n== (2) random stress, 60000 ticks, same seed per row ==\n");
    printf("preempt/1000 | submit | inv bits | jobs cb!=submit | extra cb | bad rx | order bad | done\n");
    static const uint32_t rates[] = { 0, 10, 50, 100, 300 };
    for (int m = 1; m >= 0; m--)
        for (int r = 0; r < 5; r++) {
            g_submit_fn = m ? spi_submit : submit_nocrit;
            StressStat s = stress(0xC0FFEE13u, 60000u, rates[r]);
            printf("%5u        | %s | 0x%03x    | %6u          | %8u | %6u | %9u | %u\n", (unsigned)rates[r],
                   m ? "crit  " : "nocrit", (unsigned)g_inv_bad, (unsigned)s.mismatch, (unsigned)g_extra_cb,
                   (unsigned)g_bad_rx, (unsigned)g_order_bad, (unsigned)s.done);
        }
    return 0;
}
