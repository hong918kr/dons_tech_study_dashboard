/* RTOS-M01: Cortex-M4F (ARM_CM4F 포트) 크기 측정용 최소 설정 — 링크/실행하지 않고 sizeof 만 본다 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#define configCPU_CLOCK_HZ                      80000000UL
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   ( 16 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS            1
#endif
#ifndef configTASK_NOTIFICATION_ARRAY_ENTRIES
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#endif
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY                0
#endif
#ifndef configUSE_QUEUE_SETS
#define configUSE_QUEUE_SETS                    0
#endif
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configPRIO_BITS                         4
#define configKERNEL_INTERRUPT_PRIORITY         ( 15 << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 5 << ( 8 - configPRIO_BITS ) )
#define configASSERT( x )                       do { if( !( x ) ) { for( ;; ) { } } } while( 0 )
#endif
