/* RTOS-B12 추가 연습 — 한 번에 검증
 *
 * 모델(타이머 리스트 + 명령 큐 + 데몬 + 틱 루프)은 solution.c 와 같다 (자기 완결 파일이라 복사해 두었다).
 *   연습 1  틱 랩어라운드: 0xFFFFFFF0 에서 시작해도 리스트 순서·만료가 맞는가 (before()/is_due() 의 부호 있는 차)
 *   연습 2  auto-reload 가 늦었을 때 "따라잡기"(FreeRTOS) vs "건너뛰기": LED 토글 위상
 *   연습 3  버튼 디바운스 = one-shot 5 틱 + 에지마다 reset (EXTI ISR)
 *   연습 4  워치독 킥을 타이머 콜백에서: 무조건 킥 vs 태스크 체크인 확인 후 킥
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
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
static bool g_skip_mode;                /* 연습 2: true 면 "건너뛰기" (지난 만료는 콜백 1번으로 합친다) */
static void tmr_expire(swtimer_t *t, uint32_t now) {
    uint32_t due = t->expiry;
    if (t->auto_reload && g_skip_mode) {           /* 연습 2 답: 콜백 1번, 다음 만료 = now 보다 뒤인 첫 격자점 */
        uint32_t next = due + t->period;
        while (is_due(next, now)) next += t->period;
        t->expiry = next;
        tmr_insert(t);
        tmr_fire(t, due, now);
        return;
    }
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
/* (pend_function_call 은 연습에서 안 써서 뺐다 — solution.c 참고) */
/* FromISR 판: 모델에서는 같은 동작 (ISR 은 원래 기다리지 않는다; 선점은 ISR 이 끝난 뒤 스케줄러가 판단) */
static bool timer_reset_from_isr(swtimer_t *t) { return timer_reset(t); }
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

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static void nop_cb(swtimer_t *t) { (void)t; }

/* ---- 연습 2: LED 토글 ---- */
static int g_led_state, g_toggles;
static void toggle_cb(swtimer_t *t) { (void)t; g_led_state ^= 1; g_toggles++; }
static void block10_cb(swtimer_t *t) { (void)t; cb_block(10); }
static int run_toggle(bool skip) {
    sim_reset(6, 10);
    g_skip_mode = skip; g_led_state = 0; g_toggles = 0;
    static swtimer_t led, flush;
    timer_init(&led, "LED", 4, true, toggle_cb);
    timer_init(&flush, "FLUSH", 9, false, block10_cb);
    timer_start(&led); timer_start(&flush);
    sim_run(21, NULL);                                 /* 0..20 */
    g_skip_mode = false;
    return g_led_state;
}

/* ---- 연습 3: 디바운스 ---- */
static swtimer_t g_deb;
static int g_presses;
static void deb_cb(swtimer_t *t) { (void)t; g_presses++; }       /* 5 틱 조용하면 "눌림 1번" 확정 */
static void exti_isr(uint32_t now) {
    static const uint32_t edges[] = { 10, 11, 13, 14, 40 };       /* 10~14 는 채터링 */
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++)
        if (now == edges[i]) timer_reset_from_isr(&g_deb);
}

/* ---- 연습 4: 워치독 ---- */
enum { CHK_A = 1, CHK_B = 2, CHK_ALL = 3, IWDG_TIMEOUT = 25 };
static swtimer_t g_kick;
static unsigned g_checkin;
static bool g_naive;
static uint32_t g_last_kick, g_reset_tick;
static int g_kicks;
static void kick_cb(swtimer_t *t) {
    (void)t;
    if (g_naive || (g_checkin & CHK_ALL) == CHK_ALL) {   /* 답: 모든 태스크가 체크인했을 때만 킥 */
        g_kicks++; g_last_kick = g_tick;
    }
    g_checkin = 0;
}
static void tasks_and_iwdg(uint32_t now) {               /* "태스크 체크인" 각본 + IWDG 하드웨어 카운터 */
    if (now % 5 == 0) g_checkin |= CHK_A;                /* 태스크 A: 계속 산다 */
    if (now % 5 == 0 && now <= 25) g_checkin |= CHK_B;   /* 태스크 B: 25 틱 뒤 멈춤 (데드락) */
    if (g_reset_tick == 0 && now - g_last_kick >= IWDG_TIMEOUT) g_reset_tick = now;   /* 리셋! */
}
static void run_wdg(bool naive) {
    sim_reset(6, 10);
    g_naive = naive; g_checkin = 0; g_last_kick = 0; g_reset_tick = 0; g_kicks = 0;
    timer_init(&g_kick, "KICK", 10, true, kick_cb);
    timer_start(&g_kick);
    sim_run(80, tasks_and_iwdg);
}

int main(void) {
    printf("== 연습 1: 틱 랩어라운드 ==\n");
    {
        sim_reset(6, 10);
        g_tick = g_tl0 = 0xFFFFFFF0u;
        swtimer_t led, a, b;
        timer_init(&led, "LED", 4, true, nop_cb);
        timer_init(&a, "A", 10, false, nop_cb);          /* 0xFFFFFFFA */
        timer_init(&b, "B", 30, false, nop_cb);          /* 0x0000000E — 숫자로는 작지만 더 늦다 */
        timer_start(&b); timer_start(&a); timer_start(&led);
        sim_run(1, NULL);
        CHECK(g_head == &led && led.next == &a && a.next == &b);
        sim_run(39, NULL);
        CHECK(fires_eq(&led, (const uint32_t[]){ 0xFFFFFFF4u, 0xFFFFFFF8u, 0xFFFFFFFCu, 0u, 4u, 8u, 12u, 16u, 20u }, 9));
        CHECK(fires_eq(&a, (const uint32_t[]){ 0xFFFFFFFAu }, 1) && fires_eq(&b, (const uint32_t[]){ 14u }, 1));
        CHECK(led.max_late == 0 && a.max_late == 0 && b.max_late == 0);
    }

    printf("\n== 연습 2: 따라잡기 vs 건너뛰기 ==\n");
    {
        int catchup = run_toggle(false); int n_catch = g_toggles;
        int skip = run_toggle(true);     int n_skip = g_toggles;
        int ideal = 5 & 1;                                 /* 막힘이 없었다면 4,8,12,16,20 → 5번 토글 → 1 */
        printf("  toggles by tick 20: catch-up %d, skip %d, ideal 5\n", n_catch, n_skip);
        CHECK(n_catch == 5 && catchup == ideal);           /* 19 에 2번 몰아서 → 위상 유지 */
        CHECK(n_skip == 4 && skip != ideal);               /* 1번만 → LED 가 반대 위상으로 굳는다 */
    }

    printf("\n== 연습 3: 버튼 디바운스 ==\n");
    {
        sim_reset(6, 10);
        timer_init(&g_deb, "DEB", 5, false, deb_cb);
        g_presses = 0;
        sim_run(60, exti_isr);
        swtimer_t *show[] = { &g_deb };
        tl_print("EXTI edges @10,11,13,14 (chatter) and @40", show, 1, 40);
        print_fires(&g_deb);
        CHECK(fires_eq(&g_deb, (const uint32_t[]){ 19, 45 }, 2) && g_presses == 2);   /* 14+5, 40+5 */
        CHECK(g_cmd_fail == 0 && !timer_is_active(&g_deb));
        /* 3b: 잠든 타이머에 change_period → 타이머가 "시작" 된다 (timers.c 1038: ACTIVE 를 켠다) */
        timer_change_period(&g_deb, 3);                    /* 틱 60, 태스크에서 */
        CHECK(timer_is_active(&g_deb));                    /* 데몬(6) > 호출자(0) → 즉시 처리됨 */
        sim_run(10, NULL);
        CHECK(g_presses == 3 && g_deb.at[2] == 63);        /* 버튼을 안 눌렀는데 "눌림" 1번 더! */
        timer_stop(&g_deb);
        CHECK(!timer_is_active(&g_deb));
    }

    printf("\n== 연습 4: 워치독 킥 ==\n");
    {
        run_wdg(true);
        printf("  naive : kicks=%d last_kick=%u reset_tick=%u\n", g_kicks, (unsigned)g_last_kick, (unsigned)g_reset_tick);
        CHECK(g_kicks == 7 && g_reset_tick == 0);          /* B 가 죽었는데도 계속 킥 → 리셋 안 됨 (나쁨) */
        run_wdg(false);
        printf("  checkin: kicks=%d last_kick=%u reset_tick=%u\n", g_kicks, (unsigned)g_last_kick, (unsigned)g_reset_tick);
        CHECK(g_kicks == 3 && g_last_kick == 30 && g_reset_tick == 55);   /* 30 + 25 */
    }

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
