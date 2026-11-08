/* RTOS-B10 demo — 뮤텍스를 ...FromISR API 로 쓰면 커널 코드가 어떤 길을 타나 (FreeRTOS V11.3.2, POSIX 포트)
 *
 * 주의: POSIX 포트에는 진짜 인터럽트가 없다. 여기서는 FromISR 함수를 "태스크 문맥에서" 직접 불러
 * 그 함수의 코드 경로(검사·holder 기록)만 관찰한다. 실제 칩에서는 ISR 안에서 불리는 함수들이다.
 *
 *   ./demo_isr 1   빈 뮤텍스를 xSemaphoreTakeFromISR → holder 가 NULL 로 남는다 (태스크 신원이 없음)
 *                  → 그 뒤 아무 태스크나 give 해도 통과 (소유권 검사가 무력화)
 *   ./demo_isr 2   태스크가 잡고 있는 뮤텍스를 xSemaphoreGiveFromISR → queue.c 의 configASSERT
 *                  (-DDEMO_NO_ASSERT 빌드: 통과해 버리고 count=1 인데 holder=A 인 모순 상태)
 *   ./demo_isr 3   (대조) 바이너리 세마포어 xSemaphoreGiveFromISR → 정상 (ISR→태스크 신호, RTOS-B08)
 */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t m, b;
static int sc;
static const char *nm(TaskHandle_t h) { return h ? pcTaskGetName(h) : "NULL"; }

static void taskA(void *p) {
    (void)p;
    BaseType_t woken = pdFALSE, r;
    if (sc == 1) {
        r = xSemaphoreTakeFromISR(m, &woken);
        printf("\"ISR\" TakeFromISR(mutex)  ret=%ld  holder=%s\n", (long)r, nm(xSemaphoreGetMutexHolder(m)));
        r = xSemaphoreTake(m, 0);
        printf("A     Take(mutex, 0)        ret=%ld  (mutex is taken, but by nobody)\n", (long)r);
        r = xSemaphoreGive(m);
        printf("A     Give(mutex) as non-owner ret=%ld  holder=%s   <- 검사할 소유자가 없음\n",
               (long)r, nm(xSemaphoreGetMutexHolder(m)));
    } else if (sc == 2) {
        r = xSemaphoreTake(m, 0);
        printf("A     Take(mutex)           ret=%ld  holder=%s\n", (long)r, nm(xSemaphoreGetMutexHolder(m)));
        printf("\"ISR\" GiveFromISR(mutex) ...\n");
        r = xSemaphoreGiveFromISR(m, &woken);
        printf("\"ISR\" GiveFromISR(mutex)  ret=%ld  holder=%s  count=%lu  <- 비었다는데 소유자는 A\n", (long)r,
               nm(xSemaphoreGetMutexHolder(m)), (unsigned long)uxSemaphoreGetCount(m));
    } else {
        r = xSemaphoreGiveFromISR(b, &woken);
        printf("\"ISR\" GiveFromISR(binary) ret=%ld  woken=%ld\n", (long)r, (long)woken);
        r = xSemaphoreTake(b, 0);
        printf("A     Take(binary, 0)       ret=%ld\n", (long)r);
    }
    printf("done\n");
    exit(0);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    sc = argc > 1 ? atoi(argv[1]) : 1;
    m = xSemaphoreCreateMutex();
    b = xSemaphoreCreateBinary();
    xTaskCreate(taskA, "A", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
