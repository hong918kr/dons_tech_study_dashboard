/* RTOS-M06 — FreeRTOS V11.3.2 POSIX 포트용 설정 (macOS 프로세스 안에서 실행)
 * RTOS-B11 의 설정과 같고 두 가지만 더했다:
 *  ① configUSE_TICK_HOOK 1 — 틱 훅(vApplicationTickHook)은 xTaskIncrementTick 안, 지연 리스트 처리 "뒤" 에
 *     불린다 (tasks.c:5006 vs 4871-4960). 데모 [3] 은 거기서 xQueueSendFromISR 를 불러 "타임아웃과 give 가 같은 tick" 을 만든다.
 *  ② traceBLOCKING_ON_QUEUE_RECEIVE — queue.c:1615 에서 vTaskPlaceOnEventList 직전에 불리는 trace 매크로.
 *     매크로라 xQueueReceive 의 지역 변수 xTicksToWait(= 이번에 블록할 "남은" 시간)를 그대로 볼 수 있다.
 *     (queue.c:1787 의 xQueueSemaphoreTake 에도 같은 이름의 지역 변수가 있어 그대로 컴파일된다.) */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdio.h>
#include <stdlib.h>
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      1000
#define INCLUDE_xTaskAbortDelay                 1
/* 블록할 때마다 기록: (큐, 남은 대기 tick). 정의는 demo_freertos_queue_wait.c */
void vDemoTraceBlockRx( void * pvQueue, unsigned long long ullTicksToWait );
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue )    vDemoTraceBlockRx( ( void * ) ( pxQueue ), ( unsigned long long ) ( xTicksToWait ) )
#endif
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                PTHREAD_STACK_MIN
#define configTOTAL_HEAP_SIZE                   ( 1024 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               8
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )
#define INCLUDE_xTaskAbortDelay                 1
/* 블록할 때마다 기록: (큐, 남은 대기 tick). 정의는 demo_freertos_queue_wait.c */
void vDemoTraceBlockRx( void * pvQueue, unsigned long long ullTicksToWait );
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue )    vDemoTraceBlockRx( ( void * ) ( pxQueue ), ( unsigned long long ) ( xTicksToWait ) )
#endif
