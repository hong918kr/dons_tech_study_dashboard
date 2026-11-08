/* RTOS-B08 바이너리 세마포어: ISR → 태스크 신호
 *
 * 호스트 모델: 단일 OS 스레드에서 도는 "틱 단위 결정적 미니 커널" (forced schedule).
 *   - 태스크 = 한 틱에 한 걸음씩 실행되는 상태 기계 (step 함수). 항상 우선순위가 가장 높은 READY 태스크가 돈다.
 *   - ISR 대용 = 하네스가 각본(ev[] 틱 목록)대로 부르는 함수. 같은 틱이 여러 번이면 연속 인터럽트.
 *   - 한 틱의 순서: ① 틱 ISR(타임아웃 만료) → ② 스케줄러 선택 → ③ 장치 ISR(들) → ④ 실행 중 태스크 한 걸음.
 *     ③ 에서 ISR 이 port_yield_from_isr(true) 를 부르면 ④ 전에 다시 고른다 (= 깨운 태스크가 이 틱에 바로 돈다).
 * 실제 FreeRTOS 에서 같은 숫자가 나오는지는 demo_freertos_isr_sem.c 가 확인한다.
 *
 * 대응: ksem_give_from_isr = xSemaphoreGiveFromISR, ksem_take = xSemaphoreTake,
 *       port_yield_from_isr = portYIELD_FROM_ISR, kernel_tick = 틱 ISR 의 타임아웃 처리.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (주어진 코드 + TODO) ======================= */
enum { MAX_TASKS = 4, FOREVER = -1, TL = 40 };            /* TL = 타임라인에 기록할 틱 수 */
typedef enum { ST_READY, ST_BLOCKED } tstate_t;
typedef enum { WAKE_NONE, WAKE_GIVE, WAKE_TIMEOUT } wake_t;
typedef enum { TAKE_OK, TAKE_EMPTY, TAKE_BLOCKED } take_rc_t;

typedef struct ksem {
    unsigned count, max;      /* max == 1 → 바이너리, max > 1 → 카운팅 */
    int waiter;               /* 이 세마포어에서 막혀 있는 태스크 1개 (-1 = 없음). 대기 리스트는 RTOS-B09 */
} ksem_t;

typedef struct {
    const char *name;
    int prio;                 /* 클수록 높다 (FreeRTOS 와 같은 방향) */
    tstate_t st;
    int deadline;             /* 막힌 태스크가 타임아웃으로 깨어날 틱 (FOREVER = 없음) */
    ksem_t *wait_on;          /* 기다리는 세마포어 (NULL = 없음) */
    wake_t wake;              /* 마지막으로 깨어난 이유 */
} task_t;

static task_t g_task[MAX_TASKS];
static int g_ntask;
static int g_cur = -1;        /* 지금 CPU 를 가진 태스크. ISR 입장에서는 "끼어든 대상" (-1 = idle) */
static int g_tick;
static bool g_resched;        /* ISR 가 portYIELD_FROM_ISR(pdTRUE) 로 즉시 재스케줄을 요청했다 */

static void kernel_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = 0; g_cur = -1; g_tick = 0; g_resched = false;
}
static int task_add(const char *name, int prio) {
    task_t *t = &g_task[g_ntask];
    t->name = name; t->prio = prio; t->st = ST_READY;
    t->deadline = FOREVER; t->wait_on = NULL; t->wake = WAKE_NONE;
    return g_ntask++;
}
static int pick_next(void) {                 /* 가장 높은 우선순위의 READY 태스크 (같으면 먼저 만든 것) */
    int best = -1;
    for (int i = 0; i < g_ntask; i++)
        if (g_task[i].st == ST_READY && (best < 0 || g_task[i].prio > g_task[best].prio)) best = i;
    return best;
}
static void task_wake(int id, wake_t why) {  /* BLOCKED → READY */
    g_task[id].st = ST_READY;
    g_task[id].wait_on = NULL;
    g_task[id].deadline = FOREVER;
    g_task[id].wake = why;
}
static void port_yield_from_isr(bool woken) { if (woken) g_resched = true; }

/* xSemaphoreCreateBinary() == ksem_init(s, 1, 0)  (처음엔 비어 있다)
 * xSemaphoreCreateCounting(max, init) == ksem_init(s, max, init)  — 잘못된 인자면 생성 실패 */
static bool ksem_init(ksem_t *s, unsigned max, unsigned initial) {
    if (max == 0 || initial > max) return false;
    s->count = initial; s->max = max; s->waiter = -1;
    return true;
}

/* ---- Part A: xSemaphoreGiveFromISR — 절대 막히지 않는다 ----
 * count 가 max 미만이면 +1 하고 true (pdTRUE). 이미 max 면 아무것도 바꾸지 않고 false (errQUEUE_FULL)
 *   → 바이너리에서는 "이미 신호가 있다" = 이번 이벤트는 앞 이벤트와 합쳐진다(유실).
 * 기다리는 태스크가 있으면 READY 로 깨우고(WAKE_GIVE), 그 태스크가 끼어든 태스크(g_cur)보다 우선순위가
 * 높으면(g_cur == -1 이면 항상) *woken = true. *woken 을 false 로 되돌리지는 않는다(호출자가 초기화). */
static bool ksem_give_from_isr(ksem_t *s, bool *woken) {
    /* TODO */
    (void)s; (void)woken;
    return false;
}

static void task_block(int self, ksem_t *s, int timeout);

/* ---- Part A: xSemaphoreTake ----
 * count > 0 → -1 하고 TAKE_OK.  비었고 timeout == 0 → TAKE_EMPTY (즉시 실패).
 * 비었고 timeout != 0 → task_block() 으로 막고 TAKE_BLOCKED (깨어나면 태스크가 다시 부른다). */
static take_rc_t ksem_take(ksem_t *s, int self, int timeout) {
    /* TODO */
    (void)s; (void)self; (void)timeout;
    (void)task_block;                              /* (TODO 를 채우면 지운다: -Wunused-function 방지) */
    return TAKE_EMPTY;
}

/* ---- Part B: 막기와 타임아웃 ----
 * task_block: self 를 BLOCKED 로, s 의 waiter 로 등록, deadline = (timeout == FOREVER ? FOREVER : 지금 틱 + timeout).
 * kernel_tick: (틱 ISR) deadline 이 지금 틱 이하인 막힌 태스크를 세마포어 waiter 에서 빼고 WAKE_TIMEOUT 으로 깨운다. */
static void task_block(int self, ksem_t *s, int timeout) {
    /* TODO */
    (void)self; (void)s; (void)timeout;
}
static void kernel_tick(void) {
    /* TODO */
    (void)task_wake;                               /* (TODO 를 채우면 지운다: -Wunused-function 방지) */
}

/* ---- 시뮬레이터 루프 (주어진 코드) ---- */
typedef void (*isr_fn)(void);
typedef void (*step_fn)(int id);
static char g_tl_irq[TL + 1], g_tl_give[TL + 1], g_tl_task[MAX_TASKS][TL + 1];   /* 타임라인: 틱마다 한 글자 */
static void tl_mark_give(bool ok) { if (g_tick < TL) g_tl_give[g_tick] = ok ? 'o' : 'x'; }   /* 그 틱 마지막 give 결과 */

static void sim_run(int ticks, const int *ev, int nev, isr_fn isr, step_fn step) {
    int ei = 0;
    memset(g_tl_irq, 0, sizeof g_tl_irq);
    memset(g_tl_give, '.', TL); g_tl_give[TL] = '\0';
    memset(g_tl_task, 0, sizeof g_tl_task);
    for (g_tick = 0; g_tick < ticks; g_tick++) {
        kernel_tick();                                       /* ① 틱 ISR */
        g_cur = pick_next();                                 /* ② 스케줄러 */
        int nirq = 0;
        while (ei < nev && ev[ei] < g_tick) ei++;            /* (지난 틱 목록은 건너뛴다) */
        while (ei < nev && ev[ei] == g_tick) {               /* ③ 장치 ISR */
            g_resched = false;
            isr();
            ei++; nirq++;
            if (g_resched) g_cur = pick_next();              /*    portYIELD_FROM_ISR → 즉시 전환 */
        }
        bool ran[MAX_TASKS] = { false };                     /* ④ 이번 틱의 CPU */
        while (g_cur >= 0) {
            ran[g_cur] = true;
            step(g_cur);
            if (g_task[g_cur].st == ST_READY) break;         /*    걸음 도중 막히면 다음 태스크가 이어 받는다 */
            g_cur = pick_next();
        }
        if (g_tick < TL) {
            g_tl_irq[g_tick] = nirq ? (char)('0' + (nirq > 9 ? 9 : nirq)) : '.';
            for (int i = 0; i < g_ntask; i++)
                g_tl_task[i][g_tick] = g_task[i].st == ST_BLOCKED ? 'B' : ran[i] ? 'R' : 'r';
        }
    }
}
static void print_timeline(const char *title, int ticks) {
    if (ticks > TL) ticks = TL;
    printf("  -- %s\n  tick : ", title);
    for (int t = 0; t < ticks; t++) printf("%d ", t % 10);
    printf("\n  IRQ  : ");
    for (int t = 0; t < ticks; t++) printf("%c ", g_tl_irq[t]);
    printf("\n  give : ");
    for (int t = 0; t < ticks; t++) printf("%c ", g_tl_give[t]);
    for (int i = 0; i < g_ntask; i++) {
        printf("\n  %-5s: ", g_task[i].name);
        for (int t = 0; t < ticks; t++) printf("%c ", g_tl_task[i][t]);
    }
    printf("\n");
}

/* ======================= Part B 시나리오: 이벤트 ISR → 처리 태스크 ======================= */
typedef struct { unsigned events, give_ok, give_fail, processed, timeouts, max_count, bg_ticks; } res_t;
static ksem_t g_sem;
static res_t g_res;
static int g_h, g_bg;                     /* 처리 태스크(prio 2), 백그라운드 태스크(prio 1, 항상 READY) */
static int g_proc, g_timeout;             /* 이벤트 1개 처리 틱 수, 처리 태스크의 take 타임아웃 */
static struct { bool working; int t0; } g_hs;

static void evt_isr(void) {               /* 예: DMA 전송 완료 ISR, 타이머 비교 일치 ISR */
    bool woken = false;                   /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
    g_res.events++;
    bool ok = ksem_give_from_isr(&g_sem, &woken);
    tl_mark_give(ok);
    if (ok) g_res.give_ok++;
    else g_res.give_fail++;               /* errQUEUE_FULL: 이 이벤트는 앞의 신호와 합쳐졌다 */
    if (g_sem.count > g_res.max_count) g_res.max_count = g_sem.count;
    port_yield_from_isr(woken);           /* portYIELD_FROM_ISR(xHigherPriorityTaskWoken); */
}
static void evt_step(int id) {
    if (id == g_bg) { g_res.bg_ticks++; return; }
    if (g_hs.working) {                                   /* 처리 중: 세마포어를 보지 않는다 */
        if (g_tick - g_hs.t0 < g_proc) return;
        g_res.processed++;
        g_hs.working = false;
    }
    task_t *me = &g_task[g_h];
    if (me->wake == WAKE_TIMEOUT) g_res.timeouts++;      /* xSemaphoreTake 가 pdFALSE 로 돌아온 경우 */
    me->wake = WAKE_NONE;
    if (ksem_take(&g_sem, g_h, g_timeout) == TAKE_OK) { g_hs.working = true; g_hs.t0 = g_tick; }
}
static res_t run_events(unsigned sem_max, int proc, int timeout, const int *ev, int nev, int ticks) {
    kernel_reset();
    memset(&g_res, 0, sizeof g_res);
    memset(&g_hs, 0, sizeof g_hs);
    ksem_init(&g_sem, sem_max, 0);
    g_h = task_add("H", 2);
    g_bg = task_add("bg", 1);
    g_proc = proc; g_timeout = timeout;
    sim_run(ticks, ev, nev, evt_isr, evt_step);
    return g_res;
}

/* ======================= Part C 시나리오: UART RX ISR → 파서 태스크 ======================= */
#define RING_CAP 8
typedef struct { unsigned char buf[RING_CAP]; unsigned head, tail, n; } ring_t;
static bool ring_put(ring_t *r, unsigned char b) {
    if (r->n == RING_CAP) return false;
    r->buf[r->head] = b; r->head = (r->head + 1) % RING_CAP; r->n++;
    return true;
}
static bool ring_get(ring_t *r, unsigned char *b) {
    if (r->n == 0) return false;
    *b = r->buf[r->tail]; r->tail = (r->tail + 1) % RING_CAP; r->n--;
    return true;
}
enum { MODE_ONE, MODE_DRAIN };            /* take 1번에 1바이트 / take 1번에 링이 빌 때까지 */
static ring_t g_rx;                       /* ISR → 파서 바이트 링 (SPSC) */
static ksem_t g_rx_sem;
static bool g_use_yield;
static int g_mode, g_p, g_pbg, g_pproc;   /* 파서 태스크 id, 백그라운드 id, 바이트당 처리 틱 */
static unsigned g_overrun, g_merged, g_empty_wakes, g_frames;
static int g_first_take = -1;             /* 파서가 처음 take 에 성공한 틱 */
static char g_line[32], g_last[32];
static unsigned g_linelen;
static const char *g_wire; static unsigned g_wire_i;   /* "선로" 에서 들어오는 바이트들 */
static struct { bool working; int t0; unsigned char byte; } g_ps;

static unsigned char uart_read_dr(void) { return (unsigned char)g_wire[g_wire_i++]; }   /* USART->DR 읽기 */
static void parse_byte(unsigned char b) {
    if (b == '\n') { g_line[g_linelen] = '\0'; memcpy(g_last, g_line, sizeof g_last); g_frames++; g_linelen = 0; }
    else if (g_linelen + 1 < sizeof g_line) g_line[g_linelen++] = (char)b;
}

/* ---- Part C: UART RX ISR ----
 * DR 에서 바이트를 읽어 g_rx 에 넣는다. 꽉 찼으면 g_overrun++ 하고 끝 (기다리지 않는다, give 도 안 한다).
 * 넣었으면 g_rx_sem 을 give (false 면 g_merged++ — 이미 신호가 있다는 뜻), g_use_yield 면 yield 요청. */
static void uart_rx_isr(void) {
    unsigned char b = uart_read_dr();
    /* TODO (give 결과는 tl_mark_give(ok) 로 타임라인에도 남긴다) */
    (void)b;
    (void)ring_put;                                /* (TODO 를 채우면 지운다: -Wunused-function 방지) */
}

/* ---- Part C: 바이트 하나 처리가 끝났을 때 — 다음 바이트를 바로 이어서 처리할까? ----
 * MODE_DRAIN 이면 링에서 하나 더 꺼내 *next 에 넣고 true (세마포어를 다시 기다리지 않는다).
 * MODE_ONE 이거나 링이 비었으면 false (→ 다시 take). */
static bool parser_after_byte(unsigned char *next) {
    /* TODO */
    (void)next;
    return false;
}

static void parser_step(int id) {
    if (id == g_pbg) return;
    if (g_ps.working) {
        if (g_tick - g_ps.t0 < g_pproc) return;
        parse_byte(g_ps.byte);
        if (parser_after_byte(&g_ps.byte)) { g_ps.t0 = g_tick; return; }
        g_ps.working = false;
    }
    for (;;) {
        if (ksem_take(&g_rx_sem, g_p, FOREVER) != TAKE_OK) return;   /* 막혔다 */
        if (g_first_take < 0) g_first_take = g_tick;
        if (ring_get(&g_rx, &g_ps.byte)) { g_ps.working = true; g_ps.t0 = g_tick; return; }
        g_empty_wakes++;                                              /* 깨어났는데 링이 비어 있다 */
    }
}
static void run_uart(unsigned sem_max, int mode, bool yield, int pproc, const char *wire,
                     const int *ev, int nev, int ticks) {
    kernel_reset();
    memset(&g_rx, 0, sizeof g_rx);
    memset(&g_ps, 0, sizeof g_ps);
    ksem_init(&g_rx_sem, sem_max, 0);
    g_mode = mode; g_use_yield = yield; g_pproc = pproc;
    g_overrun = g_merged = g_empty_wakes = g_frames = 0;
    g_first_take = -1; g_linelen = 0; g_last[0] = '\0';
    g_wire = wire; g_wire_i = 0;
    g_p = task_add("P", 2);
    g_pbg = task_add("bg", 1);
    sim_run(ticks, ev, nev, uart_rx_isr, parser_step);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    ksem_t s;
    bool w;

    printf("[Part A] give/take 의미\n");
    CHECK(!ksem_init(&s, 0, 0));                       /* max 0 은 생성 실패 */
    CHECK(!ksem_init(&s, 2, 3));                       /* initial > max 도 실패 */
    CHECK(ksem_init(&s, 1, 0) && s.count == 0u);       /* 바이너리: 처음엔 비어 있다 */
    kernel_reset();
    int a = task_add("A", 2);
    CHECK(ksem_take(&s, a, 0) == TAKE_EMPTY);          /* 비었고 timeout 0 → 즉시 실패 */
    w = false;
    CHECK(ksem_give_from_isr(&s, &w) && s.count == 1u && !w);   /* 기다리는 태스크 없음 → woken 그대로 */
    CHECK(!ksem_give_from_isr(&s, &w) && s.count == 1u);        /* 두 번째 give: 합쳐진다 (errQUEUE_FULL) */
    CHECK(ksem_take(&s, a, 0) == TAKE_OK && s.count == 0u);
    CHECK(ksem_take(&s, a, 0) == TAKE_EMPTY);          /* give 2번, take 성공은 1번 */
    ksem_init(&s, 3, 0);                               /* 카운팅 max 3 */
    bool r1 = ksem_give_from_isr(&s, &w), r2 = ksem_give_from_isr(&s, &w);
    bool r3 = ksem_give_from_isr(&s, &w), r4 = ksem_give_from_isr(&s, &w);
    CHECK(r1 && r2 && r3 && !r4 && s.count == 3u);     /* max 에서 포화 */
    int ok = 0;
    while (ksem_take(&s, a, 0) == TAKE_OK) ok++;
    CHECK(ok == 3);
    /* woken: 기다리던 태스크를 깨우고, 끼어든 태스크보다 높을 때만 true */
    kernel_reset();
    int hi = task_add("H", 2), lo = task_add("L", 1), top = task_add("T", 3);
    ksem_init(&s, 1, 0);
    g_task[hi].st = ST_BLOCKED; g_task[hi].wait_on = &s; s.waiter = hi;   /* H 가 s 에서 기다리는 중 */
    g_cur = lo; w = false;
    CHECK(ksem_give_from_isr(&s, &w) && w);                       /* L 실행 중 ISR → H 를 깨움 → 전환 필요 */
    CHECK(g_task[hi].st == ST_READY && g_task[hi].wake == WAKE_GIVE && s.waiter == -1 && s.count == 1u);
    CHECK(!ksem_give_from_isr(&s, &w) && w);                      /* 실패한 give 가 woken 을 false 로 안 만든다 */
    s.count = 0; g_task[hi].st = ST_BLOCKED; s.waiter = hi;
    g_cur = top; w = false;
    CHECK(ksem_give_from_isr(&s, &w) && !w && g_task[hi].st == ST_READY);   /* 더 높은 T 가 돌던 중 → 전환 불필요 */
    s.count = 0; g_task[hi].st = ST_BLOCKED; s.waiter = hi;
    g_cur = -1; w = false;
    CHECK(ksem_give_from_isr(&s, &w) && w);                       /* idle 중 ISR → 항상 전환 */

    printf("[Part B] 막기·타임아웃, 그리고 이벤트가 처리보다 빠를 때\n");
    kernel_reset();
    a = task_add("A", 2);
    ksem_init(&s, 1, 0);
    g_tick = 10;
    CHECK(ksem_take(&s, a, 5) == TAKE_BLOCKED && g_task[a].st == ST_BLOCKED && s.waiter == a);
    CHECK(g_task[a].deadline == 15);
    g_tick = 14; kernel_tick();
    CHECK(g_task[a].st == ST_BLOCKED);                            /* 아직 */
    g_tick = 15; kernel_tick();
    CHECK(g_task[a].st == ST_READY && g_task[a].wake == WAKE_TIMEOUT && s.waiter == -1);
    CHECK(ksem_take(&s, a, FOREVER) == TAKE_BLOCKED && g_task[a].deadline == FOREVER);
    g_tick = 1000; kernel_tick();
    CHECK(g_task[a].st == ST_BLOCKED);                            /* 무한 대기는 타임아웃이 없다 */

    static const int slow[] = { 2, 6, 10, 14, 18 };               /* 4 틱마다, 처리 3 틱 → 여유 있음 */
    res_t r = run_events(1, 3, FOREVER, slow, 5, 30);
    CHECK(r.events == 5u && r.processed == 5u && r.give_fail == 0u);

    static const int burst[] = { 10, 11, 12, 13, 14 };            /* 1 틱마다 5개, 처리 3 틱 */
    r = run_events(1, 3, FOREVER, burst, 5, 30);
    print_timeline("binary, burst 10..14, PROC=3", 30);
    CHECK(r.processed == 3u && r.give_ok == 3u && r.give_fail == 2u);   /* 2개 합쳐짐(유실) */
    CHECK(r.events == r.processed + r.give_fail);                 /* 유실 = give 실패 수 */
    CHECK(g_tl_task[g_h][9] == 'B' && g_tl_task[g_h][10] == 'R'); /* ISR 이 깨운 그 틱에 바로 실행 */
    r = run_events(10, 3, FOREVER, burst, 5, 30);
    print_timeline("counting(10), burst 10..14, PROC=3", 30);
    CHECK(r.processed == 5u && r.give_fail == 0u && r.max_count == 3u);
    r = run_events(2, 3, FOREVER, burst, 5, 30);
    CHECK(r.processed == 4u && r.give_fail == 1u && r.max_count == 2u);  /* 카운팅도 max 에서 포화 */

    r = run_events(1, 3, 5, NULL, 0, 21);                         /* 이벤트 없음, take 타임아웃 5 */
    CHECK(r.timeouts == 4u && r.processed == 0u);                 /* 0→5→10→15→20 에서 타임아웃 */
    static const int late[] = { 3 };
    r = run_events(1, 3, 5, late, 1, 8);                          /* 3 틱에 이벤트: 타임아웃(5) 전에 깨어남 */
    CHECK(r.timeouts == 0u && r.processed == 1u);                 /* 낡은 deadline 이 남아 있으면 안 된다 */

    printf("[Part C] UART RX ISR → 파서 태스크\n");
    static const char ping[] = "PING\n";
    static const int ping_ev[] = { 5, 5, 5, 6, 6 };               /* 5 틱에 3바이트, 6 틱에 2바이트 */
    run_uart(1, MODE_DRAIN, true, 1, ping, ping_ev, 5, 20);
    print_timeline("binary + drain, \"PING\\n\"", 14);
    CHECK(g_frames == 1u && strcmp(g_last, "PING") == 0);
    CHECK(g_rx.n == 0u && g_overrun == 0u && g_merged == 3u);     /* give 3번은 합쳐졌지만 바이트는 링에 있다 */
    CHECK(g_empty_wakes == 1u);                                   /* 드레인 중 들어온 신호 → 빈 깨어남 1번 */
    run_uart(1, MODE_ONE, true, 1, ping, ping_ev, 5, 20);
    CHECK(g_frames == 0u && g_rx.n == 3u);                        /* take 당 1바이트 + 바이너리 → 3바이트가 링에 갇힘 */
    run_uart(16, MODE_ONE, true, 1, ping, ping_ev, 5, 20);
    CHECK(g_frames == 1u && g_rx.n == 0u && g_merged == 0u && g_empty_wakes == 0u);   /* 카운팅 + 1바이트씩 */

    static const char twelve[] = "ABCDEFGHIJKL";
    static const int twelve_ev[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    run_uart(1, MODE_DRAIN, true, 1, twelve, twelve_ev, 12, 30);
    CHECK(g_overrun == 4u && g_wire_i == 12u);                    /* 링 8칸: 4바이트는 버림, ISR 은 안 막힘 */

    static const char one[] = "X";
    static const int one_ev[] = { 5 };
    run_uart(1, MODE_DRAIN, true, 1, one, one_ev, 1, 10);
    CHECK(g_first_take == 5);                                     /* yield: IRQ 틱에 바로 */
    run_uart(1, MODE_DRAIN, false, 1, one, one_ev, 1, 10);
    print_timeline("binary, no portYIELD_FROM_ISR", 10);
    CHECK(g_first_take == 6);                                     /* yield 없음: 다음 틱 ISR 까지 bg 가 계속 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
