# T2-3 · Merge Overheat Intervals from Several Sensors

> **ID**: T2-3 · **Priority**: P1 · **Pattern**: sort + sweep merge (LeetCode 56) · **Time**: 20 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-3` / `./run.sh T2-3 sol`

## 1. Problem

Three temperature sensors on a robot arm each reported their own overheat intervals (for example from [T1-1](../T1-1_overheat_intervals/index.html)). The fault log wants **one** list that answers "when was *any* part of the arm overheating?" Merge all intervals **in place** and return the new count.

- `Interval {start_ms, end_ms}` is **closed**: both ends are included, and `start_ms <= end_ms`.
- The input is in no particular order (sensor A's list, then B's, then C's).
- Two intervals merge when they **overlap or touch**: `next.start_ms <= cur.end_ms`. So `[100,200]` and `[200,300]` become `[100,300]`, but `[100,200]` and `[201,300]` stay separate.
- The result is sorted by `start_ms` and written to `iv[0 .. return-1]`. `iv` may be `NULL` when `n == 0`.
- No `malloc`. `qsort` is allowed, but the comparator must not overflow.

```c
typedef struct { uint32_t start_ms, end_ms; } Interval;

size_t merge_intervals(Interval *iv, size_t n);
```

### Example

```
sensor A: [1000,1500] [4000,4200]
sensor B: [1400,2000] [5000,5000]
sensor C: [2000,2600] [4100,4150]

input  (as one array): [1000,1500] [4000,4200] [1400,2000] [5000,5000] [2000,2600] [4100,4150]
output: 3 intervals    [1000,2600] [4000,4200] [5000,5000]
```

### Constraints

- `0 <= n <= 10000`.
- Timestamps are any `uint32_t` value (no wraparound inside one log).

### Clarifying questions to ask (first 3 minutes)

1. Are the intervals closed or half-open? Do touching intervals merge?
2. Should a small gap (say < 50 ms) also be merged?
3. May I modify the input array in place, or do I write to a separate output?
4. Is the input already sorted by start (for example, each sensor's list is sorted)?
5. Is `qsort` available on the target, or should I write my own sort?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-3`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 먼저 정렬</summary>

`start_ms` 기준으로 정렬하면, 합쳐질 구간끼리는 반드시 **이웃**하게 됩니다. 그다음은 왼쪽에서 오른쪽으로 한 번 훑으면 됩니다.

</details>

<details>
<summary>Hint 2 — 쓰기 인덱스 하나</summary>

`w`를 "지금 키우고 있는 구간"의 인덱스로 둡니다. 다음 구간이 `iv[w].end_ms` 이하에서 시작하면 끝을 늘리고(`max` 주의), 아니면 `iv[++w]`에 복사합니다. 같은 배열에 덮어써도 `w <= i`라서 안전합니다.

</details>

<details>
<summary>Hint 3 — 비교 함수</summary>

`return a->start_ms - b->start_ms;`는 틀립니다. `uint32_t` 뺄셈 결과를 `int`로 바꾸면 차이가 2³¹ 이상일 때 부호가 뒤집힙니다. `(a > b) - (a < b)`를 쓰세요(테스트 5번).

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

세 센서의 구간을 한 시간축에 겹쳐 놓고, 하나라도 과열인 시간을 노란 띠로 칠하면 그게 답입니다.

<div class="chart" data-src="chart1.svg" data-caption="A·B·C 구간 6개가 3개로 합쳐집니다. 1400–2000과 2000–2600은 2000에서 맞닿아 합쳐지고, 4100–4150은 4000–4200 안에 들어가 사라집니다"></div>

### 4.2 "맞닿음"의 정의

닫힌 구간에서는 끝점이 같으면 그 순간 두 구간이 모두 과열입니다. 그래서 `<=`로 비교합니다. 1 ms라도 떨어져 있으면 별개입니다.

<div class="chart" data-src="chart2.svg" data-caption="next.start ≤ cur.end면 합치기. half-open [s, e) 구간이었다면 같은 식이 '맞닿으면 합친다'는 뜻이 됩니다"></div>

### 4.3 손으로 따라가기 (trace)

정렬 후 배열: `[1000,1500] [1400,2000] [2000,2600] [4000,4200] [4100,4150] [5000,5000]`

| i | iv[i] | 비교 | 동작 | w | iv[w] (키우는 중) |
|---|---|---|---|---|---|
| 0 | [1000,1500] | – | 시작 | 0 | [1000,1500] |
| 1 | [1400,2000] | 1400 ≤ 1500 | 겹침 → end = max(1500, 2000) | 0 | [1000,2000] |
| 2 | [2000,2600] | 2000 ≤ 2000 | **맞닿음** → end = 2600 | 0 | [1000,2600] |
| 3 | [4000,4200] | 4000 > 2600 | 새 구간 | 1 | [4000,4200] |
| 4 | [4100,4150] | 4100 ≤ 4200 | 안에 포함 → end 그대로 4200 | 1 | [4000,4200] |
| 5 | [5000,5000] | 5000 > 4200 | 새 구간 | 2 | [5000,5000] |

반환값 `w + 1 = 3`.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `if (iv[i].end_ms > iv[w].end_ms)` 없이 `iv[w].end_ms = iv[i].end_ms`로 쓰면 포함된 짧은 구간이 끝을 **줄여** 버립니다(테스트 4번 `[100,1000]` 안의 `[200,300]`).
- 비교 함수는 start가 같을 때 end로도 정렬합니다. 병합 결과에는 영향이 없지만 정렬 결과가 결정적(deterministic)이 되어 디버깅이 쉽습니다.
- `n == 0`을 먼저 처리해서 `iv == NULL`일 때 `qsort`나 `iv[0]`에 닿지 않습니다.

### 4.5 복잡도

- 시간 **O(n log n)** (정렬) + O(n) (병합).
- 추가 메모리 **O(1)** — 제자리 병합. 단 `qsort`의 내부 스택 사용량은 구현마다 다릅니다(§5).

---

## 5. 임베디드 관점으로 말할 것

- **입력이 이미 정렬된 경우**: 센서마다 목록이 시간순이면 전체 정렬 대신 **k-way merge**(센서 3개면 포인터 3개 중 최소 선택)로 O(n·k), 정렬 없이 처리할 수 있습니다. 실시간 경로에서는 이게 더 좋습니다.
- **`qsort` 비용**: newlib의 `qsort`는 재귀를 쓰고, 콜백 호출 오버헤드가 있습니다. 작은 n(수십 개)이면 삽입 정렬이 코드도 작고 스택도 예측 가능합니다. 이 판단을 말로 하면 좋습니다.
- **비교 함수 오버플로**: 펌웨어에서 타임스탬프는 대부분 `uint32_t`입니다. "빼서 비교" 습관이 여기서 실제 버그가 됩니다. [T1-1](../T1-1_overheat_intervals/index.html)의 wrap-safe 뺄셈(duration)과는 용도가 다르다는 점도 구분해서 말합니다: **길이**는 뺄셈, **순서**는 비교.
- **스트리밍 버전**: 새 구간이 시간순으로 들어온다면 마지막 구간 하나만 들고 있으면 됩니다(`start <= last.end`면 늘리기). 배열 정렬이 필요 없습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Merge intervals separated by a gap smaller than `gap_ms`. | 겹침 검사 `next.start <= cur.end`가 거짓일 때 한 번 더 `next.start - cur.end <= gap_ms`를 봅니다. 이때는 `next.start > cur.end`라서 뺄셈이 음수가 되지 않고, `cur.end + gap_ms` 같은 덧셈 오버플로도 없습니다. |
| Total time any sensor was overheating? | 병합 후 각 구간의 `end - start`를 더합니다(닫힌 구간에서 점 하나를 1 ms로 칠지 정의 필요). 64비트 누산. |
| The time when the most sensors were hot at once? | 시작을 +1, 끝을 −1 이벤트로 만들어 시각순 정렬 후 누적합의 최대 (sweep line). 같은 시각이면 닫힌 구간에서는 +1을 먼저 처리합니다. |
| Each sensor's list is already sorted. Can you avoid the full sort? | k-way merge로 O(n·k) 또는 작은 힙으로 O(n log k). k가 3이면 단순 비교가 가장 빠릅니다. |
| Insert one new interval into an already-merged list? | 이진 탐색([T2-5](../T2-5_lower_bound/index.html))으로 위치를 찾고, 겹치는 이웃만 합칩니다(LeetCode 57). |

## 7. 자주 하는 실수

- 정렬을 빼먹음 → 테스트 1번(센서별로 섞인 입력)에서 실패
- `<` 비교로 맞닿은 구간을 합치지 않음 → 테스트 3번 실패. 정의를 먼저 물어볼 것
- 끝을 `max` 없이 덮어씀 → 테스트 4번(포함된 구간) 실패
- 비교 함수에서 `uint32_t` 뺄셈 → 테스트 5번(큰 타임스탬프) 실패
- `n == 0`에서 `iv[0]` 접근 → NULL 역참조
