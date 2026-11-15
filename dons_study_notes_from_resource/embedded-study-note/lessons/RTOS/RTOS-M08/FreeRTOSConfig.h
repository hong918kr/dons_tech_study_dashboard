/* RTOS-M08 (RTOS-B07 의 설정을 그대로 복사, demo_freertos_hwm.c 용) — FreeRTOSConfig.h (FreeRTOS-Kernel V11.3.2, POSIX/GCC 포트, macOS arm64).
 *
 * FreeRTOS 는 "라이브러리 + 이 헤더" 다. 커널 소스(tasks.c 등)는 모두 #include "FreeRTOS.h" →
 * #include "FreeRTOSConfig.h" 로 이 파일을 읽어 컴파일 시점에 기능을 켜고 끈다. 그래서 이 파일을
 * 바꾸면 커널 .c 파일까지 전부 다시 컴파일해야 한다 (링크만 다시 하면 안 된다).
 * 줄마다 "무엇 / 왜 이 값 / 실제 MCU 라면" 을 적었다. 값의 근거는 include/FreeRTOS.h 의 기본값과
 * tasks.c 의 #if 를 직접 확인했다 (md 5. 풀이 Part A 표).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdio.h>   /* configASSERT 가 fprintf 를 쓴다 */
#include <stdlib.h>  /* configASSERT 가 abort 를 쓴다 */

/* ---------- 스케줄러 ---------- */
#define configUSE_PREEMPTION                    1   /* 1 = 선점형: 더 높은 우선순위가 READY 가 되면 즉시 전환. 0 = 협력형(RTOS-B01) */
#define configUSE_TIME_SLICING                  1   /* 같은 우선순위끼리 매 틱 돌아가며 실행(라운드 로빈, RTOS-B05). 우선순위가 다르면 상관없음 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* 0 = C 로 ready list 를 위에서부터 훑는다. Cortex-M 은 1 + CLZ 가능(RTOS-B04). POSIX 포트는 0 만 */
#define configNUMBER_OF_CORES                   1   /* 단일 코어 커널 (SMP 는 RTOS-A08) */
#define configTICK_RATE_HZ                      1000 /* 틱 1 ms. pdMS_TO_TICKS(10) == 10. POSIX 포트는 usleep(1000) 스레드가 SIGALRM 을 보낸다 */
#define configMAX_PRIORITIES                    7   /* 우선순위 0..6 (0 = idle). 큰 숫자가 급하다. 리스트 7개가 RAM 을 차지하니 필요한 만큼만 */
#define configIDLE_SHOULD_YIELD                 1   /* idle(우선순위 0) 과 같은 우선순위의 태스크가 READY 면 idle 이 바로 양보 */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS /* POSIX 포트의 TickType_t 는 unsigned long(64비트) 고정. Cortex-M 은 보통 32 */

/* ---------- 메모리 (스택·힙) ---------- */
#define configMINIMAL_STACK_SIZE                ( ( configSTACK_DEPTH_TYPE ) 256 ) /* idle 태스크 스택 깊이. 단위는 "워드"(StackType_t) — 여기선 8바이트 × 256 = 2 KB */
#define configSTACK_DEPTH_TYPE                  uint32_t /* xTaskCreate 의 스택 깊이 · HWM 의 타입. 기본(StackType_t)이어도 되지만 명시 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1   /* xTaskCreate 가 TCB + 스택을 pvPortMalloc 으로 할당 */
#define configSUPPORT_STATIC_ALLOCATION         0   /* xTaskCreateStatic 미사용 → idle 용 정적 메모리 콜백도 필요 없음 */
#define configTOTAL_HEAP_SIZE                   ( 64 * 1024 ) /* heap_1/2/4 의 정적 배열 크기. 이 레슨은 heap_3(malloc 래퍼)이라 실제로는 안 쓰인다 */
#define configMAX_TASK_NAME_LEN                 12  /* 이름 버퍼(널 포함). vTaskList 는 이름을 이 길이 - 1 칸으로 공백 패딩한다 */

/* ---------- 훅 (커널이 부르는 내 함수, hooks.c) ---------- */
#define configUSE_IDLE_HOOK                     1   /* idle 루프마다 vApplicationIdleHook() — 저전력 진입(WFI) · 워치독 킥 자리 */
#define configUSE_TICK_HOOK                     0   /* 틱 ISR 안에서 부르는 훅 (RTOS-B08 에서 ISR 대용으로 씀). 여기선 끔 */
#define configUSE_MALLOC_FAILED_HOOK            1   /* pvPortMalloc 이 NULL 이면 vApplicationMallocFailedHook() — 조용한 생성 실패 방지 */
#define configCHECK_FOR_STACK_OVERFLOW          2   /* 전환 때 스택 끝 16 바이트 0xa5 패턴 검사 → vApplicationStackOverflowHook(). POSIX 포트에선 사실상 발동 안 함(md 참고) */

/* ---------- 디버그 · 통계 ---------- */
#define configUSE_TRACE_FACILITY                1   /* TCB 에 번호 등 추가, uxTaskGetSystemState() 사용 가능 */
#define configUSE_STATS_FORMATTING_FUNCTIONS    1   /* vTaskListTasks()/vTaskList() 사용 가능 (TRACE_FACILITY 도 1 이어야 함). snprintf 의존 */
#define configGENERATE_RUN_TIME_STATS           0   /* 1 이면 태스크별 CPU 시간 통계 (타이머 필요). 호스트 시간은 의미 없어 끔 */
#define configQUEUE_REGISTRY_SIZE               0   /* 디버거용 큐 이름표 개수. 큐를 안 쓰니 0 */
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 ) /* 커널 내부 검사. MCU 에선 보통 taskDISABLE_INTERRUPTS(); for(;;); */

/* ---------- 쓰는 기능만 켠다 (안 쓰면 코드 크기 절약) ---------- */
#define configUSE_MUTEXES                       1   /* TCB 에 uxBasePriority 가 생긴다 → TaskStatus_t.uxBasePriority 가 의미를 가짐 */
#define configUSE_COUNTING_SEMAPHORES           0   /* 이 레슨에선 안 씀 (RTOS-B09) */
#define configUSE_TASK_NOTIFICATIONS            1   /* 워커를 깨우는 데 xTaskNotifyGive / ulTaskNotifyTake 사용 (상세는 RTOS-M01) */
#define configUSE_TIMERS                        0   /* 소프트웨어 타이머 끔 → "Tmr Svc" 태스크가 생기지 않는다 (RTOS-B12 에서 켬) */
#define INCLUDE_vTaskPrioritySet                1   /* 이 레슨의 주인공 */
#define INCLUDE_uxTaskPriorityGet               1   /* 현재 우선순위 읽기 */
#define INCLUDE_vTaskSuspend                    1   /* vTaskSuspend/Resume + portMAX_DELAY 가 "무한 대기" 가 됨 */
#define INCLUDE_vTaskDelete                     1   /* vTaskDelete (상태 'D') */
#define INCLUDE_vTaskDelay                      1   /* vTaskDelay */
#define INCLUDE_xTaskGetCurrentTaskHandle       1   /* xTaskGetCurrentTaskHandle */
#define INCLUDE_uxTaskGetStackHighWaterMark     1   /* 스택 여유(HWM) 읽기. 켜면 새 스택을 0xa5 로 채운다 (tasks.c 131) */
#define INCLUDE_eTaskGetState                   1   /* eTaskGetState */

#endif /* FREERTOS_CONFIG_H */
