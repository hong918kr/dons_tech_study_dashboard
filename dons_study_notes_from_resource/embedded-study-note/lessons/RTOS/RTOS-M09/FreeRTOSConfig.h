/* RTOS-M09 — demo_freertos_overflow.c 용 FreeRTOSConfig.h (FreeRTOS-Kernel V11.3.2, POSIX/GCC 포트, macOS arm64).
 * RTOS-B07 의 설정을 바탕으로 스택 검사에 필요한 것만 바꿨다. 커널 .c 까지 이 헤더로 다시 컴파일해야 한다. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configNUMBER_OF_CORES                   1
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configIDLE_SHOULD_YIELD                 1
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS

#define configMINIMAL_STACK_SIZE                ( ( configSTACK_DEPTH_TYPE ) 256 ) /* 워드. POSIX 포트 StackType_t = 8 B → 2 KB */
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   ( 64 * 1024 )
#define configMAX_TASK_NAME_LEN                 12

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2   /* ★ 방법 2: 전환 때 saved SP + 스택 맨 아래 16 B 0xa5 (stack_macros.h:101-118) */
#define configRECORD_STACK_HIGH_ADDRESS         1   /* ★ TaskStatus_t.pxTopOfStack/pxEndOfStack 를 채운다 (tasks.c:6302-6305) */

#define configUSE_TRACE_FACILITY                1   /* vTaskGetInfo / TaskStatus_t */
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0
#define configQUEUE_REGISTRY_SIZE               0
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )

#define configUSE_MUTEXES                       0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#endif /* FREERTOS_CONFIG_H */
