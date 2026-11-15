/* RTOS-M09 — demo_freertos_overflow.c 가 쓰는 훅. 커널이 이름으로 부른다.
 * 실제 제품의 스택 오버플로 훅은 로그를 남기고 리셋해야 한다(돌아와서 계속 돌면 안 된다 — 메모리가 이미 깨졌다).
 * 이 데모는 "훅이 불렸나" 를 세기만 하고 돌아간다: 훅은 vTaskSwitchContext 안(커널 임계 구역, POSIX 포트에선
 * 시그널 핸들러일 수도 있음)에서 불리므로 printf 대신 카운터와 이름 포인터만 남긴다. */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

volatile unsigned long g_overflow_hook_calls;
const char * volatile g_overflow_hook_name;

void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
    ( void ) xTask;
    g_overflow_hook_calls++;
    g_overflow_hook_name = pcTaskName;
}

void vApplicationMallocFailedHook( void )
{
    fprintf( stderr, "[MallocFailedHook]\n" );
    exit( 252 );
}
