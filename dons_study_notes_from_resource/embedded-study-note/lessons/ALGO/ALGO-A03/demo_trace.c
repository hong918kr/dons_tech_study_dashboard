/* ALGO-A03 demo: 손 계산 예(체비쇼프, 점 4개)의 dp[mask][last] 표 전체를 찍는다 — 5. 풀이의 추적 표.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace -lm && ./demo_trace
 */
#define ALGO_A03_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#include "solution.c"

int main(void) {
    const pt_t p[4] = {{6, 4}, {4, 2}, {7, 0}, {5, 5}};   /* A B C D, 홈 H = (0,0) */
    const char nm[5] = {'A', 'B', 'C', 'D', 'H'};
    tsp_t t;
    build_board(&t, p, 4, (pt_t){0, 0}, MET_CHEBYSHEV);
    printf("d (chebyshev):  ");
    for (int j = 0; j <= 4; j++) printf("  %c", nm[j]);
    printf("\n");
    for (int i = 0; i <= 4; i++) {
        printf("            %c: ", nm[i]);
        for (int j = 0; j <= 4; j++) printf(" %2u", t.d[i][j]);
        printf("\n");
    }
    hk_fill(&t);
    printf("mask(DCBA)  set     end=A    end=B    end=C    end=D     (cost/prev, - = j not in mask)\n");
    for (uint32_t m = 1; m < 16; m++) {
        char set[8]; int k = 0;
        for (int j = 0; j < 4; j++) if ((m >> j) & 1u) set[k++] = nm[j];
        set[k] = 0;
        printf("  %u%u%u%u   %-6s", (m >> 3) & 1u, (m >> 2) & 1u, (m >> 1) & 1u, m & 1u, set);
        for (int j = 0; j < 4; j++) {
            if (!((m >> j) & 1u)) { printf("    -    "); continue; }
            unsigned pr = hk_par(m, j);
            printf("   %2u/%c  ", hk_dp(m, j), pr == PAR_HOME ? 'H' : nm[pr]);
        }
        printf("\n");
    }
    printf("transitions examined: %llu (= n(n-1)2^(n-2) = 48)\n", (unsigned long long)g_trans);
    return 0;
}
