/* RTOS-A08 SMP RTOS: 코어 친화성, 스핀락, 공유 ready list
 *
 * 호스트 모델 — RP2040 같은 듀얼 코어 MCU 위의 SMP RTOS 를 흉내 낸다.
 *   - 코어 = pthread 1개 (Part C). 코어마다 "인터럽트 마스크" 플래그(PRIMASK 대용)는 자기 코어만 막는다.
 *   - 전역 ready list 1개 (우선순위별 FIFO) + 커널 스핀락 1개 (C11 atomics, 코어 단위 재귀 허용).
 *     FreeRTOS SMP 의 pxReadyTasksLists[] + ISR/TASK 스핀락 2개를 락 1개로 줄인 것.
 *   - 태스크마다 코어 친화성 마스크 (bit c = 코어 c 에서 실행 가능) — FreeRTOS uxCoreAffinityMask.
 *   - IPI(코어 간 인터럽트) 대용: 코어마다 atomic 펜딩 플래그 + idle 코어를 깨우는 조건 변수 doorbell.
 *     (RP2040: SIO FIFO 에 한 워드 쓰기 → 상대 코어 SIO_IRQ_PROCn, Cortex-A: GIC SGI)
 *
 * Part A, B 는 스레드 없이 결정적으로 검사한다 (B 는 "n번째 공유 접근 직후 다른 코어가 X 를 한다" 를
 * 훅으로 끼워 넣는 스크립트 인터리빙). Part C 는 진짜 스레드 2개가 스케줄러를 돌리고, 매 실행마다
 * 반드시 성립하는 불변식만 CHECK 한다.
 */
#define _POSIX_C_SOURCE 200809L   /* -std=c11 에서 pthread/nanosleep/sched_yield 선언 (include 전에) */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NCORES    2
#define NPRIO     4                       /* 우선순위 0..3, 클수록 급함 (FreeRTOS 와 같은 방향) */
#define ALL_CORES ((1u << NCORES) - 1u)

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)

static void cpu_relax(void) { sched_yield(); }  /* 타깃에서는 __WFE() / "yield" 힌트 / 그냥 빈 루프 */

/* ======================= 커널 스핀락 (Part A) ======================= */
typedef struct {
    atomic_int locked;            /* 0 = 비었음, 1 = 잡힘. RP2040 이면 SIO SPINLOCKn 레지스터 */
    bool       owned_by[NCORES];  /* 코어 c 는 owned_by[c] 만 읽고 쓴다 (FreeRTOS RP2040 포트 방식) */
    unsigned   recursion;         /* 소유 코어만 건드린다 */
} kspin_t;

/* TODO(A1): 잡으면 true. 같은 코어가 이미 잡고 있으면 재귀 카운트만 올리고 true. 다른 코어가 잡고 있으면 false.
 *           잡을 때 acquire, 놓을 때 release. */
static bool spin_trylock(kspin_t *l, int core) {
    (void)l; (void)core;               /* TODO(A1) */
    return true;
}
/* TODO(A1): 소유자가 아니면 -1 (아무것도 바꾸지 않는다). 재귀 카운트가 0 이 될 때만 실제로 푼다. 성공 0. */
static int spin_unlock(kspin_t *l, int core) {
    (void)l; (void)core;               /* TODO(A1) */
    return 0;
}
/* (blocking 버전은 crit_enter 의 재시도 루프가 대신한다. trylock 이 relaxed load 를 먼저 보므로 TTAS 와 같은 효과) */

/* ======================= 태스크와 전역 ready list (Part A) ======================= */
enum { T_BLOCKED, T_READY, T_RUNNING, T_DONE };
enum { STEP_CONTINUE, STEP_BLOCK, STEP_DONE };
typedef struct task task_t;
typedef int (*step_fn)(task_t *self, int core);
struct task {
    const char *name;
    int         prio;
    unsigned    affinity;        /* bit c = 코어 c 허용 */
    step_fn     fn;
    /* --- 커널 락 보호 --- */
    int         state;
    int         run_core;        /* RUNNING 이면 코어 번호, 아니면 -1 */
    bool        wake_pending;    /* RUNNING 중에 깨우기가 오면 기억 (깨우기 유실 방지) */
    task_t     *next;
    /* --- 이 태스크를 지금 돌리는 코어만 접근 (넘겨줄 때는 커널 락이 순서를 보장) --- */
    unsigned    ran_on;          /* 실행된 코어 비트맵 */
    int         steps, preempted, migrations, last_core, aux;
    atomic_int  on_cpu;          /* 두 코어가 동시에 돌리면 검출 */
};
static task_t *g_rq_head[NPRIO], *g_rq_tail[NPRIO];

static void rq_push(task_t *t) {                       /* 주어진 코드: 같은 우선순위의 꼬리에 */
    t->next = NULL;
    if (g_rq_tail[t->prio]) g_rq_tail[t->prio]->next = t; else g_rq_head[t->prio] = t;
    g_rq_tail[t->prio] = t;
}
/* TODO(A2): core 에서 돌 수 있는(친화성) 태스크 중 우선순위가 가장 높은 것, 같으면 먼저 들어온 것을
 *           리스트에서 빼서 돌려준다. 없으면 NULL (= 이 코어는 idle). */
static task_t *rq_pick(int core) {
    (void)core;                        /* TODO(A2) */
    return NULL;
}

/* ======================= 코어 상태 ======================= */
typedef struct {
    task_t     *cur;             /* 커널 락: 지금 돌리는 태스크 (NULL = idle, 우선순위 -1 로 본다) */
    bool        yield_requested; /* 커널 락: 이미 IPI 를 보냈다 (FreeRTOS taskTASK_SCHEDULED_TO_YIELD) */
    bool        yield_pending;   /* 커널 락: 자기 코어에 미뤄 둔 yield (FreeRTOS xYieldPendings[]) */
    int         ipi_sent;        /* 커널 락: 이 코어로 보낸 IPI 수 */
    bool        irq_masked;      /* 이 코어만: PRIMASK */
    bool        saved_irq;       /* 이 코어만: crit 진입 전 마스크 상태 */
    unsigned    crit_nesting;    /* 이 코어만: FreeRTOS uxCriticalNestings[core] */
    unsigned    uni_depth;       /* 이 코어만: 단일 코어식 임계 구역 깊이 */
    bool        uni_saved;
    int         ipi_rx;          /* 이 코어만: 받은 IPI 수 */
    atomic_bool ipi;             /* IPI 펜딩 비트 */
    pthread_mutex_t m;           /* doorbell: idle 코어가 잠드는 곳 */
    pthread_cond_t  cv;
} core_t;
static core_t  g_core[NCORES];
static kspin_t g_klock;          /* 커널 락: ready list, task 상태, core.cur/yield 플래그 보호 */
static atomic_bool g_stop;

/* ---------- 스크립트 모드 (Part B) 의 추적/훅 — 주어진 코드 ---------- */
static int  g_sim;               /* 1 = 단일 스레드 스크립트 모드 (스레드 시작 전에만 1) */
static int  g_cur_core;          /* 스크립트 모드: 지금 코드를 실행 중인 코어 */
static char g_trace[512];
static void trace(int core, const char *s) {
    if (!g_sim) return;
    size_t n = strlen(g_trace);
    snprintf(g_trace + n, sizeof g_trace - n, "%sc%d:%s", n ? " " : "", core, s);
}
static void run_isr(int core);
static void isr_attempt(int core);
static bool g_isr_pending[NCORES], g_isr_spinning[NCORES];

static bool irq_save(int c) {                          /* __get_PRIMASK(); __disable_irq(); */
    bool was = g_core[c].irq_masked;
    g_core[c].irq_masked = true;
    if (!was) trace(c, "irqoff");
    return was;
}
static void irq_restore(int c, bool was) {             /* __set_PRIMASK(was) — 펜딩 IRQ 는 여기서 들어온다 */
    g_core[c].irq_masked = was;
    if (was) return;
    trace(c, "irqon");
    if (g_sim && g_isr_pending[c]) { g_isr_pending[c] = false; run_isr(c); }
}
static bool lock_try(int c) {                          /* 커널 락 시도 (+추적) */
    bool ok = spin_trylock(&g_klock, c);
    trace(c, ok ? "lock" : "spin");
    return ok;
}
static void lock_release(int c) {                      /* 커널 락 해제 (+추적) — 기다리던 다른 코어가 바로 잡는다 */
    trace(c, "unlock");
    spin_unlock(&g_klock, c);
    if (!g_sim) return;
    for (int o = 0; o < NCORES; o++)
        if (o != c && g_isr_spinning[o]) { g_isr_spinning[o] = false; isr_attempt(o); }
}

/* ======================= SMP 임계 구역 (Part B) ======================= */
/* TODO(B1): 코어 c 의 임계 구역에 들어간다. 이미 들어와 있으면(중첩) 깊이만 +1.
 *           처음이면 ① 자기 코어 인터럽트를 막고 ② 커널 락을 시도한다 (lock_try).
 *           락을 못 잡으면 인터럽트 상태를 되돌리고 false. 잡으면 진입 전 상태를 저장하고 true. */
static bool crit_try_enter(int c) {
    (void)c;                           /* TODO(B1) */
    return true;
}
/* TODO(B1): 깊이 -1. 0 이 되면 ① 커널 락을 풀고 (lock_release) ② 저장해 둔 인터럽트 상태로 되돌린다. */
static void crit_exit(int c) {
    (void)c;                           /* TODO(B1) */
}
static void crit_enter(int c) {                        /* 주어진 코드: taskENTER_CRITICAL() */
    while (!crit_try_enter(c)) cpu_relax();
}

/* 단일 코어식 임계 구역 (주어진 코드): 인터럽트만 막는다. 코어가 하나면 이걸로 충분하다. */
static void uni_enter(int c) { bool was = irq_save(c); if (g_core[c].uni_depth++ == 0) g_core[c].uni_saved = was; }
static void uni_exit(int c)  { if (--g_core[c].uni_depth == 0) irq_restore(c, g_core[c].uni_saved); }

/* ---------- Part B 시나리오: UART RX ISR → 파서 태스크 링 버퍼 (주어진 코드) ---------- */
enum { MODE_UNI, MODE_SMP };
static int g_mode;
static int g_cs_depth[NCORES], g_in_cs, g_in_cs_max;   /* 동시에 "임계 구역 안" 인 코어 수 */
static bool cs_try(int c) {
    bool ok = (g_mode == MODE_UNI) ? (uni_enter(c), true) : crit_try_enter(c);
    if (ok && g_cs_depth[c]++ == 0 && ++g_in_cs > g_in_cs_max) g_in_cs_max = g_in_cs;
    return ok;
}
static void cs_exit(int c) {
    if (--g_cs_depth[c] == 0) g_in_cs--;
    if (g_mode == MODE_UNI) uni_exit(c); else crit_exit(c);
}
static void cs_enter(int c) { while (!cs_try(c)) cpu_relax(); }

#define RB_N 16u
static struct { uint8_t buf[RB_N]; uint32_t head, tail, count; } g_rb;
static uint8_t g_next_byte;
static int g_nacc, g_fire_at = -1, g_fire_core;

static void fire_isr(int core) {                        /* "지금 core 에 UART RX 인터럽트가 온다" */
    if (core == g_cur_core && g_core[core].irq_masked) { g_isr_pending[core] = true; trace(core, "pend"); return; }
    run_isr(core);
}
static uint32_t sh_ld(const uint32_t *p) {             /* 타깃에서는 그냥 *p */
    uint32_t v = *p;
    trace(g_cur_core, "ldC");
    if (++g_nacc == g_fire_at) fire_isr(g_fire_core);
    return v;
}
static void sh_st(uint32_t *p, uint32_t v) {
    *p = v;
    trace(g_cur_core, "stC");
    if (++g_nacc == g_fire_at) fire_isr(g_fire_core);
}
static void isr_attempt(int core) {                    /* ISR 본문: 1바이트 push. 락을 못 잡으면 "spin" 중으로 남는다 */
    int saved = g_cur_core;
    g_cur_core = core;
    if (!cs_try(core)) {
        g_isr_spinning[core] = true;
    } else {
        uint32_t n = sh_ld(&g_rb.count);
        g_rb.buf[g_rb.head] = g_next_byte++;
        g_rb.head = (g_rb.head + 1u) % RB_N;
        sh_st(&g_rb.count, n + 1u);
        cs_exit(core);
    }
    g_cur_core = saved;
}
static void run_isr(int core) { trace(core, "IRQ"); isr_attempt(core); }

static bool rx_pop(int c, uint8_t *b) {                 /* 파서 태스크 쪽 */
    cs_enter(c);
    uint32_t n = sh_ld(&g_rb.count);
    if (n == 0) { cs_exit(c); return false; }
    *b = g_rb.buf[g_rb.tail];
    g_rb.tail = (g_rb.tail + 1u) % RB_N;
    sh_st(&g_rb.count, n - 1u);
    cs_exit(c);
    return true;
}
static uint32_t rb_used(void) { return (g_rb.head + RB_N - g_rb.tail) % RB_N; }   /* head/tail 기준 "진짜" 개수 */

/* ======================= 크로스 코어 yield (Part C) ======================= */
/* TODO(A3): 방금 READY 가 된 t 를 위해 어느 코어를 선점할지 고른다 (FreeRTOS prvYieldForTask).
 *   - t 의 친화성에 있는 코어만, 이미 yield 요청/펜딩이 걸린 코어는 건너뛴다.
 *   - 그 코어의 현재 우선순위 (idle = -1) 가 t->prio 보다 "낮아야" 후보.
 *   - 후보 중 현재 우선순위가 가장 낮은 코어. 같으면 번호가 큰 코어 (FreeRTOS 의 <= 비교 그대로).
 *   - 없으면 -1. (커널 락을 잡은 상태에서 불린다) */
static int pick_core_to_preempt(const task_t *t) {
    (void)t;                           /* TODO(A3) */
    return -1;
}
/* TODO(C1): 코어 c 에 IPI 를 보낸다: 펜딩 비트를 세우고, idle 로 잠든 코어면 깨운다 (깨우기 유실 없이). */
static void ipi_send(int c) {
    (void)c;                           /* TODO(C1) */
}
/* TODO(C2): 코어 from 에서 t 를 깨운다 (xTaskResumeFromISR / xSemaphoreGive 가 태스크를 READY 로 만들 때).
 *   - 커널 임계 구역 안에서.
 *   - BLOCKED 면 READY + ready list 에 넣고, pick_core_to_preempt 로 고른 코어에 yield 를 요청:
 *       그 코어가 from 자신이면 yield_pending (나갈 때 yield), 다른 코어면 yield_requested + ipi_sent++ + ipi_send.
 *   - RUNNING/READY 면 wake_pending 만 세운다. DONE 이면 무시. */
static void make_ready(task_t *t, int from) {
    (void)t; (void)from;               /* TODO(C2) */
}

/* ---------- 코어 루프 (주어진 코드) ---------- */
static atomic_int g_ndone, g_double_run;
static int g_ntasks;
static void idle_wait(int c) {
    pthread_mutex_lock(&g_core[c].m);
    while (!atomic_load_explicit(&g_core[c].ipi, memory_order_acquire) && !atomic_load(&g_stop))
        pthread_cond_wait(&g_core[c].cv, &g_core[c].m);
    pthread_mutex_unlock(&g_core[c].m);
}
static void *core_main(void *arg) {
    int c = (int)(intptr_t)arg;
    task_t *cur = NULL;
    while (!atomic_load(&g_stop)) {
        /* ---- 스케줄 포인트: 커널 락 안에서 현재 태스크를 되돌리고 다음 것을 고른다 ---- */
        crit_enter(c);
        g_core[c].yield_requested = g_core[c].yield_pending = false;
        if (atomic_exchange_explicit(&g_core[c].ipi, false, memory_order_acquire)) g_core[c].ipi_rx++;
        if (cur && cur->state == T_RUNNING) {          /* 선점당함 → READY 로 꼬리에 */
            cur->state = T_READY; cur->run_core = -1; cur->preempted++;
            rq_push(cur);
        }
        task_t *n = rq_pick(c);
        if (n) { n->state = T_RUNNING; n->run_core = c; }
        g_core[c].cur = n;
        crit_exit(c);
        cur = n;
        if (!cur) {                                    /* 이 코어에 돌릴 게 없다: IPI/정지까지 잠든다 */
            idle_wait(c);
            if (atomic_exchange_explicit(&g_core[c].ipi, false, memory_order_acquire)) g_core[c].ipi_rx++;
            continue;
        }
        if (cur->last_core >= 0 && cur->last_core != c) cur->migrations++;
        cur->last_core = c;
        /* ---- 실행: 태스크가 막히거나 끝나거나, yield 가 올 때까지 step 반복 ---- */
        for (;;) {
            if (atomic_exchange(&cur->on_cpu, 1) != 0) atomic_fetch_add(&g_double_run, 1);
            cur->ran_on |= 1u << c;
            int r = cur->fn(cur, c);
            atomic_store(&cur->on_cpu, 0);
            crit_enter(c);
            if (r == STEP_DONE) {
                cur->state = T_DONE; cur->run_core = -1; g_core[c].cur = NULL;
                crit_exit(c);
                atomic_fetch_add(&g_ndone, 1);
                cur = NULL;
                break;
            }
            if (r == STEP_BLOCK) {
                if (cur->wake_pending) {
                    cur->wake_pending = false;         /* 막히기 전에 이미 깨우기가 왔다 → 계속 READY */
                } else {
                    cur->state = T_BLOCKED; cur->run_core = -1; g_core[c].cur = NULL;
                    crit_exit(c);
                    cur = NULL;
                    break;
                }
            }
            bool resched = g_core[c].yield_pending;
            crit_exit(c);
            /* 명령어 경계 = 인터럽트가 들어올 수 있는 곳: IPI 펜딩이면 선점 */
            if (atomic_exchange_explicit(&g_core[c].ipi, false, memory_order_acquire)) { g_core[c].ipi_rx++; resched = true; }
            if (resched || atomic_load(&g_stop)) break;
        }
    }
    return NULL;
}

/* ---------- 리셋 (주어진 코드) ---------- */
static void task_init(task_t *t, const char *name, int prio, unsigned aff, step_fn fn, int state) {
    memset(t, 0, sizeof *t);
    t->name = name; t->prio = prio; t->affinity = aff; t->fn = fn;
    t->state = state; t->run_core = -1; t->last_core = -1;
    atomic_init(&t->on_cpu, 0);
}
static void kernel_reset(void) {
    memset(g_rq_head, 0, sizeof g_rq_head);
    memset(g_rq_tail, 0, sizeof g_rq_tail);
    atomic_store(&g_klock.locked, 0);
    memset(g_klock.owned_by, 0, sizeof g_klock.owned_by);
    g_klock.recursion = 0;
    for (int c = 0; c < NCORES; c++) {
        core_t *k = &g_core[c];
        k->cur = NULL; k->yield_requested = k->yield_pending = false; k->ipi_sent = k->ipi_rx = 0;
        k->irq_masked = k->saved_irq = false; k->crit_nesting = k->uni_depth = 0; k->uni_saved = false;
        atomic_store(&k->ipi, false);
        g_isr_pending[c] = g_isr_spinning[c] = false;
        g_cs_depth[c] = 0;
    }
    g_trace[0] = '\0';
    g_in_cs = g_in_cs_max = 0;
    g_nacc = 0; g_fire_at = -1;
    g_cur_core = 0;
}
static void rb_reset(uint32_t n) {
    memset(&g_rb, 0, sizeof g_rb);
    for (uint32_t i = 0; i < n; i++) { g_rb.buf[i] = (uint8_t)(0xA0 + i); }
    g_rb.head = n; g_rb.count = n;
    g_next_byte = 0x50;
}

/* ======================= Part C 시나리오: 듀얼 코어 모터 컨트롤러 (주어진 코드) ======================= */
#define N_ACT 100           /* 제어 주기 수 */
#define K_LOG 300
#define K_IMU 400
static task_t T_ctl, T_trig, T_bg, T_log, T_imu;
static uint32_t   g_stat;            /* 두 코어의 태스크가 같이 갱신하는 통계 — crit 로 보호 */
static atomic_bool g_ctl_done;

static int ctl_fn(task_t *t, int c) {         /* 모터 전류 제어: core1 고정, 주기마다 1 step */
    crit_enter(c); g_stat++; crit_exit(c);
    t->steps++;
    if (t->steps == N_ACT) atomic_store(&g_ctl_done, true);
    make_ready(&T_trig, c);                   /* "이번 주기 끝" → 타이머 태스크에 알림 */
    return t->steps == N_ACT ? STEP_DONE : STEP_BLOCK;
}
static int trig_fn(task_t *t, int c) {        /* PWM 주기 타이머 태스크: core0 고정 */
    if (t->aux == N_ACT) return STEP_DONE;
    crit_enter(c);
    if (T_ctl.state != T_BLOCKED) { crit_exit(c); return STEP_CONTINUE; }   /* 제어 태스크가 아직 안 잠듦 */
    t->aux++;
    make_ready(&T_ctl, c);                    /* 중첩 임계 구역: crit 안에서 crit */
    crit_exit(c);
    t->steps++;
    return STEP_BLOCK;
}
static int bg_fn(task_t *t, int c) {          /* 백그라운드 보정 루프: core1 고정, 절대 안 막힌다 */
    (void)c;
    t->steps++;
    return atomic_load(&g_ctl_done) ? STEP_DONE : STEP_CONTINUE;
}
static int work_fn(task_t *t, int c) {        /* 로거(core0) / IMU 융합(아무 코어): K 번 통계 갱신 */
    crit_enter(c); g_stat++; crit_exit(c);
    t->steps++;
    return t->steps == t->aux ? STEP_DONE : STEP_CONTINUE;
}

static bool run_two_cores(int timeout_ms) {
    pthread_t th[NCORES];
    atomic_store(&g_stop, false);
    atomic_store(&g_ndone, 0);
    atomic_store(&g_double_run, 0);
    g_sim = 0;
    for (int c = 0; c < NCORES; c++) pthread_create(&th[c], NULL, core_main, (void *)(intptr_t)c);
    struct timespec ms = {0, 1000000};
    int waited = 0;
    while (atomic_load(&g_ndone) < g_ntasks && waited < timeout_ms) { nanosleep(&ms, NULL); waited++; }
    bool ok = atomic_load(&g_ndone) == g_ntasks;
    atomic_store(&g_stop, true);
    for (int c = 0; c < NCORES; c++) {
        pthread_mutex_lock(&g_core[c].m);
        pthread_cond_broadcast(&g_core[c].cv);
        pthread_mutex_unlock(&g_core[c].m);
    }
    for (int c = 0; c < NCORES; c++) pthread_join(th[c], NULL);
    g_sim = 1;
    return ok;
}

/* ======================= 테스트 ======================= */
static int dummy_fn(task_t *t, int c) { (void)t; (void)c; return STEP_DONE; }

static void test_part_a(void) {
    printf("-- Part A: 스핀락 / ready list / 선점 코어 선택\n");
    kspin_t l;
    memset(&l, 0, sizeof l);
    atomic_init(&l.locked, 0);
    CHECK(spin_trylock(&l, 0) == true);
    CHECK(spin_trylock(&l, 1) == false);                  /* 다른 코어는 못 잡는다 */
    CHECK(spin_trylock(&l, 0) == true && l.recursion == 2);   /* 같은 코어 재귀 */
    CHECK(spin_unlock(&l, 1) == -1 && atomic_load(&l.locked) == 1);   /* 소유자 아님 → 거절 */
    CHECK(spin_unlock(&l, 0) == 0 && spin_trylock(&l, 1) == false);  /* 아직 1단 남음 */
    CHECK(spin_unlock(&l, 0) == 0 && atomic_load(&l.locked) == 0);
    CHECK(spin_trylock(&l, 1) == true && spin_unlock(&l, 1) == 0);
    CHECK(spin_unlock(&l, 0) == -1);                      /* 안 잡았는데 풀기 */

    kernel_reset();
    task_t A, B, C, D, E;
    task_init(&A, "A", 2, ALL_CORES, dummy_fn, T_READY);
    task_init(&B, "B", 3, 1u << 1, dummy_fn, T_READY);
    task_init(&C, "C", 2, 1u << 0, dummy_fn, T_READY);
    task_init(&D, "D", 1, ALL_CORES, dummy_fn, T_READY);
    task_init(&E, "E", 2, ALL_CORES, dummy_fn, T_READY);
    rq_push(&A); rq_push(&B); rq_push(&C); rq_push(&D);
    CHECK(rq_pick(0) == &A);            /* B(3)는 core1 전용 → core0 은 2 순위의 첫 번째 */
    CHECK(rq_pick(0) == &C);
    CHECK(rq_pick(0) == &D);
    CHECK(rq_pick(0) == NULL);          /* B 만 남았는데 core0 금지 → idle */
    CHECK(rq_pick(1) == &B);
    rq_push(&C); rq_push(&E); rq_push(&A);
    CHECK(rq_pick(1) == &E);            /* C 는 core0 전용 → 건너뛰고 E, 그다음 A (FIFO 유지) */
    CHECK(rq_pick(1) == &A && g_rq_head[2] == &C && g_rq_tail[2] == &C);

    kernel_reset();
    task_t r0, r1, t;
    task_init(&r0, "r0", 2, ALL_CORES, dummy_fn, T_RUNNING);
    task_init(&r1, "r1", 1, ALL_CORES, dummy_fn, T_RUNNING);
    task_init(&t, "t", 1, ALL_CORES, dummy_fn, T_READY);
    CHECK(pick_core_to_preempt(&t) == 1);       /* 둘 다 idle(-1) → 같으면 번호 큰 코어 */
    g_core[0].cur = &r0; g_core[1].cur = &r1;
    t.prio = 3; CHECK(pick_core_to_preempt(&t) == 1);   /* 가장 낮은 우선순위(1)를 돌리는 코어 */
    t.prio = 2; CHECK(pick_core_to_preempt(&t) == 1);
    t.prio = 1; CHECK(pick_core_to_preempt(&t) == -1);  /* 같은 우선순위는 선점하지 않는다 */
    t.prio = 3; t.affinity = 1u << 0; CHECK(pick_core_to_preempt(&t) == 0);   /* 친화성 */
    t.affinity = ALL_CORES; g_core[1].yield_requested = true;
    CHECK(pick_core_to_preempt(&t) == 0);       /* core1 은 이미 IPI 받음 → 다음 후보 */
    g_core[0].yield_pending = true;
    CHECK(pick_core_to_preempt(&t) == -1);
}

static void test_part_b(void) {
    printf("-- Part B: 인터럽트 끄기 != 상호 배제 (UART RX 링 버퍼)\n");
    uint8_t b;
    /* B0 (주어진 재현): 단일 코어식 임계 구역, UART ISR 은 core1 로 라우팅 */
    kernel_reset(); rb_reset(5); g_mode = MODE_UNI; g_fire_at = 1; g_fire_core = 1;
    CHECK(rx_pop(0, &b) && b == 0xA0);
    printf("      trace: %s\n", g_trace);
    CHECK(g_rb.count == 4 && rb_used() == 5);   /* 버그 재현: 바이트 1개가 count 에서 사라짐 */
    CHECK(g_in_cs_max == 2);                    /* 두 코어가 동시에 "임계 구역" 안 */
    /* B0' (주어진): 같은 방식이라도 ISR 이 같은 코어(core0)면 펜딩 → 안전. 단일 코어 가정은 거기선 맞다 */
    kernel_reset(); rb_reset(5); g_mode = MODE_UNI; g_fire_at = 1; g_fire_core = 0;
    CHECK(rx_pop(0, &b) && g_rb.count == 5 && rb_used() == 5);

    /* B1: SMP 임계 구역 — 다른 코어의 ISR 은 락에서 기다렸다가 내가 풀면 들어온다 */
    kernel_reset(); rb_reset(5); g_mode = MODE_SMP; g_fire_at = 1; g_fire_core = 1;
    CHECK(rx_pop(0, &b) && b == 0xA0);
    printf("      trace: %s\n", g_trace);
    CHECK(g_rb.count == 5 && rb_used() == 5 && g_in_cs_max == 1);
    CHECK(strcmp(g_trace, "c0:irqoff c0:lock c0:ldC c1:IRQ c1:irqoff c1:spin c1:irqon c0:stC c0:unlock "
                          "c1:irqoff c1:lock c1:ldC c1:stC c1:unlock c1:irqon c0:irqon") == 0);
    /* B2: 같은 코어 ISR 은 펜딩 → 락을 푼 "뒤" 인터럽트를 켜는 순간 들어온다 */
    kernel_reset(); rb_reset(5); g_mode = MODE_SMP; g_fire_at = 1; g_fire_core = 0;
    CHECK(rx_pop(0, &b) && g_rb.count == 5 && rb_used() == 5);
    CHECK(strcmp(g_trace, "c0:irqoff c0:lock c0:ldC c0:pend c0:stC c0:unlock c0:irqon "
                          "c0:IRQ c0:irqoff c0:lock c0:ldC c0:stC c0:unlock c0:irqon") == 0);
    /* B3: 중첩 + 저장/복원 */
    kernel_reset();
    CHECK(crit_try_enter(0) && crit_try_enter(0) && g_core[0].crit_nesting == 2);
    CHECK(crit_try_enter(1) == false && g_core[1].irq_masked == false);   /* 못 잡으면 마스크 원상복구 */
    crit_exit(0);
    CHECK(g_core[0].irq_masked && crit_try_enter(1) == false);           /* 아직 바깥 구역 */
    crit_exit(0);
    CHECK(!g_core[0].irq_masked && crit_try_enter(1) == true);
    crit_exit(1);
    bool was = irq_save(0);                               /* 호출자가 이미 막아 둔 상태에서 */
    CHECK(crit_try_enter(0));
    crit_exit(0);
    CHECK(g_core[0].irq_masked && atomic_load(&g_klock.locked) == 0);    /* 마스크는 호출자 상태 그대로 */
    irq_restore(0, was);
}

static void test_part_c(void) {
    printf("-- Part C: 듀얼 코어 스케줄러 (make_ready / IPI / 친화성)\n");
    /* C1: 단위 — make_ready 의 결정 */
    kernel_reset();
    task_t run1, run0, hi1, hi0, x;
    task_init(&run1, "bg", 1, 1u << 1, dummy_fn, T_RUNNING);
    task_init(&run0, "lo", 1, 1u << 0, dummy_fn, T_RUNNING);
    task_init(&hi1, "ctl", 3, 1u << 1, dummy_fn, T_BLOCKED);
    task_init(&hi0, "trig", 2, 1u << 0, dummy_fn, T_BLOCKED);
    task_init(&x, "x", 2, ALL_CORES, dummy_fn, T_RUNNING);
    g_core[0].cur = &run0; g_core[1].cur = &run1;
    make_ready(&hi1, 0);                                  /* core0 이 core1 전용 태스크를 깨움 */
    CHECK(hi1.state == T_READY && g_rq_head[3] == &hi1);
    CHECK(g_core[1].yield_requested && g_core[1].ipi_sent == 1 && atomic_load(&g_core[1].ipi));
    make_ready(&hi0, 0);                                  /* 자기 코어 → IPI 대신 yield_pending */
    CHECK(g_core[0].yield_pending && g_core[0].ipi_sent == 0 && !atomic_load(&g_core[0].ipi));
    make_ready(&x, 1);                                    /* 이미 RUNNING → 깨우기 기억만 */
    CHECK(x.wake_pending && g_rq_head[2] == &hi0 && hi0.next == NULL);
    CHECK(g_core[0].crit_nesting == 0 && g_core[1].crit_nesting == 0 && atomic_load(&g_klock.locked) == 0);

    /* C2: 진짜 스레드 2개 */
    kernel_reset();
    task_init(&T_ctl,  "ctl",  3, 1u << 1,  ctl_fn,  T_BLOCKED);
    task_init(&T_trig, "trig", 2, 1u << 0,  trig_fn, T_READY);
    task_init(&T_bg,   "bg",   1, 1u << 1,  bg_fn,   T_READY);
    task_init(&T_log,  "log",  1, 1u << 0,  work_fn, T_READY); T_log.aux = K_LOG;
    task_init(&T_imu,  "imu",  2, ALL_CORES, work_fn, T_READY); T_imu.aux = K_IMU;
    rq_push(&T_trig); rq_push(&T_bg); rq_push(&T_log); rq_push(&T_imu);
    g_ntasks = 5; g_stat = 0; atomic_store(&g_ctl_done, false);
    bool done = run_two_cores(3000);
    CHECK(done);
    CHECK(T_ctl.steps == N_ACT && T_trig.aux == N_ACT);
    CHECK(g_stat == N_ACT + K_LOG + K_IMU);                /* 두 코어가 같이 쓴 통계: 유실 0 */
    CHECK(T_ctl.ran_on == 2u && T_bg.ran_on == 2u && T_trig.ran_on == 1u && T_log.ran_on == 1u);
    CHECK((T_imu.ran_on & ~ALL_CORES) == 0u);
    CHECK(atomic_load(&g_double_run) == 0);               /* 한 태스크가 두 코어에서 동시에 돈 적 없음 */
    CHECK(g_core[1].ipi_sent == N_ACT && g_core[1].ipi_rx == N_ACT);   /* 매 주기 IPI 1번, 유실/중복 없음 */
    printf("      info: ipi core0 sent/rx=%d/%d  core1=%d/%d  imu ran_on=0x%x migrations=%d  "
           "bg steps=%d preempted=%d  imu preempted=%d\n",
           g_core[0].ipi_sent, g_core[0].ipi_rx, g_core[1].ipi_sent, g_core[1].ipi_rx,
           T_imu.ran_on, T_imu.migrations, T_bg.steps, T_bg.preempted, T_imu.preempted);
}

int main(void) {
    (void)lock_try; (void)lock_release; (void)ipi_send;   /* 시작 코드에서 "안 쓰는 함수" 경고 방지 */
    for (int c = 0; c < NCORES; c++) {
        pthread_mutex_init(&g_core[c].m, NULL);
        pthread_cond_init(&g_core[c].cv, NULL);
        atomic_init(&g_core[c].ipi, false);
    }
    g_sim = 1;
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
