---
id: ALGO-M04
time: 80분
updated: 2026-11-11
---
# ALGO-M04 BFS/DFS: 점유 격자 지도 탐색

## 학습 목표

- 점유 격자(occupancy grid)에서 **BFS 최단 경로**를 짜고, "큐 안의 거리값은 d 와 d+1 두 가지뿐이고 비내림차순" 이라는 **큐 불변식**으로 "처음 발견 = 최단" 을 증명한다. parent 배열로 경로를 **뒤집기 없이** 복원한다.
- 장애물 덩어리(blob) 수를 **명시적 스택 DFS** 와 **BFS** 두 가지로 세고, **4-연결 vs 8-연결**이 답을 어떻게 바꾸는지 — 그리고 4방향으로 움직이는 로봇에게는 왜 장애물을 8-연결로 봐야 하는지(쌍대성, duality) — 손 지도로 설명한다.
- 펌웨어판으로 1비트 지도(512 B) + 방문 비트맵 + **2비트 방향 parent** 를 써서 작업 메모리를 24 KB → 9.5 KB 로 줄이고, 결과가 바이트판과 **칸 단위로 같은지** 확인한다.
- 손으로 만든 지도 10개(기댓값을 손으로 셈)와, 의심할 곳이 없는 느린 기준 구현("바뀌지 않을 때까지 완화")을 무작위·전수 격자에 대조하는 테스트 설계를 말로 설명할 수 있다.

## 1. 문제

격자 BFS 는 로봇·드론 회사 코딩 면접의 단골이다 — "섬 개수", "최단 경로", "도달 가능한 면적" 이 모두 같은 30줄에서 나오고, 꼬리질문은 거의 항상 "그 보드의 RAM 으로 돌아가요?" 다.

로봇 청소기(또는 창고 AMR) 펌웨어가 64×64 점유 격자를 들고 있다. 칸마다 0 = 빈칸, 1 = 장애물. 로봇은 **상하좌우 4방향**으로 한 칸씩 움직인다. 필요한 것은 세 가지다.

1. 도크 S 에서 목표 G 까지 **가장 적은 이동 수**와 그 경로 (경로 계획의 바닥 단계)
2. 지도 위 **장애물 덩어리 개수** (가구·벽 단위 분할, 지도 품질 진단)
3. S 에서 **닿을 수 있는 바닥 면적** (청소 가능 면적, 갇힌 방 검출)

칸 번호는 `id = r * 64 + c` 로 고정한다 (지도가 64열보다 좁아도 행 폭은 64). 그러면 `r = id >> 6`, `c = id & 63` 이라 나눗셈이 없다 (나눗셈기 없는 코어 얘기는 ALGO-B11 Q6). 대신 `c + 1 == cols` 인 칸은 배열 안이지만 **지도 밖**이므로 범위 검사는 `(r, c)` 로 한다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">지도 M5 — 같은 장애물, 4-연결이면 덩어리 6개 / 8-연결이면 1개</text>
  <g stroke="currentColor">
    <rect x="40" y="60" width="40" height="40" class="f-warn"/><rect x="80" y="60" width="40" height="40" class="f-mut"/><rect x="120" y="60" width="40" height="40" class="f-warn"/><rect x="160" y="60" width="40" height="40" class="f-mut"/>
    <rect x="40" y="100" width="40" height="40" class="f-mut"/><rect x="80" y="100" width="40" height="40" class="f-warn"/><rect x="120" y="100" width="40" height="40" class="f-mut"/><rect x="160" y="100" width="40" height="40" class="f-warn"/>
    <rect x="40" y="140" width="40" height="40" class="f-warn"/><rect x="80" y="140" width="40" height="40" class="f-ok"/><rect x="120" y="140" width="40" height="40" class="f-warn"/><rect x="160" y="140" width="40" height="40" class="f-ok"/>
    <rect x="40" y="180" width="40" height="40" class="f-ok"/><rect x="80" y="180" width="40" height="40" class="f-ok"/><rect x="120" y="180" width="40" height="40" class="f-ok"/><rect x="160" y="180" width="40" height="40" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="85">1</text><text x="140" y="85">2</text><text x="100" y="125">3</text><text x="180" y="125">4</text>
    <text x="60" y="165">5</text><text x="140" y="165">6</text><text x="60" y="205">S</text><text x="180" y="205">G</text>
  </g>
  <g fill="currentColor">
    <text x="215" y="85">4-연결:</text><text x="215" y="105">덩어리 6개</text>
    <text x="215" y="135">대각선으로만</text><text x="215" y="155">닿으면 남남</text>
  </g>
  <g stroke="currentColor">
    <rect x="420" y="60" width="40" height="40" class="f-warn"/><rect x="460" y="60" width="40" height="40" class="f-mut"/><rect x="500" y="60" width="40" height="40" class="f-warn"/><rect x="540" y="60" width="40" height="40" class="f-mut"/>
    <rect x="420" y="100" width="40" height="40" class="f-mut"/><rect x="460" y="100" width="40" height="40" class="f-warn"/><rect x="500" y="100" width="40" height="40" class="f-mut"/><rect x="540" y="100" width="40" height="40" class="f-warn"/>
    <rect x="420" y="140" width="40" height="40" class="f-warn"/><rect x="460" y="140" width="40" height="40" class="f-ok"/><rect x="500" y="140" width="40" height="40" class="f-warn"/><rect x="540" y="140" width="40" height="40" class="f-ok"/>
    <rect x="420" y="180" width="40" height="40" class="f-ok"/><rect x="460" y="180" width="40" height="40" class="f-ok"/><rect x="500" y="180" width="40" height="40" class="f-ok"/><rect x="540" y="180" width="40" height="40" class="f-ok"/>
  </g>
  <g stroke="currentColor" stroke-width="2.5">
    <line x1="440" y1="80" x2="480" y2="120"/><line x1="480" y1="120" x2="520" y2="80"/><line x1="520" y1="80" x2="560" y2="120"/>
    <line x1="560" y1="120" x2="520" y2="160"/><line x1="520" y1="160" x2="480" y2="120"/><line x1="480" y1="120" x2="440" y2="160"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="440" y="205">S</text><text x="560" y="205">G</text>
  </g>
  <g fill="currentColor">
    <text x="595" y="85">8-연결:</text><text x="595" y="105">덩어리 1개</text>
    <text x="595" y="135">4방향 로봇에게는</text><text x="595" y="155">이게 벽 하나다</text>
  </g>
  <text x="40" y="250" fill="currentColor">주황 = 장애물, 초록 = S 에서 4방향으로 닿는 빈칸 6개, 회색 = 갇힌 빈칸 4개.</text>
  <text x="40" y="272" fill="currentColor">대각선으로만 이어진 장애물 사슬도 4방향 로봇은 못 지나간다 → 로봇 기준 "벽" 은 8-연결로 센다.</text>
</svg>
```

### Part A — BFS 최단 경로와 경로 복원

```c
typedef struct { int rows, cols; uint8_t cell[64 * 64]; } grid_t;            /* cell[r*64+c]: 0 빈칸, 1 장애물 */
typedef struct { int16_t dist[4096], parent[4096], queue[4096]; } bfs_ws_t;  /* 24 KB, 호출자가 준다 */
int grid_bfs (const grid_t *g, int start, bfs_ws_t *ws);   /* 도달한 칸 수(start 포함), start 가 밖/장애물이면 0 */
int grid_path(const grid_t *g, int start, int goal, bfs_ws_t *ws, int16_t path[], int cap);
            /* path[0]=start … path[len-1]=goal, len = 거리+1. 도달 불가·잘못된 칸 -1, cap 부족 -2 (path 는 안 건드림) */
```

이웃 순서는 위·오른쪽·아래·왼쪽으로 **고정**한다 → 최단 경로가 여러 개여도 항상 같은 경로가 나온다 (테스트와 로그 재현에 필요). 미도달 칸의 `dist` 는 -1.

### Part B — 장애물 덩어리: 스택 DFS 와 BFS, 4-연결 / 8-연결

```c
int blobs_dfs(const grid_t *g, int conn, int16_t label[], int16_t stack[]);   /* conn = 4 또는 8, 그 외 -1 */
int blobs_bfs(const grid_t *g, int conn, int16_t label[], int16_t queue[]);
```

반환값은 덩어리 수 k. `label[id]` 는 장애물이면 1..k, 빈칸이면 0. 번호는 **행 우선(row-major)으로 처음 만난 덩어리부터** 1, 2, … — 그래서 DFS 판과 BFS 판의 `label` 배열이 바이트 단위로 같아야 한다 (테스트가 확인). 재귀는 쓰지 않는다 (깊이 = 덩어리 크기, ALGO-B11). 체커보드 64×64 는 4-연결 덩어리가 2048개라 라벨은 `int8_t` 로 부족하다.

### Part C — 펌웨어판: 1비트 지도로 도달 면적과 경로

```c
typedef struct { int rows, cols; uint8_t bits[512]; } occ_map_t;        /* 1비트/칸, 1 = 장애물 */
typedef struct { uint8_t seen[512]; uint8_t dir[1024]; uint16_t queue[4096]; } plan_ws_t;  /* 9,728 B */
int occ_bfs(const occ_map_t *m, int start, int stop_at, plan_ws_t *ws);   /* stop_at 을 발견하면 즉시 멈춤 (-1 = 끝까지) */
int occ_reach_area(const occ_map_t *m, int start, plan_ws_t *ws);         /* S 에서 닿는 빈칸 수 */
int occ_plan(const occ_map_t *m, int start, int goal, plan_ws_t *ws, int16_t path[], int cap);   /* grid_path 와 같은 계약 */
```

`dir` 는 칸마다 2비트: "부모에서 이 칸으로 올 때 쓴 방향 k". parent 를 int16 으로 들고 다니는 대신 `parent = v - DID[k]` (`DID = {-64, +1, +64, -1}`) 로 되돌린다. **경로는 Part A 와 칸열까지 같아야 한다** (같은 이웃 순서, 같은 발견 순서).

시나리오(테스트에 있음): 64×64 집. 21·42번 행과 열이 벽 → 방 9개. 바깥 8개 방은 2칸짜리 문으로 고리처럼 이어지고 가운데 방(20×20)은 문이 모두 닫혀 있다. 도크 (1,1) 에서 청소 가능 면적과 갇힌 면적을 보고하고, 가운데 문을 열면 다시 계산한다.

## 2. Problem (English)

> **Occupancy-grid search.** A robot stores a map of up to 64×64 cells, `cell[r*64 + c]` (0 = free, 1 = obstacle); the row stride is always 64 even if `cols < 64`. The robot moves in 4 directions. No recursion, no heap allocation; all scratch memory is passed in.
>
> 1. `grid_bfs(g, start, ws)` fills `ws->dist` (moves from `start`, `-1` if unreachable) and `ws->parent`, and returns the number of reachable cells (0 if `start` is outside the map or blocked). `grid_path(g, start, goal, ws, path, cap)` writes one shortest path `start … goal` into `path` and returns its length in cells, `-1` if unreachable/invalid, `-2` if `cap` is too small (nothing written). Neighbour order is up, right, down, left, so the path is deterministic.
> 2. `blobs_dfs(g, conn, label, stack)` / `blobs_bfs(...)` count connected obstacle components with `conn` = 4 or 8 (`-1` otherwise). Components are numbered 1..k in row-major order of their first cell; both functions must produce identical `label` arrays.
> 3. Same map packed 1 bit per cell (512 B). `occ_reach_area` returns the reachable free area from `start`; `occ_plan` returns the same path as `grid_path` using a visited bitmap and a 2-bit "came-from direction" per cell instead of a 16-bit parent.
>
> Example (`S` start, `G` goal, `#` obstacle):
> ```
> S.#..
> ..#..
> .....
> ..#.G
> ```
> `dist(S, G) = 7`, path = `(0,0) (0,1) (1,1) (2,1) (2,2) (2,3) (2,4) (3,4)`, reachable area = 17, blobs: 2 with 4-connectivity, 2 with 8-connectivity.
>
> Constraints: `1 <= rows, cols <= 64`. Time O(rows·cols) per call. State the extra memory in bytes.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 7개 함수 본문 (`grid_bfs`, `grid_path`, `blobs_dfs`, `blobs_bfs`, `occ_bfs`, `occ_reach_area`, `occ_plan`). 스텁은 "아무 데도 못 감 / 덩어리 0개" 를 돌려준다. 실제 출력 (FAIL 줄 대부분 생략):

```text
M1 one cell        dist= -1 reach=  0 blobs4=0 blobs8=0  MISMATCH
FAIL starter.c:280  ok && check_grid(&g_grid, start, goal) == 0
…
M10 checkerboard   dist= -1 reach=  0 blobs4=0 blobs8=0  MISMATCH
…
house 64x64: free=3860 reach from dock=0 unreachable=3860, path dock->(62,62)=-2 moves, door opened: reach=0
1xn strips n=1..16: 131070 grids, mismatches vs C(n+1,2k)=88
4x4 exhaustive: 65536 grids, bad=65536, start blocked=65536, reach=16 (all free)=0
random: 3040 grids (3000 up to 24x24, 40 at 64x64), goal reachable in 1347, bad=3040
7/40 passed
```

(exit 1. 통과한 7개는 "장애물 위에서 시작하면 0 / -1", "지도 밖 칸", "빈 지도의 덩어리 0개", "닫힌 방 목표 = -1" 처럼 스텁의 0 / -1 이 우연히 정답인 경계 사례다.)

## 4. 힌트

<details><summary>힌트 1 — 표시는 큐에 넣을 때 한다</summary>

`dist[v] = dist[u] + 1` 을 **넣는 순간** 쓰면 그 자체가 방문 표시다. 그러면 칸마다 큐에 정확히 한 번 들어가므로 큐 크기 = 칸 수 4096 이면 끝이고, 링 버퍼도 필요 없다 (`head`, `tail` 이 4096 을 넘지 않는다). 꺼낼 때 표시하면 같은 칸이 여러 번 들어간다 (7. 실수 2, 실측 1.92배).

</details>

<details><summary>힌트 2 — 경로는 길이를 알고 있으니 뒤에서부터 쓴다</summary>

`len = dist[goal] + 1`. `v = goal` 에서 시작해 `path[len-1], path[len-2], …, path[0]` 순으로 쓰면서 `v = parent[v]`. 마지막에 `v` 는 start 의 parent 인 -1 이 된다. 뒤집기(reverse) 단계가 필요 없고, cap 검사를 쓰기 **전에** 할 수 있다.

</details>

<details><summary>힌트 3 — 덩어리 세기는 "행 우선 훑기 + 씨앗마다 채우기"</summary>

모든 칸을 행 우선으로 보다가 "장애물인데 라벨이 없는" 칸을 만나면 `count++` 하고 그 칸에서 채우기(flood)를 한다. DFS 판은 스택(`stack[sp++]`, `stack[--sp]`), BFS 판은 큐(`queue[tail++]`, `queue[head++]`) — 나머지는 한 글자도 다르지 않다. 8-연결은 이웃 표의 앞 4개 + 대각선 4개를 쓰는 것뿐이다.

</details>

<details><summary>힌트 4 — 2비트 방향으로 parent 를 대신하기</summary>

`dir_set(ws->dir, v, k)`: v 를 발견할 때 쓴 방향 k (0 위, 1 오른쪽, 2 아래, 3 왼쪽). 되돌아갈 때는 `v -= DID[k]` (`DID = {-64, +1, +64, -1}`). start 의 dir 는 쓰지 않으므로 루프 조건은 `v != start`. 길이를 모르니 한 번 따라가며 세고, cap 검사 후 다시 따라가며 뒤에서부터 채운다.

</details>

<details><summary>힌트 5 — 테스트의 기준 구현은 왜 "완화 반복" 인가</summary>

BFS 를 BFS 로 검사하면 같은 실수를 두 번 한다. `d[v] = min(d[v], d[u] + 1)` 을 모든 칸·이웃에 대해 **바뀌지 않을 때까지** 반복하면 큐도 순서도 없어서 틀릴 곳이 없다 (Bellman–Ford 의 격자판, 일반형은 ALGO-A02). 대가는 시간: 뱀 복도에서 1010번 훑는다 (5장 측정).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 이동은 4방향? 8방향이면 모서리 자르기 허용? 시작/목표가 장애물·지도 밖이면? 시작 = 목표? 경로가 여러 개면 아무거나? 지도 최대 크기(→ 인덱스 타입), RAM 예산? 재귀 허용? |
| 2. 무식한 방법 | 모든 경로 DFS 열거 → 지수 시간. 또는 거리 완화를 바뀌지 않을 때까지 반복 → O(V·D) (D = 지름). 후자는 **테스트 기준**으로 남긴다 |
| 3. 더 나은 생각 + 불변식 | 간선 가중치가 모두 1 → BFS. 큐 안 거리는 [d, …, d, d+1, …, d+1] 이라 꺼내는 순서 = 거리 순서 → 처음 발견이 최단 |
| 4. 코드 | `dist = -1` 초기화, 넣을 때 표시, 이웃 4개 범위 검사, parent 기록, 뒤에서부터 경로 쓰기 |
| 5. 소리 내어 말할 테스트 | 1×1, 일자 복도, 목표가 갇힘, 시작이 장애물, 대각선 장애물(4 vs 8), 체커보드(덩어리 최대), 뱀 복도(경로 최장), 빈 64×64 (126) |
| 6. 복잡도 | 시간 O(R·C) — 칸마다 큐에 한 번, 이웃 검사 4번. 추가 메모리: 바이트판 24 KB, 비트판 9.5 KB. 스택 O(1) (재귀 없음) |

### Part A — BFS 가 왜 최단인가: 큐 불변식

δ(v) 를 진짜 최단 이동 수라고 하자. 증명할 것: BFS 가 쓰는 `dist[v]` = δ(v).

1. **dist 는 실제 경로의 길이다.** `dist[v] = dist[u] + 1` 은 u 까지의 경로에 간선 u→v 를 붙인 것이므로 귀납적으로 `dist[v] ≥ δ(v)`.
2. **큐 불변식.** 큐에 든 칸들의 dist 를 앞에서 뒤로 읽으면 비내림차순이고 (마지막 − 처음) ≤ 1. 처음엔 [0] 하나라 성립. 앞에서 d 인 u 를 꺼내 d+1 인 칸들을 뒤에 붙인다. 꺼내기 전 큐가 [d, …, d, d+1, …] 였으니 붙인 뒤에도 [d, …, d+1, …, d+1] — 성립.
3. 2 에서 바로: **칸은 dist 오름차순으로 꺼내진다.**
4. **dist ≤ δ.** δ 에 대한 귀납. δ(v) = k+1 이면 최단 경로에서 v 바로 앞 칸 u 는 δ(u) = k 이고 귀납 가정으로 `dist[u] = k`. u 를 꺼낼 때 v 가 아직 미발견이면 `dist[v] = k+1`. 이미 발견됐다면 v 를 발견한 칸 w 는 u 보다 **먼저** 꺼내졌으니 (3) `dist[w] ≤ dist[u] = k` → `dist[v] = dist[w] + 1 ≤ k+1`.

1 과 4 로 `dist = δ`. 도달 불가 칸은 아무도 발견하지 않으므로 -1 로 남는다. 이 증명에서 핵심은 **"한 번 정해진 dist 는 다시 고칠 필요가 없다"** 는 3 이다 — 스택(LIFO)으로 바꾸면 3 이 깨져 처음 발견이 최단이 아니게 된다 (7. 실수 3, 실측).

**경로 복원.** `parent[v]` 는 `dist[parent[v]] = dist[v] − 1` 인 칸이다. 그러니 goal 에서 parent 를 따라가면 매 걸음 dist 가 1 씩 줄어 정확히 `dist[goal]` 걸음 만에 start(dist 0) 에 닿는다 → 길이 `dist[goal]` 의 경로 = 최단 경로. parent 포인터 전체는 start 를 뿌리로 하는 **최단 경로 트리(BFS 트리)** 다.

손 추적 — 3×4 지도, 시작 (0,0), 이웃 순서 위·오·아래·왼 (실제 실행 출력에서 옮김):

```text
S . . #
. # . .
. . . G
```

| 꺼낸 칸 (dist) | 새로 넣은 칸 = dist | 큐 (앞 → 뒤) |
|---|---|---|
| (0,0) 0 | (0,1)=1, (1,0)=1 | (0,1)¹ (1,0)¹ |
| (0,1) 1 | (0,2)=2 | (1,0)¹ (0,2)² |
| (1,0) 1 | (2,0)=2 | (0,2)² (2,0)² |
| (0,2) 2 | (1,2)=3 — (0,3) 은 장애물 | (2,0)² (1,2)³ |
| (2,0) 2 | (2,1)=3 | (1,2)³ (2,1)³ |
| (1,2) 3 | (1,3)=4, (2,2)=4 | (2,1)³ (1,3)⁴ (2,2)⁴ |
| (2,1) 3 | 없음 — (2,2) 는 이미 4 | (1,3)⁴ (2,2)⁴ |
| (1,3) 4 | (2,3)=5 | (2,2)⁴ (2,3)⁵ |
| (2,2) 4 | 없음 | (2,3)⁵ |
| (2,3) 5 | 없음 | (빔) |

어느 줄에서도 큐 안 위첨자는 두 값 이하이고 비내림차순이다. (2,1) 에서 (2,2) 를 다시 보지만 이미 4 라서 건너뛴다 — 여기서 4 가 3+1 과 같으니 다른 최단 경로가 있다는 뜻이다 (경로 수 세기는 연습 1).

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m04a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">BFS 거리 층과 parent 경로 (3×4 손 추적 지도)</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="50" height="50" class="f-acc"/><rect x="90" y="50" width="50" height="50" class="f-acc"/><rect x="140" y="50" width="50" height="50" class="f-acc"/><rect x="190" y="50" width="50" height="50" class="f-warn"/>
    <rect x="40" y="100" width="50" height="50" class="f-ok"/><rect x="90" y="100" width="50" height="50" class="f-warn"/><rect x="140" y="100" width="50" height="50" class="f-acc"/><rect x="190" y="100" width="50" height="50" class="f-acc"/>
    <rect x="40" y="150" width="50" height="50" class="f-ok"/><rect x="90" y="150" width="50" height="50" class="f-ok"/><rect x="140" y="150" width="50" height="50" class="f-ok"/><rect x="190" y="150" width="50" height="50" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="48" y="68">0</text><text x="98" y="68">1</text><text x="148" y="68">2</text>
    <text x="48" y="118">1</text><text x="148" y="118">3</text><text x="198" y="118">4</text>
    <text x="48" y="168">2</text><text x="98" y="168">3</text><text x="148" y="168">4</text><text x="198" y="168">5</text>
  </g>
  <polyline points="215,185 215,135 165,135 165,85 115,85 70,85" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#m04a)"/>
  <text x="40" y="222" fill="currentColor">강조 칸 = parent 를 따라 G(2,3) → S(0,0) 로 되돌아간 경로, 길이 5.</text>
  <text x="40" y="242" fill="currentColor">숫자 = dist. 같은 숫자 = 같은 층. 주황 = 장애물.</text>
  <text x="290" y="60" fill="currentColor" font-weight="bold">큐 불변식</text>
  <g stroke="currentColor">
    <rect x="290" y="72" width="60" height="30" class="f-ok"/><rect x="350" y="72" width="60" height="30" class="f-ok"/><rect x="410" y="72" width="60" height="30" class="f-ok"/>
    <rect x="470" y="72" width="60" height="30" class="f-acc"/><rect x="530" y="72" width="60" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="320" y="92">d</text><text x="380" y="92">d</text><text x="440" y="92">d</text><text x="500" y="92">d+1</text><text x="560" y="92">d+1</text>
  </g>
  <text x="290" y="125" fill="currentColor">앞(head)에서 d 를 꺼내 → 뒤(tail)에 d+1 만 붙인다.</text>
  <text x="290" y="147" fill="currentColor">그래서 꺼내는 순서 = 거리 순서, 처음 발견 = 최단.</text>
  <text x="290" y="177" fill="currentColor" font-family="monospace">pop (1,2)³ → 큐: (2,1)³ (1,3)⁴ (2,2)⁴</text>
  <text x="290" y="199" fill="currentColor">스택(LIFO)으로 바꾸면 이 순서가 깨진다 → 실수 3.</text>
</svg>
```

**복잡도.** 칸마다 큐에 최대 한 번 (넣을 때 표시) → 꺼내기 ≤ V = R·C, 이웃 검사 ≤ 4V. 초기화 O(V). 시간 Θ(V) 최악. 추가 메모리 `dist + parent + queue` = 3 × 4096 × 2 B = **24,576 B**, 스택 O(1). 목표 하나만 필요하면 goal 을 발견하는 순간 멈춰도 된다 (Part C 의 `stop_at`) — 최악은 같지만 평균이 준다.

### Part B — 덩어리 세기: 같은 채우기, 다른 자료구조

`blobs_dfs` 의 정확성: 씨앗 s 에서 채운 집합 F 가 s 의 연결 요소 K 와 같다.

- F ⊆ K: 스택에 넣는 칸은 이미 F 에 있는 칸의 이웃 장애물뿐 → 귀납으로 모두 s 와 연결.
- K ⊆ F: K 안 칸 v 와 s 를 잇는 경로 s = x₀, x₁, …, x_m = v 를 잡는다. x_i 가 라벨을 받으면 언젠가 스택에서 꺼내지고, 그때 이웃 x_{i+1} 은 (아직 없었다면) 라벨을 받는다 → 귀납으로 v 도 라벨.
- 라벨은 넣을 때 붙이므로 칸마다 한 번만 들어간다 → **스택 깊이 ≤ 장애물 칸 수 ≤ 4096**. 재귀판은 같은 상한이지만 단위가 "프레임" 이다 (ALGO-B11 의 실측: 64×64 열린 바닥에서 재귀 깊이 2083).

BFS 판은 증명이 같고 꺼내는 순서만 다르다. 행 우선 훑기에서 씨앗이 나오는 순서가 같으니 **라벨 번호도 같다** — 테스트는 두 `label` 배열과 기준 구현이 만든 기대 라벨 셋을 `memcmp` 한다. 덩어리 하나를 채우는 데 O(크기 × conn), 훑기 O(V) → 전체 O(V).

**4-연결 vs 8-연결 — 어느 것을 써야 하나.** 디지털 위상(digital topology)의 고전 결과(Rosenfeld)로, 전경과 배경에 **같은** 연결성을 쓰면 "닫힌 곡선이 안과 밖을 나눈다" 는 성질이 깨진다. 짝을 맞춰야 한다: 빈칸을 4-연결로 보면 장애물은 8-연결로, 그 반대도 마찬가지. 로봇 말로 하면

| 로봇 이동 | 빈 공간 연결 | 장애물 "벽" 을 셀 때 |
|---|---|---|
| 4방향 | 4-연결 | **8-연결** — 대각선으로만 닿은 장애물 사이로도 못 지나간다 (M5, M8) |
| 8방향, 모서리 자르기 허용 | 8-연결 | 4-연결 — 대각선 틈으로 빠져나간다 |
| 8방향, 모서리 자르기 금지 | 도달 가능성은 4방향과 같음 (거리만 다름) | 8-연결 |

마지막 줄의 이유: 모서리 자르기를 금지하면 대각선 이동 (r,c)→(r+dr,c+dc) 은 (r+dr,c) 와 (r,c+dc) 가 둘 다 빈칸일 때만 되는데, 그러면 같은 두 칸이 직교 두 걸음으로도 이어진다. 연결 요소는 4방향과 같고 거리만 짧아진다 (연습 3).

M8 이 좋은 예다 (LeetCode 200 예제 2 를 장애물로 그린 것): 4-연결 덩어리 3개라서 "사이로 지나갈 수 있겠다" 싶지만 8-연결로는 1개이고, 실제로 S 에서 G 로 못 간다.

### 손 지도 10개 — 기댓값을 손으로 센 방법

테스트의 `HAND[]` 표와 같은 숫자다. 전부 맞는지는 solution 실행으로 확인했고(6장), 기준 구현 대조(`check_grid`)도 지도마다 같이 돈다.

| 지도 | 모양 | dist S→G | reach | 4-덩어리 | 8-덩어리 | 손으로 센 근거 |
|---|---|---|---|---|---|---|
| M1 | `S` | 0 | 1 | 0 | 0 | 시작 = 목표 |
| M2 | `S...G` | 4 | 5 | 0 | 0 | 일자 복도 |
| M3 | 2장 예제 | 7 | 17 | 2 | 2 | 맨해튼 3+4 = 7 인 경로가 2행 구멍으로 지나감. 20 − 장애물 3 |
| M4 | 목표가 고리 안 | −1 | 7 | 1 | 1 | 8칸 고리는 4-연결로도 한 덩어리 |
| M5 | 대각선 장애물 | 3 | 6 | 6 | 1 | 위 그림. 빈칸 10 중 4칸 갇힘 |
| M6 | 뱀 모양 5×6 | 19 | 20 | 2 | 2 | 빈칸 20개가 한 줄 복도 → 경로 유일(칸열까지 검사) |
| M7 | 방 + 2×2 기둥 | 8 | 20 | 1 | 1 | 맨해튼 3+5. 최단 경로 11개 (연습 1) |
| M8 | LC200 예제 2 | −1 | 8 | 3 | 1 | 왼쪽 아래 5칸이 8-연결 사슬에 갇힘 |
| M9 | LC200 예제 1 | 4 | 11 | 1 | 1 | 20 − 9 |
| M10 | 4×4 체커보드 | −1 | 1 | 8 | 1 | 빈칸 8개가 모두 4-고립 |

### Part C — 메모리를 줄이는 세 가지 표현

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">작업 메모리 (64×64, 막대 1px ≈ 51 B) — demo_bench.c 의 sizeof 출력</text>
  <g fill="currentColor"><text x="10" y="58">V1 바이트판</text><text x="10" y="108">V3 비트판</text><text x="10" y="158">V4 비트보드</text></g>
  <g stroke="currentColor">
    <rect x="110" y="42" width="160" height="26" class="f-warn"/><rect x="270" y="42" width="160" height="26" class="f-warn"/><rect x="430" y="42" width="160" height="26" class="f-mut"/>
    <rect x="110" y="92" width="10" height="26" class="f-ok"/><rect x="120" y="92" width="20" height="26" class="f-ok"/><rect x="140" y="92" width="160" height="26" class="f-mut"/><rect x="300" y="92" width="10" height="26" class="f-acc"/>
    <rect x="110" y="142" width="40" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="60">dist 8 KB</text><text x="305" y="60">parent 8 KB</text><text x="470" y="60">queue 8 KB</text><text x="600" y="60">= 24,576 B</text>
    <text x="175" y="110">queue 8 KB</text><text x="320" y="110">seen 512 + dir 1 KB + 큐 = 9,728 B (+ 지도 512)</text>
    <text x="160" y="160">지도·방문·프런티어·다음 = 4 × 512 = 2,048 B (경로 없음)</text>
  </g>
  <text x="10" y="200" fill="currentColor" font-weight="bold">dir 바이트 하나 = 칸 4개 × 2비트</text>
  <g stroke="currentColor">
    <rect x="110" y="212" width="80" height="28" class="f-mut"/><rect x="190" y="212" width="80" height="28" class="f-mut"/><rect x="270" y="212" width="80" height="28" class="f-mut"/><rect x="350" y="212" width="80" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="150" y="231">[7:6] 4j+3</text><text x="230" y="231">[5:4] 4j+2</text><text x="310" y="231">[3:2] 4j+1</text><text x="390" y="231">[1:0] 4j</text>
  </g>
  <text x="450" y="224" fill="currentColor" font-size="12">k: 0 위 · 1 오른쪽 · 2 아래 · 3 왼쪽</text>
  <text x="450" y="244" fill="currentColor" font-size="12" font-family="monospace">parent = v - DID[k]</text>
  <text x="110" y="262" fill="currentColor" font-size="12">dist 는 저장하지 않는다: 필요한 건 "어디서 왔나" 뿐이고 길이는 되돌아가며 센다.</text>
</svg>
```

바이트판(V1)의 24 KB 는 SRAM 20 KB 짜리 MCU(예: STM32F103C8)에는 아예 안 들어간다. 줄이는 순서:

1. **지도 1비트/칸**: 4096 B → 512 B. 비트 읽기는 `(b[i>>3] >> (i&7)) & 1` (DS-B08 비트맵).
2. **dist 를 버린다**: 방문 여부만 알면 BFS 는 돈다 → `seen` 비트맵 512 B. 거리가 필요하면 경로 길이 − 1.
3. **parent 를 2비트 방향으로**: 4방향 중 하나이므로 2비트면 충분 → 1 KB (int16 parent 의 1/8). `dir` 는 초기화하지 않는다 — `seen` 인 칸만 읽으니까.
4. **큐는 그대로 8 KB**: 칸마다 한 번 들어가는 보장이 상한이다. 열린 바닥에서 실측 큐 피크는 훨씬 작지만(ALGO-B11 Part C: 64×64 가운데 127 엔트리) "이 지도 모양에서는 작다" 는 보장이 없으면 cap 을 줄일 수 없다. 줄이고 싶으면 넘침을 에러로 보고하는 링 버퍼(DS-B04)와 재시도 경로를 둔다 (B11 Q5).

`occ_plan` 은 `stop_at = goal` 로 조기 종료한다. 발견 순서가 `grid_bfs` 와 같고(같은 이웃 순서) 방향은 발견 때 한 번 쓰이므로 되돌아간 경로는 `grid_path` 와 **같은 칸열**이다 — 테스트가 4×4 전수·무작위·뱀 복도(2079칸)에서 `memcmp` 로 확인한다. 실행 결과(집 시나리오):

```text
house 64x64: free=3860 reach from dock=3460 unreachable=400, path dock->(62,62)=122 moves, door opened: reach=3862
```

손 계산: 벽 4줄 × 64 − 교차 4 = 252칸, 빈칸 4096 − 252 + 문 8개 × 2 = 3860. 가운데 방 20×20 = 400 이 갇혀 3460. 문 2칸을 열면 3862 전부. 경로 122 = 맨해튼 61 + 61 — 문 위치가 오른쪽·아래로만 가는 경로를 허용한다 ((0,0)방 → 9·10행 문 → (0,1)방 → (0,2)방 → 52·53열 문으로 두 번 내려감).

**측정 — `demo_bench.c`** (64×64 네 지도, 네 구현 + 기준 구현. 이 맥 Apple Silicon M-series, `-O2`, 워밍업 후 101 표본 중앙값, 표본 = 50회 평균. 세 번 돌려 비슷한 값을 확인했고 아래는 그중 하나. 절대값보다 **비율**을 본다.)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

```text
work memory: V1 24576 B, V2 26136 B, V3 9728 B (+ map 512 B), V4 2048 B
map              reach  dist layers |    V1 us    V2 us    V3 us    V4 us |    REF us sweeps
open 64x64        4096   126    127 |     8.28     5.84    12.60     9.04 |      11.0      2
random 30%        2734    64     72 |     6.38     6.18     8.20     5.38 |     601.0     62
serpentine        2079  2078   2079 |     6.12     4.10     7.02   157.58 |    5433.0   1010
house (9 rooms)   3460   122    125 |     7.68     4.76    10.74     9.22 |     126.0     11
(all variants agree on reach and dist for every map)
```

- **V2 센티널**(66×66, 바깥 한 겹을 장애물로 채움): 범위 검사 4개가 사라져 V1 보다 1.0~1.6배 빠르다. 대가는 지도가 4356칸이 되는 것(메모리 6% 증가)과, 지도 갱신 때 테두리를 지켜야 하는 것.
- **V3 비트판**: 메모리 2.5배 적고 시간은 V1 의 1.1~1.5배 (비트 시프트·마스크가 칸 접근마다 붙는다). 펌웨어에서 흔한 맞바꿈이다.
- **V4 비트보드 층 BFS**: 행 하나를 `uint64_t` 로 두고 한 층을 `next[r] = (f[r]<<1 | f[r]>>1 | f[r-1] | f[r+1]) & free[r] & ~seen[r]` 로 한꺼번에 민다. 큐가 없어 2 KB. 비용은 **층 수 × 64행** 이라 층이 적은 지도(열린 바닥·집·무작위)에서는 V1 의 0.8~1.2배로 비슷하고, 층이 2079개인 뱀 복도에서는 V1 의 약 26배로 느려진다. 거리·면적만 나오고 경로는 없다(층별 비트맵을 저장하면 되지만 그만큼 메모리).
- **REF(완화 반복)**: 훑는 횟수가 "최단 경로가 훑는 방향을 거스르는 횟수" 에 따라 2회~1010회. 열린 바닥에서는 좌상단 시작이라 행 우선 훑기 한 번에 거의 끝나지만, 뱀 복도에서는 BFS 의 약 900배 느리다. 테스트 기준으로는 좋지만 펌웨어 코드로는 쓸 수 없는 이유.

Cortex-M4 에서 V4 의 한 행 계산은 64비트 시프트가 32비트 두 워드로 쪼개진다. `demo_cm4_row.c` 를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 로 본 실제 출력 중 시프트 부분:

```text
lsl.w   r4, lr, #1          @ hi << 1
orr.w   r6, r4, r0, lsr #31 @ | lo >> 31  (넘어오는 비트)
lsrs.w  r4, lr, #1          @ hi >> 1, 밀려난 비트 → C 플래그
rrx     r5, r0              @ lo >> 1 에 C 를 넣는다
orr.w   r0, r5, r0, lsl #1
```

함수 전체가 Cortex-M4 29개 명령, arm64 13개 명령이었다 (같은 파일, `clang -O2 -S`). 32비트 코어에서는 지도 폭을 32열 단위로 나누는 게 자연스럽다. MCU 실측 시간은 이 레슨에서 재지 않았다.

### 복잡도 요약

| 함수 | 시간 (최악) | 추가 메모리 (64×64) | 스택 |
|---|---|---|---|
| `grid_bfs`, `grid_path` | Θ(V), 이웃 검사 ≤ 4V | 24,576 B (dist·parent·queue int16) | O(1) |
| `blobs_dfs` / `blobs_bfs` | Θ(V·conn) | label 8 KB + 스택/큐 8 KB | O(1) |
| `occ_bfs`, `occ_reach_area`, `occ_plan` | Θ(V) | 9,728 B + 지도 512 B | O(1) |
| 비트보드 층 BFS (demo) | Θ(층 수 × R) 워드 연산 | 2,048 B | O(1) |
| 기준 구현(완화 반복) | O(V × 훑기 수), 훑기 ≤ V | 16 KB (int32 dist) | O(1) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 40/40 passed, 약 0.8초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 40/40, 보고 없음
```

실제 출력:

```text
M1 one cell        dist=  0 reach=  1 blobs4=0 blobs8=0  ok
M2 corridor        dist=  4 reach=  5 blobs4=0 blobs8=0  ok
M3 wall with gap   dist=  7 reach= 17 blobs4=2 blobs8=2  ok
M4 goal enclosed   dist= -1 reach=  7 blobs4=1 blobs8=1  ok
M5 diagonal seal   dist=  3 reach=  6 blobs4=6 blobs8=1  ok
M6 serpentine      dist= 19 reach= 20 blobs4=2 blobs8=2  ok
M7 room + pillar   dist=  8 reach= 20 blobs4=1 blobs8=1  ok
M8 LC200 ex2       dist= -1 reach=  8 blobs4=3 blobs8=1  ok
M9 LC200 ex1       dist=  4 reach= 11 blobs4=1 blobs8=1  ok
M10 checkerboard   dist= -1 reach=  1 blobs4=8 blobs8=1  ok
M3 path: (0,0) (0,1) (1,1) (2,1) (2,2) (2,3) (2,4) (3,4)
house 64x64: free=3860 reach from dock=3460 unreachable=400, path dock->(62,62)=122 moves, door opened: reach=3862
1xn strips n=1..16: 131070 grids, mismatches vs C(n+1,2k)=0
4x4 exhaustive: 65536 grids, bad=0, start blocked=32768, reach=16 (all free)=1
random: 3040 grids (3000 up to 24x24, 40 at 64x64), goal reachable in 1347, bad=0
40/40 passed
```

테스트 구성:

- **손 지도 10개**: 위 표의 숫자를 `HAND[]` 에 손으로 적어 두고 비교 + 같은 지도에서 기준 구현 전체 대조. M6 은 경로 칸열 20개까지 비교.
- **1×n 띠 전수** (n = 1..16, 131,070개): 길이 n 0/1 문자열에서 1 의 런(run)이 k 개인 것의 수는 C(n+1, 2k) (런의 시작·끝 2k 개 경계를 n+1 개 틈에서 고르는 것). 덩어리 히스토그램이 이 공식과 같아야 한다. 1행이라 4-연결과 8-연결이 같아야 한다는 것도 같이 본다.
- **4×4 전수** (65,536개): 시작 (0,0), 목표 (3,3) 로 모든 함수를 기준 구현과 대조. 시작이 막힌 격자가 정확히 절반(32,768), 전부 빈 격자만 reach 16.
- **무작위** 3,040개 (24×24 이하 3,000개, 64×64 40개, 밀도 0~60%): `dist` 배열 전체, 경로 유효성(시작·끝·빈칸·4-이웃·길이), `occ_plan` = `grid_path` 칸열, DFS/BFS × 4/8 라벨 배열.
- **기준 구현** 두 개: 거리 완화 반복, 라벨 전파(장애물 칸마다 자기 id 에서 시작해 이웃 최솟값을 받는 것을 바뀌지 않을 때까지 → 덩어리 라벨 = 행 우선 첫 칸 id → 1..k 로 번호 매김).

## 7. 자주 하는 실수

`demo_bugs.c` 가 실수 1~3 을 무작위 격자 2000개(2..24 × 2..24, 밀도 0~40%)에서 기준 구현과 대조한다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
```

```text
2000 random grids (2..24 x 2..24, density 0-40%)
B1 id+-1 row wrap   : wrong dist arrays 1794
B2 mark on pop      : wrong dist arrays 0, pushes > free cells in 1576 grids, worst pushes/free = 1105/576 = 1.92
B3 stack instead    : wrong dist arrays 1729
B1 2x3 from (0,2): true d(1,0)=3, buggy d(1,0)=1
B3 3x3 from (0,0): true | stack
  0 1 2 | 0 1 6
  1 2 3 | 1 2 5
  2 3 4 | 2 3 4
```

**1. 평평한 인덱스에서 `id ± 1` 로 좌우 이웃 (B1).** 행 폭이 cols 인 배열에서 행 끝 칸의 `id + 1` 은 다음 행 첫 칸이다 → 지도 오른쪽 끝에서 왼쪽 끝으로 "순간이동". 2×3 에서 (0,2)→(1,0) 진짜 거리 3 이 1 이 됐고, 무작위 격자 90% 가 틀렸다. 위·아래(`id ± cols`)는 배열 범위 검사로 막히지만 좌우는 안 막힌다. 고침: `(r, c)` 로 범위 검사, 또는 센티널 테두리(demo_bench V2). 이 레슨의 행 폭 64 고정도 같은 함정이 있다: `cols < 64` 면 `c + 1 == cols` 칸이 배열 안이라 `id < 4096` 검사로는 못 막는다 — 그래서 `valid_id` 가 `COL(id) < cols` 를 본다 (테스트 "지도 밖 칸" 사례).

**2. 꺼낼 때 방문 표시 (B2).** 거리는 맞는다 (0 오류) — 그래서 위험하다. 같은 칸이 여러 이웃에게서 큐에 들어가 push 총량이 빈칸 수의 최대 1.92배였다 (2000개 중 1576개에서 칸 수 초과). 큐를 칸 수로 잡은 펌웨어라면 **넘친다**. 상한은 1 + 4V. 고침: 넣을 때 표시 (ALGO-B11 Q4).

**3. 큐 대신 스택으로 "처음 발견 = 거리" (B3).** 빈 3×3 에서 (0,2) 가 2 가 아니라 6. LIFO 는 깊이 먼저 파고들어 먼 길로 먼저 발견한다 — Part A 증명의 3 단계(거리 순서로 꺼냄)가 깨진다. 덩어리 세기·도달 면적은 순서와 무관해서 스택이 괜찮지만, **거리**는 반드시 FIFO.

**4. 시작/목표 검사를 빼먹음.** 시작이 장애물인데 검사하지 않으면 BFS 는 시작 칸의 빈 이웃으로 그대로 퍼져, 벽 속에서 출발한 경로와 면적을 답한다 (테스트는 이 경우 0 / -1 을 요구한다). 목표가 지도 밖이면 `dist[goal]` 이 쓰레기 칸을 읽는다. 고침: 함수 입구에서 `valid_id` + 장애물 검사 → 0 / -1.

**5. 경로를 앞에서부터 쓰고 cap 을 나중에 검사.** parent 를 따라가면 goal → start 순서라 뒤집기가 필요하고, 길이를 모르는 채 쓰면 cap 을 넘어 쓴다. 고침: `len = dist[goal] + 1` 을 먼저 구해 cap 과 비교하고 `path[len-1]` 부터 채운다. 테스트가 `cap = 7` 일 때 `-2` 와 `path[7]` 미변경을 본다.

**6. 연결성을 문제와 다르게 고름.** "섬 개수" 류 문제는 대개 4-연결이 기본이지만, 로봇 지도에서 "벽이 몇 개냐 / 지나갈 틈이 있냐" 는 이동 규칙의 **쌍대** 연결성으로 세야 한다 (Part B 표). 면접에서는 반드시 먼저 묻는다.

**7. 라벨·거리 타입이 작음.** 체커보드 64×64 는 덩어리 2048개, 뱀 복도는 거리 2078 → `int8_t`/`uint8_t` 로는 넘친다. 64×64 면 `int16_t` 로 충분하다 (최대 4096).

## 8. 면접 Q&A

<details><summary>Q1. BFS 가 최단 거리를 준다는 걸 증명해 보세요. DFS 는 왜 안 되죠?</summary>

큐 안 거리값이 [d, …, d, d+1, …, d+1] 꼴(비내림차순, 폭 ≤ 1)이라는 불변식 → 칸이 거리 순서로 꺼내짐 → δ 에 대한 귀납으로 "처음 발견 때 쓴 dist = δ" (5장 Part A 의 4단계). DFS 는 LIFO 라 꺼내는 순서가 거리 순서가 아니고, 처음 발견한 경로가 우회로일 수 있다 — 빈 3×3 에서 (0,2) 를 6 으로 적었다 (demo_bugs B3). DFS 로 최단을 구하려면 모든 경로를 보고 최솟값을 고쳐 가야 해서 최악 지수 시간. 꼬리: "가중치가 있으면?" → 1 이 아니면 다익스트라(ALGO-M06), 0 과 1 뿐이면 덱으로 0-1 BFS (연습 4), 음수가 있으면 벨만-포드(ALGO-A02).

</details>

<details><summary>Q2. 큐 크기는 얼마로 잡나요? 링 버퍼가 필요한가요?</summary>

넣을 때 표시하면 칸마다 한 번 → 총 push ≤ V, 그래서 `head`/`tail` 이 V 를 넘지 않는 **평평한 배열**로 충분하고 링(모듈러 인덱스)이 필요 없다. 동시에 큐에 있는 최대 개수(피크)는 지도에 따라 훨씬 작다 (B11 실측: 열린 64×64 가운데 127). 피크 크기의 링 버퍼로 줄이려면 그 지도 종류에서 피크 상한을 **증명**하거나, 넘침을 감지해 에러로 보고하고 재시도하는 경로가 있어야 한다. 꺼낼 때 표시하는 판은 상한이 1 + 4V 로 커진다 (실측 1.92V). 꼬리: "BFS 대신 DFS 면?" → 스택도 넣을 때 표시면 ≤ V, 덩어리 세기·면적에는 DFS 로 충분.

</details>

<details><summary>Q3. 4-연결과 8-연결 중 무엇을 써야 하죠?</summary>

문제에 달렸고, 로봇이면 **이동 규칙의 쌍대**다. 4방향 로봇은 대각선으로만 닿은 두 장애물 사이를 못 지나가므로 장애물을 8-연결로 세야 "벽" 개수가 맞는다 (M5: 4-연결 6개, 8-연결 1개, 실제로 빈칸 4개가 갇힘). 전경·배경을 같은 연결성으로 보면 이산 조르당 곡선 정리가 깨진다는 Rosenfeld 의 결과가 근거다. 8방향 + 모서리 자르기 금지는 도달 가능성이 4방향과 같다 (대각선 한 번 = 직교 두 번으로 대체 가능) — 거리만 다르다 (연습 3 이 빈 격자에서 체비셰프 거리와 대조). 꼬리: "덩어리를 DSU 로 세면?" → 행 우선 두 번 훑기(two-pass labeling)로 왼쪽·위 이웃만 합치면 된다 (ALGO-M14).

</details>

<details><summary>Q4. 목표가 하나면 BFS 대신 A* 를 써야 하지 않나요?</summary>

A* 는 맨해튼 거리처럼 허용 가능한(admissible) 휴리스틱으로 목표 방향을 먼저 넓혀 방문 칸을 줄인다 — 자세한 것은 ALGO-A01. 대가는 우선순위 큐(힙)와 g/f 값 저장이라 메모리와 코드가 늘고, 최악(목표가 막힌 경우)은 결국 전체를 본다. 64×64 BFS 는 이 맥에서 수 µs (demo_bench) 수준이라, 작은 지도에서는 BFS + 조기 종료(`occ_plan` 의 `stop_at`)가 단순하고 최악 시간이 결정적이다. 그 밖의 선택지: 양방향 BFS(양쪽 프런티어가 만날 때까지, 방문 칸이 대략 절반 반경 두 개), 목표에서 한 번 역방향 BFS 를 돌려 **거리장(distance field)** 을 만들어 두면 로봇이 어디 있든 "dist 가 1 작은 이웃으로" 내려가기만 하면 된다 — 목표가 고정이고 로봇이 자주 재계획할 때 좋다.

</details>

<details><summary>Q5. 센서가 지도를 계속 바꿉니다. 매번 BFS 를 처음부터 다시 돌리나요?</summary>

64×64 면 대개 그렇다. Θ(V) 고정 비용이라 최악 시간을 예산에 넣기 쉽다. 점진적 재계획 알고리즘(D* Lite, LPA* 계열)이 있지만 상태가 크고 코드가 복잡해서, 작은 MCU 지도에서는 "주기마다 전체 BFS" 가 흔한 선택이다 (확인 권장: 쓰는 플랫폼의 계획 주기·지도 크기로 직접 재 볼 것). 줄이는 요령: (1) 바뀐 칸이 현재 경로 위에 있을 때만 재계획, (2) 지도 갱신과 계획을 이중 버퍼로 분리(계획 중에 지도가 바뀌면 일관성이 깨진다 — ISR/다른 태스크가 지도를 쓰는 경우 특히), (3) 조기 종료. 이 레슨에서 MCU 실측은 하지 않았다.

</details>

<details><summary>Q6. 덩어리 세기를 재귀 DFS 로 짜면 안 되나요?</summary>

재귀 깊이가 덩어리 크기까지 커진다. 64×64 꽉 찬 지도나 뱀 복도면 수천 프레임 — Cortex-M 태스크 스택 몇 KB 로는 넘친다 (ALGO-B11 에서 실제로 SIGSEGV/SIGBUS 를 냈고, 프레임 크기도 쟀다). MISRA C:2012 Rule 17.2 도 재귀를 금지한다 (C-A10). 명시적 스택 배열(DS-B02)을 쓰고 넣을 때 표시하면 상한이 칸 수로 정해진다. 꼬리: "스택 4096 엔트리도 아까우면?" → 스캔라인(행 구간 단위) 채우기, 또는 DSU 두 번 훑기(ALGO-M14)는 보조 스택이 필요 없다.

</details>

<details><summary>Q7. 최단 경로가 여러 개인데 어떤 게 나오나요? 경로를 어떻게 저장·전송하죠?</summary>

이웃 순서(위·오·아래·왼)와 발견 순서가 정하는 BFS 트리의 경로 하나가 결정적으로 나온다. M7(방 + 기둥)에는 최단 경로가 11개 있고 (연습 1) 그중 "위·오른쪽을 먼저 시도한" 트리의 것이 나온다. 회전이 적은 경로를 원하면 상태를 (칸, 들어온 방향) 으로 넓혀 회전에 비용을 주는 탐색이 된다 (상태 4V). 저장은 2비트 방향열(chain code)이 작다: 2078걸음 뱀 복도 경로가 int16 칸 id 로는 4,158 B, 2비트 방향으로는 520 B. 시작 칸 하나 + 방향열이면 복원된다 — Part C 의 `dir` 배열이 같은 아이디어다.

</details>

<details><summary>Q8. 이걸 Cortex-M 에 올리면 무엇을 바꾸겠어요?</summary>

(1) 메모리: 바이트판 24 KB → 비트 지도 + `seen` + 2비트 방향 9.5 KB (+ 지도 0.5 KB). 큐 8 KB 가 남은 최대 항목. 거리·면적만 필요하면 비트보드 층 BFS 로 2 KB 까지 (demo_bench V4) — 단 층 수에 비례해 느려져 뱀 복도에서 V1 의 약 26배. (2) 나눗셈 없는 인덱스: 행 폭 2의 거듭제곱 → 시프트·마스크. (3) 64비트 행 연산은 Cortex-M4 에서 32비트 두 워드로 쪼개져 명령 수가 arm64 의 두 배 이상(29 vs 13, `demo_cm4_row.c`) → 32열 단위 워드가 자연스럽다. (4) 결정적 최악 시간: 조기 종료가 있어도 최악은 Θ(V) 이고, 계획 태스크의 주기·우선순위에 그 최악을 넣는다. (5) 지도는 다른 태스크(센서 융합)가 쓰므로 이중 버퍼나 스냅샷 복사 후 계획. 모든 숫자는 이 맥 측정이며 MCU 실측이 아니다.

</details>

## 9. 추가 연습

### 연습 1 — 최단 경로 개수

`bfs_count_paths(g, start, dist, ways, queue)`: 각 칸까지의 최단 경로 수를 `uint64_t` 로. 빈 R×C 격자 모서리→모서리 = C(R+C−2, R−1) (LeetCode 62 Unique Paths 의 값), 작은 무작위 격자에서는 길이가 정확히 dist 인 단순 경로를 전수 열거한 수와 대조.

<details><summary>답 보기</summary>

BFS 를 그대로 돌리면서, u 를 꺼낼 때 각 빈 이웃 v 에 대해 `dist[v] == dist[u] + 1` 이면 (방금 발견했든 이미 발견됐든) `ways[v] += ways[u]`. u 를 꺼낼 때 `ways[u]` 는 확정이다: u 로 들어오는 최단 간선은 모두 dist[u]−1 층에서 오고, 큐 불변식 때문에 그 층은 u 보다 먼저 전부 꺼내졌다. 오버플로 한계: C(66, 33) ≈ 7.2·10¹⁸ < 2⁶⁴ 이라 34×34 까지 `uint64_t` 로 안전, 64×64 의 C(126, 63) ≈ 6·10³⁶ 은 넘친다 — 펌웨어라면 포화(saturating) 덧셈이나 mod. 실제 출력:

```text
연습 1: empty RxC (R,C<=34) 1156 cases bad=0, 34x34 corner ways=7219428434016265740
연습 1: 600 random grids <=5x5, 504 reachable pairs enumerated, bad=0
연습 1: M7 room+pillar shortest paths = 11
```

M7 의 11 = 기둥 오른쪽으로 도는 경로(0행을 따라 (0,3) 까지 간 뒤 C(5, 2) = 10가지) + 왼쪽 기둥 밖으로 내려가는 1가지.

</details>

### 연습 2 — 가장 가까운 충전 도크 (다중 시작점 BFS)

도크가 여러 개일 때 모든 칸에서 가장 가까운 도크까지의 거리와 그 도크 번호. (LeetCode 286 Walls and Gates, 542 01 Matrix, 994 Rotting Oranges 와 같은 꼴.) 도크마다 BFS 를 따로 돌린 최솟값과 대조.

<details><summary>답 보기</summary>

모든 도크를 거리 0 으로 큐에 **먼저 다 넣고** 한 번의 BFS. 가상의 "슈퍼 소스" 에서 모든 도크로 가는 거리 0 간선을 붙인 것과 같아서 Part A 증명이 그대로 성립한다. `src[v] = src[u]` 로 어느 도크가 먼저 닿았는지 전파 (같은 거리면 큐에 먼저 넣은 도크 쪽). 도크 k 개에 BFS k 번 = O(kV) → 한 번 = O(V). 테스트는 거리와 함께 "src 가 가리키는 도크까지의 실제 거리 = 최소 거리" 도 확인한다. 실제 출력: `연습 2: 400 random grids, 1-5 docks, bad=0`.

</details>

### 연습 3 — 8방향, 모서리 자르기 금지

대각선 이동은 두 직교 이웃이 모두 빈칸일 때만 허용 (로봇 몸체가 장애물 모서리를 긁지 않게). 빈 격자에서는 체비셰프 거리 max(|dr|, |dc|) 와 같아야 한다. (LeetCode 1091 Shortest Path in Binary Matrix 는 8방향이지만 이 제한이 없는 판이다 — 문제 설명 확인 권장.)

<details><summary>답 보기</summary>

이웃 표를 8개로 늘리고 `k >= 4` 일 때 `(r+dr, c)` 와 `(r, c+dc)` 를 추가로 검사. 간선 가중치가 모두 1 이라 BFS 가 그대로 맞다 (대각선을 √2 로 치려면 가중치가 생겨 다익스트라/A* — ALGO-M06, A01). M5 에서 (3,0)→(2,1) 대각선은 (2,0) 이 장애물이라 금지 → dist 2, 갇힌 (1,2) 는 여전히 -1 (Part B 표의 "도달 가능성은 4방향과 같다"). 실제 출력: `연습 3: empty 64x64 Chebyshev + 600 random grids, bad=0`.

</details>

### 연습 4 — 장애물을 최소 몇 개 부숴야 하나 (0-1 BFS)

빈칸으로 들어가면 비용 0, 장애물로 들어가면 1 (부숨). (0,0) 계열 시작에서 목표까지 최소 비용. (LeetCode 의 "Minimum Obstacle Removal to Reach Corner" 와 같은 문제.) 가중 완화 기준 구현과 대조.

<details><summary>답 보기</summary>

덱(deque): 비용 0 간선의 칸은 앞에, 1 은 뒤에 넣는다. 덱 안 비용은 비내림차순이고 폭 ≤ 1 이라 (BFS 큐 불변식의 가중판) 꺼내는 순서가 다익스트라와 같다. 같은 칸이 비용이 줄 때마다 다시 들어갈 수 있으므로 꺼낼 때 `done` 확인. push 총량 ≤ 1 + 4V 라 배열 가운데서 시작하는 2(4V+1)+1 칸 덱이면 랩어라운드 없이 충분 (링으로 하면 4V+1). O(V). 실제 출력:

```text
연습 4: 600 random grids (density up to 70%), bad=0
연습 4: M4 breaks=1, M10 breaks=3
```

M10 체커보드 (0,0)→(3,3): 어떤 경로든 칸 색이 번갈아 바뀌어 최소 6걸음 중 3칸이 장애물.

</details>

### 연습 5 — 닫힌 빈 공간 (갇힌 방 찾기)

테두리에 닿지 않는 빈칸 4-연결 요소의 수 (바깥에서 들어갈 수 없는 공간). LeetCode 130 Surrounded Regions, "Number of Closed Islands" 와 같은 꼴.

<details><summary>답 보기</summary>

1) 테두리의 빈칸마다 flood 해서 "열림" 표시, 2) 안쪽을 행 우선으로 훑으며 남은 빈칸마다 count++ 하고 flood. 각 칸은 한 번만 칠해져 O(V). 1 단계를 "테두리를 씨앗으로 한 다중 시작점 채우기" 로 보면 연습 2 와 같은 생각이다. 실제 출력: `연습 5: 600 random grids, bad=0`, `closed pockets M4=1 M5=1 M8=0` — M8 의 갇힌 5칸은 S 에서는 못 가지만 테두리에 닿아 있어 "닫힌 공간" 은 아니다 (정의가 "S 에서 도달" 과 다르다는 것을 면접에서 짚을 것).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 11/11, 보고 없음
```

## 10. 핵심 정리

- BFS = 넣을 때 표시(`dist[v] = dist[u] + 1`) + FIFO. 큐 안 거리는 [d…d, d+1…d+1] → 처음 발견 = 최단. 스택으로 바꾸면 거리가 틀린다 (빈 3×3 에서 2 → 6).
- 경로: `len = dist[goal] + 1` 을 먼저, cap 검사, parent 를 따라 `path[len-1]` 부터. 이웃 순서를 고정하면 경로가 결정적.
- 덩어리: 행 우선 훑기 + 씨앗마다 채우기. DFS(스택)·BFS(큐) 결과 동일, 넣을 때 표시면 보조 배열 ≤ V. 재귀 금지(B11).
- 4방향 로봇의 벽은 8-연결로 센다 (쌍대성). 8방향 + 모서리 자르기 금지의 도달 가능성 = 4방향.
- 인덱스: 행 폭 64 고정 → 시프트·마스크, 그래도 범위 검사는 (r, c) 로 (`id ± 1` 은 행을 넘는다).
- 메모리: 바이트판 24 KB → 비트 지도 + seen + 2비트 방향 9.5 KB → 비트보드 층 BFS 2 KB (층 수만큼 느려짐).
- 테스트: 손 지도 기댓값 + 느리지만 의심할 곳 없는 기준 구현(완화 반복, 라벨 전파) + 4×4 전수 + 공식(C(n+1, 2k), C(R+C−2, R−1)).

## 참고자료

- AlgoMonster 노트 M06 §1 (BFS Intro), M07 §1~§5 (Graph Intro · BFS on Graphs · DFS on Graph · BFS vs DFS · Shortest Path), §7~§11 (Matrix as Graph · Flood Fill · Number of Islands · Knight Minimum Moves · Walls and Gates) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M06_breadth_first_search.md`, `2026-09-27_M07_graph.md`
- T. H. Cormen et al., *Introduction to Algorithms* (3rd ed.), §22.2 Breadth-first search (최단 거리 정리와 BFS 트리), §22.3 Depth-first search.
- A. Rosenfeld, "Connectivity in Digital Pictures", *Journal of the ACM* 17(1), 1970 — 4/8-연결성과 전경·배경 짝 맞추기.
- A. Elfes, "Using Occupancy Grids for Mobile Robot Perception and Navigation", *IEEE Computer* 22(6), 1989 — 점유 격자 표현의 원조.
- S. M. LaValle, *Planning Algorithms*, Cambridge University Press, 2006, Ch. 2 (Discrete Planning) — 격자 위 전방 탐색과 BFS/A* 의 관계: <http://lavalle.pl/planning/>
- LeetCode 200 Number of Islands · 695 Max Area of Island · 733 Flood Fill · 1091 Shortest Path in Binary Matrix · 994 Rotting Oranges · 542 01 Matrix · 286 Walls and Gates · 130 Surrounded Regions · 62 Unique Paths.
- 관련 레슨: ALGO-B11 (재귀 flood fill 의 스택 깊이, 큐 피크 실측, 넣을 때 표시), ALGO-A01 (A* 와 장애물 팽창), ALGO-M06 (다익스트라), ALGO-A02 (벨만-포드), ALGO-M14 (Union-Find 로 연결 요소), DS-B02 (배열 스택), DS-B04 (2의 거듭제곱 링 버퍼), DS-B08 (비트맵), C-A10 (MISRA: 재귀 금지).
