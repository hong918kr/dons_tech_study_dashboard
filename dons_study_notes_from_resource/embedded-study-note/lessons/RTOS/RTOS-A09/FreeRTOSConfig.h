/* RTOS-A09 — FreeRTOS POSIX 포트(macOS) 설정 + "RTOS-A06 포맷 트레이스를 쓰는" trace 매크로.
 * ~/.esn_scratch/freertos-posix-mac 템플릿에서 시작해 이 데모에 필요한 것만 남겼다.
 *
 * trace 매크로는 include/FreeRTOS.h 가 "#ifndef traceXXX → 빈 매크로" 로 기본값을 주는 훅이다 (V11.3.2:
 * 635 traceTASK_SWITCHED_IN, 666 traceTASK_SWITCHED_OUT, 694 traceBLOCKING_ON_QUEUE_RECEIVE,
 * 712 traceBLOCKING_ON_QUEUE_SEND, 730 traceMOVED_TASK_TO_READY_STATE, 790 traceQUEUE_SEND, 798 traceQUEUE_RECEIVE,
 * 854 traceTASK_DELAY_UNTIL, 878 traceTASK_INCREMENT_TICK). 여기서 먼저 정의하면 그게 쓰인다 — 커널 소스는 그대로.
 * tasks.c 안에서 펼쳐지는 매크로는 pxCurrentTCB 를 바로 쓴다. queue.c 안의 것은 큐 포인터만 넘기고
 * 데모 쪽(a09_q)이 "이 큐가 락인지 데이터 큐인지" 표를 보고 이벤트 이름을 정한다.
 * a09_* 는 틱 시그널 핸들러·임계 구역 안에서도 불리므로 printf 금지: 배열에 적기만 한다 (demo_freertos_deadline.c).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configMAX_PRIORITIES                    6     /* 0 IDLE, 1..4 앱, 5 감독(트레이스 덤프) */
#define configTICK_RATE_HZ                      100   /* 10 ms tick: 호스트 스레드 깨우기 지연보다 충분히 길게 */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configMINIMAL_STACK_SIZE                PTHREAD_STACK_MIN
#define configTOTAL_HEAP_SIZE                   ( 256 * 1024 )
#define configMAX_TASK_NAME_LEN                 16
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1     /* xSemaphoreCreateMutex = 우선순위 상속 */
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        0     /* 타이머 데몬 태스크 없음 → 트레이스가 단순 */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1     /* 틱 ISR 안에서 RELEASE / DEADLINE_MISS 판정 */
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                0
#define configQUEUE_REGISTRY_SIZE               0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskPriorityGet               1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )

/* ---------- trace → RTOS-A06 포맷 (demo_freertos_deadline.c) ---------- */
void a09_switch_out( const char * name );
void a09_switch_in( const char * name );
void a09_tick( uint64_t new_tick, void * cur_tcb );
void a09_q( int what, void * queue );            /* 0 받기 성공, 1 보내기 성공, 2 받기 블록, 3 보내기 블록 */
void a09_ready( const char * name );
void a09_delay_until( const char * name );

#define traceTASK_SWITCHED_OUT()                    a09_switch_out( pxCurrentTCB->pcTaskName )
#define traceTASK_SWITCHED_IN()                     a09_switch_in( pxCurrentTCB->pcTaskName )
/* tasks.c 4842: 증가 "전" 값을 받는다 → +1 = 이번 틱 번호. pxCurrentTCB = 방금 끝난 틱 동안 돌던 태스크 */
#define traceTASK_INCREMENT_TICK( xTickCount )      a09_tick( ( uint64_t ) ( xTickCount ) + 1U, ( void * ) pxCurrentTCB )
#define traceQUEUE_RECEIVE( pxQueue )               a09_q( 0, ( void * ) ( pxQueue ) )   /* queue.c 1545, 1698 */
#define traceQUEUE_SEND( pxQueue )                  a09_q( 1, ( void * ) ( pxQueue ) )   /* queue.c 979 */
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue )   a09_q( 2, ( void * ) ( pxQueue ) )   /* queue.c 1615, 1787 */
#define traceBLOCKING_ON_QUEUE_SEND( pxQueue )      a09_q( 3, ( void * ) ( pxQueue ) )   /* queue.c 1125 */
#define traceMOVED_TASK_TO_READY_STATE( pxTCB )     a09_ready( ( pxTCB )->pcTaskName )   /* tasks.c 289 */
#define traceTASK_DELAY_UNTIL( x )                  a09_delay_until( pxCurrentTCB->pcTaskName )  /* tasks.c 2472 */

#endif /* FREERTOS_CONFIG_H */
