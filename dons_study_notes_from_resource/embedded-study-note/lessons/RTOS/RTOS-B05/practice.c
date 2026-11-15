/* RTOS-B05 추가 연습 — 한 번에 검증 (결정적 tick 시뮬레이터만 사용: 실제 시그널/문맥 교환 없음)
 *
 * BUILD: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *
 *   연습 1  time slicing 끔 + task_yield(): 같은 우선순위는 "양보할 때만" 바뀐다
 *   연습 2  태스크별 slice (가중 라운드 로빈): slice 3/2/1 → CPU 3:2:1
 *   연습 3  새로 깨어난 같은 우선순위 태스크의 최악 대기 = n·s − 1 tick (위상 전수 조사)
 *   연습 4  스케줄러 잠금 (vTaskSuspendAll / xTaskResumeAll): 잠긴 동안 tick 은 "보류" → 풀 때 몰아서 처리
 *   연습 5  선점당한 태스크를 줄 맨 앞 (POSIX SCHED_RR 식) vs 맨 뒤 (FreeRTOS 식)
 *
 * 커널 코드는 solution.c 의 sim 부분과 같다 (kernel_tick / sched_pick_next / port_switch 의 정답 버전).
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NPRIO     4
#define MAX_TASKS 8
#define TL_LEN    64

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
enum { SCRIPT_CPU, SCRIPT_PERIODIC, SCRIPT_YIELDER };

typedef struct tcb {
    const char *name;
    uint8_t prio;
    task_state_t state;
    uint32_t wake_tick;
    void *wait_obj;
    struct tcb *next, *prev;
    uint32_t slice_left, slice;
    bool keep_slice;                /* 연습 5: 선점당해 앞에 선 태스크는 남은 slice 를 유지 */
    uint32_t period;
    int script, burst, left;
    uint32_t ticks_run, activations, max_lat, first_run;
    uint32_t act_tick[64];
    char tl[TL_LEN + 1];
} tcb_t;

static struct { bool preempt, slicing, head_on_preempt; uint32_t slice; } g_cfg;
static tcb_t    g_task[MAX_TASKS];
static int      g_ntask;
static tcb_t   *g_rq_head[NPRIO], *g_rq_tail[NPRIO];
static tcb_t   *g_cur;
static uint32_t g_tick, g_ctx_switches;
static int      g_crit_nest;
static sigset_t g_alrm_set;
static int      g_sched_lock;       /* 연습 4: uxSchedulerSuspended */
static uint32_t g_pended;           /* 연습 4: xPendedTicks */

static void rq_push_tail(tcb_t *t) {
    t->next = NULL; t->prev = g_rq_tail[t->prio];
    if (t->prev) t->prev->next = t; else g_rq_head[t->prio] = t;
    g_rq_tail[t->prio] = t;
}
static void rq_push_head(tcb_t *t) {
    t->prev = NULL; t->next = g_rq_head[t->prio];
    if (t->next) t->next->prev = t; else g_rq_tail[t->prio] = t;
    g_rq_head[t->prio] = t;
}
static void rq_remove(tcb_t *t) {
    if (t->prev) t->prev->next = t->next; else g_rq_head[t->prio] = t->next;
    if (t->next) t->next->prev = t->prev; else g_rq_tail[t->prio] = t->prev;
    t->next = t->prev = NULL;
}
static uint32_t slice_of(const tcb_t *t) { return t->slice ? t->slice : g_cfg.slice; }
static bool higher_ready(uint8_t prio) {
    for (int p = NPRIO - 1; p > prio; p--) if (g_rq_head[p]) return true;
    return false;
}

static void k_reset(bool preempt, bool slicing, uint32_t slice) {
    memset(g_task, 0, sizeof g_task);
    memset(g_rq_head, 0, sizeof g_rq_head); memset(g_rq_tail, 0, sizeof g_rq_tail);
    g_ntask = 0; g_cur = NULL; g_tick = 0; g_ctx_switches = 0; g_crit_nest = 0;
    g_sched_lock = 0; g_pended = 0;
    g_cfg.preempt = preempt; g_cfg.slicing = slicing; g_cfg.slice = slice; g_cfg.head_on_preempt = false;
}
static tcb_t *k_add(const char *name, uint8_t prio, int script, int burst, uint32_t period) {
    tcb_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->state = TASK_READY;
    t->script = script; t->burst = t->left = burst; t->period = period; t->first_run = UINT32_MAX;
    memset(t->tl, '.', TL_LEN);
    rq_push_tail(t);
    return t;
}
static void k_start(void) {
    for (int p = NPRIO - 1; p >= 0 && !g_cur; p--)
        if (g_rq_head[p]) { g_cur = g_rq_head[p]; rq_remove(g_cur); }
    g_cur->state = TASK_RUNNING; g_cur->slice_left = slice_of(g_cur);
}

/* ---- 커널 핵심 (solution.c 와 같음, 연습 5 의 head_on_preempt 분기만 추가) ---- */
static bool kernel_tick(void) {
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
static tcb_t *sched_pick_next(void) {
    if (g_cur && g_cur->state == TASK_RUNNING) {
        g_cur->state = TASK_READY;
        if (g_cfg.head_on_preempt && higher_ready(g_cur->prio) && g_cur->slice_left > 0) {
            g_cur->keep_slice = true;            /* 연습 5: 선점당함 → 줄 맨 앞, 남은 slice 유지 */
            rq_push_head(g_cur);
        } else {
            rq_push_tail(g_cur);
        }
    }
    for (int p = NPRIO - 1; p >= 0; p--) {
        tcb_t *t = g_rq_head[p];
        if (t) {
            rq_remove(t);
            t->state = TASK_RUNNING;
            if (!t->keep_slice) t->slice_left = slice_of(t);
            t->keep_slice = false;
            return t;
        }
    }
    return NULL;
}
static void crit_enter(void) { if (g_crit_nest++ == 0) sigprocmask(SIG_BLOCK, &g_alrm_set, NULL); }
static void crit_exit(void)  { if (--g_crit_nest == 0) sigprocmask(SIG_UNBLOCK, &g_alrm_set, NULL); }
static void port_switch(void) {
    tcb_t *prev = g_cur, *next = sched_pick_next();
    if (next == NULL || next == prev) return;
    g_ctx_switches++;
    g_cur = next;
}
static void task_delay(uint32_t n) {
    crit_enter();
    g_cur->state = TASK_BLOCKED; g_cur->wait_obj = NULL; g_cur->wake_tick = g_tick + n;
    port_switch();
    crit_exit();
}
static void task_yield(void) { crit_enter(); port_switch(); crit_exit(); }

/* ---- 연습 4: 스케줄러 잠금 ---- */
static void sched_suspend(void) { crit_enter(); g_sched_lock++; crit_exit(); }        /* vTaskSuspendAll */
static bool sched_resume(void) {                                                      /* xTaskResumeAll */
    bool need = false;
    crit_enter();
    if (--g_sched_lock == 0) {
        while (g_pended > 0) {                     /* 보류된 tick 을 하나씩 처리 → tick 이 밀리지 않는다 */
            g_tick++;
            if (kernel_tick()) need = true;
            g_pended--;
        }
        if (need) port_switch();
    }
    crit_exit();
    return need;
}
static void tick_isr(void) {
    if (g_sched_lock) { g_pended++; return; }      /* 잠김: 세기만 한다 (tasks.c xPendedTicks += 1) */
    g_tick++;
    if (kernel_tick()) port_switch();
}

/* ---- sim 루프 (solution.c 와 같은 tick 순서 + 각본 확장) ---- */
static uint32_t g_lock_from = UINT32_MAX, g_lock_to = UINT32_MAX;
static void sim_run(uint32_t nticks) {
    for (uint32_t k = 0; k < nticks; k++) {
        if (k > 0) tick_isr();                                           /* ① tick ISR */
        if (k == g_lock_from) sched_suspend();                           /* (연습 4) 지금 도는 태스크가 잠근다 */
        if (k == g_lock_to) sched_resume();
        tcb_t *c = g_cur;
        if (c->script == SCRIPT_PERIODIC && c->left == 0) {              /* ② burst 끝 → 지연 */
            c->left = c->burst; task_delay(c->period - (uint32_t)c->burst); c = g_cur;
        } else if (c->script == SCRIPT_YIELDER && c->left == 0) {        /* ② burst 끝 → 양보 */
            c->left = c->burst; task_yield(); c = g_cur;
        }
        if (c->script == SCRIPT_PERIODIC && c->left == c->burst) {
            uint32_t lat = k - c->wake_tick;
            if (lat > c->max_lat) c->max_lat = lat;
            if (c->activations < 64) c->act_tick[c->activations] = k;
            c->activations++;
        }
        if (c->first_run == UINT32_MAX) c->first_run = k;
        c->ticks_run++;                                                  /* ③ 실행 */
        if (c->script != SCRIPT_CPU) c->left--;
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

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static tcb_t *T(const char *name) {
    for (int i = 0; i < g_ntask; i++) if (strcmp(g_task[i].name, name) == 0) return &g_task[i];
    return &g_task[0];
}
static bool tl_is(const tcb_t *t, const char *s) { return strncmp(t->tl, s, strlen(s)) == 0; }

static void ex1_yield(void) {
    k_reset(true, false, 1);                                    /* slicing 끔 */
    k_add("A", 1, SCRIPT_YIELDER, 3, 0); k_add("B", 1, SCRIPT_YIELDER, 3, 0); k_add("C", 1, SCRIPT_YIELDER, 3, 0);
    k_start(); sim_run(18);
    print_tl("ex1 slicing=0, each task yields after 3 ticks", 18);
    CHECK(tl_is(T("A"), "RRRrrrrrrRRRrrrrrr") && tl_is(T("C"), "rrrrrrRRRrrrrrrRRR"));
    CHECK(g_ctx_switches == 5);
    k_reset(true, false, 1);                                    /* 같은 우선순위에 혼자면 yield 는 그냥 돌아온다 */
    k_add("A", 1, SCRIPT_YIELDER, 1, 0); k_add("I", 0, SCRIPT_CPU, 0, 0);
    k_start(); sim_run(10);
    CHECK(g_ctx_switches == 0 && T("A")->ticks_run == 10 && T("I")->ticks_run == 0);
    k_reset(true, false, 1);                                    /* 양보 안 하는 CPU 태스크 → 다른 둘은 굶는다 */
    k_add("A", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_YIELDER, 3, 0); k_add("C", 1, SCRIPT_YIELDER, 3, 0);
    k_start(); sim_run(18);
    CHECK(T("A")->ticks_run == 18 && T("B")->ticks_run == 0 && T("C")->ticks_run == 0);
}

static void ex2_weighted(void) {
    k_reset(true, true, 1);
    k_add("A", 1, SCRIPT_CPU, 0, 0)->slice = 3;
    k_add("B", 1, SCRIPT_CPU, 0, 0)->slice = 2;
    k_add("C", 1, SCRIPT_CPU, 0, 0)->slice = 1;
    k_start(); sim_run(60);
    printf("ex2 weighted RR (slice 3/2/1, 60 ticks): A=%u B=%u C=%u switches=%u\n",
           T("A")->ticks_run, T("B")->ticks_run, T("C")->ticks_run, g_ctx_switches);
    CHECK(T("A")->ticks_run == 30 && T("B")->ticks_run == 20 && T("C")->ticks_run == 10);
    CHECK(tl_is(T("A"), "RRRrrrRRRrrr") && tl_is(T("B"), "rrrRRrrrrRRr"));
    CHECK(g_ctx_switches == 29);                                 /* 6 tick 에 3번, 마지막 경계(60) 는 시뮬 밖 */
}

static void ex3_worst_wait(void) {
    static const uint32_t ns[] = { 2, 3, 4 }, ss[] = { 1, 2, 5 };
    printf("ex3 worst wait of a newly woken equal-priority task (n CPU tasks, slice s)\n  n s | min max | n*s-1\n");
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
        uint32_t n = ns[a], s = ss[b], mn = UINT32_MAX, mx = 0;
        for (uint32_t ph = 1; ph <= 2 * n * s + 2; ph++) {      /* W 가 깨어나는 위상을 전부 */
            k_reset(true, true, s);
            static const char *nm[] = { "T0", "T1", "T2", "T3" };
            for (uint32_t i = 0; i < n; i++) k_add(nm[i], 1, SCRIPT_CPU, 0, 0);
            tcb_t *w = k_add("W", 1, SCRIPT_CPU, 0, 0);
            rq_remove(w); w->state = TASK_BLOCKED; w->wake_tick = ph;
            k_start(); sim_run(ph + n * s + 2);
            uint32_t wait = w->first_run - ph;
            if (wait < mn) mn = wait;
            if (wait > mx) mx = wait;
        }
        printf("  %u %u | %3u %3u | %u\n", n, s, mn, mx, n * s - 1);
        CHECK(mx == n * s - 1 && mn == (n - 1) * s);
    }
}

static void ex4_suspend_all(void) {
    k_reset(true, true, 1);
    k_add("H", 2, SCRIPT_PERIODIC, 1, 4);
    k_add("L", 1, SCRIPT_CPU, 0, 0);
    k_start();
    g_lock_from = 2; g_lock_to = 9;                              /* L 이 tick 2~8 동안 스케줄러를 잠근다 */
    sim_run(16);
    g_lock_from = g_lock_to = UINT32_MAX;
    print_tl("ex4 L locks the scheduler at tick 2, unlocks at tick 9", 16);
    printf("ex4 H activations=%u at: %u %u %u, max latency %u, g_tick=%u pended=%u\n", T("H")->activations,
           T("H")->act_tick[0], T("H")->act_tick[1], T("H")->act_tick[2],
           T("H")->max_lat, g_tick, g_pended);
    CHECK(T("H")->act_tick[1] == 9 && T("H")->max_lat == 5);     /* tick 4 에 깨어나야 했지만 9 에 */
    CHECK(T("H")->act_tick[2] == 13);                            /* 다음 주기는 9 + 4 (delay 기준) */
    CHECK(g_tick == 15 && g_pended == 0);                        /* tick 은 하나도 잃지 않았다 */
}

static void ex5_head_vs_tail(void) {
    const char *b_tl[2]; static char keep[2][TL_LEN + 1];
    uint32_t turns_short[2];   /* [0]=tail, [1]=head */
    for (int v = 0; v < 2; v++) {
        k_reset(true, true, 2);
        g_cfg.head_on_preempt = v == 1;
        k_add("H", 2, SCRIPT_PERIODIC, 1, 4);
        k_add("HOG", 1, SCRIPT_CPU, 0, 0); k_add("B", 1, SCRIPT_CPU, 0, 0); k_add("C", 1, SCRIPT_CPU, 0, 0);
        k_start(); sim_run(24);
        print_tl(v ? "ex5 preempted -> HEAD (POSIX style), slice 2" : "ex5 preempted -> TAIL (FreeRTOS style), slice 2", 24);
        memcpy(keep[v], T("B")->tl, TL_LEN); b_tl[v] = keep[v];
        uint32_t skipped = 0;                                    /* slice 를 남긴 채 선점당하고 H 뒤에 바로 못 돌아온 횟수 */
        for (int k = 1; k + 1 < 24; k++) {
            if (T("H")->tl[k] != 'R') continue;
            for (int i = 1; i < 4; i++) {
                const char *x = g_task[i].tl;
                int run = 0;                                     /* H 직전까지 연속으로 돈 tick 수 */
                for (int j = k - 1; j >= 0 && x[j] == 'R'; j--) run++;
                if (run > 0 && run < 2 && x[k + 1] != 'R') skipped++;   /* slice 2 를 다 못 쓰고 밀려남 */
            }
        }
        turns_short[v] = skipped;
    }
    printf("ex5 preempted task NOT resumed right after H: tail=%u head=%u\n", turns_short[0], turns_short[1]);
    CHECK(strncmp(b_tl[0], "rrrRrrrrrRRrrrrRrrrrrRRr", 24) == 0);   /* solution.c A2 slice=2 와 같은 결과 */
    CHECK(strncmp(b_tl[1], "rrrRrRrrrrrRrRrrrrrRrRrr", 24) == 0);   /* B: tick 3 → (H) → tick 5 에 나머지 1 tick */
    CHECK(turns_short[0] > 0 && turns_short[1] == 0);
}

int main(void) {
    sigemptyset(&g_alrm_set); sigaddset(&g_alrm_set, SIGALRM);
    ex1_yield();
    ex2_weighted();
    ex3_worst_wait();
    ex4_suspend_all();
    ex5_head_vs_tail();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
