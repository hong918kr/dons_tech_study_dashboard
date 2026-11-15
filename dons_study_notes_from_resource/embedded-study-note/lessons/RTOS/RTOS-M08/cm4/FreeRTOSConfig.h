/* RTOS-M08: Cortex-M4F (ARM_CM4F 포트) 스택 분석용 최소 설정 — 링크/실행하지 않는다. -fstack-usage 로 .su 만 뽑는다.
 * 값은 STM32F4 급 MCU 의 흔한 설정 (NVIC 우선순위 비트 4개, 시스템콜 상한 5). */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#define configCPU_CLOCK_HZ                      168000000UL
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1          /* CLZ (RTOS-B04) */
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   ( 32 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TIMERS                        0
#define configCHECK_FOR_STACK_OVERFLOW          0          /* 1/2 면 전환 때 검사 코드가 PendSV 경로에 붙는다 (RTOS-M09) */
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define configPRIO_BITS                         4
#define configKERNEL_INTERRUPT_PRIORITY         ( 15 << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 5 << ( 8 - configPRIO_BITS ) )
#define configASSERT( x )                       do { if( !( x ) ) { for( ;; ) { } } } while( 0 )
#endif
