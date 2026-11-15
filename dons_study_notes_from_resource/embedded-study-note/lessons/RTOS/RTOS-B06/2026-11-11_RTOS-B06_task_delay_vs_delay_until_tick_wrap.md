---
id: RTOS-B06
time: 60분
updated: 2026-11-11
---
# RTOS-B06 태스크 지연: Delay vs DelayUntil, tick wrap 안전 비교

## 학습 목표

- 상대 지연 `delay(ticks)` 와 절대 지연 `delay_until(&last, period)` 의 차이를 식으로 말하고, 작업 시간이 들쭉날쭉할 때 **상대 지연만 주기가 밀리는(drift)** 이유를 측정값으로 보인다.
- 32비트 tick 카운터의 wrap(0xFFFFFFFF → 0)에서도 맞는 비교 `(uint32_t)(now - wake) < 2^31` 을 만들고, 왜 맞는지(모듈러 산술)와 어디까지 맞는지(거리 < 2^31)를 설명한다.
- wake tick 순으로 정렬된 **delayed list** 를 구현하고, FreeRTOS 의 다른 방법(현재/overflow 두 리스트를 wrap 때 바꾸기)과 비교한다.
- `xTaskDelayUntil` 이 `pdFALSE` 를 돌려주는 조건(마감이 이미 지났거나 바로 지금)과 `vTaskDelay(0)` = yield 를 소스로 확인하고, overrun 때 **따라잡기(catch-up) vs 건너뛰기(skip)** 정책을 고른다.

## 1. 문제

"vTaskDelay 와 vTaskDelayUntil 의 차이는? tick 카운터가 넘치면(overflow) 어떻게 되죠?" — RTOS 면접 단골이고, 실제 펌웨어 버그의 단골이기도 하다. 100 Hz 로 IMU 를 읽는 태스크가 `vTaskDelay(10)` 을 쓰면 샘플 간격이 조금씩 늘어나 필터·적분이 틀어지고, 32비트 ms 카운터를 `now >= deadline` 으로 비교한 코드는 49.7일(2^32 ms)마다 한 번 타임아웃이 즉시 터지거나 영원히 안 터진다.

두 지연의 차이는 **기준점** 하나다.

- `delay(n)` (상대, `vTaskDelay`): "**지금부터** n tick 뒤". 지금 = 작업이 끝난 시각이라서, 작업 시간과 선점(preemption)당한 시간이 매 주기 더해진다.
- `delay_until(&last, P)` (절대, `xTaskDelayUntil`): "**지난번 깨어날 시각 + P**". 작업이 얼마나 걸렸든 깨어날 시각은 격자(grid) 0, P, 2P, ... 위에 있다.

```svg
<svg viewBox="0 0 740 236" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">상대 지연 vs 절대 지연 — 작업 3 · 4 · 5 tick, 주기 P = 10 tick</text> <text x="106" y="40" fill="currentColor">0</text> <text x="252" y="40" fill="currentColor">10</text> <text x="402" y="40" fill="currentColor">20</text> <text x="552" y="40" fill="currentColor">30</text> <text x="702" y="40" fill="currentColor">40</text>
  <path d="M110 46 L110 170 M260 46 L260 170 M410 46 L410 170 M560 46 L560 170 M710 46 L710 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <text x="10" y="76" fill="currentColor" font-family="monospace">delay(10)</text>
  <rect x="110" y="60" width="45" height="22" class="f-acc" stroke="currentColor"/> <rect x="290" y="60" width="60" height="22" class="f-acc" stroke="currentColor"/> <rect x="485" y="60" width="75" height="22" class="f-acc" stroke="currentColor"/> <rect x="695" y="60" width="15" height="22" class="f-warn" stroke="currentColor"/> <path d="M148 88 Q219 104 290 88 M343 88 Q414 104 485 88 M553 88 Q624 104 695 88" stroke="currentColor" fill="none"/>
  <text x="196" y="112" fill="currentColor" font-size="12">호출 + 10</text> <text x="391" y="112" fill="currentColor" font-size="12">호출 + 10</text> <text x="601" y="112" fill="currentColor" font-size="12">호출 + 10</text> <text x="10" y="141" fill="currentColor" font-family="monospace">until(10)</text>
  <rect x="110" y="125" width="45" height="22" class="f-acc" stroke="currentColor"/> <rect x="260" y="125" width="60" height="22" class="f-acc" stroke="currentColor"/> <rect x="410" y="125" width="75" height="22" class="f-acc" stroke="currentColor"/> <rect x="560" y="125" width="45" height="22" class="f-acc" stroke="currentColor"/> <path d="M110 153 Q185 169 260 153 M260 153 Q335 169 410 153 M410 153 Q485 169 560 153" stroke="currentColor" fill="none"/>
  <text x="152" y="182" fill="currentColor" font-size="12">last + 10</text> <text x="302" y="182" fill="currentColor" font-size="12">last + 10</text> <text x="452" y="182" fill="currentColor" font-size="12">last + 10</text> <text x="10" y="208" fill="currentColor">delay: 릴리스 0 → 12 → 25 → 39 — 주기 = 10 + (작업 − 1), 밀린 만큼 되돌아오지 않는다.</text>
  <text x="10" y="228" fill="currentColor">delay_until: 릴리스 0 → 10 → 20 → 30 — 깨어날 시각이 작업 길이와 무관하다.</text>
</svg>
```

(작업 c tick 은 tick r … r+c−1 동안 돌고 마지막 tick 에서 지연을 부른다 → `delay(10)` 이면 다음 릴리스 = r + c − 1 + 10.)

**미니 커널 (하네스 제공).** RTOS-B09 처럼 OS 스레드 1개가 tick 을 하나씩 돌리는 결정적 시뮬레이터다. 이번엔 tick 이 **`uint32_t`** 이고 일부러 **0xFFFFFFE0~F0 에서 시작**해서 실행 도중에 0 으로 넘어간다. 하네스는 넘치지 않는 64비트 그림자 시계 `g_tick64` 로 "언제 깨어나야 하는가" 를 따로 계산해 학생 코드(32비트)의 결과와 비교한다(오라클).

| 규칙 | 이 모델 | 실제 RTOS 와 다른 점 |
|---|---|---|
| tick 하나 | ① `dlist_expire()` (학생) → ② `sched_pick_next()`: READY 중 최고 우선순위, 같으면 먼저 READY 된 순 → 그 태스크 `step()` 1번 → ③ `g_tick++` | 실제는 SysTick ISR 이 `xTaskIncrementTick` 을 부르고 필요하면 PendSV 로 전환 |
| 태스크 | 상태 기계 `step()` + `pc`. 한 step = CPU 1 tick | 실제는 C 루프가 tick 중간에도 돈다 (Part C 에서 차이 확인) |
| 상태 | `TASK_READY/RUNNING/BLOCKED/SUSPENDED` (RTOS-B02 어휘), delayed list 링크 `next/prev` | FreeRTOS 는 `xStateListItem` 하나를 ready ↔ delayed 리스트로 옮긴다 |
| 타임라인 | `R` 실행 · `r` READY · `B` delayed list 에서 잠 · `.` 끝. 머리줄 = tick 의 **하위 16진 한 자리**, `^` = wrap 지점 | RTOS-B09/M03 형식 |

### Part A — 지연의 부품 다섯 개

- `tick_reached(now, wake)` — now 가 wake 에 닿았거나 지났나. wrap 과 무관하게, 실제 거리가 2^31 미만이면 맞아야 한다.
- `dlist_insert(t)` — `t->wake_tick` 이 가까운 순으로 이중 연결 리스트에 넣기. 같은 wake 는 먼저 온 순(FIFO).
- `dlist_expire()` — tick 마다 맨 앞부터 깰 태스크를 빼서 `k_make_ready()`. 첫 미달에서 멈춘다.
- `task_delay(self, ticks)` — `ticks == 0` 이면 블록하지 않고 yield (같은 우선순위 맨 뒤로).
- `task_delay_until(self, &last, period)` — 깨어날 시각 `last + period`, `*last` 는 **항상** 그 값으로 갱신. 이미 지났거나 바로 지금이면 블록하지 않고 `false`.

### Part B — 드리프트 측정과 wrap 테스트

1. **B1 드리프트**: PER(p2, P = 10) 의 작업 시간 1–4 tick (고정 seed), COMM(p3) 이 7 tick 마다 2 tick 끼어든다. `delay(10)` 판과 `delay_until(&last, 10)` 판을 릴리스 51번(주기 50개) 돌려 릴리스 tick − 10k 를 표와 그래프로.
2. **B2 wrap 순서**: 0xFFFFFFF0 부터 A=delay(20), B=delay(5), C=delay(30), D=delay(3), E=delay_until(&start, 25), F=delay(2^31−1) 을 한 tick 씩 걸었을 때 깨어나는 순서와 정확한 tick.
3. **B3 무작위 300**: 시작 tick 절반은 wrap 직전, 태스크 2–5개 + 아주 긴 잠꾸러기, 64비트 오라클이 모든 깨어남이 "정확히 그 tick" 인지 검사.

### Part C — IMU 100 Hz 샘플링의 overrun 정책

STM32 + IMU(예: SPI 가속도계) 를 10 ms 마다 읽는 태스크(p3)와 BLE 텔레메트리(p1, `delay(20)`). 네 번째 샘플(k=3)에서 flash 에 로그를 쓰느라 25 tick 이 걸린다. `periodic_wait(self, &pt)` 를 두 정책으로 구현한다.

- `PT_CATCH_UP` — `delay_until` 그대로. 놓친 주기는 `false` 를 받으며 연달아 실행해 따라잡는다 (FreeRTOS `xTaskDelayUntil` 의 기본 동작).
- `PT_SKIP` — now 보다 엄격히 과거인 격자점은 버리고(`skipped` 에 셈) 다음 격자점까지 잔다. 위상(phase)은 그대로.

## 2. Problem (English)

> **Delayed list with wrap-safe tick comparison; relative vs absolute delays.**
>
> ```c
> bool tick_reached(uint32_t now, uint32_t wake);          // true if now is at/after wake (|now-wake| < 2^31)
> void task_delay(tcb_t *self, uint32_t ticks);            // block for ticks; 0 = yield
> bool task_delay_until(tcb_t *self, uint32_t *last, uint32_t period);
>      // wake at *last + period, always update *last; return false (no block) if that time is now or past
> bool periodic_wait(tcb_t *self, periodic_t *pt);         // CATCH_UP or SKIP missed periods
> ```
>
> The kernel tick is a 32-bit counter that starts at `0xFFFFFFF0` and wraps during the test. Keep blocked tasks in a list sorted by wake tick (FIFO for equal ticks); the tick handler must wake exactly the due tasks, in order, never early or late.
>
> 1. A periodic task with period 10 and job time 1–4 ticks, preempted by a higher-priority task: measure release drift over 50 periods with `task_delay(10)` vs `task_delay_until(&last, 10)`.
> 2. Show ordering across the wrap: delays of 20, 5, 30, 3 issued at 0xFFFFFFF0..F3 must wake as B, D, A, ..., C.
> 3. One job overruns 2.5 periods. Implement catch-up and skip policies; which keeps the sample grid, which loses samples?
>
> Follow-ups: How does FreeRTOS handle tick overflow (two delayed lists)? What does `xTaskDelayUntil` return when the deadline has passed? What does `vTaskDelay(0)` do? Why not compute `P - elapsed` yourself? What is the largest safe delay with a wrap-safe compare on a 16-bit tick?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초 안에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:330  tick_reached(5, 5) && tick_reached(6, 5) && !tick_reached(4, 5)
A1 tick_reached: 200000 random pairs (|d| < 2^31): wrong=99748   naive (now >= wake): wrong=50003
A2 delayed list @0xfffffff0:
A3 delay(0) order: XXXYYY
COMM : R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R R
...
3/36 passed
```

스텁 `task_delay` 는 아무것도 안 하므로 COMM 이 한 번도 잠들지 않고 CPU 를 독점한다 — "지연 = CPU 를 내놓는 것" 이라는 걸 바로 보여 준다. 하네스 설계:

- **무한 루프 없음.** `k_run(limit, until)` 은 tick 상한에서 멈춘다.
- **오라클은 64비트.** 태스크는 래퍼 `prog_delay / prog_delay_until / prog_periodic_wait` 를 부르고, 래퍼가 64비트 시계로 기대 wake 시각·반환값·`*last` 를 먼저 계산한다. `kernel_tick` 은 매 tick "깨어난 태스크는 정확히 기대 tick 에 깼나, 아직 자는 태스크가 마감을 넘기지 않았나" 를 본다 (`g_wake_err`, `g_ret_err`).
- **리스트 불변식** (`dlist_check`, 매 tick): `prev` 링크 일치, 리스트 안은 모두 BLOCKED, 남은 거리 `wake − now` 가 1 이상 2^31 미만이고 앞에서 뒤로 커진다, 리스트 길이 == BLOCKED 수.
- `(void)k_make_ready; (void)block_until;` 줄은 starter 에서 아직 안 부르는 서비스의 unused 경고를 막는다.

## 4. 힌트

<details>
<summary>힌트 1 — 뺄셈은 wrap 해도 "거리" 를 지킨다</summary>

`uint32_t` 뺄셈은 mod 2^32 이다. 0x00000004 − 0xFFFFFFF0 = 0x14 = 20. 진짜 거리 D 가 −2^31 ≤ D < 2^31 이면, `now − wake` 를 mod 2^32 로 본 값이 2^31 보다 작다 ⇔ D ≥ 0. 그래서 `(uint32_t)(now - wake) < 0x80000000u`. `now >= wake` 는 날값(raw) 비교라서 wrap 을 사이에 두면 뒤집힌다.

</details>

<details>
<summary>힌트 2 — 정렬 삽입은 "x 이후면 지나간다"</summary>

`while (x && tick_reached(t->wake_tick, x->wake_tick))` — 내 wake 가 x 의 wake 에 닿았거나 지났으면(같음 포함) 지나간다 → 같은 tick 은 뒤에 붙어 FIFO. 리스트 안 원소는 모두 [now, now + 2^31) 안에 있으므로 서로 비교해도 안전하다. 같은 생각을 `wake − now` (남은 거리, 부호 없는 값) 비교로 써도 된다 — practice.c 의 `before_safe`.

</details>

<details>
<summary>힌트 3 — expire 는 맨 앞만 본다</summary>

정렬돼 있으니 맨 앞이 아직이면 뒤도 아직이다. `while (g_dhead && tick_reached(g_tick, g_dhead->wake_tick))` 로 머리를 떼고 `prev/next` 를 정리한 뒤 `k_make_ready`. FreeRTOS 는 한 발 더 나가 `xNextTaskUnblockTime` 을 캐시해서 대부분의 tick 에서 리스트를 아예 안 본다 (tasks.c:4871).

</details>

<details>
<summary>힌트 4 — delay_until 의 조건은 "경과 &lt; 주기"</summary>

`elapsed = now − *last` (wrap 안전), `wake = *last + period`, `*last = wake` 는 무조건. `elapsed < period` 이면 블록 + true, 아니면 (마감이 지났거나 **바로 지금**) false. FreeRTOS 의 4갈래 if 문(tasks.c:2436–2465)은 이 한 줄과 같다 — 5. 풀이에서 확인.

</details>

<details>
<summary>힌트 5 — SKIP: 과거 격자점 개수 = (elapsed − 1) / P</summary>

격자점 last + jP (j ≥ 1) 중 now 보다 엄격히 앞인 것은 jP &lt; elapsed, 즉 j ≤ (elapsed − 1)/P. `elapsed > P` 일 때만 그만큼 `last` 를 당기고 `skipped += m`, 그다음은 그냥 `task_delay_until`.

</details>

## 5. 풀이

### Part A — wrap 안전 비교, delayed list, 두 가지 지연

```c
static bool tick_reached(uint32_t now, uint32_t wake) { return (uint32_t)(now - wake) < HALF_RANGE; }
static bool task_delay_until(tcb_t *self, uint32_t *last, uint32_t period) {
    uint32_t elapsed = g_tick - *last, wake = *last + period;
    *last = wake;
    if (elapsed < period) { block_until(self, wake); return true; }
    k_make_ready(self);                     /* 안 막혀도 yield (FreeRTOS tasks.c:2483-2490) */
    return false;
}
```

**왜 맞나 (모듈러 산술).** 64비트 진짜 시각을 N, W 라 하고 D = N − W 라 하자. 32비트 값은 n = N mod 2^32, w = W mod 2^32 이고 `(uint32_t)(n − w)` = D mod 2^32. D ∈ [0, 2^31) 이면 그 값은 D 자체(< 2^31) → true. D ∈ [−2^31, 0) 이면 2^32 + D ∈ [2^31, 2^32) → false. 그러니 **|D| 가 2^31 미만인 한 wrap 위치와 상관없이 정확**하다. 대가는 표현 범위: 지연은 2^31 − 1 tick 까지 (1 kHz 면 약 24.8 일). `task_delay` 가 그 이상을 잘라 두는 이유다.

| 경계 (now, wake) | 결과 | 설명 |
|---|---|---|
| (5, 5) / (6, 5) / (4, 5) | 닿음 / 닿음 / 아직 | 같으면 닿은 것 |
| (0x00000000, 0xFFFFFFFF) | 닿음 | wrap 직후: 1 tick 지남 |
| (0xFFFFFFF0, 0x00000004) | **아직** | `now >= wake` 는 여기서 true → 20 tick 짜리 지연이 즉시 끝나는 버그 |
| (0x7FFFFFFF, 0) / (0x80000000, 0) | 닿음 / 아직 | 정확히 2^31 은 어느 쪽으로도 "닿음" 이 아니다 (모호 구간) |

`(int32_t)(now - wake) >= 0` 으로 쓰는 코드도 흔하다 (Linux 의 `time_after()` 가 같은 생각). 다만 2^31 이상인 `uint32_t` 를 `int32_t` 로 바꾸는 결과는 C11 6.3.1.3p3 에서 **구현 정의**라서, 여기서는 부호 없는 비교로 썼다 (gcc/clang 은 2의 보수로 정의해 두므로 실무에선 둘 다 동작).

실제 출력 (A1): `200000 random pairs (|d| < 2^31): wrong=0   naive (now >= wake): wrong=50003`. 날값 비교가 약 25% 틀린 것은 계산과 맞다: 시작점이 균등하면 [now, wake] 구간이 wrap 지점을 품을 확률은 |d|/2^32 이고, |d| 가 [0, 2^31) 균등이면 평균 2^30/2^32 = 1/4.

`demo_naive_wrap.c` 로 같은 버그를 리스트 수준에서 본다:

<!-- code: demo_naive_wrap.c -->

```text
delay(20) at 0xfffffff0 -> wake 0x00000004
  naive now>=wake at the same tick : TRUE  (wakes after 0 ticks!)
  safe  (now-wake) < 2^31          : false (keeps sleeping)
raw  sorted list : 00000004 00000010 fffffff5 fffffff6   -> expire at 0xfffffff1 pops 2 task(s)
safe sorted list : fffffff5 fffffff6 00000004 00000010   -> expire at 0xfffffff1 pops 0 task(s)
half range: now - wake = 0x80000000 -> safe(now, wake) = 0 and safe(wake, now) = 0 (both 'not reached')
```

**delayed list 를 wrap 에 강하게 만드는 두 방법.** 이 레슨은 ① 리스트 하나 + wrap 안전 비교. FreeRTOS 는 ② **리스트 두 개** 를 쓴다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">0xFFFFFFF0 근처에서 건 지연들 — 정렬 방법 세 가지</text> <path d="M40 70 L700 70" stroke="currentColor" stroke-width="1.5"/>
  <path d="M50 64 L50 76 M194 64 L194 76 M338 58 L338 82 M482 64 L482 76 M626 64 L626 76" stroke="currentColor"/> <text x="26" y="96" fill="currentColor" font-family="monospace" font-size="12">..fff0</text> <text x="170" y="96" fill="currentColor" font-family="monospace" font-size="12">..fff8</text> <text x="306" y="96" fill="currentColor" font-family="monospace" font-size="12">00000000</text> <text x="458" y="96" fill="currentColor" font-family="monospace" font-size="12">00000008</text> <text x="602" y="96" fill="currentColor" font-family="monospace" font-size="12">00000010</text>
  <text x="344" y="52" fill="currentColor" font-size="12">wrap</text> <text x="40" y="48" fill="currentColor" font-size="12">now</text> <path d="M50 52 L50 62" stroke="currentColor" stroke-width="2"/> <circle cx="158" cy="70" r="6" class="f-warn" stroke="currentColor"/> <text x="140" y="52" fill="currentColor">B,D f6</text>
  <circle cx="410" cy="70" r="6" class="f-acc" stroke="currentColor"/> <text x="402" y="52" fill="currentColor">A 04</text> <circle cx="500" cy="70" r="6" class="f-acc" stroke="currentColor"/> <text x="492" y="52" fill="currentColor">E 09</text>
  <circle cx="626" cy="70" r="6" class="f-acc" stroke="currentColor"/> <text x="618" y="52" fill="currentColor">C 10</text> <text x="10" y="126" fill="currentColor" font-family="monospace">날값 정렬     : A 04 · E 09 · C 10 · B f6 · D f6</text> <text x="470" y="126" fill="currentColor">✗ A 가 맨 앞, now&gt;=wake 로 즉시 깸</text>
  <text x="10" y="150" fill="currentColor" font-family="monospace">wrap 안전 정렬: B f6 · D f6 · A 04 · E 09 · C 10</text> <text x="470" y="150" fill="currentColor">✓ 이 레슨 (wake − now 순)</text> <rect x="10" y="168" width="300" height="72" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="188" fill="currentColor" font-family="monospace" font-size="12">pxDelayedTaskList</text> <text x="20" y="208" fill="currentColor" font-size="12">now + ticks 가 안 넘친 것 (raw &lt; 로 정렬)</text>
  <text x="20" y="230" fill="currentColor" font-family="monospace">B f6 → D f6</text> <rect x="430" y="168" width="300" height="72" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="440" y="188" fill="currentColor" font-family="monospace" font-size="12">pxOverflowDelayedTaskList</text> <text x="440" y="208" fill="currentColor" font-size="12">wake &lt; now (넘쳤다) — 다음 바퀴용</text>
  <text x="440" y="230" fill="currentColor" font-family="monospace">A 04 → E 09 → C 10</text> <path d="M316 196 L424 196 M414 190 L424 196 L414 202 M424 214 L316 214 M326 208 L316 214 L326 220" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="330" y="186" fill="currentColor" font-size="12">tick → 0:</text> <text x="330" y="232" fill="currentColor" font-size="12">포인터 교환</text> <text x="10" y="266" fill="currentColor">FreeRTOS: 넣을 때 overflow 여부만 보고 리스트를 고른다 → 두 리스트 안은 평범한 &lt; 로 충분.</text>
  <text x="10" y="288" fill="currentColor">tick 이 0 이 되는 순간 taskSWITCH_DELAYED_LISTS(); 그때 current 는 비어 있어야 한다 (configASSERT).</text>
</svg>
```

FreeRTOS V11.3.2 소스 (직접 확인):

- 넣기 — `prvAddCurrentTaskToDelayedList` (tasks.c:8685): `xTimeToWake = xConstTickCount + xTicksToWait;` (8729), `if( xTimeToWake < xConstTickCount )` 이면 넘친 것 → `vListInsert( pxOverflowDelayedList, ... )`, 아니면 `pxDelayedList` (8734–8745). 정렬 삽입 `vListInsert` 는 `<=` 동안 지나가므로 같은 값은 뒤에 = FIFO (list.c:192).
- 바꾸기 — `xTaskIncrementTick`: `xTickCount = xConstTickCount; if( xConstTickCount == ( TickType_t ) 0U ) { taskSWITCH_DELAYED_LISTS(); }` (tasks.c:4856–4861). 매크로는 `configASSERT( listLIST_IS_EMPTY( pxDelayedTaskList ) )` 후 두 포인터를 맞바꾸고 `xNumOfOverflows` 를 올린다 (tasks.c:265–278).
- 깨우기 — `if( xConstTickCount >= xNextTaskUnblockTime )` 일 때만 리스트 머리를 본다 (tasks.c:4871–4904). 평범한 `>=` 인데 맞는 이유: current 리스트에는 "이번 바퀴" 값만 있기 때문이다.

| | ① 리스트 1개 + wrap 안전 비교 (이 레슨) | ② FreeRTOS 두 리스트 |
|---|---|---|
| 비교 | `(uint32_t)(a − b) < 2^31` | 평범한 `<`, `>=` |
| 최대 지연 | 2^31 − 1 tick | 거의 2^32 − 1 tick (연습 3: 16비트에서 확인) |
| wrap 때 할 일 | 없음 | 포인터 교환 1번 |
| 다른 곳 | 타임아웃 계산도 같은 비교 | `xTaskCheckForTimeOut` 은 `xNumOfOverflows` 로 "한 바퀴 돌았나" 까지 본다 (tasks.c:5731) |

연습 1 에서 ②를 직접 짜서 ①과 200 시나리오 × 9000 tick 동안 깨어나는 (tick, id) 순서가 완전히 같음을 확인한다.

**A2 실제 출력** — 0xFFFFFFF0 에서 A=20, B=5, C=2^31−1, D=5, E=16:

```text
A2 delayed list @0xfffffff0: B(fffffff5) D(fffffff5) E(00000000) A(00000004) C(7fffffef)
```

B, D 는 같은 wake → 넣은 순. E 는 정확히 0 에 깬다. C 는 최대 거리라도 맨 뒤에 바르게 선다.

**`delay(0)` = yield.** FreeRTOS `vTaskDelay`: `/* A delay time of zero just forces a reschedule. */ if( xTicksToDelay > ( TickType_t ) 0U )` 일 때만 delayed list 에 넣고, 아니면 바로 `taskYIELD_WITHIN_API()` (tasks.c:2512–2541). yield 는 **같은 우선순위** 에게만 양보한다 — 낮은 태스크는 여전히 못 돈다. A3 출력 `XYXYXY` (스텁은 `XXXYYY`).

**`delay_until` 의 조건 — FreeRTOS 4갈래 = "경과 &lt; 주기".** tasks.c:2434–2465:

| 경우 (prev = `*pxPreviousWakeTime`) | FreeRTOS 가 블록하는 조건 | elapsed = now − prev (mod 2^32) 로 쓰면 |
|---|---|---|
| now ≥ prev, wake 안 넘침 | `wake > now` | P > elapsed |
| now ≥ prev, wake 넘침 | 항상 (`wake < prev`) | now ≤ 2^32−1 &lt; prev+P 이므로 elapsed &lt; P |
| now &lt; prev (tick 이 넘침), wake 넘침 | `wake > now` | P > elapsed |
| now &lt; prev, wake 안 넘침 | 안 함 | elapsed ≥ P |

실제 경과가 2^32 미만이면 네 칸 모두 `elapsed < period` 와 같다. 그리고 `*pxPreviousWakeTime = xTimeToWake;` 는 if 밖이라 블록하든 안 하든 갱신된다 (2468) → 놓친 주기를 "따라잡는" 이유. 반환 `xShouldDelay` (2498) 가 `pdFALSE` 면 마감이 **이미 지났거나 바로 지금**(`wake > now` 가 거짓)이다. `vTaskDelayUntil()` 은 이 반환값을 버리는 옛 매크로다 (task.h:1006–1009). A4 테스트 표:

| last, P | now | 반환 | 새 last | 의미 |
|---|---|---|---|---|
| 100, 10 | 105 | true | 110 | 평범 |
| 110, 10 | 125 | false | 120 | 마감 지남 (catch-up 대상) |
| 120, 10 | 130 | false | 130 | 마감 == now → 블록 안 함 |
| 0xFFFFFFFA, 10 | 0xFFFFFFFE / 3 / 5 | true / true / false | 4 | wake 만 넘침 / 둘 다 넘침 / 이미 지남 |

### Part B — 드리프트 측정 (실제 출력)

```text
B1 timeline — PER uses delay(10)
tick : 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 8 9 a b c d e f
wrap :                                 ^
COMM : R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B
PER  : r r R R R B B B B B B B B B r r R R R B B B B B B B B B r r R R R R B B B B B B B B B r R R R R
B1 timeline — PER uses delay_until(&last, 10)
COMM : R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B B R R B B B B
PER  : r r R R R B B B B B R R R B B B B B B B R r r R R R B B B B R R R R B B B B B B R R B B B B B B
```

```svg
<svg viewBox="0 0 740 258" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B1 실제 타임라인 — 첫 48 tick (solution.c 출력, 머리줄 = tick 하위 16진)</text>
  <text x="71.0" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="125.0" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="179.0" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="233.0" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text> <text x="287.0" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="341.0" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="395.0" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="449.0" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text> <text x="503.0" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="557.0" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="611.0" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="665.0" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text>
  <text x="10" y="54" fill="currentColor" font-weight="bold">delay(10)</text>
  <text x="10" y="71" fill="currentColor" font-family="monospace">COMM</text> <rect x="70.0" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="97.0" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="164.5" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="191.5" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="259.0" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="286.0" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="353.5" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="380.5" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="448.0" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="542.5" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="569.5" y="58" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="637.0" y="58" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="664.0" y="58" width="54.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="95" fill="currentColor" font-family="monospace">PER</text> <rect x="70.0" y="82" width="27.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="97.0" y="82" width="40.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="137.5" y="82" width="121.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="259.0" y="82" width="27.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="286.0" y="82" width="40.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="326.5" y="82" width="121.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="448.0" y="82" width="27.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="82" width="54.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="529.0" y="82" width="121.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="650.5" y="82" width="13.5" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="664.0" y="82" width="54.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="118" fill="currentColor" font-weight="bold">delay_until(&amp;last, 10)</text>
  <text x="10" y="135" fill="currentColor" font-family="monospace">COMM</text> <rect x="70.0" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="97.0" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="164.5" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="191.5" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="259.0" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="286.0" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="353.5" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="380.5" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="448.0" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="542.5" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="569.5" y="122" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="637.0" y="122" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="664.0" y="122" width="54.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="159" fill="currentColor" font-family="monospace">PER</text> <rect x="70.0" y="146" width="27.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="97.0" y="146" width="40.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="137.5" y="146" width="67.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="205.0" y="146" width="40.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="245.5" y="146" width="94.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="340.0" y="146" width="13.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="353.5" y="146" width="27.0" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="380.5" y="146" width="40.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="421.0" y="146" width="54.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="475.0" y="146" width="54.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="529.0" y="146" width="81.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="610.0" y="146" width="27.0" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="637.0" y="146" width="81.0" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <path d="M70.0 42 L70.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M205.0 42 L205.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M340.0 42 L340.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M475.0 42 L475.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M610.0 42 L610.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/>
  <path d="M286.0 36 L286.0 170" stroke="currentColor" stroke-width="2"/> <text x="290.0" y="184" fill="currentColor">wrap: 0xFFFFFFFF → 0</text>
  <rect x="10" y="196" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="207" fill="currentColor">R 실행</text> <rect x="96" y="196" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="116" y="207" fill="currentColor">r READY(CPU 대기)</text> <rect x="281" y="196" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="301" y="207" fill="currentColor">B delayed list 에서 잠</text> <path d="M502 202 L522 202" stroke="currentColor" stroke-dasharray="2 3"/> <text x="528" y="207" fill="currentColor">이상적 격자 (10 tick)</text>
  <text x="10" y="228" fill="currentColor">delay: PER 시작이 2 → 16 → 30 → 44 로 격자(점선)에서 점점 멀어진다.</text>
  <text x="10" y="244" fill="currentColor">delay_until: 10 · 20 · 30 · 40 격자에 붙어 있다 (COMM 과 겹칠 때만 1–2 tick 늦음).</text>
</svg>
```

```text
  k  cost  ideal | delay: rel  drift | delay_until: rel  late
  0     3      0 |          2      2 |                2     2
  1     3     10 |         16      6 |               10     0
  2     4     20 |         30     10 |               20     0
  5     1     50 |         69     19 |               51     1
 10     2    100 |        128     28 |              100     0
 20     4    200 |        261     61 |              200     0
 30     4    300 |        394     94 |              300     0
 40     2    400 |        530    130 |              401     1
 50     2    500 |        660    160 |              500     0
B1 summary: delay: 50 periods took 658 ticks (mean 13.16), drift 160; sum(cost-1)=92, min period 10
           delay_until: 50 periods took 498 ticks (mean 9.96), max late 2, false returns 0; end tick 0x00000286 / 0x000001e6
```

```svg
<svg viewBox="0 0 740 312" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part B1 드리프트 — 릴리스 tick − 10·k (실측, solution.c 의 drift/late 줄)</text>
  <path d="M60 40 L60 240 L720 240" stroke="currentColor" fill="none"/> <path d="M56 240.0 L60 240.0" stroke="currentColor"/> <text x="52" y="244.0" fill="currentColor" text-anchor="end">0</text> <path d="M56 190.0 L60 190.0" stroke="currentColor"/> <text x="52" y="194.0" fill="currentColor" text-anchor="end">40</text> <path d="M56 140.0 L60 140.0" stroke="currentColor"/> <text x="52" y="144.0" fill="currentColor" text-anchor="end">80</text> <path d="M56 90.0 L60 90.0" stroke="currentColor"/> <text x="52" y="94.0" fill="currentColor" text-anchor="end">120</text> <path d="M56 40.0 L60 40.0" stroke="currentColor"/> <text x="52" y="44.0" fill="currentColor" text-anchor="end">160</text> <path d="M60.0 240 L60.0 244" stroke="currentColor"/> <text x="60.0" y="258" fill="currentColor" text-anchor="middle">0</text> <path d="M192.0 240 L192.0 244" stroke="currentColor"/> <text x="192.0" y="258" fill="currentColor" text-anchor="middle">10</text> <path d="M324.0 240 L324.0 244" stroke="currentColor"/> <text x="324.0" y="258" fill="currentColor" text-anchor="middle">20</text> <path d="M456.0 240 L456.0 244" stroke="currentColor"/> <text x="456.0" y="258" fill="currentColor" text-anchor="middle">30</text> <path d="M588.0 240 L588.0 244" stroke="currentColor"/> <text x="588.0" y="258" fill="currentColor" text-anchor="middle">40</text> <path d="M720.0 240 L720.0 244" stroke="currentColor"/> <text x="720.0" y="258" fill="currentColor" text-anchor="middle">50</text>
  <text x="390.0" y="274" fill="currentColor" text-anchor="middle">k (주기 번호, P = 10 tick)</text> <text x="14" y="32" fill="currentColor">tick</text>
  <polyline points="60.0,237.5 73.2,232.5 86.4,227.5 99.6,222.5 112.8,217.5 126.0,216.2 139.2,216.2 152.4,211.2 165.6,210.0 178.8,205.0 192.0,205.0 205.2,203.8 218.4,195.0 231.6,190.0 244.8,188.8 258.0,183.8 271.2,175.0 284.4,170.0 297.6,170.0 310.8,165.0 324.0,163.8 337.2,158.8 350.4,153.8 363.6,148.8 376.8,143.8 390.0,142.5 403.2,133.8 416.4,133.8 429.6,132.5 442.8,131.2 456.0,122.5 469.2,117.5 482.4,116.2 495.6,107.5 508.8,102.5 522.0,101.2 535.2,92.5 548.4,87.5 561.6,86.2 574.8,81.2 588.0,77.5 601.2,76.2 614.4,71.2 627.6,71.2 640.8,70.0 654.0,65.0 667.2,63.8 680.4,55.0 693.6,55.0 706.8,48.8 720.0,40.0" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <circle cx="60.0" cy="237.5" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="126.0" cy="216.2" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="192.0" cy="205.0" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="258.0" cy="183.8" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="324.0" cy="163.8" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="390.0" cy="142.5" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="456.0" cy="122.5" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="522.0" cy="101.2" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="588.0" cy="77.5" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="654.0" cy="65.0" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="720.0" cy="40.0" r="2.5" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <polyline points="60.0,237.5 73.2,240.0 86.4,240.0 99.6,240.0 112.8,240.0 126.0,238.8 139.2,240.0 152.4,237.5 165.6,240.0 178.8,240.0 192.0,240.0 205.2,240.0 218.4,238.8 231.6,240.0 244.8,237.5 258.0,240.0 271.2,240.0 284.4,240.0 297.6,240.0 310.8,238.8 324.0,240.0 337.2,237.5 350.4,240.0 363.6,240.0 376.8,240.0 390.0,240.0 403.2,238.8 416.4,240.0 429.6,237.5 442.8,240.0 456.0,240.0 469.2,240.0 482.4,240.0 495.6,238.8 508.8,240.0 522.0,237.5 535.2,240.0 548.4,240.0 561.6,240.0 574.8,240.0 588.0,238.8 601.2,240.0 614.4,237.5 627.6,240.0 640.8,240.0 654.0,240.0 667.2,240.0 680.4,238.8 693.6,240.0 706.8,237.5 720.0,240.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3"/>
  <circle cx="60.0" cy="237.5" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="126.0" cy="238.8" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="192.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="258.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="324.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="390.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="456.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="522.0" cy="237.5" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="588.0" cy="238.8" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="654.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <circle cx="720.0" cy="240.0" r="2.5" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="139.2" y="102.5" fill="currentColor">delay(10): 50 주기 뒤 160 tick 밀림 (평균 주기 13.16)</text>
  <text x="416.4" y="230.0" fill="currentColor">delay_until: 0–2 tick 에서 머문다 (평균 주기 9.96 → 10)</text>
  <circle cx="80" cy="298" r="4" class="f-warn" stroke="currentColor"/> <text x="90" y="302" fill="currentColor">delay — 실선</text> <circle cx="230" cy="298" r="4" class="f-ok" stroke="currentColor"/> <text x="240" y="302" fill="currentColor">delay_until — 점선 (점은 5 주기마다)</text>
</svg>
```

읽는 법 — 식 두 줄:

- `delay`: rel(k+1) = rel(k) + (c_k − 1) + 10 + L(k+1), L = 깨어난 뒤 COMM 때문에 기다린 tick. 매 주기 **(c_k − 1) + L** 이 새고 되돌아오지 않는다. 50 주기 뒤 160 = rel(0) 2 + Σ(c_k − 1) 92 + ΣL 66. 하네스 체크: 모든 주기 ≥ 10, 드리프트 ≥ Σ(c_k − 1).
- `delay_until`: rel(k) = 10k + L(k), L(k) ∈ {0, 1, 2} — COMM(7 tick 마다 2 tick) 과 격자가 겹칠 때만 늦고 **쌓이지 않는다**. 평균 주기 9.96 은 rel(0) = 2 가 늦게 시작해서다 (500 − 2)/50.
- 두 판 모두 tick 이 0xFFFFFFF0 에서 0x00000286 / 0x000001e6 까지 갔다 = wrap 을 지났는데 `g_wake_err = g_list_err = g_ret_err = 0`.

**B2 — wrap 을 가로지르는 깨우기 (실제 출력)**

```text
B2 list after 6 ticks: B(fffffff6) D(fffffff6) A(00000004) E(00000009) C(00000010) F(7ffffff4)
B    : r R B B B B R . . .
D    : r r r R B B r R . .
B2 wake order: B D A E C   woke at: B=fffffff6 D=fffffff6 A=00000004 E=00000009 C=00000010
```

B 와 D 는 같은 tick(f6)에 깨지만 둘 다 p2 라서 D 는 한 tick `r` 로 기다린다 (깨어남 ≠ 실행). F(2^31 − 1) 는 끝까지 B.

**B3 무작위** — `300 scenarios, wrapped in 150, wakes=27102, delay_until false=7740, scenarios with errors=0`. 작업 길이가 주기보다 긴 경우를 일부러 섞어 `false` 경로도 7740 번 지났다.

**진짜 FreeRTOS 도 같은가** — `demo_freertos_delay.c` (POSIX 포트, `configINITIAL_TICK_COUNT = ULONG_MAX - 15`; 이 포트의 `TickType_t` 는 `unsigned long` = 64비트라서 (portmacro.h:66) 2^64 에서 wrap 하지만 지나는 코드 경로는 32비트와 같다):

<!-- code: demo_freertos_delay.c -->

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_delay.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_freertos_delay && ./demo_freertos_delay
```

```text
start tick S = 0xfffffffffffffff0, now = S+40 (wrapped: now = 0x18)
1) wake order across the wrap: BDAEC
   A called at S+0, woke at S+20  (raw 0x0000000000000004)
   B called at S+0, woke at S+5   (raw 0xfffffffffffffff5)
   C called at S+0, woke at S+30  (raw 0x000000000000000e)
   D called at S+0, woke at S+5   (raw 0xfffffffffffffff5)
   E called at S+0, woke at S+25  (raw 0x0000000000000009)
2) costs: 3 3 4 4 2 1 2 4 1 3
   vTaskDelay(10)       releases (rel. first):   0  12  24  37  50  61  71  82  95 105
   xTaskDelayUntil(10)  releases (rel. first):   0  10  20  30  40  50  60  70  80  90
```

10번 실행: 깨우기 순서 `BDAEC`, `xTaskDelayUntil` 릴리스 줄, 절 3·4 는 **10/10 같았다**. 달라진 것은 호스트 타이밍 두 가지: 7번은 태스크들이 S+1 에 지연을 걸어 A–D 가 모두 1 tick 늦게(S+6, S+21, S+31) 깼는데 **E(`xTaskDelayUntil(&S, 25)`) 는 10번 모두 S+25** 였다 — 절대 지연의 정의 그대로. `vTaskDelay` 릴리스 줄은 9번이 위와 같고 1번은 첫 주기가 13 이었다. 모델 식 rel(k+1) = rel(k) + c_k − 1 + 10 으로 계산한 0, 12, 24, 37, 50, 61, 71, 82, 95, 105 와 정확히 같다.

### Part C — overrun: CATCH_UP vs SKIP (실제 출력)

```c
static bool periodic_wait(tcb_t *self, periodic_t *pt) {
    if (pt->policy == PT_SKIP) {
        uint32_t elapsed = g_tick - pt->last;
        if (elapsed > pt->period) {                      /* last+period 가 이미 과거 */
            uint32_t m = (elapsed - 1) / pt->period;     /* now 보다 앞인 격자점 수 */
            pt->last += m * pt->period; pt->skipped += m;
        }
    }
    bool blocked = task_delay_until(self, &pt->last, pt->period);
    if (!blocked) pt->overruns++;
    return blocked;
}
```

```text
C CATCH_UP IMU releases: 0 10 20 30 55 57 60 70 80 90 100 110  | overruns=2 skipped=0  BLE releases: 2 24 59 83 105
C SKIP     IMU releases: 0 10 20 30 60 70 80 90 100 110  | overruns=0 skipped=2  BLE releases: 2 24 55 77 99
```

```svg
<svg viewBox="0 0 740 258" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C 실제 타임라인 — IMU(p3) 주기 10, k=3 작업 25 tick (시작 0xFFFFFFE0)</text>
  <text x="71.0" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="112.2" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="153.4" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="194.6" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text> <text x="235.8" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="277.0" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="318.2" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="359.4" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text> <text x="400.6" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="441.8" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="483.0" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="524.2" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text> <text x="565.4" y="34" fill="currentColor" font-family="monospace" font-size="12">0</text> <text x="606.6" y="34" fill="currentColor" font-family="monospace" font-size="12">4</text> <text x="647.8" y="34" fill="currentColor" font-family="monospace" font-size="12">8</text> <text x="689.0" y="34" fill="currentColor" font-family="monospace" font-size="12">c</text>
  <text x="10" y="54" fill="currentColor" font-weight="bold">CATCH_UP (xTaskDelayUntil)</text>
  <text x="10" y="71" fill="currentColor" font-family="monospace">IMU</text> <rect x="70.0" y="58" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="90.6" y="58" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="173.0" y="58" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="193.6" y="58" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="276.0" y="58" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="296.6" y="58" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="379.0" y="58" width="298.7" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="677.7" y="58" width="10.3" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="688.0" y="58" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="708.6" y="58" width="20.6" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="95" fill="currentColor" font-family="monospace">BLE</text> <rect x="70.0" y="82" width="20.6" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="90.6" y="82" width="30.9" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="121.5" y="82" width="195.7" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="317.2" y="82" width="30.9" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="348.1" y="82" width="195.7" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="543.8" y="82" width="133.9" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="677.7" y="82" width="10.3" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="688.0" y="82" width="20.6" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="708.6" y="82" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="118" fill="currentColor" font-weight="bold">SKIP (격자 유지)</text>
  <text x="10" y="135" fill="currentColor" font-family="monospace">IMU</text> <rect x="70.0" y="122" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="90.6" y="122" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="173.0" y="122" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="193.6" y="122" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="276.0" y="122" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="296.6" y="122" width="82.4" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="379.0" y="122" width="257.5" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="636.5" y="122" width="51.5" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="688.0" y="122" width="20.6" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="708.6" y="122" width="20.6" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="159" fill="currentColor" font-family="monospace">BLE</text> <rect x="70.0" y="146" width="20.6" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="90.6" y="146" width="30.9" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="121.5" y="146" width="195.7" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="317.2" y="146" width="30.9" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="348.1" y="146" width="195.7" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="543.8" y="146" width="92.7" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="636.5" y="146" width="30.9" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="667.4" y="146" width="61.8" height="18" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <path d="M70.0 42 L70.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M173.0 42 L173.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M276.0 42 L276.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M379.0 42 L379.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M482.0 42 L482.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M585.0 42 L585.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/> <path d="M688.0 42 L688.0 170" stroke="currentColor" stroke-width="0.8" stroke-dasharray="2 3"/>
  <path d="M399.6 36 L399.6 170" stroke="currentColor" stroke-width="2"/> <text x="403.6" y="184" fill="currentColor">wrap: 0xFFFFFFFF → 0</text>
  <rect x="10" y="196" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="207" fill="currentColor">R 실행</text> <rect x="96" y="196" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="116" y="207" fill="currentColor">r READY(CPU 대기)</text> <rect x="281" y="196" width="14" height="12" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <text x="301" y="207" fill="currentColor">B delayed list 에서 잠</text> <path d="M502 202 L522 202" stroke="currentColor" stroke-dasharray="2 3"/> <text x="528" y="207" fill="currentColor">이상적 격자 (10 tick)</text>
  <text x="10" y="228" fill="currentColor">CATCH_UP: 오프셋 55 · 57 에 늦은 샘플 2개를 연달아 실행하고 60 에서 격자 복귀.</text>
  <text x="10" y="244" fill="currentColor">SKIP: 40 · 50 을 버리고 60 에서 바로 격자. BLE(p1)는 CATCH_UP 쪽에서 더 오래 굶는다.</text>
</svg>
```

| | CATCH_UP (`xTaskDelayUntil`) | SKIP (격자 유지) |
|---|---|---|
| 샘플 수 (0–119) | 12 — 하나도 안 잃음 | 10 — 40, 50 을 버림 |
| 늦은 샘플 | 2개 (15 tick, 7 tick 늦음), 연달아 실행 | 없음 — 다음 격자 60 |
| 격자 복귀 | 60 부터 | 60 부터 |
| 낮은 태스크 | BLE 가 59 까지 밀림 (따라잡기 폭주가 CPU 를 더 씀) | BLE 55 |
| 맞는 곳 | 모든 이벤트를 세야 할 때 (적산계, 카운터) — 단, 늦은 샘플엔 **실제 시각** 타임스탬프 | 제어 루프·센서 융합 (늦은 샘플은 오히려 해롭다), 오디오 프레임 |

계산 확인: k=3 은 30 에 시작해 25 tick → tick 54 에 대기 호출. CATCH_UP: elapsed = 54 − 30 = 24 ≥ 10 → `false`, last = 40 → 55 에 k=4, 56 에 호출: elapsed 16 → `false`, last = 50 → 57 에 k=5, 58 에 호출: elapsed 8 → 블록, 60. SKIP: m = (24 − 1)/10 = 2 → last = 50, skipped = 2, wake 60.

**진짜 FreeRTOS** (같은 데모 절 3·4):

```text
3) overrun at k=3 (25 ticks), period 10
   xTaskDelayUntil k3=25 releases (rel. first):   0  10  20  30  54  55  60  70  80  returns 1 1 1 0 0 1 1 1 1
   xTaskPeriodicDelay   releases (rel. first):   0  10  20  30  60  70  80  90 100  returns 0 1 1 3 1 1 1 1 1
4) with vTaskDelay(0): XYXYXY   without: WWWZZZ
```

- `xTaskDelayUntil` 은 `pdFALSE` 2번 뒤 60 에서 격자 복귀 — 모델과 같다. 늦은 두 샘플이 **54, 55** (모델 55, 57) 인 것은 모델의 "한 step = 1 tick" 때문이다. 진짜 태스크는 `pdFALSE` 를 받은 그 tick 안에서 바로 다음 작업을 시작한다 (연습 4 의 계산식은 이 동작을 따라 54, 55 를 낸다).
- V11.3.2 에는 SKIP 에 해당하는 **`xTaskPeriodicDelay`** 가 있다 (tasks.c:2365–2411, task.h:897–931): `xIncrements = xTicksElapsed / xTimeIncrement; *pxPreviousWakeTime += ...; xTicksToWait = xTimeIncrement - ( xTicksElapsed - xTicksIncrements );` — 반환값 3 = 정상 1 + 건너뛴 2. 이 함수의 `last` 는 "지난 과거 wake" 라서 한 주기 늦게 따라오고, now 와 정확히 겹친 격자점도 건너뛴다 (연습 4). 공식 웹 API 문서에서는 아직 못 찾았으니 쓰기 전에 내 커널 버전에 있는지 확인할 것.

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS (V11.3.2 소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `task_delay(t, n)` | `vTaskDelay(n)` (tasks.c:2506) | `k_sleep(K_TICKS(n))` / `k_msleep(ms)` | `osDelay(n)` |
| `task_delay(t, 0)` | `vTaskDelay(0)` = yield (2512–2541), `taskYIELD()` | `k_yield()` | `osThreadYield()` |
| `task_delay_until(t, &last, P)` | `xTaskDelayUntil(&last, P)` → pdTRUE/pdFALSE (2414–2498) | `k_sleep(K_TIMEOUT_ABS_TICKS(t))` (64비트 timeout 설정 필요) 또는 `k_timer` 주기 | `osDelayUntil(abs_ticks)` + `osKernelGetTickCount()` |
| `PT_SKIP` | `xTaskPeriodicDelay(&last, P)` → 더한 주기 수 (2365) | `k_timer_status_get()` 으로 놓친 만료 수 확인 | 직접 계산 |
| `g_tick` | `xTaskGetTickCount()` (4213), ISR 에선 `...FromISR` | `k_uptime_ticks()` (64비트) | `osKernelGetTickCount()` (uint32_t) |
| tick 폭 | `configTICK_TYPE_WIDTH_IN_BITS` 16/32/64 — CM4F 포트는 32비트일 때만 `portTICK_TYPE_IS_ATOMIC 1` (portable/GCC/ARM_CM4F/portmacro.h:62–77) | 기본 64비트 시간 (문서 기준) | 32비트 |
| wrap 테스트 시작점 | `configINITIAL_TICK_COUNT` (FreeRTOS.h:2921, tasks.c:506) | — | — |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

출력은 5. 풀이에 실은 줄들 + 마지막 `36/36 passed`. 5번 실행 출력 md5 가 모두 같았다(결정적, 0.1 초). 새니타이저 (경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 36/36
```

단일 스레드 시뮬레이터라 데이터 레이스는 없다(TSan 불필요). 이 레슨의 버그(wrap 비교, 정렬, 마감 계산)는 전부 **로직 버그** 라서 64비트 오라클과 리스트 불변식으로 잡는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (now >= wake)` | wrap 을 사이에 두면 뒤집힌다. 0xFFFFFFF0 에서 건 `delay(20)` 이 즉시 끝남 (demo) | `(uint32_t)(now - wake) < 0x80000000u` |
| `(int32_t)now >= (int32_t)wake` | 각각 부호로 바꿔도 0x7FFFFFFF → 0x80000000 에서 똑같이 뒤집힌다. 빼고 나서 바꿔야 한다 | `(int32_t)(now - wake) >= 0` (구현 정의 주의) |
| 리스트를 wake 날값으로 정렬 | wrap 너머 값(0x04)이 맨 앞에 와서 먼저 깬다 | wrap 안전 비교로 정렬, 또는 FreeRTOS 두 리스트 |
| 주기 태스크에 `vTaskDelay(P)` | 매 주기 작업 시간 + 선점 시간만큼 밀린다 (B1: 50 주기에 160 tick) | `xTaskDelayUntil(&last, P)` |
| `last` 를 루프 안에서 매번 `xTaskGetTickCount()` 로 다시 읽음 | 기준점이 "지금" 이 되어 그냥 `vTaskDelay` 와 같아진다 | 루프 **밖에서 한 번** 초기화, 이후는 커널이 갱신 |
| `vTaskDelay(P - (now - start))` 로 직접 계산 | 시계를 읽은 뒤 delay 를 부르기 전에 선점되면 그 시간이 샌다 (연습 2: 30 주기에 93 tick) | 커널 안에서 원자적으로 하는 `xTaskDelayUntil` |
| `xTaskDelayUntil` 반환값 무시 | overrun 을 모른 채 늦은 샘플을 연달아 처리한다 | `if (xTaskDelayUntil(...) == pdFALSE) overrun++;` + 정책 결정 |
| `vTaskDelay(0)` 으로 낮은 태스크에게 양보했다고 생각 | 같은 우선순위에게만 양보한다. 바쁜 루프면 idle 태스크(와 낮은 태스크)가 굶는다 | 정말 쉬려면 `vTaskDelay(1)` 이상이나 이벤트 대기 |
| `vTaskDelay(1)` = 1 ms 라고 가정 | 호출 시점이 tick 중간이면 실제로는 0~1 tick 사이 | "최소 n tick" 이 필요하면 `n + 1` |
| 2^31 이상 지연 | wrap 안전 비교의 범위를 넘으면 즉시 깬다 (연습 3: 16비트에서 40000) | 범위 제한 또는 두 리스트 방식 |

## 8. 면접 Q&A

<details>
<summary>Q1. vTaskDelay 와 xTaskDelayUntil 의 차이는? 각각 언제 쓰나요?</summary>

`vTaskDelay(n)` 은 **호출 시각 기준** n tick, `xTaskDelayUntil(&last, P)` 는 **지난 wake 시각 + P** 라는 절대 시각까지. 주기 태스크(센서 샘플링, 제어 루프, 하트비트)는 반드시 DelayUntil — 상대 지연은 작업 시간과 선점 시간이 매 주기 더해져 드리프트한다 (B1: P=10, 작업 1–4 tick 에서 평균 주기 13.16, 50 주기 뒤 160 tick 밀림; DelayUntil 은 늦음 0–2 에 머묾). 상대 지연은 "최소 이만큼 쉬어라" 가 맞는 곳: 디바운스, 재시도 백오프, 센서 전원 켠 뒤 안정화 대기. 꼬리: "DelayUntil 은 지터(jitter)도 없애나?" → 아니다. 깨어날 **시각** 은 격자에 있지만 실제 실행은 더 높은 태스크·ISR 때문에 늦을 수 있다 (B1 의 L(k)). 쌓이지 않을 뿐이다. 지터까지 줄이려면 우선순위를 높이거나 하드웨어 타이머 트리거(ADC/DMA)를 쓴다.

</details>

<details>
<summary>Q2. tick 카운터가 넘치면 FreeRTOS 는 어떻게 하나요?</summary>

delayed list 를 두 개 둔다. 태스크를 넣을 때 `now + ticks` 가 넘쳤으면(`xTimeToWake < xConstTickCount`) overflow 리스트에, 아니면 current 리스트에 넣는다 (tasks.c:8729–8745). tick 이 0 이 되는 순간 `taskSWITCH_DELAYED_LISTS()` 로 두 포인터를 맞바꾸고 `xNumOfOverflows++` (tasks.c:265–278, 4858–4861). 그래서 리스트 안에서는 평범한 `<`, `>=` 비교로 충분하고, 지연 범위가 거의 tick 전체(2^32 − 1)다. 바꾸는 순간 current 는 비어 있어야 한다는 `configASSERT` 가 있다 — 이번 바퀴 값은 그 전에 다 깼어야 하므로. 꼬리: "타임아웃(세마포어 take 대기)은?" → `vTaskSetTimeOutState` 가 `xNumOfOverflows` 를 같이 저장하고 `xTaskCheckForTimeOut` 이 "넘침 횟수가 달라졌고 now ≥ 시작" 이면 한 바퀴 이상 돈 것으로 본다 (tasks.c:5731).

</details>

<details>
<summary>Q3. wrap 에 안전한 시간 비교를 C 로 써 보세요. 왜 맞죠? 한계는?</summary>

`static inline bool time_reached(uint32_t now, uint32_t t) { return (uint32_t)(now - t) < 0x80000000u; }`. 부호 없는 뺄셈은 mod 2^32 라서 진짜 차이 D 가 [−2^31, 2^31) 에 있으면 결과 < 2^31 ⇔ D ≥ 0. 한계: 두 시각이 2^31 이상 떨어지면 판단 불가 → 지연/타임아웃은 2^31 − 1 tick 이하 (1 kHz 32비트 ≈ 24.8 일, 16비트면 32.767 초). `(int32_t)(now - t) >= 0` 도 같은 뜻이지만 범위 밖 변환은 구현 정의(C11 6.3.1.3p3). Linux 커널의 `time_after()` / `time_before()` 매크로(include/linux/jiffies.h)가 같은 기법이다. 꼬리: "wrap 버그를 어떻게 테스트하나?" → 카운터를 max 직전에서 시작한다. FreeRTOS `configINITIAL_TICK_COUNT` (FreeRTOS.h:2921), 이 레슨은 0xFFFFFFF0, Linux 는 jiffies 를 부팅 몇 분 뒤 wrap 하도록 초기화하는 것으로 알려져 있다 (`INITIAL_JIFFIES`, 세부 미검증). 그리고 64비트 그림자 시계를 오라클로 둔다.

</details>

<details>
<summary>Q4. xTaskDelayUntil 이 pdFALSE 를 돌려주면 무슨 뜻이고 어떻게 처리하나요?</summary>

다음 wake 시각(`last + P`) 이 이미 지났거나 **바로 지금** 이라서 블록하지 않았다는 뜻 (`xTimeToWake > xConstTickCount` 가 거짓, tasks.c:2436–2465). 그래도 `*pxPreviousWakeTime` 은 갱신되므로(2468) 다음 호출들도 연달아 `pdFALSE` 를 받으며 놓친 주기를 따라잡는다(catch-up). 처리는 정책: ① 그대로 따라잡기 — 이벤트 수가 중요할 때 ② 건너뛰기 — `xTaskPeriodicDelay` (V11.3.2 tasks.c:2365) 또는 직접 `last` 를 격자 위로 당긴다 (Part C SKIP) ③ 재동기화 `last = xTaskGetTickCount()` — 위상을 버린다. 어느 경우든 overrun 카운터를 올려 텔레메트리로 내보낸다 (WCET 예산이 깨졌다는 신호). 꼬리: "늘 pdFALSE 면?" → 작업이 주기보다 길다. 주기를 늘리거나 일을 쪼개거나(상태 기계, RTOS-B01) 낮은 우선순위 태스크로 넘긴다.

</details>

<details>
<summary>Q5. vTaskDelay(0) 은 뭘 하나요? taskYIELD 와 같나요?</summary>

`xTicksToDelay > 0` 일 때만 delayed list 에 넣고, 0 이면 바로 `taskYIELD_WITHIN_API()` 만 한다 (tasks.c:2512–2541) → 같은 우선순위의 다른 READY 태스크에게 차례를 넘긴다. 데모: time slicing 을 끈 상태에서 `XYXYXY` (없으면 `WWWZZZ`). `taskYIELD()` 와 효과가 같다. 함정: 낮은 우선순위는 여전히 못 돈다 — `for(;;){ poll(); vTaskDelay(0); }` 는 idle 태스크를 굶겨 idle hook·메모리 해제(삭제된 태스크 정리)·tickless 절전이 안 일어난다. 꼬리: "그럼 폴링 루프는?" → `vTaskDelay(1)` 이상이나, 더 좋게는 ISR → 세마포어/태스크 알림으로 바꾼다 (RTOS-B08, M01).

</details>

<details>
<summary>Q6. 남은 시간을 직접 계산해서 vTaskDelay(P − elapsed) 하면 안 되나요?</summary>

안 된다. `elapsed = now() − start;` 와 `vTaskDelay(P − elapsed);` 사이에 선점이나 tick 경계가 끼면 그 시간이 측정되지 않고 샌다. 게다가 기준점 `start` 가 "실제로 깨어난(늦은) 시각" 이라 깨어난 뒤 기다린 시간도 다음 주기로 넘어간다. 연습 2: 정리 1 tick + 선점이 끼는 루프에서 30 주기 뒤 93 tick 드리프트, `delay_until` 은 최대 3 tick 늦음. FreeRTOS 는 `vTaskSuspendAll()` 안에서 `const TickType_t xConstTickCount = xTickCount;` 한 번 읽고 계산·리스트 삽입까지 한다 (tasks.c:2425–2477) — 계산과 블록이 원자적이다. 꼬리: "ISR 이 그 사이에 tick 을 올리면?" → 스케줄러가 멈춘 동안 tick 은 `xPendedTicks += 1U` 로 미뤄졌다가 (tasks.c:5064) `xTaskResumeAll` 에서 한꺼번에 처리된다 (4151–4171).

</details>

<details>
<summary>Q7. delayed list 를 정렬 리스트로 두면 삽입이 O(n) 인데 괜찮나요? tick ISR 비용은?</summary>

삽입 O(n) 은 태스크 수가 수십 개인 MCU 에서는 문제가 아니고, 대신 **tick ISR 이 O(1)** 이 된다: 맨 앞만 보면 되고, FreeRTOS 는 그마저 `xNextTaskUnblockTime` 에 캐시해서 `if( xConstTickCount >= xNextTaskUnblockTime )` 일 때만 리스트를 연다 (tasks.c:4871). 실시간 시스템에서는 ISR 의 최악 시간이 태스크 쪽 삽입보다 중요하다. 같은 값으로 tickless idle 이 "다음 깰 때까지 몇 tick 잘 수 있나" 를 `xNextTaskUnblockTime - xTickCount` 로 계산한다 (tasks.c:4054–4055 — 이것도 wrap 안전한 뺄셈). 꼬리: "태스크/타이머가 수천 개면?" → 타이밍 휠(timing wheel), 최소 힙 등 — Linux 타이머 휠이 예 (세부 미검증).

</details>

## 9. 추가 연습

### 연습 1 — FreeRTOS 두 리스트 트릭을 직접 구현

`dl2_insert(now + ticks 가 넘치면 overflow 리스트)` 와 `dl2_tick(now++; 0 이면 교환; 머리부터 now >= wake 인 것 pop)` 을 평범한 `<` 만으로 짜고, 이 레슨의 wrap 안전 단일 리스트와 무작위로 비교하라. 교환 순간 current 가 비어 있나?

<details>
<summary>답 보기</summary>

```text
ex1 two-list vs wrap-safe: 200 scenarios x 9000 ticks, wake events=25336, mismatching ticks=0, list swaps=200, swap with non-empty current list=0
```

모든 tick 에서 깨어난 id 열이 같다. 교환 때 current 가 비어 있지 않은 경우 0 — current 에는 wake ≤ 0xFFFFFFFF 인 것만 있으니 0 이 되기 전에 다 깼다. FreeRTOS 가 그 자리에 `configASSERT` 를 둔 이유. 정렬 삽입은 "앞 원소 ≤ 나 이면 지나간다" 로 FIFO (list.c:192 와 같다).

</details>

### 연습 2 — `delay(P − elapsed)` 손 계산은 왜 새나

PER(p1, P=10): 작업 c tick → **시계를 읽고** → 정리(로그) 1 tick → `delay(10 − elapsed)`. HI(p2) 는 13 tick 마다 3 tick. 30 주기 드리프트를 `delay_until` 과 비교하라.

<details>
<summary>답 보기</summary>

```text
ex2 k    :    0    5   10   15   20   25   30
ex2 naive:    3   18   33   48   63   78   93   (drift = release - 10k)
ex2 until:    3    0    0    0    0    0    2
ex2 naive drift after 30 periods = 93 ticks, delay_until max lateness = 3
```

주기마다 약 3 tick 이 샌다: 정리 1 tick(읽기와 호출 사이) + 그 사이 선점 + 깨어난 뒤 HI 때문에 기다린 시간(기준점이 실제 릴리스라서 다음 주기로 이월). `delay_until` 은 기준이 격자라서 늦어도 최대 3 (HI 한 번) 에 머문다. 교훈: 시간 계산과 블록은 커널 안에서 한 번에.

</details>

### 연습 3 — 16비트 tick 의 긴 지연

`configTICK_TYPE_WIDTH_IN_BITS = TICK_TYPE_WIDTH_16_BITS` (1 kHz 면 65.5 초마다 wrap) 에서 지연 0x7FFF, 40000, 65535 를 wrap 안전 비교와 두 리스트 방식으로 각각 시뮬레이션하라.

<details>
<summary>답 보기</summary>

```text
ex3 16-bit start=  100 delay=32767: wrap-safe compare woke after  32767, two-list after  32767
ex3 16-bit start=  100 delay=40000: wrap-safe compare woke after      0, two-list after  40000
ex3 16-bit start=60000 delay=40000: wrap-safe compare woke after      0, two-list after  40000
ex3 16-bit start=60000 delay=65535: wrap-safe compare woke after      0, two-list after  65535
```

wrap 안전 비교는 거리 ≥ 2^15 에서 "이미 지남" 으로 판단해 **즉시 깬다**. 두 리스트는 "넘쳤나" 를 한 번만 보고 리스트를 고르므로 최대 2^16 − 1 까지 맞다. 8/16비트 MCU 에서 16비트 tick 을 쓰면 `pdMS_TO_TICKS(40000)` 같은 지연이 이런 이유로 위험하다 — 타임아웃 비교를 직접 짤 때는 범위를 주석과 assert 로 박아 둔다.

</details>

### 연습 4 — `xTaskPeriodicDelay` 의미를 식으로 구현

tasks.c:2378–2390 을 옮긴 `periodic_delay_calc(now, &last, P, &wait)` 를 쓰고, 가장 높은 우선순위 태스크 하나(작업 먼저, 그다음 대기)로 k=3 작업이 25 tick / 11 tick 일 때 `xTaskDelayUntil` 과 릴리스를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex4 DelayUntil    k=3 cost 25: releases   0  10  20  30  54  55  60  70  80  | returns 1 1 1 0 0 1 1 1 1
ex4 PeriodicDelay k=3 cost 25: releases   0  10  20  30  60  70  80  90 100  | returns 0 1 1 3 1 1 1 1 1
ex4 DelayUntil    k=3 cost 11: releases   0  10  20  30  40  50  60  70  80  | returns 1 1 1 0 1 1 1 1 1
ex4 PeriodicDelay k=3 cost 11: releases   0  10  20  30  50  60  70  80  90  | returns 0 1 1 2 1 1 1 1 1
```

25 tick 경우는 진짜 FreeRTOS 데모 절 3 과 **숫자가 똑같다**. 11 tick 경우(호출이 정확히 격자 40 에 걸림): `xTaskDelayUntil` 은 `pdFALSE` 를 주지만 그 tick 에 바로 실행되니 사실상 제때, `xTaskPeriodicDelay` 는 elapsed = 20 → 증분 2 → 40 도 건너뛰고 50. 첫 호출은 0 을 돌려준다 (`last` 가 "지난 wake" 를 한 주기 늦게 따라가는 설계). 어느 함수를 쓰든 반환값을 overrun 신호로 읽는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 12/12, 경고 0
```

## 10. 핵심 정리

- `delay(n)` = 호출 시각 + n (상대), `delay_until(&last, P)` = last + P (절대). 주기 태스크는 DelayUntil — 상대 지연은 작업 + 선점 시간이 매 주기 쌓인다 (50 주기에 160 tick vs 0–2 tick).
- wrap 안전 비교: `(uint32_t)(now - wake) < 2^31` — 거리 < 2^31 이면 wrap 위치와 무관하게 맞다. `now >= wake` 는 wrap 에서 뒤집힌다.
- delayed list 는 wake 순 정렬 + FIFO, tick 은 머리만 본다. FreeRTOS 는 current/overflow 두 리스트를 tick 0 에서 교환 (tasks.c:265–278, 4858) + `xNextTaskUnblockTime` 캐시.
- `xTaskDelayUntil` 은 마감이 지났거나 == now 면 `pdFALSE`, 그래도 `last` 는 갱신 → 따라잡기. 건너뛰기는 `xTaskPeriodicDelay` 또는 직접 격자 당기기.
- `vTaskDelay(0)` = yield (같은 우선순위에게만). `vTaskDelay(1)` 은 0~1 tick.
- 시간 계산과 블록은 커널 안에서 원자적으로 — 직접 `P − elapsed` 를 계산하면 샌다.
- wrap 테스트는 카운터를 max 직전에서 시작 (`configINITIAL_TICK_COUNT`) + 64비트 오라클.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` — `taskSWITCH_DELAYED_LISTS` 265–278, 리스트 포인터 481–482, `xTickCount` 초기값 506, `xTaskPeriodicDelay` 2365–2411, `xTaskDelayUntil` 2414–2498, `vTaskDelay` 2506–2549, `prvGetExpectedIdleTime` 4002/4054, `xTaskGetTickCount` 4213, `xTaskIncrementTick` 4856–4904, `xTaskCheckForTimeOut` 5731, `prvAddCurrentTaskToDelayedList` 8685–8745; `list.c:192`; `include/task.h` 855–1009; `include/FreeRTOS.h:2921`; `portable/GCC/ARM_CM4F/portmacro.h:62–77`; `portable/ThirdParty/GCC/Posix/portmacro.h:66`: <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS API Reference — Task Control (`vTaskDelay`, `xTaskDelayUntil`), Task Utilities (`xTaskGetTickCount`): <https://www.freertos.org> → Documentation → API Reference
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Task Management 장 ("Using the Blocked state to create a delay", `vTaskDelayUntil`): <https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book>
- Zephyr — Kernel Timing / Timers (`k_sleep`, `K_TIMEOUT_ABS_TICKS`, `k_timer`): <https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html>
- CMSIS-RTOS2 — Generic Wait Functions (`osDelay`, `osDelayUntil`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__Wait.html>
- Linux `include/linux/jiffies.h` — `time_after()` / `time_before()` 와 `INITIAL_JIFFIES`: <https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h>
- ISO/IEC 9899:2011 (C11) 6.3.1.3 — 부호 있는 정수로의 변환 (범위 밖이면 구현 정의)
- 관련 레슨: RTOS-B01 (협력형 스케줄러, `next_run += period`), RTOS-B02 (TCB·상태 전이), RTOS-B05 (선점 tick), RTOS-B08/B09 (세마포어 대기 + 타임아웃), RTOS-B12 (소프트웨어 타이머: auto-reload 도 같은 격자 문제), RTOS-M01 (태스크 알림), C-M14 (ISR 공유 데이터)
