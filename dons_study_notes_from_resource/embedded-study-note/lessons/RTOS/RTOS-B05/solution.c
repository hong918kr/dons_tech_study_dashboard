/* RTOS-B05 선점과 라운드 로빈: SIGALRM tick 시뮬레이션
 *
 * BUILD: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (starter.c 도 같은 명령)
 *   - 첫 줄의 #define _XOPEN_SOURCE 600 은 macOS <ucontext.h> 가 요구한다 (없으면 #error).
 *   - ucontext 함수는 macOS 에서 deprecated → 그 경고 하나만 #pragma 로 끈다 (-Werror 는 그대로).
 *
 * 미니 커널 (RTOS-B02~B04 와 같은 어휘: tcb_t, kernel_tick, sched_pick_next, task_delay, task_yield)
 *   sim  모드: 결정적 tick 시뮬레이터. 태스크 = 각본(CPU 를 계속 씀 / 주기적으로 burst 후 지연).
 *              문맥(스택)이 없으므로 결과가 매번 같다 → 타임라인 문자열까지 정확히 CHECK.
 *   real 모드: setitimer(ITIMER_REAL) 의 SIGALRM = tick 인터럽트(SysTick 자리). 핸들러가 kernel_tick() 으로
 *              "선점? slice 만료?" 를 정하고 swapcontext() 로 태스크를 바꾼다. 태스크는 진짜 C 함수
 *              (절대 양보하지 않는 무한 루프 포함). 실제 타이머라 비결정적 → CHECK 는 넉넉한 범위로.
 *   주의: 시그널 핸들러에서 swapcontext 는 POSIX 가 보장하지 않는다 (async-signal-safe 목록에 없음).
 *         학습용 모델이다. 실제 Cortex-M 커널은 SysTick → PendSV 로 문맥 교환한다 (RTOS-A01, ARM-A01).
 *
 * TODO 1 kernel_tick()     : 지연 끝난 태스크 깨우기 + 선점 판단 + time slice 회계 → "전환 필요?" 반환
 * TODO 2 sched_pick_next() : 최고 우선순위 선택, 같은 우선순위는 라운드 로빈 (밀려난 태스크는 줄 맨 뒤)
 * TODO 3 crit_enter/exit() : sigprocmask(SIGALRM) 로 "인터럽트 금지" + 중첩 카운트
 * TODO 4 port_switch()     : pick → 다른 태스크면 swapcontext (중첩 카운트는 태스크별로 저장/복원)
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* getcontext/makecontext/swapcontext 만 해당 */

/* ======================= 미니 커널 자료구조 (하네스 제공) ======================= */
#define NPRIO     4                 /* 0 = idle, 1..3 */
#define MAX_TASKS 8
#define STACK_SZ  (64 * 1024)
#define TL_LEN    64                /* 타임라인은 앞 64 tick 만 기록 */
#define MAX_ACT   256

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
enum { SCRIPT_CPU, SCRIPT_PERIODIC };   /* sim 모드 각본: CPU 를 계속 씀 / burst tick 일하고 지연 */

typedef struct tcb {
    const char *name;
    uint8_t prio;                   /* 클수록 급하다 (FreeRTOS 방향. Zephyr/µC/OS 는 반대) */
    task_state_t state;
    uint32_t wake_tick;             /* BLOCKED(지연) 이면 깨어날 tick */
    void *wait_obj;                 /* 이 레슨에선 늘 NULL = 순수 지연 (세마포어 대기는 RTOS-B08/B09) */
    struct tcb *next, *prev;        /* ready list 링크 (RUNNING 태스크는 리스트에서 빠져 있다) */
    uint32_t slice_left;            /* 이번 차례에 남은 time slice (tick) */
    uint32_t slice;                 /* 0 = g_cfg.slice 사용 (연습 2: 태스크별 slice) */
    /* real 모드 */
    ucontext_t ctx;
    void (*fn)(struct tcb *self);
    uint32_t period;                /* 주기 태스크: 한 번 일하고 task_delay(period) */
    volatile uint64_t work;         /* 태스크가 한 일 (루프 횟수) — 선점으로 언제 끊길지 몰라 volatile */
    /* sim 모드 각본 */
    int script, burst, left;
    /* 통계 */
    uint32_t ticks_run, dispatches, activations, max_lat;
    uint32_t act_tick[MAX_ACT];
    char tl[TL_LEN + 1];
} tcb_t;

static struct { bool preempt, slicing; uint32_t slice; } g_cfg;   /* configUSE_PREEMPTION / _TIME_SLICING */

static tcb_t    g_task[MAX_TASKS];
static int      g_ntask;
static tcb_t   *g_rq_head[NPRIO], *g_rq_tail[NPRIO];  /* 우선순위별 ready FIFO */
static tcb_t   *g_cur;                                /* pxCurrentTCB */
static uint32_t g_tick;                               /* xTickCount: 핸들러가 쓰고, 태스크는 crit 안에서 읽는다 */
static uint32_t g_ctx_switches;                       /* 실제로 다른 태스크로 바뀐 횟수 */
static bool     g_sim = true;                         /* true = 결정적 시뮬레이터 (문맥 교환 없음) */
static volatile sig_atomic_t g_crit_nest;             /* uxCriticalNesting */
static volatile sig_atomic_t g_stopped;               /* real 모드 종료 플래그 (핸들러 ↔ main) */
static sigset_t   g_alrm_set;                         /* {SIGALRM} = "인터럽트 마스크" */
static ucontext_t g_main_ctx;
static uint32_t   g_stop_tick;
static _Alignas(16) char g_stack[MAX_TASKS][STACK_SZ];

static void rq_push_tail(tcb_t *t) {
    t->next = NULL; t->prev = g_rq_tail[t->prio];
    if (t->prev) t->prev->next = t; else g_rq_head[t->prio] = t;
    g_rq_tail[t->prio] = t;
}
static void rq_remove(tcb_t *t) {
    if (t->prev) t->prev->next = t->next; else g_rq_head[t->prio] = t->next;
    if (t->next) t->next->prev = t->prev; else g_rq_tail[t->prio] = t->prev;
    t->next = t->prev = NULL;
}
static uint32_t slice_of(const tcb_t *t) { return t->slice ? t->slice : g_cfg.slice; }

static void k_reset(bool preempt, bool slicing, uint32_t slice) {
    memset(g_task, 0, sizeof g_task);
    memset(g_rq_head, 0, sizeof g_rq_head); memset(g_rq_tail, 0, sizeof g_rq_tail);
    g_ntask = 0; g_cur = NULL; g_tick = 0; g_ctx_switches = 0; g_sim = true; g_crit_nest = 0;
    g_cfg.preempt = preempt; g_cfg.slicing = slicing; g_cfg.slice = slice;
}
static tcb_t *k_add(const char *name, uint8_t prio, int script, int burst, uint32_t period) {
    tcb_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->state = TASK_READY;
    t->script = script; t->burst = t->left = burst; t->period = period;
    memset(t->tl, '.', TL_LEN);
    rq_push_tail(t);
    return t;
}
static void k_start(void) {             /* vTaskStartScheduler: 최고 우선순위 맨 앞 태스크로 시작 */
    for (int p = NPRIO - 1; p >= 0 && !g_cur; p--)
        if (g_rq_head[p]) { g_cur = g_rq_head[p]; rq_remove(g_cur); }
    g_cur->state = TASK_RUNNING; g_cur->slice_left = slice_of(g_cur); g_cur->dispatches++;
}

/* ======================= TODO: 커널 핵심 ======================= */

/* TODO 1 — tick 인터럽트마다 (g_tick 은 하네스가 이미 +1 했다). "인터럽트 금지" 상태에서 불린다.
 *   ① BLOCKED 이고 wait_obj == NULL 이며 wake_tick 에 도달한 태스크 → READY (줄 맨 뒤). wrap 안전 비교.
 *   ② 선점이 켜져 있고 깨어난 태스크가 g_cur 보다 급하면 → 전환 필요
 *   ③ 선점 + time slicing 이 켜져 있으면 g_cur 의 slice_left 를 1 줄이고, 0 이 되면
 *      같은 우선순위 READY 가 있으면 전환 필요, 없으면 slice 를 다시 채우고 계속
 *   반환: 전환이 필요하면 true (FreeRTOS xTaskIncrementTick 의 xSwitchRequired) */
static bool kernel_tick(void)
{
    bool need = false;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (t->state == TASK_BLOCKED && t->wait_obj == NULL && (int32_t)(g_tick - t->wake_tick) >= 0) {
            t->state = TASK_READY;
            rq_push_tail(t);
            if (g_cfg.preempt && t->prio > g_cur->prio) need = true;
        }
    }
    if (g_cfg.preempt && g_cfg.slicing && g_cur->state == TASK_RUNNING) {
        if (g_cur->slice_left > 0) g_cur->slice_left--;
        if (g_cur->slice_left == 0) {
            if (g_rq_head[g_cur->prio]) need = true;
            else g_cur->slice_left = slice_of(g_cur);
        }
    }
    return need;
}

/* TODO 2 — 다음에 돌 태스크를 골라 RUNNING 으로 만들고 돌려준다 (vTaskSwitchContext 의 선택 부분).
 *   - g_cur 가 아직 RUNNING (선점당함 / slice 끝 / yield) 이면 READY 로 바꿔 자기 우선순위 줄 맨 뒤에
 *   - 가장 높은 우선순위 줄의 맨 앞을 꺼내 RUNNING, slice_left = slice_of(t)
 *   - g_cur 를 바꾸지는 않는다 (port_switch 가 한다) */
static tcb_t *sched_pick_next(void)
{
    if (g_cur && g_cur->state == TASK_RUNNING) {
        g_cur->state = TASK_READY;
        rq_push_tail(g_cur);
    }
    for (int p = NPRIO - 1; p >= 0; p--) {
        tcb_t *t = g_rq_head[p];
        if (t) {
            rq_remove(t);
            t->state = TASK_RUNNING;
            t->slice_left = slice_of(t);
            return t;
        }
    }
    return NULL;
}

/* TODO 3 — 임계 구역 = SIGALRM 차단 (taskENTER_CRITICAL / taskEXIT_CRITICAL).
 *   중첩 가능: 바깥쪽 enter 에서만 막고, 바깥쪽 exit 에서만 푼다. g_crit_nest 가 깊이. */
static void crit_enter(void)
{
    if (g_crit_nest == 0) sigprocmask(SIG_BLOCK, &g_alrm_set, NULL);
    g_crit_nest++;
}
static void crit_exit(void)
{
    g_crit_nest--;
    if (g_crit_nest == 0) sigprocmask(SIG_UNBLOCK, &g_alrm_set, NULL);
}

/* TODO 4 — 문맥 교환 (PendSV 자리). 인터럽트 금지 상태(핸들러 안 또는 crit 안)에서 불린다.
 *   next = sched_pick_next(); 같은 태스크(또는 NULL)면 아무것도 안 함.
 *   다르면 g_ctx_switches++, next->dispatches++, g_cur = next,
 *   real 모드(!g_sim)면 swapcontext(&prev->ctx, &next->ctx).
 *   g_crit_nest 는 태스크마다 다르므로 swap 전에 지역 변수(=내 스택)에 저장, 돌아오면 복원. */
static void port_switch(void)
{
    tcb_t *prev = g_cur;
    tcb_t *next = sched_pick_next();
    if (next == NULL || next == prev) return;
    g_ctx_switches++;
    next->dispatches++;
    g_cur = next;
    if (!g_sim) {
        sig_atomic_t nest = g_crit_nest;
        swapcontext(&prev->ctx, &next->ctx);   /* ... 다른 태스크들이 돈다 ... 언젠가 여기로 돌아온다 */
        g_crit_nest = nest;
    }
}

/* ======================= 커널 서비스 (하네스 제공) ======================= */
static void task_delay(uint32_t n) {                 /* vTaskDelay */
    crit_enter();
    g_cur->state = TASK_BLOCKED; g_cur->wait_obj = NULL; g_cur->wake_tick = g_tick + n;
    port_switch();
    crit_exit();
}
static void task_yield(void) { crit_enter(); port_switch(); crit_exit(); }   /* taskYIELD */
static uint32_t task_get_tick(void) {                /* xTaskGetTickCount */
    crit_enter(); uint32_t t = g_tick; crit_exit();
    return t;
}

/* ---------- sim 모드: tick 루프 ---------- */
static void sim_activation(tcb_t *c, uint32_t k) {
    uint32_t lat = k - c->wake_tick;
    if (lat > c->max_lat) c->max_lat = lat;
    if (c->activations < MAX_ACT) c->act_tick[c->activations] = k;
    c->activations++;
}
static void sim_run(uint32_t nticks) {
    for (uint32_t k = 0; k < nticks; k++) {
        if (k > 0) { g_tick++; if (kernel_tick()) port_switch(); }      /* ① tick ISR (경계 k) */
        tcb_t *c = g_cur;
        if (c->script == SCRIPT_PERIODIC && c->left == 0) {               /* ② burst 끝난 태스크는 스스로 지연 */
            c->left = c->burst;
            task_delay(c->period - (uint32_t)c->burst);                   /* period tick 마다 깨어나게 */
            c = g_cur;
        }
        if (c->script == SCRIPT_PERIODIC && c->left == c->burst) sim_activation(c, k);
        c->ticks_run++;                                                    /* ③ 이 tick 동안 c 가 CPU 를 쓴다 */
        if (c->script == SCRIPT_PERIODIC) c->left--;
        if (k < TL_LEN)
            for (int i = 0; i < g_ntask; i++) {
                tcb_t *t = &g_task[i];
                t->tl[k] = t == c ? 'R' : t->state == TASK_READY ? 'r' : t->state == TASK_BLOCKED ? 'B'
                         : t->state == TASK_SUSPENDED ? 'S' : '.';
            }
    }
}
static void print_tl(const char *title, uint32_t n) {
    printf("%s\ntick : ", title);
    for (uint32_t k = 0; k < n; k++) printf("%u", k % 10);
    printf("\n");
    for (int i = 0; i < g_ntask; i++) printf("%-5s: %.*s\n", g_task[i].name, (int)n, g_task[i].tl);
}

/* ---------- real 모드: SIGALRM tick + swapcontext ---------- */
static void task_entry(void) {                      /* makecontext 진입점: 처음 디스패치되면 여기서 시작 */
    g_crit_nest = 0;                                /* 새 태스크는 임계 구역 밖에서 시작 */
    sigprocmask(SIG_UNBLOCK, &g_alrm_set, NULL);    /* (핸들러 안에서 처음 디스패치돼도) tick 을 받게 */
    g_cur->fn(g_cur);
    for (;;) task_delay(1000000u);                  /* 태스크 함수는 return 하면 안 된다 */
}
static void on_tick(int sig) {                      /* SIGALRM 핸들러 = SysTick_Handler */
    (void)sig;
    int saved_errno = errno;
    if (g_stopped || g_cur == NULL) { errno = saved_errno; return; }
    g_crit_nest++;                                  /* 핸들러 안 = SIGALRM 차단 상태 (sa_mask) */
    if (g_tick < TL_LEN) g_cur->tl[g_tick] = 'R';   /* 방금 끝난 tick 에 CPU 를 쥐고 있던 태스크 */
    g_cur->ticks_run++;
    g_tick++;
    if (g_tick >= g_stop_tick) {
        g_stopped = 1;
        swapcontext(&g_cur->ctx, &g_main_ctx);      /* 커널 정지 → main 으로 (다시 오지 않는다) */
    } else if (kernel_tick()) {
        port_switch();                              /* 여기서 다른 태스크로 간다 — 돌아오면 핸들러를 마저 끝낸다 */
    }
    g_crit_nest--;
    errno = saved_errno;
}
static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
static double real_run(uint32_t nticks, int tick_us) {   /* 반환: 걸린 실제 시간 (ms) */
    g_sim = false; g_stopped = 0; g_stop_tick = nticks; g_tick = 0; g_ctx_switches = 0;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        getcontext(&t->ctx);
        t->ctx.uc_stack.ss_sp = g_stack[i]; t->ctx.uc_stack.ss_size = STACK_SZ; t->ctx.uc_link = NULL;
        makecontext(&t->ctx, task_entry, 0);
    }
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tick; sigemptyset(&sa.sa_mask); sa.sa_flags = 0;   /* 핸들러 중엔 SIGALRM 자동 차단 */
    sigaction(SIGALRM, &sa, &old);
    sigprocmask(SIG_BLOCK, &g_alrm_set, NULL);      /* main 은 tick 을 받지 않는다 */
    k_start();
    struct itimerval it = { { 0, tick_us }, { 0, tick_us } };
    double t0 = now_ms();
    setitimer(ITIMER_REAL, &it, NULL);
    sig_atomic_t nest = g_crit_nest;
    swapcontext(&g_main_ctx, &g_cur->ctx);          /* 스케줄러 시작. g_stop_tick 에서 핸들러가 여기로 돌려보낸다 */
    g_crit_nest = nest;
    double ms = now_ms() - t0;
    struct itimerval off; memset(&off, 0, sizeof off);
    setitimer(ITIMER_REAL, &off, NULL);
    sigprocmask(SIG_UNBLOCK, &g_alrm_set, NULL);    /* 남아 있던 SIGALRM 은 g_stopped 라 무시된다 */
    sigaction(SIGALRM, &old, NULL);
    g_sim = true;
    return ms;
}

/* real 모드 태스크들 */
static void hog_task(tcb_t *self) { for (;;) self->work++; }   /* 절대 양보하지 않는 무한 루프 */
static void idle_task(tcb_t *self) {                            /* WFI 자리: 다음 인터럽트까지 잔다 */
    sigset_t none; sigemptyset(&none);
    for (;;) { self->work++; sigsuspend(&none); }
}
static void note_activation(tcb_t *self) {
    uint32_t now = task_get_tick();
    uint32_t lat = now - self->wake_tick;           /* 깨어나야 할 tick 대비 실제로 시작한 tick */
    if (lat > self->max_lat) self->max_lat = lat;
    if (self->activations < MAX_ACT) self->act_tick[self->activations] = now;
    self->activations++;
    if (now < TL_LEN) self->tl[now] = 'R';
}
static void periodic_task(tcb_t *self) {
    for (;;) {
        note_activation(self);
        for (int i = 0; i < 2000; i++) self->work++;   /* 짧은 일 (수 µs) */
        task_delay(self->period);
    }
}

/* Part C: IMU 샘플 레코드 (IMU 태스크가 쓰고, 텔레메트리 태스크가 읽는다) */
typedef struct { uint32_t seq; int32_t ax, ay; uint32_t sum; } imu_rec_t;
static volatile imu_rec_t g_rec;
static volatile uint32_t g_reads, g_torn;      /* 비동기로 끊기는 무한 루프가 쓰고 main 이 읽는다 */
static bool     g_telem_crit = true;
static uint32_t rec_sum(uint32_t seq, int32_t ax, int32_t ay) {
    return (seq * 2654435761u) ^ (uint32_t)ax ^ ((uint32_t)ay << 1);
}
static void imu_task(tcb_t *self) {                 /* 최고 우선순위: 쓰는 도중엔 아무 태스크도 못 끼어든다 */
    for (;;) {
        note_activation(self);
        uint32_t s = g_rec.seq + 1;
        int32_t ax = (int32_t)(s % 1000u) - 500, ay = 250 - (int32_t)(s % 700u);
        g_rec.seq = s; g_rec.ax = ax; g_rec.ay = ay; g_rec.sum = rec_sum(s, ax, ay);
        task_delay(self->period);
    }
}
static void telem_task(tcb_t *self) {               /* 낮은 우선순위: 읽는 도중 IMU 에게 선점될 수 있다 */
    for (;;) {
        imu_rec_t r;
        if (g_telem_crit) crit_enter();
        r.seq = g_rec.seq; r.ax = g_rec.ax; r.ay = g_rec.ay; r.sum = g_rec.sum;
        if (g_telem_crit) crit_exit();
        if (r.sum != rec_sum(r.seq, r.ax, r.ay)) g_torn++;
        g_reads++; self->work++;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool alrm_blocked(void) {
    sigset_t s; sigprocmask(SIG_BLOCK, NULL, &s);
    return sigismember(&s, SIGALRM) == 1;
}
static bool in_range(double x, double expect, double lo, double hi) { return x >= expect * lo && x <= expect * hi; }
static tcb_t *T(const char *name) {
    for (int i = 0; i < g_ntask; i++) if (strcmp(g_task[i].name, name) == 0) return &g_task[i];
    return &g_task[0];
}
static bool tl_is(const tcb_t *t, const char *s) { return strncmp(t->tl, s, strlen(s)) == 0; }

/* Part A1 — 손으로 만든 상태에서 함수 하나씩 */
static void part_a_unit(void) {
    k_reset(true, true, 2);
    k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0); k_add("C", 1, SCRIPT_CPU, 0, 0);
    k_start();
    CHECK(g_cur == T("A") && g_cur->slice_left == 2);
    g_tick++; CHECK(!kernel_tick() && g_cur->slice_left == 1);          /* slice 2 중 1 tick 씀 */
    g_tick++; CHECK(kernel_tick());                                       /* slice 끝 + B, C 대기 → 전환 */
    port_switch();
    CHECK(g_cur == T("B") && T("A")->state == TASK_READY && g_ctx_switches == 1);
    CHECK(g_rq_head[1] == T("C") && g_rq_tail[1] == T("A"));             /* A 는 줄 맨 뒤 */
    char order[8] = { 0 };
    for (int i = 0; i < 4; i++) { task_yield(); order[i] = g_cur->name[0]; }
    CHECK(strcmp(order, "CABC") == 0);                                    /* yield = 라운드 로빈 */

    k_reset(true, true, 1);                                               /* 같은 우선순위에 혼자면 계속 */
    k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("I", 0, SCRIPT_CPU, 0, 0);
    k_start();
    int sw = 0;
    for (int i = 0; i < 10; i++) { g_tick++; sw += kernel_tick(); }
    CHECK(sw == 0 && g_cur->slice_left == 1);
    task_yield(); CHECK(g_cur == T("A") && g_ctx_switches == 0);          /* yield 해도 갈 곳이 없다 */

    k_reset(false, true, 1);                                              /* 협력형: 무엇이 와도 전환 안 함 */
    k_add("H", 2, SCRIPT_CPU, 0, 0); k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0);
    k_start();
    task_delay(3);                                                        /* H 지연 → A */
    CHECK(g_cur == T("A") && T("H")->state == TASK_BLOCKED && T("H")->wake_tick == 3);
    sw = 0;
    for (int i = 0; i < 5; i++) { g_tick++; sw += kernel_tick(); }
    CHECK(sw == 0 && T("H")->state == TASK_READY && g_cur == T("A"));     /* 깨어나긴 했지만 선점 없음 */

    k_reset(true, false, 1);                                              /* 선점 O, slicing X */
    k_add("H", 2, SCRIPT_CPU, 0, 0); k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0);
    k_start(); task_delay(3);
    bool r1 = (g_tick++, kernel_tick()), r2 = (g_tick++, kernel_tick()), r3 = (g_tick++, kernel_tick());
    CHECK(!r1 && !r2 && r3);                                              /* tick 3 에 H 가 깨어나 즉시 선점 */
    port_switch();
    CHECK(g_cur == T("H") && g_rq_head[1] == T("B") && g_rq_tail[1] == T("A"));   /* 선점당한 A 는 맨 뒤 */
    task_delay(3);
    CHECK(g_cur == T("B"));                                               /* H 가 비키면 A 가 아니라 B */

    k_reset(true, true, 1);                                               /* tick wrap: 0xFFFFFFFE + 3 = 1 */
    k_add("H", 2, SCRIPT_CPU, 0, 0); k_add("A", 1, SCRIPT_CPU, 0, 0);
    k_start(); g_tick = 0xFFFFFFFEu; task_delay(3);
    sw = 0;
    while (!kernel_tick() && sw < 10) { g_tick++; sw++; }
    CHECK(g_tick == 1u && T("H")->state == TASK_READY);

    k_reset(true, true, 1);                                               /* 임계 구역 중첩 */
    crit_enter(); crit_enter();
    bool b2 = alrm_blocked() && g_crit_nest == 2;
    crit_exit();
    bool b1 = alrm_blocked() && g_crit_nest == 1;
    crit_exit();
    CHECK(b2 && b1 && !alrm_blocked() && g_crit_nest == 0);
}

/* Part A2 — 시나리오: H(p2, 4 tick 마다 1 tick) + HOG/B/C(p1, CPU 를 놓지 않음) + IDLE */
static void sim_scenario(bool preempt, bool slicing, uint32_t slice, uint32_t n) {
    k_reset(preempt, slicing, slice);
    k_add("H", 2, SCRIPT_PERIODIC, 1, 4);
    k_add("HOG", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0); k_add("C", 1, SCRIPT_CPU, 0, 0);
    k_add("IDLE", 0, SCRIPT_CPU, 0, 0);
    k_start();
    sim_run(n);
}
static void part_a_scenarios(void) {
    sim_scenario(false, false, 1, 24);
    print_tl("A2 coop (preempt=0)", 24);
    CHECK(T("H")->activations == 1 && T("B")->ticks_run == 0 && T("C")->ticks_run == 0 && T("HOG")->ticks_run == 23);
    CHECK(tl_is(T("H"), "RBBBrrrrrrrrrrrrrrrrrrrr"));   /* tick 4 에 깨어나지만 영원히 r */

    sim_scenario(true, false, 1, 24);
    print_tl("A2 preempt=1 slicing=0", 24);
    CHECK(T("H")->activations == 6 && T("H")->max_lat == 0);
    CHECK(T("HOG")->ticks_run == 6 && T("B")->ticks_run == 6 && T("C")->ticks_run == 6);   /* H 가 돌릴 때만 바뀜 */
    CHECK(tl_is(T("B"), "rrrrrRRRrrrrrrrrrRRRrrrr"));

    sim_scenario(true, true, 1, 24);
    print_tl("A2 preempt=1 slicing=1 slice=1 (FreeRTOS 기본)", 24);
    CHECK(T("H")->activations == 6 && T("H")->max_lat == 0);
    CHECK(tl_is(T("HOG"), "rRrrrRrrrRrrrRrrrRrrrRrr"));
    CHECK(g_ctx_switches == 23);                                    /* 매 tick 바뀐다 */

    sim_scenario(true, true, 2, 24);
    print_tl("A2 preempt=1 slicing=1 slice=2", 24);
    CHECK(tl_is(T("B"), "rrrRrrrrrRRrrrrRrrrrrRRr"));    /* tick 4: B 는 slice 1 남기고 선점 → 맨 뒤 */
    CHECK(T("H")->activations == 6 && T("IDLE")->ticks_run == 0);
}

/* Part A3 — slice 길이 vs 문맥 교환 횟수 (CPU 태스크 3개, 120 tick, 결정적) */
static void part_a_sweep(void) {
    static const uint32_t sl[] = { 1, 2, 5, 10 };
    printf("A3 sim sweep: 3 CPU tasks, 120 ticks\n slice | switches | expect floor(119/s) | ticks A/B/C\n");
    for (int i = 0; i < 4; i++) {
        k_reset(true, true, sl[i]);
        k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0); k_add("C", 1, SCRIPT_CPU, 0, 0);
        k_start(); sim_run(120);
        uint32_t ta = T("A")->ticks_run, tb = T("B")->ticks_run, tc = T("C")->ticks_run;
        printf("  %4u | %8u | %19u | %u/%u/%u\n", sl[i], g_ctx_switches, (119u / sl[i]), ta, tb, tc);
        CHECK(g_ctx_switches == 119u / sl[i]);              /* tick ISR 은 119 번 (경계 1..119) */
        CHECK(ta + tb + tc == 120 && ta - tc <= sl[i] && tc <= ta);
    }
}

/* Part B1 — 실제 SIGALRM: 같은 시나리오를 진짜 무한 루프로 */
static void real_scenario(bool preempt, bool slicing, uint32_t slice, uint32_t n, double *ms) {
    k_reset(preempt, slicing, slice);
    tcb_t *h = k_add("H", 2, SCRIPT_CPU, 0, 4); h->fn = periodic_task;
    k_add("HOG", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
    k_add("B", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
    k_add("C", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
    k_add("IDLE", 0, SCRIPT_CPU, 0, 0)->fn = idle_task;
    *ms = real_run(n, 1000);
}
static void part_b_modes(void) {
    const uint32_t N = 200;
    double ms;
    printf("B1 real SIGALRM (1 ms tick, %u ticks)\n mode                 | wall ms | H act | H maxlat | ticks HOG/B/C | switches\n", N);
    static const struct { bool p, s; const char *name; } m[] = {
        { false, false, "coop (preempt=0)    " }, { true, false, "preempt, slicing=0  " }, { true, true, "preempt, slicing=1  " } };
    for (int i = 0; i < 3; i++) {
        real_scenario(m[i].p, m[i].s, 1, N, &ms);
        tcb_t *h = T("H"), *hog = T("HOG"), *b = T("B"), *c = T("C");
        printf(" %s | %7.1f | %5u | %8u | %u/%u/%u | %u\n", m[i].name, ms, h->activations, h->max_lat,
               hog->ticks_run, b->ticks_run, c->ticks_run, g_ctx_switches);
        if (i == 2) print_tl("B1 real timeline (preempt, slicing=1): R = tick 끝에 CPU 를 쥔 태스크 / H 는 시작 tick", 40);
        CHECK(in_range(ms, N, 0.5, 3.0));                                     /* tick 이 대략 실제 1 ms */
        if (i == 0) {
            CHECK(h->activations == 1 && b->ticks_run == 0 && c->ticks_run == 0);   /* 무한 루프가 전부 굶긴다 */
        } else {
            CHECK(in_range(h->activations, N / 4.0, 0.5, 1.5) && h->max_lat <= 1);  /* 선점: H 는 제때 돈다 */
            CHECK(b->ticks_run >= 1 && c->ticks_run >= 1);                          /* HOG 가 있어도 B, C 가 돈다 */
        }
        if (i == 2) {
            double mean = (hog->ticks_run + b->ticks_run + c->ticks_run) / 3.0;
            CHECK(in_range(hog->ticks_run, mean, 0.5, 1.5) && in_range(b->ticks_run, mean, 0.5, 1.5) &&
                  in_range(c->ticks_run, mean, 0.5, 1.5));                         /* slicing: 고르게 나눈다 */
        }
    }
}

/* Part B2 — slice 길이별 실제 문맥 교환 횟수 */
static void part_b_sweep(void) {
    static const uint32_t sl[] = { 1, 2, 5, 10 };
    const uint32_t N = 250;
    printf("B2 real sweep: 3 hogs, 1 ms tick, %u ticks\n slice | wall ms | switches | expect N/s | ratio | work share A/B/C\n", N);
    for (int i = 0; i < 4; i++) {
        k_reset(true, true, sl[i]);
        k_add("A", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
        k_add("B", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
        k_add("C", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
        k_add("IDLE", 0, SCRIPT_CPU, 0, 0)->fn = idle_task;
        double ms = real_run(N, 1000);
        double wa = (double)T("A")->work, wb = (double)T("B")->work, wc = (double)T("C")->work, w = wa + wb + wc;
        double expect = (double)N / sl[i];
        printf("  %4u | %7.1f | %8u | %10.1f | %5.2f | %.2f/%.2f/%.2f\n", sl[i], ms, g_ctx_switches, expect,
               g_ctx_switches / expect, wa / w, wb / w, wc / w);
        CHECK(in_range(g_ctx_switches, expect, 0.5, 1.5));
        CHECK(wa / w > 0.15 && wb / w > 0.15 && wc / w > 0.15);                 /* 셋 다 일을 했다 */
    }
}

/* Part C — IMU(p3, 4 ms) + TELEM(p1, 레코드 복사) + BLE(p1, 무한 루프) */
static void part_c(void) {
    const uint32_t N = 300;
    k_reset(true, true, 1);
    k_add("IMU", 3, SCRIPT_CPU, 0, 4)->fn = imu_task;
    k_add("TELEM", 1, SCRIPT_CPU, 0, 0)->fn = telem_task;
    k_add("BLE", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
    k_add("IDLE", 0, SCRIPT_CPU, 0, 0)->fn = idle_task;
    memset((void *)&g_rec, 0, sizeof g_rec); g_rec.sum = rec_sum(0, 0, 0);
    g_reads = g_torn = 0; g_telem_crit = true;
    double ms = real_run(N, 1000);
    tcb_t *imu = T("IMU");
    printf("C IMU/TELEM/BLE: wall %.1f ms, IMU act=%u maxlat=%u, TELEM reads=%u torn=%u, ticks TELEM/BLE=%u/%u, nest=%d\n",
           ms, imu->activations, imu->max_lat, g_reads, g_torn, T("TELEM")->ticks_run, T("BLE")->ticks_run, (int)g_crit_nest);
    CHECK(in_range(imu->activations, N / 4.0, 0.5, 1.5) && imu->max_lat <= 1);
    CHECK(g_reads > 1000 && T("BLE")->work > 0);
    CHECK(g_torn == 0);                                                     /* crit 안에서 복사 → 찢어진 레코드 0 */
    CHECK(g_crit_nest == 0 && !alrm_blocked());
}

int main(void) {
    sigemptyset(&g_alrm_set); sigaddset(&g_alrm_set, SIGALRM);
    part_a_unit();
    part_a_scenarios();
    part_a_sweep();
    part_b_modes();
    part_b_sweep();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
