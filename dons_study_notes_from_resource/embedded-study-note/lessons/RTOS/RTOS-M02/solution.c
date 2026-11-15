/* RTOS-M02 ISR 지연 처리: FromISR API와 yield 요청
 *
 * 호스트 모델: 단일 OS 스레드에서 도는 "step 단위 결정적 미니 커널 + 인터럽트 컨트롤러" 시뮬레이터.
 *   - 시간 단위 = step. OS tick 1개 = TICK(10) step. (1 kHz tick 이면 1 step = 100 us 쯤으로 생각)
 *   - IRQ 는 하드웨어 우선순위(hwprio, 클수록 급함)와 길이(len step)와 몸체(body)를 가진다.
 *     SysTick(틱 ISR)도 IRQ 하나: hwprio 0(최저, FreeRTOS Cortex-M 포트와 같다), 길이 1, 몸체 = kernel_tick().
 *   - 매 step: ① 각본대로 IRQ 를 pending 으로 ② pending 중 "지금 실행 중인 ISR 보다 높은" 것 하나 진입(중첩)
 *     ③ ISR 이 있으면 맨 위 ISR 이 1 step 실행 — 마지막 step 끝에 isr_exit(woken)
 *        ISR 이 없으면 현재 태스크가 1 step (일 1단위). 커널 호출·컨텍스트 스위치는 0 step.
 *   - 태스크가 일을 안 하고 막히면 같은 step 을 다음 태스크가 이어 받는다.
 * 대응: isr_enter/isr_exit = 포트의 ISR 입출구(+ portYIELD_FROM_ISR), sem_give_from_isr = xSemaphoreGiveFromISR,
 *       K.pendsv = SCB->ICSR PENDSVSET, K.yield_missed = xYieldPendings[0], api_guard = 포트의 "ISR 에서 부르면 assert".
 * TODO 1 isr_enter/isr_exit  2 sem_give_from_isr  3 api_guard  4 kernel_tick 의 반환값  5 dma_isr_deferred
 * 실행: ./solution      (표만)   ./solution -v   (타임라인까지)
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
    int nswitch;                  /* 컨텍스트 스위치 횟수 */
    uint32_t seq;
} K;
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
static void tick_body(int k, bool *woken) { (void)k; *woken = kernel_tick(); }   /* SysTick_Handler */

static void sim_step(void) {
    int s = K.step;
    if (g_tick_irq >= 0 && s % TICK == 0) irq_raise(g_tick_irq);
    if (g_script) g_script(s);
    int act = g_nf ? g_irq[g_frame[g_nf - 1].irq].hwprio : -1;
    int best = -1;
    for (int i = 0; i < g_nirq; i++)
        if (g_irq[i].pending && g_irq[i].hwprio > act && (best < 0 || g_irq[i].hwprio > g_irq[best].hwprio)) best = i;
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

/* ======================= Part B: IRQ → 처리 태스크 지연 측정 ======================= */
enum { D_YIELD, D_NOYIELD, D_INISR };
enum { SENS_LEN = 2, W = 6 };                         /* ISR 프롤로그 2 step (플래그 클리어·레지스터 읽기), 처리 6 step */
static const char *const DNAME[] = { "yield at exit", "no yield", "all work in ISR" };
static int g_design, g_evt_step, g_sens;
static ksem_t g_evt;
static struct { int left, first_run, done; } g_h;     /* 처리 태스크 H */
static struct { int off_max, runs; } g_ctrl;          /* 제어 태스크: tick 경계부터 몇 step 뒤에 시작했나 */
static int g_isr_done = -1;

static void sens_body(int k, bool *woken) {           /* 센서 data-ready ISR (예: IMU INT1 → EXTI) */
    if (g_design == D_INISR) {                        /* 처리까지 ISR 안에서: 길이 SENS_LEN + W */
        if (k == SENS_LEN + W - 1) g_isr_done = K.step;
        return;
    }
    if (k == SENS_LEN - 1) {                          /* ISR 마지막 줄들 */
        bool w = false;                               /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
        sem_give_from_isr(&g_evt, &w);                /* xSemaphoreGiveFromISR(evt, &w); */
        if (g_design == D_YIELD) *woken = w;          /* portYIELD_FROM_ISR(w);  — D_NOYIELD 는 이 줄을 빼먹었다 */
    }
}
static void uart_body(int k, bool *woken) { (void)k; (void)woken; }   /* 피해자: DR 1바이트 읽기만 */
static void h_step(int id) {
    (void)id;
    if (g_h.left == 0) {
        if (sem_take(&g_evt, FOREVER) != K_OK) return;   /* 막힘: 일 안 함 */
        if (g_h.first_run < 0) g_h.first_run = K.step;   /* 핸들러 태스크의 첫 명령 */
        g_h.left = W;
    }
    cpu_work();
    if (--g_h.left == 0) g_h.done = K.step;
}
static void bg_step(int id) { (void)id; cpu_work(); }
static void ctrl_step(int id) {                       /* 1 tick 마다 1 step 짜리 제어 루프 */
    (void)id;
    cpu_work();
    int off = K.step % TICK;
    if (off > g_ctrl.off_max) g_ctrl.off_max = off;
    g_ctrl.runs++;
    task_delay(1);
}
static int g_uart;
static void lat_script(int s) {
    if (s == g_evt_step) irq_raise(g_sens);
    if (g_uart >= 0 && s == g_evt_step + 1) irq_raise(g_uart);
}
typedef struct { int first_run, done, uart_lat, tick_lat_max, ctrl_off_max; } lat_t;
static lat_t run_latency(int design, int evt_step, bool victims) {
    sim_reset();
    g_design = design; g_evt_step = evt_step; g_isr_done = -1;
    memset(&g_h, 0, sizeof g_h); g_h.first_run = -1; g_h.done = -1;
    memset(&g_ctrl, 0, sizeof g_ctrl);
    g_evt.count = 0; g_evt.max = 1;                   /* 바이너리 세마포어 */
    task_add("bg", 1, bg_step);
    task_add("H", 3, h_step);
    if (victims) task_add("ctrl", 4, ctrl_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_uart = victims ? irq_add("UART", 1, 1, uart_body) : -1;
    g_sens = irq_add("SENS", 2, design == D_INISR ? SENS_LEN + W : SENS_LEN, sens_body);
    g_script = lat_script;
    sim_run(evt_step + 25);
    lat_t r = { design == D_INISR ? evt_step + SENS_LEN : g_h.first_run,
                design == D_INISR ? g_isr_done : g_h.done,
                g_uart >= 0 ? g_irq[g_uart].last_lat : -1, g_irq[g_tick_irq].lat_max, g_ctrl.off_max };
    return r;
}

/* ---- 중첩 ISR: 안쪽 ISR 이 yield 를 요청해도 스위치는 가장 바깥 ISR 이 끝날 때 ---- */
static ksem_t g_sa, g_sb;
static int g_nest_seen, g_first_h2 = -1, g_switch_at_inner_exit;
static void outer_body(int k, bool *woken) {          /* 낮은 IRQ (hwprio 2, 4 step): k=0 에 A 를 give */
    if (K.nest > g_nest_seen) g_nest_seen = K.nest;
    if (k == 0) { bool w = false; sem_give_from_isr(&g_sa, &w); *woken = *woken || w; }
}
static void inner_body(int k, bool *woken) {          /* 높은 IRQ (hwprio 3, 1 step): B 를 give + yield 요청 */
    (void)k;
    if (K.nest > g_nest_seen) g_nest_seen = K.nest;
    bool w = false;
    sem_give_from_isr(&g_sb, &w);
    *woken = w;
}
static struct { int left; } g_n1, g_n2;
static void n_step_generic(ksem_t *s, int *left, int *first, int id) {
    (void)id;
    if (*left == 0) {
        if (sem_take(s, FOREVER) != K_OK) return;
        if (*first < 0) *first = K.step;
        *left = 2;
    }
    cpu_work();
    (*left)--;
}
static int g_first_h1 = -1;
static void n1_step(int id) { n_step_generic(&g_sa, &g_n1.left, &g_first_h1, id); }
static void n2_step(int id) { n_step_generic(&g_sb, &g_n2.left, &g_first_h2, id); }
static int g_outer, g_inner;
static void nest_script(int s) {
    if (s == 12) irq_raise(g_outer);
    if (s == 13) irq_raise(g_inner);
    if (s == 14 && K.cur == 2) g_switch_at_inner_exit = 1;    /* 바깥 ISR 이 아직인데 H2 가 cur 가 됐나 */
}

/* ======================= Part C: ADC 이중 버퍼 DMA (HT/TC) ISR → 필터 태스크 ======================= */
enum { HALF = 20, DMA_OFF = 5, WF = 8, UART_PERIOD = 4, NSTEP_C = 110 };
enum { C_INISR, C_DEFER };
static struct { int half, ready; } g_blk[4];          /* ISR → 태스크 블록 디스크립터 링 (SPSC) */
static unsigned g_bh, g_bt;
static ksem_t g_blk_sem;                              /* 카운팅 세마포어 max 2 = 링에 쌓인 블록 수 */
static struct { int dma_n, ok, late, dropped, resp_max, left, half, ready; } g_c;
static int g_dma;

static bool blk_push(int half, int ready) {
    if (g_bh - g_bt >= 4) return false;
    g_blk[g_bh % 4].half = half; g_blk[g_bh % 4].ready = ready; g_bh++;
    return true;
}
static bool blk_pop(int *half, int *ready) {
    if (g_bh == g_bt) return false;
    *half = g_blk[g_bt % 4].half; *ready = g_blk[g_bt % 4].ready; g_bt++;
    return true;
}
static int dma_ack(void) { return g_c.dma_n++ % 2; }  /* DMA1->IFCR 에 HTIF/TCIF 클리어. HT → 0번 절반, TC → 1번 절반 */
static void block_finished(int ready) {               /* 절반 하나 처리 끝: DMA 가 그 절반을 다시 덮기 전이었나 */
    int resp = K.step - ready + 1;
    if (resp > g_c.resp_max) g_c.resp_max = resp;
    if (K.step < ready + HALF) g_c.ok++; else g_c.late++;
}

static void dma_isr_inisr(int k, bool *woken) {       /* 비교용 (주어진 코드): 필터를 ISR 안에서 */
    (void)woken;
    static int ready;
    if (k == 0) { dma_ack(); ready = K.step; }
    if (k == WF) block_finished(ready);
}

/* ---- TODO 5: DMA HT/TC ISR — 지연 처리 버전 (길이 1 step) ----
 * dma_ack() 으로 어느 절반인지 받고, blk_push(half, K.step) 로 디스크립터를 넘긴다 (꽉 차면 g_c.dropped++ 하고 끝).
 * 그 다음 g_blk_sem 을 give 하고, 받은 woken 을 *woken 에 넣는다 (= portYIELD_FROM_ISR(woken)). */
static void dma_isr_deferred(int k, bool *woken) {
    (void)k;
    int half = dma_ack();
    if (!blk_push(half, K.step)) { g_c.dropped++; return; }
    bool w = false;
    sem_give_from_isr(&g_blk_sem, &w);
    *woken = w;
}

static void f_step(int id) {                          /* 필터 태스크 (prio 3): 블록 하나에 WF step */
    (void)id;
    if (g_c.left == 0) {
        if (sem_take(&g_blk_sem, FOREVER) != K_OK) return;
        if (!blk_pop(&g_c.half, &g_c.ready)) return;
        g_c.left = WF;
    }
    cpu_work();
    if (--g_c.left == 0) block_finished(g_c.ready);
}
static void c_script(int s) {
    if (s % HALF == DMA_OFF) irq_raise(g_dma);
    if (s % UART_PERIOD == 1) irq_raise(g_uart);      /* UART 바이트가 4 step 마다 — DR 1칸, 안 읽으면 overrun */
}
typedef struct { int ok, late, dropped, uart_overrun, uart_lat_max, tick_lat_max, ctrl_off_max, resp_max; } c_res_t;
static c_res_t run_dma(int design) {
    sim_reset();
    memset(&g_c, 0, sizeof g_c); memset(&g_ctrl, 0, sizeof g_ctrl);
    g_bh = g_bt = 0;
    g_blk_sem.count = 0; g_blk_sem.max = 2;
    task_add("bg", 1, bg_step);
    task_add("F", 3, f_step);
    task_add("ctrl", 4, ctrl_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_uart = irq_add("UART", 1, 1, uart_body);
    g_dma = design == C_INISR ? irq_add("DMA", 2, 1 + WF, dma_isr_inisr) : irq_add("DMA", 2, 1, dma_isr_deferred);
    g_script = c_script;
    sim_run(NSTEP_C);
    c_res_t r = { g_c.ok, g_c.late, g_c.dropped, g_irq[g_uart].lost, g_irq[g_uart].lat_max,
                  g_irq[g_tick_irq].lat_max, g_ctrl.off_max, g_c.resp_max };
    return r;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    g_kassert_hook = kassert_count;                   /* 테스트 동안은 멈추지 말고 센다 (기본은 abort) */
    bool w;
    ksem_t s;

    printf("[Part A] ISR 문맥 플래그, give_from_isr, ISR 에서 막히는 API\n");
    kernel_reset();
    int bg = task_add("bg", 1, bg_step), h = task_add("H", 3, h_step), lo = task_add("L", 0, bg_step);
    K.cur = bg;
    CHECK(!K.in_isr && K.nest == 0);
    isr_enter();
    CHECK(K.in_isr && K.nest == 1);
    isr_enter();                                      /* 더 높은 IRQ 가 끼어듦 */
    CHECK(K.in_isr && K.nest == 2);
    isr_exit(false);
    CHECK(K.in_isr && K.nest == 1);                   /* 안쪽만 끝남: 아직 ISR 문맥 */
    isr_exit(false);
    CHECK(!K.in_isr && K.nest == 0);

    s.count = 0; s.max = 1;
    K.t[h].state = TASK_BLOCKED; K.t[h].wait_obj = &s;          /* H 가 s 에서 기다리는 중, bg 실행 중 */
    int sw0 = K.nswitch;
    isr_enter();
    w = false;
    CHECK(sem_give_from_isr(&s, &w) && w && s.count == 1u);
    CHECK(K.t[h].state == TASK_READY && K.t[h].wait_obj == NULL);
    CHECK(K.cur == bg && K.nswitch == sw0);           /* ISR 안에서는 깨우기만, 스위치 없음 */
    CHECK(!sem_give_from_isr(&s, &w) && w);           /* 꽉 참 → false, 그래도 앞의 woken 은 유지 */
    isr_exit(w);
    CHECK(K.cur == h && K.nswitch == sw0 + 1 && !K.pendsv);   /* portYIELD_FROM_ISR → ISR 끝에서 정확히 1번 */

    s.count = 0; K.t[lo].state = TASK_BLOCKED; K.t[lo].wait_obj = &s;   /* L(0) 이 기다림, H(3) 실행 중 */
    isr_enter(); w = false;
    CHECK(sem_give_from_isr(&s, &w) && !w);           /* 끼어든 H 보다 낮다 → woken false */
    isr_exit(w);
    CHECK(K.cur == h && K.t[lo].state == TASK_READY && !K.yield_missed);

    s.count = 0; K.cur = bg; K.t[h].state = TASK_BLOCKED; K.t[h].wait_obj = &s;
    isr_enter(); w = false;
    sem_give_from_isr(&s, &w);
    isr_exit(false);                                  /* woken 을 버렸다 (portYIELD_FROM_ISR 생략) */
    CHECK(K.cur == bg && K.yield_missed);             /* 지금은 안 바뀜. xYieldPendings 만 남는다 */

    /* ISR 에서 막히는 API → KERNEL_ASSERT, 아무것도 바꾸지 않는다 */
    kernel_reset();
    bg = task_add("bg", 1, bg_step);
    K.cur = bg;
    kmutex_t m = { -1 };
    s.count = 0; s.max = 1;
    g_viol = 0;
    isr_enter();
    CHECK(sem_take(&s, FOREVER) == K_ERR_ISR && g_viol == 1);
    CHECK(strstr(g_kassert_msg, "sem_take") != NULL);
    CHECK(K.t[bg].state == TASK_READY && K.cur == bg); /* 끼어든 bg 가 대신 막히지 않았다 */
    CHECK(sem_take(&s, 5) == K_ERR_ISR && g_viol == 2);
    CHECK(sem_take(&s, 0) == K_EMPTY && g_viol == 2);  /* timeout 0 은 막히지 않으니 허용 (TakeFromISR 류) */
    CHECK(task_delay(3) == K_ERR_ISR && g_viol == 3 && strstr(g_kassert_msg, "task_delay") != NULL);
    CHECK(mutex_take(&m, 0) == K_ERR_ISR && m.owner == -1 && g_viol == 4);   /* 뮤텍스는 주인이 태스크여야 */
    CHECK(sem_give(&s) == K_ERR_ISR && s.count == 0u && g_viol == 5);       /* 태스크용 give 는 스스로 스위치할 수 있다 */
    w = false;
    CHECK(sem_give_from_isr(&s, &w) && s.count == 1u && g_viol == 5);       /* FromISR 판은 OK */
    isr_exit(false);
    CHECK(sem_take(&s, FOREVER) == K_OK && g_viol == 5);                    /* 태스크 문맥에선 모두 정상 */
    CHECK(task_delay(2) == K_BLOCKED && K.t[bg].state == TASK_BLOCKED && K.t[bg].wake_tick == 2 && g_viol == 5);

    printf("[Part B] IRQ → 핸들러 태스크 첫 명령까지 (step, 1 tick = %d step, ISR %d step, 처리 %d step)\n", TICK, SENS_LEN, W);
    g_viol = 0;
    int lat_a[10], lat_b[10], min_a = 99, max_a = 0, min_b = 99, max_b = 0, sum_a = 0, sum_b = 0;
    printf("  phase | (a) yield  (b) no-yield | (a) done  (b) done  (c) in-ISR done\n");
    for (int p = 0; p < 10; p++) {
        lat_t a = run_latency(D_YIELD, 20 + p, false);
        lat_t b = run_latency(D_NOYIELD, 20 + p, false);
        lat_t c = run_latency(D_INISR, 20 + p, false);
        lat_a[p] = a.first_run < 0 ? -1 : a.first_run - (20 + p);
        lat_b[p] = b.first_run < 0 ? -1 : b.first_run - (20 + p);
        if (lat_a[p] < min_a) min_a = lat_a[p];
        if (lat_a[p] > max_a) max_a = lat_a[p];
        if (lat_b[p] < min_b) min_b = lat_b[p];
        if (lat_b[p] > max_b) max_b = lat_b[p];
        sum_a += lat_a[p]; sum_b += lat_b[p];
        printf("    %d   |   %2d          %2d       |   %3d       %3d        %3d\n", p, lat_a[p], lat_b[p], a.done, b.done, c.done);
    }
    printf("  (a) min/max/sum %d/%d/%d   (b) min/max/sum %d/%d/%d\n", min_a, max_a, sum_a, min_b, max_b, sum_b);
    CHECK(min_a == SENS_LEN && max_a == SENS_LEN + 1);      /* yield: ISR 길이 (+ 같이 걸린 tick ISR 1) */
    CHECK(lat_a[1] == 2 && lat_a[0] == 3 && lat_a[9] == 3);
    CHECK(lat_b[1] == 10 && lat_b[2] == 9 && lat_b[7] == 4);  /* no yield: 다음 tick ISR 끝까지 */
    CHECK(min_b == 3 && max_b == TICK && sum_b == 58);
    run_latency(D_YIELD, 21, false);
    print_timeline("(a) yield at exit, IRQ@21", 18, 42);
    run_latency(D_NOYIELD, 21, false);
    print_timeline("(b) no yield, IRQ@21", 18, 42);
    CHECK(K.nswitch > 0 && g_viol == 0);
    lat_t c21 = run_latency(D_INISR, 21, false);
    print_timeline("(c) all work in ISR, IRQ@21", 18, 42);
    CHECK(c21.done == 28 && run_latency(D_YIELD, 21, false).done == 28);    /* 혼자일 때 (c) 와 (a) 의 완료는 같다 */

    printf("  -- 피해자 포함 (IRQ@29, UART 바이트@30, ctrl prio 4 가 매 tick)\n");
    lat_t va = run_latency(D_YIELD, 29, true);
    print_timeline("(a) yield + victims, IRQ@29", 18, 44);
    lat_t vc = run_latency(D_INISR, 29, true);
    print_timeline("(c) all work in ISR + victims, IRQ@29", 18, 44);
    printf("  design          | H start  work done | UART lat  SysTick lat max  ctrl offset max\n");
    printf("  %-15s |   %3d      %3d    |   %2d          %2d              %2d\n", DNAME[D_YIELD], va.first_run, va.done, va.uart_lat, va.tick_lat_max, va.ctrl_off_max);
    printf("  %-15s |   %3s      %3d    |   %2d          %2d              %2d\n", DNAME[D_INISR], "-", vc.done, vc.uart_lat, vc.tick_lat_max, vc.ctrl_off_max);
    CHECK(va.uart_lat == 1 && va.tick_lat_max == 2 && va.ctrl_off_max == 3);
    CHECK(vc.uart_lat == 7 && vc.tick_lat_max == 8 && vc.ctrl_off_max == 9);  /* 긴 ISR = 모두의 지터 */
    CHECK(vc.done < va.done);                                               /* 자기 일만 보면 (c) 가 더 빠르다 */

    /* 중첩: 안쪽 ISR 의 yield 요청은 가장 바깥 ISR 이 끝날 때 한 번에 */
    sim_reset();
    g_sa.count = 0; g_sa.max = 1; g_sb.count = 0; g_sb.max = 1;
    g_n1.left = g_n2.left = 0; g_first_h1 = g_first_h2 = -1; g_nest_seen = 0; g_switch_at_inner_exit = 0;
    task_add("bg", 1, bg_step);
    task_add("H1", 3, n1_step);
    task_add("H2", 5, n2_step);
    g_tick_irq = irq_add("SysTk", 0, 1, tick_body);
    g_outer = irq_add("OUTER", 2, 4, outer_body);
    g_inner = irq_add("INNER", 3, 1, inner_body);
    g_script = nest_script;
    sim_run(1);                                       /* step 0: tick ISR */
    sim_run(1);                                       /* step 1: H2 → H1 → bg 순으로 막히며 bg 가 실행 */
    int sw_before = K.nswitch;
    sim_run(28);
    print_timeline("nested: OUTER@12 (4 step), INNER@13 (1 step)", 10, 24);
    CHECK(g_nest_seen == 2 && g_switch_at_inner_exit == 0);
    CHECK(g_first_h2 == 17 && g_first_h1 == 19);      /* 바깥 ISR(12,14,15,16) 이 끝난 뒤 17 에 H2, 그다음 H1 */
    CHECK(K.nswitch - sw_before == 3 && g_viol == 0); /* bg→H2 (16 끝: yield 요청 2개가 PendSV 1번), H2→H1, H1→bg */
    CHECK(!K.in_isr && K.nest == 0 && !K.pendsv);

    printf("[Part C] ADC 이중 버퍼 DMA HT/TC ISR → 필터 태스크 (절반 %d step 마다, 처리 %d step, UART 바이트 %d step 마다)\n",
           HALF, WF, UART_PERIOD);
    g_viol = 0;
    c_res_t ci = run_dma(C_INISR);
    print_timeline("filter inside DMA ISR", 0, 50);
    c_res_t cd = run_dma(C_DEFER);
    print_timeline("deferred: DMA ISR gives, filter task", 0, 50);
    printf("  design   | blocks ok late drop | resp_max | UART overrun lat_max | SysTick lat_max | ctrl off_max\n");
    printf("  in ISR   |         %d    %d    %d |    %2d    |       %2d       %2d     |       %2d        |      %2d\n",
           ci.ok, ci.late, ci.dropped, ci.resp_max, ci.uart_overrun, ci.uart_lat_max, ci.tick_lat_max, ci.ctrl_off_max);
    printf("  deferred |         %d    %d    %d |    %2d    |       %2d       %2d     |       %2d        |      %2d\n",
           cd.ok, cd.late, cd.dropped, cd.resp_max, cd.uart_overrun, cd.uart_lat_max, cd.tick_lat_max, cd.ctrl_off_max);
    CHECK(ci.ok == 5 && ci.late == 0 && ci.resp_max == 1 + WF);     /* ISR 안 처리: 자기 블록은 늘 9 step 만에 */
    CHECK(ci.uart_overrun == 11 && ci.uart_lat_max == 9);            /* 긴 ISR 동안 UART DR 이 또 차서 overrun */
    CHECK(ci.tick_lat_max == 5 && ci.ctrl_off_max == 6);             /* tick 과 제어 루프가 밀린다 */
    CHECK(cd.ok == 5 && cd.late == 0 && cd.dropped == 0);
    CHECK(cd.uart_overrun == 0 && cd.uart_lat_max == 1 && cd.tick_lat_max == 0 && cd.ctrl_off_max == 2);
    CHECK(cd.resp_max == 15 && cd.resp_max < HALF && g_viol == 0);   /* 블록 응답은 늘지만 마감(20) 안 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
