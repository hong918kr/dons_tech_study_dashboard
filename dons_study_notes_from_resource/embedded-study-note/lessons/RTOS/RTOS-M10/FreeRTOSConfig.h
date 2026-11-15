/* RTOS-M10 — FreeRTOS POSIX 포트(macOS) 설정. ~/.esn_scratch/freertos-posix-mac 템플릿에서 힙 관련만 바꿨다:
 *  - configTOTAL_HEAP_SIZE 8192 : 모델과 같은 8 KB 아레나 (템플릿은 1 MB)
 *  - configAPPLICATION_ALLOCATED_HEAP 1 : ucHeap 을 데모가 직접 정의 → 주소(정렬)를 알고 오프셋을 비교할 수 있다
 *  - configUSE_MALLOC_FAILED_HOOK 1 : 실패마다 데모의 훅이 센다 (exit 하지 않음)
 *  - configUSE_TIMERS 0 : 타이머 태스크·큐가 힙을 건드리지 않게 (워크로드 전 힙 = 미사용)
 * 데모의 태스크는 xTaskCreateStatic, idle 은 configKERNEL_PROVIDED_STATIC_MEMORY → 커널은 힙을 쓰지 않는다. */
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
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                   ( 8192 )
#endif
#define configAPPLICATION_ALLOCATED_HEAP        1
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
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
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_TIMERS                        0
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )
#endif
