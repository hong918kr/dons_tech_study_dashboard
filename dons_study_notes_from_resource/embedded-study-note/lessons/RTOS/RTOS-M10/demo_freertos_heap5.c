/* RTOS-M10 demo — 진짜 heap_5.c (V11.3.2) 의 영역 2개: 영역 사이 끝 표시(size 0)가 free list 에 남는 것을 본다.
 * 연습 2 (practice.c 의 h5 모델, hdr=16) 가 같은 숫자를 낸다.
 *
 * K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 * cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_heap5.c \
 *    $K/{tasks,list,queue}.c $K/portable/MemMang/heap_5.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M10_d5
 */
#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

static _Alignas(16) uint8_t sram[4096];         /* 영역 A = [0,1024) "SRAM1", 영역 B = [2048,2560) "SRAM2" */
void vApplicationMallocFailedHook(void) { }

static void show(const char *what) {
    HeapStats_t s; vPortGetHeapStats(&s);
    printf("%-34s free=%4zu blocks=%zu largest=%4zu smallest=%4zu\n", what, s.xAvailableHeapSpaceInBytes,
           s.xNumberOfFreeBlocks, s.xSizeOfLargestFreeBlockInBytes, s.xSizeOfSmallestFreeBlockInBytes);
}

static void runner(void *arg) {
    (void)arg;
    show("after vPortDefineHeapRegions");
    void *big = pvPortMalloc(1200);
    printf("pvPortMalloc(1200) -> %s (영역 하나에 안 들어감)\n", big ? "ok" : "NULL");
    uint8_t *a = pvPortMalloc(900), *b = pvPortMalloc(400);
    printf("malloc(900) at sram+%ld, malloc(400) at sram+%ld\n", (long)(a - sram), (long)(b - sram));
    show("after 900 + 400");
    vPortFree(a); vPortFree(b);
    show("after free both");
    uint8_t *w = pvPortMalloc(992);              /* 1008 B 블록 = 영역 A 전부 */
    printf("malloc(992) at sram+%ld\n", (long)(w - sram));
    vPortFree(w);
    show("after whole-A alloc + free");
    exit(0);
}

static StaticTask_t tcb;
static StackType_t stack[configMINIMAL_STACK_SIZE * 4];
int main(void) {
    const HeapRegion_t regions[] = { { sram, 1024 }, { sram + 2048, 512 }, { NULL, 0 } };   /* 주소 오름차순 필수 */
    vPortDefineHeapRegions(regions);
    xTaskCreateStatic(runner, "runner", configMINIMAL_STACK_SIZE * 4, NULL, 2, stack, &tcb);
    vTaskStartScheduler();
    return 2;
}
