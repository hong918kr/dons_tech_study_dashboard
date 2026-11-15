/* RTOS-M13 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)에 trace 매크로를 꽂아
 * xTaskCreate → prvAddNewTaskToReadyList → prvAddTaskToReadyList, xTaskIncrementTick, vTaskSwitchContext 가
 * "실제로 어떤 순서로" 불리는지 찍는다. 매크로 정의는 이 폴더의 FreeRTOSConfig.h.
 *
 * 시나리오 (solution.c 의 모델이 같은 것을 재생해 글자 하나까지 같은 trace 를 내야 한다):
 *   main      : Boot(prio 4) 생성 → vTaskStartScheduler (IDLE 생성, 첫 태스크 = Boot)
 *   Boot      : 최고 우선순위로 tick 을 폴링하다가 tick == -6 (T0 = configINITIAL_TICK_COUNT = -30 에서 24 tick 뒤)
 *               이 되는 순간 기록 시작 → LogA(1), Ctrl(3), LogB(1), Comm(2) 생성 → vTaskSuspend(NULL)
 *   Ctrl (3)  : for(;;) vTaskDelay(4)   — 모터 제어 루프 (세 번째로 깨면 trace 를 출력하고 exit)
 *   Comm (2)  : for(;;) vTaskDelay(3)   — 통신 폴링
 *   LogA/LogB : 바쁜 루프 (같은 우선순위 1) — time slicing 을 보여 주는 로거 두 개
 * 왜 Boot 로 정렬하나: POSIX 포트의 tick 스레드는 시작하자마자 첫 SIGALRM 을 보낸다 (port.c 460-468:
 * pthread_kill 다음에 usleep). 그래서 첫 tick 이 첫 태스크의 첫 API 호출 전/후 어느 쪽에 올지 경쟁이다.
 * 처음엔 xTaskDelayUntil(&T0, 24) 로 정렬했는데, 호스트 부하가 클 때(load avg ~150) 깨어난 Boot 스레드가
 * 다음 tick 뒤에야 실제로 돌아 ~10% run 이 한 tick 밀렸다 (md 5. 풀이). 폴링하면 Boot 는 블록하지 않으므로
 * tick 처리(SIGALRM 핸들러)가 Boot 자신의 스레드 안에서 일어나고 스레드 깨우기가 끼지 않는다.
 * 기록은 traceSTARTING_SCHEDULER(→ m13_pause) 에서 끄고 정렬 순간 다시 켠다.
 * 그래도 정렬이 실패하면(본 tick != -6) 숨기지 않고 "SYNC FAILED" 와 exit 2.
 *
 * 빌드 (md 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_trace.c \
 *      $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M13_dt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#define MAXEV 512
typedef struct { int ev; const char *name; long long tick, arg; } ev_t;
static ev_t g_ev[MAXEV];
static volatile int g_nev;
static volatile int g_rec = 1;                 /* main 의 생성 과정부터 기록 */

void m13_pause( void ) { g_rec = 0; }
static volatile unsigned long g_spin[2];

/* 틱 시그널 핸들러·임계 구역 안에서도 불린다 → 배열에 적기만 (printf 금지). */
void m13_trace( int ev, const char * name, long long tick, long long arg )
{
    if( !g_rec || g_nev >= MAXEV ) return;
    g_ev[ g_nev ].ev = ev;
    g_ev[ g_nev ].name = name;   /* pcTaskName 은 TCB 안 배열 — 태스크를 지우지 않으니 끝까지 유효 */
    g_ev[ g_nev ].tick = tick;
    g_ev[ g_nev ].arg = arg;
    g_nev++;
}

/* solution.c 의 trace_format() 과 같은 규칙: TICK 이벤트 또는 tick 값이 바뀌면 새 줄 "[t]". */
static void print_trace( void )
{
    static const char * const tok[] = { "new", "rdy", "in", "tick", "delay", "dly", "ovf", "susp" };
    long long line = 0x7fffffffffffffffLL;
    for( int i = 0; i < g_nev; i++ )
    {
        const ev_t * e = &g_ev[ i ];
        if( e->ev == 3 || e->tick != line )
        {
            if( i ) printf( "\n" );
            printf( "[%lld]", e->tick );
            line = e->tick;
        }
        if( e->ev == 3 ) printf( " tick" );
        else if( e->ev == 5 || e->ev == 6 ) printf( " %s:%s@%lld", tok[ e->ev ], e->name, e->arg );
        else printf( " %s:%s", tok[ e->ev ], e->name );
    }
    printf( "\n" );
}

static void busy_task( void * p )
{
    volatile unsigned long * c = p;
    for( ; ; ) ( *c )++;                        /* API 호출 없음: tick 만이 이 태스크를 바꾼다 */
}

static void comm_task( void * p )
{
    ( void ) p;
    for( ; ; ) vTaskDelay( 3 );
}

static void ctrl_task( void * p )
{
    ( void ) p;
    for( int wake = 0; ; wake++ )
    {
        if( wake == 3 )
        {
            g_rec = 0;
            print_trace();
            printf( "events=%d  LogA/LogB spins > 0: %d %d\n", g_nev, g_spin[ 0 ] > 0, g_spin[ 1 ] > 0 );
            fflush( stdout );
            exit( 0 );
        }
        vTaskDelay( 4 );
    }
}

static void boot_task( void * p )
{
    ( void ) p;
    /* Boot(4) 혼자 최고 우선순위: tick 이 와도 ready[4] 길이 1 → time slicing 스위치 없음 (tasks.c 4971) */
    while( ( long long ) xTaskGetTickCount() < -6 ) { }
    g_rec = 1;
    if( ( long long ) xTaskGetTickCount() != -6 )
    {
        printf( "SYNC FAILED: tick=%lld\n", ( long long ) xTaskGetTickCount() );
        exit( 2 );
    }
    xTaskCreate( busy_task, "LogA", configMINIMAL_STACK_SIZE, ( void * ) &g_spin[ 0 ], 1, NULL );
    xTaskCreate( ctrl_task, "Ctrl", configMINIMAL_STACK_SIZE, NULL, 3, NULL );
    xTaskCreate( busy_task, "LogB", configMINIMAL_STACK_SIZE, ( void * ) &g_spin[ 1 ], 1, NULL );
    xTaskCreate( comm_task, "Comm", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
    vTaskSuspend( NULL );                       /* 다시 오지 않는다 */
    for( ; ; ) { }
}

int main( void )
{
    xTaskCreate( boot_task, "Boot", configMINIMAL_STACK_SIZE, NULL, 4, NULL );
    vTaskStartScheduler();
    return 1;                                   /* 여기 오면 힙 부족 */
}
