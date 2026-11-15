/* RTOS-A06 — FreeRTOS POSIX 포트(macOS) 설정 + "rtos-trace v1 포맷으로 기록하는" trace 매크로.
 * ~/.esn_scratch/freertos-posix-mac 템플릿에서 시작해 이 레슨에 필요한 것만 바꿨다.
 *
 * trace 매크로는 include/FreeRTOS.h 가 "#ifndef traceXXX → 빈 매크로" 로 기본값을 주는 훅이다 (V11.3.2:
 * 631 traceTASK_SWITCHED_IN, 662 traceTASK_SWITCHED_OUT, 688 traceBLOCKING_ON_QUEUE_RECEIVE, 706 ..._SEND,
 * 729 traceMOVED_TASK_TO_READY_STATE, 789 traceQUEUE_SEND, 797 traceQUEUE_RECEIVE, 853 traceTASK_DELAY_UNTIL,
 * 857 traceTASK_DELAY, 877 traceTASK_INCREMENT_TICK). 여기서 먼저 정의하면 그게 쓰인다 — 커널 소스는 그대로.
 * 매크로는 tasks.c / queue.c "안에서" 펼쳐지므로 pxCurrentTCB(tasks.c static), pxQueue->uxQueueType
 * (queue.c 65 의 #define = pcHead, 뮤텍스면 NULL) 같은 내부 이름을 쓸 수 있다.
 * a06_*() 는 tick 시그널 핸들러(= 틱 ISR)와 임계 구역 안에서도 불린다 → printf 금지, 링에 적기만 (데모 파일).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* POSIX 포트는 generic(C) 선택만 */
#define configMAX_PRIORITIES                    6   /* 0 IDLE, 2 Log, 3 Comm, 4 Ctrl, 5 Report */
#define configTICK_RATE_HZ                      1000 /* 1 ms tick → 타임스탬프 = tick × 1000 µs + sub-tick */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS   /* POSIX 포트는 64비트 tick */
/* tick 을 4294667 에서 시작 → 32비트 µs 타임스탬프 (tick × 1000) 가 4294967296 을 300.296 ms 뒤에 넘는다.
 * 즉 600 ms 실행 중에 트레이스 타임스탬프가 한 번 wrap 한다 (디코더 wrap 처리를 진짜 데이터로 시험). */
#define configINITIAL_TICK_COUNT                ( ( TickType_t ) 4294667 )
#define configMINIMAL_STACK_SIZE                PTHREAD_STACK_MIN
#define configTOTAL_HEAP_SIZE                   ( 1024 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1   /* spi 뮤텍스: MUTEX_TAKE/GIVE + 우선순위 상속 */
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        0   /* Tmr Svc 없음 (tasks.c 5461: vTaskPlaceOnEventListRestricted 도 빠진다) */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1   /* vApplicationTickHook = 틱 ISR 안 = 이 데모의 "UART RX ISR" */
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                1   /* uxTaskGetSystemState */
#define configQUEUE_REGISTRY_SIZE               0
/* 커널 자체 실행 시간 통계: 트레이스와 "같은 시계" 를 쓰게 해 결과를 대조한다.
 * tasks.c 5231–5254: vTaskSwitchContext 가 traceTASK_SWITCHED_OUT 직후 이 값을 읽어 나가는 태스크에 더한다.
 * portmacro.h 145 의 기본값(ulPortGetRunTime = times() 의 tms_utime, 10 ms 단위)은 너무 거칠다 →
 * portALT_ 훅(tasks.c 5232 가 먼저 본다)으로 바꾼다. 값 = 첫 SWITCH_IN 기준 µs. */
#define configGENERATE_RUN_TIME_STATS           1
#define portALT_GET_RUN_TIME_COUNTER_VALUE( x ) ( ( x ) = a06_runtime() )
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )

/* ---------- trace 훅: demo_freertos_trace.c 가 구현 ---------- */
void a06_switch_out( const char * task );
void a06_switch_in( const char * task );
void a06_tick( unsigned long long new_tick );
void a06_block( const char * task, const char * why );            /* BLOCK <task> delay|delay_until */
void a06_ready( const char * task );                               /* UNBLOCK <task> */
void a06_queue( int op, void * obj, int is_mutex );                 /* op 0 recv-block 1 send-block 2 recv-ok 3 send-ok */
unsigned int a06_runtime( void );

#define traceTASK_SWITCHED_OUT()                    a06_switch_out( pxCurrentTCB->pcTaskName )
#define traceTASK_SWITCHED_IN()                     a06_switch_in( pxCurrentTCB->pcTaskName )
/* tasks.c 4842: 증가 "전" 값을 받는다 (스케줄러 정지 중이면 증가 없이 xPendedTicks++ 만) → +1 = 새 tick */
#define traceTASK_INCREMENT_TICK( xOld )            a06_tick( ( unsigned long long ) ( xOld ) + 1ULL )
#define traceTASK_DELAY()                           a06_block( pxCurrentTCB->pcTaskName, "delay" )
#define traceTASK_DELAY_UNTIL( xTimeToWake )        a06_block( pxCurrentTCB->pcTaskName, "delay_until" )
#define traceMOVED_TASK_TO_READY_STATE( pxTCB )     a06_ready( ( pxTCB )->pcTaskName )
/* queue.c 1615/1787 (수신 대기), 1125 (송신 대기), 1698 (세마포어/뮤텍스 획득), 979 (송신 = 뮤텍스 반납) */
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue )   a06_queue( 0, ( void * ) ( pxQueue ), ( pxQueue )->uxQueueType == queueQUEUE_IS_MUTEX )
#define traceBLOCKING_ON_QUEUE_SEND( pxQueue )      a06_queue( 1, ( void * ) ( pxQueue ), ( pxQueue )->uxQueueType == queueQUEUE_IS_MUTEX )
#define traceQUEUE_RECEIVE( pxQueue )               a06_queue( 2, ( void * ) ( pxQueue ), ( pxQueue )->uxQueueType == queueQUEUE_IS_MUTEX )
#define traceQUEUE_SEND( pxQueue )                  a06_queue( 3, ( void * ) ( pxQueue ), ( pxQueue )->uxQueueType == queueQUEUE_IS_MUTEX )

#endif /* FREERTOS_CONFIG_H */
