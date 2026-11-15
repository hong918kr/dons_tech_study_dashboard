/* BUILD: freertos */
/* RTOS-B07 — FreeRTOS POSIX 포트 첫 실행: 태스크·우선순위·스택
 *
 * 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트)를 macOS 프로세스 안에서 돌린다. 태스크 1개 = pthread 1개,
 * 그중 "지금 RUNNING 인 태스크" 의 스레드만 실제로 돌고 나머지는 조건 변수에서 잔다 (port.c 32-38).
 * 그래서 우선순위에 따른 **실행 순서** 는 진짜 커널 그대로이고, **시간** 은 호스트 OS 맘대로다.
 * 이 프로그램은 시간에 기대지 않는 것만 검사한다 → 몇 번을 돌려도 같은 결과.
 *
 * 시나리오 (드론 비행 제어기 흉내): imu(3) > ctrl(2) > telem(1), 감독 태스크 sup(6).
 *   ① 세 워커를 우선순위 표대로 만든다 (만드는 순서는 telem, ctrl, imu — 우선순위 역순)
 *   ② sup 이 셋을 동시에 깨우고 CPU 를 내주면 → 실행 순서는 만든 순서가 아니라 우선순위 순 "ICT"
 *   ③ vTaskListTasks / uxTaskGetSystemState 로 상태 · 우선순위 · 스택 HWM · 번호를 찍는다
 *   ④ 지상국이 로그 덤프를 요청 → telem 을 4 로 올리면(vTaskPrioritySet) 순서가 "TIC" 로 바뀐다
 *   ⑤ 원래대로(1) 내리면 다시 "ICT"
 *
 * 빌드 (자기 파일은 -Werror, 커널은 -Werror 없이 따로 컴파일 — md 3. 시작 코드):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   CF=(-O2 -g -std=gnu11 -pthread -I. -I$K/include -I$P -I$P/utils)   # 배열: bash·zsh 둘 다 OK
 *   SRC=solution        # starter / solution / practice
 *   cc "${CF[@]}" -Wall -Wextra -Werror -c $SRC.c hooks.c
 *   cc "${CF[@]}" -Wall -Wextra -c $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
 *   cc -pthread $SRC.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o $SRC && ./$SRC
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
                      else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

extern volatile unsigned long g_idle_hook_calls, g_stack_overflow_calls;   /* hooks.c */

#define SUP_PRIO   6u
#define SUP_DEPTH  512u
#define NW         3

typedef struct {
    const char *name;
    char tag;                       /* 실행 순서 로그에 남길 글자 */
    UBaseType_t prio;
    configSTACK_DEPTH_TYPE depth;   /* 워드 단위 (StackType_t = 8 바이트) */
    TaskHandle_t h;
} worker_t;

/* 만드는 순서 = 배열 순서 (우선순위 역순으로 일부러 섞었다) */
static worker_t g_w[NW] = {
    { "telem", 'T', 1, 1024, NULL },   /* 텔레메트리: 느긋하다 */
    { "ctrl",  'C', 2,  512, NULL },   /* 자세 제어 루프 */
    { "imu",   'I', 3,  256, NULL },   /* IMU 샘플링: 가장 급하다 */
};
enum { W_TELEM, W_CTRL, W_IMU };

typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    char st;                        /* X R B S D */
    unsigned prio, base, hwm, num;
} row_t;

static char g_order[64];
static volatile unsigned g_nord;
static BaseType_t g_created = pdFAIL;

static void log_run(char c) {
    taskENTER_CRITICAL();
    if (g_nord < sizeof g_order - 1) { g_order[g_nord++] = c; g_order[g_nord] = '\0'; }
    taskEXIT_CRITICAL();
}

/* 워커: "깨워 줄 때까지 막힘 → 한 번 일하고 → 다시 막힘". 막힘 = Blocked 상태 (CPU 를 안 쓴다) */
static void worker(void *p) {
    worker_t *w = p;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        log_run(w->tag);
        if (w->tag == 'T') {                         /* telem 은 2 KB 짜리 지역 버퍼로 리포트를 만든다 */
            volatile char report[2048];
            for (unsigned i = 0; i < sizeof report; i++) report[i] = (char)i;
        }
    }
}

/* ---------------- TODO 1: 워커 3개 만들기 ----------------
 * g_w[] 순서대로 xTaskCreate(worker, 이름, 깊이(워드), &g_w[i], 우선순위, &핸들).
 * 하나라도 실패하면 pdFAIL, 다 되면 pdPASS. */
static BaseType_t create_workers(void) {
    for (int i = 0; i < NW; i++) {
        if (xTaskCreate(worker, g_w[i].name, g_w[i].depth, &g_w[i], g_w[i].prio, &g_w[i].h) != pdPASS)
            return pdFAIL;
    }
    return pdPASS;
}

/* ---------------- TODO 2: eTaskState → vTaskList 글자 ----------------
 * eRunning 'X', eReady 'R', eBlocked 'B', eSuspended 'S', eDeleted 'D', 그 밖 '?' */
static char state_letter(eTaskState s) {
    switch (s) {
    case eRunning:   return 'X';
    case eReady:     return 'R';
    case eBlocked:   return 'B';
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

/* ---------------- TODO 3: uxTaskGetSystemState 로 스냅샷 ----------------
 * 모든 태스크의 이름·상태 글자·현재/기본 우선순위·HWM(워드)·번호를 rows[] 에 채우고
 * 태스크 번호(xTaskNumber) 오름차순으로 정렬해 개수를 돌려준다. 배열이 모자라면 0. */
static UBaseType_t take_snapshot(row_t *rows, UBaseType_t max) {
    TaskStatus_t ts[8];
    UBaseType_t n = uxTaskGetSystemState(ts, 8, NULL);
    if (n > max) return 0;
    for (UBaseType_t i = 0; i < n; i++) {
        row_t r;
        snprintf(r.name, sizeof r.name, "%s", ts[i].pcTaskName);
        r.st = state_letter(ts[i].eCurrentState);
        r.prio = (unsigned)ts[i].uxCurrentPriority;
        r.base = (unsigned)ts[i].uxBasePriority;
        r.hwm = (unsigned)ts[i].usStackHighWaterMark;
        r.num = (unsigned)ts[i].xTaskNumber;
        UBaseType_t j = i;                           /* 삽입 정렬 (번호 오름차순) */
        while (j > 0 && rows[j - 1].num > r.num) { rows[j] = rows[j - 1]; j--; }
        rows[j] = r;
    }
    return n;
}

/* ---------------- TODO 4: 깨운 워커들이 다 돌 때까지 CPU 를 내준다 ----------------
 * 시간(vTaskDelay)에 기대지 말 것. 힌트: 내 우선순위를 idle 로 내렸다가 되돌린다. */
static void let_workers_run(void) {
    vTaskPrioritySet(NULL, tskIDLE_PRIORITY);   /* 이 호출 안에서 바로 전환된다 (tasks.c 2925-2935, 3022-3026) */
    vTaskPrioritySet(NULL, SUP_PRIO);           /* READY 워커가 하나도 없어야 여기까지 온다 */
}

/* ---------------- TODO 5: 우선순위 바꾸기 ----------------
 * h 의 (기본) 우선순위를 p 로 바꾸고, 바꾸기 전 값을 돌려준다. */
static UBaseType_t set_prio(TaskHandle_t h, UBaseType_t p) {
    UBaseType_t old = uxTaskPriorityGet(h);
    vTaskPrioritySet(h, p);
    return old;
}

/* ================= 하네스 (starter 와 같음) ================= */
static void kick_all(void) {                     /* 만든 순서대로 깨운다: T, C, I */
    for (int i = 0; i < NW; i++) if (g_w[i].h) xTaskNotifyGive(g_w[i].h);
}

static void round_and_record(char *out, size_t cap) {
    unsigned start = g_nord;
    kick_all();
    let_workers_run();
    snprintf(out, cap, "%s", g_order + start);
}

static const row_t *find_row(const row_t *rows, UBaseType_t n, const char *name) {
    for (UBaseType_t i = 0; i < n; i++) if (strcmp(rows[i].name, name) == 0) return &rows[i];
    return NULL;
}

/* vTaskListTasks 텍스트를 줄 단위로 읽는다: "이름(공백 패딩)\t상태\t우선순위\tHWM\t번호\r\n" (tasks.c 7518-7526) */
static UBaseType_t parse_vtasklist(const char *buf, row_t *rows, UBaseType_t max, char *order, size_t ocap) {
    UBaseType_t n = 0;
    order[0] = '\0';
    while (*buf && n < max) {
        row_t r = { "", '?', 0, 0, 0, 0 };
        char name[32];
        if (sscanf(buf, "%31s %c %u %u %u", name, &r.st, &r.prio, &r.hwm, &r.num) != 5) break;
        snprintf(r.name, sizeof r.name, "%s", name);
        rows[n++] = r;
        if (order[0]) strncat(order, " ", ocap - strlen(order) - 1);
        strncat(order, r.name, ocap - strlen(order) - 1);
        const char *nl = strchr(buf, '\n');
        if (!nl) break;
        buf = nl + 1;
    }
    return n;
}

static void print_snapshot(const char *title, const row_t *rows, UBaseType_t n) {
    printf("\n-- %s (uxTaskGetSystemState, 번호순)\n", title);
    printf("  num name        st prio base  hwm(words) = bytes\n");
    for (UBaseType_t i = 0; i < n; i++)
        printf("  %3u %-11s %c  %4u %4u  %10u = %u\n", rows[i].num, rows[i].name, rows[i].st,
               rows[i].prio, rows[i].base, rows[i].hwm, rows[i].hwm * (unsigned)sizeof(StackType_t));
}

static int snapshot_matches_vtasklist(const row_t *snap, UBaseType_t n, char *order, size_t ocap) {
    static char buf[1024];
    row_t vt[8];
    vTaskListTasks(buf, sizeof buf);
    printf("\n-- vTaskListTasks() 원문 (이름 / 상태 / 우선순위 / HWM 워드 / 번호)\n%s", buf);
    UBaseType_t m = parse_vtasklist(buf, vt, 8, order, ocap);
    if (m != n || n == 0) return 0;
    for (UBaseType_t i = 0; i < m; i++) {
        const row_t *s = find_row(snap, n, vt[i].name);
        if (!s || s->st != vt[i].st || s->prio != vt[i].prio || s->hwm != vt[i].hwm || s->num != vt[i].num) return 0;
    }
    return 1;
}

static void supervisor(void *p) {
    (void)p;
    row_t rows[8];
    char ord[16], vorder[64];

    printf("== Part A: 설정과 생성\n");
    CHECK(configMAX_PRIORITIES == 7 && configTICK_RATE_HZ == 1000 && pdMS_TO_TICKS(10) == 10);
    CHECK(configUSE_PREEMPTION == 1 && configUSE_TRACE_FACILITY == 1 && configUSE_STATS_FORMATTING_FUNCTIONS == 1);
    CHECK(g_created == pdPASS);
    CHECK(g_w[W_TELEM].h != NULL && g_w[W_CTRL].h != NULL && g_w[W_IMU].h != NULL);
    CHECK(uxTaskGetNumberOfTasks() == 5);               /* sup + 워커 3 + IDLE (타이머 태스크 없음) */
    CHECK(g_w[W_IMU].h && uxTaskPriorityGet(g_w[W_IMU].h) == 3);
    CHECK(g_w[W_CTRL].h && uxTaskPriorityGet(g_w[W_CTRL].h) == 2);
    CHECK(g_w[W_TELEM].h && uxTaskPriorityGet(g_w[W_TELEM].h) == 1);
    for (int i = 0; i < 1000 && g_idle_hook_calls == 0; i++) vTaskDelay(1);
    CHECK(g_idle_hook_calls > 0);                       /* configUSE_IDLE_HOOK=1 → idle 이 불렀다 */

    printf("\n== Part B: 우선순위대로 실행되고, 상태와 스택 여유를 본다\n");
    round_and_record(ord, sizeof ord);
    printf("round 1 run order: \"%s\"\n", ord);
    CHECK(strcmp(ord, "ICT") == 0);                     /* 깨운 순서 T,C,I 가 아니라 우선순위 순 */
    CHECK(uxTaskPriorityGet(NULL) == SUP_PRIO);         /* 내렸던 내 우선순위를 되돌렸다 */
    CHECK(state_letter(eRunning) == 'X' && state_letter(eReady) == 'R' && state_letter(eBlocked) == 'B'
          && state_letter(eSuspended) == 'S' && state_letter(eDeleted) == 'D');
    UBaseType_t n = take_snapshot(rows, 8);
    print_snapshot("after round 1", rows, n);
    CHECK(n == 5);
    const row_t *rs = find_row(rows, n, "sup"), *ri = find_row(rows, n, "imu"), *rc = find_row(rows, n, "ctrl"),
                *rt = find_row(rows, n, "telem"), *rd = find_row(rows, n, "IDLE");
    CHECK(rs && rs->st == 'X' && rs->prio == SUP_PRIO && rs->num == 1);   /* 스냅샷을 찍는 나 자신 = Running */
    CHECK(rd && rd->st == 'R' && rd->prio == 0 && rd->num == 5);          /* idle 은 늘 Ready, 맨 마지막에 생성 */
    CHECK(ri && rc && rt && ri->st == 'B' && rc->st == 'B' && rt->st == 'B'); /* 알림 대기 = Blocked */
    CHECK(rt && rc && ri && rt->num == 2 && rc->num == 3 && ri->num == 4);   /* 번호 = 만든 순서 */
    /* POSIX 포트: 태스크 코드는 pthread 자기 스택에서 돈다 → FreeRTOS 스택 배열은 맨 위 Thread_t 말고는
       아무도 안 건드린다 → HWM = 깊이 - (고정 몇 워드), 태스크가 실제로 얼마나 썼는지와 무관 */
    unsigned k_i = ri ? 256 - ri->hwm : 999, k_c = rc ? 512 - rc->hwm : 999, k_t = rt ? 1024 - rt->hwm : 999;
    printf("depth - hwm: imu %u, ctrl %u, telem %u (words)\n", k_i, k_c, k_t);
    CHECK(k_i == k_c && k_c == k_t && k_i >= 1 && k_i <= 8); /* telem 의 2 KB 지역 버퍼도 HWM 을 못 줄였다 */
    CHECK(rd && rs && 256 - rd->hwm == k_i && SUP_DEPTH - rs->hwm == k_i);
    CHECK(snapshot_matches_vtasklist(rows, n, vorder, sizeof vorder));
    printf("vTaskList row order: %s\n", vorder);
    CHECK(strcmp(vorder, "sup IDLE imu ctrl telem") == 0);   /* Ready(높→낮) → … → 무한 대기 리스트(막힌 순서) */

    printf("\n== Part C: vTaskPrioritySet 으로 순서 바꾸기 (지상국 로그 덤프 요청)\n");
    UBaseType_t old = set_prio(g_w[W_TELEM].h, 4);
    CHECK(old == 1);
    CHECK(uxTaskPriorityGet(g_w[W_TELEM].h) == 4);
    n = take_snapshot(rows, 8);
    rt = find_row(rows, n, "telem");
    print_snapshot("telem boosted to 4 (still blocked)", rows, n);
    CHECK(rt && rt->st == 'B' && rt->prio == 4 && rt->base == 4);   /* 막힌 태스크는 올려도 안 돈다 */
    round_and_record(ord, sizeof ord);
    printf("round 2 run order: \"%s\"\n", ord);
    CHECK(strcmp(ord, "TIC") == 0);
    old = set_prio(g_w[W_TELEM].h, 1);
    CHECK(old == 4);
    round_and_record(ord, sizeof ord);
    printf("round 3 run order: \"%s\"\n", ord);
    CHECK(strcmp(ord, "ICT") == 0);
    printf("full log: \"%s\"\n", g_order);
    CHECK(strcmp(g_order, "ICTTICICT") == 0);
    CHECK(g_stack_overflow_calls == 0);

    printf("\n%d/%d passed\n", g_pass, g_total);
    fflush(stdout);
    exit(g_pass == g_total ? 0 : 1);
}

int main(void) {
    if (xTaskCreate(supervisor, "sup", SUP_DEPTH, NULL, SUP_PRIO, NULL) != pdPASS) return 2;
    g_created = create_workers();
    vTaskStartScheduler();                        /* 돌아오지 않는다. 돌아오면 idle 생성 실패(힙 부족) */
    return 3;
}
