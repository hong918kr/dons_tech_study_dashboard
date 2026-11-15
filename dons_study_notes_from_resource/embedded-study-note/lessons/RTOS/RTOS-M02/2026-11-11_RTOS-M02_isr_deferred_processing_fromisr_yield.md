---
id: RTOS-M02
time: 90분
updated: 2026-11-11
---
# RTOS-M02 ISR 지연 처리: FromISR API와 yield 요청

## 학습 목표

- 미니 커널에 **ISR 문맥 플래그 + 중첩 깊이** 를 넣고, `sem_give_from_isr(s, &higher_woken)` 는 **깨우기만** 하고 스위치는 `isr_exit(higher_woken)`(= `portYIELD_FROM_ISR`) 에서, 그것도 **가장 바깥 ISR 이 끝날 때 한 번만** 일어나게 만든다.
- ISR 에서 막힐 수 있는 API(`sem_take` timeout≠0, `task_delay`, `mutex_take`, 태스크용 `sem_give`)를 부르면 `KERNEL_ASSERT` 가 걸리게 하고, 가드가 없으면 **ISR 이 끼어든 엉뚱한 태스크가 대신 막힌다** 는 것을 재현한다.
- IRQ → 핸들러 태스크 첫 명령까지의 지연을 step 단위로 잰다: (a) yield 함 = ISR 길이(2~3), (b) yield 생략 = 다음 tick 까지(3~10, 최악 1 tick), (c) 전부 ISR 안 = 자기 일은 빠르지만 **낮은 IRQ·tick·모든 태스크가 ISR 길이만큼 밀린다** (UART 지연 1 → 7, 제어 루프 시작 3 → 9).
- FreeRTOS V11.3.2 소스로 설명할 수 있다: `xQueueGiveFromISR` 의 `pxHigherPriorityTaskWoken`, `xYieldPendings`, Cortex-M `portYIELD` = PENDSVSET, PendSV 최저 우선순위, `configMAX_SYSCALL_INTERRUPT_PRIORITY` 와 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID`, `xTimerPendFunctionCallFromISR`.
- DMA 이중 버퍼(HT/TC) ISR → 필터 태스크 설계에서 "처리를 ISR 에 넣으면" UART overrun 11 회, "태스크로 미루면" 0 회가 나오는 이유를 타임라인으로 설명한다.

## 1. 문제

"ISR 은 짧게 하라고 하죠. 그럼 일은 어디서 하고, `xHigherPriorityTaskWoken` 과 `portYIELD_FROM_ISR` 는 정확히 뭘 하나요? ISR 에서 `xSemaphoreTake` 를 부르면요?" — FreeRTOS 를 쓰는 펌웨어 면접에서 거의 반드시 나오는 질문이다. RTOS-B08 에서 "ISR 이 세마포어로 태스크를 깨운다" 는 모양과 `woken` 의 뜻(깨운 태스크가 **끼어든 태스크보다 높을 때만** true)은 이미 다뤘다. 이 레슨은 그 한 단계 아래 — **커널이 ISR 문맥을 어떻게 알고, 스위치를 언제 하며, 금지된 호출을 어떻게 잡는지** — 를 직접 구현하고, 설계 셋의 지연을 숫자로 비교한다.

현장 예: STM32 에서 IMU 의 data-ready 핀이 EXTI 인터럽트를 걸고, ISR 은 플래그를 지우고 세마포어를 준다. ADC 를 DMA 순환 모드로 돌리면 절반이 찰 때(HT)와 끝까지 찰 때(TC) 인터럽트가 온다. 둘 다 "ISR 에서 필터까지 돌리면 안 되나?" 라는 유혹이 있다. 답은 "그 ISR 보다 낮은 모든 것의 지터(jitter)가 된다" 이고, 이 레슨의 Part B/C 가 그걸 숫자로 보인다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <rect x="10" y="30" width="150" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="55" fill="currentColor" font-weight="bold">주변장치 IRQ</text>
  <text x="22" y="75" fill="currentColor" font-size="12">EXTI / DMA HT·TC</text>
  <text x="22" y="91" fill="currentColor" font-size="12">USART RXNE</text>
  <path d="M160 65 L188 65" stroke="currentColor" stroke-width="2"/><path d="M180 59 L188 65 L180 71" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="190" y="20" width="260" height="90" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="202" y="42" fill="currentColor" font-weight="bold">ISR (짧게, in_isr = 1)</text>
  <text x="202" y="62" fill="currentColor" font-size="12">플래그 클리어, DR 읽기</text>
  <text x="202" y="80" fill="currentColor" font-size="12">xSemaphoreGiveFromISR(s, &amp;woken)</text>
  <text x="202" y="98" fill="currentColor" font-size="12">portYIELD_FROM_ISR(woken)</text>
  <path d="M450 65 L478 65" stroke="currentColor" stroke-width="2"/><path d="M470 59 L478 65 L470 71" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="480" y="30" width="250" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="492" y="55" fill="currentColor" font-weight="bold">요청만 남긴다</text>
  <text x="492" y="75" fill="currentColor" font-size="12">Cortex-M: ICSR = PENDSVSET</text>
  <text x="492" y="91" fill="currentColor" font-size="12">모델: K.pendsv = true</text>
  <path d="M605 100 L605 138" stroke="currentColor" stroke-width="2"/><path d="M599 130 L605 138 L611 130" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="480" y="140" width="250" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="492" y="163" fill="currentColor" font-weight="bold">가장 바깥 ISR 이 끝난 뒤</text>
  <text x="492" y="183" fill="currentColor" font-size="12">PendSV(최저 우선순위) 실행</text>
  <text x="492" y="199" fill="currentColor" font-size="12">→ vTaskSwitchContext()</text>
  <path d="M480 175 L452 175" stroke="currentColor" stroke-width="2"/><path d="M460 169 L452 175 L460 181" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="190" y="140" width="260" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="202" y="163" fill="currentColor" font-weight="bold">핸들러 태스크 (긴 처리)</text>
  <text x="202" y="183" fill="currentColor" font-size="12">xSemaphoreTake(s, portMAX_DELAY)</text>
  <text x="202" y="199" fill="currentColor" font-size="12">필터 · 파싱 · printf · 플래시</text>
  <text x="10" y="240" fill="currentColor">(a) yield 함: 핸들러 첫 명령 = IRQ + ISR 길이  (이 레슨 모델: 2~3 step)</text>
  <text x="10" y="260" fill="currentColor">(b) yield 생략: 다음 tick ISR 이 xYieldPendings 를 보고 바꿀 때까지 (3~10 step, 최악 1 tick)</text>
  <text x="10" y="280" fill="currentColor">(c) 처리까지 ISR 안: 자기 일은 빠르다. 대신 낮은 IRQ · tick · 모든 태스크가 ISR 길이만큼 밀린다</text>
</svg>
```

**모델 규칙** (starter.c 머리 주석과 같다. 레슨마다 커널을 따로 둔다 — 공유 라이브러리 없음):

| 항목 | 규칙 |
|---|---|
| 시간 | 단일 OS 스레드, **step** 단위. OS tick 1개 = `TICK` = 10 step (1 kHz tick 이면 1 step ≈ 100 µs 로 생각) |
| IRQ | `hwprio`(클수록 급함), 길이 `len` step, 몸체 `body(k, &woken)`. 이미 pending 인데 또 오면 `lost++` (UART 면 overrun) |
| 진입 | 매 step pending 중 **지금 실행 중인 ISR 보다 높은** 것 하나만 진입(중첩). 낮거나 같으면 기다린다 (tail-chaining 처럼 다음 step 에) |
| SysTick | IRQ 하나: hwprio 0 (최저 — FreeRTOS Cortex-M 포트와 같다), 길이 1, 몸체 = `*woken = kernel_tick()` |
| 실행 | ISR 이 있으면 맨 위 ISR 이 1 step. 마지막 step 끝에 `isr_exit(woken)`. ISR 이 없으면 현재 태스크가 일 1단위 |
| 커널 호출 | 0 step. 일 없이 막히면 같은 step 을 다음 태스크가 이어 받는다. 같은 prio 끼리 time slicing 없음 |
| 기록 | ISR 줄 `I` 실행 `i` 중첩돼 멈춤 `p` pending · 태스크 줄 `R` 실행 `r` READY 인데 못 돎 `B` 막힘 |

### Part A — ISR 문맥 플래그, give_from_isr, 블로킹 API 가드

- `isr_enter()` / `isr_exit(bool higher_woken)` — **TODO 1.** 깊이 `K.nest` 와 플래그 `K.in_isr`. `isr_exit` 은 `higher_woken` 이면 `K.pendsv = true` (요청만), 깊이를 줄이고, **가장 바깥** 이 끝났을 때만 플래그를 끄고 pending 이면 `port_switch()`.
- `sem_give_from_isr(s, &higher_woken)` — **TODO 2.** 꽉 차면 false. 아니면 count++, 최고 대기자를 READY 로. 그 태스크가 끼어든 태스크(`K.cur`)보다 높으면 `*higher_woken = true` 그리고 `K.yield_missed = true` (FreeRTOS `xYieldPendings`). **스위치는 절대 안 한다.**
- `api_guard(api, may_block)` — **TODO 3.** ISR 문맥에서 막힐(또는 스스로 스위치할) 수 있는 호출이면 `KERNEL_ASSERT` 실패 → false. `sem_take`(timeout≠0), `task_delay`, `mutex_take`, 태스크용 `sem_give` 가 이걸 부른다. `KERNEL_ASSERT` 는 RTOS-B02 처럼 훅을 바꿔 끼울 수 있다 (기본 abort, 테스트는 개수를 센다).

### Part B — 지연 측정: yield 함 / 생략 / 전부 ISR, 그리고 중첩

- `kernel_tick()` 의 반환값 — **TODO 4.** "이 tick ISR 끝에서 스위치가 필요한가" = 깨어난 태스크 중 끼어든 태스크보다 높은 것이 있거나 `K.yield_missed`.
- 하네스가 센서 ISR(hwprio 2, 2 step)을 위상 0..9 (step 20..29)에 걸고, 핸들러 태스크 H(prio 3, 처리 6 step)와 백그라운드 bg(prio 1, 항상 바쁨)로 (a)(b)(c) 를 잰다. 피해자(UART hwprio 1, 제어 태스크 ctrl prio 4 가 매 tick 1 step)를 넣은 판도 잰다.
- 중첩: OUTER(hwprio 2, 4 step)가 도는 중에 INNER(hwprio 3)가 H2(prio 5)를 깨우고 yield 를 요청한다 → 스위치는 OUTER 가 끝날 때 **1 번**.

### Part C — ADC 이중 버퍼 DMA HT/TC ISR → 필터 태스크

ADC 를 DMA 순환 모드로 돌린다. 20 step 마다 절반 하나가 차서 HT 또는 TC 인터럽트(hwprio 2)가 온다. 그 절반은 **다음 인터럽트(20 step 뒤)에 DMA 가 다시 덮기 전에** 처리(8 step)해야 한다. 같은 칩에서 UART 가 4 step 마다 1 바이트(DR 1칸, hwprio 1)를 받는다.

- `dma_isr_deferred(k, &woken)` — **TODO 5.** `dma_ack()` 로 절반 번호를 받고 `blk_push(half, K.step)` (꽉 차면 `dropped++`), 카운팅 세마포어(max 2)를 give, woken 을 넘긴다. 비교용 "필터를 ISR 안에서" 판(`dma_isr_inisr`, 길이 9)은 주어진다.
- 확인: 두 설계 모두 5 블록을 마감 안에 처리하지만, ISR 판은 UART overrun 11 회 · SysTick 지연 5 · 제어 루프 오프셋 6, 지연 판은 0 · 0 · 2.

## 2. Problem (English)

> **Deferred interrupt processing in a tick-driven mini RTOS.**
>
> You are given a deterministic step simulator with a nested-priority interrupt controller (SysTick is the lowest-priority IRQ and calls `kernel_tick()`), and a mini kernel with tasks, binary/counting semaphores and a mutex. Implement:
>
> 1. `void isr_enter(void)` / `void isr_exit(bool higher_woken)` — track ISR nesting and an `in_isr` flag. `isr_exit` is the port's `portYIELD_FROM_ISR` + return: it only *requests* a context switch; the switch (`port_switch()`) must happen exactly once, when the **outermost** ISR returns. `port_switch()` asserts if any ISR is still active.
> 2. `bool sem_give_from_isr(ksem_t *s, bool *higher_woken)` — never blocks, never switches. Return false if full. Wake the best waiter; if it outranks the interrupted task set `*higher_woken = true` (never write false) and record a missed-yield flag (`xYieldPendings`).
> 3. `bool api_guard(const char *api, bool may_block)` — `KERNEL_ASSERT` when a potentially blocking/switching API is called from ISR context. `sem_take(s, 0)` from an ISR is allowed.
> 4. `kernel_tick()` must return true when a woken task outranks the interrupted one **or** a yield was missed.
> 5. A DMA half/full-transfer ISR that hands the block to a filter task via a ring + counting semaphore.
>
> Example: ISR length 2 steps, tick = 10 steps. IRQ at step 21: with yield the handler task runs at 23; without yield at 31 (end of the next tick ISR). Doing the 6-step job inside the ISR finishes at 28 — same as (a) — but a UART IRQ raised at 30 then waits 7 steps instead of 1.
>
> Follow-ups: Why doesn't the ISR switch immediately? What happens if you ignore `xHigherPriorityTaskWoken`? How does FreeRTOS on Cortex-M catch `xSemaphoreTake()` in an ISR? What is `configMAX_SYSCALL_INTERRUPT_PRIORITY` for? With three nested ISRs each waking a task, how many context switches?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 8/43 passed, exit 1
./starter -v                                                                 # 타임라인까지
```

처음 상태(stub)는 "ISR 을 모르는 커널" 이다: `isr_enter/exit` 가 아무것도 안 하고, give 는 늘 실패하고, 가드는 늘 통과시키고, tick 은 스위치를 요청하지 않는다. 그래서 H 는 끝까지 못 돌고(`-1`), ISR 에서 `sem_take(FOREVER)` 를 부르면 끼어든 bg 가 막혀 버린다. 시뮬레이터는 step 수가 정해져 있어 멈추지 않는다. 스레드를 쓰지 않으니 `-pthread`/TSan 은 필요 없다.

진짜 FreeRTOS 데모 (커널 소스 `~/.esn_scratch/FreeRTOS-Kernel` V11.3.2. 이 폴더의 `FreeRTOSConfig.h` 는 템플릿에서 `configUSE_TICK_HOOK` 만 1 로 바꾼 것 — RTOS-B08 과 같다):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_from_isr.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_isr && ./demo_isr
```

## 4. 힌트

<details>
<summary>힌트 1 — isr_exit 는 "바꾼다" 가 아니라 "바꿔 달라고 적는다"</summary>

Cortex-M 의 `portYIELD_FROM_ISR(x)` 는 레지스터 한 비트(PENDSVSET)를 쓰고 끝난다. 실제 전환은 PendSV 라는 **가장 낮은 우선순위** 예외가 하므로, 살아 있는 ISR 이 하나라도 있으면 그게 다 끝날 때까지 미뤄진다. 모델에서는 `K.pendsv` 가 그 비트다. 깊이를 줄인 뒤 `nest > 0` 이면 그냥 돌아가고, 0 이 되었을 때만 비트를 보고 `port_switch()`.

</details>

<details>
<summary>힌트 2 — give_from_isr 는 B08 과 같고, 한 줄이 더 있다</summary>

꽉 참 → false 가 먼저. 그다음 count++, `best_waiter(s)` 를 `task_unblock`. 비교 대상은 "지금 실행 중이던 = ISR 이 끼어든" 태스크 `K.cur` 이다 (`higher_than_cur()` 가 있다). 더 높으면 `*higher_woken = true` 와 **`K.yield_missed = true`** 둘 다. 뒤의 것은 ISR 이 woken 을 버려도 다음 tick 이 구해 주는 장치다 (TODO 4 에서 쓴다).

</details>

<details>
<summary>힌트 3 — 가드는 한 줄, 하지만 왜 "끼어든 태스크" 가 위험한가</summary>

`return KERNEL_ASSERT(!(K.in_isr && may_block), "%s() called from ISR ...", api, ...)`. 가드가 없으면 `sem_take` 는 `task_block_cur()` 로 **`K.cur` 를** 막는다. ISR 안에서 `K.cur` 는 ISR 자신이 아니라 ISR 이 끼어든 아무 태스크다. 그 태스크는 세마포어와 아무 관계가 없는데 막힌다.

</details>

<details>
<summary>힌트 4 — (b) 의 숫자 예측: 다음 tick 은 언제?</summary>

IRQ 가 s 에 오면 ISR 은 s, s+1 에 돌고 s+2 에 끝난다. 그 사이(또는 s+2 에) tick 이 걸려 있으면 tick ISR 이 바로 이어 돌고 H 는 그다음 step. 아니면 다음 10 의 배수 T 에서 tick ISR, H 는 T+1. 그래서 s = 21 → 31 (지연 10), s = 27 → 31 (지연 4). (연습 3 에서 이 식을 80 위상에 대해 검증한다.)

</details>

<details>
<summary>힌트 5 — DMA ISR: 데이터는 링으로, 신호는 세마포어로</summary>

카운팅 세마포어는 "몇 개가 쌓였나" 만 안다. **어느 절반인지와 언제 찼는지** 는 디스크립터 링(`blk_push`)으로 넘긴다. 순서: push(실패하면 버리고 끝) → give → woken 전달. give 를 먼저 하면 태스크가 빈 링을 볼 수 있다 — 모델에선 ISR 이 끝나야 태스크가 돌아서 안 보이지만, 멀티코어나 우선순위가 다른 생산자가 둘이면 진짜 버그다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨의 모델 | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `K.in_isr` | Cortex-M: `xPortIsInsideInterrupt()` — IPSR ≠ 0 (ARM_CM4F/portmacro.h:191–209) | `k_is_in_isr()` | (RTX 내부에서 IPSR 확인) |
| `isr_enter/isr_exit` | 하드웨어 예외 진입/복귀 + `portEND_SWITCHING_ISR` | 커널 ISR 래퍼가 처리 | 커널이 처리 |
| `sem_give_from_isr(s,&w)` | `xSemaphoreGiveFromISR(s,&w)` = `xQueueGiveFromISR` (semphr.h:640) | `k_sem_give()` (ISR 가능) | `osSemaphoreRelease()` (ISR 가능) |
| `isr_exit(w)` 의 요청 | `portYIELD_FROM_ISR(w)` → `portYIELD()` = PENDSVSET (ARM_CM4F/portmacro.h:88–114) | 없음 — ISR 끝에서 커널이 판단 | 없음 |
| `K.yield_missed` | `xYieldPendings[0]` (tasks.c:5564 설정, 5020 tick 에서 사용, 5227 지움) | — | — |
| `api_guard` | CM4F `vPortEnterCritical` 의 VECTACTIVE assert (port.c:475–489), 일부 포트 `portASSERT_IF_IN_ISR` | `k_sem_take` 는 ISR 에서 `K_NO_WAIT` 만 | `osSemaphoreAcquire` 는 ISR 에서 timeout 0 만 |
| `sem_take(s, 0)` in ISR | `xSemaphoreTakeFromISR` (semphr.h:675) | `k_sem_take(&s, K_NO_WAIT)` | `osSemaphoreAcquire(s, 0)` |
| `kernel_tick()` 반환 | `xTaskIncrementTick()` → `xSwitchRequired` | `sys_clock_announce()` | — |
| 연습 2 `pend_call_from_isr` | `xTimerPendFunctionCallFromISR` (timers.c:1238) | `k_work_submit()` (시스템 워크큐) | — |
| 연습 4 `g_syscall_max` | `configMAX_SYSCALL_INTERRUPT_PRIORITY` + `vPortValidateInterruptPriority` (port.c:851, 888) | `CONFIG_ZERO_LATENCY_IRQS` | — |

우선순위 방향: 이 모델과 FreeRTOS **태스크** 우선순위는 클수록 급하다. Cortex-M **인터럽트** 우선순위(NVIC)는 숫자가 **작을수록** 급하다 — 모델의 `hwprio` 는 이해하기 쉽게 "클수록 급함" 으로 뒤집어 놓았다. 그리고 두 우선순위 공간은 별개다: 가장 낮은 IRQ 도 가장 높은 태스크를 선점한다.

### Part A — 깨우기와 바꾸기를 나눈다

```c
static void isr_enter(void) { K.nest++; K.in_isr = true; }
static void isr_exit(bool higher_woken) {
    if (higher_woken) K.pendsv = true;            /* portYIELD_FROM_ISR(pdTRUE): SCB->ICSR = PENDSVSET */
    K.nest--;
    if (K.nest > 0) return;                       /* 바깥 ISR 로 복귀 — 여기서 바꾸면 안 된다 */
    K.in_isr = false;
    if (K.pendsv) { K.pendsv = false; port_switch(); }   /* 가장 바깥 ISR 이 끝난 직후 = PendSV 실행 */
}
```

왜 ISR 안에서 바로 바꾸면 안 되나? ISR 은 태스크가 아니다. 지금 CPU 에는 ISR 의 스택 프레임(Cortex-M 은 MSP 위)이 있고, 그 아래 바깥 ISR 이 더 있을 수 있다. 여기서 태스크 문맥을 바꾸면 "돌아갈 ISR" 을 잃는다. 그래서 모델의 `port_switch()` 는 `KERNEL_ASSERT(K.nest == 0, ...)` 로 시작한다. FreeRTOS Cortex-M 포트는 이것을 **하드웨어 우선순위로** 해결한다:

```c
/* FreeRTOS-Kernel V11.3.2 portable/GCC/ARM_CM4F — 발췌, 줄 번호는 실제 파일 기준 */
portmacro.h:88   #define portYIELD()  { portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;  /* 0xe000ed04, 1<<28 */
                                       __asm volatile ( "dsb" ::: "memory" ); __asm volatile ( "isb" ); }
portmacro.h:101  #define portEND_SWITCHING_ISR( xSwitchRequired )  /* 101–113 요약: 참이면 portYIELD(), 아니면 trace 만 */
portmacro.h:114  #define portYIELD_FROM_ISR( x )    portEND_SWITCHING_ISR( x )
port.c:433       portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;     /* PendSV = 최저 우선순위 (255) */
port.c:434       portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;    /* SysTick 도 최저 */
```

PendSV 가 최저 우선순위이니 pending 비트를 세워도 **다른 ISR 이 하나라도 실행 중이면 들어오지 못한다** — 가장 바깥 ISR 이 끝나야 (그리고 PendSV 보다 높은, 기다리던 IRQ 들이 tail-chaining 으로 먼저 돈 뒤에야) PendSV 가 돈다 (ARM 문서 기준). 하드웨어가 아닌 소프트웨어로 같은 일을 하는 포트도 있다. Cortex-A9 포트는 `portEND_SWITCHING_ISR` 이 `ulPortYieldRequired = pdTRUE` 만 쓰고(ARM_CA9/portmacro.h:80–88), 공통 IRQ 핸들러가 깊이를 확인한다:

```text
ARM_CA9/portASM.S:223  /* Restore the old nesting count. */
ARM_CA9/portASM.S:226  /* A context switch is never performed if the nesting count is not 0. */
ARM_CA9/portASM.S:236  BNE     switch_before_exit          /* 깊이 0 이고 ulPortYieldRequired 면 전환 */
```

모델의 `isr_exit` 이 바로 이 구조다. (모델은 가장 바깥 ISR 이 끝나는 순간 바꾸고, 그 뒤 기다리던 IRQ 가 있으면 다음 step 에 그 IRQ 가 먼저 돈다. 태스크 쪽 타임라인은 PendSV 가 tail-chaining 뒤에 도는 실제와 같다.)

**give_from_isr** 는 RTOS-B08 의 것에 한 줄(`K.yield_missed`)이 더 있다:

```c
static bool sem_give_from_isr(ksem_t *s, bool *higher_woken) {
    if (s->count >= s->max) return false;
    s->count++;
    int w = best_waiter(s);
    if (w >= 0) {
        task_unblock(w);                          /* READY 로만 — 깨어난 태스크가 다시 take 해서 가져간다 */
        if (higher_than_cur(w)) { *higher_woken = true; K.yield_missed = true; }
    }
    return true;
}
```

FreeRTOS 도 같은 두 기록을 남긴다. `xQueueGiveFromISR` (queue.c:1338~) 는 1376 에서 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`, 1381 에서 `taskENTER_CRITICAL_FROM_ISR()`, 1400 에서 count+1, 1433 에서 `xTaskRemoveFromEventList()` 가 pdTRUE 면 1439 에서 `*pxHigherPriorityTaskWoken = pdTRUE` (false 는 안 쓴다), 꽉 차면 1498 `errQUEUE_FULL`. 그리고 `xTaskRemoveFromEventList` 안에서:

```c
/* tasks.c:5555–5564 */
if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
{
    xReturn = pdTRUE;
    /* Mark that a yield is pending in case the user is not using the
     * "xHigherPriorityTaskWoken" parameter to an ISR safe FreeRTOS function. */
    xYieldPendings[ 0 ] = pdTRUE;
}
```

**가드** 는 한 줄이다. `./solution -v` 는 세어진 assert 를 `(KERNEL_ASSERT counted) sem_take() called from ISR (nest=1)` 처럼 찍는다 — Part A 에서 5 줄 (sem_take 2, task_delay, mutex_take, sem_give):

| ISR 에서 호출 | 모델 결과 | 이유 |
|---|---|---|
| `sem_take(s, FOREVER)` / `(s, 5)` | `K_ERR_ISR` + assert | 막힐 수 있다 — 막히는 건 끼어든 태스크 |
| `sem_take(s, 0)` | 허용 (`K_EMPTY`) | 막히지 않는다 = `xSemaphoreTakeFromISR` |
| `task_delay(3)` | `K_ERR_ISR` + assert | ISR 은 "자기" 가 없다 |
| `mutex_take(m, 0)` | `K_ERR_ISR` + assert (timeout 0 이어도) | 주인이 태스크여야 상속이 된다 (RTOS-M04). ISR 이 주인이면 `K.cur` 가 주인이 되는 엉터리 |
| 태스크용 `sem_give(s)` | `K_ERR_ISR` + assert | 높은 태스크를 깨우면 **그 자리에서** 스위치한다 |
| `sem_give_from_isr` | 허용 | 깨우기만 |

진짜 FreeRTOS 는 어떻게 잡나? 공통 코드에는 "ISR 인가?" 검사가 거의 없다. 대신 **포트** 가 잡는다. Cortex-M4F 포트는 태스크용 API 가 반드시 지나가는 `taskENTER_CRITICAL()` → `vPortEnterCritical()` 에서:

```c
/* ARM_CM4F/port.c:475–489 발췌 */
void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;
    /* This is not the interrupt safe version of the enter critical function so
     * assert() if it is being called from an interrupt context.  Only API
     * functions that end in "FromISR" can be used in an interrupt. ... */
    if( uxCriticalNesting == 1 )
    {
        configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );
    }
}
```

`xSemaphoreTake` (= `xQueueSemaphoreTake`, queue.c:1659) 는 1688 에서 `taskENTER_CRITICAL()` 을 부르므로 ISR 에서 부르면 여기서 걸린다 (`configASSERT` 가 정의돼 있을 때만). `portASSERT_IF_IN_ISR()` 를 정의하는 포트도 있다 — PIC32MX (`configASSERT( uxInterruptNesting == 0 )`, MPLAB/PIC32MX/portmacro.h:183), ESP32 (ThirdParty/GCC/Xtensa_ESP32/include/portmacro.h:227), XCORE.AI. 정의하지 않으면 FreeRTOS.h:2799–2801 에서 빈 매크로가 된다. **POSIX 포트에는 둘 다 없다** (`portASSERT_IF_IN_ISR`, `portASSERT_IF_INTERRUPT_PRIORITY_INVALID` 둘 다 Posix/portmacro.h 에 정의 없음 → 빈 매크로) — 호스트에서 ISR 규칙 위반을 커널이 잡아 주지 않는다는 뜻이다.

### Part B — 세 설계의 지연을 잰다

TODO 4 는 반환값 한 줄이다:

```c
return higher || K.yield_missed;              /* xYieldPendings 가 woken 을 버린 ISR 을 구한다 */
```

FreeRTOS 도 tick 마다 같은 것을 본다 (tasks.c:5016–5022, `configUSE_PREEMPTION == 1`):

```c
if( xYieldPendings[ 0 ] != pdFALSE )
{
    xSwitchRequired = pdTRUE;
}
```

그리고 Cortex-M 의 `xPortSysTickHandler` (port.c:560~) 는 `xTaskIncrementTick()` 이 pdTRUE 면 PENDSVSET 을 쓴다. 즉 `portYIELD_FROM_ISR` 를 빼먹어도 **영원히** 늦지는 않고, 다음 tick 까지 늦는다.

실제 출력 (`./solution`):

```text
[Part B] IRQ → 핸들러 태스크 첫 명령까지 (step, 1 tick = 10 step, ISR 2 step, 처리 6 step)
  phase | (a) yield  (b) no-yield | (a) done  (b) done  (c) in-ISR done
    0   |    3           3       |    28        28         27
    1   |    2          10       |    28        36         28
    2   |    2           9       |    29        36         29
    3   |    2           8       |    31        36         30
    4   |    2           7       |    32        36         31
    5   |    2           6       |    33        36         32
    6   |    2           5       |    34        36         33
    7   |    2           4       |    35        36         34
    8   |    3           3       |    36        36         35
    9   |    3           3       |    37        37         36
  (a) min/max/sum 2/3/23   (b) min/max/sum 3/10/58
```

| 설계 | 핸들러 첫 명령 지연 (step) | 무엇이 정하나 |
|---|---|---|
| (a) give + `portYIELD_FROM_ISR` | 2 ~ 3 (평균 2.3) | ISR 길이. tick ISR 이 겹치면 +1 |
| (b) yield 생략 | 3 ~ 10 (평균 5.8) | **다음 tick 위치** — IRQ 위상에 따라 0 ~ 1 tick 의 지터 |
| (c) 처리까지 ISR 안 | 처리 시작 = s (ISR 진입 즉시) | 자기 처리 완료는 (a) 와 같거나 1 빠름 |

위상 1 (IRQ@21) 의 실제 타임라인을 그림으로 옮기면:

```svg
<svg viewBox="0 0 740 374" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">(a) give + portYIELD_FROM_ISR — H 첫 명령 step 23 (지연 2)</text> <text x="119" y="38" fill="currentColor" font-size="12">20</text> <text x="239" y="38" fill="currentColor" font-size="12">25</text> <text x="359" y="38" fill="currentColor" font-size="12">30</text> <text x="479" y="38" fill="currentColor" font-size="12">35</text> <text x="599" y="38" fill="currentColor" font-size="12">40</text> <text x="10" y="54" fill="currentColor" font-size="12">SENS</text> <rect x="142" y="42" width="23" height="15" class="f-warn"/> <rect x="166" y="42" width="23" height="15" class="f-warn"/> <text x="10" y="73" fill="currentColor" font-size="12">SysTk</text> <rect x="118" y="61" width="23" height="15" class="f-warn"/> <rect x="358" y="61" width="23" height="15" class="f-warn"/> <rect x="598" y="61" width="23" height="15" class="f-warn"/> <text x="10" y="92" fill="currentColor" font-size="12">H</text>
  <rect x="166" y="90" width="23" height="5" class="f-ok"/> <rect x="190" y="80" width="23" height="15" class="f-acc"/> <rect x="214" y="80" width="23" height="15" class="f-acc"/> <rect x="238" y="80" width="23" height="15" class="f-acc"/> <rect x="262" y="80" width="23" height="15" class="f-acc"/> <rect x="286" y="80" width="23" height="15" class="f-acc"/> <rect x="310" y="80" width="23" height="15" class="f-acc"/> <text x="10" y="111" fill="currentColor" font-size="12">bg</text> <rect x="70" y="99" width="23" height="15" class="f-acc"/> <rect x="94" y="99" width="23" height="15" class="f-acc"/> <rect x="118" y="109" width="23" height="5" class="f-ok"/> <rect x="142" y="109" width="23" height="5" class="f-ok"/> <rect x="166" y="109" width="23" height="5" class="f-ok"/> <rect x="190" y="109" width="23" height="5" class="f-ok"/>
  <rect x="214" y="109" width="23" height="5" class="f-ok"/> <rect x="238" y="109" width="23" height="5" class="f-ok"/> <rect x="262" y="109" width="23" height="5" class="f-ok"/> <rect x="286" y="109" width="23" height="5" class="f-ok"/> <rect x="310" y="109" width="23" height="5" class="f-ok"/> <rect x="334" y="99" width="23" height="15" class="f-acc"/> <rect x="358" y="109" width="23" height="5" class="f-ok"/> <rect x="382" y="99" width="23" height="15" class="f-acc"/> <rect x="406" y="99" width="23" height="15" class="f-acc"/> <rect x="430" y="99" width="23" height="15" class="f-acc"/> <rect x="454" y="99" width="23" height="15" class="f-acc"/> <rect x="478" y="99" width="23" height="15" class="f-acc"/> <rect x="502" y="99" width="23" height="15" class="f-acc"/> <rect x="526" y="99" width="23" height="15" class="f-acc"/>
  <rect x="550" y="99" width="23" height="15" class="f-acc"/> <rect x="574" y="99" width="23" height="15" class="f-acc"/> <rect x="598" y="109" width="23" height="5" class="f-ok"/> <rect x="622" y="99" width="23" height="15" class="f-acc"/> <text x="10" y="130" fill="currentColor" font-weight="bold">(b) yield 생략 — H 는 다음 SysTick(30) 끝에서, step 31 (지연 10)</text> <text x="119" y="148" fill="currentColor" font-size="12">20</text> <text x="239" y="148" fill="currentColor" font-size="12">25</text> <text x="359" y="148" fill="currentColor" font-size="12">30</text> <text x="479" y="148" fill="currentColor" font-size="12">35</text> <text x="599" y="148" fill="currentColor" font-size="12">40</text> <text x="10" y="164" fill="currentColor" font-size="12">SENS</text> <rect x="142" y="152" width="23" height="15" class="f-warn"/> <rect x="166" y="152" width="23" height="15" class="f-warn"/> <text x="10" y="183" fill="currentColor" font-size="12">SysTk</text>
  <rect x="118" y="171" width="23" height="15" class="f-warn"/> <rect x="358" y="171" width="23" height="15" class="f-warn"/> <rect x="598" y="171" width="23" height="15" class="f-warn"/> <text x="10" y="202" fill="currentColor" font-size="12">H</text> <rect x="166" y="200" width="23" height="5" class="f-ok"/> <rect x="190" y="200" width="23" height="5" class="f-ok"/> <rect x="214" y="200" width="23" height="5" class="f-ok"/> <rect x="238" y="200" width="23" height="5" class="f-ok"/> <rect x="262" y="200" width="23" height="5" class="f-ok"/> <rect x="286" y="200" width="23" height="5" class="f-ok"/> <rect x="310" y="200" width="23" height="5" class="f-ok"/> <rect x="334" y="200" width="23" height="5" class="f-ok"/> <rect x="358" y="200" width="23" height="5" class="f-ok"/> <rect x="382" y="190" width="23" height="15" class="f-acc"/>
  <rect x="406" y="190" width="23" height="15" class="f-acc"/> <rect x="430" y="190" width="23" height="15" class="f-acc"/> <rect x="454" y="190" width="23" height="15" class="f-acc"/> <rect x="478" y="190" width="23" height="15" class="f-acc"/> <rect x="502" y="190" width="23" height="15" class="f-acc"/> <text x="10" y="221" fill="currentColor" font-size="12">bg</text> <rect x="70" y="209" width="23" height="15" class="f-acc"/> <rect x="94" y="209" width="23" height="15" class="f-acc"/> <rect x="118" y="219" width="23" height="5" class="f-ok"/> <rect x="142" y="219" width="23" height="5" class="f-ok"/> <rect x="166" y="219" width="23" height="5" class="f-ok"/> <rect x="190" y="209" width="23" height="15" class="f-acc"/> <rect x="214" y="209" width="23" height="15" class="f-acc"/> <rect x="238" y="209" width="23" height="15" class="f-acc"/>
  <rect x="262" y="209" width="23" height="15" class="f-acc"/> <rect x="286" y="209" width="23" height="15" class="f-acc"/> <rect x="310" y="209" width="23" height="15" class="f-acc"/> <rect x="334" y="209" width="23" height="15" class="f-acc"/> <rect x="358" y="219" width="23" height="5" class="f-ok"/> <rect x="382" y="219" width="23" height="5" class="f-ok"/> <rect x="406" y="219" width="23" height="5" class="f-ok"/> <rect x="430" y="219" width="23" height="5" class="f-ok"/> <rect x="454" y="219" width="23" height="5" class="f-ok"/> <rect x="478" y="219" width="23" height="5" class="f-ok"/> <rect x="502" y="219" width="23" height="5" class="f-ok"/> <rect x="526" y="209" width="23" height="15" class="f-acc"/> <rect x="550" y="209" width="23" height="15" class="f-acc"/> <rect x="574" y="209" width="23" height="15" class="f-acc"/>
  <rect x="598" y="219" width="23" height="5" class="f-ok"/> <rect x="622" y="209" width="23" height="15" class="f-acc"/> <text x="10" y="240" fill="currentColor" font-weight="bold">(c) 처리까지 ISR 안 — 21..28 ISR 8 step, 완료 28 (a 와 같음)</text> <text x="119" y="258" fill="currentColor" font-size="12">20</text> <text x="239" y="258" fill="currentColor" font-size="12">25</text> <text x="359" y="258" fill="currentColor" font-size="12">30</text> <text x="479" y="258" fill="currentColor" font-size="12">35</text> <text x="599" y="258" fill="currentColor" font-size="12">40</text> <text x="10" y="274" fill="currentColor" font-size="12">SENS</text> <rect x="142" y="262" width="23" height="15" class="f-warn"/> <rect x="166" y="262" width="23" height="15" class="f-warn"/> <rect x="190" y="262" width="23" height="15" class="f-warn"/> <rect x="214" y="262" width="23" height="15" class="f-warn"/> <rect x="238" y="262" width="23" height="15" class="f-warn"/>
  <rect x="262" y="262" width="23" height="15" class="f-warn"/> <rect x="286" y="262" width="23" height="15" class="f-warn"/> <rect x="310" y="262" width="23" height="15" class="f-warn"/> <text x="10" y="293" fill="currentColor" font-size="12">SysTk</text> <rect x="118" y="281" width="23" height="15" class="f-warn"/> <rect x="358" y="281" width="23" height="15" class="f-warn"/> <rect x="598" y="281" width="23" height="15" class="f-warn"/> <text x="10" y="312" fill="currentColor" font-size="12">H</text> <text x="10" y="331" fill="currentColor" font-size="12">bg</text> <rect x="70" y="319" width="23" height="15" class="f-acc"/> <rect x="94" y="319" width="23" height="15" class="f-acc"/> <rect x="118" y="329" width="23" height="5" class="f-ok"/> <rect x="142" y="329" width="23" height="5" class="f-ok"/> <rect x="166" y="329" width="23" height="5" class="f-ok"/>
  <rect x="190" y="329" width="23" height="5" class="f-ok"/> <rect x="214" y="329" width="23" height="5" class="f-ok"/> <rect x="238" y="329" width="23" height="5" class="f-ok"/> <rect x="262" y="329" width="23" height="5" class="f-ok"/> <rect x="286" y="329" width="23" height="5" class="f-ok"/> <rect x="310" y="329" width="23" height="5" class="f-ok"/> <rect x="334" y="319" width="23" height="15" class="f-acc"/> <rect x="358" y="329" width="23" height="5" class="f-ok"/> <rect x="382" y="319" width="23" height="15" class="f-acc"/> <rect x="406" y="319" width="23" height="15" class="f-acc"/> <rect x="430" y="319" width="23" height="15" class="f-acc"/> <rect x="454" y="319" width="23" height="15" class="f-acc"/> <rect x="478" y="319" width="23" height="15" class="f-acc"/> <rect x="502" y="319" width="23" height="15" class="f-acc"/>
  <rect x="526" y="319" width="23" height="15" class="f-acc"/> <rect x="550" y="319" width="23" height="15" class="f-acc"/> <rect x="574" y="319" width="23" height="15" class="f-acc"/> <rect x="598" y="329" width="23" height="5" class="f-ok"/> <rect x="622" y="319" width="23" height="15" class="f-acc"/> <rect x="10" y="350" width="14" height="13" class="f-acc"/> <text x="28" y="361" fill="currentColor" font-size="12">R 태스크 실행</text> <rect x="160" y="358" width="14" height="5" class="f-ok"/> <text x="178" y="361" fill="currentColor" font-size="12">r READY(대기)</text> <rect x="310" y="350" width="14" height="13" class="f-warn"/> <text x="328" y="361" fill="currentColor" font-size="12">I ISR 실행</text> <rect x="460" y="350" width="14" height="13" class="f-mut"/> <text x="478" y="361" fill="currentColor" font-size="12">i 중첩돼 멈춘 ISR</text> <rect x="610" y="350" width="14" height="13" fill="none" stroke="currentColor" stroke-dasharray="2 2"/>
  <text x="628" y="361" fill="currentColor" font-size="12">p pending</text>
</svg>
```

(b) 에서 H 는 22 부터 `r`(READY)인데 bg 가 계속 돈다 — 스위치를 요청한 사람이 없기 때문이다. 30 의 tick ISR 이 `K.yield_missed` 를 보고 바꾼다. **(c) 가 혼자서는 (a) 와 똑같이 28 에 끝난다** 는 것이 함정이다. 비용은 다른 데서 나온다. 같은 IRQ 를 29 에 걸고, UART 바이트가 30 에 오고, 매 tick 1 step 씩 도는 제어 태스크(prio 4)를 넣은 결과:

```text
  -- (a) yield + victims, IRQ@29                -- (c) all work in ISR + victims, IRQ@29
  step :   2         3         4                 step :   2         3         4
         89012345678901234567890123                     89012345678901234567890123
  SENS : ...........II.............              SENS : ...........IIIIIIII.......
  UART : ............pI............              UART : ............pppppppI......
  SysTk: ..I.........ppI.......I...              SysTk: ..I.........ppppppppI.I...
  ctrl : BBrRBBBBBBBBBBrRBBBBBBrRBB              ctrl : BBrRBBBBBBBBBBBBBBBBrRrRBB
  H    : BBBBBBBBBBBBrrrrRRRRRRrrBB              H    : BBBBBBBBBBBBBBBBBBBBBBBBBB
  bg   : RRrrRRRRRRRrrrrrrrrrrrrrRR              bg   : RRrrRRRRRRRrrrrrrrrrrrrrRR

  design          | H start  work done | UART lat  SysTick lat max  ctrl offset max
  yield at exit   |    34       39    |    1           2               3
  all work in ISR |     -       36    |    7           8               9
```

(두 타임라인은 같은 `./solution -v` 출력을 나란히 붙인 것.) (c) 는 자기 일을 36 에 끝내 (a) 의 39 보다 빠르다. 대신 UART 는 7 step, SysTick 은 8 step 기다리고, 제어 루프는 tick 경계에서 9 step 뒤에야 시작한다 — 1 tick 이 10 step 이니 **거의 한 주기를 통째로 잃었다**. (a) 에서는 H 의 처리가 태스크 prio 3 이라 ctrl(4)이 그냥 선점한다: 처리 중인 일이 있어도 ctrl 은 3 에 시작한다. 정리하면 "ISR 에서 처리 = 그 처리 시간이 **그보다 낮은 모든 IRQ 와 모든 태스크** 의 지연에 더해진다". 태스크로 미루면 그 시간은 **처리 태스크보다 낮은 태스크들** 의 지연에만 더해진다 — 우선순위로 누가 비용을 낼지 고를 수 있게 된다.

**중첩** — OUTER(hwprio 2, 4 step)가 12 에, INNER(hwprio 3, 1 step)가 13 에 온다. OUTER 는 H1(3)을, INNER 는 H2(5)를 깨우고 둘 다 woken 이다:

```svg
<svg viewBox="0 0 740 236" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">중첩 ISR — solution -v 의 실제 타임라인 (OUTER hwprio 2, INNER hwprio 3)</text> <text x="71" y="38" fill="currentColor" font-size="12">10</text> <text x="221" y="38" fill="currentColor" font-size="12">15</text> <text x="371" y="38" fill="currentColor" font-size="12">20</text> <text x="10" y="54" fill="currentColor" font-size="12">INNER</text> <rect x="160" y="42" width="29" height="15" class="f-warn"/> <text x="10" y="73" fill="currentColor" font-size="12">OUTER</text> <rect x="130" y="61" width="29" height="15" class="f-warn"/> <rect x="160" y="61" width="29" height="15" class="f-mut"/> <rect x="190" y="61" width="29" height="15" class="f-warn"/> <rect x="220" y="61" width="29" height="15" class="f-warn"/> <rect x="250" y="61" width="29" height="15" class="f-warn"/> <text x="10" y="92" fill="currentColor" font-size="12">SysTk</text> <rect x="70" y="80" width="29" height="15" class="f-warn"/>
  <rect x="370" y="80" width="29" height="15" class="f-warn"/> <text x="10" y="111" fill="currentColor" font-size="12">H2</text> <rect x="160" y="109" width="29" height="5" class="f-ok"/> <rect x="190" y="109" width="29" height="5" class="f-ok"/> <rect x="220" y="109" width="29" height="5" class="f-ok"/> <rect x="250" y="109" width="29" height="5" class="f-ok"/> <rect x="280" y="99" width="29" height="15" class="f-acc"/> <rect x="310" y="99" width="29" height="15" class="f-acc"/> <text x="10" y="130" fill="currentColor" font-size="12">H1</text> <rect x="130" y="128" width="29" height="5" class="f-ok"/> <rect x="160" y="128" width="29" height="5" class="f-ok"/> <rect x="190" y="128" width="29" height="5" class="f-ok"/> <rect x="220" y="128" width="29" height="5" class="f-ok"/> <rect x="250" y="128" width="29" height="5" class="f-ok"/>
  <rect x="280" y="128" width="29" height="5" class="f-ok"/> <rect x="310" y="128" width="29" height="5" class="f-ok"/> <rect x="340" y="118" width="29" height="15" class="f-acc"/> <rect x="370" y="128" width="29" height="5" class="f-ok"/> <rect x="400" y="118" width="29" height="15" class="f-acc"/> <text x="10" y="149" fill="currentColor" font-size="12">bg</text> <rect x="70" y="147" width="29" height="5" class="f-ok"/> <rect x="100" y="137" width="29" height="15" class="f-acc"/> <rect x="130" y="147" width="29" height="5" class="f-ok"/> <rect x="160" y="147" width="29" height="5" class="f-ok"/> <rect x="190" y="147" width="29" height="5" class="f-ok"/> <rect x="220" y="147" width="29" height="5" class="f-ok"/> <rect x="250" y="147" width="29" height="5" class="f-ok"/> <rect x="280" y="147" width="29" height="5" class="f-ok"/>
  <rect x="310" y="147" width="29" height="5" class="f-ok"/> <rect x="340" y="147" width="29" height="5" class="f-ok"/> <rect x="370" y="147" width="29" height="5" class="f-ok"/> <rect x="400" y="147" width="29" height="5" class="f-ok"/> <rect x="430" y="137" width="29" height="15" class="f-acc"/> <rect x="460" y="137" width="29" height="15" class="f-acc"/> <path d="M190 160 L190 31" stroke="currentColor" stroke-dasharray="3 3" fill="none"/> <text x="130" y="174" fill="currentColor" font-size="12">13: INNER 끝, yield 요청 → PendSV pending 만</text> <path d="M280 182 L280 31" stroke="currentColor" stroke-width="2" fill="none"/> <text x="220" y="196" fill="currentColor" font-size="12">16 끝: 가장 바깥 OUTER 끝 → 스위치 1번 → 17 에 H2(5)</text> <rect x="10" y="212" width="14" height="13" class="f-acc"/> <text x="28" y="223" fill="currentColor" font-size="12">R 태스크 실행</text> <rect x="160" y="220" width="14" height="5" class="f-ok"/> <text x="178" y="223" fill="currentColor" font-size="12">r READY(대기)</text>
  <rect x="310" y="212" width="14" height="13" class="f-warn"/> <text x="328" y="223" fill="currentColor" font-size="12">I ISR 실행</text> <rect x="460" y="212" width="14" height="13" class="f-mut"/> <text x="478" y="223" fill="currentColor" font-size="12">i 중첩돼 멈춘 ISR</text> <rect x="610" y="212" width="14" height="13" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <text x="628" y="223" fill="currentColor" font-size="12">p pending</text>
</svg>
```

(그림은 `./solution -v` 의 `nested` 타임라인을 그대로 옮긴 것.) 13 끝에 INNER 가 yield 를 요청하지만 깊이가 2 → 1 이라 `K.pendsv` 만 남는다. 16 끝에 OUTER 가 끝나며 깊이 0 → 스위치 **1 번** (yield 요청 2 개). 그때 `sched_pick_next()` 가 가장 높은 H2 를 고른다. 측정 구간의 스위치는 bg→H2, H2→H1, H1→bg 의 3 번이고 `KERNEL_ASSERT` 는 0 번.

### Part C — DMA HT/TC: 처리를 ISR 에 넣으면 UART 가 깨진다

```c
static void dma_isr_deferred(int k, bool *woken) {
    (void)k;
    int half = dma_ack();                          /* DMA1->IFCR 에 HTIF/TCIF 클리어 */
    if (!blk_push(half, K.step)) { g_c.dropped++; return; }
    bool w = false;
    sem_give_from_isr(&g_blk_sem, &w);
    *woken = w;                                    /* portYIELD_FROM_ISR(w) */
}
```

실제 출력:

```text
[Part C] ADC 이중 버퍼 DMA HT/TC ISR → 필터 태스크 (절반 20 step 마다, 처리 8 step, UART 바이트 4 step 마다)
  design   | blocks ok late drop | resp_max | UART overrun lat_max | SysTick lat_max | ctrl off_max
  in ISR   |         5    0    0 |     9    |       11        9     |        5        |       6
  deferred |         5    0    0 |    15    |        0        1     |        0        |       2
```

```svg
<svg viewBox="0 0 740 302" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">in ISR: DMA ISR 이 9 step — UART 바이트가 쌓여 overrun, SysTick 이 5 step 밀림</text> <text x="61" y="38" fill="currentColor" font-size="12">0</text> <text x="126" y="38" fill="currentColor" font-size="12">5</text> <text x="191" y="38" fill="currentColor" font-size="12">10</text> <text x="256" y="38" fill="currentColor" font-size="12">15</text> <text x="321" y="38" fill="currentColor" font-size="12">20</text> <text x="386" y="38" fill="currentColor" font-size="12">25</text> <text x="451" y="38" fill="currentColor" font-size="12">30</text> <text x="516" y="38" fill="currentColor" font-size="12">35</text> <text x="581" y="38" fill="currentColor" font-size="12">40</text> <text x="646" y="38" fill="currentColor" font-size="12">45</text> <text x="10" y="54" fill="currentColor" font-size="12">DMA</text> <rect x="125" y="42" width="12" height="15" class="f-warn"/> <rect x="138" y="42" width="12" height="15" class="f-warn"/>
  <rect x="151" y="42" width="12" height="15" class="f-warn"/> <rect x="164" y="42" width="12" height="15" class="f-warn"/> <rect x="177" y="42" width="12" height="15" class="f-warn"/> <rect x="190" y="42" width="12" height="15" class="f-warn"/> <rect x="203" y="42" width="12" height="15" class="f-warn"/> <rect x="216" y="42" width="12" height="15" class="f-warn"/> <rect x="229" y="42" width="12" height="15" class="f-warn"/> <rect x="385" y="42" width="12" height="15" class="f-warn"/> <rect x="398" y="42" width="12" height="15" class="f-warn"/> <rect x="411" y="42" width="12" height="15" class="f-warn"/> <rect x="424" y="42" width="12" height="15" class="f-warn"/> <rect x="437" y="42" width="12" height="15" class="f-warn"/> <rect x="450" y="42" width="12" height="15" class="f-warn"/> <rect x="463" y="42" width="12" height="15" class="f-warn"/>
  <rect x="476" y="42" width="12" height="15" class="f-warn"/> <rect x="489" y="42" width="12" height="15" class="f-warn"/> <rect x="645" y="42" width="12" height="15" class="f-warn"/> <rect x="658" y="42" width="12" height="15" class="f-warn"/> <rect x="671" y="42" width="12" height="15" class="f-warn"/> <rect x="684" y="42" width="12" height="15" class="f-warn"/> <rect x="697" y="42" width="12" height="15" class="f-warn"/> <text x="10" y="73" fill="currentColor" font-size="12">UART</text> <rect x="73" y="61" width="12" height="15" class="f-warn"/> <rect x="126" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="139" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="152" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="165" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="178" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/>
  <rect x="191" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="204" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="217" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="230" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="242" y="61" width="12" height="15" class="f-warn"/> <rect x="281" y="61" width="12" height="15" class="f-warn"/> <rect x="333" y="61" width="12" height="15" class="f-warn"/> <rect x="386" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="399" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="412" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="425" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="438" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="451" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="464" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/>
  <rect x="477" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="490" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="502" y="61" width="12" height="15" class="f-warn"/> <rect x="541" y="61" width="12" height="15" class="f-warn"/> <rect x="593" y="61" width="12" height="15" class="f-warn"/> <rect x="646" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="659" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="672" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="685" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="698" y="63" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <text x="10" y="92" fill="currentColor" font-size="12">SysTk</text> <rect x="60" y="80" width="12" height="15" class="f-warn"/> <rect x="191" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="204" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/>
  <rect x="217" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="230" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="243" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="255" y="80" width="12" height="15" class="f-warn"/> <rect x="320" y="80" width="12" height="15" class="f-warn"/> <rect x="451" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="464" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="477" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="490" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="503" y="82" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="515" y="80" width="12" height="15" class="f-warn"/> <rect x="580" y="80" width="12" height="15" class="f-warn"/> <text x="10" y="111" fill="currentColor" font-size="12">ctrl</text> <rect x="60" y="109" width="12" height="5" class="f-ok"/>
  <rect x="73" y="109" width="12" height="5" class="f-ok"/> <rect x="86" y="99" width="12" height="15" class="f-acc"/> <rect x="255" y="109" width="12" height="5" class="f-ok"/> <rect x="268" y="99" width="12" height="15" class="f-acc"/> <rect x="320" y="109" width="12" height="5" class="f-ok"/> <rect x="333" y="109" width="12" height="5" class="f-ok"/> <rect x="346" y="99" width="12" height="15" class="f-acc"/> <rect x="515" y="109" width="12" height="5" class="f-ok"/> <rect x="528" y="99" width="12" height="15" class="f-acc"/> <rect x="580" y="109" width="12" height="5" class="f-ok"/> <rect x="593" y="109" width="12" height="5" class="f-ok"/> <rect x="606" y="99" width="12" height="15" class="f-acc"/> <text x="10" y="130" fill="currentColor" font-size="12">F</text> <rect x="60" y="128" width="12" height="5" class="f-ok"/>
  <rect x="73" y="128" width="12" height="5" class="f-ok"/> <rect x="86" y="128" width="12" height="5" class="f-ok"/> <text x="10" y="149" fill="currentColor" font-weight="bold">deferred: DMA ISR 1 step + 필터 태스크 F(prio 3) — UART·SysTick 거의 제때</text> <text x="61" y="167" fill="currentColor" font-size="12">0</text> <text x="126" y="167" fill="currentColor" font-size="12">5</text> <text x="191" y="167" fill="currentColor" font-size="12">10</text> <text x="256" y="167" fill="currentColor" font-size="12">15</text> <text x="321" y="167" fill="currentColor" font-size="12">20</text> <text x="386" y="167" fill="currentColor" font-size="12">25</text> <text x="451" y="167" fill="currentColor" font-size="12">30</text> <text x="516" y="167" fill="currentColor" font-size="12">35</text> <text x="581" y="167" fill="currentColor" font-size="12">40</text> <text x="646" y="167" fill="currentColor" font-size="12">45</text> <text x="10" y="183" fill="currentColor" font-size="12">DMA</text>
  <rect x="125" y="171" width="12" height="15" class="f-warn"/> <rect x="385" y="171" width="12" height="15" class="f-warn"/> <rect x="645" y="171" width="12" height="15" class="f-warn"/> <text x="10" y="202" fill="currentColor" font-size="12">UART</text> <rect x="73" y="190" width="12" height="15" class="f-warn"/> <rect x="126" y="192" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="138" y="190" width="12" height="15" class="f-warn"/> <rect x="177" y="190" width="12" height="15" class="f-warn"/> <rect x="229" y="190" width="12" height="15" class="f-warn"/> <rect x="281" y="190" width="12" height="15" class="f-warn"/> <rect x="333" y="190" width="12" height="15" class="f-warn"/> <rect x="386" y="192" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="398" y="190" width="12" height="15" class="f-warn"/> <rect x="437" y="190" width="12" height="15" class="f-warn"/>
  <rect x="489" y="190" width="12" height="15" class="f-warn"/> <rect x="541" y="190" width="12" height="15" class="f-warn"/> <rect x="593" y="190" width="12" height="15" class="f-warn"/> <rect x="646" y="192" width="10" height="11" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <rect x="658" y="190" width="12" height="15" class="f-warn"/> <rect x="697" y="190" width="12" height="15" class="f-warn"/> <text x="10" y="221" fill="currentColor" font-size="12">SysTk</text> <rect x="60" y="209" width="12" height="15" class="f-warn"/> <rect x="190" y="209" width="12" height="15" class="f-warn"/> <rect x="320" y="209" width="12" height="15" class="f-warn"/> <rect x="450" y="209" width="12" height="15" class="f-warn"/> <rect x="580" y="209" width="12" height="15" class="f-warn"/> <text x="10" y="240" fill="currentColor" font-size="12">ctrl</text> <rect x="60" y="238" width="12" height="5" class="f-ok"/>
  <rect x="73" y="238" width="12" height="5" class="f-ok"/> <rect x="86" y="228" width="12" height="15" class="f-acc"/> <rect x="190" y="238" width="12" height="5" class="f-ok"/> <rect x="203" y="228" width="12" height="15" class="f-acc"/> <rect x="320" y="238" width="12" height="5" class="f-ok"/> <rect x="333" y="238" width="12" height="5" class="f-ok"/> <rect x="346" y="228" width="12" height="15" class="f-acc"/> <rect x="450" y="238" width="12" height="5" class="f-ok"/> <rect x="463" y="228" width="12" height="15" class="f-acc"/> <rect x="580" y="238" width="12" height="5" class="f-ok"/> <rect x="593" y="238" width="12" height="5" class="f-ok"/> <rect x="606" y="228" width="12" height="15" class="f-acc"/> <text x="10" y="259" fill="currentColor" font-size="12">F</text> <rect x="60" y="257" width="12" height="5" class="f-ok"/>
  <rect x="73" y="257" width="12" height="5" class="f-ok"/> <rect x="86" y="257" width="12" height="5" class="f-ok"/> <rect x="125" y="257" width="12" height="5" class="f-ok"/> <rect x="138" y="257" width="12" height="5" class="f-ok"/> <rect x="151" y="247" width="12" height="15" class="f-acc"/> <rect x="164" y="247" width="12" height="15" class="f-acc"/> <rect x="177" y="257" width="12" height="5" class="f-ok"/> <rect x="190" y="257" width="12" height="5" class="f-ok"/> <rect x="203" y="257" width="12" height="5" class="f-ok"/> <rect x="216" y="247" width="12" height="15" class="f-acc"/> <rect x="229" y="257" width="12" height="5" class="f-ok"/> <rect x="242" y="247" width="12" height="15" class="f-acc"/> <rect x="255" y="247" width="12" height="15" class="f-acc"/> <rect x="268" y="247" width="12" height="15" class="f-acc"/>
  <rect x="281" y="257" width="12" height="5" class="f-ok"/> <rect x="294" y="247" width="12" height="15" class="f-acc"/> <rect x="307" y="247" width="12" height="15" class="f-acc"/> <rect x="320" y="257" width="12" height="5" class="f-ok"/> <rect x="333" y="257" width="12" height="5" class="f-ok"/> <rect x="346" y="257" width="12" height="5" class="f-ok"/> <rect x="385" y="257" width="12" height="5" class="f-ok"/> <rect x="398" y="257" width="12" height="5" class="f-ok"/> <rect x="411" y="247" width="12" height="15" class="f-acc"/> <rect x="424" y="247" width="12" height="15" class="f-acc"/> <rect x="437" y="257" width="12" height="5" class="f-ok"/> <rect x="450" y="257" width="12" height="5" class="f-ok"/> <rect x="463" y="257" width="12" height="5" class="f-ok"/> <rect x="476" y="247" width="12" height="15" class="f-acc"/>
  <rect x="489" y="257" width="12" height="5" class="f-ok"/> <rect x="502" y="247" width="12" height="15" class="f-acc"/> <rect x="515" y="247" width="12" height="15" class="f-acc"/> <rect x="528" y="247" width="12" height="15" class="f-acc"/> <rect x="541" y="257" width="12" height="5" class="f-ok"/> <rect x="554" y="247" width="12" height="15" class="f-acc"/> <rect x="567" y="247" width="12" height="15" class="f-acc"/> <rect x="580" y="257" width="12" height="5" class="f-ok"/> <rect x="593" y="257" width="12" height="5" class="f-ok"/> <rect x="606" y="257" width="12" height="5" class="f-ok"/> <rect x="645" y="257" width="12" height="5" class="f-ok"/> <rect x="658" y="257" width="12" height="5" class="f-ok"/> <rect x="671" y="247" width="12" height="15" class="f-acc"/> <rect x="684" y="247" width="12" height="15" class="f-acc"/>
  <rect x="697" y="257" width="12" height="5" class="f-ok"/> <rect x="10" y="278" width="14" height="13" class="f-acc"/> <text x="28" y="289" fill="currentColor" font-size="12">R 태스크 실행</text> <rect x="160" y="286" width="14" height="5" class="f-ok"/> <text x="178" y="289" fill="currentColor" font-size="12">r READY(대기)</text> <rect x="310" y="278" width="14" height="13" class="f-warn"/> <text x="328" y="289" fill="currentColor" font-size="12">I ISR 실행</text> <rect x="460" y="278" width="14" height="13" class="f-mut"/> <text x="478" y="289" fill="currentColor" font-size="12">i 중첩돼 멈춘 ISR</text> <rect x="610" y="278" width="14" height="13" fill="none" stroke="currentColor" stroke-dasharray="2 2"/> <text x="628" y="289" fill="currentColor" font-size="12">p pending</text>
</svg>
```

- **ISR 판**: DMA ISR 이 9 step 이다. UART 바이트는 4 step 마다 오는데 DR 은 1 칸뿐이라, ISR 동안 바이트가 두 번 오면 두 번째가 첫 번째를 덮는다(overrun). 110 step 에 11 번. 실제 STM32 USART 라면 SR 의 ORE 비트가 서고 바이트가 사라지는 상황이다. SysTick 도 5 step 밀려 제어 루프가 늦게 시작한다.
- **지연 판**: DMA ISR 1 step. 필터 F 는 prio 3 태스크라 UART ISR(어떤 태스크보다 높다)이 언제든 끼어든다 → overrun 0, UART 지연 최대 1. 대가: 블록 하나의 응답이 9 → 15 step 으로 늘었다 (UART·tick·ctrl 이 사이사이 끼어서). 하지만 마감 20 안이다 — **마감 안이면 늦어도 괜찮다** 가 실시간 설계의 기준이다.
- 링 + 카운팅 세마포어(max 2): 세마포어는 개수만, 어느 절반·언제 찼는지는 디스크립터로. 처리가 밀리면 `dropped` 로 드러난다 (RTOS-B08 의 "바이너리는 합쳐진다" 를 피하는 방법). 함수 호출 자체를 데몬에 미루는 `xTimerPendFunctionCallFromISR` 도 있다 (연습 2).

### 진짜 FreeRTOS 로 확인 — 순서와 tick

<!-- code: demo_freertos_from_isr.c -->

POSIX 포트에는 진짜 장치 인터럽트가 없다. 그래서 두 가지 "ISR" 을 쓴다: (1) 틱 훅 `vApplicationTickHook()` — 틱 ISR(POSIX 포트에선 SIGALRM 핸들러 `vPortSystemTickHandler`, Posix/port.c:488) 안에서 불린다 (tasks.c:5006). (2) 태스크 안에서 `taskENTER_CRITICAL()` 로 틱 신호를 막고 FromISR API 를 부르는 흉내 ISR. POSIX 포트의 `portYIELD_FROM_ISR(x)` 는 x 가 참이면 `vPortYield()` — 즉시 스위치다 (Posix/portmacro.h:86–99). 실제 출력 (busy prio 1 이 항상 바쁘고, handler prio 3 이 세마포어를 기다린다):

```text
who       dt   what                                     value
-- E1a fake ISR + yield
busy      +0   E1a fake ISR + yield                     0
isr       +0   GiveFromISR rc                           1
isr       +0   woken -> portYIELD_FROM_ISR(woken)       1
handler   +0   woke (first instruction)                 0
busy      +0   back from ISR, continues                 0
-- E1b fake ISR, no yield
busy      +0   E1b fake ISR, no yield                   0
isr       +0   GiveFromISR rc                           1
isr       +0   woken (yield omitted)                    1
busy      +0   back from ISR, continues                 0
handler   +1   woke (first instruction)                 0
-- E2 tick hook give
busy      -2   E2 tick hook give                        0
tickhook  +0   GiveFromISR rc                           1
tickhook  +0   woken (no portYIELD_FROM_ISR)            1
handler   +0   woke (first instruction)                 0
-- E3 tick hook PendFunctionCall
busy      -2   E3 tick hook PendFunctionCall            0
tickhook  +0   PendFunctionCallFromISR pdPASS count     1
tickhook  +0   woken                                    1
callback  +0   Tmr Svc                                  1
-- E4 11 x PendFunctionCall (queue 10)
busy      -2   E4 11 x PendFunctionCall (queue 10)      0
tickhook  +0   PendFunctionCallFromISR pdPASS count     10
tickhook  +0   woken                                    1
callback  +0   Tmr Svc                                  1
callback  +0   Tmr Svc                                  2
   ... (3 ~ 9 생략, 모두 +0)
callback  +0   Tmr Svc                                  10
```

(dt = 기록 tick − 실험 기준 tick.) 줄 **순서** 는 20 회 실행에서 20 회 모두 같았다. tick 값은 호스트 타이밍이라 가끔 흔들린다: 30 회 중 E1b handler +1 이 30/30, E3 callback 이 1 번 +1 (나머지 +0). 읽는 법:

1. **E1a vs E1b** — yield 를 하면 handler 가 "ISR 복귀" 보다 **먼저** 같은 tick 에 돈다 (모델 (a)). 생략하면 busy 가 먼저 계속하고 handler 는 **다음 tick** (+1) 에 — 그 tick ISR 이 `xYieldPendings` 를 보고 바꿨다 (모델 (b)).
2. **E2** — 틱 훅에서는 yield 를 안 불러도 같은 tick 이다. 틱 훅이 tick ISR **안** 에서 불리고 (tasks.c:5006), 바로 뒤 5020 에서 `xYieldPendings` 를 보기 때문이다. 그래서 틱 훅에서는 `portYIELD_FROM_ISR` 를 부르지 않는다 (POSIX 포트에서 부르면 SIGALRM 핸들러 한가운데서 `vPortYield()` 가 실행된다).
3. **E3/E4** — `xTimerPendFunctionCallFromISR` 는 "함수 호출 메시지" 를 `xQueueSendFromISR( xTimerQueue, ...)` 로 데몬 큐에 넣는다 (timers.c:1250–1255). 콜백은 `pcTaskGetName(NULL)` = **"Tmr Svc"** 태스크에서, 데몬 우선순위가 7(최고)이라 woken = 1, 같은 tick. 큐가 10 칸이라 11 번째는 `pdFAIL` — FromISR 는 기다리지 않고 실패한다.

(드물게 프로그램 종료 시 stderr 에 `vPortSystemTickHandler called from non-FreeRTOS thread` 가 한 줄 찍힌다 — 20 회 중 1 회 관찰. `exit()` 중에 SIGALRM 이 FreeRTOS 소유가 아닌 스레드에 떨어질 때 포트가 찍는 메시지(Posix/port.c:515)다. 결과와는 무관.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 43/43 passed (5회 반복 동일, 각 0.00 s)
./solution -v                                                                 # 위 풀이의 타임라인 전부
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 43/43, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 에서 `xSemaphoreGive()` / `xQueueSend()` (FromISR 아님) | 태스크용 API 는 높은 태스크를 깨우면 그 자리에서 스위치하려 하고, 임계 구역도 ISR 용이 아니다. CM4F 포트는 `vPortEnterCritical` 에서 assert (port.c:487) | `xSemaphoreGiveFromISR(s, &w); portYIELD_FROM_ISR(w);` |
| ISR 에서 `xSemaphoreTake(s, portMAX_DELAY)` 로 "데이터가 올 때까지" | 막히는 건 ISR 이 아니라 **끼어든 태스크**. 커널 상태가 모순된다 (연습 1) | ISR 은 신호만, 기다림은 태스크가 |
| `BaseType_t w;` 초기화 안 함 | FromISR 는 true 만 쓴다 (queue.c:1439). 쓰레기 값이면 불필요한 스위치 | `BaseType_t w = pdFALSE;` |
| give 마다 바로 `portYIELD_FROM_ISR` | 같은 ISR 에서 여러 번 give 하면 요청이 여러 번 — 해롭진 않지만 의도가 흐려진다 | woken 하나에 OR 로 모으고 ISR **마지막 줄** 에서 한 번 |
| `portYIELD_FROM_ISR` 생략 | 최악 1 tick 지연 (Part B: 3 → 10 step). 동작은 하니 테스트에서 안 잡힌다 | ISR 끝에 넣는다 |
| 자체 포트에서 yield 요청 시 바로 문맥 전환 | 중첩 ISR 중이면 바깥 ISR 을 잃는다 (연습 5: assert 3 번) | 깊이 0 에서만 (CA9 portASM.S:226), Cortex-M 은 PendSV 최저 우선순위 |
| 필터·파싱·`printf` 를 ISR 안에서 | 그보다 낮은 IRQ·tick·모든 태스크의 지터 (Part C: UART overrun 11) | 링 + 세마포어/알림으로 태스크에 넘긴다 |
| 처리 태스크를 아무 우선순위로 | 미룬 일이 낮은 우선순위면 다른 태스크 뒤로 밀려 마감을 놓친다 (연습 2: 15 step) | 마감 기준으로 우선순위를 정한다 (RTOS-M11) |
| FreeRTOS API 를 부르는 ISR 을 NVIC 기본 우선순위 0 에 | Cortex-M 에서 0 은 최고 → `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 높다 → 커널 임계 구역에 안 가려져 자료구조가 깨질 수 있다 | `NVIC_SetPriority(IRQn, 숫자 >= configMAX_SYSCALL...)`, 그 위 IRQ 는 API 금지 (연습 4) |

## 8. 면접 Q&A

<details>
<summary>Q1. ISR 은 왜 짧아야 하나요? "짧다" 는 얼마나인가요?</summary>

ISR 이 도는 동안 그보다 낮거나 같은 IRQ 는 모두 pending 으로 기다리고, **모든 태스크** 가 멈춘다 (가장 낮은 IRQ 도 가장 높은 태스크보다 높다). 그래서 ISR 길이는 그 아래 모든 것의 지연(지터)에 그대로 더해진다 — Part B 에서 6 step 처리를 ISR 에 넣자 UART 지연 1 → 7, tick 2 → 8, 제어 루프 시작 3 → 9. "짧다" 의 기준은 절대 시간이 아니라 **그 아래 가장 빡빡한 마감** 이다: 예를 들어 FIFO 없는 UART 로 115200 baud 를 DMA 없이 받으면 바이트 간격이 약 87 µs (10 비트 / 115200) 라, 그보다 긴 상위 ISR 은 overrun 을 만들 수 있다 (Part C). 실무 답: 하드웨어 응답(플래그 클리어, 레지스터 읽기, 버퍼 넘기기)만 하고 나머지는 태스크로. 꼬리: "그럼 모든 처리를 태스크로?" — 아니다. 수 µs 안에 반응해야 하는 일(모터 PWM 차단 등)은 ISR 에서, 그리고 그런 IRQ 는 커널보다 높은 우선순위에 두되 API 를 부르지 않는다 (연습 4).

</details>

<details>
<summary>Q2. portYIELD_FROM_ISR 는 정확히 무엇을 하나요? 왜 ISR 안에서 바로 컨텍스트를 바꾸지 않나요?</summary>

Cortex-M FreeRTOS 포트에서 `portYIELD_FROM_ISR(x)` → `portEND_SWITCHING_ISR(x)` → x 가 참이면 `portYIELD()` = `SCB->ICSR` 에 PENDSVSET 비트 쓰기 + dsb/isb (ARM_CM4F/portmacro.h:88–114). 전환 자체는 PendSV 예외 핸들러가 한다. 포트가 PendSV 를 **최저 우선순위** 로 두므로 (port.c:433), 지금 ISR 과 그 아래 중첩된 ISR, 기다리던 IRQ 가 모두 끝나야 PendSV 가 돈다. ISR 안에서 바로 바꾸지 않는 이유: ISR 은 태스크 문맥이 아니고, 중첩 중이면 돌아가야 할 바깥 ISR 이 있다. 요청을 모아 마지막에 한 번 바꾸면 비용도 한 번이다 (Part B 중첩: 요청 2 개 → 스위치 1 번). 꼬리: "SysTick 도 최저 우선순위인데?" — 포트가 둘 다 최저로 둔다 (port.c:433–434). SysTick 핸들러도 직접 바꾸지 않고 PENDSVSET 만 쓴다 (port.c:560~ `xPortSysTickHandler`) — 전환 경로는 PendSV 하나다.

</details>

<details>
<summary>Q3. xHigherPriorityTaskWoken 을 무시하면 어떻게 되나요? 최악 지연은?</summary>

`xTaskRemoveFromEventList` 가 더 높은 태스크를 깨우면 `xYieldPendings[0] = pdTRUE` 를 남긴다 (tasks.c:5564, 주석: "in case the user is not using the xHigherPriorityTaskWoken parameter"). 그래서 다음 tick 의 `xTaskIncrementTick` 이 그 플래그를 보고 스위치를 요청한다 (5020–5022). 즉 영원히가 아니라 **최대 약 1 tick** 늦는다 — Part B 에서 3 ~ 10 step (1 tick = 10 step), 연습 3 의 닫힌 식으로 80 위상 전부 일치. 1 ms tick 이면 최대 ~1 ms 의 지터다. 그 사이 다른 커널 호출이 스위치를 일으키면(실행 중 태스크가 막히는 등) 더 일찍 바뀐다. 꼬리: tickless idle 에서는? — CPU 가 idle 로 자고 있었다면 ISR 이 끝나고 idle 태스크가 다시 돌면서 스위치가 일어날 수 있지만, 이 경로는 이 레슨에서 확인하지 않았다 (RTOS-A02).

</details>

<details>
<summary>Q4. ISR 에서 xSemaphoreTake(s, portMAX_DELAY) 를 부르면 무슨 일이 생기나요? FreeRTOS 는 어떻게 잡나요?</summary>

ISR 은 막힐 "자기" 가 없다. 커널 입장에서 현재 태스크는 ISR 이 끼어든 태스크이므로, 그 태스크가 엉뚱하게 이벤트 리스트에 걸리고(연습 1: bg 가 BLOCKED 인데 여전히 `K.cur`) 이후 스케줄러 상태가 모순된다. FreeRTOS 공통 코드는 ISR 여부를 일일이 보지 않고 **포트** 가 잡는다: Cortex-M3/M4F 포트의 `vPortEnterCritical` 이 `configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 )` (ARM_CM4F/port.c:487) — 막힐 수 있는 태스크용 API 는 대부분 `taskENTER_CRITICAL()` 을 지나므로 (예: `xQueueSemaphoreTake` 는 queue.c:1688) 첫 임계 구역에서 걸린다. PIC32·ESP32 등은 `portASSERT_IF_IN_ISR()` 를 정의한다. `configASSERT` 를 끄면 아무것도 안 잡는다 — 개발 빌드에서는 반드시 켜야 하는 이유. POSIX 포트는 둘 다 없다. 막히지 않는 판이 필요하면 `xSemaphoreTakeFromISR` (semphr.h:675).

</details>

<details>
<summary>Q5. configMAX_SYSCALL_INTERRUPT_PRIORITY 는 무엇이고, ISR 우선순위를 어떻게 정하나요?</summary>

Cortex-M3/M4 포트의 커널 임계 구역은 PRIMASK 가 아니라 **BASEPRI** 를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 로 올린다 (ARM_CM4F/portmacro.h:220–240). 그래서 그 값보다 (논리적으로) 높은 IRQ 는 커널 임계 구역 중에도 들어온다 — 지연 0 이 필요한 모터 보호 같은 일에 쓴다. 대가: 그런 IRQ 는 커널 자료구조를 건드리면 안 되므로 FromISR API 도 금지. `configASSERT` 가 켜져 있으면 FromISR 함수 앞의 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` (queue.c:1376) → `vPortValidateInterruptPriority()` (port.c:851) 가 `configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )` (port.c:888) 로 잡는다. NVIC 숫자는 작을수록 급하므로 "API 를 부르는 IRQ 는 숫자가 configMAX_SYSCALL... 이상" 이 규칙이고, 기본값 0 은 최고 우선순위라 반드시 걸린다 (주석: "must not be left at their default priority of zero"). 연습 4 가 이 구조를 모델로 돌린다: 임계 구역 중 UART 는 5 step 기다리고 FAULT 는 0 step, FAULT 가 직접 give 하면 assert, 대신 낮은 소프트웨어 IRQ 를 pend 해서 그쪽이 give.

</details>

<details>
<summary>Q6. ISR 에서 태스크로 일을 넘기는 방법을 비교해 보세요.</summary>

| 방법 | 넘기는 것 | 특징 |
|---|---|---|
| 바이너리 세마포어 | "1 번 이상 일어났다" | 합쳐짐 주의 — 데이터는 링에 (RTOS-B08) |
| 카운팅 세마포어 + 링 | 개수 + 디스크립터 | Part C. 넘치면 `dropped` 로 드러남 |
| 태스크 알림 `vTaskNotifyGiveFromISR` | 카운트/비트 | 가장 가볍다, 받는 태스크 1 개 (RTOS-M01) |
| 큐 `xQueueSendFromISR` | 값 복사 | 작은 메시지에 적합, 복사 비용 (RTOS-B11) |
| `xTimerPendFunctionCallFromISR` | 함수 + 인자 2 개 | 태스크를 따로 안 만든다. 우선순위 = 데몬 우선순위, 큐 길이 = `configTIMER_QUEUE_LENGTH` (연습 2, 데모 E4) |
| 스트림/메시지 버퍼 | 바이트열 | 단일 생산자-단일 소비자 UART 류 |

다른 OS (문서 기준, 미검증): Zephyr 는 `k_work_submit()` 으로 시스템 워크큐 스레드에 미룬다. Linux 는 top half / bottom half — `request_threaded_irq()` 의 스레드 핸들러, softirq/tasklet, workqueue. 공통 원리: 하드 IRQ 문맥에서는 막히지 않는 최소한만, 나머지는 스케줄 가능한 문맥에서.

</details>

<details>
<summary>Q7. 중첩된 ISR 세 개가 각각 더 높은 태스크를 깨우고 yield 를 요청했습니다. 컨텍스트 스위치는 몇 번 일어나나요?</summary>

1 번. 요청은 PendSV pending 비트 하나(또는 `ulPortYieldRequired` 변수 하나)에 모일 뿐이고, 실제 전환은 가장 바깥 ISR 이 끝난 뒤 한 번 일어난다. 그 순간 스케줄러가 READY 중 가장 높은 태스크를 고른다 — 연습 5 에서 bg → T4 (prio 4), 그다음 T4 가 막히면 T3, T2 순 (17/18/19). "요청마다 바로 바꾸는" 잘못된 포트는 ISR 안 전환을 3 번 시도한다 (모델에서 assert 3 번). 꼬리: "그럼 woken 을 OR 로 모으는 게 꼭 필요한가?" — 한 ISR 안에서 여러 FromISR 를 부를 때 변수 하나를 같이 넘기면 된다 (FromISR 는 false 를 쓰지 않으니 자연히 OR).

</details>

<details>
<summary>Q8. 지연 처리 태스크의 우선순위는 어떻게 정하나요? 인터럽트 우선순위와는 어떤 관계인가요?</summary>

별개의 두 공간이다: 모든 IRQ 는 모든 태스크보다 높다. ISR 에서 일을 빼면 그 일의 우선순위를 **태스크 공간에서 다시 고를 수 있게** 된다 — 이것이 지연 처리의 진짜 이득이다. 기준은 마감: Part C 의 필터는 다음 DMA 인터럽트(20 step) 전에 끝나야 하므로 그 마감을 지킬 만큼 높게(prio 3), 하지만 1 tick 마감의 제어 루프(prio 4)보다는 낮게. 마감이 짧을수록 높게 주는 것이 Rate/Deadline Monotonic 의 직관이다 (RTOS-M11). 흔한 함정: `xTimerPendFunctionCallFromISR` 로 미뤘는데 `configTIMER_TASK_PRIORITY` 가 낮아서 바쁜 태스크 뒤로 밀림 — 연습 2 에서 지연 2 → 15 step.

</details>

## 9. 추가 연습

### 연습 1 — 가드가 없으면 누가 막히나

가드를 끈 커널(`g_guard_on = false`)에서, bg 가 돌던 중 ISR 이 `sem_take(&s, FOREVER)` 를 부른다. bg, `K.cur`, assert 수는 어떻게 되나? 가드를 켜면?

<details>
<summary>답 보기</summary>

```text
[연습 1] ISR 에서 sem_take(FOREVER): 가드 없음 vs 있음
  guard off: rc=3 (K_BLOCKED=3), bg.state=BLOCKED, wait_obj=&s, K.cur=bg, asserts=1 ("context switch while 1 ISR(s) active")
```

ISR 과 아무 관계없는 bg 가 s 의 대기자가 되어 BLOCKED. `task_block_cur` 가 바로 스위치하려 했지만 ISR 안이라 `port_switch` 의 assert 가 막았다 → ISR 은 **BLOCKED 상태인 태스크로 복귀** 한다. 실제 커널이라면 "실행 중인데 대기 리스트에도 있는" 태스크가 생긴 것이다. 가드를 켜면 `sem_take`, `mutex_take`, `sem_give` 세 호출 모두 `K_ERR_ISR` + assert 3 개, bg 는 READY 그대로.

</details>

### 연습 2 — deferred function call (xTimerPendFunctionCallFromISR 모델)

`pend_call_from_isr(fn, p1, p2, &woken)` 와 데몬 태스크를 만들어라 (큐 4 칸, 콜백 1 개 = 1 step). IRQ 는 21 에. 데몬 prio 5 일 때, 데몬 prio 2 이고 prio 3 로거가 30 step 동안 바쁠 때 콜백 시각은? 한 ISR 에서 5 번 pend 하면?

<details>
<summary>답 보기</summary>

```text
  daemon prio 5 (> logger 3): IRQ@21 → callback at step 23 (latency 2)
  daemon prio 2 (< logger 3): IRQ@21 → callback at step 36 (latency 15)
  5 calls pended in one ISR, queue length 4: ok=4, callbacks run=4, args=1 2 3 4
```

최고 우선순위 데몬은 yield 한 핸들러 태스크와 똑같이 2 step. 데몬이 로거보다 낮으면 로거가 일을 다 끝낸 35 다음(36)에야 돈다. 그래서 FreeRTOS 기본 설정 예들이 `configTIMER_TASK_PRIORITY` 를 높게 둔다 (이 레슨 FreeRTOSConfig.h: `configMAX_PRIORITIES - 1`). 큐가 차면 FromISR 는 기다리지 않고 실패 → 반환값을 확인해 "놓친 이벤트" 를 세야 한다 (데모 E4: 11 번 중 10 번 pdPASS).

</details>

### 연습 3 — 지연을 닫힌 식으로

ISR 길이 L, tick 주기 T(=10), tick ISR 1 step 일 때 IRQ 위상 s 에 대한 (a) yield, (b) no-yield 지연 식을 세우고 s = 11..90, L = 2, 5 에서 시뮬레이터와 비교하라.

<details>
<summary>답 보기</summary>

[s, s+L] 안에 T 의 배수(tick IRQ)가 있으면 그 tick ISR 이 ISR 직후 이어 돈다. (a) = L + (있으면 1). (b) = 있으면 L + 1, 없으면 (s+L 보다 큰 첫 T 배수) + 1 − s.

```c
static bool tick_in(int a, int b) { return (b / TICK) * TICK >= a; }
static int predict_yield(int L, int s) { return L + (tick_in(s, s + L) ? 1 : 0); }
static int predict_noyield(int L, int s) {
    int e = s + L;
    if (tick_in(s, e)) return L + 1;
    int T = (e / TICK + 1) * TICK;
    return T + 1 - s;
}
```

```text
  L=2: mismatches=0  yield min/max 2/3   no-yield min/max 3/10 avg 5.80
  L=5: mismatches=0  yield min/max 5/6   no-yield min/max 6/10 avg 7.00
```

no-yield 의 최악은 L 과 무관하게 **T** (s ≡ 1 mod T 일 때). yield 를 빼먹는 비용은 "ISR 길이" 가 아니라 "tick 주기" 로 정해진다.

</details>

### 연습 4 — configMAX_SYSCALL_INTERRUPT_PRIORITY

syscall max = 4 로 두자. bg 가 11..15 동안 임계 구역(hwprio ≤ 4 가림). UART(1) 가 12 에, 모터 FAULT(6) 가 13 에 온다. FAULT 는 PWM 을 끄고 H 를 깨워야 한다. 직접 `sem_give_from_isr` 를 부르면? 낮은 SWI(2) 를 pend 해서 그쪽이 give 하면?

<details>
<summary>답 보기</summary>

```text
  -- FAULT(6) → SWI(2) → H, bg critical 11..15
  step :  1         2
         9012345678901
  FAULT: ....I........
  SWI  : ....pppI.....
  UART : ...pppppI....
  SysTk: .I.........I.
  H    : BBBBBBBrrRBBB
  bg   : RrRRrRRrrrRrR
  right: FAULT lat 0 (pwm off @13), UART lat 5, SWI lat 3, H first @18, asserts 0
  wrong: FAULT calls sem_give_from_isr → asserts 1 ("FromISR API from IRQ prio 6 > syscall max 4"), H first @-1
```

FAULT 는 임계 구역 중에도 0 step 에 PWM 을 끈다 — 이게 syscall max 위에 두는 이유. 하지만 커널 API 는 금지(모델에서 assert, 실제 Cortex-M 은 `vPortValidateInterruptPriority` 가 잡는다). 커널 일은 `NVIC_SetPendingIRQ()` 로 낮은 IRQ(SWI)에 넘기면, 그 IRQ 는 임계 구역이 끝난 16 에 돌아 H 를 깨운다. 실제 칩에서는 쓰지 않는 주변장치 IRQ 번호 하나를 소프트웨어 인터럽트로 쓰는 경우가 많다.

</details>

### 연습 5 — ISR 폭풍과 PendSV 1 번

A(hwprio 2, 3 step)@11, B(3, 2 step)@12, C(4, 1 step)@13 이 각각 T2/T3/T4(prio 2/3/4)를 깨우고 yield 를 요청한다. 11..16 동안 스위치는 몇 번이고 누가 먼저 도나? "요청마다 바로 바꾸는" 포트는?

<details>
<summary>답 보기</summary>

```text
  -- A(2)@11 len3, B(3)@12 len2, C(4)@13 len1
  step : 1         2
         01234567890
  C    : ...I.......
  B    : ..IiI......
  A    : .IiiiII....
  T4   : BBBrrrrRBBB
  T3   : BBrrrrrrRBB
  T2   : BrrrrrrrrRr
  correct port: switches during 11..16 = 1, T4/T3/T2 first run @17/18/19, asserts 0
  naive port (switch on every yield request): asserts 3, switches during 11..16 = 1
```

요청 3 개 → A 가 끝나는 16 에 1 번, 고르는 건 가장 높은 T4. naive 판은 C·B·A 의 끝에서 각각 ISR 안 전환을 시도해 assert 3 번 (모두 거절, 실제라면 문맥 파괴). 결론: "yield 요청" 은 비트(또는 플래그) 하나이고, 전환은 깊이 0 에서 한 번.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 24/24 passed
./practice -v                                                                # 위 타임라인들
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 24/24, 보고 없음
```

## 10. 핵심 정리

- ISR 은 **깨우기만**, 전환은 `portYIELD_FROM_ISR` 의 **요청** 으로. Cortex-M 은 PENDSVSET 한 비트, PendSV 가 최저 우선순위라 가장 바깥 ISR(과 기다리던 IRQ)이 끝난 뒤 한 번만 전환된다.
- `xHigherPriorityTaskWoken` = 깨운 태스크가 끼어든 태스크보다 높다. 호출자가 pdFALSE 로 초기화, FromISR 는 true 만 쓴다. 무시해도 `xYieldPendings` 덕에 다음 tick 에는 바뀐다 → 최악 약 1 tick 지연.
- ISR 에서 막히는 API 금지: 막히는 건 끼어든 태스크다. FreeRTOS CM4F 는 `vPortEnterCritical` 의 VECTACTIVE assert 로, 일부 포트는 `portASSERT_IF_IN_ISR` 로 잡는다. `configASSERT` 를 켜 둘 것.
- 측정: yield 함 2~3 step, 생략 3~10 step, 전부 ISR 은 자기 일만 빠르고 UART 지연 1 → 7, tick 2 → 8, 제어 루프 3 → 9 — ISR 길이는 그 아래 모두의 지터.
- DMA HT/TC → 링 + 카운팅 세마포어 → 필터 태스크: overrun 11 → 0, 블록 응답은 9 → 15 로 늘지만 마감 20 안.
- `configMAX_SYSCALL_INTERRUPT_PRIORITY` 위의 IRQ 는 커널에 안 가려지는 대신 API 금지 — 커널 일은 낮은 IRQ 나 태스크로 넘긴다. NVIC 숫자는 작을수록 급하다.
- 미루는 방법: 세마포어/알림/큐/`xTimerPendFunctionCallFromISR`. 미룬 일의 우선순위는 마감으로 정한다.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — queue.c (`xQueueGiveFromISR` 1338~: 1376 우선순위 검사, 1381 임계 구역, 1433–1439 woken, 1498 errQUEUE_FULL; `xQueueSemaphoreTake` 1659, 1688), tasks.c (`xTaskRemoveFromEventList` 5555–5564, `xTaskIncrementTick` 4831~ 의 틱 훅 5006 · `xYieldPendings` 5020–5022, `vTaskSwitchContext` 5215–5227), timers.c 1238–1258, include/semphr.h 640 · 675, include/FreeRTOS.h 2783–2801 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- 같은 소스의 포트 — portable/GCC/ARM_CM4F (portmacro.h 88–114 · 179 · 191–209 · 220–240, port.c 433–434 · 475–489 · 560~ · 851–888), portable/GCC/ARM_CA9 (portmacro.h 80–90, portASM.S 170–260), portable/ThirdParty/GCC/Posix (portmacro.h 84–99, port.c 424–429 · 488–516), MPLAB/PIC32MX/portmacro.h 183
- FreeRTOS 문서 — "Running the RTOS on a ARM Cortex-M Core" (configMAX_SYSCALL_INTERRUPT_PRIORITY, NVIC 우선순위 숫자 방향) — <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- FreeRTOS API 설명 — `xTimerPendFunctionCallFromISR` (include/timers.h 1120–1210 의 주석과 예제: "defer processing of a function to the RTOS daemon task")
- R. Barry, *Mastering the FreeRTOS Real Time Kernel* — Interrupt Management 장 (deferred interrupt processing, FromISR API)
- ARM, *Cortex-M4 Devices Generic User Guide* — PendSV, 예외 우선순위, tail-chaining (문서 기준)
- J. Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — PendSV 를 이용한 OS 컨텍스트 스위치
- Zephyr 문서 — Interrupts (`k_is_in_isr`, zero-latency IRQ), Workqueue Threads (`k_work_submit`) — <https://docs.zephyrproject.org/latest/kernel/services/interrupts.html> (문서 기준, 미검증)
- CMSIS-RTOS2 API — "Calls from Interrupt Service Routines" 표 — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html> (문서 기준, 미검증)
- Linux kernel 문서 — `request_threaded_irq`, workqueue — <https://docs.kernel.org/core-api/genericirq.html> (문서 기준, 미검증)
- 관련 레슨: RTOS-B08 (바이너리 세마포어 ISR→태스크, woken 의 뜻), RTOS-B02 (KERNEL_ASSERT 훅), RTOS-B11 (큐), RTOS-B12 (타이머 데몬), RTOS-M01 (태스크 알림), RTOS-M04 (뮤텍스 주인과 상속), RTOS-M11 (마감 기반 우선순위), RTOS-A02 (tickless idle), C-M14 (ISR 공유 데이터, PRIMASK)
