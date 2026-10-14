# T1-8 · Blocking API Wrapper

> **ID**: T1-8 · **Priority**: P2 · **Pattern**: dedicated sampler thread + mutex-protected cache + ring buffer window · **Time**: 40 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-8` / `./run.sh T1-8 sol`

## 1. Problem

The vendor driver gives you a single function:

```c
int16_t read_temp(void);   /* temp in 0.1 degC, or TEMP_ERR (INT16_MIN) on a bus error */
```

It **blocks for up to 1 second** (one I2C conversion) and is **not thread-safe** — two threads inside it at the same time corrupt the bus. Many application threads need the temperature. Build a small service on top of it:

```c
int  sampler_start(void);              /* start ONE background sampler thread; 0 ok, -1 if running */
void sampler_stop(void);               /* stop and join; safe to call twice */
bool get_latest_temp(int16_t *out);    /* NEVER waits for the sensor; false if no reading yet */
bool is_overheating(int16_t thresh);   /* max of readings with now - t <= 5000 ms is > thresh */
```

- Only the sampler thread may call `read_temp()`. **Never hold a lock across `read_temp()`.**
- Drop `TEMP_ERR` readings and keep the last good value.
- Keep history in a fixed-size ring buffer of `(t_ms, temp)` — no `malloc`. Use `now_ms()` for time.
- `is_overheating` returns `false` when there is no reading inside the window.
- `start`/`stop` are called from one control thread.

The file provides a fake sensor and a fake clock so the test runs in about 1 second: `read_temp()` polls with `nanosleep` until the test feeds a value, or returns `TEMP_ERR` after 200 ms (a scaled-down "1 s"). `now_ms()` only moves when the test sets it.

### Example

```
now=1000  sensor -> 250   get_latest_temp -> 250   is_overheating(800) -> false
now=2000  sensor -> 900   get_latest_temp -> 900   is_overheating(800) -> true
now=3000  sensor -> 300   get_latest_temp -> 300   is_overheating(800) -> true  (900 is 1 s old)
now=7000  (no reading)                             is_overheating(800) -> true  (7000-2000 = 5000)
now=7001  (no reading)                             is_overheating(800) -> false (900 expired)
```

### Constraints

- One read takes about 1 s, so the 5 s window holds about 5–6 readings. Ring capacity `HIST_CAP = 16`.
- `get_latest_temp` / `is_overheating` may be called from any number of threads at any rate.

### Clarifying questions to ask (first 3 minutes)

1. Should reads be continuous (a background thread), or on demand when someone asks? How fresh must "latest" be?
2. What should `get_latest_temp` return before the first reading, or when the sensor has been failing for a long time?
3. "Last 5 s" — measured by the time the reading **finished**? Inclusive at exactly 5000 ms?
4. Is `>` or `>=` the overheat rule? And with no readings in the window — `false`, or "unknown" (fail-safe)?
5. How quickly must `stop()` return? (It cannot interrupt a read that is already in progress.)

## 2. Your workspace

`./run.sh T1-8`로 바로 테스트하세요. 위쪽 GIVEN 블록(가짜 센서·시계)은 고치지 말고, TODO 부분만 채우면 됩니다. stub은 약 2.5초, 정답은 약 1초 걸립니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 누가 센서를 만지는가</summary>

센서를 부르는 스레드를 **딱 하나(sampler)**로 정하면 "thread-safe가 아님" 문제는 설계로 사라집니다. 나머지 스레드는 센서가 아니라 sampler가 남긴 **복사본**만 읽습니다. 그래서 `get_latest_temp`는 절대 1초를 기다리지 않습니다.

</details>

<details>
<summary>Hint 2 — 락은 어디까지?</summary>

```
v = read_temp();          // 락 밖 (최대 1 s)
t = now_ms();
lock;  latest = v; ring_push(t, v);  unlock;   // 락 안 (µs)
```

`read_temp()`를 락 안에 넣으면 그동안 모든 호출자가 락에서 최대 1초 막힙니다. 요구사항 "never blocks" 위반입니다.

</details>

<details>
<summary>Hint 3 — 5초 윈도우 max</summary>

링버퍼를 **최신 → 과거** 순으로 돌면서 `(uint32_t)(now - t) > 5000`이 나오면 멈춥니다(그보다 오래된 건 더 오래됨). 그 전까지의 max를 구해 `> thresh`. 링 인덱스는 `(head + CAP - 1 - k) % CAP`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 스레드 타임라인

<div class="chart" data-src="chart1.svg" data-caption="sampler 스레드만 read_temp()를 부르고, 끝날 때마다 아주 짧게 락을 잡아 publish. 호출자들은 언제 불러도 즉시 반환. TEMP_ERR는 버리고 v2 유지"></div>

구조는 세 덩어리입니다.

1. **sampler 스레드**: `while (running) { v = read_temp(); if (err) continue; lock; 저장; unlock; }`
2. **공유 상태** (`g_mx` 하나로 보호): `latest`, `have`, 링버퍼 `hist[16]`, `head`, `count`
3. **읽기 API**: 락 잡고 복사/스캔하고 놓기. 센서는 절대 안 건드림

`running`은 `atomic_bool`입니다. 루프 조건으로만 쓰이고 다른 데이터와 묶여 있지 않으므로 락 없이 atomic 하나면 충분합니다.

### 4.2 락 범위 (lock scope)

<div class="chart" data-src="chart2.svg" data-caption="왼쪽이 정답. 오른쪽처럼 read_temp()를 락 안에 두면 get_latest_temp()가 최대 1초 막힙니다"></div>

### 4.3 손으로 따라가기 (trace) — 테스트 흐름

| 단계 | now_ms | 사건 | latest | 링버퍼 (t, temp) | is_overheating(800) |
|---|---|---|---|---|---|
| 1 | 0 | start 전 | 없음 | 비어 있음 | false |
| 3 | 1000 | feed 250 | 250 | (1000,250) | false (max 250) |
| 3 | 2000 | feed 900 | 900 | +(2000,900) | **true** (max 900) |
| 4 | 3000 | feed 300 | 300 | +(3000,300) | **true** — 최신값은 300이지만 900이 윈도우 안 |
| 4 | 7000 | 시계만 이동 | 300 | 그대로 | **true** — 7000−2000 = 5000 ≤ 5000. (1000,250)은 6000이라 스캔 중단 |
| 4 | 7001 | 시계만 이동 | 300 | 그대로 | **false** — (3000,300) 나이 4001은 포함, (2000,900) 나이 5001에서 중단 → max 300 |
| 5 | 7001 | read 타임아웃 → TEMP_ERR | 300 (유지) | 그대로 | – |
| 6 | 10000…11900 | feed 400…419 (20개) | 419 | 총 23번 push → head = 23 % 16 = 7, count = 16, 값 404…419만 남음 | `is_overheating(418)` true, `(419)` false |
| 7 | – | stop | 419 (stop 후에도 읽힘) | 그대로 | – |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `sampler_main`에서 `now_ms()`는 `read_temp()`가 **끝난 뒤** 찍습니다. 1초짜리 읽기의 "측정 시각"을 시작으로 볼지 끝으로 볼지는 정의 문제지만, 끝을 쓰면 윈도우가 데이터를 너무 일찍 버리지 않습니다.
- `is_overheating`은 락 안에서 `now_ms()`를 읽습니다. 락 밖에서 읽으면 그 사이 sampler가 "미래" 타임스탬프를 넣어 `now - t`가 음수(→ `uint32_t`로 아주 큰 수)가 되는 경합이 생길 수 있습니다.
- `sampler_stop`: `running = false` 후 `pthread_join`. sampler가 `read_temp()` 안에 있으면 그 읽기가 끝날 때까지(최대 1 s, 여기선 200 ms) 기다립니다. 테스트 7번이 이 상한을 확인합니다.
- `get_latest_temp`의 반환을 `bool` + out 파라미터로 둔 이유: `int16_t` 하나로는 "아직 값 없음"을 표현할 값이 없습니다(모든 값이 유효한 온도).
- 하네스의 `g_dev_overlap`은 `read_temp()` 동시 진입을 감지합니다. 테스트 7번에서 false여야 합니다.

### 4.5 복잡도

- `get_latest_temp`: **O(1)**, 락 보유 시간은 복사 몇 번.
- `is_overheating`: **O(W)**, W = 윈도우 안 샘플 수(≈ 5). 최대 `HIST_CAP` = 16.
- 메모리: 링버퍼 16 × 8바이트 + 스레드 1개. `malloc` 없음.

---

## 5. 임베디드 관점으로 말할 것

- **"센서 소유자는 하나"**: 버스·주변장치는 담당 스레드(또는 RTOS 태스크) 하나가 소유하고, 나머지는 메시지/공유 캐시로만 접근하는 게 임베디드의 표준 패턴입니다. 락으로 직렬화하는 것보다 단순하고 우선순위 역전도 없습니다.
- **락 밖 I/O**: 블로킹 I/O를 락 안에서 하면 모든 읽기 스레드가 I/O 시간만큼 막히고, RTOS에서는 높은 우선순위 태스크가 낮은 우선순위 태스크의 I2C를 기다리는 **우선순위 역전**이 생깁니다.
- **fail-safe 정책**: 센서가 계속 `TEMP_ERR`이면 윈도우가 비어 `is_overheating`이 false가 됩니다. 안전 관점에서는 "모름"을 과열 쪽으로 처리해야 할 수 있습니다. 실무 API라면 `enum { OK, HOT, STALE }` 3상태로 돌려줍니다 → [T1-6](../T1-6_sensor_fault_detect/index.html).
- **RTOS 버전**: pthread 대신 태스크 1개 + 뮤텍스(또는 크리티컬 섹션). `latest`가 32비트 이하면 **atomic store/load 한 번**으로 락 없이도 됩니다(Cortex-M에서 정렬된 32비트 접근은 원자적).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Can `get_latest_temp` be lock-free? | `(valid << 16) \| (uint16_t)temp`를 `_Atomic uint32_t` 하나에 담아 store/load. 값과 유효 플래그가 한 워드라 찢어지지 않습니다. 링버퍼까지 락 없이 하려면 seqlock이 필요합니다. |
| Reads might be much faster than 1 s. Is the ring still enough? | 아닙니다. 16칸이 5초보다 빨리 차면 윈도우 안 샘플을 잃어 max가 틀립니다. 샘플러를 고정 주기로 페이싱하거나, **초 단위 버킷 max**(6칸)로 바꾸면 샘플 속도와 무관하게 O(1) 메모리입니다 → [T2-4](../T2-4_window_max/index.html)의 단조 덱도 대안. |
| `stop()` must return within 10 ms. | 진행 중인 `read_temp()`는 끊을 수 없으므로 `pthread_join`을 기다리지 않는 detach + "종료 중" 플래그, 또는 드라이버에 타임아웃/취소 API가 필요합니다. 읽기 사이에 쉬는 구간이 있다면 `pthread_cond_timedwait`로 즉시 깨웁니다. |
| Readers want fresh data on demand instead of a background thread. | 요청 결합(single-flight): 첫 요청자만 읽고 나머지는 condvar로 그 결과를 기다림. Verkada 원본 유형 → [04_temp_single_flight](../../../../../dons_study_note_from_experience/ConcurrencyPrepNoteBasedOnVerkadaSrEmbdLinuxEng/index.html). |
| What if the clock is wall time and NTP steps it backwards? | `CLOCK_MONOTONIC`을 씁니다. 벽시계가 뒤로 가면 `now - t`가 랩되어 거대한 값 → 윈도우가 통째로 비어 보입니다. |

## 7. 자주 하는 실수

- `read_temp()`를 락 안에서 호출 → `get_latest_temp()`가 진행 중인 읽기만큼(여기선 최대 200 ms, 실제는 1 s) 막힘 (테스트 2번 실패)
- `get_latest_temp`가 직접 `read_temp()`를 부름 → 블로킹 + 동시 진입 (`g_dev_overlap` true, 테스트 7번 실패)
- 윈도우를 "최신 5개"로 구현 → 시간 기반이 아님 (테스트 4번의 7001 케이스 실패)
- 스캔 중단 조건을 `now - t >= 5000`으로 → 정확히 5000 ms인 샘플을 버려 7000에서 false (테스트 4번 실패)
- `TEMP_ERR`를 그대로 publish → latest가 −3276.8 °C (테스트 5번 실패)
- `stop()`을 두 번 부르면 `pthread_join`을 두 번 → 미정의 동작. `g_started` 플래그로 막음
