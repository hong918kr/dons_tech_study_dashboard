/* RTOS-B13 추가 연습 — 이벤트 그룹 (practice.c)
 *
 *   위쪽 미니 커널 + 이벤트 그룹(Part A 정답)은 solution.c 와 같다. 아래 연습 4개를 한 번에 검증한다.
 *   연습 1  eg_sync 랑데부 (xEventGroupSync): 3 태스크 2라운드 + "set 따로, wait 따로" 가 왜 막히나
 *   연습 2  ISR 의 set 은 타이머 데몬 태스크로 미뤄진다: 데몬 우선순위 → 지연, 커맨드 큐 가득 → 실패
 *   연습 3  wait-any + clear-on-exit 의 반환값: else-if 로 하나만 처리하면 이벤트를 잃는다
 *   연습 4  틀린 set (깨우자마자 지우기): 대기 리스트 순서에 따라 한 명씩 잃는다
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (하네스 제공) ======================= */
#define MAX_TASKS 8
#define TL_LEN    64          /* 타임라인은 앞 64 tick 만 기록 */

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED,
               TASK_DORMANT /* 아직 출발 전 */, TASK_DELETED /* 끝 */ } task_state_t;

typedef uint32_t ebits_t;
#define EG_CTRL_MASK 0xFF000000u  /* 상위 8비트 = 커널 제어용 → 사용자 비트 24개 (FreeRTOS 32비트 tick 과 같음) */
#define EG_ANY       0x0          /* 하나라도 */
#define EG_ALL       0x1          /* 전부 (wait-for-all) */
#define EG_CLEAR     0x2          /* clear on exit: 조건이 맞아 나갈 때 want 비트를 지운다 */
#define EG_FOREVER   (-1)
enum { EG_OK, EG_TIMEOUT, EG_BLOCKED, EG_EINVAL };

typedef struct tcb tcb_t;
typedef void (*step_fn)(tcb_t *t);
struct tcb {
    const char *name;
    uint8_t prio;             /* 클수록 급하다 (FreeRTOS 방향. Zephyr/µC/OS 는 반대) */
    task_state_t state;
    uint32_t wake_tick;       /* 지연·타임아웃 마감 tick */
    void *wait_obj;           /* 기다리는 이벤트 그룹 (NULL = 시간만 기다리거나 영원히 멈춤) */
    struct tcb *next, *prev;  /* 이벤트 그룹 대기 리스트 링크 — 학생 코드가 관리 */
    /* 이벤트 그룹 대기 정보 (FreeRTOS 는 xEventListItem 의 값 하나에 모두 넣는다) */
    ebits_t ev_want;          /* 기다리는 비트 */
    int ev_flags;             /* EG_ALL | EG_CLEAR */
    ebits_t ev_value;         /* set 이 깨울 때 넣어 주는 "그 순간의 비트 값" (지우기 전) */
    bool ev_by_set;           /* true = 조건이 맞아 깨어남, false = 타임아웃 */
    /* 시뮬레이터 */
    step_fn step;
    int pc, release, has_timeout;
    unsigned ready_seq;
    /* 태스크 프로그램이 쓰는 칸 */
    ebits_t arg_bits, got;
    int arg_flags, arg_to, rc, got_at, cnt, left, aux, ran;
    char tl[TL_LEN + 1];
};

typedef struct {
    ebits_t bits;
    tcb_t *head, *tail;       /* 대기 리스트: 정렬 없음, 끝에 붙인다 (FreeRTOS xTasksWaitingForBits) */
    int nwait;
} evgroup_t;

static tcb_t    g_task[MAX_TASKS];
static int      g_ntask;
static uint32_t g_now;
static unsigned g_seq;
static int      g_assert_fail;          /* KASSERT 실패 수 (configASSERT 자리: 테스트는 세기만 한다) */
static char     g_wakes[256];           /* 이벤트로 깨어난 순서 */
static char     g_runlog[256];          /* 대기에서 돌아와 처음 돈 순서 */
static void   (*g_tick_hook)(uint32_t now);
static void   (*g_check_hook)(void);

#define KASSERT(x) do { if (!(x)) g_assert_fail++; } while (0)

static void str_add(char *buf, size_t cap, const char *s) {
    size_t n = strlen(buf);
    snprintf(buf + n, cap - n, "%s%s", n ? " " : "", s);
}
static void k_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = 0; g_now = 0; g_seq = 0; g_assert_fail = 0;
    g_wakes[0] = g_runlog[0] = '\0';
    g_tick_hook = NULL; g_check_hook = NULL;
}
static tcb_t *k_add(const char *name, int prio, int release, step_fn fn) {
    tcb_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = (uint8_t)prio; t->release = release; t->step = fn;
    t->state = TASK_DORMANT; t->rc = -1; t->got_at = -1;
    return t;
}
/* 이벤트 그룹 코드가 부르는 커널 서비스 */
static void k_block(tcb_t *t, void *obj, int timeout) {        /* timeout: tick 수 또는 EG_FOREVER */
    t->state = TASK_BLOCKED; t->wait_obj = obj;
    t->has_timeout = timeout >= 0;
    t->wake_tick = g_now + (uint32_t)(timeout > 0 ? timeout : 0);
}
static void k_unblock(tcb_t *t) {                                /* 이벤트로 깨움 → READY (순서 로그) */
    str_add(g_wakes, sizeof g_wakes, t->name);
    t->state = TASK_READY; t->wait_obj = NULL; t->ready_seq = ++g_seq;
}
static void task_delay(tcb_t *t, int n) {
    t->state = TASK_BLOCKED; t->wait_obj = NULL; t->has_timeout = 1;
    t->wake_tick = g_now + (uint32_t)(n > 0 ? n : 1);
}
static void k_done(tcb_t *t) { t->state = TASK_DELETED; }
static ebits_t eg_get(const evgroup_t *g) { return g->bits; }    /* = xEventGroupGetBits() */
static void eg_init(evgroup_t *g) { memset(g, 0, sizeof *g); }

static void eg_on_timeout(evgroup_t *g, tcb_t *t);               /* 학생 (Part A5) */
static void k_timeout_now(tcb_t *t) {                            /* 커널: 마감이 된 대기자 처리 */
    if (t->wait_obj) eg_on_timeout((evgroup_t *)t->wait_obj, t);
    t->state = TASK_READY; t->wait_obj = NULL; t->ready_seq = ++g_seq;
}
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
static void kernel_tick(void) {
    if (g_tick_hook) g_tick_hook(g_now);                         /* ① ISR / 데몬 */
    for (int i = 0; i < g_ntask; i++) {                          /* ② 출발 · 마감 (wrap-safe 비교, RTOS-B06) */
        tcb_t *t = &g_task[i];
        if (t->state == TASK_DORMANT && (int32_t)(g_now - (uint32_t)t->release) >= 0) {
            t->state = TASK_READY; t->ready_seq = ++g_seq;
        } else if (t->state == TASK_BLOCKED && t->has_timeout && (int32_t)(g_now - t->wake_tick) >= 0) {
            k_timeout_now(t);
        }
    }
    tcb_t *run = sched_pick_next();                              /* ③ 한 걸음 */
    if (run) {
        run->state = TASK_RUNNING;
        run->step(run);
        run->ran++;
        if (run->state == TASK_RUNNING) run->state = TASK_READY; /* 순번(ready_seq)은 그대로 */
    }
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        char c = t == run ? 'R' : t->state == TASK_READY ? 'r'
               : t->state == TASK_BLOCKED ? (t->wait_obj || !t->has_timeout ? 'B' : 'd')
               : t->state == TASK_SUSPENDED ? 'S' : '.';
        if (g_now < TL_LEN) t->tl[g_now] = c;
    }
    if (g_check_hook) g_check_hook();
    g_now++;
}
static bool k_all_done(void) {
    for (int i = 0; i < g_ntask; i++) if (g_task[i].state != TASK_DELETED) return false;
    return true;
}
static int k_run(int limit) { while ((int)g_now < limit && !k_all_done()) kernel_tick(); return (int)g_now; }
static void k_print(int to) {
    if (to > TL_LEN) to = TL_LEN;
    printf("tick : ");
    for (int t = 0; t < to; t++) printf("%d ", t % 10);
    printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%-5.5s: ", g_task[i].name);
        for (int t = 0; t < to; t++) printf("%c ", g_task[i].tl[t] ? g_task[i].tl[t] : '.');
        printf("\n");
    }
}

/* ======================= Part A — 이벤트 그룹 (학생 구현) ==================== */

/* A0. 대기 리스트: 끝에 붙이기 (FreeRTOS listINSERT_END), 아무 위치에서나 O(1) 제거 (prev 포인터) */
static void wl_push_back(evgroup_t *g, tcb_t *t) {
    t->next = NULL; t->prev = g->tail;
    if (g->tail) g->tail->next = t; else g->head = t;
    g->tail = t;
    g->nwait++;
}
static void wl_remove(evgroup_t *g, tcb_t *t) {
    if (t->prev) t->prev->next = t->next; else g->head = t->next;
    if (t->next) t->next->prev = t->prev; else g->tail = t->prev;
    t->next = t->prev = NULL;
    g->nwait--;
}

/* A1. 조건: ALL 이면 want 비트가 전부 1, ANY 이면 하나라도 1 */
static bool eg_cond_met(ebits_t bits, ebits_t want, bool all) {
    return all ? (bits & want) == want : (bits & want) != 0;
}

/* A2. wait: want == 0 이거나 제어 비트를 건드리면 KASSERT + EG_EINVAL.
 *     ① 이미 조건이 맞으면: *out = 지금 값(지우기 전), EG_CLEAR 면 want 를 지우고 EG_OK
 *     ② 아니고 timeout == 0 이면: *out = 지금 값, EG_TIMEOUT (블록 안 함 = 폴링)
 *     ③ 아니면: ev_want/ev_flags 를 TCB 에 적고 대기 리스트 끝에 붙인 뒤 k_block → EG_BLOCKED */
static int eg_wait(evgroup_t *g, tcb_t *self, ebits_t want, int flags, int timeout, ebits_t *out) {
    KASSERT(want != 0 && (want & EG_CTRL_MASK) == 0);
    if (want == 0 || (want & EG_CTRL_MASK)) { *out = g->bits; return EG_EINVAL; }
    ebits_t cur = g->bits;
    if (eg_cond_met(cur, want, flags & EG_ALL)) {
        *out = cur;
        if (flags & EG_CLEAR) g->bits &= ~want;
        return EG_OK;
    }
    if (timeout == 0) { *out = cur; return EG_TIMEOUT; }
    self->ev_want = want; self->ev_flags = flags;
    self->ev_value = 0; self->ev_by_set = false;
    wl_push_back(g, self);
    k_block(self, g, timeout);
    *out = 0;
    return EG_BLOCKED;
}

/* A3. set: 비트를 OR 하고, 대기 리스트를 "끝까지" 돌며 조건이 맞는 대기자를 모두 깨운다.
 *     깨운 태스크에는 ev_value = 지금 비트(지우기 전), ev_by_set = true.
 *     EG_CLEAR 대기자의 want 는 모아 두었다가 루프가 끝난 뒤 한 번에 지운다. 반환 = 최종 비트 값 */
static ebits_t eg_set(evgroup_t *g, ebits_t set) {
    KASSERT((set & EG_CTRL_MASK) == 0);
    set &= ~EG_CTRL_MASK;
    ebits_t clr = 0;
    g->bits |= set;
    for (tcb_t *t = g->head, *nx; t; t = nx) {
        nx = t->next;                                   /* t 가 리스트에서 빠져도 다음으로 갈 수 있게 */
        if (!eg_cond_met(g->bits, t->ev_want, t->ev_flags & EG_ALL)) continue;
        if (t->ev_flags & EG_CLEAR) clr |= t->ev_want;  /* 지금 지우면 뒤 대기자가 못 본다 → 모아 둔다 */
        t->ev_value = g->bits; t->ev_by_set = true;
        wl_remove(g, t);
        k_unblock(t);
    }
    g->bits &= ~clr;
    return g->bits;
}

/* A4. clear: 지우기 "전" 값을 돌려준다 (= 원자적 fetch-and-clear) */
static ebits_t eg_clear(evgroup_t *g, ebits_t clr) {
    KASSERT((clr & EG_CTRL_MASK) == 0);
    ebits_t old = g->bits;
    g->bits &= ~clr;
    return old;
}

/* A5. 커널이 마감 tick 에 부른다: 대기 리스트에서 빼고 "타임아웃으로 깨어남" 표시 */
static void eg_on_timeout(evgroup_t *g, tcb_t *t) {
    wl_remove(g, t);
    t->ev_by_set = false;
}

/* A6. 블록했던 태스크가 다시 돌 때 결과를 받는다.
 *     set 으로 깨어났으면 *out = ev_value, EG_OK.
 *     타임아웃이면 *out = 지금 값, 그리고 "그 사이 조건이 맞았는지" 다시 본다:
 *     맞으면 EG_CLEAR 처리 후 EG_OK, 아니면 EG_TIMEOUT */
static int eg_wait_result(evgroup_t *g, tcb_t *self, ebits_t *out) {
    if (self->ev_by_set) { *out = self->ev_value; return EG_OK; }
    ebits_t cur = g->bits;
    *out = cur;
    if (eg_cond_met(cur, self->ev_want, self->ev_flags & EG_ALL)) {
        if (self->ev_flags & EG_CLEAR) g->bits &= ~self->ev_want;
        return EG_OK;
    }
    return EG_TIMEOUT;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static evgroup_t g_eg;
static int g_set_err, g_list_err;

/* 오라클: 학생 코드와 독립적으로 "이번 set 이 누구를 깨우고 비트가 얼마가 되어야 하나" 계산 */
static bool ref_met(ebits_t b, ebits_t w, bool all) { return all ? (b & w) == w : (b & w) != 0; }
static ebits_t H_set(evgroup_t *g, ebits_t set) {
    ebits_t now = g->bits | set, clr = 0;
    bool waiting[MAX_TASKS], expect[MAX_TASKS];
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        waiting[i] = t->state == TASK_BLOCKED && t->wait_obj == g;
        expect[i] = waiting[i] && ref_met(now, t->ev_want, t->ev_flags & EG_ALL);
        if (expect[i] && (t->ev_flags & EG_CLEAR)) clr |= t->ev_want;
    }
    ebits_t r = eg_set(g, set);
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_task[i];
        if (expect[i] && (t->state != TASK_READY || !t->ev_by_set || t->ev_value != now)) g_set_err++;
        if (waiting[i] && !expect[i] && (t->state != TASK_BLOCKED || t->wait_obj != g)) g_set_err++;
    }
    if (g->bits != (now & ~clr) || r != g->bits) g_set_err++;
    return r;
}
/* 대기 리스트 일관성: prev 링크, tail, nwait, 그리고 "이 그룹에서 BLOCKED 인 태스크 수" 와 일치 */
static bool eg_list_ok(const evgroup_t *g) {
    int n = 0;
    const tcb_t *prev = NULL;
    for (const tcb_t *t = g->head; t; prev = t, t = t->next)
        if (t->prev != prev || t->state != TASK_BLOCKED || t->wait_obj != g || ++n > MAX_TASKS) return false;
    if (g->tail != prev || n != g->nwait) return false;
    int blocked = 0;
    for (int i = 0; i < g_ntask; i++) blocked += g_task[i].state == TASK_BLOCKED && g_task[i].wait_obj == g;
    return blocked == n;
}
static void list_check(void) { if (!eg_list_ok(&g_eg)) g_list_err++; }

/* 스크립트: tick hook 이 정해진 tick 에 비트를 세운다 (ISR → 데몬이 set 한 셈) */
typedef struct { int tick; ebits_t bits; } ev_t;
static const ev_t *g_script;
static int g_nscript, g_last_set;
static void script_hook(uint32_t now) {
    for (int i = 0; i < g_nscript; i++)
        if ((uint32_t)g_script[i].tick == now) { H_set(&g_eg, g_script[i].bits); g_last_set = (int)now; }
}
static void setup(const ev_t *script, int n) {
    k_reset(); eg_init(&g_eg);
    g_set_err = g_list_err = 0; g_last_set = -1;
    g_script = script; g_nscript = n;
    g_tick_hook = script_hook; g_check_hook = list_check;
}

/* ---- 공용 태스크: arg_bits 를 arg_flags 로 arg_to tick 기다리고, 결과를 남기고 끝 ---- */
static void waiter_task(tcb_t *t) {
    int rc = t->pc == 0 ? eg_wait(&g_eg, t, t->arg_bits, t->arg_flags, t->arg_to, &t->got)
                        : eg_wait_result(&g_eg, t, &t->got);
    if (rc == EG_BLOCKED) { t->pc = 1; return; }
    t->rc = rc; t->got_at = (int)g_now;
    str_add(g_runlog, sizeof g_runlog, t->name);
    k_done(t);
}
static tcb_t *add_waiter(const char *n, int prio, int rel, ebits_t want, int flags, int to) {
    tcb_t *t = k_add(n, prio, rel, waiter_task);
    t->arg_bits = want; t->arg_flags = flags; t->arg_to = to;
    return t;
}
/* 높은 태스크가 자기 차례에 비트를 세우고 끝 (B2c) */
static void setter_task(tcb_t *t) { H_set(&g_eg, t->arg_bits); g_last_set = (int)g_now; k_done(t); }


/* ======================= 연습 1 — 랑데부: eg_sync (xEventGroupSync) ======================= */
#define SY_A   0x1u
#define SY_B   0x2u
#define SY_C   0x4u
#define SY_ALL 0x7u
/* 내 비트를 세우고 + 전부 모일 때까지 기다리고 + 나갈 때 지우기를 "한 번에" */
static int eg_sync(evgroup_t *g, tcb_t *self, ebits_t set, ebits_t wait, int timeout, ebits_t *out) {
    KASSERT(wait != 0 && (wait & EG_CTRL_MASK) == 0);
    ebits_t orig = g->bits;
    (void)eg_set(g, set);                           /* 먼저 온 대기자들은 여기서 깨어난다 (그들의 CLEAR 로 비트가 지워질 수 있다) */
    if (((orig | set) & wait) == wait) {            /* 내가 마지막: 지워졌더라도 orig|set 으로 판단 */
        *out = orig | set;
        g->bits &= ~wait;                           /* "Rendezvous always clear the bits" */
        return EG_OK;
    }
    if (timeout == 0) { *out = g->bits; return EG_TIMEOUT; }
    self->ev_want = wait; self->ev_flags = EG_ALL | EG_CLEAR;
    self->ev_value = 0; self->ev_by_set = false;
    wl_push_back(g, self);
    k_block(self, g, timeout);
    *out = 0;
    return EG_BLOCKED;
}
static int g_arrive[2][3], g_leave[2][3], g_sync_fail, g_naive;
static void sync_task(tcb_t *t) {               /* left tick 일 → 랑데부 → 2라운드 */
    ebits_t b;
    int rc, me = t->aux;
    if (t->pc == 0) { if (--t->left > 0) return; t->pc = 1; g_arrive[t->cnt][me] = (int)g_now; }
    if (t->pc == 1) {
        if (g_naive) { (void)eg_set(&g_eg, t->arg_bits); t->pc = 3; return; }     /* 틀린 판: set 따로 */
        rc = eg_sync(&g_eg, t, t->arg_bits, SY_ALL, 20, &b);
    } else if (t->pc == 3) {
        rc = eg_wait(&g_eg, t, SY_ALL, EG_ALL | EG_CLEAR, 20, &b);                 /* 틀린 판: wait 따로 */
    } else {
        rc = eg_wait_result(&g_eg, t, &b);
    }
    if (rc == EG_BLOCKED) { t->pc = 2; return; }
    if (rc != EG_OK) { g_sync_fail++; t->rc = rc; k_done(t); return; }
    g_leave[t->cnt][me] = (int)g_now;
    if (++t->cnt == 2) { t->rc = EG_OK; k_done(t); return; }
    t->left = t->arg_to; t->pc = 0;
}
static void sync_setup(int naive) {
    setup(NULL, 0);
    g_naive = naive; g_sync_fail = 0;
    memset(g_arrive, -1, sizeof g_arrive); memset(g_leave, -1, sizeof g_leave);
    static const struct { const char *n; int prio, work; ebits_t bit; } S[3] = {
        { "A", 3, 2, SY_A }, { "B", 2, 3, SY_B }, { "C", 1, 5, SY_C } };
    for (int i = 0; i < 3; i++) {
        tcb_t *t = k_add(S[i].n, S[i].prio, 0, sync_task);
        t->arg_bits = S[i].bit; t->left = t->arg_to = S[i].work; t->aux = i;
    }
}

/* ======================= 연습 2 — ISR 의 set 은 데몬 태스크로 미뤄진다 ======================= */
#define DQ_LEN  2
#define EV_DRDY 0x1u
static struct { ebits_t q[DQ_LEN]; int head, n; } g_dq;   /* 타이머 커맨드 큐 (configTIMER_QUEUE_LENGTH) */
static tcb_t *g_daemon;
static int g_isr_fail, g_isr_at[4], g_nisr;
static int eg_set_from_isr(ebits_t bits) {               /* = xTimerPendFunctionCallFromISR(vEventGroupSetBitsCallback) */
    if (g_dq.n == DQ_LEN) return 0;                       /* pdFAIL: 큐가 찼다 */
    g_dq.q[(g_dq.head + g_dq.n++) % DQ_LEN] = bits;
    if (g_daemon->state == TASK_BLOCKED) k_timeout_now(g_daemon);   /* 큐 대기에서 깨움 */
    return 1;
}
static void daemon_task(tcb_t *t) {                       /* 한 걸음에 명령 하나: eg_set */
    if (g_dq.n == 0) { k_block(t, NULL, EG_FOREVER); return; }
    ebits_t b = g_dq.q[g_dq.head]; g_dq.head = (g_dq.head + 1) % DQ_LEN; g_dq.n--;
    (void)H_set(&g_eg, b);
    if (g_dq.n == 0) k_block(t, NULL, EG_FOREVER);       /* 비었으면 바로 다시 큐 대기 */
}
static void busy_task(tcb_t *t) { if (--t->left <= 0) k_done(t); }
static void proc_task(tcb_t *t) {                         /* DRDY 를 기다렸다가 처리 시각 기록 */
    ebits_t b;
    int rc = t->pc == 0 ? eg_wait(&g_eg, t, EV_DRDY, EG_ANY | EG_CLEAR, EG_FOREVER, &b) : eg_wait_result(&g_eg, t, &b);
    if (rc == EG_BLOCKED) { t->pc = 1; return; }
    t->got_at = (int)g_now; t->cnt++; t->pc = 0;
}
static void isr_hook(uint32_t now) {
    for (int i = 0; i < g_nisr; i++) if ((uint32_t)g_isr_at[i] == now && !eg_set_from_isr(EV_DRDY)) g_isr_fail++;
}
static tcb_t *isr_setup(int daemon_prio, int nisr) {
    setup(NULL, 0);
    g_tick_hook = isr_hook;
    memset(&g_dq, 0, sizeof g_dq); g_isr_fail = 0; g_nisr = nisr;
    for (int i = 0; i < nisr; i++) g_isr_at[i] = 2 + i;
    tcb_t *p = k_add("PROC", 3, 0, proc_task);
    tcb_t *bz = k_add("BUSY", 2, 1, busy_task); bz->left = 8;
    g_daemon = k_add("TMR", daemon_prio, 0, daemon_task);
    return p;
}

/* ======================= 연습 3 — 반환값의 비트를 "전부" 처리하라 ======================= */
#define EV_BTN  0x1u
#define EV_UART 0x2u
#define EV_BATT 0x4u
static int g_h[3], g_elseif;
static void multi_consumer(tcb_t *t) {
    ebits_t b;
    int rc = t->pc == 0 ? eg_wait(&g_eg, t, EV_BTN | EV_UART | EV_BATT, EG_ANY | EG_CLEAR, EG_FOREVER, &b)
                        : eg_wait_result(&g_eg, t, &b);
    if (rc == EG_BLOCKED) { t->pc = 1; return; }
    t->pc = 0;
    if (g_elseif) {                               /* 틀린 판: 하나만 처리 — 나머지는 이미 지워졌다 */
        if (b & EV_BTN) g_h[0]++;
        else if (b & EV_UART) g_h[1]++;
        else if (b & EV_BATT) g_h[2]++;
    } else {                                      /* 고친 판: 돌려받은 비트 전부 */
        for (int i = 0; i < 3; i++) if (b & (1u << i)) g_h[i]++;
    }
}

/* ======================= 연습 4 — 틀린 set: 깨우자마자 지우기 ======================= */
static ebits_t eg_set_immediate(evgroup_t *g, ebits_t set) {
    g->bits |= set;
    for (tcb_t *t = g->head, *nx; t; t = nx) {
        nx = t->next;
        if (!eg_cond_met(g->bits, t->ev_want, t->ev_flags & EG_ALL)) continue;
        t->ev_value = g->bits; t->ev_by_set = true;
        if (t->ev_flags & EG_CLEAR) g->bits &= ~t->ev_want;      /* 버그: 뒤 대기자가 볼 비트를 지금 지운다 */
        wl_remove(g, t);
        k_unblock(t);
    }
    return g->bits;
}
static const char *two_waiters(int w2_first, int immediate) {
    ebits_t v;
    setup(NULL, 0);
    tcb_t *a = add_waiter("W1", 1, 0, 0, 0, 0), *b = add_waiter("W2", 2, 0, 0, 0, 0);
    g_eg.bits = 0x2;
    if (w2_first) eg_wait(&g_eg, b, 0x3, EG_ALL | EG_CLEAR, EG_FOREVER, &v);
    eg_wait(&g_eg, a, 0x1, EG_ANY | EG_CLEAR, EG_FOREVER, &v);
    if (!w2_first) eg_wait(&g_eg, b, 0x3, EG_ALL | EG_CLEAR, EG_FOREVER, &v);
    if (immediate) eg_set_immediate(&g_eg, 0x1); else eg_set(&g_eg, 0x1);
    return g_wakes;
}

int main(void) {
    (void)k_block; (void)k_unblock; (void)wl_push_back; (void)wl_remove;      /* 이 파일에서 안 쓰는 */
    (void)task_delay; (void)eg_get; (void)eg_clear; (void)setter_task;      /* 커널 함수: 경고 방지 */

    /* ---- 연습 1 ---- */
    sync_setup(0);
    k_run(80);
    printf("ex1 eg_sync : round1 arrive A=%d B=%d C=%d leave A=%d B=%d C=%d | round2 arrive %d %d %d leave %d %d %d | fail=%d bits=%#x\n",
           g_arrive[0][0], g_arrive[0][1], g_arrive[0][2], g_leave[0][0], g_leave[0][1], g_leave[0][2],
           g_arrive[1][0], g_arrive[1][1], g_arrive[1][2], g_leave[1][0], g_leave[1][1], g_leave[1][2], g_sync_fail, (unsigned)g_eg.bits);
    k_print((int)g_now);
    int ok = k_all_done() && g_sync_fail == 0;
    for (int r = 0; r < 2; r++) {
        int last = 0;
        for (int i = 0; i < 3; i++) if (g_arrive[r][i] > last) last = g_arrive[r][i];
        for (int i = 0; i < 3; i++) ok &= g_leave[r][i] >= last;      /* 아무도 마지막 도착 전에 못 나간다 */
    }
    CHECK(ok && g_eg.bits == 0 && g_list_err == 0 && g_set_err == 0);
    sync_setup(1);
    k_run(80);
    printf("ex1 naive   : set() then wait(ALL|CLEAR) -> timeouts=%d, round1 leave A=%d B=%d C=%d\n",
           g_sync_fail, g_leave[0][0], g_leave[0][1], g_leave[0][2]);
    CHECK(g_sync_fail == 3 && g_leave[0][2] < 0);                   /* C 는 자기 비트가 지워져 영원히 못 모인다 */

    /* ---- 연습 2 ---- */
    tcb_t *p = isr_setup(4, 1);
    k_run(30);
    int lat_hi = p->got_at - 2;
    printf("ex2 ISR set, daemon prio 4: PROC(p3) runs at tick %d (ISR at 2) -> latency %d\n", p->got_at, lat_hi);
    k_print(12);
    p = isr_setup(1, 1);
    k_run(30);
    int lat_lo = p->got_at - 2;
    printf("ex2 ISR set, daemon prio 1: PROC(p3) runs at tick %d (ISR at 2) -> latency %d\n", p->got_at, lat_lo);
    k_print(12);
    CHECK(lat_hi == 1 && lat_lo == 8);                              /* 데몬이 BUSY(p2) 뒤에 밀린다 */
    p = isr_setup(1, 3);
    k_run(30);
    printf("ex2 3 ISRs (ticks 2,3,4), queue len %d, daemon prio 1: fails=%d, PROC woke %d time(s)\n", DQ_LEN, g_isr_fail, p->cnt);
    CHECK(g_isr_fail == 1 && p->cnt == 2);                          /* 2개는 큐에, 3번째는 pdFAIL → 1개 잃음 */

    /* ---- 연습 3 ---- */
    static const ev_t s3[] = { { 3, EV_BTN }, { 3, EV_UART }, { 6, EV_BATT } };
    for (int v = 1; v >= 0; v--) {
        setup(s3, 3);
        g_elseif = v; memset(g_h, 0, sizeof g_h);
        k_add("APP", 2, 5, multi_consumer);       /* tick 5 에야 기다리기 시작 (앞 일 처리 중) */
        k_run(20);
        printf("ex3 %-8s: btn=%d uart=%d batt=%d (sent 1 each)\n", v ? "else-if" : "all-bits", g_h[0], g_h[1], g_h[2]);
        if (v) CHECK(g_h[0] == 1 && g_h[1] == 0 && g_h[2] == 1);    /* UART 프레임 하나를 잃었다 */
        else   CHECK(g_h[0] == 1 && g_h[1] == 1 && g_h[2] == 1 && g_eg.bits == 0);
    }

    /* ---- 연습 4 ---- */
    char r1[32], r2[32], r3[32], r4[32];
    snprintf(r1, sizeof r1, "%s", two_waiters(0, 1));
    snprintf(r2, sizeof r2, "%s", two_waiters(1, 1));
    snprintf(r3, sizeof r3, "%s", two_waiters(0, 0));
    snprintf(r4, sizeof r4, "%s", two_waiters(1, 0));
    printf("ex4 immediate clear: list W1,W2 -> woke [%s]; list W2,W1 -> woke [%s]\n", r1, r2);
    printf("ex4 deferred clear : list W1,W2 -> woke [%s]; list W2,W1 -> woke [%s]\n", r3, r4);
    CHECK(strcmp(r1, "W1") == 0 && strcmp(r2, "W2") == 0);          /* 순서에 따라 한 명씩 잃는다 */
    CHECK(strcmp(r3, "W1 W2") == 0 && strcmp(r4, "W2 W1") == 0 && g_eg.bits == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
