---
id: ALGO-B04
time: 60분
updated: 2026-11-11
---
# ALGO-B04 답을 이분 탐색: Parametric Search

## 학습 목표

- "최솟값을 구하라"를 "**이 값으로 되나?**"(판정 함수, feasibility predicate)로 바꾸고, 판정이 **단조**(F…FT…T)임을 증명한 뒤 답 공간(answer space)을 이분 탐색할 수 있다.
- 최소 프레임 용량 문제에서 그리디 판정이 **최적**인 이유(greedy stays ahead)와 답 범위 `[max, sum]` 을 말로 설명하고, 판정 함수 호출이 ⌈log₂(sum − max + 1)⌉ 번 이하임을 확인한다.
- 정수 제곱근을 **last true** 탐색(위쪽 mid)으로 짜면서 `mid*mid` 넘침, `hi - lo + 1` 넘침, 0 나눗셈을 피한다. `(uint64_t)sqrt((double)x)` 가 실제로 틀리는 x 를 이 맥에서 찾아 보인다.
- 펌웨어 설정값(DMA 버퍼 크기, 최소 보드레이트, 표준 보드레이트 표 고르기)을 같은 틀로 계산한다.

## 1. 문제

펌웨어 면접의 "최소 버퍼 크기 / 최소 클럭 / 최소 대역폭은?" 질문은 대부분 **답을 이분 탐색**하는 문제다. 공식이 없어도 "이 값이면 되나?"만 O(n) 에 판정할 수 있으면 O(n log 범위) 에 끝난다 — 그리고 최악 시간이 정해져 있다.

ALGO-B02 의 이진 탐색은 **배열 안에서** 값을 찾았다. 이 레슨은 배열이 없다. 후보 답 c = 0, 1, 2, … 자체가 정렬된 가상의 배열이고, 각 칸의 값은 `feasible(c)` (true/false) 다. 그 배열이 F…FT…T 꼴이면 ALGO-B02 의 lower_bound 를 그대로 쓴다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">패킷 1..10 바이트, D = 5 프레임: 용량 14 는 안 되고 15 는 된다</text>
  <text x="10" y="56" fill="currentColor">cap=14</text>
  <g stroke="currentColor" fill="none">
    <rect x="100" y="40" width="98" height="26"/><rect x="206" y="40" width="98" height="26"/><rect x="312" y="40" width="98" height="26"/>
    <rect x="418" y="40" width="98" height="26"/><rect x="524" y="40" width="98" height="26"/><rect x="630" y="40" width="98" height="26"/>
  </g>
  <g class="f-acc"><rect x="100" y="40" width="70" height="26"/><rect x="206" y="40" width="77" height="26"/><rect x="312" y="40" width="49" height="26"/>
    <rect x="418" y="40" width="56" height="26"/><rect x="524" y="40" width="63" height="26"/><rect x="630" y="40" width="70" height="26"/></g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="104" y="58">1+2+3+4</text><text x="210" y="58">5+6</text><text x="316" y="58">7</text>
    <text x="422" y="58">8</text><text x="528" y="58">9</text><text x="634" y="58">10</text>
  </g>
  <text x="100" y="86" fill="currentColor">6 프레임 &gt; D=5 → false (그리디가 최소 프레임 수를 준다: 증명은 5. 풀이)</text>
  <text x="10" y="126" fill="currentColor">cap=15</text>
  <g stroke="currentColor" fill="none">
    <rect x="100" y="110" width="105" height="26"/><rect x="213" y="110" width="105" height="26"/><rect x="326" y="110" width="105" height="26"/>
    <rect x="439" y="110" width="105" height="26"/><rect x="552" y="110" width="105" height="26"/>
  </g>
  <g class="f-ok"><rect x="100" y="110" width="105" height="26"/><rect x="213" y="110" width="91" height="26"/><rect x="326" y="110" width="56" height="26"/>
    <rect x="439" y="110" width="63" height="26"/><rect x="552" y="110" width="70" height="26"/></g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="104" y="128">1+2+3+4+5</text><text x="217" y="128">6+7</text><text x="330" y="128">8</text>
    <text x="443" y="128">9</text><text x="556" y="128">10</text>
  </g>
  <text x="100" y="156" fill="currentColor">5 프레임 ≤ D → true. 채운 막대 = 담긴 바이트, 빈 칸 = 남는 용량 (7 px = 1 B)</text>
  <text x="10" y="196" fill="currentColor" font-weight="bold">답 공간 [max, sum] = [10, 55] 위의 판정 값과 이분 탐색이 물어본 순서</text>
  <rect x="100" y="210" width="69" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="169" y="210" width="551" height="28" class="f-ok" stroke="currentColor"/>
  <text x="112" y="229" fill="currentColor">F (10..14)</text>
  <text x="400" y="229" fill="currentColor">T (15..55)</text>
  <g stroke="currentColor" stroke-width="2">
    <path d="M403 206 L403 242"/><path d="M252 206 L252 242"/><path d="M169 206 L169 242"/><path d="M128 206 L128 242"/><path d="M155 206 L155 242"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="392" y="260">①32</text><text x="241" y="260">②21</text><text x="162" y="276">③15</text>
    <text x="104" y="260">④12</text><text x="140" y="292">⑤14</text>
  </g>
  <text x="96" y="318" fill="currentColor">10</text><text x="706" y="318" fill="currentColor">55</text>
  <text x="250" y="318" fill="currentColor">구간 46칸 → ⌈log₂46⌉ = 6 이하, 실제 5번 물어보고 답 15</text>
</svg>
```

### Part A — 최소 프레임 용량 (그리디 판정 + first true)

패킷 n개(크기 `sz[i]` 바이트, **순서 고정, 쪼갤 수 없음**)를 D개 프레임으로 보낸다. 모든 프레임의 용량이 같을 때 그 **최소 용량**을 구한다.

- `frames_needed(sz, n, cap)` — "넣을 수 있을 만큼 넣고, 넘치면 새 프레임" 그리디로 필요한 프레임 수. 패킷 하나가 cap 보다 크면 `SIZE_MAX`.
- `min_frame_capacity(sz, n, D)` — 주어진 판정 `fits()` (= `frames_needed ≤ D`, 호출 횟수를 셈) 로 `[max, sum]` 에서 first true. `n == 0` → 0, `D == 0 && n > 0` → `UINT64_MAX`. 합은 **uint64**.
- 테스트: 손 계산 17개(D=1 → 합, D≥n → 최댓값, 0 바이트 패킷, 32비트를 넘는 합 등) + 독립 브루트포스 두 개(n ≤ 12 **자르는 위치 전수 조사**, n ≤ 48 **O(D n²) DP**)와 랜덤 대조 + 판정 함수 단조성 직접 검사 + probe 횟수 상한 검사.

### Part B — 정수 제곱근 (last true, 넘침 경계)

`floor(sqrt(x))` = `s*s ≤ x` 인 가장 큰 s. 판정 `s*s ≤ x` 는 T…TF…F 꼴(앞이 true)이라 **last true** 탐색이다.

- `isqrt32(x)` — 64비트 연산 없이: `mid*mid ≤ x` 대신 `mid ≤ x / mid`. 답이 65535 이하이니 hi 를 거기로 자른다.
- `isqrt64(x)` — hi 를 2³²−1 로 자르면 `mid*mid ≤ (2³²−1)² < 2⁶⁴` 라 uint64 곱이 넘치지 않는다.
- 테스트: x = 0..2²⁰ **전수**(선형 브루트포스), 모든 k ≤ 65535 의 k²−1/k²/k²+1, 랜덤 32·64비트(비트 길이도 랜덤)를 `s*s ≤ x < (s+1)²` 넘침 없는 직접 확인으로, 64비트 k²±1 경계, `UINT32_MAX`, `UINT64_MAX`, `(2³²−1)²`.

### Part C — RS-485 TDMA 로거: DMA 버퍼 크기와 보드레이트

드론 비행 컨트롤러의 로거 노드가 RS-485 멀티드롭 버스(UART 8N1)를 쓴다. 마스터는 20 ms 로깅 주기마다 이 노드에 **1 ms 슬롯 D개**를 준다. 큐에 쌓인 기록 24개(IMU 28 B, 기압 12 B, 상태 16 B, GPS/이벤트 40~72 B)는 **순서대로**(타임스탬프 단조) 보내야 하고, 기록 하나를 두 슬롯에 **쪼갤 수 없다**(슬롯 하나 = DMA 전송 하나 = 수신 측 파서가 통째로 받는 단위).

- 최소 슬롯 페이로드 = Part A 의 `min_frame_capacity` = **DMA TX 버퍼 크기**(RAM).
- `min_baud_exact(need, slot_us, baud_max)` — `slot_bytes(b) ≥ need` 인 최소 정수 보드레이트 (판정 + 이분 탐색, 닫힌 식 `⌈need·10⁷/slot_us⌉` 과 대조).
- `pick_std_baud(tbl, nb, need, slot_us)` — 표준 보드레이트 표(9600 … 3 M)에서 첫 만족 항목 (표 인덱스 위의 first true, 선형 탐색과 대조).

## 2. Problem (English)

> **Minimum frame capacity.** You must transmit `n` packets in the given order. Packet `i` has `sz[i]` bytes and cannot be split. You have `D` frames, all with the same capacity `cap` bytes; each frame carries a contiguous run of packets whose total size is at most `cap`. Return the minimum `cap`.
>
> ```c
> uint64_t min_frame_capacity(const uint32_t *sz, size_t n, size_t D);
> ```
>
> Constraints: `0 <= n <= 10^5`, `0 <= sz[i] <= 2^32 - 1`, `1 <= D` (return `UINT64_MAX` if `D == 0` and `n > 0`; return 0 if `n == 0`). O(n log(sum)) time, O(1) extra memory.
>
> Examples: `sz = [1..10], D = 5 -> 15` (`[1,2,3,4,5] [6,7] [8] [9] [10]`); `sz = [3,2,2,4,1,4], D = 3 -> 6`; `sz = [1,2,3,1,1], D = 4 -> 3`.
>
> **Follow-up: integer square root.** Implement `uint32_t isqrt32(uint32_t x)` and `uint64_t isqrt64(uint64_t x)` returning `floor(sqrt(x))` without floating point. Must be exact for every input including `UINT64_MAX` (answer `4294967295`). Why is `(uint64_t)sqrt((double)x)` not acceptable?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데(`frames_needed`, `min_frame_capacity`, `isqrt32`, `isqrt64`, `min_baud_exact`, `pick_std_baud`). 판정 래퍼 `fits()`, `slot_bytes()`, 브루트포스 세 개는 주어진다. 스텁은 0/-1 을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:138  frames_needed(w1, 10, 15) == 5
…
Part A random: cuts-brute mismatches=3690  dp-brute mismatches=1500  monotonic violations=35997  too-many-probes=0
…
Part B: isqrt32 bad=1339166  isqrt64 bad=1343843  around-squares bad=265535  32/64 cross bad=0
…
FAIL starter.c:291  fits(rec, nrec, D, cap) && !fits(rec, nrec, D, cap - 1)
7/48 passed
```

(exit 1. 통과한 7개는 TODO 와 무관하거나 스텁 값이 우연히 맞는 것 — `isqrt32(0) == 0`, `slot_bytes`, 0 을 기대하는 경계 등.)

## 4. 힌트

<details><summary>힌트 1 — 질문을 뒤집는다</summary>

"최소 용량은?" 대신 "용량 c 로 D 프레임 안에 되나?" c 가 되면 c+1 도 된다(같은 나누기가 그대로 통한다). 그러면 c = 0, 1, 2, … 의 답은 F…FT…T. 처음 T 를 찾으면 된다.

</details>

<details><summary>힌트 2 — 판정은 그리디 한 번</summary>

정해진 c 에서 프레임 수를 최소로 쓰는 방법: 지금 프레임에 다음 패킷이 들어가면 넣고, 안 들어가면 새 프레임. "왜 이게 최소인가"는 앞에서부터 k 번째 프레임까지 그리디가 **항상 더 멀리 가 있다**는 귀납으로 보인다.

</details>

<details><summary>힌트 3 — 답의 범위</summary>

c < max(sz) 면 가장 큰 패킷이 어디에도 안 들어간다 → 무조건 false. c = sum 이면 한 프레임에 다 들어간다 → D ≥ 1 이면 true. 그래서 `[max, sum]` 에서 찾고, 합은 uint64.

</details>

<details><summary>힌트 4 — first true 와 last true 의 mid</summary>

first true(`if ok hi = mid; else lo = mid + 1`)는 아래쪽 mid `lo + (hi-lo)/2`. last true(`if ok lo = mid; else hi = mid - 1`)는 **위쪽** mid `lo + (hi-lo+1)/2` — 아래쪽을 쓰면 hi = lo+1 에서 `lo = mid` 가 제자리라 영원히 돈다.

</details>

<details><summary>힌트 5 — 제곱이 넘치지 않게</summary>

32비트: 답 ≤ 65535 이니 hi = min(x, 65535). 비교는 `mid <= x / mid` (mid ≥ 1 은 위쪽 mid 가 보장). 64비트: hi = min(x, 2³²−1) 이면 `mid*mid` 가 uint64 안에 들어간다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 확인 질문 | 순서 고정? 쪼개기 불가? 크기 범위(합이 32비트를 넘나)? D=0, n=0 은? 프레임을 비워 둬도 되나? |
| 2. 브루트포스 + 복잡도 | 자르는 위치 n−1 곳 중 ≤ D−1 개 선택 → O(2ⁿ·n). DP `dp[k][i]` → O(D·n²) |
| 3. 더 나은 생각 + 불변식 | 판정 `fits(c)` 가 단조 → 답 ∈ `[lo, hi]`, `fits(hi)` 는 true, lo 미만은 전부 false 를 유지하며 반씩 |
| 4. 코드 | 그리디 판정 10줄 + first true 루프 6줄 |
| 5. 소리 내어 말할 테스트 | `[1..10]` D=5 → 15, D=1 → 합, D ≥ n → 최댓값, n=0, 패킷 하나, 모두 같은 값, 0 바이트, 합 > 2³² |
| 6. 복잡도 | 시간 O(n · log(sum − max)), 추가 메모리 O(1), 스택 O(1) (재귀 없음), 최악 = 평균 |

### Part A — 판정 함수가 단조인 이유 (증명)

정의: `feasible(c)` ⇔ 패킷 열을 **연속 구간 ≤ D 개**로 나누되 각 구간 합 ≤ c 인 방법이 존재한다.

**보조정리 1 (단조성).** c 에서 되면 c' ≥ c 에서도 된다. — c 에서 쓴 나누기를 그대로 쓰면 각 구간 합 ≤ c ≤ c'. ∎
그래서 정수 c 위에서 `feasible` 은 F…FT…T 이고, "처음 T" 가 답이다.

**보조정리 2 (그리디가 최소 프레임 수).** 크기가 모두 0 이상일 때, c 에서 가능한 아무 나누기 Q (구간 끝 위치 q₁ < q₂ < … < q_m = n) 를 잡자. 그리디의 j 번째 프레임 끝을 g_j 라 하면 **g_j ≥ q_j** (그리디가 항상 같거나 더 멀리 가 있다).

- j = 1: Q 의 첫 구간은 합 ≤ c 인 접두사다. 그리디는 합 ≤ c 인 **가장 긴** 접두사를 담으니 g₁ ≥ q₁.
- j → j+1: g_j ≥ q_j 라 하자. g_j ≥ q_{j+1} 이면 끝. 아니면 그리디의 다음 프레임은 패킷 g_j+1 부터 시작하고, 구간 (g_j, q_{j+1}] 은 Q 의 구간 (q_j, q_{j+1}] 의 **뒷부분**이므로 합 ≤ c (크기 ≥ 0 이라 부분 합이 전체 합 이하). 그리디는 적어도 q_{j+1} 까지 담는다 → g_{j+1} ≥ q_{j+1}. ∎

따라서 그리디는 m 개 이하의 프레임으로 끝나고, `feasible(c)` ⇔ `frames_needed(c) ≤ D`. (크기에 음수가 있으면 "부분 합 ≤ 전체 합" 이 깨져 그리디가 틀린다 — 크기라서 성립하는 증명이다.)

**답의 범위.** c < max 면 가장 큰 패킷을 담을 프레임이 없다 → false. c = sum 이면 프레임 하나 → D ≥ 1 이면 true. 그래서 처음 T 는 `[max, sum]` 안에 반드시 있다. 하네스의 단조성 검사는 랜덤 배열 300개에서 c = max..sum 전부에 대해 `frames_needed` 가 늘지 않음을 직접 확인한다 (위반 0).

### Part A — 탐색 루프와 불변식

```c
uint64_t lo = max, hi = sum;          /* 불변식: 답 ∈ [lo, hi] */
while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2; /* lo <= mid < hi */
    if (fits(sz, n, D, mid)) hi = mid; /* mid 가 답이거나 답은 왼쪽 */
    else lo = mid + 1;                 /* 보조정리 1: mid 이하는 전부 false */
}
return lo;                            /* lo == hi == 처음 true */
```

`[1..10]`, D = 5 의 실제 진행 (위 그림 아래쪽):

| 단계 | lo | hi | mid | frames_needed(mid) | ≤ 5? | 다음 |
|---|---|---|---|---|---|---|
| 1 | 10 | 55 | 32 | 2 | T | hi = 32 |
| 2 | 10 | 32 | 21 | 3 | T | hi = 21 |
| 3 | 10 | 21 | 15 | 5 | T | hi = 15 |
| 4 | 10 | 15 | 12 | 6 | F | lo = 13 |
| 5 | 13 | 15 | 14 | 6 | F | lo = 15 |
| 끝 | 15 | 15 | | | | 답 15 |

**probe 횟수.** 구간 크기 L 에서 한 번 물으면 ⌈L/2⌉ 또는 ⌊L/2⌋ 로 줄어든다 → 최대 ⌈log₂ L⌉ 번. 하네스가 랜덤 1500개에서 `g_pred_calls ≤ ⌈log₂(sum − max + 1)⌉` 을 확인한다(위반 0). sum < 2⁶⁴ 이니 최악에도 64번, 판정 한 번이 O(n) → **O(n log S)**, S = sum − max + 1.

**왜 이게 좋은가.** DP 는 O(D·n²) 시간에 O(D·n) 메모리(이 레슨 브루트포스는 48×48 표). 이분 탐색은 추가 메모리 O(1), 재귀 없음, 실행 시간 상한이 n 과 비트 수로 정해진다 — 펌웨어 부팅 중 설정 계산이나 타이밍 예산이 정해진 태스크에 넣기 좋다. 정답이 "어떤 구간 합 하나"라는 사실을 이용해 후보 O(n²) 개를 정렬해 탐색하는 방법도 있지만, 값 범위를 직접 반으로 자르는 쪽이 짧고 메모리가 없다.

### Part B — 정수 제곱근: last true

판정 `ok(s) = s*s ≤ x` 는 s 가 커지면 true → false 로 한 번만 바뀐다(T…TF…F). 원하는 건 **마지막 true**.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">isqrt(17): s = 0..17 위의 last true, 위쪽 mid</text>
  <g stroke="currentColor">
    <rect x="60" y="36" width="180" height="26" class="f-ok"/>
    <rect x="240" y="36" width="468" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="78" y="54">0</text><text x="114" y="54">1</text><text x="150" y="54">2</text><text x="186" y="54">3</text><text x="222" y="54">4</text>
    <text x="258" y="54">5</text><text x="294" y="54">6</text><text x="330" y="54">7</text><text x="366" y="54">8</text><text x="402" y="54">9</text>
    <text x="438" y="54">10</text><text x="474" y="54">11</text><text x="510" y="54">12</text><text x="546" y="54">13</text><text x="582" y="54">14</text>
    <text x="618" y="54">15</text><text x="654" y="54">16</text><text x="690" y="54">17</text>
  </g>
  <text x="62" y="80" fill="currentColor" font-size="12">s*s ≤ 17 (true)</text>
  <text x="244" y="80" fill="currentColor" font-size="12">s*s &gt; 17 (false)</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <rect x="60" y="96" width="648" height="18"/><rect x="60" y="128" width="324" height="18"/>
    <rect x="204" y="160" width="180" height="18"/><rect x="204" y="192" width="72" height="18"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="384" y="96" width="36" height="18"/><rect x="204" y="128" width="36" height="18"/>
    <rect x="276" y="160" width="36" height="18"/><rect x="240" y="192" width="36" height="18"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="110">①</text><text x="10" y="142">②</text><text x="10" y="174">③</text><text x="10" y="206">④</text>
    <text x="430" y="110">lo=0 hi=17 mid=9: 81 &gt; 17 → hi=8</text>
    <text x="430" y="142">lo=0 hi=8 mid=4: 16 ≤ 17 → lo=4</text>
    <text x="430" y="174">lo=4 hi=8 mid=6: 36 &gt; 17 → hi=5</text>
    <text x="430" y="206">lo=4 hi=5 mid=5: 25 &gt; 17 → hi=4 → 답 4</text>
  </g>
  <text x="10" y="240" fill="currentColor">테두리 = [lo, hi], 채운 칸 = mid. 불변식: lo 는 언제나 true, hi 보다 큰 s 는 언제나 false.</text>
  <text x="10" y="262" fill="currentColor">④에서 아래쪽 mid = 4 를 쓰면: 16 ≤ 17 → lo = 4 (제자리) → 무한 루프.</text>
  <text x="10" y="284" fill="currentColor">위쪽 mid = lo + (hi − lo + 1)/2 는 lo &lt; mid ≤ hi 라 매번 구간이 줄고, mid ≥ 1 이라 x / mid 가 안전.</text>
</svg>
```

**넘침 세 군데.**

| 위치 | 무엇이 넘치나 | 이 레슨의 처리 |
|---|---|---|
| `mid*mid` (uint32) | hi = x 그대로면 mid 가 65536 이상 → 곱이 2³² 에서 감긴다 (부호 없는 정수라 UB 는 아니고 mod 2³² — C11 6.2.5p9) | hi = min(x, 65535) 이고 비교는 `mid <= x / mid` |
| `mid*mid` (uint64) | hi = x 면 mid ≥ 2³² → 2⁶⁴ 에서 감김 | hi = min(x, 2³²−1) → `(2³²−1)² = 2⁶⁴ − 2³³ + 1 < 2⁶⁴` |
| `hi - lo + 1` | lo = 0, hi = UINT32_MAX 면 2³² → 0 으로 감김 → mid = 0 → `x / 0` (UB) | hi 클램프 덕분에 hi ≤ 65535 |

32비트판에 클램프와 나눗셈 비교를 **둘 다** 둔 이유: 클램프만 있으면 `mid*mid ≤ 65535² = 4294836225` 라 uint32 곱으로도 충분하다. 나눗셈 비교는 클램프를 깜빡해도 곱 넘침은 막아 주는 이중 안전장치다. 대신 나눗셈 비용이 있다 (8. Q5 — Cortex-M0 에는 나눗셈 명령이 없다).

`demo_bugs.c` 로 실제로 돌린 버그 변형 (실제 출력):

```text
B1 isqrt32 hi=x, uint32 mid*mid: wrong 916877 / 1000000 random x  (e.g. x=723471715 -> 7067919, correct 26897)
B2 isqrt64 hi=x, uint64 mid*mid: wrong 68574 / 1000000 random x  (e.g. x=16434719676095672086 -> 16306284183976825026, correct 4053975786)
   B1 first wrong x (scan from 0): 131071
B3 last-true with lower mid: 578 of x in [0,1000) never terminate (first x = 1)
```

B1 은 x < 131071 에서는 **멀쩡하다** — 작은 수만 손으로 넣어 보는 테스트로는 못 잡는다. 그래서 하네스는 모든 k ≤ 65535 의 k² 양옆과 `UINT32_MAX` 를 넣는다.

**반복 횟수.** isqrt32 는 구간 ≤ 65536 → 최대 16번, isqrt64 는 최대 32번. 매번 곱(또는 나눗셈) 한 번. 곱·나눗셈 없이 시프트·뺄셈만 쓰는 비트 단위 isqrt 는 ALGO-M15 에서 다룬다 (반복 횟수는 같다).

### Part B — `(uint64_t)sqrt((double)x)` 는 언제 틀리나

`demo_sqrt_double.c` 실행 결과 (이 맥, Apple Silicon, -O2, 실제 출력):

```text
uint32 all 2^32 x: (uint32_t)sqrt((double)x) wrong = 0
uint32 all 2^32 x: (uint32_t)sqrtf((float)x)  wrong = 6986365 (too big 6986365, too small 0), first x = 16785407
  e.g. x = 16785407: sqrtf -> 4097, correct 4096
smallest k with sqrt_double(k^2-1) != k-1: k = 67108865 (k^2-1 = 4503599761588224, 2^53 = 9007199254740992)
  x = 18014398509481983: (uint64_t)sqrt((double)x) = 134217728, correct = 134217727
  x = 18446744065119617024: (uint64_t)sqrt((double)x) = 4294967295, correct = 4294967294
  x = 18446744073709551615: (uint64_t)sqrt((double)x) = 4294967296, correct = 4294967295
random uniform 64-bit x (2000000): double wrong = 0 (too big 0, too small 0)
random k in [1, 2^32): sqrt_double(k^2-1) wrong = 1968884 of 2000000 (98.4%)
double + integer fix-up: wrong = 0 of 4000000
```

읽는 법:

- **32비트 x 는 double 로 안전**(2³² 개 전수, 오차 0). float 은 x > 2²⁴ 부터 입력 자체가 반올림돼 700만 개 가까이 1 크게 나온다 — Cortex-M4F 의 FPU 는 단정밀도뿐이라 `sqrtf` 를 쓰고 싶은 유혹이 큰 곳이다.
- **첫 실패 k = 2²⁶ + 1** 은 입력 반올림이 아니다 (k²−1 = 2⁵² + 2²⁷ < 2⁵³ 은 double 로 정확). √(k²−1) = k − 1/(2k) − …, 그리고 [2²⁶, 2²⁷) 의 double 간격은 2⁻²⁶ 이다. 1/(2k) < 2⁻²⁷ (반 간격)이 되는 순간, 즉 k > 2²⁶ 부터 정확히 반올림된(IEEE 754 sqrt 는 correctly rounded) 결과가 k 로 올라간다. 수학적으로 정확한 결과를 정확히 반올림해도 **floor 는 틀릴 수 있다**.
- **균일 랜덤 64비트 x 200만 개로는 하나도 못 찾았다.** 틀리는 x 는 완전제곱 바로 아래 좁은 띠에만 있다. k²−1 을 일부러 넣으면 98.4% 가 틀린다. "랜덤 테스트만 돌렸다"가 왜 부족한지 보여 주는 예.
- `UINT64_MAX` 는 double 로 바꾸면 2⁶⁴ 이 되고 sqrt 는 2³². 이것을 `uint32_t` 로 변환하면 범위 밖 부동소수 → 정수 변환이라 **UB** (C11 6.3.1.4p1).
- 고치는 법: double 로 추정한 뒤 정수로 보정(`while (s*s > x) s--; while ((s+1)² ≤ x) s++;`, 넘침 주의) → 오차 0. 하지만 FPU·libm 이 없는 MCU 에서는 처음부터 정수 탐색이 낫다.

### Part C — 판정 + 이분 탐색을 두 번

1단계는 Part A 그대로다: 최소 슬롯 페이로드 = `min_frame_capacity(rec, 24, D)`. 2단계는 "보드레이트 b 면 슬롯 하나에 `slot_bytes(b) = ⌊b · slot_us / 10⁷⌋` 바이트(8N1 = 바이트당 10비트)" 가 b 에 대해 단조 증가라서, `slot_bytes(b) ≥ need` 의 first true 를 정수 b 위에서(`min_baud_exact`) 또는 표준 표 인덱스 위에서(`pick_std_baud`, 모양은 ALGO-B02 의 lower_bound) 찾는다. 닫힌 식 `⌈need · 10⁷ / slot_us⌉` 과 2만 번 대조해 불일치 0. 식이 있는데 왜 탐색을 하나? 실제 판정은 식으로 안 풀리는 경우가 많다 — 슬롯당 헤더·CRC 바이트, RS-485 드라이버 방향 전환 시간, 보드레이트 분주기 정수 반올림 오차를 넣으면 `slot_bytes` 는 더 이상 깔끔한 식이 아니지만 **단조성만 유지되면** 같은 루프가 그대로 동작한다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">D = 8 슬롯 × 1 ms, 최소 페이로드 108 B: 슬롯별로 담긴 바이트</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="40" width="70" height="160"/><rect x="140" y="40" width="70" height="160"/><rect x="220" y="40" width="70" height="160"/>
    <rect x="300" y="40" width="70" height="160"/><rect x="380" y="40" width="70" height="160"/><rect x="460" y="40" width="70" height="160"/>
    <rect x="540" y="40" width="70" height="160"/><rect x="620" y="40" width="70" height="160"/>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="60" y="99" width="70" height="101"/><rect x="140" y="52" width="70" height="148"/><rect x="220" y="76" width="70" height="124"/>
    <rect x="460" y="76" width="70" height="124"/><rect x="540" y="52" width="70" height="148"/><rect x="620" y="135" width="70" height="65"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="300" y="40" width="70" height="160"/><rect x="380" y="40" width="70" height="160"/>
  </g>
  <path d="M50 52 L700 52" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="430" y="34" fill="currentColor" font-size="12">점선 = 100 B (1 Mbaud 한 슬롯)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="95" y="218">68</text><text x="175" y="218">100</text><text x="255" y="218">84</text><text x="335" y="218">108</text>
    <text x="415" y="218">108</text><text x="495" y="218">84</text><text x="575" y="218">100</text><text x="655" y="218">44</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="335" y="120">64+28</text><text x="335" y="136">+16</text><text x="415" y="120">28+12</text><text x="415" y="136">+28+40</text>
  </g>
  <text x="10" y="246" fill="currentColor">두 슬롯(경고색)이 108 B 로 꽉 찬다 → 1 Mbaud(100 B/슬롯)로는 9 슬롯이 필요하다.</text>
  <text x="10" y="268" fill="currentColor">선택지: 2 Mbaud 로 올리기 / 슬롯 2개 더 받기 (D=10 → 92 B → 921600 baud) /</text>
  <text x="10" y="290" fill="currentColor">큰 기록을 쪼갤 수 있게 프로토콜 바꾸기.</text>
</svg>
```

`solution.c` 의 Part C 줄 (실제 출력):

```text
Part C: 24 records, 8 slots x 1000 us -> min slot payload (= DMA TX buffer) 108 B, frames used 8, min baud 1080000, pick 2000000 (slot holds 200 B)
```

`demo_tdma_sweep.c` 로 D 를 바꿔 본 표 (실제 출력, 합 696 B, 최대 기록 72 B):

| D | 최소 페이로드 B | 이분 탐색 probe | 선형 탐색 probe (72 부터 +1) | 최소 보드레이트 | 표준 표 선택 | 그 보드레이트의 슬롯 B |
|---|---|---|---|---|---|---|
| 1 | 696 | 9 | 625 | 6960000 | 없음 | — |
| 2 | 352 | 10 | 281 | 3520000 | 없음 | — |
| 3 | 244 | 10 | 173 | 2440000 | 3000000 | 300 |
| 4 | 184 | 9 | 113 | 1840000 | 2000000 | 200 |
| 8 | 108 | 10 | 37 | 1080000 | 2000000 | 200 |
| 10 | 92 | 10 | 21 | 920000 | 921600 | 92 |
| 12 | 72 | 10 | 1 | 720000 | 921600 | 92 |
| 24 | 72 | 10 | 1 | 720000 | 921600 | 92 |

(D = 6, 7, 16 줄은 생략: 140/116/72 B.) 읽을 것: probe 는 D 와 무관하게 9~10번(구간 [72, 696] 의 ⌈log₂ 625⌉ = 10). D 가 크면 답이 바로 max 라 선형 탐색이 1번에 끝나는데 이분 탐색은 10번 묻는다 → 실무에서는 `fits(max)` 를 먼저 한 번 보는 것이 싸다. 1 Mbaud 가 8 B 모자라 2 Mbaud 로 뛰는 D = 8 이 이 레슨의 시나리오: "버퍼는 108 B, 보드레이트는 표준 값 중 2 M" 이라는 두 숫자를 근거와 함께 답할 수 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 48/48 passed, 0.2 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 48/48, 보고 없음
```

실제 출력 요약:

```text
Part A random: cuts-brute mismatches=0  dp-brute mismatches=0  monotonic violations=0  too-many-probes=0
Part B: isqrt32 bad=0  isqrt64 bad=0  around-squares bad=0  32/64 cross bad=0
Part C: 24 records, 8 slots x 1000 us -> min slot payload (= DMA TX buffer) 108 B, frames used 8, min baud 1080000, pick 2000000 (slot holds 200 B)
48/48 passed
```

데모 세 개 (solution.c 와 독립, 각자 함수를 복사해 둠):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sqrt_double.c -lm -o demo_sqrt && ./demo_sqrt     # 2^32 전수 포함, 약 4 s
cc -std=c11 -Wall -Wextra -Werror -O2 demo_tdma_sweep.c -o demo_sweep && ./demo_sweep
```

## 7. 자주 하는 실수

**1. 하한을 0 으로 잡고, 판정에서 "패킷 > cap" 을 안 거른다.** 큰 패킷도 "프레임 하나" 로 세면 cap 이 작아도 프레임 수가 n 을 넘지 않아 true 가 나온다. `demo_bugs` 실제 결과: 랜덤 2만 개 중 **8991개** 틀림, `{10, 1, 1}` D=3 → 정답 10 대신 **0**. → `if (sz[i] > cap) return SIZE_MAX;` 그리고 lo = max.

**2. 합을 uint32 로 더한다.** 패킷 크기가 크면 hi 가 감겨 답이 엉뚱한 구간에서 나온다. 실제: 2만 개 중 **2704개** 틀림 (4개에 1개꼴로 32비트 전체 크기를 넣은 경우). → `uint64_t hi`. 판정 안의 `used + sz[i]` 도 uint64.

**3. 답 후보에서 양 끝을 뺀다** (`hi = sum - 1`, `lo = max + 1` 같은 "어차피 끝은 아니겠지"). D=1 이면 답이 sum, D ≥ n 이면 답이 max 다. 실제: 2만 개 중 **11044개**, `[1..10]` D=1 → 54, D=10 → 11. → 불변식 "답 ∈ [lo, hi]" 를 먼저 말하고 경계를 정한다.

**4. last true 에 아래쪽 mid.** `lo = mid` 가 제자리라 hi = lo+1 에서 안 끝난다. 실제: x ∈ [0, 1000) 중 **578개**가 무한 루프 (x = 1 부터). → `mid = lo + (hi - lo + 1) / 2`. 외우는 법: "lo = mid 를 쓰면 mid 를 위로".

**5. `mid*mid` 를 hi = x 그대로 두고 계산한다.** 실제: uint32 판 랜덤 100만 개 중 **916877개**, uint64 판 **68574개** 틀림. 작은 x(< 131071)에서는 맞아서 손 테스트로는 안 보인다. → hi 클램프 또는 `mid <= x / mid`.

```c
uint32_t lo = 0, hi = x;                                  /* 틀림: 클램프 없음 */
...  if (mid * mid <= x) ...                               /* 65536 이상이면 2^32 에서 감김 */
uint32_t lo = 0, hi = x < 65535u ? x : 65535u;            /* 고침 */
...  if (mid <= x / mid) ...                               /* 위쪽 mid 라 mid >= 1 */
```

**6. 판정을 "정확히 D 프레임" 으로 쓴다.** `frames_needed(c) == D` 는 단조가 아니다 — c 가 크면 D 보다 적게 써서 false 가 된다. 프레임을 비워 두는 것이 허용되면 조건은 언제나 **≤ D**. 면접에서는 "남는 프레임은 비워 둬도 되나요?" 를 먼저 묻는다.

**7. `(uint64_t)sqrt((double)x)` 를 정답으로 낸다.** 위 demo: k > 2²⁶ 에서 k²−1 이 1 크게 나오고, `UINT64_MAX` 는 2³² 이 된다. 랜덤 테스트로는 안 잡힌다. → 정수 탐색, 또는 double 추정 후 정수 보정.

## 8. 면접 Q&A

<details><summary>Q1. 이분 탐색을 써도 되는 근거를 증명해 보세요. 그리디 판정이 왜 최적인가요?</summary>

두 단계다. (1) c 에서 가능한 나누기는 c+1 에서도 가능 → `feasible` 이 F…FT…T. (2) 고정된 c 에서 그리디(넣을 수 있으면 넣기)가 최소 프레임 수: 아무 유효한 나누기 Q 에 대해 그리디의 j 번째 프레임 끝 ≥ Q 의 j 번째 끝을 귀납으로 보인다 — Q 의 구간 하나가 c 이하이면 그 뒷부분도 c 이하(크기 ≥ 0)라서 그리디는 적어도 거기까지 간다. 그래서 `feasible(c) ⇔ greedy(c) ≤ D`. 꼬리: "크기에 음수가 있으면?" → 부분 합 ≤ 전체 합이 깨져 그리디가 최소가 아니고, 단조성(보조정리 1)은 여전히 맞지만 판정을 다른 방법(DP)으로 해야 한다. 꼬리 2: "이 문제의 이름?" → LeetCode 1011 Capacity To Ship Packages Within D Days. D 를 사람 수로 바꾸면 LeetCode 410 Split Array Largest Sum (AlgoMonster 의 Newspapers 와 같은 문제).

</details>

<details><summary>Q2. 왜 범위가 [0, sum] 이 아니라 [max, sum] 이죠? 시간 복잡도를 정확히 말해 보세요.</summary>

c < max 면 무조건 false 이고, 판정이 그 경우를 `SIZE_MAX` 로 거르면 [0, sum] 도 정답은 나온다 — 하한을 max 로 두는 건 probe 를 조금 줄이고 "답 ∈ [lo, hi]" 불변식을 처음부터 참으로 만든다. 시간: 판정 O(n) × probe ⌈log₂(sum − max + 1)⌉ ≤ 64 → O(n log S). 추가 메모리 O(1), 스택 O(1). 측정(Part C 표): 구간 625 칸에서 probe 9~10번, 선형 탐색은 최대 625번. 꼬리: "D ≥ n 인 경우가 흔하면?" → `fits(max)` 를 먼저 보면 1번에 끝난다 (표의 D=12, 24 행).

</details>

<details><summary>Q3. lo + (hi − lo)/2 를 쓰는 이유는? 이 레슨에서 실제로 넘칠 수 있는 곳은 어디였나요?</summary>

`(lo + hi) / 2` 는 lo + hi 가 타입 폭을 넘으면 감긴다 (부호 없는 타입은 mod 2ᴺ, `int` 면 부호 있는 넘침이라 UB — C11 6.5p5). `lo + (hi - lo)/2` 는 hi ≥ lo 인 동안 안전하다. 이 레슨에서 실제 위험은 세 곳: `mid*mid`(곱), `hi - lo + 1`(lo = 0, hi = UINT32_MAX 면 0 으로 감겨 mid = 0 → `x / 0` UB), 합(uint32 로 더하면 감김). 꼬리: "Cortex-M 에서 0 으로 나누면?" → ARMv7-M 의 UDIV/SDIV 는 CCR.DIV_0_TRP 가 0 이면 결과 0 을 내고 계속 간다(1 이면 UsageFault). 그러면 `0 <= x/0` 이 참 → lo = 0 제자리 → 무한 루프로 워치독 리셋. C 언어 차원에서는 UB 이니 애초에 mid ≥ 1 을 보장해야 한다.

</details>

<details><summary>Q4. 그냥 sqrt() 를 쓰면 안 되나요?</summary>

이 맥에서 직접 확인했다: 32비트 x 는 double 경로가 2³² 개 전부 정확하지만, 64비트에서는 k = 2²⁶+1 부터 k²−1 이 1 크게 나오고(입력은 정확한데 결과 반올림이 k 로 올라감), `UINT64_MAX` 는 2³² 이 된다. float 경로(Cortex-M4F 의 하드웨어 `VSQRT.F32` 를 쓰고 싶은 경우)는 32비트 x 중 6986365개가 틀린다(첫 예 x = 16785407 → 4097, 정답 4096). 게다가 Cortex-M4F/M33 의 FPU 는 단정밀도라 double sqrt 는 소프트웨어 에뮬레이션이다. 꼬리: "그래도 빠르게 하려면?" → float 으로 근삿값 → 정수 보정 1~2단계, 또는 정수 Newton 반복(초기값만 잘 주면 몇 번에 수렴). 결과가 floor 와 정확히 같아야 하는 곳(보정 테이블 인덱스, 거리 임계값 비교)에서는 정수 버전이 기본이다. 비교만 필요하면 sqrt 를 아예 안 쓰고 `dx² + dy² ≤ r²` 로 바꾸는 게 최선.

</details>

<details><summary>Q5. 나눗셈기가 없는 Cortex-M0 나 8비트 MCU 라면 isqrt32 를 어떻게 바꾸겠어요?</summary>

이 맥에서 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 로 보면 `mid <= x / mid` 가 `udiv` 명령으로 나오고, `--target=thumbv6m-none-eabi -mcpu=cortex-m0` 에서는 `bl __aeabi_uidiv` 라이브러리 호출이 된다(둘 다 실제 확인). M0 에서는 16번의 루프마다 소프트웨어 나눗셈이 들어가니 곱 비교로 바꾼다: hi 를 65535 로 클램프하면 `mid*mid` 가 uint32 에 들어가므로 나눗셈이 필요 없다(같은 asm 에서 `muls` 로 나옴). 더 가볍게는 비트 단위 isqrt(한 번에 결과 비트 하나씩, 시프트·뺄셈만) — ALGO-M15. 8비트 AVR 이면 16비트 x 의 isqrt 만 필요한 경우가 많아 반복 8번. 꼬리: "결과를 상수 시간으로?" → 반복 횟수가 x 와 무관하게 16번이 되도록 `while (lo < hi)` 대신 고정 횟수 루프로 쓰면 타이밍이 일정해진다 (ISR·암호 코드에서 중요).

</details>

<details><summary>Q6. first true 와 last true 는 어떻게 구분하고, 각각 루프를 어떻게 쓰나요?</summary>

"최소 c 중 되는 것"(용량, 보드레이트, 속도) = F…FT…T 의 **처음 T** → `if (ok(mid)) hi = mid; else lo = mid + 1;` + 아래쪽 mid. "최대 s 중 되는 것"(제곱근, 최소 간격의 최대화) = T…TF…F 의 **마지막 T** → `if (ok(mid)) lo = mid; else hi = mid - 1;` + 위쪽 mid. 둘 다 루프가 끝나면 lo == hi 가 답이고, 판정이 끝에서 참이라는 걸 범위 설정으로 보장한다(hi = sum 은 반드시 true, lo = 0 은 반드시 true). 다른 스타일(`while (lo <= hi)` + `ans` 변수 기록, AlgoMonster 노트의 boundary 방식)도 맞다 — 섞지만 않으면 된다. 꼬리: "실수(real) 답이면?" → 정수 대신 반복 횟수를 고정(예: 60번)한 bisection, 종료 조건을 `hi - lo < eps` 로 두면 큰 값에서 eps 가 ulp 보다 작아 안 끝날 수 있다.

</details>

<details><summary>Q7. 이 계산을 펌웨어 어디서 돌리나요? ISR 에서 불러도 되나요?</summary>

보통은 빌드 시간(테이블 생성 스크립트)이나 부팅/재설정 시 한 번이다: 슬롯 할당이 바뀔 때 버퍼 크기와 보드레이트를 다시 정한다. 함수 자체는 순수 함수(전역 상태 없음 — 하네스의 probe 카운터만 예외), 재귀 없음, 스택 몇 워드, 실행 시간 상한 = 64 × O(n) 이라 ISR 에서도 **안전하긴** 하지만, n 이 크면 그 상한이 ISR 지연 예산을 넘는다. 실무는 태스크에서 계산하고 결과(용량)만 원자적으로 바꿔 쓴다. DMA 버퍼는 이 최소값보다 **정적으로 크게** 잡아 두는 것이 일반적이다(정적 할당, malloc 금지) — 이 계산은 "얼마면 충분한가"의 근거 문서가 된다. 꼬리: SSD 펌웨어라면? → 정전 시 홀드업 커패시터 시간 안에 더티 페이지를 플러시하는 데 필요한 최소 쓰기 대역폭/최소 커패시턴스를 같은 방식으로 정한다 (판정 = 그 대역폭으로 플러시 시뮬레이션이 마감 안에 끝나나).

</details>

<details><summary>Q8. 판정이 단조가 아니면 어떻게 되나요? 단조인지 어떻게 테스트하나요?</summary>

이분 탐색은 F/T 경계가 하나라는 가정에 기대므로 단조가 아니면 아무 경계나(또는 엉뚱한 값을) 돌려준다. 예: "정확히 D 프레임" 조건, 보드레이트 분주기 오차가 허용 범위 안인지(분주기가 정수라 오차는 b 에 대해 들쭉날쭉) — 이런 건 이분 탐색이 아니라 전수 검사(후보가 적으면)가 맞다. 테스트: 작은 입력에서 판정 함수를 전 범위로 돌려 F…FT…T 인지 직접 확인한다. 이 레슨 하네스의 `monotonic violations` 검사가 그것이다. 꼬리: "단조성을 코드 리뷰에서 어떻게 설명?" → 보조정리 1 처럼 "c 의 해가 c+1 의 해" 라는 한 문장 증명을 주석에 남긴다.

</details>

## 9. 추가 연습

### 연습 1 — 최소 처리 속도 (first true)

ISR 이 배치 b[i] 개의 샘플을 큐에 넣었다. 처리 태스크는 틱마다 **한 배치에서** 최대 k 개 처리한다(틱 하나가 두 배치에 걸치지 않음). H 틱 안에 끝내는 최소 k 는? 예: `[3,6,7,11]`, H=8 → 4. (LeetCode 875 Koko Eating Bananas 와 같은 문제.)

<details><summary>답 보기</summary>

판정 `Σ ⌈b_i / k⌉ ≤ H` 는 k 가 커지면 각 항이 줄어드니 단조. 범위: k = max(b) 면 배치당 1틱 → n ≤ H 이면 true, k = 1 이 하한. H < n 이면 불가능(0 반환). 올림 나눗셈 `(b + k - 1) / k` 는 b + k − 1 이 넘치지 않게 uint64 로. `[30,11,23,4,20]` 은 H=5 → 30, H=6 → 23. 브루트포스(k 를 1부터 올리기)와 랜덤 5000개 대조. 펌웨어 해석: 최소 k 가 곧 "태스크가 틱마다 처리해야 할 최소 샘플 수" = 필요한 CPU 예산.

</details>

### 연습 2 — 최소 간격의 최대화 (last true)

정렬된 후보 시각 p[0..n−1] (예: 무선 송신이 허용된 시각) 중 k 개를 골라 **이웃 간격의 최솟값을 최대로** 하라. 예: `[1,2,4,8,9]`, k=3 → 3.

<details><summary>답 보기</summary>

판정 `can_place(g)` = 첫 후보를 고르고, 마지막으로 고른 것에서 g 이상 떨어진 첫 후보를 계속 고를 때 k 개를 채우나. g 가 작을수록 쉬우니 T…TF…F → **last true**, 위쪽 mid. 상한 = (p[n−1] − p[0]) / (k−1) (간격 k−1 개의 합이 전체 폭 이하). 그리디가 최적인 이유는 Part A 와 같은 "항상 더 왼쪽에 있다" 귀납 (가능한 한 일찍 고르면 다음 선택의 여지가 가장 넓다). 브루트포스 = 부분집합 전수(n ≤ 14), 4000개 대조. 펌웨어 해석: 송신 사이 최소 간격을 최대로 → 수신기·전력 회로의 회복 시간 확보 (이 문제는 "aggressive cows" 라는 이름으로도 알려져 있다).

</details>

### 연습 3 — 정수 세제곱근 (uint64)

`icbrt64(x)` = s³ ≤ x 인 가장 큰 s. `mid*mid*mid` 가 넘치지 않게 하라.

<details><summary>답 보기</summary>

2642245³ = 18446724184312856125 ≤ 2⁶⁴−1 < 2642246³ (파이썬으로 확인) → 답 ≤ 2642245. hi 를 거기로 자르면 mid³ < 2⁶⁴. 정답 확인은 넘침 없이 `s³ ≤ x` 와 `(s+1)² > x / (s+1)` (정수 나눗셈: t³ > x ⇔ t² > ⌊x/t⌋, t ≥ 1). 테스트: 0..2·10⁶ 전수, 랜덤 64비트 30만 개, 랜덤 k 의 k³ 과 k³−1, `UINT64_MAX` → 2642245. 반복 횟수 ≤ 22 (⌈log₂ 2642246⌉).

</details>

### 연습 4 — k 번째로 작은 쌍 차이 (값을 탐색, 개수로 판정)

타임스탬프 n 개에서 모든 쌍의 차이 n(n−1)/2 개 중 **k 번째로 작은 값**(지터 분포의 분위수)을 구하라. 차이를 전부 만들지 말 것. 예: `[1,6,1]`, k=3 → 5. (LeetCode 719 Find K-th Smallest Pair Distance.)

<details><summary>답 보기</summary>

정렬 후 판정 `count(d) = |{i<j : a[j] − a[i] ≤ d}| ≥ k`. d 가 커지면 개수가 늘어 단조 → first true. 개수는 투 포인터(ALGO-B09)로 O(n): j 를 늘리며 `a[j] − a[i] > d` 인 동안 i 를 올리고 `j − i` 를 더한다. 전체 O(n log n + n log W), W = 최대 차이. 답이 실제 어떤 쌍의 차이와 같은 이유: first true 값 d* 에서 count(d*) ≥ k > count(d*−1) 이므로 차이가 정확히 d* 인 쌍이 있다. 브루트포스(모든 차이를 정렬)와 n ≤ 64 랜덤 3000개 대조. 메모리 O(1) (정렬은 제자리) — 차이 배열 O(n²) 를 만들 수 없는 MCU 에서 의미가 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 16/16, 보고 없음
```

## 10. 핵심 정리

- "최소/최대 X 는?" → "X 로 되나?" 판정 함수를 만들고 **단조성**을 한 문장으로 증명(c 의 해는 c+1 의 해) → 답 공간 이분 탐색.
- 최소 프레임 용량: 그리디 판정(넣을 수 있으면 넣기)이 최소 프레임 수 — "그리디가 항상 더 멀리" 귀납, 크기 ≥ 0 이 전제. 범위 `[max, sum]`, 합은 uint64.
- 시간 O(n · log S), probe ≤ ⌈log₂ S⌉ ≤ 64, 추가 메모리 O(1), 재귀 없음, 최악 = 평균. DP 는 O(D n²).
- first true: `ok → hi = mid` / `else lo = mid + 1`, 아래쪽 mid. last true: `ok → lo = mid` / `else hi = mid − 1`, **위쪽 mid** (아니면 x=1 부터 무한 루프).
- isqrt: hi 를 65535 / 2³²−1 로 자르면 곱이 안 넘친다. 나눗셈 비교 `mid <= x / mid` 는 이중 안전장치지만 M0 에선 `__aeabi_uidiv` 호출.
- `(uint64_t)sqrt((double)x)` 는 k > 2²⁶ 의 k²−1 에서 1 크고 `UINT64_MAX` 에서 2³². 균일 랜덤 테스트로는 못 잡는다 → 경계(k²±1)를 일부러 넣는다.
- 펌웨어: 최소 DMA 버퍼 = 최소 프레임 용량, 최소 보드레이트 = `slot_bytes(b) ≥ need` 의 first true, 표준 표 = 인덱스 위 lower_bound. 오버헤드가 붙어도 단조이면 같은 루프.

## 참고자료

- AlgoMonster 노트 M02 §3 Monotonic Function, §6 Square Root Estimation, §9 Newspapers (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M02_binary_search.md`
- LeetCode 1011 Capacity To Ship Packages Within D Days · LeetCode 410 Split Array Largest Sum · LeetCode 69 Sqrt(x) · LeetCode 875 Koko Eating Bananas · LeetCode 719 Find K-th Smallest Pair Distance.
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 (부호 없는 정수 연산은 mod 2ᴺ, 넘침 아님), 6.5p5 (부호 있는 넘침 UB), 6.5.5p5 (0 나눗셈 UB), 6.3.1.4p1 (범위 밖 부동소수 → 정수 변환 UB). 초안 N1570: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- IEEE 754-2019 — sqrt 는 correctly rounded 연산 (5.4.1). 정확히 반올림돼도 floor 가 틀릴 수 있는 이유는 5. 풀이.
- Arm, *ARMv7-M Architecture Reference Manual* — UDIV/SDIV 와 CCR.DIV_0_TRP (0 나눗셈 시 결과 0 또는 UsageFault). Cortex-M0(ARMv6-M)에는 나눗셈 명령 없음 → `__aeabi_uidiv` (이 레슨에서 clang `-S` 로 확인).
- J. Bentley, *Programming Pearls* 2판, 4장·9장 — 이진 탐색의 불변식과 정확성 논증. J. Bloch, "Extra, Extra - Read All About It: Nearly All Binary Searches and Mergesorts are Broken" (Google Research Blog, 2006) — `(lo + hi) / 2` 넘침.
- 관련 레슨: ALGO-B02 (반개구간 이진 탐색·lower_bound), ALGO-B03 (보정 테이블 이진 탐색), ALGO-B09 (투 포인터, 연습 4), ALGO-M09 (그리디 증명), ALGO-M15 (비트 단위 isqrt·상수 나눗셈), C-B02 (정수 승격·고정 폭), ARM-B07 (곱셈·나눗셈 명령), NUM-B01 (ULP), COMM-B01 (UART 8N1 프레임), BM-M08 (DMA 엔진), COMM-M11 (DMA UART)
