/* ALGO-A02 demo_bench — 측정 전용 (CHECK 없음)
 *   1) 음수 사이클이 있으면 플로이드-워셜 값이 지수적으로 작아진다: 모든 간선 -1 인 완전 그래프, 가드 없는 int64
 *   2) 모든 쌍 최단 경로 시간: FW (int32, 가드 포함) vs 다익스트라 O(V^2) × n vs 벨만-포드(조기 종료) × n
 *   3) 벨만-포드 조기 종료: 실제 패스 수 (랜덤 간선 순서) vs 최악 n
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NMAX 256
#define EMAX (16 * NMAX)
#define INF64 INT64_MAX
#define FW_INF INT32_MAX
#define FW_NEG_INF (-(1 << 30))

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

typedef struct { int16_t u, v; int64_t w; } edge_t;
static edge_t E[EMAX];
static int nE;
static int32_t W32[NMAX][NMAX], D[NMAX][NMAX];
static uint8_t NX[NMAX][NMAX];
static int64_t W64[NMAX][NMAX];

static void gen(int n, int m, uint32_t *s) {
    nE = 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { W32[i][j] = i == j ? 0 : FW_INF; W64[i][j] = INF64; }
    for (int k = 0; k < m; k++) {
        int u = (int)(xs32(s) % (uint32_t)n), v = (int)(xs32(s) % (uint32_t)n);
        int32_t w = (int32_t)(xs32(s) % 10000u);
        E[nE].u = (int16_t)u; E[nE].v = (int16_t)v; E[nE].w = w; nE++;
        if (u != v && w < W32[u][v]) W32[u][v] = w;
        if (w < W64[u][v]) W64[u][v] = w;
    }
}
static int64_t fw(int n) {
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { D[i][j] = W32[i][j]; NX[i][j] = (uint8_t)j; }
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++) {
            int32_t dik = D[i][k];
            if (dik == FW_INF) continue;
            for (int j = 0; j < n; j++) {
                int32_t dkj = D[k][j];
                if (dkj == FW_INF) continue;
                int32_t s = dik + dkj;
                if (s < FW_NEG_INF) s = FW_NEG_INF;
                if (s < D[i][j]) { D[i][j] = s; NX[i][j] = NX[i][k]; }
            }
        }
    int64_t sum = 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (D[i][j] != FW_INF) sum += D[i][j];
    return sum;
}
static int64_t dijk_all(int n) {
    int64_t sum = 0, d[NMAX];
    bool done[NMAX];
    for (int s = 0; s < n; s++) {
        for (int v = 0; v < n; v++) { d[v] = INF64; done[v] = false; }
        d[s] = 0;
        for (;;) {
            int u = -1;
            for (int v = 0; v < n; v++) if (!done[v] && d[v] != INF64 && (u < 0 || d[v] < d[u])) u = v;
            if (u < 0) break;
            done[u] = true;
            for (int v = 0; v < n; v++) if (W64[u][v] != INF64 && d[u] + W64[u][v] < d[v]) d[v] = d[u] + W64[u][v];
        }
        for (int v = 0; v < n; v++) if (d[v] != INF64) sum += d[v];
    }
    return sum;
}
static long g_passes;
static int64_t bf_all(int n) {
    int64_t sum = 0, d[NMAX];
    g_passes = 0;
    for (int s = 0; s < n; s++) {
        for (int v = 0; v < n; v++) d[v] = INF64;
        d[s] = 0;
        for (int pass = 1; pass <= n; pass++) {
            bool ch = false;
            g_passes++;
            for (int k = 0; k < nE; k++)
                if (d[E[k].u] != INF64 && d[E[k].u] + E[k].w < d[E[k].v]) { d[E[k].v] = d[E[k].u] + E[k].w; ch = true; }
            if (!ch) break;
        }
        for (int v = 0; v < n; v++) if (d[v] != INF64) sum += d[v];
    }
    return sum;
}

int main(void) {
    /* 1) 지수 폭주 */
    printf("1) Floyd-Warshall on complete digraph, all weights -1, NO clamp (int64):\n");
    static int64_t M[64][64];
    for (int n = 2; n <= 30; n++) {
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) M[i][j] = i == j ? 0 : -1;
        int ovf = 0;
        for (int k = 0; k < n; k++) for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
            int64_t s;
            if (__builtin_add_overflow(M[i][k], M[k][j], &s)) { ovf = 1; continue; }
            if (s < M[i][j]) M[i][j] = s;
        }
        int64_t mn = 0;
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (M[i][j] < mn) mn = M[i][j];
        if (n <= 6 || n == 8 || n == 13 || n == 14 || n == 20 || n == 25 || ovf)
            printf("   n=%2d  min entry = %.4g%s%s\n", n, (double)mn, mn < INT32_MIN ? "  (< INT32_MIN)" : "",
                   ovf ? "  <-- int64 addition overflowed" : "");
        if (ovf) break;
    }

    /* 2) 시간 */
    printf("2) all-pairs time, m = 8n random edges, w in [0,9999], median of 5 (min..max) ms after 1 warm-up\n");
    printf("   %4s %16s %16s %16s %8s %8s\n", "n", "FW int32", "Dijkstra V^2 xn", "BF early-exit xn", "Dij/FW", "BF/FW");
    int ns[] = {64, 128, 256};
    for (int t = 0; t < 3; t++) {
        int n = ns[t];
        uint32_t s = 12345u + (uint32_t)n;
        gen(n, 8 * n, &s);
        double tf[5], td[5], tb[5];
        int64_t c1 = fw(n), c2 = dijk_all(n), c3 = bf_all(n);   /* warm-up + 결과 일치 확인 */
        for (int r = 0; r < 5; r++) {
            double a = now_ms(); c1 = fw(n); double b = now_ms(); c2 = dijk_all(n); double c = now_ms(); c3 = bf_all(n); double d = now_ms();
            tf[r] = b - a; td[r] = c - b; tb[r] = d - c;
        }
        qsort(tf, 5, sizeof(double), cmpd); qsort(td, 5, sizeof(double), cmpd); qsort(tb, 5, sizeof(double), cmpd);
        printf("   %4d %6.3f (%.2f..%.2f) %6.3f (%.2f..%.2f) %6.3f (%.2f..%.2f) %8.2f %8.2f  sums %s, BF passes/src = %.2f\n", n,
               tf[2], tf[0], tf[4], td[2], td[0], td[4], tb[2], tb[0], tb[4], td[2] / tf[2], tb[2] / tf[2],
               (c1 == c2 && c2 == c3) ? "equal" : "DIFFER", (double)g_passes / n);
    }

    /* 3) 조기 종료 패스 수 */
    printf("3) Bellman-Ford passes (incl. final no-change pass), random graphs m = 4n, edge order random, 200 graphs x 1 src\n");
    for (int n = 16; n <= 256; n *= 2) {
        uint32_t s = 777u + (uint32_t)n;
        long tot = 0, mx = 0;
        for (int it = 0; it < 200; it++) {
            gen(n, 4 * n, &s);
            int64_t d[NMAX];
            for (int v = 0; v < n; v++) d[v] = INF64;
            d[0] = 0;
            int pass;
            for (pass = 1; pass <= n; pass++) {
                bool ch = false;
                for (int k = 0; k < nE; k++)
                    if (d[E[k].u] != INF64 && d[E[k].u] + E[k].w < d[E[k].v]) { d[E[k].v] = d[E[k].u] + E[k].w; ch = true; }
                if (!ch) break;
            }
            if (pass > n) pass = n;
            tot += pass; if (pass > mx) mx = pass;
        }
        printf("   n=%3d  avg passes %.2f  max %ld  (worst case n = %d)\n", n, (double)tot / 200, mx, n);
    }
    return 0;
}
