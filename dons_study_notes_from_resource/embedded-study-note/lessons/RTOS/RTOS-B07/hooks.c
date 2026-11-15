/* RTOS-B07 — FreeRTOSConfig.h 가 켠 훅들. 커널이 이름으로 부르므로 이름·시그니처를 바꾸면 링크 에러.
 * 카운터는 starter/solution/practice/demo 가 extern 으로 읽어 "불렸나" 를 검사한다. */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

volatile unsigned long g_idle_hook_calls;       /* configUSE_IDLE_HOOK = 1 */
volatile unsigned long g_stack_overflow_calls;  /* configCHECK_FOR_STACK_OVERFLOW = 2 */

/* idle 태스크 루프에서 매번 불린다 (tasks.c 5968-5973). 막히는 API 금지 — idle 이 막히면 READY 태스크가
 * 하나도 없는 순간이 생긴다. 실제 MCU 라면 여기서 __WFI() 로 잠들거나 워치독 조건을 모은다. */
void vApplicationIdleHook( void )
{
    g_idle_hook_calls++;
}

/* pvPortMalloc 실패 시 (tasks.c xTaskCreate → heap_3.c). 실패한 xTaskCreate 는 errCOULD_NOT_ALLOCATE... 를
 * 돌려주지만 반환값을 안 보는 코드가 많아서, 훅에서 크게 알린다. */
void vApplicationMallocFailedHook( void )
{
    fprintf( stderr, "[MallocFailedHook]\n" );
    exit( 252 );
}

/* 컨텍스트 전환 때 스택 끝 패턴이 깨졌으면 불린다 (include/stack_macros.h 99-118).
 * MCU 라면 여기서 로그를 남기고 리셋한다. 돌아와서 계속 돌면 안 된다 — 이미 메모리가 깨졌다. */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
    ( void ) xTask;
    g_stack_overflow_calls++;
    fprintf( stderr, "[StackOverflowHook] %s\n", pcTaskName );
    abort();
}
