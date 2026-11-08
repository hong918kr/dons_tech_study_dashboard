/* RTOS-M01 demo — RAM 과 호출당 시간: 세마포어/이벤트 그룹 vs 태스크 알림 (진짜 FreeRTOS V11.3.2, POSIX 포트)
 *
 * ① RAM: Static*_t 크기 + heap_4 에서 동적 생성 시 실제로 줄어든 힙(xPortGetFreeHeapSize 차이).
 * ② 시간: 한 태스크 안에서 "주고 → 바로 받기" 를 N 번 (블록 없음) + 두 태스크 핑퐁(블록·스위치 포함).
 *    호스트 숫자다: POSIX 포트의 taskENTER_CRITICAL 은 pthread_sigmask 시스템 콜이고(port.c 406–421),
 *    컨텍스트 스위치는 pthread 깨우기다. 절대값은 MCU 와 무관하고 "같은 호스트에서의 상대 비교" 로만 읽는다.
 *
 * K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 * cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_bench.c hooks.c \
 *    $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_4.c $P/port.c $P/utils/wait_for_event.c \
 *    -o /tmp/RTOS-M01_db && /tmp/RTOS-M01_db
 */
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#define N_ITER 200000
#define N_REP  7
#define N_PING 20000

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
typedef void (*bench_fn)(int n);
static void (*g_reset)(void);                       /* 반복 사이 상태 비우기 (측정 밖) */
static void run(const char *name, bench_fn f, int n) {
    double r[N_REP];
    f(1000);                                         /* 워밍업 */
    for (int i = 0; i < N_REP; i++) {
        if (g_reset) g_reset();
        uint64_t t0 = now_ns();
        f(n);
        r[i] = (double)(now_ns() - t0) / n;
    }
    qsort(r, N_REP, sizeof r[0], cmp_d);
    printf("  %-50s min %7.1f  median %7.1f ns\n", name, r[0], r[N_REP / 2]);
}

static SemaphoreHandle_t g_bin, g_cnt;
static EventGroupHandle_t g_eg;
static TaskHandle_t g_me;

/* ---- 참고: POSIX 포트의 임계 구역 1번 = pthread_sigmask 2번 (BLOCK + UNBLOCK) ---- */
static void b_sigmask_pair(int n) {
    sigset_t all;
    sigfillset(&all);
    for (int i = 0; i < n; i++) { pthread_sigmask(SIG_BLOCK, &all, NULL); pthread_sigmask(SIG_UNBLOCK, &all, NULL); }
}
/* ---- 블록 없는 경로: 주고 바로 받기 (한 쌍 = 1회) ---- */
static void b_binsem(int n) { for (int i = 0; i < n; i++) { xSemaphoreGive(g_bin); xSemaphoreTake(g_bin, 0); } }
static void b_notify_bin(int n) { for (int i = 0; i < n; i++) { xTaskNotifyGive(g_me); ulTaskNotifyTake(pdTRUE, 0); } }
static void b_egroup(int n) {
    for (int i = 0; i < n; i++) { xEventGroupSetBits(g_eg, 0x1); xEventGroupWaitBits(g_eg, 0x1, pdTRUE, pdFALSE, 0); }
}
static void b_notify_bits(int n) {
    uint32_t v;
    for (int i = 0; i < n; i++) { xTaskNotify(g_me, 0x1, eSetBits); xTaskNotifyWait(0, 0xFFFFFFFFu, &v, 0); }
}
/* ---- ISR 쪽 give 만 (FromISR 마스크는 POSIX 포트에서 시스템 콜이 아니다: port.c 424–436) ---- */
static void b_sem_give_isr(int n) {
    BaseType_t hp = pdFALSE;
    for (int i = 0; i < n; i++) xSemaphoreGiveFromISR(g_cnt, &hp);
}
static void reset_cnt(void) { while (xSemaphoreTake(g_cnt, 0) == pdTRUE) { } }
static void b_notify_give_isr(int n) {
    BaseType_t hp = pdFALSE;
    for (int i = 0; i < n; i++) vTaskNotifyGiveFromISR(g_me, &hp);
}
static void reset_notify(void) { (void)ulTaskNotifyTake(pdTRUE, 0); }

/* ---- 두 태스크 핑퐁: high 가 기다리고 ctrl 이 준다 → 매번 high 로 스위치 → high 가 다시 블록 ---- */
static volatile int g_mode;            /* 0 = 세마포어, 1 = 알림 */
static volatile unsigned long g_got;
static TaskHandle_t g_hi;
static void hi_task(void *p) {
    (void)p;
    for (;;) {
        if (g_mode == 0) { if (xSemaphoreTake(g_bin, portMAX_DELAY) == pdTRUE) g_got++; }
        else { if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) g_got++; }
    }
}
static void b_ping_sem(int n) { for (int i = 0; i < n; i++) xSemaphoreGive(g_bin); }
static void b_ping_notify(int n) { for (int i = 0; i < n; i++) xTaskNotifyGive(g_hi); }

static void ctrl(void *p) {
    (void)p;
    g_me = xTaskGetCurrentTaskHandle();

    puts("== RAM (host: arm64 macOS, POSIX port, this FreeRTOSConfig.h)");
    printf("  sizeof(TickType_t)=%zu  sizeof(UBaseType_t)=%zu  configTASK_NOTIFICATION_ARRAY_ENTRIES=%d\n",
           sizeof(TickType_t), sizeof(UBaseType_t), configTASK_NOTIFICATION_ARRAY_ENTRIES);
    printf("  StaticSemaphore_t %3zu  StaticEventGroup_t %3zu  StaticTask_t %3zu\n",
           sizeof(StaticSemaphore_t), sizeof(StaticEventGroup_t), sizeof(StaticTask_t));
    size_t f0 = xPortGetFreeHeapSize();
    g_bin = xSemaphoreCreateBinary();
    size_t f1 = xPortGetFreeHeapSize();
    g_cnt = xSemaphoreCreateCounting(0xFFFFFFFFu, 0);
    size_t f2 = xPortGetFreeHeapSize();
    g_eg = xEventGroupCreate();
    size_t f3 = xPortGetFreeHeapSize();
    printf("  heap_4 used by create: binary sem %zu, counting sem %zu, event group %zu bytes\n", f0 - f1, f1 - f2, f2 - f3);
    printf("  task notification: 0 extra bytes (fields already in every TCB: %d x (4 + 1) bytes + padding)\n",
           configTASK_NOTIFICATION_ARRAY_ENTRIES);

    printf("== time per call pair, host-relative only (N=%d, %d reps)\n", N_ITER, N_REP);
    run("ref: pthread_sigmask BLOCK+UNBLOCK (= 1 critical section)", b_sigmask_pair, N_ITER);
    run("binary sem: Give + Take(0)", b_binsem, N_ITER);
    run("notify:     NotifyGive + NotifyTake(pdTRUE,0)", b_notify_bin, N_ITER);
    run("event grp:  SetBits + WaitBits(clear,0)", b_egroup, N_ITER);
    run("notify:     Notify(eSetBits) + NotifyWait(0)", b_notify_bits, N_ITER);
    g_reset = reset_cnt;
    run("FromISR give only: xSemaphoreGiveFromISR (counting)", b_sem_give_isr, N_ITER);
    g_reset = reset_notify;
    run("FromISR give only: vTaskNotifyGiveFromISR", b_notify_give_isr, N_ITER);
    g_reset = NULL;

    xTaskCreate(hi_task, "hi", configMINIMAL_STACK_SIZE, NULL, 3, &g_hi);   /* 바로 블록 */
    printf("== ping-pong: give from ctrl(prio 2) -> wake hi(prio 3) -> hi blocks again (N=%d)\n", N_PING);
    g_mode = 0; g_got = 0;
    run("binary sem round trip", b_ping_sem, N_PING);
    unsigned long got_sem = g_got;
    g_mode = 1; g_got = 0;
    xSemaphoreGive(g_bin);                   /* hi 를 세마포어 대기에서 빼내 알림 모드로 */
    g_got = 0;
    run("notify round trip", b_ping_notify, N_PING);
    printf("  wakeups counted: sem %lu, notify %lu (expected %d each)\n", got_sem, g_got,
           1000 + N_REP * N_PING);
    exit(0);
}

int main(void) {
    xTaskCreate(ctrl, "ctrl", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
