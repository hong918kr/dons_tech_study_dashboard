---
id: RTOS-A10
time: 120분
updated: 2026-11-11
---
# RTOS-A10 RTOS 시스템 설계: 모터 제어 + 통신 + 로깅

## 학습 목표

- 10 kHz 전류 루프, 1 kHz 속도 루프, CAN 명령, 플래시 로깅, 워치독이 있는 컨트롤러를 **ISR / 태스크로 나누고**, 각 경계(전류 루프는 ISR, 속도 루프는 태스크, 브리지 SWI)를 숫자로 정당화할 수 있다.
- NVIC 우선순위와 FreeRTOS 우선순위를 **한 줄로 세워** 확장 RTA(블로킹 B, 지터 J, ISR 간섭)를 돌리고, 지터가 ISR → SWI → 태스크로 **전파**되는 것을 계산할 수 있다.
- CAN 큐 깊이를 "도착 곡선 vs 서비스 하한" 으로, 로그 링 크기를 "최악 지우기 시간 동안의 도착 수 + 지속 가능성 조건" 으로 **유도**하고, 시뮬레이터로 관측값이 분석 안에 드는지 확인할 수 있다.
- 미니 커널에 핵심 경로(ISR → 알림, ISR → 큐, 로그 링 + 최하위 flush, 생존 비트마스크 워치독)를 구현하고, 나쁜 설계 4가지(임계 구역 안 플래시 대기, 얕은 큐, 작은 링, tick 훅 킥)가 **어떻게 깨지는지** 재현할 수 있다.
- 면접에서 설계 문제를 "요구사항 → 타이밍 예산 → 분할 → 우선순위 → IPC → 자원·스택 → 검증 → 실패 모드" 순서로 말하고, 조건이 바뀌면 무엇을 바꿀지 답할 수 있다.

## 1. 문제

"BLDC 모터 컨트롤러 펌웨어를 RTOS 로 설계해 보세요. 전류 루프 10 kHz, 속도 루프 1 kHz, CAN 으로 명령을 받고, 플래시에 로그를 남기고, 워치독도 있어야 합니다." — 시니어/스태프 펌웨어 면접(로보틱스, 드론, EV, 방산)의 단골 설계 문제다. 면접관이 보는 것은 정답 하나가 아니라 **결정마다 숫자와 이유가 붙어 있는지**, 그리고 "그 결정이 무엇을 놓치나, 어떻게 확인하나" 에 답할 수 있는지다.

이 레슨은 그 설계를 끝까지 해 본다. 지금까지의 RTOS 레슨이 부품이었다면(RTOS-M01 알림, M02 ISR 지연 처리, B11/M06 큐, M08 스택, M11 RTA, A04 확장 RTA) 여기서는 그것들을 **하나의 시스템** 으로 묶는다. 하드웨어 가정: STM32G4 급 Cortex-M4F 단일 코어, FreeRTOS(우선순위 클수록 급함), NVIC 우선순위 비트 4개(작을수록 급함), `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 5, tick 1 kHz. 시간 단위는 µs. **C(실행 시간) 값은 모두 추정치** 다 — 실제 보드에서는 RTOS-A03 방식으로 측정해 바꿔 넣는다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">시스템 분할 — 위: ISR (NVIC, 작을수록 급함) / 아래: 태스크 (FreeRTOS, 클수록 급함)</text>
  <rect x="10" y="30" width="170" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="48" fill="currentColor" font-weight="bold">cur_isr  NVIC 1</text>
  <text x="20" y="64" fill="currentColor">10 kHz FOC, C 25 / D 60</text>
  <text x="20" y="78" fill="currentColor" font-size="11">커널 API 금지 → SWI 펜딩</text>
  <path d="M 180 57 L 200 57" stroke="currentColor" fill="none"/>
  <polygon points="200,57 192,53 192,61" fill="currentColor"/>
  <rect x="200" y="30" width="160" height="54" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="210" y="48" fill="currentColor" font-weight="bold">bridge_swi  NVIC 6</text>
  <text x="210" y="64" fill="currentColor">C 2, 10번째(1 ms)마다</text>
  <text x="210" y="78" fill="currentColor" font-size="11">NotifyGiveFromISR</text>
  <rect x="380" y="30" width="160" height="54" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="390" y="48" fill="currentColor" font-weight="bold">can_isr  NVIC 5</text>
  <text x="390" y="64" fill="currentColor">C 4, 최소 간격 111</text>
  <text x="390" y="78" fill="currentColor" font-size="11">HW FIFO 3 → 큐</text>
  <rect x="560" y="30" width="170" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="570" y="48" fill="currentColor" font-weight="bold">SysTick / PendSV  15</text>
  <text x="570" y="64" fill="currentColor">tick 1 kHz, C 3</text>
  <line x1="10" y1="100" x2="730" y2="100" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="120" y="115" fill="currentColor" font-size="11">↑ NVIC 5 이상(숫자)은 taskENTER_CRITICAL (BASEPRI = 5) 동안 가려진다 · NVIC 1 은 절대 안 가려진다</text>
  <rect x="10" y="150" width="150" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="168" fill="currentColor" font-weight="bold">wdg_mon  prio 5</text>
  <text x="20" y="184" fill="currentColor">20 ms, C 10</text>
  <text x="20" y="198" fill="currentColor" font-size="11">생존 비트 다 차면 킥</text>
  <rect x="180" y="150" width="150" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="190" y="168" fill="currentColor" font-weight="bold">speed  prio 4</text>
  <text x="190" y="184" fill="currentColor">1 kHz, C 150 / D 1000</text>
  <text x="190" y="198" fill="currentColor" font-size="11">ulTaskNotifyTake</text>
  <rect x="350" y="150" width="150" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="360" y="168" fill="currentColor" font-weight="bold">can_cmd  prio 3</text>
  <text x="360" y="184" fill="currentColor">프레임당 C 30</text>
  <text x="360" y="198" fill="currentColor" font-size="11">xQueueReceive 10 ms</text>
  <rect x="520" y="150" width="150" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="530" y="168" fill="currentColor" font-weight="bold">log_flush  prio 1</text>
  <text x="530" y="184" fill="currentColor">배경, 1 tick 폴링</text>
  <text x="530" y="198" fill="currentColor" font-size="11">임계 구역 4 µs 조각</text>
  <path d="M 280 84 L 255 150" stroke="currentColor" fill="none"/>
  <polygon points="255,150 252,139 261,141" fill="currentColor"/>
  <text x="200" y="138" fill="currentColor" font-size="11">알림</text>
  <path d="M 460 84 L 425 150" stroke="currentColor" fill="none"/>
  <polygon points="425,150 423,139 432,142" fill="currentColor"/>
  <text x="446" y="138" fill="currentColor" font-size="11">큐 깊이 3</text>
  <rect x="250" y="250" width="250" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="262" y="270" fill="currentColor" font-weight="bold">로그 링 224 레코드 (3.5 KB)</text>
  <text x="262" y="288" fill="currentColor" font-size="11">생산자는 절대 안 기다림: 가득 차면 버리고 센다</text>
  <path d="M 255 204 L 300 250" stroke="currentColor" fill="none"/>
  <polygon points="300,250 289,245 296,238" fill="currentColor"/>
  <path d="M 425 204 L 400 250" stroke="currentColor" fill="none"/>
  <polygon points="400,250 399,239 408,242" fill="currentColor"/>
  <path d="M 500 275 L 595 204" stroke="currentColor" fill="none"/>
  <polygon points="595,204 593,215 586,209" fill="currentColor"/>
  <rect x="560" y="250" width="170" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="570" y="270" fill="currentColor" font-weight="bold">SPI NOR 플래시</text>
  <text x="570" y="288" fill="currentColor" font-size="11">쓰기 ≤3 ms · 지우기 ≤400 ms</text>
  <path d="M 670 204 L 660 250" stroke="currentColor" fill="none"/>
  <polygon points="660,250 657,239 666,241" fill="currentColor"/>
  <rect x="10" y="250" width="200" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="270" fill="currentColor" font-weight="bold">IWDG 100 ms</text>
  <text x="20" y="288" fill="currentColor" font-size="11">킥 = 모니터만 (tick 훅 아님)</text>
  <path d="M 85 204 L 85 250" stroke="currentColor" fill="none"/>
  <polygon points="85,250 80,240 90,240" fill="currentColor"/>
  <text x="10" y="330" fill="currentColor">체크인: speed (매 작업) · can_cmd (프레임마다 + 10 ms 타임아웃) · log_flush (루프마다 + 지우기 폴링마다)</text>
  <text x="10" y="350" fill="currentColor">전류 루프는 RTOS 와 무관하게 정시 — 그래서 "속도 태스크가 죽어도 모터는 돈다" 가 별도 실패 모드다 (Q7).</text>
  <text x="10" y="370" fill="currentColor">분석 U (주기 개체) = 44.15 %, 시뮬레이션 1 s 동안 idle 58 %.</text>
</svg>
```

### Part A — 설계 숫자: 블로킹, RTA, 큐 깊이, 로그 링, 스택

설계 표는 주어진다 (`build_design`): 개체마다 `C, T, D, J` 와 **그 개체 안의 가장 긴 커널 임계 구역** `crit` (log push 2 µs, flush 조각 4 µs, FromISR 내부 1 µs, tick 2 µs).

- **TODO 1** `blocking_term(s, i)` — `B_i`. BASEPRI 위 ISR 은 0, BASEPRI 에 가려지는 ISR 은 "모든 태스크 + 덜 급한 커널 ISR" 의 crit 최댓값, 태스크는 "더 낮은 태스크" 의 crit 최댓값.
- **TODO 2** `rta(s, i)` — `w = C + B + Σ_{hp} ⌈(w + J_j)/T_j⌉·C_j`, `R = w + J_i` (RTOS-A04 확장 RTA 축약). ISR 과 태스크를 한 축(`level`)에 세운다. 주어진 `propagate_jitter` 가 `J_bridge = R_cur`, `J_speed = R_bridge` 를 채운다.
- **TODO 3** `can_backlog(s, consumer, n, a, c, limit)` — 16 프레임 버스트(111 µs 간격)가 들어올 때 `can_cmd` 앞에 쌓일 수 있는 최대 개수와 버스트 처리 지연 상한.
- **TODO 4** `log_ring_needed(...)` — 섹터 지우기(최대 400 ms) 동안 쌓일 레코드 수. 섹터 한 바퀴 동안의 도착이 섹터 용량보다 많으면 **지속 불가** 로 0.
- 스택 예산은 RTOS-M08 의 방법(경로 + 문맥 프레임 + 여유 25 %/최소 64 B)을 주어진 코드로 계산한다.

### Part B — 미니 커널로 핵심 경로 구현

1 µs 스텝 결정적 시뮬레이터(ISR 중첩, BASEPRI 마스킹, NVIC 펜딩 비트, tick, 우선순위 선점)는 주어진다. 직접 채우는 것은 IPC 와 워치독이다.

- **TODO 5** `notify_give_from_isr` / `notify_take` — 카운팅 알림. **NVIC 가 `NVIC_SYSCALL` 보다 급한 ISR 에서 부르면 assert** (FreeRTOS `vPortValidateInterruptPriority`).
- **TODO 6** `queue_send_from_isr` / `queue_try_receive` — 가득 차면 버리고 센다(ISR 은 못 기다린다), 기다리던 `can_cmd` 를 깨운다.
- **TODO 7** `log_push` / `log_pop` — 레코드 링. 생산자(속도 루프, can_cmd)는 **절대 기다리지 않는다.**
- **TODO 8** `wdg_checkin` / `wdg_monitor` — 생존 비트마스크가 다 찼을 때만 킥, 빠진 비트를 돌려준다.
- 검사: 1 s 동안 전류 루프 10 000 회 무결, 속도 루프 1000 작업 미스 0, 관측 응답 ≤ RTA, CAN 304 프레임 유실 0 + 큐 최대 ≤ 분석 깊이, 로그 유실 0 + 링 최대 ≤ 분석, 섹터 지우기 2번, 레코드 보존, 리셋 없음.

### Part C — 실패 모드를 재현한다

| 시나리오 | 바꾼 것 | 기대 |
|---|---|---|
| naive flush | 플래시 드라이버가 지우기·쓰기 완료를 `taskENTER_CRITICAL` 안에서 폴링 | 속도 루프 미스, CAN HW FIFO 오버런, tick 유실, 워치독 리셋. **전류 루프만 무사** |
| 얕은 큐 | CAN 큐 깊이 1 | ISR 은 정시인데 소프트웨어 큐에서 유실 |
| 작은 링 | 로그 링 = 필요량 / 2 | 로그만 잃고 제어는 멀쩡 |
| 태스크 정지 | speed / can_cmd / log_flush 가 300.3 ms 에 영원히 블록, speed 폭주 루프 | 상한 `P_mon + R_mon + W` 안에 리셋, 빠진 비트로 범인 식별 |
| 나쁜 워치독 | SysTick 훅에서 무조건 킥 | 속도 루프가 죽어도 리셋 없음 |
| 금지된 호출 | 전류 ISR(NVIC 1)에서 `NotifyGiveFromISR` 직접 | assert 5회 / 5 ms, 알림 안 감 |

## 2. Problem (English)

> **Design a single-core RTOS architecture for a BLDC motor controller** (Cortex-M4F, FreeRTOS, 4 NVIC priority bits, `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 5, 1 kHz tick):
> a 10 kHz current loop (C = 25 µs, must finish within 60 µs of the ADC trigger), a 1 kHz speed loop (C = 150 µs, D = 1 ms), CAN commands at 1 Mbit/s arriving in bursts of up to 16 back-to-back frames (hardware RX FIFO depth 3), logging to an external SPI NOR flash (page program ≤ 3 ms, 4 KB sector erase ≤ 400 ms), and a 100 ms independent watchdog.
>
> Part A — justify the partition and priorities, then implement:
> 1. `uint32_t blocking_term(const sys_t *s, int i)` — worst blocking from kernel critical sections (BASEPRI).
> 2. `rta_t rta(const sys_t *s, int i)` — fixed-priority RTA with blocking and release jitter over a unified ISR+task priority order.
> 3. `backlog_t can_backlog(const sys_t *s, int consumer, uint32_t n, uint32_t a, uint32_t c, uint32_t limit)` — queue depth and burst latency bound from an arrival curve and a leftover-service lower bound.
> 4. `uint32_t log_ring_needed(const producer_t *p, int np, uint32_t stall_us, uint32_t rec_per_page, uint32_t cycle_us, uint32_t sector_recs)` — RAM ring size to ride through a sector erase; 0 if the log rate is not sustainable at worst-case erase time.
>
> Part B — in the given 1 µs-step mini-kernel simulator implement the critical path: ISR → task notification (with an interrupt-priority guard), ISR → copy queue (drop and count when full), a never-blocking log ring drained by the lowest-priority flush task, and a task-alive-bitmask watchdog that kicks only when every monitored task checked in.
>
> Examples: `R_cur = 25`, `R_can_isr = 33`, `J_bridge = 25 → R_bridge = 60`, `J_speed = 60 → R_speed = 316 ≤ 1000`; CAN backlog depth 3, burst latency ≤ 1950 µs; log ring 218 records. A stalled task must cause a reset within `P_mon + R_mon + W_iwdg` = 120 048 µs.
>
> Follow-ups: current loop in an ISR or a task? How does an ISR above the syscall priority wake a task? Where do you kick the watchdog? What happens to the motor between a task stall and the reset? When is this analysis wrong?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter        # 12/48 passed, exit 1
./starter -v      # 분석 표 + 시나리오별 통계
./starter -t      # 정상 실행 50.000–50.620 ms 구간을 rtos-trace v1 (RTOS-A06 포맷) 으로
```

처음에는 TODO 8개가 전부 스텁이다. 알림이 안 가니 속도 태스크는 영원히 블록하고, 아무도 체크인을 못 하니 **100 ms 에 워치독이 문다** (`bite 1@100000 missing 0x7`) — 시뮬레이터가 거기서 멈추므로 starter 는 멈추지 않고 FAIL 로 끝난다. 단일 스레드·결정적 프로그램이라 `-pthread`/TSan 은 필요 없다. 실행 시간 약 0.1 s.

주어진 코드 지도: `build_design` (설계 표), `propagate_jitter`, `ctx_bytes`/`with_margin` (M08 스택 방법), `served_lb`/`arrivals` (도우미), 커널 (`pend`, `isr_dispatch`, `isr_end`, `kernel_tick`, `schedule`, `run_thread_1us`), 태스크 프로그램 `prog_wdg/speed/can/log` (세그먼트 단위 상태 기계: `seg(c, µs, crit)` 하나가 "그만큼 실행, crit = BASEPRI 올린 채"), 고장 주입 `stall_here`.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: "누가 나를 가릴 수 있나" 를 세 경우로</summary>

`taskENTER_CRITICAL()` 은 Cortex-M4F 포트에서 BASEPRI 를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 로 올린다. 그동안 NVIC 5 이상(숫자)인 ISR 과 PendSV(=문맥 전환)가 막힌다. 그러니 ① NVIC 1 은 아무도 못 막는다 → 0. ② 커널 ISR 은 어떤 태스크의 임계 구역에도, 더 덜 급한 커널 ISR 의 FromISR 내부 임계 구역에도 막힌다. ③ 태스크는 자기보다 낮은 태스크의 임계 구역에만 막힌다 (높은 태스크의 임계 구역은 이미 간섭 항에 있다). 합이 아니라 최댓값 — 단일 코어에서 열린 임계 구역은 한 번에 하나다.

</details>

<details>
<summary>힌트 2 — TODO 2: ISR 도 "우선순위 높은 태스크" 다</summary>

`level()` 이 ISR 을 `1000 − nvic`, 태스크를 `prio` 로 바꿔 한 축에 세운다. hp 는 level 이 나 이상인 다른 개체 (같은 level 도 보수적으로 넣는다). `w0 = C + B` 에서 시작해 반복, `w + J_i > D` 면 즉시 실패, 같아지면 수렴. `D == 0` (데드라인 없음) 은 한도 10 s 로. 교과서 세트 (20,100)(40,150)(100,350) → 20, 60, 240 이 먼저 맞아야 한다.

</details>

<details>
<summary>힌트 3 — TODO 3: 도착 곡선과 서비스 곡선의 차이</summary>

`A(t)` = t 까지 도착한 프레임 수, `β(Δ)` = `served_lb` (길이 Δ 동안 소비자가 반드시 끝내는 수). 끝낸 수의 하한은 "어느 시점 s 이후로 계속 바빴다" 를 모든 s 에 대해 따진 최솟값이다: s 가 첫 도착 전이면 `β(t)`, s 가 k 번째와 k+1 번째 도착 사이면 `k + β(t − s)` — β 가 Δ 에 단조라 그 구간의 **마지막 시각** `k·a − 1` 이 최소를 준다. depth = `max A − D`, latency = `D ≥ n` 이 되는 첫 t.

</details>

<details>
<summary>힌트 4 — TODO 4: 크기 문제 전에 처리량 문제</summary>

링이 아무리 커도, 섹터 하나(256 레코드)를 쓰는 한 바퀴(지우기 400 ms + 페이지 16장 × (3 ms + 1 tick)) 동안 그보다 많이 들어오면 언젠가 넘친다. 그 검사가 먼저다. 통과하면 `rec_per_page + arrivals(stall)` — flush 는 페이지 1장이 찼을 때 일을 시작하고, 그 뒤 stall 동안 아무것도 못 꺼낸다.

</details>

<details>
<summary>힌트 5 — TODO 5~8: 커널 API 의 세 가지 약속</summary>

① ISR 용 API 는 **절대 기다리지 않는다** — 큐가 차면 `errQUEUE_FULL`, 링이 차면 버림. ② 깨우기와 문맥 전환은 나뉜다 — give 는 READY 로 만들고 `*woken` 만 세운다 (RTOS-M02). ③ 커널 API 는 BASEPRI 아래 ISR 에서만 — 위에서 부르면 assert. 워치독은 "킥을 누가 결정하나" 가 전부다: 모니터가 **모든** 비트를 봤을 때만, 그리고 판정 후 비트를 지워 다음 창에서 다시 증명하게 한다.

</details>

## 5. 풀이

### 0. 면접에서 말하는 순서

| 단계 | 이 시스템에서 말할 것 | 근거 숫자 |
|---|---|---|
| 1. 요구사항 확인 | 전류 루프 데드라인은 어디서 오나 (ADC 트리거 → 다음 PWM 섀도 로드), CAN 비트레이트·버스트 크기, 로그 유실 허용 여부, 워치독 종류(IWDG/WWDG), 안전 상태 정의 | D_cur 60 µs, 16 프레임 × 111 µs, 지우기 ≤ 400 ms |
| 2. 타이밍 예산 | 개체별 C·T·D 표, 총 이용률 | U = 44 % (주기 개체) |
| 3. 분할 | 무엇이 ISR, 무엇이 태스크, 왜 | 전류 루프 ISR (아래 표), 나머지 태스크 |
| 4. 우선순위 | NVIC: 데드라인 짧은 순. 태스크: RM/DM + 모니터 예외 | NVIC 1/5/6/15, prio 5/4/3/1 |
| 5. IPC | ISR→태스크 = 알림, ISR→태스크 데이터 = 큐, 태스크→flush = 링 | 큐 3, 링 224 |
| 6. 자원·스택 | 임계 구역 길이 상한, 스택 예산, RAM 합 | B = 4 µs, 스택 1912 B + MSP 456 B |
| 7. 검증 | RTA + 큐/링 분석 → 시뮬레이션·트레이스로 관측 ≤ 분석 확인 → 보드에서 GPIO/트레이스 | R_speed 316 vs 관측 248 |
| 8. 실패 모드 | 태스크 정지·폭주, 플래시 드라이버, 버스트 폭주, 워치독 위치, 안전 상태 | 리셋 상한 120 ms, 그 전에 토크 차단 |

### 0-1. 모델 ↔ 실제 API 대응표

| 이 레슨 (모델) | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|
| `notify_give_from_isr` | `vTaskNotifyGiveFromISR` → `vTaskGenericNotifyGiveFromISR` (tasks.c:8315, 값 ++ 8358, 대기 중이면 READY 8364, 더 높으면 `*pxHigherPriorityTaskWoken` + `xYieldPendings` 8399–8411) | `k_sem_give()` (ISR 가능) | `osThreadFlagsSet()` |
| `notify_take` (clear on exit) | `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` (task.h:3302 → tasks.c:7797, 값 읽기 7867, 0 으로 7873) | `k_sem_take(K_FOREVER)` | `osThreadFlagsWait()` |
| ISR 우선순위 가드 (`asserts++`) | `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` (tasks.c:8344) = `vPortValidateInterruptPriority` (ARM_CM4F portmacro.h:179, port.c:851, assert 888) | Zero-latency IRQ 는 커널 API 금지 (`CONFIG_ZERO_LATENCY_IRQS`) | — |
| `queue_send_from_isr` (가득 → 버림) | `xQueueSendFromISR` → `xQueueGenericSendFromISR` (queue.c:1167, 자리 검사 1207, `errQUEUE_FULL` 1327) | `k_msgq_put(…, K_NO_WAIT)` | `osMessageQueuePut(…, 0)` |
| `prog_can` 의 10 ms 타임아웃 수신 | `xQueueReceive(q, &m, pdMS_TO_TICKS(10))` (queue.c:1509) | `k_msgq_get(…, K_MSEC(10))` | `osMessageQueueGet(…, 10)` |
| `log_push` / `log_pop` | 직접 만든 링 + `taskENTER_CRITICAL`, 또는 단일 생산자면 `xStreamBufferSend` (스트림/메시지 버퍼는 **writer 1·reader 1 가정**, stream_buffer.h:35–44) | `ring_buf` + `k_spin_lock` | — |
| `seg(c, n, 1)` (임계 구역) | `taskENTER_CRITICAL` → `portDISABLE_INTERRUPTS` = `vPortRaiseBASEPRI` (portmacro.h:122, 213–224) | `irq_lock()` / `k_spin_lock()` | — |
| `pend(I_BRIDGE)` | `NVIC_SetPendingIRQ()` 또는 `NVIC->STIR` (CMSIS-Core) | `NVIC_SetPendingIRQ()` | 같음 |
| 스레드 모드 복귀 시 재선택 | `portYIELD_FROM_ISR(woken)` → `portEND_SWITCHING_ISR` → `portYIELD` = PENDSVSET (portmacro.h:114, 101, 88–97) | ISR 종료 시 자동 | 자동 |
| `prog_wdg` 의 `delay_until` | `xTaskDelayUntil(&last, 20)` (tasks.c:2414, 이미 지났으면 블록 없이 pdFALSE — 2498) | `k_sleep(K_TIMEOUT_ABS_MS())` | `osDelayUntil()` |
| `wdg_kick()` | 칩 레지스터 (STM32 IWDG: `KR = 0xAAAA`, RM 기준) | `wdt_feed()` | — |

### Part A — 분할: 무엇을 ISR 에, 무엇을 태스크에

| 일 | 어디서 | 왜 | 대안과 비용 |
|---|---|---|---|
| 전류 루프 (FOC) | **ISR, NVIC 1** (ADC 주입 변환 완료) | 100 µs 주기에 60 µs 데드라인, 지터가 곧 토크 리플. NVIC 1 은 BASEPRI 에 안 가려져 RTOS 의 어떤 임계 구역도 지연시키지 못한다 → R = C = 25 µs **고정** | 태스크로 하면 매 주기 문맥 전환 2번(초당 2만 번), J = ADC ISR 응답 10 µs + 임계 구역 B 4 µs → R 48 µs, 지터 0 → 23 µs (연습 1) |
| 속도 루프 | 태스크 prio 4 | 1 ms 에 150 µs 는 ISR 로 하기엔 길다 — ISR 이면 그 아래 모든 IRQ(CAN, tick)가 150 µs 씩 밀린다 (RTOS-M02 (c)) | 타이머 기반 `xTaskDelayUntil` 도 가능하지만 전류 루프와 **위상이 안 맞는다**. 전류 ISR 에서 파생하면 항상 같은 PWM 주기에 정렬 |
| 전류 → 속도 연결 | **브리지 SWI (NVIC 6)** | NVIC 1 ISR 은 FreeRTOS API 를 못 부른다 (`vPortValidateInterruptPriority`). 10번째마다 비어 있는 IRQ 를 STIR 로 펜딩 → 그 ISR 이 `vTaskNotifyGiveFromISR` | 전류 ISR 을 NVIC 5 로 내리면 바로 부를 수 있지만 B = 4 µs 지터가 생긴다 (테스트: R 25 → 33) |
| CAN 수신 | ISR 은 HW FIFO → 큐 복사만 (C 4) | HW FIFO 3개 = 333 µs 안에 꺼내야 오버런 없음. 파싱은 태스크 | ISR 에서 파싱(30 µs)하면 tick·브리지가 그만큼 밀림 |
| 명령 처리 | 태스크 prio 3 | 데드라인이 ms 단위 (응답 2 ms) | 속도 루프보다 위로 올리면 속도 루프의 R 이 버스트 동안 커진다 |
| 로깅 | 링 + 최하위 flush | 플래시는 ms~수백 ms. 누구도 플래시를 기다리면 안 된다 | 로그를 큐로 보내면 생산자가 가득 찬 큐에서 블록할 수 있다 (RTOS-A09 큐 포화 사례) |
| 워치독 | 모니터 태스크 prio 5 + 생존 비트 | "tick 이 돈다" 가 아니라 "**모든 태스크가 일했다**" 를 증명해야 킥 | 하드웨어 윈도우 워치독이면 킥 시각의 흔들림도 중요 → 모니터를 높게 (연습 2) |

**우선순위 근거.** ISR 은 데드라인 순(DM): 전류 60 µs < CAN 333 µs < 브리지·tick 1 ms. 태스크는 주기 순(RM)으로 speed(1 ms) > can_cmd > log_flush 인데, 모니터만 예외로 맨 위다 — C 가 10 µs 라 다른 태스크의 R 에 거의 영향이 없고(RTA 에서 speed 의 간섭 10 µs), 킥 시각 흔들림이 48 − 10 = 38 µs 로 작아진다 (prio 2 로 내리면 R_max 935 µs, 연습 2). 모니터가 높아도 "굶는 태스크" 는 비트가 비어서 잡힌다 — 우선순위가 아니라 비트마스크가 생존을 판정한다.

### Part A — 블로킹과 RTA (TODO 1, 2)

```c
static uint32_t blocking_term(const sys_t *s, int i) {
    const ent_t *ei = &s->e[i];
    if (ei->isr && !maskable(ei)) return 0;
    uint32_t b = 0;
    for (int j = 0; j < s->n; j++) {
        if (j == i) continue;
        const ent_t *ej = &s->e[j];
        int can_block = ei->isr ? (!ej->isr || ej->nvic > ei->nvic)
                                : (!ej->isr && ej->prio < ei->prio);
        if (can_block && ej->crit > b) b = ej->crit;
    }
    return b;
}
```

`./solution -v` 의 분석 표 (실제 출력):

```text
== RTA (us)        kind  nvic/prio     C      T      D     J   B      R  ok iters
  cur_isr        ISR           1    25    100     60     0   0     25   1     1
  can_isr        ISR           5     4    111    333     0   4     33   1     2
  bridge_swi     ISR           6     2   1000   1000    25   4     60   1     2
  systick        ISR          15     3   1000   1000     0   4     38   1     2
  wdg_mon        task          5    10  20000  20000     0   4     48   1     2
  speed          task          4   150   1000   1000    60   4    316   1     3
  can_cmd        task          3    30    111      -     0   4      -   -     - (no deadline: backlog / background)
  log_flush      task          1     4   1000      -     0   0      -   -     - (no deadline: backlog / background)
  U(periodic, x1e-4) = 4415   CAN backlog: depth 3, burst latency 1950 us   log ring need 218 rec
```

손으로 따라가 보자. **브리지**: `w0 = 2 + 4 = 6` → 전류 ISR ⌈6/100⌉·25 + CAN ⌈6/111⌉·4 = 29 → `w = 35` → 그대로 수렴. `R = 35 + J(25) = 60`. **속도**: `J = 60`, `w0 = 154` → `154 + 50 + 8 + 2 + 3 + 10 = 227` → 전류 3번(75), CAN 3번(12) → `256` → 수렴, `R = 256 + 60 = 316 ≤ 1000`.

**지터 전파** (`propagate_jitter`): 속도 작업의 "명목 출시" 는 1 ms 경계(전류 ISR 트리거 시각)다. 실제로 READY 가 되는 건 전류 ISR 이 끝나고 → 브리지가 펜딩되어 → 브리지가 돌고 난 뒤다. 그래서 `J_bridge = R_cur = 25`, `J_speed = R_bridge = 60`. 체인으로 연결된 ISR/태스크에서 앞 단계의 응답이 다음 단계의 지터가 되는 이 방식은 holistic analysis 라고 부른다 (Tindell & Clark 1994, 분산 시스템 대상 — 제목은 기억 기준). 같은 전류 ISR 이 J 와 간섭 w 양쪽에 들어가므로 **보수적** 이다 — 관측 지터는 31 µs, 관측 응답 최대는 248 µs 였다.

교과서 검사(M11 의 세트)도 같은 함수로 통과한다: (20,100)(40,150)(100,350) → 20, 60, 240, t2 에 J = 70 을 주면 R2 = 130, R3 = 280 (상위 지터가 하위 창에 작업을 하나 더 밀어 넣는다).

**나쁜 설계 두 개를 분석으로 먼저 잡는다** (테스트):

- flush 의 crit 를 400 000 µs (지우기 대기를 임계 구역 안에서) 로 바꾸면 → `B_speed = B_can_isr = 400 ms` → 둘 다 RTA 실패. `B_cur` 는 여전히 0 → 전류 루프만 살아남는다.
- 전류 ISR 을 NVIC 5 로 내리면 → `B_cur = 4`, `R_cur = 29 + 4 = 33` (CAN ISR 4 µs 도 같은 레벨로 간섭). 60 µs 안이지만 **지터가 생긴다**.

### Part A — CAN 큐 깊이 (TODO 3)

요구사항 "호스트는 최대 16 프레임을 연달아 보낸다". 1 Mbit/s, 표준 ID, DLC 8 프레임의 **가장 짧은** 길이는 스터핑 없이 44 + 64 + 3(IFS) = 111 비트 (COMM-M04) — 큐 분석에는 최단 간격이 최악이다. 소비자 `can_cmd` (prio 3) 는 프레임당 30 µs, 그 위의 전부(ISR 4개, 모니터, 속도 루프)에 간섭받는다.

```c
for (uint32_t t = 0; t <= limit; t++) {
    uint32_t A = t / a + 1 < n ? t / a + 1 : n;            /* 도착 곡선 */
    uint32_t D = served_lb(s, i, t, B, c);                  /* 첫 도착 전부터 바빴다고 보면 */
    for (uint32_t k = 1; k <= n && (k - 1) * a <= t; k++) {
        uint32_t sk = (k < n && k * a - 1 < t) ? k * a - 1 : t;
        uint32_t term = k + served_lb(s, i, t - sk, B, c);  /* sk 이후로 계속 바빴다고 보면 */
        if (term < D) D = term;
    }
    if (D > n) D = n;
    if (A > D && A - D > r.depth) r.depth = A - D;
    if (D >= n) { r.ok = 1; r.latency = t; break; }
}
```

`served_lb(Δ) = ⌊max(0, Δ − B − I(Δ)) / c⌋` 는 "고정 우선순위 아래에서 남는 서비스(leftover service)" 의 하한이고, 끝낸 수의 하한 `D(t) = min_s [A(s) + β(t − s)]` 는 네트워크 캘큘러스의 min-plus 합성곱이다 (Le Boudec & Thiran, *Network Calculus*, 2001). 결과: **깊이 3, 버스트 지연 ≤ 1950 µs**. 시뮬레이션(19 버스트, 위상을 389 µs 씩 바꿔 가며)의 관측은 최대 2개, 최대 1929 µs — 분석이 관측을 덮고, 지연은 21 µs 차이로 꽤 빡빡하다. 깊이는 3 으로 정했다 (`CAN_QDEPTH`) — 분석에는 "꺼내서 처리 중인 1개" 가 포함돼 있어 1 만큼 보수적이다. 메모리는 3 × 16 B = 48 B 뿐이니 여기서 아낄 이유는 없다.

처음 시도했던 "s = 0 하나만 보는" 식 (`A(t) − β(t)`) 은 깊이 3 은 맞췄지만 지연을 **935 µs** 로 냈다 — 마지막 프레임이 1665 µs 에야 도착하는데도! 아직 안 온 일을 서비스로 센 것이다. 관측 1929 µs 와 비교해서 틀린 걸 알았다. 분석은 반드시 시뮬레이션이나 실측과 대조해야 하는 이유다.

민감도: 속도 루프 C 를 300 µs 로 두 배 하면 깊이 **8**, 지연 2439 µs. 큐 깊이는 위쪽 태스크의 길이에 민감하다 (연습 5: 깊이 3 을 유지하려면 C_speed ≤ 169 µs, RTA 만 보면 635 µs 까지 가능).

### Part A — 로그 링 크기와 처리량 (TODO 4)

```c
static uint32_t log_ring_needed(const producer_t *p, int np, uint32_t stall_us, uint32_t rec_per_page,
                                uint32_t cycle_us, uint32_t sector_recs) {
    if (arrivals(p, np, cycle_us) > sector_recs) return 0;     /* 처리량이 먼저 */
    return rec_per_page + arrivals(p, np, stall_us);
}
```

생산자: 속도 루프 100 Hz (10번에 1번 로그), CAN 버스트 16개 / 50 ms. 숫자 (16 B 레코드, 256 B 페이지, 4 KB 섹터):

| 항목 | 식 | 값 |
|---|---|---|
| stall | 지우기 400 ms + 페이지 3 ms + 폴링 2 tick | 405 ms |
| 도착 (속도) | ⌈405 000 / 10 000⌉ + 1 | 42 |
| 도착 (CAN) | (⌈405 000 / 50 000⌉ + 1) × 16 | 160 |
| 필요량 | 16 + 42 + 160 | **218 레코드** → 224 로 올림 = 3584 B |
| 섹터 한 바퀴 | 400 ms + 16 × (3 + 1) ms = 464 ms 동안 47 + 176 = 223 ≤ 256 | 지속 가능 |
| 1 kHz 전부 로그 + 20 ms 버스트 | 한 바퀴 동안 465 + 400 > 256 | **지속 불가 → 0** |

**최악 지우기 시간은 버퍼 크기가 아니라 로그 대역폭을 제한한다.** 400 ms 지우기를 섹터마다 견디면 지속 가능한 로그는 약 256 레코드 / 464 ms ≈ 0.55 레코드/ms 다. 1 kHz 로 매번 로그하면 링을 얼마로 잡든 언젠가 넘친다 — 연습 4 에서 최대 지우기 기준 최소 간격이 6 (166 Hz) 으로 나온다. 시뮬레이션: 링 최대 184 ≤ 218, 지우기 2번, 유실 0, 생산 404 = 기록 + 링 + 페이지 버퍼 (레코드 보존).

`flush` 의 구조가 곧 **이중 버퍼** 다: 링(앞 버퍼)에는 생산자가 계속 쓰고, flush 는 페이지 1장을 자기 페이지 버퍼(뒤 버퍼)로 4개씩 임계 구역 4 µs 조각으로 옮긴 뒤 플래시가 바쁜 동안은 **1 tick 씩 잠들며 폴링** 한다 (`task_delay(1)`, 체크인 포함). 플래시가 400 ms 바빠도 앞 버퍼는 계속 받는다.

### Part A — 스택과 RAM

RTOS-M08 의 방법 그대로: 최악 경로(추정) + 문맥(FPU 태스크 208 B = 예외 프레임 104 + 정렬 4 + PendSV r4–r11·r14·s16–s31 100, 정수 태스크 72 B) + 여유 25 %(최소 64 B), 8 B 정렬. `./solution -v` (실제 출력):

```text
== stack budget (bytes, path = estimate)
  wdg_mon   path  96 fpu 0 ctx  72 -> 232 B (58 words)
  speed     path 320 fpu 1 ctx 208 -> 664 B (166 words)
  can_cmd   path 208 fpu 1 ctx 208 -> 520 B (130 words)
  log_flush path 176 fpu 0 ctx  72 -> 312 B (78 words)
  IDLE      path  48 fpu 0 ctx  72 -> 184 B (46 words)
  tasks total 1912 B, MSP 456 B, CAN queue 48 B, log ring 3584 B, page buf 256 B
```

경로 바이트는 **추정** 이다 — 실제 빌드에서는 `-fstack-usage` + 호출 그래프로 바꾼다 (M08). can_cmd 를 FPU 로 친 이유: 명령의 float 스케일링 한 줄이 FPCA 를 켠다 (M08 의 comms 함정). MSP 는 중첩 사슬 SysTick(44) ← 브리지(40) ← CAN(64) ← 전류(120), 끼어들 때마다 정수 프레임 32 B (핸들러 진입 시 FPCA 는 0 이므로 기본 프레임 — Armv7-M 문서 기준) → 364 → 456 B. RAM 합계 약 6.2 KB + TCB·커널 객체. 로그 링이 가장 크다 — RAM 이 빠듯하면 바꿀 결정 1순위.

### Part B — 핵심 경로 (TODO 5~8)

```c
static void notify_give_from_isr(tcb_t *t, int *woken) {
    if (K.in_isr >= 0 && K.isr[K.in_isr].nvic < NVIC_SYSCALL) { K.st.asserts++; return; }
    t->notify++;
    if (t->state == TASK_BLOCKED && t->wait == W_NOTIFY) {
        make_ready(t);
        if (t->prio > running_prio()) *woken = 1;
    }
}
static uint32_t wdg_monitor(void) {
    uint32_t missing = WDG_ALL & ~K.alive;
    if (missing == 0) wdg_kick();
    K.alive = 0;
    return missing;
}
```

- **알림 값은 카운트다.** 속도 태스크가 한 주기를 놓치면 다음 take 가 2 를 돌려준다 → `spd_merged` 로 세고, 응답은 가장 오래된 출시 기준으로 잰다. 바이너리 세마포어였다면 두 번째 give 가 조용히 사라진다 (RTOS-M01).
- **큐 송신은 기다리지 않는다.** FreeRTOS 도 같다: `xQueueGenericSendFromISR` 는 자리가 없으면(queue.c:1207) 바로 `errQUEUE_FULL` (1327). 그래서 깊이는 **분석으로** 정해야 한다.
- **링 push 는 실패해도 된다.** 버리고 센다 — 속도 루프가 로그 때문에 늦는 것보다 로그 한 줄 잃는 게 낫다. 버린 수는 다음 레코드에 실어 보내 사후 분석에 쓴다.
- **판정 후 비트를 지운다.** 지우지 않으면 한 번 체크인한 태스크가 그 뒤 영원히 "살아 있음" 으로 보인다. 실제 칩에서 체크인은 `taskENTER_CRITICAL` 또는 `LDREX/STREX` 원자적 OR 이어야 한다 — 모니터의 "읽고 지우기" 와 섞이면 비트가 되살아나거나 사라진다 (실수 표).

`./solution -t` — 정상 실행 50.000–50.620 ms (버스트 1의 첫 프레임이 속도 출시와 같은 시각에 도착하도록 위상을 맞춘 구간) 의 실제 트레이스 일부 (rtos-trace v1, RTOS-A06 포맷):

```text
# rtos-trace v1 unit=us
50000 ISR_ENTER cur_isr
50025 ISR_EXIT cur_isr
50025 ISR_ENTER bridge_swi
50027 ISR_EXIT bridge_swi
50027 RELEASE speed 50000
50027 ISR_ENTER can_isr
50031 ISR_EXIT can_isr
50031 ISR_ENTER systick
50034 ISR_EXIT systick
50034 SWITCH_OUT IDLE
50034 SWITCH_IN speed
...
50238 SWITCH_OUT speed
50238 SWITCH_IN can_cmd
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">실제 트레이스 50.000–50.600 ms (1 px = 1 µs) — 버스트 첫 프레임이 속도 출시와 겹친 최악 위상</text>
  <text x="10" y="44" fill="currentColor">cur_isr</text>
  <text x="10" y="66" fill="currentColor">bridge_swi</text>
  <text x="10" y="88" fill="currentColor">can_isr</text>
  <text x="10" y="110" fill="currentColor">systick</text>
  <text x="10" y="140" fill="currentColor">speed</text>
  <text x="10" y="162" fill="currentColor">can_cmd</text>
  <text x="10" y="184" fill="currentColor">IDLE</text>
  <g class="f-warn" stroke="currentColor">
    <rect x="110" y="32" width="25" height="16"/><rect x="210" y="32" width="25" height="16"/><rect x="310" y="32" width="25" height="16"/>
    <rect x="410" y="32" width="25" height="16"/><rect x="510" y="32" width="25" height="16"/><rect x="610" y="32" width="25" height="16"/>
  </g>
  <rect x="135" y="54" width="2" height="16" class="f-acc" stroke="currentColor"/>
  <g class="f-acc" stroke="currentColor">
    <rect x="137" y="76" width="4" height="16"/><rect x="248" y="76" width="4" height="16"/><rect x="359" y="76" width="4" height="16"/>
    <rect x="470" y="76" width="4" height="16"/><rect x="581" y="76" width="4" height="16"/><rect x="692" y="76" width="4" height="16"/>
  </g>
  <rect x="141" y="98" width="3" height="16" class="f-mut" stroke="currentColor"/>
  <g class="f-ok" stroke="currentColor">
    <rect x="144" y="128" width="66" height="16"/><rect x="235" y="128" width="13" height="16"/>
    <rect x="252" y="128" width="58" height="16"/><rect x="335" y="128" width="13" height="16"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="348" y="150" width="11" height="16"/><rect x="363" y="150" width="47" height="16"/><rect x="435" y="150" width="32" height="16"/>
    <rect x="474" y="150" width="30" height="16"/><rect x="585" y="150" width="25" height="16"/><rect x="635" y="150" width="5" height="16"/>
    <rect x="696" y="150" width="14" height="16"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <rect x="467" y="172" width="3" height="16"/><rect x="504" y="172" width="6" height="16"/>
    <rect x="535" y="172" width="46" height="16"/><rect x="640" y="172" width="52" height="16"/>
  </g>
  <line x1="110" y1="196" x2="710" y2="196" stroke="currentColor"/>
  <text x="104" y="212" fill="currentColor">0</text><text x="200" y="212" fill="currentColor">100</text><text x="300" y="212" fill="currentColor">200</text>
  <text x="400" y="212" fill="currentColor">300</text><text x="500" y="212" fill="currentColor">400</text><text x="600" y="212" fill="currentColor">500</text>
  <text x="694" y="212" fill="currentColor">600</text>
  <line x1="110" y1="226" x2="348" y2="226" stroke="currentColor"/>
  <line x1="110" y1="220" x2="110" y2="232" stroke="currentColor"/><line x1="348" y1="220" x2="348" y2="232" stroke="currentColor"/>
  <text x="116" y="246" fill="currentColor">speed 응답 238 µs (명목 출시 → 완료). 분석 R = 316, 1 s 관측 최대 248</text>
  <text x="10" y="270" fill="currentColor">CAN 프레임 도착 27·138·249·360·471·582 — speed 가 도는 동안 큐 2개까지 쌓였다가 238 부터 can_cmd 가 비운다.</text>
  <text x="10" y="290" fill="currentColor">전류 ISR 은 매 100 µs 정각에 25 µs — 어떤 태스크·임계 구역도 이 줄을 밀지 못한다 (cur max 25, 1만 회).</text>
</svg>
```

(357 µs 에 log_flush 가 0 µs 동안 깼다가 링이 16개가 안 돼 다시 잠든 것은 막대로 안 그렸다.) 정상 설계 1 s 실행 요약 (`./solution -v`, 실제 출력):

```text
  design                 spd 1000 jobs miss 0 max 248 jit 31 merged 0 | cur max 25 miss 0 | can 304/304 hw_ovr 0 q_drop 0 q_max 2 isr_max 25 burst 1929
                         log push 404 drop 0 ring_max 184 pages 16 erases 2 | wdg kicks 49 bite 0@0 missing 0x0 | tick_lost 0 ctx 3560 idle 58% end 1000000
```

| 양 | 분석 | 관측 (1 s) |
|---|---|---|
| 전류 ISR 응답 | 25 | 25 (1만 회) |
| CAN 도착 → ISR 이 꺼냄 | R_can = 33 | 25 |
| 속도 출시 지터 | J = 60 | 31 |
| 속도 응답 | 316 | 248 |
| CAN 큐 | 깊이 3, 지연 1950 | 최대 2, 1929 |
| 로그 링 | 218 | 184 |

### Part C — 실패 모드

`./solution -v` 의 실패 시나리오 (실제 출력 중 핵심):

```text
  naive flush (crit)     spd 52 jobs miss 88 ... | cur max 25 miss 0 | can 12/32 hw_ovr 17 ...
                         ... erases 1 | wdg kicks 2 bite 1@140038 missing 0x0 | tick_lost 88 ...
  can queue depth 1      ... can 272/304 hw_ovr 0 q_drop 32 q_max 1 ...
  log ring need/2        spd 1000 jobs miss 0 ... | log push 329 drop 75 ring_max 109 ...
  speed                  ... wdg kicks 16 bite 1@420038 missing 0x1
  can_cmd                ... wdg kicks 15 bite 1@400038 missing 0x2
  log_flush              ... wdg kicks 16 bite 1@420038 missing 0x4
  speed busy-loop        ... can 80/128 hw_ovr 0 q_drop 44 ... bite 1@420038 missing 0x7
  kick in tick hook      spd 301 jobs miss 298 ... | wdg kicks 615 bite 0@0 missing 0x1
```

1. **naive flush.** 지우기 400 ms 를 임계 구역 안에서 기다리면 BASEPRI 아래 전부 — CAN ISR, 브리지, SysTick, PendSV — 가 멈춘다. 속도 미스 88, CAN HW FIFO 오버런 17/32, tick 유실 88 (펜딩 비트가 하나라 겹친 tick 은 사라진다 → 시간이 늦게 간다), 그리고 모니터가 못 돌아 **140 ms 에 워치독 리셋** — `missing 0x0` 은 "판정조차 못 했다" 는 뜻이다. 전류 ISR 만 0 미스: NVIC 1 을 BASEPRI 위에 둔 덕분이다. 현장에서는 "로그가 지우기를 시작할 때마다 리셋되는 보드" 로 보인다.
2. **얕은 큐.** ISR 은 정시(오버런 0)인데 소프트웨어 큐에서 32개 유실 — 병목은 ISR 이 아니라 **소비자 앞의 버퍼** 다.
3. **작은 링.** 로그 75개 유실, 속도 미스 0. 로깅의 실패가 제어로 번지지 않는다 — 설계 의도대로.
4. **태스크 정지.** 상한 = 모니터 주기 20 ms + R_mon 48 µs + IWDG 100 ms = **120 048 µs**. 300.3 ms 에 멈추게 하면: speed 는 420.038 ms 에 리셋 (119 738 µs, 상한에 310 µs 차이 — 거의 최악), can_cmd 는 400.038 ms, log_flush 는 420.038 ms. `missing` 비트가 범인을 정확히 가리킨다. 폭주 루프(speed 가 prio 4 로 CPU 를 먹음)는 can_cmd·log_flush 를 굶겨 0x7 — 모니터는 prio 5 라 여전히 돌아서 "킥하지 않기로" 결정할 수 있다.
5. **tick 훅 킥.** 같은 고장인데 600 ms 동안 리셋이 없다. 속도 루프는 300 ms 부터 죽어 있었다 (298 미스). 전류 루프는 마지막 속도 명령으로 계속 토크를 낸다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">워치독 상한 — speed 가 300.3 ms 에 멈춤 (실제 실행, 1 px = 0.25 ms)</text>
  <line x1="40" y1="80" x2="700" y2="80" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="40" y1="74" x2="40" y2="86"/><line x1="120" y1="74" x2="120" y2="86"/><line x1="200" y1="74" x2="200" y2="86"/>
    <line x1="280" y1="74" x2="280" y2="86"/><line x1="360" y1="74" x2="360" y2="86"/><line x1="440" y1="74" x2="440" y2="86"/>
    <line x1="520" y1="74" x2="520" y2="86"/><line x1="600" y1="74" x2="600" y2="86"/>
  </g>
  <text x="28" y="102" fill="currentColor">280</text><text x="108" y="102" fill="currentColor">300</text><text x="188" y="102" fill="currentColor">320</text>
  <text x="268" y="102" fill="currentColor">340</text><text x="348" y="102" fill="currentColor">360</text><text x="428" y="102" fill="currentColor">380</text>
  <text x="508" y="102" fill="currentColor">400</text><text x="588" y="102" fill="currentColor">420 ms</text>
  <circle cx="40" cy="60" r="6" class="f-ok" stroke="currentColor"/><circle cx="120" cy="60" r="6" class="f-ok" stroke="currentColor"/>
  <circle cx="200" cy="60" r="6" class="f-ok" stroke="currentColor"/>
  <circle cx="280" cy="60" r="6" class="f-warn" stroke="currentColor"/><circle cx="360" cy="60" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="440" cy="60" r="6" class="f-warn" stroke="currentColor"/><circle cx="520" cy="60" r="6" class="f-warn" stroke="currentColor"/>
  <text x="40" y="40" fill="currentColor">킥 (모든 비트)</text>
  <text x="290" y="40" fill="currentColor">킥 안 함: missing 0x1</text>
  <line x1="121" y1="110" x2="121" y2="150" stroke="currentColor" stroke-width="2"/>
  <text x="128" y="146" fill="currentColor">300.3 정지 — 300 ms 작업은 300.2 무렵 이미 체크인</text>
  <line x1="200" y1="170" x2="600" y2="170" stroke="currentColor"/>
  <polygon points="600,170 590,165 590,175" fill="currentColor"/>
  <text x="250" y="164" fill="currentColor">마지막 킥 320.038 + IWDG 100 ms</text>
  <rect x="600" y="52" width="14" height="40" class="f-warn" stroke="currentColor"/>
  <text x="560" y="128" fill="currentColor">리셋 420.038</text>
  <line x1="121" y1="196" x2="601" y2="196" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="130" y="216" fill="currentColor">상한 = P_mon 20 + R_mon 0.048 + W 100 = 120.048 ms → 관측 119.738 ms</text>
</svg>
```

최악은 "모니터가 판정한 **직후** 마지막 체크인을 하고 멈추는" 경우다. 그 비트는 다음 판정(P 뒤)에서 아직 유효해 킥이 한 번 더 일어나고, 그 킥부터 W 가 지나야 리셋된다. 120 ms 는 모터에게 길다 — 그래서 워치독은 **최후의 수단** 이고, 전류 ISR 이 속도 명령의 나이(heartbeat)를 보고 수 ms 안에 토크를 끄는 안전 경로가 따로 있어야 한다 (Q7).

### 질문받으면 바꿀 결정

| 바뀐 조건 | 바꿀 결정 | 이유 / 숫자 |
|---|---|---|
| 전류 루프 20 kHz, 또는 C_cur 40 µs | FOC 일부를 하드웨어로 (STM32G4 CORDIC/FMAC 등, 문서 기준), 코드·데이터를 CCM/ITCM RAM 으로 | ISR 이용률 25 % → 50~80 % 면 나머지 전부의 R 이 커진다 |
| 내부 플래시에 로그 | 단일 뱅크면 지우기 동안 **플래시에서 코드 실행이 멈춘다** (칩 RM 기준) → 전류 ISR·벡터를 RAM 으로, 또는 듀얼 뱅크/RWW | NVIC 1 도 못 피하는 정지 — 분석의 가정이 깨진다 |
| 로그를 잃으면 안 됨 (블랙박스) | FRAM/MRAM, 미리 지운 섹터 풀, 지우기 일시정지, 로그 비율 상한 | 400 ms 지우기에서 지속 가능 비율 ≈ 0.55 레코드/ms (연습 4) |
| CAN 버스트 64 프레임, CAN FD | 큐 깊이 재유도, DMA/FIFO 워터마크, 명령 우선순위 큐 분리 | 깊이는 버스트·소비자 간섭에 선형 이상으로 민감 (C_speed 300 → 8) |
| RAM 이 4 KB 밖에 없음 | 로그 링을 줄이고 버림 + 카운터, 또는 로그 비율 낮춤 | 링 3.5 KB 가 RAM 의 절반 이상 |
| 듀얼 코어 (STM32H7 류) | 전류 루프를 한 코어에 단독으로, 통신·로그는 다른 코어 + 메일박스 | 코어 간은 PRIMASK 로 못 막는다 (RTOS-A08) |
| 기능 안전 요구 (IEC 61508 / ISO 26262 류) | 윈도우 워치독 + 프로그램 흐름 감시, 독립 클럭, 안전 상태 진입 시간 요구치로 P_mon·W 역산 | 리셋 상한 120 ms 가 요구치 안인지 |
| Zephyr 로 이식 | 전류 ISR = zero-latency IRQ, 브리지 = 일반 IRQ + `k_sem_give`, 큐 = `k_msgq` | 구조는 그대로, 이름만 바뀐다 (문서 기준, RTOS-A07) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 48/48 passed (약 0.1 s)
./solution -v      # 분석 표 + 시나리오 11개 통계
./solution -t      # 트레이스 구간 (rtos-trace v1)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 48/48, 보고 없음
```

O2 빌드로 5번 연속 48/48, `-v` 출력의 md5 도 매번 같았다 (시뮬레이션 시간이라 부하와 무관). 확인 환경: Apple clang 21.0.0, load average 약 13–17 (다른 작성 작업 동시 실행 중 — 결과에는 영향 없음).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| 전류 ISR(NVIC 0~4)에서 `xSemaphoreGiveFromISR` / `vTaskNotifyGiveFromISR` | BASEPRI 위 ISR 은 커널 임계 구역 도중에도 들어온다 → 리스트가 반쯤 바뀐 상태에서 커널을 만진다. `configASSERT` 가 켜져 있으면 `vPortValidateInterruptPriority` (port.c:888) 가 잡는다 (Part B: 5 ms 에 assert 5번) | 브리지 SWI (NVIC ≥ 5) 를 펜딩, 또는 전류 ISR 을 NVIC ≥ 5 로 내리고 지터를 감수 |
| 커널을 쓰는 ISR 을 기본 우선순위 0 에 둠 | NVIC 0 = 가장 급함 = 무조건 BASEPRI 위. 위와 같은 버그 | 커널 API 쓰는 ISR 은 전부 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이상(숫자) |
| 플래시 드라이버가 완료 대기를 임계 구역/스케줄러 정지 안에서 | B = 지우기 시간. 속도 미스, CAN 오버런, tick 유실, 리셋 (Part C 1) | 명령만 내고 1 tick 씩 잠들며 폴링, 임계 구역은 µs 조각으로 |
| 로그를 "가득 차면 기다리는" 큐로 | 플래시가 바쁜 동안 속도 루프가 로그에서 블록 → 제어가 로깅에 묶인다 | 링 + 버림 + 카운터, 생산자는 절대 블록 안 함 |
| 큐 깊이를 평균 비율로 정함 ("초당 프레임 < 처리 능력이니 1~2개면 충분") | 버스트 + 소비자 위의 간섭이 문제다. 평균으로는 안전해도 깊이 1 은 32개 유실 | 도착 곡선 vs 서비스 하한 (TODO 3) → 3 |
| 지우기 typ(45 ms) 로 링 크기 계산 | 최악 400 ms 에서 넘친다. 그리고 처리량 자체가 부족할 수 있다 | max 로 계산 + 지속 가능성 검사 (TODO 4) |
| 워치독을 SysTick 훅/타이머 ISR 에서 킥 | tick 은 태스크가 죽어도 돈다 → 아무것도 못 잡는다 (Part C 5) | 생존 비트 전부 확인한 모니터만 킥 |
| 모니터가 비트를 안 지움 | 한 번 체크인한 태스크가 영원히 살아 있는 것으로 보인다 | 판정 후 `alive = 0` |
| 체크인을 `alive |= bit` 로 (원자성 없이) | 모니터(prio 5)가 RMW 중간에 끼어들어 지운 뒤 태스크가 옛 값을 다시 쓰면 다른 태스크의 비트가 되살아나 한 창 늦게 잡는다 | `taskENTER_CRITICAL` 안에서, 또는 `atomic_fetch_or` (LDREX/STREX) |
| 블록하는 태스크가 타임아웃 없이 기다림 (`portMAX_DELAY`) | 명령이 안 오는 정상 상황에서 체크인을 못 해 **거짓 리셋** | 10 ms 타임아웃마다 깨어 체크인 |
| 지터를 빼먹은 RTA | 브리지/속도는 1 ms 경계보다 늦게 READY 가 된다. 지터를 빼면 R 이 60 µs 작게 나온다 | `propagate_jitter`: J = 앞 단계의 R |

## 8. 면접 Q&A

<details>
<summary>Q1. 10 kHz 전류 루프를 태스크로 돌리겠습니까, ISR 로 돌리겠습니까?</summary>

ISR 이다, 단 **BASEPRI 위(NVIC 1)** 에. 이유는 지터다: 그 자리면 RTOS 의 어떤 임계 구역도 막지 못해 R = C = 25 µs 가 고정이다. 태스크로 돌리면 ① 매 주기 문맥 전환 2번 = 초당 2만 번, ② ADC ISR → 알림 → 태스크 경로의 지터(J 10 µs), ③ 커널 임계 구역 블로킹(B 4 µs) 이 붙어 R 이 48 µs 까지, 즉 25~48 µs 사이에서 흔들린다 (연습 1: 문맥 전환이 6 µs 를 넘으면 60 µs 데드라인도 깨진다). 대가: NVIC 1 ISR 은 커널 API 를 못 부르므로 속도 태스크를 깨우는 다리(SWI)가 필요하고, 그 ISR 안에서 쓰는 데이터(속도 명령)는 커널 락이 아니라 **잠금 없는 방식** 으로 넘겨야 한다 (이중 버퍼 + 시퀀스 번호, 또는 32비트 원자 쓰기). 꼬리: "그럼 언제 태스크가 맞나?" → 주기가 1 kHz 이하이고 C 가 길어 ISR 로 두면 다른 IRQ 를 오래 막을 때 — 이 시스템의 속도 루프가 그렇다.

</details>

<details>
<summary>Q2. configMAX_SYSCALL_INTERRUPT_PRIORITY 위의 ISR 에서 태스크를 어떻게 깨웁니까?</summary>

직접은 못 깨운다 — `vTaskNotifyGiveFromISR` 첫 줄의 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` (tasks.c:8344) 가 Cortex-M4F 포트에서 `vPortValidateInterruptPriority()` 로 현재 IPSR 의 우선순위가 `ucMaxSysCallPriority` 이상(숫자)인지 assert 한다 (port.c:851–888). 방법: ① 쓰지 않는 IRQ 하나를 NVIC 5 이상으로 설정하고 전류 ISR 이 `NVIC->STIR` 로 펜딩 → 그 ISR 이 `vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR` (이 레슨의 브리지, C 2 µs, 지터 전파 J 25 → 60). ② 속도 태스크를 `xTaskDelayUntil` 로 독립 주기 실행 — 단순하지만 전류 루프와 위상이 어긋나고 두 클럭이 미세하게 다를 수 있다. ③ 전류 ISR 을 NVIC 5 로 내림 — API 직접 호출 가능, 대신 B 만큼 지터. 꼬리: "브리지 펜딩이 겹치면?" → NVIC 펜딩 비트는 하나라 합쳐지고 알림도 한 번만 간다 (`bridge_lost`, 정상 설계에서 0). 1 ms 에 한 번이라 여유가 크다. 반면 브리지는 제때 줬는데 **속도 태스크가** 한 주기를 놓치면 알림 값이 2 가 되어 셀 수 있다 (`spd_merged`). 두 경우 모두 잡으려면 전류 ISR 이 주기 번호를 공유 변수에 쓰고 속도 태스크가 그걸 비교한다.

</details>

<details>
<summary>Q3. 우선순위는 어떤 근거로 정했습니까? RM 그대로입니까?</summary>

ISR 은 데드라인 순(DM): 전류 60 µs → NVIC 1, CAN 은 HW FIFO 3개 × 111 µs = 333 µs → 5, 브리지·tick 1 ms → 6, 15. 태스크는 RM 순 speed(1 ms) > can_cmd(버스트, 응답 ms 단위) > log_flush(배경). 예외 하나: 워치독 모니터(20 ms)를 맨 위에 뒀다. C 가 10 µs 라 speed 의 R 에 10 µs 만 더하고, 킥 시각의 흔들림이 38 µs 로 작아져 윈도우 워치독을 쓸 수 있으며, 높아도 굶는 태스크는 비트로 잡힌다. RM 은 독립 주기 태스크 + D = T 에서만 최적이고 (Liu & Layland 1973), 블로킹·지터가 있으면 DM 도 최적이 아닐 수 있다 (RTOS-M11 Q). 결국 **RTA 로 확인한 우선순위 배정** 이 근거다. 꼬리: "can_cmd 를 speed 위로 올리면?" → 버스트 하나(16 × 30 = 480 µs, 50 ms 마다)를 한 작업으로 근사해 RTA 를 돌리면 R_speed = **995 µs** — 1 ms 에 5 µs 남는다 (이 레슨 코드로 계산, 근사 모델). 버스트 처리와 그 사이 ISR 간섭이 통째로 speed 앞에 끼기 때문이다. 명령 응답을 빨리 하고 싶으면 프레임 처리를 짧게(파싱·검증만) 하고 적용은 speed 루프에서 한다.

</details>

<details>
<summary>Q4. CAN 큐 깊이 3 은 어떻게 나왔습니까? 이 분석이 틀리는 경우는요?</summary>

도착 곡선 A(t) (16 프레임, 111 µs 간격) 와 소비자의 서비스 하한 β(Δ) = ⌊(Δ − B − I(Δ))/30⌋ (I = ISR 4개·모니터·speed 의 최악 간섭) 의 수직 거리 최댓값이 3, 수평 거리(마지막 처리 완료)가 1950 µs. 시뮬레이션 19 버스트에서 관측 최대 2개, 1929 µs. 틀리는 경우: ① **도착 가정 위반** — 호스트가 버스트 규칙을 어기거나 버스에 고장 노드가 있어 필터를 통과하는 프레임이 계속 오면(babbling) 어떤 깊이도 넘친다 → 하드웨어 필터 + 큐 가득 시 버림 카운터 + 상위 프로토콜 재전송. ② 소비자가 프레임 처리 중 **블록** (뮤텍스, 플래시) 하면 β 가 무너진다. ③ C 가 측정값이라 WCET 가 아닐 때 (RTOS-A03). ④ 위쪽 태스크가 늘면 — speed 가 300 µs 면 깊이 8. 깊이 3 은 48 B 라 실무에서는 2배쯤 잡고 **최대 점유를 계측해 텔레메트리로 보낸다** (`q_max`) — 분석 가정이 현장에서 맞는지 확인하는 장치다.

</details>

<details>
<summary>Q5. 플래시 섹터 지우기가 수백 ms 걸리는데 로깅이 제어를 방해하지 않게 하려면?</summary>

세 가지. ① **아무도 플래시를 기다리지 않게** — 생산자는 링에 넣고 끝(가득 차면 버림 + 카운트), flush 는 최하위 우선순위에서 명령만 내고 1 tick 씩 잠들며 상태를 폴링한다. 임계 구역은 링에서 꺼내는 4 µs 조각뿐 (B = 4). ② **크기**: 링 ≥ 페이지 1장 + 최악 정지(지우기 400 ms + 쓰기 + 폴링) 동안 도착 = 218 레코드. ③ **처리량**: 섹터 한 바퀴(464 ms) 동안 도착 ≤ 256 이어야 한다 — 1 kHz 로 매번 로그하면 링 크기와 상관없이 지속 불가. 그래서 100 Hz 로 줄였다. 꼬리: "내부 플래시라면?" → 단일 뱅크 STM32 류는 지우기/쓰기 동안 같은 뱅크에서의 명령 페치가 멈춘다 (칩 RM 기준) → NVIC 1 ISR 도 멈춘다. 전류 ISR·벡터 테이블을 RAM 에 두거나 듀얼 뱅크 RWW 를 쓴다. 이 레슨의 분석은 외부 SPI NOR 가정이다.

</details>

<details>
<summary>Q6. 워치독은 어디서 킥합니까? 태스크가 멈추면 얼마 안에 리셋됩니까?</summary>

감시할 태스크마다 비트를 주고, 각자 한 바퀴 돌 때 체크인한다 (블록하는 태스크는 타임아웃으로 주기적으로 깨어 체크인). 모니터 태스크가 20 ms 마다 **모든 비트가 찼을 때만** 킥하고 비트를 지운다. tick 훅이나 타이머 ISR 에서 킥하면 tick 이 도는 한 아무것도 못 잡는다 (Part C 5: 속도 루프가 죽은 채 600 ms 리셋 없음). 상한은 P_mon + R_mon + W = 20 + 0.048 + 100 = 120.048 ms — 마지막 체크인이 판정 직후였다면 다음 판정에서 한 번 더 킥되기 때문이다. 관측 119.738 ms. 꼬리: "어느 태스크였는지는?" → 판정마다 `missing` 을 noinit RAM 에 남기고, 리셋 후 부팅 때 RCC 리셋 원인 플래그(IWDG)와 함께 읽어 로그로 보낸다 (이 레슨: 0x1/0x2/0x4/0x7 로 정확히 식별).

</details>

<details>
<summary>Q7. 속도 태스크가 죽으면 워치독이 120 ms 뒤 리셋합니다. 그동안 모터는 어떻게 됩니까? 어떻게 바꾸겠습니까?</summary>

전류 루프는 RTOS 와 독립적으로 정시에 돈다 (Part C 4·5: cur miss 0) — 즉 **마지막 속도 명령(전류 기준값)으로 계속 토크를 낸다.** 120 ms 동안 폭주하거나, tick 훅 킥이면 무한정. 바꿀 것: 전류 ISR 안에 속도 명령의 **나이 검사** — 속도 태스크가 쓸 때마다 시퀀스 번호를 올리고, 전류 ISR 이 N 주기(예: 30 주기 = 3 ms) 동안 안 바뀌면 전류 기준을 0 으로 램프하거나 PWM 출력을 끈다(안전 토크 차단). 하드웨어 쪽: 타이머의 브레이크 입력(BKIN)으로 과전류·고장 시 PWM 을 즉시 끄는 경로는 소프트웨어와 무관하게 둔다. 워치독은 "소프트웨어가 전부 멈춘" 경우의 최후 수단이고, 안전 상태 진입 시간 요구치가 있다면 그걸로 P_mon·W 를 역산한다.

</details>

<details>
<summary>Q8. 이 RTA 결과(속도 루프 R = 316 µs)를 믿어도 됩니까? 분석이 틀리는 경우는요?</summary>

모델 안에서는 안전하다 — 시뮬레이션 1000 작업의 최대 248 µs ≤ 316. 하지만 모델 밖에서 틀릴 수 있다. ① **C 가 측정값** — 캐시/플래시 대기 상태, 입력 경로에 따라 최악이 숨어 있다 (RTOS-A03). ② **PRIMASK 구간** — 벤더 HAL 이나 라이브러리의 `__disable_irq()` 는 NVIC 1 까지 막는다. 전류 ISR 의 B = 0 은 "코드 전체에 PRIMASK 가 없다" 는 가정이다 → grep 과 리뷰로 확인. ③ **문맥 전환 비용** 을 C 에 안 넣었다 — 작업당 2번 (M11 연습 2). ④ tick ISR 비용이 고정이 아니다 — 같은 tick 에 많이 깨면 `xTaskIncrementTick` 이 길어진다. ⑤ DMA 버스 경합, 플래시 대기. ⑥ ISR 최소 간격 가정 위반 (CAN babbling). ⑦ 반대로 지터를 J 와 간섭에 이중으로 세서 **비관적** 이다. 그래서 보드에서 GPIO 토글 + 로직 분석기, 또는 트레이스(RTOS-A06)로 R 분포를 재고, 분석과 실측의 차이가 크면 어느 가정이 깨졌는지 찾는다.

</details>

<details>
<summary>Q9. 태스크 스택 크기와 RAM 예산은 어떻게 말합니까?</summary>

RTOS-M08 의 방법: 태스크별 최악 호출 경로(`-fstack-usage` + 호출 그래프) + 문맥 프레임 + 여유. Cortex-M4F + FreeRTOS 에서 FP 를 한 번이라도 쓰는 태스크는 문맥이 208 B (예외 프레임 104 + 정렬 4 + PendSV 의 r4–r11·r14·s16–s31 100), 정수 태스크는 72 B. ISR 은 MSP 에서 도니 태스크엔 첫 프레임만, 중첩 사슬은 MSP 하나에 (여기선 456 B). 여유 25 %/최소 64 B 를 더해 speed 664 B, 합계 1912 B + MSP 456 + 큐 48 + 로그 링 3584 + 페이지 버퍼 256 ≈ 6.2 KB. 보드에서 최악 시나리오(버스트 + 지우기 + 최대 속도)를 돌린 뒤 `uxTaskGetStackHighWaterMark` 로 교차 확인하고, `configCHECK_FOR_STACK_OVERFLOW` 를 켜 둔다 (RTOS-M09). 꼬리: "가장 큰 RAM 항목은?" → 로그 링. RAM 이 부족하면 로그 비율을 줄이거나 지우기 일시정지 지원 플래시로 링을 50 레코드까지 줄인다 (연습 4).

</details>

## 9. 추가 연습

### 연습 1 — 전류 루프를 태스크로 옮기면

전류 루프를 최상위 태스크(prio 6)로, ADC 완료 ISR(NVIC 5, 2 µs)이 알림을 주게 바꾼다. 태스크의 C = 25 + 2·cs (매 주기 문맥 전환 2번). RTA 로 R 을 구하고, D = 60 µs 를 지키는 최대 cs 를 찾아라.

<details>
<summary>답 보기</summary>

J = ADC ISR 응답 = 2 + B 4 + 같은 레벨 CAN ISR 4 = **10 µs**. cs = 0 이면 B = 4 (이제 임계 구역에 막힌다), w = 25 + 4 + ADC 2 + CAN 4 + tick 3 = 38 → R = 38 + 10 = **48 µs**. 지터는 0 → 최대 23 µs. 최대 cs = **6 µs** (7 이면 R = 62 > 60). PendSV 전환은 보통 1 µs 안팎이라(170 MHz 에서 수백 사이클, 미측정) 수치상 들어가긴 하지만, 지터와 초당 2만 번의 전환 비용, "커널 버그 = 전류 루프 정지" 결합이 남는다. ISR 판이 이긴다.

</details>

### 연습 2 — 윈도우 워치독과 모니터 우선순위

WWDG 는 창이 열리기 전에 킥해도 리셋이다. 모니터가 `xTaskDelayUntil` 로 P = 20 ms 마다 출시되고 응답이 R_min(= C = 10 µs) ~ R_max 사이일 때, 연속 킥 간격 범위를 구하고 창 [19.9, 20.1] ms 를 견디는지 모니터 prio 5 와 prio 2 에서 비교하라 (can_cmd 는 버스트 하나 = 480 µs 작업 / 50 ms 로 근사).

<details>
<summary>답 보기</summary>

간격 ∈ [P − (R_max − R_min), P + (R_max − R_min)]. prio 5: R_max = 48 µs → ±38 µs → 창 통과. prio 2: R_max = **935 µs** (speed·can_cmd 간섭) → ±925 µs → [19.9, 20.1] 실패, [19, 21] 은 통과. 모니터를 높이 두는 실익이 이것이다 — 킥 시각이 결정적이 된다.

</details>

### 연습 3 — CAN 비트레이트별 큐 깊이

125 k / 250 k / 500 k / 1 Mbit/s 에서 최단 프레임 간격(111 비트)과 CAN ISR 주기를 바꿔 필요한 깊이를 구하라.

<details>
<summary>답 보기</summary>

간격 888 / 444 / 222 / 111 µs → 깊이 **1 / 1 / 2 / 3**. 느린 버스에서는 소비자(30 µs)가 프레임 간격 안에 거의 끝내 쌓일 일이 없다. 1 Mbit/s 에서도 깊이가 버스트 길이(16)보다 훨씬 작은 이유는 소비자가 도착률(1/111 µs)보다 빨리 처리하기 때문이다 — 쌓이는 건 "speed 가 도는 150 µs + ISR" 동안뿐이다.

</details>

### 연습 4 — 로그 링: typ / max / 지우기 일시정지

같은 생산자로 지우기 typ 45 ms, max 400 ms, 그리고 "지우기 일시정지(erase suspend)로 페이지 쓰기를 끼워 넣어 정지가 쓰기 1번 + 2 tick 뿐" 인 경우의 링 크기를 구하라. 또 최대 지우기 시간에서 지속 가능한 속도 루프 로그 간격의 최솟값은?

<details>
<summary>답 보기</summary>

typ **54**, max **218**, 일시정지 **50** 레코드 (CAN 버스트 16개 × 2 가 바닥을 만든다). 최대 지우기에서 지속 가능하려면 속도 루프는 **6번에 1번** 이하로 로그해야 한다 (45 ms 기준이면 매번도 가능). 데이터시트의 typ 로 크기를 잡으면 현장의 느린 부품·고온에서 넘친다 — max 로 잡거나 일시정지/FRAM 으로 가정을 바꾼다. 일시정지 지원 여부와 명령은 부품마다 다르다 (데이터시트 확인).

</details>

### 연습 5 — 민감도: 속도 루프를 얼마나 무겁게 할 수 있나

C_speed 를 이분 탐색해 ① 모든 RTA 가 통과하는 최댓값, ② 거기에 더해 CAN 큐 깊이 3 을 유지하는 최댓값을 구하라.

<details>
<summary>답 보기</summary>

① **635 µs** (그 이상이면 speed 자신이 1 ms 를 못 지킨다), ② **169 µs**. 이 시스템의 진짜 제약은 속도 루프 자신의 데드라인이 아니라 **그 아래 태스크 앞의 버퍼** 다. 속도 루프에 관측기를 추가하는 변경이 들어오면 RTA 만 다시 돌리는 게 아니라 큐 깊이도 다시 유도해야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice -v   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 11/11, 보고 없음
```

실제 출력 (`-v`):

```text
ex1: cur loop as task: J 10 R 48 (C 25) -> jitter up to 23 us; max context switch 6 us
ex2: monitor R_max prio5 48 us, prio2 935 us
ex3: depth 125k 1 250k 1 500k 2 1M 3
ex4: ring typ 54 max 218 suspend 50 records; min decimation @400ms 6, @45ms 1
ex5: max C_speed by RTA 635 us, keeping CAN depth <= 3: 169 us
11/11 passed
```

## 10. 핵심 정리

- 설계 답은 순서로 말한다: 요구사항 → 타이밍 예산 → 분할 → 우선순위 → IPC → 자원·스택 → 검증 → 실패 모드. 결정마다 숫자 하나.
- 전류 루프는 **BASEPRI 위 ISR** (R = C, 지터 0). 대가는 커널 API 금지 → 브리지 SWI 로 태스크를 깨운다. 지터는 ISR → SWI → 태스크로 전파된다 (J_speed = R_bridge = 60).
- 임계 구역 길이가 곧 B 다. 플래시 완료를 임계 구역 안에서 기다리는 드라이버 하나가 속도 루프·CAN·tick·워치독을 한꺼번에 깨뜨린다.
- 큐 깊이 = 도착 곡선과 서비스 하한의 최대 거리 (16 프레임 버스트 → 3). 평균 비율로 정하면 안 된다. 분석은 시뮬레이션/실측과 대조해야 틀린 걸 안다.
- 로그 링 = 페이지 1장 + 최악 지우기 동안 도착 (218). 그 전에 처리량: 최악 지우기 시간이 지속 가능한 로그 비율을 제한한다.
- 워치독은 생존 비트를 다 확인한 모니터만 킥, 판정 후 비트 지움. 리셋 상한 = P_mon + R_mon + W (120 ms) — 그 전에 전류 ISR 의 명령 나이 검사로 토크를 끈다.
- 분석이 틀리는 곳: 측정 C, 숨은 PRIMASK, 문맥 전환 비용, 도착 가정 위반, 내부 플래시 정지.

## 참고자료

- FreeRTOS-Kernel V11.3.2 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` — `xTaskDelayUntil` 2414–2498, `ulTaskGenericNotifyTake` 7797 (값 7867, clear 7873, −1 7877), `vTaskGenericNotifyGiveFromISR` 8315 (우선순위 검사 8344, ++ 8358, 대기 해제 8364, yield 8399–8411); `queue.c` — `xQueueGenericSendFromISR` 1167, 1207, 1327, `xQueueReceive` 1509; `include/task.h` 3196 (`vTaskNotifyGiveFromISR`), 3302 (`ulTaskNotifyTake`); `include/stream_buffer.h` 35–44 (단일 writer/reader); `portable/GCC/ARM_CM4F/portmacro.h` 88–97 (`portYIELD` = PENDSVSET), 101–114, 120–122, 179, 213–245 (`vPortRaiseBASEPRI`); `port.c` 370–412 (`ucMaxSysCallPriority`), 851–888 (`vPortValidateInterruptPriority`) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS, "Running the RTOS on a ARM Cortex-M Core" (configMAX_SYSCALL_INTERRUPT_PRIORITY, 우선순위 숫자 방향) — <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- N. Audsley, A. Burns, M. Richardson, K. Tindell, A. Wellings, "Applying new scheduling theory to static priority pre-emptive scheduling", Software Engineering Journal, 1993 (지터·블로킹 포함 RTA)
- L. Sha, R. Rajkumar, J. Lehoczky, "Priority inheritance protocols: an approach to real-time synchronization", IEEE Trans. Computers, 1990
- K. Tindell, J. Clark, "Holistic schedulability analysis for distributed hard real-time systems", Microprocessing and Microprogramming, 1994 (지터 전파 — 서지 정보는 기억 기준)
- C. L. Liu, J. Layland, "Scheduling algorithms for multiprogramming in a hard-real-time environment", JACM, 1973
- J.-Y. Le Boudec, P. Thiran, *Network Calculus: A Theory of Deterministic Queuing Systems for the Internet*, Springer, 2001 (도착 곡선 / 서비스 곡선 / min-plus)
- R. Davis, A. Burns, R. Bril, J. Lukkien, "Controller Area Network (CAN) schedulability analysis: refuted, revisited and revised", Real-Time Systems, 2007 (최악 프레임 길이 — COMM-M04)
- ARM, *Armv7-M Architecture Reference Manual* (DDI0403) — 예외 진입 프레임, BASEPRI, FPCA; CMSIS-Core `NVIC_SetPendingIRQ`, STIR
- Winbond W25Q 계열 SPI NOR 데이터시트 — 페이지 쓰기 / 4 KB 섹터 지우기 typ·max (부품마다 다름, 이 레슨의 3 ms / 400 ms 는 대표값으로 사용, 미검증); STM32 RM — IWDG, bxCAN/FDCAN RX FIFO, 플래시 지우기 중 페치 정지 (문서 기준)
- 관련 레슨: RTOS-M01 (알림), RTOS-M02 (ISR 지연 처리, portYIELD_FROM_ISR), RTOS-B11/M06 (큐), RTOS-M08 (스택 예산), RTOS-M09 (오버플로 탐지), RTOS-M11 (RM/RTA, Part C 의 같은 모터 시나리오), RTOS-A03 (WCET 측정), RTOS-A04 (확장 RTA), RTOS-A06 (트레이스 포맷), RTOS-A07 (Zephyr), RTOS-A08 (SMP), RTOS-A09 (데드라인 미스 사후 분석), COMM-M04/M07 (CAN 프레임 길이, 오류 처리), BM-M11 (저전력)
