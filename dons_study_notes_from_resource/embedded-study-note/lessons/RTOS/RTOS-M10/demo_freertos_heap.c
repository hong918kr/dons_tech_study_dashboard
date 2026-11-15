/* RTOS-M10 demo — 진짜 FreeRTOS V11.3.2 (POSIX 포트)의 heap_4.c / heap_2.c 에 solution.c 와 "같은 워크로드" 를 돌려
 * 연산마다 (반환 오프셋, xFreeBytesRemaining) 과 끝 통계를 모델과 비교한다.
 *  - solution.c 를 그대로 #include 한다 (main 만 이름을 바꿈) → 워크로드 코드가 글자 그대로 같다.
 *  - 64비트 호스트라 BlockLink_t = 16 B → 모델은 hdr = HDR_HOST64(16) 로 돌린다.
 *  - ucHeap 은 여기서 정의 (configAPPLICATION_ALLOCATED_HEAP 1, 16 B 정렬) → 정렬 손실 0, 오프셋 비교 가능.
 *  - 시드마다 vPortHeapResetState() (heap_4.c:629, heap_2.c:401) 로 힙을 처음 상태로 되돌린다.
 *
 * K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 * heap_4: cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_heap.c \
 *            $K/{tasks,list,queue}.c $K/portable/MemMang/heap_4.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M10_d4
 * heap_2: 같은 줄에 -DUSE_HEAP2 를 넣고 heap_4.c 대신 heap_2.c                                        -o /tmp/RTOS-M10_d2
 */
#pragma clang diagnostic ignored "-Wunused-function"
#define main model_main
#include "solution.c"
#undef main

#include "FreeRTOS.h"
#include "task.h"

_Alignas(16) uint8_t ucHeap[configTOTAL_HEAP_SIZE];
static _Alignas(16) uint8_t model_arena[configTOTAL_HEAP_SIZE];
static int g_hook_calls;
void vApplicationMallocFailedHook(void) { g_hook_calls++; }   /* 실패해도 계속 (템플릿 훅은 exit) */

static void *rt_alloc(void *c, size_t n) { (void)c; return pvPortMalloc(n); }
static int rt_release(void *c, void *p) { (void)c; vPortFree(p); return 0; }
static void rt_stats(void *c, heap_stats_t *s) {
    (void)c; memset(s, 0, sizeof *s);
#ifdef USE_HEAP2
    s->available = (uint32_t)xPortGetFreeHeapSize();         /* heap_2 엔 vPortGetHeapStats 가 없다 */
#else
    HeapStats_t x; vPortGetHeapStats(&x);
    s->available = (uint32_t)x.xAvailableHeapSpaceInBytes; s->largest = (uint32_t)x.xSizeOfLargestFreeBlockInBytes;
    s->smallest = (uint32_t)x.xSizeOfSmallestFreeBlockInBytes; s->n_blocks = (uint32_t)x.xNumberOfFreeBlocks;
    s->min_ever = (uint32_t)x.xMinimumEverFreeBytesRemaining;
    s->n_alloc = (uint32_t)x.xNumberOfSuccessfulAllocations; s->n_free = (uint32_t)x.xNumberOfSuccessfulFrees;
#endif
}

#define STEPS 5000
static wl_trace_t tm[STEPS], tr[STEPS];

static void runner(void *arg) {
    (void)arg;
    const char *name =
#ifdef USE_HEAP2
        "heap_2";
#else
        "heap_4";
#endif
    printf("%s, configTOTAL_HEAP_SIZE=%d, sizeof(BlockLink-like header)=%u, ucHeap %% 16 = %u\n",
           name, configTOTAL_HEAP_SIZE, HDR_HOST64, (unsigned)((uintptr_t)ucHeap & 15));
    uint8_t *p1 = pvPortMalloc(1);
    printf("first pvPortMalloc(1) - ucHeap = %ld, free after = %zu\n", (long)(p1 - ucHeap), xPortGetFreeHeapSize());
    vPortFree(p1);
    printf("seed | first_fail  fails hook | ops equal (kind,off,avail) | end avail largest blocks min_ever n_alloc n_free | model\n");
    int all_ok = 1;
    for (uint32_t seed = 1; seed <= 8; seed++) {
        heap_t h; wl_result_t rm, rr; heap_stats_t sm, sr;
#ifdef USE_HEAP2
        h2_init(&h, model_arena, configTOTAL_HEAP_SIZE, HDR_HOST64);
#else
        h4_init(&h, model_arena, configTOTAL_HEAP_SIZE, HDR_HOST64);
#endif
        alloc_if_t im = { m_alloc, m_release, m_stats, m_check, &h, (uintptr_t)h.base };
        alloc_if_t ir = { rt_alloc, rt_release, rt_stats, NULL, NULL, (uintptr_t)ucHeap };
        run_workload(&im, seed, STEPS, 20, &rm, tm);
        vPortHeapResetState();
        g_hook_calls = 0;
        run_workload(&ir, seed, STEPS, 20, &rr, tr);
        int eq = 0;
        for (int i = 0; i < STEPS; i++)
            eq += tm[i].kind == tr[i].kind && tm[i].off == tr[i].off && tm[i].avail == tr[i].avail;
        m_stats(&h, &sm); rt_stats(NULL, &sr);
#ifdef USE_HEAP2
        int stats_eq = sm.available == sr.available;
#else
        int stats_eq = memcmp(&sm, &sr, sizeof sm) == 0;
#endif
        int ok = eq == STEPS && stats_eq && rm.first_fail == rr.first_fail && rm.fails == rr.fails &&
                 g_hook_calls == rr.fails && rm.inv_bad == 0 && rr.corrupt == 0;
        all_ok &= ok;
        printf("%4u | %10d %6d %4d | %5d/%d | %9u %7u %6u %8u %7u %6u | %s\n", seed, rr.first_fail, rr.fails, g_hook_calls,
               eq, STEPS, sr.available, sr.largest, sr.n_blocks, sr.min_ever, sr.n_alloc, sr.n_free, ok ? "SAME" : "DIFF");
    }
    printf("%s: model %s real FreeRTOS on all 8 seeds\n", name, all_ok ? "==" : "!=");
    exit(all_ok ? 0 : 1);
}

static StaticTask_t tcb;
static StackType_t stack[configMINIMAL_STACK_SIZE * 4];
int main(void) {
    xTaskCreateStatic(runner, "runner", configMINIMAL_STACK_SIZE * 4, NULL, 2, stack, &tcb);
    vTaskStartScheduler();
    return 2;
}
