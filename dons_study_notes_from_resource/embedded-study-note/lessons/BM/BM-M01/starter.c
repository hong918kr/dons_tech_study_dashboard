/* BM-M01 NVIC 기초: ISER/ICER/IPR 비트 계산과 ISR 규칙
 *
 * Part A — mock NVIC: ISER/ICER (1 을 쓴 비트만 set/clear, 읽으면 enable 상태), ISPR/ICPR, IABR(읽기 전용),
 *          IPR (IRQ 당 1 바이트, 상위 4비트만 구현), STIR. 그 위에 CMSIS 식 드라이버 nvic_* 를 쓰고
 *          IRQ 0..95 전체를 전수 검사한다 (대상 비트/바이트만 바뀌었나, 버스 접근이 몇 번인가).
 * Part B — 음수 IRQn → SCB SHPR1..3, 잘못된 번호 거부, 고전 버그 재현(ICER |=, 시프트 빠진 prio, IRQ 번호를
 *          워드 인덱스로), 32비트 전용 mock 에서 IPR 워드 RMW 와 ISR 레이스.
 * Part C — 늦은 플래그 클리어: 주변장치 쓰기는 버스 브리지를 지나 늦게 도착(posted write)하고, NVIC 는 예외
 *          복귀 순간 라인을 다시 본다 → ISR 재진입(헛 인터럽트). 고침: 먼저 지우기 + read-back. 초기화 순서.
 *
 * 주소·비트는 ARMv7-M ARM / RM0090(STM32F4) 기준 대표값 — 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-M01_x && /tmp/BM-M01_x   (-v: 트레이스)
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 레지스터 접근 seam (BM-B02 mock 축약판) ======================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* ---- ARMv7-M NVIC / SCB (ARM ARM B3.4, B3.2 기준 대표값 — 확인 권장) ---- */
#define NVIC_ISER0  0xE000E100u   /* +4·n  W1S, 읽으면 enable 상태 */
#define NVIC_ICER0  0xE000E180u   /* +4·n  W1C, 읽으면 enable 상태 */
#define NVIC_ISPR0  0xE000E200u   /* +4·n  W1S, 읽으면 pending 상태 */
#define NVIC_ICPR0  0xE000E280u   /* +4·n  W1C, 읽으면 pending 상태 */
#define NVIC_IABR0  0xE000E300u   /* +4·n  읽기 전용 active 상태 */
#define NVIC_IPR0   0xE000E400u   /* IRQ 당 1 바이트: 워드 irq>>2, 레인 irq&3 */
#define NVIC_STIR   0xE000EF00u   /* [8:0] INTID 쓰기 → 그 IRQ pending */
#define SCB_SHPR1   0xE000ED18u   /* 예외 4..7, SHPR2 0xE000ED1C = 8..11, SHPR3 0xE000ED20 = 12..15 */
#define NVIC_PRIO_BITS 4u         /* STM32F4 __NVIC_PRIO_BITS: 우선순위 값은 바이트의 [7:4] */
#define PRIO_IMPL_MASK ((0xFFu << (8u - NVIC_PRIO_BITS)) & 0xFFu)   /* 0xF0 */
#define N_IRQ       96            /* 이 모델: ICTR.INTLINESNUM = 2 → 96 라인 (F407 은 82개 사용 — 확인 권장) */
#define N_WORDS     (N_IRQ / 32)
#define BANK_WORD(a) ((((uint32_t)(a)) & 0x7Fu) >> 2)   /* ISER/ICER/ISPR/ICPR/IABR 공통: 뱅크 안 워드 번호 */
/* CMSIS 시스템 예외 IRQn (= 예외 번호 − 16) */
#define NonMaskableInt_IRQn   (-14)
#define HardFault_IRQn        (-13)
#define MemoryManagement_IRQn (-12)
#define BusFault_IRQn         (-11)
#define UsageFault_IRQn       (-10)
#define SVCall_IRQn           (-5)
#define DebugMonitor_IRQn     (-4)
#define PendSV_IRQn           (-2)
#define SysTick_IRQn          (-1)
/* ---- STM32F4 TIM2 (RM0090 기준 대표값 — 확인 권장) ---- */
#define TIM2_BASE   0x40000000u
#define TIM2_DIER   (TIM2_BASE + 0x0Cu)   /* UIE = bit0 */
#define TIM2_SR     (TIM2_BASE + 0x10u)   /* rc_w0, UIF = bit0 */
#define TIM_UIF     (1u << 0)
#define TIM_UIE     (1u << 0)
#define TIM2_IRQn   28

/* ======================= mock core: 레지스터 표 + 훅 + 로그 + 끼어들기 + 시간 ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* CPU 가 보는 값 */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* 새 저장값 */
typedef void (*mock_fn)(void);
typedef struct { uint32_t addr, stored; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; bool isr; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 64
#define MOCK_LOG_CAP  64
static mock_reg_t g_reg[MOCK_MAX_REGS];
static int        g_nreg;
static mock_log_t g_log[MOCK_LOG_CAP];
static int        g_nlog;
static uint32_t   g_nacc;                  /* CPU 버스 접근 수 */
static int        g_unexpected;            /* 표에 없는 주소 — 테스트 실패로 센다 (실칩은 RAZ/WI 라 조용하다) */
static struct { uint32_t at; mock_fn fn; } g_inj;   /* g_nacc == at 인 접근 직후 ISR fn 실행 (BM-B04 방식) */
static bool       g_primask, g_in_isr;
static mock_fn    g_deferred;              /* PRIMASK 때문에 미뤄진 ISR */

/* 시간 + posted write: 주변장치(0x4000_0000~) 쓰기는 버스 브리지를 지나 늦게 도착한다. PPB(NVIC/SCB)는 즉시 */
static uint64_t g_cycle;
static uint32_t g_core_wb, g_bridge;       /* 코어 쓰기 버퍼 지연, 브리지 posted 지연 (사이클) */
typedef struct { uint32_t addr, val; uint64_t t_core, t_arr; } post_t;
#define POST_CAP 8
static post_t g_post[POST_CAP];
static int    g_npost;

/* 트레이스 (Part C 브링업 로그) */
#define TRACE_CAP 40
static struct { uint64_t cyc; const char *ev; uint32_t v; } g_tr[TRACE_CAP];
static int  g_ntr;
static bool g_tron;
static void trace(const char *ev, uint32_t v) {
    if (g_tron && g_ntr < TRACE_CAP) { g_tr[g_ntr].cyc = g_cycle; g_tr[g_ntr].ev = ev; g_tr[g_ntr].v = v; g_ntr++; }
}

static void mock_reset(void) {
    memset(g_reg, 0, sizeof g_reg); g_nreg = 0; g_nlog = 0; g_nacc = 0; g_unexpected = 0;
    memset(&g_inj, 0, sizeof g_inj); g_primask = g_in_isr = false; g_deferred = NULL;
    g_cycle = 0; g_core_wb = g_bridge = 0; g_npost = 0; g_ntr = 0; g_tron = false;
}
static int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nreg >= MOCK_MAX_REGS) return -1;
    g_reg[g_nreg] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return g_nreg++;
}
static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nreg; i++) if (g_reg[i].addr == addr) return &g_reg[i];
    return NULL;
}
static void mock_log_add(char op, uint32_t addr, uint32_t val) {
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, g_in_isr, addr, val };
}
static void mock_apply_write(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (r == NULL) { g_unexpected++; return; }
    r->stored = r->w ? r->w(addr, r->stored, val, r->ctx) : val;
}
static void tim_tick(void);
static void clock_advance(uint32_t n) {             /* 한 사이클씩: 도착한 쓰기 적용 → 타이머 */
    while (n--) {
        g_cycle++;
        while (g_npost > 0 && g_post[0].t_arr <= g_cycle) {   /* FIFO: 지연이 일정하니 도착 순서 = 발행 순서 */
            post_t p = g_post[0];
            memmove(&g_post[0], &g_post[1], (size_t)(g_npost - 1) * sizeof g_post[0]);
            g_npost--;
            trace("bridge: write arrives at peripheral", p.val);
            mock_apply_write(p.addr, p.val);
        }
        tim_tick();
    }
}
static bool is_periph(uint32_t a) { return a >= 0x40000000u && a < 0x60000000u; }
static void mock_after_access(void) {
    if (g_inj.fn == NULL || g_in_isr || g_nacc != g_inj.at) return;
    mock_fn fn = g_inj.fn; g_inj.fn = NULL;
    if (g_primask) { g_deferred = fn; return; }      /* NVIC 펜딩으로 남는다 */
    g_in_isr = true; fn(); g_in_isr = false;
}
uint32_t mock_read32(uint32_t addr) {
    if (is_periph(addr)) while (g_npost > 0) clock_advance(1);   /* 같은 브리지 읽기: 앞선 쓰기가 먼저 끝난다 */
    mock_reg_t *r = mock_find(addr);
    uint32_t v = 0;
    if (r == NULL) g_unexpected++;
    else v = r->r ? r->r(addr, r->stored, r->ctx) : r->stored;
    mock_log_add('R', addr, v); g_nacc++; clock_advance(1); mock_after_access();
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    if (is_periph(addr)) {
        while (g_npost >= POST_CAP) clock_advance(1);
        g_post[g_npost++] = (post_t){ addr, val, g_cycle + 1u + g_core_wb, g_cycle + 1u + g_core_wb + g_bridge };
        if (g_core_wb + g_bridge > 0u) trace("CPU: write posted (not yet at peripheral)", val);
    } else {
        mock_apply_write(addr, val);                  /* PPB: Strongly-ordered, 즉시 */
    }
    mock_log_add('W', addr, val); g_nacc++; clock_advance(1); mock_after_access();
}
static void mock_inject_at(uint32_t n, mock_fn fn) { g_inj.at = n; g_inj.fn = fn; }
static void cpu_work(uint32_t n) { clock_advance(n); }          /* 버스 접근 없는 계산 n 사이클 */
static void cpu_dsb(void) {                                      /* DSB 모델: 코어 쪽 쓰기 버퍼만 비운다 */
    uint64_t t = 0;
    for (int i = 0; i < g_npost; i++) if (g_post[i].t_core > t) t = g_post[i].t_core;
    while (g_cycle < t) clock_advance(1);
}
/* 임계 구역 (C-M14, ARM-M10). 타깃: __get_PRIMASK / __disable_irq / __set_PRIMASK */
uint32_t cs_enter(void) { uint32_t p = g_primask ? 1u : 0u; g_primask = true; return p; }
void cs_exit(uint32_t prev) {
    g_primask = prev != 0u;
    if (!g_primask && g_deferred) { mock_fn f = g_deferred; g_deferred = NULL; g_in_isr = true; f(); g_in_isr = false; }
}

/* ======================= NVIC 모델 (이 레슨이 전체판을 소유) ======================= */
static struct {
    uint32_t en[N_WORDS], pend[N_WORDS], act[N_WORDS], line[N_WORDS];
    uint8_t  ipr[N_IRQ];        /* 저장된 우선순위 바이트 (구현 비트만) */
    uint8_t  shp[12];           /* 예외 4..15 의 우선순위 바이트 (SHPR1..3) */
    int      n_impl;            /* 구현된 IRQ 수 — 그 위 비트는 RAZ/WI */
} g_nv;
static uint32_t impl_mask(uint32_t w) {
    int lo = (int)w * 32;
    if (g_nv.n_impl >= lo + 32) return 0xFFFFFFFFu;
    if (g_nv.n_impl <= lo) return 0u;
    return (1u << (g_nv.n_impl - lo)) - 1u;
}
/* 레벨 인터럽트: 라인이 high 이고 active 가 아니면 pending (Cortex-M Generic User Guide 4.2.9 — 확인 권장) */
static void nvic_relatch(uint32_t w) { g_nv.pend[w] |= g_nv.line[w] & ~g_nv.act[w] & impl_mask(w); }
static void irq_line_set(int irq, bool level) {
    uint32_t w = (uint32_t)irq >> 5, b = 1u << ((uint32_t)irq & 31u);
    if (level) g_nv.line[w] |= b; else g_nv.line[w] &= ~b;
    nvic_relatch(w);
}
static uint32_t en_on_read(uint32_t a, uint32_t s, void *c)   { (void)s; (void)c; return g_nv.en[BANK_WORD(a)]; }
static uint32_t pend_on_read(uint32_t a, uint32_t s, void *c) { (void)s; (void)c; return g_nv.pend[BANK_WORD(a)]; }
static uint32_t act_on_read(uint32_t a, uint32_t s, void *c)  { (void)s; (void)c; return g_nv.act[BANK_WORD(a)]; }
/* ISER: 1 을 쓴 비트만 enable, 0 은 영향 없음 (구현 안 된 IRQ 비트는 무시) */
static uint32_t iser_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    g_nv.en[BANK_WORD(a)] = val;                    /* TODO: 지금은 평범한 rw 처럼 덮어쓴다 (틀림) */
    return 0u;
}
/* ICER: 1 을 쓴 비트만 disable, 0 은 영향 없음 */
static uint32_t icer_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    g_nv.en[BANK_WORD(a)] = val;                    /* TODO: 지금은 평범한 rw 처럼 덮어쓴다 (틀림) */
    return 0u;
}
static uint32_t ispr_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    g_nv.pend[BANK_WORD(a)] |= val & impl_mask(BANK_WORD(a));
    return 0u;
}
static uint32_t icpr_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    g_nv.pend[BANK_WORD(a)] &= ~val;
    nvic_relatch(BANK_WORD(a));                     /* 소스 라인이 아직 high 면 곧바로 다시 pending */
    return 0u;
}
static uint32_t ro_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {   /* IABR: 쓰기 무시 */
    (void)a; (void)val; (void)c;
    return old;
}
static uint32_t ipr_on_read(uint32_t a, uint32_t s, void *c) {
    (void)s; (void)c;
    uint32_t w = (a - NVIC_IPR0) >> 2, v = 0;
    for (uint32_t lane = 0; lane < 4u; lane++) v |= (uint32_t)g_nv.ipr[4u * w + lane] << (8u * lane);
    return v;
}
/* IPR: 바이트 4개. 각 바이트의 상위 NVIC_PRIO_BITS 비트만 구현 — 하위 비트는 읽으면 0 (RAZ/WI) */
static uint32_t ipr_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    uint32_t w = (a - NVIC_IPR0) >> 2;
    for (uint32_t lane = 0; lane < 4u; lane++)      /* TODO: 구현 비트 마스크가 없다 (틀림) */
        g_nv.ipr[4u * w + lane] = (uint8_t)(val >> (8u * lane));
    return 0u;
}
static uint32_t stir_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)a; (void)old; (void)c;
    uint32_t id = val & 0x1FFu;                     /* INTID [8:0] */
    if ((int)id < g_nv.n_impl) g_nv.pend[id >> 5] |= 1u << (id & 31u);
    return 0u;
}
static bool shp_implemented(uint32_t exc) {         /* 4 MemManage 5 BusFault 6 UsageFault 11 SVCall 12 DebugMon 14 PendSV 15 SysTick */
    return exc == 4u || exc == 5u || exc == 6u || exc == 11u || exc == 12u || exc == 14u || exc == 15u;
}
static uint32_t shpr_on_read(uint32_t a, uint32_t s, void *c) {
    (void)s; (void)c;
    uint32_t w = (a - SCB_SHPR1) >> 2, v = 0;
    for (uint32_t lane = 0; lane < 4u; lane++) v |= (uint32_t)g_nv.shp[4u * w + lane] << (8u * lane);
    return v;
}
static uint32_t shpr_on_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)old; (void)c;
    uint32_t w = (a - SCB_SHPR1) >> 2;
    for (uint32_t lane = 0; lane < 4u; lane++)      /* 예약된 예외(7..10, 13)의 바이트는 RAZ/WI */
        if (shp_implemented(4u + 4u * w + lane))
            g_nv.shp[4u * w + lane] = (uint8_t)((val >> (8u * lane)) & PRIO_IMPL_MASK);
    return 0u;
}

/* ---- NVIC-lite 디스패처: 단일 레벨(선점 없음 — 선점/PRIGROUP 은 BM-M02), 동기 호출 ---- */
static mock_fn  g_vec[N_IRQ];
static uint32_t g_isr_calls, g_storms;
#define ISR_CAP_PER_STEP 16                          /* 한 스텝에 16 번 넘게 들어가면 "인터럽트 폭주" 로 끊는다 */
static int nvic_pick(void) {                         /* enabled & pending & !active 중 우선순위 값 최소, 같으면 번호 최소 */
    int best = -1;
    for (uint32_t w = 0; w < N_WORDS; w++) {
        uint32_t m = g_nv.en[w] & g_nv.pend[w] & ~g_nv.act[w] & impl_mask(w);
        for (uint32_t bit = 0; m != 0u; bit++, m >>= 1) {
            int i = (int)(32u * w + bit);
            if ((m & 1u) && (best < 0 || g_nv.ipr[i] < g_nv.ipr[best])) best = i;
        }
    }
    return best;
}
static void nvic_dispatch(void) {
    if (g_primask || g_in_isr) return;
    for (int n = 0; n < ISR_CAP_PER_STEP; n++) {
        int irq = nvic_pick();
        if (irq < 0) return;
        uint32_t w = (uint32_t)irq >> 5, b = 1u << ((uint32_t)irq & 31u);
        g_nv.pend[w] &= ~b; g_nv.act[w] |= b; g_isr_calls++;      /* 진입: pending → active */
        trace("NVIC: exception entry IRQ", (uint32_t)irq);
        g_in_isr = true; if (g_vec[irq]) g_vec[irq](); g_in_isr = false;
        g_nv.act[w] &= ~b;                                         /* 복귀: active 해제 후 라인을 다시 본다 */
        nvic_relatch(w);
        trace((g_nv.pend[w] & b) ? "NVIC: exc return, line HIGH -> pending again (tail-chain)"
                                 : "NVIC: exc return, line low", (uint32_t)irq);
    }
    g_storms++;
}
static void sim_step(void) { clock_advance(1); nvic_dispatch(); }

/* ---- TIM2 모델 (BM-B14 타이머 모델 축약판): period 사이클마다 UIF, 라인 = UIF & UIE ---- */
static struct { uint32_t sr, dier, period, phase, updates; bool run; } g_tim;
static void tim2_line(void) { irq_line_set(TIM2_IRQn, (g_tim.sr & TIM_UIF) && (g_tim.dier & TIM_UIE)); }
static uint32_t tim_sr_read(uint32_t a, uint32_t s, void *c)   { (void)a; (void)s; (void)c; return g_tim.sr; }
static uint32_t tim_dier_read(uint32_t a, uint32_t s, void *c) { (void)a; (void)s; (void)c; return g_tim.dier; }
static uint32_t tim_sr_write(uint32_t a, uint32_t old, uint32_t val, void *c) {   /* rc_w0 (BM-B04) */
    (void)a; (void)old; (void)c;
    g_tim.sr &= val; tim2_line();
    return 0u;
}
static uint32_t tim_dier_write(uint32_t a, uint32_t old, uint32_t val, void *c) {
    (void)a; (void)old; (void)c;
    g_tim.dier = val & TIM_UIE; tim2_line();
    return 0u;
}
static void tim_tick(void) {
    if (!g_tim.run || g_tim.period == 0u || g_cycle % g_tim.period != g_tim.phase) return;
    g_tim.sr |= TIM_UIF; g_tim.updates++;
    trace("TIM2: update event, UIF=1", g_tim.updates);
    tim2_line();
}

static void board_setup(int n_impl) {
    mock_reset();
    memset(&g_nv, 0, sizeof g_nv); g_nv.n_impl = n_impl;
    memset(&g_tim, 0, sizeof g_tim); memset(g_vec, 0, sizeof g_vec); g_isr_calls = g_storms = 0;
    for (uint32_t w = 0; w < N_WORDS; w++) {
        mock_add_reg(NVIC_ISER0 + 4u * w, 0u, en_on_read, iser_on_write, NULL);
        mock_add_reg(NVIC_ICER0 + 4u * w, 0u, en_on_read, icer_on_write, NULL);
        mock_add_reg(NVIC_ISPR0 + 4u * w, 0u, pend_on_read, ispr_on_write, NULL);
        mock_add_reg(NVIC_ICPR0 + 4u * w, 0u, pend_on_read, icpr_on_write, NULL);
        mock_add_reg(NVIC_IABR0 + 4u * w, 0u, act_on_read, ro_on_write, NULL);
    }
    for (uint32_t w = 0; w < N_IRQ / 4; w++) mock_add_reg(NVIC_IPR0 + 4u * w, 0u, ipr_on_read, ipr_on_write, NULL);
    mock_add_reg(NVIC_STIR, 0u, NULL, stir_on_write, NULL);
    for (uint32_t w = 0; w < 3u; w++) mock_add_reg(SCB_SHPR1 + 4u * w, 0u, shpr_on_read, shpr_on_write, NULL);
    mock_add_reg(TIM2_DIER, 0u, tim_dier_read, tim_dier_write, NULL);
    mock_add_reg(TIM2_SR, 0u, tim_sr_read, tim_sr_write, NULL);
}

/* ======================= Part A: CMSIS 식 NVIC 드라이버 ======================= */
#define IRQ_WORD(n) ((uint32_t)(n) >> 5)             /* 32 개씩: ISER0 = IRQ 0..31, ISER1 = 32..63 ... */
#define IRQ_BIT(n)  (1u << ((uint32_t)(n) & 31u))
bool irq_valid(int irqn) { return irqn >= 0 && irqn < N_IRQ; }

int nvic_enable_irq(int irqn) {
    (void)irqn;
    return -1;                                                       /* TODO */
}
int nvic_disable_irq(int irqn) {
    (void)irqn;
    return -1;                                                       /* TODO */
}
int nvic_get_enable(int irqn) {                                      /* 1 / 0, 잘못된 번호 −1 */
    (void)irqn;
    return 0;                                                        /* TODO */
}
int nvic_set_pending(int irqn) {
    (void)irqn;
    return -1;                                                       /* TODO */
}
int nvic_clear_pending(int irqn) {
    (void)irqn;
    return -1;                                                       /* TODO */
}
int nvic_get_pending(int irqn) {
    (void)irqn;
    return 0;                                                        /* TODO */
}
int nvic_get_active(int irqn) {
    (void)irqn;
    return 0;                                                        /* TODO */
}
int nvic_trigger_stir(int irqn) {                                    /* 소프트웨어 트리거: STIR 에 번호를 쓴다 */
    (void)irqn;
    return -1;                                                       /* TODO */
}
/* 우선순위 바이트의 위치: IRQn ≥ 0 → IPR 워드 + 레인, IRQn < 0 → SHPR1..3 (Part B). 성공 0, 잘못된 번호 −1 */
int prio_loc(int irqn, uint32_t *word_addr, uint32_t *shift) {
    (void)irqn; (void)word_addr; (void)shift;
    return -1;                                                       /* TODO */
}
/* prio: 0..15 (0 = 가장 급함). 바이트 [7:4] 에 넣는다. mock 은 32비트 접근만 → 워드 RMW (실칩은 바이트 STRB 가능) */
int nvic_set_priority(int irqn, uint32_t prio) {
    (void)irqn; (void)prio;
    return -1;                                                       /* TODO */
}
int nvic_get_priority(int irqn) {                                    /* 0..15, 잘못된 번호 −1 */
    (void)irqn;
    return 0;                                                        /* TODO */
}

/* ======================= Part B: 흔한 버그 코드 (주어짐 — 재현용) ======================= */
static void bug_disable_oreq(int irq) {              /* NVIC->ICER[n] |= bit */
    uint32_t a = NVIC_ICER0 + 4u * IRQ_WORD(irq);
    REG_WRITE(a, REG_READ(a) | IRQ_BIT(irq));
}
static void bug_enable_oreq(int irq) {               /* NVIC->ISER[n] |= bit */
    uint32_t a = NVIC_ISER0 + 4u * IRQ_WORD(irq);
    REG_WRITE(a, REG_READ(a) | IRQ_BIT(irq));
}
static void bug_enable_wordindex(int irq) {          /* NVIC->ISER[irq] = bit — IRQ 번호를 워드 인덱스로 */
    REG_WRITE(NVIC_ISER0 + 4u * (uint32_t)irq, IRQ_BIT(irq));
}
static void ipr_rmw(int irq, uint32_t byte) {        /* 보호 없는 워드 RMW (위치는 맞음) */
    uint32_t a = NVIC_IPR0 + 4u * ((uint32_t)irq >> 2), sh = 8u * ((uint32_t)irq & 3u);
    REG_WRITE(a, (REG_READ(a) & ~(0xFFu << sh)) | ((byte & 0xFFu) << sh));
}
static void bug_prio_noshift(int irq, uint32_t prio) { ipr_rmw(irq, prio); }          /* << 4 를 잊음 */
static void bug_prio_wordindex(int irq, uint32_t prio) {                               /* IPR[irq] 를 워드로 */
    uint32_t a = NVIC_IPR0 + 4u * (uint32_t)irq, sh = 8u * ((uint32_t)irq & 3u);
    REG_WRITE(a, (REG_READ(a) & ~(0xFFu << sh)) | ((prio << 4) << sh));
}

/* ======================= Part C: TIM2 ISR 과 초기화 ======================= */
static uint32_t g_ticks, g_spurious, g_work;
static uint64_t g_first_entry;
static void isr_mark(void) { if (g_first_entry == UINT64_MAX) g_first_entry = g_cycle; }
static void isr_late_clear(void) {                   /* 흔한 코드: 일하고, 맨 끝에서 지운다 */
    isr_mark();
    uint32_t sr = REG_READ(TIM2_SR);
    trace("ISR: read TIM2_SR", sr);
    if (sr & TIM_UIF) { g_ticks++; cpu_work(g_work); REG_WRITE(TIM2_SR, ~TIM_UIF); }
    else g_spurious++;
}
static void isr_late_clear_dsb(void) {               /* 끝에서 지우고 DSB */
    isr_mark();
    if (REG_READ(TIM2_SR) & TIM_UIF) { g_ticks++; cpu_work(g_work); REG_WRITE(TIM2_SR, ~TIM_UIF); cpu_dsb(); }
    else g_spurious++;
}
static void isr_clear_first(void) {                  /* 먼저 지우고 일한다 (read-back 없음) */
    isr_mark();
    if (REG_READ(TIM2_SR) & TIM_UIF) { REG_WRITE(TIM2_SR, ~TIM_UIF); g_ticks++; cpu_work(g_work); }
    else g_spurious++;
}
/* 할 일: 원인 확인 → 먼저 지운다 → 일(g_work 사이클) → 같은 주변장치 read-back. 원인이 없으면 g_spurious++ */
void tim2_isr_fixed(void) {
    isr_mark();
    /* TODO */
}
static void tim2_irq_init_naive(void) {              /* 흔한 코드: 바로 enable */
    nvic_set_priority(TIM2_IRQn, 6);
    REG_WRITE(TIM2_DIER, TIM_UIE);
    nvic_enable_irq(TIM2_IRQn);
}
static void tim2_irq_init_noreadback(void) {         /* 순서는 맞는데 소스 클리어 도착을 안 기다린다 */
    REG_WRITE(TIM2_SR, ~TIM_UIF);
    nvic_clear_pending(TIM2_IRQn);
    nvic_set_priority(TIM2_IRQn, 6);
    REG_WRITE(TIM2_DIER, TIM_UIE);
    nvic_enable_irq(TIM2_IRQn);
}
/* 할 일: 소스 플래그 지우기 → 도착 확인 → NVIC pending 지우기 → 우선순위 → 소스 인터럽트 enable → NVIC enable */
void tim2_irq_init(uint32_t prio) {
    (void)prio;                                      /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static bool log_is(int i, char op, uint32_t addr, uint32_t val) {
    return i < g_nlog && g_log[i].op == op && g_log[i].addr == addr && g_log[i].val == val;
}
static void log_clear(void) { g_nlog = 0; g_nacc = 0; }

/* sweep: op 의 k 번째 버스 접근 직후 ISR 을 끼운다 (k = 0: op 전). 기대와 다른 k 의 비트마스크 */
typedef struct { mock_fn setup, op, isr; bool (*ok)(void); } race_t;
static uint32_t sweep(const race_t *rc, uint32_t *n_out) {
    board_setup(N_IRQ); rc->setup(); log_clear(); rc->op();
    uint32_t n = g_nacc, bad = 0;
    for (uint32_t k = 0; k <= n; k++) {
        board_setup(N_IRQ); rc->setup(); log_clear();
        if (k == 0) { g_in_isr = true; rc->isr(); g_in_isr = false; }
        else mock_inject_at(k, rc->isr);
        rc->op();
        if (!rc->ok()) bad |= 1u << k;
    }
    if (n_out) *n_out = n;
    return bad;
}
/* race 1: 메인이 IRQ 33 enable, ISR 이 IRQ 40 disable (ICER1 bit 8) */
static void r1_setup(void) { g_nv.en[1] = IRQ_BIT(40); }
static void r1_isr(void)   { REG_WRITE(NVIC_ICER0 + 4u, IRQ_BIT(40)); }
static void r1_bug(void)   { bug_enable_oreq(33); }
static void r1_ok_op(void) { nvic_enable_irq(33); }
static bool r1_ok(void)    { return g_nv.en[1] == IRQ_BIT(33); }
/* race 2: 메인이 IRQ 37 prio 5, ISR 이 같은 워드(IPR9)의 IRQ 38 을 prio 9 로 */
static void r2_setup(void) { }
static void r2_isr(void)   { ipr_rmw(38, 9u << 4); }
static void r2_bug(void)   { ipr_rmw(37, 5u << 4); }
static void r2_ok_op(void) { nvic_set_priority(37, 5); }
static bool r2_ok(void)    { return g_nv.ipr[37] == 0x50u && g_nv.ipr[38] == 0x90u; }

/* Part A 보조 ISR: 진입 중 active/pending 을 드라이버로 읽는다 */
static int g_seen_active = -9, g_seen_pending = -9;
static void isr_probe10(void) { g_seen_active = nvic_get_active(10); g_seen_pending = nvic_get_pending(10); }

/* Part C 실행기 */
typedef struct { uint32_t ticks, spurious, updates, calls, storms; uint64_t first; } run_t;
enum { INIT_CLEAN, INIT_NAIVE, INIT_NORB, INIT_DRIVER };
#define FIRST(r) ((r).first == UINT64_MAX ? -1LL : (long long)(r).first)
#define RUN_CYCLES 20000u
static run_t run_tim2(mock_fn isr, uint32_t core_wb, uint32_t bridge, uint32_t work, int init) {
    board_setup(N_IRQ);
    g_vec[TIM2_IRQn] = isr; g_work = work; g_ticks = g_spurious = 0; g_first_entry = UINT64_MAX;
    g_tim.period = 100u; g_tim.phase = 50u; g_tim.run = true;          /* 업데이트: 사이클 50, 150, 250 ... */
    if (init == INIT_CLEAN) {                                           /* 깨끗한 상태를 모델에 직접 */
        g_tim.dier = TIM_UIE; g_nv.en[0] = IRQ_BIT(TIM2_IRQn); g_nv.ipr[TIM2_IRQn] = 0x60u;
    } else {                                                            /* 앞선 코드가 남긴 상태: UIF=1, UIE=1, */
        g_tim.sr = TIM_UIF; g_tim.dier = TIM_UIE; tim2_line();          /* NVIC 는 disable 인데 pending 래치됨 */
    }
    g_core_wb = core_wb; g_bridge = bridge;
    if (init == INIT_NAIVE) tim2_irq_init_naive();
    else if (init == INIT_NORB) tim2_irq_init_noreadback();
    else if (init == INIT_DRIVER) tim2_irq_init(6);
    while (g_cycle < RUN_CYCLES) sim_step();
    return (run_t){ g_ticks, g_spurious, g_tim.updates, g_isr_calls, g_storms, g_first_entry };
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int unexpected_total = 0;

    /* ---------- Part A: 모델 의미 ---------- */
    board_setup(N_IRQ);
    REG_WRITE(NVIC_ISER0 + 4u, 0x00000021u);                          /* IRQ 32, 37 */
    REG_WRITE(NVIC_ISER0 + 4u, 0u);                                   /* 0 쓰기 = 영향 없음 */
    CHECK(g_nv.en[1] == 0x21u && REG_READ(NVIC_ICER0 + 4u) == 0x21u); /* ICER 를 읽어도 enable 상태 */
    REG_WRITE(NVIC_ICER0 + 4u, 0x00000001u);
    CHECK(g_nv.en[1] == 0x20u);
    REG_WRITE(NVIC_IPR0 + 4u * 9u, 0xFFFFFFFFu);
    CHECK(REG_READ(NVIC_IPR0 + 4u * 9u) == 0xF0F0F0F0u);              /* 하위 4비트는 구현 안 됨 */
    g_nv.act[0] = 0x4u; REG_WRITE(NVIC_IABR0, 0u);
    CHECK(REG_READ(NVIC_IABR0) == 0x4u);                              /* IABR 는 읽기 전용 */
    REG_WRITE(NVIC_STIR, 37u); REG_WRITE(NVIC_STIR, 300u);
    CHECK(g_nv.pend[1] == 0x20u && g_nv.pend[0] == 0u && g_nv.pend[2] == 0u);
    unexpected_total += g_unexpected;

    /* 예시 (2. Problem) */
    board_setup(N_IRQ); log_clear();
    nvic_enable_irq(37); nvic_set_priority(37, 5);
    if (g_nlog == 3) printf("example: enable(37) -> W %08X=%08X ; set_priority(37,5) -> R %08X, W %08X=%08X\n",
           (unsigned)g_log[0].addr, (unsigned)g_log[0].val, (unsigned)g_log[1].addr,
           (unsigned)g_log[2].addr, (unsigned)g_log[2].val);
    CHECK(g_nlog == 3 && log_is(0, 'W', 0xE000E104u, 0x20u) && log_is(2, 'W', 0xE000E424u, 0x5000u));
    CHECK(nvic_get_enable(37) == 1 && nvic_get_priority(37) == 5);

    /* 전수: IRQ 0..95 enable / disable / set_pending / clear_pending — 대상 비트만, 쓰기 1 번, 읽기 0 번 */
    uint32_t bad_en = 0, bad_dis = 0, bad_sp = 0, bad_cp = 0, bad_get = 0;
    board_setup(N_IRQ);
    for (int irq = 0; irq < N_IRQ; irq++) {
        uint32_t w = IRQ_WORD(irq), b = IRQ_BIT(irq), before[N_WORDS];
        for (uint32_t i = 0; i < N_WORDS; i++) before[i] = g_nv.en[i] = xs32();
        log_clear(); nvic_enable_irq(irq);
        for (uint32_t i = 0; i < N_WORDS; i++) if (g_nv.en[i] != (i == w ? before[i] | b : before[i])) bad_en++;
        if (!(g_nlog == 1 && log_is(0, 'W', NVIC_ISER0 + 4u * w, b))) bad_en++;
        for (uint32_t i = 0; i < N_WORDS; i++) before[i] = g_nv.en[i] = xs32();
        log_clear(); nvic_disable_irq(irq);
        for (uint32_t i = 0; i < N_WORDS; i++) if (g_nv.en[i] != (i == w ? before[i] & ~b : before[i])) bad_dis++;
        if (!(g_nlog == 1 && log_is(0, 'W', NVIC_ICER0 + 4u * w, b))) bad_dis++;
        for (uint32_t i = 0; i < N_WORDS; i++) before[i] = g_nv.pend[i] = xs32();
        log_clear(); nvic_set_pending(irq);
        for (uint32_t i = 0; i < N_WORDS; i++) if (g_nv.pend[i] != (i == w ? before[i] | b : before[i])) bad_sp++;
        if (!(g_nlog == 1 && log_is(0, 'W', NVIC_ISPR0 + 4u * w, b))) bad_sp++;
        for (uint32_t i = 0; i < N_WORDS; i++) before[i] = g_nv.pend[i] = xs32();
        log_clear(); nvic_clear_pending(irq);
        for (uint32_t i = 0; i < N_WORDS; i++) if (g_nv.pend[i] != (i == w ? before[i] & ~b : before[i])) bad_cp++;
        if (!(g_nlog == 1 && log_is(0, 'W', NVIC_ICPR0 + 4u * w, b))) bad_cp++;
        for (uint32_t i = 0; i < N_WORDS; i++) { g_nv.en[i] = xs32(); g_nv.pend[i] = xs32(); g_nv.act[i] = xs32(); }
        if (nvic_get_enable(irq) != ((g_nv.en[w] & b) ? 1 : 0)) bad_get++;
        if (nvic_get_pending(irq) != ((g_nv.pend[w] & b) ? 1 : 0)) bad_get++;
        if (nvic_get_active(irq) != ((g_nv.act[w] & b) ? 1 : 0)) bad_get++;
    }
    CHECK(bad_en == 0u);
    CHECK(bad_dis == 0u);
    CHECK(bad_sp == 0u);
    CHECK(bad_cp == 0u);
    CHECK(bad_get == 0u);
    /* 전수: IRQ 0..95 × prio 0..15 — 대상 바이트만 바뀌고 읽으면 prio, 로그는 R/W 같은 IPR 워드 */
    uint32_t bad_prio = 0, n_prio = 0;
    for (int irq = 0; irq < N_IRQ; irq++)
        for (uint32_t p = 0; p < 16u; p++) {
            uint8_t before[N_IRQ];
            for (int i = 0; i < N_IRQ; i++) before[i] = g_nv.ipr[i] = (uint8_t)(xs32() & PRIO_IMPL_MASK);
            log_clear();
            int rc = nvic_set_priority(irq, p);
            uint32_t wa = NVIC_IPR0 + 4u * ((uint32_t)irq >> 2);
            if (rc != 0 || g_nlog != 2 || g_log[0].op != 'R' || g_log[0].addr != wa ||
                g_log[1].op != 'W' || g_log[1].addr != wa) bad_prio++;
            for (int i = 0; i < N_IRQ; i++)
                if (g_nv.ipr[i] != (i == irq ? (uint8_t)(p << 4) : before[i])) bad_prio++;
            if (nvic_get_priority(irq) != (int)p) bad_prio++;
            n_prio++;
        }
    printf("exhaustive: enable/disable/pend/unpend/get x %d IRQs, priority %u cases\n", N_IRQ, (unsigned)n_prio);
    CHECK(bad_prio == 0u && n_prio == 1536u);
    unexpected_total += g_unexpected;

    /* 무작위 5000 연산 vs 그림자 모델 */
    board_setup(N_IRQ);
    uint32_t sh_en[N_WORDS] = {0}, sh_pd[N_WORDS] = {0}, bad_rand = 0;
    uint8_t sh_pr[N_IRQ] = {0};
    for (int t = 0; t < 5000; t++) {
        uint32_t r = xs32(); int irq = (int)((r >> 8) % N_IRQ); uint32_t w = IRQ_WORD(irq), b = IRQ_BIT(irq);
        switch (r % 7u) {
        case 0: nvic_enable_irq(irq);  sh_en[w] |= b;  break;
        case 1: nvic_disable_irq(irq); sh_en[w] &= ~b; break;
        case 2: nvic_set_pending(irq); sh_pd[w] |= b;  break;
        case 3: nvic_clear_pending(irq); sh_pd[w] &= ~b; break;
        case 4: { uint32_t p = (r >> 20) & 15u; nvic_set_priority(irq, p); sh_pr[irq] = (uint8_t)(p << 4); } break;
        case 5: if (nvic_get_enable(irq) != ((sh_en[w] & b) ? 1 : 0)) bad_rand++; break;
        default: if (nvic_get_priority(irq) != (sh_pr[irq] >> 4)) bad_rand++; break;
        }
    }
    for (uint32_t i = 0; i < N_WORDS; i++) if (g_nv.en[i] != sh_en[i] || g_nv.pend[i] != sh_pd[i]) bad_rand++;
    if (memcmp(g_nv.ipr, sh_pr, sizeof sh_pr) != 0) bad_rand++;
    CHECK(bad_rand == 0u);
    unexpected_total += g_unexpected;

    /* pending 은 disable 이어도 래치된다 → enable 하는 순간 ISR. 진입 중엔 active=1, pending=0 */
    board_setup(N_IRQ); g_vec[10] = isr_probe10;
    nvic_set_pending(10); sim_step();
    CHECK(g_isr_calls == 0u && nvic_get_pending(10) == 1);
    nvic_enable_irq(10); sim_step();
    CHECK(g_isr_calls == 1u && g_seen_active == 1 && g_seen_pending == 0);
    CHECK(nvic_get_active(10) == 0 && nvic_get_pending(10) == 0);
    nvic_trigger_stir(10); sim_step();
    CHECK(g_isr_calls == 2u);
    unexpected_total += g_unexpected;

    /* ---------- Part B: 시스템 예외 → SHPR ---------- */
    const int sys_ok[] = { MemoryManagement_IRQn, BusFault_IRQn, UsageFault_IRQn, SVCall_IRQn,
                           DebugMonitor_IRQn, PendSV_IRQn, SysTick_IRQn };
    uint32_t bad_sys = 0;
    board_setup(N_IRQ);
    for (unsigned k = 0; k < sizeof sys_ok / sizeof sys_ok[0]; k++)
        for (uint32_t p = 0; p < 16u; p++) {
            uint8_t before[12];
            for (uint32_t i = 0; i < 12u; i++)
                before[i] = g_nv.shp[i] = shp_implemented(4u + i) ? (uint8_t)(xs32() & PRIO_IMPL_MASK) : 0u;
            uint8_t ipr_before[N_IRQ]; memcpy(ipr_before, g_nv.ipr, sizeof ipr_before);
            uint32_t idx = (uint32_t)(16 + sys_ok[k]) - 4u;
            if (nvic_set_priority(sys_ok[k], p) != 0) bad_sys++;
            for (uint32_t i = 0; i < 12u; i++) if (g_nv.shp[i] != (i == idx ? (uint8_t)(p << 4) : before[i])) bad_sys++;
            if (memcmp(ipr_before, g_nv.ipr, sizeof ipr_before) != 0) bad_sys++;
            if (nvic_get_priority(sys_ok[k]) != (int)p) bad_sys++;
        }
    CHECK(bad_sys == 0u);
    uint32_t a = 0, s = 0;
    CHECK(prio_loc(SysTick_IRQn, &a, &s) == 0 && a == 0xE000ED20u && s == 24u);
    CHECK(prio_loc(PendSV_IRQn, &a, &s) == 0 && a == 0xE000ED20u && s == 16u);
    CHECK(prio_loc(SVCall_IRQn, &a, &s) == 0 && a == 0xE000ED1Cu && s == 24u);
    CHECK(prio_loc(MemoryManagement_IRQn, &a, &s) == 0 && a == 0xE000ED18u && s == 0u);
    board_setup(N_IRQ);
    nvic_set_priority(SysTick_IRQn, 15); nvic_set_priority(PendSV_IRQn, 15);
    CHECK(REG_READ(0xE000ED20u) == 0xF0F00000u);
    /* 잘못된 번호 / 값: −1, 버스 접근 0 번 */
    const int bad_ids[] = { N_IRQ, 200, 1000, HardFault_IRQn, NonMaskableInt_IRQn, -3, -6, -7, -8, -9, -15, -16, -100 };
    uint32_t bad_rej = 0;
    for (unsigned k = 0; k < sizeof bad_ids / sizeof bad_ids[0]; k++) {
        log_clear();
        if (nvic_set_priority(bad_ids[k], 3) != -1 || nvic_get_priority(bad_ids[k]) != -1) bad_rej++;
        if (nvic_enable_irq(bad_ids[k]) != -1 || nvic_set_pending(bad_ids[k]) != -1) bad_rej++;
        if (g_nacc != 0u) bad_rej++;
    }
    log_clear();
    if (nvic_set_priority(5, 16) != -1 || nvic_enable_irq(SysTick_IRQn) != -1 || g_nacc != 0u) bad_rej++;
    CHECK(bad_rej == 0u);
    unexpected_total += g_unexpected;

    /* 버그 1: ICER |= bit → 읽은 enable 비트 전부에 1 을 써서 워드 전체 disable */
    board_setup(N_IRQ);
    g_nv.en[1] = IRQ_BIT(32) | IRQ_BIT(37) | IRQ_BIT(40) | IRQ_BIT(63);
    bug_disable_oreq(37);
    CHECK(g_nv.en[1] == 0u);
    g_nv.en[1] = IRQ_BIT(32) | IRQ_BIT(37) | IRQ_BIT(40) | IRQ_BIT(63);
    nvic_disable_irq(37);
    CHECK(g_nv.en[1] == (IRQ_BIT(32) | IRQ_BIT(40) | IRQ_BIT(63)));
    /* 버그 2: 시프트 없는 prio 5 → 바이트 0x05 → 구현 안 된 비트 → 읽으면 0 = 가장 급한 우선순위 */
    board_setup(N_IRQ);
    nvic_set_priority(37, 9);
    bug_prio_noshift(37, 5);
    CHECK(g_nv.ipr[37] == 0x00u && nvic_get_priority(37) == 0);
    /* 버그 3: IRQ 번호를 워드 인덱스로 — enable(33) 이 ICER1 에 써서 33 을 끄고, enable(37) 은 없는 주소 */
    board_setup(N_IRQ);
    g_nv.en[1] = IRQ_BIT(33);
    bug_enable_wordindex(33);
    CHECK(g_nv.en[1] == 0u && g_unexpected == 0);
    bug_enable_wordindex(37);
    CHECK(g_unexpected == 1 && g_nv.en[1] == 0u);
    /* 버그 4: IPR[irq] 를 워드로 — prio(5, 3) 이 IRQ 21 (IPR5 레인 1) 을 바꾼다 */
    board_setup(N_IRQ);
    bug_prio_wordindex(5, 3);
    CHECK(g_nv.ipr[21] == 0x30u && g_nv.ipr[5] == 0x00u);
    /* 레이스: ISER |= 는 ISR 이 방금 끈 IRQ 를 되살린다 / 보호 없는 IPR 워드 RMW 는 ISR 의 이웃 바이트를 잃는다 */
    uint32_t n = 0;
    race_t r1b = { r1_setup, r1_bug, r1_isr, r1_ok }, r1g = { r1_setup, r1_ok_op, r1_isr, r1_ok };
    race_t r2b = { r2_setup, r2_bug, r2_isr, r2_ok }, r2g = { r2_setup, r2_ok_op, r2_isr, r2_ok };
    uint32_t m1b = sweep(&r1b, &n);  uint32_t n1b = n;
    uint32_t m1g = sweep(&r1g, &n);  uint32_t n1g = n;
    uint32_t m2b = sweep(&r2b, &n);  uint32_t n2b = n;
    uint32_t m2g = sweep(&r2g, &n);  uint32_t n2g = n;
    printf("sweep ISER|=: lost-at-k mask=0x%X (n=%u)  ISER=bit: 0x%X (n=%u)\n",
           (unsigned)m1b, (unsigned)n1b, (unsigned)m1g, (unsigned)n1g);
    printf("sweep IPR RMW: unprotected 0x%X (n=%u)  nvic_set_priority: 0x%X (n=%u)\n",
           (unsigned)m2b, (unsigned)n2b, (unsigned)m2g, (unsigned)n2g);
    CHECK(m1b == 0x2u && n1b == 2u);
    CHECK(m1g == 0u && n1g == 1u);
    CHECK(m2b == 0x2u && n2b == 2u);
    CHECK(m2g == 0u && n2g == 2u);
    /* ICPR 를 써도 소스 라인이 high 면 pending 이 바로 다시 선다 */
    board_setup(N_IRQ);
    g_tim.sr = TIM_UIF; g_tim.dier = TIM_UIE; tim2_line();
    nvic_clear_pending(TIM2_IRQn);
    CHECK(nvic_get_pending(TIM2_IRQn) == 1);
    REG_WRITE(TIM2_SR, ~TIM_UIF); nvic_clear_pending(TIM2_IRQn);
    CHECK(nvic_get_pending(TIM2_IRQn) == 0);
    unexpected_total += g_unexpected;

    /* ---------- Part C: 늦은 플래그 클리어 ---------- */
    static const uint32_t lat[][2] = { {0, 0}, {1, 0}, {2, 0}, {1, 3}, {2, 6} };   /* {코어 쓰기 버퍼, 브리지} */
    static const struct { const char *name; mock_fn isr; uint32_t work; } var[] = {
        { "late clear (W=20)",       isr_late_clear,     20 },
        { "late clear + DSB (W=20)", isr_late_clear_dsb, 20 },
        { "clear first (W=20)",      isr_clear_first,    20 },
        { "clear first (W=2)",       isr_clear_first,     2 },
        { "fixed: first+readback W20", tim2_isr_fixed,   20 },
        { "fixed: first+readback W2",  tim2_isr_fixed,    2 },
    };
    run_t tab[6][5];
    printf("spurious entries per 200 updates   lat{wb,bridge}: {0,0} {1,0} {2,0} {1,3} {2,6}\n");
    for (int v = 0; v < 6; v++) {
        printf("  %-27s", var[v].name);
        for (int l = 0; l < 5; l++) {
            tab[v][l] = run_tim2(var[v].isr, lat[l][0], lat[l][1], var[v].work, INIT_CLEAN);
            unexpected_total += g_unexpected;
            if (tab[v][l].storms) printf(" storm");
            else printf(" %5u", (unsigned)tab[v][l].spurious);
        }
        printf("\n");
    }
    bool c_late = true, c_dsb = true, c_cf = true, c_fix = true;
    for (int l = 0; l < 5; l++) {
        uint32_t L = lat[l][0] + lat[l][1];
        for (int v = 0; v < 6; v++) if (tab[v][l].updates != 200u) c_late = false;
        if (tab[0][l].ticks != 200u || tab[0][l].spurious != (L > 0u ? 200u : 0u)) c_late = false;
        if (tab[1][l].ticks != 200u || tab[1][l].spurious != (lat[l][1] > 0u ? 200u : 0u)) c_dsb = false;
        if (tab[2][l].spurious != (L > 20u ? 200u : 0u) || tab[3][l].spurious != (L > 2u ? 200u : 0u)) c_cf = false;
        for (int v = 4; v < 6; v++)
            if (tab[v][l].ticks != 200u || tab[v][l].spurious != 0u || tab[v][l].storms != 0u) c_fix = false;
    }
    CHECK(c_late);        /* 지연이 있으면 매번 한 번 더 들어온다 */
    CHECK(c_dsb);         /* DSB 는 코어 쪽만 — 브리지가 쓰기를 붙잡으면 소용없다 (모델) */
    CHECK(c_cf);          /* 먼저 지우기는 "남은 일 ≥ 지연" 일 때만 */
    CHECK(c_fix);         /* 먼저 지우기 + read-back 은 지연과 무관 */
    /* 초기화 순서: 앞선 코드가 남긴 UIF + 래치된 NVIC pending */
    run_t ni = run_tim2(tim2_isr_fixed, 1, 3, 20, INIT_NAIVE);    unexpected_total += g_unexpected;
    run_t nr = run_tim2(tim2_isr_fixed, 1, 3, 20, INIT_NORB);     unexpected_total += g_unexpected;
    run_t dr = run_tim2(tim2_isr_fixed, 1, 3, 20, INIT_DRIVER);   unexpected_total += g_unexpected;
    run_t d2 = run_tim2(tim2_isr_fixed, 2, 6, 2, INIT_DRIVER);    unexpected_total += g_unexpected;
    printf("init naive      : first ISR @%lld ticks=%u updates=%u spurious=%u\n", FIRST(ni),
           (unsigned)ni.ticks, (unsigned)ni.updates, (unsigned)ni.spurious);
    printf("init no readback: first ISR @%lld ticks=%u updates=%u spurious=%u\n", FIRST(nr),
           (unsigned)nr.ticks, (unsigned)nr.updates, (unsigned)nr.spurious);
    printf("tim2_irq_init   : first ISR @%lld ticks=%u updates=%u spurious=%u\n", FIRST(dr),
           (unsigned)dr.ticks, (unsigned)dr.updates, (unsigned)dr.spurious);
    CHECK(ni.first < 50u && ni.ticks == ni.updates + 1u);          /* 남은 UIF 가 가짜 첫 틱 */
    CHECK(nr.spurious == 1u && nr.ticks == nr.updates);            /* ICPR 가 소스 클리어보다 먼저 도착 */
    CHECK(dr.first >= 50u && dr.ticks == dr.updates && dr.spurious == 0u && dr.updates == 200u);
    CHECK(d2.ticks == d2.updates && d2.spurious == 0u && d2.storms == 0u);
    CHECK(g_nv.ipr[TIM2_IRQn] == 0x60u && (g_nv.en[0] & IRQ_BIT(TIM2_IRQn)) != 0u);

    /* 브링업 로그: late clear, lat {1,3}, 첫 업데이트 하나 */
    if (g_verbose) {
        board_setup(N_IRQ);
        g_vec[TIM2_IRQn] = isr_late_clear; g_work = 20; g_ticks = g_spurious = 0; g_first_entry = UINT64_MAX;
        g_tim.period = 100u; g_tim.phase = 50u; g_tim.run = true; g_tim.dier = TIM_UIE;
        g_nv.en[0] = IRQ_BIT(TIM2_IRQn); g_core_wb = 1; g_bridge = 3; g_tron = true;
        while (g_cycle < 120u) sim_step();
        printf("trace (late clear, lat {1,3}):\n");
        for (int i = 0; i < g_ntr; i++)
            printf(g_tr[i].v > 0xFFFFu ? "  cyc %3llu  %-57s 0x%08X\n" : "  cyc %3llu  %-57s %u\n",
                   (unsigned long long)g_tr[i].cyc, g_tr[i].ev, (unsigned)g_tr[i].v);
        printf("  -> ticks=%u spurious=%u isr_calls=%u\n", (unsigned)g_ticks, (unsigned)g_spurious, (unsigned)g_isr_calls);
    }
    CHECK(unexpected_total == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
