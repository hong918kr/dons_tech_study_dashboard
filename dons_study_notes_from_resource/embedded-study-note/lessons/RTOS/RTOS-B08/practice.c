/* RTOS-B08 추가 연습 — 한 번에 검증
 *
 * solution.c 의 미니 커널(틱 단위 결정적 모델)을 그대로 복사해 쓰고, 연습 1~4 의 답을 검사한다.
 *   연습 1: 주기 이벤트 E=3 틱, 처리 P=1..8 틱 → 바이너리 vs 카운팅 처리 수 (표 + 공식)
 *   연습 2: 카운팅 세마포어 + 드레인 = 빈 깨어남
 *   연습 3: take 타임아웃으로 "센서 멈춤" 감지 (워치독)
 *   연습 4: ISR 두 개가 바이너리 세마포어 하나를 공유 → 이벤트 종류가 섞인다 → 이벤트 비트로 고친다
 */
#include <stdatomic.h>
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
    if (s->count >= s->max) return false;          /* 꽉 참: 기다리지 않고 즉시 실패 */
    s->count++;
    if (s->waiter >= 0) {
        int w = s->waiter;
        s->waiter = -1;
        task_wake(w, WAKE_GIVE);                   /* 깨어난 태스크가 다시 take 해서 count 를 가져간다 */
        if (g_cur < 0 || g_task[w].prio > g_task[g_cur].prio) *woken = true;
    }
    return true;
}

static void task_block(int self, ksem_t *s, int timeout);

/* ---- Part A: xSemaphoreTake ----
 * count > 0 → -1 하고 TAKE_OK.  비었고 timeout == 0 → TAKE_EMPTY (즉시 실패).
 * 비었고 timeout != 0 → task_block() 으로 막고 TAKE_BLOCKED (깨어나면 태스크가 다시 부른다). */
static take_rc_t ksem_take(ksem_t *s, int self, int timeout) {
    if (s->count > 0) { s->count--; return TAKE_OK; }
    if (timeout == 0) return TAKE_EMPTY;
    task_block(self, s, timeout);
    return TAKE_BLOCKED;
}

/* ---- Part B: 막기와 타임아웃 ----
 * task_block: self 를 BLOCKED 로, s 의 waiter 로 등록, deadline = (timeout == FOREVER ? FOREVER : 지금 틱 + timeout).
 * kernel_tick: (틱 ISR) deadline 이 지금 틱 이하인 막힌 태스크를 세마포어 waiter 에서 빼고 WAKE_TIMEOUT 으로 깨운다. */
static void task_block(int self, ksem_t *s, int timeout) {
    task_t *t = &g_task[self];
    t->st = ST_BLOCKED;
    t->wait_on = s;
    t->deadline = (timeout == FOREVER) ? FOREVER : g_tick + timeout;
    t->wake = WAKE_NONE;
    s->waiter = self;
}
static void kernel_tick(void) {
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        if (t->st == ST_BLOCKED && t->deadline != FOREVER && t->deadline <= g_tick) {
            if (t->wait_on && t->wait_on->waiter == i) t->wait_on->waiter = -1;
            task_wake(i, WAKE_TIMEOUT);
        }
    }
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
typedef struct { unsigned events, give_ok, give_fail, processed, timeouts, max_count, bg_ticks; int first_timeout; } res_t;
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
    if (me->wake == WAKE_TIMEOUT) {                       /* xSemaphoreTake 가 pdFALSE 로 돌아온 경우 */
        if (g_res.timeouts++ == 0) g_res.first_timeout = g_tick;   /* 연습 3: 첫 타임아웃 틱 */
    }
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
    if (!ring_put(&g_rx, b)) { g_overrun++; return; }
    bool woken = false;
    bool ok = ksem_give_from_isr(&g_rx_sem, &woken);
    tl_mark_give(ok);
    if (!ok) g_merged++;
    if (g_use_yield) port_yield_from_isr(woken);
}

/* ---- Part C: 바이트 하나 처리가 끝났을 때 — 다음 바이트를 바로 이어서 처리할까? ----
 * MODE_DRAIN 이면 링에서 하나 더 꺼내 *next 에 넣고 true (세마포어를 다시 기다리지 않는다).
 * MODE_ONE 이거나 링이 비었으면 false (→ 다시 take). */
static bool parser_after_byte(unsigned char *next) {
    return g_mode == MODE_DRAIN && ring_get(&g_rx, next);
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


/* ======================= 연습 1: 처리 시간 sweep ======================= */
/* 공식: 주기 E 로 N 개, 처리 P. P <= E 면 전부. P > E 면 처리 태스크가 쉬지 않고 돌며
 * P 틱마다 "그 사이 온 신호 1개" 만 가져간다 → 1 + ceil((N-1)E / P). */
static unsigned expected_binary(unsigned n, unsigned e, unsigned p) {
    if (p <= e) return n;
    return 1u + ((n - 1u) * e + p - 1u) / p;
}

/* ======================= 연습 4: ISR 두 개, 세마포어 하나 ======================= */
enum { EV_DMA = 1u << 0, EV_UARTERR = 1u << 1 };
static atomic_uint g_evbits;              /* 고친 버전: ISR 은 비트를 OR, 태스크는 통째로 가져간다 */
static unsigned g_last_src;               /* 잘못된 버전: "마지막 원인" 변수 하나 */
static bool g_use_bits;
static const unsigned *g_src; static unsigned g_src_i;
static unsigned g_dma_handled, g_err_handled, g_mux_wakes;
static int g_mt, g_mbg;

static void mux_isr(void) {               /* DMA 완료 ISR 과 UART 에러 ISR 이 같은 세마포어를 give */
    unsigned src = g_src[g_src_i++];
    if (g_use_bits) atomic_fetch_or_explicit(&g_evbits, src, memory_order_relaxed);
    else g_last_src = src;
    bool woken = false;
    tl_mark_give(ksem_give_from_isr(&g_sem, &woken));
    port_yield_from_isr(woken);
}
static void mux_step(int id) {
    if (id == g_mbg) return;
    while (ksem_take(&g_sem, g_mt, FOREVER) == TAKE_OK) {
        unsigned bits = g_use_bits ? atomic_exchange_explicit(&g_evbits, 0u, memory_order_relaxed) : g_last_src;
        g_mux_wakes++;
        if (bits & EV_DMA) g_dma_handled++;
        if (bits & EV_UARTERR) g_err_handled++;
    }
}
static void run_mux(bool bits, const unsigned *src, const int *ev, int nev, int ticks) {
    kernel_reset();
    ksem_init(&g_sem, 1, 0);
    atomic_store(&g_evbits, 0u);
    g_last_src = 0; g_use_bits = bits; g_src = src; g_src_i = 0;
    g_dma_handled = g_err_handled = g_mux_wakes = 0;
    g_mt = task_add("T", 2);
    g_mbg = task_add("bg", 1);
    sim_run(ticks, ev, nev, mux_isr, mux_step);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    printf("[연습 1] E=3, N=12, P sweep\n  P  binary  counting  formula\n");
    int ev[12];
    for (int i = 0; i < 12; i++) ev[i] = 3 * i;
    bool mono = true;
    unsigned prev = 13;
    for (int p = 1; p <= 8; p++) {
        res_t b = run_events(1, p, FOREVER, ev, 12, 120);
        res_t c = run_events(16, p, FOREVER, ev, 12, 120);
        unsigned f = expected_binary(12, 3, (unsigned)p);
        printf("  %d  %6u  %8u  %7u\n", p, b.processed, c.processed, f);
        CHECK(b.processed == f);                        /* 공식과 일치 */
        CHECK(c.processed == 12u && c.give_fail == 0u); /* 카운팅은 다 처리 (늦게라도) */
        CHECK(b.processed + b.give_fail == 12u);        /* 유실 = give 실패 */
        if (b.processed > prev) mono = false;
        prev = b.processed;
    }
    CHECK(mono);                                        /* P 가 길수록 바이너리 처리 수는 줄기만 한다 */

    printf("[연습 2] 카운팅 + 드레인\n");
    static const char ping[] = "PING\n";
    static const int ping_ev[] = { 5, 5, 5, 6, 6 };
    run_uart(16, MODE_DRAIN, true, 1, ping, ping_ev, 5, 20);
    CHECK(g_frames == 1u && g_rx.n == 0u);
    CHECK(g_empty_wakes == 4u);                         /* 5바이트를 한 번에 다 비웠는데 count 4 가 남았다 */
    run_uart(1, MODE_DRAIN, true, 1, ping, ping_ev, 5, 20);
    CHECK(g_frames == 1u && g_empty_wakes <= 1u);       /* 바이너리 + 드레인: 빈 깨어남은 많아야 1번 */

    printf("[연습 3] 타임아웃 워치독\n");
    static const int sensor[] = { 0, 4, 8, 12, 16, 20 };   /* 20 틱 이후 센서가 멈춘다 */
    res_t r = run_events(1, 1, 10, sensor, 6, 40);
    CHECK(r.processed == 6u);
    CHECK(r.first_timeout == 31);                       /* 21 틱에 마지막 처리 끝 → 10 틱 뒤 */
    CHECK(r.timeouts == 1u);                            /* 31 → 다음은 41 (시뮬레이션 밖) */

    printf("[연습 4] ISR 두 개, 바이너리 세마포어 하나\n");
    static const unsigned src[] = { EV_DMA, EV_UARTERR };
    static const int src_ev[] = { 5, 5 };               /* 같은 틱에 두 ISR */
    run_mux(false, src, src_ev, 2, 10);
    print_timeline("ISR 2개 → 바이너리 1개 (5 틱에 DMA, UARTERR)", 10);
    CHECK(g_mux_wakes == 1u && g_dma_handled == 0u && g_err_handled == 1u);   /* DMA 완료가 사라졌다 */
    run_mux(true, src, src_ev, 2, 10);
    CHECK(g_mux_wakes == 1u && g_dma_handled == 1u && g_err_handled == 1u);   /* 비트: 둘 다 처리 */
    static const unsigned src2[] = { EV_DMA, EV_DMA };
    run_mux(true, src2, src_ev, 2, 10);
    CHECK(g_dma_handled == 1u);                         /* 비트도 "몇 번" 은 모른다 → 개수는 카운터로 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
