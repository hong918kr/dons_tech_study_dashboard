/* ALGO-M14 demo: Kruskal(정렬 + DSU) vs Prim O(V^2)(인접 행렬) — 희소 ~ 완전 그래프에서 시간
 *
 * solution.c 의 kruskal_msf / sort_edges_by_weight 를 그대로 쓴다 (DSU_MAX_N 을 4096 으로 키워 include).
 * V = 2000 연결 그래프 (무작위 신장 트리 + 무작위 간선), 가중치 1..10^6.
 * Prim 은 인접 행렬을 입력으로 받는다고 보고 행렬 생성 시간은 뺀다. 각 값 = 워밍업 1 + 5회 중앙값.
 * 매 회 두 결과의 총 가중치가 같은지 확인한다. 이 맥(Apple Silicon M 시리즈, -O2) — 비율을 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_mst.c -o demo_mst && ./demo_mst
 */
#define _POSIX_C_SOURCE 200809L
#define DSU_MAX_N 4096
#define ALGO_M14_NO_MAIN
#include "solution.c"
#include <time.h>

#define V 2000
#define MMAX (V * (V - 1) / 2)
static edge_t E[MMAX];
static int ORD[MMAX], CH[V];
static int32_t W[V][V];
static dsu_t D;

static uint32_t rs = 12345u;
static uint32_t xs(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static int64_t prim_v2(void) {
    static int64_t key[V];
    static uint8_t in[V];
    for (int i = 0; i < V; i++) { key[i] = INT64_MAX; in[i] = 0; }
    key[0] = 0;
    int64_t total = 0;
    for (int it = 0; it < V; it++) {
        int b = -1;
        for (int i = 0; i < V; i++) if (!in[i] && (b < 0 || key[i] < key[b])) b = i;
        in[b] = 1; total += key[b];
        for (int i = 0; i < V; i++) if (!in[i] && W[b][i] < key[i]) key[i] = W[b][i];
    }
    return total;
}

static int build(int m) {   /* 연결 보장: 0..V-1 무작위 순열 위의 경로 + 나머지 무작위 (완전 그래프면 모든 쌍) */
    for (int i = 0; i < V; i++) for (int j = 0; j < V; j++) W[i][j] = INT32_MAX;
    int k = 0;
    if (m == MMAX) {
        for (int i = 0; i < V; i++) for (int j = i + 1; j < V; j++) E[k++] = (edge_t){ (uint16_t)i, (uint16_t)j, (int32_t)(1 + xs() % 1000000u) };
    } else {
        static int perm[V];
        for (int i = 0; i < V; i++) perm[i] = i;
        for (int i = V - 1; i > 0; i--) { int j = (int)(xs() % (uint32_t)(i + 1)), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
        for (int i = 0; i + 1 < V; i++) E[k++] = (edge_t){ (uint16_t)perm[i], (uint16_t)perm[i + 1], (int32_t)(1 + xs() % 1000000u) };
        while (k < m) {
            int a = (int)(xs() % V), b = (int)(xs() % V);
            if (a == b) continue;
            E[k++] = (edge_t){ (uint16_t)a, (uint16_t)b, (int32_t)(1 + xs() % 1000000u) };
        }
    }
    for (int i = 0; i < k; i++) {
        int a = E[i].u, b = E[i].v;
        if (E[i].w < W[a][b]) W[a][b] = W[b][a] = E[i].w;
    }
    return k;
}

static double med5(double *t) {
    for (int a = 0; a < 5; a++) for (int b = a + 1; b < 5; b++) if (t[b] < t[a]) { double x = t[a]; t[a] = t[b]; t[b] = x; }
    return t[2];
}

int main(void) {
    const int ms[] = { 4 * V, 25 * V, 100 * V, 400 * V, MMAX };
    printf("V = %d, Kruskal = heapsort(order) + DSU scan; Prim = O(V^2) on adjacency matrix\n", V);
    printf("%10s %8s %12s %12s %12s %12s %6s\n", "E", "E/V", "sort ms", "kruskal ms", "prim ms", "kruskal/prim", "same");
    for (int c = 0; c < 5; c++) {
        int m = build(ms[c]);
        double ts[5], tk[5], tp[5];
        int64_t tot_k = 0, tot_p = 0;
        int same = 1;
        sort_edges_by_weight(E, m, ORD); kruskal_msf(&D, V, E, m, ORD, CH, &tot_k); tot_p = prim_v2();   /* 워밍업 */
        for (int r = 0; r < 5; r++) {
            double t0 = now_ms(); sort_edges_by_weight(E, m, ORD);
            double t1 = now_ms(); kruskal_msf(&D, V, E, m, ORD, CH, &tot_k);
            double t2 = now_ms(); tot_p = prim_v2();
            double t3 = now_ms();
            ts[r] = t1 - t0; tk[r] = t2 - t1; tp[r] = t3 - t2;
            if (tot_k != tot_p) same = 0;
        }
        double a = med5(ts), b = med5(tk), p = med5(tp);
        printf("%10d %8d %12.2f %12.2f %12.2f %12.2f %6s\n", m, m / V, a, b, p, b / p, same ? "yes" : "NO");
    }
    return 0;
}
