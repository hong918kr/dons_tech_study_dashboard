# 15. 그래프 (Graphs: 인접 리스트 · BFS/DFS · DSU · Dijkstra) 🕸️

> 기존 문제은행의 **가장 큰 공백**. 트리/리스트를 넘어 "임의의 연결"을 다루는 순간
> 그래프다. Anduril류 로보틱스/방산 펌웨어에서 **상태공간 탐색, 메시 네트워크,
> 경로계획, 태스크 의존성, 실시간 클러스터링**이 전부 그래프 문제로 환원된다.

---

## 왜 Anduril 펌웨어/로보틱스 인터뷰에 나오나

- **BFS/DFS → 상태공간·메시 네트워크 탐색**: 도달 가능한 상태/노드 열거, 스웜(swarm)
  메시에서 특정 기체까지 도달성 판정, 그리드 맵 플러드필(점유격자 영역 라벨링).
- **Dijkstra → 경로계획/라우팅**: 양수 가중치(거리/지연/에너지) 위 최소비용 경로.
  드론 웨이포인트, 라디오 메시 라우팅, 최소 지연 링크 선택.
- **위상 정렬 → 태스크 의존성 순서**: 부팅 시퀀스, 캘리브레이션 단계, 빌드/센서 초기화
  의존 그래프. 순환이 있으면 데드락 → topo 실패로 조기 진단.
- **Union-Find → 실시간 클러스터링/연결 판정**: 센서 융합에서 같은 물체로 묶기,
  무향 그래프 사이클 검출, 네트워크 파티션 감지.
- 인터뷰어는 **인접 리스트를 STL 없이 C 로 직접 짜고**, 큐/스택을 인라인으로 구현하며,
  포인터/경계(비연결·빈 그래프·자기 루프)를 안전하게 다루는지로 실력을 가늠한다.

---

## 그래프 표현: 인접 리스트 vs 행렬

| 표현 | 공간 | 간선 존재 확인 | 이웃 순회 | 언제 |
|------|------|----------------|-----------|------|
| 인접 리스트 | O(V+E) | O(deg) | O(deg) | **희소 그래프(대부분)**, 임베디드 기본 |
| 인접 행렬 | O(V²) | O(1) | O(V) | 조밀 그래프, V 작고 간선 잦은 조회 |

이 은행은 **인접 리스트**를 표준으로 쓴다. 각 정점이 `(to, w)` 간선 노드의 단일 연결
리스트를 가진다. **꼬리에 append** 하여 삽입 순서를 보존 → BFS/DFS 방문 순서가 결정론적.

```c
typedef struct edge { int to; int w; struct edge *next; } edge_t;
typedef struct { int n; bool directed; edge_t *adj[MAXV]; } graph_t;

void graph_add_edge(graph_t *g, int u, int v, int w) {
    add_directed_edge(g, u, v, w);              // u -> v 꼬리 append
    if (!g->directed) add_directed_edge(g, v, u, w);  // 무향이면 반대도
}
```

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 | 복잡도 |
|------|-----------|--------|
| BFS (배열 큐) | 넣을 때 visited 표시, FIFO. 최단 홉 수. | O(V+E) |
| DFS 재귀 | visited 후 이웃 재귀. 전위 순서. | O(V+E) |
| DFS 반복 (배열 스택) | 이웃 **역순** push → 재귀와 동일 순서. | O(V+E) |
| 연결 요소 | 미방문마다 BFS/DFS 1회 → 카운트. | O(V+E) |
| num_islands | 그리드 플러드필, 방문한 1 을 0 으로. | O(R·C) |
| 위상 정렬(Kahn) | indegree 0 부터 벗겨내기. cnt<n → 사이클. | O(V+E) |
| Union-Find | find 경로압축 + union 랭크 ≈ 상수시간. | ~O(α) |
| Dijkstra (배열) | 미확정 최소 dist 선택 + relax. | O(V²) |
| 방향 사이클(3색) | GRAY 재방문 = back edge = 사이클. | O(V+E) |

---

## BFS: 배열 큐로 최단 홉

```c
int bfs(const graph_t *g, int src, int *out) {
    bool visited[MAXV] = {false};
    int q[MAXV], qh = 0, qt = 0, cnt = 0;
    visited[src] = true; q[qt++] = src;      // 넣을 때 표시(중복 방지)
    while (qh < qt) {
        int u = q[qh++];                     // dequeue
        out[cnt++] = u;
        for (edge_t *e = g->adj[u]; e; e = e->next)
            if (!visited[e->to]) { visited[e->to] = true; q[qt++] = e->to; }
    }
    return cnt;
}
```

- **visited 는 반드시 enqueue 시점에** 표시한다. dequeue 시점에 표시하면 같은 정점이
  큐에 여러 번 들어가 O(V²)/무한 위험.
- 각 정점이 큐에 한 번만 → 배열 큐 크기 `MAXV` 로 충분(동적할당 불필요, 임베디드 친화).
- 비가중 그래프의 **최단 경로(홉 수)** = BFS 레벨.

## DFS: 재귀와 반복이 같은 순서를 내려면

```c
// 반복: 이웃을 임시배열에 모아 역순 push → pop 시 원래 순서로 방문 → 재귀와 일치
int nbr[MAXV], m = 0;
for (edge_t *e = g->adj[u]; e; e = e->next) nbr[m++] = e->to;
for (int i = m - 1; i >= 0; --i)
    if (!visited[nbr[i]]) { visited[nbr[i]] = true; st[sp++] = nbr[i]; }
```

- 재귀 DFS 는 콜스택을 쓴다 → **깊은 그래프에서 스택 오버플로** 위험(임베디드에서 특히).
  명시적 스택 반복 버전이 안전.
- 이웃 **역순 push** 가 포인트: 그냥 순서대로 넣으면 마지막 이웃부터 방문된다.

---

## 위상 정렬 (Kahn's BFS)

```c
int indeg[MAXV] = {0};
for (u) for (e in adj[u]) indeg[e->to]++;     // 진입차수 계산
for (v) if (indeg[v]==0) q[qt++] = v;         // 시작점 = 진입차수 0
while (queue) {
    u = dequeue; out[cnt++] = u;
    for (e in adj[u]) if (--indeg[e->to] == 0) enqueue(e->to);
}
return (cnt == n) ? cnt : -1;                 // cnt < n → 사이클
```

- **cnt < n 이면 사이클** → 위상 순서 자체가 존재하지 않음. 부팅/의존성 그래프의
  순환 의존을 이걸로 조기 검출.
- 결과 순서는 큐에서 꺼내는 순서에 따라 여러 개일 수 있다(유효하기만 하면 됨).

## Union-Find (DSU): 경로 압축 + 랭크

```c
int dsu_find(dsu_t *d, int x) {
    while (d->parent[x] != x) {
        d->parent[x] = d->parent[d->parent[x]];  // 경로 절반 압축
        x = d->parent[x];
    }
    return x;
}
bool dsu_union(dsu_t *d, int a, int b) {
    int ra = dsu_find(d,a), rb = dsu_find(d,b);
    if (ra == rb) return false;                   // 이미 같은 집합(사이클 신호)
    if (d->rank_[ra] < d->rank_[rb]) swap(ra, rb);
    d->parent[rb] = ra;
    if (d->rank_[ra] == d->rank_[rb]) d->rank_[ra]++;
    d->count--; return true;
}
```

- 두 최적화(경로 압축 + 랭크)로 amortized **거의 상수시간**(역아커만 α).
- `union` 이 `false` 를 반환 = 두 정점이 이미 연결 = **무향 그래프 사이클**. 크루스칼
  MST, 실시간 클러스터 병합에 직접 쓰인다.

## Dijkstra (배열 기반 O(V²))

```c
for (i) dist[i] = INF;  dist[src] = 0;
for (V번) {
    u = 미확정 중 dist 최소 정점;   // 선형 탐색 O(V)
    if (u == -1) break;             // 남은 게 모두 도달 불가(INF)
    done[u] = true;
    for (e in adj[u])
        if (!done[e->to] && dist[u] + e->w < dist[e->to])
            dist[e->to] = dist[u] + e->w;   // relax
}
```

- **음수 가중치 금지** — 확정한 정점을 다시 안 보므로 음수 간선이 있으면 틀린다
  (그 경우 Bellman-Ford).
- 배열 버전 O(V²) 는 **힙 없이** 구현 → 소형/조밀 그래프와 결정론적 임베디드에 적합.
  큰 희소 그래프는 이진 힙으로 O(E log V).
- 도달 불가 정점은 `INF` 로 남는다.

## 방향 그래프 사이클 검출 (3색 DFS)

- `WHITE`(미방문) → `GRAY`(현재 재귀 스택) → `BLACK`(완료).
- DFS 중 **GRAY 정점으로 가는 간선(back edge)** 을 만나면 사이클. self-loop 도 사이클.
- 무향 그래프 사이클은 3색이 아니라 **DSU** 또는 "부모 아닌 방문 정점" 규칙으로.

---

## 함정 & 임베디드 주의점

- **BFS visited 타이밍**: enqueue 시점에 표시하지 않으면 중복 삽입 → 성능/정확성 붕괴.
- **DFS 재귀 깊이**: 깊은 그래프에서 콜스택 오버플로. 명시적 스택 반복이 안전.
- **DFS 반복 순서**: 이웃을 역순 push 하지 않으면 재귀와 순서가 달라진다.
- **Dijkstra 음수 간선**: 틀린 답. 반드시 비음수 가정 확인.
- **오버플로**: `dist[u] + w` 에서 `dist[u]==INF` 면 오버플로. 선택된 정점만 relax 하도록
  가드(선택 시 이미 유한 보장) 또는 `INF` 체크.
- **비연결/빈 그래프**: 연결 요소는 정점 수만큼, 빈 그래프도 크래시 없이. 시작점 하나만으로
  전체를 다 도는 코드는 비연결 그래프에서 일부만 방문한다(모든 정점 루프 필요).
- **자기 루프(self-loop)**: 무향 요소 계산엔 영향 없지만 방향 사이클 검출엔 즉시 true.
- **정적 상한(MAXV)**: 임베디드에선 동적할당 대신 컴파일 타임 상한 배열이 흔하다. 큐/스택도
  정점 수 상한으로 정적 확보 → 힙 0, 결정론적.

---

## 복잡도 요약

| 알고리즘 | 시간 | 공간 |
|----------|------|------|
| build adjacency (E 간선) | O(E·평균deg) append / O(E) 앞삽입 | O(V+E) |
| BFS / DFS | O(V+E) | O(V) |
| connected components | O(V+E) | O(V) |
| num_islands (R×C) | O(R·C) | O(R·C) |
| topological sort (Kahn) | O(V+E) | O(V) |
| union / find | ~O(α(N)) amortized | O(V) |
| Dijkstra (배열) | O(V²) | O(V) |
| Dijkstra (이진 힙) | O(E log V) | O(V) |
| 방향 사이클 (3색 DFS) | O(V+E) | O(V) |
