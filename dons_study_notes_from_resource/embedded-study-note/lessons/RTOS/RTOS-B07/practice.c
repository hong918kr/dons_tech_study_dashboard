/* BUILD: freertos */
/* RTOS-B07 추가 연습 — 한 번에 검증 (FreeRTOS V11.3.2 POSIX 포트, macOS)
 *
 *   연습 1  vTaskPrioritySet 이 "그 자리에서" 선점하는 두 경우 (내 우선순위 내리기 / 남을 내 위로 올리기)
 *   연습 2  상태 글자 X R B S 를 한 스냅샷에서 + vTaskSuspend 로 멈췄던 대기 태스크를 Resume 하면?
 *   연습 3  uxTaskGetSystemState 배열 · vTaskListTasks 버퍼가 작을 때
 *   연습 4  POSIX 포트의 스택 HWM 은 왜 "거짓말" 인가 — 16 KB 지역 배열을 써도 300 워드 스택이 멀쩡
 *
 * 빌드 (solution.c 와 같은 방법, SRC=practice):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   CF=(-O2 -g -std=gnu11 -pthread -I. -I$K/include -I$P -I$P/utils)   # 배열: bash·zsh 둘 다 OK
 *   SRC=practice
 *   cc "${CF[@]}" -Wall -Wextra -Werror -c $SRC.c hooks.c
 *   cc "${CF[@]}" -Wall -Wextra -c $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 *   cc -pthread $SRC.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o $SRC && ./$SRC
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
                      else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

extern volatile unsigned long g_idle_hook_calls, g_stack_overflow_calls;   /* hooks.c */

#define SUP_PRIO 6u
#define HOG_DEPTH 300u

typedef struct {
    const char *name;
    char tag;
    UBaseType_t prio;
    configSTACK_DEPTH_TYPE depth;
    TaskHandle_t h;
    volatile unsigned spurious;        /* ulTaskNotifyTake(portMAX_DELAY) 가 0 을 돌려준 횟수 */
} worker_t;

static worker_t g_w[3] = {
    { "telem", 'T', 1, 1024, NULL, 0 },
    { "ctrl",  'C', 2,  512, NULL, 0 },
    { "imu",   'I', 3,  256, NULL, 0 },
};
enum { W_TELEM, W_CTRL, W_IMU };
static TaskHandle_t g_sleeper, g_hog;

static char g_log[64];
static volatile unsigned g_nlog;
static void log_c(char c) {
    taskENTER_CRITICAL();
    if (g_nlog < sizeof g_log - 1) { g_log[g_nlog++] = c; g_log[g_nlog] = '\0'; }
    taskEXIT_CRITICAL();
}

static void worker(void *p) {
    worker_t *w = p;
    for (;;) {
        uint32_t got = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (got == 0) w->spurious++;      /* 알림 없이 깨어났다 (연습 2) */
        log_c(w->tag);
    }
}

static void sleeper(void *p) {            /* 오래 자는 태스크: 지연 리스트 → 'B' */
    (void)p;
    for (;;) vTaskDelay(100000);
}

/* 연습 4: 300 워드(2400 B) 스택인데 16 KB 지역 배열을 쓴다. Cortex-M 이라면 확실한 오버플로. */
static volatile uintptr_t g_hog_local_addr;
static volatile size_t g_hog_pthread_stack;
static volatile unsigned g_hog_runs;
static void hog(void *p) {
    (void)p;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        volatile char big[16 * 1024];
        for (size_t i = 0; i < sizeof big; i += 64) big[i] = (char)i;
        g_hog_local_addr = (uintptr_t)&big[0];
        g_hog_pthread_stack = pthread_get_stacksize_np(pthread_self());   /* 이 태스크를 실제로 돌리는 pthread 의 스택 */
        g_hog_runs++;
    }
}

static void let_run(void) { vTaskPrioritySet(NULL, tskIDLE_PRIORITY); vTaskPrioritySet(NULL, SUP_PRIO); }

static const TaskStatus_t *find_ts(const TaskStatus_t *ts, UBaseType_t n, const char *name) {
    for (UBaseType_t i = 0; i < n; i++) if (strcmp(ts[i].pcTaskName, name) == 0) return &ts[i];
    return NULL;
}

/* port.c 82-89 의 Thread_t 와 같은 모양 (port.c 안의 private 타입이라 직접 볼 수 없어 따라 썼다) */
typedef struct { pthread_t pthread; void (*pxCode)(void *); void *pvParams; long xDying; void *ev; } thread_mirror_t;

static void supervisor(void *p) {
    (void)p;
    TaskStatus_t ts[10];
    UBaseType_t n;

    for (int i = 0; i < 1000 && g_idle_hook_calls == 0; i++) vTaskDelay(1);   /* 워커들이 첫 대기에 들어간다 */

    printf("== 연습 1: vTaskPrioritySet 이 그 자리에서 선점한다\n");
    xTaskNotifyGive(g_w[W_IMU].h);                  /* imu(3) READY. 나는 6 이라 아직 안 돈다 */
    CHECK(eTaskGetState(g_w[W_IMU].h) == eReady && g_nlog == 0);
    vTaskPrioritySet(NULL, 2);                      /* 나를 2 로 → imu(3) 가 이 호출 안에서 돈다 */
    log_c('|');
    xTaskNotifyGive(g_w[W_TELEM].h);                /* telem(1) READY, 나(2)보다 낮아 안 돈다 */
    CHECK(eTaskGetState(g_w[W_TELEM].h) == eReady);
    vTaskPrioritySet(g_w[W_TELEM].h, 3);            /* 남을 내 위(3 > 2)로 → 이 호출 안에서 telem 이 돈다 */
    log_c('|');
    xTaskNotifyGive(g_w[W_CTRL].h);                 /* ctrl(2) READY, 나(2)와 같음 */
    vTaskPrioritySet(NULL, 3);                      /* 나를 "올리는" 건 전환 사유가 아니다 → ctrl 안 돎 */
    log_c('|');
    vTaskPrioritySet(g_w[W_TELEM].h, 1);
    let_run();                                      /* 이제 ctrl 이 돈다 */
    printf("log: \"%s\"\n", g_log);
    CHECK(strcmp(g_log, "I|T||C") == 0);
    CHECK(uxTaskPriorityGet(NULL) == SUP_PRIO && uxTaskPriorityGet(g_w[W_TELEM].h) == 1);

    printf("\n== 연습 2: X R B S 한 화면에, 그리고 Suspend 된 대기 태스크를 Resume 하면\n");
    g_nlog = 0; g_log[0] = '\0';
    xTaskNotifyGive(g_w[W_IMU].h);                  /* imu: READY 지만 못 돎 → 'R' */
    vTaskSuspend(g_w[W_CTRL].h);                    /* ctrl: 알림 대기 중에 강제로 Suspended → 'S' */
    n = uxTaskGetSystemState(ts, 10, NULL);
    static char buf[1024];
    vTaskListTasks(buf, sizeof buf);
    printf("%s", buf);
    const TaskStatus_t *t_sup = find_ts(ts, n, "sup"), *t_imu = find_ts(ts, n, "imu"), *t_ctrl = find_ts(ts, n, "ctrl"),
                       *t_tel = find_ts(ts, n, "telem"), *t_slp = find_ts(ts, n, "sleeper"), *t_idle = find_ts(ts, n, "IDLE");
    CHECK(n == 7 && t_sup && t_imu && t_ctrl && t_tel && t_slp && t_idle);
    CHECK(t_sup && t_sup->eCurrentState == eRunning);
    CHECK(t_imu && t_idle && t_imu->eCurrentState == eReady && t_idle->eCurrentState == eReady);
    CHECK(t_tel && t_slp && t_tel->eCurrentState == eBlocked && t_slp->eCurrentState == eBlocked);
    CHECK(t_ctrl && t_ctrl->eCurrentState == eSuspended);
    let_run();                                      /* imu 만 돈다. ctrl 은 Suspended */
    CHECK(strcmp(g_log, "I") == 0);
    vTaskResume(g_w[W_CTRL].h);                     /* 알림은 아무도 안 줬다 */
    let_run();
    printf("log: \"%s\", ctrl spurious wakes: %u\n", g_log, g_w[W_CTRL].spurious);
    CHECK(strcmp(g_log, "IC") == 0 && g_w[W_CTRL].spurious == 1);   /* portMAX_DELAY 인데 0 을 돌려받고 깨어났다 */

    printf("\n== 연습 3: 배열 · 버퍼가 작을 때\n");
    CHECK(uxTaskGetSystemState(ts, 3, NULL) == 0);  /* 태스크 7개 > 배열 3칸 → 아무것도 안 쓰고 0 */
    char small[40];
    memset(small, '#', sizeof small);
    vTaskListTasks(small, sizeof small);
    printf("vTaskListTasks(small, 40) -> %zu chars: \"", strlen(small));
    for (const char *c = small; *c; c++) printf("%s", *c == '\t' ? "\\t" : *c == '\r' ? "\\r" : *c == '\n' ? "\\n" : (char[2]){ *c, 0 });
    printf("\"\n");
    CHECK(strlen(small) < sizeof small);            /* 넘치지 않았다 — 대신 마지막 줄이 잘렸다 */
    CHECK(strncmp(small, "sup", 3) == 0 && small[strlen(small) - 1] != '\n');

    printf("\n== 연습 4: POSIX 포트의 HWM\n");
    xTaskNotifyGive(g_hog);
    let_run();
    n = uxTaskGetSystemState(ts, 10, NULL);
    const TaskStatus_t *t_hog = find_ts(ts, n, "hog");
    CHECK(g_hog_runs == 1);
    unsigned k = t_hog ? HOG_DEPTH - (unsigned)t_hog->usStackHighWaterMark : 999;
    printf("hog: depth %u words, hwm %u words, depth-hwm = %u words; sizeof(Thread_t mirror) = %zu bytes\n",
           HOG_DEPTH, t_hog ? (unsigned)t_hog->usStackHighWaterMark : 0, k, sizeof(thread_mirror_t));
    CHECK(k == sizeof(thread_mirror_t) / sizeof(StackType_t));   /* 맨 위 Thread_t 5 워드만 쓰였다 */
    uintptr_t lo = t_hog ? (uintptr_t)t_hog->pxStackBase : 0, hi = lo + HOG_DEPTH * sizeof(StackType_t);
    printf("FreeRTOS stack = [base, base+%u B), local big[] is %s it; pthread stack size = %zu bytes\n",
           HOG_DEPTH * (unsigned)sizeof(StackType_t), (g_hog_local_addr >= lo && g_hog_local_addr < hi) ? "inside" : "outside", (size_t)g_hog_pthread_stack);
    CHECK(t_hog && !(g_hog_local_addr >= lo && g_hog_local_addr < hi));  /* 지역 변수는 FreeRTOS 스택에 없다 */
    CHECK(g_hog_pthread_stack >= 64 * 1024);        /* 실제 스택 = pthread 기본 스택 */
    CHECK(g_stack_overflow_calls == 0);             /* 16 KB 를 썼는데 오버플로 훅은 침묵 */

    printf("\n%d/%d passed\n", g_pass, g_total);
    fflush(stdout);
    exit(g_pass == g_total ? 0 : 1);
}

int main(void) {
    if (xTaskCreate(supervisor, "sup", 512, NULL, SUP_PRIO, NULL) != pdPASS) return 2;
    for (int i = 0; i < 3; i++)
        if (xTaskCreate(worker, g_w[i].name, g_w[i].depth, &g_w[i], g_w[i].prio, &g_w[i].h) != pdPASS) return 2;
    if (xTaskCreate(sleeper, "sleeper", 256, NULL, 1, &g_sleeper) != pdPASS) return 2;
    if (xTaskCreate(hog, "hog", HOG_DEPTH, NULL, 1, &g_hog) != pdPASS) return 2;
    vTaskStartScheduler();
    return 3;
}
