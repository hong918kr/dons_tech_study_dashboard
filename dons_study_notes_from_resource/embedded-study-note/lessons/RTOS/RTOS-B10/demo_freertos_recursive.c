/* RTOS-B10 demo — 재귀 뮤텍스 중첩 take/give, 그리고 일반 뮤텍스의 "자기 자신과 교착" (FreeRTOS V11.3.2, POSIX 포트)
 *
 * A(prio 2) 가 재귀 뮤텍스를 3번 take → 1번씩 give 할 때마다 B(prio 3) 가 take(0) 를 시도해
 * "깊이가 0 이 되어야 비로소 풀린다" 를 확인한다. 비소유자 GiveRecursive, 초과 give 의 반환값도 본다.
 * 마지막으로 일반 뮤텍스를 같은 태스크가 두 번 take 하면? (타임아웃 5 tick 으로 — portMAX_DELAY 면 영원히 멈춤)
 * configASSERT 켠 기본 빌드로 실행한다 (이 데모의 어떤 호출도 assert 에 걸리지 않는다).
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t rm, m;
static TaskHandle_t hA, hB;
static volatile int step;            /* A 가 올리고 B 가 따라가는 단계 번호 */

static const char *nm(TaskHandle_t h) { return h ? pcTaskGetName(h) : "NULL"; }
static void show(const char *who, const char *what, BaseType_t r) {
    printf("%-2s %-36s ret=%ld  holder=%s\n", who, what, (long)r, nm(xSemaphoreGetMutexHolder(rm)));
}

static void taskA(void *p) {
    (void)p;
    show("A", "TakeRecursive #1", xSemaphoreTakeRecursive(rm, 0));
    show("A", "TakeRecursive #2", xSemaphoreTakeRecursive(rm, 0));
    show("A", "TakeRecursive #3", xSemaphoreTakeRecursive(rm, 0));
    step = 1; vTaskDelay(2);                       /* B: take(0) 실패, 비소유자 give 실패 */
    show("A", "GiveRecursive (depth 3->2)", xSemaphoreGiveRecursive(rm));
    show("A", "GiveRecursive (depth 2->1)", xSemaphoreGiveRecursive(rm));
    step = 2; vTaskDelay(2);                       /* B: 아직 실패 */
    show("A", "GiveRecursive (depth 1->0)", xSemaphoreGiveRecursive(rm));
    step = 3; vTaskDelay(2);                       /* B: 이제 성공 */
    show("A", "GiveRecursive (extra, not holder)", xSemaphoreGiveRecursive(rm));

    /* 일반(비재귀) 뮤텍스를 같은 태스크가 두 번 take */
    printf("A  plain mutex take #1 ret=%ld\n", (long)xSemaphoreTake(m, 0));
    TickType_t t0 = xTaskGetTickCount();
    BaseType_t r = xSemaphoreTake(m, 5);
    printf("A  plain mutex take #2 (timeout 5) ret=%ld after %lu ticks  <- 자기 자신을 기다림\n",
           (long)r, (unsigned long)(xTaskGetTickCount() - t0));
    printf("A  plain mutex give  ret=%ld, give again ret=%ld\n", (long)xSemaphoreGive(m),
           (long)xSemaphoreGive(m));
    printf("done\n");
    exit(0);
}
static void taskB(void *p) {
    (void)p;
    for (int seen = 0; seen < 3;) {
        if (step == seen) { vTaskDelay(1); continue; }
        seen = step;
        if (seen == 1) {
            show("B", "TakeRecursive(0) while depth 3", xSemaphoreTakeRecursive(rm, 0));
            show("B", "GiveRecursive WITHOUT owning", xSemaphoreGiveRecursive(rm));
        } else if (seen == 2) {
            show("B", "TakeRecursive(0) while depth 1", xSemaphoreTakeRecursive(rm, 0));
        } else {
            show("B", "TakeRecursive(0) after depth 0", xSemaphoreTakeRecursive(rm, 0));
            show("B", "GiveRecursive (B is holder)", xSemaphoreGiveRecursive(rm));
        }
    }
    vTaskDelete(NULL);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    rm = xSemaphoreCreateRecursiveMutex();
    m = xSemaphoreCreateMutex();
    xTaskCreate(taskA, "A", configMINIMAL_STACK_SIZE, NULL, 2, &hA);
    xTaskCreate(taskB, "B", configMINIMAL_STACK_SIZE, NULL, 3, &hB);
    vTaskStartScheduler();
    return 1;
}
