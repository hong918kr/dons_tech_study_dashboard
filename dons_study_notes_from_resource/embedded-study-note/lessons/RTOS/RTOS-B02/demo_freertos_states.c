/* RTOS-B02 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)에서 태스크 상태 전이를 관찰한다.
 *
 * 질문 다섯 개 (모델의 설계 선택과 FreeRTOS 의 실제 동작이 같은지/다른지):
 *   Q1  eTaskGetState: 자신=eRunning, 바쁜 낮은 태스크=eReady, 타임아웃 대기·무한 대기·delay=eBlocked, 자기 suspend=eSuspended?
 *       (무한 대기 태스크는 xSuspendedTaskList 에 들어가지만 eBlocked 로 보여야 한다 — tasks.c:8717-8722, 2602-2638)
 *   Q2  BLOCKED 태스크에 vTaskResume → 아무 일도 없나? (prvTaskIsTaskSuspended 가 거름, tasks.c:3456)
 *   Q3  BLOCKED(무한 대기) 태스크를 vTaskSuspend → 그 사이 give → resume 하면? (suspend 가 event 리스트에서 뺌, 3223-3226)
 *   Q4  BLOCKED(타임아웃 50) 태스크를 suspend → 즉시 resume → 다시 BLOCKED 로 돌아가나? 마감은 처음 시각 그대로인가?
 *   Q5  xTaskAbortDelay: delay 중 → pdPASS + 조기 복귀, SUSPENDED/READY 태스크 → pdFAIL (tasks.c:4750)
 *       같은 태스크를 vTaskSuspend 두 번 → assert 없이 eSuspended 유지?
 * POSIX 포트 = 순서와 반환값만 본다. 실시간성은 없다 (호스트). tick 값은 시작 tick 기준 상대값.
 *
 * 빌드 (md 의 명령 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_states.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t semA, semB;
static TaskHandle_t hBusy, hWto, hWfor, hDly, hSus;
static volatile BaseType_t rWto = 99, rWfor = 99;     /* 99 = 아직 xSemaphoreTake 에서 안 돌아옴 */
static volatile TickType_t tWto, tWfor, tDly, t0;
static volatile int dlyDone;
static int fails;

static const char *st(eTaskState s) {
    static const char *const N[] = { "eRunning", "eReady", "eBlocked", "eSuspended", "eDeleted", "eInvalid" };
    return (unsigned)s <= eInvalid ? N[s] : "?";
}
static TickType_t rel(void) { return xTaskGetTickCount() - t0; }
static void expect(const char *what, int ok) { printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL"); fails += !ok; }

static void busy(void *p)  { (void)p; for (;;) { } }                                   /* prio 1: 늘 READY */
static void wto(void *p)   { (void)p; rWto = xSemaphoreTake(semA, 50); tWto = rel(); for (;;) vTaskDelay(1000); }
static void wfor(void *p)  { (void)p; rWfor = xSemaphoreTake(semB, portMAX_DELAY); tWfor = rel(); for (;;) vTaskDelay(1000); }
static void dly(void *p)   { (void)p; vTaskDelay(1000); dlyDone = 1; tDly = rel(); for (;;) vTaskDelay(1000); }
static void sus(void *p)   { (void)p; vTaskSuspend(NULL); for (;;) vTaskDelay(1000); }

static void ctrl(void *p) {
    (void)p;
    vTaskDelay(5);                                    /* 낮은 태스크들이 각자 대기 지점까지 가게 */
    printf("Q1 states at t=%lu\n", (unsigned long)rel());
    printf("  ctrl=%s busy=%s wto=%s wfor=%s dly=%s sus=%s\n", st(eTaskGetState(xTaskGetCurrentTaskHandle())),
           st(eTaskGetState(hBusy)), st(eTaskGetState(hWto)), st(eTaskGetState(hWfor)), st(eTaskGetState(hDly)), st(eTaskGetState(hSus)));
    expect("self eRunning, busy eReady", eTaskGetState(xTaskGetCurrentTaskHandle()) == eRunning && eTaskGetState(hBusy) == eReady);
    expect("timeout wait / forever wait / delay all eBlocked",
           eTaskGetState(hWto) == eBlocked && eTaskGetState(hWfor) == eBlocked && eTaskGetState(hDly) == eBlocked);
    expect("self-suspended eSuspended", eTaskGetState(hSus) == eSuspended);

    printf("Q2 vTaskResume(blocked wfor)\n");
    vTaskResume(hWfor);
    vTaskDelay(2);
    expect("still eBlocked, take not returned (no-op)", eTaskGetState(hWfor) == eBlocked && rWfor == 99);

    printf("Q3 suspend blocked wfor, give semB, resume\n");
    vTaskSuspend(hWfor);
    printf("  after suspend: wfor=%s\n", st(eTaskGetState(hWfor)));
    BaseType_t g = xSemaphoreGive(semB);
    vTaskDelay(2);
    printf("  give=%ld, semB count=%lu, rWfor=%ld\n", (long)g, (unsigned long)uxSemaphoreGetCount(semB), (long)rWfor);
    expect("suspended task is not woken by give (count stays 1)", g == pdPASS && uxSemaphoreGetCount(semB) == 1 && rWfor == 99);
    vTaskResume(hWfor);
    vTaskDelay(2);
    printf("  after resume: rWfor=%ld at t=%lu, semB count=%lu, wfor=%s\n", (long)rWfor, (unsigned long)tWfor,
           (unsigned long)uxSemaphoreGetCount(semB), st(eTaskGetState(hWfor)));
    expect("resumed task re-checks and takes the token (pdTRUE)", rWfor == pdTRUE && uxSemaphoreGetCount(semB) == 0);

    printf("Q4 suspend + immediate resume of wto (50-tick timeout, started at t~0)\n");
    vTaskSuspend(hWto);
    vTaskResume(hWto);
    vTaskDelay(2);
    printf("  t=%lu wto=%s rWto=%ld\n", (unsigned long)rel(), st(eTaskGetState(hWto)), (long)rWto);
    expect("re-blocked (eBlocked), take not returned yet", eTaskGetState(hWto) == eBlocked && rWto == 99);
    vTaskDelay(60);
    printf("  take returned %ld at t=%lu\n", (long)rWto, (unsigned long)tWto);
    expect("times out with pdFALSE at the ORIGINAL deadline (t<=52)", rWto == pdFALSE && tWto >= 50 && tWto <= 52);

    printf("Q5 xTaskAbortDelay / double suspend\n");
    BaseType_t a1 = xTaskAbortDelay(hDly);
    vTaskDelay(2);
    BaseType_t a2 = xTaskAbortDelay(hSus), a3 = xTaskAbortDelay(hBusy);
    printf("  abort(dly)=%ld -> dly returned at t=%lu (asked 1000); abort(sus)=%ld abort(busy)=%ld\n",
           (long)a1, (unsigned long)tDly, (long)a2, (long)a3);
    expect("abort delayed -> pdPASS, early return", a1 == pdPASS && dlyDone && tDly < 1000);
    expect("abort suspended / ready -> pdFAIL", a2 == pdFAIL && a3 == pdFAIL);
    vTaskSuspend(hSus);                               /* 이미 SUSPENDED 인 태스크를 또 */
    expect("suspend twice: no assert, still eSuspended", eTaskGetState(hSus) == eSuspended);

    printf("%s (%d fail)\n", fails ? "SOME FAIL" : "ALL PASS", fails);
    exit(fails ? 1 : 0);
}

int main(void) {
    semA = xSemaphoreCreateBinary();
    semB = xSemaphoreCreateBinary();
    xTaskCreate(busy, "busy", configMINIMAL_STACK_SIZE, NULL, 1, &hBusy);
    xTaskCreate(wto,  "wto",  configMINIMAL_STACK_SIZE, NULL, 3, &hWto);
    xTaskCreate(wfor, "wfor", configMINIMAL_STACK_SIZE, NULL, 3, &hWfor);
    xTaskCreate(dly,  "dly",  configMINIMAL_STACK_SIZE, NULL, 2, &hDly);
    xTaskCreate(sus,  "sus",  configMINIMAL_STACK_SIZE, NULL, 2, &hSus);
    xTaskCreate(ctrl, "ctrl", configMINIMAL_STACK_SIZE, NULL, 6, NULL);
    t0 = 0;
    vTaskStartScheduler();
    return 2;
}
