# T1-7 · Time Above Threshold

> **ID**: T1-7 · **Priority**: P1 · **Pattern**: segment walk + interval overlap + integer interpolation · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-7` / `./run.sh T1-7 sol`

## 1. Problem

You are given temperature samples sorted by time (timestamp in ms). Return the **total time inside the window `[t0_ms, t1_ms]`** during which the temperature was **`>= thresh`**. Support two models of what happens between samples:

- **`MODE_STEP`** (sample-and-hold): sample `i`'s value holds on `[t_i, t_{i+1})`.
- **`MODE_LERP`** (linear interpolation): the value moves on a straight line between consecutive samples; compute where the line crosses `thresh`.

Rules:

- The signal is known only between the first and the last sample. Window time outside that range counts as **not hot**.
- Return the result in **microseconds** (`uint64_t`). Round each crossing time to the nearest microsecond.
- Integer math only — no float. Multiply before divide; use 64-bit intermediates.
- Return 0 if `n < 2` or `t1_ms <= t0_ms`.

```c
typedef struct { uint32_t t_ms; int16_t temp; } Sample;
typedef enum { MODE_STEP, MODE_LERP } Mode;

uint64_t time_above_us(const Sample *s, size_t n, int16_t thresh,
                       uint32_t t0_ms, uint32_t t1_ms, Mode mode);
```

### Example

```
samples = (0,70) (10,90) (20,95) (30,60),  thresh = 80,  window = [0, 30]

MODE_STEP : hot on [10, 30)                        -> 20000 us  (20 ms)
MODE_LERP : 70->90 crosses at t = 5,
            95->60 crosses at t = 20 + 15*10/35 = 24.2857
            hot on [5, 24.286]                     -> 19286 us  (~19.29 ms)

window = [15, 25] -> STEP 10000 us, LERP 9286 us
```

### Constraints

- `0 <= n <= 100000`, timestamps strictly increasing, no wraparound.
- `temp` is any `int16_t`; timestamps are any `uint32_t` (up to ~49.7 days).

### Clarifying questions to ask (first 3 minutes)

1. Between samples, does the value hold (step) or change linearly? Or should I support both?
2. What if the window extends before the first or after the last sample — extrapolate, or count as no data?
3. What resolution do you want back — ms (truncating 19.29 → 19) or something finer?
4. Is "above" `>=` or `>`? (For linear interpolation it only matters for flat segments exactly at `thresh`.)
5. Are the window bounds inclusive? (For a time *length* the endpoints have zero width, so it does not matter.)

## 2. Your workspace

`./run.sh T1-7`로 바로 테스트하세요. 테스트 6묶음이 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 샘플이 아니라 "구간(segment)"을 돌아라</summary>

`i`와 `i+1` 사이 구간 `[ta, tb]` 하나마다 "이 구간에서 과열인 부분 `[ha, hb]`"를 구하고, 그것과 윈도우 `[lo, hi]`의 **겹침 길이** `max(0, min(hb,hi) − max(ha,lo))`를 더합니다. 윈도우 처리는 이 한 줄이 전부입니다.

</details>

<details>
<summary>Hint 2 — LERP에서 구간 하나의 경우는 4개</summary>

- 둘 다 hot → 구간 전체
- 둘 다 cool → 0
- cool → hot (상승) → `[tc, tb]`
- hot → cool (하강) → `[ta, tc]`

직선이라 교차점은 많아야 1개입니다. `ha != hb`일 때만 교차점을 계산하므로 `vb − va`가 0이 될 일이 없습니다.

</details>

<details>
<summary>Hint 3 — 교차 시각을 정수로</summary>

`tc = ta + (thresh − va) × (tb − ta) / (vb − va)`. **곱셈 먼저**, 나눗셈은 마지막에 한 번. 시간을 µs로 바꾸면 `(tb − ta)`가 최대 4.3×10¹², 온도 차가 최대 65535라 곱은 2.8×10¹⁷ → `int64_t`에 들어갑니다. 분모가 음수면 분자·분모 부호를 같이 뒤집은 뒤 `(num + den/2) / den`으로 반올림합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 두 모델을 한 그림에

<div class="chart" data-src="chart1.svg" data-caption="노랑 = STEP에서 과열인 영역(10~30), 보라 = LERP에서 과열인 영역(5~24.286). 같은 데이터인데 답이 다릅니다"></div>

두 모델은 **"샘플 사이에서 무슨 일이 있었다고 가정하느냐"**의 차이입니다.

- STEP: 센서 값이 다음 측정까지 유지된다고 봅니다. 샘플 `i`만 보면 구간의 hot 여부가 결정되므로 교차점 계산이 없습니다. ZOH(zero-order hold)와 같습니다.
- LERP: 온도는 연속적으로 변하니 직선으로 잇습니다. 물리적으로 더 그럴듯하지만 나눗셈이 필요합니다.

어느 쪽이 "정답"인지는 문제 정의입니다. 첫 질문으로 정하고, 시간이 남으면 둘 다 구현합니다.

### 4.2 손으로 따라가기 (trace) — window [0, 30], µs 단위

| seg | (ta,va) → (tb,vb) | STEP | LERP 경우 | 교차 tc 계산 | LERP hot 구간 | LERP 겹침 |
|---|---|---|---|---|---|---|
| 0 | (0,70) → (10000,90) | cool → 0 | 상승 | (10×10000 + 10) / 20 = 5000 | [5000, 10000] | 5000 |
| 1 | (10000,90) → (20000,95) | hot → 10000 | 둘 다 hot | – | [10000, 20000] | 10000 |
| 2 | (20000,95) → (30000,60) | hot → 10000 | 하강 | num = −15×10000, den = −35 → (150000 + 17) / 35 = 4286 → tc = 24286 | [20000, 24286] | 4286 |
| 합 | | **20000** | | | | **19286** |

window `[15, 25]` → `lo = 15000, hi = 25000`:
- STEP: seg1 `[10000,20000]∩` = 5000, seg2 `[20000,30000]∩` = 5000 → **10000**
- LERP: seg0 은 `tb = 10000 <= lo`라 건너뜀, seg1 = 5000, seg2 `[20000,24286]∩` = 4286 → **9286**

window `[25, 100]`: 데이터가 30 ms에서 끝나므로 30~100은 0. STEP = 25000~30000 = **5000**, LERP = 24286에서 이미 식었으므로 **0**.

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `overlap()` 헬퍼 하나가 윈도우 부분 겹침, 윈도우 밖, 빈 구간을 모두 처리합니다. 경계 케이스를 `if`로 늘어놓지 않는 게 핵심입니다.
- 시간을 처음부터 `int64_t` µs로 바꿔 계산합니다. `uint32_t` ms × 1000은 32비트를 넘으므로 **캐스팅을 곱셈 전에** 해야 합니다: `(int64_t)s[i].t_ms * 1000`.
- `crossing_us()`의 `int32_t va, vb, th`: `int16_t` 차이(최대 65535)는 `int16_t`에 안 들어가므로 넓혀서 받습니다.
- STEP 모드에서 마지막 샘플은 "길이 0"입니다(다음 샘플이 없음). 마지막 샘플 이후를 연장하고 싶다면 그건 정의를 바꾸는 것이니 질문으로 확인합니다.
- 반올림: `(num + den/2) / den`은 `num`과 `den`의 부호가 같을 때만 맞습니다. 실제 교차에서는 항상 같은 부호(하강 구간은 둘 다 음수)지만, 먼저 `den > 0`으로 정규화해 두면 읽는 사람이 따로 증명할 필요가 없습니다.

### 4.4 복잡도

- 시간 **O(n)**, 메모리 **O(1)**.
- 윈도우가 데이터보다 훨씬 작으면 `lower_bound`(이진 탐색)로 `t0`가 들어 있는 구간부터 시작해 **O(log n + k)**로 줄일 수 있습니다 → [T2-5](../T2-5_lower_bound/index.html).

---

## 5. 임베디드 관점으로 말할 것

- **단위 선택이 정밀도**: ms로 돌려주면 19.29 ms가 19로 잘립니다. 출력 단위를 µs로 바꾸거나(이 풀이), "ms × 1000 고정소수점"이라고 부르면 FPU 없이 소수점 3자리를 유지합니다. 단위를 API 이름(`_us`)에 넣어 오해를 막습니다.
- **오버플로 예산을 말로**: "온도 차 최대 2¹⁶, 시간 최대 2³² ms = 2⁴² µs, 곱은 2⁵⁸ < 2⁶³이라 int64로 안전합니다." 이렇게 비트 수로 근거를 대면 설득력이 있습니다.
- **32비트 MCU에서 64비트 나눗셈은 비싸다**: Cortex-M에는 64비트 나눗셈 명령이 없어 라이브러리 호출(`__aeabi_ldivmod`)이 됩니다. 구간 길이가 짧다는 게 보장되면(예: 샘플 간격 ≤ 1 s → 10⁶ µs × 65535 < 2³⁷) 여전히 64비트가 필요하지만, ms 단위·온도 범위 제한으로 32비트에 맞출 수도 있다고 말할 수 있습니다.
- **스트리밍 누적**: 이 함수는 배치지만, 실제 장치에서는 샘플이 올 때마다 직전 샘플과의 구간 하나만 계산해 누적합니다. 상태는 직전 샘플 1개 + 누적값 → O(1).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Which model is more accurate? | 실제 온도는 연속이므로 보통 LERP가 더 가깝습니다. 단, 샘플 간격이 길고 사이에 짧은 스파이크가 있었다면 둘 다 놓칩니다. 결국 샘플링 주기가 열 시상수보다 충분히 짧아야 합니다. |
| What if there is a long gap (dropout) between two samples? | 간격이 `max_gap`보다 큰 구간은 "데이터 없음"으로 보고 0으로 치거나 별도로 보고합니다. 공백 동안을 직선으로 이으면 근거 없는 추정입니다 → [T1-6](../T1-6_sensor_fault_detect/index.html). |
| Many queries over the same data with different windows? | 구간별 hot 길이의 누적합(prefix sum) 배열을 만들면, 질의마다 양 끝 부분 구간 2개만 따로 계산 + 이진 탐색으로 O(log n). |
| Can you return milliseconds as fixed-point instead? | 같은 값입니다. µs 정수 = "ms × 1000 고정소수점". 표시할 때 `v / 1000`, `v % 1000`으로 `19.286`을 출력합니다. |
| Timestamps can wrap around. | 샘플 시각을 첫 샘플 기준 상대 시간(`(uint32_t)(t - t_first)`)으로 바꾼 뒤 같은 알고리즘을 씁니다. 전체 길이가 49.7일 미만이면 안전합니다. |

## 7. 자주 하는 실수

- `s[i].t_ms * 1000`을 `uint32_t`로 계산 → 4,294,967 ms 이후 넘침 (테스트 6번 실패)
- 교차점을 `(thresh − va) / (vb − va) * (tb − ta)`처럼 **나눗셈 먼저** → 정수 나눗셈에서 0이 됨 (테스트 1번 LERP 실패)
- 윈도우를 샘플 단위로 자름(`t0` 이상인 첫 샘플부터 시작) → 그 앞 구간의 일부를 놓침 (테스트 2번 실패)
- 마지막 hot 구간을 데이터 끝(30 ms)이 아니라 윈도우 끝까지 연장 → `[25, 100]` STEP이 75000이 됨 (테스트 3번 실패)
- 교차 시각을 반올림 없이 버림(`num / den`) → 4285, 합이 19285가 됨 (테스트 1번 LERP 실패). 반올림 규칙도 질문으로 정합니다
