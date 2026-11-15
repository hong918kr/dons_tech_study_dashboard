---
id: ALGO-M07
time: 90분
updated: 2026-11-11
---
# ALGO-M07 동적 계획법 1D: 계단·동전 교환·LIS

## 학습 목표

- 어떤 1D DP 문제든 **상태(state) → 점화식(recurrence) → 기저(base) → 계산 순서(order)** 네 줄을 먼저 말하고, 같은 점화식을 하향식 메모(top-down memoization), 상향식 테이블(bottom-up tabulation), 공간 최적화(rolling / ring buffer) 세 가지로 옮긴다.
- 동전 교환의 **경우의 수**에서 "순서 무관(조합)" 과 "순서 중요(순열)" 가 **두 루프의 순서 하나**로 갈린다는 것을 표의 변화로 설명하고 증명한다 ({1,2,3}, 4 → 4 vs 7).
- 경우의 수가 `uint64_t` 를 넘는 지점을 실측(계단 {1,2} 는 n = 93, {1,2,3} 은 n = 74 에서 넘침)하고, 포화 덧셈(saturating add)이 "min(참값, 2^64−1)" 을 정확히 보존한다는 것을 증명한다.
- LIS 를 O(n²) 와 O(n log n) (patience / tails) 로 짜고, tails 불변식을 증명하고, prev 체인으로 **실제 부분수열 하나**를 복원한다. 펌웨어 로그에서 "최소 몇 개를 버리면 단조 증가가 되나" = n − LIS.
- 하향식 메모의 재귀 깊이가 **하위 문제를 부르는 순서**에 따라 n 에서 2까지 달라진다는 것을 실측하고, Cortex-M 태스크 스택 예산으로 어느 쪽을 고를지 판단한다.

## 1. 문제

DP 는 임베디드 코딩 면접에서 "점화식 하나 + 메모리 예산" 을 같이 묻기 좋은 주제다 — 정답 코드보다 "테이블이 몇 바이트, 재귀가 몇 단, 경우의 수가 언제 넘치나" 가 꼬리질문으로 나온다.

세 문제를 같은 틀에 넣는다. 핵심 그림: 상태는 정수 하나(계단 칸, 금액, 배열 위치), 점화식의 화살표는 항상 **작은 상태 → 큰 상태** 를 가리킨다 (DAG). 그래서 작은 것부터 채우면(상향식) 재귀가 필요 없고, 큰 것부터 물으면(하향식) 재귀 + 메모가 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part A 상태 그래프: steps {1,2,3}, 칸 2·5 막힘, n = 7 → f(7) = 8</text>
  <g stroke="currentColor">
    <rect x="30" y="60" width="60" height="44" class="f-ok"/>
    <rect x="115" y="60" width="60" height="44" class="f-ok"/>
    <rect x="200" y="60" width="60" height="44" class="f-warn"/>
    <rect x="285" y="60" width="60" height="44" class="f-ok"/>
    <rect x="370" y="60" width="60" height="44" class="f-ok"/>
    <rect x="455" y="60" width="60" height="44" class="f-warn"/>
    <rect x="540" y="60" width="60" height="44" class="f-ok"/>
    <rect x="625" y="60" width="60" height="44" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="60" y="78">칸 0</text><text x="145" y="78">칸 1</text><text x="230" y="78">칸 2</text><text x="315" y="78">칸 3</text>
    <text x="400" y="78">칸 4</text><text x="485" y="78">칸 5</text><text x="570" y="78">칸 6</text><text x="655" y="78">칸 7</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">
    <text x="60" y="97">f=1</text><text x="145" y="97">f=1</text><text x="230" y="97">막힘 0</text><text x="315" y="97">f=2</text>
    <text x="400" y="97">f=3</text><text x="485" y="97">막힘 0</text><text x="570" y="97">f=5</text><text x="655" y="97">f=8</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M572 106 Q 612 136 650 108" marker-end="url(#m07a)"/>
    <path d="M487 106 Q 570 160 652 108" stroke-dasharray="4 3" marker-end="url(#m07a)"/>
    <path d="M402 106 Q 530 186 655 108" marker-end="url(#m07a)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="140">f(7) = f(6) + f(5) + f(4) = 5 + 0 + 3</text>
    <text x="30" y="160">(점선 = 막힌 칸에서 오는 0)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="210">상태 f(i) = 칸 0 → 칸 i 의 순서 있는 방법 수.  점화식 f(i) = Σ f(i − s)  (s ∈ steps, s ≤ i)</text>
    <text x="30" y="232">기저 f(0) = 1 (빈 경로), 막힌 칸은 0.  화살표가 오른쪽만 가리키므로 i 오름차순이 계산 순서.</text>
    <text x="30" y="254">하향식 = f(7) 부터 물어 내려감 (재귀 + 메모).  상향식 = f(0) 부터 채움.  링 = 최근 3칸만.</text>
    <text x="30" y="276">동전(칸 = 금액, 화살표 = 동전)과 LIS(칸 = 위치, 화살표 = a[j] &lt; a[i])도 같은 모양의 그래프.</text>
  </g>
</svg>
```

### Part A — 계단 오르기 변형 (보폭 집합 + 막힌 칸)

다리 로봇이 계단을 한 번에 `steps` 중 하나(1~7칸, 서로 다름)만큼 오른다. 손상된 칸 `blocked[i] != 0` 에는 발을 디딜 수 없다. 칸 0 → 칸 n 의 **순서 있는** 방법 수를 세는 세 함수를 쓴다 (n ≤ 256).

- `stairs_memo` — 하향식, 정적 메모 배열. 재귀 깊이를 말할 수 있어야 한다.
- `stairs_tab` — 상향식 `dp[0..n]`.
- `stairs_roll` — 8칸 링 버퍼(64 B, n 과 무관). 인덱스는 `i & 7`.
- 경우의 수는 `uint64_t` + 포화 덧셈: 참값이 2^64 − 1 이상이면 `UINT64_MAX`. {1,2} 와 {1,2,3} 에서 처음 넘치는 n 을 찾는다.

검증: n ≤ 10 의 **모든 막힘 패턴**을 합성(composition) 열거 브루트포스와 대조, n ≤ 256 무작위는 128비트 참조와 대조, OEIS A000073(tribonacci) 값, F(93).

### Part B — 동전 교환: 최소 개수와 경우의 수 (DMA 버스트 계획)

DMA 엔진이 허용하는 버스트 크기(서로 다른 양수) `coins[k]` 로 `amount` 바이트를 옮긴다.

1. **최소 트랜잭션 수**: `coin_min_memo` (amount ≤ 2000), `coin_min_tab` (`uint16_t dp`, amount ≤ 10000), `coin_min_ring` (모든 버스트 < 64 면 링 64칸, amount 상한 없음). 불가능하면 -1.
2. **계획 수 — 순서 무관**(어떤 크기를 몇 번 쓰나, 다중집합): `coin_ways_combo_memo` (상태 `(i, a)` 2개), `coin_ways_combo` (1D).
3. **계획 수 — 순서 중요**(버스 위 트랜잭션 순서까지 다른 스케줄): `coin_ways_perm`.

2와 3은 **같은 점화 재료**에 루프 순서만 다르다. 이 함정이 Part B 의 주제다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">루프 순서 함정: coins {1,2,3}, amount 4 — 같은 dp[], 다른 답</text>
  <text x="20" y="50" fill="currentColor">조합 (바깥 = 동전, 안 = 금액 ↑)</text>
  <text x="400" y="50" fill="currentColor">순열 (바깥 = 금액, 안 = 동전)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="80">a        0  1  2  3  4</text>
    <text x="20" y="104">시작     1  0  0  0  0</text>
    <text x="20" y="128">동전 1   1  1  1  1  1</text>
    <text x="20" y="152">동전 2   1  1  2  2  3</text>
    <text x="20" y="176">동전 3   1  1  2  3  4</text>
    <text x="400" y="80">dp[0] = 1</text>
    <text x="400" y="104">dp[1] = dp[0]               = 1</text>
    <text x="400" y="128">dp[2] = dp[1]+dp[0]         = 2</text>
    <text x="400" y="152">dp[3] = dp[2]+dp[1]+dp[0]   = 4</text>
    <text x="400" y="176">dp[4] = dp[3]+dp[2]+dp[1]   = 7</text>
  </g>
  <rect x="15" y="190" width="360" height="48" class="f-ok" stroke="currentColor"/>
  <rect x="395" y="190" width="330" height="48" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="25" y="209">답 4 = {1,1,1,1} {1,1,2} {2,2} {1,3}</text>
    <text x="25" y="229">번호 순서로만 쌓음 → 다중집합 1번씩 (LC 518)</text>
    <text x="405" y="209">답 7: 1+1+2, 1+2+1, 2+1+1 이 따로 (LC 377)</text>
    <text x="405" y="229">= steps {1,2,3} 계단의 f(4) (Part A)</text>
  </g>
  <text x="20" y="268" fill="currentColor" font-size="12">면접 한 줄: 동전이 바깥이면 넣는 순서가 고정돼 조합, 금액이 바깥이면 마지막 동전이 자유라 순열.</text>
</svg>
```

### Part C — LIS: 뒤섞인 시퀀스 번호 로그

무선 링크로 받은 패킷 로그에 시퀀스 번호가 `int32_t` 로 기록돼 있다 (재전송·재정렬·이전 세션 잔재로 뒤섞임). **가장 많이 남기면서 시퀀스 번호가 순증가**가 되게 하려면 무엇을 남기나? 남길 것 = 최장 증가 부분수열(LIS, longest increasing subsequence), 버릴 최소 개수 = n − LIS.

- `lis_memo` — 하향식 O(n²), 길이만.
- `lis_n2` — 상향식 O(n²) + 복원 (`out_idx` 에 인덱스 오름차순, NULL 가능).
- `lis_nlogn` — patience(tails) O(n log n) + `prev[]` 체인으로 복원.
- `lis_push` — 스트리밍 길이 전용: tails **값**만 caller 버퍼(cap 칸)에. O(L) 메모리, 샘플당 O(log L). 버퍼가 모자라면 false.

예: `[100,101,102,105,103,104,106,90,107,108]` → 8개 남김(100..104, 106, 107, 108), 105·90 버림. 검증: 길이 0~7, 값 0~3 의 **모든 배열**(21845개)을 부분집합 브루트포스와 대조하고 복원 결과가 실제 증가 부분수열인지 확인.

## 2. Problem (English)

> **1D dynamic programming, three ways each.** C11, no heap allocation (static arrays sized by `MAX_*` constants or caller buffers).
>
> 1. **Stairs.** `uint64_t stairs_{memo,tab,roll}(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked)` — number of ordered step sequences from 0 to `n` using step sizes from `steps` (distinct, 1..7), never landing on `i` with `blocked[i] != 0` (`blocked` may be `NULL`). `n <= 256`. Counts saturate at `UINT64_MAX`. `roll` must use O(max step) memory.
> 2. **Coin change.** `int32_t coin_min_{memo,tab,ring}(const uint32_t *coins, size_t k, uint32_t amount)` — fewest coins (unlimited supply) summing to `amount`, or `-1`. `uint64_t coin_ways_combo(...)` — number of multisets; `coin_ways_perm(...)` — number of ordered sequences. Saturate at `UINT64_MAX`.
> 3. **LIS.** `size_t lis_nlogn(const int32_t *a, size_t n, size_t *out_idx)` — length of the longest strictly increasing subsequence in O(n log n); if `out_idx != NULL`, write one such subsequence's indices in increasing order. Also an O(n²) version and a streaming length-only `bool lis_push(lis_stream_t *s, int32_t x)`.
>
> Examples: stairs `n=7, steps={1,2,3}, blocked={2,5}` → 8. `coin_min([1,2,5], 11) = 3`, `coin_min([2], 3) = -1`, `coin_min([1,3,4], 6) = 2` (greedy gives 3). `combo([1,2,5], 5) = 4`, `perm([1,2,3], 4) = 7`. `lis([10,9,2,5,3,7,101,18]) = 4` (e.g. 2,3,7,18).
>
> Follow-ups: state the recursion depth of each memoized version; when does each count overflow 64 bits; what changes if order matters.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 13개 함수 본문(메모 재귀 4개는 `*_f` 헬퍼, 래퍼의 메모 초기화·입력 검사는 주어짐). 스텁은 0 / -1 / true 같은 "틀렸지만 안전한" 값을 돌려준다. 처음 실행 (실제 출력, FAIL 줄 생략):

```text
Part A exhaustive: 20470 (steps, n, blocked) cases vs composition brute force, mismatches=2136
Part A random: 3000 cases (n<=256), saturated=1018, mismatches=2579
Part A uint64 limit: steps{1,2} last exact n=92, steps{1,2,3} last exact n=73
Part B exhaustive: 6375 (coin set, amount) cases, mismatches min=5739 combo=5739 perm=3808
Part B random: 400 cases (amount<=2000), impossible=400, mismatches=0
Part C exhaustive: 21845 arrays (n<=7, values 0..3), mismatches len=21844 reconstruct=43688
Part C random: 1500 small (vs brute) mismatches=1400, 60 large n<=600 mismatches=0
10/46 passed
```

(exit 1, FAIL 36줄. 통과한 10개는 "규칙 밖 입력 → 0/-1" 같은 주어진 검사와, 스텁끼리 똑같이 틀려서 "구현끼리 일치" 가 공짜로 성립한 무작위 대조 2개다 — 그래서 그 대조들은 전수 브루트포스 검사와 **짝으로만** 의미가 있다. "Part A uint64 limit" 줄은 테스트 쪽 128비트 참조가 계산한 값이라 스텁과 무관하다.)

## 4. 힌트

<details><summary>힌트 1 — 코드 전에 네 줄을 쓴다</summary>

상태: "f(i) = ...인 것의 개수/최솟값/길이". 점화식: "마지막 한 수(마지막 보폭, 마지막 동전, 바로 앞 원소)로 경우를 나눈다". 기저: f(0). 순서: 화살표가 작은 쪽을 가리키면 오름차순. 이 네 줄이 정해지면 세 구현은 기계적으로 나온다.

</details>

<details><summary>힌트 2 — 링 버퍼는 "최대 몇 칸 뒤까지 보나" 로 크기를 정한다</summary>

계단은 최대 7칸 뒤, 동전 링은 최대 63칸 뒤를 본다. 링 크기를 그보다 큰 2의 거듭제곱(8, 64)으로 잡으면 `i & (R-1)` 로 인덱싱되고, `i` 칸에 쓰는 순간 덮어쓰는 칸은 `i - R` — 아무도 다시 안 읽는다.

</details>

<details><summary>힌트 3 — 조합 수 메모는 상태가 하나 더 필요하다</summary>

"순서 무관" 을 재귀로 세려면 동전을 **정해진 순서로만** 써야 한다: h(i, a) = coins[i..] 만으로 a 를 만드는 수. "동전 i 를 그만 쓴다 → h(i+1, a)" 와 "하나 더 쓴다 → h(i, a − c_i)" 로 나뉜다. 상향식으로 바꾸면 i 차원이 바깥 루프가 되고, 행 하나만 덮어쓰며 굴리면 1D 가 된다.

</details>

<details><summary>힌트 4 — tails 는 "길이별 가장 작은 끝값"</summary>

`tails[m]` = 지금까지 본 접두부에서 길이 m+1 인 증가 부분수열들의 끝값 중 최소. 새 값 x 가 오면 `tails` 에서 "x 이상인 첫 칸"(lower_bound, ALGO-B02)을 x 로 바꾸거나 끝에 붙인다. 복원을 원하면 값 대신 **인덱스**를 tails 에 두고, 놓을 때 `prev[i] = tails[pos-1]` 을 기록한다.

</details>

<details><summary>힌트 5 — 포화 덧셈과 센티널</summary>

메모 "아직 모름" 표시를 답이 될 수 있는 값과 겹치게 하지 않는다: 계단은 `bool s_have[]`, 동전 최소는 -2, LIS 는 0 (참값 ≥ 1). 경우의 수는 `UINT64_MAX` 가 "넘침" 이라 센티널로 못 쓴다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 순서가 다르면 다른 방법인가? 보폭/동전 값은 서로 다른가, 0 이 있나? 불가능하면 무엇을 반환? 개수가 64비트를 넘으면(모듈러 1e9+7 인가)? LIS 는 순증가인가 비감소인가? 부분수열 자체도 필요한가? |
| 2. 무식한 방법 | 계단 = 합성 2^(n−1) 개 열거, 동전 = 다중집합/수열 DFS, LIS = 부분집합 2^n. 모두 지수 시간. 테스트 기준(oracle)으로 남긴다 |
| 3. 더 나은 생각 | 같은 하위 문제가 겹친다(overlapping subproblems) + 최적해의 부분이 최적(optimal substructure) → 상태 / 점화식 / 기저 / 순서 네 줄 |
| 4. 코드 | 상향식 먼저(스택 0), 그다음 공간 최적화. 하향식은 "필요한 상태만 계산" 이 이득일 때 |
| 5. 소리 내어 말할 테스트 | n = 0, 출발·도착이 막힘, 짝수 보폭만 + 홀수 n, 동전 {2} 로 3, amount 0, 탐욕 반례 {1,3,4}·6, 전부 같은 값 LIS = 1, INT32_MIN/MAX |
| 6. 복잡도 | 시간·메모리·**재귀 깊이**·넘침 한계를 각각 (아래 표) |

### Part A — 계단: 상태, 점화식, 세 구현

**정확성(합의 법칙).** 칸 0 → 칸 i (i ≥ 1, 안 막힘) 경로 하나를 **마지막 보폭 s** 로 분류하면, 마지막 보폭을 뗀 나머지는 칸 0 → 칸 i − s 의 경로이고 반대로 그런 경로에 s 를 붙이면 i 로 가는 경로가 된다 (막힘 조건은 착지 칸 i 하나만 더 보면 된다). 서로 다른 s 의 집합은 겹치지 않으니 f(i) = Σ_s f(i − s). 기저 f(0) = 1 은 "빈 경로 1개" 이고, 칸 0 이 막혔으면 0. 화살표 i − s → i 는 항상 오른쪽이라 그래프가 DAG 이고, i 오름차순이 위상 순서다.

손 계산 (위 그림, steps {1,2,3}, 칸 2·5 막힘):

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 막힘 | | | ✕ | | | ✕ | | |
| f(i−1)+f(i−2)+f(i−3) | 기저 | 1 | — | 0+1+1 | 2+0+1 | — | 0+3+2 | 5+0+3 |
| f(i) | 1 | 1 | **0** | 2 | 3 | **0** | 5 | **8** |

막힘이 없으면 f(7) = 44 (tribonacci, OEIS A000073 의 a(9)). 막힌 칸 두 개가 경로의 82% 를 지운다.

**세 구현의 차이는 "누가 순서를 정하나" 뿐이다.**

- `stairs_tab`: 루프가 i = 1..n 순서를 정한다. `dp[i - s]` 는 이미 채워져 있다.
- `stairs_memo`: `stairs_f(n)` 이 필요한 것만 물어 내려간다. 메모가 없으면 같은 f(i) 를 지수 번 다시 계산한다 (트리 크기 = 답의 크기 이상). 메모가 있으면 각 i 는 한 번만 계산 → O(n·k).
- `stairs_roll`: `dp[i]` 는 `dp[i−7..i−1]` 만 본다. 링 8칸에서 i 를 쓰면 `i − 8` 칸이 덮이고, 1 ≤ s ≤ 7 이면 `(i − s) & 7 ≠ i & 7` 이라 읽을 칸이 덮이지 않는다. 메모리 64 B 고정.

**재귀 깊이는 부르는 순서가 정한다** (demo_depth.c 실측, arm64 -O2, 프레임 약 64 B):

| 메모 함수 | 하위 문제 부르는 순서 | 최대 깊이 | 스택 |
|---|---|---|---|
| 계단 n = 256 | steps {1,2,3} (f(i−1) 먼저) | 257 | 16528 B |
| 계단 n = 256 | steps {3,2,1} (f(i−3) 먼저) | 87 | 5648 B |
| 동전 최소 amount 2000 | {1,5,10,25} | 2001 | 128144 B |
| 동전 최소 amount 2000 | {25,10,5,1} | 87 | 5648 B |
| 조합 h(i,a) amount 2000 | {1,5,10,25} / {25,10,5,1} | 2001 / 2004 | 128 KB |
| LIS n = 4000 (정렬된 입력) | 바깥 오름 · 안 오름 | 2 | 224 B |
| LIS n = 4000 (정렬된 입력) | 바깥 내림 · 안 내림 | 4000 | 320064 B |

f(i−1) 을 먼저 부르면 사슬 i → i−1 → … → 0 이 생겨 깊이 n+1. 큰 보폭을 먼저 부르면 사슬이 n/3 으로 짧아지고 나머지 호출은 메모 적중으로 바로 돌아온다. LIS 에서 안쪽 j 를 오름차순으로 돌면 L(j) 를 부를 때 그보다 작은 것들이 이미 메모돼 있어 깊이가 상수다 — 하향식 코드가 사실상 상향식 순서로 돈 것이다. 조합 h(i, a) 는 어떤 순서로도 "동전 1 하나 더" 사슬(a → a−1 → …)이 생겨 깊이 ≈ amount 를 못 피한다.

Cortex-M 태스크 스택이 수 KB 라는 걸 생각하면 128 KB 는 그냥 HardFault 다. 실제로 64 KiB pthread 스택(태스크 스택 흉내)에서 동전 최소 메모를 돌리면 (fork 한 자식에서 실행):

```text
    stack   64 KiB, amount    500: ok, answer=100
    stack   64 KiB, amount   2000: child killed by signal 10 (Bus error: 10)
    stack 1024 KiB, amount  10000: ok, answer=2000
    stack 1024 KiB, amount  20000: child killed by signal 10 (Bus error: 10)
```

macOS 는 스레드 스택 가드 페이지를 건드리면 SIGBUS 로 죽였다. Cortex-M 에는 가드 페이지가 기본으로 없다 — MPU 로 스택 아래에 금지 영역을 두지 않으면 옆 메모리를 조용히 덮고, FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` 는 문맥 전환 시점에만 검사한다 (ALGO-B11 참고). 그래서 펌웨어에서는 상향식이 기본값이다.

**넘침과 포화 덧셈.** 경우의 수는 지수적으로 커진다: {1,2} 는 f(n) = F(n+1) (피보나치), {1,2,3} 은 tribonacci. `sat_add_u64(a, b) = min(a + b, M)` (M = 2^64 − 1)에 대해 보조정리: x, y ≥ 0 이고 x' = min(x, M), y' = min(y, M) 이면 `sat(x', y') = min(x + y, M)`. 증명: x ≥ M 또는 y ≥ M 이면 x' + y' ≥ M 이고 x + y ≥ M 이라 양변 모두 M. 아니면 x' = x, y' = y 라 정의 그대로. 귀납으로 DP 의 모든 칸은 min(참값, M) 이다 — 포화는 "틀린 값" 이 아니라 **정확한 하한 정보**다 (뺄셈·최솟값이 섞이면 이 성질이 깨지므로 개수 합에서만 쓴다). 한계 실측 (solution.c, 128비트 참조):

```text
Part A uint64 limit: steps{1,2} last exact n=92, steps{1,2,3} last exact n=73
```

F(93) = 12200160415121876738 이 uint64 에 들어가는 마지막 피보나치 수이고 테스트가 그 값을 직접 확인한다. 면접에서 "답을 1e9+7 로 나눈 나머지" 가 붙는 이유가 이것이다.

### Part B — 동전 교환

**최소 개수: 최적 부분 구조.** amount a 의 최적 다중집합 S 에서 동전 c 하나를 빼면 S∖{c} 는 a − c 를 만든다. 그게 a − c 의 최적보다 많다면 a − c 의 최적해에 c 를 붙여 S 보다 적은 해를 만들 수 있으니 모순(잘라 붙이기, cut-and-paste). 그래서 g(a) = 1 + min_{c ≤ a, g(a−c) 존재} g(a − c), g(0) = 0. 불가능은 `COIN_INF` / -1 로 전파하고 `INF + 1` 을 하지 않는다 (7. 실수 4).

손 계산 {1,3,4}, a = 6:

| a | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| 후보 (−1, −3, −4) | 기저 | g0+1 | g1+1 | g2+1, g0+1 | g3+1, g1+1, g0+1 | g4+1, g2+1, g1+1 | g5+1, **g3+1**, g2+1 |
| g(a) | 0 | 1 | 2 | 1 | 1 | 2 | **2** (3+3) |

탐욕(큰 것부터)은 4+1+1 = 3개. 탐욕이 항상 맞는 동전 체계를 정준(canonical) 체계라 하고 미국 동전 {1,5,10,25,50} 이 그 예다 — 테스트는 이 사실을 큰 금액 오라클로 쓴다 (`coin_min_ring(us5, 999999) == greedy`).

**링 버전.** g(a) 는 g(a − c) (c < 64) 만 보므로 링 64칸이면 된다. amount 가 수백만이어도 256 B. 대가: **어떤 동전을 썼는지 복원할 수 없다** (지나간 칸이 지워짐) — 복원하려면 `choice[a]` 1 B × amount 가 필요하다 (연습 5).

**경우의 수: 왜 루프 순서가 의미를 바꾸나.** D_j[a] = coins[0..j] 로 a 를 만드는 다중집합 수. 동전 j 를 0개 쓰는 경우와 1개 이상 쓰는 경우(하나를 빼면 여전히 coins[0..j] 다중집합)로 나누면

D_j[a] = D_{j−1}[a] + D_j[a − c_j].

1D 루프 `for j { for a = c_j..amount: dp[a] += dp[a − c_j] }` 에서 동전 j 를 처리하는 중 a 에 도달했을 때, `dp[a]` 는 아직 이전 행 D_{j−1}[a] 이고 `dp[a − c_j]` 는 이미 이번 행 D_j[a − c_j] 로 갱신돼 있다 (a 오름차순이니까). 정확히 위 식이다. 2D 표 k × (amount+1) 의 행을 한 줄에 덮어쓴 것이 1D 버전이고, 이게 이 문제의 공간 최적화다. (a 를 **내림차순**으로 돌면 `dp[a − c_j]` 가 이전 행이라 각 동전을 최대 1번 쓰는 0/1 배낭이 된다 — ALGO-M08 의 역방향 루프.)

순열 수 P[a] 는 **마지막 동전**으로 나눈다: P[a] = Σ_c P[a − c]. 금액이 바깥 루프여야 `P[a − c]` 가 "모든 동전을 다 쓴 완성값" 이다. 계단과 같은 식이라 테스트가 `coin_ways_perm({1,2,3}, n) == stairs_tab({1,2,3}, n)` 을 n = 0..256 에서 확인한다. 조합에서 동전 바깥 루프가 하는 일은 "동전을 번호 순서로만 쌓는다" 는 정규형(canonical form)을 강제하는 것이고, 순열은 그 강제를 풀었기에 같은 다중집합이 순서별로 따로 세진다.

하향식 조합 `combo_f(i, a)` 는 상태가 2개라 메모가 16 × 2001 × 8 B = 256 KB (+ 플래그 32 KB) 다. 같은 답을 1D 상향식은 8 B × 2001 = 16 KB 로 낸다. 면접에서 "하향식이 더 자연스러운데 메모리가 왜 이렇게 크냐" 가 바로 이 지점이다.

공개 값으로도 확인한다: LeetCode 518 예제(5, [1,2,5] → 4), LeetCode 377 예제([1,2,3], 4 → 7), SICP 1.2.2 의 100센트 {50,25,10,5,1} → 292, Project Euler 31 의 200p 영국 동전 8종 → 73682, OEIS A001399 ({1,2,3} 조합 수 = round((n+3)²/12)) 를 n = 0..10000 에서.

### Part C — LIS

**O(n²) 점화식.** L(i) = a[i] 로 **끝나는** 가장 긴 증가 부분수열의 길이. 그런 부분수열에서 a[i] 바로 앞 원소를 a[j] (j < i, a[j] < a[i]) 라 하면 앞부분은 a[j] 로 끝나는 증가 부분수열이고 최장이어야 한다 → L(i) = 1 + max L(j), 없으면 1. 답은 max_i L(i) (끝 위치를 모르니까 — "상태에 '끝난다' 를 넣는 것" 이 이 문제의 요령). 복원은 max 를 준 j 를 `prev[i]` 에 적고 끝에서 거꾸로 걷는다.

**O(n log n) 불변식 (tails).** 접두부 a[0..i) 를 처리한 뒤:

- (I1) `T[l]` = 길이 l+1 인 증가 부분수열들의 끝값 중 최소 (그런 게 있는 l < len 에 대해), len = 접두부의 LIS 길이.
- (I2) T 는 순증가: 길이 l+2 짜리가 T[l+1] 로 끝나면 그 앞 l+1 개는 T[l+1] 보다 작은 값으로 끝나므로 T[l] < T[l+1].

새 값 x 에 대해 p = "T[p] ≥ x 인 첫 p" (lower_bound). (1) T[p−1] < x 이므로 x 는 길이 p 짜리 뒤에 붙어 길이 p+1 을 만든다 → T[p] 는 min(T[p], x) = x (p = len 이면 len 증가). (2) l < p: 길이 l+1 의 끝을 x 로 하려면 길이 l 짜리가 x 미만으로 끝나야 하는데 T[l] < x 라 이미 T[l] 이 더 작다 → 변화 없음. (3) l > p: x 로 끝나는 길이 l+1 이 있으려면 T[l−1] < x 여야 하는데 l−1 ≥ p 라 T[l−1] ≥ T[p] ≥ x → 불가능. 그래서 바뀌는 칸은 T[p] 하나이고 불변식이 유지된다. 비교는 원소당 ⌈log₂(len+1)⌉ 회 이하 → O(n log L).

`prev[i] = tail[p−1]` (인덱스) 의 체인이 올바른 이유: 그 순간 a[tail[p−1]] < a[i] 이고 tail[p−1] < i 이며, tail[p−1] 자신도 같은 방식으로 길이 p 짜리의 끝이었다. 귀납으로 체인 길이 p+1, 값·인덱스 모두 순증가.

손 계산 `[10, 9, 2, 5, 3, 7, 101, 18]`:

| i | a[i] | lower_bound p | 동작 | tails (값) | prev[i] | L(i) (O(n²)) |
|---|---|---|---|---|---|---|
| 0 | 10 | 0 | 연장 | 10 | — | 1 |
| 1 | 9 | 0 | 교체 | 9 | — | 1 |
| 2 | 2 | 0 | 교체 | 2 | — | 1 |
| 3 | 5 | 1 | 연장 | 2 5 | 2 | 2 |
| 4 | 3 | 1 | 교체 | 2 3 | 2 | 2 |
| 5 | 7 | 2 | 연장 | 2 3 7 | 4 | 3 |
| 6 | 101 | 3 | 연장 | 2 3 7 101 | 5 | 4 |
| 7 | 18 | 3 | 교체 | 2 3 7 18 | 5 | 4 |

len = 4. 복원: tail[3] = 7 → prev 5 → 4 → 2 → 인덱스 (2, 4, 5, 7) = 값 2, 3, 7, 18.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">tails 와 prev 체인: [10, 9, 2, 5, 3, 7, 101, 18]</text>
  <g fill="currentColor" font-size="12"><text x="20" y="62">길이 1</text><text x="20" y="112">길이 2</text><text x="20" y="162">길이 3</text><text x="20" y="212">길이 4</text></g>
  <g stroke="currentColor">
    <rect x="90" y="44" width="50" height="28" class="f-mut"/><rect x="150" y="44" width="50" height="28" class="f-mut"/><rect x="210" y="44" width="50" height="28" class="f-ok"/>
    <rect x="270" y="94" width="50" height="28" class="f-mut"/><rect x="330" y="94" width="50" height="28" class="f-ok"/>
    <rect x="390" y="144" width="50" height="28" class="f-ok"/>
    <rect x="450" y="194" width="50" height="28" class="f-mut"/><rect x="510" y="194" width="50" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="115" y="63">10</text><text x="175" y="63">9</text><text x="235" y="63">2</text>
    <text x="295" y="113">5</text><text x="355" y="113">3</text>
    <text x="415" y="163">7</text>
    <text x="475" y="213">101</text><text x="535" y="213">18</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M510 200 L440 166" marker-end="url(#m07b)"/>
    <path d="M390 150 L380 124" marker-end="url(#m07b)"/>
    <path d="M330 100 L260 72" marker-end="url(#m07b)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="580" y="63">tails[0] = 2</text><text x="580" y="113">tails[1] = 3</text>
    <text x="580" y="163">tails[2] = 7</text><text x="580" y="213">tails[3] = 18</text>
  </g>
  <text x="20" y="250" fill="currentColor" font-size="12">같은 줄의 오른쪽 칸이 왼쪽 칸을 교체 (회색 = 교체됨). 화살표 = prev. 18 → 7 → 3 → 2 가 복원된 LIS.</text>
</svg>
```

**주의: tails 배열 자체는 LIS 가 아니다.** 이 예에서는 우연히 `[2,3,7,18]` 이지만 `[3, 4, 1]` 이면 tails = `[1, 4]` 이고 1 은 4 뒤에 있다. 길이만 맞다 (Q4).

**스트리밍판 `lis_push`.** tails 값만 들고 샘플당 lower_bound 한 번. 버퍼 cap 을 넘는 연장이 오면 false — 길이 상한이 정해진 진단 카운터로 쓸 수 있다. 무작위 순열에서 LIS 는 약 2√n 이라 (아래 demo) 10^6 샘플도 tails 2000칸 정도면 된다 — 단, 이건 **무작위 입력의 기댓값**이고 정렬된 입력이면 L = n 이다. 최악을 보장해야 하면 cap = n.

### 복잡도 요약

| 함수 | 시간 | 추가 메모리 | 재귀 깊이 (최악) |
|---|---|---|---|
| stairs_memo / tab / roll | O(n·k) | 8(n+1)+(n+1) B / 8(n+1) B / 64 B | n+1 / 0 / 0 |
| coin_min_memo / tab / ring | O(A·k) | 4(A+1) B / 2(A+1) B / 256 B | A / 0 / 0 |
| coin_ways_combo_memo / combo / perm | O(A·k) | 9·k·(A+1) B / 8(A+1) B / 8(A+1) B | A+k / 0 / 0 |
| lis_memo / lis_n2 | O(n²) | 8n / 16n B | n (solution 의 순서 — 바깥 내림·안 오름 — 에서는 3) / 0 |
| lis_nlogn | O(n log L) | 16n B (tail + prev) | 0 |
| lis_push (스트리밍) | O(log L) / 샘플 | 4L B (caller) | 0 |

(A = amount, k = 보폭/동전 수, L = LIS 길이, size_t = 8 B 인 이 맥 기준. Cortex-M 에서는 size_t 4 B 라 절반. 비교 모델에서 LIS 길이 계산은 Ω(n log n) 비교가 필요하다는 하한이 Fredman(1975)에 있다 — 확인 권장.)

**측정 (demo_bench.c, 이 맥 -O2, warm-up + 5회 중앙값).** 측정 중 다른 작업 14개가 병렬로 돌아 load average ≈ 45 였다 — 절대값은 흔들리고 비율의 자릿수만 의미 있다.

```text
1) LIS time, random permutation (median of 5, ms)
       n |     O(n^2) |   O(nlogn) |    ratio | LIS
    1000 |      0.427 |     0.0170 |      25x | 58
    2000 |      1.056 |     0.0380 |      28x | 84
    4000 |      6.447 |     0.0760 |      85x | 121
    8000 |     38.319 |     0.2620 |     146x | 178
   16000 |    305.298 |     0.3630 |     841x | 257

2) LIS length of a random permutation vs 2*sqrt(n) (O(n log n) only, 5 seeds each)
       n | mean LIS | 2sqrt(n) |  ratio | time/run ms
    1000 |     58.2 |     63.2 |  0.920 | 0.04
   10000 |    192.0 |    200.0 |  0.960 | 0.41
  100000 |    623.6 |    632.5 |  0.986 | 12.98
 1000000 |   1982.2 |   2000.0 |  0.991 | 65.02

3) min coins, amount=2000, coins {1,7,13,29,31,47} (median of 5, us per call)
  memo  : answer=44  26.08 us  (table 8004 B + recursion)
  tab   : answer=44  12.11 us  (table 4002 B)
  ring  : answer=44  8.32 us  (ring 256 B)
  memo/tab = 2.15x

4) first amount whose number of ways exceeds UINT64_MAX (exact 128-bit count)
  perm  {1,2}                 : 93
  perm  {1,2,3}               : 74
  perm  {1,5,10,25,50,100}    : 148
  combo {1,5,10,25,50,100}    : 424715
  combo {1,2,3}               : -1 (closed form (n+3)^2/12 -> about 1.49e10)
```

읽는 법: (1) n 이 2배면 O(n²) 는 4배 이상(캐시·부하 영향으로 더 튐), O(n log n) 은 약 2배 — 비율이 n 에 비례해 벌어진다. (2) LIS/2√n 이 1 로 다가간다 (Vershik–Kerov, Logan–Shepp 1977). 차이는 n^{1/6} 차수의 보정항으로 알려져 있다: E[L_n] ≈ 2√n − 1.77·n^{1/6} (Baik–Deift–Johansson 1999, Tracy–Widom 분포의 평균 — 확인 권장). n = 10^6 에서 2000 − 17.7 = 1982.3 로 측정값 1982.2 와 맞는다. (3) 같은 O(A·k) 라도 메모는 호출·분기 비용 때문에 상향식의 약 2배, 링은 작은 배열이 L1 에 머물러 더 빠르다. (4) 순열 수는 지수 증가라 금액 100 남짓에서 넘치지만, 조합 수는 다항식(동전 6종이면 대략 A^5 차수)이라 42만까지 버틴다. {1,2,3} 조합 수는 (n+3)²/12 > 2^64 이려면 n ≈ 1.49 × 10^10 — 사실상 안 넘친다. 그래서 "어떤 개수를 세느냐" 가 타입 선택을 바꾼다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 46/46 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 46/46, 보고 없음
```

실제 출력:

```text
Part A exhaustive: 20470 (steps, n, blocked) cases vs composition brute force, mismatches=0
Part A random: 3000 cases (n<=256), saturated=1018, mismatches=0
Part A uint64 limit: steps{1,2} last exact n=92, steps{1,2,3} last exact n=73
Part B exhaustive: 6375 (coin set, amount) cases, mismatches min=0 combo=0 perm=0
Part B random: 400 cases (amount<=2000), impossible=111, mismatches=0
Part C exhaustive: 21845 arrays (n<=7, values 0..3), mismatches len=0 reconstruct=0
Part C random: 1500 small (vs brute) mismatches=0, 60 large n<=600 mismatches=0
46/46 passed
```

(-O2 빌드 전체 약 0.25초, ASan/UBSan 빌드 약 0.5초. 20470 = 보폭 집합 5개 × Σ_{n=0..10} 2^(n+1) 막힘 패턴, 6375 = {1..8} 의 비지 않은 부분집합 255개 × 금액 0..24, 21845 = Σ_{n=0..7} 4^n. 무작위 계단 3000건 중 1018건은 포화 영역 — 포화 경로도 128비트 참조와 일치.)

## 7. 자주 하는 실수

**1. 조합을 세려다 순열을 센다 (루프 순서).** `for a { for coin }` 로 LeetCode 518 을 풀면 {1,2,5}, 5 에서 4 대신 9 가 나온다 (1+2+2, 2+1+2, 2+2+1 이 따로 셈). 고침: 동전을 바깥 루프로. 반대로 377 을 동전 바깥으로 풀면 7 대신 4. 테스트 한 줄로 바로 잡힌다: `combo({1,2,3},4) == 4`, `perm == 7`.

**2. 부호 없는 인덱스에서 `i - s >= 0`.** `uint32_t i` 에서 `i - s >= 0` 은 항상 참이고 `i < s` 면 `i - s` 가 2^32 근처로 감싸져(wrap) 배열 밖을 읽는다. 고침: 비교를 먼저 `s <= i`, 그다음에 뺄셈. 이 레슨의 모든 루프가 `if (steps[j] <= i)` 를 먼저 쓰는 이유다.

**3. 메모 "모름" 센티널이 답과 겹친다.** 계단 메모를 0 = 모름 으로 두면 막힌 칸·도달 불가 칸의 참값 0 이 "모름" 으로 보여 매번 다시 계산한다 — 답은 맞지만 도달 불가 구간이 크면(예: 칸 0 이 막힘 → 모든 f = 0) 메모가 전혀 듣지 않아 지수 시간으로 돌아간다. 경우의 수에 `UINT64_MAX` 를 센티널로 쓰면 포화값과 충돌한다. 고침: 별도 `bool have[]` 이나 답이 될 수 없는 값(-2, LIS 의 0).

**4. 불가능 표시에 +1.** `dp[a] = min(dp[a], dp[a-c] + 1)` 에 INF = `INT_MAX` 를 쓰면 `INT_MAX + 1` 은 부호 있는 오버플로(UB, C11 6.5p5)다. `uint16_t` INF = 65535 는 식 안에서는 int 로 승격돼 65536 이라 비교가 우연히 맞지만, 후보를 `uint16_t cand = dp[a-c] + 1;` 처럼 먼저 저장하면 0 이 되어 "0개로 가능" 이라는 틀린 답이 나온다. 고침: `dp[a-c] != INF` 를 먼저 확인 (solution 의 `coin_min_tab`).

**5. 링 크기를 "보폭 최댓값" 으로 잡는다.** 보폭 ≤ 7 인데 링이 7칸이면 `(i - 7) % 7 == i % 7` — 읽을 칸을 방금 덮어쓴다. 링은 **최대 거리 + 1** 칸 이상. 그리고 `% R` 은 R 이 2의 거듭제곱이 아니면 나눗셈이 된다: demo_target.c 를 Cortex-M0 로 컴파일하면 링 3칸판(`% 3`)은 함수 안에 `bl __aeabi_uidiv` / `bl __aeabi_uidivmod` 호출이 19개(컴파일러가 루프를 펼친 결과) 생기고, 링 4칸판(`& 3`)은 0개다. Cortex-M4 는 ÷3 을 `movw r6, #43691` / `movt r6, #43690` (= 0xAAAAAAAB) 와 `umull` 로 바꿨다 (ALGO-M15 의 곱셈 역원). 1칸(8 B)을 더 쓰고 나눗셈을 없애는 게 싸다.

**6. LIS 에서 lower_bound 와 upper_bound 를 바꿔 쓴다.** 순증가는 "x 이상인 첫 칸"(lower_bound), 비감소는 "x 초과인 첫 칸"(upper_bound). `[7,7,7]` 에 upper_bound 를 쓰면 순증가 LIS 가 3 으로 나온다 (정답 1). 연습 3 이 비감소판이다.

**7. tails 배열을 LIS 로 출력한다.** 위 `[3,4,1]` 반례. 부분수열이 필요하면 tails 에 인덱스를 두고 prev 체인으로 복원.

**8. 메모 초기화를 잊거나 너무 많이 한다.** 정적 메모를 호출마다 지우지 않으면 이전 입력의 값이 남는다 (테스트가 수천 번 다른 입력으로 부르니 바로 걸린다). 반대로 `memset(전체 MAX)` 를 매번 하면 작은 입력에서도 O(MAX) 비용 — 래퍼는 `amount + 1` 칸만 지운다.

## 8. 면접 Q&A

<details><summary>Q1. 하향식 메모와 상향식 테이블, 펌웨어에서는 어느 쪽을 쓰죠?</summary>

기본은 상향식: 스택 0, 루프라 WCET(최악 실행 시간) 분석이 쉽고, 실측으로도 메모가 상향식의 약 2.15배 느렸다 (호출·메모 확인 비용). 하향식이 이기는 경우는 **필요한 상태가 전체의 일부**일 때다 — 예: 동전이 {100, 250} 이고 amount 가 10^6 이면 방문하는 금액은 50 의 배수뿐이다 (약 2만 개 vs 상향식 100만 칸). 그래도 재귀 깊이가 amount 까지 갈 수 있어서 Cortex-M 에서는 명시적 스택(배열)으로 바꾸거나 깊이 상한을 걸어야 한다. 꼬리: "메모 재귀 깊이를 줄이는 방법?" → 하위 문제를 큰 보폭부터 부르기(실측 257 → 87), 혹은 상향식으로 몇 칸 미리 채워 두기(warm start). 근본 해결은 상향식.

</details>

<details><summary>Q2. 경우의 수가 64비트를 넘으면요?</summary>

먼저 언제 넘는지 말한다: 계단 {1,2} 는 n = 93, {1,2,3} 은 n = 74, 순열 동전 {1,5,10,25,50,100} 은 148 에서 넘었고 조합은 같은 동전으로 424715 까지 버텼다 (128비트 정확 계산으로 실측). 대처: (1) 문제가 허락하면 모듈러(1e9+7) — 덧셈만 있으니 `(a + b) % M` 이 안전하고 a, b < M < 2^31 이면 32비트로 충분, (2) 포화 덧셈 — "충분히 많다" 만 알면 될 때. 합만 있는 DP 에서 min(참값, MAX) 이 정확히 보존된다 (5. 풀이의 보조정리), (3) `unsigned __int128` (컴파일러 확장, Cortex-M 에서는 소프트웨어 연산) 또는 큰 수. 꼬리: "포화값으로 최솟값/뺄셈을 하면?" → 보존 성질이 깨진다. 32비트 코어에서 uint64 덧셈은 `adds` + `adcs` 두 명령이었다 (Cortex-M4 asm 에서 `sat_add` 는 8 명령, 분기 없음).

</details>

<details><summary>Q3. 동전 교환에 탐욕을 쓰면 안 되나요? 언제 맞죠?</summary>

정준(canonical) 체계에서만 맞는다. {1,3,4}, 6 에서 탐욕은 4+1+1 = 3, 최적은 3+3 = 2. 미국 동전 {1,5,10,25,50} 은 정준이라 테스트가 금액 999999 까지 탐욕과 DP 가 같은지 확인한다. 어떤 체계가 정준인지 판정하는 다항 시간 알고리즘(Pearson, 2005)과 "반례가 있다면 작은 금액 범위 안에 있다" 는 상한(Kozen–Zaks, 1994)이 알려져 있다 — 정확한 범위는 확인 권장. 실무에서는 버스트 크기 집합이 고정이면 DP 를 빌드 시간에 돌려 탐욕과 비교해 두면 된다. 꼬리: "DMA 버스트 {1,4,8,16} 은?" → 각 크기가 다음 크기를 나누는 체계(1 | 4 | 8 | 16)는 탐욕이 최적이다. 이 레슨 작업 중 0..10000 을 DP 와 탐욕으로 비교해 불일치 0 을 확인했다.

</details>

<details><summary>Q4. O(n log n) LIS 에서 tails 배열이 답인가요? 실제 부분수열은 어떻게?</summary>

아니다. tails 는 "길이별 최소 끝값" 이라 서로 다른 부분수열의 끝값이 섞여 있다: `[3,4,1]` → tails `[1,4]`, 1 은 4 뒤. 길이 len 만 정확하다. 복원은 tails 에 값 대신 인덱스를 두고, 원소 i 를 위치 p 에 놓을 때 `prev[i] = tails[p−1]` 을 기록, 마지막에 `tails[len−1]` 에서 prev 를 따라간다. 추가 메모리 n 개 인덱스. 꼬리: "사전순으로 가장 작은 LIS?" → 그건 다른 문제(뒤에서부터 계산 등)이고 prev 체인이 주는 것은 "하나의" LIS 다. LeetCode 300 Longest Increasing Subsequence, 354 Russian Doll Envelopes(2D 로 정렬 후 LIS) 가 이 패턴.

</details>

<details><summary>Q5. 시퀀스 번호가 uint16 이고 wrap 되면 LIS 를 어떻게 쓰죠?</summary>

wrap 된 번호에는 전순서(total order)가 없다 — 직렬 번호 산술(serial number arithmetic, RFC 1982)의 "a 가 b 보다 앞" 은 차이가 2^15 미만일 때만 정의되고 추이성이 없다. LIS 는 전순서가 필요하니 먼저 **펼친다(unwrap)**: 직전 값과의 차 `(int16_t)(cur − prev)` 를 누적해 int32/int64 로 만든 뒤 LIS. 차이를 int16 으로 바꾸는 변환은 구현 정의(C11 6.3.1.3p3)라 `cur − prev` 를 uint16 으로 받고 0x8000 이상이면 65536 을 빼는 식으로 쓰면 이식성이 있다. 재정렬 폭이 2^15 보다 크면 펼치기 자체가 모호해진다 — 그건 프로토콜 설계 문제.

</details>

<details><summary>Q6. 재귀 깊이가 n 이 되는 메모를 Cortex-M 에서 쓰면 어떻게 되나요?</summary>

이 맥 arm64 -O2 에서 프레임이 약 64 B 였다 (amount 2000 → 128144 B). 64 KiB 스레드 스택에서 amount 2000 은 SIGBUS 로 죽었고 500 은 살았다 (demo_depth.c 실측). Cortex-M 은 프레임이 더 작을 수 있지만(레지스터 4 B) 태스크 스택도 1~4 KB 수준이라 결과는 같다. 차이는 **죽는 방식**: MPU 가드가 없으면 스택이 힙/전역 변수를 덮고 한참 뒤 엉뚱한 곳에서 HardFault 가 난다. 대처: 상향식, 또는 깊이 상한 + 정적 분석(`-fstack-usage`, 재귀는 도구가 상한을 못 낸다), MISRA C 규칙 17.2 (재귀 금지). 꼬리: "LIS 메모는 깊이가 2 였는데?" → 안쪽 루프를 오름차순으로 돌면 하위 문제가 이미 메모돼 있어서다. 같은 코드가 순서 하나로 깊이 4000(320 KB)이 됐다.

</details>

<details><summary>Q7. 공간 최적화를 하면 무엇을 잃죠?</summary>

복원(reconstruction)과 임의 질의다. 링 64칸 `coin_min_ring` 은 "몇 개" 만 알고 "어떤 동전" 은 모른다 — 지나간 칸이 지워졌으니까. 복원하려면 `choice[a]` (1 B × amount, 연습 5) 또는 전체 dp 를 다시 걷는 방법이 필요하다. 계단 링도 f(n) 하나만 남는다 — f(k) 를 여러 k 에 대해 묻는 API 라면 테이블이 낫다. 2D DP 에서는 "공간 최적화 + 복원" 을 둘 다 얻는 분할 정복(Hirschberg)이 있다 (LCS/편집 거리, ALGO-M08). 펌웨어 판단 기준: 결과 하나만 필요하고 입력이 커질 수 있으면 링, 계획(plan)을 실행해야 하면 choice 배열.

</details>

<details><summary>Q8. 이 DP 들을 ISR 이나 실시간 루프에서 돌려도 되나요?</summary>

상향식 계단/동전은 O(A·k) 고정 반복이라 WCET 가 입력 크기로 정해진다 — 단 A 가 크면 길다. ISR 에서는 하지 말고 태스크에서, 또는 빌드 시간에 테이블을 만들어 플래시에 둔다(예: 버스트 계획표 A ≤ 4096 → `choice` 4 KB). `lis_push` 는 샘플당 O(log L) 이고 상태가 caller 버퍼라 재진입 가능 — 같은 스트림을 두 문맥이 동시에 push 하지만 않으면 된다. 정적 메모 배열(`s_memo`, `m_memo`)을 쓰는 함수들은 **재진입 불가**다: 두 태스크가 동시에 부르면 서로의 메모를 덮는다 — 메모를 caller 가 넘기는 구조로 바꾸거나 뮤텍스.

</details>

## 9. 추가 연습

### 연습 1 — 인접 금지 최대합 (LeetCode 198 House Robber)

송신 슬롯 n 개에 우선순위 `v[i]` 가 있다. 발열 때문에 **연속 두 슬롯**에 고출력 송신을 할 수 없다. 고른 슬롯의 우선순위 합 최대를 O(1) 메모리로. (AlgoMonster M09 §5 의 상수 전이 DP.)

<details><summary>답 보기</summary>

상태 best(i) = 슬롯 0..i 에서 최대합. 슬롯 i 를 안 쓰면 best(i−1), 쓰면 best(i−2) + v[i] → `max`. 두 변수만 굴린다. v 가 uint32 면 합은 uint64 로 (테스트: `{MAX,0,MAX,0,MAX}` → 3·MAX). 브루트포스: 인접 비트 없는 마스크(`m & (m >> 1)) == 0`) 전부. 실제 출력:

```text
연습 1: 2000 random arrays (n<=18) vs mask brute force, mismatches=0
```

</details>

### 연습 2 — Decode Ways (LeetCode 91)

숫자 문자열을 '1'→A … '26'→Z 로 읽는 방법 수. '0' 은 혼자 못 읽고 "10", "20" 의 일부로만. O(1) 메모리, 포화 덧셈. (Don 의 AlgoMonster 매핑에서 "변형" 으로만 표시된 칸 — M05 §12.)

<details><summary>답 보기</summary>

d(i) = 앞 i 글자를 읽는 방법 수. 마지막 한 글자로 읽기(그 글자 ≠ '0') → d(i−1), 마지막 두 글자로 읽기(첫 글자 ≠ '0' 이고 10..26) → d(i−2). 계단 {1,2} 에 "칸마다 허용되는 보폭이 다른" 막힘이 붙은 것과 같다. 그래서 "111…1" (92개) = F(93), 93개면 포화 — 테스트가 둘 다 확인. 실제 출력:

```text
연습 2: 5000 random digit strings (len<=23) vs split brute force, mismatches=0
```

</details>

### 연습 3 — 비감소 LIS: 최소 삭제로 정렬

센서 단조 카운터 로그(같은 값 반복 허용)에서 비감소가 되게 하려면 최소 몇 개를 지워야 하나. 예: `[3,3,4,2,4,5,5,1]` → 남길 6개(3,3,4,4,5,5), 지울 2개.

<details><summary>답 보기</summary>

tails 에서 "x **초과**인 첫 칸"(upper_bound)을 교체 — 같은 값이 와도 연장된다. 답 = n − LNDS. lower/upper 한 글자 차이라 부분집합 브루트포스로 꼭 대조한다 (같은 값이 많은 range 1..8 입력).

</details>

### 연습 4 — LIS 개수 (LeetCode 673)

순증가 LIS 가 **몇 개**(서로 다른 인덱스 집합)인가. 그리고 이 개수가 언제 uint64 를 넘나.

<details><summary>답 보기</summary>

len[i] 와 cnt[i] 를 같이: j < i, a[j] < a[i] 에서 len[j]+1 이 더 크면 (len, cnt) 를 교체, 같으면 cnt 를 더한다. O(n²). 개수는 지수적일 수 있다: `(1,0),(3,2),(5,4),…` m 쌍이면 각 쌍에서 하나씩 골라 2^m 개. m = 63 (n = 126) → 2^63 정확, m = 64 → 포화. 실제 출력:

```text
연습 3/4: 3000 random arrays (n<=15) vs subset brute force, mismatches lnds=0 count=0
```

</details>

### 연습 5 — 최소 버스트 + 실제 계획 복원

버스트 크기 {1,4,6,9} (탐욕이 틀리는 집합: 12 = 6+6, 탐욕은 9+1+1+1)로 amount 를 만드는 최소 개수와 **사용한 버스트 목록**을 출력. 목록 버퍼가 모자라면 -2.

<details><summary>답 보기</summary>

dp 와 함께 `choice[a]` = 최적해의 마지막 버스트 인덱스(1 B)를 저장. a 에서 `coins[choice[a]]` 를 빼며 0 까지 걷는다. 걸음 수 = dp[amount]. 메모리 3 B × (A+1) (`uint16_t dp` + `uint8_t choice`). 실제 출력:

```text
연습 5: 1500 random (coins<=12, amount<40) vs multiset brute force, mismatches=0
```

(무작위 동전에 중복 값이 섞여도 최소 개수는 같아야 한다 — 그것도 확인.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 19/19, 보고 없음
```

데모 빌드 (위 표의 출처):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench   # LIS 시간, 2√n, 메모 vs 테이블, 넘침 지점
cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth && ./demo_depth   # 재귀 깊이·스택 바이트, 작은 스택에서 SIGBUS
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -   # % 3 → __aeabi_uidiv 호출
```

## 10. 핵심 정리

- DP 네 줄: **상태 / 점화식(마지막 한 수로 경우 나누기) / 기저 / 순서**. 화살표가 작은 상태를 가리키면 오름차순 상향식.
- 하향식 = 필요한 것만 + 재귀 깊이 위험(부르는 순서로 n ↔ 상수), 상향식 = 스택 0·실측 2배 빠름, 링 = 최대 거리+1 칸(2의 거듭제곱 → `&`).
- 조합 vs 순열: 동전 바깥 = 다중집합(518), 금액 바깥 = 순서 있는 수열(377) = 계단. {1,2,3}, 4 → 4 vs 7.
- 금액을 내림차순으로 돌면 각 동전 1회 = 0/1 배낭 (ALGO-M08).
- 개수는 uint64 + 포화: min(참값, 2^64−1) 이 정확히 보존된다. 계단 {1,2} 은 n = 93, {1,2,3} 은 74 에서 넘침.
- 동전 최소는 탐욕이 정준 체계에서만 맞다 ({1,3,4}, 6). 불가능 INF 에 +1 금지.
- LIS: L(i) = a[i] 로 끝나는 길이 (O(n²)), tails = 길이별 최소 끝값 + lower_bound (O(n log n)), 복원은 prev 체인. tails 자체는 LIS 가 아니다.
- 펌웨어: 메모 재귀 128 KB 는 Cortex-M 에서 HardFault, 정적 메모 함수는 재진입 불가, 공간 최적화하면 복원을 잃는다.

## 참고자료

- AlgoMonster 노트 M09 §1~§7 (DP Intro, Climbing Stairs, Tribonacci, Constant Transition, House Robber, Min Cost Climbing Stairs, Tickets), §21~§22 (Non-constant Transition, Longest Increasing Subsequence) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M09_dynamic_programming.md`
- AlgoMonster 노트 M05 §10 (Memoization), §12 (Decode Ways), §13 (Min Coins to Make Change) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M05_backtracking.md`
- T. H. Cormen et al., *Introduction to Algorithms* (3rd ed.), Ch. 15 "Dynamic Programming" — 최적 부분 구조, 겹치는 하위 문제, 메모 vs 상향식.
- OEIS A000073 (Tribonacci numbers): <https://oeis.org/A000073> · A001399 (partitions into at most 3 parts = parts ≤ 3): <https://oeis.org/A001399>
- H. Abelson, G. J. Sussman, *Structure and Interpretation of Computer Programs*, §1.2.2 "Counting change" — 100센트 → 292.
- Project Euler Problem 31 "Coin sums": <https://projecteuler.net/problem=31> (답 73682 는 이 레슨 테스트가 계산해 확인).
- M. L. Fredman, "On computing the length of longest increasing subsequences", *Discrete Mathematics* 11 (1975) — O(n log n) 와 비교 하한 (확인 권장).
- J. Baik, P. Deift, K. Johansson, "On the distribution of the length of the longest increasing subsequence of random permutations", *J. AMS* 12 (1999) — 2√n 과 n^{1/6} 보정 (확인 권장). A. M. Vershik, S. V. Kerov (1977), B. F. Logan, L. A. Shepp (1977) — E[L_n] ~ 2√n.
- D. Kozen, S. Zaks, "Optimal bounds for the change-making problem", *TCS* 123 (1994); D. Pearson, "A polynomial-time algorithm for the change-making problem", *Operations Research Letters* 33 (2005) — 정준 체계 판정 (확인 권장).
- RFC 1982 "Serial Number Arithmetic": <https://www.rfc-editor.org/rfc/rfc1982>
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 (부호 없는 wrap), 6.5p5 (부호 있는 오버플로 UB), 6.3.1.3p3 (부호 있는 형으로의 변환은 구현 정의).
- LeetCode 70 Climbing Stairs · 746 Min Cost Climbing Stairs · 1137 N-th Tribonacci Number · 322 Coin Change · 518 Coin Change II · 377 Combination Sum IV · 300 Longest Increasing Subsequence · 673 Number of Longest Increasing Subsequence · 354 Russian Doll Envelopes · 198 House Robber · 91 Decode Ways.
- 관련 레슨: ALGO-B02 (lower_bound / upper_bound — tails 탐색), ALGO-B11 (재귀와 스택 깊이), ALGO-M03 (백트래킹 — 메모 없는 열거), ALGO-M08 (2D DP, 0/1 배낭의 역방향 루프), ALGO-M15 (상수 나눗셈의 곱셈 역원), ALGO-A03 (비트마스크 DP), C-M12 (UB 카탈로그).
