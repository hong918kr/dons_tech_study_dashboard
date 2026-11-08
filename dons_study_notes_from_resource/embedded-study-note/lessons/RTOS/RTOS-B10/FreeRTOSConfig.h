/* RTOS-B10 데모용 FreeRTOS 설정 — POSIX/GCC 포트 (macOS 프로세스 안에서 실행, 실시간성 없음)
 * -DDEMO_NO_ASSERT 로 빌드하면 configASSERT 를 끈다 (assert 없는 양산 빌드 흉내). */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdio.h>
#include <stdlib.h>
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      1000
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
#define configUSE_TICK_HOOK                     0
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
#define INCLUDE_xSemaphoreGetMutexHolder        1
#ifndef DEMO_NO_ASSERT
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )
#endif
#endif
