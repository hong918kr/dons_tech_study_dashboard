// 09_rtos_embedded.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// RTOS & 임베디드 개념 (RTOS & Embedded Systems)  —  Q96~Q102  ⏱️
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 09_rtos_embedded.c -o /tmp/andb_rtos_embedded -lpthread
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 베어메탈/RTOS 펌웨어의 "개념 문제"들을 호스트(macOS)에서 시뮬레이션한다.
//   Q96  watchdog timer   : 정기적으로 pat 하지 않으면 reset 콜백이 발동
//   Q97  cooperative sched: 함수 포인터 태스크 + 주기 기반 라운드로빈 스케줄러
//   Q98  ISR<->main 공유  : 레이스 재현 -> 임계구역(IRQ 마스킹)/_Atomic 로 수정
//   Q99  priority inversion: 우선순위 역전 시연 + 우선순위 상속/천장
//   Q100 interrupt latency: 지연 기여 요소 합산(worst-case) + 임계구역 길이 경계
//   Q101 MMIO volatile    : 메모리맵 레지스터 접근(volatile) 관용구
//   Q102 timer wheel      : malloc 없는 소프트웨어 타이머 휠 (정적 풀)
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// (Q96) Watchdog Timer
// ---------------------------------------------------------------------------
// down-counter. pat(=reload) 안 하면 0 도달 시 리셋 콜백 1회 발동 후 래치(expired).
// ===========================================================================
typedef void (*wdt_reset_cb)(void *ctx);
typedef struct {
    uint32_t     timeout;   // reload 값 (pat 없이 버틸 tick 수)
    uint32_t     counter;   // 남은 tick
    bool         expired;   // 한 번 터지면 래치
    wdt_reset_cb cb;
    void        *ctx;
} wdt_t;

// (제공) 초기화 — counter 를 timeout 으로 장전.
void wdt_init(wdt_t *w, uint32_t timeout, wdt_reset_cb cb, void *ctx) {
    if (!w) return;
    w->timeout = timeout; w->counter = timeout; w->expired = false;
    w->cb = cb; w->ctx = ctx;
}

// Q96a. pat: 카운터를 timeout 으로 재장전. 만료 후엔 무시.
void wdt_pat(wdt_t *w) {
    (void)w;
    // TODO: !expired 일 때만 counter = timeout
}

// Q96b. tick: 카운터 감소. 0 도달 시 콜백 1회 발동 후 expired 래치.
void wdt_tick(wdt_t *w) {
    (void)w;
    // TODO: expired 면 무시; --counter==0(또는 timeout==0)이면 cb 발동 + expired=true
}

// Q96c. 만료되었는가? NULL 은 true(죽은 것).
bool wdt_expired(const wdt_t *w) {
    (void)w;
    // TODO
    return false;   // placeholder
}

// ===========================================================================
// (Q97) Cooperative Round-Robin Scheduler
// ---------------------------------------------------------------------------
// 함수 포인터 태스크 + 주기(period). 매 tick now++; now % period == 0 인 태스크를
// 등록 순서대로 실행(협력형, 선점 없음).
// ===========================================================================
typedef void (*sched_task_fn)(void *ctx);
typedef struct {
    sched_task_fn fn;
    uint32_t      period;
    void         *ctx;
    uint32_t      runs;
} sched_task_t;

#define SCHED_MAX 8
typedef struct {
    sched_task_t tasks[SCHED_MAX];
    size_t       count;
    uint32_t     now;
} scheduler_t;

// (제공) 초기화.
void sched_init(scheduler_t *s) {
    if (!s) return;
    s->count = 0; s->now = 0;
}

// Q97a. 태스크 등록. period==0 / NULL / 테이블 초과면 false.
bool sched_add_task(scheduler_t *s, sched_task_fn fn, uint32_t period, void *ctx) {
    (void)s; (void)fn; (void)period; (void)ctx;
    // TODO: 유효성 검사 후 테이블에 저장, runs=0
    return false;   // placeholder
}

// Q97b. 1 tick 진행: now++ 후 주기 도래 태스크를 등록 순서로 실행.
void sched_run_tick(scheduler_t *s) {
    (void)s;
    // TODO: now++; for each task: if (now % period == 0) { runs++; fn(ctx); }
}

// Q97c. i 번째 태스크 실행 횟수(검증용).
uint32_t sched_task_runs(const scheduler_t *s, size_t i) {
    (void)s; (void)i;
    // TODO
    return 0;   // placeholder
}

// ===========================================================================
// (Q98) ISR <-> main 공유 자원 보호  —  임계구역 & _Atomic
// ---------------------------------------------------------------------------
// RMW 도중 ISR 이 끼어들면 lost update. (A) 인터럽트 마스킹 임계구역, (B) _Atomic.
// ===========================================================================
static volatile uint32_t g_primask = 0;  // 0=enable, 1=disable (mock PRIMASK)

// Q98a. 현재 상태 저장 후 마스킹, 이전 상태 반환. (중첩 안전 관용구)
uint32_t irq_save(void) {
    // TODO: prev=g_primask; g_primask=1; return prev;
    return 0;   // placeholder
}

// Q98b. 저장했던 상태로 복원(무조건 enable 이 아니라 "이전 값"으로).
void irq_restore(uint32_t saved) {
    (void)saved;
    // TODO: g_primask = saved;
}

// (제공) 현재 인터럽트 enable 여부.
bool irq_enabled(void) { return g_primask == 0; }

typedef struct { volatile uint32_t value; } shared_t;
#define ISR_DELTA 100u

// Q98c. ISR 모델: 공유 값에 ISR_DELTA 를 더한다.
void isr_fire(shared_t *r) {
    (void)r;
    // TODO: if (r) r->value += ISR_DELTA;
}

// Q98d. 원자 덧셈 래퍼(멀티스레드 안전).
void atomic_add_u32(_Atomic uint32_t *c, uint32_t v) {
    (void)c; (void)v;
    // TODO: atomic_fetch_add_explicit(c, v, memory_order_relaxed);
}

#define ATOMIC_THREADS 4
#define ATOMIC_PER     250000u
typedef struct { _Atomic uint32_t *c; } atomic_arg_t;
static void *atomic_worker(void *arg) {
    atomic_arg_t *a = (atomic_arg_t *)arg;
    for (uint32_t i = 0; i < ATOMIC_PER; ++i) atomic_add_u32(a->c, 1);
    return NULL;
}

// ===========================================================================
// (Q99) Priority Inversion  —  우선순위 역전 & 상속/천장
// ===========================================================================

// Q99a. 우선순위 상속: 보유자 유효 우선순위 = max(base, 최고 대기자).
int pi_effective_priority(int base, const int *waiters, size_t nwaiters) {
    (void)waiters; (void)nwaiters;
    // TODO: base 와 모든 waiters 중 최댓값
    return base;   // placeholder
}

// Q99b. 우선순위 천장: 자원 사용 태스크들 중 최고 우선순위. NULL/0 -> 0.
int pcp_ceiling(const int *user_prios, size_t n) {
    (void)user_prios; (void)n;
    // TODO
    return 0;   // placeholder
}

// (제공) 결정론적 시뮬레이터: 상속 유무에 따른 H 완료 tick.
#define PI_L_BASE 1
#define PI_M_BASE 2
#define PI_H_BASE 3
#define PI_CS_LEN 3
#define PI_M_WORK 5
#define PI_H_WORK 2
int simulate_h_finish(bool inherit) {
    int cs_left = PI_CS_LEN - 1;
    int m_left  = PI_M_WORK;
    int t       = 2;
    while (cs_left > 0) {
        int l_eff;
        if (inherit) {
            int w[1] = { PI_H_BASE };
            l_eff = pi_effective_priority(PI_L_BASE, w, 1);
        } else {
            l_eff = PI_L_BASE;
        }
        bool run_l = (m_left > 0) ? (l_eff >= PI_M_BASE) : true;
        if (run_l) cs_left--; else m_left--;
        t++;
    }
    t += PI_H_WORK;
    return t;
}

// ===========================================================================
// (Q100) Interrupt Latency  —  인터럽트 지연 시간
// ---------------------------------------------------------------------------
// worst-case = sync + disable_window + higher_isr + context_save + isr_prologue.
// 오버플로는 포화(UINT32_MAX)로 처리해 UB 를 피할 것.
// ===========================================================================
typedef struct {
    uint32_t sync;
    uint32_t disable_window;
    uint32_t higher_isr;
    uint32_t context_save;
    uint32_t isr_prologue;
} irq_latency_t;

// Q100a. 최악 지연 = 기여 요소 합(포화 덧셈). NULL -> 0.
uint32_t irq_latency_wc(const irq_latency_t *m) {
    (void)m;
    // TODO: 다섯 필드를 오버플로-세이프하게 합산
    return 0;   // placeholder
}

// Q100b. 여러 임계구역 중 최장(=disable_window 경계값). NULL -> 0.
uint32_t max_disable_window(const uint32_t *cs, size_t n) {
    (void)cs; (void)n;
    // TODO
    return 0;   // placeholder
}

// Q100c. 최악 지연 + ISR 실행시간 <= deadline ?
bool meets_deadline(uint32_t wc_latency, uint32_t isr_exec, uint32_t deadline) {
    (void)wc_latency; (void)isr_exec; (void)deadline;
    // TODO (오버플로 주의)
    return false;   // placeholder
}

// (제공) 측정 데모용 단조 시계.
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ===========================================================================
// (Q101) Memory-Mapped Register Access  —  MMIO + volatile
// ---------------------------------------------------------------------------
// 주변장치 레지스터는 volatile 로 접근(컴파일러가 제거/캐시/재정렬 못 하게).
// 호스트에선 정적 배열을 "주변장치"로 사용.
// ===========================================================================
uint32_t reg_read(const volatile uint32_t *reg) {
    (void)reg;
    // TODO: return reg ? *reg : 0;
    return 0;   // placeholder
}
void reg_write(volatile uint32_t *reg, uint32_t val) {
    (void)reg; (void)val;
    // TODO: if (reg) *reg = val;
}
void reg_set_bits(volatile uint32_t *reg, uint32_t mask) {
    (void)reg; (void)mask;
    // TODO: *reg |= mask (RMW)
}
void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) {
    (void)reg; (void)mask;
    // TODO: *reg &= ~mask (RMW)
}
// mask 영역을 지우고 (val<<shift)&mask 로 대체. shift>=32 는 UB -> 가드.
void reg_modify_field(volatile uint32_t *reg, uint32_t mask, uint32_t shift, uint32_t val) {
    (void)reg; (void)mask; (void)shift; (void)val;
    // TODO
}
// mask 비트가 모두 셋이 될 때까지(최대 max_iters) 폴링. 타임아웃 -> false.
bool reg_poll_flag(const volatile uint32_t *reg, uint32_t mask, uint32_t max_iters) {
    (void)reg; (void)mask; (void)max_iters;
    // TODO: volatile 재-읽기 루프
    return false;   // placeholder
}

// ===========================================================================
// (Q102) Software Timer Wheel  —  소프트웨어 타이머 휠 (malloc 없이)
// ---------------------------------------------------------------------------
// slot   = (now + delay) % TW_SLOTS
// rounds = (delay - 1) / TW_SLOTS
// tick 마다 현재 슬롯을 훑어 rounds==0 이면 발동, 아니면 rounds--. 노드는 정적 풀.
// ===========================================================================
typedef void (*tw_cb)(void *ctx);
#define TW_SLOTS 8
#define TW_MAX   16
typedef struct {
    bool     active;
    uint32_t rounds;
    tw_cb    cb;
    void    *ctx;
    int      next;    // 슬롯 리스트/free-list 링크 (-1=끝)
} tw_timer_t;
typedef struct {
    tw_timer_t pool[TW_MAX];
    int        slots[TW_SLOTS];
    int        free_head;
    uint32_t   now;
} timer_wheel_t;

// (제공) 초기화 — 풀 전체를 free-list 로 엮고 슬롯을 비운다.
void tw_init(timer_wheel_t *w) {
    if (!w) return;
    for (int i = 0; i < TW_MAX - 1; ++i) { w->pool[i].next = i + 1; w->pool[i].active = false; }
    w->pool[TW_MAX - 1].next = -1;
    w->pool[TW_MAX - 1].active = false;
    w->free_head = 0;
    for (int i = 0; i < TW_SLOTS; ++i) w->slots[i] = -1;
    w->now = 0;
}

// Q102a. 타이머 등록. delay tick 뒤 cb(ctx) 발동. 고갈/NULL/delay==0 -> -1.
//   힌트: free-list 머리에서 노드 하나 떼어 slot/rounds 계산 후 슬롯 리스트 앞에 삽입.
int tw_start(timer_wheel_t *w, uint32_t delay, tw_cb cb, void *ctx) {
    (void)w; (void)delay; (void)cb; (void)ctx;
    // TODO
    return -1;   // placeholder
}

// Q102b. 1 tick 진행: now++ 후 현재 슬롯을 순회, rounds==0 이면 발동+반납, else rounds--.
void tw_tick(timer_wheel_t *w) {
    (void)w;
    // TODO: 슬롯 리스트를 이중 포인터로 순회하며 unlink + free-list 반납 후 cb 호출
}

// ===========================================================================
// 테스트용 콜백/컨텍스트 (제공)
// ===========================================================================
static void wdt_count_cb(void *ctx) { (*(int *)ctx)++; }
static void task_tick_cb(void *ctx) { (*(uint32_t *)ctx)++; }
typedef struct { bool fired; uint32_t fired_at; const uint32_t *now_ref; } tmark_t;
static void tmark_cb(void *ctx) {
    tmark_t *m = (tmark_t *)ctx;
    m->fired = true;
    m->fired_at = *m->now_ref;
}
static volatile uint32_t g_sink;
static void busy_critical_section(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) g_sink += i;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL  (하네스는 solution 과 동일)
// ===========================================================================
int main(void) {
    // -------- (Q96) Watchdog Timer --------
    printf("== (Q96) Watchdog Timer ==\n");
    T("wdt_expired(NULL) -> true (safe)", wdt_expired(NULL) == true);
    int resets = 0;
    wdt_t w;
    wdt_init(&w, 3, wdt_count_cb, &resets);
    T("init -> not expired",             !wdt_expired(&w));
    wdt_tick(&w); wdt_tick(&w);
    T("2 ticks (timeout 3) -> alive",    !wdt_expired(&w) && resets == 0);
    wdt_pat(&w);
    wdt_tick(&w); wdt_tick(&w);
    T("pat then 2 ticks -> still alive", !wdt_expired(&w) && resets == 0);
    wdt_tick(&w);
    T("3rd tick -> expired, reset fired", wdt_expired(&w) && resets == 1);
    wdt_tick(&w); wdt_pat(&w); wdt_tick(&w);
    T("after expiry: ticks/pat ignored (reset once)", resets == 1);
    int r0 = 0; wdt_t w0; wdt_init(&w0, 0, wdt_count_cb, &r0);
    wdt_tick(&w0);
    T("timeout 0 -> fires on first tick", wdt_expired(&w0) && r0 == 1);

    // -------- (Q97) Cooperative Scheduler --------
    printf("== (Q97) Cooperative Round-Robin Scheduler ==\n");
    scheduler_t s;
    sched_init(&s);
    uint32_t c1 = 0, c2 = 0, c3 = 0;
    T("add period=0 -> false",           sched_add_task(&s, task_tick_cb, 0, &c1) == false);
    T("add NULL fn -> false",            sched_add_task(&s, NULL, 1, &c1) == false);
    T("add task p=1 -> true",            sched_add_task(&s, task_tick_cb, 1, &c1));
    T("add task p=2 -> true",            sched_add_task(&s, task_tick_cb, 2, &c2));
    T("add task p=3 -> true",            sched_add_task(&s, task_tick_cb, 3, &c3));
    for (int i = 0; i < 6; ++i) sched_run_tick(&s);
    T("p=1 ran 6x",                      sched_task_runs(&s, 0) == 6 && c1 == 6);
    T("p=2 ran 3x",                      sched_task_runs(&s, 1) == 3 && c2 == 3);
    T("p=3 ran 2x",                      sched_task_runs(&s, 2) == 2 && c3 == 2);
    scheduler_t sf; sched_init(&sf);
    int added = 0;
    while (sched_add_task(&sf, task_tick_cb, 1, &c1)) ++added;
    T("table full at SCHED_MAX (8)",     added == SCHED_MAX);

    // -------- (Q98) ISR<->main shared resource --------
    printf("== (Q98) ISR<->main Shared Resource ==\n");
    T("irq enabled initially",           irq_enabled());
    uint32_t s1 = irq_save();
    T("after save -> disabled",          !irq_enabled());
    uint32_t s2 = irq_save();
    irq_restore(s2);
    T("inner restore -> still disabled", !irq_enabled());
    irq_restore(s1);
    T("outer restore -> enabled again",  irq_enabled());
    shared_t rr = { 0 };
    uint32_t tmp = rr.value;
    isr_fire(&rr);
    rr.value = tmp + 1;
    T("RACE reproduced: ISR update lost (value==1, not 101)", rr.value == 1);
    shared_t rs = { 0 };
    uint32_t sv = irq_save();
    uint32_t t2 = rs.value;
    rs.value = t2 + 1;
    irq_restore(sv);
    isr_fire(&rs);
    T("FIX (critical section): value==101 (no loss)", rs.value == 101);
    _Atomic uint32_t ac = 0;
    atomic_arg_t aarg = { &ac };
    pthread_t th[ATOMIC_THREADS];
    for (int i = 0; i < ATOMIC_THREADS; ++i) pthread_create(&th[i], NULL, atomic_worker, &aarg);
    for (int i = 0; i < ATOMIC_THREADS; ++i) pthread_join(th[i], NULL);
    T("FIX (_Atomic): 4 threads x 250000 -> exact 1000000",
      atomic_load(&ac) == (uint32_t)ATOMIC_THREADS * ATOMIC_PER);

    // -------- (Q99) Priority Inversion --------
    printf("== (Q99) Priority Inversion ==\n");
    int waiters[2] = { PI_H_BASE, PI_M_BASE };
    T("pi_effective_priority(L=1, waiter H=3) -> 3", pi_effective_priority(PI_L_BASE, waiters, 1) == 3);
    T("pi_effective_priority(base higher) -> base",  pi_effective_priority(5, waiters, 2) == 5);
    T("pi_effective_priority(no waiters) -> base",   pi_effective_priority(2, NULL, 0) == 2);
    int users[3] = { PI_L_BASE, PI_H_BASE, PI_M_BASE };
    T("pcp_ceiling({1,3,2}) -> 3",                   pcp_ceiling(users, 3) == 3);
    T("pcp_ceiling(NULL/0) -> 0",                    pcp_ceiling(NULL, 0) == 0);
    int no_inh = simulate_h_finish(false);
    int inh    = simulate_h_finish(true);
    printf("    H finish: no-inheritance=%d, inheritance=%d\n", no_inh, inh);
    T("no inheritance -> H delayed by M (finish 11)", no_inh == 11);
    T("inheritance -> H bounded (finish 6)",          inh == 6);
    T("inheritance strictly faster (bounded inversion)", inh < no_inh);

    // -------- (Q100) Interrupt Latency --------
    printf("== (Q100) Interrupt Latency ==\n");
    irq_latency_t lat = { .sync = 2, .disable_window = 40, .higher_isr = 15,
                          .context_save = 12, .isr_prologue = 6 };
    T("irq_latency_wc sum == 75",        irq_latency_wc(&lat) == 75);
    T("irq_latency_wc(NULL) -> 0",       irq_latency_wc(NULL) == 0);
    irq_latency_t big = { .sync = UINT32_MAX, .disable_window = 10, .higher_isr = 0,
                          .context_save = 0, .isr_prologue = 0 };
    T("irq_latency_wc saturates (no overflow)", irq_latency_wc(&big) == UINT32_MAX);
    uint32_t cs[4] = { 12, 40, 8, 33 };
    T("max_disable_window({12,40,8,33}) -> 40", max_disable_window(cs, 4) == 40);
    T("max_disable_window(NULL) -> 0",   max_disable_window(NULL, 0) == 0);
    T("meets_deadline(75+20 <= 100) -> true",  meets_deadline(75, 20, 100));
    T("meets_deadline(75+30 <= 100) -> false", !meets_deadline(75, 30, 100));
    uint64_t t_start = now_ns();
    busy_critical_section();
    uint64_t t_end = now_ns();
    printf("    measured critical-section length ~= %llu ns (informational)\n",
           (unsigned long long)(t_end - t_start));

    // -------- (Q101) MMIO volatile --------
    printf("== (Q101) Memory-Mapped Register Access (volatile) ==\n");
    static volatile uint32_t PERIPH[2];
    volatile uint32_t *CR = &PERIPH[0];
    volatile uint32_t *SR = &PERIPH[1];
    reg_write(CR, 0);
    T("reg_write/read roundtrip",        (reg_write(CR, 0xABCD0000u), reg_read(CR) == 0xABCD0000u));
    reg_set_bits(CR, 0x1u);
    T("reg_set_bits sets bit0",          (reg_read(CR) & 0x1u) == 0x1u);
    reg_clear_bits(CR, 0x1u);
    T("reg_clear_bits clears bit0",      (reg_read(CR) & 0x1u) == 0u);
    reg_modify_field(CR, 0xF00u, 8, 0xAu);
    T("reg_modify_field field==0xA",     ((reg_read(CR) >> 8) & 0xFu) == 0xAu);
    reg_modify_field(CR, 0xF00u, 8, 0x3u);
    T("reg_modify_field overwrite==0x3", ((reg_read(CR) >> 8) & 0xFu) == 0x3u);
    T("reg_modify_field shift>=32 -> no-op safe", (reg_modify_field(CR, 0xFu, 40, 1), true));
    T("reg_*(NULL) safe",                (reg_write(NULL, 1), reg_set_bits(NULL, 1),
                                          reg_clear_bits(NULL, 1), reg_read(NULL) == 0));
    reg_write(SR, 0);
    T("poll_flag timeout when clear -> false", reg_poll_flag(SR, 0x80u, 100) == false);
    reg_write(SR, 0x80u);
    T("poll_flag success when set -> true",    reg_poll_flag(SR, 0x80u, 100) == true);

    // -------- (Q102) Software Timer Wheel --------
    printf("== (Q102) Software Timer Wheel ==\n");
    timer_wheel_t tw;
    tw_init(&tw);
    T("tw_start(delay 0) -> -1",         tw_start(&tw, 0, tmark_cb, NULL) == -1);
    T("tw_start(NULL cb) -> -1",         tw_start(&tw, 5, NULL, NULL) == -1);
    uint32_t delays[5] = { 1, 3, 8, 9, 16 };
    tmark_t marks[5];
    for (int i = 0; i < 5; ++i) {
        marks[i].fired = false; marks[i].fired_at = 0; marks[i].now_ref = &tw.now;
        T("tw_start ok", tw_start(&tw, delays[i], tmark_cb, &marks[i]) >= 0);
    }
    for (int i = 0; i < 16; ++i) tw_tick(&tw);
    bool all_ok = true;
    for (int i = 0; i < 5; ++i)
        if (!marks[i].fired || marks[i].fired_at != delays[i]) all_ok = false;
    T("all timers fired at exact delay tick (1,3,8,9,16)", all_ok);
    timer_wheel_t tf; tw_init(&tf);
    int okc = 0;
    while (tw_start(&tf, 5, tmark_cb, NULL) >= 0) ++okc;
    T("pool exhaustion at TW_MAX (16)", okc == TW_MAX);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
