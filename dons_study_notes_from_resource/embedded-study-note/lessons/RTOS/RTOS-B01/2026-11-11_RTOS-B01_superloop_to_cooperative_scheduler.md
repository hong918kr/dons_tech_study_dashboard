---
id: RTOS-B01
time: 60분
updated: 2026-11-11
---
# RTOS-B01 슈퍼루프에서 협력형 스케줄러로

## 학습 목표

- 주기가 다른 작업 3개(10/50/100 ms)를 **슈퍼루프(superloop, 카운터 + `HAL_Delay`)** 와 **타임스탬프 기반 run-to-completion 협력형 스케줄러(cooperative scheduler)** 로 각각 구현하고, 시뮬레이션 시계로 작업별 **최대 지연(latency)·지터(jitter)·실행 횟수** 를 결정적으로 측정한다.
- tick wrap 에 안전한 비교 `(int32_t)(now - next) >= 0` 이 왜 맞는지, 한계(2^31)와 C11 상 이식성(구현 정의 변환)까지 설명한다.
- `next_run += period` 와 `next_run = now + period` 의 차이(드리프트), 밀린 릴리스를 **몰아서 실행(catch-up)** 할지 **건너뛸지(skip)** 를 숫자로 비교하고 작업 성격에 맞게 고른다.
- 협력형 스케줄링에서 지연은 **가장 긴 다른 작업 한 번** 으로 묶인다(최상위 작업: ≤ max C − 1)는 것을 측정과 무작위 검증으로 보이고, 긴 작업을 **상태 기계(state machine)** 로 쪼개 25 ms → 3 ms 로 줄인다. 그리고 이것이 왜 선점(preemption, RTOS-B05)의 동기인지 말할 수 있다.

## 1. 문제

"RTOS 없이 주기 작업 여러 개를 어떻게 돌리나요? 슈퍼루프의 문제는? 언제 RTOS 가 필요하죠?" — 펌웨어 면접의 첫 RTOS 질문이고, 답은 "슈퍼루프 → 타임스탬프 스케줄러 → 선점형 RTOS" 의 진화를 **숫자로** 설명할 수 있느냐로 갈린다. STM32 보드의 첫 펌웨어는 거의 다 `while (1) { ...; HAL_Delay(10); }` 로 시작하고, 기능이 붙으면서 "가끔 센서 샘플이 늦다", "printf 넣었더니 제어가 흔들린다" 를 겪는다. 이 레슨은 그 과정을 시뮬레이션으로 재현한다.

작업 3개 (드론/로봇 제어 보드 풍, 1 tick = 1 ms):

| 작업 | 주기 | 비용 | 실제 모습 |
|---|---|---|---|
| SENSOR | 10 ms | 1 ms | IMU(SPI) 읽기 — 100 Hz 샘플, 간격이 일정해야 적분/필터가 맞다 |
| CONTROL | 50 ms | 4 ms | PID 계산 + PWM 갱신 |
| TELEM | 100 ms | 2 ms (DMA UART) / **30 ms** (블로킹 `printf` → `HAL_UART_Transmit`, 115200 bps ≈ 11.5 B/ms → 345 B ≈ 30 ms) | 텔레메트리 한 프레임 |

```svg
<svg viewBox="0 0 740 316" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">(a) 슈퍼루프 — 순서와 delay 로 시간을 맞춘다</text>
  <rect x="20" y="34" width="300" height="168" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="34" y="58" fill="currentColor" font-family="monospace">while (1) {</text>
  <text x="50" y="80" fill="currentColor" font-family="monospace">sensor();          // 1</text>
  <text x="50" y="102" fill="currentColor" font-family="monospace">if (n % 5 == 0)  control();</text>
  <text x="50" y="124" fill="currentColor" font-family="monospace">if (n % 10 == 0) telem();</text>
  <text x="50" y="146" fill="currentColor" font-family="monospace">n++;</text>
  <text x="50" y="168" fill="currentColor" font-family="monospace">HAL_Delay(10);</text>
  <text x="34" y="190" fill="currentColor" font-family="monospace">}</text>
  <text x="20" y="226" fill="currentColor">한 바퀴 = 10 ms + 작업 시간 → 주기가 늘어난다</text>
  <text x="20" y="248" fill="currentColor">(드리프트). 긴 작업은 모든 작업을 민다.</text>
  <text x="380" y="20" fill="currentColor" font-weight="bold">(b) 타임스탬프 협력형 스케줄러</text>
  <rect x="380" y="34" width="340" height="112" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="392" y="56" fill="currentColor" font-family="monospace">#  fn       period next_run</text>
  <text x="392" y="78" fill="currentColor" font-family="monospace">0  sensor   10     120</text>
  <text x="392" y="100" fill="currentColor" font-family="monospace">1  control  50     150</text>
  <text x="392" y="122" fill="currentColor" font-family="monospace">2  telem    100    200</text>
  <text x="392" y="140" fill="currentColor">위에 있을수록 우선순위 높음</text>
  <rect x="380" y="158" width="340" height="96" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="392" y="180" fill="currentColor" font-family="monospace">for (;;) {  i = 첫 due 작업;</text>
  <text x="392" y="202" fill="currentColor" font-family="monospace">  없으면 WFI; 있으면 next += period;</text>
  <text x="392" y="224" fill="currentColor" font-family="monospace">  fn() 끝까지 실행 (선점 없음) }</text>
  <text x="392" y="246" fill="currentColor">due = (int32_t)(now - next) &gt;= 0</text>
  <text x="20" y="280" fill="currentColor">둘 다 run-to-completion: 한 번 시작한 작업은 끝날 때까지 CPU 를 쥔다.</text>
  <text x="20" y="302" fill="currentColor">차이는 시간 기준 — (a) delay 누적, (b) 절대 시각 격자.</text>
</svg>
```

**시뮬레이션 시계 (하네스 제공).** 실제 시간을 쓰면 macOS 스케줄러 잡음 때문에 숫자가 매번 다르다(5. 풀이의 호스트 데모에서 직접 본다). 그래서 `g_now`(uint32_t ms) 를 하네스가 쥐고, 작업은 `sim_work(ms)` 로 "CPU 를 ms 만큼 쓴다". 같은 코드는 매번 같은 숫자를 낸다.

| 하네스 함수 | 의미 | 실제 MCU |
|---|---|---|
| `g_now` | 현재 시각 ms, 32-bit, wrap 함 | `HAL_GetTick()` (SysTick 1 kHz 가 `uwTick++`) |
| `sim_work(ms)` | 지금 작업이 CPU 를 ms 동안 씀 | 작업 본문 실행 |
| `sim_delay(ms)` | 바쁜 대기 (아무 일도 못 함) | `HAL_Delay(ms)` |
| `sim_idle(1)` | 할 일 없음, 다음 tick 까지 잠 | `__WFI()` |
| `trace_release(j, rel)` | 새 릴리스 시작 기록 → 지연 = now − rel, 간격, 타임라인 | 트레이스 훅 (FreeRTOS `traceTASK_SWITCHED_IN` 자리) |
| `job_call(j)` | 작업 함수 호출 + 한 번 호출 길이 측정 | 함수 호출 |

용어: **릴리스(release)** = 작업이 실행돼야 하는 시각, **지연(latency)** = 실제 시작 − 릴리스, **지터(jitter)** = 지연의 최대 − 최소, **간격(ivl)** = 이번 시작 − 지난 시작. 타임라인은 `R` 실행, `r` 릴리스됐는데 못 돎, `.` 그 외.

### Part A — 슈퍼루프와 협력형 스케줄러

- `time_due(now, t)` — `now` 가 `t` 에 도달/경과했나. wrap 안전, 거리 2^31 미만이면 정확.
- `superloop_run(J, duration)` — 매 패스 SENSOR, 5 패스마다 CONTROL, 10 패스마다 TELEM, 끝에 `sim_delay(10)`.
- `sched_add(s, name, fn, ctx, period, offset)` — 작업 테이블에 등록, `next_run = now + offset`. 꽉 찼거나 period 0 이면 −1.
- `sched_dispatch(s)` — 테이블을 앞(우선순위 높음)부터 보고 할 일이 있는 **첫 작업 하나** 를 실행, 그 인덱스 반환 (없으면 −1). 새 릴리스면 `trace_release` → `next_run += period` → `job_call`.
- 측정: 긴 작업 없이 1000 ms. 스케줄러는 SENSOR 100/100, 간격 정확히 10, 슈퍼루프는 덜 돌고(84/100) 계속 밀린다.

### Part B — tick wrap, 밀린 릴리스, 드리프트

1. **wrap**: 시계를 `0xFFFFFF00` (256 ms 뒤 wrap) 에서 시작해도 모든 통계와 타임라인이 0 에서 시작한 것과 **바이트 단위로 같다**.
2. **catch-up vs skip**: TELEM 이 30 ms 블로킹이면 SENSOR 릴리스 3개가 밀린다. `skip_missed = false` 면 몰아서 3번(간격 1 ms), `true` 면 이미 지난 릴리스를 `skipped++` 하고 건너뛴다. `runs + skipped == 100`.
3. **드리프트 0**: 10 초 동안 SENSOR 1000/1000, CONTROL 200/200, TELEM 100/100 (catch-up).

### Part C — 긴 작업 하나가 지터를 얼마나 늘리나, 그리고 쪼개기

TELEM 을 30 ms 블로킹으로 바꾸고 슈퍼루프·스케줄러를 다시 잰다. 그다음 `telem_sm_step()` 을 상태 기계로 짠다: 한 번 불릴 때 **한 조각만** (FORMAT 2 ms → SEND 4 ms × 7), 끝나지 않았으면 `JOB_MORE` 를 돌려주고 스케줄러는 다음 디스패치에서 이어 부른다 (그 사이 SENSOR 가 끼어들 수 있다). 일의 총량은 그대로 30 ms × 10 = 300 ms. 목표: SENSOR 최대 지연 ≤ 조각 길이 − 1 = 3 ms.

## 2. Problem (English)

> **From a superloop to a cooperative time-triggered scheduler.**
>
> ```c
> bool time_due(uint32_t now, uint32_t t);           // wrap-safe "now has reached t" (|now - t| < 2^31)
> void superloop_run(job_t J[3], uint32_t duration); // while(1){ every job by pass counter; HAL_Delay(10); }
> int  sched_add(sched_t *s, const char *name, job_fn fn, void *ctx, uint32_t period, uint32_t offset);
> int  sched_dispatch(sched_t *s);                   // run ONE due job (table order = priority) to completion
> job_ret_t telem_sm_step(void *ctx);                // one bounded chunk per call; JOB_MORE until done
> ```
>
> Three periodic jobs: sensor 10 ms (1 ms of CPU), control 50 ms (4 ms), telemetry 100 ms (2 ms, or 30 ms if it does a blocking UART print). Time is a 32-bit millisecond counter that wraps.
>
> 1. Implement both designs and report, per job, the number of runs, max release latency and jitter over 1 s, with and without the 30 ms telemetry job.
> 2. The scheduler must not drift (`next_run += period`), must survive tick wrap, and must support either catch-up or skipping of missed releases.
> 3. Bring the sensor's worst latency below 4 ms without making telemetry do less work.
>
> Follow-ups: Why is `(int32_t)(now - next) >= 0` correct across wrap, and when does it break? `next += period` vs `now + period`? What bounds latency in a cooperative scheduler, and what changes with preemption? How do you split a long job? Where does the watchdog kick go?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부 — 0.1 초 안에 끝나고 exit 1):

```text
FAIL starter.c:263  time_due(0x00000002u, 0xFFFFFFF8u)
FAIL starter.c:264  !time_due(0xFFFFFFF8u, 0x00000002u)
FAIL starter.c:265  time_due(1000u + 0x7FFFFFFFu, 1000u) && !time_due(1000u + 0x80000000u, 1000u)
FAIL starter.c:273  bad == 0
FAIL starter.c:280  sched_add(&s, "a", job_work, &w, 10, 3) == 0 && s.job[0].next_run == 1237 && s.job[0].row == 0
...
FAIL starter.c:338  sp.j[0].lat_max <= TELEM_CHUNK_MS - 1 && sp.j[0].runs == 100
4/28 passed
```

- 스텁 `time_due` 는 `now >= t` 라서 같은 쪽 비교는 맞지만 wrap 케이스에서 틀린다. 스텁 `sched_add/sched_dispatch` 는 아무것도 안 하므로 run 수가 전부 0.
- **무한 루프가 없다.** 메인 루프는 `g_now - start < duration` 과 반복 상한(2,000,000)으로 끝난다. 디스패치가 −1 이면 `sim_idle(1)` 로 시계가 간다.
- 스텁 `telem_sm_step` 은 "블로킹 30 ms" 버전이다 — 즉 **고치기 전의 펌웨어** 그 자체.
- `(void)superloop_call;` 같은 줄은 아직 안 쓰는 하네스 함수의 unused 경고(`-Werror`)를 막는다.

## 4. 힌트

<details>
<summary>힌트 1 — time_due: 뺄셈은 unsigned 로, 판정은 부호로</summary>

`now - t` 는 unsigned 라서 mod 2^32 로 정확하다. 이것을 `int32_t` 로 보면 "t 가 now 보다 최대 2^31 − 1 앞인지 뒤인지" 가 부호로 나온다. `now >= t` 는 wrap 직후(`now = 2`, `t = 0xFFFFFFF8`)에 거짓을 낸다.

</details>

<details>
<summary>힌트 2 — 슈퍼루프: period / 10 이 "몇 패스마다" 다</summary>

`pass % (J[i].period / LOOP_BASE_MS) == 0` 이면 그 작업을 부른다 (SENSOR 1, CONTROL 5, TELEM 10). 패스 끝에 `sim_delay(LOOP_BASE_MS)`. 루프 조건은 `g_now - start < duration` (unsigned 차 → wrap 안전).

</details>

<details>
<summary>힌트 3 — dispatch: 하나만 실행하고 바로 return</summary>

여러 개를 한 번에 다 돌리면, 긴 작업 뒤에 다시 테이블 맨 위(SENSOR)부터 볼 기회를 놓친다. "첫 due 하나 → 실행 → return" 이면 다음 호출이 다시 맨 위부터 본다 = 우선순위.

</details>

<details>
<summary>힌트 4 — 새 릴리스의 순서: 기록 → next 갱신 → (skip) → 실행</summary>

`trace_release(j, j->next_run)` 을 **갱신 전에** 불러야 지연이 원래 릴리스 기준이 된다. 갱신은 `next_run += period`. skip 이면 `while (time_due(g_now, next_run)) { next_run += period; skipped++; }`. `in_progress` 인 작업은 릴리스가 아니므로 `trace_release` 없이 `job_call` 만.

</details>

<details>
<summary>힌트 5 — 상태 기계: 지역 변수는 다음 호출까지 살아남지 않는다</summary>

"지금 몇 번째 조각인가" 는 `telem_sm_t` (ctx) 안에 둔다. `switch (t->state)` 로 들어가 한 조각 하고 return. 마지막 조각이면 `frames++`, 상태를 `TS_FORMAT` 으로 되돌리고 `JOB_DONE`.

</details>

## 5. 풀이

### Part A — 네 함수

```c
static bool time_due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

static int sched_dispatch(sched_t *s) {
    for (int i = 0; i < s->n; i++) {
        job_t *j = &s->job[i];
        if (j->in_progress) { j->in_progress = (job_call(j) == JOB_MORE); return i; }   /* 이어서 */
        if (time_due(g_now, j->next_run)) {
            trace_release(j, j->next_run);           /* 지연 = now - 원래 릴리스 */
            j->next_run += j->period;                /* 절대 시각 격자 → 드리프트 0 */
            if (s->skip_missed)
                while (time_due(g_now, j->next_run)) { j->next_run += j->period; j->skipped++; }
            j->in_progress = (job_call(j) == JOB_MORE);
            return i;                                /* 하나만! 다음 호출은 다시 맨 위부터 */
        }
    }
    return -1;                                       /* 할 일 없음 → 메인 루프가 WFI */
}
```

**`time_due` 가 맞는 이유.** `d = now − t (mod 2^32)`. 진짜 거리 `Δ = now − t` (정수)가 `−2^31 ≤ Δ < 2^31` 이면 `d` 를 32-bit 2의 보수로 읽은 값이 정확히 `Δ` 다. 그래서 부호 = 앞/뒤. wrap 은 상관없다 (mod 연산이 이미 흡수).

| now | t | `now >= t` | `(int32_t)(now - t)` | 맞는 답 |
|---|---|---|---|---|
| 5 | 5 | true | 0 → true | due |
| `0x00000002` | `0xFFFFFFF8` | **false** | 10 → true | due (wrap 지나 10 ms 경과) |
| `0xFFFFFFF8` | `0x00000002` | **true** | −10 → false | 아직 (10 ms 남음) |
| t + `0x7FFFFFFF` | t | true | 2^31−1 → true | due |
| t + `0x80000000` | t | true | −2^31 → **false** | 한계: 거리 2^31 이상은 구분 불가 |

- 한계: 1 ms tick 이면 2^31 ms ≈ **24.8 일**. 작업 주기·마감이 그보다 짧으면 문제없다. 32-bit 카운터 자체는 2^32 ms ≈ **49.7 일** 에 wrap — "몇 주 돌면 멈추는" 펌웨어 버그의 단골 원인이다.
- **이식성 (Don 이 물을 부분).** C11 6.3.1.3p3: 표현 범위를 넘는 값을 부호 있는 정수로 변환하면 **구현 정의(implementation-defined)** 결과(또는 구현 정의 시그널)다. UB 는 아니다. GCC 문서는 "mod 2^N 으로 줄인다" 고 정의하고 (arm-none-eabi-gcc 도 GCC), clang 도 같게 동작한다. 완전히 이식적인 형태는 unsigned 만 쓰는 `(uint32_t)(now - t) < 0x80000000u` — 하네스가 무작위 10만 쌍에서 두 식이 같음을 확인한다 (`bad == 0`).

**슈퍼루프** 는 힌트 2 그대로. **sched_add** 는 `memset` 후 필드 설정, `next_run = g_now + offset`, `row = 인덱스`, `return s->n++`.

#### 1000 ms, 긴 작업 없음 — 실제 출력

```text
[A] superloop, base
  SENSOR  runs  84/100 skip  0  lat max 169  jitter 169  ivl 11..17   step max  1
  CONTROL runs  17/20  skip  0  lat max 161  jitter 160  ivl 59..61   step max  4
  TELEM   runs   9/10  skip  0  lat max 165  jitter 160  ivl 120..120  step max  2
  busy 170 ms, idle 0 ms, delay 840 ms
[A] scheduler, base
  SENSOR  runs 100/100 skip  0  lat max   0  jitter   0  ivl 10..10   step max  1
  CONTROL runs  20/20  skip  0  lat max   1  jitter   0  ivl 50..50   step max  4
  TELEM   runs  10/10  skip  0  lat max   5  jitter   0  ivl 100..100  step max  2
  busy 200 ms, idle 800 ms, delay 0 ms
```

손으로 확인 (슈퍼루프): 패스 0 = S 1 + C 4 + T 2 + delay 10 = 17 ms, 패스 1–4 = 11 ms, 패스 5 = 15 ms, 패스 6–9 = 11 ms → **10 패스 = 120 ms** (TELEM `ivl 120`). "100 ms 마다" 가 실제로는 120 ms 마다다. 1000 ms 동안 SENSOR 84번, 이상적 격자 대비 169 ms 뒤처졌고 실행할수록 더 벌어진다. CPU 는 840 ms 를 `HAL_Delay` 로 **바쁘게 기다리는 데** 썼다 (idle 0 → WFI 절전도 못 함).

스케줄러: t=0 에 셋 다 due → 테이블 순서대로 S(0–1) C(1–5) T(5–7). 그래서 CONTROL 지연 1, TELEM 지연 5 가 **매 릴리스 똑같다** (지터 0). SENSOR 간격 정확히 10.

### Part B — wrap · catch-up vs skip · 드리프트

```text
[B1] start 0xFFFFFF00: sched long identical=1, sched base identical=1, loop identical=1
[B2] scheduler, long, catch-up
  SENSOR  runs 100/100 skip  0  lat max  25  jitter  25  ivl  1..35   step max  1
[B2] scheduler, long, skip
  SENSOR  runs  80/100 skip 20  lat max  25  jitter  25  ivl  5..35   step max  1
  busy 460 ms, idle 540 ms, delay 0 ms
[B3] 10 s long: catch-up runs 1000/200/100, skip runs 800 + skipped 200
```

- **B1**: 지연·간격은 전부 `g_now - x` (unsigned 차) 로만 계산하고 비교는 `time_due` 로만 하므로 시작 시각이 결과에 전혀 들어가지 않는다. 그래서 `result_t` 가 `memcmp` 로 같다. 연습 1 은 `now >= next` 로 바꿨을 때 같은 시작점에서 무엇이 망가지는지 센다.
- **B2 catch-up**: TELEM 이 5–35 를 쥔다. SENSOR 릴리스 10·20·30 이 막혔다가 35·36·37 에 연달아 돈다 (`ivl 1`). 지연은 25, 16, 7. 릴리스는 하나도 안 잃지만 **IMU 를 1 ms 간격으로 3번 읽은 샘플** 은 필터 입장에서 쓸모없다 (dt 가 10 ms 라고 가정한 적분이 틀어진다).
- **B2 skip**: 35 에 한 번 돌고 20·30 은 건너뜀 → 100 ms 마다 2개, 1 초에 20개 skip, SENSOR 작업 20 ms 절약 (busy 480 → 460). `runs + skipped == 100` 은 하네스가 확인.
- **어느 쪽?** 샘플링·제어(최신 값이 중요)는 skip, 개수가 의미 있는 일(소프트웨어 RTC 초 카운트, 에너지 적산, 정해진 수의 패킷)은 catch-up. FreeRTOS `xTaskDelayUntil` 은 catch-up 쪽이다: 깨울 시각을 `*pxPreviousWakeTime + xTimeIncrement` 로 계산하고 (tasks.c:2434) 이미 지났으면 지연 없이 바로 돌아오되 기준 시각은 무조건 `*pxPreviousWakeTime = xTimeToWake` 로 한 칸만 전진한다 (tasks.c:2468) — 밀린 만큼 연달아 돈다 (상세는 RTOS-B06).
- **B3**: `next_run += period` 는 릴리스 격자가 시작 시각 + k·period 로 **고정** 이라 10 초 뒤에도 정확히 1000번. `now + period` 로 쓰면 늦을 때마다 격자가 밀린다 → 연습 2: 10 초에 **792번** (208번 손실).

### Part C — 긴 작업과 상태 기계

```c
static job_ret_t telem_sm_step(void *ctx) {
    telem_sm_t *t = ctx;
    switch (t->state) {
    case TS_FORMAT: sim_work(TELEM_FORMAT_MS); t->work_ms += TELEM_FORMAT_MS;
                    t->chunk = 0; t->state = TS_SEND; return JOB_MORE;
    case TS_SEND: default:
        sim_work(TELEM_CHUNK_MS); t->work_ms += TELEM_CHUNK_MS;
        if (++t->chunk == TELEM_CHUNKS) { t->frames++; t->state = TS_FORMAT; return JOB_DONE; }
        return JOB_MORE;
    }
}
```

실제 타임라인 (solution.c 출력):

```text
[C] superloop, long
  tick   : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  SENSOR : R . . . . . . . . . r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r r R r r r r
  CONTROL: r R R R R . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
  TELEM  : r r r r r R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R . . . . . . . . . . . . . . .
[C] scheduler, long (catch-up)
  SENSOR : R . . . . . . . . . r r r r r r r r r r r r r r r r r r r r r r r r r R R R . . R . . . . . . . . .
  CONTROL: r R R R R . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
  TELEM  : r r r r r R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R . . . . . . . . . . . . . . .
[C] scheduler, split
  SENSOR : R . . . . . . . . . r R . . . . . . . . R . . . . . . . . . r r r R . . . . . . R . . . . . . . . .
  CONTROL: r R R R R . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
  TELEM  : r r r r r R R R R R R r R R R R R R R R r R R R R R R R R R R R R r R R R R . . . . . . . . . . . .
```

```svg
<svg viewBox="0 0 740 344" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">실제 타임라인 0–49 ms (solution.c 출력 그대로, 1 칸 = 1 ms)</text>
  <text x="10" y="46" fill="currentColor" font-weight="bold">슈퍼루프 + HAL_Delay(10), TELEM 30 ms 블로킹</text>
  <text x="80" y="62" fill="currentColor">0</text> <text x="200" y="62" fill="currentColor">10</text> <text x="320" y="62" fill="currentColor">20</text> <text x="440" y="62" fill="currentColor">30</text> <text x="560" y="62" fill="currentColor">40</text>
  <text x="10" y="78" fill="currentColor" font-family="monospace">SENSOR</text>
  <rect x="80" y="66" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="66" width="420" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="620" y="66" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="632" y="66" width="48" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="94" fill="currentColor" font-family="monospace">CONTROL</text>
  <rect x="80" y="82" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="92" y="82" width="48" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="110" fill="currentColor" font-family="monospace">TELEM</text>
  <rect x="80" y="98" width="60" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="140" y="98" width="360" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="130" fill="currentColor" font-weight="bold">협력형 스케줄러, TELEM 30 ms 한 번에 (catch-up)</text>
  <text x="80" y="146" fill="currentColor">0</text> <text x="200" y="146" fill="currentColor">10</text> <text x="320" y="146" fill="currentColor">20</text> <text x="440" y="146" fill="currentColor">30</text> <text x="560" y="146" fill="currentColor">40</text>
  <text x="10" y="162" fill="currentColor" font-family="monospace">SENSOR</text>
  <rect x="80" y="150" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="150" width="300" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="500" y="150" width="36" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="560" y="150" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="178" fill="currentColor" font-family="monospace">CONTROL</text>
  <rect x="80" y="166" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="92" y="166" width="48" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="194" fill="currentColor" font-family="monospace">TELEM</text>
  <rect x="80" y="182" width="60" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="140" y="182" width="360" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="214" fill="currentColor" font-weight="bold">협력형 스케줄러, TELEM 을 2 + 7×4 ms 상태 기계로</text>
  <text x="80" y="230" fill="currentColor">0</text> <text x="200" y="230" fill="currentColor">10</text> <text x="320" y="230" fill="currentColor">20</text> <text x="440" y="230" fill="currentColor">30</text> <text x="560" y="230" fill="currentColor">40</text>
  <text x="10" y="246" fill="currentColor" font-family="monospace">SENSOR</text>
  <rect x="80" y="234" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="200" y="234" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="212" y="234" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="234" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="440" y="234" width="36" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="476" y="234" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="560" y="234" width="12" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="262" fill="currentColor" font-family="monospace">CONTROL</text>
  <rect x="80" y="250" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="92" y="250" width="48" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="278" fill="currentColor" font-family="monospace">TELEM</text>
  <rect x="80" y="266" width="60" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="140" y="266" width="72" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="212" y="266" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="224" y="266" width="96" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="320" y="266" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="332" y="266" width="144" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="476" y="266" width="12" height="14" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="488" y="266" width="48" height="14" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="10" y="292" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="303" fill="currentColor">R 실행 중</text> <rect x="120" y="292" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="140" y="303" fill="currentColor">r 릴리스됐는데 못 돎 (= 지연)</text>
  <text x="350" y="303" fill="currentColor">빈칸 = 대기 없음 / idle / HAL_Delay</text>
  <text x="10" y="316" fill="currentColor">위: 슈퍼루프는 SENSOR 가 계속 r — 주기 자체가 늘어나 영원히 뒤처진다 (드리프트).</text>
  <text x="10" y="334" fill="currentColor">가운데: SENSOR 10·20·30 이 35 까지 막혔다가 35·36·37 에 몰아서 실행. 아래: 최대 3 ms.</text>
</svg>
```

split 줄 읽기: TELEM 은 5–7 FORMAT, 7–11 조각 1. SENSOR 릴리스 10 은 조각 1 이 끝난 11 에 시작 (지연 1) — TELEM 의 `r` 칸(11)이 "양보한 자리" 다. 릴리스 30 은 조각 6 (29–33) 에 걸려 지연 3 = 조각 길이 − 1. TELEM 프레임은 38 에 끝난다 (블로킹이면 35) — **SENSOR 를 위해 TELEM 이 3 ms 늦게 끝나는 거래** 다.

#### 요약 표 (1000 ms, solution.c 마지막 출력 + 각 블록)

| 설계 | SENSOR runs | SENSOR 최대 지연 / 지터 | SENSOR 간격 | CONTROL 최대 지연 | TELEM 한 번 호출 최대 |
|---|---|---|---|---|---|
| 슈퍼루프, base | 84/100 | 169 / 169 (드리프트) | 11..17 | 161 | 2 |
| 슈퍼루프, long | 67/100 | 332 / 332 (드리프트) | 11..45 | 328 | 30 |
| 스케줄러, base | 100/100 | **0 / 0** | 10..10 | 1 | 2 |
| 스케줄러, long, catch-up | 100/100 | **25 / 25** | 1..35 | 1 | 30 |
| 스케줄러, long, skip | 80 (+20 skip) | 25 / 25 | 5..35 | 1 | 30 |
| 스케줄러, split | 100/100 | **3 / 3** | 7..13 | 1 | 4 |

```svg
<svg viewBox="0 0 740 288" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SENSOR(10 ms) 시작 간격의 범위와 최대 지연 — solution.c 실제 출력 (1000 ms)</text>
  <text x="166" y="36" fill="currentColor">0</text>
  <line x1="170" y1="40" x2="170" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <text x="246" y="36" fill="currentColor">10</text>
  <line x1="250" y1="40" x2="250" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <text x="326" y="36" fill="currentColor">20</text>
  <line x1="330" y1="40" x2="330" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <text x="406" y="36" fill="currentColor">30</text>
  <line x1="410" y1="40" x2="410" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <text x="486" y="36" fill="currentColor">40</text>
  <line x1="490" y1="40" x2="490" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <text x="566" y="36" fill="currentColor">50</text>
  <line x1="570" y1="40" x2="570" y2="224" stroke="currentColor" stroke-width="0.3"/>
  <line x1="250" y1="40" x2="250" y2="224" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3"/>
  <text x="10" y="61" fill="currentColor">슈퍼루프 base</text>
  <rect x="258" y="50" width="48" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="314" y="62" fill="currentColor">11..17 · 지연 169</text>
  <text x="10" y="91" fill="currentColor">슈퍼루프 long</text>
  <rect x="258" y="80" width="272" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="92" fill="currentColor">11..45 · 지연 332</text>
  <text x="10" y="121" fill="currentColor">스케줄러 base</text>
  <rect x="248" y="110" width="4" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="256" y="122" fill="currentColor">10..10 · 지연 0</text>
  <text x="10" y="151" fill="currentColor">스케줄러 long catch-up</text>
  <rect x="178" y="140" width="272" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="458" y="152" fill="currentColor">1..35 · 지연 25</text>
  <text x="10" y="181" fill="currentColor">스케줄러 long skip</text>
  <rect x="210" y="170" width="240" height="16" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="458" y="182" fill="currentColor">5..35 · 지연 25</text>
  <text x="10" y="211" fill="currentColor">스케줄러 split</text>
  <rect x="226" y="200" width="48" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="282" y="212" fill="currentColor">7..13 · 지연 3</text>
  <text x="10" y="242" fill="currentColor">점선 = 이상적인 간격 10 ms. 막대 = 실제 시작 간격 최소..최대 (ms). 지연 = 최대 지연 (ms).</text>
  <text x="10" y="260" fill="currentColor">슈퍼루프의 지연 169 / 332 는 이상적 격자 대비 누적 드리프트 — 실행할수록 커진다.</text>
  <text x="10" y="278" fill="currentColor">catch-up 의 1 ms 간격 = 밀린 샘플 3 개를 몰아서 읽은 것 (IMU 로는 쓸모없다).</text>
</svg>
```

**상한: "가장 긴 다른 한 번 호출 − 1".** 협력형에서 SENSOR(최상위)가 릴리스되는 순간 다른 작업 X 가 돌고 있으면, X 의 그 호출이 끝나는 다음 디스패치에서 SENSOR 가 뽑힌다 (테이블 맨 위니까). 릴리스와 시작이 정수 ms 이고 X 는 릴리스 전에 시작했으므로 지연 ≤ C_X − 1. 밀린 SENSOR 릴리스가 몰아치면 뒤쪽 릴리스일수록 지연이 줄어든다 (C_S < period 이면). 그래서

`L_max(SENSOR) ≤ max_{X ≠ SENSOR} C_X − 1` (단, C_X = 한 번 호출의 길이)

- long: max C = 30 → 상한 29, 측정 25 (위상 때문 — TELEM 이 SENSOR 릴리스 0 직후 5 에 시작). TELEM 위상(offset)을 9 로 주면 9–39 가 SENSOR 릴리스 10 을 막아 정확히 29 가 나온다 (확인함). 연습 3 의 무작위 300 시나리오(시작 시각·비용·주기·위상 무작위)에서 위반 0, 상한과 **정확히 같은** 경우 196 — 상한이 빡빡하다(tight).
- split: max C = max(CONTROL 4, 조각 4) = 4 → 상한 3, 측정 3.
- 아래 우선순위 작업(CONTROL)은 여기에 **위 작업들의 몰아치기** 가 더해진다. 연습 3: TELEM 위상을 40 으로 바꾸면 CONTROL 지연 = 막힘 21 + SENSOR 3번 = **24** (기본 위상에서는 1 이었다 — 측정값 1 은 운이었다).

**선점이 필요한 이유 (→ RTOS-B05).** 협력형의 응답성은 **가장 느린 작업의 작성자** 가 정한다. 누군가 30 ms 짜리 `printf` 를 넣으면 IMU 지연 상한이 29 ms 가 된다 — 코드 리뷰로만 지켜야 하는 전역 계약이다. 선점형 고정 우선순위에서는 SENSOR 가 릴리스되는 즉시 TELEM 을 밀어내므로 SENSOR 지연은 낮은 작업의 길이와 **무관** 해진다 (남는 것은 ISR·임계 구역·스케줄러 오버헤드). 대신 공유 데이터 보호(뮤텍스, RTOS-B10), 우선순위 역전(RTOS-M03), 태스크마다 스택이 필요하다. 협력형은 공유 데이터 문제가 거의 없고(작업 사이에서만 전환) 스택 하나로 돈다 — 그래서 작은 MCU 에선 여전히 좋은 선택이다.

**쪼개는 방법들.** ① 상태 기계(이 레슨) ② DMA + 완료 인터럽트: CPU 는 전송 시작만 하고 바로 return (TELEM 2 ms 버전이 이것) ③ protothreads / FreeRTOS 코루틴: `switch` + `__LINE__` 매크로로 상태 기계를 자동 생성 — `crSTART` 는 `switch( ((CRCB_t *)(pxCRCB))->uxState ) { case 0:` 이고 (croutine.h:214–216), `crSET_STATE0` 은 `uxState = (__LINE__ * 2); return; case (__LINE__ * 2):` 이다 (croutine.h:258–260). 스택이 없으므로 croutine.h 예제 주석도 "블로킹 호출을 넘어 값을 유지하려면 변수를 `static` 으로" 라고 한다 (힌트 5 와 같은 이유) ④ RTOS 태스크로 옮기고 선점에 맡긴다.

#### 호스트 데모 — 진짜 시계로 (`demo_host_clock.c`, HOST 맛보기)

<!-- code: demo_host_clock.c -->

`clock_gettime(CLOCK_MONOTONIC)` 를 ms 로 자른 uint32_t 에 offset 을 더해 실행 약 1 초 지점에서 **실제로 wrap** 시킨다. 작업은 진짜 바쁜 대기, idle 은 `nanosleep(200 µs)`.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_host_clock.c -o demo_host_clock && ./demo_host_clock
```

```text
long (30 ms)   (wrap seen: yes)
  SENSOR  runs 200/200  lat max 28 ms  jitter 28 ms
  CONTROL runs  40/40   lat max  3 ms  jitter  2 ms
  TELEM   runs  20/20   lat max  8 ms  jitter  3 ms
split (2+7x4)  (wrap seen: yes)
  SENSOR  runs 200/200  lat max  3 ms  jitter  3 ms
  CONTROL runs  40/40   lat max  2 ms  jitter  1 ms
  TELEM   runs  20/20   lat max  6 ms  jitter  1 ms
```

같은 바이너리를 8번 더 돌린 SENSOR 최대 지연 (long split 순서, ms): `27 27 · 28 23 · 29 50 · 27 7 · 25 6 · 25 4 · 27 3 · 26 9`. long 은 25–29 로 일정(시뮬레이션의 25–29 범위와 같다), split 은 보통 3–9 인데 가끔 23·50 이 튄다 — macOS 가 이 프로세스를 재우거나 `nanosleep` 이 늦게 깬 것이다(호스트 OS 잡음). wrap 은 매번 지나갔고 runs 는 매번 200/200. 이 잡음 때문에 체크는 시뮬레이션 시계로 한다.

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | 베어메탈 STM32 HAL (문서·소스 기준, 이 맥에서 미빌드) | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|---|
| `g_now` | `HAL_GetTick()` (SysTick → `HAL_IncTick` → `uwTick`) | `xTaskGetTickCount()` | `k_uptime_get_32()` | `osKernelGetTickCount()` |
| `time_due` | `HAL_Delay` 안의 `(HAL_GetTick() - tickstart) < wait` 와 같은 unsigned 차 | 커널 내부는 overflow 리스트 2개 (RTOS-B06), `xTaskDelayUntil` 의 wrap 분기 tasks.c:2436–2457 | — | — |
| `sim_delay(10)` 슈퍼루프 | `HAL_Delay(10)` (바쁜 대기) | `vTaskDelay(10)` (상대 지연, 블록) | `k_msleep(10)` | `osDelay(10)` |
| `next_run += period` | 직접 구현 | `xTaskDelayUntil(&last, P)` (tasks.c:2434, 2468) | `k_sleep(K_TIMEOUT_ABS_MS(t))` | `osDelayUntil(t)` |
| 협력형 디스패치 | 이 레슨의 `sched_dispatch` | `configUSE_PREEMPTION 0` — "a yield should not be performed just because a higher priority task has been woken" (tasks.c:68–71), tick 에서도 깨운 태스크로 전환 안 함 (tasks.c:4929–4957) / 코루틴 `vCoRoutineSchedule` (croutine.c:299–333) | 음수 우선순위 = cooperative thread | (RTX5 설정 의존) |
| `JOB_MORE` (양보) | 상태 기계 return | `taskYIELD()` (task.h:227) / `crDELAY` (croutine.h:313) | `k_yield()` | `osThreadYield()` |
| `sim_idle(1)` | `__WFI()` | idle 태스크 (+ tickless idle) | idle thread | — |
| 워치독 (연습 4) | `HAL_IWDG_Refresh()` | — (앱 몫) | `wdt_feed()` | — |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 28/28 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o so_asan && ./so_asan  # 28/28, 경고 0
```

5번 반복 실행 모두 `28/28 passed`, 출력 md5 5번 동일 (결정적), 실행 0.1 초. 단일 스레드라 TSan 대상 없음. 참고: UBSan 은 `(int32_t)(now - t)` 를 잡지 않는다 — UB 가 아니라 구현 정의 변환이기 때문이다. 작은 시험 프로그램을 `-fsanitize=undefined,implicit-conversion` 으로 돌려 봐도 이 캐스트(`-10` 이 나옴)는 보고되지 않았다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (now >= next)` | wrap 직전: 이미 wrap 한 `next` 가 작아 보여 **일찍 몰아서** 실행 / wrap 직후: 아직 wrap 안 한 `next` 가 커 보여 **49.7 일 동안 멈춤** (연습 1: CONTROL 5번 뒤 정지, TELEM 20번 일찍) | `(int32_t)(now - next) >= 0` |
| `next = now + period` | 늦을 때마다 격자가 밀린다. 10 초에 SENSOR 792/1000 (연습 2) | `next += period` |
| 지연을 갱신 **후** `next_run` 으로 계산 | 지연이 항상 −period 근처 (음수 → unsigned 거대값) | `trace_release(j, j->next_run)` 을 먼저 |
| due 작업을 한 바퀴에 전부 실행 | 긴 작업 뒤 맨 위(SENSOR)를 다시 볼 기회 없이 아래 작업까지 연달아 → 상위 작업 지연 증가 | 하나 실행하고 `return i` |
| 슈퍼루프에 `HAL_Delay` 로 주기 맞추기 | 한 바퀴 = delay + 작업 시간 → "100 ms" 가 120 ms (긴 작업이면 148 ms). CPU 는 바쁘게 기다려 절전도 못 함 | 타임스탬프 비교 (`HAL_GetTick`) |
| 상태 기계의 진행 상황을 지역 변수에 | 다음 호출에서 사라진다 → 처음부터 다시 | ctx 구조체 / `static` (croutine.h 주석과 같은 이유) |
| 밀린 릴리스 정책을 생각 안 함 | catch-up 이 1 ms 간격 3번 샘플을 만들어 필터 dt 가정이 깨진다 | 작업 성격별로 catch-up / skip 선택 |
| 2^31 ms 이상 미래의 마감 | `time_due` 가 "이미 지남" 으로 읽는다 | 긴 시간은 64-bit 카운터나 일/초 단위 카운터로 |
| 워치독을 타이머 ISR 에서 kick | 메인 루프가 멈춰도 ISR 은 돈다 → 워치독이 의미 없음 | 메인 루프(또는 모든 작업의 alive 플래그 확인 후)에서 kick |

## 8. 면접 Q&A

<details>
<summary>Q1. 슈퍼루프, 협력형 스케줄러, 선점형 RTOS — 각각 언제 쓰나요?</summary>

슈퍼루프: 작업이 한두 개, 타이밍이 느슨할 때 (LED + 버튼). 숫자: 이 레슨에서 "100 ms 마다" 가 실제 120 ms, 1 초에 SENSOR 84/100. 협력형 타임스탬프 스케줄러: 주기 작업 여러 개, 모든 작업이 짧게 끝나도록 **작성할 수 있을 때**. 스택 하나, 락 거의 불필요, 결정적이고 분석 쉬움 (time-triggered 아키텍처 — Pont 의 책 — 가 안전 분야에서 쓰이는 이유). 선점형 RTOS: 긴 작업(통신 스택, 파일시스템, 로그)과 짧은 마감(제어 루프)이 섞일 때. 대가: 태스크별 스택 RAM, 공유 데이터 보호, 우선순위 역전. 꼬리: "RTOS 쓰면 지터가 0?" → 아니다. ISR, 임계 구역, 같은 우선순위 time slice, 커널 오버헤드가 남는다. 단지 **낮은 우선순위 작업의 길이와는 무관** 해진다.

</details>

<details>
<summary>Q2. <code>(int32_t)(now - next) &gt;= 0</code> 은 왜 wrap 에 안전하죠? 한계는?</summary>

unsigned 뺄셈은 mod 2^32 라서 진짜 거리 Δ 가 [−2^31, 2^31) 안이면 결과를 부호 있게 읽은 값이 Δ 그대로다. wrap 은 mod 가 흡수한다. 한계: 거리 2^31 tick 이상은 앞/뒤를 구분 못 한다 (1 kHz tick 이면 24.8 일). 이식성: C11 6.3.1.3p3 상 범위 밖 값을 signed 로 바꾸는 것은 구현 정의 (UB 아님), GCC/clang 은 mod 2^N 로 정의. 완전 이식형은 `(uint32_t)(now - next) < 0x80000000u`. 꼬리: "`now - next >= 0` 은?" → unsigned 라 **항상 참** (컴파일러가 경고하기도 한다). 꼬리2: "Linux 는?" → `time_after(a, b)` 가 같은 생각의 매크로 (`(long)((b) - (a)) < 0`, include/linux/jiffies.h — 기억 기준, 이 레슨에서 소스 확인 안 함).

</details>

<details>
<summary>Q3. <code>next += period</code> 와 <code>now + period</code> 는 언제 각각 맞나요? 밀린 릴리스는?</summary>

주기 작업(샘플링, 제어)은 `next += period` — 릴리스 격자가 고정되어 장기 드리프트 0 (B3: 10 초에 1000/1000, `now + period` 는 792). `now + period` 가 맞는 경우는 "**최소 간격**" 이 의미일 때: 디바운스, 재시도 백오프, "마지막 수신 후 500 ms 무응답이면 타임아웃" (RTOS-B12 one-shot 타이머 리셋). 밀린 릴리스: catch-up (개수가 의미 — 시계, 적산) 또는 skip (최신 값이 의미 — 센서, 제어). FreeRTOS `xTaskDelayUntil` 은 기준을 한 칸씩만 전진하므로 (tasks.c:2468) 밀리면 연달아 돈다 = catch-up. 꼬리: "catch-up 이 위험한 경우?" → 과부하가 계속되면 백로그가 끝없이 쌓여 영원히 못 따라잡는다. skip 이나 overrun 카운터 + 경보가 필요.

</details>

<details>
<summary>Q4. 협력형 스케줄러에서 최악 지연은 얼마인가요?</summary>

최상위 작업: 릴리스 순간 돌고 있던 다른 작업 **한 번 호출** 이 끝날 때까지 — `≤ max C_other − 1` (정수 tick). 이 레슨: 30 ms TELEM → 상한 29, 측정 25 (위상), 무작위 300 시나리오에서 위반 0, 상한 도달 196. 아래 작업은 거기에 위 작업들의 실행(몰아치기 포함)이 더해진다 — 연습 3 에서 CONTROL 이 24 (21 + SENSOR 3번). 일반식은 비선점 고정 우선순위 응답 시간 분석(blocking term B_i = 낮은 작업 중 최대 C)으로 풀 수 있다. 핵심 문장: "**협력형의 지연 상한은 가장 긴 작업이 정한다. 선점은 그 항을 없앤다.**" 꼬리: "그럼 긴 작업이 꼭 있으면?" → 쪼개거나(상태 기계, DMA), 그 작업만 RTOS 태스크로.

</details>

<details>
<summary>Q5. 긴 작업은 어떻게 쪼개나요? 그 대가는?</summary>

작업을 한 번 호출이 짧은 **재진입 가능한 단계** 로 나누고 진행 상황을 구조체에 저장한다 (state + chunk). 이 레슨: FORMAT 2 + SEND 4×7 → SENSOR 지연 25 → 3, 일의 총량은 300 ms 그대로, 대신 TELEM 프레임이 3 ms 늦게 끝난다. 대가: ① 코드가 뒤집힌다 (콜백/상태 기계) ② 지역 변수가 살아남지 않는다 ③ 조각 길이를 모든 작성자가 지켜야 한다. 이걸 자동화한 것이 protothreads, FreeRTOS 코루틴(`crSTART` = `switch(uxState){ case 0:`, croutine.h:214–216, `__LINE__` 로 case 생성)이고, 아예 **스택을 따로 주고** 레지스터를 저장하면 그게 태스크다 → RTOS-B03 (컨텍스트 스위치). 꼬리: "UART 는 쪼개는 것보다?" → DMA + TC 인터럽트로 CPU 를 아예 안 쓰는 게 정답. 쪼개기는 CPU 계산(압축, 암호, FFT)일 때.

</details>

<details>
<summary>Q6. 협력형이면 공유 데이터 문제가 없나요? ISR 은요? idle 에서 잘 때는?</summary>

작업끼리는 서로를 끊지 않으므로 작업 사이 공유 변수는 락이 필요 없다 (협력형의 큰 장점). 하지만 **ISR 은 여전히 선점** 한다 → ISR 과 작업이 공유하는 데이터는 `volatile` + 임계 구역 (C-M14, PRIMASK 저장/복원). idle 에서 "할 일 확인 → WFI" 사이에 인터럽트가 와서 플래그를 세우면, 확인은 이미 끝났으므로 다음 인터럽트(최대 1 SysTick)까지 잠든다 — lost wakeup. 고전적 해법: `__disable_irq(); if (!work) __WFI(); __enable_irq();` — Cortex-M 의 WFI 는 PRIMASK 로 막힌 인터럽트가 pending 이어도 깨어난다 (ARMv7-M 아키텍처 매뉴얼 기준, 이 레슨에서 실험 안 함). 꼬리: "OS-B05 의 조건 변수 while 루프와 같은 구조?" → 그렇다. check 와 sleep 을 원자적으로.

</details>

<details>
<summary>Q7. 워치독은 어디서 kick 하나요?</summary>

ISR 이 아니라 **메인 루프** 에서, 가능하면 "모든 작업이 최근에 돌았다" (alive 플래그/카운터) 를 확인한 뒤에. 협력형에서 kick 간격의 최대 = 가장 긴 한 번 호출. 연습 4: IWDG 20 ms 에서 30 ms 블로킹 TELEM → 간격 30, **1 초에 리셋 10번**, split → 간격 4, 리셋 0. 실제 사고 패턴: 디버그 `printf` 를 넣었더니 보드가 가끔 리셋된다. 꼬리: "RTOS 에서는?" → 가장 낮은 우선순위 태스크나 전용 감시 태스크가 각 태스크의 체크인을 모아서 kick (굶는 태스크 감지).

</details>

## 9. 추가 연습

### 연습 1 — `now >= next` 가 wrap 에서 하는 일

시계를 `0xFFFFFF00` 에서 시작해 1000 ms, 기본 비용으로 `time_due` 와 `naive_due` (`now >= t`) 를 비교하라. 일찍 실행(릴리스 전 시작) 횟수와 run 수를 세라.

<details>
<summary>답 보기</summary>

```text
ex1 start 0xFFFFFF00, time_due: S/C/T runs 100/20/10 early 0/0/0
ex1 start 0xFFFFFF00, naive   : S/C/T runs 100/5/23 early 4/0/20, C last start = t0+201
```

t0+250 = `0xFFFFFFFA` 에서 SENSOR 가 돌고 `next` 가 wrap 해서 `0x00000004` 가 된다. `now(0xFFFFFFFB) >= 4` → 참 → **일찍** 실행, 다시 14, 24, 34 … `now` 가 0 으로 넘어갈 때까지 4번 몰아침. 그동안 CONTROL(`next = 0xFFFFFFFA`)은 SENSOR 에 밀려 기회가 없었고, `now` 가 wrap 한 뒤에는 `0 >= 0xFFFFFFFA` 가 거짓 → **2^32 ms ≈ 49.7 일 동안 정지** (마지막 시작 t0+201). TELEM 은 반대로 `next` 가 먼저 wrap 해서 20번 일찍 실행 (10 → 23 runs). 한 버그로 "몰아치기" 와 "멈춤" 이 동시에 나온다. 현장에서는 "보드가 49일쯤 돌면 제어가 멈춘다" 로 보인다.

</details>

### 연습 2 — `now + period` 드리프트

TELEM 30 ms 블로킹, 10 초. `next_run = now + period` 로 바꾸고 runs 를 비교하라. 기본 비용(긴 작업 없음)에서는?

<details>
<summary>답 보기</summary>

```text
ex2 10 s long: next+=period S/C/T runs 1000/200/100 | now+period S/C/T runs 792/199/99, S ivl 10..35
ex2 10 s base: now+period S/C/T runs 1000/200/100
```

긴 작업이 SENSOR 를 25 ms 늦출 때마다 격자 전체가 25 ms 밀린다 → 1000 중 208 손실. 몰아치기는 없다 (`ivl min 10`) — 대신 잃은 것은 영원히 안 돌아온다. 기본 비용에서 CONTROL 은 첫 릴리스만 1 ms 늦고 그 뒤는 매번 SENSOR 직후에 정확히 50 간격 → 위상만 1 ms 밀리고 손실 0. 즉 `now + period` 의 피해는 **늦음이 들쭉날쭉할 때** 누적된다.

</details>

### 연습 3 — 지연 상한 "max C_other − 1" 검증

(a) TELEM 위상(offset)을 40 으로 바꿔 CONTROL 지연을 재라. (b) 무작위 300 시나리오: SENSOR 비용 1–3, CONTROL 비용 1–10·주기 20–60·위상 무작위, TELEM 비용 1–40·주기 100–200·위상 무작위, 시작 시각 무작위(wrap 포함), 2000 ms. SENSOR 최대 지연 ≤ max(C_control, C_telem) − 1 인지 확인하라.

<details>
<summary>답 보기</summary>

```text
ex3 telem offset 40: long C lat max 24, S lat max 21 | split C lat max 2, S lat max 3
ex3 random 300: sensor lat max <= maxC_other - 1 violated 0, tight (== bound) 196
```

(a) TELEM 41–71 이 CONTROL 릴리스 50 을 막고, 71 에 밀린 SENSOR 3개(50·60·70)가 먼저 돈 뒤 74 에 시작 → 24 = 21 + 3. Part C 의 "CONTROL 1" 은 위상 운이었다. split 이면 2. (b) 위반 0, 300 중 196 이 상한과 **같다** — 상한이 느슨한 어림이 아니라 실제로 도달하는 값이다. 주의: 2000 ms 창 끝에서 막힌 릴리스는 창 밖에서 시작하므로 runs 가 200 보다 조금 작을 수 있어(최소 196 이상 확인) runs 는 상한 판정에 쓰지 않았다.

</details>

### 연습 4 — 워치독 kick 간격

메인 루프 한 바퀴(디스패치 1회 또는 idle 1 tick)마다 IWDG 를 kick 한다. 타임아웃 20 ms. long 과 split 의 최대 kick 간격과 리셋 횟수는?

<details>
<summary>답 보기</summary>

```text
ex4 IWDG 20 ms: long gap max 30 ms, resets 10 | split gap max 4 ms, resets 0
```

kick 간격의 최대 = 가장 긴 한 번 호출. 30 ms 블로킹 TELEM 은 100 ms 마다 워치독을 넘겨 **1 초에 10번 리셋**. split 은 4 ms. 워치독 타임아웃은 "가장 긴 한 번 호출 + 여유" 보다 커야 하고, 그 값이 곧 협력형의 응답성 상한과 같은 숫자다. STM32 IWDG 는 내부 RC 발진기 LSI(수십 kHz, 칩마다 오차가 크다)로 돌기 때문에 여유를 크게 잡는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_asan && ./pr_asan  # 11/11, 경고 0
```

## 10. 핵심 정리

- 슈퍼루프 + `HAL_Delay`: 한 바퀴 = delay + 작업 시간 → 주기 늘어남(100 → 120 ms), 드리프트 누적, CPU 는 바쁘게 기다림.
- 타임스탬프 협력형 스케줄러 = 작업 테이블(fn, period, next_run) + "첫 due 하나 실행하고 return" (테이블 순서 = 우선순위).
- due 판정은 `(int32_t)(now - next) >= 0` (거리 < 2^31; C11 상 구현 정의 변환, 이식형은 `(now - next) < 0x80000000u`). `now >= next` 는 wrap 에서 몰아치기 + 49.7 일 정지.
- `next += period` → 드리프트 0. `now + period` 는 최소 간격이 의미일 때만. 밀린 릴리스는 catch-up(개수) / skip(최신 값).
- 협력형 지연 상한 = 가장 긴 다른 한 번 호출 − 1 (30 ms printf → 25 측정, 29 상한). 쪼개면 3.
- 쪼개기: 상태 기계 / DMA / 코루틴(`crSTART` = switch + `__LINE__`). 진행 상황은 ctx 에.
- 이 상한이 "작성자 전원의 약속" 이라는 점이 선점(RTOS-B05)의 동기. 워치독 kick 간격도 같은 숫자로 정해진다.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` (cooperative 모드 yield 매크로 68–110, `xTaskDelayUntil` 2414–2468, `xTaskIncrementTick` 4831~ 의 선점 분기 4929–4957), `croutine.c` (`vCoRoutineSchedule` 299–333), `include/croutine.h` (`crSTART` 214–216, `crSET_STATE0/1` 258–263, `crDELAY` 313), `include/task.h:227` (`taskYIELD`): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS — Co-routines, `configUSE_PREEMPTION`, `xTaskDelayUntil` 문서: <https://www.freertos.org> → Documentation
- Michael J. Pont, *Patterns for Time-Triggered Embedded Systems* (2001) — co-operative scheduler, task 분할, 워치독 패턴의 고전
- Adam Dunkels, *Protothreads* — 스택 없는 경량 스레드 (`switch` + `__LINE__`): <https://dunkels.com/adam/pt/>
- ISO/IEC 9899:2011 (C11) 6.3.1.3 Signed and unsigned integers — 범위 밖 signed 변환은 구현 정의
- GCC Manual — Integers implementation-defined behavior ("reduced modulo 2^N"): <https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html>
- STM32Cube HAL — `HAL_GetTick`, `HAL_Delay`, `HAL_IWDG_Refresh` (stm32xxxx_hal.c / _hal_iwdg.c)
- Zephyr — Threads (cooperative vs preemptive priority), `k_uptime_get_32`, `K_TIMEOUT_ABS_MS`: <https://docs.zephyrproject.org/latest/kernel/services/threads/index.html>
- CMSIS-RTOS2 — `osDelayUntil`, `osKernelGetTickCount`, `osThreadYield`: <https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html>
- 관련 레슨: C-M14 (ISR↔메인 공유 데이터, PRIMASK), OS-B05 (조건 변수와 lost wakeup), RTOS-B02 (TCB와 상태), RTOS-B03 (컨텍스트 스위치 — 스택 있는 "쪼개기"), RTOS-B05 (선점, SIGALRM tick), RTOS-B06 (Delay vs DelayUntil, tick wrap), RTOS-B12 (소프트웨어 타이머 — 콜백도 run-to-completion), RTOS-M03 (우선순위 역전)
