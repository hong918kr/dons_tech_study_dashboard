/* ALGO-M06 다익스트라: 가중 그래프 최단 경로
 *
 * 통신 노드 간 지연(ms) 그래프에서 단일 출발점 최단 경로를 구한다. 동적 할당 없음 (정적 배열 + MAX_* 상수).
 *   Part A  인접 리스트(forward star) + indexed min-PQ(decrease-key) 다익스트라, 경로 복원(prev → src..dst)
 *   Part B  lazy deletion 이진 힙 변형(push/pop/stale 개수 비교), 결과 검증기 spt_verify(최단 경로 증명서),
 *           음수 간선 반례 (다익스트라가 틀리는 것을 검증기가 잡는다)
 *   Part C  메시 통신망 라우팅 테이블: 목적지별 지연 + 다음 홉(next hop), 링크 장애 후 재계산
 *
 * 테스트: 손 계산 예제(7정점) · 플로이드-워셜 O(V^3) 참조와 랜덤 그래프 800개 대조(dist 완전 일치,
 *         복원 경로의 비용 == dist, 도달 불가 일치) · 4정점 그래프 전수(간선마다 {없음, 0, 2}, 3^12 개) ·
 *         lazy 와 indexed 의 결과/카운터 항등식 · 검증기에 변조한 답 넣기 · 음수 간선 반례 · 메시 시나리오 2개.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 256          /* 정점 id 는 int16_t 에 들어간다 */
#define MAX_E 2048
#define INF   INT32_MAX    /* 도달 불가. 가중치 0..65535, V <= 256 이면 최장 경로 255*65535 < 2^31 */
#define NONE  (-1)

/* ======================= 그래프: 정적 배열 인접 리스트 (forward star) ======================= */
/* 정점 u 의 나가는 간선: e = head[u]; e != NONE; e = nxt[e]  (추가 역순으로 나온다) */
typedef struct {
    int     nv, ne;
    int16_t head[MAX_V];
    int16_t nxt[MAX_E];
    int16_t to[MAX_E];
    int32_t w[MAX_E];      /* 지연 ms. 다익스트라의 전제는 w >= 0 (Part B 반례에서만 음수) */
} graph_t;

static void graph_init(graph_t *g, int nv) {
    g->nv = nv; g->ne = 0;
    for (int i = 0; i < nv; i++) g->head[i] = NONE;
}
static int graph_add_edge(graph_t *g, int u, int v, int32_t w) {
    if (g->ne >= MAX_E || u < 0 || u >= g->nv || v < 0 || v >= g->nv) return -1;
    int e = g->ne++;
    g->to[e] = (int16_t)v; g->w[e] = w;
    g->nxt[e] = g->head[u]; g->head[u] = (int16_t)e;
    return 0;
}
/* 양방향 링크 = 방향 간선 2개 (지연이 비대칭이면 따로 넣는다) */
static int graph_add_link(graph_t *g, int u, int v, int32_t w) {
    return graph_add_edge(g, u, v, w) | graph_add_edge(g, v, u, w);
}

/* ======================= indexed min-PQ (DS-M11 에서 자세히) ======================= */
/* 원소 = 정점 id, 키 = 외부 배열 dist[id]. decrease-key = "dist[v] 를 줄인 뒤 sift_up(pos[v])".
 * 불변식: pos[heap[i]] == i,  큐에 없으면 pos[v] == NONE.  크기 <= V 보장. */
typedef struct {
    int            n;
    int16_t        heap[MAX_V];
    int16_t        pos[MAX_V];
    const int32_t *key;
} ipq_t;

static void ipq_init(ipq_t *q, int nv, const int32_t *key) {
    q->n = 0; q->key = key;
    for (int i = 0; i < nv; i++) q->pos[i] = NONE;
}
static void ipq_swap(ipq_t *q, int i, int j) {
    int16_t t = q->heap[i]; q->heap[i] = q->heap[j]; q->heap[j] = t;
    q->pos[q->heap[i]] = (int16_t)i; q->pos[q->heap[j]] = (int16_t)j;
}
static void ipq_sift_up(ipq_t *q, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (q->key[q->heap[p]] <= q->key[q->heap[i]]) break;
        ipq_swap(q, i, p); i = p;
    }
}
static void ipq_sift_down(ipq_t *q, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < q->n && q->key[q->heap[l]] < q->key[q->heap[m]]) m = l;
        if (r < q->n && q->key[q->heap[r]] < q->key[q->heap[m]]) m = r;
        if (m == i) return;
        ipq_swap(q, i, m); i = m;
    }
}
static void ipq_push(ipq_t *q, int v)     { q->heap[q->n] = (int16_t)v; q->pos[v] = (int16_t)q->n; q->n++; ipq_sift_up(q, q->n - 1); }
static void ipq_decrease(ipq_t *q, int v) { ipq_sift_up(q, q->pos[v]); }   /* 호출 전에 key[v] 를 이미 줄였다 */
static int  ipq_pop(ipq_t *q) {
    int v = q->heap[0];
    q->n--;
    if (q->n > 0) { q->heap[0] = q->heap[q->n]; q->pos[q->heap[0]] = 0; ipq_sift_down(q, 0); }
    q->pos[v] = NONE;
    return v;
}

/* ======================= lazy deletion 용 (d, v) 최소 힙 (DS-B12 에서 자세히) ======================= */
#define MAX_LH (MAX_E + 1)      /* 음 아닌 가중치에서 push 는 1 + (성공한 완화 수) <= 1 + E */
typedef struct { int32_t d; int16_t v; } lh_ent_t;
typedef struct { int n; lh_ent_t a[MAX_LH]; } lheap_t;

static int lh_push(lheap_t *h, int32_t d, int v) {
    if (h->n >= MAX_LH) return -1;
    int i = h->n++;
    while (i > 0 && h->a[(i - 1) / 2].d > d) { h->a[i] = h->a[(i - 1) / 2]; i = (i - 1) / 2; }
    h->a[i].d = d; h->a[i].v = (int16_t)v;
    return 0;
}
static lh_ent_t lh_pop(lheap_t *h) {
    lh_ent_t top = h->a[0], last = h->a[--h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && h->a[c + 1].d < h->a[c].d) c++;
        if (h->a[c].d >= last.d) break;
        h->a[i] = h->a[c]; i = c;
    }
    if (h->n > 0) h->a[i] = last;
    return top;
}

typedef struct {
    unsigned pushes, pops, decreases, stale, relax_ok, max_size;
} pq_stats_t;

/* ======================= Part A: 다익스트라 (indexed PQ) + 경로 복원 ======================= */
static ipq_t g_ipq;                 /* 정적 저장소: 스택에 1 KB 를 올리지 않는다 */

/* dist[v] = src 에서 v 까지 최소 지연 (도달 불가 INF), prev[v] = 최단 경로에서 v 직전 정점 (src·도달 불가 NONE).
 * 불변식: pop 된 정점(done)의 dist 는 최종값.  반환: 확정(pop)된 정점 수. 전제: 모든 w >= 0. */
static int dijkstra_ipq(const graph_t *g, int src, int32_t *dist, int16_t *prev, pq_stats_t *st) {
    uint8_t done[MAX_V];
    memset(st, 0, sizeof *st);
    for (int v = 0; v < g->nv; v++) { dist[v] = INF; prev[v] = NONE; done[v] = 0; }
    ipq_t *q = &g_ipq;
    ipq_init(q, g->nv, dist);
    dist[src] = 0;
    ipq_push(q, src); st->pushes = 1; st->max_size = 1;
    int settled = 0;
    while (q->n > 0) {
        int u = ipq_pop(q);
        st->pops++; done[u] = 1; settled++;
        for (int e = g->head[u]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e];
            if (done[v]) continue;                      /* 확정된 정점은 다시 안 연다 */
            int32_t nd = dist[u] + g->w[e];             /* dist[u] 는 유한 (pop 된 정점) */
            if (nd >= dist[v]) continue;
            dist[v] = nd; prev[v] = (int16_t)u; st->relax_ok++;
            if (q->pos[v] == NONE) { ipq_push(q, v); st->pushes++; }
            else                   { ipq_decrease(q, v); st->decreases++; }
            if ((unsigned)q->n > st->max_size) st->max_size = (unsigned)q->n;
        }
    }
    return settled;
}

/* out[0..len) = src, ..., dst. 반환: 정점 개수(src == dst 면 1), 도달 불가 0, out 이 작거나 prev 가 깨졌으면 -1 */
static int path_build(const int16_t *prev, int nv, int src, int dst, int16_t *out, int max) {
    if (dst != src && prev[dst] == NONE) return 0;
    int len = 0;
    for (int v = dst; ; v = prev[v]) {
        if (len >= max || len >= nv || v == NONE) return -1;   /* 정점 nv 개를 넘으면 prev 에 사이클 */
        out[len++] = (int16_t)v;
        if (v == src) break;
    }
    for (int i = 0, j = len - 1; i < j; i++, j--) { int16_t t = out[i]; out[i] = out[j]; out[j] = t; }
    return len;
}

/* ======================= Part B: lazy deletion 변형 + 검증기 ======================= */
static lheap_t g_lh;

/* decrease-key 대신 개선될 때마다 (nd, v) 를 또 push, pop 때 d > dist[v] 면 낡은(stale) 엔트리로 버린다.
 * done[] 배열이 없다 — "확정" 판정은 d == dist[u].  반환: 처리(완화)한 pop 수, 힙이 넘치면 -1. */
static int dijkstra_lazy(const graph_t *g, int src, int32_t *dist, int16_t *prev, pq_stats_t *st) {
    memset(st, 0, sizeof *st);
    for (int v = 0; v < g->nv; v++) { dist[v] = INF; prev[v] = NONE; }
    lheap_t *h = &g_lh;
    h->n = 0;
    dist[src] = 0;
    lh_push(h, 0, src); st->pushes = 1; st->max_size = 1;
    int expanded = 0;
    while (h->n > 0) {
        lh_ent_t top = lh_pop(h);
        st->pops++;
        if (top.d > dist[top.v]) { st->stale++; continue; }   /* 더 짧은 값이 이미 처리됨 */
        int u = top.v;
        expanded++;
        for (int e = g->head[u]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e];
            int32_t nd = top.d + g->w[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd; prev[v] = (int16_t)u; st->relax_ok++;
            if (lh_push(h, nd, v) < 0) return -1;
            st->pushes++;
            if ((unsigned)h->n > st->max_size) st->max_size = (unsigned)h->n;
        }
    }
    return expanded;
}

/* 최단 경로 "증명서(certificate)" 검사 — 어떤 알고리즘이 만든 (dist, prev) 든 O(V + E) 에 참/거짓 판정.
 *  (1) dist[src] == 0, prev[src] == NONE
 *  (2) 모든 간선 u→v (dist[u] 유한): dist[v] <= dist[u] + w      → dist <= 진짜 거리 (모든 경로를 따라 귀납)
 *  (3) 유한한 v != src: prev[v] 가 유한 정점이고 간선 prev[v]→v 가 꼭 맞음(tight: dist[p] + w == dist[v]),
 *      prev 를 따라가면 src 에 도착                               → 길이 dist[v] 인 실제 경로가 있다
 *  (4) dist[v] == INF 이면 prev[v] == NONE
 * 반환 1 = 정답임이 증명됨, 0 = 틀림. */
static int spt_verify(const graph_t *g, int src, const int32_t *dist, const int16_t *prev) {
    static uint8_t mark[MAX_V];                 /* 0 미확인, 1 지금 걷는 중, 2 src 에 닿음 */
    if (dist[src] != 0 || prev[src] != NONE) return 0;
    for (int u = 0; u < g->nv; u++) {
        if (dist[u] == INF) continue;
        for (int e = g->head[u]; e != NONE; e = g->nxt[e]) {
            int v = g->to[e];
            if (dist[v] == INF) return 0;                                  /* 도달 가능한데 INF */
            if ((int64_t)dist[u] + g->w[e] < (int64_t)dist[v]) return 0;   /* 삼각 부등식 위반 */
        }
    }
    for (int v = 0; v < g->nv; v++) {
        if (v == src) continue;
        if (dist[v] == INF) { if (prev[v] != NONE) return 0; continue; }
        int p = prev[v];
        if (p < 0 || p >= g->nv || dist[p] == INF) return 0;
        int tight = 0;
        for (int e = g->head[p]; e != NONE; e = g->nxt[e])
            if (g->to[e] == v && (int64_t)dist[p] + g->w[e] == (int64_t)dist[v]) { tight = 1; break; }
        if (!tight) return 0;
    }
    for (int v = 0; v < g->nv; v++) mark[v] = 0;
    mark[src] = 2;
    for (int v = 0; v < g->nv; v++) {               /* prev 사슬이 src 로 가는지 (0 가중치 사이클 차단) */
        if (dist[v] == INF) continue;
        int u = v;
        while (mark[u] == 0) { mark[u] = 1; u = prev[u]; }
        if (mark[u] == 1) return 0;                  /* src 에 닿기 전에 자기 걸음으로 돌아옴 = 사이클 */
        for (u = v; mark[u] == 1; u = prev[u]) mark[u] = 2;
    }
    return 1;
}

/* ======================= Part C: 메시 통신망 라우팅 테이블 ======================= */
typedef struct {
    int32_t dist[MAX_V];       /* 목적지까지 지연 ms */
    int16_t prev[MAX_V];
    int16_t next_hop[MAX_V];   /* src 에서 첫 번째로 보낼 이웃. src 는 src, 도달 불가 NONE */
} route_t;

/* next_hop[v] = 최단 경로 src → ... → v 에서 src 다음 정점. O(V): 위로 걸으며 답을 아는 조상을 만나면
 * 걸어온 정점들을 한 번에 채운다 (각 정점은 스택에 최대 한 번). */
static void route_table(const int32_t *dist, const int16_t *prev, int nv, int src, int16_t *next_hop) {
    static int16_t stk[MAX_V];
    for (int v = 0; v < nv; v++) next_hop[v] = NONE;
    next_hop[src] = (int16_t)src;
    for (int v = 0; v < nv; v++) {
        if (dist[v] == INF || next_hop[v] != NONE) continue;
        int sp = 0, u = v;
        while (next_hop[u] == NONE && prev[u] != src && sp < nv) { stk[sp++] = (int16_t)u; u = prev[u]; }
        int16_t h = (next_hop[u] != NONE) ? next_hop[u] : (int16_t)u;   /* prev[u] == src 면 u 자신이 첫 홉 */
        next_hop[u] = h;
        while (sp > 0) next_hop[stk[--sp]] = h;
    }
}

#define ROUTE_ERR_NEG (-1)
/* 펌웨어 API: 전제(w >= 0)를 O(E) 로 먼저 확인하고 계산한다 */
static int route_compute(const graph_t *g, int src, route_t *r) {
    for (int e = 0; e < g->ne; e++) if (g->w[e] < 0) return ROUTE_ERR_NEG;
    pq_stats_t st;
    dijkstra_ipq(g, src, r->dist, r->prev, &st);
    route_table(r->dist, r->prev, g->nv, src, r->next_hop);
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 플로이드-워셜 참조 (테스트 쪽, O(V^3), 음수 간선도 됨 — 음수 사이클만 없으면). ALGO-A02 에서 자세히 */
#define FW_MAX 48
#define INF64  INT64_MAX
static int64_t g_fw[FW_MAX][FW_MAX];
static void fw_run(const graph_t *g) {
    int n = g->nv;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) g_fw[i][j] = (i == j) ? 0 : INF64;
    for (int u = 0; u < n; u++)
        for (int e = g->head[u]; e != NONE; e = g->nxt[e])
            if (g->w[e] < g_fw[u][g->to[e]]) g_fw[u][g->to[e]] = g->w[e];
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++) {
            if (g_fw[i][k] == INF64) continue;
            for (int j = 0; j < n; j++)
                if (g_fw[k][j] != INF64 && g_fw[i][k] + g_fw[k][j] < g_fw[i][j]) g_fw[i][j] = g_fw[i][k] + g_fw[k][j];
        }
}
/* u→v 직접 간선 중 최소 가중치 (없으면 -1) — 복원한 경로의 비용 계산용 */
static int64_t min_edge(const graph_t *g, int u, int v) {
    int64_t best = -1;
    for (int e = g->head[u]; e != NONE; e = g->nxt[e])
        if (g->to[e] == v && (best < 0 || g->w[e] < best)) best = g->w[e];
    return best;
}

/* dist 가 FW 행 src 와 같은가 + 모든 v 의 복원 경로가 실제 간선이고 비용 == dist 인가. 틀린 개수 반환 */
static unsigned check_vs_fw(const graph_t *g, int src, const int32_t *dist, const int16_t *prev, unsigned *unreach) {
    unsigned bad = 0;
    int16_t path[MAX_V];
    for (int v = 0; v < g->nv; v++) {
        int64_t ref = g_fw[src][v];
        if ((ref == INF64) != (dist[v] == INF)) { bad++; continue; }
        int len = path_build(prev, g->nv, src, v, path, MAX_V);
        if (ref == INF64) { (*unreach)++; if (len != 0) bad++; continue; }
        if (ref != dist[v]) { bad++; continue; }
        if (len < 1 || path[0] != src || path[len - 1] != v) { bad++; continue; }
        int64_t cost = 0;
        for (int i = 0; i + 1 < len; i++) {
            int64_t w = min_edge(g, path[i], path[i + 1]);
            if (w < 0) { cost = -1; break; }
            cost += w;
        }
        if (cost != ref) bad++;
    }
    return bad;
}

static void random_graph(graph_t *g, int nv, int ne, int32_t wmax) {
    graph_init(g, nv);
    for (int i = 0; i < ne; i++)   /* 자기 루프·중복 간선 허용 */
        graph_add_edge(g, (int)(xorshift32() % (uint32_t)nv), (int)(xorshift32() % (uint32_t)nv),
                       (int32_t)(xorshift32() % (uint32_t)(wmax + 1)));
}

static graph_t g_g;
static route_t g_r;

/* Part A 손 계산 예제: 7정점. 6 은 0 에서 도달 불가 (6→0 만 있다) */
static void build_hand(graph_t *g) {
    graph_init(g, 7);
    graph_add_edge(g, 0, 1, 4);  graph_add_edge(g, 0, 2, 1);
    graph_add_edge(g, 2, 1, 2);  graph_add_edge(g, 1, 3, 5);
    graph_add_edge(g, 2, 3, 8);  graph_add_edge(g, 2, 4, 10);
    graph_add_edge(g, 3, 4, 2);  graph_add_edge(g, 4, 5, 3);
    graph_add_edge(g, 3, 5, 6);  graph_add_edge(g, 6, 0, 1);
}

/* Part C 메시: 0 GCS, 1 RLY_N, 2 RLY_S, 3 MAST, 4 UAV1, 5 UAV2, 6 UAV3, 7 UGV1, 8 BUOY(송신 전용) */
enum { GCS, RLY_N, RLY_S, MAST, UAV1, UAV2, UAV3, UGV1, BUOY, N_NODES };
static const char *const k_name[N_NODES] = { "GCS", "RLY_N", "RLY_S", "MAST", "UAV1", "UAV2", "UAV3", "UGV1", "BUOY" };
static const struct { int u, v; int32_t ms; } k_links[] = {
    { GCS, RLY_N, 8 }, { GCS, RLY_S, 5 }, { GCS, MAST, 30 }, { RLY_S, RLY_N, 4 }, { RLY_N, UAV1, 8 },
    { RLY_N, MAST, 9 }, { RLY_S, UGV1, 20 }, { MAST, UAV2, 6 }, { UAV1, UAV2, 3 }, { UAV2, UAV3, 7 },
    { MAST, UAV3, 15 }, { UGV1, UAV3, 25 },
};
static void build_mesh(graph_t *g, int failed_link) {   /* failed_link = k_links 인덱스, -1 이면 장애 없음 */
    graph_init(g, N_NODES);
    for (int i = 0; i < (int)(sizeof k_links / sizeof k_links[0]); i++)
        if (i != failed_link) graph_add_link(g, k_links[i].u, k_links[i].v, k_links[i].ms);
    graph_add_edge(g, BUOY, MAST, 10);                    /* 부이는 올려 보내기만 한다 */
}
static void print_routes(const route_t *r) {
    for (int v = 0; v < N_NODES; v++) {
        if (r->dist[v] == INF) printf("  %-5s  unreachable\n", k_name[v]);
        else printf("  %-5s  %3d ms  next=%s\n", k_name[v], (int)r->dist[v],
                    (r->next_hop[v] >= 0 && r->next_hop[v] < N_NODES) ? k_name[r->next_hop[v]] : "?");
    }
}

int main(void) {
    int32_t dist[MAX_V], dist2[MAX_V];
    int16_t prev[MAX_V], prev2[MAX_V], path[MAX_V];
    pq_stats_t st, st2;

    /* ---- Part A: 손 계산 예제 ---- */
    build_hand(&g_g);
    int settled = dijkstra_ipq(&g_g, 0, dist, prev, &st);
    const int32_t exp_d[7] = { 0, 3, 1, 8, 10, 13, INF };
    const int16_t exp_p[7] = { NONE, 2, 0, 1, 3, 4, NONE };
    CHECK(memcmp(dist, exp_d, sizeof exp_d) == 0);
    CHECK(memcmp(prev, exp_p, sizeof exp_p) == 0);
    CHECK(settled == 6 && st.pops == 6 && st.pushes == 6 && st.decreases == 4 && st.relax_ok == 9);
    printf("Part A hand: dist = %d %d %d %d %d %d INF, pushes=%u decreases=%u pops=%u max_pq=%u\n",
           dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], st.pushes, st.decreases, st.pops, st.max_size);
    {
        const int16_t exp_path[6] = { 0, 2, 1, 3, 4, 5 };
        CHECK(path_build(prev, 7, 0, 5, path, MAX_V) == 6 && memcmp(path, exp_path, sizeof exp_path) == 0);
        CHECK(path_build(prev, 7, 0, 0, path, MAX_V) == 1 && path[0] == 0);    /* src 자신 */
        CHECK(path_build(prev, 7, 0, 6, path, MAX_V) == 0);                    /* 도달 불가 */
        CHECK(path_build(prev, 7, 0, 5, path, 5) == -1);                       /* 버퍼 부족 */
        CHECK(dijkstra_ipq(&g_g, 6, dist, prev, &st) == 7 && dist[0] == 1 && dist[5] == 14);   /* 6 에서는 전부 도달 */
    }
    {   /* 정점 1개, 간선 없음 / 자기 루프만 */
        graph_init(&g_g, 1);
        CHECK(dijkstra_ipq(&g_g, 0, dist, prev, &st) == 1 && dist[0] == 0 && prev[0] == NONE);
        graph_add_edge(&g_g, 0, 0, 5);
        CHECK(dijkstra_ipq(&g_g, 0, dist, prev, &st) == 1 && dist[0] == 0 && st.decreases == 0);
    }

    /* ---- Part A: 랜덤 그래프 vs 플로이드-워셜 ---- */
    {
        unsigned graphs = 0, bad = 0, unreach = 0, edges = 0, maxpq_bad = 0;
        for (int t = 0; t < 800; t++) {
            int nv = 1 + (int)(xorshift32() % 40);
            int ne = (int)(xorshift32() % (uint32_t)(6 * nv + 1));
            int32_t wmax = (t % 2) ? 20 : 65535;               /* 작은 가중치 = 동점·0 이 많다 */
            random_graph(&g_g, nv, ne, wmax);
            int src = (int)(xorshift32() % (uint32_t)nv);
            fw_run(&g_g);
            dijkstra_ipq(&g_g, src, dist, prev, &st);
            bad += check_vs_fw(&g_g, src, dist, prev, &unreach);
            if (st.max_size > (unsigned)nv) maxpq_bad++;
            graphs++; edges += (unsigned)ne;
        }
        printf("Part A random vs Floyd-Warshall: %u graphs, %u edges, unreachable nodes %u, mismatches=%u\n",
               graphs, edges, unreach, bad);
        CHECK(bad == 0);
        CHECK(unreach > 0);          /* 도달 불가 경우가 실제로 시험됐다 */
        CHECK(maxpq_bad == 0);       /* indexed PQ 크기 <= V */
    }

    /* ---- Part A+B: 4정점 전수 — 12개 방향 간선 각각 {없음, 0, 2} ---- */
    {
        unsigned long graphs = 0, bad_fw = 0, bad_lazy = 0, bad_verify = 0;
        unsigned unreach = 0;
        int us[12], vs[12], k = 0;
        for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) if (u != v) { us[k] = u; vs[k] = v; k++; }
        for (uint32_t code = 0; code < 531441u; code++) {     /* 3^12 */
            graph_init(&g_g, 4);
            uint32_t c = code;
            for (int i = 0; i < 12; i++, c /= 3)
                if (c % 3) graph_add_edge(&g_g, us[i], vs[i], (c % 3 == 1) ? 0 : 2);
            fw_run(&g_g);
            dijkstra_ipq(&g_g, 0, dist, prev, &st);
            dijkstra_lazy(&g_g, 0, dist2, prev2, &st2);
            if (check_vs_fw(&g_g, 0, dist, prev, &unreach)) bad_fw++;
            if (memcmp(dist, dist2, 4 * sizeof dist[0]) != 0) bad_lazy++;
            if (!spt_verify(&g_g, 0, dist, prev) || !spt_verify(&g_g, 0, dist2, prev2)) bad_verify++;
            graphs++;
        }
        printf("Part A exhaustive 4-node: %lu graphs, mismatches fw=%lu lazy=%lu verify_reject=%lu\n",
               graphs, bad_fw, bad_lazy, bad_verify);
        CHECK(bad_fw == 0);
        CHECK(bad_lazy == 0);
        CHECK(bad_verify == 0);
    }

    /* ---- Part B: lazy 손 계산 ---- */
    build_hand(&g_g);
    dijkstra_ipq(&g_g, 0, dist, prev, &st);
    int expanded = dijkstra_lazy(&g_g, 0, dist2, prev2, &st2);
    CHECK(memcmp(dist, dist2, 7 * sizeof dist[0]) == 0);
    CHECK(expanded == 6 && st2.pushes == 10 && st2.pops == 10 && st2.stale == 4);
    printf("Part B hand: lazy pushes=%u pops=%u stale=%u max_heap=%u | indexed pushes=%u decreases=%u pops=%u max_pq=%u\n",
           st2.pushes, st2.pops, st2.stale, st2.max_size, st.pushes, st.decreases, st.pops, st.max_size);

    /* ---- Part B: 랜덤 — lazy == indexed, 카운터 항등식 ---- */
    {
        unsigned bad_d = 0, bad_id = 0;
        unsigned long sum_lp = 0, sum_ip = 0, sum_dec = 0, sum_stale = 0;
        for (int t = 0; t < 600; t++) {
            int nv = 1 + (int)(xorshift32() % 200);
            int ne = (int)(xorshift32() % (uint32_t)(8 * nv + 1));
            if (ne > MAX_E) ne = MAX_E;
            random_graph(&g_g, nv, ne, (t % 3) ? 1000 : 3);
            int src = (int)(xorshift32() % (uint32_t)nv);
            int s1 = dijkstra_ipq(&g_g, src, dist, prev, &st);
            int s2 = dijkstra_lazy(&g_g, src, dist2, prev2, &st2);
            if (memcmp(dist, dist2, (size_t)nv * sizeof dist[0]) != 0) bad_d++;
            int ok = s1 == s2                                            /* 처리한 정점 수 = 도달 가능 수 */
                  && st.pushes == (unsigned)s1 && st.pops == (unsigned)s1 /* indexed: 정점당 push/pop 1회 */
                  && st.pushes + st.decreases == 1 + st.relax_ok
                  && st2.pushes == 1 + st2.relax_ok                      /* lazy: 성공한 완화마다 push */
                  && st2.pops == st2.pushes                              /* 끝까지 비운다 */
                  && st2.stale == st2.pushes - (unsigned)s2
                  && st2.pushes <= (unsigned)ne + 1
                  && st.max_size <= (unsigned)nv
                  && spt_verify(&g_g, src, dist, prev) && spt_verify(&g_g, src, dist2, prev2);
            if (!ok) bad_id++;
            sum_lp += st2.pushes; sum_ip += st.pushes; sum_dec += st.decreases; sum_stale += st2.stale;
        }
        printf("Part B random: 600 graphs, dist mismatches=%u, identity failures=%u | total lazy pushes=%lu stale=%lu, indexed pushes=%lu decreases=%lu\n",
               bad_d, bad_id, sum_lp, sum_stale, sum_ip, sum_dec);
        CHECK(bad_d == 0);
        CHECK(bad_id == 0);
    }

    /* ---- Part B: 검증기는 변조된 답을 거부해야 한다 ---- */
    {
        unsigned tried = 0, accepted_bad = 0;
        for (int t = 0; t < 300; t++) {
            int nv = 2 + (int)(xorshift32() % 30);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(4 * nv + 1)), 50);
            int src = (int)(xorshift32() % (uint32_t)nv);
            dijkstra_ipq(&g_g, src, dist, prev, &st);
            for (int m = 0; m < 4; m++) {
                memcpy(dist2, dist, (size_t)nv * sizeof dist[0]);
                memcpy(prev2, prev, (size_t)nv * sizeof prev[0]);
                int v = (int)(xorshift32() % (uint32_t)nv);
                if (m == 0) { if (dist2[v] == INF) continue; dist2[v] += 1; }                    /* 너무 큼 */
                if (m == 1) { if (dist2[v] == INF || dist2[v] == 0) continue; dist2[v] -= 1; }   /* 너무 작음 */
                if (m == 2) { if (v == src || dist2[v] == INF) continue; dist2[v] = INF; prev2[v] = NONE; }
                if (m == 3) { if (dist2[v] != INF) continue; dist2[v] = 0; prev2[v] = (int16_t)src; }   /* 도달 불가를 도달로 */
                tried++;
                if (spt_verify(&g_g, src, dist2, prev2)) accepted_bad++;
            }
        }
        /* 0 가중치 사이클로 prev 가 서로를 가리키는 경우: dist 는 맞아도 경로가 없다 */
        graph_init(&g_g, 3);
        graph_add_edge(&g_g, 0, 1, 5); graph_add_edge(&g_g, 1, 2, 0); graph_add_edge(&g_g, 2, 1, 0);
        const int32_t d_cyc[3] = { 0, 5, 5 };
        const int16_t p_cyc[3] = { NONE, 2, 1 };
        const int16_t p_ok[3]  = { NONE, 0, 1 };
        printf("Part B verifier: %u corrupted answers tried, wrongly accepted=%u\n", tried, accepted_bad);
        CHECK(tried > 500 && accepted_bad == 0);
        CHECK(spt_verify(&g_g, 0, d_cyc, p_cyc) == 0 && spt_verify(&g_g, 0, d_cyc, p_ok) == 1);
    }

    /* ---- Part B: 음수 간선 반례 — s=0, a=1, b=2, t=3 ---- */
    {
        graph_init(&g_g, 4);
        graph_add_edge(&g_g, 0, 1, 2); graph_add_edge(&g_g, 0, 2, 3);
        graph_add_edge(&g_g, 2, 1, -2); graph_add_edge(&g_g, 1, 3, 1);
        fw_run(&g_g);
        dijkstra_ipq(&g_g, 0, dist, prev, &st);
        dijkstra_lazy(&g_g, 0, dist2, prev2, &st2);
        printf("Part B negative edge: true(FW) a=%lld t=%lld | indexed a=%d t=%d verify=%d | lazy a=%d t=%d verify=%d (pops=%u)\n",
               (long long)g_fw[0][1], (long long)g_fw[0][3], dist[1], dist[3], spt_verify(&g_g, 0, dist, prev),
               dist2[1], dist2[3], spt_verify(&g_g, 0, dist2, prev2), st2.pops);
        CHECK(g_fw[0][1] == 1 && g_fw[0][3] == 2);
        CHECK(dist[1] == 2 && dist[3] == 3);                       /* 다익스트라(확정 집합)는 틀린다 */
        CHECK(spt_verify(&g_g, 0, dist, prev) == 0);                /* 그리고 검증기가 잡는다 */
        CHECK(dist2[1] == 1 && dist2[3] == 2 && spt_verify(&g_g, 0, dist2, prev2) == 1);   /* lazy 는 이 경우 다시 연다 */
        CHECK(route_compute(&g_g, 0, &g_r) == ROUTE_ERR_NEG);       /* 펌웨어 API 는 전제부터 거부 */
    }

    /* ---- Part C: 메시 라우팅 테이블 ---- */
    {
        build_mesh(&g_g, -1);
        CHECK(route_compute(&g_g, GCS, &g_r) == 0);
        printf("Part C mesh (from GCS):\n");
        print_routes(&g_r);
        const int32_t exp_d0[N_NODES] = { 0, 8, 5, 17, 16, 19, 26, 25, INF };
        const int16_t exp_h0[N_NODES] = { GCS, RLY_N, RLY_S, RLY_N, RLY_N, RLY_N, RLY_N, RLY_S, NONE };
        CHECK(memcmp(g_r.dist, exp_d0, sizeof exp_d0) == 0);
        CHECK(memcmp(g_r.next_hop, exp_h0, sizeof exp_h0) == 0);
        CHECK(path_build(g_r.prev, N_NODES, GCS, UAV3, path, MAX_V) == 5 && path[1] == RLY_N && path[2] == UAV1
              && path[3] == UAV2);                                  /* 4 홉 26 ms > 2 홉 GCS→MAST→UAV3 45 ms */

        build_mesh(&g_g, 0);                                         /* GCS–RLY_N 링크 장애 */
        CHECK(route_compute(&g_g, GCS, &g_r) == 0);
        printf("Part C mesh, link GCS-RLY_N down:\n");
        print_routes(&g_r);
        const int32_t exp_d1[N_NODES] = { 0, 9, 5, 18, 17, 20, 27, 25, INF };
        const int16_t exp_h1[N_NODES] = { GCS, RLY_S, RLY_S, RLY_S, RLY_S, RLY_S, RLY_S, RLY_S, NONE };
        CHECK(memcmp(g_r.dist, exp_d1, sizeof exp_d1) == 0);
        CHECK(memcmp(g_r.next_hop, exp_h1, sizeof exp_h1) == 0);

        CHECK(route_compute(&g_g, BUOY, &g_r) == 0 && g_r.dist[GCS] == 28 && g_r.next_hop[GCS] == MAST);   /* 부이 → GCS 는 된다 */

        /* next_hop 을 랜덤 그래프에서 prev 사슬 직접 걷기와 대조 */
        unsigned bad = 0;
        for (int t = 0; t < 300; t++) {
            int nv = 1 + (int)(xorshift32() % 120);
            random_graph(&g_g, nv, (int)(xorshift32() % (uint32_t)(3 * nv + 1)), 100);
            int src = (int)(xorshift32() % (uint32_t)nv);
            route_compute(&g_g, src, &g_r);
            for (int v = 0; v < nv; v++) {
                int16_t want = NONE;
                if (v == src) want = (int16_t)src;
                else if (g_r.dist[v] != INF) {
                    int u = v, steps = 0;
                    while (g_r.prev[u] != src && g_r.prev[u] != NONE && steps++ < nv) u = g_r.prev[u];
                    want = (int16_t)u;
                }
                if (g_r.next_hop[v] != want) bad++;
            }
        }
        printf("Part C next_hop random: 300 graphs, mismatches=%u\n", bad);
        CHECK(bad == 0);
        printf("RAM: graph_t=%zu B, ipq_t=%zu B, lheap_t=%zu B, route_t=%zu B\n",
               sizeof(graph_t), sizeof(ipq_t), sizeof(lheap_t), sizeof(route_t));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
