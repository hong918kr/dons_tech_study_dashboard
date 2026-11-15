/* demo_soak.c — solution.c 를 그대로 include 해서 긴 soak 를 돈다 (COMM-A11).
 *   표 1: 장애 빈도(평균 간격)별 — 샘플 하나하나가 어디로 갔나, 컴포넌트별 FAULT 시간, 최대 회복 시간
 *   표 2: 장애 종류 하나만 켰을 때 — 어느 장애가 어느 손실을 만드나
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_soak.c -o demo_soak && ./demo_soak
 */
#define main solution_main
#include "solution.c"
#undef main

#define SOAK_T  50000u
#define QUIET_T 3000u
static const uint32_t SEEDS[4] = { 0x1234567u, 0xC0FFEEu, 0xBADC0DEu, 0x5EED1234u };

typedef struct {
    double produced, slots, misses, logged, log_dropped, burned, acked, failed, tlm_dropped, retx;
    double fault_ticks[NCOMP], rec_max[NCOMP];
    uint32_t inv_fail, final_bad, not_ok_after_quiet, nfaults;
} Agg;

static void one_run(uint32_t seed, uint32_t gap, int only_kind, Agg *a) {
    RunStat rs = { 0, 0, NULL };
    world_init(&g_w, seed);
    if (gap) faults_random(&g_w, seed, 100, SOAK_T, gap);
    if (only_kind >= 0) {                                   /* 한 종류만 남긴다 */
        uint32_t k = 0;
        for (uint32_t i = 0; i < g_w.nf; i++) if (g_w.f[i].kind == only_kind) g_w.f[k++] = g_w.f[i];
        g_w.nf = k;
    }
    a->nfaults += g_w.nf;
    run_ticks(&g_w, SOAK_T, &rs);
    const Hub *h = &g_w.hub;
    for (int c = 0; c < NCOMP; c++) {
        a->fault_ticks[c] += h->hl[c].ticks_in[H_FAULT];
        if (h->hl[c].rec_max > a->rec_max[c]) a->rec_max[c] = h->hl[c].rec_max;
    }
    run_ticks(&g_w, QUIET_T, &rs);
    if (hub_overall(h) != H_OK) a->not_ok_after_quiet++;
    drain(&g_w, 8000, &rs);
    a->produced += h->produced;
    a->slots += h->sens[0].slots + h->sens[1].slots;
    a->misses += h->sens[0].misses + h->sens[1].misses;
    a->logged += h->lg.logged; a->log_dropped += h->lg.dropped; a->burned += h->lg.burned;
    a->acked += h->tl.acked; a->failed += h->tl.failed; a->tlm_dropped += h->tl.dropped; a->retx += h->tl.retx;
    a->inv_fail += rs.inv_fail;
    a->final_bad += final_check(&g_w, false);
}

static void print_header(const char *first) {
    printf("| %s | faults | miss %% | logged %% | log drop %% | burned | acked %% | failed %% | tlm drop %% | retx/msg | FAULT %% i2c/tmp/hum/nor/uart | max rec (tick) i2c/tmp/hum/nor/uart | inv/final/notOK |\n", first);
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
}
static void print_row(const char *label, const Agg *a, uint32_t runs) {
    double ticks = (double)SOAK_T * runs;
    printf("| %s | %u | %.1f | %.1f | %.1f | %.0f | %.1f | %.2f | %.1f | %.3f | %.1f/%.1f/%.1f/%.1f/%.1f | %.0f/%.0f/%.0f/%.0f/%.0f | %u/%u/%u |\n",
           label, a->nfaults, 100.0 * a->misses / a->slots, 100.0 * a->logged / a->produced, 100.0 * a->log_dropped / a->produced,
           a->burned, 100.0 * a->acked / a->produced, 100.0 * a->failed / a->produced, 100.0 * a->tlm_dropped / a->produced,
           a->acked > 0 ? a->retx / (a->acked + a->failed) : 0.0,
           100.0 * a->fault_ticks[0] / ticks, 100.0 * a->fault_ticks[1] / ticks, 100.0 * a->fault_ticks[2] / ticks,
           100.0 * a->fault_ticks[3] / ticks, 100.0 * a->fault_ticks[4] / ticks,
           a->rec_max[0], a->rec_max[1], a->rec_max[2], a->rec_max[3], a->rec_max[4], a->inv_fail, a->final_bad, a->not_ok_after_quiet);
}

int main(void) {
    printf("soak: %u seeds x %u ticks (+%u quiet + drain), 1 tick = 1 ms\n\n", 4u, SOAK_T, QUIET_T);
    printf("Table 1 — all fault kinds, mean gap between fault starts\n\n");
    print_header("mean gap");
    const uint32_t gaps[] = { 0, 2000, 800, 300, 150 };
    for (unsigned g = 0; g < sizeof gaps / sizeof gaps[0]; g++) {
        Agg a; memset(&a, 0, sizeof a);
        for (int s = 0; s < 4; s++) one_run(SEEDS[s], gaps[g], -1, &a);
        char label[32];
        if (gaps[g]) snprintf(label, sizeof label, "%u", gaps[g]); else snprintf(label, sizeof label, "none");
        print_row(label, &a, 4);
    }
    printf("\nTable 2 — one fault kind only, mean gap 300 (same random schedule, other kinds removed)\n\n");
    print_header("kind");
    for (int k = 0; k < F_KINDS; k++) {
        Agg a; memset(&a, 0, sizeof a);
        for (int s = 0; s < 4; s++) one_run(SEEDS[s], 300, k, &a);
        print_row(F_NAME[k], &a, 4);
    }
    return 0;
}
