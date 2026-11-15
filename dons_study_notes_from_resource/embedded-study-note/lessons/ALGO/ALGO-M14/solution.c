/* ALGO-M14 Union-Find: 연결성과 그룹 병합
 *
 * 정적 배열 DSU(Disjoint Set Union) 하나로 세 문제를 푼다. 동적 할당 없음, 재귀 없음.
 *   Part A  dsu_find(경로 반감, path halving) + dsu_union(크기 기준 합치기, union by size) + 연결 요소 수
 *           — BFS 참조와 무작위·전수 대조, 트리 높이 <= floor(log2 n) 확인 (그리고 그 상한이 빡빡함)
 *   Part B  무방향 간선 목록에서 사이클을 처음 만드는 간선(자기 루프·평행 간선 포함),
 *           Kruskal 최소 신장 포레스트(MSF) — Prim O(V^2) 참조 + 부분집합 전수 브루트포스와 총 가중치 대조
 *   Part C  센서 메시 네트워크: 링크가 하나씩 올라올 때 연결 요소 수 / 최대 클러스터 / 게이트웨이 도달 노드 수,
 *           링크 비용표(ETX x10)로 최소 비용 백본 출력 (끊긴 망이면 포레스트)
 *
 * 테스트 쪽 참조 구현(BFS, Prim, 브루트포스)은 DSU 를 전혀 쓰지 않는다 — 독립 검증.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: DSU 핵심 ======================= */
#ifndef DSU_MAX_N
#define DSU_MAX_N 1024
#endif
_Static_assert(DSU_MAX_N <= 65535, "parent/size 가 uint16_t 이므로 n <= 65535");

typedef struct {
    uint16_t parent[DSU_MAX_N];   /* parent[x] == x 이면 x 는 루트(대표) */
    uint16_t size[DSU_MAX_N];     /* 루트에서만 의미: 그 집합의 원소 수 */
    int n;
    int comps;                    /* 현재 집합(= 연결 요소) 수 */
} dsu_t;                          /* 노드당 4 B. 1024 노드 = 4 KB + 8 B */

void dsu_init(dsu_t *d, int n) {
    d->n = n;
    d->comps = n;
    for (int i = 0; i < n; i++) { d->parent[i] = (uint16_t)i; d->size[i] = 1; }
}

/* TODO A1: x 의 루트를 돌려준다. 올라가면서 지나는 노드를 할아버지로 당긴다 (경로 반감). 반복문만. */
int dsu_find(dsu_t *d, int x) {
    while (d->parent[x] != x) {
        d->parent[x] = d->parent[d->parent[x]];   /* 할아버지로 당김: 같은 트리 안의 조상이라 집합은 그대로 */
        x = d->parent[x];
    }
    return x;
}

/* TODO A2: a, b 의 집합을 합친다. 작은 트리의 루트를 큰 트리의 루트 밑에 (크기가 같으면 b 쪽을 a 쪽 밑에).
 * 새로 합쳤으면 true (comps 1 감소), 이미 같은 집합이면 false. */
bool dsu_union(dsu_t *d, int a, int b) {
    int ra = dsu_find(d, a), rb = dsu_find(d, b);
    if (ra == rb) return false;
    if (d->size[ra] < d->size[rb]) { int t = ra; ra = rb; rb = t; }   /* ra = 큰 쪽 */
    d->parent[rb] = (uint16_t)ra;
    d->size[ra] = (uint16_t)(d->size[ra] + d->size[rb]);
    d->comps--;
    return true;
}

bool dsu_same(dsu_t *d, int a, int b) { return dsu_find(d, a) == dsu_find(d, b); }
int  dsu_set_size(dsu_t *d, int x)    { return d->size[dsu_find(d, x)]; }

/* ======================= Part B: 사이클 검출 · Kruskal ======================= */
typedef struct { uint16_t u, v; int32_t w; } edge_t;   /* 무방향 간선, 8 B */

/* TODO B1: e[0..m) 를 순서대로 넣을 때 처음으로 사이클을 닫는 간선의 인덱스. 숲(forest)이면 -1.
 * 자기 루프 (u == v) 와 평행 간선도 사이클이다. d 는 작업 공간 (여기서 n 으로 초기화). */
int first_cycle_edge(dsu_t *d, int n, const edge_t *e, int m) {
    dsu_init(d, n);
    for (int k = 0; k < m; k++)
        if (!dsu_union(d, e[k].u, e[k].v)) return k;   /* 이미 연결된 두 끝점 = 경로가 있었다 → 사이클 */
    return -1;
}

/* (주어짐) order[0..m) = 간선 인덱스를 (w, 인덱스) 오름차순으로. 힙 정렬: 재귀 없음, 추가 메모리 없음 (DS-B12).
 * 키에 인덱스를 넣어 전순서가 되므로 결과는 결정적이다 (같은 가중치면 먼저 입력된 간선이 앞). */
static bool edge_before(const edge_t *e, int i, int j) {
    return e[i].w < e[j].w || (e[i].w == e[j].w && i < j);
}
static void order_sift_down(const edge_t *e, int *ord, int i, int len) {
    for (;;) {
        int c = 2 * i + 1;
        if (c >= len) return;
        if (c + 1 < len && edge_before(e, ord[c], ord[c + 1])) c++;
        if (!edge_before(e, ord[i], ord[c])) return;
        int t = ord[i]; ord[i] = ord[c]; ord[c] = t;
        i = c;
    }
}
void sort_edges_by_weight(const edge_t *e, int m, int *order) {
    for (int i = 0; i < m; i++) order[i] = i;
    for (int s = m / 2 - 1; s >= 0; s--) order_sift_down(e, order, s, m);
    for (int end = m - 1; end > 0; end--) {
        int t = order[0]; order[0] = order[end]; order[end] = t;
        order_sift_down(e, order, 0, end);
    }
}

/* TODO B2: Kruskal 최소 신장 포레스트. order 는 호출자가 준 m 칸 작업 버퍼, chosen 은 n-1 칸 출력 버퍼.
 * 반환 = 고른 간선 수 (= n - 연결 요소 수), chosen[] = 고른 간선 인덱스(가중치 오름차순), *total = 가중치 합. */
int kruskal_msf(dsu_t *d, int n, const edge_t *e, int m, int *order, int *chosen, int64_t *total) {
    dsu_init(d, n);
    sort_edges_by_weight(e, m, order);
    int cnt = 0;
    int64_t sum = 0;
    for (int k = 0; k < m && cnt < n - 1; k++) {          /* n-1 개면 신장 트리 완성 → 조기 종료 */
        int i = order[k];
        if (dsu_union(d, e[i].u, e[i].v)) { chosen[cnt++] = i; sum += e[i].w; }
    }
    *total = sum;
    return cnt;
}

/* ======================= Part C: 센서 메시 네트워크 ======================= */
typedef struct {
    dsu_t d;
    int gateway;    /* 게이트웨이(싱크) 노드 번호 */
    int largest;    /* 가장 큰 클러스터 크기 — 링크 추가만 있으면 줄지 않는다 */
} mesh_t;

void mesh_init(mesh_t *m, int n, int gateway) {
    dsu_init(&m->d, n);
    m->gateway = gateway;
    m->largest = n > 0 ? 1 : 0;
}

/* TODO C1: 링크 a-b 가 올라왔다. 두 클러스터가 새로 합쳐졌으면 true, largest 갱신.
 * 이미 같은 클러스터면 (중복 경로 = 메시의 여분 링크) false. */
bool mesh_link_up(mesh_t *m, int a, int b) {
    if (!dsu_union(&m->d, a, b)) return false;
    int s = dsu_set_size(&m->d, a);
    if (s > m->largest) m->largest = s;
    return true;
}

int  mesh_components(const mesh_t *m)          { return m->d.comps; }
int  mesh_largest(const mesh_t *m)             { return m->largest; }
int  mesh_gateway_reach(mesh_t *m)             { return dsu_set_size(&m->d, m->gateway); }  /* 게이트웨이 포함 */
bool mesh_can_reach(mesh_t *m, int a, int b)   { return dsu_same(&m->d, a, b); }

#ifndef ALGO_M14_NO_MAIN
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
static int rnd(int k) { return (int)(xorshift32() % (uint32_t)k); }

/* ---- 참조 1: BFS 연결 요소 (간선 목록 → CSR 인접 리스트). DSU 를 쓰지 않는다. ---- */
#define REF_MAX_N 1024
#define REF_MAX_M 4096
static int ref_off[REF_MAX_N + 1], ref_adj[2 * REF_MAX_M], ref_q[REF_MAX_N];
/* e[0..m) 로 만든 그래프의 요소 수. label[v] = 요소 번호 (0, 1, ... 발견 순) */
static int ref_components(int n, const edge_t *e, int m, int *label) {
    memset(ref_off, 0, sizeof(int) * (size_t)(n + 1));
    for (int k = 0; k < m; k++) { ref_off[e[k].u + 1]++; ref_off[e[k].v + 1]++; }
    for (int v = 0; v < n; v++) ref_off[v + 1] += ref_off[v];
    static int fill[REF_MAX_N];
    memcpy(fill, ref_off, sizeof(int) * (size_t)n);
    for (int k = 0; k < m; k++) { ref_adj[fill[e[k].u]++] = e[k].v; ref_adj[fill[e[k].v]++] = e[k].u; }
    for (int v = 0; v < n; v++) label[v] = -1;
    int c = 0;
    for (int s = 0; s < n; s++) {
        if (label[s] >= 0) continue;
        int qh = 0, qt = 0;
        ref_q[qt++] = s; label[s] = c;
        while (qh < qt) {
            int v = ref_q[qh++];
            for (int k = ref_off[v]; k < ref_off[v + 1]; k++)
                if (label[ref_adj[k]] < 0) { label[ref_adj[k]] = c; ref_q[qt++] = ref_adj[k]; }
        }
        c++;
    }
    return c;
}

/* ---- 참조 2: Prim O(V^2) 최소 신장 포레스트 (인접 행렬, 평행 간선은 최소만, 자기 루프 무시) ---- */
#define PRIM_MAX_N 64
static int64_t prim_msf_total(int n, const edge_t *e, int m) {
    static int64_t w[PRIM_MAX_N][PRIM_MAX_N];
    const int64_t INF = INT64_MAX;
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) w[i][j] = INF;
    for (int k = 0; k < m; k++) {
        int u = e[k].u, v = e[k].v;
        if (u == v) continue;
        if (e[k].w < w[u][v]) w[u][v] = w[v][u] = e[k].w;
    }
    bool in[PRIM_MAX_N] = { false };
    int64_t key[PRIM_MAX_N], total = 0;
    for (int s = 0; s < n; s++) {                  /* 아직 안 들어간 정점마다 새 트리 하나 */
        if (in[s]) continue;
        for (int i = 0; i < n; i++) key[i] = INF;
        key[s] = 0;
        for (;;) {
            int best = -1;
            for (int i = 0; i < n; i++)
                if (!in[i] && key[i] != INF && (best < 0 || key[i] < key[best])) best = i;
            if (best < 0) break;                   /* 이 트리에서 더 닿는 정점 없음 */
            in[best] = true; total += key[best];
            for (int i = 0; i < n; i++)
                if (!in[i] && w[best][i] < key[i]) key[i] = w[best][i];
        }
    }
    return total;
}

/* ---- 참조 3: 부분집합 전수 브루트포스 (m <= 12). 신장 포레스트 = |S| == n - c(G) 이고 c(S) == c(G) ---- */
static bool brute_msf_total(int n, const edge_t *e, int m, int64_t *best) {
    static int lab[REF_MAX_N];
    edge_t sub[16];
    int cg = ref_components(n, e, m, lab);
    bool found = false;
    for (uint32_t mask = 0; mask < (1u << m); mask++) {
        int k = 0; int64_t s = 0;
        for (int i = 0; i < m; i++) if (mask >> i & 1u) { sub[k++] = e[i]; s += e[i].w; }
        if (k != n - cg) continue;
        if (ref_components(n, sub, k, lab) != cg) continue;
        if (!found || s < *best) { *best = s; found = true; }
    }
    return found;
}

/* 부모 포인터만 따라가며(변경 없이) 가장 깊은 노드의 깊이 */
static int max_depth_peek(const dsu_t *d) {
    int md = 0;
    for (int x = 0; x < d->n; x++) {
        int dep = 0, y = x;
        while (d->parent[y] != y) { y = d->parent[y]; dep++; }
        if (dep > md) md = dep;
    }
    return md;
}
static int floor_log2(int n) { int b = -1; while (n) { b++; n >>= 1; } return b; }

static dsu_t g_d;
static edge_t g_e[REF_MAX_M];
static int g_lab[REF_MAX_N], g_lab2[REF_MAX_N], g_cnt[REF_MAX_N];
static int g_order[REF_MAX_M], g_chosen[REF_MAX_N];

int main(void) {
    /* ================= Part A ================= */
    {   /* 손 예제: 10 노드 */
        dsu_t *d = &g_d;
        dsu_init(d, 10);
        CHECK(dsu_union(d, 1, 2) && dsu_union(d, 3, 4) && dsu_union(d, 2, 4));
        CHECK(d->comps == 7 && dsu_same(d, 1, 3) && dsu_set_size(d, 4) == 4 && !dsu_same(d, 0, 1));
        CHECK(!dsu_union(d, 1, 4) && !dsu_union(d, 5, 5) && d->comps == 7);   /* 이미 같음 · 자기 자신 */
        dsu_init(d, 0);
        CHECK(d->comps == 0);
        dsu_init(d, 1);
        CHECK(d->comps == 1 && dsu_find(d, 0) == 0 && !dsu_union(d, 0, 0));
    }
    {   /* 전수: n = 4, union 3번의 모든 순서쌍 수열 (16^3 = 4096), 매 단계 BFS 와 비교 */
        unsigned long seqs = 0, bad = 0;
        for (int s = 0; s < 16 * 16 * 16; s++) {
            dsu_t *d = &g_d;
            dsu_init(d, 4);
            int code = s;
            for (int k = 0; k < 3; k++) {
                int a = code % 4, b = (code / 4) % 4; code /= 16;
                ref_components(4, g_e, k, g_lab2);             /* 이번 union 전 상태 */
                bool want = g_lab2[a] != g_lab2[b];
                g_e[k] = (edge_t){ (uint16_t)a, (uint16_t)b, 0 };
                bool got = dsu_union(d, a, b);
                int c = ref_components(4, g_e, k + 1, g_lab);
                if (got != want || d->comps != c) bad++;
                for (int x = 0; x < 4; x++)
                    for (int y = 0; y < 4; y++)
                        if (dsu_same(d, x, y) != (g_lab[x] == g_lab[y])) bad++;
            }
            seqs++;
        }
        printf("Part A exhaustive n=4: %lu sequences x 3 unions, mismatches=%lu\n", seqs, bad);
        CHECK(bad == 0);
    }
    {   /* 무작위: n = 1..64, union 0..3n 번. 매 union 후 요소 수·반환값, 끝에 모든 쌍·크기·깊이 상한 */
        unsigned long ops = 0, bad_ret = 0, bad_comps = 0, bad_pair = 0, bad_size = 0, bad_depth = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + rnd(64), m = rnd(3 * n + 1);
            dsu_t *d = &g_d;
            dsu_init(d, n);
            for (int k = 0; k < m; k++) {
                int a = rnd(n), b = rnd(n);
                ref_components(n, g_e, k, g_lab2);
                bool want = g_lab2[a] != g_lab2[b];
                g_e[k] = (edge_t){ (uint16_t)a, (uint16_t)b, 0 };
                if (dsu_union(d, a, b) != want) bad_ret++;
                if (d->comps != ref_components(n, g_e, k + 1, g_lab)) bad_comps++;
                ops++;
            }
            ref_components(n, g_e, m, g_lab);
            memset(g_cnt, 0, sizeof g_cnt);
            for (int v = 0; v < n; v++) g_cnt[g_lab[v]]++;
            if (max_depth_peek(d) > floor_log2(n)) bad_depth++;      /* 경로 반감 전에 재야 의미가 있다 */
            for (int x = 0; x < n; x++) {
                if (dsu_set_size(d, x) != g_cnt[g_lab[x]]) bad_size++;
                for (int y = 0; y < n; y++)
                    if (dsu_same(d, x, y) != (g_lab[x] == g_lab[y])) bad_pair++;
            }
        }
        printf("Part A random: 300 trials, %lu unions, bad ret=%lu comps=%lu pair=%lu size=%lu depth>log2n=%lu\n",
               ops, bad_ret, bad_comps, bad_pair, bad_size, bad_depth);
        CHECK(bad_ret == 0);
        CHECK(bad_comps == 0);
        CHECK(bad_pair == 0 && bad_size == 0);
        CHECK(bad_depth == 0);
    }
    {   /* n = DSU_MAX_N: 사슬 순서 합치기 → 한 집합. 짝짓기 2배 순서 → 이항 트리, 깊이가 정확히 log2 n (상한이 빡빡함) */
        dsu_t *d = &g_d;
        int n = DSU_MAX_N;
        dsu_init(d, n);
        for (int i = 0; i + 1 < n; i++) dsu_union(d, i, i + 1);
        CHECK(d->comps == 1 && dsu_set_size(d, 0) == n && max_depth_peek(d) <= floor_log2(n));
        dsu_init(d, n);
        for (int s = 1; s < n; s *= 2)
            for (int i = 0; i + s < n; i += 2 * s) dsu_union(d, i, i + s);
        int dep = max_depth_peek(d);
        printf("Part A n=%d: pairwise-doubling unions -> max depth %d (floor(log2 n) = %d)\n", n, dep, floor_log2(n));
        CHECK(d->comps == 1 && dep == floor_log2(n));
        dsu_find(d, n - 1);                                  /* 가장 깊은 노드 한 번 찾기 → 경로 반감 */
        CHECK(max_depth_peek(d) <= dep && dsu_set_size(d, 5) == n);
    }

    /* ================= Part B: 사이클 ================= */
    {
        dsu_t *d = &g_d;
        const edge_t self[] = { { 2, 2, 0 }, { 0, 1, 0 } };
        const edge_t para[] = { { 0, 1, 0 }, { 1, 0, 0 } };
        const edge_t tri[]  = { { 0, 1, 0 }, { 1, 2, 0 }, { 2, 0, 0 }, { 2, 3, 0 } };
        const edge_t tree[] = { { 0, 1, 0 }, { 0, 2, 0 }, { 2, 3, 0 }, { 2, 4, 0 } };
        CHECK(first_cycle_edge(d, 3, self, 2) == 0);         /* 자기 루프 */
        CHECK(first_cycle_edge(d, 2, para, 2) == 1);         /* 평행 간선 */
        CHECK(first_cycle_edge(d, 4, tri, 4) == 2 && first_cycle_edge(d, 5, tree, 4) == -1);
        CHECK(first_cycle_edge(d, 0, NULL, 0) == -1 && first_cycle_edge(d, 7, NULL, 0) == -1);
    }
    {   /* 전수: n = 4, 비순서쌍(자기 루프 포함) 10종으로 만든 길이 0..4 의 모든 간선 수열 (1+10+...+10^4 = 11111) */
        int pu[10], pv[10], np = 0;
        for (int a = 0; a < 4; a++) for (int b = a; b < 4; b++) { pu[np] = a; pv[np] = b; np++; }
        unsigned long bad = 0, seqs = 0, forests = 0;
        for (int len = 0, lim = 1; len <= 4; len++, lim *= 10) {
            for (int s = 0; s < lim; s++) {
                int code = s;
                for (int k = 0; k < len; k++) { int p = code % 10; code /= 10; g_e[k] = (edge_t){ (uint16_t)pu[p], (uint16_t)pv[p], 0 }; }
                int want = -1;
                for (int k = 0; k < len && want < 0; k++) {     /* 정의 그대로: 앞 간선들만으로 u, v 가 이미 연결? */
                    ref_components(4, g_e, k, g_lab);
                    if (g_lab[g_e[k].u] == g_lab[g_e[k].v]) want = k;
                }
                if (want < 0) forests++;
                if (first_cycle_edge(&g_d, 4, g_e, len) != want) bad++;
                seqs++;
            }
        }
        printf("Part B cycle exhaustive n=4: %lu edge sequences of length 0..4 (%lu forests), mismatches=%lu\n", seqs, forests, bad);
        CHECK(bad == 0 && forests > 0);
    }
    {   /* 무작위: n = 1..64, m = 0..2n, 정의 참조 (k 마다 BFS) */
        unsigned long bad = 0, forests = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + rnd(64), m = rnd(2 * n + 1);
            for (int k = 0; k < m; k++) g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), 0 };
            int want = -1;
            for (int k = 0; k < m && want < 0; k++) {
                ref_components(n, g_e, k, g_lab);
                if (g_lab[g_e[k].u] == g_lab[g_e[k].v]) want = k;
            }
            if (want < 0) forests++;
            if (first_cycle_edge(&g_d, n, g_e, m) != want) bad++;
        }
        printf("Part B cycle random: 300 graphs (%lu forests), mismatches=%lu\n", forests, bad);
        CHECK(bad == 0);
    }

    /* ================= Part B: Kruskal ================= */
    {   /* CLRS 그림 23.1 (a..i = 0..8): MST 가중치 37, 간선 8개 */
        const edge_t clrs[] = {
            { 0, 1, 4 }, { 0, 7, 8 }, { 1, 2, 8 }, { 1, 7, 11 }, { 2, 3, 7 }, { 2, 5, 4 }, { 2, 8, 2 },
            { 3, 4, 9 }, { 3, 5, 14 }, { 4, 5, 10 }, { 5, 6, 2 }, { 6, 7, 1 }, { 6, 8, 6 }, { 7, 8, 7 } };
        int64_t tot = 0;
        int k = kruskal_msf(&g_d, 9, clrs, 14, g_order, g_chosen, &tot);
        printf("Part B CLRS 23.1: kruskal edges=%d total=%lld, prim total=%lld\n", k, (long long)tot,
               (long long)prim_msf_total(9, clrs, 14));
        CHECK(k == 8 && tot == 37 && prim_msf_total(9, clrs, 14) == 37);
        CHECK(kruskal_msf(&g_d, 0, NULL, 0, g_order, g_chosen, &tot) == 0 && tot == 0);
        const edge_t loop1[] = { { 0, 0, -5 } };              /* 자기 루프는 절대 안 고른다 */
        CHECK(kruskal_msf(&g_d, 1, loop1, 1, g_order, g_chosen, &tot) == 0 && tot == 0);
    }
    {   /* 무작위 vs Prim: 400 그래프, 절반은 가중치 1..5 (동점 많음), 절반은 -1000..1000 (음수 포함) */
        unsigned long bad_total = 0, bad_cnt = 0, bad_forest = 0, bad_sorted = 0, disconnected = 0;
        for (int t = 0; t < 400; t++) {
            int n = 1 + rnd(40), m = rnd(3 * n + 1);
            for (int k = 0; k < m; k++) {
                int32_t w = (t & 1) ? (int32_t)rnd(2001) - 1000 : 1 + (int32_t)rnd(5);
                g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), w };
            }
            int c = ref_components(n, g_e, m, g_lab);
            if (c > 1) disconnected++;
            int64_t tot = 0;
            int k = kruskal_msf(&g_d, n, g_e, m, g_order, g_chosen, &tot);
            if (tot != prim_msf_total(n, g_e, m)) bad_total++;
            if (k != n - c) bad_cnt++;
            static edge_t sub[REF_MAX_N];
            for (int i = 0; i < k; i++) sub[i] = g_e[g_chosen[i]];
            if (ref_components(n, sub, k, g_lab2) != c) bad_forest++;   /* n-c 간선으로 c 요소 → 사이클 없음 */
            for (int i = 1; i < k; i++) if (sub[i - 1].w > sub[i].w) { bad_sorted++; break; }
        }
        printf("Part B kruskal vs prim: 400 graphs (%lu disconnected), bad total=%lu count=%lu forest=%lu order=%lu\n",
               disconnected, bad_total, bad_cnt, bad_forest, bad_sorted);
        CHECK(bad_total == 0);
        CHECK(bad_cnt == 0 && bad_forest == 0 && bad_sorted == 0);
        CHECK(disconnected > 50);
    }
    {   /* 작은 그래프 전수 브루트포스: n = 1..6, m = 0..10, 가중치 -3..6 */
        unsigned long bad = 0, graphs = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + rnd(6), m = rnd(11);
            for (int k = 0; k < m; k++) g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), (int32_t)rnd(10) - 3 };
            int64_t best = 0, tot = 0;
            if (!brute_msf_total(n, g_e, m, &best)) { bad++; continue; }   /* 신장 포레스트는 항상 존재 */
            kruskal_msf(&g_d, n, g_e, m, g_order, g_chosen, &tot);
            if (tot != best) bad++;
            graphs++;
        }
        printf("Part B kruskal vs brute force (all edge subsets): %lu graphs, mismatches=%lu\n", graphs, bad);
        CHECK(bad == 0);
    }

    /* ================= Part C: 센서 메시 ================= */
    {   /* 손 시나리오: 8 노드, 게이트웨이 0. 기대값은 손으로 쓴 표 */
        static const int ev[8][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 1, 2 }, { 3, 0 }, { 6, 7 }, { 5, 6 }, { 3, 4 } };
        static const int want_merge[8] = { 1, 1, 1, 1, 0, 1, 1, 1 };
        static const int want_comps[8] = { 7, 6, 5, 4, 4, 3, 2, 1 };
        static const int want_large[8] = { 2, 2, 2, 4, 4, 4, 4, 8 };
        static const int want_gw[8]    = { 2, 2, 2, 4, 4, 4, 4, 8 };
        static mesh_t ms;
        mesh_init(&ms, 8, 0);
        int bad = 0;
        printf("Part C scenario:  event   merged comps largest gw_reach\n");
        for (int i = 0; i < 8; i++) {
            bool mg = mesh_link_up(&ms, ev[i][0], ev[i][1]);
            printf("                  %d-%d     %d      %d     %d       %d\n", ev[i][0], ev[i][1], mg,
                   mesh_components(&ms), mesh_largest(&ms), mesh_gateway_reach(&ms));
            if (mg != want_merge[i] || mesh_components(&ms) != want_comps[i] ||
                mesh_largest(&ms) != want_large[i] || mesh_gateway_reach(&ms) != want_gw[i]) bad++;
        }
        CHECK(bad == 0);
        CHECK(mesh_can_reach(&ms, 7, 0));
    }
    {   /* 무작위 링크 스트림 vs BFS: 100 스트림 x 150 이벤트, n = 64 */
        static mesh_t ms;
        unsigned long events = 0, bad = 0;
        for (int t = 0; t < 100; t++) {
            int n = 64, gw = rnd(n);
            mesh_init(&ms, n, gw);
            for (int k = 0; k < 150; k++) {
                int a = rnd(n), b = rnd(n);
                g_e[k] = (edge_t){ (uint16_t)a, (uint16_t)b, 0 };
                mesh_link_up(&ms, a, b);
                int c = ref_components(n, g_e, k + 1, g_lab);
                memset(g_cnt, 0, sizeof(int) * (size_t)n);
                int big = 0;
                for (int v = 0; v < n; v++) if (++g_cnt[g_lab[v]] > big) big = g_cnt[g_lab[v]];
                if (mesh_components(&ms) != c || mesh_largest(&ms) != big || mesh_gateway_reach(&ms) != g_cnt[g_lab[gw]]) bad++;
                for (int q = 0; q < 4; q++) {
                    int x = rnd(n), y = rnd(n);
                    if (mesh_can_reach(&ms, x, y) != (g_lab[x] == g_lab[y])) bad++;
                }
                events++;
            }
        }
        printf("Part C random streams: %lu link events, mismatches=%lu\n", events, bad);
        CHECK(bad == 0);
    }
    {   /* 백본: 링크 비용 = ETX x 10 (기대 전송 횟수, 낮을수록 좋은 링크). 0 = 게이트웨이 */
        static const edge_t links[] = {
            { 0, 1, 12 }, { 0, 2, 15 }, { 1, 2, 11 }, { 1, 3, 20 }, { 2, 3, 14 }, { 2, 4, 30 }, { 3, 4, 13 },
            { 3, 5, 25 }, { 4, 5, 10 }, { 4, 6, 18 }, { 5, 6, 22 }, { 5, 7, 16 }, { 6, 7, 17 }, { 0, 4, 40 } };
        const int L = (int)(sizeof links / sizeof links[0]);
        int64_t tot = 0;
        int k = kruskal_msf(&g_d, 8, links, L, g_order, g_chosen, &tot);
        printf("Part C backbone (8 nodes, %d candidate links):", L);
        for (int i = 0; i < k; i++) printf(" %d-%d(%d)", links[g_chosen[i]].u, links[g_chosen[i]].v, (int)links[g_chosen[i]].w);
        printf("  total=%lld trees=%d\n", (long long)tot, g_d.comps);
        CHECK(k == 7 && tot == prim_msf_total(8, links, L) && g_d.comps == 1);
        /* 노드 7 의 무선 링크 둘(5-7, 6-7)이 죽은 상태 → 포레스트 */
        static edge_t alive[16];
        int na = 0;
        for (int i = 0; i < L; i++) if (links[i].u != 7 && links[i].v != 7) alive[na++] = links[i];
        k = kruskal_msf(&g_d, 8, alive, na, g_order, g_chosen, &tot);
        printf("Part C backbone without node 7 links: edges=%d total=%lld trees=%d, 7 reaches gateway? %s\n",
               k, (long long)tot, g_d.comps, dsu_same(&g_d, 7, 0) ? "yes" : "no");
        CHECK(k == 6 && g_d.comps == 2 && !dsu_same(&g_d, 7, 0) && tot == prim_msf_total(8, alive, na));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_M14_NO_MAIN */
