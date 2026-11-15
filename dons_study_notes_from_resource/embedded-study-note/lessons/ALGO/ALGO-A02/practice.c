/* ALGO-A02 추가 연습 — 한 번에 검증
 *   연습 1  Johnson: 벨만-포드 퍼텐셜 h 로 가중치를 음 아니게 바꾼 뒤 정점마다 다익스트라 → 플로이드-워셜과 대조
 *   연습 2  SPFA (FIFO 큐 + in-queue 플래그 + 경로 간선 수로 음수 사이클 검출) → 벨만-포드와 대조, 완화 수 비교
 *   연습 3  차분 제약(difference constraints): 센서 트리거 타이밍 x_v - x_u <= c 를 풀거나, 모순 제약 사이클을 보고
 *   연습 4  단일 출발점 -∞ 표시: n 번째 패스에서 완화된 정점에서 BFS → "음수 사이클이 끼어드는 목적지" 전부
 * 동적 할당 없음. build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr -lm && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 64
#define MAX_E 4096
#define INF INT64_MAX
#define VSRC (-1)   /* 가상 출발점: 모든 dist = 0 으로 시작 */

typedef struct { int16_t u, v; int64_t w; } edge_t;
typedef struct { int n, m; edge_t e[MAX_E]; } graph_t;
static void graph_init(graph_t *g, int n) { g->n = n; g->m = 0; }
static int graph_add(graph_t *g, int u, int v, int64_t w) {
    if (g->m >= MAX_E) return -1;
    g->e[g->m].u = (int16_t)u; g->e[g->m].v = (int16_t)v; g->e[g->m].w = w; g->m++;
    return 0;
}

/* 본문 Part A 와 같은 벨만-포드 (조기 종료, n 번째 패스 완화 = 음수 사이클). relaxed_last[v] = n 번째 패스에서 완화됨.
 * 반환 0 = OK, 1 = 음수 사이클. 연습 4 를 위해 n 번째 패스는 끝까지 돈다. */
static int bf(const graph_t *g, int src, int64_t *dist, int16_t *pred, bool *relaxed_last, long *relax) {
    int n = g->n, neg = 0;
    for (int v = 0; v < n; v++) { dist[v] = src == VSRC ? 0 : INF; pred[v] = -1; if (relaxed_last) relaxed_last[v] = false; }
    if (src >= 0) dist[src] = 0;
    *relax = 0;
    for (int pass = 1; pass <= n; pass++) {
        bool changed = false;
        for (int k = 0; k < g->m; k++) {
            const edge_t *e = &g->e[k];
            if (dist[e->u] == INF || dist[e->u] + e->w >= dist[e->v]) continue;
            dist[e->v] = dist[e->u] + e->w; pred[e->v] = (int16_t)k; changed = true; (*relax)++;
            if (pass == n) { neg = 1; if (relaxed_last) relaxed_last[e->v] = true; }
        }
        if (!changed) break;
    }
    return neg;
}
static int extract_cycle(const graph_t *g, const int16_t *pred, int v, int *cyc_e) {
    for (int t = 0; t < g->n; t++) { if (pred[v] < 0) return -1; v = g->e[pred[v]].u; }
    int len = 0, x = v;
    do { if (pred[x] < 0 || len >= g->n) return -1; cyc_e[len++] = pred[x]; x = g->e[pred[x]].u; } while (x != v);
    for (int i = 0, j = len - 1; i < j; i++, j--) { int t = cyc_e[i]; cyc_e[i] = cyc_e[j]; cyc_e[j] = t; }
    return len;
}

/* 기준: int64 플로이드-워셜 (작은 가중치에서만 씀). d[i][i] < 0 이면 음수 사이클 */
static int64_t FD[MAX_V][MAX_V];
static int fw_ref(const graph_t *g) {
    int n = g->n;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) FD[i][j] = i == j ? 0 : INF;
    for (int k = 0; k < g->m; k++) if (g->e[k].w < FD[g->e[k].u][g->e[k].v]) FD[g->e[k].u][g->e[k].v] = g->e[k].w;
    for (int k = 0; k < n; k++) for (int i = 0; i < n; i++) if (FD[i][k] != INF) for (int j = 0; j < n; j++)
        if (FD[k][j] != INF && FD[i][k] + FD[k][j] < FD[i][j]) FD[i][j] = FD[i][k] + FD[k][j];
    for (int i = 0; i < n; i++) if (FD[i][i] < 0) return 1;
    return 0;
}

/* ======================= 연습 1 — Johnson ======================= */
/* out[s][t] = 최단 거리 (INF 도달 불가). 음수 사이클이면 -1. 재가중 후 모든 w' >= 0 인지도 확인해 *min_wp 로 돌려준다 */
static int64_t JW[MAX_V][MAX_V];
static int johnson(const graph_t *g, int64_t out[][MAX_V], int64_t *min_wp) {
    int n = g->n;
    int64_t h[MAX_V], dp[MAX_V];
    int16_t pred[MAX_V];
    long rl;
    if (bf(g, VSRC, h, pred, NULL, &rl)) return -1;          /* h(v) = 가상 출발점에서의 거리 (<= 0) */
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) JW[i][j] = INF;
    *min_wp = INF;
    for (int k = 0; k < g->m; k++) {
        const edge_t *e = &g->e[k];
        int64_t wp = e->w + h[e->u] - h[e->v];                /* 삼각 부등식 h(v) <= h(u) + w → wp >= 0 */
        if (wp < *min_wp) *min_wp = wp;
        if (wp < JW[e->u][e->v]) JW[e->u][e->v] = wp;
    }
    for (int s = 0; s < n; s++) {                             /* O(V^2) 다익스트라 × n */
        bool done[MAX_V] = {false};
        for (int v = 0; v < n; v++) dp[v] = INF;
        dp[s] = 0;
        for (;;) {
            int u = -1;
            for (int v = 0; v < n; v++) if (!done[v] && dp[v] != INF && (u < 0 || dp[v] < dp[u])) u = v;
            if (u < 0) break;
            done[u] = true;
            for (int v = 0; v < n; v++) if (JW[u][v] != INF && dp[u] + JW[u][v] < dp[v]) dp[v] = dp[u] + JW[u][v];
        }
        for (int t = 0; t < n; t++) out[s][t] = dp[t] == INF ? INF : dp[t] - h[s] + h[t];   /* 경로 위 h 는 망원급수로 상쇄 */
    }
    return 0;
}

/* ======================= 연습 2 — SPFA ======================= */
/* CSR 인접 리스트 + 크기 n 링 버퍼 (in-queue 플래그 덕에 큐에 같은 정점은 하나뿐 → n 칸이면 충분).
 * cnt[v] = 현재 dist[v] 를 만든 경로의 간선 수. cnt >= n 이면 경로에 정점이 n+1 개 → 반복 → 음수 사이클. */
static int spfa(const graph_t *g, int src, int64_t *dist, long *relax) {
    int n = g->n, off[MAX_V + 1] = {0}, adj[MAX_E], q[MAX_V], cnt[MAX_V], head = 0, size = 0;
    bool inq[MAX_V];
    for (int k = 0; k < g->m; k++) off[g->e[k].u + 1]++;
    for (int v = 0; v < n; v++) off[v + 1] += off[v];
    int pos[MAX_V + 1];
    memcpy(pos, off, sizeof pos);
    for (int k = 0; k < g->m; k++) adj[pos[g->e[k].u]++] = k;
    for (int v = 0; v < n; v++) { dist[v] = INF; inq[v] = false; cnt[v] = 0; }
    dist[src] = 0; q[0] = src; size = 1; inq[src] = true;
    *relax = 0;
    while (size > 0) {
        int u = q[head]; head = (head + 1) % n; size--; inq[u] = false;
        for (int a = off[u]; a < off[u + 1]; a++) {
            const edge_t *e = &g->e[adj[a]];
            if (dist[u] + e->w >= dist[e->v]) continue;
            dist[e->v] = dist[u] + e->w; cnt[e->v] = cnt[u] + 1; (*relax)++;
            if (cnt[e->v] >= n) return 1;
            if (!inq[e->v]) { q[(head + size) % n] = e->v; size++; inq[e->v] = true; }
        }
    }
    return 0;
}

/* ======================= 연습 3 — 차분 제약 ======================= */
/* 제약 x[b] - x[a] <= c  ↔  간선 a → b, 가중치 c.  해 = 가상 출발점 거리 (모든 x <= 0, 최소 하나는 0).
 * 반환 0 = 해 x[], 1 = 모순: cyc[] 에 제약 번호 순환, *len. */
typedef struct { int a, b; int64_t c; } dcon_t;
static int diff_solve(int n, const dcon_t *con, int m, int64_t *x, int *cyc, int *len) {
    static graph_t g;
    int16_t pred[MAX_V];
    long rl;
    graph_init(&g, n);
    for (int k = 0; k < m; k++) graph_add(&g, con[k].a, con[k].b, con[k].c);
    *len = 0;
    if (!bf(&g, VSRC, x, pred, NULL, &rl)) return 0;
    /* bf() 는 연습 4 때문에 n 번째 패스를 끝까지 돈다 → 그 뒤의 pred 로는 "첫 완화 직후" 라는 증명 전제가 깨질 수 있다.
     * 추출용으로 n 번째 패스의 첫 완화에서 멈추는 판을 한 번 더 돈다 (본문 Part A 증명 그대로). */
    int64_t d[MAX_V];
    for (int v = 0; v < n; v++) { d[v] = 0; pred[v] = -1; }
    for (int pass = 1; pass <= n; pass++)
        for (int k = 0; k < g.m; k++) {
            const edge_t *e = &g.e[k];
            if (d[e->u] + e->w < d[e->v]) {
                d[e->v] = d[e->u] + e->w; pred[e->v] = (int16_t)k;
                if (pass == n) { *len = extract_cycle(&g, pred, e->v, cyc); return 1; }
            }
        }
    return 1;
}

/* ======================= 연습 4 — 단일 출발점 -∞ 표시 ======================= */
/* ninf[v] = src 에서 v 로 가는 걸음이 음수 사이클을 지날 수 있음 (최단 거리 -∞). */
static void bf_neg_inf(const graph_t *g, int src, bool *ninf) {
    int64_t dist[MAX_V];
    int16_t pred[MAX_V];
    bool last[MAX_V];
    int q[MAX_V], qh = 0, qt = 0;
    long rl;
    for (int v = 0; v < g->n; v++) ninf[v] = false;
    if (!bf(g, src, dist, pred, last, &rl)) return;
    for (int v = 0; v < g->n; v++) if (last[v]) { ninf[v] = true; q[qt++] = v; }
    while (qh < qt) {
        int u = q[qh++];
        for (int k = 0; k < g->m; k++)
            if (g->e[k].u == u && !ninf[g->e[k].v]) { ninf[g->e[k].v] = true; q[qt++] = g->e[k].v; }
    }
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int64_t rnd(uint32_t *s, int64_t lo, int64_t hi) { return lo + (int64_t)(xs32(s) % (uint32_t)(hi - lo + 1)); }

static void gen(graph_t *g, uint32_t *s, int n, int m, int64_t wmax, int mode) {   /* 0: w>=0, 1: 퍼텐셜, 2: 아무 음수 */
    int64_t pot[MAX_V];
    graph_init(g, n);
    for (int v = 0; v < n; v++) pot[v] = rnd(s, 0, wmax);
    for (int k = 0; k < m; k++) {
        int u = (int)rnd(s, 0, n - 1), v = (int)rnd(s, 0, n - 1);
        int64_t w = mode == 2 ? rnd(s, -wmax / 3, wmax) : rnd(s, 0, wmax);
        if (mode == 1) w += pot[u] - pot[v];
        graph_add(g, u, v, w);
    }
}

int main(void) {
    static graph_t g;
    static int64_t J[MAX_V][MAX_V];
    uint32_t seed = 0xA02Cu;

    /* 연습 1 */
    {
        int bad = 0, badw = 0, negok = 0, graphs = 0;
        for (int it = 0; it < 400; it++) {
            int n = (int)rnd(&seed, 1, 30);
            gen(&g, &seed, n, (int)rnd(&seed, 0, 5 * n), (it & 1) ? 15 : 50000, 1);
            int64_t minwp;
            if (fw_ref(&g) || johnson(&g, J, &minwp) != 0) { bad++; continue; }
            graphs++;
            if (g.m > 0 && minwp < 0) badw++;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (J[i][j] != FD[i][j]) bad++;
        }
        graph_init(&g, 3); graph_add(&g, 0, 1, 2); graph_add(&g, 1, 2, -5); graph_add(&g, 2, 0, 1);
        int64_t mw;
        negok = johnson(&g, J, &mw) == -1;
        CHECK(bad == 0 && badw == 0 && graphs == 400);
        CHECK(negok);
        printf("ex1 Johnson vs Floyd-Warshall: %d graphs with negative edges, mismatches=%d, negative reweighted edges=%d\n",
               graphs, bad, badw);
    }
    /* 연습 2 */
    {
        int bad = 0, det = 0, runs = 0;
        long rs = 0, rb = 0;
        for (int it = 0; it < 1500; it++) {
            int n = (int)rnd(&seed, 1, 30);
            int mode = it % 3;
            gen(&g, &seed, n, (int)rnd(&seed, 0, 4 * n), 40, mode);
            for (int s = 0; s < n; s++) {
                int64_t d1[MAX_V], d2[MAX_V];
                int16_t pred[MAX_V];
                long r1, r2;
                int a = spfa(&g, s, d1, &r1), b = bf(&g, s, d2, pred, NULL, &r2);
                runs++;
                if (a != b) { det++; continue; }
                if (a) continue;
                rs += r1; rb += r2;
                for (int v = 0; v < n; v++) if (d1[v] != d2[v]) bad++;
            }
        }
        CHECK(bad == 0 && det == 0);
        printf("ex2 SPFA vs Bellman-Ford: %d runs, dist mismatch=%d, detect mismatch=%d, relaxations SPFA/BF = %ld/%ld (%.2f)\n",
               runs, bad, det, rs, rb, rb ? (double)rs / (double)rb : 0.0);
    }
    /* 연습 3: 센서 트리거 타이밍 (µs). 0 ADC_TRIG, 1 DMA_START, 2 SPI_CS, 3 IMU_READ, 4 TIMESTAMP */
    {
        dcon_t con[16];
        int m = 0;
        #define AT_LEAST(a, b, c) (con[m++] = (dcon_t){(b), (a), -(c)})   /* x[b] - x[a] >= c  ↔  x[a] - x[b] <= -c */
        #define AT_MOST(a, b, c)  (con[m++] = (dcon_t){(a), (b), (c)})    /* x[b] - x[a] <= c */
        AT_LEAST(0, 1, 2);  AT_MOST(0, 1, 5);      /* DMA 는 ADC 트리거 후 2..5 µs */
        AT_LEAST(1, 2, 10);                        /* SPI CS 는 DMA 시작 후 10 µs 이상 */
        AT_LEAST(2, 3, 1);  AT_MOST(2, 3, 3);      /* IMU 읽기는 CS 후 1..3 µs */
        AT_MOST(0, 3, 16);                         /* 전체 ADC→IMU 16 µs 이내 */
        AT_LEAST(3, 4, 0);  AT_MOST(0, 4, 20);     /* 타임스탬프는 IMU 뒤, ADC 후 20 µs 이내 */
        int64_t x[MAX_V];
        int cyc[MAX_V], len;
        int r = diff_solve(5, con, m, x, cyc, &len);
        bool ok = r == 0;
        for (int k = 0; ok && k < m; k++) if (x[con[k].b] - x[con[k].a] > con[k].c) ok = false;
        CHECK(ok);
        printf("ex3 feasible schedule (us, relative to ADC_TRIG):");
        for (int v = 0; v < 5; v++) printf(" %lld", (long long)(x[v] - x[0]));
        printf("\n");
        AT_MOST(0, 3, 12);                         /* 새 요구: ADC→IMU 12 µs 이내 → 2 + 10 + 1 = 13 > 12, 모순 */
        r = diff_solve(5, con, m, x, cyc, &len);
        int64_t sum = 0;
        for (int t = 0; t < len; t++) sum += con[cyc[t]].c;
        CHECK(r == 1 && len >= 2 && sum < 0);
        printf("ex3 add 'ADC->IMU <= 12us': infeasible, conflicting constraint cycle of %d, slack sum = %lld us\n", len, (long long)sum);
        /* 랜덤 제약계: 해가 있으면 모든 제약 만족, 없다고 하면 기준 FW 도 음수 사이클 */
        int bad = 0, infeas = 0;
        for (int it = 0; it < 1500; it++) {
            int n = (int)rnd(&seed, 1, 12), mm = (int)rnd(&seed, 0, 3 * n);
            static dcon_t cs[64];
            if (mm > 64) mm = 64;
            graph_init(&g, n);
            for (int k = 0; k < mm; k++) {
                cs[k] = (dcon_t){(int)rnd(&seed, 0, n - 1), (int)rnd(&seed, 0, n - 1), rnd(&seed, -10, 30)};
                graph_add(&g, cs[k].a, cs[k].b, cs[k].c);
            }
            int rr = diff_solve(n, cs, mm, x, cyc, &len);
            if (rr != fw_ref(&g)) bad++;
            if (rr == 0) { for (int k = 0; k < mm; k++) if (x[cs[k].b] - x[cs[k].a] > cs[k].c) bad++; }
            else { infeas++; int64_t s2 = 0; for (int t = 0; t < len; t++) s2 += cs[cyc[t]].c; if (len < 1 || s2 >= 0) bad++; }
        }
        CHECK(bad == 0 && infeas > 100);
        printf("ex3 random constraint systems: 1500, infeasible %d, errors=%d\n", infeas, bad);
    }
    /* 연습 4: -∞ 표시 vs 기준 (FW 행렬: v 가 음수 사이클 위 ⇔ 어떤 k 로 d[k][k] < 0 이고 k ⇝ v ⇝ k, 도달은 별도 계산) */
    {
        int bad = 0, marked = 0;
        for (int it = 0; it < 2000; it++) {
            int n = (int)rnd(&seed, 1, 12);
            gen(&g, &seed, n, (int)rnd(&seed, 0, 3 * n), 12, 2);
            bool R[MAX_V][MAX_V];
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) R[i][j] = i == j;
            for (int k = 0; k < g.m; k++) R[g.e[k].u][g.e[k].v] = true;
            for (int k = 0; k < n; k++) for (int i = 0; i < n; i++) if (R[i][k]) for (int j = 0; j < n; j++) if (R[k][j]) R[i][j] = true;
            fw_ref(&g);    /* 작은 가중치·n <= 12 라 int64 폭주 없음 */
            for (int s = 0; s < n; s++) {
                bool ninf[MAX_V];
                bf_neg_inf(&g, s, ninf);
                for (int v = 0; v < n; v++) {
                    bool want = false;
                    for (int k = 0; k < n; k++) if (FD[k][k] < 0 && R[s][k] && R[k][v]) want = true;
                    if (want != ninf[v]) bad++;
                    marked += ninf[v];
                }
            }
        }
        CHECK(bad == 0 && marked > 1000);
        printf("ex4 single-source -inf marking: 2000 graphs, marked %d (src,v) pairs, mismatches=%d\n", marked, bad);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
