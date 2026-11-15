/* ALGO-A01 demo_bench — 64×64 지도에서 다익스트라 vs A* (옥타일 3가지 동점 처리, 맨해튼):
 *   (1) 지도 종류별 확장 노드 수·최대 열린 집합·맨해튼 비최적 비율 (결정적 — 시간 무관)
 *   (2) 질의당 시간 (워밍업 1회 + 7회 측정, 중앙값과 최소~최대)
 *   (3) 팽창: 행 구간 찍기 vs 칸마다 창 훑기 (시간)
 *   (4) 작업 공간 크기
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define ALGO_A01_NO_MAIN
#include "solution.c"
#include <time.h>

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

#define NMAP 100
static grid_t maps[NMAP];
static int qs[NMAP][4];
static astar_ws_t W;

/* kind: 0..2 = 흩뿌림 10/20/30%, 3 = 벽 조각. 시작은 왼쪽 4열, 목표는 오른쪽 4열 (멀리 떨어진 질의) */
static void make_maps(int kind, uint32_t seed) {
    for (int m = 0; m < NMAP; m++) {
        grid_t *g = &maps[m];
        g->rows = g->cols = 64;
        memset(g->cell, 0, sizeof g->cell);
        if (kind < 3) {
            int dens = 10 * (kind + 1);
            for (int i = 0; i < 64 * 64; i++) g->cell[i] = (int)(xs(&seed) % 100) < dens;
        } else {
            for (int w = 0; w < 64 * 64 / 100; w++) {   /* 벽 40개, 길이 2..34 */
                int r = (int)(xs(&seed) % 64), c = (int)(xs(&seed) % 64), len = 2 + (int)(xs(&seed) % 33), v = (int)(xs(&seed) & 1);
                for (int k = 0; k < len; k++) { int rr = r + (v ? k : 0), cc = c + (v ? 0 : k); if (rr < 64 && cc < 64) g->cell[rr * 64 + cc] = 1; }
            }
        }
        int sr = (int)(xs(&seed) % 64), sc = (int)(xs(&seed) % 4), gr = (int)(xs(&seed) % 64), gc = 60 + (int)(xs(&seed) % 4);
        g->cell[sr * 64 + sc] = g->cell[gr * 64 + gc] = 0;
        qs[m][0] = sr; qs[m][1] = sc; qs[m][2] = gr; qs[m][3] = gc;
    }
}

static const struct { const char *name; heur_t h; tie_t tb; } VAR[5] = {
    {"dijkstra", H_ZERO, TB_ID}, {"octile/high-g", H_OCTILE, TB_HIGH_G}, {"octile/low-g", H_OCTILE, TB_LOW_G},
    {"octile/id", H_OCTILE, TB_ID}, {"manhattan/high-g", H_MANHATTAN, TB_HIGH_G},
};

static volatile long sink;

int main(void) {
    (void)path_extract; (void)render;
    const char *kname[4] = {"scatter 10%", "scatter 20%", "scatter 30%", "wall segments"};
    printf("sizeof(grid_t)=%zu  sizeof(astar_ws_t)=%zu (%.1f B/cell)\n", sizeof(grid_t), sizeof(astar_ws_t),
           (double)sizeof(astar_ws_t) / MAXN);
    printf("\n(1) expanded nodes, mean per query over %d maps of 64x64 (start col 0..3 -> goal col 60..63)\n", NMAP);
    printf("%-14s %6s | %9s %9s %9s %9s %9s | %8s | %s\n", "map", "reach", "dijkstra", "oct/hi-g", "oct/lo-g", "oct/id",
           "manh/hi-g", "maxOpen", "manhattan: subopt maps, mean excess, worst ratio");
    for (int kind = 0; kind < 4; kind++) {
        make_maps(kind, 0xBEEF0u + (uint32_t)kind);
        long exp[5] = {0}; int reach = 0, sub = 0, maxopen = 0; double excess = 0, worst = 1;
        for (int m = 0; m < NMAP; m++) {
            int cost[5];
            for (int v = 0; v < 5; v++) {
                cost[v] = astar(&maps[m], qs[m][0], qs[m][1], qs[m][2], qs[m][3], VAR[v].h, VAR[v].tb, &W);
                exp[v] += W.expanded;
                if (W.max_open > maxopen) maxopen = W.max_open;
            }
            if (cost[0] < 0) continue;
            reach++;
            if (cost[4] > cost[0]) {
                sub++; excess += (double)(cost[4] - cost[0]) / cost[0];
                if ((double)cost[4] / cost[0] > worst) worst = (double)cost[4] / cost[0];
            }
            for (int v = 1; v < 4; v++) if (cost[v] != cost[0]) { printf("COST MISMATCH\n"); return 1; }
        }
        printf("%-14s %3d/%d | %9.1f %9.1f %9.1f %9.1f %9.1f | %8d | %d, %.2f%%, %.4f\n", kname[kind], reach, NMAP,
               exp[0] / (double)NMAP, exp[1] / (double)NMAP, exp[2] / (double)NMAP, exp[3] / (double)NMAP,
               exp[4] / (double)NMAP, maxopen, sub, sub ? 100.0 * excess / sub : 0.0, worst);
    }

    printf("\n(2) time per query, us (median of 7 runs [min..max]), scatter 20%% maps\n");
    make_maps(1, 0xBEEF1u);
    double base = 0;
    for (int v = 0; v < 5; v++) {
        double t[8];
        for (int run = 0; run < 8; run++) {                      /* run 0 = 워밍업, 버림 */
            double t0 = now_us();
            long acc = 0;
            for (int m = 0; m < NMAP; m++) acc += astar(&maps[m], qs[m][0], qs[m][1], qs[m][2], qs[m][3], VAR[v].h, VAR[v].tb, &W);
            sink += acc;
            t[run] = (now_us() - t0) / NMAP;
        }
        qsort(t + 1, 7, sizeof(double), cmpd);
        if (v == 0) base = t[4];
        printf("  %-17s %7.2f [%6.2f..%6.2f]  x%.2f vs dijkstra\n", VAR[v].name, t[4], t[1], t[7], t[4] / base);
    }

    printf("\n(3) inflate 64x64 scatter-20%% map, us per call (median of 7 [min..max])\n");
    static grid_t out, out2;
    for (int r = 1; r <= 8; r *= 2) {
        double ts[8], tb[8];
        for (int run = 0; run < 8; run++) {
            double t0 = now_us();
            for (int m = 0; m < 20; m++) inflate(&maps[m], &out, r);
            ts[run] = (now_us() - t0) / 20;
            t0 = now_us();
            for (int m = 0; m < 20; m++) {                         /* 칸마다 (2r+1)² 창 훑기 */
                out2 = maps[m];
                for (int x = 0; x < 64; x++) for (int y = 0; y < 64; y++) {
                    int hit = 0;
                    for (int a = x - r; a <= x + r && !hit; a++) for (int b = y - r; b <= y + r && !hit; b++)
                        if (a >= 0 && a < 64 && b >= 0 && b < 64 && maps[m].cell[a * 64 + b] && (a - x) * (a - x) + (b - y) * (b - y) <= r * r) hit = 1;
                    out2.cell[x * 64 + y] = (uint8_t)hit;
                }
            }
            tb[run] = (now_us() - t0) / 20;
            if (memcmp(out.cell, out2.cell, sizeof out.cell)) { printf("INFLATE MISMATCH\n"); return 1; }
        }
        qsort(ts + 1, 7, sizeof(double), cmpd); qsort(tb + 1, 7, sizeof(double), cmpd);
        printf("  r=%d  span-stamp %7.2f [%6.2f..%6.2f]   per-cell window %7.2f [%6.2f..%6.2f]  ratio %.1fx\n",
               r, ts[4], ts[1], ts[7], tb[4], tb[1], tb[7], tb[4] / ts[4]);
    }
    printf("sink=%ld\n", sink);
    return 0;
}
