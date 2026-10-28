# T1-2 · Streaming Overheat Detector

> **ID**: T1-2 · **Priority**: P0 · **Pattern**: state machine + hysteresis + consecutive-N counter · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-2` / `./run.sh T1-2 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 샘플이 하나씩 들어올 때마다 호출되는 과열 감지기를 만들고, 상태가 바뀌는 바로 그 샘플에서만 이벤트(`EV_OVERHEAT` / `EV_RECOVERED`)를 돌려준다.
- **상황**: 타이머 인터럽트(예: 100 ms마다)에서 센서 온도가 한 개씩 도착한다. 온도는 0.1 °C 단위 정수(`850` = 85.0 °C). 상태는 O(1)만 쓴다.
- **입력**: `det_init(d, high, low, n_confirm)` — 과열 기준 `high`, 복귀 기준 `low`(둘 다 0.1 °C), 연속 확인 횟수 `n_confirm`(1~255). `det_update(d, temp_c10)` — 새 샘플 하나. `Detector` 구조체 내용은 직접 설계한다.
- **출력**: `det_init`은 설정이 유효하면 `true`, 아니면 `false`. `det_update`는 그 샘플에서 생긴 이벤트 `EV_NONE` / `EV_OVERHEAT` / `EV_RECOVERED` 중 하나(레벨이 아니라 엣지).
- **규칙**
  - NORMAL에서 `temp >= high`가 `n_confirm`번 **연속**되면 OVERHEATED로 바꾸고 그 샘플에서 `EV_OVERHEAT`.
  - OVERHEATED에서 `temp <= low`가 `n_confirm`번 연속되면 NORMAL로 바꾸고 그 샘플에서 `EV_RECOVERED`.
  - 연속을 깨는 샘플은 카운터를 리셋. 나머지는 전부 `EV_NONE`이고, 과열 중에 `EV_OVERHEAT`를 또 내지 않는다.
  - `TEMP_INVALID`(`INT16_MIN`)는 센서 에러: `EV_NONE` 반환 + 카운터 리셋.
  - `det_init`은 `low >= high` 또는 `n_confirm == 0`이면 `false`. 초기화가 거부된 감지기는 절대 이벤트를 내지 않는다. `malloc` 금지.
- **예제 풀어 읽기**: `high = 850`, `low = 800`, `n_confirm = 3`. index 1·2·3의 860·870·880이 세 번 연속 `>= 850` → index 3에서 `EV_OVERHEAT`. index 6·7·8의 790·780·770이 세 번 연속 `<= 800` → index 8에서 `EV_RECOVERED`. `900 700 900 700`처럼 튀는 값은 연속 3을 못 채워 이벤트 없음.
- **놓치기 쉬운 것**: 계속 뜨거워도 `EV_OVERHEAT`는 딱 한 번, 과열 중 `low`와 `high` 사이 값(820)이 복귀 연속을 리셋, `TEMP_INVALID`가 과열 연속을 끊음, 경계값 850 / 800 포함(`>=`, `<=`), `n_confirm == 1`, 거부된 `det_init` 뒤 `det_update(900)`도 `EV_NONE`.
- **구현할 함수**: `Detector` 구조체 설계, `det_init`(설정 검증 + 상태 초기화), `det_update`(샘플 하나 처리 + 이벤트 반환).

## 1. Problem

Temperature samples from one sensor arrive **one at a time** (for example, from a timer interrupt every 100 ms). Temperatures are in tenths of a degree Celsius (`850` means 85.0 °C).

Write a detector with O(1) state that reports **events**, not levels:

- While **NORMAL**: after `n_confirm` **consecutive** samples with `temp >= high`, switch to OVERHEATED and return `EV_OVERHEAT` on that sample.
- While **OVERHEATED**: after `n_confirm` **consecutive** samples with `temp <= low`, switch back to NORMAL and return `EV_RECOVERED` on that sample.
- Every other sample returns `EV_NONE`. A sample that breaks the streak resets the counter. Never report `EV_OVERHEAT` twice in a row.
- `TEMP_INVALID` (`INT16_MIN`) is a sensor error code: return `EV_NONE` and reset the counter.
- `det_init` must reject `low >= high` and `n_confirm == 0` by returning `false`; an uninitialized detector never fires.
- No `malloc`.

```c
#define TEMP_INVALID INT16_MIN
typedef enum { EV_NONE, EV_OVERHEAT, EV_RECOVERED } Event;
typedef struct { /* you design it */ } Detector;

bool  det_init(Detector *d, int16_t high, int16_t low, uint8_t n_confirm);
Event det_update(Detector *d, int16_t temp_c10);
```

### Example

```
high = 850, low = 800, n_confirm = 3

index : 0    1    2    3    4    5    6    7    8
temp  : 840  860  870  880  860  820  790  780  770
event : -    -    -    OVH  -    -    -    -    REC

EV_OVERHEAT at index 3 (860, 870, 880 are three in a row >= 850)
EV_RECOVERED at index 8 (790, 780, 770 are three in a row <= 800)

Spikes: 900 700 900 700  ->  no events (the streak never reaches 3)
```

### Constraints

- `1 <= n_confirm <= 255`, `low < high`.
- `det_update` is called from one context only (no locking needed in this version).

### Clarifying questions to ask (first 3 minutes)

1. Is "hot" `>= high` or `> high`? Is "recovered" `<= low` or `< low`?
2. Does a sample between `low` and `high` while overheated reset the recovery streak, or just not count?
3. What should a sensor error value do — be skipped, or break the streak?
4. Should I return an event (edge) or the current state (level)? Can the caller ask for the state too?
5. Should `det_init` validate `low < high`? What happens if it is called with bad values?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 로컬에서 `./run.sh T1-2`로 바로 테스트하세요. 테스트 7개가 들어 있습니다. `Detector` 구조체는 비어 있으니 필드를 직접 설계하세요.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 상태에 무엇이 필요한가?</summary>

설정값 3개(`high`, `low`, `n_confirm`) + 런타임 상태 2개면 충분합니다: **현재 상태**(`bool overheated`)와 **연속 카운터**(`uint8_t streak`). 카운터를 두 개(hot용, cool용) 둘 필요는 없습니다. 지금 상태에서 "반대편으로 가는 조건"만 세면 됩니다.

</details>

<details>
<summary>Hint 2 — 한 줄로 조건 고르기</summary>

`bool toward = overheated ? (t <= low) : (t >= high);` 이 조건이 거짓이면 `streak = 0`, 참이면 `streak++`. `streak == n_confirm`이 되는 순간 상태를 뒤집고 이벤트를 반환하면서 `streak = 0`으로 되돌립니다.

</details>

<details>
<summary>Hint 3 — 중복 이벤트는 구조상 생기지 않는다</summary>

과열 상태에서는 `>= high`를 아예 세지 않습니다. 그래서 900이 계속 들어와도 `toward`가 거짓 → `EV_NONE`만 나옵니다. 상태 머신으로 짜면 "이벤트 중복 금지" 플래그가 따로 필요 없습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

임계값이 하나면 85.0 °C 근처에서 값이 흔들릴 때 OVERHEAT/RECOVERED가 계속 번갈아 나옵니다(chattering). 해결책은 두 가지를 겹쳐 쓰는 것입니다.

- **히스테리시스**: 들어갈 때는 `high`, 나올 때는 `low`. 그 사이(노란 띠)에서는 상태가 바뀌지 않습니다.
- **연속 N회 확인**: 한 번 튄 값(spike)으로는 상태를 바꾸지 않습니다.

<div class="chart" data-src="chart1.svg" data-caption="예제 입력: 860·870·880 세 번 연속 ≥ 850 → index 3에서 EV_OVERHEAT. 860·820은 ≤ 800이 아니라 streak 0. 790·780·770 → index 8에서 EV_RECOVERED"></div>

### 4.2 상태 머신

<div class="chart" data-src="chart2.svg" data-caption="두 상태, 카운터 하나. 각 상태는 '반대편으로 가는 조건'만 셉니다. 전이할 때 streak을 0으로 되돌리는 것을 잊지 말 것"></div>

### 4.3 손으로 따라가기 (trace)

`high=850, low=800, n_confirm=3`

| i | temp | 상태(입력 전) | 세는 조건 | 조건 충족? | streak | event | 상태(입력 후) |
|---|---|---|---|---|---|---|---|
| 0 | 840 | NORMAL | ≥ 850 | ✗ | 0 | – | NORMAL |
| 1 | 860 | NORMAL | ≥ 850 | ✓ | 1 | – | NORMAL |
| 2 | 870 | NORMAL | ≥ 850 | ✓ | 2 | – | NORMAL |
| 3 | 880 | NORMAL | ≥ 850 | ✓ | 3 → 0 | **EV_OVERHEAT** | OVERHEATED |
| 4 | 860 | OVERHEATED | ≤ 800 | ✗ | 0 | – | OVERHEATED |
| 5 | 820 | OVERHEATED | ≤ 800 | ✗ (띠 안) | 0 | – | OVERHEATED |
| 6 | 790 | OVERHEATED | ≤ 800 | ✓ | 1 | – | OVERHEATED |
| 7 | 780 | OVERHEATED | ≤ 800 | ✓ | 2 | – | OVERHEATED |
| 8 | 770 | OVERHEATED | ≤ 800 | ✓ | 3 → 0 | **EV_RECOVERED** | NORMAL |

스파이크 입력 `900 700 900 700`: streak이 1 → 0 → 1 → 0으로만 움직여서 3에 닿지 않습니다 → 이벤트 없음.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `toward` 한 줄이 상태별 분기를 대신합니다. 상태가 3개 이상(경고/위험/셧다운)으로 늘면 `switch (state)`로 바꾸면 됩니다.
- `++d->streak < d->n_confirm`: `streak`은 `n_confirm`에 닿는 순간 0으로 돌아가므로 `uint8_t`가 넘칠 일이 없습니다(최대 255).
- `valid` 플래그: `det_init`이 실패한 감지기는 아무 이벤트도 내지 않습니다. "잘못된 설정으로 조용히 오동작"보다 안전합니다. `memset`으로 먼저 0을 채워서 실패해도 상태가 정의됩니다.
- 원래 문제 시그니처는 `void det_init`이었는데, 검증 결과를 알리려고 `bool`로 바꿨습니다. 면접에서도 "init이 bool을 돌려줘도 될까요?"라고 물어보면 좋습니다.

### 4.5 복잡도

- `det_update` 한 번에 **O(1)** 시간.
- 상태 **O(1)** — 구조체 8바이트(`int16_t` 2개 + `uint8_t` 2개 + `bool` 2개). 센서가 16개면 128바이트 → [T1-4](../T1-4_multi_sensor/index.html).

---

## 5. 임베디드 관점으로 말할 것

- **ISR에서 부를 수 있는 크기**: 분기 몇 개와 증가 하나뿐이고 블로킹·나눗셈·malloc이 없습니다. 다만 이벤트 처리(로그, 팬 제어, 셧다운)는 ISR 밖에서 하도록 플래그나 큐로 넘깁니다.
- **Edge vs level**: 이벤트(edge)를 돌려주면 호출자는 "켜기/끄기"를 한 번만 하면 됩니다. 현재 상태(level)가 필요하면 `det_is_overheated()` 접근자를 따로 둡니다.
- **히스테리시스 + 디바운스는 펌웨어 기본기**: 버튼 디바운스, 배터리 저전압 컷오프, 팬 on/off 모두 같은 패턴입니다 → [T2-7](../T2-7_debounce_edge/index.html).
- **센서 에러를 0 °C로 취급하지 않기**: `INT16_MIN` 같은 sentinel을 정해서 걸러야 합니다. 그렇지 않으면 I2C가 끊긴 순간 "급격히 냉각"으로 보여 RECOVERED가 잘못 나갈 수 있습니다. 여기서는 streak을 리셋해서 "확인되지 않은 연속"을 끊었습니다.
- **실패하면 안전한 쪽으로**: 에러가 오래 지속되면 과열로 간주할지(fail-safe) 따로 정책이 필요합니다 → [T1-6](../T1-6_sensor_fault_detect/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Use time instead of a sample count: overheat after 3 s above `high`. | streak 대신 `uint32_t since_ms`를 저장합니다. 조건이 처음 참이 될 때 시각을 기록하고, `(uint32_t)(now - since_ms) >= hold_ms`로 판정합니다. 뺄셈 비교라 tick 랩어라운드에도 안전합니다. 샘플 간격이 들쭉날쭉해도 맞습니다. |
| Add warning / critical / shutdown levels. | `enum Level`과 레벨별 `{enter, exit}` 임계값 테이블을 두고, 한 단계씩 올라가고 내려가는 상태 머신으로 만듭니다. 각 전이는 같은 streak 로직을 씁니다. 셧다운은 보통 래치(자동 복귀 없음)로 합니다. |
| Call a callback instead of returning an event. | `det_init`에 `void (*cb)(void *ctx, Event e)`와 `ctx`를 받습니다. ISR 문맥이면 콜백 안에서 무거운 일을 하지 말라고 문서화합니다. |
| What if `det_update` is called from an ISR and `det_is_overheated()` from the main loop? | `overheated`는 `bool` 한 바이트라 읽기 자체는 원자적이지만, 컴파일러 캐싱을 막으려면 `volatile` 또는 C11 `atomic_bool`이 필요합니다. 여러 필드를 함께 읽으면 인터럽트를 잠깐 막습니다. |
| What happens with `n_confirm = 255` at 1 kHz? | 255 ms 지연입니다. 확인 횟수는 "반응 속도 vs 오탐"의 트레이드오프라서 샘플링 주기와 함께 정해야 합니다. |

## 7. 자주 하는 실수

- 전이할 때 `streak = 0`을 안 함 → 과열 직후 회복 카운트가 이전 값에서 시작해서 너무 빨리 RECOVERED
- 과열 상태에서도 `>= high`를 세서 `EV_OVERHEAT`를 반복 반환 → 테스트 3번 실패
- 띠 안의 값(820)이 회복 streak을 리셋하지 않음 → 테스트 4번에서 index 6에 일찍 회복
- `>`/`<`로 비교 → 경계값 테스트 6번 실패
- 이벤트 대신 현재 상태를 반환 → 호출자가 매 샘플마다 팬을 다시 켬
- `low >= high` 검증 누락 → 상태가 매 샘플 뒤집히는 진동
