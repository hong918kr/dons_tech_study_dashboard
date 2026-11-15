---
id: ALGO-A02
time: 120분
updated: 2026-11-11
---
# ALGO-A02 벨만-포드와 플로이드-워셜

## 학습 목표

- 벨만-포드(Bellman-Ford)를 **조기 종료**와 함께 구현하고, "n−1 패스면 충분하다"와 "n 번째 패스에서 값이 줄면 음수 사이클이 있다"를 양방향으로 증명한다. 간선 순서가 패스 수를 2 에서 n 까지 바꾸는 것을 직접 본다.
- 음수 사이클을 **검출만 하지 않고 꺼낸다**: n 번째 패스에서 완화된 정점에서 pred 를 n 번 거슬러 올라가 사이클 안에 들어간 뒤 한 바퀴 모은다. 왜 n 번인지(꼬리), 왜 그 사이클이 음수인지(pred 그래프 보조정리)를 증명한다.
- 환율 차익(arbitrage)을 −log(rate) 로 바꾸고, **double 로 하면 곱이 정확히 1 인 표에서 차익이 나오는** 것을 실제로 보인 뒤, 2^−32 고정소수점 정수 가중치(올림 + 1)로 "거짓 양성 0, 놓침은 ln 이득 ≤ 2L·2^−32 일 때만"을 증명·검사한다.
- 플로이드-워셜(Floyd–Warshall)로 지연 행렬과 `next[i][j]` 경로 복원 테이블을 만든다. k 가 왜 가장 바깥이어야 하는지, INF 가드·음수 쪽 포화(지수 폭주 대비), `d[i][i] < 0` 검출과 −∞ 쌍 표시를 증명하고, 다익스트라·벨만-포드를 모든 출발점에서 돌린 결과와 대조한다.
- n = 256 노드 행렬을 MCU 에 올릴 수 있는지 메모리·Cortex-M4 명령 수로 판단하고, 펌웨어에서는 "내 행 하나(다음 홉 n 바이트)"만 두는 이유를 말한다.

## 1. 문제

면접에서 "다익스트라는 음수 간선에서 왜 틀리죠? 그럼 뭘 쓰죠?" 다음 질문은 거의 항상 **"음수 사이클은 어떻게 찾고, 찾은 사이클은 어떻게 보여 주죠?"** 와 **"모든 쌍이 필요하면?"** 이다. 이 레슨은 그 두 꼬리질문의 답 — 벨만-포드와 플로이드-워셜 — 을 펌웨어 엔지니어가 신경 써야 할 부분(정수 폭, INF 연산, 고정 RAM, 결정적 최악 시간)까지 끝낸다.

M06 은 음 아닌 가중치에서 다익스트라를 끝냈고, 음수 간선 반례(4정점)에서 다익스트라가 조용히 틀리는 것을 보였다. 음수 가중치는 생각보다 자주 나온다.

| 어디서 | 간선 u→v 의 가중치 | 음수 사이클의 의미 |
|---|---|---|
| 환율·변환 비율 (Part B) | −ln(rate) | 한 바퀴 돌면 돈이 는다 = 차익 |
| 타이밍 제약 x_v − x_u ≤ c (연습 3) | c (음수 가능) | 제약끼리 모순 = 스케줄 불가능 |
| 에너지 소비 (내리막 회생 제동) | 소비 − 회생 | 영구 기관 = 모델 버그 |
| Johnson 재가중 (연습 1) | w + h(u) − h(v) 를 만들기 위한 전처리 | 있으면 Johnson 자체가 불가 |
| 거리 벡터 라우팅 (RIP 등) | 링크 비용 (양수) | — 분산 벨만-포드 자체 (Q7) |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">환율 → 그래프: 곱 &gt; 1 인 순환 ⇔ −ln 합 &lt; 0 인 사이클</text>
  <g stroke="currentColor" stroke-width="1.5">
    <circle cx="90" cy="90" r="24" class="f-acc"/><circle cx="300" cy="90" r="24" class="f-acc"/><circle cx="195" cy="240" r="24" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="90" y="95">EUR</text><text x="300" y="95">GBP</text><text x="195" y="245">USD</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.6" marker-end="url(#a02a)">
    <path d="M116 84 L272 84"/>
    <path d="M286 114 L210 218"/>
    <path d="M178 220 L104 114"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="130" y="74">0.85464567 (낡은 시세)</text>
    <text x="252" y="170">1.26873</text>
    <text x="60" y="170">0.925</text>
  </g>
  <rect x="380" y="44" width="350" height="106" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="392" y="66">곱 = 0.85464567 × 1.26873 × 0.925 ≈ 1.00300</text>
    <text x="392" y="88">−ln 합 = 0.1570 − 0.2380 + 0.0780 ≈ −0.0030 &lt; 0</text>
    <text x="392" y="110">→ 벨만-포드의 음수 사이클 = 0.3% 차익</text>
    <text x="392" y="132">(실제로 solution.c 는 더 짧은 GBP↔EUR 2-순환을 먼저 찾는다)</text>
  </g>
  <rect x="380" y="166" width="350" height="106" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="392" y="188">함정: 0.5 × 0.8 × 2.5 = 1 (정확히, 차익 없음)</text>
    <text x="392" y="210" font-family="monospace">-log(0.5)-log(0.8)-log(2.5) = -1.11e-16</text>
    <text x="392" y="232">double 은 "차익 있음" 이라고 답한다 (실제 실행)</text>
    <text x="392" y="254">→ 정수 고정소수점 + 오차 한계로 판정 (Part B)</text>
  </g>
</svg>
```

세 알고리즘의 자리:

| | 다익스트라 (M06) | 벨만-포드 (Part A) | 플로이드-워셜 (Part C) |
|---|---|---|---|
| 질문 | 한 출발점, w ≥ 0 | 한 출발점, 음수 허용 | 모든 쌍, 음수 허용 |
| 음수 사이클 | 모름 (조용히 틀림) | 검출 + 추출 | 검출 (d[i][i] &lt; 0) + −∞ 쌍 |
| 시간 | O((V+E) log V) | O(VE), 조기 종료 시 훨씬 적음 | Θ(V³) 항상 |
| 메모리 | O(V+E) | 간선 목록 + O(V) | O(V²) 행렬 2개 |

### Part A — 벨만-포드: 조기 종료, 음수 사이클 검출과 추출

- `bf_run(g, src, dist, pred_e, cyc_e, &cyc_len, &st)` — 간선 목록을 최대 n 번 훑는다. 변화 없는 패스가 나오면 즉시 `BF_OK`. n 번째 패스에서 완화가 일어나면 그 자리에서 멈추고 `BF_NEGCYCLE`. `src = BF_ALL` 이면 모든 dist 를 0 으로 시작(가상 출발점) — 어디에 있든 음수 사이클을 찾는다.
- `bf_extract_cycle(g, pred_e, v, cyc_e)` — pred 를 n 번 거슬러 올라가 사이클 안으로 들어간 뒤 한 바퀴를 간선 번호 열로(정방향) 꺼낸다.
- 테스트: 손 계산(5정점, 4 패스), 사슬 40정점(정방향 2 패스 / 역방향 40 패스), **꼬리 사례**(n 번째 패스에서 처음 완화되는 정점이 사이클 밖), 정점 1개·음수 자기 루프·0 가중치 사이클·평행 간선·안 닿는 음수 사이클, 무작위 3000개 그래프 × 모든 출발점을 단순 경로/단순 사이클 전수와 대조.

### Part B — 환율 차익: −log 와 부동소수점

- `rate_to_weight(q)` — 소수 8자리 정수 시세 q 를 `ceil(−ln(q/1e8)·2^32) + 1` 정수로.
- `arb_detect(n, rate, cyc, &len)` — 시세표 → 그래프 → `bf_run(BF_ALL)` → 통화 순환.
- 테스트: 손으로 만든 표 4개(차익 없음 / 낡은 시세 하나 / 곱이 정확히 1 / 1 ppm 차익), 무작위 4000개 표를 **큰 정수로 정확히 곱한** 전수 기준과 대조 — 거짓 양성 0, 한계 밖 놓침 0.

### Part C — 플로이드-워셜: 지연 행렬과 경로 복원 테이블

- `fw_run(n, d, nx)` — k 바깥 3중 루프, INF 피연산자 건너뛰기, `FW_NEG_INF` 포화, `nx[i][j] = nx[i][k]`, 끝나고 `d[k][k] < 0` 세기와 −∞ 쌍 표시.
- `fw_path(n, d, nx, i, j, path, max)` — 다음 홉을 따라 경로 복원.
- 시나리오: 기체 내부 통신망 8노드(fc, gw, radio, cam, imu, esc1, esc2, gps)의 한 방향 링크 지연(µs) → 지연 행렬, gw 의 라우팅 테이블, 링크 장애 후 재계산.
- 대조: 무작위 600개 그래프에서 FW vs 다익스트라 × 모든 출발점(w ≥ 0), FW vs 벨만-포드 × 모든 출발점(음수 간선, 음수 사이클 없음), 3000개 그래프에서 음수 사이클 검출·−∞ 표시 vs 전수, 4정점 그래프 3^12 개 전수로 경로 복원 검사.

## 2. Problem (English)

You maintain routing and pricing tables on an embedded gateway. Implement, without dynamic allocation:

```c
/* Bellman-Ford over an edge list. src >= 0, or BF_ALL (virtual source: every dist starts at 0).
 * At most n passes; return early after a pass with no relaxation.
 * If a relaxation happens in pass n, stop, write one negative cycle as edge ids in forward order
 * to cyc_e[0..*cyc_len) and return BF_NEGCYCLE. Otherwise BF_OK with dist[] / pred_e[]. */
int bf_run(const graph_t *g, int src, int64_t *dist, int16_t *pred_e,
           int *cyc_e, int *cyc_len, bf_stats_t *st);

/* rate[i][j] = units of currency j per 1 unit of i, as an integer with 8 decimals (0 = no quote).
 * Return 1 and a profitable cycle of currencies if one exists, else 0.
 * Must never report a cycle whose exact product is <= 1. */
int arb_detect(int n, const uint64_t rate[][MAX_CUR], int *cyc, int *len);

/* All-pairs shortest paths in place on d (int32, FW_INF = no edge), next-hop table nx (uint8).
 * Returns the number of k with d[k][k] < 0; pairs whose distance is -infinity are set to FW_NEG_INF. */
int fw_run(int n, int32_t d[][MAX_V], uint8_t nx[][MAX_V]);
int fw_path(int n, const int32_t d[][MAX_V], const uint8_t nx[][MAX_V], int i, int j, int *path, int max);
```

Constraints: n ≤ 256, m ≤ 4096, Bellman-Ford weights |w| ≤ 2^40, Floyd–Warshall weights |w| ≤ 2^22 − 1. No signed overflow anywhere, even with negative cycles. Deterministic.

Example (Part A hand graph, `src = 0`, edges listed back-to-front):
`dist = [0, 5, 8, 5, 7]`, 4 passes, `path(0→4) = [0, 2, 3, 4]`.
Example (Part B): rates `A→B 0.5, B→C 0.8, C→A 2.5` (and reverses 2.0, 1.25, 0.4): no arbitrage. A `double` implementation reports one.

Follow-ups: Why n−1 passes? Why walk `pred` n times before collecting the cycle? Why must `k` be the outer loop? What can overflow, and when? Would you put a 256×256 matrix on a Cortex-M4?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter
```

TODO 는 여섯 군데: `bf_extract_cycle`, `bf_run` (Part A) / `rate_to_weight`, `arb_detect` (Part B) / `fw_run`, `fw_path` (Part C). 그래프 구조, `bf_path`, `fw_init`, double 판 `arb_detect_double`, O(V²) 다익스트라, 전수 기준(단순 경로·단순 사이클·정확한 큰 정수 곱)과 하네스는 주어진다. 스텁은 "전부 도달 불가 / 차익 없음 / 아무것도 안 함" 을 돌려줘 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:359  r == BF_OK && dist[0] == 0 && dist[1] == 5 && dist[2] == 8 && dist[3] == 5 && dist[4] == 7
FAIL starter.c:360  st.passes == 4
…
Part A random vs brute: 15152 runs (0 with negative cycle), dist mismatch=28394, path bad=0, detect mismatch=1426, bad cycle=0
…
Part B exact-1 table (0.5*0.8*2.5): double says ARBITRAGE, fixed-point says none, exact says none
…
Part C negative cycles: 3000 graphs (1069 with a negative cycle), detect mismatch=1069, -inf mark mismatch=11482
Part C exhaustive 4-node {none,0,1}: 531441 graphs, bad path=6377292
7/41 passed
```

(0.05 초, exit 1. 통과한 7개는 스텁과 무관하게 맞는 것들 — 패스 수 상한(스텁은 0 패스), double 판이 "차익" 을 내는 검사와 스텁의 "차익 없음" 이 우연히 맞는 표들, 거짓 양성 0(아무것도 안 찾으니까), 주어진 `fw_init` 의 입력·범위 검사와 비대칭 초기값.)

## 4. 힌트

<details><summary>힌트 1 — 벨만-포드는 "모든 간선 한 번" 을 반복할 뿐</summary>

한 패스 = `for k in edges: if dist[u] != INF && dist[u] + w < dist[v]: dist[v] = …, pred_e[v] = k`. 패스 번호를 1..n 으로 세고, 패스가 끝났을 때 변화가 없으면 바로 끝낸다. n 번째 패스에서 완화가 **한 번이라도** 일어나면 그 즉시 멈춘다 — 그 순간의 pred 로 사이클을 꺼내야 증명이 성립한다(힌트 3).

</details>

<details><summary>힌트 2 — BF_ALL 은 초기화만 다르다</summary>

모든 정점에 가중치 0 간선을 가진 가상 출발점 s* 를 상상하라. s* 에서의 첫 패스는 "모든 dist = 0" 과 같다. 그래서 초기화를 0 으로 하고 pred 는 -1 로 두면 나머지 코드는 그대로다. 패스 수 한계도 그대로 n 이다 (5. 풀이).

</details>

<details><summary>힌트 3 — 완화된 정점은 사이클 밖일 수 있다</summary>

n 번째 패스에서 방금 완화된 v 에서 pred 를 따라가면 "꼬리 → 사이클" 모양(ρ)이다. 꼬리 길이는 n−1 이하이므로 **n 번** 따라간 정점은 반드시 사이클 위다. 거기서 출발점으로 돌아올 때까지 pred 간선을 모으고 뒤집으면 정방향.

</details>

<details><summary>힌트 4 — 차익 가중치는 "위로" 반올림</summary>

반올림 방향이 판정 방향을 정한다. 각 가중치를 진짜 값보다 **조금 크게** 만들면 정수 합이 음수일 때 진짜 합도 음수다(거짓 양성 없음). double 오차까지 덮으려면 올림 뒤 +1. `ldexp(x, 32)` 는 2^32 를 곱하는 정확한 연산이다.

</details>

<details><summary>힌트 5 — 플로이드-워셜: k 바깥, 피연산자 INF 면 건너뛰기, 아래로 포화</summary>

`for k { for i { dik = d[i][k]; if INF continue; for j { dkj = d[k][j]; if INF continue; s = dik + dkj; if (s < FW_NEG_INF) s = FW_NEG_INF; if (s < d[i][j]) { d[i][j] = s; nx[i][j] = nx[i][k]; } } } }`. 다음 홉은 "i 에서 k 로 갈 때의 첫 홉" 이다 — `k` 가 아니다. 끝나고 `d[k][k] < 0` 인 k 를 세고, 그런 k 마다 `d[i][k]`, `d[k][j]` 가 유한한 모든 (i, j) 를 `FW_NEG_INF` 로.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 음수 가중치가 있나? 음수 사이클이 있으면 무엇을 돌려줘야 하나(검출만? 사이클 자체? −∞ 쌍?) 한 출발점인가 모든 쌍인가? n, m, 가중치 범위(→ 정수 폭)? 환율이면 시세 형식(소수 몇 자리)·수수료 포함인가? |
| 2. 무식한 방법 | 모든 단순 경로/사이클 나열 — 지수. 테스트 기준으로만 쓴다 (이 레슨의 n ≤ 7 전수) |
| 3. 더 나은 생각 + 불변식 | 벨만-포드: "r 패스 후 dist[v] ≤ 간선 r 개 이하 걸음의 최소". 음수 사이클이 없으면 최단 걸음 = 단순 경로(간선 ≤ n−1). 플로이드-워셜: "k 단계 후 d[i][j] = 중간 정점 ⊆ {0..k−1} 인 최단 경로" |
| 4. 코드 | 간선 목록 + 조기 종료 + n 번째 패스 검출 + pred n 번 → 사이클. FW 는 k 바깥 + INF 가드 + 포화 + `nx[i][j] = nx[i][k]` |
| 5. 소리 내어 말할 테스트 | 정점 1개, 음수 자기 루프(길이 1 사이클), 0 가중치 사이클(음수 아님!), 평행 간선, 안 닿는 음수 사이클(BF_ALL), 꼬리 사례, 간선 역순 사슬(최악 패스), 곱이 정확히 1 인 환율표 |
| 6. 복잡도 | BF: 시간 O(nm), 조기 종료로 실제 패스 수는 랜덤 그래프에서 ~log n 수준(측정), 추가 메모리 O(n). FW: Θ(n³) 시간, Θ(n²) 메모리 — n = 256 이면 int32 행렬만 256 KiB. 재귀 없음 → 스택 O(1) |

### Part A — 왜 n−1 패스인가

δ(v) 를 src 에서 v 까지 최단 거리, δ_r(v) 를 "간선 r 개 이하 걸음(walk)" 중 최소라 하자.

**보조정리 1 (패스 r 뒤).** r 번째 패스가 끝나면 `dist[v] ≤ δ_r(v)`. 귀납: 간선 r 개 이하 최적 걸음의 마지막 간선을 (u, v) 라 하면 앞부분은 간선 r−1 개 이하라서 패스 r−1 뒤 `dist[u] ≤ δ_{r−1}(u)`. 패스 r 에서 (u, v) 를 볼 때 dist[u] 는 그보다 작거나 같고(값은 줄기만 한다), 완화가 `dist[v] ≤ dist[u] + w` 를 만든다. 패스 **안에서** 이미 줄어든 값을 바로 쓰는 것(in-place)은 이 부등식을 깨지 않는다 — 더 빨리 수렴할 뿐이다.

**보조정리 2 (아래로 안 내려감).** 유한한 dist[v] 는 언제나 어떤 실제 걸음 src → … → v 의 길이다 (완화가 "dist[u] 의 걸음 + 간선" 을 만든다). 음수 사이클이 닿지 않으면 모든 걸음 ≥ δ(v) 이므로 `dist[v] ≥ δ(v)`.

**정리.** src 에서 닿는 음수 사이클이 없으면, 최단 걸음은 사이클을 빼도 길어지지 않으므로 단순 경로이고 간선은 n−1 개 이하. 두 보조정리로 패스 n−1 뒤 `δ(v) ≤ dist[v] ≤ δ_{n−1}(v) = δ(v)`. ∎

**조기 종료가 맞는 이유.** 어떤 패스에서 변화가 없으면 모든 간선이 `dist[v] ≤ dist[u] + w` 를 만족하는 고정점이다 — 다음 패스도 아무것도 안 바꾸므로 n−1 패스 뒤의 값과 같다. 덤으로 "닿는 음수 사이클 없음" 도 증명된다: 닿는 사이클 C 위에서 부등식을 모두 더하면 dist 항이 상쇄되어 `0 ≤ w(C)`.

손 계산 (`solution.c` Part A, 간선을 일부러 뒤에서부터 나열 — 각 패스 뒤 상태는 이 레슨 작업용 trace 프로그램의 실제 출력):

| 간선 순서 | e0 C→D 2 · e1 B→C −3 · e2 A→B 4 · e3 S→A 5 · e4 S→B 8 · e5 A→C 6 · e6 D→A −1 |
|---|---|

| 패스 | dist (S A B C D) | pred_e (S A B C D) | 무슨 일 |
|---|---|---|---|
| 1 | 0 5 8 11 INF | − 3 4 5 − | e0, e1, e2 는 출발점이 아직 INF → 건너뜀. e3·e4·e5 가 A, B, C 를 연다 |
| 2 | 0 5 8 **5** **13** | − 3 4 **1** **0** | e0: D = 11+2. e1: C = 8−3 = 5 &lt; 11 |
| 3 | 0 5 8 5 **7** | − 3 4 1 0 | e0 이 맨 앞이라 C 의 새 값이 한 패스 늦게 D 로 간다 |
| 4 | 0 5 8 5 7 | (같음) | 변화 없음 → 종료. 패스 4 &lt; n = 5 |

실제 출력: `Part A hand: dist = 0 5 8 5 7, passes=4 relax=6`. 경로 S→B→C→D (0+8−3+2 = 7).

**간선 순서가 전부다.** 같은 사슬 0→1→…→39 (가중치 −1) 이 간선 정방향이면 패스 1 에 모두 확정되고 패스 2 가 "변화 없음" 을 확인한다. 역방향이면 패스마다 한 칸씩 → 패스 39 까지 변화, 패스 40 이 확인 (실제 출력: `forward edge order passes=2, reverse order passes=40`). n−1 + 1 = n 패스가 **실제로** 필요한 입력이 있다는 뜻이고, 최악 O(nm) 은 과장이 아니다. 무작위 그래프에서는 다르다 (`demo_bench.c` 3번, 실제 출력):

```text
3) Bellman-Ford passes (incl. final no-change pass), random graphs m = 4n, edge order random, 200 graphs x 1 src
   n= 16  avg passes 4.03  max 6  (worst case n = 16)
   n= 64  avg passes 5.61  max 9  (worst case n = 64)
   n=256  avg passes 7.64  max 11  (worst case n = 256)
```

패스 수 ≈ (최단 경로 트리의 깊이 중 "간선 순서를 거스르는" 횟수) + 1 이라 무작위 그래프에선 깊이 ~ log n 정도로 자란다. 펌웨어 판단: **평균은 빠르지만 최악 n 패스를 시간 예산에 넣어야** 한다 (워치독·RTOS 마감). 간선을 BFS 순서로 미리 정렬해 두면 실제 패스 수를 줄일 수 있다 — 이건 SPFA 아이디어와 같은 뿌리다.

### Part A — n 번째 패스 = 음수 사이클 (양방향)

- (⇐) 닿는 음수 사이클이 없으면 패스 n−1 뒤 dist = δ 이고 고정점이므로 패스 n 에서 완화가 없다.
- (⇒) 닿는 음수 사이클 C 가 있는데 패스 n 에서 완화가 없다면, 그 패스 동안 C 위의 dist 는 변하지 않았고(유한 — 닿으니까) 모든 간선에서 `dist[v] ≤ dist[u] + w` 가 동시에 성립한다. C 를 따라 더하면 `0 ≤ w(C)`. 모순. ∎

`BF_ALL` 도 같다: 가상 출발점 s* 와 0 간선을 더한 n+1 정점 그래프에서 첫 패스(초기화)가 이미 끝난 상태이므로, 남은 간선 ≤ n−1 개 → 패스 n−1 + 검출 패스 n.

### Part A — 사이클 꺼내기: 왜 pred 를 n 번 따라가나

pred 그래프 = 각 정점에서 `pred_e[v]` 간선의 출발점으로 가는 화살표(정점당 하나 이하) — "함수 그래프" 라서 따라가면 반드시 (a) pred 가 없는 정점에서 끝나거나 (b) 사이클로 들어간다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a02b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">꼬리 사례: 패스 6(= n)에서 처음 완화된 정점은 E — 사이클 밖이다</text>
  <g stroke="currentColor" stroke-width="1.5">
    <circle cx="70" cy="150" r="20" class="f-ok"/>
    <circle cx="210" cy="80" r="20" class="f-warn"/><circle cx="360" cy="80" r="20" class="f-warn"/>
    <circle cx="360" cy="220" r="20" class="f-warn"/><circle cx="210" cy="220" r="20" class="f-warn"/>
    <circle cx="560" cy="220" r="20" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="70" y="155">S</text><text x="210" y="85">A</text><text x="360" y="85">B</text>
    <text x="360" y="225">C</text><text x="210" y="225">D</text><text x="560" y="225">E</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.6" stroke-dasharray="5 4" marker-end="url(#a02b)">
    <path d="M538 220 L382 220"/>
    <path d="M232 220 L338 220"/>
    <path d="M360 200 L360 102"/>
    <path d="M338 80 L232 80"/>
    <path d="M210 100 L210 198"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="420" y="210">pred</text><text x="250" y="240">D 의 pred = C</text>
    <text x="370" y="150">C 의 pred = B</text><text x="250" y="70">B 의 pred = A</text><text x="100" y="150">A 의 pred = D</text>
  </g>
  <text x="460" y="70" fill="currentColor" font-size="12">사이클 A→B→C→D→A</text>
  <text x="460" y="88" fill="currentColor" font-size="12">= 4 − 3 + 2 − 9 = −6</text>
  <text x="10" y="270" fill="currentColor">E 에서 pred 6 번: D, C, B, A, D, C → C 는 사이클 위. C 에서 C 로 돌아올 때까지 모으고 뒤집으면 정방향.</text>
  <text x="10" y="290" fill="currentColor">E 에서 "E 로 돌아올 때까지" 모으면 영원히 안 돌아온다 (무한 루프 또는 길이 초과).</text>
</svg>
```

**보조정리 3 (pred 그래프의 사이클은 음수).** 완화로 `pred[x]` 를 정할 때 `dist[x] = dist[p] + w(p, x)` 이고, 그 뒤 `dist[p]` 는 줄기만 하므로 언제나 `dist[x] ≥ dist[p] + w(p, x)`. dist[x] 와 pred[x] 는 **같이** 바뀐다. 마지막 완화 `pred[v] := u` 가 사이클을 닫는 순간, 그 직전에는 `dist[v] > dist[u] + w(u, v)` (엄격). 사이클을 따라 더하면 dist 합이 양쪽에서 같아서 `0 > w(사이클)`. 사이클이 그대로 남아 있는 동안 그 위의 dist 는 안 바뀌므로(바뀌면 pred 도 바뀐다) 엄격함이 유지된다. ∎ — 덤: 음수 사이클이 없으면 pred 그래프는 **항상 숲**이라 `bf_path` 가 반드시 src 에 닿는다 (0 가중치 사이클이 있어도).

**정리 (n 번 따라가면 사이클 안).** 패스 n 에서 v 가 완화된 **직후**, v 에서의 pred 사슬이 사이클 없이 pred 가 없는 정점 s 에서 끝난다고 하자. s 는 dist 가 처음 값(src 면 0, BF_ALL 이면 0) 그대로이고, 사슬은 간선 ≤ n−1 개인 단순 경로 P(s → v). 보조정리 3 의 부등식을 사슬을 따라 더하면 `dist_new[v] ≥ w(P) ≥ δ_{n−1}(v)` (BF_ALL 이면 "아무 데서나 출발" 의 최소). 그런데 완화 직전 값은 보조정리 1 로 `dist_old[v] ≤ δ_{n−1}(v)` 이고 `dist_new[v] < dist_old[v]` — 모순. 그러므로 사슬은 사이클로 들어가고(ρ 모양), 꼬리 + 사이클 정점 수 ≤ n 이라 **꼬리 ≤ n − (사이클 길이) ≤ n−1**. n 번 따라간 정점은 사이클 위. 보조정리 3 으로 그 사이클은 음수. ∎

두 가지 주의:

1. 증명은 "완화 **직후**" 의 pred 를 쓴다. 패스 n 을 끝까지 돌고 나서 꺼내면 다른 pred 가 바뀌어 전제가 깨질 수 있다 → 첫 완화에서 멈춘다 (연습 3 의 `diff_solve` 가 이 때문에 한 번 더 돈다).
2. 꼬리는 드물지 않다. "n 번 걷기" 를 빼면(7. 실수 3) 무작위 음수 사이클 1426 건 중 **551 건**(39%)에서 추출이 실패했다 — 처음 완화된 정점이 사이클 밖이었다는 뜻이다.

실제 출력: `Part A tail case: negcycle len=4 starting at 3, passes=6` (3 = C).

### Part A — 정수 폭과 INF

- `dist[u] == BF_INF` 이면 건너뛴다 — `INT64_MAX + w` 는 부호 있는 오버플로(C11 6.5p5, UB). 이 한 줄을 지운 변형은 -O2 에서 wrap 된 값이 "도달 가능" 처럼 보여 41개 중 10개 검사가 FAIL 했다.
- **음수 사이클이 있어도 넘치지 않는가?** 유한한 dist 는 "성공한 완화 수" 이하 간선의 걸음 길이다. 완화는 패스당 ≤ m, 패스 ≤ n 이므로 `|dist| ≤ n·m·W`. n = 256, m = 4096, |w| ≤ 2^40 → 2^8·2^12·2^40 = 2^60 &lt; 2^63. 음수 사이클을 n 패스 넘게 돌리지 않으니 이 한계가 성립한다 (BF 를 "수렴할 때까지" 돌리면 음수 사이클에서 끝없이 내려간다).
- Cortex-M4 에서 int64 덧셈·비교는 32비트 명령 두어 개라 비싸지 않다. 가중치가 작으면(예: µs 지연 ≤ 2^16, n ≤ 256) `|dist| ≤ n·m·W` 를 다시 계산해 int32 로 줄일 수 있다.

### Part B — 환율 차익: 왜 −log, 그리고 부동소수점

곱이 1 을 넘는 순환 ∏ r_e > 1 ⇔ Σ ln r_e > 0 ⇔ Σ (−ln r_e) &lt; 0. 그래서 가중치 −ln(rate) 그래프의 음수 사이클 = 차익. 출발 통화와 무관하게 찾으려면 `BF_ALL`.

**문제: double.** 시세 0.5 / 0.8 / 2.5 는 곱이 정확히 1 이다. 그런데 `-log(0.5)-log(0.8)-log(2.5)` 를 double 로 계산하면 −1.11e−16 (반대 방향 순환도 −1.11e−16, 실제 실행). 흔한 double 판 `arb_detect_double` 은 이 표에서 **차익 있음** 을 낸다:

```text
Part B exact-1 table (0.5*0.8*2.5): double says ARBITRAGE, fixed-point says none, exact says none
```

곱이 정확히 1 인 이런 3-순환(소수 4자리 시세, 앞의 두 시세 ∈ [0.5, 2.0])을 이 레슨 작업 중 훑어 보니 61개 중 37개가 double 합이 음수였다(나머지 24개는 정확히 0, 양수는 0개 — 실제 실행, 수치는 입력 범위에 따라 다르다). 반대로 진짜 차익이 1e−16 수준이면 double 이 **숨길** 수도 있다. 어느 쪽이든 "부호 하나로 판정하는데 그 부호가 반올림 잡음 크기" 라는 게 문제다. 흔한 처방 `d[u] + w < d[v] - EPS` 는 잡음을 가리지만, EPS 가 완화마다 들어가서 "몇 % 미만의 차익은 놓친다" 같은 **명시적 한계를 말하기 어렵다**.

**처방: 정수 고정소수점 + 한계 증명.** 시세는 소수 8자리 정수 q 로 들어온다(q &lt; 2^53 이면 `(double)q` 는 정확). 가중치:

```c
w = (int64_t)ceil(ldexp(-log((double)q / 1e8), 32)) + 1;     /* 2^-32 단위, 위로 */
```

x = −ln(q/1e8) 를 진짜 값, x̂ 를 double 계산값이라 하자. 나눗셈은 상대 오차 2^−53, `log` 의 오차를 1 ulp 이하로 보면(libm 정확도 — 확인 권장) |x| ≤ 25 인 범위에서 |x̂ − x| ≤ 2^−46 정도, 2^32 를 곱해도 2^−14 ≪ 1. `ldexp` 와 `ceil` 은 이 범위(2^37 미만)에서 정확하다. 그래서

    x·2^32 − 2^−14 + 1 ≤ w ≤ x·2^32 + 2^−14 + 2   ⇒   x·2^32 < w < x·2^32 + 2.0001

길이 L 사이클에서 더하면:

- **거짓 양성 없음**: Σw &lt; 0 ⇒ Σx·2^32 &lt; Σw &lt; 0 ⇒ 진짜 곱 > 1. 벨만-포드가 돌려준 사이클은 정수 그래프의 음수 사이클이므로 진짜 차익이다.
- **놓침의 한계**: 어떤 순환이 ln(곱) > 2.0001·L·2^−32 (L = 4 면 약 1.9e−9) 이면 Σw &lt; 0 → 정수 그래프에 음수 사이클이 있으므로 BF 가 **어떤** 차익 사이클을 반드시 낸다. 그보다 작은 차익은 놓칠 수 있다 — 설계로 정한, 문서화된 회색 지대다. 실무의 수수료(보통 bp = 1e−4 단위)에 비하면 무시할 크기.

검사 (실제 출력):

```text
Part B stale EUR->GBP quote: arbitrage cycle (len 2): GBP EUR
Part B random tables: 4000 tables, 854 with exact arbitrage, false positive=0, missed beyond bound=0, missed inside bound (allowed)=2, bad cycle=0
```

- 낡은 EUR→GBP 시세 하나(1.08/1.27 × 1.005)가 들어간 표에서 BF 는 그림의 3-순환이 아니라 GBP→EUR→GBP 2-순환을 먼저 찾았다 (0.85464567 × 1.17475 ≈ 1.0040). 테스트는 "EUR→GBP 간선을 쓰고 정확한 곱 > 1" 만 요구한다 — 어떤 사이클이 나올지는 간선 순서에 달렸으니 특정 사이클을 기대하면 안 된다.
- 무작위 표의 정답은 **모든 단순 순환을 나열해 큰 정수로 정확히** ∏q > (1e8)^L 을 비교해서 얻었다 (`bn_t`, 32비트 limb). 회색 지대에 걸린 2개는 이득이 2L·2^−32 이하인 순환뿐인 표였다 — 놓쳐도 되는 경우.
- `rate_to_weight` 자체는 10개 값에서 long double 참값과 `tw < w ≤ tw + 2` 와 단조성을 확인한다.

**가장 이득이 큰 순환** 을 찾는 건 다른 문제다(단순 사이클 최대화 = NP-hard 계열 — 확인 권장). 벨만-포드는 "있다 + 하나" 까지다. 실무에서는 찾은 사이클을 실행 전에 **정확한 정수로 다시 계산**(`cycle_profit_exact` 처럼)하고, 수수료·호가 깊이를 반영한다.

### Part C — 플로이드-워셜: 왜 k 가 가장 바깥인가

d^(k)[i][j] = "중간 정점이 모두 {0, …, k−1} 안인 i→j 최단 경로" 라 하자. 음수 사이클이 없으면 최단 경로는 단순하므로, 중간 정점 ⊆ {0..k} 인 최단 경로는 k 를 안 쓰거나 **정확히 한 번** 쓰고, 쓴다면 k 에서 잘린 두 조각은 각각 중간 정점 ⊆ {0..k−1}:

    d^(k+1)[i][j] = min( d^(k)[i][j],  d^(k)[i][k] + d^(k)[k][j] )

k 단계 전체가 끝나야 다음 단계의 재료가 준비된다 — 그래서 k 가 바깥이다. k 를 가장 안쪽에 두면(`for i for j for k`) d[i][j] 를 한 번에 확정하는데 그때 d[i][k], d[k][j] 는 아직 덜 계산된 값이다. 실제로 그렇게 바꾼 변형은 다익스트라 대조에서 333228 쌍 중 **34504** 쌍이 틀렸다.

**제자리(in-place) 갱신이 괜찮은 이유**: 단계 k 동안 k 행·k 열은 안 바뀐다. `d[i][k] = min(d[i][k], d[i][k] + d[k][k])` 이고 음수 사이클이 없으면 d[k][k] = 0. 그래서 행렬 하나로 충분하다 (코드에서 `dik` 를 미리 읽어 두는 것도 같은 이유로 안전하다).

trace (4정점: 0→1 4, 1→2 −2, 0→2 5, 2→3 3, 3→0 1, 1→3 6; 작업용 trace 프로그램의 실제 출력, 바뀐 칸만):

| 단계 | 바뀐 칸 | 이유 |
|---|---|---|
| k=0 | d[3][1] INF→5, d[3][2] INF→6 | 3→0→1, 3→0→2. `nx[3][1] = nx[3][2] = nx[3][0] = 0` |
| k=1 | d[0][2] 5→**2**, d[0][3] INF→10, d[3][2] 6→3 | 음수 간선 1→2 가 0→2 직행(5)을 이긴다 |
| k=2 | d[0][3] 10→5, d[1][3] 6→**1** | 1→2→3 = −2+3 |
| k=3 | d[1][0] INF→2, d[2][0] INF→4, d[2][1] INF→8 | 마지막에 3→0 이 열린다 |

최종 `nx[3][2] = 0`, `nx[0][2] = 1`, `nx[1][2] = 2` → 경로 3→0→1→2, 비용 1+4−2 = 3 = d[3][2]. 

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a02c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">단계 k: "i → k → j" 를 시도. nx[i][j] 는 nx[i][k] (i 에서 k 로 갈 때의 첫 홉)</text>
  <rect x="160" y="40" width="420" height="120" rx="12" class="f-mut" stroke="currentColor"/>
  <text x="172" y="60" fill="currentColor" font-size="12">중간 정점 ⊆ {0, …, k−1} 만 쓰는 최단 경로들 (이미 계산됨)</text>
  <g stroke="currentColor" stroke-width="1.5">
    <circle cx="80" cy="200" r="20" class="f-acc"/><circle cx="370" cy="110" r="20" class="f-warn"/><circle cx="660" cy="200" r="20" class="f-acc"/>
    <circle cx="200" cy="120" r="14" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="205">i</text><text x="370" y="115">k</text><text x="660" y="205">j</text><text x="200" y="125">a</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.6" marker-end="url(#a02c)">
    <path d="M96 188 L188 128"/>
    <path d="M214 118 L348 110" stroke-dasharray="5 4"/>
    <path d="M390 116 L640 192" stroke-dasharray="5 4"/>
    <path d="M100 205 L638 205" stroke-dasharray="2 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="60" y="150">첫 홉 a = nx[i][k]</text>
    <text x="250" y="102">d[i][k]</text><text x="520" y="140">d[k][j]</text>
    <text x="300" y="225">기존 d[i][j] (k 를 안 거침)</text>
    <text x="10" y="245">d[i][k] + d[k][j] &lt; d[i][j] 이면 바꾸고 nx[i][j] = a. 각 값 = 첫 간선이 i→nx 인 실제 걸음의 길이 (경로 복원 증명의 핵심)</text>
  </g>
</svg>
```

### Part C — next[i][j] 로 복원한 경로는 맞는가

**불변식.** 언제나 d[i][j] (유한, i ≠ j) 는 **첫 간선이 i → nx[i][j] 인 실제 걸음**의 길이다. 초기값(간선 하나)에서 성립하고, 갱신은 걸음 W(i,k)·W(k,j) 의 길이를 넣으며 첫 간선은 W(i,k) 의 첫 간선 = i → nx[i][k] 이다.

음수 사이클이 없으면 끝났을 때 d = δ 이고, a = nx[i][j] 라 하면 그 걸음의 나머지(a → j)는 길이 ≥ δ(a, j) 이므로 `δ(i,j) ≥ w(i,a) + δ(a,j)`. 반대 부등식은 삼각 부등식. 그래서 **모든 다음 홉 간선은 꼭 맞는(tight) 간선**이고, nx 를 따라가는 걸음의 비용은 정확히 d[i][j] 다 (도착한다면).

도착은? 꼭 맞는 간선만 따라가다 같은 정점을 다시 만나면 그 순환의 가중치는 0 이다. 그러니 **양의 가중치 사이클만 있으면 반드시 도착**한다. 0 가중치 사이클이 있을 때도 엄격한 `<` 갱신 덕에 도는 경우를 나는 찾지 못했다: 4정점 그래프에서 방향 간선 12개 각각 {없음, 0, 1} 인 3^12 = **531441 개 전수** 와 무작위 수천 개에서 모든 쌍의 복원이 n 걸음 안에 도착하고 비용 == d 였다 (실제 출력 `bad path=0`). 하지만 일반 증명은 여기서 하지 않았다 — 확인 권장. 그래서 `fw_path` 는 n 걸음을 넘으면 −1 을 돌려주는 가드를 둔다. (벨만-포드의 pred 는 보조정리 3 덕에 증명이 있다 — 대조적이다.)

`nx[i][j] = k` 로 적는 흔한 실수는 "k 를 거친다" 는 정보일 뿐 첫 홉이 아니다. 그 변형은 링크 장애 시나리오와 경로 비용 검사에서 바로 FAIL 했다 (4정점 전수 531441 개 중 148716 쌍 불량).

### Part C — INF, 지수 폭주, 음수 사이클

**INF 가드.** `FW_INF = INT32_MAX`. 피연산자 중 하나라도 INF 면 건너뛴다 — INF + 음수가 "유한한 큰 값" 으로 둔갑하는 것과 UB 를 둘 다 막는다.

**음수 쪽: 지수 폭주.** 음수 사이클이 있으면 플로이드-워셜의 값은 단계마다 몇 배씩 작아진다. Hougardy (2010) 는 행렬 원소가 Ω(6^n) 배까지 커질 수 있음을 보였다(초록 기준 — 본문 정리는 확인 권장). 모든 간선이 −1 인 완전 그래프로 직접 재 보면 (`demo_bench.c` 1번, int64, 가드 없음, 실제 출력):

```text
   n= 2  min entry = -4
   n= 3  min entry = -24
   n= 4  min entry = -144
   n= 5  min entry = -864
   n= 8  min entry = -1.866e+05
   n=13  min entry = -1.451e+09
   n=14  min entry = -8.707e+09  (< INT32_MIN)
   n=25  min entry = -3.159e+18  (< INT32_MIN)
   n=26  min entry = -8.687e+18  (< INT32_MIN)  <-- int64 addition overflowed
```

정확히 4·6^(n−2) 다. **int32 는 n = 14, int64 도 n = 26 에서 넘친다.** "음수 사이클은 끝나고 d[i][i] 로 검사하면 되지" 는 그 전에 UB 가 터지므로 틀린 생각이다.

**포화(clamp).** 합이 `FW_NEG_INF = −2^30` 보다 작으면 −2^30 으로 자른다. 이게 안전한 이유:

1. 음수 사이클이 끼어들지 않는 쌍(i, j)의 값은 언제나 실제 걸음 길이 ≥ δ(i,j) ≥ −(n−1)W, 그리고 처음 값 이하라서 ≤ (n−1)W. n ≤ 256, W = FW_WMAX = 2^22 − 1 이면 (n−1)W = 1,069,547,265 &lt; 2^30 = 1,073,741,824. 그러니 이런 쌍은 **절대 포화되지 않는다**.
2. 그런 쌍의 갱신은 같은 성질의 값만 재료로 쓴다: d[i][k] 가 음수 사이클의 영향을 받으면(i ⇝ C ⇝ k) d[k][j] 가 유한한 이상 i ⇝ C ⇝ j 도 영향을 받는다. 포화된 값은 영향받는 쌍에만 들어가고, 그 쌍의 답은 어차피 −∞ 다.
3. 모든 유한 값이 [−2^30, 2^30) 이므로 두 값의 합은 [−2^31, 2^31) — int32 덧셈에 오버플로가 없다. 양쪽 합의 최댓값 2·1,069,547,265 = 2,139,094,530 &lt; 2^31 − 1 도 확인.

이 가드를 지운 변형은 -O2 실행에서는 41/41 이 그대로 통과했다(wrap 된 값이 우연히 표시 단계에서 덮였다). **UBSan 실행만** `signed integer overflow: -774840978 + -1549681955` 로 잡았다. 그래서 검증 절차에 sanitizer 실행이 들어 있다.

**검출과 −∞ 표시.**

- 음수 사이클이 있다 ⇔ 어떤 k 에서 d[k][k] &lt; 0. (⇐) d[k][k] 는 k 로 돌아오는 실제 닫힌 걸음의 길이(또는 포화값 ≤ −2^30)이고, 음수 닫힌 걸음은 음수 단순 사이클을 포함한다. (⇒) 음수 단순 사이클 위의 정점 v 에 대해 위 귀납("중간 정점 ⊆ {0..k−1} 인 **단순** 경로 이하" — 상한 쪽은 음수 사이클이 있어도 성립)을 v→…→v 에 쓰면 d[v][v] ≤ w(C) &lt; 0.
- (i, j) 가 −∞ ⇔ d[k][k] &lt; 0 이고 d[i][k], d[k][j] 가 유한한 k 가 있다. (⇐) i ⇝ k ⇝ (음수 닫힌 걸음) ⇝ k ⇝ j 를 원하는 만큼 돌 수 있다. (⇒) i 에서 j 로 가는 걸음이 음수 단순 사이클 C 를 지날 수 있으면 C 위의 v 는 d[v][v] &lt; 0 이고 i ⇝ v ⇝ j.

```text
Part C negative cycles: 3000 graphs (1069 with a negative cycle), detect mismatch=0, -inf mark mismatch=0
```

기준은 FW 와 무관한 단순 사이클 전수(음수 사이클 위 정점) + 도달 행렬이다. 벨만-포드(`BF_ALL`) 의 검출 결과도 같은 3000개에서 일치했다. 모든 간선 −1 인 K_40 에서 `fw_run` 은 40개 정점 모두 d[k][k] &lt; 0, 모든 쌍 −∞ 로 끝난다 (UBSan 보고 없음).

### Part C — 통신망 지연 행렬과 라우팅 테이블

`solution.c` 의 시나리오 (한 방향 지연 µs, radio↔gw 는 900 / 1400 으로 비대칭), 실제 출력:

```text
Part C latency matrix (us), row = from:
          fc    gw radio   cam   imu  esc1  esc2   gps
    fc     0   120  1020   420    40    80   200   370
    gw   120     0   900   300   160   200   320   250
 radio  1520  1400     0  1700  1560  1600  1720  1650
   cam   420   300  1200     0   460   500   620   550
   imu    40   160  1060   460     0   120   240   410
  esc1    80   200  1100   500   120     0   150   450
  esc2   200   320  1220   620   240   150     0   570
   gps   370   250  1150   550   410   450   570     0
Part C next hop from gw: fc->fc gw->gw radio->radio cam->cam imu->fc esc1->fc esc2->fc gps->gps
Part C fc<->esc2 down: radio->esc2 = 1750 us via radio gw fc esc1 esc2
```

- 행렬은 대칭이 아니다 (radio→gw 1400, gw→radio 900). 무선 상향/하향 비대칭, 큐 깊이 차이 같은 이유로 실제로 흔하다 → 방향 그래프로 모델링.
- `nx[gw][*]` 한 행이 곧 gw 의 **라우팅 테이블**(목적지 → 다음 홉)이다. 각 노드는 자기 행 n 바이트만 있으면 패킷을 보낼 수 있다.
- fc↔esc2 링크가 끊기면 radio→esc2 는 1720 → 1750 µs, 경로가 esc1 을 거친다. 재계산은 Θ(n³) 전체 — 링크 하나 바뀔 때 증분 갱신은 연습 아이디어로 남긴다(간선 비용이 **줄어들 때**는 O(n²) 갱신이 쉽고, 늘어날 때는 어렵다).

**교차 검증** (실제 출력):

```text
Part C FW vs Dijkstra (w>=0) / Bellman-Ford (neg edges): 600 graphs, 333228 pairs, unreachable 105007, mismatch dijkstra=0 bf=0, bad path=0
Part C exhaustive 4-node {none,0,1}: 531441 graphs, bad path=0
```

n = 1..40, m = 0..6n, 자기 루프·평행 간선 허용, 가중치 0..10 (동점·0 많음)과 0..100000 번갈아. 음수 쪽은 **퍼텐셜 트릭**으로 만든다: 음 아닌 w 와 무작위 p(v) 로 w' = w + p(u) − p(v). 어떤 사이클에서도 p 항이 상쇄되어 사이클 합이 그대로(≥ 0)이므로 음수 간선은 많지만 음수 사이클은 절대 없다 — 연습 1 Johnson 의 거꾸로다. 모든 도달 쌍에서 `fw_path` 경로가 실제 간선이고 비용 == d, 도달 불가 쌍은 0.

### Part C — 복잡도와 MCU 에 올릴 수 있나

| | 시간 | 추가 메모리 | n = 256 실제 바이트 |
|---|---|---|---|
| 벨만-포드 (한 출발점) | O(nm), 무작위면 패스 ~8 | dist + pred: O(n) | int32 간선(u,v: 1 B씩 + w 4 B → 8 B 정렬) × 2048 = 16 KiB, dist 1 KiB, pred 512 B |
| 플로이드-워셜 | Θ(n³) = 16.7M 갱신 | d + nx: Θ(n²) | int32 d **256 KiB** + uint8 nx 64 KiB = **320 KiB** |
| FW, int16 d (경로 합 &lt; 2^15 일 때) | 같음 | | 128 KiB + 64 KiB |
| FW, 무방향(대칭) 삼각 저장 | 같음 | | int32 n(n+1)/2 = 128.5 KiB (nx 는 대칭 아님 — 줄지 않는다) |
| 다익스트라 × n (M06, 힙) | O(n·(n+m) log n) | O(n + m) 만, 결과는 행 하나씩 | 행을 그때그때 계산하면 1 KiB |
| **내 행만** (라우팅 노드) | 오프라인 계산 | n B 다음 홉 + n·4 B 거리 | 256 B + 1 KiB |

uint8 nx 는 n ≤ 256 이면 id 0..255 를 다 쓰고 "없음" 값이 남지 않는데, d 가 INF 인 칸의 nx 는 읽지 않으므로 문제없다. 320 KiB 는 STM32F4 급 SRAM(약 192 KB 급 — 데이터시트 확인 권장)에 안 들어가고, H7 급(약 1 MB)에는 들어간다. 결론: **행렬 전체는 호스트/게이트웨이에서 계산해 각 노드에 자기 행만 내려 보내는** 구성이 펌웨어답다. 노드가 스스로 계산해야 하면(토폴로지가 자주 바뀌면) 행렬이 아니라 거리 벡터(분산 벨만-포드, Q7)다.

**Cortex-M4 에서의 안쪽 루프** (`demo_fw_m4.c`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`, 실제 출력에서 j 한 칸):

```text
	ldr.w	r0, [r9, #4]          @ dkj = d[k][j]
	cmp	r0, r4                    @ == INT32_MAX ?
	beq	.LBB0_25                  @ INF 면 건너뜀
	add.w	r2, r3, r8, lsl #2
	add	r0, r12                   @ s = dik + dkj
	ldr	r5, [r2, #4]              @ d[i][j]
	cmp.w	r0, #-1073741824
	it	le
	movle	r0, r10                   @ 포화: s = FW_NEG_INF
	cmp	r0, r5
	ittt	lt
	strlt	r0, [r2, #4]              @ d[i][j] = s
	addlt.w	r0, r1, r8
	strblt.w	lr, [r0, #1]      @ nx[i][j] = nx[i][k]
```

컴파일러가 j 루프를 4배 펼쳤고, 갱신은 분기 없이 IT 블록의 조건부 저장이다. 포화 가드는 `cmp + it + movle` 세 개로 끝난다. 한 칸에 13~14 명령 + 루프 부담이니, 16.7M 칸이면 대략 2×10^8 명령 — 168 MHz M4 에서 **1 초 안팎으로 추정**(측정 아님, 메모리 대기·플래시 대기 상태에 따라 다름). 실시간 루프에서 돌릴 계산은 아니다.

**이 맥에서의 시간** (`demo_bench.c` 2번, Apple M6, Apple clang 21.0.0, -O2, 측정 직전 `uptime` load averages **63.24 66.47 40.80** — 다른 작업자들이 동시에 돌고 있어 절대값보다 비율을 볼 것; 1회 워밍업 후 5회 중앙값, 괄호는 최소..최대, 세 방법의 거리 합이 같은지 확인):

| n (m = 8n) | FW int32 ms | 다익스트라 O(V²) × n ms | 벨만-포드 조기 종료 × n ms | Dij/FW | BF/FW | BF 패스/출발점 |
|---|---|---|---|---|---|---|
| 64 | 0.236 (0.22..0.27) | 0.872 (0.71..1.00) | 0.265 (0.12..0.33) | 3.69 | 1.12 | 6.22 |
| 128 | 1.005 (0.79..2.23) | 4.382 (3.76..6.05) | 0.577 (0.44..1.08) | 4.36 | 0.57 | 5.85 |
| 256 | 7.314 (7.13..9.93) | 32.716 (29.56..38.17) | 2.774 (2.56..4.61) | 4.47 | 0.38 | 7.09 |

읽을 것: (1) 같은 Θ(n³) 인 "배열 다익스트라 × n" 보다 FW 가 4배 빠르다 — 안쪽 루프가 단순하고 순차 접근이다. (2) 희소 무작위 그래프(m = 8n)에서는 **벨만-포드 × n 이 FW 보다 빠르다**: 패스가 ~7 이라 실제 일은 7·n·m = 56n² 이고 n³ 보다 작다. 하지만 이건 평균이다 — 간선 역순 사슬이면 패스 n 개로 n²m 이 된다. 결정적 최악 시간이 필요하면 FW 의 Θ(n³) 가 오히려 예측하기 쉽다. (3) 희소 그래프에서 모든 쌍이 정말 필요하면 Johnson(연습 1: 벨만-포드 한 번 + 힙 다익스트라 n 번, O(nm log n))이 정석이다.

### SPFA 한 단락

SPFA(Shortest Path Faster Algorithm)는 "dist 가 바뀐 정점만 큐에 넣어 그 정점의 간선만 완화" 하는 벨만-포드다. FIFO 큐 + in-queue 플래그 판은 최악 O(nm) 이다 — 큐를 "단계" 로 나누면 단계 r 이 끝날 때 dist ≤ δ_r 가 성립하고(벨만-포드 패스와 같은 귀납), 한 단계에 정점은 큐에 한 번만 있으므로 단계당 O(m), 단계 ≤ n. 음수 사이클 검출은 "dist 를 만든 경로의 간선 수 cnt[v] ≥ n" 으로 한다(연습 2). 무작위 그래프에서는 빠르지만(연습 2 실측: 완화 수가 벨만-포드의 0.87배 — 작은 그래프라 차이가 작다) 최악 보장은 벨만-포드와 같다. "SPFA 가 지수 시간" 이라는 말은 큐 대신 스택(LIFO)을 쓰는 변형 이야기로 알고 있는데, 직접 보이지 않았으니 확인 권장 — 면접에서는 "평균은 빠르지만 최악은 O(nm), 적대적 입력이 존재한다" 까지만 말하자.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution   # 41/41 passed, 0.22 s user
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san -lm && ./solution_san   # 41/41, 보고 없음 (0.7 s)
```

전체 실행 출력의 Part 별 요약 줄은 5. 풀이에 그대로 실었다. 이 맥의 ASan 은 누수 검사(LSan)를 지원하지 않는다 — 어차피 동적 할당이 없다.

## 7. 자주 하는 실수

아래 "실제" 숫자는 `solution.c` 의 사본에 해당 실수를 넣고 같은 하네스를 돌린 결과다 (레슨 폴더 밖의 개인 작업 폴더에서).

**1. 패스를 n−1 번만 돌고 "n−1 번째 패스에서 바뀌면 음수 사이클" 로 판정한다.** 정상 그래프를 음수 사이클로 오판한다(역순 사슬은 정확히 패스 n−1 에서도 바뀐다). 실제: 41개 중 28개 통과, 무작위 15152 회 중 **검출 불일치 596**, 꺼낸 "사이클" 70개가 사이클이 아님. → 완화는 최대 n−1 패스, **검출은 n 번째 패스**.

**2. 꺼낼 때 완화된 정점 v 에서 바로 "v 로 돌아올 때까지" 모은다.** v 가 꼬리 위면 영원히 안 돌아온다(가드가 없으면 무한 루프). 실제(가드 있음): 꼬리 사례 실패, 무작위 음수 사이클 1426 건 중 **551 건** 추출 실패. → pred 를 **n 번** 먼저 따라간다.

**3. 패스 n 을 끝까지 돈 뒤 아무 완화 정점에서 꺼낸다.** 증명은 "첫 완화 직후" 의 pred 를 쓴다. 대개는 맞지만 보장이 사라진다 → 첫 완화에서 멈추거나, 꼭 끝까지 돌아야 하면(−∞ 표시, 연습 4) 추출용으로 한 번 더 돈다.

**4. `dist[u] == INF` 검사를 뺀다.** `INT64_MAX + w` 는 UB 이고, 실제로는 wrap 되어 "INF 보다 조금 작은 유한값" 이 되며 도달 불가 정점이 도달 가능해진다. 실제: 31/41, 손 계산부터 FAIL. → 피연산자가 INF 면 건너뛴다 (BF·FW 모두).

**5. 환율 가중치를 그냥 `floor`(또는 반올림)로 정수화한다.** 반올림 방향이 판정을 정하는데 그걸 안 정했다. 실제: 곱이 정확히 1 인 표에서 고정소수점도 **ARBITRAGE** — double 과 같은 거짓 양성. 무작위 표(스프레드가 0 이 아님)에서는 0 이 나와 **이 검사가 없으면 못 잡는다**. → 위로 올림 + 1, 그리고 한계를 문서화.

**6. FW 에서 k 를 안쪽 루프에 둔다.** 실제: 다익스트라 대조 34504 쌍, 벨만-포드 대조 33585 쌍 불일치. → k 가 가장 바깥.

**7. `nx[i][j] = k` 로 적는다.** k 는 "어딘가에서 거친다" 이지 다음 홉이 아니다. 실제: 무작위 대조에서 경로 복원 112339 쌍 불량, 4정점 전수에서 148716 쌍 불량, 링크 장애 시나리오 FAIL. → `nx[i][j] = nx[i][k]` (또는 pred 행렬 `pred[i][j] = pred[k][j]` 로 끝에서 거꾸로).

**8. 음수 사이클이 있는데 FW 를 그대로 돌린다 (INF 가드만 있고 포화 없음).** 위 표처럼 int32 는 n = 14 에서 넘친다. 실제 변형은 -O2 에서 41/41 통과 → **UBSan 만** 잡았다. → 포화 + 범위 증명, 그리고 sanitizer 실행을 검증에 넣는다. (대안: 단계마다 d[i][i] &lt; 0 이 나오면 즉시 중단.)

## 8. 면접 Q&A

<details><summary>Q1. 벨만-포드는 왜 정확히 n−1 패스면 되나요? 0 가중치 사이클이 있으면요?</summary>

음수 사이클이 닿지 않으면 최단 걸음에서 사이클을 빼도 길어지지 않으므로 최단 경로는 단순하고 간선 ≤ n−1 개. r 패스 뒤 dist ≤ "간선 r 개 이하 걸음의 최소" (패스 안에서 갱신된 값을 바로 써도 성립 — 더 빨라질 뿐). 그리고 dist 는 늘 실제 걸음 길이라 δ 아래로 못 내려간다. 0 가중치 사이클은 "빼도 길이가 같다" 이므로 단순 경로 최단이 여전히 존재해 문제없다 — 단 n 번째 패스에서 완화가 일어나지 않는다(검출 대상 아님). 꼬리: "실제로 몇 패스 도나?" → 간선 순서에 따라 2 ~ n. 역순 사슬이 n, 무작위 n = 256 그래프는 평균 7.6 패스(측정). 꼬리 2: "그럼 최악을 줄이려면?" → 간선을 BFS/위상 순서로 정렬(DAG 면 한 패스로 끝 — M05 위상 정렬 + 한 번 완화 = O(V+E)), 또는 SPFA.

</details>

<details><summary>Q2. 음수 사이클을 "있다" 가 아니라 실제 사이클로 보여 주세요. 왜 pred 를 n 번 따라가나요?</summary>

n 번째 패스에서 완화된 v 의 pred 사슬은 반드시 사이클로 들어간다(사이클 없이 출발점에 닿으면 그 경로 길이 ≥ δ_{n−1}(v) ≥ 직전 dist[v] 라서 방금 줄었다는 것과 모순). 사슬 모양은 ρ — 꼬리 길이 ≤ n−1 이므로 n 번 따라간 정점은 사이클 위. pred 그래프의 사이클은 만들어지는 순간 "직전 dist[v] > dist[u] + w" 가 엄격해서 합이 음수다(보조정리 3). 측정상 완화된 정점이 꼬리에 있는 비율이 무작위 그래프에서 39% 라 이 단계를 빼면 자주 틀린다. 꼬리: "pred 를 정점 대신 간선 번호로 저장한 이유?" → 평행 간선(같은 u→v 에 가중치 둘)에서 어떤 간선이 사이클을 만들었는지 알아야 사이클 가중치를 정확히 보고한다.

</details>

<details><summary>Q3. 환율 차익 문제를 double 로 풀면 뭐가 문제죠? 엡실론을 쓰면 되지 않나요?</summary>

0.5×0.8×2.5 = 1 인데 double 의 −log 합은 −1.11e−16 이라 "차익" 이 나온다(실행 확인). 엡실론은 이걸 가리지만 완화마다 들어가므로 "어떤 크기의 차익까지 보장하나" 를 말하기 어렵고, 경로 길이에 따라 효과가 달라진다. 나는 시세를 정수(소수 8자리)로 받고 가중치를 2^−32 단위로 **위로 올림 + 1** 했다: 각 가중치 > 진짜 값 이므로 정수 사이클 합 &lt; 0 ⇒ 진짜 이득 > 1 (거짓 양성 0), 각 가중치 &lt; 진짜 + 2.0001 이므로 ln 이득 > 2.0001·L·2^−32 인 차익은 반드시 찾는다. 무작위 4000 표를 큰 정수 곱 전수와 대조해 거짓 양성 0, 한계 밖 놓침 0. 꼬리: "실행 전에 한 번 더?" → 찾은 사이클의 곱을 정확한 정수로 재계산, 수수료·호가 깊이·지연(시세가 그 사이 바뀜) 반영. 꼬리 2: "가장 이득이 큰 사이클?" → BF 는 하나를 줄 뿐, 최대화는 다른(어려운) 문제.

</details>

<details><summary>Q4. 플로이드-워셜에서 k 루프가 왜 가장 바깥이어야 하죠? 행렬을 하나만 써도 되는 이유는?</summary>

DP 상태가 "중간 정점 ⊆ {0..k−1}" 이고 d^(k+1) 은 d^(k) 의 i–k, k–j 값을 재료로 쓴다. k 를 바깥에 두면 단계 k 의 모든 (i, j) 가 단계 k 재료로 계산된다. k 를 안쪽에 두면 d[i][j] 를 한 번에 확정하면서 아직 덜 계산된 d[i][k] 를 쓴다 — 실측 333228 쌍 중 34504 쌍 오답. 제자리 갱신은 단계 k 동안 k 행·열이 안 바뀌어서(d[k][k] = 0) 괜찮다. 꼬리: "음수 사이클이 있으면?" → d[k][k] &lt; 0 이 되어 k 행·열도 바뀌지만, 그런 쌍은 어차피 −∞ 로 표시하므로 정답에 영향 없음. 꼬리 2: "i, k, j 순서는?" → 바깥이 k 이기만 하면 i, j 순서는 자유 — 캐시를 위해 j 를 가장 안쪽(행 순차 접근)에 둔다.

</details>

<details><summary>Q5. INF 는 어떻게 표현하고 오버플로는 어떻게 막았나요? 음수 사이클이 있으면요?</summary>

INF = 타입 최댓값, 피연산자가 INF 면 덧셈을 아예 안 한다(INF/2 같은 "여유 INF" 는 합이 INF 보다 커지는 경우를 따로 다뤄야 해서 덜 깔끔하다). 양수 쪽은 경로 상한 (n−1)·W 를 계산해 타입을 고른다. 음수 사이클이 있으면 FW 값이 4·6^(n−2) 처럼 지수적으로 작아진다(완전 그래프 −1, 측정: int32 n = 14, int64 n = 26 에서 넘침; Hougardy 2010). 그래서 −2^30 에서 포화시키고, 음수 사이클의 영향을 안 받는 쌍은 |값| ≤ (n−1)W &lt; 2^30 이라 절대 포화되지 않음을 증명했다(n ≤ 256, W &lt; 2^22). 벨만-포드는 패스를 n 번으로 끊으니 |dist| ≤ n·m·W 로 int64 면 충분. 꼬리: "포화를 빼면 테스트가 잡나?" → -O2 에선 통과해 버렸고 UBSan 만 잡았다 — sanitizer 를 CI 에 넣는 이유.

</details>

<details><summary>Q6. 모든 쌍 최단 경로를 MCU 에서 n = 256 으로 하겠어요?</summary>

FW 는 int32 d 256 KiB + uint8 next 64 KiB = 320 KiB 로 F4 급 SRAM 에 안 들어가고, 16.7M 갱신 × 13~14 명령(Cortex-M4 asm 확인)이면 168 MHz 에서 1 초 안팎(추정)이다. 하지 않는다. 대신 (1) 행렬은 호스트/게이트웨이가 계산해 각 노드에 **자기 행(다음 홉 n 바이트)** 만 플래시에 넣는다. (2) 토폴로지가 바뀌면 그 노드만 다익스트라 한 번(M06, O((n+m) log n), RAM O(n+m)). (3) 노드들이 협조해 계산해야 하면 거리 벡터(분산 벨만-포드). 꼬리: "d 를 int16 으로?" → 경로 합 상한이 32767 미만이면 128 KiB, 대칭 그래프면 삼각 저장으로 거리만 절반. 꼬리 2: "FW 를 쪼개서 여러 tick 에 나눠 돌리면?" → k 단계 단위로 끊으면 된다(단계 사이 상태는 행렬 자체) — 워치독/마감 안에서 점진 계산 가능.

</details>

<details><summary>Q7. 라우팅 프로토콜이 벨만-포드라던데 무슨 뜻이죠? count-to-infinity 는요?</summary>

거리 벡터 라우팅(RIP 등)은 각 라우터가 "이웃이 알려 준 거리 + 링크 비용" 의 최솟값으로 자기 거리표를 갱신한다 — 벨만-포드의 완화를 라우터마다 비동기로 하는 것이다. 링크가 끊기면 두 이웃이 서로를 경유지로 믿고 거리를 1 씩 올려 가는 count-to-infinity 가 생긴다. RIP 는 메트릭 16 을 무한대로 정해 이 루프를 끊는다(RFC 2453 — 절 번호는 확인 권장), split horizon / poison reverse 로 줄인다. 링크 상태 라우팅(OSPF)은 전체 지도를 받아 각자 다익스트라를 돈다. 이 레슨의 Part C 는 "중앙에서 FW → 각 노드에 행 배포" 로 세 번째 방식이다. 꼬리: "펌웨어 메시(BLE mesh, Thread)는?" → 프로토콜마다 다르니 여기서 단정하지 않는다(확인 권장).

</details>

<details><summary>Q8. 이 코드는 어떻게 테스트했나요? 두 구현이 같은 버그를 공유하면요?</summary>

세 겹이다. (1) 알고리즘과 무관한 기준: n ≤ 7 에서 단순 경로·단순 사이클을 전수 DFS 로 나열(최단 거리, 음수 사이클 위 정점), 환율은 모든 단순 순환을 큰 정수로 정확히 곱함. (2) 서로 다른 알고리즘끼리: FW vs 다익스트라 × n (w ≥ 0), FW vs 벨만-포드 × n (퍼텐셜로 만든 음수 간선 — 음수 사이클 없음이 **구성으로 보장**), FW 검출 vs `BF_ALL` 검출. (3) 결과 자체 검사: 꺼낸 사이클이 닫히고 단순하고 합 &lt; 0, 복원 경로가 실제 간선이고 비용 == d. 그리고 4정점 전수 3^12 개, 손 계산·꼬리·역순 사슬 같은 이름 붙은 사례. 마지막으로 그럴듯한 버그 7개를 일부러 넣어 하네스가 잡는지 봤다 — 6개는 -O2 실행이, 포화 누락은 UBSan 이 잡았다. 꼬리: "랜덤 시드는?" → 고정(xorshift32) — 실패를 재현할 수 있어야 한다.

</details>

## 9. 추가 연습

### 연습 1 — Johnson: 음수 간선 + 희소 그래프의 모든 쌍

벨만-포드(가상 출발점)로 퍼텐셜 h 를 구해 w'(u,v) = w + h(u) − h(v) 로 바꾸고, 정점마다 다익스트라를 돌려 d(s,t) = d'(s,t) − h(s) + h(t) 로 되돌려라. 모든 w' ≥ 0 인지 확인하고 FW 와 대조하라. 음수 사이클이면 −1.

<details><summary>답 보기</summary>

벨만-포드가 끝나면 모든 간선에서 h(v) ≤ h(u) + w — 그대로 w' ≥ 0 이다. 경로 위에서 h 항은 망원급수로 상쇄되어 모든 s→t 경로의 길이가 같은 상수 h(s) − h(t) 만큼 바뀌므로 최단 경로가 보존된다. 시간 O(nm + n·(m log n)) (힙 다익스트라) — 희소 그래프에서 FW 의 n³ 보다 작다. `practice.c` 는 O(V²) 배열 다익스트라를 썼다(검증용). 실제 출력:

```text
ex1 Johnson vs Floyd-Warshall: 400 graphs with negative edges, mismatches=0, negative reweighted edges=0
```

본문 Part C 의 "퍼텐셜로 음수 간선 만들기" 는 이 변환의 거꾸로다. 같은 퍼텐셜 아이디어가 A* 의 일관된 휴리스틱(ALGO-A01)과 최소 비용 흐름의 축소 비용(reduced cost)에도 나온다.

</details>

### 연습 2 — SPFA 와 음수 사이클 검출

FIFO 큐(크기 n 링 버퍼) + in-queue 플래그로 SPFA 를 짜고, `cnt[v]` (dist[v] 를 만든 경로의 간선 수) ≥ n 이면 음수 사이클로 판정하라. 벨만-포드와 dist·검출을 대조하고 완화 수를 비교하라.

<details><summary>답 보기</summary>

in-queue 플래그 덕에 큐에 같은 정점은 하나뿐 → 링 버퍼 n 칸이면 넘치지 않는다(펌웨어에서 고정 크기를 증명할 수 있는 이유). cnt[v] ≥ n 이면 그 경로는 정점 n+1 개 → 정점이 반복 → 그 경로 안에 사이클이 있고, 그 사이클을 돌아 dist 가 줄었으니 음수. 실제 출력:

```text
ex2 SPFA vs Bellman-Ford: 22910 runs, dist mismatch=0, detect mismatch=0, relaxations SPFA/BF = 218473/249824 (0.87)
```

작은 무작위 그래프(n ≤ 30)라 차이가 13% 뿐이다. 조기 종료 벨만-포드가 이미 빠르기 때문이다. 큰 그래프·격자에서 비교해 보고, 최악 O(nm) 단계 논증(5. 풀이)을 스스로 써 보라.

</details>

### 연습 3 — 차분 제약: 센서 트리거 타이밍

이벤트 ADC_TRIG(0), DMA_START(1), SPI_CS(2), IMU_READ(3), TIMESTAMP(4) 의 시각 x 에 대해 "DMA 는 ADC 후 2..5 µs", "CS 는 DMA 후 ≥ 10 µs", "IMU 는 CS 후 1..3 µs", "ADC→IMU ≤ 16 µs", "TIMESTAMP 는 IMU 이후, ADC 후 ≤ 20 µs" 를 만족하는 스케줄을 구하라. 그다음 "ADC→IMU ≤ 12 µs" 를 추가하면 어떤 제약들이 모순인지 보고하라.

<details><summary>답 보기</summary>

x_b − x_a ≤ c 는 간선 a→b 가중치 c. "≥ c" 는 x_a − x_b ≤ −c 로 뒤집는다. 가상 출발점(모든 dist = 0)에서 벨만-포드 → 음수 사이클이 없으면 x = dist 가 해다(모든 간선에서 dist[b] ≤ dist[a] + c). 있으면 그 사이클의 제약들을 더하면 0 ≤ (음수) 라는 모순 = **반드시 하나는 풀어야 하는 제약 묶음**. 실제 출력:

```text
ex3 feasible schedule (us, relative to ADC_TRIG): 0 2 12 13 13
ex3 add 'ADC->IMU <= 12us': infeasible, conflicting constraint cycle of 4, slack sum = -1 us
ex3 random constraint systems: 1500, infeasible 603, errors=0
```

2 + 10 + 1 = 13 > 12 — 사이클 합 −1 µs 가 "1 µs 모자란다" 를 그대로 말해 준다. 펌웨어에서 타이머 채널 오프셋, DMA/SPI 셋업 시간, 링커의 메모리 배치 제약 같은 데 같은 모양이 나온다.

</details>

### 연습 4 — 단일 출발점에서 −∞ 인 목적지 전부

한 출발점에서 최단 거리가 −∞ 인(음수 사이클을 지날 수 있는) 모든 정점을 표시하라. 벨만-포드 n 번째 패스를 끝까지 돌며 완화된 정점들을 모으고, 거기서 BFS 하라. 기준과 대조하라.

<details><summary>답 보기</summary>

(완전성) 닿는 음수 사이클 C 마다 패스 n 에서 C 위의 어떤 정점은 반드시 완화된다 — 아니라면 그 패스 동안 C 위의 dist 가 고정이고 부등식이 동시에 성립해 합 ≥ 0, 모순. 그 정점에서 BFS 하면 C 이후로 닿는 모든 정점이 표시된다. (건전성) 음수 사이클의 영향을 안 받는 정점은 패스 n−1 뒤 dist = δ 이고 그 아래로 못 내려가니 패스 n 에서 완화되지 않는다 — 그리고 영향받는 정점에서만 BFS 가 퍼지므로 잘못 표시되지 않는다. 실제 출력:

```text
ex4 single-source -inf marking: 2000 graphs, marked 21881 (src,v) pairs, mismatches=0
```

라우팅으로 말하면 "어느 목적지가 루프에 오염됐나" 를 찾는 것이고, 본문 FW 의 −∞ 표시의 단일 출발점판이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 7/7 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san   # 7/7, 보고 없음
```

## 10. 핵심 정리

- 벨만-포드: 간선 목록을 최대 n 패스. "r 패스 뒤 dist ≤ 간선 r 개 이하 최소" + "dist 는 실제 걸음 길이" → 음수 사이클이 없으면 n−1 패스에 끝. 변화 없는 패스 = 고정점 = 조기 종료 + "닿는 음수 사이클 없음" 증명.
- n 번째 패스의 완화 ⇔ 닿는 음수 사이클. **첫 완화에서 멈추고** pred 를 **n 번** 따라가 사이클 안으로(꼬리 ≤ n−1), 한 바퀴 모아 뒤집는다. pred 그래프의 사이클은 항상 음수.
- 패스 수는 간선 순서에 달렸다: 역순 사슬 n, 무작위 n = 256 평균 7.6. 시간 예산은 최악 n 패스로.
- 차익 = −ln(rate) 의 음수 사이클. double 은 곱 = 1 인 표에서 거짓 차익. 정수 2^−32 가중치를 **올림 + 1** → 거짓 양성 0, ln 이득 > 2.0001·L·2^−32 면 반드시 검출.
- 플로이드-워셜: k 가 바깥(DP 상태 = 중간 정점 ⊆ {0..k−1}), 제자리 OK, `nx[i][j] = nx[i][k]`. 복원 경로는 꼭 맞는 간선만 따라간다 — 0 사이클 종료는 전수 확인, 증명은 확인 권장 → n 걸음 가드.
- INF 피연산자는 건너뛰기. 음수 사이클이면 FW 값이 4·6^(n−2) 로 폭주(int32 n = 14, int64 n = 26) → −2^30 포화 + "영향 없는 쌍은 |d| ≤ (n−1)W &lt; 2^30" 증명. UBSan 만 잡는 버그가 있다.
- d[k][k] &lt; 0 ⇔ 음수 사이클, (i, j) = −∞ ⇔ ∃k: d[k][k] &lt; 0, i ⇝ k ⇝ j.
- n = 256 행렬 = 320 KiB, M4 에서 ~1 초(추정) → 호스트에서 계산하고 노드엔 자기 행(다음 홉 n 바이트)만.

## 참고자료

- R. Bellman, "On a routing problem", *Quarterly of Applied Mathematics* 16(1), 1958 / L. R. Ford Jr., "Network Flow Theory", RAND P-923, 1956 — 벨만-포드의 원전(서지는 기억에 의존 — 확인 권장).
- R. W. Floyd, "Algorithm 97: Shortest Path", *Communications of the ACM* 5(6), 1962 / S. Warshall, "A theorem on Boolean matrices", *Journal of the ACM* 9(1), 1962 — 서지는 기억에 의존, 확인 권장.
- S. Hougardy, "The Floyd–Warshall algorithm on graphs with negative cycles", *Information Processing Letters* 110(8–9), 2010, pp. 279–281, doi:10.1016/j.ipl.2010.02.001 — 음수 사이클에서 행렬 원소가 지수적으로 커져 오버플로가 날 수 있음. (초록·서지 확인, 본문 정리는 미확인.)
- D. B. Johnson, "Efficient algorithms for shortest paths in sparse networks", *Journal of the ACM* 24(1), 1977 — Johnson 재가중(연습 1). 서지는 기억에 의존 — 확인 권장.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 단일 출발점 최단 경로(벨만-포드, 차분 제약)와 모든 쌍 최단 경로(플로이드-워셜, Johnson) 장.
- cp-algorithms, "Finding a negative cycle in the graph" / "Floyd–Warshall algorithm" (음수 사이클 시 값이 지수적으로 음으로 간다는 경고 포함): <https://cp-algorithms.com/graph/finding-negative-cycle-in-graph.html>, <https://cp-algorithms.com/graph/all-pair-shortest-path-floyd-warshall.html>
- RFC 2453, "RIP Version 2" (G. Malkin, 1998) — 거리 벡터 라우팅, 메트릭 16 = 무한대: <https://www.rfc-editor.org/rfc/rfc2453>
- AlgoMonster 노트 M07 §22 Dijkstra (Don 정리본) — "음수 간선이면 Bellman-Ford, 음수 사이클은 검출만" 메모. 이 레슨은 검출에 더해 **추출**까지 한다. AlgoMonster 노트 M01 §1 Patterns (Don 정리본) — 벨만-포드는 코딩 면접 출제 빈도가 낮다는 통계 메모(그래서 이 레슨은 "다익스트라 다음 꼬리질문" 으로 포지셔닝).
- 관련 레슨: ALGO-M06 (다익스트라, 음수 간선 반례, 최단 경로 증명서), ALGO-M05 (위상 정렬 — DAG 는 한 패스), ALGO-A01 (A*, 일관된 휴리스틱 = 퍼텐셜), ALGO-A10 (SCC — 음수 사이클은 한 SCC 안에 있다), DS-B12 (이진 힙), DS-M11 (indexed PQ), C-A10 (재귀 금지·고정 메모리 스타일).
