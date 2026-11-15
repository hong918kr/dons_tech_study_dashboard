/* BUILD: freertos (POSIX 포트, 아래 명령)
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_hwm.c fw_graph.c hooks.c \
 *      $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
 *      -o /tmp/RTOS-M08_hwm && /tmp/RTOS-M08_hwm
 *
 * RTOS-M08 데모 — FreeRTOS POSIX 포트의 uxTaskGetStackHighWaterMark 는 스택 예산의 "실측" 이 될 수 있나?
 *   세 태스크가 스택을 아주 다르게 쓴다: ctrl(제어 경로 1회), comms(텔레메트리 1회), hog(지역 배열 1500 B).
 *   Cortex-M 이라면 HWM 이 셋 다 달라야 한다. 이 포트에서는? 그리고 지역 변수의 주소는 FreeRTOS 가 준 스택 안인가?
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "fw_graph.h"

#define DEPTH 256                                  /* 워드 (이 포트의 StackType_t = 8 B → 2048 B) */

typedef struct { const char *name; TaskHandle_t h; UBaseType_t hwm; uintptr_t local; int done; } rec_t;
static rec_t R[3] = { { "ctrl", 0, 0, 0, 0 }, { "comms", 0, 0, 0, 0 }, { "hog", 0, 0, 0, 0 } };
static ctl_state_t g_st;

static void worker(void *p) {
    rec_t *r = p;
    volatile char probe = 0;
    if (r == &R[0]) ctrl_task_once(&g_st);
    else if (r == &R[1]) comms_task_once(&g_st);
    else { volatile char big[1500]; for (int i = 0; i < 1500; i++) big[i] = (char)i; probe = big[1499]; }
    r->local = (uintptr_t)&probe;
    r->hwm = uxTaskGetStackHighWaterMark(NULL);
    r->done = 1;
    vTaskSuspend(NULL);
}

static void reporter(void *p) {
    (void)p;
    while (!(R[0].done && R[1].done && R[2].done)) vTaskDelay(1);
    printf("POSIX port: depth %d words x %zu B = %zu B per task\n", DEPTH, sizeof(StackType_t), DEPTH * sizeof(StackType_t));
    printf("%-6s %10s %12s   %s\n", "task", "HWM(words)", "depth-HWM", "&local inside FreeRTOS stack [pxStackBase, +2048)?");
    for (int i = 0; i < 3; i++) {
        TaskStatus_t s;
        vTaskGetInfo(R[i].h, &s, pdFALSE, eInvalid);
        uintptr_t lo = (uintptr_t)s.pxStackBase, hi = lo + DEPTH * sizeof(StackType_t);
        printf("%-6s %10lu %12lu   %s (local %#lx, stack %#lx..%#lx)\n", R[i].name, (unsigned long)R[i].hwm,
               (unsigned long)(DEPTH - R[i].hwm), (R[i].local >= lo && R[i].local < hi) ? "yes" : "NO",
               (unsigned long)R[i].local, (unsigned long)lo, (unsigned long)hi);
    }
    printf("each task really runs on its own pthread stack: %zu B (pthread_get_stacksize_np)\n",
           pthread_get_stacksize_np(pthread_self()));
    exit(0);
}

int main(void) {
    g_st.pid[0].hi = g_st.pid[1].hi = g_st.pid[2].hi = 10.0f;
    g_st.pid[0].lo = g_st.pid[1].lo = g_st.pid[2].lo = -10.0f;
    for (int i = 0; i < 3; i++)
        if (xTaskCreate(worker, R[i].name, DEPTH, &R[i], 2, &R[i].h) != pdPASS) return 1;
    if (xTaskCreate(reporter, "rep", DEPTH, NULL, 1, NULL) != pdPASS) return 1;
    vTaskStartScheduler();
    return 1;
}
