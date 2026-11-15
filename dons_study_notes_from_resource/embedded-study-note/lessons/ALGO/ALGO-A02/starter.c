/* ALGO-A02 벨만-포드와 플로이드-워셜
 *
 * 음수 간선이 있는 그래프의 최단 경로. 동적 할당 없음 (정적 배열 + MAX_* 상수).
 *   Part A  벨만-포드(Bellman-Ford): 간선 목록 완화 + 조기 종료, n 번째 패스에서 음수 사이클 검출,
 *           pred 를 n 번 거슬러 올라가 사이클 "안"에 들어간 뒤 실제 사이클(간선 열) 추출
 *   Part B  환율 차익(arbitrage): 가중치 = -log(rate) 를 2^-32 고정소수점 정수로 (올림 + 1 → 거짓 양성 0),
 *           double 로 하면 곱이 정확히 1 인 표에서도 "차익" 이 나오는 것을 보인다
 *   Part C  플로이드-워셜(Floyd–Warshall): 노드 간 지연 행렬 + next[i][j] 경로 복원 테이블,
 *           INF 가드 · 음수 쪽 포화(clamp) · d[i][i] < 0 검출 · -∞ 쌍 표시. 다익스트라·벨만-포드 반복과 대조
 *
 * 테스트: 손 계산 예제 · 단순 경로 전수(brute force) 와 랜덤 그래프 대조 · 퍼텐셜로 만든 음수 간선 그래프 ·
 *         음수 사이클 검출 vs 단순 사이클 전수 · 환율표 정확 곱(큰 정수) 대조 · 4정점 그래프 전수(3^12).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so -lm && ./so   (starter.c 도 같은 명령)
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 64           /* 테스트용. 정점 id 는 int16_t / uint8_t 에 들어간다 (Part C 는 n <= 256 까지 설계) */
#define MAX_E 4096         /* 간선 번호는 int16_t (pred_e) 에 들어간다 */
#define BF_INF INT64_MAX   /* 벨만-포드 도달 불가 */
#define BF_ALL (-1)        /* src 대신: 가상 출발점(모든 정점 dist = 0) — 어디에 있든 음수 사이클을 찾는다 */
enum { BF_OK = 0, BF_NEGCYCLE = 1 };

/* ======================= 그래프: 간선 목록 (edge list) ======================= */
/* 벨만-포드는 "모든 간선을 한 번씩" 훑는 게 전부라 간선 배열이 가장 자연스럽다.
 * 가중치 범위 |w| <= 2^40, n <= 256, m <= 4096 이면 int64 dist 가 넘치지 않는다 (5. 풀이 Part A). */
typedef struct { int16_t u, v; int64_t w; } edge_t;
typedef struct { int n, m; edge_t e[MAX_E]; } graph_t;

static void graph_init(graph_t *g, int n) { g->n = n; g->m = 0; }
static int graph_add(graph_t *g, int u, int v, int64_t w) {
    if (g->m >= MAX_E || u < 0 || u >= g->n || v < 0 || v >= g->n) return -1;
    g->e[g->m].u = (int16_t)u; g->e[g->m].v = (int16_t)v; g->e[g->m].w = w;
    g->m++;
    return 0;
}

typedef struct { int passes; int relax; } bf_stats_t;

/* ======================= Part A — 벨만-포드 ======================= */
/* 음수 사이클 추출. v = n 번째 패스에서 방금 완화된 정점 (pred_e[v] 도 방금 바뀜).
 * cyc_e[0..len) 에 사이클 간선 번호를 "정방향" 순서로 쓴다: e[cyc_e[t]].v == e[cyc_e[t+1]].u, 마지막은 처음으로.
 * v 자신은 사이클 위가 아닐 수 있다(꼬리) → 먼저 pred 를 n 번 따라가 사이클 안으로 들어간다. 반환 = 길이, 실패 -1 */
static int bf_extract_cycle(const graph_t *g, const int16_t *pred_e, int v, int *cyc_e) {
    (void)g; (void)pred_e; (void)v; (void)cyc_e;
    return 0; /* TODO: pred 를 n 번 거슬러 올라가 사이클 안으로 → 한 바퀴 모으고 → 뒤집어 정방향 */
}

/* src >= 0: 단일 출발점. src == BF_ALL: 모든 dist 를 0 으로 시작(가상 출발점).
 * dist[v]: 최단 거리 (도달 불가 BF_INF). pred_e[v]: 마지막으로 성공한 완화의 간선 번호 (-1 = 없음).
 * 패스(전체 간선 한 번 훑기)는 최대 n 번: 변화 없는 패스가 나오면 즉시 BF_OK (조기 종료).
 * n 번째 패스에서 완화가 일어나면 → 음수 사이클: bf_extract_cycle 로 cyc_e/cyc_len 을 채우고 BF_NEGCYCLE.
 * st->passes = 실행한 패스 수, st->relax = 성공한 완화 수. */
static int bf_run(const graph_t *g, int src, int64_t *dist, int16_t *pred_e,
                  int *cyc_e, int *cyc_len, bf_stats_t *st) {
    for (int v = 0; v < g->n; v++) { dist[v] = BF_INF; pred_e[v] = -1; }
    (void)src; (void)cyc_e;
    st->passes = 0; st->relax = 0; *cyc_len = 0;
    return BF_OK; /* TODO: 초기화(BF_ALL 이면 전부 0) → 최대 n 패스 완화, 변화 없으면 조기 종료, n 번째 패스 완화 = 음수 사이클 */
}

/* 경로 복원 (pred_e 를 따라 src 까지). 음수 사이클이 없을 때만 의미. 반환 = 정점 수, 도달 불가 0, 버퍼 부족 -1 */
static int bf_path(const graph_t *g, const int64_t *dist, const int16_t *pred_e, int src, int dst, int *path, int max) {
    if (dist[dst] == BF_INF) return 0;
    int len = 0, x = dst;
    for (;;) {
        if (len >= max || len >= g->n) return -1;
        path[len++] = x;
        if (x == src) break;
        if (pred_e[x] < 0) return -1;
        x = g->e[pred_e[x]].u;
    }
    for (int i = 0, j = len - 1; i < j; i++, j--) { int t = path[i]; path[i] = path[j]; path[j] = t; }
    return len;
}

/* ======================= Part B — 환율 차익 ======================= */
#define MAX_CUR 8
#define QSCALE  100000000ULL   /* 환율은 소수 8자리 고정소수점: q = rate * 1e8 (정수 시세). q = 0 은 시세 없음 */
#define FX_S    32             /* 가중치 = -ln(rate) * 2^32 (정수) */

/* -ln(q / 1e8) 을 2^-FX_S 단위 정수로 "위로" 반올림 + 1.  보장 (5. 풀이 Part B):
 *   true * 2^S < w <= true * 2^S + 2   (double/log 오차 ~2^-46 << 1 이 여유 안에 들어간다)
 * → 정수 사이클 합 < 0 이면 진짜 곱 > 1 (거짓 양성 없음), 진짜 ln(곱) > 2L * 2^-S 이면 반드시 검출. */
static int64_t rate_to_weight(uint64_t q) {
    (void)q;
    return 0; /* TODO: -ln(q/QSCALE) * 2^FX_S 를 올림하고 +1 (ceil, ldexp) */
}

/* rate[i][j] = i 1단위로 얻는 j 의 양 (q 형식, 0 = 시세 없음, i == j 무시).
 * 차익이 있으면 1 을 돌려주고 cyc[0..*len) 에 통화 순서를 쓴다 (cyc[t] → cyc[t+1] → … → cyc[0]). 없으면 0. */
static int arb_detect(int n, const uint64_t rate[][MAX_CUR], int *cyc, int *len) {
    (void)n; (void)rate; (void)cyc;
    *len = 0;
    return 0; /* TODO: 시세마다 간선(i→j, rate_to_weight) → bf_run(BF_ALL) → 사이클 간선의 출발 통화들 */
}

/* (주어진 코드) 흔한 구현: double 로 -log(rate), 엡실론 없이. 곱이 정확히 1 인 표에서 거짓 차익을 낸다 (테스트). */
static int arb_detect_double(int n, const uint64_t rate[][MAX_CUR]) {
    double w[MAX_CUR][MAX_CUR], d[MAX_CUR];
    for (int i = 0; i < n; i++) {
        d[i] = 0.0;
        for (int j = 0; j < n; j++) w[i][j] = rate[i][j] ? -log((double)rate[i][j] / (double)QSCALE) : 0.0;
    }
    for (int pass = 1; pass <= n; pass++) {
        bool changed = false;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (i != j && rate[i][j] && d[i] + w[i][j] < d[j]) {
                    d[j] = d[i] + w[i][j]; changed = true;
                    if (pass == n) return 1;
                }
        if (!changed) return 0;
    }
    return 0;
}

/* ======================= Part C — 플로이드-워셜 ======================= */
/* d 는 int32: n <= 256 이고 |w| <= FW_WMAX 면 음수 사이클이 닿지 않는 쌍의 값은 [-(n-1)W, (n-1)W] ⊂ (-2^30, 2^30).
 * 음수 사이클이 있으면 값이 지수적으로 작아진다(Hougardy 2010) → FW_NEG_INF 에서 포화시킨다. 두 값의 합은
 * [-2^31, 2^31) 안 → int32 덧셈에 UB 없음. FW_NEG_INF 는 실행 후 "-∞ (음수 사이클이 끼어드는 쌍)" 표시도 겸한다. */
#define FW_INF     INT32_MAX
#define FW_NEG_INF (-(1 << 30))
#define FW_WMAX    ((1 << 22) - 1)

/* (주어진 코드) 간선 목록 → 행렬. 평행 간선은 최솟값, d[i][i] = min(0, 자기 루프). nx[i][j] = j (간선이 있을 때).
 * |w| > FW_WMAX 면 -1. */
static int fw_init(const graph_t *g, int32_t d[][MAX_V], uint8_t nx[][MAX_V]) {
    int n = g->n;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) { d[i][j] = (i == j) ? 0 : FW_INF; nx[i][j] = (uint8_t)(i == j ? i : 0); }
    for (int k = 0; k < g->m; k++) {
        const edge_t *e = &g->e[k];
        if (e->w > FW_WMAX || e->w < -FW_WMAX) return -1;
        if ((int32_t)e->w < d[e->u][e->v]) { d[e->u][e->v] = (int32_t)e->w; nx[e->u][e->v] = (uint8_t)e->v; }
    }
    return 0;
}

/* 플로이드-워셜 본체 (k 가 가장 바깥). nx[i][j] = i 에서 j 로 가는 최단 경로의 "다음 홉".
 * INF 피연산자는 건너뛰고(INF + x 를 계산하지 않음), 합이 FW_NEG_INF 보다 작으면 FW_NEG_INF 로 포화.
 * 끝난 뒤: d[k][k] < 0 인 k 의 개수를 센다 (0 = 음수 사이클 없음). 있으면 i ⇝ k ⇝ j 인 모든 쌍을 FW_NEG_INF 로 표시.
 * 반환 = d[k][k] < 0 인 정점 수. */
static int fw_run(int n, int32_t d[][MAX_V], uint8_t nx[][MAX_V]) {
    (void)n; (void)d; (void)nx;
    return 0; /* TODO: k 바깥 3중 루프(INF 건너뛰기, FW_NEG_INF 포화, nx[i][j] = nx[i][k]) → d[k][k] < 0 세기 → -∞ 표시 */
}

/* nx 를 따라 i → j 경로를 path 에 (i, …, j). 반환 = 정점 수. 도달 불가 0.
 * -∞ 쌍(FW_NEG_INF), 버퍼 부족, n 걸음 안에 j 에 못 닿음(방어) → -1. i == j 면 [i] 하나 (1). */
static int fw_path(int n, const int32_t d[][MAX_V], const uint8_t nx[][MAX_V], int i, int j, int *path, int max) {
    (void)n; (void)d; (void)nx; (void)i; (void)j; (void)path; (void)max;
    return 0; /* TODO: INF → 0, FW_NEG_INF → -1, 아니면 x = nx[x][j] 를 j 까지 (n 걸음·max 넘으면 -1) */
}

/* (주어진 코드) 테스트 기준: O(V^2) 배열 다익스트라 (M06 의 힙 없는 판). 가중치 >= 0 전제. */
static void dijkstra_v2(const graph_t *g, int src, int64_t *dist) {
    static int64_t w[MAX_V][MAX_V];
    bool done[MAX_V];
    int n = g->n;
    for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) w[i][j] = BF_INF; dist[i] = BF_INF; done[i] = false; }
    for (int k = 0; k < g->m; k++) if (g->e[k].w < w[g->e[k].u][g->e[k].v]) w[g->e[k].u][g->e[k].v] = g->e[k].w;
    dist[src] = 0;
    for (;;) {
        int u = -1;
        for (int v = 0; v < n; v++) if (!done[v] && dist[v] != BF_INF && (u < 0 || dist[v] < dist[u])) u = v;
        if (u < 0) break;
        done[u] = true;
        for (int v = 0; v < n; v++)
            if (w[u][v] != BF_INF && dist[u] + w[u][v] < dist[v]) dist[v] = dist[u] + w[u][v];
    }
}

/* ======================= 테스트 하네스 ======================= */
#ifndef ALGO_A02_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int64_t rnd(uint32_t *s, int64_t lo, int64_t hi) { return lo + (int64_t)(xs32(s) % (uint32_t)(hi - lo + 1)); }

/* ---- 기준(oracle) 1: 단순 경로·단순 사이클 전수 (n <= 7) ---- */
static int64_t bs_best[MAX_V];
static bool bs_on[MAX_V];
static void bs_dfs(const graph_t *g, int u, int64_t len) {
    if (len < bs_best[u]) bs_best[u] = len;
    for (int k = 0; k < g->m; k++)
        if (g->e[k].u == u && !bs_on[g->e[k].v]) {
            bs_on[g->e[k].v] = true; bs_dfs(g, g->e[k].v, len + g->e[k].w); bs_on[g->e[k].v] = false;
        }
}
static void brute_sp(const graph_t *g, int src, int64_t *best) {          /* 단순 경로 중 최소 */
    for (int v = 0; v < g->n; v++) { bs_best[v] = BF_INF; bs_on[v] = false; }
    bs_on[src] = true; bs_dfs(g, src, 0);
    for (int v = 0; v < g->n; v++) best[v] = bs_best[v];
}
static int cy_start, cy_path[MAX_V], cy_len;
static bool cy_onneg[MAX_V];
static void cy_dfs(const graph_t *g, int u, int64_t len) {             /* 가장 작은 정점 cy_start 에서 시작하는 단순 사이클 */
    for (int k = 0; k < g->m; k++) {
        int v = g->e[k].v;
        if (g->e[k].u != u || v < cy_start) continue;
        if (v == cy_start) {
            if (len + g->e[k].w < 0) for (int t = 0; t < cy_len; t++) cy_onneg[cy_path[t]] = true;
        } else if (!bs_on[v]) {
            bs_on[v] = true; cy_path[cy_len++] = v;
            cy_dfs(g, v, len + g->e[k].w);
            cy_len--; bs_on[v] = false;
        }
    }
}
static void brute_negcycle(const graph_t *g, bool *onneg) {           /* onneg[v] = v 가 어떤 음수 단순 사이클 위 */
    for (int v = 0; v < g->n; v++) { cy_onneg[v] = false; bs_on[v] = false; }
    for (int s = 0; s < g->n; s++) { cy_start = s; cy_len = 1; cy_path[0] = s; bs_on[s] = true; cy_dfs(g, s, 0); bs_on[s] = false; }
    for (int v = 0; v < g->n; v++) onneg[v] = cy_onneg[v];
}
static void reach_matrix(const graph_t *g, bool r[][MAX_V]) {          /* r[i][j] = i ⇝ j (자기 자신 포함) */
    int n = g->n;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) r[i][j] = (i == j);
    for (int k = 0; k < g->m; k++) r[g->e[k].u][g->e[k].v] = true;
    for (int k = 0; k < n; k++) for (int i = 0; i < n; i++) if (r[i][k]) for (int j = 0; j < n; j++) if (r[k][j]) r[i][j] = true;
}

/* 추출된 사이클 검사: 길이 1..n, 이어지고 닫히며, 정점이 겹치지 않고(단순), 가중치 합 < 0 */
static bool cycle_ok(const graph_t *g, const int *cyc_e, int len) {
    if (len < 1 || len > g->n) return false;
    bool seen[MAX_V] = {false};
    int64_t sum = 0;
    for (int t = 0; t < len; t++) {
        if (cyc_e[t] < 0 || cyc_e[t] >= g->m) return false;
        const edge_t *e = &g->e[cyc_e[t]], *nx = &g->e[cyc_e[(t + 1) % len]];
        if (e->v != nx->u || seen[e->u]) return false;
        seen[e->u] = true; sum += e->w;
    }
    return sum < 0;
}
/* 경로 검사: 실제 간선으로 이어지고 비용 == want (평행 간선은 가장 싼 것) */
static bool path_cost_ok(const graph_t *g, const int *p, int len, int s, int t, int64_t want) {
    if (len < 1 || p[0] != s || p[len - 1] != t) return false;
    int64_t c = 0;
    for (int i = 0; i + 1 < len; i++) {
        int64_t best = BF_INF;
        for (int k = 0; k < g->m; k++) if (g->e[k].u == p[i] && g->e[k].v == p[i + 1] && g->e[k].w < best) best = g->e[k].w;
        if (best == BF_INF) return false;
        c += best;
    }
    return c == want;
}

/* 랜덤 그래프. mode 0: w in [0,wmax]  mode 1: 퍼텐셜로 음수 간선(음수 사이클 없음)  mode 2: 아무 음수 */
static void gen_graph(graph_t *g, uint32_t *s, int n, int m, int64_t wmax, int mode) {
    int64_t pot[MAX_V];
    graph_init(g, n);
    for (int v = 0; v < n; v++) pot[v] = rnd(s, 0, wmax);
    for (int k = 0; k < m; k++) {
        int u = (int)rnd(s, 0, n - 1), v = (int)rnd(s, 0, n - 1);
        int64_t w = rnd(s, 0, wmax);
        if (mode == 1) w = w + pot[u] - pot[v];       /* 사이클 합 = 원래 합 >= 0 */
        if (mode == 2) w = rnd(s, -wmax / 3, wmax);
        graph_add(g, u, v, w);
    }
}

/* ---- 큰 정수 (환율 곱을 정확히 비교하는 기준용) ---- */
typedef struct { int n; uint32_t d[24]; } bn_t;
static void bn_set(bn_t *a, uint64_t v) { a->n = 0; while (v) { a->d[a->n++] = (uint32_t)v; v >>= 32; } }
static void bn_mul(bn_t *a, uint64_t m) {                 /* a *= m */
    bn_t b; bn_set(&b, m);
    uint32_t r[24] = {0};
    for (int i = 0; i < a->n; i++) {
        uint64_t c = 0;
        for (int j = 0; j < b.n; j++) { uint64_t t = (uint64_t)a->d[i] * b.d[j] + r[i + j] + c; r[i + j] = (uint32_t)t; c = t >> 32; }
        for (int k = i + b.n; c; k++) { uint64_t t = (uint64_t)r[k] + c; r[k] = (uint32_t)t; c = t >> 32; }
    }
    int n = a->n + b.n;
    while (n > 0 && r[n - 1] == 0) n--;
    a->n = n; memcpy(a->d, r, sizeof r);
}
static int bn_cmp(const bn_t *a, const bn_t *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}
/* 통화 순환 cyc[0..L) 의 곱 > 1 인가 (정확히): ∏ q > QSCALE^L */
static bool cycle_profit_exact(const uint64_t rate[][MAX_CUR], const int *cyc, int L) {
    bn_t p, s; bn_set(&p, 1); bn_set(&s, 1);
    for (int t = 0; t < L; t++) {
        uint64_t q = rate[cyc[t]][cyc[(t + 1) % L]];
        if (q == 0) return false;
        bn_mul(&p, q); bn_mul(&s, QSCALE);
    }
    return bn_cmp(&p, &s) > 0;
}
/* 모든 단순 순환 전수: 정확히 이득인 순환이 있나 + 가장 큰 (ln 이득 - 2L*2^-S) */
static int ab_n, ab_cyc[MAX_CUR];
static bool ab_on[MAX_CUR], ab_any;
static double ab_margin;
static void ab_dfs(const uint64_t rate[][MAX_CUR], int L) {
    int u = ab_cyc[L - 1];
    for (int v = ab_cyc[0]; v < ab_n; v++) {
        if (rate[u][v] == 0 || v == u) continue;
        if (v == ab_cyc[0]) {
            if (L >= 2 && cycle_profit_exact(rate, ab_cyc, L)) {
                ab_any = true;
                double lp = 0;
                for (int t = 0; t < L; t++) lp += log((double)rate[ab_cyc[t]][ab_cyc[(t + 1) % L]] / (double)QSCALE);
                double m = lp - 2.0 * L * ldexp(1.0, -FX_S);
                if (m > ab_margin) ab_margin = m;
            }
        } else if (!ab_on[v]) {
            ab_on[v] = true; ab_cyc[L] = v; ab_dfs(rate, L + 1); ab_on[v] = false;
        }
    }
}
static bool brute_arb(int n, const uint64_t rate[][MAX_CUR], double *margin) {
    ab_n = n; ab_any = false; ab_margin = -1.0;
    for (int s = 0; s < n; s++) { memset(ab_on, 0, sizeof ab_on); ab_on[s] = true; ab_cyc[0] = s; ab_dfs(rate, 1); }
    *margin = ab_margin;
    return ab_any;
}

static int32_t D[MAX_V][MAX_V];
static uint8_t NX[MAX_V][MAX_V];

/* Part C 시나리오: 기체 내부 통신망 8노드, 한 방향 링크 지연(µs). 비대칭 링크 있음. */
static const char *const NODE[8] = {"fc", "gw", "radio", "cam", "imu", "esc1", "esc2", "gps"};
enum { FC, GW, RADIO, CAM, IMU, ESC1, ESC2, GPS };
static void build_net(graph_t *g, bool with_fc_esc2) {
    graph_init(g, 8);
    graph_add(g, FC, GW, 120);   graph_add(g, GW, FC, 120);
    graph_add(g, GW, RADIO, 900); graph_add(g, RADIO, GW, 1400);   /* 무선 쪽은 비대칭 */
    graph_add(g, GW, CAM, 300);  graph_add(g, CAM, GW, 300);
    graph_add(g, FC, IMU, 40);   graph_add(g, IMU, FC, 40);
    graph_add(g, FC, ESC1, 80);  graph_add(g, ESC1, FC, 80);
    graph_add(g, ESC1, ESC2, 150); graph_add(g, ESC2, ESC1, 150);
    graph_add(g, GW, GPS, 250);  graph_add(g, GPS, GW, 250);
    graph_add(g, IMU, GPS, 500); graph_add(g, GPS, IMU, 500);
    if (with_fc_esc2) { graph_add(g, FC, ESC2, 200); graph_add(g, ESC2, FC, 200); }
}

int main(void) {
    static graph_t g;
    static int64_t dist[MAX_V], ref[MAX_V];
    static int16_t pred[MAX_V];
    int cyc[MAX_V], L, path[MAX_V + 1];
    bf_stats_t st;
    uint32_t seed = 0xA02u;

    /* ---------- Part A: 손 계산 ---------- */
    {
        graph_init(&g, 5);   /* S=0 A=1 B=2 C=3 D=4, 간선 순서가 일부러 "뒤에서부터" */
        graph_add(&g, 3, 4, 2); graph_add(&g, 2, 3, -3); graph_add(&g, 1, 2, 4); graph_add(&g, 0, 1, 5);
        graph_add(&g, 0, 2, 8); graph_add(&g, 1, 3, 6); graph_add(&g, 4, 1, -1);
        int r = bf_run(&g, 0, dist, pred, cyc, &L, &st);
        CHECK(r == BF_OK && dist[0] == 0 && dist[1] == 5 && dist[2] == 8 && dist[3] == 5 && dist[4] == 7);
        CHECK(st.passes == 4);                                     /* 3 패스에 수렴 + 확인 1 패스 (n=5 보다 하나 적다) */
        int len = bf_path(&g, dist, pred, 0, 4, path, MAX_V);
        CHECK(len == 4 && path[0] == 0 && path[1] == 2 && path[2] == 3 && path[3] == 4);   /* S→B→C→D */
        printf("Part A hand: dist = %lld %lld %lld %lld %lld, passes=%d relax=%d\n", (long long)dist[0], (long long)dist[1],
               (long long)dist[2], (long long)dist[3], (long long)dist[4], st.passes, st.relax);
    }
    /* 사슬: 간선 순서가 정방향이면 2 패스, 역방향이면 n 패스 (n-1 이 실제로 필요한 최악) */
    {
        int n = 40;
        graph_init(&g, n);
        for (int v = 0; v + 1 < n; v++) graph_add(&g, v, v + 1, -1);
        int r1 = bf_run(&g, 0, dist, pred, cyc, &L, &st);
        int p1 = st.passes;
        CHECK(r1 == BF_OK && p1 == 2 && dist[n - 1] == -(n - 1));
        graph_init(&g, n);
        for (int v = n - 2; v >= 0; v--) graph_add(&g, v, v + 1, -1);
        int r2 = bf_run(&g, 0, dist, pred, cyc, &L, &st);
        CHECK(r2 == BF_OK && st.passes == n && dist[n - 1] == -(n - 1));
        printf("Part A chain n=%d: forward edge order passes=%d, reverse order passes=%d\n", n, p1, st.passes);
    }
    /* 꼬리 사례: n 번째 패스에서 처음 완화되는 정점(E=5)은 사이클 위가 아니다 */
    {
        graph_init(&g, 6);
        graph_add(&g, 4, 5, 1); graph_add(&g, 3, 4, 2); graph_add(&g, 2, 3, -3); graph_add(&g, 1, 2, 4);
        graph_add(&g, 0, 1, 5); graph_add(&g, 0, 2, 8); graph_add(&g, 1, 3, 6); graph_add(&g, 4, 1, -9);
        int r = bf_run(&g, 0, dist, pred, cyc, &L, &st);
        CHECK(r == BF_NEGCYCLE && L == 4 && cycle_ok(&g, cyc, L));   /* A→B→C→D→A, 합 4-3+2-9 = -6 */
        bool has5 = false;
        for (int t = 0; t < L; t++) if (g.e[cyc[t]].u == 5) has5 = true;
        CHECK(r == BF_NEGCYCLE && !has5 && st.passes == 6);
        printf("Part A tail case: negcycle len=%d starting at %d, passes=%d\n", L, L > 0 ? g.e[cyc[0]].u : -1, st.passes);
        /* bf_extract_cycle 단독: 손으로 만든 pred (rho 모양 5 ← 4 ← 3 ← 2 ← 1 ← 4). 5 는 꼬리, 6 걸음 뒤 3 에 선다 */
        static const int16_t pe[6] = {-1, 7, 3, 2, 1, 0};
        int len = bf_extract_cycle(&g, pe, 5, cyc);
        CHECK(len == 4 && cycle_ok(&g, cyc, len) && g.e[cyc[0]].u == 3);
    }
    /* 경계: 정점 1개, 음수 자기 루프, 0 가중치 사이클(음수 아님), 평행 간선, src 에서 안 닿는 음수 사이클 */
    {
        graph_init(&g, 1);
        CHECK(bf_run(&g, 0, dist, pred, cyc, &L, &st) == BF_OK && dist[0] == 0 && st.passes == 1);
        graph_add(&g, 0, 0, -1);
        CHECK(bf_run(&g, 0, dist, pred, cyc, &L, &st) == BF_NEGCYCLE && L == 1 && cycle_ok(&g, cyc, L));
        graph_init(&g, 3);
        graph_add(&g, 0, 1, 3); graph_add(&g, 1, 2, 0); graph_add(&g, 2, 1, 0);
        CHECK(bf_run(&g, 0, dist, pred, cyc, &L, &st) == BF_OK && dist[1] == 3 && dist[2] == 3);
        graph_init(&g, 2);
        graph_add(&g, 0, 1, 7); graph_add(&g, 0, 1, -2); graph_add(&g, 1, 0, 3);   /* 평행: -2 를 쓰면 사이클 1 >= 0 */
        CHECK(bf_run(&g, 0, dist, pred, cyc, &L, &st) == BF_OK && dist[1] == -2 && pred[1] == 1);
        graph_add(&g, 1, 0, 1);                                                  /* 이제 -2 + 1 = -1 < 0 */
        CHECK(bf_run(&g, 0, dist, pred, cyc, &L, &st) == BF_NEGCYCLE && L == 2 && cycle_ok(&g, cyc, L));
        graph_init(&g, 4);
        graph_add(&g, 0, 1, 1); graph_add(&g, 2, 3, -5); graph_add(&g, 3, 2, 1);   /* 2↔3 음수, 0 에서 안 닿음 */
        int r0 = bf_run(&g, 0, dist, pred, cyc, &L, &st);
        int ra = bf_run(&g, BF_ALL, dist, pred, cyc, &L, &st);
        CHECK(r0 == BF_OK && ra == BF_NEGCYCLE && L == 2 && cycle_ok(&g, cyc, L));
    }
    /* 랜덤 vs 단순 경로 전수 (음수 사이클 없음: 퍼텐셜) + 아무 음수 (검출 ⇔ 닿는 음수 단순 사이클 존재) */
    {
        int bad_d = 0, bad_det = 0, bad_cyc = 0, bad_path = 0, n_neg = 0, n_cases = 0, max_pass_ok = 1;
        static bool R[MAX_V][MAX_V];
        bool onneg[MAX_V];
        for (int it = 0; it < 3000; it++) {
            int n = (int)rnd(&seed, 1, 7), m = (int)rnd(&seed, 0, 3 * n);
            int mode = it % 3;
            gen_graph(&g, &seed, n, m, (it & 4) ? 20 : 1000, mode == 0 ? 1 : (mode == 1 ? 0 : 2));
            brute_negcycle(&g, onneg);
            reach_matrix(&g, R);
            for (int src = -1; src < n; src++) {
                int s = src < 0 ? BF_ALL : src;
                bool want_neg = false;
                for (int v = 0; v < n; v++) if (onneg[v] && (src < 0 || R[src][v])) want_neg = true;
                int r = bf_run(&g, s, dist, pred, cyc, &L, &st);
                n_cases++;
                if (st.passes > n) max_pass_ok = 0;
                if ((r == BF_NEGCYCLE) != want_neg) { bad_det++; continue; }
                if (r == BF_NEGCYCLE) { n_neg++; if (!cycle_ok(&g, cyc, L)) bad_cyc++; continue; }
                if (src < 0) continue;
                brute_sp(&g, src, ref);
                for (int v = 0; v < n; v++) {
                    if (dist[v] != ref[v]) bad_d++;
                    else if (dist[v] != BF_INF) {
                        int len = bf_path(&g, dist, pred, src, v, path, MAX_V);
                        if (len < 1 || !path_cost_ok(&g, path, len, src, v, dist[v])) bad_path++;
                    }
                }
            }
        }
        CHECK(bad_d == 0 && bad_path == 0);
        CHECK(bad_det == 0);
        CHECK(bad_cyc == 0 && n_neg > 1000);
        CHECK(max_pass_ok);
        printf("Part A random vs brute: %d runs (%d with negative cycle), dist mismatch=%d, path bad=%d, "
               "detect mismatch=%d, bad cycle=%d\n", n_cases, n_neg, bad_d, bad_path, bad_det, bad_cyc);
    }

    /* ---------- Part B: 환율 차익 ---------- */
    {
        /* 가중치 성질: 1.0 → 1, 정확한 값보다 크고 2 이하로 크다, 단조 */
        CHECK(rate_to_weight(QSCALE) == 1);
        int wbad = 0;
        uint64_t qs[] = {1, 7, 12345678, 50000000, 99999999, 100000000, 100000001, 125000000, 15025000000ULL, 999999999999ULL};
        for (unsigned i = 0; i < sizeof qs / sizeof qs[0]; i++) {
            long double tw = -logl((long double)qs[i] / (long double)QSCALE) * 4294967296.0L;
            long double w = (long double)rate_to_weight(qs[i]);
            if (!(w > tw && w <= tw + 2.0L)) wbad++;
            if (i > 0 && rate_to_weight(qs[i]) > rate_to_weight(qs[i - 1])) wbad++;
        }
        CHECK(wbad == 0);
    }
    {
        /* 손으로 만든 표. 0 USD, 1 EUR, 2 GBP, 3 JPY.  USD 가치 1, 1.08, 1.27, 0.0067 에 양방향 0.1% 스프레드 */
        static const uint64_t T_NOARB[MAX_CUR][MAX_CUR] = {
            {0, 92500000, 78661417, 14910447761ULL},
            {107892000, 0, 84954331, 16103283582ULL},
            {126873000, 117475000, 0, 18936268657ULL},
            {669330, 619750, 527031, 0}};
        uint64_t T_ARB[MAX_CUR][MAX_CUR];
        memcpy(T_ARB, T_NOARB, sizeof T_ARB);
        T_ARB[1][2] = 85464567;                    /* EUR→GBP 시세가 낡았다: 1.08/1.27 * 1.005 */
        static const uint64_t T_EXACT1[MAX_CUR][MAX_CUR] = {   /* 모든 순환의 곱이 정확히 1 (0.5*0.8*2.5) */
            {0, 50000000, 40000000}, {200000000, 0, 80000000}, {250000000, 125000000, 0}};
        uint64_t T_TINY[MAX_CUR][MAX_CUR];
        memcpy(T_TINY, T_EXACT1, sizeof T_TINY);
        T_TINY[2][0] = 250000250;                  /* 0.5*0.8*2.5000025 = 1.000001 (이득 1 ppm) */

        int cy[MAX_CUR], len;
        double mg;
        CHECK(arb_detect(4, T_NOARB, cy, &len) == 0 && !brute_arb(4, T_NOARB, &mg));
        int a = arb_detect(4, T_ARB, cy, &len);
        bool uses_eg = false;
        for (int t = 0; t < len; t++) if (cy[t] == 1 && cy[(t + 1) % len] == 2) uses_eg = true;
        CHECK(a == 1 && len >= 2 && cycle_profit_exact(T_ARB, cy, len) && uses_eg);
        printf("Part B stale EUR->GBP quote: arbitrage cycle (len %d):", len);
        for (int t = 0; t < len; t++) printf(" %s", (const char *[]){"USD", "EUR", "GBP", "JPY"}[cy[t]]);
        printf("\n");
        int dbl = arb_detect_double(3, T_EXACT1);
        int fx = arb_detect(3, T_EXACT1, cy, &len);
        CHECK(dbl == 1 && fx == 0 && !brute_arb(3, T_EXACT1, &mg));   /* double 은 거짓 차익, 고정소수점은 정확 */
        printf("Part B exact-1 table (0.5*0.8*2.5): double says %s, fixed-point says %s, exact says none\n",
               dbl ? "ARBITRAGE" : "none", fx ? "ARBITRAGE" : "none");
        CHECK(arb_detect(3, T_TINY, cy, &len) == 1 && cycle_profit_exact(T_TINY, cy, len));
    }
    {
        /* 랜덤 환율표 vs 정확한 전수: 거짓 양성 0, 여유(2L*2^-32) 밖의 차익은 전부 검출 */
        int fp = 0, fn = 0, gray = 0, arb_cnt = 0, total = 0, badcyc = 0;
        for (int it = 0; it < 4000; it++) {
            int n = (int)rnd(&seed, 2, 6);
            uint64_t T[MAX_CUR][MAX_CUR];
            double p[MAX_CUR];
            for (int i = 0; i < n; i++) p[i] = exp((double)rnd(&seed, -6000, 6000) / 1000.0);
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++) {
                    double sp = (double)rnd(&seed, -300, 3000) * 1e-6;       /* -0.03% .. +0.3% 스프레드 */
                    if (it % 5 == 0) sp = (double)rnd(&seed, -20, 200) * 1e-9; /* 아주 작은 차익 구간 */
                    T[i][j] = (i == j || rnd(&seed, 0, 9) == 0) ? 0 : (uint64_t)llround(p[i] / p[j] * (1.0 - sp) * (double)QSCALE);
                }
            int cy[MAX_CUR], len;
            double margin;
            bool truth = brute_arb(n, T, &margin);
            int got = arb_detect(n, T, cy, &len);
            total++;
            if (truth) arb_cnt++;
            if (got && !truth) fp++;
            if (got && !cycle_profit_exact(T, cy, len)) badcyc++;
            if (!got && truth) { if (margin > 1e-12) fn++; else gray++; }
        }
        CHECK(fp == 0 && badcyc == 0);
        CHECK(fn == 0);
        printf("Part B random tables: %d tables, %d with exact arbitrage, false positive=%d, missed beyond bound=%d, "
               "missed inside bound (allowed)=%d, bad cycle=%d\n", total, arb_cnt, fp, fn, gray, badcyc);
    }

    /* ---------- Part C: 플로이드-워셜 ---------- */
    {
        build_net(&g, true);
        CHECK(fw_init(&g, D, NX) == 0);
        int neg = fw_run(8, D, NX);
        CHECK(neg == 0 && D[GW][ESC2] == 120 + 200 && D[RADIO][ESC2] == 1400 + 320 && D[CAM][IMU] == 300 + 120 + 40);
        int len = fw_path(8, D, NX, RADIO, ESC2, path, MAX_V);
        CHECK(len == 4 && path[0] == RADIO && path[1] == GW && path[2] == FC && path[3] == ESC2);
        CHECK(D[GW][RADIO] == 900 && D[RADIO][GW] == 1400);     /* 비대칭 그대로 */
        printf("Part C latency matrix (us), row = from:\n      ");
        for (int j = 0; j < 8; j++) printf("%6s", NODE[j]);
        printf("\n");
        for (int i = 0; i < 8; i++) {
            printf("%6s", NODE[i]);
            for (int j = 0; j < 8; j++) { if (D[i][j] == FW_INF) printf("%6s", "INF"); else printf("%6d", D[i][j]); }
            printf("\n");
        }
        printf("Part C next hop from gw:");
        for (int j = 0; j < 8; j++) printf(" %s->%s", NODE[j], NODE[NX[GW][j]]);
        printf("\n");
        /* 링크 장애: fc↔esc2 가 끊기면 esc1 을 거쳐 간다 */
        build_net(&g, false);
        fw_init(&g, D, NX); fw_run(8, D, NX);
        len = fw_path(8, D, NX, RADIO, ESC2, path, MAX_V);
        CHECK(D[RADIO][ESC2] == 1400 + 120 + 80 + 150 && len == 5 && path[3] == ESC1);
        printf("Part C fc<->esc2 down: radio->esc2 = %d us via", D[RADIO][ESC2]);
        for (int t = 0; t < len; t++) printf(" %s", NODE[path[t]]);
        printf("\n");
        CHECK(fw_path(8, D, NX, RADIO, ESC2, path, 3) == -1 && fw_path(8, D, NX, IMU, IMU, path, 1) == 1);
    }
    {
        /* FW vs 다익스트라 반복 (w >= 0) 과 vs 벨만-포드 반복 (음수 간선, 음수 사이클 없음) */
        int bad_dj = 0, bad_bf = 0, bad_path = 0, pairs = 0, unreach = 0;
        for (int it = 0; it < 600; it++) {
            int n = (int)rnd(&seed, 1, 40), m = (int)rnd(&seed, 0, 6 * n);
            int neg = it & 1;
            gen_graph(&g, &seed, n, m, (it & 2) ? 10 : 100000, neg ? 1 : 0);
            if (fw_init(&g, D, NX) != 0) { bad_bf++; continue; }
            if (fw_run(n, D, NX) != 0) { bad_bf++; continue; }
            for (int s = 0; s < n; s++) {
                if (neg) { if (bf_run(&g, s, ref, pred, cyc, &L, &st) != BF_OK) { bad_bf++; continue; } }
                else dijkstra_v2(&g, s, ref);
                for (int t = 0; t < n; t++) {
                    pairs++;
                    int64_t fw = D[s][t] == FW_INF ? BF_INF : D[s][t];
                    if (fw != ref[t]) { if (neg) bad_bf++; else bad_dj++; continue; }
                    if (fw == BF_INF) { unreach++; if (fw_path(n, D, NX, s, t, path, MAX_V) != 0) bad_path++; continue; }
                    int len = fw_path(n, D, NX, s, t, path, MAX_V);
                    if (len < 1 || !path_cost_ok(&g, path, len, s, t, fw)) bad_path++;
                }
            }
        }
        CHECK(bad_dj == 0);
        CHECK(bad_bf == 0);
        CHECK(bad_path == 0);
        printf("Part C FW vs Dijkstra (w>=0) / Bellman-Ford (neg edges): 600 graphs, %d pairs, unreachable %d, "
               "mismatch dijkstra=%d bf=%d, bad path=%d\n", pairs, unreach, bad_dj, bad_bf, bad_path);
    }
    {
        /* 음수 사이클: d[i][i] < 0 검출 ⇔ 전수, -∞ 쌍 ⇔ i ⇝ (음수 사이클 정점) ⇝ j */
        int bad_det = 0, bad_mark = 0, n_negg = 0, bad_path = 0;
        static bool R[MAX_V][MAX_V];
        bool onneg[MAX_V];
        for (int it = 0; it < 3000; it++) {
            int n = (int)rnd(&seed, 1, 7), m = (int)rnd(&seed, 0, 3 * n);
            gen_graph(&g, &seed, n, m, (it & 1) ? 12 : 3000, 2);
            brute_negcycle(&g, onneg);
            reach_matrix(&g, R);
            bool any = false;
            for (int v = 0; v < n; v++) any |= onneg[v];
            fw_init(&g, D, NX);
            int neg = fw_run(n, D, NX);
            int bfall = bf_run(&g, BF_ALL, dist, pred, cyc, &L, &st);
            if ((neg > 0) != any || (bfall == BF_NEGCYCLE) != any) bad_det++;
            if (any) n_negg++;
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++) {
                    bool want = false;
                    for (int v = 0; v < n; v++) if (onneg[v] && R[i][v] && R[v][j]) want = true;
                    if ((D[i][j] == FW_NEG_INF) != want) bad_mark++;
                    if (want && fw_path(n, D, NX, i, j, path, MAX_V) != -1) bad_path++;
                }
        }
        CHECK(bad_det == 0 && n_negg > 500);
        CHECK(bad_mark == 0 && bad_path == 0);
        printf("Part C negative cycles: 3000 graphs (%d with a negative cycle), detect mismatch=%d, -inf mark mismatch=%d\n",
               n_negg, bad_det, bad_mark);
    }
    {
        /* 지수 폭주 가드: 모든 간선 -1 인 완전 그래프 n=40 (가드 없으면 int64 도 n=26 에서 넘친다, demo_bench.c) */
        int n = 40;
        graph_init(&g, n);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (i != j) graph_add(&g, i, j, -1);
        fw_init(&g, D, NX);
        int neg = fw_run(n, D, NX);
        int allneg = 1;
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (D[i][j] != FW_NEG_INF) allneg = 0;
        CHECK(neg == n && allneg);
        /* 경계 가중치: (n-1)*FW_WMAX 짜리 사슬도 넘치지 않는다 (n = 64) */
        n = MAX_V;
        graph_init(&g, n);
        for (int v = 0; v + 1 < n; v++) graph_add(&g, v, v + 1, FW_WMAX);
        CHECK(fw_init(&g, D, NX) == 0 && fw_run(n, D, NX) == 0 && D[0][n - 1] == (int32_t)(n - 1) * FW_WMAX);
        graph_add(&g, 0, 1, FW_WMAX + 1);
        CHECK(fw_init(&g, D, NX) == -1);
    }
    {
        /* 4정점 전수: 방향 간선 12개 각각 {없음, 0, 1} → 3^12 = 531441 개. 0 가중치 사이클이 아주 많다.
         * nx 를 따라가면 항상 n 걸음 안에 도착하고 비용 == d 인가 */
        int bad = 0;
        long cnt = 0;
        for (long code = 0; code < 531441; code++) {
            long c = code;
            graph_init(&g, 4);
            for (int u = 0; u < 4; u++)
                for (int v = 0; v < 4; v++) {
                    if (u == v) continue;
                    int x = (int)(c % 3); c /= 3;
                    if (x) graph_add(&g, u, v, x - 1);
                }
            fw_init(&g, D, NX);
            fw_run(4, D, NX);
            cnt++;
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    if (D[i][j] == FW_INF) continue;
                    int len = fw_path(4, D, NX, i, j, path, MAX_V);
                    if (len < 1 || !path_cost_ok(&g, path, len, i, j, D[i][j])) bad++;
                }
        }
        CHECK(bad == 0);
        printf("Part C exhaustive 4-node {none,0,1}: %ld graphs, bad path=%d\n", cnt, bad);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A02_NO_MAIN */
