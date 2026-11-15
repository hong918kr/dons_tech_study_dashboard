---
id: RTOS-A04
time: 110분
updated: 2026-11-11
---
# RTOS-A04 확장 RTA: 블로킹 시간·릴리즈 지터·ISR 부하 반영

## 학습 목표

- ISR 을 "모든 태스크보다 높은 산발적(sporadic) 태스크" 로, `taskENTER_CRITICAL` 을 "천장이 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 인 자원" 으로 모델링해 **ISR 과 태스크를 한 우선순위 축** 에서 분석할 수 있다.
- IPCP 천장으로 블로킹 항 `B_i` 를 구하고, 확장식 `w = C + B + Σ⌈(w + J_j)/T_j⌉C_j`, `R = w + J_i` 로 ISR 까지 포함한 응답 시간을 정수로 계산한다.
- 분석과 코드를 공유하지 않는 **이산 시간 시뮬레이터** (IPCP 자원 · 지터 · ISR 포함)와 **최악 출시 패턴 생성기** 를 짜서, "분석 ≥ 관측" (안전) 을 무작위 + 적대적 패턴으로 확인한다.
- RTOS-M11 의 기본 RTA (B·J·ISR 없음) 가 **실제로 위험한** 시스템을 보여 주고, 반대로 확장 분석이 **비관적** 인 경우(자기 천장이 상위 ISR 을 가림, 이산 시간 ε)를 숫자로 설명한다.
- "분석은 통과했는데 보드에서 미스" 가 나면 어디부터 의심할지 (모델 밖 가정 목록) 말할 수 있다.

## 1. 문제

스태프급 펌웨어 면접의 RTOS 질문은 "RTA 를 아나요?" 가 아니라 **"RTA 는 통과했는데 보드에서 CAN FIFO 가 넘쳤습니다. 분석에서 뭘 빠뜨렸죠?"** 다. 정답은 거의 항상 세 개다 — ISR 부하, 공유 자원·커널 임계 구역의 **블로킹**, 그리고 ISR → 태스크 경로의 **릴리즈 지터**. 이 셋을 넣은 분석을 손으로 할 수 있고, 그 분석이 정말 안전한지 **시뮬레이터로 깨 보려고 시도** 할 수 있어야 한다.

RTOS-M11 은 기본 RTA 를 만들었고 Part C 에서 B, J 가 들어간 식을 소개했다 (블로커를 t=0 에 하나 두는 시뮬레이션까지). 이번 레슨은 그 도구를 실제 펌웨어 쪽으로 넓힌다.

| | RTOS-M11 | **RTOS-A04 (이 레슨)** |
|---|---|---|
| 분석 대상 | 태스크 | 태스크 **+ ISR** (최소 도착 간격 T 의 최상위 산발적 entity) |
| B | 사용 표에서 IPCP max | 같은 규칙 + **커널 임계 구역 = ISR 레벨 천장** → ISR 도 B 를 받는다 |
| 시뮬레이터 | 블로커 1개를 t=0 에 고정 | 진짜 자원 잠금/해제 · IPCP 천장 · 지터 · 산발적 ISR · 작업 FIFO |
| 검증 | 동시 출시 1 패턴 == R | 무작위 수백 회 + **entity 마다 최악 패턴**, 무작위 시스템 150 개 |
| 질문 | "R 은 얼마?" | "이 R 은 **안전한가**, 얼마나 **비관적인가**, 모델 밖에는 뭐가 있나?" |

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 줄로 세운 우선순위 — ISR, 커널 임계 구역, 뮤텍스 천장이 같은 축 위에 (Part C)</text>
  <line x1="30" y1="350" x2="30" y2="40" stroke="currentColor" stroke-width="2"/>
  <polygon points="30,34 25,46 35,46" fill="currentColor"/>
  <text x="38" y="44" fill="currentColor" font-size="12">급함</text>
  <rect x="50" y="52" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="60" y="72" fill="currentColor">adc ISR (15) 20 kHz · API 금지</text>
  <line x1="40" y1="96" x2="720" y2="96" stroke="currentColor" stroke-width="2" stroke-dasharray="8 4"/>
  <text x="320" y="91" fill="currentColor" font-size="12">BASEPRI 선 = configMAX_SYSCALL_INTERRUPT_PRIORITY (12)</text>
  <text x="588" y="122" fill="currentColor" font-size="12">= KERNEL 천장</text>
  <rect x="50" y="104" width="250" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="60" y="124" fill="currentColor">can ISR (12) · xQueueSendFromISR</text>
  <rect x="50" y="140" width="250" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="60" y="160" fill="currentColor">systick ISR (11) · 최저 ISR</text>
  <text x="60" y="190" fill="currentColor" font-size="12">(PendSV 도 최저 ISR — 전환 비용은 연습 3)</text>
  <line x1="40" y1="202" x2="720" y2="202" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="320" y="197" fill="currentColor" font-size="12">스레드 모드 · SCHED 천장 4</text>
  <rect x="50" y="210" width="250" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="60" y="229" fill="currentColor">ctrl (4) 1 kHz 속도 루프</text>
  <rect x="50" y="244" width="250" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="60" y="263" fill="currentColor">imu (3) · SPI 천장 = 3</text>
  <rect x="50" y="278" width="250" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="60" y="297" fill="currentColor">comms (2)</text>
  <rect x="50" y="312" width="250" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="60" y="331" fill="currentColor">log (1) · 플래시 기록</text>
  <line x1="300" y1="326" x2="580" y2="326" stroke="currentColor"/>
  <line x1="580" y1="326" x2="580" y2="100" stroke="currentColor" stroke-width="3"/>
  <polygon points="580,98 574,110 586,110" fill="currentColor"/>
  <text x="588" y="140" fill="currentColor" font-size="12">taskENTER_CRITICAL</text>
  <text x="588" y="156" fill="currentColor" font-size="12">45 µs → can, systick,</text>
  <text x="588" y="172" fill="currentColor" font-size="12">모든 태스크를 막는다</text>
  <line x1="430" y1="326" x2="430" y2="248" stroke="currentColor" stroke-width="3"/>
  <polygon points="430,246 424,258 436,258" fill="currentColor"/>
  <text x="438" y="280" fill="currentColor" font-size="12">SPI 500 µs →</text>
  <text x="438" y="296" fill="currentColor" font-size="12">imu, comms 를 막는다</text>
  <text x="320" y="128" fill="currentColor" font-size="12">B_can = 45 (log) → R 66 &gt; D 40</text>
  <text x="320" y="72" fill="currentColor" font-size="12">B_adc = 0: 어떤 임계 구역도 못 막는다</text>
  <text x="10" y="372" fill="currentColor" font-size="12">B_i = 나보다 낮은 entity 의 cs 중 천장 ≥ prio_i 인 것의 최댓값 하나 (IPCP) — ISR 도 같은 식.</text>
</svg>
```

**모델** (모든 시간은 정수 tick 또는 µs, 우선순위는 클수록 급함 — FreeRTOS 방향. NVIC 숫자와 Zephyr 는 반대):

| 필드 | 의미 |
|---|---|
| `ent_t {is_isr, prio, C, T, D, J}` | 태스크 또는 ISR. T = 주기 또는 **최소 도착 간격**, `C ≤ D ≤ T`, `J < T`. ISR 의 prio 는 모든 태스크보다 높아야 한다 (`sys_check` 가 검사) |
| `cs_t {res, off, len}` | "실행량이 off 가 되면 res 를 잡고 len 동안 쥔다". entity 당 최대 3개, 겹치지 않음 (중첩 없음) |
| 자원 `rmin` | 선언된 최소 천장. `KERNEL` = SYSCALL 레벨 (taskENTER_CRITICAL), `SCHED` = 최상위 태스크 (vTaskSuspendAll), 일반 뮤텍스 = 0 |
| 지터 J | 도착(arrival, 데드라인 기준)부터 출시(release, 실행 가능) 까지 0..J. 응답 = 완료 − **도착** |

### Part A — 천장, B, 확장 RTA, 최악 패턴 (손으로 따라가는 작은 시스템)

- `compute_ceilings(s)` — **TODO 1.** `ceil[r] = max(rmin[r], r 을 쓰는 entity 의 prio)`.
- `blocking_ipcp(s)` — **TODO 2.** `B_i = max{len : 주인 prio < prio_i, ceil[res] ≥ prio_i}`. ISR 도 똑같이.
- `rta_ext(s, i)` — **TODO 3.** `w0 = C + B`, `w = C_i + B_i + Σ_{prio_j > prio_i} ⌈(w + J_j)/T_j⌉C_j`. `w + J_i > D_i` 면 즉시 멈추고 `ok = 0`, 수렴하면 `R = w + J_i`.
- `sim_run(s, pattern, &obs, timeline)` — **TODO 4.** 1 단위씩: 출시 → 활성 우선순위(쥔 자원의 천장) 최고 선택 → 1 단위 실행 + 잠금/해제/완료 → 블로킹 시간 집계. IPCP 에서 잠금 충돌이 나면 `ipcp_conflicts++` (0 이어야 한다).
- `gen_critical(s, i, horizon, pattern)` — **TODO 5.** entity i 의 최악 출시 패턴: 가장 긴 블로커가 i 출시 직전에 자원을 잡고, i 와 모든 상위 entity 는 최대 지터로 동시에 출시, 이후 상위는 `T − J` 간격으로 가장 촘촘하게.
- 예제: isr1 (SYSCALL 위), isr2 (FromISR API, KERNEL 1), tH/tM/tL + 뮤텍스 R1. 확장 R = 1, 5, 15, 19, 27 / 순진 R(tH) = 4 인데 실제 최악 14.

### Part B — 순진한 분석이 깨지는 곳, 그리고 안전성 대량 검사

- 지터만으로: hi (C2, T10, J6) 위의 lo (C5, D9) — 순진 7, 실제 9. D = 8 로 줄이면 순진 "통과", 실제 **미스**.
- SYSCALL 위/아래 ISR: 커널 임계 구역 200 tick 이 있을 때 위쪽 ISR 의 B = 0, 아래쪽은 200 → 응답 205.
- 상위 이용률 ≥ 1 (ISR 포함) → 멈춤. 순진한 분석은 ISR 을 빼서 "여유 많음".
- 무작위 시스템 150 개 (ISR 1–2, 태스크 3–5, 자원 3, 무작위 임계 구역): 확장 분석 통과 → 무작위 10 회 + entity 별 최악 패턴. 관측 ≤ R, 관측 블로킹 ≤ B, IPCP 충돌 0, 그리고 최악 패턴 == 이산 시간 분석값.

### Part C — STM32 모터 드라이브: 순진 vs 확장 vs 시뮬레이션

| entity | 종류 / prio | C | T | D | J | 임계 구역 |
|---|---|---|---|---|---|---|
| adc | ISR 15 (SYSCALL 위) | 6 | 50 | 50 | 0 | 없음 (API 금지) |
| can | ISR 12 (= SYSCALL) | 9 | 160 (최소 간격) | 40 (HW FIFO) | 0 | KERNEL 3 (`xQueueSendFromISR` 내부) |
| systick | ISR 11 (최저) | 5 | 1000 | 1000 | 0 | KERNEL 5 (핸들러 전체) |
| ctrl | 태스크 4 | 120 | 1000 | 400 | 20 | PARAM 10, KERNEL 4 |
| imu | 태스크 3 | 200 | 2000 | 1000 | 30 | SPI 60 |
| comms | 태스크 2 | 300 | 5000 | 3000 | 0 | PARAM 15, KERNEL 6 |
| log | 태스크 1 | 1500 | 20000 | 20000 | 0 | **KERNEL 45** (링 버퍼 memcpy), **SPI 500** (플래시 페이지 쓰기) |

- C1: 순진한 RTA 는 전부 통과. 확장 분석은? 최악 패턴 시뮬레이션은 실제로 미스를 재현하나?
- C2 (고침): log 의 memcpy 를 `vTaskSuspendAll` (SCHED) 로, 플래시 쓰기를 100 µs 조각 둘로 → 전부 통과, 무작위 40 회 × 40 ms + 최악 패턴으로 "관측 ≤ R" 과 "최악 패턴 == 분석" 확인.

## 2. Problem (English)

> **Extended response-time analysis with blocking, release jitter and interrupts.**
>
> You get a single-core fixed-priority system: entities are tasks or ISRs (`C, T, D, J`, priority, larger = more urgent; every ISR is above every task; for an ISR, `T` is its minimum inter-arrival time). Entities lock resources in non-nested critical sections `(res, off, len)`. Resources follow the immediate priority ceiling protocol; a resource may declare a minimum ceiling (a kernel critical section that raises BASEPRI to `configMAX_SYSCALL_INTERRUPT_PRIORITY` is a resource whose ceiling is that interrupt level).
>
> 1. `compute_ceilings`, `blocking_ipcp` — `B_i` = longest critical section of a lower-priority entity on a resource whose ceiling ≥ `prio_i`.
> 2. `rta_t rta_ext(sys, i)` — iterate `w = C_i + B_i + Σ_{j: prio_j > prio_i} ⌈(w + J_j)/T_j⌉·C_j` from `w = C_i + B_i`; stop when `w + J_i > D_i`; `R_i = w + J_i`. Integers only.
> 3. `sim_run` — a discrete-time simulator sharing no code with the analysis (releases, ceilings, IPCP lock/unlock, per-job response from arrival, misses, observed blocking, IPCP conflicts).
> 4. `gen_critical(sys, i, ...)` — the release pattern that maximises entity *i*'s first response.
>
> Show that the analysis is safe (no observed response exceeds `R`) over random and adversarial patterns, and find a system where the basic RTA `R = C + Σ⌈R/T_j⌉C_j` over tasks only is unsafe.
>
> Example: ISRs (1,10) and (2,20) with a 1-tick kernel section; tasks tH (4,40,D20,J2), tM (6,60,D40), tL (10,120); tL holds R1 for 5 ticks. → `B = 0, 2, 5, 5, 0`, `R = 1, 5, 15, 19, 27`; basic RTA says tH = 4, the simulator observes 14.
>
> Follow-ups: Why can an ISR be blocked by a task? Which ISRs may sit above `configMAX_SYSCALL_INTERRUPT_PRIORITY`? Why is random testing not enough? When is the analysis pessimistic, and when is it wrong? How do you measure B and J on the board?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 15/57 passed, exit 1
./starter -v                                                                 # 표 · 타임라인 · 요약 출력
```

처음 상태에서는 **주어진 코드** 테스트 15개만 통과한다 (검사기, M11 식 순진한 RTA, 생성기 형식). 스텁 `sim_run` 은 아무 작업도 실행하지 않고, 스텁 `rta_ext` 는 `ok = 0` 이라 무작위 시스템 루프는 3000 개 씨드에서 그냥 끝난다 — 무한 루프 없음. 단일 스레드 · 결정적이라 `-pthread` · TSan 은 필요 없다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1·2: 천장은 "쓰는 entity 중 최고" 와 "선언값" 중 큰 쪽</summary>

`taskENTER_CRITICAL` 은 뮤텍스가 아니지만, 잡는 순간 BASEPRI 를 SYSCALL 레벨로 올려 그 레벨 이하를 전부 막는다 — 즉 **천장이 SYSCALL 레벨인 IPCP 자원** 과 똑같다. 그래서 `rmin` 을 두고 `max(rmin, 사용자 최고 prio)`. B 는 M05/M11 처럼 **합이 아니라 max**: IPCP 에서는 출시 전에 이미 자원을 쥔 하위 entity 가 많아야 하나다. 같은 우선순위 축이라 ISR 의 B 도 이 식 그대로 나온다.

</details>

<details>
<summary>힌트 2 — TODO 3: ISR 을 특별 취급하지 마라</summary>

ISR 은 그냥 prio 가 큰 entity 다. hp(i) = `prio_j > prio_i` (우선순위는 모두 다르다). M11 과 다른 점은 `+ B_i`, `+ J_j` (상위 지터), `+ J_i` (끝에) 세 개뿐. 멈춤 검사를 수렴 검사보다 **먼저**: `nw + J_i > D_i` 면 반환. 64비트로 누적하면 오버플로 걱정이 없다.

</details>

<details>
<summary>힌트 3 — TODO 4: 큐 없이 작업 FIFO</summary>

작업 목록은 출시 시각 오름차순이고 같은 entity 의 작업은 순서대로 끝나므로, 대기 작업 = 목록의 `[done, rel)` 구간이다. entity 마다 `exec` (현재 작업 실행량), `nextcs` (다음 임계 구역 번호), `hold` (쥔 cs 번호 또는 −1) 만 있으면 된다. 선택 키 = `2 × 활성 우선순위 + (쥐고 있으면 1)` — 천장 == 다른 entity 의 prio 인 동점에서 쥔 쪽이 이겨야 BASEPRI 의 "같은 레벨까지 막는다" 와 맞는다. 대기 작업이 없으면 다음 출시 시각으로 건너뛴다.

</details>

<details>
<summary>힌트 4 — TODO 5: 블로커는 "출시 1 tick 전에" 자원을 잡아야 한다</summary>

이산 시간에서 같은 시각에 출시된 i 와 블로커가 경쟁하면 i 가 이긴다 (블로커는 아직 자원을 안 쥐었다). 그래서 블로커 작업을 t=0 에 출시해 `off` 단위를 돌게 하고 t=off 에 잡게 한 뒤, 나머지를 `shift = off + 1` 에 출시한다. 그 결과 관측 가능한 최대 블로킹은 `B − 1` — 연속 시간의 `B − ε`. 상위 entity: 첫 도착 `shift − J` (출시 shift, 최대 지터), 이후 도착 = 출시 = `shift − J + kT`.

</details>

<details>
<summary>힌트 5 — 검증: 무엇과 같아야 하나</summary>

최악 패턴에서 i 의 첫 응답은 `B` 를 `B − 1` 로 바꾼 RTA (`rta_discrete`) 와 **정확히 같아야** 한다 (i 가 자기보다 높은 천장의 자원을 쥐지 않을 때). 무작위 패턴은 `≤ R` 이면 되고, 대개 그보다 꽤 작다 — 그게 이 레슨의 요점 중 하나다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 모델 | FreeRTOS V11.3.2 (Cortex-M3/M4 포트) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|
| ISR entity, prio > 모든 태스크 | 모든 예외는 스레드 모드보다 높다. NVIC 숫자는 **작을수록** 급함 | 같음 (`IRQ_CONNECT` 우선순위) | 같음 (CMSIS-Core `NVIC_SetPriority`) |
| KERNEL 자원, 천장 = SYSCALL 레벨 | `taskENTER_CRITICAL()` → `portENTER_CRITICAL()` (task.h:241, ARM_CM4F/portmacro.h:124) → `vPortEnterCritical` (port.c:475–489) → `vPortRaiseBASEPRI` 가 `msr basepri, configMAX_SYSCALL_INTERRUPT_PRIORITY` (portmacro.h:213–225) | `irq_lock()` (Cortex-M 은 BASEPRI, `CONFIG_ZERO_LATENCY_IRQS` 위는 안 막음) | — (커널 내부) |
| ISR 안의 KERNEL 구간 | `taskENTER_CRITICAL_FROM_ISR()` = `portSET_INTERRUPT_MASK_FROM_ISR()` (task.h:243, portmacro.h:120). 예: `xQueueGenericSendFromISR` (queue.c:1167) 가 1205–1330 동안 마스크 | 커널 API 내부 spinlock/irq_lock | — |
| SYSCALL 위 ISR (B = 0, API 금지) | `vPortValidateInterruptPriority` 가 `configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )` (port.c:851, 888) | zero-latency IRQ | — |
| systick: 최저 ISR, 핸들러 전체가 KERNEL | `xPortStartScheduler` 가 SHPR3 에 `portNVIC_PENDSV_PRI`, `portNVIC_SYSTICK_PRI` 를 OR (port.c:433–434, 값 255 = 최저, 65–67). `xPortSysTickHandler` 가 처음에 `portDISABLE_INTERRUPTS()` 후 `xTaskIncrementTick()` (port.c:560–570) | sys_clock ISR | — |
| SCHED 자원, 천장 = 최상위 태스크 | `vTaskSuspendAll()` — 단일 코어에서는 `uxSchedulerSuspended` 를 1 올릴 뿐 인터럽트는 안 막는다 (tasks.c:3890–3928). 그동안 ISR 이 깨운 태스크는 `xPendingReadyList` 로 (tasks.c:483, 5527–5550) | `k_sched_lock()` | `osKernelLock()` |
| IPCP 뮤텍스 (SPI, PARAM) | 없음 — FreeRTOS 뮤텍스는 우선순위 **상속** (RTOS-M04). 천장은 M05 처럼 `vTaskPrioritySet` 래퍼, PI 그대로 쓸 때의 B 는 연습 4 | `k_mutex` 는 상속 | `osMutexPrioInherit` (상속) |
| J (릴리즈 지터) | ISR → `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR` 경로 지연, 빠뜨리면 최대 1 tick (RTOS-M02) | ISR → `k_sem_give` | ISR → `osSemaphoreRelease` |

핵심 한 줄: **Cortex-M FreeRTOS 의 커널 임계 구역은 "BASEPRI = SYSCALL" 이라는 천장을 가진 자원이고, 그 천장은 태스크 영역을 넘어 ISR 영역까지 올라간다.** 그래서 어떤 태스크의 `taskENTER_CRITICAL` 45 µs 는 SYSCALL 이하 **모든 ISR 의 B** 가 된다. 반대로 SYSCALL 위 ISR 은 아무 임계 구역에도 안 막히는 대신 FreeRTOS API 를 부를 수 없다 (RTOS-M02 연습 4).

### Part A — 천장과 B: 표 한 장

`build_a` 시스템 (단위 tick):

| entity | prio | C | T | D | J | 임계 구역 |
|---|---|---|---|---|---|---|
| isr1 | 10 | 1 | 10 | 10 | 0 | — (SYSCALL 위) |
| isr2 | 9 | 2 | 20 | 10 | 0 | KERNEL off 0 len 1 |
| tH | 3 | 4 | 40 | 20 | 2 | R1 off 1 len 2 |
| tM | 2 | 6 | 60 | 40 | 0 | KERNEL off 2 len 2 |
| tL | 1 | 10 | 120 | 120 | 0 | R1 off 3 len 5, KERNEL off 9 len 1 |

천장: KERNEL = max(rmin 9, isr2 9, tM 2, tL 1) = **9**, R1 = max(tH 3, tL 1) = **3**.

| entity | 막을 수 있는 하위 cs (천장 ≥ prio) | B |
|---|---|---|
| isr1 (10) | 없음 — 천장 10 이상인 자원이 없다 | **0** |
| isr2 (9) | tM KERNEL 2, tL KERNEL 1 (isr2 보다 낮은 **태스크** 가 isr2 를 막는다) | **2** |
| tH (3) | tM KERNEL 2, tL R1 5, tL KERNEL 1 | **5** |
| tM (2) | tL R1 5 (tM 은 R1 을 안 쓰지만 천장 3 ≥ 2 → push-through), tL KERNEL 1 | **5** |
| tL (1) | 없음 | 0 |

### Part A — 확장 RTA 손계산

tH: `w0 = C + B = 4 + 5 = 9`

| k | w | isr1 ⌈w/10⌉·1 | isr2 ⌈w/20⌉·2 | 새 w (= 9 + …) |
|---|---|---|---|---|
| 1 | 9 | 1 | 2 | 12 |
| 2 | 12 | 2 | 2 | 13 |
| 3 | 13 | 2 | 2 | **13** 수렴 → R = 13 + J 2 = **15** ≤ 20 |

tL (`w0 = 10`, B = 0): `10 + isr1 + isr2 + tH ⌈(w+2)/40⌉·4 + tM ⌈w/60⌉·6` → 23 → 27 → 27, R = **27**. tH 의 지터 2 가 tH 행에서 `w + 2` 로 들어간다.

```text
== A: small system (ticks)
   name    isr prio      C      T      D    J     B |      R  it      | naive
   isr1    isr   10      1     10     10    0     0 |      1   1 ok   |   (ISR 무시)
   isr2    isr    9      2     20     10    0     2 |      5   2 ok   |   (ISR 무시)
   tH             3      4     40     20    2     5 |     15   3 ok   |      4 ok
   tM             2      6     60     40    0     5 |     19   2 ok   |     10 ok
   tL             1     10    120    120    0     0 |     27   3 ok   |     20 ok
```

순진한 M11 RTA 는 tH = 4 (자기 실행만), tM = 10, tL = 20. 전부 "통과" 지만 숫자가 3~4 배 낙관적이다. 이게 **위험** 한지는 시뮬레이터로 확인한다.

### Part A — 시뮬레이터와 최악 패턴

`sim_run` 의 한 tick (주석에 규칙을 그대로 적어 둠):

```c
/* (1) 출시 */  while (rel[e] < jb->n && jb->rel[rel[e]] <= t) rel[e]++;
/* (2) 선택 */  act = hold[e] >= 0 ? ceil[res] : prio;  key = 2 * act + (hold[e] >= 0);   /* 동점은 쥔 쪽 */
/* (3) 블로킹 집계 */ if (prio[e] > prio[best]) blk[e]++;            /* 나보다 낮은 entity 가 돌고 있다 */
/* (4) 실행 */  if (exec == cs.off) lock;  exec++;  if (exec == off + len) unlock;  if (exec == C) 완료;
```

분석식과 공유하는 것이 **하나도 없다** — 천장 표(`ceil[]`)만 입력으로 받는다. 그래서 `ipcp_conflicts == 0` 은 "IPCP 에서는 잠금이 절대 막히지 않는다" (RTOS-M05 Part A) 를 독립적으로 확인하는 불변식이 된다.

`gen_critical(s, tH, …)` 이 만든 패턴과 실제 실행 (`./solution -v`):

```text
-- A: critical pattern for tH (shift 4)
 tick   : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
 isr1   : . . . . R . . . . . . . . . R . . . . . . . . . R . . . . . . . . . R . . . . .
 isr2   : . . . . r C R . . . . . . . . . . . . . . . . . r C R . . . . . . . . . . . . .
 tH     : . . . . r r r B B B B R C C r R . . . . . . . . . . . . . . . . . . . . . . . .
 tM     : . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 tL     : R R R C r r r C C C C r r r r r R C . . . . . . . . . . . . . . . . . . . . . .
```

(타임라인 기호: R 실행, **C 자원을 쥐고 실행**, r 더 높은 entity 에 선점당해 대기, **B 더 낮은 entity 때문에 대기 = 블로킹**, . 작업 없음.)

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">tH 의 최악 패턴 — sim_run 의 실제 타임라인 (solution -v, t = 0..17)</text>
  <g fill="currentColor" font-size="12"><text x="109" y="42">0</text><text x="143" y="42">1</text><text x="177" y="42">2</text><text x="211" y="42">3</text><text x="245" y="42">4</text><text x="279" y="42">5</text><text x="313" y="42">6</text><text x="347" y="42">7</text><text x="381" y="42">8</text><text x="415" y="42">9</text><text x="449" y="42">0</text><text x="483" y="42">1</text><text x="517" y="42">2</text><text x="551" y="42">3</text><text x="585" y="42">4</text><text x="619" y="42">5</text><text x="653" y="42">6</text><text x="687" y="42">7</text></g>
  <text x="10" y="68" fill="currentColor">isr1 (10)</text>
  <rect x="232" y="50" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="244" y="68" fill="currentColor" font-size="12">R</text><rect x="572" y="50" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="584" y="68" fill="currentColor" font-size="12">R</text>
  <text x="10" y="106" fill="currentColor">isr2 (9)</text>
  <rect x="232" y="88" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="244" y="106" fill="currentColor" font-size="12">r</text><rect x="266" y="88" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="278" y="106" fill="currentColor" font-size="12">C</text><rect x="300" y="88" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="312" y="106" fill="currentColor" font-size="12">R</text>
  <text x="10" y="144" fill="currentColor">tH (3)</text>
  <rect x="232" y="126" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="244" y="144" fill="currentColor" font-size="12">r</text><rect x="266" y="126" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="278" y="144" fill="currentColor" font-size="12">r</text><rect x="300" y="126" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="312" y="144" fill="currentColor" font-size="12">r</text><rect x="334" y="126" width="34" height="26" class="f-warn" stroke="currentColor"/><text x="346" y="144" fill="currentColor" font-size="12">B</text><rect x="368" y="126" width="34" height="26" class="f-warn" stroke="currentColor"/><text x="380" y="144" fill="currentColor" font-size="12">B</text><rect x="402" y="126" width="34" height="26" class="f-warn" stroke="currentColor"/><text x="414" y="144" fill="currentColor" font-size="12">B</text><rect x="436" y="126" width="34" height="26" class="f-warn" stroke="currentColor"/><text x="448" y="144" fill="currentColor" font-size="12">B</text><rect x="470" y="126" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="482" y="144" fill="currentColor" font-size="12">R</text><rect x="504" y="126" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="516" y="144" fill="currentColor" font-size="12">C</text><rect x="538" y="126" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="550" y="144" fill="currentColor" font-size="12">C</text><rect x="572" y="126" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="584" y="144" fill="currentColor" font-size="12">r</text><rect x="606" y="126" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="618" y="144" fill="currentColor" font-size="12">R</text>
  <text x="10" y="182" fill="currentColor">tL (1)</text>
  <rect x="96" y="164" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="108" y="182" fill="currentColor" font-size="12">R</text><rect x="130" y="164" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="142" y="182" fill="currentColor" font-size="12">R</text><rect x="164" y="164" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="176" y="182" fill="currentColor" font-size="12">R</text><rect x="198" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="210" y="182" fill="currentColor" font-size="12">C</text><rect x="232" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="244" y="182" fill="currentColor" font-size="12">r</text><rect x="266" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="278" y="182" fill="currentColor" font-size="12">r</text><rect x="300" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="312" y="182" fill="currentColor" font-size="12">r</text><rect x="334" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="346" y="182" fill="currentColor" font-size="12">C</text><rect x="368" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="380" y="182" fill="currentColor" font-size="12">C</text><rect x="402" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="414" y="182" fill="currentColor" font-size="12">C</text><rect x="436" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="448" y="182" fill="currentColor" font-size="12">C</text><rect x="470" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="482" y="182" fill="currentColor" font-size="12">r</text><rect x="504" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="516" y="182" fill="currentColor" font-size="12">r</text><rect x="538" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="550" y="182" fill="currentColor" font-size="12">r</text><rect x="572" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="584" y="182" fill="currentColor" font-size="12">r</text><rect x="606" y="164" width="34" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="618" y="182" fill="currentColor" font-size="12">r</text><rect x="640" y="164" width="34" height="26" class="f-ok" stroke="currentColor"/><text x="652" y="182" fill="currentColor" font-size="12">R</text><rect x="674" y="164" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="686" y="182" fill="currentColor" font-size="12">C</text>
  <line x1="232" y1="30" x2="232" y2="198" stroke="currentColor" stroke-width="2"/>
  <text x="202" y="214" fill="currentColor" font-size="12">t=4 임계 순간 (tH 출시, 도착 t=2)</text>
  <line x1="640" y1="120" x2="640" y2="198" stroke="currentColor" stroke-width="2"/>
  <text x="530" y="232" fill="currentColor" font-size="12">t=16 tH 완료: 응답 16 − 2 = 14</text>
  <text x="10" y="258" fill="currentColor" font-size="12">R 실행 · C 자원 쥐고 실행 (tL: R1 천장 3, isr2: KERNEL) · 점선 = 선점당해 대기</text>
  <text x="10" y="276" fill="currentColor" font-size="12">B = 더 낮은 tL 때문에 대기 (4칸 = B − 1). tL 은 t=3, 즉 tH 출시 1 tick 전에 R1 을 잡았다.</text>
  <text x="10" y="294" fill="currentColor" font-size="12">isr1·isr2 는 천장 3 보다 높아 그대로 끼어든다: 응답 14 = 지터 2 + isr1 2 + isr2 2 + 블로킹 4 + 자기 4.</text>
</svg>
```

응답 14 는 확장 R 15 보다 1 작다. 이유는 **이산 시간의 ε**: tL 이 tH 보다 먼저 자원을 잡으려면 tH 출시보다 최소 1 tick 앞서 잡아야 하고, 그 1 tick 은 tH 의 응답 창 밖이다. 그래서 관측 가능한 최대 블로킹은 `B − 1 = 4` (위의 B 칸 4개, `max_block[tH] == 4`) 이고, `B` 를 `B − 1` 로 바꾼 RTA (`rta_discrete`) 가 14 를 정확히 낸다. 연속 시간 모델(µs 아래로 계속 쪼갤 수 있는 실제 CPU)에서는 블로킹이 `B − ε` 까지 가므로 분석은 `B` 를 그대로 쓴다 — 안전한 쪽이다.

최악 패턴의 구성 (TODO 5) 이 곧 **임계 순간(critical instant) 정리의 확장판** 이다:

| 구성 요소 | 왜 최악인가 | 식의 어느 항 |
|---|---|---|
| 블로커가 출시 직전에 가장 긴 cs 를 잡음 | 출시 뒤에는 IPCP 상 아무도 새로 못 잡는다 → 블로킹은 출시 시점에 이미 쥔 하나뿐 | `B_i` |
| 상위 entity 가 i 와 같은 순간 출시, 그 첫 작업이 **최대로 늦게** (지터 J) | 다음 작업은 제때 (`T − J` 뒤) → 한 창에 하나가 더 들어온다 | `⌈(w + J_j)/T_j⌉` |
| i 자신도 최대 지터 | 데드라인은 도착 기준 | `+ J_i` |
| 상위 entity 는 그 뒤 최소 간격으로 계속 | 산발적 ISR 도 최소 간격이 최악 | `T_j` = 최소 도착 간격 |

모든 entity 에 대해 최악 패턴을 돌리면 5/5 가 `rta_discrete` 와 정확히 같다. 그리고 같은 시스템에 **무작위 패턴 200 회** 만 돌리면:

```text
   A sim: random-only 200 runs max isr1=1 isr2=4 tH=13 tM=18 tL=27
   A sim: + critical patterns   max isr1=1 isr2=4 tH=14 tM=18 tL=27
```

무작위 200 회는 tH 의 최악 14 를 한 번도 만나지 못했다 (13 이 최대). 테스트·HIL 장비에서 "몇 시간 돌렸는데 미스 없음" 이 증거가 못 되는 이유가 이것이다 — 최악은 블로커 · 지터 · ISR 위상이 **동시에** 맞아야 생기고, 그 확률은 작다. 분석은 그 정렬을 가정하고, 적대적 패턴은 그것을 **만든다**. 순진한 R = 4 는 관측 14 보다 작으니 **위험한(unsafe)** 분석이다.

### Part B — 지터만으로 깨지는 순진한 분석

hi (C 2, T 10, J 6) 아래의 lo (C 5, T 20, D 9):

| 분석 | 계산 | R |
|---|---|---|
| 순진 (M11 A) | `5 + ⌈7/10⌉·2 = 7` | 7 |
| 확장 | `w = 5 + ⌈(w + 6)/10⌉·2` → 9 → 9 | **9** |
| 최악 패턴 시뮬레이션 | hi 출시 0 (도착 −6) 와 4 (도착 4): lo 는 2–3, 6–8 실행 | **9** |

hi 의 첫 작업이 6 늦게 출시되고 둘째는 제때 출시되면 두 작업 사이가 **4 tick** 으로 줄어, lo 의 9 tick 창에 hi 가 두 번 들어온다. lo 의 D 를 8 로 줄이면 순진한 분석은 여전히 7 ≤ 8 "통과", 확장 분석은 `9 > 8` 로 거부, 시뮬레이션은 **실제 미스** (`misses = 1, first_resp = 9`). 지터는 B 와 ISR 없이도 혼자서 분석을 깬다. hi 자신의 R = 2 + 6 = 8 (자기 지터는 끝에 더한다).

### Part B — SYSCALL 위/아래 ISR

배경 태스크 bg 가 `taskENTER_CRITICAL` 안에서 200 tick 을 보내는 시스템 (KERNEL 천장 5, fast ISR 6, slow ISR 5):

| ISR | B | 확장 RTA | 최악 패턴 관측 (테스트 B2) |
|---|---|---|---|
| fast (prio 6, SYSCALL 위) | 0 | R = 1 ok | 1 |
| slow (prio 5, = SYSCALL) | 200 | R > D = 5, MISS | 205 = 블로킹 199 + fast 5 회 + 자기 1 |

같은 하드웨어 · 같은 ISR 코드인데 NVIC 우선순위 숫자 하나로 최악 응답이 1 → 205. "ISR 은 태스크보다 높으니 태스크가 ISR 을 막을 수 없다" 는 틀렸다 — 태스크가 BASEPRI 를 올리는 순간 그 태스크는 SYSCALL 레벨 ISR 보다 **높다**. 그래서 FreeRTOS 문서는 커널 임계 구역을 짧게 하라고 하고, 지연이 0 이어야 하는 일(모터 보호, 전류 루프)은 SYSCALL 위에 둔다 — 대가는 그 ISR 에서 FreeRTOS API 를 못 부르는 것 (`vPortValidateInterruptPriority`, port.c:888).

### Part B — 과부하와 검사기

`irq (5, 10)` + `h (5, 10)` → 상위 이용률 100 %. l 의 확장 RTA 는 `w + J > D` 에서 멈춘다 (반복 < 100). 순진한 RTA 는 ISR 을 빼고 h 만 보니 `l` 이 "통과". 검사기는 ISR 이 태스크보다 낮으면 `-4`, cs 가 C 를 넘으면 `-3` 을 돌려준다.

### Part B — 무작위 시스템 150 개: 안전성과 정확성

`gen_system(seed)`: ISR 1–2 개 (prio 20 = SYSCALL 레벨 — 절반은 KERNEL 1 tick 사용, 21 = 그 위), 태스크 3–5 개 (T ∈ {20,…,120}, D ∈ [2T/3, T], J < T/5), 자원 KERNEL/R1/R2, 태스크당 임계 구역 0–2 개. 확장 분석이 **전부 통과** 라고 한 시스템만 골라 무작위 10 회 + entity 별 최악 패턴을 돌린다.

```text
random systems: tried 389, ext-pass 150, violations 0, critical==analysis 539/539, tasks with obs > naive R 538, naive-pass but real miss 131
self-ceiling targets 201: obs < analysis in 11, obs > analysis in 0
```

| 결과 | 의미 |
|---|---|
| violations 0 | 150 시스템 × (10 + n) 실행에서 **관측 > R 이 한 번도 없음**, 관측 블로킹 ≤ B, IPCP 잠금 충돌 0, 미완료 0 |
| critical == analysis 539/539 | 자기 천장이 자기보다 높지 않은 entity 에서 최악 패턴 첫 응답 == `rta_discrete` — 분석이 (이산 시간 ε 빼고) **정확(tight)** |
| obs > naive R : 538 | 순진한 RTA 가 실제보다 작은 값을 낸 태스크 수 (분석 통과 시스템 안에서도) |
| naive-pass but real miss : 131 | 확장 분석이 거부한 시스템 중 순진한 분석은 "전부 통과" 인데 최악 패턴에서 **실제 미스** 가 난 시스템 수 |
| self-ceiling 201 → 11 비관 | 아래 "이 분석이 틀리는 경우" |

### 이 분석이 틀리는 경우 — 비관적인 쪽

`self-ceiling targets`: entity i 가 **자기 우선순위보다 높은 천장** 의 자원(예: 태스크가 KERNEL, 즉 ISR 레벨)을 쥐는 구간이 있는 경우다. 그 구간 동안 i 는 SYSCALL 레벨 ISR 을 **가린다** — 그 ISR 작업은 i 의 cs 가 끝날 때까지 밀리고, i 가 cs 뒤에 바로 끝나면 그 작업은 아예 i 의 응답 창 밖으로 나간다. 식은 "상위 entity 는 언제든 i 를 선점한다" 고 가정하므로 이 효과를 못 본다 → 201 개 중 11 개에서 관측 < 분석 (비관적), 반대 방향은 0. 안전하므로 문제는 없지만, "R 이 왜 실측보다 크죠?" 의 정당한 답 중 하나다 (preemption threshold / 비선점 구간 분석이 이걸 활용한다 — 다음 단계).

다른 비관 원인: B 와 J 의 최악이 실제로는 겹칠 수 없는 시스템 (예: 블로커와 i 가 같은 tick 에 위상 고정), 오프셋이 고정된 주기 태스크(동시 출시가 절대 안 생김), 상위 ISR 이 실제로는 버스트 상한이 있는 경우 (연습 2).

### Part C — 순진한 분석이 놓친 두 버그

```text
== C1 firmware, as written (us)
   name    isr prio      C      T      D    J     B |      R  it      | naive
   adc     isr   15      6     50     50    0     0 |      6   1 ok   |   (ISR 무시)
   can     isr   12      9    160     40    0    45 |     66   1 MISS |   (ISR 무시)
   systick isr   11      5   1000   1000    0    45 |     71   3 ok   |   (ISR 무시)
   ctrl           4    120   1000    400   30    45 |    248   3 ok   |    120 ok
   imu            3    200   2000   1000   30   500 |   1029   2 MISS |    320 ok
   comms          2    300   5000   3000    0   500 |   1526   5 ok   |    620 ok
   log            1   1500  20000  20000    0     0 |   3285   6 ok   |   2560 ok
   C1 critical-pattern sim: can first response 65 (D=40), imu 1195 (D=1000)
```

1. **can ISR**: `w = 9 + B 45 + ⌈w/50⌉·6` → 66 > 40. B 45 는 log 의 링 버퍼 memcpy 를 감싼 `taskENTER_CRITICAL` 이다. 로그 태스크(최하위)의 코드 한 줄이 최상위급 ISR 의 데드라인을 깬다. 최악 패턴 시뮬레이션은 65 (= 9 + 44 + 12, 이산 ε) — 하드웨어였다면 CAN 수신 FIFO 오버런.
2. **imu**: B 500 = 플래시 페이지 쓰기 동안 쥔 SPI. `w0 = 700 → 999`, `999 + J 30 = 1029 > 1000` 에서 멈춤. 멈춘 값은 하한일 뿐이고 (M11 §7), 최악 패턴 시뮬레이션의 실제 응답은 **1195** — ctrl 의 둘째 작업이 창 안에 들어오면서 더 커진다.
3. 순진한 분석은 imu 320, ctrl 120 — "CPU 반도 안 쓴다". 실제 U 는 ISR 포함 0.12 + 0.056 + 0.005 + 0.12 + 0.1 + 0.06 + 0.075 = 0.536 으로 높지 않다. **미스의 원인은 U 가 아니라 B 다.**
4. systick 의 B 도 45 → R 71. ctrl 은 `xTaskDelayUntil` 로 tick 에서 출시되므로 J 는 "tick ISR 응답 + 스케줄러 경로" 다. 표의 J 30 은 고친 설계(systick R 26)에 맞춘 값이고, C1 에서 정직하려면 J ≥ 75 쯤이어야 한다 — 손으로 다시 하면 ctrl `w = 218`, R = 218 + 75 = 293 ≤ 400 이라 결론은 같다. **분석 결과(R)가 다른 entity 의 입력(J)이 되는** 이런 연결을 놓치지 않는 것이 실무 RTA 의 절반이다.

고친 설계 (C2):

```text
== C2 fixed: SCHED lock + 100 us flash chunks (us)
   name    isr prio      C      T      D    J     B |      R  it      | naive
   adc     isr   15      6     50     50    0     0 |      6   1 ok   |   (ISR 무시)
   can     isr   12      9    160     40    0     6 |     21   2 ok   |   (ISR 무시)
   systick isr   11      5   1000   1000    0     6 |     26   2 ok   |   (ISR 무시)
   ctrl           4    120   1000    400   30    45 |    248   3 ok   |    120 ok
   imu            3    200   2000   1000   30   100 |    557   4 ok   |    320 ok
   comms          2    300   5000   3000    0   100 |    887   4 ok   |    620 ok
   log            1   1500  20000  20000    0     0 |   3285   6 ok   |   2560 ok
   C2 random-only max: adc 6 can 19 systick 20 ctrl 222 imu 429 comms 835 log 3079
   C2 sim (40 random x 40 ms + critical): max obs / R: adc 6/6 can 20/21 systick 25/26 ctrl 247/248 imu 556/557 comms 886/887 log 3285/3285
```

| 고침 | 천장 변화 | 효과 |
|---|---|---|
| memcpy 를 `taskENTER_CRITICAL` → `vTaskSuspendAll` | KERNEL 12 → SCHED 4 (최상위 태스크) | can/systick 의 B 45 → 6 (comms 의 짧은 KERNEL). ctrl 의 B 는 45 그대로 — 태스크끼리는 여전히 막힌다. **필요한 만큼만 높은 천장** 을 고르는 것이 요점 |
| 플래시 쓰기를 100 µs 조각 둘로, 조각 사이에 SPI 해제 | SPI 천장 3 그대로 | imu/comms 의 B 500 → 100 |

무작위 40 회 × 40 ms 의 최대는 imu 429 / ctrl 222 — 최악 패턴(556 / 247)보다 작다. 다시 한번: **무작위 실행 시간이 길어도 최악은 거의 안 나온다.** 모든 entity 에서 최악 패턴 == `rta_discrete`, 관측 ≤ R.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C — 응답 / 데드라인 (%) : 순진한 RTA vs 고치기 전 실제 최악 vs 고친 뒤 확장 R</text>
  <text x="10" y="74" fill="currentColor">can (D 40)</text>
  <text x="134" y="57" fill="currentColor" font-size="12">순진: ISR 은 모델에 없음</text>
  <rect x="130" y="64" width="390" height="14" class="f-warn" stroke="currentColor"/><text x="526" y="76" fill="currentColor" font-size="12">C1 시뮬 최악 65</text>
  <rect x="130" y="82" width="126" height="14" class="f-ok" stroke="currentColor"/><text x="262" y="94" fill="currentColor" font-size="12">C2 확장 R 21</text>
  <text x="10" y="140" fill="currentColor">ctrl (D 400)</text>
  <rect x="130" y="112" width="72" height="14" class="f-mut" stroke="currentColor"/><text x="208" y="124" fill="currentColor" font-size="12">순진 30%</text>
  <rect x="130" y="130" width="148" height="14" class="f-warn" stroke="currentColor"/><text x="284" y="142" fill="currentColor" font-size="12">C1 시뮬 최악 247</text>
  <rect x="130" y="148" width="149" height="14" class="f-ok" stroke="currentColor"/><text x="285" y="160" fill="currentColor" font-size="12">C2 확장 R 248</text>
  <text x="10" y="206" fill="currentColor">imu (D 1000)</text>
  <rect x="130" y="178" width="77" height="14" class="f-mut" stroke="currentColor"/><text x="213" y="190" fill="currentColor" font-size="12">순진 32%</text>
  <rect x="130" y="196" width="287" height="14" class="f-warn" stroke="currentColor"/><text x="423" y="208" fill="currentColor" font-size="12">C1 시뮬 최악 1195</text>
  <rect x="130" y="214" width="134" height="14" class="f-ok" stroke="currentColor"/><text x="270" y="226" fill="currentColor" font-size="12">C2 확장 R 557</text>
  <text x="10" y="272" fill="currentColor">comms (D 3000)</text>
  <rect x="130" y="244" width="50" height="14" class="f-mut" stroke="currentColor"/><text x="186" y="256" fill="currentColor" font-size="12">순진 21%</text>
  <rect x="130" y="262" width="122" height="14" class="f-warn" stroke="currentColor"/><text x="258" y="274" fill="currentColor" font-size="12">C1 시뮬 최악 1525</text>
  <rect x="130" y="280" width="71" height="14" class="f-ok" stroke="currentColor"/><text x="207" y="292" fill="currentColor" font-size="12">C2 확장 R 887</text>
  <line x1="370" y1="36" x2="370" y2="308" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4"/>
  <text x="330" y="324" fill="currentColor" font-size="12">데드라인 100 %</text>
  <text x="440" y="324" fill="currentColor" font-size="12">흐린 = 순진 · 경고 = C1 · 초록 = C2</text>
</svg>
```

**실무 체크리스트** — 분석이 보드와 맞으려면:

| 항목 | 이 도구 | 보드에서 할 일 |
|---|---|---|
| C | 주어진 값 | 측정 최댓값 + 여유 (RTOS-A03: 무작위 입력은 최악 경로를 놓친다 — 이 레슨의 무작위 출시와 같은 문제) |
| ISR T | 최소 도착 간격 | 데이터시트/프로토콜에서 (CAN: 최소 프레임 길이 × 비트 시간), 버스트 모델 (연습 2). 노이즈 많은 GPIO EXTI 는 상한이 없다 → 디바운스 또는 폴링 |
| B | 임계 구역 표 | `taskENTER_CRITICAL` / `vTaskSuspendAll` / 뮤텍스 구간을 전부 목록화 · 측정. 벤더 HAL 의 숨은 `__disable_irq()` (PRIMASK — **모든** ISR 을 막는다, 천장 = 최상위) 포함 |
| J | 주어진 값 | ISR 진입 → 태스크 첫 줄 시간을 GPIO 토글/트레이스로 (RTOS-A06), tick 기반 출시면 + tick ISR 응답 |
| 오버헤드 | 없음 | 문맥 전환 2회/작업 + 예외 진입·복귀 (연습 3) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 57/57 passed (5회 반복 동일, 약 0.05 s)
./solution -v                                                                 # 표 · 타임라인 · 무작위 시스템 요약
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 57/57, 보고 없음
```

테스트가 진짜 실수를 잡는지 돌연변이(mutation)로 확인했다 (`/tmp` 사본, 레슨 파일 아님):

| 돌연변이 | 실패 테스트 수 |
|---|---|
| 상위 지터 `+ J_j` 삭제 | 5 |
| 자기 지터 `+ J_i` 삭제 | 11 |
| B 를 max 대신 합으로 | 11 |
| 천장 비교 `>=` → `>` | 18 |
| 태스크 분석에서 ISR 을 hp 에서 제외 | 14 |
| 시뮬레이터 동점 규칙 (쥔 쪽 우선) 삭제 | 15 |
| 선언 천장 `rmin` 무시 (KERNEL 이 ISR 레벨로 안 올라감) | 4 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 설계 |
|---|---|---|
| ISR 을 분석에서 뺌 ("짧으니까") | adc 6 µs / 50 µs = 12 % — 모든 태스크 R 에 들어간다. B3 는 ISR 을 빼면 "여유 많음" | ISR = 최상위 산발적 entity, T = **최소** 간격 |
| ISR 의 B = 0 이라고 가정 | `taskENTER_CRITICAL` 은 SYSCALL 이하 ISR 을 막는다 (C1 can: B 45 → 미스) | KERNEL 자원 천장 = SYSCALL 레벨, `rmin` |
| 천장 비교를 `>` 로 | BASEPRI 는 **같은 레벨까지** 막는다 (ARMv7-M: 우선순위 값 ≥ BASEPRI 인 예외 차단). can (12) 이 KERNEL (12) 에 안 막히는 것으로 계산 | `ceil >= prio_i` |
| B = 임계 구역 합 (IPCP 에서) | 출시 시점에 쥔 하위 cs 는 하나뿐 (M05) — 과하게 비관 | `max`. 단, PI 뮤텍스면 합 계열 상한 (연습 4) |
| ISR T 에 평균 도착 간격 | 버스트가 오면 한 창에 여러 개 — 연습 2: 평균율 R 730 < 실제 820 | 최소 간격 또는 버스트 모델 |
| 지터를 상위 쪽에만 / 자기 쪽에만 | `⌈(w+J_j)/T_j⌉` 는 상위 작업 수, `+ J_i` 는 데드라인 기준 | 둘 다 (돌연변이 5, 11 실패) |
| "HIL 에서 24 시간 돌렸는데 미스 없음" = 안전 | 무작위 200 회가 tH 최악 14 를 못 만남, C2 무작위 imu 429 vs 최악 556 | 분석 + 적대적 패턴 (임계 순간 재현) |
| 시뮬레이터가 분석 코드를 호출 | 같은 버그를 공유하면 교차 확인이 무의미 | `sim_run` 은 천장 표만 입력 |
| 멈춘 R (`MISS`) 을 실제 응답으로 보고 | C1 imu: 출력 1029, 실제 최악 1195 | "R > D" 라고만 보고, 크기는 시뮬레이션/계속 반복 |
| 모든 공유 데이터를 `taskENTER_CRITICAL` 로 | 태스크끼리만 공유하는 데이터에 ISR 레벨 천장 → ISR 들의 B 증가 | 태스크 전용이면 `vTaskSuspendAll` 또는 IPCP/PI 뮤텍스, ISR 과 공유할 때만 KERNEL |

## 8. 면접 Q&A

<details>
<summary>Q1. 블로킹 항 B 는 어떻게 구하나요? ISR 도 블로킹을 받나요?</summary>

IPCP/PCP 기준: entity i 는 "자기보다 낮은 entity 의 임계 구역 중, 그 자원의 천장이 prio_i 이상인 것" **하나** 만큼만 막힌다 → `B_i = max` (Sha · Rajkumar · Lehoczky 1990, RTOS-M05). 천장을 정할 때 커널 임계 구역을 빠뜨리면 안 된다: Cortex-M FreeRTOS 의 `taskENTER_CRITICAL` 은 BASEPRI 를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 로 올리므로 (portmacro.h:213–225) **천장이 ISR 레벨인 자원** 이다. 그래서 SYSCALL 이하 ISR 은 어느 태스크의 커널 임계 구역에도 막힌다 — Part C 의 can ISR 은 로그 태스크의 memcpy 45 µs 때문에 미스했다. systick 은 핸들러 전체가 `portDISABLE_INTERRUPTS()` 안이라 (port.c:566–570) 같은 레벨 ISR 의 B 가 된다. SYSCALL 위 ISR 만 B = 0.

</details>

<details>
<summary>Q2. ISR 을 분석에 어떻게 넣고, 주기 없는 인터럽트(CAN, UART, GPIO)의 T 는 무엇으로 하나요?</summary>

ISR 은 모든 태스크보다 높은 산발적 entity: C = ISR 최악 실행 시간(진입·복귀 포함, 연습 3), T = **최소 도착 간격**, 다른 ISR 과는 NVIC 우선순위대로 선점. T 출처: CAN 은 최소 프레임 길이 × 비트 시간, UART RX 는 바이트 시간 (DMA 면 반/완료 인터럽트 간격), 타이머는 주기. 평균 간격을 쓰면 **위험** 하다 — 연습 2 에서 평균율 모델 R 730 < 실제 버스트 최악 820. 최소 간격만 쓰면 너무 비관적일 수 있어 버스트 모델(창 P 마다 최대 k 개)을 쓴다. 상한이 없는 인터럽트(채터링하는 버튼 EXTI)는 분석이 불가능하다 → 디바운스 하드웨어, 인터럽트 마스킹 후 재활성화, 또는 폴링으로 바꿔 상한을 **만든다**.

</details>

<details>
<summary>Q3. 지터는 왜 상위 쪽에서 ⌈(w + J_j)/T_j⌉ 로, 자기 쪽에서 + J_i 로 들어가나요?</summary>

상위 j 의 첫 작업이 J_j 늦게 출시되고 다음 작업은 제때 출시되면 두 출시 간격이 `T_j − J_j` 로 줄어, 길이 w 창에 `⌈(w + J_j)/T_j⌉` 개까지 들어온다. 자기 지터는 데드라인이 **도착** 기준이라 끝에 더한다 (Audsley · Burns · Richardson · Tindell · Wellings 1993). Part B 예: hi (2, 10, J 6) 아래 lo 는 순진 7, 실제 9 — B 도 ISR 도 없이 지터 하나로 D = 8 시스템이 미스한다. 출처: ISR → `xSemaphoreGiveFromISR` → 태스크 경로, `portYIELD_FROM_ISR` 누락 시 최대 1 tick (RTOS-M02), tick 기반 출시면 tick ISR 응답 (Part C ctrl).

</details>

<details>
<summary>Q4. 분석이 안전하다는 걸 어떻게 확인했나요? 오래 돌려 보면 되지 않나요?</summary>

두 가지를 분리했다. (1) **안전성**: 분석과 코드를 공유하지 않는 시뮬레이터로 무작위 시스템 150 개 × (무작위 10 + 최악 패턴 n) 실행 — 관측 > R 0 건, 관측 블로킹 ≤ B, IPCP 잠금 충돌 0. (2) **정확성**: entity 마다 임계 순간을 직접 만드는 패턴 (가장 긴 블로커가 직전에 자원 획득, 상위 전부 최대 지터로 동시 출시) → 539/539 가 이산 시간 분석값과 일치. "오래 돌리기" 는 (1) 의 약한 버전일 뿐이다: Part A 에서 무작위 200 회는 tH 최악 14 를 한 번도 못 만났고 (13), Part C 에서 40 ms × 40 회는 imu 429 (최악 556). 최악은 위상이 정확히 맞아야 나오므로 테스트로 "찾기" 보다 분석으로 "가정" 하고, 적대적 패턴으로 그 가정이 실제로 **달성 가능** 함을 보이는 게 맞다.

</details>

<details>
<summary>Q5. 이 분석이 틀리는 경우는? (비관적인 쪽과 위험한 쪽을 나눠서)</summary>

**비관적 (안전하지만 R 이 실측보다 큼)**: ① i 자신이 자기보다 높은 천장을 쥐는 구간 — 그동안 상위 ISR 이 가려져 그 일부가 i 완료 뒤로 밀린다 (무작위 시스템 201 개 중 11 개에서 관측 < 분석). ② 블로킹과 지터 최악이 실제로는 겹칠 수 없는 위상. ③ 오프셋 고정 시스템 (동시 출시가 절대 없음). ④ 버스트 상한이 있는 ISR 을 최소 간격으로만 모델링 (연습 2). ⑤ 연속 시간 B vs 이산 시간 B − 1.
**위험 (R 이 실측보다 작을 수 있음 — 모델 밖 가정)**: C 가 측정값 (최악 경로 미측정, 캐시·플래시 대기, RTOS-A03), 문맥 전환·예외 진입 비용 누락 (연습 3), 숨은 임계 구역 (벤더 HAL 의 `__disable_irq()` = PRIMASK, 모든 ISR 차단), ISR 이 최소 간격을 어김 (노이즈), 뮤텍스가 사실 PI 인데 IPCP 식 사용 (연습 4), DMA·다른 버스 마스터의 메모리 경합, 플래시 쓰기/지우기 중 같은 뱅크에서 명령어 인출이 멈추는 MCU (STM32 등, 데이터시트 기준 — 미검증), 멀티코어, R > T (이전 작업이 남아 있음 — busy period 분석 필요, Lehoczky 1990).

</details>

<details>
<summary>Q6. 10 kHz 전류 루프 ISR 을 configMAX_SYSCALL_INTERRUPT_PRIORITY 위에 두겠나요, 아래에 두겠나요? 바꾼다면 무엇이 달라지나요?</summary>

위에 둔다 (Part C 의 adc). 이유: B = 0 — 어느 태스크의 `taskENTER_CRITICAL` 도, 커널 자신의 임계 구역(tick 핸들러, `xQueueSendFromISR` 내부)도 이 ISR 을 늦추지 못한다. 응답 = 자기 C + 더 높은 ISR 뿐. 대가: FreeRTOS API 를 부를 수 없다 (port.c:888 assert). 그래서 속도 루프 태스크에 알리려면 (a) 공유 변수 + 더 낮은 "중계" ISR 을 소프트웨어로 펜딩 (NVIC ISPR/STIR 로 SYSCALL 이하 IRQ 를 트리거, RTOS-A01 의 방식), 또는 (b) 속도 루프를 tick 이나 별도 타이머로 출시. 아래에 두면 API 는 쓸 수 있지만 B = 시스템에서 가장 긴 커널 임계 구역이 되고, 그 값은 **다른 팀의 코드** 가 정한다 — 10 kHz (100 µs) 루프에 45 µs 블로킹은 치명적. 면접에서는 "제어 루프 데드라인을 다른 모듈의 임계 구역 길이에 묶지 않으려고 위에 둔다" 가 핵심 문장이다.

</details>

<details>
<summary>Q7. 보드에서 B 와 J 는 어떻게 측정하나요?</summary>

B: 임계 구역 진입/퇴출에 계측. FreeRTOS 는 `taskENTER_CRITICAL` 이 매크로라 프로젝트에서 감싸 DWT CYCCNT 로 최장 길이를 기록하거나, GPIO 를 올렸다 내려 로직 애널라이저로 최대 폭을 본다 (Cortex-M3/M4 의 CYCCNT 사용법은 RTOS-A03). 뮤텍스 보유 시간은 트레이스 훅으로 — 뮤텍스 take 는 `xQueueSemaphoreTake` 안의 `traceQUEUE_RECEIVE` (queue.c:1698), give 는 `xQueueGenericSend` 의 `traceQUEUE_SEND` (queue.c:979) 를 지나므로 핸들이 뮤텍스인 것만 거른다 (RTOS-A06 포맷의 `MUTEX_TAKE/GIVE`). J: 하드웨어 이벤트 (예: CAN RX 핀 / DRDY 핀) 와 태스크 첫 줄 GPIO 사이 시간을 스코프로, 또는 ISR_ENTER → SWITCH_IN 트레이스 간격 최댓값. 측정값은 항상 **하한** 이다 — 분석에 넣을 때는 코드 리뷰로 찾은 최장 경로 + 여유를 쓴다. "측정한 B 와 분석의 B 가 다르면?" → 측정이 작으면 최악 위상을 못 만났거나 경로를 못 탄 것, 크면 표에서 빠진 임계 구역이 있다 (보통 HAL).

</details>

<details>
<summary>Q8. FreeRTOS 뮤텍스는 우선순위 상속(PI)인데, IPCP 식으로 B 를 구하면 안전한가요?</summary>

아니다 — PI 에서는 한 작업이 여러 번 막힐 수 있다: 하위 작업마다 최대 한 번, 또는 자원마다 최대 한 번 → 상한 `min(Σ_하위 entity max cs, Σ_자원 max cs)` (Sha · Rajkumar · Lehoczky 1990). 커널 임계 구역과 스케줄러 잠금은 진짜 천장이라 최대 1개. 연습 4 의 보수적 혼합 상한으로 Part C 를 다시 계산하면 imu 의 B 100 → 160, comms 100 → 145, ctrl 45 → 60 이고 다행히 전부 통과 (imu R 623). 이 여유가 없으면 선택지: IPCP 를 `vTaskPrioritySet` 래퍼로 구현 (M05), 자원 접근을 전담 태스크 + 큐로 바꿔 뮤텍스 제거, 또는 임계 구역 단축. PI 를 쓰면 교착 방지도 설계 규칙 (잠금 순서) 으로 따로 지켜야 한다 (M05 Part B).

</details>

<details>
<summary>Q9. 분석은 통과했는데 보드에서 데드라인 미스가 났습니다. 무엇부터 보겠나요?</summary>

순서: ① **트레이스로 미스한 작업의 창을 재구성** (RTOS-A06/A09) — 그 창에서 CPU 를 쓴 것을 ISR / 상위 태스크 / 하위 태스크(= 블로킹) / 커널로 나눈다. ② 하위 태스크 시간이 B 보다 크면: 표에 없는 임계 구역 (HAL 의 `__disable_irq`, 로깅 `printf` 안의 뮤텍스, PI 체인) → B 표 갱신. ③ ISR 시간이 크면: 도착 간격이 가정한 T 보다 짧다 (버스트, 노이즈, 재진입하는 에러 인터럽트) → T 또는 버스트 모델, 하드웨어 필터. ④ 상위 태스크 시간이 크면: C 초과 (측정 안 된 경로, 캐시 미스, 플래시 대기) → WCET 재측정 (RTOS-A03). ⑤ 출시가 늦으면: J 과소 (tick 양자화, `portYIELD_FROM_ISR` 누락). 고친 뒤에는 **분석 → 적대적 패턴 시뮬레이션 → 보드 트레이스** 순서로 같은 숫자가 나오는지 맞춘다. 면접 답의 골격: "분석은 모델이 맞을 때만 맞다 — 미스는 모델의 어느 입력이 틀렸는지 알려 주는 데이터다."

</details>

## 9. 추가 연습

### 연습 1 — 민감도: 임계 구역을 얼마나 길게 해도 되나

C2 시스템에서 log 의 SPI 구간을 **조각 하나** 로 둘 때 최대 길이는? log 의 짧은 구간을 다시 `taskENTER_CRITICAL` 로 되돌린다면 최대 몇 µs 까지 can ISR 이 견디나? 이분 탐색 `max_spi_chunk()`, `max_kernel_crit()` 를 작성하라.

<details>
<summary>답 보기</summary>

`all_ext_ok` 는 구간 길이에 대해 단조이므로 이분 탐색.

```text
ex1 max SPI chunk 462 us (imu R=1000, D=1000); chunk+1 -> imu crit sim 1000, chunk+2 -> 1144 | max taskENTER_CRITICAL in log 25 us
```

- SPI 조각 462 µs 까지 — imu R = 1000 = D 로 딱 맞는다. 463 이면 분석은 거부하지만 최악 패턴 시뮬레이션은 1000 (이산 시간 B − 1 = 462 → 미스 아님). 464 면 **1144** 로 뛴다 — 응답이 ctrl 의 둘째 작업이 창에 들어오는 계단을 넘으면서 144 µs 가 한 번에 늘어난다. 응답 시간은 C·B 에 대해 **불연속** 이라 "조금 늘려도 조금만 나빠진다" 는 직관이 틀린다.
- 커널 임계 구역은 25 µs: can `w = 9 + 25 + ⌈w/50⌉·6 = 40 = D`. 26 µs 면 41 > 40. 리뷰 규칙으로 쓰면 "SYSCALL 이하 ISR 이 있는 한 `taskENTER_CRITICAL` 은 25 µs 미만" — 숫자가 있는 코딩 규칙.

</details>

### 연습 2 — 버스트 도착 ISR: 최소 간격 vs 버스트 vs 평균율

can ISR (C 30) 의 프레임은 최소 100 µs 간격, 단 10 ms 창마다 최대 4 개 (상위 프로토콜 제약). ctrl (300, 1000, D 500) 아래 proto (400, 5000, D 900). `η(w) = min(⌈w/t_min⌉, k·⌈w/P⌉)` 를 RTA 에 넣고, 세 모델 (버스트 / 산발적 T = 100 / 평균율 T = P/k = 2500) 을 비교하라. 실제 버스트를 만드는 시뮬레이션으로 어느 모델이 위험한지 보여라.

<details>
<summary>답 보기</summary>

```text
ex2 proto R: burst model 820 ok | sporadic t_min MISS (stopped 970) | average-rate 730 ok (UNSAFE) | critical burst sim 820, random 60 runs max 820
```

| 모델 | proto R | 판정 |
|---|---|---|
| 산발적 T = t_min (100) | 970 에서 멈춤 (> 900) | 거부 — 너무 비관적 (10 ms 동안 100 개 프레임을 가정) |
| **버스트 η** | 400 + 300 + 4·30 = **820** | 통과, 최악 패턴 시뮬레이션 820 과 정확히 같음 |
| 평균율 T = 2500 | 400 + 300 + 30 = 730 | "통과" 지만 **실제 820** — 위험 |

버스트 모델은 "길이 P 창마다 k 개" 와 "최소 간격" 둘 다로 세고 작은 쪽을 쓴다 — 두 상한이 모두 참이므로 안전하다. Tindell 류의 버스트 식은 이보다 더 정밀하다 (문서 기준). 무작위 60 회 (각 60 ms) 의 최대는 820 — 이 시스템은 버스트가 proto 출시와 겹치기 쉬워 무작위로도 최악이 나왔다 (Part A·C 와 대조).

</details>

### 연습 3 — 문맥 전환과 예외 진입 비용

태스크 작업마다 문맥 전환 2 회 (`C + 2·cs`, RTOS-M11 연습 2 의 규칙), ISR 마다 진입·복귀 1 µs 를 더할 때 C2 시스템이 견디는 최대 cs 는? 누가 먼저 깨지나?

<details>
<summary>답 보기</summary>

```text
ex3 max context-switch cost 56 us (2 per job, ISR +1 us); first to fail at +1: ctrl
```

56 µs — 여유는 크다 (Cortex-M4 FreeRTOS 전환은 보통 수 µs 이하로 알려져 있다, 이 레슨에서 측정 안 함). 먼저 깨지는 건 ctrl: C 가 작은 고주파 태스크일수록 `2·cs / C` 비율이 커지고, 상위 ISR 간섭 계단을 넘기 쉽다. ISR 쪽 오버헤드는 adc (20 kHz) 처럼 **빈도가 높은** ISR 에서 크다: 1 µs × 20,000 /s = 2 % CPU. 실제 Cortex-M3/M4 의 예외 진입은 12 사이클 정도로 문서화돼 있지만 (ARM 문서 기준) 대기 상태·FPU lazy stacking 에 따라 달라지니 측정값을 쓴다.

</details>

### 연습 4 — FreeRTOS 현실: PI 뮤텍스 + 천장형 커널 구간

SPI, PARAM 을 PI 뮤텍스로, KERNEL, SCHED 를 천장으로 볼 때 보수적 상한 `B = (천장형 구간 max) + min(Σ_하위 entity PI 구간 max, Σ_PI 자원 하위 구간 max)` 를 구현하고 C2 를 다시 분석하라.

<details>
<summary>답 보기</summary>

```text
ex4 adc     B ipcp   0 -> PI-mixed   0, R    6 ok
ex4 can     B ipcp   6 -> PI-mixed   6, R   21 ok
ex4 systick B ipcp   6 -> PI-mixed   6, R   26 ok
ex4 ctrl    B ipcp  45 -> PI-mixed  60, R  263 ok
ex4 imu     B ipcp 100 -> PI-mixed 160, R  623 ok
ex4 comms   B ipcp 100 -> PI-mixed 145, R  938 ok
ex4 log     B ipcp   0 -> PI-mixed   0, R 3285 ok
```

imu: 천장형 SCHED 45 (log) + PI {comms PARAM 15, log SPI 100} = 45 + min(115, 115) = 160. PI 에서는 comms 의 PARAM 구간과 log 의 SPI 구간에 **차례로** 막힐 수 있기 때문이다 (M04 의 연쇄). 이 상한은 "천장형 하나 + PI 합" 을 그냥 더한 것이라 보수적이다 — 더 정밀한 혼합 분석은 다음 단계. 결론: 이 설계는 PI 로도 통과하지만 imu 여유가 557 → 623 으로 준다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 18/18 passed
./practice -v                                                                # 위의 ex1–ex4 줄
```

## 10. 핵심 정리

- ISR = 모든 태스크 위의 산발적 entity (C, **최소** 도착 간격 T). 평균율은 위험, 상한 없는 인터럽트는 분석 불가.
- 커널 임계 구역은 **천장이 SYSCALL 레벨인 자원**: Cortex-M FreeRTOS 의 `taskENTER_CRITICAL` (BASEPRI) 은 태스크뿐 아니라 SYSCALL 이하 ISR 의 B 가 된다. SYSCALL 위 ISR 은 B = 0, 대신 API 금지.
- `B_i = max{len : 하위 entity, 천장 ≥ prio_i}` (IPCP, 하나만). PI 뮤텍스면 합 계열 상한.
- `w = C + B + Σ_{hp} ⌈(w + J_j)/T_j⌉C_j`, `R = w + J_i` — ISR 도 같은 식, 정수로, `w + J > D` 에서 멈춤.
- 검증 = 분석과 독립인 시뮬레이터 + **적대적 패턴** (가장 긴 블로커가 직전 획득, 상위 최대 지터 동시 출시). 무작위 실행은 최악을 거의 못 만난다 (tH 13 vs 14, imu 429 vs 556).
- 순진한 RTA (B·J·ISR 없음) 는 위험하다: Part C 는 순진 "전부 통과" 인데 can 65 > 40, imu 1195 > 1000 이 실제로 재현된다. 미스의 원인은 U (0.54) 가 아니라 B.
- 고침의 원칙: **필요한 만큼만 높은 천장** (태스크끼리면 `vTaskSuspendAll`/뮤텍스, ISR 과 공유할 때만 KERNEL), 긴 구간은 조각내기.
- 분석이 비관적인 경우 (자기 천장이 상위를 가림, 이산 ε) 와 위험한 경우 (모델 밖: C, 숨은 `__disable_irq`, 버스트, 오버헤드, PI) 를 구분해 말한다.

## 참고자료

- N. Audsley, A. Burns, M. Richardson, K. Tindell, A. Wellings, "Applying New Scheduling Theory to Static Priority Pre-emptive Scheduling", Software Engineering Journal 8(5), 1993 — 블로킹 · 릴리즈 지터를 넣은 응답 시간 분석
- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers 39(9), 1990 — PI/PCP 블로킹 상한
- M. Joseph, P. Pandya, "Finding Response Times in a Real-Time System", The Computer Journal 29(5), 1986 — 응답 시간 반복식
- K. Tindell, A. Burns, A. Wellings, "An Extendible Approach for Analyzing Fixed Priority Hard Real-Time Tasks", Real-Time Systems 6(2), 1994 — 버스트 · 지터 확장 (제목·권호 문서 기준, 미검증)
- J. Lehoczky, "Fixed Priority Scheduling of Periodic Task Sets with Arbitrary Deadlines", RTSS 1990 — R > T 일 때의 busy period
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", JACM 20(1), 1973 — 임계 순간
- G. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 7장 자원 접근 프로토콜 · 블로킹 / A. Burns, A. Wellings, *Real-Time Systems and Programming Languages* — RTA 장
- FreeRTOS-Kernel V11.3.2 소스 — portable/GCC/ARM_CM4F/portmacro.h (120–124 마스크 매크로, 213–245 `vPortRaiseBASEPRI`/`ulPortRaiseBASEPRI`), portable/GCC/ARM_CM4F/port.c (65–67 PendSV/SysTick 최저 우선순위, 433–434, 475–501 `vPortEnterCritical`/`vPortExitCritical`, 560–570 `xPortSysTickHandler`, 851–888 `vPortValidateInterruptPriority`), include/task.h (241, 243), queue.c (1167, 1205, 1330 `xQueueGenericSendFromISR`), tasks.c (483 `xPendingReadyList`, 3890–3928 `vTaskSuspendAll`, 5527–5550, 4831 `xTaskIncrementTick`) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — "Running the RTOS on a ARM Cortex-M Core" (configMAX_SYSCALL_INTERRUPT_PRIORITY, NVIC 우선순위 방향) — <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- ARMv7-M Architecture Reference Manual — BASEPRI 의 의미 (값 이상 우선순위 예외 차단), 예외 진입
- 관련 레슨: RTOS-M11 (기본 RTA · B/J 식 소개 · OPA), RTOS-M05 (IPCP · 블로킹 1개), RTOS-M04 (PI · 연쇄), RTOS-M02 (FromISR · SYSCALL 우선순위), RTOS-A01 (Cortex-M BASEPRI 임계 구역 실측), RTOS-A03 (WCET 측정), RTOS-A05 (비주기 서버), RTOS-A06 (트레이싱), RTOS-A09 (데드라인 미스 사후 분석), RTOS-A10 (모터 제어 설계)
