/* RTOS-B10 demo — 바이너리 세마포어로 상호배제를 하면 생기는 문제 (FreeRTOS V11.3.2, POSIX 포트)
 *
 *   ./demo_binsem 1    ① 처음엔 비어 있다  ② 아무나 give 할 수 있다 → 둘이 동시에 임계 구역
 *   ./demo_binsem 2    L/M/H 우선순위 역전 — 락 = 바이너리 세마포어 (상속 없음)
 *   ./demo_binsem 3    같은 시나리오 — 락 = 뮤텍스 (우선순위 상속)
 *
 * configASSERT 켠 기본 빌드. POSIX 포트는 실시간성이 없으므로 tick 번호는 ±1 흔들릴 수 있다 —
 * 보는 것은 "누가 먼저 끝나나(순서)" 와 "L 의 우선순위 값" 이다.
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t lk;
static TaskHandle_t hL, hM, hH;
static volatile int in_cs;
static volatile UBaseType_t l_max_prio;
static char order[64];
static void mark(const char *s) {               /* 끝난 순서 기록 */
    size_t n = 0; while (order[n]) n++;
    while (*s && n + 1 < sizeof order) order[n++] = *s++;
    order[n] = '\0';
}
static void spin(TickType_t ticks) {            /* CPU 일 (선점 가능) */
    TickType_t until = xTaskGetTickCount() + ticks;
    while (xTaskGetTickCount() < until) {
        UBaseType_t p = uxTaskPriorityGet(NULL);
        if (p > l_max_prio && xTaskGetCurrentTaskHandle() == hL) l_max_prio = p;
    }
}

/* ---------- 시나리오 1 ---------- */
static void t1A(void *p) {                      /* prio 2: 락을 잡고 일하는 태스크 */
    (void)p;
    printf("A  take on fresh binary sem (timeout 0)  ret=%ld   <- 처음엔 비어 있다\n", (long)xSemaphoreTake(lk, 0));
    printf("A  give (make it 'unlocked')             ret=%ld\n", (long)xSemaphoreGive(lk));
    printf("A  give again (already 1)                ret=%ld   <- 최대 1, 넘치면 실패\n", (long)xSemaphoreGive(lk));
    BaseType_t r = xSemaphoreTake(lk, portMAX_DELAY); in_cs++;
    printf("A  take = lock                           ret=%ld   in_cs=%d\n", (long)r, in_cs);
    vTaskDelay(10);
    printf("A  back from work, still in CS                    in_cs=%d\n", in_cs);
    in_cs--; printf("A  give = unlock                         ret=%ld\n", (long)xSemaphoreGive(lk));
    vTaskDelay(20); printf("done\n"); exit(0);
}
static void t1B(void *p) {                      /* prio 3: 남의 락을 give 하는 버그 */
    (void)p;
    vTaskDelay(2);
    printf("B  give WITHOUT having taken             ret=%ld   <- 세마포어엔 소유자가 없다 (assert 도 없음)\n",
           (long)xSemaphoreGive(lk));
    BaseType_t r = xSemaphoreTake(lk, 0); if (r == pdPASS) in_cs++;
    printf("B  take = lock                           ret=%ld   in_cs=%d   <- 상호배제 깨짐\n", (long)r, in_cs);
    vTaskDelay(15);
    in_cs--; printf("B  give = unlock                         ret=%ld\n", (long)xSemaphoreGive(lk));
    vTaskDelete(NULL);
}

/* ---------- 시나리오 2/3: L(1) 이 락, H(3) 대기, M(2) CPU 작업 ---------- */
static void tL(void *p) {
    (void)p;
    xSemaphoreTake(lk, portMAX_DELAY);
    printf("t=%2lu L takes lock\n", (unsigned long)xTaskGetTickCount());
    spin(6);
    xSemaphoreGive(lk);
    printf("t=%2lu L gives lock (max prio seen while holding = %lu)\n",
           (unsigned long)xTaskGetTickCount(), (unsigned long)l_max_prio);
    mark("L ");
    vTaskDelay(40);
    printf("finish order: %s\n", order);
    exit(0);
}
static void tH(void *p) {
    (void)p;
    vTaskDelay(2);
    TickType_t t0 = xTaskGetTickCount();
    printf("t=%2lu H requests lock\n", (unsigned long)t0);
    xSemaphoreTake(lk, portMAX_DELAY);
    printf("t=%2lu H got lock after %lu ticks\n", (unsigned long)xTaskGetTickCount(),
           (unsigned long)(xTaskGetTickCount() - t0));
    xSemaphoreGive(lk);
    mark("H ");
    vTaskDelete(NULL);
}
static void tM(void *p) {
    (void)p;
    vTaskDelay(3);
    printf("t=%2lu M starts 15 ticks of CPU work\n", (unsigned long)xTaskGetTickCount());
    spin(15);
    printf("t=%2lu M done\n", (unsigned long)xTaskGetTickCount());
    mark("M ");
    vTaskDelete(NULL);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int sc = argc > 1 ? atoi(argv[1]) : 1;
    if (sc == 1) {
        lk = xSemaphoreCreateBinary();
        xTaskCreate(t1A, "A", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
        xTaskCreate(t1B, "B", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    } else {
        if (sc == 2) { lk = xSemaphoreCreateBinary(); xSemaphoreGive(lk); printf("lock = binary semaphore\n"); }
        else         { lk = xSemaphoreCreateMutex();                    printf("lock = mutex\n"); }
        xTaskCreate(tL, "L", configMINIMAL_STACK_SIZE, NULL, 1, &hL);
        xTaskCreate(tH, "H", configMINIMAL_STACK_SIZE, NULL, 3, &hH);
        xTaskCreate(tM, "M", configMINIMAL_STACK_SIZE, NULL, 2, &hM);
    }
    vTaskStartScheduler();
    return 1;
}
