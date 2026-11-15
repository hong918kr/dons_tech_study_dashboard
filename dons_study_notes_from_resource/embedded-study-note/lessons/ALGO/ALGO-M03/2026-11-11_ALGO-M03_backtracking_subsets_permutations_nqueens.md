---
id: ALGO-M03
time: 90분
updated: 2026-11-11
---
# ALGO-M03 백트래킹: 부분집합·순열·N-Queens

## 학습 목표

- **선택-탐색-취소(choose - explore - unchoose)** 템플릿 하나로 부분집합 합 · 중복 원소 순열 · N-Queens · 핀 배치를 짜고, "취소가 정확히 되돌리는 상태가 무엇인지" 를 코드로 말할 수 있다.
- 가지치기(pruning)가 **해를 하나도 잃지 않는다**는 것을 각 조건마다 증명하고, 방문 노드 수를 실측해 비교한다 (부분집합 n = 20: 2,097,151 → 5,185 노드, N-Queens n = 8: 19,173,961 → 109,601 → 2,057 노드).
- 중복 원소 순열에서 `!used[i-1]` 규칙이 **같은 값 순열을 정확히 한 번** 만든다는 것을 대표원(canonical representative) 논증으로 증명하고, 개수를 n!/∏kᵥ! 와, 순서를 `next_permutation` 과 대조한다.
- N-Queens 를 열·두 대각선 **비트마스크**로 짜서 n = 0..12 개수를 OEIS A000170 과 맞추고, 재귀 깊이 n+1 프레임이 Cortex-M4 에서 몇 바이트인지(프레임 40 B, 실측 asm) 말할 수 있다.
- 펌웨어 핀 다중화(pin-mux) 배치를 같은 템플릿 + MRV(가장 제약이 큰 변수 먼저) + 전방 검사(forward checking)로 풀고, "JTAG 를 끄면 해가 생긴다" 같은 결론을 숫자로 낸다.

## 1. 문제

백트래킹은 "모든 경우를 만들어 봐라" 류 면접 문제의 표준 답이고, 펌웨어 쪽에서는 꼬리질문이 거의 정해져 있다 — **재귀 깊이는 얼마이고 스택은 몇 바이트냐, 결과를 malloc 없이 어디에 담느냐, 가지치기가 해를 놓치지 않는다는 근거는 뭐냐.**

템플릿은 하나다. 상태(state)를 하나 들고 다니며, 후보마다 ① 상태에 넣고(choose) ② 재귀로 내려가고(explore) ③ 상태에서 뺀다(unchoose). 상태가 원래대로 돌아와야 다음 후보가 깨끗한 상태에서 시작한다. 가지치기는 ② 전에 "이 아래에는 해가 없다" 를 **증명할 수 있을 때만** 내려가지 않는 것이다. 재귀 깊이는 결정 수 + 1 (루트) 이라 이 레슨의 모든 문제에서 n+1 프레임이다 (재귀·스택 일반론은 ALGO-B06/B11).

아래 그림은 Part A 예제 `{3,34,4,12,5,2}`, target 9 를 오름차순 `[2,3,4,5,12,34]` 로 정렬해 가지치기한 실제 탐색 트리다 (23 노드, 가지치기 없으면 2⁷−1 = 127 노드). 왼쪽 가지 = 넣음, 오른쪽 점선 = 뺌, 원 안 숫자 = 현재 합.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">부분집합 합 [2,3,4,5,12,34], target 9 — 가지치기한 트리 (23 노드)</text>
  <g stroke="currentColor" fill="none">
    <line x1="318" y1="60" x2="145" y2="120"/><line x1="318" y1="60" x2="492" y2="120" stroke-dasharray="4 3"/>
    <line x1="145" y1="120" x2="79" y2="180"/><line x1="145" y1="120" x2="210" y2="180" stroke-dasharray="4 3"/>
    <line x1="492" y1="120" x2="384" y2="180"/><line x1="492" y1="120" x2="601" y2="180" stroke-dasharray="4 3"/>
    <line x1="79" y1="180" x2="50" y2="240"/><line x1="79" y1="180" x2="108" y2="240" stroke-dasharray="4 3"/>
    <line x1="210" y1="180" x2="166" y2="240"/><line x1="210" y1="180" x2="253" y2="240" stroke-dasharray="4 3"/>
    <line x1="384" y1="180" x2="340" y2="240"/><line x1="384" y1="180" x2="427" y2="240" stroke-dasharray="4 3"/>
    <line x1="601" y1="180" x2="543" y2="240"/><line x1="601" y1="180" x2="659" y2="240" stroke-dasharray="4 3"/>
    <line x1="253" y1="240" x2="224" y2="300"/><line x1="253" y1="240" x2="282" y2="300" stroke-dasharray="4 3"/>
    <line x1="427" y1="240" x2="398" y2="300"/><line x1="427" y1="240" x2="456" y2="300" stroke-dasharray="4 3"/>
    <line x1="543" y1="240" x2="514" y2="300"/><line x1="543" y1="240" x2="572" y2="300" stroke-dasharray="4 3"/>
    <line x1="659" y1="240" x2="630" y2="300"/><line x1="659" y1="240" x2="688" y2="300" stroke-dasharray="4 3"/>
  </g>
  <g stroke="currentColor">
    <circle cx="318" cy="60" r="14" class="f-mut"/><circle cx="145" cy="120" r="14" class="f-mut"/><circle cx="492" cy="120" r="14" class="f-mut"/>
    <circle cx="79" cy="180" r="14" class="f-mut"/><circle cx="210" cy="180" r="14" class="f-mut"/><circle cx="384" cy="180" r="14" class="f-mut"/><circle cx="601" cy="180" r="14" class="f-mut"/>
    <circle cx="50" cy="240" r="14" class="f-ok"/><circle cx="108" cy="240" r="14" class="f-warn"/><circle cx="166" cy="240" r="14" class="f-warn"/>
    <circle cx="253" cy="240" r="14" class="f-mut"/><circle cx="340" cy="240" r="14" class="f-warn"/><circle cx="427" cy="240" r="14" class="f-mut"/>
    <circle cx="543" cy="240" r="14" class="f-mut"/><circle cx="659" cy="240" r="14" class="f-mut"/>
    <circle cx="224" cy="300" r="14" class="f-warn"/><circle cx="282" cy="300" r="14" class="f-warn"/><circle cx="398" cy="300" r="14" class="f-warn"/>
    <circle cx="456" cy="300" r="14" class="f-warn"/><circle cx="514" cy="300" r="14" class="f-ok"/><circle cx="572" cy="300" r="14" class="f-warn"/>
    <circle cx="630" cy="300" r="14" class="f-warn"/><circle cx="688" cy="300" r="14" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="318" y="64">0</text><text x="145" y="124">2</text><text x="492" y="124">0</text>
    <text x="79" y="184">5</text><text x="210" y="184">2</text><text x="384" y="184">3</text><text x="601" y="184">0</text>
    <text x="50" y="244">9</text><text x="108" y="244">5</text><text x="166" y="244">6</text><text x="253" y="244">2</text>
    <text x="340" y="244">7</text><text x="427" y="244">3</text><text x="543" y="244">4</text><text x="659" y="244">0</text>
    <text x="224" y="304">7</text><text x="282" y="304">2</text><text x="398" y="304">8</text><text x="456" y="304">3</text>
    <text x="514" y="304">9</text><text x="572" y="304">4</text><text x="630" y="304">5</text><text x="688" y="304">0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="680" y="64" font-size="11">k=0 (2?)</text><text x="680" y="124" font-size="11">k=1 (3?)</text><text x="680" y="184" font-size="11">k=2 (4?)</text><text x="680" y="230" font-size="11">k=3 (5?)</text>
    <text x="10" y="340">녹색 = 해 {2,3,4}, {4,5}.  주황 = "합 + w[k] &gt; 9" (정렬돼 있으니 뒤 원소도 전부 넘침) → 더 내려가지 않고 끝.</text>
    <text x="10" y="360">k=4 의 12, 34 는 한 번도 고려되지 않는다. 실선 = 넣음(choose → explore → unchoose), 점선 = 뺌.</text>
    <text x="10" y="380">solution.c 실측: prune 0 → 127 노드, prune 1 → 23 노드, 둘 다 해 2개.</text>
  </g>
</svg>
```

### Part A — 부분집합 합: 모든 해와 개수

`size_t subset_sum_all(const uint32_t *w, size_t n, uint64_t target, int prune, uint32_t *out, size_t cap, uint64_t *nodes)`

- 합이 `target` 인 **모든** 부분집합을 원래 인덱스 비트마스크로 `out[0..cap)` 에 쓰고, 개수는 cap 을 넘어도 끝까지 센다 (malloc 없는 펌웨어식 출력: 호출자 버퍼 + 전체 개수). n ≤ 24.
- `prune = 0`: 넣음/뺌 완전 이진 트리 — 노드 수가 정확히 2ⁿ⁺¹−1 인지 테스트가 확인. `prune = 1`: 오름차순 정렬 + 두 가지치기 (남은 합 부족 / 다음 원소부터 초과).
- 상황: 보드 3.3 V 레일 전류가 37 mA 올랐다. 주변장치별 소비 전류표(무게)에서 "어떤 조합이 켜졌나" 를 찾으면 바로 이 문제다. 해가 둘 이상이면 측정만으로는 구별이 안 된다는 것까지 알려 준다.
- 검증: 무게 ∈ {0..3}ⁿ (n ≤ 6) 의 **모든 배열 × 모든 target**, 무작위 n = 16 × 120개를 2ⁿ 마스크 전수와 집합 비교. 0 무게, 빈 입력, 합이 32비트를 넘는 무게(`0xFFFFFFFF` × 4).

### Part B — 중복 원소 순열 · N-Queens 비트마스크

1. `size_t perm_all(const int32_t *a, size_t n, int dedup, int32_t *out, size_t cap, uint64_t *nodes)` — 사전순 순열. `dedup` 이면 같은 값 순열은 한 번만. 개수 = n!/∏kᵥ! (MISSISSIPPI → 34650). {0,1,2} 위 길이 0..7 의 **모든 배열**(3,280개)에서 출력 열 전체를 `next_permutation` 과 한 줄씩 대조.
2. `uint64_t nqueens_count(unsigned n, int mode, uint64_t *nodes)` — `NQ_BITS` 를 구현한다 (열·대각선 비트마스크). 가지치기 없는 두 기준판 `NQ_NAIVE`(행마다 n 칸 아무거나, 끝에서 검사)와 `NQ_PERM`(열만 겹치지 않게)은 주어져 있다. n = 0..12 를 OEIS A000170 과 대조하고, 세 모드의 노드 수를 표로 만든다.

### Part C — 핀 다중화(pin-mux) 배치

STM32F1 류 MCU 는 주변장치마다 **리맵(remap) 옵션** 몇 개 중 하나를 고른다 (예: USART1 = PA9/PA10 또는 PB6/PB7). 핀은 하나의 기능만 가질 수 있고, SWD/JTAG·크리스털(HSE)·패키지에 없는 핀은 쓸 수 없다. `pinmux_search(p, np, reserved, mode, &res)` 는 핀이 겹치지 않는 **모든 배치의 수**와 하나의 배치를 돌려준다. mode 0(기준, 주어짐) = 다 고른 뒤 검사, mode 1 = 겹치면 즉시 가지치기, mode 2 = + MRV + 전방 검사. 무작위 400 인스턴스를 직교곱 전수와 대조하고, 보드 시나리오 5개의 개수를 **손으로 센 값**과 맞춘다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">Part C 보드 — 옵션(상자)과 핀 충돌(선). 시나리오 S4 = LQFP48 + 전체 JTAG + HSE + USB</text>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="75" y="36">USART1</text><text x="245" y="36">I2C1</text><text x="415" y="36">CAN1</text><text x="575" y="36">USB</text>
    <text x="95" y="156">SPI1</text><text x="300" y="156">TIM3</text><text x="470" y="156">USART2</text><text x="605" y="156">ADC1</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="40" width="110" height="26" class="f-mut"/><rect x="20" y="72" width="110" height="26" class="f-mut"/>
    <rect x="190" y="40" width="110" height="26" class="f-mut"/><rect x="190" y="72" width="110" height="26" class="f-mut"/>
    <rect x="360" y="40" width="110" height="26" class="f-mut"/><rect x="360" y="72" width="110" height="26" class="f-mut"/><rect x="360" y="104" width="110" height="26" class="f-warn"/>
    <rect x="530" y="40" width="90" height="26" class="f-acc"/>
    <rect x="20" y="160" width="150" height="26" class="f-mut"/><rect x="20" y="192" width="150" height="26" class="f-warn"/>
    <rect x="250" y="160" width="100" height="26" class="f-mut"/><rect x="250" y="192" width="100" height="26" class="f-warn"/><rect x="250" y="224" width="100" height="26" class="f-warn"/>
    <rect x="420" y="160" width="100" height="26" class="f-mut"/><rect x="420" y="192" width="100" height="26" class="f-warn"/>
    <rect x="560" y="160" width="90" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="11">
    <text x="75" y="57">PA9/PA10</text><text x="75" y="89">PB6/PB7</text>
    <text x="245" y="57">PB6/PB7</text><text x="245" y="89">PB8/PB9</text>
    <text x="415" y="57">PA11/PA12</text><text x="415" y="89">PB8/PB9</text><text x="415" y="121">PD0/PD1</text>
    <text x="575" y="57">PA11/PA12</text>
    <text x="95" y="177">PA4-PA7</text><text x="95" y="209">PA15/PB3/PB4/PB5</text>
    <text x="300" y="177">PA6/PA7</text><text x="300" y="209">PB4/PB5</text><text x="300" y="241">PC6/PC7</text>
    <text x="470" y="177">PA2/PA3</text><text x="470" y="209">PD5/PD6</text><text x="605" y="177">PA0/PA1</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <line x1="130" y1="85" x2="190" y2="53"/>
    <line x1="300" y1="85" x2="360" y2="85"/>
    <line x1="470" y1="53" x2="530" y2="53"/>
    <line x1="170" y1="173" x2="250" y2="173"/>
    <line x1="170" y1="205" x2="250" y2="205" stroke-dasharray="5 3"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="280">주황 = S4 에서 막힌 옵션: SPI1 리맵과 TIM3 PB4/PB5 (PA15/PB3/PB4 는 JTAG 핀),</text>
    <text x="20" y="298">CAN1 PD0/PD1 (HSE 크리스털), TIM3 PC6/PC7 과 USART2 PD5/PD6 (48핀 패키지에 없음).</text>
    <text x="20" y="320">USB 고정 → CAN1 = PB8/PB9 → I2C1 = PB6/PB7 → USART1 = PA9/PA10 (사슬로 강제).</text>
    <text x="20" y="338">SPI1 = PA4-PA7 뿐 → TIM3 PA6/PA7 과 충돌, TIM3 의 나머지 두 옵션은 막힘 → 해 0.</text>
    <text x="20" y="356">JTAG 를 끄고 SWD 만 쓰면 (S5) PA15·PB3·PB4 가 풀려 해 2개.</text>
    <text x="20" y="374" font-size="11">선 = 핀이 겹치는 옵션 쌍. 핀 번호는 RM0008 AFIO 리맵 표 기준 기억값 — 확인 권장.</text>
  </g>
</svg>
```

## 2. Problem (English)

> **Backtracking toolkit.** C11, no heap allocation, results go to caller-provided buffers (store up to `cap`, but always return the full count). Count every call of your recursive function as one visited node (including the root).
>
> 1. `subset_sum_all(w, n, target, prune, out, cap, nodes)`: all subsets of the non-negative weights `w[0..n)` (n ≤ 24) whose sum is `target`, as bitmasks over the original indices. With `prune == 0` the search must visit exactly 2ⁿ⁺¹−1 nodes; with `prune == 1` it may visit fewer but must return the same set.
> 2. `perm_all(a, n, dedup, out, cap, nodes)`: all permutations of `a` in lexicographic order; if `dedup`, each distinct sequence exactly once. Example: `[1,1,2]` → `[1,1,2], [1,2,1], [2,1,1]`.
> 3. `nqueens_count(n, NQ_BITS, nodes)`: number of ways to place n non-attacking queens on an n×n board, n ≤ 16, using bitmasks for columns and both diagonals. `n = 8 → 92`.
> 4. `pinmux_search(p, np, reserved, mode, res)`: each peripheral has 1–4 options, each option is a set of pins (`uint64_t` mask). Choose one option per peripheral so that no pin is used twice and no reserved pin is used. Return the number of valid assignments and one of them.
>
> Follow-ups: max recursion depth and stack bytes on a Cortex-M4; why your pruning never loses a solution; what changes if weights can be negative.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 재귀 함수 네 개의 본문 — `ss_rec`, `perm_rec`, `nq_bits_rec`, `pm_rec`. 바깥 준비 함수(정렬, suffix 합, 컨텍스트 초기화)와 기준판(`NQ_NAIVE`, `NQ_PERM`, pin-mux mode 0)은 주어져 있다. 스텁은 노드만 1 세고 해 0 개를 돌려준다. 처음 실행 (실제 출력, 발췌):

```text
FAIL starter.c:339  k == 2 && got[0] == want[0] && got[1] == want[1]
…
Part A exhaustive: 207532 cases (w in {0..3}^n, n<=6), mismatches=96248, nodes!=2^(n+1)-1: 103764
Part B perms exhaustive: 3280 arrays over {0,1,2}, len<=7: seq mismatches=3280, count!=n!/prod k!: 3280, no-dedup nodes/count wrong: 3280
Part B N-Queens bitmask n=0..12 vs OEIS A000170: MISMATCH
Part C random: 400 instances (200 feasible), count mismatches=400, bad picks=400
…
3/31 passed
```

(exit 1. 통과 3개는 "해 0개" 가 정답인 사례 — n = 0 에 target 5, 옵션 0개 주변장치, n > NQ_MAXN.)

## 4. 힌트

<details><summary>힌트 1 — 상태를 어디에 두나: 컨텍스트 vs 인자</summary>

부분집합·순열·pin-mux 는 상태(합, 마스크, used[], cur[], used 핀)를 컨텍스트 구조체에 두고 choose 에서 바꾸고 unchoose 에서 **정확히 반대 연산**으로 되돌린다. N-Queens 는 상태(세 마스크)가 워드 3개라 인자로 값 전달하면 unchoose 가 "함수에서 돌아오는 것" 자체가 된다. 둘 다 같은 템플릿이다.

</details>

<details><summary>힌트 2 — 부분집합 가지치기 두 개</summary>

오름차순 정렬 후 k 번째 노드에서: (1) `sum + suffix[k] < target` — 남은 걸 전부 넣어도 모자라면 끝. (2) `sum + w[k] > target` — w[k] 가 넘치면 그 뒤 원소는 더 크니 전부 넘친다. 이때 가능한 완성은 "나머지 전부 빼기" 하나이므로 `sum == target` 이면 해 하나를 내고 끝. k == n 도 (2)와 같이 처리하면 코드가 짧아진다.

</details>

<details><summary>힌트 3 — 같은 값 순열을 한 번만</summary>

정렬해서 같은 값을 붙여 두고, 같은 값의 복사본은 **인덱스 순서대로만** 쓰게 한다: `a[i] == a[i-1]` 인데 `i-1` 이 아직 안 쓰였으면 `i` 를 건너뛴다. 그러면 [1ₐ,1ᵦ,2] 에서 1ᵦ 가 1ₐ 보다 먼저 나오는 배치는 만들어지지 않는다.

</details>

<details><summary>힌트 4 — N-Queens 대각선 마스크는 한 행마다 한 칸씩 민다</summary>

(r, c) 의 퀸은 다음 행에서 c+1 과 c−1 을 공격한다. "이번 행에서 공격받는 열" 집합 d1, d2 를 들고 다니다가 다음 행으로 갈 때 `d1 = (d1 | bit) << 1`, `d2 = (d2 | bit) >> 1`. 놓을 수 있는 열은 `full & ~(cols | d1 | d2)`, 가장 낮은 비트는 `x & -x` (부호 없는 `0u - x`).

</details>

<details><summary>힌트 5 — MRV 와 전방 검사는 루프 하나</summary>

아직 안 고른 주변장치마다 "지금 used 와 안 겹치는 옵션 수" 를 센다. 0 인 게 하나라도 있으면 이 노드 아래엔 해가 없다 → return. 아니면 수가 가장 작은 주변장치로 분기한다. 이 루프 하나가 MRV(minimum remaining values)와 전방 검사를 동시에 한다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 해를 **전부** 원하나 개수만? 순서(사전순)? 원소에 중복·음수·0 이 있나? n 의 최대? 결과를 담을 메모리(펌웨어: 호출자 버퍼)? |
| 2. 무식한 방법 | 부분집합 2ⁿ 마스크 전수 O(n·2ⁿ), 순열 n! 개 만든 뒤 중복 제거(정렬 O(n!·n log n!)), N-Queens nⁿ 배치 검사. 모두 테스트 기준으로 남긴다 |
| 3. 더 나은 생각 + 불변식 | 결정을 하나씩 내리며 **부분해가 유효한 동안만** 내려간다. 불변식: 각 노드의 상태 = 루트에서 그 노드까지의 선택만 반영 (unchoose 가 보장). 가지치기 조건마다 "아래에 해가 없다" 증명 |
| 4. 코드 | choose → explore → unchoose 세 줄을 먼저 쓰고, 종료 조건, 가지치기를 붙인다 |
| 5. 소리 내어 말할 테스트 | n = 0 (해 = 빈 집합/빈 순열 1개), 0 무게, 전부 같은 값, target = 0, target > 총합, N-Queens n = 2·3 (해 0), 큰 무게 합 오버플로 |
| 6. 복잡도 | 시간 = 노드 수 × 노드당 일 (출력 크기에 비례하는 하한이 있다). 추가 메모리 O(n) 상태, **스택 깊이 n+1 프레임** |

### Part A — 부분집합 합: 두 가지치기가 해를 잃지 않는 이유

트리: 깊이 k 노드는 "w[0..k) 의 포함 여부를 정했다". 불변식: `c->sum` = 지금까지 넣은 무게의 합, `c->mask` = 넣은 원래 인덱스. choose 에서 `sum += w[k]`, `mask |= bit`, unchoose 에서 정확히 빼므로 두 번째 explore(뺀 갈래)는 부모와 같은 상태에서 시작한다. 가지치기 없으면 모든 깊이 n 노드(2ⁿ 개)가 리프이므로 노드 = 2ⁿ⁺¹ − 1 — 테스트가 n = 0..6 의 모든 입력에서 정확히 이 값인지 확인한다.

가지치기 증명 (무게 ≥ 0, 오름차순 w[0] ≤ … ≤ w[n−1]):

1. `sum + suffix[k] < target` 이면 이 노드 아래 어떤 완성도 합 ≤ sum + suffix[k] < target. 해 없음.
2. `sum + w[k] > target` 이면 j ≥ k 인 모든 j 에 w[j] ≥ w[k] 이므로 하나라도 넣으면 합 > target. 남은 완성은 "전부 뺌" 하나이고 그 합은 sum. 그래서 `sum == target` 이면 그것만 내고 끝. (`w[k] == 0` 이면 조건이 거짓이라 0 을 넣는/빼는 두 갈래가 모두 탐색된다 — 0 무게 해가 2배가 되는 것이 맞는 답이고, 테스트 `{0,0,1}` → 4개가 그것이다.)

두 조건 모두 **무게 ≥ 0 과 정렬**에 기댄다. 음수가 있으면 (1)·(2) 모두 틀린다 (7. 실수 5). 합은 `uint64_t` — n ≤ 24, w ≤ 2³²−1 이면 합 < 2³⁷.

손 추적 (그림의 트리, 정렬 `[2,3,4,5,12,34]`, suffix = `[60,58,55,51,46,34,0]`):

| 노드 (DFS 순) | k | sum | 선택 | 판단 |
|---|---|---|---|---|
| 1 | 0 | 0 | {} | 0+60 ≥ 9, 0+2 ≤ 9 → 분기 |
| 2 | 1 | 2 | {2} | 2+3 ≤ 9 → 분기 |
| 3 | 2 | 5 | {2,3} | 5+4 ≤ 9 → 분기 |
| 4 | 3 | 9 | {2,3,4} | 9+5 > 9, sum == 9 → **해** |
| 5 | 3 | 5 | {2,3} | 5+5 > 9, sum ≠ 9 → 끝 |
| 6 | 2 | 2 | {2} | 2+4 ≤ 9 → 분기 |
| … | | | | (그림 참고) |
| 19 | 4 | 9 | {4,5} | 9+12 > 9, sum == 9 → **해** |
| 합계 | | | | 23 노드, 해 2개 (원래 인덱스 마스크 0x25, 0x14) |

실측 (solution.c, n = 20, 무게 1..1000):

```text
Part A nodes (n=20, sum=9458):  target | solutions | nodes no-prune | nodes prune
      1500 |        10 |        2097151 |        5185
      2364 |        49 |        2097151 |       43773
      4729 |       356 |        2097151 |      539771
      9451 |         0 |        2097151 |          39
```

demo_nodes.c 의 n = 24 (이 맥 `-O2`, 5회 중앙값 ms) 실제 출력:

```text
3) subset sum n=24 (weights 1..1000), median of 5 runs
   sum=11639
   target | solutions | nodes no-prune   ms    | nodes prune   ms
     2000 |        64 |     33554431    56.65 |       45907   0.07
     5819 |      4980 |     33554431    58.33 |     7983883  18.39
    11636 |         0 |     33554431    57.31 |          47   0.00
```

target 이 총합의 절반 근처일 때 가지치기 효과가 가장 작다 (부분합이 target 근처에 몰려 두 조건이 늦게 걸린다). target = 총합 − 7 은 "합 7 이하의 원소를 빼야 한다" 인데 그런 원소가 없어 suffix 조건이 거의 루트에서 끊는다. **최악은 그대로 2ⁿ⁺¹−1** 이다: 무게가 전부 0 이고 target = 0 이면 2ⁿ 개 리프가 모두 해라 출력 자체가 2ⁿ 이다. 부분집합 합 판정은 NP-완전(NP-complete)이므로 다항 최악을 기대하면 안 된다. 개수만 필요하고 target 이 작으면 O(n·T) 의사 다항(pseudo-polynomial) DP 가 낫다 (ALGO-M07/M08).

### Part B-1 — 중복 원소 순열: `!used[i-1]` 의 증명

```svg
<svg viewBox="0 0 740 275" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">[1ₐ, 1ᵦ, 2] — 같은 깊이의 1ᵦ 는 1ₐ 가 안 쓰였으면 건너뛴다 (9 노드, 리프 3)</text>
  <g stroke="currentColor" fill="none">
    <line x1="370" y1="55" x2="170" y2="105"/><line x1="370" y1="55" x2="370" y2="105" stroke-dasharray="3 3"/><line x1="370" y1="55" x2="570" y2="105"/>
    <line x1="170" y1="105" x2="110" y2="155"/><line x1="170" y1="105" x2="230" y2="155"/>
    <line x1="570" y1="105" x2="510" y2="155"/><line x1="570" y1="105" x2="630" y2="155" stroke-dasharray="3 3"/>
    <line x1="110" y1="155" x2="110" y2="205"/><line x1="230" y1="155" x2="230" y2="205"/><line x1="510" y1="155" x2="510" y2="205"/>
  </g>
  <g stroke="currentColor">
    <rect x="345" y="42" width="50" height="24" class="f-mut"/>
    <rect x="145" y="93" width="50" height="24" class="f-mut"/><rect x="345" y="93" width="50" height="24" class="f-warn"/><rect x="545" y="93" width="50" height="24" class="f-mut"/>
    <rect x="85" y="143" width="50" height="24" class="f-mut"/><rect x="205" y="143" width="50" height="24" class="f-mut"/>
    <rect x="485" y="143" width="50" height="24" class="f-mut"/><rect x="605" y="143" width="50" height="24" class="f-warn"/>
    <rect x="70" y="193" width="80" height="24" class="f-ok"/><rect x="190" y="193" width="80" height="24" class="f-ok"/><rect x="470" y="193" width="80" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="370" y="59">()</text><text x="170" y="110">1ₐ</text><text x="370" y="110">1ᵦ ✕</text><text x="570" y="110">2</text>
    <text x="110" y="160">1ₐ1ᵦ</text><text x="230" y="160">1ₐ2</text><text x="510" y="160">2 1ₐ</text><text x="630" y="160">2 1ᵦ ✕</text>
    <text x="110" y="210">1 1 2</text><text x="230" y="210">1 2 1</text><text x="510" y="210">2 1 1</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="243">✕ = a[i] == a[i-1] 이고 used[i-1] == 0 → 건너뜀.</text>
    <text x="10" y="263">남는 리프는 1ₐ 가 1ᵦ 보다 앞에 오는 배치뿐 = 같은 값 순열의 대표 하나씩.</text>
  </g>
</svg>
```

**주장.** 정렬된 a 에서 `perm_rec(dedup=1)` 의 리프는 서로 다른 값 순열과 1:1 이고, 사전순으로 나온다.

**증명.** 인덱스 순열 π 마다 값 열 a∘π 가 있다. 값 v 가 kᵥ 번 나오면 그 복사본끼리만 바꾼 kᵥ! 개 인덱스 순열이 같은 값 열을 준다 → 같은 값 열의 동치류 크기는 ∏kᵥ!, 동치류 수 = n!/∏kᵥ!. 각 동치류에는 "각 값의 복사본이 인덱스 오름차순으로 놓인" 인덱스 순열이 **정확히 하나** 있다 (대표원). 규칙 "a[i] = a[i−1] 이고 i−1 미사용이면 i 금지" 는 복사본 i 를 놓을 때 i−1 이 이미 놓여 있기를 요구하므로, 경로에서 규칙이 한 번도 걸리지 않는 것 ⇔ 모든 값의 복사본이 인덱스 순서로 놓임 (귀납: 같은 값 묶음 j < j+1 < … 에서 각 쌍의 순서가 강제되면 전체 순서가 강제된다). 그래서 리프 = 대표원 = 서로 다른 값 열.
**막다른 가지가 없다**: 깊이 < n 인 노드에서 아직 남은 값 v 가 있으면 v 의 "가장 앞 미사용 복사본" 은 앞 복사본이 사용됐거나(같은 값) 다른 값이라 항상 허용된다. 그래서 모든 내부 노드는 자식이 있고, 노드 수 = 서로 다른 값 접두사의 수다.
**사전순**: 각 깊이에서 i 를 오름차순으로 돌고, 같은 값은 대표 하나만 통과하므로 자식이 값 오름차순으로 나온다. 테스트가 3,280 개 배열 전체에서 `next_permutation` 의 열과 한 줄씩 같음을 확인했다.

반대 규칙 `used[i-1]` (앞 복사본이 **쓰였으면** 건너뜀)도 결과는 같다 — 이번엔 복사본을 인덱스 **역순**으로 강제한다. 하지만 막다른 가지가 생긴다: 묶음의 뒤쪽이 아닌 복사본 j 를 먼저 놓으면 j+1 은 영원히 놓을 수 없는데, 탐색은 그 사실을 남은 깊이를 다 내려가 본 뒤에야 안다. demo_nodes.c 실측:

```text
2) duplicate-skip rule: !used[i-1] vs used[i-1]  (same leaves, different nodes)
   [1,1,2]              leaves      3 /      3   nodes !used=       9  used=      12
   [1,1,1,2,2,3,3,3]    leaves    560 /    560   nodes !used=    1749  used=    8496
   [1,1,1,1,2,2,2,2]    leaves     70 /     70   nodes !used=     251  used=    4695
   [0 x10]              leaves      1 /      1   nodes !used=      11  used=   49864
   [0x5,1x5]            leaves    252 /    252   nodes !used=     923  used=   91177
```

중복 제거를 끄면 노드 = Σₖ n!/(n−k)! (길이 k 의 서로 다른 인덱스 접두사 수, OEIS A000522: 1, 2, 5, 16, 65, …) — 테스트가 n ≤ 7 의 모든 배열에서 이 공식과 같음을 확인. `[1,1,1,2,2,3,3,3]` 에서 109,601 노드 → 1,749 노드, 리프 40,320 → 560 = 8!/(3!·2!·3!).

### Part B-2 — N-Queens: 비트마스크 상태와 정확성

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">n = 4 해 (1,3,0,2) 를 찾는 경로 — 행마다 cols / d1 / d2 / avail (열 0 이 왼쪽)</text>
  <g stroke="currentColor">
    <rect x="30" y="40" width="40" height="40" class="f-mut"/><rect x="70" y="40" width="40" height="40" class="f-acc"/><rect x="110" y="40" width="40" height="40" class="f-mut"/><rect x="150" y="40" width="40" height="40" class="f-mut"/>
    <rect x="30" y="80" width="40" height="40" class="f-warn"/><rect x="70" y="80" width="40" height="40" class="f-warn"/><rect x="110" y="80" width="40" height="40" class="f-warn"/><rect x="150" y="80" width="40" height="40" class="f-acc"/>
    <rect x="30" y="120" width="40" height="40" class="f-acc"/><rect x="70" y="120" width="40" height="40" class="f-warn"/><rect x="110" y="120" width="40" height="40" class="f-warn"/><rect x="150" y="120" width="40" height="40" class="f-warn"/>
    <rect x="30" y="160" width="40" height="40" class="f-warn"/><rect x="70" y="160" width="40" height="40" class="f-warn"/><rect x="110" y="160" width="40" height="40" class="f-acc"/><rect x="150" y="160" width="40" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold" font-size="14">
    <text x="90" y="65">Q</text><text x="170" y="105">Q</text><text x="50" y="145">Q</text><text x="130" y="185">Q</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="220" y="50">row  cols  d1    d2    avail  놓은 열</text>
    <text x="220" y="70">0    0000  0000  0000  1111   1 (avail 의 최저 비트는 0 이지만</text>
    <text x="220" y="88">                               0 가지는 막혀 되돌아온 뒤)</text>
    <text x="220" y="110">1    0100  0010  1000  0001   3</text>
    <text x="220" y="150">2    0101  0001  0010  1000   0</text>
    <text x="220" y="190">3    1101  0100  0100  0010   2  → cols = 1111 = full → 해</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="222">주황 = 그 행에서 cols | d1 | d2 로 막힌 칸.</text>
    <text x="10" y="240">d1 은 매 행 &lt;&lt; 1 (오른쪽 아래 대각선), d2 는 &gt;&gt; 1 (왼쪽 아래 대각선).</text>
    <text x="10" y="258">avail = full &amp; ~(cols | d1 | d2): 판 밖으로 밀려난 비트는 full 로 잘린다.</text>
  </g>
</svg>
```

**불변식.** 행 r 노드에서 `cols` = 위 r 개 퀸의 열 집합, `d1` = {c₀ + (r − r₀)} (각 퀸 (r₀,c₀) 의 오른쪽 아래 대각선이 행 r 에서 지나는 열), `d2` = {c₀ − (r − r₀)}. 다음 행으로 갈 때 모든 원소가 +1 / −1 되므로 `<< 1` / `>> 1` 이 정확히 그 갱신이고, 새 퀸 bit 를 먼저 OR 한다. 범위 밖 열(≥ n 또는 < 0)은 이후 행에서도 계속 범위 밖으로만 움직이므로 `full &` 로 버려도 된다 (`>> 1` 은 0 아래를 자동으로 버리고, `<< 1` 로 넘친 비트는 uint32 에서 정의된 동작으로 사라진다 — n ≤ 16 이라 무관). 그러므로 `avail` = 위 퀸들과 충돌하지 않는 열 정확히 그것이고, 각 노드는 **서로 공격하지 않는 부분 배치** 하나와 1:1 이다. 노드 수 = Σᵣ (첫 r 행의 비공격 배치 수).

세 모드 노드 수 (solution.c 출력; naive 는 n ≤ 7 실행, n = 8·9 는 공식 Σ nᵏ):

```text
Part B N-Queens bitmask n=0..12 vs OEIS A000170: all equal
Part B N-Queens nodes:  n | solutions | naive (n^n) | perm (cols only) | bitmask (cols+diags)
     4 |         2 |         341  |               65 |                   17
     5 |        10 |        3906  |              326 |                   54
     6 |         4 |       55987  |             1957 |                  153
     7 |        40 |      960800  |            13700 |                  552
     8 |        92 |    19173961* |           109601 |                 2057
     9 |       352 |   435848050* |           986410 |                 8394
```

demo_nodes.c 는 같은 세 모드를 더 큰 n 까지 돌린다 (이 맥 Apple Silicon M 계열, `-O2`, 5회 중앙값 ms; 다른 작업과 동시에 돌아 절대값은 흔들린다 — 비율을 볼 것):

```text
   n | solutions |   naive nodes   ms     |   perm nodes     ms     | bitmask nodes   ms    | ns/node(bits)
   8 |        92 |     19173961     55.14 |       109601      0.84 |         2057     0.00 |      -
  10 |       724 |            -         - |      9864101     71.77 |        35539     0.15 |   4.22
  11 |      2680 |            -         - |    108505112    748.21 |       166926     0.93 |   5.55
  12 |     14200 |            -         - |            -         - |       856189     4.71 |   5.50
  14 |    365596 |            -         - |            -         - |     27358553   146.58 |   5.36
  15 |   2279184 |            -         - |            -         - |    171129072   978.57 |   5.72
```

n = 8 에서 naive → 열만 → 비트마스크가 약 175배, 다시 53배 노드를 줄인다. n = 11 에서 열만 가지치기(순열 탐색)는 748 ms, 비트마스크는 0.93 ms — 이번 실행 기준 약 800배 (노드 비는 650배, 나머지는 리프의 O(n²) 검사 비용). 노드당 5 ns 남짓. 해 개수(n = 13: 73,712, 14: 365,596, 15: 2,279,184)도 A000170 과 같다.

### Part C — pin-mux: 같은 템플릿, 변수 순서만 다르다

변수 = 주변장치, 값 = 리맵 옵션, 제약 = 핀 집합이 서로·예약 핀과 겹치지 않음. 상태 `used` (예약 + 고른 핀). choose 는 `used |= m`, unchoose 는 `used &= ~m` — m 이 used 와 겹치지 않았을 때만 골랐으니 이 복원은 정확하다 (겹친 상태에서 `&= ~m` 하면 예약 핀까지 지워진다 — 실수 1 의 변형).

**정확성.** (a) 개수: 각 노드에서 **한 변수**의 가능한 값 전부로 분기하므로 그 노드 아래 해 집합이 서로소인 조각으로 나뉜다 → 해마다 리프 하나. 변수 순서를 노드마다 바꿔도(MRV) 이 성질은 그대로다. (b) mode 1 가지치기: 고른 옵션끼리 이미 겹치면 어떤 완성도 무효. (c) 전방 검사: 미배정 주변장치 하나가 가능한 옵션 0 이면 어떤 완성도 그 주변장치를 채울 수 없다. 셋 다 "아래에 해가 없다" 증명이라 개수가 바뀌지 않는다 — 무작위 400개에서 세 모드 = 직교곱 전수.

**손으로 센 시나리오 값** (테스트의 기대값):

- S1 (100핀, SWD 만, HSE): USART2 2가지 × ADC 1 × {USART1, I2C1, CAN1} 4가지 (CAN=PA11/12 일 때 I2C1=PB6/7→USART1=PA9/10 1 + I2C1=PB8/9→USART1 2 = 3, CAN=PB8/9 → I2C1=PB6/7 → USART1=PA9/10 1) × {SPI1, TIM3} 4가지 (SPI 기본 → TIM3 PB4/5·PC6/7, SPI 리맵 → TIM3 PA6/7·PC6/7) = **32**.
- S2 (전체 JTAG): SPI 리맵·TIM3 PB4/5 막힘 → {SPI1,TIM3} 1가지 → 2×4×1 = **8**.
- S3 (S1 + USB): CAN 기본 막힘 → 체인 1가지 → 2×1×4 = **8**.
- S4 (48핀 + JTAG + USB): 위 그림 — **0**. S5 (S4 에서 SWD 만): {SPI1,TIM3} 가 (기본, PB4/5) 또는 (리맵, PA6/7) → **2**.

```text
Part C board:  scenario                         | count | nodes m0 | m1 | m2
    S1 LQFP100, SWD only, HSE          |    32 |      367 | 105 |  68
    S2 LQFP100, full JTAG, HSE         |     8 |      367 |  43 |  22
    S3 S1 + USB                        |     8 |      511 | 113 |  20
    S4 LQFP48, full JTAG, HSE, USB     |     0 |      511 |  15 |   3
    S5 LQFP48, SWD only, HSE, USB      |     2 |      511 |  40 |  11
    S5 first solution (MRV): USART1=PA9/PA10 USART2=PA2/PA3 I2C1=PB6/PB7 SPI1=PA4-PA7 CAN1=PB8/PB9 TIM3=PB4/PB5 ADC1=PA0/PA1 USB=PA11/PA12
```

mode 0 은 직교곱 2·2·2·2·3·3·1(·1) = 144 리프라 노드 367 (= 1+2+4+8+16+48+144+144). S4 에서 MRV 는 3 노드 만에 "해 없음" 을 증명한다: 루트에서 가능 옵션이 1개인 주변장치 중 번호가 가장 작은 USART2(PD5/PD6 없음)를, 다음 노드에서 SPI1(리맵 막힘)을 고르고, SPI1 = PA4-PA7 을 놓는 순간 TIM3 의 가능 옵션이 0 이 된 것을 전방 검사가 본다. SPI1 에 다른 옵션이 없으니 루트까지 그대로 끝. 펌웨어 쪽 결론이 숫자로 나온다: **JTAG 를 끄고 SWD 만 쓰면(STM32F1 의 `AFIO_MAPR.SWJ_CFG`, 확인 권장) 48핀 보드에서 배치가 가능해진다.**

왜 탐욕/매칭이 아니라 백트래킹인가: 옵션이 핀 **하나**라면 "주변장치-핀 이분 매칭" 이라 다항 시간에 하나를 찾는다(Hopcroft–Karp). 여기는 옵션이 핀 **집합**이라 집합 포장(set packing) 꼴이고 일반적으로 NP-어렵다. 또 "모든 배치 개수" 는 단순한 이분 완전 매칭 개수조차 #P-완전(Valiant 1979)이다. 실제 보드는 주변장치 수십 개 이하라 가지치기 백트래킹이 현실적인 답이다.

### 복잡도와 스택

| 함수 | 노드 수 (최악) | 노드당 일 | 추가 메모리 | 재귀 깊이 |
|---|---|---|---|---|
| subset_sum_all | 2ⁿ⁺¹−1 (출력이 2ⁿ 일 수 있음) | O(1) + 해 출력 O(1) (마스크) | 컨텍스트 392 B (arm64 sizeof) | n+1 |
| perm_all (dedup) | Σ 서로 다른 접두사 ≤ Σ n!/(n−k)! | O(n) (루프) + 출력 O(n) | O(n) | n+1 |
| nqueens NQ_BITS | Σᵣ 비공격 부분 배치 (지수적, 닫힌 식 모름) | O(1) 워드 연산 | 0 (인자 4개) | n+1 |
| pinmux mode 2 | ∏ 옵션 수 (최악) | O(np·옵션) (MRV 스캔) | O(np) | np+1 |

재귀 깊이 n+1 은 작지만 펌웨어에선 바이트로 말해야 한다. demo_nodes.c (arm64 `-O2`, 깊이 계측판 — 계측 변수 때문에 원본보다 프레임이 크다):

```text
4) recursion depth & frame bytes (bitmask N-Queens, depth-instrumented copy)
   n= 8 solutions=92 max depth=9 frames (rows + root), stack span=768 B -> 96.0 B/frame
   n=12 solutions=14200 max depth=13 frames (rows + root), stack span=1152 B -> 96.0 B/frame
```

Cortex-M4 (`demo_m4.c`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`, 실제 asm 발췌):

```text
nq_bits_rec:
	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
	sub	sp, #4
	...
.LBB0_3:                                @ =>This Inner Loop Header: Depth=1
	rsbs	r0, r5, #0                  @ 0 - avail
	and.w	r6, r5, r0                  @ bit = avail & -avail
	orr.w	r0, r6, r9
	lsls	r2, r0, #1                  @ (d1 | bit) << 1
	orr.w	r0, r6, r8
	orr.w	r1, r6, r10
	lsrs	r3, r0, #1                  @ (d2 | bit) >> 1
	mov	r0, r11
	eor.w	r7, r6, r5                  @ avail ^ bit
	bl	nq_bits_rec
```

레지스터 9개 push(36 B) + `sub sp, #4` = **40 B/프레임** — `-fstack-usage` 도 `nq_bits_rec 40 static` 으로 같은 값을 냈다. Cortex-M0 (`thumbv6m`)는 `push {r4, r5, r6, r7, lr}` + `sub sp, #12` = 32 B. n = 12 → 13 프레임 × 40 B = 520 B. ISR 이 아닌 태스크에서, 스택 여유를 이 숫자로 잡는다. 깊이가 입력에 비례해 커질 수 있는 문제(부분집합 n = 24 → 25 프레임)라도 n 에 상한이 있어 정적 분석이 가능하다. 재귀가 금지된 코드(MISRA C:2012 Rule 17.2)라면 행마다 상태 16 B 를 배열 스택에 두는 반복판(연습 2)으로 바꾼다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 31/31 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 31/31, 보고 없음
```

실제 출력 (위에 이미 붙인 표 제외):

```text
Part A exhaustive: 207532 cases (w in {0..3}^n, n<=6), mismatches=0, nodes!=2^(n+1)-1: 0
Part A random: 120 instances n=16, total solutions=17968, mismatches=0
Part B perms exhaustive: 3280 arrays over {0,1,2}, len<=7: seq mismatches=0, count!=n!/prod k!: 0, no-dedup nodes/count wrong: 0
Part B perms [1,1,1,2,2,3,3,3]: no-dedup leaves=40320 nodes=109601 | dedup leaves=560 nodes=1749
Part C random: 400 instances (200 feasible), count mismatches=0, bad picks=0
31/31 passed
```

(전체 약 0.16 초 user. 207,532 = Σₙ 4ⁿ × (3n+2) × 2 모드.)

## 7. 자주 하는 실수

**1. unchoose 를 빠뜨리거나 반만 한다.** `c->sum += w[k]` 후 재귀하고 `sum -= w[k]` 를 잊으면 뺀 갈래가 "넣은 상태" 로 시작한다. 마스크만 되돌리고 합은 안 되돌리는 반쪽 실수가 더 흔하다 — 출력 마스크는 그럴듯한데 합이 틀린다. 규칙: choose 에서 바꾼 **모든** 필드를 unchoose 에서 역순으로 되돌린다. 아니면 N-Queens 처럼 상태를 값으로 넘겨 되돌릴 일을 없앤다.

```c
c->sum += c->w[k]; c->mask |= 1u << c->idx[k];      /* choose */
ss_rec(c, k + 1);
c->sum -= c->w[k]; c->mask &= ~(1u << c->idx[k]);   /* unchoose: 둘 다 */
```

**2. 정렬 없이 `a[i] == a[i-1]` 로 중복 제거.** `[1,2,1]` 은 같은 값이 붙어 있지 않아 규칙이 아무것도 안 거른다 → 6개 출력(정답 3개). 중복 제거 규칙은 "같은 값이 인접" 을 전제로 한다. `perm_all` 이 입력을 먼저 삽입 정렬하는 이유.

**3. `used[i-1]` 와 `!used[i-1]` 를 헷갈림.** 둘 다 **맞는 답**을 내서 테스트로는 안 잡힌다. 차이는 노드 수: 0 이 10개일 때 11 vs 49,864 (위 실측). 면접에서 "왜 !used 인가" 를 물으면 "같은 깊이에서 같은 값을 두 번 시작하지 않게 하는 것 = 첫 레벨에서 바로 자른다, 반대 규칙은 막다른 가지를 끝까지 내려간다" 로 답한다.

**4. 결과로 상태 버퍼의 포인터를 저장.** `results[k] = c->cur;` 는 모든 결과가 같은 버퍼를 가리켜, 탐색이 끝나면 전부 마지막 상태(또는 빈 접두사)를 보인다. 리프에서 **복사**한다 (`memcpy(out + count*n, cur, n*sizeof *cur)`). 펌웨어에서는 출력이 너무 크면 콜백으로 하나씩 넘기고(7번 Q 참고) 저장하지 않는다.

**5. 가지치기 전제를 확인하지 않음.** 두 조건은 **정렬 + 무게 ≥ 0** 에서만 맞다 (이 레슨 API 가 `uint32_t` 무게인 이유). 음수를 허용한 정수판으로 옮기면 바로 해를 잃는다. (a) `[-5, 3]`, target 3: 루트에서 `sum + suffix = 0 + (−2) < 3` → 잘려서 {3} 을 놓친다. (b) 정렬된 `[-5, -1, 3]`, target −6: 루트에서 `0 + (−5) > −6` → "뒤도 전부 넘친다" 로 끝내서 {−5, −1} 을 놓친다 (음수 두 개를 **같이** 넣으면 더 작아진다). 음수가 있으면 suffix 조건을 "남은 양수 합 / 남은 음수 합" 상·하한으로 바꾸고 (2)는 버린다. 면접에선 "무게가 음수일 수 있나?" 를 먼저 묻는다.

**6. N-Queens 에서 `full &` 를 빠뜨리거나 부호 있는 시프트.** `avail = ~(cols | d1 | d2)` 만 쓰면 n 이상 열의 비트가 켜져 판 밖에 퀸을 놓는다 — `while (avail)` 이 끝나지 않거나 개수가 엉터리. 마스크를 `int` 로 두면 `1 << 31`·음수 왼쪽 시프트가 UB(C11 6.5.7p4)다. 마스크는 `uint32_t`, 최저 비트는 `x & (0u - x)`.

**7. 개수 타입.** 12! = 479,001,600 은 32비트에 들어가지만 13! = 6,227,020,800 은 안 들어간다. 노드 수는 해 수보다 훨씬 크다 (n = 15 N-Queens 노드 1.7×10⁸, n = 24 부분집합 노드 3.4×10⁷ — 둘 다 32비트 안이지만 n = 32 부분집합이면 넘친다). 카운터는 `uint64_t`.

## 8. 면접 Q&A

<details><summary>Q1. 백트래킹, 완전 탐색, DP 는 뭐가 다르죠? 언제 메모이제이션이 되나요?</summary>

완전 탐색은 모든 후보를 만든 뒤 검사, 백트래킹은 부분해 단계에서 검사해 **가지째** 버린다(N-Queens n = 8: 1.9×10⁷ → 2,057 노드). DP/메모는 **같은 부분 문제가 여러 경로에서 반복**될 때 그 답을 재사용한다. 부분집합 합의 "개수" 는 상태가 (k, sum) 둘뿐이라 O(n·T) DP 가 되지만(ALGO-M07/M08), "해 목록 전부" 는 출력이 2ⁿ 일 수 있어 어떤 방법도 출력 크기보다 빠를 수 없다. N-Queens 는 상태(cols, d1, d2)가 거의 겹치지 않아 메모 이득이 작다. 꼬리: "부분집합 합이 NP-완전인데 DP 는 다항 아닌가?" → O(n·T) 는 T 의 **값**에 비례하고 입력 길이(log T 비트)에는 지수 → 의사 다항.

</details>

<details><summary>Q2. 시간 복잡도를 어떻게 말하죠? 가지치기를 넣으면 빅오가 바뀌나요?</summary>

"노드 수 × 노드당 일" 로 말한다. 부분집합 전체 열거 O(2ⁿ) 노드, 해를 배열로 내면 O(n·2ⁿ). 순열 O(n·n!) (리프 n! 개, 복사 n, 내부 노드는 Σ n!/(n−k)! ≈ e·n!). N-Queens 는 닫힌 식이 없고 상한 O(n!) 정도로 말한 뒤 실측 노드 수를 댄다. **가지치기는 최악 빅오를 보통 바꾸지 못한다** — 부분집합에서 무게가 전부 0 이면 2ⁿ 해가 다 나온다. 바뀌는 것은 실전 입력에서의 상수·지수 밑이다. 그래서 측정표(노드 수)가 면접에서 설득력이 있다. 꼬리: "출력에 민감한(output-sensitive) 경계는?" → 막다른 가지가 없는 dedup 순열은 노드 ≤ (해 수) × (n+1) 이라 출력 크기에 선형.

</details>

<details><summary>Q3. 재귀 깊이와 스택 — Cortex-M 태스크 스택 1 KB 에서 돌려도 되나요?</summary>

깊이는 결정 수 + 1 = n+1 로 **입력 크기 상한이 정한다** (B11 의 flood fill 처럼 데이터 모양이 정하는 게 아니다). N-Queens n = 12 는 Cortex-M4 -O2 에서 40 B × 13 = 520 B (실측 asm·`-fstack-usage`). 1 KB 태스크라면 다른 호출 체인·인터럽트 스태킹(예외 진입 시 8워드 32 B, FPU 컨텍스트면 더)까지 더해 빠듯하다. 대안: (1) 반복판 — 행마다 16 B 상태를 정적 배열에 (연습 2), (2) 깊이 상한을 상수로 두고 `-fstack-usage` + 호출 그래프로 정적 검증, (3) FreeRTOS `uxTaskGetStackHighWaterMark` 로 실측. ISR 안에서 탐색을 돌리는 건 시간 최악이 지수라 안 된다 — 태스크로 넘긴다.

</details>

<details><summary>Q4. 중복 순열에서 `!used[i-1]` 가 왜 맞는지 증명해 보세요.</summary>

5. 풀이 B-1 의 대표원 논증: 같은 값 열의 동치류 크기는 ∏kᵥ!, 각 동치류에 "복사본이 인덱스 순서로 놓인" 인덱스 순열이 하나뿐이고, 규칙이 정확히 그 순서를 강제한다. 그리고 가장 앞 미사용 복사본은 항상 허용되므로 막다른 가지가 없다. 대안 구현: 값→개수 표(freq)를 두고 "서로 다른 값마다 개수 > 0 이면 하나 쓰기" — used[] 대신 개수 배열, 같은 결과, 정렬 불필요(값 종류가 적으면 더 빠름). 꼬리: "다음 순열만 필요하면?" → `next_permutation` (재귀·추가 메모리 없음, 상각 O(1)) — 이 레슨의 테스트 기준이 바로 그것. LeetCode 47 Permutations II, 31 Next Permutation.

</details>

<details><summary>Q5. N-Queens 를 더 빠르게 하려면요?</summary>

(1) 좌우 대칭: 첫 행을 왼쪽 절반만 두고 ×2, 홀수 n 은 가운데 열 따로 — 노드 약 절반 (연습 3: n = 12 856,189 → 428,095). 8가지 대칭(회전·반사) 전부 쓰면 더 줄지만 경계 처리가 복잡하다. (2) 병렬화: 첫 한두 행의 배치마다 독립 부분 트리라 코어별로 나눈다 (통신 0). (3) 노드당 일: 이미 워드 연산 몇 개(위 Cortex-M4 루프 10명령). 알려진 최대 n 의 개수는 대규모 분산 계산으로 구했다(n = 27 까지 알려진 것으로 기억 — 확인 권장). 꼬리: "해 하나만 필요하면?" → 탐색 없이 만드는 명시적 구성법이 있다(n ≥ 4).

</details>

<details><summary>Q6. 핀 배치를 그냥 그리디로 하면 안 되나요? STM32CubeMX 는 어떻게 하죠?</summary>

그리디(주변장치 순서대로 첫 가능한 옵션)는 순서에 따라 S3 에서 실패한다: USART1 이 먼저 PB6/PB7 을 가져가면 I2C1 은 PB8/PB9 로 밀리고, USB 때문에 PB8/PB9 밖에 남지 않은 CAN1 이 막힌다. 되돌아가기가 필요하다. 옵션이 단일 핀이면 이분 매칭(다항), 핀 집합이면 집합 포장이라 일반적으로 NP-어렵다 — 그래서 MRV + 전방 검사 백트래킹이 실무적인 답이다 (S4 를 3 노드에 "불가능" 으로 증명). CubeMX 같은 도구도 핀 충돌을 표시하고 대체 핀을 제안하는데, 내부 알고리즘은 공개 문서로 확인하지 못했다 — 추측하지 않는다. 펌웨어 관점 꼬리: "부팅 때 동적으로?" → 아니다. 핀 배치는 보드 설계 시점(회로도)에 정해지고, 이 탐색은 설계 도구·빌드 스크립트·하드웨어 리비전 검증에 쓴다.

</details>

<details><summary>Q7. 결과가 수만 개면 malloc 없이 어떻게 돌려주죠?</summary>

세 가지 패턴. (1) 호출자 버퍼 + cap + 전체 개수 반환 (이 레슨, `snprintf` 와 같은 규약 — 모자라면 다시 부를 크기를 안다). (2) 방문자 콜백 `void (*visit)(const int32_t *perm, size_t n, void *ctx)` — 저장 없이 하나씩 넘기고, 콜백이 0 을 돌려주면 조기 종료(첫 해만 필요할 때). (3) 반복자(iterator) 상태 — `next_permutation` 이나 연습 2 의 명시적 스택을 구조체에 넣어 "다음 해" 를 부를 때마다 하나씩 (코루틴 없이 생성기). 펌웨어 테스트 벡터 생성이나 자체 시험(BIST) 패턴 열거에 (3) 이 잘 맞는다: 메모리 O(n), 중간에 멈췄다 이어갈 수 있다.

</details>

<details><summary>Q8. 무게에 음수가 있으면? n = 40 이면?</summary>

음수가 있으면 두 가지치기가 모두 무효다 (7. 실수 5). 상한/하한을 "남은 양수 합 / 남은 음수 합" 으로 바꾸면 suffix 조건은 살릴 수 있다. n = 40 이면 2⁴⁰ ≈ 10¹² 노드라 백트래킹으론 최악이 안 된다 → **반으로 나누기(meet-in-the-middle)**: 앞 20개 부분합 2²⁰ 개와 뒤 20개 부분합을 각각 만들어 정렬한 뒤, 투 포인터(ALGO-B09)나 이진 탐색(ALGO-B02)으로 합이 target 인 쌍의 수를 센다. O(2^(n/2)·n) 시간, O(2^(n/2)) 메모리(약 8 MB — MCU 가 아니라 호스트 도구 영역). 개수만 필요하고 T 가 작으면 O(n·T) DP.

</details>

## 9. 추가 연습

### 연습 1 — Combination Sum II (LeetCode 40)

후보에 같은 값이 있을 수 있고 각 원소는 한 번만 쓴다. 합이 target 인 **고유 조합**을 사전순으로. 예: `[10,1,2,7,6,1,5]`, 8 → `[1,1,6], [1,2,5], [1,7], [2,6]`. 무작위 300개를 마스크 전수 + 정렬 + 중복 제거와 대조.

<details><summary>답 보기</summary>

정렬 + `start` 인덱스 + "같은 깊이에서 같은 값은 한 번만 시작" (`i > start && a[i] == a[i-1]` 이면 skip) + `a[i] > remain` 이면 break (정렬돼 있으니 뒤도 전부 큼). 재귀는 `i + 1` 부터 (각 원소 1회; LeetCode 39 는 재사용이라 `i`). 순열의 `!used[i-1]` 와 같은 대표원 논리다: 같은 값 묶음에서 앞쪽 복사본부터 쓰는 조합만 남는다. 여기서는 `i > start` 조건이 "앞 복사본을 이번 깊이에서 이미 시작해 봤다" 와 같은 뜻이다. `[1,1,1,1,1,1]`, 3 → 마스크로는 C(6,3) = 20 개지만 고유 조합은 1 개. 실제 출력: `연습 1: 300 random instances, 951 unique combinations, mismatches=0`.

</details>

### 연습 2 — 재귀 없는 N-Queens

명시적 스택(행마다 `cols, d1, d2, avail` 16 B)으로 N-Queens 를 반복문으로 짜라. n = 0..12 개수 = A000170, 노드 수 = 재귀판 호출 수인지 확인.

<details><summary>답 보기</summary>

스택 칸 = 재귀 프레임의 지역 상태. `avail` 을 **스택 칸에 저장**해 두는 것이 핵심이다 — 되돌아왔을 때 "이 행에서 다음 후보" 를 이어서 고르는 재귀판의 `while (avail)` 루프 위치가 바로 그것이다. 칸의 avail 이 0 이면 pop(= 함수 반환 = unchoose). 최저 비트를 꺼내 지운 뒤(choose) 마지막 행이면 개수만 올리고, 아니면 다음 행 칸을 push(explore). 메모리 = n × 16 B 정적 배열(n ≤ 16 → 256 B), 깊이 초과가 불가능하다는 것이 배열 크기로 보장된다. 실제 출력: `연습 2: iterative N-Queens n=0..12 == A000170, nodes n=12: recursive=856189 iterative=856189, stack = 12 x 16 B`.

</details>

### 연습 3 — 좌우 대칭으로 절반만

첫 행 퀸을 왼쪽 절반 열에만 두고 결과를 2배 하라. 홀수 n 의 가운데 열은? 개수 = A000170, 노드 수 비교.

<details><summary>답 보기</summary>

좌우 반사 c → n−1−c 는 해를 해로 보내는 전단사이고, 첫 행 퀸이 왼쪽 절반(c < n/2)인 해를 오른쪽 절반인 해로 보낸다. 그래서 왼쪽 절반 시작 해 수 × 2 = 가운데 아닌 시작 해 수. 홀수 n 의 가운데 시작 해들은 반사해도 가운데 시작이라 따로 한 번만 센다 (그 안에서도 짝을 이루지만, 둘째 행에서 다시 대칭을 쓰지 않는 한 그대로 둔다). 실제 출력: `연습 3: mirror-halved N-Queens n=0..12 == A000170, nodes n=12: full=856189 halved=428095`.

</details>

### 연습 4 — 올바른 괄호 생성 (LeetCode 22)

n 쌍의 올바른 괄호열을 사전순(`'(' < ')'`)으로 전부 만들고 개수가 Catalan 수(OEIS A000108: 1, 1, 2, 5, 14, 42, 132, …, n = 12 → 208012)인지, n ≤ 6 은 2²ⁿ 전수와 같은지 확인.

<details><summary>답 보기</summary>

상태 (pos, open, close). `open < n` 이면 '(' 를, `close < open` 이면 ')' 를 놓는다 — 이 두 조건이 "접두사의 균형 ≥ 0, 여는 괄호 ≤ n" 이라는 **정확한** 가지치기라 막다른 가지가 없다(모든 리프가 해). '(' 를 먼저 시도하니 사전순. 상태를 값으로 넘기고 버퍼 한 칸만 덮어쓰므로 unchoose 가 필요 없다. 실제 출력: `연습 4: parentheses n=0..12 == Catalan (A000108): yes; n=3 last = ()()()`.

</details>

### 연습 5 — 회문 분할 개수 (LeetCode 131, AlgoMonster M05 §5)

문자열을 모든 조각이 회문이 되게 자르는 방법의 수. `"aab"` → 2 (`a|a|b`, `aa|b`). 길이 ≤ 16 무작위 {a,b} 문자열 400개를 2ⁿ⁻¹ 자르기 마스크 전수와 대조.

<details><summary>답 보기</summary>

start 부터 end 까지 조각이 회문이면 end 에서 재귀, 아니면 그 조각에서 바로 가지치기. 깊이 ≤ n+1. 같은 글자 n 개면 모든 자르기가 유효해 2ⁿ⁻¹ (`"aaaa"` → 8). 개수만 필요하면 `count(start)` 가 start 에만 의존하므로 메모 한 줄로 O(n²) 가 된다 (회문 표를 미리 만들면) — 백트래킹이 DP 로 넘어가는 경계를 보여 주는 문제다. 실제 출력: `연습 5: 400 random strings over {a,b}, len<=16: mismatches=0`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_nodes.c -o demo_nodes && ./demo_nodes   # 노드 수·시간·프레임 표 (약 9 초)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -   # Cortex-M4 asm
```

## 10. 핵심 정리

- 템플릿: **choose → explore → unchoose**. unchoose 는 choose 에서 바꾼 모든 필드를 정확히 되돌린다. 상태가 워드 몇 개면 값으로 넘겨 되돌릴 일을 없앤다 (N-Queens).
- 가지치기 = "이 아래에 해가 없다" 의 **증명**. 부분집합: 정렬 + 무게 ≥ 0 전제에서 `sum+suffix < T`, `sum+w[k] > T`. 전제(음수 없음)를 먼저 묻는다.
- 중복 순열: 정렬 + `a[i]==a[i-1] && !used[i-1] → skip` = 복사본을 인덱스 순서로만 = 동치류 대표원. 개수 n!/∏kᵥ!, 막다른 가지 없음. `used[i-1]` 도 맞지만 노드 11 → 49,864.
- N-Queens 비트마스크: `avail = full & ~(cols|d1|d2)`, `bit = avail & -avail`, 다음 행 `d1<<1`, `d2>>1`. n = 8: 1.9×10⁷ → 1.1×10⁵ → 2,057 노드. 개수 A000170 (8 → 92, 12 → 14200).
- 재귀 깊이 = 결정 수 + 1. Cortex-M4 N-Queens 프레임 40 B → n = 12 에 520 B. 금지면 명시적 스택(행당 16 B).
- 개수 정확성: 노드마다 **한 변수의 모든 값**으로 분기하면 해마다 리프 하나 — 변수 순서(MRV)를 바꿔도 개수는 그대로.
- pin-mux: MRV + 전방 검사로 불가능을 3 노드에 증명. 48핀 + JTAG 는 해 0, SWD 만 쓰면 해 2.
- 최악 빅오는 가지치기로 안 바뀐다 (출력 자체가 2ⁿ·n! 일 수 있다). 개수만 필요하면 DP·meet-in-the-middle 을 먼저 떠올린다.

## 참고자료

- AlgoMonster 노트 M05 §1~§4 (DFS with States, Backtracking 1, 전화번호 조합, Pruning), §5 (Partition a String Into Palindromes), §8 (Permutations), §14~§16 (Deduplication, Combination Sum, Subsets), §17 (Backtracking Speedrun) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M05_backtracking.md`
- OEIS A000170 — Number of ways of placing n nonattacking queens on an n×n board: <https://oeis.org/A000170>
- OEIS A000108 — Catalan numbers: <https://oeis.org/A000108> · OEIS A000522 — Σₖ n!/k! (순열 트리 노드 수): <https://oeis.org/A000522>
- D. E. Knuth, *The Art of Computer Programming* Vol. 4A (Combinatorial Algorithms), §7.2.1.2 "Generating all permutations" (Algorithm L = 사전순 다음 순열, 중복 원소 허용) 및 Vol. 4 Fascicle 5 "Backtrack programming" (§7.2.2) — N-Queens·노드 수 추정.
- S. Russell, P. Norvig, *Artificial Intelligence: A Modern Approach*, Ch. 6 "Constraint Satisfaction Problems" — MRV 휴리스틱, 전방 검사(forward checking).
- L. G. Valiant, "The complexity of computing the permanent", *Theoretical Computer Science* 8 (1979) — 이분 완전 매칭 개수(퍼머넌트)가 #P-완전.
- STMicroelectronics RM0008 (STM32F101/102/103/105/107 Reference Manual), §9 "General-purpose and alternate-function I/Os" — AFIO_MAPR 리맵 비트(USART1/I2C1/SPI1/CAN/TIM3_REMAP, SWJ_CFG). 이 레슨의 핀 표는 기억값이므로 **확인 권장**.
- ARM, *Procedure Call Standard for the Arm Architecture* (AAPCS) — r4–r11 callee-saved (프레임에 push 되는 이유).
- MISRA C:2012 Rule 17.2 (함수는 직접·간접으로 자기를 호출하면 안 된다).
- LeetCode 78 Subsets · 90 Subsets II · 46 Permutations · 47 Permutations II · 31 Next Permutation · 51 N-Queens · 52 N-Queens II · 39 Combination Sum · 40 Combination Sum II · 22 Generate Parentheses · 131 Palindrome Partitioning.
- 관련 레슨: ALGO-B02 (이진 탐색 — meet-in-the-middle 의 반쪽 탐색), ALGO-B09 (투 포인터), ALGO-B11 (재귀 깊이·프레임 실측·반복 변환), ALGO-B12/B13 (비트 트릭 `x & -x`), ALGO-M07/M08 (부분집합 합 개수 DP·배낭), ALGO-M14 (Union-Find), C-A10 (MISRA 스타일).
