/* RTOS-B12 — FreeRTOS V11.3.2 POSIX 포트용 설정 (macOS 프로세스 안에서 실행)
 * ~/.esn_scratch/freertos-posix-mac/FreeRTOSConfig.h 템플릿에서 바꾼 줄:
 *   configUSE_TICK_HOOK = 1                  틱 ISR 안의 vApplicationTickHook() 을 "UART RX ISR" 대용으로 쓴다
 *   configUSE_DAEMON_TASK_STARTUP_HOOK = 1   데몬(타이머 서비스 태스크)이 처음 돌 때 자기 우선순위를 바꾼다
 *   configTIMER_* 세 줄에 설명 주석 (값은 템플릿과 같음: 우선순위 MAX-1, 큐 10칸)
 * 실시간성은 없다. 틱 번호·반환값·순서만 비교한다. */
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
#define configUSE_TICK_HOOK                     1   /* vApplicationTickHook = 틱 ISR 안 → "RX ISR" 대용 */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
/* ---- 소프트웨어 타이머 ---- */
#define configUSE_TIMERS                        1   /* timers.c 사용 + 스케줄러 시작 시 데몬 태스크 생성 (tasks.c 3758-3769) */
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 ) /* 데몬 우선순위. 데모는 startup hook 에서 바꾼다 */
#define configTIMER_QUEUE_LENGTH                10  /* 타이머 명령 큐 길이: 데몬이 처리 전까지 쌓일 수 있는 명령 수 */
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE /* 콜백은 이 스택에서 돈다 */
#define configUSE_DAEMON_TASK_STARTUP_HOOK      1   /* vApplicationDaemonTaskStartupHook (timers.c 753-761) */
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
#define INCLUDE_xTimerPendFunctionCall          1   /* xTimerPendFunctionCall(FromISR) 사용 */
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )
#endif
