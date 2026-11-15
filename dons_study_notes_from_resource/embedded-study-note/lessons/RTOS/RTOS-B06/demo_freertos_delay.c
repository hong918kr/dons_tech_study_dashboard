/* RTOS-B06 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)에서 지연·wrap 을 확인한다.
 *
 * tick 은 configINITIAL_TICK_COUNT = ULONG_MAX - 15 에서 시작한다 → 16 tick 뒤 0 으로 wrap
 * (xTaskIncrementTick 이 0 이 되는 순간 taskSWITCH_DELAYED_LISTS(), tasks.c:4857-4861).
 *   1) wrap 을 가로지르는 깨우기 순서: A=vTaskDelay(20) B=5 C=30 D=5, E=xTaskDelayUntil(&S, 25)
 *   2) 작업 시간이 들쭉날쭉할 때 vTaskDelay(10) vs xTaskDelayUntil(&last, 10) — 릴리스 tick 10번
 *   3) overrun: k=3 의 작업이 25 tick → xTaskDelayUntil 반환값 / xTaskPeriodicDelay 반환값과 릴리스
 *   4) vTaskDelay(0) = yield (time slicing 끔): 같은 우선순위 두 태스크
 * 실시간성은 없다(호스트). tick 번호·반환값·순서만 본다. 작업은 "tick 이 c-1 만큼 지날 때까지 바쁜 루프".
 *
 * 빌드:
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_delay.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
 *      -o demo_freertos_delay
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#define S ( ( TickType_t ) configINITIAL_TICK_COUNT )
static const unsigned g_cost[ 10 ] = { 3, 3, 4, 4, 2, 1, 2, 4, 1, 3 };   /* 절 2 의 작업 길이 (tick) */

static volatile int g_done[ 8 ];
static char g_order[ 32 ];
static TickType_t g_woke[ 5 ], g_called[ 5 ];
static TickType_t g_rel[ 4 ][ 12 ];
static BaseType_t g_ret[ 4 ][ 12 ];

static void work( TickType_t r, unsigned c )               /* 릴리스 r 부터 c tick 동안 실행된 것처럼 */
{
    while( xTaskGetTickCount() - r < ( TickType_t ) ( c - 1 ) ) { }
}

static void wrap_task( void * p )                           /* 절 1 */
{
    int i = ( int ) ( intptr_t ) p;
    static const TickType_t d[ 5 ] = { 20, 5, 30, 5, 25 };
    g_called[ i ] = xTaskGetTickCount();
    if( i == 4 ) { TickType_t last = S; ( void ) xTaskDelayUntil( &last, d[ i ] ); }
    else vTaskDelay( d[ i ] );
    g_woke[ i ] = xTaskGetTickCount();
    taskENTER_CRITICAL();
    size_t n = strlen( g_order );
    g_order[ n ] = ( char ) ( 'A' + i ); g_order[ n + 1 ] = '\0';
    taskEXIT_CRITICAL();
    vTaskDelete( NULL );
}

static void periodic_task( void * p )                       /* 절 2 (0: delay, 1: until), 절 3 (2: until, 3: periodic) */
{
    int m = ( int ) ( intptr_t ) p;
    TickType_t last = xTaskGetTickCount();
    for( int k = 0; k < ( m < 2 ? 10 : 9 ); k++ )
    {
        TickType_t r = xTaskGetTickCount();
        g_rel[ m ][ k ] = r;
        work( r, m < 2 ? g_cost[ k ] : ( k == 3 ? 25u : 2u ) );
        if( m == 0 ) vTaskDelay( 10 );
        else if( m == 3 ) g_ret[ m ][ k ] = ( BaseType_t ) xTaskPeriodicDelay( &last, 10 );
        else g_ret[ m ][ k ] = xTaskDelayUntil( &last, 10 );
    }
    g_done[ m ] = 1;
    vTaskDelete( NULL );
}

static void yield_task( void * p )                          /* 절 4 */
{
    const char * nm = p;
    for( int k = 0; k < 3; k++ )
    {
        taskENTER_CRITICAL();
        size_t n = strlen( g_order ); g_order[ n ] = nm[ 0 ]; g_order[ n + 1 ] = '\0';
        taskEXIT_CRITICAL();
        if( nm[ 1 ] == 'y' ) vTaskDelay( 0 );
    }
    g_done[ 4 + ( nm[ 0 ] & 1 ) ]++;
    vTaskDelete( NULL );
}

static void wait_flag( volatile int * f ) { while( !*f ) vTaskDelay( 1 ); }

static void controller( void * p )
{
    ( void ) p;
    vTaskDelay( 40 );                                       /* 절 1 이 끝날 때까지 (S+30 에 마지막 깸) */
    printf( "start tick S = 0x%lx, now = S+%lu (wrapped: now = 0x%lx)\n", ( unsigned long ) S,
            ( unsigned long ) ( xTaskGetTickCount() - S ), ( unsigned long ) xTaskGetTickCount() );
    printf( "1) wake order across the wrap: %s\n", g_order );
    for( int i = 0; i < 5; i++ )
        printf( "   %c called at S+%lu, woke at S+%-3lu (raw 0x%016lx)\n", 'A' + i, ( unsigned long ) ( g_called[ i ] - S ),
                ( unsigned long ) ( g_woke[ i ] - S ), ( unsigned long ) g_woke[ i ] );

    for( int m = 0; m < 4; m++ )
    {
        xTaskCreate( periodic_task, "per", configMINIMAL_STACK_SIZE, ( void * ) ( intptr_t ) m, 3, NULL );
        wait_flag( &g_done[ m ] );
        vTaskDelay( 3 );
    }
    const char * nm[ 4 ] = { "vTaskDelay(10)      ", "xTaskDelayUntil(10) ", "xTaskDelayUntil k3=25", "xTaskPeriodicDelay  " };
    printf( "2) costs:" ); for( int k = 0; k < 10; k++ ) printf( " %u", g_cost[ k ] ); printf( "\n" );
    for( int m = 0; m < 4; m++ )
    {
        if( m == 2 ) printf( "3) overrun at k=3 (25 ticks), period 10\n" );
        printf( "   %s releases (rel. first):", nm[ m ] );
        for( int k = 0; k < ( m < 2 ? 10 : 9 ); k++ ) printf( " %3lu", ( unsigned long ) ( g_rel[ m ][ k ] - g_rel[ m ][ 0 ] ) );
        if( m >= 2 ) { printf( "  returns" ); for( int k = 0; k < 9; k++ ) printf( " %ld", ( long ) g_ret[ m ][ k ] ); }
        printf( "\n" );
    }

    g_order[ 0 ] = '\0';
    xTaskCreate( yield_task, "X", configMINIMAL_STACK_SIZE, "Xy", 2, NULL );
    xTaskCreate( yield_task, "Y", configMINIMAL_STACK_SIZE, "Yy", 2, NULL );
    while( g_done[ 4 ] + g_done[ 5 ] < 2 ) vTaskDelay( 1 );
    printf( "4) with vTaskDelay(0): %s", g_order );
    g_order[ 0 ] = '\0'; g_done[ 4 ] = g_done[ 5 ] = 0;
    xTaskCreate( yield_task, "W", configMINIMAL_STACK_SIZE, "W-", 2, NULL );
    xTaskCreate( yield_task, "Z", configMINIMAL_STACK_SIZE, "Z-", 2, NULL );
    while( g_done[ 4 ] + g_done[ 5 ] < 2 ) vTaskDelay( 1 );
    printf( "   without: %s\n", g_order );
    exit( 0 );
}

int main( void )
{
    for( int i = 0; i < 5; i++ )
        xTaskCreate( wrap_task, "w", configMINIMAL_STACK_SIZE, ( void * ) ( intptr_t ) i, 4, NULL );
    xTaskCreate( controller, "ctl", configMINIMAL_STACK_SIZE, NULL, 5, NULL );
    vTaskStartScheduler();
    return 1;
}
