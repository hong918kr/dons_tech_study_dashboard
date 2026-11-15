/* RTOS-B13 이벤트 그룹: wait-all/any와 clear-on-exit
 *
 *   미니 커널 (하네스 제공): OS 스레드 1개, tick 구동, 완전히 결정적 (RTOS-B09 와 같은 틀, B02 어휘).
 *     매 tick:  ① tick hook (ISR 자리 — FreeRTOS 에서는 타이머 데몬이 대신 set 한다)
 *               ② 출발 시각 / 지연·타임아웃 마감이 된 태스크 → READY
 *               ③ READY 중 우선순위가 가장 높은 태스크 1개가 "한 걸음(step)" 실행
 *                  (같은 우선순위는 먼저 READY 된 순 = FIFO, time slicing 없음)
 *   Part A  이벤트 그룹: 대기 리스트(끝에 붙이기, O(1) 제거), 조건 검사(ALL/ANY), wait(즉시·폴링·블록),
 *           set(조건 맞는 대기자를 "모두" 깨우고 clear-on-exit 는 전부 평가한 "뒤에" 한 번에), clear, 타임아웃
 *   Part B  여러 대기자 + 우선순위, 타임아웃 재검사, 경쟁 조건 2개 재현(버그판 vs 고친판)
 *   Part C  BLE 센서 노드 부팅 (init 3개 → main 이 wait-all), 워치독 건강 감시 (alive 비트 wait-all + clear)
 *
 * 체크는 상태/순서/반환값만 본다 (시간 측정 없음). 틀린 코드도 tick 상한에서 멈춘다 (무한 루프 없음).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st       (solution.c 도 같은 명령)
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
    (void)g; (void)t;                           /* TODO A0 */
}
static void wl_remove(evgroup_t *g, tcb_t *t) {
    (void)g; (void)t;                           /* TODO A0 */
}

/* A1. 조건: ALL 이면 want 비트가 전부 1, ANY 이면 하나라도 1 */
static bool eg_cond_met(ebits_t bits, ebits_t want, bool all) {
    (void)bits; (void)want; (void)all;          /* TODO A1 */
    return false;
}

/* A2. wait: want == 0 이거나 제어 비트를 건드리면 KASSERT + EG_EINVAL.
 *     ① 이미 조건이 맞으면: *out = 지금 값(지우기 전), EG_CLEAR 면 want 를 지우고 EG_OK
 *     ② 아니고 timeout == 0 이면: *out = 지금 값, EG_TIMEOUT (블록 안 함 = 폴링)
 *     ③ 아니면: ev_want/ev_flags 를 TCB 에 적고 대기 리스트 끝에 붙인 뒤 k_block → EG_BLOCKED */
static int eg_wait(evgroup_t *g, tcb_t *self, ebits_t want, int flags, int timeout, ebits_t *out) {
    (void)self; (void)want; (void)flags; (void)timeout;   /* TODO A2: 지금은 "항상 이미 맞음" */
    *out = g->bits;
    return EG_OK;
}

/* A3. set: 비트를 OR 하고, 대기 리스트를 "끝까지" 돌며 조건이 맞는 대기자를 모두 깨운다.
 *     깨운 태스크에는 ev_value = 지금 비트(지우기 전), ev_by_set = true.
 *     EG_CLEAR 대기자의 want 는 모아 두었다가 루프가 끝난 뒤 한 번에 지운다. 반환 = 최종 비트 값 */
static ebits_t eg_set(evgroup_t *g, ebits_t set) {
    g->bits |= set;                             /* TODO A3: 대기자를 깨우지 않는다 */
    return g->bits;
}

/* A4. clear: 지우기 "전" 값을 돌려준다 (= 원자적 fetch-and-clear) */
static ebits_t eg_clear(evgroup_t *g, ebits_t clr) {
    (void)clr;                                  /* TODO A4 */
    return g->bits;
}

/* A5. 커널이 마감 tick 에 부른다: 대기 리스트에서 빼고 "타임아웃으로 깨어남" 표시 */
static void eg_on_timeout(evgroup_t *g, tcb_t *t) {
    (void)g; (void)t;                           /* TODO A5 */
}

/* A6. 블록했던 태스크가 다시 돌 때 결과를 받는다.
 *     set 으로 깨어났으면 *out = ev_value, EG_OK.
 *     타임아웃이면 *out = 지금 값, 그리고 "그 사이 조건이 맞았는지" 다시 본다:
 *     맞으면 EG_CLEAR 처리 후 EG_OK, 아니면 EG_TIMEOUT */
static int eg_wait_result(evgroup_t *g, tcb_t *self, ebits_t *out) {
    (void)g; (void)self;                        /* TODO A6 */
    *out = 0;
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

/* ---- Part B3: RX 이벤트 소비자 — 버그판 vs 고친판 ---- */
#define EV_RX  0x1u
#define HANDLE 4                                  /* 프레임 하나 처리에 4 tick */
static int g_handled;
static void rx_buggy(tcb_t *t) {
    ebits_t b;
    switch (t->pc) {
    case 0:                                       /* ① 확인 (get) — 없으면 기다림 (clear 없이) */
        if (eg_get(&g_eg) & EV_RX) { t->pc = 2; t->left = HANDLE; return; }
        if (eg_wait(&g_eg, t, EV_RX, EG_ANY, EG_FOREVER, &b) == EG_BLOCKED) { t->pc = 1; return; }
        t->pc = 2; t->left = HANDLE; return;
    case 1:                                       /* ② 깨어남 */
        (void)eg_wait_result(&g_eg, t, &b);
        t->pc = 2; t->left = HANDLE; return;
    default:                                      /* ③ 처리 → 끝나고 나서야 clear */
        if (--t->left > 0) return;
        (void)eg_clear(&g_eg, EV_RX);
        g_handled++; t->pc = 0; return;
    }
}
static void rx_fixed(tcb_t *t) {
    ebits_t b;
    switch (t->pc) {
    case 0:                                       /* 확인 + 지우기를 한 번에 (wait with clear-on-exit) */
        if (eg_wait(&g_eg, t, EV_RX, EG_ANY | EG_CLEAR, EG_FOREVER, &b) == EG_BLOCKED) { t->pc = 1; return; }
        t->pc = 2; t->left = HANDLE; return;
    case 1:
        (void)eg_wait_result(&g_eg, t, &b);
        t->pc = 2; t->left = HANDLE; return;
    default:
        if (--t->left > 0) return;
        g_handled++; t->pc = 0; return;
    }
}

/* ---- Part B4: 남의 비트 지우기 — CTRL(IMU 비트 주인) vs LOG(LOGREQ 비트 주인) ---- */
#define EV_IMU    0x1u
#define EV_LOGREQ 0x2u
static int g_ctrl_n, g_log_ok, g_log_lost;
static void ctrl_buggy(tcb_t *t) {
    ebits_t b;
    if (t->pc == 0) { if (eg_wait(&g_eg, t, EV_IMU, EG_ANY, EG_FOREVER, &b) == EG_BLOCKED) { t->pc = 1; return; } }
    else (void)eg_wait_result(&g_eg, t, &b);
    g_ctrl_n++;                                   /* 제어 루프 1회 */
    (void)eg_clear(&g_eg, 0x00FFFFFFu);           /* "남은 플래그 싹 지우고 다음 주기" — 남의 비트까지 */
    t->pc = 0;
}
static void ctrl_fixed(tcb_t *t) {
    ebits_t b;
    if (t->pc == 0) { if (eg_wait(&g_eg, t, EV_IMU, EG_ANY | EG_CLEAR, EG_FOREVER, &b) == EG_BLOCKED) { t->pc = 1; return; } }
    else (void)eg_wait_result(&g_eg, t, &b);
    g_ctrl_n++;                                   /* 자기 비트만, wait 가 원자적으로 지웠다 */
    t->pc = 0;
}
static void log_task(tcb_t *t) {
    ebits_t b;
    int rc;
    if (t->pc == 0) { if (--t->left <= 0) t->pc = 1; return; }   /* 플래시에 쓰는 중 — 아직 기다리지 않는다 */
    rc = t->pc == 1 ? eg_wait(&g_eg, t, EV_LOGREQ, EG_ANY | EG_CLEAR, 10, &b) : eg_wait_result(&g_eg, t, &b);
    if (rc == EG_BLOCKED) { t->pc = 2; return; }
    if (rc == EG_OK) g_log_ok++; else g_log_lost++;
    t->rc = rc; t->got_at = (int)g_now;
    k_done(t);
}

/* ---- Part C1: BLE 센서 노드 부팅 ---- */
#define BOOT_SENSOR  0x1u     /* IMU (SPI) WHO_AM_I 확인 + ODR 설정 */
#define BOOT_RADIO   0x2u     /* BLE 스택 + 광고 시작 */
#define BOOT_STORAGE 0x4u     /* 외부 플래시 파일시스템 마운트 */
#define BOOT_ALL     0x7u
static void init_task(tcb_t *t) {                 /* left tick 동안 초기화 → 성공하면 자기 비트 */
    if (--t->left > 0) return;
    if (!t->aux) { H_set(&g_eg, t->arg_bits); g_last_set = (int)g_now; }
    k_done(t);
}
static tcb_t *add_init(const char *n, int prio, ebits_t bit, int ticks, int fail) {
    tcb_t *t = k_add(n, prio, 0, init_task);
    t->arg_bits = bit; t->left = ticks; t->aux = fail;
    return t;
}
static tcb_t *boot_setup(int radio_fail, int main_rel) {
    setup(NULL, 0);
    tcb_t *m = add_waiter("MAIN", 4, main_rel, BOOT_ALL, EG_ALL, 30);   /* 상태 플래그 → clear 하지 않는다 */
    add_init("SENS", 3, BOOT_SENSOR, 3, 0);
    add_init("RADIO", 2, BOOT_RADIO, 6, radio_fail);
    add_init("STORE", 2, BOOT_STORAGE, 4, 0);
    add_waiter("TELEM", 1, 20, BOOT_ALL, EG_ALL, 0);                    /* 늦게 온 태스크: 폴링 */
    return m;
}

/* ---- Part C2: 워치독 건강 감시 ---- */
#define AL_IMU   0x1u
#define AL_CTRL  0x2u
#define AL_COMM  0x4u
#define AL_ALL   0x7u
#define WD_PERIOD   4         /* 일꾼 주기 */
#define WD_TIMEOUT  8         /* 감시자가 한 번에 기다리는 최대 tick */
#define IWDG_RELOAD 12        /* 하드웨어 IWDG: 12 tick 안에 kick 이 없으면 리셋 */
static int g_kicks, g_misses, g_first_miss, g_iwdg, g_reset_at, g_hang_at;
static ebits_t g_miss_mask;
static void worker_task(tcb_t *t) {
    if (t->aux > 0 && t->cnt >= t->aux) {         /* 멈춤: 안 오는 세마포어를 영원히 기다리는 버그 */
        if (g_hang_at < 0) g_hang_at = (int)g_now;
        k_block(t, NULL, EG_FOREVER);
        return;
    }
    t->cnt++;
    H_set(&g_eg, t->arg_bits);                    /* "나 살아 있음" */
    task_delay(t, WD_PERIOD);
}
static void wdg_task(tcb_t *t) {
    ebits_t b;
    int rc = t->pc == 0 ? eg_wait(&g_eg, t, AL_ALL, EG_ALL | EG_CLEAR, WD_TIMEOUT, &b) : eg_wait_result(&g_eg, t, &b);
    if (rc == EG_BLOCKED) { t->pc = 1; return; }
    t->pc = 0;
    if (rc == EG_OK) { g_kicks++; g_iwdg = IWDG_RELOAD; return; }   /* 모두 살아 있음 → IWDG->KR = 0xAAAA */
    g_misses++;
    if (g_first_miss < 0) { g_first_miss = (int)g_now; g_miss_mask = AL_ALL & ~b; }
}
static void wdg_hook(uint32_t now) {              /* 하드웨어 IWDG 카운트다운 */
    if (g_iwdg > 0 && --g_iwdg == 0 && g_reset_at < 0) g_reset_at = (int)now;
}
static void wdg_setup(int comm_hang_after) {
    setup(NULL, 0);
    g_tick_hook = wdg_hook;
    g_kicks = g_misses = 0; g_first_miss = g_reset_at = g_hang_at = -1; g_miss_mask = 0; g_iwdg = IWDG_RELOAD;
    k_add("WDG", 4, 0, wdg_task);
    tcb_t *w;
    w = k_add("IMU", 3, 0, worker_task);  w->arg_bits = AL_IMU;
    w = k_add("CTRL", 2, 0, worker_task); w->arg_bits = AL_CTRL;
    w = k_add("COMM", 1, 0, worker_task); w->arg_bits = AL_COMM; w->aux = comm_hang_after;
}
static void k_run_until(int limit) { while ((int)g_now < limit) kernel_tick(); }

int main(void) {
    ebits_t v;
    (void)k_block; (void)k_unblock; (void)wl_push_back; (void)wl_remove;   /* starter 에서 안 쓰이는 함수: 경고 방지 */

    /* ---------- Part A: 단위 테스트 ---------- */
    CHECK(eg_cond_met(0x5, 0x5, true) && !eg_cond_met(0x4, 0x5, true) && eg_cond_met(0x4, 0x5, false) && !eg_cond_met(0x2, 0x5, false));
    CHECK(eg_cond_met(0x7, 0x1, true) && eg_cond_met(0x7, 0x1, false) && !eg_cond_met(0x0, 0x1, false));

    setup(NULL, 0);
    tcb_t *a = add_waiter("a", 2, 0, 0, 0, 0);
    g_eg.bits = 0x5;
    CHECK(eg_wait(&g_eg, a, 0x1, EG_ANY | EG_CLEAR, 0, &v) == EG_OK && v == 0x5 && g_eg.bits == 0x4);   /* 값은 지우기 전 */
    CHECK(eg_wait(&g_eg, a, 0x3, EG_ALL, 0, &v) == EG_TIMEOUT && v == 0x4 && a->state != TASK_BLOCKED && g_eg.nwait == 0);
    CHECK(eg_wait(&g_eg, a, 0, EG_ANY, 5, &v) == EG_EINVAL && eg_wait(&g_eg, a, 0x01000000u, EG_ANY, 5, &v) == EG_EINVAL
          && g_assert_fail == 2 && g_eg.nwait == 0);
    CHECK(eg_clear(&g_eg, 0x4) == 0x4 && g_eg.bits == 0 && eg_get(&g_eg) == 0);                      /* 지우기 전 값 */

    setup(NULL, 0);                                /* 여러 대기자 + 한 번의 set */
    tcb_t *w1 = add_waiter("W1", 1, 0, 0, 0, 0), *w2 = add_waiter("W2", 2, 0, 0, 0, 0);
    tcb_t *w3 = add_waiter("W3", 3, 0, 0, 0, 0), *w4 = add_waiter("W4", 2, 0, 0, 0, 0);
    CHECK(eg_wait(&g_eg, w1, 0x1, EG_ANY | EG_CLEAR, EG_FOREVER, &v) == EG_BLOCKED && w1->state == TASK_BLOCKED && w1->wait_obj == &g_eg);
    CHECK(eg_wait(&g_eg, w2, 0x3, EG_ALL | EG_CLEAR, EG_FOREVER, &v) == EG_BLOCKED && eg_wait(&g_eg, w3, 0x2, EG_ANY, EG_FOREVER, &v) == EG_BLOCKED
          && eg_wait(&g_eg, w4, 0x7, EG_ALL, EG_FOREVER, &v) == EG_BLOCKED);
    CHECK(g_eg.nwait == 4 && g_eg.head == w1 && g_eg.tail == w4 && eg_list_ok(&g_eg));             /* 끝에 붙인다 */
    CHECK(H_set(&g_eg, 0x2) == 0x2 && w3->state == TASK_READY && w3->ev_value == 0x2 && g_eg.nwait == 3);   /* W3 만 */
    CHECK(H_set(&g_eg, 0x1) == 0x0 && w1->state == TASK_READY && w2->state == TASK_READY);           /* W1 이 지워도 W2 는 깬다 */
    CHECK(w1->ev_value == 0x3 && w2->ev_value == 0x3 && w1->ev_by_set && w2->ev_by_set);             /* 둘 다 지우기 전 값 */
    CHECK(w4->state == TASK_BLOCKED && g_eg.nwait == 1 && g_eg.head == w4 && g_eg.tail == w4 && eg_list_ok(&g_eg));
    CHECK(strcmp(g_wakes, "W3 W1 W2") == 0 && g_set_err == 0);

    tcb_t *x = add_waiter("X", 2, 0, 0, 0, 0), *y = add_waiter("Y", 2, 0, 0, 0, 0);   /* 가운데 타임아웃 */
    eg_wait(&g_eg, x, 0x10, EG_ANY | EG_CLEAR, 3, &v);
    eg_wait(&g_eg, y, 0x20, EG_ANY, EG_FOREVER, &v);
    k_timeout_now(x);                                                     /* 리스트 W4 X Y → W4 Y */
    CHECK(g_eg.nwait == 2 && g_eg.head == w4 && w4->next == y && y->prev == w4 && g_eg.tail == y && eg_list_ok(&g_eg));
    CHECK(H_set(&g_eg, 0x30) == 0x30 && y->state == TASK_READY && x->ev_by_set == false && g_set_err == 0);
    CHECK(eg_wait_result(&g_eg, x, &v) == EG_OK && v == 0x30 && g_eg.bits == 0x20);   /* 타임아웃 뒤 재검사 → 성공 + clear */
    CHECK(eg_wait_result(&g_eg, y, &v) == EG_OK && v == 0x30);

    /* ---------- Part B1: 여러 대기자, 깨우는 순서 vs 실행 순서 ---------- */
    static const ev_t s1[] = { { 5, 0x2 }, { 7, 0x1 } };
    setup(s1, 2);
    w1 = add_waiter("W1", 1, 0, 0x1, EG_ANY | EG_CLEAR, EG_FOREVER);
    w2 = add_waiter("W2", 2, 1, 0x3, EG_ALL | EG_CLEAR, EG_FOREVER);
    w3 = add_waiter("W3", 3, 2, 0x2, EG_ANY, EG_FOREVER);
    w4 = add_waiter("W4", 2, 3, 0x7, EG_ALL, 10);
    int end = k_run(40);
    printf("Part B1 multi-waiter: wakes=[%s] run=[%s] W1=%#x W2=%#x W3=%#x W4 rc=%d got=%#x bits=%#x (done %d)\n",
           g_wakes, g_runlog, (unsigned)w1->got, (unsigned)w2->got, (unsigned)w3->got, w4->rc, (unsigned)w4->got,
           (unsigned)g_eg.bits, end);
    k_print(end);
    CHECK(strcmp(g_wakes, "W3 W1 W2") == 0 && strcmp(g_runlog, "W3 W2 W1 W4") == 0);
    CHECK(w1->got == 0x3 && w2->got == 0x3 && w3->got == 0x2 && w1->got_at == 8 && w2->got_at == 7);
    CHECK(w4->rc == EG_TIMEOUT && w4->got == 0x0 && w4->got_at == 13 && g_eg.bits == 0);
    CHECK(g_set_err == 0 && g_list_err == 0 && k_all_done());

    /* ---------- Part B2: 타임아웃 ---------- */
    static const ev_t s2a[] = { { 1, 0x1 }, { 2, 0x4 } };
    setup(s2a, 2);
    tcb_t *m = add_waiter("M", 2, 0, 0x7, EG_ALL, 5);
    k_run(20);
    printf("Part B2a timeout      : rc=%d got=%#x at tick %d -> missing %#x\n", m->rc, (unsigned)m->got, m->got_at, (unsigned)(0x7 & ~m->got));
    CHECK(m->rc == EG_TIMEOUT && m->got == 0x5 && m->got_at == 5 && (0x7 & ~m->got) == 0x2 && g_list_err == 0);

    static const ev_t s2b[] = { { 1, 0x1 }, { 2, 0x4 }, { 5, 0x2 } };
    setup(s2b, 3);
    m = add_waiter("M", 2, 0, 0x7, EG_ALL, 5);
    k_run(20);
    printf("Part B2b set@deadline : rc=%d got=%#x at tick %d (hook runs before timeouts)\n", m->rc, (unsigned)m->got, m->got_at);
    CHECK(m->rc == EG_OK && m->got == 0x7 && m->got_at == 5 && m->ev_by_set);

    static const ev_t s2c[] = { { 1, 0x1 } };
    setup(s2c, 1);
    m = add_waiter("M", 1, 0, 0x3, EG_ALL | EG_CLEAR, 4);
    tcb_t *h = k_add("H", 3, 4, setter_task); h->arg_bits = 0x2;
    k_run(20);
    printf("Part B2c late set     : rc=%d got=%#x at tick %d, by_set=%d, bits after=%#x\n", m->rc, (unsigned)m->got, m->got_at, m->ev_by_set, (unsigned)g_eg.bits);
    k_print(6);
    CHECK(m->rc == EG_OK && m->got == 0x3 && m->got_at == 5 && !m->ev_by_set && g_eg.bits == 0 && g_set_err == 0);

    /* ---------- Part B3: 경쟁 1 — "확인 → 처리 → 그제서야 clear" 는 처리 중 온 이벤트를 지운다 ---------- */
    static const ev_t s3[] = { { 2, EV_RX }, { 6, EV_RX }, { 14, EV_RX } };
    setup(s3, 3); g_handled = 0;
    k_add("RXbug", 2, 0, rx_buggy);
    k_run_until(30);
    int bug_handled = g_handled;
    printf("Part B3 lost event: buggy (get, wait, handle, clear) handled %d of 3\n", bug_handled);
    k_print(24);
    setup(s3, 3); g_handled = 0;
    k_add("RXfix", 2, 0, rx_fixed);
    k_run_until(30);
    printf("Part B3 lost event: fixed (wait with clear-on-exit)  handled %d of 3\n", g_handled);
    k_print(24);
    CHECK(bug_handled == 2);                       /* 재현: tick 6 의 이벤트가 tick 6 의 clear 에 지워진다 */
    CHECK(g_handled == 3 && g_list_err == 0 && g_set_err == 0);
    static const ev_t s3m[] = { { 2, EV_RX }, { 6, EV_RX }, { 7, EV_RX } };
    setup(s3m, 3); g_handled = 0;
    k_add("RXfix", 2, 0, rx_fixed);
    k_run_until(30);
    printf("Part B3 merge     : fixed, events at 2,6,7 -> handled %d of 3 (bits do not count)\n", g_handled);
    CHECK(g_handled == 2);                         /* 처리 중 두 번 오면 합쳐진다 — 이벤트 그룹은 개수를 세지 않는다 */

    /* ---------- Part B4: 경쟁 2 — 남의 비트를 지우는 태스크 ---------- */
    static const ev_t s4[] = { { 1, EV_IMU }, { 3, EV_LOGREQ }, { 4, EV_IMU }, { 8, EV_IMU }, { 12, EV_IMU } };
    setup(s4, 5); g_ctrl_n = g_log_ok = g_log_lost = 0;
    k_add("CTRL", 3, 0, ctrl_buggy);
    tcb_t *lg = k_add("LOG", 1, 0, log_task); lg->left = 6;
    k_run_until(30);
    printf("Part B4 wrong-task clear: buggy CTRL -> ctrl=%d log ok=%d lost=%d (LOG rc=%d at %d)\n", g_ctrl_n, g_log_ok, g_log_lost, lg->rc, lg->got_at);
    k_print(24);
    int bug_lost = g_log_lost, bug_ok = g_log_ok;
    setup(s4, 5); g_ctrl_n = g_log_ok = g_log_lost = 0;
    k_add("CTRL", 3, 0, ctrl_fixed);
    lg = k_add("LOG", 1, 0, log_task); lg->left = 6;
    k_run_until(30);
    printf("Part B4 wrong-task clear: fixed CTRL -> ctrl=%d log ok=%d lost=%d (LOG rc=%d at %d)\n", g_ctrl_n, g_log_ok, g_log_lost, lg->rc, lg->got_at);
    CHECK(bug_lost == 1 && bug_ok == 0);           /* 재현: tick 4 의 clear(0xFFFFFF) 가 LOGREQ 를 지웠다 */
    CHECK(g_log_ok == 1 && g_log_lost == 0 && g_ctrl_n == 4 && g_eg.bits == 0);

    /* ---------- Part C1: BLE 센서 노드 부팅 — init 3개가 끝나야 MAIN 시작 ---------- */
    m = boot_setup(0, 0);
    end = k_run(60);
    tcb_t *tel = &g_task[4];
    printf("Part C1 boot: last init bit at tick %d, MAIN rc=%d got=%#x starts tick %d (ran %d steps), TELEM rc=%d got=%#x, bits=%#x\n",
           g_last_set, m->rc, (unsigned)m->got, m->got_at, m->ran, tel->rc, (unsigned)tel->got, (unsigned)g_eg.bits);
    k_print(end);
    CHECK(m->rc == EG_OK && m->got == BOOT_ALL && m->got_at == g_last_set + 1 && m->ran == 2);   /* 기다리는 동안 CPU 0 */
    CHECK(tel->rc == EG_OK && tel->got == BOOT_ALL && g_eg.bits == BOOT_ALL);                     /* 상태 비트는 남는다 */
    m = boot_setup(1, 0);
    k_run(60);
    printf("Part C1 boot (radio fails): MAIN rc=%d got=%#x at tick %d -> missing %#x -> degraded mode\n",
           m->rc, (unsigned)m->got, m->got_at, (unsigned)(BOOT_ALL & ~m->got));
    CHECK(m->rc == EG_TIMEOUT && m->got_at == 30 && (BOOT_ALL & ~m->got) == BOOT_RADIO);
    m = boot_setup(0, 20);
    k_run(60);
    printf("Part C1 boot (MAIN starts late): rc=%d at tick %d, ran %d step(s)\n", m->rc, m->got_at, m->ran);
    CHECK(m->rc == EG_OK && m->got_at == 20 && m->ran == 1 && g_list_err == 0);                  /* 순서 무관 */

    /* ---------- Part C2: 워치독 건강 감시 ---------- */
    wdg_setup(0);
    k_run_until(64);
    printf("Part C2 watchdog healthy: kicks=%d misses=%d reset_at=%d\n", g_kicks, g_misses, g_reset_at);
    CHECK(g_kicks >= 11 && g_misses == 0 && g_reset_at < 0 && g_list_err == 0);
    wdg_setup(5);                                  /* COMM 이 5번 일한 뒤 멈춘다 */
    k_run_until(64);
    printf("Part C2 watchdog hang   : COMM hung at %d, first miss at %d mask=%#x, kicks=%d misses=%d, IWDG reset at %d\n",
           g_hang_at, g_first_miss, (unsigned)g_miss_mask, g_kicks, g_misses, g_reset_at);
    k_print(40);
    CHECK(g_hang_at > 0 && g_first_miss > g_hang_at && g_first_miss <= g_hang_at + WD_TIMEOUT + WD_PERIOD);
    CHECK(g_miss_mask == AL_COMM && g_reset_at > g_first_miss && g_reset_at <= g_hang_at + WD_TIMEOUT + WD_PERIOD + IWDG_RELOAD);
    CHECK(g_set_err == 0 && g_list_err == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
