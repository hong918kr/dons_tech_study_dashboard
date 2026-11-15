/* RTOS-A02 Tickless idle: 다음 만료 계산과 tick 보정
 *
 * 호스트 모델 — 시뮬레이션 시간 (진짜로 자지 않는다, sleep/usleep 없음).
 *   - 시간 단위 = 타이머 클럭 1개(count). g_now = "진짜 시간" (64-bit, 벽시계 역할).
 *   - ST = SysTick 같은 24-bit 다운 카운터 (LOAD / VAL / ENABLE / COUNTFLAG) + NVIC 펜딩 비트 2개
 *     (g_pend_tick = SysTick, g_pend_ext = 외부 IRQ 하나) + PRIMASK (g_primask).
 *     모델 규칙 (ARMv7-M SysTick 설명 + FreeRTOS port.c 주석의 "core clock 이면 enable 즉시 LOAD 적재" 기준):
 *       start: ENABLE=1, VAL 이 0 이면 그 순간 LOAD 를 싣는다      → LOAD=X 로 start 하면 X 클럭 뒤 첫 IRQ
 *       매 클럭: VAL>0 이면 1 감소, 1→0 순간 COUNTFLAG=1 + IRQ 펜딩 / VAL==0 이면 그 클럭에 LOAD 재적재
 *                                                                  → 주기 모드 LOAD=N-1 이면 주기 N
 *       VAL 쓰기 = 0 으로 클리어 + COUNTFLAG 클리어, CTRL 읽기 = COUNTFLAG 읽고 클리어.
 *   - 미니 커널: tick(32-bit, wrap), 지연 태스크(delay_until), 외부 IRQ 가 세마포어로 깨우는 이벤트 태스크,
 *     스케줄러 정지(suspend_all) + pended tick + pending-ready 리스트 (FreeRTOS xPendedTicks / xPendingReadyList).
 *     ISR 실행 시간은 0 (시간 보정 문제와 무관). 태스크는 정해진 클럭만큼 돌고 막힌다 (run-to-completion).
 *   - idle_task_step() = FreeRTOS prvIdleTask 의 tickless 부분 (tasks.c:5980-6024) 그대로.
 *   - port_suppress_ticks_and_sleep() = portable/GCC/ARM_CM4F/port.c vPortSuppressTicksAndSleep (589-803) 축소판.
 * 검사는 전부 결정적 (시뮬레이션 시간 + 고정 시드 xorshift32). 정지 구간 시간 = 0 인 이상적 모델 →
 * 올바른 구현의 드리프트는 정확히 0 이어야 한다 (정지 구간 보정은 practice.c 연습 2).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)

/* ======================= 설정 (vPortSetupTimerInterrupt, port.c:817-819) ======================= */
#define MAX_24BIT                       0xFFFFFFu     /* portMAX_24_BIT_NUMBER */
#define EXPECTED_IDLE_TIME_BEFORE_SLEEP 2u            /* configEXPECTED_IDLE_TIME_BEFORE_SLEEP 기본값 (>= 2 강제) */
#define PORT_MAX_DELAY                  0xFFFFFFFFu
#define NEVER                           UINT64_MAX
static uint32_t g_N;          /* ulTimerCountsForOneTick = 타이머 클럭 / tick 주파수 */
static uint32_t g_max_supp;   /* xMaximumPossibleSuppressedTicks = 0xFFFFFF / N */

/* ======================= 시뮬레이션 시간 + SysTick 모델 ======================= */
typedef struct { uint32_t load, val; bool en, countflag; } systick_t;
static systick_t ST;
static uint64_t  g_now;                         /* 진짜 시간 (클럭) */
static bool      g_pend_tick, g_pend_ext;       /* NVIC 펜딩 */
static bool      g_primask;                     /* true = cpsid i 상태 */

static void st_write_val(void) { ST.val = 0; ST.countflag = false; }          /* SYST_CVR 쓰기 */
static void st_start(void)     { ST.en = true; if (ST.val == 0) ST.val = ST.load; }
static void st_stop(void)      { ST.en = false; }
static bool st_read_countflag(void) { bool f = ST.countflag; ST.countflag = false; return f; }
static uint64_t st_clocks_to_irq(void) {
    if (!ST.en) return NEVER;
    return ST.val ? ST.val : (uint64_t)ST.load + 1u;     /* 0 이면 재적재 1 클럭 + LOAD 번 감소 */
}
static void st_advance(uint64_t d) {
    while (ST.en && d > 0) {
        uint64_t k = st_clocks_to_irq();
        if (d < k) { ST.val = ST.val ? ST.val - (uint32_t)d : ST.load - (uint32_t)(d - 1u); return; }
        d -= k; ST.val = 0; ST.countflag = true; g_pend_tick = true;
    }
}

/* 외부 IRQ 일정 (예: 가속도계 모션 인터럽트) — 오름차순 시각 */
#define MAX_EXT 4096
static uint64_t g_ext[MAX_EXT];
static int      g_ext_n, g_ext_i;
static uint64_t ext_next(void) { return g_ext_i < g_ext_n ? g_ext[g_ext_i] : NEVER; }

/* ======================= 미니 커널 ======================= */
typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
typedef struct tcb {
    const char  *name;
    uint8_t      prio;            /* 클수록 급함 (FreeRTOS 방향) */
    task_state_t state;
    uint32_t     wake_tick;       /* delayed 일 때 깰 tick (절대값, wrap) */
    bool         delayed;         /* 지연 리스트에 있음 */
    bool         pending_ready;   /* xPendingReadyList (스케줄러 정지 중에 깨워짐) */
    uint32_t     period;          /* 0 = 이벤트 태스크 (외부 IRQ 세마포어로만 깸) */
    uint32_t     work;            /* 한 번 실행에 쓰는 클럭 */
    bool         sem;             /* 이벤트 태스크의 바이너리 세마포어 */
    uint64_t     irq_at, lat_max; /* IRQ → 태스크 시작 지연 */
    uint32_t     runs;
    uint64_t     last_start;
} tcb_t;
#define MAXT 4
static tcb_t    g_task[MAXT];
static int      g_ntask;
static tcb_t   *g_evt;               /* 외부 IRQ 가 깨우는 태스크 */
static uint32_t g_tick;              /* xTickCount */
static uint64_t g_ticks_total;       /* 부팅 이후 tick 총합 (검사용, wrap 없음) */
static unsigned g_sched_suspended;   /* uxSchedulerSuspended */
static uint32_t g_pended;            /* xPendedTicks */

typedef struct {
    uint64_t sleep_clk, wakeups, tick_isrs, sleeps, aborts, pend_cleared;
    int64_t  err_last, err_max_abs;  uint64_t err_checks;   /* tick ISR 시각 − 커널 tick × N */
    int64_t  wake_err_max_abs;                              /* 태스크가 깬 시각 − 그 tick 경계 */
    uint64_t late_wakes, step_asserts, overruns, max_sleep_ticks, stuck;
    uint64_t hash;                                          /* (태스크, 시작 시각) 열의 FNV-1a */
} stats_t;
static stats_t S;

static int64_t abs64(int64_t x) { return x < 0 ? -x : x; }
static void hash_mix(uint64_t x) { for (int i = 0; i < 8; i++) { S.hash ^= (x >> (8 * i)) & 0xFFu; S.hash *= 1099511628211ull; } }

/* xNextTaskUnblockTime - xTickCount (지연 태스크가 없으면 FreeRTOS 처럼 portMAX_DELAY - tick) */
static uint32_t next_unblock_delta(void) {
    uint32_t best = PORT_MAX_DELAY - g_tick; bool any = false;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (t->state == TASK_BLOCKED && t->delayed) {
            uint32_t d = t->wake_tick - g_tick;
            if (!any || d < best) { best = d; any = true; }
        }
    }
    return best;
}

static void kernel_tick_increment(void) {        /* xTaskIncrementTick 축소판 */
    g_tick++; g_ticks_total++;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (t->state != TASK_BLOCKED || !t->delayed) continue;
        if (t->wake_tick == g_tick || (int32_t)(g_tick - t->wake_tick) > 0) {
            if (t->wake_tick != g_tick) S.late_wakes++;           /* step 이 만료를 건너뛰었다 = 버그 */
            t->delayed = false; t->state = TASK_READY;
            int64_t e = abs64((int64_t)g_now - (int64_t)(g_ticks_total * g_N));
            if (e > S.wake_err_max_abs) S.wake_err_max_abs = e;
        }
    }
}
static void phase_err(int64_t err) {             /* 드리프트 기록: 0 이 아니면 커널 시계가 진짜 시간과 어긋난 것 */
    S.err_last = err; S.err_checks++;
    if (abs64(err) > S.err_max_abs) S.err_max_abs = abs64(err);
}
static void tick_isr(void) {                     /* xPortSysTickHandler → xTaskIncrementTick */
    S.tick_isrs++;
    if (g_sched_suspended) { g_pended++; return; }               /* tasks.c:5064 xPendedTicks += 1 */
    kernel_tick_increment();
    phase_err((int64_t)g_now - (int64_t)(g_ticks_total * g_N)); /* 검사 1: tick ISR 시각 = k·N */
}
static void ext_isr(void) {                      /* 모션 IRQ → xSemaphoreGiveFromISR */
    tcb_t *t = g_evt;
    if (!t) return;
    if (t->state == TASK_BLOCKED && !t->delayed && !t->pending_ready) {
        t->irq_at = g_now;
        if (g_sched_suspended) t->pending_ready = true;          /* tasks.c:5546-5551 */
        else t->state = TASK_READY;
    } else {
        t->sem = true;                                           /* 이미 깨어 있음 → 합쳐짐 (RTOS-B08) */
    }
}
static void service_irqs(void) {
    while (!g_primask && (g_pend_ext || g_pend_tick)) {
        if (g_pend_ext) { g_pend_ext = false; ext_isr(); }       /* 모션 IRQ 가 SysTick 보다 우선 */
        else            { g_pend_tick = false; tick_isr(); }
    }
}
/* d 클럭 동안 CPU 가 깨어 있다 (태스크 실행). 그 사이 IRQ 는 정확한 시각에 처리 (PRIMASK=0 일 때). */
static void sim_advance(uint64_t d) {
    uint64_t end = g_now + d;
    for (;;) {
        uint64_t tt = st_clocks_to_irq(); if (tt != NEVER) tt += g_now;
        uint64_t te = ext_next();
        uint64_t nx = tt < te ? tt : te;
        if (nx > end) { st_advance(end - g_now); g_now = end; return; }
        st_advance(nx - g_now); g_now = nx;
        if (te == nx) { g_pend_ext = true; g_ext_i++; }
        service_irqs();
    }
}
/* __WFI(): 펜딩 IRQ 가 있으면 바로 통과. PRIMASK=1 이어도 펜딩 IRQ 가 깨운다 (BM-M11). */
static bool sim_wfi(void) {
    if (!(g_pend_tick || g_pend_ext)) {
        uint64_t tt = st_clocks_to_irq(); if (tt != NEVER) tt += g_now;
        uint64_t te = ext_next();
        uint64_t nx = tt < te ? tt : te;
        if (nx == NEVER) return false;
        S.sleep_clk += nx - g_now; S.wakeups++;
        st_advance(nx - g_now); g_now = nx;
        if (te == nx) { g_pend_ext = true; g_ext_i++; }
    }
    service_irqs();
    return true;
}

static void (*g_hook_after_cpsid)(void);          /* 테스트: cpsid 직후 (정지 전) 에 시간이 흐른다 */
static void (*g_hook_before_suppress)(void);      /* 테스트: 재계산 직후, cpsid 전에 IRQ 가 온다 */
static void port_cpsid(void) {
    g_primask = true;
    if (g_hook_after_cpsid) { void (*h)(void) = g_hook_after_cpsid; g_hook_after_cpsid = NULL; h(); }
}
static void port_cpsie(void) { g_primask = false; service_irqs(); }
static void suspend_all(void) { g_sched_suspended++; }           /* vTaskSuspendAll */
static void resume_all(void) {                                   /* xTaskResumeAll (tasks.c:4097-4171) */
    if (--g_sched_suspended) return;
    for (int i = 0; i < g_ntask; i++)
        if (g_task[i].pending_ready) { g_task[i].pending_ready = false; g_task[i].state = TASK_READY; }
    while (g_pended) { g_pended--; kernel_tick_increment(); }
}

typedef enum { SLEEP_ABORT, SLEEP_STANDARD, SLEEP_NO_TIMEOUT } sleep_status_t;

/* ======================= Part A — 다음 만료 계산, 단발 재프로그램, 만료 시 보정 ======================= */

/* TODO(A1): prvGetExpectedIdleTime. idle 이 아닌 태스크가 READY 면 0. 아니면 다음 지연 만료까지 남은 tick
 *           (next_unblock_delta(): wrap 안전한 xNextTaskUnblockTime - xTickCount). */
static uint32_t expected_idle_ticks(void) {
    return 0;                                             /* TODO(A1) */
}

/* TODO(A2): 잠들 때 실을 reload 값. val = 정지 순간 VAL (0 이면 한 tick 통째로 남은 것),
 *           expected tick 뒤의 tick 경계에서 IRQ 가 나도록. tick IRQ 가 이미 펜딩이면 그 tick 은 지났다 → N 만큼 뺀다. */
static uint32_t tl_reload_value(uint32_t val, uint32_t expected, bool tick_pending) {
    (void)val; (void)expected; (void)tick_pending;        /* TODO(A2) */
    return 0;
}

/* TODO(A3): 타이머 만료로 깼다. IRQ 이후 흐른 count d 를 VAL 로 구해 다음 tick 경계까지 남은 count (N - d) 를 돌려준다.
 *           IRQ 직후 VAL 은 0, 그다음 클럭에 reload 가 다시 실려 줄어든다. d >= N 이면 이미 한 tick 이상 놓친 것 → N. */
static uint32_t tl_expiry_remainder(uint32_t reload, uint32_t val) {
    (void)reload; (void)val;                              /* TODO(A3) */
    return 0;
}

static uint32_t tl_early_remainder(uint32_t expected, uint32_t val, uint32_t *complete);
static sleep_status_t confirm_sleep_status(void);
static void kernel_step_tick(uint32_t n);

/* TODO(A4): vPortSuppressTicksAndSleep. 순서: 상한 → cpsid → 확인(중단이면 cpsie 후 복귀) → 정지 → reload 계산
 *           (펜딩 tick 처리) → LOAD/VAL/start → wfi → cpsie(깨운 ISR 실행) → cpsid → 정지 → (a) COUNTFLAG 면 만료,
 *           (b) 아니면 조기 기상 → 남은 조각으로 재시작 후 LOAD=N-1 → step → cpsie. 통계: sleeps, max_sleep_ticks,
 *           aborts, pend_cleared. */
static void port_suppress_ticks_and_sleep(uint32_t expected) {
    (void)expected;                                       /* TODO(A4): 지금은 tick 을 끄지 않고 그냥 WFI */
    sim_wfi();
}

/* ======================= Part B — 조기 기상, 중단, step 경계 ======================= */

/* TODO(B1): 조기 기상. val = 정지 순간 VAL (단발 IRQ 까지 남은 count). 기준 tick 경계 이후 흐른 count 를 구해
 *           *complete = 지난 "온전한" tick 수, 반환 = 다음 tick 경계까지 남은 count (1..N). */
static uint32_t tl_early_remainder(uint32_t expected, uint32_t val, uint32_t *complete) {
    (void)expected; (void)val;                            /* TODO(B1) */
    *complete = 0;
    return 0;
}

/* TODO(B2): eTaskConfirmSleepModeStatus (tasks.c:6045-6093). 순서대로: pending-ready 태스크가 있으면 ABORT,
 *           pended tick 이 있으면 ABORT, 지연(타임아웃) 중인 태스크가 하나도 없으면 NO_TIMEOUT, 아니면 STANDARD. */
static sleep_status_t confirm_sleep_status(void) {
    return SLEEP_STANDARD;                                /* TODO(B2) */
}

/* TODO(B3): vTaskStepTick (tasks.c:4665-4701). n 이 다음 만료를 넘으면 S.step_asserts++ 하고 잘라낸다 (configASSERT).
 *           n 이 정확히 다음 만료까지라면 마지막 1 tick 은 g_pended 로 넘겨 (resume_all 의 tick 처리가 태스크를 깨우게)
 *           n-1 만 더한다. g_tick, g_ticks_total 둘 다 갱신. */
static void kernel_step_tick(uint32_t n) {
    (void)n;                                              /* TODO(B3) */
}

/* ======================= Part C — 배터리 센서 노드 ======================= */
#define I_RUN_NA   1500000ull     /* Run 1.5 mA (BM-M11 모델 값, 특정 칩 아님) */
#define I_SLEEP_NA  400000ull     /* Sleep (WFI, 클럭 살아 있음) 400 µA */
#define I_STOP_NA     1800ull     /* Stop + LPTIM 1.8 µA */
#define WAKE_CLK       480u       /* 깰 때마다 run 전류로 쓰는 클럭 (진입/복귀 + tick 처리 ≈ 10 µs @48 MHz) */

/* TODO(C1): 평균 전류 [nA] = (깨어 있던 클럭·I_RUN + 잔 클럭·i_sleep + 깨어남 횟수·WAKE_CLK·(I_RUN − i_sleep)) / 전체 클럭. */
static uint64_t avg_current_nA(const stats_t *s, uint64_t total_clk, uint64_t i_sleep_nA) {
    (void)s; (void)total_clk; (void)i_sleep_nA;           /* TODO(C1) */
    return 0;
}

/* ======================= 주어진 코드: 스케줄러, idle, 시나리오 ======================= */
static tcb_t *sched_pick_next(void) {
    tcb_t *best = NULL;
    for (int i = 0; i < g_ntask; i++)
        if (g_task[i].state == TASK_READY && (!best || g_task[i].prio > best->prio)) best = &g_task[i];
    return best;
}
static void run_task(tcb_t *t) {
    t->state = TASK_RUNNING; t->runs++; t->last_start = g_now;
    if (t->irq_at != NEVER) { uint64_t l = g_now - t->irq_at; if (l > t->lat_max) t->lat_max = l; t->irq_at = NEVER; }
    hash_mix((uint64_t)(t - g_task)); hash_mix(g_now);
    sim_advance(t->work);
    if (t->period) {                                              /* xTaskDelayUntil(&wake, period) */
        t->wake_tick += t->period;
        if ((int32_t)(t->wake_tick - g_tick) <= 0) { S.overruns++; t->state = TASK_READY; return; }
        t->delayed = true; t->state = TASK_BLOCKED;
    } else if (t->sem) { t->sem = false; t->state = TASK_READY; }
    else t->state = TASK_BLOCKED;
}

typedef void (*suppress_fn)(uint32_t);
static suppress_fn g_suppress;           /* NULL = 보통 idle (WFI 만, tick 마다 깸) */
static void idle_task_step(void) {       /* prvIdleTask 의 tickless 부분 (tasks.c:5980-6024) */
    if (g_suppress) {
        uint32_t e = expected_idle_ticks();                  /* 1차: 스케줄러 정지 없이 (값은 참고용) */
        if (e >= EXPECTED_IDLE_TIME_BEFORE_SLEEP) {
            suspend_all();
            e = expected_idle_ticks();                       /* 2차: 이제 믿을 수 있다 */
            if (g_hook_before_suppress) { void (*h)(void) = g_hook_before_suppress; g_hook_before_suppress = NULL; h(); }
            if (e >= EXPECTED_IDLE_TIME_BEFORE_SLEEP) {
                g_suppress(e);
                /* 검사 2: 깬 직후, 다음 tick IRQ 시각 = (커널이 센 tick + 아직 처리 안 한 tick + 1)·N */
                phase_err((int64_t)(g_now + st_clocks_to_irq()) - (int64_t)((g_ticks_total + g_pended + 1u) * g_N));
            }
            resume_all();
            return;
        }
    }
    sim_wfi();                           /* vApplicationIdleHook 의 __WFI(): 다음 IRQ(= 다음 tick)까지 */
}

/* 흔한 첫 구현 (비교용, 주어진 코드): 확인 없이 잠들고, 조기 기상 때 남은 조각 대신 한 주기 통째로 재시작 */
static void port_suppress_naive(uint32_t expected) {
    if (expected > g_max_supp) expected = g_max_supp;
    port_cpsid();
    st_stop();
    uint32_t left = ST.val ? ST.val : g_N;
    uint32_t reload = left + g_N * (expected - 1u);
    if (g_pend_tick) { g_pend_tick = false; reload -= g_N; }
    ST.load = reload; st_write_val(); st_start();
    S.sleeps++; if (expected > S.max_sleep_ticks) S.max_sleep_ticks = expected;
    sim_wfi(); port_cpsie(); port_cpsid();
    st_stop();
    uint32_t complete;
    if (st_read_countflag()) { complete = expected - 1u; ST.load = ST.val ? g_N - (reload - ST.val + 1u) : g_N; }
    else { complete = (expected * g_N - ST.val) / g_N; ST.load = g_N; }      /* 버그: 조각을 버린다 */
    st_write_val(); st_start(); ST.load = g_N - 1u;
    g_tick += complete; g_ticks_total += complete;
    port_cpsie();
}

static void sim_reset(uint32_t timer_hz, uint32_t tick_hz, uint32_t tick0, suppress_fn mode) {
    memset(&S, 0, sizeof S); memset(g_task, 0, sizeof g_task); memset(&ST, 0, sizeof ST);
    g_ntask = 0; g_evt = NULL; g_ext_n = g_ext_i = 0;
    g_now = 0; g_pend_tick = g_pend_ext = g_primask = false;
    g_sched_suspended = 0; g_pended = 0; g_hook_after_cpsid = g_hook_before_suppress = NULL;
    g_N = timer_hz / tick_hz; g_max_supp = MAX_24BIT / g_N;
    ST.load = g_N; st_write_val(); st_start(); ST.load = g_N - 1u;   /* 부팅: tick k 의 경계 = k·N 이 되게 정렬 */
    g_tick = tick0; g_ticks_total = 0; g_suppress = mode;
    S.hash = 14695981039346656037ull;
}
static tcb_t *task_add(const char *name, uint8_t prio, uint32_t period, uint32_t work) {
    tcb_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->period = period; t->work = work; t->irq_at = NEVER;
    if (period) { t->state = TASK_READY; t->wake_tick = g_tick; }        /* 부팅 직후 1회 실행 */
    else        { t->state = TASK_BLOCKED; g_evt = t; }
    return t;
}
static void sim_run_until(uint64_t t_end) {
    uint64_t last = g_now; int same = 0;
    while (g_now < t_end) {
        tcb_t *t = sched_pick_next();
        if (t) run_task(t);
        else { if (ext_next() == NEVER && !ST.en) break; idle_task_step(); }
        if (g_now == last) { if (++same > 1000) { S.stuck++; break; } } else { last = g_now; same = 0; }
    }
}

/* 타임라인: 칸 = tick (k·N ≤ 시각 < (k+1)·N). 태스크 행 R = 그 tick 에 시작. CPU 행 R = 태스크 실행, w = 깨어남만, z = 내내 잠. */
#define TL_COLS 30
static char g_tl_task[MAXT][TL_COLS + 1], g_tl_cpu[TL_COLS + 1];
static void tl_record(void) {
    for (int i = 0; i < g_ntask; i++) {
        uint64_t c = g_task[i].last_start / g_N;
        if (g_task[i].runs && c < TL_COLS) g_tl_task[i][c] = 'R';
    }
}
static void run_with_timeline(uint64_t t_end) {
    for (int i = 0; i < MAXT; i++) { memset(g_tl_task[i], '.', TL_COLS); g_tl_task[i][TL_COLS] = 0; }
    memset(g_tl_cpu, 'z', TL_COLS); g_tl_cpu[TL_COLS] = 0;
    int guard = 0;
    while (g_now < t_end && ++guard < 100000) {
        tcb_t *t = sched_pick_next();
        if (t) { run_task(t); tl_record(); if (t->last_start / g_N < TL_COLS) g_tl_cpu[t->last_start / g_N] = 'R'; continue; }
        uint64_t prev_w = S.wakeups;
        idle_task_step();
        if (S.wakeups != prev_w && g_now / g_N < TL_COLS && g_tl_cpu[g_now / g_N] != 'R') g_tl_cpu[g_now / g_N] = 'w';
    }
}
static void tl_print(const char *mode) {
    printf("  tick     : "); for (int c = 0; c < TL_COLS; c++) printf("%d ", c % 10); printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("  %-9s: ", g_task[i].name); for (int c = 0; c < TL_COLS; c++) printf("%c ", g_tl_task[i][c]); printf("\n");
    }
    printf("  %-9s: ", mode); for (int c = 0; c < TL_COLS; c++) printf("%c ", g_tl_cpu[c]);
    printf("  wakeups=%llu\n", (unsigned long long)S.wakeups);
}

static uint32_t g_rng;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* ---- 테스트용 훅 ---- */
static void hook_late_cpsid(void) { sim_advance(ST.val + 5u); }      /* PRIMASK=1 상태로 tick 경계를 5 count 지나침 */
static void hook_motion_irq(void) { g_pend_ext = true; service_irqs(); }
static void hook_tick_passes(void) { sim_advance(ST.val); }          /* 스케줄러 정지 중 tick ISR → pended */

/* ======================= 테스트 ======================= */
#define HZ 48000000u
#define TICK_HZ 1000u

static void part_a(void) {
    printf("\n== Part A: 다음 만료, reload, 만료 시 보정 ==\n");
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    CHECK(g_N == 48000u && g_max_supp == 349u);
    CHECK(tl_reload_value(12000, 10, false) == 12000u + 9u * 48000u);
    CHECK(tl_reload_value(0, 10, false) == 10u * 48000u);                 /* VAL 0 = 한 tick 통째로 남음 */
    CHECK(tl_reload_value(12000, 10, true) == 12000u + 8u * 48000u);      /* tick 이 이미 펜딩 */
    CHECK(tl_reload_value(48000, 349, false) <= MAX_24BIT && tl_reload_value(48000, 350, false) > MAX_24BIT);
    CHECK(tl_expiry_remainder(444000, 0) == 48000u);                      /* IRQ 바로 그 순간 */
    CHECK(tl_expiry_remainder(444000, 444000) == 47999u);                 /* 1 클럭 뒤 (재적재) */
    CHECK(tl_expiry_remainder(444000, 444000 - 99) == 47900u);            /* 100 클럭 뒤 */

    /* expected_idle_ticks */
    tcb_t *a = task_add("a", 2, 37, 1), *b = task_add("b", 1, 50, 1);
    a->state = b->state = TASK_BLOCKED; a->delayed = b->delayed = true;
    a->wake_tick = 37; b->wake_tick = 20;
    CHECK(expected_idle_ticks() == 20u);
    b->state = TASK_READY;
    CHECK(expected_idle_ticks() == 0u);                                   /* 누가 READY 면 잘 수 없다 */
    b->state = TASK_BLOCKED; b->delayed = false; a->delayed = false;
    CHECK(expected_idle_ticks() == PORT_MAX_DELAY - 0u);                  /* 지연 없음 → portMAX_DELAY - tick */
    g_tick = 0xFFFFFFF0u; a->delayed = true; a->wake_tick = 0x10u;
    CHECK(expected_idle_ticks() == 0x20u);                                /* wrap 안전 */

    /* 시나리오: 주기 10 tick 태스크 1개 + 모션 IRQ 1번 (13.4 tick), 30 tick 동안 — ticked vs tickless */
    uint64_t hash_ticked, wk_ticked;
    sim_reset(HZ, TICK_HZ, 0, NULL);
    task_add("env", 2, 10, 4800); task_add("evt", 3, 0, 2400);
    g_ext[g_ext_n++] = 13u * 48000u + 19200u;
    run_with_timeline(30u * 48000u);
    printf("[ticked idle]\n"); tl_print("ticked");
    hash_ticked = S.hash; wk_ticked = S.wakeups;
    CHECK(S.err_max_abs == 0 && S.err_checks == 30u);

    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    tcb_t *env = task_add("env", 2, 10, 4800); tcb_t *evt = task_add("evt", 3, 0, 2400);
    g_ext[g_ext_n++] = 13u * 48000u + 19200u;
    run_with_timeline(30u * 48000u);
    printf("[tickless]\n"); tl_print("tickless");
    printf("  sleeps=%llu tick_isrs=%llu err_checks=%llu ticks_total=%llu now/N=%llu\n",
           (unsigned long long)S.sleeps, (unsigned long long)S.tick_isrs, (unsigned long long)S.err_checks,
           (unsigned long long)g_ticks_total, (unsigned long long)(g_now / g_N));
    CHECK(env->runs == 3 && evt->runs == 1 && evt->lat_max == 0);
    CHECK(S.hash == hash_ticked);                                         /* 스케줄이 ticked 와 똑같다 */
    CHECK(S.wakeups == 4 && wk_ticked == 31);                              /* 10, 13.4, 20, 30 vs 매 tick */
    CHECK(S.err_max_abs == 0 && S.err_checks > 0 && S.wake_err_max_abs == 0);
    CHECK(g_ticks_total == 30u && S.late_wakes == 0 && S.step_asserts == 0);
}

static void part_b(void) {
    printf("\n== Part B: 조기 기상, 상한, 펜딩 tick, 중단, step 경계 ==\n");
    /* B-1 조기 기상: 주기 100 태스크가 자는 중 37.25 tick 에 모션 IRQ */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    tcb_t *env = task_add("env", 2, 100, 4800); tcb_t *evt = task_add("evt", 3, 0, 0);
    g_ext[g_ext_n++] = 37u * 48000u + 12000u;
    sim_run_until(37u * 48000u + 12000u);                  /* 깬 직후 (evt 는 아직 READY) 에서 멈춘다 */
    printf("  early wake @37.25: tick=%u VAL=%u LOAD=%u sleeps=%llu\n", g_tick, ST.val, ST.load, (unsigned long long)S.sleeps);
    CHECK(g_tick == 37u && ST.val == 36000u && ST.load == 47999u);       /* 0.75 tick 조각으로 재시작 */
    CHECK(evt->state == TASK_READY && S.sleeps == 1);
    sim_run_until(100u * 48000u + 1u);
    CHECK(evt->runs == 1 && evt->lat_max == 0);
    CHECK(env->runs == 2 && env->last_start == 100u * 48000u && S.err_max_abs == 0 && S.wake_err_max_abs == 0);

    /* B-2 IRQ 가 정확히 tick 경계 (37.00) */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    task_add("env", 2, 100, 4800); evt = task_add("evt", 3, 0, 0);
    g_ext[g_ext_n++] = 37u * 48000u;
    sim_run_until(37u * 48000u);
    CHECK(g_tick == 37u && ST.val == 48000u && evt->state == TASK_READY);   /* 37 tick 온전히, 조각 = 한 tick */
    sim_run_until(100u * 48000u + 1u);
    CHECK(evt->runs == 1 && S.err_max_abs == 0 && S.err_checks > 0);

    /* B-3 IRQ 와 타이머 만료가 같은 순간 (100.00) → 만료 경로 + 두 태스크 모두 */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    env = task_add("env", 2, 100, 4800); evt = task_add("evt", 3, 0, 0);
    g_ext[g_ext_n++] = 100u * 48000u;
    sim_run_until(100u * 48000u + 1u);
    CHECK(g_tick == 100u && env->runs == 2 && evt->runs == 1 && evt->last_start == 100u * 48000u);
    CHECK(S.err_max_abs == 0 && S.wake_err_max_abs == 0);

    /* B-4 최대 억제 상한: 5000 tick 지연 → 349 씩 14번 + 114 한 번 = 15번 잠 */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    env = task_add("env", 2, 5000, 4800);
    sim_run_until(5000u * 48000u + 1u);
    printf("  cap: sleeps=%llu wakeups=%llu max_sleep=%llu ticks env.last_start=%llu\n", (unsigned long long)S.sleeps,
           (unsigned long long)S.wakeups, (unsigned long long)S.max_sleep_ticks, (unsigned long long)env->last_start);
    CHECK(S.sleeps == 15 && S.wakeups == 15 && S.max_sleep_ticks == 349u);
    CHECK(env->runs == 2 && env->last_start == 5000ull * 48000u && S.err_max_abs == 0);

    /* B-5 cpsid 뒤 tick 경계를 지나 SysTick 이 펜딩된 채로 정지 → reload 에서 N 빼기 */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    env = task_add("env", 2, 50, 4800);
    sim_run_until(4800);                                    /* env 가 한 번 돌고 막힘 */
    g_hook_after_cpsid = hook_late_cpsid;
    sim_run_until(50u * 48000u + 1u);
    CHECK(S.pend_cleared == 1 && env->runs == 2 && env->last_start == 50u * 48000u);
    CHECK(g_ticks_total == 50u && S.err_max_abs == 0 && S.wake_err_max_abs == 0);

    /* B-6 중단: 재계산 뒤 cpsid 전에 모션 IRQ → pending-ready → eAbortSleep */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    task_add("env", 2, 50, 4800); evt = task_add("evt", 3, 0, 2400);
    sim_run_until(4800);
    g_hook_before_suppress = hook_motion_irq;
    sim_run_until(4800 + 1);
    CHECK(S.aborts == 1 && S.sleeps == 0 && evt->runs == 1 && evt->lat_max == 0);
    /* 같은 상황, 확인 없는 순진한 구현 → 모션 이벤트가 다음 만료(50 tick)까지 방치된다 */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_naive);
    task_add("env", 2, 50, 4800); evt = task_add("evt", 3, 0, 2400);
    sim_run_until(4800);
    g_hook_before_suppress = hook_motion_irq;
    sim_run_until(50u * 48000u + 1u);
    printf("  naive (확인 없음): motion latency = %llu clk = %.3f ms\n", (unsigned long long)evt->lat_max, evt->lat_max / 48000.0);
    CHECK(evt->runs == 1 && evt->lat_max == 50u * 48000u - 4800u);

    /* B-7 중단: 스케줄러 정지 중 tick ISR → pended tick → eAbortSleep, tick 은 잃지 않는다 */
    sim_reset(HZ, TICK_HZ, 0, port_suppress_ticks_and_sleep);
    env = task_add("env", 2, 50, 4800);
    sim_run_until(4800);
    g_hook_before_suppress = hook_tick_passes;
    sim_run_until(50u * 48000u + 1u);
    CHECK(S.aborts == 1 && env->runs == 2 && env->last_start == 50u * 48000u && S.err_max_abs == 0);

    /* B-8 조기 기상 계산 / confirm / step_tick 단위 검사 */
    uint32_t c = 99;
    CHECK(tl_early_remainder(100, 63u * 48000u, &c) == 48000u && c == 37u);            /* B-2 의 숫자 */
    CHECK(tl_early_remainder(100, 62u * 48000u + 36000u, &c) == 36000u && c == 37u);   /* B-1 의 숫자 */
    CHECK(tl_early_remainder(349, 1u, &c) == 1u && c == 348u);                          /* 만료 1 count 전 */
    sim_reset(HZ, TICK_HZ, 100, port_suppress_ticks_and_sleep);
    env = task_add("env", 2, 5, 1); evt = task_add("evt", 3, 0, 1);
    env->state = TASK_BLOCKED; env->delayed = true; env->wake_tick = 105;
    CHECK(next_unblock_delta() == 5u && confirm_sleep_status() == SLEEP_STANDARD);
    evt->pending_ready = true;  CHECK(confirm_sleep_status() == SLEEP_ABORT);  evt->pending_ready = false;
    g_pended = 1;               CHECK(confirm_sleep_status() == SLEEP_ABORT);  g_pended = 0;
    env->delayed = false;       CHECK(confirm_sleep_status() == SLEEP_NO_TIMEOUT); env->delayed = true;
    kernel_step_tick(3);
    CHECK(g_tick == 103u && g_pended == 0);
    suspend_all();
    kernel_step_tick(2);                                    /* 정확히 만료까지 → 1 은 pended 로 */
    CHECK(g_tick == 104u && g_pended == 1u && env->state == TASK_BLOCKED);
    resume_all();
    CHECK(g_tick == 105u && env->state == TASK_READY && g_ticks_total == 5u);
    env->state = TASK_BLOCKED; env->delayed = true; env->wake_tick = 110;
    kernel_step_tick(9);                                    /* 만료를 넘는 step → assert */
    CHECK(S.step_asserts == 1u && S.late_wakes == 0);
}

typedef struct { stats_t s; uint64_t total, ticks; uint64_t evt_lat; } run_t;
static run_t run_node(uint32_t seed, suppress_fn mode) {
    sim_reset(HZ, TICK_HZ, 0xFFFF0000u, mode);              /* tick 이 6만 5천 tick 뒤 wrap */
    task_add("env", 2, 500, 9600);                          /* 온습도 2 Hz, 200 µs */
    task_add("adv", 1, 1000, 48000);                        /* BLE 광고 1 Hz, 1 ms */
    tcb_t *evt = task_add("evt", 3, 0, 4800);               /* 모션 IRQ 처리, 100 µs */
    const uint64_t T_END = 600000ull * 48000u;              /* 10 분 */
    g_rng = seed; uint64_t t = 0;
    while (g_ext_n < MAX_EXT) { t += 1u + xorshift32() % (800u * 48000u); if (t >= T_END) break; g_ext[g_ext_n++] = t; }
    sim_run_until(T_END);
    run_t r = { S, g_now, g_ticks_total, evt->lat_max };
    return r;
}

static void part_c(void) {
    printf("\n== Part C: 배터리 BLE 센서 노드, 10 분 x 5 시드 ==\n");
    printf("  seed mode      wakeups  sleeps  drift[clk]   drift[ms]  avg(Sleep)uA  avg(Stop)uA  max_sleep\n");
    bool zero = true, same_sched = true, naive_drifts = true, fewer = true, cheaper = true, cap = true, clean = true;
    for (uint32_t seed = 1; seed <= 5; seed++) {
        run_t r[3]; suppress_fn modes[3] = { NULL, port_suppress_ticks_and_sleep, port_suppress_naive };
        const char *nm[3] = { "ticked", "tickless", "naive" };
        for (int m = 0; m < 3; m++) {
            r[m] = run_node(seed * 2654435761u, modes[m]);
            printf("  %4u %-9s %8llu %7llu %11lld %11.3f %13.1f %12.1f %10llu\n", seed, nm[m],
                   (unsigned long long)r[m].s.wakeups, (unsigned long long)r[m].s.sleeps, (long long)r[m].s.err_last,
                   r[m].s.err_last / 48000.0, avg_current_nA(&r[m].s, r[m].total, I_SLEEP_NA) / 1000.0,
                   avg_current_nA(&r[m].s, r[m].total, I_STOP_NA) / 1000.0, (unsigned long long)r[m].s.max_sleep_ticks);
        }
        zero = zero && r[0].s.err_max_abs == 0 && r[1].s.err_max_abs == 0 && r[1].s.err_checks > 1000
                    && r[1].s.wake_err_max_abs == 0;
        same_sched = same_sched && r[1].s.hash == r[0].s.hash && r[2].s.hash != r[0].s.hash;
        naive_drifts = naive_drifts && r[2].s.err_last > 100 * 48000;                 /* 100 tick 이상 뒤처짐 */
        fewer = fewer && r[1].s.wakeups * 20 < r[0].s.wakeups;
        cheaper = cheaper && avg_current_nA(&r[1].s, r[1].total, I_SLEEP_NA) < avg_current_nA(&r[0].s, r[0].total, I_SLEEP_NA)
                          && avg_current_nA(&r[1].s, r[1].total, I_STOP_NA) * 4 < avg_current_nA(&r[0].s, r[0].total, I_STOP_NA);
        cap = cap && r[1].s.max_sleep_ticks == 349u;
        clean = clean && r[1].s.late_wakes == 0 && r[1].s.step_asserts == 0 && r[1].s.overruns == 0
                      && r[1].s.stuck == 0 && r[0].s.stuck == 0 && r[1].ticks == r[1].total / 48000u;
    }
    CHECK(zero);            /* 올바른 tickless: 모든 tick ISR 이 정확히 k·N 에, 깨운 시각도 정확 */
    CHECK(same_sched);      /* 관찰 가능한 스케줄 = ticked 와 동일, naive 는 다름 */
    CHECK(naive_drifts);
    CHECK(fewer && cap && clean);
    CHECK(cheaper);
    stats_t z; memset(&z, 0, sizeof z); z.sleep_clk = 900; z.wakeups = 1;
    CHECK(avg_current_nA(&z, 1000, I_SLEEP_NA) == (100 * I_RUN_NA + 900 * I_SLEEP_NA + WAKE_CLK * (I_RUN_NA - I_SLEEP_NA)) / 1000);
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
