/* RTOS-B08 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)로 ISR → 태스크 신호를 확인한다.
 *
 * "ISR 대용": POSIX 포트는 "Scheduler timer" pthread 가 1 ms 마다 실행 중인 태스크 스레드에 SIGALRM 을
 * 보내고, 그 SIGALRM 핸들러(vPortSystemTickHandler)가 틱 ISR 이다 (port.c 452-516).
 * configUSE_TICK_HOOK=1 이면 틱 ISR 안에서 vApplicationTickHook() 이 불린다 (tasks.c 5000-5013)
 * → 여기서 xSemaphoreGiveFromISR 를 부르면 "타이머 스레드가 일으킨 ISR 에서 give" 가 된다.
 *
 * 시나리오 (starter/solution 의 결정적 모델과 같은 숫자):
 *   이벤트 5개가 1 틱 간격으로 몰려 온다 (BASE+0 .. BASE+4), 처리 태스크는 이벤트 1개에 3 틱 걸린다.
 *   ① 바이너리 세마포어  ② 카운팅(max 10)  ③ 카운팅(max 2) 를 시간 창을 나눠 차례로 돌린다.
 *   처리 태스크끼리 CPU 를 다투지 않게 창마다 한 채널만 이벤트를 받는다.
 * 실시간성은 없다(호스트). 틱 번호와 반환값·순서만 본다.
 *
 * 빌드 (md 의 명령 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_isr_sem.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define PROC   3u                     /* 이벤트 1개 처리 = 3 틱 (바쁜 루프) */
#define NBURST 5u                     /* BASE+0 .. BASE+4 에 이벤트 1개씩 */
#define MAXLOG 16

typedef struct {
    const char *name;
    SemaphoreHandle_t sem;
    TickType_t base;                  /* 이 채널의 이벤트 창 시작 틱 */
    volatile unsigned ok, fail, woken, done;
    volatile UBaseType_t max_count;   /* ISR 이 give 직후 본 최대 count */
    volatile int start[MAXLOG];       /* 처리 시작 틱 (base 기준) */
    volatile char give_mark[NBURST];  /* 'o' = pdTRUE, 'x' = errQUEUE_FULL */
} chan_t;

static chan_t g_ch[3] = {
    { "binary",      NULL,  50, 0, 0, 0, 0, 0, {0}, {0} },
    { "counting(10)", NULL, 120, 0, 0, 0, 0, 0, {0}, {0} },
    { "counting(2)",  NULL, 190, 0, 0, 0, 0, 0, {0}, {0} },
};

/* 틱 ISR 안에서 불린다 → ISR 문맥. FromISR API 만 쓴다. 블로킹 금지, printf 금지. */
void vApplicationTickHook( void )
{
    TickType_t now = xTaskGetTickCountFromISR();

    for( unsigned c = 0; c < 3; c++ )
    {
        chan_t * ch = &g_ch[ c ];

        if( ( now >= ch->base ) && ( now < ch->base + NBURST ) )
        {
            BaseType_t woken = pdFALSE;           /* 호출자가 pdFALSE 로 초기화해야 한다 */
            BaseType_t rc = xSemaphoreGiveFromISR( ch->sem, &woken );
            UBaseType_t cnt = uxSemaphoreGetCountFromISR( ch->sem );

            ch->give_mark[ now - ch->base ] = ( rc == pdTRUE ) ? 'o' : 'x';
            if( rc == pdTRUE ) ch->ok++; else ch->fail++;
            if( woken != pdFALSE ) ch->woken++;
            if( cnt > ch->max_count ) ch->max_count = cnt;
            /* 진짜 장치 ISR 이면 여기서 portYIELD_FROM_ISR( woken ). 틱 훅은 틱 ISR 이 끝에서
             * xYieldPendings 를 보고 스위칭하므로 필요 없다 (tasks.c 5564, 5020). */
        }
    }
}

static void handler( void * p )
{
    chan_t * ch = p;

    for( ; ; )
    {
        if( xSemaphoreTake( ch->sem, portMAX_DELAY ) == pdTRUE )
        {
            TickType_t t0 = xTaskGetTickCount();

            if( ch->done < MAXLOG ) ch->start[ ch->done ] = ( int ) ( t0 - ch->base );
            while( xTaskGetTickCount() - t0 < PROC ) { }   /* 처리 중: 세마포어를 기다리지 않는다 */
            ch->done++;
        }
    }
}

static void reporter( void * p )
{
    ( void ) p;
    SemaphoreHandle_t s = xSemaphoreCreateBinary();

    /* take 쪽: 비었으면 timeout 0 은 즉시 실패, timeout>0 은 그만큼 막힌다 */
    printf( "binary created: count=%u\n", ( unsigned ) uxSemaphoreGetCount( s ) );
    printf( "take(s, 0)        -> %ld (errQUEUE_EMPTY=%ld), returned at once\n",
            ( long ) xSemaphoreTake( s, 0 ), ( long ) errQUEUE_EMPTY );
    TickType_t a = xTaskGetTickCount();
    BaseType_t r = xSemaphoreTake( s, 5 );
    printf( "take(s, 5 ticks)  -> %ld after %lu ticks blocked\n", ( long ) r,
            ( unsigned long ) ( xTaskGetTickCount() - a ) );
    /* give 쪽: 태스크의 xSemaphoreGive 도 block time 0 (semGIVE_BLOCK_TIME) — 꽉 차면 즉시 실패 */
    BaseType_t g1 = xSemaphoreGive( s );
    BaseType_t g2 = xSemaphoreGive( s );
    printf( "give, give        -> %ld, %ld (errQUEUE_FULL=%ld), count=%u\n", ( long ) g1, ( long ) g2,
            ( long ) errQUEUE_FULL, ( unsigned ) uxSemaphoreGetCount( s ) );

    vTaskDelay( 260 );   /* 세 창이 다 끝날 때까지 */

    printf( "\nburst: %u events at base+0..%u (1/tick), PROC=%u ticks per event\n", NBURST, NBURST - 1, PROC );
    printf( "%-13s gives(o=ok,x=full)  ok fail woken maxcnt done  start ticks (rel. base)\n", "semaphore" );
    for( unsigned c = 0; c < 3; c++ )
    {
        chan_t * ch = &g_ch[ c ];
        printf( "%-13s %-18.*s %3u %4u %5u %6u %4u ", ch->name, ( int ) NBURST, ( const char * ) ch->give_mark,
                ch->ok, ch->fail, ch->woken, ( unsigned ) ch->max_count, ch->done );
        for( unsigned i = 0; i < ch->done && i < MAXLOG; i++ ) printf( " %d", ch->start[ i ] );
        printf( "\n" );
    }
    exit( 0 );
}

int main( void )
{
    g_ch[ 0 ].sem = xSemaphoreCreateBinary();          /* 처음엔 비어 있다 (count 0) */
    g_ch[ 1 ].sem = xSemaphoreCreateCounting( 10, 0 );
    g_ch[ 2 ].sem = xSemaphoreCreateCounting( 2, 0 );

    for( unsigned c = 0; c < 3; c++ )
    {
        xTaskCreate( handler, "h", configMINIMAL_STACK_SIZE, &g_ch[ c ], 3, NULL );
    }
    xTaskCreate( reporter, "rep", configMINIMAL_STACK_SIZE, NULL, 1, NULL );
    vTaskStartScheduler();
    return 1;
}
