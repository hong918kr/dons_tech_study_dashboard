/* RTOS-M01 demo — 진짜 FreeRTOS(V11.3.2, POSIX 포트)로 알림 의미론을 확인한다.
 * solution.c 의 모델 테스트와 같은 시나리오를 돌려 "모델 예측 == 커널 결과" 인지 한 줄씩 찍는다.
 * POSIX 포트는 한 번에 FreeRTOS 스레드 하나만 돌리므로(나머지는 정지) 순서는 결정적이다. 시간은 보지 않는다.
 *
 * K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 * cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_notify.c hooks.c \
 *    $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
 *    -o /tmp/RTOS-M01_dn && /tmp/RTOS-M01_dn
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

static int g_ok, g_n;
static void expect(const char *what, unsigned long got, unsigned long want) {
    g_n++;
    if (got == want) g_ok++;
    printf("[%s] %-58s got=%-10lu model=%lu\n", got == want ? " OK " : "DIFF", what, got, want);
}
#define PEEK() ((unsigned long)ulTaskNotifyValueClear(NULL, 0))   /* 비트 0개 지우기 = 값 엿보기 (상태 안 바뀜) */

/* ---- 보조 태스크들: 알림을 기다리다 깨어나면 결과를 전역에 남긴다 ---- */
static volatile unsigned long w_ret, w_runs;            /* waiter W (prio 3) */
static void waiter_take(void *p) {
    (void)p;
    for (;;) { w_ret = ulTaskNotifyTake(pdTRUE, portMAX_DELAY); w_runs++; }
}
static volatile unsigned long l_runs;                     /* low waiter L (prio 1) */
static void low_take(void *p) {
    (void)p;
    for (;;) { (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY); l_runs++; }
}
static volatile unsigned long r_cnt[2];                   /* R1, R2: 둘 다 "같은 이벤트" 를 원함 */
static void rx_receiver(void *p) {
    int id = (int)(intptr_t)p;
    for (;;) { if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) r_cnt[id]++; }
}
static EventGroupHandle_t g_eg;
static volatile unsigned long e_cnt[2];
static void eg_receiver(void *p) {
    int id = (int)(intptr_t)p;
    for (;;) {
        EventBits_t b = xEventGroupWaitBits(g_eg, 0x1, pdFALSE, pdFALSE, portMAX_DELAY);
        if (b & 0x1) e_cnt[id]++;
        while (xEventGroupGetBits(g_eg) & 0x1) vTaskDelay(1);   /* 비트가 내려갈 때까지 대기 */
    }
}
static volatile unsigned long x_take, x_wait_ret, x_wait_val, x_stage;   /* X: 인덱스 1 대기 후 인덱스 0 확인 */
static void idx_task(void *p) {
    (void)p;
    uint32_t v = 0;
    x_stage = 1;
    x_take = ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);           /* 드라이버: TX 완료 대기 */
    x_stage = 2;
    x_wait_ret = (unsigned long)xTaskNotifyWaitIndexed(0, 0, 0xFFFFFFFFu, &v, 0);   /* 앱 이벤트 확인 */
    x_wait_val = v;
    x_stage = 3;
    vTaskSuspend(NULL);
}

static void ctrl(void *p) {
    (void)p;
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    uint32_t v = 0;

    puts("-- A1 eIncrement + ulTaskNotifyTake (decrement vs clear)");
    for (int i = 0; i < 5; i++) xTaskNotifyGive(me);
    expect("Take(pdFALSE) after 5 gives returns", ulTaskNotifyTake(pdFALSE, 0), 5);
    expect("Take(pdFALSE) again returns", ulTaskNotifyTake(pdFALSE, 0), 4);
    expect("Take(pdTRUE) returns", ulTaskNotifyTake(pdTRUE, 0), 3);
    expect("Take(pdTRUE) on empty returns", ulTaskNotifyTake(pdTRUE, 0), 0);

    puts("-- A2 eSetValueWithoutOverwrite / eSetValueWithOverwrite");
    expect("NoOverwrite(11) on empty -> pdPASS", (unsigned long)xTaskNotify(me, 11, eSetValueWithoutOverwrite), pdPASS);
    expect("NoOverwrite(22) while unread -> pdFAIL", (unsigned long)xTaskNotify(me, 22, eSetValueWithoutOverwrite), pdFAIL);
    expect("Wait(0,0) -> pdTRUE", (unsigned long)xTaskNotifyWait(0, 0, &v, 0), pdTRUE);
    expect("  value read", v, 11);
    expect("NoOverwrite(33) after read -> pdPASS", (unsigned long)xTaskNotify(me, 33, eSetValueWithoutOverwrite), pdPASS);
    expect("Overwrite(44) while unread -> pdPASS", (unsigned long)xTaskNotify(me, 44, eSetValueWithOverwrite), pdPASS);
    (void)xTaskNotifyWait(0, 0xFFFFFFFFu, &v, 0);
    expect("  value read (latest wins)", v, 44);

    puts("-- A3 eSetBits + xTaskNotifyWait masks");
    xTaskNotify(me, 0x1, eSetBits);
    xTaskNotify(me, 0x4, eSetBits);
    expect("Wait(entry 0, exit 0x1) -> pdTRUE", (unsigned long)xTaskNotifyWait(0, 0x1, &v, 0), pdTRUE);
    expect("  *pulNotificationValue = value BEFORE exit clear", v, 0x5);
    expect("  value after exit clear", PEEK(), 0x4);
    expect("Wait(~0,~0,ticks=0), nothing pending -> pdFALSE", (unsigned long)xTaskNotifyWait(0xFFFFFFFFu, 0xFFFFFFFFu, &v, 0), pdFALSE);
    expect("  entry clear NOT applied when it does not block", PEEK(), 0x4);
    xTaskNotify(me, 0x2, eSetBits);
    expect("Wait(entry ~0, exit 0, ticks=10) with pending -> pdTRUE", (unsigned long)xTaskNotifyWait(0xFFFFFFFFu, 0, &v, 10), pdTRUE);
    expect("  pending bits kept (no entry clear)", v, 0x6);
    expect("Wait(entry ~0, exit ~0, ticks=5) nothing pending -> timeout", (unsigned long)xTaskNotifyWait(0xFFFFFFFFu, 0xFFFFFFFFu, &v, 5), pdFALSE);
    expect("  entry clear applied because it blocked", v, 0);

    puts("-- B  mixing Take with bits, eIncrement wrap");
    xTaskNotify(me, 0x8, eSetBits);
    expect("Take(pdTRUE) on bit value 0x8 returns", ulTaskNotifyTake(pdTRUE, 0), 8);
    xTaskNotify(me, 0xFFFFFFFFu, eSetValueWithOverwrite);
    xTaskNotifyGive(me);
    expect("0xFFFFFFFF + eIncrement wraps to", PEEK(), 0);
    expect("  Take then sees", ulTaskNotifyTake(pdTRUE, 0), 0);

    puts("-- B  eNoAction wakes a Take waiter, Take returns 0");
    TaskHandle_t w, l;
    xTaskCreate(waiter_take, "W", configMINIMAL_STACK_SIZE, NULL, 3, &w);   /* 바로 실행 → 블록 */
    xTaskNotify(w, 0, eNoAction);
    expect("W ran once", w_runs, 1);
    expect("  ulTaskNotifyTake returned", w_ret, 0);

    puts("-- B  FromISR: *pxHigherPriorityTaskWoken only set when woken prio > running prio");
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(w, &hp);           /* W(3) > ctrl(2) */
    expect("give to blocked W(prio 3) from ctrl(prio 2): hptw", (unsigned long)hp, pdTRUE);
    portYIELD_FROM_ISR(hp);
    expect("  after portYIELD_FROM_ISR W ran, Take returned", w_ret, 1);
    xTaskCreate(low_take, "L", configMINIMAL_STACK_SIZE, NULL, 1, &l);
    vTaskDelay(2);                            /* L 이 돌아서 블록하게 */
    hp = pdFALSE;
    vTaskNotifyGiveFromISR(l, &hp);           /* L(1) < ctrl(2) */
    expect("give to blocked L(prio 1): hptw stays", (unsigned long)hp, pdFALSE);
    vTaskDelay(2);
    expect("  L ran later", l_runs, 1);

    puts("-- B  single receiver: notification vs event group broadcast");
    TaskHandle_t r1, r2;
    xTaskCreate(rx_receiver, "R1", configMINIMAL_STACK_SIZE, (void *)0, 3, &r1);
    xTaskCreate(rx_receiver, "R2", configMINIMAL_STACK_SIZE, (void *)1, 3, &r2);
    xTaskNotifyGive(r1);                      /* 알림은 TCB 하나를 가리킨다 */
    expect("notify R1: R1 woke", r_cnt[0], 1);
    expect("  R2 woke", r_cnt[1], 0);
    expect("  R2 still eBlocked", (unsigned long)eTaskGetState(r2), eBlocked);
    g_eg = xEventGroupCreate();
    xTaskCreate(eg_receiver, "E1", configMINIMAL_STACK_SIZE, (void *)0, 3, NULL);
    xTaskCreate(eg_receiver, "E2", configMINIMAL_STACK_SIZE, (void *)1, 3, NULL);
    xEventGroupSetBits(g_eg, 0x1);            /* 한 번 set → 기다리던 둘 다 깨어남 */
    expect("one xEventGroupSetBits: E1 woke", e_cnt[0], 1);
    expect("  E2 woke", e_cnt[1], 1);
    xEventGroupClearBits(g_eg, 0x1);

    puts("-- C  two indexes: index-0 bits do not wake a task blocked on index 1");
    TaskHandle_t x;
    xTaskCreate(idx_task, "X", configMINIMAL_STACK_SIZE, NULL, 3, &x);
    xTaskNotifyIndexed(x, 0, 0x1, eSetBits);  /* 새 RX 프레임 이벤트 */
    expect("X after index-0 notify: stage (1 = still in Take idx 1)", x_stage, 1);
    xTaskNotifyGiveIndexed(x, 1);             /* TX 완료 */
    expect("X after index-1 give: stage", x_stage, 3);
    expect("  Take(idx 1) returned", x_take, 1);
    expect("  Wait(idx 0, ticks 0) -> pdTRUE (pending kept)", x_wait_ret, pdTRUE);
    expect("  index-0 bits", x_wait_val, 0x1);

    printf("%d/%d agree with the model\n", g_ok, g_n);
    exit(g_ok == g_n ? 0 : 1);
}

int main(void) {
    xTaskCreate(ctrl, "ctrl", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
