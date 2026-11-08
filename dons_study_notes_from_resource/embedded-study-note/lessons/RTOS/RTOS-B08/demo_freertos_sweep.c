/* RTOS-B08 데모 2 — 진짜 FreeRTOS(V11.3.2, POSIX 포트)로 연습 1 의 sweep 을 다시 잰다.
 *
 * 틱 훅(= 틱 ISR 안)이 주기 E=3 틱으로 이벤트 12개를 xSemaphoreGiveFromISR 한다.
 * 처리 태스크는 이벤트 1개에 P 틱(바쁜 루프). P = 1..8, 바이너리 / 카운팅(16) 각각.
 * 서로 CPU 를 다투지 않게 (P, 종류) 마다 110 틱짜리 시간 창을 따로 준다 → 16개 창.
 * 비교 대상: practice.c 의 결정적 모델과 공식 1 + ceil((N-1)E/P).
 *
 * 빌드: demo_freertos_isr_sem.c 와 같은 줄에서 파일 이름만 바꾼다.
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define E      3u
#define NEV    12u
#define WIN    110u
#define FIRST  50u
#define NWIN   16u                     /* P=1..8 × {binary, counting} */

typedef struct {
    SemaphoreHandle_t sem;
    TickType_t base;
    unsigned proc;
    volatile unsigned ok, fail, done;
} win_t;

static win_t g_w[ NWIN ];

void vApplicationTickHook( void )      /* 틱 ISR 문맥 */
{
    TickType_t now = xTaskGetTickCountFromISR();

    if( now < FIRST ) return;
    unsigned k = ( unsigned ) ( ( now - FIRST ) / WIN );
    if( k >= NWIN ) return;
    win_t * w = &g_w[ k ];
    TickType_t rel = now - w->base;

    if( ( rel % E == 0u ) && ( rel / E < NEV ) )
    {
        BaseType_t woken = pdFALSE;
        if( xSemaphoreGiveFromISR( w->sem, &woken ) == pdTRUE ) w->ok++; else w->fail++;
    }
}

static void handler( void * p )
{
    win_t * w = p;

    for( ; ; )
    {
        if( xSemaphoreTake( w->sem, portMAX_DELAY ) == pdTRUE )
        {
            TickType_t t0 = xTaskGetTickCount();
            while( xTaskGetTickCount() - t0 < w->proc ) { }
            w->done++;
        }
    }
}

static void reporter( void * p )
{
    ( void ) p;
    vTaskDelay( FIRST + NWIN * WIN + 5 );
    printf( "E=%u, N=%u periodic events, P ticks per event (FreeRTOS POSIX port)\n", E, NEV );
    printf( " P  binary(done/ok/fail)  counting16(done/ok/fail)  formula\n" );
    for( unsigned pp = 1; pp <= 8; pp++ )
    {
        win_t * b = &g_w[ ( pp - 1 ) * 2 ];
        win_t * c = &g_w[ ( pp - 1 ) * 2 + 1 ];
        unsigned f = ( pp <= E ) ? NEV : 1u + ( ( NEV - 1u ) * E + pp - 1u ) / pp;
        printf( " %u  %4u /%3u /%3u         %4u /%3u /%3u          %3u\n", pp, b->done, b->ok, b->fail,
                c->done, c->ok, c->fail, f );
    }
    exit( 0 );
}

int main( void )
{
    for( unsigned k = 0; k < NWIN; k++ )
    {
        g_w[ k ].proc = k / 2 + 1;
        g_w[ k ].base = FIRST + k * WIN;
        g_w[ k ].sem = ( k % 2 == 0 ) ? xSemaphoreCreateBinary() : xSemaphoreCreateCounting( 16, 0 );
        xTaskCreate( handler, "h", configMINIMAL_STACK_SIZE, &g_w[ k ], 3, NULL );
    }
    xTaskCreate( reporter, "rep", configMINIMAL_STACK_SIZE, NULL, 1, NULL );
    vTaskStartScheduler();
    return 1;
}
