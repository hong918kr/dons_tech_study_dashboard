/* ALGO-M06 추가 연습 — 한 번에 검증
 *   연습 1  목적지 하나만: dst 가 pop 되는 순간 멈추기 (조기 종료) — 전체 실행과 대조 + pop 절약량
 *   연습 2  최단 경로 개수 (LeetCode 1976 형태, w >= 1) — 단순 경로 전수 열거와 대조
 *   연습 3  최대 대역폭 경로 (widest / bottleneck path) — 임계값별 BFS 참조와 대조
 *   연습 4  K 홉 제한 최소 지연 (LeetCode 787 형태) — (정점, 홉) 상태 다익스트라 vs 홉 층별 DP
 *   연습 5  다중 출발점: 가장 가까운 게이트웨이 — 가상 출발점 1회 vs 게이트웨이별 실행의 최소
 * 테스트: 고정 시드 xorshift32 랜덤 그래프 + 이름 붙은 작은 예제. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 64
#define MAX_E 512
#define INF   INT32_MAX
#define NONE  (-1)

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---- 그래프 (본문과 같은 forward star) ---- */
typedef struct { int nv, ne; int16_t head[MAX_V], nxt[MAX_E], to[MAX_E]; int32_t w[MAX_E]; } graph_t;
static void graph_init(graph_t *g, int nv) { g->nv = nv; g->ne = 0; for (int i = 0; i < nv; i++) g->head[i] = NONE; }
static void graph_add_edge(graph_t *g, int u, int v, int32_t w) {
    int e = g->ne++; g->to[e] = (int16_t)v; g->w[e] = w; g->nxt[e] = g->head[u]; g->head[u] = (int16_t)e;
}
static void random_graph(graph_t *g, int nv, int ne, int32_t wmin, int32_t wmax) {
    graph_init(g, nv);
    for (int i = 0; i < ne; i++)
        graph_add_edge(g, (int)(xorshift32() % (uint32_t)nv), (int)(xorshift32() % (uint32_t)nv),
                       wmin + (int32_t)(xorshift32() % (uint32_t)(wmax - wmin + 1)));
}

/* ---- lazy 최소 힙: 엔트리 (key, v, aux). aux = 연습 4 의 홉 수 ---- */
#define MAX_LH 4096
typedef struct { int32_t d; int16_t v, aux; } ent_t;
static ent_t g_h[MAX_LH];
static int g_hn;
static void hpush(int32_t d, int v, int aux) {
    int i = g_hn++;
    while (i > 0 && g_h[(i - 1) / 2].d > d) { g_h[i] = g_h[(i - 1) / 2]; i = (i - 1) / 2; }
    g_h[i].d = d; g_h[i].v = (int16_t)v; g_h[i].aux = (int16_t)aux;
}
static ent_t hpop(void) {
    ent_t top = g_h[0], last = g_h[--g_hn];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= g_hn) break;
        if (c + 1 < g_hn && g_h[c + 1].d < g_h[c].d) c++;
        if (g_h[c].d >= last.d) break;
        g_h[i] = g_h[c]; i = c;
    }
    if (g_hn > 0) g_h[i] = last;
    return top;
}

/* 기준: 전체 다익스트라 (lazy). pops 를 센다 */
static void dijkstra_full(const graph_t *g, int src, int32_t *dist, unsigned *pops) {
    for (int v = 0; v < g->nv; v++) dist[v] = INF;
    g_hn = 0; dist[src] = 0; hpush(0, src, 0); *pops = 0;
    while (g_hn) {
        ent_t t = hpop(); (*pops)++;
        if (t.d > dist[t.v]) continue;
        for (int e = g->head[t.v]; e != NONE; e = g->nxt[e]) {
            int32_t nd = t.d + g->w[e];
            if (nd < dist[g->to[e]]) { dist[g->to[e]] = nd; hpush(nd, g->to[e], 0); }
        }
    }
}

/* ---------- 연습 1: 조기 종료 ---------- */
/* dst 의 유효한(stale 아닌) 엔트리가 pop 되는 순간 dist[dst] 는 최종 → 바로 반환. 도달 불가면 INF */
static int32_t dijkstra_to(const graph_t *g, int src, int dst, unsigned *pops) {
    static int32_t dist[MAX_V];
    for (int v = 0; v < g->nv; v++) dist[v] = INF;
    g_hn = 0; dist[src] = 0; hpush(0, src, 0); *pops = 0;
    while (g_hn) {
        ent_t t = hpop(); (*pops)++;
        if (t.d > dist[t.v]) continue;
        if (t.v == dst) return t.d;                    /* 확정 = 최종 */
        for (int e = g->head[t.v]; e != NONE; e = g->nxt[e]) {
            int32_t nd = t.d + g->w[e];
            if (nd < dist[g->to[e]]) { dist[g->to[e]] = nd; hpush(nd, g->to[e], 0); }
        }
    }
    return INF;
}

/* ---------- 연습 2: 최단 경로 개수 (w >= 1) ---------- */
static void count_shortest(const graph_t *g, int src, int32_t *dist, uint64_t *cnt) {
    static uint8_t done[MAX_V];
    for (int v = 0; v < g->nv; v++) { dist[v] = INF; cnt[v] = 0; done[v] = 0; }
    g_hn = 0; dist[src] = 0; cnt[src] = 1; hpush(0, src, 0);
    while (g_hn) {
        ent_t t = hpop();
        if (done[t.v] || t.d > dist[t.v]) continue;
        int u = t.v; done[u] = 1;                     /* 이 순간 cnt[u] 도 최종: 앞선 정점은 dist 가 엄격히 작다 */
        for (int e = g->head[u]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e];
            int32_t nd = t.d + g->w[e];
            if (nd < dist[v])       { dist[v] = nd; cnt[v] = cnt[u]; hpush(nd, v, 0); }
            else if (nd == dist[v]) { cnt[v] += cnt[u]; }
        }
    }
}
/* 참조: 단순 경로 전수 DFS (w >= 1 이면 최단 경로는 단순 경로) */
static int32_t ref_best[MAX_V]; static uint64_t ref_cnt[MAX_V]; static uint8_t on_path[MAX_V];
static void ref_dfs(const graph_t *g, int u, int32_t len) {
    if (len < ref_best[u]) { ref_best[u] = len; ref_cnt[u] = 1; } else if (len == ref_best[u]) ref_cnt[u]++;
    on_path[u] = 1;
    for (int e = g->head[u]; e != NONE; e = g->nxt[e]) if (!on_path[g->to[e]]) ref_dfs(g, g->to[e], len + g->w[e]);
    on_path[u] = 0;
}

/* ---------- 연습 3: 최대 대역폭 경로 ---------- */
/* width[v] = src→v 경로들 중 (경로의 최소 링크 용량) 의 최댓값. src 는 INF, 도달 불가 0.
 * 다익스트라와 같은 탐욕: 아직 확정 안 된 정점 중 width 최대를 확정, (min, max) 로 완화. O(V^2) 배열판 */
static void widest_path(const graph_t *g, int src, int32_t *width) {
    static uint8_t done[MAX_V];
    for (int v = 0; v < g->nv; v++) { width[v] = 0; done[v] = 0; }
    width[src] = INF;
    for (;;) {
        int u = NONE;
        for (int v = 0; v < g->nv; v++) if (!done[v] && width[v] > 0 && (u == NONE || width[v] > width[u])) u = v;
        if (u == NONE) break;
        done[u] = 1;
        for (int e = g->head[u]; e != NONE; e = g->nxt[e]) {
            int32_t c = width[u] < g->w[e] ? width[u] : g->w[e];
            if (c > width[g->to[e]]) width[g->to[e]] = c;
        }
    }
}
/* 참조: 용량 c 마다 "용량 >= c 인 링크만" 으로 BFS. width[v] = v 에 닿는 가장 큰 c */
static void ref_widest(const graph_t *g, int src, int32_t *width) {
    int16_t q[MAX_V]; uint8_t seen[MAX_V];
    for (int v = 0; v < g->nv; v++) width[v] = 0;
    width[src] = INF;
    for (int k = 0; k < g->ne; k++) {
        int32_t c = g->w[k];
        memset(seen, 0, sizeof seen);
        int qh = 0, qt = 0; q[qt++] = (int16_t)src; seen[src] = 1;
        while (qh < qt) {
            int u = q[qh++];
            for (int e = g->head[u]; e != NONE; e = g->nxt[e])
                if (g->w[e] >= c && !seen[g->to[e]]) { seen[g->to[e]] = 1; q[qt++] = g->to[e]; }
        }
        for (int v = 0; v < g->nv; v++) if (v != src && seen[v] && c > width[v]) width[v] = c;
    }
}

/* ---------- 연습 4: 최대 K 홉 ---------- */
#define MAX_K 8
static int32_t best_kh[MAX_V][MAX_K + 1];
static int32_t k_hop_dijkstra(const graph_t *g, int src, int dst, int K) {
    for (int v = 0; v < g->nv; v++) for (int h = 0; h <= K; h++) best_kh[v][h] = INF;
    g_hn = 0; best_kh[src][0] = 0; hpush(0, src, 0);
    while (g_hn) {
        ent_t t = hpop();
        if (t.d > best_kh[t.v][t.aux]) continue;
        if (t.v == dst) return t.d;                    /* 상태 그래프에서 dst 의 첫 확정 = 최소 */
        if (t.aux == K) continue;                      /* 홉 예산 소진 */
        for (int e = g->head[t.v]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e], h = t.aux + 1;
            int32_t nd = t.d + g->w[e];
            if (nd < best_kh[v][h]) { best_kh[v][h] = nd; hpush(nd, v, h); }
        }
    }
    return INF;
}
/* 참조: 홉 층별 DP. L[v] = 최대 r 홉으로 v 까지 최소. 이전 층 복사본에서만 완화 (같은 층 연쇄 금지) */
static int32_t ref_k_hop(const graph_t *g, int src, int dst, int K) {
    int32_t L[MAX_V], P[MAX_V];
    for (int v = 0; v < g->nv; v++) L[v] = INF;
    L[src] = 0;
    for (int r = 1; r <= K; r++) {
        memcpy(P, L, sizeof(int32_t) * (size_t)g->nv);
        for (int u = 0; u < g->nv; u++) {
            if (P[u] == INF) continue;
            for (int e = g->head[u]; e != NONE; e = g->nxt[e])
                if (P[u] + g->w[e] < L[g->to[e]]) L[g->to[e]] = P[u] + g->w[e];
        }
    }
    return L[dst];
}

/* ---------- 연습 5: 가장 가까운 게이트웨이 ---------- */
static void nearest_gateway(const graph_t *g, const int *gw, int ngw, int32_t *dist, int16_t *owner) {
    for (int v = 0; v < g->nv; v++) { dist[v] = INF; owner[v] = NONE; }
    g_hn = 0;
    for (int i = 0; i < ngw; i++) if (dist[gw[i]] != 0) { dist[gw[i]] = 0; owner[gw[i]] = (int16_t)gw[i]; hpush(0, gw[i], 0); }
    while (g_hn) {
        ent_t t = hpop();
        if (t.d > dist[t.v]) continue;
        for (int e = g->head[t.v]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e];
            int32_t nd = t.d + g->w[e];
            if (nd < dist[v]) { dist[v] = nd; owner[v] = owner[t.v]; hpush(nd, v, 0); }
        }
    }
}

static graph_t g_g;

int main(void) {
    int32_t d1[MAX_V], d2[MAX_V];
    unsigned pops;

    /* ---- 연습 1 ---- */
    {
        unsigned bad = 0, queries = 0;
        unsigned long pops_full = 0, pops_early = 0;
        for (int t = 0; t < 300; t++) {
            int nv = 1 + (int)(xorshift32() % 60);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(5 * nv + 1)), 0, 30);
            int src = (int)(xorshift32() % (uint32_t)nv);
            unsigned pf;
            dijkstra_full(&g_g, src, d1, &pf);
            for (int dst = 0; dst < nv; dst++) {
                queries++;
                if (dijkstra_to(&g_g, src, dst, &pops) != d1[dst]) bad++;
                pops_full += pf; pops_early += pops;
            }
        }
        printf("연습 1: %u queries, mismatches=%u, pops early/full = %lu/%lu (%.0f%%)\n",
               queries, bad, pops_early, pops_full, 100.0 * (double)pops_early / (double)pops_full);
        CHECK(bad == 0);
        CHECK(pops_early < pops_full);
    }

    /* ---- 연습 2 ---- */
    {
        uint64_t cnt[MAX_V];
        /* 이름 붙은 예: 0→1→3, 0→2→3 모두 2 ms, 0→3 직통 4 ms → dist 2, 2가지 */
        graph_init(&g_g, 4);
        graph_add_edge(&g_g, 0, 1, 1); graph_add_edge(&g_g, 1, 3, 1);
        graph_add_edge(&g_g, 0, 2, 1); graph_add_edge(&g_g, 2, 3, 1); graph_add_edge(&g_g, 0, 3, 4);
        count_shortest(&g_g, 0, d1, cnt);
        CHECK(d1[3] == 2 && cnt[3] == 2);
        /* 다이아몬드 사슬 k 개: 2^k 가지 */
        graph_init(&g_g, 21);
        for (int i = 0; i < 10; i++) {
            graph_add_edge(&g_g, 2 * i, 2 * i + 1, 1); graph_add_edge(&g_g, 2 * i + 1, 2 * i + 2, 1);
            graph_add_edge(&g_g, 2 * i, 2 * i + 2, 2);
        }
        count_shortest(&g_g, 0, d1, cnt);
        CHECK(d1[20] == 20 && cnt[20] == 1024);
        unsigned bad = 0;
        for (int t = 0; t < 400; t++) {
            int nv = 1 + (int)(xorshift32() % 8);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(3 * nv + 1)), 1, 3);
            count_shortest(&g_g, 0, d1, cnt);
            for (int v = 0; v < nv; v++) { ref_best[v] = INF; ref_cnt[v] = 0; on_path[v] = 0; }
            ref_dfs(&g_g, 0, 0);
            for (int v = 0; v < nv; v++) if (d1[v] != ref_best[v] || cnt[v] != ref_cnt[v]) bad++;
        }
        printf("연습 2: 400 random graphs (V<=8, w 1..3) vs simple-path enumeration, mismatches=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 3 ---- */
    {
        /* 지연이 아니라 용량(kbps): 0→1 100, 1→3 20, 0→2 50, 2→3 40 → 3 까지 최대 폭 40 (0→2→3) */
        graph_init(&g_g, 4);
        graph_add_edge(&g_g, 0, 1, 100); graph_add_edge(&g_g, 1, 3, 20);
        graph_add_edge(&g_g, 0, 2, 50);  graph_add_edge(&g_g, 2, 3, 40);
        widest_path(&g_g, 0, d1);
        CHECK(d1[3] == 40 && d1[1] == 100 && d1[0] == INF);
        unsigned bad = 0;
        for (int t = 0; t < 300; t++) {
            int nv = 1 + (int)(xorshift32() % 30);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(4 * nv + 1)), 1, 50);
            int src = (int)(xorshift32() % (uint32_t)nv);
            widest_path(&g_g, src, d1);
            ref_widest(&g_g, src, d2);
            if (memcmp(d1, d2, sizeof(int32_t) * (size_t)nv) != 0) bad++;
        }
        printf("연습 3: 300 random graphs vs threshold-BFS reference, mismatches=%u\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 4 ---- */
    {
        /* 0→1→2→3 각 1 ms (3 홉, 3 ms), 0→3 직통 10 ms. K=1 → 10, K=3 → 3 */
        graph_init(&g_g, 4);
        graph_add_edge(&g_g, 0, 1, 1); graph_add_edge(&g_g, 1, 2, 1); graph_add_edge(&g_g, 2, 3, 1);
        graph_add_edge(&g_g, 0, 3, 10);
        CHECK(k_hop_dijkstra(&g_g, 0, 3, 1) == 10 && k_hop_dijkstra(&g_g, 0, 3, 2) == 10 && k_hop_dijkstra(&g_g, 0, 3, 3) == 3);
        /* 함정 예: dist 만 키로 하면 dist[1]=2(2 홉)가 1→3 을 막는다. 상태 그래프는 0→1→3 = 11 (2 홉) 을 찾는다 */
        graph_init(&g_g, 4);
        graph_add_edge(&g_g, 0, 1, 10); graph_add_edge(&g_g, 0, 2, 1);
        graph_add_edge(&g_g, 2, 1, 1);  graph_add_edge(&g_g, 1, 3, 1);
        CHECK(k_hop_dijkstra(&g_g, 0, 3, 2) == 11 && ref_k_hop(&g_g, 0, 3, 2) == 11 && k_hop_dijkstra(&g_g, 0, 3, 3) == 3);
        unsigned bad = 0, inf_cases = 0;
        for (int t = 0; t < 500; t++) {
            int nv = 1 + (int)(xorshift32() % 25);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(4 * nv + 1)), 0, 20);
            int src = (int)(xorshift32() % (uint32_t)nv), dst = (int)(xorshift32() % (uint32_t)nv);
            int K = (int)(xorshift32() % (MAX_K + 1));
            int32_t a = k_hop_dijkstra(&g_g, src, dst, K), b = ref_k_hop(&g_g, src, dst, K);
            if (a != b) bad++;
            if (b == INF) inf_cases++;
        }
        printf("연습 4: 500 random (graph, K) cases vs hop-layer DP, mismatches=%u (unreachable within K: %u)\n", bad, inf_cases);
        CHECK(bad == 0);
    }

    /* ---- 연습 5 ---- */
    {
        int16_t owner[MAX_V];
        unsigned bad = 0;
        for (int t = 0; t < 300; t++) {
            int nv = 1 + (int)(xorshift32() % 40);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(4 * nv + 1)), 0, 40);
            int gw[4], ngw = 1 + (int)(xorshift32() % 4);
            for (int i = 0; i < ngw; i++) gw[i] = (int)(xorshift32() % (uint32_t)nv);   /* 중복 게이트웨이 가능 */
            nearest_gateway(&g_g, gw, ngw, d1, owner);
            for (int v = 0; v < nv; v++) d2[v] = INF;
            static int32_t per[4][MAX_V];
            for (int i = 0; i < ngw; i++) {
                dijkstra_full(&g_g, gw[i], per[i], &pops);
                for (int v = 0; v < nv; v++) if (per[i][v] < d2[v]) d2[v] = per[i][v];
            }
            for (int v = 0; v < nv; v++) {
                if (d1[v] != d2[v]) { bad++; continue; }
                if (d1[v] == INF) { if (owner[v] != NONE) bad++; continue; }
                int ok = 0;                                    /* owner 가 실제로 그 거리를 내는 게이트웨이인가 */
                for (int i = 0; i < ngw; i++) if (gw[i] == owner[v] && per[i][v] == d1[v]) ok = 1;
                if (!ok) bad++;
            }
        }
        printf("연습 5: 300 random graphs, multi-source vs min of per-gateway runs, mismatches=%u\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
