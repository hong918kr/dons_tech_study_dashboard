/* BUILD: freertos */
/* RTOS-B07 데모 — POSIX 포트에서 "진짜가 아닌 것" 을 숫자로 본다 (출력은 실행마다 다르다. 검사 없음)
 *
 *   ① 태스크 = pthread: 각 태스크가 자기 pthread 이름(= 태스크 이름, port.c 558)과 스레드 id 를 찍는다
 *   ② 틱 = "Scheduler timer" 스레드의 usleep(1000) + SIGALRM (port.c 452-472) → 벽시계 1 초에 틱이 몇 개?
 *   ③ vTaskDelay(100) 은 벽시계로 몇 ms? / vTaskDelay(1) 20번의 최소·최대
 *
 * 빌드 (SRC=demo_freertos_posix_reality, 방법은 solution.c 머리말과 같음)
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static void who(const char *task) {
    char name[64] = "";
    uint64_t tid = 0;
    pthread_getname_np(pthread_self(), name, sizeof name);
    pthread_threadid_np(NULL, &tid);
    printf("  task %-6s runs on pthread \"%s\" (tid %llu), pthread stack %zu B\n", task, name,
           (unsigned long long)tid, pthread_get_stacksize_np(pthread_self()));
}

static void helper(void *p) {
    (void)p;
    who("helper");
    for (;;) vTaskDelay(100000);
}

static void demo(void *p) {
    (void)p;
    vTaskDelay(2);                                   /* helper 가 먼저 한 번 돌게 */
    who("demo");

    /* ② 벽시계 1 초 동안 바쁜 루프 → 틱이 몇 개 올랐나 */
    TickType_t t0 = xTaskGetTickCount();
    double w0 = now_ms();
    while (now_ms() - w0 < 1000.0) { }
    TickType_t dt = xTaskGetTickCount() - t0;
    printf("busy 1000 ms wall clock -> %lu ticks (configTICK_RATE_HZ = %d)\n", (unsigned long)dt, configTICK_RATE_HZ);

    /* ③ vTaskDelay 의 벽시계 길이 */
    double a = now_ms();
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("vTaskDelay(100 ticks) -> %.2f ms wall clock\n", now_ms() - a);
    double mn = 1e9, mx = 0;
    for (int i = 0; i < 20; i++) {
        double b = now_ms();
        vTaskDelay(1);
        double d = now_ms() - b;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }
    printf("vTaskDelay(1) x20 -> min %.3f ms, max %.3f ms\n", mn, mx);
    fflush(stdout);
    exit(0);
}

int main(void) {
    xTaskCreate(demo, "demo", 512, NULL, 2, NULL);
    xTaskCreate(helper, "helper", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
