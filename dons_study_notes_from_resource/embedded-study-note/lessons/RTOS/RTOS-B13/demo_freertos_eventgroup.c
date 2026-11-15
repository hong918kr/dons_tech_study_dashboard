/* RTOS-B13 demo — 진짜 FreeRTOS(V11.3.2, POSIX 포트)로 이벤트 그룹 의미론을 확인한다.
 * solution.c / practice.c 모델과 같은 시나리오를 돌려 "모델 예측 == 커널 결과" 를 한 줄씩 찍는다.
 * POSIX 포트는 한 번에 FreeRTOS 스레드 하나만 돌린다 → 순서·반환값은 결정적. 시간(ns)은 보지 않는다.
 * "몇 tick 이면 충분하겠지" 를 가정하지 않는다: 대기자가 Blocked 가 될 때까지 / 돌아올 때까지 1 tick 씩 확인(settle).
 * (이 호스트 설정은 configTICK_TYPE_WIDTH_IN_BITS = 64 → EventBits_t 64비트, 제어 비트는 최상위 바이트.)
 *
 * K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 * cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_eventgroup.c hooks.c \
 *    $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
 *    -o /tmp/RTOS-B13_dfr && /tmp/RTOS-B13_dfr
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

static int g_ok, g_n;
static void expect(const char *what, unsigned long got, unsigned long want) {
    g_n++;
    if (got == want) g_ok++;
    printf("[%s] %-60s got=%#-8lx model=%#lx\n", got == want ? " OK " : "DIFF", what, got, want);
}
static EventGroupHandle_t g_eg;
static char g_order[64];                      /* 대기에서 돌아온 순서 */
static volatile int g_returned;
static void order_add(const char *n) { strcat(g_order, n); strcat(g_order, " "); g_returned++; }
/* 시간 가정 없이 기다리기: 조건이 될 때까지 1 tick 씩 (상한 1000 tick) */
static void settle(volatile int *c, int n) { for (int i = 0; i < 1000 && *c < n; i++) vTaskDelay(1); }
static void settle_blocked(TaskHandle_t *h, int n) {
    for (int i = 0; i < 1000; i++) {
        int b = 0;
        for (int k = 0; k < n; k++) b += eTaskGetState(h[k]) == eBlocked;
        if (b == n) return;
        vTaskDelay(1);
    }
}

/* ---- B1: 여러 대기자 (solution.c Part B1 과 같은 조건) ---- */
typedef struct { const char *name; EventBits_t want; BaseType_t clear, all; TickType_t to; volatile EventBits_t ret; } wspec_t;
static wspec_t W[4] = {
    { "W1", 0x1, pdTRUE,  pdFALSE, portMAX_DELAY, 0 },
    { "W2", 0x3, pdTRUE,  pdTRUE,  portMAX_DELAY, 0 },
    { "W3", 0x2, pdFALSE, pdFALSE, portMAX_DELAY, 0 },
    { "W4", 0x7, pdFALSE, pdTRUE,  10,            0 },
};
static void waiter(void *p) {
    wspec_t *w = p;
    w->ret = xEventGroupWaitBits(g_eg, w->want, w->clear, w->all, w->to);
    order_add(w->name);
    vTaskSuspend(NULL);
}
/* ---- 방송: ANY+CLEAR 대기자 둘 → set 한 번에 둘 다 ---- */
static volatile int g_bcast;
static TaskHandle_t g_h[4];
static void bcast_waiter(void *p) {
    (void)p;
    if (xEventGroupWaitBits(g_eg, 0x100, pdTRUE, pdFALSE, portMAX_DELAY) & 0x100) g_bcast++;
    vTaskSuspend(NULL);
}
/* ---- B2c: 타임아웃으로 READY 가 된 뒤, 돌기 전에 비트가 맞춰짐 ---- */
static volatile TickType_t g_m_t0;
static volatile EventBits_t g_m_ret;
static volatile int g_m_done;
static void late_waiter(void *p) {
    (void)p;
    g_m_t0 = xTaskGetTickCount();
    g_m_ret = xEventGroupWaitBits(g_eg, 0x3, pdTRUE, pdTRUE, 5);
    g_m_done = 1;
    vTaskSuspend(NULL);
}
/* ---- 랑데부: xEventGroupSync ---- */
static volatile EventBits_t g_sync_ret[3];
static volatile int g_sync_done;
static volatile TickType_t g_sync_arrive[3], g_sync_leave[3];
static void sync_task(void *p) {
    int i = (int)(intptr_t)p;
    static const TickType_t work[3] = { 2, 3, 5 };
    vTaskDelay(work[i]);
    g_sync_arrive[i] = xTaskGetTickCount();
    g_sync_ret[i] = xEventGroupSync(g_eg, (EventBits_t)0x10 << i, 0x70, 50);
    g_sync_leave[i] = xTaskGetTickCount();
    g_sync_done++;
    vTaskSuspend(NULL);
}

static void ctrl(void *p) {
    (void)p;
    g_eg = xEventGroupCreate();

    puts("-- A  wait / clear / get without blocking");
    expect("SetBits(0x5) no waiters -> returns bits", (unsigned long)xEventGroupSetBits(g_eg, 0x5), 0x5);
    expect("WaitBits(0x1, clear, any, 0) -> value BEFORE clear", (unsigned long)xEventGroupWaitBits(g_eg, 0x1, pdTRUE, pdFALSE, 0), 0x5);
    expect("  bits after clear-on-exit", (unsigned long)xEventGroupGetBits(g_eg), 0x4);
    expect("WaitBits(0x3, noclear, all, 0) not met -> current bits", (unsigned long)xEventGroupWaitBits(g_eg, 0x3, pdFALSE, pdTRUE, 0), 0x4);
    expect("ClearBits(0x4) -> value BEFORE clear", (unsigned long)xEventGroupClearBits(g_eg, 0x4), 0x4);
    expect("  bits now", (unsigned long)xEventGroupGetBits(g_eg), 0x0);
    xEventGroupSetBits(g_eg, 0x3);
    expect("WaitBits(0xB, clear, any, 0) with 0x3 set -> returns", (unsigned long)xEventGroupWaitBits(g_eg, 0xB, pdTRUE, pdFALSE, 0), 0x3);
    expect("  clear-on-exit cleared the whole mask 0xB", (unsigned long)xEventGroupGetBits(g_eg), 0x0);

    puts("-- B1 multi-waiter: W1 any 0x1 clr (p1), W2 all 0x3 clr (p2), W3 any 0x2 (p3), W4 all 0x7 to=10 (p2)");
    for (int i = 0; i < 4; i++) xTaskCreate(waiter, W[i].name, configMINIMAL_STACK_SIZE, &W[i], i == 0 ? 1 : i == 2 ? 3 : 2, &g_h[i]);
    settle_blocked(g_h, 4);                                     /* 넷 다 Blocked 가 될 때까지 */
    expect("SetBits(0x2) -> returns (W3 has no clear)", (unsigned long)xEventGroupSetBits(g_eg, 0x2), 0x2);
    settle(&g_returned, 1);
    expect("SetBits(0x1) -> returns (W1|W2 clear after the loop)", (unsigned long)xEventGroupSetBits(g_eg, 0x1), 0x0);
    settle(&g_returned, 3);
    expect("  W1 got (value before clear)", (unsigned long)W[0].ret, 0x3);
    expect("  W2 got (woken although W1 clears bit0)", (unsigned long)W[1].ret, 0x3);
    expect("  W3 got", (unsigned long)W[2].ret, 0x2);
    settle(&g_returned, 4);                                     /* W4 는 10 tick 타임아웃 */
    expect("  W4 timed out -> current bits", (unsigned long)W[3].ret, 0x0);
    printf("       return order: %s(model: W3 W2 W1 W4)\n", g_order);
    expect("  return order matches model (1 = yes)", strcmp(g_order, "W3 W2 W1 W4 ") == 0, 1);

    puts("-- B  broadcast: two waiters any 0x100 + clear, one SetBits");
    xTaskCreate(bcast_waiter, "E1", configMINIMAL_STACK_SIZE, NULL, 3, &g_h[0]);
    xTaskCreate(bcast_waiter, "E2", configMINIMAL_STACK_SIZE, NULL, 3, &g_h[1]);
    settle_blocked(g_h, 2);
    expect("SetBits(0x100) -> returns", (unsigned long)xEventGroupSetBits(g_eg, 0x100), 0x0);
    settle(&g_bcast, 2);
    expect("  both waiters woke (clear-on-exit is NOT one-consumer)", (unsigned long)g_bcast, 2);

    puts("-- B2c timeout, then bits completed before the waiter runs");
    xEventGroupSetBits(g_eg, 0x1);
    xTaskCreate(late_waiter, "M", configMINIMAL_STACK_SIZE, NULL, 1, &g_h[0]);
    settle_blocked(g_h, 1);                                     /* M 이 블록 (5 tick) */
    for (int i = 0; i < 1000 && eTaskGetState(g_h[0]) == eBlocked; i++)
        vTaskDelay(1);                                          /* 매 tick 깨어남 → M 의 마감 tick 에 더 높은 ctrl 이 먼저 돈다 */
    expect("  M timed out (Ready) but has not run yet", (unsigned long)(eTaskGetState(g_h[0]) == eReady && !g_m_done), 1);
    expect("SetBits(0x2) at deadline -> returns (M no longer waiting)", (unsigned long)xEventGroupSetBits(g_eg, 0x2), 0x3);
    settle(&g_m_done, 1);
    expect("  M returns 0x3 via the timeout re-check path", (unsigned long)g_m_ret, 0x3);
    expect("  and cleared 0x3 on its way out", (unsigned long)xEventGroupGetBits(g_eg), 0x0);

    puts("-- C  xEventGroupSync rendezvous (3 tasks, bits 0x10/0x20/0x40, work 2/3/5 ticks)");
    for (int i = 0; i < 3; i++) xTaskCreate(sync_task, "S", configMINIMAL_STACK_SIZE, (void *)(intptr_t)i, 2, NULL);
    settle(&g_sync_done, 3);
    for (int i = 0; i < 3; i++) expect("  Sync() returned all rendezvous bits", (unsigned long)(g_sync_ret[i] & 0x70), 0x70);
    expect("  rendezvous bits cleared afterwards", (unsigned long)xEventGroupGetBits(g_eg), 0x0);
    TickType_t last_arrive = 0;
    for (int i = 0; i < 3; i++) if (g_sync_arrive[i] > last_arrive) last_arrive = g_sync_arrive[i];
    int early = 0;
    for (int i = 0; i < 3; i++) early += g_sync_leave[i] < last_arrive;
    printf("       arrive (rel) %lu %lu %lu  leave (rel) %lu %lu %lu\n",
           (unsigned long)(g_sync_arrive[0] - g_sync_arrive[0]), (unsigned long)(g_sync_arrive[1] - g_sync_arrive[0]),
           (unsigned long)(g_sync_arrive[2] - g_sync_arrive[0]), (unsigned long)(g_sync_leave[0] - g_sync_arrive[0]),
           (unsigned long)(g_sync_leave[1] - g_sync_arrive[0]), (unsigned long)(g_sync_leave[2] - g_sync_arrive[0]));
    expect("  nobody left before the last arrival", (unsigned long)early, 0);

    printf("%d/%d match the model\n", g_ok, g_n);
    exit(g_ok == g_n ? 0 : 1);
}

int main(void) {
    xTaskCreate(ctrl, "ctrl", configMINIMAL_STACK_SIZE, NULL, 6, NULL);
    vTaskStartScheduler();
    return 1;
}
