/* RTOS-M02 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)에서 FromISR + yield 요청의 효과를 본다.
 *
 * "ISR" 두 가지:
 *   (1) 틱 훅 vApplicationTickHook() — 틱 ISR(POSIX 포트의 SIGALRM 핸들러, port.c 488~) 안에서 불린다 (tasks.c 5006).
 *   (2) 태스크 안에서 흉내 낸 장치 ISR — taskENTER_CRITICAL() 로 틱 신호를 막고 FromISR API 를 부른 뒤
 *       portYIELD_FROM_ISR(w) 를 부르거나(E1a) 빼먹는다(E1b). POSIX 포트에서 portYIELD_FROM_ISR(x) 는
 *       x 가 참이면 vPortYield() — 즉시 스위치 (portmacro.h 86~99). 진짜 하드웨어 ISR 은 아니다: 순서만 본다.
 * 실험:
 *   E1a  흉내 ISR: give + portYIELD_FROM_ISR(w)         → 핸들러가 같은 tick 에, "ISR 복귀" 로그보다 먼저
 *   E1b  흉내 ISR: give, yield 생략                       → 핸들러는 다음 tick ISR 끝에서 (xYieldPendings)
 *   E2   틱 훅에서 give, yield 없음                       → 같은 tick (틱 ISR 자체가 xYieldPendings 를 본다)
 *   E3   틱 훅에서 xTimerPendFunctionCallFromISR          → 콜백이 "Tmr Svc" 태스크에서, 같은 tick
 *   E4   틱 훅에서 PendFunctionCall 11번 (큐 10칸)         → 10번 pdPASS, 1번 pdFAIL
 * 모든 기록은 메모리 로그에 쌓고 마지막에 한 번에 출력한다 (ISR 안 printf 금지). dt = 기록 tick - 실험 기준 tick.
 *
 * 빌드 (md 의 명령 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_from_isr.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#define LOGN 64
typedef struct { TickType_t tick; const char *who; const char *what; long v; } ev_t;
static ev_t g_log[ LOGN ];
static volatile int g_nlog;

static void log_raw( TickType_t t, const char * who, const char * what, long v )
{
    if( g_nlog < LOGN )
    {
        g_log[ g_nlog ].tick = t; g_log[ g_nlog ].who = who; g_log[ g_nlog ].what = what; g_log[ g_nlog ].v = v;
        g_nlog++;
    }
}
static void log_task( const char * who, const char * what, long v )   /* 태스크 문맥: 틱 훅과 섞이지 않게 */
{
    taskENTER_CRITICAL();
    log_raw( xTaskGetTickCount(), who, what, v );
    taskEXIT_CRITICAL();
}

static SemaphoreHandle_t g_sem;
enum { HOOK_IDLE, HOOK_GIVE, HOOK_PEND, HOOK_PEND11 };
static volatile int g_hook_cmd = HOOK_IDLE;
static volatile TickType_t g_hook_at;

static void callback( void * p1, uint32_t p2 )   /* PendedFunction_t — 타이머 데몬 태스크에서 실행 */
{
    ( void ) p1;
    log_task( "callback", pcTaskGetName( NULL ), ( long ) p2 );
}

void vApplicationTickHook( void )                /* 틱 ISR 안. FromISR API 만, 블로킹·printf 금지 */
{
    TickType_t now = xTaskGetTickCountFromISR();
    int cmd = g_hook_cmd;

    if( ( cmd == HOOK_IDLE ) || ( now != g_hook_at ) ) return;
    g_hook_cmd = HOOK_IDLE;
    BaseType_t w = pdFALSE;

    if( cmd == HOOK_GIVE )
    {
        BaseType_t rc = xSemaphoreGiveFromISR( g_sem, &w );
        log_raw( now, "tickhook", "GiveFromISR rc", ( long ) rc );
        log_raw( now, "tickhook", "woken (no portYIELD_FROM_ISR)", ( long ) w );
    }
    else
    {
        int n = ( cmd == HOOK_PEND ) ? 1 : 11;
        int ok = 0;

        for( int i = 0; i < n; i++ )
        {
            if( xTimerPendFunctionCallFromISR( callback, NULL, ( uint32_t ) ( i + 1 ), &w ) == pdPASS ) ok++;
        }
        log_raw( now, "tickhook", "PendFunctionCallFromISR pdPASS count", ok );
        log_raw( now, "tickhook", "woken", ( long ) w );
    }
    /* 틱 훅에서는 portYIELD_FROM_ISR 를 부르지 않는다: 틱 ISR 이 끝에서 xYieldPendings 를 보고 바꾼다 */
}

static void handler( void * p )                  /* prio 3: 이벤트 처리 태스크 */
{
    ( void ) p;
    for( ; ; )
    {
        if( xSemaphoreTake( g_sem, portMAX_DELAY ) == pdTRUE ) log_task( "handler", "woke (first instruction)", 0 );
    }
}

static void spin_ticks( TickType_t n )
{
    TickType_t t = xTaskGetTickCount();
    while( xTaskGetTickCount() - t < n ) { }
}
static void wait_tick_edge( void )
{
    TickType_t t = xTaskGetTickCount();
    while( xTaskGetTickCount() == t ) { }
}

static void fake_device_isr( int use_yield )     /* 장치 ISR 흉내: ISR 본문은 임계 구역(틱 신호 막음) 안 */
{
    BaseType_t w = pdFALSE;

    taskENTER_CRITICAL();
    BaseType_t rc = xSemaphoreGiveFromISR( g_sem, &w );
    log_raw( xTaskGetTickCount(), "isr", "GiveFromISR rc", ( long ) rc );
    log_raw( xTaskGetTickCount(), "isr", use_yield ? "woken -> portYIELD_FROM_ISR(woken)" : "woken (yield omitted)", ( long ) w );
    taskEXIT_CRITICAL();

    if( use_yield ) portYIELD_FROM_ISR( w );
}

static TickType_t g_base[ 6 ];
static const char * const g_name[ 6 ] = { "E1a fake ISR + yield", "E1b fake ISR, no yield", "E2 tick hook give",
                                          "E3 tick hook PendFunctionCall", "E4 11 x PendFunctionCall (queue 10)", "" };

static void busy( void * p )                     /* prio 1: 항상 바쁜 백그라운드 태스크 = ISR 이 끼어드는 대상 */
{
    ( void ) p;
    vTaskDelay( 5 );

    for( int e = 0; e < 2; e++ )                 /* E1a, E1b */
    {
        wait_tick_edge();                        /* tick 직후: 다음 tick 까지 거의 1 ms 남았다 */
        g_base[ e ] = xTaskGetTickCount();
        log_task( "busy", g_name[ e ], 0 );
        fake_device_isr( e == 0 );
        log_task( "busy", "back from ISR, continues", 0 );
        spin_ticks( 3 );
    }

    int cmds[ 3 ] = { HOOK_GIVE, HOOK_PEND, HOOK_PEND11 };

    for( int e = 2; e < 5; e++ )                 /* E2, E3, E4: 틱 훅이 2 tick 뒤에 */
    {
        g_base[ e ] = xTaskGetTickCount() + 2;
        log_task( "busy", g_name[ e ], 0 );
        g_hook_at = g_base[ e ];
        g_hook_cmd = cmds[ e - 2 ];
        spin_ticks( 5 );
    }

    int e = 0;
    printf( "%-9s %-4s %-40s %s\n", "who", "dt", "what", "value" );
    for( int i = 0; i < g_nlog; i++ )
    {
        if( ( e < 5 ) && ( g_log[ i ].who[ 0 ] == 'b' ) && ( g_log[ i ].what == g_name[ e ] ) )
        {
            printf( "-- %s\n", g_name[ e ] );
            e++;
        }
        long dt = ( long ) g_log[ i ].tick - ( long ) g_base[ e > 0 ? e - 1 : 0 ];
        printf( "%-9s %+-4ld %-40s %ld\n", g_log[ i ].who, dt, g_log[ i ].what, g_log[ i ].v );
    }
    exit( 0 );
}

int main( void )
{
    g_sem = xSemaphoreCreateBinary();
    xTaskCreate( handler, "handler", configMINIMAL_STACK_SIZE, NULL, 3, NULL );
    xTaskCreate( busy, "busy", configMINIMAL_STACK_SIZE, NULL, 1, NULL );
    vTaskStartScheduler();
    return 1;
}
