/* RTOS-B06 — FreeRTOS V11.3.2 POSIX 포트용 설정 (macOS 프로세스 안에서 실행)
 * ~/.esn_scratch/freertos-posix-mac/FreeRTOSConfig.h 템플릿 (RTOS-B08 과 같은 뼈대) 에서 바꾼 것:
 *   configINITIAL_TICK_COUNT  = ULONG_MAX - 15 : tick 이 시작 16 tick 뒤에 0 으로 wrap 하게 한다 (FreeRTOS.h:2921)
 *                                POSIX 포트의 TickType_t 는 unsigned long = 이 맥에서 64비트 (portmacro.h:66)
 *                                → Cortex-M 의 32비트 tick 이면 0xFFFFFFF0 에 해당한다.
 *   configUSE_TIME_SLICING    = 0 : 같은 우선순위끼리 tick 마다 돌아가지 않게 → vTaskDelay(0) 의 yield 효과만 보인다
 *   configUSE_TICK_HOOK = 0, configUSE_TIMERS = 0 : 이 데모에 필요 없음 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define configTICK_RATE_HZ                      1000      /* 1 tick = 1 ms (호스트라 실제 주기는 정확하지 않다) */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                PTHREAD_STACK_MIN
#define configTOTAL_HEAP_SIZE                   ( 1024 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configINITIAL_TICK_COUNT                ( ULONG_MAX - 15UL )
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                1
#define configUSE_TIMERS                        0
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1      /* xTaskDelayUntil + xTaskPeriodicDelay */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )
#endif
