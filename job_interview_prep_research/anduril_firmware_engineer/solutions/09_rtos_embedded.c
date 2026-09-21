// 09_rtos_embedded.c  —  REFERENCE SOLUTION
// RTOS & 임베디드 개념 (RTOS & Embedded Systems)  —  Q96~Q102  ⏱️
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 09_rtos_embedded.c -o /tmp/andb_rtos_embedded -lpthread
// 베어메탈/RTOS 펌웨어의 "개념 문제"들을 호스트(macOS)에서 시뮬레이션한다.
//   Q96  watchdog timer   : 정기적으로 pat 하지 않으면 reset 콜백이 발동
//   Q97  cooperative sched: 함수 포인터 태스크 + 주기 기반 라운드로빈 스케줄러
//   Q98  ISR<->main 공유  : 레이스 재현 -> 임계구역(IRQ 마스킹)/_Atomic 로 수정
//   Q99  priority inversion: 우선순위 역전 시연 + 우선순위 상속/천장 설명
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
// (Q96) Watchdog Timer  —  워치독 타이머
// ---------------------------------------------------------------------------
// 하드웨어 워치독은 down-counter 다. 정기적으로 "pat"(=reload) 하지 않으면
// 카운터가 0에 도달하고 시스템 리셋을 유발한다. 여기서는 리셋 대신 콜백을 부른다.
//   - wdt_tick()  : 1 tick 진행(카운터 감소). 0 도달 시 콜백 1회 발동 후 expired.
//   - wdt_pat()   : 카운터를 timeout 으로 재장전(살아있음을 알림).
//   - expired 이후엔 pat/tick 모두 무시(래치). 실제 HW 리셋과 유사.
// ===========================================================================
typedef void (*wdt_reset_cb)(void *ctx);

typedef struct {
    uint32_t     timeout;   // reload 값 (pat 없이 버틸 수 있는 tick 수)
    uint32_t     counter;   // 남은 tick
    bool         expired;   // 한 번 터지면 래치
    wdt_reset_cb cb;        // 만료 시 콜백 (실제로는 시스템 리셋)
    void        *ctx;
} wdt_t;

void wdt_init(wdt_t *w, uint32_t timeout, wdt_reset_cb cb, void *ctx) {
    if (!w) return;
    w->timeout = timeout;
    w->counter = timeout;
    w->expired = false;
    w->cb      = cb;
    w->ctx     = ctx;
}

// pat(=kick/feed): 살아있음을 알려 카운터를 재장전. 만료 후엔 무시.
void wdt_pat(wdt_t *w) {
    if (!w || w->expired) return;
    w->counter = w->timeout;
}

// 1 tick 진행. 0 도달 시 콜백 1회 발동 후 래치.
void wdt_tick(wdt_t *w) {
    if (!w || w->expired) return;
    if (w->counter == 0 || --w->counter == 0) {  // timeout==0 즉시 만료 엣지 포함
        w->expired = true;
        if (w->cb) w->cb(w->ctx);
    }
}

bool wdt_expired(const wdt_t *w) {
    return w ? w->expired : true;   // NULL 은 "죽은 것"으로 안전 취급
}

// ===========================================================================
// (Q97) Cooperative Round-Robin Scheduler  —  협력형 스케줄러
// ---------------------------------------------------------------------------
// RTOS 없이 tiny system 에서 흔한 패턴: 함수 포인터 태스크 테이블 + 주기(period).
// 매 tick 마다 now 를 증가시키고, now % period == 0 인 태스크를 등록 순서대로 실행.
// 협력형(cooperative): 각 태스크는 짧게 실행하고 스스로 반환해야 한다(선점 없음).
// ===========================================================================
typedef void (*sched_task_fn)(void *ctx);

typedef struct {
    sched_task_fn fn;
    uint32_t      period;   // 실행 주기 (tick). 0 은 허용 안 함.
    void         *ctx;
    uint32_t      runs;     // 실행 횟수 (검증용)
} sched_task_t;

#define SCHED_MAX 8

typedef struct {
    sched_task_t tasks[SCHED_MAX];
    size_t       count;
    uint32_t     now;       // 누적 tick
} scheduler_t;

void sched_init(scheduler_t *s) {
    if (!s) return;
    s->count = 0;
    s->now   = 0;
}

// 태스크 등록. period==0 / NULL / 테이블 초과면 false.
bool sched_add_task(scheduler_t *s, sched_task_fn fn, uint32_t period, void *ctx) {
    if (!s || !fn || period == 0 || s->count >= SCHED_MAX) return false;
    sched_task_t *t = &s->tasks[s->count++];
    t->fn = fn; t->period = period; t->ctx = ctx; t->runs = 0;
    return true;
}

// 1 tick 진행: 등록 순서(라운드로빈)대로 주기가 도래한 태스크 실행.
void sched_run_tick(scheduler_t *s) {
    if (!s) return;
    s->now++;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->now % s->tasks[i].period == 0) {
            s->tasks[i].runs++;
            s->tasks[i].fn(s->tasks[i].ctx);
        }
    }
}

uint32_t sched_task_runs(const scheduler_t *s, size_t i) {
    return (s && i < s->count) ? s->tasks[i].runs : 0;
}

// ===========================================================================
// (Q98) ISR <-> main 공유 자원 보호  —  임계구역 & _Atomic
// ---------------------------------------------------------------------------
// 문제: read-modify-write(RMW) 도중 ISR 이 끼어들면 갱신이 유실(lost update)된다.
// 해법 (A) 임계구역: RMW 동안 인터럽트를 마스킹(disable) -> ISR 지연.
//        ARM CMSIS 관용구: s = __get_PRIMASK(); __disable_irq(); ...; __set_PRIMASK(s);
//        여기서는 g_primask 목(mock)으로 흉내낸다. 저장/복원 방식이라 중첩(nesting) 안전.
// 해법 (B) _Atomic: 단일 워드 카운터라면 원자 연산 하나로 RMW 를 불가분하게.
// ---------------------------------------------------------------------------
static volatile uint32_t g_primask = 0;  // 0=인터럽트 enable, 1=disable (mock PRIMASK)

// 현재 상태 저장 후 마스킹. 복원용 값을 반환. (중첩 호출 안전 관용구)
uint32_t irq_save(void) {
    uint32_t prev = g_primask;
    g_primask = 1;             // disable
    return prev;
}

// 저장했던 상태로 복원. (무조건 enable 이 아니라 "이전 상태"로 되돌림 -> 중첩 안전)
void irq_restore(uint32_t saved) {
    g_primask = saved;
}

bool irq_enabled(void) {
    return g_primask == 0;
}

// 공유 자원 + ISR 모델
typedef struct { volatile uint32_t value; } shared_t;
#define ISR_DELTA 100u

// ISR: 공유 값에 ISR_DELTA 를 더한다. (마스킹 중이면 실제 HW 에선 아예 못 들어옴)
void isr_fire(shared_t *r) {
    if (r) r->value += ISR_DELTA;
}

// (B) 원자 덧셈 래퍼: 멀티스레드/멀티코어에서도 lost update 없음.
void atomic_add_u32(_Atomic uint32_t *c, uint32_t v) {
    atomic_fetch_add_explicit(c, v, memory_order_relaxed);
}

// 멀티스레드 원자성 검증용 워커
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
// ---------------------------------------------------------------------------
// 시나리오(단일 CPU, 선점형 우선순위): 우선순위 H>M>L (숫자가 클수록 높음).
//   - L 이 공유자원 R 을 잠근 채 임계구역 실행 중.
//   - H 가 R 을 필요로 해 블록됨(L 보유).
//   - 이때 R 과 무관한 M 이 ready 이면...
//     * 상속 없음: M(2) > L(1) 이므로 M 이 먼저 실행 -> H 가 M 때문에 무한정 대기(역전!).
//     * 우선순위 상속: L 이 H 의 우선순위(3)를 물려받아 즉시 R 을 마치고 반납 -> H 대기 시간
//       이 "L 의 임계구역 잔여분"으로 경계(bounded).
// pi_effective_priority / pcp_ceiling 가 상속/천장의 핵심 primitive.
// ===========================================================================

// 우선순위 상속: 보유자의 유효 우선순위 = max(base, 최고 대기자). 역전 구간을 짧게.
int pi_effective_priority(int base, const int *waiters, size_t nwaiters) {
    int eff = base;
    if (waiters) {
        for (size_t i = 0; i < nwaiters; ++i)
            if (waiters[i] > eff) eff = waiters[i];
    }
    return eff;
}

// 우선순위 천장(Priority Ceiling): 자원을 쓰는 태스크들 중 최고 우선순위.
// 자원을 잠글 때 보유자를 이 값으로 즉시 올리면 교착/역전을 예방.
int pcp_ceiling(const int *user_prios, size_t n) {
    if (!user_prios || n == 0) return 0;
    int c = user_prios[0];
    for (size_t i = 1; i < n; ++i)
        if (user_prios[i] > c) c = user_prios[i];
    return c;
}

// --- 결정론적 시뮬레이터 (상속 유무에 따른 H 완료 시각) ---
// 상수: L 임계구역 총 CS_LEN tick, M 작업 M_WORK tick, H 는 R 획득 후 H_WORK tick.
#define PI_L_BASE 1
#define PI_M_BASE 2
#define PI_H_BASE 3
#define PI_CS_LEN 3
#define PI_M_WORK 5
#define PI_H_WORK 2

// H 가 R 사용을 끝내는 tick 을 반환. inherit=true 면 우선순위 상속 적용.
int simulate_h_finish(bool inherit) {
    // t=1 에 L 이 R 획득, t=2 시점엔 L 이 CS 1 tick 소화한 상태로 시작.
    int cs_left = PI_CS_LEN - 1;   // L 임계구역 잔여
    int m_left  = PI_M_WORK;       // M 잔여 작업
    int t       = 2;               // 현재 경과 tick
    while (cs_left > 0) {          // R 이 반납될 때까지
        int l_eff;
        if (inherit) {
            int w[1] = { PI_H_BASE };                 // H 가 대기 중 -> L 이 상속
            l_eff = pi_effective_priority(PI_L_BASE, w, 1);
        } else {
            l_eff = PI_L_BASE;                        // 상속 없음
        }
        // 실행 가능(블록된 H 제외): L(l_eff), 그리고 남은 작업 있으면 M(2)
        bool run_l = (m_left > 0) ? (l_eff >= PI_M_BASE) : true;
        if (run_l) cs_left--; else m_left--;
        t++;
    }
    // R 반납 시각 t. H 가 R 획득 후 최고 우선순위로 H_WORK tick 무중단 실행.
    t += PI_H_WORK;
    return t;
}

// ===========================================================================
// (Q100) Interrupt Latency  —  인터럽트 지연 시간
// ---------------------------------------------------------------------------
// worst-case 지연 = 다음 기여 요소들의 합:
//   sync           : 현재 명령/파이프라인 동기화 완료
//   disable_window : 인터럽트가 disable 되어 있던 최장 임계구역 (경계 대상!)
//   higher_isr     : 먼저 처리되는 상위/동급 우선순위 ISR 들의 실행 시간 합
//   context_save   : CPU 상태 스태킹(문맥 저장)
//   isr_prologue   : 벡터 페치 + 핸들러 진입
// 임계구역을 짧게 유지하면 disable_window 가 줄어 최악 지연이 경계된다.
// ===========================================================================
typedef struct {
    uint32_t sync;
    uint32_t disable_window;
    uint32_t higher_isr;
    uint32_t context_save;
    uint32_t isr_prologue;
} irq_latency_t;

// 포화 덧셈(overflow 시 UINT32_MAX 고정) — 최악값 계산에서 오버플로 UB 방지.
static uint32_t sat_add(uint32_t a, uint32_t b) {
    uint32_t s = a + b;
    return (s < a) ? UINT32_MAX : s;
}

uint32_t irq_latency_wc(const irq_latency_t *m) {
    if (!m) return 0;
    uint32_t t = 0;
    t = sat_add(t, m->sync);
    t = sat_add(t, m->disable_window);
    t = sat_add(t, m->higher_isr);
    t = sat_add(t, m->context_save);
    t = sat_add(t, m->isr_prologue);
    return t;
}

// 여러 임계구역 중 최장(=disable_window 을 결정하는 경계값).
uint32_t max_disable_window(const uint32_t *cs, size_t n) {
    if (!cs) return 0;
    uint32_t m = 0;
    for (size_t i = 0; i < n; ++i) if (cs[i] > m) m = cs[i];
    return m;
}

// 최악 지연 + ISR 실행시간 <= 마감(deadline) 이면 실시간 요건 충족.
bool meets_deadline(uint32_t wc_latency, uint32_t isr_exec, uint32_t deadline) {
    return sat_add(wc_latency, isr_exec) <= deadline;
}

// 실측 데모용(단조 시계, 정보 출력 전용 — 타이밍은 assert 하지 않음)
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ===========================================================================
// (Q101) Memory-Mapped Register Access  —  MMIO + volatile
// ---------------------------------------------------------------------------
// 주변장치 레지스터는 volatile 로 접근해야 컴파일러가 읽기/쓰기를 제거·캐시·재정렬
// 하지 못한다. 상태 폴링 루프에서 특히 중요(없으면 무한루프로 최적화될 수 있음).
// 호스트에선 실제 MMIO 대신 정적 배열을 "주변장치"로 사용해 관용구를 시연한다.
// ===========================================================================
uint32_t reg_read(const volatile uint32_t *reg) {
    return reg ? *reg : 0;
}
void reg_write(volatile uint32_t *reg, uint32_t val) {
    if (reg) *reg = val;
}
// 비트 셋(RMW): read-or-write.
void reg_set_bits(volatile uint32_t *reg, uint32_t mask) {
    if (reg) *reg |= mask;
}
// 비트 클리어(RMW): read-and~mask-write.
void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) {
    if (reg) *reg &= ~mask;
}
// 필드 수정: 마스크 영역을 지우고 (val<<shift)&mask 로 대체.
void reg_modify_field(volatile uint32_t *reg, uint32_t mask, uint32_t shift, uint32_t val) {
    if (!reg || shift >= 32) return;              // shift>=32 는 UB -> 가드
    uint32_t v = *reg;
    v &= ~mask;
    v |= (val << shift) & mask;
    *reg = v;
}
// 플래그 폴링: mask 비트가 모두 셋이 될 때까지(최대 max_iters) 대기. 타임아웃 시 false.
// volatile 이라 매 반복 실제 메모리를 재-읽는다(컴파일러가 hoist 못 함).
bool reg_poll_flag(const volatile uint32_t *reg, uint32_t mask, uint32_t max_iters) {
    if (!reg) return false;
    for (uint32_t i = 0; i < max_iters; ++i) {
        if ((*reg & mask) == mask) return true;
    }
    return false;
}

// ===========================================================================
// (Q102) Software Timer Wheel  —  소프트웨어 타이머 휠 (malloc 없이)
// ---------------------------------------------------------------------------
// TW_SLOTS 개의 슬롯을 가진 해시 타이밍 휠. delay tick 뒤 만료할 타이머를
//   slot   = (now + delay) % TW_SLOTS
//   rounds = (delay - 1) / TW_SLOTS   (그 슬롯을 몇 바퀴 더 지나야 하는지)
// 로 배치한다. tick 마다 현재 슬롯의 타이머들을 훑어 rounds==0 이면 발동, 아니면 rounds--.
// 노드는 정적 풀 + free-list 로 관리(힙 없음). 슬롯별 단일 연결 리스트(인덱스 링크).
// O(1) 등록, tick 당 해당 슬롯만 순회 -> tick당 O(슬롯 내 타이머 수).
// ===========================================================================
typedef void (*tw_cb)(void *ctx);
#define TW_SLOTS 8
#define TW_MAX   16

typedef struct {
    bool     active;
    uint32_t rounds;
    tw_cb    cb;
    void    *ctx;
    int      next;    // 슬롯 리스트/free-list 링크 (풀 인덱스, -1=끝)
} tw_timer_t;

typedef struct {
    tw_timer_t pool[TW_MAX];
    int        slots[TW_SLOTS];  // 슬롯별 head 인덱스 (-1=빈 슬롯)
    int        free_head;        // free-list head
    uint32_t   now;              // 누적 tick
} timer_wheel_t;

void tw_init(timer_wheel_t *w) {
    if (!w) return;
    for (int i = 0; i < TW_MAX - 1; ++i) { w->pool[i].next = i + 1; w->pool[i].active = false; }
    w->pool[TW_MAX - 1].next = -1;
    w->pool[TW_MAX - 1].active = false;
    w->free_head = 0;
    for (int i = 0; i < TW_SLOTS; ++i) w->slots[i] = -1;
    w->now = 0;
}

static int tw_alloc(timer_wheel_t *w) {
    int idx = w->free_head;
    if (idx < 0) return -1;
    w->free_head = w->pool[idx].next;
    return idx;
}
static void tw_release(timer_wheel_t *w, int idx) {
    w->pool[idx].active = false;
    w->pool[idx].next = w->free_head;
    w->free_head = idx;
}

// 타이머 등록. delay tick 뒤 cb(ctx) 발동. 풀 고갈/NULL/delay==0 이면 -1.
int tw_start(timer_wheel_t *w, uint32_t delay, tw_cb cb, void *ctx) {
    if (!w || !cb || delay == 0) return -1;
    int idx = tw_alloc(w);
    if (idx < 0) return -1;
    uint32_t slot = (w->now + delay) % TW_SLOTS;
    w->pool[idx].active = true;
    w->pool[idx].rounds = (delay - 1) / TW_SLOTS;
    w->pool[idx].cb     = cb;
    w->pool[idx].ctx    = ctx;
    w->pool[idx].next   = w->slots[slot];   // 슬롯 리스트 앞에 삽입
    w->slots[slot]      = idx;
    return idx;
}

// 1 tick 진행: 새 현재 슬롯의 타이머들을 순회, rounds==0 이면 발동 후 반납.
void tw_tick(timer_wheel_t *w) {
    if (!w) return;
    w->now++;
    uint32_t s = w->now % TW_SLOTS;
    int *pp = &w->slots[s];
    while (*pp != -1) {
        int idx = *pp;
        if (w->pool[idx].rounds == 0) {
            tw_cb cb  = w->pool[idx].cb;
            void *ctx = w->pool[idx].ctx;
            *pp = w->pool[idx].next;    // 리스트에서 unlink (next 는 release 전에 읽음)
            tw_release(w, idx);
            cb(ctx);                    // 발동 (콜백에서 재등록도 안전)
        } else {
            w->pool[idx].rounds--;
            pp = &w->pool[idx].next;
        }
    }
}

// ===========================================================================
// 테스트용 콜백/컨텍스트
// ===========================================================================
static void wdt_count_cb(void *ctx) { (*(int *)ctx)++; }         // 워치독 리셋 카운트

static void task_tick_cb(void *ctx) { (*(uint32_t *)ctx)++; }    // 스케줄러 태스크

typedef struct { bool fired; uint32_t fired_at; const uint32_t *now_ref; } tmark_t;
static void tmark_cb(void *ctx) {
    tmark_t *m = (tmark_t *)ctx;
    m->fired = true;
    m->fired_at = *m->now_ref;   // 발동 시점의 wheel now
}

// 측정 데모용 임계구역(바쁜 루프)
static volatile uint32_t g_sink;
static void busy_critical_section(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) g_sink += i;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
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
    wdt_pat(&w);                                        // 재장전
    wdt_tick(&w); wdt_tick(&w);
    T("pat then 2 ticks -> still alive", !wdt_expired(&w) && resets == 0);
    wdt_tick(&w);                                       // 3번째 -> 만료
    T("3rd tick -> expired, reset fired", wdt_expired(&w) && resets == 1);
    wdt_tick(&w); wdt_pat(&w); wdt_tick(&w);
    T("after expiry: ticks/pat ignored (reset once)", resets == 1);
    // timeout==0 즉시 만료 엣지
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
    for (int i = 0; i < 6; ++i) sched_run_tick(&s);     // 6 ticks
    T("p=1 ran 6x",                      sched_task_runs(&s, 0) == 6 && c1 == 6);
    T("p=2 ran 3x",                      sched_task_runs(&s, 1) == 3 && c2 == 3);
    T("p=3 ran 2x",                      sched_task_runs(&s, 2) == 2 && c3 == 2);
    // 테이블 초과 거부
    scheduler_t sf; sched_init(&sf);
    int added = 0;
    while (sched_add_task(&sf, task_tick_cb, 1, &c1)) ++added;
    T("table full at SCHED_MAX (8)",     added == SCHED_MAX);

    // -------- (Q98) ISR<->main shared resource --------
    printf("== (Q98) ISR<->main Shared Resource ==\n");
    // 임계구역 저장/복원 + 중첩
    T("irq enabled initially",           irq_enabled());
    uint32_t s1 = irq_save();
    T("after save -> disabled",          !irq_enabled());
    uint32_t s2 = irq_save();            // 중첩
    irq_restore(s2);
    T("inner restore -> still disabled", !irq_enabled());
    irq_restore(s1);
    T("outer restore -> enabled again",  irq_enabled());
    // 레이스 재현: RMW 중 ISR 이 끼어들면 갱신 유실
    shared_t rr = { 0 };
    uint32_t tmp = rr.value;             // main 이 0 을 읽음
    isr_fire(&rr);                       // ISR: +100 -> 100
    rr.value = tmp + 1;                  // main 이 1 을 씀 -> ISR 의 +100 유실!
    T("RACE reproduced: ISR update lost (value==1, not 101)", rr.value == 1);
    // 임계구역으로 수정: 마스킹 중엔 ISR 이 못 들어옴
    shared_t rs = { 0 };
    uint32_t sv = irq_save();            // disable
    uint32_t t2 = rs.value;              // 임계구역: RMW 불가분
    rs.value = t2 + 1;
    irq_restore(sv);                     // enable
    isr_fire(&rs);                       // 이제 안전하게 +100
    T("FIX (critical section): value==101 (no loss)", rs.value == 101);
    // 원자 연산: 멀티스레드에서도 lost update 없음
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
    // 실측 데모(정보 전용): 임계구역 길이를 측정해 경계 근거로 삼는다.
    uint64_t t_start = now_ns();
    busy_critical_section();
    uint64_t t_end = now_ns();
    printf("    measured critical-section length ~= %llu ns (informational)\n",
           (unsigned long long)(t_end - t_start));

    // -------- (Q101) MMIO volatile --------
    printf("== (Q101) Memory-Mapped Register Access (volatile) ==\n");
    static volatile uint32_t PERIPH[2];   // 가짜 주변장치 레지스터 블록
    volatile uint32_t *CR = &PERIPH[0];   // control register
    volatile uint32_t *SR = &PERIPH[1];   // status register
    reg_write(CR, 0);
    T("reg_write/read roundtrip",        (reg_write(CR, 0xABCD0000u), reg_read(CR) == 0xABCD0000u));
    reg_set_bits(CR, 0x1u);
    T("reg_set_bits sets bit0",          (reg_read(CR) & 0x1u) == 0x1u);
    reg_clear_bits(CR, 0x1u);
    T("reg_clear_bits clears bit0",      (reg_read(CR) & 0x1u) == 0u);
    // 4비트 필드 @ shift 8 (mask 0xF00) 를 0xA 로
    reg_modify_field(CR, 0xF00u, 8, 0xAu);
    T("reg_modify_field field==0xA",     ((reg_read(CR) >> 8) & 0xFu) == 0xAu);
    reg_modify_field(CR, 0xF00u, 8, 0x3u);
    T("reg_modify_field overwrite==0x3", ((reg_read(CR) >> 8) & 0xFu) == 0x3u);
    T("reg_modify_field shift>=32 -> no-op safe", (reg_modify_field(CR, 0xFu, 40, 1), true));
    T("reg_*(NULL) safe",                (reg_write(NULL, 1), reg_set_bits(NULL, 1),
                                          reg_clear_bits(NULL, 1), reg_read(NULL) == 0));
    // 플래그 폴링
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
    // delay 1,3,8,9,16 -> 정확히 그 tick 에 발동해야
    uint32_t delays[5] = { 1, 3, 8, 9, 16 };
    tmark_t marks[5];
    for (int i = 0; i < 5; ++i) {
        marks[i].fired = false; marks[i].fired_at = 0; marks[i].now_ref = &tw.now;
        T("tw_start ok", tw_start(&tw, delays[i], tmark_cb, &marks[i]) >= 0);
    }
    for (int i = 0; i < 16; ++i) tw_tick(&tw);          // 16 tick 진행
    bool all_ok = true;
    for (int i = 0; i < 5; ++i)
        if (!marks[i].fired || marks[i].fired_at != delays[i]) all_ok = false;
    T("all timers fired at exact delay tick (1,3,8,9,16)", all_ok);
    // 풀 고갈: TW_MAX 개까지만
    timer_wheel_t tf; tw_init(&tf);
    int okc = 0;
    while (tw_start(&tf, 5, tmark_cb, NULL) >= 0) ++okc;
    T("pool exhaustion at TW_MAX (16)", okc == TW_MAX);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
