# C04. RTOS — FreeRTOS와 Zephyr로 task 스케줄링·동기화·ISR 연동·저전력 idle까지

> **이 노트를 다 읽으면**: Cortex-M에서 컨텍스트 스위치가 PendSV/SysTick으로 어떻게 일어나는지 그린다 · queue/semaphore/mutex/event group/task notification 중 무엇을 언제 쓰는지 고른다 · priority inversion을 타임라인으로 설명하고 막는다 · FreeRTOS와 Zephyr 실제 API로 ISR→task, 타이머, workqueue, watchdog, tickless idle 코드를 쓴다
> **JD 연결**: "own RTOS task scheduling" · "Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar)"
> **Don 기준 난이도**: ISR·우선순위·critical section·SSD 자체 스케줄러는 이미 앎 / **FreeRTOS·Zephyr API 이름과 규칙, Zephyr 빌드 체계(west·Kconfig·devicetree), tickless idle**은 새로 배울 부분

---

## 0. 큰 그림

bare-metal의 superloop는 "한 줄로 도는 main 루프 + ISR"이다. RTOS는 여기에 **여러 개의 독립된 실행 흐름(task/thread)과 그 사이를 전환하는 스케줄러, 그리고 흐름끼리 안전하게 데이터를 주고받는 커널 객체**를 더한다.

예를 들어 Hark 같은 always-on 기기의 MCU(구조는 추정)를 RTOS로 짜면 이렇게 된다.

```
 우선순위 높음
   ^   [ISR] PDM DMA half/full ---- notify ----+
   |   [ISR] IMU INT (GPIO) ------ give sem ---|---+
   |   [ISR] UART RX (SoC IPC) --- stream buf -|---|---+
   |                                           v   |   |
   |   audio_task      (prio 5) : 10ms 블록 -> feature -> ring buffer
   |   kws_task        (prio 4) : wake word 추론 (C08) -> 감지 시 SoC wake GPIO
   |   sensor_task     (prio 3) <------------------+   |
   |   ipc_task        (prio 3) <----------------------+  SoC 와 메시지
   |   ble_host        (prio 2) : 폰 연결/알림 (벤더 스택 thread)
   |   supervisor/wdt  (prio 1) : 모든 task check-in 확인 후 HW watchdog feed
   |   idle            (prio 0) : WFI -> tickless -> deep sleep (C05)
 우선순위 낮음
```

핵심 질문은 세 가지다. **누가 먼저 도나(스케줄링)**, **어떻게 데이터를 넘기나(IPC)**, **아무것도 안 할 때 얼마나 깊게 자나(idle/저전력)**. 이 노트는 이 순서로 간다.

---

## 1. 왜 RTOS인가

### 1.1 superloop의 한계

```c
for (;;) {
    audio_poll();     /* 10ms 마다 해야 함 */
    sensor_poll();    /* 50ms 마다 */
    ble_process();    /* 가끔 30ms 걸림 */
    kws_run();        /* 추론: 15ms 걸림 */
}
```

- `kws_run()`이 15ms 걸리는 동안 `audio_poll()`의 10ms deadline이 깨진다. 긴 일을 쪼개서 상태 기계로 만들어야 하는데 코드가 급격히 복잡해진다.
- 모든 일이 서로의 최악 실행 시간에 묶인다. 한 모듈을 고치면 전체 타이밍이 바뀐다.
- "할 일이 없으면 잔다"를 전역적으로 판단하기 어렵다.

RTOS는 **preemption**으로 이것을 푼다. 높은 우선순위 task가 준비되면 낮은 task가 무엇을 하고 있든 즉시 뺏는다. 각 task는 자기 일만 순차 코드로 쓰고 기다릴 때는 **block**한다.

### 1.2 RTOS가 주는 것과 대가

| 주는 것 | 대가 |
|---|---|
| 우선순위 기반 preemption → 응답 시간 보장 | task마다 스택 RAM (수백 B ~ 수 KB) |
| block/wake 기반 설계 → idle이 자연스럽게 저전력 | 컨텍스트 스위치 비용 (Cortex-M에서 대략 수백 사이클 이하, 포트·옵션마다 다름) |
| 표준 IPC 객체 (queue, mutex...) | 새로운 버그 종류: race, deadlock, priority inversion, stack overflow |
| 타이머·시간 관리 | 비결정성 분석 필요 (RMS, 응답 시간 분석) |

> **Don 경험과 연결**: SSD 펌웨어의 자체 스케줄러(명령 큐 → 코어별 작업 디스패치, 이벤트 기반 루프)는 RTOS가 하는 일의 부분집합이다. 면접에서는 "SSD FW에서 command queue와 ISR 기반 이벤트 처리를 직접 설계했다. RTOS의 queue와 FromISR API는 그 패턴을 표준화한 것"이라고 연결한다. 단 SSD FW가 cooperative(run-to-completion)였다면 preemption이 새로 생기는 race를 이해하고 있다는 것을 보여 줘야 한다.

---

## 2. 스케줄러와 task 상태

### 2.1 FreeRTOS task 상태도

```
                    vTaskSuspend()                    
       +-----------------------------------------+       
       |                                         v       
   +-------+  스케줄러가 선택   +---------+   +-----------+
   | Ready | ----------------> | Running |   | Suspended |
   +-------+ <---------------- +---------+   +-----------+
     ^   ^   더 높은 prio 준비     |      ^        |
     |   |   / taskYIELD()         |      |        | vTaskResume()
     |   |                         |      +--------+--> Ready
     |   |   xQueueReceive(),      |  vTaskSuspend()
     |   |   vTaskDelay(),         v
     |   |   xSemaphoreTake() +---------+
     |   +------------------- | Blocked |   이벤트 도착 또는 타임아웃 -> Ready
     |      이벤트/타임아웃     +---------+
     +-- xTaskCreate() 로 생성되면 Ready 에서 시작
```

- **Running**: 지금 CPU를 쓰는 task. 단일 코어면 하나뿐이다.
- **Ready**: 돌 수 있지만 더 높거나 같은 우선순위 task가 돌고 있다.
- **Blocked**: 이벤트(queue 데이터, semaphore, 알림)나 시간을 기다린다. **타임아웃이 있다**. Blocked task는 CPU를 전혀 쓰지 않는다.
- **Suspended**: 명시적으로 멈춤. 타임아웃이 없다. (`INCLUDE_vTaskSuspend`가 1일 때 `portMAX_DELAY`로 무한 대기하는 task는 내부적으로 suspended 리스트에 들어간다.)
- 삭제된 task의 TCB/스택은 **idle task가 정리**한다. 그래서 idle task를 굶기면(높은 우선순위 task가 busy loop) 메모리 누수처럼 보인다.

### 2.2 스케줄링 규칙

- **Fixed-priority preemptive**: 항상 Ready 중 가장 높은 우선순위가 돈다(`configUSE_PREEMPTION 1`).
- **Time slicing**: 같은 우선순위끼리는 tick마다 round-robin(`configUSE_TIME_SLICING 1`, 기본값).
- Tick interrupt(`configTICK_RATE_HZ`, 흔히 1000Hz 또는 100Hz)가 지연 시간을 세고 time slice를 나눈다.
- 스케줄링은 **이벤트 기반**이다. tick만 기다리지 않는다. ISR에서 높은 우선순위 task를 깨우면 ISR이 끝나자마자(`portYIELD_FROM_ISR`) 전환된다.

### 2.3 우선순위 숫자 방향 (가장 흔한 실수)

| 시스템 | 높은 우선순위 | 비고 |
|---|---|---|
| FreeRTOS task | **숫자가 클수록 높음** | 0 = idle, 최대 `configMAX_PRIORITIES - 1` |
| Zephyr thread | **숫자가 작을수록 높음** | 음수 = cooperative(선점 안 됨), 0 이상 = preemptible |
| Cortex-M NVIC 인터럽트 | **숫자가 작을수록 높음** | 0이 가장 높음. 구현 비트 수는 칩마다(STM32 4비트, nRF52 3비트) |

FreeRTOS에서 일하다 Zephyr로 가면(또는 반대) 반드시 틀린다. 면접에서 이 표를 먼저 말하면 실무 경험이 있어 보인다.

### 2.4 Zephyr thread 종류와 상태

- **Cooperative thread**(음수 우선순위, `K_PRIO_COOP(n)`): 스스로 block/yield하기 전에는 다른 thread에게 선점당하지 않는다(ISR은 예외). 짧고 공유 데이터가 많은 코드(예: BLE 스택 일부, system workqueue 기본 우선순위 -1)에 쓴다.
- **Preemptible thread**(0 이상, `K_PRIO_PREEMPT(n)`): 더 높은 우선순위가 준비되면 선점된다. `main()`은 기본 우선순위 0(`CONFIG_MAIN_THREAD_PRIORITY`)의 preemptible thread다.
- **Meta-IRQ** 우선순위(`CONFIG_NUM_METAIRQ_PRIORITIES`): cooperative thread까지 선점하는 특수 우선순위. 드라이버 bottom half용(고급).
- 상태: Ready, Running, 그리고 "unready" 사유로 pending(커널 객체 대기), sleeping(시간 대기), suspended, terminated/aborted. FreeRTOS의 Blocked가 Zephyr에서는 pending/sleeping으로 나뉜다고 보면 된다.
- Time slicing은 `CONFIG_TIMESLICING`, `CONFIG_TIMESLICE_SIZE`, `CONFIG_TIMESLICE_PRIORITY`로 설정한다.

---

## 3. Cortex-M 컨텍스트 스위치: SysTick, PendSV, SVC

### 3.1 재료

- **두 개의 스택 포인터**: MSP(main stack, ISR과 커널 부팅용), PSP(process stack, task용). task마다 자기 스택을 가지고, 스위치는 결국 **PSP 값을 바꿔 끼우는 것**이다.
- **하드웨어 자동 stacking**: 예외 진입 시 CPU가 R0–R3, R12, LR, PC, xPSR 8워드를 현재 스택(PSP)에 자동으로 push한다. FPU 사용 중이면 S0–S15, FPSCR까지(lazy stacking 가능).
- **SysTick**: 코어 내장 24비트 타이머. RTOS tick을 만든다.
- **PendSV**: 소프트웨어로 pending시키는 예외. **가장 낮은 우선순위**로 설정한다. 그래서 다른 모든 ISR이 끝난 뒤에만 실행된다(tail-chaining).
- **SVC**: `svc` 명령으로 부르는 예외. FreeRTOS Cortex-M3/M4 포트는 첫 task 시작에 쓴다.

### 3.2 왜 스위치를 SysTick ISR 안에서 바로 하지 않고 PendSV로 미루나

SysTick이 다른 ISR(예: UART)을 선점한 상태에서 컨텍스트를 바꾸면, 스택에는 UART ISR의 프레임이 있는데 task를 바꿔 버리는 셈이 된다. PendSV를 최저 우선순위로 두면 **중첩된 ISR이 모두 끝난 순간에만** 스위치가 일어나므로 항상 "task 컨텍스트 ↔ task 컨텍스트" 전환이 된다. 여러 ISR이 각각 스위치를 요청해도 PendSV는 한 번만 실행된다.

### 3.3 타임라인

```
 시간 --->
 Task A (prio 1) ████████                          ...
 UART ISR                ████  (xQueueSendFromISR -> Task B 깨움, PendSV pend)
 PendSV                      ██ (A 저장, B 복원)
 Task B (prio 3)               ██████████ (queue 처리 후 다시 block)
 PendSV                                  ██ (B 저장, A 복원)
 Task A                                    ██████ ...

 SysTick 도 같은 방식: tick 증가 -> 깨울 task 가 있거나 time slice 만료 -> PendSV pend
```

### 3.4 PendSV 핸들러가 하는 일 (FreeRTOS ARM_CM4F 포트, 단순화)

실제 코드는 `portable/GCC/ARM_CM4F/port.c`의 `xPortPendSVHandler`다. 흐름만 남기면 이렇다.

```
xPortPendSVHandler:
    mrs     r0, psp                 @ 현재 task 의 스택 포인터 (HW 가 8워드를 이미 push)
    ldr     r3, =pxCurrentTCB
    ldr     r2, [r3]                @ r2 = 현재 TCB
    tst     r14, #0x10              @ EXC_RETURN bit4 == 0 이면 FPU 컨텍스트 있음
    it      eq
    vstmdbeq r0!, {s16-s31}         @ FPU callee-saved 레지스터 저장
    stmdb   r0!, {r4-r11, r14}      @ 나머지 레지스터 + EXC_RETURN 저장
    str     r0, [r2]                @ TCB 첫 필드(pxTopOfStack) 에 새 SP 기록

    mov     r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY
    msr     basepri, r0             @ 커널 자료구조 보호 (API 호출 가능한 ISR 만 마스크)
    bl      vTaskSwitchContext      @ 다음에 돌 task 선택 -> pxCurrentTCB 갱신
    mov     r0, #0
    msr     basepri, r0

    ldr     r1, [r3]
    ldr     r0, [r1]                @ 새 task 의 pxTopOfStack
    ldmia   r0!, {r4-r11, r14}
    tst     r14, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
    msr     psp, r0
    bx      r14                     @ 예외 복귀: HW 가 나머지 8워드를 pop -> 새 task 실행
```

```
 task 스택 (저장된 상태, 높은 주소가 위)
 +-----------+
 | xPSR      |  <- HW 자동 stacking
 | PC        |
 | LR        |
 | R12       |
 | R3 .. R0  |
 +-----------+
 | (S16-S31) |  <- FPU 사용 task 만
 | R11 .. R4 |  <- PendSV 가 수동 저장
 | EXC_RETURN|
 +-----------+  <- pxTopOfStack (TCB 에 기록)
```

- 새 task를 처음 만들 때 FreeRTOS는 이 모양의 **가짜 프레임**을 스택에 미리 만들어 둔다(`pxPortInitialiseStack`). PC 자리에 task 함수 주소, R0 자리에 인자. 그래서 첫 스위치가 "복귀"처럼 동작해 task 함수로 들어간다.
- FreeRTOSConfig.h에서 `#define xPortPendSVHandler PendSV_Handler`, `#define xPortSysTickHandler SysTick_Handler`, `#define vPortSVCHandler SVC_Handler`로 벡터 테이블 이름과 연결한다(CubeMX는 자동 처리).
- Zephyr도 같은 원리다(`arch/arm/core/cortex_m/swap_helper.S`의 PendSV 핸들러). 커널 구현을 몰라도 되지만 "PendSV가 최저 우선순위라서 ISR 중첩이 다 끝난 뒤 스위치"라는 설명은 둘 다 같다.

> **Don 경험과 연결**: Cortex-R8/R82에는 PendSV/PSP가 없다(R8 같은 ARMv7-R은 모드별 banked SP와 IRQ/FIQ 모델, R82 같은 ARMv8-R AArch64는 EL별 SP). R 코어에서 bare-metal로 컨텍스트를 다뤘다면 "M 프로파일은 이것을 하드웨어 stacking + PendSV로 단순화했다"고 비교하면 된다.

---

## 4. Task 만들기

### 4.1 FreeRTOS: 동적 생성과 정적 생성

```c
#include "FreeRTOS.h"
#include "task.h"

#define SENSOR_STACK_WORDS  256            /* 주의: 바이트가 아니라 StackType_t 개수 (Cortex-M 에서 1 word = 4B) */
#define SENSOR_PRIO         (tskIDLE_PRIORITY + 3)

static void sensor_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(50);
    TickType_t last_wake = xTaskGetTickCount();
    (void)arg;

    for (;;) {
        /* 센서 읽기 ... */
        vTaskDelayUntil(&last_wake, period);   /* 드리프트 없는 주기 실행 */
    }
}

/* 정적 할당: 힙 없이 링커가 RAM 을 잡는다 (configSUPPORT_STATIC_ALLOCATION 1) */
static StackType_t audio_stack[512];
static StaticTask_t audio_tcb;
extern void audio_task(void *arg);

int main(void)
{
    /* 하드웨어 초기화 ... */
    BaseType_t ok = xTaskCreate(sensor_task, "sensor", SENSOR_STACK_WORDS,
                                NULL, SENSOR_PRIO, NULL);
    configASSERT(ok == pdPASS);

    TaskHandle_t h = xTaskCreateStatic(audio_task, "audio", 512, NULL,
                                       tskIDLE_PRIORITY + 5, audio_stack, &audio_tcb);
    configASSERT(h != NULL);

    vTaskStartScheduler();                     /* 돌아오지 않는다 */
    for (;;) { }                               /* 여기 오면 idle/timer task 생성 실패 = 힙 부족 */
}
```

- `vTaskDelay(n)`는 "지금부터 n tick 뒤"라 처리 시간만큼 주기가 밀린다. 주기 task는 `vTaskDelayUntil()`(V10.4.3부터는 반환값이 있는 `xTaskDelayUntil()`도 있음)을 쓴다.
- `pdMS_TO_TICKS(ms)`: ms를 tick으로 바꾼다. `configTICK_RATE_HZ`가 100이면 5ms는 0 tick이 되니 주의한다.
- 정적 할당을 켜면 idle task 메모리를 앱이 제공해야 한다: `vApplicationGetIdleTaskMemory()`(타이머를 쓰면 `vApplicationGetTimerTaskMemory()`도).
- `vTaskStartScheduler()`가 돌아온다면 거의 항상 힙이 부족해서 idle/timer task를 못 만든 것이다.

### 4.2 Zephyr: 정적 정의와 런타임 생성

```c
#include <zephyr/kernel.h>

#define SENSOR_STACK_SIZE 1024                 /* Zephyr 는 바이트 단위 */
#define SENSOR_PRIO       5                    /* 작을수록 높음 (preemptible) */

static void sensor_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    int64_t next = k_uptime_get();
    while (1) {
        /* 센서 읽기 ... */
        next += 50;
        k_sleep(K_TIMEOUT_ABS_MS(next));       /* 절대 시각으로 주기 유지 (tickless 커널) */
    }
}

/* 컴파일 타임 정의: 스택, TCB, 자동 시작까지 */
K_THREAD_DEFINE(sensor_tid, SENSOR_STACK_SIZE, sensor_thread,
                NULL, NULL, NULL, SENSOR_PRIO, 0, 0);

/* 런타임 생성 방식 */
K_THREAD_STACK_DEFINE(audio_stack, 2048);
static struct k_thread audio_thread;
extern void audio_entry(void *, void *, void *);

int main(void)
{
    k_tid_t tid = k_thread_create(&audio_thread, audio_stack,
                                  K_THREAD_STACK_SIZEOF(audio_stack),
                                  audio_entry, NULL, NULL, NULL,
                                  K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
    k_thread_name_set(tid, "audio");         /* CONFIG_THREAD_NAME=y 필요 */
    return 0;                                  /* main thread 는 끝나도 다른 thread 는 계속 */
}
```

- `K_THREAD_DEFINE(name, stack_size, entry, p1, p2, p3, prio, options, delay)`: 마지막 인자는 시작 지연(ms). 0이면 커널 시작 시 바로 시작.
- Zephyr thread 함수는 인자 3개(`void *p1, void *p2, void *p3`)를 받는다.
- `K_THREAD_STACK_DEFINE`: MPU 정렬·guard 영역까지 고려해 스택을 잡는 매크로다. 그냥 `uint8_t` 배열을 쓰면 안 된다.
- `K_TIMEOUT_ABS_MS()`는 `CONFIG_TIMEOUT_64BIT`가 필요하다(대부분 기본 활성). 단순하게는 `k_msleep(50)`도 쓰지만 드리프트가 생긴다.

---

## 5. 동기화와 통신: 무엇을 언제 쓰나

### 5.1 한눈에 보는 표

| 목적 | FreeRTOS | Zephyr | ISR에서 보내기 |
|---|---|---|---|
| 데이터 복사 전달 (고정 크기) | Queue `xQueueSend/Receive` | Message queue `k_msgq_put/get` | `xQueueSendFromISR` / `k_msgq_put(..., K_NO_WAIT)` |
| 바이트 스트림 | Stream buffer `xStreamBufferSend` | Pipe `k_pipe`, ring buffer `ring_buf` | `xStreamBufferSendFromISR` |
| 가변 길이 메시지 | Message buffer | `k_fifo` + 사용자 구조체 | `xMessageBufferSendFromISR` / `k_fifo_put` |
| 이벤트 발생 신호 (카운트) | Binary/counting semaphore | `k_sem` | `xSemaphoreGiveFromISR` / `k_sem_give` |
| 공유 자원 상호 배제 | Mutex (priority inheritance) | `k_mutex` (priority inheritance) | **불가** (ISR은 소유자가 될 수 없음) |
| 여러 조건 대기 (비트) | Event group | `k_event`, `k_poll` | `xEventGroupSetBitsFromISR` (timer task 경유) / `k_event_post` |
| 한 task에 가벼운 신호 | Task notification | (대응: `k_sem`, `k_event`, `k_poll_signal`) | `vTaskNotifyGiveFromISR`, `xTaskNotifyFromISR` |
| 지연 실행 (bottom half) | Timer daemon `xTimerPendFunctionCallFromISR` | Workqueue `k_work_submit` | 둘 다 가능 |

선택 원칙:

1. **데이터가 있으면 queue**(복사). 포인터를 넘기면 소유권 규칙을 명확히 한다.
2. **"일이 생겼다"만 알리면 semaphore 또는 task notification**. 받는 task가 하나면 notification이 가장 가볍다.
3. **공유 자원 보호는 mutex**. semaphore로 보호하면 priority inheritance가 없다.
4. **짧은 공유 변수 보호는 critical section**(수 µs 이하), 길면 mutex.

### 5.2 Queue: 센서 샘플 전달

```c
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct {
    uint32_t timestamp;
    int16_t  accel[3];
} imu_sample_t;

static QueueHandle_t imu_q;

void imu_init_queue(void)
{
    imu_q = xQueueCreate(16, sizeof(imu_sample_t));   /* 16개 x 12B, 값 복사 */
    configASSERT(imu_q != NULL);
}

/* IMU data-ready GPIO ISR: 여기서는 FIFO 를 이미 DMA 로 읽었다고 가정 */
void imu_isr_push(const imu_sample_t *s)
{
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(imu_q, s, &woken) != pdPASS) {
        /* 큐 가득 참: 드롭 카운트 증가 (ISR 에서는 절대 block 하지 않는다) */
    }
    portYIELD_FROM_ISR(woken);
}

void fusion_task(void *arg)
{
    imu_sample_t s;
    (void)arg;
    for (;;) {
        if (xQueueReceive(imu_q, &s, pdMS_TO_TICKS(100)) == pdPASS) {
            /* 처리 */
        } else {
            /* 100ms 동안 샘플 없음: 센서 멈춤 감지 -> 복구 경로 */
        }
    }
}
```

- queue는 **값을 복사**한다. 큰 데이터(오디오 블록)는 포인터나 인덱스만 넘기고 버퍼는 풀에서 관리한다.
- 받는 쪽 타임아웃은 "영원히 기다림" 대신 의미 있는 값을 주어 **고장 감지**에 쓴다.

Zephyr의 같은 패턴:

```c
#include <zephyr/kernel.h>

struct imu_sample { uint32_t ts; int16_t accel[3]; };
K_MSGQ_DEFINE(imu_q, sizeof(struct imu_sample), 16, 4);   /* 이름, 크기, 개수, 정렬 */

void imu_isr_push(const struct imu_sample *s)
{
    if (k_msgq_put(&imu_q, s, K_NO_WAIT) != 0) {            /* ISR 에서는 반드시 K_NO_WAIT */
        /* -ENOMSG: 가득 참 */
    }
}

void fusion_thread(void *p1, void *p2, void *p3)
{
    struct imu_sample s;
    while (1) {
        if (k_msgq_get(&imu_q, &s, K_MSEC(100)) == 0) {
            /* 처리 */
        } else {
            /* -EAGAIN: 타임아웃 */
        }
    }
}
```

Zephyr는 FromISR 같은 별도 함수가 없다. **같은 API를 쓰되 ISR에서는 타임아웃을 `K_NO_WAIT`로** 준다. 스위치 필요 여부는 커널이 ISR 종료 시 알아서 판단한다.

### 5.3 Semaphore vs Mutex

| 항목 | Binary semaphore | Mutex |
|---|---|---|
| 개념 | 신호(이벤트 카운트) | 자원 소유권 |
| 누가 give하나 | 아무나 (ISR 포함) | take한 task만 |
| Priority inheritance | 없음 | 있음 |
| ISR에서 | give 가능 | 사용 불가 |
| 재귀 take | 불가 | recursive mutex(`xSemaphoreCreateRecursiveMutex`)면 가능. Zephyr `k_mutex`는 기본 재귀 허용 |
| 전형적 용도 | "DMA 완료", "버튼 눌림" | "I2C 버스 사용 중", "공유 설정 구조체" |

```c
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t i2c_bus_mutex;

void bus_init(void)
{
    i2c_bus_mutex = xSemaphoreCreateMutex();
}

int pmic_write(uint8_t reg, uint8_t val)
{
    if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return -1;                      /* 20ms 안에 못 얻음: 누가 오래 쥐고 있음 -> 로그 */
    }
    int ret = 0; /* i2c 전송 ... */
    (void)reg; (void)val;
    xSemaphoreGive(i2c_bus_mutex);      /* 모든 경로에서 반납 */
    return ret;
}
```

Zephyr: `K_MUTEX_DEFINE(i2c_bus_mutex);` → `k_mutex_lock(&i2c_bus_mutex, K_MSEC(20))` → `k_mutex_unlock(&i2c_bus_mutex)`. semaphore는 `K_SEM_DEFINE(dma_done, 0, 1);`(초기값 0, 최대 1) → ISR에서 `k_sem_give(&dma_done)`, thread에서 `k_sem_take(&dma_done, K_MSEC(10))`. 참고로 Zephyr I2C 드라이버 대부분은 내부에서 버스 lock을 이미 잡는다. 앱 mutex는 "여러 트랜잭션을 원자적으로 묶을 때" 필요하다.

### 5.4 Event group: 여러 조건 기다리기

```c
#include "FreeRTOS.h"
#include "event_groups.h"

#define EVT_WIFI_UP   (1u << 0)
#define EVT_TIME_SYNC (1u << 1)
#define EVT_BATT_OK   (1u << 2)

static EventGroupHandle_t sys_evt;

void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(sys_evt,
                              EVT_WIFI_UP | EVT_TIME_SYNC | EVT_BATT_OK,
                              pdFALSE,          /* 반환 시 비트 지우지 않음 (상태 비트) */
                              pdTRUE,           /* 모두 켜질 때까지 (AND) */
                              portMAX_DELAY);
        (void)bits;
        /* OTA 다운로드 시작 (C07) */
    }
}
```

- `xEventGroupSetBitsFromISR()`는 ISR에서 바로 비트를 바꾸지 않고 **timer daemon task에 요청을 보낸다**(비결정적 작업을 ISR 밖으로 빼기 위해). 그래서 `configUSE_TIMERS`와 `INCLUDE_xTimerPendFunctionCall`이 필요하고, daemon task 우선순위만큼 지연된다.
- Zephyr는 `k_event_post()`, `k_event_wait(&evt, mask, false, K_FOREVER)`, `k_event_wait_all()`이 있다(`CONFIG_EVENTS=y`).

### 5.5 Task notification: 가장 가벼운 신호

FreeRTOS의 모든 task는 TCB 안에 32비트 notification 값과 상태를 가진다. 별도 객체를 만들 필요가 없어 RAM이 적고 binary semaphore보다 빠르다(FreeRTOS 문서 기준 최대 45% 빠름).

```c
#include "FreeRTOS.h"
#include "task.h"

static TaskHandle_t spi_task_h;

void SPI_DMA_IRQHandler(void)            /* 벡터 이름은 칩마다 다름 */
{
    BaseType_t woken = pdFALSE;
    /* DMA 완료 플래그 클리어 ... */
    vTaskNotifyGiveFromISR(spi_task_h, &woken);     /* 카운팅 세마포어처럼 +1 */
    portYIELD_FROM_ISR(woken);
}

void spi_task(void *arg)
{
    (void)arg;
    spi_task_h = xTaskGetCurrentTaskHandle();
    for (;;) {
        /* DMA 전송 시작 ... */
        uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));   /* pdTRUE: 0 으로 클리어 */
        if (n == 0) {
            /* 5ms 타임아웃: DMA 멈춤 -> 복구 */
        }
    }
}
```

| 사용 방식 | 보내기 | 받기 |
|---|---|---|
| binary/counting semaphore 대용 | `xTaskNotifyGive()` / `vTaskNotifyGiveFromISR()` | `ulTaskNotifyTake()` |
| 이벤트 비트 (event group 대용) | `xTaskNotify(h, bits, eSetBits)` / `xTaskNotifyFromISR(..., eSetBits, &woken)` | `xTaskNotifyWait(clear_on_entry, clear_on_exit, &value, timeout)` |
| 값 하나 덮어쓰기 (mailbox) | `eSetValueWithOverwrite` | `xTaskNotifyWait()` |

한계: **받는 task가 정확히 하나**여야 하고, 여러 값을 버퍼링하지 못하며, ISR은 받을 수 없다. V10.4.0부터는 task당 여러 notification(`configTASK_NOTIFICATION_ARRAY_ENTRIES`, `xTaskNotifyGiveIndexed()` 등)을 쓸 수 있다.

### 5.6 Critical section

```c
taskENTER_CRITICAL();           /* Cortex-M: BASEPRI 를 올려 API 호출 가능 ISR 까지 마스크 */
shared_counter++;
taskEXIT_CRITICAL();

/* ISR 안에서 */
UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
shared_counter++;
taskEXIT_CRITICAL_FROM_ISR(saved);
```

- FreeRTOS Cortex-M 포트의 critical section은 PRIMASK가 아니라 **BASEPRI**를 쓴다. 그래서 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은(숫자가 작은) 인터럽트는 critical section 중에도 들어온다. 이것이 "zero-latency interrupt"다(7절).
- `vTaskSuspendAll()`/`xTaskResumeAll()`은 인터럽트는 허용하고 스케줄러만 멈춘다. 긴 작업에 쓰지만 그동안 block API를 부르면 안 된다.
- Zephyr: `unsigned int key = irq_lock(); ... irq_unlock(key);`(단일 코어), SMP 대응은 `k_spinlock_key_t key = k_spin_lock(&lock); ... k_spin_unlock(&lock, key);`, 스케줄러만 잠그려면 `k_sched_lock()`/`k_sched_unlock()`.

---

## 6. Priority inversion과 inheritance

### 6.1 문제 타임라인

```
 우선순위: H(높음) > M(중간) > L(낮음). L 과 H 가 mutex 로 I2C 버스 공유

 시간 --->
 L  ██[lock]████                              ████[unlock]
 H          ↑ 깨어남 [lock 시도 -> block]....................██ (드디어 실행)
 M               ↑ 깨어남 ████████████████████               (M 은 mutex 와 무관)
                         |<---- H 는 M 이 끝날 때까지 기다림 = unbounded inversion ---->|
```

H는 L을 기다리는 것까지는 정상(bounded)이다. 문제는 **M이 L을 선점**해서 L이 mutex를 못 놓는 것이다. M 같은 task가 여럿이면 H의 대기 시간에 상한이 없다.

### 6.2 해결책

| 방법 | 동작 | 장단점 |
|---|---|---|
| Priority inheritance | H가 mutex에서 block되면 L의 우선순위를 H로 임시 상승 → M이 L을 선점 못 함 → L이 빨리 unlock → 원래 우선순위 복귀 | FreeRTOS mutex, Zephyr `k_mutex` 기본 지원. 체인(중첩 mutex)에서는 복잡 |
| Priority ceiling (protocol) | mutex마다 "이 mutex를 쓰는 task 중 최고 우선순위"를 정해 lock 순간 그 우선순위로 올림 | deadlock까지 방지, 분석 쉬움. 수동 설정 필요 (FreeRTOS 기본 미지원) |
| 설계로 회피 | 공유 자원을 소유하는 **전담 task + queue**(예: I2C 서버 task) | 가장 견고. 지연이 늘어남 |
| critical section | 아주 짧은 구간이면 인터럽트/스케줄러를 잠깐 막음 | 길면 전체 응답성 악화 |

- 유명한 사례: 1997년 **Mars Pathfinder**. VxWorks에서 정보 버스 mutex를 둘러싼 priority inversion으로 watchdog reset이 반복되었고, 지상에서 mutex의 priority inheritance 옵션을 켜서 고쳤다.
- FreeRTOS 주의점: **binary semaphore에는 inheritance가 없다**. 자원 보호에 semaphore를 쓰면 이 문제가 그대로 생긴다. 또 mutex를 ISR에서 쓰면 안 된다.
- Zephyr: `CONFIG_PRIORITY_CEILING`은 inheritance로 올라갈 수 있는 **상한**을 정하는 옵션이다(priority ceiling protocol과는 이름만 비슷하다).

> **Don 경험과 연결**: "공유 버스(I2C/SPMI)를 여러 주체가 쓸 때 누가 오래 쥐면 급한 쪽이 굶는다"는 현상은 Apple에서 본 버스 arbitration·타임아웃 문제와 같은 구조다. 해결도 비슷하게 "소유자를 하나로 두고 요청을 큐로 받는 서버 구조"가 가장 튼튼하다고 말할 수 있다.

---

## 7. ISR에서 task로: deferred interrupt processing

### 7.1 원칙

ISR은 **짧게**: 하드웨어 원인 클리어 → 최소한의 데이터 이동 → task 깨우기 → 복귀. 긴 처리, block, printf, malloc, mutex는 모두 task에서 한다. 이를 top half / bottom half 또는 deferred interrupt processing이라 부른다.

### 7.2 Cortex-M 인터럽트 우선순위와 FreeRTOS 규칙

```
 NVIC priority (STM32, 4비트 구현, 숫자가 작을수록 높음)

  0  ┐
  1  │  "zero-latency" 영역: FreeRTOS critical section 으로도 마스크되지 않음
  2  │  -> FreeRTOS API (FromISR 포함) 호출 금지
  3  │  예: 모터 제어, 고속 타이밍 캡처
  4  ┘
 ---- configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 (<< 4 해서 0x50 이 BASEPRI 값) ----
  5  ┐
  .  │  FromISR API 호출 가능. critical section 중에는 대기
  .  │  예: UART, DMA, GPIO, I2S
 14  ┘
 15  <- SysTick, PendSV (configKERNEL_INTERRUPT_PRIORITY, 가장 낮음)
```

```c
/* FreeRTOSConfig.h (STM32 스타일 발췌) */
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
        (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }
```

- 흔한 버그: NVIC 우선순위를 설정하지 않은 인터럽트는 기본값 0(가장 높음)이다. 거기서 `xQueueSendFromISR()`를 부르면 커널 자료구조가 깨져 **가끔** 죽는다. `configASSERT`를 켜 두면 포트의 `vPortValidateInterruptPriority()`가 잡아 준다.
- STM32에서는 priority grouping을 "모두 preemption 비트"(`NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)` 또는 HAL의 `HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4)`)로 두라고 FreeRTOS 문서가 권장한다.
- **ISR에서는 이름이 FromISR로 끝나는 API만** 쓴다. 일반 API는 block할 수 있고 ISR 컨텍스트를 고려하지 않는다.

### 7.3 `xHigherPriorityTaskWoken`과 `portYIELD_FROM_ISR`

```c
void UART_RX_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;                     /* 반드시 pdFALSE 로 초기화 */
    uint8_t b = uart_read_byte();                   /* HW FIFO 읽기 (원인 클리어) */
    xStreamBufferSendFromISR(rx_stream, &b, 1, &woken);
    portYIELD_FROM_ISR(woken);                      /* 깨운 task 가 더 높으면 PendSV pend */
}
```

- FromISR 함수는 깨운 task가 현재 실행 중인 task보다 우선순위가 높으면 `woken`을 pdTRUE로 만든다.
- `portYIELD_FROM_ISR(pdTRUE)`는 PendSV를 pend한다. ISR이 끝나면 곧바로 높은 task로 간다. 빼먹으면 **다음 tick까지**(최대 1ms 이상) 늦게 실행된다. 기능은 동작하므로 발견하기 어려운 레이턴시 버그다.
- 한 ISR에서 여러 FromISR을 부르면 같은 변수를 계속 넘기고 마지막에 한 번만 yield한다.

### 7.4 Zephyr의 ISR과 bottom half

```c
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#define MY_IRQ      25           /* 칩별 IRQ 번호 (가상의 값) */
#define MY_IRQ_PRIO 2

static struct k_work rx_work;
static K_SEM_DEFINE(dma_done, 0, 1);

static void rx_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    /* thread 컨텍스트: block, 로그, mutex 모두 가능 */
}

static void my_isr(const void *arg)
{
    ARG_UNUSED(arg);
    /* HW 원인 클리어 ... */
    k_sem_give(&dma_done);       /* 특정 thread 깨우기 */
    k_work_submit(&rx_work);     /* 또는 system workqueue 에 일 넘기기 */
}

void my_driver_init(void)
{
    k_work_init(&rx_work, rx_work_handler);
    IRQ_CONNECT(MY_IRQ, MY_IRQ_PRIO, my_isr, NULL, 0);   /* 컴파일 타임 벡터 등록 */
    irq_enable(MY_IRQ);
}
```

- `IRQ_CONNECT(irq, priority, isr, arg, flags)`: 벡터 테이블에 빌드 타임으로 연결한다. 런타임 등록은 `CONFIG_DYNAMIC_INTERRUPTS`와 `irq_connect_dynamic()`.
- Zephyr에서도 커널 API를 부를 수 없는 **zero-latency IRQ**(`CONFIG_ZERO_LATENCY_IRQS`, flags에 `IRQ_ZERO_LATENCY`)가 있다. FreeRTOS의 "configMAX_SYSCALL 위 영역"과 같은 개념이다.
- `k_is_in_isr()`로 현재 ISR 컨텍스트인지 확인할 수 있다.
- 실제 드라이버에서는 대부분 `IRQ_CONNECT`를 직접 쓰지 않고 devicetree의 `interrupts` 속성과 드라이버가 처리한다.

---

## 8. Software timer

### 8.1 FreeRTOS: timer daemon task

```c
#include "FreeRTOS.h"
#include "timers.h"

static TimerHandle_t led_timer;

static void led_timer_cb(TimerHandle_t t)
{
    (void)t;
    /* timer daemon task 컨텍스트: 짧게, 절대 block 하지 않기 */
}

void led_timer_start(void)
{
    led_timer = xTimerCreate("led", pdMS_TO_TICKS(500), pdTRUE /* auto-reload */,
                             NULL, led_timer_cb);
    configASSERT(led_timer != NULL);
    xTimerStart(led_timer, 0);
}
```

- 콜백은 **timer daemon task**(`configTIMER_TASK_PRIORITY`, 스택 `configTIMER_TASK_STACK_DEPTH`)에서 돈다. 모든 타이머 콜백이 한 task를 공유하므로 콜백 하나가 block하면 다른 모든 타이머가 멈춘다.
- `xTimerStart()` 등은 daemon task에 **명령 queue**(`configTIMER_QUEUE_LENGTH`)로 전달된다. 두 번째 인자는 그 queue가 가득 찼을 때 기다릴 시간이다.
- ISR에서는 `xTimerStartFromISR()`, `xTimerResetFromISR()`. 버튼 debounce에 `xTimerResetFromISR()`가 유용하다(엣지마다 리셋 → 마지막 엣지 후 N ms에 콜백).
- `xTimerPendFunctionCallFromISR(fn, p1, p2, &woken)`: ISR에서 함수 실행을 daemon task로 미루는 범용 bottom half.

### 8.2 Zephyr: `k_timer`와 delayable work

| 항목 | `k_timer` | `k_work_delayable` |
|---|---|---|
| 콜백 컨텍스트 | **ISR** (system clock 인터럽트) | workqueue thread |
| 콜백에서 block | 불가 | 가능 (단, 같은 큐의 다른 일이 밀림) |
| 용도 | 정밀한 주기, 다른 thread 깨우기 | debounce, 재시도, 폴링, 타임아웃 처리 |
| API | `K_TIMER_DEFINE(t, expiry_fn, stop_fn)`, `k_timer_start(&t, K_MSEC(100), K_MSEC(100))`, `k_timer_status_sync()` | `K_WORK_DELAYABLE_DEFINE(w, fn)`, `k_work_schedule()`, `k_work_reschedule()`, `k_work_cancel_delayable()` |

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static void debounce_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_fn);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static void debounce_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    if (gpio_pin_get_dt(&btn) == 1) {
        /* 20ms 동안 안정적으로 눌림 -> 진짜 눌림 */
    }
}

/* GPIO 콜백 (ISR 컨텍스트) */
void btn_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
    k_work_reschedule(&debounce_work, K_MSEC(20));   /* 엣지마다 20ms 뒤로 미룸 */
}
```

`k_work_schedule()`은 이미 예약된 일은 그대로 두고, `k_work_reschedule()`은 기존 예약을 새 지연으로 바꾼다. debounce에는 reschedule이 맞다.

---

## 9. Zephyr workqueue 자세히

- **System workqueue**: 커널이 만드는 공용 thread(`k_sys_work_q`). 기본 우선순위 -1(cooperative), 스택은 `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE`. 많은 드라이버·서브시스템(BLE 포함)이 공유한다. **여기서 오래 block하면 시스템 전체가 느려진다.**
- **전용 workqueue**: 오래 걸리거나 block하는 일(flash 쓰기, 추론)은 자기 큐를 만든다.

```c
#include <zephyr/kernel.h>

#define AUDIO_WQ_STACK 4096
#define AUDIO_WQ_PRIO  3
K_THREAD_STACK_DEFINE(audio_wq_stack, AUDIO_WQ_STACK);
static struct k_work_q audio_wq;

struct audio_job {
    struct k_work work;
    const int16_t *pcm;
    size_t frames;
};
static struct audio_job job;

static void audio_job_fn(struct k_work *work)
{
    struct audio_job *j = CONTAINER_OF(work, struct audio_job, work);
    /* j->pcm, j->frames 처리 (feature 추출 등) */
    ARG_UNUSED(j);
}

void audio_wq_init(void)
{
    k_work_queue_init(&audio_wq);
    k_work_queue_start(&audio_wq, audio_wq_stack, K_THREAD_STACK_SIZEOF(audio_wq_stack),
                       AUDIO_WQ_PRIO, NULL);
    k_work_init(&job.work, audio_job_fn);
}

void audio_block_ready(const int16_t *pcm, size_t frames)   /* ISR 또는 thread 에서 */
{
    job.pcm = pcm;
    job.frames = frames;
    k_work_submit_to_queue(&audio_wq, &job.work);
}
```

- `CONTAINER_OF`: `struct k_work`를 더 큰 구조체에 넣고 핸들러에서 바깥 구조체를 되찾는 표준 패턴. 커널 객체에 사용자 데이터를 붙이는 방법이다.
- 같은 `k_work`를 처리 중에 다시 submit하면 **한 번만 큐에 들어간다**(이미 queued면 무시, running이면 끝난 뒤 다시). 그래서 위 코드처럼 job 하나에 데이터를 덮어쓰면 블록을 잃을 수 있다. 블록마다 job을 따로 두거나 `k_msgq`로 넘긴다.

---

## 10. 메모리: FreeRTOS heap_1~5와 정적 할당

### 10.1 FreeRTOS heap 구현 비교

| 파일 | 할당 | 해제 | 병합 | 특징 / 용도 |
|---|---|---|---|---|
| heap_1 | O | X | - | 부팅 때 만들고 절대 안 지우는 시스템. 결정적, 단편화 없음 |
| heap_2 | O | O | X (best fit) | 레거시. heap_4로 대체 권장 |
| heap_3 | O | O | (libc 따름) | 표준 `malloc`/`free`를 스케줄러 잠금으로 감쌈. 비결정적 |
| heap_4 | O | O | O (first fit + 인접 블록 병합) | 가장 흔한 선택 |
| heap_5 | O | O | O | heap_4 + **여러 개의 불연속 RAM 영역** (`vPortDefineHeapRegions()`로 첫 할당 전에 지정). 내부 SRAM + 외부 PSRAM 등 |

- 크기는 `configTOTAL_HEAP_SIZE`(heap_1/2/4). 모니터링은 `xPortGetFreeHeapSize()`, `xPortGetMinimumEverFreeHeapSize()`(heap_4/5).
- 할당 실패 hook: `configUSE_MALLOC_FAILED_HOOK 1` → `vApplicationMallocFailedHook()`.
- 양산 펌웨어의 흔한 정책: **부팅 때만 할당하고 이후 free 금지**(사실상 heap_1 또는 `configSUPPORT_DYNAMIC_ALLOCATION 0` + 전부 static). 필드에서 단편화로 죽는 일을 원천 차단한다.

### 10.2 Zephyr의 메모리

- 스레드 스택, 커널 객체는 대부분 `K_..._DEFINE` 매크로로 **정적**이다.
- 고정 크기 블록: `k_mem_slab`(C03의 I2S 버퍼). 결정적 O(1).
- 가변 크기: `k_heap`(`K_HEAP_DEFINE`, `k_heap_alloc(&h, size, K_NO_WAIT)`), 시스템 힙 `k_malloc()`은 `CONFIG_HEAP_MEM_POOL_SIZE`로 크기 지정.
- libc `malloc`은 `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE` 등 libc 설정에 따라 다르다(버전마다 다름).

### 10.3 스택 크기 정하기

1. 넉넉하게 시작한다(예: 2KB).
2. 모든 경로(에러 경로, 로그, 최악 입력)를 돌린 뒤 **high water mark**를 잰다.
3. 측정값 + 여유(20~30% 또는 수백 바이트)로 줄인다.
4. `printf`/`snprintf`(특히 float 포맷)와 큰 지역 배열이 스택을 크게 먹는다. 버퍼는 static으로 뺀다.
5. FPU를 쓰는 task는 컨텍스트 저장 공간(추가 약 100바이트 이상)을 더한다.

---

## 11. Stack overflow 감지

### 11.1 FreeRTOS

| 방법 | 설정 | 동작 | 한계 |
|---|---|---|---|
| Method 1 | `configCHECK_FOR_STACK_OVERFLOW 1` | 컨텍스트 스위치 때 SP가 스택 범위 밖인지 검사 | 스위치 사이에 넘쳤다 돌아오면 못 잡음 |
| Method 2 | `configCHECK_FOR_STACK_OVERFLOW 2` | 1 + 스택 끝 16바이트가 채움 패턴(0xA5)인지 검사 | 패턴을 건너뛰어 쓰면 못 잡음, 약간의 비용 |
| High water mark | `uxTaskGetStackHighWaterMark(h)` | 지금까지 가장 적게 남았던 양(**워드 단위**) | 사후 측정 |
| MPU / 스택 한계 레지스터 | FreeRTOS-MPU 포트, ARMv8-M `PSPLIM` | 하드웨어가 즉시 fault | 포트·코어 지원 필요 |

```c
#include "FreeRTOS.h"
#include "task.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    /* 스택이 이미 망가졌을 수 있다: 최소한만 하고 리셋 */
    crash_log_record("stack overflow", pcTaskName);   /* noinit RAM 에 기록 (가정한 함수) */
    NVIC_SystemReset();
}
```

- hook의 두 번째 인자 타입은 `char *`다. 이 hook 안에서 많은 일을 하면 망가진 스택 위에서 또 죽는다. **noinit RAM에 task 이름을 남기고 리셋** → 다음 부팅 때 telemetry로 올린다(C07, C10).

### 11.2 Zephyr

- `CONFIG_HW_STACK_PROTECTION=y`: MPU guard 영역(ARMv7-M) 또는 스택 한계 레지스터(ARMv8-M의 PSPLIM/MSPLIM)로 overflow 즉시 fault. 가장 권장.
- `CONFIG_STACK_SENTINEL=y`: 스택 끝에 sentinel 값을 두고 스위치 때 검사(소프트웨어 방식).
- `CONFIG_STACK_CANARIES=y`: 컴파일러 stack protector(함수 단위 버퍼 overflow 감지).
- `CONFIG_THREAD_ANALYZER=y`(+ `CONFIG_THREAD_ANALYZER_AUTO=y`): 주기적으로 thread별 스택 사용량과 CPU 사용률 출력. `k_thread_stack_space_get()`으로 코드에서도 확인.
- shell의 `kernel threads`, `kernel stacks` 명령(`CONFIG_KERNEL_SHELL`)이 가장 빠른 확인 방법이다.

---

## 12. Watchdog 전략

### 12.1 왜 "그냥 주기적으로 feed"는 틀렸나

idle task나 타이머 콜백에서 무조건 watchdog을 feed하면, 중요한 task 하나가 deadlock에 빠져도 다른 task가 살아 있으니 watchdog이 계속 먹는다. **시스템이 살아 있다 ≠ 모든 task가 제 할 일을 한다**.

### 12.2 Task check-in 패턴

```
  audio_task ---- set bit0 --+
  kws_task   ---- set bit1 --+--> [event group] --> supervisor_task:
  sensor_task --- set bit2 --+                         모든 비트가 기한 안에 켜졌나?
  ipc_task   ---- set bit3 --+                         YES -> HW watchdog feed + 비트 클리어
                                                       NO  -> feed 안 함 + 어떤 비트가 빠졌는지 noinit RAM 에 기록
                                                              -> HW watchdog 리셋 -> 다음 부팅 때 원인 보고
```

```c
#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#define WD_AUDIO   (1u << 0)
#define WD_KWS     (1u << 1)
#define WD_SENSOR  (1u << 2)
#define WD_IPC     (1u << 3)
#define WD_ALL     (WD_AUDIO | WD_KWS | WD_SENSOR | WD_IPC)

extern void hw_wdt_feed(void);                    /* HW WDT 타임아웃은 예: 4초 */
extern void crash_log_missing(EventBits_t missing);

static EventGroupHandle_t wd_group;

void wd_checkin(EventBits_t bit)                  /* 각 task 가 자기 루프 끝에서 호출 */
{
    xEventGroupSetBits(wd_group, bit);
}

void supervisor_task(void *arg)
{
    (void)arg;
    for (;;) {
        EventBits_t got = xEventGroupWaitBits(wd_group, WD_ALL,
                                              pdTRUE,     /* 조건 만족 시 비트 클리어 */
                                              pdTRUE,     /* 모두 필요 */
                                              pdMS_TO_TICKS(2000));
        if ((got & WD_ALL) == WD_ALL) {
            hw_wdt_feed();
        } else {
            crash_log_missing(WD_ALL & ~got);     /* feed 하지 않음 -> 곧 리셋 */
        }
    }
}
```

- 이벤트 기반 task(예: IPC는 메시지가 없으면 안 깨어남)는 block 타임아웃을 check-in 주기보다 짧게 잡아 "할 일 없음"도 check-in하게 만든다.
- supervisor 자신이 죽으면 feed가 멈추므로 자동으로 리셋된다.
- HW watchdog은 부팅 초기에 켜고, 디버거 halt 중에는 멈추도록(debug freeze 비트, 칩마다 다름) 설정한다.
- Zephyr는 이것을 **task watchdog 서브시스템**으로 제공한다: `CONFIG_TASK_WDT=y`, `task_wdt_init(hw_wdt_dev)`, `int ch = task_wdt_add(reload_ms, callback, user_data)`, 루프마다 `task_wdt_feed(ch)`. 하드웨어 watchdog 직접 사용은 `wdt_install_timeout()`, `wdt_setup()`, `wdt_feed()`(`<zephyr/drivers/watchdog.h>`).

---

## 13. Tickless idle과 저전력

### 13.1 왜 tick이 전력을 먹나

1kHz tick이면 할 일이 없어도 CPU가 1초에 1000번 깨어난다. 깨어날 때마다 클럭 복원, 코드 실행, 다시 sleep. always-on 기기에서 이것만으로 수십~수백 µA가 날아갈 수 있다(C05).

### 13.2 동작

```
 일반 tick:    |  |  |  |  |  |  |  |  |  |  |  |    (1ms 마다 SysTick wake)
 tickless:     |                             |       (다음 이벤트까지 한 번에 sleep)
               ^ idle 진입: "다음 timeout 까지 80ms" 계산
                 저전력 타이머(RTC/LPTIM)를 80ms 뒤로 설정, SysTick 정지, WFI/deep sleep
                                             ^ 깨어남 (타이머 또는 다른 인터럽트)
                                               실제 잔 시간만큼 tick count 보정
```

### 13.3 FreeRTOS 설정

- `configUSE_TICKLESS_IDLE 1`: 포트 내장 구현(Cortex-M은 SysTick 기반 `vPortSuppressTicksAndSleep()`). SysTick은 코어 클럭으로 돌기 때문에 **deep sleep에서 SysTick이 멈추는 MCU**에서는 얕은 sleep만 가능하다.
- `configUSE_TICKLESS_IDLE 2`: 앱이 `portSUPPRESS_TICKS_AND_SLEEP(xExpectedIdleTime)`를 직접 구현한다. RTC/LPTIM 같은 저전력 타이머로 깨어나야 하는 deep sleep은 보통 이것으로 한다. 벤더 SDK(ST, Ambiq, Silicon Labs 등)가 구현을 제공하는 경우가 많다(벤더마다 다름).
- `configEXPECTED_IDLE_TIME_BEFORE_SLEEP`(기본 2): 이보다 짧은 idle은 그냥 둔다.
- `configPRE_SLEEP_PROCESSING(x)` / `configPOST_SLEEP_PROCESSING(x)`: sleep 전후 주변장치 끄기/켜기 훅.
- `eTaskConfirmSleepModeStatus()`: sleep 직전에 그 사이 준비된 task가 없는지 확인.

### 13.4 Zephyr

- Zephyr는 `CONFIG_TICKLESS_KERNEL`이 대부분 보드에서 기본이다. nRF52 계열은 32.768kHz RTC(RTC1)가 system timer라 sleep 중에도 시간을 센다.
- idle thread가 `CONFIG_PM=y`일 때 PM 서브시스템에 "다음 이벤트까지 남은 시간"을 주고, PM이 devicetree의 `power-states`(`min-residency-us`, `exit-latency-us`)를 보고 가장 깊은 가능한 상태를 고른다.
- 장치별 전원은 device runtime PM(`CONFIG_PM_DEVICE_RUNTIME`, `pm_device_runtime_get()` / `pm_device_runtime_put()`)으로 "쓰는 동안만 켜기"를 구현한다.

> **Don 경험과 연결**: SSD의 power state(PS0~PS4, APST)와 구조가 같다. "진입 조건 = 예상 idle 시간 > 진입+복귀 비용"이라는 판단 규칙(`min-residency`)을 그대로 설명하면 된다.

---

## 14. Zephyr 개발 환경: west, Kconfig, devicetree, 로그

### 14.1 west 워크플로

```sh
# 설치 (Getting Started Guide 요약, macOS)
brew install cmake ninja gperf python3 ccache qemu dtc wget libmagic
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
# Zephyr SDK(툴체인) 설치는 가이드의 최신 절차를 따른다 (버전에 따라 west sdk install 제공)

# 빌드, 플래시, 설정
cd ~/zephyrproject/zephyr
west build -b nrf52840dk/nrf52840 samples/basic/blinky -p always
west flash                          # J-Link 러너 (nRF DK 온보드 디버거)
west build -t menuconfig            # Kconfig 대화형 편집
west build -t ram_report            # 심볼별 RAM 사용량
west build -t rom_report
```

- 보드 이름 형식이 Zephyr 3.7(HWMv2)부터 `nrf52840dk/nrf52840`이다. 이전 버전은 `nrf52840dk_nrf52840`.
- Nordic 칩은 실무에서 Zephyr 기반 **nRF Connect SDK(NCS)**를 많이 쓴다. 같은 west 흐름에 Nordic 전용 모듈이 추가된 것이다.

### 14.2 앱 구조와 prj.conf

```
my_app/
  CMakeLists.txt        # find_package(Zephyr) + target_sources(app PRIVATE src/main.c)
  prj.conf              # Kconfig 값
  app.overlay           # devicetree 수정 (또는 boards/<board>.overlay)
  src/main.c
```

```
# prj.conf 예시
CONFIG_GPIO=y
CONFIG_I2C=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
CONFIG_HW_STACK_PROTECTION=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_ASSERT=y
CONFIG_PM=y
```

```c
/* CMakeLists.txt 는 cmake 문법이라 여기서는 요지만:
 *   cmake_minimum_required(VERSION 3.20.0)
 *   find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
 *   project(my_app)
 *   target_sources(app PRIVATE src/main.c)
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("boot, uptime %lld ms", k_uptime_get());
    return 0;
}
```

- 로깅은 기본적으로 **deferred**(`CONFIG_LOG_MODE_DEFERRED`): 호출한 곳에서는 인자만 버퍼에 넣고 별도 thread가 출력한다. ISR에서도 부를 수 있고 타이밍 왜곡이 적다. 대신 크래시 직전 로그가 안 나올 수 있어 패닉 시 flush 설정을 본다.

### 14.3 Kconfig 한 줄 요약

devicetree는 "하드웨어가 무엇인가", Kconfig는 "어떤 소프트웨어를 넣나"(C03 10.3절). 최종 값은 `build/zephyr/.config`에서, 최종 devicetree는 `build/zephyr/zephyr.dts`에서 확인한다. "왜 이 드라이버가 안 들어갔지?"의 90%는 이 두 파일을 보면 풀린다.

---

## 15. FreeRTOS vs Zephyr 비교

| 항목 | FreeRTOS | Zephyr |
|---|---|---|
| 정체성 | **커널**(스케줄러 + IPC) 중심. 드라이버는 벤더 HAL | **OS 전체**: 커널 + 드라이버 모델 + BLE/네트워크 스택 + 파일시스템 + shell + 로그 + MCUboot 연동 |
| 라이선스 | MIT | Apache 2.0 |
| 설정 | `FreeRTOSConfig.h` 매크로 | Kconfig + devicetree |
| 빌드 | 자유 (Make, CMake, 벤더 IDE) | CMake + west (고정) |
| 우선순위 방향 | 클수록 높음 | 작을수록 높음, 음수 = cooperative |
| ISR API | 별도 `...FromISR()` + `portYIELD_FROM_ISR` | 같은 API + `K_NO_WAIT`, 스위치는 커널이 자동 |
| 정적 할당 | `xTaskCreateStatic` 등 선택 | 기본이 정적(`K_..._DEFINE`) |
| 메모리 보호 | FreeRTOS-MPU 포트 | userspace(사용자 모드 thread), MPU stack guard |
| SMP | 지원 (V11 통합) | 지원 |
| 저전력 | tickless idle (deep sleep은 포트/벤더 구현) | tickless 기본 + PM 서브시스템 + device runtime PM |
| 주 사용처 | STM32Cube, ESP-IDF(수정판), Ambiq AmbiqSuite, 다수 벤더 SDK, AWS IoT | Nordic nRF Connect SDK, NXP, Intel, Google 등. 멀티 벤더 보드 지원 |
| 학습 곡선 | 낮음 (API 작음) | 높음 (빌드 체계·devicetree) |
| 크기 | 커널 수 KB | 최소 구성 수 KB ~ 기능에 따라 수십~수백 KB |

어느 쪽을 고를지 질문을 받으면: "칩 벤더 SDK와 무선 스택이 어느 쪽에 붙어 있나"가 가장 큰 기준이다. Nordic BLE를 쓰면 Zephyr(NCS), Ambiq SDK 예제나 ESP32면 FreeRTOS가 자연스럽다. 하나의 기기 안에서 MCU별로 다를 수도 있다.

---

## 16. Rate Monotonic Scheduling 기초

### 16.1 RMS 규칙

주기 task들에서 **주기가 짧을수록 높은 우선순위**를 주는 고정 우선순위 배정이다. 독립적인 주기 task, deadline = 주기라는 가정 아래 고정 우선순위 방식 중 최적이다(RMS로 스케줄 불가능하면 다른 고정 우선순위 배정도 불가능).

**Liu & Layland 이용률 상한**: task n개의 총 이용률 U = Σ(Ci / Ti)가

```
 U <= n (2^(1/n) - 1)

 n=1 : 1.000
 n=2 : 0.828
 n=3 : 0.780
 n=4 : 0.757
 n->무한대 : ln 2 = 0.693
```

이면 **반드시** 스케줄 가능하다(충분 조건, 필요 조건 아님). 넘으면 응답 시간 분석으로 확인한다.

### 16.2 예제

| Task | 실행 시간 C | 주기 T | 이용률 | RMS 우선순위 |
|---|---|---|---|---|
| audio | 2 ms | 10 ms | 0.20 | 높음 |
| sensor | 5 ms | 50 ms | 0.10 | 중간 |
| ble | 10 ms | 100 ms | 0.10 | 낮음 |

U = 0.40 ≤ 0.780 → 스케줄 가능.

### 16.3 응답 시간 분석 (RTA)

task i의 최악 응답 시간 Ri는 자기 실행 시간 + 더 높은 우선순위 task들의 간섭이다.

```
 R_i = C_i + Σ_{j in hp(i)} ceil(R_i / T_j) x C_j      (고정점이 될 때까지 반복)

 ble: R0 = 10 + 2 + 5              = 17
      R1 = 10 + ceil(17/10)x2 + ceil(17/50)x5 = 10 + 4 + 5 = 19
      R2 = 10 + ceil(19/10)x2 + ceil(19/50)x5 = 19   -> 수렴. 19ms <= 100ms OK
```

실무 주의: 실제 시스템에는 ISR 시간, 커널 오버헤드, mutex blocking 시간(B_i)이 더해진다. `R_i = C_i + B_i + 간섭`. priority inheritance는 B_i를 bounded로 만드는 장치다. ML 추론처럼 실행 시간이 크고 가변적인 task는 가장 낮은 주기성 우선순위에 두고, 오디오 캡처처럼 deadline이 물리적으로 고정된 task를 높게 둔다.

---

## 17. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| FreeRTOS 스택 크기를 바이트로 착각 | 필요보다 4배 큰 RAM 사용 또는 Zephyr 이식 시 overflow | FreeRTOS는 word 단위, Zephyr는 byte | 매크로 이름에 단위 명시 (`_WORDS`, `_BYTES`) |
| 우선순위 0(기본) ISR에서 FromISR 호출 | 드물게 HardFault, 리스트 손상 | configMAX_SYSCALL 위 인터럽트가 커널 조작 | 모든 IRQ 우선순위 명시, `configASSERT` 켜기 |
| `portYIELD_FROM_ISR` 누락 | 기능은 되는데 레이턴시가 최대 1 tick | ISR 후 즉시 스위치 안 됨 | 모든 FromISR 뒤 yield |
| 자원 보호에 binary semaphore 사용 | 가끔 높은 task가 수십 ms 늦음 | priority inheritance 없음 | mutex 사용 또는 서버 task |
| 타이머 콜백/system workqueue에서 block | 다른 타이머·BLE 이벤트가 멈춤 | 공유 실행 컨텍스트 점유 | 전용 task/workqueue로 이동 |
| Zephyr `k_timer` 콜백에서 mutex/log 과다 | assert, 이상 동작 | 콜백이 ISR 컨텍스트 | `k_work` 제출로 변경 |
| idle에서 무조건 watchdog feed | deadlock인데 리셋 안 됨 | 살아 있는 task만 확인 | task check-in 패턴 |
| 높은 우선순위 task busy loop | 낮은 task·idle 굶음, 삭제 task 메모리 미회수, 전력 증가 | block 없이 polling | 이벤트로 block, 필요 시 `vTaskDelay` |
| `printf`를 여러 task에서 동시에 | 글자 섞임, 스택 overflow | 재진입·스택 과다 | 로그 task/deferred log, 스택 측정 |
| deep sleep에서 SysTick 기반 tickless | 기대보다 전류 높음 또는 시간 틀어짐 | SysTick이 sleep 중 정지 | 저전력 타이머 기반 `portSUPPRESS_TICKS_AND_SLEEP` |

---

## 18. 면접에서 이렇게 말한다

**Q.** Walk me through a context switch on Cortex-M.

**A.** 예외 진입 시 하드웨어가 R0–R3, R12, LR, PC, xPSR를 task의 PSP 스택에 자동 저장한다. 스케줄러가 스위치를 원하면 PendSV를 pend하는데, PendSV는 최저 우선순위라 중첩 ISR이 다 끝난 뒤 실행된다. PendSV는 R4–R11(FPU 사용 시 S16–S31)을 저장하고 PSP를 TCB에 기록한 뒤, 다음 task를 골라 반대로 복원하고 예외 복귀한다. 복귀하면서 하드웨어가 나머지를 pop해 새 task가 실행된다.

> "On exception entry the hardware stacks R0 to R3, R12, LR, PC and xPSR onto the task's process stack. When the kernel decides to switch — from SysTick or from an ISR that woke a higher-priority task — it pends PendSV, which runs at the lowest priority so it only fires once all nested interrupts are done. PendSV saves R4 to R11, plus the upper FPU registers if the task used the FPU, stores the PSP into the current TCB, picks the next task, restores its registers and PSP, and returns from exception so the hardware unstacks the rest."

**Q.** What's the difference between a mutex and a binary semaphore?

**A.** semaphore는 신호, mutex는 소유권이다. mutex는 take한 task만 give할 수 있고 priority inheritance가 있어 priority inversion을 bounded로 만든다. semaphore는 ISR에서 give할 수 있어 "DMA 완료" 같은 이벤트 알림에 쓴다. 자원 보호에 semaphore를 쓰면 inheritance가 없어서 위험하다.

> "A semaphore is a signal; a mutex is ownership. Only the task that took a mutex can give it back, and mutexes implement priority inheritance, which bounds priority inversion. A binary semaphore can be given from an ISR, so it's the right tool for 'the DMA finished' — but using one to protect a shared resource loses priority inheritance."

**Q.** Explain priority inversion and how you'd prevent it.

**A.** 낮은 task L이 mutex를 쥔 상태에서 높은 task H가 그 mutex를 기다리는데, 중간 task M이 L을 선점하면 H가 M 때문에 무한히 늦어지는 현상이다. Mars Pathfinder가 유명하다. priority inheritance mutex를 쓰거나, priority ceiling, 또는 자원을 전담 task와 queue로 소유하게 설계한다. 그리고 lock 구간을 짧게, lock 안에서 block하지 않는다.

> "Priority inversion is when a high-priority task blocks on a mutex held by a low-priority task, and a medium-priority task preempts the low one, so the high task effectively waits on an unrelated medium task — unbounded. Mars Pathfinder is the classic case. The fixes are a priority-inheritance mutex, a priority-ceiling protocol, or designing the resource to be owned by a single server task fed by a queue. And keep critical sections short with no blocking inside."

**Q.** How do you hand work from an ISR to a task in FreeRTOS?

**A.** ISR은 원인을 클리어하고 최소 데이터만 옮긴 뒤 FromISR API로 task를 깨운다. 받는 task가 하나고 데이터가 없으면 `vTaskNotifyGiveFromISR`, 데이터가 있으면 `xQueueSendFromISR`나 stream buffer를 쓴다. `xHigherPriorityTaskWoken`을 넘기고 끝에서 `portYIELD_FROM_ISR`를 불러 즉시 스위치되게 한다. 그리고 그 ISR의 NVIC 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 논리적으로 낮은지 확인한다.

> "The ISR clears the source, moves the minimum data, and wakes a task with a FromISR API — a direct-to-task notification if it's just a signal to one task, or xQueueSendFromISR or a stream buffer if there's data. I pass xHigherPriorityTaskWoken and call portYIELD_FROM_ISR at the end so the switch happens immediately rather than at the next tick. And I make sure that interrupt's NVIC priority is at or below configMAX_SYSCALL_INTERRUPT_PRIORITY."

**Q.** How would you design the watchdog for a multi-task firmware?

**A.** 하드웨어 watchdog은 supervisor task 하나만 feed한다. 각 중요 task는 자기 루프에서 check-in 비트를 세우고, supervisor는 기한 안에 모든 비트가 모였을 때만 feed한다. 빠진 비트는 noinit RAM에 기록해 리셋 후 telemetry로 보낸다. 이벤트 기반 task는 block 타임아웃으로 idle 상태에서도 check-in하게 한다.

> "Only a supervisor task feeds the hardware watchdog, and only when every critical task has checked in within its window — I use an event group or Zephyr's task watchdog. If a bit is missing, the supervisor records which task stalled in no-init RAM and lets the watchdog fire, so after reboot we report the culprit through telemetry. Event-driven tasks use a bounded block timeout so they check in even when idle."

**Q.** What is tickless idle and why does it matter on a wearable?

**A.** 할 일이 없을 때 주기적 tick 인터럽트를 멈추고 다음 timeout까지 저전력 타이머로 한 번에 자는 기능이다. 1kHz tick은 초당 1000번 깨우므로 always-on 기기의 평균 전류를 크게 올린다. deep sleep에서는 SysTick이 멈추므로 RTC 같은 저전력 타이머로 깨어나고 tick count를 보정해야 한다. Zephyr는 기본이 tickless이고 PM 서브시스템이 residency 조건으로 sleep 깊이를 고른다.

> "Tickless idle stops the periodic tick when nothing is ready and sleeps straight through to the next timeout on a low-power timer, then corrects the tick count on wake. A 1 kHz tick means a thousand wake-ups a second, which dominates the average current of an always-on device. In deep sleep SysTick usually stops, so you need an RTC-based implementation. Zephyr is tickless by default, and its PM subsystem picks the deepest state whose minimum residency fits the expected idle time."

**Q.** You know bare-metal well. Why should we trust you with RTOS work?

**A.** SSD 펌웨어에서 ISR과 작업 큐, 멀티코어 동기화, 우선순위 기반 디스패치를 직접 설계했다. RTOS는 그 패턴을 표준 API로 만든 것이고, 나는 그 아래에서 PendSV와 스택 프레임이 어떻게 동작하는지 알기 때문에 RTOS 버그(스택 overflow, 우선순위 설정 오류)를 더 빨리 잡는다. 최근 Zephyr와 FreeRTOS로 직접 프로젝트를 해 봤다(실제로 한 뒤에만 말할 것).

> "In SSD firmware I designed the pieces an RTOS gives you — interrupt-driven event handling, command queues, prioritized dispatch across cores, and the synchronization between them. An RTOS standardizes those patterns. Because I understand what's underneath — PendSV, the exception stack frame, interrupt priorities — I tend to find RTOS bugs like mis-set interrupt priorities or stack overflows quickly. I've also been building on Zephyr and FreeRTOS recently to get fluent in the APIs."

---

## 19. 직접 해보기

### 19.1 FreeRTOS on QEMU (보드 없이)

FreeRTOS 공식 저장소에 QEMU의 Arm MPS2(Cortex-M3) 데모가 있다.

```sh
git clone --recurse-submodules https://github.com/FreeRTOS/FreeRTOS.git
cd FreeRTOS/FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc
make
# 산출물 경로는 데모 README/Makefile 확인 (예: output/RTOSDemo.out)
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 \
    -kernel ./output/RTOSDemo.out -monitor none -nographic -serial stdio
```

해 볼 것:

1. `main.c`에서 blinky 데모(queue로 send/receive하는 두 task)를 선택해 동작을 읽는다.
2. 수신 task 우선순위를 송신 task보다 낮게 바꾸고 출력 순서가 어떻게 바뀌는지 본다.
3. L/M/H 세 task와 mutex로 priority inversion을 재현한다. mutex를 binary semaphore로 바꿔 H의 대기 시간이 늘어나는 것을 `xTaskGetTickCount()`로 측정한다.
4. `configCHECK_FOR_STACK_OVERFLOW 2`를 켜고 한 task의 스택을 일부러 작게 만들어 hook이 불리는지 본다.
5. QEMU에 `-s -S`를 붙이고 `arm-none-eabi-gdb`로 붙어 `xPortPendSVHandler`에 breakpoint를 걸어 컨텍스트 스위치를 한 단계씩 따라간다.

### 19.2 Zephyr on QEMU

```sh
cd ~/zephyrproject/zephyr
west build -b qemu_cortex_m3 samples/synchronization -p always
west build -t run            # 종료: Ctrl-a 다음 x
west build -b qemu_cortex_m3 samples/philosophers -p always
west build -t run
```

- `samples/synchronization`: 두 thread가 semaphore로 번갈아 출력. 우선순위와 `k_msleep` 값을 바꿔 본다.
- `samples/philosophers`: mutex/semaphore 등 여러 커널 객체로 dining philosophers. `prj.conf`의 옵션으로 객체 종류를 바꿀 수 있다.
- `CONFIG_THREAD_ANALYZER=y`, `CONFIG_THREAD_ANALYZER_AUTO=y`를 추가해 스택 사용량 출력을 본다.

### 19.3 nRF52840 DK + Zephyr: ISR → workqueue → thread

1. `samples/basic/button`을 빌드해 GPIO 인터럽트 동작을 확인한다.
2. 콜백에서 `k_work_reschedule()`로 20ms debounce를 넣는다(8.2절 코드).
3. debounce된 이벤트를 `k_msgq`로 별도 thread에 보내고, thread에서 LED 패턴을 바꾼다.
4. `CONFIG_KERNEL_SHELL=y`와 shell을 켜고 `kernel threads`, `kernel stacks`로 상태를 본다.
5. Nordic PPK2가 있으면 `CONFIG_PM`과 불필요한 로그/콘솔을 끄고 idle 전류를 측정해 tickless 효과를 확인한다(C05).

### 19.4 Task watchdog 실험

nRF52840 DK에서 `samples/subsys/task_wdt`를 빌드한다. 한 thread가 feed를 멈추게 코드를 바꾸고, 콜백과 리셋이 어떻게 일어나는지, 리셋 원인 레지스터(nRF의 RESETREAS, Zephyr `hwinfo_get_reset_cause()`)로 원인을 읽어 본다.

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Task / Thread | 독립 실행 흐름 | 자기 스택과 우선순위를 가짐. FreeRTOS=task, Zephyr=thread |
| TCB | Task Control Block | task 상태·스택 포인터·우선순위를 담은 구조체 |
| Preemption | 선점 | 높은 우선순위가 준비되면 즉시 CPU를 뺏음 |
| Cooperative thread | 협력형 | 스스로 양보하기 전엔 선점 안 됨 (Zephyr 음수 우선순위) |
| Time slicing | 시분할 | 같은 우선순위끼리 tick 단위 교대 |
| Tick | 시스템 시간 단위 | SysTick 등 주기 인터럽트. `configTICK_RATE_HZ` |
| PendSV | Pendable service call | 최저 우선순위 예외. 컨텍스트 스위치 수행 |
| PSP / MSP | Process/Main Stack Pointer | task용 / ISR·커널용 스택 포인터 |
| BASEPRI | 우선순위 마스크 레지스터 | 특정 우선순위 이하 인터럽트만 마스크 |
| Queue / msgq | 메시지 큐 | 값 복사로 데이터 전달 |
| Semaphore | 세마포어 | 카운트 기반 신호. ISR에서 give 가능 |
| Mutex | 상호 배제 | 소유권 + priority inheritance |
| Event group / k_event | 이벤트 비트 | 여러 조건의 AND/OR 대기 |
| Task notification | 직접 알림 | TCB 안의 32비트 값으로 가벼운 신호 |
| Priority inversion | 우선순위 역전 | 높은 task가 낮은 task 때문에 무한 대기 |
| Priority inheritance | 우선순위 상속 | mutex 보유자를 대기자 우선순위로 임시 상승 |
| FromISR | ISR용 API | block하지 않고 스위치 필요 여부를 반환 |
| Deferred interrupt processing | 지연 처리 | ISR은 신호만, 처리는 task/workqueue |
| Workqueue | 작업 큐 | `k_work`를 순서대로 실행하는 thread |
| Timer daemon | 타이머 서비스 task | FreeRTOS 소프트웨어 타이머 콜백 실행 task |
| Tickless idle | tick 없는 idle | 다음 이벤트까지 tick 멈추고 sleep |
| High water mark | 최대 사용 흔적 | 스택이 가장 적게 남았던 양 |
| RMS | Rate Monotonic Scheduling | 주기 짧을수록 높은 우선순위 |
| RTA | Response Time Analysis | 최악 응답 시간을 반복 계산 |
| west | Zephyr 메타 툴 | 저장소 관리 + build/flash/debug |
| Kconfig | 설정 시스템 | 빌드에 넣을 기능 선택 (`prj.conf`) |

---

## 21. 요약 & 체크리스트

RTOS는 "우선순위 기반 선점 스케줄러 + 커널 객체(queue, semaphore, mutex, event, notification) + 시간 관리"다. Cortex-M에서 스위치는 하드웨어 stacking과 최저 우선순위 PendSV로 일어나고, ISR은 FromISR API(FreeRTOS)나 `K_NO_WAIT` API(Zephyr)로 task를 깨운 뒤 빠르게 빠진다. 설계의 핵심 판단은 네 가지다. **데이터는 queue, 신호는 semaphore/notification, 자원은 mutex(또는 서버 task)**, 그리고 **우선순위는 deadline 순서(RMS)**. 양산 펌웨어에서는 여기에 정적 할당, 스택 overflow 감지, task check-in watchdog, tickless idle을 더해야 필드에서 죽지 않고 배터리를 아낀다. Zephyr는 같은 개념 위에 devicetree·Kconfig·west·workqueue·PM 서브시스템을 얹은 전체 OS이고, 우선순위 숫자 방향이 FreeRTOS와 반대라는 것을 잊지 않는다.

- [ ] FreeRTOS task 상태도와 Zephyr thread 상태의 차이를 그릴 수 있다
- [ ] Cortex-M 컨텍스트 스위치(자동 stacking, PendSV, PSP, EXC_RETURN)를 스택 그림과 함께 설명할 수 있다
- [ ] FreeRTOS/Zephyr/NVIC의 우선순위 숫자 방향을 틀리지 않고 말할 수 있다
- [ ] queue / semaphore / mutex / event group / task notification의 선택 기준을 예와 함께 말할 수 있다
- [ ] priority inversion 타임라인을 그리고 inheritance, ceiling, 서버 task 해결책을 비교할 수 있다
- [ ] `configMAX_SYSCALL_INTERRUPT_PRIORITY` 그림과 FromISR + `portYIELD_FROM_ISR` 코드를 쓸 수 있다
- [ ] Zephyr에서 ISR → `k_sem_give`/`k_work_submit`/`k_msgq_put` 패턴을 코드로 쓸 수 있다
- [ ] `k_timer`와 `k_work_delayable`의 실행 컨텍스트 차이를 설명할 수 있다
- [ ] heap_1~5 차이와 양산 펌웨어의 정적 할당 정책을 설명할 수 있다
- [ ] task check-in watchdog과 tickless idle 동작을 화이트보드에 그릴 수 있다
- [ ] RMS 이용률 상한과 응답 시간 분석을 3-task 예로 계산할 수 있다

## 참고 자료

- [FreeRTOS Kernel documentation](https://www.freertos.org/RTOS.html)
- [Mastering the FreeRTOS Real Time Kernel (공식 책, GitHub)](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book)
- [FreeRTOS on ARM Cortex-M: interrupt priority 설정](https://www.freertos.org/RTOS-Cortex-M3-M4.html)
- [FreeRTOS task notifications](https://www.freertos.org/RTOS-task-notifications.html)
- [FreeRTOS memory management (heap_1~5)](https://www.freertos.org/a00111.html)
- [FreeRTOS stack overflow checking](https://www.freertos.org/Stacks-and-stack-overflow-checking.html)
- [FreeRTOS low power / tickless idle](https://www.freertos.org/low-power-tickless-rtos.html)
- [FreeRTOS-Kernel source (port.c 포함)](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- [Zephyr Kernel Services](https://docs.zephyrproject.org/latest/kernel/services/index.html)
- [Zephyr Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html)
- [Zephyr Workqueue Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
- [Zephyr Interrupts](https://docs.zephyrproject.org/latest/kernel/services/interrupts.html)
- [Zephyr Power Management](https://docs.zephyrproject.org/latest/services/pm/index.html)
- [Zephyr Task Watchdog](https://docs.zephyrproject.org/latest/services/task_wdt/index.html)
- [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
- [Zephyr west (build, flash, debug)](https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html)
- [Arm Cortex-M4 Devices Generic User Guide (예외 모델, PendSV)](https://developer.arm.com/documentation/dui0553/latest/)
- [Liu & Layland, Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment (JACM 1973)](https://dl.acm.org/doi/10.1145/321738.321743)
