---
id: RTOS-A02
time: 110분
updated: 2026-11-11
---
# RTOS-A02 Tickless idle: 다음 만료 계산과 tick 보정

## 학습 목표

- idle 에서 **다음 지연 만료까지 몇 tick 쉴 수 있나** 를 계산하고 (FreeRTOS `prvGetExpectedIdleTime`), 24-bit 타이머 폭에서 나오는 **최대 억제 tick** 상한과 `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` 문턱을 설명할 수 있다.
- SysTick 같은 다운 카운터를 **단발(one-shot)** 로 다시 프로그램하고, 깨어난 뒤 두 경로 — (a) 타이머 만료, (b) 다른 인터럽트로 조기 기상 — 에서 **경과한 온전한 tick 수** 와 **남은 조각(fraction)** 을 정확히 계산해 드리프트 0 을 만든다.
- 잠들기 직전의 경쟁(그 사이 태스크가 깨어남, tick 이 이미 펜딩)을 `eTaskConfirmSleepModeStatus` 와 펜딩 tick 처리로 막는 이유를 말할 수 있다.
- "tickless 는 왜 시간이 밀리나?" 에 원인 4가지(조각 버림, 정지 구간, 나누어떨어지지 않는 N, 너무 긴 인터럽트 금지)와 고침을 숫자로 답한다.
- 10분 랜덤 실행으로 **깨어남 횟수 / 평균 전류** 를 ticked idle 과 비교하고, tickless 가 진짜로 이득이 되는 조건(깊은 슬립 + 저전력 타이머)을 설명할 수 있다.

## 1. 문제

배터리 센서 노드 면접에서 "**tickless idle 을 켰더니 시계가 하루에 몇 초씩 밀립니다. 어디를 보겠습니까?**" 는 단골이다 — 저전력(low-power) 설계와 커널 시간 관리(timekeeping)를 한 번에 확인할 수 있기 때문이다.

1 kHz tick 이면 CPU 는 할 일이 없어도 1초에 1000번 깨어나 tick ISR 을 돈다. Tickless idle 은 idle 태스크가 "다음에 누가 깨어나야 하는 시각" 을 보고 그때까지 tick 인터럽트를 **억제(suppress)** 한다. 타이머를 그 시각에 한 번만 울리게(단발) 다시 설정하고, 자고, 깨어나서 **그동안 세지 못한 tick 을 한 번에 더한다(step)**. 어려운 것은 마지막 단계다. 예상보다 일찍 다른 인터럽트(버튼, 가속도계, UART)로 깨면 "지금이 tick 경계 사이의 어디인가" 를 카운터 값으로 계산해서 **온전한 tick 만 더하고, 남은 조각으로 타이머를 다시 시작** 해야 한다. 조각을 버리면 깰 때마다 평균 반 tick 씩 커널 시계가 뒤처진다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ticked idle vs tickless idle (주기 10 tick 태스크 + 3.25 tick 에 외부 IRQ)</text>
  <line x1="60" y1="270" x2="720" y2="270" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="56" y="288">0</text><text x="116" y="288">1</text><text x="176" y="288">2</text><text x="236" y="288">3</text>
    <text x="296" y="288">4</text><text x="356" y="288">5</text><text x="416" y="288">6</text><text x="476" y="288">7</text>
    <text x="536" y="288">8</text><text x="596" y="288">9</text><text x="652" y="288">10</text>
  </g>
  <text x="10" y="58" fill="currentColor">ticked</text>
  <rect x="60" y="45" width="10" height="20" class="f-acc" stroke="currentColor"/>
  <g class="f-warn" stroke="currentColor">
    <rect x="120" y="48" width="4" height="14"/><rect x="180" y="48" width="4" height="14"/><rect x="240" y="48" width="4" height="14"/>
    <rect x="300" y="48" width="4" height="14"/><rect x="360" y="48" width="4" height="14"/><rect x="420" y="48" width="4" height="14"/>
    <rect x="480" y="48" width="4" height="14"/><rect x="540" y="48" width="4" height="14"/><rect x="600" y="48" width="4" height="14"/>
  </g>
  <rect x="255" y="45" width="8" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="660" y="45" width="10" height="20" class="f-acc" stroke="currentColor"/>
  <text x="430" y="40" fill="currentColor" font-size="12">매 tick 깨어남 (경고색 칸) — 1초에 1000번</text>
  <text x="10" y="128" fill="currentColor">tickless</text>
  <rect x="60" y="115" width="10" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="70" y="120" width="185" height="10" class="f-mut" stroke="currentColor"/>
  <rect x="255" y="115" width="8" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="263" y="120" width="397" height="10" class="f-mut" stroke="currentColor"/>
  <rect x="660" y="115" width="10" height="20" class="f-acc" stroke="currentColor"/>
  <text x="90" y="110" fill="currentColor" font-size="12">단발 타이머: 10 tick 뒤</text>
  <text x="380" y="110" fill="currentColor" font-size="12">다시: 남은 6.75 tick</text>
  <line x1="255" y1="150" x2="255" y2="170" stroke="currentColor" stroke-width="2"/>
  <polygon points="255,146 250,156 260,156" fill="currentColor"/>
  <text x="200" y="185" fill="currentColor" font-size="12">외부 IRQ @3.25 → 조기 기상</text>
  <rect x="60" y="200" width="660" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="70" y="220" fill="currentColor">조기 기상 보정: 기준 경계 0 이후 3.25 tick → 온전한 3 tick 만 step (tick = 3)</text>
  <text x="70" y="244" fill="currentColor">타이머는 남은 조각 0.75 tick 으로 재시작 → 다음 tick IRQ 가 정확히 경계 4 에 온다</text>
  <line x1="300" y1="195" x2="300" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
</svg>
```

호스트에서는 진짜로 자지 않는다. **시뮬레이션 시간** `g_now` (단위 = 타이머 클럭 1개, count) 위에 SysTick 같은 24-bit 다운 카운터 `ST` 를 두고, 미니 커널(RTOS-B 시리즈 어휘: `tcb_t`, `kernel_tick`, `sched_pick_next`)을 올린다. 커널이 센 tick 과 진짜 시간을 매번 비교할 수 있으니 드리프트를 **count 단위로 정확히** 잰다.

| 모델 SysTick 규칙 (ARMv7-M SysTick 설명 + FreeRTOS port.c:766-772 주석 기준) | 결과 |
|---|---|
| `st_start()`: ENABLE=1, VAL 이 0 이면 그 순간 LOAD 를 싣는다 | LOAD = X 로 시작하면 X count 뒤 첫 IRQ |
| 매 클럭: VAL > 0 이면 1 감소, **1→0** 순간 COUNTFLAG=1 + IRQ 펜딩 | `if (VAL==0) 남은 count = N` 규칙의 이유 (port.c:627-632) |
| VAL == 0 인 다음 클럭은 LOAD 재적재 (감소 없음) | 주기 모드 LOAD = N−1 → 주기 N |
| VAL 쓰기 = 0 으로 클리어 + COUNTFLAG 클리어 / CTRL 읽기 = COUNTFLAG 읽고 지움 | 깬 이유 판별 (port.c:705) |

설정: 타이머 48 MHz, tick 1 kHz → **N = 48000 count/tick**, 최대 억제 = ⌊0xFFFFFF / 48000⌋ = **349 tick**. ISR 실행 시간은 0 으로 둔다 (시간 보정 문제와 무관). 타이머를 멈춘 채 계산하는 시간도 본문에서는 0 — 그 보정(`ulStoppedTimerCompensation`)은 5장 끝과 연습 2 에서 따로 정직하게 다룬다.

### Part A — 다음 만료, reload, 만료 경로

- `expected_idle_ticks()` — `prvGetExpectedIdleTime`. idle 이 아닌 태스크가 READY 면 0, 아니면 `xNextTaskUnblockTime − xTickCount` (wrap 안전 뺄셈). 지연 태스크가 없으면 FreeRTOS 처럼 `portMAX_DELAY − tick`.
- `tl_reload_value(val, expected, tick_pending)` — 정지 순간의 VAL 로 "다음 경계까지 남은 count" 를 구하고, `expected` 번째 경계에서 IRQ 가 나게 할 reload. SysTick IRQ 가 이미 펜딩이면 한 tick 은 이미 지난 것.
- `tl_expiry_remainder(reload, val)` — 타이머 만료로 깼다. IRQ 이후 흐른 count 를 VAL 로 역산해 다음 경계까지 남은 count.
- `port_suppress_ticks_and_sleep(expected)` — `vPortSuppressTicksAndSleep` 의 10단계 (상한 → `cpsid` → 확인 → 정지 → reload → 시작 → `wfi` → `cpsie`/`cpsid` → 판별·재시작 → step).
- 검사: 30 tick 시나리오에서 ticked idle 과 **똑같은 스케줄**(태스크·시작 시각 해시 일치), 깨어남 31 → 4, 모든 tick 위상 오차 0.

### Part B — 조기 기상, 상한, 펜딩 tick, 중단

- `tl_early_remainder(expected, val, &complete)` — 다른 IRQ 로 깼다. 기준 경계 이후 흐른 count → 온전한 tick 수 + 남은 조각.
- `confirm_sleep_status()` — `eTaskConfirmSleepModeStatus`: 스케줄러 정지 중 깨어난 태스크(pending-ready)나 pended tick 이 있으면 **잠들지 않는다(abort)**.
- `kernel_step_tick(n)` — `vTaskStepTick`: 다음 만료를 넘는 step 은 assert, 정확히 만료까지면 마지막 1 tick 은 pended 로 넘겨 tick 처리 경로가 태스크를 깨우게 한다.
- 검사: 37.25 tick 조기 기상 → tick 37 + 조각 36000 count, 경계 정각(37.00) 기상, IRQ 와 만료 동시, 5000 tick 지연 = 349 × 14 + 114 로 15번 잠, `cpsid` 직후 tick 이 펜딩된 경우, 확인 없는 순진한 구현의 이벤트 지연 49.9 ms.

### Part C — 배터리 BLE 센서 노드, 10분 × 5 시드

온습도 2 Hz(200 µs), BLE 광고 1 Hz(1 ms), 가속도계 모션 IRQ 무작위(평균 400 ms 간격, 100 µs 처리). tick 카운터는 `0xFFFF0000` 에서 시작해 실행 중 wrap 한다. ticked / tickless / 순진한 tickless 를 같은 IRQ 열로 돌려서:

- 올바른 tickless: 모든 검사점에서 드리프트 **정확히 0**, 스케줄 해시가 ticked 와 같음, 최대 잠 349 tick.
- 순진한 구현: 드리프트 숫자(10분에 약 0.75초)와 그 이유.
- `avg_current_nA()` — 깨어남 횟수와 잔 시간으로 평균 전류를 추정해 Sleep / Stop 두 슬립 모드에서 비교.

## 2. Problem (English)

> **Tickless idle for a tick-based RTOS (simulated time).** A 24-bit down-counting timer (SysTick-like: LOAD/VAL/COUNTFLAG, IRQ on the 1→0 transition, reload on the next clock; starting with VAL = 0 loads LOAD immediately) generates the kernel tick every N counts. Implement:
>
> 1. `uint32_t expected_idle_ticks(void)` — 0 if any non-idle task is READY, else `next_unblock − tick` (wrap-safe; `portMAX_DELAY − tick` if nothing is delayed).
> 2. `uint32_t tl_reload_value(uint32_t val, uint32_t expected, bool tick_pending)` — counts until the `expected`-th tick boundary from the instant the timer was stopped with VAL = `val` (`val == 0` means a full tick remains); subtract one tick if the tick IRQ is already pending.
> 3. `uint32_t tl_expiry_remainder(uint32_t reload, uint32_t val)` / `uint32_t tl_early_remainder(uint32_t expected, uint32_t val, uint32_t *complete)` — after waking by timer expiry or by another interrupt, return the counts left until the next tick boundary (1..N); for an early wake also return the number of whole ticks that elapsed.
> 4. `void port_suppress_ticks_and_sleep(uint32_t expected)` — cap at `0xFFFFFF / N`, mask interrupts with PRIMASK, abort if `confirm_sleep_status()` says so, reprogram, WFI, let the waking ISR run, recompute, restart the timer with the remainder, then `kernel_step_tick()`.
> 5. `sleep_status_t confirm_sleep_status(void)`, `void kernel_step_tick(uint32_t n)`, `uint64_t avg_current_nA(...)`.
>
> Example (N = 48000): idle at tick 0 with VAL = 43200, next unblock at tick 100 → reload = 43200 + 99·48000. A button IRQ at 37.25 ticks → VAL = 3,012,000 at wake, `done` = 100·48000 − 3,012,000 = 1,788,000 = 37 ticks + 12000 counts → step 37, restart with the 36000-count remainder, next tick IRQ exactly at 38·48000.
>
> Constraints: zero cumulative drift over long random runs (the observable schedule must equal the ticked-idle schedule); no tick ever stepped past the next unblock time. Follow-ups: why does time drift in tickless mode? How do you tell timer expiry from an early wake? What limits the sleep length? What is lost if you skip the sleep-abort check?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "17/50 passed", exit 1 (약 0.1 s). idle 은 그냥 WFI 로 돌아서 멈추지 않는다
```

TODO 는 8개다: A1 `expected_idle_ticks`, A2 `tl_reload_value`, A3 `tl_expiry_remainder`, A4 `port_suppress_ticks_and_sleep`, B1 `tl_early_remainder`, B2 `confirm_sleep_status`, B3 `kernel_step_tick`, C1 `avg_current_nA`. SysTick 모델(`st_*`), 시뮬레이터(`sim_advance` = 깨어 있는 동안 시간 진행 + ISR, `sim_wfi` = 다음 IRQ 까지 잠), 커널(`kernel_tick_increment`, `tick_isr`, `ext_isr`, `suspend_all`/`resume_all`), idle 태스크(`idle_task_step`), 비교용 `port_suppress_naive`, 시나리오는 주어진 코드다. 시작 코드의 A4 는 tick 을 끄지 않고 그냥 `sim_wfi()` 를 부른다 — 시뮬레이션은 끝까지 돌고 tickless 검사만 FAIL 한다. 드리프트는 두 군데서 잰다: tick ISR 시각 − (커널 tick)·N, 그리고 매번 깬 직후 "다음 tick IRQ 시각 − (커널 tick + pended + 1)·N".

## 4. 힌트

<details>
<summary>힌트 1 — A1: "누가 READY 면 0" 이 먼저</summary>

idle 이 돌고 있다는 것은 보통 READY 태스크가 없다는 뜻이지만, 선점을 끈 협력형 설정에서는 높은 우선순위 태스크가 READY 인 채 idle 이 돌 수 있다 (FreeRTOS 주석 tasks.c:4008-4010). 그 다음은 `next_unblock_delta()` — `wake − tick` 을 `uint32_t` 로 빼면 wrap 이 저절로 처리된다 (RTOS-B06).

</details>

<details>
<summary>힌트 2 — A2: 기준은 "지금" 이 아니라 "다음 tick 경계"</summary>

정지 순간 VAL = 다음 tick IRQ 까지 남은 count (`left`). 첫 경계까지 `left`, 그 뒤로 N 씩. `expected` 번째 경계까지는 `left + N·(expected − 1)`. VAL 이 0 이면 방금 IRQ 가 났고 다음까지는 N 이 남았다. 펜딩 비트가 서 있으면 경계 하나는 **이미 지났고** 지금 VAL 은 그다음 경계까지의 값이다 → N 을 뺀다 (그 펜딩 IRQ 는 지운다).

</details>

<details>
<summary>힌트 3 — A3/B1: 타이머가 "기준 경계 이후 몇 count" 인지 알려 준다</summary>

조기 기상: 단발 IRQ 까지 남은 count 가 VAL 이다. reload 는 기준 경계(정지 순간 직전의 경계) 에서 `expected·N` 떨어진 곳을 겨눴으므로, `done = expected·N − VAL` 이 기준 경계 이후 흐른 count 다 — 펜딩 tick 때문에 N 을 뺀 경우에도 같은 식이 맞는다. `complete = done / N`, 조각 = `(complete + 1)·N − done`. 만료: IRQ 순간 VAL = 0, 다음 클럭에 reload 가 다시 실리고 줄어든다 → `d = (VAL ? reload − VAL + 1 : 0)`, 남은 count = N − d.

</details>

<details>
<summary>힌트 4 — A4: 왜 cpsie 를 했다가 다시 cpsid 하나</summary>

PRIMASK=1 이어도 펜딩 IRQ 는 WFI 를 끝낸다 (BM-M11). 깨고 나서 `cpsie` 로 **깨운 ISR 을 지금 실행** 시킨다 — 모션 ISR 은 세마포어를 주고(스케줄러 정지 중이라 pending-ready 로), SysTick ISR 은 `g_pended++`. 그다음 다시 `cpsid` 하고 타이머를 멈춰 계산한다. 계산 중에 들어온 IRQ 는 펜딩으로 남아 마지막 `cpsie` 에서 처리된다. 만료 경로에서 step 은 `expected − 1` — 마지막 1 tick 은 방금 돈 SysTick ISR 이 pended 로 남겼다.

</details>

<details>
<summary>힌트 5 — B3: step 이 만료를 "정확히" 맞추면</summary>

`vTaskStepTick` 은 tick 을 더하기만 하고 지연 리스트를 보지 않는다. 그래서 step 으로 `xNextTaskUnblockTime` 에 딱 도달하면 그 태스크를 깨울 기회가 없다 → 마지막 1 tick 은 `xPendedTicks++` 로 넘겨 `xTaskResumeAll` 의 `xTaskIncrementTick` 이 처리하게 한다 (tasks.c:4677-4691). 넘어서는 것은 assert.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 (호스트) | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 / RTX5 (문서 기준, 미검증) |
|---|---|---|---|
| `expected_idle_ticks()` | `prvGetExpectedIdleTime()` (tasks.c:4002-4058) | 커널이 다음 타임아웃을 `sys_clock_set_timeout(ticks, idle)` 로 드라이버에 넘김 | `osKernelSuspend()` 의 반환값 (잘 수 있는 tick) |
| `idle_task_step()` | `prvIdleTask` tickless 블록 (tasks.c:5980-6024) | idle 스레드 → `pm_system_suspend()` / `k_cpu_idle()` | 사용자 idle 스레드 (`osRtxIdleThread`) |
| `port_suppress_ticks_and_sleep()` | `portSUPPRESS_TICKS_AND_SLEEP` → `vPortSuppressTicksAndSleep()` (ARM_CM4F port.c:589-803, weak) | 타이머 드라이버 (`cortex_m_systick.c` 등) + `arch_cpu_idle()` | 사용자 코드: 타이머 재설정 + `__WFI()` |
| `confirm_sleep_status()` | `eTaskConfirmSleepModeStatus()` (tasks.c:6045-6093) | — | — |
| `kernel_step_tick(n)` | `vTaskStepTick()` (tasks.c:4665-4701), 대안 `xTaskCatchUpTicks()` (4707-4730) | `sys_clock_announce(ticks)` | `osKernelResume(sleep_ticks)` |
| `tl_early_remainder()` | port.c:731-764 | 드라이버의 `sys_clock_elapsed()` | — |
| `EXPECTED_IDLE_TIME_BEFORE_SLEEP` | `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` 기본 2, 2 미만이면 `#error` (FreeRTOS.h:2731-2737) | PM 상태의 `min-residency-us` | — |
| (없음) | `configPRE_SLEEP_PROCESSING` / `configPOST_SLEEP_PROCESSING` (port.c:669, 678), `configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING` (tasks.c:6004) | `pm_state_set()` 류 | — |
| `pending_ready` / `g_pended` | `xPendingReadyList` (tasks.c:483) / `xPendedTicks` (509) | — | — |

### Part A — idle 이 묻는 질문: "몇 tick 쉴 수 있나"

FreeRTOS 의 idle 태스크는 이 순서를 지킨다 (tasks.c:5989-6020):

```c
xExpectedIdleTime = prvGetExpectedIdleTime();                 /* 5989: 스케줄러 정지 없이 1차 — "not necessarily valid" */
if( xExpectedIdleTime >= configEXPECTED_IDLE_TIME_BEFORE_SLEEP ) {
    vTaskSuspendAll();                                        /* 5993 */
    xExpectedIdleTime = prvGetExpectedIdleTime();             /* 5999: 이제 믿을 수 있다 */
    configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING( xExpectedIdleTime );
    if( xExpectedIdleTime >= configEXPECTED_IDLE_TIME_BEFORE_SLEEP ) {
        traceLOW_POWER_IDLE_BEGIN();                          /* 6008 */
        portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime );    /* 6009 */
        traceLOW_POWER_IDLE_END();
    }
    ( void ) xTaskResumeAll();
}
```

1차 계산을 정지 없이 하는 이유는 주석 그대로 "매 idle 반복마다 정지/재개하는 것은 바람직하지 않아서" 다. 스케줄러를 정지해야 지연 리스트와 `xNextTaskUnblockTime` 이 그 사이 안 바뀐다. 정지 중에 ISR 이 태스크를 깨우면 ready list 대신 `xPendingReadyList` 로 간다 (tasks.c:5546-5551) — Part B 의 중단 경로가 이것을 본다.

`prvGetExpectedIdleTime` (tasks.c:4002-4058) 의 마지막 분기가 핵심이다:

```c
else { xReturn = xNextTaskUnblockTime; xReturn -= xTickCount; }   /* 4054-4055 */
```

앞의 분기들은 "지금 잘 수 없는" 경우 0 을 돌려준다: 현재 태스크가 idle 보다 높음, idle 우선순위 태스크가 둘 이상 READY(시간 분할 때문에 다음 tick 을 처리해야 함), idle 보다 높은 태스크가 READY(선점이 꺼진 경우). `xNextTaskUnblockTime` 은 정렬된 지연 리스트 머리의 캐시라 O(1) 이다 (RTOS-M07 — 타이머 휠이었다면 O(N) 이 되는 이유). 지연 리스트가 비면 캐시는 `portMAX_DELAY` 라서 `portMAX_DELAY − tick` 이 된다. 이 모델의 `next_unblock_delta()` 도 똑같이 했다 (테스트: 지연 없음 → `0xFFFFFFFF − 0`, wrap: tick `0xFFFFFFF0`, 만료 `0x10` → `0x20`).

**문턱 2 는 왜 최소인가.** `configEXPECTED_IDLE_TIME_BEFORE_SLEEP < 2` 는 컴파일 에러다 (FreeRTOS.h:2735-2737). reload 식이 "첫 tick 은 이미 일부 지났다 + 온전한 tick (E−1) 개" 구조이고, 펜딩 tick 이면 N 을 하나 더 빼므로 E ≥ 2 여야 reload 가 양수로 남는다. 1 tick 쉬자고 타이머를 멈추고 다시 계산하는 것은 그냥 다음 tick 에 깨는 것보다 이득이 없다 (연습 4: 진입 비용이 있으면 손익분기는 더 크다).

### Part A — reload: "다음 경계" 를 기준으로 센다

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">reload 와 조기 기상 계산 (N = 48000, expected E = 100)</text>
  <line x1="30" y1="110" x2="720" y2="110" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="60" y1="100" x2="60" y2="120"/><line x1="160" y1="100" x2="160" y2="120"/>
    <line x1="260" y1="100" x2="260" y2="120"/><line x1="560" y1="100" x2="560" y2="120"/><line x1="660" y1="100" x2="660" y2="120"/></g>
  <text x="40" y="138" fill="currentColor" font-size="12">경계 T</text>
  <text x="140" y="138" fill="currentColor" font-size="12">경계 T+1</text>
  <text x="240" y="138" fill="currentColor" font-size="12">T+2</text>
  <text x="535" y="138" fill="currentColor" font-size="12">T+37/38</text>
  <text x="640" y="138" fill="currentColor" font-size="12">T+E</text>
  <rect x="80" y="60" width="4" height="50" class="f-warn" stroke="currentColor"/>
  <text x="62" y="52" fill="currentColor" font-size="12">정지 순간 (VAL = left)</text>
  <line x1="84" y1="80" x2="160" y2="80" stroke="currentColor"/>
  <text x="96" y="76" fill="currentColor" font-size="12">left</text>
  <line x1="160" y1="80" x2="660" y2="80" stroke="currentColor" stroke-dasharray="6 3"/>
  <text x="330" y="76" fill="currentColor" font-size="12">N·(E − 1)</text>
  <text x="250" y="96" fill="currentColor" font-size="12">reload = left + N·(E−1)  (펜딩이면 − N)</text>
  <rect x="595" y="60" width="4" height="50" class="f-acc" stroke="currentColor"/>
  <text x="520" y="52" fill="currentColor" font-size="12">외부 IRQ (VAL = 남은 count)</text>
  <line x1="60" y1="175" x2="597" y2="175" stroke="currentColor"/>
  <polygon points="597,175 589,170 589,180" fill="currentColor"/>
  <text x="200" y="168" fill="currentColor" font-size="12">done = E·N − VAL  (경계 T 부터 잰다)</text>
  <line x1="597" y1="175" x2="660" y2="175" stroke="currentColor" stroke-dasharray="3 2"/>
  <text x="604" y="168" fill="currentColor" font-size="12">VAL</text>
  <rect x="30" y="195" width="690" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="42" y="216" fill="currentColor">complete = ⌊done / N⌋ → vTaskStepTick(complete)</text>
  <text x="42" y="236" fill="currentColor">조각 = (complete + 1)·N − done → LOAD, VAL=0, start, 그다음 LOAD = N − 1</text>
  <text x="42" y="256" fill="currentColor">기준이 경계 T 라서 "정지가 경계 사이 어디였나" 를 따로 기억할 필요가 없다</text>
</svg>
```

```c
uint32_t left = val ? val : g_N;                      /* port.c:627-632: 0 이면 한 tick 통째로 남음 */
uint32_t reload = left + g_N * (expected - 1u);       /* port.c:640 */
if (tick_pending) reload -= g_N;                      /* port.c:642-646: ICSR PENDSTSET 이면 지우고 N 빼기 */
```

- VAL == 0 이 "한 tick 남음" 인 이유: SysTick 은 **1→0 으로 줄어드는 순간** IRQ 를 요청하고, 0 다음 클럭에 재적재한다 (port.c:624-626 주석). VAL 0 을 읽었다면 방금 IRQ 가 났고 다음 IRQ 는 N 뒤다.
- 펜딩: `cpsid` 와 정지 사이에 경계를 지나면 SysTick IRQ 는 펜딩만 되고 실행되지 않는다. 그 경계는 이미 지났으므로 "두 번째 tick 이 진행 중" (port.c:635-639 주석) → N 을 빼고 펜딩을 지운다. 그 tick 은 나중 step 이 대신 센다. 테스트 B-5: 훅으로 `cpsid` 직후 5 count 동안 시간을 흘려 경계 1 을 넘긴 뒤 → `pend_cleared == 1`, 태스크가 정확히 `50·N` 에 깨고 위상 오차 0.
- 상한: `left ≤ N` 이고 `E ≤ ⌊0xFFFFFF/N⌋` 이면 `reload ≤ N·E ≤ 0xFFFFFF` — 24-bit LOAD 에 들어간다 (port.c:595-598, 817-818). 테스트: E = 349 는 들어가고 350 은 넘친다.

### Part A — 만료 경로: IRQ 이후 흐른 count

타이머가 단발 IRQ 를 내면 VAL = 0, 다음 클럭에 LOAD(= 아직 reload) 가 다시 실려 줄어든다. 그래서 IRQ 이후 d count 가 지났다면 VAL = reload − (d − 1) (d ≥ 1). FreeRTOS 는 이것을 한 줄로 쓴다:

```c
ulCalculatedLoadValue = ( ulTimerCountsForOneTick - 1UL ) - ( ulReloadValue - portNVIC_SYSTICK_CURRENT_VALUE_REG );   /* port.c:712 */
```

`(N − 1) − (reload − VAL) = N − (reload − VAL + 1) = N − d`. d = 0 (VAL 이 0) 이면 이 식은 언더플로하고, FreeRTOS 는 `<= ulStoppedTimerCompensation || > N` 을 걸러 `N − 1` 로 바꾼다 (port.c:718-721, 주석: "post sleep hook did something that took too long … or because the SysTick current-value register is zero"). 이 모델은 ISR 시간이 0 이라 d = 0 이 실제로 나오므로 그 경우를 정확히 `N` 으로 처리했다 — 이 레슨의 `tl_expiry_remainder` 가 port.c 와 다른 유일한 줄이다. 실칩에서는 IRQ 진입·`cpsie`·`cpsid` 만으로 d ≥ 1 이니 FreeRTOS 식이 맞다. 그리고 step 은 `expected − 1` (port.c:728): 마지막 1 tick 은 `cpsie` 순간 돈 SysTick ISR 이 `xPendedTicks` 로 남겼고 (스케줄러 정지 중, tasks.c:5064), `xTaskResumeAll` 이 처리한다 (tasks.c:4151-4171).

### Part A — `port_suppress_ticks_and_sleep` 10단계

| # | 이 레슨 | port.c (ARM_CM4F) | 왜 |
|---|---|---|---|
| ① | `expected > g_max_supp` 면 자르기 | 595-598 | 24-bit LOAD |
| ② | `port_cpsid()` | 602 `cpsid i` | `taskENTER_CRITICAL()`(BASEPRI) 은 깨워야 할 IRQ 까지 막는다 — BASEPRI 로 막힌 IRQ 는 WFI 를 못 깨운다 (BM-M11 Q2) |
| ③ | `confirm_sleep_status() == ABORT` 면 `cpsie` 후 복귀 | 608-613 | 1차/2차 계산 이후 생긴 일 (Part B) |
| ④ | `st_stop()`, VAL 읽기, 펜딩 확인 | 620-646 | 계산하는 동안 카운터가 움직이지 않게 |
| ⑤ | LOAD = reload, VAL = 0, start | 654-661 | 단발로 재시작 |
| ⑥ | `sim_wfi()` | 669-676 `dsb; wfi; isb` (PRE_SLEEP 훅이 0 을 주면 생략) | PRIMASK=1 이어도 펜딩 IRQ 가 깨운다 |
| ⑦ | `port_cpsie(); port_cpsid();` | 683, 691 | 깨운 ISR 을 지금 실행 → 다시 막고 계산 |
| ⑧ | `st_stop()` 후 `st_read_countflag()` | 702-705 | 정지할 때 CTRL 을 **읽지 않고 써야** COUNTFLAG 가 안 지워진다 |
| ⑨ | 조각으로 start 후 LOAD = N − 1 | 773-777 | 남은 조각만큼 첫 주기, 그 뒤 원래 주기 |
| ⑩ | `kernel_step_tick(complete)` → `cpsie` | 798, 801 | 커널 시간 보정 |

⑨ 의 순서가 중요하다: VAL=0 → start(그 순간 조각이 실림) → LOAD = N−1. LOAD 는 **다음 재적재** 때 쓰이므로 첫 주기는 조각, 그 뒤는 N 이다. SysTick 이 코어 클럭이 아닌 쪽(외부 기준 클럭)을 쓰면 enable 순간 바로 싣지 않으므로 FreeRTOS 는 잠깐 코어 클럭으로 바꿔 즉시 싣게 하고 되돌린다 (port.c:766-793 주석 — 이 모델의 "start 즉시 적재" 규칙이 이 동작이다).

**실제 출력 — 주기 10 tick 태스크 + 13.4 tick 모션 IRQ, 30 tick** (`./solution` Part A):

```text
[ticked idle]
  tick     : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  env      : R . . . . . . . . . R . . . . . . . . . R . . . . . . . . .
  evt      : . . . . . . . . . . . . . R . . . . . . . . . . . . . . . .
  ticked   : R w w w w w w w w w R w w R w w w w w w R w w w w w w w w w   wakeups=31
[tickless]
  tick     : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  env      : R . . . . . . . . . R . . . . . . . . . R . . . . . . . . .
  evt      : . . . . . . . . . . . . . R . . . . . . . . . . . . . . . .
  tickless : R z z z z z z z z z R z z R z z z z z z R z z z z z z z z z   wakeups=4
  sleeps=4 tick_isrs=3 err_checks=4 ticks_total=30 now/N=30
```

(CPU 행: R = 그 tick 에 태스크 실행, w = tick 때문에만 깨어남, z = 내내 잠. 끝 열 바깥의 tick 30 기상까지 포함해 ticked 는 tick 30번 + IRQ 1번 = 31, tickless 는 10, 13.4, 20, 30 의 4번.) 태스크 행 두 벌이 같고, 태스크·시작 시각 해시도 같다 (`S.hash == hash_ticked`). **tickless 는 관찰 가능한 스케줄을 바꾸면 안 된다** — 이것이 이 레슨의 가장 강한 불변식이다. tickless 쪽 tick ISR 은 3번뿐이고 전부 스케줄러 정지 중(pended)으로 처리됐다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">실제 출력으로 그린 Gantt (한 칸 = 1 tick = 20 px, 0–29)</text>
  <text x="10" y="50" fill="currentColor">env</text>
  <rect x="100" y="38" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="300" y="38" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="500" y="38" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <text x="10" y="80" fill="currentColor">evt</text>
  <rect x="360" y="68" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <text x="10" y="112" fill="currentColor">ticked</text>
  <rect x="100" y="100" width="600" height="16" class="f-warn" stroke="currentColor"/>
  <text x="560" y="96" fill="currentColor" font-size="12">31번 깸</text>
  <text x="10" y="144" fill="currentColor">tickless</text>
  <rect x="100" y="132" width="600" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="300" y="132" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="360" y="132" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="500" y="132" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="100" y="132" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <text x="560" y="128" fill="currentColor" font-size="12">4번 깸</text>
  <g stroke="currentColor"><line x1="100" y1="160" x2="700" y2="160"/>
    <line x1="100" y1="156" x2="100" y2="164"/><line x1="300" y1="156" x2="300" y2="164"/>
    <line x1="500" y1="156" x2="500" y2="164"/><line x1="700" y1="156" x2="700" y2="164"/></g>
  <text x="96" y="180" fill="currentColor" font-size="12">0</text><text x="292" y="180" fill="currentColor" font-size="12">10</text>
  <text x="492" y="180" fill="currentColor" font-size="12">20</text><text x="692" y="180" fill="currentColor" font-size="12">30</text>
  <text x="120" y="196" fill="currentColor" font-size="12">경고색 = 매 tick 깨어남, 흐린 칸 = 잠, 강조 칸 = 태스크 실행 (13 칸 = 모션 IRQ 13.4)</text>
</svg>
```

### Part B — 조기 기상: 온전한 tick 과 조각

```c
uint32_t done = expected * g_N - val;   /* port.c:755 */
*complete = done / g_N;                 /* port.c:759 */
return (*complete + 1u) * g_N - done;   /* port.c:763 */
```

**B-1 (37.25 tick 에 IRQ).** env 가 0 에서 4800 count 일한 뒤 idle: VAL = 43200, E = 100, reload = 43200 + 99·48000 = 4,795,200. 4800 + 4,795,200 = 4,800,000 = 100·N — 경계 100 을 정확히 겨눈다. IRQ 는 37·48000 + 12000 = 1,788,000 에 → 재시작 이후 1,783,200 count 지남 → VAL = 3,012,000. `done = 4,800,000 − 3,012,000 = 1,788,000` = 37 tick + 12000 → step 37, 조각 = 38·48000 − 1,788,000 = 36000. 실제 출력:

```text
  early wake @37.25: tick=37 VAL=36000 LOAD=47999 sleeps=1
```

(VAL 36000 = 방금 실린 조각, LOAD 47999 = 다음 재적재용 N−1.) 이후 env 는 정확히 `100·N` 에 시작하고 위상 오차 0.

- **B-2 (경계 정각 37.00):** `done = 37·N` → complete 37, 조각 = N (한 tick 통째로). 경계 37 은 step 이 세고 다음 IRQ 는 38. `VAL == 48000` 확인.
- **B-3 (IRQ 와 만료가 같은 순간 100.00):** 둘 다 펜딩, COUNTFLAG = 1 → 만료 경로. `cpsie` 에서 모션 ISR(pending-ready) 과 SysTick ISR(pended) 이 모두 돌고, step 99 + pended 1 = tick 100. evt(우선순위 3) 와 env 모두 100·N 에 처리.
- **조기 경로에서 complete 는 언제나 ≤ E−1:** VAL ≥ 1 이므로 `done ≤ E·N − 1`. 그래서 이 포트의 step 은 `vTaskStepTick` 의 "정확히 만료까지" 분기를 쓰지 않는다 — 그 분기는 다른 포트(전부 step 하는 RTC 기반 구현 등)를 위한 안전장치다.

### Part B — 최대 억제 상한

5000 tick 지연 하나뿐인 시스템: idle 은 349 tick 씩 자고(만료 → step 348 + tick 1), 다시 계산해서 또 잔다.

```text
  cap: sleeps=15 wakeups=15 max_sleep=349 ticks env.last_start=240000000
```

349 × 14 = 4886, 남은 114 를 한 번 더 → 15번. env 는 정확히 5000·48000 = 240,000,000 count 에 시작. 상한이 짧을수록 헛걸음 기상이 는다: 64 MHz SysTick 이면 262 ms 마다, 16-bit LPTIM 을 32768 Hz LSE 로 1024 Hz tick 에 쓰면 2047 tick ≈ 2 s 마다 (연습 1).

### Part B — 잠들기 직전의 경쟁: `eTaskConfirmSleepModeStatus`

2차 계산과 `cpsid` 사이에 인터럽트가 들어와 태스크를 깨울 수 있다. 스케줄러가 정지 중이라 그 태스크는 ready list 가 아니라 `xPendingReadyList` 에 있고, `expected` 는 이미 계산됐다. 그래서 `cpsid` **이후** 다시 확인한다 (tasks.c:6045-6093):

```c
if( listCURRENT_LIST_LENGTH( &xPendingReadyList ) != 0U )      eReturn = eAbortSleep;   /* 6058-6062 */
else if( xYieldPendings[ portGET_CORE_ID() ] != pdFALSE )      eReturn = eAbortSleep;   /* 6063-6067 */
else if( xPendedTicks != 0U )                                  eReturn = eAbortSleep;   /* 6068-6073 */
else if( listCURRENT_LIST_LENGTH( &xSuspendedTaskList ) == ( uxCurrentNumberOfTasks - uxNonApplicationTasks ) )
                                                               eReturn = eNoTasksWaitingTimeout;   /* 6076-6082 */
```

`cpsid` 이후라 이 확인과 `wfi` 사이에 끼어드는 IRQ 는 실행되지 못하고 펜딩으로 남아 WFI 를 즉시 끝낸다 — BM-M11 의 "확인 → 잠" 레이스 해법과 같은 구조다. 테스트:

- **B-6 중단 (pending-ready):** 훅이 2차 계산 직후 모션 IRQ 를 넣는다 → `aborts == 1`, `sleeps == 0`, evt 지연 0.
- **같은 상황, 확인 없는 순진한 구현:** 실제 출력 `naive (확인 없음): motion latency = 2395200 clk = 49.900 ms` — 모션 이벤트가 다음 만료(50 tick)까지 방치된다. 1 kHz tick 에서는 최악 1 tick 이던 지연이 **최대 억제 길이(349 ms)까지** 커질 수 있는 버그다.
- **B-7 중단 (pended tick):** 스케줄러 정지 중 경계를 지나 tick ISR 이 `g_pended++` → abort → `resume_all` 이 그 tick 을 처리 → env 는 여전히 정확히 50·N.
- `eNoTasksWaitingTimeout` 은 "타임아웃 없이 무기한 막힌 태스크뿐" 일 때다. ARM_CM4F 포트는 이 값을 따로 쓰지 않고 abort 만 본다 — 그래서 이 경우에도 349 tick 마다 헛걸음 기상을 한다. 사용자 정의 `vPortSuppressTicksAndSleep` 은 이때 타이머를 완전히 끄고 외부 IRQ 만 기다릴 수 있다 (`portSUPPRESS_TICKS_AND_SLEEP` 를 직접 정의하면 대체된다, portmacro.h:137-139).

### Part B — `vTaskStepTick`: 만료를 넘으면 안 된다

```c
xUpdatedTickCount = xTickCount + xTicksToJump;
configASSERT( xUpdatedTickCount <= xNextTaskUnblockTime );                 /* 4675 */
if( xUpdatedTickCount == xNextTaskUnblockTime ) {
    configASSERT( uxSchedulerSuspended != ( UBaseType_t ) 0U );
    configASSERT( xTicksToJump != ( TickType_t ) 0 );
    taskENTER_CRITICAL(); { xPendedTicks++; } taskEXIT_CRITICAL();           /* 4686-4690 */
    xTicksToJump--;
}
xTickCount += xTicksToJump;                                                 /* 4698 */
```

주석: "Note this does *not* call the tick hook function for each stepped tick". step 된 tick 동안 `vApplicationTickHook` 은 불리지 않는다 — tick hook 으로 시간을 세는 코드는 tickless 에서 틀린다 (7장). 테스트 B-8: 만료가 tick+5 일 때 step 3 → +3, step 2 → +1 과 pended 1 (`env` 아직 BLOCKED), `resume_all` → tick 105 에서 env READY. 만료를 넘는 step 9 → `step_asserts == 1`.

### Part C — 10분 × 5 시드 실제 결과

```text
  seed mode      wakeups  sleeps  drift[clk]   drift[ms]  avg(Sleep)uA  avg(Stop)uA  max_sleep
     1 ticked      600766       0           0       0.000         412.8         19.3          0
     1 tickless      3368    3366           0       0.000         401.9          4.4        349
     1 naive         3346    3339    37392566     779.012         401.9          4.4        349
     2 ticked      600752       0           0       0.000         412.8         19.3          0
     2 tickless      3355    3353           0       0.000         401.9          4.4        349
     2 naive         3346    3343    36369076     757.689         401.9          4.4        349
     3 ticked      600761       0           0       0.000         412.8         19.3          0
     3 tickless      3361    3353           0       0.000         401.9          4.4        349
     3 naive         3352    3348    35214507     733.636         401.9          4.4        349
     4 ticked      600767       0           0       0.000         412.8         19.3          0
     4 tickless      3361    3356           0       0.000         401.9          4.4        349
     4 naive         3327    3321    36209696     754.369         401.9          4.4        349
     5 ticked      600715       0           0       0.000         412.8         19.3          0
     5 tickless      3320    3315           0       0.000         401.9          4.3        349
     5 naive         3327    3325    35656841     742.851         401.9          4.3        349
```

(drift = 마지막 검사점의 "진짜 시간 − 커널 tick·N", 양수 = 커널 시계가 느림. 1 tick = 1 ms 라 drift[ms] = drift[clk]/48000.)

- **올바른 tickless:** 5 시드 모두 모든 tick ISR 과 모든 기상 직후 검사에서 오차 **0 count**, 태스크를 깨운 시각도 경계와 정확히 일치, 스케줄 해시가 ticked 와 같다. tick 카운터가 `0xFFFF0000` 에서 시작해 실행 중 wrap 했지만 문제 없다.
- **순진한 구현의 드리프트 ≈ 0.73–0.78 s / 10분** (하루면 약 1.8 분). 모션 IRQ 가 약 1500 번(600,000 ms / 평균 간격 400 ms) 조기 기상을 일으키고, 매번 조각(0 ~ N−1, 평균 N/2 = 24000 count) 을 버린다: 1500 × 24000 ≈ 3.6 × 10⁷ count — 표의 3.5–3.7 × 10⁷ 과 맞다. 커널 시계는 **IRQ 가 잦을수록** 느려진다 — "현장에서 가끔만 밀린다" 는 버그 리포트의 전형이다. 스케줄도 달라진다(해시 불일치): 주기 태스크가 진짜 시간보다 점점 늦게 깬다.
- **깨어남 600,766 → 3,368 (약 178배 감소).** 남은 기상 = env 1200 + 모션 약 1500 + 349 상한 때문에 둘로 나뉜 잠 (광고는 env 와 같은 순간이라 추가 기상이 없다).
- **평균 전류 (C1, 모델 값):** Sleep 모드(400 µA) 에서는 412.8 → 401.9 µA, **2.6 % 밖에 안 준다.** 잠잘 때 전류가 크면 1 ms 마다의 기상 비용(480 clk × 1.5 mA)이 묻힌다. Stop 모드(1.8 µA) 기준이면 19.3 → 4.4 µA, **4.4배**. tickless 의 진짜 가치는 **깊은 슬립과 함께** 나온다.
- 그런데 함정: SysTick 은 코어 클럭으로 돈다. STM32 계열의 Stop 모드에서는 코어 클럭(과 SysTick)이 멈춘다 (RM 기준, 이 레슨에서 미검증). 그래서 표의 "ticked + Stop" 은 실제로는 1 ms 마다 LPTIM 으로 깨는 가상의 구성이고, 실제 제품의 tickless 는 `vPortSuppressTicksAndSleep` 을 LPTIM/RTC 버전으로 **다시 써서**(weak 함수, port.c:589) Stop 에 들어간다. 그 구현은 연습 3 의 "자유 계수 카운터 + compare" 구조가 더 단순하다.

### 정지 구간 보정 — `ulStoppedTimerCompensation` 을 솔직하게

본문 모델은 타이머를 멈춘 채 계산하는 시간을 0 으로 뒀다. 실제로는 ④~⑤ 와 ⑧~⑨ 사이 두 구간 동안 진짜 시간은 흐르고 카운터는 멈춰 있다. FreeRTOS 는 이것을 고정값으로 뺀다:

```c
#define portMISSED_COUNTS_FACTOR    ( 94UL )                                                     /* port.c:105 */
ulStoppedTimerCompensation = portMISSED_COUNTS_FACTOR / ( configCPU_CLOCK_HZ / configSYSTICK_CLOCK_HZ );  /* 819 */
if( ulReloadValue > ulStoppedTimerCompensation ) ulReloadValue -= ulStoppedTimerCompensation;           /* 648-651 */
```

주석도 정직하다: "A fiddle factor to estimate the number of SysTick counts that would have occurred while the SysTick counter is stopped" (port.c:101-103), "using the tickless mode will inevitably result in some tiny drift of the time maintained by the kernel with respect to calendar time" (616-619). 연습 2 에서 두 정지 구간 s_pre, s_post 를 넣고 계산해 보면, 만료든 조기 기상이든 **한 번 잘 때마다의 위상 오차 = s_pre + s_post − comp** 다. comp 는 두 구간 합을 한 번에 빼는 값이다. 94 는 특정 컴파일러·최적화·플래시 대기에서 잰 CPU 사이클 수로 읽히고 (출처 미확인), 내 빌드의 실제 정지 구간과 다르면 잔여 드리프트가 남는다: 정지 40 + 50 count 에 보정 0 이면 10분(3,360번 잠) 동안 6.3 ms 느려지고, 보정 94 면 0.28 ms 빨라진다. `configPOST_SLEEP_PROCESSING` 에서 PLL 을 다시 켜는 등 오래 걸리는 일을 하면 이 오차가 커진다. 근본 해결은 **멈추지 않는 타이머** 다 (연습 3).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution       # 50/50 passed (약 0.14 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 50/50, 보고 없음
```

O2 빌드 5회 연속 50/50 을 확인했다 (Apple clang 21.0.0, load average 약 10 — 결과는 시뮬레이션 시간이라 부하와 무관). `tl_expiry_remainder` 의 d = 0 처리만 FreeRTOS 와 다르고 나머지 식은 port.c 와 한 줄씩 대응한다 (주석에 행 번호).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 조기 기상 후 타이머를 한 주기(N) 로 재시작 | 남은 조각을 버림 → 깰 때마다 평균 N/2 늦어짐. Part C: 10분에 0.75 s | `LOAD = (complete+1)·N − done` 으로 시작한 뒤 `LOAD = N−1` |
| 조기 기상인데 `expected − 1` 만큼 step | 커널 시간이 앞으로 튐 → 지연 태스크가 일찍 깸 | COUNTFLAG 로 경로를 나눈다 |
| 정지할 때 `CTRL` 을 읽고-수정-쓰기 | 읽는 순간 COUNTFLAG 가 지워져 만료를 조기 기상으로 오판 | 읽지 않고 값 쓰기 (port.c:702 주석) |
| reload 를 "지금부터 E·N" 으로 | 첫 tick 의 이미 지난 부분만큼 늦게 깸 → 위상이 밀림 | `left + N·(E−1)` (기준 = 다음 경계) |
| 펜딩된 SysTick 을 무시 | 그 tick 을 두 번 세거나(펜딩 ISR + step) 한 tick 늦게 깸 | 펜딩이면 N 빼고 펜딩 클리어 |
| 상한 없이 `expected` 를 그대로 | 24-bit 를 넘는 reload 가 잘려 엉뚱한 시각에 깸 | `min(expected, 0xFFFFFF / N)` |
| `taskENTER_CRITICAL()`(BASEPRI) 안에서 WFI | 막힌 IRQ 는 WFI 를 깨우지 못함 | `cpsid i` (PRIMASK) (BM-M11) |
| `cpsid` 뒤 확인(eTaskConfirmSleepModeStatus) 생략 | 직전에 깨어난 태스크가 최대 억제 길이까지 방치 (B-6: 49.9 ms) | pending-ready / pended tick 이면 abort |
| step 이 다음 만료에 딱 닿는데 그냥 더함 | 그 태스크를 깨울 tick 처리가 없다 → 다음 tick 까지 늦거나 놓침 | 마지막 1 tick 은 `xPendedTicks++` |
| tick hook 으로 ms 를 셈 | step 된 tick 에는 hook 이 안 불림 | `xTaskGetTickCount()` 나 RTC |
| tick 주파수가 타이머 클럭을 못 나누는데 그대로 (32768 Hz / 1000 Hz) | N = 32 로 잘려 커널 ms 가 2.4 % 빨리 감 (1시간에 86.4 s) | 1024 Hz tick, 또는 분수 누산기 (연습 1) |
| idle 태스크를 굶기는 폴링 루프 `vTaskDelay(0)` | idle 이 안 돌면 tickless 진입 자체가 없다 (RTOS-B06) | 이벤트 기반 (세마포어·알림) |

## 8. 면접 Q&A

<details>
<summary>Q1. tickless idle 은 무엇이고, 그냥 tick 주파수를 낮추면 안 되나요?</summary>

idle 일 때 다음 만료까지 tick 인터럽트를 끄고 단발 타이머 하나로 깨어난 뒤, 흐른 tick 을 한 번에 더하는 기법이다 (FreeRTOS `configUSE_TICKLESS_IDLE`, Zephyr `CONFIG_TICKLESS_KERNEL`, 문서 기준). tick 을 100 Hz 로 낮추면 기상은 줄지만 **시간 해상도와 응답 시간** 이 같이 나빠진다 — `vTaskDelay(1)` 이 10 ms, 타임아웃 오차 최대 10 ms, 같은 우선순위 시간 분할도 10 ms. tickless 는 바쁠 때는 1 ms 해상도를 유지하고, 한가할 때만 깨어남을 없앤다. 꼬리: "바쁠 때도 tick 을 없애면?" → Linux 의 `nohz_full` 처럼 실행 중 tick 도 줄이는 방식이 있지만, RTOS 에서는 시간 분할·지연 처리가 tick 에 묶여 있어 보통 idle 에서만 한다.

</details>

<details>
<summary>Q2. "몇 tick 잘 수 있나" 는 어떻게 O(1) 로 계산하나요? 소프트웨어 타이머는요?</summary>

지연 리스트가 깰 시각 순으로 정렬돼 있고 머리 값을 `xNextTaskUnblockTime` 에 캐시하므로 `xNextTaskUnblockTime − xTickCount` 한 번이다 (tasks.c:4054-4055, RTOS-M07). 소프트웨어 타이머는 별도 경로가 없다 — 타이머 데몬 태스크가 다음 만료까지의 block time 으로 큐에서 기다리므로 그 태스크도 지연 리스트에 있다 (RTOS-B12). 함정: 이벤트로 일찍 깨어난 태스크가 지연 리스트에서 빠져도 캐시는 보통 그대로(stale) 두는데, tickless 에서는 "더 일찍 깨는 손해" 를 피하려고 바로 다시 계산한다 (tasks.c:5532-5543 `prvResetNextTaskUnblockTime`). 꼬리: "타이머 휠이면?" → 다음 만료 찾기가 O(N) 이라 tickless 에 불리하다 (RTOS-M07 측정).

</details>

<details>
<summary>Q3. tickless 를 켰더니 시계가 밀립니다. 어디를 보겠습니까?</summary>

네 군데를 순서대로 본다. ① **조기 기상 때 조각을 버리는가** — 가장 큰 원인, IRQ 마다 평균 반 tick (Part C: 1500번 기상에 0.75 s). ② **타이머를 멈춘 채 계산하는 시간** — FreeRTOS 의 94 count 보정이 내 빌드와 맞는가, POST_SLEEP 훅이 오래 걸리지 않는가 (연습 2: 한 번 잘 때 오차 = s_pre + s_post − comp). ③ **N 이 정수로 안 나눠지는가** — 32768 Hz / 1000 Hz 는 tickless 와 무관하게 2.4 % (연습 1). ④ **인터럽트 금지가 1 tick 보다 긴 구간** — 펜딩 비트는 1개라 tick 두 개가 하나로 합쳐진다 (ticked 에서도 생긴다). 확인 방법: RTC(32 kHz 크리스털) 와 `xTaskGetTickCount()` 를 몇 시간 비교해 ppm 으로 보고, IRQ 빈도를 바꿔 드리프트가 따라 변하면 ① 이다. 꼬리: "ppm 을 0 으로 만들려면?" → 커널 시간을 멈추지 않는 RTC 에서 직접 유도 (연습 3).

</details>

<details>
<summary>Q4. 깼을 때 타이머 때문인지 다른 인터럽트 때문인지 어떻게 압니까? 둘이 동시면요?</summary>

SysTick 을 멈춘 뒤 COUNTFLAG 를 본다 (port.c:705) — 1→0 을 지났으면 1. 이때 CTRL 을 읽고-수정-쓰기로 멈추면 그 읽기가 COUNTFLAG 를 지우므로, 값을 그냥 써서 멈춘다 (port.c:695-702). 동시면 COUNTFLAG = 1 이므로 만료 경로를 타고, 다른 ISR 은 `cpsie` 에서 이미 돌아 태스크를 pending-ready 로 만들어 두었다 (테스트 B-3: tick 100, 두 태스크 모두 100·N 에). 꼬리: "LPTIM 이면?" → 보통 compare-match 플래그(ISR 레지스터) 를 보거나, 자유 계수 카운터라면 그냥 카운터 값으로 경과를 계산하고 플래그는 필요 없다 (연습 3).

</details>

<details>
<summary>Q5. 한 번에 얼마나 오래 잘 수 있나요? 그 상한이 문제가 되는 경우는?</summary>

`xMaximumPossibleSuppressedTicks = 0xFFFFFF / ulTimerCountsForOneTick` (port.c:818). 48 MHz·1 kHz → 349 ms, 64 MHz → 262 ms, 16 MHz → 1048 ms. 그보다 긴 idle 은 상한마다 깨어나 step 하고 다시 잔다 (B-4: 5000 ms 지연에 15번). 문제: 배터리 노드가 분 단위로 자고 싶을 때 수백 ms 마다 깨어나는 비용, 그리고 SysTick 자체가 Stop 모드에서 멈춘다는 점. 해결: SysTick 대신 LSE 기반 LPTIM(16-bit 32768 Hz 에 1024 Hz tick 이면 2 s, 분주기를 쓰면 더 길게) 이나 RTC wakeup 타이머로 `vPortSuppressTicksAndSleep` 을 다시 쓴다 (weak 함수). 꼬리: "분주기를 키우면 대가는?" → 조기 기상 때 경과를 잴 해상도가 거칠어져 조각 보정 오차가 커진다.

</details>

<details>
<summary>Q6. eTaskConfirmSleepModeStatus 를 빼면 무엇이 깨지나요? 왜 taskENTER_CRITICAL 이 아니라 cpsid 인가요?</summary>

1·2차 계산 이후 `cpsid` 전에 들어온 ISR 이 태스크를 깨웠거나(정지 중이라 `xPendingReadyList` 에 있음), tick 이 pended 됐을 수 있다. 확인 없이 자면 그 태스크는 다음 만료까지 방치된다 — B-6 실측 49.9 ms, 최악은 상한 349 ms. 확인은 반드시 `cpsid` **뒤** 여야 한다: 그 뒤에 오는 IRQ 는 실행되지 못하고 펜딩으로 남아 `wfi` 를 즉시 끝내기 때문이다 (BM-M11 의 슬립 진입 레이스와 같은 해법). `taskENTER_CRITICAL()` 은 Cortex-M3/M4 포트에서 BASEPRI 로 막는데, BASEPRI 로 막힌 IRQ 는 WFI 를 깨우지 못한다 — port.c:600-601 주석이 바로 이것이다 ("will mask interrupts that should exit sleep mode"). 꼬리: "그럼 configMAX_SYSCALL_INTERRUPT_PRIORITY 위의 IRQ 는?" → PRIMASK 는 그것들도 막지만 펜딩은 되므로 WFI 는 깨고, `cpsie` 직후 바로 실행된다. 그 사이 지연(계산 몇 µs)이 그 IRQ 의 지연에 더해진다.

</details>

<details>
<summary>Q7. (staff) 이 노드의 배터리 수명을 더 늘리라면, tickless 다음에 무엇을 바꾸겠습니까?</summary>

숫자부터 본다: Sleep 400 µA 기준 tickless 는 2.6 % 만 줄였다 (Part C). 잠잘 때 전류가 지배하므로 ① **슬립 모드를 Stop 으로** — 그러려면 SysTick 대신 LPTIM/RTC 로 포트를 다시 쓰고, `configPRE_SLEEP_PROCESSING` 에서 Stop 진입, `POST` 에서 클럭 복구 (복구 시간만큼 정지 구간이 길어지니 보정을 다시 잰다). 이것으로 19.3 → 4.4 µA 급 차이가 난다. ② **기상 횟수 줄이기** — 주기 태스크들의 위상을 맞춰 한 번 깰 때 같이 처리(timer coalescing), 폴링을 IRQ 로, 센서 FIFO 로 모아 읽기. ③ **활성 시간 줄이기** — 광고 1 ms 가 평균의 큰 몫이면 라디오 설정이 1순위. ④ `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` 를 Stop 복귀 비용에 맞춰 올리기 (연습 4: 진입 비용 100 µs 면 12 tick 미만은 그냥 Sleep). 꼬리: "Stop 복귀 지연 때문에 실시간 마감을 놓치면?" → Zephyr PM 의 `exit-latency-us` 처럼 다음 마감까지 남은 시간이 복귀 지연보다 짧으면 얕은 모드를 고른다 (BM-M11 Part C).

</details>

<details>
<summary>Q8. (staff) 이 레슨의 보정이 틀리는 경우는? 드리프트 0 을 믿어도 되나요?</summary>

드리프트 0 은 **모델의 가정** 위에서만 정확하다. 틀리는 경우: ① 정지 구간이 0 이 아니고 보정값이 그것과 다를 때 (연습 2), ② POST_SLEEP 처리나 다른 ISR 때문에 깬 뒤 정지까지 1 tick 이상 걸릴 때 — FreeRTOS 는 `N − 1` 로 재시작하고 그 tick 은 잃는다 (port.c:718-721), ③ 인터럽트 금지가 길어 SysTick 펜딩 두 개가 하나로 합쳐질 때, ④ SysTick 클럭이 슬립 중 바뀌는 구성 (외부 기준 클럭, Stop 진입 후 HSI 로 복귀) 에서 "1 count = 같은 시간" 가정이 깨질 때, ⑤ 디버거로 코어를 멈출 때 SysTick 도 같이 멈추는 구성 (칩·DBGMCU 설정마다 다름, 미검증) — 벽시계는 흐르는데 커널 시간은 멈춘다. ⑥ 이 모델의 SysTick 은 "enable 즉시 적재" 규칙을 가정했다 — 실제 칩의 사이클 단위 동작은 데이터시트와 로직 분석기로 확인해야 하고, 여기서는 확인하지 않았다. 꼬리: "그럼 어떻게 검증하나?" → Q9.

</details>

<details>
<summary>Q9. 실제 보드에서 tickless 가 맞게 동작하는지 어떻게 확인하나요?</summary>

① **시간**: 32 kHz RTC 와 `xTaskGetTickCount()` 를 몇 시간 같이 기록해 ppm 을 본다. 같은 실험을 외부 IRQ 빈도를 바꿔 반복 — 드리프트가 IRQ 수에 비례하면 조각 처리 버그다. ② **위상**: tick ISR 에서 GPIO 토글 + 로직 분석기로 tick 간격이 정확히 N 의 배수인지, 깬 뒤 첫 tick 이 원래 격자에 오는지. ③ **스케줄**: 이 레슨의 해시처럼 "tickless 끄고 켠 두 빌드에서 태스크 실행 순서·시각이 같은가" 를 트레이스로 비교 (`traceLOW_POWER_IDLE_BEGIN/END`, tasks.c:6008-6010, RTOS-A06 의 트레이스 포맷). ④ **전력**: 전류 프로파일러(PPK2 류)로 평균 전류와 기상 횟수. ⑤ **경계 케이스 주입**: 슬립 직전 IRQ, 만료와 같은 순간의 IRQ, 상한 근처 지연 — 이 레슨 Part B 의 시나리오를 HIL 로. 꼬리: "SysTick 이 Stop 에서 멈추는지 어떻게 확인?" → Stop 에서 1 s 자고 깬 뒤 VAL 이 변했는지 본다 (미검증).

</details>

## 9. 추가 연습

### 연습 1 — 클럭 설정: N, 최대 억제, 나누어떨어지지 않는 tick

`tick_cfg(timer_hz, tick_hz, width_bits, &N, &max_supp)` 로 64 / 48 / 16 MHz SysTick(24-bit) 과 16-bit LPTIM(32768 Hz, 1024 Hz tick) 의 값을 구하라. 그리고 32768 Hz 타이머에 1000 Hz tick 을 쓰면 진짜 1시간 동안 커널이 몇 tick 을 세는지, 분수 누산기(Bresenham)로 고치면 어떻게 되는지 보여라.

<details>
<summary>답 보기</summary>

| 타이머 | N | 최대 억제 |
|---|---|---|
| SysTick 64 MHz | 64000 | 262 tick |
| SysTick 48 MHz (본문) | 48000 | 349 |
| SysTick 16 MHz | 16000 | 1048 |
| LPTIM 16-bit 32768 Hz @1024 Hz | 32 | 2047 |

32768 / 1000 = 32.768 → N = 32 로 잘려 tick 이 0.9765625 ms 가 된다. 실제 출력 `32768/1000 → N=32, 1 시간에 3686400 tick (+86400, 2.40 %)` — 커널의 "1 ms" 가 2.4 % 빠르다. FreeRTOS 의 `ulTimerCountsForOneTick = configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ` 도 정수 나눗셈이다 (port.c:817). 고침: tick 을 1024 Hz 로 하거나, tick 마다 32 또는 33 count 를 쓰는 누산기 — `acc += 768; if (acc >= 1000) { acc -= 1000; 33 } else 32`. 1000 tick = 정확히 32768 count, 어느 순간이든 경계 오차 1 count 미만, 1시간 = 3,600,000 tick.

</details>

### 연습 2 — 정지 구간 보정의 법칙

본문 SysTick 규칙으로 잠 한 번을 계산하는 `one_sleep(N, v0, E, ext_at, s_pre, s_post, comp, &counted)` 를 작성하라: 정지 구간 s_pre / s_post 동안 진짜 시간은 흐르고 카운터는 멈춘다. 반환값 = 다음 tick IRQ 시각 − 그 tick 의 이상적 경계. 만료·조기 기상, 여러 v0 에서 오차가 무엇이 되는지 보여라.

<details>
<summary>답 보기</summary>

두 경로 모두 **오차 = s_pre + s_post − comp** (27개 조합 × 2경로 검사 통과). 만료 경로: IRQ 가 (s_pre + reload − comp) 에, 재시작이 s_post 뒤 → 두 정지가 그대로 더해지고 comp 만큼 당겨진다. 조기 기상: 카운터 기반 `done` 은 s_pre 를 모르고 comp 만큼 더 센 값이라 추정 오차가 comp − s_pre, 여기에 재시작 지연 s_post 가 더해져 같은 식이 된다. 그래서 FreeRTOS 의 보정 하나(94 / (CPU/SysTick))는 **두 정지 구간의 합** 을 겨냥한 값으로 읽힌다. 실제 출력: `정지 40+50 count: 보정 없음 302400 count = 6.300 ms 느려짐 / 보정 94 → -13440 count = -0.280 ms 빨라짐 (10 분)`. 정지 구간은 빌드마다 달라서 고정 fudge 로는 0 이 안 된다.

</details>

### 연습 3 — 자유 계수 RTC + compare: 멈추지 않으면 보정도 없다

nRF52 RTC 같은 24-bit 자유 계수 카운터(32768 Hz) 와 compare 레지스터 하나로 tickless 를 만들어라. 커널은 "마지막으로 알린 tick 경계의 카운터 값" `last` 만 기억한다. `rtc_set_timeout(E)` = `(last + E·N) mod 2^24` (상한: 반 바퀴), `rtc_announce(cnt)` = 경과 `(cnt − last) mod 2^24` 를 N 으로 나눈 온전한 tick 만 알리고 `last` 를 그만큼 전진. 20만 번 무작위(만료/조기) 잠, 카운터 wrap 포함으로 "커널 tick = 진짜 시간 / N" 이 항상 성립하는지 확인하라.

<details>
<summary>답 보기</summary>

```c
uint32_t elapsed = (cnt - k->last) & RTC_MASK;
uint32_t dt = elapsed / k->N;
k->ticks += dt;
k->last = (k->last + dt * k->N) & RTC_MASK;   /* 조각은 카운터에 남아 있다 — 버릴 게 없다 */
```

실제 출력 `200000 회 잠, 카운터 wrap 42272 번, 커널 tick=22163115273, 진짜 시간/N=22163115273`. 카운터가 멈추지 않으니 정지 보정이 필요 없고, 조각은 `last` 와 `cnt` 의 차이로 자연히 이월된다. 상한을 반 바퀴로 둔 이유: 경과가 2^24 을 넘으면 mod 연산으로 구분할 수 없다. Zephyr 의 nRF RTC 드라이버가 `sys_clock_set_timeout` / `sys_clock_announce` 로 이 구조라고 알려져 있다 (문서 기준, 미검증). 실칩 함정(미검증): compare 를 현재 카운터에 너무 가깝게 쓰면 놓칠 수 있어 최소 거리 규칙이 있다 (nRF52 RTC 는 데이터시트상 2 count 이상).

</details>

### 연습 4 — `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` 손익분기

같은 Sleep 모드에서 k tick 쉴 때 ticked idle 은 k 번 깨고(번당 `wake_clk`), tickless 는 진입·복귀 계산 `ovh_clk` + 1번 깬다. tickless 가 엄격히 이득인 최소 k 를 구하라 (wake 480 clk, ovh 0 / 960 / 4800).

<details>
<summary>답 보기</summary>

`k·wake > ovh + wake` → k > 1 + ovh/wake. 실제 출력 `ovh 0/960/4800 clk → 최소 2 / 4 / 12 tick`. 진입 비용이 0 이어도 최소 2 — FreeRTOS 가 문턱 2 미만을 `#error` 로 막는 것과 같은 결론이다 (FreeRTOS.h:2735-2737). Stop 복귀 + PLL 재설정이 100 µs (48 MHz 에서 4800 clk) 라면 12 tick 미만의 idle 은 tickless/Stop 보다 그냥 tick 을 받는 편이 싸다 — 문턱을 올리거나 `configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING` 에서 짧은 idle 을 0 으로 거른다. Stop 의 낮은 전류까지 넣으면 손익분기는 다시 짧아지므로, 실제 값은 측정한 전류·복귀 시간으로 계산한다 (BM-M11 의 break-even 과 같은 계산).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 16/16, 보고 없음
```

## 10. 핵심 정리

- tickless = idle 에서 `xNextTaskUnblockTime − xTickCount` 만큼 tick 을 끄고 단발 타이머로 깨어나 `vTaskStepTick` 으로 보정. 문턱 ≥ 2, 상한 = `0xFFFFFF / N` (48 MHz·1 kHz → 349).
- reload 의 기준은 **다음 tick 경계**: `left + N·(E−1)`, VAL 0 이면 left = N, SysTick 펜딩이면 −N.
- 깬 뒤 COUNTFLAG 로 경로를 나눈다: 만료 → step E−1 (+ tick ISR 1), 남은 count = N − d / 조기 → `done = E·N − VAL`, step ⌊done/N⌋, **조각 = (c+1)·N − done 으로 재시작**. 조각을 버리면 IRQ 마다 평균 N/2 드리프트 (10분 0.75 s).
- `cpsid i` (BASEPRI 아님) → `eTaskConfirmSleepModeStatus` (pending-ready, yield, pended tick 이면 abort) → `wfi` → `cpsie`(깨운 ISR) → `cpsid` → 계산.
- step 은 다음 만료를 넘으면 안 되고, 정확히 닿으면 마지막 1 tick 은 pended 로 — 태스크를 깨우는 것은 tick 처리 경로다. step 된 tick 에는 tick hook 이 안 불린다.
- 올바른 tickless 는 관찰 가능한 스케줄을 바꾸지 않는다 (ticked 와 해시 동일) — 이것이 최고의 테스트 불변식.
- 정지 구간 오차 = s_pre + s_post − comp (FreeRTOS 의 94 count 는 fudge). 근본 해결은 멈추지 않는 RTC + compare.
- 전력 이득은 깊은 슬립과 함께 온다: Sleep 400 µA 에서는 2.6 %, Stop 1.8 µA 에서는 4.4배 (모델 값) → SysTick 대신 LPTIM/RTC 포트.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `portable/GCC/ARM_CM4F/port.c` — `portMAX_24_BIT_NUMBER` 96, `portMISSED_COUNTS_FACTOR` 101-105, 변수 165-184, `vPortSuppressTicksAndSleep` 589-803 (상한 595, `cpsid` 602, 확인 608, 정지 620, VAL 627-632, reload 640, 펜딩 642-646, 보정 648-651, 시작 654-661, PRE/POST 669/678, `wfi` 674, COUNTFLAG 705, 만료 712-728, 조기 737-763, 재시작 773-793, step 798), `vPortSetupTimerInterrupt` 811-830; `portmacro.h` 137-139; `tasks.c` — `xPendingReadyList` 483, `xPendedTicks` 509, `prvGetExpectedIdleTime` 4002-4058, `xTaskResumeAll` 4064-4171, `vTaskStepTick` 4665-4701, `xTaskCatchUpTicks` 4707-4730, `xTaskIncrementTick` 4831 (정지 중 5064), `xTaskRemoveFromEventList` 5527-5551, idle tickless 블록 5980-6024, `eTaskConfirmSleepModeStatus` 6045-6093; `include/FreeRTOS.h` 652-659 (`traceLOW_POWER_IDLE_*`), 2727-2749 (tickless 설정 기본값) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — Low Power Support / Tickless Idle Mode — <https://www.freertos.org/low-power-tickless-rtos.html>
- ARMv7-M Architecture Reference Manual (DDI0403) B3.3 The system timer, SysTick (SYST_CSR COUNTFLAG, SYST_RVR, SYST_CVR), B1.5.19 WFI — <https://developer.arm.com/documentation/ddi0403/latest/>
- Zephyr — Kernel Timing / Clocks (`sys_clock_set_timeout`, `sys_clock_announce`, `CONFIG_TICKLESS_KERNEL`), Power Management (문서 기준, 미검증) — <https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html>
- CMSIS-RTOS2 / RTX5 — Tick-less Low-Power Operation (`osKernelSuspend`, `osKernelResume`) (문서 기준, 미검증) — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html>
- 관련 레슨: BM-M11 (WFI/WFE, 슬립 진입 레이스, PRIMASK vs BASEPRI, break-even), RTOS-B06 (delay_until, tick wrap), RTOS-M07 (지연 리스트 vs 타이머 휠, `xNextTaskUnblockTime`), RTOS-B12 (타이머 데몬), RTOS-B08 / M02 (ISR → 태스크, FromISR), RTOS-M13 (FreeRTOS 내부), RTOS-A06 (트레이싱), RTOS-A10 (시스템 설계)
