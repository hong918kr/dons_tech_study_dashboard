/* RTOS-M06 추가 연습 — 한 번에 검증 (practice.c)
 *
 * solution.c 의 미니 커널(TODO 1~5 채운 상태)을 그대로 복사하고 아래만 더했다:
 *  연습 1  흔한 오답 두 가지를 g_variant 로 켠다: VAR_RESTART(재시도마다 타임아웃을 처음부터) /
 *          VAR_TRUST(깼으면 내 것이라고 믿고 확인 없이 꺼냄)
 *  연습 2  task_suspend / task_resume / task_delete — 기다리던 태스크를 두 리스트에서 모두 뺀다
 *  연습 3  q_peek (xQueuePeek) — 데이터를 남기므로 다음 받기 대기자를 깨워야 한다
 *  연습 4  task_abort_delay (xTaskAbortDelay) — FOREVER 대기도 즉시 EMPTY
 *  연습 5  기다리는 중 우선순위 변경 — FreeRTOS 처럼 대기 리스트를 다시 정렬하지 않으면?
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (주어진 코드) ======================= */
#define MAX_PRIO  8              /* configMAX_PRIORITIES */
#define MAX_TASKS 8
#define MAX_OPS   12
#define NQ        2
#define QMAX      8
#define TL        64             /* 타임라인은 앞 64 tick 만 */
#define FOREVER   UINT32_MAX     /* portMAX_DELAY */
#define AUTO      (-1)           /* SEND 값 자동: (태스크번호+1)*100 + 보낸 순번 */

typedef enum { TASK_DORMANT, TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DONE } task_state_t;
typedef enum { Q_OK, Q_FULL, Q_EMPTY, Q_PENDING } q_rc_t;   /* pdPASS / errQUEUE_FULL / errQUEUE_EMPTY / (블록 중) */

struct tcb; struct list;
typedef struct node {                 /* ListItem_t */
    struct node *next, *prev;
    uint32_t value;                   /* xItemValue: 지연 리스트 = 깰 tick, 대기 리스트 = MAX_PRIO - prio */
    struct list *container;           /* pxContainer: 지금 들어 있는 리스트 (NULL = 없음) */
    struct tcb *owner;                /* pvOwner */
} node_t;
typedef struct list { node_t end; unsigned n; } list_t;   /* 원형 + 끝 표지(sentinel) = xListEnd */

typedef struct { uint32_t entered; } timeout_t;           /* TimeOut_t (tick wrap 은 RTOS-B06/M07 몫이라 생략) */

typedef enum { OP_END, OP_WORK, OP_SEND, OP_RECV, OP_DELAY, OP_LOOP, OP_PEEK } opk_t;
typedef struct { opk_t k; int q; int32_t v; uint32_t tmo; } op_t;
#define WORK(n)        { OP_WORK, 0, (n), 0 }
#define SEND(q, v, t)  { OP_SEND, (q), (v), (t) }
#define RECV(q, t)     { OP_RECV, (q), 0, (t) }
#define DELAY(n)       { OP_DELAY, 0, (n), 0 }
#define LOOP_          { OP_LOOP, 0, 0, 0 }
#define END_           { OP_END, 0, 0, 0 }
#define PEEK(q, t)     { OP_PEEK, (q), 0, (t) }

typedef struct tcb {
    const char *name;
    uint8_t prio;                     /* 클수록 높다 (FreeRTOS 방향) */
    task_state_t state;
    uint32_t wake_tick;               /* BLOCKED 일 때 깰 tick (FOREVER = 시간으로는 안 깸) */
    void *wait_obj;                   /* 기다리는 큐 (NULL = 큐가 아님: DELAY 중) */
    node_t state_node, event_node;    /* xStateListItem, xEventListItem */
    /* xQueueReceive/Send 의 지역 변수 중 블록을 넘어 살아남아야 하는 것 */
    int in_api;                       /* xEntryTimeSet: 이미 한 번 블록했다 → 루프를 이어 가는 중 */
    int aborted;                      /* ucDelayAborted (연습 4) */
    timeout_t tmo;                    /* xTimeOut */
    uint32_t ticks_to_wait;           /* xTicksToWait (남은 대기 시간) */
    /* 스크립트 */
    op_t prog[MAX_OPS];
    int pc, left, iter, nsent_auto;
    uint32_t release;
    unsigned ready_seq, block_seq;
    int op_started; uint32_t op_start;
    /* 기록 */
    int nblock, nok, nfull, nempty, ngot;
    int32_t got[16];
    q_rc_t last_rc; uint32_t last_done, last_elapsed, max_elapsed;
    char tl[TL + 1];
} tcb_t;

typedef struct {
    int32_t buf[QMAX];
    unsigned len, rd, wr;
    int count;                        /* uxMessagesWaiting (int: 음수가 되면 불변식이 잡는다) */
    list_t tx_wait, rx_wait;          /* xTasksWaitingToSend, xTasksWaitingToReceive */
} kqueue_t;

static tcb_t    g_task[MAX_TASKS];
static int      g_nt;
static uint32_t g_now;
static tcb_t   *g_cur;                /* 지금(ISR 중이면 직전에) 돌던 태스크 = pxCurrentTCB */
static unsigned g_seq;
static list_t   g_delayed;            /* pxDelayedTaskList (정렬: 깰 tick) */
static kqueue_t g_q[NQ];
static void   (*g_isr_hook)(uint32_t now);
static int      g_yield_req;          /* event_list_wake_head 가 "더 높은 태스크 깨움" 을 돌려준 횟수 */
static char     g_wakes[256];         /* 깨어난 순서: 이름 (데이터/공간) 또는 이름(t) (타임아웃) */
static int      g_inv_bad, g_lost, g_drift, g_early, g_fifo_bad;   /* 하네스 검사 카운터 */
enum { VAR_FREERTOS, VAR_RESTART, VAR_TRUST };
static int      g_variant;            /* 연습 1 */
static bool     g_peek_wakes_next = true;   /* 연습 3 */
static int32_t  g_fifo[NQ][256]; static int g_fifo_w[NQ], g_fifo_r[NQ];   /* FIFO 오라클 */

static void list_init(list_t *l) {
    l->end.next = l->end.prev = &l->end;
    l->end.value = UINT32_MAX; l->end.container = NULL; l->end.owner = NULL;
    l->n = 0;
}
static void list_remove(node_t *x) {                          /* uxListRemove */
    if (!x->container) return;
    x->prev->next = x->next; x->next->prev = x->prev;
    x->container->n--;
    x->container = NULL; x->next = x->prev = NULL;
}
static tcb_t *list_head_owner(const list_t *l) { return l->n ? l->end.next->owner : NULL; }

static void make_ready(tcb_t *t, bool by_timeout) {           /* prvAddTaskToReadyList + 기록 */
    t->state = TASK_READY; t->wait_obj = NULL; t->ready_seq = ++g_seq;
    size_t n = strlen(g_wakes);
    snprintf(g_wakes + n, sizeof g_wakes - n, "%s%s%s", n ? " " : "", t->name, by_timeout ? "(t)" : "");
}

/* ---------------- TODO 1: 정렬 삽입 (vListInsert) ---------------- */
/* 값 오름차순. 같은 값이 이미 있으면 그 "뒤" 에 넣는다 → 같은 prio 끼리는 먼저 온 순(FIFO).
 * 지연 리스트(값 = 깰 tick)와 큐 대기 리스트(값 = MAX_PRIO - prio) 둘 다 이 함수를 쓴다. */
static void list_insert_sorted(list_t *l, node_t *x) {
    node_t *it = &l->end;
    while (it->next != &l->end && it->next->value <= x->value) it = it->next;   /* list.c:192 의 <= */
    x->next = it->next; x->prev = it;
    it->next->prev = x; it->next = x;
    x->container = l; l->n++;
}

/* 주어진 코드: 지연 리스트에 넣기 = prvAddCurrentTaskToDelayedList (FOREVER 면 시간으로는 안 깬다) */
static void delay_insert(tcb_t *t, uint32_t ticks) {
    if (t->state_node.container) { g_inv_bad++; list_remove(&t->state_node); }   /* 방어: 유령 노드 (리스트가 꼬이지 않게) */
    if (ticks == FOREVER) { t->wake_tick = FOREVER; return; }   /* FreeRTOS: xSuspendedTaskList (tasks.c:8717-8722) */
    t->wake_tick = g_now + ticks;
    t->state_node.value = t->wake_tick;
    list_insert_sorted(&g_delayed, &t->state_node);
}
/* 주어진 코드: 대기 리스트 + 지연 리스트에 동시에 = vTaskPlaceOnEventList (tasks.c:5402) */
static void block_on(list_t *wait, uint32_t ticks, void *obj) {
    tcb_t *t = g_cur;
    t->state = TASK_BLOCKED; t->wait_obj = obj; t->block_seq = ++g_seq; t->nblock++;
    if (t->event_node.container) { g_inv_bad++; list_remove(&t->event_node); }   /* 방어: 유령 노드 */
    t->event_node.value = (uint32_t)(MAX_PRIO - t->prio);    /* tasks.c:1929 */
    list_insert_sorted(wait, &t->event_node);
    delay_insert(t, ticks);
    /* 하네스 검사: 재블록해도 마감 시각(첫 호출 tick + 원래 타임아웃)은 움직이면 안 된다 */
    uint32_t tmo = t->prog[t->pc].tmo;
    if (tmo != FOREVER && t->wake_tick != t->op_start + tmo) g_drift++;
}
static void task_delay(tcb_t *t, uint32_t n) {               /* vTaskDelay: 지연 리스트에만 */
    t->state = TASK_BLOCKED; t->wait_obj = NULL; t->block_seq = ++g_seq;
    delay_insert(t, n ? n : 1);
}

/* ---------------- TODO 2: 대기 리스트 맨 앞 깨우기 (xTaskRemoveFromEventList) ---------------- */
/* wait 는 비어 있지 않다(호출자가 확인). 맨 앞 = 최고 prio. 대기 리스트에서 빼고, 지연 리스트에도 있으면
 * 거기서도 뺀 뒤 READY 로. 반환: 깨운 태스크가 지금 태스크(g_cur)보다 "엄격히" 높으면 true (= yield 필요). */
static bool event_list_wake_head(list_t *wait) {
    tcb_t *t = list_head_owner(wait);
    list_remove(&t->event_node);                               /* tasks.c:5525 */
    if (t->state_node.container) list_remove(&t->state_node);  /* tasks.c:5529 — 안 빼면 나중에 유령 타임아웃 */
    make_ready(t, false);
    return g_cur != NULL && t->prio > g_cur->prio;             /* tasks.c:5555 */
}

/* ---------------- TODO 3: tick 만료 처리 (xTaskIncrementTick 의 지연 리스트 부분) ---------------- */
/* 지연 리스트 맨 앞부터 wake_tick <= g_now 인 태스크를 모두 꺼낸다. 큐를 기다리던 태스크면
 * 대기 리스트에서도 빼야 한다 (두 리스트 모두). 그리고 READY (타임아웃으로 깸 = make_ready(t, true)). */
static void tick_expire(void) {
    while (g_delayed.n > 0) {
        node_t *h = g_delayed.end.next;
        if (h->value > g_now) break;                           /* 정렬돼 있으니 여기서 멈춘다 (tasks.c:4897) */
        tcb_t *t = h->owner;
        list_remove(&t->state_node);                           /* tasks.c:4913 */
        if (t->event_node.container) list_remove(&t->event_node);   /* tasks.c:4917-4919 */
        make_ready(t, t->wait_obj != NULL);
    }
}

/* 주어진 코드: 타임아웃 (vTaskInternalSetTimeOutState / xTaskCheckForTimeOut, tasks.c:5681, 5693) */
static void timeout_set(timeout_t *to) { to->entered = g_now; }
static bool timeout_check(timeout_t *to, uint32_t *ticks) {  /* true = 시간 다 됨 */
    if (g_cur && g_cur->aborted) { g_cur->aborted = 0; return true; }   /* 연습 4: tasks.c:5710-5716 (FOREVER 검사보다 먼저) */
    if (*ticks == FOREVER) return false;
    uint32_t elapsed = g_now - to->entered;
    if (elapsed < *ticks) { *ticks -= elapsed; timeout_set(to); return false; }   /* tasks.c:5741-5746 */
    *ticks = 0;
    return true;
}
static void q_init(kqueue_t *q, unsigned len) {
    memset(q, 0, sizeof *q); q->len = len;
    list_init(&q->tx_wait); list_init(&q->rx_wait);
}
static void q_push(kqueue_t *q, int32_t v) { q->buf[q->wr] = v; q->wr = (q->wr + 1) % q->len; q->count++; }
static int32_t q_pop(kqueue_t *q) { int32_t v = q->buf[q->rd]; q->rd = (q->rd + 1) % q->len; q->count--; return v; }

/* ---------------- TODO 4: 받기 (xQueueReceive, queue.c:1509-1656) ---------------- */
/* 지금 태스크 self = g_cur. 반환 Q_OK(*out 채움) / Q_EMPTY / Q_PENDING(블록했다: 깨면 다시 불린다).
 * 구조 (FreeRTOS 루프 그대로):
 *   첫 진입이면(in_api == 0) ticks_to_wait = timeout.
 *   for (;;) {
 *     데이터 있음 → 꺼내고, 보내기 대기자가 있으면 맨 앞을 깨우고(true 면 g_yield_req++), in_api = 0, Q_OK
 *     ticks_to_wait == 0 → in_api = 0, Q_EMPTY
 *     아직 시각 기록 안 했으면 timeout_set + in_api = 1
 *     timeout_check 가 false(아직 남음) → 여전히 비었으면 block_on(rx_wait, 남은 ticks, q) 하고 Q_PENDING
 *                                        (그새 데이터가 생겼으면 루프 처음으로)
 *     true(시간 다 됨) → 여전히 비었으면 in_api = 0, Q_EMPTY (데이터가 있으면 루프 처음으로 → 가져간다)
 *   } */
static q_rc_t q_receive(kqueue_t *q, int32_t *out, uint32_t timeout) {
    tcb_t *self = g_cur;
    if (g_variant == VAR_TRUST && self->in_api) {             /* 연습 1(b) 오답: 깼으니 내 것 */
        *out = q_pop(q); self->in_api = 0; return Q_OK;
    }
    if (!self->in_api) self->ticks_to_wait = timeout;
    for (;;) {
        if (q->count > 0) {                                    /* queue.c:1541 */
            *out = q_pop(q);
            if (q->tx_wait.n > 0 && event_list_wake_head(&q->tx_wait)) g_yield_req++;   /* queue.c:1551-1555 */
            self->in_api = 0;
            return Q_OK;
        }
        if (self->ticks_to_wait == 0) { self->in_api = 0; return Q_EMPTY; }           /* queue.c:1575-1584 */
        if (!self->in_api) { timeout_set(&self->tmo); self->in_api = 1; }              /* queue.c:1586-1591 */
        else if (g_variant == VAR_RESTART) { timeout_set(&self->tmo); self->ticks_to_wait = timeout; }   /* 연습 1(a) 오답 */
        if (!timeout_check(&self->tmo, &self->ticks_to_wait)) {                        /* queue.c:1609 */
            if (q->count == 0) {
                block_on(&q->rx_wait, self->ticks_to_wait, q);                         /* queue.c:1616 */
                return Q_PENDING;
            }
            /* 그새 데이터가 들어왔다 → 루프 처음으로 (queue.c:1628-1634) */
        } else if (q->count == 0) {                                                    /* queue.c:1636-1649 */
            self->in_api = 0;
            return Q_EMPTY;
        }
    }
}

/* ---------------- TODO 5: 보내기 (xQueueGenericSend, queue.c:949-1164) ---------------- */
/* q_receive 의 거울: 자리 있음 → 넣고 받기 대기자 맨 앞을 깨움 / 꽉 참 → ticks_to_wait 0 이면 Q_FULL,
 * 아니면 timeout_check → 남았으면 block_on(tx_wait, ...) Q_PENDING, 다 됐으면 Q_FULL. */
static q_rc_t q_send(kqueue_t *q, int32_t v, uint32_t timeout) {
    tcb_t *self = g_cur;
    if (!self->in_api) self->ticks_to_wait = timeout;
    for (;;) {
        if (q->count < (int)q->len) {
            q_push(q, v);
            if (q->rx_wait.n > 0 && event_list_wake_head(&q->rx_wait)) g_yield_req++;   /* queue.c:1012-1020 */
            self->in_api = 0;
            return Q_OK;
        }
        if (self->ticks_to_wait == 0) { self->in_api = 0; return Q_FULL; }            /* queue.c:1085-1096 */
        if (!self->in_api) { timeout_set(&self->tmo); self->in_api = 1; }
        if (!timeout_check(&self->tmo, &self->ticks_to_wait)) {                        /* queue.c:1121 */
            if (q->count == (int)q->len) {
                block_on(&q->tx_wait, self->ticks_to_wait, q);                         /* queue.c:1126 */
                return Q_PENDING;
            }
        } else {
            self->in_api = 0;                                                          /* queue.c:1152-1161 */
            return Q_FULL;
        }
    }
}

/* 주어진 코드: ISR 에서 보내기 (xQueueGenericSendFromISR 의 단순판, 자세한 건 RTOS-M02) — 절대 블록 안 함 */
static q_rc_t q_send_from_isr(kqueue_t *q, int32_t v, bool *higher_woken) {
    if (q->count >= (int)q->len) return Q_FULL;
    q_push(q, v);
    if (q->rx_wait.n > 0 && event_list_wake_head(&q->rx_wait)) *higher_woken = true;
    return Q_OK;
}

/* 연습 3: 엿보기 (xQueuePeek, queue.c:1884-) — 꺼내지 않는다. 데이터가 남으니 다른 받기 대기자를 깨운다 (queue.c:1927-1935) */
static q_rc_t q_peek(kqueue_t *q, int32_t *out, uint32_t timeout) {
    tcb_t *self = g_cur;
    if (!self->in_api) self->ticks_to_wait = timeout;
    for (;;) {
        if (q->count > 0) {
            *out = q->buf[q->rd];
            if (g_peek_wakes_next && q->rx_wait.n > 0 && event_list_wake_head(&q->rx_wait)) g_yield_req++;
            self->in_api = 0;
            return Q_OK;
        }
        if (self->ticks_to_wait == 0) { self->in_api = 0; return Q_EMPTY; }
        if (!self->in_api) { timeout_set(&self->tmo); self->in_api = 1; }
        if (!timeout_check(&self->tmo, &self->ticks_to_wait)) {
            if (q->count == 0) { block_on(&q->rx_wait, self->ticks_to_wait, q); return Q_PENDING; }
        } else if (q->count == 0) { self->in_api = 0; return Q_EMPTY; }
    }
}

/* 연습 2: 정지 / 재개 / 삭제 (vTaskSuspend tasks.c:3197, vTaskResume 3430, vTaskDelete 2212) */
static void task_suspend(tcb_t *t) {
    list_remove(&t->state_node);                               /* tasks.c:3214 지연 리스트에서 */
    list_remove(&t->event_node);                               /* tasks.c:3224-3226 대기 리스트에서도 */
    t->state = TASK_SUSPENDED;
}
static void task_resume(tcb_t *t) { if (t->state == TASK_SUSPENDED) make_ready(t, false); }
static void task_delete(tcb_t *t) {
    list_remove(&t->state_node);                               /* tasks.c:2228 */
    list_remove(&t->event_node);                               /* tasks.c:2238-2240 */
    t->state = TASK_DONE; t->in_api = 0;
}
/* 연습 4: xTaskAbortDelay (tasks.c:4737) — 두 리스트에서 빼고, 이벤트 대기였으면 aborted 표시 */
static bool task_abort_delay(tcb_t *t) {
    if (t->state != TASK_BLOCKED) return false;
    list_remove(&t->state_node);
    if (t->event_node.container) { list_remove(&t->event_node); t->aborted = 1; }   /* tasks.c:4765-4772 */
    make_ready(t, false);
    return true;
}
/* 연습 5: vTaskPrioritySet 처럼 — 이벤트 리스트 값만 바꾸고 다시 정렬하지 않는다 (tasks.c:2972-2986) */
static void task_prio_set(tcb_t *t, int prio, bool resort) {
    t->prio = (uint8_t)prio;
    t->event_node.value = (uint32_t)(MAX_PRIO - prio);
    if (resort && t->event_node.container) {                  /* 다시 정렬하는 커널(대안) */
        list_t *l = t->event_node.container;
        list_remove(&t->event_node);
        list_insert_sorted(l, &t->event_node);
    }
}

/* ---------------- 주어진 코드: 스크립트 실행기 + 스케줄러 ---------------- */
static void k_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_nt = 0; g_now = 0; g_cur = NULL; g_seq = 0; g_isr_hook = NULL; g_yield_req = 0;
    g_wakes[0] = '\0';
    list_init(&g_delayed);
    for (int i = 0; i < NQ; i++) { q_init(&g_q[i], 1); g_fifo_w[i] = g_fifo_r[i] = 0; }
}
static tcb_t *k_task(const char *name, int prio, uint32_t release, const op_t *prog) {
    tcb_t *t = &g_task[g_nt++];
    t->name = name; t->prio = (uint8_t)prio; t->release = release; t->state = TASK_DORMANT;
    for (int i = 0; i < MAX_OPS - 1; i++) {                     /* END 또는 LOOP 까지 복사 (나머지는 0 = END) */
        t->prog[i] = prog[i];
        if (prog[i].k == OP_END || prog[i].k == OP_LOOP) break;
    }
    t->state_node.owner = t->event_node.owner = t;
    t->wake_tick = FOREVER; t->last_rc = Q_PENDING;
    return t;
}
static tcb_t *sched_pick_next(void) {
    tcb_t *best = NULL;
    for (int i = 0; i < g_nt; i++) {
        tcb_t *t = &g_task[i];
        if (t->state != TASK_READY) continue;
        if (!best || t->prio > best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq)) best = t;
    }
    return best;
}
/* 0-tick op(DELAY/LOOP/END) 실행. true = tick 을 쓰는 op 에 도착, false = 막힘/끝 */
static bool zero_ops(tcb_t *t) {
    for (int guard = 0; guard < 2 * MAX_OPS; guard++) {
        op_t *op = &t->prog[t->pc];
        switch (op->k) {
        case OP_END:   t->state = TASK_DONE; return false;
        case OP_LOOP:  t->pc = 0; t->iter++; break;
        case OP_DELAY: t->pc++; task_delay(t, (uint32_t)op->v); return false;
        default:       return true;
        }
    }
    t->state = TASK_DONE;
    return false;
}
static void log_result(tcb_t *t, const op_t *op, q_rc_t rc, int32_t v) {
    uint32_t el = g_now - t->op_start;
    t->last_rc = rc; t->last_done = g_now; t->last_elapsed = el;
    if (el > t->max_elapsed) t->max_elapsed = el;
    if (rc == Q_OK) t->nok++;
    if (rc == Q_FULL) t->nfull++;
    if (rc == Q_EMPTY) t->nempty++;
    if (rc != Q_OK && op->tmo != FOREVER && el < op->tmo) g_early++;   /* 타임아웃보다 일찍 실패하면 안 된다 */
    if (rc == Q_OK && op->k == OP_SEND && g_fifo_w[op->q] < 256) g_fifo[op->q][g_fifo_w[op->q]++] = v;
    if (rc == Q_OK && op->k == OP_RECV) {
        if (t->ngot < 16) t->got[t->ngot++] = v;
        if (g_fifo_r[op->q] >= g_fifo_w[op->q] || g_fifo[op->q][g_fifo_r[op->q]] != v) g_fifo_bad++;
        g_fifo_r[op->q]++;
    }
}
static void step(tcb_t *t) {
    op_t *op = &t->prog[t->pc];
    if (!t->op_started) { t->op_started = 1; t->op_start = g_now; }
    q_rc_t rc = Q_OK; int32_t v = 0;
    switch (op->k) {
    case OP_WORK:
        if (t->left == 0) t->left = op->v;
        if (--t->left > 0) return;
        break;
    case OP_SEND:
        v = op->v == AUTO ? (int32_t)((t - g_task + 1) * 100 + t->nsent_auto) : op->v;
        rc = q_send(&g_q[op->q], v, op->tmo);
        if (rc == Q_OK && op->v == AUTO) t->nsent_auto++;
        break;
    case OP_RECV:
        rc = q_receive(&g_q[op->q], &v, op->tmo);
        break;
    case OP_PEEK:
        rc = q_peek(&g_q[op->q], &v, op->tmo);
        if (rc == Q_OK && t->ngot < 16) t->got[t->ngot++] = v;
        break;
    default: break;
    }
    if (rc == Q_PENDING) return;                                 /* 아직 API 안: 깨면 같은 op 를 다시 */
    if (op->k == OP_SEND || op->k == OP_RECV || op->k == OP_PEEK) log_result(t, op, rc, v);
    t->op_started = 0; t->pc++;
    (void)zero_ops(t);                                           /* 뒤따르는 DELAY/LOOP/END */
}
static int pending_api(const kqueue_t *q, opk_t k) {             /* 깼지만 아직 안 돈 대기자 수 */
    int n = 0;
    for (int i = 0; i < g_nt; i++) {
        const tcb_t *t = &g_task[i];
        if (t->state == TASK_READY && t->in_api && &g_q[t->prog[t->pc].q] == q &&
            (t->prog[t->pc].k == k || (k == OP_RECV && t->prog[t->pc].k == OP_PEEK))) n++;
    }
    return n;
}
static void check_list(const list_t *l, const void *obj, bool is_event) {
    const node_t *p = &l->end; unsigned n = 0;
    for (const node_t *x = l->end.next; x != &l->end; x = x->next) {
        if (++n > MAX_TASKS || x->prev != p || x->container != l || !x->owner) { g_inv_bad++; return; }
        const tcb_t *t = x->owner;
        if (t->state != TASK_BLOCKED) g_inv_bad++;                 /* 대기/지연 리스트에 BLOCKED 아닌 태스크 */
        if (is_event && t->wait_obj != obj) g_inv_bad++;
        if (!is_event && x->value <= g_now) g_inv_bad++;           /* 만료됐어야 하는데 남아 있음 */
        if (p != &l->end && (p->value > x->value ||
            (p->value == x->value && p->owner->block_seq > t->block_seq))) g_inv_bad++;   /* 정렬 · FIFO */
        p = x;
    }
    if (n != l->n || l->end.prev != p) g_inv_bad++;
}
static void check_invariants(void) {
    for (int i = 0; i < NQ; i++) {
        kqueue_t *q = &g_q[i];
        if (q->count < 0 || q->count > (int)q->len) g_inv_bad++;
        check_list(&q->tx_wait, q, true);
        check_list(&q->rx_wait, q, true);
        /* 잃어버린 깨움: 대기자가 있는데 데이터(자리)가 "깨워 둔 태스크 수" 보다 많으면 누군가 영원히 잔다 */
        if (q->rx_wait.n > 0 && q->count > pending_api(q, OP_RECV)) g_lost++;
        if (q->tx_wait.n > 0 && (int)q->len - q->count > pending_api(q, OP_SEND)) g_lost++;
    }
    check_list(&g_delayed, NULL, false);
    for (int i = 0; i < g_nt; i++) {
        const tcb_t *t = &g_task[i];
        bool ev = t->event_node.container != NULL, de = t->state_node.container != NULL;
        if (t->state != TASK_BLOCKED && (ev || de)) g_inv_bad++;
        if (t->state == TASK_BLOCKED) {
            if (!ev && !de) g_inv_bad++;                           /* 아무도 깨울 수 없는 태스크 */
            if (t->wait_obj && !ev) g_inv_bad++;
            if (t->wake_tick != FOREVER && !de) g_inv_bad++;
        }
    }
}
static bool g_check_on = true;
static void kernel_tick(void) {
    tick_expire();                                               /* ① tick ISR */
    for (int i = 0; i < g_nt; i++)                               /* ② 출발 */
        if (g_task[i].state == TASK_DORMANT && g_task[i].release == g_now) {
            g_task[i].state = TASK_READY; g_task[i].ready_seq = ++g_seq;
        }
    if (g_isr_hook) g_isr_hook(g_now);                           /* ③ 장치 ISR */
    tcb_t *run = NULL;
    for (int guard = 0; guard < 2 * MAX_TASKS; guard++) {        /* ④ 한 op */
        tcb_t *c = sched_pick_next();
        if (!c) break;
        g_cur = c;
        if (!zero_ops(c)) continue;                              /* 막히거나 끝남 → 다시 고른다 (tick 안 씀) */
        c->state = TASK_RUNNING;
        step(c);
        if (c->state == TASK_RUNNING) c->state = TASK_READY;
        run = c;
        break;
    }
    for (int i = 0; i < g_nt; i++) {                             /* ⑤ 기록 */
        tcb_t *t = &g_task[i];
        char ch = t == run ? 'R' : t->state == TASK_READY ? 'r'
                : t->state == TASK_BLOCKED ? (t->wait_obj ? 'B' : 'd')
                : t->state == TASK_SUSPENDED ? 'S' : '.';
        if (g_now < TL) t->tl[g_now] = ch;
    }
    if (g_check_on) check_invariants();
    g_now++;
}
static void k_run(uint32_t ticks) { while (g_now < ticks) kernel_tick(); }
static void print_tl(const char *title) {
    uint32_t to = g_now < TL ? g_now : TL;
    printf("%s\n tick :", title);
    for (uint32_t x = 0; x < to; x++) printf(" %u", x % 10);
    printf("\n");
    for (int i = 0; i < g_nt; i++) {
        printf(" %-5.5s:", g_task[i].name);
        for (uint32_t x = 0; x < to; x++) printf(" %c", g_task[i].tl[x] ? g_task[i].tl[x] : '.');
        printf("\n");
    }
    printf(" wakes: %s\n", g_wakes);
}
static const char *rc_name(q_rc_t rc) {
    return rc == Q_OK ? "OK" : rc == Q_FULL ? "FULL" : rc == Q_EMPTY ? "EMPTY" : "PENDING";
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int clean(void) { return g_inv_bad == 0 && g_lost == 0 && g_drift == 0 && g_early == 0 && g_fifo_bad == 0; }
static void zero_counters(void) { g_inv_bad = g_lost = g_drift = g_early = g_fifo_bad = 0; }
static void fresh(unsigned qlen) { k_reset(); q_init(&g_q[0], qlen); zero_counters(); g_check_on = true; }

/* 연습 1 시나리오 = 본문 B1: L(p1) RECV tmo 10, S(p2) t2 SEND, H(p3) t3 RECV tmo 0 */
static void b1(int variant) {
    fresh(1); g_variant = variant;
    k_task("L", 1, 0, (op_t[]){ RECV(0, 10), END_ });
    k_task("S", 2, 2, (op_t[]){ SEND(0, 7, 0), END_ });
    k_task("H", 3, 3, (op_t[]){ RECV(0, 0), END_ });
    k_run(30);
    g_variant = VAR_FREERTOS;
}

/* 연습 2: 훅으로 "다른 태스크가 부른 것처럼" suspend/resume/delete/send 를 주입 */
static uint32_t g_at_susp, g_at_res, g_at_send, g_at_del;
static void ex2_hook(uint32_t now) {
    bool w = false;
    if (now == g_at_susp) task_suspend(&g_task[0]);
    if (now == g_at_send && q_send_from_isr(&g_q[0], 5, &w) == Q_OK) g_fifo[0][g_fifo_w[0]++] = 5;
    if (now == g_at_res) task_resume(&g_task[0]);
    if (now == g_at_del) task_delete(&g_task[0]);
}
static tcb_t *ex2(uint32_t susp, uint32_t send, uint32_t res, uint32_t del) {
    fresh(1);
    tcb_t *r = k_task("R", 2, 0, (op_t[]){ RECV(0, 10), END_ });
    g_at_susp = susp; g_at_send = send; g_at_res = res; g_at_del = del; g_isr_hook = ex2_hook;
    k_run(16);
    return r;
}

/* 연습 4 */
static uint32_t g_at_abort;
static void ex4_hook(uint32_t now) { if (now == g_at_abort) (void)task_abort_delay(&g_task[0]); }

/* 연습 5 */
static bool g_resort;
static void ex5_hook(uint32_t now) {
    bool w = false;
    if (now == 3) task_prio_set(&g_task[0], 5, g_resort);    /* R1: 1 → 5 */
    if (now == 4 && q_send_from_isr(&g_q[0], 1, &w) == Q_OK) g_fifo[0][g_fifo_w[0]++] = 1;
}

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

    /* ---- 연습 1(a): 재시도마다 타임아웃을 처음부터 → 마감이 밀린다 ---- */
    b1(VAR_RESTART);
    if (verbose) print_tl("ex1a restart timeout on every retry");
    printf("ex1a VAR_RESTART: after 30 ticks L rc=%s, blocked %d times, wake_tick now %u, deadline-moved=%d\n",
           rc_name(g_task[0].last_rc), g_task[0].nblock, g_task[0].wake_tick, g_drift);
    CHECK(g_task[0].last_rc == Q_PENDING && g_task[0].nblock == 4 && g_task[0].wake_tick == 34 && g_drift == 3);
    b1(VAR_FREERTOS);
    CHECK(g_task[0].last_rc == Q_EMPTY && g_task[0].last_done == 10 && clean());

    /* ---- 연습 1(b): 깼으면 내 것 → 빈 큐에서 꺼낸다 (중복 값, count -1) ---- */
    b1(VAR_TRUST);
    printf("ex1b VAR_TRUST: H got %d, L rc=%s got %d at t%u, count=%d, invariant=%d fifo=%d\n",
           (int)g_task[2].got[0], rc_name(g_task[0].last_rc), (int)g_task[0].got[0], g_task[0].last_done,
           g_q[0].count, g_inv_bad, g_fifo_bad);
    CHECK(g_task[2].got[0] == 7 && g_task[0].last_rc == Q_OK && g_task[0].got[0] == 7);   /* 같은 값을 두 번 받음 */
    CHECK(g_q[0].count == -1 && g_inv_bad > 0 && g_fifo_bad == 1);

    /* ---- 연습 2: 기다리는 중 정지·재개·삭제 ---- */
    tcb_t *r = ex2(3, 4, 6, 99);                      /* t3 정지, t4 데이터 도착(대기자 없음), t6 재개 */
    if (verbose) print_tl("ex2 suspend t3, data t4, resume t6");
    CHECK(r->last_rc == Q_OK && r->got[0] == 5 && r->last_done == 6 && strcmp(g_wakes, "R") == 0 && clean());
    r = ex2(3, 99, 6, 99);                            /* 데이터 없이 t6 재개 → 남은 4 tick 으로 재블록 → t10 EMPTY */
    CHECK(r->last_rc == Q_EMPTY && r->last_done == 10 && r->nblock == 2 && clean());
    r = ex2(3, 99, 12, 99);                           /* 마감(t10) 지나 재개 → 바로 EMPTY */
    CHECK(r->last_rc == Q_EMPTY && r->last_done == 12 && r->nblock == 1 && clean());
    r = ex2(99, 6, 99, 3);                            /* t3 삭제 → t6 보내기는 아무도 안 깨운다, 아이템은 남음 */
    CHECK(r->state == TASK_DONE && g_q[0].count == 1 && g_q[0].rx_wait.n == 0 && g_delayed.n == 0 && clean());

    /* ---- 연습 3: peek 는 다음 대기자를 깨워야 한다 ---- */
    for (int wake_next = 1; wake_next >= 0; wake_next--) {
        fresh(1); g_peek_wakes_next = wake_next;
        tcb_t *pk = k_task("P", 3, 0, (op_t[]){ PEEK(0, FOREVER), END_ });
        tcb_t *rv = k_task("R", 2, 0, (op_t[]){ RECV(0, FOREVER), END_ });
        k_task("S", 1, 0, (op_t[]){ SEND(0, 77, 0), END_ });
        k_run(8);
        if (verbose) print_tl(wake_next ? "ex3 peek wakes next receiver" : "ex3 peek without extra wake");
        printf("ex3 peek_wakes_next=%d: P got %d, R rc=%s, count=%d, lost-wakeup ticks=%d\n", wake_next,
               (int)pk->got[0], rc_name(rv->last_rc), g_q[0].count, g_lost);
        if (wake_next) CHECK(pk->got[0] == 77 && rv->last_rc == Q_OK && rv->got[0] == 77 && g_q[0].count == 0 && clean());
        else           CHECK(pk->got[0] == 77 && rv->last_rc == Q_PENDING && g_q[0].count == 1 && g_lost > 0);
    }
    g_peek_wakes_next = true;

    /* ---- 연습 4: xTaskAbortDelay — FOREVER 대기도 즉시 EMPTY ---- */
    fresh(1);
    r = k_task("R", 2, 0, (op_t[]){ RECV(0, FOREVER), END_ });
    g_at_abort = 3; g_isr_hook = ex4_hook;
    k_run(6);
    CHECK(r->last_rc == Q_EMPTY && r->last_done == 3 && r->last_elapsed == 3 && r->aborted == 0 && clean());
    fresh(1);                                          /* 큐가 아니라 DELAY 중이면 aborted 표시 없이 그냥 깨움 */
    r = k_task("R", 2, 0, (op_t[]){ DELAY(10), RECV(0, 0), END_ });
    g_at_abort = 2; g_isr_hook = ex4_hook;
    k_run(6);
    CHECK(r->last_rc == Q_EMPTY && r->last_done == 2 && r->aborted == 0 && clean());

    /* ---- 연습 5: 기다리는 중 우선순위 변경 ---- */
    for (int resort = 0; resort <= 1; resort++) {
        fresh(1); g_check_on = false;                 /* 정렬 안 하면 불변식(정렬)이 깨지는 게 정상이라 끔 */
        k_task("R1", 1, 0, (op_t[]){ RECV(0, FOREVER), END_ });
        k_task("R2", 3, 1, (op_t[]){ RECV(0, FOREVER), END_ });
        g_resort = resort; g_isr_hook = ex5_hook;
        k_run(8);
        printf("ex5 resort=%d: first woken = %.2s\n", resort, g_wakes);
        CHECK(strncmp(g_wakes, resort ? "R1" : "R2", 2) == 0);
    }
    g_check_on = true;

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
