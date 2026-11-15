---
id: ALGO-A03
time: 110분
updated: 2026-11-11
---
# ALGO-A03 비트마스크 DP: 테스트 포인트 방문 순서(TSP)

## 학습 목표

- 외판원 문제(TSP, traveling salesman problem)를 **Held–Karp 비트마스크 DP** `dp[mask][last]` 로 풀고, 상태·전이·기저·순서·답을 한 문장씩 말하며 최적 부분 구조를 증명한다. 시간 O(2^n·n²)(정확히 n(n−1)2^(n−2) 전이), 메모리 O(2^n·n) — n = 16 에서 실제 바이트(5 MiB)와 왜 MCU 에 안 맞는지 계산한다.
- 부모 표로 **최적 순서를 역추적**하고, 순열 브루트포스(n ≤ 9, 1830 사례)·비대칭 행렬·선행 제약·막힌 이동과 대조해 맞음을 확인한다.
- 문제 정의를 먼저 고정한다: 홈으로 **돌아오는가(closed) / 아무 데서나 끝나는가(open)**, 거리는 **유클리드(반올림) / 맨해튼 / 체비쇼프** 중 무엇인가 — XY 갠트리에서 체비쇼프를 고르는 이유와, 반올림 유클리드가 삼각 부등식을 깨는 실제 예를 든다.
- 최근접 이웃(NN) + 2-opt 휴리스틱의 최적 대비 격차를 무작위 보드 수천 장에서 측정하고, "언제 DP, 언제 휴리스틱" 을 숫자로 답한다.
- dp 엔트리 폭(uint16 vs uint32), 포화 덧셈, 메모리 절반 접기(연습 1)처럼 펌웨어 면접의 꼬리질문에 답한다.

## 1. 문제

임베디드 면접에서 "지수 문제를 다항식 곱하기 2^n 으로 줄이는" 비트마스크 DP 는 **작은 n 에서 정확해가 필요할 때**(테스트 순서, 센서 폴링 순서, 칩 핀 측정 순서) 단골이고, 꼬리질문은 늘 "그 표가 RAM 에 들어가나?" 로 간다.

상황: 생산 라인의 **플라잉 프로브 테스터**(flying probe tester) — 고정 지그(bed-of-nails) 없이 XY 갠트리에 달린 프로브가 보드의 테스트 포인트(TP)를 하나씩 찍는다. 한 보드에 TP 가 n ≤ 16 개, 보드 수천 장을 같은 순서로 돌리므로 **순서 한 번 잘 정하면 모든 보드에서 이득**이다. 프로브는 홈 H(park 위치, 보드 로딩 후 기준점 보정을 마친 자리)에서 출발한다.

먼저 정의를 고정한다 — 면접에서 이걸 묻지 않고 코딩부터 하면 감점이다.

| 질문 | 이 레슨의 답 | 왜 |
|---|---|---|
| 끝나고 홈으로 돌아오나? | **과제 = open** (H 에서 출발, 마지막 TP 에서 끝). closed 도 구현 | 측정이 끝나면 다음 명령(다음 보드의 첫 점, 혹은 재측정)이 거기서 시작한다. 보드 반출 전에 프로브를 안전 위치로 치워야 하는 장비라면 closed — 같은 표에서 답만 바꿔 고른다 |
| 비용 = 무엇? | 이동 **시간**. 기본은 **체비쇼프** max(\|dx\|,\|dy\|) | X·Y 모터가 각자 같은 최고 속도로 **동시에** 움직이면 이동 시간은 긴 축이 정한다 (Part C) |
| 좌표 단위 | 10 µm 정수, 보드 100 × 80 mm | 부동소수점 없이, 비용을 정수로 정확히 비교 |
| Z 축(내리기·접촉·올리기) | 무시 | 점마다 같은 상수 → 모든 순서에 n·c 가 더해질 뿐, 최적 순서는 그대로 |
| 비용이 대칭? | DP 는 대칭을 **가정하지 않는다** (비대칭도 테스트). 2-opt 만 대칭 가정 | 축마다 속도가 다르거나 백래시 보정이 방향마다 다르면 비대칭이 된다 |

```svg
<svg viewBox="0 0 760 500" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="14" font-weight="bold">16점 보드 (100 x 80 mm): 체비쇼프 open 최적 253.50 mm (실선) vs NN+2-opt 263.50 mm (점선)</text>
  <rect x="70.0" y="40.0" width="500.0" height="400.0" rx="6" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <polyline points="45.0,465.0 130.0,387.5 115.0,290.0 160.0,235.0 152.5,225.0 185.0,242.5 197.5,295.0 260.0,277.5 335.0,345.0 385.0,395.0 192.5,137.5 287.5,187.5 375.0,57.5 402.5,117.5 495.0,217.5 542.5,112.5 532.5,62.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="5 4" opacity="0.6"/>
  <polyline points="45.0,465.0 130.0,387.5 115.0,290.0 160.0,235.0 152.5,225.0 185.0,242.5 197.5,295.0 260.0,277.5 335.0,345.0 385.0,395.0 495.0,217.5 542.5,112.5 532.5,62.5 402.5,117.5 375.0,57.5 287.5,187.5 192.5,137.5" fill="none" stroke="currentColor" stroke-width="2"/>
  <circle cx="197.5" cy="295.0" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="335.0" cy="345.0" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="185.0" cy="242.5" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="287.5" cy="187.5" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="130.0" cy="387.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="115.0" cy="290.0" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="402.5" cy="117.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="532.5" cy="62.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="385.0" cy="395.0" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="152.5" cy="225.0" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="375.0" cy="57.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="542.5" cy="112.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="495.0" cy="217.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="260.0" cy="277.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="192.5" cy="137.5" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="160.0" cy="235.0" r="6" class="f-mut" stroke="currentColor"/>
  <text x="205.5" y="309.0" fill="currentColor" font-family="monospace">VBAT</text>
  <text x="343.0" y="337.0" fill="currentColor" font-family="monospace">3V3</text>
  <text x="194.0" y="246.5" fill="currentColor" font-family="monospace">1V8</text>
  <text x="295.5" y="179.5" fill="currentColor" font-family="monospace">VCORE</text>
  <text x="138.0" y="401.5" fill="currentColor" font-family="monospace">SWDIO</text>
  <text x="59.0" y="282.0" fill="currentColor" font-family="monospace">SWCLK</text>
  <text x="362.5" y="133.5" fill="currentColor" font-family="monospace">nRST</text>
  <text x="502.5" y="52.5" fill="currentColor" font-family="monospace">UART_TX</text>
  <text x="393.0" y="409.0" fill="currentColor" font-family="monospace">UART_RX</text>
  <text x="94.5" y="217.0" fill="currentColor" font-family="monospace">CAN_H</text>
  <text x="355.0" y="45.5" fill="currentColor" font-family="monospace">CAN_L</text>
  <text x="550.5" y="126.5" fill="currentColor" font-family="monospace">XTAL</text>
  <text x="503.0" y="231.5" fill="currentColor" font-family="monospace">GND</text>
  <text x="268.0" y="291.5" fill="currentColor" font-family="monospace">ADC_IN</text>
  <text x="172.5" y="125.5" fill="currentColor" font-family="monospace">PWM_A</text>
  <text x="96.0" y="255.0" fill="currentColor" font-family="monospace">I2C_SDA</text>
  <rect x="38.0" y="458.0" width="14" height="14" class="f-acc" stroke="currentColor"/>
  <text x="57.0" y="470.0" fill="currentColor">H 홈 (-5, -5 mm)</text>
  <line x1="588" y1="60" x2="628" y2="60" stroke="currentColor" stroke-width="2"/><text x="636" y="64" fill="currentColor">최적 (DP)</text>
  <line x1="588" y1="84" x2="628" y2="84" stroke="currentColor" stroke-dasharray="5 4" opacity="0.6"/><text x="636" y="88" fill="currentColor">NN+2-opt</text>
  <circle cx="596" cy="110" r="6" class="f-warn" stroke="currentColor"/><text x="608" y="114" fill="currentColor">전원 레일 4개</text>
  <circle cx="596" cy="134" r="6" class="f-mut" stroke="currentColor"/><text x="608" y="138" fill="currentColor">신호/기타</text>
  <text x="588" y="180" fill="currentColor">두 경로는 UART_RX 까지</text>
  <text x="588" y="198" fill="currentColor">같고 꼬리만 다르다.</text>
  <text x="588" y="216" fill="currentColor">2-opt 는 구간 뒤집기만</text>
  <text x="588" y="234" fill="currentColor">하므로 이 꼬리에 못 닿는다</text>
  <text x="588" y="252" fill="currentColor">(국소 최적, +10 mm).</text>
  <text x="10" y="490" fill="currentColor">좌표·순서는 solution.c 의 BOARD[] 와 실제 출력 그대로 (위 = +y). 체비쇼프: 긴 축 거리 = 이동 시간.</text>
</svg>
```

위 그림이 Part C 의 보드다. 전원 레일 4개(VBAT, 3V3, 1V8, VCORE)와 신호 12개. 실선은 DP 가 찾은 최적 open 경로 253.50 mm, 점선은 NN + 2-opt 가 멈춘 국소 최적 263.50 mm.

### Part A — Held–Karp: 표 채우기 · 답 · 역추적

- `hk_fill(t)` — `dp[mask][j]` = "H 에서 출발해 mask 의 점을 **정확히 한 번씩** 찍고 j 에서 끝나는 최소 비용" 을 mask 오름차순으로 채운다. `par[mask][j]` = 그 경로에서 j 바로 앞 점. 살펴본 전이 후보 수 `g_trans` 를 센다.
- `hk_answer(t, closed, &last)` — open: min_j dp[full][j], closed: min_j dp[full][j] + d[j][H]. n = 0 이면 0.
- `hk_backtrack(t, last, order)` — `(full, last)` 에서 par 를 따라 홈까지 거꾸로 걸으며 `order[]` 를 뒤에서부터 채운다.
- 테스트: 손 계산 예(점 4개, open 11 / closed 17, 표의 칸 값과 부모까지), 무작위 보드 n = 0..9 × 세 거리 × open/closed 1830 사례를 **모든 순열 브루트포스**와 대조(비용 같음, 순서가 순열, 순서의 비용 = 반환값), 전이 수 = n(n−1)2^(n−2), 비대칭 행렬 60개.

### Part B — 경계, 제약, 정수 폭

- n = 0(움직일 필요 없음 = 0), n = 1, open ≤ closed 사이의 정확한 부등식 `open + min_j d[j][H] ≤ closed ≤ open + d[last_open][H]`.
- **선행 제약** `pre[j]`(j 보다 먼저 찍어야 하는 점들의 마스크): 상태 (mask, last) 를 늘리지 않고 전이에서 `pre[j] ⊆ mask\{j}` 만 검사 — 왜 충분한지 증명. 순환 제약은 불가능(COST_INF).
- **막힌 이동**(COST_INF, 예: 키 큰 부품 위로 대각 이동 금지)과 **포화 덧셈** — 참값이 2^32 를 넘으면 작은 값으로 감기지 않고 INF. 기준은 uint64 브루트포스.

### Part C — 거리 함수, 16점 보드, 휴리스틱과의 격차

- `metric_dist(a, b, m)` — 맨해튼 / 체비쇼프 / **반올림 유클리드**(부동소수점 없이 `isqrt` + 반올림 규칙). 반올림 유클리드가 삼각 부등식을 깨는 예 (0,0)–(1,1)–(2,2).
- 위 보드: 최적 open / closed, 전원 레일 먼저 제약의 비용, 세 거리로 각각 최적화한 순서를 서로의 거리로 평가한 3×3 표.
- `nn_tour`, `two_opt` — 무작위 보드 300장(n = 10)에서 최적 ≤ 2-opt ≤ NN, 2-opt 결과가 정말 국소 최적인지(모든 구간 뒤집기를 독립적으로 재계산) 확인. 큰 측정은 `demo_gap.c`(보드 3600장), 시간·메모리는 `demo_bench.c`.

## 2. Problem (English)

A flying-probe tester must touch `n` test points (`n <= 16`) on a PCB. The probe starts at its home position `H`. Given an `(n+1) x (n+1)` integer cost matrix (`d[i][j]` = move time from `i` to `j`, node `n` = `H`, `COST_INF` = forbidden move; the matrix need not be symmetric), return the minimum total cost to visit every test point exactly once, and one optimal visiting order.

```c
typedef uint32_t cost_t;                 /* COST_INF = UINT32_MAX */
cost_t tsp_solve(const tsp_t *t, int closed, uint8_t *order);
/* closed = 0: open path H -> ... -> last point (ends anywhere)
 * closed = 1: tour that returns to H
 * t->pre[j]: bitmask of points that must be visited before j (0 = none)
 * returns COST_INF if no feasible order exists (or the optimum does not fit in 32 bits) */
cost_t metric_dist(pt_t a, pt_t b, metric_t m);   /* EUCLID (rounded), MANHATTAN, CHEBYSHEV */
cost_t nn_tour(const tsp_t *t, int closed, uint8_t *order);
cost_t two_opt(const tsp_t *t, int closed, uint8_t *order);  /* symmetric costs only */
```

Constraints: `n <= 16`, no dynamic allocation, deterministic output (ties → smallest index), whole test program < 2 s.

Example (Chebyshev metric, `H = (0,0)`, `A(6,4) B(4,2) C(7,0) D(5,5)`):

```text
open   -> 11   order H D A B C   (5 + 1 + 2 + 3)
closed -> 17   order H D A C B H (5 + 1 + 4 + 3 + 4)
greedy nearest-neighbour open -> 12  (H B A D C)
```

Follow-ups: How much memory for n = 16? Could it run on a 192 KB MCU? What if some points must be probed before others? Which metric for an XY gantry? How far from optimal is 2-opt?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter
```

TODO 는 여섯 군데: `hk_fill`, `hk_answer`, `hk_backtrack`(Part A·B), `metric_dist`, `nn_tour`, `two_opt`(Part C). 포화 덧셈 `cadd`, `tsp_solve` 묶음, `tour_cost`, `tour_valid`, `isqrt_u64`, `build_board`, 브루트포스 기준과 하네스는 주어진다. 스텁은 "불가능(COST_INF)" / 거리 0 / 번호 순서를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:216  open == 11
FAIL starter.c:217  o[0] == 3 && o[1] == 0 && o[2] == 1 && o[3] == 2
FAIL starter.c:218  g_trans == 48
FAIL starter.c:219  hk_dp(0x9u, 0) == 6 && hk_dp(0xFu, 2) == 11 && hk_par(0xFu, 2) == 1
hand open  =11: H A A A A
…
FAIL starter.c:427  ok && x[0][0] == opt[0] && x[1][1] == opt[1] && x[2][2] == opt[2]
FAIL starter.c:450  viol == 0
4/34 passed
```

(exit 1. 통과하는 4개는 TODO 와 무관하거나 "불가능" 이 정답인 검사: 순환 선행 제약, 2^32 넘는 참값 → INF, 주어진 `isqrt_u64` 확인, n = 2 포화.)

## 4. 힌트

<details><summary>힌트 1 — 상태에 "어디까지 왔나" 와 "지금 어디 있나" 둘 다</summary>

다음에 어디로 갈 수 있는지(안 찍은 점)는 **찍은 집합**만 알면 되고, 다음 이동 비용은 **지금 위치**만 알면 된다. 찍은 **순서**는 미래와 무관하다. 그래서 상태 = (집합 mask, 마지막 점 j). 집합 하나당 최선 하나만 기억하면 된다 — 순열 n! 개가 상태 2^n·n 개로 접힌다.

</details>

<details><summary>힌트 2 — 채우는 순서는 그냥 숫자 오름차순</summary>

`dp[mask][j]` 는 `dp[mask ^ (1<<j)][i]` 에서 온다. `mask ^ (1<<j)` 는 mask 에서 비트 하나를 뺀 것이라 **숫자로 더 작다**. 그러니 `for (mask = 1; mask <= full; mask++)` 이면 필요한 칸이 항상 먼저 채워져 있다. popcount 층별로 돌 필요 없다.

</details>

<details><summary>힌트 3 — 기저는 0 이 아니다</summary>

`dp[{j}][j] = d[H][j]` — 홈에서 첫 점까지 가는 비용. 0 으로 두면 "첫 점을 공짜로 고르는" 문제가 된다(연습 2 의 시작점 자유 문제). 7. 실수 1 에서 실제로 15/34 로 떨어진다.

</details>

<details><summary>힌트 4 — 역추적은 비트를 지우기 전에 부모를 읽는다</summary>

`p = par[mask][j]; mask ^= 1<<j; j = p;` 순서. 비트를 먼저 지우고 `par[mask][j]` 를 읽으면 j 가 없는 집합의 칸(쓰레기)을 읽는다.

</details>

<details><summary>힌트 5 — 반올림 유클리드를 정수로, 2-opt 는 바뀌는 간선 두 개만</summary>

r = ⌊√s⌋ 일 때 √s ≥ r + ½ ⇔ s ≥ r² + r + ¼ ⇔ (s 가 정수니까) s − r² > r. 2-opt 에서 `order[i..j]` 를 뒤집으면 대칭 비용에서는 안쪽 간선 비용이 그대로이고 양 끝 간선 두 개만 바뀐다. open 경로의 끝(j = n−1)에는 뒤 간선이 **없다**.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 돌아오나(closed) 아무 데서 끝나나(open)? 시작점은 고정(홈)? 비용은 거리인가 **시간**인가 — 축이 동시에 움직이나? 비대칭? 금지된 이동? 선행 제약? n 의 상한(16 → 2^16 이 키워드), 답은 값만 vs 순서도 |
| 2. 단순 해법 | 모든 순열 n! — n = 12 면 4.8억 개, 이 맥에서 8.7 초(아래 실측). 순서마다 비용 O(1) 누적 DFS |
| 3. 개선 + 불변식 | 미래는 (찍은 집합, 현재 위치) 만으로 정해진다 → 상태 2^n·n. **상태·전이·기저·순서·답** 한 문장씩 + 잘라 붙이기(cut-and-paste) 증명 |
| 4. 코드 | 표 초기화(INF) → 기저 → mask 오름차순 전이(포화 덧셈) → 답 → 부모 역추적 |
| 5. 소리 내어 말할 테스트 | n = 0, 1, 2 / 모든 점이 한 곳(거리 0, 동점) / 한 줄에 놓인 점 / 비대칭 / 막힌 이동으로 불가능 / 순환 선행 제약 / 비용 합이 2^32 근처 / n ≤ 9 는 브루트포스와 무작위 대조 |
| 6. 복잡도 | 시간 n(n−1)2^(n−2) 전이 = O(2^n·n²), 메모리 2^n·n 칸 (n = 16: dp 4 MiB + par 1 MiB). 재귀 없음, 스택 상수. 실행 시간이 입력 값에 **무관**(항상 같은 전이 수) — WCET 를 말할 수 있다 |

### Part A — 상태 · 전이 · 기저 · 순서 · 답

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a03a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">전이 한 칸: dp[1011][B] = min over i ∈ {A, D} of ( dp[1001][i] + d[i][B] )</text>
  <text x="10" y="50" fill="currentColor">비트 = 점 (bit0 = A, bit1 = B, bit2 = C, bit3 = D). mask 1011 = {A, B, D}, 마지막 점 = B</text>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="90" y="90">D C B A</text>
  </g>
  <g stroke="currentColor">
    <rect x="50" y="100" width="20" height="26" class="f-acc"/><rect x="70" y="100" width="20" height="26" class="f-mut"/>
    <rect x="90" y="100" width="20" height="26" class="f-warn"/><rect x="110" y="100" width="20" height="26" class="f-acc"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="60" y="118">1</text><text x="80" y="118">0</text><text x="100" y="118">1</text><text x="120" y="118">1</text>
  </g>
  <text x="140" y="118" fill="currentColor">= 1011, 끝 = B (경고색 비트)</text>
  <path d="M100 130 L100 165" stroke="currentColor" marker-end="url(#a03a)"/>
  <text x="108" y="152" fill="currentColor">B 비트를 지운다: pm = 1011 ^ 0010 = 1001 = {A, D}</text>
  <g stroke="currentColor">
    <rect x="50" y="172" width="20" height="26" class="f-acc"/><rect x="70" y="172" width="20" height="26" class="f-mut"/>
    <rect x="90" y="172" width="20" height="26" class="f-mut"/><rect x="110" y="172" width="20" height="26" class="f-acc"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <text x="60" y="190">1</text><text x="80" y="190">0</text><text x="100" y="190">0</text><text x="120" y="190">1</text>
  </g>
  <rect x="320" y="160" width="190" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="415" y="182" fill="currentColor" text-anchor="middle" font-family="monospace">dp[1001][A] = 6 (H→D→A)</text>
  <rect x="320" y="204" width="190" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="415" y="226" fill="currentColor" text-anchor="middle" font-family="monospace">dp[1001][D] = 7 (H→A→D)</text>
  <rect x="560" y="182" width="170" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="645" y="204" fill="currentColor" text-anchor="middle" font-family="monospace">dp[1011][B] = 8</text>
  <path d="M510 177 L558 193" stroke="currentColor" stroke-width="2" marker-end="url(#a03a)"/>
  <path d="M510 221 L558 207" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#a03a)"/>
  <text x="520" y="168" fill="currentColor">+ d[A][B]=2 → 8 ✓</text>
  <text x="520" y="252" fill="currentColor">+ d[D][B]=3 → 10</text>
  <text x="10" y="282" fill="currentColor">pm = 1001 &lt; 1011 → mask 를 숫자 오름차순으로 채우면 오른쪽 칸들이 항상 먼저 준비돼 있다.</text>
  <text x="10" y="304" fill="currentColor">par[1011][B] = A. 칸 하나 = 후보 (popcount−1) 개를 보는 min. 값은 demo_trace.c 의 실제 표.</text>
</svg>
```

**정의.** 점 0..n−1, 홈 H = n. 비트마스크 S ⊆ {0..n−1}, j ∈ S.

- **상태**: `dp[S][j]` = H 에서 출발해 S 의 점을 정확히 한 번씩 방문하고 j 에서 끝나는 경로의 최소 비용 (없으면 INF).
- **기저**: `dp[{j}][j] = d[H][j]`.
- **전이**: |S| ≥ 2 이면 `dp[S][j] = min_{i ∈ S\{j}} dp[S\{j}][i] + d[i][j]`.
- **순서**: S 를 정수로 보아 오름차순. S\{j} < S 이므로 우변은 이미 계산됨.
- **답**: open = min_j dp[full][j], closed = min_j (dp[full][j] + d[j][H]). n = 0 이면 둘 다 0.

**정확성 (잘라 붙이기).** S 를 정확히 한 번씩 돌아 j 에서 끝나는 아무 경로 P 를 보자. |S| ≥ 2 면 j 바로 앞 점 i ∈ S\{j} 가 있고, P = (H → … → i 까지의 앞부분 Q) + 간선 (i, j). Q 는 S\{j} 를 정확히 한 번씩 돌아 i 에서 끝나는 경로이므로 cost(Q) ≥ dp[S\{j}][i] → cost(P) ≥ 우변. 반대로 우변의 최솟값을 주는 i 와 그 칸의 최적 경로 Q* 에 (i, j) 를 붙이면 j 가 Q* 에 없으니(Q* 는 S\{j} 만 지남) 올바른 경로가 되고 비용이 정확히 우변이다 → 등호. S 의 크기에 대한 귀납으로 모든 칸이 정확하다. 쓰인 성질은 **비용이 간선 비용의 합이고, 간선 비용이 이전 경로에 의존하지 않는다** 는 것 하나뿐 — 대칭도, 삼각 부등식도, 음이 아님도 필요 없다(각 점을 한 번만 지나므로 음의 사이클 문제도 없다). 이것이 깨지는 경우는 Q2.

**역추적.** `par[S][j]` = 최솟값을 준 i (기저는 PAR_HOME). (full, last) 에서 `p = par[S][j]; S ^= 1<<j; j = p` 를 n 번 하면 순서가 뒤에서부터 나오고, 마지막에 j = PAR_HOME, S = 0 이어야 한다 — `hk_backtrack` 은 이 두 조건과 "j 가 S 에 있다" 를 매 걸음 확인해 표가 망가졌으면 −1 을 돌려준다.

### Part A — 추적 표 (손 계산 예)

`demo_trace.c` 의 실제 출력 (체비쇼프, H = (0,0), A(6,4) B(4,2) C(7,0) D(5,5)). 칸 = `비용/앞 점`.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace -lm && ./demo_trace
```

```text
d (chebyshev):    A  B  C  D  H
            A:   0  2  4  1  6
            B:   2  0  3  3  4
            C:   4  3  0  5  7
            D:   1  3  5  0  5
            H:   6  4  7  5  0
mask(DCBA)  set     end=A    end=B    end=C    end=D     (cost/prev, - = j not in mask)
  0001   A         6/H      -        -        -    
  0010   B         -        4/H      -        -    
  0011   AB        6/B      8/A      -        -    
  0100   C         -        -        7/H      -    
  0101   AC       11/C      -       10/A      -    
  0110   BC        -       10/C      7/B      -    
  0111   ABC      11/C     13/A     10/A      -    
  1000   D         -        -        -        5/H  
  1001   AD        6/D      -        -        7/A  
  1010   BD        -        8/D      -        7/B  
  1011   ABD       8/D      8/A      -        7/A  
  1100   CD        -        -       10/D     12/C  
  1101   ACD      13/D      -       10/A     12/A  
  1110   BCD       -       13/C     11/B     12/C  
  1111   ABCD     13/D     13/C     11/B     12/A  
transitions examined: 48 (= n(n-1)2^(n-2) = 48)
```

읽는 법:

- **open 답** = 마지막 행의 최소 = 11 (끝 C). 역추적: (1111, C) 의 앞 = B → (1011, B) 의 앞 = A → (1001, A) 의 앞 = D → (1000, D) 의 앞 = H. 순서 **H D A B C** = 5 + 1 + 2 + 3.
- **closed 답** = 마지막 행 + 귀환: A 13+6 = 19, B 13+4 = **17**, C 11+7 = 18, D 12+5 = **17**. 동점이면 작은 번호(B) → (1111, B) 앞 C → (1101, C) 앞 A → (1001, A) 앞 D: **H D A C B H**. 반대 방향 H B C A D H 도 17 — 대칭 비용의 닫힌 경로는 항상 짝이 있다.
- open 최적(끝 C)과 closed 최적(끝 B 또는 D)은 **순서가 다르다**. "closed 를 풀고 마지막 귀환 간선만 빼면 open" 은 틀린 생각이다 — open 답은 closed 에서 가장 비싼 간선을 빼는 게 아니라 끝점까지 다시 고른다.
- 탐욕(NN)은 홈에서 가장 가까운 B(4)로 먼저 가서 H B A D C = 4 + 2 + 1 + 5 = 12. 첫 걸음의 1 을 아끼려다 마지막에 D→C 5 를 낸다.

### Part A — 복잡도: 정확한 전이 수와 바이트

**전이 수.** 크기 k 인 S 에서 칸 하나(j) 당 후보 i 가 k − 1 개 → 전체 Σ_k C(n,k)·k·(k−1). 이것은 "서로 다른 순서쌍 (i, j) 를 고르고(n(n−1)), 나머지 n − 2 개 점을 S 에 넣을지 말지(2^(n−2))" 와 일대일이므로 **n(n−1)·2^(n−2)**. n = 16 이면 16·15·2^14 = 3 932 160. 하네스가 1830 사례 모두에서 `g_trans` 를 이 식과 대조한다(제약 없는 경우). 순열은 16! ≈ 2.1·10^13.

**메모리.** `demo_bench.c` 가 찍은 표 (실제 출력):

```text
memory (bytes): n  states=2^n*n  dp uint32  par uint8  total     folded uint16 (n*2^(n-1)*2, no par)
               10       10240      40960      10240      51200      10240
               12       49152     196608      49152     245760      49152
               14      229376     917504     229376    1146880     229376
               16     1048576    4194304    1048576    5242880    1048576
```

n = 16: dp 4 MiB + par 1 MiB = **5 MiB**. 테스터 PC 에선 아무것도 아니지만, 192 KB SRAM 급 Cortex-M4(예: STM32F407 — 데이터시트 수치 확인 권장)에는 n = 12(240 KiB)도 안 들어간다. 줄이는 방법 세 가지:

1. **엔트리 폭**: 비용 상한 = 간선 수 × 최대 간선. open 은 간선 n 개, closed 는 n + 1 개. 이 보드는 10 µm 단위 최대 간선 9950 → open 상한 16 × 9950 = 159 200 > 65 535 → **uint32 필요**. 단위를 0.1 mm 로 거칠게 하면 최대 간선 ≈ 1000 → 16 000 → uint16 으로 충분 (반올림 오차는 간선당 ±0.05 mm, 17 간선이면 최대 ±0.85 mm — 최적 순서가 바뀔 수 있음을 감수).
2. **접기**: j ∈ S 인 칸만 의미가 있다(나머지 절반은 INF). j 비트를 빼서 (n−1) 비트 수로 접으면 칸 수 n·2^(n−1). (연습 1)
3. **부모 표 없애기**: 역추적 때 `dp[S][j] == dp[S\{j}][i] + d[i][j]` 인 i 를 다시 찾는다 — O(n²) 추가 시간. 1 + 2 + 3 → n = 16 이 **1 MiB**, n = 12 가 48 KiB (연습 1 실측: 80개 보드에서 uint32 DP 와 같은 답).

popcount 층별로 두 층만 유지하는 방법도 있지만 가장 큰 층이 C(16,8)·16 = 205 920 칸이라 이득이 작고, 순위(rank) 인덱싱이 필요하며, 역추적용 정보를 따로 남겨야 해서 권하지 않는다.

### Part B — open, closed, 경계

- n = 0: 찍을 점이 없으면 프로브는 안 움직인다 → open = closed = 0. (기준 브루트포스도 n = 0 에서 d[H][H] 를 더하지 않게 맞췄다. 처음 버전은 이를 더했는데, d[H][H] = 0 이 아닌 거리 함수 변형(7. 실수 4)에서 둘이 어긋나는 것을 보고 고쳤다 — 기준도 정의를 따라야 한다.)
- n = 1: open = d[H][0], closed = d[H][0] + d[0][H]. 하네스는 비대칭(7, 11)으로 확인한다.
- **부등식** (200개 보드에서 확인): closed = min_j (dp[full][j] + d[j][H]) ≥ min_j dp[full][j] + min_j d[j][H] = open + min_j d[j][H], 그리고 open 의 끝점 ℓ 에 대해 closed ≤ dp[full][ℓ] + d[ℓ][H] = open + d[ℓ][H].
- **동점**: 엄격한 `<` 와 오름차순 탐색 → 가장 작은 i, 가장 작은 last. 출력이 결정적이라 생산 라인 레시피가 빌드마다 안 바뀐다 (같은 입력 → 같은 순서).

### Part B — 선행 제약은 상태를 늘리지 않는다

전원 인가 후 레일 전압을 먼저 확인하고 신호를 찍는다, 접지 기준점을 먼저 잡는다 같은 **부분 순서**가 흔하다. `pre[j]` = j 보다 먼저 와야 하는 점들의 마스크.

**주장**: 상태 (S, j) 로 충분하다. 증명: j 를 S\{j} 다음에 붙일 수 있는지는 "pre[j] ⊆ S\{j}" 로만 결정되고 이것은 집합만의 성질이다. 앞으로 남은 점들의 가능 여부도 각각 "pre[k] ⊆ (그때까지의 집합)" 이므로 지금까지의 **순서**와 무관하다. 그러니 같은 (S, j) 에 도달한 두 가능 경로는 미래가 같고, 싼 쪽만 남기면 된다 — Part A 의 잘라 붙이기가 그대로 성립한다. 구현은 전이 루프 한 줄:

```c
if ((uint32_t)t->pre[j] & ~pm) continue;   /* 선행 점이 아직 다 안 찍힘 → INF 그대로 */
```

기저도 `pre[j] == 0` 일 때만. 순환 제약(P0 은 P2 다음, P2 는 P0 다음)이면 full 행이 전부 INF → COST_INF. 무작위 120개(제약 2개씩) vs 브루트포스: 불일치 0, 불가능 16 사례까지 일치 (실제 출력). 보드에서 "레일 4개 먼저" 는 비용이 **253.50 → 307.00 mm (+53.50, +21%)** — 이 숫자가 공정 엔지니어와 협상할 근거다 (레일이 보드 곳곳에 흩어져 있어서 비싸다).

반대로 **시간 창**(time window: "j 는 전원 인가 후 50 ms 안에") 은 상태에 시간을 넣어야 해서 이 DP 가 그대로는 안 된다(Q2).

### Part B — 막힌 이동, 포화 덧셈, 정수 폭

`COST_INF = UINT32_MAX` 를 "금지" 로 쓰는 순간 덧셈이 위험하다: INF + 5 는 uint32 에서 4 로 감긴다(부호 없는 정수는 감기는 게 정의된 동작 — UB 는 아니지만 **조용히 틀린다**). 그래서

```c
static inline cost_t cadd(cost_t a, cost_t b) { return (a >= COST_INF - b) ? COST_INF : a + b; }
```

b = INF 면 `COST_INF - b = 0` 이라 항상 INF, 합이 INF 이상이어도 INF. 대가: 참값이 정확히 2^32 − 1 이상인 경로는 "불가능" 과 구별되지 않는다 → API 계약으로 "최적값이 2^32 − 1 미만일 때만 유효" 라고 적고, 하네스가 간선 셋이 각 2^31 인 n = 2 문제(참값 2^32)에서 INF 가 나오는지 확인한다. 기준 브루트포스는 **uint64** 로 더하고 INF 간선은 가지를 버리는, 포화와 독립인 구현이다 — 20% 금지 + 비용 < 2^27 인 240 사례에서 불일치 0, 불가능 7 (실제 출력).

부호 있는 `int` 로 하면? 오버플로가 **UB** (C11 6.5p5) 라 컴파일러가 "a + b < a 는 거짓" 이라 가정할 수 있다. 펌웨어 코드에서 비용은 부호 없는 형 + 명시적 포화가 정석이다.

### Part C — 어떤 거리인가: XY 갠트리는 체비쇼프

| 거리 | 식 | 맞는 기계 | 성질 |
|---|---|---|---|
| 맨해튼 | \|dx\| + \|dy\| | 축을 **하나씩** 움직이는 장비 (드라이버 채널 하나), 배선 길이 | 메트릭 |
| 체비쇼프 | max(\|dx\|, \|dy\|) | X·Y 모터가 **동시에**, 같은 최고 속도로 독립 이동 → 시간 = 긴 축 / v | 메트릭 |
| 유클리드 | √(dx² + dy²) | 직선 보간(G1 같은 좌표 보간) + 경로 속도 제한 | 메트릭 — 단, **반올림하면 아님** |

이 레슨의 갠트리는 축마다 독립 드라이버가 있어 동시에 움직인다고 가정 → **체비쇼프 = 이동 시간**. 축 속도가 다르면 max(\|dx\|/vx, \|dy\|/vy) 로 축척만 바꾸면 된다. 가감속(사다리꼴 속도 프로파일)을 넣으면 한 축의 이동 시간 t(d) 는 짧은 거리에서 2√(d/a), 긴 거리에서 d/v + v/a 이고, 도함수가 1/√(ad) 에서 1/v 로 줄어드는 연속 **오목** 함수다. t(0) = 0 인 단조 오목 함수는 열가법적(t(x + y) ≤ t(x) + t(y))이라 max(t(\|dx\|), t(\|dy\|)) 도 여전히 메트릭이다 — 하지만 DP 는 애초에 메트릭이 필요 없으니 측정한 이동 시간 표를 그대로 넣어도 된다. (실제 장비의 저크 제한, 정착 시간 등은 확인 안 함.)

**반올림 유클리드를 정수로.** r = ⌊√s⌋ (주어진 비트 단위 `isqrt_u64`, M15 참고). (r + ½)² = r² + r + ¼ 이고 s 는 정수이므로 √s ≥ r + ½ ⇔ s ≥ r² + r + 1 ⇔ s − r² > r. 정확히 반(.5)인 경우는 생기지 않는다((r + ½)² 가 정수가 아니므로).

```c
uint64_t s = dx * dx + dy * dy, r = isqrt_u64(s);
return (cost_t)(r + (s - r * r > r));
```

검사 기준은 `floor(sqrt(double) + 0.5)` 20만 쌍(좌표 ±2^20). 이 기준이 정확한 이유: s < 2^43 은 double 로 정확하고 IEEE 754 sqrt 는 올바르게 반올림되므로 오차 ≤ ½ ulp(√s) ≈ 2^−32. 반면 √s 와 가장 가까운 반정수 사이 거리는 \|s − (r+½)²\| / (√s + r + ½) ≥ ¼ / (2√s + 1) ≈ 4·10^−8 — 오차보다 100배 이상 크다. 그래서 double 기준도 틀릴 수 없다.

**반올림하면 메트릭이 아니다.** d((0,0),(2,2)) = round(2.83) = 3 > d((0,0),(1,1)) + d((1,1),(2,2)) = 1 + 1. 하네스에 그대로 있는 검사다. DP 의 최적성은 "주어진 행렬에 대해" 이므로 영향이 없지만, (a) 삼각 부등식에 기대는 휴리스틱 보장(Christofides 1.5배 등)이 깨지고 (b) "가는 길에 다른 점을 지나가면 손해가 없다" 같은 추론이 틀린다. 해법은 단위를 잘게(10 µm) 해서 반올림 오차를 이동 거리에 비해 무시할 만하게 만드는 것.

### Part C — 16점 보드의 결과

`solution.c` 의 실제 출력:

```text
board 16 TP chebyshev: open optimal 253.50 mm, NN 310.50 mm, NN+2opt 263.50 mm
  optimal open: H SWDIO SWCLK I2C_SDA CAN_H 1V8 VBAT ADC_IN 3V3 UART_RX GND XTAL UART_TX nRST CAN_L VCORE PWM_A
  closed (return home) optimal 296.00 mm
  rails-first open optimal 307.00 mm (+53.50 mm for the constraint)
  rails-first: H VBAT 1V8 VCORE 3V3 UART_RX ADC_IN SWDIO SWCLK I2C_SDA CAN_H PWM_A CAN_L nRST GND XTAL UART_TX
  order optimized for \ evaluated under:  euclid  manhattan  chebyshev  (mm)
  euclid         288.07     376.50     254.50
  manhattan      291.36     372.00     260.00
  chebyshev      294.17     393.00     253.50
```

- 3×3 표의 대각선이 각 열의 최소 — 다른 거리로 최적화한 순서는 그 거리에서 더 나쁠 수밖에 없다(하네스 검사). 체비쇼프 갠트리에 맨해튼으로 계획한 순서를 쓰면 260.00 mm(+2.6%), 유클리드로 계획하면 254.50 mm(+0.4%). 세 순서가 모두 다르다 — **거리 정의는 답을 바꾼다**.
- 이 보드에서 NN 은 22% 나쁘고(310.50), 2-opt 를 거쳐도 +3.9%(263.50) 에서 멈춘다. 그림의 점선: UART_RX 까지 같고 그 뒤 꼬리가 다르다. 최적 꼬리로 가려면 점 하나를 다른 자리로 옮기는(or-opt) 이동이 필요한데 2-opt 의 구간 뒤집기로는 한 번에 안 된다.
- n = 16 은 브루트포스로 확인할 수 없다. 대신 (1) DP 순서가 순열이고 그 비용이 반환값과 같다 (2) 어떤 구간 뒤집기로도 줄지 않는다(최적의 필요조건) (3) 연습 1 의 **독립 구현**(uint16 · 접힌 인덱스 · 부모 표 없음)이 n = 1..16 무작위 보드 80장(0.1 mm 단위)에서 uint32 DP 와 같은 값을 낸다 — 세 가지로 대신한다.
- 이동 시간: 갠트리 최고 속도를 예컨대 500 mm/s 로 잡으면 253.50 mm ≈ 0.51 s 대 NN 0.62 s — 보드당 0.1 초, 하루 1만 장이면 약 19 분. (가감속을 무시한 대략치 — 실제 장비 값으로 다시 계산할 것.)

### Part C — 휴리스틱은 얼마나 나쁜가

**최근접 이웃**: 홈에서 시작해 안 찍은 점 중 가장 가까운 곳으로. O(n²). **2-opt**: `order[i..j]` 를 뒤집어 비용이 엄격히 줄면 적용, 더 이상 없을 때까지. 대칭 비용에서 바뀌는 간선은 둘:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a03b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">2-opt: order[i..j] 뒤집기 = 간선 (a,b),(c,e) 를 (a,c),(b,e) 로 바꾸기</text>
  <g stroke="currentColor">
    <circle cx="40" cy="80" r="14" class="f-acc"/><circle cx="130" cy="80" r="14" class="f-warn"/>
    <circle cx="220" cy="80" r="14" class="f-mut"/><circle cx="310" cy="80" r="14" class="f-warn"/><circle cx="400" cy="80" r="14" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="40" y="85">a</text><text x="130" y="85">b</text><text x="220" y="85">…</text><text x="310" y="85">c</text><text x="400" y="85">e</text></g>
  <g stroke="currentColor" fill="none">
    <path d="M54 80 L114 80" stroke-width="2.5" marker-end="url(#a03b)"/><path d="M144 80 L204 80" marker-end="url(#a03b)"/>
    <path d="M234 80 L294 80" marker-end="url(#a03b)"/><path d="M324 80 L384 80" stroke-width="2.5" marker-end="url(#a03b)"/>
  </g>
  <text x="40" y="120" fill="currentColor">전: 굵은 두 간선 d[a][b] + d[c][e]</text>
  <g stroke="currentColor">
    <circle cx="40" cy="170" r="14" class="f-acc"/><circle cx="130" cy="170" r="14" class="f-warn"/>
    <circle cx="220" cy="170" r="14" class="f-mut"/><circle cx="310" cy="170" r="14" class="f-warn"/><circle cx="400" cy="170" r="14" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="40" y="175">a</text><text x="130" y="175">c</text><text x="220" y="175">…</text><text x="310" y="175">b</text><text x="400" y="175">e</text></g>
  <g stroke="currentColor" fill="none">
    <path d="M54 170 L114 170" stroke-width="2.5" marker-end="url(#a03b)"/><path d="M144 170 L204 170" marker-end="url(#a03b)"/>
    <path d="M234 170 L294 170" marker-end="url(#a03b)"/><path d="M324 170 L384 170" stroke-width="2.5" marker-end="url(#a03b)"/>
  </g>
  <text x="40" y="210" fill="currentColor">후: d[a][c] + d[b][e]. 안쪽은 방향만 반대 (대칭이면 비용 같음)</text>
  <text x="450" y="70" fill="currentColor">a = i==0 ? H : order[i−1]</text>
  <text x="450" y="92" fill="currentColor">e = j==n−1 ? (closed ? H : 없음)</text>
  <text x="450" y="110" fill="currentColor">        : order[j+1]</text>
  <text x="450" y="140" fill="currentColor">open 의 끝 구간을 뒤집을 땐</text>
  <text x="450" y="158" fill="currentColor">e 가 없어 d[a][c] 와 d[a][b] 만 비교</text>
  <text x="450" y="188" fill="currentColor">비대칭 비용이면 안쪽 간선도 바뀌어</text>
  <text x="450" y="206" fill="currentColor">이 O(1) 비교가 틀린다</text>
</svg>
```

끝난다는 증명: 적용할 때마다 정수 비용이 엄격히 줄고 비용은 0 이상이므로 유한 번. 하지만 몇 번인지는 입력에 따라 다르다 — **실행 시간이 데이터 의존**이라 DP(항상 n(n−1)2^(n−2) 전이)와 대조된다.

`demo_gap.c` 실제 출력 (보드 100 × 80 mm 무작위, 홈 (−5, −5) mm):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_gap.c -o demo_gap -lm && ./demo_gap
```

| 거리 | 종류 | n | 보드 | NN 평균 · 중앙 · p95 · 최대 격차 | NN 이 최적 | NN+2opt 평균 · 중앙 · p95 · 최대 | 2opt 가 최적 |
|---|---|---|---|---|---|---|---|
| 체비쇼프 | open | 8 | 2000 | 6.73 · 4.54 · 21.51 · 36.04 % | 30.8 % | 1.70 · 0.00 · 9.77 · 26.31 % | 68.0 % |
| 체비쇼프 | open | 12 | 1000 | 9.91 · 8.62 · 25.29 · 39.17 % | 13.0 % | 2.53 · 0.24 · 10.10 · 24.96 % | 47.5 % |
| 체비쇼프 | open | 16 | 200 | 12.30 · 12.96 · 26.57 · 34.91 % | 3.0 % | 3.43 · 2.12 · 11.83 · 19.41 % | 32.0 % |
| 체비쇼프 | closed | 16 | 200 | 16.45 · 15.16 · 30.91 · 42.66 % | 0.0 % | 1.70 · 0.13 · 7.26 · 11.55 % | 48.5 % |
| 유클리드 | closed | 16 | 200 | 14.55 · 12.70 · 32.35 · 38.02 % | 1.5 % | 2.29 · 0.87 · 9.14 · 19.91 % | 40.0 % |

읽을 것:

- NN 은 n 이 커질수록 나빠진다(평균 6.7 → 12.3%). 메트릭 TSP 에서 NN 의 최악 비율은 O(log n) 으로 커질 수 있다고 알려져 있다(Rosenkrantz·Stearns·Lewis 1977 — 확인 권장).
- 2-opt 는 평균 2~3% 지만 **꼬리가 길다**: p95 ≈ 10%, 최대 20~26%. 생산 레시피처럼 "한 번 계산해서 수천 번 쓰는" 경우 n ≤ 16 이면 DP 의 19 ms 를 아낄 이유가 없다.
- open 이 closed 보다 2-opt 에 불리하다(16점: 3.43% vs 1.70%). open 경로의 시작점은 홈에 묶여 있고 끝은 자유라서 구간 뒤집기만으로 바꿀 수 있는 구조가 적다(관찰 — 일반적 성질로 증명한 것은 아님).

### 측정 — `demo_bench.c` (시간)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm && ./demo_bench
```

Apple M6, Apple clang 21.0.0 (clang-2100.0.123.102), `-O2`, warm-up 1회 + 7회 중앙값. 측정 직전 `uptime` load averages **63.59 67.19 39.49** — 다른 작성 에이전트 10개가 같이 돌고 있어 부하가 매우 높다. 절대값보다 **비율**을 볼 것. 실제 출력:

```text
Held-Karp (open, chebyshev): n  transitions   median ms  [min, max]   ns/transition  ratio to n-1
                                8        3584      0.016  [0.015, 0.028]      4.46  -
                                9        9216      0.067  [0.060, 0.103]      7.27  x4.19
                               10       23040      0.168  [0.149, 0.224]      7.29  x2.51
                               11       56320      0.428  [0.397, 0.495]      7.60  x2.55
                               12      135168      0.974  [0.682, 1.076]      7.21  x2.28
                               13      319488      1.515  [1.386, 7.080]      4.74  x1.56
                               14      745472      3.681  [3.346, 6.773]      4.94  x2.43
                               15     1720320      8.938  [8.029, 13.274]      5.20  x2.43
                               16     3932160     18.816  [17.776, 25.793]      4.79  x2.11

brute force (all n! orders): n  n!          median ms   DP ms     same cost
                                 8       40320        0.34     0.025   yes
                                 9      362880        5.85     0.082   yes
                                10     3628800       47.06     0.138   yes
                                11    39916800      624.20     0.305   yes
                                12   479001600     8706.13     0.989   yes
```

- 이론 비율: T(n)/T(n−1) = n(n−1)2^(n−2) / ((n−1)(n−2)2^(n−3)) = **2n/(n−2)** → n = 12 에서 2.4, n = 16 에서 2.29. 측정 2.1~2.6 (n = 9, 13 은 부하로 튄 값).
- 전이 하나 ≈ 5~7 ns (포화 덧셈 + 비교 + 4 MiB 표의 행 하나 읽기).
- 브루트포스는 n 마다 약 n 배: n = 11 → 12 에서 ×13.9. n = 12 에서 DP 1 ms 대 8.7 초. 둘의 답은 항상 같았다.
- MCU 에선 실측하지 않았다. 전이 수가 데이터와 무관하므로 "전이당 사이클 × 3 932 160" 으로 WCET 를 잡을 수 있다는 점이 핵심이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution    # 34/34 passed, 약 0.3 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san -lm && ./solution_san   # 34/34, 보고 없음, 약 0.6 초
```

정적 표 5 MiB 는 BSS 라 실행 파일 크기에 안 들어가고 스택도 안 쓴다(펌웨어라면 링커 스크립트의 `.bss` 가 RAM 에 들어가는지부터 확인 — 여기선 맥이라 문제없음). 재귀는 테스트 쪽 브루트포스(깊이 ≤ n)뿐이다.

## 7. 자주 하는 실수

아래 1~6 은 `solution.c` 의 **사본**에 한 줄씩 넣어 하네스를 실제로 돌린 결과다 (레슨 폴더에는 남기지 않음).

**1. 기저를 0 으로 둔다.** `dp[{j}][j] = 0` — 홈에서 첫 점까지의 이동을 빠뜨려 "시작점 자유" 문제가 된다. 실제: **15/34**, 첫 실패는 손 계산 `open == 11`. → `dp[{j}][j] = d[H][j]`.

**2. 역추적에서 비트를 먼저 지우고 부모를 읽는다.**

```c
m ^= 1u << j;
unsigned p = g_par[m][j];      /* 틀림: j 가 없는 집합의 칸 = 의미 없는 값 */
```

실제: **14/34** — 무작위 1830 사례 중 1590 에서 `hk_backtrack` 의 표 검사("j 가 mask 에 있나")에 걸려 −1, `tsp_solve` 가 INF 를 돌려줘 비용 불일치로 잡혔다. 그 검사가 없었다면 비용(표에서 바로 나옴)은 맞고 **순서만** 틀린 답이 조용히 나갔을 것이다 → 하네스가 "순서가 순열인가 + 순서의 비용 = 반환값" 을 따로 검사하는 이유. 고침: 읽고 나서 지운다.

**3. closed 답에서 귀환 간선을 빼먹는다.** `c = dp[full][j]` 를 closed 에도 쓰면 open 답이 나온다. 실제: **23/34** (closed 사례 795개 틀림). → `cadd(dp[full][j], d[j][H])`, 그리고 끝점을 **다시 고른다**(Part A 추적 표: open 끝 C, closed 끝 B).

**4. 반올림 조건을 `>=` 로.** `r + (s − r² >= r)` 는 s = r² + r 에서 올림 — √(r² + r) < r + ½ 이므로 틀리고, s = 0 에서도 1 을 돌려줘 d[i][i] = 1 이 된다. 실제: **32/34** (손 계산 거리 검사, 20만 쌍 무작위 검사). → `>`.

**5. 2-opt 에서 open 경로의 끝을 closed 처럼 다룬다.** `e = (j == n−1) ? H : order[j+1]` 로 쓰면 존재하지 않는 귀환 간선 d[c][H] 를 비교에 넣어, 실제로는 손해인 뒤집기를 하고 이득인 것을 놓친다. 실제: **33/34** — 300장 n = 10 에서 2-opt 평균 격차 2.26% → **10.48%**, 최대 25.5% → 44.7%, 그리고 "2-opt 결과는 국소 최적" 검사가 실패. 휴리스틱 버그는 "답이 여전히 유효한 순서" 라서 비용 비교 없이는 안 보인다.

**6. 포화 없는 덧셈.** `a + b` 를 그대로 쓰면 INF + 비용이 작은 수로 감겨 **금지된 이동을 지나는 경로가 최적으로 뽑힌다**. 실제: **27/34** — 선행 제약 120개 중 212 불일치(제약 위반 상태의 INF 가 감겨서), 막힌 이동 240개 중 184 불일치, 순환 제약이 "가능" 으로 나옴. → `cadd`.

**7. 선행 검사에서 `~pm` 대신 `~m`.** 이것도 넣어 봤는데 **34/34 — 잡히지 않았다.** 이유: m 과 pm 은 j 비트만 다르고 `pre[j]` 에는 j 자신이 없으니 결과가 항상 같다(동치 변이). 차이가 나는 건 `pre[j]` 에 j 가 들어간 잘못된 입력뿐 — 그때 `~m` 은 "자기 자신이 먼저" 를 통과시킨다. 의도를 드러내는 `~pm`(= "j 앞의 집합") 을 쓰고, 입력 검증에서 `pre[j] & (1u << j)` 를 거부하는 게 깔끔하다.

**8. 루프를 j 바깥, mask 안쪽으로 돈다.** `for j { for mask {...} }` 이면 dp[pm][i] (i > j) 가 아직 안 채워졌을 수 있다. "모든 칸의 의존이 먼저 계산됐나" 를 항상 확인 — 여기선 mask 오름차순 하나로 충분하다 (실행해 보지 않은 추론).

**9. 엔트리 폭을 감으로 정한다.** "거리니까 uint16" 이면 이 보드(10 µm 단위, open 상한 159 200)에서 감긴다. 상한 = 간선 수 × 최대 간선을 계산해서 고르고, 연습 1 처럼 함수 입구에서 상한을 확인해 넘으면 거절한다.

## 8. 면접 Q&A

<details><summary>Q1. 그냥 순열을 다 보면 안 되나요? 언제부터 DP 가 이기나요?</summary>

n! 대 n(n−1)2^(n−2). n = 8 에서 이미 40320 대 3584 이고, 실측 n = 12 에서 브루트포스 8.7 초 대 DP 1 ms (이 맥, 부하 높음). 다만 브루트포스는 메모리가 O(n) 이고, **가지치기**(지금까지 비용 ≥ 현재 최선이면 중단, 하한으로 남은 점 최소 간선 합)를 넣은 분기 한정(branch and bound)은 실제 보드에서 훨씬 빠를 수 있다 — 하지만 최악은 여전히 지수이고 데이터에 따라 들쭉날쭉하다. 꼬리: "n = 25 면?" → 2^25·25 칸 × 4 B = 3.4 GB, 전이 25·24·2^23 ≈ 5·10^9 — PC 에서도 무겁다. 그때는 2-opt / or-opt / Lin–Kernighan 계열 휴리스틱 + 하한(1-tree, Held–Karp 하한)으로 격차를 보고하거나 ILP 솔버. 일반 가중치 TSP 에서 Held–Karp 보다 지수의 밑이 작은 정확 알고리즘은 알려져 있지 않다고 알고 있다(확인 권장).

</details>

<details><summary>Q2. 최적 부분 구조를 증명해 보세요. 이 DP 가 깨지는 경우는?</summary>

잘라 붙이기: (S, j) 의 최적 경로에서 마지막 간선 (i, j) 를 떼면 앞부분은 (S\{j}, i) 의 경로이고, 그보다 싼 경로가 있으면 바꿔 끼워 더 싼 (S, j) 경로가 되니 모순 (5. 풀이). 쓰는 가정은 "비용 = 간선 비용의 합, 간선 비용은 과거와 무관" 하나. 깨지는 경우: (1) **이동 비용이 이전 방향에 의존** — 프로브 헤드 회전(θ 축), 백래시가 직전 이동 방향에 따라 다름 → 상태에 (직전 점) 또는 (방향)을 추가: dp[S][prev][last], n 배 커짐. (2) **시간 창** — "레일은 전원 인가 후 50 ms 안에" → 도착 시각이 상태에 필요(값 범위가 크면 지수 + 의사다항). (3) **경로 의존 제약**(같은 프로브로 연속 측정 금지 등)은 상태에 그 정보를 넣으면 다시 성립. 선행 제약은 집합만의 성질이라 상태를 안 늘린다(Part B 증명).

</details>

<details><summary>Q3. n = 16 이면 메모리가 얼마고, MCU 에서 돌릴 수 있나요?</summary>

2^16·16 칸. uint32 dp 4 MiB + uint8 부모 1 MiB = 5 MiB (demo_bench 표). 192 KB SRAM MCU 에는 n = 12(240 KiB)도 안 들어간다. 줄이기: (1) j ∈ S 칸만 남기는 접기로 절반, (2) 비용 상한이 허락하면 uint16, (3) 부모 표 대신 역추적 때 등식을 다시 확인 → n = 16 이 1 MiB, n = 12 가 48 KiB (연습 1, 같은 답 확인). 더 줄여야 하면 MCU 에선 NN + 2-opt (메모리 O(n), 평균 2~3%, 최악 20%대). 현실적 답: 순서는 **테스터 PC 나 빌드 시점**에 계산해 레시피(테이블)로 내려보내고, MCU 는 그 순서를 실행만 한다. 꼬리: "꼭 MCU 에서 계산해야 한다면?" → 실행 시간이 데이터 무관(전이 수 고정)이라 WCET 를 정확히 잡을 수 있고, mask 범위를 나눠 저우선순위 태스크에서 조금씩(코루틴처럼) 진행하면 다른 태스크를 막지 않는다. ISR 에서 할 일은 절대 아니다.

</details>

<details><summary>Q4. 홈으로 돌아와야 하나요? open 과 closed 는 어떻게 바꿔 끼우나요?</summary>

이 레슨의 과제는 open(홈 출발, 마지막 TP 에서 끝): 다음 명령이 거기서 시작하니까. 보드 반출 전 프로브를 치워야 하는 장비면 closed. 같은 표에서 답 고르는 식만 다르다: open = min dp[full][j], closed = min (dp[full][j] + d[j][H]). 변환: open = "귀환 간선이 0 인 closed", 시작·끝 모두 자유 = "가상 홈과 0 간선" (연습 2). closed 의 최적 순서에서 마지막 귀환만 빼면 open 최적이 **아니다** — 손 계산 예에서 open 끝은 C, closed 끝은 B. 부등식 open + min_j d[j][H] ≤ closed ≤ open + d[ℓ][H] 로 서로를 확인할 수 있다.

</details>

<details><summary>Q5. 거리는 뭘 쓰나요? 체비쇼프를 고른 이유는?</summary>

비용은 거리가 아니라 **이동 시간**이다. X·Y 축이 독립 드라이버로 동시에 같은 최고 속도로 움직이면 시간 = max(\|dx\|, \|dy\|)/v = 체비쇼프. 축을 하나씩 움직이면 맨해튼, 직선 보간 + 경로 속도 제한이면 유클리드. 이 보드에서 거리 정의에 따라 최적 순서 셋이 모두 달랐고, 맨해튼으로 계획한 순서를 체비쇼프 갠트리에서 쓰면 +2.6% (실측 3×3 표). 가감속은 축별 시간을 오목 함수로 바꿀 뿐이라 max 는 여전히 메트릭이고, DP 는 메트릭이 아니어도 되니 측정한 이동 시간 표를 그대로 넣는 게 가장 정직하다. 꼬리: "유클리드를 정수로?" → round(√s) = r + (s − r² > r), r = ⌊√s⌋ — 그리고 반올림하면 삼각 부등식이 깨진다((0,0)-(1,1)-(2,2): 3 > 1 + 1).

</details>

<details><summary>Q6. dp 엔트리는 몇 비트여야 하나요? INF 는 어떻게 다루죠?</summary>

상한 = (간선 수) × (최대 간선): open n 개, closed n + 1 개. 10 µm 단위 100 × 80 mm 보드면 최대 간선 9950 → open 159 200 → uint32. 0.1 mm 단위면 16 000 → uint16 (대신 간선당 ±0.05 mm 반올림). INF 를 "금지" 로 쓰면 반드시 **포화 덧셈** — 부호 없는 감김은 UB 는 아니지만 조용히 금지 경로를 최적으로 만든다(실수 6 실측: 불일치 184/240). 부호 있는 int 는 오버플로가 UB 라 더 나쁘다. 계약: 최적값이 2^32 − 1 미만일 때만 유효, 넘으면 INF(오류) — 하네스가 참값 2^32 인 사례로 확인. 꼬리: "INF 를 UINT32_MAX/2 로 두고 그냥 더하면?" → 간선 두 개까지는 안전하지만 경로 길이만큼 더해지면 다시 위험. 포화가 확실하다.

</details>

<details><summary>Q7. 휴리스틱으로 충분하지 않나요? 2-opt 는 얼마나 나쁜가요?</summary>

측정(demo_gap): 체비쇼프 open n = 16, 200장에서 NN 평균 12.3% (최적인 경우 3%), NN + 2-opt 평균 3.4%, p95 11.8%, 최대 19.4% (최적 32%). closed 에선 2-opt 가 더 잘 맞는다(평균 1.7%). 평균은 괜찮지만 꼬리가 길다. 레시피를 한 번 계산해 수천 장에 쓰는 생산 테스트라면 n ≤ 16 에서 19 ms 짜리 정확해를 안 쓸 이유가 없다. n 이 크거나 매번 순서가 바뀌는(적응형 재측정) 경우엔 2-opt + or-opt(점 하나 옮기기) — 보드 예에서 2-opt 가 못 넘은 꼬리는 or-opt 이동이 필요한 모양이었다. 꼬리: "2-opt 는 비대칭에서?" → 뒤집힌 구간 안쪽 간선 비용이 바뀌므로 O(1) 차이 계산이 틀린다. 비대칭이면 or-opt 나 3-opt 의 방향 보존 이동(segment insertion)을 쓴다.

</details>

<details><summary>Q8. 이걸 어떻게 테스트했나요? n = 16 은 브루트포스가 안 되는데요.</summary>

(1) n ≤ 9 는 모든 순열과 대조 — 세 거리 × open/closed × 좌표 범위 둘(작은 격자는 동점·겹친 점을 일부러 만든다) = 1830 사례, 비대칭 행렬, 선행 제약, 막힌 이동, 큰 비용(uint64 기준). (2) 반환값뿐 아니라 **순서**를 검증: 순열인가, 제약을 지키나, 순서의 비용 = 반환값 — 실수 2 처럼 비용만 맞고 순서가 틀린 버그를 잡는다. (3) 구조적 불변식: 전이 수 = n(n−1)2^(n−2), open ≤ closed 부등식. (4) n = 16 은 독립 구현(연습 1)과 일치 + 2-opt 국소 최적(필요조건). (5) 변이 테스트: 버그 7개를 일부러 넣어 6개가 잡히고 1개는 동치 변이임을 확인(7. 실수). 꼬리: "테스트 기준(브루트포스)이 틀리면?" → 기준은 포화 대신 uint64, INF 간선은 가지 버리기처럼 **다른 방식**으로 짰고, n = 0 정의 불일치를 실제로 기준 쪽에서 발견해 고쳤다.

</details>

## 9. 추가 연습

### 연습 1 — 1 MiB 로: uint16 · 접힌 인덱스 · 부모 표 없음

(S, j) 에서 j ∈ S 인 칸만 저장하도록 j 비트를 빼서 (n−1) 비트로 접고, 비용을 uint16 으로, 부모 표 없이 역추적하라. 비용 상한이 uint16 을 넘으면 거절(−1). uint32 DP 와 같은 답인지 n = 1..16 무작위 보드에서 확인하라.

<details><summary>답 보기</summary>

접기: `fold(m, j) = ((m >> (j+1)) << j) | (m & ((1u << j) − 1))` — j 위쪽 비트를 한 칸 내린다. 칸 번호 `j·2^(n−1) + fold(m, j)` 는 (m, j∈m) 쌍과 0..n·2^(n−1)−1 사이의 전단사(n = 5 에서 80칸 전수 확인). 역추적: 현재 비용 cur = dp[S][j] 에 대해 `dp[S\{j}][i] + d[i][j] == cur` 인 가장 작은 i 를 찾는다 — 그런 i 는 반드시 있다(전이의 min 을 준 i). 상한 검사 `n·max_edge ≤ 0xFFFE` 로 감김이 원천적으로 없게 하면 포화 덧셈도 필요 없다. 실제 출력:

```text
ex1: 80 boards n=1..16, uint16 folded DP vs uint32 DP mismatches=0; bytes full=5242880 folded=1048576
```

5 MiB → 1 MiB. 10 µm 단위 보드는 상한을 넘어 거절되는 것도 확인한다. 대가: 역추적이 O(n²), 그리고 0.1 mm 단위의 반올림.

</details>

### 연습 2 — 시작점도 자유: 최단 해밀턴 경로

홈 없이, 아무 점에서 시작해 아무 점에서 끝나도 될 때 모든 점을 찍는 최소 비용 (예: 프로브가 이전 보드의 마지막 점 근처 어딘가에 있고 그 위치를 신경 쓰지 않을 때). 본문 코드를 고치지 말고 입력만 바꿔서 풀어라.

<details><summary>답 보기</summary>

가상 홈 H 를 두고 d[H][j] = d[j][H] = 0. open 최적 = 첫 간선이 공짜인 경로 = 시작 자유인 최단 해밀턴 경로. 60개 무작위(n ≤ 8)에서 모든 순열 브루트포스와 일치. 일직선 예: 점 x = 50, 10, 40, 20, 홈 x = 100 → 홈 출발 open = 90 (100→50→40→20→10), 자유 시작 = 40 (10→20→40→50). 실수 1(기저 0)이 정확히 이 문제를 푸는 셈이다 — 버그와 변형은 한 줄 차이.

</details>

### 연습 3 — 프로브 2개: 점을 나눠 맡는 makespan

플라잉 프로브 장비는 보통 프로브가 여러 개다. 프로브 A(홈 HA)와 B(홈 HB)가 점을 나눠 맡아 **동시에** 움직일 때, 둘 중 늦게 끝나는 시간(makespan)을 최소화하라. 두 프로브의 충돌은 무시.

<details><summary>답 보기</summary>

핵심 관찰: DP 를 한 번 채우면 **모든 부분집합** S 에 대해 best[S] = min_j dp[S][j] (홈에서 S 를 다 찍는 최소 open 비용) 가 공짜로 나온다. A 홈으로 한 번, B 홈으로 한 번 채우고 답 = min_S max(bestA[S], bestB[full ^ S]). 추가 비용 O(2^n). 브루트포스(순열 × 분할 위치 — 앞 k 개는 A, 나머지는 B 순서)와 60개(n ≤ 7) 일치, 그중 47개는 두 프로브가 실제로 일을 나눴다(실제 출력). 프로브가 셋 이상이면 S 를 여러 조각으로 나누는 부분집합 DP 가 되고 부분 마스크(submask) 열거 `sub = (sub − 1) & mask` 가 필요하다 — 그 비트 트릭과 Σ 3^n 비용은 ALGO-A07.

</details>

### 연습 4 — LeetCode 847 Shortest Path Visiting All Nodes

무가중 무향 연결 그래프에서 모든 노드를 방문하는 가장 짧은 걸음 수(시작·끝 자유, **노드·간선 재방문 허용**). 상태 (mask, node) BFS 로 풀고, "Floyd 로 최단 거리 행렬을 만든 뒤 시작 자유 TSP" 와 같은지 확인하라.

<details><summary>답 보기</summary>

모든 (1<<v, v) 를 거리 0 으로 큐에 넣고 층 단위 BFS — 간선 가중치가 1 이라 처음 mask == full 이 되는 층이 답. 상태 n·2^n, 시간 O(2^n·n²)(간선을 인접 마스크로 훑을 때). 재방문을 허용하면 "i 에서 j 로 가는 비용" 은 그래프의 **최단 거리**가 되므로 Floyd–Warshall(ALGO-A02) 로 메트릭 폐포를 만든 뒤 연습 2 의 시작 자유 Held–Karp 와 같아야 한다 — 무작위 연결 그래프 80개(n ≤ 10)에서 일치, LeetCode 예제 두 개(별 모양 → 4, 두 번째 예 → 4)도 확인. 보드에 비유하면: 프로브가 키 큰 부품 위로 못 지나가 **정해진 통로**만 다닐 때, 통로 그래프의 최단 거리(A* — ALGO-A01)로 비용 표를 만들고 TSP 를 푼다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 9/9 passed, 약 0.3 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san   # 9/9, 보고 없음
```

## 10. 핵심 정리

- 상태 dp[S][j] = "S 를 다 찍고 j 에서 끝나는 최소 비용". 기저 dp[{j}][j] = d[H][j], 전이 min_{i∈S\{j}} dp[S\{j}][i] + d[i][j], **mask 오름차순**이면 순서 문제 없음. 증명은 잘라 붙이기, 가정은 "간선 비용이 과거와 무관" 하나 (대칭·삼각 부등식 불필요).
- 전이 정확히 n(n−1)2^(n−2) (n = 16: 393만, 이 맥 ~19 ms), 칸 2^n·n (n = 16: dp 4 MiB + par 1 MiB). 실행 시간이 데이터 무관 → WCET 를 말할 수 있다. 브루트포스 n = 12 는 8.7 초.
- open(과제: 홈 출발, 끝 자유) vs closed(귀환): 같은 표, 답 고르는 식만 다르고 **끝점도 다르다**. open + min d[j][H] ≤ closed ≤ open + d[ℓ][H].
- 역추적: 부모를 **읽고 나서** 비트를 지운다. 테스트는 비용뿐 아니라 순서의 비용을 다시 계산해서.
- 선행 제약은 전이에서 pre[j] ⊆ S\{j} 만 검사 (상태 그대로). 시간 창·방향 의존 비용은 상태를 키워야 한다.
- INF + 포화 덧셈, 폭 = (간선 수 × 최대 간선) 로 고른다. MCU 용: 접기 + uint16 + 부모 표 없음 = 5 MiB → 1 MiB (n = 12 는 48 KiB).
- XY 동시 이동 갠트리 = 체비쇼프(시간 = 긴 축). 거리 정의가 최적 순서를 바꾼다(+2.6% 실측). 반올림 유클리드는 메트릭이 아니다.
- NN + 2-opt: n = 16 평균 3.4%, 최대 19% (open). 한 번 계산해 수천 번 쓰는 레시피면 n ≤ 16 은 DP.

## 참고자료

- R. Bellman, "Dynamic programming treatment of the travelling salesman problem", *Journal of the ACM* 9(1), 1962 (DOI 10.1145/321105.321111).
- M. Held, R. M. Karp, "A dynamic programming approach to sequencing problems", *Journal of the Society for Industrial and Applied Mathematics* 10(1):196–210, 1962 (DOI 10.1137/0110015 — 서지 레코드로 확인, 본문은 안 봄). 위키백과 "Held–Karp algorithm" 항목도 두 논문이 1962년 독립적으로 같은 방법을 냈다고 정리한다: <https://en.wikipedia.org/wiki/Held%E2%80%93Karp_algorithm>
- G. A. Croes, "A method for solving traveling-salesman problems", *Operations Research* 6(6), 1958 — 2-opt 의 출발점으로 흔히 인용 (서지는 기억에 의존, 확인 권장).
- D. J. Rosenkrantz, R. E. Stearns, P. M. Lewis, "An analysis of several heuristics for the traveling salesman problem", *SIAM Journal on Computing* 6(3), 1977 — 최근접 이웃의 로그 비율 (기억에 의존, 확인 권장).
- LeetCode 847 Shortest Path Visiting All Nodes (연습 4): <https://leetcode.com/problems/shortest-path-visiting-all-nodes/> — 문제 설명은 <https://walkccc.me/LeetCode/problems/847/> 에서 확인.
- ISO/IEC 9899:2011 (C11) 6.5p5 — 부호 있는 정수 오버플로는 정의되지 않은 동작; 6.2.5p9 — 부호 없는 연산은 감긴다.
- AlgoMonster 노트 M09 Dynamic Programming (Don 정리본) — 1D/2D/배낭 DP 의 상태 설계. 매핑표(2026-11-07_mapping_coding_pattern.md)에 따르면 비트마스크 DP 는 소스에 없는 확장이다.
- 관련 레슨: ALGO-M07 (1D DP, 상태·전이), ALGO-M08 (2D DP, 역추적, 배낭의 폭 축소), ALGO-A07 (부분 마스크 열거·Gosper — 연습 3 확장), ALGO-A02 (Floyd–Warshall — 연습 4 의 메트릭 폐포), ALGO-A01 (A* — 통로를 따라가는 실제 이동 비용), ALGO-M15 (정수 제곱근 `isqrt`), ALGO-B12/B13 (비트 연산 기초).
