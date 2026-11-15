/* RTOS-B02 TCB 설계와 태스크 상태 전이
 *
 *   컨텍스트 스위치는 없다 (RTOS-B03). 이 레슨은 커널의 "장부" 만 만든다:
 *     TCB  = 스택 포인터 자리 + 우선순위(prio/base_prio) + 상태 + 깨어날 tick + 기다리는 객체 + 리스트 노드 2개
 *     상태 = READY / RUNNING / BLOCKED / SUSPENDED / DELETED — 상태마다 "어느 리스트에 있어야 하나" 가 정해져 있다.
 *   Part A  합법 전이 표 + 전이 함수 (make_ready, sched_run, block_on, delay, unblock, signal, tick, suspend, resume, delete)
 *   Part B  불법 전이(BLOCKED→RUNNING 직행 등)를 KERNEL_ASSERT 로 거부. 훅을 갈아 끼워 테스트가 위반을 센다
 *           (configASSERT 처럼). 5×5 전이 전수 검사. 진짜 abort(SIGABRT) death test 는 demo_death.c (fork+waitpid).
 *   Part C  BLE 센서 노드: IMU(DRDY 인터럽트 대기 + 타임아웃), BLE 주기 작업, 로그, 전원 관리 태스크의
 *           suspend / resume / delete. 매 tick 불변식(상태 ↔ 리스트 소속) 검사, 타임라인 출력.
 *
 * 결정적: 스레드·타이머 없음. 틀린 코드도 tick 상한에서 멈춘다 (무한 루프 없음).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st       (solution.c 도 같은 명령)
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
    (void)from; (void)to;                             /* TODO A1: 5×5 합법 전이 표 (READY RUNNING BLOCKED SUSPENDED DELETED) */
    return true;
}

/* A2: 어느 리스트에 있든 두 노드를 모두 빼낸다 (+ current 해제). 하나만 빼면 유령 노드가 남는다 */
static void detach(tcb_t *t) {
    (void)t;                                          /* TODO A2: state_node·event_node 둘 다 빼고, wait_obj·current 정리 */
}

/* A3: X → READY (선점·yield = RUNNING→READY, 깨어남 = BLOCKED→READY, resume = SUSPENDED→READY 의 공통 부분) */
static bool task_make_ready(tcb_t *t) {
    (void)t;                                          /* TODO A3: TRANSITION_CHECK → detach → ready[prio] 끝 → set_state */
    return false;
}

/* A4: 가장 높은 우선순위 READY 의 맨 앞 (선형 탐색 — O(1) 비트맵은 RTOS-B04) */
static tcb_t *sched_pick_next(void) {
    return NULL;                                      /* TODO A4: 가장 높은 우선순위 ready 리스트의 맨 앞 */
}
/* A4: READY → RUNNING (dispatch). 단일 코어: RUNNING 은 동시에 하나 */
static bool sched_run(tcb_t *t) {
    (void)t;                                          /* TODO A4: READY→RUNNING, current 가 비어 있어야 함 */
    return false;
}

/* A5: RUNNING → BLOCKED. obj 를 기다리며(없으면 순수 delay) 최대 ticks 동안. WAIT_FOREVER 는 obj 가 있을 때만 */
static bool task_block_on(tcb_t *t, kobj_t *obj, uint32_t ticks) {
    (void)t; (void)obj; (void)ticks;                  /* TODO A5: RUNNING→BLOCKED, event_node→obj, state_node→delayed */
    return false;
}
static bool task_delay(tcb_t *t, uint32_t ticks) { return task_block_on(t, NULL, ticks); }

/* A6: BLOCKED → READY. 이벤트로 깨든 타임아웃으로 깨든 두 리스트에서 모두 빠져야 한다 */
static bool task_unblock(tcb_t *t, wake_t why) {
    (void)t; (void)why;                               /* TODO A6: BLOCKED→READY (두 리스트 모두에서 빠짐) + 이유 */
    return false;
}
/* A6: 객체에 이벤트 1개 — 맨 앞(최고 우선순위, 가장 오래 기다린) 대기자를 깨운다. 없으면 NULL */
static tcb_t *obj_signal(kobj_t *o) {
    (void)o;                                          /* TODO A6: 맨 앞 대기자를 WAKE_EVENT 로 */
    return NULL;
}
/* A6: tick 인터럽트: 시각을 올리고, 깰 시각이 된 BLOCKED 를 모두 타임아웃으로 깨운다 */
static void kernel_tick(void) {
    K.tick++;                                         /* TODO A6: 깰 시각이 된 delayed 태스크를 WAKE_TIMEOUT 으로 */
}

/* A7: X → SUSPENDED. BLOCKED 였다면 기다리던 것을 잃는다 (두 리스트 모두에서 빠짐) */
static bool task_suspend(tcb_t *t) {
    (void)t;                                          /* TODO A7: X→SUSPENDED (BLOCKED 였으면 대기를 잃음) */
    return false;
}
/* A7: SUSPENDED → READY. 이 커널은 엄격하다: SUSPENDED 가 아닌 태스크 resume 은 계약 위반 */
static bool task_resume(tcb_t *t) {
    (void)t;                                          /* TODO A7: SUSPENDED→READY, 아니면 KERNEL_ASSERT */
    return false;
}
/* A7: X → DELETED. 모든 리스트에서 빠진다 (메모리 회수는 연습 4) */
static bool task_delete(tcb_t *t) {
    (void)t;                                          /* TODO A7: X→DELETED, 모든 리스트에서 빠짐 */
    return false;
}

/* ======================= Part B 하네스: from 상태의 태스크를 합법 경로로 만들고, to 로 가 본다 ======================= */
static const char LEGAL[TASK_NSTATES][TASK_NSTATES + 1] = { "01011", "10111", "10011", "10001", "00000" };
static kobj_t g_obj;
static tcb_t  g_t, g_u;

static bool make_task_in(task_state_t s) {
    k_init(); obj_init(&g_obj, "obj");
    if (!tcb_init(&g_t, "T", 3)) return false;
    switch (s) {
    case TASK_READY:     return g_t.state == TASK_READY;
    case TASK_RUNNING:   return sched_run(&g_t) && g_t.state == TASK_RUNNING;
    case TASK_BLOCKED:   return sched_run(&g_t) && task_block_on(&g_t, &g_obj, 10) && g_t.state == TASK_BLOCKED;
    case TASK_SUSPENDED: return task_suspend(&g_t) && g_t.state == TASK_SUSPENDED;
    case TASK_DELETED:   return task_delete(&g_t) && g_t.state == TASK_DELETED;
    default:             return false;
    }
}
static bool try_to(tcb_t *t, task_state_t to) {
    switch (to) {
    case TASK_READY:     return task_make_ready(t);
    case TASK_RUNNING:   return sched_run(t);
    case TASK_BLOCKED:   return task_block_on(t, &g_obj, 5);
    case TASK_SUSPENDED: return task_suspend(t);
    case TASK_DELETED:   return task_delete(t);
    default:             return false;
    }
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
static int c_run(void) {                              /* 반환: 누적 불변식 위반 수 */
    int inv = 0;
    for (int now = 0; now < C_TICKS; now++) {
        if (now) kernel_tick();                                           /* ① tick: 타임아웃 */
        if (drdy_at(K.tick)) { c_pulses++; if (!obj_signal(&drdy)) c_missed++; }   /* ② DRDY ISR */
        tcb_t *n = sched_pick_next();                                     /* ③ 스케줄 */
        if (K.current && n && n->prio > K.current->prio) task_make_ready(K.current);   /* 선점 */
        if (!K.current && (n = sched_pick_next()) != NULL) sched_run(n);
        tcb_t *ran = K.current;                                           /* ④ 한 tick 실행 */
        if (ran) ((app_t *)ran)->step((app_t *)ran);
        for (int i = 0; i < K.ntasks; i++) {
            const tcb_t *t = K.tasks[i];
            c_tl[i][now] = t == ran ? 'R' : t->state == TASK_READY ? 'r' : t->state == TASK_BLOCKED ? 'B'
                         : t->state == TASK_SUSPENDED ? 'S' : '.';
        }
        inv += k_check();
    }
    return inv;
}
static void c_print(void) {
    printf("tick : ");
    for (int t = 0; t < C_TICKS; t++) printf("%d ", t % 10);
    printf("\n");
    for (int i = 0; i < K.ntasks; i++) {
        printf("%-5.5s: ", K.tasks[i]->name);
        for (int t = 0; t < C_TICKS; t++) printf("%c ", c_tl[i][t] ? c_tl[i][t] : '.');
        printf("\n");
    }
    printf("DRDY : ");
    for (int t = 0; t < C_TICKS; t++) printf("%c ", drdy_at((uint32_t)t) ? '^' : ' ');
    printf("\n");
}
static void print_trans(void) {
    printf("transitions   ");
    for (int to = 0; to < TASK_NSTATES; to++) printf("%10s", ST_NAME[to]);
    printf("\n");
    for (int f = 0; f < TASK_NSTATES; f++) {
        printf("%-12s  ", ST_NAME[f]);
        for (int to = 0; to < TASK_NSTATES; to++) printf("%10u", K.trans[f][to]);
        printf("\n");
    }
}

int main(void) {
    g_kassert_hook = kassert_count;                   /* 테스트 동안은 멈추지 말고 센다 */
    (void)list_insert_sorted; (void)list_head; (void)set_state;      /* starter 의 스텁이 아직 안 부르는 */
    (void)detach; (void)task_unblock;                                 /* 함수들: 미사용 경고 방지 */
    tcb_t a, b, c, x, y, l, h1, h2, z;
    kobj_t q;

    /* ---------- Part A1: 합법 전이 표 ---------- */
    int mism = 0, nlegal = 0;
    for (int f = 0; f < TASK_NSTATES; f++)
        for (int to = 0; to < TASK_NSTATES; to++) {
            bool got = transition_ok((task_state_t)f, (task_state_t)to);
            nlegal += got; mism += got != (LEGAL[f][to] == '1');
        }
    CHECK(mism == 0 && nlegal == 12);
    CHECK(!transition_ok(TASK_BLOCKED, TASK_RUNNING) && !transition_ok(TASK_SUSPENDED, TASK_RUNNING) &&
          !transition_ok(TASK_RUNNING, TASK_RUNNING) && !transition_ok(TASK_DELETED, TASK_READY));

    /* ---------- Part A2: 생성 → dispatch → 선점 ---------- */
    k_init(); g_viol = 0; g_inv = 0;
    tcb_init(&a, "A", 1); tcb_init(&b, "B", 2); tcb_init(&c, "C", 3);
    CHECK(a.state == TASK_READY && K.ready[3].count == 1 && k_check() == 0);
    tcb_t *n = sched_pick_next();
    CHECK(n == &c && sched_run(&c) && c.state == TASK_RUNNING && K.current == &c && c.state_node.container == NULL);
    CHECK(sched_pick_next() == &b && K.ready[3].count == 0 && k_check() == 0);   /* RUNNING 은 ready 리스트 밖 */
    CHECK(task_make_ready(&c) && c.state == TASK_READY && K.current == NULL && K.ready[3].count == 1);

    /* ---------- Part A3: 타임아웃으로 깨어남 → 대기 리스트에서도 빠져야 한다 ---------- */
    k_init(); obj_init(&q, "q");
    tcb_init(&x, "X", 2);
    CHECK(sched_run(&x) && task_block_on(&x, &q, 3) && x.state == TASK_BLOCKED && K.current == NULL);
    CHECK(x.wake_tick == 3 && x.state_node.container == &K.delayed && x.event_node.container == &q.waiters);
    kernel_tick(); kernel_tick();
    CHECK(x.state == TASK_BLOCKED && k_check() == 0);
    kernel_tick();
    CHECK(x.state == TASK_READY && x.wake_reason == WAKE_TIMEOUT && q.waiters.count == 0 && K.delayed.count == 0);
    CHECK(obj_signal(&q) == NULL && x.state == TASK_READY && K.trans[TASK_BLOCKED][TASK_READY] == 1);  /* 두 번 깨지 않는다 */

    /* ---------- Part A4: 이벤트로 깨어남 → delayed 리스트에서도 빠져야 한다 ---------- */
    k_init(); obj_init(&q, "q");
    tcb_init(&y, "Y", 2);
    sched_run(&y); task_block_on(&y, &q, 5);
    CHECK(obj_signal(&q) == &y && y.state == TASK_READY && y.wake_reason == WAKE_EVENT && K.delayed.count == 0);
    for (int i = 0; i < 10; i++) kernel_tick();
    CHECK(y.state == TASK_READY && y.wake_reason == WAKE_EVENT && K.trans[TASK_BLOCKED][TASK_READY] == 1 && k_check() == 0);

    /* ---------- Part A5: 대기 순서 (우선순위 → FIFO), 무한 대기는 delayed 리스트에 없다 ---------- */
    k_init(); obj_init(&q, "q");
    tcb_init(&l, "L", 1); tcb_init(&h1, "H1", 3); tcb_init(&h2, "H2", 3);
    sched_run(&l);  task_block_on(&l, &q, WAIT_FOREVER);
    sched_run(&h1); task_block_on(&h1, &q, WAIT_FOREVER);
    sched_run(&h2); task_block_on(&h2, &q, WAIT_FOREVER);
    CHECK(q.waiters.count == 3 && K.delayed.count == 0 && l.state_node.container == NULL && k_check() == 0);
    tcb_t *w1 = obj_signal(&q), *w2 = obj_signal(&q), *w3 = obj_signal(&q);
    CHECK(w1 == &h1 && w2 == &h2 && w3 == &l && obj_signal(&q) == NULL);

    /* ---------- Part A6: 순수 delay, suspend 가 대기를 끊는다, resume ---------- */
    k_init(); obj_init(&q, "q");
    tcb_init(&x, "D", 2); tcb_init(&z, "Z", 3);
    sched_run(&x); task_delay(&x, 4);
    CHECK(x.state == TASK_BLOCKED && x.event_node.container == NULL && x.wait_obj == NULL && x.wake_tick == 4);
    sched_run(&z); task_block_on(&z, &q, 6);
    CHECK(task_suspend(&z) && z.state == TASK_SUSPENDED && z.wake_reason == WAKE_ABORT &&
          q.waiters.count == 0 && K.delayed.count == 1 && K.suspended.count == 1);
    for (int i = 0; i < 10; i++) kernel_tick();
    CHECK(x.state == TASK_READY && x.wake_reason == WAKE_TIMEOUT && z.state == TASK_SUSPENDED && obj_signal(&q) == NULL);
    CHECK(task_resume(&z) && z.state == TASK_READY && K.suspended.count == 0 && sched_pick_next() == &z);

    /* ---------- Part A7: 자기 자신 suspend / delete ---------- */
    CHECK(sched_run(&z) && task_suspend(&z) && K.current == NULL && z.state == TASK_SUSPENDED);
    CHECK(task_delete(&z) && z.state == TASK_DELETED && K.suspended.count == 0);
    CHECK(sched_run(&x) && task_delete(&x) && K.current == NULL && x.state_node.container == NULL);
    CHECK(g_viol == 0 && g_inv == 0);                 /* Part A 는 전부 합법 경로: 위반 0 */
    if (g_inv) printf("  first invariant error: %s\n", g_inv_msg);

    /* ---------- Part B1: 5×5 전수 — 합법은 성공, 불법은 assert 1번 + 아무것도 안 바뀜 ---------- */
    int legal_ok = 0, illegal_rejected = 0;
    printf("Part B1 transition matrix (ok = done, ASSERT = rejected, BUG = wrong)\n");
    printf("  from \\ to  ");
    for (int to = 0; to < TASK_NSTATES; to++) printf("%-10s", ST_NAME[to]);
    printf("\n");
    for (int f = 0; f < TASK_NSTATES; f++) {
        printf("  %-10s ", ST_NAME[f]);
        for (int to = 0; to < TASK_NSTATES; to++) {
            g_viol = 0; g_inv = 0;
            bool setup = make_task_in((task_state_t)f) && g_viol == 0;
            task_state_t s0 = g_t.state;
            const list_t *sc0 = g_t.state_node.container, *ec0 = g_t.event_node.container;
            const tcb_t *cur0 = K.current;
            int v0 = g_viol;
            bool r = try_to(&g_t, (task_state_t)to);
            int dv = g_viol - v0;
            bool inv_ok = k_check() == 0;
            bool good;
            if (LEGAL[f][to] == '1') {
                good = setup && r && g_t.state == (task_state_t)to && dv == 0 && inv_ok;
                legal_ok += good;
            } else {
                good = setup && !r && dv == 1 && g_t.state == s0 && g_t.state_node.container == sc0 &&
                       g_t.event_node.container == ec0 && K.current == cur0 && inv_ok;
                illegal_rejected += good;
            }
            printf("%-10s", !good ? "BUG" : LEGAL[f][to] == '1' ? "ok" : "ASSERT");
        }
        printf("\n");
    }
    printf("  legal ok %d/12, illegal rejected %d/13\n", legal_ok, illegal_rejected);
    CHECK(legal_ok == 12);
    CHECK(illegal_rejected == 13);

    /* ---------- Part B2: 대표 불법 전이와 메시지 ---------- */
    g_viol = 0;
    make_task_in(TASK_BLOCKED);
    CHECK(!sched_run(&g_t) && g_viol == 1 && strstr(g_kassert_msg, "BLOCKED -> RUNNING") != NULL);
    make_task_in(TASK_SUSPENDED);
    CHECK(!sched_run(&g_t) && g_viol == 2 && strstr(g_kassert_msg, "SUSPENDED -> RUNNING") != NULL);
    make_task_in(TASK_RUNNING);
    CHECK(!sched_run(&g_t) && g_viol == 3 && strstr(g_kassert_msg, "RUNNING -> RUNNING") != NULL);
    make_task_in(TASK_RUNNING); tcb_init(&g_u, "U", 1);  /* T 가 RUNNING 인데 U 를 또 dispatch */
    CHECK(!sched_run(&g_u) && g_viol == 4 && g_u.state == TASK_READY && strstr(g_kassert_msg, "still RUNNING") != NULL);
    make_task_in(TASK_READY);                         /* READY 를 또 READY 로 = ready 리스트 이중 삽입 */
    CHECK(!task_make_ready(&g_t) && g_viol == 5 && K.ready[3].count == 1 && k_check() == 0);
    make_task_in(TASK_READY);
    CHECK(!task_resume(&g_t) && g_viol == 6 && strstr(g_kassert_msg, "not SUSPENDED") != NULL);

    /* ---------- Part B3: 불변식 검사기 자체 시험 — 유령 노드를 일부러 만든다 ---------- */
    g_inv = 0;
    make_task_in(TASK_BLOCKED);
    list_remove(&g_t.state_node);                     /* 버그 있는 unblock: state_node 만 빼고 */
    g_t.state = TASK_READY;                           /* READY 로 바꿈 — event_node 는 obj 에 남았다 */
    list_insert_end(&K.ready[g_t.prio], &g_t.state_node);
    CHECK(k_check() > 0 && strstr(g_inv_msg, "event_node in obj") != NULL);

    /* ---------- Part C: BLE 센서 노드 ---------- */
    g_viol = 0; g_inv = 0;
    c_setup();
    int inv = c_run();
    printf("Part C BLE sensor node: %d ticks, IMU samples=%d timeouts=%d (first %d, last %d), DRDY pulses=%d missed=%d, BLE=%d, LOG ticks=%d\n",
           C_TICKS, c_samples, c_timeouts, c_first_timeout, c_last_timeout, c_pulses, c_missed, c_ble, c_logs);
    c_print();
    print_trans();
    if (inv) printf("  first invariant error: %s\n", g_inv_msg);
    CHECK(inv == 0 && g_viol == 0);
    int illegal_used = 0;
    for (int f = 0; f < TASK_NSTATES; f++)
        for (int to = 0; to < TASK_NSTATES; to++)
            if (LEGAL[f][to] == '0') illegal_used += (int)K.trans[f][to];
    CHECK(illegal_used == 0);
    CHECK(K.trans[TASK_READY][TASK_RUNNING] > 0 && K.trans[TASK_RUNNING][TASK_READY] > 0 &&
          K.trans[TASK_RUNNING][TASK_BLOCKED] > 0 && K.trans[TASK_BLOCKED][TASK_READY] > 0);
    CHECK(K.trans[TASK_READY][TASK_SUSPENDED] == 1 && K.trans[TASK_SUSPENDED][TASK_READY] == 1 &&
          K.trans[TASK_RUNNING][TASK_DELETED] == 1 && a_pwr.tcb.state == TASK_DELETED);
    int log_run_susp = 0, log_s = 0;
    for (int t = 8; t < 16; t++) { log_run_susp += c_tl[3][t] == 'R'; log_s += c_tl[3][t] == 'S'; }
    CHECK(log_run_susp == 0 && log_s == 8);            /* tick 8–15: LOG 는 한 번도 못 돈다 */
    CHECK(c_samples + c_missed == c_pulses && c_samples > 0);   /* DRDY 는 샘플이 되거나 놓쳤다 — 사라지지 않는다 */
    CHECK(c_timeouts >= 1 && c_first_timeout >= 13 && c_last_timeout <= 21);   /* 타임아웃은 센서가 멈춘 구간에서만 */
    int flow_bad = 0;                                  /* 흐름 보존: 시작 개수 + 들어온 수 - 나간 수 == 지금 개수 */
    for (int s = 0; s < TASK_NSTATES; s++) {
        int in = 0, out = 0, now_cnt = 0;
        for (int o = 0; o < TASK_NSTATES; o++) { in += (int)K.trans[o][s]; out += (int)K.trans[s][o]; }
        for (int i = 0; i < K.ntasks; i++) now_cnt += K.tasks[i]->state == (task_state_t)s;
        flow_bad += (s == TASK_READY ? 4 : 0) + in - out != now_cnt;
    }
    CHECK(flow_bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
