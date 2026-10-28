# J2 · Battery State of Charge by Coulomb Counting

> **ID**: J2 · **Priority**: P1 · **Pattern**: streaming integration + exact accumulator + wrap-safe dt + clamp + hysteresis + table interpolation · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh J2` / `./run.sh J2 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 배터리 전류 ADC 값을 시간에 대해 적분해서 남은 충전량(SoC)을 추적하고, 저전압 경보(히스테리시스)와 휴지 전압 기반 재보정까지 만든다.
- **상황**: 배터리로 움직이는 로봇이 shunt 저항 + 전류 증폭기 + 12비트 ADC로 팩 전류를 잰다. 타이머가 ADC 값과 ms 타임스탬프를 가지고 주기적으로 `soc_update`를 부른다.
- **배경 개념**
  - **Coulomb counting**: 흐른 전하량 = 전류 × 시간. 매 샘플마다 "전류 × 경과 시간"을 더하거나 빼서 배터리에 남은 양을 추적하는 방법.
  - **단위**: 용량은 mAh(1 mA로 1시간). 이 문제는 내부 누적을 mA·ms로 한다. 예제 기준 1 mAh = 1000 mA × 3600 ms = 3,600,000 mA·ms.
  - **SoC permille**: 남은 양을 0..1000(= 0..100.0 %)으로 나타낸 값.
  - **OCV(open-circuit voltage)**: 전류가 안 흐르고 쉬고 있을 때의 셀 전압. 이 전압으로 SoC를 표에서 찾을 수 있어서, 적분 오차를 리셋하는 데 쓴다.
- **입력**: `soc_init(s, capacity_mAh, soc_permille, now_ms)` — 용량(mAh, 1..1,000,000), 시작 SoC(permille, 1000 초과면 1000으로), 시작 시각. `soc_update(s, adc_raw, now_ms)` — ADC 코드(0..4095), 시각(ms, `uint32_t`, wrap). `current_mA(raw)` — ADC 코드. `ocv_to_permille(mv)`, `soc_rest_calibrate(s, cell_mv)` — 셀 전압(mV).
- **출력**: `current_mA`는 mA(`int32_t`, 양수 = 방전). `soc_permille`은 0..1000. `soc_low_alert`는 경보 상태 `bool`. `ocv_to_permille`은 permille. 상태는 `Soc` 구조체에: `cap_mAms`, `rem_mAms`(0..cap), `last_ms`, `gaps`, `faults`, `fault`, `low_alert`.
- **규칙**
  - Part A — 전류: `current_mA = (raw - 2048) * 10`(코드 2048이 0 mA, 1카운트당 10 mA). 양수 = 방전(잔량 감소), 음수 = 충전.
  - Part A — 포화: 코드 `0`, `4095`는 적분하지 않고 `fault` = true, `faults` +1. 다음 정상 샘플이 `fault`를 지움.
  - Part A — 시간: `dt = (uint32_t)(now_ms - last_ms)`로 wrap-safe. 매 호출마다 `last_ms` 갱신. `now_ms`에서 잰 전류를 구간 `(last, now]` 전체에 적용. `dt > 1000 ms`(`MAX_GAP_MS`)면 그 구간은 적분 안 하고 `gaps` +1.
  - Part A — 누적: `int64_t` mA·ms로 더해서 작은 전류도 반올림으로 사라지지 않게. 잔량은 `[0, capacity]`로 clamp. `soc_permille` = `remaining * 1000 / capacity`(버림).
  - Part B — 경보: `<= 150`에서 ON, `>= 200`에서 OFF, 그 사이는 이전 상태 유지. `soc_init`과 `soc_rest_calibrate`에서도 평가.
  - Part C — OCV 표 `3300:0 3500:50 3600:150 3700:400 3800:600 4000:850 4200:1000`에서 정수 선형 보간(버림), 첫 점 아래·마지막 점 위는 clamp. `soc_rest_calibrate`는 그 값으로 잔량을 덮어씀.
  - malloc, 부동소수점 금지.
- **예제 풀어 읽기**: 10 mAh 팩, 1000 permille에서 시작, t = 0. raw 2148 = (2148 − 2048) × 10 = +1000 mA(방전)를 100 ms마다 t = 3600까지 → 1000 mA × 3600 ms = 3,600,000 mA·ms = 1 mAh 소모 → 900 permille. 1 mAh 팩에 raw 2049 = +10 mA를 10 ms마다 18000번(180 s) → 한 번에 100 mA·ms뿐이지만 18000 × 100 = 0.5 mAh → 500 permille. `ocv_to_permille(3650)`은 3600:150과 3700:400 사이 → 150 + (50 × 250) / 100 = 275. 3000 → 0, 4300 → 1000.
- **놓치기 쉬운 것**: 매 단계 mAh로 나누면 작은 전류가 0이 됨(누적은 mA·ms로), 충전 시 저장값 자체가 capacity에서 clamp(`rem_mAms == cap_mAms`), 과방전 시 0에서 clamp 후 충전은 0부터, 시작부터 50 permille이면 init에서 경보 ON, timer wrap(`last` = `0xFFFFFE0C`, now = 500 → dt = 1000은 gap 아님), dt = 1001이면 gap이지만 `last_ms`는 갱신해서 다음 dt = 1000은 적분, 4095와 0 둘 다 fault(카운트 누적), 정상 샘플이 flag 해제, 경보 경계(150에서 ON, 190에선 계속 ON, 200에서 OFF, 다시 170으로 내려가도 재트리거 없음, ON·OFF 각각 정확히 1번), OCV 정확한 점(3300 → 0, 3700 → 400, 4200 → 1000), 3900 → 725, 3333 → 8(8.25 버림), 재보정 후 경보 재평가(3500 → 50 → ON).
- **구현할 함수**
  - Part A — `current_mA(raw)`: 코드 → mA. `soc_init(s, capacity_mAh, soc_pm, now_ms)`: 용량·잔량·시각 설정(+ 경보 평가). `soc_update(s, adc_raw, now_ms)`: dt, fault/gap 검사, 적분, clamp, 경보. `soc_permille(s)`: 잔량 → permille.
  - Part B — `soc_low_alert(s)`: 히스테리시스 경보 상태 반환.
  - Part C — `ocv_to_permille(mv)`: 표 보간. `soc_rest_calibrate(s, cell_mv)`: OCV로 잔량 덮어쓰기(+ 경보 평가).

## 1. Problem

A battery-powered robot measures pack current with a shunt resistor, a current-sense amplifier and a **12-bit ADC**. A timer calls you with each ADC reading and a millisecond timestamp. Track the **state of charge (SoC)** by integrating current over time ("coulomb counting").

**Part A — counting**

- Convert a reading to current: `current_mA = ((int32_t)raw - 2048) * 10 / 1` (zero current at code 2048, 10 mA per count). **Positive = discharge**, negative = charging.
- ADC codes `0` and `4095` are **saturated** (the sensor is at a rail): do not integrate, set `s->fault`, increment `s->faults`. The next good sample clears `s->fault`.
- `dt = (uint32_t)(now_ms - last_ms)` must be **wrap-safe**. Every call updates `last_ms`. The current measured at `now_ms` is used for the whole interval `(last, now]`.
- If `dt > MAX_GAP_MS` (1000 ms), samples were lost: do not integrate that interval, increment `s->gaps`.
- Accumulate charge in **mA·ms in an `int64_t`** so that small currents are never lost to rounding. Clamp the remaining charge to `[0, capacity]`.
- `soc_permille()` returns `remaining * 1000 / capacity`, rounded down (0..1000).

**Part B — low-battery alert with hysteresis**: the alert turns **ON at `<= 150`** permille and turns **OFF at `>= 200`** permille. Between them it keeps its previous state. `soc_init` and `soc_rest_calibrate` evaluate it too.

**Part C — rest-voltage recalibration**: when the pack has been at rest, its open-circuit voltage (OCV) tells you the SoC. Map millivolts to permille through the given 7-point table with **integer linear interpolation** (rounded down), clamped below the first and above the last point. `soc_rest_calibrate()` overwrites the counted charge with that value.

```c
#define ZERO_RAW 2048            /* 10 mA per count, + = discharge */
#define MAX_GAP_MS 1000u
typedef struct {
    int64_t  cap_mAms;           /* capacity in mA*ms           */
    int64_t  rem_mAms;           /* remaining charge, 0..cap    */
    uint32_t last_ms;
    uint32_t gaps, faults;
    bool     fault;              /* most recent sample saturated */
    bool     low_alert;
} Soc;                           /* you may add fields; keep these */

int32_t  current_mA(uint16_t raw);
void     soc_init(Soc *s, uint32_t capacity_mAh, uint16_t soc_permille, uint32_t now_ms);
void     soc_update(Soc *s, uint16_t adc_raw, uint32_t now_ms);
uint16_t soc_permille(const Soc *s);
bool     soc_low_alert(const Soc *s);

/* OCV table (mV -> permille): 3300:0 3500:50 3600:150 3700:400 3800:600 4000:850 4200:1000 */
uint16_t ocv_to_permille(uint16_t mv);
void     soc_rest_calibrate(Soc *s, uint16_t cell_mv);
```

### Example

```
10 mAh pack, start 1000 permille, t = 0
raw 2148 (= +1000 mA) every 100 ms until t = 3600
  -> 1000 mA * 3600 ms = 3,600,000 mA*ms = 1 mAh  -> 900 permille

1 mAh pack, raw 2049 (= +10 mA) every 10 ms, 18000 times (180 s)
  -> each step is only 100 mA*ms, but 18000 * 100 = 0.5 mAh -> 500 permille

ocv_to_permille(3650) = 150 + (50 * 250) / 100 = 275
ocv_to_permille(3000) = 0,  ocv_to_permille(4300) = 1000
```

### Constraints

- `1 <= capacity_mAh <= 1,000,000`; `soc_permille` argument above 1000 is clamped to 1000.
- Calls come from one context (a timer task); no locking needed in this version.
- No `malloc`, no floating point.

### Clarifying questions to ask (first 3 minutes)

1. Which sign is discharge? Where is the zero-current code, and is it calibrated?
2. Over an interval, do I use the new sample, the previous one, or the average (trapezoid)?
3. What should happen after a long gap — integrate with the last current, or skip?
4. What does a saturated ADC code mean here — a real overcurrent or a broken sensor?
5. Should the alert use hysteresis, and what are the on/off levels? Is the boundary inclusive?
6. When is it safe to trust the rest voltage (how long at rest, which temperature)?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 로컬에서 `./run.sh J2`로 바로 테스트하세요. 테스트 10묶음(정상 방전 → remainder → 충전 clamp → 과방전 clamp → 타이머 wrap → gap → 포화 ADC → 히스테리시스 → OCV 표 → 재보정)이 들어 있습니다. OCV 표 데이터는 stub에 이미 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 단위를 mA·ms로 고정하면 나눗셈이 사라진다</summary>

`I(mA) × dt(ms)`는 항상 정수이고 정확합니다. 용량도 같은 단위로 저장하세요: `cap_mAms = capacity_mAh × 3,600,000`. 그러면 `soc_update`에는 곱셈·뺄셈·clamp만 남고, 나눗셈은 `soc_permille()`에서 **읽을 때 한 번만** 합니다. 매 step마다 mAh로 나누면 몫이 0이 되어 작은 전류가 통째로 사라집니다.

</details>

<details>
<summary>Hint 2 — soc_update의 순서</summary>

1. `dt = (uint32_t)(now - last); last = now;` ← 어떤 경로로 빠져나가도 last는 갱신
2. 포화 코드(0, 4095)면 `fault = true; faults++; return;`
3. `fault = false;` 그리고 `dt > MAX_GAP_MS`면 `gaps++; return;`
4. `rem -= (int64_t)current_mA(raw) * dt;` → `[0, cap]`로 clamp
5. 히스테리시스 갱신

</details>

<details>
<summary>Hint 3 — 히스테리시스와 표 보간</summary>

- 알람: `if (!on && p <= 150) on = true; else if (on && p >= 200) on = false;` — 상태별로 "반대편으로 가는 조건"만 봅니다([T1-2](../T1-2_streaming_hysteresis/index.html)와 같은 패턴).
- 보간: 먼저 양 끝 clamp, 그다음 `OCV[i-1].mv < mv <= OCV[i].mv`인 구간을 찾고 `y0 + (mv - x0) * (y1 - y0) / (x1 - x0)`. **곱하고 나서 나눕니다**. 순서를 바꾸면 `250/100 = 2`가 되어 오차가 큽니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

Coulomb counting은 "전류를 시간에 대해 적분"하는 것, 즉 `Q = Σ I·dt`입니다. 문제의 대부분은 적분 자체보다 **경계 조건**(정밀도, 시간 wrap, 끊긴 샘플, 고장 센서, 범위 clamp)을 처리하는 데 있습니다.

먼저 정밀도. 1 mAh 팩에 10 mA가 10 ms마다 들어오면 한 step에 빠지는 양은 100 mA·ms = 0.0000278 mAh입니다. 남은 용량을 mAh 정수로 들고 매번 나누면 `100 / 3,600,000 = 0`이라 180초가 지나도 1000‰ 그대로입니다. mA·ms 누적기는 18000 step 뒤 정확히 1,800,000 mA·ms(= 0.5 mAh) → **500‰**입니다.

<div class="chart" data-src="chart3.svg" data-caption="테스트 2: 매 step 나누는 naive 방식은 작은 전류를 전부 잃어 1000‰에 머물고, mA·ms 누적기는 500‰까지 정확히 내려갑니다"></div>

"remainder를 들고 다닌다"는 말은 결국 이것입니다. 단위를 따로 mAh로 두고 싶다면 `rem_mAh`와 `frac_mAms`(0..3,599,999) 두 개를 들고, `frac`이 3,600,000을 넘을 때만 1 mAh를 넘겨주면 됩니다. 결과는 같고, 하나의 `int64` mA·ms가 더 단순합니다.

### 4.2 히스테리시스 알람

10 mAh 팩에 360 mA를 1 s 동안 흘리면 360,000 mA·ms = 0.1 mAh = **10‰**입니다. 300‰에서 16초 방전 → 6초 충전 → 3초 방전하면 다음과 같습니다.

<div class="chart" data-src="chart1.svg" data-caption="테스트 8: t=15 s에 150‰ → ON. 충전 중 150~190‰에서도 ON 유지, t=22 s에 200‰ → OFF. 다시 170‰까지 내려가도 띠 안이라 재알람 없음(ON 1번, OFF 1번)"></div>

| t (s) | raw | 전류 | permille | alert | 이유 |
|---|---|---|---|---|---|
| 0 | – | – | 300 | off | init, 300 > 150 |
| 14 | 2084 | +360 mA | 160 | off | 아직 > 150 |
| **15** | 2084 | +360 mA | **150** | **ON** | `<= 150` (경계 포함) |
| 16 | 2084 | +360 mA | 140 | ON | |
| 17 | 2012 | −360 mA | 150 | ON | 충전 시작, 띠 안 |
| 21 | 2012 | −360 mA | 190 | ON | 아직 < 200 |
| **22** | 2012 | −360 mA | **200** | **off** | `>= 200` (경계 포함) |
| 25 | 2084 | +360 mA | 170 | off | 띠 안, 재알람 없음 |

### 4.3 OCV 표 보간

<div class="chart" data-src="chart2.svg" data-caption="OCV 곡선(Li-ion 형태). 3650 mV는 3600~3700 구간 → 275‰, 3900 mV는 3800~4000 구간 → 725‰. 표 밖은 0 / 1000으로 clamp"></div>

### 4.4 손으로 따라가기 (trace)

**soc_update 경로별** (10 mAh 팩, 1 mAh = 3,600,000 mA·ms, `cap_mAms = 36,000,000`)

| 테스트 | last → now | dt | raw | 경로 | rem 변화 (mA·ms) | permille |
|---|---|---|---|---|---|---|
| 1 정상 | 0 → 3600 (36번) | 100 ×36 | 2148 | 적분 | −1000×3600 = −3,600,000 | 1000 → **900** |
| 3 충전 clamp | 0 → 1000 → 2000 | 1000 | 1688 | 적분 + clamp | 35,640,000 → 36,000,000 (cap) | 990 → **1000** |
| 3 이어서 | 2000 → 3000 | 1000 | 2084 | 적분 | −360,000 | **990** |
| 4 과방전 clamp | 0 → 1000 → 2000 | 1000 | 2408 | 적분 + clamp | 1,800,000 → 0 | 50 → **0** |
| 4 이어서 | 2000 → 3000 | 1000 | 2012 | 적분 | +360,000 | **10** |
| 5 wrap | 0xFFFFFE0C → 500 | **1000** | 2084 | 적분 | −360,000 | 1000 → **990** |
| 6 gap | 0 → 1001 | **1001** | 2408 | gap (`gaps=1`) | 0 | **1000** |
| 6 이어서 | 1001 → 2001 | 1000 | 2408 | 적분 | −3,600,000 | **900** |
| 7 포화 | 0 → 1000 | 1000 | 4095 | fault (`faults=1`) | 0 | **1000** |
| 7 포화 | 1000 → 2000 | 1000 | 0 | fault (`faults=2`) | 0 | **1000** |
| 7 회복 | 2000 → 3000 | 1000 | 2084 | 적분, `fault=false` | −360,000 | **990** |

포인트: 테스트 3에서 clamp를 **저장값**에 하지 않고 표시값만 1000으로 자르면, 내부 값이 1190‰ 상당으로 남아서 다음 방전 후에도 1000으로 보입니다(990이 나와야 함). 테스트 5에서 `int32_t`로 빼거나 `now < last`를 검사하면 dt가 엉뚱해져서 gap으로 처리되고 1000이 남습니다.

**ocv_to_permille**

| mv | 구간 | 계산 | 결과 |
|---|---|---|---|
| 3000 | 표 아래 | clamp | **0** |
| 3300 | 첫 점 | clamp (`<=`) | **0** |
| 3333 | 3300~3500 | 0 + 33×50/200 = 8.25 | **8** |
| 3650 | 3600~3700 | 150 + 50×250/100 | **275** |
| 3700 | 정확히 표 점 | 150 + 100×250/100 | **400** |
| 3900 | 3800~4000 | 600 + 100×250/200 | **725** |
| 4200 / 4300 | 끝점 / 표 위 | clamp | **1000** |

재보정(테스트 10): 900‰ 상태에서 `soc_rest_calibrate(3650)` → 275‰, 알람 off. 이어서 `3500` → 50‰, 알람 ON.

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `(int64_t)current_mA(raw) * dt`: 캐스트를 곱셈 **전에** 합니다. `int32 × uint32`는 `uint32`로 승격되어 음수 전류(충전)가 거대한 양수가 됩니다. 이 버그는 테스트 4의 충전 단계에서 바로 드러납니다.
- `last_ms = now_ms`를 함수 맨 앞에서 갱신합니다. fault/gap으로 `return`해도 다음 dt가 "직전 호출부터"로 맞습니다. 이걸 빠뜨리면 gap 다음 샘플도 계속 gap이 됩니다.
- `update_alert()` 하나를 init·update·calibrate 세 곳에서 부릅니다. 상태 머신을 한 곳에만 두는 게 버그를 줄입니다.
- `soc_permille`에서 `rem × 1000`: 용량 상한 1,000,000 mAh → 3.6×10¹² mA·ms × 1000 = 3.6×10¹⁵ < 9.2×10¹⁸(`INT64_MAX`)이라 안전합니다.
- OCV 보간은 `int32_t`로 계산합니다(`uint16_t` 차이를 빼면 음수가 될 수 있는 식을 피함). 표가 단조 증가라는 전제로 선형 탐색 7번이면 충분하고, 표가 크면 이분 탐색으로 바꿉니다 → [T2-5](../T2-5_lower_bound/index.html).

### 4.6 복잡도

- `soc_update`: **O(1)** — 곱셈 1번, 비교 몇 번. 나눗셈 없음.
- `ocv_to_permille`: **O(표 크기)** = 7, 이분 탐색이면 O(log n).
- 상태: 구조체 32바이트(`int64` 2개 + `uint32` 3개 + `bool` 2개 + 패딩).

---

## 5. 임베디드 관점으로 말할 것

- **왜 이 문제인가**: 면접관 Jason MacDonald는 Johnson Outdoors에서 RTOS 기반 트롤링 모터 제품에 "배터리 전류를 모니터링해서 SoC를 계산"하는 기능을 직접 구현했습니다. Scout도 배터리로 움직이는 로봇·드론을 만듭니다. 그래서 "전류 적분 + 경계 처리 + 보정"을 짧게 짜고 설명할 수 있으면 대화가 바로 그 경험으로 이어집니다. 끝나고 "트롤링 모터에서는 offset 보정을 어떻게 하셨나요?" 같은 질문을 던지기 좋습니다.
- **정밀도는 단위 선택으로 해결**: 부동소수점 없이 mA·ms 정수 누적기를 쓰면 매 step 오차가 0입니다. 오차는 센서(offset, gain) 쪽에서만 생깁니다.
- **어디서 돌리나**: ADC를 타이머 트리거 + DMA로 돌리고, DMA 완료 ISR(또는 그 ISR이 깨우는 태스크)에서 샘플 블록을 평균 내서 `soc_update`를 부릅니다. ISR에서는 64비트 곱셈 1번이라 Cortex-M에서도 짧지만, 알람 처리·로그·CAN 전송은 태스크로 넘깁니다. 다른 태스크가 `rem_mAms`(64비트)를 읽으면 **찢어진 읽기**가 생길 수 있으니 critical section이나 seqlock으로 보호합니다 → [E3](../E3_isr_to_task_pipeline/index.html).
- **센서를 믿지 않기**: 포화 코드는 실제 과전류일 수도, 증폭기 고장일 수도 있습니다. 적분하지 않고 플래그를 세운 뒤 상위(BMS/안전 로직)가 판단하게 합니다 → [T1-6](../T1-6_sensor_fault_detect/index.html).
- **gap은 추측하지 않기**: 샘플이 끊긴 동안을 마지막 전류로 채우면 큰 오차가 생깁니다. 건너뛰고 세어 두면 나중에 진단할 수 있습니다.
- **히스테리시스**: 부하가 바뀔 때 전압·SoC가 흔들려서 알람이 깜빡이는 걸 막습니다. 저전압 컷오프도 같은 패턴입니다 → [T1-2](../T1-2_streaming_hysteresis/index.html), [T2-7](../T2-7_debounce_edge/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why does coulomb counting drift over time? | 적분이라 작은 **offset 오차가 시간에 비례해서 쌓입니다**. 예: zero 코드가 1 count(10 mA) 틀리면 하루에 240 mAh가 틀어집니다. 그 밖에 gain 오차(shunt 저항 온도계수), 자가방전(측정 안 되는 전류), 온도에 따른 실제 용량 변화, **Peukert 효과**(고전류일수록 꺼낼 수 있는 용량이 줄어듦), 노화로 인한 용량 감소가 있습니다. |
| How do you calibrate the current-sense offset? | 부하를 끊은 상태(MOSFET off, 또는 부팅 직후 모터 정지)에서 수백 샘플을 평균 내 `zero_raw`를 측정해 저장합니다. 온도별로 다르면 몇 점 테이블로 둡니다. 런타임에는 "전류가 거의 0인 구간"의 아주 작은 값을 deadband로 0 처리하기도 합니다(대신 자가방전을 놓침). |
| When do you trust OCV, and how do you combine it with counting? | 부하가 없고 **일정 시간(수십 분) 쉬어서 전압이 이완된 뒤**에만 OCV가 SoC를 잘 나타냅니다. 부하 중에는 IR drop 때문에 낮게 보입니다. 그래서 휴지 상태가 감지되면 OCV로 재보정(Part C)하고, 사용 중에는 counting을 씁니다. Li-ion은 중간 구간(3600~3800 mV)이 평평하지 않지만 LFP는 매우 평평해서 OCV 보정이 거의 끝점에서만 유효합니다. |
| Can you do better than resetting from OCV? | **Kalman filter(EKF)**로 두 정보를 융합합니다. 상태 = SoC(+ RC 분극 전압), 예측 = coulomb counting, 측정 = 단자 전압과 배터리 등가회로 모델. 측정 신뢰도에 따라 가중치가 자동으로 정해져서 "갑자기 점프"하는 재보정 대신 부드럽게 보정됩니다. 다만 모델 파라미터 식별이 필요해서 MCU에서는 간단한 버전을 씁니다. |
| How big can the accumulator get? Could int32 work? | 최악 1 step: 20,470 mA × 1000 ms ≈ 2×10⁷ — int32에 들어갑니다. 하지만 누적값은 10 Ah 팩만 해도 3.6×10¹⁰ mA·ms라 **int32(2.1×10⁹) 초과**입니다. int64면 9.2×10¹⁸이라 충분하고, `×1000` 연산까지 고려해 용량 상한(1,000 Ah)을 둡니다. int32만 쓰고 싶으면 mAh 정수 + mA·ms 나머지(0..3,599,999)로 쪼갭니다. |
| Rectangle or trapezoid integration? | 샘플 주기가 전류 변화보다 충분히 빠르면 사각형으로 충분합니다. PWM 모터 전류처럼 리플이 크면 ADC를 PWM에 동기화하거나 하드웨어 평균/필터를 쓰는 게 적분 방식보다 중요합니다. 사다리꼴은 이전 전류를 저장해야 해서 상태가 하나 늘어납니다. |
| Would you use a fuel-gauge IC instead? | 보통 그렇습니다. **TI BQ 시리즈**(예: BQ34Z100, BQ40Z50) 같은 fuel gauge는 coulomb counter, 온도 보상, 셀 모델(Impedance Track), 노화 학습, 보호를 하드웨어로 제공하고 I2C/SMBus로 SoC를 읽습니다. DIY는 부품비·전력을 아끼거나 특수 화학/팩 구조일 때 고려합니다. 어느 쪽이든 펌웨어는 I2C 드라이버, 고장 처리, 알람 정책을 맡습니다 → [E1](../E1_i2c_sensor_driver/index.html). |
| The pack is replaced or the robot reboots. What happens? | RAM 상태가 사라지므로 부팅 시 OCV로 초기 SoC를 정하고(모터 정지 상태), 마지막 값과 학습한 용량은 플래시/EEPROM에 주기적으로(마모 고려) 저장합니다. 팩 교체는 ID 칩이나 전압 점프로 감지합니다. |

## 7. 자주 하는 실수

- 매 step `I*dt/3600000`으로 mAh에 나눠서 더함 → 작은 전류가 0으로 잘려 사라짐 → 테스트 2 실패(1000‰에 머묾)
- `int32 × uint32` 곱셈을 캐스트 없이 함 → 충전 전류가 unsigned로 바뀌어 거대한 방전 → 테스트 3·4 실패
- 표시값만 clamp하고 저장값은 그대로 → 테스트 3의 "충전 후 방전 990" 실패
- `now < last`를 검사하거나 signed로 뺌 → 타이머 wrap에서 gap으로 오판 → 테스트 5 실패
- gap/fault로 `return`하기 전에 `last_ms`를 갱신하지 않음 → 이후 샘플이 계속 gap → 테스트 6·7 실패
- 알람을 `p <= 150`의 level로만 계산 → 160~190‰에서 깜빡임(히스테리시스 없음) → 테스트 8 실패
- 보간에서 `(y1-y0)/(x1-x0)`를 먼저 나눔 → `250/100 = 2`로 잘려서 3650 mV가 150 + 50×2 = 250이 됨(정답 275)
- OCV 표 끝점 처리 누락 → 표 밖 인덱스 접근(배열 범위 초과)
