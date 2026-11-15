/* BM-M10 demo: IWDG/WWDG 표 — RM 표 재현, LSI 오차를 넣은 범위, "고를 수 있나" 지도
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_tables.c -o demo_tables && ./demo_tables
 * (공식은 solution.c 와 같은 정의의 축약 사본)
 */
#include <stdint.h>
#include <stdio.h>

static uint64_t iwdg_ticks(unsigned pr, unsigned rlr) { return ((uint64_t)4u << pr) * ((uint64_t)rlr + 1u); }

/* solution.c iwdg_pick 과 같은 규칙: 성공하면 PR/RLR, 실패 -1 */
static int pick(uint32_t min_ms, uint32_t max_ms, uint32_t lmin, uint32_t lmax, unsigned *pr_o, unsigned *rlr_o) {
    for (unsigned pr = 0; pr <= 6; pr++) {
        uint64_t step = (uint64_t)4u << pr, need = (uint64_t)min_ms * lmax;
        uint64_t n = (need + step * 1000u - 1u) / (step * 1000u);
        if (n == 0) n = 1;
        if (n > 4096u) continue;
        if (step * n * 1000u > (uint64_t)max_ms * lmin) continue;
        *pr_o = pr; *rlr_o = (unsigned)(n - 1u);
        return 0;
    }
    return -1;
}

int main(void) {
    static const uint32_t lsi[3] = { 17000, 32000, 47000 };
    printf("== IWDG timeout (ms) = 4*2^PR*(RLR+1)/f_LSI ==\n");
    printf("PR  div  | RLR=0: 17k     32k     47k   | RLR=0xFFF: 17k        32k        47k\n");
    for (unsigned pr = 0; pr <= 6; pr++) {
        printf("%u  /%-4u |", pr, 4u << pr);
        for (int i = 0; i < 3; i++) printf(" %7.3f", (double)iwdg_ticks(pr, 0) * 1000.0 / lsi[i]);
        printf("  |");
        for (int i = 0; i < 3; i++) printf(" %10.1f", (double)iwdg_ticks(pr, 0xFFF) * 1000.0 / lsi[i]);
        printf("\n");
    }

    printf("\n== pickable? min_ms x (max_ms/min_ms), LSI 17-47 kHz (ratio 2.765) vs calibrated 28.8-35.2 kHz (1.222) ==\n");
    static const uint32_t mins[5] = { 1, 10, 100, 1000, 10000 };
    static const unsigned r10[7] = { 12, 13, 15, 20, 25, 28, 30 };      /* ratio x10 */
    printf("min_ms  |");
    for (int j = 0; j < 7; j++) printf("  x%u.%u          ", r10[j] / 10, r10[j] % 10);
    printf("\n");
    for (int k = 0; k < 2; k++) {
        uint32_t lmin = k ? 28800 : 17000, lmax = k ? 35200 : 47000;
        printf("-- LSI %u..%u Hz --\n", lmin, lmax);
        for (int i = 0; i < 5; i++) {
            printf("%6u  |", mins[i]);
            for (int j = 0; j < 7; j++) {
                unsigned pr, rlr;
                uint32_t mx = mins[i] * r10[j] / 10u;
                if (pick(mins[i], mx, lmin, lmax, &pr, &rlr) == 0) printf("  PR%u RLR=%-5u  ", pr, rlr);
                else printf("  --             ");
            }
            printf("\n");
        }
    }

    printf("\n== WWDG @ PCLK1 = 42 MHz: tick = 4096*2^WDGTB/PCLK1 ==\n");
    printf("WDGTB  tick(us)   T=0x40 reset(us)  T=0x7F reset(us)  max guaranteed refresh window (W=0x7F)\n");
    for (unsigned tb = 0; tb <= 3; tb++) {
        double tick = (double)(4096u << tb) / 42.0;
        printf("%u      %8.3f   %10.2f        %10.2f        0 .. %.2f us\n", tb, tick, tick * 1, tick * 64, tick * 63);
    }
    return 0;
}
