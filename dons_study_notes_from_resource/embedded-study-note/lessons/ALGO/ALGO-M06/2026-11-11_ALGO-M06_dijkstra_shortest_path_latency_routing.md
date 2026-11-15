---
id: ALGO-M06
time: 80분
updated: 2026-11-11
---
# ALGO-M06 다익스트라: 가중 그래프 최단 경로

## 학습 목표

- 정적 배열 인접 리스트(forward star)와 **indexed min-PQ(decrease-key)** 로 다익스트라를 구현하고, 불변식 "pop 된 정점의 dist 는 최종값" 을 **음 아닌 가중치가 어디서 쓰이는지까지** 증명으로 말한다.
- `prev[]` 로 경로를 복원하고, 도달 불가 정점(INF)·출발점 자신·버퍼 부족을 구분해 돌려준다. 플로이드-워셜(Floyd–Warshall) 참조와 랜덤 그래프 800개 + 4정점 그래프 531441개 전수로 대조한다.
- **lazy deletion** 변형(중복 push + stale 버리기)을 짜고 push/pop 수를 indexed 판과 비교한다. 카운터 항등식(`pushes + decreases == 1 + relax`)으로 구현을 검사하고, 적대적 그래프에서 lazy 힙이 E 에 가깝게 커지는 것을 실측한다.
- 알고리즘과 무관한 **최단 경로 증명서(certificate) 검사기** 를 O(V+E) 로 짜서, 음수 간선 반례에서 다익스트라가 틀린 답을 내는 것을 잡는다.
- 메시 통신망(지연 ms)에서 목적지별 **다음 홉(next hop) 라우팅 테이블** 을 만들고, 링크 장애 후 재계산 결과를 손 계산 표와 맞춘다.

## 1. 문제

"가중치가 있는 최단 경로" 는 펌웨어 면접 코딩 라운드에서 BFS 다음으로 자주 나오는 그래프 문제다. 그리고 꼬리질문(음수 간선? decrease-key 없이? 정점 1000개에 RAM 은? 경로는 어떻게 꺼내나?)이 진짜 평가 대상이다.

상황: 지상 관제(GCS), 중계기(relay), 통신 마스트, 드론(UAV), 지상 로봇(UGV)이 무선 링크로 엮인 메시 네트워크가 있다. 링크마다 측정된 지연(ms)이 있고, 명령 패킷을 **지연 합이 가장 작은 경로**로 보내고 싶다. 홉 수가 아니라 지연 합이 기준이라 BFS(ALGO-M04)로는 안 된다 — 가중치가 있는 단일 출발점 최단 경로(single-source shortest path) 문제이고, 가중치가 음이 아니면 표준 답이 **다익스트라(Dijkstra)** 다.

핵심 아이디어: 출발점에서 "가까운 순서대로" 정점을 하나씩 **확정(settle)** 한다. 확정 안 된 정점 중 현재 추정 거리 `dist` 가 가장 작은 것을 꺼내면 그 값은 더 줄어들 수 없다 — 다른 경로는 이미 그보다 먼 곳을 거쳐야 하고, 가중치가 음이 아니니 돌아오면서 줄어들 수 없기 때문이다. 꺼낸 정점의 나가는 간선으로 이웃의 추정치를 낮추는 것을 **완화(relaxation)** 라고 한다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part A 예제 그래프 (지연 ms) — 굵은 선 = 최단 경로 트리(prev), 점선 = 안 쓰인 간선</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m06a)">
    <path d="M97 159 L213 81" stroke-dasharray="5 4"/>
    <path d="M98 179 L212 241" stroke-width="2.6"/>
    <path d="M230 230 L230 92" stroke-width="2.6"/>
    <path d="M247 80 L373 150" stroke-width="2.6"/>
    <path d="M247 240 L373 170" stroke-dasharray="5 4"/>
    <path d="M250 252 L520 252" stroke-dasharray="5 4"/>
    <path d="M407 170 L523 240" stroke-width="2.6"/>
    <path d="M410 160 L660 160" stroke-dasharray="5 4"/>
    <path d="M557 239 L663 171" stroke-width="2.6"/>
    <path d="M80 70 L80 150" stroke-dasharray="5 4"/>
  </g>
  <g stroke="currentColor">
    <circle cx="80" cy="170" r="20" class="f-acc"/>
    <circle cx="230" cy="70" r="20" class="f-ok"/><circle cx="230" cy="250" r="20" class="f-ok"/>
    <circle cx="390" cy="160" r="20" class="f-ok"/><circle cx="540" cy="250" r="20" class="f-ok"/>
    <circle cx="680" cy="160" r="20" class="f-ok"/><circle cx="80" cy="50" r="20" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="175">0</text><text x="230" y="75">1</text><text x="230" y="255">2</text><text x="390" y="165">3</text>
    <text x="540" y="255">4</text><text x="680" y="165">5</text><text x="80" y="55">6</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="140" y="112">4</text><text x="140" y="226">1</text><text x="238" y="160">2</text><text x="315" y="104">5</text>
    <text x="315" y="196">8</text><text x="380" y="268">10</text><text x="470" y="196">2</text><text x="520" y="152">6</text>
    <text x="616" y="196">3</text><text x="88" y="114">1</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="205">d=0</text><text x="258" y="56">d=3</text><text x="258" y="282">d=1</text><text x="372" y="134">d=8</text>
    <text x="560" y="282">d=10</text><text x="650" y="134">d=13</text><text x="108" y="44">d=INF</text>
  </g>
  <text x="10" y="302" fill="currentColor" font-size="12">0→5 최단 = 0→2→1→3→4→5 (5 홉, 13 ms). 홉이 적은 0→2→4→5 는 14 ms. 정점 6 은 6→0 만 있어 0 에서 도달 불가.</text>
</svg>
```

### Part A — 다익스트라 (indexed PQ) + 경로 복원

- 그래프: `graph_t` = 정적 배열 `head[MAX_V]`, `nxt/to/w[MAX_E]` (MAX_V = 256, MAX_E = 2048). 정점 id 는 `int16_t`.
- PQ: 주어진 indexed min-PQ (`heap[]` + 역함수 `pos[]`, 키 = 외부 배열 `dist[]`). decrease-key = "`dist[v]` 를 줄이고 `sift_up(pos[v])`" (DS-M11 에서 자세히).
- `int dijkstra_ipq(g, src, dist, prev, stats)` — `dist[v]` = 최소 지연(도달 불가 `INF = INT32_MAX`), `prev[v]` = 최단 경로에서 직전 정점. push/decrease/pop/성공한 완화 수를 센다. 반환 = 확정된 정점 수.
- `int path_build(prev, nv, src, dst, out, max)` — `out = [src, …, dst]`, 반환 = 정점 수(src == dst 면 1), 도달 불가 0, 버퍼 부족·`prev` 사이클 -1.

### Part B — lazy 변형, 검증기, 음수 간선

- `dijkstra_lazy` — decrease-key 없이 `(nd, v)` 를 또 push 하고, pop 때 `d > dist[v]` 면 버린다(stale). 결과는 Part A 와 같아야 하고, push/pop/stale 수를 비교한다.
- `int spt_verify(g, src, dist, prev)` — 주어진 `(dist, prev)` 가 **진짜 최단 거리임을 증명**하는지 O(V+E) 에 판정. 변조한 답 550개를 모두 거부해야 한다.
- 음수 간선 4정점 반례: 다익스트라가 틀린 값을 내고, 검증기가 그것을 거부하고, FW 는 맞는 값을 내는 것을 테스트로 확인 (벨만-포드는 ALGO-A02).

### Part C — 메시 라우팅 테이블

- `void route_table(dist, prev, nv, src, next_hop)` — `next_hop[v]` = src 에서 v 로 갈 때 **처음 보낼 이웃**. O(V).
- 주어진 `route_compute()` 가 전제(모든 w ≥ 0)를 O(E) 로 확인하고(음수면 `ROUTE_ERR_NEG`) Part A + `route_table` 을 부른다.
- 9노드 메시(GCS, RLY_N, RLY_S, MAST, UAV1~3, UGV1, 송신 전용 BUOY)에서 정상 / GCS–RLY_N 링크 장애 두 경우의 표를 손 계산 값과 비교.

## 2. Problem (English)

> **Lowest-latency routing.** A directed graph has `V <= 256` nodes and `E <= 2048` edges with integer latency `0 <= w <= 65535` ms (self-loops and parallel edges allowed). No dynamic allocation.
>
> 1. `int dijkstra_ipq(const graph_t *g, int src, int32_t *dist, int16_t *prev, pq_stats_t *st)` computes the minimum latency from `src` to every node (`INT32_MAX` if unreachable) and the predecessor on one shortest path, using a binary heap with decrease-key. `int path_build(const int16_t *prev, int nv, int src, int dst, int16_t *out, int max)` writes the node sequence `src..dst` and returns its length, `0` if unreachable, `-1` if `max` is too small.
> 2. Implement the lazy-deletion variant (no decrease-key). Write `spt_verify` that, given `(dist, prev)` from *any* algorithm, returns 1 iff they are provably correct, in `O(V + E)`. Show a graph with a negative edge on which Dijkstra returns a wrong distance.
> 3. Produce a routing table: for every destination, the latency and the first hop from `src`. Recompute after a link fails.
>
> Example (Part A graph, `src = 0`): `dist = [0, 3, 1, 8, 10, 13, INF]`, `path(0→5) = [0, 2, 1, 3, 4, 5]`, `path(0→6)` returns 0.
> Expected: `O((V + E) log V)` time, `O(V)` priority-queue entries for the indexed version.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 5개 함수 본문: `dijkstra_ipq`, `path_build` (Part A) / `dijkstra_lazy`, `spt_verify` (Part B) / `route_table` (Part C). 그래프, indexed PQ, lazy 힙, `route_compute`, 플로이드-워셜 참조, 그래프 생성기, 메시 시나리오는 주어진다. 스텁은 "전부 INF" / "검증 통과" 같은 틀렸지만 안전한 값을 돌려준다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:309  memcmp(dist, exp_d, sizeof exp_d) == 0
…
Part A random vs Floyd-Warshall: 800 graphs, 47694 edges, unreachable nodes 5156, mismatches=10883
…
Part B verifier: 300 corrupted answers tried, wrongly accepted=300
…
Part C next_hop random: 300 graphs, mismatches=300
FAIL starter.c:509  bad == 0
RAM: graph_t=16904 B, ipq_t=1040 B, lheap_t=16396 B, route_t=2048 B
11/36 passed
```

(exit 1. 통과한 11개는 스텁의 "전부 INF" 가 우연히 맞는 도달 불가 사례, "lazy == indexed" 처럼 둘 다 틀려서 같은 비교, 검증 스텁이 "무엇이든 맞다" 라서 통과한 것, 그리고 주어진 코드(플로이드-워셜 값, `route_compute` 의 음수 거부·반환값) 검사다.)

## 4. 힌트

<details><summary>힌트 1 — 루프 뼈대는 다섯 줄</summary>

`dist[src] = 0; push(src);` → `while (큐가 비지 않음) { u = pop(); done[u] = 1; for (u 의 간선 e) relax(e); }`. relax 는 `nd = dist[u] + w; if (nd < dist[v]) { dist[v] = nd; prev[v] = u; push 또는 decrease }`. BFS 와 다른 점은 큐가 FIFO 가 아니라 `dist` 최소 힙이라는 것, 그리고 **방문 표시를 push 때가 아니라 pop 때** 한다는 것.

</details>

<details><summary>힌트 2 — push 인가 decrease 인가</summary>

indexed PQ 는 `pos[v] == NONE` 이면 큐에 없다. `dist[v]` 를 먼저 줄인 뒤, 없으면 `ipq_push`, 있으면 `ipq_decrease` (키 배열이 곧 `dist` 라서 decrease 는 sift_up 만 하면 된다). `done[v]` 인 정점은 건너뛴다 — 음 아닌 가중치에서는 어차피 `nd >= dist[v]` 라 안 바뀌지만, 음수 간선이 섞였을 때 "확정 후 재개방" 을 막는 것이 다익스트라의 정의다.

</details>

<details><summary>힌트 3 — path_build 는 거꾸로 담고 뒤집는다</summary>

`prev` 는 dst → src 방향으로만 따라갈 수 있다. `v = dst` 부터 `out[len++] = v; if (v == src) break; v = prev[v];` 후 `out` 을 뒤집는다. 도달 불가는 `dst != src && prev[dst] == NONE`. 길이가 `nv` 를 넘으면 `prev` 에 사이클이 있다는 뜻이니 -1.

</details>

<details><summary>힌트 4 — 검증기: "모든 간선이 지켜지고, 모든 정점에 꼭 맞는 간선이 있다"</summary>

(1) `dist[src] == 0`. (2) 모든 간선 u→v 에 대해 `dist[v] <= dist[u] + w` — 이것이 성립하면 어떤 경로를 따라가도 `dist` 가 경로 길이 이하이므로 `dist <= 진짜 거리`. (3) 유한한 v 마다 `prev[v]→v` 간선이 `dist[prev] + w == dist[v]` 로 꼭 맞고 prev 사슬이 src 에 닿으면 길이 `dist[v]` 인 실제 경로가 있으므로 `dist >= 진짜 거리`. 0 가중치 사이클이 있으면 prev 끼리 서로 가리켜도 (3)의 "꼭 맞음" 은 통과한다 — 사슬이 src 에 닿는지 따로 본다.

</details>

<details><summary>힌트 5 — next_hop 은 위로 걷다가 아는 곳에서 멈춘다</summary>

`prev[u] == src` 인 u 는 자기 자신이 첫 홉이다. 그 밖의 v 는 `next_hop[v] = next_hop[prev[v]]`. 매번 src 까지 걸으면 O(V·깊이) 이므로, 걷다가 이미 답이 있는 조상을 만나면 멈추고, 걸어온 정점들을 스택에서 꺼내며 한 번에 채운다 → 각 정점은 한 번만 걷는다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 방향 그래프인가 (링크 지연이 비대칭일 수 있나)? 가중치 범위·**음수 가능?** 정점/간선 수 상한? 출발점 하나인가, 목적지 하나인가? 경로도 필요한가, 거리만인가? 도달 불가 표현은? |
| 2. 무식한 방법 | 모든 단순 경로 나열은 지수. 벨만-포드 O(VE) 는 음수도 되지만 느리다. 플로이드-워셜 O(V³) 은 모든 쌍 — 테스트 기준(oracle)으로 남긴다 |
| 3. 더 나은 생각 + 불변식 | w ≥ 0 이면 "가까운 순서로 확정". 불변식: **pop 된 정점의 dist 는 최종**, 큐 안의 dist 는 "확정 정점만 거치는 최단" |
| 4. 코드 | 힙 + `done[]` + relax. decrease-key 가 있는 PQ 면 크기 ≤ V, 없으면 lazy (중복 push + stale 버림) |
| 5. 소리 내어 말할 테스트 | src 자신, 도달 불가, 0 가중치 간선/사이클, 자기 루프, 중복 간선, 동점, 정점 1개, 음수 간선이 들어오면 거부 |
| 6. 복잡도 | 시간 O((V+E) log V), 추가 메모리 O(V) (lazy 는 PQ O(E)), 스택 O(1) — 재귀 없음. 조밀 그래프·작은 V 는 O(V²) 배열판도 후보 |

### Part A — 불변식과 증명

δ(v) 를 진짜 최단 거리라고 하자. 증명에 필요한 사실은 둘이다.

**보조정리 (위에서 내려옴).** 실행 중 언제나 `dist[v] >= δ(v)`. 유한한 `dist[v]` 는 항상 `dist[u] + w(u,v)` 로 만들어졌고, 귀납적으로 `dist[u]` 가 어떤 실제 걸음(walk)의 길이이므로 `dist[v]` 도 실제 걸음 src→…→u→v 의 길이다. 걸음 길이는 δ 이상이다.

**정리 (확정 = 최종).** u 가 pop 되는 순간 `dist[u] = δ(u)`.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">확정 집합 S 와 "처음으로 S 를 벗어나는 정점 y" — 귀류법</text>
  <rect x="30" y="40" width="300" height="170" rx="14" class="f-ok" stroke="currentColor"/>
  <text x="44" y="62" fill="currentColor">S = 이미 pop 됨 (dist = δ 라고 가정)</text>
  <rect x="350" y="40" width="370" height="170" rx="14" class="f-mut" stroke="currentColor"/>
  <text x="364" y="62" fill="currentColor">아직 확정 안 됨 (큐 안이거나 INF)</text>
  <g stroke="currentColor">
    <circle cx="80" cy="140" r="16" class="f-acc"/><circle cx="270" cy="140" r="16" class="f-ok"/>
    <circle cx="420" cy="140" r="16" class="f-warn"/><circle cx="640" cy="140" r="16" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="145">s</text><text x="270" y="145">x</text><text x="420" y="145">y</text><text x="640" y="145">u</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m06b)">
    <path d="M96 140 L252 140" stroke-dasharray="5 4"/>
    <path d="M286 140 L402 140" stroke-width="2.4"/>
    <path d="M436 140 L622 140" stroke-dasharray="5 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="130" y="128">최단 경로 P 의 앞부분</text><text x="318" y="128">간선 x→y</text><text x="470" y="128">나머지 (w ≥ 0)</text>
    <text x="370" y="186">dist[y] ≤ δ(x) + w(x,y) = δ(y)</text>
    <text x="560" y="186">δ(y) ≤ δ(u)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="236">u 가 "dist[u] &gt; δ(u) 인데 pop 된 첫 정점" 이라 하자. P 위에서 S 밖의 첫 정점을 y (직전 x ∈ S) 라 하면:</text>
    <text x="30" y="256">dist[y] ≤ δ(y) ≤ δ(u) &lt; dist[u]. 그런데 u 가 큐에서 최소로 뽑혔으니 dist[u] ≤ dist[y]. 모순.</text>
    <text x="30" y="274">음 아닌 가중치는 오직 "δ(y) ≤ δ(u)" (경로의 앞부분은 전체보다 길지 않다) 한 줄에서 쓰인다.</text>
  </g>
</svg>
```

자세히: u 가 반례인 첫 정점이면, u 직전까지 pop 된 S 의 정점은 모두 `dist = δ`. src 는 S 안에 있고 u 는 밖이므로 최단 경로 P(src→u) 위에 "S 밖의 첫 정점" y 가 있다 (y = u 일 수도 있다). 그 직전 x 는 S 안이고 x 가 pop 될 때 간선 x→y 를 완화했으므로 `dist[y] <= δ(x) + w(x,y) = δ(y)` (최단 경로의 부분 경로도 최단 — 부분 구조 최적성). y = u 면 `dist[u] <= δ(u)` 로 바로 모순. y ≠ u 면 P 에서 y 이후 부분의 가중치가 음이 아니므로 `δ(y) <= δ(u) < dist[u]`, 그런데 힙이 u 를 골랐으니 `dist[u] <= dist[y]` — 모순. ∎

나머지 둘: (a) 루프가 끝났을 때 pop 안 된 정점은 모두 도달 불가다 — 도달 가능한데 남았다면 위와 같은 y 가 큐 안에 있어 루프가 안 끝났다. (b) `prev[v]` 는 마지막으로 성공한 완화의 출처 u 이고, 그때 u 는 이미 확정이므로 간선 `prev[v]→v` 는 **꼭 맞는(tight)** 간선이다. 그리고 `prev[v]` 는 v 보다 먼저 pop 됐으므로 prev 그래프에는 사이클이 없다 (0 가중치 사이클이 있어도) — `path_build` 가 항상 src 에 닿는다.

손 계산 (위 그림, src = 0). 간선은 forward star 라 **추가의 역순**으로 나온다.

| 단계 | pop (u, dist) | 완화 | PQ 연산 | 이후 PQ (id:dist) |
|---|---|---|---|---|
| 0 | — | dist[0] = 0 | push 0 | 0:0 |
| 1 | 0, 0 | 2 ← 1, 1 ← 4 | push, push | 2:1 1:4 |
| 2 | 2, 1 | 4 ← 11, 3 ← 9, 1: **3 &lt; 4** | push, push, **decrease** | 1:3 3:9 4:11 |
| 3 | 1, 3 | 3: **8 &lt; 9** | **decrease** | 3:8 4:11 |
| 4 | 3, 8 | 5 ← 14, 4: **10 &lt; 11** | push, **decrease** | 4:10 5:14 |
| 5 | 4, 10 | 5: **13 &lt; 14** | **decrease** | 5:13 |
| 6 | 5, 13 | (나가는 간선 없음) | — | 비어 있음 → 6 은 INF |

실제 출력: `Part A hand: dist = 0 3 1 8 10 13 INF, pushes=6 decreases=4 pops=6 max_pq=3`. 성공한 완화 9 = push 5 (src 제외) + decrease 4.

`path_build(0→5)`: 5 → prev 4 → 3 → 1 → 2 → 0 을 담고 뒤집어 `[0,2,1,3,4,5]`. 도달 불가(6)는 0, `max = 5` 로 부르면 6칸이 필요하니 -1.

**오버플로**: `INF = INT32_MAX` 이고 완화는 pop 된(유한한) u 에서만 하므로 `INF + w` 는 계산되지 않는다. `nd <= δ(u) + w <= (V-1)·65535 + 65535 = 256·65535 = 16,776,960 < 2^31`. 가중치 상한이나 V 상한을 바꾸면 이 곱을 다시 계산해야 한다 (Q7).

**랜덤 대조**: V = 1..40, E = 0..6V, 가중치 0..65535 와 0..20 (동점·0 이 많음) 을 번갈아, 자기 루프·중복 간선 허용. 참조는 플로이드-워셜 (힙이 전혀 없는 독립 구현, O(V³)). 모든 v 에 대해 dist 일치, 복원 경로가 실제 간선이고 비용 == dist, 도달 불가 ↔ 경로 0. 그리고 4정점 그래프 **전수**: 방향 간선 12개 각각 {없음, 0, 2} → 3^12 = 531441 개.

```text
Part A random vs Floyd-Warshall: 800 graphs, 47694 edges, unreachable nodes 5156, mismatches=0
Part A exhaustive 4-node: 531441 graphs, mismatches fw=0 lazy=0 verify_reject=0
```

### Part A — 복잡도

| 구현 | PQ 연산 | 시간 | PQ 최대 크기 |
|---|---|---|---|
| indexed (decrease-key) | push ≤ V, pop ≤ V, decrease ≤ E | O((V + E) log V) | ≤ V |
| lazy (중복 push) | push = 1 + 성공한 완화 ≤ 1 + E, pop = push | O(E log E) = O(E log V) (단순 그래프: E ≤ V², log E ≤ 2 log V) | ≤ 1 + E |
| 배열 O(V²) (PQ 없음) | 매번 V 개 선형 탐색 | O(V² + E) | — |

decrease ≤ E 인 이유: 간선 u→v 는 u 가 pop 될 때 **딱 한 번** 검사된다. 추가 메모리는 `dist`, `prev`, `done`, PQ 로 O(V), 스택은 재귀가 없어 O(1) (`done[256]` 지역 배열 256 B 는 있다).

### Part B — lazy deletion: 같은 답, 다른 비용

lazy 는 `done[]` 대신 "pop 한 `d` 가 지금의 `dist[u]` 와 같은가" 로 확정을 판정한다. 더 큰 `d` 는 그 뒤에 더 짧은 값이 들어왔다는 뜻 — 버린다. push 는 엄격한 개선 때만 하므로 정점 u 의 엔트리 키는 들어온 순서대로 엄격히 감소하고, `d == dist[u]` 인 엔트리는 **마지막 하나뿐**이다 → 음 아닌 가중치에서 각 정점은 한 번만 처리된다.

손 계산 그래프에서 lazy: push 10 (= 1 + 완화 9), pop 10, stale 4 (`1:4, 3:9, 4:11, 5:14` — 위 표에서 decrease 4번이 남긴 낡은 엔트리 4개와 정확히 같다), 힙 최대 4. 실제 출력:

```text
Part B hand: lazy pushes=10 pops=10 stale=4 max_heap=4 | indexed pushes=6 decreases=4 pops=6 max_pq=3
Part B random: 600 graphs, dist mismatches=0, identity failures=0 | total lazy pushes=66683 stale=19930, indexed pushes=46753 decreases=20033
```

랜덤 600개 (V ≤ 200, E ≤ 8V, 가중치 0..3 과 0..1000) 에서 매번 검사한 **카운터 항등식**:

- indexed: `pushes == pops == 도달 가능 수`, `pushes + decreases == 1 + relax_ok`, `max_pq <= V`
- lazy: `pushes == 1 + relax_ok`, `pops == pushes`, `stale == pushes - 처리한 정점 수`, `pushes <= E + 1`

이런 항등식은 결과 비교만으로는 안 보이는 버그(예: stale 엔트리를 처리해 버려 시간만 늘어나는 버그 — 7. 실수 2)를 잡는다.

**벤치마크 — `demo_bench.c`**. 세 구현을 같은 그래프에서 돌린다 (V = 1000 랜덤 w 1..1000, 완전 그래프 V = 300, 그리고 적대적 DAG: 간선 i→i+1 가중치 1, i→j(j > i+1) 가중치 3n − 2i — 정점 i 가 확정될 때마다 남은 모든 j 가 1 씩 개선돼 **모든 완화가 성공**).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

이 맥 (Apple Silicon M 시리즈, Apple clang 21.0.0, -O2), 워밍업 2회 + 표본 11개 중앙값, 실제 출력:

```text
random E=4V  (V=1000, E=4000)
  indexed      49.1 us  pushes=   1000 pops=   1000 decrease=    334 max_pq=    455
  lazy         29.9 us  pushes=   1334 pops=   1334 stale   =    334 max_pq=    573
  array      1424.5 us  (no PQ: 1000 linear scans of V)
random E=16V  (V=1000, E=16000)
  indexed      91.0 us  pushes=   1000 pops=   1000 decrease=   1383 max_pq=    771
  lazy         97.8 us  pushes=   2384 pops=   2384 stale   =   1384 max_pq=   1643
  array      1033.7 us  (no PQ: 1000 linear scans of V)
random E=64V  (V=1000, E=64000)
  indexed     482.0 us  pushes=   1000 pops=   1000 decrease=   2621 max_pq=    917
  lazy        502.6 us  pushes=   3628 pops=   3628 stale   =   2628 max_pq=   2930
  array      1469.0 us  (no PQ: 1000 linear scans of V)
complete graph  (V=300, E=89700)
  indexed     249.2 us  pushes=    300 pops=    300 decrease=   1256 max_pq=    299
  lazy        247.0 us  pushes=   1549 pops=   1549 stale   =   1249 max_pq=   1361
  array       231.7 us  (no PQ: 300 linear scans of V)
adversarial DAG  (V=700, E=244650)
  indexed     626.8 us  pushes=    700 pops=    700 decrease= 243951 max_pq=    699
  lazy      17377.0 us  pushes= 244651 pops= 244651 stale   = 243951 max_pq= 243952
  array       994.0 us  (no PQ: 700 linear scans of V)
```

(각 그래프마다 "dist identical across methods: yes" 줄이 붙는다 — 생략. 다른 작업이 같이 돌던 맥이라 시간은 실행마다 흔들렸다: 두 번째 실행에서 array V=1000 이 889 / 1358 / 2130 us, lazy E=4V 가 23 us. 횟수 열은 결정적이다. 비율만 본다.)

| 관찰 | 의미 |
|---|---|
| 희소(E=4V): lazy 30 us < indexed 49 us | 낡은 엔트리가 적으면 간접 참조(`dist[heap[i]]`)와 `pos` 갱신이 없는 lazy 가 싸다 |
| E=64V: max PQ indexed 917 vs lazy 2930 | indexed 는 **≤ V 보장**, lazy 는 E 에 비례해 자란다 |
| 완전 그래프 V=300: 세 구현이 비슷 (232~249 us) | E ≈ V² 이면 O(V²) 배열판이 힙판과 같은 급 — 그리고 코드가 가장 짧고 PQ 메모리가 0 |
| 적대적 DAG: lazy **17.4 ms** vs indexed 0.63 ms, lazy 힙 243952 엔트리 | decrease = E − (V−1) = 243951 로 상한에 닿는다. lazy 는 그만큼 push 하고 다 pop 해서 버린다. 펌웨어라면 힙 배열을 E 크기로 정적 확보해야 한다 |

DS-M11 의 같은 비교(V = 1000, 무작위)와 결론이 같다. 이 레슨의 추가는 **최악 입력**에서 둘의 격차(시간 약 28배, PQ 크기 약 350배)다.

### Part B — 검증기: 최단 경로 증명서

`spt_verify` 는 알고리즘을 다시 돌리지 않고 결과만 본다. 조건 (1)~(4) (solution.c 주석) 가 성립하면 `dist = δ` 임을 증명할 수 있다.

- **dist ≤ δ**: src 에서 v 로 가는 아무 경로 `src = p0 → p1 → … → pk = v` 에서 (1) `dist[p0] = 0`, (2) 를 k 번 쓰면 `dist[v] <= 경로 길이`. 최단 경로에 적용하면 `dist[v] <= δ(v)`. 이 단계는 v 가 도달 가능하면 `dist[v]` 가 유한해야 한다는 것도 준다 (2 의 "도달 가능한데 INF" 검사).
- **dist ≥ δ**: (3) 으로 prev 사슬이 src 에 닿고 사슬의 모든 간선이 꼭 맞으므로, 사슬을 거꾸로 이은 경로의 길이가 정확히 `dist[v]` → `δ(v) <= dist[v]`. 유한한 dist 는 실제 경로가 있으니 도달 불가 정점이 유한한 값을 가질 수 없다.

선형계획(LP) 관점으로는 (2) 가 쌍대 실행 가능성(feasible potential), (3) 의 꼭 맞는 간선이 상보 여유(complementary slackness) 다 — 면접에서는 "삼각 부등식 + 꼭 맞는 트리" 로 말하면 충분하다. 비용은 O(V + E) (prev 사슬 검사는 색칠로 각 정점 한 번) 라 알고리즘 자체(O((V+E) log V))보다 싸다. 디버그 빌드에서 매번 `assert(spt_verify(...))` 를 걸 수 있는 이유다.

0 가중치 사이클 함정: `0→1 (5)`, `1↔2 (0)` 에서 `dist = [0,5,5]`, `prev = [NONE, 2, 1]` 은 (2)·(3)의 꼭 맞음은 통과하지만 prev 끼리 가리켜 src 에 닿지 않는다 — 사슬 검사가 필요한 이유이고 테스트에 그대로 들어 있다. 변조 테스트 (dist ±1, 도달 가능 → INF, 도달 불가 → 0):

```text
Part B verifier: 550 corrupted answers tried, wrongly accepted=0
```

### Part B — 음수 간선 반례

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">음수 간선 하나로 "확정 = 최종" 이 깨진다 (s=0, a=1, b=2, t=3)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m06c)">
    <path d="M97 112 L262 64" stroke-width="2"/>
    <path d="M97 132 L262 178" stroke-width="2"/>
    <path d="M280 168 L280 72" stroke-width="2.4" stroke-dasharray="6 3"/>
    <path d="M300 55 L460 55" stroke-width="2"/>
  </g>
  <g stroke="currentColor">
    <circle cx="80" cy="122" r="18" class="f-acc"/><circle cx="280" cy="55" r="18" class="f-warn"/>
    <circle cx="280" cy="188" r="18" class="f-mut"/><circle cx="480" cy="55" r="18" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="127">s</text><text x="280" y="60">a</text><text x="280" y="193">b</text><text x="480" y="60">t</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="170" y="76">2</text><text x="170" y="172">3</text><text x="290" y="125">−2</text><text x="380" y="46">1</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="520" y="40">진짜: a = 1 (s→b→a)</text><text x="520" y="58">      t = 2</text>
    <text x="520" y="90">다익스트라: a = 2, t = 3</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="226">pop s → a=2, b=3 / pop a(2) 확정 → t=3 / pop b(3): b→a 로 a=1 이 되지만 이미 확정 → 무시</text>
    <text x="20" y="244">증명에서 깨진 줄: y=b, u=a 일 때 δ(b)=3 &gt; δ(a)=1 — 경로의 앞부분이 전체보다 길다.</text>
  </g>
</svg>
```

```text
Part B negative edge: true(FW) a=1 t=2 | indexed a=2 t=3 verify=0 | lazy a=1 t=2 verify=1 (pops=6)
```

세 가지를 읽는다.

1. indexed 다익스트라는 a = 2, t = 3 으로 **틀리고**, 검증기가 간선 b→a (`3 + (−2) = 1 < 2`) 에서 거부한다. 플로이드-워셜은 음수 사이클만 없으면 음수 간선도 맞게 푼다.
2. lazy 판은 이 그래프에서 **맞았다**. `done[]` 이 없어서 a 의 dist 가 1 로 줄자 a 를 다시 push 하고 다시 처리했기 때문이다. 이것은 다익스트라가 아니라 "우선순위 큐를 쓰는 label-correcting" 알고리즘이 된 것이다. 음수 사이클이 없으면 결국 맞게 끝나지만(검증기 조건 (2) 가 끝에서 성립), 같은 정점을 여러 번 열 수 있어 최악은 지수 시간이라고 알려져 있다 (Johnson 1973 — 확인 권장). "lazy 면 음수도 되네" 라고 말하면 안 되는 이유다.
3. 펌웨어 API `route_compute` 는 O(E) 로 음수를 먼저 찾아 `ROUTE_ERR_NEG` 를 돌려준다. 전제를 코드로 확인하는 것이 비용 대비 가장 싼 방어다. 음수가 정말 필요하면 벨만-포드 O(VE), 또는 퍼텐셜로 가중치를 다시 매기는 Johnson 방식(ALGO-A02).

"모든 가중치에 큰 상수 c 를 더해 음수를 없애면?" 은 흔한 오답이다. 홉이 k 개인 경로는 kc 만큼 늘어나 **홉이 적은 경로를 편애**하게 된다. 위 그림에서 c = 2 면 s→a = 4, s→b→a = 5 + 0 = 5 로 순위가 바뀐다.

### Part C — 메시 라우팅 테이블

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">메시 링크 (양방향, 지연 ms). 굵은 선 = GCS 기준 최단 경로 트리, 상자 안 = GCS 에서의 지연</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 175 L200 85" stroke-width="3"/>
    <path d="M60 175 L200 265" stroke-width="3"/>
    <path d="M60 175 L390 175" stroke-dasharray="5 4"/>
    <path d="M200 85 L200 265" stroke-dasharray="5 4"/>
    <path d="M200 85 L390 55" stroke-width="3"/>
    <path d="M200 85 L390 175" stroke-width="3"/>
    <path d="M200 265 L390 305" stroke-width="3"/>
    <path d="M390 175 L560 85" stroke-dasharray="5 4"/>
    <path d="M390 55 L560 85" stroke-width="3"/>
    <path d="M560 85 L680 205" stroke-width="3"/>
    <path d="M390 175 L680 205" stroke-dasharray="5 4"/>
    <path d="M390 305 L680 205" stroke-dasharray="5 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="118" y="122">8</text><text x="118" y="236">5</text><text x="290" y="168">30</text><text x="206" y="180">4</text>
    <text x="290" y="62">8</text><text x="300" y="122">9</text><text x="290" y="296">20</text><text x="470" y="120">6</text>
    <text x="470" y="62">3</text><text x="626" y="138">7</text><text x="530" y="182">15</text><text x="530" y="268">25</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="162" width="80" height="26" rx="6" class="f-acc"/>
    <rect x="160" y="72" width="80" height="26" rx="6" class="f-ok"/><rect x="160" y="252" width="80" height="26" rx="6" class="f-ok"/>
    <rect x="350" y="162" width="80" height="26" rx="6" class="f-ok"/><rect x="350" y="42" width="80" height="26" rx="6" class="f-ok"/>
    <rect x="520" y="72" width="80" height="26" rx="6" class="f-ok"/><rect x="640" y="192" width="80" height="26" rx="6" class="f-warn"/>
    <rect x="350" y="292" width="80" height="26" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">
    <text x="60" y="180">GCS 0</text><text x="200" y="90">RLY_N 8</text><text x="200" y="270">RLY_S 5</text>
    <text x="390" y="180">MAST 17</text><text x="390" y="60">UAV1 16</text><text x="560" y="90">UAV2 19</text>
    <text x="680" y="210">UAV3 26</text><text x="390" y="310">UGV1 25</text>
  </g>
  <text x="10" y="342" fill="currentColor" font-size="12">UAV3 로: 2 홉 GCS→MAST→UAV3 = 45 ms  vs  4 홉 GCS→RLY_N→UAV1→UAV2→UAV3 = 26 ms (굵은 선)</text>
  <text x="10" y="364" fill="currentColor" font-size="12">BUOY 는 BUOY→MAST(10) 단방향만 있어 그림에서 생략 — GCS 에서 도달 불가.</text>
</svg>
```

라우터(또는 메시 노드의 펌웨어)는 전체 경로가 아니라 **"이 목적지면 어느 이웃에게 넘기나"** 만 저장한다 (hop-by-hop 포워딩). `next_hop[v]` 는 prev 트리에서 src 바로 아래 조상이다. 순진하게 각 v 에서 src 까지 걸으면 O(V·깊이) = 최악 O(V²) (사슬 그래프). `route_table` 은 걷다가 답을 아는 조상에서 멈추고 걸어온 정점을 스택에서 꺼내 채우므로 각 정점이 스택에 한 번만 들어간다 → O(V). (스택 배열 `stk[MAX_V]` 는 `static` — 512 B 를 호출 스택에 두지 않는다.)

실제 출력 (정상 → GCS–RLY_N 링크 장애):

```text
Part C mesh (from GCS):                Part C mesh, link GCS-RLY_N down:
  GCS      0 ms  next=GCS                GCS      0 ms  next=GCS
  RLY_N    8 ms  next=RLY_N              RLY_N    9 ms  next=RLY_S
  RLY_S    5 ms  next=RLY_S              RLY_S    5 ms  next=RLY_S
  MAST    17 ms  next=RLY_N              MAST    18 ms  next=RLY_S
  UAV1    16 ms  next=RLY_N              UAV1    17 ms  next=RLY_S
  UAV2    19 ms  next=RLY_N              UAV2    20 ms  next=RLY_S
  UAV3    26 ms  next=RLY_N              UAV3    27 ms  next=RLY_S
  UGV1    25 ms  next=RLY_S              UGV1    25 ms  next=RLY_S
  BUOY   unreachable                     BUOY   unreachable
```

(두 블록은 프로그램이 위아래로 찍은 것을 나란히 놓은 것.) 손 계산: 장애 후 RLY_N = min(직통 없음, 5 + 4) = 9, MAST = min(30, 9 + 9) = 18, UAV1 = 9 + 8 = 17, UAV2 = min(17 + 3, 18 + 6) = 20, UAV3 = min(20 + 7, 18 + 15, 25 + 25) = 27. 모든 트래픽이 RLY_S 로 몰린다 — 실제 망이라면 RLY_S 링크 부하가 다음 문제다 (지연만 보는 최단 경로는 혼잡을 모른다).

설계 포인트:

- **홉 수 ≠ 지연**: BFS(ALGO-M04)는 UAV3 로 2 홉 경로(45 ms)를 고른다. 지연 기준은 4 홉 26 ms.
- **방향성**: 링크는 방향 간선 2개라 비대칭 지연을 넣을 수 있다. BUOY 는 송신 전용(BUOY→MAST) 이라 GCS 에서는 도달 불가지만, BUOY 에서 GCS 로는 간다: 장애 상황에서 `route_compute(BUOY)` → GCS 28 ms, 첫 홉 MAST (테스트에 있다). "모든 노드에서 GCS 로 가는 다음 홉" 이 필요하면 V 번 돌리지 말고 **역방향 그래프에서 GCS 를 출발점으로 한 번** 돌리면 된다 (연습 5 의 아이디어와 같은 뒤집기).
- **링크 장애**: 이 레슨은 그래프를 다시 만들고 전부 재계산한다. 위 벤치에서 V = 1000, E = 4000 도 이 맥에서 50 µs 안팎이었으니, 정점 수십 개 메시의 재계산은 링크 상태 변화(초 단위)에 비해 무시할 만하다 — MCU 에서는 클럭·캐시 차이만큼 느리므로 실측 권장. 증분 갱신(dynamic SSSP)은 복잡도에 비해 이득이 작아 면접에서는 "전체 재계산 + 변화 감지 시에만" 이 정답에 가깝다.
- **RAM**: 실제 출력 `graph_t=16904 B, ipq_t=1040 B, lheap_t=16396 B, route_t=2048 B` (이 맥 64비트 기준, `ipq_t` 의 키 포인터 8 B 포함). 그래프의 대부분은 `MAX_E × (2+2+4) B = 16 KB` — 9노드 메시에 2048 간선은 과하다. 제품에서는 MAX_* 를 실제 상한에 맞추고, 토폴로지가 고정이면 CSR 을 `const` 로 플래시에 둔다. lazy 힙이 그래프만큼 큰 것도 여기서 보인다 — indexed 를 쓰면 PQ 는 1 KB.

### 복잡도 요약

| 함수 | 시간 (최악) | 추가 메모리 | 스택 |
|---|---|---|---|
| `dijkstra_ipq` | O((V+E) log V) | O(V): dist, prev, done, PQ ≤ V | O(1) + `done[]` 256 B |
| `dijkstra_lazy` | O(E log E) | PQ ≤ 1 + E 엔트리 (8 B 씩) | O(1) |
| `path_build` | O(경로 길이) ≤ O(V) | 호출자 버퍼 | O(1) |
| `spt_verify` | O(V + E) | `mark[V]` (static) | O(1) |
| `route_table` | O(V) | `stk[V]` (static) | O(1) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 36/36 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 36/36, 보고 없음
```

실제 출력 (메시 표는 5. 풀이에 있으므로 생략):

```text
Part A hand: dist = 0 3 1 8 10 13 INF, pushes=6 decreases=4 pops=6 max_pq=3
Part A random vs Floyd-Warshall: 800 graphs, 47694 edges, unreachable nodes 5156, mismatches=0
Part A exhaustive 4-node: 531441 graphs, mismatches fw=0 lazy=0 verify_reject=0
Part B hand: lazy pushes=10 pops=10 stale=4 max_heap=4 | indexed pushes=6 decreases=4 pops=6 max_pq=3
Part B random: 600 graphs, dist mismatches=0, identity failures=0 | total lazy pushes=66683 stale=19930, indexed pushes=46753 decreases=20033
Part B verifier: 550 corrupted answers tried, wrongly accepted=0
Part B negative edge: true(FW) a=1 t=2 | indexed a=2 t=3 verify=0 | lazy a=1 t=2 verify=1 (pops=6)
…
Part C next_hop random: 300 graphs, mismatches=0
RAM: graph_t=16904 B, ipq_t=1040 B, lheap_t=16396 B, route_t=2048 B
36/36 passed
```

(-O2 빌드 전체 0.12 s user, ASan/UBSan 빌드 0.46 s.)

## 7. 자주 하는 실수

**1. push 할 때 방문 표시 (BFS 습관).** BFS 는 처음 발견한 거리가 최종이라 push 때 `visited` 를 찍는다. 가중치가 있으면 처음 발견이 최단이 아니다. 손 계산 그래프에서 0 을 처리하며 1 을 `dist 4` 로 push 하고 방문 표시하면, 2 를 거친 `1 + 2 = 3` 이 들어올 수 없어 dist[1] = 4, 그 뒤 dist[3] = 9 … 로 줄줄이 틀린다. 고침: 확정(`done`)은 **pop 때**.

**2. lazy 에서 stale 검사 빠뜨림.** `if (d > dist[u]) continue;` 가 없으면 결과는 (음 아닌 가중치에서) 여전히 맞다 — 낡은 엔트리로 다시 완화해도 `nd >= dist[v]` 라 아무것도 안 바뀐다. 그래서 결과 비교 테스트는 통과하고 **시간만** 늘어난다: 낡은 pop 마다 나가는 간선을 전부 다시 훑는다. 적대적 DAG 라면 stale 243951 번 × 평균 차수 수백. 고침: stale 검사, 그리고 Part B 의 항등식 `stale == pushes − 처리 수` 같은 카운터 테스트.

**3. `dist[u] + w` 오버플로.** `INF = INT32_MAX` 인데 INF 정점에서 완화하면 `INT32_MAX + w` — 부호 있는 오버플로 = UB (C11 6.5p5). pop 된 정점만 완화하면 생기지 않지만, "배열판에서 INF 인 u 를 골라 버리는" 실수(선형 탐색에서 `dist[v] != INF` 조건 누락)와 결합하면 나타난다. `INF = INT32_MAX / 2` 로 두는 습관도 있지만, 그러면 "합이 INF 보다 크면 도달 불가" 를 별도로 보장해야 한다. 고침: 유한한 u 에서만 완화 + 최장 경로 상한 계산 (Part A 오버플로 단락).

**4. indexed PQ 에서 decrease 대신 또 push.** `pos[v] != NONE` 인데 `ipq_push(v)` 를 하면 `heap[]` 에 같은 id 가 두 칸, `pos[v]` 는 나중 칸만 가리킨다 → 한쪽 엔트리가 고아가 되어 PQ 크기 ≤ V 보장이 깨지고 정적 배열을 넘칠 수 있다. 고침: `pos[v] == NONE ? push : decrease`. 키를 줄이기 **전에** sift_up 하면 아무 일도 안 일어난다 — 키 갱신이 먼저.

**5. 조기 종료를 push 때 함.** "목적지만 필요하니 dst 를 push(또는 처음 완화)할 때 멈추자" — 그 값은 아직 추정치다. 손 계산 그래프에서 dst = 1 이면 처음 완화 값은 4 이고 답은 3. 고침: dst 가 **pop 될 때** 멈춘다 (연습 1, 정리 그대로).

**6. 경로 복원에서 도달 불가·뒤집기 누락.** `while (v != src) v = prev[v];` 는 v 가 도달 불가면 `prev[v] = -1` 로 배열 밖을 읽는다. 결과를 뒤집지 않으면 dst→src 순서. 고침: `prev[dst] == NONE && dst != src` 먼저, 길이 상한(nv) 으로 사이클 방어, 마지막에 뒤집기.

**7. 무방향 링크를 간선 하나로만 추가.** 메시 링크는 양방향인데 `add_edge(u, v)` 만 하면 반대 방향 경로가 사라진다. `graph_add_link` 처럼 두 개를 넣고, `MAX_E` 를 링크 수 × 2 로 잡는다.

## 8. 면접 Q&A

<details><summary>Q1. 왜 음수 간선이 있으면 안 되나요? 정확히 증명의 어느 줄이 깨지죠?</summary>

증명은 "최단 경로 위에서 S 밖의 첫 정점 y 는 `δ(y) <= δ(u)`" 를 쓴다 — y 이후 부분 경로의 가중치 합이 음이 아니어야 한다. 음수 간선이 그 부분에 있으면 `δ(y) > δ(u)` 가 가능하고, u 를 먼저 확정한 것이 틀린다 (Part B 반례: y = b, u = a, δ(b) = 3 > δ(a) = 1). 대안: 벨만-포드 O(VE) (음수 사이클 검출 포함), 모든 쌍이면 플로이드-워셜, 여러 번 질의하면 벨만-포드로 퍼텐셜 h 를 구해 `w'(u,v) = w + h(u) − h(v) >= 0` 로 바꾼 뒤 다익스트라 (Johnson) — 모두 ALGO-A02. 꼬리: "상수를 더하면?" → 홉 수에 비례해 늘어나 순위가 바뀐다 (5. 풀이 끝).

</details>

<details><summary>Q2. decrease-key 가 꼭 필요한가요? 펌웨어라면 어느 쪽을 고르죠?</summary>

필요 없다. lazy 는 중복 push + stale 버리기로 같은 답, O(E log V) 시간. 차이는 **PQ 크기**: indexed ≤ V, lazy ≤ 1 + E. 실측: 희소 그래프에서는 lazy 가 오히려 빨랐고(30 vs 49 us), 적대적 DAG 에서는 lazy 힙 243952 엔트리·17.4 ms vs indexed 699 엔트리·0.63 ms. 펌웨어는 최악 크기로 정적 할당해야 하므로 "PQ ≤ V" 가 결정적이다 — 정점 64개, 간선 512개 메시면 PQ 64 × 2 B + pos 64 × 2 B 대 lazy 513 × 8 B. 데스크톱 면접 코드(C++ `std::priority_queue` 에 decrease-key 가 없다)는 lazy 가 표준. 꼬리: "Fibonacci 힙?" → decrease-key 상각 O(1) 로 O(E + V log V) 지만 상수가 크고 **상각(amortized)** 이라 개별 연산의 최악이 길다 — 실시간 시스템에서는 이진/4-ary 힙이 보통 낫다 (DS-M11 Q3).

</details>

<details><summary>Q3. 정점 50개짜리 MCU 라우팅이면 힙이 필요한가요?</summary>

아니다 — O(V²) 배열판(매번 미확정 정점 중 최소를 선형 탐색)이 좋은 선택일 수 있다. PQ 메모리 0, 코드가 가장 짧고, 실행 시간이 입력과 거의 무관하게 V² 로 고정돼 **최악을 예측하기 쉽다**. 조밀 그래프(E ≈ V²)에서는 점근적으로도 힙판(O(V² log V))보다 낫다. 실측 완전 그래프 V = 300: array 232 us, indexed 249 us, lazy 247 us — 같은 급. V = 1000 희소~중간(E = 4V, 16V) 에서는 array 가 약 10~47배 느렸다. 판단 기준: E 가 V²/log V 보다 크거나, V 가 수십 이하이고 결정적 실행 시간이 중요하면 배열판.

</details>

<details><summary>Q4. 가중치가 작은 정수(0/1, 또는 1~10 ms)면 더 빠른 방법이 있나요?</summary>

있다. 가중치가 0/1 이면 **0-1 BFS**: 덱(deque)에서 0 간선은 앞에, 1 간선은 뒤에 넣으면 O(V + E). 모든 가중치가 1 이면 그냥 BFS (ALGO-M04). 최대 가중치 C 가 작으면 **Dial 의 버킷 큐**: 거리 값별 버킷 C+1 개를 원형으로 돌려 O(E + V·C) — 힙 비교가 없어 MCU 에서 결정적이다. 꼬리: "왜 BFS 가 가중치 1 에서 다익스트라와 같나?" → FIFO 큐 안의 거리가 항상 `d, d, …, d+1, d+1` 두 값뿐이라 FIFO 순서가 곧 최소 순서다 (단조 큐).

</details>

<details><summary>Q5. 참조 구현 없이 다익스트라가 맞는지 어떻게 테스트하나요?</summary>

결과를 **증명서로 검사**한다 (Part B `spt_verify`): `dist[src] = 0`, 모든 간선에서 `dist[v] <= dist[u] + w`, 유한한 정점마다 꼭 맞는 prev 간선이 있고 prev 사슬이 src 에 닿는다. 이 셋이면 `dist = δ` 가 증명된다 (5. 풀이). O(V + E) 라 알고리즘보다 싸서 디버그 빌드 assert 나 현장 자가 진단에 쓸 수 있다. 추가로: 작은 그래프는 플로이드-워셜/전수 열거와 대조, 카운터 항등식(`pushes + decreases == 1 + relax`), 랜덤 + 고정 시드 + 0 가중치·자기 루프·중복 간선·도달 불가를 일부러 넣는다. 꼬리: "검증기가 틀리면?" → 변조한 답(dist ±1, INF 바꾸기, 0 가중치 사이클 prev)을 넣어 **거부하는지**도 테스트한다 (550개, 오수락 0).

</details>

<details><summary>Q6. 실제 라우팅 프로토콜도 이렇게 하나요?</summary>

링크 상태(link-state) 프로토콜이 그렇다: OSPF 는 각 라우터가 전체 토폴로지(LSA)를 모은 뒤 자기 자신을 출발점으로 최단 경로 트리를 계산해(RFC 2328 16장, 다익스트라) next hop 을 뽑는다 — 이 레슨의 Part C 와 같은 모양이다. 거리 벡터(distance-vector) 프로토콜(RIP)은 이웃끼리 거리표를 주고받는 분산 벨만-포드에 가깝다. 메시/드론 네트워크용 OLSR 같은 프로토콜도 링크 상태 계열로 알려져 있다 (확인 권장). 실제 시스템의 추가 이슈: 링크 지연 측정의 잡음(평활화·히스테리시스로 경로 떨림 방지), 혼잡을 모르는 최단 경로가 한 링크로 트래픽을 몰기(Part C 장애 후 RLY_S), 동점 다중 경로(ECMP).

</details>

<details><summary>Q7. 32비트 MCU 에서 dist 타입과 오버플로는 어떻게 정하죠?</summary>

최장 단순 경로의 상한 = (V − 1) × w_max. V = 256, w_max = 65535 ms 면 약 1.67 × 10^7 — `int32_t` 로 여유가 크다. w 를 µs 단위로 바꾸면 w_max ≈ 6.5 × 10^7 × 255 > 2^31 → `uint32_t` 로도 부족할 수 있으니 계산해서 고른다. INF 는 "어떤 실제 거리보다 크다" 만 보장하면 되고, `INF + w` 를 아예 계산하지 않게(유한한 u 에서만 완화) 짜면 `INT32_MAX` 를 써도 안전하다. 부호 없는 타입이면 오버플로는 UB 가 아니라 wrap (C11 6.2.5p9) 이지만, wrap 된 작은 값이 "더 짧은 경로" 로 보이는 **조용한 오답**이 된다 — UB 보다 나을 게 없다. 정점 id 는 `int16_t`/`uint8_t` 로 줄여 RAM 을 아끼되, 루프 변수는 `int` 로 두어 Cortex-M 에서 불필요한 부호 확장/잘라내기 명령을 피한다.

</details>

<details><summary>Q8. 목적지가 하나뿐이면 더 빨리 끝낼 수 있나요? A* 는요?</summary>

dst 가 **pop** 되는 순간 멈추면 된다 (정리: pop = 최종). 연습 1 실측: 랜덤 그래프에서 pop 수가 전체 실행의 약 50%. 더 줄이려면 (1) 양방향 다익스트라 — 양쪽에서 확장하다 만나는 지점에서 멈춤 (종료 조건이 미묘: 처음 만난 정점이 아니라 `top_f + top_b >= best` 일 때), (2) A* — 목적지까지 하한 추정 h(v) 를 키에 더한다. h 가 일관적(consistent: `h(u) <= w(u,v) + h(v)`)이면 A* 는 가중치를 `w + h(v) − h(u) >= 0` 로 바꾼 그래프의 다익스트라와 같다 — ALGO-A01 에서 격자 휴리스틱으로. 꼬리: "h = 0 이면?" → 그냥 다익스트라.

</details>

## 9. 추가 연습

### 연습 1 — 목적지 하나: 조기 종료 (LeetCode 743 Network Delay Time 의 단일 목적지판)

`dijkstra_to(g, src, dst, &pops)`: dst 까지 최소 지연, 도달 불가 INF. 전체 실행과 랜덤 그래프의 모든 (src, dst) 로 대조하고 pop 수를 비교하라.

<details><summary>답 보기</summary>

lazy 루프에서 stale 이 아닌 엔트리를 pop 했는데 그것이 dst 면 즉시 반환. 정리에 의해 그 값은 최종이다. 실수 5 처럼 완화 시점에 멈추면 틀린다. 실제 출력:

```text
연습 1: 9854 queries, mismatches=0, pops early/full = 151322/302467 (50%)
```

목적지가 출발점에서 무작위 순위에 있으니 평균 절반 — 기대대로다. (LeetCode 743 자체는 "모든 노드에 신호가 닿는 시간 = max dist, 하나라도 INF 면 −1" 로, 본문 Part A 결과의 최댓값이다.)

</details>

### 연습 2 — 최단 경로의 개수 (LeetCode 1976 Number of Ways to Arrive at Destination)

모든 w ≥ 1 일 때 각 정점까지 최단 경로가 몇 개인지 센다. 경로 지연이 같은 다중 경로(ECMP) 후보 수다. V ≤ 8 랜덤 그래프에서 단순 경로 전수 DFS 와 대조, 다이아몬드 10개 사슬에서 2^10 = 1024 확인.

<details><summary>답 보기</summary>

완화에서 `nd < dist[v]` 면 `cnt[v] = cnt[u]`, `nd == dist[v]` 면 `cnt[v] += cnt[u]`. u 가 확정될 때 `cnt[u]` 도 최종인 이유: u 로 들어오는 최단 경로의 직전 정점 p 는 `dist[p] = dist[u] − w < dist[u]` (**w ≥ 1 이 여기서 필요**) 라서 u 보다 먼저 pop 되어 이미 기여를 끝냈다. w = 0 이 있으면 같은 dist 의 정점끼리 순서가 정해지지 않아 틀릴 수 있고, 0 가중치 사이클이면 개수가 무한이다. 개수는 지수적으로 커지므로 (다이아몬드 k 개 → 2^k) 실제 문제는 mod 10^9+7 을 요구한다; 여기서는 `uint64_t` 로 V ≤ 8 · 사슬 10개라 넘치지 않는다. 실제 출력:

```text
연습 2: 400 random graphs (V<=8, w 1..3) vs simple-path enumeration, mismatches=0
```

</details>

### 연습 3 — 최대 대역폭 경로 (widest / bottleneck path)

링크마다 용량(kbps)이 있을 때, 경로의 대역폭 = 경로 위 최소 용량. src 에서 각 정점까지 **대역폭이 최대인** 경로의 값을 구하라. 참조: 용량 임계값 c 마다 "용량 ≥ c 링크만" 으로 BFS 해서 닿는 최대 c. (비슷한 minimax 문제: LeetCode 1631 Path With Minimum Effort.)

<details><summary>답 보기</summary>

다익스트라의 (min, +) 를 (max, min) 으로 바꾼다: 미확정 중 width 가 **최대**인 정점을 확정하고, `min(width[u], cap)` 으로 이웃을 올린다. 맞는 이유는 같은 교환 논증 — 경로를 늘리면 bottleneck 은 **줄어들기만** 한다(단조성)는 것이 "음 아닌 가중치" 의 역할을 한다. 답은 O(V²) 배열판으로 짰다 (V 가 작은 링크 계획용). 실제 출력:

```text
연습 3: 300 random graphs vs threshold-BFS reference, mismatches=0
```

이름 붙은 예: 0→1 100, 1→3 20, 0→2 50, 2→3 40 → 3 까지 40 (0→2→3). 지연 합 최소와 대역폭 최대는 다른 경로를 고를 수 있다 — 실제 라우팅은 둘을 섞은 비용을 쓴다.

</details>

### 연습 4 — 최대 K 홉 (LeetCode 787 Cheapest Flights Within K Stops 형태)

중계 홉 수에 제한이 있다(각 홉이 큐 지연·패킷 손실 위험을 더한다). 간선을 최대 K 개 쓰는 최소 지연을 구하라. 참조: 홉 층별 DP.

<details><summary>답 보기</summary>

상태를 (정점, 사용한 홉 수) 로 늘린 그래프에서 다익스트라: `best[v][h]`, 엔트리 `(d, v, h)`, `h == K` 면 확장하지 않음, dst 의 아무 상태가 처음 확정되면 반환. 상태 수 V(K+1), 시간 O(E(K+1) log(V(K+1))). **함정**: dist 만 키로 하는 보통 다익스트라에 "홉 > K 면 버림" 만 붙이면 틀린다 — 싸지만 홉이 많은 경로가 `dist[v]` 를 차지해서 비싸지만 홉이 적은 경로를 지워 버린다. 예: K = 2, 0→1 10, 0→2 1, 2→1 1, 1→3 1. dist[1] = 2 (2 홉) 가 남아 1→3 은 3 홉이라 버려지고, 0→1→3 = 11 (2 홉) 을 못 찾는다 (이 예는 practice.c 에 CHECK 로 들어 있다: 상태판 11, K = 3 이면 3). 참조 DP 는 r 번째 층에서 **이전 층 복사본**으로만 완화해야 같은 층에서 연쇄되지 않는다 (벨만-포드의 K 회 버전). 실제 출력:

```text
연습 4: 500 random (graph, K) cases vs hop-layer DP, mismatches=0 (unreachable within K: 225)
```

</details>

### 연습 5 — 가장 가까운 게이트웨이 (다중 출발점)

메시에 위성 게이트웨이가 여러 개 있다. 각 노드에 대해 가장 가까운 게이트웨이까지의 지연과 그 게이트웨이를 구하라. 게이트웨이별로 따로 돌린 결과의 최솟값과 대조.

<details><summary>답 보기</summary>

가상 출발점 S* 에서 모든 게이트웨이로 가중치 0 간선을 둔 것과 같다 — 실제로는 모든 게이트웨이를 dist 0 으로 큐에 넣고 시작하면 된다. 한 번의 실행 O((V+E) log V) 로 게이트웨이 수 k 배를 아낀다. `owner[v] = owner[prev]` 로 소속을 전파 (동점이면 어느 게이트웨이든 정답 — 테스트는 "owner 의 단독 실행 거리 == dist" 로 확인). 게이트웨이 → 노드가 아니라 노드 → 게이트웨이(업링크) 지연이 필요하면 **역방향 그래프**에서 같은 일을 한다. 실제 출력:

```text
연습 5: 300 random graphs, multi-source vs min of per-gateway runs, mismatches=0
```

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 11/11, 보고 없음
```

## 10. 핵심 정리

- 다익스트라 = 가까운 순서로 확정. 불변식: **pop 된 정점의 dist 는 최종** (귀류법, 음 아닌 가중치는 `δ(y) <= δ(u)` 한 줄에서만 쓰인다).
- 확정은 pop 때 (`done[]`), 완화는 `nd < dist[v]` 일 때만, 조기 종료도 dst 가 **pop** 될 때.
- indexed PQ: push ≤ V, decrease ≤ E, PQ ≤ V → O((V+E) log V). lazy: PQ ≤ 1 + E, stale 버리기 — 실측 적대적 DAG 에서 힙 243952 엔트리, 17.4 ms vs 0.63 ms.
- 조밀하거나 V 가 작으면 O(V²) 배열판 (완전 그래프 V=300 에서 힙판과 같은 급, PQ 메모리 0, 결정적 시간).
- 경로 복원: prev 를 dst 부터 따라가 뒤집기. prev 간선은 모두 꼭 맞고(tight) 사이클이 없다. 라우터는 next hop 만 — prev 트리에서 O(V).
- 테스트는 증명서로: `dist[src]=0` + 모든 간선 삼각 부등식 + 꼭 맞는 prev 사슬이 src 에 닿음 ⇒ dist = δ. O(V+E).
- 음수 간선 → 다익스트라는 조용히 틀린다 (반례 a=2 vs 1). 전제를 O(E) 로 검사하고, 필요하면 벨만-포드/Johnson (ALGO-A02). 상수 더하기는 오답.
- 펌웨어: MAX_V/MAX_E 정적 배열, id 는 int16, 최장 경로 (V−1)·w_max 로 dist 폭 결정, INF 에서 완화하지 않기.

## 참고자료

- AlgoMonster 노트 M07 §5 (Shortest Path), §22 (Dijkstra's Algorithm | Shortest Path in a Weighted Graph) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M07_graph.md`
- E. W. Dijkstra, "A note on two problems in connexion with graphs", *Numerische Mathematik* 1, 269–271 (1959).
- T. H. Cormen et al., *Introduction to Algorithms* — 3판 24장 (24.3 Dijkstra, 24.5 최단 경로 성질: 상한·경로 완화·선행자 부분 그래프), 4판은 22장.
- M. L. Fredman, R. E. Tarjan, "Fibonacci heaps and their uses in improved network optimization algorithms", *JACM* 34(3), 1987 — O(E + V log V).
- D. B. Johnson, "A note on Dijkstra's shortest path algorithm", *JACM* 20(3), 1973 — 음수 간선에서 재개방하는 변형의 지수적 최악 (확인 권장).
- R. B. Dial, "Algorithm 360: Shortest-path forest with topological ordering", *CACM* 12(11), 1969 — 버킷 큐 (Q4).
- RFC 2328 *OSPF Version 2*, 16장 "Calculation of the routing table" — 링크 상태 라우팅의 최단 경로 트리 계산: <https://www.rfc-editor.org/rfc/rfc2328>
- ISO/IEC 9899:2011 (C11) — 6.5p5 (부호 있는 오버플로 UB), 6.2.5p9 (부호 없는 wrap).
- LeetCode 743 Network Delay Time · 1976 Number of Ways to Arrive at Destination · 787 Cheapest Flights Within K Stops · 1631 Path With Minimum Effort.
- 관련 레슨: DS-B12 (이진 힙), DS-M11 (indexed PQ, decrease-key vs lazy 벤치), ALGO-M04 (BFS 격자 — 가중치 없는 최단 경로), ALGO-M05 (위상 정렬 — DAG 최단 경로는 O(V+E)), ALGO-M14 (Union-Find·Kruskal), ALGO-A01 (A*), ALGO-A02 (벨만-포드·플로이드-워셜·음수 사이클).
