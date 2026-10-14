# T1-3 · Moving Average and Rise-Rate Overheat

> **ID**: T1-3 · **Priority**: P0 · **Pattern**: ring buffer + running sum + integer rounding · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-3` / `./run.sh T1-3 sol`

## 1. Problem

A temperature sensor is noisy, so the firmware smooths it with a **moving average of the last `WIN` samples** before deciding anything. Temperatures are `int16_t` in tenths of a degree Celsius.

1. Maintain the moving average in **O(1) per sample** (no re-summing the window). Use the struct below.
2. While the window is still filling, average over the samples you have (`count`), not over `WIN`.
3. Return the average as an integer, **rounded half away from zero** (`7.5 → 8`, `-7.5 → -8`).
4. Write `ma_overheat()`: overheat if the **average** `>= avg_thresh` **or** the **rise** across the window (newest sample − oldest sample still in the window) `>= rise_thresh`. An empty window is never overheated.
5. No `malloc`, no floating point.

```c
#define WIN 4      /* 8 in production; 4 keeps tests small */
typedef struct { int16_t buf[WIN]; uint8_t idx, count; int32_t sum; } MovAvg;

void    ma_init(MovAvg *m);
int16_t ma_avg(const MovAvg *m);                 /* 0 if empty */
int16_t ma_push(MovAvg *m, int16_t x);           /* returns the new average */
bool    ma_overheat(const MovAvg *m, int16_t avg_thresh, int16_t rise_thresh);
```

### Example

```
WIN = 4
push:    10   20   30   40   50
returns: 10   15   20   25   35      (last window = 20, 30, 40, 50)

after the 5th push: avg = 35, rise = 50 - 20 = 30
ma_overheat(m, 36, 30) -> true   (rise)
ma_overheat(m, 36, 31) -> false

rounding: push -10, -5  ->  -15/2 = -7.5  ->  -8   (plain C division gives -7)
```

### Constraints

- `WIN <= 255` (fits `uint8_t`); samples are any `int16_t` value, including `INT16_MIN` / `INT16_MAX`.
- `ma_push` must be O(1); memory is the struct only.

### Clarifying questions to ask (first 3 minutes)

1. Before the window is full, do we average over `count` or over `WIN`? Do we report anything at all?
2. How should the average be rounded — truncate, floor, or round to nearest? What about negative temperatures?
3. Is "rise" newest − oldest in the window, or a slope per second?
4. What range can samples take? (Decides the type of `sum`.)
5. Should the overheat decision wait until the window is full?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 로컬에서 `./run.sh T1-3`으로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — O(1)의 비결은 누적합</summary>

매번 `WIN`개를 다시 더하지 말고 `sum`을 들고 다닙니다. 새 값이 들어오면 **나갈 값(가장 오래된 값)을 먼저 빼고**, 새 값을 더한 뒤 그 칸에 덮어씁니다. 윈도우가 덜 찼으면 빼지 않고 `count++`만 합니다.

</details>

<details>
<summary>Hint 2 — idx 하나로 newest와 oldest를 둘 다 찾기</summary>

`idx`는 "다음에 쓸 칸"입니다. 그래서 **newest = `buf[(idx + WIN - 1) % WIN]`**, **oldest = `buf[(idx + WIN - count) % WIN]`**. 꽉 찼으면 oldest는 `buf[idx]`와 같고, 덜 찼으면 `buf[0]`입니다. 이 한 식으로 두 경우가 다 됩니다.

</details>

<details>
<summary>Hint 3 — 음수 반올림</summary>

C의 `/`는 0 쪽으로 자릅니다(`-15/2 == -7`). `(sum + cnt/2) / cnt`는 양수에서만 맞습니다. 부호로 나눠서 음수는 절댓값으로 반올림한 뒤 부호를 다시 붙이세요: `-((-sum + cnt/2) / cnt)`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

링버퍼 칸 4개, 다음에 쓸 위치 `idx`, 지금 들어 있는 개수 `count`, 합 `sum` 네 가지만 보면 됩니다. 다섯 번째 push에서 처음으로 **빼기(evict)**가 일어납니다.

<div class="chart" data-src="chart1.svg" data-caption="WIN=4에서 10, 20, 30, 40, 50을 넣을 때의 링버퍼. 보라 삼각형이 idx(다음에 쓸 칸). 다섯 번째 push에서 buf[0]의 10을 sum에서 빼고 50으로 덮어씀"></div>

### 4.2 반올림 규칙

평균은 `int16_t`로 돌려줘야 하니 정수 나눗셈의 반올림 규칙을 정해야 합니다. 여기서는 **round half away from zero**로 정했습니다. 양수와 음수가 대칭이라 영하 온도에서 평균이 한쪽으로 치우치지 않습니다.

<div class="chart" data-src="chart2.svg" data-caption="C의 '/'는 0 쪽으로 잘라서 −7.5 → −7, 7.5 → 7. 우리 규칙은 0에서 멀어지는 쪽으로 −8, 8. 양수 공식을 음수에 그대로 쓰면 −7이 나오는 함정"></div>

### 4.3 손으로 따라가기 (trace)

`WIN = 4`

| push | 동작 | buf[0..3] | idx(후) | count | sum | avg |
|---|---|---|---|---|---|---|
| 10 | count++ | 10 · – · – · – | 1 | 1 | 0 + 10 = 10 | 10/1 = **10** |
| 20 | count++ | 10 · 20 · – · – | 2 | 2 | 10 + 20 = 30 | 30/2 = **15** |
| 30 | count++ | 10 · 20 · 30 · – | 3 | 3 | 30 + 30 = 60 | 60/3 = **20** |
| 40 | count++ | 10 · 20 · 30 · 40 | 0 | 4 | 60 + 40 = 100 | 100/4 = **25** |
| 50 | **evict buf[0]=10** | 50 · 20 · 30 · 40 | 1 | 4 | 100 − 10 + 50 = 140 | 140/4 = **35** |

마지막 상태에서 `ma_overheat`: newest = `buf[(1+3)%4]` = `buf[0]` = 50, oldest = `buf[(1+4−4)%4]` = `buf[1]` = 20 → rise = 30, avg = 35.

| 호출 | avg ≥ ? | rise ≥ ? | 결과 |
|---|---|---|---|
| `ma_overheat(m, 35, 100)` | 35 ≥ 35 ✓ | 30 ≥ 100 ✗ | true |
| `ma_overheat(m, 36, 31)` | ✗ | 30 ≥ 31 ✗ | false |
| `ma_overheat(m, 36, 30)` | ✗ | 30 ≥ 30 ✓ | true |

반올림 테스트 값들:

| 입력 | sum / count | 정확한 값 | C `/` | 우리 규칙 |
|---|---|---|---|---|
| 1, 2 | 3 / 2 | 1.5 | 1 | **2** |
| 1, 2, 2 | 5 / 3 | 1.67 | 1 | **2** |
| 1, 2, 2, 0 | 5 / 4 | 1.25 | 1 | **1** |
| −10, −5 | −15 / 2 | −7.5 | −7 | **−8** |
| −1, −2, −2 | −5 / 3 | −1.67 | −1 | **−2** |
| 32767 ×3, −32768 | 65533 / 4 | 16383.25 | 16383 | **16383** |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `if (count == WIN) sum -= buf[idx]; else count++;` — 덮어쓰기 **전에** 빼야 합니다. 순서를 바꾸면 방금 넣은 값을 뺍니다.
- `sum`이 `int32_t`인 이유: `int16_t` 8개의 합은 최대 ±262,144라서 `int16_t`로는 넘칩니다. `int32_t`면 `WIN`이 65,536까지 안전합니다.
- `rise`도 `int32_t`로 계산합니다. `32767 - (-32768) = 65535`는 `int16_t` 범위를 넘습니다(테스트 6번 마지막 줄).
- 반올림된 평균은 항상 윈도우의 최솟값과 최댓값 사이 정수라서 `int16_t`로 캐스팅해도 안전합니다.
- `% WIN`은 `WIN`이 상수라 컴파일러가 최적화합니다. 2의 거듭제곱이면 `& (WIN - 1)`과 같습니다.

### 4.5 복잡도

- `ma_push`, `ma_avg`, `ma_overheat` 모두 **O(1)** 시간.
- 메모리 **O(WIN)** — `WIN=8`이면 `buf` 16바이트 + `idx`·`count` 2바이트 + `sum` 4바이트(패딩 포함 약 24바이트).

---

## 5. 임베디드 관점으로 말할 것

- **FPU 없이 정수만**: 평균·반올림·상승률 모두 정수 연산입니다. Cortex-M0에는 하드웨어 나눗셈도 없으니, `WIN`을 2의 거듭제곱으로 잡아 꽉 찬 뒤에는 시프트로 나누는 방법도 말할 수 있습니다(음수 시프트는 floor라 반올림을 따로 처리).
- **오버플로 범위를 말로 계산하기**: "int16 최대 32767 × 8 = 262,136, int32로 충분"이라고 숫자로 말하면 신뢰가 갑니다.
- **필터 지연**: 이동 평균은 `WIN`/2 샘플 정도 늦게 반응합니다. 그래서 **평균**은 노이즈에 강한 느린 판정, **rise**는 급상승을 빨리 잡는 판정으로 둘을 OR로 묶습니다. 이 트레이드오프를 설명하면 좋습니다.
- **덜 찬 윈도우의 정책**: 부팅 직후 샘플 1개로 평균 판정을 할지, `count == WIN`까지 기다릴지는 안전 요구사항으로 정할 일입니다. 여기서는 바로 판정합니다(과열은 빨리 잡는 쪽이 안전).
- 스트리밍 감지기와 결합하면 "평균이 N회 연속 임계 이상" 같은 판정이 됩니다 → [T1-2](../T1-2_streaming_hysteresis/index.html). 링버퍼 자체는 [T2-1](../T2-1_ring_buffer/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| `WIN` is a power of two. Replace the division with a shift. | 꽉 찬 뒤에는 `sum >> 3`(WIN=8)이 가능하지만, 음수에서 `>>`는 floor(−7.5 → −8, −7.25 → −8)이고 구현 정의 동작입니다. 반올림까지 하려면 `(sum + 4) >> 3`은 양수에서만 맞으니 부호 처리를 똑같이 해야 합니다. 덜 찬 동안은 여전히 `count`로 나눕니다. |
| Compare with an exponential moving average. | EMA: `avg += (x - avg) >> k`. 메모리 1워드, 버퍼 없음, 나눗셈 없음. 대신 오래된 값이 완전히 사라지지 않고, 정밀도를 위해 `avg`를 고정소수점(`<< k` 스케일)으로 들고 있어야 합니다. 정확한 "최근 N개 평균"이나 "최근 N개 중 가장 오래된 값"이 필요하면 링버퍼가 맞습니다. |
| Make the rise a rate in 0.1 °C per second. | 샘플마다 타임스탬프도 링버퍼에 넣고 `rise * 1000 / (t_newest - t_oldest)`를 계산합니다. 곱셈은 `int32_t`로, 시간차는 `uint32_t` 뺄셈(랩 안전), 0으로 나누기 검사. 실제로는 나눗셈 대신 `rise * 1000 >= R * dt`로 비교하면 더 빠르고 안전합니다. |
| What if `ma_push` runs in an ISR and `ma_avg` in the main loop? | `sum`(32비트)과 `count`를 따로 읽으면 중간에 끊긴 값을 볼 수 있습니다. 짧게 인터럽트를 막고 복사하거나, ISR이 계산된 평균 하나만 `volatile int16_t`로 내보내게 합니다. |
| Running max or median instead of mean? | 이동 최댓값은 단조 deque로 O(1) 분할상환 → [T2-4](../T2-4_window_max/index.html). 중앙값은 작은 `WIN`이면 정렬된 배열 삽입, 크면 힙 두 개 → [T2-8](../T2-8_running_median/index.html). |

## 7. 자주 하는 실수

- 덜 찬 윈도우에서 `WIN`으로 나눔 → 첫 출력이 10이 아니라 3(10/4 = 2.5 → 반올림), 테스트 1번 실패
- 덮어쓴 **뒤에** `sum -= buf[idx]` → 새 값을 빼서 합이 틀어짐
- `sum`을 `int16_t`로 선언 → 극값 테스트 4번 실패(오버플로는 UB라 sanitizer가 잡기도 함)
- 음수에 `(sum + cnt/2) / cnt` → `-15/2`가 −7, 테스트 3번 실패
- oldest를 항상 `buf[idx]`로 읽음 → 덜 찬 윈도우에서는 빈 칸(0)을 읽어서 rise가 틀림, 테스트 6번 실패
- `int16_t rise = newest - oldest;` → 65535가 −1로 바뀌어 과열을 놓침
