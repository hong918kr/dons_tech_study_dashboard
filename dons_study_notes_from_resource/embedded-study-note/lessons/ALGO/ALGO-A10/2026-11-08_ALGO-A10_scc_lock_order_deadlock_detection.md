---
id: ALGO-A10
time: 110분
updated: 2026-11-08
---
# ALGO-A10 강한 연결 요소: 락 의존 그래프 데드락 검출

## 학습 목표

- 스레드별 락 이벤트 로그에서 **락 순서 그래프**(lock-order graph: 간선 A→B = "A 를 쥔 채 B 를 잡았다")를 만들고, 간선마다 증거(스레드·소스 위치·로그 줄)와 **게이트 후보**(같이 쥐고 있던 락)를 남긴다.
- **Tarjan SCC** 를 재귀 없이(명시적 스택) 구현하고, 재귀판이 정점 10^5 사슬에서 실제로 SIGSEGV 로 죽는 것을 이 맥에서 확인한다. **Kosaraju** 를 같은 규약으로 구현해 무작위 그래프 500개 + 손 계산 사례로 두 결과를 대조한다.
- SCC 마다 **증거 사이클 하나**(가장 짧은 것)를 뽑고, DEADLOCK / GATED / SINGLE-THREAD / SELF-LOOP 로 판정한다. 왜 "SCC 찾기 ≠ 모든 사이클 나열"인지(Johnson, 지수 개수), 왜 O(V+E) 인지 말할 수 있다.
- 펌웨어 트레이스(SPI 버스·DMA 채널·전원 도메인·로거, 태스크 4개)에서 3-락 순환과 게이트로 막힌 순환을 구분하고, 축약 그래프의 위상 순서로 **락 계층(lock hierarchy)** 을 제안한다.
- 면접 체크 3개(Coffman 4조건과 각각 깨는 법 · lockdep 식 런타임 검사 · 의존 그래프의 사이클 → SCC)를 한 흐름으로 설명한다.

## 1. 문제

"필드에서 1주일에 한 번 펌웨어가 멈춘다. 재현은 안 되고 락 트레이스만 있다." — 이때 쓰는 도구가 이 레슨이다. 면접에서는 "로그로 잠재적 데드락을 찾는 프로그램을 짜 보라 → 사이클이 여러 개면? → 그래프가 10^6 이면? → 가짜 경보는?" 순서로 꼬리질문이 이어진다. 답의 뼈대는 **강한 연결 요소**(SCC, strongly connected component)다.

교착 상태(deadlock)의 네 조건(Coffman·Elphick·Shoshani 1971: 상호 배제, 점유 대기, 비선점, 순환 대기)은 OS-M10 이 표로 다룬다. 이 레슨은 그중 **순환 대기(circular wait)** 를 "실행 기록에서 미리 찾아내는" 쪽이다. 핵심 관찰은 이렇다.

- 스레드 T1 이 A 를 쥔 채 B 를 잡은 적이 있고(A→B), T2 가 B 를 쥔 채 A 를 잡은 적이 있다면(B→A), **두 실행이 겹치는 날** T1 은 B 를, T2 는 A 를 기다리며 영원히 멈춘다. 로그에서는 두 일이 몇 초 떨어져 일어나서 실제로는 안 멈췄어도, 그래프에는 사이클 A→B→A 가 남는다.
- 그래프의 모든 사이클은 **어느 한 SCC 안에** 들어 있다 (사이클 위의 정점들은 서로 도달 가능하니까). 그래서 "크기 2 이상인 SCC 또는 자기 루프" = "사이클이 있는 곳" 이다. 사이클을 하나하나 나열하지 않고도 O(V+E) 에 **사이클이 있는 모든 영역**을 찾는다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C 트레이스의 락 순서 그래프 (간선 A→B = A 를 쥔 채 B 를 잡음)</text>
  <rect x="40" y="60" width="110" height="30" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="95" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">spi_bus</text>
  <rect x="300" y="60" width="110" height="30" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="355" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">dma_ch2</text>
  <rect x="170" y="170" width="110" height="30" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="225" y="190" fill="currentColor" text-anchor="middle" font-family="monospace">pwr_domain</text>
  <g stroke="currentColor" fill="none" stroke-width="1.6">
    <path d="M150 75 L298 75" marker-end="url(#a10a)"/>
    <path d="M345 90 L268 168" marker-end="url(#a10a)"/>
    <path d="M190 170 L112 92" marker-end="url(#a10a)"/>
  </g>
  <text x="175" y="68" fill="currentColor">spi_task</text>
  <text x="236" y="128" fill="currentColor">dma_task</text>
  <text x="70" y="145" fill="currentColor">pm_task</text>
  <text x="40" y="228" fill="currentColor" font-weight="bold">DEADLOCK: 스레드 3개가 만든 3-락 순환</text>
  <rect x="560" y="50" width="110" height="30" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="615" y="70" fill="currentColor" text-anchor="middle" font-family="monospace">board_cfg</text>
  <rect x="470" y="150" width="100" height="30" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="520" y="170" fill="currentColor" text-anchor="middle" font-family="monospace">clk_tree</text>
  <rect x="625" y="150" width="100" height="30" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="675" y="170" fill="currentColor" text-anchor="middle" font-family="monospace">pinmux</text>
  <g stroke="currentColor" fill="none" stroke-width="1.6">
    <path d="M595 80 L535 148" marker-end="url(#a10a)"/>
    <path d="M635 80 L668 148" marker-end="url(#a10a)"/>
    <path d="M570 158 L623 158" marker-end="url(#a10a)"/>
    <path d="M625 173 L572 173" marker-end="url(#a10a)"/>
  </g>
  <text x="470" y="205" fill="currentColor">clk→pin: pm_task · pin→clk: log_task</text>
  <text x="470" y="225" fill="currentColor" font-weight="bold">GATED: 둘 다 board_cfg 아래</text>
  <rect x="300" y="270" width="110" height="30" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="355" y="290" fill="currentColor" text-anchor="middle" font-family="monospace">log_buf</text>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M355 90 L355 268" marker-end="url(#a10a)"/>
    <path d="M240 200 L318 268" marker-end="url(#a10a)"/>
    <path d="M510 180 L412 275" marker-end="url(#a10a)"/>
  </g>
  <text x="420" y="290" fill="currentColor">log_buf = leaf 락 → 사이클 없음</text>
  <text x="10" y="330" fill="currentColor">점선 = 순환에 안 끼는 간선(일부만). 경고색 = 교착 후보, 회색 = 게이트로 막힘</text>
</svg>
```

**같은 순환 대기, 다른 층.** 6층의 일곱 레슨은 모두 "무언가를 쥔 채 다음 것을 기다리는 관계가 원을 이룬다"를 다룬다. 다른 점은 무엇이 정점이고 언제 검사하느냐뿐이다.

| 레슨 | 정점(자원) | 간선 | 언제 찾나 | 이 레슨과의 관계 |
|---|---|---|---|---|
| OS-M10 | 포크(뮤텍스) | 쥔 포크 → 원하는 포크 | 실제로 멈춘 뒤(워치독) | 4조건 표·해법의 주인. 여기선 인용만 |
| OS-M11 | 락 클래스 | held → new | 실행 중, 간선이 **새로 생길 때마다** DFS | 온라인·증분. 여기는 오프라인·전체 |
| **ALGO-A10** | 락 이름 | held → new (+스레드·위치·게이트) | 로그 전체를 한 번에, SCC | 모든 순환 영역을 한 번에 보고 |
| STOR-M08 | 트랜잭션 | 대기 → 보유자 (wait-for) | 막힐 때/주기적으로 | wait-for 그래프는 "지금" 교착 (연습 1) |
| STOR-M10 | B+tree 노드 래치 | 부모 → 자식 순서 | 프로토콜로 예방 | 전역 순서(위→아래) = 순환 대기 금지 |
| SOCX-M05 | NoC 채널(버퍼) | 채널 → 다음 채널 (CDG) | 설계 시 증명 | Dally–Seitz: CDG 가 비순환이면 교착 없음 |
| DCN-M05 | 스위치 큐(PFC) | 버퍼 → 다음 버퍼 (BDG) | 라우팅 검사 | 같은 DFS/SCC, 대상이 네트워크 |

**로그 형식** (이 레슨이 정한 것, 펌웨어 트레이스 링 버퍼에서 UART/SWO 로 뽑았다고 가정):

```text
t=<thread> acq <lock> @<site>     # 락 획득 (site = 파일:줄)
t=<thread> rel <lock>             # 락 해제 ('#' 뒤는 주석, 빈 줄 허용)
```

### Part A — 그래프 만들기와 SCC 두 가지

- `lg_acquire_edges(lg, t, lock, site, line)` — 스레드 t 가 지금 쥔 **모든** 락 h 에 대해 간선 h→lock 을 기록한다. 게이트 후보 = 그 순간 쥔 락 중 h 를 뺀 나머지. 같은 간선이 다시 나오면 횟수·스레드 집합을 누적하고 게이트는 **교집합**.
- `scc_tarjan(g, comp)` — 반복(iterative) Tarjan. 재귀 금지 (정점 10^6 사슬에서도 동작).
- `scc_kosaraju(g, comp)` — 반복 Kosaraju (post-order → 역그래프에서 역순).
- 테스트: 손 계산 6가지(DAG, 큰 고리 하나, 중첩 사이클, 자기 루프, 분리된 그래프, SCC 사슬), 무작위 그래프 500개를 두 알고리즘 + 전이 폐포(oracle)로 3자 대조, 정점 10^6 사슬·고리.

### Part B — 증거 사이클과 판정

- `scc_witness(g, comp, s, cyc, maxlen)` — 성분 안에서 s 로 돌아오는 **가장 짧은** 사이클을 BFS 로.
- `scc_verdict(...)` — 성분 안쪽 간선 전체를 보고: 게이트 교집합이 있으면 **GATED**, 스레드가 하나뿐이면 **SINGLE-THREAD**, 크기 1(자기 루프)이면 **SELF-LOOP**, 아니면 **DEADLOCK**.
- 테스트: ABBA(순차 실행인데도 보고), G 아래 ABBA(거름), G 가 한쪽에만(안 거름), 한 스레드의 양방향, 같은 락 재획득, 4-락 중첩 성분(증거 길이 3), 게이트 섞인 성분(보수적 보고), 형식 오류 줄 번호, 안 쥔 락 해제 경고.

### Part C — 펌웨어 드라이버 트레이스

- 위 그림의 로그(태스크 4개, 락 7개)를 분석해 보고서를 찍는다: DEADLOCK 1개(spi_bus → dma_ch2 → pwr_domain → spi_bus, 스레드 3개, 각 간선의 위치), GATED 1개(clk_tree ↔ pinmux, board_cfg 아래).
- `cond_order(g, comp, ncomp, rank)` — 축약 그래프(condensation)를 Kahn 으로 위상 정렬 → **락 계층 제안**. 모든 성분 사이 간선이 순서를 지키고, 로거 락이 맨 끝인지 확인.

## 2. Problem (English)

You are given a per-thread lock event trace, one event per line:

```text
t=<thread> acq <lock> @<site>
t=<thread> rel <lock>
```

Build the **lock-order graph**: whenever thread `t` acquires lock `L` while holding locks `H1..Hk`, add edges `Hi -> L` (deduplicated), each annotated with the first thread/site/line that produced it, a count, the set of threads, and the intersection of "other locks held at that moment" (gate candidates).

Then report every **non-trivial strongly connected component** (size > 1, or size 1 with a self-loop) as a potential deadlock, with one concrete witness cycle (shortest cycle through the component's smallest lock id) and a verdict:

```c
int scc_tarjan(const graph_t *g, int *comp);     /* iterative; returns #components, -1 on OOM */
int scc_kosaraju(const graph_t *g, int *comp);   /* iterative; same contract */
int scc_witness(const graph_t *g, const int *comp, int s, int *cyc, int maxlen);
verdict_t scc_verdict(const lockgraph_t *lg, const graph_t *g, const int *comp, int c, int size,
                      int *gate_out, uint64_t *threads_out);
int cond_order(const graph_t *g, const int *comp, int ncomp, int *rank);  /* topo order of condensation */
```

Constraints: up to 10^6 vertices and edges — **no recursion** (a 10^6-deep DFS must not overflow the stack); O(V+E) time and memory; ≤ 64 threads (bitmask); ≤ 16 locks held at once per thread; deterministic output. Cross-check Tarjan against Kosaraju on random graphs by comparing *canonicalized* partitions (label = smallest vertex in the component).

Example — `T1: A then B`, later `T2: B then A` (they never overlap, nothing actually hung):

```text
[DEADLOCK] SCC {A, B}  threads: T1 T2
  A -> B  t=T1 holds @a.c:10, takes @a.c:11  (log line 2, seen 1x)
  B -> A  t=T2 holds @b.c:20, takes @b.c:21  (log line 6, seen 1x)
```

Follow-ups: why is "list all cycles" the wrong goal? When is a cycle harmless (gate lock)? Is a cycle produced by a single thread a bug?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데(`scc_tarjan`, `scc_kosaraju`, `lg_acquire_edges`, `scc_witness`, `scc_verdict`, `cond_order`). 문자열 표, 간선 해시, 로그 파서, CSR 빌더, `analyze()`, `report_print()` 는 주어진다. 스텁은 "성분 0개"를 돌려주므로 `analyze()` 가 -1 로 빠지고 하네스는 멈추지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
  case 'DAG': tarjan=BAD kosaraju=BAD order=BAD
FAIL starter.c:575  r == 3
FAIL starter.c:576  ord
…
random: 500 graphs, 15679 vertices, 22844 edges: tarjan mismatches=500 kosaraju mismatches=500 order violations=500
FAIL starter.c:594  bad1 == 0
…
FAIL starter.c:606  scc_tarjan(&chain, c) == N
…
FAIL starter.c:691  perm && respect
FAIL starter.c:692  rc == 0 && an.rank[an.comp[lock_id(&lg, "log_buf")]] == an.ncomp - 1
2/40 passed
```

(0.1 초, exit 1. 통과한 2개는 TODO 와 무관한 파서 검사 — 형식 오류 줄 번호, 안 쥔 락 해제 경고.)

## 4. 힌트

<details><summary>힌트 1 — 간선은 "쥔 것 전부 → 새 것"</summary>

held 스택이 `[G, A]` 인데 B 를 잡으면 G→B (게이트 후보 {A}) 와 A→B (게이트 후보 {G}) 두 개. 스택 맨 위 하나만 쓰면 SCC 결과는 같지만(G→A→B 경로가 있으니) 게이트 정보가 사라진다. 해제는 맨 위가 아니어도 된다 — 파서가 이미 처리한다.

</details>

<details><summary>힌트 2 — 재귀 Tarjan 을 그대로 옮긴다</summary>

재귀판의 지역 상태는 "지금 정점 v" 와 "다음에 볼 간선 위치" 두 개뿐이다. `cs[]`(정점)와 `it[]`(간선 위치) 두 배열을 호출 스택으로 쓰면 된다. 맨 위 프레임에서 간선을 하나 꺼내 → 미방문이면 push, 아니면 `on[w]` 일 때만 `low[v] = min(low[v], idx[w])`. 간선이 끝나면 pop 하면서 `low[v]==idx[v]` 면 성분을 꺼내고, 부모의 low 를 갱신한다 (재귀판에서 호출이 돌아온 직후 하던 일).

</details>

<details><summary>힌트 3 — Kosaraju 의 두 단계</summary>

1단계: G 에서 DFS, **끝나는 순서**(post-order)로 기록. 2단계: 간선을 뒤집은 G^T 를 `graph_build` 로 만들고, 1단계 기록을 **뒤에서부터** 보며 아직 번호 없는 정점에서 G^T DFS → 닿는 정점이 한 성분. 2단계 DFS 는 순서가 상관없으니 단순 스택이면 된다.

</details>

<details><summary>힌트 4 — 증거는 BFS, 판정은 "성분 안쪽 간선 전부"</summary>

s 에서 BFS 하되 `comp[w] != comp[s]` 인 간선은 건너뛴다. 처음으로 s 로 돌아가는 간선을 만나면 그게 s 를 지나는 가장 짧은 사이클. `par[]` 에 들어온 CSR 간선 위치를 저장하고 `csr_from()` 으로 거꾸로 따라간다. 판정은 증거 사이클 하나가 아니라 양 끝이 모두 c 인 간선 전부의 게이트 교집합·스레드 합집합으로 한다.

</details>

<details><summary>힌트 5 — 락 계층 = 축약 그래프의 위상 순서</summary>

성분 사이 간선마다 도착 성분의 진입 차수를 센다. 진입 차수 0 인 성분을 대기열에 넣고 꺼낼 때마다 rank 를 매기고, 나가는 간선의 진입 차수를 줄인다. 축약 그래프는 항상 DAG 이므로(성분끼리 사이클이 있으면 하나의 성분이었을 것) 전부 꺼내진다.

</details>

## 5. 풀이

### Part A — 락 순서 그래프: 무엇을 간선으로 남기나

`lg_acquire_edges` 는 짧다. 중요한 건 **무엇을 같이 저장하느냐**다.

```c
for (int i = 0; i < nh; i++) {
    int gates[MAX_HELD], ng = 0;
    for (int j = 0; j < nh; j++)
        if (j != i && h[j].lock != h[i].lock) gates[ng++] = h[j].lock;
    if (lg_add_edge(lg, h[i].lock, lock, t, h[i].site, site, line, gates, ng) != 0) return -1;
}
```

| 저장하는 것 | 왜 필요한가 |
|---|---|
| 처음 본 스레드, `site_from`(h 를 잡은 곳), `site_to`(새 락을 잡은 곳), 로그 줄 | 보고서에서 "어디를 고치나" 로 바로 간다 |
| 횟수, 스레드 집합(`uint64_t` 비트) | 한 스레드만 만든 순환인지 판정 (Part B) |
| 게이트 후보 = 매번 같이 쥐고 있던 다른 락의 **교집합** | 한 번이라도 G 없이 이 간선이 생겼다면 G 는 이 간선을 지켜 주지 않는다 |

순서 밖 해제(`acq A, acq B, rel A, acq C`)에서 C 는 B 만 쥔 채 잡힌다 → 간선 B→C 만 생겨야 한다. "맨 위만 해제" 로 짜면 `rel A` 를 경고로 무시하고 A 가 계속 쥐어진 것으로 남아 가짜 간선 A→C 가 생긴다 (7. 실수 3, 실제로 그 테스트 하나가 FAIL).

Linux lockdep 은 (내가 아는 한) 새 락을 잡을 때 **바로 앞에 쥔 락** 하나와의 의존만 추가하고 나머지는 전이로 처리한다 — 런타임 비용을 줄이려는 선택이다. 오프라인 분석은 비용 걱정이 적으니 전부 남기고 게이트 정보를 얻는다. SCC 결과는 같다 (G→A, A→B 가 있으면 G→B 가 없어도 G 에서 B 로 간다).

### Part A — Tarjan: low-link 의 의미

DFS 로 정점을 처음 방문한 순서를 `idx[v]` 라 하자. `low[v]` = "v 의 DFS 부분 트리에서, **아직 성분이 정해지지 않은**(스택 위) 정점으로 가는 간선 하나로 닿을 수 있는 가장 작은 idx". v 가 자기 부분 트리 밖의 더 오래된 스택 정점에 닿지 못하면(`low[v] == idx[v]`) v 가 성분의 **뿌리**고, 스택에서 v 위에 쌓인 정점이 전부 그 성분이다.

반복판 실행을 그대로 찍은 것 (손 계산 사례 "nested cycles": A→B, B→C, C→A, B→D, D→B, D→E, E→F; 이 레슨 작업용 trace 프로그램의 실제 출력):

```text
start A                    cs=[A] stk=[A] idx/low: A0/0
A->B tree (push)           cs=[AB] stk=[AB] idx/low: A0/0 B1/1
B->C tree (push)           cs=[ABC] stk=[ABC] idx/low: A0/0 B1/1 C2/2
C->A on-stack: low         cs=[ABC] stk=[ABC] idx/low: A0/0 B1/1 C2/0
return C                   cs=[AB] stk=[ABC] idx/low: A0/0 B1/0 C2/0
B->D tree (push)           cs=[ABD] stk=[ABCD] idx/low: A0/0 B1/0 C2/0 D3/3
D->B on-stack: low         cs=[ABD] stk=[ABCD] idx/low: A0/0 B1/0 C2/0 D3/1
D->E tree (push)           cs=[ABDE] stk=[ABCDE] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4
E->F tree (push)           cs=[ABDEF] stk=[ABCDEF] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
  root F: pop {F} = comp 0
return F                   cs=[ABDE] stk=[ABCDE] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
  root E: pop {E} = comp 1
return E                   cs=[ABD] stk=[ABCD] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
return D                   cs=[AB] stk=[ABCD] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
return B                   cs=[A] stk=[ABCD] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
  root A: pop {DCBA} = comp 2
return A                   cs=[] stk=[] idx/low: A0/0 B1/0 C2/0 D3/1 E4/4 F5/5
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">반복 Tarjan: E→F 를 push 한 순간 — 호출 스택(cs)과 Tarjan 스택(stk)은 다르다</text>
  <rect x="30" y="58" width="290" height="170" rx="8" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <g stroke="currentColor" stroke-width="1.5">
    <circle cx="60" cy="90" r="18" class="f-warn"/><circle cx="170" cy="90" r="18" class="f-warn"/>
    <circle cx="115" cy="175" r="18" class="f-warn"/><circle cx="280" cy="90" r="18" class="f-warn"/>
    <circle cx="390" cy="90" r="18" class="f-mut"/><circle cx="480" cy="90" r="18" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="60" y="95">A</text><text x="170" y="95">B</text><text x="115" y="180">C</text>
    <text x="280" y="95">D</text><text x="390" y="95">E</text><text x="480" y="95">F</text>
    <text x="60" y="127" font-family="monospace">0/0</text><text x="170" y="127" font-family="monospace">1/0</text>
    <text x="115" y="212" font-family="monospace">2/0</text><text x="280" y="127" font-family="monospace">3/1</text>
    <text x="390" y="127" font-family="monospace">4/4</text><text x="480" y="127" font-family="monospace">5/5</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M78 90 L150 90" marker-end="url(#a10b)"/>
    <path d="M160 106 L127 159" marker-end="url(#a10b)"/>
    <path d="M102 160 L70 107" marker-end="url(#a10b)"/>
    <path d="M188 84 L260 84" marker-end="url(#a10b)"/>
    <path d="M262 98 L190 98" marker-end="url(#a10b)"/>
    <path d="M298 90 L370 90" marker-end="url(#a10b)"/>
    <path d="M408 90 L460 90" marker-end="url(#a10b)"/>
  </g>
  <text x="40" y="248" fill="currentColor">점선 = SCC {A,B,C,D}. 숫자 = idx/low (최종값)</text>
  <text x="545" y="56" fill="currentColor">cs (호출 스택)</text>
  <text x="640" y="56" fill="currentColor">stk (Tarjan)</text>
  <g stroke="currentColor">
    <rect x="545" y="204" width="70" height="26" class="f-mut"/><rect x="545" y="178" width="70" height="26" class="f-mut"/>
    <rect x="545" y="152" width="70" height="26" class="f-mut"/><rect x="545" y="126" width="70" height="26" class="f-mut"/>
    <rect x="545" y="100" width="70" height="26" class="f-acc"/>
    <rect x="640" y="204" width="70" height="26" class="f-mut"/><rect x="640" y="178" width="70" height="26" class="f-mut"/>
    <rect x="640" y="152" width="70" height="26" class="f-warn"/><rect x="640" y="126" width="70" height="26" class="f-mut"/>
    <rect x="640" y="100" width="70" height="26" class="f-mut"/><rect x="640" y="74" width="70" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="580" y="222">A</text><text x="580" y="196">B</text><text x="580" y="170">D</text><text x="580" y="144">E</text><text x="580" y="118">F</text>
    <text x="675" y="222">A</text><text x="675" y="196">B</text><text x="675" y="170">C</text><text x="675" y="144">D</text><text x="675" y="118">E</text><text x="675" y="92">F</text>
  </g>
  <text x="545" y="250" fill="currentColor">C 는 이미 return 했지만</text>
  <text x="545" y="266" fill="currentColor">low=0 이라 stk 에 남는다</text>
  <text x="10" y="292" fill="currentColor">cs 한 칸 = (정점, 다음 간선 위치 it) = 재귀판의 스택 프레임.</text>
  <text x="10" y="312" fill="currentColor">F, E 는 low==idx 라 혼자 성분. D, B 는 low 가 작아 남는다.</text>
  <text x="10" y="332" fill="currentColor">A 가 return 할 때 stk 에서 A 까지 {D,C,B,A} 를 한꺼번에 꺼낸다.</text>
</svg>
```

정확성 요지 (Tarjan 1972 의 증명 뼈대):

1. 한 성분에서 DFS 가 **처음** 들어간 정점 r 이 그 성분의 모든 정점을 자기 부분 트리에 포함한다 (r 에서 다 도달 가능하고, r 이 끝나기 전에 다 방문된다 — 백색 경로 정리).
2. 성분 안의 다른 정점 v 는 r 로 돌아가는 경로가 있어서 `low[v] < idx[v]`, r 은 자기보다 오래된 스택 정점에 닿지 못해서 `low[r] == idx[r]`.
3. r 이 끝날 때 스택에서 r 위에 있는 건 r 의 부분 트리 중 아직 성분이 없는 정점 = 정확히 그 성분.

`on[w]` 조건이 핵심이다: 이미 다른 성분으로 꺼내진 정점으로 가는 간선(교차 간선)은 low 를 낮추면 안 된다. 그 조건을 지우면 무작위 그래프 500개 중 **442개**가 틀린다 (7. 실수 2, 실제 실행).

### Part A — 재귀를 반복으로: 실제로 터지는 크기

재귀판 `strong(v)` 는 DFS 깊이만큼 스택 프레임을 쌓는다. 락 그래프는 작지만, 같은 도구를 빌드 의존 그래프·콜 그래프·SoC 신호 그래프에 쓰면 정점이 10^5~10^6 이고 사슬처럼 깊을 수 있다. `demo_recursion.c` 는 정점 N 개 사슬을 자식 프로세스에서 재귀판으로 돌린다 (실제 출력):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_recursion.c -o demo_rec && ./demo_rec
```

```text
RLIMIT_STACK soft = 8176 KiB (main thread)
N=  10000 recursive: ok, ncomp=10000, stack used ~1093 KiB (112 B/frame)
N=  50000 recursive: ok, ncomp=50000, stack used ~5468 KiB (112 B/frame)
N= 100000 recursive: CRASH, killed by signal 11 (SIGSEGV)
N= 200000 recursive: CRASH, killed by signal 11 (SIGSEGV)
N= 500000 recursive: CRASH, killed by signal 11 (SIGSEGV)
N=1000000 recursive: CRASH, killed by signal 11 (SIGSEGV)
```

(각 줄 뒤의 `iterative: ncomp=N` 줄은 생략 — 반복판은 모든 N 에서 정상.) `-O0` 빌드는 프레임이 **48 B** 로 오히려 작아서 N=100000 까지 살고 200000 에서 죽었다. `-O2` 는 루프 변수를 callee-saved 레지스터에 두고 그 레지스터들을 프레임에 저장하느라 112 B 가 됐다 — "최적화하면 스택을 덜 쓴다" 는 보장이 없다. 계산: 8176 KiB / 112 B ≈ 7.5만 프레임, / 48 B ≈ 17.4만 — 관찰과 맞는다.

펌웨어 관점: RTOS 태스크 스택은 보통 1~8 KB 다. 112 B 프레임이면 깊이 수십에서 끝난다. MISRA 스타일 규칙이 재귀를 금지하는 이유(C-A10)와 같은 이야기고, `ulimit -s` 를 올리는 건 절벽을 옮길 뿐이다. 반복판의 추가 메모리는 정점당 int 5개 + bool 1개 = 힙 O(V) 로 **예측 가능**하다.

### Part A — Kosaraju: 왜 역그래프인가

1단계 DFS 의 끝난 시각을 f(v) 라 하면, 성분 C 에서 성분 D 로 간선이 있을 때 max f(C) > max f(D) 다. 그래서 끝난 순서의 **역순**으로 보면 첫 정점은 "아무도 자기로 들어오지 않는" 소스 성분에 있다. 그 정점에서 **역그래프** G^T 로 DFS 하면 들어오는 쪽(원래 그래프에서 나가는 쪽)이 막혀 있어서 정확히 자기 성분만 닿는다. 원래 그래프로 DFS 하면 하류 성분까지 다 삼킨다 (7. 실수 4: 500개 중 464개 틀림).

| | Tarjan | Kosaraju |
|---|---|---|
| DFS 횟수 | 1 | 2 (G, G^T) |
| 추가 메모리 | idx·low·stk·on + 호출 스택 | 역그래프 (간선 E 개 복사) + order + 호출 스택 |
| 성분 번호 | **역 위상 순서** (교차 간선 u→v 면 comp[u] > comp[v]) | **위상 순서** (comp[u] < comp[v]) |
| 장점 | 한 번에 끝, 메모리 적음 | 이해·증명 쉬움, 2단계는 아무 순회나 가능 |

하네스의 `run_both()` 는 이 번호 순서 성질까지 검사한다.

### Part A — 교차 검증: 정규형과 제3의 기준

두 알고리즘의 성분 번호는 다르다(위 표). 그래서 `scc_canon()` 으로 "각 정점의 라벨 = 성분의 가장 작은 정점 번호" 로 바꾼 뒤 비교한다. 둘이 같은 버그를 공유할 수도 있으니 **전이 폐포**(Floyd–Warshall, `uint64_t` 비트셋, n ≤ 64)를 제3의 기준으로 둔다: u, v 가 서로 도달 가능 ⇔ 같은 SCC. 실제 출력:

```text
random: 500 graphs, 15679 vertices, 22844 edges: tarjan mismatches=0 kosaraju mismatches=0 order violations=0
```

손 계산 사례에서 "self-loop" 는 SCC 로는 셋 다 혼자다 — SCC 는 자기 루프를 구분하지 않는다. 그래서 `analyze()` 가 자기 루프를 따로 찾아 크기 1 성분도 보고한다.

### Part B — 증거 사이클: 가장 짧은 것 하나

SCC 는 "이 락들 사이에 순환이 있다" 까지만 말한다. 엔지니어가 고치려면 **구체적인 사이클 하나**와 그 간선을 만든 코드 위치가 필요하다. 성분의 가장 작은 락 s 에서, 성분 밖으로 나가는 간선을 무시하고 BFS 하면 처음 s 로 돌아오는 간선이 s 를 지나는 가장 짧은 사이클이다 (BFS 는 거리 순서로 정점을 꺼내니까). 중첩 성분 {A,B,C,D} 에서 A 를 지나는 사이클은 A→B→C→A(3), A→B→D→B→C→A 는 단순 사이클이 아니니 후보가 아님 — 테스트는 길이 3 을 확인한다. 성분 하나당 O(V+E) 이고 보고할 성분 수가 적으니 충분하다.

### Part B — 판정: 진짜 교착 후보만 남기기

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">게이트 락: 같은 순환이라도 공통 락 G 를 쥔 채 생긴 간선끼리는 동시에 성립할 수 없다</text>
  <text x="10" y="52" fill="currentColor" font-weight="bold">① 게이트 없음 (ABBA) — 교착 가능</text>
  <text x="20" y="82" fill="currentColor">T1</text>
  <rect x="60" y="66" width="160" height="24" class="f-mut" stroke="currentColor"/><text x="70" y="83" fill="currentColor">A 보유</text>
  <rect x="220" y="66" width="200" height="24" class="f-warn" stroke="currentColor"/><text x="230" y="83" fill="currentColor">B 대기 … 영원히</text>
  <text x="20" y="118" fill="currentColor">T2</text>
  <rect x="100" y="102" width="140" height="24" class="f-mut" stroke="currentColor"/><text x="110" y="119" fill="currentColor">B 보유</text>
  <rect x="240" y="102" width="180" height="24" class="f-warn" stroke="currentColor"/><text x="250" y="119" fill="currentColor">A 대기 … 영원히</text>
  <text x="440" y="90" fill="currentColor">A→B (T1) + B→A (T2)</text>
  <text x="440" y="110" fill="currentColor">스레드 다름, 게이트 서로소 → DEADLOCK</text>
  <text x="10" y="160" fill="currentColor" font-weight="bold">② 둘 다 G 아래 — 교착 불가 (GATED)</text>
  <text x="20" y="190" fill="currentColor">T1</text>
  <rect x="60" y="174" width="60" height="24" class="f-acc" stroke="currentColor"/><text x="70" y="191" fill="currentColor">G</text>
  <rect x="120" y="174" width="100" height="24" class="f-mut" stroke="currentColor"/><text x="130" y="191" fill="currentColor">A</text>
  <rect x="220" y="174" width="80" height="24" class="f-ok" stroke="currentColor"/><text x="230" y="191" fill="currentColor">B 얻음</text>
  <text x="20" y="226" fill="currentColor">T2</text>
  <rect x="100" y="210" width="200" height="24" class="f-mut" stroke="currentColor"/><text x="110" y="227" fill="currentColor">G 대기 (아무것도 안 쥠)</text>
  <rect x="300" y="210" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="310" y="227" fill="currentColor">G → B → A</text>
  <text x="440" y="198" fill="currentColor">B→A 를 만들려면 T2 가 G 를 쥐어야 하는데</text>
  <text x="440" y="218" fill="currentColor">G 는 T1 이 쥐고 있다 → 겹칠 수 없음</text>
  <text x="10" y="266" fill="currentColor">일반 규칙(GoodLock 계열): 스레드가 모두 다르고 게이트가 서로소일 때만 교착 후보 (연습 2)</text>
</svg>
```

`scc_verdict` 는 성분 **안쪽 간선 전부**를 본다:

| 판정 | 조건 | 의미 / 해야 할 일 |
|---|---|---|
| SELF-LOOP | 크기 1 + 자기 루프 | 같은 락(이름)을 쥔 채 다시 잡음. 재귀 뮤텍스가 아니면 **혼자서도** 즉시 교착. 같은 클래스의 다른 인스턴스(예: 노드 락 두 개)면 lockdep 의 subclass/nesting 주석처럼 이름을 나눠야 한다 (OS-M11) |
| GATED | 안쪽 간선 전부의 게이트 교집합 ≠ ∅ | 이 성분의 **모든** 사이클이 G 아래 → 교착 불가. 단, G 를 나중에 없애면 바로 살아나는 잠복 버그라 "보고는 하되 등급을 낮춘다" |
| SINGLE-THREAD | 안쪽 간선을 만든 스레드가 하나 | 이 로그만으로는 교착 못 함(자기 자신을 기다릴 수 없다). 하지만 **같은 코드를 다른 스레드가 실행하면** 진짜 ABBA 다. 예: init 경로와 런타임 경로를 지금은 한 태스크가 다 돌리지만 나중에 나뉠 수 있다. lockdep 도 스레드를 따지지 않고 보고한다 → WARN 으로 남긴다 |
| DEADLOCK | 그 밖 | 고친다: 순서 통일 / 한쪽을 trylock+후퇴 / 락 분리 |

**왜 성분 단위인가 — 그리고 그 대가.** 성분 안의 모든 사이클은 안쪽 간선만 쓴다. 그러니 안쪽 간선 전부가 G 를 갖고 있으면 모든 사이클이 G 아래라는 결론은 **정확**하다(놓치는 진짜 교착 없음). 반대로 교집합이 비었다고 모든 사이클이 진짜는 아니다. `LOG_MIXED` 는 A↔B 를 G 아래에서, B↔C 를 게이트 없이 만든다. A↔B 사이클은 무해하고 B↔C 는 진짜인데 둘이 한 성분이라 성분 전체가 DEADLOCK 이다. 게다가 증거로는 **무해한** A→B→A 가 나온다 (보고서 실제 출력, 줄 정리):

```text
[DEADLOCK] SCC {A, B, C}  threads: T1 T2 T3 T4
  A          -> B           t=T1       holds @m.c:2, takes @m.c:3  (log line 3, seen 1x)
  B          -> A           t=T2       holds @m.c:5, takes @m.c:6  (log line 9, seen 1x)
```

증거는 "가장 작은 락을 지나는 가장 짧은 사이클" 일 뿐 "게이트 없는 사이클" 이 아니다 — 읽는 사람은 성분 목록 전체를 봐야 한다. 사이클 단위로 정확히 거르려면 사이클을 열거해야 하는데 그게 바로 다음 문제다. 이 도구는 "**거짓 경보는 내도 진짜는 안 놓친다**" 쪽을 택했다. (게이트를 성분 하나에서 정밀하게 다루는 개선: 게이트 G 를 가진 간선을 지운 부분 그래프에서 SCC 를 다시 돌리는 식 — 연습 아이디어로 남긴다.)

### Part B — SCC ≠ 모든 사이클, 그리고 O(V+E)

락 7개짜리 완전 그래프에도 단순 사이클은 2365개다 (연습 4 실제 출력: K_8 → 16064, K_9 → 125664 = Σ C(n,k)(k−1)!). 모든 기본 사이클을 나열하는 Johnson 알고리즘(1975)은 사이클 수 c 에 대해 O((V+E)(c+1)) 이라 **출력 크기 자체가 지수적**일 수 있다. 데드락 보고에 필요한 건 "어디에 순환이 있나 + 증거 하나" 이므로 SCC 가 맞는 도구다.

O(V+E) 스케치 (Tarjan): 각 정점은 한 번 push(`idx==-1` 일 때만), 한 번 pop(호출 스택), 한 번 Tarjan 스택에서 꺼내진다. 각 간선은 `it[]` 가 단조 증가하므로 정확히 한 번 본다. 간선 하나당 일은 O(1). Kosaraju 는 G 와 G^T 에 같은 논리 두 번 + G^T 를 만드는 계수 정렬 O(V+E). 이 레슨 `analyze()` 전체는 SCC + (보고 성분마다 BFS) + Kahn, 보고 성분이 k 개면 O(V+E) + O(k(V+E)).

### Part C — 펌웨어 트레이스 보고서

`solution.c` 가 마지막에 찍는 실제 보고서:

```text
== lock-order report: 7 locks, 13 edges, 4 threads, 0 warnings ==
[DEADLOCK] SCC {spi_bus, dma_ch2, pwr_domain}  threads: spi_task dma_task pm_task
  spi_bus    -> dma_ch2     t=spi_task holds @spi.c:88, takes @spi.c:95  (log line 3, seen 1x)
  dma_ch2    -> pwr_domain  t=dma_task holds @dma.c:140, takes @dma.c:152  (log line 9, seen 1x)
  pwr_domain -> spi_bus     t=pm_task  holds @pm.c:61, takes @pm.c:77  (log line 13, seen 1x)
[GATED by board_cfg] SCC {clk_tree, pinmux}  threads: pm_task log_task
  clk_tree   -> pinmux      t=pm_task  holds @clk.c:12, takes @pinmux.c:40  (log line 20, seen 1x)
  pinmux     -> clk_tree    t=log_task holds @pinmux.c:55, takes @clk.c:19  (log line 26, seen 1x)
suggested lock order: spi_bus* dma_ch2* pwr_domain* board_cfg clk_tree pinmux log_buf   (* = in a DEADLOCK cycle: break it first)
summary: deadlock=1 gated=1 single-thread=0 self=0
```

읽는 법:

- **교착 시나리오**: SPI 태스크가 버스를 쥐고 TX DMA 채널을 기다리는 동안, DMA 완료 처리(bottom half)가 채널을 쥐고 전원 도메인을 기다리고, 전원 관리 태스크가 서스펜드 중 도메인을 쥐고 SPI 플래시에 deep power-down 명령을 보내려 버스를 기다린다. 세 태스크가 같은 순간에 각자의 첫 락을 쥐는 일은 드물다 — 그래서 필드에서 "1주일에 한 번" 이다.
- **고치는 법** (어느 Coffman 조건을 깨나): (1) dma_task 가 dma_ch2 를 **먼저 풀고** 전원 참조 감소는 지연 작업으로(점유 대기 깨기, 연습 3 의 고친 트레이스) (2) pm_task 가 도메인을 잡기 전에 SPI 를 정리(순서 통일: spi_bus → pwr_domain 이 전역 규칙이 됨 → 순환 대기 깨기) (3) pm 쪽 `spi_bus` 를 trylock + 실패 시 서스펜드 연기(비선점 깨기에 가까움 — 후퇴).
- **GATED**: clk_tree↔pinmux 는 순서가 반대지만 두 경로 모두 board_cfg 아래라 지금은 무해. 누가 "board_cfg 가 너무 크다" 며 쪼개는 날 살아난다 → 주석으로 남길 가치가 있다.
- **락 계층**: 축약 그래프의 위상 순서. `*` 표시 성분은 순서를 정할 수 없으니(순환) 먼저 고친다. log_buf 가 맨 끝 = "로거 락을 쥔 채 다른 락을 잡지 마라" 는 규칙이 이미 지켜지고 있다는 뜻. 고친 뒤 이 순서를 레벨 표로 박아 넣고 런타임에 검사하는 것이 연습 3 이다.

트레이스는 어떻게 얻나: 펌웨어에서는 락 래퍼에 `trace_lock(op, lock_id, __FILE__, __LINE__)` 를 넣어 RAM 링 버퍼(DS-M10)에 쌓고 UART/SWO/RTT 로 뽑는다. ISR 은 뮤텍스를 잡을 수 없으니(C-M14) ISR 쪽 자원은 "인터럽트 금지 구간" 이나 스핀락을 가상의 락으로 기록해야 그래프에 나타난다. SEGGER SystemView·Percepio Tracealyzer 같은 RTOS 트레이서도 뮤텍스 이벤트를 기록할 수 있다(제품별 형식은 확인 안 함) — 이 레슨의 로그 형식으로 변환하는 스크립트만 있으면 된다.

**한계 — 정직하게.** (1) 로그에 **실행된 경로만** 나온다: 테스트가 안 건드린 경로의 역순은 못 본다(거짓 음성). (2) happens-before 를 모른다: T2 가 T1 의 join 뒤에만 실행된다면 A→B / B→A 는 겹칠 수 없는데 보고한다(거짓 양성). (3) 이름 = 클래스로 본다: 같은 이름의 인스턴스 둘을 중첩하면 SELF-LOOP 이 뜬다. 동적 락을 주소로 이름 붙이면 주소 재사용이 서로 다른 락을 합친다.

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6 (`hw.perflevel0/1/2.physicalcpu` = 2/4/6), Apple clang version 21.0.0 (clang-2100.0.123.102). 측정 직전 `uptime` load averages: **9.63 6.54 4.56** (다른 작업이 같이 돌고 있음 — 절대값보다 **비율**을 믿을 것). 각 값 = 5번 중 최솟값, 매 회 두 결과의 정규형 일치 확인 (실제 출력, 열 정리):

| 그래프 | V | E | SCC 수 | 가장 큰 SCC | Tarjan ms | Kosaraju ms | K/T | ns/(V+E) T · K |
|---|---|---|---|---|---|---|---|---|
| random | 100000 | 100000 | 99999 | 2 | 2.50 | 2.73 | 1.09 | 12.5 · 13.6 |
| random | 100000 | 1000000 | 13 | 99988 | 5.70 | 10.63 | 1.87 | 5.2 · 9.7 |
| random | 1000000 | 1000000 | 999516 | 464 | 52.60 | 56.53 | 1.07 | 26.3 · 28.3 |
| random | 300000 | 1000000 | 24111 | 275890 | 14.01 | 21.66 | 1.55 | 10.8 · 16.7 |
| chain | 1000000 | 999999 | 1000000 | 1 | 11.04 | 11.25 | 1.02 | 5.5 · 5.6 |
| ring | 1000000 | 1000000 | 1 | 1000000 | 10.17 | 11.89 | 1.17 | 5.1 · 5.9 |

```text
log: 1000744 lines (19.5 MB), rc=0/0, 1000 locks, 1998 edges, deadlock=1 single-thread=0  parse 64.9 ms (65 ns/line)  analyze 0.03 ms
```

바로 다음 실행(load 12.62)에서는 같은 줄이 Tarjan 1.90/5.68/51.72/12.37/9.90/10.50 ms, K/T 1.45/2.43/0.94/1.65/1.05/1.04, 로그 파싱 42.9 ms 였다. 읽을 것:

- 간선이 정점보다 훨씬 많을 때(E/V = 10) Kosaraju 가 1.9~2.4배 느리다 — 역그래프를 만들며 간선을 복사하고 간선을 두 번 훑기 때문. E ≈ V 면 두 알고리즘이 거의 같다(0.94~1.17).
- V=10^6 무작위가 정점·간선당 가장 비싸다(~26 ns): 정점 배열들이 캐시에 안 들어가고 접근이 무작위라 캐시 미스가 지배한다. 같은 크기의 사슬·고리(순차 접근)는 ~5 ns.
- 실무 로그(락 1000개, 이벤트 10^6 줄)에서는 **파싱이 전부**다(43~65 ms). SCC 분석은 0.03 ms — 락 그래프는 이벤트 수가 아니라 **락 종류 수**만큼 크다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 40/40 passed (5회 반복 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 40/40, 보고 없음
```

스레드를 쓰지 않는 오프라인 도구라 TSan 은 해당 없음. 이 맥의 ASan 은 `detect_leaks=1` 을 "not supported on this platform" 으로 거부한다(실제 메시지) — 누수 검사는 리눅스(LSan)에서 해야 한다.

## 7. 자주 하는 실수

**1. 재귀 Tarjan 을 그대로 제출한다.** 면접 화이트보드에선 괜찮지만 "그래프가 10^6 이면?" 에서 무너진다. 이 맥 `-O2` 에서 사슬 10^5 에 SIGSEGV(위 demo). → 명시적 스택 `(v, it)`.

**2. 스택에 없는 정점으로 가는 간선에서도 low 를 낮춘다.**

```c
} else if (idx[w] < low[v]) {          /* 틀림: on[w] 확인 없음 */
```

이미 다른 성분으로 확정된 정점(교차 간선)의 작은 idx 를 가져와 성분 둘을 합친다. 이 한 줄 변형을 실제로 돌리면 무작위 500개 중 **442개** 불일치, 31/40. → `on[w] &&` 필수. (스택 위 정점에 대해 `idx[w]` 대신 `low[w]` 를 쓰는 변형은 SCC 에서는 결과가 같다 — 이 레슨의 무작위 500개에서도 실제로 0 불일치, 40/40. 하지만 단절점·다리 문제로 옮기면 틀린다고 알려져 있으니 원래 형태 `idx[w]` 를 외워 두자.)

**3. 해제를 "스택 맨 위만" 처리한다.** 순서 밖 해제가 경고로 무시되고 그 락이 계속 쥐어진 것으로 남아 가짜 간선이 생긴다. 실제 변형: 무작위 SCC 는 무관하지만 `acq A, acq B, rel A, acq C` 테스트가 FAIL(39/40, A→C 가 생김). → 위에서부터 찾아서 그 자리 제거 (`memmove`).

**4. Kosaraju 2단계를 원래 그래프로, 또는 1단계 순서를 앞에서부터 돈다.** 둘 다 하류 성분을 삼킨다. 실제: 원래 그래프로 → 500개 중 464개 불일치, 순서를 정방향으로 → 463개 불일치. → G^T 로, 끝난 순서의 **역순**으로.

**5. 두 알고리즘의 `comp[]` 를 그대로 비교한다.** 번호 체계가 반대(역 위상 vs 위상)라 맞는 구현도 다르게 보인다. → 정규형(성분 최소 정점) 비교 + 제3의 기준(전이 폐포).

**6. 판정을 증거 사이클 하나로 한다.** 증거가 우연히 G 아래 사이클(A→B→A)이면 같은 성분의 진짜 B↔C 를 "GATED" 로 숨긴다(`LOG_MIXED`). → 성분 안쪽 간선 **전부**의 교집합으로 판정(보수적).

**7. 스레드를 64개 넘게 받는다.** `1ull << thread` 는 thread ≥ 64 에서 UB (C11 6.5.7p3: 시프트 양이 폭 이상). → 파서가 64 에서 형식 오류로 막는다. 더 필요하면 비트셋 배열.

**8. 락을 주소로 이름 붙인다.** 해제된 락의 주소를 새 락이 재사용하면 서로 다른 락이 한 정점이 된다(가짜 사이클). → 생성 위치/타입 = 클래스로 이름 붙이고(lockdep 의 lock class 개념, OS-M11), 같은 클래스 중첩은 subclass 이름으로.

## 8. 면접 Q&A

<details><summary>Q1. 교착의 4가지 필요 조건과 각각을 깨는 방법은? 이 도구는 어디에 해당하나요?</summary>

Coffman·Elphick·Shoshani(1971): **상호 배제**(자원을 한 번에 하나만) · **점유 대기**(쥔 채 더 기다림) · **비선점**(남이 뺏을 수 없음) · **순환 대기**. 넷이 모두 성립해야 교착. 깨는 법: 상호 배제 → lock-free·코어별 데이터·SPSC 큐(DS-A01). 점유 대기 → 필요한 락을 한 번에(전부 trylock, 하나라도 실패하면 다 풀기), 또는 "쥔 채 기다리지 않기"(Part C 고침 1). 비선점 → trylock + 후퇴, DB 의 희생자 abort(STOR-M08), 최후의 워치독 리셋. 순환 대기 → 전역 락 순서/계층(OS-M10 의 lowest-fork-first, STOR-M10 의 위→아래 래치). 이 도구는 **순환 대기가 생길 수 있는지**를 기록에서 찾고, 고친 뒤에는 그 순서를 계층으로 제안한다. 꼬리: "실무에서 가장 많이 쓰는 건?" → 순환 대기 깨기(락 순서). OSTEP 32장도 "가장 실용적인 예방"으로 꼽는다. 꼬리 2: "우선순위 상한(priority ceiling)은?" → RTOS 에서 상한 프로토콜은 교착도 막는다(RTOS-M05, OS-M10).

</details>

<details><summary>Q2. 로그에 사이클이 있으면 반드시 데드락인가요? 반대로 없으면 안전한가요?</summary>

둘 다 아니다. 사이클은 **가능성**이다: 게이트 락(공통 G), 같은 스레드, happens-before(T2 가 T1 join 뒤에만 실행), 실제로는 동시에 못 도는 모드(부트 vs 런타임) 때문에 영원히 안 일어날 수 있다 → GATED/SINGLE-THREAD 로 등급을 낮추고 나머지는 사람이 본다. 반대로 사이클이 없어도, 로그는 **실행된 경로만** 담는다 — 커버리지 밖의 역순은 못 본다. 또 락이 아닌 대기(세마포어 대기, 이벤트 플래그, 큐 가득 참, 조건 변수)는 이 그래프에 안 나온다. 꼬리: "그럼 커버리지는 어떻게 올리나?" → 스트레스·퍼징으로 경로를 늘리고, 같은 그래프를 **여러 실행에서 합쳐** 분석(간선 합집합)한다 — 오프라인 도구의 장점.

</details>

<details><summary>Q3. lockdep 은 이걸 어떻게 런타임에 하나요? 오프라인 SCC 와 뭐가 다르죠?</summary>

Linux lockdep(`CONFIG_PROVE_LOCKING`)은 락을 **클래스**(보통 초기화 위치) 단위로 보고, 락을 잡을 때 held 스택의 락 → 새 락 의존을 그래프에 추가한다. 새 의존이 처음 생길 때만 "새 락에서 쥔 락으로 돌아가는 경로가 있나" 를 탐색해, 있으면 두 경로의 스택 트레이스와 함께 보고한다 — 실제로 멈추기 **전에**, 두 스레드가 순차로 돌아도. OS-M11 이 그 미니판이다. 차이: 온라인은 **첫 사이클에서** 바로 알려 주고 그 순간의 콜스택이 있다. 오프라인은 로그 전체를 보므로 **모든 순환 영역**을 한 번에, 여러 실행을 합쳐, 게이트·스레드 정보로 분류할 수 있고 대상 시스템에 비용을 거의 안 준다(이벤트 기록만). 꼬리: "lockdep 은 이미 본 간선을 어떻게 싸게 처리?" → 의존 체인 해시 캐시로 정상 상태에서는 탐색 없음(세부는 커널 문서 참고, 정확한 자료구조는 확인 안 함).

</details>

<details><summary>Q4. Tarjan 과 Kosaraju 중 무엇을 쓰겠어요? 왜 둘 다 O(V+E) 인가요?</summary>

기본은 Tarjan: DFS 한 번, 역그래프 불필요, 측정에서도 E/V 가 클 때 Kosaraju 가 1.9~2.4배 느렸다. Kosaraju 는 증명이 쉽고 2단계가 아무 순회나 되므로 분산·병렬 변형(예: 전진·후진 도달 집합의 교집합으로 성분을 쪼개는 방식)의 출발점이다. 둘 다 각 정점을 상수 번 push/pop, 각 간선을 상수 번 본다(반복판에서 `it[]` 가 단조 증가). 꼬리: "결과 번호의 의미는?" → Tarjan 은 역 위상, Kosaraju 는 위상 순서 — Part C 락 계층이 바로 이것. 꼬리 2: "메모리를 더 줄이려면?" → Tarjan 은 idx 와 low 를 한 배열로 합치는 공간 절약 변형이 있다(Pearce 의 변형 — 세부 확인 안 함).

</details>

<details><summary>Q5. 재귀 DFS 가 왜 문제인가요? 반복으로 바꾸는 요령은?</summary>

DFS 깊이 = 호출 깊이. 이 맥에서 메인 스레드 스택 8176 KiB, `-O2` 프레임 112 B → 사슬 10^5 에서 SIGSEGV 를 실제로 봤다. 펌웨어 태스크 스택은 KB 단위라 훨씬 일찍 죽고, 스택 오버플로는 보통 조용히 옆 메모리를 덮는다(MPU 가드가 없으면). 요령: 재귀 함수의 **지역 상태 전부**를 프레임 구조체로 만든다. Tarjan 은 (v, 다음 간선 위치) 둘이면 되고, "호출에서 돌아온 직후 하던 일"(부모 low 갱신)은 pop 직후에 한다. 꼬리: "꼬리 재귀 최적화로 안 되나?" → Tarjan 의 재귀는 호출 뒤에 할 일(low 갱신)이 있어 꼬리 호출이 아니다.

</details>

<details><summary>Q6. 사이클을 모두 나열해서 보여 주면 더 친절하지 않나요?</summary>

사이클 수는 지수적이다. 완전 그래프 K_9 하나에 단순 사이클 125664개(연습 4 실측). Johnson(1975)은 사이클당 O(V+E) 로 최적에 가깝지만 출력 자체가 폭발한다. 데드락 보고의 목적은 "어느 락 묶음을 고칠지" 이므로 SCC 마다 **증거 하나**(가장 짧은 것) + 성분 전체 목록이면 충분하다. 고친 뒤 다시 돌리면 다음 증거가 나온다. 꼬리: "모든 사이클을 끊는 최소 간선 집합은?" → 최소 피드백 간선 집합(feedback arc set)은 NP-hard — 그래서 실무는 사람이 락 순서를 정하고 도구는 위반을 찾는다.

</details>

<details><summary>Q7. 게이트 락으로 거르면 놓치는 게 없나요? 한 스레드가 만든 사이클은?</summary>

게이트 판정은 "성분 안쪽 간선 **전부**가 같은 G 를 매번 쥐고 있었다" 일 때만 거른다 — 그러면 그 성분의 모든 사이클이 G 아래라 동시에 성립 불가, 놓치는 것 없음. 대가는 반대 방향의 거짓 경보(LOG_MIXED). 더 정밀한 규칙은 "사이클 간선들의 게이트가 서로소이고 스레드가 모두 달라야 후보"(GoodLock 계열, 연습 2) — 하지만 사이클 단위라 열거 비용 문제가 생긴다. 한 스레드 사이클은 이 실행에선 못 멈추지만 코드 경로 두 개가 순서를 어긴 것이므로 버그다: 나중에 그 경로가 다른 태스크로 옮겨지면 진짜 ABBA. lockdep 도 스레드를 구분하지 않고 보고한다. 꼬리: "G 가 reader-writer 락이면?" → 둘 다 read 로 쥐면 서로 막지 않으므로 게이트가 아니다 — 모드를 기록해야 한다.

</details>

<details><summary>Q8. ISR 이나 멀티코어 MCU 에서는 이 분석이 어떻게 바뀌나요?</summary>

ISR 은 뮤텍스를 기다릴 수 없다(C-M14) — 대신 인터럽트 금지/스핀락으로 보호한다. 단일 코어에서 "태스크가 락 L 을 쥔 채 인터럽트 허용 → ISR 이 L 을 원함" 은 그래프의 사이클이 아니라 **자기 교착**(ISR 이 태스크를 선점했으니 태스크는 L 을 못 푼다)이다. 그래서 lockdep 은 IRQ 상태(hardirq-safe/unsafe)를 따로 추적한다. 이 도구로 흉내내려면 "irq_off" 를 가상의 락으로 기록하고, ISR 에서 잡힌 락은 "ISR 컨텍스트" 표시를 달아 "인터럽트 허용 상태에서 쥔 적 있음" 과 충돌을 검사해야 한다(이 레슨 범위 밖). 듀얼 코어(STM32H7, nRF5340)에서 HSEM/스핀락은 진짜 순환이 생길 수 있어 그래프 분석이 그대로 적용된다. 꼬리: "워치독은?" → 교착 후 복구(비선점 깨기의 최후 수단), 예방이 아니다.

</details>

## 9. 추가 연습

### 연습 1 — wait-for 그래프: "지금" 멈췄나

락 순서 그래프는 **가능성**(과거 기록)이다. 반면 덤프 순간의 스냅샷 — 락 l 의 주인 `owner[l]`, 스레드 t 가 기다리는 락 `waiting[t]` — 로 만든 wait-for 그래프(스레드 → 기다리는 락의 주인)는 **지금 실제로** 교착인지 알려 준다. 스레드당 나가는 간선이 최대 하나다. SCC 없이 O(T) 로 사이클을 찾아라. 테스트: OS-M10 의 철학자 5명(P_i 가 F_i 쥐고 F_{i+1} 대기) → 사이클 길이 5, P4 가 안 기다리면 사이클 없음.

<details><summary>답 보기</summary>

나가는 간선이 하나뿐인 "함수 그래프" 는 각 정점에서 따라가기만 하면 결국 (a) 끝(기다리지 않는 스레드)에 닿거나 (b) 사이클에 들어간다. 색 3개(미방문 / 이번 걸음 / 끝남)로 걸음마다 경로를 기록하고, "이번 걸음" 색을 다시 만나면 그 지점부터가 사이클. 각 스레드는 한 번만 칠해지니 O(T). STOR-M08 의 DB 교착 탐지기(대기 → 보유자)가 같은 그래프이고, 공유 락처럼 한 스레드가 여러 주인을 기다릴 수 있으면 출력 차수가 늘어 일반 사이클 탐지(DFS/SCC)로 돌아간다.

```c
while (t >= 0 && color[t] == 0) { color[t] = 1; path[np++] = t;
    t = waiting[t] >= 0 ? owner[waiting[t]] : -1; }
if (t >= 0 && color[t] == 1) { /* path 안의 t 부터 끝까지 = 사이클 */ }
```

</details>

### 연습 2 — GoodLock 규칙: 사이클 단위 정밀 판정

후보 사이클의 간선 i 가 스레드 `thread[i]` 에 의해 게이트 집합 `gates[i]`(비트마스크)를 쥔 채 생겼다. 이 사이클이 **동시에** 성립할 수 있는지 판정하라. 본문의 "모든 간선 공통 게이트" 규칙이 못 거르는 경우(게이트가 간선 일부에만 공통)를 테스트로 보여라.

<details><summary>답 보기</summary>

교착 상태에서는 사이클의 k 개 대기가 같은 순간에 성립한다. 그러려면 (1) 스레드가 모두 달라야 하고(한 스레드는 한 번에 하나만 기다린다) (2) 두 간선이 같은 게이트를 공유하면 안 된다(둘 다 그 순간 G 를 쥐고 있어야 하는데 G 는 하나). 쌍마다 검사, O(k²):

```c
for (i) for (j > i) if (thread[i] == thread[j] || (gates[i] & gates[j])) return false;
```

3-사이클에서 G 가 간선 1·3 에만 있으면 공통 교집합은 ∅ 이라 본문 규칙은 DEADLOCK 이지만, 이 규칙은 "불가능" 으로 거른다. 대가: 사이클 단위라 증거 사이클 하나만 검사하면 같은 성분의 다른 사이클을 놓칠 수 있다 — 그래서 본문은 성분 단위(보수적)를 택했다. 문헌에선 Havelund 의 GoodLock 과 그 확장(게이트/가드 락)이 이 아이디어다(세부 정의는 논문마다 다름 — 확인 안 함).

</details>

### 연습 3 — 선언된 락 계층 검사기

Part C 보고서로 순서를 정했다: `board_cfg 10 < pwr_domain 20 < spi_bus 30 < dma_ch2 40 < log_buf 90`. 이벤트 열에서 "새로 잡는 락의 레벨은 그 스레드가 쥔 모든 락보다 엄격히 커야 한다" 를 검사해 첫 위반 이벤트를 찾아라. 원래 트레이스와, dma_task 가 dma_ch2 를 먼저 푸는 고친 트레이스를 모두 검사하라.

<details><summary>답 보기</summary>

스레드별 held 목록을 유지하고 획득마다 `max(level[held]) < level[new]` 확인. 원래 트레이스는 이벤트 5 (dma_task 가 dma_ch2(40) 쥔 채 pwr_domain(20))에서 위반, 고친 트레이스는 -1. 이게 펌웨어에서 가장 싸게 쓰는 방어다: 락 생성 시 레벨을 주고 `lock()` 래퍼에서 태스크별 "현재 최대 레벨" 하나만 비교하면 O(1) — 디버그 빌드에서 assert. 그래프 분석(오프라인, 이 레슨)은 **레벨을 정하는 데**, 레벨 검사(런타임)는 **지키는 데** 쓴다. 레벨이 같은 락을 둘 잡아야 하면(노드 두 개) 주소 순서 같은 부차 규칙이 필요하다(OSTEP 32장 "락 주소 순서로 잡기").

</details>

### 연습 4 — SCC 하나, 사이클 12만 개

완전 유향 그래프 K_n(n=2..9, 자기 루프 없음)의 단순 사이클 수를 백트래킹으로 세고 공식 Σ_{k=2..n} C(n,k)(k−1)! 과 비교하라. SCC 는 항상 1개다.

<details><summary>답 보기</summary>

각 사이클을 "가장 작은 정점에서 출발할 때만" 센다 — 출발점 s 에서 s 보다 큰 정점만 쓰며 DFS, s 로 돌아오는 간선을 만나면(경로 정점 ≥ 2) 하나. 공식: 크기 k 정점 집합을 고르고(C(n,k)), 그 위의 순환 순서는 (k−1)! 가지. 실제 출력 (K_2..K_9 중 일부 줄):

```text
K_2: V=2 E=2  SCC=1  simple cycles=1 (formula 1)
K_5: V=5 E=20  SCC=1  simple cycles=84 (formula 84)
K_7: V=7 E=42  SCC=1  simple cycles=2365 (formula 2365)
K_8: V=8 E=56  SCC=1  simple cycles=16064 (formula 16064)
K_9: V=9 E=72  SCC=1  simple cycles=125664 (formula 125664)
```

간선 72개짜리 그래프에 사이클 12만 개. "사이클을 다 보여 달라" 는 요구에 "SCC + 증거 하나" 로 답하는 근거다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- 락 순서 그래프: "A 를 쥔 채 B" 마다 간선 A→B (쥔 것 **전부** → 새 것) + 스레드·위치·게이트 교집합. 실제로 안 멈춘 순차 실행에서도 순환이 보인다 — 그게 가치.
- 모든 사이클은 한 SCC 안에 있다 → 크기 ≥ 2 또는 자기 루프인 SCC = 순환 영역. O(V+E). 사이클 나열은 지수적(K_9 에 125664개) — SCC + 증거 하나.
- Tarjan: idx/low, 스택 위 정점(`on[w]`)으로만 low 갱신, low==idx 면 뿌리 → 스택에서 꺼낸다. 성분은 **역 위상** 순서. Kosaraju: post-order → G^T 에서 역순, **위상** 순서.
- 재귀 금지: 이 맥 `-O2` 112 B/프레임, 사슬 10^5 에서 SIGSEGV. 프레임 = (v, 다음 간선 위치) 로 명시적 스택.
- 대조 테스트는 정규형(성분 최소 정점) + 제3의 기준(전이 폐포). 번호를 직접 비교하지 않는다.
- 판정: SELF-LOOP(같은 락 재획득) · GATED(안쪽 간선 전부 공통 G) · SINGLE-THREAD(경고, 여전히 버그) · DEADLOCK. 성분 단위 = 진짜는 안 놓치고 거짓 경보는 낼 수 있음.
- 축약 그래프의 위상 순서 = 락 계층 제안 → 펌웨어에선 레벨 표 + `lock()` 래퍼 O(1) 검사로 지킨다.
- 같은 순환 대기: 락(OS-M10/M11) · DB wait-for(STOR-M08) · 래치 순서(STOR-M10) · NoC 채널(SOCX-M05) · PFC 버퍼(DCN-M05).

## 참고자료

- R. E. Tarjan, "Depth-first search and linear graph algorithms", *SIAM Journal on Computing* 1(2), 1972 — SCC 알고리즘 원 논문.
- M. Sharir, "A strong-connectivity algorithm and its applications in data flow analysis", *Computers & Mathematics with Applications* 7(1), 1981 — Kosaraju 알고리즘(Kosaraju 는 미출판, 1978 무렵)의 출판본. 서지는 기억에 의존 — 확인 권장.
- D. B. Johnson, "Finding all the elementary circuits of a directed graph", *SIAM Journal on Computing* 4(1), 1975 — 모든 기본 사이클 열거, O((V+E)(c+1)).
- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), June 1971 — 교착의 네 조건.
- R. & A. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, 32장 "Common Concurrency Problems" (v0.91 PDF p.379–393) — 네 조건, 락 순서, 주소 순서 팁, 탐지 후 복구: <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-bugs.pdf>
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, "Strongly connected components" 절 — Kosaraju 증명(끝난 시각 보조정리), 백색 경로 정리.
- Linux kernel — Runtime locking correctness validator (lockdep): <https://docs.kernel.org/locking/lockdep-design.html>
- K. Havelund, "Using runtime analysis to guide model checking of Java programs", SPIN 2000 — GoodLock; S. Bensalem, K. Havelund, "Dynamic deadlock analysis of multi-threaded programs", 2005 — 게이트(가드) 락으로 거짓 경보 줄이기. 서지·정의 세부는 기억에 의존 — 확인 권장.
- W. J. Dally, C. L. Seitz, "Deadlock-free message routing in multiprocessor interconnection networks", *IEEE Trans. Computers* C-36(5), 1987 — 채널 의존 그래프 (SOCX-M05).
- 관련 레슨: OS-M10 (4조건·철학자), OS-M11 (mini lockdep, 온라인), STOR-M08 (wait-for 그래프·희생자), STOR-M10 (래치 순서), SOCX-M05 (CDG), DCN-M05 (PFC 버퍼 의존), RTOS-M05 (우선순위 상한), C-M14 (ISR 공유 데이터), C-A10 (재귀 금지 등 MISRA 스타일), DS-M10 (트레이스 링 버퍼)
