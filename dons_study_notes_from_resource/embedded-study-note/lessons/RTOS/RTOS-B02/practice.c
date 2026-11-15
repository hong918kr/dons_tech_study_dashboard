/* RTOS-B02 추가 연습 — 한 번에 검증
 *   연습 1  state_from_lists(): 리스트 소속만 보고 상태를 알아낸다 (FreeRTOS eTaskGetState 방식) — Part C 매 tick 교차 검사
 *   연습 2  task_abort_delay(): BLOCKED 를 강제로 READY 로 (xTaskAbortDelay). BLOCKED 가 아니면 false (assert 아님)
 *   연습 3  task_set_prio(): base_prio/prio 규칙 + 리스트 재배치. 대기 리스트 재정렬 O vs X(FreeRTOS 방식) 비교
 *   연습 4  자기 삭제와 지연 회수: 달리는 태스크는 자기 스택을 못 지운다 → 좀비 목록 → idle 이 회수
 * 커널(리스트·TCB·전이 함수)은 solution.c 와 같다 (자기완결 파일이라 복사해 둠).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================= KERNEL_ASSERT (configASSERT 자리, 하네스 제공) ======================= */
/* 기본 훅은 멈춘다(abort). 테스트는 훅을 바꿔 끼워 "몇 번 걸렸나" 를 센다.
 * 훅이 돌아오면 KERNEL_ASSERT 는 false — 호출한 전이 함수는 아무것도 바꾸지 않고 false 를 돌려줘야 한다. */
typedef void (*kassert_hook_t)(const char *msg, const char *file, int line);
static void kassert_abort(const char *msg, const char *file, int line) {
    fprintf(stderr, "KERNEL_ASSERT: %s (%s:%d)\n", msg, file, line);
    abort();
}
static kassert_hook_t g_kassert_hook = kassert_abort;
static char g_kassert_msg[128];
static int  g_viol;                                   /* 카운팅 훅이 센 위반 수 */
static void kassert_count(const char *msg, const char *file, int line) { (void)msg; (void)file; (void)line; g_viol++; }

__attribute__((format(printf, 3, 4)))
static bool kassert_failf(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_kassert_msg, sizeof g_kassert_msg, fmt, ap);
    va_end(ap);
    g_kassert_hook(g_kassert_msg, file, line);
    return false;
}
#define KERNEL_ASSERT(cond, ...) ((cond) ? true : kassert_failf(__FILE__, __LINE__, __VA_ARGS__))

/* ======================= 침투형(intrusive) 이중 연결 리스트 (하네스 제공) ======================= */
typedef struct tcb  tcb_t;
typedef struct list list_t;
typedef struct lnode {
    struct lnode *next, *prev;
    list_t       *container;   /* 지금 들어 있는 리스트 (NULL = 어디에도 없음)  — FreeRTOS pxContainer */
    tcb_t        *owner;       /* 이 노드를 품은 TCB                           — FreeRTOS pvOwner */
    uint32_t      value;       /* 정렬 키: state_node = wake_tick, event_node = NPRIO - prio */
} lnode_t;
struct list { lnode_t end; uint32_t count; const char *name; };      /* 원형 + 센티넬(end) */

static void list_init(list_t *l, const char *name) {
    l->end.next = l->end.prev = &l->end;
    l->end.container = l; l->end.owner = NULL; l->end.value = UINT32_MAX;
    l->count = 0; l->name = name;
}
static void list_insert_end(list_t *l, lnode_t *n) {
    n->prev = l->end.prev; n->next = &l->end;
    l->end.prev->next = n; l->end.prev = n;
    n->container = l; l->count++;
}
/* value 오름차순, 같은 값은 뒤로 (FreeRTOS list.c vListInsert 의 <= 규칙 → 같은 우선순위 FIFO).
 * tick wrap 은 다루지 않는다 (RTOS-B06). */
static void list_insert_sorted(list_t *l, lnode_t *n) {
    lnode_t *it = &l->end;
    while (it->next != &l->end && it->next->value <= n->value) it = it->next;
    n->next = it->next; n->prev = it;
    it->next->prev = n; it->next = n;
    n->container = l; l->count++;
}
static void list_remove(lnode_t *n) {
    if (!n->container) return;                        /* 리스트에 없으면 아무 일도 안 한다 */
    n->prev->next = n->next; n->next->prev = n->prev;
    n->container->count--;
    n->container = NULL; n->next = n->prev = NULL;
}
static tcb_t *list_head(const list_t *l) { return l->count ? l->end.next->owner : NULL; }

/* ======================= TCB · 상태 · 커널 ======================= */
#define NPRIO        8           /* 0(가장 낮음, idle) … 7. FreeRTOS 처럼 클수록 급하다 (Zephyr/µC/OS 는 반대) */
#define MAX_TASKS    8
#define STACK_WORDS  64
#define WAIT_FOREVER UINT32_MAX

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DELETED, TASK_NSTATES } task_state_t;
typedef enum { WAKE_NONE, WAKE_EVENT, WAKE_TIMEOUT, WAKE_ABORT } wake_t;
static const char *const ST_NAME[TASK_NSTATES] = { "READY", "RUNNING", "BLOCKED", "SUSPENDED", "DELETED" };

/* 대기 객체: 세마포어·큐·이벤트 그룹이 품는 "대기 리스트" 만 떼어 낸 것 (B08–B13 에서 살이 붙는다) */
typedef struct kobj { const char *name; list_t waiters; } kobj_t;

struct tcb {
    uint32_t     *sp;          /* 저장된 스택 포인터. 반드시 첫 멤버 — 포트 asm 이 [TCB+0] 으로 읽는다 (B03) */
    uint32_t     *stack_base;  /* 스택 영역 시작 (high-water mark·오버플로 검사, RTOS-M09) */
    const char   *name;
    uint8_t       prio;        /* 유효 우선순위 — 상속이면 올라간다 (RTOS-M04) */
    uint8_t       base_prio;   /* 원래 우선순위 — 상속이 끝나면 여기로 돌아온다 */
    task_state_t  state;
    wake_t        wake_reason; /* 마지막으로 BLOCKED 를 벗어난 이유 (블로킹 API 의 반환값이 된다) */
    uint32_t      wake_tick;   /* 타임아웃 시각 — BLOCKED + 유한 타임아웃일 때만 의미 */
    kobj_t       *wait_obj;    /* 기다리는 객체 (NULL = 순수 delay) */
    lnode_t       state_node;  /* "무슨 상태인가": ready[prio] | delayed | suspended | (없음) */
    lnode_t       event_node;  /* "무엇을 기다리나": wait_obj->waiters | (없음) */
};

typedef struct {
    list_t   ready[NPRIO];
    list_t   delayed;          /* 유한 타임아웃 BLOCKED, wake_tick 오름차순 */
    list_t   suspended;
    tcb_t   *current;          /* RUNNING 태스크 (NULL = idle) */
    uint32_t tick;
    tcb_t   *tasks[MAX_TASKS]; /* 하네스: 불변식 검사·타임라인용 전체 목록 */
    int      ntasks;
    unsigned trans[TASK_NSTATES][TASK_NSTATES];       /* 전이 횟수 (하네스 통계) */
} kernel_t;
static kernel_t K;
static uint32_t g_stacks[MAX_TASKS][STACK_WORDS];

static void k_init(void) {
    memset(&K, 0, sizeof K);
    for (int p = 0; p < NPRIO; p++) list_init(&K.ready[p], "ready");
    list_init(&K.delayed, "delayed");
    list_init(&K.suspended, "suspended");
}
static void obj_init(kobj_t *o, const char *name) { o->name = name; list_init(&o->waiters, name); }

/* xTaskCreate → prvAddNewTaskToReadyList 자리: 새 태스크는 곧바로 READY (ready 리스트 끝) */
static bool tcb_init(tcb_t *t, const char *name, uint8_t prio) {
    if (!KERNEL_ASSERT(prio < NPRIO && K.ntasks < MAX_TASKS, "tcb_init(%s): prio %u / %d tasks", name, prio, K.ntasks))
        return false;
    memset(t, 0, sizeof *t);
    t->name = name; t->prio = t->base_prio = prio;
    t->stack_base = g_stacks[K.ntasks];
    t->sp = g_stacks[K.ntasks] + STACK_WORDS;        /* 내려가며 쌓는 스택의 꼭대기 (자리만, B03 에서 프레임을 만든다) */
    t->state_node.owner = t; t->event_node.owner = t;
    t->state = TASK_READY;
    list_insert_end(&K.ready[prio], &t->state_node);
    K.tasks[K.ntasks++] = t;
    return true;
}
/* 상태 기록 (하네스): 전이 횟수를 센다. 합법성 검사는 호출자가 먼저 했다 */
static void set_state(tcb_t *t, task_state_t to) { K.trans[t->state][to]++; t->state = to; }

static bool transition_ok(task_state_t from, task_state_t to);
#define TRANSITION_CHECK(t, to) \
    KERNEL_ASSERT(transition_ok((t)->state, (to)), "illegal %s -> %s (%s)", ST_NAME[(t)->state], ST_NAME[to], (t)->name)

/* ======================= 불변식 검사 (하네스): 상태 ↔ 리스트 소속 ======================= */
static int  g_inv;                                    /* 누적 불변식 위반 수 */
static char g_inv_msg[160];
__attribute__((format(printf, 1, 2)))
static void inv_fail(const char *fmt, ...) {
    if (g_inv++ == 0) { va_list ap; va_start(ap, fmt); vsnprintf(g_inv_msg, sizeof g_inv_msg, fmt, ap); va_end(ap); }
}
static void list_check(const list_t *l, bool sorted) {
    const lnode_t *n = l->end.next;
    uint32_t cnt = 0, prev_val = 0;
    while (n != &l->end) {
        if (++cnt > MAX_TASKS + 1)              { inv_fail("%s: loop/too long", l->name); return; }
        if (!n->next || n->next->prev != n)     { inv_fail("%s: broken links", l->name); return; }
        if (n->container != l)                  { inv_fail("%s: node of %s has wrong container", l->name, n->owner ? n->owner->name : "?"); }
        if (sorted && cnt > 1 && n->value < prev_val) inv_fail("%s: not sorted at %s", l->name, n->owner ? n->owner->name : "?");
        prev_val = n->value;
        n = n->next;
    }
    if (cnt != l->count) inv_fail("%s: count %u but %u nodes", l->name, l->count, cnt);
}
static int k_check(void) {
    int before = g_inv, running = 0;
    for (int p = 0; p < NPRIO; p++) list_check(&K.ready[p], false);
    list_check(&K.delayed, true);
    list_check(&K.suspended, false);
    for (int i = 0; i < K.ntasks; i++) {
        const tcb_t *t = K.tasks[i];
        const list_t *sc = t->state_node.container, *ec = t->event_node.container;
        bool ok = false;
        if (ec) list_check(ec, true);
        switch (t->state) {
        case TASK_READY:     ok = sc == &K.ready[t->prio] && !ec && K.current != t; break;
        case TASK_RUNNING:   running++; ok = K.current == t && !sc && !ec; break;
        case TASK_BLOCKED:   ok = (sc == &K.delayed || (!sc && t->wait_obj)) && K.current != t &&
                                  ec == (t->wait_obj ? &t->wait_obj->waiters : NULL); break;
        case TASK_SUSPENDED: ok = sc == &K.suspended && !ec && !t->wait_obj && K.current != t; break;
        case TASK_DELETED:   ok = !sc && !ec && K.current != t; break;
        default:             ok = false; break;
        }
        if (!ok) inv_fail("%s: state %s but state_node in %s, event_node in %s", t->name,
                          (unsigned)t->state < TASK_NSTATES ? ST_NAME[t->state] : "?",
                          sc ? sc->name : "-", ec ? ec->name : "-");
    }
    if (running > 1 || (K.current && K.current->state != TASK_RUNNING)) inv_fail("RUNNING count %d / current mismatch", running);
    return g_inv - before;
}

/* ======================= Part A — 전이 표와 전이 함수 (학생 구현) ======================= */

/* A1: 합법 전이 표. RUNNING 으로 들어가는 길은 READY 하나뿐, DELETED 에서 나가는 길은 없다 */
static bool transition_ok(task_state_t from, task_state_t to) {
    static const bool ok[TASK_NSTATES][TASK_NSTATES] = {
        /* to:            READY  RUNNING BLOCKED SUSP   DELETED */
        /* READY     */ { false, true,   false,  true,  true  },
        /* RUNNING   */ { true,  false,  true,   true,  true  },
        /* BLOCKED   */ { true,  false,  false,  true,  true  },
        /* SUSPENDED */ { true,  false,  false,  false, true  },
        /* DELETED   */ { false, false,  false,  false, false },
    };
    if ((unsigned)from >= TASK_NSTATES || (unsigned)to >= TASK_NSTATES) return false;
    return ok[from][to];
}

/* A2: 어느 리스트에 있든 두 노드를 모두 빼낸다 (+ current 해제). 하나만 빼면 유령 노드가 남는다 */
static void detach(tcb_t *t) {
    list_remove(&t->state_node);
    list_remove(&t->event_node);
    t->wait_obj = NULL;
    if (K.current == t) K.current = NULL;
}

/* A3: X → READY (선점·yield = RUNNING→READY, 깨어남 = BLOCKED→READY, resume = SUSPENDED→READY 의 공통 부분) */
static bool task_make_ready(tcb_t *t) {
    if (!TRANSITION_CHECK(t, TASK_READY)) return false;
    detach(t);
    list_insert_end(&K.ready[t->prio], &t->state_node);
    set_state(t, TASK_READY);
    return true;
}

/* A4: 가장 높은 우선순위 READY 의 맨 앞 (선형 탐색 — O(1) 비트맵은 RTOS-B04) */
static tcb_t *sched_pick_next(void) {
    for (int p = NPRIO - 1; p >= 0; p--)
        if (K.ready[p].count) return list_head(&K.ready[p]);
    return NULL;
}
/* A4: READY → RUNNING (dispatch). 단일 코어: RUNNING 은 동시에 하나 */
static bool sched_run(tcb_t *t) {
    if (!TRANSITION_CHECK(t, TASK_RUNNING)) return false;
    if (!KERNEL_ASSERT(K.current == NULL, "%s still RUNNING, cannot dispatch %s",
                       K.current ? K.current->name : "-", t->name)) return false;
    list_remove(&t->state_node);
    K.current = t;
    set_state(t, TASK_RUNNING);
    return true;
}

/* A5: RUNNING → BLOCKED. obj 를 기다리며(없으면 순수 delay) 최대 ticks 동안. WAIT_FOREVER 는 obj 가 있을 때만 */
static bool task_block_on(tcb_t *t, kobj_t *obj, uint32_t ticks) {
    if (!TRANSITION_CHECK(t, TASK_BLOCKED)) return false;
    if (!KERNEL_ASSERT(ticks > 0 && (obj || ticks != WAIT_FOREVER), "bad block_on(%s, %u)", t->name, (unsigned)ticks))
        return false;
    detach(t);
    t->wake_reason = WAKE_NONE;
    if (obj) {                                        /* "무엇을 기다리나": 우선순위 순 → 같으면 FIFO */
        t->wait_obj = obj;
        t->event_node.value = NPRIO - t->prio;
        list_insert_sorted(&obj->waiters, &t->event_node);
    }
    if (ticks != WAIT_FOREVER) {                      /* "언제까지": 깨어날 tick 순 */
        t->wake_tick = K.tick + ticks;
        t->state_node.value = t->wake_tick;
        list_insert_sorted(&K.delayed, &t->state_node);
    }
    set_state(t, TASK_BLOCKED);
    return true;
}
static bool task_delay(tcb_t *t, uint32_t ticks) { return task_block_on(t, NULL, ticks); }

/* A6: BLOCKED → READY. 이벤트로 깨든 타임아웃으로 깨든 두 리스트에서 모두 빠져야 한다 */
static bool task_unblock(tcb_t *t, wake_t why) {
    if (!KERNEL_ASSERT(t->state == TASK_BLOCKED, "unblock %s: not BLOCKED (%s)", t->name, ST_NAME[t->state]))
        return false;
    if (!task_make_ready(t)) return false;
    t->wake_reason = why;
    return true;
}
/* A6: 객체에 이벤트 1개 — 맨 앞(최고 우선순위, 가장 오래 기다린) 대기자를 깨운다. 없으면 NULL */
static tcb_t *obj_signal(kobj_t *o) {
    tcb_t *t = list_head(&o->waiters);
    if (t) task_unblock(t, WAKE_EVENT);
    return t;
}
/* A6: tick 인터럽트: 시각을 올리고, 깰 시각이 된 BLOCKED 를 모두 타임아웃으로 깨운다 */
static void kernel_tick(void) {
    K.tick++;
    tcb_t *t;
    while ((t = list_head(&K.delayed)) != NULL && t->wake_tick <= K.tick)
        if (!task_unblock(t, WAKE_TIMEOUT)) break;    /* 실패하면 같은 노드에서 무한 반복하지 않게 */
}

/* A7: X → SUSPENDED. BLOCKED 였다면 기다리던 것을 잃는다 (두 리스트 모두에서 빠짐) */
static bool task_suspend(tcb_t *t) {
    if (!TRANSITION_CHECK(t, TASK_SUSPENDED)) return false;
    if (t->state == TASK_BLOCKED) t->wake_reason = WAKE_ABORT;
    detach(t);
    list_insert_end(&K.suspended, &t->state_node);
    set_state(t, TASK_SUSPENDED);
    return true;
}
/* A7: SUSPENDED → READY. 이 커널은 엄격하다: SUSPENDED 가 아닌 태스크 resume 은 계약 위반 */
static bool task_resume(tcb_t *t) {
    if (!KERNEL_ASSERT(t->state == TASK_SUSPENDED, "resume %s: not SUSPENDED (%s)", t->name, ST_NAME[t->state]))
        return false;
    return task_make_ready(t);
}
/* A7: X → DELETED. 모든 리스트에서 빠진다 (메모리 회수는 연습 4) */
static bool task_delete(tcb_t *t) {
    if (!TRANSITION_CHECK(t, TASK_DELETED)) return false;
    detach(t);
    set_state(t, TASK_DELETED);
    return true;
}

/* ======================= Part C 하네스: BLE 센서 노드 (tick 시뮬레이터) ======================= */
typedef struct app { tcb_t tcb; void (*step)(struct app *); int pc, left; } app_t;   /* tcb 가 첫 멤버 */
#define C_TICKS 30
static app_t  a_pwr, a_imu, a_ble, a_log;
static kobj_t drdy;                                   /* IMU data-ready 인터럽트를 기다리는 객체 */
static int    c_samples, c_timeouts, c_missed, c_pulses, c_ble, c_logs, c_first_timeout, c_last_timeout;
static char   c_tl[MAX_TASKS][C_TICKS + 1];

static void pwr_step(app_t *a) {                      /* p4 전원 관리: tick 8 에 LOG 정지, 16 에 재개, 끝나면 스스로 삭제 */
    tcb_t *t = &a->tcb;
    switch (a->pc++) {
    case 0:  task_delay(t, 8); break;
    case 1:  task_suspend(&a_log.tcb); task_delay(t, 8); break;     /* 배터리 낮음: 플래시 로그 중단 */
    default: task_resume(&a_log.tcb); task_delete(t); break;        /* 1회성 태스크 종료 */
    }
}
static void imu_step(app_t *a) {                      /* p3 IMU: DRDY 대기(타임아웃 3 tick). 샘플 1 tick, 타임아웃이면 I2C 복구 2 tick */
    tcb_t *t = &a->tcb;
    if (a->left > 0) { if (--a->left == 0) task_block_on(t, &drdy, 3); return; }
    if (a->pc++ == 0) { task_block_on(t, &drdy, 3); return; }
    if (t->wake_reason == WAKE_EVENT) { c_samples++; task_block_on(t, &drdy, 3); return; }
    c_timeouts++; a->left = 1;                        /* 이번 tick + 다음 tick = 버스 복구 */
    if (c_first_timeout < 0) c_first_timeout = (int)K.tick;
    c_last_timeout = (int)K.tick;
}
static void ble_step(app_t *a) {                      /* p2 BLE: 연결 이벤트 처리 2 tick → 6 tick 쉼 */
    if (++a->left == 2) { a->left = 0; c_ble++; task_delay(&a->tcb, 6); }
}
static void log_step(app_t *a) { (void)a; c_logs++; } /* p1 로그: 블록하지 않는 백그라운드 작업 */

static bool drdy_at(uint32_t now) { return now >= 2 && now % 2 == 0 && !(now >= 13 && now <= 19); }  /* 13–19: 센서 멈춤 */

static void c_setup(void) {
    k_init(); obj_init(&drdy, "drdy");
    memset(c_tl, 0, sizeof c_tl);
    c_samples = c_timeouts = c_missed = c_pulses = c_ble = c_logs = 0;
    c_first_timeout = c_last_timeout = -1;
    static const struct { app_t *a; const char *n; uint8_t p; void (*fn)(app_t *); } T[4] = {
        { &a_pwr, "PWR", 4, pwr_step }, { &a_imu, "IMU", 3, imu_step },
        { &a_ble, "BLE", 2, ble_step }, { &a_log, "LOG", 1, log_step },
    };
    for (int i = 0; i < 4; i++) {
        memset(T[i].a, 0, sizeof *T[i].a);
        tcb_init(&T[i].a->tcb, T[i].n, T[i].p);
        T[i].a->step = T[i].fn;
    }
}

/* ======================= 연습 1 — 리스트만 보고 상태 알아내기 ======================= */
/* t->state 를 믿지 않고 "어느 리스트에 들어 있나" 만으로 판정. FreeRTOS eTaskGetState 가 이렇게 한다
 * (TCB 에 state 필드가 없다). 이 커널은 무한 대기를 delayed 에 넣지 않으므로 event_node 만 보고 BLOCKED. */
static task_state_t state_from_lists(const tcb_t *t) {
    const list_t *sc = t->state_node.container, *ec = t->event_node.container;
    if (K.current == t) return TASK_RUNNING;
    if (sc == &K.suspended) return TASK_SUSPENDED;
    if (sc == &K.delayed || ec) return TASK_BLOCKED;
    for (int p = 0; p < NPRIO; p++)
        if (sc == &K.ready[p]) return TASK_READY;
    return TASK_DELETED;                              /* 어디에도 없음 (FreeRTOS: pxStateList == NULL → eDeleted) */
}

/* ======================= 연습 2 — xTaskAbortDelay ======================= */
static bool task_abort_delay(tcb_t *t) {
    if (t->state != TASK_BLOCKED) return false;      /* 조회 후 거절: 사용자 API 의 정상 실패 (pdFAIL) */
    return task_unblock(t, WAKE_ABORT);
}

/* ======================= 연습 3 — 우선순위 변경 ======================= */
/* base_prio 는 항상 새 값. prio 는 상속 중(prio > base)이면 새 값이 더 클 때만 바뀐다 (FreeRTOS tasks.c:2954-2964).
 * READY 면 ready 리스트를 옮기고, 대기 중이면 정렬 키를 바꾼다. resort=false 는 FreeRTOS 처럼 "키만" 바꾼다. */
static void task_set_prio(tcb_t *t, uint8_t p, bool resort) {
    bool inherited = t->prio > t->base_prio;
    t->base_prio = p;
    uint8_t newp = (!inherited || p > t->prio) ? p : t->prio;
    if (newp == t->prio) return;
    t->prio = newp;
    if (t->state == TASK_READY) {
        list_remove(&t->state_node);
        list_insert_end(&K.ready[newp], &t->state_node);
    }
    if (t->event_node.container) {
        list_t *wl = t->event_node.container;
        t->event_node.value = NPRIO - newp;
        if (resort) { list_remove(&t->event_node); list_insert_sorted(wl, &t->event_node); }
    }
}

/* ======================= 연습 4 — 자기 삭제와 지연 회수 ======================= */
#define DEAD_FILL 0xDEADBEEFu
static tcb_t *g_zombie[MAX_TASKS];                    /* FreeRTOS xTasksWaitingTermination 자리 */
static int    g_nzombie, g_reclaimed;
static void reclaim(tcb_t *t) {                       /* 스택을 "반납" (여기선 패턴으로 덮어 표시) */
    for (int i = 0; i < STACK_WORDS; i++) t->stack_base[i] = DEAD_FILL;
    t->sp = NULL;
    g_reclaimed++;
}
static bool task_delete_deferred(tcb_t *t) {
    bool self = K.current == t;                       /* 지금 이 스택 위에서 달리는 중인가 */
    if (!task_delete(t)) return false;
    if (self) g_zombie[g_nzombie++] = t;              /* 지금 지우면 발밑이 꺼진다 → idle 에게 미룬다 */
    else reclaim(t);
    return true;
}
static void idle_reclaim(void) {                      /* idle 태스크 (prvCheckTasksWaitingTermination 자리) */
    while (g_nzombie > 0) reclaim(g_zombie[--g_nzombie]);
}

int main(void) {
    g_kassert_hook = kassert_count;
    (void)list_head; (void)set_state; (void)detach;

    /* ---------- 연습 1: Part C 시나리오를 돌리며 매 tick, 모든 태스크 교차 검사 ---------- */
    c_setup();
    int mism = 0, checks = 0;
    for (int now = 0; now < C_TICKS; now++) {        /* c_run 과 같은 루프 + 교차 검사 */
        if (now) kernel_tick();
        if (drdy_at(K.tick)) { c_pulses++; if (!obj_signal(&drdy)) c_missed++; }
        tcb_t *n = sched_pick_next();
        if (K.current && n && n->prio > K.current->prio) task_make_ready(K.current);
        if (!K.current && (n = sched_pick_next()) != NULL) sched_run(n);
        tcb_t *ran = K.current;
        if (ran) ((app_t *)ran)->step((app_t *)ran);
        for (int i = 0; i < K.ntasks; i++, checks++) mism += state_from_lists(K.tasks[i]) != K.tasks[i]->state;
    }
    printf("ex1 state_from_lists: %d checks over %d ticks, mismatches=%d, invariant errors=%d\n", checks, C_TICKS, mism, k_check());
    CHECK(mism == 0 && checks == 4 * C_TICKS && g_viol == 0);
    tcb_t f; kobj_t q;                                /* 무한 대기: delayed 에 없어도 event_node 로 BLOCKED */
    k_init(); obj_init(&q, "q"); tcb_init(&f, "F", 2);
    sched_run(&f); task_block_on(&f, &q, WAIT_FOREVER);
    CHECK(f.state_node.container == NULL && state_from_lists(&f) == TASK_BLOCKED);

    /* ---------- 연습 2: abort delay ---------- */
    tcb_t d, r;
    k_init(); obj_init(&q, "q"); tcb_init(&d, "D", 2); tcb_init(&r, "R", 1);
    sched_run(&d); task_delay(&d, 100);
    g_viol = 0;
    CHECK(task_abort_delay(&d) && d.state == TASK_READY && d.wake_reason == WAKE_ABORT && K.delayed.count == 0);
    CHECK(!task_abort_delay(&r) && r.state == TASK_READY && g_viol == 0);   /* READY 에 abort: 조용히 false */
    sched_run(&d); task_block_on(&d, &q, 50);
    CHECK(task_abort_delay(&d) && q.waiters.count == 0 && K.delayed.count == 0 && k_check() == 0);
    printf("ex2 abort_delay: delayed task -> READY (ABORT), READY task -> false, violations=%d\n", g_viol);

    /* ---------- 연습 3: 우선순위 변경 ---------- */
    tcb_t lo, mid;
    for (int resort = 1; resort >= 0; resort--) {
        k_init(); obj_init(&q, "q"); tcb_init(&lo, "LO", 1); tcb_init(&mid, "MID", 2);
        sched_run(&mid); task_block_on(&mid, &q, WAIT_FOREVER);
        sched_run(&lo);  task_block_on(&lo, &q, WAIT_FOREVER);       /* 대기 리스트: MID, LO */
        task_set_prio(&lo, 3, resort != 0);                          /* LO 를 MID 보다 높게 */
        g_inv = 0;
        int inv = k_check();
        tcb_t *first = obj_signal(&q);
        printf("ex3 resort=%d: LO prio 1->3 while waiting, first woken=%s, invariant errors=%d%s%s\n",
               resort, first ? first->name : "-", inv, inv ? " — " : "", inv ? g_inv_msg : "");
        if (resort) CHECK(first == &lo && inv == 0);
        else        CHECK(first == &mid && inv >= 1 && strstr(g_inv_msg, "not sorted") != NULL);
    }
    k_init(); tcb_init(&lo, "LO", 1); tcb_init(&mid, "MID", 2);
    task_set_prio(&lo, 4, true);                                     /* READY: ready 리스트 이동 */
    CHECK(lo.prio == 4 && lo.base_prio == 4 && lo.state_node.container == &K.ready[4] && sched_pick_next() == &lo);
    mid.prio = 5;                                                    /* MID 가 상속으로 5 (base 2) 라고 하자 */
    list_remove(&mid.state_node); list_insert_end(&K.ready[5], &mid.state_node);
    task_set_prio(&mid, 3, true);                                    /* 상속 중: base 만 3, prio 5 유지 */
    CHECK(mid.prio == 5 && mid.base_prio == 3);
    task_set_prio(&mid, 6, true);                                    /* 상속보다 높게: prio 도 6 */
    CHECK(mid.prio == 6 && mid.base_prio == 6 && mid.state_node.container == &K.ready[6] && k_check() == 0);

    /* ---------- 연습 4: 자기 삭제 → 좀비 → idle 회수 ---------- */
    tcb_t s1, o1;
    k_init(); tcb_init(&s1, "SELF", 3); tcb_init(&o1, "OTHER", 2);
    g_nzombie = g_reclaimed = 0; g_viol = 0;
    sched_run(&s1);
    CHECK(task_delete_deferred(&s1) && s1.state == TASK_DELETED && g_nzombie == 1 && g_reclaimed == 0 && s1.sp != NULL);
    CHECK(task_delete_deferred(&o1) && g_reclaimed == 1 && o1.sp == NULL && o1.stack_base[0] == DEAD_FILL);
    idle_reclaim();
    CHECK(g_nzombie == 0 && g_reclaimed == 2 && s1.sp == NULL && s1.stack_base[STACK_WORDS - 1] == DEAD_FILL);
    CHECK(!task_make_ready(&s1) && g_viol == 1 && strstr(g_kassert_msg, "DELETED -> READY") != NULL);   /* use-after-delete */
    printf("ex4 delete: self -> zombie then idle reclaim, other -> immediate; reclaimed=%d, use-after-delete violations=%d\n",
           g_reclaimed, g_viol);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
