---
id: RTOS-B07
time: 60분
updated: 2026-11-11
---
# RTOS-B07 FreeRTOS POSIX 포트 첫 실행: 태스크·우선순위·스택

## 학습 목표

- `FreeRTOSConfig.h` 를 처음부터 직접 쓰고, 줄마다 "무엇을 켜는가 / 왜 이 값인가 / 실제 MCU 에선" 을 말할 수 있다 (틱, 우선순위 개수, 스택 단위 = **워드**, 힙, 훅, trace facility, 통계 포맷 함수).
- 진짜 FreeRTOS(V11.3.2 POSIX 포트)로 우선순위가 다른 태스크 3개를 만들어, **만든 순서·깨운 순서가 아니라 우선순위 순서로** 실행됨을 반복 실행해도 같은 결과로 확인한다.
- `vTaskListTasks` 의 열(이름 / 상태 X·R·B·S·D / 우선순위 / 스택 HWM 워드 / 번호)과 `uxTaskGetSystemState` 의 `TaskStatus_t` 를 읽고, 둘이 같은 데이터임을 코드로 확인한다.
- `vTaskPrioritySet` 이 **언제 그 자리에서** 컨텍스트 스위치를 일으키는지(내 우선순위를 내릴 때 / 남을 내 위로 올릴 때) 와, 막힌 태스크를 올리면 아무 일도 없다는 것을 실측으로 설명한다.
- POSIX 포트 모델(태스크 1개 = pthread 1개, 한 번에 하나만 실행, 틱 = 타이머 스레드의 SIGALRM)과 **진짜가 아닌 것**(시간, 스택 크기와 HWM)을 숫자로 설명한다.

## 1. 문제

"FreeRTOS 써 보셨어요? `FreeRTOSConfig.h` 에서 뭘 설정하나요? 스택 크기는 어떻게 정하셨어요?" — 펌웨어 면접에서 RTOS 경험을 확인하는 첫 질문이다. 여기서 "configMINIMAL_STACK_SIZE 는 **워드** 단위", "HWM 으로 실측해서 정했다", "우선순위는 숫자가 클수록 급하다(Zephyr 는 반대)" 가 바로 나오면 진짜로 써 본 사람이고, 막히면 예제 프로젝트를 복사만 해 본 사람이다.

이 레슨은 시리즈에서 처음으로 **직접 만든 미니 커널이 아니라 진짜 FreeRTOS 커널** 을 맥에서 돌린다. RTOS-B02~B06 에서 만든 TCB · 상태 · ready list · 선점이 실제 커널에서 어떻게 보이는지 `vTaskList` 한 장으로 확인하는 레슨이다. 커널 소스는 `~/.esn_scratch/FreeRTOS-Kernel` (태그 V11.3.2) 이고, 포트는 `portable/ThirdParty/GCC/Posix` 다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">POSIX 포트: 태스크 = pthread, 그중 "현재 태스크" 스레드만 돈다</text>
  <rect x="10" y="40" width="340" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">macOS 프로세스 1개</text>
  <rect x="24" y="74" width="96" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="34" y="92" fill="currentColor">sup (6)</text><text x="34" y="108" fill="currentColor" font-size="12">실행 중</text>
  <rect x="128" y="74" width="96" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="138" y="92" fill="currentColor">imu (3)</text><text x="138" y="108" fill="currentColor" font-size="12">event_wait</text>
  <rect x="232" y="74" width="104" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="242" y="92" fill="currentColor">ctrl / telem</text><text x="242" y="108" fill="currentColor" font-size="12">event_wait</text>
  <rect x="24" y="126" width="148" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="34" y="144" fill="currentColor">"Scheduler timer"</text><text x="34" y="160" fill="currentColor" font-size="12">usleep(1000) 반복</text>
  <rect x="184" y="126" width="152" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="194" y="144" fill="currentColor">main = "Scheduler"</text><text x="194" y="160" fill="currentColor" font-size="12">sigwait 로 대기</text>
  <path d="M98 126 L72 116" stroke="currentColor" stroke-width="2"/>
  <path d="M72 116 L82 116 M72 116 L78 124" stroke="currentColor" stroke-width="2"/>
  <text x="24" y="190" fill="currentColor" font-size="12">pthread_kill(현재 태스크 스레드, SIGALRM)</text>
  <text x="24" y="208" fill="currentColor" font-size="12">→ 그 스레드에서 틱 ISR 실행 (port.c 488)</text>
  <text x="24" y="226" fill="currentColor" font-size="12">→ xTaskIncrementTick → 필요하면 스레드 교대</text>
  <rect x="390" y="40" width="340" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="402" y="62" fill="currentColor" font-weight="bold">Cortex-M (STM32F4 등)</text>
  <text x="402" y="88" fill="currentColor">CPU 1개, 태스크마다 RAM 의 스택 배열</text>
  <text x="402" y="110" fill="currentColor">SysTick 하드웨어 타이머 → SysTick_Handler</text>
  <text x="402" y="132" fill="currentColor">→ xTaskIncrementTick → PendSV 펜딩</text>
  <text x="402" y="154" fill="currentColor">→ PendSV 가 r4–r11 저장, PSP 교체</text>
  <text x="402" y="176" fill="currentColor">태스크 코드는 FreeRTOS 스택 위에서 돈다</text>
  <text x="402" y="198" fill="currentColor">→ HWM 이 진짜 사용량을 보여 준다</text>
  <text x="402" y="220" fill="currentColor">틱 1 ms 는 수정 발진기만큼 정확</text>
  <rect x="10" y="254" width="720" height="66" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="276" fill="currentColor">같은 것: ready list · 우선순위 선택 · 상태 전이 · API 반환값 → 실행 "순서" 는 진짜 커널 그대로</text>
  <text x="22" y="296" fill="currentColor">다른 것: 시간(1 초에 틱 419~567 개 실측), 스택(코드는 512 KB pthread 스택에서 돈다 → HWM 무의미),</text>
  <text x="22" y="314" fill="currentColor">인터럽트 우선순위(BASEPRI) 없음. → 검사는 순서와 반환값만, 시간에 기대지 않는다</text>
</svg>
```

시나리오는 STM32F4 급 드론 비행 제어기다. IMU 샘플링(`imu`, 우선순위 3)이 가장 급하고, 자세 제어 루프(`ctrl`, 2), 지상국 텔레메트리(`telem`, 1) 순이다. 감독 태스크 `sup`(6)이 시험을 진행한다. 워커는 "알림(`xTaskNotifyGive`)을 받으면 한 번 일하고 다시 막힌다" — 실제 펌웨어에서 ISR 이나 타이머가 태스크를 깨우는 모양 그대로다 (알림 자체는 RTOS-M01 에서 자세히).

### Part A — `FreeRTOSConfig.h` 작성과 태스크 생성 (`create_workers`)

- 레슨 폴더의 `FreeRTOSConfig.h` 를 직접 쓴다 (정답은 5. 풀이에 줄마다 주석 달린 원본). 틱 1 kHz, 우선순위 7단계(0..6), idle 스택 256 워드, heap_3, idle 훅 · malloc 실패 훅 · 스택 오버플로 검사 2, `configUSE_TRACE_FACILITY` 와 `configUSE_STATS_FORMATTING_FUNCTIONS` 켜기, 소프트웨어 타이머 끄기.
- `BaseType_t create_workers(void)` — 표 `g_w[]` 순서(telem → ctrl → imu, **우선순위 역순**)대로 `xTaskCreate` 하고 핸들을 저장. 하나라도 실패하면 `pdFAIL`.
- 하네스가 확인: 설정값, 태스크 수 5 (sup + 워커 3 + IDLE), 각 우선순위, idle 훅이 불렸는지.

### Part B — 실행 순서 · 상태 · 스택 여유 (`let_workers_run`, `state_letter`, `take_snapshot`)

- `void let_workers_run(void)` — sup 이 세 워커를 깨운 뒤 "워커들이 다 돌고 다시 막힐 때까지" CPU 를 내준다. **`vTaskDelay` 같은 시간 기반 대기 금지** (호스트에서는 시간이 진짜가 아니라 결과가 흔들린다).
- `char state_letter(eTaskState s)` — `eRunning`→`X`, `eReady`→`R`, `eBlocked`→`B`, `eSuspended`→`S`, `eDeleted`→`D`.
- `UBaseType_t take_snapshot(row_t *rows, UBaseType_t max)` — `uxTaskGetSystemState` 로 모든 태스크의 이름 · 상태 글자 · 현재/기본 우선순위 · HWM(워드) · 번호를 채우고 **번호 오름차순** 정렬.
- 하네스가 확인: 실행 순서가 깨운 순서(T, C, I)가 아니라 `"ICT"`, 스냅샷의 상태·번호, HWM = 깊이 − (모든 태스크에 같은 작은 상수), 그리고 `vTaskListTasks` 텍스트를 파싱한 결과가 스냅샷과 같은지.

### Part C — `vTaskPrioritySet` 으로 순서 바꾸기 (`set_prio`)

- `UBaseType_t set_prio(TaskHandle_t h, UBaseType_t p)` — 우선순위를 바꾸고 **바꾸기 전 값** 을 돌려준다.
- 지상국이 비행 로그 덤프를 요청했다 → `telem` 을 1 → 4 로 올린다 (imu 보다 위). 막혀 있는 동안 올리면 상태는 그대로 `B`, 우선순위 칸만 4. 다음 라운드의 실행 순서는 `"TIC"`. 덤프가 끝나면 1 로 되돌려 다시 `"ICT"`.

## 2. Problem (English)

> **First run on a real RTOS: tasks, priorities, stack watermarks (FreeRTOS POSIX port).**
>
> Write a `FreeRTOSConfig.h` (1 kHz tick, 7 priorities, 256-word minimal stack, idle hook, malloc-failed hook, stack-overflow check method 2, trace facility + stats formatting functions, no software timers). A supervisor task `sup` (priority 6) drives three workers that block on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` and append a tag to a run log each time they wake.
>
> 1. `BaseType_t create_workers(void)` — create `telem`(prio 1, 1024 words), `ctrl`(2, 512), `imu`(3, 256) in that order; return `pdFAIL` on any failure.
> 2. `void let_workers_run(void)` — after `sup` notified all workers, give up the CPU until every ready worker has run and blocked again, **without using time** (no `vTaskDelay`).
> 3. `char state_letter(eTaskState)` and `UBaseType_t take_snapshot(row_t *rows, UBaseType_t max)` — use `uxTaskGetSystemState()`; sort rows by task number.
> 4. `UBaseType_t set_prio(TaskHandle_t h, UBaseType_t p)` — change the priority, return the old one.
>
> Expected: run order `"ICT"` (not the notify order T, C, I); after raising `telem` to 4: `"TIC"`; after restoring: `"ICT"`. Snapshot: `sup` is `X`, `IDLE` is `R`, workers are `B`; task numbers follow creation order (sup 1, telem 2, ctrl 3, imu 4, IDLE 5). The result must be identical on every run.
>
> Follow-ups: What does each `vTaskList` column mean, and why is a task blocked forever on a notification listed as `B` although the kernel keeps it in the suspended list? Is the stack high-water mark meaningful on this port? When does `vTaskPrioritySet()` cause an immediate context switch?

## 3. 시작 코드

<!-- code: starter.c -->

이 레슨의 starter/solution/practice 는 **진짜 FreeRTOS 프로그램** 이다 (첫 줄 `/* BUILD: freertos */`). 레슨 폴더에서:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
CF=(-O2 -g -std=gnu11 -pthread -I. -I$K/include -I$P -I$P/utils)   # 배열: bash·zsh 둘 다 OK
# 커널 오브젝트 (한 번만, 경고 허용 — 커널 코드는 고치지 않는다)
cc "${CF[@]}" -Wall -Wextra -c $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
# 내 코드는 -Werror
SRC=starter     # starter / solution / practice / demo_freertos_posix_reality
cc "${CF[@]}" -Wall -Wextra -Werror -c $SRC.c hooks.c
cc -pthread $SRC.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o $SRC && ./$SRC
# starter: "5/29 passed", exit 1 (설정 검사·idle 훅만 통과) — TODO 5곳을 채워 29/29
```

`FreeRTOSConfig.h` 를 고치면 **커널 오브젝트도 다시 컴파일** 해야 한다 (7. 자주 하는 실수 첫 줄). `-std=gnu11` 인 이유: 포트 헤더가 POSIX/Darwin 확장(`pthread_*_np`, `sigset_t` 등)을 쓴다. starter 는 TODO 가 비어 있어도 멈추지 않는다 — 워커가 없으면 깨울 대상이 없고, `let_workers_run` 스텁은 아무것도 안 하므로 sup 혼자 끝까지 간다.

## 4. 힌트

<details>
<summary>힌트 1 — xTaskCreate 의 세 번째 인자는 바이트가 아니다</summary>

`xTaskCreate(fn, "name", depth, param, prio, &handle)` 의 `depth` 는 **`StackType_t` 개수(워드)** 다. 이 포트에서 `StackType_t` 는 `unsigned long` (8 B), Cortex-M 은 `uint32_t` (4 B). 반환값은 `pdPASS` 또는 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` — 확인한다. `param` 으로 `&g_w[i]` 를 넘기면 워커 함수 하나로 세 태스크를 만든다.

</details>

<details>
<summary>힌트 2 — 시간 없이 "다 돌 때까지 기다리기"</summary>

선점형 커널은 늘 **READY 중 가장 높은 우선순위** 를 돌린다. sup 이 자기 우선순위를 idle(0)로 내리면, READY 인 워커가 하나라도 있는 한 sup 은 CPU 를 받을 수 없다. 워커들이 다 돌고 다시 막혀야 sup 이 돌아온다 — 그때 원래 우선순위(6)로 되돌린다. 두 줄이다.

</details>

<details>
<summary>힌트 3 — 스냅샷은 커널이 채워 주는 배열</summary>

`TaskStatus_t ts[8]; UBaseType_t n = uxTaskGetSystemState(ts, 8, NULL);` — 배열이 태스크 수보다 작으면 0 을 돌려준다. 필요한 필드: `pcTaskName`, `eCurrentState`, `uxCurrentPriority`, `uxBasePriority`, `usStackHighWaterMark`, `xTaskNumber`. 반환 순서는 번호순이 아니라 커널 리스트 순회 순서다 → 직접 정렬(삽입 정렬이면 충분).

</details>

<details>
<summary>힌트 4 — 이전 값을 먼저 읽는다</summary>

`uxTaskPriorityGet(h)` 를 `vTaskPrioritySet` **전에** 읽어 둔다. 순서를 바꾸면 새 값을 돌려주게 된다.

</details>

## 5. 풀이

### 0. 이 레슨의 호출 ↔ 실제 API 대응표

| 이 레슨 (FreeRTOS V11.3.2) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) | 주의 |
|---|---|---|---|
| `xTaskCreate(fn, name, depth_words, arg, prio, &h)` | `k_thread_create(&t, stack, size_bytes, fn, a, b, c, prio, 0, K_NO_WAIT)` | `osThreadNew(fn, arg, &attr)` (`attr.stack_size` 바이트) | FreeRTOS 만 **워드** 단위 |
| 우선순위: 큰 수 = 급함, 0 = idle | 작은 수 = 급함, 음수 = 협력형 | `osPriorityNormal` 등 열거형, 큰 값 = 급함 | 포팅할 때 뒤집힘 주의 |
| `vTaskPrioritySet(h, p)` / `uxTaskPriorityGet(h)` | `k_thread_priority_set` / `k_thread_priority_get` | `osThreadSetPriority` / `osThreadGetPriority` | |
| `uxTaskGetStackHighWaterMark(h)` (워드) | `k_thread_stack_space_get(t, &unused)` (바이트, `CONFIG_THREAD_STACK_INFO`) | `osThreadGetStackSpace(id)` (바이트) | 단위 다름 |
| `uxTaskGetSystemState` / `vTaskListTasks` | thread analyzer · 셸 `kernel threads` | `osThreadEnumerate` + `osThreadGetState` | |
| `vTaskSuspend` / `vTaskResume` | `k_thread_suspend` / `k_thread_resume` | `osThreadSuspend` / `osThreadResume` | |
| `vTaskStartScheduler()` (돌아오지 않음) | 없음 — `main()` 자체가 스레드 | `osKernelStart()` | |

### Part A — FreeRTOSConfig.h 를 줄마다 읽기

<!-- code: FreeRTOSConfig.h -->

값을 고른 근거 (전부 소스로 확인):

| 설정 | 이 레슨 | 커널에서 하는 일 (V11.3.2) | 실제 MCU 라면 |
|---|---|---|---|
| `configMAX_PRIORITIES` | 7 | `pxReadyTasksLists[configMAX_PRIORITIES]` — 우선순위마다 `List_t` 1개. 범위 밖 우선순위는 `configASSERT` (tasks.c 1903) 후 최댓값으로 깎음 (1905-1908) | `List_t` 하나가 32비트에서 20 B(무결성 검사 끔, mini list item 기본) → 필요한 만큼만. 포트 최적화 선택(CLZ, RTOS-B04)은 최대 32 |
| `configMINIMAL_STACK_SIZE` | 256 | idle 태스크 스택 깊이 (tasks.c 3704) | CM4 예제들은 보통 128~256 워드. 이 값을 기준 삼아 `x * configMINIMAL_STACK_SIZE` 로 쓰는 코드가 많다 |
| `configUSE_TRACE_FACILITY` | 1 | TCB 에 `uxTCBNumber` 등 추가 (tasks.c 2088-2093), `uxTaskGetSystemState` 컴파일 | TCB 가 커진다 (7. 실수 1번과 연결) |
| `configUSE_STATS_FORMATTING_FUNCTIONS` | 1 | `vTaskListTasks` · `vTaskGetRunTimeStatistics` 컴파일, `snprintf` 의존 | 소스 주석 스스로 "production 에선 `uxTaskGetSystemState` 를 직접" 권장 (tasks.c 7411-7432) |
| `INCLUDE_uxTaskGetStackHighWaterMark` / trace / 검사 2 | 1 / 1 / 2 | 셋 중 하나라도 켜면 새 스택을 `0xa5` 로 채운다 (tasks.c 121, 130-135, 1832-1837) | 채우는 `memset` 이 태스크 생성 시간을 늘린다 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 전환 때 저장된 sp 와 스택 맨 아래 16 B 의 `0xa5a5a5a5` 를 검사 (stack_macros.h 99-118) | 값싼 1차 방어. MPU/스택 리밋 레지스터(ARMv8-M PSPLIM)가 더 확실 (RTOS-M09) |
| `configUSE_TIMERS` | 0 | 타이머 서비스 태스크 "Tmr Svc" 를 만들지 않는다 → `vTaskList` 가 5줄 | 쓰면 이 태스크의 우선순위·스택도 예산에 넣는다 (RTOS-B12) |
| `configTOTAL_HEAP_SIZE` | 64 KB | heap_1/2/4/5 의 정적 배열. **heap_3 은 무시** (`malloc` 래퍼) | heap_4 + 링커 맵으로 실제 RAM 확인 (RTOS-M10) |

훅은 `hooks.c` 에 있다. 이름과 시그니처가 고정이라 틀리면 링크 에러가 난다:

<!-- code: hooks.c -->

`create_workers` 는 표대로 만든다:

```c
for (int i = 0; i < NW; i++)
    if (xTaskCreate(worker, g_w[i].name, g_w[i].depth, &g_w[i], g_w[i].prio, &g_w[i].h) != pdPASS)
        return pdFAIL;
return pdPASS;
```

`main()` 은 `sup` 를 먼저 만들고 워커 셋을 만든 뒤 `vTaskStartScheduler()` 를 부른다. 이 함수가 IDLE 태스크를 만들고(tasks.c 3588 `prvCreateIdleTasks`) 가장 높은 READY 태스크(= sup) 를 시작한다. 그래서 태스크 번호(`uxTCBNumber`, 생성될 때마다 +1)는 sup 1, telem 2, ctrl 3, imu 4, IDLE 5 다. 스케줄러 시작 전에 만든 태스크들은 "만들자마자 실행" 되지 않는다 — 시작 순간 우선순위로만 고른다.

### Part B — 우선순위가 순서를 정한다

```c
static void let_workers_run(void) {
    vTaskPrioritySet(NULL, tskIDLE_PRIORITY);   /* 이 호출 안에서 바로 전환된다 */
    vTaskPrioritySet(NULL, SUP_PRIO);           /* READY 워커가 하나도 없어야 여기까지 온다 */
}
```

첫 줄이 왜 "그 자리에서" 전환되나 — `vTaskPrioritySet` 의 해당 부분 (tasks.c 발췌, 줄 번호 실제):

```c
2925  else if( taskTASK_IS_RUNNING( pxTCB ) == pdTRUE )   /* 실행 중인 태스크(나)를 내리는 경우 */
2927      /* Setting the priority of a running task down means
2928       * there may now be another task of higher priority that is ready to execute. */
2934          xYieldRequired = pdTRUE;
...
3022  if( xYieldRequired != pdFALSE )
3025      taskYIELD_TASK_CORE_IF_USING_PREEMPTION( pxTCB );  /* 단일 코어: portYIELD_WITHIN_API() (tasks.c 82-86) */
```

POSIX 포트의 yield 는 `vPortYield` → `vTaskSwitchContext()` 로 다음 태스크를 고르고 그 스레드를 깨운 뒤 **내 스레드는 잠든다** (port.c 377-403, 574-601). sup 이 다시 깨어나는 건 커널이 sup 을 다시 고를 때뿐이다 — 즉 우선순위 1~3 의 READY 워커가 모두 막힌 뒤. 이것이 시간과 무관하게 결정적인 이유다. `vTaskDelay(1)` 로 기다렸다면 호스트가 바쁠 때 워커가 1 틱 안에 다 못 돌 수 있다 (데모: 1 틱이 0.7~5 ms 로 흔들린다).

`solution` 실제 출력에서 라운드별 실행 순서와 단계 타임라인 (시간 축이 아니라 **순서 축**. `R` 실행, `r` READY 대기, `B` 막힘, `.` 해당 없음):

```text
round 1: notify T, C, I  → run order "ICT"
step : 0 1 2 3 4
sup  : R r r r R      (1~3 단계: 우선순위 0 으로 내려가 READY 로 대기)
imu  : r R B B B
ctrl : r r R B B
telem: r r r R B
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">solution 실측 run log "ICTTICICT" — 순서 축 (시간 아님)</text>
  <g fill="currentColor"><text x="10" y="70">imu (3)</text><text x="10" y="110">ctrl (2)</text><text x="10" y="150">telem (1→4→1)</text><text x="10" y="190">sup (6↔0)</text></g>
  <rect x="120" y="36" width="190" height="168" rx="4" class="f-mut" stroke="currentColor"/>
  <rect x="330" y="36" width="190" height="168" rx="4" class="f-mut" stroke="currentColor"/>
  <rect x="540" y="36" width="190" height="168" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="126" y="52" fill="currentColor" font-size="12">round 1: telem 1</text>
  <text x="336" y="52" fill="currentColor" font-size="12">round 2: telem 4</text>
  <text x="546" y="52" fill="currentColor" font-size="12">round 3: telem 1</text>
  <rect x="160" y="56" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="173" y="72" fill="currentColor">I</text>
  <rect x="198" y="96" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="211" y="112" fill="currentColor">C</text>
  <rect x="236" y="136" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="249" y="152" fill="currentColor">T</text>
  <rect x="122" y="176" width="36" height="22" class="f-acc" stroke="currentColor"/><rect x="274" y="176" width="34" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="370" y="136" width="36" height="22" class="f-warn" stroke="currentColor"/><text x="383" y="152" fill="currentColor">T</text>
  <rect x="408" y="56" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="421" y="72" fill="currentColor">I</text>
  <rect x="446" y="96" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="459" y="112" fill="currentColor">C</text>
  <rect x="332" y="176" width="36" height="22" class="f-acc" stroke="currentColor"/><rect x="484" y="176" width="34" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="580" y="56" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="593" y="72" fill="currentColor">I</text>
  <rect x="618" y="96" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="631" y="112" fill="currentColor">C</text>
  <rect x="656" y="136" width="36" height="22" class="f-ok" stroke="currentColor"/><text x="669" y="152" fill="currentColor">T</text>
  <rect x="542" y="176" width="36" height="22" class="f-acc" stroke="currentColor"/><rect x="694" y="176" width="34" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="10" y="218" width="720" height="72" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="240" fill="currentColor">매 라운드 sup 은 T, C, I 순서로 깨운다 (만든 순서). 그래도 실행은 늘 높은 우선순위부터.</text>
  <text x="22" y="260" fill="currentColor">round 2: telem 을 4 로 올리자 imu(3)보다 먼저 → "TIC". (막혀 있을 때 올려서 그 순간엔 변화 없음)</text>
  <text x="22" y="280" fill="currentColor">sup 칸 = 깨우기(앞) · 우선순위 6 복귀(뒤). 10회 + 부하 12개 걸고 10회, 모두 같은 출력.</text>
</svg>
```

**상태와 스택 여유** — `uxTaskGetSystemState` 로 만든 스냅샷과 `vTaskListTasks` 원문 (실제 출력):

```text
-- after round 1 (uxTaskGetSystemState, 번호순)
  num name        st prio base  hwm(words) = bytes
    1 sup         X     6    6         507 = 4056
    2 telem       B     1    1        1019 = 8152
    3 ctrl        B     2    2         507 = 4056
    4 imu         B     3    3         251 = 2008
    5 IDLE        R     0    0         251 = 2008

-- vTaskListTasks() 원문 (이름 / 상태 / 우선순위 / HWM 워드 / 번호)
sup        	X	6	507	1
IDLE       	R	0	251	5
imu        	B	3	251	4
ctrl       	B	2	507	3
telem      	B	1	1019	2
```

`vTaskListTasks` 열 읽는 법 (tasks.c 7396-7545):

| 열 | 내용 | 출처 |
|---|---|---|
| 이름 | `configMAX_TASK_NAME_LEN - 1` = 11 칸으로 공백 패딩 | `prvWriteNameToBuffer` |
| 상태 | `X` Running (호출한 태스크 자신), `R` Ready, `B` Blocked, `S` Suspended, `D` Deleted (idle 이 아직 정리 안 함) | `tskRUNNING_CHAR` 등 tasks.c 140-144, switch 7459-7486 |
| 우선순위 | `uxCurrentPriority` — 상속 중이면 상속된 값 (기본값은 `uxBasePriority`, `vTaskList` 엔 안 나옴) | 7518-7526 |
| HWM | 지금까지 **가장 적게 남았던** 스택, **워드** 단위 (바이트 아님) | 헤더 주석 "number of unused StackType_t words" |
| 번호 | `xTaskNumber` = 생성 순 일련번호 | `uxTCBNumber` |

- **줄 순서는 번호순이 아니다.** `prvCallForEachTask` (tasks.c 4533-4570) 가 Ready 리스트(우선순위 6→0) → 지연 리스트 2개 → 삭제 대기 → Suspended 리스트 순으로 돈다. sup(Ready 6), IDLE(Ready 0), 그 다음 워커들. 그래서 테스트가 `"sup IDLE imu ctrl telem"` 을 확인한다.
- **워커는 Suspended 리스트에 있는데 `B` 로 나온다.** `portMAX_DELAY` 로 기다리면 커널은 타임아웃이 없으니 지연 리스트 대신 `xSuspendedTaskList` 에 넣는다 (tasks.c 8717-8722, `INCLUDE_vTaskSuspend == 1` 일 때). `vTaskGetInfo` 는 Suspended 리스트에서 찾은 태스크가 이벤트 리스트에 걸려 있거나 **알림을 기다리는 중** 이면 `eBlocked` 로 고쳐 보고한다 (tasks.c 6347-6385). 워커 셋이 imu, ctrl, telem 순서인 건 라운드 1 에서 그 순서로 막혔기 때문이다.
- **sup 은 `X`**, IDLE 은 `R` — 스냅샷을 찍는 태스크만 Running 이다 (`taskTASK_IS_RUNNING` → `eRunning`, tasks.c 6337-6341).

**HWM 이 말해 주는 것 (이 포트에선 거의 없음)** — 깊이 − HWM 이 모든 태스크에서 정확히 **5 워드** 다. telem 은 2 KB 지역 배열을 썼는데도 1024 − 1019 = 5. 이유:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">HWM 측정: 스택 바닥부터 0xa5 가 몇 바이트 이어지나 (tasks.c 6467-6480)</text>
  <text x="10" y="48" fill="currentColor">POSIX 포트 (telem, 1024 워드)</text>
  <rect x="10" y="58" width="320" height="40" class="f-mut" stroke="currentColor"/>
  <text x="20" y="83" fill="currentColor" font-family="monospace">a5 a5 a5 … a5 (1019 워드)</text>
  <rect x="330" y="58" width="84" height="40" class="f-acc" stroke="currentColor"/>
  <text x="338" y="76" fill="currentColor" font-size="12">Thread_t</text><text x="338" y="92" fill="currentColor" font-size="12">5 워드</text>
  <text x="10" y="116" fill="currentColor" font-size="12">pxStack(낮은 주소) ← 바닥</text><text x="340" y="116" fill="currentColor" font-size="12">꼭대기 →</text>
  <rect x="440" y="58" width="290" height="40" class="f-ok" stroke="currentColor"/>
  <text x="450" y="76" fill="currentColor" font-size="12">pthread 스택 536576 B (OS 할당)</text>
  <text x="450" y="92" fill="currentColor" font-size="12">태스크 코드 · 지역 변수는 여기서 돈다</text>
  <text x="10" y="140" fill="currentColor">→ 커널이 만든 스택 배열엔 port.c 226 의 Thread_t 40 B 만 쓰인다 → HWM = 깊이 − 5, 사용량과 무관</text>
  <text x="10" y="180" fill="currentColor">Cortex-M4F (같은 1024 워드 = 4 KB)</text>
  <rect x="10" y="190" width="300" height="40" class="f-ok" stroke="currentColor"/>
  <text x="20" y="215" fill="currentColor" font-family="monospace">a5 a5 … (남은 만큼 = HWM)</text>
  <rect x="310" y="190" width="250" height="40" class="f-warn" stroke="currentColor"/>
  <text x="318" y="208" fill="currentColor" font-size="12">태스크가 실제로 쓴 깊이</text><text x="318" y="224" fill="currentColor" font-size="12">(지역 배열, 호출 체인, ISR 프레임)</text>
  <rect x="560" y="190" width="170" height="40" class="f-acc" stroke="currentColor"/>
  <text x="566" y="208" fill="currentColor" font-size="12">초기 예외 프레임</text><text x="566" y="224" fill="currentColor" font-size="12">xPSR PC LR r12–r0 …</text>
  <text x="10" y="256" fill="currentColor">→ 태스크 코드가 이 배열 위에서 돈다 (PSP). 2 KB 지역 배열 = HWM 이 512 워드 줄어든다.</text>
  <text x="10" y="280" fill="currentColor">→ 그래서 "HWM 실측 + 여유 20~30%" 로 스택을 정하는 방법은 타깃 보드에서만 의미가 있다.</text>
</svg>
```

POSIX 포트의 `pxPortInitialiseStack` 은 스택 꼭대기에 `Thread_t` (pthread 핸들, 함수, 인자, xDying, ev = 8 B × 5) 를 놓고 `pthread_create` 를 **기본 속성** 으로 부른다 (port.c 82-89, 226-227, 238-245). `pthread_attr_setstack` 이 없으니 스레드는 OS 가 준 자기 스택에서 돈다. 반면 Cortex-M4F 포트는 같은 함수에서 "인터럽트가 막 저장한 것 같은" 가짜 예외 프레임(xPSR `0x01000000`, PC = 태스크 함수, LR, r12·r3–r1, r0 = 인자, EXC_RETURN, r11–r4)을 스택 배열에 쓰고 그 위에서 태스크가 돈다 (portable/GCC/ARM_CM4F/port.c 202-231). 연습 4 가 16 KB 지역 배열을 쓰는 300 워드 태스크로 이걸 직접 증명한다.

### Part C — vTaskPrioritySet: 무엇이 언제 바뀌나

```c
static UBaseType_t set_prio(TaskHandle_t h, UBaseType_t p) {
    UBaseType_t old = uxTaskPriorityGet(h);
    vTaskPrioritySet(h, p);
    return old;
}
```

`vTaskPrioritySet` (tasks.c 2844-3040) 이 하는 일을 경우별로:

| 경우 | 전환? | 소스 | 이 레슨에서 본 것 |
|---|---|---|---|
| 남(READY)을 **지금 실행 중인 태스크보다 높게** | 즉시 | 2895-2905 `uxNewPriority > pxCurrentTCB->uxPriority` → yield | 연습 1: telem 1→3 (sup 2) → 호출 안에서 `T` |
| 실행 중인 **나를 내림** | 즉시 (더 높은 READY 가 있으면 그쪽으로) | 2925-2935 | `let_workers_run`, 연습 1 `I|` |
| 나를 올림 / 남을 내림 | 없음 | 2910-2914, 2937-2942 주석 | 연습 1: sup 2→3, ctrl(2) 안 돎 |
| 막힌(Blocked)·Suspended 태스크 | 없음 — 숫자만 바뀜 | 2984-2987: ready list 에 있을 때만 리스트를 옮긴다 | Part C: telem 4, 여전히 `B` |
| 뮤텍스로 우선순위 **상속 중** | 기본값만 바뀌고 현재 값은 상속값 유지(새 값이 더 크면 갱신) | 2951-2964 | RTOS-M04 |

실행 결과 (solution 출력 발췌):

```text
== Part C: vTaskPrioritySet 으로 순서 바꾸기 (지상국 로그 덤프 요청)
PASS  old == 1
PASS  uxTaskPriorityGet(g_w[W_TELEM].h) == 4
-- telem boosted to 4 (still blocked) (uxTaskGetSystemState, 번호순)
    2 telem       B     4    4        1019 = 8152
PASS  rt && rt->st == 'B' && rt->prio == 4 && rt->base == 4
round 2 run order: "TIC"
round 3 run order: "ICT"
full log: "ICTTICICT"
29/29 passed
```

펌웨어에서 이런 일시 승격은 실제로 쓴다: 로그 덤프 · OTA 수신 · 플래시 쓰기 마감 같은 "잠깐 급한 일". 하지만 **imu 보다 위로 올렸다는 건 그 동안 IMU 샘플이 늦을 수 있다는 뜻** 이다. telem 이 4 인 동안 오래 돌면 1 kHz 샘플이 밀린다. 그래서 실무에선 (1) 승격 구간을 짧게, (2) 올리는 대신 덤프 작업을 잘게 쪼개거나, (3) 우선순위 설계를 RMA(RTOS-M11)로 검증한다. 공유 자원 때문에 올리는 것이라면 손으로 하지 말고 뮤텍스의 우선순위 상속(RTOS-M04)에 맡긴다.

### POSIX 포트에서 진짜가 아닌 것 — `demo_freertos_posix_reality.c`

<!-- code: demo_freertos_posix_reality.c -->

실제 출력 (3회, 다른 작성 에이전트들이 돌던 바쁜 맥 — 숫자는 매번 다르다):

```text
  task helper runs on pthread "helper" (tid 8951081), pthread stack 536576 B
  task demo   runs on pthread "demo" (tid 8951080), pthread stack 536576 B
busy 1000 ms wall clock -> 419 ticks (configTICK_RATE_HZ = 1000)
vTaskDelay(100 ticks) -> 172.36 ms wall clock
vTaskDelay(1) x20 -> min 0.719 ms, max 4.976 ms
  task helper runs on pthread "helper" (tid 8951477), pthread stack 536576 B
  task demo   runs on pthread "demo" (tid 8951476), pthread stack 536576 B
busy 1000 ms wall clock -> 476 ticks (configTICK_RATE_HZ = 1000)
vTaskDelay(100 ticks) -> 192.61 ms wall clock
vTaskDelay(1) x20 -> min 1.365 ms, max 2.179 ms
  task helper runs on pthread "helper" (tid 8951871), pthread stack 536576 B
  task demo   runs on pthread "demo" (tid 8951870), pthread stack 536576 B
busy 1000 ms wall clock -> 567 ticks (configTICK_RATE_HZ = 1000)
vTaskDelay(100 ticks) -> 144.10 ms wall clock
vTaskDelay(1) x20 -> min 1.235 ms, max 1.516 ms
```

- **태스크 = pthread**: 스레드 이름이 태스크 이름이다 (`prvWaitForStart` 가 `pthread_setname_np`, port.c 558). lldb/Instruments 에서 태스크별 백트레이스를 볼 수 있는 이유. 스택은 macOS 보조 스레드 기본값 536576 B (512 KB + 12 KB — 관찰값).
- **틱은 느리다**: "Scheduler timer" 스레드가 `pthread_kill(SIGALRM)` 후 `usleep(1000)` (port.c 460-469) — 잠든 시간 + 깨어나는 지연 + 신호 처리가 쌓여 1 초에 419~567 틱. `vTaskDelay(100)` 은 144~193 ms 였다. MCU 의 SysTick 은 하드웨어 카운터라 이런 누적이 없다.
- **`vTaskDelay(1)` 은 0.72 ms 일 수도 있다**: "다음 틱까지" 이므로 1 틱 미만도 정상이다 (MCU 에서도 그렇다 — 최소 지연을 보장하려면 `vTaskDelay(n + 1)`, RTOS-B06).
- 그래서 starter/solution/practice 는 시간 값을 **찍지도 검사하지도 않는다**. 우선순위·상태·반환값·HWM(결정적)만 본다. 그 결과 10회 반복 + CPU 부하 12개를 걸고 10회 반복에서 출력이 바이트 단위로 같았다.

**새니타이저** (자기 파일 + 커널 모두 `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer` 로 다시 빌드): solution 29/29, practice 19/19, 보고 0 줄 — ASan 은 pthread 마다 따로 스택을 추적하므로 이 포트와 잘 맞는다 (ucontext 로 스택을 바꿔치기하는 RTOS-B03 과 다르다). **TSan 은 이 포트와 맞지 않는다**: 시작하자마자 포트 자체의 데이터 경쟁을 보고하고(`prvTimerTickHandler` 가 락 없이 `pxCurrentTCB` 읽기, port.c 466 ↔ `vTaskSwitchContext` 쓰기, tasks.c 5273), Part A 의 8개 PASS 뒤 첫 `vTaskDelay` 에서 진행이 멈춰 20 초 timeout 으로 끝났다. 멈춘 원인은 TSan 이 비동기 신호(SIGALRM) 전달을 늦추는 방식과 관련 있다고 추정한다 (미검증).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
SRC=solution; cc "${CF[@]}" -Wall -Wextra -Werror -c $SRC.c hooks.c && cc -pthread $SRC.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o $SRC && ./$SRC
# 29/29 passed, exit 0 — 10회 반복 + 부하 상태 10회 모두 같은 출력
# ASan+UBSan: 커널 오브젝트까지 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer 로 다시 컴파일·링크 → 29/29, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `FreeRTOSConfig.h` 만 고치고 앱 파일만 다시 컴파일 | 설정이 TCB 모양을 바꾼다 (`configUSE_TRACE_FACILITY`, `configUSE_MUTEXES`, `configMAX_TASK_NAME_LEN` …). 커널 .o 와 앱이 다른 TCB/`TaskStatus_t` 를 가정하면 조용한 메모리 오염 | 설정을 바꾸면 커널 오브젝트까지 전부 재빌드 (Makefile 의존성에 헤더 포함) |
| `xTaskCreate(fn, "x", 1024 /* 바이트라고 생각 */, …)` | 단위가 워드. CM4 에선 4 KB, 이 포트에선 8 KB 를 잡는다 — RAM 이 4배·8배로 샌다. 반대로 바이트로 계산해 작게 주면 오버플로 | `512 / sizeof(StackType_t)` 처럼 의도를 적거나 워드로 생각 |
| `xTaskCreate` 반환값 무시 | 힙 부족이면 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`, 핸들은 NULL. 나중에 `vTaskPrioritySet(NULL, …)` 하면 **자기 자신** 이 바뀐다 | `if (xTaskCreate(...) != pdPASS) 에러 처리;` + malloc 실패 훅 |
| 우선순위를 `configMAX_PRIORITIES` 이상으로 | `configASSERT` (tasks.c 1903), assert 가 없으면 최댓값으로 조용히 깎인다 (1905-1908) — 두 태스크가 같은 우선순위가 돼 타임 슬라이싱 | 우선순위를 enum 으로 한곳에 정의, `_Static_assert(PRIO_MAX < configMAX_PRIORITIES, "")` |
| `vTaskList(buf)` 에 작은 버퍼 | 매크로가 길이로 `configSTATS_BUFFER_MAX_LENGTH`(기본 0xFFFF)를 넘긴다 (task.h 2416, FreeRTOS.h 2865) → 태스크가 늘면 오버플로 | `vTaskListTasks(buf, sizeof buf)` (연습 3) |
| 시험 코드에서 `vTaskDelay(1)` 로 "다른 태스크가 끝났겠지" | 시간 가정. 호스트에서 1 틱은 0.7~5 ms 로 흔들리고, MCU 에서도 부하에 따라 깨진다 | 우선순위 조작, 알림·세마포어로 완료를 **신호** 받는다 |
| 태스크 함수에서 `return` | 돌아갈 곳이 없다. POSIX 포트는 `configASSERT(pdFALSE)` (port.c 568), CM4 는 `prvTaskExitError` 로 멈춤 | `for (;;)` 또는 끝에 `vTaskDelete(NULL)` |
| 여러 태스크에서 `printf` (POSIX 포트) | 커널이 pthread 뮤텍스를 쥔 스레드를 잠재울 수 있어 교착 가능 (port.c 43-50 주석) | 출력은 한 태스크에서만 (이 레슨: sup) |
| POSIX 포트 / 시뮬레이터의 HWM 으로 스택 크기 결정 | HWM = 깊이 − 5 로 고정 (Part B, 연습 4). 실제 사용량이 반영되지 않는다 | 타깃 보드에서 최악 경로를 돌린 뒤 HWM + 여유 (RTOS-M08) |

## 8. 면접 Q&A

<details>
<summary>Q1. vTaskList 출력을 보여 주며 "각 열이 뭔가요?" — 그리고 무한 대기 중인 태스크는 왜 S 가 아니라 B 인가요?</summary>

이름 / 상태 / 현재 우선순위 / 스택 HWM(**워드**) / 태스크 번호. 상태는 `X` = 지금 실행 중(= `vTaskList` 를 부른 태스크 자신), `R` = 실행 가능하지만 CPU 를 못 받음, `B` = 지연이나 이벤트(큐·세마포어·알림) 대기, `S` = `vTaskSuspend` 로 정지, `D` = 스스로 삭제했지만 idle 이 아직 메모리를 회수 안 함. `portMAX_DELAY` 로 기다리는 태스크는 커널 내부적으로 Suspended 리스트에 들어가지만(타임아웃이 없으니 지연 리스트에 둘 이유가 없다, tasks.c 8717-8722) `vTaskGetInfo` 가 "이벤트 리스트에 걸려 있거나 알림 대기 중이면 Blocked" 로 고쳐 보고한다 (6347-6385). 꼬리질문 "우선순위 열이 기본값과 다를 수 있나?" → 뮤텍스 상속 중이면 `vTaskList` 는 상속된 현재 값, `TaskStatus_t.uxBasePriority` 가 원래 값.

</details>

<details>
<summary>Q2. 태스크 스택 크기는 어떻게 정했나요? HWM 은 어떻게 측정되나요?</summary>

생성 때 스택 전체를 `0xa5` 로 채우고(tasks.c 1832-1837), HWM 은 스택 바닥(오버플로 쪽)부터 `0xa5` 가 연속으로 몇 바이트 남았는지 세어 워드로 나눈 값이다 (`prvTaskCheckFreeStackSpace`, 6467-6480). 즉 "생성 이후 **가장 깊이** 썼을 때 남은 여유". 절차: 넉넉히 잡고 → 최악 경로(가장 깊은 호출, 가장 큰 `snprintf`, 중첩 ISR 이 그 태스크 스택에 쌓이는 경우 — Cortex-M 은 ISR 이 MSP 를 쓰지만 예외 진입 프레임 8~26 워드는 PSP 에 쌓인다)를 시험 중에 다 밟게 하고 → HWM 을 읽어 여유 20~30% 를 남기고 줄인다. 함정: 시험이 최악 경로를 안 밟으면 HWM 은 낙관적이다 → 정적 분석(`-fstack-usage`, RTOS-M08)과 병행. 그리고 POSIX 포트·시뮬레이터에선 HWM 이 의미가 없다 (깊이 − 5 고정, 연습 4).

</details>

<details>
<summary>Q3. vTaskPrioritySet 을 부르면 즉시 컨텍스트 스위치가 일어나나요?</summary>

경우에 따라 다르다. (1) READY 인 다른 태스크를 **지금 실행 중인 태스크보다 높게** 올리면 즉시 (tasks.c 2895-2905). (2) 실행 중인 자기 자신을 내렸는데 더 높은 READY 가 있으면 즉시 (2925-2935). 둘 다 호출이 돌아오기 전에 다른 태스크가 돈다 — 연습 1 의 로그 `"I|T||C"` 에서 `|` (sup 이 호출 직후 남긴 표시) 앞에 I, T 가 찍혔다. (3) 자기를 올리거나 남을 내리면 전환 없음. (4) Blocked/Suspended 태스크는 숫자만 바뀌고, 깨어날 때 새 우선순위로 ready list 에 들어간다. 꼬리: "ISR 에서 불러도 되나?" → `FromISR` 판이 없다. ISR 은 알림·세마포어로 태스크를 깨우고 우선순위는 태스크 문맥에서 바꾼다.

</details>

<details>
<summary>Q4. FreeRTOS 를 PC(POSIX/Windows 포트)에서 돌려 시험하는 건 의미가 있나요? 무엇은 믿고 무엇은 못 믿나요?</summary>

의미 있다 — 커널 코드(tasks.c, queue.c 등)는 MCU 와 **같은 소스** 이고, 스케줄링 결정·상태 전이·API 반환값은 같다. 그래서 프로토콜 상태 기계, 태스크 간 순서, 에러 경로를 CI 에서 단위·통합 시험하기 좋다. 못 믿는 것: (1) 시간 — 1 초에 틱 419~567 개, `vTaskDelay(100)` 이 144~193 ms (데모 실측), (2) 스택 — 코드가 pthread 스택에서 돌아 HWM·오버플로 검사가 무의미, (3) 인터럽트 — 진짜 NVIC 우선순위·`configMAX_SYSCALL_INTERRUPT_PRIORITY`·중첩이 없다 (틱만 SIGALRM), (4) 캐시·DMA·메모리 배리어. 그래서 PC 시험은 "시간에 기대지 않는 검사" 로 짠다 — 이 레슨의 `let_workers_run` 처럼. 타이밍·스택은 타깃 보드 + 트레이스(RTOS-A06)로 확인.

</details>

<details>
<summary>Q5. configMAX_PRIORITIES 를 32 나 56 처럼 크게 잡으면 안 되나요?</summary>

우선순위마다 ready list(`List_t`) 하나가 RAM 에 생기고(32비트에서 개당 20 B, 무결성 검사 끔 기준), 일반(C) 선택 방식이면 `taskSELECT_HIGHEST_PRIORITY_TASK` 가 가장 높은 우선순위부터 빈 리스트를 하나씩 내려가며 찾는다 (tasks.c 197-212). 대개 `uxTopReadyPriority` 덕분에 짧지만 최악은 우선순위 개수에 비례한다. 포트 최적화 방식(Cortex-M 의 CLZ, RTOS-B04)은 O(1) 이지만 최대 32 개로 제한된다. 실무에선 5~10 단계면 충분하다: idle, 로깅, 통신, 제어, 샘플링, 감시 정도로 묶고 같은 단계 안에선 라운드 로빈. 숫자가 많다고 설계가 좋아지지 않는다 — RMA(RTOS-M11)로 근거를 댄다.

</details>

<details>
<summary>Q6. idle 태스크는 무슨 일을 하나요? idle 훅에서 하면 안 되는 일은?</summary>

READY 인 태스크가 없을 때 도는 우선순위 0 태스크다 (`vTaskStartScheduler` 가 만든다). 하는 일: 스스로 `vTaskDelete(NULL)` 한 태스크의 TCB·스택 회수(`prvCheckTasksWaitingTermination`, tasks.c 5934), 같은 우선순위 0 태스크가 있으면 양보(`configIDLE_SHOULD_YIELD`, 5953-5965), 그리고 idle 훅 호출(5969-5973). 훅에서 **막히면 안 된다** — idle 은 항상 READY 여야 한다 (모두 막힌 순간에 돌 태스크가 없어진다). 훅의 정석: `__WFI()` 로 잠들기, 워치독 킥 조건 모으기, 저우선 백그라운드 통계. 꼬리: "높은 우선순위 태스크가 바쁘게 돌면?" → idle 이 굶어 삭제된 태스크 메모리가 안 풀리고, idle 훅 기반 저전력·워치독도 멈춘다 — 워치독이 리셋으로 알려 주는 게 정상 설계.

</details>

<details>
<summary>Q7. portMAX_DELAY 로 ulTaskNotifyTake 를 했는데 0 이 돌아왔습니다. 가능한가요?</summary>

가능하다. 기다리는 동안 누가 그 태스크를 `vTaskSuspend` 했다가 `vTaskResume` 하면, V11.3.2 의 `vTaskSuspend` 는 "알림 대기 중" 표시를 지우고(tasks.c 3235-3247 — "now suspended, so no notification was received") Resume 된 태스크는 `ulTaskGenericNotifyTake` 의 뒷부분으로 돌아와 값 0 을 읽고 반환한다 (7862-7885). 연습 2 실측: `ctrl spurious wakes: 1`. 교훈: 무한 대기여도 **반환값을 확인** 하고, 남의 태스크를 `vTaskSuspend` 로 멈추는 설계(어디서 멈출지 모름 — 뮤텍스를 쥔 채일 수도)는 피한다. 정지·재개가 필요하면 그 태스크가 스스로 안전한 지점에서 기다리게 한다.

</details>

## 9. 추가 연습

### 연습 1 — vTaskPrioritySet 이 "그 자리에서" 선점하는 두 경우

sup(6) 이 imu(3) 를 깨운 뒤 자기를 2 로 내리고 `|` 를 남긴다. 이어서 telem(1) 을 깨우고 telem 을 3 으로 올린 뒤 `|`. ctrl(2) 을 깨우고 자기를 3 으로 올린 뒤 `|`. 마지막에 원래대로 돌리고 CPU 를 내준다. 로그는?

<details>
<summary>답 보기</summary>

`"I|T||C"`. ① 자기를 2 로 내리는 호출 안에서 imu(3) 가 돈다 → `I|`. ② telem 을 3 으로 올리면 실행 중인 sup(2) 보다 높아 그 호출 안에서 telem 이 돈다 → `T|`. ③ sup 을 3 으로 **올리는** 건 전환 사유가 아니고 ctrl(2) 은 더 낮으니 그냥 `|`. ④ `let_run` 에서 ctrl 이 돈다 → `C`. 소스 근거는 Q3 의 표. 꼬리: ③ 에서 sup 을 2 로 둔 채 ctrl(2) 을 깨우면? → 같은 우선순위라 즉시 전환은 없고, 다음 틱의 타임 슬라이싱에서 교대된다 — 시간에 달린 일이라 practice 는 검사하지 않는다.

</details>

### 연습 2 — 상태 글자 X R B S 를 한 화면에

imu 를 깨우되 CPU 는 안 주고, ctrl 을 `vTaskSuspend`, telem 은 알림 대기, `sleeper` 는 `vTaskDelay(100000)`. `vTaskListTasks` 를 찍어 보라. 그 다음 ctrl 을 **알림 없이** `vTaskResume` 하고 CPU 를 주면?

<details>
<summary>답 보기</summary>

실제 출력:

```text
sup        	X	6	507	1
imu        	R	3	251	4
IDLE       	R	0	251	7
sleeper    	B	1	251	5
hog        	B	1	295	6
telem      	B	1	1019	2
ctrl       	S	2	507	3
```

imu 는 깨어났지만 sup(6) 이 돌고 있어 `R`. sleeper 는 지연 리스트라 `B`, telem·hog 는 Suspended 리스트에 있지만 알림 대기라 `B`, ctrl 만 진짜 `S` (vTaskSuspend 가 알림 대기 표시를 지웠다). 줄 순서는 Ready(높→낮) → 지연 → Suspended 리스트. Resume 하면 ctrl 은 알림 없이 `ulTaskNotifyTake` 에서 0 을 받고 돈다 → 로그 `"IC"`, `spurious == 1` (Q7).

</details>

### 연습 3 — 배열 · 버퍼가 작을 때

태스크 7개인데 `uxTaskGetSystemState(ts, 3, NULL)` 은? `char small[40]` 에 `vTaskListTasks(small, sizeof small)` 은?

<details>
<summary>답 보기</summary>

`uxTaskGetSystemState` 는 배열이 태스크 수보다 작으면 **아무것도 안 쓰고 0** (tasks.c 4600). 그래서 "몇 개 왔나" 가 아니라 "0 이면 배열을 키워라" 로 쓴다 (`uxTaskGetNumberOfTasks()` + 여유로 할당). `vTaskListTasks(small, 40)` 실제 결과: `"sup        \tX\t6\t507\t1\r\nIDLE       \tR\t0\t"` (39자) — 넘치지는 않지만 둘째 줄이 **중간에 잘린다** (이름 칸 12 자리가 남았는지 확인하고, 나머지는 `snprintf` 로 남은 길이만큼 쓴다, 7487-7531). 파서가 잘린 줄을 정상 줄로 읽지 않게 해야 한다. 그리고 길이를 안 받는 옛 `vTaskList(buf)` 는 0xFFFF 를 넘기므로 이 상황에서 **넘친다** — 쓰지 말 것.

</details>

### 연습 4 — 16 KB 를 쓰는 300 워드 태스크

`hog` 태스크(깊이 300 워드 = 2400 B)가 `volatile char big[16 * 1024]` 를 채운다. Cortex-M 이라면? 이 포트에서 HWM 과 오버플로 훅은? `big` 의 주소는 FreeRTOS 스택 배열 안인가?

<details>
<summary>답 보기</summary>

Cortex-M 이면 확실한 스택 오버플로다 — 2400 B 스택 아래 13 KB 이상을 덮어쓴다 (다른 태스크 TCB·힙). 검사 방법 2 가 다음 전환 때 바닥 패턴이 깨진 걸 보고 훅을 부르거나, 그 전에 HardFault. 이 포트 실측: `hwm 295 words, depth-hwm = 5 words`, `big[]` 주소는 `[pxStackBase, +2400 B)` **밖**, 실제 스레드 스택 536576 B, 오버플로 훅 0 회. 깊이 − HWM = 5 = `sizeof(Thread_t)` 40 B / 8 — 커널 스택 배열에 쓰인 건 포트의 스레드 정보뿐이다. 결론: 이 포트에서 스택 크기는 "Thread_t 를 담을 만큼" 만 의미가 있고, 스택 예산은 타깃에서 잰다 (RTOS-M08, 오버플로 탐지 자체는 RTOS-M09).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
SRC=practice; cc "${CF[@]}" -Wall -Wextra -Werror -c $SRC.c hooks.c && cc -pthread $SRC.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o $SRC && ./$SRC
# 19/19 passed, exit 0 (10회 반복 + 부하 상태 10회 같은 출력, ASan+UBSan 보고 없음)
```

## 10. 핵심 정리

- FreeRTOS = 커널 소스 + 내 `FreeRTOSConfig.h`. 설정은 컴파일 시점에 TCB 모양까지 바꾸므로 **바꾸면 커널까지 재빌드**.
- 스택 깊이는 **워드**(`StackType_t`) 단위: CM4 4 B, POSIX 포트 8 B. HWM = 생성 후 가장 적게 남았던 여유(워드), `0xa5` 패턴을 바닥부터 세어 잰다.
- 우선순위는 큰 수가 급함(0 = idle). 실행 순서는 만든·깨운 순서가 아니라 **READY 중 최고 우선순위** — 실측 `"ICT"`, telem 을 4 로 올리면 `"TIC"`.
- `vTaskList` 열: 이름 / X R B S D / 현재 우선순위 / HWM 워드 / 번호. 무한 대기는 내부적으로 Suspended 리스트지만 `B` 로 보고. 버퍼 길이는 `vTaskListTasks(buf, len)` 로.
- `vTaskPrioritySet`: 남을 실행 중인 태스크 위로 올리거나 나를 내리면 **호출 안에서** 전환, 막힌 태스크는 숫자만 바뀜.
- POSIX 포트 = 태스크마다 pthread, 하나만 실행, 틱은 타이머 스레드의 SIGALRM. 순서·반환값은 진짜, **시간(1 초에 틱 419~567)과 스택(HWM = 깊이 − 5 고정)은 가짜** → 시험은 시간에 기대지 않게.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (`~/.esn_scratch/FreeRTOS-Kernel`, <https://github.com/FreeRTOS/FreeRTOS-Kernel>): `tasks.c` (상태 글자 140-144, 스택 채우기 121·130-135·1832-1837, 우선순위 검사 1903-1908, 번호 2088-2093, `vTaskPrioritySet` 2844-3040, `vTaskSuspend` 알림 처리 3235-3247, `prvCreateIdleTasks` 3588, `prvCallForEachTask` 4533-4570, `uxTaskGetSystemState` 4589-4612, `vTaskGetInfo` 6285-6420, `prvTaskCheckFreeStackSpace` 6467-6480, `vTaskListTasks` 7396-7545, `ulTaskGenericNotifyTake` 7797-7890, `prvAddCurrentTaskToDelayedList` 8685-8722, idle 5934-5973), `include/task.h` (`TaskStatus_t` 167-185, `vTaskList` 매크로 2416), `include/stack_macros.h` 99-118, `include/FreeRTOS.h` 2865-2873
- POSIX 포트 `portable/ThirdParty/GCC/Posix/port.c` (모델 설명 30-56, `Thread_t` 82-89, `pxPortInitialiseStack` 211-255, yield 377-403, 타이머 스레드 452-485, 틱 핸들러 488-517, `prvWaitForStart` 545-571, 스레드 교대 574-631), `portmacro.h` (`StackType_t` = unsigned long, `portSTACK_GROWTH` −1)
- Cortex-M4F 포트 `portable/GCC/ARM_CM4F/port.c` 202-231 (초기 예외 프레임), `portmacro.h` (`StackType_t` = uint32_t)
- FreeRTOS 문서 — Customisation (FreeRTOSConfig.h) <https://www.freertos.org/a00110.html>, `uxTaskGetSystemState` / `vTaskList` / `uxTaskGetStackHighWaterMark` API 페이지, POSIX/Linux 시뮬레이터 <https://www.freertos.org/FreeRTOS-simulator-for-Linux.html>
- Richard Barry, *Mastering the FreeRTOS Real Time Kernel* — Task Management 장 (우선순위, 상태, idle 태스크, `vTaskPrioritySet` 예제) <https://www.freertos.org/Documentation/RTOS_book.html>
- Zephyr Threads 문서 <https://docs.zephyrproject.org/latest/kernel/services/threads/index.html>, CMSIS-RTOS2 Thread Management <https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__ThreadMgmt.html> (표의 Zephyr/CMSIS 칸은 문서 기준, 이 레슨에서 실행 안 함)
- 관련 레슨: RTOS-B02 (TCB·상태 전이 — `xStateListItem`/`xEventListItem`), RTOS-B04 (ready list 비트맵·CLZ), RTOS-B05 (선점·타임 슬라이싱), RTOS-B06 (지연 리스트), RTOS-B08 (같은 포트로 ISR→태스크), RTOS-M01 (태스크 알림), RTOS-M04 (우선순위 상속), RTOS-M08/M09 (스택 예산·오버플로 탐지), RTOS-M10 (힙), RTOS-M11 (RMA), RTOS-M13 (FreeRTOS 내부 읽기)
