/* BM-M06 입력 캡처: 주파수·펄스폭 측정과 오버플로 처리
 *
 * Part A — 캡처 메커니즘: mock TIM3 (16비트 CNT + PSC) 의 TI1 핀에 (시각, 레벨) 이벤트 목록을 넣으면
 *          에지 순간의 CNT 가 CCR1/CCR2 로 복사되고 CC1IF/CC2IF 가 선다 (이미 1 이면 CC1OF/CC2OF).
 *          드라이버는 overflow(UIF) 횟수로 16비트 캡처를 32비트 타임스탬프로 확장한다.
 * Part B — 경계: 캡처와 업데이트가 같은 ISR 진입에서 둘 다 pending → CCR 값의 크기로 순서를 판정.
 *          아주 낮은 주파수(수천 번 overflow), 오버캡처, CCR→SR 읽기 순서, 두 채널 순서, 신호 끊김.
 * Part C — 펌웨어: ic_init() 순서, ISR → seqlock 으로 main 에 측정값 전달, 이벤트 구동 타코미터 로그.
 *
 * 시뮬레이션 규약 (BM-M 공통): 주변장치 모델이 irq_line_set() 으로 선을 올리고, sim_step() 이 타이머 클럭
 * 1개를 진행한 뒤 선이 enable+pending 이고 마스크/실행중이 아니면 핸들러를 동기 호출한다 (NVIC-lite).
 * 레지스터 주소·비트는 STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 파일의 mock 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    (-v: 표와 로그)
 * 타깃: UNIT_TEST 를 빼면 REG_READ/REG_WRITE 가 volatile MMIO 가 된다 (드라이버 코드는 그대로).
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- MMIO seam (BM-B02) ---------------- */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* TIM3 (16비트 범용 타이머, APB1, F4 에서 IRQ 29) — RM0090 기준 대표값, RM 확인 권장 */
#define TIM3_BASE      0x40000400u
#define TIM3_IRQn      29
#define TIM_CR1        (TIM3_BASE + 0x00u)
#define TIM_SMCR       (TIM3_BASE + 0x08u)
#define TIM_DIER       (TIM3_BASE + 0x0Cu)
#define TIM_SR         (TIM3_BASE + 0x10u)
#define TIM_EGR        (TIM3_BASE + 0x14u)
#define TIM_CCMR1      (TIM3_BASE + 0x18u)
#define TIM_CCER       (TIM3_BASE + 0x20u)
#define TIM_CNT        (TIM3_BASE + 0x24u)
#define TIM_PSC        (TIM3_BASE + 0x28u)
#define TIM_ARR        (TIM3_BASE + 0x2Cu)
#define TIM_CCR1       (TIM3_BASE + 0x34u)
#define TIM_CCR2       (TIM3_BASE + 0x38u)
#define CR1_CEN        (1u << 0)
#define CR1_URS        (1u << 2)    /* 1: overflow 만 UIF (UG/슬레이브 리셋은 안 세움) */
#define CR1_ARPE       (1u << 7)
#define SMCR_SMS_RESET 4u           /* SMS[2:0] = 100: 트리거 상승 에지에서 카운터 리셋 */
#define SMCR_TS_TI1FP1 (5u << 4)    /* TS[6:4] = 101 */
#define DIER_UIE       (1u << 0)
#define DIER_CC1IE     (1u << 1)
#define DIER_CC2IE     (1u << 2)
#define SR_UIF         (1u << 0)    /* SR 은 rc_w0: 0 을 쓰면 지워지고 1 을 쓰면 그대로 */
#define SR_CC1IF       (1u << 1)    /* 입력 캡처: CCR1 을 읽어도 지워진다 */
#define SR_CC2IF       (1u << 2)
#define SR_CC1OF       (1u << 9)    /* over-capture: CC1IF 가 1 인데 또 캡처됨 */
#define SR_CC2OF       (1u << 10)
#define SR_IMPL        (SR_UIF | SR_CC1IF | SR_CC2IF | SR_CC1OF | SR_CC2OF)
#define EGR_UG         (1u << 0)
#define CCMR1_CC1S_TI1 (1u << 0)    /* CC1S[1:0] = 01: IC1 ← TI1 */
#define CCMR1_IC1PSC(n) ((uint32_t)(n) << 2)   /* 00 /1, 01 /2, 10 /4, 11 /8 */
#define CCMR1_CC2S_TI1 (2u << 8)    /* CC2S[9:8] = 10: IC2 ← TI1 (같은 핀을 두 번째 채널로) */
#define CCER_CC1E      (1u << 0)
#define CCER_CC1P      (1u << 1)    /* 입력: CC1NP:CC1P = 00 상승, 01 하강, 11 양쪽 */
#define CCER_CC1NP     (1u << 3)
#define CCER_CC2E      (1u << 4)
#define CCER_CC2P      (1u << 5)
#define CCER_CC2NP     (1u << 7)

/* ======================= mock 코어 (BM-B02 mock 축약판) + 접근 횟수 훅 (BM-B04 식) ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 16
#define MOCK_LOG_CAP  64
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int g_nregs;
static mock_log_t g_log[MOCK_LOG_CAP];
static int g_nlog, g_unexpected;
static uint32_t g_access_no;               /* 레지스터 접근 일련번호 */
static uint32_t g_hook_at;                 /* 이 번호의 접근 "직전" 에 g_hook_fn 실행 (1회) */
static void (*g_hook_fn)(void);

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
static void mock_access(char op, uint32_t addr, uint32_t val) {
    g_access_no++;
    if (g_hook_fn && g_access_no == g_hook_at) { void (*f)(void) = g_hook_fn; g_hook_fn = NULL; f(); }
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS || mock_find(addr)) return -1;
    g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return 0;
}
void mock_reset(void) { g_nregs = 0; g_nlog = 0; g_unexpected = 0; g_access_no = 0; g_hook_fn = NULL; }
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; return 0u; }
    mock_access('R', addr, g->val);
    return g->r ? g->r(addr, g->val, g->ctx) : g->val;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; return; }
    mock_access('W', addr, val);
    g->val = g->w ? g->w(addr, g->val, val, g->ctx) : val;
}
static uint32_t *hw_reg(uint32_t addr) { mock_reg_t *g = mock_find(addr); return g ? &g->val : NULL; }

/* ======================= NVIC-lite (BM-M01 의 축약) ======================= */
typedef struct { bool enabled, level; void (*handler)(void); uint32_t calls; } irq_line_t;
#define NVIC_LINES 64
static irq_line_t g_irq[NVIC_LINES];
static uint64_t g_now;                     /* 시뮬레이션 시각 [타이머 클럭] */
static uint64_t g_mask_until;              /* 이 시각 전엔 전달 안 함 (더 높은 우선순위 ISR / 임계 구역 흉내) */
static uint64_t g_busy_until;              /* ISR 실행 시간 흉내: 진입 후 g_isr_cost 클럭 동안 재진입 없음 */
static uint32_t g_isr_cost;
static void irq_line_set(int n, bool level) { g_irq[n].level = level; }
void nvic_enable_irq(int n) { g_irq[n].enabled = true; }      /* CMSIS NVIC_EnableIRQ 자리 */

/* ======================= 입력 신호: TI1 핀의 (시각, 레벨) 이벤트 목록 ======================= */
typedef struct { uint64_t t; uint8_t level; } in_ev_t;
#define IN_CAP 8192
static in_ev_t g_in[IN_CAP];
static int g_nin, g_iin;
static uint8_t g_level;                    /* 현재 TI1 레벨 */
static int in_add(uint64_t t, int level) {
    if (g_nin >= IN_CAP || t <= g_now || (g_nin > 0 && t <= g_in[g_nin - 1].t)) return -1;
    g_in[g_nin++] = (in_ev_t){ t, (uint8_t)(level != 0) };
    return 0;
}
static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1u) / b; }
/* 주기 F/T 클럭인 사각파 n 주기 (F = 1000·f_tim, T = 신호 주파수 mHz). high 폭 = H/T 클럭, H = F·duty_bp/10000.
 * k번째 상승 = t0 + ceil((ph + kF)/T), 하강 = t0 + ceil((ph + kF + H)/T): 핀 변화는 다음 클럭 경계에서 보인다. */
static int in_add_pwm(uint64_t t0, uint64_t F, uint64_t T, uint64_t ph, uint32_t duty_bp, uint32_t n) {
    uint64_t H = F * duty_bp / 10000u;
    for (uint32_t k = 0; k < n; k++) {
        if (in_add(t0 + ceil_div(ph + k * F, T), 1)) return -1;
        if (in_add(t0 + ceil_div(ph + k * F + H, T), 0)) return -1;
    }
    return 0;
}

/* ======================= 타이머 모델 (BM-B14 타이머 모델 축약판 + 입력 캡처) ======================= */
typedef struct {
    uint32_t *cr1, *smcr, *dier, *sr, *ccmr1, *ccer, *cnt, *psc, *arr, *ccr[2];
    uint32_t psc_sh, arr_sh, pc;   /* shadow 와 프리스케일러 카운터 */
    uint32_t icps[2];              /* 입력 캡처 프리스케일러(ICxPSC) 의 에지 카운터 */
    uint32_t uev, cap[2];          /* 통계: update event, 실제 캡처 수 */
} tim_model_t;
static tim_model_t g_tim;

static void tim_update_event(bool overflow) {   /* overflow / UG / 슬레이브 리셋 공통 */
    g_tim.psc_sh = *g_tim.psc;
    g_tim.arr_sh = *g_tim.arr;
    *g_tim.cnt = 0u;
    g_tim.pc = 0u;
    g_tim.uev++;
    if (overflow || !(*g_tim.cr1 & CR1_URS)) *g_tim.sr |= SR_UIF;
}

/* TODO A1: 채널 ch(0 = CH1, 1 = CH2) 의 입력 캡처 이벤트 하나 (극성·핀 매핑은 호출자가 이미 확인).
 *  - 입력 캡처 프리스케일러: div = 1 << ICxPSC (CCMR1 의 [3:2] / [11:10]). icps[ch] 를 세서
 *    div 번째 이벤트마다 한 번만 캡처한다 (그때 icps[ch] = 0).
 *  - 캡처: CCxIF 가 이미 1 이면 CCxOF 를 세우고, CCRx = CNT, CCxIF = 1, cap[ch]++. */
static void ic_capture_edge(int ch) {
    (void)ch;                                   /* TODO A1 */
}

/* 주어짐: TI1 핀 에지 → 채널별 극성 검사 → 캡처, 그리고 슬레이브 리셋 모드(연습 1, PWM 입력) */
static bool ic_polarity_match(int ch, bool rising) {
    uint32_t p = (*g_tim.ccer >> (1 + 4 * ch)) & 1u, np = (*g_tim.ccer >> (3 + 4 * ch)) & 1u;
    if (p && np) return true;           /* 양쪽 에지 */
    return p ? !rising : rising;
}
static void tim_edge(bool rising) {
    for (int ch = 0; ch < 2; ch++) {
        uint32_t ccs = (*g_tim.ccmr1 >> (8 * ch)) & 3u;
        bool from_ti1 = (ch == 0 && ccs == 1u) || (ch == 1 && ccs == 2u);
        if ((*g_tim.ccer & (CCER_CC1E << (4 * ch))) && from_ti1 && ic_polarity_match(ch, rising))
            ic_capture_edge(ch);
    }
    if ((*g_tim.smcr & 7u) == SMCR_SMS_RESET && (*g_tim.smcr & (7u << 4)) == SMCR_TS_TI1FP1 &&
        ic_polarity_match(0, rising))
        tim_update_event(false);        /* TI1FP1 에지: 캡처 다음에 카운터 리셋 (URS=1 이면 UIF 없음) */
}
static void tim_set_level(uint8_t level) {
    if (level != g_level) { g_level = level; tim_edge(level != 0); }
}
static void tim_count_once(void) {
    if (!(*g_tim.cr1 & CR1_CEN) || g_tim.arr_sh == 0u) return;
    if (g_tim.pc >= g_tim.psc_sh) {
        g_tim.pc = 0u;
        if (*g_tim.cnt == g_tim.arr_sh) tim_update_event(true);
        else *g_tim.cnt = (*g_tim.cnt + 1u) & 0xFFFFu;
    } else {
        g_tim.pc++;
    }
}
static void tim_irq_eval(void) {
    irq_line_set(TIM3_IRQn, (*g_tim.sr & *g_tim.dier & (SR_UIF | SR_CC1IF | SR_CC2IF)) != 0u);
}

/* 레지스터 접근 규칙 (훅) */
static int k_ch[2] = { 0, 1 };
static uint32_t on_w_sr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)c; return o & v & SR_IMPL; }
static uint32_t on_w_16(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)o; (void)c; return v & 0xFFFFu; }
static uint32_t on_w_arr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    if (!(*g_tim.cr1 & CR1_ARPE)) g_tim.arr_sh = v & 0xFFFFu;   /* ARPE=0: 즉시 */
    return v & 0xFFFFu;
}
static uint32_t on_w_egr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    if (v & EGR_UG) tim_update_event(false);
    return 0u;                                                  /* EGR 은 쓰기 전용 */
}
static uint32_t on_w_ccer(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    for (int ch = 0; ch < 2; ch++)
        if (!(v & (CCER_CC1E << (4 * ch)))) g_tim.icps[ch] = 0u;   /* CCxE=0 이면 IC 프리스케일러 리셋 */
    return v & 0xBBu;
}
static bool ccr_is_input(int ch) { return ((*g_tim.ccmr1 >> (8 * ch)) & 3u) != 0u; }
static uint32_t on_r_ccr(uint32_t a, uint32_t s, void *c) {
    (void)a;
    int ch = *(int *)c;
    if (ccr_is_input(ch)) *g_tim.sr &= ~(ch ? SR_CC2IF : SR_CC1IF);   /* 입력 캡처: CCR 읽기가 CCxIF 를 지운다 */
    return s;
}
static uint32_t on_w_ccr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a;
    return ccr_is_input(*(int *)c) ? o : (v & 0xFFFFu);              /* 입력 모드에서는 읽기 전용 */
}

static void tim_model_init(void) {
    mock_reset();
    memset(&g_tim, 0, sizeof g_tim);
    mock_add_reg(TIM_CR1, 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_SMCR, 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_DIER, 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_SR, 0u, NULL, on_w_sr, NULL);
    mock_add_reg(TIM_EGR, 0u, NULL, on_w_egr, NULL);
    mock_add_reg(TIM_CCMR1, 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_CCER, 0u, NULL, on_w_ccer, NULL);
    mock_add_reg(TIM_CNT, 0u, NULL, on_w_16, NULL);
    mock_add_reg(TIM_PSC, 0u, NULL, on_w_16, NULL);
    mock_add_reg(TIM_ARR, 0xFFFFu, NULL, on_w_arr, NULL);
    mock_add_reg(TIM_CCR1, 0u, on_r_ccr, on_w_ccr, &k_ch[0]);
    mock_add_reg(TIM_CCR2, 0u, on_r_ccr, on_w_ccr, &k_ch[1]);
    g_tim.cr1 = hw_reg(TIM_CR1);   g_tim.smcr = hw_reg(TIM_SMCR); g_tim.dier = hw_reg(TIM_DIER);
    g_tim.sr = hw_reg(TIM_SR);     g_tim.ccmr1 = hw_reg(TIM_CCMR1); g_tim.ccer = hw_reg(TIM_CCER);
    g_tim.cnt = hw_reg(TIM_CNT);   g_tim.psc = hw_reg(TIM_PSC);   g_tim.arr = hw_reg(TIM_ARR);
    g_tim.ccr[0] = hw_reg(TIM_CCR1); g_tim.ccr[1] = hw_reg(TIM_CCR2);
    g_tim.arr_sh = 0xFFFFu;
}

/* ======================= 시뮬레이터: sim_step() 과 이벤트 구동 빨리감기 ======================= */
static bool g_ff = true;                   /* false: 매 클럭 sim_step (빨리감기 검증용) */
static uint64_t g_steps;                   /* 실제로 실행한 sim_step 수 */

static void sim_tick_hw(void) {            /* 하드웨어만 1 클럭: 카운터 → 그 클럭의 핀 에지(캡처) */
    g_now++;
    tim_count_once();
    while (g_iin < g_nin && g_in[g_iin].t <= g_now) tim_set_level(g_in[g_iin++].level);
}
static void nvic_try_deliver(void) {
    for (int n = 0; n < NVIC_LINES; n++) {
        irq_line_t *l = &g_irq[n];
        if (!l->enabled || !l->level || !l->handler || g_now < g_mask_until || g_now < g_busy_until) continue;
        l->calls++;
        l->handler();                      /* 동기 호출: 핸들러 안의 레지스터 접근은 이 시각에 일어난다 */
        g_busy_until = g_now + g_isr_cost;
        tim_irq_eval();
        return;                            /* 클럭당 최대 1회 진입 */
    }
}
static void sim_step(void) {
    g_steps++;
    sim_tick_hw();
    tim_irq_eval();
    nvic_try_deliver();
}
/* 다음 overflow 가 몇 클럭 뒤인가 (카운터가 멈춰 있으면 UINT64_MAX) */
static uint64_t tim_clocks_to_ovf(void) {
    if (!(*g_tim.cr1 & CR1_CEN) || g_tim.arr_sh == 0u) return UINT64_MAX;
    uint64_t p = (uint64_t)g_tim.psc_sh + 1u;
    uint64_t first = g_tim.pc >= g_tim.psc_sh ? 1u : (uint64_t)(g_tim.psc_sh - g_tim.pc) + 1u;
    return first + (uint64_t)((g_tim.arr_sh - *g_tim.cnt) & 0xFFFFu) * p;
}
/* overflow·에지가 없는 j 클럭을 한 번에 진행 (j < tim_clocks_to_ovf()) */
static void tim_skip(uint64_t j) {
    if (!(*g_tim.cr1 & CR1_CEN) || g_tim.arr_sh == 0u || j == 0u) return;
    uint64_t p = (uint64_t)g_tim.psc_sh + 1u;
    uint64_t first = g_tim.pc >= g_tim.psc_sh ? 1u : (uint64_t)(g_tim.psc_sh - g_tim.pc) + 1u;
    if (j < first) { g_tim.pc += (uint32_t)j; return; }
    uint64_t rest = j - first;
    *g_tim.cnt = (uint32_t)((*g_tim.cnt + 1u + rest / p) & 0xFFFFu);
    g_tim.pc = (uint32_t)(rest % p);
}
static void sim_run_until(uint64_t t_end) {
    uint64_t cap = 400000000u;             /* 안전장치: 스텁이 폭주해도 끝난다 */
    while (g_now < t_end && cap--) {
        tim_irq_eval();
        uint64_t next = t_end;
        if (g_ff) {
            if (g_iin < g_nin && g_in[g_iin].t < next) next = g_in[g_iin].t;
            uint64_t o = tim_clocks_to_ovf();
            if (o != UINT64_MAX && g_now + o < next) next = g_now + o;
            irq_line_t *l = &g_irq[TIM3_IRQn];
            if (l->enabled && l->level) {
                uint64_t d = g_now + 1u;
                if (g_mask_until > d) d = g_mask_until;
                if (g_busy_until > d) d = g_busy_until;
                if (d < next) next = d;
            }
            if (next > g_now + 1u) { tim_skip(next - g_now - 1u); g_now = next - 1u; }
        }
        sim_step();
    }
}
/* ISR 안에서 시간이 흐르는 것 흉내 (더 높은 우선순위 ISR 이 n 클럭 동안 끼어듦): 전달 없이 하드웨어만 */
static void sim_run_nested(uint64_t n) { while (n--) sim_tick_hw(); }

/* ======================= 드라이버: ISR → main 공유 데이터 ======================= */
typedef struct {                 /* ISR 이 쓰고 main 이 읽는다: seq 가 홀수인 동안은 쓰는 중 */
    volatile uint32_t seq;
    volatile uint32_t period;    /* 마지막으로 완료된 주기 [카운터 tick] (IC 프리스케일러 div 주기분) */
    volatile uint32_t high;      /* 그 주기의 high 폭 [tick] (high_ok 일 때만 의미) */
    volatile uint32_t last_rise; /* 마지막 상승 캡처의 32비트 확장 타임스탬프 */
    volatile uint32_t rises;     /* 지금 사슬(끊김 없는 연속 캡처)에서 센 상승 캡처 수 */
    volatile uint32_t breaks;    /* 사슬이 끊긴 횟수 (오버캡처 + 신호 끊김) */
    volatile uint32_t ovc, stalls;
    volatile uint8_t  valid, high_ok;
} ic_shared_t;
typedef struct { uint32_t seq, period, high, last_rise, rises, breaks, ovc, stalls; bool valid, high_ok; } ic_meas_t;

static ic_shared_t g_ic;
static struct {                  /* ISR 전용 상태 */
    uint32_t ovf;                /* 처리한 overflow 수 = 확장 타임스탬프의 상위 16비트 */
    uint32_t last_rise, high, idle_ovf, stall_ovf, div;
    bool have_rise, have_high, duty;
} g_ics;

#ifdef UNIT_TEST
static void (*g_mid_hook)(void);           /* main 의 복사 도중 "여기서 ISR 이 돈다" 주입 */
#define IC_TEST_HOOK() do { if (g_mid_hook) { void (*h_)(void) = g_mid_hook; g_mid_hook = NULL; h_(); } } while (0)
#else
#define IC_TEST_HOOK() do { } while (0)
#endif

static void ic_pub_begin(void) { g_ic.seq++; }
static void ic_pub_end(void) { g_ic.seq++; }

/* 주어짐: 상승 캡처 하나 (확장 타임스탬프) → 지난 주기를 발행 */
static void ic_on_rise(uint32_t ext) {
    ic_pub_begin();
    if (g_ics.have_rise) {
        g_ic.period = ext - g_ics.last_rise;                    /* uint32 뺄셈: 2^32 랩도 안전 (BM-B13) */
        g_ic.high_ok = g_ics.have_high;
        g_ic.high = g_ics.have_high ? g_ics.high : 0u;
        g_ic.valid = 1u;
    }
    g_ic.last_rise = ext;
    g_ic.rises++;
    ic_pub_end();
    g_ics.last_rise = ext;
    g_ics.have_rise = true;
    g_ics.have_high = false;
    g_ics.idle_ovf = 0u;
}
/* 주어짐: 하강 캡처 → 이번 주기의 high 폭 (상승 다음 하강일 때만) */
static void ic_on_fall(uint32_t ext) {
    if (!g_ics.have_rise) return;
    g_ics.high = ext - g_ics.last_rise;
    g_ics.have_high = true;
}
/* 주어짐: 사슬 끊기 (오버캡처: 캡처를 잃었다 / 신호 끊김: stall_ovf 번 overflow 동안 상승 없음) */
static void ic_chain_break(bool stall) {
    g_ics.have_rise = false;
    g_ics.have_high = false;
    ic_pub_begin();
    g_ic.breaks++;
    g_ic.rises = 0u;
    if (stall) { g_ic.valid = 0u; g_ic.high_ok = 0u; g_ic.stalls++; }
    else g_ic.ovc++;
    ic_pub_end();
}
static void ic_overcapture(int ch) {
    if (ch == 0) { ic_chain_break(false); return; }
    g_ics.have_high = false;                                    /* CH2: high 하나만 잃음 */
    ic_pub_begin(); g_ic.ovc++; ic_pub_end();
}
static void ic_on_overflow(void) {
    if (g_ics.stall_ovf && ++g_ics.idle_ovf == g_ics.stall_ovf) ic_chain_break(true);
}

/* TODO A2: 16비트 캡처값 ccr 을 32비트 타임스탬프로 확장한다.
 *  ovf = 지금까지 ISR 이 처리한 overflow 수, uif_pending = 같은 SR 스냅샷에서 UIF 가 아직 1 인가.
 *  UIF 가 pending 이면 그 overflow 가 캡처 전인지 후인지 모른다 → ccr 이 작으면(< 0x8000) overflow 가
 *  먼저 났다(상위 = ovf+1), 크면 캡처가 먼저였다(상위 = ovf). 조건: ISR 지연 < 반 바퀴(32768 tick). */
static uint32_t ic_extend(uint32_t ovf, uint32_t ccr, bool uif_pending) {
    (void)uif_pending;                          /* TODO A2: 지금은 UIF 를 무시한다 */
    return (ovf << 16) | (ccr & 0xFFFFu);
}

/* TODO B: TIM3 인터럽트 핸들러.
 *  1) SR 을 한 번 읽어 스냅샷 sr.
 *  2) 채널마다 (CH1 = 상승, CH2 = 하강) CCxIF 가 sr 에 있으면: CCRx 를 "먼저" 읽고 (CCxIF 도 지워짐)
 *     "그 다음" SR 을 다시 읽어 CCxOF 를 본다. OF 면 그 값은 버리고 OF 를 지울 목록에 넣고
 *     ic_overcapture(ch). 아니면 ic_extend(g_ics.ovf, ccr, sr 의 UIF) 로 확장해 모아 둔다.
 *  3) 모은 캡처가 둘이면 확장 타임스탬프 순서(부호 있는 차이)로 처리: CH1 → ic_on_rise, CH2 → ic_on_fall.
 *  4) sr 에 UIF 가 있으면 g_ics.ovf++ 와 ic_on_overflow(), UIF 를 지울 목록에.
 *  5) 지울 플래그가 있으면 rc_w0 규칙으로 한 번에: SR = ~(지울 비트들). */
void TIM3_IRQHandler(void) {
    /* TODO B: 아래는 "흔한 첫 시도" — overflow 를 먼저 세고, OF 도 순서도 안 본다 */
    uint32_t sr = REG_READ(TIM_SR);
    if (sr & SR_UIF) { g_ics.ovf++; ic_on_overflow(); REG_WRITE(TIM_SR, ~SR_UIF); }
    if (sr & SR_CC1IF) ic_on_rise(ic_extend(g_ics.ovf, REG_READ(TIM_CCR1), false));
    if (sr & SR_CC2IF) ic_on_fall(ic_extend(g_ics.ovf, REG_READ(TIM_CCR2), false));
}

/* TODO C1: 입력 캡처 초기화. psc: 카운터 클럭 = f_tim/(psc+1). icpsc: IC1PSC 코드 0..3 (/1 /2 /4 /8).
 *  duty: CH2 를 TI1 하강 에지로 (CC2S=10, CC2P=1) — icpsc ≠ 0 과 함께 쓰면 -1. stall_ovf: 0 = 끔.
 *  순서: CEN=0 → DIER=0 → CCER=0 (CCxS 변경 허용 + IC 프리스케일러 리셋) → PSC, ARR=0xFFFF (free-running)
 *  → CCMR1 → CCER (CC1E [+CC2E|CC2P]) → EGR=UG (PSC shadow 로드, CNT=0) → SR=0 (UG 의 UIF·묵은 플래그)
 *  → 드라이버 상태 초기화 (g_ics, g_ic 의 seq 외 필드) → DIER (UIE|CC1IE[|CC2IE]) → NVIC enable → CEN=1. */
static int ic_init(uint32_t psc, uint32_t icpsc, bool duty, uint32_t stall_ovf) {
    (void)psc; (void)icpsc; (void)duty; (void)stall_ovf;   /* TODO C1 */
    return -1;
}

/* TODO C2: main 에서 일관된 측정값 읽기 (seqlock). s1 = seq → 필드 복사 (period 복사 직후 IC_TEST_HOOK())
 *  → s2 = seq. s1 == s2 이고 짝수면 m->seq = s1 로 0 반환, 아니면 다시 (최대 4번, 실패 시 -1). */
static int ic_get(ic_meas_t *m) {
    /* TODO C2: 지금은 seq 확인 없이 그냥 복사한다 */
    m->period = g_ic.period;
    IC_TEST_HOOK();
    m->high = g_ic.high;
    m->high_ok = g_ic.high_ok;
    m->valid = g_ic.valid;
    m->last_rise = g_ic.last_rise;
    m->rises = g_ic.rises;
    m->breaks = g_ic.breaks;
    m->ovc = g_ic.ovc;
    m->stalls = g_ic.stalls;
    m->seq = g_ic.seq;
    return 0;
}

/* 주어짐: 계산 (정수, 반올림). f_tim [Hz], 결과 [mHz] / [0.01 %] */
static uint64_t ic_freq_mHz(uint32_t f_tim, uint32_t psc, uint32_t div, uint32_t period) {
    if (period == 0u) return 0u;
    uint64_t num = (uint64_t)f_tim * 1000u * div, den = ((uint64_t)psc + 1u) * period;
    return (num + den / 2u) / den;
}
static uint32_t ic_duty_bp(uint32_t high, uint32_t period) {
    return period ? (uint32_t)(((uint64_t)high * 10000u + period / 2u) / period) : 0u;
}
/* 두 스냅샷 사이의 평균 주파수 (reciprocal counting): 같은 사슬일 때만, 아니면 0 */
static uint64_t ic_avg_mHz(const ic_meas_t *a, const ic_meas_t *b, uint32_t f_tim, uint32_t psc, uint32_t div) {
    if (a->breaks != b->breaks || a->rises == 0u || b->rises <= a->rises) return 0u;
    uint64_t n = (uint64_t)(b->rises - a->rises) * div, dt = b->last_rise - a->last_rise;
    uint64_t num = (uint64_t)f_tim * 1000u * n, den = ((uint64_t)psc + 1u) * dt;
    return (num + den / 2u) / den;
}
/* 상대 오차 [0.1 ppm 단위] = (meas − truth)/truth */
static int64_t err_dppm(uint64_t meas, uint64_t truth) {
    int64_t d = (int64_t)meas - (int64_t)truth;
    return (d * 10000000 + (d >= 0 ? (int64_t)truth / 2 : -(int64_t)truth / 2)) / (int64_t)truth;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng = 0x2545F491u;                       /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define F_TIM 84000000u                    /* TIM3 클럭: APB1 42 MHz × 2 (BM-B10) */
static void sim_reset_all(void) {
    tim_model_init();
    memset(g_irq, 0, sizeof g_irq);
    g_irq[TIM3_IRQn].handler = TIM3_IRQHandler;
    g_now = 0u; g_mask_until = 0u; g_busy_until = 0u; g_isr_cost = 0u; g_steps = 0u;
    g_nin = g_iin = 0; g_level = 0u;
    g_mid_hook = NULL; g_ff = true;
}
/* 측정 tick 수가 진짜 길이 (F·div/T 클럭) 와 1 tick 미만 차이인가: |ticks·(psc+1)·T − F·div| < (psc+1)·T */
static bool within_tick(uint32_t ticks, uint32_t psc, uint32_t div, uint64_t F, uint64_t T) {
    uint64_t p = (uint64_t)psc + 1u, clocks = (uint64_t)ticks * p, want = F * div / T;
    if (clocks > 2u * want + 2u * p) return false;
    uint64_t lhs = clocks * T, rhs = F * div;
    return (lhs > rhs ? lhs - rhs : rhs - lhs) < p * T;
}

/* 흔한 첫 시도들 (비교용, 주어짐) */
static void isr_naive_ovf_first(void) {          /* ① overflow 를 항상 먼저 센다 */
    uint32_t sr = REG_READ(TIM_SR);
    if (sr & SR_UIF) { g_ics.ovf++; REG_WRITE(TIM_SR, ~SR_UIF); }
    if (sr & SR_CC1IF) ic_on_rise((g_ics.ovf << 16) | REG_READ(TIM_CCR1));
    if (sr & SR_CC2IF) ic_on_fall((g_ics.ovf << 16) | REG_READ(TIM_CCR2));
}
static void isr_naive_cap_first(void) {          /* ② 캡처를 항상 먼저 처리한다 */
    uint32_t sr = REG_READ(TIM_SR);
    if (sr & SR_CC1IF) ic_on_rise((g_ics.ovf << 16) | REG_READ(TIM_CCR1));
    if (sr & SR_CC2IF) ic_on_fall((g_ics.ovf << 16) | REG_READ(TIM_CCR2));
    if (sr & SR_UIF) { g_ics.ovf++; REG_WRITE(TIM_SR, ~SR_UIF); }
}
static void isr_of_from_snapshot(void) {         /* ③ 확장은 맞지만 OF 를 첫 스냅샷에서만 본다 */
    uint32_t sr = REG_READ(TIM_SR), clr = 0u;
    if (sr & SR_CC1OF) { clr |= SR_CC1OF; ic_overcapture(0); }
    else if (sr & SR_CC1IF) {
        uint32_t v = REG_READ(TIM_CCR1), hi = g_ics.ovf + (((sr & SR_UIF) && v < 0x8000u) ? 1u : 0u);
        ic_on_rise((hi << 16) | v);
    }
    if (sr & SR_UIF) { g_ics.ovf++; clr |= SR_UIF; }
    if (clr) REG_WRITE(TIM_SR, ~clr);
}

/* 랩 경계 시나리오: CNT 를 65536−W 로 맞춰 overflow 가 클럭 W 에, 상승 에지는 클럭 d 에.
 * ISR 은 R = min(d, W) + L 까지 마스크 (L = 첫 pending 부터의 지연). 반환 = 기록된 확장값 − 진짜 값. */
static int64_t wrap_case(void (*isr)(void), uint32_t W, uint64_t d, uint64_t L) {
    sim_reset_all();
    g_irq[TIM3_IRQn].handler = isr;
    ic_init(0u, 0u, false, 0u);
    REG_WRITE(TIM_CNT, 65536u - W);
    uint64_t first = d < W ? d : W;
    g_mask_until = first + L;
    in_add(d, 1);
    sim_run_until((d > W ? d : W) + L + 2u);
    uint32_t truth = 65536u - W + (uint32_t)d;
    if (!g_ics.have_rise) return INT64_MIN;
    return (int64_t)(int32_t)(g_ics.last_rise - truth);
}

/* 이벤트 기록용 래퍼: ISR 진입 시각을 해시하고, 발행된 주기를 모은다 */
static uint64_t g_trace_hash;
static uint32_t g_pub_seen, g_pub_bad;
static uint64_t g_cur_F, g_cur_T;
static uint32_t g_cur_psc, g_cur_div;
static void (*g_inner)(void);
static void isr_traced(void) {
    uint32_t before = g_ic.seq;
    g_trace_hash = (g_trace_hash ^ g_now ^ (uint64_t)g_ic.period << 20) * 0x100000001B3ull;
    g_inner();
    if (g_ic.seq != before && g_ic.valid && g_ic.rises >= 2u) {
        g_pub_seen++;
        if (!within_tick(g_ic.period, g_cur_psc, g_cur_div, g_cur_F, g_cur_T)) g_pub_bad++;
    }
}
static void trace_setup(void (*inner)(void), uint64_t F, uint64_t T, uint32_t psc, uint32_t div) {
    g_inner = inner; g_irq[TIM3_IRQn].handler = isr_traced;
    g_trace_hash = 0u; g_pub_seen = g_pub_bad = 0u;
    g_cur_F = F; g_cur_T = T; g_cur_psc = psc; g_cur_div = div;
}

/* 경쟁 주입용 훅들 */
static void hook_nested_1ms(void) { sim_run_nested(84000u); }   /* SR 읽기와 CCR1 읽기 사이 1 ms 선점 */
static void hook_isr_publishes(void) {                           /* main 의 복사 도중 다음 상승까지 진행 */
    int i = g_iin;
    while (i < g_nin && g_in[i].level == 0u) i++;
    sim_run_until((i < g_nin ? g_in[i].t : g_now) + 1u);
}

/* Part C: 이벤트 구동 타코미터 (구간별 신호 → 20 ms 마다 main 이 읽어 로그) */
typedef struct { uint32_t t0_ms, t1_ms; uint64_t f_mHz; uint32_t duty_bp; } seg_t;
static const seg_t k_prof[] = {
    {   0,  40,  1000000u, 2500u },        /* 1 kHz, 25 % */
    {  40,  80, 20000000u, 5000u },        /* 20 kHz, 50 % */
    {  80, 200,    50000u, 1000u },        /* 50 Hz, 10 % */
    { 200, 340,        0u,    0u },        /* 신호 없음 (팬 정지, 핀 low) */
    { 340, 400,  3333333u, 7000u },        /* 3333.333 Hz, 70 % */
};
#define MS(x) ((uint64_t)(x) * 84000u)

static void verbose_wrap_table(void) {
    printf("-- 랩 경계: overflow @ 클럭 1024, 상승 에지 @ d, ISR 은 첫 pending + 100 클럭 (UIF·CC1IF 둘 다 pending) --\n");
    printf("   d    CCR1   진짜 확장   정답 오차   ①ovf먼저 오차   ②캡처먼저 오차\n");
    const uint64_t ds[] = { 1000u, 1023u, 1024u, 1040u };
    for (int i = 0; i < 4; i++) {
        uint64_t d = ds[i];
        uint32_t truth = 65536u - 1024u + (uint32_t)d;
        int64_t e0 = wrap_case(TIM3_IRQHandler, 1024u, d, 100u);
        int64_t e1 = wrap_case(isr_naive_ovf_first, 1024u, d, 100u);
        int64_t e2 = wrap_case(isr_naive_cap_first, 1024u, d, 100u);
        printf("%5llu  0x%04X  %9u   %+8lld   %+12lld   %+14lld\n", (unsigned long long)d,
               truth & 0xFFFFu, truth, (long long)e0, (long long)e1, (long long)e2);
    }
}

#ifndef BM_M06_NO_MAIN
int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    ic_meas_t m;
    const uint64_t F = 1000ull * F_TIM;

    /* ======== Part A: 레지스터 규칙과 캡처 메커니즘 (드라이버 없이 레지스터로 직접) ======== */
    sim_reset_all();
    REG_WRITE(TIM_CCMR1, CCMR1_CC1S_TI1 | CCMR1_CC2S_TI1);
    REG_WRITE(TIM_CCER, CCER_CC1E | CCER_CC2E | CCER_CC2P);      /* CH1 상승, CH2 하강 (둘 다 TI1) */
    REG_WRITE(TIM_CR1, CR1_CEN);
    in_add(100u, 1); in_add(300u, 0); in_add(1100u, 1); in_add(1500u, 0); in_add(1700u, 1);
    sim_run_until(150u);
    CHECK(*g_tim.ccr[0] == 100u && (*g_tim.sr & SR_CC1IF) && !(*g_tim.sr & SR_CC2IF));
    CHECK(REG_READ(TIM_CCR1) == 100u && !(*g_tim.sr & SR_CC1IF));    /* CCR1 읽기가 CC1IF 를 지운다 */
    sim_run_until(1200u);
    CHECK(*g_tim.ccr[1] == 300u && *g_tim.ccr[0] == 1100u);           /* CH2 = 하강 에지 */
    sim_run_until(1800u);                                             /* CC1IF 를 안 지운 채 또 상승 */
    CHECK(*g_tim.ccr[0] == 1700u && (*g_tim.sr & SR_CC1OF) && (*g_tim.sr & SR_CC1IF));
    CHECK((*g_tim.sr & SR_CC2OF) != 0u);                              /* 300 을 안 읽고 1500 이 덮음 */
    REG_WRITE(TIM_SR, ~SR_CC1OF);                                     /* rc_w0: OF 만 0 */
    CHECK(!(*g_tim.sr & SR_CC1OF) && (*g_tim.sr & SR_CC1IF) && (*g_tim.sr & SR_CC2OF));
    REG_WRITE(TIM_CCR1, 5u);
    CHECK(*g_tim.ccr[0] == 1700u && g_unexpected == 0);               /* 입력 모드 CCR 은 읽기 전용 */

    /* ICxPSC: 코드 0..3 마다 상승 40개 → 캡처 40/div 번, CCR1 = 마지막 div 번째 에지 시각 */
    for (uint32_t code = 0; code < 4u; code++) {
        sim_reset_all();
        REG_WRITE(TIM_CCMR1, CCMR1_CC1S_TI1 | CCMR1_IC1PSC(code));
        REG_WRITE(TIM_CCER, CCER_CC1E);
        REG_WRITE(TIM_CR1, CR1_CEN);
        for (uint32_t k = 0; k < 40u; k++) { in_add(1000u * k + 10u, 1); in_add(1000u * k + 500u, 0); }
        sim_run_until(41000u);
        uint32_t div = 1u << code;
        CHECK(g_tim.cap[0] == 40u / div && *g_tim.ccr[0] == 1000u * (40u - 1u) + 10u);
    }
    /* PSC=3: 캡처값 = floor(t/4) (UG 로 PSC 를 shadow 에 넣은 뒤) */
    sim_reset_all();
    REG_WRITE(TIM_PSC, 3u); REG_WRITE(TIM_EGR, EGR_UG); REG_WRITE(TIM_SR, 0u);
    REG_WRITE(TIM_CCMR1, CCMR1_CC1S_TI1); REG_WRITE(TIM_CCER, CCER_CC1E); REG_WRITE(TIM_CR1, CR1_CEN);
    {
        bool ok = true;
        for (uint32_t t = 1001u; t < 1041u; t += 2u) {
            in_add(t, 1); in_add(t + 1u, 0);
            sim_run_until(t);
            ok = ok && *g_tim.ccr[0] == t / 4u;
            sim_run_until(t + 1u);
        }
        CHECK(ok);
    }

    /* 기본 측정: 1 kHz, 25 % → 84000 / 21000 tick, 1000.000 Hz, 25.00 % */
    sim_reset_all();
    CHECK(ic_init(0u, 0u, true, 0u) == 0);
    in_add_pwm(5000u, F, 1000000u, 0u, 2500u, 5u);
    sim_run_until(5000u + 5u * 84000u);
    CHECK(ic_get(&m) == 0 && m.valid && m.period == 84000u && m.high_ok && m.high == 21000u);
    CHECK(ic_freq_mHz(F_TIM, 0u, 1u, m.period) == 1000000u && ic_duty_bp(m.high, m.period) == 2500u);
    CHECK(m.rises == 5u && g_irq[TIM3_IRQn].calls >= 10u && g_unexpected == 0);

    /* 무작위 1500 신호 (20 Hz–200 kHz, 듀티 5–95 %, PSC ∈ {0,1,7,83}, 위상 무작위): 주기·high 모두 ±1 tick 미만 */
    {
        const uint32_t pscs[4] = { 0u, 1u, 7u, 83u };
        int bad = 0, n = 0;
        for (int i = 0; i < 1500; i++) {
            uint32_t psc = pscs[xs32() & 3u];
            uint64_t T = 20000u + (uint64_t)xs32() % 2000u * 100000u + xs32() % 100000u;   /* mHz */
            uint32_t duty = 500u + xs32() % 9001u;
            uint64_t ph = (uint64_t)xs32() % T;
            uint64_t P = F / T, H = F * duty / 10000u;
            if (P < 4u * (psc + 1u)) continue;
            sim_reset_all();
            ic_init(psc, 0u, true, 0u);
            in_add_pwm(1000u, F, T, ph, duty, 4u);
            sim_run_until(1000u + 4u * P + P / 2u);
            n++;
            if (ic_get(&m) || !m.valid || !m.high_ok || !within_tick(m.period, psc, 1u, F, T) ||
                !within_tick(m.high, psc, 1u, H, T)) bad++;
        }
        CHECK(n > 1400 && bad == 0);
        if (g_verbose) printf("무작위 신호: %d 건 측정, 1 tick 이상 틀림 %d 건\n", n, bad);
    }
    /* 빨리감기(이벤트 구동) == 매 클럭 실행: 같은 ISR 진입 시각 해시와 같은 결과 */
    {
        uint64_t h[2]; uint32_t per[2], calls[2]; uint64_t steps[2];
        for (int mode = 0; mode < 2; mode++) {
            rng = 0xC0FFEEu;
            sim_reset_all();
            g_ff = (mode == 1);
            g_isr_cost = 40u;
            ic_init(1u, 0u, true, 64u);
            trace_setup(TIM3_IRQHandler, F, 7777777u, 1u, 1u);
            in_add_pwm(3000u, F, 7777777u, 12345u, 3300u, 60u);
            for (int k = 0; k < 20; k++) {                         /* 무작위 마스크 창 (선점 흉내) */
                sim_run_until(g_now + 20000u + xs32() % 60000u);
                g_mask_until = g_now + xs32() % 3000u;
            }
            sim_run_until(2000000u);
            h[mode] = g_trace_hash; per[mode] = g_ic.period; calls[mode] = g_irq[TIM3_IRQn].calls; steps[mode] = g_steps;
        }
        g_ff = true;
        CHECK(h[0] == h[1] && per[0] == per[1] && calls[0] == calls[1] && steps[1] * 20u < steps[0]);
        if (g_verbose) printf("빨리감기 검증: ISR %u 회, sim_step %llu (매 클럭) vs %llu (이벤트 구동)\n",
                              calls[0], (unsigned long long)steps[0], (unsigned long long)steps[1]);
    }

    /* ======== Part B: 캡처 + 업데이트가 같은 ISR 진입에서 pending (랩 경계 전수) ======== */
    /* W = 32768: d = 1..65536 이면 CCR1 이 0..0xFFFF 를 정확히 한 번씩 지난다. L = 첫 pending 부터 ISR 까지 */
    {
        const uint64_t Ls[4] = { 0u, 1u, 100u, 32767u };
        int bad = 0;
        for (int li = 0; li < 4; li++)
            for (uint64_t d = 1u; d <= 65536u; d++)
                if (wrap_case(TIM3_IRQHandler, 32768u, d, Ls[li]) != 0) bad++;
        CHECK(bad == 0);
        int b1 = 0, b2 = 0;
        for (uint64_t d = 1u; d <= 65536u; d++) {
            if (wrap_case(isr_naive_ovf_first, 32768u, d, 100u) != 0) b1++;
            if (wrap_case(isr_naive_cap_first, 32768u, d, 100u) != 0) b2++;
        }
        CHECK(b1 == 100 && b2 == 101);          /* ① 랩 직전 100 개, ② 랩 직후 101 개를 틀린다 */
        if (g_verbose) printf("랩 전수 (65536 × L∈{0,1,100,32767}): 정답 틀림 %d / ①ovf먼저 %d / ②캡처먼저 %d (L=100)\n",
                              bad, b1, b2);
    }
    /* 지연 한계: overflow 후 32767 tick 에 캡처 + 그때 ISR → 맞음, 32768 → 정확히 한 바퀴(65536) 틀림 */
    CHECK(wrap_case(TIM3_IRQHandler, 1024u, 1024u + 32767u, 32767u) == 0);
    CHECK(wrap_case(TIM3_IRQHandler, 1024u, 1024u + 32768u, 32768u) == -65536);
    if (g_verbose) verbose_wrap_table();

    /* 아주 낮은 주파수: 1 Hz, 50 % (84 000 000 tick = overflow 1281 번 사이) */
    sim_reset_all();
    ic_init(0u, 0u, true, 4000u);
    in_add_pwm(1000u, F, 1000u, 0u, 5000u, 3u);
    sim_run_until(1000u + 3u * F_TIM + 10u);
    CHECK(ic_get(&m) == 0 && m.valid && m.period == 84000000u && m.high == 42000000u && m.rises == 3u);
    CHECK(ic_freq_mHz(F_TIM, 0u, 1u, m.period) == 1000u && g_ics.ovf == 3845u);
    /* 같은 신호인데 stall 타임아웃을 100 ms (128 overflow) 로 잡으면 → 1 Hz 를 "신호 없음" 으로 판정 */
    sim_reset_all();
    ic_init(0u, 0u, true, 128u);
    in_add_pwm(1000u, F, 1000u, 0u, 5000u, 3u);
    sim_run_until(1000u + 3u * F_TIM + 10u);
    CHECK(ic_get(&m) == 0 && !m.valid && m.stalls == 3u);

    /* 오버캡처: 주기 250 클럭 (336 kHz), ISR 1회 300 클럭 → 한 번 진입에 두 플래그를 다 처리해도 못 따라감 */
    {
        uint64_t T = F / 250u;                          /* 336 000 000 mHz */
        sim_reset_all();
        g_isr_cost = 300u;
        ic_init(0u, 0u, true, 0u);
        trace_setup(TIM3_IRQHandler, F, T, 0u, 1u);
        in_add_pwm(1000u, F, T, 0u, 5000u, 2000u);
        sim_run_until(1000u + 2000u * 250u);
        CHECK(g_ic.ovc > 100u && g_pub_bad == 0u);      /* 잃은 캡처는 버리고 틀린 주기는 하나도 발행 안 함 */
        uint32_t ovc_sol = g_ic.ovc, seen_sol = g_pub_seen;
        sim_reset_all();
        g_isr_cost = 300u;
        ic_init(0u, 0u, true, 0u);
        trace_setup(isr_naive_cap_first, F, T, 0u, 1u);  /* OF 를 안 보는 ISR */
        in_add_pwm(1000u, F, T, 0u, 5000u, 2000u);
        sim_run_until(1000u + 2000u * 250u);
        CHECK(g_pub_bad > 100u);                        /* 2주기·3주기짜리를 그대로 발행 */
        if (g_verbose) printf("오버캡처 (P=250, ISR 300 클럭): 정답 ovc=%u 발행 %u 틀림 0 / OF 무시 발행 %u 틀림 %u\n",
                              ovc_sol, seen_sol, g_pub_seen, g_pub_bad);
        /* 같은 신호를 IC1PSC=/8 (주파수만) 로: 캡처 간격 2000 클럭 → 오버캡처 0, 주기/8 정확 */
        sim_reset_all();
        g_isr_cost = 300u;
        ic_init(0u, 3u, false, 0u);
        trace_setup(TIM3_IRQHandler, F, T, 0u, 8u);
        in_add_pwm(1000u, F, T, 0u, 5000u, 2000u);
        sim_run_until(1000u + 2000u * 250u);
        CHECK(ic_get(&m) == 0 && m.ovc == 0u && m.period == 2000u && g_pub_bad == 0u && g_pub_seen > 200u);
        CHECK(ic_freq_mHz(F_TIM, 0u, 8u, m.period) == 336000000u);
        CHECK(ic_init(0u, 3u, true, 0u) == -1);         /* 듀티 + IC 프리스케일러 조합은 거절 */
    }
    /* 읽기 순서 경쟁: SR 스냅샷 → (1 ms 선점: 다음 상승이 CCR1 을 덮음) → CCR1 읽기 */
    {
        for (int variant = 0; variant < 2; variant++) {
            sim_reset_all();
            if (variant) g_irq[TIM3_IRQn].handler = isr_of_from_snapshot;
            ic_init(0u, 0u, false, 0u);
            for (uint32_t k = 0; k < 6u; k++) { in_add(10000u + 84000u * k, 1); in_add(50000u + 84000u * k, 0); }
            sim_run_until(94000u - 1u);                 /* 두 번째 상승 직전 */
            g_hook_at = g_access_no + 2u;              /* ISR 의 2번째 접근(CCR1 읽기) 직전 */
            g_hook_fn = hook_nested_1ms;
            uint32_t seq0 = g_ic.seq;
            sim_run_until(94000u);                      /* 이 ISR 안에서 178000 의 상승이 CCR1 을 덮는다 */
            bool torn_pub = (g_ic.seq != seq0 && g_ic.valid && g_ic.period != 84000u);
            uint32_t per_after = g_ic.period, ovc_after = g_ic.ovc;
            sim_run_until(10000u + 84000u * 5u + 1u);   /* 남은 상승 3개 */
            if (variant == 0) {
                CHECK(!torn_pub && ovc_after == 1u && g_ic.period == 84000u && g_ic.valid);
            } else {
                CHECK(torn_pub);                        /* ③: 엉뚱한 주기를 발행한다 */
                if (g_verbose) printf("읽기 순서 경쟁: ③ 스냅샷에서만 OF 확인 → 발행된 주기 %u tick (진짜 84000)\n", per_after);
            }
        }
    }
    /* 두 채널 순서: 하강(지난 주기)과 다음 상승이 한 ISR 에 같이 pending → 시각 순서로 처리 */
    sim_reset_all();
    ic_init(0u, 0u, true, 0u);
    in_add_pwm(1000u, F, 1000000u, 0u, 7500u, 6u);   /* 1 kHz, 75 %: 하강 @ +63000, 다음 상승 @ +84000 */
    sim_run_until(1000u + 84000u + 62000u);
    g_mask_until = 1000u + 2u * 84000u + 500u;       /* 하강과 상승을 둘 다 넘겨서 ISR */
    sim_run_until(1000u + 2u * 84000u + 501u);
    CHECK(ic_get(&m) == 0 && m.valid && m.period == 84000u && m.high_ok && m.high == 63000u && m.rises == 3u);
    sim_run_until(1000u + 5u * 84000u + 10u);
    CHECK(ic_get(&m) == 0 && m.valid && m.period == 84000u && m.high_ok && m.high == 63000u && m.ovc == 0u);
    /* 신호 끊김: 1 kHz 10 ms → 200 ms 동안 없음 → stall (128 overflow ≈ 99.9 ms) → 다시 2 kHz */
    sim_reset_all();
    ic_init(0u, 0u, true, 128u);
    in_add_pwm(MS(1), F, 1000000u, 0u, 5000u, 10u);
    sim_run_until(MS(200));
    CHECK(ic_get(&m) == 0 && !m.valid && m.stalls == 1u && m.breaks == 1u && m.rises == 0u);
    in_add_pwm(MS(210), F, 2000000u, 0u, 5000u, 3u);
    sim_run_until(MS(212) + 10u);
    CHECK(ic_get(&m) == 0 && m.valid && m.period == 42000u && m.high == 21000u && m.rises == 3u);

    /* ======== Part C: 초기화 순서, seqlock, 타코미터 로그 ======== */
    sim_reset_all();
    ic_init(83u, 0u, true, 0u);
    {
        int i_ccer0 = -1, i_ccmr = -1, i_psc = -1, i_ug = -1, i_sr = -1, i_dier = -1, i_cen = -1, i_arr = -1;
        for (int i = 0; i < g_nlog; i++) {
            mock_log_t *e = &g_log[i];
            if (e->op != 'W') continue;
            if (e->addr == TIM_CCER && e->val == 0u && i_ccer0 < 0) i_ccer0 = i;
            if (e->addr == TIM_CCMR1) i_ccmr = i;
            if (e->addr == TIM_PSC && e->val == 83u) i_psc = i;
            if (e->addr == TIM_ARR && e->val == 0xFFFFu) i_arr = i;
            if (e->addr == TIM_EGR && (e->val & EGR_UG)) i_ug = i;
            if (e->addr == TIM_SR && e->val == 0u) i_sr = i;
            if (e->addr == TIM_DIER && e->val != 0u) i_dier = i;
            if (e->addr == TIM_CR1 && (e->val & CR1_CEN)) i_cen = i;
        }
        CHECK(g_nlog > 0 && g_log[0].addr == TIM_CR1 && g_log[0].val == 0u);
        CHECK(i_ccer0 >= 0 && i_ccmr > i_ccer0 && i_psc >= 0 && i_arr >= 0 && i_ug > i_psc && i_ug > i_ccmr &&
              i_sr > i_ug && i_dier > i_sr && i_cen > i_dier && i_cen == g_nlog - 1);
        CHECK(g_tim.psc_sh == 83u && *g_tim.sr == 0u && *g_tim.dier == (DIER_UIE | DIER_CC1IE | DIER_CC2IE));
    }
    /* seqlock: main 이 period 를 복사한 직후 ISR 이 새 주기(42000/31500)를 발행 → 찢어진 쌍이 나오면 안 된다 */
    sim_reset_all();
    ic_init(0u, 0u, true, 0u);
    in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 3u);          /* 84000 / 21000 */
    in_add_pwm(1000u + 3u * 84000u, F, 2000000u, 0u, 7500u, 3u);   /* 42000 / 31500 */
    sim_run_until(1000u + 3u * 84000u + 35000u);            /* 2 kHz 첫 하강 뒤: 발행값 = 84000/21000 */
    g_mid_hook = hook_isr_publishes;                        /* period 복사 직후 다음 상승 → 42000/31500 발행 */
    {
        int r = ic_get(&m);
        bool consistent = (m.period == 84000u && m.high == 21000u) || (m.period == 42000u && m.high == 31500u);
        CHECK(r == 0 && consistent && m.seq == g_ic.seq && m.period == 42000u);
    }

    /* 타코미터: 프로파일 400 ms, main 이 20 ms 마다 읽는다 */
    sim_reset_all();
    ic_init(0u, 0u, true, 128u);
    for (size_t s = 0; s < sizeof k_prof / sizeof k_prof[0]; s++) {
        const seg_t *g = &k_prof[s];
        if (!g->f_mHz) continue;
        uint64_t P = F / g->f_mHz, n = (MS(g->t1_ms - g->t0_ms)) / (P + 1u);
        in_add_pwm(MS(g->t0_ms) + 1u, F, g->f_mHz, 0u, g->duty_bp, (uint32_t)n);
    }
    {
        ic_meas_t prev; memset(&prev, 0, sizeof prev);
        int ok_samples = 0, bad_samples = 0, stall_seen = 0;
        if (g_verbose) printf("-- 타코미터 로그 (PSC=0, 11.9 ns/tick, stall 128 ovf) --\n"
                              "  t[ms]  상태     f_inst[Hz]   duty[%%]   f_avg[Hz]  avg오차[ppm]  ovc stall\n");
        for (uint32_t t = 20u; t <= 400u; t += 20u) {
            sim_run_until(MS(t));
            if (ic_get(&m)) { bad_samples++; continue; }
            const seg_t *g = NULL;
            for (size_t s = 0; s < sizeof k_prof / sizeof k_prof[0]; s++)
                if (t > k_prof[s].t0_ms && t <= k_prof[s].t1_ms) g = &k_prof[s];
            uint64_t fi = m.valid ? ic_freq_mHz(F_TIM, 0u, 1u, m.period) : 0u;
            uint64_t fa = ic_avg_mHz(&prev, &m, F_TIM, 0u, 1u);
            /* 구간 시작 후 2.2 주기 이상 지났으면 측정이 안정 (구간 경계의 "섞인 주기" 는 제외) */
            bool settled = g && g->f_mHz && (uint64_t)(t - g->t0_ms) * g->f_mHz >= 2200000u;
            bool avg_ok = settled && t >= g->t0_ms + 20u && (uint64_t)(t - 20u - g->t0_ms) * g->f_mHz >= 1100000u;
            if (settled) {
                bool ok = m.valid && within_tick(m.period, 0u, 1u, F, g->f_mHz) && m.high_ok &&
                          within_tick(m.high, 0u, 1u, F * g->duty_bp / 10000u, g->f_mHz);
                if (avg_ok && m.rises > prev.rises) ok = ok && fa && err_dppm(fa, g->f_mHz) <= 10 && err_dppm(fa, g->f_mHz) >= -10;
                if (ok) ok_samples++; else bad_samples++;
            }
            if (g && !g->f_mHz && t >= 300u) { if (!m.valid && m.stalls == 1u) stall_seen++; else bad_samples++; }
            if (g_verbose) {
                char avg[48] = "         -            -";
                if (fa) snprintf(avg, sizeof avg, "%10.3f   %s", (double)fa / 1000.0, avg_ok ? "" : "(구간 섞임)");
                if (fa && avg_ok) snprintf(avg, sizeof avg, "%10.3f   %+8.1f", (double)fa / 1000.0,
                                           (double)err_dppm(fa, g->f_mHz) / 10.0);
                printf("  %4u   %-6s %12.3f   %6.2f   %s   %3u %3u\n", t, m.valid ? "OK" : "NO-SIG",
                       (double)fi / 1000.0, m.valid && m.high_ok ? ic_duty_bp(m.high, m.period) / 100.0 : 0.0,
                       avg, m.ovc, m.stalls);
            }
            prev = m;
        }
        CHECK(ok_samples >= 8 && bad_samples == 0 && stall_seen == 3);
        if (g_verbose) printf("안정 구간 샘플 %d 개 OK, 틀림 %d, 정지 감지 샘플 %d\n", ok_samples, bad_samples, stall_seen);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
