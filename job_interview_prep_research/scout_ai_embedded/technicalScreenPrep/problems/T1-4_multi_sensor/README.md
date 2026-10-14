# T1-4 · Multi-Sensor Overheat

> **ID**: T1-4 · **Priority**: P1 · **Pattern**: array of per-item state machines + input validation · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-4` / `./run.sh T1-4 sol`

## 1. Problem

A robot has up to `MAX_SENSORS = 16` temperature sensors. Their readings arrive **interleaved** in one array, in arrival order. Each reading has a sensor id, a timestamp in ms, and a temperature in tenths of a degree Celsius.

Run an **independent** overheat detector for every sensor, with the same rules as [T1-2](../T1-2_streaming_hysteresis/index.html):

- NORMAL → OVERHEATED after `N_CONFIRM = 3` consecutive readings `>= HIGH_C10 (850)` **from that sensor**. Emit an `Alert {id, t_ms}` on that reading.
- OVERHEATED → NORMAL after 3 consecutive readings `<= LOW_C10 (800)` from that sensor (no alert). A reading that breaks the streak resets that sensor's counter.
- Alerts are returned **in input order**.

Bad input:

- `id >= MAX_SENSORS` → ignore the reading and count it in `*dropped`.
- A reading whose `t_ms` is **not after** the previous accepted reading of the same sensor (older or equal; compare wrap-safe) → ignore it and count it in `*dropped`. It must not change the streak.

Write at most `out_cap` alerts into `out` but **always return the total number of alerts**. `out` may be `NULL` when `out_cap == 0`; `dropped` may be `NULL`. No `malloc`.

```c
#define MAX_SENSORS 16
#define HIGH_C10    850
#define LOW_C10     800
#define N_CONFIRM   3
typedef struct { uint8_t id; uint32_t t_ms; int16_t temp_c10; } Reading;
typedef struct { uint8_t id; uint32_t t_ms; } Alert;

size_t process(const Reading *r, size_t n, Alert *out, size_t out_cap, size_t *dropped);
```

### Example

```
(id, t_ms, temp):
(0,0,860) (1,0,700) (2,0,900)
(0,100,870) (1,100,900) (2,100,700)
(0,200,880) (1,200,910) (20,200,999) (2,200,900)
(1,300,920) (0,300,890) (2,300,900)
(1,250,950)                     <- older than sensor 1's last reading (300)
(2,400,900)

return 3, out = {0,200} {1,300} {2,400}, dropped = 2 (id 20, and sensor 1 at t=250)
```

### Constraints

- `0 <= n <= 1,000,000`. Timestamps are `uint32_t` ms and may wrap around 2^32.
- The array is in arrival order; different sensors' timestamps may interleave arbitrarily.

### Clarifying questions to ask (first 3 minutes)

1. Are the detectors independent per sensor, with the same thresholds for all sensors?
2. What should happen to an id outside the valid range — ignore, count, or fail?
3. Is "time order" per sensor or global? What do we do with a stale or duplicate timestamp?
4. Should alerts be ordered by input order or sorted by timestamp?
5. Do I report only the overheat start, or recovery events too?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 로컬에서 `./run.sh T1-4`로 바로 테스트하세요. 테스트 7개가 들어 있습니다. 센서별 상태 구조체는 직접 설계하세요.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — Item = 센서별 상태</summary>

T1-2의 `Detector`를 **센서 수만큼 배열로** 둡니다: `SensorState st[MAX_SENSORS]`. id가 0..15라서 해시가 필요 없고 `st[id]`로 바로 찾습니다. 함수 시작에서 `memset`으로 0 초기화하면 "한 번도 안 나온 센서"도 자동으로 NORMAL, streak 0입니다.

</details>

<details>
<summary>Hint 2 — 검증 순서</summary>

1. `id >= MAX_SENSORS`이면 **배열에 접근하기 전에** 버립니다(범위 밖 쓰기 = 메모리 오염).
2. 그 센서를 본 적이 있고 `(int32_t)(t - last_t) <= 0`이면 버립니다. 처음 보는 센서는 비교할 기준이 없으니 `bool seen`이 필요합니다.
3. 통과한 읽기만 `last_t`를 갱신하고 streak 로직으로 보냅니다.

</details>

<details>
<summary>Hint 3 — 출력은 T1-1과 같은 패턴</summary>

`if (cnt < out_cap) out[cnt] = …; cnt++;` 알림은 입력을 앞에서부터 처리하면서 생기는 순서 그대로 쌓이므로 정렬이 필요 없습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

센서마다 레인(lane)을 하나씩 그리면, 섞여 들어온 입력이 사실은 **서로 독립인 T1-2 감지기 3개**라는 게 보입니다. 각 레인의 streak은 다른 센서의 읽기에 영향을 받지 않습니다.

<div class="chart" data-src="chart1.svg" data-caption="예제 입력을 센서별 레인으로 펼친 그림. s=는 그 읽기 처리 후 streak. sensor 1의 t=250 읽기는 입력에서 t=300 뒤에 와서 stale로 버림, id 20은 범위 밖이라 버림"></div>

센서 2를 보면 700(t=100)이 streak을 0으로 되돌려서, 900이 다시 세 번 연속된 t=400에서야 알림이 나옵니다. sensor 1의 t=250(950)을 받아 줬다면 이미 과열 상태라 이벤트는 없지만, 과열 전이었다면 시간을 거슬러 streak을 올리는 버그가 됩니다.

### 4.2 상태 설계

| 필드 | 타입 | 이유 |
|---|---|---|
| `last_t` | `uint32_t` | 역행·중복 타임스탬프 검사 (랩 안전 뺄셈 비교) |
| `streak` | `uint8_t` | 반대 상태로 가는 연속 횟수 (T1-2와 동일) |
| `seen` | `bool` | 첫 읽기에는 비교 기준이 없음. `last_t = 0`을 "없음"으로 쓰면 t=0 읽기가 버려짐 |
| `overheated` | `bool` | 현재 상태 |

구조체 하나 8바이트, 16개면 128바이트입니다.

### 4.3 손으로 따라가기 (trace)

| i | id | t_ms | temp | 검사 | 그 센서 streak | 이벤트 | cnt | dropped |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 860 | ok | s0: 1 | – | 0 | 0 |
| 1 | 1 | 0 | 700 | ok | s1: 0 | – | 0 | 0 |
| 2 | 2 | 0 | 900 | ok | s2: 1 | – | 0 | 0 |
| 3 | 0 | 100 | 870 | ok | s0: 2 | – | 0 | 0 |
| 4 | 1 | 100 | 900 | ok | s1: 1 | – | 0 | 0 |
| 5 | 2 | 100 | 700 | ok | s2: 0 (리셋) | – | 0 | 0 |
| 6 | 0 | 200 | 880 | ok | s0: 3 → 0 | **ALERT {0,200}** | 1 | 0 |
| 7 | 1 | 200 | 910 | ok | s1: 2 | – | 1 | 0 |
| 8 | 20 | 200 | 999 | **id ≥ 16** | – | drop | 1 | 1 |
| 9 | 2 | 200 | 900 | ok | s2: 1 | – | 1 | 1 |
| 10 | 1 | 300 | 920 | ok | s1: 3 → 0 | **ALERT {1,300}** | 2 | 1 |
| 11 | 0 | 300 | 890 | ok | s0: 0 (과열 중, ≤ 800 아님) | – | 2 | 1 |
| 12 | 2 | 300 | 900 | ok | s2: 2 | – | 2 | 1 |
| 13 | 1 | 250 | 950 | **250 − 300 < 0** | – | drop | 2 | 2 |
| 14 | 2 | 400 | 900 | ok | s2: 3 → 0 | **ALERT {2,400}** | 3 | 2 |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `if (r[i].id >= MAX_SENSORS)`가 `&st[r[i].id]`보다 **먼저** 옵니다. 순서가 바뀌면 포인터 계산만으로도 UB입니다.
- `(int32_t)(r[i].t_ms - s->last_t) <= 0`: 두 시각 차이가 2^31 ms(약 24.8일) 미만이면 랩어라운드가 있어도 "더 최근인가"를 정확히 판단합니다(테스트 6번: `0xFFFFFFF0 → 0x10`은 +32 ms).
- 같은 시각(`== 0`)도 버립니다. 같은 샘플이 두 번 들어오면 한 번의 고온이 streak을 2 올리기 때문입니다.
- `SensorState`는 함수 지역 배열이라 호출마다 새로 시작합니다. 실제 펌웨어라면 상태를 호출자 구조체로 빼서 스트리밍으로 이어 갑니다(§6).

### 4.5 복잡도

- 시간 **O(n)** — 읽기마다 O(1)(배열 인덱싱 + T1-2 로직).
- 추가 메모리 **O(MAX_SENSORS)** = 128바이트 스택. 출력 버퍼는 호출자가 줍니다.

---

## 5. 임베디드 관점으로 말할 것

- **신뢰할 수 없는 입력에서 온 인덱스는 항상 검사**: id는 CAN/I2C 프레임에서 옵니다. 검사 없이 `st[id]`에 쓰면 스택의 다른 변수를 덮어씁니다. 실제 사고 원인 1순위인 패턴입니다.
- **버린 개수를 세서 돌려주기**: 조용히 버리면 센서 배선 문제나 버스 오류를 아무도 모릅니다. `dropped` 카운터는 진단(telemetry)으로 올라갑니다.
- **고정 크기 테이블**: 센서 수 상한이 정해져 있으니 정적 배열이면 충분합니다. 메모리 사용량이 컴파일 시점에 정해지고, 할당 실패가 없습니다.
- **구조체 크기와 정렬**: 필드를 큰 것부터(`uint32_t` → `uint8_t` → `bool`) 놓아서 패딩 없이 8바이트입니다. 순서를 섞으면 12바이트가 될 수 있다는 걸 말할 수 있으면 좋습니다.
- 한 센서만 볼 때의 로직은 [T1-2](../T1-2_streaming_hysteresis/index.html), 고장 판정(stuck/dropout)을 센서별로 붙이면 [T1-6](../T1-6_sensor_fault_detect/index.html), 가장 뜨거운 K개 센서는 [T2-10](../T2-10_top_k_sensors/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Sensor ids are arbitrary 32-bit values, still at most 16 sensors. | 크기 32(2의 거듭제곱, 부하율 ≤ 50%)인 오픈 어드레싱 해시 테이블을 둡니다. `slot = hash(id) & 31`, 충돌 시 선형 탐사, 빈 칸이면 새 센서 등록. 가득 차면 새 센서를 버리고 `dropped`로 셉니다. 16개면 단순 선형 검색도 충분히 빠르다는 점도 말합니다. |
| Report how many sensors are overheated right now, in O(1). | `uint8_t n_hot` 카운터를 두고 OVERHEAT 전이에서 `++`, RECOVERED 전이에서 `--`. 또는 `uint16_t hot_mask`의 비트를 세우고 `popcount`. 마스크면 "어떤 센서인지"도 바로 압니다. |
| Which sensor overheated first? | 첫 알림이 곧 답입니다(입력 순서 = 도착 순서). 타임스탬프 기준이라면 알림 중 `t_ms`가 가장 이른 것을 랩 안전 비교로 고릅니다. |
| Make it streaming instead of a batch function. | `SensorState st[MAX_SENSORS]`를 `typedef struct { SensorState st[MAX_SENSORS]; uint32_t dropped; } Monitor;`로 빼고 `Event monitor_update(Monitor *m, const Reading *r)`가 알림 여부를 돌려주게 합니다. |
| What if a sensor stops reporting? | 센서별 `last_t`가 있으니 주기적으로 `now - last_t > timeout`을 검사해서 dropout으로 판정합니다. 과열 상태에서 끊기면 보수적으로 과열 유지(fail-safe) → [T1-6](../T1-6_sensor_fault_detect/index.html). |

## 7. 자주 하는 실수

- id 범위 검사 없이 `st[id]` 접근 → 테스트 1·7번에서 스택 오염(ASan이 잡음)
- 센서 하나의 streak을 전역으로 둠 → 다른 센서의 읽기가 streak을 리셋해서 알림을 놓침
- `last_t == 0`을 "본 적 없음"으로 씀 → t=0 읽기 뒤에 같은 t=0 중복이 오면 처음 보는 센서로 착각해서 통과시킴
- `t_ms < last_t`로 비교 → 랩어라운드(테스트 6번)에서 정상 읽기를 버림
- 버린 읽기에서도 `last_t`나 streak을 갱신 → 테스트 3번 실패
- `id >= MAX_SENSORS` 대신 `id > MAX_SENSORS` → id 16이 통과, 테스트 7번 실패
