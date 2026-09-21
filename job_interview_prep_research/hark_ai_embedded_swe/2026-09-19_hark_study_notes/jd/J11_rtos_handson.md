# J11. Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar)

> **분류**: Requirement 4/7 · **관련 개념 노트**: C04, C03, C05, S02
> **Don 현재 상태**: ❌ 갭 (이 JD에서 **가장 큰 갭**) — 레쥬메에 상용 RTOS 이름이 하나도 없다. bare-metal + SSD 자체 스케줄러가 전부다.
> **이 노트를 다 읽으면**: ① 면접관이 "hands-on"이라는 단어로 정확히 무엇을 검증하는지 안다 ② FreeRTOS/Zephyr의 실제 API 이름과 설정 상수를 근거로 말할 수 있다 ③ bare-metal 경험을 과장 없이 RTOS 언어로 번역하고, "안 써 봤다"를 강점으로 끝내는 스크립트와 1주 램프업 계획을 갖는다.

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| **Hands-on** | 읽어서 안 게 아니라 만져 봤다 | 이 단어가 이 문장의 전부다. "familiar with"도 "experience with"도 아닌 **hands-on**을 골랐다. 튜토리얼 수준이 아니라 **디버깅해 본 흔적**을 보겠다는 뜻 |
| experience with RTOS | 선점형 멀티태스킹 커널을 쓴 경험 | JD Responsibility 2번의 "RTOS task scheduling"과 짝이다. 즉 쓰는 것뿐 아니라 **스케줄 설계**까지 맡긴다 |
| FreeRTOS | Amazon(AWS) 관리 하의 MIT 라이선스 커널. 가장 널리 쓰임 | 벤더 SDK(Nordic, ST, NXP, Ambiq…)의 기본값인 경우가 많다 |
| Zephyr | Linux Foundation 프로젝트. Apache-2.0. 커널 + devicetree + 드라이버 모델 + 스택 전체 | 멀티 라디오·센서가 많은 웨어러블에는 Zephyr 쪽 생태계가 유리하다. nRF Connect SDK가 Zephyr 기반 |
| **or similar** | 특정 커널을 고집하지 않는다 | ThreadX, Mbed OS, RT-Thread, NuttX, embOS, 혹은 **사내 자체 커널**도 인정한다는 신호. Don에게 열려 있는 문이다 |
| (없는 단어) "production", "shipped" | 없다 | 출하 경험을 명시하지 않았다. 그러나 hands-on이 그걸 부분적으로 대신한다 |

핵심 해석: **"or similar"가 있으므로 Don의 SSD 자체 스케줄러 경험은 형식적으로 요건 안에 든다.** 다만 면접관이 FreeRTOS/Zephyr API를 물었을 때 침묵하면 거기서 끝난다. 그래서 전략은 두 갈래다 — ① 자체 커널 경험을 RTOS 개념어로 정확히 번역하고 ② FreeRTOS/Zephyr의 **API 수준 질문에 답할 수 있을 만큼**만 실제로 만져 둔다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

context 2.2/2.7절의 구조 추정(Qualcomm SoC + always-on 저전력 MCU, Ambiq 계열 [추정])에 근거한다.

- **[추정] 커널 선택 또는 상속**: 1세대 기기 팀이므로 RTOS가 아직 확정 안 됐을 수 있다(context 4.7절 역질문 4번이 정확히 이걸 묻는 이유다). 확정돼 있다면 Nordic/Ambiq SDK를 따라 Zephyr 또는 FreeRTOS다.
- **[추정] 매일 하는 일**: 센서 ISR → 큐 → 처리 task로 데이터를 흘리고, 오디오 DMA 완료를 task notification으로 깨우고, BLE 스택 task와 우선순위를 조율한다. 우선순위를 잘못 잡으면 오디오가 끊기거나 BLE 연결이 끊긴다.
- **[추정] 주 단위 일**: "왜 가끔 wake word 응답이 40ms 늦나"를 추적한다. 범인은 대개 우선순위 역전, 긴 critical section, workqueue 정체, 또는 tickless idle 진입/탈출 오버헤드다.
- **[추정] 설계 결정**: always-on 기기라 **tickless idle**과 **스택 크기**가 전력·메모리 예산에 직접 들어간다. task 8개에 스택 2KB씩이면 16KB — MCU RAM의 상당 부분이다.
- **[추정] 협업**: on-device AI 팀이 "추론이 30ms 걸린다"고 하면, 그 30ms 동안 오디오 DMA가 놓치지 않도록 우선순위와 버퍼 깊이를 설계하는 게 이 역할이다. 즉 RTOS 지식이 J05(모델 추론 예산)와 직결된다.
- **[추정] 왜 이 요건이 필수인가**: bare-metal 슈퍼루프로는 "오디오 + BLE + 센서 + 전원 관리 + OTA"를 동시에 못 굴린다. 팀이 이미 RTOS로 갔거나 갈 예정이라는 뜻이다.

---

## 2. 핵심 개념 — "hands-on"이 검증되는 지점

### 2.1 면접관의 판별 격자

| 신호 | 튜토리얼만 한 사람 | hands-on 한 사람 |
|---|---|---|
| API 이름 | "큐에 넣는다" | `xQueueSendFromISR(q, &item, &woken)` + `portYIELD_FROM_ISR(woken)` |
| ISR 규칙 | "ISR에서 API를 조심해서 쓴다" | "`configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 ISR은 **어떤** FreeRTOS API도 못 부른다" |
| 스택 | "넉넉히 잡는다" | "`uxTaskGetStackHighWaterMark()`로 실측하고 여유를 남긴다" |
| 디버깅 | "잘 동작했다" | "스택 오버플로로 랜덤 크래시가 나서 `configCHECK_FOR_STACK_OVERFLOW=2`로 잡았다" |
| 우선순위 | "우선순위를 정한다" | "우선순위 역전으로 오디오가 끊겼고 mutex로 바꿔 상속을 받았다" |
| 전력 | "sleep을 쓴다" | "tickless idle을 켰더니 idle 전류가 떨어졌지만 첫 tick 복귀 지연이 생겼다" |
| 실패담 | 없다 | **있다** — 이게 결정적이다 |

**면접관이 진짜로 찾는 것은 실패담이다.** RTOS를 실제로 쓴 사람은 반드시 한 번은 스택 오버플로, 우선순위 역전, ISR에서 잘못된 API 호출, 또는 큐가 가득 차서 데이터를 잃은 경험이 있다. 이 이야기가 없으면 "슬라이드로 배운 사람"으로 분류된다.

### 2.2 질문의 두 층 — API 레벨과 설계 레벨

```
 [API 레벨]  "How do you pass data from an ISR to a task?"
             → 정확한 함수 이름, FromISR 접미사, yield 규칙
             → 준비하면 확실히 얻을 수 있는 점수. 외워도 된다.

 [설계 레벨]  "Design the task structure for an always-on audio device."
             → 우선순위 배치, 버퍼 깊이, 실패 모드, 전력
             → Don이 오히려 유리한 층. bare-metal에서 이미 한 일이다.
```

Don의 전략: **API 레벨은 벼락치기로 메우고, 대화를 설계 레벨로 끌고 간다.** 설계 레벨에서는 커널 이름이 FreeRTOS든 자체 커널이든 논리가 같다.

### 2.3 RTOS가 실제로 주는 것 — 자체 커널과 비교해 정확히

| 기능 | RTOS가 주는 것 | bare-metal/자체 커널에서 Don이 했을 일 |
|---|---|---|
| 선점형 스케줄러 | 우선순위 기반 선점, 같은 우선순위는 라운드로빈(설정 시) | 협조적 상태 기계 또는 자체 tick 기반 디스패처 |
| 블로킹 대기 | `xQueueReceive(q, &v, portMAX_DELAY)` — CPU를 놓는다 | 폴링 루프 또는 플래그 검사. CPU를 안 놓음 |
| 우선순위 상속 mutex | 자동 | 직접 구현하거나, 애초에 공유를 피하는 설계 |
| 타이머 서비스 | software timer / delayable work | 자체 타이머 휠 또는 tick 콜백 테이블 |
| 스택 분리 | task마다 별도 스택 + overflow 검사 훅 | 단일 스택. 최악 깊이를 손으로 계산 |
| tickless idle | 커널이 다음 만료까지 계산해 sleep | 직접 wake 시점 계산 후 WFI |

이 표가 Don의 무기다. **"RTOS가 주는 것들을 나는 손으로 만들어야 했다"**가 정확하고 겸손하면서도 강한 프레이밍이다.

### 2.4 FreeRTOS — 반드시 아는 API 세트

객체 생성과 task:

```c
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

static void sensor_task(void *arg);

/* 동적 생성: pdPASS 를 확인한다. 스택 크기 단위는 '워드'다(바이트 아님). */
BaseType_t ok = xTaskCreate(sensor_task, "sensor",
                            512 /* words = 2KB on 32-bit */,
                            NULL, 3 /* priority */, NULL);
configASSERT(ok == pdPASS);
```

정적 생성은 `xTaskCreateStatic()`이고 `configSUPPORT_STATIC_ALLOCATION=1`이 필요하다. 양산 기기에서는 힙 단편화와 런타임 실패를 피하려고 정적 쪽을 선호한다.

| 범주 | 실제 API 이름 |
|---|---|
| Task | `xTaskCreate`, `xTaskCreateStatic`, `vTaskDelete`, `vTaskDelay`, `xTaskDelayUntil`, `vTaskSuspend`, `vTaskResume`, `vTaskPrioritySet`, `uxTaskGetStackHighWaterMark` |
| Queue | `xQueueCreate`, `xQueueCreateStatic`, `xQueueSend`, `xQueueSendToFront`, `xQueueReceive`, `xQueuePeek`, `xQueueOverwrite`, `uxQueueMessagesWaiting`, `xQueueSendFromISR`, `xQueueReceiveFromISR` |
| Semaphore / Mutex | `xSemaphoreCreateBinary`, `xSemaphoreCreateCounting`, `xSemaphoreTake`, `xSemaphoreGive`, `xSemaphoreGiveFromISR`, `xSemaphoreCreateMutex`(우선순위 상속 있음), `xSemaphoreCreateRecursiveMutex` |
| Event group | `xEventGroupCreate`, `xEventGroupSetBits`, `xEventGroupWaitBits`, `xEventGroupSync`, `xEventGroupSetBitsFromISR` |
| Task notification | `xTaskNotifyGive`, `ulTaskNotifyTake`, `xTaskNotify`, `xTaskNotifyWait`, `vTaskNotifyGiveFromISR`, `xTaskNotifyFromISR` |
| Software timer | `xTimerCreate`, `xTimerStart`, `xTimerStop`, `xTimerChangePeriod`, `pvTimerGetTimerID`, `xTimerStartFromISR` |
| Stream/Message buffer | `xStreamBufferCreate`, `xStreamBufferSend`, `xStreamBufferReceive`, `xMessageBufferCreate` |
| Critical section / yield | `taskENTER_CRITICAL`, `taskEXIT_CRITICAL`, `vTaskSuspendAll`, `xTaskResumeAll`, `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` |

**버전 주의**: `xTaskDelayUntil()`은 FreeRTOS V10.4.0에서 도입됐고 예전 이름 `vTaskDelayUntil()`은 호환용으로 남아 있다. 인덱스 기반 notification(`xTaskNotifyGiveIndexed` 등)도 V10.4.0에서 추가됐다. SMP(`configNUMBER_OF_CORES`)는 V11에서 커널에 들어갔다. **사용 중인 커널 버전에 따라 다름.**

필수 설정 상수:

| 상수 | 무엇 | 왜 중요 |
|---|---|---|
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 이 우선순위보다 **높은**(숫자가 작은) ISR은 FreeRTOS API 호출 금지 | 이걸 어기면 커널 자료구조가 깨진다. **가장 자주 나오는 면접 질문** |
| `configKERNEL_INTERRUPT_PRIORITY` | SysTick/PendSV가 쓰는 가장 낮은 우선순위 | 컨텍스트 스위치가 다른 ISR을 막지 않게 |
| `configASSERT` | 커널 내부 가정 검사 | **개발 중 반드시 켠다.** RTOS 버그의 절반을 여기서 잡는다 |
| `configCHECK_FOR_STACK_OVERFLOW` | 1=SP 범위 검사, 2=스택 끝 패턴 검사 | 2가 더 잘 잡는다. `vApplicationStackOverflowHook` 필요 |
| `configUSE_TICKLESS_IDLE` | tickless 저전력 | 배터리 기기에서 사실상 필수 |
| `configUSE_TRACE_FACILITY`, `configSUPPORT_STATIC_ALLOCATION` | `uxTaskGetSystemState` 활성화 / 정적 생성 허용 | CPU 사용률 측정, 양산 기기 권장 |

### 2.5 Zephyr — 반드시 아는 API 세트

Zephyr는 커널만이 아니라 **빌드 시스템(west/CMake) + 설정(Kconfig) + 하드웨어 서술(devicetree) + 드라이버 모델**이 한 덩어리다. 면접에서 Zephyr를 말하면 커널 API보다 **이 네 조각의 관계**를 먼저 묻는 경우가 많다.

```
 prj.conf (Kconfig)  ──┐
 app.overlay (DTS)   ──┤──> west build ──> CMake ──> zephyr.elf / zephyr.hex
 boards/<board>.dts  ──┘                     │
 src/main.c          ──────────────────────> └─ devicetree_generated.h
                                                (DT_ 매크로가 여기서 나온다)
```

| 범주 | 실제 API 이름 |
|---|---|
| Thread | `k_thread_create`, `K_THREAD_DEFINE`, `k_thread_start`, `k_thread_join`, `k_thread_priority_set`, `k_sleep`, `k_msleep`, `k_yield`, `k_is_in_isr` |
| 메시지 큐 / FIFO | `K_MSGQ_DEFINE`, `k_msgq_put`, `k_msgq_get`, `k_msgq_num_used_get`, `K_FIFO_DEFINE`, `k_fifo_put`, `k_fifo_get` |
| Semaphore / Mutex | `K_SEM_DEFINE`, `k_sem_take`, `k_sem_give`, `K_MUTEX_DEFINE`, `k_mutex_lock`, `k_mutex_unlock` (mutex는 우선순위 상속 있음) |
| Event / poll | `k_event_post`, `k_event_wait`, `k_poll`, `k_poll_signal_raise` |
| Workqueue | `k_work_init`, `k_work_submit`, `k_work_submit_to_queue`, `k_work_init_delayable`, `k_work_schedule`, `k_work_reschedule` |
| Timer | `K_TIMER_DEFINE`, `k_timer_init`, `k_timer_start`, `k_timer_stop`, `k_uptime_get` |
| 메모리 | `K_MEM_SLAB_DEFINE`, `k_mem_slab_alloc`, `k_heap_alloc`, `k_malloc` |
| 동기화(저수준) | `k_spinlock`, `k_sched_lock`, `k_sched_unlock`, `irq_lock`, `irq_unlock` |
| 인터럽트 | `IRQ_CONNECT`, `irq_enable`, `irq_disable` |
| Devicetree/드라이버 | `DEVICE_DT_GET`, `DT_NODELABEL`, `GPIO_DT_SPEC_GET`, `gpio_pin_configure_dt`, `device_is_ready` |
| 타임아웃 표기 | `K_MSEC(x)`, `K_SECONDS(x)`, `K_NO_WAIT`, `K_FOREVER` |

Zephyr 우선순위 규칙 — FreeRTOS와 **반대 방향이라 헷갈린다**:

| | FreeRTOS | Zephyr |
|---|---|---|
| 숫자 방향 | **큰 숫자 = 높은 우선순위** (0이 idle) | **작은 숫자 = 높은 우선순위** |
| 범위 | 0 ~ `configMAX_PRIORITIES-1` | 협조적: 음수(`-CONFIG_NUM_COOP_PRIORITIES` ~ -1), 선점형: 0 ~ `CONFIG_NUM_PREEMPT_PRIORITIES-1` |
| 협조적 스레드 | 별도 개념 없음 | 음수 우선순위 = 스스로 양보할 때까지 선점 안 됨 |

**이 방향 차이를 정확히 말하면 두 커널을 다 만져 본 사람으로 들린다.** 반대로 헷갈리면 즉시 들통난다.

주요 Kconfig:

| 옵션 | 무엇 |
|---|---|
| `CONFIG_MAIN_STACK_SIZE`, `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | main 스레드 / 시스템 workqueue 스택 |
| `CONFIG_THREAD_ANALYZER`, `CONFIG_THREAD_ANALYZER_AUTO` | 스레드별 스택 사용률 주기 출력 |
| `CONFIG_HW_STACK_PROTECTION`, `CONFIG_STACK_SENTINEL` | MPU 기반 스택 가드 / MPU 없을 때 패턴 검사 |
| `CONFIG_ASSERT`, `CONFIG_LOG`, `CONFIG_SHELL` | 커널 assert, 로깅, 셸 |
| `CONFIG_PM`, `CONFIG_PM_DEVICE` | 시스템/디바이스 전원 관리 |
| `CONFIG_TIMESLICING`, `CONFIG_TIMESLICE_SIZE` | 동일 우선순위 라운드로빈 |

### 2.6 두 커널 비교 — 무엇으로 고르나

| 축 | FreeRTOS | Zephyr |
|---|---|---|
| 범위 | 커널 중심(+ FreeRTOS-Plus 라이브러리) | 커널 + 드라이버 모델 + 네트워킹/BLE + 부트로더 연동까지 |
| 라이선스 | MIT | Apache-2.0 |
| 빌드 | 벤더 SDK/IDE에 파일로 들어감 | west + CMake + Kconfig + devicetree, 고정된 워크플로 |
| 하드웨어 추상화 | 벤더 HAL을 직접 씀 | Zephyr device driver API로 통일 |
| 학습 곡선 | 낮다. 며칠이면 돌린다 | 높다. devicetree가 진입 장벽 |
| 이식성 | 커널은 이식되지만 드라이버는 매번 다시 | 보드만 바꾸면 앱이 거의 그대로 |
| 메모리 | 더 작다 | 기능을 켤수록 커진다 |
| 언제 고르나 | MCU 하나, 주변장치 적고, 벤더 SDK가 이미 FreeRTOS | 라디오·센서 많고 보드 리비전이 자주 바뀌는 제품 |

Hark 맥락의 [추정] 답: **Zephyr가 유리하다** — 멀티 라디오(BLE/Wi-Fi) + 센서 다수 + 보드 리비전이 빠르게 도는 1세대 기기이고, MCUboot(OTA)와의 통합도 Zephyr 쪽이 정리되어 있다. 단, 팀이 Ambiq SDK를 쓰고 있다면 FreeRTOS가 기본일 수 있다. **면접에서는 "둘 중 뭘 쓰시나요"를 되묻는 게 정답에 가깝다.**

---

## 3. 실무 패턴과 함정

### 3.1 ISR → task 전달 (가장 많이 나오는 코드 질문)

FreeRTOS:

```c
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct { uint32_t ts; int16_t x, y, z; } sample_t;
static QueueHandle_t g_q;          /* xQueueCreate(16, sizeof(sample_t)) */

void SENSOR_IRQHandler(void)
{
    sample_t s;
    BaseType_t woken = pdFALSE;

    s.ts = read_timestamp();
    read_sensor_xyz(&s.x, &s.y, &s.z);

    /* ISR 안에서는 반드시 FromISR 계열. 블로킹 타임아웃 인자가 없다. */
    (void)xQueueSendFromISR(g_q, &s, &woken);

    /* 더 높은 우선순위 task가 깨어났다면 ISR 종료 시 바로 그 task로 간다.
       이 줄을 빼면 다음 tick 까지 최대 1 tick 지연된다. */
    portYIELD_FROM_ISR(woken);
}

static void sensor_task(void *arg)
{
    sample_t s;
    (void)arg;
    for (;;) {
        if (xQueueReceive(g_q, &s, portMAX_DELAY) == pdTRUE) {
            process(&s);
        }
    }
}
```

Zephyr(같은 일):

```c
#include <zephyr/kernel.h>

K_MSGQ_DEFINE(g_msgq, sizeof(sample_t), 16, 4);   /* 4 = 정렬 */

static void sensor_isr(const void *arg)
{
    sample_t s;
    ARG_UNUSED(arg);
    s.ts = k_uptime_get_32();
    read_sensor_xyz(&s.x, &s.y, &s.z);
    /* ISR 에서는 K_NO_WAIT 만 허용된다. 큐가 차 있으면 -ENOMSG. */
    (void)k_msgq_put(&g_msgq, &s, K_NO_WAIT);
}

static void sensor_thread(void *a, void *b, void *c)
{
    sample_t s;
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        if (k_msgq_get(&g_msgq, &s, K_FOREVER) == 0) {
            process(&s);
        }
    }
}
K_THREAD_DEFINE(sensor_tid, 1024, sensor_thread, NULL, NULL, NULL, 5, 0, 0);
```

차이점 세 가지를 말할 수 있으면 좋다: ① Zephyr는 `FromISR` 접미사가 없고 **`K_NO_WAIT` 강제**로 같은 규칙을 표현한다 ② Zephyr는 ISR 종료 시 스케줄러가 알아서 재평가하므로 명시적 yield 호출이 보통 필요 없다 ③ Zephyr는 정적 정의 매크로(`K_MSGQ_DEFINE`, `K_THREAD_DEFINE`)가 1급 시민이다.

### 3.2 가장 가벼운 신호 — task notification vs 세마포어

ISR이 단순히 "일 있음"만 알리면 되는 경우, FreeRTOS에서는 세마포어보다 **direct-to-task notification**이 더 빠르고 RAM도 덜 쓴다(별도 커널 객체가 필요 없다).

```c
static TaskHandle_t g_dma_task;

void DMA_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    clear_dma_flag();
    vTaskNotifyGiveFromISR(g_dma_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void dma_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* pdTRUE = 받은 뒤 카운트를 0으로 (binary semaphore 처럼) */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        handle_dma_complete();
    }
}
```

제약: notification은 **수신자가 한 task로 고정**이다. 여러 task가 기다리거나 브로드캐스트가 필요하면 세마포어/이벤트 그룹을 써야 한다. 이 트레이드오프를 말하면 실제로 써 본 사람으로 들린다.

### 3.3 우선순위 역전 — 이야기로 준비할 것

```
 시간 →
 High  |          [블로킹 — L이 쥔 mutex 대기]────────────[실행]
 Med   |      [실행 ─────────────────────────────]
 Low   |  [mutex 획득][선점당함 ...................][해제]
             ↑ M 이 L 을 선점하는 바람에 H 가 M 보다 오래 기다린다
```

해법: **mutex(우선순위 상속)**를 쓰면 L이 임시로 H의 우선순위를 물려받아 M에게 선점당하지 않는다. FreeRTOS는 `xSemaphoreCreateMutex()`에 상속이 들어 있고, `xSemaphoreCreateBinary()`에는 **없다**. Zephyr `k_mutex`도 상속이 있다.

상속이 고치지 못하는 것: ① ISR이 가로채는 지연 ② 데드락 ③ 여러 mutex를 잡는 사슬(chained blocking) ④ **세마포어로 자원 보호를 한 경우**. 이 네 가지를 덧붙이면 깊이가 드러난다.

### 3.4 스택 크기 — 실측 없이는 아무 말도 하지 않는다

| 방법 | FreeRTOS | Zephyr |
|---|---|---|
| 실측 | `uxTaskGetStackHighWaterMark(handle)` — 남은 최소 여유(워드 단위) | `CONFIG_THREAD_ANALYZER_AUTO=y` → 주기적으로 사용률 출력 |
| 자동 검출 | `configCHECK_FOR_STACK_OVERFLOW=2` + `vApplicationStackOverflowHook` | `CONFIG_HW_STACK_PROTECTION`(MPU 가드) 또는 `CONFIG_STACK_SENTINEL` |
| 정적 분석 | GCC `-fstack-usage`로 프레임 크기 수집 후 콜그래프 합산 | 동일 |

함정: **`xTaskCreate`의 스택 인자는 바이트가 아니라 워드다.** 32비트에서 512를 주면 2KB다. 이걸 바이트로 착각해 1/4 크기로 잡는 실수가 흔하다. Zephyr의 `K_THREAD_DEFINE` 스택 인자는 **바이트**다. 두 커널이 다르다.

또 하나: ISR은 task 스택이 아니라 **MSP(메인 스택)**를 쓴다. 중첩 인터럽트가 깊으면 task 스택은 멀쩡한데 MSP가 넘친다. FreeRTOS에서 MSP 크기는 링커 스크립트가 정한다.

### 3.5 tickless idle — 배터리 기기 필수

| | 동작 | 대가 |
|---|---|---|
| 일반 tick | 1ms마다 SysTick 인터럽트 → 매번 코어 깨어남 | idle 전류가 tick 주파수에 비례해 올라감 |
| tickless | 다음 타이머 만료까지 계산해 그만큼 저전력 타이머를 걸고 deep sleep | 복귀 시 tick 보정 필요, wake 지연이 약간 늘어남 |

FreeRTOS: `configUSE_TICKLESS_IDLE=1`(포트 제공 구현) 또는 2(사용자 구현 `portSUPPRESS_TICKS_AND_SLEEP`). Zephyr: `CONFIG_TICKLESS_KERNEL` — **최근 버전에서는 대부분의 보드에서 기본 활성화**이므로 "켰다"가 아니라 "확인했다"가 정확한 표현이다. **버전·보드마다 다름.**

함정: tickless를 켜도 **1ms마다 폴링하는 task가 하나만 있으면 아무 의미가 없다.** 전력 문제는 커널 설정이 아니라 task 설계에서 온다 — 이 문장이 면접에서 잘 먹힌다.

### 3.6 함정 모음

| 함정 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR 우선순위 규칙 위반 | 드물고 재현 안 되는 커널 크래시 | `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 ISR에서 API 호출 | `configASSERT` 켜기. FreeRTOS가 잡아 준다 |
| `portYIELD_FROM_ISR` 누락 | 응답이 최대 1 tick 늦음 | yield 안 함 | `xHigherPriorityTaskWoken` 전달 |
| 스택 인자 단위 혼동 | 부팅 직후 또는 특정 경로에서 크래시 | 워드 vs 바이트 | `uxTaskGetStackHighWaterMark`로 실측 |
| binary semaphore로 자원 보호 | 간헐적 지연, 오디오 끊김 | 우선순위 상속 없음 | mutex로 교체 |
| ISR에서 mutex take | 즉시 assert 또는 정의되지 않은 동작 | mutex는 소유자 개념이 있어 ISR에서 못 씀 | 세마포어/notification으로 |
| 큐가 가득 참을 무시 | 데이터 조용히 유실 | 반환값 미확인 | 반환값 검사 + 드롭 카운터 telemetry |
| 긴 critical section | 인터럽트 지연 급증 | 로그/printf를 critical 안에서 | 임계 구간 최소화, 로그는 밖에서 |
| Zephyr 우선순위 방향 착각 | 중요한 스레드가 안 돌아감 | 숫자 방향이 FreeRTOS와 반대 | 표(2.5)를 붙여 두고 리뷰 |
| heap 사용 후 malloc 실패 | 런타임에 조용히 실패 | 동적 생성 + 단편화 | 정적 할당, `vApplicationMallocFailedHook` |
| watchdog을 타이머에서 feed | 행이 걸려도 리셋 안 됨 | 모든 task가 살아 있는지 확인 안 함 | task별 check-in 후 모두 OK일 때만 feed |

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| FreeRTOS 공식 사이트 | 커널 개요, API 레퍼런스, 설정 상수 설명 | 각 API 페이지의 "Notes"에 ISR 사용 규칙이 있다 | https://www.freertos.org/ |
| FreeRTOS 커널 소스 + 릴리스 노트 | `tasks.c`, `queue.c`, 포트별 `port.c`(PendSV 구현), 버전별 변경 | `portable/GCC/ARM_CM4F/port.c`의 `xPortPendSVHandler` | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| Mastering the FreeRTOS Real Time Kernel (공식 무료 책) | 커널 개념의 정본 | 3장(task), 4장(queue), 6장(인터럽트), 7장(자원 관리) | https://www.freertos.org/Documentation/RTOS_book.html |
| Zephyr 공식 문서 | 커널 서비스, 빌드, devicetree, Kconfig 전체 | "Kernel Services"와 "Devicetree Guide" | https://docs.zephyrproject.org/latest/ |
| Zephyr Kernel Services | `k_sem`, `k_msgq`, `k_work`, `k_timer` 각 API와 ISR 사용 가능 여부 표 | 각 페이지의 "Concepts"와 API 표 | https://docs.zephyrproject.org/latest/kernel/services/index.html |
| Zephyr 스레드 우선순위 | 협조적/선점형, 음수 우선순위 규칙 | "Threads" 페이지의 Priorities 절 | https://docs.zephyrproject.org/latest/kernel/services/threads/index.html |
| Zephyr Getting Started | 툴체인 설치, west 초기화, 첫 빌드 | 전체(순서대로 따라 하면 된다) | https://docs.zephyrproject.org/latest/develop/getting_started/index.html |
| Zephyr Build/Flash/Debug | `west build`, `west flash`, `west debug`, 보드 지정법 | "Building" 절 | https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html |
| Zephyr Board Porting (hardware model v2) | **보드 이름 체계가 바뀐 근거 문서** | "Board terminology"와 board target 표기 | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Zephyr 릴리스 노트 | HWMv2 전환, `native_posix` → `native_sim` 등 | 해당 버전의 "Migration guide" | https://docs.zephyrproject.org/latest/releases/index.html |
| nRF Connect SDK 문서 | Zephyr 기반 Nordic 배포판. 실제 보드 실습 기준 | "Getting started"와 샘플 | https://docs.nordicsemi.com/ |
| Eclipse ThreadX / CMSIS-RTOS2 | "or similar"에 해당하는 대표 커널과 벤더 중립 래퍼 | README와 API 개요 | https://github.com/eclipse-threadx/threadx |
| MCUboot 문서 | Zephyr/FreeRTOS와 결합되는 부트로더. OTA(J04)와의 접점 | "Design" 문서 | https://docs.mcuboot.com/ |

**버전 의존 사실 — 반드시 표시하고 말할 것**
- **Zephyr 보드 이름 형식**: hardware model v2가 Zephyr 3.7에서 도입됐다. 예전 `nrf52840dk_nrf52840` → 현재 `nrf52840dk/nrf52840` 형태이고, 멀티코어 SoC는 `nrf5340dk/nrf5340/cpuapp`처럼 코어까지 붙는다. **어느 Zephyr 버전을 쓰느냐에 따라 다름** — 면접에서 말할 때 "버전에 따라 표기가 다릅니다"를 붙이는 게 정확하다.
- **`native_posix` → `native_sim`**: 호스트 시뮬레이션 보드 이름이 바뀌었다. 버전에 따라 다름.
- **`CONFIG_TICKLESS_KERNEL` 기본값**: 최근 버전에서 대부분 보드에 기본 활성화. **보드/버전마다 다름.**
- **FreeRTOS `vTaskDelayUntil` vs `xTaskDelayUntil`**: V10.4.0 이후 후자가 권장, 전자는 호환용.
- **FreeRTOS SMP**: V11부터 커널 본체에 통합. 그 이전에는 별도 브랜치/포트였다.
- FreeRTOS 공식 사이트는 문서 구조가 개편된 적이 있어 **딥링크가 깨질 수 있다**. 루트에서 검색하는 편이 안전하다.

---

## 5. 예상 면접 질문

난이도 표기: **[기초]** = 못 하면 탈락, **[중급]** = 실무 경험 확인, **[심화]** = 레벨 결정.

### Q01. [기초] Which RTOS have you used, and what did you build with it?

**왜 묻나**: 첫 질문이자 가장 위험한 질문. 여기서 얼버무리면 나머지가 다 의심받는다.

**30초 답변**: 정면으로 답한다 — 상용 RTOS로 제품을 출하한 적은 없고, 엔터프라이즈 SSD 펌웨어에서 사내 스케줄러 기반으로 멀티코어 task 스케줄링·ISR·동기화를 직접 다뤘다. RTOS가 제공하는 것들을 손으로 구현하는 쪽이었다. 최근에 Zephyr로 직접 만들어 본 것이 있다.

**English answer**: I'll be straight with you: I have not shipped a product on FreeRTOS or Zephyr. My production work is enterprise SSD firmware, which runs on an in-house scheduler across multiple ARM cores — so I have owned the things an RTOS gives you, but I built them rather than called them. Command queues between producers and consumers, deferring work out of interrupt context, synchronising access to shared state across cores, and budgeting a single stack by hand. Recently I've been working through Zephyr on a development board specifically because I wanted the API-level fluency to go with that, and I'm happy to talk about either layer.

**꼬리질문**
- "그 사내 스케줄러는 선점형이었나?" → `<확인 필요: SSD 펌웨어 스케줄러가 선점형인지 협조적인지>` 정확히 답할 것.
- "RTOS를 안 쓴 이유는?" → 결정적 지연과 오버헤드. 스토리지 컨트롤러는 마이크로초 단위 예산이라 커널 추상화 비용을 피했다.

### Q02. [기초] How do you pass data from an ISR to a task? Show me the calls.

**왜 묻나**: hands-on 여부를 API 이름 하나로 판별하는 질문. 준비하면 반드시 맞힐 수 있다.

**30초 답변**: FreeRTOS면 ISR에서 `xQueueSendFromISR(q, &item, &woken)`을 호출하고 끝에 `portYIELD_FROM_ISR(woken)`을 부른다. task는 `xQueueReceive(q, &item, portMAX_DELAY)`로 블로킹 대기한다. Zephyr면 `k_msgq_put(&q, &item, K_NO_WAIT)`와 `k_msgq_get(&q, &item, K_FOREVER)`다.

**English answer**: In FreeRTOS the ISR calls the FromISR variant — xQueueSendFromISR with a pointer to a BaseType_t called xHigherPriorityTaskWoken — and at the end of the handler I call portYIELD_FROM_ISR with that flag. If a higher priority task became ready, the context switch happens on exit from the interrupt instead of waiting up to a full tick. The task side is just xQueueReceive with portMAX_DELAY, which blocks and gives the CPU up. Zephyr expresses the same rule differently: there is no FromISR suffix, but you must pass K_NO_WAIT from interrupt context, so k_msgq_put with K_NO_WAIT going in and k_msgq_get with K_FOREVER coming out. And in both cases I check the return value, because a full queue is a real failure mode that should increment a dropped-sample counter, not disappear.

**꼬리질문**
- "`portYIELD_FROM_ISR`을 빼면?" → 최대 1 tick 지연. 오디오처럼 시간이 빡빡하면 치명적.
- "ISR에서 mutex를 잡을 수 있나?" → 없다. mutex는 소유자 개념이 있어 ISR에서 쓸 수 없다.
- "큐 대신 더 가벼운 방법은?" → task notification. 수신자가 한 task로 고정되는 제약이 있다.

### Q03. [기초] Mutex versus binary semaphore. When does the difference actually matter?

**왜 묻나**: 우선순위 역전으로 자연스럽게 이어지는 관문 질문.

**30초 답변**: mutex는 소유자가 있고 우선순위 상속이 붙는다. binary semaphore는 소유자가 없고 상속도 없다. 자원 보호에는 mutex, ISR→task 신호에는 semaphore. semaphore로 자원을 보호하면 우선순위 역전이 그대로 남는다.

**English answer**: A mutex has an owner and, in both FreeRTOS and Zephyr, priority inheritance. A binary semaphore is just a flag with no owner. The practical rule is that a semaphore signals an event — typically from an ISR to a task — and a mutex protects a resource. The difference matters the moment a low priority task holds the lock and a medium priority task preempts it while a high priority task is waiting: with a mutex the holder is temporarily boosted and finishes quickly; with a semaphore nothing boosts anything and your high priority task waits on the medium one. That is unbounded priority inversion, and on an audio path it shows up as a dropout rather than as an obvious bug.

**꼬리질문**
- "상속이 못 고치는 건?" → ISR 지연, 데드락, chained blocking, 여러 자원의 사슬.
- "priority ceiling은?" → 잡는 순간 정해진 상한으로 올린다. 상속보다 예측 가능하지만 우선순위를 미리 정해야 한다.

### Q04. [중급] What is configMAX_SYSCALL_INTERRUPT_PRIORITY and why does it exist?

**왜 묻나**: FreeRTOS를 실제로 설정해 본 사람만 답한다. **hands-on 판별의 단일 최강 질문.**

**30초 답변**: 커널이 critical section에서 BASEPRI로 막는 상한선이다. 이 값보다 높은 우선순위(숫자가 작은) ISR은 커널이 막을 수 없으므로 FreeRTOS API를 절대 호출하면 안 된다. 대신 그 ISR은 커널 지연 없이 동작한다.

**English answer**: FreeRTOS protects its internal data structures by writing BASEPRI, which masks every interrupt at or below a configured level. That level is configMAX_SYSCALL_INTERRUPT_PRIORITY. Interrupts above it are never masked by the kernel, so they get genuinely deterministic latency — but for exactly that reason they must not call any FreeRTOS API, not even a FromISR one, because the kernel cannot protect itself against them. The design intent is that you put your hardest real-time handler above the line and keep it pure hardware work, and everything that needs to talk to tasks sits below it. The failure mode when you get this wrong is nasty: rare, unreproducible corruption of a queue or task list. Turning on configASSERT catches most of it during development, which is why I would never ship a bring-up build with it off.

**꼬리질문**
- "Cortex-M0+에서는?" → BASEPRI가 없어 PRIMASK로 전부 막는다. 그래서 이 구분 자체가 성립하지 않는다.
- "Zephyr에도 같은 개념이 있나?" → `IRQ_CONNECT`로 등록한 일반 ISR은 커널 API 규칙을 따르고, zero-latency IRQ(`IRQ_ZERO_LATENCY` 플래그, 아키텍처가 지원할 때)가 비슷한 역할을 한다.

### Q05. [중급] How do you size a task stack, and how do you find out it overflowed?

**왜 묻나**: 실제로 디버깅해 본 사람만 구체적으로 답한다.

**30초 답변**: 처음엔 넉넉히 잡고 `uxTaskGetStackHighWaterMark()`로 실측해 줄인다. 검출은 `configCHECK_FOR_STACK_OVERFLOW=2`(패턴 검사) + 훅. Zephyr는 `CONFIG_THREAD_ANALYZER`로 사용률을 보고 `CONFIG_HW_STACK_PROTECTION`으로 MPU 가드를 건다. 정적으로는 `-fstack-usage`로 프레임 크기를 모아 콜그래프에서 최악 경로를 계산한다.

**English answer**: I never guess and leave it. I start generous, run the worst-case workload — including the deepest error path, which is the one people forget — and read uxTaskGetStackHighWaterMark to see the minimum free words ever observed, then trim with margin. For detection I set configCHECK_FOR_STACK_OVERFLOW to 2, which paints a pattern at the stack limit and checks it on every switch, and implement vApplicationStackOverflowHook to record which task died. On Zephyr I turn on CONFIG_THREAD_ANALYZER_AUTO for periodic usage reports and CONFIG_HW_STACK_PROTECTION so the MPU faults instead of silently corrupting a neighbour. One detail that bites people: the stack argument to xTaskCreate is in words, not bytes, while Zephyr's K_THREAD_DEFINE takes bytes. And interrupts run on the main stack, not the task stack, so a deep nested-interrupt path can blow MSP while every task stack looks fine.

**꼬리질문**
- "printf가 스택을 얼마나 먹나?" → 구현에 따라 수백 바이트에서 1KB 이상. newlib full printf는 특히 크다.
- "high water mark의 한계는?" → 실행되지 않은 경로는 반영이 안 된다. 그래서 정적 분석과 병행한다.

### Q06. [중급] Design the task structure for an always-on device: microphone, BLE, sensors, power management.

**왜 묻나**: **설계 레벨 질문. Don이 유리한 지점이다.** 커널 이름이 필요 없다.

**30초 답변**: 우선순위는 마감이 빡빡한 순서로. 오디오 DMA 완료 처리가 최상위, 그 아래 BLE 스택, 센서 융합, 그 아래 로깅/OTA/telemetry. ISR은 가능한 한 짧게 하고 실제 처리는 task로 넘긴다. 큐 깊이는 최악 burst에 소비자 지연을 곱해 정한다. 모든 블로킹 대기는 타임아웃을 갖고, watchdog은 task별 check-in으로 관리한다.

**English answer**: I'd assign priority by deadline tightness, not by importance. The audio path has the hardest deadline — if a DMA half-transfer callback is late by one buffer you lose samples permanently — so that sits at the top, and the handler itself does almost nothing: mark the buffer and signal. Below it the radio stack, because BLE connection events have timing requirements you cannot miss without dropping the link. Then sensor fusion, then the soft work: logging, telemetry, OTA download. Queue depth I size from the worst-case burst times the consumer's worst-case latency, with a drop counter so I find out in the field rather than guess. Every blocking wait gets a timeout except the one main loop of each task. And for the watchdog, each task checks in to a supervisor and the supervisor only kicks the hardware watchdog when all of them have — feeding it from a timer just proves the timer is alive. This is essentially the structure I already built by hand on the SSD side, where the command path and the background media-management path had exactly this kind of priority conflict.

**꼬리질문**
- "오디오와 BLE 우선순위가 충돌하면?" → 둘 다 하드 마감이므로 우선순위만으로 못 푼다. 버퍼를 깊게 잡아 지터를 흡수하거나, 라디오를 별도 코어/칩으로 분리한다.
- "task 개수는 몇 개가 적당한가?" → 스택 예산이 정한다. task마다 수 KB이므로 무작정 늘릴 수 없다. 이벤트 종류가 아니라 **마감이 다른 단위**로 나눈다.

### Q07. [중급] FreeRTOS or Zephyr for this device? How would you choose?

**왜 묻나**: 둘 다 아는지, 그리고 **기술 선택을 제품 근거로 하는지**를 본다.

**30초 답변**: 주변장치가 많고 보드 리비전이 자주 도는 제품이면 Zephyr — devicetree 덕에 보드가 바뀌어도 앱이 안 바뀌고, BLE/Wi-Fi 스택과 MCUboot가 같이 온다. 벤더 SDK가 이미 FreeRTOS이고 주변장치가 적으면 FreeRTOS가 빠르고 작다. 실제로는 벤더 SDK가 반 이상을 결정한다.

**English answer**: They solve different scopes. FreeRTOS is a kernel — you bring your own HAL, your own build, your own networking — and it is small, fast to learn, and already the default inside most vendor SDKs. Zephyr is a whole platform: kernel plus a device driver model, devicetree, Kconfig, and in-tree Bluetooth and networking stacks, with MCUboot integration for updates. For a first-generation consumer device with several radios and a board that respins every few weeks, I'd lean Zephyr, because devicetree means a board revision is an overlay change rather than a driver change, and that is where the schedule actually leaks. The cost is a steeper ramp and a larger image. Honestly though, the strongest input is which SDK the silicon vendor supports well — fighting a vendor's default costs more than either kernel's technical difference. Can I ask what you're on today?

**꼬리질문**
- "Zephyr의 devicetree가 실제로 뭘 해 주나?" → 핀·버스·인스턴스 정보를 빌드 타임에 생성해 준다. 앱 코드는 `DEVICE_DT_GET(DT_NODELABEL(...))`로 받는다.
- "둘 다 안 쓴다면?" → 코어가 하나이고 상태가 단순하면 슈퍼루프 + ISR로도 충분하다. RTOS는 공짜가 아니다.

### Q08. [심화] A rare audio glitch happens once every few hours. How do you find it?

**왜 묻나**: **hands-on의 진짜 증거는 이런 디버깅 이야기다.** Don의 강점 영역이기도 하다.

**30초 답변**: 먼저 재현 조건을 좁힌다. GPIO를 토글해 DMA 완료부터 처리 완료까지의 간격을 로직 애널라이저로 장시간 캡처하고, 임계값을 넘는 순간 트리거를 건다. 동시에 큐 드롭 카운터와 task별 최대 실행 시간을 계측한다. 원인 후보는 우선순위 역전, 긴 critical section, 높은 우선순위 ISR의 burst, 로깅/플래시 쓰기 같은 예기치 않은 블로킹이다.

**English answer**: Rare timing bugs are won by instrumentation, not by staring at code. I'd put a GPIO high when the DMA completion handler starts and low when the consumer finishes the buffer, then let a logic analyser run for hours with a width trigger set just above the budget, so the capture is centred on the actual failure instead of on an hour of healthy operation. In parallel I add cheap counters — dropped buffers, max observed latency per task, number of times each mutex blocked — because the counter that moved tells you where to look. The usual suspects in that order: priority inversion on a shared resource, a critical section someone lengthened, a high-priority interrupt bursting, and logging or a flash write blocking in a path nobody expected to block. This is the kind of problem I do most days now — at Apple I root-cause interface failures that only reproduce on some units, so the habit of designing the measurement before forming the theory is already how I work.

**꼬리질문**
- "왜 printf로 안 찾나?" → 타이밍을 바꿔 버린다. RTT나 GPIO가 훨씬 가볍다.
- "트레이스 도구는?" → SEGGER SystemView나 Percepio Tracealyzer가 task 전환과 ISR을 시각화한다. Zephyr는 트레이싱 서브시스템으로 연동한다.

### Q09. [심화] Explain what happens on a context switch on Cortex-M. Why PendSV?

**왜 묻나**: 커널을 쓰는 사람과 커널이 어떻게 도는지 아는 사람을 가른다. **Don에게 유리하다 — 코어 지식은 강하다.**

**30초 답변**: 예외 진입 시 하드웨어가 이미 8워드를 저장했으므로 커널은 R4~R11(+ FP 쓰면 S16~S31)만 저장하면 된다. 스위치는 가장 낮은 우선순위인 PendSV에서 한다. 그래야 다른 ISR이 처리되는 도중에 스택을 바꾸지 않고, 모든 ISR이 끝난 뒤 마지막에 한 번만 전환된다.

**English answer**: The hardware already stacked R0 to R3, R12, LR, PC and xPSR when the exception was taken, so the kernel's job is only to save the callee-saved registers — R4 through R11, plus S16 to S31 if the floating point context is live — store the stack pointer in the task control block, pick the next task, and restore. The reason it happens in PendSV specifically is priority: PendSV is set to the lowest priority, so it can only run when every other interrupt has finished. SysTick or a driver ISR can set the PendSV pending bit and return, and the switch happens once at the very end rather than in the middle of a nested interrupt sequence. It also means you never switch stacks while another handler is still using one. That's the same reasoning behind deferred interrupt processing in general — the interrupt decides that work is needed, something else does it.

**꼬리질문**
- "SVC는 왜 쓰나?" → 첫 task를 시작할 때, 그리고 비특권 코드에서 커널 서비스를 부를 때.
- "FPU가 켜져 있으면 스위치 비용은?" → lazy stacking 덕분에 FP를 안 쓴 task는 추가 비용이 없다. 쓰는 task만 32워드를 더 저장한다.

### Q10. [심화] How do you prove all tasks meet their deadlines?

**왜 묻나**: 실시간 이론까지 가는지 본다. 대부분 못 간다 — 여기서 차별화된다.

**30초 답변**: 이론 쪽은 rate monotonic 우선순위 배정과 이용률 한계, 더 정확히는 응답 시간 분석(RTA)으로 각 task의 최악 응답 시간이 마감보다 작은지 검사한다. 실무 쪽은 GPIO/트레이스로 최악 실행 시간을 실측하고, 여유(headroom)를 CI에서 회귀 감시한다. 이론만도 실측만도 충분하지 않다.

**English answer**: Two halves. Analytically, if the tasks are periodic I assign priorities rate-monotonically — shorter period gets higher priority — and then do response time analysis: each task's worst case response is its own execution time plus interference from every higher priority task over that window, iterated to a fixed point, plus blocking from any lower priority task holding a shared resource. If that number is under the deadline, the set is schedulable. Empirically, analysis is only as good as the execution times you feed it, so I measure worst-case execution with cycle counters or GPIO and a logic analyser under adversarial load, not average load. Then I track headroom as a number in CI, because the real failure is not a design that was never schedulable — it is one that was fine in March and drifted. I want to be honest that I have applied the empirical half far more than the formal half.

**꼬리질문**
- "인터럽트는 분석에 어떻게 들어가나?" → 어떤 task보다도 높은 우선순위의 간섭으로 들어간다. ISR의 실행 시간과 최대 빈도가 필요하다.
- "DVFS나 sleep이 있으면?" → 클럭이 바뀌면 실행 시간이 바뀐다. 최악 클럭 기준으로 분석해야 한다.

### Q11. [중급] What did you get wrong with an RTOS, and how did you find it?

**왜 묻나**: 2.1절에서 말한 **실패담 질문**. 이게 진짜 판별기다.

**30초 답변**: 솔직하게 — 상용 RTOS 실패담은 최근 Zephyr 학습 중 겪은 것으로, 그 전의 실제 제품 실패담은 자체 스케줄러 쪽에서 가져온다. 지어내지 않는다.

**English answer**: On the production side my failures are in the equivalent machinery rather than in a commercial kernel. The one I think about most is a shared-resource ordering problem in SSD firmware where a background path held something the latency-critical command path needed, and the symptom was a rare tail-latency spike rather than a functional failure — which is the worst kind, because the tests pass. We found it by instrumenting how long each acquisition blocked and looking at the maximum rather than the mean. Learning Zephyr recently I hit a much more ordinary one: my thread simply never ran, and the reason was that Zephyr's priority numbers go the opposite way from FreeRTOS — lower is higher, and negative values are cooperative. Small thing, but it is the kind of thing you only learn by having it bite you.

**꼬리질문**
- "tail latency를 어떻게 측정했나?" → `<확인 필요: SSD 펌웨어에서 지연 계측을 어떤 방식으로 했는가 — 하드웨어 타이머, telemetry, 호스트 측 측정>`
- "그 뒤 재발 방지는?" → 공유 자원 취득 시간을 지표로 만들고 회귀 감시.

### Q12. [중급] How does an RTOS help or hurt battery life?

**왜 묻나**: J03(저전력)과의 교차점. always-on 기기에서 반드시 나온다.

**30초 답변**: 돕는 쪽은 블로킹 대기 — task가 큐에서 기다리면 CPU를 완전히 놓으므로 idle에서 sleep으로 갈 수 있다. tickless idle이 주기적 tick 깨어남까지 없앤다. 해치는 쪽은 task가 많아 스택 RAM이 늘고 RAM retention 전류가 늘어나는 것, 그리고 불필요한 주기 task가 sleep을 잘게 쪼개는 것이다.

**English answer**: The help is structural: blocking on a queue or semaphore genuinely yields the CPU, so the idle task runs and the port can enter a low power state, which is much harder to get right in a superloop where everything polls. Tickless idle removes the periodic SysTick wake-up, which on a device that is asleep 99% of the time is the difference between microamps and hundreds of microamps. The harm is subtler. Each task costs a stack, and on a part where you pay retention current per retained SRAM block, eight tasks with generous stacks can move the sleep current. And an RTOS makes it easy to write a task that wakes every ten milliseconds for no good reason — the kernel will happily let you fragment sleep into pieces too short to reach the deep state. So the number I actually watch is not CPU utilisation, it is wake-ups per second and the distribution of sleep durations.

**꼬리질문**
- "sleep에서 wake까지 비용은?" → 클럭 안정화 시간과 컨텍스트 복원. 이게 sleep 구간보다 길면 손해다.
- "어떻게 측정하나?" → 전류 프로파일러(Nordic PPK2, Joulescope 등)로 파형을 보고 wake 이벤트를 센다.

### Q13. [심화] Design a watchdog strategy for a system with eight tasks.

**왜 묻나**: 필드 신뢰성 사고. OTA로 운영되는 기기라 중요하다.

**30초 답변**: 하드웨어 watchdog은 감독 역할 하나만 feed한다. 각 task는 자기 주기에 맞는 마감 안에 감독에게 체크인하고, 감독은 **모든** task가 체크인했을 때만 feed한다. 리셋 전에 어떤 task가 미체크인이었는지를 비휘발성 영역에 남기고, 부팅 후 telemetry로 보낸다.

**English answer**: Feeding the hardware watchdog from a timer callback only proves the timer works, which is the most common mistake. Instead one supervisor owns the hardware watchdog, and every task registers with a deadline appropriate to its own period — a sensor task at ten hertz has a much tighter deadline than an OTA task. Each task checks in from its main loop, the supervisor kicks the hardware only when all registered tasks are current, and if one is late the supervisor stops kicking and lets the reset happen. Crucially, before that reset I want evidence: write which task was delinquent, plus the fault context if there is one, into a RAM region that survives reset or a reserved flash record, and ship it with telemetry on the next boot. Otherwise every field reset looks identical and you learn nothing. Zephyr has a task watchdog subsystem that implements this pattern, and it is easy enough to build on FreeRTOS with a supervisor task and a bitmask.

**꼬리질문**
- "OTA 중에는?" → 플래시 쓰기가 길어 마감을 넘길 수 있다. 그 구간에는 마감을 늘리거나 채널을 일시 해제한다.
- "watchdog 리셋과 전원 리셋을 어떻게 구분하나?" → 리셋 원인 레지스터(벤더마다 이름 다름)를 부팅 시 읽어 기록한다.

### Q14. [심화] You inherit firmware where everything is in one 10ms superloop and it now misses deadlines. Migrate it.

**왜 묻나**: 실제 Hark 같은 팀에서 일어날 법한 일. 설계 판단과 리스크 관리를 본다.

**30초 답변**: 한 번에 전환하지 않는다. 먼저 현재 루프의 각 단계 실행 시간을 측정해 누가 마감을 먹는지 확인한다. 가장 시간이 빡빡한 것 하나만 높은 우선순위 task로 빼고 나머지는 하나의 낮은 우선순위 task 안의 기존 루프로 그대로 둔다. 그 상태로 안정화한 뒤 하나씩 뗀다. 각 단계마다 전류와 지연을 회귀 측정한다.

**English answer**: The risk in this migration is not technical, it's that you rewrite everything at once and then cannot tell which change broke which behaviour. So I'd do it incrementally. First measure: instrument each stage of the loop and find out where the time actually goes, because often one stage grew and the structure is fine. Then introduce the RTOS with exactly two tasks — one high priority task containing only the hardest-deadline work, and one low priority task that still runs the original superloop unchanged. That alone usually fixes the symptom and it is a small, reviewable diff. From there peel off one stage at a time, and after each step re-measure latency, stack high water marks and sleep current, because the RTOS changes all three. The thing I'd watch hardest is shared state: code that was implicitly safe because it was single-threaded is now genuinely concurrent, and that class of bug does not show up in the first week.

**꼬리질문**
- "공유 상태를 어떻게 찾나?" → 전역 변수를 열거하고 어느 task/ISR이 건드리는지 표로 만든다. 그다음 mutex나 메시지 전달로 바꾼다.
- "언제 RTOS를 안 넣는 게 맞나?" → 마감이 하나뿐이고 상태가 단순하면 인터럽트 + 이벤트 큐로 충분하다.

---

## 6. Don 매핑

근거는 context 3.1절의 레쥬메 문장만 사용한다. **RTOS는 이 JD에서 Don의 가장 큰 갭이다.** 이 절의 목표는 갭을 덮는 게 아니라 **정확히 그려 놓고 옆에 있는 강점을 보여 주는 것**이다.

### 6.1 지금 사실관계

| 항목 | 사실 |
|---|---|
| 상용 RTOS 출하 경험 | **없다** — 레쥬메에 FreeRTOS/Zephyr가 없다 (context TL;DR, 3.1) |
| 자체 스케줄러 경험 | 엔터프라이즈 SSD 펌웨어. bare-metal 기반, 멀티코어(Cortex-R8/R82/M0+, Xtensa) |
| 전이 가능한 것 | 멀티코어 task 분할, ISR/DMA, 동기화, command queue, 성능 튜닝, 에러 처리 |
| 레쥬메 기재 규칙 | context 3.3/3.5절 — Zephyr 샘플 실습 후에만 기재. **"RTOS는 사이드 프로젝트를 한 뒤에만 쓸 것"** |

**이 규칙을 지킨다.** 하지 않은 것을 레쥬메에 쓰면 Q02/Q04 같은 질문에서 무너지고, 그때 잃는 것은 이 요건 하나가 아니라 **모든 답변의 신뢰도**다.

### 6.2 bare-metal → RTOS 번역표

Don이 한 일을 RTOS 개념어로 바꾼다. **개념이 대응할 뿐 같은 것은 아니라는 단서를 반드시 붙인다.**

| Don이 한 일 (레쥬메 근거) | 대응하는 RTOS 개념 | 안전한 표현 |
|---|---|---|
| SSD 펌웨어의 command queue 처리 | queue / message passing, 생산자-소비자 | "the same producer-consumer shape an RTOS queue gives you" |
| 멀티코어(R8/R82/M0+) 간 작업 분할 | task 분할, 코어 간 통신 | "work partitioning across cores, which is the same design question as task partitioning" |
| ISR + DMA 데이터 경로 | deferred interrupt processing, ISR→task | "do almost nothing in the handler, hand it off" |
| 코어/경로 간 동기화 | mutex, semaphore, critical section | 자체 구현이었음을 명시 |
| "firmware performance tuning" | 응답 시간, 최악 지연 관리 | "worst-case latency, not average" |
| error reporting/handling 설계, NVMe telemetry | watchdog, fault telemetry, 필드 observability | Q08/Q11/Q13과 바로 연결 |

### 6.3 "안 써 봤다"를 말하는 전체 스크립트

**구조: ① 즉시 인정 → ② 내가 실제로 한 동등한 일 → ③ 최근에 실제로 만든 것 → ④ 램프업 속도 근거 → ⑤ 대화를 설계 레벨로 넘김.**

> "Short answer: I have not shipped a product on FreeRTOS or Zephyr, and I'd rather tell you that up front than have it come out three questions in.
>
> What I have done is the layer underneath. Enterprise SSD firmware runs bare-metal on an in-house scheduler across several ARM cores, and I owned pieces of that: splitting work between the latency-critical command path and background work, moving everything out of interrupt context into queued processing, synchronising shared state across cores, and budgeting stack by hand because there was no kernel to check it for me. When I read the FreeRTOS API, I am not learning the concepts — I am learning the names for things I had to build.
>
> I also didn't want to say that without doing something about it, so over the last week I've been working through Zephyr on a development board: west and devicetree, an interrupt feeding a message queue into a thread, the work queue, the thread analyzer for stack usage, and the current profile with tickless idle on and off. It's a week of hands-on, not a year — but it means I can talk about the API and not just the theory, and I know where the friction is.
>
> The honest read is that the API surface is a couple of weeks and the design judgement is the part that takes years, and that part I have. Where I'd want to lean on the team early is your specific conventions — priority assignment, how you handle the radio stack's timing, what you've standardised on for logging and watchdog.
>
> Can I ask which one you're on, and whether that was a deliberate choice or came with the SDK?"

왜 이게 통하나:
- 거짓이 없다. 검증 가능한 것만 말한다.
- "개념은 안다, 이름을 배우는 중"이라는 프레이밍이 정확하면서도 강하다.
- **최근 1주 실습**이 "말만 하는 사람"과 가른다. 이게 없으면 위 스크립트는 변명으로 들린다.
- 마지막 질문이 대화를 Don이 강한 설계 레벨로 옮긴다.

**하지 말 것**: "RTOS는 다 비슷하죠"라고 뭉개기, 한 번 돌려 본 샘플을 "경험"으로 부풀리기, API를 외워 온 티가 나도록 열거하기.

### 6.4 1주 Zephyr 램프업 계획 — 면접 이야깃거리를 만드는 것이 목표

전제: nRF52840 DK 같은 보드가 있으면 가장 좋다. 없으면 `native_sim`이나 QEMU 보드로 커널 부분은 다 할 수 있다(전력 측정만 못 한다).

**Day 1 — 환경과 첫 빌드**

```sh
# Zephyr 공식 Getting Started 순서를 따른다 (OS별 사전 패키지는 문서 참조)
pip install west
west init ~/zephyrproject
cd ~/zephyrproject && west update
west zephyr-export
pip install -r ~/zephyrproject/zephyr/scripts/requirements.txt
# SDK(툴체인) 설치는 문서의 Zephyr SDK 절을 따른다

cd ~/zephyrproject/zephyr
west build -p always -b nrf52840dk/nrf52840 samples/basic/blinky
west flash
```

보드 이름은 **Zephyr 버전에 따라 다르다**. 3.7 이전이면 `nrf52840dk_nrf52840`. 보드가 없으면 `-b native_sim` 또는 `-b qemu_cortex_m3`으로 바꾸고 `west build -t run`.

**Day 2 — 스레드, 큐, 동기화**
- `samples/synchronization`을 읽고 직접 다시 쓴 뒤, 3.1절의 ISR → `k_msgq` → 스레드 코드를 실제로 돌린다.
- 우선순위를 바꿔 가며 어느 쪽이 먼저 도는지 확인한다 — **Zephyr는 숫자가 작을수록 높다**는 것을 몸으로 확인.

**Day 3 — devicetree와 드라이버 모델**

```sh
west build -b nrf52840dk/nrf52840 samples/basic/button
west build -t menuconfig          # Kconfig 를 눈으로 본다
```

- `app.overlay`로 GPIO 핀을 바꾸고 **빌드 산출물의 `devicetree_generated.h`를 열어 본다** — 이걸 본 사람은 devicetree를 말할 수 있다. `DEVICE_DT_GET(DT_NODELABEL(...))` / `GPIO_DT_SPEC_GET`의 흐름을 따라간다.

**Day 4 — workqueue, timer, 로그**
- `k_work_delayable` + `k_work_schedule`로 디바운스를 구현하고, `CONFIG_LOG=y`, `CONFIG_SHELL=y`를 켜고 셸에서 상태를 조회한다.
- `CONFIG_THREAD_ANALYZER=y`, `CONFIG_THREAD_ANALYZER_AUTO=y`로 스택 사용률을 본다 → **"스택을 실측했다"는 Q05의 실증**.

**Day 5 — 저전력**

```sh
west build -b nrf52840dk/nrf52840 samples/boards/nordic/system_off   # 경로는 버전마다 다름
```

- `CONFIG_PM=y`로 켜고 끄며 평균 전류를 잰다(PPK2가 있으면 파형까지). **"tickless를 껐다 켜 봤다"는 Q12의 실증**. 1ms 폴링 스레드를 하나 넣어 보고 전류가 어떻게 망가지는지도 확인한다 → 3.5절 함정의 체험판.

**Day 6 — 고장을 일부러 낸다 (이 날이 가장 중요하다)**
- 스택을 일부러 작게 잡아 `CONFIG_HW_STACK_PROTECTION`이 잡는지 본다. 큐를 가득 채워 `k_msgq_put`이 `-ENOMSG`를 반환하는 것도 확인한다.
- 우선순위 역전을 재현한다: 낮은 우선순위 스레드가 `k_mutex`를 잡고, 중간이 CPU를 먹고, 높은 쪽이 기다리게. 그다음 `k_sem`으로 바꿔 차이를 관찰한다.
- **여기서 나온 관찰 3개가 Q11(실패담)의 재료다.**

**Day 7 — 정리와 스크립트화**
- 배운 것을 6.3 스크립트의 3번 문단에 구체적으로 채워 넣는다(무엇을 만들었고 무엇에 걸렸는지).
- FreeRTOS 쪽은 코드를 쓰지 않아도 2.4절 API 표와 `configMAX_SYSCALL_INTERRUPT_PRIORITY`(Q04), 그리고 두 커널의 **우선순위 방향 차이**(2.5절 표)는 반드시 말할 수 있게 한다 — 가장 싸게 얻는 신뢰도다.

산출물 목표: "Zephyr로 ISR→큐→스레드 파이프라인을 만들고, 스택 사용률을 실측하고, tickless 전후 전류를 비교하고, 우선순위 역전을 재현해 봤다" — **이 네 문장이면 면접에서 hands-on 대화를 할 수 있다.**

### 6.5 확인 필요

- `<확인 필요: SSD 펌웨어의 사내 스케줄러가 선점형이었는가, 협조적이었는가, 아니면 인터럽트 + 상태기계 구조였는가>` — Q01/Q02 답의 정확도가 여기 달렸다. 틀리게 말하면 꼬리질문에서 드러난다.
- `<확인 필요: 그 코드베이스에서 task/thread라는 용어를 썼는가, 아니면 다른 이름(context, agent, engine 등)이었는가>` — 실제로 쓴 용어로 말해야 자연스럽다.
- `<확인 필요: 멀티코어 간 동기화를 직접 구현했는가, 이미 있는 것을 사용했는가>` — 구현했다면 Q03/Q09에서 매우 강한 답이 된다.
- `<확인 필요: 지연/성능 계측을 어떤 도구로 했는가 — 하드웨어 타이머, 트레이스, 호스트 측 측정>` — Q08/Q10/Q11에서 구체성이 필요하다.
- `<확인 필요: Xtensa 쪽 작업에서 FreeRTOS 기반 SDK(ESP-IDF 계열 등)를 접한 적이 있는가>` — 있었다면 "FreeRTOS를 스쳐는 봤다"는 문장이 가능해진다. **없으면 절대 말하지 않는다.**
- `<확인 필요: 6.4 램프업을 실제로 수행했는가>` — 수행 전에는 6.3 스크립트의 3번 문단을 말하면 안 된다.

---

## 7. 준비 체크리스트

- [ ] 6.4의 1주 Zephyr 램프업을 **실제로 실행한다**. 이 노트에서 가장 중요한 항목이다
- [ ] 2.4의 FreeRTOS API 표에서 Task/Queue/Semaphore/Notification 4행을 **보지 않고** 말할 수 있게 한다
- [ ] `configMAX_SYSCALL_INTERRUPT_PRIORITY`를 60초 동안 설명해 본다 (Q04 — 단일 최강 판별 질문)
- [ ] FreeRTOS와 Zephyr의 **우선순위 숫자 방향**을 반대로 말하지 않는지 스스로 퀴즈한다
- [ ] 3.1의 ISR→task 코드를 두 커널 모두 **백지에서** 써 본다. 우선순위 역전 타임라인(3.3)도 그려 보고 상속이 고치지 못하는 4가지를 말한다
- [ ] 6.3의 솔직 스크립트를 영어로 90초 안에 말하도록 다듬는다. 3번 문단은 램프업 후 구체적 내용으로 교체
- [ ] 6.5의 `<확인 필요>` 6개를 본인 기억으로 채운다 — 특히 사내 스케줄러의 성격
- [ ] Q06(always-on 기기 task 설계)을 화이트보드에 그려 5분 안에 설명한다 — Don이 가장 유리한 질문
- [ ] Q11(실패담)의 답을 램프업 Day 6 관찰로 채운다. SSD 쪽 실패담도 1개 준비
- [ ] 역질문 준비: "FreeRTOS인가 Zephyr인가, 선택인가 SDK를 따라간 것인가" (context 4.7절 역질문 4번과 짝). 레쥬메 기재는 램프업 완료 후 "hands-on with Zephyr (personal project)" 수준의 정확한 표현으로만

---

## 8. 더 읽기

| 가고 싶은 곳 | 노트와 절 |
|---|---|
| 스케줄러·task 상태·우선순위 숫자 방향 | C04 §2 |
| 컨텍스트 스위치 내부 (PendSV 코드까지) | C04 §3, C01 §3, J10 §2.2 |
| task 생성(동적/정적)과 동기화 객체 선택 | C04 §4, §5 |
| 우선순위 역전 상세 | C04 §6 |
| ISR → task 전달과 `configMAX_SYSCALL_INTERRUPT_PRIORITY` | C04 §7 |
| Zephyr workqueue | C04 §9 |
| 메모리(heap_1~5), 스택 크기 산정, overflow 감지 | C04 §10, §11 |
| watchdog 전략 (task check-in 패턴 코드) | C04 §12 |
| tickless idle | C04 §13, C05 |
| west / Kconfig / devicetree 워크플로 | C04 §14 |
| FreeRTOS vs Zephyr 비교표 | C04 §15 |
| RMS와 응답 시간 분석 | C04 §16 |
| 직접 해 보기 (QEMU 포함) | C04 §19 |
| 같은 주제 면접 문답 | S02 Q15~Q24 (스케줄러, 역전, mutex, ISR 전달, 스택, watchdog, tickless, 정적 할당, 커널 선택, 마감 증명) |
| JD의 이웃 문장 | J02 (BSP + RTOS task scheduling — 업무 관점), J03 (저전력), J05 (추론 예산과 스케줄링), J10 (Cortex + 툴체인) |
