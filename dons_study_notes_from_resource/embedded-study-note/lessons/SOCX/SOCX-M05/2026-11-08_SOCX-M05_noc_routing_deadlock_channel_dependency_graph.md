---
id: SOCX-M05
time: 90분
updated: 2026-11-08
---
# SOCX-M05 NoC 라우팅 데드락: 채널 의존 그래프

## 학습 목표

- 채널(channel = 방향 링크 × 가상 채널 VC)과 라우팅 함수 R로 **채널 의존 그래프(CDG, channel dependency graph)** 를 만들고, "어떤 패킷이 c1을 잡은 채 c2를 요청할 수 있다"는 정의를 도달 가능한 상태만으로 구현할 수 있다.
- **Dally–Seitz 정리**(결정적 라우팅: CDG 비순환 ⇔ 데드락 자유)를 정확히 말하고, 증명 아이디어(채널 번호가 경로를 따라 엄격히 증가)를 mesh XY로 보일 수 있다. 적응 라우팅에는 **Duato 정리**가 따로 있다는 것도 안다.
- mesh XY는 비순환, 무제한 최소 경로(완전 적응)와 torus 기본 DOR은 순환, **dateline VC 2개**로 torus 사이클이 사라지는 것을 도구 출력으로 보일 수 있다.
- 사이클 단위 wormhole 시뮬레이터에서 심어 둔 트래픽으로 **실제 데드락을 재현**하고, 대기 그래프(wait-for graph)에서 순환 대기를 찾아 보고한 뒤, 같은 트래픽이 dateline VC로 끝나는 것을 확인할 수 있다.
- 같은 "순환 대기"가 OS 락·DB·네트워크에서 어떻게 다시 나오는지 연결하고, Coffman 4조건 중 NoC 해법이 각각 무엇을 깨는지 말할 수 있다.

## 1. 문제

SoC 인터커넥트(ARM CMN 같은 mesh, AI 가속기의 on-chip NoC, TPU 계열의 torus 칩 간 링크)는 한 번 데드락이 나면 칩 전체가 멈추고 리셋 말고는 답이 없다 — 그래서 아키텍처·검증 면접에서 "torus에서 DOR이 왜 데드락이 나고 VC 몇 개로 어떻게 막나요?"가 단골이다. 동시성 가이드 6층의 결론은 하나다: **소프트웨어 락의 순환 대기와 NoC 버퍼의 순환 대기는 같은 그래프 문제다.**

NoC 기본 용어부터 맞추자.

| 용어 | 뜻 | 이 레슨의 코드 |
|---|---|---|
| wormhole | 패킷을 flit(flow control unit)으로 쪼개 head flit이 길을 열고 body/tail이 따라감. 패킷 전체를 담을 버퍼가 없어도 됨 | `WormholeSim` |
| 채널(channel) | 방향 링크 하나의 VC 하나 = 하류 라우터의 입력 VC 버퍼 하나 | `Channel(src, dst, dir, vc)` |
| VC(virtual channel) | 한 물리 링크를 나눠 쓰는 독립 버퍼(큐). 대역폭은 공유, 버퍼·소유권은 따로 | `Topo(nvc=2)` |
| credit / backpressure | 하류 버퍼 빈 칸만큼만 보낸다. 빈 칸이 없으면 상류가 멈춤 → 손실 없음(lossless) | `space[o]` |
| 라우팅 함수 R | (출발, 현재 노드, 목적지, 들어온 채널) → 다음 채널 후보 | `route_*()` |
| DOR(dimension-order routing) | X를 다 간 뒤 Y. mesh에서는 XY 라우팅 | `route_xy`, `route_torus_dor` |

wormhole의 핵심 성질: head가 다음 채널을 못 얻으면 **이미 잡은 채널들을 놓지 않고 기다린다** (body flit이 그 버퍼들에 걸쳐 있으니까). 아래 그림이 Part C에서 재현하는 바로 그 상황이다.

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m1a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">4-노드 ring, VC 1개: 모든 노드가 2칸 앞으로 4-flit 패킷을 동시에 보냄</text>
  <rect x="70" y="50" width="60" height="40" class="f-mut" stroke="currentColor"/><text x="88" y="75" fill="currentColor">R0</text>
  <rect x="390" y="50" width="60" height="40" class="f-mut" stroke="currentColor"/><text x="408" y="75" fill="currentColor">R1</text>
  <rect x="390" y="210" width="60" height="40" class="f-mut" stroke="currentColor"/><text x="408" y="235" fill="currentColor">R2</text>
  <rect x="70" y="210" width="60" height="40" class="f-mut" stroke="currentColor"/><text x="88" y="235" fill="currentColor">R3</text>
  <line x1="130" y1="70" x2="388" y2="70" stroke="currentColor" stroke-width="2" marker-end="url(#m1a)"/>
  <line x1="420" y1="90" x2="420" y2="208" stroke="currentColor" stroke-width="2" marker-end="url(#m1a)"/>
  <line x1="390" y1="230" x2="132" y2="230" stroke="currentColor" stroke-width="2" marker-end="url(#m1a)"/>
  <line x1="100" y1="210" x2="100" y2="92" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" marker-end="url(#m1a)"/>
  <rect x="330" y="58" width="22" height="24" class="f-acc" stroke="currentColor"/><rect x="354" y="58" width="22" height="24" class="f-acc" stroke="currentColor"/>
  <text x="333" y="75" fill="currentColor" font-size="12">P0</text><text x="357" y="75" fill="currentColor" font-size="12">P0</text>
  <rect x="408" y="150" width="24" height="22" class="f-acc" stroke="currentColor"/><rect x="408" y="174" width="24" height="22" class="f-acc" stroke="currentColor"/>
  <text x="410" y="166" fill="currentColor" font-size="12">P1</text><text x="410" y="190" fill="currentColor" font-size="12">P1</text>
  <rect x="140" y="218" width="22" height="24" class="f-acc" stroke="currentColor"/><rect x="164" y="218" width="22" height="24" class="f-acc" stroke="currentColor"/>
  <text x="143" y="235" fill="currentColor" font-size="12">P2</text><text x="167" y="235" fill="currentColor" font-size="12">P2</text>
  <rect x="88" y="100" width="24" height="22" class="f-acc" stroke="currentColor"/><rect x="88" y="124" width="24" height="22" class="f-acc" stroke="currentColor"/>
  <text x="90" y="116" fill="currentColor" font-size="12">P3</text><text x="90" y="140" fill="currentColor" font-size="12">P3</text>
  <text x="200" y="62" fill="currentColor">ch 0→1 (P0 소유)</text>
  <text x="430" y="130" fill="currentColor">ch 1→2</text>
  <text x="200" y="262" fill="currentColor">ch 2→3 (P2 소유)</text>
  <text x="14" y="170" fill="currentColor">wrap</text><text x="14" y="186" fill="currentColor">3→0</text>
  <text x="490" y="60" fill="currentColor" font-weight="bold">각 head flit이 기다리는 것</text>
  <text x="490" y="84" fill="currentColor">P0 (0→2): 1→2 요청 → P1 소유</text>
  <text x="490" y="106" fill="currentColor">P1 (1→3): 2→3 요청 → P2 소유</text>
  <text x="490" y="128" fill="currentColor">P2 (2→0): 3→0 요청 → P3 소유</text>
  <text x="490" y="150" fill="currentColor">P3 (3→1): 0→1 요청 → P0 소유</text>
  <rect x="486" y="168" width="226" height="54" class="f-warn" stroke="currentColor"/>
  <text x="494" y="190" fill="currentColor">버퍼 2칸이 꽉 참, 나머지 2 flit은</text>
  <text x="494" y="210" fill="currentColor">소스 큐에 → 아무도 못 움직임</text>
  <text x="490" y="250" fill="currentColor">= 순환 대기 (circular wait)</text>
  <text x="490" y="272" fill="currentColor">버퍼를 키워도 VC 소유는 그대로</text>
</svg>
```

**같은 순환 대기, 다른 층.** 6층의 일곱 레슨은 모두 "자원 → 자원" 그래프의 사이클을 다룬다. 노드와 간선이 무엇인지만 다르다.

| 레슨 | 자원(그래프 노드) | 간선 "A를 잡고 B를 기다림" | 사이클을 없애는 대표 방법 |
|---|---|---|---|
| OS-M10 | 포크(뮤텍스) | 철학자가 왼쪽 포크를 쥐고 오른쪽을 기다림 | 전역 락 순서 (번호 작은 포크 먼저) |
| OS-M11 | 락 클래스 | 런타임에 관찰한 획득 순서 held→new | lockdep: 새 간선마다 역방향 도달성 검사 |
| ALGO-A10 | 락/스레드 | 의존 그래프 일반 | Tarjan/Kosaraju SCC로 모든 순환 영역 |
| STOR-M08 | 트랜잭션 | wait-for (T1이 T2의 락을 기다림) | 탐지 후 희생자 abort |
| STOR-M10 | B+tree 노드 래치 | 부모 래치를 쥔 채 자식 래치 | 위→아래 한 방향 crabbing (= 순서) |
| **SOCX-M05** | **채널(링크 × VC)** | **패킷이 c1을 잡은 채 c2를 요청** | **DOR · 턴 모델 · dateline VC** |
| DCN-M05 | (스위치, 입력 포트, 우선순위) 버퍼 | PFC PAUSE 전파 | up-down 라우팅, 홉마다 우선순위 올리기, 워치독 |

특히 **CDG의 위상 순서(topological order)는 곧 전역 락 순서**다. XY 라우팅은 "X 채널을 다 잡은 뒤 Y 채널"이라는 락 순서를 하드웨어가 강제하는 것과 같다 (OS-M10의 "번호 작은 포크 먼저"와 같은 아이디어).

### Part A — 토폴로지 · 라우팅 함수 → CDG, DFS 사이클 검사, mesh XY 비순환

1. `route_xy(topo, src, cur, dst, in_ch)`: mesh XY. 후보는 항상 1개.
2. `build_cdg(topo, route)`: 모든 (src, dst) 쌍에 대해 패킷이 실제로 지나갈 수 있는 채널만 따라가며 간선 c1 → c2를 모은다. 라우팅이 막다른 곳(후보 0개)에 빠지면 `ValueError`.
3. `find_cycle(g)`: 3색 DFS(명시적 스택)로 사이클 하나를 반환, 없으면 `None`. `topo_order(g)`: Kahn 알고리즘.
4. 4×4 mesh XY: 채널 48개, 간선 수가 공식 `2·2·k·(k−2) + 4·(k−1)²` = 68과 같고, 사이클 없음, 위상 순서에서 모든 간선이 앞으로 간다.

### Part B — 순환을 만들고 깨기: 완전 적응 · torus DOR · dateline

1. `route_minimal_adaptive`: 거리를 줄이는 방향 전부 → 사이클 출력. 단위 정사각형을 도는 E→N→W→S 턴 사이클이 들어 있다.
2. 라우팅을 무시한 naive CDG(주어짐)는 XY 경로가 쓰지도 않는 간선 때문에 순환이다 — **CDG는 라우팅 함수의 성질**이다.
3. `route_torus_dor`: 링마다 최단 방향(동률 k/2이면 +). k=4에서는 E·N 링 8개만 순환, k=5에서는 20개 모두 순환.
4. `route_torus_dateline`: VC 2개. wrap 링크를 건널 때 VC1, 그 차원이 끝날 때까지 VC1 유지, 차원이 바뀌면 VC0. → 비순환, wrap 링크의 VC0 16개는 아예 안 쓰인다.

### Part C — wormhole 시뮬레이터에서 실제 데드락 재현 · 보고 · 해소

1. `WormholeSim.wait_for_graph()`: 막힌 VC 버퍼 → 그 앞 flit이 기다리는 VC 버퍼. 시뮬레이터가 이 그래프에 `find_cycle`을 돌려 순환 대기를 보고한다.
2. 4-노드 ring, VC 1개, 모든 노드가 i → i+2로 4-flit 패킷(버퍼 2칸): 데드락, 순환 대기는 E 채널 4개, 이후 500사이클 동안 이동 0 (영구적).
3. 같은 트래픽이 dateline VC 2개로 16/16 도착. 4×4 torus에서도 64/64. 버퍼를 8칸(패킷보다 큼)으로 늘려도 VC 1개면 여전히 데드락.

## 2. Problem (English)

You are given a 2-D network (k×k mesh or k-ary 2-cube torus, optionally a 1-D ring) whose *channels* are
directed links × virtual channels. A routing function returns the candidate next channels:

```python
Channel = namedtuple("Channel", "src dst dir vc")
def route(topo, src, cur, dst, in_ch) -> list[Channel]   # in_ch is None at injection; cur != dst
def build_cdg(topo, route) -> dict[Channel, set[Channel]]
def find_cycle(g) -> list | None          # [v0, ..., vm-1], edges vi -> vi+1 and vm-1 -> v0
def topo_order(g) -> list | None
class WormholeSim:
    def wait_for_graph(self) -> dict[Channel, set[Channel]]
```

1. Build the channel dependency graph: edge `c1 -> c2` iff some packet (some src, dst) can occupy `c1` and
   then request `c2`. Only follow states a packet can actually reach.
2. Show: XY on a 4×4 mesh is acyclic (print a topological order, 68 edges); minimal fully-adaptive routing
   without VCs is cyclic; dimension-order routing on a 4×4 torus is cyclic on every ring it uses; the
   two-VC dateline scheme is acyclic.
3. In a cycle-accurate wormhole simulator (per-VC input buffers, credit flow control, VC held from head to
   tail), reproduce a deadlock with a planted pattern, report the circular wait of buffers, and show that the
   same traffic completes with dateline VCs. The simulator must terminate (bounded cycles) even when deadlocked.

Example: ring k=4, 1 VC, node i sends 4 flits to i+2, buffer depth 2 →
`DEADLOCK @cycle 3 (0/16 delivered): (0,0)->(1,0).v0[P0] -> (1,0)->(2,0).v0[P1] -> ... -> back`.
Constraints: Python 3.9 stdlib only, deterministic, every run well under a second.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/SOCX/SOCX-M05
python3 -I starter.py ; echo "exit=$?"     # 0/29 passed, exit=1
```

토폴로지(`Topo`, `ring_dir`, `is_wrap`), naive CDG, 시뮬레이터의 라우팅·VC 할당·스위치·credit 동작, 하네스는 주어져 있다. TODO는 8곳: `route_xy`, `build_cdg`, `find_cycle`, `topo_order`, `route_minimal_adaptive`, `route_torus_dor`, `route_torus_dateline`, `WormholeSim.wait_for_graph`. 미구현 함수는 `NotImplementedError`를 던지고 하네스가 FAIL로 센다 — 시뮬레이터도 `max_cycles`(기본 5000)로 끝나므로 starter가 멈추는 일은 없다.

```text
-- Part A: XY on 4x4 mesh --
FAIL  XY (0,0)->(2,1) first hop = E  (NotImplementedError: route_xy)
...
FAIL  mesh XY, (x+1,y) for x<3 wraps to x=0 (westbound): completes
0/29 passed
exit=1
```

## 4. 힌트

<details><summary>힌트 1 — CDG 간선은 "토폴로지"가 아니라 "패킷"이 만든다</summary>

노드 n으로 들어오는 채널과 나가는 채널을 모두 이으면(naive) 거의 항상 사이클이 생긴다. 간선 c1 → c2는 "목적지 d로 가는 어떤 패킷이 c1에 있고, R이 다음으로 c2를 줄 수 있다"일 때만 있다. 그러니 (s, d)마다 경로를 따라가는 DFS/BFS를 하고, 패킷이 실제로 서 있을 수 있는 채널에서만 R을 호출한다. 목적지에 도착한 채널에서는 R을 부르지 않는다 (소비).

</details>

<details><summary>힌트 2 — 사이클 찾기: visited 하나로는 부족하다</summary>

"이미 방문한 정점을 다시 만나면 사이클"은 틀렸다: a→b→d, a→c→d (다이아몬드)에서 d를 두 번 만나지만 사이클이 아니다. 색 3개가 필요하다. GRAY = 지금 DFS 스택 위(조상), BLACK = 다 끝남. **GRAY로 가는 간선(back edge)** 만 사이클이다. 사이클 경로는 스택 `path`에서 그 GRAY 정점 위치부터 끝까지.

</details>

<details><summary>힌트 3 — torus DOR에서 사이클은 링 안에 있다</summary>

DOR은 X → Y만 허용하므로 Y 채널에서 X 채널로 가는 간선이 없다. 사이클이 있다면 한 차원의 한 방향 링 안이다. k=4에서 (b−a) mod 4 = 2인 동률을 +로 보내면 + 방향은 2홉 경로가 있어 링의 모든 연속 채널 쌍이 간선이 되고, − 방향은 1홉뿐이라 간선이 안 생긴다.

</details>

<details><summary>힌트 4 — dateline: "건너는 순간 VC1, 그 차원 끝까지 VC1"</summary>

`route_torus_dor`로 방향을 정한 뒤 VC만 고른다. (1) 지금 hop이 wrap 링크면 VC1. (2) 같은 축에서 VC1으로 들어왔으면 VC1 유지. (3) 그 외 VC0. 차원을 바꾸면 (X→Y) 다시 VC0 — Y 링은 X 링과 따로 순서를 가진다.

</details>

<details><summary>힌트 5 — 대기 그래프의 간선 두 종류</summary>

버퍼 앞 flit이 (a) head인데 VC를 못 잡았으면 후보 VC들(다른 패킷 소유)을 기다리고, (b) 출력 VC가 할당됐는데 하류 버퍼가 꽉 찼으면 그 하류 버퍼를 기다린다. 링크 경합에서 이번 사이클에 진 것은 간선이 아니다 (다음 사이클에 풀린다). 결정적 라우팅에서는 이 그래프의 사이클 = 데드락.

</details>

## 5. 풀이

### Part A — CDG를 정확히 만들기

**정의.** Dally & Seitz(1987)의 모델에서 라우팅 함수는 R: C × N → C (현재 채널, 목적지 → 다음 채널), CDG D는 정점 = 채널, 간선 (cᵢ, cⱼ) ⇔ 어떤 목적지 n에 대해 R(cᵢ, n) = cⱼ. 이 레슨은 출발 노드도 인자로 받는다(odd-even 같은 알고리즘이 출발 열을 쓰기 때문, 추가 연습 2). 그리고 간선을 **도달 가능한 상태에서만** 넣는다 — c₁에 목적지 d 패킷이 실제로 올 수 없는 조합까지 넣으면 거짓 간선이 생긴다 (원 논문의 정의를 기계적으로 적용할 때의 함정. 이 레슨 기준의 해석이다).

```text
build_cdg:
  for s, d in 모든 노드 쌍 (s ≠ d):
      stack = R(s, s, d, None)            ← 주입 시 첫 채널 후보
      while stack:
          c = pop
          if c.dst == d: continue          ← 목적지에서 소비 (consumption assumption)
          for c2 in R(s, c.dst, d, c):     ← 비어 있으면 라우팅이 막힘 → ValueError
              g[c].add(c2); 처음 보면 push
```

비용: (s, d) 쌍 k⁴개 × 경로 길이. 4×4는 즉시, 8×8 torus(채널 512개)를 포함한 practice.py 전체가 이 Mac에서 0.23초(solution.py 0.13초).

**3색 DFS.** 재귀 대신 `(path, iters)` 두 스택을 쓴다. 채널이 수천 개면 파이썬 재귀 한도(기본 1000)에 걸린다.

| 이웃의 색 | 뜻 | 처리 |
|---|---|---|
| WHITE | 처음 봄 | GRAY로 칠하고 push |
| GRAY | 지금 경로의 조상 → back edge | `path[path.index(nxt):]` 반환 = 사이클 |
| BLACK | 이미 끝난 부분 (cross/forward edge) | 무시 — 다이아몬드가 여기에 걸린다 |

**mesh XY 간선 수 68은 손으로 셀 수 있다.** XY에서 가능한 의존은 직진(E→E, W→W, N→N, S→S)과 X→Y 턴(E→N, E→S, W→N, W→S)뿐이다.

| 종류 | 개수 (k=4) | 이유 |
|---|---|---|
| X 직진 | 2방향 × k행 × (k−2) = 16 | 한 행에서 연속한 두 E 채널 쌍은 k−2개 |
| Y 직진 | 16 | 같은 이유 |
| X→Y 턴 | 4종 × (k−1)² = 36 | 예: E→N은 x ≥ 1 (E로 들어올 수 있음), y ≤ k−2 (N으로 나갈 수 있음)인 노드 |
| 합 | **68** | 테스트 `XY CDG edges = 68 (expect 68)` |

```text
PASS  mesh 4x4: 48 channels, XY CDG edges = 68 (expect 68)
PASS  XY CDG has no cycle
PASS  XY topological order covers all 48 channels; every edge goes forward
      order[:6] = (0,0)->(1,0).v0, (0,1)->(1,1).v0, (0,2)->(1,2).v0, (0,3)->(1,3).v0, (1,0)->(2,0).v0, (1,1)->(2,1).v0 ...
```

**Dally–Seitz 정리 (정확히).** "결정적(deterministic) 라우팅 함수 R은 CDG에 사이클이 없을 때, 그리고 그때만 데드락 자유다." 두 방향의 무게가 다르다.

- **⇐ (비순환이면 데드락 없음)** — 이쪽이 설계에 쓰는 방향이다. 증명 아이디어: CDG가 비순환이면 채널에 번호를 매겨 모든 간선에서 번호가 엄격히 증가하게 할 수 있다(위상 순서). 그러면 모든 패킷은 번호가 증가하는 채널만 차례로 잡는다. 데드락이 났다고 하고, 막힌 패킷들이 잡은 채널 중 **번호가 가장 큰** 채널을 보자. 그 패킷이 기다리는 다음 채널은 번호가 더 크니 막힌 패킷 누구도 잡고 있지 않다 → 비어 있거나 결국 비게 되고(목적지는 항상 소비), 그 패킷은 진행한다. 모순. 이것이 OS-M10의 "전역 락 순서면 교착 없음" 증명과 **글자 그대로 같다**. 추가 연습 5에서 XY의 번호 함수를 직접 쓴다 (X 채널 0..k−2, Y 채널 k..2k−2).
- **⇒ (사이클이 있으면 데드락 가능)** — 원 논문의 모델(사이클의 모든 채널을 동시에 채울 수 있는 패킷 집합이 존재)에 기대는 방향이다. 사이클이 있어도 실제로는 채울 수 없는 경우가 있다는 후속 연구가 있다 (예: Schwiebert & Jayasimha의 wormhole 필요충분조건 논문 — 제목·연도는 기억에 의존, 확인 필요). 면접에서는 "비순환이면 확실히 안전, 순환이면 데드락 가능성이 있으니 실제 패턴을 찾아본다"가 안전한 답이다. Part C가 바로 그 "실제 패턴 찾기"다.

### Part B — 사이클이 생기는 세 가지 방식과 dateline

**① 완전 적응 최소 경로 (VC 없음).** 도구가 찾은 사이클:

```text
PASS  fully adaptive minimal mesh CDG has a cycle: (0,1)->(0,0).v0 -> (0,0)->(1,0).v0 -> (1,0)->(1,1).v0 -> (1,1)->(0,1).v0 -> back
PASS  unit-square turn cycle E->N->W->S is in the adaptive CDG
```

단위 정사각형을 반시계로 도는 네 턴(SE, EN, NW, WS)이다. 2D mesh의 90° 턴 8개는 두 개의 **추상 사이클**(반시계 4개, 시계 4개)로 나뉜다. 데드락을 막으려면 **각 추상 사이클에서 턴을 적어도 하나** 금지해야 한다 — Glass & Ni의 턴 모델(turn model). XY는 Y→X 턴 4개를 모두 금지(과잉), west-first·north-last·negative-first는 2개만 금지해서 적응성을 남긴다. 단, 두 개를 아무렇게나 고르면 안 된다. 같은 추상 사이클 안의 두 턴을 금지하면 다른 사이클이 그대로 남는다. 각 사이클에서 하나씩 금지해도(4 × 4 = 16가지) 남은 턴들이 더 긴 **복합 사이클**을 만드는 조합이 있다. 이 레슨의 도구로 4×4 mesh의 턴 그래프(naive CDG에서 금지 턴만 뺀 것)를 직접 세면 **12/16만 비순환**이고, EN+NE, NW+WN, WS+SW, SE+ES 금지는 길이 8–10의 사이클이 남는다 (연습 1의 실제 출력. Glass & Ni 논문의 "16가지 중 12가지" 결론과 같다 — 논문 수치는 기억 기반). 그래서 금지 집합을 정하면 **반드시 CDG로 확인**한다 (연습 1·2).

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m2a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">2D mesh의 90° 턴 8개 = 추상 사이클 2개. 각 사이클에서 최소 1개 금지</text>
  <rect x="60" y="50" width="140" height="140" class="f-mut" stroke="none"/>
  <line x1="70" y1="180" x2="188" y2="180" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="190" y1="178" x2="190" y2="62" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="188" y1="60" x2="72" y2="60" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="70" y1="62" x2="70" y2="176" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <text x="196" y="196" fill="currentColor">EN</text><text x="196" y="56" fill="currentColor">NW</text>
  <text x="40" y="56" fill="currentColor">WS</text><text x="40" y="196" fill="currentColor">SE</text>
  <text x="80" y="128" fill="currentColor">반시계</text>
  <rect x="300" y="50" width="140" height="140" class="f-mut" stroke="none"/>
  <line x1="310" y1="60" x2="428" y2="60" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="430" y1="62" x2="430" y2="178" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="428" y1="180" x2="312" y2="180" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <line x1="310" y1="178" x2="310" y2="64" stroke="currentColor" stroke-width="2" marker-end="url(#m2a)"/>
  <text x="436" y="56" fill="currentColor">ES</text><text x="436" y="196" fill="currentColor">SW</text>
  <text x="280" y="196" fill="currentColor">WN</text><text x="280" y="56" fill="currentColor">NE</text>
  <text x="335" y="128" fill="currentColor">시계</text>
  <rect x="480" y="44" width="232" height="164" class="f-acc" stroke="currentColor"/>
  <text x="490" y="66" fill="currentColor" font-weight="bold">금지하는 턴</text>
  <text x="490" y="90" fill="currentColor">XY: NE NW SE SW (4개)</text>
  <text x="490" y="112" fill="currentColor">west-first: NW SW</text>
  <text x="490" y="134" fill="currentColor">north-last: NE NW</text>
  <text x="490" y="156" fill="currentColor">negative-first: ES NW</text>
  <text x="490" y="178" fill="currentColor">odd-even: 짝수 열 EN ES,</text>
  <text x="490" y="198" fill="currentColor">홀수 열 NW SW</text>
</svg>
```

(턴 이름 "AB" = A 방향으로 오다가 B 방향으로 꺾음.)

**② naive CDG의 거짓 사이클.** 토폴로지만 보고 "들어온 채널 → 나가는 모든 채널(U턴 제외)"로 그리면 간선 104개에 사이클이 있다. 진짜 XY CDG(68개)는 그 부분집합이고 비순환이다. 같은 칩, 같은 링크라도 **라우팅 함수가 데드락 자유성을 결정한다**.

```text
PASS  naive (topology-only) CDG: 104 edges, cyclic; true XY CDG: 68 edges, acyclic
```

**③ torus DOR — 링 자체가 사이클.** mesh XY와 똑같이 X → Y 순서를 지켜도 torus에서는 한 차원 안이 이미 원이다. 링 위 모든 연속 채널 쌍 (i→i+1, i+1→i+2)을 2홉 이상 가는 어떤 패킷이 쓰기 때문이다.

```text
PASS  torus 4x4 DOR CDG has a cycle: (0,0)->(0,1).v0 -> (0,1)->(0,2).v0 -> (0,2)->(0,3).v0 -> (0,3)->(0,0).v0 -> back
PASS  k=4: every E and N ring is cyclic (8), W/S rings are not (ties go +): 8
PASS  k=5: all 20 directed rings cyclic (no ties): 20
```

k=4가 흥미롭다. 거리 2는 동률이라 + 방향으로 보내므로 − 방향은 1홉 경로만 생기고, 1홉 경로는 의존 간선을 만들지 않는다. 그래서 W·S 링 8개는 VC 없이도 비순환이다. k=5에서는 양방향 모두 2홉 경로가 있어 20개 링이 다 순환이다. 이런 "동률 규칙 하나가 사이클 수를 바꾼다"는 점은 손 증명보다 도구가 정확한 이유다.

**dateline.** 각 링에 dateline을 하나 긋는다 (여기서는 wrap 링크). 패킷은 VC0으로 출발하고, dateline을 건너는 순간 VC1으로 옮겨 그 차원이 끝날 때까지 VC1에 머문다. 최소 경로는 한 링에서 dateline을 **최대 한 번** 건너므로 VC0 체인과 VC1 체인은 각각 직선이 되고, VC1 → VC0 간선은 생기지 않는다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">k=4 ring의 CDG (E 방향, cᵢ = i→i+1): VC 1개 vs dateline VC 2개</text>
  <rect x="40" y="50" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="54" y="70" fill="currentColor">c0.v0</text>
  <rect x="200" y="50" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="214" y="70" fill="currentColor">c1.v0</text>
  <rect x="200" y="160" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="214" y="180" fill="currentColor">c2.v0</text>
  <rect x="40" y="160" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="54" y="180" fill="currentColor">c3.v0</text>
  <line x1="110" y1="65" x2="198" y2="65" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="235" y1="80" x2="235" y2="158" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="200" y1="175" x2="112" y2="175" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="75" y1="160" x2="75" y2="82" stroke="currentColor" marker-end="url(#m3a)"/>
  <text x="100" y="128" fill="currentColor">사이클</text>
  <text x="40" y="222" fill="currentColor">1 VC: 0→2, 1→3, 2→0, 3→1 패킷이</text>
  <text x="40" y="240" fill="currentColor">네 간선을 하나씩 만든다</text>
  <line x1="320" y1="40" x2="320" y2="240" stroke="currentColor" stroke-dasharray="4 4"/>
  <rect x="340" y="60" width="62" height="30" class="f-ok" stroke="currentColor"/><text x="350" y="80" fill="currentColor">c0.v0</text>
  <rect x="412" y="60" width="62" height="30" class="f-ok" stroke="currentColor"/><text x="422" y="80" fill="currentColor">c1.v0</text>
  <rect x="484" y="60" width="62" height="30" class="f-ok" stroke="currentColor"/><text x="494" y="80" fill="currentColor">c2.v0</text>
  <rect x="556" y="60" width="62" height="30" class="f-acc" stroke="currentColor"/><text x="566" y="80" fill="currentColor">c3.v1</text>
  <rect x="628" y="60" width="62" height="30" class="f-ok" stroke="currentColor"/><text x="638" y="80" fill="currentColor">c0.v1</text>
  <line x1="402" y1="75" x2="410" y2="75" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="474" y1="75" x2="482" y2="75" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="546" y1="75" x2="554" y2="75" stroke="currentColor" marker-end="url(#m3a)"/>
  <line x1="618" y1="75" x2="626" y2="75" stroke="currentColor" marker-end="url(#m3a)"/>
  <text x="556" y="110" fill="currentColor">wrap = dateline</text>
  <rect x="340" y="140" width="62" height="30" class="f-mut" stroke="currentColor"/><text x="350" y="160" fill="currentColor">c3.v0</text>
  <rect x="412" y="140" width="62" height="30" class="f-mut" stroke="currentColor"/><text x="422" y="160" fill="currentColor">c1.v1</text>
  <rect x="484" y="140" width="62" height="30" class="f-mut" stroke="currentColor"/><text x="494" y="160" fill="currentColor">c2.v1</text>
  <text x="560" y="160" fill="currentColor">안 쓰임</text>
  <text x="340" y="205" fill="currentColor">2 VC: 한 줄 = 위상 순서. VC1 → VC0 간선 없음</text>
  <text x="340" y="225" fill="currentColor">→ 비순환 (Dally–Seitz ⇐ 방향으로 데드락 자유)</text>
</svg>
```

```text
PASS  dateline: crossing (3,0)->(0,0) uses VC1
PASS  dateline: after crossing, stay on VC1
PASS  dateline: X->Y dimension change resets to VC0
PASS  torus 4x4 + dateline 2 VCs: CDG acyclic (104 edges)
PASS  VC0 of all 16 wrap links is never used; 72/128 channels used at all
```

VC를 2배로 늘렸는데 실제로 쓰이는 채널은 128개 중 72개다. dateline 방식의 VC 이용이 불균형하다는 것(VC1은 dateline 직후 몇 홉에만 몰림)은 실제 설계에서 알려진 단점이고, 그래서 dateline 위치를 링마다 바꾸거나 VC를 다르게 나누는 변형들이 있다 (구체적 설계 사례는 제품별로 확인 필요).

**적응 라우팅은 사이클이 있어도 안전할 수 있다 — Duato 정리 (언급만).** 완전 적응 라우팅 R의 CDG가 순환이라도, R 안에 연결된(connected) 부분 라우팅 R₁ — 보통 전용 "escape VC" 위의 DOR — 이 있고 R₁의 **확장(extended)** CDG가 비순환이면 R은 데드락 자유다 (Duato 1993). 패킷은 적응 VC에서 막히면 언제든 escape VC로 내려갈 수 있으므로 escape 네트워크가 끝까지 흐른다. 그래서 "CDG 순환 = 데드락"은 결정적 라우팅에서만 쓰는 말이다.

### Part C — 시뮬레이터에서 실제로 멈추게 하고, 원인을 보고하기

**모델.** 데드락에 필요한 것만 남겼다.

| 요소 | 구현 | 데드락에서의 역할 |
|---|---|---|
| 입력 VC 버퍼 | 채널마다 `depth` 칸 deque | 붙잡히는 자원 |
| VC 소유권 | head가 잡고 tail이 버퍼를 떠날 때 해제 (atomic VC 재할당) | 상호 배제 + 점유 대기 |
| credit | 사이클 시작 때 하류 빈 칸 수 (해제된 칸은 다음 사이클부터 = 1사이클 credit 지연) | lossless → 선점 없음 |
| 스위치 | 물리 링크당 사이클 1 flit, 라운드로빈 | VC끼리 대역폭 공유 |
| ejection | 목적지에서 무조건 소비 | Dally–Seitz의 consumption assumption |
| 탐지 | 한 사이클 이동 0, 또는 어떤 버퍼 앞 flit이 W=16 사이클 넘게 정지 → 대기 그래프 사이클 검사 | 보고 후 종료, `max_cycles` 상한 |

**ring k=4, VC 1개 — 사이클별 추적** (보조 스크립트로 찍은 실제 출력, 버퍼 내용은 패킷 번호):

```text
cycle  0 moved 4 delivered  0 | 0>1.v0:0 1>2.v0:1 2>3.v0:2 3>0.v0:3
cycle  1 moved 4 delivered  0 | 0>1.v0:00 1>2.v0:11 2>3.v0:22 3>0.v0:33
cycle  2 moved 0 delivered  0 | 0>1.v0:00 1>2.v0:11 2>3.v0:22 3>0.v0:33
```

사이클 0에 네 소스가 동시에 첫 채널의 VC를 잡는다. 사이클 1에 head들이 다음 라우터에 도착해 다음 채널을 요청하지만 그 VC는 이웃 패킷이 사이클 0에 이미 잡았다. 두 번째 flit이 들어와 버퍼(2칸)가 차고, 사이클 2부터 아무것도 움직이지 않는다. 시뮬레이터 보고:

```text
      ring k=4, 1 VC : DEADLOCK @cycle 3 (0/16 delivered): (0,0)->(1,0).v0[P0] -> (1,0)->(2,0).v0[P1] -> (2,0)->(3,0).v0[P2] -> (3,0)->(0,0).v0[P3] -> back
PASS  ring 1 VC: planted i->i+2 (4 flits, depth 2) deadlocks; circular wait = 4 E channels
PASS  ring 1 VC: detection is bounded (cycle 3 < 100) and some flits never arrive
PASS  ring 1 VC: 500 more cycles move 0 flits (deadlock is permanent, congestion is not)
```

읽는 법: `(0,0)->(1,0).v0[P0]`의 앞 flit(P0 head)이 다음 항목 `(1,0)->(2,0).v0`(P1 소유)를 기다리고 … 마지막이 처음을 기다린다. **이 사이클이 Part B에서 CDG가 예언한 바로 그 사이클**이다. CDG는 "가능한 모든 대기"의 정적 그래프, 대기 그래프는 "지금 이 순간의 대기"의 동적 그래프다 — OS-M11 lockdep(정적·잠재 순서)과 STOR-M08 wait-for 탐지(동적·실제 대기)의 관계와 같다.

**dateline VC 2개 — 같은 트래픽:**

```text
cycle  0 moved 4 delivered  0 | 0>1.v0:0 0>1.v1:- 1>2.v0:1 1>2.v1:- 2>3.v0:2 2>3.v1:- 3>0.v0:- 3>0.v1:3
cycle  1 moved 4 delivered  0 | 0>1.v0:0 0>1.v1:3 1>2.v0:11 1>2.v1:- 2>3.v0:22 2>3.v1:- 3>0.v0:- 3>0.v1:3
cycle  2 moved 3 delivered  1 | 0>1.v0:0 0>1.v1:3 1>2.v0:11 1>2.v1:- 2>3.v0:22 2>3.v1:- 3>0.v0:- 3>0.v1:3
...
cycle 17 moved 1 delivered 16 | 0>1.v0:- 0>1.v1:- 1>2.v0:- 1>2.v1:- 2>3.v0:- 2>3.v1:- 3>0.v0:- 3>0.v1:-
```

P3(3→1)은 wrap을 건너며 VC1을 쓰므로 `0>1.v1`이 비어 있어 진행한다. P3가 빠지면 `3>0.v1`이 풀려 P2가, 그다음 P1, P0 순으로 사슬이 풀린다. 사이클 1에 `0>1.v0`에 P0 flit이 하나뿐인 것은 같은 물리 링크 0→1을 P3(VC1)와 나눠 쓰다 라운드로빈에서 졌기 때문이다 — **VC는 버퍼를 나눌 뿐 대역폭은 나누지 않는다**.

```text
      ring k=4, 2 VC : DONE @cycle 18: 16/16 flits delivered
PASS  ring dateline 2 VCs: same traffic completes 16/16
PASS  ring 1 VC, depth 8 >= packet: still deadlocks (VC ownership is the held resource)
      torus 4x4, 1 VC: DEADLOCK @cycle 3 (0/64 delivered): (0,0)->(1,0).v0[P0] -> (1,0)->(2,0).v0[P1] -> (2,0)->(3,0).v0[P2] -> (3,0)->(0,0).v0[P3] -> back
PASS  torus 4x4 1 VC: planted (x+2,y) deadlocks
      torus 4x4, 2 VC: DONE @cycle 18: 64/64 flits delivered
PASS  torus 4x4 dateline: completes 64/64
PASS  mesh XY, (x+1,y) for x<3 wraps to x=0 (westbound): completes
29/29 passed
```

버퍼 8칸(패킷 4 flit보다 큼)에서도 데드락인 이유: 이 모델의 자원은 **칸이 아니라 VC 소유권**이다. 한 VC는 tail이 떠날 때까지 한 패킷 것이라 칸이 남아도 다른 패킷의 head가 못 들어간다. 버퍼를 키우면 사이클을 **채우는 데 필요한 트래픽**이 늘어 확률이 줄 뿐, 사이클이 그래프에서 사라지지 않는다 (VC를 여러 패킷이 나눠 쓰는 비-atomic 재할당·virtual cut-through에서도 버퍼가 패킷 단위 자원이 될 뿐 같은 논리가 적용된다).

**랜덤 트래픽에서는 언제 나나 — demo.** 같은 시뮬레이터에 균등 랜덤 목적지 트래픽을 300사이클 넣고 비운다 (seed 0–9 고정).

<!-- code: demo_deadlock_rate.py -->

```text
$ uptime
16:07  up 6 days, 16:28, 10 users, load averages: 6.62 4.82 3.70
$ python3 -I demo_deadlock_rate.py
4x4, 4-flit packets, VC depth 2, inject 300 cycles, 10 seeds per cell
config                  rate deadlocks       DL @cycle   delivered (all)   drain cycle*
torus DOR, 1 VC         0.02      0/10               -       4032/4032              303
torus DOR, 1 VC         0.06      0/10               -      11536/11536             310
torus DOR, 1 VC         0.10      0/10               -      19476/19476             317
torus DOR, 1 VC         0.13      3/10        131..245      21347/21936             373
torus DOR, 1 VC         0.16      4/10         98..295      24658/26760             437
torus DOR, 1 VC         0.24      9/10         24..469      19837/28600             691
torus dateline, 2 VC    0.02      0/10               -       4032/4032              303
torus dateline, 2 VC    0.06      0/10               -      11536/11536             309
torus dateline, 2 VC    0.10      0/10               -      19476/19476             318
torus dateline, 2 VC    0.13      0/10               -      25076/25076             357
torus dateline, 2 VC    0.16      0/10               -      31092/31092             430
torus dateline, 2 VC    0.24      0/10               -      46120/46120             635
mesh XY, 1 VC           0.02      0/10               -       4032/4032              303
mesh XY, 1 VC           0.06      0/10               -      11536/11536             311
mesh XY, 1 VC           0.10      0/10               -      19476/19476             354
mesh XY, 1 VC           0.13      0/10               -      25076/25076             410
mesh XY, 1 VC           0.16      0/10               -      31092/31092             529
mesh XY, 1 VC           0.24      0/10               -      46120/46120             746

first deadlock found: torus DOR, 1 VC rate 0.13 seed 3 @cycle 245, cycle of 4 buffers:
  (3,1)->(3,2).v0 -> (3,2)->(3,3).v0 -> (3,3)->(3,0).v0 -> (3,0)->(3,1).v0 -> back
* drain cycle = median cycle at which the last flit arrived (runs that completed); injection stops at 300
runtime 7.1 s
```

기계: Apple M6 (perflevel0/1/2 물리 코어 2/4/6), Apple clang 21.0.0 (C 레슨용 줄, 이 레슨은 Python 3.9.6). load average 6–7 (다른 세션 동시 실행) — **런타임은 참고용**, 데드락 수·도착 수·drain 사이클은 seed가 고정이라 기계 부하와 무관하게 재현된다.

| 관찰 | 해석 |
|---|---|
| torus DOR은 rate ≤ 0.10에서 10/10 무사, 0.13부터 데드락 | 사이클은 늘 그래프에 있지만, **채워질 만큼 트래픽이 몰려야** 난다. 저부하 테스트만 통과한 설계가 실리콘에서 멈추는 이유 |
| 같은 seed에서 dateline·mesh XY는 0/10 | CDG 비순환 = 어떤 부하에서도 안 남 (Dally–Seitz ⇐) |
| mesh XY도 0.24에서 drain이 746까지 늘어남 | 그건 **혼잡(congestion)**: 느려지지만 결국 비워진다. 데드락은 "주입을 멈춰도 영원히 0" (Part C의 500사이클 테스트) |
| "delivered (all)"의 분모가 데드락 행에서 작다 | 데드락 시점에 시뮬레이션이 멈춰서 그 뒤 생성될 패킷이 안 들어갔기 때문 |
| dateline이 mesh XY보다 빨리 비움 | torus는 wrap 링크로 평균 거리가 짧다. 단 dateline은 버퍼(VC)도 2배라 공정한 비교가 아니다 |

**Coffman 4조건으로 정리 (Coffman, Elphick, Shoshani 1971; 조건표 자체는 OS-M10 참고).**

| 조건 | NoC에서의 모습 | 깨는 방법 | 비용 |
|---|---|---|---|
| 상호 배제 | VC 버퍼는 한 패킷 것 | 사실상 못 깸 | — |
| 점유 대기 | wormhole은 잡은 버퍼를 쥔 채 다음을 요청 | (거의 안 씀) store-and-forward도 패킷 버퍼를 쥔 채 기다리므로 안 깨진다 | — |
| 선점 없음 | lossless(credit)라 flit을 버리거나 뺏지 않음 | deflection(bufferless, 틀린 방향으로 밀어냄), drop + 재전송, 탐지 후 복구(Disha 같은 전용 탈출 버퍼 — 기억 기반, 원문 확인) | livelock 위험, 재전송 버퍼, 복잡도 |
| **순환 대기** | CDG 사이클 | **회피(avoidance)**: DOR, 턴 모델, dateline VC, Duato escape VC | 적응성 손실 또는 VC 면적 |

실제 NoC 설계는 거의 다 마지막 줄(회피)이다. DCN-M05의 PFC 워치독은 "선점 없음"을 깨는 쪽(드롭)이라는 점이 대조적이다.

**칩 검증 관점에서 한 가지 더.** 라우팅 데드락만 있는 게 아니다. 요청을 받은 노드가 응답을 보낼 수 없으면 요청을 소비하지 못한다 → "목적지는 항상 소비"라는 가정이 깨져 CDG가 비순환이어도 멈춘다 (**프로토콜/메시지 의존 데드락**). 그래서 일관성 프로토콜은 메시지 종류마다 채널을 분리한다 — 예를 들어 ARM AMBA CHI는 REQ·RSP·SNP·DAT 채널을 따로 둔다. 실리콘 bring-up에서 인터커넥트 hang은 대개 버스 타임아웃/hang 탐지 레지스터나 워치독 리셋으로만 보이므로, RTL 시뮬레이션 단계에서 이 레슨 같은 대기 그래프 덤프(누가 어느 VC를 쥐고 무엇을 기다리나)를 남기는 것이 디버그의 핵심이다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"                  # 29/29 passed, exit=0
python3 -I -W error -X dev solution.py >/dev/null; echo $? # 0 (경고 없음)
```

## 7. 자주 하는 실수

**① visited 집합 하나로 사이클 판정.** 다이아몬드 DAG에서 거짓 양성. CDG처럼 합류가 많은 그래프에서는 거의 항상 "사이클 있음"이 나온다.

```python
# 틀림                                   # 고침: GRAY(스택 위)로 가는 간선만 사이클
if nxt in visited: return True           if color[nxt] == GRAY: return path[path.index(nxt):]
                                         if color[nxt] == BLACK: continue
```

**② 토폴로지로 CDG를 그림.** naive CDG(104개 간선)는 XY에서도 순환이다. "mesh는 사이클이 있으니 VC가 필요하다"는 틀린 결론이 나온다. 간선은 R이 실제로 주는 다음 채널에서만 만든다.

**③ dateline을 건넌 다음 홉에서 VC0으로 돌아감.** wrap 링크만 VC1로 칠하면 `... -> (0,3)->(0,0).v1 -> (0,0)->(0,1).v0 -> ...`로 사이클이 그대로 남는다 (연습 4의 실제 출력). VC1은 그 차원이 끝날 때까지 유지해야 VC1 → VC0 간선이 없어진다.

```python
vc = 1 if topo.is_wrap(cur, d) else 0                     # 틀림
vc = 1 if topo.is_wrap(cur, d) or (in_ch and AXIS[in_ch.dir] == AXIS[d] and in_ch.vc == 1) else 0
```

**④ dateline을 한 차원에만.** X 링만 고치면 Y 링 사이클이 남는다 (연습 4). torus는 **모든** 차원에 VC 쌍이 필요하다.

**⑤ "오래 멈춤 = 데드락"으로 판정.** 0.24 부하의 mesh XY는 버퍼 앞 flit이 16사이클 넘게 멈추는 일이 흔하지만 결국 다 도착한다. 타임아웃은 **의심**일 뿐이고, 판정은 대기 그래프의 사이클(또는 "주입을 멈춰도 영원히 0")로 한다. 반대로 "전체 네트워크 이동 0"만 보면 일부 링만 멈춘 **부분 데드락**을 놓친다 — 그래서 시뮬레이터는 버퍼별 정지 시간도 본다.

**⑥ 적응 라우팅의 대기 그래프 사이클을 곧 데드락으로 봄.** 후보가 여럿이면 "A 또는 B 중 하나만 풀리면 된다"(OR 대기)라서 사이클만으로는 부족하고 **knot**(빠져나갈 간선이 없는 강연결 영역)이 필요하다. 이 레슨의 시뮬레이터는 결정적 라우팅에만 쓰고, 그래서 사이클 = 데드락이 성립한다.

**⑦ 재귀 DFS.** 8×8 torus × 2 VC = 채널 512개, 3D torus나 큰 mesh에서는 수천 개 — 파이썬 재귀 한도에서 `RecursionError`. 명시적 스택으로 쓴다.

## 8. 면접 Q&A

<details>
<summary>Q1. Dally–Seitz 정리를 정확히 말해 보세요. 증명은 어떻게 하나요?</summary>

결정적 라우팅 함수 R은 채널 의존 그래프에 사이클이 없을 때, 그리고 그때만 데드락 자유다. CDG 정점은 채널, 간선 cᵢ→cⱼ는 어떤 목적지로 가는 패킷이 cᵢ 다음에 cⱼ를 쓸 수 있을 때. 충분조건 증명: 비순환이면 위상 순서로 채널에 번호를 매길 수 있고 모든 패킷은 번호가 증가하는 순서로 채널을 잡는다. 막힌 패킷 중 가장 큰 번호의 채널을 쥔 패킷은 더 큰 번호를 기다리는데, 그걸 쥔 막힌 패킷은 없으니 진행한다 — 모순. 전역 락 순서 증명과 같다.
꼬리질문: "⇒ 방향(사이클 → 데드락)은 항상 맞나요?" → 원 모델(사이클을 동시에 채울 수 있다는 가정) 안에서다. 사이클이 있어도 실제로 채울 수 없는 경우를 지적한 후속 연구가 있다. 그래서 실무에서는 "비순환으로 만든다"가 목표이고, 사이클이 남으면 채우는 트래픽 패턴을 시뮬레이션으로 찾는다.

</details>

<details>
<summary>Q2. mesh XY는 VC 없이 데드락이 없는데, torus DOR은 왜 VC가 필요한가요? 몇 개?</summary>

mesh XY는 X 안에서 한 방향 직선, X→Y만 허용하므로 번호를 매길 수 있다. torus는 X 순서를 지켜도 한 차원의 링 자체가 원이라 링 안의 연속 채널 쌍이 순환 의존을 만든다. 각 차원·방향마다 VC 2개(dateline 앞 VC0, 뒤 VC1)면 링이 두 직선으로 풀린다. 최소 경로는 dateline을 최대 한 번 건너기 때문에 2개로 충분하다.
꼬리질문: "k=4에서 −방향 링은 VC 없이도 비순환이던데?" → 동률(거리 2)을 +로 보내면 −방향은 1홉만 쓰고 1홉은 의존을 만들지 않는다. k가 홀수이거나 동률을 나눠 보내면 양쪽 모두 순환이다. 이런 건 도구로 확인한다.
꼬리질문 2: "dateline 말고는?" → 링마다 VC를 나누는 다른 규칙(예: 목적지가 dateline 너머인지로 출발 VC 결정), bubble flow control(링에 빈 패킷 칸 하나를 늘 남기는 주입 제한 — 기억 기반, 세부는 확인 필요)처럼 VC 수를 아끼는 방식들이 있다.

</details>

<details>
<summary>Q3. 교착의 4가지 조건을 NoC에 대응시키고 각각 어떻게 깨나요?</summary>

상호 배제 = VC 버퍼 소유(못 깸). 점유 대기 = wormhole이 잡은 버퍼를 쥔 채 다음 요청(store-and-forward로 바꿔도 패킷 버퍼를 쥐고 기다리므로 안 깨짐). 선점 없음 = lossless credit 흐름(deflection 라우팅, drop+재전송, 탐지 후 탈출 버퍼로 복구로 깸). 순환 대기 = CDG 사이클(DOR·턴 모델·dateline VC·escape VC로 회피). 온칩에서는 회피가 표준이다 — 손실 없는 흐름 제어와 단순한 라우터를 유지할 수 있으니까.
꼬리질문: "deflection은 데드락이 없나요?" → 기다리지 않으니 데드락은 없지만 계속 밀려나는 livelock 위험이 있어 나이 우선(oldest-first) 같은 우선순위가 필요하다 (OS-M10의 무작위 백오프와 같은 종류의 문제).

</details>

<details>
<summary>Q4. 완전 적응 라우팅은 CDG가 순환인데 어떻게 데드락 자유가 되나요?</summary>

Duato 정리: 적응 라우팅 R에 연결된 부분 함수 R₁(보통 escape VC 위의 DOR)이 있고 그 확장 CDG(직접·간접 의존 포함)가 비순환이면 R은 데드락 자유다. 적응 VC에서 막힌 패킷은 언제든 escape VC로 옮길 수 있고, escape 네트워크는 비순환이라 결국 흐른다.
꼬리질문: "VC 없이 적응성을 얻으려면?" → 턴 모델(west-first 등)이나 odd-even. 금지 턴 2개로 비순환을 유지하며 일부 적응성을 남긴다 — 연습 1·2에서 XY 0개, west-first 232개, odd-even 208개의 적응 상태(4×4)를 비교한다.

</details>

<details>
<summary>Q5. 라우팅은 데드락 자유인데 SoC가 멈췄습니다. 무엇을 의심하나요?</summary>

메시지 의존(프로토콜) 데드락. Dally–Seitz는 "목적지는 항상 소비한다"를 가정한다. 요청 패킷을 받은 슬레이브가 응답을 보낼 버퍼가 없어 요청을 못 받으면, 응답 채널 ↔ 요청 채널 사이에 라우팅 밖의 의존이 생긴다. 해법은 메시지 클래스별 가상 네트워크 분리(요청/응답/스누프/데이터 — AMBA CHI의 REQ·RSP·SNP·DAT처럼)와, 엔드포인트가 응답을 위한 자원을 미리 확보한 뒤에만 요청을 받는 규칙이다.
꼬리질문: "디버그는?" → 시뮬레이션이면 이 레슨처럼 VC 소유자·대기 대상 덤프로 대기 그래프를 만든다. 실리콘이면 인터커넥트 타임아웃/hang 탐지 상태 레지스터와 디버그 버스로 멈춘 트랜잭션을 찾는다 (구체적 레지스터는 IP마다 다름).

</details>

<details>
<summary>Q6. lockdep과 CDG는 무엇이 같고 무엇이 다른가요? SCC는 어디에 쓰나요?</summary>

같은 점: 둘 다 "A를 쥐고 B를 요청" 간선의 그래프에서 사이클을 찾고, 실제로 멈추기 **전에** 잠재 데드락을 잡는다. 다른 점: CDG는 라우팅 함수에서 **모든 가능한** 경로를 정적으로 열거(완전, 설계 시점), lockdep(OS-M11)은 실행 중 **관찰된** 획득 순서만 기록(실행된 경로만, 대신 실제 코드 그대로). 시뮬레이터의 대기 그래프는 STOR-M08처럼 "지금 실제로 막힌" 동적 그래프다.
SCC: `find_cycle`은 사이클 하나만 준다. 고쳐야 할 순환 영역 전부(예: torus DOR의 링 8개)를 한 번에 보려면 Tarjan/Kosaraju로 크기 ≥ 2인 강연결 요소를 뽑는다 (ALGO-A10). 각 SCC가 "VC 분리나 턴 금지가 필요한 곳"이다.

</details>

<details>
<summary>Q7. 버퍼를 깊게 하면 데드락이 없어지나요?</summary>

아니다. 사이클을 채우는 데 필요한 트래픽이 늘어 확률이 줄 뿐이다. 이 레슨의 시뮬레이터에서는 VC가 tail까지 한 패킷 것이라 버퍼 8칸(패킷 4 flit)에서도 심어 둔 패턴이 그대로 데드락이다. demo에서도 DOR 1 VC는 저부하(≤ 0.10)에서 무사하다가 0.13부터 멈춘다 — 저부하 회귀 테스트만 돌리면 못 잡는다.
꼬리질문: "virtual cut-through면?" → 버퍼가 패킷 하나를 통째로 담아 head가 막혀도 링크를 쥐지 않지만, 그 버퍼 자체가 쥔 자원이 되어 버퍼 단위 CDG에 같은 사이클이 생긴다. 회피 규칙은 그대로 필요하다.

</details>

## 9. 추가 연습

### 연습 1 — 턴 모델 세 가지

(a) 반시계·시계 추상 사이클에서 턴을 하나씩 금지하는 16가지 조합 각각에 대해, 4×4 mesh 턴 그래프(naive CDG − 금지 턴)가 비순환인지 세어라. (b) west-first, north-last, negative-first를 라우팅 함수로 쓰고 CDG가 비순환인지, "후보가 2개 이상인 도달 상태" 수가 XY·완전 적응과 비교해 어떤지 보라.

<details>
<summary>답 보기</summary>

(a) 12/16 비순환. 사이클이 남는 4개는 EN+NE, NW+WN, WS+SW, SE+ES (사이클 길이 8, 10, 10, 8): 한 방향으로 꺾는 턴과 그 반대로 되꺾는 턴을 함께 금지하면 남은 6개 턴으로 더 큰 사이클을 돌 수 있다. 턴 그래프는 비최소 경로까지 포함하므로 라우팅 알고리즘이 무엇이든 성립하는 판정이다. (b) 세 가지 모두 비순환, 간선 86개, 적응 상태 232개(대칭이라 같은 숫자). XY는 0, 완전 적응은 464지만 순환이다. west-first는 "W로 갈 일이 있으면 W 먼저 전부, 그다음 E/N/S 중 적응" — 금지 턴은 NW·SW(W로 꺾기). north-last는 "N은 마지막에만" — NE·NW 금지. negative-first는 "W·S를 먼저 적응적으로, 그다음 E·N" — ES·NW 금지. 셋 다 반시계·시계 사이클에서 하나씩 금지한다. 출발지에 따라 적응성이 비대칭이라는 것이 턴 모델의 약점이다 (예: west-first는 서쪽 목적지에 대해 경로가 하나뿐).

</details>

### 연습 2 — odd-even 턴 모델 (Chiu)

짝수 열에서 EN·ES 턴, 홀수 열에서 NW·SW 턴을 금지하는 최소 경로 라우팅을 쓰라. 4×4/5×5/6×6에서 (a) 막다른 상태 없음(연결성), (b) CDG에 금지 턴 간선 0개, (c) 비순환을 확인하라.

<details>
<summary>답 보기</summary>

동쪽으로 가는 중: 현재 열이 홀수이거나 출발 열이면 N/S 가능, 목적지 열이 홀수이거나 아직 2열 이상 남았으면 E 가능 (짝수 목적지 열 바로 앞에서 E로 들어가면 그 짝수 열에서 EN/ES로 꺾을 수 없어 막힌다). 서쪽으로 가는 중: W는 늘 가능, N/S는 짝수 열에서만. 금지 턴 필터만 하고 이 "미리 보기"를 빼면 `build_cdg`가 `routing stuck`으로 실패한다 — 턴 금지와 라우팅 알고리즘은 별개이고 연결성까지 보장해야 한다. 실제 출력: 4×4 적응 상태 208, 5×5 880, 6×6 2850, 셋 다 금지 턴 0개·비순환. (의사코드는 Chiu 2000의 ROUTE를 기억으로 옮겼고 테스트가 세 조건을 확인한다.)

</details>

### 연습 3 — XY와 YX를 섞으면

출발 노드의 (x+y) 홀짝으로 XY와 YX를 골라 VC 하나에 섞어라. CDG는? 2×2 mesh에서 네 노드가 대각선 맞은편으로 4-flit 패킷을 보내면? XY는 VC0, YX는 VC1에 두면(O1TURN 식)?

<details>
<summary>답 보기</summary>

XY는 Y→X 턴을, YX는 X→Y 턴을 금지하지만 둘을 섞으면 서로가 금지한 턴을 쓰므로 8개 턴이 모두 생긴다 → `(0,1)->(0,0) -> (0,0)->(1,0) -> (1,0)->(1,1) -> (1,1)->(0,1)` 사이클. 2×2 시뮬레이터: `DEADLOCK @cycle 3 (0/16 delivered)`, 네 패킷이 정사각형을 반시계로 서로 기다린다. VC 분리 시 각 VC 안은 순수 XY 또는 순수 YX라 각각 비순환이고 VC 사이 간선이 없다 → `DONE @cycle 10: 16/16`. O1TURN(Seo 외, ISCA 2005)은 출발 시 무작위로 XY/YX를 골라 부하를 나누는 대신 이 VC 분리로 데드락을 막는다.

</details>

### 연습 4 — dateline 실수 두 가지와 k=5, k=8

(a) X 차원에만 dateline, (b) wrap 홉만 VC1이고 다음 홉에서 VC0으로 복귀 — 각각 CDG는? k=5, k=8 torus에서 DOR(1 VC)과 dateline(2 VC)은?

<details>
<summary>답 보기</summary>

(a) Y 링 사이클이 남음: `(0,0)->(0,1).v0 -> (0,1)->(0,2).v0 -> (0,2)->(0,3).v0 -> (0,3)->(0,0).v0`. (b) `(0,3)->(0,0).v1 -> (0,0)->(0,1).v0`가 VC0 체인으로 되돌아가 사이클이 남는다. k=5: DOR 순환, dateline 비순환(채널 200개, 간선 220개). k=8: DOR 순환, dateline 비순환(채널 512개, 간선 640개). k가 커져도 최소 경로는 한 링에서 dateline을 한 번만 건너므로 VC 2개면 된다.

</details>

### 연습 5 — Dally–Seitz 방식의 번호 함수

XY의 모든 의존 간선에서 엄격히 증가하는 채널 번호 함수를 명시적으로 써라. 같은 함수를 완전 적응 CDG에 적용하면?

<details>
<summary>답 보기</summary>

`E(x) = x`, `W(x) = k−1−x` (X 채널 0..k−2), `N(y) = k + y`, `S(y) = k + (k−1−y)` (Y 채널 k..2k−2). 직진은 같은 차원 안에서 +1, X→Y 턴은 X 번호 ≤ k−2 < k ≤ Y 번호라 증가 → 68개 간선 모두 위반 0. 완전 적응 CDG에 적용하면 Y→X 턴 36개가 감소 방향이다. 그리고 그 CDG는 순환이므로 어떤 번호 함수도 존재할 수 없다 — 번호 함수의 존재 = 비순환 = (결정적이면) 데드락 자유.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 18/18 passed, exit=0
```

실제 출력:

```text
-- 연습 1: turn models --
PASS  west-first    : CDG acyclic, 86 edges, 232 adaptive states
PASS  north-last    : CDG acyclic, 86 edges, 232 adaptive states
PASS  negative-first: CDG acyclic, 86 edges, 232 adaptive states
PASS  Glass-Ni: forbid 1 turn per abstract cycle -> 12/16 acyclic; cyclic: EN+NE(len 8), NW+WN(len 10), WS+SW(len 10), SE+ES(len 8)
PASS  XY has 0 adaptive states, fully adaptive 464 (but cyclic)
-- 연습 2: odd-even --
PASS  odd-even 4x4: connected, acyclic, 0 forbidden turns used, 208 adaptive states
PASS  odd-even 5x5: connected, acyclic, 0 forbidden turns used, 880 adaptive states
PASS  odd-even 6x6: connected, acyclic, 0 forbidden turns used, 2850 adaptive states
-- 연습 3: XY + YX mixed --
PASS  mixed XY/YX on 1 VC: cycle (0,1)->(0,0).v0 -> (0,0)->(1,0).v0 -> (1,0)->(1,1).v0 -> (1,1)->(0,1).v0 -> back
PASS  XY on VC0 + YX on VC1: acyclic
      2x2 mesh, 1 VC: DEADLOCK @cycle 3 (0/16 delivered): (0,0)->(1,0).v0[P0] -> (1,0)->(1,1).v0[P1] -> (1,1)->(0,1).v0[P2] -> (0,1)->(0,0).v0[P3] -> back
PASS  2x2 diagonal swap, 1 VC: deadlock
      2x2 mesh, 2 VC: DONE @cycle 10: 16/16 flits delivered
PASS  2x2 diagonal swap, 2 VC: completes 16/16
-- 연습 4: dateline mistakes --
PASS  dateline on X only: still cyclic: (0,0)->(0,1).v0 -> (0,1)->(0,2).v0 -> (0,2)->(0,3).v0 -> (0,3)->(0,0).v0 -> back
PASS  VC1 only on the wrap hop: still cyclic: (0,0)->(0,1).v0 -> (0,1)->(0,2).v0 -> (0,2)->(0,3).v0 -> (0,3)->(0,0).v1 -> back
PASS  k=5 torus: DOR cyclic, dateline acyclic (200 channels, 220 edges)
PASS  k=8 torus: DOR cyclic, dateline acyclic (512 channels, 640 edges)
-- 연습 5: Dally-Seitz numbering for XY --
PASS  XY: rank strictly increases on all 68 dependency edges (violations 0)
PASS  same rank on fully adaptive CDG: 36 edges go down (Y->X turns); CDG is cyclic so NO rank can work
18/18 passed
```

## 10. 핵심 정리

- CDG: 정점 = 채널(링크 × VC), 간선 c1→c2 ⇔ 어떤 패킷이 c1을 쥔 채 c2를 요청할 수 있음. **도달 가능한 상태에서 R이 주는 것만** — 토폴로지로 그리면 거짓 사이클.
- Dally–Seitz: 결정적 라우팅은 CDG 비순환 ⇔ 데드락 자유. 증명 = 채널 번호가 경로를 따라 엄격히 증가 = 전역 락 순서. 적응 라우팅은 Duato(비순환 escape 부분 함수).
- mesh XY 비순환(4×4: 채널 48, 간선 68). 완전 적응은 단위 정사각형 턴 사이클. 턴 모델은 두 추상 사이클에서 하나씩 금지(west-first, north-last, negative-first, odd-even) — 정한 뒤 반드시 CDG로 확인.
- torus DOR은 링 자체가 사이클. dateline: wrap을 건너면 VC1, 그 차원 끝까지 유지, 차원 바뀌면 VC0 → 비순환. 모든 차원에 필요.
- 시뮬레이터: 데드락 = 대기 그래프 사이클 + "영원히 이동 0". 오래 멈춤은 혼잡일 수 있다. 부분 데드락은 전역 정지만 봐서는 놓친다.
- 버퍼를 키우면 확률만 준다. torus DOR은 저부하에서 10/10 무사, 0.13부터 멈춤 — 저부하 테스트로는 못 잡는다.
- 같은 순환 대기: OS-M10(포크 순서) · OS-M11(lockdep) · ALGO-A10(SCC) · STOR-M08(wait-for) · STOR-M10(래치 순서) · DCN-M05(PFC 버퍼). 프로토콜 데드락은 메시지 클래스 분리로 따로 막는다.

## 참고자료

- W. J. Dally, C. L. Seitz, "Deadlock-Free Message Routing in Multiprocessor Interconnection Networks," IEEE Trans. Computers C-36(5), 1987 — CDG와 정리, virtual channel로 torus 사이클을 끊는 방법
- W. J. Dally, C. L. Seitz, "The Torus Routing Chip," Distributed Computing 1(4), 1986 — 2 VC로 torus 링 사이클을 끊은 실제 칩
- W. J. Dally, B. Towles, *Principles and Practices of Interconnection Networks*, Morgan Kaufmann 2004 — 라우팅·흐름 제어·데드락 장 (장 번호는 판본에서 확인)
- J. Duato, "A New Theory of Deadlock-Free Adaptive Routing in Wormhole Networks," IEEE TPDS 4(12), 1993
- C. J. Glass, L. M. Ni, "The Turn Model for Adaptive Routing," ISCA 1992
- G.-M. Chiu, "The Odd-Even Turn Model for Adaptive Routing," IEEE TPDS 11(7), 2000
- D. Seo et al., "Near-Optimal Worst-Case Throughput Routing for Two-Dimensional Mesh Networks" (O1TURN), ISCA 2005
- J. Duato, S. Yalamanchili, L. Ni, *Interconnection Networks: An Engineering Approach* — 데드락 회피·복구 (Disha 등) 개관
- N. Enright Jerger, T. Krishna, L.-S. Peh, *On-Chip Networks* (Synthesis Lectures on Computer Architecture) — 온칩 라우터 VC 할당·credit·프로토콜 데드락
- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks," ACM Computing Surveys 3(2), 1971 — 4조건
- R. Tarjan, "Depth-First Search and Linear Graph Algorithms," SIAM J. Computing 1(2), 1972 — DFS 간선 분류, SCC
- Arm AMBA CHI Architecture Specification — REQ/RSP/SNP/DAT 채널 분리
- 관련 레슨: OS-M10(4조건 · 락 순서) · OS-M11(mini lockdep) · ALGO-A10(SCC) · STOR-M08(2PL wait-for) · STOR-M10(latch crabbing) · DCN-M05(PFC 순환 버퍼 의존) · RTOS-M05(priority ceiling도 순서로 교착 예방)
