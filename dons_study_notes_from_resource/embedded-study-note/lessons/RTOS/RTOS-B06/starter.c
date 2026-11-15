/* RTOS-B06 태스크 지연: Delay vs DelayUntil, tick wrap 안전 비교
 *
 *   미니 커널 (하네스 제공): OS 스레드 1개, tick 구동, 완전히 결정적. tick 카운터는 32비트(uint32_t)이고
 *   0xFFFFFFE0~F0 근처에서 시작해서 실행 도중에 0 으로 wrap 한다.
 *     매 tick:  ① dlist_expire(): delayed list 맨 앞부터 wake_tick 에 닿은 태스크 → READY
 *               ② sched_pick_next(): READY 중 우선순위 최고(같으면 먼저 READY 된 순) 1개가 step() 한 번
 *               ③ g_tick++ (0xFFFFFFFF 다음은 0)
 *   Part A  wrap 안전 비교 tick_reached(), wake_tick 순 delayed list (dlist_insert / dlist_expire),
 *           task_delay(ticks) (0 = yield), task_delay_until(&last, period) (FreeRTOS xTaskDelayUntil 의미)
 *   Part B  들쭉날쭉한 작업 시간 + 높은 우선순위 간섭에서 delay vs delay_until 드리프트 측정 (50 주기),
 *           wrap 을 가로지르는 깨우기 순서, 무작위 300 시나리오 (64비트 그림자 시계 오라클)
 *   Part C  IMU 100 Hz 샘플링 태스크의 overrun 정책: CATCH_UP (xTaskDelayUntil) vs SKIP (격자 유지, 놓친 주기 버림)
 *
 * 오라클: 하네스는 wrap 하지 않는 64비트 시계 g_tick64 를 따로 돌리며 "언제 깨어나야 하는지" 를 64비트로 계산해
 * 학생 코드(32비트)의 결과와 비교한다. 틀린 코드도 tick 상한에서 멈춘다 (무한 루프 없음).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st       (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (하네스 제공) ======================= */
#define MAX_TASKS  8
#define TL_LEN     64             /* 타임라인은 앞 64 tick 만 기록 */
#define MAX_REL    64
#define HALF_RANGE 0x80000000u    /* 2^31: 이 거리 이상 떨어진 미래는 32비트 비교로 구별할 수 없다 */

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
typedef struct tcb tcb_t;
typedef void (*step_fn)(tcb_t *t);

struct tcb {
    const char  *name;
    uint8_t      prio;            /* 클수록 급하다 (FreeRTOS 방향) */
    task_state_t state;
    uint32_t     wake_tick;       /* BLOCKED 일 때 깨어날 절대 tick (32비트 — wrap 한 값일 수 있다) */
    void        *wait_obj;        /* 이 레슨에선 안 씀 (B08/B09 의 세마포어 자리) */
    struct tcb  *next, *prev;     /* delayed list 링크 — 학생 코드가 관리 */
    step_fn      step;
    unsigned     ready_seq;       /* READY 가 된 순번: 같은 우선순위 FIFO */
    bool         done;
    /* ---- 태스크 프로그램이 쓰는 칸 ---- */
    int          pc, mode, k, nk, cost_left, nfalse;
    uint32_t     last, period, arg;
    uint32_t     rel[MAX_REL];    /* job 시작(릴리스) tick, g_start 기준 오프셋 */
    /* ---- 하네스 오라클 (64비트 그림자 시계) ---- */
    uint64_t     exp64, last64;
    bool         exp_valid;
    uint32_t     woke, ex_skipped;
    char         tl[TL_LEN + 1];
};

static tcb_t    g_task[MAX_TASKS];
static int      g_ntask;
static uint32_t g_tick, g_start;  /* 커널 tick (32비트, wrap 한다) / 시나리오 시작 tick */
static uint64_t g_tick64;         /* 오라클 전용: g_tick 의 wrap 안 된 값 (하위 32비트 == g_tick) */
static tcb_t   *g_dhead;          /* delayed list 머리: 가장 먼저 깰 태스크 */
static unsigned g_seq;
static int      g_wake_err;       /* 이른/늦은/엉뚱한 깨어남 */
static int      g_list_err;       /* 링크·정렬·개수 불변식 위반 */
static int      g_ret_err;        /* delay_until 반환값·last 갱신이 오라클과 다름 */
static int      g_nwakes;
static char     g_wakes[128];     /* 깨어난 순서 로그 */

static void k_reset(uint32_t start) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = 0; g_tick = g_start = start; g_tick64 = ((uint64_t)7 << 32) | start;
    g_dhead = NULL; g_seq = 0; g_wake_err = g_list_err = g_ret_err = g_nwakes = 0; g_wakes[0] = '\0';
}
static tcb_t *k_add(const char *name, uint8_t prio, step_fn fn) {
    tcb_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->step = fn;
    t->state = TASK_READY; t->ready_seq = ++g_seq;
    t->last = g_tick; t->last64 = g_tick64;          /* xLastWakeTime = xTaskGetTickCount() */
    return t;
}
static void k_make_ready(tcb_t *t) { t->state = TASK_READY; t->ready_seq = ++g_seq; }  /* 같은 우선순위 맨 뒤로 */
static void k_done(tcb_t *t) { t->state = TASK_SUSPENDED; t->done = true; }

static tcb_t *sched_pick_next(void) {
    tcb_t *best = NULL;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (t->state != TASK_READY) continue;
        if (!best || t->prio > best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq))
            best = t;
    }
    return best;
}

/* ======================= Part A — 학생 코드 ======================= */

/* now 가 wake 에 "닿았거나 지났나" — 둘의 실제 거리가 2^31 미만이면 wrap 과 상관없이 맞다.
 * (int32_t)(now - wake) >= 0 과 같은 뜻이지만, 2^31 이상인 uint32_t 를 int32_t 로 바꾸는 것은
 * 구현 정의(C11 6.3.1.3p3)라서 부호 없는 비교로 쓴다. */
static bool tick_reached(uint32_t now, uint32_t wake) {
    (void)now; (void)wake;                      /* TODO A1 */
    return false;
}

/* delayed list 에 wake_tick 순(가까운 미래가 앞)으로 넣는다. 같은 wake_tick 은 먼저 온 순(FIFO). */
static void dlist_insert(tcb_t *t) {
    (void)t;                                    /* TODO A2 */
}

/* tick 마다 커널이 부른다: 맨 앞부터 wake_tick 에 닿은 태스크를 빼서 READY. 정렬돼 있으니 첫 미달에서 멈춘다. */
static int dlist_expire(void) {
    /* TODO A3 */
    return 0;
}

static void block_until(tcb_t *self, uint32_t wake) {
    self->wake_tick = wake;
    self->state = TASK_BLOCKED;
    dlist_insert(self);
}

/* 상대 지연: "지금부터 ticks 뒤". 0 이면 블록하지 않고 yield (FreeRTOS vTaskDelay(0) 과 같음). */
static void task_delay(tcb_t *self, uint32_t ticks) {
    (void)self; (void)ticks;                    /* TODO A4 */
}

/* 절대 지연: 다음 깨어날 시각 = *last + period. *last 는 항상 그 값으로 갱신한다.
 * 그 시각이 이미 지났거나 바로 지금이면 블록하지 않고 false (FreeRTOS xTaskDelayUntil 이 pdFALSE). */
static bool task_delay_until(tcb_t *self, uint32_t *last, uint32_t period) {
    (void)self; (void)last; (void)period;       /* TODO A5 */
    return false;
}

/* ======================= Part C — 학생 코드 ======================= */
typedef enum { PT_CATCH_UP, PT_SKIP } pt_policy_t;
typedef struct {
    uint32_t    last, period;
    pt_policy_t policy;
    uint32_t    overruns;          /* delay_until 이 false 를 돌려준 횟수 */
    uint32_t    skipped;           /* SKIP 정책이 버린 격자점 수 */
} periodic_t;

/* 다음 주기까지 기다린다. CATCH_UP = delay_until 그대로 (놓친 주기를 연달아 실행해 따라잡는다).
 * SKIP = now 보다 엄격히 과거인 격자점은 버리고(skipped 에 셈) 그다음 격자점까지 delay_until. */
static bool periodic_wait(tcb_t *self, periodic_t *pt) {
    (void)self; (void)pt;                       /* TODO C1 */
    return false;
}

/* ======================= 커널 tick + 오라클 (하네스) ======================= */
static void dlist_check(void) {
    int n = 0, nb = 0;
    tcb_t *p = NULL;
    uint32_t prevd = 0;
    for (tcb_t *x = g_dhead; x && n <= MAX_TASKS; p = x, x = x->next, n++) {
        uint32_t d = x->wake_tick - g_tick;           /* 남은 tick (expire 뒤에는 1 이상이어야) */
        if (x->prev != p || x->state != TASK_BLOCKED || d == 0 || d >= HALF_RANGE || d < prevd) g_list_err++;
        prevd = d;
    }
    for (int i = 0; i < g_ntask; i++) if (g_task[i].state == TASK_BLOCKED) nb++;
    if (n != nb) g_list_err++;
}

static void kernel_tick(void) {
    bool was[MAX_TASKS];
    for (int i = 0; i < g_ntask; i++) was[i] = g_task[i].state == TASK_BLOCKED;
    dlist_expire();                                                   /* ① */
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (was[i] && t->state == TASK_READY) {
            g_nwakes++; t->woke = g_tick;
            size_t n = strlen(g_wakes);
            if (n + 4 < sizeof g_wakes) snprintf(g_wakes + n, sizeof g_wakes - n, "%s%s", n ? " " : "", t->name);
        }
        if (!t->exp_valid) continue;
        if (t->state == TASK_BLOCKED) {                               /* 아직 자는데 마감이 지났다 = 늦음 */
            if (t->exp64 <= g_tick64) { g_wake_err++; t->exp_valid = false; }
        } else {                                                      /* 깨어 있다: 이번 tick 에, 정확히 */
            if (!was[i] || t->exp64 != g_tick64) g_wake_err++;
            t->exp_valid = false;
        }
    }
    dlist_check();
    tcb_t *run = sched_pick_next();                                   /* ② */
    if (run) {
        run->state = TASK_RUNNING;
        run->step(run);
        if (run->state == TASK_RUNNING) run->state = TASK_READY;
    }
    uint32_t off = g_tick - g_start;
    if (off < TL_LEN)
        for (int i = 0; i < g_ntask; i++) {
            tcb_t *t = &g_task[i];
            t->tl[off] = t == run ? 'R' : t->done ? '.' : t->state == TASK_READY ? 'r'
                       : t->state == TASK_BLOCKED ? 'B' : 'S';
        }
    g_tick++; g_tick64++;                                             /* ③ 32비트는 wrap, 64비트는 안 함 */
}
static void k_run(uint32_t limit, const tcb_t *until_done) {
    for (uint32_t i = 0; i < limit; i++) {
        if (until_done && until_done->done) return;
        kernel_tick();
    }
}

/* 태스크 프로그램이 부르는 래퍼: 오라클이 64비트로 기대값을 먼저 계산하고 학생 함수를 부른다 */
static void prog_delay(tcb_t *t, uint32_t ticks) {
    if (ticks) { t->exp64 = g_tick64 + ticks; t->exp_valid = true; }
    task_delay(t, ticks);
}
static bool prog_delay_until(tcb_t *t, uint32_t *last, uint32_t period) {
    uint64_t wake64 = t->last64 + period;
    bool exp_block = wake64 > g_tick64;
    t->last64 = wake64;
    if (exp_block) { t->exp64 = wake64; t->exp_valid = true; }
    bool r = task_delay_until(t, last, period);
    if (r != exp_block || *last != (uint32_t)wake64) g_ret_err++;
    if (!r) t->nfalse++;
    return r;
}
static bool prog_periodic_wait(tcb_t *t, periodic_t *pt) {
    uint64_t el = g_tick64 - t->last64;
    if (pt->policy == PT_SKIP && el > pt->period) {
        uint64_t m = (el - 1) / pt->period;
        t->last64 += m * pt->period; t->ex_skipped += (uint32_t)m;
    }
    uint64_t wake64 = t->last64 + pt->period;
    bool exp_block = wake64 > g_tick64;
    t->last64 = wake64;
    if (exp_block) { t->exp64 = wake64; t->exp_valid = true; }
    bool r = periodic_wait(t, pt);
    if (r != exp_block || pt->last != (uint32_t)wake64 || pt->skipped != t->ex_skipped) g_ret_err++;
    if (!r) t->nfalse++;
    return r;
}

static void print_tl(const char *title, uint32_t nticks) {
    if (nticks > TL_LEN) nticks = TL_LEN;
    printf("%s\ntick :", title);
    for (uint32_t i = 0; i < nticks; i++) printf(" %x", (unsigned)((g_start + i) & 0xF));
    printf("    (하위 16진 한 자리)\nwrap :");
    for (uint32_t i = 0; i < nticks; i++) printf(" %c", (uint32_t)(g_start + i) == 0 ? '^' : ' ');
    printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%-5s:", g_task[i].name);
        for (uint32_t j = 0; j < nticks; j++) printf(" %c", g_task[i].tl[j] ? g_task[i].tl[j] : ' ');
        printf("\n");
    }
}

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- Part A 용 태스크 프로그램 ---- */
static char g_order[32];
static void yield_step(tcb_t *t) {               /* 이름을 찍고 vTaskDelay(0) */
    size_t n = strlen(g_order);
    if (n + 1 < sizeof g_order) { g_order[n] = t->name[0]; g_order[n + 1] = '\0'; }
    if (++t->k == 3) { k_done(t); return; }
    prog_delay(t, 0);
}

/* ---- Part B 용 태스크 프로그램 ---- */
#define B1_N   51                                 /* 릴리스 51번 = 주기 50개 */
#define B1_P   10
static uint32_t g_cost[B1_N];
static void per_step(tcb_t *t) {                  /* mode 0 = delay(P), mode 1 = delay_until(&last, P) */
    if (t->pc == 0) { t->rel[t->k] = g_tick - g_start; t->cost_left = (int)g_cost[t->k]; t->pc = 1; }
    if (--t->cost_left > 0) return;               /* 작업 중 (1 step = 1 tick) */
    t->pc = 0;
    if (++t->k == t->nk) { k_done(t); return; }
    if (t->mode == 0) prog_delay(t, t->period);
    else              prog_delay_until(t, &t->last, t->period);
}
static void comm_step(tcb_t *t) {                 /* 높은 우선순위 간섭: 7 tick 마다 2 tick (delay_until) */
    if (t->pc == 0) { t->cost_left = 2; t->pc = 1; }
    if (--t->cost_left > 0) return;
    t->pc = 0;
    prog_delay_until(t, &t->last, 7);
}
static void once_step(tcb_t *t) {                 /* B2: 한 번 지연하고, 깨면 끝 */
    if (t->pc == 0) {
        t->pc = 1;
        if (t->mode == 0) prog_delay(t, t->arg); else prog_delay_until(t, &t->last, t->arg);
        return;
    }
    k_done(t);
}
static void rnd_step(tcb_t *t) {                  /* B3: 무작위 작업 길이 + delay 또는 delay_until */
    if (t->pc == 0) { t->cost_left = 1 + (int)(xs32() % (uint32_t)t->nk); t->pc = 1; }
    if (--t->cost_left > 0) return;
    t->pc = 0;
    if (t->mode == 0) prog_delay(t, t->arg);
    else if (t->mode == 1) prog_delay_until(t, &t->last, t->arg);
    else { t->pc = 9; prog_delay(t, t->arg); }    /* mode 2: 아주 긴 잠 (깨면 안 됨) */
}

/* ---- Part C 용 태스크 프로그램 ---- */
static periodic_t g_pt;
static void imu_step(tcb_t *t) {                  /* 100 Hz IMU 샘플: 보통 2 tick, 4번째(k=3)만 flash 로그 25 tick */
    if (t->pc == 0) {
        if (t->k < MAX_REL) t->rel[t->k] = g_tick - g_start;
        t->cost_left = t->k == 3 ? 25 : 2;
        t->pc = 1;
    }
    if (--t->cost_left > 0) return;
    t->pc = 0; t->k++;
    prog_periodic_wait(t, &g_pt);
}
static void ble_step(tcb_t *t) {                  /* 낮은 우선순위 BLE 텔레메트리: 3 tick 일하고 delay(20) */
    if (t->pc == 0) { if (t->k < MAX_REL) t->rel[t->k] = g_tick - g_start; t->cost_left = 3; t->pc = 1; }
    if (--t->cost_left > 0) return;
    t->pc = 0; t->k++;
    prog_delay(t, 20);
}

static void list_str(char *buf, size_t cap) {
    buf[0] = '\0';
    for (tcb_t *x = g_dhead; x; x = x->next) {
        size_t n = strlen(buf);
        snprintf(buf + n, cap - n, "%s%s(%08x)", n ? " " : "", x->name, (unsigned)x->wake_tick);
    }
}

int main(void) {
    (void)k_make_ready; (void)block_until;      /* starter 에서 아직 안 부르는 서비스의 unused 경고 방지 */
    /* ================= Part A ================= */
    /* A1: tick_reached — 이름 붙은 경계 + 64비트 진짜 시간과 무작위 비교 */
    CHECK(tick_reached(5, 5) && tick_reached(6, 5) && !tick_reached(4, 5));
    CHECK(tick_reached(0x00000000u, 0xFFFFFFFFu) && !tick_reached(0xFFFFFFFFu, 0x00000000u));
    CHECK(tick_reached(0x00000004u, 0xFFFFFFF0u) && !tick_reached(0xFFFFFFF0u, 0x00000004u));
    CHECK(tick_reached(0x7FFFFFFFu, 0) && !tick_reached(0x80000000u, 0));   /* 정확히 2^31 은 "미래" 로 본다 */
    int bad = 0, naive_bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t a64 = ((uint64_t)xs32() << 32) | xs32();
        int64_t d = (int64_t)(xs32() & 0x7FFFFFFFu) * ((xs32() & 1) ? 1 : -1);  /* |d| < 2^31 */
        uint64_t b64 = a64 + (uint64_t)d;
        bool truth = d <= 0;                 /* now=a, wake=b=a+d: d <= 0 이면 이미 닿았다 (64비트 진실) */
        if (tick_reached((uint32_t)a64, (uint32_t)b64) != truth) bad++;
        if (((uint32_t)a64 >= (uint32_t)b64) != truth) naive_bad++;
    }
    printf("A1 tick_reached: 200000 random pairs (|d| < 2^31): wrong=%d   naive (now >= wake): wrong=%d\n", bad, naive_bad);
    CHECK(bad == 0);

    /* A2: delayed list 정렬 (wrap 을 가로지름) + 같은 wake 는 FIFO + prev 링크 */
    k_reset(0xFFFFFFF0u);
    tcb_t *a = k_add("A", 1, once_step), *b = k_add("B", 1, once_step), *c = k_add("C", 1, once_step);
    tcb_t *d = k_add("D", 1, once_step), *e = k_add("E", 1, once_step);
    task_delay(a, 20);               /* wake 0x00000004 (wrap 너머) */
    task_delay(b, 5);                /* wake 0xFFFFFFF5 */
    task_delay(c, 0x7FFFFFFFu);      /* 가장 먼 미래 (2^31 - 1) */
    task_delay(d, 5);                /* B 와 같은 wake → B 뒤 */
    task_delay(e, 16);               /* wake 0x00000000 정확히 */
    char ls[160];
    list_str(ls, sizeof ls);
    printf("A2 delayed list @0xfffffff0: %s\n", ls);
    CHECK(strcmp(ls, "B(fffffff5) D(fffffff5) E(00000000) A(00000004) C(7fffffef)") == 0);
    CHECK(g_dhead == b && b->next == d && d->prev == b && a->next == c && c->next == NULL && c->prev == a);
    dlist_check();
    CHECK(g_list_err == 0);
    g_tick = 0xFFFFFFF4u; CHECK(dlist_expire() == 0);
    g_tick = 0xFFFFFFF5u; CHECK(dlist_expire() == 2 && b->state == TASK_READY && d->state == TASK_READY && g_dhead == e);
    g_tick = 0x00000000u; CHECK(dlist_expire() == 1 && g_dhead == a && a->prev == NULL);
    g_tick = 0x00000010u; CHECK(dlist_expire() == 1 && g_dhead == c && c->state == TASK_BLOCKED);

    /* A3: delay(0) = yield → 같은 우선순위끼리 번갈아 */
    k_reset(0xFFFFFFFEu); g_order[0] = '\0';
    k_add("X", 2, yield_step); k_add("Y", 2, yield_step);
    k_run(20, NULL);
    printf("A3 delay(0) order: %s\n", g_order);
    CHECK(strcmp(g_order, "XYXYXY") == 0);

    /* A4: delay_until 반환값과 *last 갱신 (FreeRTOS xTaskDelayUntil 의 표) */
    k_reset(100);
    tcb_t *u = k_add("U", 1, once_step);
    uint32_t last = 100;
    g_tick = 105; bool r1 = task_delay_until(u, &last, 10);           /* 아직 110 전 → 블록 */
    CHECK(r1 && last == 110 && u->state == TASK_BLOCKED && u->wake_tick == 110);
    dlist_expire(); g_tick = 110; dlist_expire();
    g_tick = 125; bool r2 = task_delay_until(u, &last, 10);           /* 120 이미 지남 → false */
    CHECK(!r2 && last == 120 && u->state == TASK_READY && g_dhead == NULL);
    g_tick = 130; bool r3 = task_delay_until(u, &last, 10);           /* 마감 == now → false */
    CHECK(!r3 && last == 130 && g_dhead == NULL);
    last = 0xFFFFFFFAu;
    g_tick = 0xFFFFFFFEu; bool r4 = task_delay_until(u, &last, 10);   /* wake 가 wrap (0x4), now 는 아직 */
    CHECK(r4 && last == 4u && u->wake_tick == 4u);
    g_tick = 4; dlist_expire();
    last = 0xFFFFFFFAu;
    g_tick = 3; bool r5 = task_delay_until(u, &last, 10);             /* 둘 다 wrap, 4 > 3 → 블록 */
    CHECK(r5 && last == 4u);
    g_tick = 4; dlist_expire();
    last = 0xFFFFFFFAu;
    g_tick = 5; bool r6 = task_delay_until(u, &last, 10);             /* 둘 다 wrap, 4 < 5 → false */
    CHECK(!r6 && last == 4u && g_dhead == NULL);

    /* ================= Part B ================= */
    /* B1: 작업 시간 1..4 tick (고정 seed) + COMM(p3) 간섭, P = 10 tick, 릴리스 51번 */
    g_rng = 0xB06B06u;
    for (int i = 0; i < B1_N; i++) g_cost[i] = 1 + xs32() % 4;
    uint32_t relv[2][B1_N];
    int nfalse[2], werr[2], lerr[2], rerr[2];
    uint32_t endtick[2];
    for (int mode = 0; mode < 2; mode++) {
        k_reset(0xFFFFFFF0u);
        k_add("COMM", 3, comm_step);
        tcb_t *p = k_add("PER", 2, per_step);
        p->mode = mode; p->period = B1_P; p->nk = B1_N;
        k_run(3000, p);
        print_tl(mode == 0 ? "B1 timeline — PER uses delay(10)" : "B1 timeline — PER uses delay_until(&last, 10)", 48);
        memcpy(relv[mode], p->rel, sizeof relv[mode]);
        nfalse[mode] = p->nfalse; werr[mode] = g_wake_err; lerr[mode] = g_list_err; rerr[mode] = g_ret_err;
        endtick[mode] = g_tick;
        CHECK(p->done && p->k == B1_N);
    }
    printf("B1 drift table (P=%d, release tick offset from start 0xfffffff0; ideal = k*P)\n", B1_P);
    printf("  k  cost  ideal | delay: rel  drift | delay_until: rel  late\n");
    int ks[] = { 0, 1, 2, 3, 4, 5, 10, 20, 30, 40, 50 };
    for (unsigned i = 0; i < sizeof ks / sizeof ks[0]; i++) {
        int k = ks[i];
        printf(" %2d  %4u  %5d | %10u  %5d | %16u  %4d\n", k, (unsigned)g_cost[k], k * B1_P,
               (unsigned)relv[0][k], (int)relv[0][k] - k * B1_P, (unsigned)relv[1][k], (int)relv[1][k] - k * B1_P);
    }
    printf("drift :");
    for (int k = 0; k < B1_N; k++) printf(" %d", (int)relv[0][k] - k * B1_P);
    printf("\nlate  :");
    for (int k = 0; k < B1_N; k++) printf(" %d", (int)relv[1][k] - k * B1_P);
    uint32_t sumc = 0;
    for (int k = 0; k < B1_N - 1; k++) sumc += g_cost[k] - 1;
    int maxlate = 0, minper = 1 << 30;
    for (int k = 0; k < B1_N; k++) if ((int)relv[1][k] - k * B1_P > maxlate) maxlate = (int)relv[1][k] - k * B1_P;
    for (int k = 0; k + 1 < B1_N; k++) if ((int)(relv[0][k + 1] - relv[0][k]) < minper) minper = (int)(relv[0][k + 1] - relv[0][k]);
    printf("\nB1 summary: delay: 50 periods took %u ticks (mean %.2f), drift %d; sum(cost-1)=%u, min period %d\n",
           (unsigned)(relv[0][50] - relv[0][0]), (relv[0][50] - relv[0][0]) / 50.0, (int)relv[0][50] - 50 * B1_P,
           (unsigned)sumc, minper);
    printf("           delay_until: 50 periods took %u ticks (mean %.2f), max late %d, false returns %d; "
           "end tick 0x%08x / 0x%08x\n", (unsigned)(relv[1][50] - relv[1][0]), (relv[1][50] - relv[1][0]) / 50.0,
           maxlate, nfalse[1], (unsigned)endtick[0], (unsigned)endtick[1]);
    bool on_grid = true;
    for (int k = 0; k < B1_N; k++) { int l = (int)relv[1][k] - k * B1_P; if (l < 0 || l > 2) on_grid = false; }
    CHECK(on_grid && nfalse[1] == 0);                       /* delay_until: 늦음 0..2, 쌓이지 않음 */
    CHECK(minper >= B1_P && (int)relv[0][50] - 50 * B1_P >= (int)sumc);   /* delay: 주기 ≥ P, 드리프트 ≥ Σ(cost-1) */
    CHECK(werr[0] == 0 && werr[1] == 0 && lerr[0] == 0 && lerr[1] == 0 && rerr[0] == 0 && rerr[1] == 0);
    CHECK(endtick[0] < 0xFFFFFFF0u && endtick[1] < 0xFFFFFFF0u);   /* 실제로 wrap 을 지났다 */

    /* B2: wrap 을 가로지르는 깨우기 순서 (각 태스크는 자기 tick 에 지연을 건다: A@f0 B@f1 ...) */
    k_reset(0xFFFFFFF0u);
    tcb_t *ta = k_add("A", 2, once_step); ta->arg = 20;              /* f0+20 = 0x04 */
    tcb_t *tb = k_add("B", 2, once_step); tb->arg = 5;               /* f1+5  = 0xf6 */
    tcb_t *tc = k_add("C", 2, once_step); tc->arg = 30;              /* f2+30 = 0x10 */
    tcb_t *td = k_add("D", 2, once_step); td->arg = 3;               /* f3+3  = 0xf6 (B 와 같음, B 뒤) */
    tcb_t *te = k_add("E", 2, once_step); te->arg = 25; te->mode = 1; /* delay_until(last=f0, 25) = 0x09 */
    tcb_t *tf = k_add("F", 2, once_step); tf->arg = 0x7FFFFFFFu;     /* f5+2^31-1: 이 실행 안에서 깨면 안 됨 */
    k_run(6, NULL);
    list_str(ls, sizeof ls);
    printf("B2 list after 6 ticks: %s\n", ls);
    k_run(40, NULL);
    print_tl("B2 timeline — wake order across the wrap", 40);
    printf("B2 wake order: %s   woke at: B=%08x D=%08x A=%08x E=%08x C=%08x\n", g_wakes, (unsigned)tb->woke,
           (unsigned)td->woke, (unsigned)ta->woke, (unsigned)te->woke, (unsigned)tc->woke);
    CHECK(strcmp(ls, "B(fffffff6) D(fffffff6) A(00000004) E(00000009) C(00000010) F(7ffffff4)") == 0);
    CHECK(strcmp(g_wakes, "B D A E C") == 0);
    CHECK(tb->woke == 0xFFFFFFF6u && td->woke == 0xFFFFFFF6u && ta->woke == 4 && te->woke == 9 && tc->woke == 0x10);
    CHECK(tf->state == TASK_BLOCKED && g_wake_err == 0 && g_ret_err == 0 && g_list_err == 0);

    /* B3: 무작위 300 시나리오 — 시작 tick 절반은 wrap 직전, 64비트 오라클이 모든 깨어남을 검사 */
    g_rng = 0x5EED0B06u;
    int sc_err = 0, wraps = 0; long wakes = 0, falses = 0;
    for (int s = 0; s < 300; s++) {
        uint32_t st = (s & 1) ? xs32() : 0xFFFFFFFFu - xs32() % 300;
        k_reset(st);
        int nt = 2 + (int)(xs32() % 4);
        for (int i = 0; i < nt; i++) {
            static const char *nm[] = { "T0", "T1", "T2", "T3", "T4" };
            tcb_t *t = k_add(nm[i], (uint8_t)(1 + xs32() % 3), rnd_step);
            t->mode = (int)(xs32() % 2);
            t->arg = t->mode == 0 ? xs32() % 25 : 1 + xs32() % 25;     /* delay 0..24 (0 = yield), 주기 1..25 */
            t->nk = 1 + (int)(xs32() % 8);                              /* 작업 1..nk tick → 주기보다 길 수도 */
            if (t->mode == 0 && t->arg == 0 && t->prio == 3) t->arg = 1; /* 최고 우선순위의 무한 yield 는 빼둔다 */
        }
        tcb_t *sl = k_add("SL", 3, rnd_step); sl->mode = 2; sl->nk = 1; sl->arg = 0x7FFFFFFFu - xs32() % 16;
        k_run(400, NULL);
        if (g_tick < st) wraps++;
        wakes += g_nwakes;
        for (int i = 0; i < g_ntask; i++) falses += g_task[i].nfalse;
        if (g_wake_err || g_ret_err || g_list_err || sl->state != TASK_BLOCKED) sc_err++;
    }
    printf("B3 random: 300 scenarios, wrapped in %d, wakes=%ld, delay_until false=%ld, scenarios with errors=%d\n",
           wraps, wakes, falses, sc_err);
    CHECK(sc_err == 0 && wraps >= 150 && wakes > 10000 && falses > 0);

    /* ================= Part C ================= */
    for (int pol = 0; pol < 2; pol++) {
        k_reset(0xFFFFFFE0u);
        tcb_t *imu = k_add("IMU", 3, imu_step);
        tcb_t *ble = k_add("BLE", 1, ble_step);
        g_pt = (periodic_t){ .last = g_tick, .period = 10, .policy = pol ? PT_SKIP : PT_CATCH_UP };
        k_run(120, NULL);
        print_tl(pol ? "C timeline — SKIP" : "C timeline — CATCH_UP", 64);
        printf("C %-8s IMU releases:", pol ? "SKIP" : "CATCH_UP");
        for (int k = 0; k < imu->k + 1 && k < MAX_REL && (k == 0 || imu->rel[k]); k++) printf(" %u", (unsigned)imu->rel[k]);
        printf("  | overruns=%u skipped=%u  BLE releases:", (unsigned)g_pt.overruns, (unsigned)g_pt.skipped);
        for (int k = 0; k < ble->k + 1 && k < MAX_REL && (k == 0 || ble->rel[k]); k++) printf(" %u", (unsigned)ble->rel[k]);
        printf("\n");
        int nrel = 0; bool grid_after = true;
        for (int k = 0; k < MAX_REL && (k == 0 || imu->rel[k]); k++) {
            nrel++;
            if (imu->rel[k] >= 60 && imu->rel[k] % 10 != 0) grid_after = false;
        }
        CHECK(g_wake_err == 0 && g_ret_err == 0 && g_list_err == 0);
        if (pol == 0) {
            CHECK(g_pt.overruns == 2 && g_pt.skipped == 0 && nrel == 12 && imu->rel[4] == 55 && imu->rel[5] == 57);
        } else {
            CHECK(g_pt.overruns == 0 && g_pt.skipped == 2 && nrel == 10 && imu->rel[4] == 60);
        }
        CHECK(grid_after);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
