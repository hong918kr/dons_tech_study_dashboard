---
id: ALGO-M14
time: 80분
updated: 2026-11-11
---
# ALGO-M14 Union-Find: 연결성과 그룹 병합

## 학습 목표

- 정적 배열 두 개(`parent`, `size`)로 **DSU(Disjoint Set Union, 서로소 집합)** 를 짜고, `find` 는 **경로 반감(path halving)** 반복문, `union` 은 **크기 기준 합치기(union by size)** 로 쓴다. 노드당 4 B, 재귀 0, 단일 `find` 최악 ⌊log₂ n⌋ 단계라는 것을 증명과 측정으로 말한다.
- "union by size + 경로 압축 = op 당 O(α(n))" 을 **정확히** 말한다: 상각(amortized) 한계이고, 한 번의 `find` 는 여전히 Θ(log n) 일 수 있으며, 둘 중 하나만 쓰면 무엇이 깨지는지 표로 보여 준다.
- 무방향 간선 목록에서 **사이클을 처음 닫는 간선**(자기 루프·평행 간선 포함)을 찾고, 왜 "두 끝점이 이미 같은 집합 ⇔ 사이클" 인지 증명한다.
- **Kruskal 최소 신장 포레스트(MSF)** 를 교환 논증으로 증명하고, Prim O(V²)·부분집합 전수 브루트포스와 총 가중치를 대조한다. 끊긴 그래프는 트리가 아니라 포레스트가 나온다.
- 센서 메시 네트워크에서 링크가 하나씩 올라올 때 연결 요소 수·최대 클러스터·게이트웨이 도달 노드 수를 O(α) 로 갱신하고, DSU 가 **링크 끊김(삭제)** 을 못 한다는 한계와 그 우회(오프라인 역순 처리)를 안다.

## 1. 문제

"이 두 노드가 지금 연결돼 있나?" 가 수천 번 들어오는데 간선은 계속 추가된다 — 매번 BFS(O(n+m))를 돌리면 느리고, DSU 로 바꾸면 10줄짜리 코드가 거의 상수 시간에 답한다. 그래서 그래프 면접 문제(섬 개수, 중복 연결, 친구 그룹, MST)의 절반이 결국 "DSU 를 쓸 줄 아나" 로 갈리고, 펌웨어 면접에서는 "정적 메모리로, 재귀 없이, 최악 시간이 얼마냐" 가 꼬리질문으로 붙는다.

표현은 **포레스트(forest)** 다. 집합 하나 = 트리 하나, 트리의 루트 = 그 집합의 대표(representative). `parent[x] == x` 면 x 가 루트. 이 레슨의 불변식:

- (I1) 각 집합은 정확히 하나의 루트를 가진 트리이고, 같은 트리의 노드끼리만 같은 집합이다.
- (I2) 루트 r 에서 `size[r]` = 그 트리의 노드 수. (루트가 아닌 노드의 `size` 는 의미 없음 — 읽지 않는다.)
- (I3) `comps` = 루트의 개수 = 집합(연결 요소) 수.
- (I4) 모든 노드 x 에 대해 depth(x) ≤ log₂ |tree(x)| ≤ log₂ n (Part A 에서 증명).

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">n = 6 손 예제: union(5,3) 한 번과 find(5) 한 번 — 화살표는 parent 포인터</text>
  <text x="20" y="45" fill="currentColor" font-size="12">① union(5,3) 직전 (크기 4 와 2)</text>
  <text x="270" y="45" fill="currentColor" font-size="12">② union(5,3) 직후 (작은 쪽 4 를 0 밑에)</text>
  <text x="520" y="45" fill="currentColor" font-size="12">③ find(5) 직후 (반감: 5 → 할아버지 0)</text>
  <g stroke="currentColor">
    <line x1="40" y1="140" x2="70" y2="80"/><line x1="100" y1="140" x2="70" y2="80"/><line x1="100" y1="200" x2="100" y2="140"/>
    <line x1="190" y1="140" x2="190" y2="80"/>
    <line x1="290" y1="130" x2="380" y2="70"/><line x1="340" y1="130" x2="380" y2="70"/><line x1="400" y1="130" x2="380" y2="70"/>
    <line x1="455" y1="130" x2="380" y2="70" stroke-width="2.5"/><line x1="455" y1="190" x2="455" y2="130"/>
    <line x1="535" y1="130" x2="625" y2="70"/><line x1="580" y1="130" x2="625" y2="70"/><line x1="625" y1="130" x2="625" y2="70"/>
    <line x1="670" y1="130" x2="625" y2="70"/><line x1="715" y1="130" x2="625" y2="70" stroke-width="2.5"/>
    <circle cx="70" cy="80" r="14" class="f-acc"/><circle cx="40" cy="140" r="14" class="f-mut"/><circle cx="100" cy="140" r="14" class="f-mut"/><circle cx="100" cy="200" r="14" class="f-mut"/>
    <circle cx="190" cy="80" r="14" class="f-acc"/><circle cx="190" cy="140" r="14" class="f-warn"/>
    <circle cx="380" cy="70" r="14" class="f-acc"/><circle cx="290" cy="130" r="14" class="f-mut"/><circle cx="340" cy="130" r="14" class="f-mut"/>
    <circle cx="400" cy="130" r="14" class="f-ok"/><circle cx="455" cy="130" r="14" class="f-mut"/><circle cx="455" cy="190" r="14" class="f-warn"/>
    <circle cx="625" cy="70" r="14" class="f-acc"/><circle cx="535" cy="130" r="14" class="f-mut"/><circle cx="580" cy="130" r="14" class="f-mut"/>
    <circle cx="625" cy="130" r="14" class="f-mut"/><circle cx="670" cy="130" r="14" class="f-mut"/><circle cx="715" cy="130" r="14" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="70" y="84">0</text><text x="40" y="144">1</text><text x="100" y="144">2</text><text x="100" y="204">3</text>
    <text x="190" y="84">4</text><text x="190" y="144">5</text>
    <text x="380" y="74">0</text><text x="290" y="134">1</text><text x="340" y="134">2</text><text x="400" y="134">3</text><text x="455" y="134">4</text><text x="455" y="194">5</text>
    <text x="625" y="74">0</text><text x="535" y="134">1</text><text x="580" y="134">2</text><text x="625" y="134">3</text><text x="670" y="134">4</text><text x="715" y="134">5</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="250">parent=[0,0,0,2,4,4]</text><text x="20" y="268">size[0]=4 size[4]=2</text>
    <text x="270" y="250">parent=[0,0,0,0,0,4]</text><text x="270" y="268">size[0]=6  comps=1</text>
    <text x="520" y="250">parent=[0,0,0,0,0,0]</text><text x="520" y="268">5 의 깊이 2 → 1</text>
  </g>
  <text x="20" y="296" fill="currentColor" font-size="12">② 에서 3 이 0 을 직접 가리키는 것(녹색)은 union 안의 find(3) 이 반감으로 3 을 할아버지 0 으로 당긴 결과다.</text>
  <text x="20" y="314" fill="currentColor" font-size="12">강조 = 루트. 포인터를 조상으로만 바꾸므로 어느 노드도 다른 트리로 옮겨 가지 않는다 → 집합은 그대로.</text>
</svg>
```

### Part A — DSU 핵심 (`dsu_find`, `dsu_union`)

`dsu_t` 는 `uint16_t parent[DSU_MAX_N]`, `uint16_t size[DSU_MAX_N]`, `n`, `comps` (노드당 4 B, 1024 노드 = 4 KB). `dsu_init` 은 주어진다.

- `int dsu_find(dsu_t *d, int x)` — x 의 루트. 반복문, 지나가는 노드를 할아버지로 당긴다 (`parent[x] = parent[parent[x]]`).
- `bool dsu_union(dsu_t *d, int a, int b)` — 이미 같은 집합이면 false. 아니면 **작은 트리의 루트를 큰 트리의 루트 밑에** (같으면 b 쪽을 a 쪽 밑에 — 결정적), `size` 더하고 `comps--`, true.

검증: n = 4 에서 union 3번의 모든 수열(16³ = 4096개) 매 단계 + 무작위 300회(n ≤ 64, union ≤ 3n)를 **DSU 를 안 쓰는 BFS 참조**와 비교 — 반환값, `comps`, 모든 쌍의 `dsu_same`, 모든 노드의 집합 크기, 그리고 깊이 ≤ ⌊log₂ n⌋. n = 1024 에서 "2배 짝짓기" 순서로 합치면 깊이가 **정확히** 10 이 되는지(상한이 빡빡함)도 본다.

### Part B — 사이클 검출과 Kruskal 최소 신장 포레스트

- `int first_cycle_edge(dsu_t *d, int n, const edge_t *e, int m)` — 간선을 순서대로 넣을 때 처음 사이클을 닫는 간선의 인덱스, 숲이면 -1. 자기 루프(`u == v`)와 평행 간선도 사이클이다 (LeetCode 684 Redundant Connection 의 일반형).
- `int kruskal_msf(dsu_t *d, int n, const edge_t *e, int m, int *order, int *chosen, int64_t *total)` — `order` 는 호출자가 준 m 칸 작업 버퍼(malloc 없음), 정렬 도우미 `sort_edges_by_weight` (힙 정렬, (w, 인덱스) 키) 는 주어진다. 반환 = 고른 간선 수 = n − (연결 요소 수). 끊긴 그래프면 요소마다 트리 하나 = 포레스트.

검증: n = 4 에서 길이 0..4 의 모든 간선 수열 11111개 + 무작위 300개를 "앞 간선들만으로 BFS 했을 때 u, v 가 이미 연결?" 이라는 **정의 그대로의** 참조와 비교. Kruskal 은 CLRS 예제(총 37) + 무작위 400개를 Prim O(V²) 와(총 가중치, 간선 수 = n − c, 고른 간선이 사이클 없음) + 작은 그래프 300개를 **간선 부분집합 전수**와 비교. 음수 가중치·동점 많음·자기 루프·끊긴 그래프 포함.

### Part C — 센서 메시 네트워크

노드 0 이 게이트웨이(싱크)인 무선 센서 메시. 부팅 후 이웃 탐색으로 링크가 하나씩 "up" 된다.

1. `bool mesh_link_up(mesh_t *m, int a, int b)` — 링크 추가. 두 클러스터가 새로 합쳐졌으면 true 이고 `largest`(최대 클러스터 크기) 갱신. 그 뒤 질의 `mesh_components`, `mesh_largest`, `mesh_gateway_reach`(게이트웨이와 같은 클러스터의 노드 수), `mesh_can_reach(a, b)` 가 전부 O(α).
2. 후보 링크 14개의 비용(ETX × 10, ETX = 한 패킷을 성공시키는 데 드는 기대 전송 횟수)으로 **총 비용 최소 백본**을 Kruskal 로 뽑아 출력. 노드 7 의 링크가 모두 죽은 경우 포레스트(트리 2개)와 "7 은 게이트웨이에 못 감" 을 보고.

검증: 손으로 쓴 8-이벤트 시나리오 표(기대값을 코드에 적어 둠) + 무작위 링크 스트림 100개 × 150 이벤트를 매 이벤트 BFS 와 비교.

## 2. Problem (English)

> **Union-Find for a sensor mesh.** C11, no recursion, no dynamic allocation in the algorithm (static arrays sized by `DSU_MAX_N`, caller-provided buffers).
>
> 1. Implement `int dsu_find(dsu_t *d, int x)` with path halving and `bool dsu_union(dsu_t *d, int a, int b)` with union by size. `d->comps` must always equal the number of disjoint sets. `dsu_union` returns `false` if `a` and `b` are already in the same set.
> 2. `int first_cycle_edge(dsu_t *d, int n, const edge_t *e, int m)`: edges are undirected and added in order; return the index of the first edge that closes a cycle (self-loops and parallel edges count), or `-1` if the whole list is a forest.
> 3. `int kruskal_msf(...)`: return the edges of a minimum spanning **forest** (one tree per connected component), their count (`n - components`) and total weight in `int64_t`. Weights may be negative; the graph may be disconnected and contain self-loops / parallel edges.
> 4. Mesh: nodes `0..n-1`, gateway `g`. Process `link_up(a, b)` events and answer after each one: number of clusters, largest cluster size, number of nodes in the gateway's cluster, `can_reach(a, b)`.
>
> Example: `n = 6`, `union(0,1), union(2,3), union(1,3), union(4,5), union(5,3)` → `comps` = 5, 4, 3, 2, 1. Edges `(0,1),(1,2),(2,0),(2,3)` → `first_cycle_edge = 2`. CLRS Fig. 23.1 graph → MST weight 37 with 8 edges.
>
> Constraints: `n <= DSU_MAX_N <= 65535`, `m` up to the caller's buffer. State the time per operation and the worst case of a single `find`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 5개 함수 본문(`dsu_find`, `dsu_union`, `first_cycle_edge`, `kruskal_msf`, `mesh_link_up`). 스텁은 "자기 자신이 루트", "아무것도 안 합침", "-1", "간선 0개" 를 돌려준다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:232  dsu_union(d, 1, 2) && dsu_union(d, 3, 4) && dsu_union(d, 2, 4)
…
Part A random: 300 trials, 14315 unions, bad ret=7310 comps=14287 pair=272640 size=7958 depth>log2n=0
…
Part A n=1024: pairwise-doubling unions -> max depth 0 (floor(log2 n) = 10)
…
Part B cycle exhaustive n=4: 11111 edge sequences of length 0..4 (133 forests), mismatches=10978
…
Part B CLRS 23.1: kruskal edges=0 total=0, prim total=37
…
Part C backbone (8 nodes, 14 candidate links):  total=0 trees=8
FAIL starter.c:471  k == 7 && tot == prim_msf_total(8, links, L) && g_d.comps == 1
…
7/31 passed
```

(exit 1. 통과한 7개는 빈 그래프·n = 0/1·"숲이면 -1" 처럼 스텁 값이 우연히 맞는 경계 사례와, 아무것도 안 합쳐서 깊이 0 이 된 깊이 상한 검사다. 참조 쪽 Prim 은 처음부터 37 을 낸다.)

## 4. 힌트

<details><summary>힌트 1 — find 는 "루트까지 올라가기" 에 한 줄만 더한다</summary>

`while (parent[x] != x) x = parent[x];` 가 기본형이다. 올라가기 **전에** `parent[x] = parent[parent[x]]` 를 하면 x 가 할아버지를 가리키게 되고, 그다음 `x = parent[x]` 로 두 칸을 한 번에 오른다. 루트 바로 아래라면 `parent[parent[x]]` 가 루트 자신이라 아무것도 안 바뀐다.

</details>

<details><summary>힌트 2 — union 은 루트끼리만 잇는다</summary>

`ra = find(a)`, `rb = find(b)` 를 먼저. `parent[a] = b` 처럼 루트가 아닌 노드를 옮기면 a 의 원래 트리에서 a 의 서브트리만 떨어져 나간다 (7. 실수 1). 크기는 루트에서만 읽고 쓴다.

</details>

<details><summary>힌트 3 — 사이클 = union 이 실패하는 순간</summary>

지금까지의 간선이 숲이라면, 새 간선 (u, v) 가 사이클을 만드는 것은 u 와 v 가 이미 같은 트리에 있을 때뿐이다. 그러니 `dsu_union` 의 반환값이 그대로 답이다. 자기 루프는 `find(u) == find(u)` 라 저절로 잡힌다.

</details>

<details><summary>힌트 4 — Kruskal 은 "정렬 + 힌트 3"</summary>

가벼운 간선부터 보면서 union 이 성공한(= 사이클을 안 만드는) 간선만 고른다. n−1 개를 고르면 멈춘다. 그래프가 끊겨 있으면 n−1 개에 영영 못 가니, 루프 조건은 `k < m && cnt < n - 1` 둘 다.

</details>

<details><summary>힌트 5 — 최대 클러스터는 줄지 않는다</summary>

링크 추가만 있으면 집합은 합쳐지기만 하므로 최댓값은 단조 증가다. 새로 합쳐진 집합의 크기 하나만 `largest` 와 비교하면 된다. 게이트웨이 도달 수는 `size[find(gateway)]`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 노드 수 상한(→ `uint16_t` 로 충분?), 간선 추가만인가 삭제도 있나, 자기 루프·평행 간선 가능?, 질의와 갱신이 섞여 오나, 그래프가 끊겨 있을 수 있나(MST → 포레스트), 가중치 범위·음수(→ 합은 `int64_t`) |
| 2. 무식한 방법 | 질의마다 BFS: O(n + m) / 질의. 이벤트 q 개면 O(q(n + m)). 이걸 테스트 참조로 남긴다 |
| 3. 더 나은 생각 + 불변식 | 집합 = 루트 있는 트리. find 는 루트로, union 은 루트끼리 작은 쪽을 큰 쪽 밑에. 불변식 I1~I4 를 먼저 말한다 |
| 4. 코드 | find 4줄(반감), union 7줄, Kruskal = 정렬 + union 성공한 간선 채택 |
| 5. 소리 내어 말할 테스트 | n = 0/1, 자기 루프, 평행 간선, 이미 같은 집합 union, 사슬 순서 합치기, 끊긴 그래프 MST, 동점·음수 가중치 |
| 6. 복잡도 | op 당 상각 O(α(n)), 단일 find 최악 ⌊log₂ n⌋ 단계, 메모리 노드당 4 B, 스택 O(1). Kruskal O(m log m) (정렬이 지배) |

### Part A — 불변식과 두 함수

**find 의 정당성.** 반감은 `parent[x]` 를 x 의 **조상**(할아버지)으로 바꾼다. 조상은 같은 트리 안에 있으므로 어느 노드도 다른 트리로 가지 않고, 루트는 그대로 루트다 (루트의 parent 는 안 건드림). 그래서 I1~I3 가 유지되고, 각 노드의 깊이는 줄거나 그대로다 — 늘지 않는다. 종료: 매 반복 x 는 적어도 한 칸 위로 가고 트리는 유한하다.

**union 의 정당성.** 두 루트가 다르면 rb 를 ra 밑에 붙여 트리 하나로 만든다(I1), 새 루트의 크기는 두 크기의 합(I2), 루트가 하나 줄었으니 `comps--`(I3). 같으면 아무것도 안 하고 false — 이 반환값이 Part B 와 C 의 핵심 신호다.

**깊이 상한 (I4) 증명.** 주장: 항상 depth(x) ≤ log₂ |tree(x)|.

1. 초기: 깊이 0, 크기 1 → 0 ≤ 0.
2. find(반감): 깊이는 줄기만 하고 트리 크기는 그대로 → 유지.
3. union(rb 를 ra 밑에, |A| ≥ |B|): A 쪽 노드는 깊이 그대로, 트리는 커짐 → 유지. B 쪽 노드 x 는 깊이가 정확히 1 늘지만 트리 크기는 |A| + |B| ≥ 2|B|: depth'(x) = depth(x) + 1 ≤ log₂|B| + 1 = log₂(2|B|) ≤ log₂(|A| + |B|). 유지.

따라서 어떤 순서로 연산하든 최대 깊이 ≤ ⌊log₂ n⌋ 이고, 압축이 없어도 **단일 find 의 최악이 O(log n)** 이다. 이 상한은 빡빡하다: 크기가 같은 트리끼리만 계속 합치면(이항 트리, binomial tree) 정확히 log₂ n 에 닿는다. 테스트 실제 출력: `Part A n=1024: pairwise-doubling unions -> max depth 10 (floor(log2 n) = 10)`.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">union by size 의 깊이 상한: 깊이 +1 이 될 때마다 내 트리 크기는 2배 이상</text>
  <g stroke="currentColor">
    <line x1="60" y1="130" x2="150" y2="70"/><line x1="150" y1="130" x2="150" y2="70"/><line x1="260" y1="130" x2="150" y2="70"/>
    <line x1="150" y1="190" x2="150" y2="130"/><line x1="220" y1="190" x2="260" y2="130"/><line x1="300" y1="190" x2="260" y2="130"/>
    <line x1="300" y1="250" x2="300" y2="190"/>
    <circle cx="150" cy="70" r="14" class="f-acc"/><circle cx="60" cy="130" r="14" class="f-mut"/><circle cx="150" cy="130" r="14" class="f-mut"/>
    <circle cx="260" cy="130" r="14" class="f-mut"/><circle cx="150" cy="190" r="14" class="f-mut"/><circle cx="220" cy="190" r="14" class="f-mut"/>
    <circle cx="300" cy="190" r="14" class="f-mut"/><circle cx="300" cy="250" r="14" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="150" y="74">0</text><text x="60" y="134">1</text><text x="150" y="134">2</text><text x="260" y="134">4</text>
    <text x="150" y="194">3</text><text x="220" y="194">5</text><text x="300" y="194">6</text><text x="300" y="254">7</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="290">n = 8, s = 1, 2, 4 순서로 union(i, i+s): 노드 7 의 깊이 3 = log₂ 8</text>
    <text x="370" y="60">노드 7 의 역사 (깊이, 7 이 속한 트리 크기):</text>
    <text x="385" y="82" font-family="monospace">시작        (0, 1)</text>
    <text x="385" y="102" font-family="monospace">u(6,7)  →  (1, 2)   7 을 6 밑에</text>
    <text x="385" y="122" font-family="monospace">u(4,6)  →  (2, 4)   {6,7} 을 4 밑에</text>
    <text x="385" y="142" font-family="monospace">u(0,4)  →  (3, 8)   {4..7} 을 0 밑에</text>
    <text x="370" y="176">작은(또는 같은) 쪽만 한 칸 내려가므로</text>
    <text x="370" y="196">깊이 d 인 노드의 트리 크기 ≥ 2^d  ⇒  d ≤ log₂ n.</text>
    <text x="370" y="226">실측 (Part A, demo_depth): n = 1024 → 10,</text>
    <text x="370" y="246">n = 2^16 → 16, n = 2^20 → 20. 상한에 정확히 닿는다.</text>
  </g>
</svg>
```

손 계산 (1. 문제의 그림과 같은 예, solution.c 로 실제 확인한 배열):

| 연산 | find 가 한 일 | 루트·크기 비교 | parent 배열 후 | comps |
|---|---|---|---|---|
| init | — | — | [0,1,2,3,4,5] | 6 |
| union(0,1) | 둘 다 루트 | 1 = 1 → 1 을 0 밑에 | [0,0,2,3,4,5] | 5 |
| union(2,3) | 둘 다 루트 | 1 = 1 → 3 을 2 밑에 | [0,0,2,2,4,5] | 4 |
| union(1,3) | find(1)=0, find(3)=2 | 2 = 2 → 2 를 0 밑에 | [0,0,0,2,4,5] | 3 |
| union(4,5) | 둘 다 루트 | 1 = 1 → 5 를 4 밑에 | [0,0,0,2,4,4] | 2 |
| union(5,3) | find(5)=4, find(3): 3 → 할아버지 0 (반감) | 2 &lt; 4 → 4 를 0 밑에 | [0,0,0,0,0,4] | 1 |
| find(5) | 5 → 할아버지 0 (반감) | — | [0,0,0,0,0,0] | 1 |

**α(n) 를 정확히 말하기.** Tarjan(1975): union by rank(또는 size) + 경로 압축으로 n 원소에 m 번(m ≥ n) 연산하면 총 Θ(m·α(m, n)) — α 는 아커만 함수의 역함수이고 상상 가능한 모든 n 에서 4 이하다 (CLRS 의 정의로 α(n) ≤ 4 는 n 이 10^80 을 훨씬 넘어도 성립). 세 가지를 붙여 말한다.

1. **상각(amortized)** 한계다. 한 번의 find 는 여전히 Θ(log n) 일 수 있다 (위 이항 트리에서 압축 전 첫 find). 최악 단일 연산 상한을 주는 건 union by size 쪽이다.
2. 경로 반감·경로 분할(splitting)도 같은 O(α) 를 준다 (Tarjan & van Leeuwen 1984). 반감은 한 번 훑고(1-pass) 추가 변수 없이 끝나서 펌웨어에 맞다.
3. "상수" 가 아니다: Fredman & Saks(1989)가 cell-probe 모델에서 op 당 Ω(α) 하한을 보였다 — 이 자료구조는 점근적으로 최적이지만 O(1) 은 아니다.

**최적화 유무 실측 (demo_depth.c).** 6 변형 × 3 작업. hop = find 루프 반복 수 (반감은 한 반복에 두 칸, 완전 압축은 루트를 찾는 1-pass 반복 수), union 안의 find 두 번도 포함해 op 당 평균. depth = 작업 직후(질의 전) 최대 깊이. n = 2^20, union 뒤 무작위 find 2^20 번:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth && ./demo_depth   # 이 맥 약 10초
```

```text
n = 1048576 (2^20), then 1048576 random finds
workload  variant              depth      hops/op     max hops     ns/op
chain     naive                    -            -            -   skipped
chain     halving only       1048575         1.20       369891       5.3
chain     full compr only    1048575         1.00       739781       2.7
chain     size only                1         1.00            1       2.4
chain     size+halving             1         1.00            1       2.4
chain     size+full compr          1         1.00            1       2.7
random    naive                    -            -            -   skipped
random    halving only            17         1.95           12      15.0
random    full compr only         13         2.89           19      18.9
random    size only                8         1.83            8      15.0
random    size+halving             5         0.94            3       9.2
random    size+full compr          4         1.18            6      12.2
doubling  naive                   20         6.00           19      20.2
doubling  halving only            19         1.16            7       4.4
doubling  full compr only         19         1.61           14       5.9
doubling  size only               20         5.00           20      21.0
doubling  size+halving            20         0.70            8       4.3
doubling  size+full compr         20         0.89           15       5.9
```

n = 2^16 에서는 naive 도 돌렸다 (같은 실행의 앞부분):

```text
chain     naive                65535     16410.73        65533    8508.1
random    naive                 6813      2623.36         6813    8005.3
doubling  size only               16         4.00           16      10.2
```

읽는 법:

- **naive (둘 다 없음)** 은 사슬에서 깊이 n−1, op 당 1만 6천 hop, 8.5 µs/op — 나머지보다 1000배 이상 느리다. 무작위 순서로 합쳐도 깊이 6813 이 나왔다. 2^20 에서는 몇 분이 걸려 생략했다.
- **압축만** 쓰면 평균은 좋지만(사슬에서 op 당 1.0~1.2) **단일 find 최대 hop 이 369891 / 739781** — 처음 한 번은 사슬 전체를 탄다. 상각이 좋다는 것과 최악이 좋다는 것은 다른 이야기다.
- **size 만** 쓰면 최대 hop 이 정확히 깊이 = log₂ n (2^20 → 20). 최악 단일 op 가 보장되는 유일한 축이다.
- **size + 반감** (이 레슨의 solution) 은 무작위에서 op 당 0.94 hop, 최대 3, 가장 빠름(9.2 ns/op). 완전 압축(2-pass)은 hop 이 조금 더 적은 경우도 있지만 두 번 훑어서 이 맥에서는 반감보다 느렸다.
- ns 는 이 맥(Apple M6, Apple clang 21, -O2, 워밍업 1 + 3회 중앙값) 값이다. 비율을 본다 — 2^20 원소 int32 배열 둘(8 MB)은 캐시를 넘으므로 무작위 작업의 ns 는 대부분 캐시 미스다.

### Part B — 사이클을 닫는 간선

**보조정리.** F 가 숲이고 e = (u, v) 가 새 간선일 때, F + e 에 사이클이 있다 ⇔ u 와 v 가 F 에서 이미 연결돼 있다.
(⇐) F 에 u–v 경로 P 가 있으면 P + e 가 사이클이다 (u = v 면 e 자체가 자기 루프 사이클). (⇒) F 는 사이클이 없으니 F + e 의 사이클은 e 를 지난다. 그 사이클에서 e 를 빼면 F 안의 u–v 경로가 남는다.

`first_cycle_edge` 는 첫 사이클 전까지 간선들이 숲이라는 사실과 이 보조정리를 그대로 쓴다: k 번째 간선에서 `dsu_union` 이 false = 이미 연결 = 사이클. 평행 간선은 첫 번째 사본이 두 끝을 이미 이어 놓았으니 두 번째에서 잡힌다.

전수 테스트가 고른 숲 개수 133 은 검산할 수 있다: 길이 0 (1개) + 길이 1 (자기 루프가 아닌 6개) + 길이 2 (서로 다른 비루프 간선의 순서쌍 6·5 = 30) + 길이 3 (4 정점 위 3-간선 숲 = 신장 트리 4^(4−2) = 16개(케일리 공식) × 3! 순서 = 96) = 133. 길이 4 는 4 정점에 간선 4개라 항상 사이클.

**같은 사실의 셈 버전:** 어떤 그래프든 m ≥ n − c (c = 연결 요소 수)이고 등호는 숲일 때만이다. 그래서 "간선 수가 n − c 보다 많다 ⇔ 사이클이 있다", 여분 간선 수 = m − (n − c). 연습 1(트리 판정)과 연습 4(케이블 옮기기)가 이 식이다.

### Part B — Kruskal: 교환 논증

알고리즘: 간선을 (w, 입력 순서) 오름차순으로 보면서 union 이 성공하는 간선만 채택.

**정리.** 결과 F 는 최소 신장 포레스트(각 연결 요소의 MST 들의 합집합)다.

**증명 (불변식: "지금까지 고른 F 를 포함하는 MSF T* 가 존재").** 시작 F = ∅ 은 자명. 다음에 채택하는 간선을 e = (u, v), 그 시점 F 에서 u 가 속한 요소를 C 라 하자. T* 가 e 를 포함하면 끝. 아니면 u, v 는 원래 그래프에서 같은 요소에 있으므로 T* 에 u–v 경로가 있고, T* + e 의 사이클은 C 의 경계를 e 말고 다른 간선 e' 로 한 번 더 건넌다 (C 에서 나갔다 들어와야 하므로). e' ∉ F (F 의 간선은 C 안이거나 C 와 무관). e' 가 e 보다 **먼저** 검사됐다면, 그때 e' 의 두 끝은 다른 요소에 있었을 것이다 (요소는 합쳐지기만 하고, 지금도 한 끝은 C 안, 한 끝은 C 밖) → Kruskal 이 채택했을 것 → e' ∈ F 모순. 그러므로 e' 는 e 이후에 검사되고 w(e') ≥ w(e). T' = T* − e' + e 는 여전히 신장 포레스트이고 무게 ≤ T* 이므로 역시 MSF, 그리고 F + e 를 포함한다. ∎

음수 가중치도 이 논증 어디에도 걸리지 않는다 (다익스트라와 다름 — ALGO-M06). 동점이면 MST 가 여러 개일 수 있지만 **총 가중치는 유일**하므로 테스트는 총합을 Prim 과 비교하고, 구조는 "간선 n − c 개 + 그 간선만으로 요소 c 개 (= 사이클 없음)" 로 따로 확인한다.

Part C 백본의 실제 Kruskal 추적 (8 노드, 후보 14개):

| 순서 | 간선(비용) | find 결과 | 판단 | 고른 수 / 요소 수 |
|---|---|---|---|---|
| 1 | 4-5 (10) | 다름 | 채택 | 1 / 7 |
| 2 | 1-2 (11) | 다름 | 채택 | 2 / 6 |
| 3 | 0-1 (12) | 다름 | 채택 | 3 / 5 |
| 4 | 3-4 (13) | 다름 | 채택 | 4 / 4 |
| 5 | 2-3 (14) | 다름 ({0,1,2} vs {3,4,5}) | 채택 | 5 / 3 |
| 6 | 0-2 (15) | 같음 | 버림 (사이클 0-1-2) | 5 / 3 |
| 7 | 5-7 (16) | 다름 | 채택 | 6 / 2 |
| 8 | 6-7 (17) | 다름 | 채택 → n−1 = 7, 종료 | 7 / 1 |

나머지 6개(18~40)는 검사도 안 한다 (조기 종료). 총 93 — 실제 출력과 같다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">메시 백본: 굵은 선 = Kruskal 이 고른 7개 (총 93), 점선 = 버림/미검사</text>
  <g stroke="currentColor" stroke-dasharray="5 4">
    <line x1="70" y1="150" x2="200" y2="230"/><line x1="200" y1="70" x2="340" y2="150"/><line x1="200" y1="230" x2="480" y2="230"/>
    <line x1="340" y1="150" x2="480" y2="70"/><line x1="480" y1="230" x2="620" y2="230"/><line x1="480" y1="70" x2="620" y2="230"/>
    <path d="M70 165 Q 260 320 470 240" fill="none"/>
  </g>
  <g stroke="currentColor" stroke-width="3">
    <line x1="70" y1="150" x2="200" y2="70"/><line x1="200" y1="70" x2="200" y2="230"/><line x1="200" y1="230" x2="340" y2="150"/>
    <line x1="340" y1="150" x2="480" y2="230"/><line x1="480" y1="230" x2="480" y2="70"/><line x1="480" y1="70" x2="620" y2="70"/>
    <line x1="620" y1="70" x2="620" y2="230"/>
  </g>
  <g stroke="currentColor">
    <circle cx="70" cy="150" r="16" class="f-acc"/><circle cx="200" cy="70" r="16" class="f-ok"/><circle cx="200" cy="230" r="16" class="f-ok"/>
    <circle cx="340" cy="150" r="16" class="f-ok"/><circle cx="480" cy="230" r="16" class="f-ok"/><circle cx="480" cy="70" r="16" class="f-ok"/>
    <circle cx="620" cy="230" r="16" class="f-ok"/><circle cx="620" cy="70" r="16" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="70" y="154">GW0</text><text x="200" y="74">1</text><text x="200" y="234">2</text><text x="340" y="154">3</text>
    <text x="480" y="234">4</text><text x="480" y="74">5</text><text x="620" y="234">6</text><text x="620" y="74">7</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="120" y="102">12</text><text x="122" y="208">15</text><text x="208" y="154">11</text><text x="275" y="100">20</text>
    <text x="262" y="200">14</text><text x="330" y="246">30</text><text x="410" y="182">13</text><text x="410" y="102">25</text>
    <text x="488" y="154">10</text><text x="545" y="62">16</text><text x="545" y="248">18</text><text x="560" y="150">22</text>
    <text x="628" y="154">17</text><text x="250" y="296">40 (0-4)</text>
  </g>
  <text x="20" y="322" fill="currentColor" font-size="12">노드 7 의 링크(16, 17)가 죽으면: 같은 알고리즘이 간선 6개·트리 2개(총 78)를 내고 "7 은 게이트웨이에 못 감" 이 된다.</text>
</svg>
```

### Part C — 메시: 링크 업 스트림

`mesh_link_up` 은 `dsu_union` + "합쳐졌으면 새 크기로 `largest` 갱신" 이 전부다. `largest` 를 매번 전체 스캔하지 않아도 되는 근거는 단조성: 집합은 합쳐지기만 하므로 최대 클러스터는 줄지 않고, 바뀔 수 있는 건 방금 합쳐진 집합 하나뿐이다. 손 시나리오 (실제 출력, 기대값 표와 일치):

```text
Part C scenario:  event   merged comps largest gw_reach
                  0-1     1      7     2       2
                  2-3     1      6     2       2
                  4-5     1      5     2       2
                  1-2     1      4     4       4
                  3-0     0      4     4       4
                  6-7     1      3     4       4
                  5-6     1      2     4       4
                  3-4     1      1     8       8
```

`3-0` 은 merged = 0 — 이미 0-1-2-3 이 이어져 있으니 메시의 **여분 경로**(redundant link)다. 라우팅 관점에서는 쓸모 있는 링크(경로 다양성)이지만 연결성은 바꾸지 않는다. 이 신호로 "여분 링크 수 = 이벤트 수 − (n − comps)" 를 바로 셀 수 있다.

**한계: 링크 다운.** DSU 는 집합을 쪼개지 못한다. 실무 선택지: (1) 링크 다운이 드물면 현재 살아 있는 링크 목록으로 다시 만든다 (O(n + m·α), 1024 노드면 수천 op), (2) 장애 순서를 미리 알면 **거꾸로** 처리해 삭제를 추가로 바꾼다 (연습 2, AlgoMonster M10 §7 "Reverse Union Find"), (3) 완전 동적 연결성(dynamic connectivity) 자료구조는 op 당 polylog 상각이지만 MCU 에 올릴 크기가 아니다.

### 복잡도 요약

| 연산 | 시간 | 추가 메모리 | 스택 |
|---|---|---|---|
| `dsu_find` / `dsu_union` | 상각 O(α(n)), 단일 최악 ⌊log₂ n⌋ + 1 반복 | 노드당 4 B (`uint16_t` ×2) | O(1), 재귀 없음 |
| `first_cycle_edge` | O(m·α(n)) | DSU 만 | O(1) |
| `kruskal_msf` | O(m log m) 정렬 + O(m·α(n)) | `order` m × 4 B (호출자) | O(1) (힙 정렬도 반복문) |
| Prim O(V²) (참조) | O(V²) | 인접 행렬 V² | O(1) |
| 메시 이벤트/질의 | 상각 O(α(n)) | DSU + 정수 2개 | O(1) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 31/31 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 31/31, 보고 없음
```

실제 출력 (Part C 시나리오 표는 위에 있으니 생략):

```text
Part A exhaustive n=4: 4096 sequences x 3 unions, mismatches=0
Part A random: 300 trials, 14315 unions, bad ret=0 comps=0 pair=0 size=0 depth>log2n=0
Part A n=1024: pairwise-doubling unions -> max depth 10 (floor(log2 n) = 10)
Part B cycle exhaustive n=4: 11111 edge sequences of length 0..4 (133 forests), mismatches=0
Part B cycle random: 300 graphs (62 forests), mismatches=0
Part B CLRS 23.1: kruskal edges=8 total=37, prim total=37
Part B kruskal vs prim: 400 graphs (241 disconnected), bad total=0 count=0 forest=0 order=0
Part B kruskal vs brute force (all edge subsets): 300 graphs, mismatches=0
Part C random streams: 15000 link events, mismatches=0
Part C backbone (8 nodes, 14 candidate links): 4-5(10) 1-2(11) 0-1(12) 3-4(13) 2-3(14) 5-7(16) 6-7(17)  total=93 trees=1
Part C backbone without node 7 links: edges=6 total=78 trees=2, 7 reaches gateway? no
31/31 passed
```

(전체 0.13초. 무작위 Kruskal 400개 중 241개가 끊긴 그래프라 포레스트 경로가 충분히 시험된다. CLRS 그래프는 3판 그림 23.1 의 간선 목록을 옮긴 것이다 — Prim 참조도 37 을 내므로 목록 자체는 일관되지만, 판에 따라 그림 번호가 다를 수 있어 번호는 확인 권장.)

## 7. 자주 하는 실수

**1. 루트가 아닌 노드를 잇는다.** `parent[a] = b;` — a 가 루트가 아니면 a 와 그 서브트리만 원래 트리에서 떨어져 나가 b 쪽으로 간다. a 의 원래 동료들은 a 와 같은 집합이 아니게 되고 `comps` 도 틀린다. 고침: 항상 `ra = find(a), rb = find(b)` 후 `parent[rb] = ra`.

**2. 이미 같은 집합인데 `comps--`.** 반환 전에 확인하지 않으면 사이클 간선마다 요소 수가 줄어 음수까지 간다. Part C 의 `3-0` 이벤트가 이 버그를 바로 드러낸다 (기대 comps 4 → 버그면 3). 고침: `if (ra == rb) return false;` 를 맨 앞에.

**3. 루트가 아닌 노드의 `size` 를 읽는다.** `size[a]` 는 a 가 루트였던 시절의 값이 남아 있다. union by size 비교도, 게이트웨이 도달 수도 `size[find(x)]` 여야 한다. 위 손 계산에서 노드 2 의 `size[2]` 는 union(1,3) 뒤에도 2 로 남아 있지만 2 는 더 이상 루트가 아니다.

**4. 재귀 find + 크기 무시.** 흔한 한 줄 `return parent[x] == x ? x : (parent[x] = find(parent[x]));` 은 완전 압축이라 상각은 좋지만, union by size 가 없으면 첫 find 의 재귀 깊이가 사슬 길이다. demo_depth 의 "full compr only, chain" 단일 find 최대 hop 739781 = 그만큼의 재귀 프레임 — 프레임 16~32 B 로 쳐도 12~24 MB, Cortex-M 의 1~4 KB 태스크 스택은 수백 단계에서 넘친다 (ALGO-B11). 고침: 반복문(반감 또는 2-pass) + union by size 로 깊이 자체를 log₂ n 으로.

```c
/* 2-pass 완전 압축, 반복문 (연습 5) */
int r = x;
while (parent[r] != r) r = parent[r];
while (parent[x] != r) { int nx = parent[x]; parent[x] = r; x = nx; }
```

**5. Kruskal 이 연결 그래프를 가정한다.** `while (cnt < n - 1)` 만 쓰면 끊긴 그래프에서 간선 배열 끝을 넘어 읽는다. 고침: `k < m && cnt < n - 1`, 그리고 반환값(고른 수)으로 연결 여부를 호출자가 판단 (`cnt == n - 1` ⇔ 연결).

**6. 정렬 비교 함수 `return a->w - b->w;`.** 가중치가 int32 전 범위면 뺄셈이 넘친다 (부호 있는 오버플로 = UB). `(a > b) - (a < b)` 나 이 레슨처럼 직접 비교. 총합도 `int32_t` 면 간선 2000개 × 10^6 에서 넘친다 → `int64_t`.

**7. `uint16_t` parent 에 n > 65535.** 조용히 잘려서 다른 노드를 가리킨다. 이 레슨은 `_Static_assert(DSU_MAX_N <= 65535, ...)` 로 컴파일 시점에 막는다. `size` 도 같은 폭이라 n = 65536 이면 한 집합의 크기가 0 으로 감싸진다 — 그래서 상한이 65536 이 아니라 65535.

## 8. 면접 Q&A

<details><summary>Q1. "Union-Find 는 거의 O(1)" — 정확히 무슨 뜻이죠?</summary>

union by rank/size + 경로 압축(또는 반감·분할)으로 m 번 연산 총 O(m·α(m, n)) — **상각**이다 (Tarjan 1975, 반감·분할은 Tarjan & van Leeuwen 1984). α 는 역아커만 함수로 현실의 모든 n 에서 4 이하. 하지만 (1) 한 번의 find 는 Θ(log n) 까지 갈 수 있고 (이항 트리의 첫 find, demo: size+halving 의 doubling max hops 8, size only 는 20), (2) 상수가 아니라 Ω(α) 하한이 있다 (Fredman & Saks 1989). 실측 감각: n = 2^20 무작위 작업에서 op 당 평균 0.94 hop, 9.2 ns. 꼬리: "m < n 이면?" → 연산이 원소보다 적으면 대부분의 원소는 건드리지도 않으니 의미 있는 건 m ≥ n 영역이다.

</details>

<details><summary>Q2. 경로 압축과 union by size 중 하나만 쓴다면? 펌웨어에서는?</summary>

size 만: 단일 find 최악 ⌊log₂ n⌋ (증명은 5. 풀이, 실측 2^20 → 20). 압축만: m 번 총 Θ(n + m·log_{1+m/n} n) (Tarjan & van Leeuwen; m ≈ n 이면 op 당 상각 O(log n)) 이지만 **단일 find 는 O(n)** — demo 에서 사슬 첫 find 739781 hop. 펌웨어(ISR·제어 루프 마감)에서는 최악이 중요하므로 **union by size 는 필수**, 압축(반감)은 평균을 더 줄이는 덤이다. 반감은 쓰기가 생기므로 플래시에 둔 읽기 전용 테이블이라면 size 만 쓰고 find 를 읽기 전용으로 둔다. rank 버전은 노드당 `uint8_t` rank 로 3 B (rank ≤ log₂ n ≤ 16), size 버전은 크기를 덤으로 준다 (메시 클러스터 크기처럼 필요하면 size).

</details>

<details><summary>Q3. 링크가 끊기면요? DSU 에서 삭제는?</summary>

못 한다 — 트리를 쪼개면 누가 누구의 조상이었는지 정보가 압축으로 사라졌다. 선택지: (1) 주기적/이벤트 시 재구성 O(n + m·α) — 1024 노드 메시면 밀리초 이하라 대부분 이걸로 충분, (2) 오프라인이면 역순 처리 (연습 2: 장애 순서를 거꾸로 = 링크 추가, LeetCode 류 "Reverse Union Find"), (3) 시간 구간이 있는 오프라인 질의면 분할 정복 + 되돌리기(rollback) DSU — 이때는 압축을 쓰면 안 되고 union by size 만 쓴다 (되돌릴 수 있도록), (4) 완전 동적 연결성은 polylog 상각 자료구조가 있지만 MCU 범위 밖. 꼬리: "rollback DSU 에서 압축을 왜 못 쓰나?" → 압축은 find 가 여러 포인터를 바꾸므로 되돌릴 기록이 커지고, union 하나 = 포인터 하나 변경이라는 성질이 깨진다.

</details>

<details><summary>Q4. Kruskal 과 Prim, 언제 무엇을?</summary>

Kruskal O(m log m) — 정렬이 지배, 간선 목록만 있으면 되고 포레스트를 자연스럽게 낸다. Prim O(V²) (행렬) 또는 O(m log V) (이진 힙). 이 맥 실측 (demo_mst.c, V = 2000, Kruskal 시간에 정렬 포함, Prim 은 인접 행렬이 주어졌다고 보고 행렬 생성 제외, 5회 중앙값):

```text
         E      E/V      sort ms   kruskal ms      prim ms kruskal/prim   same
      8000        4         0.47         0.39         5.06         0.08    yes
     50000       25         3.84         3.52         4.86         0.73    yes
    200000      100        18.91        18.40         5.07         3.63    yes
    800000      400       120.67       111.86         5.27        21.24    yes
   1999000      999       476.84       519.72         5.13       101.39    yes
```

희소(E/V = 4)는 Kruskal 이 13배 빠르고, E/V 25~100 사이에서 역전, 완전 그래프에서는 Prim 이 100배 빠르다. Kruskal 시간은 거의 전부 정렬 (sort ms 와 kruskal ms 가 비슷 — 둘은 따로 잰 값이라 측정 잡음만큼 순서가 뒤집히기도 한다). DSU 스캔 자체는 몇 % 다. 메모리: Prim 행렬 V² — 2000 노드면 16 MB 라 MCU 불가, 32 노드 메시면 `uint8_t` 비용 1 KB 로 충분. 꼬리: "좌표 n 개로 완전 그래프 MST (LeetCode 1584 Min Cost to Connect All Points)?" → 간선 n²/2 를 만들지 말고 Prim O(n²) 에서 거리를 즉석 계산.

</details>

<details><summary>Q5. 메시 라우팅 트리를 MST 로 만들면 되나요?</summary>

보통 아니다. MST 는 **링크 비용의 총합**을 최소화하고, 라우팅은 **각 노드에서 게이트웨이까지의 경로 비용**을 최소화해야 한다 — 그건 최단 경로 트리(ALGO-M06 다익스트라)다. 반례: 게이트웨이 0, 비용 0-1 = 10, 0-2 = 10, 1-2 = 1. MST = {1-2, 0-1} (총 11) 이라 노드 2 의 경로 비용 11, 최단 경로 트리는 {0-1, 0-2} (총 20) 이고 노드 2 의 경로 비용 10. 실제 저전력 메시 라우팅(예: RPL 의 DODAG)은 ETX 같은 경로 비용 기반 목적 함수로 부모를 고른다 (세부는 RFC 6550/6719 확인 권장). MST 가 맞는 곳: 전체 송신 에너지를 줄이는 브로드캐스트/플러딩 트리, 케이블 배선, Kruskal 을 k 요소에서 멈춘 단일 연결 클러스터링(single-linkage clustering).

</details>

<details><summary>Q6. Cortex-M 의 ISR 에서 dsu_same() 을 불러도 되나요? 최악 시간은?</summary>

시간: union by size 덕에 반복 ≤ ⌊log₂ n⌋ (1024 노드 → 10). 이 맥에서 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -Os -S` 로 본 반감 루프 본문은 8개 명령(`lsls, ldrh, strh, lsls, adds, ldrh, cmp, bne`), Cortex-M4 (`thumbv7em`, -Os) 는 `ldrh.w, strh, ldrh.w, add.w, cmp, bne` 6개 — `uint16_t` 라 `ldrh`/`strh` 반쪽 워드 접근이다. 즉 WCET 를 반복 상한 × 루프 사이클로 셀 수 있다. **하지만 재진입은 안전하지 않다**: find 는 "읽기" 처럼 보여도 반감으로 `parent[]` 를 **쓴다**. 태스크가 union 중일 때 ISR 이 find 로 같은 배열을 고치면 불변식이 깨질 수 있다. 실무 패턴: 라디오 ISR 은 링크 이벤트를 큐에 넣기만 하고 메시 태스크 하나가 DSU 를 소유한다. ISR 쪽 읽기가 꼭 필요하면 압축 없는 읽기 전용 find(쓰기 0)를 쓰고, 그래도 union 과의 경합은 임계 구역으로 막는다. (병렬 lock-free DSU 연구는 있지만 펌웨어에서 쓸 일은 드물다.)

</details>

<details><summary>Q7. 연결성 질의, 그냥 BFS 하면 안 되나요? DSU 가 못 하는 건?</summary>

질의 한 번이면 BFS O(n + m) 가 맞고 **경로**까지 준다 — DSU 는 "같은 집합인가" 만 알지 경로는 모른다. DSU 가 이기는 건 갱신과 질의가 섞여 반복될 때: 이벤트 q 개면 BFS 는 O(q(n + m)), DSU 는 O((q + m)·α). 이 레슨 테스트가 그 대비다 — 15000 메시 이벤트마다 BFS 를 다시 돌리는 참조가 DSU 를 검증한다. 또 DSU 는 무방향 전용이다: 방향 그래프의 도달 가능성/사이클(드라이버 의존성)은 DFS 3색이나 Kahn (ALGO-M05), 강한 연결 요소는 ALGO-A10.

</details>

<details><summary>Q8. 방향 그래프에서 DSU 로 사이클을 찾으면 왜 틀리죠?</summary>

DSU 는 방향을 잊는다. 간선 0→1, 0→2, 1→2 는 DAG(사이클 없음)이지만 세 번째 간선에서 1, 2 가 이미 같은 집합이라 DSU 는 "사이클" 이라고 답한다. 무방향으로 보면 0-1-2-0 삼각형이 맞으니 DSU 는 **무방향** 질문에 대해서만 옳다. 방향 사이클은 DFS 의 회색(진행 중) 정점으로 되돌아가는 간선(back edge)으로 찾는다 (ALGO-M05). 꼬리: "LeetCode 685 Redundant Connection II (방향 버전)?" → 들어오는 간선이 2개인 노드 후보 + DSU 를 섞는 케이스 분석 문제다.

</details>

## 9. 추가 연습

### 연습 1 — 그래프가 트리인가 (LeetCode 261 Graph Valid Tree)

n 노드, 간선 목록이 정확히 하나의 트리인지. 예: n = 5, `[[0,1],[0,2],[0,3],[1,4]]` → true, `[[0,1],[1,2],[2,3],[1,3],[1,4]]` → false.

<details><summary>답 보기</summary>

트리 ⇔ (간선 n − 1 개) ∧ (사이클 없음) — 5. 풀이의 "m ≥ n − c, 등호 ⇔ 숲" 에서 m = n − 1 이고 숲이면 c = 1. 그래서 `m != n - 1` 이면 바로 false, 아니면 Part B 처럼 union 이 한 번이라도 실패하면 false. 연결성을 따로 확인할 필요가 없다. 3000개 무작위 그래프(n ≤ 8)를 "m == n−1 ∧ BFS 요소 1개" 와 대조:

```text
연습 1: 3000 graphs (211 trees), mismatches=0
```

</details>

### 연습 2 — 링크 장애를 오프라인으로 (역순 Union-Find)

메시 링크 m 개가 모두 살아 있다가 장애 로그 `fail[0..q)` 순서로 하나씩 죽는다. 각 장애 직후의 클러스터 수를 구하라 (장애 분석 툴, 로그를 다 받은 뒤 계산).

<details><summary>답 보기</summary>

DSU 는 삭제를 못 하지만 시간을 거꾸로 돌리면 삭제가 추가가 된다. (1) 한 번도 안 죽은 링크로 DSU 를 만든다 = "전부 죽은 뒤" 상태 → `out[q-1]`. (2) i = q−1 부터 내려가며 `out[i]` 를 기록하고 `fail[i]` 를 union (되살림) → 이제 `fail[0..i-1]` 만 죽은 상태. 총 O((m + q)·α). 매 장애마다 BFS 로 다시 센 값과 대조:

```text
연습 2: 200 networks, 1966 failure events, mismatches=0
```

AlgoMonster M10 §7 (Umbristan | Reverse Union Find) 이 같은 기법이다. 온라인(장애가 실시간으로 옴)이면 이 방법은 못 쓴다 — Q3.

</details>

### 연습 3 — 모든 센서가 처음 연결되는 시각 (LeetCode 1101 The Earliest Moment When Everyone Become Friends)

링크 업 로그 `{t_ms, a, b}` (시간순이 아닐 수 있음)에서 전체가 한 클러스터가 되는 가장 이른 시각, 끝까지 안 되면 "없음".

<details><summary>답 보기</summary>

시각으로 정렬(여기선 m 이 작아 삽입 정렬) → 차례로 union, 처음 `comps == 1` 이 된 링크의 시각. n = 1 이면 처음부터 연결(시각 0으로 정의). LeetCode 예제(n = 6, 답 20190301) 확인 + 시각 T 를 0..999 로 올리며 BFS 하는 참조와 300개 대조:

```text
연습 3: 300 link logs (134 never fully connected), mismatches=0
```

</details>

### 연습 4 — 망 복구에 옮길 케이블 수 (LeetCode 1319 Number of Operations to Make Network Connected)

케이블 m 개로 이어진 n 대의 장치. 케이블 하나를 뽑아 다른 두 장치 사이에 꽂는 것을 한 번의 작업이라 할 때, 전체를 연결하는 최소 작업 수 (불가능이면 -1).

<details><summary>답 보기</summary>

m < n − 1 이면 어떻게 해도 연결 불가 → -1. 아니면 답 = c − 1 (c = 요소 수). 하한: 작업 하나는 요소 수를 최대 1 줄인다. 달성: 여분 케이블 수 = m − (n − c) ≥ (n − 1) − (n − c) = c − 1 이고, 여분(사이클 위) 케이블은 뽑아도 요소 수가 안 늘어나므로 c − 1 개를 옮겨 요소를 하나씩 이으면 된다. LeetCode 예제 세 개(1, 2, -1) + 1000개 무작위 대조:

```text
연습 4: 1000 networks, mismatches=0
```

</details>

### 연습 5 — union by rank (`uint8_t`) + 2-pass 완전 압축

rank 버전을 짜고 (1) 모든 union 반환값·요소 수·같은 집합 판정이 size 버전과 같은지, (2) rank ≤ ⌊log₂ n⌋ 인지, (3) 2배 짝짓기에서 루트 rank 가 정확히 log₂ n 인지, (4) 노드당 바이트를 확인하라.

<details><summary>답 보기</summary>

rank = 압축이 없었다면의 높이 상한. 같은 rank 끼리 합칠 때만 +1 이므로 rank r 인 루트의 트리에는 ≥ 2^r 노드 (size 증명과 같은 귀납) → rank ≤ log₂ n, n ≤ 65535 면 rank ≤ 15 라 `uint8_t` 로 충분 (4비트면 되지만 비트 필드 접근 비용이 아깝다). 압축은 rank 를 고치지 않는다 — 그래서 rank 는 "높이" 가 아니라 "상한" 이다. find 는 루트를 먼저 찾고(1-pass) 두 번째 훑기에서 모든 노드를 루트로 (2-pass, 재귀 없음).

```text
연습 5: rank variant vs size variant mismatches=0, rank>log2n=0, max rank seen=4, doubling n=256 -> root rank 8, bytes/node 3 vs 4
```

무작위 합치기에서는 rank 가 4 를 넘지 않았다(n ≤ 256). 최악(2배 짝짓기)에서만 log₂ n 에 닿는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 10/10 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 10/10, 보고 없음
```

## 10. 핵심 정리

- 집합 = 루트 있는 트리. `find` 는 루트로(반감: `parent[x] = parent[parent[x]]; x = parent[x]`), `union` 은 **루트끼리**, 작은 쪽을 큰 쪽 밑에, 같으면 false.
- 불변식: 루트 수 = `comps`, 루트의 `size` 만 유효, depth(x) ≤ log₂ |tree(x)| (작은 쪽만 내려가고 그때 크기 2배).
- union by size → 단일 find 최악 ⌊log₂ n⌋ (실측 2^20 → 20). 압축 → 상각만 좋아짐 (압축만이면 단일 find 739781 hop). 둘 다 → 상각 O(α), 상수 아님.
- 무방향 간선 (u, v) 가 사이클을 닫는다 ⇔ u, v 가 이미 같은 집합 = union 실패. m ≥ n − c, 등호 ⇔ 숲.
- Kruskal = 정렬 + union 성공 간선 채택, `k < m && cnt < n−1`. 교환 논증으로 MSF, 음수 가중치 OK, 끊긴 그래프 → 포레스트.
- 희소 그래프는 Kruskal, 조밀하면 Prim O(V²) (V = 2000 완전 그래프에서 100배 차). MST ≠ 라우팅 트리(최단 경로 트리).
- 펌웨어: 노드당 4 B 정적 배열, 재귀 금지, find 도 **쓴다** → ISR 공유 금지, 삭제 불가 → 재구성 또는 오프라인 역순.

## 참고자료

- AlgoMonster 노트 M10 §1~§7 (DSU Fundamentals, Path Compression · Union by Rank, Size of Connected Components, Number of Connected Components, Reverse Union Find) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M10_advanced_data_structures.md`
- AlgoMonster 노트 M07 §23~§24 (Minimum Spanning Tree, MST Forests) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M07_graph.md` (매핑 문서상 MST 전용 아이템이 없어 이 레슨이 맡는다)
- R. E. Tarjan, "Efficiency of a Good But Not Linear Set Union Algorithm", *J. ACM* 22(2), 1975 — α(m, n) 상한.
- R. E. Tarjan, J. van Leeuwen, "Worst-case Analysis of Set Union Algorithms", *J. ACM* 31(2), 1984 — 경로 반감·분할, 압축만 쓸 때의 한계.
- M. L. Fredman, M. E. Saks, "The Cell Probe Complexity of Dynamic Data Structures", STOC 1989 — Ω(α) 하한.
- J. B. Kruskal, "On the Shortest Spanning Subtree of a Graph and the Traveling Salesman Problem", *Proc. AMS* 7(1), 1956. R. C. Prim, "Shortest Connection Networks and Some Generalizations", *Bell System Technical Journal* 36(6), 1957.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* — 서로소 집합 장(3판 21장, 4판 19장)과 MST 장(3판 23장, 4판 21장). 테스트의 37 그래프는 3판 그림 23.1 (판에 따라 그림 번호가 다를 수 있어 확인 권장).
- D. S. J. De Couto et al., "A High-Throughput Path Metric for Multi-Hop Wireless Routing" (ETX), MobiCom 2003. RFC 6550 (RPL), RFC 6719 (MRHOF) — 메시 라우팅은 경로 비용 기반 (Q5).
- LeetCode 261 Graph Valid Tree · 323 Number of Connected Components in an Undirected Graph · 547 Number of Provinces · 684 Redundant Connection · 685 Redundant Connection II · 721 Accounts Merge · 1101 The Earliest Moment When Everyone Become Friends · 1319 Number of Operations to Make Network Connected · 1584 Min Cost to Connect All Points.
- 관련 레슨: ALGO-B11 (재귀 → 반복, 스택 깊이), ALGO-M04 (격자 BFS/DFS — 정적 연결 요소), ALGO-M05 (방향 그래프 사이클·위상 정렬), ALGO-M06 (다익스트라 — 최단 경로 트리), ALGO-A10 (강한 연결 요소), DS-B12 (이진 힙 — 정렬 도우미의 힙 정렬), DS-M11 (인덱스 PQ — 힙 Prim 의 decrease-key), C-A10 (MISRA 류 재귀 금지).
