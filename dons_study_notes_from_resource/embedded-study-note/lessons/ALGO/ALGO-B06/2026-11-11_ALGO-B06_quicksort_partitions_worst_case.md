---
id: ALGO-B06
time: 60분
updated: 2026-11-11
---
# ALGO-B06 퀵소트: Lomuto·Hoare 파티션과 최악 케이스

## 학습 목표

- Lomuto · Hoare · 3-way(Dijkstra) 파티션을 직접 짜고, 각각의 **사후조건**(postcondition)을 한 줄로 정확히 말한다. 특히 "Hoare 가 돌려주는 j 는 피벗의 최종 위치가 아니다" 를 예제로 보인다.
- 정렬된 입력 · 역순 · 전부 같은 값에서 O(n²) 비교와 **깊이 n** 재귀를 실제로 재현하고, 비교 횟수와 최대 재귀 깊이를 계측해 표로 읽는다.
- median-of-3 피벗 + "작은 쪽만 재귀, 큰 쪽은 루프" 로 깊이가 ⌊log2 n⌋ 이하임을 증명하고 측정으로 확인한다. 이 장치가 **스택**은 막지만 **시간**은 못 막는다는 것도 구분한다.
- 작은 스택에서 교과서 퀵소트가 실제로 죽는 모습(macOS SIGBUS/SIGSEGV)과 가드 없이 조용히 메모리를 덮는 모습을 보고, Cortex-M 태스크 스택 예산(프레임 32~40 B)으로 옮겨 말한다.

## 1. 문제

퀵소트는 면접에서 "짜 보세요" 보다 **"언제 느려지나요? 스택은요?"** 로 더 많이 나온다. 펌웨어 면접관이 듣고 싶은 답은 "평균 O(n log n)" 이 아니라 "정렬된 센서 램프가 들어오면 깊이 n 재귀로 2 KB 태스크 스택을 뚫는다, 그래서 이렇게 막는다" 다.

퀵소트(quicksort)의 뼈대는 하나다: 피벗(pivot)을 고르고, 구간을 "피벗보다 작은 쪽 / 큰 쪽" 으로 **파티션**(partition)한 뒤 양쪽을 재귀로 정렬한다. 합치는 단계가 없다. 차이는 전부 파티션에 있다 — 무엇을 보장하고(사후조건), 같은 값을 어떻게 다루고, 피벗이 어디에 남는가.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">파티션 도중의 불변식 (루프가 도는 동안 항상 참)</text>
  <g stroke="currentColor">
    <rect x="110" y="40" width="170" height="28" class="f-ok"/><rect x="280" y="40" width="140" height="28" class="f-warn"/><rect x="420" y="40" width="230" height="28" class="f-mut"/><rect x="650" y="40" width="40" height="28" class="f-acc"/>
    <rect x="110" y="120" width="190" height="28" class="f-ok"/><rect x="300" y="120" width="230" height="28" class="f-mut"/><rect x="530" y="120" width="160" height="28" class="f-warn"/>
    <rect x="110" y="200" width="130" height="28" class="f-ok"/><rect x="240" y="200" width="150" height="28" class="f-acc"/><rect x="390" y="200" width="160" height="28" class="f-mut"/><rect x="550" y="200" width="140" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="58">Lomuto</text><text x="10" y="138">Hoare</text><text x="10" y="218">3-way</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="59">≤ pivot</text><text x="320" y="59">&gt; pivot</text><text x="490" y="59">아직 안 봄</text><text x="656" y="59">piv</text>
    <text x="108" y="84">lo</text><text x="276" y="84">i</text><text x="416" y="84">j</text><text x="672" y="84">hi</text>
    <text x="150" y="139">≤ pivot</text><text x="370" y="139">아직 안 봄</text><text x="575" y="139">≥ pivot</text>
    <text x="108" y="164">lo</text><text x="296" y="164">i →</text><text x="510" y="164">← j</text><text x="672" y="164">hi</text>
    <text x="155" y="219">&lt; v</text><text x="295" y="219">== v</text><text x="430" y="219">아직 안 봄</text><text x="600" y="219">&gt; v</text>
    <text x="108" y="244">lo</text><text x="236" y="244">lt</text><text x="386" y="244">i</text><text x="540" y="244">gt</text><text x="672" y="244">hi</text>
    <text x="10" y="278">끝난 뒤 — Lomuto: [lo..p-1] ≤ a[p]=pivot &lt; [p+1..hi]   (p = 피벗의 최종 위치)</text>
    <text x="10" y="298">Hoare: lo ≤ j &lt; hi, [lo..j] ≤ pivot ≤ [j+1..hi]   (j 는 경계일 뿐, 피벗 위치 아님)</text>
    <text x="10" y="318">3-way: [lo..lt-1] &lt; v, [lt..gt] == v, [gt+1..hi] &gt; v   (같은 값 덩어리는 다시 안 봄)</text>
  </g>
</svg>
```

### Part A — 파티션 3종과 교과서 퀵소트

`lomuto_partition` (피벗 = 마지막 원소), `hoare_partition` (피벗 = 첫 원소의 **값**), `partition3` (Dijkstra 의 Dutch national flag, 피벗 = 첫 원소) 를 구현한다. 비교는 반드시 `less()` 로, 교환은 `swap_at()` 으로 해서 횟수가 세지게 한다. 재귀 껍데기(`qs_lomuto_rec` 등, 양쪽 다 재귀)는 주어진다. 3-way 는 Bentley–McIlroy 방식(같은 값을 양 끝에 모았다가 가운데로 옮김)도 있지만 이 레슨은 **Dijkstra 방식**(lt / i / gt 세 포인터, 한 번 훑기)을 쓴다.

### Part B — 최악 케이스 재현과 깊이 제한

`max_depth` = 실제로 파티션하는 재귀 프레임의 최대 중첩 수(루트 = 1). 테스트가 다음을 정확히 확인한다 (n = 1000): Lomuto 는 정렬 입력과 같은 값 입력 모두 비교 n(n−1)/2 = 499500, 깊이 n−1. Hoare(첫 원소)는 정렬 입력에서 깊이 n−1. 3-way 는 같은 값에서 깊이 1 · 비교 2(n−1) 이지만 역순에서는 역시 n(n−1)/2.

고침: `median3_to_lo` (a[lo], a[mid], a[hi] 의 중앙값을 a[lo] 로) 와 `qs_safe_rec` (median-of-3 → Hoare → 작은 쪽만 재귀, 큰 쪽은 `while` 로). 요구: **모든 입력에서** `max_depth ≤ ⌊log2 n⌋`.

### Part C — ADC 블록 요약 (스택 예산 안에서)

12비트 ADC 블록(최대 수백 샘플)을 받아 정렬한 뒤 **중앙값**(아래쪽 중앙값 `s[(n-1)/2]`)과 **절사 평균**(trimmed mean, 위·아래 `trim` 개씩 버린 평균)을 낸다. 센서 현실에서 정렬 입력(램프), 같은 값(고착 · 포화)은 "특이 케이스" 가 아니라 **매일 들어오는 입력**이다. `ilog2_floor(n)` (내장 함수 없이)도 구현해 깊이 상한을 계산한다.

## 2. Problem (English)

Implement in-place quicksort variants on `int` arrays (closed index range `[lo, hi]`), instrumented with comparison/swap counters and the maximum recursion depth.

```c
typedef struct { uint64_t cmp, swp; int max_depth; } qs_stats_t;

int  lomuto_partition(int *a, int lo, int hi, qs_stats_t *st); /* pivot a[hi]; returns final pivot index p   */
int  hoare_partition (int *a, int lo, int hi, qs_stats_t *st); /* pivot value a[lo]; returns j, lo <= j < hi,
                                                                  a[lo..j] <= pivot <= a[j+1..hi]            */
void partition3(int *a, int lo, int hi, int *lt, int *gt, qs_stats_t *st);  /* <v | ==v | >v               */
void quicksort_safe(int *a, int n, qs_stats_t *st);            /* max_depth <= floor(log2 n) for ANY input   */
int  adc_block_summary(int *s, int n, int trim, adc_summary_t *out);  /* median + trimmed mean, -1 on bad args */
```

Constraints: `0 <= n <= 10^6`, values span the full `int` range (no `a - b` tricks), no `malloc`, no recursion deeper than `floor(log2 n)` in `quicksort_safe`. Verify against `qsort` on random and adversarial inputs (sorted, reversed, all-equal, organ-pipe, few distinct values, `INT_MIN/INT_MAX` alternating).

```text
hoare_partition([5 3 8 4 9 1 6 2 7], 0, 8)  -> j = 3, array = [2 3 1 4 | 9 8 6 5 7]   (pivot 5 ends at index 7)
lomuto_partition([5 3 8 4 9 1 6 2 7], 0, 8) -> p = 6, array = [5 3 4 1 6 2 | 7 | 8 9]
partition3([2 0 2 1 2 0 2], 0, 6)           -> lt = 3, gt = 6, array = [0 1 0 | 2 2 2 2]
quicksort_lomuto(sorted 0..999)             -> cmp = 499500, max_depth = 999
quicksort_safe(sorted 0..99999)             -> max_depth <= 16
adc_block_summary(ramp 0..255, n=256, trim=16) -> median 127, trimmed_mean 127
```

Follow-ups: What is the worst case and which inputs trigger it? Does median-of-3 make the worst case impossible? How much stack does your sort use on a 2 KB RTOS task? Is it stable?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데: 파티션 3개, `median3_to_lo`, `qs_safe_rec`, `ilog2_floor`, `adc_block_summary`. 스텁 파티션은 "가운데 인덱스" 를 돌려줘서 재귀가 log n 깊이로 끝나고(멈추거나 터지지 않음) 배열은 정렬되지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:177  j == 3 && h[3] == 4
FAIL starter.c:234  bad[s] == 0
  (lomuto: 21515 bad)
…
FAIL starter.c:347  badr == 0
7/42 passed
```

(exit 1. 통과한 7개는 스텁과 우연히 맞는 것들 — 잘못된 인자에 −1, n ≤ 1 에서 깊이 0 등.)

## 4. 힌트

<details><summary>힌트 1 — Lomuto: "≤ 구역의 끝" 하나만 들고 간다</summary>

`i` = "≤ pivot 구역 바로 다음 칸". j 를 lo 부터 hi−1 까지 보며 `a[j] ≤ pivot` 이면 `a[i]` 와 바꾸고 i++. 끝나면 `a[i]` 와 `a[hi]`(피벗)를 바꾸고 i 를 돌려준다. `a[j] ≤ pivot` 을 비교 한 번으로: `!less(pivot, a[j])`.

</details>

<details><summary>힌트 2 — Hoare: 양쪽에서 걸어와 "잘못 선 쌍" 을 바꾼다</summary>

`i = lo-1, j = hi+1` 에서 시작, `do i++ while (a[i] < pivot)`, `do j-- while (pivot < a[j])`. 둘 다 멈췄을 때 `i >= j` 면 j 반환, 아니면 교환. 비교가 **엄격한 `<`** 라서 피벗과 같은 값에서도 멈춘다 — 그게 같은 값 입력을 가운데서 쪼개 주고, 배열 밖으로 달려나가지 않게 막는 보초(sentinel) 역할도 한다.

</details>

<details><summary>힌트 3 — 3-way: 안 본 구역 [i..gt] 를 줄여 간다</summary>

`a[i] < v` → `a[lt]` 와 바꾸고 lt++, i++. `a[i] > v` → `a[gt]` 와 바꾸고 gt-- (**i 는 그대로** — 뒤에서 온 값은 아직 안 봤다). 같으면 i++. `i > gt` 면 끝.

</details>

<details><summary>힌트 4 — 깊이 ⌊log2 n⌋ 의 비결은 피벗이 아니다</summary>

파티션 후 두 조각 중 **작은 쪽**만 재귀 호출하고, 큰 쪽은 `lo`/`hi` 를 바꿔 `while` 을 한 번 더 돈다. 재귀로 넘어가는 조각은 항상 현재 구간의 절반 이하 → 프레임을 하나 쌓을 때마다 크기가 반 이하. 원소 2개 이상일 때만 재귀하면 깊이 k 프레임의 크기는 2 이상 n/2^(k−1) 이하.

</details>

<details><summary>힌트 5 — median-of-3 는 정렬 세 칸 + 교환 하나</summary>

a[lo], a[mid], a[hi] 를 비교·교환 2~3번으로 오름차순으로 만든 뒤 `swap(lo, mid)` — 이제 a[lo] 가 중앙값이고 Hoare 가 그 값을 피벗으로 쓴다. `mid = lo + (hi - lo) / 2` (`(lo+hi)/2` 는 큰 인덱스에서 넘친다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 내용 (이 문제) |
|---|---|
| 1. 확인 질문 | n 범위? 값 범위(음수, INT_MIN)? 중복이 많나? 안정성 필요? 메모리/스택 예산? 최악 시간 보장이 필요한가(실시간)? |
| 2. 무식한 방법 | 삽입 정렬 O(n²) 시간 · O(1) 공간 (ALGO-B05). 작은 n 이면 이게 정답일 수도 있다고 먼저 말한다 |
| 3. 더 나은 생각 + 불변식 | 퀵소트: 파티션 불변식(그림 1) → 평균 O(n log n). Hoare 사후조건을 **말로** 먼저 고정 |
| 4. 코드 | 파티션 → 재귀. 바로 이어서 "작은 쪽만 재귀" 로 고친 버전을 제시 |
| 5. 소리 내어 말할 테스트 | n=0, 1, 2 · 전부 같은 값 · 정렬/역순 · INT_MIN/INT_MAX · 피벗이 최소/최대 |
| 6. 복잡도 | 시간 평균 O(n log n), 최악 O(n²) (median-of-3 도 킬러 입력 있음) · 추가 메모리 O(1) 힙 + **스택 O(log n)** (작은 쪽 재귀) · 불안정 |

### Part A — Lomuto: 피벗을 제자리에 놓는다

불변식: `a[lo..i-1] ≤ pivot`, `a[i..j-1] > pivot`, `a[j..hi-1]` 미확인, `a[hi] = pivot`. j 가 hi 에 닿으면 미확인 구역이 비고, `swap(i, hi)` 로 피벗이 "≤ 구역" 과 "> 구역" 사이에 들어간다 → **p = i 는 피벗의 최종 위치**. 그래서 재귀는 `[lo..p-1]`, `[p+1..hi]` — 피벗을 뺀다.

`[5 3 8 4 9 1 6 2 7]`, pivot = 7 (이 레슨 작업용 trace 프로그램의 실제 출력):

| j | a[j] | 동작 | 배열 | i |
|---|---|---|---|---|
| 0 | 5 | ≤7, swap(0,0) | 5 3 8 4 9 1 6 2 7 | 1 |
| 1 | 3 | ≤7, swap(1,1) | 5 3 8 4 9 1 6 2 7 | 2 |
| 2 | 8 | >7, 그대로 | 5 3 8 4 9 1 6 2 7 | 2 |
| 3 | 4 | ≤7, swap(2,3) | 5 3 **4 8** 9 1 6 2 7 | 3 |
| 4 | 9 | >7 | 5 3 4 8 9 1 6 2 7 | 3 |
| 5 | 1 | ≤7, swap(3,5) | 5 3 4 **1** 9 **8** 6 2 7 | 4 |
| 6 | 6 | ≤7, swap(4,6) | 5 3 4 1 **6** 8 **9** 2 7 | 5 |
| 7 | 2 | ≤7, swap(5,7) | 5 3 4 1 6 **2** 9 **8** 7 | 6 |
| 끝 | | swap(i=6, hi) | 5 3 4 1 6 2 **7** 8 9 | p = 6 |

비교는 정확히 hi − lo = n−1 번. 약점 두 가지가 바로 보인다: (1) 피벗이 최대/최소면 한쪽이 n−1 개 → 정렬·역순 입력에서 O(n²). (2) **같은 값이 전부 "≤" 쪽으로 간다** → 전부 같은 값이면 p = hi, 역시 O(n²). 교환도 많다 (무작위 n=10^4 에서 Lomuto 87,561 번 vs Hoare 30,887 번, 실측).

### Part A — Hoare: j 는 경계일 뿐

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 입력 [5 3 8 4 9 1 6 2 7] — 두 파티션이 남기는 것</text>
  <g fill="currentColor"><text x="10" y="62">Hoare</text><text x="10" y="172">Lomuto</text></g>
  <g fill="currentColor" font-size="12"><text x="10" y="80">pivot = 5</text><text x="10" y="190">pivot = 7</text></g>
  <g stroke="currentColor">
    <rect x="110" y="45" width="52" height="34" class="f-ok"/><rect x="162" y="45" width="52" height="34" class="f-ok"/><rect x="214" y="45" width="52" height="34" class="f-ok"/><rect x="266" y="45" width="52" height="34" class="f-ok"/><rect x="318" y="45" width="52" height="34" class="f-warn"/><rect x="370" y="45" width="52" height="34" class="f-warn"/><rect x="422" y="45" width="52" height="34" class="f-warn"/><rect x="474" y="45" width="52" height="34" class="f-acc"/><rect x="526" y="45" width="52" height="34" class="f-warn"/>
    <rect x="110" y="155" width="52" height="34" class="f-ok"/><rect x="162" y="155" width="52" height="34" class="f-ok"/><rect x="214" y="155" width="52" height="34" class="f-ok"/><rect x="266" y="155" width="52" height="34" class="f-ok"/><rect x="318" y="155" width="52" height="34" class="f-ok"/><rect x="370" y="155" width="52" height="34" class="f-ok"/><rect x="422" y="155" width="52" height="34" class="f-acc"/><rect x="474" y="155" width="52" height="34" class="f-warn"/><rect x="526" y="155" width="52" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="14">
    <text x="136" y="67">2</text><text x="188" y="67">3</text><text x="240" y="67">1</text><text x="292" y="67">4</text><text x="344" y="67">9</text><text x="396" y="67">8</text><text x="448" y="67">6</text><text x="500" y="67">5</text><text x="552" y="67">7</text>
    <text x="136" y="177">5</text><text x="188" y="177">3</text><text x="240" y="177">4</text><text x="292" y="177">1</text><text x="344" y="177">6</text><text x="396" y="177">2</text><text x="448" y="177">7</text><text x="500" y="177">8</text><text x="552" y="177">9</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="136" y="96">0</text><text x="188" y="96">1</text><text x="240" y="96">2</text><text x="292" y="96">3</text><text x="344" y="96">4</text><text x="396" y="96">5</text><text x="448" y="96">6</text><text x="500" y="96">7</text><text x="552" y="96">8</text>
    <text x="292" y="116">j = 3 (a[3]=4)</text><text x="500" y="116">피벗 5 는 여기</text><text x="448" y="210">p = 6 = 피벗의 최종 자리</text>
  </g>
  <line x1="318" y1="38" x2="318" y2="86" stroke="currentColor" stroke-width="3"/>
  <g fill="currentColor" font-size="12">
    <text x="600" y="60">[0..3] ≤ 5</text><text x="600" y="78">[4..8] ≥ 5</text>
    <text x="10" y="240">Hoare 재귀는 (lo, j), (j+1, hi). (lo, j-1) 로 쓰면 매번 a[j] 가 빠져 결과가 1 2 3 4 6 7 8 5 9 (실측).</text>
  </g>
</svg>
```

Hoare 의 불변식: `a[lo..i-1] ≤ pivot`, `a[j+1..hi] ≥ pivot`. 두 포인터가 멈추는 곳은 "잘못 선 원소" 뿐이고, 교환하면 두 원소 모두 제 편으로 간다. 실제 trace:

| 단계 | i 멈춘 곳 | j 멈춘 곳 | 동작 | 배열 |
|---|---|---|---|---|
| 1 | 0 (5 ≥ 5) | 7 (2 ≤ 5) | swap | **2** 3 8 4 9 1 6 **5** 7 |
| 2 | 2 (8) | 5 (1) | swap | 2 3 **1** 4 9 **8** 6 5 7 |
| 3 | 4 (9) | 3 (4) | i ≥ j → return 3 | 2 3 1 4 9 8 6 5 7 |

왜 `lo ≤ j < hi` 인가 (= 양쪽 모두 비지 않음 = 무한 재귀 없음): 첫 바퀴에서 i 는 반드시 lo 에서 멈춘다(a[lo] = pivot 이라 `a[lo] < pivot` 이 거짓). j 가 lo 까지 내려오면 바로 `i ≥ j` 로 lo 를 돌려준다 (< hi). j 가 lo 보다 위에서 멈추면 교환이 일어나고, 그 뒤 j 는 적어도 한 칸 더 내려가므로 hi 보다 작다. 또 교환 뒤에는 `a[lo..i] ≤ pivot`, `a[j..hi] ≥ pivot` 이 i, j 바깥의 **보초** 역할을 해서 두 스캔이 배열 밖으로 나가지 않는다. 피벗을 **마지막 원소**로 바꾸면 이 논증이 깨진다: `[1, 2]` 에서 j = hi = 1 을 돌려주고 같은 구간을 끝없이 재귀한다 (7. 실수 2, 실제로 SIGSEGV).

Hoare 의 장점: 교환이 적고(같은 쪽에 있어야 할 원소는 건드리지 않음), **같은 값 입력에서 양쪽이 번갈아 멈춰 가운데서 만난다** → 전부 같은 값이어도 깊이 ~log2 n (n=1000 에서 10, 실측).

### Part A — 3-way: 같은 값 덩어리를 한 번에 치운다

`[2 0 2 1 2 0 2]`, v = 2 (실제 trace, 상태 = lt i gt):

| a[i] | 동작 | 배열 | lt i gt |
|---|---|---|---|
| — | 시작 | 2 0 2 1 2 0 2 | 0 1 6 |
| 0 | < v: swap(lt,i), lt++, i++ | 0 2 2 1 2 0 2 | 1 2 6 |
| 2 | == v: i++ | 0 2 2 1 2 0 2 | 1 3 6 |
| 1 | < v | 0 1 2 2 2 0 2 | 2 4 6 |
| 2 | == v | 0 1 2 2 2 0 2 | 2 5 6 |
| 0 | < v | 0 1 0 2 2 2 2 | 3 6 6 |
| 2 | == v: i++ → i > gt, 끝 | 0 1 0 2 2 2 2 | 3 7 6 |

재귀는 `[lo..lt-1]` 과 `[gt+1..hi]` 만 — 같은 값 `[lt..gt]` 는 다시 안 본다. 파티션 한 번마다 값 한 종류가 통째로 끝나므로, 서로 다른 값이 k 종류뿐이면 깊이는 n 이 아니라 k 이하로 묶인다 (값 0..9, n=10^4 에서 깊이 6, 실측). 대가: 서로 다른 키가 많을 때는 원소마다 비교가 최대 2번이고 교환도 많다(무작위 n=10^4: 비교 217,919 · 교환 146,436 vs Hoare 206,956 · 30,887).

### Part B — 최악 케이스: 실측 표

`demo_bench.c` 는 solution.c 를 그대로 포함해 입력 모양별로 비교 횟수 · 최대 깊이 · 시간을 잰다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

이 맥 (Apple M6, Apple clang 21.0.0, `-O2`), 측정 직전 load average 8.95 (다른 작업이 같이 돎). 시간 = 워밍업 1회 + 5회 중앙값, 카운터 증가가 들어간 계측 코드라 **절대 ms 보다 비율**을 볼 것. 같은 표를 다시 돌리면 ms 가 ±20% 정도 흔들렸고 비교 횟수·깊이는 당연히 똑같았다. n = 10,000 (실제 출력, 열 정리):

| 입력 | Lomuto(끝) 비교 · 깊이 · ms | Hoare(첫) 비교 · 깊이 · ms | 3-way(첫) 비교 · 깊이 · ms | safe(m3+작은쪽) 비교 · 깊이 · ms | libc qsort ms |
|---|---|---|---|---|---|
| 무작위 | 158,547 · 31 · 0.296 | 206,956 · 34 · 0.300 | 217,919 · 28 · 0.319 | 194,583 · **9** · 0.331 | 0.370 |
| 값 0..9 | **5,031,644** · 1036 · 1.961 | 159,983 · 22 · 0.101 | 54,733 · 6 · 0.049 | 178,745 · 11 · 0.129 | 0.043 |
| 정렬 | **49,995,000 · 9999** · 22.27 | **50,014,998 · 9999** · 11.24 | 2,079,700 · 269 · 0.509 | 171,645 · 12 · 0.066 | 0.018 |
| 역순 | **49,995,000 · 9999** · 14.40 | **50,019,998 · 9999** · 12.85 | **49,995,000 · 9999** · 22.59 | 288,201 · 12 · 0.092 | 0.103 |
| 전부 같은 값 | **49,995,000 · 9999** · 19.65 | 149,214 · 14 · 0.064 | 19,998 · **1** · 0.004 | 169,212 · 13 · 0.080 | 0.009 |
| organ-pipe | 6,019,538 · 2060 · 3.466 | 483,598 · 84 · 0.173 | 1,867,298 · 822 · 1.194 | 427,078 · 8 · 0.176 | 0.192 |
| 톱니 i%16 | 3,204,360 · 639 · 1.259 | 179,154 · 25 · 0.099 | 92,468 · 8 · 0.034 | 182,476 · 11 · 0.110 | 0.040 |
| INT_MIN/MAX 교대 | 25,004,999 · 5000 · 14.49 | 151,105 · 18 · 0.095 | 29,996 · 2 · 0.007 | 171,831 · 12 · 0.098 | 0.018 |

읽을 것:

- **정확한 공식이 맞는다.** Lomuto 정렬 입력: 크기 k 파티션마다 k−1 비교 → Σ(k−1) = n(n−1)/2 = 49,995,000. Hoare 정렬 입력: i 가 1번, j 가 k번 → k+1 비교 → Σ_{k=2..n}(k+1) = 50,014,998. 둘 다 표와 한 자리까지 같다.
- 무작위 입력의 Lomuto 158,547 은 이론 평균 2(n+1)H_n − 4n ≈ 155,771 (H_n ≈ ln n + 0.5772) 보다 1.8% 많다 — 한 번의 표본이니 맞는 수준.
- **3-way 는 중복을 고칠 뿐 순서는 못 고친다.** 역순에서 피벗(첫 원소)이 최댓값이라 n(n−1)/2. 정렬 입력에서 2.08M 으로 덜 나쁜 건 우연이다: `> v` 원소를 뒤로 보내는 교환이 남은 구간을 뒤섞기 때문.
- **Lomuto 는 중복에 약하다**: 값이 10종류뿐인 입력에서 5.0M 비교, 깊이 1036 — ADC 샘플(12비트에 노이즈 몇 LSB)이 정확히 이 모양이다.
- libc qsort 열은 참고용이다 — 계측이 없고 비교를 함수 포인터로 부르는 코드라 공정한 비교가 아니다. 정렬·같은 값에서 특히 빠른 것은, macOS libc 가 FreeBSD 계열 Bentley–McIlroy 구현이고 교환이 없던 구간은 삽입 정렬로 바꾸는 휴리스틱이 있다고 알려져 있기 때문으로 추정한다 (소스는 이 레슨에서 확인 안 함).

### Part B — 고침: median-of-3 + 작은 쪽만 재귀

```c
static void qs_safe_rec(int *a, int lo, int hi, qs_stats_t *st, int depth) {
    enter(st, depth);
    while (lo < hi) {
        median3_to_lo(a, lo, hi, st);
        int j = hoare_partition(a, lo, hi, st);
        if (j - lo < hi - j) { if (lo < j)     qs_safe_rec(a, lo, j, st, depth + 1);     lo = j + 1; }
        else                 { if (j + 1 < hi) qs_safe_rec(a, j + 1, hi, st, depth + 1); hi = j; }
    }
}
```

두 장치는 **다른 문제**를 푼다:

| 장치 | 막는 것 | 못 막는 것 |
|---|---|---|
| median-of-3 피벗 | 정렬 · 역순 · 같은 값(Hoare 와 함께) 같은 **흔한** 나쁜 입력의 O(n²) 시간 | 일부러 만든 킬러 입력 (연습 4: n=4000 에서 비교 4,023,992 ≈ n²/4) |
| 작은 쪽만 재귀 | **어떤 입력에서도** 스택 깊이 ⌊log2 n⌋ | 시간 O(n²) (킬러 입력에서도 깊이는 2, 시간은 n²/4) |

**깊이 증명.** 크기를 "구간 원소 수" 라 하자. 깊이 k 프레임이 재귀 호출을 할 때 넘기는 조각은 (a) 그 프레임의 현재 구간의 절반 이하 — 두 조각 크기 합이 현재 크기 m 이고 작은 쪽을 고르므로 ≤ ⌊m/2⌋, (b) 현재 구간은 루프를 돌며 줄어들기만 하므로 프레임 시작 크기 이하. 그러니 깊이 k+1 프레임의 크기 ≤ (깊이 k 프레임 크기)/2, 귀납으로 깊이 k 프레임 크기 ≤ n / 2^(k−1). 또 원소 2개 이상일 때만 재귀하므로 크기 ≥ 2. 합치면 2 ≤ n/2^(k−1) → k ≤ log2 n → k ≤ ⌊log2 n⌋. (Hoare 는 lo ≤ j < hi 라 두 조각이 모두 비지 않음 — 루프가 반드시 줄어든다.)

측정 (demo_bench 두 번째 표, 실제 출력) — n = 2^k − 1 에서 safe 의 최대 깊이:

| n | 상한 ⌊log2 n⌋ | 무작위 | 정렬 | 같은 값 | organ-pipe |
|---|---|---|---|---|---|
| 1,023 | 9 | 7 | 9 | 9 | 6 |
| 4,095 | 11 | 8 | 11 | 11 | 7 |
| 16,383 | 13 | 9 | 13 | 13 | 8 |
| 65,535 | 15 | 12 | 15 | 15 | 10 |
| 262,143 | 17 | 12 | 17 | 17 | 10 |
| 1,048,575 | 19 | 14 | 19 | 19 | 12 |

정렬·같은 값 입력은 median-of-3 가 정확히 반을 갈라서 **상한에 딱 붙는다** — 상한이 느슨하지 않다는 증거. solution.c 는 8가지 입력 × n = 2 부터 1.5배씩 2048 까지, 그리고 n = 100,000 (상한 16)에서 이 부등식을 검사한다.

이 "손으로 하는 꼬리 호출 제거"(tail-call elimination)가 왜 필요한가? 컴파일러도 꼬리 호출을 루프로 바꾸지만 **항상 같은 쪽(두 번째 호출)** 만이다. arm64 `-O2` 어셈블리에서 `qs_hoare_rec` 의 `bl _qs_hoare_rec` 는 하나뿐이고 두 번째 호출은 `b` (점프) 였다(직접 확인). 그래서 Hoare 정렬 입력(깊은 쪽이 오른쪽 = 꼬리)은 `-O2` 에서 우연히 스택이 안 자라고, Lomuto 정렬 입력(깊은 쪽이 왼쪽)은 깊이만큼 자란다. `-O0` 에서는 둘 다 깊이만큼 자란다 — 다음 절의 실측.

### Part B — 실제로 터뜨려 보기: `demo_stack.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_stack.c -o demo_stack && ./demo_stack
```

1) **스택 칠하기**(stack painting): 4 MiB 스택을 0xA5 로 칠한 pthread 에서 정렬하고, 칠이 지워진 깊이를 잰다 — FreeRTOS 의 `uxTaskGetStackHighWaterMark()` 와 같은 원리(FreeRTOS 도 태스크 스택을 0xA5 로 채운다). 실제 출력:

```text
sort                    n  depth    stack B
lomuto (sorted)         1      0        272
lomuto (sorted)       100     99       6416
lomuto (sorted)      1000    999      64016
lomuto (sorted)     10000   9999     640016
hoare  (sorted)     10000   9999        272
safe   (sorted)      1000      9        800
safe   (sorted)     10000     12       1040
safe   (random)     10000      9        800
safe   (sorted)   1000000     19       1600
safe   (random)   1000000     15       1280
safe   (equal)    1000000     19       1600
```

단계당 바이트는 행 사이의 **기울기**로 읽는다(절편은 스레드 시작 등이 섞여 해석하지 않는다): Lomuto (640016 − 64016) / 9000 = **64 B**, safe (1600 − 1040) / (19 − 12) = **80 B**. arm64 `-O2` 어셈블리의 prologue 와 정확히 같다 — `qs_lomuto_rec` 는 `stp` 4개(레지스터 8개 = 64 B), `qs_safe_rec` 는 `stp` 5개(80 B). safe 는 프레임이 더 크지만 깊이가 12~19 라 총 1.6 KB 이하다.

`hoare (sorted)` 의 272 B 는 계측 깊이가 9999 인데도 스택이 안 자란 것 — 위에서 말한 컴파일러 꼬리 호출 제거. 같은 파일을 `-O0` 로 빌드하면 그 줄이 **480,192 B** 가 되고, Lomuto 는 단계당 48 B (480204 − 48204) / 9000 로 오히려 `-O2` 보다 작다 (실측). 최적화 수준에 따라 살고 죽는 코드는 펌웨어에 넣으면 안 된다.

2) **가드 페이지가 있는 64 KiB pthread** (자식 프로세스에서 실행, 실제 출력):

```text
  lomuto sorted    n=    500 stack= 64 KiB: ok, depth=499, sorted=1
  lomuto sorted    n=   1000 stack= 64 KiB: ok, depth=999, sorted=1
  lomuto sorted    n=   1500 stack= 64 KiB: CRASH signal 10 (SIGBUS)
  lomuto sorted    n=   2000 stack= 64 KiB: CRASH signal 10 (SIGBUS)
  lomuto sorted    n=   4000 stack= 64 KiB: CRASH signal 10 (SIGBUS)
  safe sorted      n=1000000 stack= 16 KiB: ok, depth=19, sorted=1
  safe random      n=1000000 stack= 16 KiB: ok, depth=14, sorted=1
```

n=1000 은 64,016 B 로 64 KiB(65,536 B) 안에 아슬아슬하게 들어가 살았고, 1500 부터 가드 페이지를 건드려 SIGBUS. safe 는 **macOS 최소 pthread 스택 16 KiB** 에서 10^6 개를 정렬한다.

3) **메인 스레드 + `ulimit -s`**: 서브셸에서 줄인 한도가 새로 실행한 프로세스의 메인 스레드에 실제로 적용됐다 (실제 출력, zsh 의 `$?`):

```text
$ (ulimit -s 1024; ./demo_stack main 16300; echo "exit=$?")
main thread, RLIMIT_STACK soft = 1024 KiB, lomuto sorted n=16300 ...
survived: depth=16299
exit=0
$ (ulimit -s 1024; ./demo_stack main 16500; echo "exit=$?")
main thread, RLIMIT_STACK soft = 1024 KiB, lomuto sorted n=16500 ...
exit=139
```

139 = 128 + 11 (SIGSEGV). 1024 KiB / 64 B = 16,384 단계 — 경계가 16,300 과 16,500 사이로 계산과 맞는다 (기본 한도 8176 KiB 에서는 n=100,000 이 살고 200,000 이 139 로 죽었다). 이 맥에서 메인 스레드 넘침은 SIGSEGV, pthread 가드 페이지 넘침은 SIGBUS(signal 10, 셸에서는 138)로 보였다.

4) **가드 없는 스택** — `[이웃 64 KiB][스택 64 KiB]` 를 한 번에 mmap 하고 위쪽을 스택으로 준다:

```text
  lomuto sorted n=1500, 64 KiB stack without guard: finished, sorted=1, depth=1499,
  neighbour bytes changed: 30472 of 65536 (overflowed 30480 B below the stack)
```

**아무 에러 없이 끝나고 결과도 정렬돼 있다.** 그 사이 이웃 30 KB 가 덮였다. MPU 없는 Cortex-M 에서 태스크 스택이 넘치면 정확히 이렇다: 아래에 있던 다른 태스크의 TCB · 큐 · `.bss` 변수가 망가지고, 증상은 몇 초 뒤 엉뚱한 곳에서 나온다.

### Part C — ADC 블록 요약과 Cortex-M 스택 예산

```c
if (!s || !out || n < 1 || trim < 0 || trim >= n - trim) return -1;   /* 2*trim >= n 을 넘침 없이 */
quicksort_safe(s, n, &st);
int64_t sum = 0;                                   /* 4095 * n 은 16비트 int MCU 에서 바로 넘친다 */
for (int i = trim; i < n - trim; i++) sum += s[i];
out->median = s[(n - 1) / 2];
out->trimmed_mean = (int)(sum / (n - 2 * trim));   /* C11 6.5.5: 정수 나눗셈은 0 쪽으로 버림 */
```

테스트 입력이 그대로 센서 상황이다: 램프 0..255 (정렬 입력 — 중앙값 127, 절사 평균 28560/224 = 127.5 → 127), 고착 2048 × 256 (같은 값), 998..1002 노이즈에 4095/0 스파이크 4개 (trim 4 로 제거), 무작위 블록 500개를 qsort 기준과 대조, 그리고 모든 경우 `max_depth ≤ ⌊log2 n⌋`.

이제 펌웨어 숫자로 옮긴다. `demo_m4.c` (계측 없는 같은 알고리즘, `int16_t` 샘플)를 Cortex-M4 로 컴파일:

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c -fstack-usage demo_m4.c -o demo_m4.o && cat demo_m4.su
```

```text
demo_m4.c:13:qs_lomuto	32	static
demo_m4.c:36:qs_safe	40	static
```

어셈블리 prologue 도 일치한다: `qs_lomuto` = `push.w {r4-r9, lr}` (28 B) + `sub sp, #4`, `qs_safe` = `push.w {r4-r11, lr}` (36 B) + `sub sp, #4`. `bl` 재귀 호출은 `qs_lomuto` 1개, `qs_safe` 2개 — 교과서 Lomuto 의 두 번째 호출은 여기서도 루프로 바뀌어 있었다.

| 2 KB 태스크 스택에서 | 교과서 Lomuto (32 B/단계) | safe (40 B/단계) |
|---|---|---|
| 정렬된 4096 샘플의 최악 깊이 | 4095 단계 ≈ 128 KB → **즉시 넘침** | ≤ 12 단계 = 480 B |
| 2 KB 로 버티는 최대 깊이 (다른 사용 무시) | 64 프레임 → 정렬된 블록 **60여 개**면 끝 | 51 프레임 → 32비트 n 의 최대 깊이 31 도 1,240 B |

여기에 인터럽트가 태스크 스택(PSP)에 쌓는 예외 프레임(기본 8워드 = 32 B, FPU 확장 프레임 26워드 = 104 B)과 ISR 중첩, 다른 호출 체인이 더해진다 (RTOS-M08). 대응 수단: FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` (1 = 문맥 전환 때 SP 검사, 2 = 스택 끝 패턴 검사), high-water mark, MPU 가드 영역(넘치면 MemManage fault, ARCH-M15), Cortex-M33(ARMv8-M) 의 `PSPLIM`/`MSPLIM` 스택 한계 레지스터 (RTOS-M09). 하지만 가장 좋은 대응은 **깊이가 입력과 무관하게 정해지는 코드**다 — MISRA C:2012 Rule 17.2 가 재귀 자체를 금지하는 이유(C-A10).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 42/42 passed, 0.27 s
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 42/42, 보고 없음
```

테스트 구성: 파티션 사후조건 (손 예제 3개 + 무작위 3000개 × 3종, 다중집합 보존 포함) · 값 {0,1,2,3} 길이 0~7 **전수** 21,845개 × 4종 · 무작위 2000개 (n ≤ 600, 전 범위와 0..9, INT_MIN/MAX 주입) · 적대적 8종 n=1000 · 최악 케이스 정확한 횟수 6개 · median-of-3 27가지 · 깊이 상한 · `ilog2_floor` 1..2^20 전수 · ADC 블록 named + 무작위 500개.

## 7. 자주 하는 실수

**1. Hoare 의 j 를 피벗 위치로 착각한다.**

```c
qs(a, lo, j - 1); qs(a, j + 1, hi);     /* 틀림: a[j] 는 피벗이 아니다 */
```

실제로 돌리면 무작위 배열 500개 중 **488개**가 틀리고, `[5 3 8 4 9 1 6 2 7]` 은 `1 2 3 4 6 7 8 5 9` 가 된다 — 첫 단계의 a[3]=4 는 우연히 제자리였지만, 오른쪽 재귀의 각 단계에서 빠진 a[j] 때문에 5 가 제자리를 못 찾았다. → `(lo, j)` 와 `(j+1, hi)`.

**2. Hoare 에 마지막 원소를 피벗으로 쓴다.** `[1, 2]` 에서 j = hi 를 돌려주고 같은 구간을 무한 재귀 → 자식 프로세스에서 실제로 SIGSEGV(11). → Hoare 는 첫 원소 값(또는 median-of-3 를 a[lo] 로 옮긴 값). 마지막 원소를 피벗으로 쓰려면 반환값을 i 로 바꾸고 `(lo, i-1)`/`(i, hi)` 로 쪼개는 대칭 규약을 써야 한다 (첫 바퀴에 j 가 hi 에서 멈추므로 lo < i ≤ hi).

**3. 인덱스를 `size_t` 로 쓰고 `p - 1` 을 한다.**

```c
size_t p = lomuto(a, lo, hi);  qs(a, lo, p - 1);   /* p == 0 이면 SIZE_MAX */
```

`[3, 2, 1]` 에서 p = 0 → `hi = SIZE_MAX` → `lo >= hi` 검사가 통과하지 않고 `a[SIZE_MAX]` 를 읽어 실제로 SIGSEGV. 부호 없는 산술 자체는 UB 가 아니라 정의된 순환(C11 6.2.5p9)이라 컴파일러가 경고도 안 한다. → 부호 있는 인덱스(`int`, `ptrdiff_t`)로 닫힌 구간을 쓰거나, 반열린 구간 `[lo, hi)` + `size_t` 로 일관되게.

**4. qsort 비교 함수를 `return a - b;` 로 쓴다.** `INT_MIN - 1` 같은 부호 있는 오버플로는 **UB**(C11 6.5p5). 실제로 무작위 전 범위 배열 500개 중 431개가 틀리게 정렬됐고 `{INT_MIN, 1, INT_MAX, -1}` 은 `1 2147483647 -2147483648 -1` 이 됐다. → `(a > b) - (a < b)`.

**5. "작은 쪽" 비교를 뒤집는다 (큰 쪽을 재귀).** 흔한 입력에서는 티가 안 난다 — 정렬 n=1000 에서 깊이 11, 무작위 17 (실측; 올바른 버전은 9, 8). 그런데 킬러 입력(연습 4, n=4000)에서 깊이가 2에서 **2001** 로 뛴다(실측). 상한은 "보통 괜찮다" 가 아니라 "항상" 이어야 한다.

**6. 컴파일러의 꼬리 호출 최적화를 믿는다.** `-O2` 에서 Hoare 정렬 입력 10^4 는 272 B, `-O0` 에서 480,192 B (실측). 디버그 빌드에서만 터지는 버그가 된다. 그리고 컴파일러는 항상 **두 번째** 호출만 바꾼다. → 작은 쪽 재귀를 코드로 명시.

**7. 중복 많은 데이터에 Lomuto.** 값 10종류 n=10^4 에서 비교 5.0M, 깊이 1036 (실측). ADC/온도처럼 값의 종류가 적은 데이터는 3-way 나 Hoare, 아예 계수 정렬(ALGO-B08)이 맞다.

## 8. 면접 Q&A

<details><summary>Q1. Lomuto 와 Hoare 의 차이를 사후조건으로 설명해 보세요.</summary>

Lomuto 는 `[lo..p-1] ≤ a[p] = pivot < [p+1..hi]` — p 가 피벗의 최종 자리라 재귀에서 p 를 뺀다. Hoare 는 `lo ≤ j < hi`, `[lo..j] ≤ pivot ≤ [j+1..hi]` — 경계만 보장하고 피벗 값은 어느 쪽에든 있을 수 있어서 j 를 포함해 재귀한다. 실무 차이: Hoare 가 교환이 약 1/3 (무작위 10^4: 30,887 vs 87,561, 실측)이고 같은 값 입력에서 균형 있게 쪼갠다. Lomuto 는 짧고 증명이 쉬워서 교과서(CLRS 7장)에 쓰인다. 꼬리: "Hoare 스캔에서 `<` 대신 `<=` 를 쓰면?" → 같은 값에서 멈추지 않아 i 가 구간 밖으로 달려나갈 수 있다(피벗이 최댓값일 때). 엄격한 비교가 보초를 만든다.

</details>

<details><summary>Q2. 최악 O(n²) 은 언제 나오고, median-of-3 로 해결되나요?</summary>

피벗이 매번 최소/최대 근처이면 한쪽 조각이 n−1 → Σ(k−1) = n(n−1)/2. 첫/끝 원소 피벗은 정렬·역순 입력에서, Lomuto 는 전부 같은 값에서도 그렇다(표: 49,995,000). median-of-3 는 흔한 경우(정렬/역순)를 고치지만 **보장은 아니다**: 피벗 선택 규칙이 결정적이면 McIlroy(1999)의 적대적 비교 함수로 그 구현 전용 킬러 입력을 만들 수 있다 — 연습 4 에서 이 레슨의 median-of-3 퀵소트를 n=4000 에서 4,023,992 비교(≈ n²/4)로 만들었다. 무작위 피벗은 "기대" O(n log n) 을 주지만 최악은 여전히 확률적으로 가능하다. 최악 보장이 필요하면 인트로소트(Musser 1997: 깊이 예산 2⌊log2 n⌋ 를 넘으면 힙정렬)나 힙정렬/머지소트. libstdc++ 의 `std::sort` 가 인트로소트이고, C++11 부터 표준이 `std::sort` 의 비교 횟수를 최악 O(n log n) 으로 요구한다.

</details>

<details><summary>Q3. 재귀 깊이를 O(log n) 으로 묶는 방법과 증명은?</summary>

작은 쪽만 재귀, 큰 쪽은 루프. 재귀로 넘기는 조각이 항상 현재 구간의 절반 이하이므로 깊이 k 프레임의 크기 ≤ n/2^(k−1), 원소 2개 이상일 때만 재귀하면 k ≤ ⌊log2 n⌋. 실측: n = 2^20−1 에서 정렬·같은 값 입력이 정확히 19 (상한과 일치), 킬러 입력에서도 2. 꼬리: "시간도 log 로 묶이나?" → 아니다. 깊이 2 인 채로 n²/4 비교가 가능하다(연습 4). 스택(공간)과 시간은 따로 막아야 한다. 꼬리 2: "재귀를 아예 없애려면?" → (lo, hi) 쌍을 담는 명시적 스택 배열. 작은 쪽을 나중에 처리하도록 큰 쪽을 먼저 push 하면 배열 크기 ⌈log2 n⌉ 개로 충분 — 32비트 n 이면 `int stk[32][2]` 고정 크기 (ALGO-B11).

</details>

<details><summary>Q4. 중복이 많은 데이터에는 어떻게 하나요? 3-way 는 공짜인가요?</summary>

같은 값이 많으면 3-way 로 `[lt..gt]` 덩어리를 한 번에 확정한다: 전부 같은 값 n=10^4 에서 비교 19,998, 깊이 1, 교환 0 (실측). 공짜는 아니다 — 서로 다른 키가 많으면 원소당 비교 최대 2회 + 교환이 많아(무작위 146,436 vs Hoare 30,887) 오히려 느릴 수 있다. Bentley–McIlroy 3-way 는 같은 값을 양 끝으로 모아 두었다가 마지막에 가운데로 옮겨서, 같은 값이 적을 때의 비용을 줄인다. Hoare 는 3-way 가 아니어도 같은 값을 가운데서 쪼개 O(n log n) 을 유지한다(같은 값 10^4: 149,214 비교). 값의 종류가 아주 적고 범위가 작으면(12비트 ADC) 비교 정렬을 버리고 계수 정렬 O(n + 4096) (ALGO-B08).

</details>

<details><summary>Q5. 퀵소트는 안정 정렬인가요? 펌웨어에서 언제 문제가 되나요?</summary>

아니다. 파티션의 먼 거리 교환이 같은 키의 상대 순서를 바꾼다. 예: CAN 프레임을 우선순위(ID)로 정렬하는데 같은 ID 안에서는 도착 순서를 지켜야 하면 깨진다. 해결: (키, 도착 순번) 쌍으로 비교해 키를 유일하게 만들거나, 안정 정렬(머지소트 ALGO-B07 — 대신 O(n) 버퍼, 또는 작은 n 이면 삽입 정렬 ALGO-B05). 꼬리: "qsort 는 안정한가?" → C 표준은 안정성을 보장하지 않는다(C11 7.22.5.2: 같다고 비교되는 원소의 순서는 미지정).

</details>

<details><summary>Q6. Cortex-M 에서 2 KB 태스크로 센서 블록을 정렬해야 합니다. 무엇을 쓰겠어요?</summary>

먼저 "정렬이 정말 필요한가" — 중앙값만 필요하면 quickselect (연습 1, 재귀 없음, 기대 O(n)), 실시간 스트림 중앙값이면 두 힙(ALGO-M11), 3·5·9개 고정 창이면 정렬 네트워크(분기 없는 비교·교환 고정 개수 → WCET 가 상수). 전체 정렬이 필요하면: 12비트 샘플은 계수 정렬, 일반 키는 깊이 제한 퀵소트(작은 쪽 재귀, 이 레슨 safe: Cortex-M4 `-O2` 40 B/단계 → 4096 샘플이 480 B) 또는 **힙정렬**(재귀 없음, 추가 메모리 O(1), 최악 O(n log n) — 시간 상한이 필요한 제어 루프용, DS-B12). 그리고 `-fstack-usage` + high-water mark 로 실측해 예산표(RTOS-M08)에 적는다. 꼬리: "newlib 의 qsort 를 그냥 쓰면?" → 구현의 재귀 깊이·최악 시간을 문서/소스로 확인하기 전에는 실시간 경로에 넣지 않는다.

</details>

<details><summary>Q7. 인덱스 타입과 오버플로에서 조심할 점은?</summary>

(1) `mid = (lo + hi) / 2` 는 lo+hi 가 INT_MAX 를 넘으면 UB → `lo + (hi - lo) / 2`. (2) `size_t` 로 닫힌 구간을 쓰면 `p - 1` 이 SIZE_MAX 로 순환(실수 3). (3) Hoare 의 `j = hi + 1` 은 hi = INT_MAX 면 UB → n ≤ INT_MAX−1 을 문서화. (4) 비교 함수 `a - b` 는 UB(실수 4). (5) 16비트 `int` MCU(MSP430, AVR)에서는 n ≤ 32767 이고 `4095 * n` 합계는 바로 넘친다 → 합계는 `int32_t`/`int64_t`. 꼬리: "`(hi - lo) / 2` 와 `>> 1` 은 같나?" → 음이 아닌 값이면 같다. 부호 있는 int 의 `/ 2` 는 컴파일러가 음수 보정 명령을 붙일 수 있으니, 핫루프에선 `unsigned` 로 계산하거나 `>> 1` 을 쓴다(값이 음이 아님을 아는 경우).

</details>

<details><summary>Q8. ISR 안에서 정렬해도 되나요?</summary>

안 된다. 정렬은 데이터 의존적인 긴 시간(퀵소트 최악 O(n²))을 쓰고, ISR 은 짧고 결정적이어야 한다. 또 ISR 은 MSP(메인 스택)를 쓰므로 깊은 재귀가 모든 인터럽트가 공유하는 스택을 먹는다. 패턴: ISR 은 샘플을 링 버퍼(DS-B04)나 DMA 이중 버퍼에 넣고 태스크에 알림 → 태스크가 블록 단위로 요약(Part C). 샘플이 하나씩 들어오며 항상 정렬 상태가 필요하면 삽입 한 번 O(n) 이 상한이 정해져 있어 오히려 낫다.

</details>

## 9. 추가 연습

### 연습 1 — quickselect: 정렬 없이 중앙값 (LeetCode 215 Kth Largest Element in an Array 의 변형)

`int quickselect(int *a, int n, int k)` — k번째(0부터) 작은 값을 재귀 없이 구하라. ADC 블록 중앙값이 목적이면 전체 정렬은 낭비다. 테스트: 무작위 300개 배열의 **모든 k** 를 qsort 와 대조하고, 결과 배열이 `a[0..k-1] ≤ a[k] ≤ a[k+1..]` 인지도 확인.

<details><summary>답 보기</summary>

median-of-3 + Hoare 로 파티션한 뒤 **k 가 있는 쪽만** 남긴다: `k <= j` 면 `hi = j`, 아니면 `lo = j + 1`. 한쪽만 따라가므로 원래 재귀도 꼬리 호출 하나뿐 → while 루프, 스택 O(1). 기대 비교 O(n) (n + n/2 + n/4 + … 의 기댓값 버전), 최악은 여전히 O(n²) — 보장이 필요하면 median-of-medians(BFPRT, 최악 O(n)) 이나 introselect. Hoare 사후조건 덕에 끝나면 `a[k]` 가 제자리이고 부산물로 양쪽이 나뉘어 있다 — 절사 평균은 이걸 두 번(k = trim, n−1−trim) 쓰면 정렬 없이 된다.

</details>

### 연습 2 — Dutch national flag (LeetCode 75 Sort Colors)

값이 0/1/2 (예: 메시지 우선순위 높음/보통/낮음) 뿐인 배열을 **한 번 훑어** 정렬하라. 반복 횟수가 n 이하임을 확인. 테스트: {0,1,2}^n, n = 0..10 전수 (88,573개).

<details><summary>답 보기</summary>

Part A 의 `partition3` 에서 피벗 값을 1 로 고정한 것과 같다: 0 이면 `swap(lt++, i++)`, 2 이면 `swap(i, gt--)` (i 그대로), 1 이면 `i++`. 매 반복에 `i` 가 늘거나 `gt` 가 줄어 미확인 구역 `[i..gt]` 가 정확히 1 줄므로 반복 = n. 전수 테스트에서 실제로 모든 경우 steps ≤ n. 키 종류가 3개라 비교 정렬 하한 Ω(n log n) 이 적용되지 않는다.

</details>

### 연습 3 — 작은 구간은 삽입 정렬로 (cutoff)

safe 퀵소트에서 구간 크기가 `cutoff` 이하이면 삽입 정렬로 끝내라. 무작위 n=10^4 에서 cutoff = 1, 8, 16, 32 의 재귀 호출 수와 비교 횟수를 출력하라.

<details><summary>답 보기</summary>

실제 출력:

```text
  [연습3] cutoff= 1  calls=10000  comparisons=193091
  [연습3] cutoff= 8  calls= 1970  comparisons=153367
  [연습3] cutoff=16  calls= 1044  comparisons=153577
  [연습3] cutoff=32  calls=  526  comparisons=168186
```

호출 수가 cutoff 16 에서 1/10 로 준다 — 작은 구간에서 median-of-3 · 함수 호출 · 분기 예측 실패가 삽입 정렬의 몇 번 더 많은 이동보다 비싸다. 비교 횟수는 8~16 근처가 최소이고 32 에서 다시 는다(삽입 정렬은 구간 길이의 제곱에 비례). 그래서 실제 라이브러리들도 10~20 근처 값을 쓴다. 덤: 재귀 깊이 상한이 ⌊log2(n/cutoff)⌋ 정도로 더 준다. 원소가 거의 정렬된 블록(램프 + 노이즈)이면 삽입 정렬이 거의 O(n) 이라 더 유리하다 (ALGO-B05).

</details>

### 연습 4 — median-of-3 킬러 입력 만들기 + 인트로소트

McIlroy 의 적대자(adversary) 비교 함수로 이 레슨의 median-of-3 퀵소트 전용 킬러 입력을 만들고, 그 입력에서 인트로소트(깊이 예산 2⌊log2 n⌋, 다 쓰면 힙정렬)가 O(n log n) 에 머무는지 비교 횟수로 보여라.

<details><summary>답 보기</summary>

적대자의 생각: 값을 미리 정하지 않는다. 모든 원소를 "기체(gas)" 로 두고, 기체 둘이 비교될 때 **피벗 후보로 보이는 쪽이 아닌 쪽**을 다음으로 작은 고체 값으로 얼린다. 결과적으로 피벗은 늘 작은 쪽에 가깝게 놓인다. 정렬이 끝난 뒤의 값 배열이 킬러 입력이다. 같은 결정적 알고리즘에 이 입력을 넣으면 같은 비교 결과가 나오므로 비교 횟수가 정확히 같아야 한다 — 실제로 일치했다. 실제 출력:

```text
  [연습4] n=1000  adversary=255992  m3-quicksort=255992 (n^2/4=250000, depth=2)  introsort=33683 (n*(log2n+1)=10000, heap fallbacks=2)
  [연습4] n=2000  adversary=1011992  m3-quicksort=1011992 (n^2/4=1000000, depth=2)  introsort=75863 (n*(log2n+1)=22000, heap fallbacks=2)
  [연습4] n=4000  adversary=4023992  m3-quicksort=4023992 (n^2/4=4000000, depth=2)  introsort=168310 (n*(log2n+1)=48000, heap fallbacks=2)
```

- median-of-3 퀵소트는 ≈ n²/4 비교 — 피벗이 매번 2개 정도만 떼어 낸다. **깊이는 2** — 작은 쪽 재귀가 스택은 지켰지만 시간은 못 지킨 장면이다.
- 인트로소트는 n=4000 에서 168,310 — 퀵소트의 1/24. 처음 2⌊log2 n⌋ 번의 파티션이 각각 ~n 비교를 쓰고 나머지는 힙정렬 O(n log n). 비율 (비교 / n(log2 n + 1)) 이 3.4 → 3.5 로 거의 일정 = n log n 꼴.
- 원문(McIlroy 1999)은 gas = n−1 이지만 여기선 n 으로 둬서 얼린 값 0..n−1 과 절대 겹치지 않게 했다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed, 0.03 s
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 15/15, 보고 없음
```

## 10. 핵심 정리

- 사후조건을 외운다: Lomuto `[lo..p-1] ≤ a[p] < [p+1..hi]` (p = 피벗 자리) · Hoare `lo ≤ j < hi, [lo..j] ≤ pivot ≤ [j+1..hi]` (j 는 경계, 재귀는 (lo, j)/(j+1, hi)) · 3-way `< | == | >`.
- 최악 O(n²) = 피벗이 매번 끝값: 첫/끝 피벗 + 정렬·역순, Lomuto + 같은 값. 실측 n=10^4 에서 정확히 n(n−1)/2 = 49,995,000 비교, 깊이 n−1.
- 같은 값: Lomuto 최악, Hoare 균형(엄격한 `<` 로 양쪽이 멈춤), 3-way 최고(깊이 1, 비교 2(n−1)). 3-way 도 순서(역순)는 못 고친다.
- 작은 쪽만 재귀 + 큰 쪽 루프 → 어떤 입력이든 깊이 ≤ ⌊log2 n⌋ (정렬 입력에서 상한에 딱 붙음). 스택은 막지만 시간은 못 막는다.
- median-of-3 도 킬러 입력이 있다 (McIlroy, n=4000 에서 ≈ n²/4). 최악 시간 보장 = 인트로소트(2⌊log2 n⌋ 후 힙정렬) 또는 힙정렬.
- 스택 실측: arm64 `-O2` 64 B/단계, Cortex-M4 32~40 B/단계. 교과서 Lomuto 는 2 KB 태스크에서 정렬된 65개 블록에 넘친다. 컴파일러 꼬리 호출 제거는 두 번째 호출만, `-O0` 에선 없음.
- 가드가 없으면 스택 넘침은 **조용히** 이웃을 덮는다 (실측 30 KB). MPU 가드 · PSPLIM · high-water mark · `-fstack-usage` 로 확인.

## 참고자료

- C. A. R. Hoare, "Quicksort", *The Computer Journal* 5(1), 1962 — 원래의 양방향 스캔 파티션.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 7장 "Quicksort" — Lomuto 파티션, 평균 분석, 문제 7-1(Hoare 파티션의 정확성), 7-4(꼬리 재귀와 스택 깊이) (문제 번호는 3판 기준, 기억에 의존).
- J. Bentley, *Programming Pearls* 2nd ed., Column 11 "Sorting" — Lomuto 방식의 대중화, 같은 값에서의 퇴화.
- E. W. Dijkstra, *A Discipline of Programming*, 1976, 14장 "The problem of the Dutch national flag".
- J. L. Bentley, M. D. McIlroy, "Engineering a Sort Function", *Software—Practice and Experience* 23(11), 1993 — 3-way(양 끝 모으기) 파티션, ninther, BSD/libc qsort 의 바탕.
- M. D. McIlroy, "A Killer Adversary for Quicksort", *Software—Practice and Experience* 29(4), 1999 — 연습 4 의 적대자 비교 함수.
- D. R. Musser, "Introspective Sorting and Selection Algorithms", *Software—Practice and Experience* 27(8), 1997 — 인트로소트·인트로셀렉트.
- R. Sedgewick, K. Wayne, *Algorithms* 4th ed., 2.3 "Quicksort" — Quick3way(Dijkstra), cutoff to insertion sort.
- R. Sedgewick, "Implementing Quicksort Programs", *CACM* 21(10), 1978 — 작은 쪽 먼저 처리로 스택 O(log n).
- FreeRTOS — Stack usage and stack overflow checking (`configCHECK_FOR_STACK_OVERFLOW`, `uxTaskGetStackHighWaterMark`): <https://www.freertos.org/Stacks-and-stack-overflow-checking.html> (예전 주소 — 새 문서 구조로 리다이렉트될 수 있음)
- Arm, *ARMv7-M Architecture Reference Manual* (예외 진입 스택 프레임, 기본/FP 확장) · *ARMv8-M Architecture Reference Manual* (MSPLIM/PSPLIM).
- MISRA C:2012 Rule 17.2 (재귀 금지).
- AlgoMonster 노트 M01 §15 "Advanced Sorting — Merge Sort | Quick Sort" (Don 정리본), §14 (O(n²) 정렬).
- 관련 레슨: ALGO-B05 (삽입 정렬·안정성), ALGO-B07 (머지소트, 안정·O(n log n) 보장), ALGO-B08 (계수 정렬, ADC), ALGO-B11 (재귀 → 반복, ulimit), ALGO-M11 (스트리밍 중앙값), DS-B12 (힙 → 힙정렬), DS-B04 (링 버퍼), RTOS-M08 (태스크 스택 예산), RTOS-M09 (스택 오버플로 탐지), ARCH-M15 (MPU), C-A10 (MISRA 재귀 금지), C-B11 (스택은 어디에).
