// 15_graphs.c  —  REFERENCE SOLUTION
// 그래프 (Graphs: 인접 리스트, BFS/DFS, DSU, Dijkstra)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 15_graphs.c -o /tmp/andb_15 && /tmp/andb_15
// (fully self-contained: 인접 리스트 + 인라인 큐/스택 직접 구현, STL 없음)
//
// 임베디드/로보틱스 관점 핵심:
//   - 인접 리스트(adjacency list) = 희소 그래프의 표준 표현. 노드=상태/기체/태스크.
//   - BFS/DFS = 상태공간 탐색, 메시 네트워크 도달성, 그리드 플러드필(num_islands).
//   - topological sort(Kahn) = 태스크/부팅 의존성 순서, 빌드 그래프.
//   - union-find(DSU) = 실시간 클러스터링, 연결 판정, 사이클 검출(무향).
//   - Dijkstra = 경로계획/라우팅(양수 가중치), 최소 지연 경로.
//   - 방향 그래프 사이클 검출(3색 DFS) = 데드락/순환 의존성 진단.
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
// 공통 그래프 표현 (인접 리스트) — Q1 build_adjacency_list
// ---------------------------------------------------------------------------
// 각 정점 u 는 (to, w) 간선 노드들의 단일 연결 리스트를 가진다. 삽입 순서를
// 보존하기 위해 꼬리에 append (BFS/DFS 순서를 결정론적으로 만들기 위함).
// 무향 그래프면 (u->v) 와 (v->u) 두 방향을 모두 추가한다.
// ===========================================================================
#define MAXV 64          // 데모용 최대 정점 수 (정적 상한)
#define INF  INT_MAX

typedef struct edge {
    int          to;     // 도착 정점
    int          w;      // 가중치 (비가중이면 1)
    struct edge *next;
} edge_t;

typedef struct {
    int     n;           // 정점 수
    bool    directed;    // 방향 그래프 여부
    edge_t *adj[MAXV];   // 정점별 인접 리스트 head
} graph_t;

void graph_init(graph_t *g, int n, bool directed) {
    g->n = n;
    g->directed = directed;
    for (int i = 0; i < MAXV; ++i) g->adj[i] = NULL;
}

// 한 방향 간선 (u -> v, w) 을 꼬리에 append. O(deg(u)).
static void add_directed_edge(graph_t *g, int u, int v, int w) {
    edge_t *e = (edge_t *)malloc(sizeof *e);
    if (!e) return;                     // 실 펌웨어라면 실패 반드시 처리
    e->to = v; e->w = w; e->next = NULL;
    edge_t **pp = &g->adj[u];           // 이중 포인터로 꼬리 슬롯 추적
    while (*pp) pp = &(*pp)->next;
    *pp = e;
}

// Q1. 간선 추가 = 인접 리스트 구축의 기본 연산.
//     directed 면 한 방향, 무향이면 양방향 추가.
void graph_add_edge(graph_t *g, int u, int v, int w) {
    if (u < 0 || u >= g->n || v < 0 || v >= g->n) return;  // 범위 가드
    add_directed_edge(g, u, v, w);
    if (!g->directed) add_directed_edge(g, v, u, w);
}

void graph_free(graph_t *g) {
    for (int i = 0; i < g->n; ++i) {
        edge_t *e = g->adj[i];
        while (e) { edge_t *nx = e->next; free(e); e = nx; }
        g->adj[i] = NULL;
    }
}

// ===========================================================================
// Q2. BFS — 소스에서의 방문 순서 반환 (인라인 배열 큐)
// ---------------------------------------------------------------------------
// visited 를 "큐에 넣을 때" 표시(중복 삽입 방지). 각 정점은 한 번만 큐에 들어가므로
// 큐 크기는 정점 수로 충분. 반환값 = 방문한 정점 수.  O(V+E).
// ===========================================================================
int bfs(const graph_t *g, int src, int *out) {
    bool visited[MAXV] = {false};
    int  q[MAXV];                       // 인라인 링/배열 큐
    int  qh = 0, qt = 0;                // head/tail 인덱스
    int  cnt = 0;

    visited[src] = true;
    q[qt++] = src;
    while (qh < qt) {
        int u = q[qh++];                // dequeue
        out[cnt++] = u;
        for (edge_t *e = g->adj[u]; e; e = e->next) {
            if (!visited[e->to]) {
                visited[e->to] = true;  // 넣을 때 표시
                q[qt++] = e->to;        // enqueue
            }
        }
    }
    return cnt;
}

// ===========================================================================
// Q3a. DFS 재귀 — 전위(preorder) 방문 순서
// ===========================================================================
static void dfs_visit(const graph_t *g, int u, bool *visited, int *out, int *cnt) {
    visited[u] = true;
    out[(*cnt)++] = u;
    for (edge_t *e = g->adj[u]; e; e = e->next)
        if (!visited[e->to])
            dfs_visit(g, e->to, visited, out, cnt);
}

int dfs_recursive(const graph_t *g, int src, int *out) {
    bool visited[MAXV] = {false};
    int cnt = 0;
    dfs_visit(g, src, visited, &out[0], &cnt);
    return cnt;
}

// ===========================================================================
// Q3b. DFS 반복 — 명시적 인라인 스택 (재귀와 동일 전위 순서)
// ---------------------------------------------------------------------------
// 이웃을 "역순으로" 스택에 push 해야 pop 시 원래(삽입) 순서로 방문 → 재귀와 일치.
// push 할 때 visited 표시(중복 push 방지) → 스택 크기는 정점 수로 충분.
// ===========================================================================
int dfs_iterative(const graph_t *g, int src, int *out) {
    bool visited[MAXV] = {false};
    int  st[MAXV];                      // 인라인 배열 스택
    int  sp = 0;                        // 스택 포인터
    int  cnt = 0;

    visited[src] = true;
    st[sp++] = src;
    while (sp > 0) {
        int u = st[--sp];               // pop
        out[cnt++] = u;
        // 이웃을 임시 배열에 모아 역순 push → 원래 순서로 방문
        int nbr[MAXV], m = 0;
        for (edge_t *e = g->adj[u]; e; e = e->next) nbr[m++] = e->to;
        for (int i = m - 1; i >= 0; --i) {
            if (!visited[nbr[i]]) {
                visited[nbr[i]] = true; // push 시 표시
                st[sp++] = nbr[i];
            }
        }
    }
    return cnt;
}

// ===========================================================================
// Q4. 연결 요소 개수 (무향 그래프) — 미방문 정점마다 BFS/DFS 1회
// ---------------------------------------------------------------------------
// 방문 안 된 정점을 만날 때마다 새 요소 시작 → 그 요소 전체를 표시. O(V+E).
// ===========================================================================
int connected_components(const graph_t *g) {
    bool visited[MAXV] = {false};
    int  comps = 0;
    int  q[MAXV];
    for (int s = 0; s < g->n; ++s) {
        if (visited[s]) continue;
        ++comps;                        // 새 연결 요소
        int qh = 0, qt = 0;
        visited[s] = true; q[qt++] = s;
        while (qh < qt) {
            int u = q[qh++];
            for (edge_t *e = g->adj[u]; e; e = e->next)
                if (!visited[e->to]) { visited[e->to] = true; q[qt++] = e->to; }
        }
    }
    return comps;
}

// ===========================================================================
// Q5. 섬의 개수 (2D 그리드) — 그리드 플러드필(BFS)
// ---------------------------------------------------------------------------
// grid 는 rows*cols 의 평탄 배열, 1=땅, 0=물. 상하좌우 4방향 연결.
// 1 을 만나면 요소 +1 하고 인접 1 을 모두 0 으로 지워(visited) 재방문 방지.
// 좌표를 int(=r*cols+c)로 인코딩해 인라인 큐에 담는다.  O(rows*cols).
// ===========================================================================
int num_islands(int rows, int cols, int *grid) {
    if (rows <= 0 || cols <= 0) return 0;
    int islands = 0;
    int cap = rows * cols;
    int *q = (int *)malloc(sizeof(int) * (size_t)cap);   // 좌표 큐
    if (!q) return -1;
    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (grid[r * cols + c] != 1) continue;
            ++islands;
            int qh = 0, qt = 0;
            grid[r * cols + c] = 0;      // 방문 표시(가라앉힘)
            q[qt++] = r * cols + c;
            while (qh < qt) {
                int cell = q[qh++];
                int cr = cell / cols, cc = cell % cols;
                for (int d = 0; d < 4; ++d) {
                    int nr = cr + dr[d], nc = cc + dc[d];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    if (grid[nr * cols + nc] == 1) {
                        grid[nr * cols + nc] = 0;
                        q[qt++] = nr * cols + nc;
                    }
                }
            }
        }
    }
    free(q);
    return islands;
}

// ===========================================================================
// Q6. 위상 정렬 (Kahn's BFS) — 진입차수 0 부터 벗겨내기
// ---------------------------------------------------------------------------
// indegree 계산 → 0 인 정점을 큐에. 하나 꺼낼 때마다 out-edge 를 제거(이웃 indegree--),
// 새로 0 이 되면 큐에. 방문 수가 n 미만이면 사이클 → 위상 순서 없음 → -1.
// out 에 순서를 채우고 반환값 = 처리한 정점 수 (== n 이면 성공).  O(V+E).
// ===========================================================================
int topological_sort(const graph_t *g, int *out) {
    int indeg[MAXV] = {0};
    for (int u = 0; u < g->n; ++u)
        for (edge_t *e = g->adj[u]; e; e = e->next) indeg[e->to]++;

    int q[MAXV], qh = 0, qt = 0;
    for (int v = 0; v < g->n; ++v) if (indeg[v] == 0) q[qt++] = v;

    int cnt = 0;
    while (qh < qt) {
        int u = q[qh++];
        out[cnt++] = u;
        for (edge_t *e = g->adj[u]; e; e = e->next)
            if (--indeg[e->to] == 0) q[qt++] = e->to;
    }
    return (cnt == g->n) ? cnt : -1;    // -1 = 사이클 존재
}

// ===========================================================================
// Q7. Union-Find / DSU (경로 압축 + 랭크 기반 합치기)
// ---------------------------------------------------------------------------
// find: 루트까지 올라가며 경로 압축(부모를 루트로 당김). union: 랭크 낮은 트리를
// 높은 트리 밑에 붙임(트리 높이 억제). 두 최적화로 사실상 상수시간(역아커만).
// count = 현재 서로소 집합(=연결 요소) 개수.
// ===========================================================================
typedef struct {
    int parent[MAXV];
    int rank_[MAXV];    // 'rank' 는 표준헤더 충돌 회피 위해 rank_
    int count;          // 서로소 집합 개수
} dsu_t;

void dsu_init(dsu_t *d, int n) {
    for (int i = 0; i < n; ++i) { d->parent[i] = i; d->rank_[i] = 0; }
    d->count = n;
}

int dsu_find(dsu_t *d, int x) {
    while (d->parent[x] != x) {
        d->parent[x] = d->parent[d->parent[x]];  // 경로 절반 압축
        x = d->parent[x];
    }
    return x;
}

// 두 집합 합치기. 이미 같은 집합이면 false(사이클 신호로 활용 가능).
bool dsu_union(dsu_t *d, int a, int b) {
    int ra = dsu_find(d, a), rb = dsu_find(d, b);
    if (ra == rb) return false;
    if (d->rank_[ra] < d->rank_[rb]) { int t = ra; ra = rb; rb = t; }
    d->parent[rb] = ra;                 // 랭크 큰 ra 아래로 rb 붙임
    if (d->rank_[ra] == d->rank_[rb]) d->rank_[ra]++;
    d->count--;
    return true;
}

bool dsu_connected(dsu_t *d, int a, int b) {
    return dsu_find(d, a) == dsu_find(d, b);
}

// ===========================================================================
// Q8. Dijkstra (음이 아닌 가중치) — 배열 기반 O(V^2)
// ---------------------------------------------------------------------------
// dist[src]=0, 나머지 INF. 매 스텝 미확정 중 dist 최소인 정점 u 를 선택(확정),
// u 의 간선으로 이웃 완화(relax). 힙 없이 선형 탐색이라 O(V^2) — 조밀/소형 그래프
// 및 결정론적 임베디드에서 오히려 단순/안전. 도달 불가 정점은 INF 로 남는다.
// ===========================================================================
void dijkstra(const graph_t *g, int src, int *dist) {
    bool done[MAXV] = {false};
    for (int i = 0; i < g->n; ++i) dist[i] = INF;
    dist[src] = 0;

    for (int it = 0; it < g->n; ++it) {
        // 미확정 중 최소 dist 정점 선택
        int u = -1, best = INF;
        for (int v = 0; v < g->n; ++v)
            if (!done[v] && dist[v] < best) { best = dist[v]; u = v; }
        if (u == -1) break;             // 남은 게 모두 도달 불가
        done[u] = true;
        for (edge_t *e = g->adj[u]; e; e = e->next) {
            // dist[u] != INF 보장됨(선택되었으므로). 오버플로 회피 위해 분리 계산.
            if (!done[e->to] && dist[u] + e->w < dist[e->to])
                dist[e->to] = dist[u] + e->w;
        }
    }
}

// ===========================================================================
// Q9. 방향 그래프 사이클 검출 (3색 DFS)
// ---------------------------------------------------------------------------
// WHITE(0)=미방문, GRAY(1)=현재 재귀 스택에 있음, BLACK(2)=완료.
// DFS 중 GRAY 정점으로 가는 간선(back edge)을 만나면 사이클.
// 모든 정점을 시작점으로 시도(비연결/여러 컴포넌트 대비).  O(V+E).
// ===========================================================================
enum { WHITE = 0, GRAY = 1, BLACK = 2 };

static bool dfs_cycle(const graph_t *g, int u, int *color) {
    color[u] = GRAY;
    for (edge_t *e = g->adj[u]; e; e = e->next) {
        if (color[e->to] == GRAY) return true;          // back edge → 사이클
        if (color[e->to] == WHITE && dfs_cycle(g, e->to, color)) return true;
    }
    color[u] = BLACK;
    return false;
}

bool detect_cycle_directed(const graph_t *g) {
    int color[MAXV] = {WHITE};
    for (int s = 0; s < g->n; ++s)
        if (color[s] == WHITE && dfs_cycle(g, s, color)) return true;
    return false;
}

// ===========================================================================
// 테스트 헬퍼
// ===========================================================================
static bool arr_eq(const int *a, const int *b, int n) {
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

// out 이 g 의 유효한 위상 순서인지 검증(모든 간선 u->v 에서 pos[u] < pos[v]).
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

    // -------- Q1/Q2/Q3/Q4: 무향 그래프 --------
    // 정점 6개. 간선(무향): 0-1, 0-2, 1-3, 2-3, 4-5.
    // 인접(append 순): 0:[1,2] 1:[0,3] 2:[0,3] 3:[1,2] 4:[5] 5:[4]
    printf("== Q1~Q4 Undirected: adj / BFS / DFS / components ==\n");
    graph_t ug;
    graph_init(&ug, 6, false);
    graph_add_edge(&ug, 0, 1, 1);
    graph_add_edge(&ug, 0, 2, 1);
    graph_add_edge(&ug, 1, 3, 1);
    graph_add_edge(&ug, 2, 3, 1);
    graph_add_edge(&ug, 4, 5, 1);

    // Q1: 인접 리스트 구조 확인 (0 의 이웃 = 1,2 순서)
    T("adj[0] == 1,2",  ug.adj[0] && ug.adj[0]->to == 1 &&
                        ug.adj[0]->next && ug.adj[0]->next->to == 2 &&
                        ug.adj[0]->next->next == NULL);
    T("adj[3] == 1,2 (무향 역방향 포함)",
                        ug.adj[3] && ug.adj[3]->to == 1 &&
                        ug.adj[3]->next && ug.adj[3]->next->to == 2);

    // Q2: BFS from 0 -> 0,1,2,3
    int nb = bfs(&ug, 0, order);
    T("BFS(0) count == 4", nb == 4);
    T("BFS(0) order == 0,1,2,3", arr_eq(order, (int[]){0,1,2,3}, 4));
    int nb4 = bfs(&ug, 4, order);
    T("BFS(4) order == 4,5", nb4 == 2 && arr_eq(order, (int[]){4,5}, 2));

    // Q3: DFS from 0 -> 0,1,3,2 (재귀 == 반복)
    int nr = dfs_recursive(&ug, 0, order);
    T("DFS-rec(0) order == 0,1,3,2", nr == 4 && arr_eq(order, (int[]){0,1,3,2}, 4));
    int ni = dfs_iterative(&ug, 0, order);
    T("DFS-iter(0) order == 0,1,3,2", ni == 4 && arr_eq(order, (int[]){0,1,3,2}, 4));

    // Q4: 연결 요소 = 2  ({0,1,2,3}, {4,5})
    T("connected_components == 2", connected_components(&ug) == 2);
    graph_free(&ug);

    // 단일 정점 + 자기 루프도 1개 요소
    graph_t sg; graph_init(&sg, 1, false);
    graph_add_edge(&sg, 0, 0, 1);           // self-loop
    T("single self-loop: components == 1", connected_components(&sg) == 1);
    int nbs = bfs(&sg, 0, order);
    T("single self-loop: BFS count == 1", nbs == 1 && order[0] == 0);
    graph_free(&sg);

    // 완전 비연결(간선 0) → 요소 = 정점 수
    graph_t dg; graph_init(&dg, 4, false);
    T("no edges: components == 4", connected_components(&dg) == 4);
    graph_free(&dg);

    // -------- Q5: 섬의 개수 --------
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

    // -------- Q6/Q9: 방향 그래프 (DAG) --------
    printf("== Q6/Q9 Directed DAG: topo sort / cycle ==\n");
    // 6정점 DAG: 5->2, 5->0, 4->0, 4->1, 2->3, 3->1
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

    // 사이클 있는 방향 그래프: 0->1->2->0
    graph_t cyc; graph_init(&cyc, 3, true);
    graph_add_edge(&cyc, 0, 1, 1);
    graph_add_edge(&cyc, 1, 2, 1);
    graph_add_edge(&cyc, 2, 0, 1);
    T("cyclic: detect_cycle == true", detect_cycle_directed(&cyc));
    T("cyclic: topo sort == -1", topological_sort(&cyc, order) == -1);
    graph_free(&cyc);

    // self-loop 도 사이클
    graph_t slf; graph_init(&slf, 2, true);
    graph_add_edge(&slf, 0, 0, 1);
    T("self-loop directed: cycle == true", detect_cycle_directed(&slf));
    graph_free(&slf);

    // -------- Q7: Union-Find --------
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
    dsu_union(&d, 2, 4);                     // {0,1,2,3,4}
    T("after union(2,4): connected(1,3)", dsu_connected(&d, 1, 3));
    T("count == 2", d.count == 2);

    // DSU 로 무향 사이클 검출: 두 끝이 이미 같은 집합이면 사이클
    dsu_t d2; dsu_init(&d2, 3);
    dsu_union(&d2, 0, 1);
    dsu_union(&d2, 1, 2);
    T("DSU cycle detect: union(0,2) => cycle", !dsu_union(&d2, 0, 2));

    // -------- Q8: Dijkstra --------
    printf("== Q8 Dijkstra (non-negative weights) ==\n");
    // 방향 가중 그래프: 0->1(4) 0->2(1) 2->1(2) 1->3(1) 2->3(5), 정점4 고립
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

    // 무향 가중 그래프에서도 동작
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

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
