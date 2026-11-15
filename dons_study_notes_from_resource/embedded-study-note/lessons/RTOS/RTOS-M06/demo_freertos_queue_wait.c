/* RTOS-M06 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)가 모델과 같은 순서·반환값을 내는지 본다.
 *
 * [1] 받기 대기자 4명 (RL p2, RH p5, RM1 p3, RM2 p3 — 블록하는 순서는 RL, RH, RM1, RM2)
 *     (a) 보내는 쪽이 가장 낮을 때(p1): 보낼 때마다 깨운 태스크가 더 높으니 바로 전환 → 받은 순서 = 깨운 순서
 *     (b) 보내는 쪽이 가장 높을 때(p6): 4개를 다 보낸 뒤 대기자들이 prio 순으로 돈다
 *     둘 다 기대값: RH, RM1, RM2, RL (xItemValue = configMAX_PRIORITIES - prio 오름차순, 같으면 먼저 온 순)
 * [2] 뺏긴 아이템: L(p1) 이 100 tick 타임아웃으로 받기 대기. S(p2) 가 보내서 L 을 깨우고(L 이 더 낮아 전환 없음)
 *     H(p3) 에게 알림 → H 가 먼저 돌아 timeout 0 으로 가져간다 → L 은 루프로 돌아가 "남은" 시간으로 다시 블록.
 *     traceBLOCKING_ON_QUEUE_RECEIVE 로 L 이 블록할 때의 xTicksToWait 를 기록한다.
 * [3] 타임아웃과 give 가 같은 tick: T(p2) 가 50 tick 타임아웃으로 받기. 틱 훅(= 틱 ISR, 지연 리스트 처리 뒤)이
 *     마감 tick - 1 / 마감 tick / 마감 tick + 1 에 xQueueSendFromISR.
 * 태스크는 지우지 않는다 (macOS POSIX 포트에서 다른 태스크의 vTaskDelete 가 가끔 교착 — RTOS-B11 참고).
 * 끝나면 컨트롤러가 exit(0). 실시간성은 없다: 순서·반환값·tick 차이를 본다.
 *
 * 빌드:
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_queue_wait.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---------- trace: 블록할 때마다 (태스크 이름, 남은 tick, 지금 tick) ---------- */
#define NTRACE 64
static struct { char name[8]; unsigned long long ticks; TickType_t at; } g_tr[ NTRACE ];
static volatile int g_ntr;
static QueueHandle_t g_trace_q;                /* 이 큐에서 블록할 때만 기록 */
void vDemoTraceBlockRx( void * pvQueue, unsigned long long ullTicksToWait )
{
    if( ( pvQueue != ( void * ) g_trace_q ) || ( g_ntr >= NTRACE ) ) return;
    int i = g_ntr++;
    snprintf( g_tr[ i ].name, sizeof g_tr[ i ].name, "%s", pcTaskGetName( NULL ) );
    g_tr[ i ].ticks = ullTicksToWait;
    g_tr[ i ].at = xTaskGetTickCount();
}

/* ---------- [1] ---------- */
static QueueHandle_t g_q1;
static char g_got_name[ 8 ][ 8 ];
static int g_got_val[ 8 ];
static volatile int g_ngot;
typedef struct { const char *name; TickType_t stagger; } rx_arg_t;
static void receiver( void * pv )
{
    const rx_arg_t *a = pv;
    for( ; ; )
    {
        int v;
        vTaskDelay( a->stagger );              /* 블록하는 순서를 정한다 */
        if( xQueueReceive( g_q1, &v, portMAX_DELAY ) == pdPASS )
        {
            int i = g_ngot++;
            snprintf( g_got_name[ i ], sizeof g_got_name[ i ], "%s", a->name );
            g_got_val[ i ] = v;
        }
    }
}

/* ---------- [2] ---------- */
static QueueHandle_t g_q2;
static TaskHandle_t g_hH;
static volatile int g_L_rc = -1, g_H_rc = -1, g_H_val;
static volatile TickType_t g_L_start, g_L_end, g_S_at, g_H_at;
static void task_L( void * pv )
{
    ( void ) pv;
    int v;
    vTaskDelay( 1 );
    g_L_start = xTaskGetTickCount();
    g_L_rc = ( int ) xQueueReceive( g_q2, &v, 100 );
    g_L_end = xTaskGetTickCount();
    for( ; ; ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
}
static void task_S( void * pv )
{
    ( void ) pv;
    int v = 7;
    vTaskDelay( 21 );
    g_S_at = xTaskGetTickCount();
    xQueueSend( g_q2, &v, 0 );                 /* L 을 깨운다 (L 이 더 낮음 → 전환 없음) */
    xTaskNotifyGive( g_hH );                   /* H 가 더 높음 → 여기서 H 로 전환 */
    for( ; ; ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
}
static void task_H( void * pv )
{
    ( void ) pv;
    int v;
    ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
    g_H_at = xTaskGetTickCount();
    g_H_rc = ( int ) xQueueReceive( g_q2, &v, 0 );
    g_H_val = v;
    for( ; ; ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
}

/* ---------- [3] ---------- */
static QueueHandle_t g_q3;
static volatile TickType_t g_isr_at;           /* 0 = 꺼짐 */
static volatile int g_isr_rc = -1;
void vApplicationTickHook( void )              /* 틱 ISR 안 — FromISR API 만 */
{
    if( ( g_isr_at != 0 ) && ( xTaskGetTickCountFromISR() == g_isr_at ) )
    {
        int v = 42;
        BaseType_t woken = pdFALSE;
        g_isr_rc = ( int ) xQueueSendFromISR( g_q3, &v, &woken );
        g_isr_at = 0;
        ( void ) woken;                         /* 깨운 태스크가 있으면 xYieldPendings 로 처리된다 (tasks.c:5020) */
    }
}
static volatile int g_T_rc, g_T_val, g_T_done;
static volatile TickType_t g_T_elapsed;
static volatile int g_T_offset;                /* 마감 대비 ISR 시각: -1, 0, +1 */
static TaskHandle_t g_hT;
static void task_T( void * pv )
{
    ( void ) pv;
    for( ; ; )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        int v = 0;
        vTaskDelay( 1 );                        /* tick 경계에 맞춘다 */
        TickType_t start = xTaskGetTickCount();
        g_isr_at = start + 50 + ( TickType_t ) ( int64_t ) g_T_offset;
        g_T_rc = ( int ) xQueueReceive( g_q3, &v, 50 );
        g_T_elapsed = xTaskGetTickCount() - start;
        g_T_val = v;
        g_T_done = 1;
    }
}

static const char *rc_s( int rc ) { return rc == pdPASS ? "pdPASS" : rc == errQUEUE_EMPTY ? "errQUEUE_EMPTY" : "?"; }

static void controller( void * pv )
{
    ( void ) pv;
    static const rx_arg_t args[ 4 ] = { { "RL", 5 }, { "RH", 10 }, { "RM1", 15 }, { "RM2", 20 } };
    static const UBaseType_t prio[ 4 ] = { 2, 5, 3, 3 };

    /* [1] — 컨트롤러 자신이 보내는 쪽 */
    g_q1 = xQueueCreate( 4, sizeof( int ) );
    g_trace_q = g_q1;
    vTaskPrioritySet( NULL, 1 );               /* (a) 보내는 쪽이 가장 낮다 */
    for( int i = 0; i < 4; i++ ) xTaskCreate( receiver, args[ i ].name, configMINIMAL_STACK_SIZE, ( void * ) &args[ i ], prio[ i ], NULL );
    vTaskDelay( 30 );
    printf( "[1] block order (trace):" );
    for( int i = 0; i < g_ntr; i++ ) printf( " %s", g_tr[ i ].name );
    printf( "\n" );
    for( int v = 1; v <= 4; v++ ) xQueueSend( g_q1, &v, 0 );
    printf( "[1a] sender p1 :" );
    for( int i = 0; i < g_ngot; i++ ) printf( " %s<-%d", g_got_name[ i ], g_got_val[ i ] );
    printf( "\n" );
    g_ngot = 0; g_ntr = 0;
    vTaskPrioritySet( NULL, 6 );               /* (b) 보내는 쪽이 가장 높다 */
    vTaskDelay( 30 );                          /* 대기자들이 다시 RL, RH, RM1, RM2 순으로 블록 (5 tick 간격) */
    printf( "[1] block order (trace):" );
    for( int i = 0; i < g_ntr; i++ ) printf( " %s", g_tr[ i ].name );
    printf( "\n" );
    for( int v = 1; v <= 4; v++ ) xQueueSend( g_q1, &v, 0 );
    int after_send = g_ngot;                   /* 아직 아무도 못 돌았다 */
    vTaskDelay( 2 );
    printf( "[1b] sender p6 : received during sends=%d, then", after_send );
    for( int i = 0; i < g_ngot; i++ ) printf( " %s<-%d", g_got_name[ i ], g_got_val[ i ] );
    printf( "\n" );

    /* [2] — 컨트롤러는 p6 에서 기다린다 */
    g_q2 = xQueueCreate( 1, sizeof( int ) );
    g_trace_q = g_q2; g_ntr = 0;
    xTaskCreate( task_H, "H", configMINIMAL_STACK_SIZE, NULL, 3, &g_hH );
    xTaskCreate( task_S, "S", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
    xTaskCreate( task_L, "L", configMINIMAL_STACK_SIZE, NULL, 1, NULL );
    vTaskDelay( 130 );
    printf( "[2] S sent at +%lu, H got %d (%s) at +%lu, L: %s after %lu ticks\n",
            ( unsigned long ) ( g_S_at - g_L_start ), g_H_val, rc_s( g_H_rc ), ( unsigned long ) ( g_H_at - g_L_start ),
            rc_s( g_L_rc ), ( unsigned long ) ( g_L_end - g_L_start ) );
    for( int i = 0; i < g_ntr; i++ )
        printf( "[2] trace: %s blocks at +%lu with xTicksToWait=%llu\n", g_tr[ i ].name,
                ( unsigned long ) ( g_tr[ i ].at - g_L_start ), g_tr[ i ].ticks );

    /* [3] */
    g_q3 = xQueueCreate( 1, sizeof( int ) );
    g_trace_q = NULL;
    xTaskCreate( task_T, "T", configMINIMAL_STACK_SIZE, NULL, 2, &g_hT );
    for( int off = -1; off <= 1; off++ )
    {
        int junk;
        g_T_offset = off; g_T_done = 0; g_isr_rc = -1;
        xTaskNotifyGive( g_hT );
        while( !g_T_done ) vTaskDelay( 5 );
        vTaskDelay( 3 );
        printf( "[3] ISR send at deadline%+d: isr=%s, T got %s val=%d after %lu ticks, left in queue=%lu\n", off,
                g_isr_rc == pdPASS ? "pdPASS" : "?", rc_s( g_T_rc ), g_T_rc == pdPASS ? g_T_val : 0,
                ( unsigned long ) g_T_elapsed, ( unsigned long ) uxQueueMessagesWaiting( g_q3 ) );
        while( xQueueReceive( g_q3, &junk, 0 ) == pdPASS ) { }
    }
    fflush( stdout );
    exit( 0 );
}

int main( void )
{
    xTaskCreate( controller, "ctl", configMINIMAL_STACK_SIZE, NULL, 6, NULL );
    vTaskStartScheduler();
    return 1;
}
