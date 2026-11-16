/* OS-A08 데모: 사용률 스윕과 정책 비교 (엔진 = solution.c)
 *   T1 균등 랜덤: u = 0.50..0.90 에서 FIFO / greedy / cost-benefit 의 WA + 해석 모델
 *   T2 hot-cold 90/10: 같은 스윕, greedy vs cost-benefit (분리 + 나이 정렬)
 *   T3 hot-cold u=0.80 에서 분리(separate)·나이 정렬(age_sort)을 끄고 켜기
 *   T4 victim 사용률 히스토그램 (LFS 논문의 "bimodal" 그림)
 *   T5 시드 5 개에서 테스트가 단언하는 순서가 늘 성립하나 + 마모(erase) 분포
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep
 */
#define LFS_NO_MAIN
#include "solution.c"

#define NSEG 1024u
#define SEGB 64u
#define WARM 4.0
#define MEAS 8.0

static lfs_cfg_t mk(double u, int pol, bool sep, bool sort, uint32_t batch) {
    lfs_cfg_t c = { NSEG, SEGB, (uint32_t)(u * NSEG * SEGB), pol, 2, 3, 1, batch, sep, sort };
    return c;
}
static double wa(double u, int pol, int wl, bool sep, bool sort, uint32_t batch, uint32_t seed, run_t *out) {
    lfs_cfg_t c = mk(u, pol, sep, sort, batch);
    run_t r;
    if (run_sim(&c, wl, seed, WARM, MEAS, &r) != 0 || r.integrity_bad) { printf("run failed\n"); exit(1); }
    if (out) *out = r;
    return r.wa;
}

int main(void) {
    printf("device: %u segments x %u blocks (4 KiB) = %u MiB, warm %.0fx, measure %.0fx nlba host writes\n",
           NSEG, SEGB, NSEG * SEGB / 256, WARM, MEAS);

    printf("\nT1 uniform random overwrite (separate, no sort, batch 1)\n");
    printf("  u     FIFO   model   greedy  model   c-b     greedy seg/GiB\n");
    for (int k = 0; k <= 8; k++) {
        double u = 0.50 + 0.05 * k;
        run_t rf, rg, rc;
        wa(u, POL_FIFO, WL_UNIFORM, true, false, 1, 3, &rf);
        wa(u, POL_GREEDY, WL_UNIFORM, true, false, 1, 3, &rg);
        wa(u, POL_COSTBEN, WL_UNIFORM, true, false, 1, 3, &rc);
        lfs_cfg_t c = mk(u, POL_FIFO, true, false, 1);
        printf("  %.2f  %.3f  %.3f   %.3f   %.3f   %.3f   %.0f\n", u, rf.wa, model_fifo(eff_alpha(&c, rf.mean_free)),
               rg.wa, model_greedy(eff_alpha(&c, rg.mean_free), SEGB), rc.wa, rg.segs_per_gib);
    }

    printf("\nT2 hot-cold 90/10 (separate + age sort, batch 4)\n");
    printf("  u     greedy  c-b     c-b/greedy  greedy-uniform(T1 cfg)\n");
    for (int k = 0; k <= 8; k++) {
        double u = 0.50 + 0.05 * k;
        double g = wa(u, POL_GREEDY, WL_HOTCOLD, true, true, 4, 5, NULL);
        double c = wa(u, POL_COSTBEN, WL_HOTCOLD, true, true, 4, 5, NULL);
        double gu = wa(u, POL_GREEDY, WL_UNIFORM, true, false, 1, 3, NULL);
        printf("  %.2f  %.3f   %.3f   %.3f       %.3f\n", u, g, c, c / g, gu);
    }

    printf("\nT3 hot-cold 90/10, u = 0.80: cleaner-output separation and age sort\n");
    printf("  policy        sep  sort batch  WA      mean victim u\n");
    for (int pol = 0; pol < 2; pol++)
        for (int sep = 0; sep < 2; sep++)
            for (int so = 0; so < 2; so++) {
                run_t r;
                wa(0.80, pol, WL_HOTCOLD, sep, so, so ? 4 : 1, 5, &r);
                printf("  %-12s  %-3s  %-4s %u      %.3f   %.3f\n", POL_NAME[pol], sep ? "on" : "off", so ? "on" : "off",
                       so ? 4u : 1u, r.wa, r.mean_victim_u);
            }

    printf("\nT4 victim utilization histogram, hot-cold u = 0.75 (separate + sort, batch 4) — %% of cleaned segments\n");
    printf("  u bucket      ");
    for (int b = 0; b < 10; b++) printf(" %.1f-", b / 10.0);
    printf("\n");
    for (int pol = 0; pol < 2; pol++) {
        run_t r;
        wa(0.75, pol, WL_HOTCOLD, true, true, 4, 5, &r);
        printf("  %-12s  ", POL_NAME[pol]);
        for (int b = 0; b < 10; b++) printf(" %5.1f", 100.0 * r.st.hist[b] / (double)r.st.segs_cleaned);
        printf("   (WA %.3f, mean u %.3f)\n", r.wa, r.mean_victim_u);
    }

    printf("\nT5 seeds 1..5, u = 0.80: orderings asserted by the tests + wear (erase count spread)\n");
    printf("  seed  greedy-uni  greedy-hc  c-b-hc   hc>uni  cb<g\n");
    for (uint32_t sd = 1; sd <= 5; sd++) {
        double gu = wa(0.80, POL_GREEDY, WL_UNIFORM, true, true, 4, sd, NULL);
        double gh = wa(0.80, POL_GREEDY, WL_HOTCOLD, true, true, 4, sd, NULL);
        double cb = wa(0.80, POL_COSTBEN, WL_HOTCOLD, true, true, 4, sd, NULL);
        printf("  %u     %.3f       %.3f      %.3f    %s     %s\n", sd, gu, gh, cb, gh > gu ? "yes" : "NO", cb < gh ? "yes" : "NO");
    }
    printf("  wear after warm+measure (hot-cold u=0.80, seed 5): policy  erases min / max / mean\n");
    for (int pol = 0; pol < 3; pol++) {
        lfs_t L;
        lfs_cfg_t c = mk(0.80, pol, true, true, 4);
        if (lfs_init(&L, &c) != 0) return 1;
        uint32_t rng = 5;
        for (uint32_t b = 0; b < c.nlba; b++) (void)lfs_write(&L, b);
        for (uint64_t k = 0; k < (uint64_t)((WARM + MEAS) * c.nlba); k++) (void)lfs_write(&L, next_lba(WL_HOTCOLD, c.nlba, &rng, k));
        uint32_t mn = UINT32_MAX, mx = 0;
        for (uint32_t s = 0; s < c.nseg; s++) { if (L.erases[s] < mn) mn = L.erases[s]; if (L.erases[s] > mx) mx = L.erases[s]; }
        printf("    %-12s  %u / %u / %.1f\n", POL_NAME[pol], mn, mx, (double)L.erase_total / c.nseg);
        lfs_free(&L);
    }
    return 0;
}
