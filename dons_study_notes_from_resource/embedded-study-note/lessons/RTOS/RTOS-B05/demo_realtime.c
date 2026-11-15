/* RTOS-B05 demo — 실제 SIGALRM tick 으로만 볼 수 있는 세 가지 (CHECK 없음, 관찰용. 실행마다 숫자가 조금씩 다르다)
 *
 * BUILD: cc -std=c11 -Wall -Wextra -Werror -O2 demo_realtime.c -o demo_rt && ./demo_rt
 *   커널 코드는 solution.c 와 같다 (복사본 — 레슨 파일은 각자 자기 완결).
 *   E1 찢어진 읽기: TELEM 이 crit 없이 IMU 레코드를 복사하면 IMU 선점에 끊겨 섞인 레코드가 보인다
 *   E2 잃어버린 tick: crit(SIGALRM 차단)을 tick 보다 길게 쥐면 그동안의 SIGALRM 이 1개로 합쳐진다
 *   E3 호스트 "커널" 부품의 비용: swapcontext 한 번, crit(sigprocmask 2번), 시그널 1번 (raise + 빈 핸들러)
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

/* ======================= 실험 ======================= */
static void e1_torn(bool use_crit, uint32_t *torn, uint32_t *reads) {
    k_reset(true, true, 1);
    k_add("IMU", 3, SCRIPT_CPU, 0, 4)->fn = imu_task;
    k_add("TELEM", 1, SCRIPT_CPU, 0, 0)->fn = telem_task;
    k_add("BLE", 1, SCRIPT_CPU, 0, 0)->fn = hog_task;
    k_add("IDLE", 0, SCRIPT_CPU, 0, 0)->fn = idle_task;
    memset((void *)&g_rec, 0, sizeof g_rec); g_rec.sum = rec_sum(0, 0, 0);
    g_reads = g_torn = 0; g_telem_crit = use_crit;
    real_run(300, 1000);
    *torn = g_torn; *reads = g_reads;
}

static uint32_t g_hold_us;                        /* E2: crit 를 쥐는 시간 */
static void busy_us(uint32_t us) { double t0 = now_ms(); while ((now_ms() - t0) * 1000.0 < us) { } }
static void long_crit_task(tcb_t *self) {
    for (;;) { crit_enter(); busy_us(g_hold_us); crit_exit(); self->work++; }
}

static ucontext_t g_pa, g_pb;                      /* E3: 시그널 없이 swapcontext 왕복 */
static void pong(void) { for (;;) swapcontext(&g_pb, &g_pa); }
static volatile sig_atomic_t g_hits;
static void empty_handler(int sig) { (void)sig; g_hits++; }
static double median5(double *v) {
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (v[j] < v[i]) { double t = v[i]; v[i] = v[j]; v[j] = t; }
    return v[2];
}

int main(void) {
    sigemptyset(&g_alrm_set); sigaddset(&g_alrm_set, SIGALRM);
    (void)task_yield; (void)sim_run; (void)print_tl; (void)periodic_task;   /* solution.c 와 같은 커널 — 여기선 안 씀 */

    printf("E1 torn reads (IMU p3 writes every 4 ticks, TELEM p1 copies 4 fields), 300 ticks x 5 runs\n");
    for (int r = 0; r < 5; r++) {
        uint32_t t0, r0, t1, r1;
        e1_torn(false, &t0, &r0); e1_torn(true, &t1, &r1);
        printf("  run %d: no crit torn=%3u / reads=%u   |  crit torn=%u / reads=%u\n", r, t0, r0, t1, r1);
    }

    printf("E2 lost ticks: one task holds crit (SIGALRM blocked) for X us, loops; 1 ms tick, run until tick 100\n");
    printf("  hold us | wall ms | ticks | ms per tick\n");
    static const uint32_t hold[] = { 200, 900, 2500, 5500 };
    for (int i = 0; i < 4; i++) {
        k_reset(true, true, 1);
        k_add("L", 1, SCRIPT_CPU, 0, 0)->fn = long_crit_task;
        k_add("IDLE", 0, SCRIPT_CPU, 0, 0)->fn = idle_task;
        g_hold_us = hold[i];
        double ms = real_run(100, 1000);
        printf("  %7u | %7.1f | %5u | %5.2f\n", hold[i], ms, g_tick, ms / g_tick);
    }

    printf("E3 cost of the host \"kernel\" primitives (median of 5 x 200000, ns per op)\n");
    const int M = 200000;
    double v[5];
    getcontext(&g_pb); g_pb.uc_stack.ss_sp = g_stack[0]; g_pb.uc_stack.ss_size = STACK_SZ; g_pb.uc_link = NULL;
    makecontext(&g_pb, pong, 0);
    for (int r = 0; r < 5; r++) {
        double t0 = now_ms();
        for (int k = 0; k < M; k++) swapcontext(&g_pa, &g_pb);       /* 왕복 = swapcontext 2번 */
        v[r] = (now_ms() - t0) * 1e6 / (2.0 * M);
    }
    printf("  swapcontext (one direction)      : %6.1f ns\n", median5(v));
    for (int r = 0; r < 5; r++) {
        double t0 = now_ms();
        for (int k = 0; k < M; k++) { sigprocmask(SIG_BLOCK, &g_alrm_set, NULL); sigprocmask(SIG_UNBLOCK, &g_alrm_set, NULL); }
        v[r] = (now_ms() - t0) * 1e6 / M;
    }
    printf("  crit_enter+crit_exit (2 syscalls): %6.1f ns\n", median5(v));
    struct sigaction sa, old; memset(&sa, 0, sizeof sa); sa.sa_handler = empty_handler; sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, &old);
    for (int r = 0; r < 5; r++) {
        double t0 = now_ms();
        for (int k = 0; k < M; k++) raise(SIGUSR1);
        v[r] = (now_ms() - t0) * 1e6 / M;
    }
    sigaction(SIGUSR1, &old, NULL);
    printf("  raise + empty handler (tick ISR) : %6.1f ns   (hits=%d)\n", median5(v), (int)g_hits);
    return 0;
}
