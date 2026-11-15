/* RTOS-B12 소프트웨어 타이머: one-shot·auto-reload·타이머 태스크
 *
 * 호스트 모델: OS 스레드 1개가 틱을 하나씩 돌리는 결정적 시뮬레이터 (FreeRTOS timers.c 의 동작을 흉내).
 *   - 소프트웨어 타이머 = {period, auto_reload, active, expiry, cb}. 활성 타이머는 expiry 순 리스트 1개.
 *   - 타이머 API(start/reset/stop/change_period/pend_function_call)는 "명령" 을 큐(길이 g_qlen)에 넣기만 한다.
 *     START/RESET 명령은 "부른 순간의 틱" 을 싣고 간다 (xTimerReset → xTaskGetTickCount(), timers.h 803-804).
 *   - 데몬(타이머 서비스 태스크, 우선순위 g_dmn_prio)이 깨어나면: ① 명령 큐를 비우고 ② 만료된 타이머 1개 처리,
 *     ①②를 할 일이 없을 때까지 반복 (prvTimerTask 루프, timers.c 746-777). 콜백은 데몬 문맥에서 돈다.
 *   - 한 틱의 순서: ⓪ 막힌 것 깨우기 → ① ISR 각본 → ② 데몬이 가장 높은 READY 면 데몬 실행(시간 0)
 *                   → ③ 가장 높은 앱 태스크가 그 틱을 쓴다. 태스크가 명령을 보냈고 데몬이 더 높으면 즉시 선점.
 *   - 콜백 안의 블로킹(vTaskDelay 등)은 cb_block(n): 데몬이 n 틱 동안 BLOCKED → 그동안 다른 타이머도 못 돈다.
 * 같은 시나리오를 진짜 FreeRTOS 로 돌린 결과는 demo_freertos_timers.c (md 에 실제 출력).
 *
 * Part A  tmr_insert / tmr_expire / tmr_process_command  — 리스트, one-shot vs auto-reload(따라잡기), 명령
 * Part B  log_flush_cb_deferred                          — 콜백에서 막지 말고 worker 에게 넘기기
 * Part C  comms_timeout_cb / uart_frame_isr / link_up_pended — 통신 타임아웃(one-shot + RX 마다 reset) + LED
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 소프트웨어 타이머 + 데몬 모델 (주어진 코드 + TODO) ======================= */
enum { QCAP = 16, TL = 40, MAX_AT = 40, MAX_ORDER = 64 };
typedef enum { CTX_TASK, CTX_ISR, CTX_DAEMON } ctx_t;

typedef struct swtimer swtimer_t;
typedef void (*tmr_cb_t)(swtimer_t *t);
struct swtimer {
    const char *name;
    uint32_t period;
    bool auto_reload;
    bool active;          /* tmrSTATUS_IS_ACTIVE — 명령을 "보낼 때" 가 아니라 데몬이 "처리할 때" 바뀐다 */
    bool in_list;
    uint32_t expiry;      /* 다음 만료 틱 (절대값) */
    tmr_cb_t cb;
    swtimer_t *next;      /* 활성 리스트 링크 */
    /* ---- 하네스 기록용 ---- */
    int nat;                  /* 콜백 횟수 */
    uint32_t at[MAX_AT];      /* 콜백이 실제로 불린 틱 */
    uint32_t max_late;        /* max(불린 틱 - 원래 만료 틱) */
    char tl[TL];              /* 타임라인: 그 틱의 콜백 수 */
};

typedef enum { CMD_START, CMD_RESET, CMD_STOP, CMD_CHANGE_PERIOD, CMD_PEND_CALL } cmd_id_t;
typedef void (*pend_fn_t)(void *arg1, uint32_t arg2);
typedef struct {
    cmd_id_t id;
    swtimer_t *t;
    uint32_t value;       /* START/RESET: 명령을 보낸 틱, CHANGE_PERIOD: 새 주기 */
    pend_fn_t fn; void *arg1; uint32_t arg2;      /* PEND_CALL */
} tmr_cmd_t;

static uint32_t g_tick, g_tl0;          /* g_tl0 = 타임라인 0 열의 틱 */
static ctx_t g_ctx = CTX_TASK;
static int g_cur_prio;                  /* API 를 부른 태스크의 우선순위 (선점 판단) */
static swtimer_t *g_head;               /* 활성 리스트: expiry 오름차순, 같은 값은 먼저 넣은 것이 앞 */
static tmr_cmd_t g_q[QCAP];
static int g_qhead, g_qn, g_qlen = 10, g_cmd_fail;
static int g_dmn_prio = 6;
static bool g_dmn_blocked, g_dmn_ran;   /* 콜백 안에서 막힘 / 이번 틱에 데몬이 돌았음 */
static bool g_sched;                    /* 스케줄러가 돌기 시작했나 (전에는 API 가 큐에 넣기만 한다) */
static uint32_t g_dmn_wake;
static int g_ctx_err;                   /* 콜백·지연 함수가 데몬 밖에서 불린 횟수 */
static char g_order[MAX_ORDER][12];     /* 콜백 순서 로그 "LED@19" */
static int g_norder;

static bool is_due(uint32_t expiry, uint32_t now) { return (int32_t)(now - expiry) >= 0; }   /* 랩 안전 */
static bool before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }                  /* a 가 b 보다 이르다 */

static void timer_init(swtimer_t *t, const char *name, uint32_t period, bool auto_reload, tmr_cb_t cb) {
    memset(t, 0, sizeof *t);
    t->name = name; t->period = period; t->auto_reload = auto_reload; t->cb = cb;
    memset(t->tl, '.', TL);
}
static void tmr_remove(swtimer_t *t) {
    for (swtimer_t **pp = &g_head; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; break; }
    t->next = NULL; t->in_list = false;
}
/* 콜백 1번 부르기 (기록 + 데몬 문맥 확인). due = 이 콜백이 원래 불렸어야 할 만료 틱 */
static void tmr_fire(swtimer_t *t, uint32_t due, uint32_t now) {
    uint32_t late = now - due;
    if (late > t->max_late) t->max_late = late;
    if (t->nat < MAX_AT) t->at[t->nat] = now;
    t->nat++;
    if (now - g_tl0 < TL && t->tl[now - g_tl0] < '9') t->tl[now - g_tl0] = (t->tl[now - g_tl0] == '.') ? '1' : (char)(t->tl[now - g_tl0] + 1);
    if (g_norder < MAX_ORDER) snprintf(g_order[g_norder++], sizeof g_order[0], "%s@%u", t->name, (unsigned)now);
    if (g_ctx != CTX_DAEMON) g_ctx_err++;
    if (t->cb) t->cb(t);
}
/* 콜백 안에서 vTaskDelay(n) 같은 블로킹 호출을 했다 */
static void cb_block(uint32_t n) { g_dmn_blocked = true; g_dmn_wake = g_tick + n; }

/* ---- Part A-1: 활성 리스트에 expiry 순으로 넣기 ----
 * 앞에서부터 보며 "나보다 늦은(before(t, x))" 첫 노드 앞에 넣는다. 같은 expiry 는 기존 것 뒤 (FIFO, vListInsert 와 같음).
 * 비교는 before() 로 (틱이 0xFFFFFFFF → 0 으로 넘어가도 맞게). in_list = true. */
static void tmr_insert(swtimer_t *t) {
    swtimer_t **pp = &g_head;
    while (*pp && !before(t->expiry, (*pp)->expiry)) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
    t->in_list = true;
}

/* ---- Part A-2: 만료 처리 (t 는 이미 리스트에서 빠졌고 t->expiry 는 now 이하) ----
 * one-shot  : active = false, 콜백 1번 (due = t->expiry).
 * auto-reload: 위상을 지킨다 — 다음 만료 = 이전 만료 + period (now + period 가 아님!).
 *   데몬이 늦어서 그 다음 만료도 이미 지났으면, 지난 만료마다 콜백을 한 번씩 부르며 따라잡는다
 *   (prvReloadTimer, timers.c 697-713). 마지막으로 now 보다 뒤인 만료로 다시 넣고, 마지막 콜백을 부른다.
 *   콜백 순서: 이른 만료부터. 각 콜백의 due 를 tmr_fire 에 정확히 넘길 것. */
static void tmr_expire(swtimer_t *t, uint32_t now) {
    uint32_t due = t->expiry;
    if (t->auto_reload) {
        while (is_due(due + t->period, now)) {      /* 다음 만료도 이미 지났다 → 이번 것 콜백, 한 칸 전진 */
            tmr_fire(t, due, now);
            due += t->period;
        }
        t->expiry = due + t->period;
        tmr_insert(t);
    } else {
        t->active = false;
    }
    tmr_fire(t, due, now);
}

/* ---- Part A-3: 명령 하나 처리 (데몬 문맥, now = 처리하는 틱) — prvProcessReceivedCommands, timers.c 936-1080 ----
 * PEND_CALL      : c->fn(c->arg1, c->arg2) 를 부르고 끝 (타이머 없음).
 * 나머지는 먼저 타이머가 리스트에 있으면 뺀다 (tmr_remove).
 * START / RESET  : active = true, expiry = c->value(보낸 틱) + period.
 *                  처리할 때 이미 그 expiry 가 지났으면(is_due) 리스트에 넣지 말고 바로 tmr_expire(t, now)
 *                  — 데몬이 늦게 처리해도 기준은 "부른 순간" 이다. 아니면 tmr_insert.
 * STOP           : active = false.
 * CHANGE_PERIOD  : period = c->value, active = true, expiry = now + period (이건 "처리한 순간" 기준!), insert. */
static void tmr_process_command(const tmr_cmd_t *c, uint32_t now) {
    if (c->id == CMD_PEND_CALL) { c->fn(c->arg1, c->arg2); return; }
    swtimer_t *t = c->t;
    if (t->in_list) tmr_remove(t);
    switch (c->id) {
    case CMD_START:
    case CMD_RESET:
        t->active = true;
        t->expiry = c->value + t->period;
        if (is_due(t->expiry, now)) tmr_expire(t, now);
        else tmr_insert(t);
        break;
    case CMD_STOP:
        t->active = false;
        break;
    case CMD_CHANGE_PERIOD:
        t->period = c->value;
        t->active = true;
        t->expiry = now + t->period;
        tmr_insert(t);
        break;
    default:
        break;
    }
}

/* ---- 데몬 (주어진 코드): prvTimerTask 루프 ----
 * 데몬은 언제나 "큐 대기" 또는 "콜백 안" 에서 멈췄다가 깨어나므로, 깨어나면 큐 비우기부터 한다. */
static bool q_recv(tmr_cmd_t *c) {
    if (g_qn == 0) return false;
    *c = g_q[g_qhead]; g_qhead = (g_qhead + 1) % QCAP; g_qn--;
    return true;
}
static bool daemon_ready(uint32_t now) {
    return !g_dmn_blocked && (g_qn > 0 || (g_head && is_due(g_head->expiry, now)));
}
static void daemon_run(uint32_t now) {
    ctx_t sc = g_ctx; int sp = g_cur_prio;
    g_ctx = CTX_DAEMON; g_cur_prio = g_dmn_prio; g_dmn_ran = true;
    for (int guard = 0; guard < 1000; guard++) {
        tmr_cmd_t c;
        while (!g_dmn_blocked && q_recv(&c)) tmr_process_command(&c, now);   /* ① 큐 비우기 */
        if (g_dmn_blocked) break;
        if (!(g_head && is_due(g_head->expiry, now))) break;                  /* 할 일 없음 → 대기 */
        swtimer_t *t = g_head;                                                /* ② 만료 1개 */
        g_head = t->next; t->next = NULL; t->in_list = false;
        tmr_expire(t, now);
        if (g_dmn_blocked) break;
    }
    g_ctx = sc; g_cur_prio = sp;
}

/* ---- 타이머 API (주어진 코드): 모두 block time 0 — 큐가 꽉 차면 즉시 false (pdFAIL) ---- */
static bool q_send(tmr_cmd_t c) {
    if (g_qn >= g_qlen) { g_cmd_fail++; return false; }
    g_q[(g_qhead + g_qn) % QCAP] = c; g_qn++;
    /* 태스크가 보냈고 데몬이 더 높으면 즉시 선점 (xQueueSend 가 높은 대기 태스크를 깨우면 바로 yield) */
    if (g_sched && g_ctx == CTX_TASK && g_dmn_prio > g_cur_prio && daemon_ready(g_tick)) daemon_run(g_tick);
    return true;
}
static bool timer_start(swtimer_t *t)  { return q_send((tmr_cmd_t){ .id = CMD_START, .t = t, .value = g_tick }); }
static bool timer_reset(swtimer_t *t)  { return q_send((tmr_cmd_t){ .id = CMD_RESET, .t = t, .value = g_tick }); }
static bool timer_stop(swtimer_t *t)   { return q_send((tmr_cmd_t){ .id = CMD_STOP, .t = t }); }
static bool timer_change_period(swtimer_t *t, uint32_t p) { return q_send((tmr_cmd_t){ .id = CMD_CHANGE_PERIOD, .t = t, .value = p }); }
static bool pend_function_call(pend_fn_t fn, void *a1, uint32_t a2) { return q_send((tmr_cmd_t){ .id = CMD_PEND_CALL, .fn = fn, .arg1 = a1, .arg2 = a2 }); }
/* FromISR 판: 모델에서는 같은 동작 (ISR 은 원래 기다리지 않는다; 선점은 ISR 이 끝난 뒤 스케줄러가 판단) */
static bool timer_reset_from_isr(swtimer_t *t) { return timer_reset(t); }
static bool pend_function_call_from_isr(pend_fn_t fn, void *a1, uint32_t a2) { return pend_function_call(fn, a1, a2); }
static bool timer_is_active(const swtimer_t *t) { return t->active; }

/* ======================= 앱 태스크 + 틱 루프 (하네스) ======================= */
typedef void (*isr_fn)(uint32_t now);
typedef void (*hook_fn)(uint32_t now);
static struct { bool used; int prio; uint32_t from, to; hook_fn hook; char tl[TL]; } g_ctrl;   /* [from,to) 동안 CPU 를 쓰는 태스크 */
static struct { bool used, blocked; int prio, sem; uint32_t wake, work, done_tick; int done; char tl[TL]; } g_wrk;
static char g_dtl[TL];                  /* 데몬 타임라인 */

static void sim_reset(int dmn_prio, int qlen) {
    g_tick = 0; g_tl0 = 0; g_ctx = CTX_TASK; g_cur_prio = 0; g_head = NULL;
    g_qhead = g_qn = 0; g_qlen = qlen; g_cmd_fail = 0;
    g_dmn_prio = dmn_prio; g_dmn_blocked = false; g_dmn_ran = false; g_sched = false; g_ctx_err = 0; g_norder = 0;
    memset(&g_ctrl, 0, sizeof g_ctrl); memset(&g_wrk, 0, sizeof g_wrk);
    memset(g_ctrl.tl, '.', TL); memset(g_wrk.tl, '.', TL); memset(g_dtl, '.', TL);
}
static bool ctrl_ready(uint32_t now) { return g_ctrl.used && !before(now, g_ctrl.from) && before(now, g_ctrl.to); }
static bool wrk_ready(void) { return g_wrk.used && !g_wrk.blocked && g_wrk.sem > 0; }
static int app_top_prio(uint32_t now) {
    int p = -1;
    if (ctrl_ready(now)) p = g_ctrl.prio;
    if (wrk_ready() && g_wrk.prio > p) p = g_wrk.prio;
    return p;
}
static void sim_run(uint32_t ticks, isr_fn isr) {
    g_sched = true;
    for (uint32_t k = 0; k < ticks; k++, g_tick++) {
        uint32_t now = g_tick, col = now - g_tl0;
        g_dmn_ran = false;
        if (g_dmn_blocked && is_due(g_dmn_wake, now)) g_dmn_blocked = false;          /* ⓪ 콜백이 깨어남 */
        if (g_wrk.used && g_wrk.blocked && is_due(g_wrk.wake, now)) { g_wrk.blocked = false; g_wrk.done++; g_wrk.done_tick = now; }
        if (isr) { g_ctx = CTX_ISR; isr(now); g_ctx = CTX_TASK; }                    /* ① ISR */
        if (daemon_ready(now) && g_dmn_prio > app_top_prio(now)) daemon_run(now);     /* ② 데몬 */
        int ap = app_top_prio(now);                                                  /* ③ 앱 태스크 1개 */
        bool c_run = false, w_run = false;
        if (ap >= 0 && ctrl_ready(now) && g_ctrl.prio == ap) {
            c_run = true; g_cur_prio = g_ctrl.prio;
            if (g_ctrl.hook) g_ctrl.hook(now);
            g_cur_prio = 0;
        } else if (ap >= 0 && wrk_ready()) {
            w_run = true; g_wrk.sem--; g_wrk.blocked = true; g_wrk.wake = now + g_wrk.work;   /* 느린 일 = vTaskDelay(work) */
        }
        if (col < TL) {
            g_dtl[col] = g_dmn_blocked ? 'B' : g_dmn_ran ? 'R' : daemon_ready(now) ? 'r' : '.';
            if (g_ctrl.used) g_ctrl.tl[col] = c_run ? 'R' : ctrl_ready(now) ? 'r' : '.';
            if (g_wrk.used) g_wrk.tl[col] = w_run ? 'R' : g_wrk.blocked ? 'B' : '.';
        }
    }
}
static void tl_print(const char *title, swtimer_t **tm, int ntm, uint32_t ncol) {
    printf("  -- %s\n  tick : ", title);
    for (uint32_t i = 0; i < ncol; i++) printf("%u ", (unsigned)((g_tl0 + i) % 10));
    printf("\n  Tmr  : "); for (uint32_t i = 0; i < ncol; i++) printf("%c ", g_dtl[i]);
    if (g_ctrl.used) { printf("\n  ctrl : "); for (uint32_t i = 0; i < ncol; i++) printf("%c ", g_ctrl.tl[i]); }
    if (g_wrk.used)  { printf("\n  wrk  : "); for (uint32_t i = 0; i < ncol; i++) printf("%c ", g_wrk.tl[i]); }
    for (int k = 0; k < ntm; k++) {
        printf("\n  %-5s: ", tm[k]->name);
        for (uint32_t i = 0; i < ncol; i++) printf("%c ", tm[k]->tl[i]);
    }
    printf("\n  order:");
    for (int i = 0; i < g_norder; i++) printf(" %s", g_order[i]);
    printf("\n");
}
static void print_fires(const swtimer_t *t) {
    printf("  %-6s:", t->name);
    for (int i = 0; i < t->nat && i < MAX_AT; i++) printf(" %u", (unsigned)t->at[i]);
    printf("   (max late %u)\n", (unsigned)t->max_late);
}
static bool fires_eq(const swtimer_t *t, const uint32_t *exp, int n) {
    if (t->nat != n) return false;
    for (int i = 0; i < n; i++) if (t->at[i] != exp[i]) return false;
    return true;
}

/* ======================= Part B: 시나리오 타이머와 콜백 ======================= */
static swtimer_t g_led, g_flush, g_comms, g_x;
static void led_cb(swtimer_t *t)   { (void)t; }               /* GPIO 토글 (기록은 tmr_fire 가 한다) */
static void comms_cb_b(swtimer_t *t) { (void)t; }
static void x_cb(swtimer_t *t)     { (void)t; }
static void log_flush_cb_blocking(swtimer_t *t) { (void)t; cb_block(10); }   /* 나쁜 예: 콜백 안에서 10 틱 대기 */

/* ---- Part B: 콜백은 짧게 — 느린 일은 worker 태스크에게 ----
 * 플래시 쓰기(10 틱 대기)를 콜백에서 하지 말고, worker 의 세마포어를 give 만 한다 (g_wrk.sem++ = xSemaphoreGive).
 * worker 가 깨어나 자기 문맥에서 10 틱을 기다린다 → 데몬은 막히지 않는다. */
static void log_flush_cb_deferred(swtimer_t *t) {
    (void)t;
    g_wrk.sem++;
}
static void isr_rx_at_15(uint32_t now) { if (now == 15) timer_reset_from_isr(&g_comms); }

static bool g_x_active_after_start;
static void ctrl_hook_start_x(uint32_t now) {
    if (now == 11) { timer_start(&g_x); g_x_active_after_start = timer_is_active(&g_x); }
}

/* ======================= Part C: 통신 타임아웃 + LED 상태 표시 ======================= */
enum { LED_SLOW = 8, LED_FAST = 2, COMMS_TO = 20 };
static bool g_link_up = true;
static uint32_t g_link_down_tick, g_link_up_tick;
static int g_link_downs, g_link_ups, g_api_fail;

/* ---- Part C-1: 통신 타임아웃 콜백 (데몬 문맥) ----
 * 링크 끊김: g_link_up = false, g_link_downs++, g_link_down_tick = g_tick,
 * LED 를 빠르게: timer_change_period(&g_led, LED_FAST) — 콜백 안이므로 block time 0 (모델 API 는 원래 0).
 * 실패하면 g_api_fail++. */
static void comms_timeout_cb(swtimer_t *t) {
    (void)t;
    g_link_up = false;
    g_link_downs++;
    g_link_down_tick = g_tick;
    if (!timer_change_period(&g_led, LED_FAST)) g_api_fail++;
}

/* ---- Part C-2: 링크 복구 — xTimerPendFunctionCallFromISR 로 데몬 문맥에서 실행된다 ----
 * 이미 up 이면 아무것도 안 한다 (ISR 이 두 번 pend 해도 안전 = 멱등).
 * 아니면 g_link_up = true, g_link_ups++, g_link_up_tick = g_tick, LED 를 느리게(LED_SLOW). 실패하면 g_api_fail++. */
static void link_up_pended(void *arg1, uint32_t rx_tick) {
    (void)arg1; (void)rx_tick;
    if (g_ctx != CTX_DAEMON) g_ctx_err++;
    if (g_link_up) return;
    g_link_up = true;
    g_link_ups++;
    g_link_up_tick = g_tick;
    if (!timer_change_period(&g_led, LED_SLOW)) g_api_fail++;
}

/* ---- Part C-3: UART 프레임 끝 ISR (IDLE line 인터럽트) ----
 * 매 프레임: timer_reset_from_isr(&g_comms) — 실패하면 g_api_fail++.
 * 링크가 끊겨 있으면 상태 변경은 직접 하지 말고 pend_function_call_from_isr(link_up_pended, NULL, now) 로 데몬에게.
 * (실패하면 g_api_fail++) */
static void uart_frame_isr(uint32_t now) {
    if (!timer_reset_from_isr(&g_comms)) g_api_fail++;
    if (!g_link_up && !pend_function_call_from_isr(link_up_pended, NULL, now)) g_api_fail++;
}

static const uint32_t RX_TICKS[] = { 5, 15, 25, 35, 80, 90, 100 };
static void isr_link(uint32_t now) {
    for (size_t i = 0; i < sizeof RX_TICKS / sizeof RX_TICKS[0]; i++)
        if (now == RX_TICKS[i]) uart_frame_isr(now);
}
static int g_burst_ok, g_burst_fail;
static void isr_burst_per_byte(uint32_t now) {           /* 나쁜 예: 바이트마다 reset — 12 바이트가 한 ISR 에 */
    if (now == 5) for (int i = 0; i < 12; i++) { if (timer_reset_from_isr(&g_comms)) g_burst_ok++; else g_burst_fail++; }
}
static void isr_burst_per_frame(uint32_t now) {          /* 프레임 끝에 1번 */
    if (now == 5) { if (timer_reset_from_isr(&g_comms)) g_burst_ok++; else g_burst_fail++; }
}

/* 명령 순서 확인용 지연 함수 */
static char g_seq[16]; static int g_nseq;
static void pend_mark(void *a1, uint32_t a2) { (void)a1; if (g_nseq < 15) g_seq[g_nseq++] = (char)a2; if (g_ctx != CTX_DAEMON) g_ctx_err++; }

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static void scenario_b(bool deferred, int dmn_prio, bool with_ctrl) {
    sim_reset(dmn_prio, 10);
    timer_init(&g_led, "LED", 4, true, led_cb);
    timer_init(&g_comms, "COMMS", 14, false, comms_cb_b);
    timer_init(&g_flush, "FLUSH", 9, false, deferred ? log_flush_cb_deferred : log_flush_cb_blocking);
    timer_init(&g_x, "X", 10, false, x_cb);
    if (deferred) { g_wrk.used = true; g_wrk.prio = 2; g_wrk.work = 10; }
    if (with_ctrl) { g_ctrl.used = true; g_ctrl.prio = 3; g_ctrl.from = 10; g_ctrl.to = 25; g_ctrl.hook = ctrl_hook_start_x; }
    timer_start(&g_led); timer_start(&g_comms);            /* 스케줄러 시작 전: 큐에만 (명령 틱 0) */
    if (!with_ctrl) timer_start(&g_flush);
    sim_run(40, isr_rx_at_15);
}

int main(void) {
    printf("== Part A: 리스트 · one-shot · auto-reload · 명령 ==\n");
    {   /* A1 리스트 순서 (같은 expiry 는 FIFO) */
        swtimer_t a, b, c, d;
        sim_reset(6, 10);
        timer_init(&a, "a", 1, false, NULL); timer_init(&b, "b", 1, false, NULL);
        timer_init(&c, "c", 1, false, NULL); timer_init(&d, "d", 1, false, NULL);
        a.expiry = 30; b.expiry = 10; c.expiry = 20; d.expiry = 10;
        tmr_insert(&a); tmr_insert(&b); tmr_insert(&c); tmr_insert(&d);
        char s[8] = {0}; int n = 0;
        for (swtimer_t *p = g_head; p && n < 7; p = p->next) s[n++] = p->name[0];
        CHECK(strcmp(s, "bdca") == 0);
        CHECK(a.in_list && b.in_list && c.in_list && d.in_list);
        tmr_remove(&c);
        CHECK(g_head == &b && b.next == &d && d.next == &a && !c.in_list);
    }
    {   /* A2 auto-reload 4 + one-shot 9, 틱 0 에 start */
        sim_reset(6, 10);
        swtimer_t led, os;
        timer_init(&led, "LED", 4, true, led_cb); timer_init(&os, "OS", 9, false, NULL);
        timer_start(&led); timer_start(&os);
        CHECK(!timer_is_active(&led) && g_qn == 2);        /* 아직 큐에만 있다 — 데몬이 처리해야 active */
        sim_run(22, NULL);
        CHECK(fires_eq(&led, (const uint32_t[]){ 4, 8, 12, 16, 20 }, 5) && led.max_late == 0);
        CHECK(timer_is_active(&led) && led.expiry == 24);
        CHECK(fires_eq(&os, (const uint32_t[]){ 9 }, 1) && !timer_is_active(&os) && !os.in_list);
        CHECK(g_ctx_err == 0);
    }
    {   /* A3 reset 은 "부른 틱" 기준, 잠든 타이머 reset = start, stop, change_period 는 "처리한 틱" 기준 */
        sim_reset(6, 10);
        swtimer_t to, led;
        timer_init(&to, "TO", 10, false, NULL); timer_init(&led, "LED", 4, true, led_cb);
        timer_start(&to); timer_start(&led);
        sim_run(6, NULL);  timer_reset(&to);                /* 틱 6 에 reset → 16 */
        sim_run(3, NULL);  timer_change_period(&led, 2);    /* 틱 9 에 2 로 → 11, 13, ... */
        sim_run(11, NULL); timer_reset(&to);                /* 틱 20: 잠든(one-shot 끝난) 타이머 reset → start → 30 */
        sim_run(6, NULL);  timer_stop(&led);                /* 틱 26 에 stop */
        sim_run(10, NULL);
        CHECK(fires_eq(&to, (const uint32_t[]){ 16, 30 }, 2));
        CHECK(fires_eq(&led, (const uint32_t[]){ 4, 8, 11, 13, 15, 17, 19, 21, 23, 25 }, 10));
        CHECK(!timer_is_active(&led) && !led.in_list && led.period == 2);
    }
    {   /* A4 늦게 처리된 START: 보낸 틱 + period 가 이미 지났으면 처리 즉시 콜백 */
        sim_reset(1, 10);
        swtimer_t os;
        timer_init(&os, "OS", 5, false, NULL);
        g_ctrl.used = true; g_ctrl.prio = 3; g_ctrl.from = 0; g_ctrl.to = 12;   /* 데몬(1)을 0..11 굶긴다 */
        g_cur_prio = 3; timer_start(&os); g_cur_prio = 0;                         /* 틱 0 에 보냄 → expiry 5 */
        sim_run(20, NULL);
        CHECK(fires_eq(&os, (const uint32_t[]){ 12 }, 1) && os.max_late == 7);
    }
    {   /* A5 xTimerPendFunctionCall: 데몬 문맥, 명령과 같은 큐에서 FIFO */
        sim_reset(6, 10);
        swtimer_t os;
        timer_init(&os, "OS", 3, false, NULL);
        g_nseq = 0; memset(g_seq, 0, sizeof g_seq);
        pend_function_call(pend_mark, NULL, 'a'); timer_start(&os); pend_function_call(pend_mark, NULL, 'b');
        CHECK(g_nseq == 0 && g_qn == 3);                    /* 보낸 쪽에서는 아직 안 돌았다 — 큐에만 */
        sim_run(1, NULL);
        CHECK(strcmp(g_seq, "ab") == 0 && g_ctx_err == 0 && timer_is_active(&os) && os.expiry == 3);
    }

    printf("\n== Part B: 콜백이 막히면 / 데몬 우선순위 ==\n");
    swtimer_t *show[] = { &g_led, &g_flush, &g_comms };
    scenario_b(false, 6, false);
    tl_print("blocking callback (FLUSH 콜백이 10 틱 대기), Tmr prio 6", show, 3, 40);
    print_fires(&g_led); print_fires(&g_comms);
    CHECK(fires_eq(&g_led, (const uint32_t[]){ 4, 8, 19, 19, 20, 24, 28, 32, 36 }, 9));
    CHECK(g_led.max_late == 7);                             /* 12 에 불렸어야 할 콜백이 19 에 */
    CHECK(fires_eq(&g_comms, (const uint32_t[]){ 29 }, 1)); /* 14 의 타임아웃은 사라졌다 (15 의 reset 이 먼저 처리) */
    CHECK(g_dtl[9] == 'B' && g_dtl[18] == 'B' && g_dtl[19] == 'R');

    scenario_b(true, 6, false);
    tl_print("deferred (FLUSH 콜백은 give 만, worker 가 10 틱 대기)", show, 3, 40);
    print_fires(&g_led); print_fires(&g_comms);
    CHECK(fires_eq(&g_led, (const uint32_t[]){ 4, 8, 12, 16, 20, 24, 28, 32, 36 }, 9) && g_led.max_late == 0);
    CHECK(fires_eq(&g_comms, (const uint32_t[]){ 14, 29 }, 2));
    CHECK(g_wrk.done == 1 && g_wrk.done_tick == 19);

    swtimer_t *show3[] = { &g_led, &g_x, &g_comms };
    scenario_b(false, 1, true);
    tl_print("Tmr prio 1 < ctrl prio 3 (ctrl 이 10..24 CPU 사용)", show3, 3, 40);
    print_fires(&g_led); print_fires(&g_x); print_fires(&g_comms);
    CHECK(fires_eq(&g_led, (const uint32_t[]){ 4, 8, 25, 25, 25, 25, 28, 32, 36 }, 9) && g_led.max_late == 13);
    CHECK(!g_x_active_after_start && fires_eq(&g_x, (const uint32_t[]){ 25 }, 1) && g_x.max_late == 4);
    CHECK(fires_eq(&g_comms, (const uint32_t[]){ 29 }, 1));
    CHECK(strcmp(g_order[2], "X@25") == 0);                 /* 깨어난 데몬은 큐부터: X 가 LED 따라잡기보다 먼저 */

    scenario_b(false, 6, true);
    tl_print("Tmr prio 6 > ctrl prio 3", show3, 3, 40);
    print_fires(&g_led); print_fires(&g_x); print_fires(&g_comms);
    CHECK(fires_eq(&g_led, (const uint32_t[]){ 4, 8, 12, 16, 20, 24, 28, 32, 36 }, 9) && g_led.max_late == 0);
    CHECK(g_x_active_after_start && fires_eq(&g_x, (const uint32_t[]){ 21 }, 1));
    CHECK(fires_eq(&g_comms, (const uint32_t[]){ 14, 29 }, 2));

    printf("\n== Part C: 통신 타임아웃 + LED 상태 표시 ==\n");
    sim_reset(6, 10);
    timer_init(&g_led, "LED", LED_SLOW, true, led_cb);
    timer_init(&g_comms, "COMMS", COMMS_TO, false, comms_timeout_cb);
    g_link_up = true; g_link_downs = g_link_ups = g_api_fail = 0;
    timer_start(&g_led); timer_start(&g_comms);
    sim_run(112, isr_link);
    print_fires(&g_led); print_fires(&g_comms);
    printf("  order:"); for (int i = 0; i < g_norder; i++) printf(" %s", g_order[i]); printf("\n");
    CHECK(fires_eq(&g_comms, (const uint32_t[]){ 55 }, 1) && g_link_downs == 1 && g_link_down_tick == 55);
    CHECK(g_link_ups == 1 && g_link_up_tick == 80 && g_link_up);
    CHECK(fires_eq(&g_led, (const uint32_t[]){ 8, 16, 24, 32, 40, 48, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 88, 96, 104 }, 21));
    CHECK(g_api_fail == 0 && g_cmd_fail == 0 && g_ctx_err == 0);
    CHECK(timer_is_active(&g_comms) && g_comms.expiry == 120);

    sim_reset(6, 10);                                        /* 명령 큐 넘침: 한 ISR 에서 reset 12번 */
    timer_init(&g_comms, "COMMS", 14, false, comms_cb_b);
    g_burst_ok = g_burst_fail = 0;
    sim_run(10, isr_burst_per_byte);
    printf("  per-byte reset x12 in one ISR (queue 10): ok=%d fail=%d\n", g_burst_ok, g_burst_fail);
    CHECK(g_burst_ok == 10 && g_burst_fail == 2 && g_comms.expiry == 19);
    sim_reset(6, 10); timer_init(&g_comms, "COMMS", 14, false, comms_cb_b);
    g_burst_ok = g_burst_fail = 0;
    sim_run(10, isr_burst_per_frame);
    CHECK(g_burst_ok == 1 && g_burst_fail == 0);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
