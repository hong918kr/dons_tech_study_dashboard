/* RTOS-M13 — FreeRTOS POSIX 포트(macOS) 설정 + "커널 경로를 찍는" trace 매크로.
 * ~/.esn_scratch/freertos-posix-mac 템플릿에서 시작해 이 레슨에 필요한 것만 바꿨다.
 *
 * trace 매크로는 FreeRTOS.h 가 "#ifndef traceXXX → 빈 매크로" 로 기본값을 주는 훅이다 (include/FreeRTOS.h
 * 631 traceTASK_SWITCHED_IN, 729 traceMOVED_TASK_TO_READY_STATE, 737/741 traceMOVED_TASK_TO_(OVERFLOW_)DELAYED_LIST,
 * 841 traceTASK_CREATE, 857 traceTASK_DELAY, 877 traceTASK_INCREMENT_TICK ...). 여기서 먼저 정의하면 그게 쓰인다.
 * 매크로는 tasks.c 안에서 펼쳐지므로 tasks.c 의 static 변수(xTickCount, pxCurrentTCB)와 그 함수의 지역 변수
 * (prvAddCurrentTaskToDelayedList 의 xTimeToWake)를 그대로 쓸 수 있다 — 커널 소스는 한 줄도 고치지 않는다.
 * m13_trace() 는 ISR(틱 시그널 핸들러)·임계 구역 안에서도 불리므로 printf 금지: 배열에 적기만 한다
 * (demo_freertos_trace.c).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* POSIX 포트는 generic(C) 선택만 */
#define configMAX_PRIORITIES                    5   /* 0 IDLE, 1 LogA/LogB, 2 Comm, 3 Ctrl, 4 Boot */
#define configTICK_RATE_HZ                      100  /* 10 ms tick: 호스트 스레드 깨우기 지연(가끔 1 ms 넘음)보다 충분히 길게 */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS /* POSIX 포트는 64비트 tick */
/* tick 카운터를 wrap 직전에서 시작한다 → 실행 중에 0 을 지나며 지연 리스트 두 개가 바뀐다.
 * (TickType_t)-30 은 64비트에서 0xFFFF_FFFF_FFFF_FFE2. tasks.c 506, 3799 에서 쓰인다. */
#define configINITIAL_TICK_COUNT                ( ( TickType_t ) -30 )
#define configMINIMAL_STACK_SIZE                PTHREAD_STACK_MIN
#define configTOTAL_HEAP_SIZE                   ( 256 * 1024 )
#define configMAX_TASK_NAME_LEN                 8
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        0   /* "Tmr Svc" 태스크를 만들지 않는다 → 생성 순서가 단순 */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_TRACE_FACILITY                0
#define configQUEUE_REGISTRY_SIZE               0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configASSERT( x ) do { if( !( x ) ) { fprintf( stderr, "[configASSERT] %s:%d\n", __FILE__, __LINE__ ); abort(); } } while( 0 )

/* ---------- trace: 이벤트 번호는 demo_freertos_trace.c 의 출력 토큰과 짝 ---------- */
void m13_trace( int ev, const char * name, long long tick, long long arg );
void m13_pause( void );
#define M13_TICK_NOW    ( ( long long ) xTickCount )   /* 64비트 unsigned → signed: -8, -7, ... (clang: 2의 보수) */

#define traceTASK_CREATE( pxNewTCB )                 m13_trace( 0, ( pxNewTCB )->pcTaskName, M13_TICK_NOW, 0 )
#define traceMOVED_TASK_TO_READY_STATE( pxTCB )      m13_trace( 1, ( pxTCB )->pcTaskName, M13_TICK_NOW, 0 )
#define traceTASK_SWITCHED_IN()                      m13_trace( 2, pxCurrentTCB->pcTaskName, M13_TICK_NOW, 0 )
/* tasks.c 4842: 이 매크로는 증가 "전" 값을 받는다 → +1 해서 "새 tick" 으로 적는다. */
#define traceTASK_INCREMENT_TICK( xOld )             m13_trace( 3, NULL, ( long long ) ( TickType_t ) ( ( xOld ) + 1U ), 0 )
#define traceTASK_DELAY()                            m13_trace( 4, pxCurrentTCB->pcTaskName, M13_TICK_NOW, 0 )
/* tasks.c 8745 / 8738: prvAddCurrentTaskToDelayedList 안에서 펼쳐진다 → 지역 변수 xTimeToWake 를 쓴다. */
#define traceMOVED_TASK_TO_DELAYED_LIST()            m13_trace( 5, pxCurrentTCB->pcTaskName, M13_TICK_NOW, ( long long ) xTimeToWake )
#define traceMOVED_TASK_TO_OVERFLOW_DELAYED_LIST()   m13_trace( 6, pxCurrentTCB->pcTaskName, M13_TICK_NOW, ( long long ) xTimeToWake )
#define traceTASK_SUSPEND( pxTCB )                   m13_trace( 7, ( pxTCB )->pcTaskName, M13_TICK_NOW, 0 )
/* tasks.c 3811: 첫 태스크의 traceTASK_SWITCHED_IN(3809) 바로 뒤, xPortStartScheduler 전(인터럽트 꺼짐).
 * 여기서 기록을 멈춰야 "첫 tick 이 언제 오나" 경쟁 구간이 trace 에 섞이지 않는다. */
#define traceSTARTING_SCHEDULER( xIdleTaskHandles )  m13_pause()

#endif /* FREERTOS_CONFIG_H */
