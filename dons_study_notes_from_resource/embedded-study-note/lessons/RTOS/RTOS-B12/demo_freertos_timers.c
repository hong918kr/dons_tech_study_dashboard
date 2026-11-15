/* RTOS-B12 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)의 소프트웨어 타이머가
 * starter/solution 의 결정적 모델과 같은 틱에 콜백을 부르는지 확인한다.
 *
 * 사용법: ./demo_timers <mode>
 *   block     LED(auto 4) + FLUSH(one-shot 9, 콜백 안에서 vTaskDelay(10)) + COMMS(one-shot 14), RX 리셋 @15
 *   defer     같은 시나리오, FLUSH 콜백은 세마포어만 주고 worker 태스크(prio 2)가 vTaskDelay(10)
 *   prio_low  데몬 prio 1, ctrl(prio 3)이 10..24 틱 동안 CPU 를 쓴다. ctrl 이 11 틱에 xTimerStart(X, 0)
 *   prio_high 같은 시나리오, 데몬 prio 6
 *   link      Part C: LED 느림(8)/빠름(2), COMMS one-shot 20, RX 프레임 @5,15,25,35,80,90,100
 *   burst     ISR 하나가 xTimerResetFromISR 를 12번 (명령 큐 10칸)
 * "ISR" = vApplicationTickHook (틱 ISR 안, tasks.c 5000-5013). 실시간성은 없다 — 틱 번호·반환값·순서만 본다.
 * 틱 번호는 모두 BASE(=50) 기준 상대값: 스케줄러 시작 직후의 스레드 생성 지연을 피하려고 setup 태스크가
 * BASE 틱에 타이머를 start 한다 (모델의 tick 0 = 여기의 BASE).
 *
 * 빌드 (md 의 명령 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_timers.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
 *      -o demo_timers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

#define MAXLOG 64
static struct { TickType_t tick; const char *what; } g_log[ MAXLOG ];
static volatile int g_nlog;
#define BASE 50u                                  /* 모델의 tick 0 */
static TickType_t rel_now( void ) { return xTaskGetTickCount() - BASE; }
static void logev( const char *what )             /* 데몬 문맥에서만 부른다 (한 태스크만 씀) */
{
    if( g_nlog < MAXLOG ) { g_log[ g_nlog ].tick = rel_now(); g_log[ g_nlog ].what = what; }
    g_nlog++;
}

static const char *g_mode;
static UBaseType_t g_tmr_prio = 6;
static TickType_t g_end = 40;
static TimerHandle_t g_led, g_flush, g_comms, g_x;
static SemaphoreHandle_t g_flush_sem;
static volatile TickType_t g_worker_done, g_x_cmd_tick;
static volatile BaseType_t g_x_active_after_start = -1, g_x_send_rc = -1;
static volatile int g_link_up = 1, g_rx_fail, g_burst_ok, g_burst_fail, g_pend_ok;
static char g_pend_ctx[ configMAX_TASK_NAME_LEN ];

/* 데몬이 처음 돌 때 데몬 안에서 불린다 → 자기 우선순위를 모드에 맞게 */
void vApplicationDaemonTaskStartupHook( void ) { vTaskPrioritySet( NULL, g_tmr_prio ); }

static void led_cb( TimerHandle_t t )   { ( void ) t; logev( "LED" ); }
static void x_cb( TimerHandle_t t )     { ( void ) t; logev( "X" ); }
static void flush_cb( TimerHandle_t t )
{
    ( void ) t;
    logev( "FLUSH" );
    if( strcmp( g_mode, "defer" ) == 0 ) xSemaphoreGive( g_flush_sem );   /* 일은 worker 에게 */
    else vTaskDelay( 10 );                                                /* 나쁜 예: 데몬이 10 틱 막힌다 */
}
static void comms_cb( TimerHandle_t t )
{
    ( void ) t;
    logev( "COMMS" );
    if( strcmp( g_mode, "link" ) == 0 )
    {
        g_link_up = 0;
        if( xTimerChangePeriod( g_led, 2, 0 ) != pdPASS ) g_rx_fail++;   /* 콜백 안: block time 0 */
    }
}
static void link_up_pended( void *p1, uint32_t rx_tick )                /* xTimerPendFunctionCallFromISR 로 미뤄진 함수 */
{
    ( void ) p1; ( void ) rx_tick;
    strncpy( g_pend_ctx, pcTaskGetName( NULL ), sizeof g_pend_ctx - 1 );
    if( g_link_up ) return;                                              /* 두 번 와도 한 번만 (멱등) */
    g_link_up = 1;
    logev( "UP" );
    if( xTimerChangePeriod( g_led, 8, 0 ) != pdPASS ) g_rx_fail++;
}

void vApplicationTickHook( void )                                     /* 틱 ISR 안 = "UART RX ISR" */
{
    TickType_t abs_now = xTaskGetTickCountFromISR();
    BaseType_t woken = pdFALSE;
    if( abs_now < BASE ) return;
    TickType_t now = abs_now - BASE;
    if( strcmp( g_mode, "link" ) == 0 )
    {
        if( now == 5 || now == 15 || now == 25 || now == 35 || now == 80 || now == 90 || now == 100 )
        {
            if( xTimerResetFromISR( g_comms, &woken ) != pdPASS ) g_rx_fail++;
            if( !g_link_up )
            {
                if( xTimerPendFunctionCallFromISR( link_up_pended, NULL, ( uint32_t ) now, &woken ) == pdPASS ) g_pend_ok++;
                else g_rx_fail++;
            }
        }
    }
    else if( strcmp( g_mode, "burst" ) == 0 )
    {
        if( now == 5 )
            for( int i = 0; i < 12; i++ )
            {
                if( xTimerResetFromISR( g_comms, &woken ) == pdPASS ) g_burst_ok++;
                else g_burst_fail++;
            }
    }
    else if( now == 15 )
    {
        ( void ) xTimerResetFromISR( g_comms, &woken );
    }
    /* 틱 훅 안에서는 portYIELD_FROM_ISR 를 따로 부르지 않는다 — 틱 ISR 이 끝에서 전환을 판단 (RTOS-B08) */
}

static void worker_task( void *p )
{
    ( void ) p;
    for( ; ; )
    {
        xSemaphoreTake( g_flush_sem, portMAX_DELAY );
        vTaskDelay( 10 );                                                /* 느린 플래시 쓰기 대용 */
        g_worker_done = rel_now();
    }
}

static void ctrl_task( void *p )                                        /* CPU 를 많이 쓰는 앱 태스크 (prio 3) */
{
    ( void ) p;
    TickType_t wake = 0;
    vTaskDelayUntil( &wake, BASE + 10 );
    int started = 0;
    TickType_t now;
    while( ( now = rel_now() ) < 25 )
    {
        if( !started && now >= 11 )
        {
            started = 1;
            g_x_cmd_tick = now;
            g_x_send_rc = xTimerStart( g_x, 0 );
            g_x_active_after_start = xTimerIsTimerActive( g_x );         /* 데몬이 아직 처리 안 했으면 pdFALSE */
        }
    }
    vTaskSuspend( NULL );
}

static void setup_task( void *p )                                       /* prio 7: BASE 틱에 타이머 start → 명령 틱 = BASE */
{
    ( void ) p;
    TickType_t wake = 0;
    vTaskDelayUntil( &wake, BASE );
    xTimerStart( g_led, 0 );
    xTimerStart( g_comms, 0 );
    if( g_flush ) xTimerStart( g_flush, 0 );
    vTaskSuspend( NULL );
}

static void print_fires( const char *name )
{
    printf( "  %-6s:", name );
    for( int i = 0; i < g_nlog && i < MAXLOG; i++ )
        if( strcmp( g_log[ i ].what, name ) == 0 && g_log[ i ].tick < g_end ) printf( " %lu", ( unsigned long ) g_log[ i ].tick );
    printf( "\n" );
}

static void monitor_task( void *p )
{
    ( void ) p;
    TickType_t wake = 0;
    vTaskDelayUntil( &wake, BASE + g_end );
    printf( "mode=%s  timer task prio=%lu  (ticks < %lu)\n", g_mode,
            ( unsigned long ) uxTaskPriorityGet( xTimerGetTimerDaemonTaskHandle() ), ( unsigned long ) g_end );
    print_fires( "LED" );
    if( g_flush ) print_fires( "FLUSH" );
    print_fires( "COMMS" );
    if( g_x ) print_fires( "X" );
    if( strcmp( g_mode, "link" ) == 0 ) print_fires( "UP" );
    printf( "  order :" );
    for( int i = 0; i < g_nlog && i < MAXLOG; i++ )
        if( g_log[ i ].tick < g_end ) printf( " %s@%lu", g_log[ i ].what, ( unsigned long ) g_log[ i ].tick );
    printf( "\n" );
    if( strcmp( g_mode, "defer" ) == 0 ) printf( "  worker flush done at tick %lu\n", ( unsigned long ) g_worker_done );
    if( g_x )
        printf( "  ctrl: xTimerStart(X,0) at tick %lu -> %ld, xTimerIsTimerActive right after -> %ld\n",
                ( unsigned long ) g_x_cmd_tick, ( long ) g_x_send_rc, ( long ) g_x_active_after_start );
    if( strcmp( g_mode, "link" ) == 0 )
        printf( "  pended fn ran in task \"%s\", pend ok=%d, timer API fails=%d\n", g_pend_ctx, g_pend_ok, g_rx_fail );
    if( strcmp( g_mode, "burst" ) == 0 )
        printf( "  12 x xTimerResetFromISR in one ISR: pdPASS=%d  pdFAIL=%d (configTIMER_QUEUE_LENGTH=%d)\n",
                g_burst_ok, g_burst_fail, configTIMER_QUEUE_LENGTH );
    exit( 0 );
}

int main( int argc, char **argv )
{
    g_mode = ( argc > 1 ) ? argv[ 1 ] : "block";
    int is_prio = ( strncmp( g_mode, "prio", 4 ) == 0 );
    if( strcmp( g_mode, "prio_low" ) == 0 ) g_tmr_prio = 1;

    if( strcmp( g_mode, "link" ) == 0 )
    {
        g_end = 112;
        g_led = xTimerCreate( "LED", 8, pdTRUE, NULL, led_cb );
        g_comms = xTimerCreate( "COMMS", 20, pdFALSE, NULL, comms_cb );
    }
    else
    {
        g_led = xTimerCreate( "LED", 4, pdTRUE, NULL, led_cb );
        g_comms = xTimerCreate( "COMMS", 14, pdFALSE, NULL, comms_cb );
        if( strcmp( g_mode, "block" ) == 0 || strcmp( g_mode, "defer" ) == 0 )
            g_flush = xTimerCreate( "FLUSH", 9, pdFALSE, NULL, flush_cb );
        if( is_prio ) g_x = xTimerCreate( "X", 10, pdFALSE, NULL, x_cb );
    }

    g_flush_sem = xSemaphoreCreateBinary();
    if( strcmp( g_mode, "defer" ) == 0 ) xTaskCreate( worker_task, "worker", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
    if( is_prio ) xTaskCreate( ctrl_task, "ctrl", configMINIMAL_STACK_SIZE, NULL, 3, NULL );
    xTaskCreate( monitor_task, "mon", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
    xTaskCreate( setup_task, "setup", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, NULL );
    vTaskStartScheduler();
    return 1;
}
