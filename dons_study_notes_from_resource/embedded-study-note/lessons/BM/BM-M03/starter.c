/* BM-M03 외부 인터럽트(EXTI)와 버튼 디바운스
 *
 * Part A — EXTI 레지스터: SYSCFG_EXTICR 라인→포트 mux, RTSR/FTSR, IMR, PR(rc_w1), 라인→IRQ 번호, 라인 소유권
 * Part B — 디바운서 1: EXTI 엣지 → 라인 마스크 → 원샷 타이머(TIM7) → 레벨 재확인 → 이벤트 → PR 지우고 unmask
 *          (짧은 글리치 무시, 롱프레스 1회, 마스크 창 안의 누름, stale PR, 같은 틱 두 IRQ 의 가짜 진입, 채터링)
 * Part C — 디바운서 2: 1 ms SysTick 샘플링 + 10 샘플 시프트 레지스터, 두 방식 일치/불일치, 이벤트 큐 → 메인 루프,
 *          ISR 비용 표, 브링업 로그
 *
 * 시뮬레이션: sim_step() 1번 = 0.1 ms. 순서 = 핀 갱신 → EXTI 엣지 검출 → TIM7 카운트 → SysTick(10 스텝마다)
 *            → NVIC-lite 디스패치 (IRQ 번호 작은 것부터, 우선순위는 모두 같다고 가정) → 메인 루프(app_poll).
 * 레지스터 주소·비트·IRQ 번호는 STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 파일 mock 의 정의.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M03_so && /tmp/BM-M03_so    (-v: 이벤트 로그)
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== mock core (BM-B02 mock 축약판 + BM-B04 식 끼어들기 훅) ===================== */
#ifdef UNIT_TEST                      /* host build: hooks */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else                                 /* target build: real MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);

#define MOCK_MAX_REGS 48
#define MOCK_LOG_CAP  128
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int        g_nregs, g_nlog, g_unexpected;
static mock_log_t g_log[MOCK_LOG_CAP];
static uint32_t   g_nacc;                                  /* CPU 접근 총 수 */
static uint32_t   g_inj_addr; static int g_inj_left;       /* inj_addr 를 n 번째 읽은 "직후" fn 실행 */
static void     (*g_inj_fn)(void);

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS || mock_find(addr)) return -1;
    g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return 0;
}
void mock_reset(void) { g_nregs = g_nlog = g_unexpected = 0; g_nacc = 0; g_inj_fn = NULL; }
static void mock_log_add(char op, uint32_t addr, uint32_t val) {
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
}
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *r = mock_find(addr);
    uint32_t v = 0;
    if (!r) g_unexpected++; else v = r->r ? r->r(addr, r->val, r->ctx) : r->val;
    mock_log_add('R', addr, v); g_nacc++;
    if (g_inj_fn && addr == g_inj_addr && --g_inj_left == 0) { void (*f)(void) = g_inj_fn; g_inj_fn = NULL; f(); }
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (!r) g_unexpected++; else r->val = r->w ? r->w(addr, r->val, val, r->ctx) : val;
    mock_log_add('W', addr, val); g_nacc++;
}
static uint32_t *mock_slot(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? &r->val : NULL; }
static uint32_t mock_peek(uint32_t addr) { uint32_t *p = mock_slot(addr); return p ? *p : 0xDEADBEEFu; }
static void mock_poke(uint32_t addr, uint32_t v) { uint32_t *p = mock_slot(addr); if (p) *p = v; }

/* ===================== 주소 (RM0090 기준 대표값 — RM 확인 권장) ===================== */
#define RCC_APB2ENR           0x40023844u
#define RCC_APB2ENR_SYSCFGEN  (1u << 14)
enum { PORT_A, PORT_B, PORT_C, PORT_D, PORT_E, PORT_F, PORT_G, PORT_H, PORT_I, EXTI_NPORT };
#define GPIO_IDR(p)           (0x40020000u + 0x400u * (uint32_t)(p) + 0x10u)
#define SYSCFG_EXTICR(line)   (0x40013808u + 4u * ((uint32_t)(line) >> 2))   /* EXTICR1..4 = +0x08..+0x14 */
#define EXTI_IMR              0x40013C00u
#define EXTI_EMR              0x40013C04u
#define EXTI_RTSR             0x40013C08u
#define EXTI_FTSR             0x40013C0Cu
#define EXTI_SWIER            0x40013C10u
#define EXTI_PR               0x40013C14u        /* rc_w1: 1 을 쓴 비트만 지워진다 */
#define EXTI_LINES_MASK       0x007FFFFFu        /* F4: 라인 0–22 (0–15 = GPIO) */
#define TIM7_CR1              0x40001400u
#define TIM7_DIER             0x4000140Cu
#define TIM7_SR               0x40001410u        /* rc_w0 */
#define TIM7_EGR              0x40001414u
#define TIM7_CNT              0x40001424u
#define TIM7_PSC              0x40001428u
#define TIM7_ARR              0x4000142Cu
#define TIM_CR1_CEN 1u
#define TIM_CR1_OPM (1u << 3)
#define TIM_DIER_UIE 1u
#define TIM_SR_UIF 1u
#define TIM_EGR_UG 1u
enum { IRQ_EXTI0 = 6, IRQ_EXTI9_5 = 23, IRQ_EXTI15_10 = 40, IRQ_TIM7 = 55, NIRQ = 64 };

/* ===================== 외부 세계: 핀 레벨 + 버튼 신호 타임라인 ===================== */
#define SIG_MAX 120000u                 /* 12 s @ 0.1 ms */
#define BTN_PORT PORT_C                 /* Nucleo-F4 B1 = PC13, 누르면 0 (보드 매뉴얼 확인 권장) */
#define BTN_PIN  13u
static uint8_t  g_sig[SIG_MAX];         /* PC13 원시 레벨: 1 = 놓음(풀업), 0 = 눌림 */
static uint32_t g_sig_len = 1;
static uint16_t g_pins[EXTI_NPORT];
static uint32_t g_tick;                 /* 모델 시계: 0.1 ms 단위 */
static uint32_t g_pin_edges;            /* 버튼 핀의 실제 엣지 수 */
static uint32_t sim_now(void) { return g_tick; }
static void pin_set(unsigned port, unsigned pin, unsigned level) {
    uint16_t old = g_pins[port], nv = (uint16_t)(level ? (old | (1u << pin)) : (old & ~(1u << pin)));
    if (nv != old && port == BTN_PORT && pin == BTN_PIN) g_pin_edges++;
    g_pins[port] = nv;
}

/* ===================== EXTI 모델 =====================
 * 엣지 검출기는 EXTICR 로 고른 포트의 핀을 본다. 선택한 엣지(RTSR/FTSR) && IMR → PR 비트 1.
 * 기본: 마스크된 라인은 PR 을 세우지 않는다 (RM0090 블록도 + ST 커뮤니티 실험 기준 — RM 확인 권장).
 * latch_masked = true: "마스크 중에도 PR 이 래치되는" 하드웨어를 흉내 (계열/구현에 따라 다름 — 비교용).
 * SWIER 0→1 쓰기 = 소프트웨어 엣지. PR 에 1 쓰면 그 비트와 SWIER 비트가 지워진다.
 * RTSR/FTSR 의 그 라인 비트를 바꾸면 PR 도 지워진다 (RM 문구 "changing the sensitivity" — 확인 권장).
 * EXTICR 은 SYSCFG 클럭(APB2ENR.SYSCFGEN)이 꺼져 있으면 쓰기 무시, 읽으면 0 (BM-B03 규칙 축약판).
 * EXTICR 을 바꾸면 mux 출력이 바뀌므로 그 순간 엣지가 생길 수 있다 (모델 정의 — 그래서 드라이버 순서가 중요). */
static struct { uint32_t prev_in; bool latch_masked; } g_exti;
static uint32_t *R_APB2, *R_IMR, *R_RTSR, *R_FTSR, *R_SWIER, *R_PR, *R_EXTICR[4];
static uint32_t *T_CR1, *T_DIER, *T_SR, *T_CNT, *T_ARR;
static bool syscfg_clk(void) { return (*R_APB2 & RCC_APB2ENR_SYSCFGEN) != 0u; }
static uint32_t exti_gate(void) { return g_exti.latch_masked ? EXTI_LINES_MASK : *R_IMR; }
static uint32_t exti_inputs(void) {
    uint32_t in = 0;
    for (unsigned line = 0; line < 16u; line++) {
        unsigned port = (*R_EXTICR[line >> 2] >> (4u * (line & 3u))) & 0xFu;
        if (port < EXTI_NPORT && ((g_pins[port] >> line) & 1u)) in |= 1u << line;
    }
    return in;
}
static void exti_sample(void) {
    uint32_t in = exti_inputs(), ch = in ^ g_exti.prev_in;
    uint32_t trig = (ch & in & *R_RTSR) | (ch & ~in & *R_FTSR);
    *R_PR |= trig & exti_gate();
    g_exti.prev_in = in;
}
static uint32_t h_mask_w(uint32_t a, uint32_t old, uint32_t v, void *c) { (void)a; (void)old; (void)c; return v & EXTI_LINES_MASK; }
static uint32_t h_pr_w(uint32_t a, uint32_t old, uint32_t v, void *c) { (void)a; (void)c; *R_SWIER &= ~v; return old & ~v; }
static uint32_t h_swier_w(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    *R_PR |= (v & ~old & EXTI_LINES_MASK) & exti_gate();
    return (old | v) & EXTI_LINES_MASK;
}
static uint32_t h_trig_w(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    *R_PR &= ~((old ^ v) & EXTI_LINES_MASK);
    return v & EXTI_LINES_MASK;
}
static uint32_t h_exticr_r(uint32_t a, uint32_t stored, void *c) { (void)a; (void)c; return syscfg_clk() ? stored : 0u; }
static uint32_t h_exticr_w(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)c;
    if (!syscfg_clk()) return old;
    *mock_slot(a) = v & 0xFFFFu;
    exti_sample();                                   /* mux 가 바뀐 순간의 엣지 */
    return v & 0xFFFFu;
}
static uint32_t h_idr_r(uint32_t a, uint32_t stored, void *c) { (void)stored; (void)c; return g_pins[(a - 0x40020010u) / 0x400u]; }
static uint32_t h_ro_w(uint32_t a, uint32_t old, uint32_t v, void *c) { (void)a; (void)v; (void)c; return old; }

/* ===================== TIM7 원샷 모델 (BM-B14 타이머 모델 축약판) =====================
 * 카운터 클럭 = 모델 틱(10 kHz). 실제 보드라면 84 MHz / (PSC+1) = 10 kHz → PSC = 8399 (B14).
 * CEN 이면 매 틱 CNT++ ; CNT == ARR 다음 틱에 업데이트: CNT=0, UIF=1, OPM 이면 CEN=0. → 지연 = ARR+1 틱. */
static uint32_t h_sr_w(uint32_t a, uint32_t old, uint32_t v, void *c) { (void)a; (void)c; return old & v; }
static uint32_t h_egr_w(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)old; (void)c;
    if (v & TIM_EGR_UG) { *T_CNT = 0; *T_SR |= TIM_SR_UIF; }   /* UG 도 UIF 를 세운다 (URS=0) */
    return 0;
}
static void tim7_tick(void) {
    if (!(*T_CR1 & TIM_CR1_CEN)) return;
    if (*T_CNT >= *T_ARR) { *T_CNT = 0; *T_SR |= TIM_SR_UIF; if (*T_CR1 & TIM_CR1_OPM) *T_CR1 &= ~TIM_CR1_CEN; }
    else (*T_CNT)++;
}
/* 드라이버 도우미 (주어짐): ticks 틱 뒤 UIF 1번. 이미 돌고 있으면 다시 시작 = 이전 예약 취소 */
static void tim7_oneshot(uint32_t ticks) {
    if (ticks == 0u) ticks = 1u;
    if (ticks > 65536u) ticks = 65536u;
    REG_WRITE(TIM7_CR1, TIM_CR1_OPM);                 /* 정지 */
    REG_WRITE(TIM7_ARR, ticks - 1u);
    REG_WRITE(TIM7_EGR, TIM_EGR_UG);                  /* CNT = 0 (UIF 도 섬) */
    REG_WRITE(TIM7_SR, ~TIM_SR_UIF);                  /* rc_w0: UG 가 세운 UIF 지우기 */
    REG_WRITE(TIM7_DIER, TIM_DIER_UIE);
    REG_WRITE(TIM7_CR1, TIM_CR1_OPM | TIM_CR1_CEN);
}

/* ===================== NVIC-lite + SysTick ===================== */
typedef void (*isr_t)(void);
static isr_t    g_vec[NIRQ];
static bool     g_nv_en[NIRQ], g_nv_pend[NIRQ];
static uint32_t g_isr_calls[NIRQ], g_storm;
static isr_t    g_systick_isr;
static uint32_t g_systick_calls;
#define SYSTICK_STEPS 10u                               /* 1 ms */
static void nvic_lite_enable(int irqn) { if (irqn >= 0 && irqn < NIRQ) g_nv_en[irqn] = true; }
static void irq_line_set(int irqn, bool level) { if (level) g_nv_pend[irqn] = true; }   /* 펜딩은 래치된다 */
static bool irq_line_level(int irqn) {                 /* 주변장치 → NVIC 라인 */
    uint32_t act = *R_PR & *R_IMR;
    if (irqn >= IRQ_EXTI0 && irqn <= IRQ_EXTI0 + 4) return (act >> (irqn - IRQ_EXTI0)) & 1u;
    if (irqn == IRQ_EXTI9_5) return (act & 0x03E0u) != 0u;
    if (irqn == IRQ_EXTI15_10) return (act & 0xFC00u) != 0u;
    if (irqn == IRQ_TIM7) return (*T_SR & TIM_SR_UIF) && (*T_DIER & TIM_DIER_UIE);
    return false;
}
static const int k_irqs[] = { 6, 7, 8, 9, 10, IRQ_EXTI9_5, IRQ_EXTI15_10, IRQ_TIM7 };
#define NK ((int)(sizeof k_irqs / sizeof k_irqs[0]))
static void nvic_dispatch(void) {
    for (int guard = 0; guard < 16; guard++) {
        for (int i = 0; i < NK; i++) irq_line_set(k_irqs[i], irq_line_level(k_irqs[i]));
        int run = -1;
        for (int i = 0; i < NK && run < 0; i++) if (g_nv_pend[k_irqs[i]] && g_nv_en[k_irqs[i]]) run = k_irqs[i];
        if (run < 0) return;
        g_nv_pend[run] = false;                          /* 진입 = 펜딩 해제 */
        g_isr_calls[run]++;
        if (g_vec[run]) g_vec[run]();
    }
    g_storm++;                                           /* 한 틱에 16번 넘게 재진입 = 인터럽트 폭풍 */
}

/* ===================== 이벤트 큐 (ISR → 메인, SPSC, 2의 거듭제곱) ===================== */
enum { EV_PRESS = 1, EV_RELEASE, EV_LONG };
static const char *k_evname[] = { "?", "PRESS", "RELEASE", "LONG_PRESS" };
typedef struct { uint8_t type; char src; uint32_t t; } ev_t;
#define EVQ_CAP 64u
static ev_t g_evq[EVQ_CAP];
static volatile uint32_t g_evq_w, g_evq_r;
static uint32_t g_evq_drop;
static int g_verbose;
static void ev_push(uint8_t type, char src, uint32_t t) {          /* ISR 쪽 (같은 우선순위 → 생산자 1개) */
    if (g_evq_w - g_evq_r >= EVQ_CAP) { g_evq_drop++; return; }
    g_evq[g_evq_w & (EVQ_CAP - 1u)] = (ev_t){ type, src, t };
    g_evq_w++;
}
#define OUT_CAP 512
static ev_t g_out[OUT_CAP];
static int  g_nout;
static void app_poll(void) {                                        /* 메인 루프 쪽 */
    while (g_evq_r != g_evq_w) {
        ev_t e = g_evq[g_evq_r & (EVQ_CAP - 1u)];
        g_evq_r++;
        if (g_nout < OUT_CAP) g_out[g_nout++] = e;
        if (g_verbose) printf("  [%8.1f ms] %c %s\n", e.t / 10.0, e.src, k_evname[e.type]);
    }
}

/* ===================== 드라이버 + 디바운서 공용 정의 ===================== */
#define EXTI_EINVAL (-1)
#define EXTI_EBUSY  (-2)
enum { EXTI_RISING = 1, EXTI_FALLING = 2, EXTI_BOTH = 3 };
#define LINE_FREE 0xFFu
static uint8_t g_line_owner[16];                     /* 라인 n 을 쓰는 포트 (EXTICR 은 라인당 포트 1개) */
static void exti_driver_reset(void) { memset(g_line_owner, LINE_FREE, sizeof g_line_owner); }

#define DB_TICKS    200u                             /* 20 ms 재확인 지연 */
#define LONG_TICKS  8000u                            /* 800 ms 롱프레스 */
#define SAMP_K      10u                              /* 1 ms 샘플 10개 연속 */
#define SAMP_MASK   ((1u << SAMP_K) - 1u)
typedef enum { DBX_IDLE, DBX_SETTLE, DBX_HOLD } dbx_state_t;
typedef struct {
    unsigned port, line;
    volatile uint8_t state, stable, long_fired;      /* stable: 1 = 눌림 확정 */
    volatile uint32_t press_t;
    uint32_t n_spurious, n_rearm;
} dbx_t;
static dbx_t g_dbx;
typedef struct { unsigned port, pin; uint16_t hist; uint8_t stable, long_fired; uint32_t press_t; } dbs_t;
static dbs_t g_dbs;
static uint8_t btn_raw_pressed(unsigned port, unsigned pin) {      /* 액티브 로우 */
    return ((REG_READ(GPIO_IDR(port)) >> pin) & 1u) == 0u;
}

/* ===================== Part A — EXTI 드라이버 ===================== */
/* TODO(A1): EXTI 라인 → NVIC IRQ 번호. 0–4 → 6–10 (각자), 5–9 → 23 (EXTI9_5), 10–15 → 40 (EXTI15_10), 그 외 -1 */
int exti_line_to_irqn(unsigned line)
{
    (void)line;
    return -1;                                   /* TODO(A1) */
}

/* TODO(A2): 포트 port 의 핀 pin 을 EXTI 라인 pin 에 연결하고 edges 엣지로 설정, 마지막에 unmask.
 *   인자 검사(port < 9, pin ≤ 15, edges 1..3) 실패 → EXTI_EINVAL, 레지스터 접근 0번.
 *   라인이 이미 다른 포트 소유 → EXTI_EBUSY (접근 0번). 순서:
 *   SYSCFG 클럭 RMW + 읽어 보기 → IMR 비트 끄기 → EXTICR 니블 RMW → RTSR/FTSR 비트 RMW → PR 에 그 비트만 1 → IMR 비트 켜기 */
int exti_config(unsigned port, unsigned pin, unsigned edges)
{
    (void)port; (void)pin; (void)edges;
    return EXTI_EINVAL;                          /* TODO(A2) */
}

/* TODO(A3): exti_ack = PR 에 그 비트만 1 (|= 금지). exti_mask = IMR 비트 끄기.
 *           exti_unmask = PR 먼저 지우고 IMR 비트 켜기 */
void exti_ack(unsigned line)
{
    (void)line;                                  /* TODO(A3) */
}
void exti_mask(unsigned line)
{
    (void)line;                                  /* TODO(A3) */
}
void exti_unmask(unsigned line)
{
    (void)line;                                  /* TODO(A3) */
}

/* ===================== Part B — 디바운서 1: EXTI + 원샷 타이머 재확인 ===================== */
/* TODO(B1): EXTI15_10 핸들러. 공유 벡터라 내 라인의 PR 이 아니면 n_spurious++ 하고 끝.
 *           아니면 마스크 → PR 지우기 → state = SETTLE → tim7_oneshot(DB_TICKS) (진행 중이던 롱프레스 예약은 취소) */
void dbx_exti_isr(void)
{
    /* TODO(B1) */
}

/* TODO(B2): TIM7 핸들러. UIF 가 0 이면 가짜 진입(n_spurious++), 아니면 UIF 지우고(rc_w0):
 *   SETTLE: 레벨 1번 읽기 → stable 과 다르면 PRESS/RELEASE (PRESS 면 press_t = now, long_fired = 0)
 *           → exti_unmask → 다시 읽어 stable 과 다르면 (마스크 창 안의 변화) 다시 마스크·ack·재예약(n_rearm++)
 *           → 눌림 확정이고 롱프레스 전이면 남은 시간으로 HOLD 예약 (이미 지났으면 즉시 LONG) → 아니면 IDLE
 *   HOLD:   눌림이고 아직이면 LONG 1번 → IDLE */
void dbx_tim_isr(void)
{
    (void)ev_push; (void)sim_now;                /* TODO(B2) */
}

/* ===================== Part C — 디바운서 2: 1 ms 샘플링 + 시프트 레지스터 ===================== */
/* TODO(C1): SysTick(1 ms)마다 호출. hist = (hist << 1 | 눌림) & SAMP_MASK.
 *   놓음 확정인데 hist == SAMP_MASK → PRESS (press_t, long_fired=0) / 눌림 확정인데 hist == 0 → RELEASE
 *   눌림 확정 && 롱프레스 전 && now - press_t ≥ LONG_TICKS → LONG 1번 */
void dbs_tick(void)
{
    /* TODO(C1) */
}

/* ===================== 시작 코드 (주어짐) ===================== */
static int dbx_init(unsigned port, unsigned pin) {
    memset(&g_dbx, 0, sizeof g_dbx);
    g_dbx.port = port; g_dbx.line = pin;
    g_dbx.stable = btn_raw_pressed(port, pin);       /* 부팅 때 눌려 있으면: PRESS/LONG 없음, 떼면 RELEASE */
    g_dbx.long_fired = 1;
    int rc = exti_config(port, pin, EXTI_BOTH);
    if (rc != 0) return rc;
    int irqn = exti_line_to_irqn(pin);
    if (irqn < 0) return -1;
    g_vec[irqn] = dbx_exti_isr; g_vec[IRQ_TIM7] = dbx_tim_isr;
    nvic_lite_enable(irqn); nvic_lite_enable(IRQ_TIM7);
    return 0;
}
static void dbs_init(unsigned port, unsigned pin) {
    memset(&g_dbs, 0, sizeof g_dbs);
    g_dbs.port = port; g_dbs.pin = pin;
    g_dbs.stable = btn_raw_pressed(port, pin);
    g_dbs.hist = g_dbs.stable ? (uint16_t)SAMP_MASK : 0u;
    g_dbs.long_fired = 1;
    g_systick_isr = dbs_tick;
}

/* ===================== 시뮬레이션 루프 ===================== */
static void world_reset(bool latch_masked) {
    mock_reset();
    mock_add_reg(RCC_APB2ENR, 0, NULL, NULL, NULL);
    for (unsigned p = 0; p < EXTI_NPORT; p++) mock_add_reg(GPIO_IDR(p), 0, h_idr_r, h_ro_w, NULL);
    for (unsigned k = 0; k < 4u; k++) mock_add_reg(SYSCFG_EXTICR(4u * k), 0, h_exticr_r, h_exticr_w, NULL);
    mock_add_reg(EXTI_IMR, 0, NULL, h_mask_w, NULL);
    mock_add_reg(EXTI_EMR, 0, NULL, h_mask_w, NULL);
    mock_add_reg(EXTI_RTSR, 0, NULL, h_trig_w, NULL);
    mock_add_reg(EXTI_FTSR, 0, NULL, h_trig_w, NULL);
    mock_add_reg(EXTI_SWIER, 0, NULL, h_swier_w, NULL);
    mock_add_reg(EXTI_PR, 0, NULL, h_pr_w, NULL);
    mock_add_reg(TIM7_CR1, 0, NULL, NULL, NULL);
    mock_add_reg(TIM7_DIER, 0, NULL, NULL, NULL);
    mock_add_reg(TIM7_SR, 0, NULL, h_sr_w, NULL);
    mock_add_reg(TIM7_EGR, 0, NULL, h_egr_w, NULL);
    mock_add_reg(TIM7_CNT, 0, NULL, NULL, NULL);
    mock_add_reg(TIM7_PSC, 0, NULL, NULL, NULL);
    mock_add_reg(TIM7_ARR, 0xFFFFu, NULL, NULL, NULL);
    R_APB2 = mock_slot(RCC_APB2ENR);
    R_IMR = mock_slot(EXTI_IMR); R_RTSR = mock_slot(EXTI_RTSR); R_FTSR = mock_slot(EXTI_FTSR);
    R_SWIER = mock_slot(EXTI_SWIER); R_PR = mock_slot(EXTI_PR);
    for (unsigned k = 0; k < 4u; k++) R_EXTICR[k] = mock_slot(SYSCFG_EXTICR(4u * k));
    T_CR1 = mock_slot(TIM7_CR1); T_DIER = mock_slot(TIM7_DIER); T_SR = mock_slot(TIM7_SR);
    T_CNT = mock_slot(TIM7_CNT); T_ARR = mock_slot(TIM7_ARR);
    memset(g_pins, 0, sizeof g_pins);                  /* PA13 등은 0 */
    g_pins[BTN_PORT] = (uint16_t)(1u << BTN_PIN);      /* PC13 풀업 = 1 */
    g_exti.latch_masked = latch_masked;
    g_exti.prev_in = exti_inputs();
    g_tick = 0; g_pin_edges = 0;
    memset(g_vec, 0, sizeof g_vec); memset(g_nv_en, 0, sizeof g_nv_en); memset(g_nv_pend, 0, sizeof g_nv_pend);
    memset(g_isr_calls, 0, sizeof g_isr_calls); g_storm = 0;
    g_systick_isr = NULL; g_systick_calls = 0;
    g_evq_w = g_evq_r = 0; g_evq_drop = 0; g_nout = 0;
    exti_driver_reset();
    memset(&g_dbx, 0, sizeof g_dbx); memset(&g_dbs, 0, sizeof g_dbs);
}
static void sim_step(void) {
    g_tick++;
    pin_set(BTN_PORT, BTN_PIN, g_sig[g_tick < g_sig_len ? g_tick : g_sig_len - 1u]);
    exti_sample();
    tim7_tick();
    if (g_tick % SYSTICK_STEPS == 0u && g_systick_isr) { g_systick_calls++; g_systick_isr(); }
    nvic_dispatch();
    app_poll();
}
static void sim_run_until(uint32_t t_end) { while (g_tick < t_end) sim_step(); }
enum { RUN_EXTI = 1, RUN_SAMP = 2 };
static int sim_start(int methods, bool latch_masked) {   /* 신호를 다 만든 뒤 호출 */
    world_reset(latch_masked);
    pin_set(BTN_PORT, BTN_PIN, g_sig[0]);
    g_exti.prev_in = exti_inputs();
    g_pin_edges = 0;
    int rc = 0;
    if (methods & RUN_EXTI) rc = dbx_init(BTN_PORT, BTN_PIN);
    if (methods & RUN_SAMP) dbs_init(BTN_PORT, BTN_PIN);
    return rc;
}

/* ===================== 신호 생성기 (고정 시드) ===================== */
static uint32_t t_rng = 0x2468ACE1u;
static uint32_t xs32(void) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 17; t_rng ^= t_rng << 5; return t_rng; }
static uint32_t rnd(uint32_t n) { return n ? xs32() % n : 0u; }
static void sig_reset(uint32_t len, uint8_t level) { g_sig_len = len; memset(g_sig, level, len); }
static void sig_fill(uint32_t from, uint8_t level) { if (from < g_sig_len) memset(g_sig + from, level, g_sig_len - from); }
/* t 에서 final 로 바뀜. t+1 .. t+bounce-1 은 무작위 튐, t+bounce 부터 final 고정. 첫 엣지 = t */
static void sig_edge(uint32_t t, uint8_t final, uint32_t bounce) {
    sig_fill(t, final);
    for (uint32_t i = 1; i < bounce && t + i < g_sig_len; i++) g_sig[t + i] = (uint8_t)(xs32() & 1u);
}
static uint32_t sig_last_edge(uint32_t from, uint32_t to) {   /* [from, to] 안의 마지막 엣지 틱 */
    uint32_t last = from;
    for (uint32_t t = from; t <= to && t < g_sig_len; t++) if (t > 0 && g_sig[t] != g_sig[t - 1]) last = t;
    return last;
}

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int ev_count(char src, int type) {
    int n = 0;
    for (int i = 0; i < g_nout; i++) if (g_out[i].src == src && (type == 0 || g_out[i].type == type)) n++;
    return n;
}
static const ev_t *ev_nth(char src, int k) {
    for (int i = 0; i < g_nout; i++) if (g_out[i].src == src && k-- == 0) return &g_out[i];
    return NULL;
}
static bool ev_is(char src, int k, int type, uint32_t t) {
    const ev_t *e = ev_nth(src, k);
    return e && e->type == type && e->t == t;
}
static int log_find(char op, uint32_t addr, int from) {
    for (int i = from; i < g_nlog; i++) if (g_log[i].op == op && g_log[i].addr == addr) return i;
    return -1;
}

/* ---------- Part A ---------- */
static void test_model(void) {
    /* 모델 자체 (스타터에서도 통과): 레지스터를 직접 찔러 본다 */
    world_reset(false);
    *R_APB2 = RCC_APB2ENR_SYSCFGEN;
    REG_WRITE(SYSCFG_EXTICR(13), 0x0020u);                  /* 라인 13 = PC13 */
    CHECK(mock_peek(SYSCFG_EXTICR(13)) == 0x0020u);
    REG_WRITE(EXTI_FTSR, 1u << 13);
    pin_set(PORT_C, 13, 0); exti_sample();
    CHECK(*R_PR == 0u);                                      /* 마스크 상태: PR 안 섬 (기본 모델) */
    pin_set(PORT_C, 13, 1); exti_sample();
    REG_WRITE(EXTI_IMR, 1u << 13);
    pin_set(PORT_C, 13, 0); exti_sample();
    CHECK(*R_PR == (1u << 13));                              /* 하강 엣지 + IMR → PR */
    *R_PR |= 1u;                                             /* 라인 0 도 펜딩이라 치고 */
    REG_WRITE(EXTI_PR, 1u << 13);
    CHECK(*R_PR == 1u);                                      /* rc_w1: 쓴 비트만 지워짐 */
    REG_WRITE(EXTI_SWIER, 1u << 13);
    CHECK((*R_PR & (1u << 13)) && *R_SWIER == (1u << 13));   /* SWIER → PR */
    REG_WRITE(EXTI_PR, 1u << 13);
    CHECK(*R_SWIER == 0u);                                   /* PR 지우면 SWIER 도 */
    REG_WRITE(EXTI_SWIER, 1u << 13);
    REG_WRITE(EXTI_RTSR, 1u << 13);                          /* 감도 변경 → PR 지워짐 */
    CHECK((*R_PR & (1u << 13)) == 0u);
    world_reset(true);                                       /* latch_masked 모델 */
    *R_APB2 = RCC_APB2ENR_SYSCFGEN;
    REG_WRITE(SYSCFG_EXTICR(13), 0x0020u);
    REG_WRITE(EXTI_FTSR, 1u << 13);
    pin_set(PORT_C, 13, 0); exti_sample();
    CHECK(*R_PR == (1u << 13));                              /* 마스크 중에도 래치 */
    world_reset(false);
    REG_WRITE(SYSCFG_EXTICR(13), 0x0020u);                   /* SYSCFG 클럭 꺼짐 → 무시 */
    CHECK(mock_peek(SYSCFG_EXTICR(13)) == 0u && REG_READ(SYSCFG_EXTICR(13)) == 0u);
    tim7_oneshot(200);                                       /* TIM7 원샷: 정확히 200 틱 */
    uint32_t t0 = g_tick, fired = 0;
    for (int i = 0; i < 400; i++) { g_tick++; tim7_tick(); if ((*T_SR & TIM_SR_UIF) && !fired) fired = g_tick - t0; }
    CHECK(fired == 200u && (*T_CR1 & TIM_CR1_CEN) == 0u);
}

static void test_irqn(void) {
    int ok = 1;
    for (unsigned l = 0; l < 16u; l++) {
        int want = l <= 4u ? 6 + (int)l : (l <= 9u ? 23 : 40);
        if (exti_line_to_irqn(l) != want) ok = 0;
    }
    CHECK(ok);
    CHECK(exti_line_to_irqn(13) == IRQ_EXTI15_10 && exti_line_to_irqn(0) == IRQ_EXTI0 && exti_line_to_irqn(5) == IRQ_EXTI9_5);
    CHECK(exti_line_to_irqn(16) == -1 && exti_line_to_irqn(99) == -1);
}

static void test_config_exhaustive(void) {
    int cases = 0, bad_field = 0, bad_other = 0, bad_order = 0, bad_pr = 0;
    for (unsigned port = 0; port < EXTI_NPORT; port++)
    for (unsigned pin = 0; pin < 16u; pin++)
    for (unsigned edges = 1; edges <= 3u; edges++) {
        world_reset(false);
        uint32_t apb = xs32() & ~RCC_APB2ENR_SYSCFGEN, cr[4], imr = xs32() & EXTI_LINES_MASK, emr = xs32() & EXTI_LINES_MASK;
        uint32_t rt = xs32() & EXTI_LINES_MASK, ft = xs32() & EXTI_LINES_MASK, pr = xs32() & EXTI_LINES_MASK;
        if (xs32() & 1u) apb |= RCC_APB2ENR_SYSCFGEN;
        *R_APB2 = apb;
        for (unsigned k = 0; k < 4u; k++) { cr[k] = xs32() & 0x8888u; *R_EXTICR[k] = cr[k]; }   /* 니블 0 또는 8 */
        *R_IMR = imr; mock_poke(EXTI_EMR, emr); *R_RTSR = rt; *R_FTSR = ft; *R_PR = pr;
        g_exti.prev_in = exti_inputs();
        g_nlog = 0;
        int rc = exti_config(port, pin, edges);
        uint32_t bit = 1u << pin, k = pin >> 2, sh = 4u * (pin & 3u);
        cases++;
        if (rc != 0) { bad_field++; continue; }
        uint32_t want_cr = (cr[k] & ~(0xFu << sh)) | (port << sh);
        if (*R_EXTICR[k] != want_cr) bad_field++;
        if (((*R_RTSR & bit) != 0) != ((edges & 1u) != 0) || ((*R_FTSR & bit) != 0) != ((edges & 2u) != 0)) bad_field++;
        if (!(*R_IMR & bit)) bad_field++;
        if (*R_APB2 != (apb | RCC_APB2ENR_SYSCFGEN)) bad_other++;
        for (unsigned j = 0; j < 4u; j++) if (j != k && *R_EXTICR[j] != cr[j]) bad_other++;
        if ((*R_RTSR & ~bit) != (rt & ~bit) || (*R_FTSR & ~bit) != (ft & ~bit)) bad_other++;
        if ((*R_IMR & ~bit) != (imr & ~bit) || mock_peek(EXTI_EMR) != emr) bad_other++;
        if (*R_PR != (pr & ~bit)) bad_pr++;
        int i_apb = log_find('W', RCC_APB2ENR, 0), i_mask = log_find('W', EXTI_IMR, 0);
        int i_cr = log_find('W', SYSCFG_EXTICR(pin), 0), i_pr = log_find('W', EXTI_PR, 0);
        if (i_apb < 0 || i_mask < 0 || i_cr < 0 || i_pr < 0 || !(i_apb < i_mask && i_mask < i_cr && i_cr < i_pr)) bad_order++;
        else if ((g_log[i_mask].val & bit) || g_log[i_pr].val != bit) bad_order++;
        if (g_nlog == 0 || g_log[g_nlog - 1].op != 'W' || g_log[g_nlog - 1].addr != EXTI_IMR || !(g_log[g_nlog - 1].val & bit)) bad_order++;
        if (g_unexpected) bad_other++;
    }
    printf("  exti_config exhaustive: %d cases, field %d, other-bits %d, order %d, PR %d\n",
           cases, bad_field, bad_other, bad_order, bad_pr);
    CHECK(cases == 432 && bad_field == 0);
    CHECK(bad_other == 0);
    CHECK(bad_order == 0);
    CHECK(bad_pr == 0);
}

static void test_config_args(void) {
    world_reset(false);
    uint32_t n0 = g_nacc;
    CHECK(exti_config(9, 0, EXTI_BOTH) == EXTI_EINVAL);
    CHECK(exti_config(PORT_C, 16, EXTI_BOTH) == EXTI_EINVAL);
    CHECK(exti_config(PORT_C, 13, 0) == EXTI_EINVAL && exti_config(PORT_C, 13, 4) == EXTI_EINVAL);
    CHECK(g_nacc == n0);                                     /* 거부는 접근 0번 */
    CHECK(exti_config(PORT_C, 13, EXTI_FALLING) == 0);
    n0 = g_nacc;
    CHECK(exti_config(PORT_A, 13, EXTI_RISING) == EXTI_EBUSY && g_nacc == n0);   /* PA13 과 PC13 은 라인 하나 */
    CHECK(exti_config(PORT_C, 13, EXTI_BOTH) == 0 && (*R_RTSR & *R_FTSR & (1u << 13)));
    CHECK(exti_config(PORT_B, 10, EXTI_RISING) == 0 && (*R_EXTICR[2] & 0x0F00u) == 0x0100u);
    CHECK(mock_peek(SYSCFG_EXTICR(13)) == 0x0020u);          /* EXTICR4[7:4] = 2 (PC) */
}

static void test_ack_mask(void) {
    world_reset(false);
    *R_PR = 0x2021u;                                         /* 라인 0, 5, 13 펜딩 */
    g_nlog = 0;
    exti_ack(13);
    CHECK(*R_PR == 0x0021u);                                 /* 13 만 지워짐 */
    CHECK(g_nlog == 1 && g_log[0].op == 'W' && g_log[0].addr == EXTI_PR && g_log[0].val == 0x2000u);
    *R_IMR = 0x2001u;
    exti_mask(13);
    CHECK(*R_IMR == 0x0001u);
    *R_PR = 0x2000u;                                         /* 마스크 중 묵은 펜딩이 있다고 치고 */
    g_nlog = 0;
    exti_unmask(13);
    CHECK(*R_IMR == 0x2001u && (*R_PR & 0x2000u) == 0u);
    int i_pr = log_find('W', EXTI_PR, 0), i_imr = log_find('W', EXTI_IMR, 0);
    CHECK(i_pr >= 0 && i_imr > i_pr);                        /* PR 먼저, IMR 나중 */
}

static void test_phantom_and_swier(void) {
    /* PA13 = 0, PC13 = 1 (놓음). EXTICR 이 PA → PC 로 바뀌는 순간 mux 출력이 0→1 (가짜 상승 엣지) */
    sig_reset(4000, 1);
    CHECK(sim_start(RUN_EXTI, false) == 0);
    CHECK(*R_PR == 0u && (*R_IMR & (1u << 13)));
    sim_run_until(10);
    CHECK(g_isr_calls[IRQ_EXTI15_10] == 0u);                 /* 부팅 직후 가짜 인터럽트 없음 */
    REG_WRITE(EXTI_SWIER, 1u << 13);                         /* ISR 경로 자가 진단 */
    sim_run_until(600);
    CHECK(g_isr_calls[IRQ_EXTI15_10] == 1u && *R_SWIER == 0u);
    CHECK(g_nout == 0 && g_isr_calls[IRQ_TIM7] == 1u);        /* 재확인: 레벨 그대로 → 이벤트 없음 */
}

/* ---------- Part B ---------- */
static void test_exti_clean(void) {
    sig_reset(8000, 1);
    sig_edge(1000, 0, 0); sig_edge(4000, 1, 0);              /* 300 ms 누름 */
    sim_start(RUN_EXTI, false);
    sim_run_until(8000);
    CHECK(ev_count('E', 0) == 2 && ev_is('E', 0, EV_PRESS, 1200) && ev_is('E', 1, EV_RELEASE, 4200));
    CHECK(g_isr_calls[IRQ_EXTI15_10] == 2u && g_isr_calls[IRQ_TIM7] == 2u);   /* HOLD 예약은 떼는 엣지가 취소 */
    CHECK(g_dbx.n_spurious == 0u && g_unexpected == 0);
}

static void test_exti_bouncy_random(void) {
    int bad = 0, n = 0;
    uint32_t edges_total = 0, isr_total = 0;
    for (int it = 0; it < 150; it++) {
        uint32_t t0 = 500 + rnd(500), hold = 500 + rnd(6500), bp = rnd(81), br = rnd(81);
        sig_reset(t0 + hold + 1500, 1);
        sig_edge(t0, 0, bp); sig_edge(t0 + hold, 1, br);
        sim_start(RUN_EXTI, it & 1);                         /* 절반은 latch_masked 모델 */
        sim_run_until(g_sig_len - 1);
        n++;
        if (!(ev_count('E', 0) == 2 && ev_is('E', 0, EV_PRESS, t0 + DB_TICKS) && ev_is('E', 1, EV_RELEASE, t0 + hold + DB_TICKS))) bad++;
        if (g_isr_calls[IRQ_EXTI15_10] != 2u || g_dbx.n_spurious != 0u || g_storm) bad++;
        edges_total += g_pin_edges; isr_total += g_isr_calls[IRQ_EXTI15_10];
    }
    printf("  bouncy press/release x%d: pin edges %u, EXTI ISR %u, bad %d\n", n, (unsigned)edges_total, (unsigned)isr_total, bad);
    CHECK(bad == 0);
}

static void test_exti_glitch(void) {
    int ign_bad = 0, det_bad = 0;
    for (uint32_t L = 1; L <= 260; L++) {
        sig_reset(1000 + L + 700, 1);
        sig_edge(1000, 0, 0); sig_edge(1000 + L, 1, 0);
        sim_start(RUN_EXTI, false);
        sim_run_until(g_sig_len - 1);
        if (L <= DB_TICKS) { if (g_nout != 0) ign_bad++; }
        else if (!(ev_count('E', 0) == 2 && ev_is('E', 0, EV_PRESS, 1200) && ev_is('E', 1, EV_RELEASE, 1000 + L + DB_TICKS))) det_bad++;
    }
    CHECK(ign_bad == 0);                                     /* ≤ 20.0 ms 글리치 무시 */
    CHECK(det_bad == 0);                                     /* > 20.0 ms 는 누름 */
}

static void test_exti_long(void) {
    sig_reset(30000, 1);                                     /* 2 s 누름 */
    sig_edge(1000, 0, 40); sig_edge(21000, 1, 40);
    sim_start(RUN_EXTI, false); sim_run_until(29999);
    CHECK(ev_count('E', 0) == 3 && ev_is('E', 0, EV_PRESS, 1200) && ev_is('E', 1, EV_LONG, 1200 + LONG_TICKS)
          && ev_is('E', 2, EV_RELEASE, 21200));
    sig_reset(12000, 1);                                     /* 790 ms → LONG 없음 */
    sig_edge(1000, 0, 0); sig_edge(1000 + 7900, 1, 0);
    sim_start(RUN_EXTI, false); sim_run_until(11999);
    CHECK(ev_count('E', EV_LONG) == 0 && ev_count('E', 0) == 2);
    sig_reset(30000, 1);                                     /* 누른 채 +500 ms 에 2 ms 튐 → LONG 제때 1번 */
    sig_edge(1000, 0, 0); sig_edge(1200 + 5000, 1, 0); sig_edge(1200 + 5020, 0, 0); sig_edge(21000, 1, 0);
    sim_start(RUN_EXTI, false); sim_run_until(29999);
    CHECK(ev_count('E', EV_LONG) == 1 && ev_is('E', 1, EV_LONG, 1200 + LONG_TICKS) && ev_count('E', 0) == 3);
    sig_reset(30000, 1);                                     /* 마감 직전(+785 ms) 튐 → LONG 은 재확인 때 (늦게) 1번 */
    sig_edge(1000, 0, 0); sig_edge(1200 + 7850, 1, 0); sig_edge(1200 + 7860, 0, 0); sig_edge(21000, 1, 0);
    sim_start(RUN_EXTI, false); sim_run_until(29999);
    CHECK(ev_count('E', EV_LONG) == 1 && ev_is('E', 1, EV_LONG, 1200 + 7850 + DB_TICKS));
}

static void inj_press_now(void) {                            /* 훅: 지금 이 순간 버튼이 눌리고 그대로 */
    sig_fill(g_tick, 0);
    pin_set(BTN_PORT, BTN_PIN, 0);
    exti_sample();                                           /* 하드웨어는 즉시 본다 (라인은 마스크 중) */
}
static void test_masked_window(void) {
    sig_reset(9000, 1);
    sig_edge(1000, 0, 0); sig_edge(1003, 1, 0);              /* 0.3 ms 글리치 → 마스크 + 재확인 예약 */
    sim_start(RUN_EXTI, false);
    sim_run_until(1199);
    g_inj_addr = GPIO_IDR(BTN_PORT); g_inj_left = 1; g_inj_fn = inj_press_now;   /* 재확인 읽기 직후 진짜 누름 */
    sim_run_until(8999);
    CHECK(g_inj_fn == NULL);                                  /* 훅이 실제로 발동 */
    CHECK(ev_count('E', 0) == 1 && ev_is('E', 0, EV_PRESS, 1000 + 2 * DB_TICKS));
    CHECK(g_dbx.n_rearm == 1u);
}

static void test_stale_pr(void) {
    for (int latch = 0; latch <= 1; latch++) {
        sig_reset(9000, 1);
        sig_edge(1000, 0, 80); sig_edge(5000, 1, 80);
        sim_start(RUN_EXTI, latch);
        sim_run_until(8999);
        CHECK(ev_count('E', 0) == 2 && g_isr_calls[IRQ_EXTI15_10] == 2u && g_dbx.n_spurious == 0u);
    }
}

static void test_same_tick(void) {
    /* 롱프레스 HOLD 타이머 만료(1200+8000) 와 떼는 엣지가 같은 틱 → EXTI(40) 먼저, TIM7(55) 은 가짜 진입 */
    sig_reset(15000, 1);
    sig_edge(1000, 0, 0); sig_edge(1200 + LONG_TICKS, 1, 0);
    sim_start(RUN_EXTI, false); sim_run_until(14999);
    CHECK(g_dbx.n_spurious == 1u);
    CHECK(ev_count('E', EV_LONG) == 0 && ev_count('E', 0) == 2 && ev_is('E', 1, EV_RELEASE, 1200 + LONG_TICKS + DB_TICKS));
}

static void test_stuck_and_chatter(void) {
    sig_reset(100000, 1);                                    /* 눌린 채 고착 (9.9 s) */
    sig_edge(1000, 0, 60);
    sim_start(RUN_EXTI | RUN_SAMP, false); sim_run_until(99999);
    CHECK(ev_count('E', 0) == 2 && ev_count('E', EV_LONG) == 1 && g_isr_calls[IRQ_EXTI15_10] == 1u);
    CHECK(ev_count('S', 0) == 2 && ev_count('S', EV_LONG) == 1);
    sig_reset(40000, 0);                                     /* 부팅 때부터 눌림 → 3 s 에 뗌 */
    sig_edge(30000, 1, 50);
    sim_start(RUN_EXTI | RUN_SAMP, false); sim_run_until(39999);
    CHECK(ev_count('E', 0) == 1 && ev_is('E', 0, EV_RELEASE, 30000 + DB_TICKS) && ev_count('S', 0) == 1);
    sig_reset(30000, 1);                                     /* 접점 불량: 2 s 동안 매 틱 무작위 */
    for (uint32_t t = 1000; t < 21000; t++) g_sig[t] = (uint8_t)(xs32() & 1u);
    sim_start(RUN_EXTI, false); sim_run_until(29999);
    int alt = 1;
    for (int i = 0; i < g_nout; i++) if (g_out[i].type != EV_LONG && g_out[i].type != (i % 2 == 0 ? EV_PRESS : EV_RELEASE)) alt = 0;
    printf("  chatter 2 s: pin edges %u, EXTI ISR %u, TIM7 ISR %u, events %d\n", (unsigned)g_pin_edges,
           (unsigned)g_isr_calls[IRQ_EXTI15_10], (unsigned)g_isr_calls[IRQ_TIM7], g_nout);
    CHECK(g_isr_calls[IRQ_EXTI15_10] <= 20000u / DB_TICKS + 3u && g_storm == 0u);
    CHECK(alt && g_dbx.stable == 0 && g_nout > 0);           /* 끝나면 놓음으로 수렴, 이벤트는 번갈아 */
}

/* ---------- Part C ---------- */
static void test_samp_basic(void) {
    sig_reset(30000, 1);
    sig_edge(1003, 0, 70); sig_edge(21007, 1, 70);
    uint32_t lp = sig_last_edge(1003, 1100), lr = sig_last_edge(21007, 21100);   /* 샘플 PRESS ∈ [첫 엣지+90, 마지막 엣지+99] */
    sim_start(RUN_SAMP, false); sim_run_until(29999);
    const ev_t *p = ev_nth('S', 0), *l = ev_nth('S', 1), *r = ev_nth('S', 2);
    CHECK(ev_count('S', 0) == 3 && p && l && r && p->type == EV_PRESS && l->type == EV_LONG && r->type == EV_RELEASE);
    CHECK(p && p->t >= 1003u + 90u && p->t <= lp + 99u && r && r->t >= 21007u + 90u && r->t <= lr + 99u);
    CHECK(p && l && l->t - p->t == LONG_TICKS);
    CHECK(g_systick_calls == 2999u);
}

static void test_samp_glitch(void) {
    int ign_bad = 0, det_bad = 0;
    for (uint32_t L = 1; L <= 140; L++) {
        if (L > 90 && L < 100) continue;                     /* 91–99: 위상에 따라 다름 (demo_sweep.c) */
        for (uint32_t ph = 0; ph < 10; ph++) {
            uint32_t t0 = 1000 + ph;
            sig_reset(t0 + L + 300, 1);
            sig_edge(t0, 0, 0); sig_edge(t0 + L, 1, 0);
            sim_start(RUN_SAMP, false); sim_run_until(g_sig_len - 1);
            if (L <= 90) { if (g_nout) ign_bad++; }
            else if (!(ev_count('S', EV_PRESS) == 1 && ev_count('S', EV_RELEASE) == 1)) det_bad++;
        }
    }
    CHECK(ign_bad == 0);                                     /* ≤ 9 ms 는 어떤 위상이든 무시 */
    CHECK(det_bad == 0);                                     /* ≥ 10 ms 는 어떤 위상이든 검출 */
}

static void test_agree(void) {
    int bad_seq = 0, bad_te = 0, bad_ts = 0, bad_long = 0, presses = 0, longs = 0;
    for (int it = 0; it < 120; it++) {
        uint32_t pe[4], re[4], hold[4], np = 0, t = 300 + rnd(2000);
        sig_reset(SIG_MAX, 1);
        while (np < 4u) {
            uint32_t h = 500 + rnd(14500);
            if (h >= 7800u && h <= 8200u) continue;          /* 800 ms 경계 근처는 두 방식이 갈릴 수 있어 제외 */
            if (t + h + 3000u >= SIG_MAX) break;
            sig_edge(t, 0, rnd(81)); sig_edge(t + h, 1, rnd(81));
            pe[np] = t; re[np] = t + h; hold[np] = h; np++;
            t += h + 1000 + rnd(3000);
        }
        g_sig_len = t + 500;
        sim_start(RUN_EXTI | RUN_SAMP, it & 1);
        sim_run_until(g_sig_len - 1);
        int ke = 0, ks = 0;
        for (uint32_t i = 0; i < np; i++) {
            bool lg = hold[i] > LONG_TICKS;
            presses++; longs += lg;
            const ev_t *ep = ev_nth('E', ke), *sp = ev_nth('S', ks);
            if (!ep || !sp || ep->type != EV_PRESS || sp->type != EV_PRESS) { bad_seq++; break; }
            if (ep->t != pe[i] + DB_TICKS) bad_te++;
            uint32_t lp = sig_last_edge(pe[i], pe[i] + 100);
            if (sp->t < pe[i] + 90u || sp->t > lp + 99u) bad_ts++;
            ke++; ks++;
            if (lg) {
                const ev_t *el = ev_nth('E', ke), *sl = ev_nth('S', ks);
                if (!el || !sl || el->type != EV_LONG || sl->type != EV_LONG) { bad_seq++; break; }
                if (el->t - ep->t != LONG_TICKS || sl->t - sp->t != LONG_TICKS) bad_long++;
                ke++; ks++;
            }
            const ev_t *er = ev_nth('E', ke), *sr = ev_nth('S', ks);
            if (!er || !sr || er->type != EV_RELEASE || sr->type != EV_RELEASE) { bad_seq++; break; }
            if (er->t != re[i] + DB_TICKS) bad_te++;
            uint32_t lr = sig_last_edge(re[i], re[i] + 100);
            if (sr->t < re[i] + 90u || sr->t > lr + 99u) bad_ts++;
            ke++; ks++;
        }
        if (ev_count('E', 0) != ke || ev_count('S', 0) != ks || g_evq_drop) bad_seq++;
    }
    printf("  agree: 120 sessions, %d presses (%d long), seq %d, E-time %d, S-time %d, long %d\n",
           presses, longs, bad_seq, bad_te, bad_ts, bad_long);
    CHECK(bad_seq == 0);
    CHECK(bad_te == 0 && bad_ts == 0);
    CHECK(bad_long == 0 && longs > 0);
}

static void test_glitch_train(void) {
    /* 1 ms 폭 펄스가 20 ms 마다 (모터 PWM 커플링 같은 주기 잡음) — 재확인 순간이 펄스와 겹친다 */
    sig_reset(20000, 1);
    for (uint32_t t = 1000; t < 15000; t += DB_TICKS) { sig_edge(t, 0, 0); sig_edge(t + 10, 1, 0); }
    sim_start(RUN_EXTI | RUN_SAMP, false); sim_run_until(19999);
    printf("  glitch train (1 ms every 20 ms): EXTI-method events %d, sampling events %d\n", ev_count('E', 0), ev_count('S', 0));
    CHECK(ev_count('E', EV_PRESS) >= 1);                     /* 1번 보는 재확인은 속는다 */
    CHECK(ev_count('S', 0) == 0);                            /* 10 연속 샘플은 안 속는다 */
}

static uint32_t session_build(void) {                        /* 10 s 브링업 세션 (매번 같은 시드) */
    t_rng = 0x5EED0003u;
    sig_reset(100000, 1);
    sig_edge(5000, 0, 0);   sig_edge(6500, 1, 0);            /* 깨끗한 150 ms 클릭 */
    sig_edge(10000, 0, 50); sig_edge(13000, 1, 80);          /* 튀는 300 ms 클릭 */
    sig_edge(20000, 0, 60); sig_edge(35000, 1, 70);          /* 1.5 s 롱프레스 */
    sig_edge(40000, 0, 0);  sig_edge(40010, 1, 0);           /* 1 ms 글리치 */
    for (uint32_t t = 45000; t < 48000; t++) g_sig[t] = (uint8_t)(xs32() & 1u);   /* 300 ms 접점 불량 */
    sig_edge(48000, 0, 0);  sig_edge(50000, 1, 60);
    for (uint32_t k = 0; k < 5; k++) { uint32_t t = 60000 + 3000 * k; sig_edge(t, 0, 30 + 10 * k); sig_edge(t + 1200, 1, 80); }
    return 100000;
}
static void test_cost_and_log(void) {
    uint32_t len = session_build();
    sim_start(RUN_EXTI, false); sim_run_until(len - 1);
    uint32_t e_exti = g_isr_calls[IRQ_EXTI15_10], e_tim = g_isr_calls[IRQ_TIM7], edges = g_pin_edges;
    int e_ev = ev_count('E', 0);
    session_build();
    sim_start(RUN_SAMP, false); sim_run_until(len - 1);
    uint32_t s_tick = g_systick_calls; int s_ev = ev_count('S', 0);
    printf("  cost over 10 s session: pin edges %u (= naive ISR-per-edge), EXTI-method ISR %u (EXTI %u + TIM7 %u),"
           " sampling ISR %u; events E %d / S %d\n", (unsigned)edges, (unsigned)(e_exti + e_tim), (unsigned)e_exti,
           (unsigned)e_tim, (unsigned)s_tick, e_ev, s_ev);
    CHECK(e_exti + e_tim < edges && s_tick == (len - 1u) / SYSTICK_STEPS);
    CHECK(s_ev == 19 && e_ev >= s_ev);                       /* 9 클릭×2 + LONG 1. 접점 불량 구간에서 E 는 더 낸다 */
    session_build();                                         /* 브링업 로그: 두 방식 동시 */
    sim_start(RUN_EXTI | RUN_SAMP, false);
    int save = g_verbose; g_verbose = 1;
    printf("  bring-up log (first 8 events, both methods):\n");
    while (g_tick < len - 1 && g_nout < 8) sim_step();
    g_verbose = save;
    sim_run_until(len - 1);
    int ordered = 1;
    for (int i = 1; i < g_nout; i++) if (g_out[i].t < g_out[i - 1].t) ordered = 0;
    CHECK(ordered && g_evq_drop == 0u && g_nout == e_ev + s_ev);
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    printf("[Part A] EXTI registers\n");
    test_model(); test_irqn(); test_config_exhaustive(); test_config_args(); test_ack_mask(); test_phantom_and_swier();
    printf("[Part B] EXTI + one-shot timer debouncer\n");
    test_exti_clean(); test_exti_bouncy_random(); test_exti_glitch(); test_exti_long();
    test_masked_window(); test_stale_pr(); test_same_tick(); test_stuck_and_chatter();
    printf("[Part C] 1 ms sampling debouncer, comparison, cost\n");
    test_samp_basic(); test_samp_glitch(); test_agree(); test_glitch_train(); test_cost_and_log();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
