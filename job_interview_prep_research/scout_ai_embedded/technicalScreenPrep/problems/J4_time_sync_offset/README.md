# J4 · Clock Offset from Request/Response Timestamps (NTP-style)

> **ID**: J4 · **Priority**: P1 · **Pattern**: 4-timestamp math + min-delay filter + slew-limited correction · **Time**: 20 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh J4` / `./run.sh J4 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: MCU와 host가 주고받은 timestamp 4개로 두 시계의 차이(offset)를 구하고(A), 여러 샘플 중 가장 믿을 만한 걸 고르고(B), 그 offset을 천천히 적용한다(C).
- **상황**: MCU는 자기 시계(µs, `int64_t`)로 센서 샘플에 시각을 찍는다. host의 fusion 코드는 그 시각을 host 시계 기준으로 필요로 한다. 그래서 MCU가 host에 ping을 보내고 timestamp 4개를 기록한다.
- **배경 개념**
  - **timestamp 4개**: t1 = MCU가 요청 보낸 시각(MCU 시계), t2 = host가 받은 시각(host 시계), t3 = host가 답장 보낸 시각(host 시계), t4 = MCU가 답장 받은 시각(MCU 시계).
  - **offset**: host 시계 − MCU 시계. `host_time = mcu_time + offset`. 가는 길과 오는 길 시간이 같다고 가정하고 `((t2 - t1) + (t3 - t4)) / 2`로 구한다.
  - **delay**: 왕복 시간에서 host가 붙잡고 있던 시간을 뺀 순수 네트워크 왕복 시간. `(t4 - t1) - (t3 - t2)`. delay가 작을수록 가는/오는 길 비대칭에 의한 오차가 작아서(오차 ≤ delay/2) 더 믿을 만하다.
  - **step vs slew**: step = 시계를 한 번에 새 값으로 점프. slew = 시간 흐름이 튀지 않게 조금씩 따라가기(여기선 local 1초당 최대 500 µs, 즉 500 ppm).
- **입력**: Part A/B는 `t[4]` 배열(또는 그 배열 `n`개). Part C는 `ClockSync`(`offset_us`, `synced`), 새로 잰 offset, 지난번 호출 이후 흐른 local 시간 `elapsed_local_us`.
- **출력**: A/B는 `*offset_us`, `*delay_us`를 채우고 성공 여부 `bool` 반환. C는 적용된 offset 반환, `to_host_time()`은 `local_us + offset`.
- **규칙**
  - 공통: `/ 2`는 **floor**(음의 무한대 쪽으로 내림). C의 `/`는 0 쪽으로 잘라서 다르다. `malloc` 금지, 정수 연산만.
  - Part A: `t4 < t1`, `t3 < t2`, `delay < 0` 중 하나면 `false`이고 출력은 건드리지 않는다.
  - Part B: 유효한 샘플 중 **delay가 가장 작은** 것 사용. 같으면 앞 index. 유효한 게 없으면 `false`.
  - Part C: 아직 sync 안 됐거나, measured와 applied의 차이 절댓값이 `128000`보다 **크면**(`>`) step. 아니면 slew: 최대 `500 × elapsed_local_us / 1e6` µs(정수 floor)만큼 measured 쪽으로 이동, `elapsed_local_us <= 0`이면 안 움직임.
- **예제 풀어 읽기**: `t = {10000, 15200, 15300, 10500}` → offset = (5200 + 4800) / 2 = 5000, delay = 500 − 100 = 400. 비대칭(가는 길 300, 오는 길 100)인 `{20000, 25300, 25400, 20500}` → offset 5100, delay 400, 오차 100 ≤ delay/2. `ClockSync {0, false}`에서 `apply(5000, 0)` → 5000(첫 sync는 step), 이후 `apply(7000, 1000000)`을 반복하면 5500 → 6000 → 6500 → 7000 → 7000.
- **놓치기 쉬운 것**: 음수 offset(MCU가 host보다 앞선 경우, -2000); 홀수 합의 반올림(1999 → 999, -2001 → -1001); 실패 시 출력값 유지; delay tie는 앞쪽 index; 유효 샘플 0개나 `samples == NULL, n == 0`; slew가 목표를 넘어가지 않기(overshoot 금지); `elapsed = 0`이면 그대로; 음수 방향 slew도 clamp(-1000 → -200); 차이가 정확히 128000이면 slew, 128001이면 step.
- **구현할 함수**
  - Part A: `sync_sample(t, offset_us, delay_us)`
  - Part B: `sync_best(samples, n, offset_us, delay_us)`
  - Part C: `clock_apply(cs, measured_offset_us, elapsed_local_us)`, `to_host_time(cs, local_us)`

## 1. Problem

A robot MCU stamps its sensor samples with its own free-running clock (microseconds, `int64_t`). The fusion code on the host needs those stamps on the **host clock**. To learn the mapping, the MCU pings the host and records four timestamps:

| | event | clock |
|---|---|---|
| `t[0]` = t1 | MCU sends request | MCU |
| `t[1]` = t2 | host receives request | host |
| `t[2]` = t3 | host sends reply | host |
| `t[3]` = t4 | MCU receives reply | MCU |

```
offset = ((t2 - t1) + (t3 - t4)) / 2     /* host_time = mcu_time + offset */
delay  = (t4 - t1) - (t3 - t2)           /* round trip minus host hold time */
```

- The `/ 2` rounds toward **negative infinity** (floor): `1999 / 2 -> 999`, `-2001 / 2 -> -1001`. (Plain C `/` truncates toward zero; agree on this first.)
- **Part A** — `sync_sample()`: compute offset and delay of one sample. Return `false` and leave the outputs untouched if `t4 < t1`, `t3 < t2`, or `delay < 0`.
- **Part B** — `sync_best()`: among `n` samples, use the **valid sample with the smallest delay**. Ties go to the earliest index. Return `false` if no sample is valid.
- **Part C** — `clock_apply()`: update the applied offset with a new measurement.
  - If not synced yet, or `|measured - applied| > STEP_THRESHOLD_US` (128000), **step**: applied = measured.
  - Otherwise **slew**: move toward measured by at most `MAX_SLEW_PPM` (500) × `elapsed_local_us` / 1e6 µs (integer floor; no movement if `elapsed_local_us <= 0`).
  - Return the applied offset. `to_host_time()` returns `local_us + applied offset`.
- No `malloc`, integer math only.

```c
#define STEP_THRESHOLD_US 128000
#define MAX_SLEW_PPM      500

typedef struct { int64_t offset_us; bool synced; } ClockSync;

bool    sync_sample(const int64_t t[4], int64_t *offset_us, int64_t *delay_us);
bool    sync_best(const int64_t (*samples)[4], size_t n, int64_t *offset_us, int64_t *delay_us);
int64_t clock_apply(ClockSync *cs, int64_t measured_offset_us, int64_t elapsed_local_us);
int64_t to_host_time(const ClockSync *cs, int64_t local_us);
```

### Example

```
true offset = +5000 us (host is ahead), 200 us each way, host holds 100 us
t = {10000, 15200, 15300, 10500}
offset = ((15200-10000) + (15300-10500)) / 2 = (5200 + 4800) / 2 = 5000
delay  = (10500-10000) - (15300-15200)      = 500 - 100          = 400

asymmetric: up 300, down 100, same true offset
t = {20000, 25300, 25400, 20500}  ->  offset 5100, delay 400   (error 100 <= delay/2)

ClockSync starts {0, false}:
apply(5000, 0)       -> 5000    first sync: step
apply(7000, 1000000) -> 5500    slew, +500 us per second
apply(7000, 1000000) -> 6000 -> 6500 -> 7000 -> 7000
```

### Constraints

- Timestamps are `int64_t` µs; differences fit easily (no wraparound in this version).
- `samples` may be `NULL` when `n == 0`. `cs` is never `NULL`.

### Clarifying questions to ask (first 3 minutes)

1. Which sign convention: `host - MCU` or `MCU - host`?
2. How should the `/ 2` round for negative or odd sums?
3. What makes a sample invalid — and should I write outputs when it is?
4. With several samples, average them or pick the min-delay one? Tie-break?
5. Step or slew? What threshold and max slew rate? Is the first sync always a step?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh J4`로 바로 테스트하세요. 테스트 8묶음(CHECK 29개)이 들어 있고, 스텁 상태에서는 22개가 실패합니다(`false`·`0`을 기대하는 CHECK 7개는 스텁도 우연히 통과).

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 공식을 외우지 말고 유도하기</summary>

진짜 offset을 `θ`, 올라가는 지연을 `u`, 내려오는 지연을 `d`라고 하면 `t2 − t1 = θ + u`, `t3 − t4 = θ − d`입니다. 더해서 2로 나누면 `θ + (u − d)/2` → 지연이 대칭(u = d)이면 정확히 `θ`. 빼면 `u + d` = delay입니다. 면접에서 이 두 줄을 화이트보드에 쓰면 공식을 몰라도 됩니다.

</details>

<details>
<summary>Hint 2 — floor 나눗셈</summary>

C의 `/`는 0 쪽으로 자릅니다(`-3/2 == -1`). floor가 필요하면 `q = x / 2; if (x < 0 && x % 2) q--;`. 음수에 `>> 1`은 C23 전까지 implementation-defined라 피하는 것이 안전합니다.

</details>

<details>
<summary>Hint 3 — slew 클램프</summary>

`err = measured − applied`, `max = elapsed * 500 / 1000000`. `|err| > 128000`이면 그냥 대입(step). 아니면 `err`를 `[-max, +max]`로 자른 뒤 더합니다. 이렇게 하면 목표를 지나치는 일(overshoot)이 자동으로 없습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

같은 메시지 왕복을 두 개의 시계로 잰다고 생각하면 됩니다. x축은 "진짜 시간"이고, t1·t4는 MCU 시계로, t2·t3는 host 시계로 찍힌 값입니다.

<div class="chart" data-src="chart1.svg" data-caption="예제 1: 왕복 500 µs 중 host가 붙잡은 100 µs를 빼면 delay 400. 양쪽 차이를 평균하면 지연이 상쇄되어 offset 5000"></div>

- `t2 − t1`에는 offset + 올라가는 지연, `t3 − t4`에는 offset − 내려오는 지연이 들어 있습니다. 평균을 내면 지연이 상쇄됩니다.
- 상쇄는 **대칭일 때만** 정확합니다. 비대칭이면 오차는 `(u − d)/2`이고, 그 크기는 최대 `delay/2`입니다(예제 2: up 300/down 100 → 5100, 오차 100 ≤ 200).
- 그래서 여러 번 재고 **delay가 가장 작은 샘플**을 고릅니다. delay가 작다는 것은 큐잉(버스 경합, OS 스케줄링)이 적었다는 뜻이고, 오차 상한 `delay/2`도 가장 작습니다. 평균을 내면 큐잉에 오염된 샘플이 결과를 끌고 갑니다.

### 4.2 step vs slew

측정한 offset을 바로 대입하면 `to_host_time()`이 거꾸로 가거나 갑자기 뛸 수 있습니다. 작은 오차는 천천히 따라가고(slew), 큰 오차만 즉시 맞춥니다(step). chrony/ntpd의 동작과 같습니다.

<div class="chart" data-src="chart2.svg" data-caption="왼쪽: 오차 2000 µs를 초당 500 µs씩 4초에 걸쳐 따라감. 오른쪽: 오차 293000 µs는 slew로 586초가 걸리므로 즉시 step"></div>

### 4.3 손으로 따라가기 (trace)

**Part B — `sync_best` (테스트 6)**

| i | t1, t2, t3, t4 | valid? | offset | delay | best 갱신 |
|---|---|---|---|---|---|
| 0 | 10000, 15200, 15300, 10500 | ✓ | 5000 | 400 | **첫 유효** → (5000, 400) |
| 1 | 20000, 25900, 26000, 21300 | ✓ | 5300 | 1200 | 1200 > 400 → 유지 |
| 2 | 30000, 35080, 35100, 30180 | ✓ | 5000 | 160 | 160 < 400 → **(5000, 160)** |
| 3 | 40000, 45000, 44000, 41000 | ✗ (t3 < t2) | – | – | skip |
| 4 | 50000, 55100, 55120, 50180 | ✓ | 5020 | 160 | 160 < 160 아님 → 유지 (tie → 앞의 것) |

결과: offset 5000, delay 160. 1번 샘플은 올라가는 길에 큐에서 기다려 offset이 300 µs 틀어졌는데, min-delay 필터가 걸러 냅니다.

**Part C — `clock_apply` (테스트 7·8, STEP 128000, 500 ppm)**

| 호출 (measured, elapsed) | applied 전 | err | max_slew | 동작 | applied 후 |
|---|---|---|---|---|---|
| (5000, 0) | 0, unsynced | – | – | **STEP** (첫 sync) | 5000 |
| (7000, 1000000) | 5000 | 2000 | 500 | slew +500 | 5500 |
| (7000, 1000000) ×3 | 5500 → 6000 → 6500 | 1500 → 500 | 500 | slew | 6000 → 6500 → 7000 |
| (7000, 1000000) | 7000 | 0 | 500 | 변화 없음 | 7000 |
| (9000, 0) | 7000 | 2000 | 0 | 시간 안 흐름 → 정지 | 7000 |
| (300000, 1000000) | 7000 | 293000 | – | **STEP** (> 128000) | 300000 |
| (299000, 400000) | 300000 | −1000 | 200 | slew −200 | 299800 |
| (427800, 1000000) | 299800 | 128000 | 500 | 경계값: `>` 아님 → slew | 300300 |
| (428301, 1000000) | 300300 | 128001 | – | **STEP** | 428301 |

`to_host_time(&cs, 1000000)`은 applied가 7000일 때 1007000입니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `sync_sample()`은 검사를 모두 통과한 **다음에만** 출력 포인터에 씁니다. 실패 시 호출자의 이전 값이 보존됩니다(테스트 5).
- `sync_best()`는 `sync_sample()`을 재사용하고, 비교를 `<`로 해서 동점이면 앞의 샘플을 유지합니다. `<=`로 쓰면 테스트 6이 5020으로 실패합니다.
- `clock_apply()`의 `elapsed * 500 / 1000000`은 곱셈 먼저, 나눗셈 나중입니다. 순서를 바꾸면 1초 미만 elapsed에서 0이 됩니다(400000 µs → 200 µs가 나와야 함).
- step 판정은 `mag > STEP_THRESHOLD_US`(엄격한 `>`)입니다. 경계값 128000은 slew 쪽입니다.

### 4.5 복잡도

- `sync_sample`, `clock_apply`, `to_host_time`: **O(1)**.
- `sync_best`: 시간 **O(n)**, 추가 메모리 **O(1)** — 최선값 두 개만 들고 다닙니다.

---

## 5. 임베디드 관점으로 말할 것

- **왜 이 문제인가**: 면접관 Jason은 자율주행(Latitude AI)과 Charles Machine Works에서 공용 라이브러리에 NTP time synchronization을 넣었던 경력이 있습니다. 로봇 센서 퓨전은 MCU의 IMU/엔코더 타임스탬프를 host(Jetson 등) 시계 위로 옮겨야 카메라·LiDAR와 정렬됩니다. "타임스탬프가 틀리면 퓨전이 틀린다"는 한 줄로 시작하면 좋습니다.
- **타임스탬프는 가능한 한 하드웨어 가까이**: t1은 UART/CAN 송신 직전이 아니라 송신 완료 인터럽트에서, t4는 수신 ISR에서 찍어야 소프트웨어 지연이 delay에 섞이지 않습니다. 센서 샘플도 DRDY 인터럽트(ISR)에서 free-running 타이머를 읽어 찍고, 처리는 task에서 합니다 → [E3](../E3_isr_to_task_pipeline/index.html).
- **int64 µs**: 32비트 µs 카운터는 71분이면 랩어라운드합니다. 하드웨어 타이머가 32비트라면 overflow 인터럽트로 상위 32비트를 확장해 64비트로 씁니다.
- **slew 중에도 단조 증가**: 500 ppm slew는 시계를 최대 0.05% 빠르게/느리게 할 뿐이라 시간이 거꾸로 가지 않습니다. step은 로그를 남기고, 퓨전 버퍼처럼 시간 순서에 의존하는 소비자에게 알려야 합니다.
- **min-delay 필터**: 실제로는 8–16개 샘플을 링버퍼에 두고 그중 최소 delay를 쓰는 방식이 흔합니다(NTP clock filter와 같은 생각) → [T2-1](../T2-1_ring_buffer/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why does an asymmetric link bias the offset? How big can the error be? | 공식이 `θ + (u − d)/2`를 계산하기 때문입니다. u, d ≥ 0이고 u + d = delay이므로 오차 `|u − d|/2 ≤ delay/2`. 측정만으로는 비대칭을 알 수 없어서, delay가 작은 샘플을 써서 상한을 줄이는 것이 최선입니다. |
| How does PTP (IEEE 1588) do better than NTP? | 같은 4-타임스탬프 수식이지만, 타임스탬프를 **PHY/MAC 하드웨어**가 패킷이 선을 지나는 순간 찍습니다. OS·드라이버·큐 지연이 빠져서 sub-µs 정확도가 나옵니다. 스위치가 체류 시간을 보정하는 transparent clock도 있습니다. |
| The offset keeps drifting between syncs. How do you handle frequency error? | 크리스털은 수십 ppm 틀립니다. (local time, offset) 쌍들을 모아 **선형 회귀**하면 기울기 = 주파수 오차(drift), 절편 = offset입니다. `host = local + offset + drift × (local − t_ref)`로 sync 사이를 보간하고, slew rate에도 drift 보정을 더합니다. |
| On Linux, which clock should the host use? | 간격 측정과 타임아웃은 `CLOCK_MONOTONIC`(뒤로 안 감, NTP slew만 반영). 로그·다른 장비와 맞추는 절대 시각은 `CLOCK_REALTIME`(step 가능). 센서 퓨전은 보통 monotonic 기반. chrony가 slew/step 정책(`makestep`)을 관리합니다. 원시 하드웨어 속도는 `CLOCK_MONOTONIC_RAW`. |
| Where do you timestamp a sensor sample — ISR or task? | ISR(DRDY 인터럽트)에서 타이머를 읽어 샘플과 같이 큐에 넣습니다. task에서 찍으면 스케줄링 지연(수백 µs~ms)이 가변 오차로 들어갑니다. DMA라면 버퍼 완료 시각에서 샘플 간격으로 역산합니다. |
| What if the robot has GPS? | GPS **PPS** 신호는 매 초 정각에 ±수십 ns 정확도의 엣지를 줍니다. 타이머 input capture로 엣지 시각을 잡으면 MCU 시계의 offset과 drift를 직접 구할 수 있고, host도 같은 PPS로 맞추면(chrony + PPS) 모든 장비가 같은 시간축을 씁니다. |
| Why not just average all samples? | 큐잉 지연은 항상 양수 방향으로만 늘고 분포가 한쪽으로 긴 꼬리를 가집니다. 평균은 그 꼬리에 끌려가지만, 최소 delay 샘플은 거의 "순수 전송 시간"에 가깝습니다. |

## 7. 자주 하는 실수

- offset 부호를 반대로 정의(`MCU − host`) → 테스트 1부터 전부 부호가 틀림. 질문 1번으로 먼저 고정하기
- `/ 2`를 그대로 써서 음수 홀수 합이 0 쪽으로 잘림 → 테스트 4의 `-1001`이 `-1000`이 됨
- 실패한 샘플에서도 출력 포인터에 씀 → 테스트 5 실패
- `sync_best`에서 `<=` 사용 → 동점에서 뒤 샘플(5020)이 선택되어 테스트 6 실패
- `elapsed / 1000000 * 500`처럼 나눗셈을 먼저 함 → 400000 µs에서 slew 0, 테스트 8 실패
- slew에서 클램프 없이 `applied += max_slew` → 목표를 지나침(overshoot), 7000에서 멈추지 않음
- 첫 호출을 step으로 처리하지 않음 → 첫 오차가 128 ms 이내면 0에서 500 ppm으로 기어가며 최대 256초 동안 틀린 시간을 줌(테스트 7 첫 CHECK 실패)
