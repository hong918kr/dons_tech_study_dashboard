/* OS-B11 demo — OSTEP 22.6 의 세 작업 부하로 히트율 곡선 (그림 22.6~22.8 + 22.9 Clock) 을 직접 만든다.
 *
 *   no-locality : 100 페이지 균등 무작위, 1만 참조
 *   80-20       : 80% 참조가 20 페이지(hot)로, 20% 가 나머지 80 페이지로, 1만 참조
 *   looping     : 0..49 를 반복, 1만 참조 (LRU·FIFO 최악)
 *
 * ./demo_curves        → 프레임 10,20,..,100 표 + 80-20 에서 Clock 희생자의 LRU 순위 분포
 * ./demo_curves csv    → 프레임 1..100 전부 CSV (그림용)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_curves.c -o demo_curves   (solution.c 와 같은 폴더에서)
 */
#define OSB11_NO_MAIN
#include "solution.c"

#define NREF 10000L

int main(int argc, char **argv) {
    static int w[3][NREF];
    const char *wname[3] = { "no-locality", "80-20", "looping" };
    wl_uniform(w[0], NREF, 100, 99u);
    wl_8020(w[1], NREF, 100, 7u);
    wl_loop(w[2], NREF, 50);
    const policy_t order[4] = { POL_OPT, POL_LRU, POL_CLOCK, POL_FIFO };
    bool csv = argc > 1 && strcmp(argv[1], "csv") == 0;

    if (csv) printf("workload,frames,OPT,LRU,CLOCK,FIFO\n");
    for (int k = 0; k < 3; k++) {
        if (!csv) printf("%s — hit rate %%\n  frames    OPT    LRU  CLOCK   FIFO\n", wname[k]);
        for (int f = csv ? 1 : 10; f <= 100; f += csv ? 1 : 10) {
            if (csv) printf("%s,%d", wname[k], f); else printf("  %6d", f);
            for (int i = 0; i < 4; i++) {
                stats_t st;
                sim_run(order[i], w[k], NREF, f, &st, NULL, NULL);
                double hr = 100.0 * (double)st.hits / (double)NREF;
                if (csv) printf(",%.2f", hr); else printf(" %6.1f", hr);
            }
            printf("\n");
        }
    }
    if (csv) return 0;

    printf("\nClock vs LRU on 80-20: victim's LRU rank (0 = the LRU page), hand steps per eviction\n");
    printf("  frames  evictions  rank0%%  rank<f/4%%  rank<f/2%%  mean-rank/f  steps/evict\n");
    for (int f = 10; f <= 90; f += 20) {
        long hist[MAX_FRAMES] = { 0 };
        stats_t st;
        long ev = clock_lru_rank(w[1], NREF, f, hist, &st);
        long q = 0, h = 0; double rs = 0;
        for (int r = 0; r < f; r++) {
            if (r < f / 4) q += hist[r];
            if (r < f / 2) h += hist[r];
            rs += (double)r * (double)hist[r];
        }
        printf("  %6d  %9ld  %6.1f  %9.1f  %9.1f  %11.3f  %11.2f\n", f, ev,
               100.0 * (double)hist[0] / (double)ev, 100.0 * (double)q / (double)ev,
               100.0 * (double)h / (double)ev, rs / (double)ev / f, (double)st.hand_steps / (double)ev);
    }
    return 0;
}
