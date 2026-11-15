/* RTOS-M06 블로킹 큐 구현: 대기 리스트와 타임아웃
 *
 * 호스트 모델: OS 스레드 1개로 도는 "tick 단위 결정적 미니 커널" (RTOS-B09/B11 과 같은 틀).
 *  - 태스크 = 작은 스크립트: SEND(q,v,tmo) / RECV(q,tmo) / WORK(n) 은 tick 을 쓴다 (시도 1번 = 1 tick).
 *    DELAY(n) / LOOP / END 는 0 tick — 앞 op 가 끝나는 순간(또는 태스크가 뽑히는 순간) 바로 실행된다.
 *  - 한 tick 의 순서: ① tick ISR (지연 리스트 만료 → 타임아웃 해제)  ② 새 태스크 출발  ③ 장치 ISR 훅
 *    ④ READY 중 prio 최고(클수록 높음, 같으면 먼저 READY 된 쪽) 태스크가 op 하나 실행  ⑤ 타임라인·불변식 기록.
 *    API 가 더 높은 태스크를 깨우면 그 전환은 다음 tick 에 반영된다 (1 tick 해상도).
 *  - TCB 는 FreeRTOS 처럼 리스트 노드가 2개다: state_node (지연 리스트 = xStateListItem),
 *    event_node (큐의 대기 리스트 = xEventListItem). 대기 리스트 값 = MAX_PRIO - prio → 오름차순 정렬이
 *    곧 "prio 높은 순, 같으면 먼저 온 순".
 *  - 큐 깨우기는 FreeRTOS 방식: 소유권을 넘겨주지(handoff) 않는다. 깨어난 태스크는 API 의 for(;;) 루프로
 *    돌아가 큐를 다시 보고, 그사이 누가 가져갔으면 "남은" 타임아웃으로 다시 잔다. 모델에서는 블록된 API 가
 *    Q_PENDING 을 돌려주고, 태스크가 다시 뽑히면 같은 API 를 다시 불러 루프를 이어 간다 (in_api = 1).
 *
 * 채울 것 (TODO 1~5):
 *  1. list_insert_sorted()  : vListInsert — 값 오름차순, 같은 값이면 "뒤에" (FIFO)
 *  2. event_list_wake_head(): xTaskRemoveFromEventList — 대기 리스트 맨 앞을 꺼내 지연 리스트에서도 빼고 READY
 *  3. tick_expire()         : xTaskIncrementTick 의 만료 처리 — 지연 리스트에서 빼고, 대기 리스트에도 있으면 거기서도 뺀다
 *  4. q_receive()           : xQueueReceive 의 for(;;) 재시도 루프 (남은 타임아웃으로 재블록)
 *  5. q_send()              : xQueueGenericSend 의 같은 루프
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 타임라인 더 보기)
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

typedef enum { OP_END, OP_WORK, OP_SEND, OP_RECV, OP_DELAY, OP_LOOP } opk_t;
typedef struct { opk_t k; int q; int32_t v; uint32_t tmo; } op_t;
#define WORK(n)        { OP_WORK, 0, (n), 0 }
#define SEND(q, v, t)  { OP_SEND, (q), (v), (t) }
#define RECV(q, t)     { OP_RECV, (q), 0, (t) }
#define DELAY(n)       { OP_DELAY, 0, (n), 0 }
#define LOOP_          { OP_LOOP, 0, 0, 0 }
#define END_           { OP_END, 0, 0, 0 }

typedef struct tcb {
    const char *name;
    uint8_t prio;                     /* 클수록 높다 (FreeRTOS 방향) */
    task_state_t state;
    uint32_t wake_tick;               /* BLOCKED 일 때 깰 tick (FOREVER = 시간으로는 안 깸) */
    void *wait_obj;                   /* 기다리는 큐 (NULL = 큐가 아님: DELAY 중) */
    node_t state_node, event_node;    /* xStateListItem, xEventListItem */
    /* xQueueReceive/Send 의 지역 변수 중 블록을 넘어 살아남아야 하는 것 */
    int in_api;                       /* xEntryTimeSet: 이미 한 번 블록했다 → 루프를 이어 가는 중 */
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
    default: break;
    }
    if (rc == Q_PENDING) return;                                 /* 아직 API 안: 깨면 같은 op 를 다시 */
    if (op->k == OP_SEND || op->k == OP_RECV) log_result(t, op, rc, v);
    t->op_started = 0; t->pc++;
    (void)zero_ops(t);                                           /* 뒤따르는 DELAY/LOOP/END */
}
static int pending_api(const kqueue_t *q, opk_t k) {             /* 깼지만 아직 안 돈 대기자 수 */
    int n = 0;
    for (int i = 0; i < g_nt; i++) {
        const tcb_t *t = &g_task[i];
        if (t->state == TASK_READY && t->in_api && t->prog[t->pc].k == k && &g_q[t->prog[t->pc].q] == q) n++;
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
static bool row_is(const tcb_t *t, const char *s) { return strncmp(t->tl, s, strlen(s)) == 0; }
static int clean(void) { return g_inv_bad == 0 && g_lost == 0 && g_drift == 0 && g_early == 0 && g_fifo_bad == 0; }
static void zero_counters(void) { g_inv_bad = g_lost = g_drift = g_early = g_fifo_bad = 0; }

static int32_t g_isr_val; static uint32_t g_isr_at[2];
static void isr_send_at(uint32_t now) {
    for (int i = 0; i < 2; i++)
        if (now == g_isr_at[i]) {
            bool w = false;
            if (q_send_from_isr(&g_q[0], g_isr_val + i, &w) == Q_OK) g_fifo[0][g_fifo_w[0]++] = g_isr_val + i;
        }
}

static uint32_t g_rng = 0x2026111Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rnd(int lo, int hi) { return lo + (int)(xorshift32() % (uint32_t)(hi - lo + 1)); }
static const char *const g_names[MAX_TASKS] = { "a", "b", "c", "d", "e", "f", "g", "h" };
static void random_set(void) {
    static const uint32_t tmos[] = { 0, 1, 2, 3, 5, FOREVER };
    k_reset();
    q_init(&g_q[0], (unsigned)rnd(1, 3)); q_init(&g_q[1], (unsigned)rnd(1, 2));
    int nt = rnd(4, 7);
    for (int i = 0; i < nt; i++) {
        op_t p[MAX_OPS]; int n = 0, nops = rnd(3, 6);
        for (int j = 0; j < nops; j++) {
            int kind = rnd(0, 9), q = rnd(0, 1);
            uint32_t tmo = tmos[rnd(0, 5)];
            if (kind < 4)      p[n++] = (op_t)SEND(q, AUTO, tmo);
            else if (kind < 8) p[n++] = (op_t)RECV(q, tmo);
            else if (kind < 9) p[n++] = (op_t)WORK(rnd(1, 2));
            else               p[n++] = (op_t)DELAY(rnd(1, 3));
        }
        p[n++] = (op_t)END_;
        k_task(g_names[i], rnd(1, 5), (uint32_t)rnd(0, 3), p);
    }
    k_run(TL);
}

/* Part C: 로그 큐. 생산자 3개(우선순위·타임아웃 정책이 다름) → 길이 qlen 큐 → UART 로거 1개 */
typedef struct { int ok[3], full[3], maxblk[3], logged, left; } logstat_t;
static logstat_t log_scenario(int uart_work, unsigned qlen, uint32_t ticks) {
    enum { QL = 0 };
    k_reset();
    q_init(&g_q[QL], qlen);
    k_task("CTRL", 4, 0, (op_t[]){ SEND(QL, AUTO, 0), DELAY(4), LOOP_ });          /* 제어 루프: 절대 안 기다림 */
    k_task("IMU",  3, 1, (op_t[]){ SEND(QL, AUTO, 2), DELAY(8), LOOP_ });          /* 최대 2 tick 기다리고 버림 */
    k_task("COMM", 2, 2, (op_t[]){ SEND(QL, AUTO, FOREVER), DELAY(16), LOOP_ });   /* 영원히 기다림 */
    if (uart_work > 0)                                                             /* UART: 받기 1 + 출력 uart_work tick */
        k_task("LOG", 1, 0, (op_t[]){ RECV(QL, FOREVER), WORK(uart_work), LOOP_ });
    else                                                                           /* DMA UART: 받기만 */
        k_task("LOG", 1, 0, (op_t[]){ RECV(QL, FOREVER), LOOP_ });
    k_run(ticks);
    logstat_t s;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 3; i++) {
        s.ok[i] = g_task[i].nok; s.full[i] = g_task[i].nfull; s.maxblk[i] = (int)g_task[i].max_elapsed;
    }
    s.logged = g_task[3].nok; s.left = g_q[QL].count;
    return s;
}

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

    /* ---- Part A1: 정렬 삽입 (TODO 1) ---- */
    {
        list_t l; node_t x[6]; list_init(&l);
        static const uint32_t vals[6] = { 5, 3, 5, 1, 3, 9 };
        for (int i = 0; i < 6; i++) { memset(&x[i], 0, sizeof x[i]); x[i].value = vals[i]; list_insert_sorted(&l, &x[i]); }
        int order[6], k = 0;
        for (node_t *p = l.end.next; p != &l.end && k < 6; p = p->next) order[k++] = (int)(p - x);
        CHECK(k == 6 && l.n == 6);
        CHECK(k == 6 && order[0] == 3 && order[1] == 1 && order[2] == 4 && order[3] == 0 && order[4] == 2 && order[5] == 5);
        list_remove(&x[1]); list_remove(&x[5]);
        CHECK(l.n == 4 && l.end.next == &x[3] && x[3].next == &x[4] && l.end.prev == &x[2] && x[1].container == NULL);
    }

    /* ---- Part A2: 빈 큐에서 블록 → send 가 깨움 ---- */
    enum { Q0 = 0 };
    k_reset(); q_init(&g_q[Q0], 2);
    tcb_t *c = k_task("C", 2, 0, (op_t[]){ RECV(Q0, FOREVER), END_ });
    tcb_t *p = k_task("P", 1, 0, (op_t[]){ WORK(3), SEND(Q0, 7, FOREVER), END_ });
    k_run(8);
    if (verbose) print_tl("A2 receiver blocks on empty queue, producer sends at t4");
    CHECK(c->last_rc == Q_OK && c->got[0] == 7 && c->last_done == 5 && c->nblock == 1);
    CHECK(row_is(c, "RBBBrR") && row_is(p, "rRRRR."));
    CHECK(g_yield_req == 1 && g_q[Q0].count == 0 && g_q[Q0].rx_wait.n == 0);
    k_reset(); q_init(&g_q[Q0], 2);                         /* 같은 prio 를 깨우면 yield 요청 없음 (엄격히 > 일 때만) */
    c = k_task("C", 1, 0, (op_t[]){ RECV(Q0, FOREVER), END_ });
    p = k_task("P", 1, 1, (op_t[]){ SEND(Q0, 9, 0), END_ });
    k_run(4);
    CHECK(g_yield_req == 0 && c->last_rc == Q_OK && c->got[0] == 9 && c->last_done == 2);

    /* ---- Part A3: 타임아웃 → 두 리스트 모두에서 빠지고 EMPTY / FULL ---- */
    k_reset(); q_init(&g_q[Q0], 1);
    c = k_task("C", 2, 0, (op_t[]){ RECV(Q0, 5), END_ });
    k_run(8);
    CHECK(c->last_rc == Q_EMPTY && c->last_done == 5 && c->last_elapsed == 5);
    CHECK(g_q[Q0].rx_wait.n == 0 && g_delayed.n == 0 && c->state == TASK_DONE);
    CHECK(strcmp(g_wakes, "C(t)") == 0);
    k_reset(); q_init(&g_q[Q0], 1);
    p = k_task("P", 2, 0, (op_t[]){ SEND(Q0, 1, 0), SEND(Q0, 2, 0), SEND(Q0, 3, 3), END_ });
    k_run(8);
    CHECK(p->nok == 1 && p->nfull == 2 && p->last_done == 5 && p->last_elapsed == 3);
    CHECK(g_q[Q0].count == 1 && g_q[Q0].tx_wait.n == 0 && g_delayed.n == 0);

    /* ---- Part A4: 받기 대기자 4명 — 최고 prio 부터, 같은 prio 는 먼저 온 순 ---- */
    k_reset(); q_init(&g_q[Q0], 4); zero_counters();
    tcb_t *r1 = k_task("R1", 1, 0, (op_t[]){ RECV(Q0, FOREVER), END_ });
    tcb_t *r2 = k_task("R2", 3, 1, (op_t[]){ RECV(Q0, FOREVER), END_ });
    tcb_t *r3 = k_task("R3", 2, 2, (op_t[]){ RECV(Q0, FOREVER), END_ });
    tcb_t *r4 = k_task("R4", 3, 3, (op_t[]){ RECV(Q0, FOREVER), END_ });
    k_task("S", 4, 5, (op_t[]){ SEND(Q0, 1, 0), SEND(Q0, 2, 0), SEND(Q0, 3, 0), SEND(Q0, 4, 0), END_ });
    k_run(14);
    print_tl("A4 four receivers (R1 p1, R2 p3, R3 p2, R4 p3), sender S p4");
    CHECK(strcmp(g_wakes, "R2 R4 R3 R1") == 0);
    CHECK(r2->got[0] == 1 && r4->got[0] == 2 && r3->got[0] == 3 && r1->got[0] == 4);
    CHECK(g_yield_req == 0 && clean());

    /* ---- Part B1: 깨어난 L 이 아이템을 H 에게 뺏긴다 → 남은 타임아웃으로 재블록 ---- */
    enum { BL = 0, BS = 1, BH = 2 };
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    k_task("L", 1, 0, (op_t[]){ RECV(Q0, 10), END_ });
    k_task("S", 2, 2, (op_t[]){ SEND(Q0, 7, 0), END_ });
    k_task("H", 3, 3, (op_t[]){ RECV(Q0, 0), END_ });
    k_run(12);
    print_tl("B1 L(p1) RECV tmo 10 @t0, S(p2) SEND @t2 wakes L, H(p3) RECV tmo 0 @t3 steals it");
    printf(" L: rc=%s at t%u, elapsed %u, blocked %d times\n", rc_name(g_task[BL].last_rc),
           g_task[BL].last_done, g_task[BL].last_elapsed, g_task[BL].nblock);
    CHECK(g_task[BH].last_rc == Q_OK && g_task[BH].got[0] == 7 && g_task[BH].last_done == 3);
    CHECK(g_task[BL].last_rc == Q_EMPTY && g_task[BL].last_done == 10 && g_task[BL].last_elapsed == 10);
    CHECK(g_task[BL].nblock == 2 && row_is(&g_task[BL], "RBrrRBBBBBR"));
    CHECK(strcmp(g_wakes, "L L(t)") == 0 && g_yield_req == 0 && clean());
    /* 재블록한 L 도 데이터가 오면 정상적으로 깬다 */
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    k_task("L", 1, 0, (op_t[]){ RECV(Q0, 10), END_ });
    k_task("S", 2, 2, (op_t[]){ SEND(Q0, 7, 0), END_ });
    k_task("H", 3, 3, (op_t[]){ RECV(Q0, 0), END_ });
    k_task("S2", 2, 6, (op_t[]){ SEND(Q0, 8, 0), END_ });
    k_run(12);
    CHECK(g_task[BL].last_rc == Q_OK && g_task[BL].got[0] == 8 && g_task[BL].last_done == 7 && clean());

    /* ---- Part B2: 타임아웃과 give 가 같은 tick — 규칙: "깨어난 태스크가 돌 때 데이터가 있으면 데이터가 이긴다" ---- */
    /* (a) 마감 tick(t5)에 ISR 이 보낸다: ① 에서 이미 타임아웃으로 깨어남 → ISR 은 대기자 없이 넣기만 → R 이 가져감 */
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    tcb_t *r = k_task("R", 2, 0, (op_t[]){ RECV(Q0, 5), END_ });
    g_isr_val = 42; g_isr_at[0] = 5; g_isr_at[1] = 99; g_isr_hook = isr_send_at;
    k_run(8);
    if (verbose) print_tl("B2a ISR sends at the deadline tick t5");
    CHECK(r->last_rc == Q_OK && r->got[0] == 42 && r->last_done == 5 && strcmp(g_wakes, "R(t)") == 0);
    /* (b) 더 높은 태스크가 마감 tick 에 보낸다 → R 은 t6 에 돌고 데이터가 있으니 OK (마감 지났어도) */
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    r = k_task("R", 2, 0, (op_t[]){ RECV(Q0, 5), END_ });
    k_task("Hs", 3, 0, (op_t[]){ DELAY(5), SEND(Q0, 43, 0), END_ });
    k_run(8);
    if (verbose) print_tl("B2b higher-prio Hs sends at t5");
    CHECK(r->last_rc == Q_OK && r->got[0] == 43 && r->last_done == 6 && r->last_elapsed == 6);
    /* (c) 더 낮은 태스크가 마감 tick 에 깬다 → R 이 먼저 돌아 EMPTY, 아이템은 큐에 남는다 */
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    r = k_task("R", 2, 0, (op_t[]){ RECV(Q0, 5), END_ });
    k_task("Ls", 1, 1, (op_t[]){ DELAY(4), SEND(Q0, 44, 0), END_ });
    k_run(8);
    if (verbose) print_tl("B2c lower-prio Ls wakes at t5 too");
    CHECK(r->last_rc == Q_EMPTY && r->last_done == 5 && g_task[1].last_rc == Q_OK && g_q[Q0].count == 1);
    /* (d) 마감 1 tick 전에 ISR → 데이터로 깸: 지연 리스트에서도 빠져야 다음 대기가 유령 타임아웃을 안 맞는다 */
    k_reset(); q_init(&g_q[Q0], 1); zero_counters();
    r = k_task("R", 2, 0, (op_t[]){ RECV(Q0, 5), RECV(Q0, FOREVER), END_ });
    g_isr_val = 45; g_isr_at[0] = 4; g_isr_at[1] = 9; g_isr_hook = isr_send_at;
    k_run(12);
    if (verbose) print_tl("B2d ISR sends at t4 (before the deadline) and t9");
    CHECK(r->ngot == 2 && r->got[0] == 45 && r->got[1] == 46 && r->last_done == 9);
    CHECK(strcmp(g_wakes, "R R") == 0 && r->nblock == 2 && clean());

    /* ---- Part B3: 꽉 찬 큐에 보내기 대기자 4명 → prio 순으로 깨어난다 ---- */
    k_reset(); q_init(&g_q[Q0], 2); zero_counters();
    q_push(&g_q[Q0], 100); q_push(&g_q[Q0], 101);
    g_fifo[Q0][0] = 100; g_fifo[Q0][1] = 101; g_fifo_w[Q0] = 2;
    k_task("S1", 1, 0, (op_t[]){ SEND(Q0, 1, FOREVER), END_ });
    k_task("S2", 3, 1, (op_t[]){ SEND(Q0, 2, FOREVER), END_ });
    k_task("S3", 2, 2, (op_t[]){ SEND(Q0, 3, FOREVER), END_ });
    k_task("S4", 3, 3, (op_t[]){ SEND(Q0, 4, FOREVER), END_ });
    tcb_t *cc = k_task("C", 4, 5, (op_t[]){ RECV(Q0, FOREVER), RECV(Q0, FOREVER), RECV(Q0, FOREVER),
                                            RECV(Q0, FOREVER), RECV(Q0, FOREVER), RECV(Q0, FOREVER), END_ });
    k_run(24);
    print_tl("B3 full queue (len 2), senders S1 p1, S2 p3, S3 p2, S4 p3; consumer C p4");
    CHECK(strncmp(g_wakes, "S2 S4", 5) == 0);
    CHECK(cc->ngot == 6 && cc->got[0] == 100 && cc->got[1] == 101 && cc->got[2] == 2 && cc->got[3] == 4 &&
          cc->got[4] == 3 && cc->got[5] == 1);
    CHECK(clean() && g_q[Q0].count == 0);

    /* ---- Part B4: 무작위 300 세트 — 매 tick 불변식 ---- */
    zero_counters();
    int all_ok = 0, all_err = 0;
    for (int s = 0; s < 300; s++) {
        random_set();
        for (int i = 0; i < g_nt; i++) { all_ok += g_task[i].nok; all_err += g_task[i].nfull + g_task[i].nempty; }
        for (int qi = 0; qi < NQ; qi++) if (g_fifo_w[qi] - g_fifo_r[qi] != g_q[qi].count) g_fifo_bad++;
    }
    printf("B4 random: 300 sets x %d ticks, ops ok=%d err=%d | invariant=%d lost-wakeup=%d deadline-moved=%d "
           "early-timeout=%d fifo/conservation=%d\n", TL, all_ok, all_err, g_inv_bad, g_lost, g_drift, g_early, g_fifo_bad);
    CHECK(g_inv_bad == 0 && g_lost == 0);
    CHECK(g_drift == 0 && g_early == 0 && g_fifo_bad == 0);
    CHECK(all_ok > 1000 && all_err > 100);

    /* ---- Part C: 로그 큐 (CTRL p4 tmo 0 / IMU p3 tmo 2 / COMM p2 FOREVER → LOG) ---- */
    zero_counters();
    logstat_t base = log_scenario(1, 4, 48);
    print_tl("C log queue len 4, LOG p1 + UART 1 tick (first 48 ticks)");
    (void)base;
    printf("C  UART work qlen | CTRL ok/full  IMU ok/full maxblk  COMM ok maxblk | logged\n");
    static const int uw[4] = { 1, 1, 0, 0 };
    static const unsigned ql[4] = { 4, 8, 4, 1 };
    logstat_t st[4];
    for (int i = 0; i < 4; i++) {
        st[i] = log_scenario(uw[i], ql[i], 240);
        printf("   %9d %4u | %7d/%-4d  %6d/%-4d %6d  %7d %6d | %6d\n", uw[i], ql[i], st[i].ok[0], st[i].full[0],
               st[i].ok[1], st[i].full[1], st[i].maxblk[1], st[i].ok[2], st[i].maxblk[2], st[i].logged);
    }
    for (int i = 0; i < 4; i++) CHECK(st[i].logged + st[i].left == st[i].ok[0] + st[i].ok[1] + st[i].ok[2]);
    CHECK(st[0].maxblk[0] == 0 && st[0].full[0] > st[0].full[1] + st[0].full[2]);   /* CTRL: 안 기다리지만 버림은 다 떠안음 */
    CHECK(st[0].maxblk[1] == 2 && st[0].full[2] == 0 && st[0].maxblk[2] >= 8);      /* IMU 2 tick 상한, COMM 은 오래 막힘 */
    CHECK(st[1].full[0] > 0);                                                       /* 큐를 늘려도 지속 과부하는 못 막는다 */
    CHECK(st[2].full[0] + st[2].full[1] == 0 && st[2].maxblk[2] == 0);             /* 소비 속도를 올리면 버림 0 */
    CHECK(st[3].full[0] > 0);                                                       /* 같은 속도라도 깊이 1 은 버스트에 진다 */
    CHECK(clean());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
