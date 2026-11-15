/* RTOS-M13 추가 연습 — 한 번에 검증
 *
 * solution.c 의 list.c / tasks.c 모델(정답)을 그대로 가져와 네 가지를 더한다:
 *   연습 1  이벤트 리스트: vTaskPlaceOnEventList (tasks.c 5402) / xTaskRemoveFromEventList (5500)
 *           — 큐에서 기다리는 태스크가 "높은 우선순위 먼저, 같으면 FIFO" 로 깨는 이유
 *   연습 2  uxListRemove 가 pxIndex 를 안 고치면 (list.c 232–235 를 지운 버그 버전) 무슨 일이 생기나
 *   연습 3  스케줄러 정지: vTaskSuspendAll (3890) / xTaskResumeAll (4064), xPendedTicks, xPendingReadyList,
 *           vTaskSwitchContext 의 xYieldPendings (5219–5227)
 *   연습 4  generic vs port-optimised 선택: 빈 리스트를 몇 번 지나치나 (op count) + 같은 결정을 내리나
 * 우선순위는 32 단계 (configMAX_PRIORITIES 32 = 비트맵 한 워드의 상한, ARM_CM4F portmacro.h 161).
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
    l->index = &l->end;
    l->end.value = MAX_DELAY;
    l->end.next = &l->end;
    l->end.prev = &l->end;
    l->end.owner = NULL;
    l->end.container = NULL;
    l->n = 0;
}

/* TODO A2 — vListInsertEnd (list.c 105–136): 정렬하지 않는다. 새 항목을 index "바로 앞" 에 넣어
 * list_next_owner 가 가장 나중에 돌려주게 한다 (= 라운드 로빈 줄의 맨 뒤). container 기록, n++. */
static void list_insert_end(fl_list_t *l, fl_item_t *it) {
    fl_item_t *const idx = l->index;
    it->next = idx;
    it->prev = idx->prev;
    idx->prev->next = it;
    idx->prev = it;
    it->container = l;
    l->n++;
}

/* TODO A3 — vListInsert (list.c 139–213): value 오름차순 정렬 삽입. 같은 값이 이미 있으면 그 "뒤" 에
 * (안정적). value == MAX_DELAY 면 보초 바로 앞(맨 끝)에 — 일반 루프는 보초 값도 MAX 라 멈추지 않는다.
 * index 는 건드리지 않는다. container 기록, n++. */
static void list_insert(fl_list_t *l, fl_item_t *it) {
    fl_item_t *p;
    const tick_t v = it->value;
    if (v == MAX_DELAY) {
        p = l->end.prev;
    } else {
        for (p = &l->end; p->next->value <= v; p = p->next) { }
    }
    it->next = p->next;
    it->next->prev = it;
    it->prev = p;
    p->next = it;
    it->container = l;
    l->n++;
}

/* TODO A4 — uxListRemove (list.c 217–247): 항목이 아는 container 에서 뺀다. 빼는 항목이 index 였으면
 * index 를 그 앞 항목으로 옮긴다 (안 그러면 index 가 다른 리스트로 옮겨 간 항목을 가리킨다 — 연습 2).
 * container = NULL, n--, 남은 개수를 돌려준다. */
static unsigned list_remove(fl_item_t *it) {
    fl_list_t *const l = it->container;
    it->next->prev = it->prev;
    it->prev->next = it->next;
    if (l->index == it) l->index = it->prev;
    it->container = NULL;
    l->n--;
    return l->n;
}

/* TODO A5 — listGET_OWNER_OF_NEXT_ENTRY (include/list.h 286–297): index 를 한 칸 앞으로. 보초에 닿으면
 * 한 칸 더(첫 항목). 그 항목의 owner 를 돌려준다. 커널은 빈 리스트에 부르지 않는다 — 모델은 NULL. */
static void *list_next_owner(fl_list_t *l) {
    if (l->n == 0) return NULL;
    l->index = l->index->next;
    if (l->index == &l->end) l->index = l->end.next;
    return l->index->owner;
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
#define NPRIO 32                              /* configMAX_PRIORITIES (연습 4: 비트맵 상한)    */
#define MAXT  16

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
    unsigned sched_suspended;                 /* uxSchedulerSuspended (연습 3) */
    tick_t pended;                            /* xPendedTicks (연습 3) */
    fl_list_t pending_ready;                  /* xPendingReadyList: event_item 으로 걸린다 (연습 3) */
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
    trace_ev(k, EV_RDY, t->name, 0);
    if (k->optimised) k->top_ready |= 1u << t->prio;
    else if (t->prio > k->top_ready) k->top_ready = t->prio;
    list_insert_end(&k->ready[t->prio], &t->state_item);
}

/* TODO B2 — taskSELECT_HIGHEST_PRIORITY_TASK.
 * generic (tasks.c 197–212): top_ready 부터 빈 리스트를 내려가며(지나칠 때마다 scan_steps++, 0 아래로 가면
 * KASSERT), 찾은 리스트에서 list_next_owner 로 current 를 고르고, top_ready = 찾은 우선순위 (여기서만 내린다).
 * optimised (238–246): p = 31 - clz32(top_ready), 그 리스트가 비어 있지 않아야 한다(KASSERT). */
static void select_highest(kernel_t *k) {
    unsigned p;
    if (k->optimised) {
        p = 31u - clz32(k->top_ready);
        if (p >= NPRIO || list_empty(&k->ready[p])) { k->asserts++; return; }
    } else {
        p = k->top_ready;
        while (list_empty(&k->ready[p])) {
            if (p == 0) { k->asserts++; return; }
            --p;
            k->scan_steps++;
        }
        k->top_ready = p;
    }
    k->current = list_next_owner(&k->ready[p]);
}

/* vTaskSwitchContext (tasks.c 5215–5298, 단일 코어) + 연습 3: 스케줄러가 멈춰 있으면 바꾸지 않고
 * xYieldPendings 만 세운다 (5219–5223). */
static void switch_context(kernel_t *k) {
    if (k->sched_suspended) { k->yield_pending = 1; return; }
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
    tcb_t *t = k->current;
    if (list_remove(&t->state_item) == 0 && k->optimised) k->top_ready &= ~(1u << t->prio);
    if (ticks == MAX_DELAY && can_block_indef) {
        list_insert_end(&k->suspended, &t->state_item);
        return;
    }
    tick_t wake = k->tick + ticks;
    t->state_item.value = wake;
    if (wake < k->tick) {
        trace_ev(k, EV_OVF, t->name, wake);
        list_insert(k->overflow, &t->state_item);
    } else {
        trace_ev(k, EV_DLY, t->name, wake);
        list_insert(k->delayed, &t->state_item);
        if (wake < k->next_unblock) k->next_unblock = wake;
    }
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
    int sw = 0;
    const tick_t now = k->tick + 1;
    trace_ev(k, EV_TICK, NULL, now);
    if (k->sched_suspended) { k->pended++; return 0; }    /* 연습 3: tasks.c 5061–5065 */
    k->tick = now;
    if (now == 0) {
        fl_list_t *tmp;
        KASSERT(k, list_empty(k->delayed));
        tmp = k->delayed;
        k->delayed = k->overflow;
        k->overflow = tmp;
        k->overflows++;
        prv_reset_next_unblock(k);
    }
    if (now >= k->next_unblock) {
        for (;;) {
            k->walks++;
            if (list_empty(k->delayed)) { k->next_unblock = MAX_DELAY; break; }
            tcb_t *t = list_head_owner(k->delayed);
            if (now < t->state_item.value) { k->next_unblock = t->state_item.value; break; }
            list_remove(&t->state_item);
            if (t->event_item.container) list_remove(&t->event_item);
            add_to_ready(k, t);
            if (t->prio > k->current->prio) sw = 1;
        }
    }
    if (list_len(&k->ready[k->current->prio]) > 1) sw = 1;
    if (k->yield_pending) sw = 1;
    return sw;
}

/* ---- 커널 조각 (solution.c 와 같음; prvAddNewTaskToReadyList 는 이 연습에서 안 씀) ---- */
static void k_init(kernel_t *k, tick_t tick0, int optimised) {      /* prvInitialiseTaskLists 6175 */
    memset(k, 0, sizeof *k);
    for (int p = 0; p < NPRIO; p++) list_init(&k->ready[p]);
    list_init(&k->dl1);
    list_init(&k->dl2);
    list_init(&k->suspended);
    list_init(&k->pending_ready);
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


/* ======================= 연습 1: 이벤트 리스트 ======================= */
/* vTaskPlaceOnEventList (tasks.c 5402–5428): event_item 을 "정렬" 삽입한다. 값 = NPRIO - prio (1929) 라서
 * 오름차순 = 높은 우선순위가 앞, 같은 우선순위는 vListInsert 의 안정성 덕에 FIFO. 그리고 지연 리스트로
 * (xCanBlockIndefinitely = pdTRUE → portMAX_DELAY 면 suspended 리스트 = 무한 대기). */
static void place_on_event_list(kernel_t *k, fl_list_t *ev, tick_t ticks) {
    list_insert(ev, &k->current->event_item);
    add_current_to_delayed(k, ticks, 1);
}

/* xTaskRemoveFromEventList (tasks.c 5500–5590): 머리 = 가장 높은 우선순위를 깨운다. 스케줄러가 멈춰 있으면
 * ready/지연 리스트를 못 건드리므로 event_item 을 xPendingReadyList 에 걸어 둔다 (5550).
 * 깨운 태스크가 지금 태스크보다 높으면 1 + xYieldPendings (5555–5564). */
static int remove_from_event_list(kernel_t *k, fl_list_t *ev) {
    tcb_t *t = list_head_owner(ev);
    list_remove(&t->event_item);
    if (!k->sched_suspended) {
        list_remove(&t->state_item);
        add_to_ready(k, t);
    } else {
        list_insert_end(&k->pending_ready, &t->event_item);
    }
    if (t->prio > k->current->prio) { k->yield_pending = 1; return 1; }
    return 0;
}

/* ======================= 연습 2: pxIndex 를 안 고치는 uxListRemove ======================= */
static unsigned list_remove_buggy(fl_item_t *it) {    /* list.c 232–235 를 빼먹은 버전 */
    fl_list_t *const l = it->container;
    it->next->prev = it->prev;
    it->prev->next = it->next;
    it->container = NULL;
    l->n--;
    return l->n;
}

/* ======================= 연습 3: 스케줄러 정지 ======================= */
static void suspend_all(kernel_t *k) { k->sched_suspended++; }   /* vTaskSuspendAll (3928): 중첩 가능 */

/* xTaskResumeAll (tasks.c 4064–4200, 단일 코어): 0 이 되는 순간에만
 *  ① xPendingReadyList → ready (더 높으면 yield_pending), ② 하나라도 옮겼으면 next_unblock 재계산,
 *  ③ 밀린 tick(xPendedTicks)을 increment_tick 으로 하나씩 다시 돌린다, ④ yield_pending 이면 바로 스위치. */
static int resume_all(kernel_t *k) {
    tcb_t *t = NULL;
    KASSERT(k, k->sched_suspended != 0);
    if (--k->sched_suspended != 0) return 0;
    while (!list_empty(&k->pending_ready)) {
        t = list_head_owner(&k->pending_ready);
        list_remove(&t->event_item);
        list_remove(&t->state_item);
        add_to_ready(k, t);
        if (t->prio > k->current->prio) k->yield_pending = 1;
    }
    if (t) prv_reset_next_unblock(k);
    while (k->pended) {
        if (increment_tick(k)) k->yield_pending = 1;
        k->pended--;
    }
    if (k->yield_pending) { switch_context(k); return 1; }   /* xAlreadyYielded = pdTRUE */
    return 0;
}

/* ======================= 연습 4: generic vs optimised ======================= */
static int ready_consistent(const kernel_t *k) {
    unsigned hi = 0;
    int any = 0;
    for (unsigned p = 0; p < NPRIO; p++) {
        int ne = !list_empty(&k->ready[p]);
        if (ne) { hi = p; any = 1; }
        if (k->optimised && (((k->top_ready >> p) & 1u) != (unsigned)ne)) return 0;   /* 비트 = 비어 있지 않음 */
        if (!list_check(&k->ready[p])) return 0;
    }
    return k->optimised || !any || k->top_ready >= hi;                              /* generic: 상한 */
}

static unsigned rnd(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* 무작위 워크로드: 매 걸음 지금 태스크가 1..6 tick 블록하거나, tick 이 하나 지나간다. tick 은 0xFFFFFF00 에서
 * 시작해 wrap 을 지난다. 걸음마다 current 이름 한 글자를 log 에 남긴다. */
static void random_run(kernel_t *k, int optimised, uint32_t seed, char *log, int steps, int *bad) {
    static const char *nm[] = { "a", "b", "c", "d", "e", "f", "g", "h" };
    tcb_t *idle;
    k_init(k, 0xFFFFFF00u, optimised);
    k->rec = 0;
    k->next_unblock = MAX_DELAY;
    idle = k_tcb(k, "I", 0, K_BUSY, 0, 0);
    add_to_ready(k, idle);
    for (int i = 0; i < 8; i++) add_to_ready(k, k_tcb(k, nm[i], 1u + rnd(&seed) % 31u, K_BUSY, 0, 0));
    select_highest(k);
    for (int s = 0; s < steps; s++) {
        if (rnd(&seed) % 4u == 0 && k->current != idle) {
            add_current_to_delayed(k, 1u + rnd(&seed) % 6u, 0);
            switch_context(k);
        } else if (increment_tick(k)) {
            switch_context(k);
        }
        log[s] = k->current ? k->current->name[0] : '?';
        if (!ready_consistent(k)) (*bad)++;
    }
    log[steps] = '\0';
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STR_IS(l, s) (strcmp(list_str((l), buf, sizeof buf), (s)) == 0)

static void test_ex1(void) {
    static kernel_t k;
    static fl_list_t q;                                   /* 어떤 큐의 xTasksWaitingToReceive */
    static const char *nm[] = { "R1", "R2", "R3", "R4", "R5" };
    static const unsigned pr[] = { 2, 3, 1, 3, 2 };
    char buf[128], order[64] = "";
    tcb_t *r[5], *L, *T;
    int ret[5];
    k_init(&k, 0, 0);
    k.rec = 0;
    k.next_unblock = MAX_DELAY;
    list_init(&q);
    L = k_tcb(&k, "L", 1, K_BUSY, 0, 0);
    add_to_ready(&k, L);
    for (int i = 0; i < 5; i++) {
        r[i] = k_tcb(&k, nm[i], pr[i], K_BUSY, 0, 0);
        add_to_ready(&k, r[i]);
        k.current = r[i];
        place_on_event_list(&k, &q, 10);                  /* xQueueReceive(q, &x, 10) 에서 막힘 */
    }
    k.current = L;
    CHECK(STR_IS(&q, "R2 R4 R1 R5 R3"));                  /* 3,3 / 2,2 / 1 — 같은 우선순위는 온 순서 */
    CHECK(r[0]->state_item.container == k.delayed && r[0]->state_item.value == 10u);
    for (int i = 0; i < 5; i++) {
        tcb_t *h = list_head_owner(&q);
        strcat(order, h->name);
        strcat(order, " ");
        ret[i] = remove_from_event_list(&k, &q);          /* xQueueSend 가 한 번씩 */
    }
    printf("ex1 wake order: %s\n", order);
    CHECK(strcmp(order, "R2 R4 R1 R5 R3 ") == 0);
    CHECK(ret[0] == 1 && ret[3] == 1 && ret[4] == 0);     /* R3(1) 는 L(1) 보다 높지 않다 */
    CHECK(r[1]->state_item.container == &k.ready[3] && r[1]->event_item.container == NULL);
    CHECK(list_empty(k.delayed) && list_empty(&q));       /* 두 리스트 모두에서 빠졌다 */
    /* 타임아웃 경로: 3 tick 뒤 xTaskIncrementTick 이 지연 리스트와 이벤트 리스트 둘 다에서 뺀다 */
    T = k_tcb(&k, "T", 2, K_BUSY, 0, 0);
    add_to_ready(&k, T);
    k.current = T;
    place_on_event_list(&k, &q, 3);
    k.current = L;
    (void)increment_tick(&k); (void)increment_tick(&k);
    CHECK(T->event_item.container == &q);
    CHECK(increment_tick(&k) == 1 && T->event_item.container == NULL && T->state_item.container == &k.ready[2]);
    /* portMAX_DELAY 로 기다리면 지연 리스트가 아니라 suspended 리스트 (타임아웃 없음) */
    k.current = T;
    place_on_event_list(&k, &q, MAX_DELAY);
    CHECK(T->state_item.container == &k.suspended && T->event_item.container == &q);
}

static void test_ex2(void) {
    static fl_list_t rl, dl;
    static fl_item_t A, B, C, D;
    char buf[64];
    for (int bug = 0; bug < 3; bug++) {
        list_init(&rl);
        list_init(&dl);
        item_init(&A, (void *)"A"); item_init(&B, (void *)"B"); item_init(&C, (void *)"C"); item_init(&D, (void *)"D");
        list_insert_end(&rl, &A); list_insert_end(&rl, &B); list_insert_end(&rl, &C);
        D.value = 50;
        list_insert(&dl, &D);                             /* 이미 블록된 D (wake 50) */
        (void)list_next_owner(&rl);
        (void)list_next_owner(&rl);                       /* B 가 실행 중 (index = B) */
        if (bug == 0) list_remove(&B); else list_remove_buggy(&B);
        B.value = (bug == 2) ? 99 : 10;                   /* B 블록: 지연 리스트로 (D 앞 / 뒤) */
        list_insert(&dl, &B);
        void *nx = list_next_owner(&rl);                  /* 다음 tick 의 time slicing 선택 */
        printf("ex2 %-22s next=%s  ready list check=%d  ready=\"%s\"\n",
               bug == 0 ? "correct uxListRemove:" : bug == 1 ? "buggy, B before D:" : "buggy, B at tail:",
               nx ? (const char *)nx : "NULL(end of delayed list)", list_check(&rl), list_str(&rl, buf, sizeof buf));
        if (bug == 0) CHECK(nx == (void *)C.owner && list_check(&rl));
        if (bug == 1) CHECK(nx == D.owner && D.container == &dl && !list_check(&rl));   /* 블록된 D 를 고름 */
        if (bug == 2) CHECK(nx == NULL && rl.index == &dl.end);                         /* 남의 보초 → owner NULL */
    }
}

static void test_ex3(void) {
    static kernel_t k;
    static fl_list_t q;
    tcb_t *L, *T, *E;
    k_init(&k, 10, 0);
    k.rec = 0;
    k.next_unblock = MAX_DELAY;
    list_init(&q);
    L = k_tcb(&k, "L", 1, K_BUSY, 0, 0);
    T = k_tcb(&k, "T", 3, K_BUSY, 0, 0);
    E = k_tcb(&k, "E", 2, K_BUSY, 0, 0);
    add_to_ready(&k, L); add_to_ready(&k, T); add_to_ready(&k, E);
    k.current = T; add_current_to_delayed(&k, 2, 0);      /* T: wake 12 */
    k.current = E; place_on_event_list(&k, &q, 100);      /* E: 큐 대기 */
    k.current = L;

    suspend_all(&k);                                      /* L 이 vTaskSuspendAll() 구간에 들어감 */
    switch_context(&k);                                   /* 스위치 요청이 와도 */
    CHECK(k.current == L && k.yield_pending == 1);        /* 바꾸지 않고 xYieldPendings 만 */
    for (int i = 0; i < 3; i++) CHECK(increment_tick(&k) == 0);
    CHECK(k.tick == 10u && k.pended == 3u);               /* tick 은 멈추고 밀린 수만 센다 */
    CHECK(remove_from_event_list(&k, &q) == 1);           /* ISR 이 큐에 넣음 → E 깸 (2 > 1) */
    CHECK(E->event_item.container == &k.pending_ready && E->state_item.container == k.delayed);
    CHECK(list_empty(&k.ready[2]));                       /* 아직 ready 아님 */
    suspend_all(&k);
    CHECK(resume_all(&k) == 0 && k.sched_suspended == 1 && k.pended == 3u);   /* 중첩: 바깥까지 기다림 */
    CHECK(resume_all(&k) == 1);                           /* 이제 진짜 재개 → 바로 스위치 */
    CHECK(k.tick == 13u && k.pended == 0u && k.sched_suspended == 0);
    CHECK(T->state_item.container == &k.ready[3] && E->state_item.container == &k.ready[2]);
    CHECK(list_empty(&k.pending_ready) && k.current == T && k.yield_pending == 0);
    CHECK(k.asserts == 0);
}

static void test_ex4(void) {
    static kernel_t k;
    static char log0[3001], log1[3001];
    unsigned steps[2];
    for (int opt = 0; opt < 2; opt++) {
        tcb_t *I, *H;
        k_init(&k, 0, opt);
        k.rec = 0;
        k.next_unblock = MAX_DELAY;
        I = k_tcb(&k, "IDLE", 0, K_BUSY, 0, 0);
        H = k_tcb(&k, "H", 31, K_BUSY, 0, 0);
        add_to_ready(&k, I); add_to_ready(&k, H);
        select_highest(&k);
        for (int c = 0; c < 10; c++) {                    /* H: 1 tick 일하고 1 tick 쉼 × 10 */
            add_current_to_delayed(&k, 1, 0);
            if (c == 0 && !opt) CHECK(k.top_ready == 31u);   /* 비었지만 힌트는 그대로 (지연 갱신) */
            switch_context(&k);
            if (c == 0) CHECK(k.current == I);
            if (increment_tick(&k)) switch_context(&k);
            if (c == 0) CHECK(k.current == H);
        }
        steps[opt] = k.scan_steps;
    }
    printf("ex4 H(31)<->IDLE x10: generic scan_steps=%u, optimised scan_steps=%u (+1 CLZ per switch)\n", steps[0], steps[1]);
    CHECK(steps[0] == 310u && steps[1] == 0u);
    {
        int bad0 = 0, bad1 = 0;
        random_run(&k, 0, 0xC0FFEEu, log0, 3000, &bad0);
        unsigned g_steps = k.scan_steps, ovf = k.overflows, as0 = k.asserts;
        random_run(&k, 1, 0xC0FFEEu, log1, 3000, &bad1);
        printf("ex4 random 3000 steps across a wrap (overflows=%u): same decisions=%d, generic scan_steps=%u, "
               "inconsistent=%d/%d\n", ovf, strcmp(log0, log1) == 0, g_steps, bad0, bad1);
        CHECK(strcmp(log0, log1) == 0 && bad0 == 0 && bad1 == 0);
        CHECK(ovf == 1 && as0 == 0 && k.asserts == 0);
    }
}

int main(void) {
    test_ex1();
    test_ex2();
    test_ex3();
    test_ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
