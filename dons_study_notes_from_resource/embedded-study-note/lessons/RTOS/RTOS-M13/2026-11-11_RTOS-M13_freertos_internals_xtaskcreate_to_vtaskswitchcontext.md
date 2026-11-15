---
id: RTOS-M13
time: 95분
updated: 2026-11-11
---
# RTOS-M13 FreeRTOS 내부 읽기: xTaskCreate부터 vTaskSwitchContext까지

## 학습 목표

- FreeRTOS-Kernel V11.3.2 에서 `xTaskCreate → prvCreateTask → prvInitialiseNewTask → prvAddNewTaskToReadyList → prvAddTaskToReadyList`, `xTaskIncrementTick`, `vTaskSwitchContext → taskSELECT_HIGHEST_PRIORITY_TASK` 경로를 **file:line 으로** 따라가고, 호출 다이어그램으로 설명할 수 있다.
- `list.c` 의 의미론 — 보초 `xListEnd`(값 `portMAX_DELAY`), 같은 값 뒤에 넣는 `vListInsert`, `pxIndex` **앞에** 넣는 `vListInsertEnd`, 보초를 건너뛰는 `listGET_OWNER_OF_NEXT_ENTRY`, `pxIndex` 를 고치는 `uxListRemove` — 을 직접 구현하고 미묘한 경우까지 테스트로 증명한다.
- 그 리스트 위에 ready 리스트·지연 리스트(overflow 교환)·`xNextTaskUnblockTime`·time slicing 을 얹은 모델이 **진짜 커널이 찍은 trace 와 글자 하나까지 같다** 는 것을 보인다 (POSIX 포트 + trace 매크로).
- 우리 미니 커널(RTOS-B02/B04/B06)과 FreeRTOS 의 차이 다섯 가지 이상을 코드 근거로 말한다: 리스트 항목 두 개, `pxIndex` 라운드 로빈, `uxTopReadyPriority` 힌트, 스케줄러 정지와 `xPendedTicks`, 임계 구역 중첩, `xYieldPendings`.

## 1. 문제

"FreeRTOS 소스 읽어 봤어요? `xTaskCreate` 부터 첫 컨텍스트 스위치까지 무슨 일이 일어나는지 말해 보세요." — API 를 **써 본** 사람과 **아는** 사람을 가르는 시니어 펌웨어 면접 단골 질문이다. 하드폴트가 `vListInsert` 의 `for` 루프에서 멈춰 있을 때 왜 거기인지 아는 것도 같은 지식에서 나온다.

지도부터 보자. 세 개의 문맥(태스크, tick ISR, PendSV)이 같은 리스트들을 고친다.

```svg
<svg viewBox="0 0 760 440" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="14" font-weight="bold">FreeRTOS V11.3.2 핵심 경로 (tasks.c 줄 번호, 단일 코어, ARM_CM4F 포트 기준)</text>
  <text x="14" y="42" fill="currentColor" font-weight="bold">① 생성 (태스크 문맥)</text>
  <text x="259" y="42" fill="currentColor" font-weight="bold">② tick (SysTick ISR)</text>
  <text x="499" y="42" fill="currentColor" font-weight="bold">③ 스위치 (PendSV, 최저 우선순위)</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="235" height="40" rx="5" class="f-mut"/>
    <rect x="10" y="102" width="235" height="40" rx="5" class="f-mut"/>
    <rect x="10" y="154" width="235" height="40" rx="5" class="f-mut"/>
    <rect x="10" y="206" width="235" height="40" rx="5" class="f-acc"/>
    <rect x="10" y="258" width="235" height="40" rx="5" class="f-acc"/>
    <rect x="255" y="50" width="230" height="40" rx="5" class="f-warn"/>
    <rect x="255" y="102" width="230" height="40" rx="5" class="f-acc"/>
    <rect x="255" y="154" width="230" height="40" rx="5" class="f-mut"/>
    <rect x="255" y="206" width="230" height="40" rx="5" class="f-mut"/>
    <rect x="255" y="258" width="230" height="40" rx="5" class="f-mut"/>
    <rect x="495" y="50" width="255" height="40" rx="5" class="f-warn"/>
    <rect x="495" y="102" width="255" height="40" rx="5" class="f-acc"/>
    <rect x="495" y="154" width="255" height="40" rx="5" class="f-acc"/>
    <rect x="495" y="206" width="255" height="40" rx="5" class="f-mut"/>
    <rect x="495" y="258" width="255" height="40" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="18" y="67">xTaskCreate  1729</text>
    <text x="18" y="119">prvCreateTask  1631</text>
    <text x="18" y="171">prvInitialiseNewTask  1804</text>
    <text x="18" y="223">prvAddNewTaskToReadyList 2040</text>
    <text x="18" y="275">prvAddTaskToReadyList  287</text>
    <text x="263" y="67">xPortSysTickHandler port.c:560</text>
    <text x="263" y="119">xTaskIncrementTick  4831</text>
    <text x="263" y="171">taskSWITCH_DELAYED_LISTS 267</text>
    <text x="263" y="223">delayed 머리 검사 4871-4959</text>
    <text x="263" y="275">time slicing  4971</text>
    <text x="503" y="67">xPortPendSVHandler port.c:504</text>
    <text x="503" y="119">vTaskSwitchContext  5215</text>
    <text x="503" y="171">taskSELECT_HIGHEST_PRIORITY_TASK</text>
    <text x="503" y="223">listGET_OWNER_OF_NEXT_ENTRY</text>
    <text x="503" y="275">pxCurrentTCB 교체 → 복원</text>
  </g>
  <g fill="currentColor">
    <text x="18" y="84">→ prvCreateTask, 성공하면 다음 단계</text>
    <text x="18" y="136">스택 + TCB pvPortMalloc, memset 0</text>
    <text x="18" y="188">항목 2개, event 값 = MAX-prio</text>
    <text x="18" y="240">임계 구역 + pxCurrentTCB 선택</text>
    <text x="18" y="292">RECORD_READY + listINSERT_END</text>
    <text x="263" y="84">→ pdTRUE 면 PENDSVSET (576)</text>
    <text x="263" y="136">xTickCount++ (정지 중이면 pended)</text>
    <text x="263" y="188">tick == 0 이면 리스트 두 개 교환</text>
    <text x="263" y="240">xNextTaskUnblockTime 조기 종료</text>
    <text x="263" y="292">같은 prio 2개 이상 → 스위치</text>
    <text x="503" y="84">r4-r11, lr (+s16-s31) 저장</text>
    <text x="503" y="136">정지 중이면 xYieldPendings 만</text>
    <text x="503" y="188">generic 197 / 비트맵+CLZ 238</text>
    <text x="503" y="240">list.h 286: 보초 건너뛰는 라운드 로빈</text>
    <text x="503" y="292">새 TCB 의 pxTopOfStack 에서 pop</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M127 90 V102 M127 142 V154 M127 194 V206 M127 246 V258"/>
    <path d="M370 90 V102 M370 142 V154 M370 194 V206 M370 246 V258"/>
    <path d="M622 90 V102 M622 142 V154 M622 194 V206 M622 246 V258"/>
    <path d="M485 70 H495"/>
    <path d="M255 226 C 250 226, 250 278, 245 278" stroke-dasharray="4 3"/>
  </g>
  <text x="252" y="324" fill="currentColor">점선: 깨운 태스크도 같은 prvAddTaskToReadyList (매크로) 로 들어간다</text>
  <rect x="10" y="336" width="740" height="96" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="356" fill="currentColor" font-weight="bold">④ 블록·깨움 (태스크 문맥, queue.c)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="376">vTaskDelay 2506 → prvAddCurrentTaskToDelayedList 8685 → vListInsert → yield</text>
    <text x="20" y="396">xQueueReceive queue.c:1509 → vTaskSuspendAll → vTaskPlaceOnEventList 5402 (+ 지연 리스트)</text>
    <text x="20" y="416">xQueueGenericSend queue.c:949 → xTaskRemoveFromEventList 5500 → queueYIELD_IF_USING_PREEMPTION</text>
  </g>
</svg>
```

호스트(맥)에서는 이 경로를 두 가지로 확인한다. ① 진짜 커널(POSIX 포트)에 **trace 매크로** 를 꽂아 실제 호출 순서를 찍는다 (`demo_freertos_trace.c`, 커널 소스 한 줄도 안 고침). ② 같은 의미론을 `list.c` 부터 직접 짠 **모델** 이 같은 시나리오에서 같은 trace 를 내는지 검사한다. ②가 체크 대상 코딩 문제다.

| 읽는 순서 | 파일:줄 (V11.3.2) | 볼 것 |
|---|---|---|
| 1 | `include/list.h` 144–179 | `ListItem_t` 5필드, `List_t` = 개수 + `pxIndex` + 보초 `xListEnd` |
| 2 | `list.c` 49–247 | 초기화·두 가지 삽입·제거 — 전부 합쳐 100줄 남짓 |
| 3 | `tasks.c` 377–455 | TCB: `xStateListItem`, `xEventListItem` (389–390) |
| 4 | `tasks.c` 478–513 | ready 리스트 배열, 지연 리스트 두 개, `uxTopReadyPriority`, `xPendedTicks`, `xYieldPendings`, `xNextTaskUnblockTime` |
| 5 | `tasks.c` 1729 → 1631 → 1804 → 2040 → 287 | 생성 경로 |
| 6 | `tasks.c` 4831–5078 | tick |
| 7 | `tasks.c` 5215–5298, 178–261 | 스위치와 선택 매크로 |
| 8 | `portable/GCC/ARM_CM4F/port.c` 475–584 | 임계 구역, PendSV, SysTick |

### Part A — list.c 모델

`fl_list_t`/`fl_item_t` 로 `list_init`, `list_insert_end`, `list_insert`, `list_remove`, `list_next_owner` 를 채운다. 테스트가 확인하는 미묘한 점: 같은 값은 **뒤에**, 값이 `MAX_DELAY` 면 루프 없이 맨 끝에, `insert_end` 는 **index 앞에** (새로 준비된 태스크는 라운드 로빈 줄의 맨 뒤), index 항목을 빼면 index 가 **앞 항목으로** 물러남, 보초는 절대 owner 로 나오지 않음. 마지막으로 무작위 2000 연산을 배열 기준 모델과 비교한다.

### Part B — tasks.c 스케줄러 핵심

같은 리스트 위에 `add_to_ready` (prvAddTaskToReadyList), `select_highest` (generic + port-optimised), `add_current_to_delayed` (overflow 리스트, `xNextTaskUnblockTime`), `increment_tick` (wrap 교환, 조기 종료, 이벤트 리스트에서도 제거, 선점, time slicing)을 채운다.

### Part C — 모터 제어 펌웨어 부팅: 모델 trace == 커널 trace

STM32 모터 컨트롤러의 흔한 구조를 그대로 쓴다. 최고 우선순위 `Boot` 가 초기화 후 `Ctrl`(모터 제어, 4 tick 주기, prio 3), `Comm`(통신 폴링, 3 tick, prio 2), 로거 `LogA`/`LogB`(바쁜 루프, prio 1)를 만들고 자신은 `vTaskSuspend(NULL)`. tick 카운터는 wrap 직전(`configINITIAL_TICK_COUNT = -30`)에서 시작해 실행 중 0 을 지난다. `add_new_task` (prvAddNewTaskToReadyList)를 채우면 주어진 실행기가 시나리오를 재생하고, 결과 trace 가 진짜 커널 출력(`KERNEL_TRACE`, 붙여 넣은 상수)과 같아야 한다 — generic 선택과 비트맵 선택 둘 다.

## 2. Problem (English)

> **Re-implement the core of FreeRTOS's scheduler from its data structures up.**
>
> 1. *Lists (list.c semantics).* A list is a circular doubly linked list with an end marker whose value is `portMAX_DELAY`, an item count, and an index pointer. Implement `list_init`, `list_insert_end` (insert *before* the index so the new item is returned last by round-robin), `list_insert` (ascending by value, after existing equal values; a `MAX_DELAY` value goes straight to the tail), `list_remove` (returns the remaining count; if the removed item was the index, move the index back to its predecessor), and `list_next_owner` (advance the index, skip the end marker, return the owner).
> 2. *Scheduler.* Per-priority ready lists, two delayed lists (current / overflow) and a suspended list. Implement `add_to_ready` (record the priority, insert at end), `select_highest` (generic: walk down from `uxTopReadyPriority`, which is only an upper bound; optimised: `31 - clz(bitmap)`), `add_current_to_delayed(ticks, can_block_indefinitely)`, and `increment_tick()` returning whether a switch is required (swap lists on wrap to 0, early exit via `xNextTaskUnblockTime`, remove timed-out tasks from both their state and event lists, preempt only on strictly higher priority, time-slice when the running task's ready list has more than one task).
> 3. *Task creation.* `add_new_task`: before the scheduler starts, the current task becomes the new task if its priority is `>=` the current one; after it starts, yield only if the new task is strictly higher.
>
> Example: ready list `[A,B,C]`, round-robin returned `B`, then `D` is readied → physical order `A D B C`, round-robin continues `C, A, D, B`. Tick count `0xFFFFFFFE`, delay 5 → wake tick 3 in the overflow list.
>
> Follow-ups: Why two list items per TCB? Why is the generic `uxTopReadyPriority` allowed to be stale? What happens if `uxListRemove` forgets the index fix? Why does the tick ISR pend PendSV instead of switching?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/RTOS-M13_st && /tmp/RTOS-M13_st
# 처음엔 "6/60 passed", exit 1 — TODO A1–A5, B1–B4, C1 을 채워 60/60
```

주어진 것: 불변식 검사 `list_check`, `trace_ev`(데모와 같은 출력 형식), `clz32`, `prv_reset_next_unblock`, `switch_context`, `task_delay`, `task_suspend_self`, `k_start`, 스크립트 실행기 `run_current`/`k_run`, 시나리오와 `KERNEL_TRACE`. starter 는 스텁이라도 멈추거나 죽지 않는다(실행기에 tick·걸음 상한, 리스트 순회에 NULL·길이 가드).

진짜 커널 데모 (체크 대상 아님, `~/.esn_scratch/FreeRTOS-Kernel` V11.3.2 필요):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_trace.c \
   $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M13_dt && /tmp/RTOS-M13_dt
```

레슨 폴더의 `FreeRTOSConfig.h` 가 trace 매크로 9개를 정의한다 (커널 파일은 경고 0개로 빌드됐다).

## 4. 힌트

<details>
<summary>힌트 1 — 보초가 있으면 빈 리스트도 "원형" 이다</summary>

빈 리스트에서 `end.next == end.prev == &end`. 그래서 삽입·제거에 "머리가 NULL 인가" 분기가 하나도 없다. `insert_end` 는 "index 와 index->prev 사이" 에 끼우는 4줄, `insert` 는 "p 와 p->next 사이" 에 끼우는 4줄이다.

</details>

<details>
<summary>힌트 2 — vListInsert 의 루프 조건은 &lt;= 다</summary>

`for (p = &end; p->next->value <= v; p = p->next)` — 같은 값을 **지나쳐서** 그 뒤에 넣는다(안정성). 그런데 보초 값도 `MAX_DELAY` 라서 `v == MAX_DELAY` 면 보초도 지나쳐 영원히 돈다. 그래서 그 경우만 `p = end.prev` 로 바로 꼬리를 잡는다 (list.c 161–164).

</details>

<details>
<summary>힌트 3 — index 는 "마지막으로 돌려준 것"</summary>

`list_next_owner` 는 index 를 **먼저** 한 칸 옮기고 돌려준다. 그러니 index 바로 앞(`index->prev` 뒤)에 넣은 항목은 한 바퀴를 다 돈 뒤에야 나온다. 제거할 때 index 를 앞 항목으로 물리면 다음 호출이 "뺀 항목의 다음" 을 돌려준다 — 순서가 끊기지 않는다.

</details>

<details>
<summary>힌트 4 — generic 선택에서 top_ready 는 "내리지 않는다"</summary>

`add_to_ready` 는 더 클 때만 올린다. 블록·삭제 경로는 generic 에서 아무것도 안 한다(`portRESET_READY_PRIORITY` 가 빈 매크로, tasks.c 225). 내리는 곳은 선택 매크로 하나뿐: 빈 리스트를 내려가며 찾은 뒤 `uxTopReadyPriority = uxTopPriority` (211).

</details>

<details>
<summary>힌트 5 — increment_tick 의 순서</summary>

tick 증가 → (0 이면 교환) → `now >= next_unblock` 일 때만 지연 리스트 머리부터: 비었으면 MAX, 아직이면 그 값 저장 후 break, 됐으면 두 리스트에서 빼고 ready 로. 그다음 time slicing, 그다음 `yield_pending`. 깨운 태스크와 **같은** 우선순위면 선점 조건(`>`)은 거짓이지만, 같은 리스트에 둘이 되니 time slicing 이 참이 된다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 대응표

| 모델 (이 레슨) | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 |
|---|---|---|---|
| `list_insert_end` / `list_insert` / `list_remove` | `vListInsertEnd` / `vListInsert` / `uxListRemove` | `sys_dlist_append` 등 (커널은 우선순위 큐를 따로 둠) | — |
| `add_new_task` (`k_create`) | `xTaskCreate` → `prvAddNewTaskToReadyList` | `k_thread_create` | `osThreadNew` |
| `task_delay` | `vTaskDelay` | `k_sleep` / `k_msleep` | `osDelay` |
| `increment_tick` | `xTaskIncrementTick` (SysTick 에서) | `sys_clock_announce` | — (`osKernelGetTickCount` 는 읽기) |
| `switch_context` | `vTaskSwitchContext` (PendSV 에서) | 아키텍처 스위치 코드 | — |
| `task_suspend_self` | `vTaskSuspend(NULL)` | `k_thread_suspend(k_current_get())` | `osThreadSuspend(osThreadGetId())` |
| `suspend_all` / `resume_all` (연습 3) | `vTaskSuspendAll` / `xTaskResumeAll` | `k_sched_lock` / `k_sched_unlock` | `osKernelLock` / `osKernelUnlock` |
| `place_on_event_list` (연습 1) | `vTaskPlaceOnEventList` | (커널 객체 wait_q) | — |

### 읽기 경로 — 생성

`xTaskCreate` (tasks.c 1729–1763)는 두 줄짜리다: `prvCreateTask` 로 메모리를 만들고, 성공하면 `prvAddNewTaskToReadyList`. `prvCreateTask` (1631–1726)는 스택이 아래로 자라면(`portSTACK_GROWTH < 0`, Cortex-M) **스택을 먼저, TCB 를 나중에** 할당한다 — 스택이 넘쳐도 자기 TCB 를 먼저 덮지 않게 (1640–1642 주석). TCB 는 `memset 0`.

`prvInitialiseNewTask` (1804–2035)의 리스트 부분이 이 레슨의 핵심이다:

```c
pxNewTCB->uxPriority = uxPriority;                                              /* 1914 */
vListInitialiseItem( &( pxNewTCB->xStateListItem ) );                          /* 1921 */
vListInitialiseItem( &( pxNewTCB->xEventListItem ) );                          /* 1922 */
listSET_LIST_ITEM_OWNER( &( pxNewTCB->xStateListItem ), pxNewTCB );            /* 1926 */
/* Event lists are always in priority order. */
listSET_LIST_ITEM_VALUE( &( pxNewTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxPriority );  /* 1929 */
```

그리고 `pxPortInitialiseStack` (1996)이 "방금 인터럽트당한 것처럼" 보이는 가짜 예외 프레임을 쌓는다 (PC = 태스크 함수, RTOS-A01 이 다룬다). 이름 복사(1876–1895), 우선순위 클램프(1903–1908)도 여기다.

`prvAddNewTaskToReadyList` (2040–2114) 는 임계 구역 안에서:

```c
if( pxCurrentTCB == NULL ) { pxCurrentTCB = pxNewTCB;                           /* 2048–2052 */
    if( uxCurrentNumberOfTasks == ( UBaseType_t ) 1 ) prvInitialiseTaskLists(); } /* 첫 태스크: 리스트 초기화 */
else if( xSchedulerRunning == pdFALSE ) {
    if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority ) pxCurrentTCB = pxNewTCB; } /* 2071–2075 */
traceTASK_CREATE( pxNewTCB );                                                   /* 2096 */
prvAddTaskToReadyList( pxNewTCB );                                              /* 2098 */
/* 임계 구역 밖 */ if( xSchedulerRunning != pdFALSE ) taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxNewTCB ); /* 2104–2108 */
```

`<=` 가 미묘하다: 시작 전에는 **같은 우선순위면 나중에 만든 태스크** 가 첫 태스크다 (테스트 C1: X1·Y3·Z3·W2 → Z). 시작 후의 양보는 `<` (tasks.c 90) — 같은 우선순위를 만들어도 바로 넘어가지 않는다 (U 테스트). 그리고 ready 리스트 삽입은 **매크로** 다:

```c
#define prvAddTaskToReadyList( pxTCB ) do { traceMOVED_TASK_TO_READY_STATE( pxTCB ); \
    taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority ); \
    listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) ); \
    tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB ); } while( 0 )                  /* 287–293 */
```

`listINSERT_END` (list.h 362–385) 는 `vListInsertEnd` 와 같은 코드의 매크로판 — 함수 호출 비용을 아끼려고 커널 내부는 매크로를 쓴다.

### Part A — list.c: 보초 하나, 포인터 넷

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">List_t: 원형 이중 연결 + 보초 xListEnd(값 MAX) + pxIndex</text>
  <text x="10" y="40" fill="currentColor">① [A,B,C], next_owner 가 B 를 돌려준 직후 (index = B, B 실행 중)</text>
  <g stroke="currentColor">
    <rect x="20" y="60" width="110" height="40" rx="5" class="f-mut"/>
    <rect x="170" y="60" width="80" height="40" rx="5" class="f-ok"/>
    <rect x="290" y="60" width="80" height="40" rx="5" class="f-acc"/>
    <rect x="410" y="60" width="80" height="40" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="85" font-family="monospace">xListEnd MAX</text>
    <text x="203" y="85">A</text><text x="323" y="85">B</text><text x="443" y="85">C</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M130 75 H170 M250 75 H290 M370 75 H410"/>
    <path d="M170 90 H130 M290 90 H250 M410 90 H370"/>
    <path d="M490 80 H520 V120 H75 V100"/>
  </g>
  <text x="300" y="56" fill="currentColor" font-family="monospace" font-size="12">pxIndex ▼</text>
  <text x="530" y="85" fill="currentColor">C.next = 보초 (원형)</text>
  <text x="10" y="150" fill="currentColor">② D 가 준비됨 → vListInsertEnd: index(B) "앞" 에 → 물리 순서 A D B C</text>
  <g stroke="currentColor">
    <rect x="20" y="160" width="110" height="36" rx="5" class="f-mut"/>
    <rect x="160" y="160" width="70" height="36" rx="5" class="f-ok"/>
    <rect x="260" y="160" width="70" height="36" rx="5" class="f-warn"/>
    <rect x="360" y="160" width="70" height="36" rx="5" class="f-acc"/>
    <rect x="460" y="160" width="70" height="36" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="183" font-family="monospace">xListEnd</text>
    <text x="190" y="183">A</text><text x="290" y="183">D</text><text x="390" y="183">B</text><text x="490" y="183">C</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M130 178 H160 M230 178 H260 M330 178 H360 M430 178 H460"/></g>
  <text x="545" y="176" fill="currentColor">다음 차례: C → (보초 건너뜀)</text>
  <text x="545" y="194" fill="currentColor">→ A → D → B. D 는 맨 뒤</text>
  <text x="10" y="232" fill="currentColor">③ B 가 블록 → uxListRemove(B): index == B 였으므로 index = B.prev = D</text>
  <g stroke="currentColor">
    <rect x="20" y="244" width="110" height="36" rx="5" class="f-mut"/>
    <rect x="160" y="244" width="70" height="36" rx="5" class="f-ok"/>
    <rect x="260" y="244" width="70" height="36" rx="5" class="f-acc"/>
    <rect x="360" y="244" width="70" height="36" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="267" font-family="monospace">xListEnd</text>
    <text x="190" y="267">A</text><text x="290" y="267">D</text><text x="390" y="267">C</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M130 262 H160 M230 262 H260 M330 262 H360"/></g>
  <text x="445" y="260" fill="currentColor">다음 차례 = C (끊김 없음).</text>
  <text x="445" y="278" fill="currentColor">안 고치면 index 가 지연 리스트로 (연습 2)</text>
  <text x="10" y="318" fill="currentColor">vListInsert(정렬)는 index 를 안 건드린다 — ready 리스트는 InsertEnd, 지연·이벤트 리스트는 Insert 를 쓴다.</text>
</svg>
```

`list.c` 는 주석을 빼면 짧다. 다섯 함수의 핵심 줄:

| 함수 | 핵심 (list.c / list.h V11.3.2) | 테스트 |
|---|---|---|
| `vListInitialise` | `pxIndex = &xListEnd` (56), `xListEnd.xItemValue = portMAX_DELAY` (62), next/prev = 자기 (66–67) | A1 |
| `vListInsertEnd` | `new->pxNext = pxIndex; new->pxPrevious = pxIndex->pxPrevious;` (121–122) → 앞뒤 연결 (127–128) | A4, A6 |
| `vListInsert` | `if (value == portMAX_DELAY) it = xListEnd.pxPrevious;` (161–164), 아니면 `for (...; it->pxNext->xItemValue <= value; ...)` (192) | A2, A3 |
| `uxListRemove` | 앞뒤 잇기 (225–226), `if (pxList->pxIndex == item) pxList->pxIndex = item->pxPrevious;` (232–235), 남은 개수 반환 (246) | A7–A9 |
| `listGET_OWNER_OF_NEXT_ENTRY` | `pxIndex = pxIndex->pxNext; if (pxIndex == &xListEnd) pxIndex = xListEnd.pxNext;` (list.h 291–295) | A5, A9 |

실행 결과의 퍼즈 줄: `Part A fuzz: 2000 ops (534 next_owner), mismatches=0` — 무작위 삽입(값 1·2·2·3·7·7·100·MAX−1·MAX 중)/끝 삽입/제거/라운드 로빈을 배열 기준 모델과 매 연산 비교했고, 순서·index 위치·개수·불변식이 모두 같았다.

두 가지 주의:

- **`configUSE_MINI_LIST_ITEM` 기본값은 1** (FreeRTOS.h 608–609): 보초는 `MiniListItem_t`(값·next·prev 만)라 `pvOwner` 가 없다. 모델은 보초를 완전한 항목으로 두고 owner 를 NULL 로 뒀다. 커널은 빈 리스트에 `listGET_OWNER_OF_NEXT_ENTRY` 를 부르지 않는다(선택 매크로가 빈 리스트를 건너뛴 뒤에만 부른다) — 부르면 보초 뒤 메모리를 owner 로 읽는다.
- `vListInsert` 주석 168–190 은 "여기서 멈추면 원인은 ① 스택 오버플로 ② Cortex-M 인터럽트 우선순위 설정 오류 ③ 임계 구역/스케줄러 정지 중 API 호출이나 ISR 에서 FromISR 아닌 API ④ 초기화 전 객체 사용 ⑤ tick 인터럽트 우선순위" 라고 적어 둔다. 리스트가 깨지면 `<=` 루프가 보초를 못 만나 무한히 돈다 — 하드폴트가 아니라 **멈춤** 으로 보이는 이유다 (Q8).

### Part B — 선택, 지연, tick

**선택: generic 은 "상한 힌트", optimised 는 "정확한 비트맵".**

```c
/* generic, tasks.c 197–212 */
UBaseType_t uxTopPriority = uxTopReadyPriority;
while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) != pdFALSE ) { configASSERT( uxTopPriority ); --uxTopPriority; }
listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );
uxTopReadyPriority = uxTopPriority;
/* optimised, 238–246 + ARM_CM4F portmacro.h 166–171 */
uxTopPriority = ( 31UL - ( uint32_t ) ucPortCountLeadingZeros( ( uxTopReadyPriority ) ) );   /* clz 한 명령 (155) */
```

| | generic (`configUSE_PORT_OPTIMISED_TASK_SELECTION 0`) | optimised (1) |
|---|---|---|
| `uxTopReadyPriority` 의 뜻 | 가장 높은 ready 우선순위의 **상한** | ready 우선순위 **비트맵** |
| 올리기 | `if (prio > top) top = prio` (186–192) | `|= 1 << prio` |
| 내리기 | 선택할 때만 (211) | 리스트가 빌 때마다 `&= ~(1 << prio)` (8708, 253–259) |
| 선택 비용 | 빈 리스트 수만큼 루프 | `clz` 1번, 상수 |
| 제약 | 없음 (POSIX 포트·Cortex-M0 는 이것뿐) | `configMAX_PRIORITIES ≤ 32` (portmacro.h 161–163) |
| 기본값 | FreeRTOS.h 2803–2804 = 0 | ARM_CM4F portmacro.h 144–146 = 1 |

테스트 B: b(3) 가 블록한 직후 generic 은 `top_ready == 3` 그대로이고 `ready[3]` 은 비어 있다. 다음 선택에서 3→2→1 두 칸 내려가 `scan_steps == 2`. optimised 는 블록 순간 비트맵 `0x0A → 0x02`, 선택에서 `scan_steps == 0`. 시나리오 전체로는 generic 이 빈 리스트를 10번 지나쳤다 (`generic: scan_steps=10`).

**지연: `prvAddCurrentTaskToDelayedList` (8685–8760).** 상태 항목은 하나라서 ready 리스트에서 **먼저 빼고**(8704) 지연 리스트에 넣는다. `wake = tick + ticks` 가 wrap 하면(`wake < tick`, 8734) overflow 리스트로 — 이때 `xNextTaskUnblockTime` 은 건드리지 않는다(현재 리스트 기준 값이니까). 테스트 B3 의 두 `MAX_DELAY`:

| 호출 | `xCanBlockIndefinitely` | 결과 |
|---|---|---|
| `xQueueReceive(q, &x, portMAX_DELAY)` → `vTaskPlaceOnEventList` (5425) | pdTRUE | suspended 리스트 = 진짜 무한 대기 (8717–8722) |
| `vTaskDelay(portMAX_DELAY)` (2528) | **pdFALSE** | `100 + 0xFFFFFFFF = 99` → overflow 리스트. 32비트 1 kHz 면 약 49.7일 뒤에 깬다 |

**tick: `xTaskIncrementTick` (4831–5078)** 을 모델로 그대로 옮기면:

```c
const tick_t now = k->tick + 1;  k->tick = now;                         /* 4852–4856 */
if (now == 0) { /* taskSWITCH_DELAYED_LISTS 267–279: delayed 가 비어 있어야(assert), 교환, overflows++, reset */ }
if (now >= k->next_unblock) for (;;) {                                  /* 4871 조기 종료 */
    if (list_empty(k->delayed)) { k->next_unblock = MAX_DELAY; break; } /* 4875–4883 */
    t = list_head_owner(k->delayed);
    if (now < t->state_item.value) { k->next_unblock = t->state_item.value; break; }   /* 4897–4905 */
    list_remove(&t->state_item);
    if (t->event_item.container) list_remove(&t->event_item);           /* 4917–4919: 큐 대기 타임아웃 */
    add_to_ready(k, t);
    if (t->prio > k->current->prio) sw = 1;                             /* 4944: 같으면 아님 */
}
if (list_len(&k->ready[k->current->prio]) > 1) sw = 1;                  /* 4971 time slicing */
if (k->yield_pending) sw = 1;                                           /* 5020 */
```

테스트 B4 의 숫자: tick −3 에서 h(wake −1), m(wake 1 → overflow) 를 걸어 두면 tick −2 는 `walks == 0` (리스트를 보지도 않음), tick −1 은 h 를 깨우고 머리가 빈 것을 보고 끝(`walks == 2`, `next_unblock == MAX`), tick 0 에서 교환 → `next_unblock == 1`, tick 1 에서 m 이 타임아웃되며 **큐의 이벤트 리스트에서도 빠진다** (`list_empty(&evq)`). 교환 직전 지연 리스트가 비어 있지 않으면 `configASSERT` (272) — 정상이면 불가능하다: wrap 전 마지막 tick(0xFFFFFFFF = `portMAX_DELAY`)은 `now >= next_unblock` 이 항상 참이라 남은 항목을 모두 깨운다.

### Part C — 진짜 커널 trace 와 비교

`FreeRTOSConfig.h` 의 trace 매크로 (FreeRTOS.h 에 `#ifndef` 기본값이 있는 훅들 — 631, 729, 737, 741, 841, 857, 877 줄):

```c
#define traceTASK_CREATE( pxNewTCB )              m13_trace( 0, ( pxNewTCB )->pcTaskName, M13_TICK_NOW, 0 )
#define traceMOVED_TASK_TO_READY_STATE( pxTCB )   m13_trace( 1, ( pxTCB )->pcTaskName, M13_TICK_NOW, 0 )
#define traceTASK_SWITCHED_IN()                   m13_trace( 2, pxCurrentTCB->pcTaskName, M13_TICK_NOW, 0 )
#define traceTASK_INCREMENT_TICK( xOld )          m13_trace( 3, NULL, ( long long ) ( TickType_t ) ( ( xOld ) + 1U ), 0 )
#define traceMOVED_TASK_TO_DELAYED_LIST()         m13_trace( 5, pxCurrentTCB->pcTaskName, M13_TICK_NOW, ( long long ) xTimeToWake )
#define traceSTARTING_SCHEDULER( xIdleTaskHandles ) m13_pause()
```

매크로는 tasks.c **안에서** 펼쳐지므로 static 변수 `xTickCount`, `pxCurrentTCB` 와 `prvAddCurrentTaskToDelayedList` 의 지역 변수 `xTimeToWake` 까지 쓸 수 있다. `traceTASK_INCREMENT_TICK` 은 증가 **전** 값을 받는다(4842) — 그래서 +1. `m13_trace` 는 tick 시그널 핸들러 안에서도 불리므로 배열에만 적고, 출력은 끝에 태스크에서 한다.

실제 출력 (`/tmp/RTOS-M13_dt`, Apple M6, Apple clang 21.0.0):

```text
[-30] new:Boot rdy:Boot new:IDLE rdy:IDLE in:Boot
[-6] new:LogA rdy:LogA new:Ctrl rdy:Ctrl new:LogB rdy:LogB new:Comm rdy:Comm susp:Boot in:Ctrl delay:Ctrl dly:Ctrl@-2 in:Comm delay:Comm dly:Comm@-3 in:LogA
[-5] tick in:LogB
[-4] tick in:LogA
[-3] tick rdy:Comm in:Comm delay:Comm ovf:Comm@0 in:LogB
[-2] tick rdy:Ctrl in:Ctrl delay:Ctrl ovf:Ctrl@2 in:LogA
[-1] tick in:LogB
[0] tick rdy:Comm in:Comm delay:Comm dly:Comm@3 in:LogA
[1] tick in:LogB
[2] tick rdy:Ctrl in:Ctrl delay:Ctrl dly:Ctrl@6 in:LogA
[3] tick rdy:Comm in:Comm delay:Comm dly:Comm@6 in:LogB
[4] tick in:LogA
[5] tick in:LogB
[6] tick rdy:Ctrl rdy:Comm in:Ctrl
events=67  LogA/LogB spins > 0: 1 1
```

solution.c 의 모델 출력은 이 14줄과 **바이트 단위로 같다** (`strcmp == 0`, generic 과 optimised 둘 다). 줄마다 읽으면 경로가 보인다:

1. `[-30]`: `xTaskCreate(Boot)` → `new`·`rdy` (2096, 289). `vTaskStartScheduler` 가 IDLE 을 만들고(`4 <= 0` 이 거짓이라 첫 태스크는 Boot 그대로) 3809 의 `traceTASK_SWITCHED_IN` 이 `in:Boot`. 스위치 없이 첫 태스크를 "들어간 것" 으로 찍는다.
2. `[-6]`: 실행 중 생성 — prio 4 Boot 보다 높지 않아 양보 없음. `susp:Boot` → 선택이 4→3 으로 내려가 `in:Ctrl`. Ctrl 의 `vTaskDelay(4)` = `delay` → `dly@-2` → 다음 `in:Comm` → `dly@-3` → prio 1 리스트의 index 가 보초라 첫 항목 `in:LogA`.
3. `[-5]`, `[-4]`: 깨울 것 없음(조기 종료), time slicing 만 → LogB, LogA 번갈아.
4. `[-3]`: Comm 깸 → `rdy` → `in:Comm` → `-3 + 3 = 0` 은 wrap (`0 < -3` 을 부호 없이 비교) → **`ovf`**. 다음 차례는 index(LogA) 다음 = LogB.
5. `[0]`: wrap → 리스트 교환 → `next_unblock` = 새 머리(Comm@0) → 같은 tick 에 Comm 깸. `ovf` 로 들어간 두 태스크가 교환 덕에 제때 깬 것이 trace 로 보인다.
6. `[6]`: Ctrl(@6, tick 2 에 넣음)과 Comm(@6, tick 3 에 넣음)이 같은 값 → `vListInsert` 가 뒤에 넣었으므로 `rdy:Ctrl rdy:Comm` 순서. Ctrl 의 세 번째 깸에서 출력하고 끝.

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">진짜 커널 trace 로 그린 Gantt (tick -6 ~ 6, 칸 = 1 tick, 좁은 막대 = tick 직후 잠깐 실행 후 블록)</text>
  <g fill="currentColor" text-anchor="middle">
    <text x="114" y="40">-6</text><text x="162" y="40">-5</text><text x="210" y="40">-4</text><text x="258" y="40">-3</text>
    <text x="306" y="40">-2</text><text x="354" y="40">-1</text><text x="402" y="40">0</text><text x="450" y="40">1</text>
    <text x="498" y="40">2</text><text x="546" y="40">3</text><text x="594" y="40">4</text><text x="642" y="40">5</text><text x="690" y="40">6</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="64">Boot (4)</text><text x="10" y="94">Ctrl (3)</text><text x="10" y="124">Comm (2)</text>
    <text x="10" y="154">LogA (1)</text><text x="10" y="184">LogB (1)</text>
  </g>
  <g stroke="currentColor">
    <rect x="91" y="50" width="6" height="20" class="f-warn"/>
    <rect x="98" y="80" width="6" height="20" class="f-acc"/>
    <rect x="283" y="80" width="8" height="20" class="f-acc"/>
    <rect x="475" y="80" width="8" height="20" class="f-acc"/>
    <rect x="667" y="80" width="8" height="20" class="f-acc"/>
    <rect x="105" y="110" width="6" height="20" class="f-acc"/>
    <rect x="235" y="110" width="8" height="20" class="f-acc"/>
    <rect x="379" y="110" width="8" height="20" class="f-acc"/>
    <rect x="523" y="110" width="8" height="20" class="f-acc"/>
    <rect x="112" y="140" width="26" height="20" class="f-ok"/>
    <rect x="186" y="140" width="47" height="20" class="f-ok"/>
    <rect x="292" y="140" width="38" height="20" class="f-ok"/>
    <rect x="388" y="140" width="38" height="20" class="f-ok"/>
    <rect x="484" y="140" width="38" height="20" class="f-ok"/>
    <rect x="570" y="140" width="47" height="20" class="f-ok"/>
    <rect x="138" y="170" width="47" height="20" class="f-ok"/>
    <rect x="244" y="170" width="38" height="20" class="f-ok"/>
    <rect x="330" y="170" width="47" height="20" class="f-ok"/>
    <rect x="426" y="170" width="47" height="20" class="f-ok"/>
    <rect x="532" y="170" width="38" height="20" class="f-ok"/>
    <rect x="618" y="170" width="47" height="20" class="f-ok"/>
  </g>
  <text x="105" y="64" fill="currentColor">생성 4개 → susp</text>
  <text x="682" y="94" fill="currentColor">출력</text>
  <path d="M378 44 V196" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="300" y="212" fill="currentColor">tick 0: wrap → taskSWITCH_DELAYED_LISTS → 같은 tick 에 Comm 깸</text>
  <text x="10" y="234" fill="currentColor">ovf: Comm@0 (tick -3 에 넣음), Ctrl@2 (tick -2) — overflow 리스트에 있다가 교환 후 깨어남</text>
  <text x="10" y="254" fill="currentColor">LogA/LogB 는 pxIndex 차례대로: 높은 태스크가 끼어든 tick 에도 "다음 차례" 가 이어서 돈다</text>
</svg>
```

**결정성 — 무엇을 했고 무엇을 봤나.** POSIX 포트의 tick 스레드는 시작하자마자 첫 SIGALRM 을 보낸다(port.c 460–468: `pthread_kill` 다음에 `usleep`). 첫 버전(1 kHz, T0 = −8, Boot 가 첫 줄에서 기록을 끄고 `xTaskDelayUntil(&T0, 2)` 로 정렬)은 20번 중 **세 가지** 출력이 나왔다: 첫 tick 이 Boot 의 첫 명령보다 먼저 와 `[-7] tick` 이 끼거나, Boot 가 늦게 시작해 trace 전체가 한 tick 밀렸다. 기록 끄기를 `traceSTARTING_SCHEDULER`(3811, 인터럽트가 꺼진 시점)로 옮기고 여유를 24 tick(T0 = −30)으로 늘려도, 1 kHz 에서 40번 중 3번, 100 Hz 에서도 60번 중 6번 "SYNC FAILED: tick=-5" — 호스트 부하(load average ~148, 다른 작업 병렬 실행 중)에서 깨어난 Boot 스레드가 실제로 돌기 전에 다음 tick 이 왔다. 최종 버전은 100 Hz 에서 Boot(최고 우선순위)가 블록하지 않고 **tick 을 폴링** 한다: tick 처리(SIGALRM 핸들러)가 Boot 자신의 스레드에서 일어나 스레드 깨우기가 끼지 않는다. 결과: 60/60 (load ~70), 100/100 (load ~16–35) 동일 출력, stderr 0. 정렬이 실패하면 숨기지 않고 exit 2 로 끝난다.

<!-- code: demo_freertos_trace.c -->

**lldb 로 본 호출 스택** (`-O0` 빌드, 조건부 브레이크포인트; SIGALRM 은 `process handle -s false` 로 통과). 실제 세션에서 프레임 부분만 그대로:

```text
* thread #2, name = 'Boot', stop reason = breakpoint 1.1
  * frame #0: RTOS-M13_dt0`prvAddNewTaskToReadyList(pxNewTCB=0x000000010069d110) at tasks.c:2044:9
    frame #1: RTOS-M13_dt0`xTaskCreate(pxTaskCode=(RTOS-M13_dt0`ctrl_task at demo_freertos_trace.c:85), pcName="Ctrl", uxStackDepth=16384, pvParameters=0x0000000000000000, uxPriority=3, pxCreatedTask=0x0000000000000000) at tasks.c:1752:13
    frame #2: RTOS-M13_dt0`boot_task(p=0x0000000000000000) at demo_freertos_trace.c:113:5
    frame #3: RTOS-M13_dt0`prvWaitForStart(pvParams=0x0000007cc141ffd8) at port.c:561:5
...
* thread #5, stop reason = breakpoint 3.1
  * frame #0: RTOS-M13_dt0`vTaskSwitchContext at tasks.c:5219:13
    frame #1: RTOS-M13_dt0`vPortSystemTickHandler(sig=14) at port.c:504:13
    frame #2: libsystem_platform.dylib`_sigtramp + 56
    frame #3: libsystem_pthread.dylib`pthread_sigmask + 16
    frame #4: RTOS-M13_dt0`vPortEnableInterrupts at port.c:419:9
(lldb) p pxCurrentTCB->pcTaskName
(char[8]) "LogA"
(lldb) finish
(lldb) p pxCurrentTCB->pcTaskName
(char[8]) "LogB"
```

읽는 법: 생성은 `xTaskCreate`(1752) → `prvAddNewTaskToReadyList`. tick 은 POSIX 포트에서 **실행 중 태스크의 스레드 안 시그널 핸들러** (`_sigtramp` → `vPortSystemTickHandler` → `vTaskSwitchContext`)이고, 막혀 있던 SIGALRM 이 `vPortEnableInterrupts` 의 `pthread_sigmask` 에서 풀리는 순간 들어왔다 — "인터럽트를 켜는 순간 펜딩 인터럽트가 들어온다" 를 그대로 보여 준다. `vTaskSwitchContext` 전후로 `pxCurrentTCB` 가 LogA → LogB. 단 디버거로 멈추면 tick 과 태스크 코드의 상대 타이밍이 바뀐다 (이 세션에서는 tick −1→0 처리가 LogA 스레드의 첫 시작 지점 `prvWaitForStart` 에서 일어났다 — 방해 없는 실행에서 LogA 는 tick −6 에 처음 돈다) — 호출 경로의 증거로만 쓰고 순서는 trace 로 본다. Cortex-M 이라면 frame #1 자리가 `xPortSysTickHandler` → (PENDSVSET) → `xPortPendSVHandler` 다.

### 미니 커널과의 차이 (코드 근거)

| # | 미니 커널 (RTOS-B02/B04/B06/B05) | FreeRTOS V11.3.2 | 근거 | 왜 |
|---|---|---|---|---|
| 1 | TCB 에 리스트 노드 하나 (`next/prev`) | `xStateListItem` + `xEventListItem` | tasks.c 389–390, 1921–1930, 4917–4919 | 큐에서 기다리는 태스크는 **지연 리스트(타임아웃)와 이벤트 리스트(대기 순서)에 동시에** 있어야 한다. 정지 중 깨움은 `xEventListItem` 으로 `xPendingReadyList` 에 건다 (5550) |
| 2 | ready = FIFO, 실행할 때 머리를 꺼냄 | 실행 중 태스크도 ready 리스트에 **그대로**, `pxIndex` 가 차례를 가리킴 | list.h 286–297, list.c 121–128 | 꺼내고 다시 넣는 일이 없다. 새로 준비된 태스크는 index 앞 = 줄의 맨 뒤 |
| 3 | 비트맵이 항상 정확 (B04) | generic: `uxTopReadyPriority` 는 상한, 선택 때만 내림 / optimised: 비트맵 | tasks.c 186–212, 225, 234–259 | 블록 경로를 싸게(generic 은 아무것도 안 함). 대가는 다음 선택의 루프 (연습 4: 31칸) |
| 4 | `kernel_tick()` 은 원자적 | 스케줄러 정지 중 tick 은 `xPendedTicks` 로만 세고, `xTaskResumeAll` 이 나중에 돌린다 | 3928, 5061–5065, 4141–4171 | 리스트 조작을 인터럽트를 끄지 않고 보호 (긴 작업에 임계 구역 대신) |
| 5 | 임계 구역 중첩 없음 (B05 `sigprocmask` 한 쌍) | `uxCriticalNesting` — 0 이 될 때만 인터럽트를 다시 켬 | ARM_CM4F port.c 161, 475–501; POSIX port.c 354–374, 588/599 | 임계 구역 안에서 임계 구역을 쓰는 API 를 부를 수 있게. POSIX 포트는 스레드 전환 때 태스크별로 저장·복원 |
| 6 | 스위치는 즉시 | `xYieldPendings`: 정지 중 스위치 요청(5219–5223), ISR 이 hptw 를 안 넘긴 경우(5564) → 다음 tick(5020) 이나 ResumeAll(4179) 에서 | tasks.c 510, 5219–5227, 5564, 5020 | 지금 바꿀 수 없을 때 잊지 않기 위한 깃발 |
| 7 | 지연 리스트 하나 + wrap-safe 비교 (B06) | 리스트 두 개 + tick 0 에서 교환 + `xNextTaskUnblockTime` 조기 종료 | 267–279, 4858–4905, 8734–8754 | 비교를 그냥 `<` 로 할 수 있고, 대부분의 tick 은 비교 한 번으로 끝 (M07 이 자세히) |

추가로 **스위치 위치**: Cortex-M 에서는 tick ISR 이 직접 바꾸지 않고 PendSV 를 펜딩한다(ARM_CM4F port.c 576). PendSV 는 최저 우선순위라 중첩된 ISR 이 다 끝난 뒤 테일 체이닝으로 한 번만 실행되고, 그 안에서 `r4–r11`(+ FPU 를 쓴 태스크면 `s16–s31`)을 저장하고 `vTaskSwitchContext` 를 부른다 (504–557).

### queue.c 접점

| queue.c (V11.3.2) | 하는 일 | tasks.c 로 |
|---|---|---|
| `xQueueGenericSend` 949, 블록 경로 1117–1143 | 가득 차면 `vTaskSuspendAll` + `prvLockQueue` 후 대기 | `vTaskPlaceOnEventList(&xTasksWaitingToSend)` 1126 → `xTaskResumeAll` |
| 같은 함수 1012–1020 | 넣은 뒤 받을 태스크가 있으면 | `xTaskRemoveFromEventList(&xTasksWaitingToReceive)` → 더 높으면 `queueYIELD_IF_USING_PREEMPTION` (= `portYIELD_WITHIN_API`, 92) |
| `xQueueReceive` 1509, 1605–1619 | 비었으면 같은 패턴 | `vTaskPlaceOnEventList(&xTasksWaitingToReceive)` 1616 |
| `prvLockQueue` 258 / `prvUnlockQueue` 183, 325–338 | 잠긴 동안 ISR 은 리스트 대신 `cTxLock` 만 올림 | 풀 때 몰아서 `xTaskRemoveFromEventList` |
| `xQueueGiveFromISR` 1338, 1404–1461 | ISR give | 잠기지 않았으면 `xTaskRemoveFromEventList` (연습 1·3) |

이벤트 리스트가 우선순위 순인 이유는 `xEventListItem` 값 = `configMAX_PRIORITIES - uxPriority` (1929) + `vListInsert` 정렬 + 안정성: 높은 우선순위가 먼저, 같으면 먼저 온 순서. 연습 1 이 이것을 확인한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/RTOS-M13_so && /tmp/RTOS-M13_so      # 60/60 passed (5회 동일 출력)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/RTOS-M13_san && /tmp/RTOS-M13_san   # 60/60, 보고 없음
```

starter 도 ASan/UBSan 으로 돌려 봤다: 6/60, 보고 없음 (스텁 상태에서도 잘못된 메모리 접근 없음). 체크 대상 코드는 스레드를 쓰지 않는다 — TSan 대상 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `vListInsert` 루프를 `<` 로 | 같은 값 **앞** 에 들어가 FIFO 가 뒤집힌다: 같은 tick 에 깨는 태스크·같은 우선순위 대기자 순서가 바뀜 | `p->next->value <= v` (list.c 192) |
| `MAX_DELAY` 특례를 뺌 | 보초 값도 MAX 라 루프가 보초를 지나 영원히 돈다 | `if (v == MAX_DELAY) p = end.prev;` |
| `insert_end` 를 "보초 앞(꼬리)" 에 | index 가 보초가 아니면 라운드 로빈 차례가 틀어진다 (A6: CADB 가 아니게 됨) | index 앞에: `it->next = idx; it->prev = idx->prev;` |
| `uxListRemove` 에서 index 수정 누락 | index 가 지연 리스트로 간 항목을 따라가 블록된 태스크나 남의 보초를 고른다 (연습 2) | `if (l->index == it) l->index = it->prev;` |
| `next_owner` 에서 보초를 그대로 반환 | owner 없는 보초(`MiniListItem_t`)를 TCB 로 씀 | 보초면 한 칸 더 |
| generic 에서 블록할 때 `top_ready` 를 직접 내림 | 같은 우선순위 다른 태스크가 남아 있으면 틀린다. 커널은 선택할 때만 내린다 | 블록 경로는 아무것도 안 함 (225) |
| optimised 에서 블록할 때 무조건 비트 지움 | 같은 우선순위 태스크가 남아 있으면 그 우선순위가 사라진다 | `list_remove(...) == 0` 일 때만 |
| overflow 로 넣을 때도 `next_unblock` 갱신 | wrap 된 작은 값이 현재 리스트의 조기 종료를 망친다 (매 tick 루프, 또는 엉뚱한 시각) | 현재 리스트에 넣을 때만 |
| 타임아웃에서 `event_item` 을 안 뺌 | 큐 대기 리스트에 죽은 대기자가 남아 다음 send 가 엉뚱한 태스크를 "깨운다" | `if (t->event_item.container) list_remove(&t->event_item);` |
| 깨운 태스크가 "같아도" 선점 | FreeRTOS 는 `>` 만 선점. 같은 우선순위는 time slicing 이 다음 tick 에 나눈다 | `t->prio > current->prio` |
| 시작 전 첫 태스크를 `<` 로 고름 | 같은 우선순위면 **나중에** 만든 것이 첫 태스크다 (2073) | `current->prio <= t->prio` |

## 8. 면접 Q&A

<details>
<summary>Q1. xTaskCreate 를 부르면 무슨 일이 일어나고, 그 태스크는 언제 처음 실행되나요?</summary>

`prvCreateTask` 가 스택과 TCB 를 할당한다(아래로 자라는 스택이면 스택 먼저 — 넘쳐도 자기 TCB 를 먼저 덮지 않게, 1640). `prvInitialiseNewTask` 가 이름·우선순위·리스트 항목 두 개(이벤트 항목 값 = `configMAX_PRIORITIES − prio`)를 채우고 `pxPortInitialiseStack` 으로 "방금 인터럽트당한 것 같은" 가짜 프레임을 쌓는다(PC = 태스크 함수). `prvAddNewTaskToReadyList` 가 임계 구역 안에서 개수를 늘리고(첫 태스크면 리스트 초기화), ready 리스트 꼬리(index 앞)에 넣는다. 처음 실행: 스케줄러 전이면 `vTaskStartScheduler` 때 — 가장 높은 우선순위, 같으면 **나중에 만든** 태스크가 첫 태스크(`<=`, 2073). 스케줄러가 돌고 있으면 지금 태스크보다 **엄격히** 높을 때만 바로 양보(`taskYIELD_ANY_CORE_IF_USING_PREEMPTION`, 90). 꼬리: "실패하면?" → `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` 반환 — 반환값을 안 보면 조용히 태스크가 없다.

</details>

<details>
<summary>Q2. TCB 에 리스트 항목이 왜 두 개인가요?</summary>

`xStateListItem` 은 상태(ready/지연/suspended) 리스트에, `xEventListItem` 은 이벤트(큐·세마포어 대기) 리스트에 건다. 타임아웃이 있는 `xQueueReceive` 로 기다리는 태스크는 **둘 다에 동시에** 있어야 한다: 지연 리스트는 wake tick 순(타임아웃), 이벤트 리스트는 우선순위 순(누구를 먼저 깨울지). 데이터가 오면 `xTaskRemoveFromEventList` 가 이벤트에서 빼고 상태 항목을 지연 → ready 로 옮기고(5525–5530), 타임아웃이 먼저면 `xTaskIncrementTick` 이 상태에서 빼고 이벤트에서도 뺀다(4913–4919). 스케줄러가 정지 중이면 이벤트 항목을 `xPendingReadyList` 에 건다(5550) — 이 리스트를 위한 세 번째 항목을 따로 두지 않는 재활용이다.

</details>

<details>
<summary>Q3. 같은 우선순위 태스크의 라운드 로빈은 어떻게 구현돼 있나요?</summary>

실행 중인 태스크를 ready 리스트에서 빼지 않는다. 리스트의 `pxIndex` 가 "마지막으로 고른 항목" 을 가리키고, `listGET_OWNER_OF_NEXT_ENTRY` 가 한 칸 옮겨(보초면 한 칸 더) 다음 owner 를 준다(list.h 286–297). 새로 준비된 태스크는 `listINSERT_END` 로 **index 앞** 에 들어가 한 바퀴 뒤에 차례가 온다. tick 마다 같은 우선순위 ready 가 둘 이상이면 `xTaskIncrementTick` 이 pdTRUE(4971). 꼬리: "높은 태스크가 tick 중간에 깨어 잠깐 돌고 블록하면, 끼어들기 당한 태스크가 이어서 도나?" → 아니다. 선택은 다시 `GET_OWNER_OF_NEXT_ENTRY` 라 **다음 차례** 가 돈다 — 끼어들기 당한 태스크는 남은 slice 를 잃는다. 이 레슨 trace 에서는 깨움이 tick 경계에서만 일어나 손해가 드러나지 않는다 — ISR 이 tick 중간에 깨우는 경우는 코드(선택 매크로 210)에서 추론한 것이고 실측하지 않았다.

</details>

<details>
<summary>Q4. xNextTaskUnblockTime 과 지연 리스트가 두 개인 이유는?</summary>

지연 리스트는 wake tick 오름차순이고 머리가 가장 먼저 깬다. `xNextTaskUnblockTime` = 머리의 값이라 `xTaskIncrementTick` 은 대부분의 tick 에서 비교 한 번으로 끝난다(4871; 테스트 B4 `walks == 0`). 두 리스트는 wrap 처리다: `tick + ticks` 가 넘치면 overflow 리스트에 넣고(8734), tick 이 0 이 되는 순간 둘을 교환한다(267–279). 그래서 리스트 안의 비교를 그냥 부호 없는 `<` 로 할 수 있다 — wrap-safe 뺄셈 비교(B06) 대신. 교환 시 현재 리스트는 반드시 비어 있다(assert 272): 마지막 tick(=`portMAX_DELAY`)에 `now >= next_unblock` 이 항상 참이라 남은 항목을 다 깨우기 때문. 이 레슨의 진짜 커널 trace 에서 `ovf:Comm@0` 가 tick 0 에 정확히 깼다. 꼬리: tickless idle 은 `xNextTaskUnblockTime` 을 "얼마나 잘 수 있나" 로 쓴다 (RTOS-A02).

</details>

<details>
<summary>Q5. configUSE_PORT_OPTIMISED_TASK_SELECTION 은 무엇을 바꾸나요? 0 이면 왜 uxTopReadyPriority 가 틀려도 되나요?</summary>

1 이면 `uxTopReadyPriority` 가 비트맵이고 선택은 `31 − clz` 한 번(Cortex-M3/M4 의 `clz` 명령, ARM_CM4F portmacro.h 155). 리스트가 빌 때 비트를 지워야 한다(8708). 우선순위는 32 개까지. 0(generic)이면 `uxTopReadyPriority` 는 **상한** 일 뿐이다: 올릴 때만 갱신하고(186–192), 블록 경로에선 안 건드리며(225 빈 매크로), 선택할 때 빈 리스트를 내려가며 고친다(197–212). 틀려도(높게) 되는 이유는 선택 루프가 어차피 빈 리스트를 건너뛰기 때문 — 낮게 틀리는 경우는 없다(불변식, 연습 4 에서 3000 걸음 검사). 비용: 연습 4 에서 prio 31 태스크가 블록하고 IDLE 만 남으면 선택마다 31칸, 10번에 310. Cortex-M0 는 `clz` 가 없어 generic (RTOS-B04).

</details>

<details>
<summary>Q6. vTaskSuspendAll 과 taskENTER_CRITICAL 은 무엇이 다르고 언제 무엇을 쓰나요?</summary>

`taskENTER_CRITICAL` 은 인터럽트를 막는다(Cortex-M 은 BASEPRI 로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하만, 중첩 카운트 `uxCriticalNesting`, port.c 475–501). 짧게만. `vTaskSuspendAll` 은 카운터 하나만 올린다(3928) — 인터럽트는 계속 들어오고, 컨텍스트 스위치만 막힌다. 그 사이 tick 은 `xPendedTicks` 로만 세고(5064), ISR 이 깨운 태스크는 `xPendingReadyList` 에 대기(5550), 스위치 요청은 `xYieldPendings`(5223). `xTaskResumeAll` 이 이것들을 정산한다(4097–4171; 연습 3: tick 10→13, T·E 준비, 바로 스위치). 커널 자신도 큐 블록 경로에서 "리스트를 길게 만지는 부분" 을 이걸로 보호한다(queue.c 1117). 꼬리: 정지 중에 블록 API(`vTaskDelay`, `xQueueReceive(..., 10)`)를 부르면 안 된다 — 스위치가 막혀 있어 블록할 수 없다 (configASSERT 가 잡는 곳도 있다, 2517).

</details>

<details>
<summary>Q7. Cortex-M 에서 tick ISR 이 왜 직접 컨텍스트 스위치를 하지 않고 PendSV 를 쓰나요?</summary>

SysTick 핸들러는 `xTaskIncrementTick()` 이 pdTRUE 면 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT` 만 하고 나간다(port.c 570–576). PendSV 는 최저 우선순위로 설정되므로(`portNVIC_PENDSV_PRI` = 255, port.c 65–66, 433), 다른 ISR 이 중첩돼 있으면 그것들이 모두 끝난 뒤 한 번만 실행된다(테일 체이닝). 그래서 ① ISR 중간에 태스크 스택을 바꾸는 일이 없고 ② 여러 ISR 이 각각 스위치를 요청해도 실제 스위치는 한 번이다. `portYIELD()` 도 같은 PENDSVSET (portmacro.h 88–97). PendSV 안에서 `psp` 를 읽고, FPU 를 쓴 태스크면(`EXC_RETURN` bit4 == 0) `s16–s31`, 그다음 `r4–r11` 과 `r14` 를 저장하고 `vTaskSwitchContext` 를 부른 뒤 새 `pxCurrentTCB->pxTopOfStack` 에서 복원한다(504–557). `r0–r3, r12, lr, pc, xPSR` 는 하드웨어가 예외 진입 때 이미 쌓았다.

</details>

<details>
<summary>Q8. 하드폴트가 아니라 vListInsert 의 for 루프에서 멈춰 있습니다. 무엇을 의심하나요?</summary>

리스트가 깨져서 `<=` 루프가 보초를 다시 만나지 못하는 것이다. 소스 주석(list.c 167–190)이 꼽는 순서대로: ① 스택 오버플로가 TCB·리스트 항목을 덮음(`configCHECK_FOR_STACK_OVERFLOW`, RTOS-M09) ② Cortex-M 인터럽트 우선순위 오류 — FreeRTOS API 를 부르는 ISR 의 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 높아(숫자로 작아) 임계 구역이 그 ISR 을 못 막음 ③ 임계 구역·스케줄러 정지 중 API 호출, 또는 ISR 에서 FromISR 아닌 API ④ 초기화 전 큐·세마포어 사용, 스케줄러 시작 전 인터럽트 ⑤ tick 인터럽트 우선순위. 확인: `configASSERT` 를 켜고, `configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES` 로 리스트 무결성 값을 켜면 `listTEST_LIST_INTEGRITY` 가 덮어쓰기를 잡을 수 있다(list.c 115–116, 150–151). 이 레슨의 `list_check` 와 같은 생각이다.

</details>

## 9. 추가 연습

### 연습 1 — 이벤트 리스트: 큐 대기자는 누가 먼저 깨나

`place_on_event_list` (vTaskPlaceOnEventList) 와 `remove_from_event_list` (xTaskRemoveFromEventList) 를 작성하라. 우선순위 2, 3, 1, 3, 2 인 R1–R5 가 이 순서로 한 큐에서 막혔을 때 깨는 순서와 반환값은? 타임아웃 3 으로 기다리던 T 는 3 tick 뒤 어디에 있나? `portMAX_DELAY` 로 기다리면?

<details>
<summary>답 보기</summary>

`list_insert(ev, &cur->event_item)` (값 = NPRIO − prio) + `add_current_to_delayed(k, ticks, 1)`. 깨우기: 머리를 이벤트에서 빼고, 정지 중이 아니면 상태 항목을 ready 로(정지 중이면 `event_item` 을 pending ready 리스트로), 깨운 prio 가 더 높으면 `yield_pending = 1` 후 1. 실행 결과 `ex1 wake order: R2 R4 R1 R5 R3` — 3, 3(온 순서), 2, 2, 1. 지금 태스크가 prio 1 이라 R3 만 0 을 반환. T 는 tick 3 에 `increment_tick` 이 지연 리스트와 이벤트 리스트 **둘 다** 에서 빼 ready 로(1 반환, 2 &gt; 1). `portMAX_DELAY` 는 `xCanBlockIndefinitely = pdTRUE` 라 suspended 리스트 — 타임아웃이 없다.

</details>

### 연습 2 — uxListRemove 의 index 수정을 빼먹으면

`list_remove_buggy` (list.c 232–235 를 지운 버전)로, ready 리스트 [A,B,C] 에서 실행 중인 B 를 빼 지연 리스트(D 가 wake 50 으로 있음)에 넣은 뒤 다음 라운드 로빈이 무엇을 고르는지 확인하라. B 의 wake 가 10 일 때와 99 일 때.

<details>
<summary>답 보기</summary>

실행 결과:

```text
ex2 correct uxListRemove:  next=C  ready list check=1  ready="A C"
ex2 buggy, B before D:     next=D  ready list check=0  ready="A C"
ex2 buggy, B at tail:      next=NULL(end of delayed list)  ready list check=0  ready="A C"
```

index 가 B 를 가리킨 채 남고, B 의 `next` 는 이제 지연 리스트 안이다. wake 10 이면 B.next = D → **블록된 D 가 실행 대상으로** 뽑힌다. wake 99 면 B.next = 지연 리스트의 보초 → ready 리스트의 보초와 다르므로 건너뛰지 않고 그 owner(NULL; 커널에서는 `MiniListItem_t` 라 owner 필드조차 없어 뒤 메모리를 읽음)를 TCB 로 쓴다 → 하드폴트. 리스트 자체(A C)는 멀쩡해 보여서 찾기 어렵다 — 그래서 index 가 리스트 안에 있는지까지 검사하는 `list_check` 가 0 을 낸다.

</details>

### 연습 3 — 스케줄러 정지: 밀린 tick 과 pending ready

`suspend_all` / `resume_all` 을 작성하고 `switch_context`·`increment_tick` 에 정지 처리를 넣어라. tick 10 에 T(prio 3, wake 12) 블록, E(prio 2) 큐 대기, L(prio 1) 실행 중. L 이 정지 구간에서 3 tick 을 보내는 동안 ISR 이 큐에 넣어 E 를 깨운다. 재개 직후 상태는?

<details>
<summary>답 보기</summary>

정지 중: `switch_context` 는 바꾸지 않고 `yield_pending = 1` (5219–5223), `increment_tick` 은 `pended++` 후 0 (5064) → tick 10, pended 3. E 는 `event_item` 이 pending ready 리스트에, 상태 항목은 아직 **지연 리스트에**(ready[2] 비어 있음). 중첩 정지(두 번 정지, 한 번 재개)면 아무 일도 없다. 마지막 재개: pending → ready(E), next_unblock 재계산, 밀린 tick 3개를 `increment_tick` 으로 다시 돌려 tick 12 에서 T 를 깨우고 tick 13 에서 끝, `yield_pending` 이므로 바로 스위치 → current = T, 반환 1 (`xAlreadyYielded`). tick 이 "미끄러지지 않는다" 는 게 핵심: 정지가 길어도 깨는 시각은 정확하고 늦게 처리될 뿐이다.

</details>

### 연습 4 — generic vs optimised: 비용과 같은 결정

prio 31 태스크 H 가 1 tick 일하고 1 tick 쉬기를 10번 할 때(나머지는 IDLE 뿐) 두 방식의 `scan_steps` 를 세라. 그리고 무작위 워크로드(태스크 8개, 우선순위 1–31 무작위, tick 0xFFFFFF00 에서 시작해 wrap 통과, 3000 걸음)에서 두 방식이 매 걸음 같은 태스크를 고르는지, 불변식(generic: `top_ready ≥` 실제 최고 / optimised: 비트 = 리스트 비어 있지 않음)이 지켜지는지 확인하라.

<details>
<summary>답 보기</summary>

실행 결과:

```text
ex4 H(31)<->IDLE x10: generic scan_steps=310, optimised scan_steps=0 (+1 CLZ per switch)
ex4 random 3000 steps across a wrap (overflows=1): same decisions=1, generic scan_steps=1684, inconsistent=0/0
```

generic 은 H 가 블록한 순간에도 `top_ready == 31` (지연 갱신)이고, 다음 선택이 31칸을 내려간다 — 스위치 비용이 **우선순위 간격에 비례**. optimised 는 상수. 결정은 3000 걸음 모두 같았다 — 선택 방식은 "어떻게 찾나" 만 바꾸고 "누구를" 은 바꾸지 않는다. 그래서 같은 trace 가 나온 것(Part C)도 당연하다. 실무 결론: 우선순위를 듬성듬성(1, 10, 20, 31) 쓰는 generic 시스템은 빈 리스트 스캔을 매 스위치 치른다 — `configMAX_PRIORITIES` 를 필요한 만큼만.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-M13_pr && /tmp/RTOS-M13_pr   # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/RTOS-M13_ps && /tmp/RTOS-M13_ps   # 보고 없음
```

## 10. 핵심 정리

- 경로: `xTaskCreate`(1729) → `prvCreateTask`(스택 먼저, 1631) → `prvInitialiseNewTask`(리스트 항목 2개, 가짜 프레임, 1804) → `prvAddNewTaskToReadyList`(임계 구역, 첫 태스크 `<=`, 2040) → `prvAddTaskToReadyList`(RECORD + INSERT_END, 287).
- tick: SysTick → `xTaskIncrementTick`(4831): 0 이면 지연 리스트 교환, `xNextTaskUnblockTime` 조기 종료, 깬 태스크는 두 리스트에서 빼서 ready, `>` 면 선점, 같은 prio 둘 이상이면 time slicing → PendSV → `vTaskSwitchContext`(5215) → `taskSELECT_HIGHEST_PRIORITY_TASK`.
- `List_t` = 개수 + `pxIndex` + 보초(값 MAX). `vListInsert` 는 같은 값 **뒤**(MAX 는 바로 꼬리), `vListInsertEnd` 는 index **앞**, `uxListRemove` 는 index 를 **앞으로** 물림, `GET_OWNER_OF_NEXT_ENTRY` 는 보초를 건너뛴다.
- 실행 중 태스크도 ready 리스트에 있다. 라운드 로빈 = `pxIndex` 이동. 끼어들기 당한 태스크는 남은 slice 를 잃는다.
- generic `uxTopReadyPriority` = 상한(선택 때만 내림), optimised = 비트맵 + `clz`(≤ 32 prio). 결정은 같고 비용만 다르다.
- TCB 리스트 항목 둘: 상태(시간) + 이벤트(우선순위 순 대기). 정지 중 깨움은 이벤트 항목으로 pending ready 에.
- `vTaskSuspendAll` = 스위치만 막음(tick 은 `xPendedTicks`), `taskENTER_CRITICAL` = 인터럽트 막음 + 중첩 카운트. 못 바꾸는 스위치는 `xYieldPendings` 로 기억.
- 모델 trace 가 진짜 커널(POSIX 포트 + trace 매크로) trace 와 바이트 단위로 같았다 — 소스를 "읽었다" 의 증거.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — `list.c` 49–247; `include/list.h` 144–179, 286–297, 322–338, 362–385; `tasks.c` 68–110(yield 매크로), 178–293(선택·RECORD·지연 리스트 교환·prvAddTaskToReadyList), 377–455(TCB), 478–513(전역), 1631–1763(생성), 1804–2035, 2040–2114, 2506–2549(vTaskDelay), 3197–(vTaskSuspend), 3742–3818(시작), 3890–3934, 4064–4200(정지/재개), 4831–5078(tick), 5215–5298(스위치), 5402–5590(이벤트 리스트), 6175–6204, 6621–6640, 8685–8760; `queue.c` 89–94, 258–338, 949–1160, 1338, 1509–1641; `include/FreeRTOS.h` 608–609, 617–877(trace 기본값), 2724, 2803–2804, 2921–2923; `portable/GCC/ARM_CM4F/port.c` 161, 475–584, `portmacro.h` 88–100, 144–173; `portable/ThirdParty/GCC/Posix/port.c` 270–322, 354–436, 452–516, 574–601 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS — Trace Hook Macros — <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-RTOS-trace-feature>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Task Management / Scheduling 장 — <https://www.freertos.org/Documentation/RTOS_book.html>
- ARM, *Cortex-M4 Devices Generic User Guide* — PendSV, SysTick, 예외 진입 시 스택 프레임
- 관련 레슨: RTOS-B02(TCB·상태), RTOS-B04(비트맵 CLZ), RTOS-B05(선점·라운드 로빈), RTOS-B06(지연·wrap), RTOS-B07(POSIX 포트 첫 실행), RTOS-B11(큐), RTOS-M01(알림), RTOS-M02(FromISR·yield), RTOS-M06(블로킹 큐), RTOS-M07(지연 리스트 vs 타이머 휠), RTOS-M09(스택 오버플로), RTOS-A01(Cortex-M 컨텍스트 스위치), RTOS-A02(tickless)
