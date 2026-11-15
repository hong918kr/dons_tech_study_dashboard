---
id: ALGO-A01
time: 120분
updated: 2026-11-11
---
# ALGO-A01 A* 경로 계획: 휴리스틱과 장애물 팽창

## 학습 목표

- 8방향 점유 격자에서 정수 비용 **10(직선) / 14(대각선)** 로 A* 를 짜고, "14 ≈ 10√2" 라서 결과가 **10/14 거리에 대해 최적**일 뿐 진짜 길이 최적과는 최대 1.0102배 다를 수 있다는 것을 숫자로 말한다.
- **옥타일(octile)** 휴리스틱이 허용 가능(admissible)하고 일관적(consistent)임을 "장애물을 지운 문제의 정확한 거리" 로 증명하고, **맨해튼(Manhattan)** 은 8방향에서 허용 불가라서 다익스트라보다 비싼 경로(58 대신 64)를 내는 지도를 실제로 보인다.
- "닫힌 집합 + 일관성 ⇒ 재개방 없음" 을 줄인 비용(reduced cost) 위의 다익스트라로 증명하고, 확장 노드 집합 ⊆ 다익스트라의 집합임을 무작위 지도 300개로 확인한다. 동점 처리(f 같으면 큰 g 먼저)가 확장 수를 984 → 64 로 줄이는 것을 잰다.
- 모서리 자르기 금지 규칙을 A*·다익스트라·기준 구현에 **똑같이** 적용하고, 로봇 반경 r 칸만큼 장애물을 **유클리드 원판**으로 팽창(행 구간 찍기, 무식한 방법과 대조)한 지도에서 경로를 ASCII 로 그린다.

## 1. 문제

격자 A* 는 로봇·드론 회사 코딩 면접의 "최단 경로 다음 질문" 이다 — BFS(ALGO-M04)와 다익스트라(ALGO-M06)를 답하면 바로 "목표가 하나면 더 빨리?" → "그 휴리스틱이 왜 맞아요?" → "로봇이 점이 아니면?" 으로 이어진다.

창고 AMR(자율 이동 로봇) 펌웨어가 64×64 점유 격자(occupancy grid, 0 = 빈칸, 1 = 장애물)를 들고 있다. M04 와 달리 로봇은 **8방향**으로 움직이고, 대각선 한 칸은 직선보다 길다. 부동소수점 없이 비용을 정수로 쓴다:

| 이동 | 실제 길이 | 이 레슨의 비용 | 상대 오차 |
|---|---|---|---|
| 직선 (N S W E) | 1 | 10 | 0 |
| 대각선 | √2 = 1.41421… | 14 | 14/14.1421 − 1 = **−1.005%** |

그래서 이 레슨의 "최적" 은 언제나 **10/14 거리에 대해 최적**이다. 어떤 격자 경로 P 든 `cost14(P) / 10 ≤ len(P) ≤ (10√2/14) · cost14(P) / 10` 이므로, 10/14 최적 경로 P* 와 진짜 최적 격자 경로 Q 에 대해 `len(P*) ≤ (10√2/14)·cost14(P*)/10 ≤ (10√2/14)·cost14(Q)/10 ≤ (10√2/14)·len(Q)` — 최대 **1.0102배** 길다. 무작위 지도에서는 이 차이가 거의 안 나타나고(연습 1: 145개 중 0개), 일부러 만든 지도에서 1.00167배가 나온다. 또 8방향 격자 경로 자체는 직선보다 최대 √(4−2√2) = 1.0824배 길다(22.5° 방향) — 이건 휴리스틱이 아니라 격자의 한계이고 any-angle 계획(Theta* 등)의 몫이다.

**모서리 자르기 금지 (corner-cutting rule).** 대각선 (r,c)→(r+dr,c+dc) 는 스쳐 가는 두 직교 이웃 (r+dr,c) 와 (r,c+dc) 가 **둘 다** 빈칸일 때만 허용한다. 하나만 막혀도 금지(로봇 몸체가 모서리를 긁는다), 둘 다 막힌 "비집고 지나가기(squeeze)" 는 당연히 금지. 이 규칙은 A*, 다익스트라, 기준 구현, 경로 검증기에 **똑같이** 들어가야 비교가 의미 있다. 규칙은 대칭이다: u→v 가 되면 v→u 도 된다(같은 두 칸을 본다) — 그래서 목표에서 거꾸로 돌린 거리 = 목표까지의 진짜 거리 h* 로 쓸 수 있다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">① 8방향 비용 10/14   ② 모서리 자르기 금지   ③ 반경 r = 2 팽창 (유클리드 원판)</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="45" height="45" class="f-ok"/><rect x="85" y="50" width="45" height="45" class="f-ok"/><rect x="130" y="50" width="45" height="45" class="f-ok"/>
    <rect x="40" y="95" width="45" height="45" class="f-ok"/><rect x="85" y="95" width="45" height="45" class="f-acc"/><rect x="130" y="95" width="45" height="45" class="f-ok"/>
    <rect x="40" y="140" width="45" height="45" class="f-ok"/><rect x="85" y="140" width="45" height="45" class="f-ok"/><rect x="130" y="140" width="45" height="45" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="62" y="77">14</text><text x="107" y="77">10</text><text x="152" y="77">14</text>
    <text x="62" y="122">10</text><text x="107" y="122">u</text><text x="152" y="122">10</text>
    <text x="62" y="167">14</text><text x="107" y="167">10</text><text x="152" y="167">14</text>
  </g>
  <text x="20" y="210" fill="currentColor">14 = 10√2 의 정수 근사</text>
  <text x="20" y="228" fill="currentColor">(−1.0%: 대각선이 약간 싸다)</text>
  <g stroke="currentColor">
    <rect x="250" y="60" width="35" height="35" class="f-ok"/><rect x="285" y="60" width="35" height="35" class="f-ok"/>
    <rect x="250" y="95" width="35" height="35" class="f-ok"/><rect x="285" y="95" width="35" height="35" class="f-ok"/>
    <rect x="345" y="60" width="35" height="35" class="f-ok"/><rect x="380" y="60" width="35" height="35" fill="currentColor"/>
    <rect x="345" y="95" width="35" height="35" class="f-ok"/><rect x="380" y="95" width="35" height="35" class="f-ok"/>
    <rect x="440" y="60" width="35" height="35" class="f-ok"/><rect x="475" y="60" width="35" height="35" fill="currentColor"/>
    <rect x="440" y="95" width="35" height="35" fill="currentColor"/><rect x="475" y="95" width="35" height="35" class="f-ok"/>
  </g>
  <g stroke="currentColor" stroke-width="2.5" fill="none">
    <path d="M267 112 L302 78"/><path d="M362 112 L397 78" stroke-dasharray="4 3"/><path d="M457 78 L492 112" stroke-dasharray="4 3"/>
  </g>
  <text x="252" y="155" fill="currentColor">허용</text>
  <text x="340" y="155" fill="currentColor">금지: 하나 막힘</text>
  <text x="440" y="155" fill="currentColor">금지: 비집기</text>
  <text x="250" y="190" fill="currentColor">대각선 = 두 직교 이웃이 모두 빈칸일 때만.</text>
  <text x="250" y="208" fill="currentColor">A*·기준·검증기 모두 같은 규칙.</text>
  <g stroke="currentColor">
    <rect x="560" y="50" width="30" height="30" class="f-mut"/><rect x="590" y="50" width="30" height="30" class="f-mut"/><rect x="620" y="50" width="30" height="30" class="f-warn"/><rect x="650" y="50" width="30" height="30" class="f-mut"/><rect x="680" y="50" width="30" height="30" class="f-mut"/>
    <rect x="560" y="80" width="30" height="30" class="f-mut"/><rect x="590" y="80" width="30" height="30" class="f-warn"/><rect x="620" y="80" width="30" height="30" class="f-warn"/><rect x="650" y="80" width="30" height="30" class="f-warn"/><rect x="680" y="80" width="30" height="30" class="f-mut"/>
    <rect x="560" y="110" width="30" height="30" class="f-warn"/><rect x="590" y="110" width="30" height="30" class="f-warn"/><rect x="620" y="110" width="30" height="30" fill="currentColor"/><rect x="650" y="110" width="30" height="30" class="f-warn"/><rect x="680" y="110" width="30" height="30" class="f-warn"/>
    <rect x="560" y="140" width="30" height="30" class="f-mut"/><rect x="590" y="140" width="30" height="30" class="f-warn"/><rect x="620" y="140" width="30" height="30" class="f-warn"/><rect x="650" y="140" width="30" height="30" class="f-warn"/><rect x="680" y="140" width="30" height="30" class="f-mut"/>
    <rect x="560" y="170" width="30" height="30" class="f-mut"/><rect x="590" y="170" width="30" height="30" class="f-mut"/><rect x="620" y="170" width="30" height="30" class="f-warn"/><rect x="650" y="170" width="30" height="30" class="f-mut"/><rect x="680" y="170" width="30" height="30" class="f-mut"/>
  </g>
  <circle cx="635" cy="125" r="60" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="540" y="222" fill="currentColor">dx² + dy² ≤ r² → 13칸</text>
  <text x="540" y="240" fill="currentColor">행별 반폭 = 0, 1, 2, 1, 0</text>
  <text x="10" y="262" fill="currentColor">검정 = 장애물, 경고색 = 팽창으로 막힌 칸, 회색 = 원판 밖(사각 창으로 찍으면 틀리게 막히는 칸)</text>
</svg>
```

이 레슨의 범위: 격자 위 A* 와 반경 팽창까지. 거리에 따라 비용이 줄어드는 **계층 코스트맵**(inflation cost decay)은 AUTON-B06, 차량의 최소 회전 반경을 따르는 **Hybrid A\*** 는 AUTON-M06 이 다룬다.

### Part A — 이동 규칙·휴리스틱·A* 와 다익스트라 대조

- `move_ok(g, r, c, k)` — 위 규칙. `heur(h, r, c, gr, gc)` — `H_ZERO` / `H_MANHATTAN = 10(dx+dy)` / `H_OCTILE = 10·max + 4·min`.
- `astar(...)` — indexed min-PQ(decrease-key, DS-M11), 목표를 **꺼낼 때** 종료, 닫힌 칸은 다시 열지 않음(재개방 없음). 다익스트라 = `H_ZERO`.
- 검사: 손 계산 사례, 빈 8×8 의 모든 (시작, 목표) 4096쌍에서 A* = 옥타일 = 기준, 무작위 지도 300개(흩뿌림/벽 조각, 도달 불가 56개 포함)에서 비용 = 기준 구현("바뀌지 않을 때까지 완화"), 경로 검증, **A* 의 닫힌 칸 ⊆ 다익스트라의 닫힌 칸**, 허용성·일관성 직접 검사.

### Part B — 맨해튼 반례·동점 처리·재개방

- 3×6 손 지도에서 다익스트라 58, 옥타일 58, 맨해튼 **64** (동점 처리 3가지 모두).
- 무작위 지도에서 맨해튼의 비최적 빈도·최악 비율, 이론 상한 10/7 확인.
- `node_before` — f 동점에서 `TB_HIGH_G` / `TB_LOW_G` / `TB_ID`. 빈 64×64 에서 확장 수 비교.
- 일관적 휴리스틱에서는 `closed_improve`(닫힌 칸을 더 싸게 갈 간선 발견) = 0, 맨해튼에서는 > 0.

### Part C — 로봇 반경 팽창과 창고 지도

- `inflate(src, dst, r)` — 유클리드 원판 팽창. 반폭 표 + 행 구간 `memset`. 칸마다 창을 훑는 무식한 방법과 (지도, r) 840쌍 대조.
- 창고 지도(19×41, 두께 1 벽에 좁은 문 3칸·넓은 문 7칸): r = 0..5 에서 비용·지나는 문·확장 수, r=2 경로 ASCII, 경로 칸의 여유 거리(모든 장애물까지 거리² > r²) 검사.

## 2. Problem (English)

You are given an occupancy grid (≤ 64×64, `0` free, `1` blocked). A robot moves in 8 directions; a straight step costs 10, a diagonal step costs 14. A diagonal step is allowed only if **both** orthogonally adjacent cells it passes are free (no corner cutting, no squeezing between two obstacles).

```c
bool    move_ok(const grid_t *g, int r, int c, int k);                 /* k = 0..7 */
int32_t heur(heur_t h, int r, int c, int gr, int gc);                  /* H_ZERO, H_MANHATTAN, H_OCTILE */
int     astar(const grid_t *g, int sr, int sc, int gr, int gc, heur_t h, tie_t tb, astar_ws_t *w);
                                                                        /* min cost or -1; w->parent gives the path */
void    inflate(const grid_t *src, grid_t *dst, int r);                /* Euclidean disk of radius r cells */
```

1. Implement A* with the octile heuristic and show it returns the same cost as Dijkstra on random maps while expanding fewer nodes. Prove octile is admissible and consistent for this cost model.
2. Show that the Manhattan heuristic is **not** admissible here: give a map where Manhattan-A* returns a costlier path than Dijkstra.
3. The robot has radius `r` cells. Inflate obstacles (a cell is blocked if its center is within distance `r` of an obstacle center), plan on the inflated map, and print the path.

Constraints: no dynamic allocation in the planner; worst case O(V log V) with V = rows·cols; deterministic output (break ties explicitly).

```text
.....G      dijkstra 58, A*-octile 58, A*-manhattan 64
..#...      S = (2,0), G = (0,5), one obstacle at (1,2)
S.....
```

Follow-ups: why stop when the goal is *popped*, not pushed? Does A* ever re-open a closed node? What does tie-breaking change? How much RAM on a Cortex-M?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 군데(`move_ok`, `heur`, `node_before`, `astar` 본문, `inflate` 본문). 힙 연산(`pq_*`), `ws_reset`, `path_extract`, `render`, 기준 구현·검증기는 주어진다. 스텁은 "이동 불가 / h = 0 / -1" 을 돌려줘 멈추지 않는다. 실제 출력(중간 생략):

```text
FAIL starter.c:304  astar(g, 0, 0, 0, 0, H_OCTILE, TB_HIGH_G, &W1) == 0 && W1.expanded == 1
FAIL starter.c:320  okmask == 0xCE
…
Part A random: 300 maps (56 unreachable), cost mismatches=976, bad paths=0, …
Part C inflate vs brute: 840 (map, r) cases, mismatches=696
…
8/29 passed
```

(exit 1. 통과한 8개는 "도달 불가면 -1" 처럼 스텁이 우연히 맞히는 것과, h ≡ 0 이라 허용성 검사가 공짜로 통과하는 것.)

## 4. 힌트

<details><summary>힌트 1 — 옥타일은 "장애물이 없을 때의 정확한 비용"</summary>

빈 격자에서 (dx, dy), dx ≥ dy 만큼 가려면 대각선 dy 번 + 직선 dx−dy 번이 최선: 14·dy + 10·(dx−dy) = 10·dx + 4·dy. 장애물은 간선을 **지우기만** 하므로 진짜 비용은 이보다 작을 수 없다.

</details>

<details><summary>힌트 2 — 목표는 꺼낼 때 끝낸다</summary>

넣을 때(처음 발견할 때) 끝내면 나중에 더 싼 경로로 g 가 줄어들 기회를 버린다. 다익스트라의 "pop = 확정" 과 같은 이유. 실측: 이 실수는 무작위 1200번 실행 중 **1번**만 비용이 틀린다 — 그래서 무작위 비용 대조만으로는 잘 안 잡힌다(7. 실수 2).

</details>

<details><summary>힌트 3 — 닫힌 칸에서 더 싼 간선을 보면?</summary>

일관적 h 에서는 일어나지 않는다(5. 풀이의 증명). 그래서 "닫힌 칸이면 건너뛴다" 가 맞다. 대신 그 일이 일어나면 `closed_improve++` 로 세어 두면 휴리스틱이 일관적이지 않다는 **경보**가 된다(맨해튼: 300개 중 22개 지도에서 발생).

</details>

<details><summary>힌트 4 — 동점 처리: 목표에 가까운 쪽 먼저</summary>

빈 지도에서는 최적 경로가 수없이 많고 그 위의 칸이 모두 f = C* 다. f 가 같으면 g 가 큰(= h 가 작은) 칸을 먼저 꺼내면 한 경로를 따라 곧장 내려간다. 비교 함수 하나만 바꾸면 된다.

</details>

<details><summary>힌트 5 — 팽창은 행 구간으로</summary>

원판은 행마다 연속 구간이다. |dy| = 0..r 에 대해 hw = ⌊√(r² − dy²)⌋ 를 **정수로**(`while ((x+1)² ≤ rem) x++`) 한 번 구해 두고, 장애물마다 2r+1 개 행에 `memset`. 지도 밖으로 나가는 구간은 자른다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 4방향/8방향? 대각선 비용(√2 를 정수 14 로?) 모서리 자르기 허용? 지도 최대 크기·RAM? 목표 하나? 경로까지 필요? 로봇 크기(점인가)? 시작이 장애물 근처면? |
| 2. 무식한 방법 | 다익스트라 O(V log V) — 목표 방향을 모르고 사방으로 퍼진다. "바뀌지 않을 때까지 완화" O(V · 스윕 수) 는 **테스트 기준**으로 남긴다 |
| 3. 더 나은 생각 + 불변식 | A*: 키 = g + h. h 가 일관적이면 줄인 비용 c − h(u) + h(v) ≥ 0 위의 다익스트라 → pop = 확정, 재개방 없음. 옥타일 = 장애물 지운 문제의 정확한 거리 |
| 4. 코드 | indexed PQ, `closed[]`, 목표 pop 에서 종료, 8방향 + 모서리 규칙, parent 로 복원. 동점은 큰 g |
| 5. 소리 내어 말할 테스트 | 시작 = 목표, 목표가 막힘/지도 밖, 2×2 모서리, 빈 지도 = 옥타일, 무작위 vs 기준, 맨해튼 반례, 팽창 후 시작이 막힘 |
| 6. 복잡도 | 시간 O(V log V) (간선 ≤ 8V), 추가 메모리 O(V) — 이 구현 15 B/칸 = 61 KB, 압축하면 7~9 B/칸. 스택 O(1) |

### Part A — 옥타일이 허용 가능하고 일관적인 이유

**허용성(admissible)**: 모든 칸 u 에서 h(u) ≤ h*(u) (진짜 남은 비용). **일관성(consistent)**: 모든 합법 이동 u→v 에서 h(u) ≤ c(u,v) + h(v), 그리고 h(목표) = 0. 일관성 ⇒ 허용성(목표까지 최단 경로를 따라 부등식을 더하면 된다).

**보조정리 (빈 격자의 정확한 거리).** 장애물 없는 8방향 10/14 격자에서 변위 (dx, dy), dx ≥ dy 인 두 칸의 최단 비용은 `oct = 10·dx + 4·dy` 다.
- 달성: 대각선 dy 번 + 직선 dx − dy 번 = 14dy + 10(dx − dy).
- 하한: 경로가 대각선 b 번, 직선 a 번이라 하자. 대각선은 두 좌표를 1씩, 직선은 하나를 1 바꾸므로 `a + b ≥ dx` (큰 축) 이고 `a + 2b ≥ dx + dy` (두 축 합). 그러면 `10a + 14b = 6(a + b) + 4(a + 2b) ≥ 6dx + 4(dx + dy) = 10dx + 4dy`. ∎

**정리.** 옥타일 h(u) = oct(u, 목표) 는 일관적이다(따라서 허용 가능).
- oct 는 빈 격자의 최단 거리이므로 거리 공리(삼각 부등식)를 만족한다: `oct(u,t) ≤ oct(u,v) + oct(v,t)`.
- 합법 이동 u→v 에 대해 `oct(u,v)` 는 정확히 10 또는 14 = c(u,v).
- 그러므로 `h(u) ≤ c(u,v) + h(v)`. 장애물과 모서리 규칙은 간선을 **지우기만** 하므로 증명에 쓰이지 않는다 — "제약을 푼 문제(relaxed problem)의 정확한 해는 일관적인 휴리스틱" 이라는 일반 원리의 한 예. ∎

하네스는 이 증명을 믿지 않고 직접 센다: 무작위 지도 300개의 모든 도달 가능 칸에서 h ≤ h*(목표에서 거꾸로 돌린 기준 거리), 모든 합법 간선에서 일관성. 결과 0/0. 맨해튼은 h > h* 인 칸 56809개, 비일관 간선 57532개.

### Part A — 닫힌 집합 + 일관성 ⇒ 재개방 없음

줄인 비용 `c'(u,v) = c(u,v) − h(u) + h(v)` 를 정의하자. 일관성 ⇔ 모든 간선에서 c' ≥ 0. 경로 s→…→v 의 c' 합은 망원합(telescoping)으로 `g(v) − h(s) + h(v)` 이다. 그러므로

- A* 의 키 `f(v) = g(v) + h(v)` = (c' 위의 거리) + h(s). h(s) 는 상수 → **A* 가 꺼내는 순서 = c' 그래프 위 다익스트라가 꺼내는 순서**.
- c' ≥ 0 이므로 다익스트라의 정리(ALGO-M06 Part A: "pop 된 정점의 dist 는 최종")가 그대로 성립: v 를 꺼내는 순간 g(v) = g*(v).
- 꺼낸 칸의 g 가 이미 최소이니 그 칸을 더 싸게 가는 간선은 없다 → `closed_improve` = 0, **재개방이 필요 없다**. 하네스: 무작위 지도에서 다익스트라 + 옥타일 3가지 = 1200번 실행, closed_improve 합 0.
- 덤: 꺼내는 f 값은 비내림차순이고, 목표는 f = C* 로 꺼내진다.

**A* 가 닫는 칸 ⊆ 다익스트라가 닫는 칸 (옥타일).** A* 가 닫는 칸 n 은 f(n) = g*(n) + h(n) ≤ C*. h ≥ 0 이므로 g*(n) ≤ C*. g*(n) < C* 이면 다익스트라도 목표(키 C*)보다 먼저 n 을 꺼낸다. g*(n) = C* 이면 h(n) = 0 인데 옥타일은 목표에서만 0 → n = 목표. 동점 처리와 무관하게 성립하고, 하네스가 지도마다 칸 단위로 확인한다(위반 0). 그래서 "A* 는 다익스트라보다 **절대** 더 많이 확장하지 않는다(일관적 + h = 0 은 목표에서만)" 는 측정이 아니라 정리다.

### Part A — 손 계산: 3×6 지도에서 세 가지 탐색

지도는 2. Problem 의 예(S=(2,0), G=(0,5), 장애물 (1,2)). 꺼낸 순서 (이 레슨 trace 도구의 실제 출력을 표로, `g/f`):

| # | 다익스트라 (h=0) | A* 옥타일, 큰 g | A* 맨해튼, 큰 g |
|---|---|---|---|
| 1 | (2,0) 0 | (2,0) 0/58 | (2,0) 0/70 |
| 2 | (1,0) 10 | (1,1) 14/58 | (1,1) 14/**64** |
| 3 | (2,1) 10 | (2,1) 10/58 | (0,1) 24/64 |
| 4 | (1,1) 14 | (2,2) 20/58 | (0,2) 34/64 |
| 5 | (0,0) 20 | (2,3) 30/58 | (0,3) 44/64 |
| 6 | (2,2) 20 | (1,4) 44/58 | (0,4) 54/64 |
| 7 | (0,1) 24 | **(0,5) 58/58 목표** | **(0,5) 64/64 목표** |
| … | … 17번째에 (0,5) 58 | | |

다익스트라는 빈칸 17개를 전부 닫고 58. 옥타일은 7개만 닫고 58. (1,1) 은 f = 58 로 보이지만(h=44 < h*=50, 허용 범위의 낙관) 막다른 길이라 다음에 (2,1) 로 돌아간다 — 허용 가능한 h 가 "너무 낙관적" 이어도 정답은 지킨다. 맨해튼은 2번에서 (1,1) 을 f = 64 로 꺼내는데, 진짜 최적 경로의 첫 칸 (2,1) 은 f = 10 + **60** = 70 으로 보여서 끝까지 안 꺼낸다 → 64.

### Part A — 기준 구현과 무작위 대조

기준 `ref_dist` 는 힙·휴리스틱이 없는 "바뀌지 않을 때까지 완화"(정방향·역방향 스윕 번갈아)이고, 이동 규칙도 `ref_step` 으로 **따로** 다시 썼다(`move_ok` 의 버그를 같이 갖지 않도록). 경로 검증기 `path_ok` 도 `ref_step` 으로 걸음마다 합법성과 비용 합을 본다. 실제 출력:

```text
Part A random: 300 maps (56 unreachable), cost mismatches=0, bad paths=0, closed(A*) not in closed(Dijkstra)=0, closed-improve with consistent h=0
  octile: admissibility violations=0, consistency violations=0 | manhattan: h > h* at 56809 cells, inconsistent edges=57532
  expanded total: dijkstra=52048  octile/high-g=18863  octile/low-g=21312  octile/id=20369  manhattan/high-g=17179
  manhattan: suboptimal on 15 maps, worst cost ratio 1.0985 (bound 10/7 = 1.4286), maps with closed-improve=22
```

300개 지도(5..40 × 5..40, 흩뿌림 밀도 0..40% 와 벽 조각 반반)에서 옥타일 A* 는 다익스트라의 **36%** 만 확장한다(18863 / 52048). 시작·목표가 무작위라 가까운 질의도 섞여 있다 — 멀리 떨어진 질의는 아래 벤치마크.

### Part B — 맨해튼은 왜 8방향에서 허용 불가인가

맨해튼 − 옥타일 = 10(dx+dy) − (10·max + 4·min) = **6·min(dx, dy)**. 즉 "남은 대각선 하나당 6 벌점". 대각선 한 걸음은 g 를 14 올리고 맨해튼 h 를 20 내리므로 f 가 **6 줄어든다** — A* 는 대각선을 빨리 쓰는 칸을 좋아하고, 대각선을 나중에 쓰려고 아껴 둔 칸은 6·(남은 대각선 수)만큼 비싸 보인다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">맨해튼 A* 가 64 를 내는 지도 (최적 58) — 대각선을 일찍 쓴 길이 싸 보인다</text>
  <g stroke="currentColor">
    <rect x="30" y="40" width="300" height="150" class="f-ok"/>
    <rect x="130" y="90" width="50" height="50" fill="currentColor"/>
  </g>
  <g stroke="currentColor" stroke-width="0.6" fill="none">
    <path d="M80 40 V190 M130 40 V190 M180 40 V190 M230 40 V190 M280 40 V190 M30 90 H330 M30 140 H330"/>
  </g>
  <g stroke="currentColor" stroke-width="3" fill="none" stroke-dasharray="7 5">
    <path d="M55 165 L105 115 L105 65 L305 65"/>
  </g>
  <g stroke="currentColor" stroke-width="3" fill="none">
    <path d="M55 165 L205 165 L255 115 L305 65"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="38" y="178">S</text><text x="312" y="58">G</text>
    <text x="84" y="104">f=64</text><text x="84" y="186">f=70</text>
  </g>
  <text x="30" y="212" fill="currentColor">점선: 맨해튼 경로 14+10·5 = 64.  실선: 최적 10·3+14·2 = 58.</text>
  <text x="30" y="232" fill="currentColor">(1,1): g 14 + h 50 (h* 50) = 64.   (2,1): g 10 + h 60 (h* 48) = 70 → 끝까지 안 꺼냄.</text>
  <text x="370" y="60" fill="currentColor">맨해튼 − 옥타일 = 6·min(dx, dy)</text>
  <text x="370" y="80" fill="currentColor">= 남은 대각선 하나당 6 과대평가</text>
  <text x="370" y="110" fill="currentColor">대각선 한 걸음: g +14, h −20 → f −6</text>
  <text x="370" y="130" fill="currentColor">직선 한 걸음(목표 쪽): g +10, h −10 → f 그대로</text>
  <text x="370" y="160" fill="currentColor">→ 대각선을 아껴 둔 칸일수록 비싸 보인다.</text>
  <text x="370" y="180" fill="currentColor">닫힌 칸 재개방이 없으니 64 로 끝난다.</text>
</svg>
```

이 지도는 가장 작은 반례를 무작위 탐색으로 찾은 것이고(장애물 1개), 동점 처리 3가지 모두 64 다(하네스 실제 출력 `dijkstra=58 octile=58 manhattan=64/64/64`). 더 작은 4×3 반례도 있었지만 "작은 g 우선" 으로 바꾸면 58 을 찾아서 버렸다 — 반례가 동점 처리에 기대면 면접에서 반박당한다.

**재개방하면 고쳐지나?** 아니다. 맨해튼 A* 는 목표를 f = 64 로 꺼낸 순간 끝난다. 그때 열린 집합의 (2,1) 은 f = 70 이라 재개방 여부와 상관없이 먼저 꺼낼 이유가 없다. 허용 불가 h 의 문제는 재개방이 아니라 **종료 조건**(f(목표) ≤ 모든 열린 f 가 최적을 보장하지 못함)이다.

### Part B — 그래도 맨해튼(또는 부풀린 h)을 쓰는 경우

맨해튼 = (10/7) · h', h' = 7(dx+dy). h' 는 일관적이다 — 직선 한 걸음에 h' 가 많아야 7 ≤ 10, 대각선에 많아야 14 ≤ 14 변한다. 그러니 맨해튼 A* 는 **가중 A\*(weighted A\*, f = g + w·h, w = 10/7)** 다. 일관적 h 에 w 를 곱한 가중 A* 는 재개방 없이도 비용 ≤ w · C* 를 보장한다는 결과가 있다(Likhachev·Gordon·Thrun 의 ARA* 논문 — 정리의 정확한 형태는 확인 권장). 측정은 이 상한 안이다:

| 실험 | 비최적 비율 | 최악 비용 비율 | 상한 |
|---|---|---|---|
| 무작위 300개 (solution.c) | 15/300 | 1.0985 | 10/7 = 1.4286 |
| 64×64 멀리 떨어진 질의, 흩뿌림 10/20/30%, 벽 (demo) | 80/100, 86/99, 63/90, 9/62 | 1.0951 / 1.1194 / 1.0836 / 1.1019 | 1.4286 |
| 옥타일 × w = 1.25 / 1.5 / 2 / 3 (연습 3) | 53 / 83 / 95 / 97 (194개 중) | 1.0986 / 1.1405 / 1.3806 / 1.3806 | w |

대가로 얻는 것: 64×64 흩뿌림 20% 에서 확장 274 vs 옥타일 657 vs 다익스트라 3007 (demo). 그래서 **"최적이 아니어도 되고 빨리 답해야 할 때"**(게임 NPC, 재계획 주기가 빠듯한 로봇, 먼저 대충 경로를 낸 뒤 시간이 남으면 w 를 줄여 다시 — ARA* 의 아이디어) 일부러 쓴다. 극단(g 무시, h 만) = 탐욕적 최선 우선(greedy best-first): 빠르지만 상한이 없다. 면접 답: "맨해튼은 4방향용이고, 8방향에서 쓰면 w = 10/7 가중 A* 와 같다 — 최악 43% 비싼 경로를 감수하고 빨리 가려는 선택" .

### Part B — 동점 처리: 같은 비용, 다른 확장 수

빈 64×64, (0,0) → (63,40). 최적 경로가 C(63, 40) ≈ 9.4×10^16 개(대각선 40번과 직선 23번의 순서 — 하한 증명의 등호 조건 b = dy, a = dx − dy 에서) 그 위 칸이 모두 f = C* = 790. 실제 출력:

```text
Part B ties, empty 64x64 (0,0)->(63,40): cost 790/790/790/790  expanded high-g=64 low-g=984 id=984 dijkstra=3925  (pushes 270/1167/1167/3970)
```

- **큰 g 우선**: 64 = 경로 위 칸 수(63걸음 + 1). 한 경로를 따라 곧장 내려간다.
- **작은 g 우선**: 984 — f = C* 띠 안에서 시작 쪽부터 다 닫는다(최악의 동점 처리).
- **칸 번호 순**: 이 질의에서는 984 로 작은 g 와 같다 — 시작이 (0,0) 이고 칸 번호가 행 우선이라 "번호 작은 칸 = 시작에 가까운 칸" 이 되어 우연히 작은 g 우선처럼 행동한다. 아래 그림처럼 시작이 왼쪽 아래면 번호 순도 18 로 큰 g 우선과 같아진다. 번호 순은 정책이 아니라 우연이다.

```svg
<svg viewBox="0 0 740 205" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 지도·같은 비용 198, 닫힌 칸 수만 다르다 (astar 실제 출력)</text>
  <text x="10" y="40" fill="currentColor">dijkstra — 닫힌 칸 184</text>
  <rect x="10" y="50" width="198" height="11" class="f-mut"/>
  <rect x="10" y="61" width="99" height="11" class="f-mut"/><rect x="109" y="61" width="110" height="11" class="f-acc"/>
  <rect x="10" y="72" width="88" height="11" class="f-mut"/><rect x="98" y="72" width="11" height="11" class="f-acc"/><rect x="109" y="72" width="11" height="11" class="f-mut"/><rect x="120" y="72" width="11" height="11" fill="currentColor"/><rect x="131" y="72" width="77" height="11" class="f-mut"/>
  <rect x="10" y="83" width="77" height="11" class="f-mut"/><rect x="87" y="83" width="11" height="11" class="f-acc"/><rect x="98" y="83" width="22" height="11" class="f-mut"/><rect x="120" y="83" width="11" height="11" fill="currentColor"/><rect x="131" y="83" width="77" height="11" class="f-mut"/>
  <rect x="10" y="94" width="66" height="11" class="f-mut"/><rect x="76" y="94" width="11" height="11" class="f-acc"/><rect x="87" y="94" width="33" height="11" class="f-mut"/><rect x="120" y="94" width="11" height="11" fill="currentColor"/><rect x="131" y="94" width="88" height="11" class="f-mut"/>
  <rect x="10" y="105" width="55" height="11" class="f-mut"/><rect x="65" y="105" width="11" height="11" class="f-acc"/><rect x="76" y="105" width="44" height="11" class="f-mut"/><rect x="120" y="105" width="11" height="11" fill="currentColor"/><rect x="131" y="105" width="88" height="11" class="f-mut"/>
  <rect x="10" y="116" width="44" height="11" class="f-mut"/><rect x="54" y="116" width="11" height="11" class="f-acc"/><rect x="65" y="116" width="55" height="11" class="f-mut"/><rect x="120" y="116" width="11" height="11" fill="currentColor"/><rect x="131" y="116" width="99" height="11" class="f-mut"/>
  <rect x="10" y="127" width="33" height="11" class="f-mut"/><rect x="43" y="127" width="11" height="11" class="f-acc"/><rect x="54" y="127" width="66" height="11" class="f-mut"/><rect x="120" y="127" width="11" height="11" fill="currentColor"/><rect x="131" y="127" width="99" height="11" class="f-mut"/>
  <rect x="10" y="138" width="11" height="11" class="f-mut"/><rect x="21" y="138" width="22" height="11" class="f-acc"/><rect x="43" y="138" width="77" height="11" class="f-mut"/><rect x="120" y="138" width="11" height="11" fill="currentColor"/><rect x="131" y="138" width="99" height="11" class="f-mut"/>
  <rect x="10" y="149" width="220" height="11" class="f-mut"/>
  <rect x="10" y="50" width="220" height="110" fill="none" stroke="currentColor"/><text x="23" y="147" fill="currentColor" font-size="12">S</text><text x="210" y="70" fill="currentColor" font-size="12">G</text>
  <text x="255" y="40" fill="currentColor">octile low-g — 닫힌 칸 53</text>
  <rect x="343" y="61" width="11" height="11" class="f-mut"/><rect x="354" y="61" width="110" height="11" class="f-acc"/>
  <rect x="332" y="72" width="11" height="11" class="f-mut"/><rect x="343" y="72" width="11" height="11" class="f-acc"/><rect x="354" y="72" width="11" height="11" class="f-mut"/><rect x="365" y="72" width="11" height="11" fill="currentColor"/>
  <rect x="321" y="83" width="11" height="11" class="f-mut"/><rect x="332" y="83" width="11" height="11" class="f-acc"/><rect x="343" y="83" width="22" height="11" class="f-mut"/><rect x="365" y="83" width="11" height="11" fill="currentColor"/>
  <rect x="310" y="94" width="11" height="11" class="f-mut"/><rect x="321" y="94" width="11" height="11" class="f-acc"/><rect x="332" y="94" width="33" height="11" class="f-mut"/><rect x="365" y="94" width="11" height="11" fill="currentColor"/>
  <rect x="299" y="105" width="11" height="11" class="f-mut"/><rect x="310" y="105" width="11" height="11" class="f-acc"/><rect x="321" y="105" width="44" height="11" class="f-mut"/><rect x="365" y="105" width="11" height="11" fill="currentColor"/>
  <rect x="288" y="116" width="11" height="11" class="f-mut"/><rect x="299" y="116" width="11" height="11" class="f-acc"/><rect x="310" y="116" width="55" height="11" class="f-mut"/><rect x="365" y="116" width="11" height="11" fill="currentColor"/>
  <rect x="277" y="127" width="11" height="11" class="f-mut"/><rect x="288" y="127" width="11" height="11" class="f-acc"/><rect x="299" y="127" width="66" height="11" class="f-mut"/><rect x="365" y="127" width="11" height="11" fill="currentColor"/>
  <rect x="266" y="138" width="22" height="11" class="f-acc"/><rect x="288" y="138" width="77" height="11" class="f-mut"/><rect x="365" y="138" width="11" height="11" fill="currentColor"/>
  <rect x="255" y="50" width="220" height="110" fill="none" stroke="currentColor"/><text x="268" y="147" fill="currentColor" font-size="12">S</text><text x="455" y="70" fill="currentColor" font-size="12">G</text>
  <text x="500" y="40" fill="currentColor">octile high-g — 닫힌 칸 18</text>
  <rect x="588" y="61" width="121" height="11" class="f-acc"/>
  <rect x="577" y="72" width="11" height="11" class="f-acc"/><rect x="610" y="72" width="11" height="11" fill="currentColor"/>
  <rect x="566" y="83" width="11" height="11" class="f-acc"/><rect x="610" y="83" width="11" height="11" fill="currentColor"/>
  <rect x="555" y="94" width="11" height="11" class="f-acc"/><rect x="610" y="94" width="11" height="11" fill="currentColor"/>
  <rect x="544" y="105" width="11" height="11" class="f-acc"/><rect x="610" y="105" width="11" height="11" fill="currentColor"/>
  <rect x="533" y="116" width="11" height="11" class="f-acc"/><rect x="610" y="116" width="11" height="11" fill="currentColor"/>
  <rect x="522" y="127" width="11" height="11" class="f-acc"/><rect x="610" y="127" width="11" height="11" fill="currentColor"/>
  <rect x="511" y="138" width="11" height="11" class="f-acc"/><rect x="610" y="138" width="11" height="11" fill="currentColor"/>
  <rect x="500" y="50" width="220" height="110" fill="none" stroke="currentColor"/><text x="513" y="147" fill="currentColor" font-size="12">S</text><text x="700" y="70" fill="currentColor" font-size="12">G</text>
  <text x="10" y="180" fill="currentColor">10×20 지도 + 세로 벽(검정). S=(8,1) → G=(1,18). 진한 칸 = 경로, 회색 = 닫힌 칸.</text>
  <text x="10" y="198" fill="currentColor">다익스트라 = g &lt; C* 전부 · 낮은 g 우선 = f = C* 띠 전체 · 높은 g 우선 = 경로만</text>
</svg>
```

무작위 지도에서는 차이가 작다(300개 합계 18863 / 21312 / 20369, demo 의 64×64 흩뿌림 10%: 301 / 469 / 396). 장애물이 동점을 깨기 때문이다. 동점 처리는 **열린 공간이 넓은 지도**(창고, 실외)에서 효과가 크다. 다른 흔한 방법: h 에 (1 + p) 를 곱하되 p < (한 걸음 최소 비용) / (예상 최대 경로 길이) 로 작게 — Amit Patel 의 휴리스틱 노트가 소개하는 방법(예: p = 1/1000). 허용성을 아주 살짝 깨는 대신 동점이 목표 쪽으로 기운다. 정수 비용에서는 키 비교에 g 를 2순위로 넣는 이 레슨 방식이 h 를 건드리지 않아 깔끔하다.

### Part C — 팽창: 원판, 행 구간, 정확성

로봇을 반경 R 의 원으로 보고 중심만 계획하려면 **구성 공간(configuration space)** 을 만든다: 장애물 칸 중심에서 거리 ≤ r 인 칸은 중심이 갈 수 없다. r = ⌈R / 해상도⌉ (+ 안전 여유) 로 올림해야 보수적이다 — 격자 이산화 오차(칸 안 어디에 장애물이 있는지 모름)가 있으니 반 칸 정도 여유를 더 두는 것이 보통이다(확인 권장: 사용하는 스택의 규칙).

`inflate` 는 원판이 행마다 연속 구간이라는 점을 쓴다. 반폭 `hw[|dy|] = ⌊√(r² − dy²)⌋` 를 정수 루프로 한 번(sqrt 없음, FPU 없는 M0 도 가능), 그다음 장애물마다 2r+1 번 `memset`. 시간 O(장애물 수 · r) 바이트 쓰기 수준. 무식한 기준 `inflate_brute` 는 칸마다 (2r+1)² 창을 훑는다. 검사:

- 빈 21×21 가운데 장애물 하나: r = 0..5 에서 막힌 칸 1, 5, 13, 29, 49, 81 — 원 안 격자점 수(Gauss circle problem 의 N(r))와 일치.
- 무작위 120개 지도(1..64 × 1..64, 3개 중 1개는 장애물을 1/8 로 솎은 드문 지도) × r = 0..6 = 840쌍, 칸 단위 일치. r = 1000 은 상한 96 으로 잘려 장애물이 하나라도 있으면 전부 막힌다(64×64 대각선 ≈ 89 < 96).

사각 창(체비쇼프, `max(|dx|,|dy|) ≤ r`)으로 찍으면 위 그림 ③ 의 회색 모서리까지 막혀 통로가 실제보다 좁아진다. 맨해튼 원판(`|dx|+|dy| ≤ r`)은 반대로 대각선 방향 여유가 모자라 위험하다.

### Part C — 창고 지도: 반경에 따라 길이 바뀐다

19×41 창고, 열 20 에 두께 1 벽. 좁은 문 = 행 4..6 (3칸), 넓은 문 = 행 10..16 (7칸). S=(5,5), G=(5,35). 문 칸 x 가 반경 r 로 열려 있으려면 위아래 벽 칸까지 거리가 r 보다 커야 하므로: 좁은 문(벽 행 3, 7)은 `x − 3 > r` 이고 `7 − x > r` → r ≤ 1 에서만 행 5 가 열림. 넓은 문(벽 행 9, 17)은 r = 2 → 행 12..14, r = 3 → 행 13 하나, r = 4 → 닫힘. 실제 출력:

| r | 비용 | 지나는 행 | 확장 | 막힌 칸 |
|---|---|---|---|---|
| 0 | 300 | 5 (좁은 문) | 31 | 139 |
| 1 | 300 | 5 (좁은 문) | 31 | 283 |
| 2 | 356 | 12 (넓은 문) | 108 | 424 |
| 3 | 388 | 13 (넓은 문) | 80 | 564 |
| 4 | -1 | — | 8 | 688 |
| 5 | -1 | — (시작 칸 자체가 막힘) | 0 | 755 |

r = 4 는 문이 둘 다 닫히고 왼쪽 상자의 팽창까지 겹쳐 시작에서 8칸만 닿는다. r = 5 는 시작 칸이 바깥벽에서 5칸이라 팽창 영역 안 → 확장 0. 펌웨어라면 "시작이 막힘" 을 따로 보고해야 한다(Q6). r = 2 경로 (`#` 원래 장애물, `+` 팽창, `*` 경로, solution.c 실제 출력):

```text
  #########################################
  #+++++++++++++++++++#+++++++++++++++++++#
  #+++++++++++++++++++#+++++++++++++++++++#
  #++...............++#++...............++#
  #++................+++................++#
  #++..S*******.......+.......*******G..++#
  #++..........*.....+++.....*..........++#
  #++......++++.*...++#++...*...........++#
  #++.....++++++.*..++#++..*............++#
  #++....++####++.*.++#++.*.............++#
  #++....++####++..*.+++.*..............++#
  #++.....++++++....*.+.*.....++++......++#
  #++......++++......***.....++++++.....++#
  #++.......................++####++....++#
  #++.......................++####++....++#
  #++.................+......++++++.....++#
  #+++++++++++++++++++++++++++++++++++++++#
  #+++++++++++++++++++#+++++++++++++++++++#
  #########################################
```

검사: 경로의 모든 칸에서 **원래** 장애물까지 거리² > r² (여유 거리), 경로가 팽창 지도에서 합법(`path_ok`). 경로가 '+' 바로 옆을 스치는 것은 정상이다 — 최단 경로는 언제나 제약에 붙어 다닌다. 그래서 실무에서는 팽창 바깥에 **거리에 따라 줄어드는 비용**을 더해 벽에서 떨어지게 한다(AUTON-B06 의 inflation layer).

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6, Apple clang version 21.0.0 (clang-2100.0.123.102), `-O2`. 측정 직전 `uptime` load averages **125.91 62.20 30.23**, 측정 후 127.04 63.49 30.87 — 다른 작성 작업 10개가 같이 돌고 있어 절대 µs 는 믿지 말고 **비율**만 볼 것. 64×64 지도 100개씩, 시작은 왼쪽 4열·목표는 오른쪽 4열. (1)은 시간과 무관한 결정적 숫자 (실제 출력, 열 정리):

| 지도 | 도달 | 다익스트라 | 옥타일 큰 g | 옥타일 작은 g | 옥타일 번호 | 맨해튼 큰 g | 최대 열린 집합 |
|---|---|---|---|---|---|---|---|
| 흩뿌림 10% | 100/100 | 3375.7 | 301.3 | 469.4 | 395.8 | 135.0 | 273 |
| 흩뿌림 20% | 99/100 | 3007.2 | 656.7 | 714.1 | 689.0 | 274.2 | 232 |
| 흩뿌림 30% | 90/100 | 2392.7 | 924.4 | 942.8 | 934.8 | 590.4 | 164 |
| 벽 조각 40개 | 62/100 | 2596.9 | 1626.5 | 1689.7 | 1655.3 | 1403.7 | 268 |

(2) 질의당 시간, 흩뿌림 20%, 워밍업 1회 + 7회 측정의 중앙값 [최소..최대] µs: 다익스트라 349.59 [219.44..526.44], 옥타일 큰 g 93.93 [64.86..101.37] (**×0.27**), 작은 g 105.23 (×0.30), 번호 103.99 (×0.30), 맨해튼 28.44 [25.42..76.14] (×0.08). (3) 팽창 µs: 행 구간 찍기 11.70 / 13.70 / 25.00 / 48.35 vs 칸마다 창 37.45 / 43.40 / 51.90 / 114.55 (r = 1/2/4/8, 2.1~3.2배).

읽을 것: (a) 멀리 떨어진 질의에서 옥타일 A* 는 흩뿌림 지도에서 다익스트라의 **9~39%** 만 확장한다. 벽 조각 지도는 우회가 커서 휴리스틱이 덜 맞고(h 와 h* 차이 큼) 63% 까지 올라간다 — A* 의 이득은 h 가 h* 에 얼마나 가까운가에 달렸다. (b) 장애물이 많을수록 다익스트라는 오히려 덜 확장한다(닿는 칸이 줄어서). (c) 최대 열린 집합은 273 — 4096 칸 힙 배열의 7% (Q4 의 RAM 이야기).

### Cortex-M4 코드와 RAM 예산

`demo_cm4_heur.c` (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`, 지시어 줄 제외 실제 출력):

```text
h_octile:
	subs	r2, r0, r2
	it	mi
	rsbmi	r2, r2, #0
	subs	r1, r1, r3
	it	mi
	rsbmi	r1, r1, #0
	add.w	r0, r1, r1, lsl #2
	lsls	r0, r0, #1
	add.w	r0, r0, r2, lsl #2
	add.w	r12, r2, r2, lsl #2
	lsls	r3, r1, #2
	cmp	r2, r1
	it	hi
	addhi.w	r0, r3, r12, lsl #1
	bx	lr
```

분기·곱셈·FPU 없이 15개 명령: 절댓값은 `subs` + `it mi` + `rsbmi`, ×10 은 `x + (x << 2)` 후 `<< 1`, ×4 는 시프트, max/min 선택은 `cmp` + `it hi`. 두 경우의 값을 다 계산한 뒤 조건부로 덮어쓴다. 맨해튼은 10개. 유클리드 h 를 쓰면 칸마다 sqrt 가 들어가고(M4F 라면 단정밀 FPU, M0 는 소프트웨어 — 이 레슨에서 asm 은 확인 안 함), **10/14 비용에서는 허용성도 깨진다**: 10·√(1²+1²) = 14.14 > 14 라 대각선 한 칸을 과대평가한다. 진짜 √2 비용이라면 유클리드는 허용 가능하지만 옥타일보다 작아(덜 정보적) 더 많이 확장한다.

| 항목 | 이 구현 (호스트) | 압축안 (계산값, 측정 아님) |
|---|---|---|
| g | int32 4 B | uint16 2 B — 단순 경로 ≤ 4095걸음 × 14 = 57330 < 65536 |
| f | int32 4 B | 저장 안 함(비교 때 g + h 재계산) 또는 uint16 2 B (f ≤ 57330 + 882) |
| parent | int16 2 B | 방향 3비트 |
| closed / 힙 안 여부 | uint8 1 B | 1비트씩 → parent 와 한 바이트 |
| 힙 위치 pos + 힙 슬롯 | int16 + int16 4 B | 같음 (또는 힙을 줄이고 넘치면 실패 처리) |
| 칸당 / 64×64 | 15 B / **61468 B** (`sizeof`) | 7~9 B / 28~36 KB |

SRAM 이 수백 KB 인 MCU 면 그대로 들어가지만(모델별 SRAM 은 데이터시트 확인), 64 KB 급 MCU 에서는 압축안이 필요하고 지도 자체(1비트 512 B, M04 Part C)보다 계획 작업 공간이 훨씬 크다는 점이 핵심이다.

### 복잡도 요약

| | 시간 | 추가 메모리 | 비고 |
|---|---|---|---|
| A* / 다익스트라 (indexed PQ) | O(V log V) — push ≤ V, decrease ≤ 8V | O(V) | 일관적 h: 칸마다 확장 최대 1번 |
| 기준 구현 (완화 반복) | O(V · 스윕 수), 최악 O(V²) | O(V) | 테스트 전용 |
| 팽창: 행 구간 | O(장애물 · r) memset (바이트 O(장애물 · r²)) | O(r) 반폭 표 | 연습 2 의 EDT 는 r 과 무관하게 O(V) |
| 팽창: 칸마다 창 | O(V · r²) | 0 | 기준 |

스택: 재귀 없음, 지역 변수 몇 개 + `hw[97]`(388 B). 작업 공간은 정적(`static astar_ws_t`) — 태스크 스택에 61 KB 를 두면 안 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 29/29 passed, 0.08 s user
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 29/29, 보고 없음 (0.25 s)
```

## 7. 자주 하는 실수

아래 숫자는 모두 solution.c 의 사본에 버그 하나를 넣고 같은 하네스를 돌린 **실제 결과**다.

**1. 휴리스틱을 "조금" 과대평가한다.** 옥타일의 4 를 5 로(`10·max + 5·min`) — 대각선을 15 로 착각한 셈. 무작위 300개 지도의 **비용 대조는 0개 틀린다**. 잡은 것은 빈 8×8 전수(h == 비용), 허용성·일관성 직접 검사, `closed_improve`(실행 3번), 동점 검사였다(24/29). 교훈: 허용성 위반은 결과 비용에 잘 안 드러난다 → 휴리스틱은 **h ≤ h\* 를 직접** 검사한다.

**2. 목표를 넣을 때(발견할 때) 끝낸다.**

```c
if (v == t) return nd;            /* 틀림: 아직 더 싼 경로가 열린 집합에 있을 수 있다 */
```

1200번 실행 중 비용이 틀린 것은 **1번**. 대신 "닫힌 칸 ⊆ 다익스트라" 검사가 158칸 위반으로 잡았다(27/29). → 목표는 pop 에서.

**3. 모서리 규칙을 "둘 다 막혔을 때만 금지" 로 쓴다** (`||` 대신 `&&`). 비집기만 막고 모서리 긁기를 허용하는 다른 규칙이 된다. 기준 구현(엄격한 규칙)과 비용 532번, 경로 검증 511번 불일치. 규칙을 바꾸는 것 자체는 설계 선택이지만, **A*·기준·검증기가 같은 규칙**이어야 한다.

**4. decrease-key 후 sift-up 을 빠뜨린다.** g 는 줄였는데 힙 위치가 그대로 → 꺼내는 순서가 깨져 비용 32번 불일치, 일관적 h 인데도 `closed_improve` 가 실행 101번에서 발생 (27/29). "닫힌 칸이 더 싸질 수 있다" 가 보이면 힙 버그를 의심하라.

**5. 팽창 반폭을 `(x+1)² < rem` 으로 구한다.** 경계 위 격자점(예: (2,0) 이 r=2 원 위)을 빠뜨려 r=1 은 세로 3칸, r=2 는 13칸이 아니라 11칸. 원 안 격자점 수 검사, 무식한 대조(840쌍 중 540 불일치), 경로 여유 거리 검사가 모두 FAIL (26/29). `≤ r²` 는 "거리 정확히 r 인 장애물 칸 중심에는 로봇 중심이 못 간다" 는 보수적 선택이다.

**6. 동점 비교를 거꾸로 쓴다** (큰 g 우선 자리에 `<`). 비용은 다 맞고 64×64 확장 수 검사 두 개만 FAIL (27/29). 성능 버그는 비용 테스트로 안 잡힌다 — 확장 수를 테스트에 넣어 둔 이유.

**7. 사각 창이나 맨해튼 원판으로 팽창한다.** 사각은 통로를 과하게 닫고(r=2 에서 25칸 vs 13칸), 맨해튼(|dx|+|dy| ≤ r)은 대각선 방향 여유가 모자라다. 로봇 외형이 원이면 유클리드.

**8. 비용을 float √2 로 두고 `==` 로 비교한다.** 같은 경로가 더하는 순서에 따라 다른 값이 되어 동점이 깨지고 결정성이 사라진다. 정수 10/14(또는 더 정밀하게 1000/1414)를 쓰고 그 오차를 말하는 편이 낫다.

## 8. 면접 Q&A

<details><summary>Q1. A* 는 왜 최적인가요? 허용성과 일관성의 차이는?</summary>

허용성(h ≤ h*)이면, 재개방을 허용하는 A* 는 목표를 꺼낼 때 최적이다: 그 순간 최적 경로 위의 어떤 칸 n 이 열린 집합에 g = g* 로 있고 f(n) ≤ g*(n) + h*(n) = C*, 목표가 먼저 꺼내졌으니 f(목표) = g(목표) ≤ C*. 일관성(h(u) ≤ c + h(v))은 더 강하다: 줄인 비용 c − h(u) + h(v) ≥ 0 이라 A* = 다익스트라가 되어 **꺼낸 칸은 다시 안 연다**(닫힌 집합만으로 충분, 각 칸 최대 1번 확장). 허용하지만 일관적이지 않은 h 에서 닫힌 집합만 쓰면 최적이 깨질 수 있다. 꼬리: "옥타일은?" → 장애물 지운 문제의 정확한 거리 → 일관적(5. 풀이 증명). "맨해튼은?" → 4방향에선 같은 논리로 일관적, 8방향에선 둘 다 아님.

</details>

<details><summary>Q2. 8방향인데 맨해튼을 쓰면 어떻게 되나요? 언제 일부러 쓰나요?</summary>

대각선 한 칸의 진짜 비용 14 를 20 으로 보니 허용 불가 — 이 레슨 지도에서 58 대신 64. 맨해튼 = (10/7)·7(dx+dy) 이고 7(dx+dy) 가 일관적이라 **w = 10/7 가중 A\*** 와 같다: 비용 ≤ 1.43·C* (ARA* 계열 결과, 확인 권장), 실측 최악 1.12. 확장은 옥타일의 42~86% (demo). 쓰는 경우: 재계획 주기가 빠듯하고 몇 % 더 긴 길을 감수할 때, 또는 anytime 계획(ARA*)의 첫 해. 꼬리: "h 를 키우면 무조건 빨라지나?" → 대체로 확장이 줄지만 보장은 아니다; 연습 3 실측은 w = 1 → 3 에서 평균 88.7 → 32.6.

</details>

<details><summary>Q3. 왜 목표를 꺼낼 때 멈추죠? 넣을 때 멈추면 안 되나요?</summary>

넣을 때의 g 는 "지금까지 본 경로" 일 뿐이다. 더 싼 경로가 아직 열린 집합에 있는 칸을 거쳐 나중에 발견될 수 있다. 꺼낼 때는 일관성 정리로 g = g*. 실측: 넣을 때 멈추면 1200번 중 1번 틀렸다 — 드물어서 더 위험하다(테스트를 통과하고 필드에서 가끔 긴 길). 꼬리: "BFS(M04)는 넣을 때 멈춰도 되던데?" → 가중치가 모두 1 이면 FIFO 큐의 값이 d, d+1 두 가지뿐이라 처음 발견 = 최단. 가중치가 생기면 깨진다. 대각선 14 가 그 경우다.

</details>

<details><summary>Q4. 64×64 지도에서 RAM 은 얼마나? 힙 크기를 줄일 수 있나요?</summary>

이 구현 61468 B(15 B/칸). g 를 uint16(단순 경로 비용 ≤ 4095·14 = 57330), parent 를 방향 3비트, closed 를 1비트로 줄이면 7~9 B/칸 = 28~36 KB(계산값). 측정한 최대 열린 집합은 273(지도 400개) — 힙을 512 칸으로 줄이는 유혹이 있지만 **최악은 O(V)** 다(빈 지도에서 작은 g 우선은 1167 push). 줄이려면 "힙이 차면 실패 반환" 같은 명시적 처리를 두고 그 확률을 시험해야 한다. 다른 선택: 비용이 작은 정수라 f 값별 **버킷 큐**(Dial, M06 Q 참고) — f 범위 [h(s), C*] 가 수천이라 버킷 머리 배열 + 칸별 다음 포인터 2 B 로 힙 없이 O(1) 연산. 꼬리: "지도가 1024×1024 면?" → 칸 단위 상태가 MB 급 → 계층 계획(거친 격자에서 먼저, 경로 주변만 세밀하게)이나 JPS 같은 가지치기.

</details>

<details><summary>Q5. 동점 처리는 왜 중요하고, 어떻게 하나요?</summary>

열린 공간에서는 최적 경로가 조합적으로 많고 그 위 칸이 모두 f = C* 다. 동점 처리가 없으면 그 띠 전체를 열 수 있다: 빈 64×64 에서 작은 g 우선 984, 큰 g 우선 64(경로 길이). 방법: 키를 (f, −g) 사전순으로. "칸 번호 순" 은 정책이 아니라 지도 방향에 따라 우연히 좋거나 나쁘다(같은 질의에서 984, 다른 지도에서 18). h 에 작은 배수를 곱하는 방법은 허용성을 살짝 깬다. 꼬리: "결정성은?" → 마지막 동점까지 칸 번호로 깨서 같은 입력 → 같은 경로. 로그 재현과 HIL 테스트에서 중요하다.

</details>

<details><summary>Q6. 팽창했더니 로봇의 현재 칸이 막혔다고 나옵니다. 어떻게 하죠?</summary>

흔하다: 센서 갱신으로 장애물이 가까워지거나 로봇이 벽에 붙어 정지한 경우. 이 레슨의 `astar` 는 -1 을 돌려준다(창고 지도 r=5). 실무 선택지: (1) 시작 칸만 예외로 열고 "팽창 영역 안에서는 밖으로 나가는 방향의 이동만 허용" (2) 거리 변환(연습 2)의 기울기를 따라 가장 가까운 자유 칸으로 먼저 빠져나온 뒤 계획 (3) 반경을 줄여 다시 시도하고 속도 제한. 어떤 것이든 **원인을 구분해 보고**해야 한다: "목표 도달 불가" 와 "시작이 막힘" 은 상위 행동(재탐색 vs 후진)이 다르다. 꼬리: "목표가 팽창 영역 안이면?" → 목표 근처 가장 가까운 자유 칸으로 바꾸고 마지막은 다른 제어기(정밀 도킹)에 맡긴다.

</details>

<details><summary>Q7. 대각선을 14 로 두면 답이 "진짜 최단" 인가요?</summary>

아니다. 10/14 거리에 대해 최단이다. 14 < 10√2 라 대각선이 1% 싸서, 대각선이 많은 경로를 살짝 편애한다. 증명된 상한은 진짜 길이 기준 10√2/14 = 1.0102배, 연습 1 의 일부러 만든 지도(U 통로 vs V 통로)에서 1.00167배를 실제로 만들었고 무작위 지도에서는 차이가 안 났다. 더 정밀하게 하려면 1000/1414(오차 0.01%)처럼 스케일을 키운다 — 대신 비용 폭(uint16 이 넘침)을 다시 계산해야 한다. 그리고 격자 경로 자체는 직선보다 최대 8.2% 길다(22.5° 방향) — 그건 Theta* 같은 any-angle 계획이나 후처리(가시선 단축)의 몫이다.

</details>

<details><summary>Q8. 지도가 계속 바뀌면 매번 A* 를 처음부터 돌리나요?</summary>

작은 지도면 그렇다 — 64×64 에서 수십~수백 µs(이 맥, 부하 높음 — MCU 에선 직접 재야 함). 경로가 지나는 칸이 바뀌었을 때만 재계획하면 더 싸다. 지도가 크고 변화가 국소적이면 증분 계획(D* Lite, LPA*)이 이전 탐색을 재사용한다 — 상태가 칸당 g, rhs 두 개로 늘고 코드가 복잡하다. 목표가 고정이고 로봇이 자주 재계획하면 목표에서 한 번 역방향 다익스트라로 비용장을 만들어 두고 내려가기만 하는 방법도 있다(M04 Q4 의 거리장). 펌웨어 주의: 계획 중에 지도 버퍼를 다른 태스크/ISR 이 바꾸면 일관성이 깨진다 → 이중 버퍼나 스냅샷 복사.

</details>

<details><summary>Q9. A* 와 BFS·다익스트라·JPS 를 한 문장씩 비교하면?</summary>

BFS: 가중치 1 전용, 큐 하나, 처음 발견 = 최단(M04). 다익스트라: 음 아닌 가중치, 목표 방향 무시(M06). A*: 다익스트라 + 하한 추정으로 목표 쪽만, 일관적 h 면 확장 ⊆ 다익스트라. JPS(Jump Point Search, Harabor·Grastien 2011): 균일 비용 격자에서 대칭 경로를 가지치기해 A* 의 확장을 크게 줄인다 — 단, 이 레슨처럼 칸마다 비용이 다르거나 팽창 비용이 섞이면 그대로는 못 쓴다. 꼬리: "LeetCode 1091 Shortest Path in Binary Matrix 는?" → 8방향이지만 가중치가 모두 1 이라 BFS 로 충분, 모서리 규칙도 없다.

</details>

## 9. 추가 연습

### 연습 1 — 10/14 근사의 대가

10/14 로 찾은 경로의 진짜 길이(직선 1, 대각선 √2)를 진짜 최적(double 가중치 다익스트라)과 비교하라. 무작위 지도에서, 그리고 일부러 10/14 와 진짜 길이가 다른 답을 내게 만든 지도에서. 덤으로 빈 지도에서 (옥타일 진짜 길이 / 직선 거리)의 최댓값.

<details><summary>답 보기</summary>

경로 A(대각선 a₁, 직선 b₁)와 B(a₂, b₂)에서 10/14 는 A 를, 진짜 길이는 B 를 고르려면 Δa = a₁ − a₂, Δb = b₂ − b₁ 일 때 1.4·Δa < Δb < 1.41421·Δa — 정수 해가 Δa = 17 부터 나온다. U 통로(위로 7, 옆으로 36, 아래로 7: 직선 50 = 500, 진짜 50)와 V 통로(대각선 18번 내려갔다 18번 올라옴, 꼭짓점에서 2번을 직선으로 줄여 대각선 34 + 직선 2 = 496, 진짜 50.083)를 판 26×37 지도에서 10/14 는 V(496), 진짜 최적은 U(50). 실제 출력:

```text
연습 1: 145 random maps, 10/14 path really longer on 0, worst 1.00000 (bound 10*sqrt2/14 = 1.01015); grid vs straight line max 1.08239 at (29,12), sqrt(4-2*sqrt2) = 1.08239
  built U/V map: 10/14 cost 496, its real length 50.0833, real optimum 50.0000, ratio 1.00167
```

옥타일/직선 = cos θ + (√2 − 1) sin θ, 최댓값 √(1 + (√2−1)²) = √(4 − 2√2) 은 tan θ = √2 − 1(22.5°)에서 — (29,12) 의 12/29 = 0.414 가 그 방향이다.

</details>

### 연습 2 — 거리 변환으로 팽창 (모든 r 을 한 번에)

칸마다 가장 가까운 장애물까지의 **거리²**(squared Euclidean distance transform)를 O(V) 로 구하고, 팽창 = `d² ≤ r²` 로 만들어 본문의 행 구간 찍기와 r = 0..10 에서 대조하라.

<details><summary>답 보기</summary>

거리²는 분리 가능하다: d²(x,y) = min_i ((x−i)² + g(i,y)²). 열마다 1D 변환, 그다음 행마다 1D 변환. 1D 변환 d(q) = min_p ((q−p)² + f(p)) 는 포물선들의 **하한 포락선(lower envelope)** 이라 O(n) (Felzenszwalb·Huttenlocher). 포물선 p, q 의 교점 s = ((f(q)+q²) − (f(p)+p²)) / (2(q−p)) 를 스택에 유지하고, 새 포물선이 들어올 때 교점이 이전 경계보다 왼쪽이면 이전 포물선을 버린다. 장애물이 없는 칸(f = ∞)은 포물선을 넣지 않는다. 실제 출력: `연습 2: EDT threshold vs span stamping: 660 (map, r) cases, mismatches=0`. 거리장이 있으면 반경을 바꿔도 다시 계산할 필요가 없고, 거리에 따라 줄어드는 비용(AUTON-B06)과 Q6 의 "빠져나오기 방향" 도 같은 배열에서 나온다. 1×7 양 끝 장애물의 거리² = 0 1 4 9 4 1 0 도 손 계산 검사로 넣었다.

</details>

### 연습 3 — 가중 A*: 얼마나 빨라지고 얼마나 나빠지나

f = g + w·h (옥타일, 재개방 없음)를 w = 1, 1.25, 1.5, 2, 3 으로 돌려 확장 수와 비용 비율을 재고, 모든 지도에서 비용 ≤ w·최적인지 확인하라. 정수만 쓰려면 w = num/den 으로 키를 den·g + num·h 로.

<details><summary>답 보기</summary>

194개 지도(16..64 × 16..64) 실제 출력:

```text
  w=1.00  expanded mean    88.7  suboptimal maps   0  worst ratio 1.0000
  w=1.25  expanded mean    47.8  suboptimal maps  53  worst ratio 1.0986
  w=1.50  expanded mean    38.9  suboptimal maps  83  worst ratio 1.1405
  w=2.00  expanded mean    35.2  suboptimal maps  95  worst ratio 1.3806
  w=3.00  expanded mean    32.6  suboptimal maps  97  worst ratio 1.3806
```

상한 위반 0. w = 1.25 에서 이미 확장이 절반이고, 그 뒤로는 거의 안 준다(대부분 경로 길이 근처에 붙음) — 비용만 나빠진다. 그래서 실무 w 는 1.x 대가 흔하다. 키를 int64 로 둔 이유: den·g + num·h 가 int32 를 넘을 수 있는 큰 w/큰 지도 대비.

</details>

### 연습 4 — 가장 가까운 충전 도크 (목표 여러 개)

도크 k 개(1..6) 중 아무 데나 가면 된다. h = min_j oct(u, dock_j) 로 A* 를 돌리고 "어느 목표든 꺼내면 끝" 으로 하라. h 가 일관적임을 보이고, 다익스트라·기준 구현(각 도크까지 최단의 최솟값)과 대조하라.

<details><summary>답 보기</summary>

각 oct(·, dock_j) 가 일관적이면 최솟값도 일관적이다: j* = argmin_j oct(v, dock_j) 로 두면 h(u) ≤ oct(u, dock_j*) ≤ c(u,v) + oct(v, dock_j*) = c(u,v) + h(v). 목표에서 h = 0 (그 도크까지 0). 실제 출력: `연습 4: 150 maps, mismatches=0, A* expanded fewer than Dijkstra on 141 maps` (나머지 9개는 같은 수 — 정리상 더 많을 수는 없다). 도크가 많으면 h 계산이 칸당 O(k) 라 표로 미리 구했다. 도크가 고정이면 아예 도크들에서 한 번 다중 출발점 다익스트라를 돌려 비용장을 저장하는 편이 낫다(M06 연습 5).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 8/8 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san   # 8/8, 보고 없음
```

## 10. 핵심 정리

- 8방향 정수 비용 10/14: 결과는 **10/14 거리에 대해** 최적. 진짜 길이로는 최대 10√2/14 = 1.0102배(만든 지도에서 1.00167 실측), 격자 경로 자체는 직선의 최대 1.0824배.
- 옥타일 10·max + 4·min = 장애물 지운 문제의 정확한 거리 → 일관적 → 허용 가능. 하한 증명: 10a + 14b = 6(a+b) + 4(a+2b).
- 일관성 ⇔ 줄인 비용 ≥ 0 ⇒ A* = 다익스트라 → pop = 확정, 재개방 없음, 닫는 칸 ⊆ 다익스트라. 목표는 **꺼낼 때** 끝.
- 맨해튼(8방향)은 대각선마다 6 과대평가 = w 10/7 가중 A*: 58 대신 64, 비용 ≤ 1.43·C*. 빨리 대충이 필요할 때만 일부러.
- 동점은 (f, −g): 빈 64×64 확장 984 → 64. 칸 번호 순은 우연.
- 모서리 자르기 금지 규칙은 A*·기준·검증기에 똑같이. 기준 구현은 이동 규칙까지 따로 다시 쓴다.
- 팽창 = 유클리드 원판 dx²+dy² ≤ r² (r=2 → 13칸), 반폭 표 + 행 memset. 시작 칸이 막히는 경우를 따로 보고.
- 테스트는 비용만 보면 못 잡는 버그가 있다(과대평가·넣을 때 종료·동점): h ≤ h\* 직접 검사, closed_improve, 확장 집합 포함 관계, 확장 수까지 본다.

## 참고자료

- P. E. Hart, N. J. Nilsson, B. Raphael, "A Formal Basis for the Heuristic Determination of Minimum Cost Paths", *IEEE Transactions on Systems Science and Cybernetics* 4(2), 1968 — A* 원 논문 (허용성, 일관성에 해당하는 조건).
- I. Pohl, "Heuristic search viewed as path finding in a graph", *Artificial Intelligence* 1(3), 1970 — 가중 휴리스틱 탐색. 서지는 기억에 의존 — 확인 권장.
- M. Likhachev, G. Gordon, S. Thrun, "ARA*: Anytime A* with Provable Bounds on Sub-Optimality", NIPS 2003 — 부풀린 일관적 휴리스틱, 재확장 없이 ε-최적 상한 (정리의 정확한 조건은 확인 권장).
- P. F. Felzenszwalb, D. P. Huttenlocher, "Distance Transforms of Sampled Functions", *Theory of Computing* 8, 2012 — 연습 2 의 하한 포락선 거리 변환.
- D. Harabor, A. Grastien, "Online Graph Pruning for Pathfinding on Grid Maps", AAAI 2011 — Jump Point Search.
- A. Nash, K. Daniel, S. Koenig, A. Felner, "Theta*: Any-Angle Path Planning on Grids", AAAI 2007 — 격자 경로의 각도 제약 넘기.
- S. Koenig, M. Likhachev, "D* Lite", AAAI 2002 — 증분 재계획.
- Amit Patel, "Heuristics" (Amit's A* Pages): <http://theory.stanford.edu/~amitp/GameProgramming/Heuristics.html> — 대각선(옥타일) 거리, 동점 처리, 휴리스틱 크기와 속도/정확도.
- S. M. LaValle, *Planning Algorithms*, Cambridge University Press, 2006, Ch. 2 (이산 계획, A*)·Ch. 4 (구성 공간): <http://lavalle.pl/planning/>
- 관련 레슨: ALGO-M04 (BFS 격자·1비트 지도·거리장), ALGO-M06 (다익스트라 증명·indexed PQ·Dial 버킷), DS-M11 (indexed PQ), DS-B12 (이진 힙), AUTON-B05 (점유 격자 매핑), AUTON-B06 (계층 코스트맵·감쇠 팽창 — 이 레슨의 A* 를 씀), AUTON-M06 (Hybrid A*), AUTON-M07 (RRT/RRT*), ALGO-A02 (음수 간선·벨만-포드).
