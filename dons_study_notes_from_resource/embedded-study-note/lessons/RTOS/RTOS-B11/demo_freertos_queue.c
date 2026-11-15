/* RTOS-B11 데모 1 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)로 큐의 의미와 파이프라인 정책을 확인한다.
 *
 * [1] API 의미: 복사(보낸 뒤 원본을 바꿔도 큐 안의 값은 그대로), FIFO, 꽉 참/빔 + timeout 0 / 5 틱,
 *     길이 1 큐의 xQueueOverwrite + xQueuePeek.
 * [2] 센서(prio 3) → q_raw → 처리(prio 2) → q_log → 로깅(prio 1) 파이프라인. solution.c 모델과 같은 시나리오를
 *     시간만 K 배로 늘려 돌린다 (주기 3K, 처리 1K, 로깅 1K, seq 2 로깅만 12K). 정책: BLOCK / DROP / TIMEOUT / OVERWRITE.
 * [3] ISR 변형: 틱 훅(= 틱 ISR 안)에서 xQueueSendFromISR — 꽉 차면 errQUEUE_FULL, 기다리지 않는다.
 *
 * "일" 은 CPU 시간으로 센다: 틱 훅이 틱마다 그 순간 돌고 있던 태스크에 CPU 틱 1개를 매기고(g_cpu[]), 태스크는
 * 자기 CPU 틱이 cost 만큼 늘 때까지 바쁜 루프를 돈다 → 높은 태스크에게 선점당한 시간은 일로 치지 않는다 (모델과 같다).
 * 실시간성은 없다(호스트). 반환값·순서·개수를 본다.
 * 태스크는 한 번만 만들고 실행마다 재사용한다: 다른 태스크에서 vTaskDelete 를 부르면 macOS POSIX 포트가 가끔
 * 교착한다 (vPortCancelThread → event_signal 의 mutex, port.c 531-541 / wait_for_event.c 126-133 — 5회 중 1회 관측).
 * 그래서 큐는 깊이별로 미리 만들어 두고(지우지 않음), P·L 은 KS 틱 타임아웃으로 받으며 매번 "지금 큐" 를 다시 읽는다.
 *
 * 빌드 (md 의 명령 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_queue.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define KS 10u                                 /* 시간 배율: 모델의 1 틱 = 여기서 KS 틱 */
typedef struct { uint32_t seq; int32_t t_rel; int16_t raw[4]; } sample_t;   /* 16 B (모델과 같은 모양) */
typedef struct { uint32_t seq; int32_t t_rel; int32_t value; } logrec_t;    /* 12 B */
typedef enum { POL_BLOCK, POL_DROP, POL_TIMEOUT, POL_OVERWRITE } policy_t;
static const char *const pol_name[] = { "BLOCK", "DROP", "TIMEOUT2", "OVERWRITE" };

/* ---------- 태스크별 CPU 틱 (틱 훅이 매긴다) ---------- */
enum { T_S, T_P, T_L, T_RX, T_BG, T_N };
static TaskHandle_t g_h[ T_N ];
static volatile uint32_t g_cpu[ T_N ];
static void burn( int me, uint32_t ticks )     /* 내 CPU 틱이 ticks 만큼 늘 때까지 일한다 */
{
    uint32_t s = g_cpu[ me ];
    while( g_cpu[ me ] - s < ticks ) { }
}

/* ---------- [3] ISR 변형 상태 ---------- */
static QueueHandle_t g_qisr;
static volatile TickType_t g_isr_base;         /* 0 = 꺼짐 */
static volatile unsigned g_isr_n, g_isr_ok, g_isr_full, g_isr_woken, g_isr_got;

/* 틱 ISR 안에서 불린다 → ISR 문맥. FromISR API 만, 블로킹·printf 금지. */
void vApplicationTickHook( void )
{
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();   /* 단일 코어: pxCurrentTCB 를 읽을 뿐 (tasks.c 6645-6654) */

    for( int i = 0; i < T_N; i++ )
    {
        if( ( cur != NULL ) && ( cur == g_h[ i ] ) ) g_cpu[ i ]++;
    }

    TickType_t now = xTaskGetTickCountFromISR();
    TickType_t b = g_isr_base;

    if( ( b != 0 ) && ( now >= b ) && ( ( now - b ) % KS == 0 ) && ( ( now - b ) / KS < g_isr_n ) )
    {
        uint32_t v = ( uint32_t ) ( ( now - b ) / KS );
        BaseType_t woken = pdFALSE;                   /* 호출자가 pdFALSE 로 초기화 */

        if( xQueueSendFromISR( g_qisr, &v, &woken ) == pdPASS ) g_isr_ok++; else g_isr_full++;
        if( woken != pdFALSE ) g_isr_woken++;
        /* 진짜 장치 ISR 이면 portYIELD_FROM_ISR( woken ). 틱 훅은 틱 ISR 이 끝에서 xYieldPendings 를 보고 전환. */
    }
}

/* ---------- [2] 파이프라인 ---------- */
typedef struct { int n; uint32_t period, proc, log_cost; int stall_seq; uint32_t stall_cost;
                 UBaseType_t raw_depth, log_depth; policy_t pol; } cfg_t;
static cfg_t C;
static QueueHandle_t volatile g_qraw, g_qlog;     /* 이번 실행이 쓰는 큐 (미리 만든 것 중에서 고른다) */
static QueueHandle_t g_raw_by_depth[ 8 ], g_log_by_depth[ 8 ];
static volatile unsigned r_produced, r_sdrop, r_pdrop, r_ow, r_logged;
static volatile int r_maxlate, r_order_ok, r_data_ok;
static volatile uint32_t r_seq[ 32 ];
static volatile int r_done;                    /* 로깅이 끝난(또는 버려진) 마지막 처리 */

static int16_t raw_of( uint32_t seq, int k ) { return ( int16_t ) ( seq * 10u + ( unsigned ) k ); }
static TickType_t send_to( void )
{
    return C.pol == POL_DROP ? 0 : C.pol == POL_TIMEOUT ? 2 * KS : portMAX_DELAY;
}

static void sensor( void * arg )
{
    ( void ) arg;
    sample_t s;

    for( ; ; )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );        /* 제어 태스크가 "시작" 을 알릴 때까지 */
        TickType_t base = xTaskGetTickCount(), last = base;

        for( int seq = 0; seq < C.n; seq++ )
        {
            TickType_t rel = base + ( TickType_t ) seq * C.period;
            s.seq = ( uint32_t ) seq; s.t_rel = ( int32_t ) ( rel - base );
            for( int k = 0; k < 4; k++ ) s.raw[ k ] = raw_of( ( uint32_t ) seq, k );

            if( xQueueSend( g_qraw, &s, C.pol == POL_OVERWRITE ? portMAX_DELAY : send_to() ) == pdPASS )
            {
                int late = ( int ) ( xTaskGetTickCount() - rel );
                if( late > r_maxlate ) r_maxlate = late;
                memset( s.raw, 0xEE, sizeof s.raw );          /* 보낸 뒤 원본에 낙서 — 복사 큐라 안전 */
            }
            else
            {
                r_sdrop++;
            }
            r_produced++;
            if( seq + 1 < C.n ) xTaskDelayUntil( &last, C.period );   /* 늦었으면 바로 돌아온다 */
        }
    }
}

static void process( void * arg )
{
    ( void ) arg;
    sample_t in;
    logrec_t out;

    for( ; ; )
    {
        if( xQueueReceive( g_qraw, &in, KS ) != pdPASS ) continue;   /* 실행 사이: 큐가 바뀌었을 수 있다 */
        burn( T_P, C.proc );
        out.seq = in.seq; out.t_rel = in.t_rel; out.value = 0;
        for( int k = 0; k < 4; k++ ) out.value += in.raw[ k ];

        if( C.pol == POL_OVERWRITE )
        {
            if( uxQueueMessagesWaiting( g_qlog ) != 0 ) r_ow++;   /* 안 읽힌 값을 덮어쓰게 된다 */
            ( void ) xQueueOverwrite( g_qlog, &out );                /* 항상 pdPASS */
        }
        else if( xQueueSend( g_qlog, &out, send_to() ) != pdPASS )
        {
            r_pdrop++;
            if( ( int ) out.seq == C.n - 1 ) r_done = 1;
        }
    }
}

static void logger( void * arg )
{
    ( void ) arg;
    logrec_t in;

    for( ; ; )
    {
        if( xQueueReceive( g_qlog, &in, KS ) != pdPASS ) continue;
        burn( T_L, ( int ) in.seq == C.stall_seq ? C.stall_cost : C.log_cost );
        int32_t v = 0;
        for( int k = 0; k < 4; k++ ) v += raw_of( in.seq, k );
        if( in.value != v ) r_data_ok = 0;
        if( ( r_logged > 0 ) && ( in.seq <= r_seq[ r_logged - 1 ] ) ) r_order_ok = 0;
        if( r_logged < 32 ) r_seq[ r_logged ] = in.seq;
        r_logged++;
        if( ( int ) in.seq == C.n - 1 ) r_done = 1;
    }
}

static void run_pipeline( cfg_t cfg, int print_seq )
{
    C = cfg;
    r_produced = r_sdrop = r_pdrop = r_ow = r_logged = 0;
    r_maxlate = 0; r_order_ok = r_data_ok = 1; r_done = 0;
    g_qraw = g_raw_by_depth[ C.raw_depth ];
    g_qlog = g_log_by_depth[ C.log_depth ];
    xQueueReset( g_qraw ); xQueueReset( g_qlog );
    vTaskDelay( 3 * KS );                              /* P·L 이 타임아웃 후 새 큐를 집어 들 시간 */
    xTaskNotifyGive( g_h[ T_S ] );                     /* 시작 */

    TickType_t t0 = xTaskGetTickCount();
    while( !r_done && xTaskGetTickCount() - t0 < 2000 ) vTaskDelay( KS );
    vTaskDelay( 2 * KS );

    printf( "%-9s %u/%u %8u %5u %5u %3u %6u %6d (%.1f) %3s %3s ", pol_name[ C.pol ],
            ( unsigned ) C.raw_depth, ( unsigned ) C.log_depth, r_produced, r_logged, r_sdrop, r_pdrop, r_ow,
            r_maxlate, ( double ) r_maxlate / KS, r_order_ok ? "ok" : "BAD", r_data_ok ? "ok" : "BAD" );
    if( print_seq ) for( unsigned i = 0; i < r_logged && i < 32; i++ ) printf( " %u", ( unsigned ) r_seq[ i ] );
    printf( "\n" );
}

/* ---------- [3] ISR → 큐 → 느린 태스크 ---------- */
static void rx_task( void * arg )
{
    ( void ) arg;
    uint32_t v;

    for( ; ; )
    {
        xQueueReceive( g_qisr, &v, portMAX_DELAY );
        burn( T_RX, 3 * KS );                         /* 항목당 3K CPU 틱 (ISR 은 K 틱마다 1개) */
        g_isr_got++;
    }
}
static void bg_task( void * arg ) { ( void ) arg; for( ; ; ) { } }

/* ---------- 제어 태스크 (prio 4): [1] → [2] → [3] ---------- */
static void controller( void * arg )
{
    ( void ) arg;
    printf( "[1] API semantics\n" );
    QueueHandle_t q = xQueueCreate( 3, sizeof( sample_t ) );
    sample_t a = { 0 }, b = { 0 };
    a.seq = 1; a.raw[ 0 ] = 100;
    BaseType_t r1 = xQueueSend( q, &a, 0 );
    a.seq = 99; a.raw[ 0 ] = -1;                               /* 보낸 뒤 원본을 바꾼다 */
    a.seq = 2; BaseType_t r2 = xQueueSend( q, &a, 0 );
    a.seq = 3; BaseType_t r3 = xQueueSend( q, &a, 0 );
    a.seq = 4; BaseType_t r4 = xQueueSend( q, &a, 0 );
    printf( "send x4 (depth 3, timeout 0) -> %ld %ld %ld %ld (errQUEUE_FULL=%ld); waiting=%u spaces=%u itemsize=%u\n",
            ( long ) r1, ( long ) r2, ( long ) r3, ( long ) r4, ( long ) errQUEUE_FULL,
            ( unsigned ) uxQueueMessagesWaiting( q ), ( unsigned ) uxQueueSpacesAvailable( q ),
            ( unsigned ) uxQueueGetQueueItemSize( q ) );
    TickType_t t = xTaskGetTickCount();
    BaseType_t r5 = xQueueSend( q, &a, 5 );
    printf( "send (full, timeout 5)       -> %ld after %lu ticks blocked\n", ( long ) r5, ( unsigned long ) ( xTaskGetTickCount() - t ) );
    xQueueReceive( q, &b, 0 );
    printf( "receive #1                   -> seq=%u raw0=%d   (copy taken at send time)\n", ( unsigned ) b.seq, b.raw[ 0 ] );
    xQueueReceive( q, &b, 0 ); printf( "receive #2, #3               -> seq=%u", ( unsigned ) b.seq );
    xQueueReceive( q, &b, 0 ); printf( " seq=%u\n", ( unsigned ) b.seq );
    b.seq = 777;
    BaseType_t r6 = xQueueReceive( q, &b, 0 );
    printf( "receive (empty, timeout 0)   -> %ld (errQUEUE_EMPTY=%ld), buffer seq still %u\n", ( long ) r6, ( long ) errQUEUE_EMPTY, ( unsigned ) b.seq );
    t = xTaskGetTickCount();
    BaseType_t r7 = xQueueReceive( q, &b, 5 );
    printf( "receive (empty, timeout 5)   -> %ld after %lu ticks blocked\n", ( long ) r7, ( unsigned long ) ( xTaskGetTickCount() - t ) );
    vQueueDelete( q );

    QueueHandle_t mb = xQueueCreate( 1, sizeof( logrec_t ) );
    logrec_t m = { 0 }, o = { 0 };
    BaseType_t ov[ 3 ];
    for( int i = 0; i < 3; i++ ) { m.seq = ( uint32_t ) i + 1; ov[ i ] = xQueueOverwrite( mb, &m ); }
    printf( "overwrite x3 on length-1     -> %ld %ld %ld; waiting=%u\n", ( long ) ov[ 0 ], ( long ) ov[ 1 ], ( long ) ov[ 2 ],
            ( unsigned ) uxQueueMessagesWaiting( mb ) );
    xQueuePeek( mb, &o, 0 );
    printf( "peek                         -> seq=%u, waiting=%u (peek does not remove)\n", ( unsigned ) o.seq, ( unsigned ) uxQueueMessagesWaiting( mb ) );
    xQueueReceive( mb, &o, 0 );
    printf( "receive                      -> seq=%u, waiting=%u\n", ( unsigned ) o.seq, ( unsigned ) uxQueueMessagesWaiting( mb ) );
    vQueueDelete( mb );

    printf( "\n[2] pipeline S(3) -> q_raw -> P(2) -> q_log -> L(1), N=12, period %u, proc %u, log %u ticks (K=%u)\n",
            3 * KS, KS, KS, KS );
    printf( "policy    depth produced logged sdrop pdrop ow maxlate(/K) ord data  logged seqs\n" );
    for( int d = 1; d <= 7; d++ )
    {
        g_raw_by_depth[ d ] = xQueueCreate( ( UBaseType_t ) d, sizeof( sample_t ) );
        g_log_by_depth[ d ] = xQueueCreate( ( UBaseType_t ) d, sizeof( logrec_t ) );
    }
    g_qraw = g_raw_by_depth[ 1 ]; g_qlog = g_log_by_depth[ 1 ];
    xTaskCreate( logger, "L", configMINIMAL_STACK_SIZE, NULL, 1, &g_h[ T_L ] );
    xTaskCreate( process, "P", configMINIMAL_STACK_SIZE, NULL, 2, &g_h[ T_P ] );
    xTaskCreate( sensor, "S", configMINIMAL_STACK_SIZE, NULL, 3, &g_h[ T_S ] );
    cfg_t base = { 12, 3 * KS, KS, KS, -1, 0, 4, 2, POL_BLOCK };
    run_pipeline( base, 0 );
    printf( "-- stall: logging of seq 2 takes %u ticks\n", 12 * KS );
    cfg_t st = base; st.stall_seq = 2; st.stall_cost = 12 * KS;
    run_pipeline( st, 1 );
    cfg_t sh = st; sh.raw_depth = 1; sh.log_depth = 1;
    run_pipeline( sh, 1 );
    cfg_t c = sh; c.pol = POL_DROP; run_pipeline( c, 1 );
    c.pol = POL_TIMEOUT; run_pipeline( c, 1 );
    c.pol = POL_OVERWRITE; run_pipeline( c, 1 );
    printf( "-- DROP, log depth sweep\n" );
    for( UBaseType_t d = 1; d <= 7; d++ ) { c = sh; c.pol = POL_DROP; c.log_depth = d; run_pipeline( c, 0 ); }

    printf( "\n[3] ISR (tick hook) sends every %u ticks x12, rx task needs %u CPU ticks/item, depth 4\n", KS, 3 * KS );
    g_qisr = xQueueCreate( 4, sizeof( uint32_t ) );
    xTaskCreate( bg_task, "bg", configMINIMAL_STACK_SIZE, NULL, 1, &g_h[ T_BG ] );
    xTaskCreate( rx_task, "rx", configMINIMAL_STACK_SIZE, NULL, 2, &g_h[ T_RX ] );
    vTaskDelay( KS );                                          /* rx 가 빈 큐에서 막힐 시간 */
    g_isr_n = 12;
    g_isr_base = xTaskGetTickCount() + KS;
    vTaskDelay( 60 * KS );
    printf( "isr ok %u full %u woken %u, task got %u\n", g_isr_ok, g_isr_full, g_isr_woken, g_isr_got );
    exit( 0 );
}

int main( void )
{
    xTaskCreate( controller, "ctl", configMINIMAL_STACK_SIZE, NULL, 4, NULL );
    vTaskStartScheduler();
    return 1;
}
