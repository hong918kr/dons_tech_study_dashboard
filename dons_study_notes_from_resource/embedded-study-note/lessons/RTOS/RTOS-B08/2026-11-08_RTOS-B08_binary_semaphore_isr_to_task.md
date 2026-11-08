---
id: RTOS-B08
time: 60분
updated: 2026-11-08
---
# RTOS-B08 바이너리 세마포어: ISR → 태스크 신호

## 학습 목표

- ISR 은 `xSemaphoreGiveFromISR` 로 **신호만** 주고 일은 태스크가 하는 "지연 처리(deferred processing)" 패턴을 미니 커널 모델로 구현하고, give 는 **절대 막히지 않고**(꽉 차면 `errQUEUE_FULL`) take 는 **막힌다**(타임아웃 포함)는 비대칭을 코드로 설명한다.
- 처리보다 빠르게 이벤트가 오면 바이너리 세마포어에서 신호가 **합쳐져 유실** 되는 것을 틱 단위 타임라인으로 재현하고, 카운팅 세마포어(최대값 포화 포함)와 숫자로 비교한다 — 그리고 진짜 FreeRTOS(V11.3.2 POSIX 포트)가 같은 숫자를 내는지 확인한다.
- `pxHigherPriorityTaskWoken` 의 정확한 의미(깨운 태스크가 **끼어든 태스크보다** 높을 때만 pdTRUE, false 로는 안 되돌림)와 `portYIELD_FROM_ISR` 를 빼먹었을 때의 지연(최대 1 틱)을 말할 수 있다.
- UART RX 처럼 데이터가 따로 있는 경우 "**바이너리 + 링 드레인**" 또는 "**카운팅 + 1개씩**" 짝이 맞는 이유, 그리고 짝이 틀리면 생기는 일(바이트가 갇힘 / 빈 깨어남)을 안다.

## 1. 문제

"ISR 에서 긴 처리를 하면 안 되죠. 그럼 어떻게 하나요? 바이너리 세마포어를 쓴다면, 이벤트가 처리보다 빨리 오면?" — RTOS 펌웨어 면접의 단골 흐름이다. 정답의 앞부분(ISR 은 짧게, 태스크로 미룬다)은 다들 말한다. 차이는 뒷부분이다: **바이너리 세마포어는 "1개 이상 왔다" 만 기억하므로, 처리 태스크가 바쁜 동안 온 이벤트는 합쳐진다.** 이걸 모르고 "이벤트마다 1번 처리" 를 가정한 코드는 부하가 몰릴 때만 이벤트를 잃는다 — 실험실에선 멀쩡하고 현장에서만 깨지는 종류의 버그다.

실제 칩에서 이 구조는 매일 나온다. STM32 의 `USART1_IRQHandler` 가 수신 바이트를 링 버퍼에 넣고 파서 태스크를 깨운다. DMA 전송 완료(TC) ISR 이 "버퍼 하나 찼다" 를 처리 태스크에 알린다. 타이머 비교 일치 ISR 이 제어 루프 태스크를 깨운다. 모두 **ISR → give → (태스크가) take** 다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">지연 처리: ISR 은 신호만 주고, 일은 태스크가 한다</text>
  <rect x="10" y="42" width="220" height="110" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="64" fill="currentColor" font-weight="bold">USART1_IRQHandler (ISR)</text>
  <text x="22" y="86" fill="currentColor">① DR 읽기 → 링 버퍼에 넣기</text>
  <text x="22" y="108" fill="currentColor">② ...GiveFromISR(s, &amp;woken)</text>
  <text x="22" y="130" fill="currentColor">③ portYIELD_FROM_ISR(woken)</text>
  <rect x="290" y="62" width="160" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="302" y="86" fill="currentColor" font-weight="bold">바이너리 세마포어</text>
  <text x="302" y="110" fill="currentColor" font-family="monospace">count ∈ {0, 1}</text>
  <rect x="510" y="42" width="220" height="110" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="522" y="64" fill="currentColor" font-weight="bold">파서 태스크 (prio 2)</text>
  <text x="522" y="86" fill="currentColor">xSemaphoreTake(s, 무한)</text>
  <text x="522" y="108" fill="currentColor">→ 비었으면 막힌다(Blocked)</text>
  <text x="522" y="130" fill="currentColor">→ 깨면 링을 처리</text>
  <path d="M230 97 L288 97" stroke="currentColor" stroke-width="2"/>
  <path d="M278 91 L288 97 L278 103" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="236" y="88" fill="currentColor">give</text>
  <path d="M450 97 L508 97" stroke="currentColor" stroke-width="2"/>
  <path d="M498 91 L508 97 L498 103" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="458" y="88" fill="currentColor">take</text>
  <rect x="10" y="176" width="350" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="198" fill="currentColor">give 는 절대 기다리지 않는다: 꽉 찼으면</text>
  <text x="22" y="218" fill="currentColor">즉시 errQUEUE_FULL 을 돌려주고 끝 (ISR 은 못 멈춘다)</text>
  <rect x="380" y="176" width="350" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="392" y="198" fill="currentColor">take 는 기다릴 수 있다: count 0 이면 Blocked,</text>
  <text x="392" y="218" fill="currentColor">give 또는 타임아웃이 깨운다 (태스크만 가능)</text>
  <rect x="10" y="240" width="720" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="262" fill="currentColor">태스크가 처리 중일 때 이벤트가 2개 오면: 첫 give 는 count 0→1, 둘째 give 는 count 가 이미 1 → 실패.</text>
  <text x="22" y="282" fill="currentColor">태스크가 다음에 take 할 때 보이는 건 "신호 1개" — 이벤트 2개가 1개로 합쳐졌다(유실).</text>
</svg>
```

호스트(맥)에는 진짜 인터럽트도 RTOS 도 없으니 두 가지를 쓴다.

1. **체크되는 코드(starter/solution/practice)** — 단일 OS 스레드에서 도는 **틱 단위 결정적 미니 커널** (forced schedule). 태스크는 한 틱에 한 걸음씩 실행되는 상태 기계이고, ISR 대용은 하네스가 각본(`ev[]` = 인터럽트가 걸리는 틱 목록)대로 부르는 함수다. 같은 입력이면 매번 같은 타임라인이 나온다. 한 틱 안의 순서는 고정이다:

   | 순서 | 하는 일 | 실제 RTOS 의 대응 |
   |---|---|---|
   | ① | `kernel_tick()` — 타임아웃 만료된 태스크 깨우기 | SysTick ISR 안의 `xTaskIncrementTick()` |
   | ② | `pick_next()` — 가장 높은 우선순위 READY 태스크 선택 | `vTaskSwitchContext()` |
   | ③ | 장치 ISR (같은 틱에 여러 번 가능). yield 요청 시 즉시 다시 선택 | `USART1_IRQHandler` + `portYIELD_FROM_ISR` → PendSV |
   | ④ | 선택된 태스크가 한 걸음. 그 안에서 막히면 다음 태스크가 이어 받음 | 태스크 코드 실행 |

   타임라인 표기 (RTOS-M03/M04/M05 와 같은 형식): `R` = 이 틱에 실행, `B` = 막힘(이 틱에 잠깐 돌고 바로 막힌 경우 포함), `r` = READY 인데 CPU 를 못 받음. `IRQ` 줄은 그 틱의 인터럽트 수, `give` 줄은 그 틱 give 의 결과(`o` 성공 / `x` 꽉 참).
2. **진짜 FreeRTOS** — `demo_freertos_isr_sem.c`, `demo_freertos_sweep.c`. POSIX 포트는 "Scheduler timer" **pthread** 가 1 ms 마다 실행 중인 태스크 스레드에 `SIGALRM` 을 보내고, 그 핸들러가 틱 ISR 이다. `configUSE_TICK_HOOK=1` 로 틱 ISR 안에서 불리는 `vApplicationTickHook()` 에서 `xSemaphoreGiveFromISR` 를 부르면 "타이머 스레드가 일으킨 ISR 에서 give" 가 된다. 실시간성은 없으므로 **틱 번호·반환값·순서만** 비교한다.

### Part A — give 와 take 의 의미 (`ksem_give_from_isr`, `ksem_take`)

- `bool ksem_give_from_isr(ksem_t *s, bool *woken)` — `count < max` 면 +1 하고 true. 이미 `max` 면 아무것도 바꾸지 않고 **즉시** false (= `errQUEUE_FULL`). 기다리던 태스크(`s->waiter`)가 있으면 READY 로 깨우고(`WAKE_GIVE`), 그 태스크의 우선순위가 **ISR 이 끼어든 태스크(`g_cur`)보다 높을 때만** `*woken = true` (`g_cur == -1` 은 idle 이므로 항상 true). `*woken` 을 false 로 쓰지는 않는다.
- `take_rc_t ksem_take(ksem_t *s, int self, int timeout)` — `count > 0` 이면 −1 하고 `TAKE_OK`. 비었고 `timeout == 0` 이면 `TAKE_EMPTY`(즉시 실패). 아니면 `task_block()` 으로 막고 `TAKE_BLOCKED` — 깨어난 태스크는 다시 `ksem_take` 를 부른다 (FreeRTOS `xQueueSemaphoreTake` 의 `for(;;)` 루프와 같다).

### Part B — 막기·타임아웃, 그리고 유실 재현 (`task_block`, `kernel_tick`)

- `task_block(self, s, timeout)` — BLOCKED 로 바꾸고 `s->waiter` 에 등록, `deadline = (timeout == FOREVER) ? FOREVER : g_tick + timeout`.
- `kernel_tick()` — `deadline <= g_tick` 인 막힌 태스크를 세마포어 waiter 에서 빼고 `WAKE_TIMEOUT` 으로 깨운다.
- 그 다음 테스트가 시나리오를 돌린다: 이벤트 5개가 1 틱 간격(10..14 틱), 처리 3 틱/개. 바이너리 / 카운팅(10) / 카운팅(2) 의 처리 수·유실 수·최대 count 를 확인하고, 진짜 FreeRTOS 결과와 비교한다.

### Part C — UART RX ISR → 파서 태스크 (`uart_rx_isr`, `parser_after_byte`)

- `uart_rx_isr()` — `uart_read_dr()` 로 바이트를 읽어 링(8칸)에 넣는다. 꽉 찼으면 `g_overrun++` 하고 끝(기다리지 않음, give 도 안 함 — 하드웨어 UART 의 ORE 와 같은 처지). 넣었으면 give (실패하면 `g_merged++` — "이미 신호가 있다"), `g_use_yield` 면 `port_yield_from_isr(woken)`.
- `parser_after_byte(&next)` — 바이트 하나 처리가 끝났을 때 `MODE_DRAIN` 이면 링에서 하나 더 꺼내 계속(true), 아니면 false(→ 다시 take).
- 테스트: `"PING\n"` 이 5 틱에 3바이트, 6 틱에 2바이트로 몰려 온다. 바이너리+드레인 / 바이너리+1개씩 / 카운팅+1개씩 결과, 12바이트 폭주 시 overrun, 그리고 `portYIELD_FROM_ISR` 유무에 따른 첫 실행 틱.

## 2. Problem (English)

> **ISR-to-task signalling with a binary semaphore.**
>
> You are given a deterministic, tick-driven mini kernel (single OS thread). Each tick: (1) `kernel_tick()` expires timeouts, (2) the highest-priority READY task is selected, (3) scripted "device ISRs" run (an ISR may request an immediate reschedule via `port_yield_from_isr(true)`), (4) the selected task runs one step. A semaphore is `{count, max, waiter}` (`max == 1` → binary; one waiter slot).
>
> 1. `bool ksem_give_from_isr(ksem_t *s, bool *woken)` — never blocks. If `count == max` return false (FreeRTOS `errQUEUE_FULL`) and change nothing. Otherwise `count++`; if a task is waiting, make it READY (`WAKE_GIVE`), clear the waiter, and set `*woken = true` iff its priority is higher than the interrupted task `g_cur` (or `g_cur == -1`). Never write false to `*woken`.
> 2. `take_rc_t ksem_take(ksem_t *s, int self, int timeout)` — `TAKE_OK` (count−1), `TAKE_EMPTY` if empty and `timeout == 0`, otherwise block the caller and return `TAKE_BLOCKED`.
> 3. `void task_block(int self, ksem_t *s, int timeout)` and `void kernel_tick(void)` — blocking with an absolute deadline; on expiry remove the task from the semaphore and wake it with `WAKE_TIMEOUT`.
> 4. `void uart_rx_isr(void)` and `bool parser_after_byte(unsigned char *next)` — UART RX ISR that pushes into an 8-byte ring (count overruns, never block), gives the semaphore, optionally yields; parser that either processes one byte per take or drains the ring.
>
> Example: 5 events at ticks 10..14, 3 ticks of work each. Binary: gives `o o x x o`, 3 processed (starts at 10, 13, 16), 2 lost. Counting(10): 5 processed, max count 3. Counting(2): 4 processed, 1 lost.
>
> Follow-ups: why can't an ISR call `xSemaphoreTake(s, portMAX_DELAY)`? What happens if you ignore `xHigherPriorityTaskWoken`? When is a binary semaphore still the right choice for a UART that delivers many bytes?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
# 처음엔 "6/37 passed" (init 검사 등만 통과), exit 1 — TODO 6곳을 채워 37/37 을 만든다
```

스레드는 쓰지 않지만 시리즈의 다른 레슨과 같은 명령을 쓰려고 `-pthread` 를 붙였다. 시뮬레이션은 틱 수가 정해져 있어 TODO 가 비어 있어도 멈추지 않는다 (starter 실행에서 처리 태스크가 계속 `R` 로 찍히는 건 `ksem_take` 스텁이 늘 `TAKE_EMPTY` 를 돌려줘 폴링하기 때문이다).

진짜 FreeRTOS 데모 (커널 소스는 `~/.esn_scratch/FreeRTOS-Kernel`, V11.3.2 태그. 이 폴더의 `FreeRTOSConfig.h` 는 템플릿에서 `configUSE_TICK_HOOK` 만 1 로 바꾼 것):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_isr_sem.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o demo_isr && ./demo_isr
# demo_freertos_sweep.c 도 같은 줄에서 파일 이름만 바꾼다 (-o demo_sweep)
```

## 4. 힌트

<details>
<summary>힌트 1 — give: 실패 경로가 먼저다</summary>

ISR 은 기다릴 수 없다. 그러니 "꽉 찼으면?" 의 답은 하나뿐 — **아무것도 바꾸지 말고 false**. 그 다음에 count 를 올리고, 기다리는 태스크가 있으면 깨운다. 깨운 태스크가 count 를 직접 가져가지 않는다는 점에 주의: 깨어난 태스크가 다시 `ksem_take` 를 불러서 가져간다 (그래서 give 직후 count 는 1).

</details>

<details>
<summary>힌트 2 — woken 은 "전환이 필요한가" 다</summary>

ISR 은 어떤 태스크(`g_cur`)를 멈추고 들어왔다. ISR 이 끝나면 원래 그 태스크로 돌아가는 게 기본이다. 방금 깨운 태스크가 그보다 **높으면** 돌아가지 말고 전환해야 한다 → true. 같거나 낮으면 전환할 이유가 없다. 그리고 한 ISR 안에서 give 를 여러 번 할 수 있으니, 두 번째 give 가 첫 번째의 true 를 지우면 안 된다.

</details>

<details>
<summary>힌트 3 — 타임아웃은 "절대 틱" 으로 저장한다</summary>

`deadline = g_tick + timeout` 을 저장하고, 매 틱 `deadline <= g_tick` 을 본다. 깨울 때 세마포어의 `waiter` 도 지워야 한다 — 안 지우면 나중의 give 가 이미 깨어 다른 일을 하는 태스크를 "또 깨운다". give 로 깨울 때는 반대로 deadline 이 지워져야 한다 (`task_wake` 가 해 준다).

</details>

<details>
<summary>힌트 4 — UART: 신호의 수 ≠ 바이트의 수</summary>

바이너리 세마포어는 바이트 수를 세지 못한다. 그럼 바이트 수는 어디에 있나? 링의 `n` 에. 파서가 한 번 깨어날 때 **링이 빌 때까지** 처리하면 신호가 몇 번 합쳐졌든 상관없다. `parser_after_byte` 는 한 줄이면 된다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨의 모델 | FreeRTOS (V11.3.2) | Zephyr | CMSIS-RTOS2 | Cortex-M 에서 실제로 일어나는 일 |
|---|---|---|---|---|
| `ksem_init(s, 1, 0)` | `xSemaphoreCreateBinary()` (처음엔 비어 있음) | `k_sem_init(&s, 0, 1)` | `osSemaphoreNew(1, 0, NULL)` | RAM 에 제어 블록 |
| `ksem_init(s, N, 0)` | `xSemaphoreCreateCounting(N, 0)` | `k_sem_init(&s, 0, N)` | `osSemaphoreNew(N, 0, NULL)` | |
| `ksem_give_from_isr(s, &w)` | `xSemaphoreGiveFromISR(s, &w)` → pdTRUE / `errQUEUE_FULL` | `k_sem_give(&s)` (ISR 에서 가능, 반환값 없음, limit 에서 그대로 — 문서 기준) | `osSemaphoreRelease(s)` (ISR 가능, 최대면 `osErrorResource` — 문서 기준) | 임계 구역(BASEPRI) 안에서 count 갱신 |
| `port_yield_from_isr(w)` | `portYIELD_FROM_ISR(w)` | 없음 — ISR 종료 시 커널이 판단 (문서 기준) | 없음 (커널 내부) | `SCB->ICSR = PENDSVSET` → ISR 끝나고 PendSV 에서 전환 |
| `ksem_take(s, me, FOREVER)` | `xSemaphoreTake(s, portMAX_DELAY)` | `k_sem_take(&s, K_FOREVER)` | `osSemaphoreAcquire(s, osWaitForever)` | 태스크를 대기 리스트로, 컨텍스트 스위치 |
| `ksem_take(s, me, 0)` → `TAKE_EMPTY` | `xSemaphoreTake(s, 0)` → pdFALSE | `k_sem_take(&s, K_NO_WAIT)` → `-EBUSY` | → `osErrorResource` | |
| 타임아웃 → `WAKE_TIMEOUT` | `xSemaphoreTake(s, n)` → pdFALSE | → `-EAGAIN` | → `osErrorTimeout` | SysTick 이 지연 리스트 만료 |
| `kernel_tick()` | `xTaskIncrementTick()` | `sys_clock_announce()` (타이머 드라이버가 호출, 문서 기준) | — | SysTick_Handler |

(Zephyr/CMSIS 칸은 공식 문서 기준, 이 레슨에서 실행 안 함 — RTOS-A07.)

### Part A — give: 꽉 찼으면 즉시 실패, 아니면 +1 하고 깨운다

```c
static bool ksem_give_from_isr(ksem_t *s, bool *woken) {
    if (s->count >= s->max) return false;          /* 꽉 참: 기다리지 않고 즉시 실패 */
    s->count++;
    if (s->waiter >= 0) {
        int w = s->waiter;
        s->waiter = -1;
        task_wake(w, WAKE_GIVE);                   /* 깨어난 태스크가 다시 take 해서 count 를 가져간다 */
        if (g_cur < 0 || g_task[w].prio > g_task[g_cur].prio) *woken = true;
    }
    return true;
}
```

진짜 FreeRTOS 도 같은 모양이다. 세마포어는 "항목 크기 0 인 큐" 이고 count 는 `uxMessagesWaiting` 이다 (`semphr.h:167` — 바이너리 = `xQueueGenericCreate(1, 0, queueQUEUE_TYPE_BINARY_SEMAPHORE)`). `xSemaphoreGiveFromISR` 는 `xQueueGiveFromISR` 매크로다 (`semphr.h:640`). 발췌:

```c
/* FreeRTOS-Kernel V11.3.2 queue.c — xQueueGiveFromISR (1338~) 발췌, 줄 번호는 실제 파일 기준 */
1381  uxSavedInterruptStatus = ( UBaseType_t ) taskENTER_CRITICAL_FROM_ISR();
1388      if( uxMessagesWaiting < pxQueue->uxLength )          /* 바이너리는 uxLength == 1 */
1400          pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxMessagesWaiting + ( UBaseType_t ) 1 );
1431          if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
1433              if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE )
1439                  *pxHigherPriorityTaskWoken = pdTRUE;     /* true 만 쓴다. false 는 안 쓴다 */
1498          xReturn = errQUEUE_FULL;                         /* else: 대기 없이 바로 실패 */
1501  taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
```

`xTaskRemoveFromEventList` 가 언제 pdTRUE 를 돌려주나 (`tasks.c:5555`): `pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority` — 깨운 태스크가 **지금 돌던 태스크보다 높을 때**. 모델의 `g_task[w].prio > g_task[g_cur].prio` 가 이 줄이다. 그래서 `semphr.h:570-574` 의 설명대로 호출자가 `xHigherPriorityTaskWoken = pdFALSE` 로 **초기화** 해야 한다. 초기화하지 않은 쓰레기 값이 0 이 아니면 매번 쓸데없이 전환한다.

테스트가 확인하는 다섯 경우:

| 상황 | give 반환 | `*woken` | 이유 |
|---|---|---|---|
| 아무도 안 기다림, count 0 | true, count 1 | 그대로 (false) | 깨운 태스크 없음 |
| count 이미 1 (바이너리) | **false**, count 1 | 그대로 | 합쳐짐 = 이벤트 유실 |
| H(2) 가 대기, L(1) 실행 중 | true | **true** | H 가 L 보다 높다 → 전환 필요 |
| 위 직후 한 번 더 give (실패) | false | **true 유지** | 실패한 give 가 앞의 true 를 지우면 안 된다 |
| H(2) 가 대기, T(3) 실행 중 | true | false | 더 높은 T 로 돌아가는 게 맞다. H 는 READY 로만 |
| H(2) 가 대기, idle 중 | true | true | idle 보다는 누구든 높다 |

**take** 는 반대로 기다릴 수 있다:

```c
static take_rc_t ksem_take(ksem_t *s, int self, int timeout) {
    if (s->count > 0) { s->count--; return TAKE_OK; }
    if (timeout == 0) return TAKE_EMPTY;
    task_block(self, s, timeout);
    return TAKE_BLOCKED;
}
```

FreeRTOS `xQueueSemaphoreTake` (`queue.c:1659`): `if( uxSemaphoreCount > 0 )` (1696) → `uxMessagesWaiting = uxSemaphoreCount - 1` (1702) → pdPASS. 비었고 `xTicksToWait == 0` (1745) → `errQUEUE_EMPTY`. 아니면 `vTaskPlaceOnEventList(&xTasksWaitingToReceive, xTicksToWait)` (1806) 로 막고, 깨어나면 바깥 `for( ; ; )` 루프 처음으로 돌아가 count 를 **다시** 확인한다 — 모델에서 깨어난 태스크가 `ksem_take` 를 다시 부르는 것과 같다.

| | give (`...FromISR`) | take |
|---|---|---|
| ISR 에서 | 된다 (FromISR 판) | **안 된다** — ISR 은 태스크가 아니라 막힐 "자리" 가 없다 |
| 꽉 참 / 비었음 | 즉시 `errQUEUE_FULL` | 기다린다 (타임아웃까지) |
| 태스크에서 | `xSemaphoreGive` 도 block time 0 (`semGIVE_BLOCK_TIME`, `semphr.h:42`, 460) → 역시 즉시 실패 | 기다린다 |
| 반환값 | 성공/실패 → 실패 = 이벤트 유실 신호 | 성공/타임아웃 |

`errQUEUE_FULL` 과 `errQUEUE_EMPTY` 는 둘 다 `( ( BaseType_t ) 0 )` 이다 (`projdefs.h:61-62`) — pdFALSE 와 같은 값이라 `if (xSemaphoreGiveFromISR(...) != pdTRUE) lost++;` 로 세면 된다.

### Part B — 막기와 타임아웃

```c
static void task_block(int self, ksem_t *s, int timeout) {
    task_t *t = &g_task[self];
    t->st = ST_BLOCKED;
    t->wait_on = s;
    t->deadline = (timeout == FOREVER) ? FOREVER : g_tick + timeout;
    t->wake = WAKE_NONE;
    s->waiter = self;
}
static void kernel_tick(void) {
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        if (t->st == ST_BLOCKED && t->deadline != FOREVER && t->deadline <= g_tick) {
            if (t->wait_on && t->wait_on->waiter == i) t->wait_on->waiter = -1;
            task_wake(i, WAKE_TIMEOUT);
        }
    }
}
```

FreeRTOS 는 막힌 태스크를 **두 리스트에 동시에** 건다: 세마포어의 이벤트 리스트(`xTasksWaitingToReceive`)와 지연 리스트(타임아웃용). 어느 쪽이 먼저 깨우든 다른 쪽에서도 빼야 한다. 모델의 `waiter = -1` (타임아웃 쪽)과 `task_wake` 의 `deadline = FOREVER` (give 쪽)가 그 일이다. 테스트 `late[] = {3}` (타임아웃 5 전에 3 틱에 이벤트)는 낡은 deadline 이 남아 있으면 일하던 태스크를 "타임아웃" 으로 착각하게 되는 경우를 막는다. 리스트를 진짜 정렬 리스트로 만드는 일은 RTOS-M06/M07, 여러 대기자의 순서는 RTOS-B09 의 몫이다. 이벤트 없이 타임아웃 5 로 기다리면 0→5→10→15→20 틱에 4번 타임아웃이 난다 (21 틱 시뮬레이션). 이것이 "센서가 멈췄는지" 감시하는 워치독 패턴이다 (추가 연습 3).

### Part B — 처리보다 빠른 이벤트: 합쳐짐 재현

`solution` 의 실제 출력 (이벤트 10..14 틱, 처리 3 틱/개):

```text
  -- binary, burst 10..14, PROC=3
  tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  IRQ  : . . . . . . . . . . 1 1 1 1 1 . . . . . . . . . . . . . . .
  give : . . . . . . . . . . o o x x o . . . . . . . . . . . . . . .
  H    : B B B B B B B B B B R R R R R R R R R B B B B B B B B B B B
  bg   : R R R R R R R R R R r r r r r r r r r R R R R R R R R R R R
  -- counting(10), burst 10..14, PROC=3
  tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
  IRQ  : . . . . . . . . . . 1 1 1 1 1 . . . . . . . . . . . . . . .
  give : . . . . . . . . . . o o o o o . . . . . . . . . . . . . . .
  H    : B B B B B B B B B B R R R R R R R R R R R R R R R B B B B B
  bg   : R R R R R R R R R R r r r r r r r r r r r r r r r R R R R R
```

틱별로 따라가면 (바이너리):

| 틱 | ISR 의 give | count (그 틱 끝) | 처리 태스크 H |
|---|---|---|---|
| 10 | o — H 를 깨움, woken | 0 | 바로 실행(yield) → take(1→0), 이벤트 ① 처리 시작 |
| 11 | o | 1 | 처리 중 (세마포어를 안 본다) |
| 12 | **x** — 이미 1 | 1 | 처리 중 |
| 13 | **x** — 이미 1 (ISR 이 ④ 보다 먼저) | 0 | ① 끝 → take(1→0), 다음 처리 시작 |
| 14 | o | 1 | 처리 중 |
| 16 | — | 0 | 끝 → take(1→0), 처리 시작 |
| 19 | — | 0 | 끝 → take → 비었음 → 막힘 (`B`) |

이벤트 5개 중 3개만 처리, 2개(12, 13 틱)는 **앞의 신호와 합쳐졌다**. ISR 쪽에서는 give 가 false 를 돌려준 횟수로 정확히 셀 수 있다 (`events == processed + give_fail` 체크). 카운팅(10)은 count 가 최대 3 까지 쌓였다가 하나씩 줄어 5개를 다 처리한다 — 대신 마지막 이벤트(14 틱)는 22 틱에야 시작한다(8 틱 지연). 카운팅은 이벤트를 **잃지 않을 뿐** 빨리 처리해 주지는 않는다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">solution 이 찍은 타임라인 (틱 8~26): 처리 구간 = 이벤트 1개 (3 틱)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="104" y="46">8</text><text x="168" y="46">10</text><text x="264" y="46">13</text>
    <text x="360" y="46">16</text><text x="456" y="46">19</text><text x="552" y="46">22</text><text x="648" y="46">25</text>
  </g>
  <line x1="110" y1="52" x2="718" y2="52" stroke="currentColor"/>
  <text x="10" y="80" fill="currentColor">binary give</text>
  <g font-family="monospace" font-size="13" fill="currentColor">
    <text x="184" y="80">o</text><text x="216" y="80">o</text><text x="248" y="80">x</text><text x="280" y="80">x</text><text x="312" y="80">o</text>
  </g>
  <rect x="242" y="64" width="20" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="274" y="64" width="20" height="22" class="f-warn" stroke="currentColor"/>
  <text x="10" y="112" fill="currentColor">binary H</text>
  <rect x="174" y="96" width="96" height="24" class="f-acc" stroke="currentColor"/><text x="214" y="113" fill="currentColor">①</text>
  <rect x="270" y="96" width="96" height="24" class="f-acc" stroke="currentColor"/><text x="310" y="113" fill="currentColor">②</text>
  <rect x="366" y="96" width="96" height="24" class="f-acc" stroke="currentColor"/><text x="406" y="113" fill="currentColor">③</text>
  <text x="472" y="113" fill="currentColor">B … 처리 3개, 유실 2개</text>
  <text x="10" y="170" fill="currentColor">count(10) give</text>
  <g font-family="monospace" font-size="13" fill="currentColor">
    <text x="184" y="170">o</text><text x="216" y="170">o</text><text x="248" y="170">o</text><text x="280" y="170">o</text><text x="312" y="170">o</text>
  </g>
  <text x="10" y="202" fill="currentColor">count(10) H</text>
  <rect x="174" y="186" width="96" height="24" class="f-ok" stroke="currentColor"/><text x="214" y="203" fill="currentColor">①</text>
  <rect x="270" y="186" width="96" height="24" class="f-ok" stroke="currentColor"/><text x="310" y="203" fill="currentColor">②</text>
  <rect x="366" y="186" width="96" height="24" class="f-ok" stroke="currentColor"/><text x="406" y="203" fill="currentColor">③</text>
  <rect x="462" y="186" width="96" height="24" class="f-ok" stroke="currentColor"/><text x="502" y="203" fill="currentColor">④</text>
  <rect x="558" y="186" width="96" height="24" class="f-ok" stroke="currentColor"/><text x="598" y="203" fill="currentColor">⑤</text>
  <text x="10" y="236" fill="currentColor">count 값(카운팅, give 직후): 10→1 11→1 12→2 13→3 14→3, 그 뒤 처리 시작마다 −1</text>
  <rect x="10" y="248" width="720" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="270" fill="currentColor">x = give 가 errQUEUE_FULL. 그 순간 count 는 이미 1 — 태스크가 처리 중이라 아무도 take 하지 않았다.</text>
</svg>
```

**진짜 FreeRTOS 도 같은가** — `demo_freertos_isr_sem.c`:

<!-- code: demo_freertos_isr_sem.c -->

실제 출력 (Apple M6, Apple clang 21.0.0, 10회 실행 모두 같은 출력):

```text
binary created: count=0
take(s, 0)        -> 0 (errQUEUE_EMPTY=0), returned at once
take(s, 5 ticks)  -> 0 after 5 ticks blocked
give, give        -> 1, 0 (errQUEUE_FULL=0), count=1

burst: 5 events at base+0..4 (1/tick), PROC=3 ticks per event
semaphore     gives(o=ok,x=full)  ok fail woken maxcnt done  start ticks (rel. base)
binary        ooxxo                3    2     1      1    3  0 3 6
counting(10)  ooooo                5    0     1      3    5  0 3 6 9 12
counting(2)   oooxo                4    1     1      2    4  0 3 6 9
```

모델 테스트의 숫자와 **완전히 같다**: 바이너리 3 처리 / 2 실패(`ooxxo`), 카운팅(10) 5 처리 / 최대 count 3, 카운팅(2) 4 처리 / 1 실패 — 카운팅도 **max 에 닿으면 바이너리처럼 잃는다**. `woken` 은 각 1번: 처리 태스크가 막혀 있던 첫 이벤트에서만 true 였다 (나머지 give 때는 처리 태스크가 실행 중이라 기다리는 태스크가 없다). 위 네 줄은 take/give 의 비대칭을 그대로 보여 준다: 빈 세마포어에 `take(s, 0)` 은 즉시 0, `take(s, 5)` 는 정확히 5 틱 막혔다가 0, 태스크의 `xSemaphoreGive` 도 꽉 차면 즉시 0.

이 데모에서 "ISR" 은 틱 훅이다. POSIX 포트는 `prvTimerTickHandler` 스레드(`port.c:452-472`)가 `pthread_kill(…, SIGALRM)` 을 보내고, 핸들러 `vPortSystemTickHandler`(`port.c:488`)가 `xTaskIncrementTick()` 을 부르며, 그 안에서 `vApplicationTickHook()` 이 불린다 (`tasks.c:5000-5013`, 헤더 설명 "called in the system tick handler", `task.h:2053`). give 가 더 높은 태스크를 깨우면 `xTaskRemoveFromEventList` 가 `xYieldPendings[0] = pdTRUE` 를 남기고(`tasks.c:5564`) 틱 ISR 이 끝에서 그걸 보고 전환한다(`tasks.c:5020`) — 그래서 틱 훅 안에서는 `portYIELD_FROM_ISR` 를 따로 부르지 않았다. 왜 아무 pthread 에서나 `xSemaphoreGiveFromISR` 를 부르지 않았나: POSIX 포트의 "인터럽트 끄기" 는 **호출한 스레드의** `pthread_sigmask` 이고, FreeRTOS 가 만든 스레드가 아니면 아무것도 안 한다 (`port.c:406-421`). `vPortYield` 는 `configASSERT( prvIsFreeRTOSThread() == pdTRUE )` 다 (`port.c:396`). 즉 외부 스레드에서의 호출은 커널을 보호하지 못하고, yield 하면 assert 로 죽는다. 실제 MCU 의 "ISR 은 현재 태스크를 멈추고 같은 코어에서 돈다" 를 흉내 내는 건 이 포트에서는 SIGALRM 핸들러(틱 ISR) 뿐이다.

### Part B — 얼마나 빨라야 안 잃나: 주기 E, 처리 P

주기 E = 3 틱으로 12개를 보내고 처리 시간 P 를 바꿔 본다. 모델(practice.c 연습 1)과 진짜 FreeRTOS 를 비교한다.

<!-- code: demo_freertos_sweep.c -->

실제 출력 (5회 실행 모두 같음, 모델 `practice` 의 표와 칸마다 같다):

```text
E=3, N=12 periodic events, P ticks per event (FreeRTOS POSIX port)
 P  binary(done/ok/fail)  counting16(done/ok/fail)  formula
 1    12 / 12 /  0           12 / 12 /  0           12
 2    12 / 12 /  0           12 / 12 /  0           12
 3    12 / 12 /  0           12 / 12 /  0           12
 4    10 / 10 /  2           12 / 12 /  0           10
 5     8 /  8 /  4           12 / 12 /  0            8
 6     7 /  7 /  5           12 / 12 /  0            7
 7     6 /  6 /  6           12 / 12 /  0            6
 8     6 /  6 /  6           12 / 12 /  0            6
```

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">처리한 이벤트 수 (12개 중) — 주기 E = 3 틱, 처리 P 틱. 실측 (모델 = FreeRTOS)</text>
  <line x1="80" y1="260" x2="700" y2="260" stroke="currentColor"/>
  <line x1="80" y1="50" x2="80" y2="260" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="58" y="64">12</text><text x="62" y="164">6</text><text x="62" y="264">0</text>
  </g>
  <line x1="80" y1="60" x2="700" y2="60" stroke="currentColor" stroke-dasharray="3 3"/>
  <g stroke="currentColor">
    <rect x="94" y="60" width="26" height="200" class="f-acc"/><rect x="122" y="60" width="26" height="200" class="f-ok"/>
    <rect x="169" y="60" width="26" height="200" class="f-acc"/><rect x="197" y="60" width="26" height="200" class="f-ok"/>
    <rect x="244" y="60" width="26" height="200" class="f-acc"/><rect x="272" y="60" width="26" height="200" class="f-ok"/>
    <rect x="319" y="93" width="26" height="167" class="f-acc"/><rect x="347" y="60" width="26" height="200" class="f-ok"/>
    <rect x="394" y="127" width="26" height="133" class="f-acc"/><rect x="422" y="60" width="26" height="200" class="f-ok"/>
    <rect x="469" y="143" width="26" height="117" class="f-acc"/><rect x="497" y="60" width="26" height="200" class="f-ok"/>
    <rect x="544" y="160" width="26" height="100" class="f-acc"/><rect x="572" y="60" width="26" height="200" class="f-ok"/>
    <rect x="619" y="160" width="26" height="100" class="f-acc"/><rect x="647" y="60" width="26" height="200" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="322" y="88">10</text><text x="401" y="122">8</text><text x="476" y="138">7</text>
    <text x="551" y="155">6</text><text x="626" y="155">6</text>
    <text x="113" y="280">P=1</text><text x="190" y="280">2</text><text x="265" y="280">3</text><text x="340" y="280">4</text>
    <text x="415" y="280">5</text><text x="490" y="280">6</text><text x="565" y="280">7</text><text x="640" y="280">8</text>
  </g>
  <rect x="94" y="292" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="114" y="303" fill="currentColor">바이너리</text>
  <rect x="190" y="292" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="210" y="303" fill="currentColor">카운팅(16)</text>
  <text x="320" y="303" fill="currentColor">P ≤ E 이면 둘 다 12. P 가 E 를 넘는 순간 바이너리만 줄어든다.</text>
</svg>
```

규칙: **처리 시간 P ≤ 이벤트 간격 E 이면 바이너리로 충분** (다음 이벤트가 오기 전에 처리 태스크가 다시 take 하고 있다). P = E 경계에서도 안 잃는 이유는 이 모델·FreeRTOS 둘 다 "같은 틱의 ISR 이 태스크보다 먼저" 이기 때문이다 — 실제 하드웨어에서는 경계가 지터에 따라 갈리므로 여유를 둔다. P > E 이면 바이너리는 P 틱마다 1개만 가져가고 나머지는 합쳐진다. 정확한 수는 연습 1 에서 공식으로 확인한다. 카운팅은 다 처리하지만 P > E 가 계속되면 count 가 끝없이 올라가 결국 max 에서 잃는다 — **평균 처리율이 도착률보다 낮으면 어떤 세마포어도 못 구한다.** 해결은 처리를 줄이거나(배치 처리) 우선순위·설계를 바꾸는 것이다.

### Part C — UART RX: 신호의 수가 아니라 링을 본다

```c
static void uart_rx_isr(void) {
    unsigned char b = uart_read_dr();
    if (!ring_put(&g_rx, b)) { g_overrun++; return; }
    bool woken = false;
    bool ok = ksem_give_from_isr(&g_rx_sem, &woken);
    tl_mark_give(ok);
    if (!ok) g_merged++;
    if (g_use_yield) port_yield_from_isr(woken);
}
static bool parser_after_byte(unsigned char *next) {
    return g_mode == MODE_DRAIN && ring_get(&g_rx, next);
}
```

`"PING\n"`: 5 틱에 3바이트, 6 틱에 2바이트, 바이트당 처리 1 틱. 실제 출력:

```text
  -- binary + drain, "PING\n"
  tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3
  IRQ  : . . . . . 3 2 . . . . . . .
  give : . . . . . x x . . . . . . .
  P    : B B B B B R R R R R B B B B
  bg   : R R R R R r r r r r R R R R
```

| 조합 | 결과 (테스트로 확인) | 왜 |
|---|---|---|
| 바이너리 + **드레인** | 프레임 `"PING"` 1개, 링 비움, 합쳐진 give 3번(5 틱 `o x x`, 6 틱 `o x` — `give` 줄은 그 틱 마지막 결과), 빈 깨어남 1번 | 바이트 수는 링의 `n` 이 안다. 신호는 "가서 봐라" 일 뿐 |
| 바이너리 + 1개씩 | 프레임 0, **링에 3바이트 갇힘** | 신호 2개(5·6 틱의 첫 give)로 2바이트만 꺼냈다. 다음 바이트가 올 때까지 `N G \n` 이 묶여 있다 |
| 카운팅(16) + 1개씩 | 프레임 1, 합쳐짐 0, 빈 깨어남 0 | count == 링의 바이트 수 (불변식) |
| 카운팅 + 드레인 | 빈 깨어남 4번 (연습 2) | 한 번에 다 비웠는데 count 4 가 남았다 |
| 12바이트가 한 틱에 | overrun 4, ISR 은 12번 다 즉시 반환 | 링 8칸. ISR 은 기다릴 곳이 없다 → 버리고 센다 |

빈 깨어남 1번은 버그가 아니다: 드레인하는 동안(6 틱) 들어온 바이트의 give 가 count 를 1 로 만들어 두었고, 링을 다 비운 뒤 take 가 그 신호를 가져가 보니 링이 비어 있는 것이다. **드레인 방식의 태스크는 "깨어났는데 할 일이 없음" 을 정상 경로로 처리해야 한다.** 반대로 "다 비웠으니 남은 신호를 지우자" (`while (xSemaphoreTake(s, 0) == pdTRUE);`) 는 틀렸다 — 비운 직후 ~ 지우기 사이에 들어온 바이트의 신호까지 지워서 그 바이트가 갇힌다.

펌웨어 결론:
- 데이터가 링/FIFO/DMA 버퍼에 따로 있으면 → **바이너리(또는 태스크 알림) + 드레인 루프**. 세마포어는 1비트 "깨워" 신호.
- 이벤트 자체가 데이터(“버튼 눌림 1번”, “DMA 반쪽 완료 1번”)이고 횟수가 중요하면 → **카운팅 + 1개씩**, max 는 최악 버스트 이상, give 실패는 반드시 센다.
- 바이트 스트림을 그대로 넘기려면 큐(`xQueueSendFromISR`, RTOS-B11)나 스트림 버퍼, 고속이면 ISR→태스크 SPSC 링(DS-A01) + 알림(RTOS-M01). 인터럽트 구동 UART 드라이버 자체는 BM-M04.

**`portYIELD_FROM_ISR` 를 빼먹으면** — 1바이트가 5 틱에 오고 그때 `bg`(prio 1)가 돌고 있다:

```text
  -- binary, no portYIELD_FROM_ISR
  tick : 0 1 2 3 4 5 6 7 8 9
  IRQ  : . . . . . 1 . . . .
  give : . . . . . o . . . .
  P    : B B B B B r R B B B
  bg   : R R R R R R r R R R
```

파서는 5 틱에 READY(`r`)가 됐지만 ISR 이 전환을 요청하지 않았으니 bg 가 그 틱을 마저 쓰고, 다음 틱 ISR(스케줄러)에서야 파서가 돈다 → `g_first_take == 6`. yield 를 하면 `5`. FreeRTOS 실제 동작도 같은 방향이다: woken 을 무시해도 `xYieldPendings` 가 남아 있어(`tasks.c:5564`) 늦어도 다음 틱에는 전환되지만, 1 ms 틱이면 최대 1 ms 의 불필요한 지연이다. Cortex-M 의 `portYIELD_FROM_ISR` 는 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT` (ICSR 0xE000ED04 의 bit 28) + `dsb`/`isb` 다 (`portable/GCC/ARM_CM4F/portmacro.h:88-114`). FreeRTOS 포트는 PendSV 를 가장 낮은 우선순위로 설정하므로(`ARM_CM4F/port.c:66`, 433 — `portMIN_INTERRUPT_PRIORITY`) **ISR 이 다 끝난 뒤** 전환이 일어난다 — ISR 중간에 태스크로 가는 게 아니다. yield 의 상세(지연 처리 설계, 여러 FromISR 호출의 woken 합치기)는 RTOS-M02. 그리고 진짜 FreeRTOS 에서 `xSemaphoreGiveFromISR` 는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 **급한** 우선순위의 ISR 에서 부르면 안 된다 (`portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`, `queue.c:1376` 과 그 위 주석). 그런 ISR 은 RTOS 임계 구역 중에도 실행되므로 커널 자료구조를 건드릴 수 없다 (C-M14 Q4 의 BASEPRI 이야기).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution    # 37/37 passed (5회 같은 결과)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_san && ./sol_san   # 37/37, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan   # 37/37, 보고 없음 (스레드 1개라 당연)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 에서 `xSemaphoreGive()` (FromISR 아님) 또는 `xSemaphoreTake(s, portMAX_DELAY)` | ISR 은 막힐 수 없고, 태스크용 API 는 ISR 문맥에서 컨텍스트 스위치를 시도한다. FreeRTOS 는 FromISR 판만 ISR 에서 허용 | `xSemaphoreGiveFromISR(s, &woken); portYIELD_FROM_ISR(woken);` |
| `BaseType_t woken;` 초기화 안 함 | give 는 true 만 쓴다(`queue.c:1439`). 쓰레기 값이면 매번 쓸데없는 전환 | `BaseType_t woken = pdFALSE;` |
| `portYIELD_FROM_ISR(woken)` 생략 | 깨운 태스크가 다음 틱까지 기다린다 (Part C: 5 → 6 틱) | ISR 마지막 줄에 넣는다 |
| 이벤트마다 1번 처리하는 태스크에 바이너리 세마포어 | 처리 중 온 이벤트가 합쳐진다 (Part B: 5개 중 2개 유실) | 횟수가 중요하면 카운팅 + give 실패 카운터, 아니면 상태(링)를 드레인 |
| give 반환값 무시 | 유실이 조용히 일어난다. 현장 로그로도 못 찾는다 | `if (xSemaphoreGiveFromISR(...) != pdTRUE) g_lost++;` |
| 카운팅 max 를 "적당히" 작게 | 버스트가 max 를 넘으면 카운팅도 잃는다 (카운팅(2): 1개 유실) | max ≥ 최악 버스트, 그래도 실패 카운터 |
| 드레인 태스크에서 빈 깨어남을 에러로 처리 | 드레인 중 들어온 신호 때문에 정상적으로 생긴다 | 링이 비었으면 조용히 다시 take |
| 드레인 후 `while (take(s, 0));` 로 신호 정리 | 비운 직후 들어온 바이트의 신호까지 지워 그 바이트가 갇힌다 | 정리하지 않는다. 빈 깨어남을 허용 |
| `vSemaphoreCreateBinary()` (옛 매크로)를 새 것과 같다고 가정 | 옛 매크로는 **찬 상태** 로 만든다 → 첫 take 가 이벤트 없이 통과 (`semphr.h:129-134`) | `xSemaphoreCreateBinary()` (빈 상태) |
| 한 바이너리 세마포어를 ISR 두 개가 공유하고 "마지막 원인" 변수 하나로 구분 | 같은 틱에 두 ISR 이면 앞 원인이 덮인다 (연습 4) | 원인별 비트를 원자 OR, 태스크가 exchange — 이벤트 그룹/태스크 알림(RTOS-B13, RTOS-M01) |

## 8. 면접 Q&A

<details>
<summary>Q1. ISR 에서 직접 처리하지 않고 세마포어로 태스크에 넘기는 이유는? 바이너리 세마포어는 뮤텍스와 뭐가 다른가요?</summary>

ISR 이 길면 같거나 낮은 우선순위의 다른 인터럽트가 그만큼 늦고(지연·지터), ISR 안에서는 막히는 일(플래시 쓰기, 큐 대기, `printf`)을 못 한다. 그래서 ISR 은 하드웨어를 달래는 최소한(DR 읽기, 플래그 클리어)만 하고 신호를 준다 — 지연 처리(deferred interrupt processing). 처리 태스크는 우선순위로 급한 정도를 정할 수 있고 RTOS API 를 다 쓸 수 있다. 바이너리 세마포어는 **소유자가 없는 신호** 다: give 하는 쪽(ISR)과 take 하는 쪽(태스크)이 다르고, take 한 쪽이 돌려주지 않는다. 뮤텍스는 잡은 태스크가 풀어야 하는 **소유권** 과 우선순위 상속이 있고 그래서 ISR 에서 못 쓴다 (`queue.c:1360` 의 configASSERT — 소유자가 있는 뮤텍스는 ISR 에서 give 불가). 상세 비교는 RTOS-B10.

</details>

<details>
<summary>Q2. 처리 태스크가 바쁜데 같은 인터럽트가 세 번 오면 바이너리 세마포어에서 어떻게 되나요? 어떻게 고치나요?</summary>

첫 give 가 count 를 0→1, 나머지 두 번은 count 가 이미 1 이라 `errQUEUE_FULL` — 태스크는 나중에 take 1번만 성공하므로 이벤트 3개가 1개로 합쳐진다 (Part B 실측: 5개 중 2개 유실, FreeRTOS 실측도 `ooxxo`). NVIC 펜딩 비트가 같은 IRQ 를 합치는 것(C-M14)과 같은 정보량 문제다. 고치는 법은 "무엇을 잃으면 안 되나" 에 따라: ① 횟수가 중요 → 카운팅 세마포어(max ≥ 최악 버스트) + give 실패 카운터, ② 데이터가 링에 따로 있음 → 바이너리 그대로 두고 태스크가 링을 **드레인**, ③ 이벤트마다 데이터 → 큐. 꼬리: "카운팅이면 완전히 해결?" → 아니다. 평균 처리 시간이 평균 간격보다 길면 count 가 계속 쌓여 결국 max 에서 잃는다 (P sweep). 처리율 문제는 세마포어가 아니라 설계로 푼다.

</details>

<details>
<summary>Q3. xHigherPriorityTaskWoken 은 정확히 언제 pdTRUE 가 되나요? 무시하면?</summary>

give 가 기다리던 태스크를 깨웠고, 그 태스크의 우선순위가 **ISR 이 끼어든 태스크(지금 실행 중이던 태스크)보다 높을 때** (`tasks.c:5555`). 같은 우선순위면 false 다. FromISR 함수는 이 값을 true 로만 쓰므로 호출자가 pdFALSE 로 초기화하고, 한 ISR 에서 여러 번 FromISR 를 불러도 변수 하나로 OR 처럼 모은 뒤 마지막에 `portYIELD_FROM_ISR(woken)` 한 번. 무시하면 깨어난 고우선순위 태스크가 ISR 복귀 후에도 낮은 태스크 뒤에서 기다린다. FreeRTOS 는 `xYieldPendings` 로 기록해 두었다가 다음 틱 등에서 전환하므로 영원히는 아니지만 최대 1 틱 지연이 생긴다 (Part C 실측: 5 → 6 틱). Cortex-M 에서는 PENDSVSET 을 세우는 것이고, 실제 전환은 모든 ISR 이 끝난 뒤 가장 낮은 우선순위의 PendSV 에서 일어난다.

</details>

<details>
<summary>Q4. give 는 왜 막히지 않게 설계되어 있나요? 태스크에서 xSemaphoreGive 를 부르면 막힐 수 있나요?</summary>

ISR 은 태스크가 아니라 대기 리스트에 올릴 TCB 도, 잠들 스택 문맥도 없다. 막히면 그 ISR 아래의 모든 것(태스크 전체, 같은/낮은 우선순위 인터럽트)이 멈춘다. 그래서 FromISR 판은 임계 구역 안에서 count 를 보고 성공 아니면 `errQUEUE_FULL` 로 즉시 끝난다 (`queue.c:1388`, 1498). 태스크용 `xSemaphoreGive` 도 `xQueueGenericSend(..., semGIVE_BLOCK_TIME, ...)` 이고 그 값이 0 (`semphr.h:42`, 460) 이라 꽉 찼으면 즉시 실패한다 — 데모 실측 `give, give -> 1, 0`. 세마포어에서 기다리는 쪽은 언제나 take 다.

</details>

<details>
<summary>Q5. UART 수신처럼 바이트가 많이 오는데 바이너리 세마포어를 써도 되나요?</summary>

된다, 단 태스크가 **깨어날 때마다 링을 빌 때까지 처리** 한다면. 이때 세마포어는 "링에 뭔가 있다, 가서 봐라" 라는 1비트이고 바이트 수는 링의 head/tail 이 정확히 안다. 신호가 몇 번 합쳐져도 데이터는 안 잃는다 (Part C: 합쳐진 give 3번, 프레임 정상). 대신 드레인 중 들어온 신호 때문에 "깨었는데 비어 있음" 이 생기므로 그걸 정상 처리해야 한다. 반대로 take 1번에 1바이트만 처리하면 바이너리에서는 바이트가 링에 갇힌다 (실측 3바이트). 데이터 유실은 세마포어가 아니라 **링 overrun** 에서 생기므로 ISR 이 overrun 을 세고(UART ORE 도 확인), 링 크기는 "최악 지연 × 보레이트" 로 정한다. 요즘 FreeRTOS 에서는 이 용도로 바이너리 세마포어보다 태스크 알림(RTOS-M01)이 가볍다 (`semphr.h:116-117` 의 권고).

</details>

<details>
<summary>Q6. 처리 태스크가 xSemaphoreTake(s, pdMS_TO_TICKS(100)) 로 기다리다 pdFALSE 를 받았습니다. 무엇을 해야 하나요?</summary>

100 ms 동안 ISR 이 한 번도 give 하지 않았다는 뜻 — 센서/주변장치가 멈췄거나, 인터럽트가 꺼졌거나(NVIC 설정, 클럭 게이팅), 케이블이 빠졌다. 이것이 타임아웃 take 의 주 용도인 **워치독** 이다 (연습 3: 마지막 처리 후 정확히 10 틱 뒤 감지). 실무: 장애 카운터를 올리고, 주변장치를 재초기화하거나 상태를 보고, 다시 기다린다. 무한 대기(portMAX_DELAY)는 "이 태스크는 이벤트 없으면 영원히 자도 된다" 일 때만. 꼬리: "타임아웃과 give 가 같은 틱에 오면?" → FreeRTOS 는 깨어난 뒤 루프에서 count 를 다시 보므로 count 가 있으면 다시 가져간다(`queue.c:1828-1836` 의 "Timed out" 분기: count 가 0 일 때만 `errQUEUE_EMPTY`, 아니면 루프 처음으로). 이 모델은 ① 틱 ISR(타임아웃) → ③ 장치 ISR 순서라 타임아웃이 먼저 처리되고, 태스크는 그 틱 ④ 에서 다시 take 해 신호를 받는다.

</details>

## 9. 추가 연습

### 연습 1 — 처리 시간 P 와 유실 수의 공식

주기 E = 3 틱으로 이벤트 12개(0, 3, …, 33 틱), 처리 P = 1..8 틱. 바이너리 세마포어가 처리하는 수를 공식 `expected_binary(n, e, p)` 로 쓰고 시뮬레이션과 맞춰 보라. 카운팅(16)은?

<details>
<summary>답 보기</summary>

P ≤ E 이면 전부(12). P > E 이면 처리 태스크는 0 틱부터 쉬지 않고 돌며 P, 2P, 3P … 틱마다 "그 P 틱 동안 온 신호 1개" 를 가져간다. 마지막 이벤트 L = (N−1)E 를 포함하는 구간까지 세면 `1 + ceil((N−1)·E / P)`. E = 3, N = 12 → P=4: 1+ceil(33/4)=10, P=5: 8, P=6: 7, P=7: 6, P=8: 6 — practice 출력의 `formula` 칸과 `binary` 칸이 모든 P 에서 같고, 5. 풀이의 FreeRTOS 실측 표와도 같다. 카운팅은 늘 12 — 대신 마지막 이벤트가 처리되는 시점은 P=8 이면 88 틱 (33 틱에 도착, 55 틱 지연). `처리 + give 실패 == 12` 도 모든 P 에서 성립한다 (유실은 ISR 쪽에서 정확히 셀 수 있다).

</details>

### 연습 2 — 카운팅 + 드레인

Part C 의 `"PING\n"` 를 카운팅(16) 세마포어 + 드레인 파서로 받으면 빈 깨어남은 몇 번인가? 왜 이 조합이 나쁜가?

<details>
<summary>답 보기</summary>

4번. give 5번이 모두 성공해 count 가 쌓이는데, 파서는 첫 깨어남에서 5바이트를 다 비운다. 남은 count 4 는 각각 "깨었는데 링이 빔" 이 된다. 데이터는 안 잃지만 깨어날 때마다 컨텍스트 스위치 2번씩 낭비하고, 로그에 "spurious wake" 경고를 찍는 코드라면 시끄럽다. 짝을 맞춘다: 드레인이면 바이너리(빈 깨어남은 많아야 1번 — practice 에서 `<= 1` 확인), 1개씩이면 카운팅.

</details>

### 연습 3 — 타임아웃으로 센서 멈춤 감지

센서 ISR 이 0, 4, …, 20 틱에 오고 그 뒤 멈춘다. 처리 1 틱, take 타임아웃 10 틱. 첫 타임아웃은 몇 틱인가?

<details>
<summary>답 보기</summary>

31 틱. 20 틱 이벤트 처리가 21 틱에 끝나고, 21 틱에 다시 take(타임아웃 10) → deadline 31. 마지막 **이벤트**(20) 기준이 아니라 마지막 **take** 기준 10 틱이라는 점이 중요하다 — "센서가 10 틱 조용하면" 을 정확히 원한다면 처리 시간만큼 오차가 있다. 정밀하게 하려면 ISR 이 타임스탬프를 남기고 태스크가 `xTaskGetTickCount() - last_ts` 로 판단한다. practice 는 `first_timeout == 31`, 40 틱 안에 타임아웃 1번을 확인한다.

</details>

### 연습 4 — ISR 두 개가 바이너리 세마포어 하나를 공유

DMA 완료 ISR 과 UART 에러 ISR 이 같은 세마포어를 give 하고, "원인" 은 전역 변수 `g_last_src` 하나에 적는다. 두 ISR 이 같은 틱에 오면? 고쳐라.

<details>
<summary>답 보기</summary>

태스크는 1번 깨고 `g_last_src` 는 나중 ISR(UART 에러)의 값이라 **DMA 완료가 사라진다** (practice: `dma=0, err=1`). 고침: 원인별 비트를 ISR 이 원자적으로 OR (`atomic_fetch_or(&g_evbits, EV_DMA)`), 태스크는 깨어나면 `atomic_exchange(&g_evbits, 0)` 로 한 번에 가져가 비트마다 처리 → 둘 다 처리. 단 비트도 "몇 번" 은 모른다 (DMA 두 번 → 처리 1번 확인). Cortex-M0/M0+ 처럼 LDREX/STREX 가 없으면 `atomic_fetch_or` 가 라이브러리 호출(보통 인터럽트 잠깐 끄기)이 된다 — C-M14 Part A. FreeRTOS 에서는 이 일을 이벤트 그룹(`xEventGroupSetBitsFromISR`, RTOS-B13) 이나 태스크 알림의 `eSetBits`(RTOS-M01) 가 해 준다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice   # 34/34 passed
```

## 10. 핵심 정리

- ISR 은 짧게: DR 읽기·플래그 클리어 후 `xSemaphoreGiveFromISR(s, &woken)` + `portYIELD_FROM_ISR(woken)`. 일은 태스크가 `xSemaphoreTake` 로 기다렸다가 한다.
- **give 는 절대 안 막힌다** (꽉 차면 즉시 `errQUEUE_FULL`, 태스크의 `xSemaphoreGive` 도 block time 0). **기다리는 건 언제나 take** — 그래서 ISR 에서 take 는 불가.
- 바이너리 = count 0/1 = "1개 이상 왔다". 처리 중 온 이벤트는 합쳐진다 (실측: 1 틱 간격 5개, 처리 3 틱 → 3개 처리, `ooxxo`). 카운팅은 횟수를 지키지만 max 에서 포화하고, 처리율 부족은 못 고친다.
- P ≤ E 면 바이너리로 충분, P > E 면 바이너리는 `1 + ceil((N−1)E/P)` 개만 처리. 모델과 FreeRTOS POSIX 포트가 같은 숫자.
- `woken` 은 깨운 태스크가 **끼어든 태스크보다 높을 때만** true, 초기화는 호출자, false 로 안 되돌림. yield 를 빼먹으면 최대 1 틱 늦는다 (PendSV 는 ISR 이 다 끝난 뒤).
- 데이터가 링에 따로 있으면 바이너리 + 드레인 (빈 깨어남 허용), 이벤트 자체가 데이터면 카운팅 + 1개씩. give 실패와 링 overrun 은 반드시 센다.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (`~/.esn_scratch/FreeRTOS-Kernel`, <https://github.com/FreeRTOS/FreeRTOS-Kernel>): `include/semphr.h` (42, 129-134, 167, 298, 460, 570-576, 640, 1025), `queue.c` (`xQueueCreateCountingSemaphore` 912, `xQueueGiveFromISR` 1338-1505, `xQueueSemaphoreTake` 1659-1880, Timed out 분기 1828), `tasks.c` (`xTaskRemoveFromEventList` 5500, 5555-5564; 틱 훅 5000-5013; `xYieldPendings` 확인 5020), `include/projdefs.h` 61-62, `portable/ThirdParty/GCC/Posix/port.c` (396, 406-421, 452-516), `portable/GCC/ARM_CM4F/portmacro.h` 88-114
- FreeRTOS 문서 — Binary Semaphores / `xSemaphoreGiveFromISR` / Deferred interrupt processing — <https://www.freertos.org/Embedded-RTOS-Binary-Semaphores.html>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Interrupt Management 장 (지연 처리, 바이너리/카운팅 세마포어) — <https://www.freertos.org/Documentation/RTOS_book.html>
- Zephyr — Semaphores (`k_sem_give`, `k_sem_take` 반환값) — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/semaphores.html>
- CMSIS-RTOS2 — `osSemaphoreRelease` / `osSemaphoreAcquire` — <https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__SemaphoreMgmt.html>
- ARMv7-M Architecture Reference Manual (DDI0403E) — B1.5 예외 모델, B3.2.4 ICSR(PENDSVSET) — <https://developer.arm.com/documentation/ddi0403/latest/>
- 관련 레슨: C-M14 (ISR 공유 데이터, 펜딩 비트 합쳐짐, 플래그 vs 카운터), OS-B06 (조건 변수 — 세마포어와 달리 상태를 기억 못 함), RTOS-B09 (카운팅 세마포어 대기 리스트), RTOS-B10 (뮤텍스 vs 세마포어), RTOS-B11 (큐), RTOS-B13 (이벤트 그룹), RTOS-M01 (태스크 알림), RTOS-M02 (ISR 지연 처리·yield), BM-M04 (인터럽트 구동 UART), DS-A01 (SPSC 링)
