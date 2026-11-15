/* RTOS-B11 데모 2 — 진짜 FreeRTOS(V11.3.2, POSIX 포트)로 "큰 버퍼는 포인터 + 고정 블록 풀" 을 확인한다.
 *
 * 풀 = "빈 블록 포인터의 큐" (free_q). 이 방식이면 alloc/free 가 그냥 큐 receive/send 라서 ISR 에서도
 * xQueueReceiveFromISR / xQueueSendFromISR 로 쓸 수 있고, 따로 임계 구역을 짤 필요가 없다.
 *   생산자(prio 3, 3K 틱마다):  free_q 에서 블록 받기(0 대기) → 채우기 → full_q 로 포인터 send(0 대기)
 *                                send 실패면 소유권이 안 넘어갔다 → 블록을 free_q 로 직접 돌려준다.
 *   소비자(prio 1):             full_q 에서 포인터 받기 → 7K CPU 틱 처리 → 검사 → free_q 로 반납.
 * solution.c 모델(run_ptr_pipeline: 주기 3, 처리 7, 풀 4, 큐 깊이 4 / 1)과 같은 시나리오를 K 배 시간으로.
 *
 * [2] 소유권 위반(use-after-send): 보낸 뒤 블록에 쓰는 버그가 "보이느냐" 는 우선순위에 달렸다.
 *
 * 태스크는 한 번만 만들어 재사용한다 (vTaskDelete 교착 회피 — demo_freertos_queue.c 머리말 참고).
 * 우선순위는 실행마다 vTaskPrioritySet 으로 바꾼다.
 *
 * 빌드: demo_freertos_queue.c 와 같은 줄에서 파일 이름만 바꾼다.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define KS     10u
#define POOL_N 4u
#define BLK_SIZE 256u
typedef struct { uint32_t seq; uint8_t data[ BLK_SIZE ]; } blk_t;

static blk_t g_blocks[ POOL_N ];               /* 정적 저장소: 힙을 안 쓴다 */
static QueueHandle_t g_free_q;
static QueueHandle_t volatile g_full_q;          /* 이번 실행이 쓰는 포인터 큐 (깊이별로 미리 만든 것) */
static QueueHandle_t g_full_by_depth[ 5 ];
static volatile uint32_t g_cost;
static TaskHandle_t g_hp, g_hc;
static volatile uint32_t g_cpu_c;
static volatile unsigned produced, sent, nomem, dropfull, consumed, bad, maxinuse;
static volatile int g_bug, g_done;
static int g_n;

void vApplicationTickHook( void )
{
    if( ( g_hc != NULL ) && ( xTaskGetCurrentTaskHandle() == g_hc ) ) g_cpu_c++;   /* 소비자의 CPU 틱 */
}
static uint8_t pat( uint32_t seq, unsigned i ) { return ( uint8_t ) ( seq * 31u + i ); }

static void producer( void * arg )
{
    ( void ) arg;

    for( ; ; )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );        /* "시작" */
        TickType_t last = xTaskGetTickCount();

        for( int seq = 0; seq < g_n; seq++ )
        {
            blk_t * b;

            if( xQueueReceive( g_free_q, &b, 0 ) != pdPASS )      /* pool_alloc: 비었으면 기다리지 않는다 */
            {
                nomem++;
            }
            else
            {
                unsigned inuse = POOL_N - ( unsigned ) uxQueueMessagesWaiting( g_free_q );
                if( inuse > maxinuse ) maxinuse = inuse;
                b->seq = ( uint32_t ) seq;
                for( unsigned i = 0; i < BLK_SIZE; i++ ) b->data[ i ] = pat( ( uint32_t ) seq, i );

                if( xQueueSend( g_full_q, &b, 0 ) == pdPASS )       /* 포인터 8 B 만 복사된다 */
                {
                    sent++;
                    if( g_bug ) b->data[ 0 ] ^= 0xFF;                /* 버그: 이미 소비자 것인 블록에 쓴다 */
                }
                else
                {
                    ( void ) xQueueSend( g_free_q, &b, 0 );          /* 소유권이 안 넘어갔다 → 내가 반납 */
                    dropfull++;
                }
            }
            produced++;
            if( seq + 1 < g_n ) xTaskDelayUntil( &last, 3 * KS );
        }
    }
}

static void consumer( void * arg )
{
    ( void ) arg;
    blk_t * b;

    for( ; ; )
    {
        if( xQueueReceive( g_full_q, &b, KS ) != pdPASS ) continue;   /* 실행 사이: 큐가 바뀌었을 수 있다 */
        uint32_t s = g_cpu_c;
        while( g_cpu_c - s < g_cost ) { }                         /* 처리 (FFT, 압축 ...) */
        int ok = 1;
        for( unsigned i = 0; i < BLK_SIZE; i++ ) if( b->data[ i ] != pat( b->seq, i ) ) ok = 0;
        if( !ok ) bad++;
        consumed++;
        if( ( int ) b->seq == g_n - 1 ) g_done = 1;
        ( void ) xQueueSend( g_free_q, &b, 0 );                 /* pool_free: 다 쓴 쪽이 반납 */
    }
}

static void run( int n, uint32_t cost, UBaseType_t qdepth, UBaseType_t prod_prio, UBaseType_t cons_prio, int bug )
{
    g_n = n; g_bug = bug; g_done = 0; g_cost = cost;
    produced = sent = nomem = dropfull = consumed = bad = maxinuse = 0;
    xQueueReset( g_free_q );
    g_full_q = g_full_by_depth[ qdepth ];
    xQueueReset( g_full_q );
    for( unsigned i = 0; i < POOL_N; i++ ) { blk_t * b = &g_blocks[ i ]; xQueueSend( g_free_q, &b, 0 ); }
    vTaskPrioritySet( g_hc, cons_prio );
    vTaskPrioritySet( g_hp, prod_prio );
    vTaskDelay( 3 * KS );                              /* 소비자가 새 큐를 집어 들 시간 */
    xTaskNotifyGive( g_hp );

    TickType_t t0 = xTaskGetTickCount();
    while( xTaskGetTickCount() - t0 < ( TickType_t ) ( 3 * KS * ( unsigned ) n + 10 * cost ) ) vTaskDelay( KS );

    unsigned freeleft = ( unsigned ) uxQueueMessagesWaiting( g_free_q );
    printf( "cost %3u qdepth %u prio p%u/c%u bug %d | produced %2u sent %2u nomem %u dropfull %u consumed %2u corrupt %u"
            " | max in use %u, free at end %u/%u\n",
            ( unsigned ) cost, ( unsigned ) qdepth, ( unsigned ) prod_prio, ( unsigned ) cons_prio, bug,
            produced, sent, nomem, dropfull, consumed, bad, maxinuse, freeleft, POOL_N );
}

static void controller( void * arg )
{
    ( void ) arg;
    printf( "item size: copy queue would move %u B per item; pointer queue moves %u B\n",
            ( unsigned ) sizeof( blk_t ), ( unsigned ) sizeof( blk_t * ) );
    g_free_q = xQueueCreate( POOL_N, sizeof( blk_t * ) );            /* 풀 = 빈 블록 포인터의 큐 */
    for( UBaseType_t d = 1; d <= 4; d++ ) g_full_by_depth[ d ] = xQueueCreate( d, sizeof( blk_t * ) );
    g_full_q = g_full_by_depth[ 1 ];
    xTaskCreate( consumer, "cons", configMINIMAL_STACK_SIZE, NULL, 1, &g_hc );
    xTaskCreate( producer, "prod", configMINIMAL_STACK_SIZE, NULL, 3, &g_hp );
    printf( "[1] pool of %u blocks, producer every %u ticks, N=10\n", POOL_N, 3 * KS );
    run( 10, 2 * KS, 2, 3, 1, 0 );                 /* 소비자 충분히 빠름 */
    run( 10, 7 * KS, 4, 3, 1, 0 );                 /* 소비자 느림, 큐 4: 풀이 먼저 마른다 */
    run( 10, 7 * KS, 1, 3, 1, 0 );                 /* 소비자 느림, 큐 1: 큐가 먼저 찬다 */
    printf( "[2] use-after-send bug (producer writes data[0] after xQueueSend)\n" );
    run( 6, 2 * KS, 2, 3, 1, 1 );                  /* 소비자가 낮다: 생산자가 먼저 낙서 → 깨진 데이터 */
    run( 6, 2 * KS, 2, 1, 3, 1 );                  /* 소비자가 높다: send 즉시 소비자가 다 읽음 → 안 보임 */
    exit( 0 );
}

int main( void )
{
    xTaskCreate( controller, "ctl", configMINIMAL_STACK_SIZE, NULL, 4, NULL );
    vTaskStartScheduler();
    return 1;
}
