/* BUILD: freertos */
/* RTOS-M09 데모 — FreeRTOS POSIX 포트에서 configCHECK_FOR_STACK_OVERFLOW 2 는 왜 안 울리나 (검사 없음, 관찰용)
 *
 *   ① 스택 256 워드(2 KB)짜리 태스크 hog 가 16 KB 지역 배열을 다 쓰고, 그 배열이 살아 있는 채로 vTaskDelay
 *      → 진짜 MCU 라면 방법 1·2 둘 다 잡을 상황. 여기선 훅 0 회.
 *   ② 이유를 숫자로: 배열 주소가 커널이 준 스택 [pxStackBase, pxEndOfStack] 밖(pthread 스택)에 있고,
 *      커널의 pxTopOfStack 은 생성 후 한 번도 안 바뀌며, 스택 맨 아래 16 B 는 계속 0xa5.
 *   ③ 검사 코드 자체는 산다: 커널 스택의 맨 아래 1 워드를 일부러 덮고 vTaskDelay → 훅이 hog 이름으로 불린다.
 *
 * 빌드는 md 6. 정답 코드 아래 "FreeRTOS 데모" 참고 (커널 오브젝트는 경고 허용, 이 파일은 -Werror).
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

extern volatile unsigned long g_overflow_hook_calls;
extern const char * volatile g_overflow_hook_name;

static TaskStatus_t info(void) {
    TaskStatus_t s;
    vTaskGetInfo(NULL, &s, pdTRUE, eInvalid);
    return s;
}

static void hog(void *p) {
    (void)p;
    TaskStatus_t a = info();
    printf("hog stack (kernel) : base %p .. end %p  (%" PRIuPTR " B), pxTopOfStack %p\n",
           (void *)a.pxStackBase, (void *)a.pxEndOfStack,
           (uintptr_t)(a.pxEndOfStack + 1) - (uintptr_t)a.pxStackBase, (void *)a.pxTopOfStack);
    printf("hog pthread stack  : %zu B, a local lives at %p\n", pthread_get_stacksize_np(pthread_self()), (void *)&a);

    /* ① 2 KB 스택에 16 KB 배열 — 살아 있는 채로 전환 */
    volatile uint8_t big[16 * 1024];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)i;
    vTaskDelay(2);
    TaskStatus_t b = info();
    printf("[1] 16 KB local live across vTaskDelay: hook calls = %lu\n", g_overflow_hook_calls);
    printf("    big[] at %p..%p  inside kernel stack? %s\n", (void *)&big[0], (void *)&big[sizeof big - 1],
           ((uintptr_t)&big[0] >= (uintptr_t)b.pxStackBase && (uintptr_t)&big[0] <= (uintptr_t)b.pxEndOfStack) ? "yes" : "NO");
    printf("    pxTopOfStack before %p after %p (%s)\n", (void *)a.pxTopOfStack, (void *)b.pxTopOfStack,
           a.pxTopOfStack == b.pxTopOfStack ? "unchanged" : "changed");
    const uint32_t *w = (const uint32_t *)b.pxStackBase;
    printf("    lowest 16 B of kernel stack: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n", w[0], w[1], w[2], w[3]);
    printf("    high-water mark = %u words of 256\n", (unsigned)b.usStackHighWaterMark);

    /* ③ 커널이 보는 메모리(스택 맨 아래)를 직접 덮으면 검사가 동작한다 */
    ((volatile uint32_t *)b.pxStackBase)[1] = 0;
    vTaskDelay(1);
    printf("[3] after clobbering pxStack word 1: hook calls = %lu, name = %s\n", g_overflow_hook_calls,
           g_overflow_hook_name ? g_overflow_hook_name : "(none)");
    fflush(stdout);
    exit(0);
}

int main(void) {
    xTaskCreate(hog, "hog", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
