# T1-6 · Sensor Fault Detection

> **ID**: T1-6 · **Priority**: P1 · **Pattern**: single pass + previous-sample state + bitmask flags · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-6` / `./run.sh T1-6 sol`

## 1. Problem

A temperature sensor streams samples (timestamp in ms, temperature in 0.1 °C). Before deciding "overheat", you must reject readings that come from a **faulty sensor**. Classify every sample with a **bitmask** of faults — several faults can apply to the same sample:

| Flag | Rule for sample `i` |
|---|---|
| `F_RANGE` | `temp < lo` or `temp > hi` (physically impossible, e.g. outside −40.0 … 150.0 °C) |
| `F_DROPOUT` | `i > 0` and `t[i] - t[i-1] > 2 * period_ms` (flag the first sample **after** the gap) |
| `F_SPIKE` | `\|temp - ref\| > max_step`, where `ref` is the last sample that was neither `F_RANGE` nor `F_SPIKE`. Skip this check if the sample is `F_RANGE` or `F_DROPOUT`, or if there is no `ref` yet. |
| `F_STUCK` | the same raw value has now appeared `stuck_n` times in a row (flag the `stuck_n`-th sample and every later one in that run) |

Then return the index of the **first clean sample** (`flags == 0`) whose temperature is `>= thresh`, or `-1`.

```c
typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;
enum { F_RANGE = 1, F_DROPOUT = 2, F_SPIKE = 4, F_STUCK = 8 };
typedef struct {
    uint32_t period_ms; uint16_t stuck_n; int16_t max_step; int16_t lo, hi;
} FaultCfg;

/* Writes flags[0..n-1]; returns the number of faulty samples (flags != 0). */
size_t  classify_faults(const Sample *s, size_t n, const FaultCfg *cfg, uint8_t *flags);
int32_t first_valid_overheat(const Sample *s, size_t n, const uint8_t *flags, int16_t thresh);
```

### Example

```
cfg = { period 100, stuck_n 3, max_step 50, lo -400, hi 1500 },  thresh = 800

i     0    1    2    3    4    5    6    7    8     9   10
t     0  100  200  300  400  700  800  900 1000  1100 1200
temp 250  255  900  260  262  265  265  265  265  1600  270
flag   .    . SPIKE   .    . DROP   . STUCK STUCK RANGE  .

classify_faults -> 5
first_valid_overheat -> -1   (the only hot samples, 900 and 1600, are faulty)
```

### Constraints

- `0 <= n <= 100000`, timestamps increasing but `uint32_t` ms may **wrap around**.
- `stuck_n >= 2`. `flags` has room for `n` entries. No `malloc`.

### Clarifying questions to ask (first 3 minutes)

1. Is a gap of exactly `2 * period` a dropout, or only strictly more?
2. For a spike, do I compare with the previous sample, or the last **good** sample? (Previous sample flags the return-to-normal too.)
3. Should faults be exclusive, or can one sample carry several? (→ bitmask)
4. Which sample gets the stuck flag — all of the run, or only from the N-th? (Streaming can only know at the N-th.)
5. On a fault, what is the system policy — ignore the sample, hold the last good value, or treat it as overheat (fail-safe)?

## 2. Your workspace

`./run.sh T1-6`로 바로 테스트하세요. 테스트 6묶음이 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 들고 다닐 상태는 3개</summary>

`ref`(마지막 정상 기준값) + `has_ref`, 그리고 같은 값 연속 길이 `run`. 간격 검사는 `s[i-1]`만 보면 되므로 상태가 필요 없습니다. 샘플마다 `uint8_t f = 0;`에서 시작해 규칙마다 `f |= FLAG`.

</details>

<details>
<summary>Hint 2 — SPIKE 기준을 "직전 샘플"로 하면 생기는 일</summary>

255 → **900** → 260 에서 900도 SPIKE, 그다음 260도 |260−900| = 640이라 SPIKE가 됩니다. 정상으로 돌아온 샘플까지 버리게 됩니다. 그래서 기준(`ref`)은 **RANGE/SPIKE가 아니었던 마지막 샘플**로 둡니다. DROPOUT 다음 샘플은 Δ를 보지 않습니다(공백 동안 실제로 온도가 변했을 수 있음).

</details>

<details>
<summary>Hint 3 — 시간 비교와 오버플로</summary>

- 간격: `(uint32_t)(s[i].t_ms - s[i-1].t_ms) > 2u * period_ms` → 랩어라운드에도 맞습니다.
- Δ: `int16_t`끼리 빼면 `int`로 승격되긴 하지만, 의도를 드러내게 `int32_t d = (int32_t)v - ref;` 후 절댓값.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

<div class="chart" data-src="chart1.svg" data-caption="예제 입력. 과열처럼 보이는 두 점(900, 1600)은 모두 고장 샘플이라 과열 판정에서 빠집니다"></div>

규칙 4개는 서로 독립이라 **한 샘플에 여러 비트가 동시에 켜질 수 있습니다**(예: 2000에 고정된 센서 → `F_RANGE | F_STUCK`). 그래서 enum 하나가 아니라 비트마스크입니다. 순서만 주의하면 됩니다.

1. RANGE, DROPOUT 먼저 계산 (SPIKE 검사를 건너뛸지 결정하는 데 필요)
2. SPIKE: RANGE·DROPOUT이 아니고 `ref`가 있을 때만
3. STUCK: `run` 갱신 후 `run >= stuck_n`
4. 마지막에 `ref` 갱신: RANGE·SPIKE가 아니면 `ref = v`

STUCK·DROPOUT 샘플은 값 자체는 그럴듯하므로 `ref` 갱신을 막지 않습니다. 이렇게 해야 DROPOUT 뒤에 바로 SPIKE 검사를 다시 시작할 수 있습니다.

### 4.2 손으로 따라가기 (trace)

| i | t | temp | gap | ref(전) | \|Δ\| | run | flags | ref(후) |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 250 | – | 없음 | – | 1 | 0 | 250 |
| 1 | 100 | 255 | 100 | 250 | 5 | 1 | 0 | 255 |
| 2 | 200 | 900 | 100 | 255 | **645** | 1 | **SPIKE** | 255 (유지) |
| 3 | 300 | 260 | 100 | 255 | 5 | 1 | 0 | 260 |
| 4 | 400 | 262 | 100 | 260 | 2 | 1 | 0 | 262 |
| 5 | 700 | 265 | **300** | 262 | 건너뜀 | 1 | **DROPOUT** | 265 |
| 6 | 800 | 265 | 100 | 265 | 0 | 2 | 0 | 265 |
| 7 | 900 | 265 | 100 | 265 | 0 | **3** | **STUCK** | 265 |
| 8 | 1000 | 265 | 100 | 265 | 0 | 4 | **STUCK** | 265 |
| 9 | 1100 | 1600 | 100 | 265 | 건너뜀 | 1 | **RANGE** | 265 (유지) |
| 10 | 1200 | 270 | 100 | 265 | 5 | 1 | 0 | 270 |

고장 샘플 수 = 5 (i = 2, 5, 7, 8, 9). `thresh = 800` 이상은 i=2(900), i=9(1600)뿐인데 둘 다 고장이므로 `first_valid_overheat = -1`.

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `run`은 `UINT16_MAX`에서 **포화**시킵니다. 몇 시간째 같은 값이 나오는 고장 센서에서 카운터가 랩되어 1로 돌아가면 STUCK이 풀려 버립니다.
- 테스트 4번의 첫 타임스탬프 `0xFFFFFF9C`는 −100 ms입니다. 다음 샘플 100과의 간격은 `uint32_t` 뺄셈으로 정확히 200 → DROPOUT 아님(`>`이므로).
- 테스트 5번: −50.0 °C(RANGE) 샘플이 `ref`를 오염시키지 않아서, 그다음 790은 760과 비교되어 정상입니다. 고장 뒤에도 진짜 과열(i=3, 810)은 찾아냅니다.
- `first_valid_overheat`를 분리해 둔 이유: 고장 분류와 정책(무엇을 과열로 볼지)을 섞지 않기 위해서입니다. 정책은 시스템마다 다릅니다.

### 4.4 복잡도

- 시간 **O(n)**, 추가 메모리 **O(1)** (`ref`, `has_ref`, `run`). 출력 `flags`는 호출자 버퍼.
- 스트리밍 버전은 같은 상태에 `prev_t`, `prev_v`만 더하면 샘플당 O(1)입니다.

---

## 5. 임베디드 관점으로 말할 것

- **고장 ≠ 정상**: "센서가 죽었으니 과열 아님"은 위험한 결론입니다. 고장 샘플을 과열 판정에서 빼는 것과 별개로, 고장이 일정 시간 지속되면 **fault 상태를 올리고 fail-safe 동작**(팬 최대, 출력 제한, 셧다운)을 해야 합니다. 이건 시스템 정책 질문으로 면접관에게 되묻습니다.
- **비트마스크**: `uint8_t` 하나에 고장 4종을 담으면 로그·텔레메트리로 그대로 보낼 수 있고, 다중 고장도 표현됩니다. 진단 코드(DTC)와 같은 사고방식입니다.
- **STUCK의 오탐**: 분해능이 낮은 센서(0.0625 °C)가 안정된 환경에 있으면 같은 값이 정상적으로 반복됩니다. `stuck_n`은 "주기 × N이 실제로 온도가 변하지 않을 수 없는 시간"이 되도록 크게 잡습니다. ADC 노이즈가 전혀 없는 것 자체가 고장 신호라는 점도 말할 만합니다.
- **SPIKE 기준 고착**: 진짜로 온도가 크게 계단 변화하면 `ref`가 옛 값에 묶여 이후 샘플이 전부 SPIKE가 됩니다. 실무에서는 "연속 K번 새 값 근처면 `ref` 재설정" 규칙을 둡니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| A genuine step change makes every later sample a SPIKE. Fix it. | 같은 방향으로 `max_step` 이내끼리 K번 연속이면 새 값을 `ref`로 채택(재동기화). 또는 SPIKE 판정을 메디안 필터(3샘플)로 대체 → [T2-8](../T2-8_running_median/index.html). |
| How do you decide the sensor has recovered? | 고장 플래그가 없는 샘플이 M번 연속이면 fault 해제(히스테리시스). 바로 해제하면 깜빡이는 센서에서 fault가 on/off를 반복합니다 → [T1-2](../T1-2_streaming_hysteresis/index.html)와 같은 구조. |
| Make it streaming. | `ref, has_ref, run, prev_t, prev_v, have_prev`를 구조체에 넣고 `uint8_t fault_update(st, sample)`로 샘플마다 플래그 반환. |
| What if the timestamp goes backwards or repeats? | `(int32_t)(t - prev_t) <= 0`이면 별도 플래그(`F_TIME`)를 붙이고 버립니다. 랩어라운드와 역행은 부호 있는 차이로 구분합니다. |
| How would this run for 16 sensors on an MCU? | 센서별 상태 구조체를 `static` 배열로, cfg는 `const`로 플래시에. 상태는 센서당 10바이트 남짓 → [T1-4](../T1-4_multi_sensor/index.html). |

## 7. 자주 하는 실수

- 고장을 `enum` 하나로 분류 → RANGE + STUCK 동시 발생을 못 나타냄 (테스트 3번 실패)
- SPIKE 기준을 직전 샘플로 → 스파이크 다음 정상 샘플까지 SPIKE (테스트 1번 실패)
- RANGE 샘플로 `ref` 갱신 → −500 다음 790이 SPIKE가 됨 (테스트 5번 실패)
- 첫 샘플에서 `s[i-1]` 접근 → 버퍼 언더런 (ASan이 잡음)
- `t[i] > t[i-1] + 2*period` 같은 덧셈 비교 → 랩어라운드 직전에 오탐 (예: `t[i-1] = 0xFFFFFF9C`, `t[i] = 0xFFFFFFF0`이면 간격 84인데 DROPOUT)
- `>=`로 dropout 판정 → 정확히 2×period 간격을 고장으로 봄 (테스트 4번 실패)
