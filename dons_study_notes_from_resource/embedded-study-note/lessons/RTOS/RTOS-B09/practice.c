/* RTOS-B09 추가 연습 — 한 번에 검증
 *
 *   연습 1  take 에 타임아웃 (tick 단위): 대기 리스트 중간에서 빼기, give 와 타임아웃이 같은 tick 이면?
 *   연습 2  대기 순서 FIFO 옵션: 우선순위 순서에서 낮은 태스크가 굶는 시나리오 vs FIFO
 *   연습 3  "가로채기(barging)" give (FreeRTOS 방식: count++ 후 깨우기, 깨어난 태스크가 다시 시도)
 *           vs 직접 넘겨주기(handoff)
 *   연습 4  UART RX ISR → 4칸 링: 세마포어 2개(slots / filled) + ISR 용 non-blocking take, 오버런 카운트
 *
 * 미니 커널은 solution.c 와 같다 + (타임아웃 deadline, 세마포어별 FIFO 옵션) 두 가지만 추가.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 ======================= */
#define MAX_TASKS 8
enum { ST_NOTREL, ST_READY, ST_BLOCKED, ST_SLEEP, ST_DONE };
#define SEM_OK   0
#define SEM_FULL 1

typedef struct task task_t;
typedef struct ksem ksem_t;
typedef void (*step_fn)(task_t *t);

struct task {
    const char *name;
    int prio, state, pc, release, wake_at;
    int deadline;             /* 연습 1: 블록 중 타임아웃 tick (-1 = 영원히) */
    step_fn step;
    unsigned ready_seq, wait_seq;
    ksem_t *waiting_on;
    task_t *wnext;
    int token, timed_out;
    int hold, rounds, served, got_at, futile, ran;
};
struct ksem {
    int count, max;
    bool fifo;                /* 연습 2: true 면 우선순위 무시, 온 순서대로 */
    task_t *wait_head;
    int nwait;
    long full_drops;
};

static task_t   g_task[MAX_TASKS];
static int      g_ntask, g_now, g_order_err;
static task_t  *g_cur;
static bool     g_in_isr;
static unsigned g_seq;
static void   (*g_tick_hook)(int now);

static void k_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = g_now = g_order_err = 0; g_cur = NULL; g_in_isr = false; g_seq = 0; g_tick_hook = NULL;
}
static task_t *k_add(const char *name, int prio, int release, step_fn fn) {
    task_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->release = release; t->step = fn;
    t->state = ST_NOTREL; t->deadline = -1; t->got_at = -1;
    return t;
}
static void k_block(task_t *t, ksem_t *s, int timeout) {
    t->state = ST_BLOCKED; t->waiting_on = s; t->wait_seq = ++g_seq;
    t->deadline = timeout > 0 ? g_now + timeout : -1;
}
static void k_unblock(task_t *t) {
    ksem_t *s = t->waiting_on;
    for (int i = 0; i < g_ntask; i++) {           /* 오라클: 모드에 맞는 1등이었나 */
        task_t *o = &g_task[i];
        if (o == t || o->state != ST_BLOCKED || o->waiting_on != s) continue;
        bool before = s->fifo ? o->wait_seq < t->wait_seq
                              : (o->prio > t->prio || (o->prio == t->prio && o->wait_seq < t->wait_seq));
        g_order_err += before;
    }
    t->state = ST_READY; t->waiting_on = NULL; t->deadline = -1; t->ready_seq = ++g_seq;
}
static void k_sleep(task_t *t, int n) { t->state = ST_SLEEP; t->wake_at = g_now + (n > 0 ? n : 1); }
static void k_done(task_t *t) { t->state = ST_DONE; }
static void wait_remove(ksem_t *s, task_t *t);    /* 연습 1 */
static task_t *k_pick(void) {
    task_t *best = NULL;
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        if (t->state == ST_READY && (!best || t->prio > best->prio ||
            (t->prio == best->prio && t->ready_seq < best->ready_seq))) best = t;
    }
    return best;
}
static void k_tick(void) {
    g_in_isr = true;                              /* ① ISR 이 먼저 → 마감 tick 의 give 는 이긴다 */
    if (g_tick_hook) g_tick_hook(g_now);
    g_in_isr = false;
    for (int i = 0; i < g_ntask; i++) {           /* ② release / sleep / 타임아웃 */
        task_t *t = &g_task[i];
        if ((t->state == ST_NOTREL && t->release <= g_now) || (t->state == ST_SLEEP && t->wake_at <= g_now)) {
            t->state = ST_READY; t->ready_seq = ++g_seq;
        } else if (t->state == ST_BLOCKED && t->deadline >= 0 && t->deadline <= g_now) {
            wait_remove(t->waiting_on, t);
            t->timed_out = 1; t->state = ST_READY; t->waiting_on = NULL; t->deadline = -1; t->ready_seq = ++g_seq;
        }
    }
    task_t *run = k_pick();
    g_cur = run;
    if (run) { run->step(run); run->ran++; }
    g_cur = k_pick();
    g_now++;
}
static bool k_all_done(void) {
    for (int i = 0; i < g_ntask; i++) if (g_task[i].state != ST_DONE) return false;
    return true;
}
static int k_run(int limit) { while (g_now < limit && !k_all_done()) k_tick(); return g_now; }

/* ======================= 본문의 세마포어 (solution.c 와 같음, fifo 옵션만 추가) ======================= */
static bool ksem_init(ksem_t *s, int initial, int max) {
    memset(s, 0, sizeof *s);
    if (max < 1 || initial < 0 || initial > max) return false;
    s->count = initial; s->max = max;
    return true;
}
static void wait_insert(ksem_t *s, task_t *t) {
    task_t **pp = &s->wait_head;
    while (*pp && (s->fifo || (*pp)->prio >= t->prio))   /* 연습 2: fifo 면 무조건 맨 뒤 */
        pp = &(*pp)->wnext;
    t->wnext = *pp; *pp = t; s->nwait++;
}
static task_t *wait_pop(ksem_t *s) {
    task_t *t = s->wait_head;
    if (t) { s->wait_head = t->wnext; t->wnext = NULL; s->nwait--; }
    return t;
}
static bool ksem_take(ksem_t *s, task_t *self) {
    if (g_in_isr) return false;
    if (s->count > 0) { s->count--; return true; }
    wait_insert(s, self); k_block(self, s, 0);
    return false;
}
static int ksem_give(ksem_t *s) {
    task_t *w = wait_pop(s);
    if (w) { w->token = 1; k_unblock(w); return SEM_OK; }
    if (s->count < s->max) { s->count++; return SEM_OK; }
    s->full_drops++;
    return SEM_FULL;
}
static int ksem_give_from_isr(ksem_t *s, bool *higher_woken) {
    task_t *w = s->wait_head;
    int cur = g_cur ? g_cur->prio : 0;
    int rc = ksem_give(s);
    if (w && w->prio > cur) *higher_woken = true;
    return rc;
}

/* ======================= 연습 1 — 타임아웃 ======================= */
static void wait_remove(ksem_t *s, task_t *t) {   /* 리스트 어디에 있든 빼낸다 */
    for (task_t **pp = &s->wait_head; *pp; pp = &(*pp)->wnext)
        if (*pp == t) { *pp = t->wnext; t->wnext = NULL; s->nwait--; return; }
}
/* timeout == 0: 기다리지 않음 (try). > 0: 그 tick 수만큼만 기다림.
 * 블록되면 false — 깨어났을 때 token 이면 성공, timed_out 이면 실패 */
static bool ksem_take_timeout(ksem_t *s, task_t *self, int timeout) {
    if (g_in_isr) return false;
    if (s->count > 0) { s->count--; return true; }
    if (timeout == 0) return false;
    wait_insert(s, self); k_block(self, s, timeout);
    return false;
}

/* ======================= 연습 3 — barging give ======================= */
static int g_futile;
static int ksem_give_barging(ksem_t *s) {         /* FreeRTOS 방식: 값을 올리고, 맨 앞 대기자를 READY 로만 */
    if (s->count >= s->max) { s->full_drops++; return SEM_FULL; }
    s->count++;
    task_t *w = wait_pop(s);
    if (w) k_unblock(w);                          /* token 없음 → 깨어나서 다시 take 해야 한다 */
    return SEM_OK;
}

/* ======================= 연습 4 — ISR 용 non-blocking take ======================= */
static bool ksem_try_take_isr(ksem_t *s) {        /* = xSemaphoreTakeFromISR: 블록 없음 */
    if (s->count > 0) { s->count--; return true; }
    return false;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 연습 1 ---- */
static ksem_t g_s1;
static int g_t1_timeout, g_t1_give_at;
static void t1_task(task_t *t) {
    if (t->pc == 0) {
        if (ksem_take_timeout(&g_s1, t, t->hold)) { t->got_at = g_now; k_done(t); return; }
        if (t->hold == 0) { t->timed_out = 1; k_done(t); return; }   /* try 실패 */
        t->pc = 1; return;
    }
    if (t->token) { t->token = 0; t->got_at = g_now; }
    k_done(t);
}
static void t1_isr(int now) { bool w = false; if (now == g_t1_give_at) ksem_give_from_isr(&g_s1, &w); }
static task_t *t1_run(int timeout, int give_at) {
    k_reset(); ksem_init(&g_s1, 0, 1);
    g_t1_timeout = timeout; g_t1_give_at = give_at; g_tick_hook = t1_isr;
    task_t *t = k_add("T", 2, 0, t1_task); t->hold = timeout;
    k_run(30);
    return t;
}

/* ---- 연습 2 / 3: 토큰 1개를 돌려 쓰는 태스크 ---- */
static ksem_t g_one;
static bool g_barging;
static void loop_task(task_t *t) {                /* for(;;) { take; hold; give; } — 쉬는 시간 없음 */
    switch (t->pc) {
    case 0:
        if (!ksem_take(&g_one, t)) { t->pc = 1; return; }
        break;
    case 1:                                       /* 깨어남 */
        if (!t->token) { t->futile++; g_futile++; t->pc = 0; t->step(t); return; }   /* barging: 다시 시도 */
        t->token = 0;
        break;
    default:                                      /* hold 끝 → give, 바로 다음 take */
        if (g_barging) ksem_give_barging(&g_one); else ksem_give(&g_one);
        if (++t->served >= t->rounds) { k_done(t); return; }
        t->pc = 0; t->step(t);                    /* 같은 step 안에서 바로 다시 take */
        return;
    }
    if (t->got_at < 0) t->got_at = g_now;
    k_sleep(t, t->hold); t->pc = 2;
}
static task_t *loop_add(const char *n, int prio, int rel, int hold, int rounds) {
    task_t *t = k_add(n, prio, rel, loop_task); t->hold = hold; t->rounds = rounds;
    return t;
}

/* ---- 연습 4: UART RX ISR → 4칸 링 → 파서 ---- */
#define RING 4
static ksem_t g_slots, g_filled;
static int g_ring[RING], g_wi, g_ri, g_sent, g_overrun, g_recv, g_last, g_order_bad;
static void rx_isr(int now) {
    if (now < 1 || now > 12) return;              /* tick 1..12 에 바이트 1개씩 도착 */
    int byte = ++g_sent;
    bool w = false;
    if (!ksem_try_take_isr(&g_slots)) { g_overrun++; return; }   /* 빈 칸 없음 → 버림 (오버런) */
    g_ring[g_wi] = byte; g_wi = (g_wi + 1) % RING;
    ksem_give_from_isr(&g_filled, &w);
}
static void busy_task(task_t *t) { if (++t->served >= t->hold) k_done(t); }
static void rx_parser(task_t *t) {
    if (t->pc == 0 && !ksem_take(&g_filled, t)) { t->pc = 1; return; }
    t->token = 0; t->pc = 0;
    int b = g_ring[g_ri]; g_ri = (g_ri + 1) % RING;
    ksem_give(&g_slots);                          /* 칸 반납 */
    if (b <= g_last) g_order_bad++;
    g_last = b; g_recv++;
    if (g_recv + g_overrun >= 12) k_done(t);
}

int main(void) {
    /* ---------- 연습 1: 타임아웃 ---------- */
    task_t *t = t1_run(5, -1);                    /* give 없음 → tick 5 에 타임아웃 */
    printf("ex1 no give      : timed_out=%d got_at=%d nwait=%d\n", t->timed_out, t->got_at, g_s1.nwait);
    CHECK(t->timed_out && t->got_at < 0 && g_s1.nwait == 0 && k_all_done());
    t = t1_run(5, 3);                             /* 마감 전 give */
    CHECK(!t->timed_out && t->got_at == 3);
    t = t1_run(5, 5);                             /* 마감 tick 에 give: ISR 이 먼저 → 성공 */
    printf("ex1 give@deadline: timed_out=%d got_at=%d\n", t->timed_out, t->got_at);
    CHECK(!t->timed_out && t->got_at == 5);
    t = t1_run(0, 1);                             /* timeout 0 = try: 기다리지 않는다 */
    CHECK(t->timed_out && t->got_at < 0 && g_s1.nwait == 0);

    /* 중간 원소 제거: 세 명 대기 중 가운데가 타임아웃 → 다음 give 는 리스트 순서대로 */
    k_reset(); ksem_init(&g_s1, 0, 3);
    task_t *a = k_add("a", 3, 0, t1_task), *b = k_add("b", 2, 0, t1_task), *c = k_add("c", 1, 0, t1_task);
    a->hold = 20; b->hold = 2; c->hold = 20;
    k_run(4);                                     /* a, b, c 블록 → b 가 tick 3 에 타임아웃 */
    CHECK(b->timed_out && g_s1.nwait == 2 && g_s1.wait_head == a && a->wnext == c && c->wnext == NULL);
    ksem_give(&g_s1); ksem_give(&g_s1);
    CHECK(a->token && c->token && g_s1.nwait == 0 && g_order_err == 0);

    /* ---------- 연습 2: 우선순위 순서의 굶주림 vs FIFO ---------- */
    int l_got[2];
    for (int mode = 0; mode < 2; mode++) {
        k_reset(); ksem_init(&g_one, 1, 1); g_one.fifo = mode == 1; g_barging = false;
        loop_add("H1", 3, 0, 2, 6);
        loop_add("H2", 3, 0, 2, 6);
        task_t *l = loop_add("L", 1, 0, 1, 1);
        int end = k_run(100);
        l_got[mode] = l->got_at;
        printf("ex2 %-8s: L first got token at tick %d (H1/H2 6 rounds each, all done at %d)\n",
               mode ? "FIFO" : "priority", l->got_at, end);
        CHECK(k_all_done() && g_order_err == 0);
    }
    CHECK(l_got[0] > 30 && l_got[1] < 12);        /* 우선순위 순: H 들이 거의 다 끝나야 / FIFO: 금방 */

    /* ---------- 연습 3: barging vs handoff ---------- */
    int w_got[2], fut[2];
    for (int mode = 0; mode < 2; mode++) {
        k_reset(); ksem_init(&g_one, 1, 1); g_barging = mode == 1; g_futile = 0;
        loop_add("G", 3, 0, 2, 4);                /* 쥐고, 놓고, 바로 다시 쥔다 */
        task_t *w = loop_add("W", 2, 1, 1, 1);
        int end = k_run(100);
        w_got[mode] = w->got_at; fut[mode] = g_futile;
        printf("ex3 %-8s: W got token at tick %d, futile wakeups=%d, done at %d\n",
               mode ? "barging" : "handoff", w->got_at, g_futile, end);
        CHECK(k_all_done());
    }
    CHECK(fut[0] == 0 && fut[1] >= 3);            /* handoff 는 헛깨움 0 */
    CHECK(w_got[0] < w_got[1]);                   /* barging: G 가 4번 다 끝나야 W 차례 */

    /* ---------- 연습 4: UART RX ISR → 링 4칸 ---------- */
    k_reset(); ksem_init(&g_slots, RING, RING); ksem_init(&g_filled, 0, RING);
    g_wi = g_ri = g_sent = g_overrun = g_recv = g_last = g_order_bad = 0;
    g_tick_hook = rx_isr;
    task_t *busy = k_add("MOTOR", 3, 0, busy_task); busy->hold = 8;   /* tick 0..7 CPU 점유 */
    k_add("PARSE", 2, 0, rx_parser);
    k_run(40);
    printf("ex4 uart ring: sent=%d recv=%d overrun=%d slots=%d filled=%d\n",
           g_sent, g_recv, g_overrun, g_slots.count, g_filled.count);
    CHECK(g_sent == 12 && g_recv + g_overrun == 12 && g_overrun > 0 && g_order_bad == 0);
    CHECK(g_slots.count == RING && g_filled.count == 0 && k_all_done());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
