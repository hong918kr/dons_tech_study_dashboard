---
id: ALGO-M05
time: 80분
updated: 2026-11-11
---
# ALGO-M05 위상 정렬: 드라이버 초기화 순서

## 학습 목표

- 드라이버 의존성("imu 는 spi1·pmic·gpio 뒤에")을 **간선 방향을 틀리지 않고** 그래프로 만들고(간선 u→v = u 먼저), **Kahn 알고리즘**으로 초기화 순서를 낸다. 진입 차수(in-degree) 불변식과 "k < n 이면 사이클" 의 증명을 말할 수 있다.
- 준비된 드라이버가 여럿일 때 **가장 작은 id 먼저** 라는 결정적 tie-break 를 min-heap(O(E + V log V))과 선형 스캔(O(V² + E)) 두 가지로 구현하고, 그 결과가 **사전순 최소 위상 순서(유일)** 임을 귀납으로 증명한다. 어느 쪽이 언제 싼지 실측으로 안다.
- 순환 의존이 있으면 **3색(WHITE/GRAY/BLACK) DFS** 로 실제 사이클 경로(`i2c1 -> pmic -> i2c1`)를 찾아 보고한다. 재귀 없이 명시적 스택으로, 왜 2색(visited)으로는 틀리는지까지.
- 펌웨어 도구로 묶는다: 이름 목록 파싱 → 순서 + 병렬 초기화 단계(wave = 최장 경로) → 사이클이면 경로 + "올라간 것 / 막힌 것" 보고. 리눅스 deferred probe 식 재시도와 비용을 비교한다.

## 1. 문제

"부팅 순서를 하드코딩했더니 보드 리비전이 바뀔 때마다 깨진다 — 의존성 목록에서 순서를 계산해 보라." 면접에서는 Course Schedule 류 문제로 나오지만, 꼬리질문은 펌웨어 쪽으로 간다: 순서가 매번 같아야 하나? 사이클이면 무엇을 출력하나? 병렬로 올리면 몇 단계? 런타임에 의존성을 모르면?

용어부터 고정한다. 의존성 목록의 한 줄 `imu: spi1 pmic gpio` 는 "imu 는 spi1, pmic, gpio 가 **먼저** 올라와 있어야 한다" 는 뜻이다. 그래프 간선은 **공급자(supplier) → 소비자(consumer)**, 즉 `spi1 → imu`, `pmic → imu`, `gpio → imu` 로 둔다. 그러면 "간선 u→v 마다 u 가 v 보다 앞" 인 정점 나열이 곧 초기화 순서 = **위상 순서(topological order)** 다. 방향을 반대로(`imu → spi1`, "의존한다") 잡아도 되지만 그때는 출력이 역순이 된다 — 둘 중 하나를 정하고 테스트가 모든 간선을 검사하게 한다(7. 실수 4).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m05a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C 의 13 드라이버 — 열 = 병렬 초기화 단계(wave), 간선 = 공급자 → 소비자</text>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="58" y="300">wave 0</text><text x="162" y="300">1</text><text x="266" y="300">2</text><text x="370" y="300">3</text>
    <text x="474" y="300">4</text><text x="578" y="300">5</text><text x="682" y="300">6</text>
  </g>
  <g stroke="currentColor" stroke-width="1" fill="none" marker-end="url(#m05a)" opacity="0.75">
    <line x1="96" y1="173" x2="124" y2="93"/><line x1="96" y1="173" x2="124" y2="253"/>
    <line x1="96" y1="173" x2="228" y2="133"/><line x1="96" y1="173" x2="228" y2="213"/>
    <line x1="200" y1="93" x2="228" y2="133"/><line x1="200" y1="93" x2="228" y2="213"/>
    <line x1="200" y1="253" x2="228" y2="213"/>
    <line x1="304" y1="133" x2="332" y2="73"/><line x1="304" y1="213" x2="332" y2="263"/>
    <line x1="304" y1="133" x2="436" y2="53"/><line x1="304" y1="133" x2="436" y2="113"/>
    <line x1="408" y1="73" x2="436" y2="53"/><line x1="408" y1="73" x2="436" y2="113"/><line x1="408" y1="73" x2="436" y2="173"/>
    <line x1="304" y1="213" x2="436" y2="173"/><line x1="200" y1="93" x2="436" y2="168"/>
    <line x1="408" y1="263" x2="436" y2="263"/>
    <line x1="512" y1="53" x2="540" y2="113"/><line x1="512" y1="113" x2="540" y2="113"/><line x1="512" y1="173" x2="540" y2="113"/>
    <line x1="512" y1="263" x2="644" y2="193"/><line x1="616" y1="113" x2="644" y2="193"/>
  </g>
  <path d="M370 60 C 340 30, 290 60, 266 120" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none" marker-end="url(#m05a)"/>
  <text x="300" y="38" fill="currentColor" font-size="12">추가된 pmic → i2c1 (사이클!)</text>
  <g stroke="currentColor">
    <rect x="20" y="160" width="76" height="26" rx="4" class="f-ok"/>
    <rect x="124" y="80" width="76" height="26" rx="4" class="f-ok"/><rect x="124" y="240" width="76" height="26" rx="4" class="f-ok"/>
    <rect x="228" y="120" width="76" height="26" rx="4" class="f-warn"/><rect x="228" y="200" width="76" height="26" rx="4" class="f-ok"/>
    <rect x="332" y="60" width="76" height="26" rx="4" class="f-warn"/><rect x="332" y="250" width="76" height="26" rx="4" class="f-ok"/>
    <rect x="436" y="40" width="76" height="26" rx="4" class="f-mut"/><rect x="436" y="100" width="76" height="26" rx="4" class="f-mut"/>
    <rect x="436" y="160" width="76" height="26" rx="4" class="f-mut"/><rect x="436" y="250" width="76" height="26" rx="4" class="f-ok"/>
    <rect x="540" y="100" width="76" height="26" rx="4" class="f-mut"/>
    <rect x="644" y="180" width="76" height="26" rx="4" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="58" y="177">clock</text><text x="162" y="97">gpio</text><text x="162" y="257">dma</text>
    <text x="266" y="137">i2c1</text><text x="266" y="217">spi1</text><text x="370" y="77">pmic</text><text x="370" y="267">flash</text>
    <text x="474" y="57">baro</text><text x="474" y="117">mag</text><text x="474" y="177">imu</text><text x="474" y="267">fs</text>
    <text x="578" y="117">fusion</text><text x="682" y="197">logger</text>
  </g>
  <text x="10" y="322" fill="currentColor" font-size="12">사이클이 생기면: 주황 = 사이클 위, 회색 = 그 하류(막힘), 녹색 = 그래도 올라가는 6개 (clock gpio dma spi1 flash fs).</text>
</svg>
```

### Part A — Kahn 알고리즘과 결정적 tie-break

그래프는 정적 배열 CSR(compressed sparse row: `off[u]..off[u+1]` 이 u 의 나가는 간선)로 주어진다 (`graph_build` 제공, MAX_V = 512, MAX_E = 4096, malloc 없음). 구현할 것:

- `int topo_kahn_lex(const graph_t *g, uint16_t *order)` — Kahn. 준비된(진입 차수 0) 정점 중 **가장 작은 id 먼저** 를 min-heap 으로 (heap 은 제공, DS-B12 에서 자세히). `order[0..k)` 를 쓰고 k 를 돌려준다. `k == n` 이면 DAG, `k < n` 이면 사이클이 있다.
- `int topo_lex_scan(...)` — 같은 결과를 heap 없이, 매 단계 0..n-1 을 선형 스캔해서.

두 결과는 같아야 하고, **사전순 최소 위상 순서** 여야 한다. 검증: n ≤ 4 의 **모든** 방향 그래프(자기 루프 포함 66,067개)를 순열 전수 탐색과 대조 (DAG 개수가 OEIS A003024 의 1, 1, 3, 25, 543 과 맞는지), n = 5 의 모든 그래프 2^20 개에서 DAG 29,281개, xorshift32 무작위 DAG 1,500개(V ≤ 300, E ≤ 4V)는 "모든 간선이 앞→뒤" 와 "매 단계가 그때 준비된 것 중 최소" 를 정의대로 검사.

### Part B — 순환 의존: 사이클 경로 보고

- `int find_cycle(const graph_t *g, uint16_t *cyc)` — 사이클이 있으면 `cyc[0..L)` 에 `cyc[0] → cyc[1] → … → cyc[L-1] → cyc[0]` 를 쓰고 L, 없으면 0. 자기 루프(`watchdog: watchdog`)는 L = 1. **재귀 금지**(명시적 스택, 깊이 ≤ V). 시작 정점은 id 오름차순, 이웃은 입력 순서 → 결과가 결정적.

검증: 반환 경로의 모든 연속 간선이 실제로 있고 정점이 중복되지 않는가, "Kahn 이 k < n" ⇔ "find_cycle > 0" 이 모든 그래프에서 일치하는가, 무작위 DAG 에 사이클을 **주입**(나가는 간선을 따라 1~8 걸음 걸은 뒤 시작점으로 되돌아오는 간선 추가)했을 때 놓치지 않는가. 덤으로 Kahn 이 못 뽑는 정점 집합이 정확히 "어떤 사이클에서 도달 가능한 정점" 인지 n ≤ 4 전수로 확인한다.

### Part C — 초기화 계획 도구

- `int init_waves(const graph_t *g, const uint16_t *order, uint16_t *wave)` — 비동기 probe 처럼 "앞 단계가 다 끝나면 다음 단계를 한꺼번에" 올릴 때의 단계 번호 = 소스에서의 **최장 경로** 간선 수. 반환 = 단계 수.
- `int plan_report(const dep_db_t *db, char *out, size_t cap)` — 위의 그림 같은 목록 텍스트(파서 `dep_parse` 제공, 선언 안 된 의존성·중복 선언은 파서가 줄 번호와 함께 거절)를 받아 보고서를 쓴다. DAG 면 `OK … / order: … / wave k: …`, 사이클이면 `CYCLE: a -> b -> a / init ok (k): … / blocked (n-k): …`. 버퍼가 모자라면 -1 (잘린 문자열을 성공처럼 내보내지 않는다).

## 2. Problem (English)

> **Driver init order.** A board has `n` drivers with ids `0..n-1`. A dependency list gives pairs "`v` needs `u`", stored as directed edges `u -> v` (u must be initialized before v) in a static CSR graph (`n <= 512`, `m <= 4096`, duplicate edges allowed, self-loops allowed). No dynamic allocation, no recursion.
>
> 1. `int topo_kahn_lex(const graph_t *g, uint16_t *order)` — write an init order; whenever several drivers are ready, pick the **smallest id** first. Return the number of drivers written; it is `< n` iff the graph has a cycle. State the time complexity of your tie-break.
> 2. `int find_cycle(const graph_t *g, uint16_t *cyc)` — if a cycle exists, write one as a vertex sequence `c0 -> c1 -> ... -> c(L-1) -> c0` and return `L >= 1`; else return 0.
> 3. `int init_waves(...)` — if every driver in a wave can init in parallel once all earlier waves are done, assign each driver its earliest wave and return the number of waves.
>
> Example: ids `0 clock, 1 pmic, 2 gpio, 3 i2c1, 4 imu, 5 led`; edges `0->2, 0->3, 2->3, 3->1, 1->4, 3->4, 2->5`. Smallest-id-first order: `0 2 3 1 4 5` (clock gpio i2c1 pmic imu led). A plain FIFO queue would give `0 2 3 5 1 4` — also valid, but not the one asked for. Waves: clock 0, gpio 1, i2c1 2, led 2, pmic 3, imu 4 → 5 waves. Adding edge `1->3` (i2c1 also needs pmic) makes `find_cycle` return `3 1` (i2c1 -> pmic -> i2c1), and only `0 2 5` can be initialized.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 함수 본문(`topo_kahn_lex`, `topo_lex_scan`, `find_cycle`, `init_waves`, `plan_report`). CSR 빌더, min-heap, 출력 헬퍼 `emit`, 목록 파서는 주어진다. 스텁은 0 / 빈 문자열을 돌려줘서 하네스가 멈추거나 범위 밖을 읽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:348  build_mask(1, 0, 1) == 0 && topo_kahn_lex(&G, order) == 1 && order[0] == 0 && find_cycle(&G, cyc) == 0
…
exhaustive n<=4: 66067 graphs, DAGs per n = 1 1 3 25 543 (OEIS A003024: 1 1 3 25 543)
  mismatches kahn=15088 scan=572 cycle=65494 blocked-set=15088
…
exhaustive n=5: 1048576 graphs, DAGs=0 (OEIS A003024: 29281), kahn/dfs disagree=1048576, bad=1048576
random DAGs: 1500 graphs, 456114 edges: bad order=1498 not-lexmin=1498 scan mismatch=0 false cycle=0
injected cycles: 1495 graphs, missed=1495 bad=0, reported length 1/2/3-4/5+ = 0/0/0/0
…
init_waves: 300 random DAGs vs relaxation reference, mismatches=11883
--- report (drivers) ---
FAIL starter.c:520  rc == 0
…
11/44 passed
```

(exit 1. exhaustive 줄의 DAG 개수 1 1 3 25 543 은 하네스의 순열 전수 탐색이 센 것이라 스텁과 무관하게 맞다. 통과한 11개는 n = 0 사례, 파서 오류 검사들, 우연히 0 이 정답인 검사들.)

## 4. 힌트

<details><summary>힌트 1 — 진입 차수가 뜻하는 것</summary>

`indeg[v]` = "아직 출력 안 된 공급자에서 v 로 오는 간선 수" 로 정의하면, `indeg[v] == 0` ⇔ v 를 지금 올려도 된다. u 를 출력할 때 u 의 나가는 간선마다 `--indeg[v]`, 0 이 되는 순간 준비 집합에 넣는다. 중복 간선은 "세는 쪽과 빼는 쪽이 같은 간선 목록" 이면 저절로 맞는다.

</details>

<details><summary>힌트 2 — tie-break 는 "준비 집합" 의 자료구조 문제</summary>

FIFO 큐면 O(V+E) 지만 순서는 "준비된 시각" 순이다. 가장 작은 id 를 원하면 준비 집합에서 최솟값을 꺼내야 한다: min-heap(push/pop O(log V)), 매번 선형 스캔(O(V)), 정점이 ≤ 32/64 개면 비트마스크의 가장 낮은 1 비트(ctz). heap 에는 정점이 **0 이 되는 순간에만** 들어가므로 push 는 최대 V 번이다.

</details>

<details><summary>힌트 3 — 왜 GRAY 가 필요한가</summary>

방향 그래프에서 "이미 방문한 정점을 다시 만났다" 는 사이클이 아니다 (0→1, 0→2, 2→1 에서 1 을 두 번 만난다). 사이클의 증거는 **현재 DFS 경로 위에 있는**(GRAY) 정점으로 가는 간선뿐이다. 끝난(BLACK) 정점으로 가는 간선은 무시한다.

</details>

<details><summary>힌트 4 — 재귀 없는 DFS 에서 경로를 꺼내기</summary>

명시적 스택 `stk[0..top]` 에 정점, `it[d]` 에 "stk[d] 의 다음에 볼 간선 위치" 를 둔다. 그러면 스택이 곧 GRAY 경로다. GRAY 정점 v 로 가는 간선을 만나면 스택에서 v 의 위치 j 를 찾아 `stk[j..top]` 을 복사하면 끝.

</details>

<details><summary>힌트 5 — wave 는 위상 순서 위의 DP</summary>

순서대로 u 를 볼 때 u 의 공급자는 이미 다 봤으니 `wave[u]` 가 확정이다. 나가는 간선마다 `wave[v] = max(wave[v], wave[u] + 1)`. 최장 경로가 일반 그래프에서는 어렵지만(NP-hard) DAG 에서는 이 한 번의 훑기로 끝난다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 간선 방향은 "u 먼저 → v"? 중복 의존·자기 의존 가능? 여러 답 중 무엇(결정적이어야 하나)? 사이클이면 무엇을 출력? n, m 상한(정적 배열 크기)? |
| 2. 무식한 방법 | 순열 n! 개를 다 보며 유효한 첫 순서 찾기 — 테스트 기준으로만(n ≤ 6). 또는 "올릴 수 있는 것 올리기를 변화 없을 때까지 반복" O(V·(V+E)) (= deferred probe, Part C 실측) |
| 3. 더 나은 생각 + 불변식 | Kahn: `indeg[v]` = 출력 안 된 공급자 간선 수, 준비 집합 = indeg 0. 사전순 최소는 준비 집합을 min-heap 으로 |
| 4. 코드 | indeg 계산 → 0 들을 push → pop·출력·간선 지우기·새 0 push. 사이클 경로는 3색 반복 DFS |
| 5. 소리 내어 말할 테스트 | n = 0, 고립 정점, 자기 루프, 2-사이클, 중복 간선, id 와 순서가 반대인 사슬(깊이 V), 사이클 하류의 정점, 전부 독립(준비 집합 = V) |
| 6. 복잡도 | Kahn+heap O(E + V log V), 스캔 O(V² + E), FIFO O(V + E). 추가 메모리 O(V) (indeg·heap·order), DFS 스택 깊이 ≤ V (재귀면 프레임 V 개) |

### Part A — Kahn 의 불변식과 정당성

불변식 (I): 매 반복 시작에서 `indeg[v]` = (아직 출력 안 된 u 에서 v 로 오는 간선 수), 준비 집합 = {출력 안 됐고 indeg 0 인 정점}. 초기화가 정의 그대로이고, u 를 출력하며 u 의 나가는 간선마다 1 씩 빼므로 유지된다 (중복 간선은 두 번 세고 두 번 뺀다).

1. **출력은 유효하다.** u 를 꺼낼 때 indeg[u] = 0 이므로 u 의 모든 공급자는 이미 출력됐다 → 모든 간선 u'→u 에서 u' 가 앞.
2. **DAG 이면 전부 출력된다.** 멈췄을 때 남은 집합 R 이 비어 있지 않다고 하자. R 의 모든 v 는 indeg > 0, 즉 R 안에 공급자가 있다. 아무 v 에서 공급자 쪽으로 계속 거슬러 가면 유한 집합 R 안에서 반드시 정점이 반복된다 → 사이클. 대우를 취하면 DAG ⇒ R = ∅. 그리고 사이클 위 정점은 같은 사이클의 앞 정점이 출력되기 전에는 indeg > 0 이라 영원히 못 나온다 ⇒ **k == n ⇔ DAG**.
3. **사전순 최소이고 유일하다** (가장 작은 id 먼저일 때). n 에 대한 귀납. 어떤 위상 순서든 첫 원소는 소스(진입 차수 0)다. 가장 작은 소스 s 로 시작하는 위상 순서는 존재한다 (G−s 의 아무 위상 순서 앞에 s 를 붙이면 됨). 따라서 사전순 최소 순서는 첫 자리에서 s 보다 작은 값을 가질 수 없고(그건 소스가 아니다) s 보다 큰 값으로 시작하면 s 로 시작하는 것보다 크다 → 첫 원소는 s. 나머지는 G−s 의 위상 순서이고, 첫 자리가 고정됐으니 사전순 비교는 나머지에서 결정된다 → 귀납 가정으로 G−s 의 사전순 최소. 알고리즘은 정확히 "현재 그래프의 가장 작은 소스" 를 반복해서 뽑는다 ∎. (같은 논리로 "가장 작은 소스" 대신 "가장 큰 id 를 마지막에" 는 다른 순서를 준다 — 사전순 최소 ≠ 역순 사전순 최대. 면접에서 "작은 번호가 먼저 나오게" 가 정확히 무엇인지 확인할 것.)

손 계산 — 2. Problem 의 예 (0 clock, 1 pmic, 2 gpio, 3 i2c1, 4 imu, 5 led):

| 단계 | pop | 지우는 간선 → 새로 0 이 된 정점 | indeg[0..5] 후 | 준비 heap 후 | order |
|---|---|---|---|---|---|
| 시작 | — | — | 0 1 1 2 2 1 | {0} | |
| 1 | 0 | 0→2 (**2**), 0→3 | – 1 0 1 2 1 | {2} | 0 |
| 2 | 2 | 2→3 (**3**), 2→5 (**5**) | – 1 – 0 2 0 | {3, 5} | 0 2 |
| 3 | 3 | 3→1 (**1**), 3→4 | – 0 – – 1 0 | {1, 5} | 0 2 3 |
| 4 | 1 | 1→4 (**4**) | – – – – 0 0 | {4, 5} | 0 2 3 1 |
| 5 | 4 | — | | {5} | 0 2 3 1 4 |
| 6 | 5 | — | | {} | 0 2 3 1 4 5 |

FIFO 큐였다면 2단계 뒤 큐가 [3, 5] 이고, 3 을 꺼내며 1 을 **뒤에** 넣으므로 5 가 1 보다 먼저 나온다: `0 2 3 5 1 4` (작업 중 따로 확인). 둘 다 유효하지만 "작은 id 먼저" 는 heap 쪽이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m05b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">2. Problem 의 예 — 칸 안 숫자 = id, 위 첨자 = 처음 진입 차수</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m05b)">
    <line x1="110" y1="110" x2="190" y2="62"/><line x1="110" y1="118" x2="330" y2="128"/>
    <line x1="270" y1="62" x2="330" y2="118"/><line x1="270" y1="52" x2="330" y2="52"/>
    <line x1="410" y1="134" x2="470" y2="174"/><line x1="550" y1="174" x2="610" y2="134"/>
    <line x1="410" y1="124" x2="610" y2="124"/>
  </g>
  <g stroke="currentColor">
    <rect x="30" y="100" width="80" height="30" rx="5" class="f-ok"/>
    <rect x="190" y="38" width="80" height="30" rx="5" class="f-ok"/>
    <rect x="330" y="38" width="80" height="30" rx="5" class="f-mut"/>
    <rect x="330" y="110" width="80" height="30" rx="5" class="f-acc"/>
    <rect x="470" y="160" width="80" height="30" rx="5" class="f-mut"/>
    <rect x="610" y="110" width="80" height="30" rx="5" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="120">0 clock</text><text x="200" y="58">2 gpio</text><text x="340" y="58">5 led</text>
    <text x="340" y="130">3 i2c1</text><text x="480" y="180">1 pmic</text><text x="620" y="130">4 imu</text>
    <text x="96" y="96">0</text><text x="256" y="34">1</text><text x="396" y="34">1</text><text x="396" y="106">2</text>
    <text x="536" y="156">1</text><text x="676" y="106">2</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="214">min-heap : {0} → {2} → {3,5} → {1,5} → {4,5} → {5} → {}  ⇒ 0 2 3 1 4 5</text>
    <text x="30" y="236">FIFO 큐  : [0] → [2] → [3,5] → [5,1] → [1] → [4] → []  ⇒ 0 2 3 5 1 4 (유효, 그러나 작은 id 먼저 아님)</text>
  </g>
</svg>
```

**tie-break 비용.** heap 에는 정점이 indeg 가 0 이 되는 **순간에만** 한 번 들어가므로 push·pop 은 각각 정확히 k ≤ V 번이고, 간선마다 감소 1번: **O(E + V log V)** 시간 (흔히 쓰는 O((V+E) log V) 는 맞지만 느슨한 상한이다 — 간선마다 push 하는 실수 7 의 구현이면 그게 정확한 값이 된다). 선형 스캔은 매 단계 O(V) → **O(V² + E)**, 대신 heap 코드가 없다. 추가 메모리는 둘 다 O(V): `indeg` 2V B, heap 2V B(또는 `done` V B), `order` 2V B. 드라이버 수가 32/64 이하면 준비 집합을 `uint32_t`/`uint64_t` 하나로 두고 가장 낮은 1 비트를 꺼내면 O(V + E) 에 heap 도 없다 (연습 코드의 `topo_lex`, asm 은 Q2).

### Part A — 실측: 어느 tie-break 가 언제 싼가 (`demo_bench.c`)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6, Apple clang 21.0.0 (clang-2100.0.123.102), -O2. 1회 워밍업 후 9회 중앙값, 결과 체크섬을 출력해 최적화로 지워지지 않게 했다. **측정 시 load average 48~77** (다른 에이전트 14개가 병렬 빌드 중) — 절대 ns 보다 **비율**을 볼 것. 두 번째 실행 (실제 출력):

```text
shape   V      E      fifo ns   heap ns   scan ns   heap/fifo  scan/heap  heap==scan
random    100    400       334       706      2351      2.12        3.3   yes
random   1000   4000      4170     10735    276325      2.57       25.7   yes
random  10000  40000     58750    301500  31799750      5.13      105.5   yes
wide      100      0        69       872      1590     12.64        1.8   yes
wide     1000      0       665      9870    136890     14.84       13.9   yes
wide    10000      0      7750    159750  13542750     20.61       84.8   yes
chain     100     99       278       276      2332      0.99        8.4   yes
chain    1000    999      1735      1940    202040      1.12      104.1   yes
chain   10000   9999     20250     30250  21227500      1.49      701.7   yes
```

첫 실행은 heap/fifo 1.94/3.57/3.68 (random), 10.8/23.5/36.6 (wide), 0.84/0.72/0.77 (chain), scan/heap 은 같은 자릿수였다. 읽을 것:

- **scan 은 V² 그대로**: V 를 10배 하면 random 에서 ~100배 (0.28 ms → 32 ms). 사슬은 준비된 정점이 항상 배열 끝(id 역순)이라 최악 — V = 10⁴ 에서 heap 보다 700~800배 (두 실행 702배·806배).
- **heap 의 값은 "준비 집합 크기"** 에 달렸다. wide(의존성 0, 처음부터 V 개 준비)에서 FIFO 대비 11~37배, chain(준비 1개)에서는 FIFO 와 같다(잡음 수준 차이).
- **드라이버 규모(V ≈ 10~100)에서는 전부 수 µs 이하**: V = 100 에서 scan 2.4 µs. 부팅 시간에 보이지도 않는다 → 펌웨어에서는 가장 짧고 검증하기 쉬운 코드(스캔 또는 비트마스크)가 정답일 수 있다. heap 은 V 가 수천 이상인 빌드 도구·패키지 관리자 쪽 이야기.

### Part B — 3색 DFS 와 사이클 경로

색의 의미: WHITE = 아직 안 봄, GRAY = 현재 DFS 경로 위(들어갔고 아직 안 나옴), BLACK = 끝남(그 아래를 다 봤음). 반복형에서 `stk[0..top]` 은 **정확히 GRAY 정점들을 경로 순서대로** 담는다 (push 할 때 GRAY, pop 할 때 BLACK).

**정리.** 방향 그래프에 사이클이 있다 ⇔ DFS 가 어떤 간선 u→v 에서 v 가 GRAY 인 것을 본다.

- (⇐) v 가 GRAY 면 v = stk[j] 이고 stk[j] → stk[j+1] → … → stk[top] = u 는 트리 간선 경로, 거기에 u→v 를 더하면 사이클. 스택의 정점은 서로 다르므로(정점은 한 번만 GRAY 가 된다) **단순 사이클**이고, 그게 그대로 `cyc` 다.
- (⇒) 사이클 C 에서 처음 GRAY 가 되는 정점을 v 라 하자. 그 순간 C 의 나머지는 모두 WHITE 이므로 흰 경로 정리(white-path theorem, CLRS 22.3)에 의해 C 의 모든 정점은 v 의 자손이 되어 v 가 끝나기 전에 끝난다. C 에서 v 바로 앞의 u 도 v 의 자손이므로, u 가 간선 u→v 를 훑는 시점에 v 는 아직 GRAY 다 ∎. (그 전에 다른 back edge 를 먼저 만나 return 할 수는 있다 — 그래도 사이클 하나를 찾은 것이니 상관없다.)

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m05c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">왼쪽: GRAY 로 가는 간선 = 사이클 / 오른쪽: BLACK 로 가는 간선 = 사이클 아님</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m05c)">
    <line x1="66" y1="70" x2="104" y2="70"/><line x1="146" y1="70" x2="184" y2="70"/><line x1="226" y1="70" x2="264" y2="70"/>
    <line x1="285" y1="91" x2="285" y2="134"/>
  </g>
  <path d="M285 49 C 270 20, 140 20, 128 48" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none" marker-end="url(#m05c)"/>
  <g stroke="currentColor">
    <circle cx="45" cy="70" r="21" class="f-acc"/><circle cx="125" cy="70" r="21" class="f-acc"/>
    <circle cx="205" cy="70" r="21" class="f-acc"/><circle cx="285" cy="70" r="21" class="f-acc"/>
    <circle cx="285" cy="155" r="21" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="45" y="75">0</text><text x="125" y="75">1</text><text x="205" y="75">2</text><text x="285" y="75">3</text><text x="285" y="160">4</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="120" y="112">3→1 에서 1 은 GRAY</text>
    <text x="20" y="195" font-family="monospace">stk = [0 1 2 3] (모두 GRAY)</text>
    <text x="20" y="213" font-family="monospace">j = 1 → cyc = stk[1..3] = 1 2 3</text>
    <text x="20" y="231">간선 0→1→2→3→1, 3→4 (입력 순서)</text>
    <text x="20" y="249">3→1 을 먼저 봐서 4 는 WHITE 로 남음</text>
  </g>
  <line x1="390" y1="35" x2="390" y2="250" stroke="currentColor" stroke-dasharray="2 4"/>
  <g stroke="currentColor" fill="none" marker-end="url(#m05c)">
    <line x1="476" y1="78" x2="530" y2="120"/><line x1="476" y1="62" x2="600" y2="62"/><line x1="610" y1="80" x2="560" y2="124"/>
  </g>
  <g stroke="currentColor">
    <circle cx="455" cy="70" r="21" class="f-acc"/><circle cx="545" cy="140" r="21" class="f-ok"/><circle cx="621" cy="62" r="21" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="455" y="75">0</text><text x="545" y="145">1</text><text x="621" y="67">2</text></g>
  <g fill="currentColor" font-size="12">
    <text x="410" y="190">0→1 먼저: 1 은 끝나서 BLACK(녹색).</text>
    <text x="410" y="210">그다음 0→2, 2→1: 1 을 "또" 만났지만 BLACK.</text>
    <text x="410" y="230">visited 하나로 판정하면 여기서 거짓 사이클.</text>
    <text x="410" y="250">(무방향 그래프와 다른 점 — 7. 실수 1)</text>
  </g>
</svg>
```

반복형 추적 (왼쪽 그림, `it[d]` = stk[d] 의 다음 간선 위치):

| 단계 | 스택(GRAY) | 본 간선 | v 의 색 → 동작 |
|---|---|---|---|
| 1 | [0] | 0→1 | WHITE → push 1 |
| 2 | [0 1] | 1→2 | WHITE → push 2 |
| 3 | [0 1 2] | 2→3 | WHITE → push 3 |
| 4 | [0 1 2 3] | 3→1 | **GRAY** → j = 1, L = top − j + 1 = 3, cyc = 1 2 3, return |

복잡도: 정점마다 push/pop 한 번, 간선마다 `it` 가 한 번 지나간다 → O(V + E). 사이클을 찾을 때 j 를 찾는 역방향 스캔은 한 번뿐이라 O(V) 추가. 메모리: color V B + stk 2V B + it 2V B. **스택 깊이는 최대 V** — 하네스의 512 정점 사슬(id 역순)에서 DFS 경로가 512 단까지 쌓이고, 닫으면 길이 512 의 사이클을 돌려준다(테스트 통과). 재귀 판이었다면 프레임 512 개다: 프레임이 수십 바이트만 돼도 Cortex-M 태스크 스택 1~2 KB 를 넘는다 (재귀를 반복으로 바꾸는 일반론은 ALGO-B11, 실제로 터지는 크기는 ALGO-A10 에서 측정).

**찾는 사이클은 "하나" 이고 가장 짧은 것은 아니다.** 결정적이기는 하다(시작 id 오름차순, 이웃은 입력 순서). 가장 짧은 사이클이 필요하면 정점마다 BFS 로 O(V(V+E)) — 이건 ALGO-A10 이 SCC 안에서 증거 사이클을 뽑을 때 쓴 방법이다. "사이클이 있는 모든 영역" 은 SCC(강한 연결 요소)로 O(V+E) 에 나온다 — 역시 ALGO-A10.

**Kahn 이 남기는 정점 = 사이클의 하류.** k < n 일 때 남은 집합 R 은 정확히 "어떤 사이클 위 정점에서 도달 가능한 정점(자기 포함)" 이다. (⊇) 사이클 위 정점은 영원히 indeg > 0 이고(위 2번), 그 하류는 그 정점이 안 나오니 따라서 못 나온다 (경로 길이 귀납). (⊆) R 의 v 에서 공급자를 거슬러 R 안에서 반복이 생길 때까지 가면 사이클에 닿는다 → v 는 그 사이클의 하류. 하네스는 이것을 n ≤ 4 의 모든 그래프에서 Warshall 폐포와 대조한다(`blocked-set=0`). 그래서 보고서의 `blocked (7)` 은 "사이클 2개 + 그 하류 5개" 이고, **남은 정점 목록을 사이클로 보고하면 틀린다**(7. 실수 5).

### Part C — wave 와 보고서

`wave[v]` = 소스에서 v 까지 최장 경로의 간선 수. 단계 수 W = max wave + 1 이 최소인 이유: (하한) 길이 L 의 경로 위 정점들은 서로 다른 단계여야 하므로 ≥ L + 1 단계. (달성) 간선 u→v 마다 wave[v] ≥ wave[u] + 1 이므로 "wave 번호 순으로 단계별 병렬 실행" 은 모든 의존을 지킨다. 계산은 위상 순서 위 DP 한 번(O(V+E)): u 를 볼 때 u 의 공급자는 이미 처리되어 wave[u] 가 확정이다. 하네스는 "변화가 없을 때까지 모든 간선을 완화" 하는 벨만 포드식 기준과 300개 무작위 DAG 에서 대조한다.

13 드라이버 목록 (`solution.c` 의 `DRIVERS`) 을 돌린 실제 출력:

```text
--- report (drivers) ---
OK 13 drivers, 7 waves
order: clock gpio dma i2c1 spi1 pmic imu baro mag flash fs fusion logger
wave 0: clock
wave 1: gpio dma
wave 2: i2c1 spi1
wave 3: pmic flash
wave 4: imu baro mag fs
wave 5: fusion
wave 6: logger
--- report (i2c1 also needs pmic) ---
CYCLE: i2c1 -> pmic -> i2c1
init ok (6): clock gpio dma spi1 flash fs
blocked (7): i2c1 pmic imu baro mag fusion logger
```

두 번째는 누군가 "I2C 풀업 전원이 PMIC 의 LDO 에서 나온다" 며 `i2c1: clock gpio pmic` 로 고친 경우다. PMIC 는 I2C 로 설정하므로 실제 보드에서도 흔한 닭과 달걀이다. 보고서가 **사이클 경로 + 올라갈 수 있는 것 + 막힌 것** 을 다 주므로, 펌웨어는 "부분 부팅(올라간 6개로 로그·플래시는 살린다)" 을 할 수 있고 사람은 어느 줄을 고칠지 바로 안다. 해결은 대개 의존을 쪼개는 것이다: PMIC 의 기본 레일은 리셋 기본값으로 이미 켜져 있으니 "pmic-early(설정 없이 쓰는 레일)" 와 "pmic(I2C 로 설정)" 를 다른 노드로 둔다.

보고서 도구의 나머지 규칙: 선언 안 된 이름(`imu: clock irqc`)은 파서가 `line 2: 'imu' needs undeclared 'irqc'` 로 거절, 중복 선언 거절, 출력 버퍼가 모자라면 -1 (13 드라이버 보고서를 40 바이트 버퍼에 쓰면 -1 이고 문자열은 NUL 로 끝난다 — 테스트됨). `sizeof(dep_db_t)` = 12,312 B 인데 대부분은 범용 MAX_V = 512 / MAX_E = 4096 CSR(9,222 B) 몫이다. 실제 MCU 에서는 드라이버 64개·간선 256개로 잡으면 수백 바이트이고, 더 좋게는 **빌드 시점에 순서를 계산해 const 표로 굽는다**(Q6).

### Part C — 리눅스 deferred probe 식 재시도와 비교 (`demo_deferred.c`)

리눅스 드라이버 모델에는 공급자가 아직 없으면 probe 가 `-EPROBE_DEFER` 를 돌려주고 나중에 다시 시도되는 **deferred probe** 가 있다. 아래는 그 **단순화한 개념 모델**이다 (등록 순서대로 시도 → 실패하면 미룬 목록에 → 한 바퀴에 하나라도 성공하면 미룬 목록 전체 재시도 → 성공 없는 바퀴면 끝). 실제 커널의 재시도 시점·순서와 같다고 주장하지 않는다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_deferred.c -o demo_deferred && ./demo_deferred
```

실제 출력 (열 정리):

| 그래프 · 등록 순서 | V | E | 재시도 방식 probe 시도 | 바퀴 | 공급자 확인 | 올라감 | Kahn probe |
|---|---|---|---|---|---|---|---|
| 사슬, 의존 순서대로 | 4000 | 3999 | 4000 (1.0×V) | 1 | 3999 | 4000 | 4000 |
| 사슬, 역순 등록 | 10 | 9 | 55 (5.5×V) | 10 | 54 | 10 | 10 |
| 사슬, 역순 등록 | 1000 | 999 | 500500 (500.5×V) | 1000 | 500499 | 1000 | 1000 |
| 사슬, 역순 등록 | 4000 | 3999 | 8002000 (2000.5×V) | 4000 | 8001999 | 4000 | 4000 |
| 무작위 DAG E=3V, 무작위 등록 | 100 | 300 | 290 (2.9×V) | 7 | 633 | 100 | 100 |
| 무작위 DAG E=3V, 무작위 등록 | 4000 | 12000 | 13993 (3.5×V) | 11 | 30850 | 4000 | 4000 |
| 13 드라이버, 이름 알파벳순 등록 | 13 | 22 | 22 (1.7×V) | 3 | 33 | 13 | 13 |
| 13 드라이버 + pmic→i2c1 사이클 | 13 | 23 | 29 (2.2×V) | 3 | 40 | 6 | 6 |

- 최악(역순 등록 사슬)은 정확히 V(V+1)/2 = 8,002,000 회: 한 바퀴에 하나씩만 성공한다. Kahn 은 항상 올라갈 수 있는 드라이버당 **한 번**, 실패 0.
- 무작위 DAG 는 바퀴 수가 대략 의존 깊이라 3×V 언저리 — 그래서 실무에서는 재시도 방식이 그럭저럭 돈다. 하지만 probe 실패가 공짜가 아니다(실제 probe 는 클럭 켜고 레지스터 읽다가 되돌린다). 리눅스가 devicetree 의 phandle 참조(clocks, *-supply, interrupts 등)에서 공급자-소비자 관계를 미리 뽑아 **device link** 로 순서를 잡는 `fw_devlink` 를 도입한 이유가 이것이다 (도입·기본값 버전과 세부 동작은 커널 버전마다 다르다 — `Documentation/admin-guide/kernel-parameters.txt` 의 `fw_devlink=` 항목 확인 권장).
- 사이클이 있으면 두 방식 모두 같은 6개만 올리고 멈춘다 (위의 "Kahn 이 남기는 정점" 정리와 같은 집합 — 재시도의 고정점도 "사이클 하류가 아닌 정점 전부"). 차이는 Kahn+DFS 는 **왜** 멈췄는지(사이클 경로)를 말해 준다는 것.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 44/44, 보고 없음
```

실제 출력 (보고서 부분은 Part C 에 옮김):

```text
exhaustive n<=4: 66067 graphs, DAGs per n = 1 1 3 25 543 (OEIS A003024: 1 1 3 25 543)
  mismatches kahn=0 scan=0 cycle=0 blocked-set=0
exhaustive n=5: 1048576 graphs, DAGs=29281 (OEIS A003024: 29281), kahn/dfs disagree=0, bad=0
random DAGs: 1500 graphs, 456114 edges: bad order=0 not-lexmin=0 scan mismatch=0 false cycle=0
injected cycles: 1495 graphs, missed=0 bad=0, reported length 1/2/3-4/5+ = 509/483/389/114
init_waves: 300 random DAGs vs relaxation reference, mismatches=0
…
sizeof(graph_t)=9222 B, sizeof(dep_db_t)=12312 B
44/44 passed
```

(-O2 빌드 0.3초 안팎, sanitizer 빌드 0.6초 안팎. 66,067 = 1 + 2 + 2⁴ + 2⁹ + 2¹⁶: n 정점의 자기 루프 포함 방향 그래프 2^(n²) 개의 합. "reported length" 에 자기 루프(1)가 많은 것은 무작위 시작점이 싱크(나가는 간선 없음)인 경우가 많아서다 — 보고된 길이이지 주입한 길이가 아니다.)

구현 메모:

- 세 함수가 `static` 작업 배열을 쓴다 → **재진입 불가**(ISR·다른 태스크에서 동시에 부르지 말 것). 부팅 때 한 번 쓰는 도구라 의도한 선택이고, 재진입이 필요하면 작업 공간 구조체를 호출자가 넘긴다.
- `topo_lex_scan` 의 `done[]` 은 "indeg 0 이지만 이미 뽑힌 정점" 을 거르기 위한 것이다. 뽑힌 정점의 indeg 를 특수값으로 바꾸는 방법도 있지만 배열 의미가 섞인다.
- `plan_report` 는 `k < n` 일 때만 `find_cycle` 을 부른다. Part A 의 정리 2 때문에 그때 사이클은 **반드시** 있어서 `cyc[0]` 접근이 안전하다 — 이런 "증명에 기대는 접근" 은 주석으로 남긴다.

## 7. 자주 하는 실수

**1. 방향 그래프 사이클을 visited 하나로 판정.** 위 그림 오른쪽: 0→1, 0→2, 2→1 에서 2→1 을 볼 때 1 은 이미 방문됐지만 사이클이 아니다(교차 간선, cross edge). 2색 판정은 이 DAG 를 사이클이라고 보고한다. 무방향 그래프의 "부모가 아닌 방문 정점 = 사이클" 규칙을 그대로 옮긴 실수다. 고침: GRAY(경로 위)와 BLACK(끝남)을 구분하고 GRAY 만 사이클로.

**2. 중복 간선에서 세기와 빼기가 다름.** 진입 차수는 중복 제거한 집합으로 세고(`indeg = 1`), 감소는 인접 리스트(중복 포함)로 하면 0 을 지나 음수(또는 uint16 wrap) — 반대로 세기는 중복 포함, 빼기는 중복 제거면 영원히 0 이 안 되어 **사이클로 오판**한다 (AlgoMonster 노트 M07 §18 이 지적하는 함정). 고침: 같은 간선 목록으로 세고 빼거나, 입력 단계에서 한 번 중복을 제거. 하네스의 "2→0 두 번" 사례가 이것을 지킨다.

**3. FIFO 큐로 짜 놓고 "작은 번호 먼저" 라고 말함.** 2. Problem 예에서 FIFO 는 `0 2 3 5 1 4`. 처음 준비 집합만 정렬해 넣는 것도 안 된다 — 나중에 준비되는 정점이 큐 뒤로 간다. 고침: 준비 집합을 min-heap(또는 스캔·비트마스크)으로.

**4. 간선 방향을 반대로.** `imu: spi1` 을 `imu → spi1` 로 넣고 Kahn 을 돌리면 logger 부터 나오는 **역순**이 나온다. 그 자체로는 "유효한 역순" 이라 눈으로는 잘 안 보인다. 고침: 방향을 문서화하고, 테스트가 출력 순서에 대해 **모든 간선** `pos[u] < pos[v]` 를 검사하게 한다(`valid_prefix`).

**5. 남은 정점을 "사이클" 로 보고.** `k < n` 일 때 남은 7개(`i2c1 pmic imu baro mag fusion logger`)를 사이클이라고 출력하면 imu·logger 를 고치러 가게 된다. 사이클은 `i2c1 -> pmic -> i2c1` 2개뿐, 나머지는 하류다(Part B 정리). 고침: 사이클은 DFS 로 따로 뽑는다.

**6. 재귀 DFS 를 MCU 에서.** 깊이가 의존 사슬 길이(최악 V)라 정적 스택 분석이 상한을 못 정하고, MISRA C 류 규칙은 재귀를 금지한다(C-A10). 고침: 명시적 스택(이 레슨), 또는 Kahn 만으로 사이클 유무를 판정하고 경로는 남은 집합 R 에서 공급자를 거슬러 반복을 찾는 식으로도 뽑을 수 있다(연습 아이디어 — 남은 정점 v 에서 "R 안의 공급자" 를 따라가다 처음 반복되는 정점부터가 사이클).

**7. 감소할 때마다 heap 에 push.** `indeg[v]--` 마다 push 하고 pop 때 "이미 뽑았나" 를 거르는 lazy 방식은 push 가 E 번까지 늘어 heap 용량을 MAX_V 로 잡은 정적 배열이 넘친다(펌웨어에서는 조용한 메모리 오염). 고침: `--indeg[v] == 0` 인 순간에만 push → push ≤ V 가 보장되어 MAX_V 칸으로 충분.

## 8. 면접 Q&A

<details><summary>Q1. Kahn 과 DFS(역후위) 위상 정렬, 무엇을 쓰겠어요?</summary>

둘 다 O(V+E). DFS 판은 정점이 끝날 때(BLACK) 뒤에서부터 채우면 위상 순서가 된다(연습 5). 이 문제에서는 Kahn 을 고른다: (1) 반복문이라 스택 깊이 걱정이 없고, (2) 준비 집합을 바꿔 끼우는 것만으로 tie-break(사전순, 우선순위)를 넣을 수 있고, (3) 사이클이 있어도 **올라갈 수 있는 것까지는** 진행해서 부분 부팅이 가능하고, (4) 준비 집합의 크기가 곧 병렬도라 wave 로 바로 이어진다. DFS 의 장점은 사이클 **경로**가 스택에 그대로 있다는 것 — 그래서 이 레슨은 "순서는 Kahn, 진단은 DFS" 로 나눴다. 꼬리: "DFS 판으로 사전순 최소를 얻을 수 있나?" → 정점을 오름차순으로 시작해도 일반적으로 아니다. 다이아몬드 3→{1,2}→0 에서 DFS 판은 `3 2 1 0`, 사전순 최소는 `3 1 2 0` (연습 5 실제 출력).

</details>

<details><summary>Q2. tie-break 비용을 정확히 말해 보세요. 드라이버가 20개뿐이면요?</summary>

heap: push·pop 이 정점당 한 번씩이라 O(V log V), 간선은 감소 O(1) 씩 → **O(E + V log V)**. 스캔: O(V² + E). 정점 ≤ 32 면 준비 집합을 `uint32_t` 하나로 두고 가장 낮은 1 비트를 꺼낸다 → O(V + E), heap 코드 0줄. 이 맥에서 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2` 로 본 `__builtin_ctz` + "가장 낮은 비트 지우기" 는 분기 없이 `rbit r0, r2` / `clz r0, r0` / `subs r3, r2, #1` / `ands r2, r3` 였다. Cortex-M0(`thumbv6m`, CLZ 명령 없음)에서는 같은 코드가 `rsbs`/`ands` 로 가장 낮은 비트만 남기고 `0x077CB531` 을 곱해 `lsrs #27` 한 값으로 32바이트 표를 읽는 **de Bruijn 곱셈** 시퀀스로 나왔다 (+ 0 입력 검사 분기). 둘 다 상수 시간이다. 측정으로는 V = 100 에서 스캔도 2.4 µs 라 20개면 무엇을 써도 된다 — 면접에서는 "규모를 보고 가장 단순한 것" 이라고 말하고 heap 판도 짤 수 있음을 보이면 된다.

</details>

<details><summary>Q3. 위상 순서가 유일한지 어떻게 판정하죠? 가능한 순서는 몇 개?</summary>

유일 ⇔ Kahn 진행 중 준비 집합 크기가 **항상 1** ⇔ 순서의 연속한 두 정점 사이에 모두 간선이 있다(해밀턴 경로가 존재). 둘 이상이 동시에 준비되면 둘을 바꾼 순서도 유효하기 때문이다 (AlgoMonster 노트 M07 §18 Reconstructing Sequence 가 이 판정). 개수 세기(선형 확장 수)는 일반적으로 #P-완전으로 알려져 있어(Brightwell–Winkler 1991) 작은 n 에서만 부분집합 비트마스크 DP O(2ⁿ·n) 로 센다 — 연습 1, 13 드라이버 그래프는 **1,346 가지**가 가능했다. 그래서 tie-break 를 정해 두지 않으면 같은 목록이라도 코드 변경(간선 입력 순서, 큐 구현)에 따라 부팅 순서가 바뀌고, "어제와 오늘 부팅 로그 순서가 다르다" 는 디버깅 지옥이 된다.

</details>

<details><summary>Q4. 사이클을 찾았으면 펌웨어는 무엇을 해야 하죠?</summary>

(1) 빌드 시점에 잡는 게 최선: 의존성 표를 빌드 스크립트가 위상 정렬하고 사이클이면 **빌드 실패** + 경로 출력. (2) 런타임(플러그인 모듈, 옵션 보드)이라면 사이클 하류만 막고 나머지는 올린다 — Kahn 의 k 개가 정확히 그 집합이다. 막힌 드라이버는 상태를 "deferred/failed" 로 남기고 사이클 경로를 로그/진단 채널로. (3) 근본 해결은 노드를 쪼개 사이클을 끊는 것: PMIC 의 리셋 기본 레일과 I2C 로 설정하는 레일을 다른 노드로, 클럭 트리의 "부트 클럭" 과 "PLL 설정 후 클럭" 을 다른 노드로. SCC 로 사이클 영역 전체를 보고 싶으면 ALGO-A10.

</details>

<details><summary>Q5. 의존성을 런타임에야 알 수 있으면요? 리눅스는 어떻게 하죠?</summary>

probe 를 해 봐야 공급자가 필요한지 아는 경우가 있다. 리눅스는 probe 가 `-EPROBE_DEFER` 를 돌려주면 그 장치를 미룬 목록에 넣고 다른 probe 가 성공할 때 다시 시도한다 (`drivers/base/dd.c`). 개념적으로 "변화 없을 때까지 반복" 이라 최악 O(V²) 시도 — demo 에서 역순 사슬 4000 개가 800만 번. 그래서 devicetree 의 참조에서 공급자를 미리 알아내 device link 로 소비자의 probe 를 공급자 뒤로 미루는 `fw_devlink` 가 들어왔다 (세부·기본값은 버전별 — 확인 권장). 정적 순서도 있다: initcall 레벨(`core_initcall` … `device_initcall` … `late_initcall`)은 링크 시점의 굵은 단계 구분이다. Zephyr 는 devicetree 로부터 빌드 시점에 장치 의존 순서(dependency ordinal)를 계산하고, 장치 초기화는 init 레벨(PRE_KERNEL_1/2, POST_KERNEL, APPLICATION) + 우선순위 숫자로 정한다 (세부는 Zephyr 문서 확인 권장). 공통점: **가능하면 빌드 시점에 위상 정렬**한다.

</details>

<details><summary>Q6. MCU 에 이 코드를 넣는다면 메모리는? 빌드 시점에 할 수는 없나요?</summary>

V 개 드라이버, E 개 간선이면 CSR = 2(V+1) + 2E B, 작업 배열(indeg, heap/done, order) = 6V B 남짓, DFS 는 color V + stk 2V + it 2V B. V = 64, E = 256 이면 다 합쳐 1.5 KB 아래다. 하지만 의존성이 컴파일 시점에 고정이면 **런타임에 정렬할 이유가 없다**: 빌드 스크립트가 순서(와 wave)를 계산해 `static const uint8_t init_order[]` 로 플래시에 굽고, 펌웨어는 그 표를 돌기만 한다(RAM 0, 실행 시간 결정적, 사이클은 빌드 실패). 런타임 정렬은 모듈 조합이 현장에서 바뀌는 경우(옵션 보드 감지, 동적 로드)에만. 그때는 정적 작업 공간을 호출자 소유로 바꿔 재진입성도 챙긴다.

</details>

<details><summary>Q7. 병렬로 올리면 부팅 시간은? 워커가 2개뿐이면?</summary>

워커가 무한(완전 비동기 probe)이면 단위 시간 기준 단계 수 = 최장 경로 + 1 (Part C 의 wave, 13 드라이버는 7 단계). 드라이버마다 시간이 다르면 가중 최장 경로 = **임계 경로(critical path)** — 연습 4: 같은 그래프에 ms 를 붙이면 순차 91 ms → 병렬 45 ms, 임계 경로 `clock gpio i2c1 pmic imu fusion logger`. 부팅을 줄이려면 이 경로 위의 드라이버(여기서는 imu 자기 시험 30 ms)만 줄이면 된다. 워커 수 m 이 유한하면 선행 제약 스케줄링(P|prec|Cmax)이 되어 m 이 입력의 일부일 때 NP-hard 로 알려져 있고, 단위 시간·m = 2 는 Coffman–Graham 알고리즘으로 다항 시간에 최적이 된다. 현실에서는 리스트 스케줄링(준비된 것 중 임계 경로가 긴 것 먼저)이 쓰인다. 오프라인 태스크 스케줄링(쿨다운, 마감 시간)은 ALGO-M10.

</details>

<details><summary>Q8. 종료(shutdown)·절전(suspend) 순서는요? 드라이버 하나만 리셋하면?</summary>

종료·suspend 는 **역위상 순서**(소비자 먼저 끄고 공급자를 나중에), resume 은 다시 정순서다. 리눅스도 장치 목록을 공급자 → 소비자 순으로 유지하고 suspend 는 역순으로 돈다는 것이 기본 아이디어다(세부는 확인 권장). 하나만 리셋(예: I2C 버스 복구)하면 그 드라이버와 **도달 가능한 모든 소비자**를 역순으로 내리고 정순서로 다시 올린다 — 연습 3: i2c1 리셋 → `i2c1 pmic imu baro mag fusion logger` 재초기화, 종료는 그 역순. flash·fs 는 i2c1 하류가 아니라서 건드리지 않는다 (로그 파일을 닫지 않아도 된다).

</details>

## 9. 추가 연습

### 연습 1 — 가능한 초기화 순서의 개수

`count_topo_orders(g)` (n ≤ 16): 부분집합 비트마스크 DP. n ≤ 7 무작위 그래프(가끔 2-사이클 주입)는 순열 전수와, 2×k 격자 포셋(a₁→…→a_k, b₁→…→b_k, aᵢ→bᵢ)은 Catalan(k)(OEIS A000108)과 대조. 13 드라이버 그래프의 개수도 출력.

<details><summary>답 보기</summary>

`dp[S]` = "먼저 초기화된 집합이 S 가 되는 순서의 수". S 에 v 를 더할 수 있는 조건은 v ∉ S 이고 v 의 공급자 비트마스크 `pred[v] ⊆ S`. `dp[S | 1<<v] += dp[S]`. 전이가 항상 더 큰 정수로만 가므로 S 를 0 부터 오름차순으로 돌면 계산 순서가 맞다. 답 `dp[(1<<n)-1]`, 사이클이면 0. 시간 O(2ⁿ·n), 메모리 2ⁿ × 8 B (n = 16 이면 512 KB — 데스크톱 도구용). 실제 출력:

```text
연습 1: 400 random graphs (n<=7) vs permutation brute force, mismatches=0
  2xk grid: 1 2 5 14 42 132 429 1430  (Catalan 1..8)
  13-driver graph: 1346 valid init orders
```

`uint64_t` 로 충분한 이유: 개수는 n! 이하이고 16! ≈ 2.09×10¹³ < 2⁶⁴.

</details>

### 연습 2 — 외계어 사전 (Alien Dictionary)

미지의 알파벳 순서로 정렬된 단어 목록에서 등장한 글자의 순서를 구한다. 여러 답이면 일반 사전순 최소, 모순(사이클, 또는 `"abc"` 다음 `"ab"`)이면 실패. 예: `["wrt","wrf","er","ett","rftt"]` → `"wertf"`. 무작위 알파벳 6글자로 단어 30개를 만들어 그 순서로 정렬한 사전 200개로 검사.

<details><summary>답 보기</summary>

인접한 두 단어의 **첫 다른 글자**에서만 간선 a→b 가 생긴다(그 뒤 글자는 정보 없음). 다른 글자가 없는데 앞 단어가 더 길면 정렬 자체가 불가능 → 실패. 글자 26개를 정점으로, 등장한 글자만 출력에 넣는다. 사전순 최소는 본문과 같은 Kahn + 최소 선택(여기서는 26비트 마스크). 무작위 검사는 "결과 순서로 다시 비교해도 모든 인접 쌍이 정렬돼 있다" 를 확인한다 (원래 알파벳과 같을 필요는 없다 — 정보가 부족하면 여러 답이 가능하므로). 실제 출력 `연습 2: 200 random sorted dictionaries, inconsistent results=0`. AlgoMonster 노트 M07 §20 과 같은 문제다.

</details>

### 연습 3 — 리셋 후 재초기화 집합

드라이버 x 가 리셋됐을 때 다시 초기화할 드라이버(x 와 x 에서 도달 가능한 모든 것)를 부팅 때와 같은 상대 순서로 출력하고, 종료 순서(역순)도 보여라. 무작위 DAG 300개는 Warshall 폐포와 대조.

<details><summary>답 보기</summary>

반복형 DFS 로 x 에서 도달 가능한 정점을 표시하고, 전체 사전순 최소 순서에서 표시된 것만 골라낸다. 부분 그래프의 위상 순서임은 자명하다(전체 순서의 부분 수열). 주의: 부분 그래프를 따로 Kahn 하면 **부분 그래프의** 사전순 최소가 나와 부팅 때와 상대 순서가 다를 수 있다 — 재현성을 위해 부팅 순서를 필터링하는 쪽을 택했다. 실제 출력:

```text
연습 3: reset i2c1 -> reinit: i2c1 pmic imu baro mag fusion logger
        shutdown (reverse): logger fusion mag baro imu pmic i2c1
연습 3: 300 random DAGs vs Warshall closure, mismatches=0
```

</details>

### 연습 4 — 병렬 probe 부팅 시간 (임계 경로)

드라이버마다 초기화 시간 `ms[v]`(clock 1, gpio 1, dma 1, i2c1 2, spi1 2, pmic 5, imu 30, baro 12, mag 8, flash 3, fs 20, fusion 2, logger 4)가 있고 워커가 무한일 때 부팅 시간과 임계 경로를 구하라. n ≤ 12 무작위 가중 DAG 는 "모든 경로 나열" 지수 시간 기준과 대조.

<details><summary>답 보기</summary>

위상 순서로 `finish[u] = start[u] + ms[u]`, 간선 u→v 마다 `start[v] = max(start[v], finish[u])` 와 그 max 를 준 u 를 `best_pred[v]` 에 기록. 답은 전체 finish 의 최댓값(싱크가 여럿일 수 있으니 마지막 정점의 값이 아니다), 경로는 끝에서 `best_pred` 를 거슬러 뒤집는다. O(V+E). 실제 출력:

```text
연습 4: parallel boot = 45 ms (sequential sum 91 ms)
        critical path: clock gpio i2c1 pmic imu fusion logger
연습 4: 300 random weighted DAGs (n<=12) vs all-paths brute force, mismatches=0
```

AlgoMonster 노트 M07 §19 (Task Scheduling 2) 와 같은 DP 다.

</details>

### 연습 5 — DFS 역후위 위상 정렬

반복형 3색 DFS 로, 정점이 BLACK 이 되는 순간 `order` 를 뒤에서부터 채워라. GRAY 를 만나면 -1. 무작위 그래프 500개에서 Kahn 과 사이클 판정이 일치하는지, 출력이 유효한지 검사.

<details><summary>답 보기</summary>

간선 u→v 에 대해 v 는 u 보다 먼저 끝난다(v 가 WHITE 면 u 의 자손이 되어 먼저 끝나고, BLACK 이면 이미 끝났고, GRAY 면 사이클). 그래서 "끝난 순서의 역순" 에서 u 가 v 앞 → 위상 순서. 실제 출력:

```text
연습 5: diamond 3->{1,2}->0: dfs order = 3 2 1 0
        13 drivers, dfs order: clock dma gpio spi1 flash fs i2c1 pmic mag baro imu fusion logger
연습 5: 500 random graphs, dfs vs kahn disagreement / invalid=0
```

유효하지만 사전순 최소(`3 1 2 0`, `clock gpio dma i2c1 …`)와 다르다 — Q1 의 꼬리질문 답. 스택 깊이는 최장 DFS 경로 ≤ V.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 20/20, 보고 없음
```

## 10. 핵심 정리

- 간선 = 공급자 → 소비자("u 먼저"). 방향을 정하고, 테스트는 출력에 대해 **모든 간선** `pos[u] < pos[v]` 를 검사.
- Kahn 불변식: `indeg[v]` = 아직 안 나온 공급자 간선 수. 꺼낼 때 0 이면 유효, 멈췄을 때 남은 게 있으면 사이클(공급자를 거슬러 가면 반복).
- 결정적 순서 = 준비 집합에서 최소 id: heap O(E + V log V), 스캔 O(V² + E), ≤ 64개면 비트마스크 ctz O(V + E). 결과는 사전순 최소이고 유일(귀납 증명).
- 실측: V = 10⁴ 에서 스캔은 heap 의 100~800배, V = 100 에서는 2.4 µs — 드라이버 규모에서는 가장 단순한 코드가 정답.
- 사이클 경로 = 3색 DFS 에서 GRAY 로 가는 간선 순간의 스택 조각. visited 하나로는 방향 그래프에서 거짓 사이클.
- Kahn 이 남기는 것 = 사이클 + 그 하류. 보고서는 사이클 경로 / 올라간 것 / 막힌 것을 나눠서. 부분 부팅이 가능해진다.
- wave = 최장 경로(위상 순서 위 DP), 시간 가중이면 임계 경로. 종료·suspend 는 역순, 부분 리셋은 하류만.
- 펌웨어 정석: 의존성이 고정이면 **빌드 시점에 정렬해 const 표로**, 사이클은 빌드 실패. 런타임 재시도(deferred probe 식)는 최악 V(V+1)/2 시도(실측 4000 → 8,002,000).

## 참고자료

- AlgoMonster 노트 M07 §16 Topological Sort Intro, §17 Task Scheduling, §18 Reconstructing Sequence, §19 Task Scheduling 2, §20 Alien Dictionary, §21 Course Schedule (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M07_graph.md`
- A. B. Kahn, "Topological sorting of large networks", *Communications of the ACM* 5(11), 1962 — Kahn 알고리즘 원 논문.
- T. H. Cormen et al., *Introduction to Algorithms* (CLRS) 3rd ed., §22.3 DFS (괄호 정리·흰 경로 정리·간선 분류), §22.4 Topological sort.
- R. Sedgewick, K. Wayne, *Algorithms* 4th ed., §4.2 Directed Graphs — DirectedCycle(스택 위 정점 표시로 사이클 경로), DepthFirstOrder(역후위).
- OEIS A003024 (n 개 레이블 정점의 DAG 수: 1, 1, 3, 25, 543, 29281, …) — <https://oeis.org/A003024> · A000108 Catalan 수 — <https://oeis.org/A000108>
- G. Brightwell, P. Winkler, "Counting linear extensions", *Order* 8, 1991 — 선형 확장 수 세기의 #P-완전성.
- E. G. Coffman, R. L. Graham, "Optimal scheduling for two-processor systems", *Acta Informatica* 1, 1972 — 단위 시간·2 프로세서 선행 제약 스케줄링.
- Linux kernel 문서 — Driver model: `Documentation/driver-api/driver-model/`, device links: `Documentation/driver-api/device_link.rst`, `fw_devlink=` 파라미터: `Documentation/admin-guide/kernel-parameters.txt`, deferred probe 구현: `drivers/base/dd.c` (버전별 차이 확인 권장).
- Zephyr Project 문서 — Device Driver Model (init levels, priorities), Devicetree "dependency ordinals" (버전별 확인 권장).
- LeetCode 207 Course Schedule · 210 Course Schedule II · 269 Alien Dictionary · 444 Sequence Reconstruction · 1136 Parallel Courses (wave 수와 같은 문제).
- 관련 레슨: DS-B12 (이진 힙), DS-M11 (인덱스 우선순위 큐), ALGO-B11 (재귀 → 명시적 스택), ALGO-M04 (격자 BFS/DFS), ALGO-M10 (오프라인 스케줄링), ALGO-A10 (SCC·증거 사이클·락 순서), C-A10 (MISRA 스타일: 재귀 금지).
