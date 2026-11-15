---
id: ALGO-B11
time: 60분
updated: 2026-11-11
---
# ALGO-B11 재귀와 스택 깊이: 재귀를 반복으로

## 학습 목표

- 4-이웃 flood fill 과 트리 순회(전위·중위·후위)를 재귀로 짜고, **재귀 깊이**와 **프레임 바이트**를 직접 잰다 (이 맥 arm64: -O0 64 B, -O2 80 B / Cortex-M4: 40 B, 56 B).
- 뱀 모양 복도 격자로 **진짜 스택 오버플로**를 낸다 — `ulimit -s` 로 줄인 메인 스레드(SIGSEGV, exit 139)와 `pthread_attr_setstacksize` 스레드(SIGBUS) 둘 다.
- 재귀를 **고정 용량 배열 스택 / 링 버퍼 큐**로 바꾸고, 넘치면 UB 대신 `B11_EOVERFLOW` 를 돌려준다. 결과를 재귀판·기준 구현과 칸 단위로 대조한다.
- "push 할 때 표시 vs pop 할 때 표시"가 왜 스택 크기를 n → 2n → 4n 으로 바꾸는지, 격자에서 DFS 스택과 BFS 큐 중 누가 더 큰지 실측 숫자로 말한다.
- Cortex-M 태스크 스택(FreeRTOS 워드 단위 크기, `configCHECK_FOR_STACK_OVERFLOW`, 스택 칠하기·high-water mark, MPU 가드)과 MISRA C:2012 Rule 17.2 를 이 문제에 연결한다.

## 1. 문제

임베디드 면접에서 DFS 를 재귀로 쓰면 거의 반드시 "그 보드의 스택이 4 KB 면?" 이 따라온다 — 재귀 깊이는 **입력이 정하고**, 태스크 스택은 **컴파일 때 정해지기** 때문이다.

로봇 청소기 펌웨어가 64×64 점유 격자(occupancy grid, 칸마다 0=빈칸 1=장애물)를 들고 있다. "현재 위치에서 갈 수 있는 바닥 면적"을 구하는 가장 짧은 코드는 재귀 flood fill 이다. 그런데 재귀 깊이는 **연결된 칸 수만큼** 커질 수 있다. 아래 뱀 모양 복도가 최악이다: 칸마다 이웃이 하나뿐이라 한 줄로 쭉 들어간다. 256×256 뱀 복도(32,896칸)는 이 맥에서 2 MiB 넘게 스택을 먹는다. 같은 깊이를 Cortex-M4 -O2 프레임(56 B, 아래 실측)으로 계산하면 약 1.8 MB — 대부분 MCU 의 SRAM 전체보다 크다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">뱀 복도 9×5: 칸 29개 → 재귀 깊이 30 (칸 + 범위 밖 검사 1)</text>
  <g stroke="currentColor" stroke-width="0.6">
    <rect x="20" y="50" width="216" height="24" class="f-ok"/>
    <rect x="20" y="74" width="192" height="24" class="f-mut"/><rect x="212" y="74" width="24" height="24" class="f-ok"/>
    <rect x="20" y="98" width="216" height="24" class="f-ok"/>
    <rect x="20" y="122" width="24" height="24" class="f-ok"/><rect x="44" y="122" width="192" height="24" class="f-mut"/>
    <rect x="20" y="146" width="216" height="24" class="f-ok"/>
  </g>
  <polyline points="32,62 224,62 224,110 32,110 32,158 220,158" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#b11a)"/>
  <text x="20" y="194" fill="currentColor">회색 = 벽, 초록 = 빈칸</text>
  <text x="20" y="212" fill="currentColor">이웃이 하나뿐 → 한 줄로 쌓인다</text>
  <text x="20" y="240" fill="currentColor" font-family="monospace">depth = cells + 1</text>
  <text x="20" y="262" fill="currentColor">256×256 → 32,897 프레임</text>
  <text x="300" y="44" fill="currentColor" font-weight="bold">호출 스택 (높은 주소 → 낮은 주소)</text>
  <g stroke="currentColor">
    <rect x="300" y="52" width="170" height="22" class="f-acc"/>
    <rect x="300" y="74" width="170" height="22" class="f-acc"/>
    <rect x="300" y="96" width="170" height="22" class="f-acc"/>
    <rect x="300" y="140" width="170" height="22" class="f-acc"/>
    <rect x="300" y="162" width="170" height="22" class="f-mut"/>
    <rect x="300" y="206" width="170" height="26" class="f-warn"/>
  </g>
  <text x="385" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">ff_rec(0,0)</text>
  <text x="385" y="89" fill="currentColor" text-anchor="middle" font-family="monospace">ff_rec(0,1)</text>
  <text x="385" y="111" fill="currentColor" text-anchor="middle" font-family="monospace">ff_rec(0,2)</text>
  <text x="385" y="133" fill="currentColor" text-anchor="middle">⋮</text>
  <text x="385" y="155" fill="currentColor" text-anchor="middle" font-family="monospace">ff_rec(4,8)</text>
  <text x="385" y="177" fill="currentColor" text-anchor="middle" font-family="monospace">ff_rec(4,9) 범위 밖</text>
  <text x="385" y="224" fill="currentColor" text-anchor="middle">가드 페이지 → 폴트</text>
  <path d="M480 60 L480 200" stroke="currentColor" fill="none" marker-end="url(#b11a)"/>
  <text x="490" y="80" fill="currentColor">프레임 1개 (실측)</text>
  <text x="490" y="100" fill="currentColor">맥 arm64: 64 B (-O0), 80 B (-O2)</text>
  <text x="490" y="120" fill="currentColor">Cortex-M4: 40 B (-O0), 56 B (-O2)</text>
  <text x="490" y="150" fill="currentColor">메인 스레드 + ulimit → SIGSEGV</text>
  <text x="490" y="170" fill="currentColor">pthread 가드 → SIGBUS</text>
  <text x="490" y="200" fill="currentColor">Cortex-M: HardFault 또는 조용한</text>
  <text x="490" y="218" fill="currentColor">메모리 덮어쓰기 (가드 없으면)</text>
  <text x="300" y="262" fill="currentColor">반복판: 같은 복도에서 스택 엔트리 1개(4 B), BFS 큐 1개 — 실측</text>
  <text x="300" y="284" fill="currentColor">재귀 깊이 문제는 "보조 메모리를 내가 정한 배열로 옮기기"로 푼다</text>
</svg>
```

모든 함수는 malloc 없이 **호출자가 준 배열**만 쓴다. 칸 번호는 `r*w + c`, 이웃 순서는 위·아래·왼쪽·오른쪽(`DR/DC`)으로 고정한다 — 그래야 재귀판과 반복판의 깊이·피크를 같은 입력에서 비교할 수 있다.

### Part A — 재귀 버전과 깊이 계측

- `ff_rec(g, r, c, from, to, d)` — (r,c)와 4-이웃으로 이어진 `from` 색 칸을 전부 `to` 로. 반환 = 칠한 칸 수. `from == to` 면 0. 모든 호출에서 `depth_enter/leave`(주어짐)로 깊이와 로컬 주소를 기록한다.
- `trav_rec(t, v, order, out, n, d)` — 배열 풀 트리(`left/right` = 노드 번호, -1 = 없음)를 전위/중위/후위로 `out` 에 적는다.
- 테스트: 1×1(깊이 2), 1×10 복도(깊이 11), `from == to`, 고리 벽 안쪽/바깥쪽, 시드가 벽인 경우, 손으로 그린 BST 9개 노드의 세 순서와 깊이 5.

### Part B — 명시적 스택 반복 버전

- `ff_dfs_iter(g, r, c, from, to, buf, cap, &peak)` — 보조 스택 = `buf[0..cap)`. **push 할 때 칠한다.** 넘치면 `B11_EOVERFLOW`, `buf[cap]` 이후는 절대 안 쓴다.
- `trav_iter(t, root, order, out, buf, cap, &peak)` — 재귀판과 **똑같은 순서**. 후위가 핵심(마지막 방문 노드 `last` 로 "오른쪽 끝났나" 판단).
- 테스트: 3×3 손 추적 피크(DFS 3), cap 0, 카나리(canary) 4워드로 넘침 검사, 무작위 격자 300개·무작위 BST 300개를 재귀판·기준 구현과 대조, **cap = peak 은 성공 / cap = peak−1 은 실패**(피크가 정확한 값인지), 높이 10^6 사슬 트리, 50만 칸 뱀 복도.

### Part C — 점유 격자 BFS 와 RAM 예산

- `ff_bfs(...)` — 같은 계약, 링 버퍼 큐. enqueue 할 때 칠한다.
- 창고 지도(24×12, 닫힌 구역 2곳)에서 로봇 R 의 도달 면적 = 97칸(닫힌 26칸 제외)을 정적 큐 64 엔트리(256 B)로 구한다. 같은 64 엔트리를 장애물 없는 64×64 바닥 가운데에 쓰면 **넘침이 에러로 보고되고** 카나리는 멀쩡해야 한다.
- 측정(`demo_depth.c`): 같은 입력에서 재귀 깊이·바이트, DFS 스택 피크(올바른 판·틀린 판 2종), BFS 큐 피크. 오버플로 재현(`demo_overflow.c`).

## 2. Problem (English)

A robot keeps a 2-D occupancy grid `uint8_t cell[h*w]` (row-major). Implement 4-neighbour flood fill and binary-tree traversal **without unbounded recursion**: all auxiliary memory comes from a caller-supplied fixed-size array.

```c
typedef struct { uint8_t *cell; int w, h; } grid_t;
typedef struct { int left, right, key; } tnode_t;      /* -1 = no child, nodes live in an array */

int ff_rec     (grid_t *g, int r, int c, uint8_t from, uint8_t to, depth_t *d);        /* reference */
int ff_dfs_iter(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak);
int ff_bfs     (grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak);
int trav_rec   (const tnode_t *t, int v, int order, int *out, int n, depth_t *d);
int trav_iter  (const tnode_t *t, int root, int order, int *out, int32_t *buf, int cap, int *peak);
```

- Return the number of cells filled / keys written; return `-1` (`B11_EOVERFLOW`) if `cap` is too small — never write past `buf[cap-1]`. `*peak` = max entries used.
- `from == to` → 0 (must not loop forever). Seed out of range or of another colour → 0.
- Constraints: `w*h ≤ 2^31−1`; up to 10^6 cells / nodes; O(n) time; the output must match the recursive version exactly (same filled set; same traversal order).

Example — 3×3 all-free grid, seed (0,0), cap = 64:

```text
ff_dfs_iter -> 9, peak = 3        ff_bfs -> 9, peak = 3        ff_rec -> 9, max depth 10
```

Follow-ups: How big must `cap` be to *guarantee* no overflow? Which uses less memory on a grid, DFS or BFS? On a balanced tree? What if you mark cells when popping instead of when pushing?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 군데(`ff_rec`, `trav_rec`, `ff_dfs_iter`, `trav_iter`, `ff_bfs`). 깊이 계측(`depth_enter/leave`), 고정 용량 스택(`is_push` 는 꽉 차면 false), 링 버퍼 큐(`iq_push/iq_pop`), 기준 구현(oracle), 지도·트리 생성기는 주어진다. 처음 실행 결과(실제 출력, 중간 생략):

```text
FAIL starter.c:257  ff_rec(&g, 0, 0, FREE, DONE, &d) == 1 && g.cell[0] == DONE
FAIL starter.c:258  d.max_depth == 2 && d.depth == 0
…
random grids: 300, filled cells 13247, mismatches rec=236 dfs=236 bfs=236, bound=0, cap-edge=944
random BSTs: 300, nodes 29580, mismatches iter!=rec=894 inorder!=sorted=296 shape=592 peak=894
warehouse 24x12: free=123 reachable=0 (sealed 123), rec depth=1, dfs peak=0, bfs peak=0
5/46 passed
```

(exit 1. 통과하는 5개는 "빈 트리 → 0", "`from == to` → 0" 처럼 스텁이 우연히 맞히는 검사.)

## 4. 힌트

<details><summary>힌트 1 — 재귀판: "들어오자마자 칠하기"</summary>

칠하는 것이 곧 방문 표시(visited)다. 칠한 **뒤에** 이웃을 부르면, 이웃이 나를 다시 불러도 색이 달라서 바로 0 을 돌려준다. `from == to` 면 칠해도 색이 안 바뀌니 무한 재귀 — 맨 앞에서 막는다. 네 번의 호출은 `n += ...` 네 줄(또는 루프)로: `a()+b()+c()+d()` 는 호출 순서가 **미지정**이다.

</details>

<details><summary>힌트 2 — 반복판: 스택에 무엇을 넣나</summary>

칸 번호 하나(`r*w+c`)면 된다. 시드를 push 하면서 칠하고, `while (top > 0)` 에서 pop → 네 이웃 중 `from` 인 칸을 **push 하면서 칠한다.** 그러면 한 칸은 평생 한 번만 스택에 들어가므로 `cap = 칠할 칸 수` 면 절대 안 넘친다.

</details>

<details><summary>힌트 3 — 후위 순회</summary>

중위와 똑같이 "왼쪽 끝까지 push 하며 내려가기"로 시작한다. 내려갈 곳이 없으면 맨 위(peek)를 본다: 오른쪽 자식이 있고 **아직 안 했으면**(`last != right`) 오른쪽으로, 아니면 pop 해서 방문하고 `last = v`. `last` 검사를 빼면 오른쪽 부분 트리를 영원히 다시 돈다.

</details>

<details><summary>힌트 4 — BFS 링 버퍼와 피크</summary>

`iq_push` 가 이미 wrap-around 와 `count` 를 처리한다. DFS 코드에서 `is_push/is_pop` 을 `iq_push/iq_pop` 으로 바꾸면 끝. 피크는 헬퍼가 기록하니 마지막에 `*peak = q.peak` 만.

</details>

## 5. 풀이

**면접에서 말하는 순서**

| 단계 | 말할 것 |
|---|---|
| 1. 확인 질문 | 4-이웃인가 8-이웃인가? `from == to` 면? 격자 최대 크기·스택 예산은? malloc 가능? 결과를 제자리(in-place)로 칠해도 되나? |
| 2. 단순 해법 | 재귀 DFS 10줄. 시간 O(n), **스택 깊이 최악 n+1** (뱀 복도). 프레임 수십 B × n |
| 3. 개선 + 불변식 | 명시적 스택/큐 배열. 불변식: "스택·큐 안의 칸은 이미 칠해져 있다" → 칸마다 최대 1번 들어감 → cap = n 이면 안전 |
| 4. 코드 | 넘치면 에러 코드 (UB 금지), peak 기록 |
| 5. 소리 내어 말할 테스트 | 1×1, 시드가 벽, `from == to`, 닫힌 방, 뱀 복도(깊이 최악), 넓은 바닥(DFS 스택 최악), cap 0 / cap = peak−1 |
| 6. 복잡도 | 시간 O(n). 보조: 재귀 O(n) **스택**, DFS 배열 ≤ n, BFS 배열 ≤ n (넓은 바닥에선 실측 O(w+h)) |

### Part A — 재귀: 불변식과 깊이

코드는 6장(10줄). 핵심은 "들어오자마자 칠하고, 그다음 네 이웃을 `n +=` 로 한 줄씩"이다.

- **불변식**: 활성 프레임 중 "칠한" 프레임들의 칸은 모두 다르다(들어갈 때 칠했으니 같은 칸에 두 번 들어갈 수 없다). 그래서 깊이 ≤ (칠한 칸 수) + 1. 마지막 +1 은 범위 밖·벽·이미 칠한 칸을 보고 바로 돌아오는 프레임이다.
- **호출 수**: 칠하는 호출 n 번 + 각자 4번씩 부르므로 총 ≤ 1 + 4n. 시간 O(n).
- 3×3 빈 격자, 모서리 (0,0): 위·아래·왼쪽·오른쪽 순서라 0→3→6→7→4→1→2→5→8 로 한 줄로 내려간다(칸 번호 = r*3+c). 칠한 칸 9 + 1 = **최대 깊이 10**. 넓은 바닥이어도 재귀는 뱀처럼 기어 다닌다: 열린 256×256 모서리에서 실측 깊이 65,537 = n+1.
- 트리판도 같다. 빈 자식 호출까지 세면 깊이 = 높이 + 1 (손 BST 높이 4 → 5). 균형 트리면 log n, 사슬이면 n.

**실제로 터뜨리기 — `demo_overflow.c`** (이 맥, macOS 27.2, Apple Silicon. 시행마다 fork 한 자식에서 지정 크기 pthread 로 돌리고 부모가 종료 시그널을 찍는다. 셀 값 = 끝까지 갔으면 실측 스택 사용량)

```bash
cc -std=c11 -Wall -Wextra -Werror -O0 demo_overflow.c -o demo_overflow_O0 && ./demo_overflow_O0
cc -std=c11 -Wall -Wextra -Werror -O2 demo_overflow.c -o demo_overflow_O2 && ./demo_overflow_O2
```

```text
[-O0] pthread stack size vs serpentine grid (recursion depth ~ cells)
pthread_get_stacksize_np:  64K->76K  256K->268K  512K->524K  1024K->1036K  8192K->8204K
ff_rec (solution.c)
grid (cells)                  64K       256K       512K      1024K      8192K
64x64 (2080)               SIGBUS    ok 130K    ok 130K    ok 130K    ok 130K
128x128 (8256)             SIGBUS     SIGBUS    ok 516K    ok 516K    ok 516K
256x256 (32896)            SIGBUS     SIGBUS     SIGBUS     SIGBUS   ok 2056K
512x512 (131328)           SIGBUS     SIGBUS     SIGBUS     SIGBUS     SIGBUS
[-O2] … ff_rec (solution.c)
64x64 (2080)               SIGBUS    ok 162K    ok 162K    ok 162K    ok 162K
128x128 (8256)             SIGBUS     SIGBUS     SIGBUS    ok 645K    ok 645K
256x256 (32896)            SIGBUS     SIGBUS     SIGBUS     SIGBUS   ok 2570K
512x512 (131328)           SIGBUS     SIGBUS     SIGBUS     SIGBUS     SIGBUS
[-O2] ff_plain (no probe)
128x128 (8256)             SIGBUS     SIGBUS    ok 393K    ok 393K    ok 393K
512x512 (131328)           SIGBUS     SIGBUS     SIGBUS     SIGBUS   ok 6180K
ff_dfs_iter, serpentine 1024x1024 (524800 cells), 64K thread stack, 16-entry heap stack: ok 0K
```

- pthread 스택을 넘으면 이 맥에서는 **SIGBUS**(가드 페이지 접근). 요청보다 12 KiB 더 받는다(`512K->524K`) — 그래서 516 KiB 가 필요한 128×128 이 -O0, 512 KiB 요청에서 통과했다.
- **`ulimit -s` 확인**: 브리프의 우려와 달리 이 맥에서는 서브셸의 `ulimit -s` 가 새로 exec 한 프로세스의 **메인 스레드**에 적용됐다 (`RLIMIT_STACK` 이 그대로 보이고 그 근처에서 죽는다):

```text
$ (ulimit -s 512; ./demo_overflow_O0 main 64); echo "exit=$?"
[-O0] main thread, RLIMIT_STACK=512 KiB, serpentine 64x64
ok: filled 2080, depth 2081, stack used 130 KiB
exit=0
$ bash -c '(ulimit -s 512; ./demo_overflow_O0 main 128); echo "exit=$?"'
[-O0] main thread, RLIMIT_STACK=512 KiB, serpentine 128x128
bash: line 1: 26178 Segmentation fault: 11  ./demo_overflow_O0 main 128
exit=139
$ ./demo_overflow_O0 main 512; echo "exit=$?"     # 기본 8176 KiB → exit=139
$ (ulimit -s 65520; ./demo_overflow_O0 main 512)  # 하드 한계까지 올리면 ok … stack used 8208 KiB
```

메인 스레드는 SIGSEGV(139 = 128+11). 하드 한계(65520 KiB)를 넘는 `ulimit -s 100000` 은 셸이 거부했다. 하나 더 확인: 프로그램 **안에서** `setrlimit(RLIMIT_STACK)` 로 소프트 한계를 하드 한계까지 올려도 이 맥에서는 메인 스레드가 바로 더 깊이 갔다 (`./demo_overflow_O0 raise 512` → `ok … stack used 8208 KiB`, 같은 입력이 `main 512` 로는 exit 139). 반면 pthread 스택은 생성 때 크기가 고정이다 — 스택 크기를 코드에서 **명시적으로** 정하고 싶으면 `pthread_attr_setstacksize` 가 확실하다. (Linux 동작은 이 레슨에서 재지 않았다.)

**프레임 바이트: -O0 vs -O2** (`depth_t` 의 로컬 주소 차 ÷ (깊이−1), 그리고 asm 으로 확인)

| 빌드 | arm64 (이 맥) | 근거 | Cortex-M4 (`cm4_ff_rec.c`, 계측 없는 판) | 근거 |
|---|---|---|---|---|
| -O0 | **64 B** | `sub sp, sp, #64` | **40 B** | `push {r7, lr}` + `sub sp, #32`, `-fstack-usage`: 40 |
| -O2 | **80 B** | x19–x24 + fp/lr 저장 + 로컬, 16 B 정렬 | **56 B** | `push.w {r4–r11, lr}` 36 B + `sub sp, #20`, `-fstack-usage`: 56 |

최적화가 프레임을 **키웠다**: -O2 는 값들을 callee-saved 레지스터에 두므로 그 레지스터들을 프레임마다 저장한다. 대신 계측 없는 `ff_plain` 은 -O2 에서 **깊이 자체가 줄었다**: Cortex-M4 asm 에 `bl ff_rec` 이 4개가 아니라 3개 — LLVM 이 마지막 호출(오른쪽)을 누산기 꼬리 재귀 제거로 루프(`adds r6, #1` 후 `blt .LBB0_5`)로 바꿨다. 그래서 512×512 가 -O2 에선 6180 KiB 로 통과하고 -O0 에선 죽는다. **C 표준은 이 변환을 보장하지 않는다** — 디버그 빌드에서만 죽는 펌웨어가 이렇게 생긴다. `ff_rec` 은 호출 뒤에 `depth_leave` 가 있어 이 변환이 막힌다(-O2 에서도 깊이 그대로).

### Part B — 명시적 스택: push 할 때 칠한다

**불변식**: 스택 안의 칸은 모두 이미 칠해져 있다 → 같은 칸이 두 번 들어갈 수 없다 → **peak ≤ 칠한 칸 수 n**. cap 을 칸 수로 잡으면 어떤 지도에서도 안 넘친다(Part C 의 RAM 예산 근거).

3×3 빈 격자, 시드 0 (칸 번호 r*3+c). 이웃 순서 위·아래·왼·오른쪽이고 스택 맨 오른쪽이 top:

| 단계 | pop | push 하며 칠함 | 스택 (push-mark) | pop-mark 판의 스택 (pop 때 칠함) |
|---|---|---|---|---|
| 0 | — | 0 | [0] | [0] |
| 1 | 0 | 3, 1 | [3, 1] | [3, 1] |
| 2 | 1 | 4, 2 | [3, 4, 2] **3** | [3, 4, 2] |
| 3 | 2 | 5 | [3, 4, 5] | [3, 4, 5] |
| 4 | 5 | 8 | [3, 4, 8] | [3, 4, 8, **4**] — 4 는 아직 안 칠해져서 또 들어감 |
| 5 | 8 | 7 | [3, 4, 7] | pop 4 → [3, 4, 8, 7, **3**] **5** |
| 6 | 7 | 6 | [3, 4, 6] | … 7, 8 도 중복 |
| 7–9 | 6, 4, 3 | — | [] | 중복 4개를 pop 해서 버리고 끝 (총 pop 13번) |

push-mark 피크 3, pop-mark 피크 5, BFS 큐 피크 3 (하네스의 손 검사 `pk == 3` 이 pop-mark 판을 잡아낸다 — 실제로 바꿔 돌리면 45/46, 그 1개만 FAIL).

**표시 시점별 push 총량 상한** (4-이웃 격자, 칸 n 개, 간선 E < 2n):

| 판 | 언제 검사·표시 | push 총량 | 3×3 피크 | 열린 256×256 가운데 피크 (실측) |
|---|---|---|---|---|
| push-mark | push 직전 검사, push 와 동시에 칠함 | ≤ n | 3 | 16,830 |
| pop-mark | push 직전 "from 인가"만 검사, pop 때 칠함 | ≤ 1 + E (간선마다 한쪽에서 최대 1번) | 5 | 33,151 |
| lazy | 검사 없이 4방향 다 push, pop 때만 검사 | ≤ 1 + 4n | 11 | 82,175 |

pop-mark 의 상한 증명: u 가 칠해질 때 아직 안 칠해진 v 만 push 한다. 나중에 v 가 칠해질 때 u 는 이미 칠해져 있으니 v→u push 는 없다. 그래서 무방향 간선 하나당 최대 1번. 격자에선 2배, 하지만 **일반 그래프에서는 O(V) → O(E)** (완전 그래프면 V²). lazy 는 벽·범위 안 칸까지 다 넣어서 뱀 복도에서도 2.5n (32,896칸 → 81,537 엔트리) — cap = n 으로 잡은 스택이 **넘친다**(연습 5).

**트리 반복 순회.** 전위는 "꺼내며 방문, 오른쪽 먼저 push". 중위·후위는 "왼쪽 끝까지 push 하며 내려가기". 후위 추적(손 BST, 스택 오른쪽이 top):

| 동작 | 스택 | 출력 |
|---|---|---|
| push 8, 3, 1 (왼쪽 끝까지) | [8, 3, 1] | |
| 1 은 오른쪽 없음 → pop, `last=1` | [8, 3] | 1 |
| peek 3: 오른쪽 6 ≠ last → 6 으로, push 6, 4 | [8, 3, 6, 4] | |
| pop 4 / peek 6 → 7 로, push 7, pop 7 | [8, 3, 6] | 1 4 7 |
| peek 6: 오른쪽 7 == last → pop 6, pop 3 (오른쪽 6 == last) | [8] | 1 4 7 6 3 |
| peek 8 → 10 → 14 → push 13 … pop 13, 14, 10, 8 | [] | 1 4 7 6 3 13 14 10 8 |

스택 = 뿌리에서 현재 노드까지의 경로 전체 → 후위 피크 = **높이** (하네스가 `pk == ht` 로 정확히 검사). 중위는 "왼쪽으로 꺾은 조상"만 남아 ≤ 높이, 전위(오른쪽 먼저)는 깊이 k 노드를 꺼낼 때 조상 쪽 오른쪽 형제 ≤ k−1 개 + 자식 2 → ≤ 높이. 재귀판 깊이는 높이 + 1 (빈 자식 프레임). 사슬 10^6 에서 중위 피크 1,000,000, 전위 피크 1 — **반복으로 바꾸면 메모리가 사라지는 게 아니라 내가 정한 배열로 옮겨질 뿐**이다. 차이는 (1) 엔트리 4 B vs 프레임 64–80 B, (2) 넘치면 에러 코드 vs 폴트.

### Part C — BFS 큐, 그리고 무엇이 더 큰가

**측정 — `demo_depth.c`** (이 맥, 재귀판은 1 GiB 스택 pthread 안에서 끝까지 돌림. 엔트리 = 4 B)

```bash
cc -std=c11 -Wall -Wextra -Werror -O0 demo_depth.c -o demo_depth_O0 && ./demo_depth_O0
cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth_O2 && ./demo_depth_O2
```

```text
build: -O0
grid case               cells  rec depth  rec KiB  B/frame | dfs push-mark  dfs pop-mark  dfs lazy  bfs queue  (entries)
open 64x64 center         4096       2083      130     64.0 |          1134          2143      5183        127
open 256x256 corner      65536      65537     4096     64.0 |         32641         65026    162818        256
open 256x256 center      65536      32899     2056     64.0 |         16830         33151     82175        511
random30% 256x256 mid    44972      14850      928     64.0 |          5797         12811     31302        398
serpentine 256x256       32896      32897     2056     64.0 |             1             1     81537          1
serpentine 1024x1024    524800     524801    32800     64.0 |             1             1   1309185          1

tree case               nodes  height  rec depth  rec KiB  B/frame | iter peak pre    in   post
perfect 2^20-1         1048575      20         21        1     64.0 |             20    20     20
random BST 1e5          100000      40         41        2     64.0 |             24    25     40
chain 1e5               100000  100000     100001     6250     64.0 |              1 100000 100000
build: optimized (-O1 or higher)        ← -O2: 엔트리 수는 같고 재귀만 80 B/frame
open 256x256 center      65536      32899     2570     80.0 |         16830         33151     82175        511
serpentine 1024x1024    524800     524801    41000     80.0 |             1             1   1309185          1
chain 1e5               100000  100000     100001     7812     80.0 |              1 100000 100000
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="24" fill="currentColor" font-weight="bold">열린 256×256 바닥, 가운데 시드: 보조 메모리 바이트 (로그 축, 실측)</text>
  <text x="322" y="65" fill="currentColor" text-anchor="end">재귀 ff_rec -O2 (32899 프레임 × 80 B)</text>
  <rect x="330" y="50" width="356" height="20" class="f-warn" stroke="currentColor"/>
  <text x="692" y="65" fill="currentColor">2.51 MiB</text>
  <text x="322" y="95" fill="currentColor" text-anchor="end">재귀 ff_rec -O0 (× 64 B)</text>
  <rect x="330" y="80" width="350" height="20" class="f-warn" stroke="currentColor"/>
  <text x="686" y="95" fill="currentColor">2.01 MiB</text>
  <text x="322" y="125" fill="currentColor" text-anchor="end">DFS lazy: pop 때만 검사 (82175 엔트리)</text>
  <rect x="330" y="110" width="306" height="20" class="f-warn" stroke="currentColor"/>
  <text x="642" y="125" fill="currentColor">321.0 KiB</text>
  <text x="322" y="155" fill="currentColor" text-anchor="end">DFS pop-mark (33151 엔트리)</text>
  <rect x="330" y="140" width="284" height="20" class="f-mut" stroke="currentColor"/>
  <text x="620" y="155" fill="currentColor">129.5 KiB</text>
  <text x="322" y="185" fill="currentColor" text-anchor="end">DFS push-mark (16830 엔트리)</text>
  <rect x="330" y="170" width="267" height="20" class="f-acc" stroke="currentColor"/>
  <text x="603" y="185" fill="currentColor">65.7 KiB</text>
  <text x="322" y="215" fill="currentColor" text-anchor="end">BFS 큐 (511 엔트리)</text>
  <rect x="330" y="200" width="183" height="20" class="f-ok" stroke="currentColor"/>
  <text x="519" y="215" fill="currentColor">2.0 KiB</text>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity="0.4">
    <line x1="330" y1="44" x2="330" y2="230"/><line x1="385" y1="44" x2="385" y2="230"/>
    <line x1="441" y1="44" x2="441" y2="230"/><line x1="496" y1="44" x2="496" y2="230"/>
    <line x1="552" y1="44" x2="552" y2="230"/><line x1="607" y1="44" x2="607" y2="230"/>
    <line x1="662" y1="44" x2="662" y2="230"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="330" y="246">1 B</text><text x="385" y="246">10</text><text x="441" y="246">100</text>
    <text x="496" y="246">1 K</text><text x="552" y="246">10 K</text><text x="607" y="246">100 K</text><text x="662" y="246">1 M</text>
  </g>
  <text x="10" y="276" fill="currentColor">막대 = log10(바이트). demo_depth 표 "open 256x256 center" 줄의 실측값 (엔트리 × 4 B)</text>
</svg>
```

**왜 이렇게 나오나.**

- **BFS 큐 = 프런티어(frontier)**: 큐에는 거리 d 와 d+1 인 칸만 있다. 열린 격자에서 거리 d 인 칸은 마름모 둘레 ≤ 4d 개 → 가운데 시드는 실측 511 ≈ 2·256, 모서리 시드는 대각선 하나 256. 즉 **O(w+h)**. 하지만 이건 열린 바닥 얘기다 — 보장 상한은 여전히 n(칸마다 1번), 갈래가 많은 지도에선 더 커질 수 있다. 펌웨어 예산은 보장 상한(n) 또는 **실제 지도들에서 잰 피크 + 여유 + 넘침 처리**로 잡는다.
- **DFS 스택 = 아직 안 꺼낸 형제들의 무덤**: 한 칸을 꺼낼 때 이웃 2–3개를 넣고 그중 하나만 바로 따라가므로, 나머지가 계속 쌓인다. 넓은 바닥에서 n/4 ~ n/2 (모서리 32,641 ≈ 0.50n). 반대로 복도에서는 형제가 없어서 1.
- **재귀 = 경로 길이**: 복도든 넓은 바닥이든 n 에 가깝고, 엔트리 하나가 4 B 가 아니라 64–80 B 다. 같은 DFS 인데 재귀판 깊이(32,899)와 반복판 피크(16,830)가 다른 이유: 재귀는 "지금 경로"를, 반복 스택은 "나중에 볼 칸"을 담는다. 경로를 그대로 담는 반복판(연습 1)은 피크 = 재귀 깊이 − 1 이다.
- **트리에선 반대**: 균형 트리에서 DFS 스택은 높이(20), BFS 큐는 가장 넓은 레벨(2^19 = 524,288 노드). 연습 3 에서 1023 노드 완전 트리로 큐 512 vs 스택 10 을 확인한다. 규칙: **DFS = 깊이, BFS = 폭**. 격자는 폭이 작고(둘레) 깊이가 크며, 균형 트리는 그 반대다.

**창고 지도 (하네스 Part C)**: 자유 칸 123, 도달 97, 닫힌 구역 26. 재귀 깊이 82, DFS 피크 5, BFS 피크 3 → 정적 큐 64 엔트리(256 B)로 충분. 그런데 같은 64 엔트리를 64×64 빈 바닥에 쓰면 BFS 피크 127 이 필요해 `B11_EOVERFLOW` — 카나리 0xA5A5A5A5 는 그대로(테스트로 확인). **예산은 평균 지도가 아니라 최악 지도에서 정한다.** MCU 라면 4096칸 인덱스는 `uint16_t` 로 충분해 큐 127 엔트리 = 254 B.

### Part C — 펌웨어: Cortex-M 태스크 스택

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">SRAM 한 장 (위 = 높은 주소). 스택은 아래로 자란다</text>
  <g stroke="currentColor">
    <rect x="30" y="36" width="240" height="40" class="f-acc"/>
    <rect x="30" y="76" width="240" height="26" class="f-mut"/>
    <rect x="30" y="102" width="240" height="70" class="f-acc"/>
    <rect x="30" y="172" width="240" height="64" class="f-ok"/>
    <rect x="30" y="236" width="240" height="18" class="f-warn"/>
    <rect x="30" y="254" width="240" height="40" class="f-mut"/>
  </g>
  <text x="150" y="54" fill="currentColor" text-anchor="middle">MSP 메인 스택: ISR·예외</text>
  <text x="150" y="70" fill="currentColor" text-anchor="middle">(예외 프레임 32 B / FPU 104 B)</text>
  <text x="150" y="94" fill="currentColor" text-anchor="middle">Task A TCB</text>
  <text x="150" y="128" fill="currentColor" text-anchor="middle">Task A 스택: 쓰는 중 (PSP)</text>
  <text x="150" y="148" fill="currentColor" text-anchor="middle">재귀 프레임 56 B × 깊이</text>
  <text x="150" y="200" fill="currentColor" text-anchor="middle">0xA5 로 칠해진 채 남은 부분</text>
  <text x="150" y="218" fill="currentColor" text-anchor="middle">= high-water mark (여유)</text>
  <text x="150" y="249" fill="currentColor" text-anchor="middle">가드: MPU no-access ≥ 32 B</text>
  <text x="150" y="272" fill="currentColor" text-anchor="middle">아래: 다른 태스크 / .bss</text>
  <text x="150" y="288" fill="currentColor" text-anchor="middle">가드 없으면 조용히 덮어씀</text>
  <path d="M280 110 L280 230" stroke="currentColor" fill="none" stroke-width="2" marker-end="url(#b11c)"/>
  <text x="300" y="60" fill="currentColor" font-weight="bold">검출 방법 (빠른 순)</text>
  <text x="300" y="86" fill="currentColor">1. MPU 가드 · ARMv8-M PSPLIM: 넘는 순간 폴트</text>
  <text x="314" y="104" fill="currentColor">(MemManage·UsageFault, 꺼져 있으면 HardFault 로 승격)</text>
  <text x="300" y="132" fill="currentColor">2. configCHECK_FOR_STACK_OVERFLOW = 2: 문맥 전환 때</text>
  <text x="314" y="150" fill="currentColor">스택 끝 16 B 패턴 검사 → 오버플로 훅 호출</text>
  <text x="300" y="178" fill="currentColor">3. = 1: 문맥 전환 때 SP 가 범위 안인지만 (싸지만 놓침)</text>
  <text x="300" y="206" fill="currentColor">4. uxTaskGetStackHighWaterMark(): 지금까지 최소 여유 (워드)</text>
  <text x="314" y="224" fill="currentColor">테스트 때 최악 입력을 넣고 재서 예산을 정한다</text>
  <text x="300" y="262" fill="currentColor">2·3 은 "전환 때" 검사라 이미 덮어쓴 뒤일 수 있다.</text>
  <text x="300" y="282" fill="currentColor">재귀는 1·4 의 전제(최대 깊이 = 상수)를 깨뜨린다.</text>
  <text x="10" y="318" fill="currentColor">레이아웃은 개념도. 실제 배치는 링커 스크립트·heap_x 구현마다 다르다 (RTOS-M08, RTOS-M09).</text>
</svg>
```

- **FreeRTOS 스택 크기는 워드 단위**: `xTaskCreate(fn, "fill", 512, ...)` 의 512 는 `StackType_t` 개수. Cortex-M 포트는 32비트라 2048 B. 이 레슨의 재귀(-O2 56 B/frame)라면 ISR·예외 프레임 몫을 빼고 대략 30여 칸 깊이에서 끝 — 64×64 지도 한 장도 못 칠한다. 반면 BFS + `uint16_t` 큐 127 엔트리는 254 B 의 **정적 배열**이고 크기가 컴파일 때 보인다.
- **정적 분석이 깨진다**: GCC/clang `-fstack-usage` 는 함수별 프레임(.su: 이 레슨 Cortex-M4 `ff_rec 56 static`)을 주고, 도구가 호출 그래프의 최장 경로를 더해 태스크 최악 스택을 낸다. 재귀는 호출 그래프에 **사이클**을 만들어 그 합이 정의되지 않는다 — 사람이 "최대 깊이"를 주석으로 박아야 하고, 그 값은 입력이 정한다.
- **MISRA C:2012 Rule 17.2** (Required): "Functions shall not call themselves, either directly or indirectly." JPL "Power of Ten" 규칙 1도 직접·간접 재귀를 금지한다. 쓰려면 문서화된 deviation + 깊이 상한 증명이 필요하다(C-A10).
- Don 의 SSD 경험으로 치면: FTL 펌웨어가 매핑 트리나 GC 후보를 재귀로 훑지 않는 이유와 같다. 컨트롤러 코어의 스택은 KB 단위, 맵 크기는 GB 단위 디스크에 비례한다.

**복잡도 정리** (n = 칠할 칸 수 또는 노드 수, h = 트리 높이)

| 함수 | 시간 | 보조 메모리 (최악) | 실측 대표값 |
|---|---|---|---|
| `ff_rec` | O(n), 호출 ≤ 1+4n | 콜 스택 (n+1)·프레임 | 뱀 1024² : 524,801 프레임, 41 MB (-O2) |
| `ff_dfs_iter` | O(n) | ≤ n 엔트리 | 열린 256² 모서리 32,641 / 뱀 1 |
| `ff_bfs` | O(n) | ≤ n 엔트리, 열린 격자 O(w+h) | 열린 256² 가운데 511 |
| `trav_rec` | O(n) | 콜 스택 (h+1)·프레임 | 사슬 10^5 : 6.1 MiB (-O0) |
| `trav_iter` | O(n) | 전위·중위 ≤ h, 후위 = h | 완전 트리 20 / 사슬 중위 10^5 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 46/46 passed (0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 46/46, 보고 없음
```

```text
random grids: 300, filled cells 13247, mismatches rec=0 dfs=0 bfs=0, bound=0, cap-edge=0
random BSTs: 300, nodes 29580, mismatches iter!=rec=0 inorder!=sorted=0 shape=0 peak=0
big inputs: chain tree 1e6 in-order peak=1000000 pre-order peak=1; serpentine 500500 cells: dfs peak=1 bfs peak=1
warehouse 24x12: free=123 reachable=97 (sealed 26), rec depth=82, dfs peak=5, bfs peak=3
open floor 64x64: bfs peak=127 entries (508 B), dfs peak=1134 entries (4536 B)
46/46 passed
```

검증 방법: 격자는 재귀·스택·큐 어느 것도 안 쓰는 **반복 완화(relaxation) 기준 구현**("표시된 이웃이 있으면 표시"를 변화가 없을 때까지)과 칸 단위 `memcmp`. 무작위 격자에는 이미 칠해진 색(2)과 `to == from` 경우가 섞여 있다. 트리는 중위 = `qsort` 결과, 전위·뒤집은 후위를 BST 에 다시 넣으면 같은 모양(둘 다 "부모가 자식보다 먼저"인 순서), 그리고 재귀판과 정확히 같은 수열. 무작위는 고정 시드 xorshift32.

## 7. 자주 하는 실수

**1. `from == to` 검사를 뺀다.** 칠해도 색이 그대로라 같은 칸을 영원히 다시 부른다. 실제로 그 조건만 지우면 하네스가 첫 출력도 못 찍고 **exit 139 (SIGSEGV)**. LeetCode 733 의 단골 함정이다.

```c
if (from != to && in_grid(g, r, c) && g->cell[r * g->w + c] == from) {   /* from != to 필수 */
```

**2. 네 호출을 한 식에 쓴다.** `n = 1 + f(up) + f(down) + f(left) + f(right);` 는 피연산자 평가 순서가 미지정(C11 6.5p3, 함수 호출끼리는 indeterminately sequenced)이다. UB 는 아니고 칠한 **집합**은 같지만, 방문 순서·깊이·반복판과의 비교가 컴파일러마다 달라질 수 있다. → `n += ...` 를 줄마다.

**3. pop 할 때 칠한다 (또는 검사 없이 다 넣는다).** 결과는 맞아서 테스트를 잘 통과한다 — 이 레슨 하네스도 무작위 300개는 통과했고, 3×3 손 추적 피크(`pk == 3`, pop-mark 판은 5) 하나만 FAIL 했다. 문제는 메모리: 상한이 n → 1+E → 1+4n 으로 커지고, cap = n 으로 잡은 배열이 lazy 판에서 넘친다(뱀 64×64 에서 실측 5,025 > 2,080). → push 와 동시에 칠한다.

```c
if (!is_push(&s, nr * g->w + nc)) { n = B11_EOVERFLOW; break; }
g->cell[nr * g->w + nc] = to;          /* push 와 같은 자리에서 */
```

**4. 후위 순회에서 `last` 를 안 본다.** 오른쪽 부분 트리를 끝내고 부모로 돌아와 다시 오른쪽으로 내려간다. 실제로 `last != t[v].right` 를 지우면 `out[]` 을 넘어 쓰다 **exit 138 (SIGBUS)**, ASan 은 `stack-buffer-overflow ... WRITE of size 4 in trav_iter`. → `last` 기록, 또는 (노드, 상태) 프레임.

**5. 전위에서 왼쪽을 먼저 push 한다.** 스택은 LIFO 라 오른쪽이 먼저 나온다. 실제로 바꾸면 손 BST 전위가 틀리고 무작위 BST 263/300 불일치(44/46). → 오른쪽 먼저 push.

**6. 넘침을 "일어날 리 없다"로 처리한다.** `buf[top++] = x` 에 용량 검사가 없으면 펌웨어에선 옆 변수를 조용히 덮는다. → push 가 bool 을 돌려주고 함수는 에러 코드. 테스트는 cap 을 일부러 작게 주고 버퍼 뒤 카나리를 확인한다(MPU 가드를 흉내 낸 것).

**7. "꼬리 재귀니까 컴파일러가 루프로 바꿔 준다"고 믿는다.** C 표준은 보장하지 않는다. 이 맥 -O2 에서 마지막 한 방향만 루프로 바뀌었고(Cortex-M4 asm 에 `bl` 3개), -O0 에서는 아니었다 — 같은 소스가 디버그 빌드에서만 죽는다(Part A 표, 512×512).

## 8. 면접 Q&A

<details><summary>Q1. 임베디드에서 재귀는 절대 금지인가요?</summary>

MISRA C:2012 Rule 17.2 (Required) 와 JPL Power of Ten 규칙 1이 금지하고, 안전 인증 코드에서는 사실상 금지다. 일반 펌웨어에서도 기준은 "**깊이 상한을 입력과 무관한 상수로 증명할 수 있나**"다. 균형 트리(높이 ≤ c·log n), 퀵소트의 "작은 쪽 먼저 재귀 + 큰 쪽 루프"(깊이 ≤ log2 n, ALGO-B06), 고정 깊이 파서 정도는 상한을 적고 high-water mark 로 확인하면 받아들여진다. flood fill·DFS 는 깊이가 입력 크기라서 안 된다. 꼬리: "상한을 어떻게 문서화?" → `-fstack-usage` 프레임 × 최대 깊이 + ISR 몫, 주석과 정적 assert.

</details>

<details><summary>Q2. 태스크 스택이 몇 바이트 필요한지 어떻게 정하나요?</summary>

두 가지를 다 한다. (1) 정적: `-fstack-usage` 로 함수별 프레임을 얻고 호출 그래프 최장 경로를 더한다 + 인터럽트 중첩 시 예외 프레임(Cortex-M 기본 32 B, FPU 확장 104 B — 단 RTOS 에선 ISR 이 MSP 를 쓰므로 태스크 스택엔 진입 프레임만). 함수 포인터·재귀가 있으면 사람이 채워야 한다. (2) 동적: 생성 시 0xA5 로 칠하고 최악 입력으로 돌린 뒤 `uxTaskGetStackHighWaterMark()`(워드 단위 최소 여유)를 본다. 그 위에 여유 20–30% 와 `configCHECK_FOR_STACK_OVERFLOW = 2`, 가능하면 MPU 가드. 자세한 예산 계산은 RTOS-M08, 검출은 RTOS-M09.

</details>

<details><summary>Q3. 격자 flood fill 에서 DFS 와 BFS 중 메모리가 적은 쪽은?</summary>

"지도에 따라"가 정답이고 숫자로 말한다. 열린 바닥에서는 BFS 큐가 프런티어(마름모 둘레)만 담아 O(w+h): 256×256 가운데에서 511 엔트리 vs DFS 스택 16,830. 복도에서는 둘 다 1. 보장 상한은 둘 다 n(push 할 때 칠하면). 재귀는 열린 바닥에서도 깊이 ≈ n 에 엔트리가 16–20배 크다. 꼬리: "트리는?" → 반대. 균형 트리에서 DFS 는 높이(20), BFS 는 최대 폭(2^19). 꼬리 2: "메모리를 더 줄이려면?" → 스캔라인 채우기(한 줄 구간 단위로 push), 또는 작은 큐 + 재시드 스캔(연습 4, 시간과 맞바꿈).

</details>

<details><summary>Q4. visited 표시를 push 때 하나요, pop 때 하나요? BFS 최단 거리에서는?</summary>

push(enqueue) 때. 그래야 칸마다 한 번만 들어가 cap = n 이 보장된다. pop 때 칠하면 결과는 맞지만 push 총량 상한이 간선 수(1+E)로, 이웃 검사까지 빼면 4n 으로 커진다(실측 2×, 5×). BFS 최단 거리는 pop 때 표시해도 거리값 자체는 맞게 만들 수 있지만(첫 pop 이 최단), 같은 칸이 큐에 여러 번 들어가 큐가 커진다. 일반 그래프에선 O(V) vs O(E) — 조밀한 그래프면 V² 차이.

</details>

<details><summary>Q5. 보조 배열이 꽉 차면 어떻게 하나요? malloc 으로 늘리면 안 되나요?</summary>

펌웨어에서 런타임 확장은 보통 금지(단편화, 최악 시간 비결정). 선택지: (1) cap 을 보장 상한(n)으로 정적 할당 — 4096칸 × `uint16_t` = 8 KB. (2) 실측 피크 + 여유로 잡고 넘치면 **에러를 보고**하고 상위에서 처리(이 레슨의 `B11_EOVERFLOW`, 격자는 부분적으로 칠해진 상태라 사본에서 돌리거나 다시 시작). (3) 넘친 칸은 버리고 격자를 다시 훑어 이어 가는 방식(연습 4) — 메모리 상수, 시간 최악 O(n²/cap) 쪽으로 나빠짐. 어떤 경우에도 `buf[cap]` 을 쓰는 건 답이 아니다.

</details>

<details><summary>Q6. 칸 번호를 int 로 r*w+c 하면 문제없나요? MCU 에서는?</summary>

`w*h` 가 `INT_MAX` 를 넘으면 부호 있는 곱셈 오버플로 = UB. 문제에서 `w*h ≤ 2^31−1` 을 제약으로 못 박거나 `size_t`/`uint32_t` 로 계산한다. 반대로 MCU 에서는 **줄이는** 쪽이 이득: 64×64 = 4096칸이면 `uint16_t` 로 충분해 큐·스택 메모리가 절반. 나눗셈 `p / w`, `p % w` 는 Cortex-M0 처럼 하드웨어 나눗셈이 없는 코어에서 라이브러리 호출이 되므로, w 를 2의 거듭제곱(64)으로 잡고 시프트·마스크로 바꾸거나 (r, c) 를 따로 넣는다.

</details>

<details><summary>Q7. ISR 안에서 이 flood fill 을 돌려도 되나요?</summary>

안 된다. 실행 시간이 O(n) 이라 인터럽트 지연(latency)을 망치고, Cortex-M 에서 ISR 은 MSP(메인 스택)를 쓰는데 MSP 는 모든 ISR 이 중첩까지 나눠 쓰는 공용 스택이라 가장 작게 잡는다 — 재귀라면 최악이다. ISR 에서는 "지도 갱신됨" 플래그나 큐 메시지만 보내고, 채우기는 우선순위 낮은 태스크에서 정적 큐로 한다(RTOS-B11 메시지 큐).

</details>

## 9. 추가 연습

### 연습 1 — 재귀를 "그대로" 옮기기: 프레임 스택

반복 스택판(Part B)은 방문 순서가 재귀와 다르다. 프레임 = (칸, 다음에 볼 방향 0..4) 를 쌓아 **방문 순서까지 재귀와 똑같이** 만들고, 피크 = 재귀 깊이 − 1 임을 무작위 격자 300개로 확인하라.

<details><summary>답</summary>

재귀 호출 = push(칸, 0), return = pop, "호출에서 돌아와 다음 이웃으로" = 맨 위 프레임의 방향 += 1. 들어갈 때 칠하는 것도 재귀와 같다. 범위 밖 bail-out 프레임은 push 하지 않으니 피크는 재귀 깊이 − 1. 실측: 300개 44,636칸, 순서 불일치 0, `peak == depth−1` 위반 0. 프레임이 5 B(칸 4 + 방향 1)라 재귀 64–80 B 보다 작지만 상한은 여전히 n — "모양만 바꾼 재귀"다. 컴파일러가 재귀를 반복으로 바꿀 때 하는 일이 정확히 이것이다.

</details>

### 연습 2 — Morris 중위 순회 (LeetCode 94 Binary Tree Inorder Traversal 의 O(1) 메모리 판)

스택도 재귀도 없이 중위 순회하라. 끝나면 트리가 원래대로여야 한다.

<details><summary>답</summary>

왼쪽 자식이 있으면 왼쪽 부분 트리의 가장 오른쪽 노드(중위 선행자)의 `right` 에 현재 노드를 임시로 걸고(thread) 왼쪽으로 간다. 다시 그 선행자에 도착했을 때 실이 걸려 있으면 "왼쪽 끝" → 실을 풀고 방문 후 오른쪽. 시간 O(n)(간선마다 최대 몇 번), 보조 O(1). 대가: 순회 중 트리를 **쓴다** — 읽기 전용 플래시의 트리, 다른 태스크가 동시에 읽는 트리에는 못 쓴다. 무작위 BST 300개에서 재귀 중위와 불일치 0, 복구 안 됨 0.

</details>

### 연습 3 — 최대 깊이 세 가지 (LeetCode 104 Maximum Depth of Binary Tree)

재귀, BFS 레벨 큐, DFS (노드, 깊이) 스택으로 구현하고 완전 이진 트리 1023 노드에서 큐 피크와 스택 피크를 비교하라.

<details><summary>답</summary>

BFS 는 레벨 하나를 통째로 꺼내며 depth++. DFS 는 (노드, 깊이) 쌍을 오른쪽 먼저 push. 실측: 깊이 10, **BFS 큐 피크 512**(마지막 레벨 폭 = 2^9) vs **DFS 스택 피크 10**. 큐 cap 100 이면 `EOVF`. 격자와 정반대 — "DFS = 깊이, BFS = 폭". 무작위 BST 300개에서 세 방법 모두 하네스의 높이와 일치, DFS 피크 ≤ h+1.

</details>

### 연습 4 — 작은 큐로도 정확한 flood fill: 넘치면 다시 훑기

큐 cap 이 1–8 이어도 결과가 기준 구현과 같게 하라. 메모리는 O(cap), 시간은 늘어나도 좋다.

<details><summary>답</summary>

진행 중 칸은 임시 색 `TEMP`(0xFF, 원래 격자에 없는 값)로 칠한다. enqueue 가 실패하면 그 칸은 **칠하지 않고** `dropped = true` 만 남긴다. 큐가 비면, dropped 였을 때 격자 전체를 훑어 "TEMP 옆의 from 칸"을 다시 넣는다(훑다가 또 차면 한 바퀴 더). 큐가 비고 이번 바퀴에 버린 게 없으면 끝 → TEMP 를 `to` 로. 매 바퀴 최소 한 칸이 칠해지므로 끝난다. 실측: 400개(cap 1–8) 불일치 0, 재시드 606번. 64×64 빈 바닥 cap 8 → 4096칸 모두, 재시드 2번. 종료·정확성의 핵심은 "버린 칸은 from 으로 남아 다시 찾힌다".

</details>

### 연습 5 — 틀린 판으로 cap = n 을 넘겨 보기

"검사 없이 이웃 4개를 다 push, pop 때만 검사"하는 판으로, 칸 수만큼 잡은 스택이 넘치는 지도를 찾아라.

<details><summary>답</summary>

뱀 복도 64×64(2,080칸): cap = 2,080 → `EOVF`. cap = 4n+1 로 주면 결과는 맞지만 피크 5,025 (2.4n). 벽 칸까지 들어가고, 복도를 따라가는 동안 아래쪽에 쌓인 "되돌아갈 이웃"들이 끝까지 안 빠진다. Part B 의 상한 1+4n 이 실제로 쓰이는 예.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- 재귀 깊이는 입력이 정한다. flood fill 은 최악 n+1 (뱀 복도, 열린 바닥 모서리 모두 실측 n+1). 프레임은 이 맥 64/80 B, Cortex-M4 40/56 B (-O0/-O2).
- 이 맥(macOS 27.2): `ulimit -s`·실행 중 `setrlimit` 모두 메인 스레드에 적용 → 넘으면 SIGSEGV(139). pthread 가드 → SIGBUS. 스레드 크기는 `pthread_attr_setstacksize`.
- 반복으로 바꾸면 메모리가 사라지지 않고 **내 배열로 옮겨진다**: 엔트리 4 B, 넘치면 에러 코드, 크기가 컴파일 때 보인다.
- **push 할 때 칠한다** → 칸마다 1번 → cap = n 이면 절대 안 넘침. pop 때 칠하면 1+E, 검사 없이 넣으면 1+4n.
- 격자: BFS 큐 = 프런티어 O(w+h)(열린 바닥), DFS 스택은 n/4–n/2. 트리: 반대 (DFS = 높이, BFS = 폭).
- 후위 반복 = 왼쪽 끝까지 push + `last` 로 오른쪽 완료 판단. 스택 피크 = 높이.
- 꼬리 재귀 제거는 보장이 아니다 (-O2 에서만 마지막 방향이 루프가 됨). MISRA C:2012 Rule 17.2, FreeRTOS 스택은 워드 단위, `configCHECK_FOR_STACK_OVERFLOW = 2` + high-water mark + MPU 가드.

## 참고자료

- MISRA C:2012 *Guidelines for the use of the C language in critical systems*, Rule 17.2 "Functions shall not call themselves, either directly or indirectly" (Required).
- G. J. Holzmann, "The Power of Ten — Rules for Developing Safety Critical Code", *IEEE Computer* 39(6), 2006 — 규칙 1: goto·setjmp/longjmp·직접/간접 재귀 금지.
- FreeRTOS — `xTaskCreate` (usStackDepth 는 바이트가 아니라 워드 수): <https://www.freertos.org/a00125.html>
- FreeRTOS — Stack usage and stack overflow checking (`configCHECK_FOR_STACK_OVERFLOW` 1/2, `vApplicationStackOverflowHook`): <https://www.freertos.org/Stacks-and-stack-overflow-checking.html>
- FreeRTOS — `uxTaskGetStackHighWaterMark`: <https://www.freertos.org/uxTaskGetStackHighWaterMark.html>
- Arm, *ARMv7-M Architecture Reference Manual* — 예외 진입 스택 프레임(기본 8 워드, FP 확장), MPU 영역 최소 32 B. *ARMv8-M Architecture Reference Manual* — MSPLIM/PSPLIM 스택 한계 레지스터와 STKOF UsageFault.
- GCC 문서 — `-fstack-usage`, `-Wstack-usage=`: <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html> (이 레슨은 Apple clang 21 의 같은 옵션으로 Cortex-M4 .su 를 얻었다).
- ISO/IEC 9899:2011 (C11) 6.5p3 — 부분식 평가 순서 미지정, 6.5.2.2p10 — 함수 호출 간 indeterminately sequenced.
- J. M. Morris, "Traversing binary trees simply and cheaply", *Information Processing Letters* 9(5), 1979 — Morris 순회 (서지는 기억에 의존, 확인 권장).
- LeetCode 733 Flood Fill, 200 Number of Islands, 94 / 144 / 145 Binary Tree Inorder / Preorder / Postorder Traversal, 104 Maximum Depth of Binary Tree.
- AlgoMonster 노트 M04 §1 Recursion Intro (Don 정리본) — 재귀와 호출 스택; §2 Trees 순회 순서.
- 관련 레슨: DS-B02 (배열 스택), DS-B03 (링 버퍼), DS-B13 (BST 순회), C-B11 (스택은 어디에), C-A10 (MISRA 스타일 검사), ALGO-B06 (퀵소트 스택 깊이 O(log n)), ALGO-M04 (점유 격자 BFS 최단 경로·덩어리), RTOS-M08 (태스크 스택 예산), RTOS-M09 (카나리·패턴 검사), RTOS-B11 (메시지 큐).
