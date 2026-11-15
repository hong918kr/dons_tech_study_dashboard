/* RTOS-M13 FreeRTOS 내부 읽기: xTaskCreate부터 vTaskSwitchContext까지
 *
 * FreeRTOS-Kernel V11.3.2 의 list.c 와 tasks.c 의 스케줄러 핵심 경로를 "같은 의미론" 으로 다시 짠다.
 * 스레드·시그널 없음: tick 은 increment_tick() 을 부르는 것, 태스크 실행은 run_current() 가 스크립트를 한 걸음씩
 * 진행하는 것 (단일 스레드, 결정적).
 *
 *   Part A  list.c : list_init / list_insert_end / list_insert / list_remove / list_next_owner
 *                    (vListInitialise 49, vListInsertEnd 105, vListInsert 139, uxListRemove 217,
 *                     listGET_OWNER_OF_NEXT_ENTRY = include/list.h 286)
 *   Part B  tasks.c: add_to_ready (prvAddTaskToReadyList 287 + taskRECORD_READY_PRIORITY 186/234),
 *                    select_highest (taskSELECT_HIGHEST_PRIORITY_TASK 197/238),
 *                    add_current_to_delayed (prvAddCurrentTaskToDelayedList 8685),
 *                    increment_tick (xTaskIncrementTick 4831)
 *   Part C  tasks.c: add_new_task (prvAddNewTaskToReadyList 2040) → 모터 제어 시나리오를 재생해
 *                    진짜 커널(demo_freertos_trace.c)이 찍은 trace 와 글자 하나까지 같아야 한다.
 * 그 밖의 커널 조각(switch_context, task_delay, task_suspend_self, k_start, 스크립트 실행기)은 주어진 코드다.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: list.c 모델 ======================= */
typedef uint32_t tick_t;                      /* TickType_t (Cortex-M 기본 32비트) */
#define MAX_DELAY 0xFFFFFFFFu                 /* portMAX_DELAY */

typedef struct fl_list fl_list_t;
typedef struct fl_item {
    tick_t value;                             /* xItemValue: 정렬 키 (wake tick, 또는 NPRIO - prio) */
    struct fl_item *next, *prev;              /* pxNext, pxPrevious (원형 이중 연결) */
    void *owner;                              /* pvOwner: 이 항목을 품은 TCB */
    fl_list_t *container;                     /* pxContainer: 지금 들어 있는 리스트 (없으면 NULL) */
} fl_item_t;

struct fl_list {
    unsigned n;                               /* uxNumberOfItems */
    fl_item_t *index;                         /* pxIndex: listGET_OWNER_OF_NEXT_ENTRY 가 마지막으로 돌려준 항목 */
    fl_item_t end;                            /* xListEnd: 값 MAX_DELAY 인 보초(sentinel). 항상 맨 끝 */
};

static void item_init(fl_item_t *it, void *owner) {   /* vListInitialiseItem + listSET_LIST_ITEM_OWNER */
    it->container = NULL;
    it->owner = owner;
}
static int list_empty(const fl_list_t *l) { return l->n == 0; }          /* listLIST_IS_EMPTY */
static unsigned list_len(const fl_list_t *l) { return l->n; }            /* listCURRENT_LIST_LENGTH */
static void *list_head_owner(const fl_list_t *l) {                       /* listGET_OWNER_OF_HEAD_ENTRY */
    return l->end.next ? l->end.next->owner : NULL;
}

/* TODO A1 — vListInitialise (list.c 49–86): 빈 리스트 = 보초 하나가 자기 자신을 가리킨다.
 * index 는 보초, 보초의 값은 MAX_DELAY, owner/container 는 NULL, n = 0. */
static void list_init(fl_list_t *l) {
    (void)l; /* TODO A1 */
}

/* TODO A2 — vListInsertEnd (list.c 105–136): 정렬하지 않는다. 새 항목을 index "바로 앞" 에 넣어
 * list_next_owner 가 가장 나중에 돌려주게 한다 (= 라운드 로빈 줄의 맨 뒤). container 기록, n++. */
static void list_insert_end(fl_list_t *l, fl_item_t *it) {
    (void)l; (void)it; /* TODO A2 */
}

/* TODO A3 — vListInsert (list.c 139–213): value 오름차순 정렬 삽입. 같은 값이 이미 있으면 그 "뒤" 에
 * (안정적). value == MAX_DELAY 면 보초 바로 앞(맨 끝)에 — 일반 루프는 보초 값도 MAX 라 멈추지 않는다.
 * index 는 건드리지 않는다. container 기록, n++. */
static void list_insert(fl_list_t *l, fl_item_t *it) {
    (void)l; (void)it; /* TODO A3 */
}

/* TODO A4 — uxListRemove (list.c 217–247): 항목이 아는 container 에서 뺀다. 빼는 항목이 index 였으면
 * index 를 그 앞 항목으로 옮긴다 (안 그러면 index 가 다른 리스트로 옮겨 간 항목을 가리킨다 — 연습 2).
 * container = NULL, n--, 남은 개수를 돌려준다. */
static unsigned list_remove(fl_item_t *it) {
    (void)it;
    return 0; /* TODO A4 */
}

/* TODO A5 — listGET_OWNER_OF_NEXT_ENTRY (include/list.h 286–297): index 를 한 칸 앞으로. 보초에 닿으면
 * 한 칸 더(첫 항목). 그 항목의 owner 를 돌려준다. 커널은 빈 리스트에 부르지 않는다 — 모델은 NULL. */
static void *list_next_owner(fl_list_t *l) {
    (void)l;
    return NULL; /* TODO A5 */
}

/* 불변식 검사 (주어진 코드): 앞뒤 링크 대칭, 개수, container, index 가 보초나 리스트 안 항목인지. */
static int list_check(const fl_list_t *l) {
    const fl_item_t *p = &l->end;
    unsigned cnt = 0;
    int index_ok = (l->index == &l->end);
    if (!p->next || !p->prev || l->end.value != MAX_DELAY) return 0;
    for (p = l->end.next; p != &l->end; p = p->next) {
        if (!p || !p->next || p->next->prev != p || p->container != l || ++cnt > l->n) return 0;
        if (p == l->index) index_ok = 1;
    }
    return cnt == l->n && index_ok && l->end.prev->next == &l->end;
}

/* 보초 다음부터 owner 이름을 공백으로 이어 붙인다. 테스트 항목은 owner = 이름 문자열,
 * TCB 는 첫 멤버가 char name[8] 이라 TCB 주소 = 이름 주소 (C11 6.7.2.1p15). */
static const char *list_str(const fl_list_t *l, char *buf, size_t cap) {
    size_t m = 0;
    unsigned guard = 0;
    buf[0] = '\0';
    for (const fl_item_t *p = l->end.next; p && p != &l->end && guard < 64; p = p->next, guard++)
        m += (size_t)snprintf(buf + m, m < cap ? cap - m : 0, "%s%s", m ? " " : "", (const char *)p->owner);
    return buf;
}

/* ======================= Part B: tasks.c 스케줄러 모델 ======================= */
#define NPRIO 5                               /* configMAX_PRIORITIES (데모와 같게) */
#define MAXT  8

typedef enum { K_BUSY, K_PERIODIC, K_BOOT } kind_t;
typedef struct tcb {
    char name[8];                             /* pcTaskName */
    unsigned prio;                            /* uxPriority (클수록 급함) */
    fl_item_t state_item;                     /* xStateListItem: ready / delayed / suspended 중 하나 */
    fl_item_t event_item;                     /* xEventListItem: 큐·세마포어 대기 리스트 */
    kind_t kind;                              /* 스크립트 종류 (모델 전용) */
    tick_t period;                            /* K_PERIODIC: vTaskDelay(period) */
    int stop_after, runs;                     /* K_PERIODIC: runs == stop_after 면 시뮬레이션 끝 */
} tcb_t;

enum { EV_NEW, EV_RDY, EV_IN, EV_TICK, EV_DELAY, EV_DLY, EV_OVF, EV_SUSP };

typedef struct kernel {
    fl_list_t ready[NPRIO];                   /* pxReadyTasksLists */
    fl_list_t dl1, dl2;                       /* xDelayedTaskList1/2 */
    fl_list_t *delayed, *overflow;            /* pxDelayedTaskList / pxOverflowDelayedTaskList */
    fl_list_t suspended;                      /* xSuspendedTaskList */
    tcb_t *current;                           /* pxCurrentTCB */
    tick_t tick;                              /* xTickCount */
    tick_t next_unblock;                      /* xNextTaskUnblockTime */
    uint32_t top_ready;                       /* uxTopReadyPriority: generic = 우선순위(상한 힌트) / optimised = 비트맵 */
    int optimised;                            /* configUSE_PORT_OPTIMISED_TASK_SELECTION */
    int running;                              /* xSchedulerRunning */
    int yield_pending;                        /* xYieldPendings[0] */
    unsigned ntasks, overflows;               /* uxCurrentNumberOfTasks, xNumOfOverflows */
    unsigned scan_steps;                      /* generic 선택이 빈 리스트를 지나친 횟수 (op count) */
    unsigned walks;                           /* increment_tick 의 지연 리스트 루프 반복 횟수 (op count) */
    unsigned asserts;                         /* configASSERT 실패 횟수 (모델은 abort 대신 센다) */
    int rec, stopped;
    tick_t sync_tick;                         /* K_BOOT: 이 tick 에 시나리오 시작 */
    void (*boot_fn)(struct kernel *);
    char trace[4096];
    size_t tlen;
    tick_t line_tick;
    int line_open;
    tcb_t t[MAXT];
    int nt;
} kernel_t;

#define KASSERT(k, x) do { if (!(x)) (k)->asserts++; } while (0)

/* uint32 tick → 부호 있는 표시값 (0xFFFFFFFA → -6). 변환을 직접 해서 구현 정의 동작을 피한다. */
static long long stick(tick_t t) { return t <= 0x7FFFFFFFu ? (long long)t : (long long)t - 4294967296LL; }

/* 데모(demo_freertos_trace.c print_trace)와 같은 형식: TICK 이거나 tick 이 바뀌면 새 줄 "[t]". */
static void trace_ev(kernel_t *k, int ev, const char *name, tick_t arg) {
    static const char *const tok[] = { "new", "rdy", "in", "tick", "delay", "dly", "ovf", "susp" };
    char b[64];
    int m = 0;
    tick_t at = (ev == EV_TICK) ? arg : k->tick;
    if (!k->rec) return;
    if (ev == EV_TICK || !k->line_open || at != k->line_tick)
        m += snprintf(b + m, sizeof b - (size_t)m, "%s[%lld]", k->line_open ? "\n" : "", stick(at));
    k->line_tick = at;
    k->line_open = 1;
    if (ev == EV_TICK) m += snprintf(b + m, sizeof b - (size_t)m, " tick");
    else if (ev == EV_DLY || ev == EV_OVF)
        m += snprintf(b + m, sizeof b - (size_t)m, " %s:%s@%lld", tok[ev], name, stick(arg));
    else m += snprintf(b + m, sizeof b - (size_t)m, " %s:%s", tok[ev], name);
    if (k->tlen + (size_t)m + 2 < sizeof k->trace) {
        memcpy(k->trace + k->tlen, b, (size_t)m);
        k->tlen += (size_t)m;
        k->trace[k->tlen] = '\0';
    }
}

static unsigned clz32(uint32_t x) {           /* RTOS-B04 의 CLZ (Cortex-M: clz 한 명령, portmacro.h 155) */
    unsigned n = 0;
    if (x == 0) return 32;
    while (!(x & 0x80000000u)) { x <<= 1; n++; }
    return n;
}

static void prv_reset_next_unblock(kernel_t *k) {     /* prvResetNextTaskUnblockTime (tasks.c 6621) */
    k->next_unblock = list_empty(k->delayed) ? MAX_DELAY : k->delayed->end.next->value;
}

/* TODO B1 — prvAddTaskToReadyList (tasks.c 287–293): trace rdy → taskRECORD_READY_PRIORITY → 그 우선순위
 * ready 리스트에 list_insert_end. RECORD: generic 은 "더 크면 올림"(186–192, 내리지 않는다),
 * optimised 는 비트 세움 (ARM_CM4F portmacro.h 166). */
static void add_to_ready(kernel_t *k, tcb_t *t) {
    (void)k; (void)t; /* TODO B1 */
}

/* TODO B2 — taskSELECT_HIGHEST_PRIORITY_TASK.
 * generic (tasks.c 197–212): top_ready 부터 빈 리스트를 내려가며(지나칠 때마다 scan_steps++, 0 아래로 가면
 * KASSERT), 찾은 리스트에서 list_next_owner 로 current 를 고르고, top_ready = 찾은 우선순위 (여기서만 내린다).
 * optimised (238–246): p = 31 - clz32(top_ready), 그 리스트가 비어 있지 않아야 한다(KASSERT). */
static void select_highest(kernel_t *k) {
    (void)k; (void)clz32; /* TODO B2 */
}

/* vTaskSwitchContext (tasks.c 5215–5298, 단일 코어). 스케줄러 정지 처리는 연습 3. */
static void switch_context(kernel_t *k) {
    k->yield_pending = 0;
    select_highest(k);
    trace_ev(k, EV_IN, k->current ? k->current->name : "?", 0);
}

/* TODO B3 — prvAddCurrentTaskToDelayedList (tasks.c 8685–8760, INCLUDE_vTaskSuspend == 1):
 *  1) current 의 state_item 을 ready 리스트에서 뺀다. 남은 개수가 0 이고 optimised 면 비트를 지운다
 *     (portRESET_READY_PRIORITY 8708; generic 에선 빈 매크로 225).
 *  2) ticks == MAX_DELAY 이고 can_block_indef 면 suspended 리스트 끝에 넣고 끝.
 *  3) wake = tick + ticks (wrap 허용). state_item.value = wake.
 *     wake < tick 이면 trace ovf → overflow 리스트에 정렬 삽입 (next_unblock 은 안 건드림).
 *     아니면 trace dly → delayed 리스트에 정렬 삽입, wake < next_unblock 이면 next_unblock = wake. */
static void add_current_to_delayed(kernel_t *k, tick_t ticks, int can_block_indef) {
    (void)k; (void)ticks; (void)can_block_indef; /* TODO B3 */
}

/* TODO B4 — xTaskIncrementTick (tasks.c 4831–5078, 단일 코어, 선점 + time slicing 켬, 스케줄러 정지 없음):
 *  1) now = tick + 1, trace_ev(EV_TICK, NULL, now), tick = now.
 *  2) now == 0 이면 taskSWITCH_DELAYED_LISTS (267–279): delayed 가 비어 있어야 함(KASSERT), 두 포인터 교환,
 *     overflows++, prv_reset_next_unblock.
 *  3) now >= next_unblock 일 때만 루프 (조기 종료, 4871). 반복마다 walks++:
 *     delayed 가 비면 next_unblock = MAX_DELAY, break. 머리 항목 값 > now 면 next_unblock = 그 값, break.
 *     아니면 state_item 을 빼고, event_item 이 어떤 리스트에 있으면 그것도 빼고, add_to_ready.
 *     깨운 태스크 prio > current prio 면 switch 필요 (4944, "같으면" 아님).
 *  4) current 의 우선순위 ready 리스트 길이 > 1 이면 switch 필요 (time slicing, 4971).
 *  5) yield_pending 이면 switch 필요 (5020). switch 필요 여부를 돌려준다. */
static int increment_tick(kernel_t *k) {
    (void)k; (void)prv_reset_next_unblock;
    return 0; /* TODO B4 */
}

/* ======================= Part C: 태스크 생성과 시나리오 ======================= */

/* TODO C1 — prvAddNewTaskToReadyList (tasks.c 2040–2114, 단일 코어):
 *  ntasks++. current 가 NULL 이면 current = t (첫 태스크면 커널은 여기서 prvInitialiseTaskLists — 모델은 k_init 이 함).
 *  아니고 스케줄러가 아직 안 돌면: current->prio <= t->prio 일 때 current = t ("같으면 나중 것", 2073).
 *  trace new → add_to_ready.
 *  스케줄러가 돌고 있고 current->prio < t->prio 면 즉시 switch_context (taskYIELD_ANY_CORE_IF_USING_PREEMPTION 88–98). */
static void add_new_task(kernel_t *k, tcb_t *t) {
    (void)k; (void)t; /* TODO C1 */
}

/* ---- 아래는 주어진 커널 조각 ---- */
static void k_init(kernel_t *k, tick_t tick0, int optimised) {      /* prvInitialiseTaskLists 6175 */
    memset(k, 0, sizeof *k);
    for (int p = 0; p < NPRIO; p++) list_init(&k->ready[p]);
    list_init(&k->dl1);
    list_init(&k->dl2);
    list_init(&k->suspended);
    k->delayed = &k->dl1;
    k->overflow = &k->dl2;
    k->tick = tick0;
    k->optimised = optimised;
    k->rec = 1;
}

/* prvInitialiseNewTask 의 리스트 부분 (tasks.c 1914–1930): 두 항목 초기화, owner = TCB,
 * event_item 값 = NPRIO - prio (이벤트 리스트가 "높은 우선순위 먼저" 로 정렬되게). */
static tcb_t *k_tcb(kernel_t *k, const char *name, unsigned prio, kind_t kind, tick_t period, int stop_after) {
    tcb_t *t = &k->t[k->nt++];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    t->prio = prio;
    t->kind = kind;
    t->period = period;
    t->stop_after = stop_after;
    item_init(&t->state_item, t);
    item_init(&t->event_item, t);
    t->event_item.value = (tick_t)(NPRIO - prio);
    return t;
}
static tcb_t *k_create(kernel_t *k, const char *name, unsigned prio, kind_t kind, tick_t period, int stop_after) {
    tcb_t *t = k_tcb(k, name, prio, kind, period, stop_after);   /* xTaskCreate → prvCreateTask */
    add_new_task(k, t);                                         /*            → prvAddNewTaskToReadyList */
    return t;
}

static void task_delay(kernel_t *k, tick_t d) {     /* vTaskDelay (tasks.c 2506–2549), d > 0 */
    trace_ev(k, EV_DELAY, k->current->name, 0);
    add_current_to_delayed(k, d, 0);                /* xCanBlockIndefinitely = pdFALSE (2528) */
    switch_context(k);                              /* xTaskResumeAll 이 pdFALSE → taskYIELD_WITHIN_API */
}

static void task_suspend_self(kernel_t *k) {        /* vTaskSuspend(NULL) (tasks.c 3197–) */
    tcb_t *t = k->current;
    trace_ev(k, EV_SUSP, t->name, 0);
    if (list_remove(&t->state_item) == 0 && k->optimised) k->top_ready &= ~(1u << t->prio);
    if (t->event_item.container) list_remove(&t->event_item);
    list_insert_end(&k->suspended, &t->state_item);
    switch_context(k);
}

static void k_start(kernel_t *k) {                  /* vTaskStartScheduler (tasks.c 3742–3818) */
    k_create(k, "IDLE", 0, K_BUSY, 0, 0);           /* prvCreateIdleTasks */
    k->next_unblock = MAX_DELAY;                    /* 3797 */
    k->running = 1;                                 /* 3798 */
    trace_ev(k, EV_IN, k->current ? k->current->name : "?", 0);   /* traceTASK_SWITCHED_IN 3809 */
    k->rec = 0;                                     /* traceSTARTING_SCHEDULER 3811 → 데모의 m13_pause */
}

/* 지금 태스크를 "다음 tick 까지" 돌린다: 블록하는 태스크는 블록하고, 다음 태스크가 이어서 돈다. */
static void run_current(kernel_t *k) {
    for (int guard = 0; guard < 32 && !k->stopped; guard++) {
        tcb_t *t = k->current;
        if (!t) { k->stopped = 1; return; }
        if (t->kind == K_BUSY) return;
        if (t->kind == K_BOOT) {
            if ((int32_t)(k->tick - k->sync_tick) < 0) return;   /* 아직 폴링 중 (wrap 안전 비교) */
            k->rec = 1;
            if (k->boot_fn) k->boot_fn(k);
            task_suspend_self(k);
            continue;
        }
        if (t->stop_after && t->runs == t->stop_after) { k->rec = 0; k->stopped = 1; return; }
        t->runs++;
        task_delay(k, t->period);
    }
}

static void k_run(kernel_t *k, int max_ticks) {
    k_start(k);
    run_current(k);
    for (int i = 0; i < max_ticks && !k->stopped; i++) {
        if (increment_tick(k)) switch_context(k);  /* SysTick → (PendSV) → vTaskSwitchContext */
        run_current(k);
    }
    if (k->tlen + 2 < sizeof k->trace) { k->trace[k->tlen++] = '\n'; k->trace[k->tlen] = '\0'; }
}

/* 데모와 같은 시나리오: Boot 가 tick -6 에 모터 제어 태스크들을 만든다. */
static void boot_motor(kernel_t *k) {
    k_create(k, "LogA", 1, K_BUSY, 0, 0);
    k_create(k, "Ctrl", 3, K_PERIODIC, 4, 3);       /* 모터 제어: 4 tick 주기, 세 번째로 깨면 끝 */
    k_create(k, "LogB", 1, K_BUSY, 0, 0);
    k_create(k, "Comm", 2, K_PERIODIC, 3, 0);       /* 통신 폴링: 3 tick 주기 */
}
static void scenario_motor(kernel_t *k, int optimised) {
    k_init(k, (tick_t)-30, optimised);              /* configINITIAL_TICK_COUNT = -30 */
    k->sync_tick = (tick_t)-6;
    k->boot_fn = boot_motor;
    k_create(k, "Boot", 4, K_BOOT, 0, 0);
    k_run(k, 64);
}

/* 진짜 FreeRTOS V11.3.2 (POSIX 포트) 출력 — demo_freertos_trace.c 를 돌려 그대로 붙였다 (60/60 run 동일). */
static const char KERNEL_TRACE[] =
    "[-30] new:Boot rdy:Boot new:IDLE rdy:IDLE in:Boot\n"
    "[-6] new:LogA rdy:LogA new:Ctrl rdy:Ctrl new:LogB rdy:LogB new:Comm rdy:Comm susp:Boot in:Ctrl delay:Ctrl dly:Ctrl@-2 in:Comm delay:Comm dly:Comm@-3 in:LogA\n"
    "[-5] tick in:LogB\n"
    "[-4] tick in:LogA\n"
    "[-3] tick rdy:Comm in:Comm delay:Comm ovf:Comm@0 in:LogB\n"
    "[-2] tick rdy:Ctrl in:Ctrl delay:Ctrl ovf:Ctrl@2 in:LogA\n"
    "[-1] tick in:LogB\n"
    "[0] tick rdy:Comm in:Comm delay:Comm dly:Comm@3 in:LogA\n"
    "[1] tick in:LogB\n"
    "[2] tick rdy:Ctrl in:Ctrl delay:Ctrl dly:Ctrl@6 in:LogA\n"
    "[3] tick rdy:Comm in:Comm delay:Comm dly:Comm@6 in:LogB\n"
    "[4] tick in:LogA\n"
    "[5] tick in:LogB\n"
    "[6] tick rdy:Ctrl rdy:Comm in:Ctrl\n";

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STR_IS(l, s) (strcmp(list_str((l), buf, sizeof buf), (s)) == 0)
static const char *nmof(void *o) { return o ? (const char *)o : "-"; }   /* owner 이름 (NULL 이면 "-") */
static const char *cur_name(const kernel_t *k) { return k->current ? k->current->name : "-"; }

static uint32_t g_rng = 0x13579BDFu;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 퍼즈 기준 모델: 배열 + index 위치(-1 = 보초). list.c 의 의미를 가장 단순하게 쓴 것. */
typedef struct { fl_item_t *a[32]; int n, idx; } ref_t;
static void ref_put(ref_t *r, int pos, fl_item_t *it) {
    for (int i = r->n; i > pos; i--) r->a[i] = r->a[i - 1];
    r->a[pos] = it;
    r->n++;
    if (r->idx >= pos) r->idx++;
}
static void ref_insert_end(ref_t *r, fl_item_t *it) { ref_put(r, r->idx < 0 ? r->n : r->idx, it); }
static void ref_insert(ref_t *r, fl_item_t *it) {
    int pos = 0;
    if (it->value == MAX_DELAY) pos = r->n;
    else while (pos < r->n && r->a[pos]->value <= it->value) pos++;
    ref_put(r, pos, it);
}
static void ref_remove(ref_t *r, fl_item_t *it) {
    int pos = 0;
    while (r->a[pos] != it) pos++;
    if (r->idx == pos) r->idx = pos - 1;
    else if (r->idx > pos) r->idx--;
    for (int i = pos; i < r->n - 1; i++) r->a[i] = r->a[i + 1];
    r->n--;
}
static void *ref_next(ref_t *r) {
    if (r->n == 0) return NULL;
    r->idx = (r->idx + 1 >= r->n) ? 0 : r->idx + 1;
    return r->a[r->idx]->owner;
}
static int ref_same(const fl_list_t *l, const ref_t *r) {
    const fl_item_t *p = l->end.next;
    if (l->n != (unsigned)r->n) return 0;
    for (int i = 0; i < r->n; i++, p = p->next) {
        if (!p || p != r->a[i]) return 0;
        if (l->index == p && r->idx != i) return 0;
    }
    return (r->idx >= 0) || l->index == &l->end;
}

static void test_part_a(void) {
    static fl_list_t l;
    static fl_item_t it[8];
    static const char *nm[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
    char buf[256];
    memset(&l, 0, sizeof l);
    memset(it, 0, sizeof it);
    for (int i = 0; i < 8; i++) item_init(&it[i], (void *)nm[i]);

    /* A1 빈 리스트 = 보초 하나 */
    list_init(&l);
    CHECK(l.n == 0 && l.index == &l.end && l.end.value == MAX_DELAY);
    CHECK(l.end.next == &l.end && l.end.prev == &l.end && list_check(&l));
    CHECK(list_next_owner(&l) == NULL);

    /* A2 정렬 삽입 + 같은 값은 뒤에 (안정적) */
    it[0].value = 5; it[1].value = 1; it[2].value = 3; it[3].value = 3; it[4].value = 3; it[5].value = 4;
    for (int i = 0; i < 6; i++) list_insert(&l, &it[i]);
    CHECK(STR_IS(&l, "B C D E F A"));                     /* 1 3 3 3 4 5, 3 끼리는 C D E 넣은 순서 */
    CHECK(l.n == 6 && list_check(&l) && it[3].container == &l);
    CHECK(strcmp(nmof(list_head_owner(&l)), "B") == 0);
    CHECK(l.index == &l.end);                             /* vListInsert 는 index 를 안 건드린다 */

    /* A3 MAX_DELAY 값: 맨 끝, 서로는 넣은 순서 (보초 값과 같아도 무한 루프 없음) */
    list_init(&l);
    it[0].value = MAX_DELAY; it[1].value = 7; it[2].value = MAX_DELAY; it[3].value = MAX_DELAY - 1;
    for (int i = 0; i < 4; i++) list_insert(&l, &it[i]);
    CHECK(STR_IS(&l, "B D A C"));
    CHECK(list_check(&l));

    /* A4 insert_end: 새 리스트(index = 보초)면 꼬리에 */
    list_init(&l);
    for (int i = 0; i < 3; i++) list_insert_end(&l, &it[i]);
    CHECK(STR_IS(&l, "A B C") && l.n == 3);

    /* A5 라운드 로빈: 보초를 건너뛰며 계속 돈다 */
    {
        char seq[32] = "";
        for (int i = 0; i < 5; i++) strcat(seq, nmof(list_next_owner(&l)));
        CHECK(strcmp(seq, "ABCAB") == 0);
        CHECK(l.index == &it[1]);                         /* 마지막으로 돌려준 B */
    }

    /* A6 insert_end 는 index "앞" 에: B 가 실행 중일 때 준비된 D 는 줄의 맨 뒤 */
    list_insert_end(&l, &it[3]);
    CHECK(STR_IS(&l, "A D B C"));                         /* 물리 순서 */
    {
        char seq[32] = "";
        for (int i = 0; i < 4; i++) strcat(seq, nmof(list_next_owner(&l)));
        CHECK(strcmp(seq, "CADB") == 0);                  /* 차례: C, A, D(새로 온 것은 마지막), B */
    }

    /* A7 index 항목을 빼면 index 는 그 앞으로 → 다음 차례는 뺀 항목의 다음 */
    CHECK(l.index == &it[1]);                             /* 지금 index = B */
    CHECK(list_remove(&it[1]) == 3u);                     /* 남은 개수 반환 */
    CHECK(l.index == &it[3] && it[1].container == NULL);  /* B 앞은 D */
    CHECK(strcmp(nmof(list_next_owner(&l)), "C") == 0);
    CHECK(list_check(&l));

    /* A8 index 가 아닌 항목을 빼면 index 그대로 */
    CHECK(list_remove(&it[0]) == 2u && l.index == &it[2] && STR_IS(&l, "D C"));

    /* A9 하나 남은 리스트: 같은 항목을 계속 / 다 빼면 보초만 */
    list_remove(&it[2]);
    CHECK(strcmp(nmof(list_next_owner(&l)), "D") == 0 && strcmp(nmof(list_next_owner(&l)), "D") == 0);
    CHECK(list_remove(&it[3]) == 0u && l.index == &l.end);
    CHECK(l.end.next == &l.end && l.end.prev == &l.end && list_check(&l));

    /* A10 퍼즈: 무작위 2000 연산을 배열 기준 모델과 비교 (순서·index·개수·불변식) */
    {
        static fl_item_t pool[16];
        static const char *pn[] = { "p0","p1","p2","p3","p4","p5","p6","p7","p8","p9","pa","pb","pc","pd","pe","pf" };
        static const tick_t vals[] = { 1, 2, 2, 3, 7, 7, 100, MAX_DELAY - 1, MAX_DELAY };
        ref_t r;
        int bad = 0, nexts = 0;
        memset(&r, 0, sizeof r);
        r.idx = -1;
        memset(&l, 0, sizeof l);
        list_init(&l);
        memset(pool, 0, sizeof pool);
        for (int i = 0; i < 16; i++) item_init(&pool[i], (void *)pn[i]);
        for (int step = 0; step < 2000; step++) {
            fl_item_t *x = &pool[xs32() % 16];
            unsigned op = xs32() % 4;
            if (op == 3) {
                void *a = list_next_owner(&l), *b = ref_next(&r);
                nexts++;
                if (a != b) bad++;
            } else if (x->container == NULL) {
                if (op == 0) { list_insert_end(&l, x); ref_insert_end(&r, x); }
                else { x->value = vals[xs32() % 9]; list_insert(&l, x); ref_insert(&r, x); }
            } else if (x->container == &l) {
                list_remove(x);
                ref_remove(&r, x);
            } else {
                bad++;                                    /* container 가 엉뚱함 */
                break;
            }
            if (!ref_same(&l, &r) || !list_check(&l)) { bad++; break; }
        }
        printf("Part A fuzz: 2000 ops (%d next_owner), mismatches=%d\n", nexts, bad);
        CHECK(bad == 0);
    }
}

static void test_part_b(void) {
    static kernel_t k;
    tcb_t *a, *b, *c, *h, *m, *x;

    /* B1 + B2 generic: uxTopReadyPriority 는 "상한 힌트" — 비어도 바로 안 내려간다 */
    k_init(&k, 100, 0);
    k.rec = 0;
    a = k_tcb(&k, "a", 1, K_BUSY, 0, 0);
    b = k_tcb(&k, "b", 3, K_BUSY, 0, 0);
    c = k_tcb(&k, "c", 1, K_BUSY, 0, 0);
    add_to_ready(&k, a); add_to_ready(&k, b); add_to_ready(&k, c);
    k.next_unblock = MAX_DELAY;                           /* 커널은 시작 때 MAX 로 (3797) */
    CHECK(k.top_ready == 3 && list_len(&k.ready[1]) == 2 && b->state_item.container == &k.ready[3]);
    select_highest(&k);
    CHECK(k.current == b && k.scan_steps == 0);
    add_current_to_delayed(&k, 5, 0);                     /* b 블록: generic 은 리셋 매크로가 비어 있다 */
    CHECK(k.top_ready == 3 && list_empty(&k.ready[3]));
    CHECK(b->state_item.value == 105u && b->state_item.container == k.delayed && k.next_unblock == 105u);
    select_highest(&k);
    CHECK(k.current == a && k.top_ready == 1 && k.scan_steps == 2);   /* 3→2→1 두 칸 내려감 */
    select_highest(&k);
    CHECK(k.current == c);                                /* 같은 우선순위는 차례대로 */
    select_highest(&k);
    CHECK(k.current == a && k.asserts == 0);

    /* B1 + B2 optimised: 비트맵은 정확하다 */
    k_init(&k, 100, 1);
    k.rec = 0;
    a = k_tcb(&k, "a", 1, K_BUSY, 0, 0);
    b = k_tcb(&k, "b", 3, K_BUSY, 0, 0);
    add_to_ready(&k, a); add_to_ready(&k, b);
    CHECK(k.top_ready == 0x0Au);
    select_highest(&k);
    CHECK(k.current == b);
    add_current_to_delayed(&k, 5, 0);
    CHECK(k.top_ready == 0x02u);                          /* 마지막 하나가 빠질 때만 비트를 지운다 */
    select_highest(&k);
    CHECK(k.current == a && k.scan_steps == 0);

    /* B3 overflow 리스트, MAX_DELAY 두 가지 */
    k_init(&k, 0xFFFFFFFEu, 0);
    k.rec = 0;
    a = k_tcb(&k, "a", 1, K_BUSY, 0, 0);
    b = k_tcb(&k, "b", 2, K_BUSY, 0, 0);
    c = k_tcb(&k, "c", 2, K_BUSY, 0, 0);
    add_to_ready(&k, a); add_to_ready(&k, b); add_to_ready(&k, c);
    k.next_unblock = MAX_DELAY;
    k.current = b;
    add_current_to_delayed(&k, 5, 0);                     /* 0xFFFFFFFE + 5 = 3 → wrap */
    CHECK(b->state_item.container == k.overflow && b->state_item.value == 3u && k.next_unblock == MAX_DELAY);
    k.current = c;
    add_current_to_delayed(&k, MAX_DELAY, 1);             /* 무한 대기 → suspended (큐 API 의 portMAX_DELAY) */
    CHECK(c->state_item.container == &k.suspended);
    k_init(&k, 100, 0);
    k.rec = 0;
    a = k_tcb(&k, "a", 1, K_BUSY, 0, 0);
    add_to_ready(&k, a);
    k.next_unblock = MAX_DELAY;
    k.current = a;
    add_current_to_delayed(&k, MAX_DELAY, 0);             /* vTaskDelay(portMAX_DELAY): pdFALSE → 100+MAX = 99 */
    CHECK(a->state_item.container == k.overflow && a->state_item.value == 99u);

    /* B4 xTaskIncrementTick: 조기 종료, 깨우기, wrap 에서 리스트 교환, 이벤트 리스트에서도 빼기 */
    k_init(&k, 0xFFFFFFFDu, 0);                           /* tick -3 */
    k.rec = 0;
    x = k_tcb(&k, "x", 1, K_BUSY, 0, 0);
    h = k_tcb(&k, "h", 3, K_BUSY, 0, 0);
    m = k_tcb(&k, "m", 2, K_BUSY, 0, 0);
    add_to_ready(&k, x); add_to_ready(&k, h); add_to_ready(&k, m);
    k.next_unblock = MAX_DELAY;
    k.current = h; add_current_to_delayed(&k, 2, 0);      /* wake -1 */
    k.current = m; add_current_to_delayed(&k, 4, 0);      /* wake 1 → overflow */
    {
        static fl_list_t evq;                             /* m 은 큐도 기다린다 (xTasksWaitingToReceive) */
        list_init(&evq);
        list_insert(&evq, &m->event_item);
        k.current = x;
        CHECK(k.next_unblock == 0xFFFFFFFFu);
        CHECK(increment_tick(&k) == 0 && k.walks == 0);   /* -2 < next_unblock → 리스트를 보지도 않는다 */
        CHECK(increment_tick(&k) == 1 && h->state_item.container == &k.ready[3]);   /* -1: h 깸, 3 > 1 */
        CHECK(k.next_unblock == MAX_DELAY && k.walks == 2);
        CHECK(increment_tick(&k) == 0 && k.tick == 0u);   /* 0: wrap → 교환 */
        CHECK(k.overflows == 1 && k.delayed == &k.dl2 && k.next_unblock == 1u && k.asserts == 0);
        CHECK(increment_tick(&k) == 1 && m->state_item.container == &k.ready[2]);   /* 1: m 타임아웃 */
        CHECK(m->event_item.container == NULL && list_empty(&evq));                 /* 두 리스트 모두에서 */
        k.current = m;                                    /* m 이 돌고 있고 ready[2] 엔 m 혼자 */
        CHECK(increment_tick(&k) == 0);
        add_to_ready(&k, k_tcb(&k, "m2", 2, K_BUSY, 0, 0));
        CHECK(increment_tick(&k) == 1);                   /* 같은 우선순위 둘 → time slicing */
    }
    /* wrap 순간 delayed 리스트가 비어 있지 않으면 configASSERT (267–272) */
    k_init(&k, 0xFFFFFFFFu, 0);
    k.rec = 0;
    a = k_tcb(&k, "a", 1, K_BUSY, 0, 0);
    b = k_tcb(&k, "b", 2, K_BUSY, 0, 0);
    add_to_ready(&k, a);
    b->state_item.value = 5;
    list_insert(k.delayed, &b->state_item);               /* 잘못된 상태를 일부러 만든다 */
    k.current = a;
    k.next_unblock = 5;
    (void)increment_tick(&k);
    CHECK(k.asserts == 1);
}

static void test_part_c(void) {
    static kernel_t k;
    char buf[256];
    /* C1 스케줄러 시작 전: 가장 높은 우선순위, 같으면 나중에 만든 것이 첫 태스크 */
    k_init(&k, 0, 0);
    k_create(&k, "X", 1, K_BUSY, 0, 0);
    tcb_t *y = k_create(&k, "Y", 3, K_BUSY, 0, 0);
    tcb_t *z = k_create(&k, "Z", 3, K_BUSY, 0, 0);
    k_create(&k, "W", 2, K_BUSY, 0, 0);
    CHECK(k.current == z && k.ntasks == 4 && y->state_item.container == &k.ready[3]);
    CHECK(strcmp(k.trace, "[0] new:X rdy:X new:Y rdy:Y new:Z rdy:Z new:W rdy:W") == 0);
    CHECK(STR_IS(&k.ready[3], "Y Z") && STR_IS(&k.ready[1], "X"));
    k.running = 1;
    k_create(&k, "V", 4, K_BUSY, 0, 0);                   /* 실행 중 더 높은 태스크 생성 → 즉시 스위치 */
    CHECK(strcmp(cur_name(&k), "V") == 0);
    k_create(&k, "U", 4, K_BUSY, 0, 0);                   /* 같은 우선순위는 선점하지 않는다 (<) */
    CHECK(strcmp(cur_name(&k), "V") == 0);

    /* C2 모터 제어 시나리오: 모델 trace == 진짜 커널 trace (generic 선택) */
    scenario_motor(&k, 0);
    printf("model trace (generic):\n%s", k.trace);
    CHECK(strcmp(k.trace, KERNEL_TRACE) == 0);
    CHECK(k.overflows == 1 && k.asserts == 0 && k.stopped);
    CHECK(strcmp(cur_name(&k), "Ctrl") == 0 && k.t[3].runs == 3);   /* t[3] = Ctrl (Boot, IDLE, LogA 다음) */
    {
        int ok = list_check(&k.dl1) && list_check(&k.dl2) && list_check(&k.suspended);
        for (int p = 0; p < NPRIO; p++) ok = ok && list_check(&k.ready[p]);
        CHECK(ok);
    }
    printf("generic: scan_steps=%u walks=%u\n", k.scan_steps, k.walks);

    /* C3 port-optimised(비트맵 + CLZ) 선택도 같은 결정을 내린다 */
    scenario_motor(&k, 1);
    CHECK(strcmp(k.trace, KERNEL_TRACE) == 0);
    CHECK(k.scan_steps == 0 && k.asserts == 0);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
