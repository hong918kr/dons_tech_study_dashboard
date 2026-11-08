/* RTOS-M01 demo — Cortex-M4F(ARM_CM4F 포트, 32비트 tick) 에서 커널 객체 크기를 "컴파일만 해서" 읽는다.
 * 링크·실행하지 않는다. sizeof 를 배열 크기로 박아 두면 심볼 크기 = sizeof 가 되어 llvm-nm -S 로 보인다.
 * 실행: bash cm4/measure_cm4.sh   (cm4/FreeRTOSConfig.h + cm4/stubs/ 사용)
 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
char sz_StaticSemaphore_t[ sizeof( StaticSemaphore_t ) ];   /* == sizeof(Queue_t): queue.c:406 의 configASSERT */
char sz_StaticEventGroup_t[ sizeof( StaticEventGroup_t ) ]; /* == sizeof(EventGroup_t): event_groups.c:101 */
char sz_StaticTask_t[ sizeof( StaticTask_t ) ];             /* == sizeof(TCB_t): tasks.c:1285 */
char sz_StaticTimer_t[ sizeof( StaticTimer_t ) ];
