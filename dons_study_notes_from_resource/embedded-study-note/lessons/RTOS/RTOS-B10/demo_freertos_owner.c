/* RTOS-B10 demo — 소유자가 아닌 태스크가 뮤텍스를 give 하면? (FreeRTOS V11.3.2, POSIX 포트)
 *
 *   ./demo_owner 1   기본 우선순위: A 가 잡은 뮤텍스를 B 가 give → B 가 take → 둘 다 임계 구역
 *   ./demo_owner 2   우선순위 상속 중: L 이 잡고 H 가 대기(L 상속) → X 가 give → L 의 상속을 X 가 풀어 버림
 *
 * configASSERT 켠 빌드(기본): 비소유자 give 순간 tasks.c 의 configASSERT( pxTCB == pxCurrentTCB ) 에서 abort.
 * -DDEMO_NO_ASSERT 빌드: assert 없이 그대로 진행 → 무엇이 깨지는지 출력.
 * POSIX 포트는 실시간성이 없다 — 여기서 보는 것은 "순서와 반환값" 뿐이다 (vTaskDelay 는 순서 맞추기용).
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t m;
static TaskHandle_t hA, hB, hL, hH, hX;
static volatile int in_cs;           /* 임계 구역 안에 있다고 믿는 태스크 수 */

static const char *nm(TaskHandle_t h) { return h ? pcTaskGetName(h) : "NULL"; }
static void show(const char *who, const char *what, BaseType_t r) {
    printf("t=%2lu %-2s %-34s ret=%ld  holder=%-4s in_cs=%d\n", (unsigned long)xTaskGetTickCount(),
           who, what, (long)r, nm(xSemaphoreGetMutexHolder(m)), in_cs);
}

/* ---------- 시나리오 1: 기본 우선순위, 상속 없음 ---------- */
static void taskA(void *p) {            /* prio 2: 정상 사용자 */
    (void)p;
    BaseType_t r = xSemaphoreTake(m, portMAX_DELAY); in_cs++;
    show("A", "take (enter CS)", r);
    vTaskDelay(10);                     /* 임계 구역에서 일하는 중 (B 가 이 사이에 실행) */
    show("A", "still in CS, back from work", 0);
    in_cs--; r = xSemaphoreGive(m);
    show("A", "give (leave CS)", r);
    vTaskDelay(20);
    printf("done\n"); exit(0);
}
static void taskB(void *p) {            /* prio 3: 버그 있는 정리 코드 — 남의 뮤텍스를 give */
    (void)p;
    vTaskDelay(2);
    show("B", "sees A holding", 0);
    BaseType_t r = xSemaphoreGive(m);   /* 비소유자 give */
    show("B", "give WITHOUT owning", r);
    r = xSemaphoreTake(m, 0); if (r == pdPASS) in_cs++;
    show("B", "take (enter CS)", r);
    vTaskDelay(15);
    in_cs--; r = xSemaphoreGive(m);
    show("B", "give (leave CS)", r);
    vTaskDelete(NULL);
}

/* ---------- 시나리오 2: 우선순위 상속이 걸린 상태에서 제3자가 give ---------- */
static void taskL(void *p) {            /* prio 1 */
    (void)p;
    BaseType_t r = xSemaphoreTake(m, portMAX_DELAY); in_cs++;
    show("L", "take (enter CS)", r);
    TickType_t until = xTaskGetTickCount() + 8;
    while (xTaskGetTickCount() < until) { }        /* CPU 일 (선점 가능) */
    printf("t=%2lu L  my prio=%lu (base 1)\n", (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxTaskPriorityGet(NULL));
    in_cs--; r = xSemaphoreGive(m);
    show("L", "give (leave CS)", r);
    vTaskDelay(20);
    printf("done\n"); exit(0);
}
static void taskH(void *p) {            /* prio 3 */
    (void)p;
    vTaskDelay(2);
    show("H", "take -> blocks", 0);
    BaseType_t r = xSemaphoreTake(m, portMAX_DELAY); in_cs++;
    show("H", "got it (enter CS)", r);
    vTaskDelay(3);
    in_cs--; r = xSemaphoreGive(m);
    show("H", "give (leave CS)", r);
    vTaskDelete(NULL);
}
static void taskX(void *p) {            /* prio 4: 비소유자 */
    (void)p;
    vTaskDelay(4);
    printf("t=%2lu X  L prio before = %lu (inherited from H)\n", (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxTaskPriorityGet(hL));
    BaseType_t r = xSemaphoreGive(m);
    show("X", "give WITHOUT owning", r);
    printf("t=%2lu X  L prio after  = %lu\n", (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxTaskPriorityGet(hL));
    vTaskDelete(NULL);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* abort 전에 출력이 버퍼에 남지 않게 */
    int sc = argc > 1 ? atoi(argv[1]) : 1;
#ifdef DEMO_NO_ASSERT
    printf("build: configASSERT OFF, scenario %d\n", sc);
#else
    printf("build: configASSERT ON, scenario %d\n", sc);
#endif
    m = xSemaphoreCreateMutex();
    if (sc == 1) {
        xTaskCreate(taskA, "A", configMINIMAL_STACK_SIZE, NULL, 2, &hA);
        xTaskCreate(taskB, "B", configMINIMAL_STACK_SIZE, NULL, 3, &hB);
    } else {
        xTaskCreate(taskL, "L", configMINIMAL_STACK_SIZE, NULL, 1, &hL);
        xTaskCreate(taskH, "H", configMINIMAL_STACK_SIZE, NULL, 3, &hH);
        xTaskCreate(taskX, "X", configMINIMAL_STACK_SIZE, NULL, 4, &hX);
    }
    vTaskStartScheduler();
    return 1;
}
