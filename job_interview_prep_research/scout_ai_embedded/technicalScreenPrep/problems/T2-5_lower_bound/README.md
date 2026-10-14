# T2-5 · Find a Sample by Time

> **ID**: T2-5 · **Priority**: P1 · **Pattern**: half-open binary search (lower_bound / upper_bound, LeetCode 35 / 34) · **Time**: 20 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-5` / `./run.sh T2-5 sol`

## 1. Problem

The robot's flight recorder keeps a time-sorted array of temperature samples. After an incident an engineer asks: "what was the temperature at `t = 300` ms?" Implement the two lookups below with **binary search**.

- Timestamps are **non-decreasing**. Duplicates are allowed: two samples may share the same `t_ms`.
- `first_at_or_after(s, n, t)`: index of the **first** sample with `t_ms >= t`, or `n` if there is none (this is `lower_bound`).
- `last_at_or_before(s, n, t)`: index of the **last** sample with `t_ms <= t`, or `NOT_FOUND` if there is none (this is `upper_bound - 1`).
- O(log n). `s` may be `NULL` when `n == 0`. The midpoint computation must not overflow.

```c
#define NOT_FOUND SIZE_MAX
typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;   /* temp in 0.1 degC */

size_t first_at_or_after(const Sample *s, size_t n, uint32_t t);
size_t last_at_or_before(const Sample *s, size_t n, uint32_t t);
```

### Example

```
t_ms  = [100, 200, 200, 200, 350, 500, 800]      (index 0..6, n = 7)

first_at_or_after(300) = 4      last_at_or_before(300) = 3
first_at_or_after(200) = 1      last_at_or_before(200) = 3    (duplicates)
first_at_or_after(50)  = 0      last_at_or_before(50)  = NOT_FOUND
first_at_or_after(900) = 7 (n)  last_at_or_before(900) = 6
```

### Constraints

- `0 <= n <= 10^6`, `t` is any `uint32_t` (including `0` and `UINT32_MAX`).
- Timestamps do not wrap inside one recording.

### Clarifying questions to ask (first 3 minutes)

1. Can timestamps repeat? If so, which one do you want — first or last?
2. What should I return when no sample qualifies — `n`, `-1`, or a flag?
3. For "temperature at t", do you want the sample at or before `t`, the nearest one, or linear interpolation?
4. Is the array strictly sorted, or could it have small out-of-order glitches?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-5`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 범위를 [lo, hi)로 잡기</summary>

`lo = 0, hi = n`으로 시작하고 `while (lo < hi)`로 돕니다. 답은 항상 `[lo, hi]` 안에 있고, 루프가 끝나면 `lo == hi`가 답입니다. `hi = n`이므로 "없음"도 자연스럽게 `n`으로 나옵니다.

</details>

<details>
<summary>Hint 2 — 조건 하나로 둘로 나누기</summary>

배열을 "조건이 거짓인 앞부분 | 참인 뒷부분"으로 봅니다. lower_bound의 조건은 `t_ms >= t`. `mid`가 거짓이면 답은 오른쪽(`lo = mid + 1`), 참이면 `mid` 또는 왼쪽(`hi = mid`). `mid - 1`이나 `<=` 루프 조건은 필요 없습니다.

</details>

<details>
<summary>Hint 3 — 마지막 ≤ t는?</summary>

"첫 번째 `t_ms > t`"(upper_bound)를 같은 방법으로 찾고 1을 뺍니다. 비교만 `<`에서 `<=`로 바뀝니다. 결과가 0이면 `NOT_FOUND`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — lo / hi / mid 따라가기

`first_at_or_after(300)`을 그리면 보라색 범위 `[lo, hi)`가 매 단계 절반으로 줄고, `lo == hi`가 되는 곳이 답입니다. 점선 칸 `[7]`은 실제 원소가 아니라 "없음 = n" 자리입니다.

<div class="chart" data-src="chart1.svg" data-caption="3단계 만에 lo = hi = 4. 노란 칸이 mid. hi는 항상 '아직 후보인 칸의 바로 다음'을 가리킵니다"></div>

### 4.2 같은 배열, 네 가지 질문

중복 시각(200 × 3)과 양 끝 밖의 `t`에서 두 함수가 어떻게 다른지 봅니다.

<div class="chart" data-src="chart2.svg" data-caption="t가 배열 안의 값이면 first ≤ last, 값 사이면 first = last + 1. 양 끝 밖에서는 각각 n, NOT_FOUND"></div>

### 4.3 손으로 따라가기 (trace)

**`first_at_or_after(t = 200)`** — 중복 중 첫 번째를 찾습니다.

| 단계 | lo | hi | mid = lo + (hi − lo) / 2 | t[mid] | t[mid] < 200? | 다음 |
|---|---|---|---|---|---|---|
| 1 | 0 | 7 | 3 | 200 | ✗ | `hi = 3` |
| 2 | 0 | 3 | 1 | 200 | ✗ | `hi = 1` |
| 3 | 0 | 1 | 0 | 100 | ✓ | `lo = 1` |
| 끝 | 1 | 1 | | | | 반환 **1** |

**`last_at_or_before(t = 200)`** — 첫 번째 `t_ms > 200`을 찾고 1을 뺍니다.

| 단계 | lo | hi | mid | t[mid] | t[mid] ≤ 200? | 다음 |
|---|---|---|---|---|---|---|
| 1 | 0 | 7 | 3 | 200 | ✓ | `lo = 4` |
| 2 | 4 | 7 | 5 | 500 | ✗ | `hi = 5` |
| 3 | 4 | 5 | 4 | 350 | ✗ | `hi = 4` |
| 끝 | 4 | 4 | | | | upper = 4 → 반환 **3** |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `mid = lo + (hi - lo) / 2`: `(lo + hi) / 2`는 `lo + hi`가 타입 범위를 넘을 수 있습니다(Java `binarySearch`의 유명한 버그). `size_t`에서는 현실적으로 드물지만 `uint16_t` 인덱스를 쓰는 MCU 코드에서는 실제로 납니다.
- `lo < hi`인 동안 `mid < hi`가 항상 성립하므로 `s[mid]`는 범위 안입니다. `n == 0`이면 루프에 들어가지 않아 `s == NULL`도 안전합니다.
- 두 함수는 비교 연산자 하나(`<` vs `<=`)만 다릅니다. 면접에서는 하나를 쓰고 "upper_bound는 여기만 바뀐다"고 말하면 시간이 절약됩니다.
- 테스트 6번은 0부터 900까지 모든 `t`를 선형 탐색과 비교합니다. off-by-one을 확실히 잡는 습관입니다.

### 4.5 복잡도

- 시간 **O(log n)** — n = 10⁶이면 비교 20번.
- 추가 메모리 **O(1)**, 재귀 없음.

---

## 5. 임베디드 관점으로 말할 것

- **플래시 로그 검색**: 샘플이 외부 플래시에 있으면 비교 한 번이 SPI 읽기 한 번입니다. 이진 탐색이면 4 MB 로그(8 B 레코드 52만 개)도 약 19번 읽기로 끝납니다. 고정 크기 레코드라 `addr = base + mid * sizeof(rec)`로 바로 접근할 수 있다는 점이 핵심입니다.
- **ring 형태 로그**: 순환 버퍼에 기록된 로그는 "회전된 정렬 배열"입니다. 가장 오래된 위치(tail)를 알면 논리 인덱스 `(tail + i) & mask`로 바꿔서 같은 코드를 씁니다([T2-1](../T2-1_ring_buffer/index.html)).
- **보간**: "t에서의 온도"는 `last_at_or_before`와 그다음 샘플로 정수 선형 보간을 할 수 있습니다. `int32_t`로 곱한 뒤 나누기(오버플로 범위 확인).
- **시각 랩어라운드**: 장시간 로그에서 `uint32_t` ms 카운터는 49.7일마다 넘어갑니다. 그러면 배열이 더 이상 정렬이 아니므로 64비트 시각이나 epoch 번호를 함께 저장합니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How many samples fall in `[t0, t1]`? | `upper_bound(t1) - lower_bound(t0)`. 즉 `last_at_or_before(t1) + 1 - first_at_or_after(t0)`이고, 음수가 되지 않게 `t0 <= t1`을 먼저 확인합니다. |
| Return the sample nearest to `t`. | `i = first_at_or_after(t)`를 구하고 `i`와 `i - 1` 중 차이가 작은 쪽. 경계(`i == 0`, `i == n`) 처리 필수. |
| Why not `while (lo <= hi)` with `hi = n - 1`? | 그 방식도 되지만 `n == 0`에서 `hi = SIZE_MAX`가 되고, `hi = mid - 1`에서 `mid == 0`이면 언더플로가 납니다. 반열린 구간은 부호 없는 인덱스와 잘 맞습니다. |
| The log is in external flash and each read costs 50 µs. | 비교 횟수 ⌈log₂ n⌉가 곧 비용입니다. 첫 몇 단계를 위한 작은 RAM 인덱스(예: 4 KB마다 첫 시각)를 두면 플래시 읽기를 더 줄일 수 있습니다. |
| What if the array is a rotated sorted array (circular log)? | 최소 원소 위치를 먼저 이진 탐색으로 찾거나(LeetCode 153), tail을 이미 알면 논리 인덱스로 변환해 같은 함수를 씁니다. |

## 7. 자주 하는 실수

- `(lo + hi) / 2` → 큰 인덱스에서 오버플로(특히 16비트 인덱스)
- `hi = n - 1`로 시작 → `n == 0`에서 `size_t` 언더플로, 그리고 "없음(n)"을 반환할 수 없음(테스트 3, 5번)
- 같은 값을 찾으면 바로 `return mid` → 중복 중 아무거나 반환(테스트 2번)
- `lo = mid` (+1 없이) → `hi = lo + 1`일 때 무한 루프
- `last_at_or_before`에서 `lo - 1`을 `lo == 0`일 때 그대로 반환 → `SIZE_MAX`가 우연히 맞아 보이지만 의도가 불분명. 명시적으로 `NOT_FOUND`를 쓸 것
