// 15_graphs.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 그래프 (Graphs: 인접 리스트, BFS/DFS, DSU, Dijkstra)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 15_graphs.c -o /tmp/andb_15 && /tmp/andb_15
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 구현 순서 제안: (1) graph_add_edge 부터 — 이게 없으면 모든 그래프가 빈 상태.
//   그 다음 BFS -> DFS -> components -> num_islands -> topo -> DSU -> Dijkstra -> cycle.
//
// 임베디드/로보틱스 힌트:
//   - 인접 리스트 = (to,w) 간선 노드의 연결 리스트. 꼬리 append 로 삽입 순서 보존.
//   - BFS = 배열 큐(넣을 때 visited 표시). DFS 반복 = 배열 스택(이웃 역순 push).
//   - topo(Kahn) = indegree 0 부터 벗겨내기. Dijkstra = 미확정 최소 dist 선택 O(V^2).
//   - DSU = find 경로압축 + union 랭크. 방향 사이클 = 3색 DFS(GRAY 재방문=back edge).
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공통 그래프 표현 (인접 리스트)
// ===========================================================================
#define MAXV 64
#define INF  INT_MAX

typedef struct edge {
    int          to;
    int          w;
    struct edge *next;
} edge_t;

typedef struct {
    int     n;
    bool    directed;
    edge_t *adj[MAXV];
} graph_t;

// (유틸) 제공됨.
void graph_init(graph_t *g, int n, bool directed) {
    g->n = n;
    g->directed = directed;
    for (int i = 0; i < MAXV; ++i) g->adj[i] = NULL;
}

// Q1. 간선 추가 = 인접 리스트 구축. directed 면 한 방향, 무향이면 양방향(u->v, v->u).
//   힌트: 새 edge_t 를 malloc 해 (to,w) 세팅, 해당 정점 리스트 꼬리에 append.
void graph_add_edge(graph_t *g, int u, int v, int w) {
    (void)g; (void)u; (void)v; (void)w;
    // TODO: 범위 가드 후 add. 무향이면 반대 방향도 add.
}

// (유틸) 제공됨.
void graph_free(graph_t *g) {
    for (int i = 0; i < g->n; ++i) {
        edge_t *e = g->adj[i];
        while (e) { edge_t *nx = e->next; free(e); e = nx; }
        g->adj[i] = NULL;
    }
}

// Q2. BFS — 소스에서의 방문 순서를 out 에 채우고 방문 정점 수 반환.  O(V+E).
//   힌트: 배열 큐 q[MAXV], visited 는 "큐에 넣을 때" 표시.
int bfs(const graph_t *g, int src, int *out) {
    (void)g; (void)src; (void)out;
    // TODO
    return 0;   // placeholder
}

// Q3a. DFS 재귀 — 전위 순서를 out 에.  방문 수 반환.
int dfs_recursive(const graph_t *g, int src, int *out) {
    (void)g; (void)src; (void)out;
    // TODO: 정적/재귀 헬퍼로 visited 표시 후 이웃 재귀
    return 0;   // placeholder
}

// Q3b. DFS 반복 — 명시적 배열 스택. 재귀와 같은 전위 순서 나오게.
//   힌트: 이웃을 임시배열에 모아 "역순" push, push 시 visited 표시.
int dfs_iterative(const graph_t *g, int src, int *out) {
    (void)g; (void)src; (void)out;
    // TODO
    return 0;   // placeholder
}

// Q4. 연결 요소 개수 (무향). 미방문 정점마다 BFS/DFS 1회.  O(V+E).
int connected_components(const graph_t *g) {
    (void)g;
    // TODO
    return 0;   // placeholder
}

// Q5. 섬의 개수 (2D 그리드). grid=rows*cols 평탄배열, 1=땅 0=물, 4방향 연결.
//   힌트: 1 을 만나면 +1 후 인접 1 을 모두 0 으로 지우는 BFS/DFS 플러드필.
int num_islands(int rows, int cols, int *grid) {
    (void)rows; (void)cols; (void)grid;
    // TODO
    return 0;   // placeholder
}

// Q6. 위상 정렬 (Kahn's BFS). out 에 순서, 반환=처리 정점 수. 사이클이면 -1.
int topological_sort(const graph_t *g, int *out) {
    (void)g; (void)out;
    // TODO: indegree 계산 -> 0 인 것 큐 -> 하나씩 빼며 이웃 indegree--
    return -1;  // placeholder
}

// ===========================================================================
// Q7. Union-Find / DSU
// ===========================================================================
typedef struct {
    int parent[MAXV];
    int rank_[MAXV];
    int count;
} dsu_t;

// Q7a. 초기화: 각자 자기 자신이 루트, count=n.
void dsu_init(dsu_t *d, int n) {
    (void)d; (void)n;
    // TODO
}

// Q7b. find + 경로 압축.
int dsu_find(dsu_t *d, int x) {
    (void)d;
    // TODO: 루트까지 올라가며 경로 압축
    return x;   // placeholder (안전: 무한루프 방지)
}

// Q7c. union (랭크 기반). 이미 같은 집합이면 false.
bool dsu_union(dsu_t *d, int a, int b) {
    (void)d; (void)a; (void)b;
    // TODO
    return false;   // placeholder
}

// (유틸) 제공됨.
bool dsu_connected(dsu_t *d, int a, int b) {
    return dsu_find(d, a) == dsu_find(d, b);
}

// Q8. Dijkstra (음이 아닌 가중치, 배열 기반 O(V^2)). dist 를 채움. 도달불가=INF.
void dijkstra(const graph_t *g, int src, int *dist) {
    (void)src;
    for (int i = 0; i < g->n; ++i) dist[i] = INF;   // 안전 초기화 (placeholder)
    // TODO: dist[src]=0; 매 스텝 미확정 최소 dist 선택 후 relax
}

// Q9. 방향 그래프 사이클 검출 (3색 DFS). GRAY 정점으로 가는 간선(back edge)=사이클.
bool detect_cycle_directed(const graph_t *g) {
    (void)g;
    // TODO
    return false;   // placeholder
}

// ===========================================================================
// 테스트 헬퍼
// ===========================================================================
static bool arr_eq(const int *a, const int *b, int n) {
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}
static bool is_valid_topo(const graph_t *g, const int *order, int n) {
    if (n != g->n) return false;
    int pos[MAXV];
    for (int i = 0; i < n; ++i) pos[order[i]] = i;
    for (int u = 0; u < g->n; ++u)
        for (edge_t *e = g->adj[u]; e; e = e->next)
            if (pos[u] >= pos[e->to]) return false;
    return true;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    int order[MAXV];

    printf("== Q1~Q4 Undirected: adj / BFS / DFS / components ==\n");
    graph_t ug;
    graph_init(&ug, 6, false);
    graph_add_edge(&ug, 0, 1, 1);
    graph_add_edge(&ug, 0, 2, 1);
    graph_add_edge(&ug, 1, 3, 1);
    graph_add_edge(&ug, 2, 3, 1);
    graph_add_edge(&ug, 4, 5, 1);

    T("adj[0] == 1,2",  ug.adj[0] && ug.adj[0]->to == 1 &&
                        ug.adj[0]->next && ug.adj[0]->next->to == 2 &&
                        ug.adj[0]->next->next == NULL);
    T("adj[3] == 1,2 (무향 역방향 포함)",
                        ug.adj[3] && ug.adj[3]->to == 1 &&
                        ug.adj[3]->next && ug.adj[3]->next->to == 2);

    int nb = bfs(&ug, 0, order);
    T("BFS(0) count == 4", nb == 4);
    T("BFS(0) order == 0,1,2,3", nb == 4 && arr_eq(order, (int[]){0,1,2,3}, 4));
    int nb4 = bfs(&ug, 4, order);
    T("BFS(4) order == 4,5", nb4 == 2 && arr_eq(order, (int[]){4,5}, 2));

    int nr = dfs_recursive(&ug, 0, order);
    T("DFS-rec(0) order == 0,1,3,2", nr == 4 && arr_eq(order, (int[]){0,1,3,2}, 4));
    int ni = dfs_iterative(&ug, 0, order);
    T("DFS-iter(0) order == 0,1,3,2", ni == 4 && arr_eq(order, (int[]){0,1,3,2}, 4));

    T("connected_components == 2", connected_components(&ug) == 2);
    graph_free(&ug);

    graph_t sg; graph_init(&sg, 1, false);
    graph_add_edge(&sg, 0, 0, 1);
    T("single self-loop: components == 1", connected_components(&sg) == 1);
    int nbs = bfs(&sg, 0, order);
    T("single self-loop: BFS count == 1", nbs == 1 && order[0] == 0);
    graph_free(&sg);

    graph_t dg; graph_init(&dg, 4, false);
    T("no edges: components == 4", connected_components(&dg) == 4);
    graph_free(&dg);

    printf("== Q5 num_islands (grid flood fill) ==\n");
    int grid[4 * 5] = {
        1,1,0,0,0,
        1,1,0,0,0,
        0,0,1,0,0,
        0,0,0,1,1,
    };
    T("num_islands 4x5 == 3", num_islands(4, 5, grid) == 3);
    int gridEmpty[2 * 2] = {0,0,0,0};
    T("all water == 0 islands", num_islands(2, 2, gridEmpty) == 0);
    int gridFull[2 * 3] = {1,1,1,1,1,1};
    T("all land == 1 island", num_islands(2, 3, gridFull) == 1);
    T("0 rows == 0 islands", num_islands(0, 5, grid) == 0);

    printf("== Q6/Q9 Directed DAG: topo sort / cycle ==\n");
    graph_t dag; graph_init(&dag, 6, true);
    graph_add_edge(&dag, 5, 2, 1);
    graph_add_edge(&dag, 5, 0, 1);
    graph_add_edge(&dag, 4, 0, 1);
    graph_add_edge(&dag, 4, 1, 1);
    graph_add_edge(&dag, 2, 3, 1);
    graph_add_edge(&dag, 3, 1, 1);
    int tcount = topological_sort(&dag, order);
    T("topo sort processes all 6", tcount == 6);
    T("topo order is valid", is_valid_topo(&dag, order, tcount));
    T("DAG has no cycle", !detect_cycle_directed(&dag));
    graph_free(&dag);

    graph_t cyc; graph_init(&cyc, 3, true);
    graph_add_edge(&cyc, 0, 1, 1);
    graph_add_edge(&cyc, 1, 2, 1);
    graph_add_edge(&cyc, 2, 0, 1);
    T("cyclic: detect_cycle == true", detect_cycle_directed(&cyc));
    T("cyclic: topo sort == -1", topological_sort(&cyc, order) == -1);
    graph_free(&cyc);

    graph_t slf; graph_init(&slf, 2, true);
    graph_add_edge(&slf, 0, 0, 1);
    T("self-loop directed: cycle == true", detect_cycle_directed(&slf));
    graph_free(&slf);

    printf("== Q7 Union-Find (DSU) ==\n");
    dsu_t d; dsu_init(&d, 6);
    T("DSU init: count == 6", d.count == 6);
    T("union(0,1) merges", dsu_union(&d, 0, 1));
    T("union(1,2) merges", dsu_union(&d, 1, 2));
    T("union(3,4) merges", dsu_union(&d, 3, 4));
    T("connected(0,2) true", dsu_connected(&d, 0, 2));
    T("connected(0,3) false", !dsu_connected(&d, 0, 3));
    T("count == 3 ({0,1,2},{3,4},{5})", d.count == 3);
    T("union(0,2) already same -> false", !dsu_union(&d, 0, 2));
    T("count unchanged == 3", d.count == 3);
    dsu_union(&d, 2, 4);
    T("after union(2,4): connected(1,3)", dsu_connected(&d, 1, 3));
    T("count == 2", d.count == 2);

    dsu_t d2; dsu_init(&d2, 3);
    dsu_union(&d2, 0, 1);
    dsu_union(&d2, 1, 2);
    T("DSU cycle detect: union(0,2) => cycle", !dsu_union(&d2, 0, 2));

    printf("== Q8 Dijkstra (non-negative weights) ==\n");
    graph_t wg; graph_init(&wg, 5, true);
    graph_add_edge(&wg, 0, 1, 4);
    graph_add_edge(&wg, 0, 2, 1);
    graph_add_edge(&wg, 2, 1, 2);
    graph_add_edge(&wg, 1, 3, 1);
    graph_add_edge(&wg, 2, 3, 5);
    int dist[5];
    dijkstra(&wg, 0, dist);
    T("dist[0] == 0", dist[0] == 0);
    T("dist[2] == 1", dist[2] == 1);
    T("dist[1] == 3 (0->2->1)", dist[1] == 3);
    T("dist[3] == 4 (0->2->1->3)", dist[3] == 4);
    T("dist[4] == INF (unreachable)", dist[4] == INF);
    graph_free(&wg);

    graph_t wu; graph_init(&wu, 4, false);
    graph_add_edge(&wu, 0, 1, 1);
    graph_add_edge(&wu, 1, 2, 2);
    graph_add_edge(&wu, 0, 2, 10);
    graph_add_edge(&wu, 2, 3, 1);
    int dist2[4];
    dijkstra(&wu, 0, dist2);
    T("undirected dist[2] == 3 (0-1-2)", dist2[2] == 3);
    T("undirected dist[3] == 4", dist2[3] == 4);
    graph_free(&wu);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
