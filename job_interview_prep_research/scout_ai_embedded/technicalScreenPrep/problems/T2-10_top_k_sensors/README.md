# T2-10 · Top-K Hottest Sensors

> **ID**: T2-10 · **Priority**: P2 · **Pattern**: size-K min-heap in a fixed array + in-place heap sort · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-10` / `./run.sh T2-10 sol`

## 1. Problem

The robot's thermal manager polls `N` temperature sensors (motor drivers, battery cells, SoC, …) and must show the **K hottest** on the operator display (LeetCode 215 / 347 style).

- "Hotter" order: higher `temp_c10` first; on equal temperature, the **smaller `id`** first.
- Write `min(n, k)` readings into `out`, sorted **hottest first**, and return that count.
- `out` has room for `k` readings; `out` may be `NULL` when `k == 0`.
- Use a **size-K min-heap built inside `out[]` itself** — no `malloc`, no extra array. Target O(n log k) time, O(1) extra memory. Do not modify the input.

```c
typedef struct { uint16_t id; int16_t temp_c10; } Reading;   /* temp in 0.1 degC */

size_t top_k_hottest(const Reading *r, size_t n, size_t k, Reading *out);
```

### Example

```
k = 3
readings (id, temp) = (0,612) (1,745) (2,598) (3,801) (4,745) (5,690) (6,812) (7,701)

output = 3 readings: (6,812) (3,801) (1,745)
         (1 and 4 are both 745; id 1 wins the tie)
```

### Constraints

- `0 <= n <= 4096`, `0 <= k <= 64`, ids are unique.
- `temp_c10` is any `int16_t` value (negative at cold start).

### Clarifying questions to ask (first 3 minutes)

1. How are ties broken? Does the output need to be sorted?
2. Is `k` small compared to `n`? (heap vs full sort vs quickselect)
3. Can I reorder the input array, or must it stay `const`?
4. Is this a one-shot call, or will readings stream in continuously?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-10`로 바로 테스트하세요. 테스트 6개가 들어 있습니다. 헬퍼(`hotter`, `sift_up`, `sift_down`)는 직접 만들어야 합니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 왜 "min"-heap인가?</summary>

가장 뜨거운 K개를 고를 때 필요한 질문은 "새 값이 **지금 남겨 둔 K개 중 가장 차가운 것**보다 뜨거운가?"입니다. 그래서 루트가 가장 차가운 값인 min-heap을 씁니다. 루트가 일종의 **문지기(gate)**입니다. 더 뜨거우면 루트를 갈아 끼우고 `sift_down`, 아니면 버립니다.

</details>

<details>
<summary>Hint 2 — 비교 함수 하나로 동점 처리</summary>

`hotter(a, b)`를 "온도가 높거나, 같으면 id가 작다"로 정의하고 **모든 비교에 이것만** 씁니다. 그러면 min-heap 루트는 "가장 덜 hotter한 것" = 온도가 낮고, 같으면 id가 큰 것이 되어 동점 규칙이 자동으로 맞습니다.

</details>

<details>
<summary>Hint 3 — 결과 정렬도 제자리에서</summary>

힙이 다 차면 heap sort의 뒷단계만 하면 됩니다. 루트(가장 차가운 것)를 배열 끝과 바꾸고 크기를 하나 줄여 `sift_down`. 반복하면 끝에서부터 차가운 순서로 쌓여서 `out[0]`이 가장 뜨거운 값이 됩니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

배열 `out[0..2]`가 곧 트리입니다(부모 `i`, 자식 `2i+1`, `2i+2`). 루트(노랑)는 항상 남겨 둔 것 중 가장 차가운 값입니다. 8개를 한 번씩 보면서 push / replace / skip을 하고, 마지막 두 칸은 heap sort로 정렬합니다.

<div class="chart" data-src="chart1.svg" data-caption="K = 3 힙의 변화. i = 4에서 id4(745)가 루트 id0(612)를 밀어내고, 같은 745인 id1보다 id가 커서 루트(더 차가운 쪽)에 앉음. i = 6에서 id6이 id4를 밀어내 결과는 id6·812, id3·801, id1·745"></div>

### 4.2 손으로 따라가기 (trace)

`heap`은 그 단계가 끝난 뒤의 `out[0..2]`이고, 표기는 `id:temp`입니다.

| i | reading | 비교 | 동작 | heap (out[0], out[1], out[2]) |
|---|---|---|---|---|
| 0 | (0, 612) | 크기 0 < 3 | push | 0:612 |
| 1 | (1, 745) | 크기 1 < 3 | push | 0:612, 1:745 |
| 2 | (2, 598) | 크기 2 < 3 | push, sift_up으로 루트 | **2:598**, 1:745, 0:612 |
| 3 | (3, 801) | 801 > 루트 598 | replace + sift_down | **0:612**, 1:745, 3:801 |
| 4 | (4, 745) | 745 > 루트 612 | replace, id4 > id1이라 루트 유지 | **4:745**, 1:745, 3:801 |
| 5 | (5, 690) | 690 < 루트 745 | skip | 4:745, 1:745, 3:801 |
| 6 | (6, 812) | 812 > 루트 745 | replace, 더 차가운 자식 1:745와 교환 | **1:745**, 6:812, 3:801 |
| 7 | (7, 701) | 701 < 루트 745 | skip | 1:745, 6:812, 3:801 |
| sort | end = 3 | 루트 ↔ out[2], sift_down(2) | 1:745 확정 | 3:801, 6:812, **1:745** |
| sort | end = 2 | 루트 ↔ out[1], sift_down(1) | 3:801 확정 | **6:812, 3:801, 1:745** |

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `sift_down`에서 `hotter(h[m], h[l])`이면 `m = l`: "지금 후보보다 자식이 더 차가우면 자식으로" — min-heap의 조건을 `hotter` 하나로 표현했습니다.
- `else if (k > 0 && …)`: `k == 0`이면 `out[0]`에 접근하면 안 되므로(`out == NULL`일 수 있음) 먼저 막습니다(테스트 2번).
- `size_t`는 부호가 없어서 `for (end = sz - 1; end >= 0; end--)`처럼 쓰면 무한 루프입니다. `end > 1`로 내려가며 `end - 1`을 인덱스로 씁니다.
- 결과 배열이 곧 힙이라 추가 메모리는 변수 몇 개뿐입니다.

### 4.4 복잡도

- 시간 **O(n log k)** — 각 reading마다 최대 한 번 sift(높이 log k), 마지막 정렬 O(k log k).
- 추가 메모리 **O(1)** — 힙이 `out` 안에 있음.
- 비교: 전체 정렬 O(n log n) + 복사본 O(n) 메모리, quickselect 평균 O(n)이지만 입력을 바꾸고 최악 O(n²).

---

## 5. 임베디드 관점으로 말할 것

- **왜 힙인가**: `n = 4096`, `k = 8`이면 전체 정렬은 4096개 복사본(16 KB)이 필요하지만, 힙은 `out` 8칸이면 됩니다. RAM이 작은 MCU에서 결정적입니다. k가 아주 작으면(≤ 4) 정렬된 배열에 삽입하는 O(nk)가 더 단순하고 빠를 수도 있다고 말하면 좋습니다.
- **스트리밍**: 같은 힙을 상태로 들고 있다가 센서 값이 들어올 때마다 replace/skip만 하면 "지금까지 최고 K개"를 항상 유지할 수 있습니다. 단 같은 센서가 값을 갱신하는 경우는 다른 문제입니다(Follow-up).
- **정수 온도**: `int16_t` 0.1 °C 비교라 FPU가 필요 없고, 동점 규칙이 명확해서 결과가 플랫폼과 무관하게 재현됩니다.
- **실행 시간 상한**: 최악은 매번 replace되는 오름차순 입력(테스트 6번)이고, 그때도 reading당 비교 2 log k번 정도라 제어 주기 예산을 계산하기 쉽습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Sensors update continuously; keep the top-K current. | 한 센서의 값이 바뀌면 힙 안 위치를 알아야 합니다. `pos[id]` 역참조 배열을 두고 값이 오르면 sift_down/내리면 sift_up 하거나, N이 작으면 주기마다 처음부터 다시 계산하는 편이 단순합니다. |
| K-th hottest only (LeetCode 215)? | 같은 힙을 만들고 마지막 정렬 없이 `out[0]`(루트)을 반환하면 됩니다. 입력을 바꿔도 되면 quickselect로 평균 O(n). |
| Top-K most frequent fault codes (LeetCode 347)? | 먼저 코드별 카운트 배열(코드 범위가 작으면 고정 배열)을 만들고, (count, code)에 같은 size-K min-heap을 적용합니다. 범위가 작으면 count별 버킷으로 O(n)도 가능합니다. |
| Why not sort all N readings? | O(n log n) 시간과 O(n) 복사본 메모리. 입력이 `const`라 제자리 정렬도 안 됩니다. K ≪ N이면 힙이 시간·메모리 모두 유리합니다. |
| What if k > n? | `min(n, k)`개만 채우고 그 개수를 반환합니다(테스트 3번). 호출자는 반환값만큼만 읽습니다. |

## 7. 자주 하는 실수

- max-heap에 전부 넣고 K번 꺼냄 → O(n) 메모리가 필요하고 "고정 배열 K칸" 조건 위반
- 동점 규칙을 `sift_up`과 `sift_down`에서 다르게 씀 → 테스트 1번(745 동점)이나 4번(전부 동점) 실패
- `k == 0`에서 `out[0]`을 읽음 → NULL 역참조(테스트 2번)
- heap sort를 빼고 힙 순서 그대로 반환 → 정렬 안 된 결과(테스트 1번)
- `size_t` 역방향 루프를 `>= 0`으로 끝냄 → 무한 루프
- 관련 문제: [T1-4](../T1-4_multi_sensor/index.html)(다중 센서 상태), [T2-4](../T2-4_window_max/index.html)(창 안 최댓값)
