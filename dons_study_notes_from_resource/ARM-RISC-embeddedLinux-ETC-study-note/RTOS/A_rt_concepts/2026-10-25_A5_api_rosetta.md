# A5. API 로제타석 — 같은 개념, 다섯 개의 이름: FreeRTOS · Zephyr · ThreadX · CMSIS-RTOS2 · POSIX

> **이 노트를 다 읽으면**: 태스크·지연·세마포어·뮤텍스·큐·이벤트·타이머·ISR 호출·임계 구역을 다섯 RTOS API로 바로 번역할 수 있다 · 우선순위 방향, 시간 단위(tick·ms·`k_timeout_t`), ISR 전용 API, 반환값 규약, 스택 단위, 뮤텍스 기본값처럼 "이름은 같은데 뜻이 다른" 함정을 실행 결과로 설명할 수 있다 · 같은 producer → queue → consumer 프로그램을 FreeRTOS·Zephyr·ThreadX·POSIX(Linux, Zephyr)에서 돌려 나란히 비교할 수 있다 · 작은 OSAL을 직접 만들고 그 한계와 CMSIS-RTOS2의 역할을 말할 수 있다
> **왜 필요한가**: 회사마다 RTOS가 다르다. 면접에서 "우리는 Zephyr를 쓰는데 FreeRTOS 경험을 어떻게 옮기나?", 입사 후 "벤더 SDK는 ThreadX, 우리 앱은 FreeRTOS, 새 보드는 Zephyr"라는 장면이 실제로 나온다. 번역은 이름 치환이 아니라 **의미 차이를 아는 것**이다.
> **Don 기준 난이도**: 태스크·큐·세마포어·ISR·임계 구역 개념은 SSD 펌웨어 executive에서 이미 안다 / 각 RTOS의 실제 함수 이름, 우선순위 숫자 방향, 시간 단위 변환, ISR 호출 규칙, 반환값, OSAL 설계의 함정을 새로 배운다
> **선행 노트**: A3(RTOS 구성 요소), A4(태스크 모델). pthread·futex·PI 뮤텍스 기본은 EL A8, Linux 커널 락은 EL F4, 시간은 EL F8. 이어지는 노트: D1~D10(동기화 깊이), G3(FreeRTOS 객체 실전), H6(Zephyr 커널 서비스), I2(ThreadX), I6(POSIX on RTOS)

---

## 0. 큰 그림 — "번역 능력"이란 무엇인가

Don이 SSD 컨트롤러 펌웨어에서 쓰던 executive를 떠올려 보자. 코어 사이 메시지 큐, "이 커맨드 끝나면 깨워 줘"라는 이벤트, 공유 테이블을 지키는 락이 있었다. 이름은 사내 규칙(예: `MsgSend`, `EvtWait`)이었지만 하는 일은 어느 RTOS에나 있는 것과 같다. RTOS 사이를 옮겨 다니는 것도 똑같다. **개념은 열 개 남짓으로 같고, 이름과 세부 의미가 다르다.**

문제는 "세부 의미" 쪽이다. 예를 들면 이렇다.

- FreeRTOS에서 `xQueueSend()`가 0을 돌려주면 **실패**다. Zephyr에서 `k_msgq_put()`이 0을 돌려주면 **성공**이다. `if (send(...))`를 그대로 옮기면 논리가 뒤집힌다.
- FreeRTOS 우선순위 7은 "높다". Zephyr 우선순위 7은 0보다 "낮다". ThreadX에서는 0이 가장 높다.
- FreeRTOS `xTaskCreate()`의 스택 인자 256은 **256 word = 1 KB**다. Zephyr·ThreadX·CMSIS·POSIX에서 같은 자리의 숫자는 **바이트**다.
- Zephyr `k_timer` 콜백은 **ISR 안에서** 돈다. FreeRTOS 타이머 콜백은 **daemon 태스크 안에서** 돈다. 같은 "타이머 콜백"인데 그 안에서 할 수 있는 일이 다르다.

이 노트는 이런 차이를 표로 외우는 대신 **직접 돌려서 눈으로 확인**한다. 같은 프로그램을 다섯 세계에서 돌리고, 일부러 틀린 호출을 해서 어떻게 터지는지 본다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="14" width="640" height="38" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="340" y="38" font-size="13" text-anchor="middle">애플리케이션 코드: 센서 태스크 · 제어 루프 · 텔레메트리</text>
<rect x="20" y="82" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="120" y="106" font-size="12" text-anchor="middle">사내 OSAL (os_queue_send …)</text>
<text x="120" y="123" font-size="12" text-anchor="middle">회사마다 다름</text>
<rect x="240" y="82" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="340" y="106" font-size="12" text-anchor="middle">CMSIS-RTOS2 (osXxx)</text>
<text x="340" y="123" font-size="12" text-anchor="middle">Arm 표준 OSAL</text>
<rect x="460" y="82" width="200" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="560" y="106" font-size="12" text-anchor="middle">POSIX (pthread · mq)</text>
<text x="560" y="123" font-size="12" text-anchor="middle">PSE51/52 프로파일</text>
<rect x="20" y="196" width="124" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="82" y="218" font-size="11" text-anchor="middle" font-weight="bold">FreeRTOS</text>
<text x="82" y="234" font-size="11" text-anchor="middle">xQueueSend</text>
<rect x="149" y="196" width="124" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="211" y="218" font-size="11" text-anchor="middle" font-weight="bold">Zephyr</text>
<text x="211" y="234" font-size="11" text-anchor="middle">k_msgq_put</text>
<rect x="278" y="196" width="124" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="340" y="218" font-size="11" text-anchor="middle" font-weight="bold">ThreadX</text>
<text x="340" y="234" font-size="11" text-anchor="middle">tx_queue_send</text>
<rect x="407" y="196" width="124" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="469" y="218" font-size="11" text-anchor="middle" font-weight="bold">RTX5</text>
<text x="469" y="234" font-size="11" text-anchor="middle">osMessageQueuePut</text>
<rect x="536" y="196" width="124" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="598" y="218" font-size="11" text-anchor="middle" font-weight="bold">Linux</text>
<text x="598" y="234" font-size="11" text-anchor="middle">mq_send / kfifo</text>
<line x1="120" y1="140" x2="82" y2="194" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="120" y1="140" x2="211" y2="194" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="120" y1="140" x2="340" y2="194" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="340" y1="140" x2="82" y2="194" stroke="#4a7bd0" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="340" y1="140" x2="211" y2="194" stroke="#4a7bd0" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="340" y1="140" x2="469" y2="194" stroke="#4a7bd0" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="560" y1="140" x2="211" y2="194" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="560" y1="140" x2="340" y2="194" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="560" y1="140" x2="598" y2="194" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="120" y1="54" x2="120" y2="80" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="340" y1="54" x2="340" y2="80" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="560" y1="54" x2="560" y2="80" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="20" y="278" font-size="13" text-anchor="start">같은 '큐에 넣기'가 커널마다 다른 이름 · 다른 시간 단위 · 다른 반환값을 가진다</text>
<text x="20" y="300" font-size="12" text-anchor="start">실선: 구현이 있는 연결 (CMSIS-FreeRTOS 래퍼, ThreadX 저장소의 POSIX 호환 계층 등)</text>
<text x="20" y="320" font-size="12" text-anchor="start">점선: 회사가 직접 만든 OSAL이 덮는 커널 (이 노트 §4에서 FreeRTOS · Zephyr용을 직접 만든다)</text>
</svg>
```

그림 1 — 애플리케이션과 커널 사이의 번역 층. 회사마다 사내 OSAL을 두기도 하고, 표준 OSAL인 CMSIS-RTOS2나 POSIX를 쓰기도 한다. 어느 경로든 맨 아래에는 이름과 의미가 다른 커널이 있다.

용어 몇 개를 먼저 정한다.

- **OSAL(OS Abstraction Layer)**: 애플리케이션이 특정 RTOS 이름 대신 부르는 얇은 함수 층. "`os_queue_send()`를 부르면 아래에서 `xQueueSend()`나 `k_msgq_put()`으로 바뀐다".
- **CMSIS-RTOS2**: Arm이 정한 표준 RTOS API(`osThreadNew`, `osMessageQueuePut` …). Arm의 RTX5가 원래 구현이고, FreeRTOS용 래퍼(CMSIS-FreeRTOS)와 Zephyr 호환 계층이 있다. 말하자면 "업계 공용 OSAL"이다.
- **POSIX PSE51 / PSE52**: POSIX의 실시간 하위 프로파일. PSE51은 "단일 프로세스·여러 스레드·파일 시스템 없음"인 최소 실시간 시스템, PSE52는 거기에 파일 시스템 등이 붙은 실시간 컨트롤러 프로파일이다. Zephyr의 `CONFIG_POSIX_API`는 Kconfig 도움말에 "PSE51 전부와 PSE52 일부"라고 적혀 있다(I6에서 자세히).

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) 한 보드에서 **FreeRTOS V11.3.2 · Zephyr v4.4.2 · Eclipse ThreadX 6.5.2(직접 포팅)** 를 돌리고, POSIX는 Lima VM(Ubuntu, Linux 7.0, aarch64)과 Zephyr POSIX 계층 양쪽에서 돌린다. 타이밍은 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 매번 같은 숫자) 기준이다. **QEMU icount 기준이므로 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스를 모델링하지 않는다). Linux 수치는 VM 기준이다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 로제타석 표 — 개념 하나에 한 행 (+ Linux 커널 열) | — |
| §2 | 같은 프로그램을 다섯 세계에서 | 예제 1~5 |
| §3 | 의미 함정 — 우선순위·시간·ISR·반환값·스택·뮤텍스·세마포어·종료·타이머·시작 | 예제 6~11 |
| §4 | OSAL 직접 만들기 — 그리고 숨은 의미가 튀어나오는 순간 | 예제 12 |
| §5 | CMSIS-RTOS2 — 표준화된 OSAL을 Zephyr 위에서 | 예제 13 |
| §6 | 측정: 코드 크기와 큐 왕복 시간 | 예제 14~16 |
| §7 | RTOS 커널 안: "세마포어 주기"를 세 커널 소스에서 따라가기 + 대기자 깨우는 순서 | 소스 확인, 예제 17 |
| §8~ | 실무, 실수, 면접, 연습, 용어, 체크리스트 | — |

---

## 1. 로제타석 표 — 개념 하나에 한 행

먼저 전체 지도다. 아래 표의 함수 이름은 전부 소스에서 grep으로 확인했다(FreeRTOS V11.3.2 `include/`, Zephyr v4.4.2 `include/zephyr/`, ThreadX `common/inc/tx_api.h`, CMSIS-RTOS2는 Zephyr에 들어 있는 `cmsis_os2.h`). 확인에 쓴 스크립트는 이렇다(이름 222개를 한 번에 검사).

```sh
# 이름이 헤더에 없으면 MISSING을 출력한다 (일부만 발췌)
chk() { local dir=$1; shift; for n in "$@"; do grep -rqw -- "$n" $dir || echo "MISSING[$dir]: $n"; done; }
chk "$F/include $F/portable/GCC/ARM_CM3" xTaskCreate xTaskDelayUntil xSemaphoreGiveFromISR xTaskNotifyGive ...
chk "$Z/include $Z/kernel" k_thread_create k_msgq_put k_event_wait k_mem_slab_alloc k_sched_lock ...
chk "$T/common/inc" tx_thread_create tx_queue_send tx_event_flags_get tx_block_allocate ...
chk "$C" osThreadNew osMessageQueuePut osThreadFlagsSet osMemoryPoolAlloc osKernelLock ...
```

```text
MISSING[/var/tmp/rtos/A5/threadx/common/inc]: TX_INT_DISABLE
verify-done
```

출력에서 볼 것: 빠진 이름은 `TX_INT_DISABLE` 하나뿐인데, 이것은 공통 헤더가 아니라 포트 헤더(`ports/cortex_m3/gnu/inc/tx_port.h`)에 있다. 즉 표의 이름은 모두 실재한다.

### 1.1 태스크와 시간

| 개념 | FreeRTOS | Zephyr | ThreadX | CMSIS-RTOS2 | POSIX |
|---|---|---|---|---|---|
| 태스크 생성 (동적) | `xTaskCreate(fn, name, words, arg, prio, &h)` | `k_thread_create(&t, stk, bytes, entry, p1, p2, p3, prio, opt, delay)` | `tx_thread_create(&t, name, entry, in, stk, bytes, prio, thresh, slice, start)` | `osThreadNew(fn, arg, &attr)` | `pthread_create(&t, &attr, fn, arg)` |
| 태스크 생성 (정적) | `xTaskCreateStatic(…, stack_buf, &tcb)` | `K_THREAD_DEFINE(name, bytes, entry, …, prio, opt, delay)` | 항상 정적: 스택과 TCB를 호출자가 준다 | `attr.stack_mem`, `attr.cb_mem` | `pthread_attr_setstack()` |
| 스스로 끝내기 | `vTaskDelete(NULL)`<br>함수에서 return 금지 | 함수에서 return 또는 `k_thread_abort()` | return → COMPLETED 상태, 그다음 `tx_thread_delete()` | `osThreadExit()` | return 또는 `pthread_exit()` |
| 남을 끝내기 | `vTaskDelete(h)` | `k_thread_abort(tid)` | `tx_thread_terminate()` → `tx_thread_delete()` | `osThreadTerminate(id)` | `pthread_cancel(t)` (취소 지점에서) |
| 상대 지연 | `vTaskDelay(ticks)` | `k_msleep(ms)`, `k_sleep(K_MSEC(ms))` | `tx_thread_sleep(ticks)` | `osDelay(ticks)` | `nanosleep()` |
| 주기 대기 (절대 시각) | `xTaskDelayUntil(&last, period)` | `k_sleep(K_TIMEOUT_ABS_MS(t))` | 없음 — `tx_time_get()`으로 직접 계산 | `osDelayUntil(ticks)` | `clock_nanosleep(…, TIMER_ABSTIME, …)` |
| 양보 | `taskYIELD()` | `k_yield()` | `tx_thread_relinquish()` | `osThreadYield()` | `sched_yield()` |
| 일시 정지 / 재개 | `vTaskSuspend()` / `vTaskResume()` | `k_thread_suspend()` / `k_thread_resume()` | `tx_thread_suspend()` / `tx_thread_resume()` | `osThreadSuspend()` / `osThreadResume()` | 없음 |
| 우선순위 변경 | `vTaskPrioritySet()` | `k_thread_priority_set()` | `tx_thread_priority_change()` | `osThreadSetPriority()` | `pthread_setschedparam()` |
| 끝나기 기다리기 | 없음 | `k_thread_join()` | 없음 (상태 조회 `tx_thread_info_get()`) | `osThreadJoin()` | `pthread_join()` |
| tick 카운트 | `xTaskGetTickCount()` | `k_uptime_ticks()` | `tx_time_get()` | `osKernelGetTickCount()` | 없음 (tick 개념이 없다) |
| ms uptime | tick × `portTICK_PERIOD_MS` | `k_uptime_get()` (int64 ms) | 직접 환산 | tick ÷ `osKernelGetTickFreq()` | `clock_gettime(CLOCK_MONOTONIC)` |
| 고해상도 시각 | 포트·보드 몫 (이 랩은 `board_now_us()`) | `k_cycle_get_32()` | 포트 몫 | `osKernelGetSysTimerCount()` | `clock_gettime()` (해상도는 구현 몫) |

### 1.2 동기화와 통신

| 개념 | FreeRTOS | Zephyr | ThreadX | CMSIS-RTOS2 | POSIX |
|---|---|---|---|---|---|
| binary 세마포어 | `xSemaphoreCreateBinary()` (처음엔 비어 있음) | `K_SEM_DEFINE(s, 0, 1)` | `tx_semaphore_create(&s, name, 0)` (상한 없음) | `osSemaphoreNew(1, 0, NULL)` | `sem_init(&s, 0, 0)` |
| counting 세마포어 | `xSemaphoreCreateCounting(max, init)` | `k_sem_init(&s, init, limit)` — 인자 순서가 반대 | 같은 함수, 초기값만 | `osSemaphoreNew(max, init, attr)` | `sem_init(&s, 0, init)` |
| 주기 (give) | `xSemaphoreGive(s)` | `k_sem_give(&s)` | `tx_semaphore_put(&s)` | `osSemaphoreRelease(id)` | `sem_post(&s)` |
| 받기 (take) | `xSemaphoreTake(s, ticks)` | `k_sem_take(&s, timeout)` | `tx_semaphore_get(&s, ticks)` | `osSemaphoreAcquire(id, ticks)` | `sem_wait()`, `sem_timedwait()`, `sem_trywait()` |
| 뮤텍스 | `xSemaphoreCreateMutex()` + Take / Give | `k_mutex_lock(&m, timeout)` / `k_mutex_unlock()` | `tx_mutex_get()` / `tx_mutex_put()` | `osMutexAcquire()` / `osMutexRelease()` | `pthread_mutex_lock()` / `unlock()` |
| 재귀 뮤텍스 | 별도 타입 `xSemaphoreCreateRecursiveMutex()` + `xSemaphoreTakeRecursive()` | 기본이 재귀 | 기본이 재귀 (중첩 소유) | `osMutexRecursive` 비트 | `PTHREAD_MUTEX_RECURSIVE` |
| 메시지 큐 | `xQueueCreate(len, item)`, `xQueueSend()`, `xQueueReceive()` | `K_MSGQ_DEFINE()`, `k_msgq_put()`, `k_msgq_get()` | `tx_queue_create(&q, name, words, buf, bytes)`, `tx_queue_send()`, `tx_queue_receive()` | `osMessageQueueNew(n, size, attr)`, `osMessageQueuePut()`, `osMessageQueueGet()` | `mq_open()`, `mq_send()`, `mq_receive()` |
| 큐 앞에 넣기 | `xQueueSendToFront()` | `k_msgq_put_front()` | `tx_queue_front_send()` | `msg_prio` 인자 (구현마다 의미 다름) | `mq_send()`의 priority 인자 |
| 이벤트 플래그 | `xEventGroupSetBits()`, `xEventGroupWaitBits()` | `k_event_post()` / `k_event_set()`, `k_event_wait()` | `tx_event_flags_set()`, `tx_event_flags_get()` | `osEventFlagsSet()`, `osEventFlagsWait()` | 없음 (condvar + 비트마스크) |
| 태스크 직접 통지 | `xTaskNotifyGive()` / `ulTaskNotifyTake()`, `xTaskNotify()` / `xTaskNotifyWait()` | 직접 대응 없음 (`k_sem`, `k_event`, `k_poll_signal_raise()`) | 없음 (이벤트 플래그, `tx_thread_resume()`) | `osThreadFlagsSet()` / `osThreadFlagsWait()` | 없음 (condvar, 시그널) |
| 바이트 스트림 | `xStreamBufferCreate()`, `xMessageBufferCreate()` | `k_pipe_write()` / `k_pipe_read()`, `ring_buf_put()` | 없음 | 없음 | `pipe()` |

### 1.3 ISR · 임계 구역 · 스케줄러

| 개념 | FreeRTOS | Zephyr | ThreadX | CMSIS-RTOS2 | POSIX |
|---|---|---|---|---|---|
| ISR에서 세마포어 주기 | `xSemaphoreGiveFromISR(s, &woken)` | `k_sem_give(&s)` 그대로 | `tx_semaphore_put(&s)` 그대로 | `osSemaphoreRelease(id)` 그대로 | `sem_post()` (시그널 핸들러에서 허용되는 async-signal-safe 함수) |
| ISR에서 큐에 넣기 | `xQueueSendFromISR(q, &v, &woken)` | `k_msgq_put(&q, &v, K_NO_WAIT)` | `tx_queue_send(&q, &v, TX_NO_WAIT)` | `osMessageQueuePut(id, &v, 0, 0)` | 해당 없음 |
| ISR 끝에서 스위치 요청 | `portYIELD_FROM_ISR(woken)` 직접 | 자동 (ISR 종료 경로) | 자동 (PendSV) | 구현이 처리 | 해당 없음 |
| 지금 ISR인가? | `xPortIsInsideInterrupt()` | `k_is_in_isr()` | 포트 몫 (IPSR 읽기) | 구현 몫 (`osErrorISR` 반환으로 알려 줌) | 해당 없음 |
| 임계 구역 (태스크) | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | `irq_lock()` / `irq_unlock(key)`, `k_spin_lock()` | `tx_interrupt_control(TX_INT_DISABLE)` | 없음 → CMSIS-Core `__disable_irq()` | 없음 → 뮤텍스 |
| 임계 구역 (ISR 안) | `taskENTER_CRITICAL_FROM_ISR()` | 같은 `irq_lock()` | 같은 함수 | 같은 CMSIS-Core | 해당 없음 |
| Cortex-M3에서 가리는 것 | BASEPRI ← `configMAX_SYSCALL_INTERRUPT_PRIORITY` | BASEPRI ← `_EXC_IRQ_DEFAULT_PRIO` | PRIMASK (전부), `TX_PORT_USE_BASEPRI`로 변경 가능 | — | — |
| 스케줄러 잠금 | `vTaskSuspendAll()` / `xTaskResumeAll()` | `k_sched_lock()` / `k_sched_unlock()` | `tx_thread_preemption_change(&t, 0, &old)` | `osKernelLock()` / `osKernelUnlock()` | 없음 |
| 커널 시작 | `vTaskStartScheduler()` (돌아오지 않음) | 없음 — `main()` 전에 이미 돈다 | `tx_kernel_enter()` → `tx_application_define()` | `osKernelInitialize()`, `osKernelStart()` | 없음 (프로세스가 이미 돈다) |

### 1.4 타이머와 메모리

| 개념 | FreeRTOS | Zephyr | ThreadX | CMSIS-RTOS2 | POSIX |
|---|---|---|---|---|---|
| SW 타이머 생성 | `xTimerCreate(name, period, reload, id, cb)` | `K_TIMER_DEFINE(t, expiry, stop)`, `k_timer_init()` | `tx_timer_create(&t, name, fn, in, first, period, TX_AUTO_ACTIVATE)` | `osTimerNew(fn, osTimerPeriodic, arg, attr)` | `timer_create(clk, &sev, &id)` |
| 시작 / 정지 | `xTimerStart()` / `xTimerStop()` | `k_timer_start(&t, first, period)` / `k_timer_stop()` | `tx_timer_activate()` / `tx_timer_deactivate()` | `osTimerStart(id, ticks)` / `osTimerStop()` | `timer_settime()` |
| 콜백이 도는 곳 | daemon 태스크 `prvTimerTask` (`configTIMER_TASK_PRIORITY`) | **ISR** (시스템 타이머 인터럽트) | 시스템 timer thread (Cortex-M3 포트 기본 우선순위 0 = 최고) | 표준은 "timer thread 또는 ISR" | `SIGEV_THREAD` 새 스레드 / `SIGEV_SIGNAL` 시그널 핸들러 |
| ISR에서 일 미루기 | `xTimerPendFunctionCallFromISR()` | `k_work_submit()` | 없음 (스레드 깨우기) | 없음 | 해당 없음 |
| 고정 크기 블록 풀 | 없음 (`heap_1`~`heap_5` 또는 직접 구현) | `K_MEM_SLAB_DEFINE()`, `k_mem_slab_alloc()` / `k_mem_slab_free()` | `tx_block_pool_create()`, `tx_block_allocate()` / `tx_block_release()` | `osMemoryPoolNew()`, `osMemoryPoolAlloc()` / `osMemoryPoolFree()` | 없음 (`malloc`) |
| 가변 크기 힙 | `pvPortMalloc()` / `vPortFree()` | `k_malloc()` / `k_free()`, `k_heap_alloc()` | `tx_byte_pool_create()`, `tx_byte_allocate()` | 없음 | `malloc()` / `free()` |

### 1.5 EL 독자용 열 — Linux 커널에서는?

Embedded Linux 노트를 막 끝낸 Don에게는 "커널 드라이버에서 쓰던 그것"으로 연결하는 것이 가장 빠르다. Linux 커널 API 자체의 설명은 EL F4(락)·F6(인터럽트)·F8(시간)에 있고, 여기서는 RTOS와 **달라지는 점만** 적는다.

| 개념 | Linux 커널 | RTOS와 다른 점 |
|---|---|---|
| 태스크 생성 | `kthread_run(fn, data, name)` | 스택 크기를 고르지 않는다 (아키텍처 고정 `THREAD_SIZE`). RT 우선순위는 따로 `sched_set_fifo()` |
| 지연 | `msleep()`, `usleep_range()` | jiffies(HZ) 또는 hrtimer 기반. 커널 태스크라도 선점·RT throttling의 영향을 받는다 (EL N1) |
| 주기 실행 | `hrtimer` (`HRTIMER_MODE_ABS`), 유저 공간은 `clock_nanosleep(TIMER_ABSTIME)` | RTOS의 delay-until과 같은 발상 |
| 세마포어 | `struct semaphore`, `down()` / `up()` | 새 코드에서는 거의 안 쓴다 — `completion`이나 `mutex`를 권한다 |
| 뮤텍스 | `mutex_lock()` / `mutex_unlock()` | 일반 커널 mutex에는 PI가 없다. PI는 `rt_mutex`와 PREEMPT_RT (EL F4, N1) |
| 메시지 큐 | `kfifo_in()` / `kfifo_out()` | kfifo 자체는 블록하지 않는다 — `wait_event()`와 묶어 쓴다 |
| 이벤트 · 통지 | `wait_event_interruptible(wq, cond)` + `wake_up()`, `complete()` | 조건식을 직접 쓰는 방식. FreeRTOS task notification과 가장 닮은 것은 `completion` |
| ISR → 스레드 | `request_threaded_irq()`, workqueue | Zephyr `k_work`와 같은 발상 (EL F6) |
| 임계 구역 | `spin_lock_irqsave()`, `local_irq_save()` | SMP라 spinlock이 기본. RTOS 단일 코어에서는 IRQ 마스크만으로 충분한 경우가 많다 |
| 스케줄러 잠금 | `preempt_disable()` | `vTaskSuspendAll()` / `k_sched_lock()`과 같은 역할 |
| 시각 | `jiffies`, `ktime_get()` | tick 카운트와 고해상도 시계가 분리된 구조는 Zephyr와 비슷 |
| 블록 풀 | `kmem_cache_create()`, `mempool_create()` | `k_mem_slab`, `tx_block_pool`과 같은 고정 크기 할당 |

### 1.6 표를 읽는 법 — 이름보다 "의미 열"을 보라

표를 처음 보면 "이름만 다르고 다 같네"라는 느낌이 든다. 그런데 표 안에 이미 함정이 숨어 있다.

- 주기 대기 행: ThreadX에는 delay-until이 **없다**. 직접 계산하면 계산과 sleep 사이에 선점될 때 주기가 밀린다(§2.3에서 실제로 작성한다).
- 콜백 행: Zephyr만 **ISR** 이다. FreeRTOS 코드의 타이머 콜백 안에 있던 `xSemaphoreTake(m, portMAX_DELAY)`를 Zephyr로 그대로 옮기면 터진다(§3.3).
- 재귀 뮤텍스 행: FreeRTOS만 기본이 **비재귀**다. Zephyr·ThreadX 코드에서 같은 뮤텍스를 두 번 잡던 습관을 FreeRTOS로 옮기면 교착(deadlock)이 된다(§3.5).
- counting 세마포어 행: `(max, init)`와 `(init, limit)`의 **순서가 반대**다. FreeRTOS 습관대로 `k_sem_init(&s, 5, 0)`이라고 쓰면 Zephyr는 limit 0을 거부한다(`k_sem_init()`은 `-EINVAL`, `K_SEM_DEFINE()`은 `BUILD_ASSERT`로 빌드 실패). 반대로 `(1, 5)`처럼 둘 다 그럴듯한 숫자면 아무 에러 없이 **초기값과 상한이 뒤바뀐 채** 돈다.

이 "의미 열"을 하나씩 실험으로 확인하는 것이 §3이다. 그 전에 같은 프로그램을 다섯 세계에서 돌려 본다.

---

## 2. 같은 프로그램을 다섯 세계에서

### 2.1 사양 — 모든 RTOS 입문 예제의 축소판

다섯 세계에서 똑같이 돌릴 프로그램은 작지만 RTOS의 핵심 객체 네 가지를 다 쓴다.

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="30" width="175" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="108" y="54" font-size="12" text-anchor="middle" font-weight="bold">producer</text>
<text x="108" y="71" font-size="12" text-anchor="middle">20 ms 주기 (prio 낮음)</text>
<rect x="255" y="36" width="150" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="330" y="62" font-size="12" text-anchor="middle">queue 깊이 4</text>
<rect x="465" y="30" width="175" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="552" y="54" font-size="12" text-anchor="middle" font-weight="bold">consumer</text>
<text x="552" y="71" font-size="12" text-anchor="middle">대기 100 ms (prio 높음)</text>
<line x1="197" y1="58" x2="253" y2="58" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="407" y1="58" x2="463" y2="58" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<text x="225" y="50" font-size="12" text-anchor="middle">{seq, t}</text>
<rect x="255" y="130" width="150" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="330" y="150" font-size="12" text-anchor="middle">mutex + shared++</text>
<text x="330" y="167" font-size="12" text-anchor="middle">양쪽이 한 번씩</text>
<line x1="107" y1="88" x2="253" y2="150" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
<line x1="552" y1="88" x2="407" y2="150" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
<rect x="20" y="140" width="175" height="48" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="108" y="160" font-size="12" text-anchor="middle">periodic timer</text>
<text x="108" y="177" font-size="12" text-anchor="middle">30 ms · fires++</text>
<text x="20" y="215" font-size="12" text-anchor="start">기대 출력: seq 1..5, shared=10, timer_fires=3 (100 ms 안에 30·60·90 ms)</text>
</svg>
```

그림 2 — 실험 프로그램. producer가 20 ms마다 `{seq, 보낸 시각}`을 큐에 넣고, 더 높은 우선순위의 consumer가 받아서 지연을 잰다. 둘 다 뮤텍스로 보호되는 공유 카운터를 한 번씩 올린다. 30 ms 주기 타이머는 발화 횟수만 센다.

- producer: 20 ms **절대 주기**로 깨어나 메시지를 보낸다(5번).
- consumer: 큐에서 받고(최대 100 ms 대기), "보낸 시각 → 받은 시각" 지연을 출력한다.
- 공유 카운터 `shared`: 뮤텍스로 보호. 기대값 10(5 + 5).
- 주기 타이머 30 ms: `timer_fires++`만 한다. 100 ms 동안 30·60·90 ms에 세 번 발화해야 한다.

드론 비행 제어기로 바꿔 말하면 producer = IMU 샘플 태스크, consumer = 자세 추정 태스크, 타이머 = 텔레메트리 주기, 공유 카운터 = 상태 블록이다. 이 축소판을 옮길 수 있으면 실제 펌웨어도 같은 순서로 옮길 수 있다.

### 2.2 예제 1 — FreeRTOS V11.3.2

무엇을 확인하는 코드인지: FreeRTOS 이름으로 쓴 기준 구현. `xTaskDelayUntil`, `pdMS_TO_TICKS`, 스택 256 **words**, 큰 숫자 = 높은 우선순위를 눈여겨본다.

```c
/* A5 ex1 — FreeRTOS: producer -> queue -> consumer + periodic timer + mutex counter */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "board.h"

typedef struct { uint32_t seq; uint32_t t_us; } msg_t;
static QueueHandle_t     q;
static SemaphoreHandle_t mtx;
static uint32_t shared, timer_fires;

static void bump(void) {                      /* mutex로 보호되는 공유 카운터 */
    xSemaphoreTake(mtx, portMAX_DELAY); shared++; xSemaphoreGive(mtx);
}
static void timer_cb(TimerHandle_t t) { (void)t; timer_fires++; } /* daemon task 문맥 */

static void producer(void *arg) {
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (uint32_t i = 1; i <= 5; i++) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(20));          /* 20 ms 주기 */
        msg_t m = { i, board_now_us() };
        bump();
        if (xQueueSend(q, &m, 0) != pdPASS) board_printf_raw("queue full!\n");
    }
    vTaskDelete(NULL);
}
static void consumer(void *arg) {
    (void)arg;
    msg_t m;
    for (int got = 0; got < 5; ) {
        if (xQueueReceive(q, &m, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint32_t lat = board_now_us() - m.t_us;
            bump(); got++;
            board_printf_raw("[%u ms] cons seq=%u lat=%u us\n",
                             (unsigned)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                             (unsigned)m.seq, (unsigned)lat);
        } else board_printf_raw("timeout\n");
    }
    board_printf_raw("DONE shared=%u timer_fires=%u\n", (unsigned)shared, (unsigned)timer_fires);
    board_exit(0);
}
int main(void) {
    q   = xQueueCreate(4, sizeof(msg_t));
    mtx = xSemaphoreCreateMutex();
    TimerHandle_t t = xTimerCreate("tick30", pdMS_TO_TICKS(30), pdTRUE, NULL, timer_cb);
    xTimerStart(t, 0);
    xTaskCreate(producer, "prod", 256, NULL, 2, NULL);  /* 256 words = 1 KB */
    xTaskCreate(consumer, "cons", 256, NULL, 3, NULL);  /* 큰 숫자 = 높은 우선순위 */
    board_printf_raw("FreeRTOS %s start\n", tskKERNEL_VERSION_NUMBER);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/A5/ex1/main.c OUT=/var/tmp/rtos/A5/ex1 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  14176	    100	 135416	 149692	  248bc	/var/tmp/rtos/A5/ex1/app.elf
FreeRTOS V11.1.0+ start
[20 ms] cons seq=1 lat=18 us
[40 ms] cons seq=2 lat=18 us
[60 ms] cons seq=3 lat=18 us
[80 ms] cons seq=4 lat=18 us
[100 ms] cons seq=5 lat=18 us
DONE shared=10 timer_fires=3
[board_exit] code=0
```

출력에서 볼 것: 20 ms 간격, 지연 18 µs(icount 기준 약 560명령), `shared=10`, `timer_fires=3`. 기대 그대로다. bss 135 KB의 대부분은 `heap_4`의 128 KB 힙(`configTOTAL_HEAP_SIZE`)이다.

> 덤으로 발견한 함정: V11.3.2 소스인데 `tskKERNEL_VERSION_NUMBER`가 `"V11.1.0+"`를 찍는다. `include/task.h`의 이 매크로가 갱신되지 않은 채 남아 있다(`History.txt` 첫 항목은 "Changes between FreeRTOS V11.3.1 and FreeRTOS V11.3.2"). 버전 판별은 매크로 하나만 믿지 말고 저장소 태그·History로 확인한다.

### 2.3 예제 2 — Zephyr v4.4.2 (같은 보드 `mps2/an385`)

무엇을 확인하는 코드인지: 같은 프로그램을 Zephyr 커널 API로. 객체를 매크로로 **정적 정의**하는 습관(`K_MSGQ_DEFINE`, `K_MUTEX_DEFINE`, `K_TIMER_DEFINE`, `K_THREAD_DEFINE`), `k_timeout_t`(`K_MSEC`, `K_NO_WAIT`, `K_FOREVER`, `K_TIMEOUT_ABS_MS`), 작은 숫자 = 높은 우선순위, 그리고 `main()`이 스케줄러 시작 **후에** 불린다는 점을 본다.

```c
/* A5 ex2 — Zephyr: producer -> k_msgq -> consumer + k_timer + k_mutex counter */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>

typedef struct { uint32_t seq; uint32_t t_cyc; } msg_t;
K_MSGQ_DEFINE(q, sizeof(msg_t), 4, 4);        /* 깊이 4, 정렬 4 */
K_MUTEX_DEFINE(mtx);
static uint32_t shared, timer_fires;

static void bump(void) {
	k_mutex_lock(&mtx, K_FOREVER); shared++; k_mutex_unlock(&mtx);
}
static void timer_cb(struct k_timer *t) { ARG_UNUSED(t); timer_fires++; } /* ISR 문맥! */
K_TIMER_DEFINE(tick30, timer_cb, NULL);

static uint32_t cyc_to_us(uint32_t c) {
	return (uint32_t)((uint64_t)c * 1000000U / sys_clock_hw_cycles_per_sec());
}
static void producer(void *a, void *b, void *c) {
	int64_t next = k_uptime_get();
	for (uint32_t i = 1; i <= 5; i++) {
		next += 20;                               /* 20 ms 주기: 절대 시각 */
		k_sleep(K_TIMEOUT_ABS_MS(next));
		msg_t m = { i, k_cycle_get_32() };
		bump();
		if (k_msgq_put(&q, &m, K_NO_WAIT) != 0) printk("queue full!\n");
	}
}
static void consumer(void *a, void *b, void *c) {
	msg_t m;
	for (int got = 0; got < 5; ) {
		if (k_msgq_get(&q, &m, K_MSEC(100)) == 0) {
			uint32_t lat = cyc_to_us(k_cycle_get_32() - m.t_cyc);
			bump(); got++;
			printk("[%u ms] cons seq=%u lat=%u us\n", (unsigned)k_uptime_get_32(),
			       m.seq, lat);
		} else printk("timeout\n");
	}
	printk("DONE shared=%u timer_fires=%u\n", shared, timer_fires);
	k_timer_stop(&tick30);
}
/* 작은 숫자 = 높은 우선순위: consumer(2)가 producer(3)보다 높다 */
K_THREAD_DEFINE(prod_id, 1024, producer, NULL, NULL, NULL, 3, 0, 0);
K_THREAD_DEFINE(cons_id, 1024, consumer, NULL, NULL, NULL, 2, 0, 0);

int main(void) {
	printk("Zephyr %s start, tick=%d Hz\n", KERNEL_VERSION_STRING,
	       CONFIG_SYS_CLOCK_TICKS_PER_SEC);
	k_timer_start(&tick30, K_MSEC(30), K_MSEC(30));
	return 0;
}
```

`prj.conf`는 두 줄이다(`CONFIG_PRINTK=y`, `CONFIG_ASSERT=y`). `CMakeLists.txt`는 Zephyr 앱의 표준 4줄이다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex2 /var/tmp/rtos/A5/ex2 && \
  timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/A5/ex2/zephyr/zephyr.elf'
```

```text
           FLASH:       26304 B         4 MB      0.63%
             RAM:        8600 B         4 MB      0.21%
*** Booting Zephyr OS build v4.4.2 ***
Zephyr 4.4.2 start, tick=100 Hz
[20 ms] cons seq=1 lat=27 us
[40 ms] cons seq=2 lat=27 us
[60 ms] cons seq=3 lat=37 us
[80 ms] cons seq=4 lat=27 us
[100 ms] cons seq=5 lat=27 us
DONE shared=10 timer_fires=3
qemu-system-arm: terminating on signal 15 from pid 751497 (timeout)
```

출력에서 볼 것:

- 결과(`shared=10`, `timer_fires=3`)는 FreeRTOS와 같다. 지연은 27 µs로 FreeRTOS보다 약간 길고, 60 ms에서만 37 µs다. 60 ms는 30 ms 타이머가 같은 tick에 겹치는 순간이다. Zephyr의 타이머 콜백은 ISR에서 돌기 때문에(§3.3) 같은 인터럽트 처리 안에 콜백 실행이 끼어든 것으로 보인다(가설 — ISR 내부 순서를 트레이스로 확인하지는 않았다).
- **`tick=100 Hz`**. Zephyr `kernel/Kconfig`의 `SYS_CLOCK_TICKS_PER_SEC`는 `default 100 if QEMU_TARGET || SOC_POSIX`, `default 10000 if TICKLESS_KERNEL`이다. 즉 **QEMU 보드는 100 Hz, 실제 tickless 보드는 기본 10 kHz**다. 같은 앱이 보드에 따라 tick 해상도가 100배 달라진다. 이 차이가 §4에서 OSAL을 깨뜨린다.
- 마지막 줄은 앱이 QEMU를 끝내지 않아서 `timeout`이 끝낸 흔적이다(뒤 예제부터는 semihosting 종료 함수를 쓴다).
- Zephyr에는 `vTaskStartScheduler()`에 해당하는 줄이 없다. `K_THREAD_DEFINE`으로 정의한 스레드는 커널 부팅 중에 이미 만들어지고, `main()` 자체가 우선순위 0의 스레드다.

### 2.4 예제 3 — Eclipse ThreadX 6.5.2 (같은 보드에 직접 포팅)

랩에는 ThreadX가 없다. 그래서 `git clone --depth 1 https://github.com/eclipse-threadx/threadx`(커밋 `56c8695`, 2026-10-13)을 `/var/tmp/rtos/A5/`에 받고, Cortex-M3 GNU 포트를 랩의 보드 파일(`startup.c`, `board.c`, `mps2_m3.ld`)에 붙였다. 포트에서 보드마다 바꿔야 하는 것은 `_tx_initialize_low_level()` 하나뿐이다. 원래는 예제 빌드의 어셈블리 파일인데, 하는 일이 단순해서 C로 옮겼다.

무엇을 확인하는 코드인지: ThreadX를 새 보드에 올리는 최소 접착 코드. "커널이 쓸 빈 메모리 시작 주소 알려 주기, SysTick 켜기, SVC·PendSV를 최저 우선순위로" 세 가지다.

```c
/* ThreadX Cortex-M3 low-level glue for the lab board (example_build의 asm을 C로 옮김) */
#include "tx_api.h"
#include "board.h"
extern VOID *_tx_initialize_unused_memory;
extern char __heap_start;
VOID _tx_timer_interrupt(VOID);

void _tx_initialize_low_level(void) {
    __asm volatile("cpsid i");
    _tx_initialize_unused_memory = &__heap_start;
    REG32(0xE000E010) = 0;                                   /* SysTick CSR */
    REG32(0xE000E018) = 0;                                   /* CVR */
    REG32(0xE000E014) = BOARD_CPU_HZ / TX_TIMER_TICKS_PER_SECOND - 1;
    REG32(0xE000E010) = 7;                                   /* CLKSRC|TICKINT|EN */
    REG32(0xE000ED1C) = 0xFF000000u;                         /* SVCall = 최저 */
    REG32(0xE000ED20) = 0x40FF0000u;                         /* SysTick 0x40, PendSV 0xFF */
}
void SysTick_Handler(void) { _tx_timer_interrupt(); }
```

`PendSV_Handler`는 포트의 `tx_thread_schedule.S`가 같은 이름으로 정의하므로 랩 `startup.c`의 weak 핸들러를 자동으로 덮는다. SysTick 핸들러만 C로 연결했다. 빌드 스크립트는 공통 소스(`common/src/*.c` 185개)와 포트의 어셈블리 7개를 컴파일해 앱과 링크한다(`-DTX_TIMER_TICKS_PER_SECOND=1000`으로 tick을 1 kHz로 맞췄다. ThreadX 기본값은 `tx_api.h`의 100).

무엇을 확인하는 코드인지: 같은 프로그램을 ThreadX 이름으로. **모든 객체를 호출자가 메모리째 넘기는** 스타일(스택 배열, 큐 버퍼), 큐 메시지 크기를 **ULONG 개수**로 주는 것, 0 = 최고 우선순위, `tx_application_define()`이라는 시작 콜백, delay-until이 없어서 직접 계산하는 producer를 본다.

```c
/* A5 ex5 — ThreadX: producer -> tx_queue -> consumer + tx_timer + tx_mutex counter */
#include "tx_api.h"
#include "board.h"

typedef struct { ULONG seq; ULONG t_us; } msg_t;           /* 2 ULONG = 8 B */
static TX_THREAD prod_t, cons_t;
static ULONG prod_stk[256], cons_stk[256];                  /* 1 KB씩 */
static TX_QUEUE q;   static ULONG q_mem[4 * 2];            /* 메시지 4개 */
static TX_MUTEX mtx; static TX_TIMER tick30;
static ULONG shared, timer_fires;

static void bump(void) { tx_mutex_get(&mtx, TX_WAIT_FOREVER); shared++; tx_mutex_put(&mtx); }
static void timer_cb(ULONG in) { (void)in; timer_fires++; } /* 시스템 timer thread 문맥 */

static void producer(ULONG in) {
    (void)in;
    ULONG next = tx_time_get();
    for (ULONG i = 1; i <= 5; i++) {
        next += 20;                                         /* 1 kHz tick 기준 20 ms */
        tx_thread_sleep(next - tx_time_get());              /* delay-until이 없어 직접 계산 */
        msg_t m = { i, board_now_us() };
        bump();
        if (tx_queue_send(&q, &m, TX_NO_WAIT) != TX_SUCCESS) board_printf_raw("queue full!\n");
    }
}
static void consumer(ULONG in) {
    (void)in; msg_t m;
    for (int got = 0; got < 5; ) {
        if (tx_queue_receive(&q, &m, 100) == TX_SUCCESS) {
            ULONG lat = board_now_us() - m.t_us;
            bump(); got++;
            board_printf_raw("[%u ms] cons seq=%u lat=%u us\n", (unsigned)tx_time_get(),
                             (unsigned)m.seq, (unsigned)lat);
        } else board_printf_raw("timeout\n");
    }
    board_printf_raw("DONE shared=%u timer_fires=%u\n", (unsigned)shared, (unsigned)timer_fires);
    board_exit(0);
}
void tx_application_define(void *first_unused) {
    (void)first_unused;
    tx_queue_create(&q, "q", 2, q_mem, sizeof q_mem);       /* 메시지 크기 = ULONG 2개 */
    tx_mutex_create(&mtx, "mtx", TX_INHERIT);               /* PI는 옵션 */
    tx_timer_create(&tick30, "t30", timer_cb, 0, 30, 30, TX_AUTO_ACTIVATE);
    /* 0 = 가장 높음: consumer(2)가 producer(3)보다 높다 */
    tx_thread_create(&prod_t, "prod", producer, 0, prod_stk, sizeof prod_stk, 3, 3,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_create(&cons_t, "cons", consumer, 0, cons_stk, sizeof cons_stk, 2, 2,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) {
    board_printf_raw("ThreadX %u.%u.%u start\n", THREADX_MAJOR_VERSION,
                     THREADX_MINOR_VERSION, THREADX_PATCH_VERSION);
    tx_kernel_enter();                                      /* 돌아오지 않는다 */
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/A5 && /Users/donh/.rtos_scratch/A5/ex5/build.sh \
  /Users/donh/.rtos_scratch/A5/ex5/main.c /var/tmp/rtos/A5/ex5 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex5/app.elf 10'
```

```text
   text	   data	    bss	    dec	    hex	filename
  11676	    100	   4540	  16316	   3fbc	/var/tmp/rtos/A5/ex5/app.elf
ThreadX 6.5.2 start
[20 ms] cons seq=1 lat=11 us
[40 ms] cons seq=2 lat=10 us
[60 ms] cons seq=3 lat=11 us
[80 ms] cons seq=4 lat=10 us
[100 ms] cons seq=5 lat=10 us
DONE shared=10 timer_fires=3
[board_exit] code=0
```

출력에서 볼 것: 첫 시도에 그대로 돌았다. 지연 10~11 µs로 셋 중 가장 짧다(§6에서 왕복 시간으로 다시 잰다). bss가 4.5 KB뿐인 이유는 ThreadX가 힙을 갖지 않고 객체 메모리를 전부 앱 배열(`prod_stk`, `q_mem` …)로 받기 때문이다.

> 정직한 메모: 앱 코드와 접착 코드는 `-Wall -Wextra` 경고 0개다. ThreadX 공통 소스 185개는 `-w`로 경고를 끄고 컴파일했다(커널 소스 자체의 경고는 이 노트의 관심 밖이라 세지 않았다).

### 2.5 예제 4 — POSIX (Linux VM, pthread + mqueue + timer_create)

무엇을 확인하는 코드인지: 같은 프로그램을 POSIX로. `mq_open`으로 메시지 큐, `clock_nanosleep(TIMER_ABSTIME)`으로 절대 주기, `timer_create(SIGEV_THREAD)`로 주기 타이머. 시간은 tick이 아니라 `struct timespec`(초 + 나노초)다. pthread·뮤텍스 자체는 EL A8에서 다뤘으니 RTOS와 다른 점만 본다.

```c
/* A5 ex3 — POSIX: producer -> mq -> consumer + timer_create + pthread mutex.
 * 같은 파일을 Linux(gcc)와 Zephyr(CONFIG_POSIX_API)에서 빌드한다. */
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef struct { uint32_t seq; uint32_t t_us; } msg_t;
static mqd_t q;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile uint32_t shared, timer_fires;

static uint32_t now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}
static void bump(void) { pthread_mutex_lock(&mtx); shared++; pthread_mutex_unlock(&mtx); }
static void timer_cb(union sigval sv) { (void)sv; timer_fires++; }  /* 새 스레드 문맥 */

static void *producer(void *arg) {
    (void)arg;
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    for (uint32_t i = 1; i <= 5; i++) {
        next.tv_nsec += 20000000;                         /* 20 ms 주기 */
        if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        msg_t m = { i, now_us() };
        bump();
        if (mq_send(q, (const char *)&m, sizeof m, 0) != 0) printf("queue full!\n");
    }
    return NULL;
}
static void *consumer(void *arg) {
    uint32_t t0 = *(uint32_t *)arg; msg_t m;
    for (int got = 0; got < 5; ) {
        if (mq_receive(q, (char *)&m, sizeof m, NULL) == (ssize_t)sizeof m) {
            uint32_t lat = now_us() - m.t_us;
            bump(); got++;
            printf("[%u ms] cons seq=%u lat=%u us\n", (unsigned)((now_us() - t0) / 1000),
                   (unsigned)m.seq, (unsigned)lat);
        }
    }
    return NULL;
}
int main(void) {
    struct mq_attr at = { .mq_maxmsg = 4, .mq_msgsize = sizeof(msg_t) };
    q = mq_open("/a5q", O_CREAT | O_RDWR, 0600, &at);
    if (q == (mqd_t)-1) { perror("mq_open"); return 1; }
    timer_t tm; struct sigevent sev = { .sigev_notify = SIGEV_THREAD,
                                        .sigev_notify_function = timer_cb };
    struct itimerspec its = { .it_value = { 0, 30000000 }, .it_interval = { 0, 30000000 } };
    timer_create(CLOCK_MONOTONIC, &sev, &tm); timer_settime(tm, 0, &its, NULL);
    uint32_t t0 = now_us(); pthread_t p, c;
    printf("POSIX start\n");
    pthread_create(&c, NULL, consumer, &t0);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(p, NULL); pthread_join(c, NULL);
    timer_delete(tm);
    printf("DONE shared=%u timer_fires=%u\n", (unsigned)shared, (unsigned)timer_fires);
    mq_close(q); mq_unlink("/a5q");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'gcc -O2 -Wall -Wextra /Users/donh/.rtos_scratch/A5/ex3/src/main.c \
  -o /var/tmp/rtos/A5/ex3/posix -lrt -pthread && /var/tmp/rtos/A5/ex3/posix; uname -r'
```

```text
POSIX start
[20 ms] cons seq=1 lat=359 us
[41 ms] cons seq=2 lat=21 us
[64 ms] cons seq=3 lat=16 us
[83 ms] cons seq=4 lat=17 us
[104 ms] cons seq=5 lat=14 us
DONE shared=10 timer_fires=3
7.0.0-38-generic
```

출력에서 볼 것: 결과는 같지만 시각이 20·41·64·83·104 ms로 흔들리고, 첫 지연이 359 µs다. 우선순위를 지정하지 않은 `SCHED_OTHER` 스레드가 VM 위에서 돌기 때문이다(EL N1의 "일반 Linux는 soft RT"). 첫 지연이 큰 것은 consumer 스레드가 처음 깨어날 때의 비용(페이지 폴트, 캐시)이 한 번 붙은 것이다. RTOS 셋은 매번 같은 숫자였다는 점과 대비해서 본다.

### 2.6 예제 5 — 같은 POSIX 파일을 Zephyr에서

Zephyr는 POSIX API 계층(`lib/posix`)을 갖고 있다. **예제 4의 소스 파일을 한 글자도 바꾸지 않고** Zephyr로 빌드한다. `CMakeLists.txt`가 `../ex3/src/main.c`를 가리키게만 했다.

무엇을 확인하는 코드인지: POSIX가 정말 "이식 가능한 API"인지, 그리고 이식은 되는데 **의미가 같은지**.

```text
CONFIG_POSIX_API=y
CONFIG_POSIX_MESSAGE_PASSING=y
CONFIG_POSIX_TIMERS=y
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_DYNAMIC_THREAD=y
CONFIG_DYNAMIC_THREAD_POOL_SIZE=3
CONFIG_DYNAMIC_THREAD_STACK_SIZE=2048
CONFIG_HEAP_MEM_POOL_SIZE=16384
```

`pthread_create(&t, NULL, …)`처럼 스택을 안 주는 호출이 동작하려면 `CONFIG_DYNAMIC_THREAD`(스택을 풀이나 힙에서 받기)가 필요하고, `mq_open`은 힙에서 버퍼를 받는다. **RTOS에서 POSIX를 쓰면 "숨은 동적 할당"이 생긴다**는 것이 첫 번째 교훈이다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex4 /var/tmp/rtos/A5/ex4; \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex4/zephyr/zephyr.elf 5'
```

```text
           FLASH:       30116 B         4 MB      0.72%
             RAM:       36952 B         4 MB      0.88%
*** Booting Zephyr OS build v4.4.2 ***
POSIX start
[30 ms] cons seq=1 lat=0 us
[50 ms] cons seq=2 lat=0 us
[70 ms] cons seq=3 lat=0 us
[90 ms] cons seq=4 lat=0 us
[110 ms] cons seq=5 lat=0 us
DONE shared=10 timer_fires=3
qemu-system-arm: terminating on signal 15 from pid 768397 (timeout)
```

출력에서 볼 것 — 두 가지가 이상하다.

1. **`lat=0 us`**. Zephyr의 `clock_gettime(CLOCK_MONOTONIC)`은 `lib/os/clock.c`에서 `timespec_from_ticks(k_uptime_ticks(), ts)`로 구현돼 있다. 즉 해상도가 **tick(여기서는 10 ms)** 이다. `clock_getres()`도 `NSEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC`를 돌려준다. Linux에서 ns 해상도였던 같은 코드가 여기서는 "같은 tick 안이면 0"이 된다.
2. **첫 메시지가 30 ms**, 이후 20 ms 간격. `lib/os/clock.c`의 `clock_nanosleep` 구현은 `TIMER_ABSTIME`을 받으면 주석 그대로 "convert absolute time to relative time duration" — 현재 시각을 빼서 **상대 시간으로 바꾼 뒤** `k_sleep()`한다. Zephyr의 상대 타임아웃은 "최소 그 시간 이상"을 보장하려고 1 tick을 더하므로(§3.2) 매번 1 tick(10 ms) 늦게 깬다. 다음 목표 시각은 원래 기준에서 20 ms씩 더하므로 누적 drift는 없고, **위상이 1 tick 밀린 채** 고정된다.

> 결론: 같은 POSIX 소스가 **컴파일되고 결과값도 맞는다**. 그러나 시간 해상도와 절대 시각 의미는 달라졌다. "POSIX로 쓰면 이식 끝"이 아니라 "POSIX로 쓰면 **이식 후 시간 의미를 다시 검증**해야 한다"가 정확한 말이다.

### 2.7 나란히 보기

| 항목 | FreeRTOS | Zephyr | ThreadX | POSIX / Linux VM | POSIX / Zephyr |
|---|---|---|---|---|---|
| 받은 시각 (ms) | 20, 40, 60, 80, 100 | 20, 40, 60, 80, 100 | 20, 40, 60, 80, 100 | 20, 41, 64, 83, 104 | 30, 50, 70, 90, 110 |
| 큐 → 수신 지연 | 18 µs 고정 | 27 µs (한 번 37) | 10~11 µs | 14~359 µs | 0 (tick 해상도라 못 잼) |
| `shared` / `timer_fires` | 10 / 3 | 10 / 3 | 10 / 3 | 10 / 3 | 10 / 3 |
| tick | 1 kHz (랩 설정) | 100 Hz (QEMU 기본) | 1 kHz (직접 설정, 기본 100) | 없음 | 100 Hz |
| 주기 대기 | `xTaskDelayUntil` | `K_TIMEOUT_ABS_MS` | 직접 계산 | `clock_nanosleep` ABS | 같은 코드, 내부는 상대 변환 |
| 코드 크기 (`.text`) | 14,176 B (-O2) | 26,304 B FLASH (-Os, assert 켬) | 11,676 B (-O2) | — | 30,116 B FLASH |
| 실행 판정 | `board_exit(0)` | timeout | `board_exit(0)` | 프로세스 종료 | timeout |

크기 숫자는 최적화·assert 설정이 섞여 있어서 아직 비교하면 안 된다. 조건을 맞춘 크기 비교는 §6에서 한다.

---

## 3. 의미 함정 — 이름은 같은데 뜻이 다른 것들

이 절이 이 노트의 핵심이다. 번역에서 사고가 나는 지점은 "함수 이름을 몰라서"가 아니라 "이름은 알았는데 의미가 달라서"다. 세 개의 탐침(probe) 프로그램 — FreeRTOS(예제 6), Zephyr(예제 7), ThreadX(예제 8) — 으로 숫자를 직접 찍어 보고, 일부러 틀린 호출(예제 9~11)로 어떻게 터지는지 본다.

### 3.0 예제 6·7·8 — 세 커널의 탐침

무엇을 확인하는 코드인지: 반환값, binary/counting 세마포어 초기값, 뮤텍스 재귀, 우선순위 숫자, 스택 단위, ms → tick 변환을 한 프로그램에서 찍는다. 아래 세 절(§3.1~§3.7)이 이 출력을 나눠서 해석한다.

**예제 6 — FreeRTOS 탐침**

```c
/* A5 ex10 — FreeRTOS 의미 함정 탐침: 반환값 · 뮤텍스 재귀 · 스택 단위 · 우선순위 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"

static void probe(void *arg) {
    (void)arg;
    QueueHandle_t q = xQueueCreate(1, sizeof(uint32_t));
    uint32_t v = 7;
    BaseType_t r1 = xQueueSend(q, &v, 0);
    BaseType_t r2 = xQueueSend(q, &v, 0);                 /* 가득 참 */
    board_printf_raw("send ok=%d full=%d (pdPASS=%d errQUEUE_FULL=%d)\n",
                     (int)r1, (int)r2, (int)pdPASS, (int)errQUEUE_FULL);

    SemaphoreHandle_t b = xSemaphoreCreateBinary();       /* 처음엔 비어 있음 */
    board_printf_raw("binary take(0) on new sem=%d (pdFALSE=%d)\n",
                     (int)xSemaphoreTake(b, 0), (int)pdFALSE);
    SemaphoreHandle_t c = xSemaphoreCreateCounting(5, 2); /* max 5, 초기 2 */
    board_printf_raw("counting(5,2) count=%u\n", (unsigned)uxSemaphoreGetCount(c));

    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    xSemaphoreTake(m, portMAX_DELAY);
    TickType_t t0 = xTaskGetTickCount();
    BaseType_t again = xSemaphoreTake(m, pdMS_TO_TICKS(50)); /* 같은 태스크가 또 */
    board_printf_raw("normal mutex re-take by owner=%d after %u ticks\n",
                     (int)again, (unsigned)(xTaskGetTickCount() - t0));
    SemaphoreHandle_t rm = xSemaphoreCreateRecursiveMutex();
    board_printf_raw("recursive take x2 = %d %d\n",
                     (int)xSemaphoreTakeRecursive(rm, 0), (int)xSemaphoreTakeRecursive(rm, 0));

    board_printf_raw("my prio=%u (configMAX_PRIORITIES=%u, idle=%u)\n",
                     (unsigned)uxTaskPriorityGet(NULL), (unsigned)configMAX_PRIORITIES,
                     (unsigned)tskIDLE_PRIORITY);
    board_printf_raw("stack: created with 256, high-water=%u (units: words of %u B)\n",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)sizeof(StackType_t));
    board_printf_raw("pdMS_TO_TICKS(5)=%u at %u Hz; portMAX_DELAY=0x%x\n",
                     (unsigned)pdMS_TO_TICKS(5), (unsigned)configTICK_RATE_HZ,
                     (unsigned)portMAX_DELAY);
    board_exit(0);
}
int main(void) {
    xTaskCreate(probe, "probe", 256, NULL, 5, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/ex10/main.c \
  OUT=/var/tmp/rtos/A5/ex10 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex10/app.elf; \
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/ex10/main.c \
  OUT=/var/tmp/rtos/A5/ex10b EXTRA_CFLAGS=-DconfigTICK_RATE_HZ=300 >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex10b/app.elf | grep pdMS'
```

```text
send ok=1 full=0 (pdPASS=1 errQUEUE_FULL=0)
binary take(0) on new sem=0 (pdFALSE=0)
counting(5,2) count=2
normal mutex re-take by owner=0 after 50 ticks
recursive take x2 = 1 1
my prio=5 (configMAX_PRIORITIES=8, idle=0)
stack: created with 256, high-water=218 (units: words of 4 B)
pdMS_TO_TICKS(5)=5 at 1000 Hz; portMAX_DELAY=0xffffffff
[board_exit] code=0
pdMS_TO_TICKS(5)=1 at 300 Hz; portMAX_DELAY=0xffffffff
```

**예제 7 — Zephyr 탐침**

```c
/* A5 ex11 — Zephyr 의미 함정 탐침: 반환값 · 뮤텍스 재귀 · 스택 단위 · 우선순위 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

K_MSGQ_DEFINE(q, sizeof(uint32_t), 1, 4);
K_SEM_DEFINE(b, 0, 1);              /* binary: 초기 0, 최대 1 */
K_SEM_DEFINE(c, 2, 5);              /* counting: 초기 2, 최대 5 — 인자 순서 주의 */
K_MUTEX_DEFINE(m);
K_THREAD_STACK_DEFINE(stk, 1024);
static struct k_thread th;

static void other(void *a, void *b2, void *c2) {
	printk("unlock by non-owner = %d (-EPERM=%d)\n", k_mutex_unlock(&m), -EPERM);
}
int main(void) {
	uint32_t v = 7;
	int r1 = k_msgq_put(&q, &v, K_NO_WAIT);
	int r2 = k_msgq_put(&q, &v, K_NO_WAIT);                  /* 가득 참 */
	printk("put ok=%d full=%d (-ENOMSG=%d)\n", r1, r2, -ENOMSG);
	printk("binary take(K_NO_WAIT) = %d; counting count=%u\n",
	       k_sem_take(&b, K_NO_WAIT), k_sem_count_get(&c));
	printk("mutex lock x2 by owner = %d %d, lock_count=%u\n",
	       k_mutex_lock(&m, K_NO_WAIT), k_mutex_lock(&m, K_NO_WAIT), m.lock_count);
	k_thread_create(&th, stk, K_THREAD_STACK_SIZEOF(stk), other, NULL, NULL, NULL,
			-1, 0, K_NO_WAIT);                              /* 음수 = cooperative */
	k_thread_join(&th, K_FOREVER);
	printk("main prio=%d other prio=%d; preempt prios 0..%d, coop -%d..-1\n",
	       k_thread_priority_get(k_current_get()), k_thread_priority_get(&th),
	       CONFIG_NUM_PREEMPT_PRIORITIES - 1, CONFIG_NUM_COOP_PRIORITIES);
	size_t unused;
	k_thread_stack_space_get(&th, &unused);
	printk("stack: K_THREAD_STACK_SIZEOF=%u bytes, unused=%u bytes\n",
	       (unsigned)K_THREAD_STACK_SIZEOF(stk), (unsigned)unused);
	printk("K_MSEC(5) -> %u ticks at %d Hz (ceil); k_ms_to_ticks_floor32(5)=%u\n",
	       (unsigned)K_MSEC(5).ticks, CONFIG_SYS_CLOCK_TICKS_PER_SEC,
	       k_ms_to_ticks_floor32(5));
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex11 /var/tmp/rtos/A5/ex11 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex11/zephyr/zephyr.elf 2'
```

```text
           FLASH:       14788 B         4 MB      0.35%
             RAM:        7356 B         4 MB      0.18%
*** Booting Zephyr OS build v4.4.2 ***
put ok=0 full=-35 (-ENOMSG=-35)
binary take(K_NO_WAIT) = -16; counting count=2
mutex lock x2 by owner = 0 0, lock_count=2
unlock by non-owner = -1 (-EPERM=-1)
main prio=0 other prio=-1; preempt prios 0..14, coop -16..-1
stack: K_THREAD_STACK_SIZEOF=1024 bytes, unused=796 bytes
K_MSEC(5) -> 1 ticks at 100 Hz (ceil); k_ms_to_ticks_floor32(5)=0
qemu-system-arm: terminating on signal 15 from pid 1011849 (timeout)
```

**예제 8 — ThreadX 탐침**

```c
/* A5 ex12 — ThreadX 의미 함정 탐침: 반환값 · 뮤텍스 중첩 · 스레드 삭제 2단계 */
#include "tx_api.h"
#include "board.h"

static TX_THREAD main_t, other_t;
static ULONG main_stk[256], other_stk[256];
static TX_QUEUE q; static ULONG q_mem[1];
static TX_SEMAPHORE s; static TX_MUTEX m;

static void other(ULONG in) {
    (void)in;
    board_printf_raw("put by non-owner = 0x%x (TX_NOT_OWNED=0x%x)\n",
                     tx_mutex_put(&m), TX_NOT_OWNED);
}
static void probe(ULONG in) {
    (void)in; ULONG v = 7;
    UINT r1 = tx_queue_send(&q, &v, TX_NO_WAIT), r2 = tx_queue_send(&q, &v, TX_NO_WAIT);
    board_printf_raw("send ok=0x%x full=0x%x (TX_SUCCESS=0x%x TX_QUEUE_FULL=0x%x)\n",
                     r1, r2, TX_SUCCESS, TX_QUEUE_FULL);
    board_printf_raw("sem get(empty) = 0x%x (TX_NO_INSTANCE=0x%x)\n",
                     tx_semaphore_get(&s, TX_NO_WAIT), TX_NO_INSTANCE);
    UINT a = tx_mutex_get(&m, TX_NO_WAIT), b = tx_mutex_get(&m, TX_NO_WAIT);
    board_printf_raw("mutex get x2 by owner = 0x%x 0x%x, ownership_count=%u\n",
                     a, b, (unsigned)m.tx_mutex_ownership_count);
    tx_thread_create(&other_t, "other", other, 0, other_stk, sizeof other_stk,
                     1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);   /* 1 < 5 → 더 높음, 즉시 실행 */
    board_printf_raw("delete before terminate = 0x%x (TX_DELETE_ERROR=0x%x)\n",
                     tx_thread_delete(&main_t), TX_DELETE_ERROR);
    UINT st; tx_thread_info_get(&other_t, TX_NULL, &st, TX_NULL, TX_NULL, TX_NULL,
                                TX_NULL, TX_NULL, TX_NULL);
    board_printf_raw("other state=%u (TX_COMPLETED=%u) delete=0x%x\n", st, TX_COMPLETED,
                     tx_thread_delete(&other_t));
    board_printf_raw("prio 0..%u (0 = highest), ticks/s=%u, stack arg in bytes=%u\n",
                     TX_MAX_PRIORITIES - 1, (unsigned)TX_TIMER_TICKS_PER_SECOND,
                     (unsigned)sizeof main_stk);
    board_exit(0);
}
void tx_application_define(void *first_unused) {
    (void)first_unused;
    tx_queue_create(&q, "q", 1, q_mem, sizeof q_mem);         /* 메시지 1 ULONG, 1개 */
    tx_semaphore_create(&s, "s", 0);                          /* 초기 0, 상한 없음 */
    tx_mutex_create(&m, "m", TX_NO_INHERIT);
    tx_thread_create(&main_t, "probe", probe, 0, main_stk, sizeof main_stk,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) { tx_kernel_enter(); return 0; }
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/A5 && /Users/donh/.rtos_scratch/A5/ex5/build.sh /Users/donh/.rtos_scratch/A5/ex12/main.c \
  /var/tmp/rtos/A5/ex12 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex12/app.elf 5'
```

```text
send ok=0x0 full=0xb (TX_SUCCESS=0x0 TX_QUEUE_FULL=0xb)
sem get(empty) = 0xd (TX_NO_INSTANCE=0xd)
mutex get x2 by owner = 0x0 0x0, ownership_count=2
put by non-owner = 0x1e (TX_NOT_OWNED=0x1e)
delete before terminate = 0x11 (TX_DELETE_ERROR=0x11)
other state=1 (TX_COMPLETED=1) delete=0x0
prio 0..31 (0 = highest), ticks/s=1000, stack arg in bytes=1024
[board_exit] code=0
```

(`ticks/s=1000`은 빌드에서 `-DTX_TIMER_TICKS_PER_SECOND=1000`을 줬기 때문이다. 아무것도 안 주면 100이다.)

### 3.1 우선순위 숫자의 방향

가장 유명한 함정이다. 숫자 하나를 옮겨 적었을 뿐인데 가장 급한 태스크가 가장 한가한 태스크가 된다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="400" y="20" font-size="13" text-anchor="middle" font-weight="bold">실제 긴급도 →  (오른쪽일수록 먼저 실행)</text>
<line x1="150" y1="30" x2="650" y2="30" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="20" y="74" font-size="13" text-anchor="start" font-weight="bold">FreeRTOS</text>
<line x1="150" y1="70" x2="650" y2="70" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="150" cy="70" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="150" y="90" font-size="12" text-anchor="start">0 idle</text>
<circle cx="400" cy="70" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="400" y="90" font-size="12" text-anchor="middle">3</text>
<circle cx="650" cy="70" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="650" y="90" font-size="12" text-anchor="end">7 = MAX-1</text>
<line x1="160" y1="58" x2="270" y2="58" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="62" font-size="11" text-anchor="end">숫자 클수록 높다</text>
<text x="20" y="130" font-size="13" text-anchor="start" font-weight="bold">Zephyr</text>
<line x1="150" y1="126" x2="650" y2="126" stroke="#e08a3c" stroke-width="2"/>
<circle cx="150" cy="126" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="150" y="146" font-size="12" text-anchor="start">15 idle</text>
<circle cx="325" cy="126" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="325" y="146" font-size="12" text-anchor="middle">14 … 0 선점형</text>
<circle cx="540" cy="126" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="540" y="146" font-size="12" text-anchor="middle">-1 … -16 협력형</text>
<line x1="270" y1="114" x2="160" y2="114" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="118" font-size="11" text-anchor="end">숫자 작을수록 높다, 음수 = 선점 안 됨</text>
<text x="20" y="186" font-size="13" text-anchor="start" font-weight="bold">ThreadX</text>
<line x1="150" y1="182" x2="650" y2="182" stroke="#e08a3c" stroke-width="2"/>
<circle cx="150" cy="182" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="150" y="202" font-size="12" text-anchor="start">31</text>
<circle cx="400" cy="182" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="400" y="202" font-size="12" text-anchor="middle">15</text>
<circle cx="650" cy="182" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="650" y="202" font-size="12" text-anchor="end">0 = 최고</text>
<line x1="270" y1="170" x2="160" y2="170" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="174" font-size="11" text-anchor="end">0이 가장 높다 (기본 32단계)</text>
<text x="20" y="242" font-size="13" text-anchor="start" font-weight="bold">CMSIS-RTOS2</text>
<line x1="150" y1="238" x2="650" y2="238" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="150" cy="238" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="150" y="258" font-size="12" text-anchor="start">Idle 1</text>
<circle cx="360" cy="238" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="360" y="258" font-size="12" text-anchor="middle">Normal 24</text>
<circle cx="575" cy="238" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="575" y="258" font-size="12" text-anchor="middle">Realtime 48</text>
<circle cx="650" cy="238" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="650" y="258" font-size="12" text-anchor="end">ISR 56</text>
<line x1="160" y1="226" x2="270" y2="226" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="230" font-size="11" text-anchor="end">enum 값이 클수록 높다</text>
<text x="20" y="298" font-size="13" text-anchor="start" font-weight="bold">POSIX FIFO</text>
<line x1="150" y1="294" x2="650" y2="294" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="150" cy="294" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="150" y="314" font-size="12" text-anchor="start">1</text>
<circle cx="400" cy="294" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="400" y="314" font-size="12" text-anchor="middle">50</text>
<circle cx="650" cy="294" r="4" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="650" y="314" font-size="12" text-anchor="end">99 (Linux)</text>
<line x1="160" y1="282" x2="270" y2="282" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="286" font-size="11" text-anchor="end">클수록 높다, 범위는 sched_get_priority_max</text>
<text x="20" y="354" font-size="13" text-anchor="start" font-weight="bold">Cortex-M NVIC</text>
<line x1="150" y1="350" x2="650" y2="350" stroke="#e08a3c" stroke-width="2"/>
<circle cx="150" cy="350" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="150" y="370" font-size="12" text-anchor="start">255</text>
<circle cx="400" cy="350" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="400" y="370" font-size="12" text-anchor="middle">0x80</text>
<circle cx="650" cy="350" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<text x="650" y="370" font-size="12" text-anchor="end">0 = 최고</text>
<line x1="270" y1="338" x2="160" y2="338" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="650" y="342" font-size="11" text-anchor="end">IRQ 하드웨어 우선순위: 작을수록 높다</text>
<text x="20" y="405" font-size="12" text-anchor="start">초록 = 숫자가 커질수록 긴급 · 주황 = 숫자가 작을수록 긴급 (작은 화살표 = 숫자가 커지는 쪽)</text>
</svg>
```

그림 3 — 우선순위 축. 가로축은 "실제로 먼저 실행되는 쪽"이다. 초록 줄은 숫자가 커질수록 급하고, 주황 줄은 숫자가 작을수록 급하다. Cortex-M NVIC의 **인터럽트** 우선순위는 RTOS **태스크** 우선순위와 반대 방향이라는 점을 FreeRTOS 사용자는 특히 조심해야 한다.

| RTOS | 범위 (이 랩 기준) | 높은 쪽 | idle | 특이점 |
|---|---|---|---|---|
| FreeRTOS | 0 ~ `configMAX_PRIORITIES - 1` (0~7) | 큰 숫자 | 0 (`tskIDLE_PRIORITY`) | 타이머 daemon은 `configTIMER_TASK_PRIORITY` (랩 설정: 7, 최고) |
| Zephyr | 협력형 -16 ~ -1, 선점형 0 ~ 14 | 작은 숫자 | 15 (`K_IDLE_PRIO`) | **음수 = cooperative**: 스스로 양보하거나 블록할 때까지 다른 스레드에 선점되지 않는다. `main`은 0 |
| ThreadX | 0 ~ `TX_MAX_PRIORITIES - 1` (0~31, 32의 배수) | 0 | 없음 (스케줄러가 직접 대기) | preemption-threshold: "이 값보다 높은 스레드만 나를 선점" |
| CMSIS-RTOS2 | `osPriorityIdle`(1) ~ `osPriorityISR`(56) | 큰 값 | 1 | `osPriorityNormal` = 24. Zephyr 계층은 `zephyr_prio = osPriorityISR - cmsis_prio`로 뒤집는다 |
| POSIX `SCHED_FIFO` | `sched_get_priority_min()` ~ `max()` (Linux 1~99) | 큰 숫자 | — | Linux 커널 내부 `prio` 필드는 반대(0이 최고) — `ps`·트레이스를 읽을 때 주의 (EL F1) |
| Cortex-M NVIC | 0 ~ 255 (구현 비트만 유효) | 작은 숫자 | — | FreeRTOS의 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 **숫자가 작은**(더 급한) IRQ에서는 RTOS API 금지 (B3, E1) |

예제 7의 `main prio=0 other prio=-1`을 보자. 숫자만 보면 other가 "더 낮아" 보이지만, -1은 협력형 영역이고 0보다 급하다. `k_thread_create()`의 마지막 인자가 `K_NO_WAIT`이므로 other는 만들어지는 즉시 main을 밀어내고 실행된다(이 예제는 바로 뒤에 `k_thread_join()`을 부르므로 출력 순서만으로는 구분되지 않는다 — 우선순위 숫자 자체가 증거다).

CMSIS 계층의 변환은 `subsys/portability/cmsis_rtos_v2/thread.c`에 그대로 있다.

```c
static inline uint32_t cmsis_to_zephyr_priority(uint32_t c_prio)
{
	return (osPriorityISR - c_prio);
}
```

그래서 Zephyr에서 CMSIS 계층을 켜려면 `CONFIG_NUM_PREEMPT_PRIORITIES=56`이 필요하다(Kconfig의 `depends on NUM_PREEMPT_PRIORITIES >= 56`). 기본값 15로는 빌드가 안 된다. OSAL이 "숫자를 번역해 줄 테니 걱정 마"라고 해도, **아래 커널의 우선순위 단계 수를 바꾸게 만든다**는 점이 보인다.

흔한 함정 세 가지:

- FreeRTOS 코드의 "prio 5"를 Zephyr로 그대로 옮기면 Zephyr 5는 main(0)보다 **낮다**. 번역 규칙(예: `zephyr = 7 - freertos`)을 한 곳에 정하고 숫자를 직접 쓰지 않는다.
- Zephyr 음수 우선순위를 "그냥 더 높은 것"으로 쓰면 **선점이 안 된다**. 오래 도는 계산을 협력형 스레드에 두면 다른 스레드가 전부 굶는다.
- FreeRTOS에서 IRQ 우선순위를 0(최고)으로 두고 그 ISR에서 `...FromISR`을 부르면 `configASSERT`(포트의 `vPortValidateInterruptPriority`)에 걸린다. 태스크는 "클수록 높다"인데 NVIC는 "작을수록 높다"라서 생기는 실수다.

### 3.2 시간 — tick · ms · `k_timeout_t` · timespec

"20 ms 기다려"를 다섯 커널에 말하는 방법이 다 다르다.

| RTOS | 타임아웃 인자의 타입·단위 | ms → 단위 변환 | 영원히 / 안 기다림 | 상대 대기의 반올림 |
|---|---|---|---|---|
| FreeRTOS | `TickType_t` — **tick** | `pdMS_TO_TICKS(ms)` = `ms × configTICK_RATE_HZ / 1000` **내림** | `portMAX_DELAY` (0xffffffff) / 0 | 다음 tick 경계까지 남은 부분 tick만큼 짧아질 수 있다 |
| Zephyr | `k_timeout_t` — **구조체** (`K_MSEC`, `K_USEC`, `K_TICKS`, `K_FOREVER`, `K_NO_WAIT`, `K_TIMEOUT_ABS_MS`) | `K_MSEC`는 tick으로 **올림** | `K_FOREVER` / `K_NO_WAIT` | 상대 타임아웃에 **+1 tick** (최소 보장) |
| ThreadX | `ULONG` — **tick** | 직접 `ms × TX_TIMER_TICKS_PER_SECOND / 1000` | `TX_WAIT_FOREVER` / `TX_NO_WAIT` | — |
| CMSIS-RTOS2 | `uint32_t` — **kernel tick** | 직접 `ms × osKernelGetTickFreq() / 1000` | `osWaitForever` / 0 | 구현 몫 |
| POSIX | `struct timespec` — **초 + 나노초**, 절대 시각은 `CLOCK_REALTIME` 기준인 함수가 많다 | 필요 없음 | 함수가 따로 (`sem_wait` / `sem_trywait`) | 구현 몫 (Zephyr에서는 tick으로 반올림) |

예제 6과 7의 마지막 줄이 이 표의 "변환" 열을 증명한다.

- FreeRTOS: `pdMS_TO_TICKS(5)`가 1 kHz에서 5, **300 Hz에서 1**이다. 5 × 300 / 1000 = 1.5를 내림했다. 1 tick = 3.33 ms이므로 "5 ms 기다려"가 3.33 ms(그것도 tick 경계에 따라 더 짧게)가 된다. `configTICK_RATE_HZ`를 바꾸는 순간 모든 `pdMS_TO_TICKS`의 의미가 바뀐다.
- Zephyr: `K_MSEC(5)`가 100 Hz에서 **1 tick(10 ms)** 이다. 올림이다. 반대로 내림 함수 `k_ms_to_ticks_floor32(5)`는 0이다.

Zephyr의 "+1 tick"은 `kernel/timeout.c`의 `z_add_timeout()`에 있다.

```c
		if (Z_IS_TIMEOUT_RELATIVE(timeout)) {
			ticks_elapsed = elapsed();
			has_elapsed = true;
			to->dticks = timeout.ticks + 1 + ticks_elapsed;
			ticks = curr_tick + to->dticks;
		} else {
			k_ticks_t dticks = Z_TICK_ABS(timeout.ticks) - curr_tick;

			to->dticks = max(1, dticks);
```

이유는 "지금 tick의 어디쯤인지 모르니, 최소한 요청한 시간 이상을 보장하려면 1 tick을 더해야 한다"이다. 절대 타임아웃(`K_TIMEOUT_ABS_MS`)에는 더하지 않는다. 그래서 예제 2는 정확히 20 ms였고, 예제 5(POSIX 절대 → 내부 상대 변환)는 10 ms 늦었다. §4에서 이 한 줄이 OSAL 앱의 주기를 20 ms에서 30 ms로 바꾸는 것을 실측한다.

두 가지 함정을 더 적어 둔다.

- FreeRTOS의 `portMAX_DELAY`가 "영원히"인 것은 `INCLUDE_vTaskSuspend == 1`일 때뿐이다. `tasks.c`의 `prvAddCurrentTaskToDelayedList()`가 `#if ( INCLUDE_vTaskSuspend == 1 )` 안에서만 `xTicksToWait == portMAX_DELAY`를 무한 대기로 처리한다. 0이면 그냥 아주 긴 tick 수(32비트 tick에서 1 kHz 기준 약 49.7일)다.
- Zephyr의 `k_timeout_t`는 정수가 아니라서 `k_sem_take(&s, 100)`은 **컴파일 에러**다. 오히려 고마운 함정이다. 반대로 CMSIS·ThreadX·FreeRTOS에 ms 숫자를 그대로 넘기면 컴파일러는 아무 말도 안 하고, tick 주파수에 따라 뜻이 바뀐다. 예를 들어 RTX5 기본 설정(1 kHz)에서 쓰던 `osDelay(100)`(100 ms)은 Zephyr CMSIS 계층(이 랩 QEMU에서 tick 100 Hz)에서 **1초**가 된다.

### 3.3 ISR에서 부르기 — 이름으로 막느냐, 인자로 막느냐

ISR에서는 블록하면 안 된다. 기다릴 "태스크"가 없기 때문이다. RTOS마다 이 규칙을 강제하는 방법이 다르다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="14" width="205" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="118" y="37" font-size="14" text-anchor="middle">FreeRTOS</text>
<rect x="15" y="66" width="205" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="118" y="90" font-size="12" text-anchor="middle">ISR 전용 API 따로</text>
<rect x="15" y="118" width="205" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="118" y="142" font-size="11" text-anchor="middle">xSemaphoreGiveFromISR(s,&amp;w)</text>
<line x1="117" y1="106" x2="117" y2="117" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="15" y="170" width="205" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="118" y="194" font-size="12" text-anchor="middle">portYIELD_FROM_ISR(w)</text>
<line x1="117" y1="158" x2="117" y2="169" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="15" y="222" width="205" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="118" y="246" font-size="12" text-anchor="middle">일반 API → configASSERT</text>
<line x1="117" y1="210" x2="117" y2="221" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="237" y="14" width="205" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="340" y="37" font-size="14" text-anchor="middle">Zephyr</text>
<rect x="237" y="66" width="205" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="340" y="90" font-size="12" text-anchor="middle">같은 API 그대로</text>
<rect x="237" y="118" width="205" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="340" y="142" font-size="11" text-anchor="middle">k_sem_give(&amp;s)</text>
<line x1="339" y1="106" x2="339" y2="117" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="237" y="170" width="205" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="340" y="194" font-size="12" text-anchor="middle">take는 K_NO_WAIT만</text>
<line x1="339" y1="158" x2="339" y2="169" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="237" y="222" width="205" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="340" y="246" font-size="12" text-anchor="middle">ISR 끝에서 자동 재스케줄</text>
<line x1="339" y1="210" x2="339" y2="221" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="459" y="14" width="205" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="562" y="37" font-size="14" text-anchor="middle">ThreadX · CMSIS</text>
<rect x="459" y="66" width="205" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="562" y="90" font-size="12" text-anchor="middle">같은 API + 대기 0</text>
<rect x="459" y="118" width="205" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="562" y="142" font-size="11" text-anchor="middle">tx_semaphore_put(&amp;s)</text>
<line x1="561" y1="106" x2="561" y2="117" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="459" y="170" width="205" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="562" y="194" font-size="12" text-anchor="middle">osSemaphoreRelease(id)</text>
<line x1="561" y1="158" x2="561" y2="169" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="459" y="222" width="205" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="562" y="246" font-size="12" text-anchor="middle">대기 인자 ≠ 0 → 오류 반환</text>
<line x1="561" y1="210" x2="561" y2="221" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="20" y="290" font-size="12" text-anchor="start">핵심: '블록할 수 있는 호출'을 ISR에서 막는 방법이 이름(FreeRTOS)이냐 인자(나머지)냐의 차이</text>
</svg>
```

그림 4 — ISR 호출 규칙의 두 계열. FreeRTOS는 ISR용 함수를 따로 두고 스위치 요청까지 앱이 직접 한다. Zephyr·ThreadX·CMSIS는 같은 함수를 쓰되 "대기 시간 0"만 허용하고, ISR을 빠져나갈 때 커널이 알아서 재스케줄한다.

**예제 9 — FreeRTOS: ISR에서 `xSemaphoreGive`를 부르면?**

무엇을 확인하는 코드인지: 1 kHz 하드웨어 타이머(CMSDK TIMER0, IRQ 8) ISR에서 세마포어를 준다. 정상 버전은 `FromISR` + `portYIELD_FROM_ISR`, `-DWRONG` 버전은 태스크용 `xSemaphoreGive`.

```c
/* A5 ex8 — FreeRTOS: ISR에서 세마포어 주기. -DWRONG이면 FromISR 아닌 API를 부른다 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

static SemaphoreHandle_t sem;
static volatile uint32_t t_isr;

void TIMER0_IRQHandler(void) {                 /* 1 kHz HW 타이머 IRQ 8 */
    TIMER_INTCLR(TIMER0_BASE) = 1;
    t_isr = board_now_us();
#ifdef WRONG
    xSemaphoreGive(sem);                       /* 태스크용 API를 ISR에서 */
#else
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woken);        /* ISR용 API: 블록하지 않음 */
    portYIELD_FROM_ISR(woken);                 /* 깨운 태스크가 더 높으면 PendSV */
#endif
}
static void waiter(void *arg) {
    (void)arg;
    TIMER_RELOAD(TIMER0_BASE) = BOARD_CPU_HZ / 1000 - 1;
    TIMER_CTRL(TIMER0_BASE) = (1u << 0) | (1u << 3);
    NVIC_IPR(8) = 0x40;                        /* 0x20(MAX_SYSCALL)보다 숫자가 커야 API 허용 */
    NVIC_ISER0 = 1u << 8;
    for (int i = 0; i < 3; i++) {
        xSemaphoreTake(sem, portMAX_DELAY);
        board_printf_raw("woke #%d isr->task %u us\n", i, (unsigned)(board_now_us() - t_isr));
    }
    board_exit(0);
}
int main(void) {
    sem = xSemaphoreCreateBinary();            /* 초기값 0 (비어 있음) */
    xTaskCreate(waiter, "wait", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "" "-DWRONG"; do echo "== $v"; \
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/ex8/main.c \
  OUT=/var/tmp/rtos/A5/ex8$v EXTRA_CFLAGS="$v" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex8$v/app.elf; done'
```

```text
== 
woke #0 isr->task 10 us
woke #1 isr->task 10 us
woke #2 isr->task 10 us
[board_exit] code=0
== -DWRONG

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
```

출력에서 볼 것: 정상 버전은 ISR → 태스크 10 µs. 틀린 버전은 첫 인터럽트에서 바로 `port.c:438`에 걸린다. 그 줄은 `vPortEnterCritical()` 안이다.

```c
void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    /* This is not the interrupt safe version of the enter critical function so
     * assert() if it is being called from an interrupt context.  Only API
     * functions that end in "FromISR" can be used in an interrupt.  ... */
    if( uxCriticalNesting == 1 )
    {
        configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );
    }
}
```

`xSemaphoreGive` → `xQueueGenericSend` → `taskENTER_CRITICAL()` → 여기서 ICSR의 VECTACTIVE(지금 처리 중인 예외 번호)가 0이 아니면 assert한다. 즉 FreeRTOS는 "이름이 FromISR가 아닌 함수를 ISR에서 불렀다"를 **런타임에 하드웨어 레지스터로** 잡는다. `configASSERT`를 꺼 둔 양산 빌드에서는 이 검사도 사라지고, 대신 언젠가 리스트가 깨진다.

**예제 10 — Zephyr: 같은 API를 ISR에서**

무엇을 확인하는 코드인지: Zephyr에서는 `k_sem_give()`를 ISR에서 그대로 부른다. 여기서는 별도 HW 타이머 대신 `k_timer` 콜백을 쓰는데, 그 콜백이 **정말 ISR인지** `k_is_in_isr()`로 같이 확인한다. `-DWRONG`은 ISR에서 `k_sem_take(K_FOREVER)`를 부른다. 덤으로 `K_NO_WAIT`와 타임아웃 만료의 반환값이 다르다는 것도 찍는다.

```c
/* A5 ex9 — Zephyr: 같은 k_sem_give를 ISR(k_timer 콜백)에서. -DWRONG이면 ISR에서 take(K_FOREVER) */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

K_SEM_DEFINE(sem, 0, 1);                       /* 초기값 0, 최대 1 = binary */
static volatile uint32_t t_isr, in_isr;

static void tmr_cb(struct k_timer *t) {
	ARG_UNUSED(t);
	in_isr = k_is_in_isr();                    /* k_timer 콜백 = 시스템 타이머 ISR */
	t_isr = k_cycle_get_32();
#ifdef WRONG
	k_sem_take(&sem, K_FOREVER);               /* ISR에서 블록 시도 */
#endif
	k_sem_give(&sem);                          /* ISR이든 스레드든 같은 API */
}
K_TIMER_DEFINE(tmr, tmr_cb, NULL);

int main(void) {
	printk("take(K_NO_WAIT) on empty sem -> %d (-EBUSY=%d)\n",
	       k_sem_take(&sem, K_NO_WAIT), -EBUSY);
	printk("take(K_MSEC(30)) on empty sem -> %d (-EAGAIN=%d)\n",
	       k_sem_take(&sem, K_MSEC(30)), -EAGAIN);
	k_timer_start(&tmr, K_MSEC(10), K_MSEC(10));
	for (int i = 0; i < 3; i++) {
		k_sem_take(&sem, K_FOREVER);
		printk("woke #%d in_isr=%u isr->thread %u us\n", i, in_isr,
		       k_cyc_to_us_floor32(k_cycle_get_32() - t_isr));
	}
	k_timer_stop(&tmr);
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex9 /var/tmp/rtos/A5/ex9 >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex9/zephyr/zephyr.elf 3; \
  /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex9 /var/tmp/rtos/A5/ex9w -- -DEXTRA_CFLAGS=-DWRONG >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex9w/zephyr/zephyr.elf 3'
```

```text
*** Booting Zephyr OS build v4.4.2 ***
take(K_NO_WAIT) on empty sem -> -16 (-EBUSY=-16)
take(K_MSEC(30)) on empty sem -> -11 (-EAGAIN=-11)
woke #0 in_isr=1 isr->thread 28 us
woke #1 in_isr=1 isr->thread 28 us
woke #2 in_isr=1 isr->thread 28 us
qemu-system-arm: terminating on signal 15 from pid 976679 (timeout)
*** Booting Zephyr OS build v4.4.2 ***
take(K_NO_WAIT) on empty sem -> -16 (-EBUSY=-16)
take(K_MSEC(30)) on empty sem -> -11 (-EAGAIN=-11)
ASSERTION FAIL [((arch_is_in_isr() == 0) || ((timeout).ticks == (((k_timeout_t){0})).ticks))] @ WEST_TOPDIR/zephyr/kernel/sem.c:136
	
r0/a1:  0x00000004  r1/a2:  0x00000088  r2/a3:  0x00000000
r3/a4:  0x00000004 r12/ip:  0x00000000 r14/lr:  0x00001853
 xpsr:  0x2100000f
Faulting instruction address (r15/pc): 0x00003780
qemu-system-arm: terminating on signal 15 from pid 999507 (timeout)
```

출력에서 볼 것:

- `in_isr=1` — `k_timer` 콜백은 ISR이다. ISR → 스레드 28 µs.
- 틀린 버전은 `kernel/sem.c:136`의 `__ASSERT(((arch_is_in_isr() == false) || K_TIMEOUT_EQ(timeout, K_NO_WAIT)), "")`에 걸린다. `xpsr`의 아래 9비트(IPSR) = `0x0f` = 15 = **SysTick 예외** 안이었다는 증거다. 즉 Zephyr는 같은 함수 안에서 "ISR이면 `K_NO_WAIT`만"을 검사한다. 단 이 검사는 `CONFIG_ASSERT=y`일 때만 있다(이 예제는 켰다).
- 타임아웃의 두 얼굴: `K_NO_WAIT`로 실패하면 `-EBUSY`, 기다리다 만료되면 `-EAGAIN`. 호출자가 "왜 실패했는지" 구분할 수 있다. FreeRTOS는 둘 다 `pdFALSE`다.

ThreadX와 CMSIS-RTOS2는 Zephyr 쪽 계열이다. ThreadX는 ISR(또는 timer thread)에서 `wait_option`이 `TX_NO_WAIT`가 아니면 `TX_WAIT_ERROR`를 돌려주고(오류 검사 계층 `_txe_` 함수들, 예: `txe_semaphore_get.c`), CMSIS-RTOS2는 표준 문서에 "ISR에서 부를 수 있는 함수" 목록(`osSemaphoreRelease`, `osMessageQueuePut` 등, 대기 인자 0일 때)을 명시하고 그 밖의 호출에는 `osErrorISR`를 돌려준다.

정리하면 번역할 때 ISR 코드는 이렇게 바뀐다.

```c
/* FreeRTOS */
BaseType_t woken = pdFALSE;
xQueueSendFromISR(q, &sample, &woken);
portYIELD_FROM_ISR(woken);

/* Zephyr */
k_msgq_put(&q, &sample, K_NO_WAIT);          /* 실패(-ENOMSG)면 샘플 버림 — 정책을 정할 것 */

/* ThreadX */
tx_queue_send(&q, &sample, TX_NO_WAIT);

/* CMSIS-RTOS2 */
osMessageQueuePut(q, &sample, 0, 0);
```

FreeRTOS → Zephyr로 옮길 때 `portYIELD_FROM_ISR`를 지우는 것을 잊어도 큰 문제는 없지만(그 매크로가 없으니 컴파일 에러), 반대 방향(Zephyr → FreeRTOS)으로 옮길 때 일반 API를 ISR에 남기면 예제 9처럼 런타임에야 터진다. 우선순위 마스킹과 zero-latency IRQ의 세부 규칙은 E1·B3에서 다룬다.

### 3.4 타이머 콜백은 어디서 도나

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="150" y="40" width="510" height="46" rx="4" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a" stroke-width="1.3"/>
<text x="20" y="68" font-size="13" text-anchor="start" font-weight="bold">ISR 문맥</text>
<rect x="150" y="110" width="510" height="46" rx="4" fill="#e08a3c" fill-opacity="0.08" stroke="#e08a3c" stroke-width="1.3"/>
<text x="20" y="138" font-size="13" text-anchor="start" font-weight="bold">커널 타이머 스레드</text>
<rect x="150" y="180" width="510" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="20" y="208" font-size="13" text-anchor="start" font-weight="bold">일반 스레드</text>
<rect x="160" y="46" width="121" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="220" y="68" font-size="12" text-anchor="middle">Zephyr k_timer</text>
<rect x="300" y="46" width="128" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="364" y="68" font-size="12" text-anchor="middle">CMSIS on Zephyr</text>
<rect x="450" y="46" width="150" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="525" y="68" font-size="12" text-anchor="middle">POSIX SIGEV_SIGNAL</text>
<rect x="160" y="116" width="128" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="224" y="138" font-size="12" text-anchor="middle">FreeRTOS daemon</text>
<rect x="300" y="116" width="164" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="382" y="138" font-size="12" text-anchor="middle">ThreadX timer thread</text>
<rect x="480" y="116" width="142" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="551" y="138" font-size="12" text-anchor="middle">RTX5 timer thread</text>
<rect x="160" y="186" width="150" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="235" y="208" font-size="12" text-anchor="middle">POSIX SIGEV_THREAD</text>
<rect x="330" y="186" width="159" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/>
<text x="410" y="208" font-size="12" text-anchor="middle">Zephyr k_work (위임)</text>
<text x="20" y="250" font-size="12" text-anchor="start">콜백이 어느 줄에 있느냐가 '블록해도 되나 · 얼마나 길어도 되나'를 정한다</text>
</svg>
```

그림 5 — 타이머 콜백이 실행되는 문맥. 맨 위 줄(ISR 문맥)에 있는 콜백에서는 블록하는 호출을 할 수 없고, 짧게 끝나야 한다. POSIX `SIGEV_SIGNAL`은 엄밀히 ISR이 아니라 시그널 핸들러지만 async-signal-safe 함수만 써야 한다는 같은 종류의 제약을 받는다.

| RTOS | 콜백 문맥 | 근거 | 그 안에서 블록하면 |
|---|---|---|---|
| FreeRTOS | daemon 태스크 `prvTimerTask` (`timers.c`) | 랩 설정 `configTIMER_TASK_PRIORITY = 7` | 기술적으로 가능하지만 **모든 다른 타이머가 멈춘다** (D6) |
| Zephyr `k_timer` | 시스템 타이머 ISR | 예제 10 `in_isr=1` | assert 또는 정의되지 않은 동작. 일을 `k_work`로 넘긴다 (D7) |
| ThreadX | 시스템 timer thread `_tx_timer_thread_entry` | Cortex-M3 `tx_port.h`: `TX_TIMER_THREAD_PRIORITY` 기본 0 | 대기 호출이 `TX_WAIT_ERROR`로 거부된다 (`txe_semaphore_get.c`가 `current_thread == &_tx_timer_thread`를 검사) |
| CMSIS-RTOS2 | **구현 몫** | 표준 문서: "executed either in a dedicated timer thread or in interrupt context" | 그래서 표준은 "ISR에서 부를 수 있는 함수만 쓰라"고 권한다 |
| CMSIS on Zephyr | ISR (`k_timer` 위에 구현) | `cmsis_rtos_v2/timer.c`의 `zephyr_timer_wrapper` | Zephyr와 같음 |
| POSIX `timer_create` | `SIGEV_THREAD`: 새(또는 풀) 스레드, `SIGEV_SIGNAL`: 시그널 | 예제 4·5가 `SIGEV_THREAD` | 스레드면 가능, 시그널이면 async-signal-safe만 |

예를 들어 드론 비행 제어기의 "100 Hz 텔레메트리 타이머 콜백에서 UART 뮤텍스를 잡고 패킷을 보낸다"는 코드는 FreeRTOS에서는 (권장되진 않지만) 돌아가고, Zephyr로 옮기는 순간 ISR에서 `k_mutex_lock(K_FOREVER)`를 부르게 된다. 이식 체크리스트에 "타이머 콜백 안의 모든 호출"을 따로 적어 두는 이유다.

### 3.5 반환값 규약 — 0은 성공인가 실패인가

| 상황 | FreeRTOS | Zephyr | ThreadX | CMSIS-RTOS2 | POSIX |
|---|---|---|---|---|---|
| 성공 | `pdPASS` / `pdTRUE` = **1** | **0** | `TX_SUCCESS` = **0** | `osOK` = **0** | **0** (`pthread_*`), 0 (`sem_*`, `mq_*`) |
| 큐 가득 참 (안 기다림) | `errQUEUE_FULL` = **0** | `-ENOMSG` (-35) | `TX_QUEUE_FULL` (0x0B) | `osErrorResource` (-3) | -1 + `errno = EAGAIN` (`O_NONBLOCK` 큐) |
| 세마포어 없음 (안 기다림) | `pdFALSE` = 0 | `-EBUSY` (-16) | `TX_NO_INSTANCE` (0x0D) | `osErrorResource` | -1 + `errno = EAGAIN` (`sem_trywait`) |
| 기다리다 만료 | `pdFALSE` = 0 | `-EAGAIN` (-11) | `TX_NO_INSTANCE` 등 객체별 | `osErrorTimeout` (-2) | -1 + `errno = ETIMEDOUT` |
| 뮤텍스 남이 풀기 | `configASSERT` (`xTaskPriorityDisinherit`의 소유자 검사) | `-EPERM` | `TX_NOT_OWNED` (0x1E) | `osErrorResource` | `EPERM` (errorcheck 타입일 때) |
| ISR에서 금지 호출 | `configASSERT` | `__ASSERT` (켰을 때) | `TX_WAIT_ERROR` 등 | `osErrorISR` (-6) | 해당 없음 |

예제 6·7·8의 첫 줄이 이 표의 첫 두 행이다: FreeRTOS `send ok=1 full=0`, Zephyr `put ok=0 full=-35`, ThreadX `send ok=0x0 full=0xb`.

가장 위험한 번역 실수는 이것이다.

```c
/* FreeRTOS 원본: "보냈으면" */
if (xQueueSend(q, &m, 0)) { stats.sent++; }

/* Zephyr로 기계적으로 옮김: 이제 "실패했으면" sent++ — 논리 반전, 컴파일 경고 없음 */
if (k_msgq_put(&q, &m, K_NO_WAIT)) { stats.sent++; }
```

규칙: 번역할 때는 **항상 상수와 비교**한다(`== pdPASS`, `== 0`, `== TX_SUCCESS`, `== osOK`). 그리고 POSIX는 같은 POSIX 안에서도 둘로 갈린다. `pthread_mutex_lock()`은 **에러 번호를 반환값으로** 돌려주고 errno를 건드리지 않지만, `sem_wait()`·`mq_send()`는 **-1을 돌려주고 errno에** 이유를 넣는다(EL A8).

### 3.6 스택 크기 — word인가 byte인가

| RTOS | 스택 인자 | "1 KB 스택"을 쓰는 법 | 여유 확인 함수의 단위 |
|---|---|---|---|
| FreeRTOS | `configSTACK_DEPTH_TYPE` **word** (`StackType_t`, Cortex-M에서 4 B) | `xTaskCreate(…, 256, …)` | `uxTaskGetStackHighWaterMark()` — **word** |
| Zephyr | **byte** (`K_THREAD_STACK_DEFINE`, `K_THREAD_STACK_SIZEOF`) | `K_THREAD_STACK_DEFINE(stk, 1024)` | `k_thread_stack_space_get()` — **byte** |
| ThreadX | **byte** | `tx_thread_create(…, stk, 1024, …)` | `tx_thread_stack_highest_ptr` 등 — 주소 |
| CMSIS-RTOS2 | **byte** (`osThreadAttr_t.stack_size`) | `.stack_size = 1024` | `osThreadGetStackSpace()` — byte |
| POSIX | **byte** (`pthread_attr_setstacksize`) | `pthread_attr_setstacksize(&a, 1024)`는 대개 실패 — `PTHREAD_STACK_MIN` 이상 | — |

예제 6에서 `created with 256, high-water=218 (units: words of 4 B)` — 256 word(1 KB)를 줬고 아직 218 word(872 B)가 남았다. 예제 7은 `K_THREAD_STACK_SIZEOF=1024 bytes, unused=796 bytes`. 같은 "1 KB 스택"을 각자의 단위로 확인했다.

FreeRTOS → Zephyr로 옮기면서 `256`을 그대로 쓰면 스택이 **4분의 1**이 되어 스택 오버플로가 난다. 반대로 Zephyr → FreeRTOS로 `1024`를 그대로 쓰면 4 KB를 잡아 RAM이 4배 든다. 앞쪽은 크래시, 뒤쪽은 RAM 부족이다. 스택 크기 산정 자체는 F3에서 다룬다.

### 3.7 뮤텍스와 세마포어의 기본값

**뮤텍스: 재귀인가, 우선순위 상속(PI)은 기본인가**

| RTOS | 같은 소유자가 두 번 잠그면 | PI | 근거 |
|---|---|---|---|
| FreeRTOS `xSemaphoreCreateMutex` | **블록** (자기 자신을 기다림 = 교착) | 항상 (단순 상속) | 예제 6: `re-take by owner=0 after 50 ticks` |
| FreeRTOS recursive mutex | 성공, 횟수만큼 풀어야 함 | 항상 | 예제 6: `recursive take x2 = 1 1` |
| Zephyr `k_mutex` | 성공 (`lock_count` 증가) | 항상 | 예제 7: `lock x2 = 0 0, lock_count=2` |
| ThreadX `tx_mutex` | 성공 (`ownership_count` 증가) | 생성 시 선택 (`TX_INHERIT` / `TX_NO_INHERIT`) | 예제 8: `ownership_count=2` |
| CMSIS-RTOS2 | `osMutexRecursive` 비트에 따름 | `osMutexPrioInherit` 비트에 따름 | 표준 |
| CMSIS on Zephyr | **비트와 상관없이 성공** (`k_mutex`가 항상 재귀) | PI 비트를 빼면 assert | `mutex.c`: `__ASSERT(attr->attr_bits & osMutexPrioInherit, …)` |
| POSIX 기본 | 정의되지 않음 (glibc normal 타입은 교착) | 없음 (`PTHREAD_PRIO_INHERIT`로 켬) | EL A8 |

FreeRTOS 코드는 "재귀가 안 되는 게 기본"이라 같은 뮤텍스를 두 번 잡는 경로가 없도록 짜여 있다. Zephyr·ThreadX 코드는 재귀가 기본이라 "드라이버 함수가 이미 잡고 있을지도 모르니 또 잡는다" 같은 코드가 섞여 있을 수 있다. 이런 코드를 FreeRTOS로 옮기면 예제 6의 `after 50 ticks`처럼 타임아웃이 있으면 이상한 실패, `portMAX_DELAY`면 **영구 교착**이다. CMSIS-on-Zephyr의 "비트를 무시하는 재귀"는 OSAL이 의미를 숨기는 좋은 예다(§4.4).

ThreadX의 PI가 선택이라는 점도 중요하다. 예제 3에서 `TX_INHERIT`를 줬지만, 기존 코드베이스에서 `TX_NO_INHERIT`로 만든 뮤텍스를 FreeRTOS로 옮기면 PI가 "생긴다". 대개는 좋은 변화지만 타이밍 분석을 다시 해야 한다. 우선순위 역전 자체는 D3에서.

**세마포어: 초기값, 상한, 가득 찬 상태에서 give**

| RTOS | 생성 직후 값 | 인자 순서 | 가득 찬 상태에서 give |
|---|---|---|---|
| FreeRTOS binary | **0** (비어 있음 — 예제 6 `take(0) = 0`) | — | `pdFAIL` 반환 (길이 1 큐가 가득 참) |
| FreeRTOS counting | 지정 | `(max, initial)` | `pdFAIL` |
| Zephyr `k_sem` | 지정 | `(initial, limit)` | **조용히 무시** (count가 limit에서 멈춤, 반환값 없음) |
| ThreadX | 지정 | `(…, initial)` — 상한 없음 | 계속 증가 (ULONG). 상한이 필요하면 `tx_semaphore_ceiling_put()` |
| CMSIS-RTOS2 | 지정 | `(max, initial, attr)` | `osErrorResource` |
| POSIX `sem_t` | 지정 | `sem_init(&s, pshared, value)` | `SEM_VALUE_MAX`까지 증가, 넘으면 `EOVERFLOW` |

Zephyr의 "조용히 무시"는 `kernel/sem.c`의 한 줄이다.

```c
		sem->count += (sem->count != sem->limit) ? 1U : 0U;
```

그리고 `k_sem_give()`의 반환 타입은 `void`다. ISR이 이벤트를 너무 빨리 줘서 binary 세마포어가 이미 1이면, 두 번째 이벤트는 **흔적 없이 사라진다**. FreeRTOS에서는 `xSemaphoreGiveFromISR`의 반환값으로 "놓쳤다"를 셀 수 있었는데, Zephyr로 옮기면 그 카운터가 항상 0이 된다. ThreadX로 옮기면 반대로 count가 계속 쌓여서, binary로 쓰던 세마포어가 counting처럼 동작한다(대기 태스크가 깨어날 때마다 쌓인 수만큼 연속으로 돈다). 세마포어 설계 자체는 D1에서.

### 3.8 "스레드를 지운다"와 "함수에서 return한다"

**예제 11 — FreeRTOS 태스크 함수가 return하면?**

무엇을 확인하는 코드인지: Zephyr·ThreadX·POSIX에서는 스레드 함수가 return하면 그 스레드가 정상 종료된다. FreeRTOS에서는?

```c
/* A5 ex13 — FreeRTOS 태스크 함수가 return하면? (Zephyr·ThreadX·POSIX에선 정상 종료) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void worker(void *arg) {
    (void)arg;
    board_printf_raw("worker: done, returning...\n");
#ifdef FIX
    vTaskDelete(NULL);                 /* FreeRTOS에서 태스크를 끝내는 유일한 정상 경로 */
#endif
}                                      /* 돌아갈 곳이 없다: LR = prvTaskExitError */
static void watcher(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10));
    board_printf_raw("watcher: worker gone cleanly\n");
    board_exit(0);
}
int main(void) {
    xTaskCreate(worker, "worker", 256, NULL, 3, NULL);
    xTaskCreate(watcher, "watch", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "" "-DFIX"; do echo "== $v"; \
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/ex13/main.c \
  OUT=/var/tmp/rtos/A5/ex13$v EXTRA_CFLAGS="$v" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex13$v/app.elf 5; done'
```

```text
== 
worker: done, returning...

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:212
[board_exit] code=253
== -DFIX
worker: done, returning...
watcher: worker gone cleanly
[board_exit] code=0
```

출력에서 볼 것: return한 순간 `port.c:212`, 즉 `prvTaskExitError()`의 `configASSERT( uxCriticalNesting == ~0UL )`(일부러 실패시키는 assert)에 걸렸다. 포트가 태스크의 첫 스택 프레임을 만들 때 LR(복귀 주소)에 `prvTaskExitError`를 넣어 두기 때문이다(C3에서 스택 프레임을 직접 본다). 소스 주석이 그대로 말한다: "A function that implements a task must not exit or attempt to return to its caller as there is nothing to return to. If a task wants to exit it should instead call vTaskDelete( NULL )."

| RTOS | 함수에서 return | 남이 죽이기 | 메모리는 언제 돌아오나 |
|---|---|---|---|
| FreeRTOS | **금지** (`prvTaskExitError`) | `vTaskDelete(h)` | 자기 삭제면 idle 태스크가 나중에 해제. idle이 굶으면 안 돌아온다 |
| Zephyr | 정상 종료 (`k_thread_join()` 가능) | `k_thread_abort()` | 스택은 앱 소유(정적)라 그대로. 같은 `struct k_thread`로 다시 만들 수 있다 |
| ThreadX | `TX_COMPLETED` 상태로 남음 (예제 8: `other state=1`) | `tx_thread_terminate()` 후 `tx_thread_delete()` — 순서가 바뀌면 `TX_DELETE_ERROR` (예제 8: `0x11`) | 스택·TCB는 앱 소유. delete 후 재사용 |
| CMSIS-RTOS2 | `osThreadExit()` 권장 | `osThreadTerminate()` | 구현 몫 |
| POSIX | 정상 종료 (`pthread_exit`과 같음) | `pthread_cancel()` — 즉시가 아니라 다음 취소 지점에서 | `pthread_join()` 또는 detach 후 |

ThreadX의 "terminate 후 delete" 두 단계는 처음 보면 번거롭지만 이유가 있다. terminate는 상태만 바꾸고, delete는 커널 목록에서 빼서 그 메모리를 앱이 다시 써도 되게 한다. FreeRTOS의 `vTaskDelete()` 한 번이 이 둘을 다 한다.

### 3.9 시작 순서 — 누가 `main`을 쥐는가

| RTOS | main에서 하는 일 | 스케줄러 시작 | 객체는 언제 만드나 |
|---|---|---|---|
| FreeRTOS | 객체·태스크 생성 후 `vTaskStartScheduler()` | 그 호출에서 (돌아오지 않음) | 시작 전에 main에서, 또는 태스크 안에서 |
| Zephyr | 그냥 "우선순위 0 스레드의 본문" | `main()` 전에 이미 | 대부분 컴파일 시점 매크로(`K_*_DEFINE`)로 |
| ThreadX | `tx_kernel_enter()` 한 줄 | 그 안에서 `tx_application_define()` 콜백 후 | `tx_application_define()` 안에서 |
| CMSIS-RTOS2 | `osKernelInitialize()` → 생성 → `osKernelStart()` | `osKernelStart()` | 그 사이 |
| CMSIS on Zephyr | 위 두 함수가 **없다** — 링크 에러 (§5 예제 13) | Zephyr 그대로 | `main()` 안에서 |
| POSIX | 그냥 `main()` | 프로세스 시작 시 | 아무 때나 |

Zephyr로 옮길 때 가장 자주 나오는 질문이 "`vTaskStartScheduler()`는 어디에 두나요?"다. 답은 "지운다"다. 그리고 Zephyr의 `main()`은 return해도 시스템이 멈추지 않는다(main 스레드만 끝난다). FreeRTOS의 `main()`이 return하면 `startup.c`가 그 값을 받아 끝내 버린다(이 랩에서는 `board_exit(main())`).

---

## 4. OSAL — 직접 만들어 보기

### 4.1 왜 회사들은 OSAL을 만드나

Don의 SSD 펌웨어에도 비슷한 층이 있었을 것이다. 코어 간 메시지, 타이머, 락을 감싼 사내 함수들. 이유는 대개 네 가지다.

- **RTOS 교체 대비**: 제품군마다 RTOS가 다르거나, 라이선스·인증 때문에 바꿀 가능성이 있다(예: FreeRTOS 제품을 SAFERTOS 같은 인증 커널로 옮기기 — A6).
- **호스트 테스트**: 같은 앱을 PC에서 돌리려면 RTOS 호출을 가짜(mock)나 POSIX로 바꿔 끼워야 한다(M8).
- **벤더 SDK 혼합**: 벤더 드라이버는 ThreadX, 앱은 FreeRTOS인 식의 조합을 한 이름으로 묶는다.
- **규칙을 한 곳에**: ms → tick 변환, 우선순위 매핑, 반환값 규약을 OSAL 한 파일에서 정하면 앱 전체가 일관된다.

ThreadX 저장소에 `utility/rtos_compatibility_layers/FreeRTOS`(FreeRTOS API를 ThreadX 위에 구현), `posix`, `OSEK` 계층이 들어 있는 것도 같은 수요 때문이다. 반대 방향으로는 Arm이 CMSIS-RTOS2를 표준 OSAL로 내놓았다(§5).

### 4.2 규약부터 정한다

OSAL의 핵심은 함수 이름이 아니라 **규약**이다. 이 노트의 작은 OSAL `os.h`는 다음을 약속한다.

| 항목 | 규약 | FreeRTOS 백엔드 | Zephyr 백엔드 |
|---|---|---|---|
| 우선순위 | 0~6, 큰 숫자 = 높음 | `prio + 1` (idle 0 피함) | `7 - prio` |
| 시간 | ms, `OS_FOREVER` | `pdMS_TO_TICKS()` / `portMAX_DELAY` | `K_MSEC()` / `K_FOREVER` |
| 반환 | 0 = 성공, -1 = 실패·타임아웃 | `== pdPASS` 비교 | `== 0` 비교 |
| 스택 | 바이트 | `÷ sizeof(StackType_t)` | 정적 스택 풀(1 KB × 4)에서 |
| 태스크 종료 | 함수에서 return 해도 됨 | 트램펄린이 `vTaskDelete(NULL)` | 그대로 |
| 타이머 콜백 | "블록 금지" (문맥을 약속하지 않음) | daemon 태스크 | ISR |
| 시작 | `os_start()` | `vTaskStartScheduler()` | 아무것도 안 함 |

### 4.3 예제 12 — `os.h` (헤더 하나, 두 백엔드)

무엇을 확인하는 코드인지: `static inline` 함수만으로 만든 OSAL. 앞쪽은 FreeRTOS 백엔드, 뒤쪽은 Zephyr 백엔드다(한 파일, 70줄 제한 때문에 두 블록으로 나눠 보인다).

```c
/* os.h — 아주 작은 OSAL. 백엔드: -DOS_FREERTOS 또는 -DOS_ZEPHYR
 * 규약: 우선순위 0..6 (큰 숫자 = 높음), 시간은 ms, 반환 0 = 성공 / -1 = 실패·타임아웃 */
#ifndef OS_H
#define OS_H
#include <stdint.h>
#include <stddef.h>
#define OS_FOREVER 0xFFFFFFFFu
typedef void (*os_task_fn)(void *arg);
typedef void (*os_timer_fn)(void);

#if defined(OS_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "board.h"
typedef QueueHandle_t os_queue_t;
typedef SemaphoreHandle_t os_mutex_t;
static inline TickType_t os__to(uint32_t ms) { return ms == OS_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ms); }
#define OS_MAX_TASKS 4
static struct { os_task_fn fn; void *arg; } os__ta[OS_MAX_TASKS]; static int os__nth;
static void os__entry(void *p)                 /* FreeRTOS 태스크는 return 금지 → 대신 지운다 */
{ os__ta[(int)p].fn(os__ta[(int)p].arg); vTaskDelete(NULL); }
static inline int os_task_create(os_task_fn fn, const char *name, size_t stack_bytes, int prio, void *arg)
{ if (os__nth >= OS_MAX_TASKS) return -1;
  os__ta[os__nth].fn = fn; os__ta[os__nth].arg = arg;   /* bytes → words, 0..6 → 1..7 */
  return xTaskCreate(os__entry, name, stack_bytes / sizeof(StackType_t), (void *)os__nth++,
                     prio + 1, NULL) == pdPASS ? 0 : -1; }
static inline os_queue_t os_queue_create(size_t depth, size_t item) { return xQueueCreate(depth, item); }
static inline int os_queue_send(os_queue_t q, const void *p, uint32_t ms) { return xQueueSend(q, p, os__to(ms)) == pdPASS ? 0 : -1; }
static inline int os_queue_recv(os_queue_t q, void *p, uint32_t ms) { return xQueueReceive(q, p, os__to(ms)) == pdPASS ? 0 : -1; }
static inline os_mutex_t os_mutex_create(void) { return xSemaphoreCreateMutex(); }
static inline void os_mutex_lock(os_mutex_t m) { xSemaphoreTake(m, portMAX_DELAY); }
static inline void os_mutex_unlock(os_mutex_t m) { xSemaphoreGive(m); }
static inline void os_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static inline uint32_t os_now_ms(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static inline uint32_t os_now_us(void) { return board_now_us(); }
static void os__tmr(TimerHandle_t t) { ((os_timer_fn)pvTimerGetTimerID(t))(); }
static inline int os_timer_periodic(os_timer_fn fn, uint32_t ms)
{ TimerHandle_t t = xTimerCreate("osal", pdMS_TO_TICKS(ms), pdTRUE, (void *)fn, os__tmr);
  return (t && xTimerStart(t, 0) == pdPASS) ? 0 : -1; }
static inline void os_start(void) { vTaskStartScheduler(); }
#define os_printf board_printf_raw
static inline void os_exit(int code) { board_exit(code); }
#define OS_NAME "FreeRTOS"

```

```c
#elif defined(OS_ZEPHYR)
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
typedef struct k_msgq *os_queue_t;
typedef struct k_mutex *os_mutex_t;
#define OS_MAX_TASKS 4
K_THREAD_STACK_ARRAY_DEFINE(os__stk, OS_MAX_TASKS, 1024);
static struct k_thread os__th[OS_MAX_TASKS]; static int os__nth;
static inline k_timeout_t os__to(uint32_t ms) { return ms == OS_FOREVER ? K_FOREVER : K_MSEC(ms); }
static void os__entry(void *fn, void *arg, void *u) { (void)u; ((os_task_fn)fn)(arg); }
static inline int os_task_create(os_task_fn fn, const char *name, size_t stack_bytes, int prio, void *arg)
{ if (os__nth >= OS_MAX_TASKS || stack_bytes > 1024) return -1;
  k_tid_t t = k_thread_create(&os__th[os__nth], os__stk[os__nth], stack_bytes, os__entry,
                              (void *)fn, arg, NULL, 7 - prio, 0, K_NO_WAIT);
  os__nth++; k_thread_name_set(t, name); return 0; }
static inline os_queue_t os_queue_create(size_t depth, size_t item)
{ struct k_msgq *q = k_malloc(sizeof *q);
  return (q && k_msgq_alloc_init(q, item, depth) == 0) ? q : NULL; }
static inline int os_queue_send(os_queue_t q, const void *p, uint32_t ms) { return k_msgq_put(q, p, os__to(ms)) == 0 ? 0 : -1; }
static inline int os_queue_recv(os_queue_t q, void *p, uint32_t ms) { return k_msgq_get(q, p, os__to(ms)) == 0 ? 0 : -1; }
static inline os_mutex_t os_mutex_create(void) { struct k_mutex *m = k_malloc(sizeof *m); if (m) k_mutex_init(m); return m; }
static inline void os_mutex_lock(os_mutex_t m) { k_mutex_lock(m, K_FOREVER); }
static inline void os_mutex_unlock(os_mutex_t m) { k_mutex_unlock(m); }
static inline void os_sleep_ms(uint32_t ms) { k_msleep(ms); }
static inline uint32_t os_now_ms(void) { return k_uptime_get_32(); }
static inline uint32_t os_now_us(void) { return k_cyc_to_us_floor32(k_cycle_get_32()); }
static struct k_timer os__t; static os_timer_fn os__tfn;
static void os__tmr(struct k_timer *t) { (void)t; os__tfn(); }   /* ISR 문맥 */
static inline int os_timer_periodic(os_timer_fn fn, uint32_t ms)
{ os__tfn = fn; k_timer_init(&os__t, os__tmr, NULL); k_timer_start(&os__t, K_MSEC(ms), K_MSEC(ms)); return 0; }
static inline void os_start(void) { }        /* Zephyr는 main 전에 이미 스케줄러가 돈다 */
#define os_printf printk
static inline void os_exit(int code)        /* semihosting SYS_EXIT (QEMU 종료) */
{ register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = code ? 0x20023 : 0x20026;
  __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory"); for (;;) { } }
#define OS_NAME "Zephyr"
#else
#error "define OS_FREERTOS or OS_ZEPHYR"
#endif
#endif
```

Zephyr 백엔드에서 눈여겨볼 곳: FreeRTOS처럼 "스택을 커널이 알아서 할당"하는 API가 기본으로 없어서(`CONFIG_DYNAMIC_THREAD`를 켜면 있다) 정적 스택 풀을 미리 잡아 두고, 큐·뮤텍스는 `k_malloc()`으로 만든다. **OSAL을 맞추려고 한쪽 RTOS의 관례(정적 정의)를 버리게 된다**는 것이 이미 보인다.

이제 앱이다. 앱은 `os.h`만 안다.

```c
/* A5 ex7 — OSAL 앱: os.h만 쓴다. FreeRTOS와 Zephyr에 그대로 빌드된다 */
#include "os.h"

typedef struct { uint32_t seq; uint32_t t_us; } msg_t;
static os_queue_t q;
static os_mutex_t mtx;
static volatile uint32_t shared, timer_fires;

static void bump(void) { os_mutex_lock(mtx); shared++; os_mutex_unlock(mtx); }
static void on_timer(void) { timer_fires++; }       /* 블록 금지 (어느 문맥인지 모름) */

static void producer(void *arg) {
    (void)arg;
    for (uint32_t i = 1; i <= 5; i++) {
        os_sleep_ms(20);                              /* OSAL엔 delay-until이 없다 */
        msg_t m = { i, os_now_us() };
        bump();
        if (os_queue_send(q, &m, 0) != 0) os_printf("queue full!\n");
    }
}
static void consumer(void *arg) {
    (void)arg; msg_t m;
    for (int got = 0; got < 5; ) {
        if (os_queue_recv(q, &m, 100) == 0) {
            uint32_t lat = os_now_us() - m.t_us;
            bump(); got++;
            os_printf("[%u ms] cons seq=%u lat=%u us\n", (unsigned)os_now_ms(),
                      (unsigned)m.seq, (unsigned)lat);
        } else os_printf("timeout\n");
    }
    os_printf("DONE shared=%u timer_fires=%u\n", (unsigned)shared, (unsigned)timer_fires);
    os_exit(0);
}
int main(void) {
    q = os_queue_create(4, sizeof(msg_t));
    mtx = os_mutex_create();
    os_timer_periodic(on_timer, 30);
    os_task_create(producer, "prod", 1024, 2, NULL);
    os_task_create(consumer, "cons", 1024, 3, NULL); /* OSAL 규약: 큰 숫자 = 높음 */
    os_printf("OSAL app on %s\n", OS_NAME);
    os_start();
    return 0;
}
```

```sh
# FreeRTOS로
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/osal/app.c \
  OUT=/var/tmp/rtos/A5/ex7f EXTRA_CFLAGS=-DOS_FREERTOS && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex7f/app.elf'
# Zephyr로 (app_z/CMakeLists.txt가 OS_ZEPHYR를 정의하고 ../app.c를 빌드)
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/osal/app_z /var/tmp/rtos/A5/ex7z && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex7z/zephyr/zephyr.elf 5'
```

```text
   text	   data	    bss	    dec	    hex	filename
  14248	    100	 135456	 149804	  2492c	/var/tmp/rtos/A5/ex7f/app.elf
OSAL app on FreeRTOS
[20 ms] cons seq=1 lat=18 us
[40 ms] cons seq=2 lat=18 us
[60 ms] cons seq=3 lat=18 us
[80 ms] cons seq=4 lat=18 us
[100 ms] cons seq=5 lat=18 us
DONE shared=10 timer_fires=3
[board_exit] code=0
```

```text
           FLASH:       16292 B         4 MB      0.39%
             RAM:       13156 B         4 MB      0.31%
*** Booting Zephyr OS build v4.4.2 ***
OSAL app on Zephyr
[30 ms] cons seq=1 lat=25 us
[60 ms] cons seq=2 lat=25 us
[90 ms] cons seq=3 lat=25 us
[120 ms] cons seq=4 lat=25 us
[150 ms] cons seq=5 lat=25 us
DONE shared=10 timer_fires=5
```

출력에서 볼 것: **같은 앱, 같은 `os_sleep_ms(20)`인데** FreeRTOS는 20 ms 주기, Zephyr는 **30 ms 주기**다. 그래서 끝나는 시각이 150 ms로 늘고 `timer_fires`도 3이 아니라 5다. 컴파일 에러도 경고도 없었다.

### 4.4 숨은 의미가 튀어나온 순간

원인은 §3.2에서 본 두 가지가 겹친 것이다.

1. 이 QEMU 보드의 Zephyr tick은 100 Hz(10 ms)다.
2. `k_msleep(20)`은 상대 타임아웃이라 `20 ms = 2 tick + 1 tick` = **3 tick = 30 ms**를 잔다. FreeRTOS `vTaskDelay(20)`은 1 kHz에서 20 tick이고, 태스크가 tick 경계에서 깨어난 직후에 다시 호출되므로 정확히 20 ms가 된다.

tick을 1 kHz로 올려서 다시 돌리면 확인된다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/osal/app_z /var/tmp/rtos/A5/ex7z1k \
  -- -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=1000 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex7z1k/zephyr/zephyr.elf 5'
```

```text
*** Booting Zephyr OS build v4.4.2 ***
OSAL app on Zephyr
[21 ms] cons seq=1 lat=18 us
[42 ms] cons seq=2 lat=18 us
[63 ms] cons seq=3 lat=25 us
[84 ms] cons seq=4 lat=18 us
[105 ms] cons seq=5 lat=18 us
DONE shared=10 timer_fires=3
```

이번에는 21 ms 주기다. 여전히 +1 tick(1 ms)이 붙는다. 주기 실행을 "상대 sleep"으로 만들면 **RTOS마다 다른 크기의 drift**가 생긴다는 것을 세 줄의 숫자가 보여 준다.

```svg
<svg viewBox="0 0 660 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="170" y1="200" x2="600" y2="200" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="170" y1="196" x2="170" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="170" y="220" font-size="12" text-anchor="middle">0</text>
<line x1="219" y1="196" x2="219" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="219" y="220" font-size="12" text-anchor="middle">20</text>
<line x1="268" y1="196" x2="268" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="268" y="220" font-size="12" text-anchor="middle">40</text>
<line x1="316" y1="196" x2="316" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="316" y="220" font-size="12" text-anchor="middle">60</text>
<line x1="365" y1="196" x2="365" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="365" y="220" font-size="12" text-anchor="middle">80</text>
<line x1="414" y1="196" x2="414" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="414" y="220" font-size="12" text-anchor="middle">100</text>
<line x1="462" y1="196" x2="462" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="462" y="220" font-size="12" text-anchor="middle">120</text>
<line x1="511" y1="196" x2="511" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="511" y="220" font-size="12" text-anchor="middle">140</text>
<line x1="560" y1="196" x2="560" y2="204" stroke="currentColor" stroke-width="1.3"/>
<text x="560" y="220" font-size="12" text-anchor="middle">160</text>
<text x="640" y="240" font-size="12" text-anchor="end">시간 (ms, QEMU icount)</text>
<text x="20" y="54" font-size="13" text-anchor="start" font-weight="bold">FreeRTOS 1 kHz</text>
<line x1="170" y1="50" x2="414" y2="50" stroke="#3f9a6b" stroke-width="1.3" stroke-dasharray="4 3"/>
<circle cx="219" cy="50" r="5" fill="#3f9a6b" stroke="#3f9a6b"/>
<circle cx="268" cy="50" r="5" fill="#3f9a6b" stroke="#3f9a6b"/>
<circle cx="316" cy="50" r="5" fill="#3f9a6b" stroke="#3f9a6b"/>
<circle cx="365" cy="50" r="5" fill="#3f9a6b" stroke="#3f9a6b"/>
<circle cx="414" cy="50" r="5" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="424" y="54" font-size="12" text-anchor="start">20 ms 간격</text>
<text x="20" y="104" font-size="13" text-anchor="start" font-weight="bold">Zephyr 100 Hz</text>
<line x1="170" y1="100" x2="536" y2="100" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3"/>
<circle cx="243" cy="100" r="5" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="316" cy="100" r="5" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="389" cy="100" r="5" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="462" cy="100" r="5" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="536" cy="100" r="5" fill="#e08a3c" stroke="#e08a3c"/>
<text x="546" y="104" font-size="12" text-anchor="start">30 ms 간격</text>
<text x="20" y="154" font-size="13" text-anchor="start" font-weight="bold">Zephyr 1 kHz</text>
<line x1="170" y1="150" x2="426" y2="150" stroke="#4a7bd0" stroke-width="1.3" stroke-dasharray="4 3"/>
<circle cx="221" cy="150" r="5" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="272" cy="150" r="5" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="324" cy="150" r="5" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="375" cy="150" r="5" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="426" cy="150" r="5" fill="#4a7bd0" stroke="#4a7bd0"/>
<text x="436" y="154" font-size="12" text-anchor="start">21 ms 간격</text>
<text x="20" y="245" font-size="12" text-anchor="start">점 = consumer가 메시지를 받은 시각</text>
</svg>
```

그림 6 — 같은 OSAL 앱에서 consumer가 메시지를 받은 시각(실측). 초록은 FreeRTOS 1 kHz, 주황은 Zephyr 100 Hz(QEMU 기본), 파랑은 Zephyr 1 kHz. 같은 `os_sleep_ms(20)`이 20 / 30 / 21 ms 주기가 된다.

고치는 법은 OSAL에 `os_sleep_until_ms(&next, period)`를 추가해서 FreeRTOS는 `xTaskDelayUntil()`, Zephyr는 `k_sleep(K_TIMEOUT_ABS_MS())`로 매핑하는 것이다(연습문제 3). 즉 **최소공통분모(lowest common denominator)로 API를 줄이면, 빠진 기능이 앱 쪽에서 더 나쁜 방식으로 다시 구현된다.**

이 OSAL에는 숨은 버그가 하나 더 있었다. 처음 만든 FreeRTOS 백엔드는 `xTaskCreate(fn, …)`에 앱 함수를 그대로 넘겼다. 그런데 producer는 5번 보내고 **return**한다. §3.8에서 본 대로 FreeRTOS에서는 금지다. 그런데도 첫 실행 출력은 멀쩡했다. producer가 마지막 메시지를 보내는 순간 더 높은 consumer가 선점해서 `board_exit(0)`으로 끝내 버려, producer가 return할 기회가 없었기 때문이다. 우선순위를 바꾸거나 consumer가 조금만 늦게 끝났다면 `prvTaskExitError`가 터졌을 것이다. 그래서 위 코드처럼 **트램펄린**(`os__entry`가 앱 함수를 부른 뒤 `vTaskDelete(NULL)`)을 넣었다. "OSAL 규약: return 해도 된다"를 FreeRTOS 위에서 지키려면 이런 접착 코드가 필요하다.

### 4.5 OSAL의 장단점

| 관점 | 장점 | 단점 / 함정 |
|---|---|---|
| 이식성 | 앱 소스를 바꾸지 않고 RTOS 교체 | 컴파일만 되고 **타이밍·문맥 의미가 바뀐다** (§4.3의 20 → 30 ms) |
| 기능 | 공통 개념만 남아 단순 | 최소공통분모: delay-until, task notification, FromISR, 큐 앞에 넣기, 정적 할당 같은 고유 기능이 사라진다 |
| 성능 | `static inline`이면 비용이 거의 0 (§6에서 측정: 왕복 차이 1 ns 이내) | 함수 호출·변환·검사를 넣는 계층은 비용이 있다 (CMSIS-on-Zephyr: 호출당 약 28명령) |
| 메모리 | — | 한쪽 RTOS의 관례(정적 정의)를 버리고 동적 할당·풀을 넣게 된다 (Zephyr 백엔드의 `k_malloc`) |
| 디버깅 | — | RTOS-aware 디버거·트레이스는 커널 객체 이름으로 보여 준다. OSAL 이름과 연결은 사람 몫 (M2) |
| 안전 인증 | 인증된 커널로 교체 쉬움 | OSAL 자체도 검증 대상이 된다 (O 모듈) |

좋은 OSAL을 만드는 규칙을 정리하면 이렇다.

- 이름이 아니라 **의미**를 명세한다: 주기 대기는 절대 시각, 타임아웃 반올림은 올림, 타이머 콜백은 "ISR과 같은 제약", 반환값은 열거형.
- ISR용 함수를 **이름으로 분리**한다(`os_queue_send_isr`). Zephyr처럼 같은 함수로 합쳐 두면 FreeRTOS 백엔드에서 문맥을 판별하는 비용(`xPortIsInsideInterrupt()`)을 매번 치르거나 실수를 놓친다.
- 우선순위 매핑과 tick 변환을 **한 곳**에만 둔다.
- 정적 할당 API를 기본으로 둔다(객체 메모리를 호출자가 준다). 동적 할당은 선택으로.
- 백엔드마다 같은 테스트(이 노트의 2.1 사양 같은 것)를 CI에서 돌려 **출력이 같은지** 비교한다. 예제 12는 그 테스트가 주기 차이를 바로 잡아낸 사례다.

---

## 5. CMSIS-RTOS2 — 표준화된 OSAL

### 5.1 무엇이고 어디에 있나

CMSIS-RTOS2는 Arm이 정한 RTOS API 표준이다. 원래 구현은 Arm의 RTX5이고, FreeRTOS 위의 구현(CMSIS-FreeRTOS)이 있으며, Zephyr는 `subsys/portability/cmsis_rtos_v2/`에 호환 계층을 갖고 있다. v4.4.2에서 이 계층은 그대로 쓸 수 있다. 헤더는 Zephyr 트리 안의 `include/zephyr/portability/cmsis_os2.h`이고(랩의 `modules/hal/cmsis_6`에도 표준 헤더와 문서가 있다), 구현 파일은 `thread.c`, `kernel.c`, `timer.c`, `mutex.c`, `semaphore.c`, `mempool.c`, `msgq.c`, `event_flags.c`, `thread_flags.c`다.

장점은 분명하다. **"OSAL을 직접 만들지 말고 표준을 쓰자"**. 함수 이름·반환값(`osStatus_t`)·우선순위 enum·ISR 호출 가능 목록이 문서로 정해져 있다. 단점은 §4.5의 OSAL 단점을 그대로 물려받는다는 것이다. 표준이 **의미를 구현에 맡긴 곳**(타이머 콜백 문맥, tick 주파수, `msg_prio` 해석)에서 구현마다 다르게 동작한다.

### 5.2 예제 13 — 같은 프로그램을 CMSIS-RTOS2로, Zephyr 위에서

무엇을 확인하는 코드인지: 2.1 사양을 `os*` 함수로. ms가 아니라 **tick**을 받는 API라 `ms2tick()`을 직접 만들었다. 표준 CMSIS 앱의 첫 줄인 `osKernelInitialize()`는 `USE_KERNEL_START`를 정의할 때만 넣었다.

```c
/* A5 ex6 — CMSIS-RTOS2 API (Zephyr 호환 계층): 같은 producer/queue/consumer/timer/mutex */
#include <cmsis_os2.h>
#include <zephyr/sys/printk.h>

typedef struct { uint32_t seq; uint32_t t_cnt; } msg_t;
static osMessageQueueId_t q;
static osMutexId_t mtx;
static osTimerId_t tick30;
static volatile uint32_t shared, timer_fires;

static uint32_t ms2tick(uint32_t ms) { return ms * osKernelGetTickFreq() / 1000U; }
static void bump(void) { osMutexAcquire(mtx, osWaitForever); shared++; osMutexRelease(mtx); }
static void timer_cb(void *arg) { (void)arg; timer_fires++; } /* Zephyr에선 ISR 문맥 */

static void producer(void *arg) {
	(void)arg;
	uint32_t next = osKernelGetTickCount();
	for (uint32_t i = 1; i <= 5; i++) {
		next += ms2tick(20);
		osDelayUntil(next);
		msg_t m = { i, osKernelGetSysTimerCount() };
		bump();
		if (osMessageQueuePut(q, &m, 0, 0) != osOK) printk("queue full!\n");
	}
}
static void consumer(void *arg) {
	(void)arg; msg_t m;
	for (int got = 0; got < 5; ) {
		if (osMessageQueueGet(q, &m, NULL, ms2tick(100)) == osOK) {
			uint32_t d = osKernelGetSysTimerCount() - m.t_cnt;
			uint32_t lat = (uint32_t)((uint64_t)d * 1000000U / osKernelGetSysTimerFreq());
			bump(); got++;
			printk("[%u ms] cons seq=%u lat=%u us\n",
			       osKernelGetTickCount() * 1000U / osKernelGetTickFreq(), m.seq, lat);
		} else printk("timeout\n");
	}
	osTimerStop(tick30);
	printk("DONE shared=%u timer_fires=%u\n", shared, timer_fires);
}
int main(void) {
#ifdef USE_KERNEL_START
	osKernelInitialize();                       /* 표준 CMSIS 앱의 첫 줄 */
#endif
	char id[32]; osVersion_t v;
	osKernelGetInfo(&v, id, sizeof id);
	printk("CMSIS-RTOS2 on %s api=0x%x kernel=0x%x tickfreq=%u\n", id, v.api, v.kernel,
	       osKernelGetTickFreq());
	q   = osMessageQueueNew(4, sizeof(msg_t), NULL);
	mtx = osMutexNew(NULL);
	tick30 = osTimerNew(timer_cb, osTimerPeriodic, NULL, NULL);
	osTimerStart(tick30, ms2tick(30));
	const osThreadAttr_t pa = { .name = "prod", .stack_size = 1024, .priority = osPriorityNormal };
	const osThreadAttr_t ca = { .name = "cons", .stack_size = 1024, .priority = osPriorityAboveNormal };
	osThreadNew(producer, NULL, &pa);           /* 큰 enum 값 = 높은 우선순위 */
	osThreadNew(consumer, NULL, &ca);
	return 0;
}
```

`prj.conf`는 이렇게 길어진다. 대부분이 CMSIS 계층의 `depends on`과 동적 할당 한도다.

```text
CONFIG_CMSIS_RTOS_V2=y
CONFIG_NUM_PREEMPT_PRIORITIES=56
CONFIG_POLL=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_THREAD_MONITOR=y
CONFIG_INIT_STACKS=y
CONFIG_CMSIS_V2_THREAD_MAX_STACK_SIZE=1024
CONFIG_ASSERT=y
CONFIG_CMSIS_V2_THREAD_DYNAMIC_MAX_COUNT=2
CONFIG_CMSIS_V2_THREAD_DYNAMIC_STACK_SIZE=1024
CONFIG_CMSIS_V2_MSGQ_MAX_DYNAMIC_SIZE=64
CONFIG_HEAP_MEM_POOL_SIZE=1024
```

`CMakeLists.txt`에는 `target_include_directories(app PRIVATE ${ZEPHYR_BASE}/include/zephyr/portability)` 한 줄이 더 필요하다. 이것이 없으면 `fatal error: cmsis_os2.h: No such file or directory`다(실제로 처음에 이 에러를 만났다. Zephyr 샘플 `samples/subsys/portability/cmsis_rtos_v2/philosophers`도 같은 줄을 쓴다).

처음 빌드는 마지막 두 줄(`MSGQ_MAX_DYNAMIC_SIZE`, `HEAP_MEM_POOL_SIZE`) 없이 했다. 결과는 부팅 직후의 assert였다(출력 일부).

```text
*** Booting Zephyr OS build v4.4.2 ***
...
ASSERTION FAIL [(msg_count * msg_size) <= 0] @ WEST_TOPDIR/zephyr/subsys/portability/cmsis_rtos_v2/msgq.c:57
	message queue size exceeds dynamic maximum
```

`osMessageQueueNew(4, 8, NULL)`처럼 버퍼를 안 주면 계층이 힙에서 할당하는데, 그 한도 `CONFIG_CMSIS_V2_MSGQ_MAX_DYNAMIC_SIZE`의 **기본값이 0**이다. 스레드도 마찬가지로 `CONFIG_CMSIS_V2_THREAD_DYNAMIC_MAX_COUNT` 기본값이 0이라서 스택 없이 `osThreadNew()`를 부르면 실패한다. RTX5나 CMSIS-FreeRTOS에서 그냥 돌던 코드가 Zephyr에서는 Kconfig를 맞춰야 돈다.

고친 뒤의 실행:

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex6 /var/tmp/rtos/A5/ex6 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex6/zephyr/zephyr.elf 4'
```

```text
           FLASH:       35160 B         4 MB      0.84%
             RAM:       15496 B         4 MB      0.37%
*** Booting Zephyr OS build v4.4.2 ***
CMSIS-RTOS2 on Zephyr V 4. 4. 2 api=0x4040200 kernel=0x4040200 tickfreq=100
[30 ms] cons seq=1 lat=38 us
[50 ms] cons seq=2 lat=28 us
[70 ms] cons seq=3 lat=28 us
[90 ms] cons seq=4 lat=38 us
[110 ms] cons seq=5 lat=28 us
DONE shared=10 timer_fires=3
qemu-system-arm: terminating on signal 15 from pid 859908 (timeout)
```

그리고 표준대로 `osKernelInitialize()`를 넣으면:

```sh
limactl shell elinux -- bash -lc '... west build -b mps2/an385 /Users/donh/.rtos_scratch/A5/ex6 -d /var/tmp/rtos/A5/ex6b \
  -- -DEXTRA_CFLAGS=-DUSE_KERNEL_START 2>&1 | grep -E "undefined|error"'
```

```text
/Users/donh/.rtos_scratch/A5/ex6/src/main.c:42:(.text.main+0x4): undefined reference to `osKernelInitialize'
collect2: error: ld returned 1 exit status
```

출력에서 볼 것:

- `api=0x4040200` — `osKernelGetInfo()`가 CMSIS API 버전이 아니라 **Zephyr 커널 버전**(4.4.2)을 api·kernel 양쪽에 넣는다(`kernel.c`의 `version->api = ver; version->kernel = ver;`).
- `tickfreq=100` — `osDelay(n)`의 n은 10 ms 단위다.
- 첫 메시지가 30 ms, 이후 20 ms 간격 — 예제 5(POSIX on Zephyr)와 똑같은 패턴이다. `osDelayUntil()`의 구현을 보면 이유가 보인다.

```c
osStatus_t osDelayUntil(uint32_t ticks)
{
	uint32_t ticks_elapsed;

	if (k_is_in_isr()) {
		return osErrorISR;
	}

	ticks_elapsed = osKernelGetTickCount();
	k_sleep(K_TICKS(ticks - ticks_elapsed));

	return osOK;
}
```

절대 시각을 받아서 **상대 sleep으로 바꾼다**. 상대 타임아웃이니 +1 tick이 붙고, 현재 tick을 읽은 직후 선점되면 그만큼 더 밀린다. 진짜 절대 대기(`K_TIMEOUT_ABS_TICKS`)로 구현할 수 있는데도 그렇게 하지 않았다. "표준 API를 썼으니 의미도 표준대로"라고 믿으면 안 되는 이유다.
- `lat=38 us`가 30 ms와 90 ms에서 나온다. 30 ms 주기 타이머 콜백(ISR)과 같은 tick에 겹친 순간이다. CMSIS 표준은 콜백 문맥을 구현에 맡겼고, 이 구현은 `k_timer` 위라서 ISR이다(`timer.c`의 `zephyr_timer_wrapper`).
- `osKernelInitialize` / `osKernelStart`는 `cmsis_os2.h`에 **선언만 있고 구현이 없다**(`subsys/portability/cmsis_rtos_v2/` 전체를 grep해도 정의가 없다). Zephyr는 `main()` 전에 커널이 이미 돌기 때문이다. 표준 CMSIS 예제 코드를 그대로 가져오면 링크 에러가 난다. 해결은 두 줄을 지우거나, 앱에서 `osOK`를 돌려주는 빈 함수를 정의하는 것이다.

### 5.3 CMSIS-on-Zephyr에서 발견한 의미 차이 모음

| 항목 | CMSIS-RTOS2 표준 | Zephyr v4.4.2 계층 | 근거 |
|---|---|---|---|
| 커널 시작 | `osKernelInitialize()` / `osKernelStart()` | 정의 없음 → 링크 에러 | 예제 13 |
| 우선순위 | 56단계 enum | `CONFIG_NUM_PREEMPT_PRIORITIES=56` 필요, `osPriorityISR - p`로 변환 | `thread.c`, Kconfig |
| 타이머 콜백 | timer thread 또는 ISR | ISR | `timer.c` |
| `osDelayUntil` | 절대 tick까지 대기 | 상대 sleep으로 변환 (+1 tick) | `kernel.c`, 예제 13 출력 |
| `osMutexRecursive` 비트 | 없으면 재귀 금지 | 무시, 항상 재귀 (`k_mutex`) | `mutex.c` |
| `osMutexPrioInherit` 비트 | 선택 | 빼면 assert ("Zephyr supports osMutexPrioInherit by default. Do not unselect it") | `mutex.c` |
| `msg_prio` | 높은 우선순위 메시지를 앞에 | `ARG_UNUSED(msg_prio)` — 무시 | `msgq.c` |
| 동적 객체 | 구현 몫 | 스레드 0개, 큐 0바이트가 기본 한도 | Kconfig, 예제 13 assert |
| `osKernelGetInfo` | API 버전과 커널 버전 | 둘 다 Zephyr 버전 | 예제 13 출력 |

이 표가 "OSAL은 이름을 통일하지만 의미는 통일하지 못한다"의 가장 구체적인 증거다. 반대로 말하면, 표준 OSAL을 쓰더라도 **구현 소스를 한 번은 읽어야** 한다.

---

## 6. 측정 — 같은 앱의 크기, 같은 큐의 왕복 시간

### 6.1 예제 14 — 코드 크기 (조건을 맞춰서)

§2.7의 크기는 최적화(-O2/-Os)와 assert 설정이 섞여 있었다. 이번에는 **모두 -Os, Zephyr assert 끔**으로 맞추고 같은 앱(2.1 사양)을 일곱 가지로 빌드한다.

```sh
# size_all.sh의 핵심 줄 (S = 소스 폴더, O = 산출물 폴더)
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex1/main.c OUT=$O/frt EXTRA_CFLAGS=-Os
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/osal/app.c OUT=$O/osal_f EXTRA_CFLAGS="-Os -DOS_FREERTOS"
(cd $O && $S/ex5/build.sh $S/ex5/main.c $O/tx -Os)
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/ex2 $O/zep -- -DCONFIG_ASSERT=n
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/osal/app_z $O/osal_z
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/ex6 $O/cmsis -- -DCONFIG_ASSERT=n
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/ex4 $O/zposix
cd $O && arm-none-eabi-size frt/app.elf osal_f/app.elf tx/app.elf zep/zephyr/zephyr.elf \
  osal_z/zephyr/zephyr.elf cmsis/zephyr/zephyr.elf zposix/zephyr/zephyr.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
  13080	    100	 135412	 148592	  24470	frt/app.elf
  13144	    100	 135452	 148696	  244d8	osal_f/app.elf
  10804	    100	   4540	  15444	   3c54	tx/app.elf
  16088	    284	   8287	  24659	   6053	zep/zephyr/zephyr.elf
  16104	    188	  12971	  29263	   724f	osal_z/zephyr/zephyr.elf
  20276	    272	  15151	  35699	   8b73	cmsis/zephyr/zephyr.elf
  29684	    428	  36528	  66640	  10450	zposix/zephyr/zephyr.elf
```

```svg
<svg viewBox="0 0 660 338" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13" text-anchor="start" font-weight="bold">같은 앱의 코드 크기 (.text, 바이트, 모두 -Os, assert 끔)</text>
<text x="20" y="57" font-size="12" text-anchor="start">ThreadX 6.5.2</text>
<rect x="230" y="44" width="124" height="20" rx="2" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="360" y="58" font-size="12" text-anchor="start">10,804 B</text>
<text x="20" y="91" font-size="12" text-anchor="start">FreeRTOS 11.3.2</text>
<rect x="230" y="78" width="150" height="20" rx="2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="386" y="92" font-size="12" text-anchor="start">13,080 B</text>
<text x="20" y="125" font-size="12" text-anchor="start">OSAL → FreeRTOS</text>
<rect x="230" y="112" width="151" height="20" rx="2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="387" y="126" font-size="12" text-anchor="start">13,144 B</text>
<text x="20" y="159" font-size="12" text-anchor="start">Zephyr 4.4.2 네이티브</text>
<rect x="230" y="146" width="184" height="20" rx="2" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1.3"/>
<text x="420" y="160" font-size="12" text-anchor="start">16,088 B</text>
<text x="20" y="193" font-size="12" text-anchor="start">OSAL → Zephyr</text>
<rect x="230" y="180" width="184" height="20" rx="2" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1.3"/>
<text x="420" y="194" font-size="12" text-anchor="start">16,104 B</text>
<text x="20" y="227" font-size="12" text-anchor="start">CMSIS-RTOS2 → Zephyr</text>
<rect x="230" y="214" width="232" height="20" rx="2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a" stroke-width="1.3"/>
<text x="468" y="228" font-size="12" text-anchor="start">20,276 B</text>
<text x="20" y="261" font-size="12" text-anchor="start">POSIX → Zephyr</text>
<rect x="230" y="248" width="340" height="20" rx="2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a" stroke-width="1.3"/>
<text x="576" y="262" font-size="12" text-anchor="start">29,684 B</text>
<line x1="230" y1="36" x2="230" y2="280" stroke="currentColor" stroke-width="1.3"/>
<text x="20" y="300" font-size="12" text-anchor="start">FreeRTOS · ThreadX는 lab board.c(UART printf) 포함, Zephyr는 printk · UART 드라이버 포함</text>
</svg>
```

그림 7 — 같은 앱의 `.text` 크기(실측, `arm-none-eabi-size`). 같은 커널 위의 OSAL은 거의 공짜(+64 B, +16 B)이고, 함수 계층인 CMSIS-RTOS2(+4.2 KB)와 POSIX(+13.6 KB)는 비용이 크다.

가장 큰 함수 몇 개를 보면 무엇이 크기를 만드는지 보인다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/A5/size; for e in frt/app.elf zep/zephyr/zephyr.elf tx/app.elf; do \
  echo "== $e"; arm-none-eabi-nm --size-sort -S -t d $e | grep -i " [tT] " | tail -8; done'
```

```text
== frt/app.elf
00006292 00000304 T xQueueReceive
00006596 00000348 T xQueueSemaphoreTake
00008124 00000352 T pvPortMalloc
00005904 00000352 T xQueueGenericSend
00003152 00000360 T xTaskResumeAll
00010324 00000364 T setvbuf
00007444 00000408 t prvTimerTask
00002744 00000408 T xTaskIncrementTick
== zep/zephyr/zephyr.elf
00008832 00000284 t skip_to_arg
00007792 00000288 T z_add_timeout
00007128 00000316 T z_thread_abort
00003364 00000388 T z_arm_mpu_init
00002144 00000424 T z_arm_fault
00000240 00000754 T __udivmoddi4
00009116 00001332 T __l_vfprintf
00009116 00001332 T vfprintf
```

(ThreadX 목록은 `_tx_queue_receive` 460 B, `_tx_mutex_put` 456 B, `_tx_thread_system_suspend` 368 B 순이다.)

해석:

- **크기 순서(이 앱, 이 설정)**: ThreadX 10.8 KB < FreeRTOS 13.1 KB < Zephyr 16.1 KB. 셋 다 "커널 + 앱 + 콘솔 출력"이다.
- Zephyr의 상위 항목은 커널이 아니라 **주변 기능**이다: `vfprintf`(printk/picolibc 포맷터, 1.3 KB), 64비트 나눗셈 `__udivmoddi4`(시간 단위 변환이 64비트로 계산되기 때문), MPU 초기화, 폴트 해석기 `z_arm_fault`. 즉 "Zephyr 커널이 크다"가 아니라 "Zephyr는 기본으로 켜 두는 것이 많다"이다. 무엇을 끌 수 있는지는 H2(Kconfig)에서.
- FreeRTOS의 `prvTimerTask`(408 B)와 `pvPortMalloc`(352 B)은 타이머 daemon과 heap_4다. ThreadX는 힙을 쓰지 않는다.
- RAM(`bss`)은 비교하지 않는다. FreeRTOS의 135 KB는 랩이 정한 힙 크기이고, ThreadX는 앱 배열만큼이다. RAM 비교를 하려면 힙·스택 크기를 같게 맞춰야 한다.

### 6.2 예제 15 — 큐 왕복 시간 (ping-pong)

무엇을 확인하는 코드인지: 큐 두 개로 ping → pong → ping을 1000번 왕복시켜 1회 왕복 시간을 잰다. pong이 더 높은 우선순위라 send 한 번마다 즉시 context switch가 일어난다. 즉 1 왕복 = send 2번 + receive 2번 + context switch 2번이다. FreeRTOS 버전과 Zephyr 버전을 보인다(ThreadX·CMSIS·POSIX·OSAL 버전은 같은 구조로 이름만 바꿨다).

```c
/* A5 bench — FreeRTOS: 큐 ping-pong 왕복 시간 (pong이 더 높은 우선순위) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define N 1000
static QueueHandle_t q1, q2;
static void pong(void *a) { (void)a; uint32_t v;
    for (;;) { xQueueReceive(q1, &v, portMAX_DELAY); xQueueSend(q2, &v, portMAX_DELAY); } }
static void ping(void *a) {
    (void)a; uint32_t v = 0, t0 = board_now_us();
    for (int i = 0; i < N; i++) { xQueueSend(q1, &v, portMAX_DELAY); xQueueReceive(q2, &v, portMAX_DELAY); }
    uint32_t dt = board_now_us() - t0;
    board_printf_raw("FreeRTOS queue round-trip: %u ns (N=%u)\n", (unsigned)(dt * 1000u / N), N);
    board_exit(0);
}
int main(void) {
    q1 = xQueueCreate(1, 4); q2 = xQueueCreate(1, 4);
    xTaskCreate(ping, "ping", 256, NULL, 2, NULL);
    xTaskCreate(pong, "pong", 256, NULL, 3, NULL);
    vTaskStartScheduler(); for (;;) { }
}
```

```c
/* A5 bench — Zephyr native k_msgq ping-pong */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "../../../exit.h"
#define N 1000
K_MSGQ_DEFINE(q1, 4, 1, 4);
K_MSGQ_DEFINE(q2, 4, 1, 4);
static void pong(void *a, void *b, void *c) { uint32_t v;
	for (;;) { k_msgq_get(&q1, &v, K_FOREVER); k_msgq_put(&q2, &v, K_FOREVER); } }
K_THREAD_DEFINE(pong_id, 1024, pong, NULL, NULL, NULL, 4, 0, 0);   /* main(5)보다 높음 */
int main(void) {
	k_thread_priority_set(k_current_get(), 5);
	uint32_t v = 0, t0 = k_cycle_get_32();
	for (int i = 0; i < N; i++) { k_msgq_put(&q1, &v, K_FOREVER); k_msgq_get(&q2, &v, K_FOREVER); }
	uint32_t ns = (uint32_t)(k_cyc_to_ns_floor64(k_cycle_get_32() - t0) / N);
	printk("Zephyr k_msgq round-trip: %u ns (N=%u)\n", ns, N);
	qemu_exit(0);
}
```

`exit.h`는 semihosting `SYS_EXIT`(`bkpt 0xab`)로 QEMU를 끝내는 6줄짜리 함수다. Zephyr 벤치는 `CONFIG_SPEED_OPTIMIZATIONS=y`(-O2)로 빌드해 FreeRTOS·ThreadX(-O2)와 맞췄다.

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/A5/bench/run_all.sh'
```

```text
FreeRTOS queue round-trip: 35161 ns (N=1000)
OSAL/FreeRTOS queue round-trip: 35160 ns (N=1000)
ThreadX queue round-trip: 13739 ns (N=1000)
Zephyr k_msgq round-trip: 35321 ns (N=1000)
OSAL/Zephyr queue round-trip: 35322 ns (N=1000)
CMSIS-RTOS2/Zephyr queue round-trip: 38881 ns (N=1000)
POSIX mq round-trip: 50730 ns (N=1000)
```

FreeRTOS 랩 설정은 학습용이라 기능을 많이 켜 둔다(스택 오버플로 검사 방식 2, run-time stats — context switch마다 `board_now_us()` 호출, trace facility, queue set, `configASSERT`). 이것들을 끈 "lean" 설정(앱 폴더에 템플릿 `FreeRTOSConfig.h`를 복사해 두고 아래 네 줄과 `configUSE_STATS_FORMATTING_FUNCTIONS 0`을 바꿈)으로도 재 봤다.

```text
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                0
#define configGENERATE_RUN_TIME_STATS           0
#define configASSERT( x ) ( ( void ) ( x ) )   /* lean: 검사 안 함 */
```

```text
FreeRTOS(lean) queue round-trip: 25897 ns (N=1000)
[board_exit] code=0
```

(`configASSERT`를 아예 정의하지 않으면 `port.c`에서 `unused variable 'pxVectorTable'` 경고가 나와서 `((void)(x))`로 정의했다.)

**예제 16 — 숫자를 명령 수로 바꾸기** (icount: 1명령 = 32 ns)

```python
# 왕복 시간(ns) → 명령 수(icount: 1명령 = 32 ns), FreeRTOS 대비 비율
rt = {"ThreadX": 13739, "FreeRTOS lean": 25897, "FreeRTOS lab": 35161, "OSAL->FreeRTOS": 35160,
      "Zephyr k_msgq": 35321, "OSAL->Zephyr": 35322, "CMSIS->Zephyr": 38881, "POSIX mq->Zephyr": 50730}
base = rt["FreeRTOS lab"]
for k, v in rt.items():
    print(f"{k:18s} {v:6d} ns  ~{v / 32:5.0f} instr/round-trip  x{v / base:4.2f}")
print(f"OSAL overhead (FreeRTOS): {rt['OSAL->FreeRTOS'] - rt['FreeRTOS lab']:+d} ns")
print(f"OSAL overhead (Zephyr):   {rt['OSAL->Zephyr'] - rt['Zephyr k_msgq']:+d} ns")
print(f"CMSIS layer over Zephyr:  {rt['CMSIS->Zephyr'] - rt['Zephyr k_msgq']:+d} ns "
      f"(~{(rt['CMSIS->Zephyr'] - rt['Zephyr k_msgq']) / 32 / 4:.0f} instr per API call)")
```

```text
ThreadX             13739 ns  ~  429 instr/round-trip  x0.39
FreeRTOS lean       25897 ns  ~  809 instr/round-trip  x0.74
FreeRTOS lab        35161 ns  ~ 1099 instr/round-trip  x1.00
OSAL->FreeRTOS      35160 ns  ~ 1099 instr/round-trip  x1.00
Zephyr k_msgq       35321 ns  ~ 1104 instr/round-trip  x1.00
OSAL->Zephyr        35322 ns  ~ 1104 instr/round-trip  x1.00
CMSIS->Zephyr       38881 ns  ~ 1215 instr/round-trip  x1.11
POSIX mq->Zephyr    50730 ns  ~ 1585 instr/round-trip  x1.44
OSAL overhead (FreeRTOS): -1 ns
OSAL overhead (Zephyr):   +1 ns
CMSIS layer over Zephyr:  +3560 ns (~28 instr per API call)
```

```svg
<svg viewBox="0 0 660 372" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13" text-anchor="start" font-weight="bold">큐 ping-pong 왕복 시간 (ns, N=1000, QEMU icount 1명령=32 ns)</text>
<text x="20" y="57" font-size="12" text-anchor="start">ThreadX 6.5.2</text>
<rect x="230" y="44" width="92" height="20" rx="2" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="328" y="58" font-size="12" text-anchor="start">13,739 ns</text>
<text x="20" y="91" font-size="12" text-anchor="start">FreeRTOS lean config</text>
<rect x="230" y="78" width="174" height="20" rx="2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="410" y="92" font-size="12" text-anchor="start">25,897 ns</text>
<text x="20" y="125" font-size="12" text-anchor="start">FreeRTOS lab config</text>
<rect x="230" y="112" width="236" height="20" rx="2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="472" y="126" font-size="12" text-anchor="start">35,161 ns</text>
<text x="20" y="159" font-size="12" text-anchor="start">OSAL → FreeRTOS</text>
<rect x="230" y="146" width="236" height="20" rx="2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="472" y="160" font-size="12" text-anchor="start">35,160 ns</text>
<text x="20" y="193" font-size="12" text-anchor="start">Zephyr k_msgq</text>
<rect x="230" y="180" width="237" height="20" rx="2" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1.3"/>
<text x="473" y="194" font-size="12" text-anchor="start">35,321 ns</text>
<text x="20" y="227" font-size="12" text-anchor="start">OSAL → Zephyr</text>
<rect x="230" y="214" width="237" height="20" rx="2" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1.3"/>
<text x="473" y="228" font-size="12" text-anchor="start">35,322 ns</text>
<text x="20" y="261" font-size="12" text-anchor="start">CMSIS-RTOS2 → Zephyr</text>
<rect x="230" y="248" width="261" height="20" rx="2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a" stroke-width="1.3"/>
<text x="497" y="262" font-size="12" text-anchor="start">38,881 ns</text>
<text x="20" y="295" font-size="12" text-anchor="start">POSIX mq → Zephyr</text>
<rect x="230" y="282" width="340" height="20" rx="2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a" stroke-width="1.3"/>
<text x="576" y="296" font-size="12" text-anchor="start">50,730 ns</text>
<line x1="230" y1="36" x2="230" y2="314" stroke="currentColor" stroke-width="1.3"/>
<text x="20" y="334" font-size="12" text-anchor="start">왕복 = send 2번 + receive 2번 + context switch 2번. 실칩 절대값과 다르다</text>
</svg>
```

그림 8 — 큐 ping-pong 1회 왕복 시간(실측, QEMU icount). 색은 아래 커널(파랑 ThreadX, 초록 FreeRTOS, 주황 Zephyr, 빨강 Zephyr 위의 표준 계층).

해석:

- **`static inline` OSAL은 공짜다.** FreeRTOS·Zephyr 모두 ±1 ns. 컴파일러가 `os_queue_send()`를 `xQueueSend()`·`k_msgq_put()`으로 그대로 펼친다. "OSAL은 느리다"는 말은 구현 방식에 따라 틀린 말이다.
- **CMSIS-RTOS2 계층은 API 호출당 약 28명령**을 더한다. 왕복에 API 4번이니 +3.6 µs. 소스를 보면 `k_is_in_isr()` 검사, `osWaitForever`/0/그 외 타임아웃 분기, Zephyr 반환값 → `osStatus_t` 변환이다.
- **POSIX mq는 +44%**. Zephyr의 `mq_send()`는 결국 `k_msgq_put()`이지만(`lib/posix/options/mqueue.c`의 `send_message()`), 그 앞에 디스크립터 검사, `O_NONBLOCK` 처리, 메시지 크기 검사, 사용량 조회(`k_msgq_num_used_get()` 두 번)가 붙는다.
- **ThreadX가 가장 짧고, FreeRTOS는 설정에 따라 26% 달라진다.** ThreadX는 오류 검사 계층(`_txe_`)이 켜진 상태인데도 429명령이다. 하지만 이 숫자로 "ThreadX가 FreeRTOS보다 2.5배 빠르다"라고 말하면 안 된다. FreeRTOS lean 설정에도 queue set 지원(`configUSE_QUEUE_SETS=1`, `xQueueGenericSend`의 추가 분기) 같은 기능이 남아 있고, Zephyr도 기본 Kconfig 그대로다. 공정한 순위를 내려면 기능 세트를 맞추고 실제 칩에서 사이클을 재야 한다(M4). 이 측정이 말해 주는 것은 "**같은 커널 안에서 설정 차이가 커널 사이 차이만큼 크다**"는 것이다.
- 참고로 같은 POSIX 벤치를 Linux VM에서 돌리면 세 번에 20,918 / 24,326 / 27,329 ns였다. 호스트 CPU는 수 GHz이므로 절대값을 QEMU 숫자와 비교하면 안 되고, **매번 다르다**는 점만 본다.

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나 — "세마포어 주기"를 세 커널에서 따라가기

같은 `give`가 세 커널 안에서 어떤 길을 가는지 소스로 따라간다. 깊은 설명(리스트 구조, PendSV, 재스케줄 판단)은 C1(자료구조)·C2(컨텍스트 스위치)·C7(크리티컬 섹션)·D1(세마포어)로 미룬다.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="12" width="210" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="33" font-size="12" text-anchor="middle" font-weight="bold">FreeRTOS xSemaphoreGive</text>
<rect x="15" y="62" width="210" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="85" font-size="11" text-anchor="middle">매크로 → xQueueGenericSend</text>
<line x1="120" y1="48" x2="120" y2="61" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="15" y="114" width="210" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="137" font-size="11" text-anchor="middle">taskENTER_CRITICAL (BASEPRI)</text>
<line x1="120" y1="100" x2="120" y2="113" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="15" y="166" width="210" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="189" font-size="11" text-anchor="middle">xTasksWaitingToReceive 검사</text>
<line x1="120" y1="152" x2="120" y2="165" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="15" y="218" width="210" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="241" font-size="11" text-anchor="middle">xTaskRemoveFromEventList</text>
<line x1="120" y1="204" x2="120" y2="217" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="15" y="270" width="210" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="120" y="293" font-size="11" text-anchor="middle">더 높으면 queueYIELD → PendSV</text>
<line x1="120" y1="256" x2="120" y2="269" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="235" y="12" width="210" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="33" font-size="12" text-anchor="middle" font-weight="bold">Zephyr k_sem_give</text>
<rect x="235" y="62" width="210" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="85" font-size="11" text-anchor="middle">z_impl_k_sem_give 진입</text>
<line x1="340" y1="48" x2="340" y2="61" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="235" y="114" width="210" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="137" font-size="11" text-anchor="middle">k_spin_lock (UP: irq_lock)</text>
<line x1="340" y1="100" x2="340" y2="113" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="235" y="166" width="210" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="189" font-size="11" text-anchor="middle">z_unpend_first_thread(wait_q)</text>
<line x1="340" y1="152" x2="340" y2="165" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="235" y="218" width="210" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="241" font-size="11" text-anchor="middle">있으면 z_ready_thread</text>
<line x1="340" y1="204" x2="340" y2="217" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="235" y="270" width="210" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340" y="293" font-size="11" text-anchor="middle">없으면 count++ (limit에서 멈춤)</text>
<line x1="340" y1="256" x2="340" y2="269" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="455" y="12" width="210" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="33" font-size="12" text-anchor="middle" font-weight="bold">ThreadX tx_semaphore_put</text>
<rect x="455" y="62" width="210" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="85" font-size="11" text-anchor="middle">_tx_semaphore_put 진입</text>
<line x1="560" y1="48" x2="560" y2="61" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="455" y="114" width="210" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="137" font-size="11" text-anchor="middle">TX_DISABLE (PRIMASK)</text>
<line x1="560" y1="100" x2="560" y2="113" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="455" y="166" width="210" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="189" font-size="11" text-anchor="middle">suspended_count == 0 ?</text>
<line x1="560" y1="152" x2="560" y2="165" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="455" y="218" width="210" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="241" font-size="11" text-anchor="middle">0이면 count++ (상한 없음)</text>
<line x1="560" y1="204" x2="560" y2="217" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<rect x="455" y="270" width="210" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="560" y="293" font-size="11" text-anchor="middle">_tx_thread_system_resume</text>
<line x1="560" y1="256" x2="560" y2="269" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<text x="20" y="336" font-size="12" text-anchor="start">셋 다 '잠그고 → 대기 리스트 맨 앞을 깨우거나 count를 올리고 → 필요하면 스위치'로 같다</text>
</svg>
```

그림 9 — 세 커널의 "세마포어 주기" 경로. 모양은 같다: 잠그고 → 기다리는 스레드가 있으면 깨우고(없으면 count 증가) → 깨운 스레드가 더 급하면 스위치.

**FreeRTOS** — 세마포어는 "항목 크기 0인 큐"다. `include/semphr.h`:

```c
#define xSemaphoreGive( xSemaphore )    xQueueGenericSend( ( QueueHandle_t ) ( xSemaphore ), NULL, semGIVE_BLOCK_TIME, queueSEND_TO_BACK )
```

`queue.c`의 `xQueueGenericSend()` 핵심(queue set 분기를 빼고 발췌):

```c
    for( ; ; )
    {
        taskENTER_CRITICAL();
        {
            if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
            {
                xYieldRequired = prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );
                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                ...
```

`queueYIELD_IF_USING_PREEMPTION()` → `portYIELD_WITHIN_API()` → `portYIELD()` — Cortex-M3 포트에서 ICSR에 PENDSVSET을 쓰는 매크로다(`portmacro.h`). 세마포어가 "항목 크기 0인 큐"이기 때문에 세마포어와 큐가 같은 코드 경로를 쓰고, 그래서 FreeRTOS 세마포어는 비교적 무겁다. 가벼운 대안이 task notification이다(D5).

**Zephyr** — `k_sem_give()`는 syscall 선언(`__syscall void k_sem_give(struct k_sem *sem);`)이고, 유저 모드가 꺼져 있으면 바로 `kernel/sem.c`의 구현으로 간다.

```c
void z_impl_k_sem_give(struct k_sem *sem)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct k_thread *thread;
	bool resched;

	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_sem, give, sem);

	thread = z_unpend_first_thread(&sem->wait_q);

	if (unlikely(thread != NULL)) {
		arch_thread_return_value_set(thread, 0);
		z_ready_thread(thread);
		resched = true;
	} else {
		sem->count += (sem->count != sem->limit) ? 1U : 0U;
		resched = handle_poll_events(sem);
	}

	if (unlikely(resched)) {
		z_reschedule(&lock, key);
	} else {
		k_spin_unlock(&lock, key);
	}
	...
```

눈여겨볼 점: 기다리는 스레드가 있으면 count를 올리지 않고 **바로 그 스레드에게 "받았음(0)"을 반환값으로 심어** 준다(`arch_thread_return_value_set`). 깨어난 스레드는 다시 경쟁하지 않는다. `z_unpend_first_thread()`는 `kernel/include/ksched.h`의 인라인 함수, `z_ready_thread()`·`z_reschedule()`은 `kernel/sched.c`에 있다. 단일 코어에서 `k_spin_lock()`은 사실상 `irq_lock()`(BASEPRI)이다.

**ThreadX** — `tx_semaphore_put()`은 오류 검사 `_txe_semaphore_put()`을 거쳐 `common/src/tx_semaphore_put.c`의 `_tx_semaphore_put()`으로 간다(주석·트레이스 매크로를 빼고 발췌).

```c
    suspended_count =  semaphore_ptr -> tx_semaphore_suspended_count;
    if (suspended_count == TX_NO_SUSPENSIONS)
    {
        semaphore_ptr -> tx_semaphore_count++;
        TX_RESTORE
    }
    else
    {
        thread_ptr =  semaphore_ptr -> tx_semaphore_suspension_list;
        ...                                   /* 리스트에서 thread_ptr를 뺀다 */
        thread_ptr -> tx_thread_suspend_status =  TX_SUCCESS;
        _tx_thread_preempt_disable++;
        TX_RESTORE
        _tx_thread_system_resume(thread_ptr);
    }
    return(TX_SUCCESS);
```

`TX_DISABLE`/`TX_RESTORE`는 Cortex-M3 포트 기본으로 PRIMASK를 쓴다(모든 IRQ를 막는다). 그리고 깨우기(`_tx_thread_system_resume`)는 인터럽트를 다시 켠 **뒤에**, 선점만 막은 상태(`_tx_thread_preempt_disable++`)에서 한다. 인터럽트를 막는 구간을 짧게 하려는 설계다.

### 7.1 예제 17 — 커널 안에서 찾은 의미 차이: 누구를 먼저 깨우나

세 소스를 나란히 보면 하나가 눈에 띈다. FreeRTOS는 대기 리스트에 넣을 때 `vListInsert()`로 **우선순위 순서**로 넣고(`tasks.c`의 `vTaskPlaceOnEventList()` 주석: "the event list is sorted in descending priority order"), Zephyr도 `add_to_waitq_locked()` → `_priq_wait_add()`로 우선순위 순서다. 그런데 ThreadX `tx_semaphore_get.c`는 주석 그대로 "add current thread to the end" — **FIFO**다. 정말 그런지 실험했다.

무엇을 확인하는 코드인지: 낮은 우선순위 스레드가 먼저 세마포어를 기다리고, 높은 스레드가 나중에 기다린다. give/put을 한 번만 하면 누가 깨나?

```c
/* A5 ex14a — ThreadX: 세마포어를 기다리는 두 스레드, put 한 번이면 누가 깨나? */
#include "tx_api.h"
#include "board.h"
static TX_THREAD lo, hi, ctl; static ULONG s1[256], s2[256], s3[256];
static TX_SEMAPHORE s;
static void waiter(ULONG prio) {
    tx_semaphore_get(&s, TX_WAIT_FOREVER);
    board_printf_raw("woke: prio %u\n", (unsigned)prio);
}
static void control(ULONG in) {
    (void)in;
    tx_thread_resume(&lo); tx_thread_sleep(1);      /* 낮은 쪽(20)이 먼저 줄 선다 */
    tx_thread_resume(&hi); tx_thread_sleep(1);      /* 높은 쪽(10)이 나중에 */
#ifdef PRIORITIZE
    tx_semaphore_prioritize(&s);                    /* 대기 줄을 우선순위 순으로 */
#endif
    tx_semaphore_put(&s); tx_thread_sleep(1);
    board_exit(0);
}
void tx_application_define(void *f) { (void)f;
    tx_semaphore_create(&s, "s", 0);
    tx_thread_create(&lo, "lo", waiter, 20, s1, sizeof s1, 20, 20, TX_NO_TIME_SLICE, TX_DONT_START);
    tx_thread_create(&hi, "hi", waiter, 10, s2, sizeof s2, 10, 10, TX_NO_TIME_SLICE, TX_DONT_START);
    tx_thread_create(&ctl, "ctl", control, 0, s3, sizeof s3, 5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) { tx_kernel_enter(); return 0; }
```

FreeRTOS(`frt.c`)와 Zephyr(`ex14z`) 버전도 같은 순서로 "낮은 쪽 먼저 줄 서기 → 높은 쪽 줄 서기 → 한 번 주기"를 한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/A5 && for v in "" "-DPRIORITIZE"; do echo "== ThreadX $v"; \
  /Users/donh/.rtos_scratch/A5/ex5/build.sh /Users/donh/.rtos_scratch/A5/ex14/tx.c /var/tmp/rtos/A5/ex14tx$v $v >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex14tx$v/app.elf 5; done; echo "== FreeRTOS"; \
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A5/ex14/frt.c OUT=/var/tmp/rtos/A5/ex14f >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex14f/app.elf 5'
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A5/ex14z /var/tmp/rtos/A5/ex14z >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A5/ex14z/zephyr/zephyr.elf 5'
```

```text
== ThreadX 
woke: prio 20
[board_exit] code=0
== ThreadX -DPRIORITIZE
woke: prio 10
[board_exit] code=0
== FreeRTOS
woke: prio 4
[board_exit] code=0
```

```text
*** Booting Zephyr OS build v4.4.2 ***
woke: prio 4
```

출력에서 볼 것: ThreadX는 **먼저 줄 선 낮은 우선순위(20)** 를 깨웠다. `tx_semaphore_prioritize()`를 부른 뒤에야 높은 쪽(10)이 깼다. FreeRTOS(4가 높음)와 Zephyr(4가 높음)는 높은 쪽을 깨웠다. ThreadX는 세마포어·큐·뮤텍스·블록 풀·바이트 풀 모두에 `*_prioritize()` 함수가 따로 있다(`common/src`에 10개 파일). 기본이 FIFO이기 때문이다.

이것은 로제타석 표의 어느 칸에도 안 보이는 차이다. FreeRTOS 코드를 ThreadX로 옮기면 "높은 우선순위 태스크가 세마포어를 먼저 받는다"는 암묵적 가정이 깨진다. 이 차이가 실시간 분석에 주는 영향(블로킹 시간 상한)은 N 모듈, 대기 큐 자료구조는 C1에서 다시 본다.

---

## 8. 실무에서 다시 보기

### 8.1 FreeRTOS 드론 비행 제어기를 Zephyr로 옮길 때 — 순서 있는 체크리스트

예를 들어 드론 비행 제어기의 IMU 경로가 FreeRTOS로 이렇게 되어 있다고 하자: IMU data-ready ISR → 큐 → 1 kHz 자세 추정 태스크 → 뮤텍스로 보호된 상태 블록 → 100 Hz 텔레메트리 타이머. 이 노트의 실험 순서대로 점검하면 된다.

1. **우선순위 표를 먼저 만든다.** FreeRTOS 숫자 → Zephyr 숫자 매핑을 한 헤더에 두고 숫자를 직접 쓰지 않는다. 음수(협력형)는 "절대 선점되면 안 되는 짧은 일"에만 쓴다(§3.1).
2. **스택 숫자를 4배로.** `xTaskCreate(…, 512, …)`(2 KB)는 `K_THREAD_STACK_DEFINE(…, 2048)`. 옮긴 뒤 `k_thread_stack_space_get()`으로 여유를 다시 잰다(§3.6).
3. **tick 주파수를 확인한다.** `CONFIG_SYS_CLOCK_TICKS_PER_SEC`가 보드마다 다르다(QEMU 100 Hz, tickless 실보드 기본 10 kHz). `vTaskDelay(pdMS_TO_TICKS(x))` → `k_msleep(x)`로 바꾸면 +1 tick이 붙는다. 1 kHz 루프는 반드시 `K_TIMEOUT_ABS_*` 또는 `k_timer` 기반 절대 주기로(§3.2, §4.4).
4. **ISR을 정리한다.** `xQueueSendFromISR` + `portYIELD_FROM_ISR` → `k_msgq_put(…, K_NO_WAIT)` 한 줄. 큐가 가득 찼을 때(`-ENOMSG`) 샘플을 버릴지 덮어쓸지 정책을 코드로 남긴다(§3.3).
5. **반환값 비교를 전부 상수로.** `if (xQueueReceive(...))` 같은 줄을 grep해서 하나씩 `== 0`으로 고친다(§3.5).
6. **타이머 콜백을 전수 조사한다.** 콜백 안에 블록 호출(뮤텍스, 큐 대기, UART 송신)이 있으면 `k_work_submit()`으로 넘긴다(§3.4, D7).
7. **task notification을 대체한다.** 대응이 없으니 용도별로 `k_sem`(카운트), `k_event`(비트), `k_poll_signal`(여러 소스 대기)을 고른다(D5).
8. **시작 코드를 지운다.** `vTaskStartScheduler()` 삭제, 객체는 `K_*_DEFINE`로 정적 정의(§3.9).
9. **뮤텍스·세마포어 의미를 재검토한다.** Zephyr 뮤텍스는 재귀가 기본, binary 세마포어 give는 가득 차면 조용히 버린다(§3.7). FreeRTOS에서 세던 "놓친 이벤트" 카운터는 다른 방법으로 세야 한다.
10. **같은 테스트로 비교한다.** 2.1 사양 같은 작은 시나리오를 양쪽에서 돌려 출력(주기, 지연, 카운터)을 diff한다.

4번을 코드로 보면 이렇다.

```c
/* FreeRTOS 원본 */
void IMU_DRDY_IRQHandler(void) {
    imu_sample_t s; imu_read_fifo(&s);
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(imu_q, &s, &woken) != pdPASS) imu_drop++;
    portYIELD_FROM_ISR(woken);
}

/* Zephyr 번역 (IRQ_CONNECT 또는 드라이버 콜백 안) */
static void imu_drdy_isr(const void *arg) {
    imu_sample_t s; imu_read_fifo(&s);
    if (k_msgq_put(&imu_q, &s, K_NO_WAIT) != 0) imu_drop++;   /* -ENOMSG = 가득 참 */
}
```

### 8.2 벤더 SDK가 ThreadX일 때

MCU 벤더 SDK에 ThreadX가 통합되어 오는 경우가 있다. FreeRTOS 경험자가 ThreadX 코드베이스에 들어가면 이 노트의 실험 중 세 가지가 바로 쓰인다.

- **대기 순서가 FIFO다**(예제 17). 우선순위 높은 스레드가 먼저 받기를 기대한다면 `tx_*_prioritize()`를 부르는 코드가 있는지 찾아본다.
- **PI가 선택이다.** `tx_mutex_create(…, TX_NO_INHERIT)`로 만든 뮤텍스가 있으면 우선순위 역전 분석 대상이다(D3).
- **timer thread에서 대기하면 `TX_WAIT_ERROR`** 가 돌아온다. FreeRTOS daemon 태스크에서 "잠깐 기다리던" 코드는 옮기면 조용히 실패한다(반환값을 안 보면).

### 8.3 센서 허브 MCU와 Linux SoC가 코드를 나눌 때

Linux SoC 옆에서 IMU 타임스탬프를 맡는 센서 허브 MCU를 생각하자. "프로토콜 파서와 필터 코드를 POSIX로 써서 Linux 데몬과 MCU(Zephyr POSIX)에서 같이 쓰자"는 제안이 자주 나온다. 컴파일은 된다(예제 5). 하지만 Zephyr의 `clock_gettime()`은 tick 해상도라서 타임스탬프 코드가 그대로 오면 **10 ms(또는 100 µs) 단위로 뭉개진다**. 공유 코드에서 시간은 `now_ns()` 같은 자체 함수로 빼고, MCU 쪽은 `k_cycle_get_32()`나 HW 타이머로 구현한다(타임스탬프 → 호스트 시계 매핑은 Q7). 즉 POSIX도 하나의 OSAL로 보고 **시간 함수만큼은 직접 감싼다**.

### 8.4 Don의 SSD 펌웨어 경험을 번역해서 말하기

SSD 컨트롤러 펌웨어의 사내 executive는 사실상 회사 전용 RTOS + OSAL이었다. 면접에서 "RTOS 경험이 FreeRTOS뿐이다"라고 줄여 말할 필요가 없다. "사내 커널의 메시지 큐·이벤트·락을 썼고, 그 개념을 FreeRTOS·Zephyr의 어떤 객체로 옮기면 되는지, 옮길 때 무엇이 달라지는지(우선순위 방향, 시간 단위, ISR 호출 규칙, 반환값)를 안다"고 말하면 된다. 이 노트의 §3 표가 그 문장의 근거다. 스토리 정리는 R6에서.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| FreeRTOS 우선순위 숫자를 Zephyr에 그대로 | 가장 급한 태스크가 늦게 돈다, 기아 | 숫자 방향이 반대 | 매핑 표를 한 헤더에, 숫자 직접 사용 금지 |
| `if (k_msgq_put(...))`를 성공 조건으로 | 성공할 때 에러 처리, 실패할 때 정상 처리 | 0의 의미가 반대 | 항상 `== 0` / `== pdPASS` 비교 |
| 스택 256을 Zephyr에 그대로 | 랜덤 HardFault, 스택 가드 위반 | word → byte, 4분의 1 크기 | ×4 후 `k_thread_stack_space_get()`으로 확인 |
| ISR에서 `xSemaphoreGive()` | `port.c:438` assert (assert 끄면 리스트 손상) | 태스크용 API의 임계 구역 | `...FromISR` + `portYIELD_FROM_ISR` |
| Zephyr `k_timer` 콜백에서 `k_mutex_lock(K_FOREVER)` | assert 또는 이상 동작 | 콜백이 ISR 문맥 | `k_work_submit()`으로 위임 |
| `vTaskDelay`/`k_msleep`로 주기 루프 | 이식 후 주기가 20 → 30 ms 같은 식으로 바뀜 | 상대 sleep + tick 반올림(+1 tick) | `xTaskDelayUntil` / `K_TIMEOUT_ABS_MS` |
| FreeRTOS 태스크 함수에서 return | `prvTaskExitError` assert 또는 멈춤 | 돌아갈 곳이 없음 | `vTaskDelete(NULL)`, OSAL은 트램펄린 |
| 표준 CMSIS 예제를 Zephyr에 그대로 | `undefined reference to osKernelInitialize`, 큐 생성 assert | 계층에 미구현, 동적 한도 기본 0 | 두 줄 삭제, `CONFIG_CMSIS_V2_*` 설정 |
| ThreadX에서 높은 우선순위가 먼저 받는다고 가정 | 가끔 높은 스레드가 늦게 깬다 | 대기 큐가 FIFO | `tx_*_prioritize()` 또는 설계 변경 |
| `K_SEM_DEFINE(s, max, init)`처럼 FreeRTOS 순서로 | 빌드 실패 또는 초기값과 상한이 뒤바뀜 | 인자 순서 반대 | `(initial, limit)` 확인 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "You've mostly used FreeRTOS. We use Zephyr. What would you watch out for when porting a FreeRTOS application?"

**A.** "API 이름은 표로 바꾸면 되니 쉽고, 위험한 것은 의미 차이다. 다섯 가지를 먼저 본다. 우선순위 숫자 방향(Zephyr는 작을수록 높고 음수는 협력형), 스택 단위(FreeRTOS는 word, Zephyr는 byte), 시간(Zephyr는 `k_timeout_t`, 상대 타임아웃에 +1 tick, 보드마다 tick 주파수가 다름), ISR 규칙(FromISR 대신 같은 API에 `K_NO_WAIT`), 반환값(0이 성공). 그리고 타이머 콜백이 ISR이라 그 안의 블록 호출을 workqueue로 옮긴다. 실제로 같은 앱을 OSAL로 양쪽에 돌려 보니 `sleep(20 ms)`가 Zephyr QEMU에서 30 ms 주기가 됐다. 그래서 주기 태스크는 절대 시각 대기로 바꾸고, 같은 시나리오 테스트로 양쪽 출력을 비교한다."

> The API names are the easy part; the semantics are where ports break. I check five things first: priority direction — in Zephyr lower numbers are higher and negative priorities are cooperative; stack units — FreeRTOS takes words, Zephyr takes bytes; time — Zephyr uses k_timeout_t, adds one tick to relative timeouts, and the tick rate differs per board; ISR rules — no FromISR variants, you call the same API with K_NO_WAIT; and return codes — zero means success in Zephyr but failure in many FreeRTOS calls. Timer callbacks also run in ISR context in Zephyr, so anything blocking moves to a workqueue. When I ran the same app through a small OSAL on both kernels, a 20 ms relative sleep became a 30 ms period on Zephyr's 100 Hz QEMU tick, so periodic tasks get converted to absolute-time waits and I diff the outputs of the same scenario test on both.

**꼬리 질문**: "What replaces task notifications?" → 직접 대응이 없어서 용도별로 `k_sem`(카운트), `k_event`(비트), `k_poll`(여러 소스)을 고른다.

**Q.** "Why does FreeRTOS have separate FromISR functions while Zephyr and ThreadX don't?"

**A.** "ISR에서는 블록하면 안 되고, ISR이 끝날 때 더 높은 태스크가 깨어났으면 스위치해야 한다. FreeRTOS는 이걸 이름으로 나눴다. FromISR 함수는 블록 인자가 없고 임계 구역도 ISR용(BASEPRI 저장/복원)이며, 깨운 태스크가 더 높은지를 `pxHigherPriorityTaskWoken`으로 돌려줘서 앱이 `portYIELD_FROM_ISR`로 PendSV를 요청한다. Zephyr·ThreadX는 같은 함수 안에서 문맥을 보고 대기 인자가 0이 아니면 거부하며, 스위치는 ISR 종료 경로에서 커널이 알아서 한다. FreeRTOS에서 일반 API를 ISR에서 부르면 포트가 ICSR의 VECTACTIVE를 보고 assert하는 것을 실제로 확인했다."

> In an ISR you must not block, and on exit you need to switch if you woke a higher-priority task. FreeRTOS encodes that in the function name: FromISR variants take no block time, use the interrupt-safe critical section, and report whether a higher-priority task was woken so the ISR can call portYIELD_FROM_ISR to pend PendSV. Zephyr and ThreadX use the same function, check the context inside, reject any non-zero wait, and reschedule automatically on interrupt exit. The FreeRTOS design keeps the task-level path lean and makes the context explicit; the Zephyr design is simpler to use but moves the check to runtime. If you call the task version from an ISR in FreeRTOS, the Cortex-M port asserts on the active-vector bits — I've triggered that on purpose.

**Q.** "What does priority 0 mean in FreeRTOS, Zephyr and ThreadX?"

**A.** "FreeRTOS에서 0은 idle과 같은 최저, Zephyr에서 0은 선점형 중 최고이자 main 스레드 기본값(그 위에 음수 협력형이 있음), ThreadX에서 0은 최고다. CMSIS-RTOS2는 enum이고 클수록 높으며, Cortex-M NVIC 인터럽트 우선순위는 0이 최고다. 그래서 포팅할 때는 숫자를 옮기지 않고 매핑 표를 둔다."

> In FreeRTOS, zero is the lowest, the idle priority. In Zephyr, zero is the highest preemptible priority and the default for main; negative values above it are cooperative threads that aren't preempted by other threads. In ThreadX, zero is the highest. CMSIS-RTOS2 uses an enum where bigger is higher, and on Cortex-M the NVIC treats zero as the most urgent interrupt. So I never copy numbers across; there's one mapping table in a header.

**Q.** "Would you put an OS abstraction layer in our firmware? What are the trade-offs?"

**A.** "목적이 분명하면 넣는다 — RTOS 교체 가능성, 호스트 테스트, 여러 SDK 혼합. 단 이름이 아니라 의미를 명세해야 한다. 주기 대기는 절대 시각, 타임아웃 반올림 규칙, 타이머 콜백은 ISR 수준 제약, ISR용 함수는 이름으로 분리, 정적 할당 기본. 비용은 구현에 따라 다르다. 직접 재 보니 `static inline` OSAL은 큐 왕복 차이가 1 ns 이내였고, CMSIS-RTOS2 같은 함수 계층은 호출당 약 28명령이 더 들었다. 가장 큰 위험은 최소공통분모로 기능이 빠지면서 앱이 더 나쁜 방식으로 다시 구현하는 것 — 예를 들어 delay-until이 없는 OSAL에서 주기가 drift하는 것이다."

> Yes, if there's a concrete reason — a likely RTOS change, host-side testing, or mixing vendor SDKs. But the abstraction has to specify semantics, not just names: absolute-time periodic waits, a defined timeout rounding rule, timer callbacks treated as interrupt context, separate ISR-safe calls, and static allocation by default. Cost depends on how it's built: in my measurements an inline-header OSAL added essentially nothing to a queue round trip, while a function-based layer like CMSIS-RTOS2 on Zephyr added about 28 instructions per call. The real risk is the lowest-common-denominator effect — when the OSAL drops delay-until, the application re-implements periodic timing with relative sleeps and starts drifting.

**Q.** "What is CMSIS-RTOS2, and would you use it?"

**A.** "Arm이 정한 표준 RTOS API다. RTX5가 원래 구현이고 FreeRTOS용 래퍼와 Zephyr 호환 계층이 있다. 장점은 이름·반환값·ISR 호출 규칙이 문서로 정해진 업계 공용 OSAL이라는 것이다. 단점은 표준이 구현에 맡긴 부분 — 타이머 콜백 문맥, tick 주파수, 메시지 우선순위 — 이 구현마다 다르다는 것이다. Zephyr v4.4.2 계층을 직접 써 보니 `osKernelInitialize`가 없어 링크 에러, `osDelayUntil`이 상대 sleep으로 구현, 재귀 비트 무시, `msg_prio` 무시, 동적 객체 한도 기본 0이었다. 새 Zephyr 프로젝트라면 네이티브 API를, 여러 RTOS를 오가는 라이브러리라면 CMSIS-RTOS2를 쓰되 구현 소스를 한 번 읽는다."

> CMSIS-RTOS2 is Arm's standard RTOS API — RTX5 is the reference implementation, and there are wrappers for FreeRTOS and a compatibility layer in Zephyr. It's essentially an industry-standard OSAL with documented names, status codes and ISR-callable functions. The catch is everything the standard leaves to the implementation: timer callback context, tick rate, message priority. On Zephyr 4.4.2 I found osKernelInitialize isn't implemented, osDelayUntil is a relative sleep under the hood, the recursive-mutex bit is ignored, msg_prio is ignored, and dynamic object limits default to zero. For a new Zephyr product I'd use the native API; for middleware that must run on several kernels, CMSIS-RTOS2 is reasonable — after reading the implementation once.

**Q.** "After a port, a periodic task drifts or runs at the wrong rate. Why?"

**A.** "세 가지를 의심한다. 상대 sleep으로 주기를 만들었는지(실행 시간과 +1 tick이 매 주기 누적), tick 주파수가 바뀌었는지(`pdMS_TO_TICKS`는 내림, `K_MSEC`는 올림), 절대 대기 API가 실제로 절대인지(Zephyr의 POSIX `clock_nanosleep`과 CMSIS `osDelayUntil`은 내부에서 상대로 바꾼다). 실측으로 FreeRTOS 1 kHz에서 20 ms였던 루프가 Zephyr 100 Hz에서 30 ms, 1 kHz에서 21 ms가 됐다."

> Three suspects. First, a relative sleep used as a period — execution time plus the extra tick that Zephyr adds to relative timeouts accumulate every cycle. Second, a different tick rate — pdMS_TO_TICKS rounds down, K_MSEC rounds up. Third, an "absolute" API that isn't — on Zephyr both POSIX clock_nanosleep with TIMER_ABSTIME and CMSIS osDelayUntil convert to a relative sleep internally. I measured a 20 ms loop that ran at exactly 20 ms on FreeRTOS at 1 kHz become 30 ms on Zephyr at 100 Hz and 21 ms at 1 kHz. The fix is xTaskDelayUntil or K_TIMEOUT_ABS_MS, or a hardware timer for anything tight.

**Q.** "Can you call a blocking API from a software timer callback?"

**A.** "RTOS마다 다르다. FreeRTOS는 daemon 태스크에서 돌아서 기술적으로는 되지만 그동안 모든 타이머가 멈추니 하지 않는다. ThreadX는 timer thread에서 대기하면 `TX_WAIT_ERROR`를 돌려준다. Zephyr `k_timer` 콜백은 ISR이라 아예 안 되고, `k_is_in_isr()`로 확인하면 1이 나온다. CMSIS-RTOS2 표준은 'timer thread 또는 ISR'이라고만 정해서 ISR 호출 가능 함수만 쓰라고 권한다. 그래서 이식 가능한 규칙은 '타이머 콜백은 ISR처럼 취급하고 무거운 일은 태스크나 workqueue로 넘긴다'이다."

> It depends on the kernel, which is exactly why I treat timer callbacks as interrupt context everywhere. FreeRTOS runs them in the timer daemon task, so blocking is technically possible but stalls every other timer. ThreadX runs them in its timer thread and returns TX_WAIT_ERROR if you try to wait there. Zephyr runs k_timer expiry functions in the system timer ISR, so blocking isn't allowed at all. CMSIS-RTOS2 explicitly says the callback may run in a timer thread or in an ISR. So the portable rule is: do the minimum in the callback — give a semaphore, set an event — and push real work to a task or a workqueue.

**Q.** "What happens if a FreeRTOS task function returns?"

**A.** "돌아갈 곳이 없다. 포트가 첫 스택 프레임의 LR에 `prvTaskExitError`를 넣어 두어서 거기로 가고, assert가 켜져 있으면 바로 걸리고 아니면 인터럽트를 끈 채 멈춘다. 끝내려면 `vTaskDelete(NULL)`. Zephyr·POSIX는 return이 정상 종료이고, ThreadX는 COMPLETED 상태로 남아 terminate·delete가 필요하다. 그래서 OSAL을 만들 때 FreeRTOS 백엔드에 트램펄린을 넣었다."

> There's nothing to return to. The Cortex-M port seeds the initial stack frame's link register with prvTaskExitError, so returning lands there: with configASSERT enabled it asserts immediately, otherwise it disables interrupts and spins. The correct exit is vTaskDelete(NULL). In Zephyr and POSIX, returning simply ends the thread; in ThreadX the thread sits in the completed state until you terminate and delete it. That difference is why my OSAL's FreeRTOS backend wraps every task entry in a trampoline that deletes the task after the function returns.

---

## 11. 직접 해보기

1. **개념**: Zephyr에서 `K_SEM_DEFINE(s, 3, 1)`이라고 쓰면 무슨 일이 일어나나? FreeRTOS 습관으로 `(max, init)`을 생각하고 쓴 코드다. 정답/힌트: `BUILD_ASSERT((count_limit) != 0 && initial_count <= count_limit …)`에 걸려 빌드가 실패한다. `(1, 3)`이었다면 빌드는 되고 초기 1, 상한 3으로 조용히 돈다.
2. **코드**: 예제 3(ThreadX)의 producer는 `next - tx_time_get()`만큼 잔다. 계산 직후 더 높은 스레드에 선점되어 `tx_time_get()`이 `next`를 지나 버리면 `ULONG` 뺄셈이 어떻게 되나? 안전한 버전을 작성하라. 정답/힌트: 음수가 아주 큰 양수(약 49일 tick)가 된다. `ULONG now = tx_time_get(); if ((LONG)(next - now) > 0) tx_thread_sleep(next - now);`처럼 부호 있는 차이로 비교한다.
3. **코드**: `os.h`에 `os_sleep_until_ms(uint32_t *next_ms, uint32_t period_ms)`를 추가하라(FreeRTOS: `xTaskDelayUntil`, Zephyr: `k_sleep(K_TIMEOUT_ABS_MS())`). 예제 12의 producer를 바꿔 Zephyr 100 Hz에서도 20·40·60·80·100 ms가 나오는지 확인하라. 정답/힌트: FreeRTOS 쪽은 `TickType_t` 상태를 OSAL 안에 숨겨야 하므로 `next_ms`를 tick으로 바꿔 저장하거나 OSAL 전용 구조체를 쓴다. Zephyr는 예제 2와 같은 결과가 나와야 한다.
4. **측정**: 예제 15의 FreeRTOS ping-pong을 (a) binary 세마포어, (b) `xTaskNotifyGive`/`ulTaskNotifyTake`로 바꿔 왕복 시간을 재라. 어느 쪽이 빠르고 왜인가? 정답/힌트: 세마포어는 "크기 0인 큐"라 큐 경로를 그대로 타고, task notification은 TCB의 값 하나를 바꾸므로 더 짧을 것으로 예상된다. 숫자는 직접 재서 확인한다(D5).
5. **측정**: Zephyr ping-pong 벤치를 `-DCONFIG_SYS_CLOCK_TICKS_PER_SEC=10000`으로 다시 빌드해 재라. 왕복 시간이 바뀌나? 정답/힌트: 큐 왕복 경로에는 타임아웃 등록이 없으므로(`K_FOREVER`) 거의 같아야 한다. tick 주파수는 "대기 시간의 해상도"를 바꾸지 "스위치 비용"을 바꾸지 않는다. 바뀐다면 tick 인터럽트 빈도 증가 때문이니 그 차이도 해석해 보라.
6. **개념**: CMSIS-RTOS2 on Zephyr에서 `osMutexAttr_t a = { .attr_bits = osMutexRecursive };`로 `osMutexNew(&a)`를 부르면? `CONFIG_ASSERT`를 켰을 때와 껐을 때로 나눠 답하라. 정답/힌트: 켜면 `osMutexPrioInherit`가 빠져서 assert. 끄면 그냥 만들어지고, 재귀 + PI인 `k_mutex`로 동작한다(비트와 무관).
7. **코드**: 예제 17을 ThreadX 큐(`tx_queue_receive`로 두 스레드 대기)로 바꿔 같은 FIFO 현상을 재현하고, `tx_queue_prioritize()`로 고쳐라. 정답/힌트: 세마포어와 같은 결과(낮은 쪽이 먼저)가 나와야 한다. 큐·뮤텍스·블록 풀·바이트 풀 모두 `*_prioritize()`가 있다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 로제타석 (Rosetta stone) | 같은 글을 여러 문자로 새긴 비석 | 여기서는 "같은 개념의 RTOS별 이름 대조표" |
| OSAL | OS Abstraction Layer | 앱과 RTOS 사이에서 이름·규약을 통일하는 얇은 층 |
| CMSIS-RTOS2 | Arm 표준 RTOS API v2 | `osThreadNew`, `osMessageQueuePut` …, 표준화된 OSAL |
| RTX5 | Arm의 RTOS 커널 | CMSIS-RTOS2의 원래(참조) 구현 |
| PSE51 / PSE52 | POSIX 실시간 하위 프로파일 | 최소 실시간 시스템 / 실시간 컨트롤러 시스템 |
| `k_timeout_t` | Zephyr 타임아웃 타입 | 정수가 아닌 구조체. `K_MSEC`, `K_FOREVER`, `K_NO_WAIT`, `K_TIMEOUT_ABS_MS`로 만든다 |
| tick | 커널 시간의 최소 단위 | 주파수가 RTOS·보드마다 다르다 (이 랩: FreeRTOS 1 kHz, Zephyr QEMU 100 Hz) |
| FromISR | FreeRTOS의 ISR 전용 API 접미사 | 블록 인자 없음, `pxHigherPriorityTaskWoken`으로 스위치 필요 여부를 알려 줌 |
| `portYIELD_FROM_ISR` | ISR 끝 스위치 요청 | Cortex-M에서 PendSV를 pend |
| cooperative thread | 협력형 스레드 | Zephyr 음수 우선순위. 스스로 양보·블록하기 전엔 다른 스레드에 선점되지 않는다 |
| preemption-threshold | ThreadX 선점 문턱 | "이 우선순위보다 높은 스레드만 나를 선점할 수 있다" |
| daemon task | FreeRTOS 타이머 서비스 태스크 | `prvTimerTask`, 타이머 콜백과 pend된 함수를 실행 |
| timer thread | ThreadX 시스템 타이머 스레드 | 우선순위 0, 그 안에서 대기하면 `TX_WAIT_ERROR` |
| 트램펄린 (trampoline) | 진짜 함수를 감싸 부르는 작은 함수 | OSAL이 return 의미를 맞추려고 넣는 접착 코드 |
| 최소공통분모 (LCD) | 모든 백엔드가 가진 기능만 남기기 | OSAL의 대표적인 단점 |
| suspension list | ThreadX 대기 리스트 | 기본 FIFO, `tx_*_prioritize()`로 우선순위 순 정렬 |
| icount | QEMU 명령 수 기반 가상 시간 | 이 랩: 명령 1개 = 32 ns, 결정적 타이밍 |
| `errQUEUE_FULL` | FreeRTOS 큐 가득 참 반환값 | 값이 0 — `pdFALSE`와 같다 |
| `osErrorISR` | CMSIS-RTOS2 반환값 -6 | ISR에서 금지된 호출 |
| semihosting | 디버거/에뮬레이터에 시스템 호출을 넘기는 방식 | 이 랩의 `bkpt 0xab` + `SYS_EXIT`로 QEMU 종료 |

---

## 13. 요약 & 체크리스트

RTOS 사이 번역은 이름 치환이 아니라 의미 검증이다. 다섯 RTOS의 태스크·큐·세마포어·뮤텍스·이벤트·타이머·임계 구역은 표 한 장(§1)으로 대응되지만, 우선순위 방향(FreeRTOS·CMSIS·POSIX는 클수록, Zephyr·ThreadX·NVIC는 작을수록 높다), 시간 단위와 반올림(`pdMS_TO_TICKS` 내림, `K_MSEC` 올림, 상대 타임아웃 +1 tick, 보드마다 다른 tick), ISR 규칙(FromISR 대 같은 API + 대기 0), 반환값(0이 성공인지 실패인지), 스택 단위(word 대 byte), 뮤텍스 재귀·PI 기본값, 세마포어 인자 순서와 포화 동작, 태스크 함수 return, 타이머 콜백 문맥, 대기자 깨우는 순서(ThreadX FIFO)가 다르다. 같은 프로그램을 FreeRTOS·Zephyr·ThreadX·POSIX(Linux와 Zephyr)에서 돌리면 결과값은 같아도 시간 의미가 달라졌고, OSAL과 CMSIS-RTOS2는 이름은 통일했지만 그 차이를 숨겼다. `static inline` OSAL은 비용이 0에 가깝고, 함수 계층(CMSIS 약 +11%, POSIX mq 약 +44% 왕복)은 비용이 있다. 표준을 쓰더라도 구현 소스를 한 번은 읽는다.

- [ ] 태스크 생성·지연·주기 대기·세마포어·뮤텍스·큐·이벤트·타이머·풀을 다섯 RTOS 이름으로 말할 수 있다
- [ ] FreeRTOS·Zephyr·ThreadX·CMSIS·POSIX·NVIC의 우선순위 방향과 0의 의미를 그림 없이 말할 수 있다
- [ ] `pdMS_TO_TICKS`(내림)와 `K_MSEC`(올림), Zephyr 상대 타임아웃 +1 tick을 설명하고 20 → 30 ms 사례를 말할 수 있다
- [ ] ISR에서 FreeRTOS는 FromISR + `portYIELD_FROM_ISR`, 나머지는 같은 API + 대기 0인 이유를 설명할 수 있다
- [ ] 각 RTOS에서 타이머 콜백이 어느 문맥에서 도는지 말할 수 있다
- [ ] 반환값 규약을 알고 번역할 때 항상 상수와 비교한다
- [ ] 스택 인자 단위(FreeRTOS word, 나머지 byte)를 안다
- [ ] 뮤텍스 재귀·PI 기본값과 세마포어 인자 순서·포화 동작의 차이를 안다
- [ ] FreeRTOS 태스크 함수가 return하면 무슨 일이 일어나는지 안다
- [ ] 작은 OSAL을 설계할 때 "의미 명세" 항목(절대 주기, 반올림, 콜백 문맥, ISR 분리, 정적 할당)을 넣을 수 있다
- [ ] CMSIS-RTOS2 on Zephyr의 함정(시작 함수 없음, 상대 `osDelayUntil`, 재귀·`msg_prio` 무시, 동적 한도 0)을 말할 수 있다
- [ ] ThreadX 대기 리스트가 FIFO이고 `tx_*_prioritize()`가 있다는 것을 안다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `include/semphr.h`(`xSemaphoreGive` 매크로), `queue.c`(`xQueueGenericSend`), `tasks.c`(`vTaskPlaceOnEventList`, `prvAddCurrentTaskToDelayedList`), `portable/GCC/ARM_CM3/port.c`(`vPortEnterCritical`, `prvTaskExitError`), `include/projdefs.h`(`pdMS_TO_TICKS`, `errQUEUE_FULL`), `History.txt` — [github.com/FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- FreeRTOS 공식 문서: [freertos.org](https://www.freertos.org/) — API 레퍼런스(태스크, 큐, 세마포어, 타이머, task notification)
- Zephyr v4.4.2 소스 (랩 `/var/rtoslab/zephyrproject/zephyr`): `kernel/sem.c`, `kernel/timeout.c`(`z_add_timeout`), `kernel/msg_q.c`, `kernel/mutex.c`, `kernel/sched.c`, `kernel/Kconfig`(`SYS_CLOCK_TICKS_PER_SEC`, 우선순위 개수), `include/zephyr/kernel.h`(`K_SEM_DEFINE`, `K_PRIO_COOP`), `lib/os/clock.c`(`clock_gettime`, `clock_nanosleep`), `lib/posix/options/mqueue.c`, `lib/posix/Kconfig.profile`(PSE51/52)
- Zephyr CMSIS-RTOS v2 호환 계층: `subsys/portability/cmsis_rtos_v2/`(`thread.c`, `kernel.c`, `timer.c`, `mutex.c`, `msgq.c`, `Kconfig`), 샘플 `samples/subsys/portability/cmsis_rtos_v2/philosophers`
- Zephyr 공식 문서: [docs.zephyrproject.org](https://docs.zephyrproject.org/latest/) — Kernel Services(스레드, 타이밍, 동기화, 데이터 전달), Portability(POSIX, CMSIS-RTOS v2)
- Eclipse ThreadX 소스: [github.com/eclipse-threadx/threadx](https://github.com/eclipse-threadx/threadx) (이 노트는 커밋 `56c8695`, `tx_api.h` 6.5.2) — `common/src/tx_semaphore_put.c`, `tx_semaphore_get.c`, `txe_semaphore_get.c`, `ports/cortex_m3/gnu/`(`tx_port.h`, `example_build/tx_initialize_low_level.S`), `utility/rtos_compatibility_layers/`(FreeRTOS, POSIX, OSEK)
- CMSIS-RTOS2 표준 문서: 랩 `modules/hal/cmsis_6/CMSIS/Documentation/Doxygen/RTOS2/src/`(`ref_cmsis_os2_timer.txt`의 콜백 문맥, `ref_cmsis_os2_groups.txt`의 ISR 호출 가능 목록), 온라인 [arm-software.github.io/CMSIS_6](https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html)
- CMSIS-FreeRTOS (CMSIS-RTOS2 API의 FreeRTOS 구현): [github.com/ARM-software/CMSIS-FreeRTOS](https://github.com/ARM-software/CMSIS-FreeRTOS)
- POSIX 실시간 하위 프로파일 근거 (Zephyr Kconfig가 인용하는 문서): [The Open Group — Subprofiling Considerations](https://pubs.opengroup.org/onlinepubs/9699919799/xrat/V4_subprofiles.html)
- 관련 Don 노트: EL A8(유저 공간 동기화, pthread·PI futex), EL F4(커널 락), EL F6(인터럽트), EL F8(시간), EL N1(Linux 실시간)
