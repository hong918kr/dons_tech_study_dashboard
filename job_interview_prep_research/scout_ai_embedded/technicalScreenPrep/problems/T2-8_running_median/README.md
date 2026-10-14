# T2-8 · Running Median Filter

> **ID**: T2-8 · **Priority**: P2 · **Pattern**: ring buffer + sorted window (insertion sort) · two heaps · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-8` / `./run.sh T2-8 sol`

## 1. Problem

A thermistor channel on a robot arm occasionally returns a single bogus reading — an ADC spike caused by motor PWM noise. A moving average smears the spike over the whole window, so filter the channel with the **median of the last `W` samples** instead (LeetCode 295 / 480 flavor).

- `med_init(m, w)`: `1 <= w <= MED_MAX_W` (15), otherwise return `false`.
- `med_push(m, x)`: add sample `x`, drop the oldest sample if the window already holds `w` samples, and return the median of the samples currently in the window.
- **Warm-up**: with fewer than `w` samples so far, use the samples available.
- **Even count**: return the **lower median**, `sorted[(count - 1) / 2]` — always a real sample, no averaging, no overflow.
- Values are `int16_t` in 0.1 °C. No `malloc`. O(W) per push is fine for small W.

```c
#define MED_MAX_W 15
typedef struct {
    int16_t ring[MED_MAX_W];     /* arrival order: tells us which sample is oldest */
    int16_t sorted[MED_MAX_W];   /* the same samples, ascending */
    uint8_t w, head, count;
} MedianFilter;

bool    med_init(MedianFilter *m, uint8_t w);
int16_t med_push(MedianFilter *m, int16_t x);
```

### Example

```
W = 5
push : 250 252 251 900 253 254 250 249 251
out  : 250 250 251 251 252 253 253 253 251

The 900 spike never reaches the output.
(A 5-sample mean would output 413.25, 381.2, 382, 381.6, 381.2 for i = 3..7.)
```

### Constraints

- `W` is small (≤ 15) and the filter runs once per sample in a control loop.
- The window may contain duplicates and any `int16_t` value, including `INT16_MIN` / `INT16_MAX`.

### Clarifying questions to ask (first 3 minutes)

1. What should be returned before the window is full?
2. For an even count, lower median, upper median, or the average of the two?
3. How large can `W` be? (decides insertion-sort window vs two heaps)
4. Is this a fixed window or the median of the whole stream so far?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-8`로 바로 테스트하세요. 테스트 6개가 들어 있고, 6번은 무작위 64샘플을 브루트포스 중앙값과 비교합니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 배열 두 개</summary>

정렬된 배열만 있으면 "가장 오래된 값"이 어느 것인지 모릅니다. 그래서 **도착 순서용 링버퍼(`ring`)**와 **정렬용 배열(`sorted`)**을 같이 둡니다. 링버퍼의 `head`가 다음에 덮어쓸 칸 = 창이 가득 찼을 때 가장 오래된 값입니다.

</details>

<details>
<summary>Hint 2 — push 한 번 = 삭제 한 번 + 삽입 한 번</summary>

1. 가득 찼으면 `old = ring[head]`를 `sorted`에서 찾아 **한 칸만** 지우고 뒤를 당깁니다.
2. `ring[head] = x`, `head = (head + 1) % w`.
3. `sorted` 끝에서부터 `x`보다 큰 값을 한 칸씩 오른쪽으로 밀고 빈자리에 `x`를 넣습니다(삽입 정렬 한 단계).
4. `sorted[(count - 1) / 2]`를 반환.

</details>

<details>
<summary>Hint 3 — 중복 값</summary>

`old`와 같은 값이 여러 개 있어도 **아무거나 하나만** 지우면 됩니다. 값이 같으면 어느 칸이든 정렬 결과가 같기 때문입니다. 전부 지우면 창 크기가 줄어 테스트 4번이 깨집니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

매 스텝 `ring`에는 한 칸(보라)만 바뀌고, `sorted`는 오래된 값 하나가 빠지고 새 값 하나가 끼어듭니다. 스파이크 900은 정렬하면 **항상 맨 끝**에 붙어 있으므로 가운데 칸(초록)에 올 수 없습니다.

<div class="chart" data-src="chart1.svg" data-caption="W = 5 예제의 스텝별 ring(도착 순서)과 sorted(정렬 사본). 중앙값 칸은 초록, 900은 창에 있는 동안 sorted[4]에 머묾"></div>

평균과 비교하면 차이가 분명합니다. 평균은 900이 창에 있는 5스텝 내내 380 이상으로 끌려 올라가지만, 중앙값은 250~253에 머뭅니다.

<div class="chart" data-src="chart2.svg" data-caption="같은 입력에 대한 5샘플 평균(빨강 점선)과 중앙값(초록). i = 3의 평균 413.25는 워밍업 중이라 샘플 4개의 평균"></div>

**규칙**: 중앙값은 창 안의 스파이크가 **절반 미만**(W = 5면 2개 이하)일 때까지 완전히 무시합니다. 3개 이상 연속이면 그건 노이즈가 아니라 실제 신호로 봅니다.

### 4.2 두 힙 방식 (끝없는 스트림)

LeetCode 295처럼 **창이 없고 전체 스트림의 중앙값**을 원하면 정렬 배열은 O(n)이라 느립니다. 이때는 힙 두 개를 씁니다.

- `lo`: 작은 절반을 담는 **max-heap**, `hi`: 큰 절반을 담는 **min-heap**.
- 새 값은 `lo`에 넣고, `lo`의 최댓값을 `hi`로 옮기고, `hi`가 더 커지면 다시 `hi`의 최솟값을 `lo`로 옮겨 `size(lo) == size(hi)` 또는 `+1`을 유지합니다.
- 중앙값은 `lo`의 꼭대기(홀수) 또는 두 꼭대기(짝수). push O(log n), 조회 O(1).
- **슬라이딩 창**(LeetCode 480)에 쓰려면 오래된 값을 힙에서 지워야 해서 "지연 삭제(lazy deletion) 해시맵"이나 인덱스 추적이 필요합니다. W ≤ 15 정도면 지금의 O(W) 배열이 더 빠르고 코드도 짧습니다.

### 4.3 손으로 따라가기 (trace)

| i | push | 빠지는 값 | sorted (push 후) | count | 반환 |
|---|---|---|---|---|---|
| 0 | 250 | – | 250 | 1 | sorted[0] = **250** |
| 1 | 252 | – | 250 252 | 2 | sorted[0] = **250** (lower) |
| 2 | 251 | – | 250 251 252 | 3 | sorted[1] = **251** |
| 3 | 900 | – | 250 251 252 900 | 4 | sorted[1] = **251** |
| 4 | 253 | – | 250 251 252 253 900 | 5 | sorted[2] = **252** |
| 5 | 254 | 250 | 251 252 253 254 900 | 5 | **253** |
| 6 | 250 | 252 | 250 251 253 254 900 | 5 | **253** |
| 7 | 249 | 251 | 249 250 253 254 900 | 5 | **253** |
| 8 | 251 | 900 | 249 250 251 253 254 | 5 | **251** |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- 삭제 루프 `while (m->sorted[i] != old) i++;`는 `old`가 반드시 들어 있으므로 범위를 넘지 않습니다. 방어적으로 쓰려면 `i < m->count` 조건을 함께 둡니다.
- 삽입은 `j = count`부터 왼쪽으로 이동하며 큰 값을 밀어냅니다. 같은 값 앞에서 멈추므로(`>`) 안정적입니다.
- 중앙값 인덱스 `(count - 1) / 2`가 홀수일 때 정확한 가운데, 짝수일 때 아래쪽 가운데입니다. 평균을 내지 않아서 `INT16_MAX + INT16_MAX` 같은 오버플로가 원천적으로 없습니다.

### 4.5 복잡도

- push **O(W)** — 찾기·당기기·밀기가 각각 최대 W. W = 5면 몇십 사이클입니다.
- 메모리 **O(W)** — `int16_t` 배열 두 개(최대 60바이트) + 3바이트.
- 두 힙(무한 스트림): push O(log n), 메모리 O(n).

---

## 5. 임베디드 관점으로 말할 것

- **왜 평균이 아니라 중앙값인가**: 평균(이동 평균, IIR)은 가우시안 노이즈에는 좋지만 **임펄스 노이즈(스파이크)**를 창 전체에 퍼뜨립니다. 과열 감지 직전에 스파이크 하나로 오경보가 나는 걸 막는 데는 중앙값이 맞습니다. 실무에서는 **중앙값(스파이크 제거) → 이동 평균(평활)** 2단으로 많이 씁니다(→ [T1-3](../T1-3_moving_average/index.html)).
- **지연**: W = 5 중앙값은 계단 변화에 (W − 1) / 2 = 2샘플 늦게 반응합니다. 과열 차단 경로라면 이 지연을 예산에 넣어야 합니다.
- **결정적 실행 시간**: O(W) 최악 시간이 고정이라 제어 루프 주기 안에 들어가는지 보장하기 쉽습니다. 힙 기반은 평균은 좋지만 코드가 길고 분기가 많습니다.
- **W = 3 특수화**: 가장 흔한 3-샘플 중앙값은 비교 3번(`max(min(a,b), min(max(a,b),c))`)으로 끝납니다. 면접에서 먼저 말하면 좋습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Median of the entire stream, not a window (LeetCode 295)? | 두 힙(`lo` max-heap, `hi` min-heap)으로 크기를 맞추며 넣습니다. push O(log n), 조회 O(1). 임베디드라면 고정 배열 힙 두 개에 최대 개수를 정해 둡니다. |
| W = 255 and 10 kHz sampling — too slow? | O(W) 이동이 병목이면 (1) 두 힙 + 지연 삭제, (2) 값 범위가 좁으면 **히스토그램(카운트 배열)** 중앙값: 값마다 개수를 세고 누적합으로 중앙값 위치를 찾습니다. ADC 12비트면 4096칸. |
| Return the average of the two middle values for even counts. | `(int32_t)a + b`로 넓혀서 더한 뒤 2로 나눕니다. `int16_t`끼리 더하면 int 승격 덕에 C에선 괜찮지만, 의도를 보이려고 명시적으로 넓힙니다. 반올림 방향(0 쪽 vs 아래쪽)도 합의합니다. |
| Can you avoid the linear search for the oldest value? | `sorted`에 값 대신 (값, ring 인덱스) 쌍을 넣고 ring 쪽에 sorted 위치를 역참조로 두면 찾기는 O(1)이지만, 당기기가 여전히 O(W)라 이득이 작습니다. |
| How do you test it? | 지금 테스트 6번처럼 의사 난수 입력을 브루트포스(창 복사 후 정렬) 결과와 비교하는 **오라클 테스트**가 가장 강력합니다. |

## 7. 자주 하는 실수

- `ring` 없이 `sorted`만 둠 → 어떤 값이 가장 오래됐는지 몰라 창이 틀어짐
- 오래된 값과 같은 값을 **전부** 지움 → 중복 테스트 4번 실패
- 삭제 전에 삽입부터 함 → 창이 가득 찼을 때 `sorted`가 `w + 1`칸을 써서 버퍼 오버런(ASan이 잡음)
- 짝수 개일 때 `count / 2`를 써서 위쪽 중앙값을 반환 → 테스트 1번의 i = 1, 3에서 실패
- `head = (head + 1) % MED_MAX_W`로 써서 실제 창 크기 `w` 대신 최대 크기로 돌림
