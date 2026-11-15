/* RTOS-M02 추가 연습 — 한 번에 검증
 *
 * solution.c 의 미니 커널(TODO 를 채운 판)을 그대로 복사하고 연습용 스위치 셋을 더했다:
 *   g_guard_on (연습 1), g_syscall_max + K.crit (연습 4), g_naive_exit (연습 5).
 * 연습 1 가드가 없으면 누가 막히나   연습 2 deferred function call (xTimerPendFunctionCallFromISR 모델)
 * 연습 3 지연 공식 vs 시뮬레이터 (80 위상)   연습 4 configMAX_SYSCALL_INTERRUPT_PRIORITY   연습 5 ISR 폭풍과 PendSV 1번
 * 실행: ./practice  (표만)   ./practice -v  (타임라인까지)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_verbose;

/* ======================= KERNEL_ASSERT (configASSERT 자리, RTOS-B02 와 같은 구조) ======================= */
/* 기본 훅은 멈춘다(abort). 테스트는 훅을 바꿔 끼워 "몇 번 걸렸나" 를 센다. 훅이 돌아오면 KERNEL_ASSERT 는 false. */
typedef void (*kassert_hook_t)(const char *msg, const char *file, int line);
static void kassert_abort(const char *msg, const char *file, int line) {
    fprintf(stderr, "KERNEL_ASSERT: %s (%s:%d)\n", msg, file, line);
    abort();
}
static kassert_hook_t g_kassert_hook = kassert_abort;
static char g_kassert_msg[128];
static int  g_viol;                                   /* 카운팅 훅이 센 위반 수 */
static void kassert_count(const char *msg, const char *file, int line) {
    (void)file; (void)line;
    g_viol++;
    if (g_verbose) printf("    (KERNEL_ASSERT counted) %s\n", msg);
}
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

/* ======================= 미니 커널 ======================= */
enum { MAXT = 6, MAXIRQ = 5, TICK = 10, FOREVER = -1, NO_WAKE = -1, TL = 110 };
typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
/* RUNNING 은 따로 저장하지 않는다: K.cur 인 READY 태스크가 실행 중 (FreeRTOS 도 실행 중 태스크를 ready 리스트에 둔다) */
typedef enum { K_OK, K_EMPTY, K_FULL, K_BLOCKED, K_ERR_ISR } krc_t;

typedef struct tcb {
    const char *name;
    uint8_t prio;                 /* 클수록 급함 (FreeRTOS 방향. Zephyr/uC-OS 는 반대) */
    task_state_t state;
    int32_t wake_tick;            /* 타임아웃/지연으로 깨어날 tick (NO_WAKE = 없음) */
    void *wait_obj;               /* 기다리는 세마포어/뮤텍스 (NULL = 지연만) */
    uint32_t seq;                 /* 막힌 순서 — 같은 prio 대기자는 FIFO */
    void (*step)(int id);         /* 태스크 몸체: 한 번 불릴 때 일 최대 1단위 */
} tcb_t;

typedef struct { unsigned count, max; } ksem_t;
typedef struct { int owner; } kmutex_t;

static struct {
    tcb_t t[MAXT];
    int nt;
    int cur;                      /* pxCurrentTCB. ISR 안에서는 "ISR 이 끼어든 태스크" (-1 = idle) */
    int32_t tick;                 /* xTickCount */
    int step;                     /* 시뮬레이션 시간 */
    bool in_isr;                  /* ISR 문맥 플래그 */
    int nest;                     /* ISR 중첩 깊이 */
    bool pendsv;                  /* portYIELD_FROM_ISR(pdTRUE) → PendSV pending. 가장 바깥 ISR 이 끝날 때 처리 */
    bool yield_missed;            /* xYieldPendings[0]: give_from_isr 가 더 높은 태스크를 깨웠다 (woken 을 버려도 남음) */
    bool crit;                    /* 태스크가 임계 구역 안 (BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY) */
    int nswitch;                  /* 컨텍스트 스위치 횟수 */
    uint32_t seq;
} K;
static int  g_syscall_max = 100;  /* 연습 4: 이 hwprio 이하만 커널 API 허용 + 임계 구역에서 가려짐 (configMAX_SYSCALL_INTERRUPT_PRIORITY) */
static bool g_guard_on = true;    /* 연습 1: false 면 api_guard 가 없는 커널 */
static bool g_naive_exit;         /* 연습 5: true 면 isr_exit 가 중첩을 무시하고 바로 스위치 시도 */
static int  isr_cur_hwprio(void); /* 지금 실행 중인 ISR 의 hwprio (시뮬레이터가 안다) */
static bool g_step_used;          /* 이번 step 의 일 1단위를 썼다 */
static int g_worker = -1;         /* 그 1단위를 쓴 태스크 (타임라인 R) */

static void kernel_reset(void) { memset(&K, 0, sizeof K); K.cur = -1; }
static int task_add(const char *name, uint8_t prio, void (*fn)(int)) {
    tcb_t *t = &K.t[K.nt];
    memset(t, 0, sizeof *t);
    t->name = name; t->prio = prio; t->state = TASK_READY; t->wake_tick = NO_WAKE; t->step = fn;
    return K.nt++;
}
static void cpu_work(void) { g_step_used = true; g_worker = K.cur; }

/* 가장 높은 READY. 같은 prio 면 지금 돌던 태스크를 유지 (time slicing 없음) */
static int sched_pick_next(void) {
    int best = -1;
    for (int i = 0; i < K.nt; i++) {
        if (K.t[i].state != TASK_READY) continue;
        if (best < 0 || K.t[i].prio > K.t[best].prio || (K.t[i].prio == K.t[best].prio && i == K.cur)) best = i;
    }
    return best;
}
/* 컨텍스트 스위치 (= PendSV 핸들러 안의 vTaskSwitchContext). ISR 이 하나라도 살아 있으면 해서는 안 된다. */
static void port_switch(void) {
    if (!KERNEL_ASSERT(K.nest == 0, "context switch while %d ISR(s) active", K.nest)) return;
    K.yield_missed = false;                       /* vTaskSwitchContext: xYieldPendings[0] = pdFALSE */
    int n = sched_pick_next();
    if (n != K.cur) { K.cur = n; K.nswitch++; }
}
static void task_unblock(int id) {
    K.t[id].state = TASK_READY; K.t[id].wait_obj = NULL; K.t[id].wake_tick = NO_WAKE;
}
static void task_block_cur(void *obj, int timeout) {   /* 실행 중 태스크를 막고 바로 스위치 (portYIELD_WITHIN_API) */
    if (!KERNEL_ASSERT(K.cur >= 0, "blocking call with no running task")) return;
    tcb_t *t = &K.t[K.cur];
    t->state = TASK_BLOCKED; t->wait_obj = obj; t->seq = ++K.seq;
    t->wake_tick = (timeout == FOREVER) ? NO_WAKE : K.tick + timeout;
    port_switch();
}
static int best_waiter(const void *obj) {             /* obj 를 기다리는 태스크 중 prio 최고, 같으면 먼저 막힌 쪽 */
    int b = -1;
    for (int i = 0; i < K.nt; i++) {
        const tcb_t *t = &K.t[i];
        if (t->state != TASK_BLOCKED || t->wait_obj != obj) continue;
        if (b < 0 || t->prio > K.t[b].prio || (t->prio == K.t[b].prio && t->seq < K.t[b].seq)) b = i;
    }
    return b;
}
static bool higher_than_cur(int id) { return K.cur < 0 || K.t[id].prio > K.t[K.cur].prio; }

/* ---- TODO 1: ISR 입구/출구 ----
 * isr_enter: 중첩 깊이 +1, ISR 문맥 플래그 켜기.
 * isr_exit(higher_woken): portYIELD_FROM_ISR(higher_woken) + ISR 복귀.
 *   higher_woken 이면 PendSV 를 pending 으로 (요청만!). 깊이 -1.
 *   아직 바깥 ISR 이 남아 있으면 (nest > 0) 그냥 돌아간다 — 스위치 금지.
 *   가장 바깥 ISR 이 끝났으면 플래그를 끄고, PendSV 가 pending 이면 지우고 port_switch(). */
static void isr_enter(void) {
    K.nest++;
    K.in_isr = true;
}
static void isr_exit(bool higher_woken) {
    if (g_naive_exit && higher_woken) port_switch();   /* 연습 5 의 잘못된 포트: 중첩 중에도 바로 바꾸려 함 */
    if (higher_woken) K.pendsv = true;            /* portYIELD_FROM_ISR(pdTRUE): SCB->ICSR = PENDSVSET */
    K.nest--;
    if (K.nest > 0) return;                       /* 바깥 ISR 로 복귀 — 여기서 바꾸면 안 된다 */
    K.in_isr = false;
    if (K.pendsv) { K.pendsv = false; port_switch(); }   /* 가장 바깥 ISR 이 끝난 직후 = PendSV 실행 */
}

/* ---- TODO 3: 블로킹 API 가드 ----
 * ISR 문맥에서 may_block(막히거나 스스로 스위치할 수 있는) API 가 불리면 KERNEL_ASSERT 실패 → false.
 * 메시지에 api 이름을 넣는다 ("%s called from ISR"). 그 밖에는 true. */
static bool api_guard(const char *api, bool may_block) {
    if (!g_guard_on) return true;
    return KERNEL_ASSERT(!(K.in_isr && may_block), "%s() called from ISR (nest=%d)", api, K.nest);
}

/* ---- 태스크용 API (가드 호출은 주어진 코드) ---- */
static krc_t sem_take(ksem_t *s, int timeout) {        /* xSemaphoreTake */
    if (!api_guard("sem_take", timeout != 0)) return K_ERR_ISR;
    if (s->count > 0) { s->count--; return K_OK; }
    if (timeout == 0) return K_EMPTY;
    task_block_cur(s, timeout);
    return K_BLOCKED;
}
static krc_t sem_give(ksem_t *s) {                     /* xSemaphoreGive — 더 높은 태스크를 깨우면 즉시 스위치 */
    if (!api_guard("sem_give", true)) return K_ERR_ISR;
    if (s->count >= s->max) return K_FULL;
    s->count++;
    int w = best_waiter(s);
    if (w >= 0) { task_unblock(w); if (higher_than_cur(w)) port_switch(); }
    return K_OK;
}
static krc_t task_delay(int ticks) {                   /* vTaskDelay */
    if (!api_guard("task_delay", true)) return K_ERR_ISR;
    if (ticks <= 0) { port_switch(); return K_OK; }
    task_block_cur(NULL, ticks);
    return K_BLOCKED;
}
static krc_t mutex_take(kmutex_t *m, int timeout) {    /* xSemaphoreTake(mutex) — 주인은 "태스크" 여야 한다 */
    if (!api_guard("mutex_take", true)) return K_ERR_ISR;
    if (m->owner < 0) { m->owner = K.cur; return K_OK; }
    if (timeout == 0) return K_EMPTY;
    task_block_cur(m, timeout);
    return K_BLOCKED;
}

/* ---- TODO 2: xSemaphoreGiveFromISR ----
 * 꽉 찼으면 아무것도 안 바꾸고 false (errQUEUE_FULL). 아니면 count++, 대기자(best_waiter)가 있으면 깨운다.
 * 깨운 태스크가 끼어든 태스크(K.cur)보다 높으면 *higher_woken = true 그리고 K.yield_missed = true.
 * *higher_woken 을 false 로 되돌리지 않는다. 절대 여기서 스위치하지 않는다 (ISR 이 아직 실행 중). */
static bool sem_give_from_isr(ksem_t *s, bool *higher_woken) {
    /* portASSERT_IF_INTERRUPT_PRIORITY_INVALID(): 커널보다 높은 IRQ 는 FromISR 도 부르면 안 된다 */
    if (K.in_isr && !KERNEL_ASSERT(isr_cur_hwprio() <= g_syscall_max, "FromISR API from IRQ prio %d > syscall max %d",
                                   isr_cur_hwprio(), g_syscall_max)) return false;
    if (s->count >= s->max) return false;
    s->count++;
    int w = best_waiter(s);
    if (w >= 0) {
        task_unblock(w);                          /* READY 로만 — 깨어난 태스크가 다시 take 해서 가져간다 */
        if (higher_than_cur(w)) { *higher_woken = true; K.yield_missed = true; }
    }
    return true;
}

/* ---- TODO 4: tick ISR 의 커널 부분 (xTaskIncrementTick) ----
 * 지연/타임아웃이 끝난 태스크 깨우기는 주어진 코드. 반환값 = "이 tick ISR 끝에서 스위치가 필요한가":
 * 깨어난 태스크 중 끼어든 태스크보다 높은 게 있거나, 누가 yield 를 놓쳤으면(K.yield_missed) true. */
static bool kernel_tick(void) {
    K.tick++;
    bool higher = false;
    for (int i = 0; i < K.nt; i++) {
        tcb_t *t = &K.t[i];
        if (t->state == TASK_BLOCKED && t->wake_tick != NO_WAKE && t->wake_tick <= K.tick) {
            task_unblock(i);
            if (higher_than_cur(i)) higher = true;
        }
    }
    return higher || K.yield_missed;              /* xYieldPendings 가 woken 을 버린 ISR 을 구한다 */
}

/* ======================= 인터럽트 컨트롤러 + step 시뮬레이터 (주어진 코드) ======================= */
typedef void (*isr_body_t)(int k, bool *woken);       /* k = ISR 안에서 몇 번째 step. woken = portYIELD_FROM_ISR 인자 */
typedef struct {
    const char *name;
    int hwprio, len;
    isr_body_t body;
    bool pending;
    int raised_at, taken, lost, lat_max, last_lat;
} irq_t;
static irq_t g_irq[MAXIRQ];
static int g_nirq, g_tick_irq;
static struct { int irq, k; bool woken; } g_frame[MAXIRQ];
static int g_nf;                                      /* 시뮬레이터가 보는 ISR 스택 깊이 (K.nest 와 별도) */
static void (*g_script)(int step);
static char g_row_irq[MAXIRQ][TL + 1], g_row_task[MAXT][TL + 1];

static void sim_reset(void) {
    kernel_reset();
    g_syscall_max = 100; g_guard_on = true; g_naive_exit = false;
    memset(g_irq, 0, sizeof g_irq); g_nirq = 0; g_tick_irq = -1; g_nf = 0; g_script = NULL;
    memset(g_row_irq, 0, sizeof g_row_irq); memset(g_row_task, 0, sizeof g_row_task);
}
static int irq_add(const char *name, int hwprio, int len, isr_body_t body) {
    irq_t *q = &g_irq[g_nirq];
    memset(q, 0, sizeof *q);
    q->name = name; q->hwprio = hwprio; q->len = len; q->body = body; q->last_lat = -1;
    return g_nirq++;
}
static void irq_raise(int i) {                        /* 주변장치가 인터럽트 라인을 올림 */
    if (g_irq[i].pending) { g_irq[i].lost++; return; }    /* 아직 처리 안 됐는데 또 → 합쳐짐 (UART 면 overrun) */
    g_irq[i].pending = true; g_irq[i].raised_at = K.step;
}
static int isr_cur_hwprio(void) { return g_nf ? g_irq[g_frame[g_nf - 1].irq].hwprio : -1; }
static void tick_body(int k, bool *woken) { (void)k; *woken = kernel_tick(); }   /* SysTick_Handler */

static void sim_step(void) {
    int s = K.step;
    if (g_tick_irq >= 0 && s % TICK == 0) irq_raise(g_tick_irq);
    if (g_script) g_script(s);
    int act = g_nf ? g_irq[g_frame[g_nf - 1].irq].hwprio : -1;
    int best = -1;
    for (int i = 0; i < g_nirq; i++)
        if (g_irq[i].pending && g_irq[i].hwprio > act && !(K.crit && g_irq[i].hwprio <= g_syscall_max) && (best < 0 || g_irq[i].hwprio > g_irq[best].hwprio)) best = i;
    if (best >= 0 && g_nf < MAXIRQ) {                 /* 진입 (더 높은 것만 중첩. 낮거나 같으면 pending 으로 대기) */
        irq_t *q = &g_irq[best];
        q->pending = false; q->taken++;
        q->last_lat = s - q->raised_at;
        if (q->last_lat > q->lat_max) q->lat_max = q->last_lat;
        g_frame[g_nf].irq = best; g_frame[g_nf].k = 0; g_frame[g_nf].woken = false; g_nf++;
        isr_enter();
    }
    int ran_irq = -1;
    g_worker = -1;
    if (g_nf > 0) {                                   /* ISR 이 CPU 를 쓴다 — 태스크는 아무도 못 돈다 */
        int fi = g_nf - 1;
        ran_irq = g_frame[fi].irq;
        g_irq[ran_irq].body(g_frame[fi].k, &g_frame[fi].woken);
        if (++g_frame[fi].k >= g_irq[ran_irq].len) { bool w = g_frame[fi].woken; g_nf--; isr_exit(w); }
    } else {
        if (K.cur >= 0 && K.t[K.cur].state != TASK_READY) K.cur = sched_pick_next();   /* (고장 난 커널 대비) */
        g_step_used = false;
        for (int guard = 0; K.cur >= 0 && guard < MAXT; guard++) {
            int c = K.cur;
            K.t[c].step(c);
            if (K.cur == c || g_step_used) break;     /* 일 없이 막혔으면 같은 step 을 다음 태스크가 */
        }
    }
    if (s < TL) {
        for (int i = 0; i < g_nirq; i++) {
            bool stacked = false;
            for (int f = 0; f < g_nf; f++) if (g_frame[f].irq == i) stacked = true;
            g_row_irq[i][s] = i == ran_irq ? 'I' : stacked ? 'i' : g_irq[i].pending ? 'p' : '.';
        }
        for (int i = 0; i < K.nt; i++)
            g_row_task[i][s] = i == g_worker ? 'R' : K.t[i].state == TASK_BLOCKED ? 'B' : 'r';
    }
    K.step++;
}
static void sim_run(int nsteps) {
    if (K.cur < 0) K.cur = sched_pick_next();
    for (int i = 0; i < nsteps; i++) sim_step();
}
static void print_timeline(const char *title, int from, int to) {
    if (!g_verbose) return;
    if (to > TL) to = TL;
    printf("  -- %s  (step %d..%d, I=ISR 실행 i=중첩돼 멈춤 p=pending | R=실행 r=READY B=막힘)\n  step : ", title, from, to - 1);
    for (int t = from; t < to; t++) putchar(t % 10 == 0 ? (char)('0' + (t / 10) % 10) : ' ');
    printf("\n         ");
    for (int t = from; t < to; t++) putchar((char)('0' + t % 10));
    for (int i = g_nirq - 1; i >= 0; i--) {
        printf("\n  %-5s: ", g_irq[i].name);
        for (int t = from; t < to; t++) putchar(g_row_irq[i][t]);
    }
    for (int i = K.nt - 1; i >= 0; i--) {
        printf("\n  %-5s: ", K.t[i].name);
        for (int t = from; t < to; t++) putchar(g_row_task[i][t]);
    }
    printf("\n");
}

static void bg_step(int id) { (void)id; cpu_work(); }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================= 연습 1 — 가드가 없으면 누가 막히나 ======================= */
static void ex1(void) {
    printf("[연습 1] ISR 에서 sem_take(FOREVER): 가드 없음 vs 있음\n");
    ksem_t s = { 0, 1 };
    sim_reset();
    int bg = task_add("bg", 1, bg_step);
    K.cur = bg;
    g_guard_on = false; g_viol = 0;
    isr_enter();
    krc_t rc = sem_take(&s, FOREVER);             /* ISR 이 "데이터 올 때까지 기다리겠다" */
    isr_exit(false);
    printf("  guard off: rc=%d (K_BLOCKED=%d), bg.state=%s, wait_obj=%s, K.cur=%s, asserts=%d (\"%s\")\n",
           rc, K_BLOCKED, K.t[bg].state == TASK_BLOCKED ? "BLOCKED" : "READY",
           K.t[bg].wait_obj == &s ? "&s" : "NULL", K.cur == bg ? "bg" : "?", g_viol, g_kassert_msg);
    CHECK(rc == K_BLOCKED);
    CHECK(K.t[bg].state == TASK_BLOCKED && K.t[bg].wait_obj == &s);   /* 끼어든 bg 가 대신 막혔다 */
    CHECK(g_viol == 1 && strstr(g_kassert_msg, "context switch") != NULL);   /* ISR 안 스위치는 거절 */
    CHECK(K.cur == bg);                           /* ISR 은 BLOCKED 상태인 bg 로 "복귀" — 커널 상태가 모순 */
    sim_reset();
    bg = task_add("bg", 1, bg_step);
    K.cur = bg; g_viol = 0;
    kmutex_t m = { -1 };
    isr_enter();
    rc = sem_take(&s, FOREVER);
    krc_t rm = mutex_take(&m, 0), rg = sem_give(&s);
    isr_exit(false);
    CHECK(rc == K_ERR_ISR && K.t[bg].state == TASK_READY && K.cur == bg);
    CHECK(rm == K_ERR_ISR && rg == K_ERR_ISR && m.owner == -1 && g_viol == 3);   /* 가드 켬: 3번 다 막고 셌다 */
}

/* ======================= 연습 2 — deferred function call ======================= */
typedef void (*pend_fn_t)(void *p1, uint32_t p2);
static struct { pend_fn_t fn; void *p1; uint32_t p2; } g_dq[4];   /* xTimerQueue (configTIMER_QUEUE_LENGTH = 4) */
static unsigned g_dqh, g_dqt;
static ksem_t g_dsem;
static bool pend_call_from_isr(pend_fn_t fn, void *p1, uint32_t p2, bool *woken) {
    if (g_dqh - g_dqt >= 4) return false;         /* xQueueSendFromISR 실패 → pdFAIL */
    g_dq[g_dqh % 4].fn = fn; g_dq[g_dqh % 4].p1 = p1; g_dq[g_dqh % 4].p2 = p2; g_dqh++;
    return sem_give_from_isr(&g_dsem, woken);
}
static void daemon_step(int id) {                 /* "Tmr Svc" 태스크: 큐에서 꺼내 콜백 실행 (콜백 1개 = 1 step) */
    (void)id;
    if (sem_take(&g_dsem, FOREVER) != K_OK) return;
    unsigned i = g_dqt++ % 4;
    cpu_work();
    g_dq[i].fn(g_dq[i].p1, g_dq[i].p2);
}
static int g_cb_step[8], g_cb_arg[8], g_ncb;
static void cb_record(void *p1, uint32_t p2) {
    (void)p1;
    if (g_ncb < 8) { g_cb_step[g_ncb] = K.step; g_cb_arg[g_ncb] = (int)p2; g_ncb++; }
}
static int g_pend_n, g_pend_ok, g_logger_left;
static void dfc_isr(int k, bool *woken) {         /* 2 step ISR: 마지막 step 에 콜백을 g_pend_n 개 예약 */
    if (k != 1) return;
    bool w = false;
    for (int i = 0; i < g_pend_n; i++) if (pend_call_from_isr(cb_record, NULL, (uint32_t)(i + 1), &w)) g_pend_ok++;
    *woken = w;
}
static void logger_step(int id) {                 /* prio 3: 처음 30 step 동안 바쁜 로거, 그다음 영원히 쉰다 */
    (void)id;
    if (g_logger_left == 0) { task_delay(1000); return; }
    cpu_work(); g_logger_left--;
}
static int g_dfc_irq;
static void dfc_script(int s) { if (s == 21) irq_raise(g_dfc_irq); }
static void run_dfc(int daemon_prio, int npend, bool logger) {
    sim_reset();
    g_dqh = g_dqt = 0; g_dsem.count = 0; g_dsem.max = 4;
    g_ncb = 0; g_pend_n = npend; g_pend_ok = 0; g_logger_left = logger ? 30 : 0;
    task_add("bg", 1, bg_step);
    if (logger) task_add("log", 3, logger_step);
    task_add("Tmr", (uint8_t)daemon_prio, daemon_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_dfc_irq = irq_add("DEV", 2, 2, dfc_isr);
    g_script = dfc_script;
    sim_run(60);
}
static void ex2(void) {
    printf("[연습 2] xTimerPendFunctionCallFromISR 모델: 콜백은 daemon 태스크에서\n");
    run_dfc(5, 1, true);
    printf("  daemon prio 5 (> logger 3): IRQ@21 → callback at step %d (latency %d)\n", g_cb_step[0], g_cb_step[0] - 21);
    CHECK(g_ncb == 1 && g_cb_step[0] == 23);      /* 최고 prio daemon = yield 한 핸들러 태스크와 같은 2 step */
    run_dfc(2, 1, true);
    print_timeline("daemon prio 2 < busy logger prio 3", 18, 42);
    printf("  daemon prio 2 (< logger 3): IRQ@21 → callback at step %d (latency %d)\n", g_cb_step[0], g_cb_step[0] - 21);
    CHECK(g_ncb == 1 && g_cb_step[0] == 36);      /* 로거가 30 step 을 다 쓸 때까지 (configTIMER_TASK_PRIORITY 함정) */
    run_dfc(5, 5, false);
    printf("  5 calls pended in one ISR, queue length 4: ok=%d, callbacks run=%d, args=%d %d %d %d\n",
           g_pend_ok, g_ncb, g_cb_arg[0], g_cb_arg[1], g_cb_arg[2], g_cb_arg[3]);
    CHECK(g_pend_ok == 4 && g_ncb == 4);          /* 큐가 차면 FromISR 는 기다리지 않고 실패 */
    CHECK(g_cb_arg[0] == 1 && g_cb_arg[3] == 4 && g_cb_step[0] == 23 && g_cb_step[3] == 26);   /* FIFO, 1 step 씩 */
}

/* ======================= 연습 3 — 지연 공식 vs 시뮬레이터 ======================= */
static int g_L, g_yield, g_ev_step, g_sens_irq, g_h_left, g_h_first;
static ksem_t g_evt;
static void sens3(int k, bool *woken) {
    if (k != g_L - 1) return;
    bool w = false;
    sem_give_from_isr(&g_evt, &w);
    if (g_yield) *woken = w;
}
static void h3_step(int id) {
    (void)id;
    if (g_h_left == 0) {
        if (sem_take(&g_evt, FOREVER) != K_OK) return;
        if (g_h_first < 0) g_h_first = K.step;
        g_h_left = 3;
    }
    cpu_work(); g_h_left--;
}
static void s3_script(int s) { if (s == g_ev_step) irq_raise(g_sens_irq); }
static int sim_latency(int L, bool yield, int s) {
    sim_reset();
    g_L = L; g_yield = yield; g_ev_step = s; g_h_left = 0; g_h_first = -1;
    g_evt.count = 0; g_evt.max = 1;
    task_add("bg", 1, bg_step);
    task_add("H", 3, h3_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_sens_irq = irq_add("SENS", 2, L, sens3);
    g_script = s3_script;
    sim_run(s + L + 2 * TICK);
    return g_h_first - s;
}
static bool tick_in(int a, int b) { return (b / TICK) * TICK >= a; }   /* [a, b] 안에 TICK 배수(tick IRQ)가 있나 */
static int predict_yield(int L, int s) { return L + (tick_in(s, s + L) ? 1 : 0); }
static int predict_noyield(int L, int s) {
    int e = s + L;                                /* ISR 이 끝난 다음 step */
    if (tick_in(s, e)) return L + 1;              /* tick 이 ISR 중/직후에 걸려 있다 → 그 tick ISR 끝에서 */
    int T = (e / TICK + 1) * TICK;                /* 다음 tick */
    return T + 1 - s;
}
static void ex3(void) {
    printf("[연습 3] 닫힌 식 vs 시뮬레이터 (IRQ 위상 s = 11..90, ISR 길이 L = 2, 5)\n");
    for (int L = 2; L <= 5; L += 3) {
        int mism = 0, maxy = 0, maxn = 0, miny = 99, minn = 99, sumn = 0;
        for (int s = 11; s <= 90; s++) {
            int y = sim_latency(L, true, s), n = sim_latency(L, false, s);
            if (y != predict_yield(L, s) || n != predict_noyield(L, s)) mism++;
            if (y > maxy) maxy = y;
            if (y < miny) miny = y;
            if (n > maxn) maxn = n;
            if (n < minn) minn = n;
            sumn += n;
        }
        printf("  L=%d: mismatches=%d  yield min/max %d/%d   no-yield min/max %d/%d avg %.2f\n",
               L, mism, miny, maxy, minn, maxn, sumn / 80.0);
        CHECK(mism == 0);
        CHECK(miny == L && maxy == L + 1);        /* yield: ISR 길이 (+ 겹친 tick ISR 1) */
        CHECK(minn == L + 1 && maxn == TICK);     /* no-yield: 최악 = tick 주기 (L < TICK 일 때) */
    }
}

/* ======================= 연습 4 — configMAX_SYSCALL_INTERRUPT_PRIORITY ======================= */
static int g_fault, g_swi, g_uart4, g_pwm_off = -1, g_h4_first = -1, g_wrong;
static ksem_t g_s4;
static void bg_crit_step(int id) {                /* 11..15 step 동안 임계 구역 (taskENTER_CRITICAL) */
    (void)id;
    cpu_work();
    if (K.step == 11) K.crit = true;
    if (K.step == 15) K.crit = false;             /* 이 step 끝에서 taskEXIT_CRITICAL */
}
static void h4_step(int id) {
    (void)id;
    if (sem_take(&g_s4, FOREVER) != K_OK) return;
    if (g_h4_first < 0) g_h4_first = K.step;
    cpu_work();
}
static void fault_isr(int k, bool *woken) {       /* 모터 과전류 비교기 → PWM 즉시 차단. hwprio 6 > syscall max */
    (void)k;
    g_pwm_off = K.step;                           /* TIM1->BDTR MOE = 0 같은 레지스터 한 줄 */
    if (g_wrong) { bool w = false; sem_give_from_isr(&g_s4, &w); *woken = w; }   /* 금지: 커널 API */
    else irq_raise(g_swi);                        /* NVIC_SetPendingIRQ(SWI) — 커널 일은 낮은 IRQ 에게 */
}
static void swi_isr(int k, bool *woken) { (void)k; bool w = false; sem_give_from_isr(&g_s4, &w); *woken = w; }
static void uart4(int k, bool *woken) { (void)k; (void)woken; }
static void s4_script(int s) {
    if (s == 12) irq_raise(g_uart4);
    if (s == 13) irq_raise(g_fault);
}
static void run4(bool wrong) {
    sim_reset();
    g_syscall_max = 4; g_wrong = wrong; g_pwm_off = -1; g_h4_first = -1;
    g_s4.count = 0; g_s4.max = 1;
    task_add("bg", 1, bg_crit_step);
    task_add("H", 3, h4_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_uart4 = irq_add("UART", 1, 1, uart4);
    g_swi = irq_add("SWI", 2, 1, swi_isr);
    g_fault = irq_add("FAULT", 6, 1, fault_isr);
    g_script = s4_script;
    sim_run(30);
}
static void ex4(void) {
    printf("[연습 4] syscall max = 4: 임계 구역은 hwprio <= 4 만 가린다, 그 위 IRQ 는 커널 API 금지\n");
    g_viol = 0;
    run4(false);
    print_timeline("FAULT(6) → SWI(2) → H, bg critical 11..15", 9, 22);
    printf("  right: FAULT lat %d (pwm off @%d), UART lat %d, SWI lat %d, H first @%d, asserts %d\n",
           g_irq[g_fault].last_lat, g_pwm_off, g_irq[g_uart4].last_lat, g_irq[g_swi].last_lat, g_h4_first, g_viol);
    CHECK(g_irq[g_fault].last_lat == 0 && g_pwm_off == 13);   /* 임계 구역 안에서도 바로 */
    CHECK(g_irq[g_uart4].last_lat == 5 && g_irq[g_swi].last_lat == 3);   /* 가려진 IRQ 는 16 에 */
    CHECK(g_h4_first == 18 && g_viol == 0);
    run4(true);
    printf("  wrong: FAULT calls sem_give_from_isr → asserts %d (\"%s\"), H first @%d\n", g_viol, g_kassert_msg, g_h4_first);
    CHECK(g_viol == 1 && strstr(g_kassert_msg, "syscall max") != NULL);
    CHECK(g_h4_first == -1 && g_s4.count == 0u);  /* give 는 거절됐다 (실제 칩이면 커널 자료구조가 깨질 수 있는 자리) */
}

/* ======================= 연습 5 — ISR 폭풍과 PendSV 1번 ======================= */
static ksem_t g_s5[3];
static int g_first5[3], g_left5[3];
static void storm_isr(int which, int k, bool *woken) {
    if (k != 0) return;
    bool w = false;
    sem_give_from_isr(&g_s5[which], &w);
    *woken = w;
}
static void a5(int k, bool *w) { storm_isr(0, k, w); }
static void b5(int k, bool *w) { storm_isr(1, k, w); }
static void c5(int k, bool *w) { storm_isr(2, k, w); }
static void t5(int which) {
    if (g_left5[which] == 0) {
        if (sem_take(&g_s5[which], FOREVER) != K_OK) return;
        if (g_first5[which] < 0) g_first5[which] = K.step;
        g_left5[which] = 1;
    }
    cpu_work(); g_left5[which]--;
}
static void t2s(int id) { (void)id; t5(0); }
static void t3s(int id) { (void)id; t5(1); }
static void t4s(int id) { (void)id; t5(2); }
static int g_ia, g_ib, g_ic;
static void s5_script(int s) {
    if (s == 11) irq_raise(g_ia);
    if (s == 12) irq_raise(g_ib);
    if (s == 13) irq_raise(g_ic);
}
static int run5(bool naive) {                     /* 반환: step 11..16 동안의 스위치 수 */
    sim_reset();
    g_naive_exit = naive;
    for (int i = 0; i < 3; i++) { g_s5[i].count = 0; g_s5[i].max = 1; g_first5[i] = -1; g_left5[i] = 0; }
    task_add("bg", 1, bg_step);
    task_add("T2", 2, t2s); task_add("T3", 3, t3s); task_add("T4", 4, t4s);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_ia = irq_add("A", 2, 3, a5); g_ib = irq_add("B", 3, 2, b5); g_ic = irq_add("C", 4, 1, c5);
    g_script = s5_script;
    sim_run(11);
    int sw0 = K.nswitch;
    sim_run(6);                                   /* step 11..16: A,B,C 중첩 (A 는 11, 15, 16) */
    int sw = K.nswitch - sw0;
    sim_run(9);
    return sw;
}
static void ex5(void) {
    printf("[연습 5] 3중 중첩 ISR 이 각각 더 높은 태스크를 깨운다\n");
    g_viol = 0;
    int sw = run5(false);
    print_timeline("A(2)@11 len3, B(3)@12 len2, C(4)@13 len1", 10, 21);
    printf("  correct port: switches during 11..16 = %d, T4/T3/T2 first run @%d/%d/%d, asserts %d\n",
           sw, g_first5[2], g_first5[1], g_first5[0], g_viol);
    CHECK(sw == 1 && g_viol == 0);                /* yield 요청 3개 → 가장 바깥 A 가 끝날 때 PendSV 1번 */
    CHECK(g_first5[2] == 17 && g_first5[1] == 18 && g_first5[0] == 19);   /* 가장 높은 T4 부터 */
    sw = run5(true);
    printf("  naive port (switch on every yield request): asserts %d, switches during 11..16 = %d\n", g_viol, sw);
    CHECK(g_viol == 3 && sw == 1);                /* ISR 안 스위치 시도 3번 (모두 거절), 실제 스위치는 여전히 1번 */
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    g_kassert_hook = kassert_count;
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
