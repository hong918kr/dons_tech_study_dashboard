---
id: ALGO-M11
time: 80분
updated: 2026-11-11
---
# ALGO-M11 힙 활용: Top-K와 스트리밍 중앙값

## 학습 목표

- 최대 힙(max-heap) `lo` + 최소 힙(min-heap) `hi` 두 개로 **실시간 중앙값(running median)** 을 유지하고, 두 불변식(순서 I1, 크기 I2)이 왜 "lo 의 top = 아래쪽 중앙값"을 보장하는지 순위(rank) 논증으로 말한다.
- 슬라이딩 윈도우 중앙값에서 **가장 오래된 샘플 삭제**를 슬롯 핸들 기반 indexed heap 으로 O(log k) 에 하고, 삭제 + 삽입 뒤 크기 차이가 **재균형 한 번**으로 돌아오는 것을 경우 분석으로 증명한다. lazy deletion 이 왜 펌웨어에 위험한지 실측(힙이 n 에 비례해 자람)으로 보인다.
- 크기 K 의 **min-heap 을 문턱(threshold)** 으로 써서 스트림의 상위 K 개 이상치(|x − 윈도우 중앙값|)를 O(K) 메모리로 추적하고, 동률 처리까지 포함한 교환 논증(exchange argument)으로 정확성을 증명한다.
- 정렬 기반 참조(정렬 배열 삽입, 매 샘플 qsort, 전부 모아 qsort)와 전수·랜덤 대조로 검증하고, 샘플당 시간(k = 5/31/255/1023)과 RAM·코드 크기를 숫자로 말한다.

## 1. 문제

"센서에 가끔 튀는 값(spike)이 섞이는데, 평균 대신 중앙값으로 걸러 보세요 → 윈도우가 255 면? → ISR 예산은?" 은 펌웨어 코딩 라운드에서 힙을 **자료구조 문제가 아니라 신호 처리 문제**로 묻는 대표 형태다. 답의 핵심은 힙 두 개와 불변식 두 줄이고, 꼬리질문은 삭제·메모리 상한·동률·넘침으로 깊어진다.

중앙값(median)은 정렬했을 때 가운데 값이다. 평균과 달리 값 하나가 아무리 커도 중앙값을 끌고 가지 못한다 — 절반 가까이가 망가져야 움직인다(붕괴점 50%). 그래서 EMI 글리치, ADC 샘플링 순간의 스위칭 노이즈, 부팅 중 보드를 툭 친 충격 같은 **드문 큰 오류**에 강하다. 문제는 비용이다: 매 샘플 정렬하면 O(k log k). 전체를 정렬할 필요는 없고 **가운데 경계만** 알면 된다 — 작은 절반의 최댓값과 큰 절반의 최솟값. 그게 힙 두 개다 (힙 자체는 DS-B12, 위치 추적 indexed heap 은 DS-M11 에서 자세히).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정렬하면 1 3 5 7 | 8 9 10 15 — 힙 두 개는 이 줄을 반으로 나눠 "경계"만 들고 있다</text>
  <g stroke="currentColor">
    <rect x="60" y="36" width="44" height="26" class="f-ok"/><rect x="106" y="36" width="44" height="26" class="f-ok"/>
    <rect x="152" y="36" width="44" height="26" class="f-ok"/><rect x="198" y="36" width="44" height="26" class="f-acc"/>
    <rect x="262" y="36" width="44" height="26" class="f-warn"/><rect x="308" y="36" width="44" height="26" class="f-mut"/>
    <rect x="354" y="36" width="44" height="26" class="f-mut"/><rect x="400" y="36" width="44" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor"><text x="78" y="54">1</text><text x="124" y="54">3</text><text x="170" y="54">5</text><text x="216" y="54">7</text>
  <text x="280" y="54">8</text><text x="326" y="54">9</text><text x="367" y="54">10</text><text x="413" y="54">15</text></g>
  <line x1="252" y1="30" x2="252" y2="70" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="460" y="48" fill="currentColor">왼쪽 = lo (작은 절반)</text>
  <text x="460" y="66" fill="currentColor">오른쪽 = hi (큰 절반)</text>
  <text x="40" y="104" fill="currentColor" font-weight="bold">lo: max-heap (루트 = 최댓값)</text>
  <g stroke="currentColor" fill="none"><path d="M150 140 L100 180"/><path d="M150 140 L200 180"/><path d="M100 200 L70 240"/></g>
  <circle cx="150" cy="128" r="16" class="f-acc" stroke="currentColor"/><text x="146" y="133" fill="currentColor">7</text>
  <circle cx="100" cy="190" r="16" class="f-ok" stroke="currentColor"/><text x="96" y="195" fill="currentColor">5</text>
  <circle cx="200" cy="190" r="16" class="f-ok" stroke="currentColor"/><text x="196" y="195" fill="currentColor">3</text>
  <circle cx="70" cy="250" r="16" class="f-ok" stroke="currentColor"/><text x="66" y="255" fill="currentColor">1</text>
  <text x="400" y="104" fill="currentColor" font-weight="bold">hi: min-heap (루트 = 최솟값)</text>
  <g stroke="currentColor" fill="none"><path d="M510 140 L460 180"/><path d="M510 140 L560 180"/><path d="M460 200 L430 240"/></g>
  <circle cx="510" cy="128" r="16" class="f-warn" stroke="currentColor"/><text x="506" y="133" fill="currentColor">8</text>
  <circle cx="460" cy="190" r="16" class="f-mut" stroke="currentColor"/><text x="456" y="195" fill="currentColor">9</text>
  <circle cx="560" cy="190" r="16" class="f-mut" stroke="currentColor"/><text x="551" y="195" fill="currentColor">15</text>
  <circle cx="430" cy="250" r="16" class="f-mut" stroke="currentColor"/><text x="421" y="255" fill="currentColor">10</text>
  <text x="250" y="132" fill="currentColor">7 ≤ 8  ← I1: max(lo) ≤ min(hi)</text>
  <text x="250" y="232" fill="currentColor">I2: |lo| − |hi| ∈ {0, 1}</text>
  <line x1="10" y1="278" x2="730" y2="278" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="300" fill="currentColor">아래쪽 중앙값 = lo 의 루트 (7).  짝수 개면 두 루트의 합 7 + 8 = 15 = 중앙값 × 2 (int64, float 없이)</text>
  <text x="10" y="320" fill="currentColor">새 값 x: x ≤ 7 이면 lo, 아니면 hi 로 넣고 → 크기 차가 2 나 −1 이 되면 루트 하나를 건너편으로 옮긴다</text>
</svg>
```

데이터는 `int32_t` 샘플(ADC 원시값, IMU raw)이다. 짝수 개일 때는 **아래쪽 중앙값**(0-based 순위 (n−1)/2)을 기본값으로 쓰고, 정확한 평균이 필요하면 **중앙값 × 2**를 `int64_t` 로 돌려준다 — float 없이, 넘침 없이.

### Part A — 실시간 중앙값 (부팅 시 자이로 바이어스 보정)

- 정지 상태에서 자이로 2000 샘플의 중앙값으로 바이어스(bias)를 잡는 상황. 누가 보드를 건드려도 평균처럼 끌려가지 않는다.
- `rm_push(m, x)`: 두 힙에 넣고 I1·I2 를 되살린다. 정적 배열이라 `RM_MAX = 8192` 개까지, 넘으면 `false`.
- `rm_median_lo(m)`: 아래쪽 중앙값. `rm_median_x2(m)`: 홀수면 2·mid, 짝수면 가운데 두 값의 합 (`int64_t`).
- 테스트: LeetCode 295 예제, INT32_MIN/MAX(합이 int32 를 넘는 경우), 용량 경계, {0,1,2} 값의 **길이 1~8 모든 수열**(9840개)의 모든 접두사, 랜덤 120 스트림(약 16만 push)을 정렬 배열 참조와 대조 + 불변식 검사기.

### Part B — 슬라이딩 윈도우 중앙값 (삭제 지원)

- 길이 k(1 ≤ k ≤ 1024) 윈도우. 새 샘플이 오면 **가장 오래된 샘플을 지우고** 넣는다. 펌웨어 조건: 메모리는 정확히 O(k), 샘플당 최악 시간이 정해져 있어야 한다.
- 방법: 링 버퍼 슬롯 번호를 힙 원소로 쓰는 **indexed heap 두 개**. `pos[slot]`·`side[slot]` 으로 지울 샘플의 위치를 O(1) 에 찾는다.
- `ih_remove_at(w, side, i)`: 힙 안 임의 위치 삭제. `wm_push(w, x, &med)`: 삭제 → 삽입 → 재균형, 창이 차면 `true` 와 아래쪽 중앙값.
- 테스트: LeetCode 480 예제 두 개 + k = 4(짝수), k = 1, 2, {0,1,2} 수열 전수(길이 ≤ 7, 모든 k: 21324회), 랜덤 160 스트림(k = 1…1024, 약 24만 샘플)을 "윈도우 복사 + qsort" 참조와 대조, 매번/주기적으로 pos·side·힙 속성 검사.

### Part C — 상위 K 개 이상치 추적 (블랙박스 로그)

- 비행 중 IMU 스트림에서 |x − 윈도우 중앙값| 이 가장 큰 K 개(≤ 64)를 **샘플 번호·값·중앙값과 함께** 남긴다. n 은 무한할 수 있으니 O(K) 메모리.
- `topk_offer`: 크기 K min-heap(루트 = 가장 약한 후보). `topk_sorted`: 강한 순으로 꺼냄. 순서는 점수 내림차순, 같으면 먼저 온 샘플이 앞(재현 가능한 로그).
- `spike_scan(x, n, win, K, w, out)`: Part B 윈도우 중앙값 + top-K. 점수는 `int64` 로 빼서 `uint32_t` (최대 2^32 − 1).
- 테스트: 삼각파 + 잡음에 스파이크 8개를 심어 K = 8 로 정확히 찾기, 경계(n < win, K = 0, win 범위 밖), 점수 2^32 − 1, top-K 힙 단독(점수 동률 많음, 번호 뒤섞음) 300회, 스트림 200개를 "점수 전부 계산 → qsort → 앞 K 개" 참조와 대조.

샘플당 시간·lazy deletion 메모리 측정은 `demo_bench.c` 에 따로 있다 (5. 풀이 후반).

## 2. Problem (English)

You receive int32 sensor samples one at a time. Implement, without dynamic allocation:

```c
bool    rm_push(rmed_t *m, int32_t x);              /* running median of everything so far (<= 8192 samples) */
int32_t rm_median_lo(const rmed_t *m);              /* lower median: element of 0-based rank (n-1)/2 */
int64_t rm_median_x2(const rmed_t *m);              /* 2 x median (sum of the two middles if n is even) */

bool wm_init(wmed_t *w, int k);                      /* 1 <= k <= 1024 */
bool wm_push(wmed_t *w, int32_t x, int32_t *med);   /* sliding window of the last k samples; true once full */

int  spike_scan(const int32_t *x, int n, int win, int K, wmed_t *work, outlier_t *out);
     /* top-K samples by |x[t] - median(window ending at t)|, strongest first, ties -> earlier t first */
```

Constraints: O(log n) per `rm_push`, O(log k) worst case per `wm_push` with exactly O(k) memory, O(log K) per candidate and O(K) memory for top-K. Values span the full int32 range.

Examples:

```text
running:  push 1, 2  -> median_x2 = 3 (1.5);  push 3 -> median_lo = 2
window k=3: [1,3,-1,-3,5,3,6,7] -> 1 -1 -1 3 5 6           (LeetCode 480)
window k=3: [1,2,3,4,2,3,1,4,2] -> 2 3 3 3 2 3 2
spikes: win=3, x = [MIN, MIN, MIN, MAX] -> idx 3, median MIN, score 4294967295
```

Follow-ups: How do you delete the oldest sample? What happens to memory with lazy deletion? Why a min-heap for top-K largest? What if k = 5? Can this run in an ISR on a Cortex-M0?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데: Part A `rm_push`·`rm_median_lo`·`rm_median_x2`, Part B `ih_remove_at`·`wm_push`, Part C `topk_offer`·`topk_sorted`·`spike_scan`. 값 힙(`vh_*`), indexed heap 의 sift(`ih_up/ih_down/ih_push/ih_pop`), top-K 힙의 sift(`tk_up/tk_down`), 참조 구현과 불변식 검사기는 주어진다. 스텁은 0 / false 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:302  rm_median_x2(&g_m) == 3
FAIL starter.c:304  rm_median_lo(&g_m) == 2 && rm_median_x2(&g_m) == 4
…
Part A exhaustive: 9840 sequences over {0,1,2}, len 1..8, mismatches=73812
…
Part B exhaustive: 21324 (sequence, k) runs over {0,1,2}, len 1..7, mismatches=81192
…
Part C top-K heap alone: 300 trials (scores 0..20, shuffled idx), mismatches=300
spikes found (K=8, win=31):
FAIL starter.c:467  r == 8 && found == 8
…
3/26 passed
```

(exit 1. 통과한 3개는 주어진 `wm_init` 의 범위 검사, 그리고 스텁의 0 이 우연히 맞는 "창보다 짧은 입력 → 0 개", "K = 0 → 0 개".)

## 4. 힌트

<details><summary>힌트 1 — 넣을 곳은 lo 의 top 과 한 번 비교해서</summary>

`x ≤ top(lo)` 이면 lo, 아니면 hi. 이것만으로 I1(max(lo) ≤ min(hi))이 유지된다. 그다음 크기만 본다: lo 가 2 개 많으면 lo 의 top 을 hi 로, hi 가 더 많으면 hi 의 top 을 lo 로. 옮기는 원소가 "경계 값"이라 I1 이 안 깨진다.

</details>

<details><summary>힌트 2 — 중앙값 × 2 는 int64 로</summary>

`(a + b) / 2` 를 int32 로 하면 INT32_MAX 근처에서 부호 있는 넘침(UB)이다. 합을 `int64_t` 로 만들어 돌려주면 float 도, 나눗셈도 없다. 호출자가 필요할 때만 `>> 1`(바닥) 한다.

</details>

<details><summary>힌트 3 — 지울 원소를 "값"으로 찾지 말고 "슬롯"으로</summary>

같은 값이 여러 개면 값으로는 어느 것을 지울지 모르고, 힙 안에서 찾는 데 O(k) 다. 링 버퍼의 슬롯 번호(0..k−1)를 힙 원소로 두고 `pos[slot]`(힙 안 칸 번호), `side[slot]`(lo/hi)을 swap 할 때마다 갱신하면 O(1) 에 찾는다. `ih_set()` 이 그 갱신을 대신 해 준다.

</details>

<details><summary>힌트 4 — 임의 위치 삭제는 "마지막 칸으로 메우고, 위 또는 아래"</summary>

i 번 칸에 마지막 원소 L 을 옮긴다. L 은 i 의 부모보다 위여야 할 수도(다른 가지에서 왔으니까) 있고, 자식보다 아래여야 할 수도 있다. 부모와 비교해서 위로 갈지, 아니면 아래로 갈지 정한다. 아래만 하면 틀린다 — 그런데 작은 힙에서는 안 드러난다 (7. 실수 3).

</details>

<details><summary>힌트 5 — 삭제 뒤 lo 가 빌 수 있다</summary>

k = 2 에서 lo = {a}, hi = {b} 이고 a 가 나가면 lo 는 비고 hi 는 남는다. 이때 "lo 가 비었으니 lo 에 넣기"를 하면 x > b 일 때 I1 이 깨진다. lo 가 비었으면 hi 의 top 과 비교하라.

</details>

<details><summary>힌트 6 — top-K "최대"는 min-heap 으로</summary>

지금까지의 상위 K 개 중 **가장 약한 것**만 알면 된다 — 새 후보가 그보다 강하면 그 자리를 뺏고, 아니면 버린다. 그 "가장 약한 것"이 min-heap 의 루트다. 동률은 "먼저 온 것이 강함"으로 정해 두면 스트림에서는 동점 후보가 절대 교체하지 않는다 (`>=` 로 쓰면 틀린다 — 7. 실수 5).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 짝수 개면 어떤 중앙값(아래쪽? 평균? → 정수로?), 값 범위(int32 전체? 12비트 ADC?), 윈도우 k 최대값, 샘플 속도(→ 샘플당 예산), 동률 처리, 메모리 상한 |
| 2. 단순 해법 | 매 샘플 윈도우 복사 + 정렬: O(k log k). 정렬 배열에 삽입·삭제: O(k) (memmove). 둘 다 정답 — 기준 구현으로 남긴다 |
| 3. 개선 + 불변식 | 힙 두 개. I1: max(lo) ≤ min(hi), I2: \|lo\| − \|hi\| ∈ {0,1} → lo 의 top = 아래쪽 중앙값. 윈도우는 슬롯 핸들 indexed heap 으로 삭제 O(log k) |
| 4. 코드 | 아래 Part A/B/C |
| 5. 소리 내어 말할 테스트 | k = 1, 2(lo 가 비는 경우), 짝수 k, 전부 같은 값, 오름차순 램프, INT32_MIN/MAX, 윈도우보다 짧은 입력, LeetCode 480 예제 |
| 6. 복잡도 | 샘플당 O(log k) 최악, 메모리 O(k) 정확히, 재귀 없음(스택 O(1)). top-K 는 O(log K)·O(K) |

### Part A — 두 힙과 순위 논증

```c
if (m->lo.n == 0 || x <= m->lo.a[0]) vh_push(&m->lo, x);
else vh_push(&m->hi, x);
if (m->lo.n > m->hi.n + 1) vh_push(&m->hi, vh_pop(&m->lo));
else if (m->hi.n > m->lo.n) vh_push(&m->lo, vh_pop(&m->hi));
```

**정리.** I1 과 I2 가 성립하면 lo 의 top 은 전체 n 개 중 1-based 순위 ⌈n/2⌉ 인 원소, 즉 아래쪽 중앙값(0-based 순위 ⌊(n−1)/2⌋)이다.
*증명.* I2 에서 |lo| = ⌈n/2⌉. I1 에서 lo 의 모든 원소 ≤ hi 의 모든 원소이므로, 전체를 정렬할 때 lo 원소들을 앞 ⌈n/2⌉ 칸에 둘 수 있다(동률이 있어도 같은 값끼리 자리를 바꾼 정렬일 뿐 정렬 결과 값의 열은 같다). 그 앞부분의 최댓값 = ⌈n/2⌉ 번째 작은 값 = top(lo). ∎ 짝수면 hi 의 top 이 ⌈n/2⌉ + 1 번째라서 두 top 의 합 = 중앙값 × 2.

**push 가 불변식을 지키는 이유.**
- *넣기:* x ≤ top(lo) 로 lo 에 넣으면 max(lo) 는 그대로라 I1 유지. x > top(lo) 로 hi 에 넣으면 hi 의 새 원소도 max(lo) 보다 크니 I1 유지. lo 가 비었으면 I2 에 의해 hi 도 비어 있다(Part A 한정 — Part B 에서는 아니다).
- *옮기기:* lo 의 top(= lo 의 최댓값)을 hi 로 옮기면 남은 lo ≤ 옮긴 값 ≤ 기존 hi 라 I1 유지. 반대도 대칭.
- *크기:* 넣기 전 d = |lo| − |hi| ∈ {0,1}, 넣은 뒤 d ∈ {−1, 0, 1, 2}. d = 2 → 하나 옮겨 0, d = −1 → 하나 옮겨 1. **한 번이면 충분**(옮기기는 d 를 정확히 2 바꾼다).

**트레이스** (자이로 원시값 대신 작은 수: 5, 15, 1, 3, 8, 7, 9, 10):

| push | 넣은 곳 | 옮김 | lo (max-heap 원소) | hi (min-heap 원소) | median_lo | median_x2 |
|---|---|---|---|---|---|---|
| 5 | lo (비어 있음) | — | {5} | {} | 5 | 10 |
| 15 | hi (15 > 5) | — | {5} | {15} | 5 | 20 |
| 1 | lo (1 ≤ 5) | — | {5, 1} | {15} | 5 | 10 |
| 3 | lo (3 ≤ 5) | d = 2 → 5 를 hi 로 | {3, 1} | {5, 15} | 3 | 8 |
| 8 | hi (8 > 3) | d = −1 → 5 를 lo 로 | {5, 3, 1} | {8, 15} | 5 | 10 |
| 7 | hi (7 > 5) | — | {5, 3, 1} | {7, 8, 15} | 5 | 12 |
| 9 | hi | d = −1 → 7 을 lo 로 | {7, 5, 3, 1} | {8, 9, 15} | 7 | 14 |
| 10 | hi | — | {7, 5, 3, 1} | {8, 9, 10, 15} | 7 | 15 |

마지막 줄이 1. 문제의 그림이다. 각 줄은 정렬 결과와 맞는다 (예: 4번째 줄 1 3 | 5 15 → 아래쪽 3, 합 8).

**복잡도.** push = 힙 push 1번 + (pop + push) 최대 1번 → O(log n) 최악, median O(1). 메모리: 힙 두 개 × (8192/2 + 1) × 4 B ≈ 32.8 KB. 재귀 없음. 펌웨어에서 "스트림 전체" 중앙값은 메모리가 n 에 비례하므로 보정처럼 **샘플 수가 정해진 작업**에만 쓴다 — 계속 도는 필터는 Part B.

### Part B — 슬롯 핸들 indexed heap

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">k = 5 윈도우: 링 버퍼 슬롯 = 힙 원소. pos/side 가 "어느 힙 몇 번 칸"을 기억</text>
  <text x="10" y="50" fill="currentColor">slot</text>
  <text x="10" y="76" fill="currentColor">val</text>
  <text x="10" y="102" fill="currentColor">side,pos</text>
  <g stroke="currentColor">
    <rect x="80" y="60" width="60" height="24" class="f-warn"/><rect x="142" y="60" width="60" height="24" class="f-mut"/>
    <rect x="204" y="60" width="60" height="24" class="f-mut"/><rect x="266" y="60" width="60" height="24" class="f-mut"/>
    <rect x="328" y="60" width="60" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="105" y="50">0</text><text x="167" y="50">1</text><text x="229" y="50">2</text><text x="291" y="50">3</text><text x="353" y="50">4</text>
    <text x="100" y="77">40</text><text x="162" y="77">90</text><text x="224" y="77">10</text><text x="286" y="77">70</text><text x="348" y="77">20</text>
    <text x="88" y="102">lo,0</text><text x="150" y="102">hi,1</text><text x="212" y="102">lo,2</text><text x="274" y="102">hi,0</text><text x="336" y="102">lo,1</text>
  </g>
  <text x="80" y="124" fill="currentColor">head = 0 → 다음 샘플이 덮어쓸 (가장 오래된) 슬롯</text>
  <text x="430" y="50" fill="currentColor">lo.a[] (max-heap, 슬롯 번호)</text>
  <g stroke="currentColor">
    <rect x="430" y="60" width="50" height="24" class="f-acc"/><rect x="482" y="60" width="50" height="24" class="f-ok"/><rect x="534" y="60" width="50" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor"><text x="440" y="77">s0:40</text><text x="492" y="77">s4:20</text><text x="544" y="77">s2:10</text></g>
  <text x="430" y="114" fill="currentColor">hi.a[] (min-heap)</text>
  <g stroke="currentColor"><rect x="430" y="122" width="50" height="24" class="f-warn"/><rect x="482" y="122" width="50" height="24" class="f-mut"/></g>
  <g fill="currentColor"><text x="440" y="139">s3:70</text><text x="492" y="139">s1:90</text></g>
  <text x="600" y="77" fill="currentColor">중앙값 = 40</text>
  <line x1="10" y1="164" x2="730" y2="164" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="188" fill="currentColor" font-weight="bold">새 샘플 x = 80 → ① slot 0 삭제: side = lo, pos = 0 → lo.a[0] 를 마지막 칸(s2)으로 메움</text>
  <g stroke="currentColor"><rect x="40" y="200" width="50" height="24" class="f-mut"/><rect x="92" y="200" width="50" height="24" class="f-ok"/></g>
  <g fill="currentColor"><text x="50" y="217">s2:10</text><text x="102" y="217">s4:20</text></g>
  <text x="160" y="217" fill="currentColor">→ 부모가 없으니 down: 20 &gt; 10 이라 교환 → lo = [s4:20, s2:10]</text>
  <text x="10" y="250" fill="currentColor">② val[0] = 80, 80 &gt; top(lo) = 20 → hi 에 push → hi = [s3:70, s1:90, s0:80]</text>
  <text x="10" y="274" fill="currentColor">③ |lo| = 2, |hi| = 3 → hi 의 top(s3:70)을 lo 로 → lo = [s3:70, s2:10, s4:20], hi = [s0:80, s1:90]</text>
  <text x="10" y="298" fill="currentColor">④ head = 1, 중앙값 = val[lo.a[0]] = 70   (창 90 10 70 20 80 → 정렬 10 20 70 80 90 ✓)</text>
  <text x="10" y="324" fill="currentColor">swap 할 때마다 ih_set() 이 a[i] 와 pos[slot] 을 같이 고친다 — 이 짝이 어긋나면 다음 삭제가 엉뚱한 칸을 지운다</text>
</svg>
```

**임의 위치 삭제 (`ih_remove_at`).** i 번 칸을 마지막 원소 L 로 메우고 크기를 줄인다. L 은 원래 다른 가지에 있었으므로 i 의 부모와는 순서 관계가 보장되지 않는다. max-heap 기준으로: L 이 parent(i) 보다 크면 위로(sift-up) — 이때 i 의 자식들은 원래 i 에 있던 값 이하이고, 원래 값 ≤ parent(i) < L 이라 아래쪽은 문제가 없다. 아니면 아래로(sift-down). 두 경우가 겹치지 않으므로 "부모와 비교해 하나만" 하면 된다. i 가 마지막 칸이었으면 줄이기만.

**크기 장부(size bookkeeping) — 재균형은 한 번이면 충분하다.** 창이 찬 뒤 한 샘플 = 삭제 1 + 삽입 1. 시작 d = |lo| − |hi| ∈ {0, 1}.

| 시작 d | 삭제한 쪽 | 삭제 후 d | 삽입한 쪽 | 삽입 후 d | 옮김 | 끝 d |
|---|---|---|---|---|---|---|
| 0 | lo | −1 | lo / hi | 0 / −2 | — / hi→lo | 0 / 0 |
| 0 | hi | 1 | lo / hi | 2 / 0 | lo→hi / — | 0 / 0 |
| 1 | lo | 0 | lo / hi | 1 / −1 | — / hi→lo | 1 / 1 |
| 1 | hi | 2 | lo / hi | 3 / 1 | lo→hi / — | 1 / 1 |

삽입 후 d ∈ {−2, −1, 0, 1, 2, 3} 이고, 옮기기 한 번이 d 를 2 만큼 바꾸므로 언제나 {0, 1} 로 돌아온다. 그래서 코드는 `while` 이 아니라 `if / else if`. 그리고 끝 d 가 count 의 홀짝과 같다 — count = k 고정이라 d 도 고정(k 홀수면 1, 짝수면 0). 창이 차기 전(count < k)에는 Part A 와 같은 삽입만 일어난다.

**삭제가 I1 을 지키나?** 부분집합을 지우는 것이라 max(lo) 는 작아지거나 같고 min(hi) 는 커지거나 같다 — 유지. 다만 **lo 가 빌 수 있다**(표 1·3행의 "삭제 후": k = 1, 2 에서 실제로 0 개). 그때 삽입은 hi 의 top 과 비교해야 한다 (힌트 5, 실수 2).

**트레이스 (LeetCode 480 예 1, k = 3, 슬롯 0·1·2 순환):**

| t | x | 지운 슬롯 (값, 힙) | 넣은 곳 | 옮김 | lo | hi | 출력 |
|---|---|---|---|---|---|---|---|
| 0 | 1 | — | lo (둘 다 빔) | — | {1} | {} | |
| 1 | 3 | — | hi | — | {1} | {3} | |
| 2 | −1 | — | lo | — | {1, −1} | {3} | 1 |
| 3 | −3 | s0 (1, lo top) | lo | — | {−1, −3} | {3} | −1 |
| 4 | 5 | s1 (3, hi) | hi | — | {−1, −3} | {5} | −1 |
| 5 | 3 | s2 (−1, lo top) | hi | 3 을 lo 로 | {3, −3} | {5} | 3 |
| 6 | 6 | s0 (−3, lo) | hi | 5 를 lo 로 | {5, 3} | {6} | 5 |
| 7 | 7 | s1 (5, lo top) | hi | 6 을 lo 로 | {6, 3} | {7} | 6 |

출력 1 −1 −1 3 5 6 이 테스트의 기대값이다.

**복잡도.** 샘플당 sift 최대 다섯 번(삭제 1, push 1, 재균형 pop 1 + push 1) × 깊이 ⌊log2(k/2 + 1)⌋ 남짓 → O(log k) 최악. 추가 메모리는 슬롯당 val 4 B + pos 2 B + side 1 B + 힙 칸 2 B × 2(두 힙 배열을 각각 WMAX 로 잡았으니) = 11 B; `sizeof(wmed_t)` = **11292 B** (WMAX = 1024, 실제 출력). k = 255 로 잡으면 약 2.8 KB. 정렬 배열 방식은 슬롯당 8 B(링 4 + 정렬본 4) 로 조금 작다.

### Part B — 왜 lazy deletion 이 아니라 indexed heap 인가

LeetCode 480 의 정석 풀이는 **lazy deletion**: 나가는 샘플을 힙에서 지우지 않고 "지워짐" 표시만 하고, 그 원소가 루트에 올라왔을 때 버린다(연습 1). 유효 원소 수(valid count)로 균형을 맞추면 정답은 나온다. 문제는 **stale 원소가 루트에 올라올 때까지 힙에 남는다**는 것. 중앙값에서 먼 값은 루트에 올라올 일이 거의 없으니 계속 쌓인다. `demo_bench.c` 실측 (k = 31, 물리 원소 수의 최댓값, 실제 출력):

```text
[4] lazy deletion: peak heap entries (valid + stale), k=31
input                    n=5000  n=20000  n=50000
random 0..4095             2554     8119    19753
random int32               2323     8547    18269
sine-like + noise           535     1395     2891
ramp up                    5000    20000    50000
constant                   5000    20000    50000
(indexed heaps: always exactly k = 31 entries)
```

램프(온도가 천천히 오르는 센서)와 상수 입력은 **샘플 수 그대로** 쌓인다: 오름차순이면 나가는 값은 항상 lo 의 가장 작은 값이라 max-heap 바닥에 묻히고, 상수면 (값, 번호) 순서에서 최신 번호가 루트라 옛 원소가 영원히 밑에 있다. 랜덤 입력도 n 의 약 40% — 31 개짜리 윈도우를 위해 수만 개를 들고 있다. RAM 이 고정된 MCU 에서는 "언젠가 넘치는 버퍼"다. 해결책은 (1) indexed heap(이 레슨), (2) stale 수가 k 를 넘으면 창으로 힙을 다시 짓기(분할상환 O(log k) 지만 그 샘플만 O(k) 지연), (3) k 가 작으면 정렬 배열.

### Part C — 크기 K min-heap 이 문턱이다

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">K = 4 개만 기억: 루트 = 지금까지 상위 4 개 중 가장 약한 것 = 입장 문턱</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="56" height="26" class="f-mut"/><rect x="80" y="50" width="56" height="26" class="f-warn"/>
    <rect x="140" y="50" width="56" height="26" class="f-mut"/><rect x="200" y="50" width="56" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor"><text x="34" y="68">s=12</text><text x="90" y="68">s=950</text><text x="154" y="68">s=7</text><text x="210" y="68">s=310</text></g>
  <text x="20" y="98" fill="currentColor">스트림 → 후보 (점수 s = |x − 윈도우 중앙값|)</text>
  <path d="M265 63 L330 63" stroke="currentColor" marker-end="url(#m11a)"/>
  <defs><marker id="m11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <rect x="340" y="40" width="120" height="46" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="352" y="60" fill="currentColor">루트(최약) s=300</text>
  <text x="352" y="78" fill="currentColor">s &gt; 300 인가?</text>
  <path d="M460 52 L540 40" stroke="currentColor" marker-end="url(#m11a)"/>
  <path d="M460 74 L540 96" stroke="currentColor" marker-end="url(#m11a)"/>
  <text x="545" y="44" fill="currentColor">예: 루트 교체 + sift-down</text>
  <text x="550" y="100" fill="currentColor">아니오: 버림 (O(1))</text>
  <g stroke="currentColor" fill="none"><path d="M400 150 L350 190"/><path d="M400 150 L450 190"/><path d="M350 210 L320 238"/></g>
  <circle cx="400" cy="138" r="18" class="f-acc" stroke="currentColor"/><text x="386" y="143" fill="currentColor">300</text>
  <circle cx="350" cy="200" r="18" class="f-ok" stroke="currentColor"/><text x="336" y="205" fill="currentColor">420</text>
  <circle cx="450" cy="200" r="18" class="f-ok" stroke="currentColor"/><text x="436" y="205" fill="currentColor">880</text>
  <circle cx="320" cy="244" r="14" class="f-ok" stroke="currentColor"/><text x="306" y="249" fill="currentColor">610</text>
  <text x="20" y="150" fill="currentColor">s=950 → 300 을 밀어냄</text>
  <text x="20" y="172" fill="currentColor">s=310 → 그다음 문턱 420? 아니오면 버림</text>
  <text x="20" y="194" fill="currentColor">s=12, s=7 → 즉시 버림</text>
  <text x="500" y="160" fill="currentColor">메모리 K 칸, 후보당 O(log K)</text>
  <text x="500" y="182" fill="currentColor">동률: 먼저 온 것이 강함 →</text>
  <text x="500" y="204" fill="currentColor">같은 점수의 새 후보는 교체 못 함</text>
</svg>
```

**전순서(total order).** 후보 x 가 y 보다 강하다 ⇔ score_x > score_y, 또는 같고 idx_x < idx_y. idx 가 모두 다르므로 동률이 없다 — 그래서 "상위 K 개"가 유일하게 정해지고, 로그가 재현 가능하다.

**정리.** 후보 t 개를 처리한 뒤 힙 H 는 그 t 개 중 상위 min(K, t) 개와 정확히 같다.
*증명 (귀납 + 교환 논증).* t 개 뒤 H 가 상위 K 개(꽉 참)이고 w = 루트 = H 의 K 번째(가장 약한 것)라 하자. 새 후보 y:
- y ≻ w: y 보다 강한 원소는 H 안에 기껏 K − 1 개(w 는 아님), H 밖의 원소는 모두 w 보다 약하니(H 가 상위 K 개라서) y 보다도 약하다 → y 는 상위 K 에 든다. 대신 w 는 이제 자기보다 강한 원소가 K 개(H − {w} 와 y)라 빠진다. 다른 원소들의 "자기보다 강한 원소 수"는 y 가 더해져도 K 를 넘지 않는다(그들은 w 보다 강하고, y 를 더해도 기껏 K − 1). 따라서 새 상위 K = H − {w} + {y} — 코드가 하는 일.
- y ≺ w: H 의 K 개 모두가 y 보다 강하다 → y 는 상위 K 밖, H 그대로. ∎

**스파이크 실험 (solution.c 실제 출력).** 삼각파(기울기 ±5/샘플) + 잡음 ±20 에 8 개를 심었다 (+4000, −3500, +5000, +3000, −4500, +3800, −3200, +4200):

```text
spikes found (K=8, win=31): 1033(4901) 2222(4554) 3998(4131) 100(4060) 2900(3891) 517(3413) 3333(3101) 1500(2942)
```

8 개 모두 찾았고 1 등은 +5000 짜리(idx 1033). 점수가 진폭과 최대 100 정도 다른 이유: **인과(causal) 윈도우**는 현재 샘플에서 끝나므로 중앙값이 15 샘플(= (31−1)/2) 전의 신호 값을 가리킨다. 기울기 5 × 15 = 75 만큼 지연(lag)이 생기고, 그 방향이 스파이크 부호와 같으면 점수가 줄고 반대면 는다. 오프라인 분석이라면 중심 윈도우(centered window)로 지연을 없앤다(Hampel 필터가 그렇다).

**넘침.** int32 두 개의 차는 최대 INT32_MAX − INT32_MIN = 2^32 − 1 → int32 로는 넘치고(UB), `int64_t` 로 빼서 절댓값을 `uint32_t` 에 담으면 정확히 들어간다. 테스트 `[MIN, MIN, MIN, MAX]`, win = 3 → 점수 4294967295.

### 샘플당 시간 — `demo_bench.c`

n = 20000 샘플(0..4095, 12비트 ADC 처럼), 워밍업 1회 + 9회 중앙값, Apple M6 / Apple clang 21 / -O2 (실제 출력 한 번):

```text
[1] sliding-window median, n=20000 samples (0..4095), ns/sample (median of 9)
     k qsort/sample   sorted arr heap del+ins heap replace    heap lazy   agree
     5         21.4         31.2         15.8          9.7         52.8   yes
    31        562.9         35.8         32.0         17.3         57.8   yes
   255       8162.9         63.4         40.7         44.1         52.1   yes
  1023      38554.8         88.0         49.5         41.7         46.5   yes

[2] running median over the whole stream, n=50000, ns/sample
two heaps 26.3   sorted array insert 950.8   agree yes

[3] top-K largest of n=1000000, ns/sample
K=8    size-K min-heap 0.51   copy+qsort all 40.21   agree yes
K=64   size-K min-heap 0.52   copy+qsort all 40.28   agree yes
K=512  size-K min-heap 0.56   copy+qsort all 40.89   agree yes
```

**주의:** 이 측정 중 맥은 다른 빌드 작업 수십 개와 나눠 쓰였다(`uptime` load average ≈ 43). 같은 바이너리를 네 번 돌린 범위: qsort/sample k = 255 는 7.9–16.5 µs, 정렬 배열 k = 1023 은 88–201 ns, 힙(삭제+삽입) k = 1023 은 50–67 ns, lazy k = 5 는 53–89 ns. **절대값이 아니라 비율과 추세**를 볼 것:

| 관찰 | 근거 |
|---|---|
| 매 샘플 qsort 는 k log k 로 폭발 | 21 → 563 → 8163 → 38555 ns (k = 5 → 1023, 약 1800배) |
| 정렬 배열은 k 에 선형이지만 상수가 작다 | memmove 는 연속 메모리 복사라 k = 255 까지 힙과 같은 급. k = 5 에선 어느 방법이나 수십 ns |
| 힙은 k 에 거의 무관 | 랜덤 입력에선 sift 가 평균적으로 몇 단계 안에서 멈춘다(노드 대부분이 잎 근처). 최악은 O(log k) |
| lazy 는 빠르지도 않다 | stale 원소를 들고 다니는 비용 + 위의 메모리 폭증 |
| 제자리 갱신(replace) 이 삭제+삽입보다 적게 움직인다 | 연습 2 실측: 힙 칸 쓰기 44473 → 28170 (k = 255) |
| 전체 중앙값: 힙 26 ns vs 정렬 배열 951 ns | n = 50000, 정렬 배열은 평균 n/4 원소 memmove |
| top-K: 힙이 약 80배 빠르고 메모리 O(K) | 대부분의 후보가 루트 비교 한 번에 탈락 |

**Cortex-M 관점.** Part B 코드만 떼어 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2` 로 컴파일하면 `.text` **1468 B**(cortex-m4: 1332 B, `llvm-size -A` 실측)이고 함수 호출이 하나도 없다. 부모 인덱스 `(i − 1) / 2` 는 `lsrs` 시프트가 된다. 반면 링 버퍼 head 를 `(head + 1) % k` 로 바꾸면 M0 출력에 `bl __aeabi_idivmod` 가 생긴다(실제 확인) — 나눗셈기가 없는 코어에서 샘플마다 소프트웨어 나눗셈. 이 레슨이 `head + 1 == k ? 0 : head + 1` 로 쓰는 이유다.

### 복잡도 정리

| 함수 | 시간 (샘플/후보당) | 추가 메모리 | 메모 |
|---|---|---|---|
| `rm_push` | O(log n) 최악 | 힙 2 × (n/2 + 1) | median O(1). 스트림 길이에 비례 → 보정용 |
| `wm_push` (indexed) | O(log k) 최악, 랜덤 평균은 거의 상수 | 정확히 O(k): 슬롯당 11 B | 재귀 없음. ISR 예산 계산 가능 |
| lazy deletion (연습 1) | O(log m) 분할상환, m = 물리 크기 | 최악 O(n) — 실측 n 그대로 | 펌웨어 비추천 |
| 정렬 배열 | O(k) (memmove) | 8 B/슬롯 | k ≤ 수십이면 실전 최선 후보 |
| `topk_offer` | O(log K), 탈락은 O(1) | K × 16 B | 끝에 `topk_sorted` O(K log K) |
| `spike_scan` | O(n (log win + log K)) | wmed_t + 1 KB 스택(topk_t) | |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 26/26 passed, exit 0, 약 0.4 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench  # 약 12 초 (qsort/sample k=1023 이 대부분)
```

solution 실행의 요약 줄 (실제 출력):

```text
Part A exhaustive: 9840 sequences over {0,1,2}, len 1..8, mismatches=0
Part A random: 120 streams, 160493 pushes, mismatches=0
Part B exhaustive: 21324 (sequence, k) runs over {0,1,2}, len 1..7, mismatches=0
Part B random: 160 streams, 241070 samples, mismatches=0
sizeof(wmed_t) = 11292 bytes for WMAX = 1024
Part C top-K heap alone: 300 trials (scores 0..20, shuffled idx), mismatches=0
Part C random: 200 streams vs sort-all reference, mismatches=0
26/26 passed
```

## 7. 자주 하는 실수

아래 숫자는 이 레슨 작업 중 solution.c 의 한 줄만 바꿔 같은 하네스로 돌린 실제 결과다.

**1. 크기만 보고 넣는다 (I1 무시).** `if (lo.n <= hi.n) push(lo) else push(hi)` 는 크기는 맞지만 큰 값이 lo 에 들어가 순서가 깨진다. Part A 전수 검사에서 **44372** 개 접두사가 틀렸고 LeetCode 295 예제부터 실패한다. 고친 코드: `x ≤ top(lo)` 로 쪽을 정하고 크기는 나중에.

**2. 삭제 뒤 lo 가 빈 경우를 놓친다.** Part A 에서 맞던 "lo 가 비면 lo 에" 를 Part B 에 그대로 쓰면 k ≤ 2 에서 틀린다 (lo = {}, hi = {b}, x > b 인데 lo 로). 전수 **547**, 랜덤 **4903** 개 틀림. 고친 코드: `else side = (hi.n > 0 && x > val[hi.a[0]]) ? 1 : 0;`

**3. 임의 위치 삭제에서 sift-down 만 한다.** 마지막 원소가 다른 가지에서 왔으면 부모보다 클 수 있다. 재미있는 점: **전수 검사(길이 ≤ 7)는 0 개** 틀렸고 랜덤(k 최대 1024)에서 **4532** 개 틀렸다. 이유: sift-up 이 필요하려면 지운 칸의 부모가 마지막 칸의 조상이 아니어야 하는데, 가장 작은 예가 크기 6 인 힙(칸 3 이나 4 를 지우고 칸 5 로 메움)이다. 한 힙에 6 개가 있으려면 k ≥ 11. **작은 전수 검사 + 큰 랜덤 검사를 둘 다** 하는 이유.

**4. "−x 를 min-heap 에" 로 max-heap 을 흉내 낸다.** Python heapq 의 관용구(AlgoMonster 풀이도 이 방식)를 C 로 옮기면 INT32_MIN 에서 부호 있는 넘침(UB)이다. UBSan 실제 출력:

```text
neg.c:6:19: runtime error: negation of -2147483648 cannot be represented in type 'int32_t' (aka 'int'); cast to an unsigned type to negate this value to itself
```

-O2 에서는 조용히 −2147483648 이 그대로 나와 그 값이 "가장 큰 값"처럼 정렬된다. 고친 코드: 방향 플래그(`is_max`)로 비교만 뒤집는다 (`vh_above`).

**5. top-K 교체 조건을 `>=` 로 쓴다.** 동점 후보가 들어올 때마다 먼저 온 원소를 밀어내 결과가 "나중 것 우선"이 되고 참조(먼저 온 것 우선)와 다르다. top-K 힙 단독 **93/300**, 스트림 **105/200** 회 불일치. 로그의 재현성 문제이고, 펌웨어에서는 같은 점수 이벤트가 계속 오면 매번 sift-down 비용까지 낸다. 고친 코드: 전순서 `ol_better(o, root)`.

**6. 평균을 int32 로 낸다.** `(a + b) / 2` 는 a = INT32_MAX − 1, b = INT32_MAX 에서 넘친다 (UBSan: `signed integer overflow: 2147483646 + 2147483647 cannot be represented in type 'int32_t'`, -O2 결과는 −1). `a + (b − a) / 2` 도 b − a 가 2^32 − 1 까지 가므로 int32 로는 안전하지 않다. 고친 코드: `int64_t` 합(= 중앙값 × 2)을 돌려준다.

**7. 재균형을 한 방향만 한다.** `hi.n > lo.n` 쪽을 빼면 LeetCode 480 예제 세 개가 모두 틀리고 전수 **31738** 개 불일치. 표(크기 장부)의 네 줄을 다 써 보면 양방향이 다 필요하다는 게 보인다.

## 8. 면접 Q&A

<details><summary>Q1. 왜 힙 두 개인가요? 정렬 배열이나 균형 트리와 비교하면?</summary>

중앙값은 "가운데 경계"만 알면 되고, 힙 두 개는 그 경계(lo 의 max, hi 의 min)를 O(1) 에 보여 주며 갱신이 O(log n) 이다. 정렬 배열은 조회 O(1) 이지만 삽입이 O(n) 이동이다(실측 n = 50000 에서 951 ns vs 26 ns). 부분 트리 크기를 들고 있는 균형 BST(order-statistic tree — DS-M07 의 AVL 노드에 크기 필드를 더한 것)는 임의 순위 질의·삭제까지 O(log n) 이라 더 일반적이지만 코드가 길고 노드 포인터 메모리가 든다. 면접 45분에는 힙 두 개가 정답 + "윈도우면 indexed heap 또는 order-statistic tree" 로 확장을 말하면 된다.

</details>

<details><summary>Q2. lo 의 top 이 정말 중앙값이라는 걸 증명해 보세요.</summary>

I2 에서 |lo| = ⌈n/2⌉, I1 에서 lo 의 원소는 hi 의 어떤 원소보다도 크지 않다. 그러면 정렬된 열의 앞 ⌈n/2⌉ 칸을 lo 원소로 채울 수 있고, 그 칸들의 최댓값 = 1-based ⌈n/2⌉ 번째 값 = 아래쪽 중앙값. 동률이 있어도 같은 값끼리의 자리 바꿈일 뿐이라 값의 열은 같다. 짝수면 hi 의 top 이 ⌈n/2⌉ + 1 번째라 두 top 의 평균이 일반적인 중앙값. 꼬리질문 "push 후 몇 번 옮기나?" → 크기 차는 넣은 뒤 {−1, 0, 1, 2} 이고 한 번 옮기면 2 가 바뀌니 최대 한 번.

</details>

<details><summary>Q3. 슬라이딩 윈도우에서 가장 오래된 샘플은 어떻게 지우나요? lazy deletion 은요?</summary>

두 가지. (1) indexed heap: 링 버퍼 슬롯을 힙 원소로, `pos/side` 로 위치를 기억해 O(log k) 에 지운다. 메모리 정확히 O(k). (2) lazy deletion: 표시만 하고 루트에 올라올 때 버린다. 코드는 쉽지만 stale 원소가 쌓인다 — 이 레슨 실측 k = 31 에서 램프·상수 입력은 n 개 전부(50000), 랜덤도 n 의 약 40%. MCU 에서는 RAM 상한이 없는 셈이라 indexed heap 을 고르고, 그 이유를 "메모리 상한과 최악 시간이 정해진다"로 말한다. 제자리 갱신(삭제+삽입 대신 슬롯 값을 바꾸고 고친 뒤 두 top 이 뒤집혔으면 한 번 교환)은 크기 장부가 아예 안 변해 더 단순하고 힙 쓰기가 37% 적다(연습 2).

</details>

<details><summary>Q4. k 가 3, 5, 7 처럼 작으면요? 값이 12비트면요?</summary>

k ≤ 7 이면 힙은 과하다. 정렬 네트워크(sorting network: 3-점 중앙값은 비교·교환 3번, 분기 없이 min/max 로)가 가장 빠르고 결정적이다 — SENS-B07 이 다룬다. 수십까지는 정렬 배열(이진 탐색 + memmove)이 단순하고 캐시 친화적이라 실측에서도 힙과 같은 급이다(k = 31: 36 ns vs 32 ns, 부하 많은 맥). 값 범위가 작으면(8~12비트) 히스토그램을 들고 중앙값 포인터를 앞뒤로 움직이는 방식(영상의 Huang 알고리즘, CV-B05)이 샘플당 거의 상수다 — 대신 4096 칸 카운터 메모리. 면접에선 "k 와 값 범위를 먼저 묻고 셋 중 고른다"가 좋은 답이다.

</details>

<details><summary>Q5. 상위 K 개 "최대"를 찾는데 왜 min-heap 인가요? quickselect 는?</summary>

들고 있는 K 개 중 **가장 약한 것**이 새 후보의 입장 문턱이라서다. 루트와 한 번 비교해 대부분 O(1) 에 탈락시키고, 들어올 때만 O(log K). 전체 O(n log K), 메모리 O(K) — 스트림에 맞다. max-heap 에 n 개를 다 넣고 K 번 pop 하면 O(n) 메모리. 배열이 이미 메모리에 다 있는 오프라인 문제면 quickselect(ALGO-B06 의 파티션)로 평균 O(n) 에 K 번째를 찾을 수 있지만 최악 O(n²) 이고 배열을 뒤섞는다. LeetCode 215 Kth Largest Element in an Array, 703 Kth Largest Element in a Stream, 973 K Closest Points to Origin 이 같은 패턴이다.

</details>

<details><summary>Q6. 이 중앙값 필터를 ADC ISR 안에서 돌려도 되나요? Cortex-M0 라면?</summary>

가능은 하다 — indexed heap 은 최악이 O(log k) 로 정해져 있어(k = 255 면 힙 깊이 7, sift 다섯 번) 사이클 상한을 계산할 수 있고 malloc·재귀가 없다. 그래도 보통은 ISR 에서 샘플을 링 버퍼에 넣기만 하고(DS-B 링 버퍼, C-M14 공유 데이터 규칙) 필터는 태스크에서 돈다 — ISR 지연(jitter)을 줄이기 위해. M0 에서 주의할 것: 나눗셈 명령이 없으므로 링 인덱스를 `% k` 로 하면 샘플마다 `__aeabi_idivmod` 호출이 생긴다(이 레슨에서 asm 확인) → 비교로 감기. 부모 인덱스 `(i − 1) / 2` 는 시프트가 된다. 코드 크기는 Part B 전체가 M0 에서 1468 B.

</details>

<details><summary>Q7. 중앙값 필터와 이동 평균은 무엇이 다른가요?</summary>

이동 평균(ALGO-B10)은 선형이라 주파수 응답이 있고, 스파이크 하나가 윈도우 내내 1/k 크기로 번진다. 중앙값은 비선형: 윈도우의 절반 미만을 차지하는 스파이크는 **완전히 지운다**(붕괴점 50%), 계단(edge)도 무디게 하지 않는다. 대가: 주파수 응답으로 설계할 수 없고, 계산이 더 비싸고, 인과 윈도우면 (k − 1)/2 샘플 지연 — Part C 실측에서 기울기 5 × 15 샘플 = 75 만큼 점수가 어긋난 것이 그 지연이다. 실전에선 중앙값으로 스파이크를 걷어낸 뒤 선형 필터를 거는 조합을 쓴다(SENS-B07, DSP 트랙).

</details>

<details><summary>Q8. 동률(같은 점수, 같은 값)은 어떻게 다루나요?</summary>

중앙값 쪽은 동률이 정확성에 영향이 없다 — I1 이 `≤` 라 같은 값이 양쪽 힙에 나뉘어 있어도 된다. 대신 **삭제할 원소를 값으로 찾으면** 같은 값 중 어느 것인지 모호해지므로 슬롯 핸들로 찾는다. top-K 는 "상위 K 개"가 유일하도록 전순서(점수, 그다음 번호)를 정한다. 안 정하면 실행마다·구현마다 로그가 달라지고 테스트 참조와 대조도 못 한다(실수 5: `>=` 한 글자로 105/200 불일치).

</details>

## 9. 추가 연습

### 연습 1 — lazy deletion 윈도우 중앙값 (LeetCode 480 정석)

힙 원소를 (값, 샘플 번호) 쌍으로 두고, 나가는 샘플은 지우지 않고 유효 개수만 고쳐라. 어느 힙에 있었는지는 어떻게 아나? 그리고 k = 31, n = 20000 에서 물리 힙 크기의 최댓값을 랜덤·램프·상수 입력으로 재 보라.

<details><summary>답 보기</summary>

(값, 번호) 사전식 순서는 번호가 모두 달라 전순서다. 나가는 원소 o 는 아직 유효하므로, lo 의 루트를 stale 정리(prune)한 뒤 `o ≤ top(lo)` 면 lo, 아니면 hi 에 있다 — 유효한 lo 원소는 모두 top(lo) 이하, 유효한 hi 원소는 모두 그보다 크기 때문. 그쪽 유효 개수를 줄이고, 균형은 유효 개수로 맞추되 옮기기 전에 루트를 정리한다. 실측 (practice.c 실제 출력):

```text
ex1 lazy peak heap entries (k=31, n=20000) random   : 9153
ex1 lazy peak heap entries (k=31, n=20000) ramp up  : 20000
ex1 lazy peak heap entries (k=31, n=20000) ramp down: 20000
ex1 lazy peak heap entries (k=31, n=20000) constant : 20000
```

정답은 맞지만(랜덤 60 스트림 불일치 0) 메모리는 n 에 비례한다. 5. 풀이의 "왜 indexed heap 인가" 참고.

</details>

### 연습 2 — 제자리 갱신(replace) 윈도우 중앙값

창이 찬 뒤에는 삭제 + 삽입 대신, 가장 오래된 슬롯의 값을 새 샘플로 **바꾸고** 그 힙 안에서 위/아래로 고친 다음, `top(lo) > top(hi)` 면 두 루트를 한 번 교환하라. 왜 교환 한 번이면 충분한가?

<details><summary>답 보기</summary>

크기는 변하지 않으니 I2 는 공짜. 새 값 x 가 lo 의 슬롯에 들어갔다고 하자(hi 는 대칭). 다른 lo 원소들은 원래부터 min(hi) = h 이하다. x ≤ h 면 끝. x > h 면 x 가 lo 의 최댓값(루트)이 되고, 교환하면 lo = (나머지 lo) ∪ {h} 의 최댓값은 h, hi = (hi − {h}) ∪ {x} 의 최솟값은 h 이상 → max(lo) ≤ min(hi). 교환된 두 루트는 각자 힙에서 sift-down. practice.c 에서 삭제+삽입판과 25 스트림 대조 불일치 0, 힙 칸 쓰기 수 실측:

```text
ex2 heap slot writes (k=255, random, n=3255): delete+insert=44473 replace=28170
```

</details>

### 연습 3 — 스트림의 K 번째 큰 값 (LeetCode 703 Kth Largest Element in a Stream)

`kth_add(q, x)` 가 지금까지 본 값 중 K 번째로 큰 값을 돌려주게 하라. 예: K = 3, 처음 [4, 5, 8, 2], 이후 3, 5, 10, 9, 4 → 4, 5, 5, 8, 8.

<details><summary>답 보기</summary>

크기 K min-heap 에 "지금까지 큰 값 K 개"를 둔다 → 루트가 K 번째 큰 값. 꽉 찼으면 x > 루트일 때만 루트를 교체하고 sift-down. 동률은 값만 보면 되니 `>` 든 `>=` 든 결과가 같다(값이 같은 원소끼리 바뀌는 것뿐). 랜덤 40 스트림을 내림차순 정렬 배열 참조와 대조.

</details>

### 연습 4 — 정렬된 센서 로그 K 개 병합 (LeetCode 23 Merge k Sorted Lists 의 배열판)

센서 K 개(≤ 16)가 각각 타임스탬프 오름차순 로그를 갖고 있다. 하나의 시간순 스트림으로 병합하라. 같은 시각이면 센서 번호가 작은 쪽 먼저. 빈 로그도 있다.

<details><summary>답 보기</summary>

각 로그의 "다음 원소" 하나씩만 min-heap 에 (ts, sensor, pos) 로 넣는다 → 힙 크기 ≤ K. 루트를 내보내고 같은 로그의 다음 원소로 루트를 **교체**(pop + push 대신 replace-top 한 번), 로그가 끝났으면 마지막 원소로 메우고 크기를 줄인다. 전체 O(N log K), 메모리 O(K). 같은 센서의 원소는 동시에 힙에 하나뿐이라 (ts, sensor) 가 힙 안에서 전순서다. 참조: 전부 이어 붙여 (ts, sensor, pos) 로 qsort — 200 회 일치. 외부 정렬의 병합 단계(ALGO-A12)와 같은 구조.

</details>

### 연습 5 — 실시간 p-분위수 (p90, p99)

중앙값은 p = 1/2 인 분위수다. 두 힙의 크기 비율을 바꿔 nearest-rank p-분위수(정렬했을 때 ⌈p·n⌉ 번째 값)를 유지하라. p = num/den 정수로.

<details><summary>답 보기</summary>

I2 를 |lo| = r = ⌈n·num/den⌉ 로 바꾼다. I1 은 그대로라 Part A 의 순위 논증에서 top(lo) = r 번째 작은 값. 넣은 뒤 `while (lo.n > r)` lo→hi, `while (lo.n < r)` hi→lo. n 이 1 늘 때 r 은 0 또는 1 늘어서 실제로는 한 번 이하로 옮긴다. r 계산은 `(n*num + den − 1) / den` 을 int64 로. 예: 10, 20, …, 100 의 p90 = 90. p ∈ {1/2, 9/10, 99/100, 1/100, 1} 로 50 스트림 대조 일치. 지연 시간 분포(p99 latency)를 펌웨어에서 직접 낼 때 쓰지만 메모리가 n 에 비례하므로, 긴 측정이면 히스토그램 근사가 현실적이다(HPC-B01 의 측정 하네스 참고).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 10/10 passed, 약 0.6 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 오류 없음
```

## 10. 핵심 정리

- 중앙값 = 작은 절반의 max(lo, max-heap) 와 큰 절반의 min(hi, min-heap) 의 경계. I1: max(lo) ≤ min(hi), I2: |lo| − |hi| ∈ {0,1} → top(lo) = 아래쪽 중앙값(순위 ⌈n/2⌉).
- push: `x ≤ top(lo)` 로 쪽을 정하고, 크기 차가 2 나 −1 이면 루트 하나를 옮긴다 — 한 번이면 충분.
- 짝수 중앙값은 int64 합(= 중앙값 × 2)으로. `-x` 로 max-heap 흉내 금지(INT32_MIN UB) — 비교 방향 플래그.
- 윈도우: 슬롯 핸들 + pos/side 로 O(log k) 임의 삭제(마지막으로 메우고 up 또는 down). lo 가 빌 수 있다(k ≤ 2). 메모리 정확히 O(k), M0 코드 1.5 KB, `% k` 대신 비교로 감기.
- lazy deletion 은 정답이지만 stale 원소가 n 에 비례해 쌓인다(실측: 램프 50000/50000) → 펌웨어는 indexed heap 또는 작은 k 면 정렬 배열·정렬 네트워크.
- top-K 최대 = 크기 K min-heap, 루트가 문턱. 전순서(점수, 먼저 온 것)로 동률을 정해야 재현 가능. O(n log K), O(K).
- 실측 비율: 매 샘플 qsort 는 k = 5 → 1023 에서 약 1800배 느려지고, 힙은 거의 평평, top-K 힙은 전체 정렬보다 약 80배 빠름.

## 참고자료

- LeetCode 295 Find Median from Data Stream, 480 Sliding Window Median, 215 Kth Largest Element in an Array, 703 Kth Largest Element in a Stream, 23 Merge k Sorted Lists, 973 K Closest Points to Origin — 예제 값은 이 레슨 테스트에서 직접 재현.
- AlgoMonster 노트 M08 §8 Median of Data Stream, §2~5 K Closest Points · Merge K Sorted Lists · Kth Largest · Kth Smallest in a Sorted Matrix (Don 정리본) — 두 힙 불변식과 top-K 패턴. 연습 4 가 Merge K 를 직접 다룬다.
- T. Cormen et al., *Introduction to Algorithms* (CLRS), 6장 Heapsort(이진 힙, 우선순위 큐)와 9장 Medians and Order Statistics(선택 알고리즘), 14장(3판) order-statistic tree.
- R. Sedgewick, K. Wayne, *Algorithms* 4th ed., §2.4 Priority Queues — indexed priority queue(`IndexMinPQ`)의 pos 배열 설계.
- T. Huang, G. Yang, G. Tang, "A fast two-dimensional median filtering algorithm", *IEEE Trans. ASSP* 27(1), 1979 — 히스토그램 기반 이동 중앙값 (Q4). 서지 세부는 기억에 의존 — 확인 권장.
- 랜덤 삽입 시 이진 힙 sift-up 의 평균 단계 수가 상수라는 분석(Porter & Simon, 1975 로 알고 있음 — 확인 권장): 실측에서 힙 비용이 k 에 거의 무관했던 이유의 배경.
- F. R. Hampel, "The influence curve and its role in robust estimation", *JASA* 69, 1974 — 중앙값의 붕괴점, Hampel 이상치 필터의 배경 (서지 세부 확인 권장).
- ARM, *ARMv6-M Architecture Reference Manual* — 나눗셈 명령 없음. 이 레슨의 M0/M4 코드 크기와 `__aeabi_idivmod` 는 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2` / `thumbv7em … cortex-m4` 실제 출력 + `llvm-size -A`.
- 관련 레슨: DS-B12 (이진 힙), DS-M11 (indexed PQ, decrease-key), DS-M07 (AVL — order-statistic 확장), ALGO-B06 (퀵소트 파티션 → quickselect), ALGO-B10 (이동 평균), ALGO-A08 (Welford·저수지 샘플링 — 스트리밍 통계), ALGO-A12 (외부 정렬 병합), ALGO-M10 (힙 기반 스케줄링), SENS-B07 (3/5/7-점 메디안 필터, 정렬 네트워크), CV-B05 (Huang 히스토그램 메디안), HPC-B01 (측정 하네스), C-M14 (ISR 공유 데이터)
