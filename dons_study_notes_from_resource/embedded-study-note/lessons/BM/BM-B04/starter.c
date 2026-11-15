/* BM-B04 RMW 함정: BSRR set/reset 레지스터와 W1C 상태 비트
 *
 * Part A — mock 에 접근 타입 세 가지를 구현한다: W1C(rc_w1, EXTI_PR), rc_w0(TIM2_SR), BSRR(w, ODR 에 반영).
 *          드라이버: 플래그는 "그 비트만" 쓰는 한 번의 store 로 지우고, GPIO 출력은 BSRR 한 번 쓰기로 바꾼다.
 * Part B — 버그 재현: W1C 에 |= (다른 플래그까지 지움), rc_w0 에 &= (읽기~쓰기 사이에 HW 가 세운 플래그 유실),
 *          ODR |= 의 읽기·쓰기 사이 모든 지점에 ISR 을 끼워 넣어 lost update 재현 → BSRR / 임계 구역으로 고친다.
 * Part C — 펌웨어처럼: EXTI15_10 공유 핸들러, TIM2 하트비트 ISR + 메인 루프 LED 필드 쓰기 1000회 시뮬레이션.
 *
 * 레지스터 주소·비트는 RM0090(STM32F4) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: sweep 표 출력)
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 레지스터 접근 seam (BM-B02 mock 축약판) ======================= */
#ifdef UNIT_TEST                      /* 호스트 빌드: 모든 접근이 mock 훅을 지난다 */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else                                 /* 타깃 빌드: 진짜 MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* RM0090 기준 대표값 (RM 확인 권장) */
#define GPIOA_BASE    0x40020000u
#define GPIO_ODR      0x14u            /* rw, [15:0] 만 유효 */
#define GPIO_BSRR     0x18u            /* w, [31:16] BRy = reset, [15:0] BSy = set, 읽으면 0 */
#define EXTI_BASE     0x40013C00u
#define EXTI_PR       (EXTI_BASE + 0x14u)   /* rc_w1 (W1C), 라인 0..22 */
#define EXTI_PR_VALID 0x007FFFFFu
#define TIM2_BASE     0x40000000u
#define TIM_SR        0x10u            /* rc_w0: 0 을 쓰면 지워지고 1 을 쓰면 그대로 */
#define TIM_SR_UIF    (1u << 0)
#define TIM_SR_CC1IF  (1u << 1)
#define PIN(n)        (1u << (n))

/* ---- mock core: 레지스터 표 + 쓰기 훅 + 접근 로그 + 끼어들기(ISR / 하드웨어 이벤트) ---- */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* CPU 가 보는 값 */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* 새 저장값 */
typedef void (*mock_fn)(void);
typedef struct { uint32_t addr, stored; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; bool isr; uint32_t addr, val; } mock_log_t;   /* op: 'R' 'W' 'H'(하드웨어) */

#define MOCK_MAX_REGS 8
#define MOCK_LOG_CAP  32
static mock_reg_t g_reg[MOCK_MAX_REGS];
static int        g_nreg;
static mock_log_t g_log[MOCK_LOG_CAP];
static int        g_nlog;                 /* CAP 을 넘으면 더 적지 않는다 (g_nacc 는 계속 센다) */
static uint32_t   g_nacc;                 /* CPU 버스 접근 수 (R + W) */
static int        g_unexpected;           /* 표에 없는 주소 접근 — 테스트 실패로 센다, 크래시 아님 */
static struct { uint32_t at; mock_fn fn; bool maskable; } g_inj;   /* g_nacc == at 인 접근 직후 fn 실행 */
static bool       g_primask, g_pending, g_in_isr;
static mock_fn    g_pending_fn;

void mock_reset(void) {
    memset(g_reg, 0, sizeof g_reg); g_nreg = 0; g_nlog = 0; g_nacc = 0; g_unexpected = 0;
    memset(&g_inj, 0, sizeof g_inj); g_primask = g_pending = g_in_isr = false; g_pending_fn = NULL;
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
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
static void mock_run_isr(mock_fn fn) { g_in_isr = true; fn(); g_in_isr = false; }
static void mock_after_access(void) {
    if (g_inj.fn == NULL || g_in_isr || g_nacc != g_inj.at) return;
    mock_fn fn = g_inj.fn; bool maskable = g_inj.maskable;
    g_inj.fn = NULL;
    if (!maskable) { fn(); return; }                       /* 하드웨어 이벤트: PRIMASK 와 무관 */
    if (g_primask) { g_pending = true; g_pending_fn = fn; return; }   /* NVIC 펜딩으로 남는다 */
    mock_run_isr(fn);
}
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *r = mock_find(addr);
    uint32_t v = 0;
    if (r == NULL) g_unexpected++;
    else v = r->r ? r->r(addr, r->stored, r->ctx) : r->stored;
    mock_log_add('R', addr, v); g_nacc++; mock_after_access();
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (r == NULL) g_unexpected++;
    else r->stored = r->w ? r->w(addr, r->stored, val, r->ctx) : val;
    mock_log_add('W', addr, val); g_nacc++; mock_after_access();
}
/* 하드웨어 쪽 접근 (CPU 접근 수에 안 들어간다) */
static uint32_t mock_peek(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? r->stored : 0u; }
static void mock_poke(uint32_t addr, uint32_t v) { mock_reg_t *r = mock_find(addr); if (r) r->stored = v; }
static void mock_hw_set(uint32_t addr, uint32_t bits) { mock_poke(addr, mock_peek(addr) | bits); mock_log_add('H', addr, bits); }
static void mock_inject_at(uint32_t n, mock_fn fn, bool maskable) { g_inj.at = n; g_inj.fn = fn; g_inj.maskable = maskable; }
static void mock_primask_set(bool on) {
    g_primask = on;
    if (!on && g_pending) { g_pending = false; mock_run_isr(g_pending_fn); }   /* 풀리는 순간 펜딩 ISR 실행 */
}

/* 임계 구역 (C-M14, ARM-M10 참고). 타깃: CMSIS __get_PRIMASK / __disable_irq / __set_PRIMASK */
#ifdef UNIT_TEST
static uint32_t cs_enter(void) { uint32_t prev = g_primask; mock_primask_set(true); return prev; }
static void     cs_exit(uint32_t prev) { mock_primask_set(prev != 0u); }
#else
static inline uint32_t cs_enter(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
static inline void     cs_exit(uint32_t p) { __set_PRIMASK(p); }
#endif

/* ======================= Part A: 접근 타입 모델 (mock 쓰기 훅) ======================= */
static uint32_t odr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    return val & 0xFFFFu;                     /* ODR: 평범한 rw (상위 16비트 예약) */
}
/* EXTI_PR (rc_w1 = W1C): 1 을 쓴 비트만 0 이 되고, 0 을 쓴 비트는 그대로. 1 을 써도 세워지지 않는다 */
static uint32_t w1c_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    return val & EXTI_PR_VALID;               /* TODO: 지금은 평범한 rw 처럼 동작 (틀림) */
}
/* TIM_SR (rc_w0): 0 을 쓴 비트만 0 이 되고, 1 을 쓴 비트는 그대로 (소프트웨어가 1 로 세울 수 없다) */
static uint32_t rcw0_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    return val;                               /* TODO: 지금은 평범한 rw 처럼 동작 (틀림) */
}
/* BSRR (w): [15:0] 의 1 → 그 핀 ODR=1, [31:16] 의 1 → 그 핀 ODR=0, 둘 다면 set 이 이긴다. 읽으면 0 */
static uint32_t bsrr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    return val;                               /* TODO: ODR 에 반영하고 저장값은 0 (지금은 ODR 그대로) */
}

static void board_setup(void) {
    mock_reset();
    mock_add_reg(GPIOA_BASE + GPIO_ODR, 0u, NULL, odr_on_write, NULL);
    mock_add_reg(GPIOA_BASE + GPIO_BSRR, 0u, NULL, bsrr_on_write, NULL);
    mock_add_reg(EXTI_PR, 0u, NULL, w1c_on_write, NULL);
    mock_add_reg(TIM2_BASE + TIM_SR, 0u, NULL, rcw0_on_write, NULL);
}

/* ======================= Part A: 올바른 드라이버 (TODO) ======================= */
static void exti_clear_pending(uint32_t line) {            /* W1C: 그 비트 하나만 1 로 쓴다 */
    (void)line;                                             /* TODO */
}
static void tim_clear_flags(uint32_t tim_base, uint32_t flags) {   /* rc_w0: 지울 비트만 0, 나머지는 1 */
    (void)tim_base; (void)flags;                            /* TODO */
}
static void gpio_set_pins(uint32_t port, uint32_t pins) {   /* BSRR 아래 절반 */
    (void)port; (void)pins;                                 /* TODO */
}
static void gpio_reset_pins(uint32_t port, uint32_t pins) { /* BSRR 위 절반 */
    (void)port; (void)pins;                                 /* TODO */
}
/* mask 안의 핀들을 value 의 해당 비트로 — BSRR 한 번 쓰기 (읽기 없음) */
static void gpio_write_field(uint32_t port, uint32_t mask, uint32_t value) {
    (void)port; (void)mask; (void)value;                    /* TODO */
}
/* 대안: ODR RMW 를 임계 구역으로 감싼다 (cs_enter / cs_exit) */
static void odr_set_pins_cs(uint32_t port, uint32_t pins) {
    (void)port; (void)pins;                                 /* TODO */
}

/* ======================= Part B: 흔한 버그 코드 (주어짐 — 재현용) ======================= */
static void exti_clear_or_eq(uint32_t line)  { REG_WRITE(EXTI_PR, REG_READ(EXTI_PR) | PIN(line)); }    /* EXTI->PR |= bit  */
static void exti_clear_and_eq(uint32_t line) { REG_WRITE(EXTI_PR, REG_READ(EXTI_PR) & ~PIN(line)); }   /* EXTI->PR &= ~bit */
static void tim_clear_and_eq(uint32_t base, uint32_t f) {                                              /* TIM2->SR &= ~UIF */
    REG_WRITE(base + TIM_SR, REG_READ(base + TIM_SR) & ~f);
}
static void odr_set_pins_rmw(uint32_t port, uint32_t pins) {                                           /* GPIOA->ODR |= pins */
    REG_WRITE(port + GPIO_ODR, REG_READ(port + GPIO_ODR) | pins);
}
static void odr_reset_pins_rmw(uint32_t port, uint32_t pins) {
    REG_WRITE(port + GPIO_ODR, REG_READ(port + GPIO_ODR) & ~pins);
}
static void odr_write_field_rmw(uint32_t port, uint32_t mask, uint32_t value) {
    REG_WRITE(port + GPIO_ODR, (REG_READ(port + GPIO_ODR) & ~mask) | (value & mask));
}

/* ======================= Part C: EXTI15_10 공유 핸들러 (TODO) ======================= */
static uint32_t g_exti_count[16];
#define EXTI15_10_LINES 0xFC00u                 /* 라인 10..15 가 한 벡터를 공유 */
/* 흔한 버그 버전: 라인마다 PR 을 다시 읽고 |= 로 지운다 */
static void exti15_10_handler_naive(void) {
    for (uint32_t line = 10; line <= 15; line++)
        if (REG_READ(EXTI_PR) & PIN(line)) { exti_clear_or_eq(line); g_exti_count[line]++; }
}
/* 할 일: PR 을 한 번 읽어 10..15 중 펜딩인 것만 지우고 각 라인 카운트를 올린다. 다른 벡터의 라인은 건드리지 않는다 */
static void exti15_10_irq_handler(void) {
    /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static const char *reg_name(uint32_t a) {
    switch (a) {
    case GPIOA_BASE + GPIO_ODR:  return "ODR";
    case GPIOA_BASE + GPIO_BSRR: return "BSRR";
    case EXTI_PR:                return "PR";
    case TIM2_BASE + TIM_SR:     return "SR";
    default:                     return "??";
    }
}
static void print_log(void) {
    for (int i = 0; i < g_nlog; i++)
        printf(" %s%c:%s=%X", g_log[i].isr ? "isr." : (g_log[i].op == 'H' ? "hw." : ""),
               g_log[i].op == 'H' ? '|' : g_log[i].op, reg_name(g_log[i].addr), (unsigned)g_log[i].val);
}
static bool log_is(int i, char op, uint32_t addr, uint32_t val) {
    return i < g_nlog && g_log[i].op == op && g_log[i].addr == addr && g_log[i].val == val;
}

/* ---- sweep: op 의 "k 번째 버스 접근 직후" 에 inj 를 끼워 넣는다 (k = 0: op 전, k = n: op 끝난 뒤) ---- */
typedef struct {
    const char *name;
    uint32_t reg, init;      /* 검사할 레지스터와 초기값 */
    mock_fn op, inj;         /* 메인 코드, 끼어드는 것 */
    bool maskable;           /* true: ISR (PRIMASK 에 막힘), false: 하드웨어 이벤트 */
    uint32_t expect;         /* 끝났을 때 reg 의 기대값 */
} race_t;
static uint32_t sweep(const race_t *rc, uint32_t *n_out) {
    board_setup(); mock_poke(rc->reg, rc->init); rc->op();
    uint32_t n = g_nacc, bad = 0;                       /* 1) 끼어들기 없이 접근 수 n 을 센다 */
    for (uint32_t k = 0; k <= n; k++) {                 /* 2) 모든 지점에 한 번씩 */
        board_setup(); mock_poke(rc->reg, rc->init);
        if (k == 0) { if (rc->maskable) mock_run_isr(rc->inj); else rc->inj(); }
        else mock_inject_at(k, rc->inj, rc->maskable);
        rc->op();
        uint32_t got = mock_peek(rc->reg);
        if (got != rc->expect) bad |= 1u << k;
        if (g_verbose) {
            printf("  %-22s k=%u  %s=%04X %s |", rc->name, (unsigned)k, reg_name(rc->reg), (unsigned)got,
                   got == rc->expect ? "ok  " : "LOST");
            print_log(); printf("\n");
        }
    }
    if (n_out) *n_out = n;
    return bad;
}
/* sweep 용 래퍼들 */
static void hw_line11(void)      { mock_hw_set(EXTI_PR, PIN(11)); }
static void hw_cc1(void)         { mock_hw_set(TIM2_BASE + TIM_SR, TIM_SR_CC1IF); }
static void isr_set6(void)       { odr_set_pins_rmw(GPIOA_BASE, PIN(6)); }   /* ISR 안의 RMW: 메인이 끼어들 수 없다 */
static void isr_clr6(void)       { odr_reset_pins_rmw(GPIOA_BASE, PIN(6)); }
static void op_exti13_ok(void)   { exti_clear_pending(13); }
static void op_exti13_or(void)   { exti_clear_or_eq(13); }
static void op_uif_ok(void)      { tim_clear_flags(TIM2_BASE, TIM_SR_UIF); }
static void op_uif_and(void)     { tim_clear_and_eq(TIM2_BASE, TIM_SR_UIF); }
static void op_uif_and_cs(void)  { uint32_t p = cs_enter(); tim_clear_and_eq(TIM2_BASE, TIM_SR_UIF); cs_exit(p); }
static void op_set5_rmw(void)    { odr_set_pins_rmw(GPIOA_BASE, PIN(5)); }
static void op_set5_bsrr(void)   { gpio_set_pins(GPIOA_BASE, PIN(5)); }
static void op_set5_cs(void)     { odr_set_pins_cs(GPIOA_BASE, PIN(5)); }

/* ---- Part C: 메인 루프(LED 필드 pins 0..3) + TIM2 하트비트 ISR(pin 6) ---- */
typedef struct { uint32_t ops, isr_runs, main_mismatch, hb_mismatch, uif_left; } fw_stats_t;
static fw_stats_t g_fw;
static bool g_hb;
static void tim2_isr(void);
static void tim2_fire(void) { mock_hw_set(TIM2_BASE + TIM_SR, TIM_SR_UIF); tim2_isr(); }   /* 업데이트 이벤트 → IRQ */
static void tim2_isr(void) {
    tim_clear_flags(TIM2_BASE, TIM_SR_UIF);
    if (mock_peek(TIM2_BASE + TIM_SR) & TIM_SR_UIF) g_fw.uif_left++;
    g_hb = !g_hb;                                           /* ISR 이 핀 6 의 주인 — 자기 shadow 로 BSRR */
    if (g_hb) gpio_set_pins(GPIOA_BASE, PIN(6)); else gpio_reset_pins(GPIOA_BASE, PIN(6));
    g_fw.isr_runs++;
    mock_inject_at(g_nacc + 1u + xs32() % 5u, tim2_fire, true);   /* 다음 업데이트: 1~5 접근 뒤 */
}
static void fw_run(bool fixed, uint32_t seed, uint32_t iters) {
    board_setup(); memset(&g_fw, 0, sizeof g_fw); g_hb = false; g_rng = seed;
    mock_inject_at(1u + xs32() % 5u, tim2_fire, true);
    for (uint32_t i = 0; i < iters; i++) {
        uint32_t want = xs32() & 0xFu;
        if (fixed) gpio_write_field(GPIOA_BASE, 0xFu, want);
        else       odr_write_field_rmw(GPIOA_BASE, 0xFu, want);
        uint32_t odr = mock_peek(GPIOA_BASE + GPIO_ODR);
        if ((odr & 0xFu) != want) g_fw.main_mismatch++;
        if (((odr >> 6) & 1u) != (uint32_t)g_hb) g_fw.hb_mismatch++;
        g_fw.ops++;
    }
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    int unexpected_total = 0;

    /* ---------- Part A: 접근 타입 모델 ---------- */
    board_setup();
    mock_poke(EXTI_PR, PIN(13) | PIN(1) | PIN(0));                    /* 0x2003 */
    REG_WRITE(EXTI_PR, PIN(13));
    CHECK(mock_peek(EXTI_PR) == 0x0003u);                              /* W1C: 13 만 지워짐 */
    REG_WRITE(EXTI_PR, 0u);
    CHECK(mock_peek(EXTI_PR) == 0x0003u);                              /* 0 쓰기는 아무 일 없음 */
    REG_WRITE(EXTI_PR, PIN(5));
    CHECK(mock_peek(EXTI_PR) == 0x0003u);                              /* 1 을 써도 세워지지 않음 */
    mock_poke(TIM2_BASE + TIM_SR, TIM_SR_UIF | TIM_SR_CC1IF);
    REG_WRITE(TIM2_BASE + TIM_SR, ~TIM_SR_UIF);
    CHECK(mock_peek(TIM2_BASE + TIM_SR) == TIM_SR_CC1IF);              /* rc_w0: 0 쓴 UIF 만 지워짐 */
    mock_poke(TIM2_BASE + TIM_SR, 0u);
    REG_WRITE(TIM2_BASE + TIM_SR, 0xFFFFFFFFu);
    CHECK(mock_peek(TIM2_BASE + TIM_SR) == 0u);                        /* 1 쓰기로 세울 수 없음 */
    REG_WRITE(GPIOA_BASE + GPIO_BSRR, PIN(5));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0020u);                /* BS5 */
    CHECK(REG_READ(GPIOA_BASE + GPIO_BSRR) == 0u);                     /* BSRR 은 읽으면 0 */
    REG_WRITE(GPIOA_BASE + GPIO_BSRR, PIN(5 + 16));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0000u);                /* BR5 */
    REG_WRITE(GPIOA_BASE + GPIO_BSRR, PIN(5) | PIN(5 + 16));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0020u);                /* BS5 + BR5 → set 이 이긴다 */
    mock_poke(GPIOA_BASE + GPIO_ODR, 0xA5A5u);
    REG_WRITE(GPIOA_BASE + GPIO_BSRR, PIN(0 + 16) | PIN(1));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0xA5A6u);                /* 다른 핀은 그대로 */
    (void)REG_READ(0x40020099u);                                       /* 표에 없는 주소 → 카운트 */
    CHECK(g_unexpected == 1);

    /* Part A: 드라이버 — 결과 + 정확히 한 번의 쓰기 */
    board_setup(); mock_poke(EXTI_PR, 0x2003u);
    exti_clear_pending(13);
    CHECK(mock_peek(EXTI_PR) == 0x0003u && g_nlog == 1 && log_is(0, 'W', EXTI_PR, 0x2000u));
    board_setup(); mock_poke(TIM2_BASE + TIM_SR, TIM_SR_UIF | TIM_SR_CC1IF);
    tim_clear_flags(TIM2_BASE, TIM_SR_UIF);
    CHECK(mock_peek(TIM2_BASE + TIM_SR) == TIM_SR_CC1IF && g_nlog == 1 && log_is(0, 'W', TIM2_BASE + TIM_SR, 0xFFFFFFFEu));
    board_setup();
    gpio_set_pins(GPIOA_BASE, PIN(5));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0020u && g_nlog == 1 && log_is(0, 'W', GPIOA_BASE + GPIO_BSRR, 0x20u));
    gpio_reset_pins(GPIOA_BASE, PIN(5));
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0u && g_nlog == 2 && log_is(1, 'W', GPIOA_BASE + GPIO_BSRR, 0x00200000u));
    board_setup(); mock_poke(GPIOA_BASE + GPIO_ODR, 0x00FAu);
    gpio_write_field(GPIOA_BASE, 0x0Fu, 0x05u);
    CHECK(mock_peek(GPIOA_BASE + GPIO_ODR) == 0x00F5u && g_nlog == 1 && log_is(0, 'W', GPIOA_BASE + GPIO_BSRR, 0x000A0005u));
    int field_ok = 0;
    for (int i = 0; i < 2000; i++) {                                    /* 무작위 2000회: 모델식과 같고 쓰기 1번 */
        uint32_t odr = xs32() & 0xFFFFu, mask = xs32() & 0xFFFFu, val = xs32();
        board_setup(); mock_poke(GPIOA_BASE + GPIO_ODR, odr);
        gpio_write_field(GPIOA_BASE, mask, val);
        if (mock_peek(GPIOA_BASE + GPIO_ODR) == ((odr & ~mask) | (val & mask)) && g_nacc == 1u) field_ok++;
    }
    CHECK(field_ok == 2000);
    unexpected_total += g_unexpected;

    /* ---------- Part B: 버그 재현 ---------- */
    board_setup(); mock_poke(EXTI_PR, 0x2003u);
    exti_clear_or_eq(13);                                              /* PR |= bit13 → 읽은 0x2003 을 그대로 씀 */
    CHECK(mock_peek(EXTI_PR) == 0u && log_is(0, 'R', EXTI_PR, 0x2003u) && log_is(1, 'W', EXTI_PR, 0x2003u));
    board_setup(); mock_poke(EXTI_PR, 0x2003u);
    exti_clear_and_eq(13);                                             /* PR &= ~bit13 → 0x0003 을 씀: 정반대 */
    CHECK(mock_peek(EXTI_PR) == 0x2000u);

    if (g_verbose) printf("-- sweep (k = 끼어든 지점) --\n");
    uint32_t n = 0;
    race_t w1c_ok  = { "PR = bit13",       EXTI_PR, 0x2001u, op_exti13_ok, hw_line11, false, 0x0801u };
    race_t w1c_or  = { "PR |= bit13",      EXTI_PR, 0x2001u, op_exti13_or, hw_line11, false, 0x0801u };
    race_t rc0_and = { "SR &= ~UIF",       TIM2_BASE + TIM_SR, TIM_SR_UIF, op_uif_and, hw_cc1, false, TIM_SR_CC1IF };
    race_t rc0_ok  = { "SR = ~UIF",        TIM2_BASE + TIM_SR, TIM_SR_UIF, op_uif_ok, hw_cc1, false, TIM_SR_CC1IF };
    race_t rc0_cs  = { "cs{SR &= ~UIF}",   TIM2_BASE + TIM_SR, TIM_SR_UIF, op_uif_and_cs, hw_cc1, false, TIM_SR_CC1IF };
    race_t odr_set = { "ODR |= P5 (isr+P6)", GPIOA_BASE + GPIO_ODR, 0x0000u, op_set5_rmw, isr_set6, true, 0x0060u };
    race_t odr_clr = { "ODR |= P5 (isr-P6)", GPIOA_BASE + GPIO_ODR, 0x0040u, op_set5_rmw, isr_clr6, true, 0x0020u };
    race_t bsrr    = { "BSRR = P5",        GPIOA_BASE + GPIO_ODR, 0x0000u, op_set5_bsrr, isr_set6, true, 0x0060u };
    race_t cs      = { "cs{ODR |= P5}",    GPIOA_BASE + GPIO_ODR, 0x0000u, op_set5_cs, isr_set6, true, 0x0060u };
    CHECK(sweep(&w1c_ok, &n) == 0u && n == 1u);                        /* 쓰기 한 번: 창이 없다 */
    CHECK(sweep(&w1c_or, &n) == 0x7u && n == 2u);                      /* 레이스가 아니라 항상 틀림 (라인 0 소실) */
    CHECK(sweep(&rc0_and, &n) == 0x2u && n == 2u);                     /* 읽기~쓰기 사이 CC1IF → 0 으로 덮임 */
    CHECK(sweep(&rc0_ok, &n) == 0u && n == 1u);
    CHECK(sweep(&rc0_cs, &n) == 0x2u);                                 /* PRIMASK 는 하드웨어를 못 막는다 */
    CHECK(sweep(&odr_set, &n) == 0x2u && n == 2u);                     /* ISR 의 set 을 메인이 덮어씀 */
    CHECK(sweep(&odr_clr, &n) == 0x2u);                                /* ISR 의 clear 를 메인이 되살림 */
    CHECK(sweep(&bsrr, &n) == 0u && n == 1u);
    CHECK(sweep(&cs, &n) == 0u && n == 2u);
    /* 임계 구역은 이전 PRIMASK 를 복원해야 한다 (중첩 안전) */
    board_setup(); mock_primask_set(true);
    mock_inject_at(1, isr_set6, true);
    odr_set_pins_cs(GPIOA_BASE, PIN(5));
    CHECK(g_primask && g_pending && mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0020u);   /* 바깥이 막아 둔 채 */
    mock_primask_set(false);
    CHECK(!g_pending && mock_peek(GPIOA_BASE + GPIO_ODR) == 0x0060u);               /* 풀리면 ISR 실행 */
    unexpected_total += g_unexpected;

    /* ---------- Part C: EXTI15_10 공유 핸들러 ---------- */
    board_setup(); memset(g_exti_count, 0, sizeof g_exti_count);
    mock_poke(EXTI_PR, PIN(13) | PIN(10) | PIN(0));                    /* 0x2401: 라인 0 은 EXTI0 벡터 몫 */
    exti15_10_handler_naive();
    CHECK(g_exti_count[10] == 1u && g_exti_count[13] == 0u && mock_peek(EXTI_PR) == 0u);   /* 13·0 소실 재현 */
    board_setup(); memset(g_exti_count, 0, sizeof g_exti_count);
    mock_poke(EXTI_PR, 0x2401u);
    exti15_10_irq_handler();
    CHECK(g_exti_count[10] == 1u && g_exti_count[13] == 1u && mock_peek(EXTI_PR) == 0x0001u);
    CHECK(g_nlog == 2 && log_is(0, 'R', EXTI_PR, 0x2401u) && log_is(1, 'W', EXTI_PR, 0x2400u));
    board_setup(); memset(g_exti_count, 0, sizeof g_exti_count);
    mock_poke(EXTI_PR, 0x2401u);
    mock_inject_at(1, hw_line11, false);                               /* 핸들러 실행 중에 라인 11 도착 */
    exti15_10_irq_handler();
    CHECK(g_exti_count[10] == 1u && g_exti_count[13] == 1u && mock_peek(EXTI_PR) == 0x0801u);
    exti15_10_irq_handler();                                           /* 펜딩이 남아 NVIC 가 다시 부른다 */
    CHECK(g_exti_count[11] == 1u && mock_peek(EXTI_PR) == 0x0001u);
    board_setup(); memset(g_exti_count, 0, sizeof g_exti_count);
    mock_poke(EXTI_PR, 0x2401u);
    mock_inject_at(1, hw_line11, false);
    exti15_10_handler_naive();
    CHECK(g_exti_count[10] == 1u && g_exti_count[11] == 0u && g_exti_count[13] == 0u && mock_peek(EXTI_PR) == 0u);
    unexpected_total += g_unexpected;

    /* Part C: 메인 루프 + TIM2 하트비트 ISR */
    fw_run(false, 0xC0FFEEu, 1000);
    fw_stats_t naive = g_fw; unexpected_total += g_unexpected;
    fw_run(true, 0xC0FFEEu, 1000);
    fw_stats_t fixed = g_fw; unexpected_total += g_unexpected;
    printf("fw naive(ODR RMW): ops=%u isr=%u main_mismatch=%u hb_mismatch=%u uif_left=%u\n",
           (unsigned)naive.ops, (unsigned)naive.isr_runs, (unsigned)naive.main_mismatch,
           (unsigned)naive.hb_mismatch, (unsigned)naive.uif_left);
    printf("fw fixed(BSRR)   : ops=%u isr=%u main_mismatch=%u hb_mismatch=%u uif_left=%u\n",
           (unsigned)fixed.ops, (unsigned)fixed.isr_runs, (unsigned)fixed.main_mismatch,
           (unsigned)fixed.hb_mismatch, (unsigned)fixed.uif_left);
    CHECK(naive.main_mismatch == 0u && naive.hb_mismatch > 0u);       /* 하트비트가 메인 RMW 에 묻힌다 */
    CHECK(fixed.main_mismatch == 0u && fixed.hb_mismatch == 0u && fixed.isr_runs > 100u);
    CHECK(fixed.uif_left == 0u);
    CHECK(unexpected_total == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
