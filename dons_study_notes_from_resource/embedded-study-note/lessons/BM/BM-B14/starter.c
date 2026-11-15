/* BM-B14 하드웨어 타이머: PSC/ARR 계산과 주기 인터럽트
 *
 * Part A — tim_calc(): 타이머 클럭 f_tim 과 목표 업데이트 주파수(mHz)로 오차 최소 (PSC, ARR) 를 찾는다.
 *          f_update = f_tim / ((PSC+1)(ARR+1)),  PSC 16비트, ARR 16비트(TIM3/4) 또는 32비트(TIM2/5).
 * Part B — mock TIM3 (BM-B02 mock 축약판 + 타이머 모델): CNT 는 (PSC+1) 클럭마다 +1, CNT == ARR 다음
 *          클럭에서 0 + update event(UEV) → UIF. PSC 는 항상 preload, ARR 은 ARPE=1 일 때만 preload.
 *          EGR.UG 는 shadow 로드 + CNT/프리스케일러 리셋 + (URS=0 이면) UIF 세트.
 * Part C — 드라이버: tim_start_periodic() / TIM3_IRQHandler() / tim_set_rate() 를 mock 위에서
 *          "ISR 사이 타이머 클럭 수" 로 검증한다.
 *
 * 레지스터 주소·비트는 STM32F4 (RM0090) 기준 대표값 — 테스트의 정답은 이 파일의 mock 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 계산 표 + 클럭 트레이스)
 * 타깃: UNIT_TEST 를 빼면 REG_READ/REG_WRITE 가 volatile MMIO 접근이 된다 (드라이버 코드는 그대로).
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- MMIO seam: 드라이버는 이 두 매크로로만 레지스터를 만진다 ---------------- */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* TIM3 (16비트 범용 타이머, APB1) — RM0090 기준 대표값, RM 확인 권장 */
#define TIM3_BASE      0x40000400u
#define TIM_CR1(b)     ((b) + 0x00u)
#define TIM_DIER(b)    ((b) + 0x0Cu)
#define TIM_SR(b)      ((b) + 0x10u)
#define TIM_EGR(b)     ((b) + 0x14u)
#define TIM_CNT(b)     ((b) + 0x24u)
#define TIM_PSC(b)     ((b) + 0x28u)
#define TIM_ARR(b)     ((b) + 0x2Cu)
#define TIM_CR1_CEN    (1u << 0)   /* counter enable */
#define TIM_CR1_URS    (1u << 2)   /* 1: overflow 만 UIF/IRQ 를 만든다 (UG 는 안 만든다) */
#define TIM_CR1_ARPE   (1u << 7)   /* ARR preload enable */
#define TIM_DIER_UIE   (1u << 0)
#define TIM_SR_UIF     (1u << 0)   /* rc_w0: 0 을 쓰면 지워지고 1 을 쓰면 그대로 */
#define TIM_EGR_UG     (1u << 0)   /* w: 1 을 쓰면 update event 생성, 읽으면 0 */

/* ======================= Part A: PSC/ARR 계산 ======================= */
typedef struct { uint32_t psc_max, arr_max; } tim_limits_t;
static const tim_limits_t TIM16 = { 0xFFFFu, 0xFFFFu };       /* TIM3/TIM4 */
static const tim_limits_t TIM32 = { 0xFFFFu, 0xFFFFFFFFu };   /* TIM2/TIM5 (F4): ARR 32비트 */

typedef struct {
    uint32_t psc, arr;     /* 레지스터에 쓸 값 */
    uint64_t n;            /* (PSC+1)(ARR+1) = 한 주기의 타이머 클럭 수 */
    bool     exact;        /* n == N* (= f_tim / f_target) */
    int64_t  err_ppm;      /* 주기 오차 (n − N*)/N* × 1e6, 반올림. + = 주기가 길다 = 주파수가 낮다 */
    uint64_t act_mHz;      /* 실제 업데이트 주파수 [mHz] (반올림) */
} tim_cfg_t;

enum { TIM_OK = 0, TIM_E_ARG = -1, TIM_E_TOO_FAST = -2, TIM_E_TOO_SLOW = -3 };
#define F_TIM_MAX 1000000000u          /* 1 GHz 이하만 받는다 → 모든 중간 곱이 uint64 에 들어간다 */

/* 주어진 헬퍼: (p = PSC+1, q = ARR+1) 로 결과 칸을 채운다. F = 1000·f_tim [mHz·tick], T = 목표 [mHz] */
static void cfg_fill(tim_cfg_t *c, uint64_t F, uint64_t T, uint64_t p, uint64_t q) {
    c->psc = (uint32_t)(p - 1u);
    c->arr = (uint32_t)(q - 1u);
    c->n = p * q;
    int64_t d = (int64_t)(c->n * T) - (int64_t)F;           /* |d| ≤ F ≤ 1e12 */
    int64_t num = d * 1000000;                               /* ≤ 1e18 < 9.2e18 */
    c->err_ppm = (num >= 0 ? num + (int64_t)F / 2 : num - (int64_t)F / 2) / (int64_t)F;
    c->exact = (d == 0);
    c->act_mHz = (F + c->n / 2u) / c->n;
}

/* TODO A: 오차 최소 (PSC, ARR) 를 찾는다.
 *  - 인자: f_tim_hz 1..F_TIM_MAX, upd_mHz ≥ 1, lim->psc_max ≤ 0xFFFF, lim->arr_max ≥ 1. 아니면 TIM_E_ARG.
 *  - N* = F / T (F = 1000·f_tim_hz, T = upd_mHz). ARR = 0 이면 카운터가 멈추므로 ARR ≥ 1 → N ≥ 2.
 *    N* < 2 → TIM_E_TOO_FAST,  N* > (psc_max+1)(arr_max+1) → TIM_E_TOO_SLOW.
 *  - 순위: ① |N·T − F| 최소 (주기 오차, 정수로 정확히 비교) ② ARR 최대 (분해능) ③ PSC 최소.
 *  - 결과는 cfg_fill(out, F, T, PSC+1, ARR+1) 로 채우고 TIM_OK. */
static int tim_calc(uint32_t f_tim_hz, uint64_t upd_mHz, const tim_limits_t *lim, tim_cfg_t *out) {
    (void)f_tim_hz; (void)upd_mHz; (void)lim; (void)out; (void)cfg_fill;
    return TIM_E_ARG; /* TODO */
}

/* ======================= Part B: mock 코어 (BM-B02 mock 축약판) ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 16
#define MOCK_LOG_CAP  64
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int g_nregs;
static mock_log_t g_log[MOCK_LOG_CAP];
static int g_nlog, g_log_dropped, g_unexpected;
static bool g_log_on = true;

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
static void mock_log(char op, uint32_t addr, uint32_t val) {
    if (!g_log_on) return;
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
    else g_log_dropped++;
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS || mock_find(addr)) return -1;
    g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return 0;
}
void mock_reset(void) { g_nregs = 0; g_nlog = 0; g_log_dropped = 0; g_unexpected = 0; }
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; mock_log('?', addr, 0u); return 0u; }
    uint32_t v = g->r ? g->r(addr, g->val, g->ctx) : g->val;
    mock_log('R', addr, v);
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; mock_log('?', addr, val); return; }
    g->val = g->w ? g->w(addr, g->val, val, g->ctx) : val;
    mock_log('W', addr, val);
}
/* 하드웨어 쪽(모델)이 저장값을 직접 보는 포인터 — 로그에 남지 않는다 */
static uint32_t *hw_reg(uint32_t addr) { mock_reg_t *g = mock_find(addr); return g ? &g->val : NULL; }

/* ======================= Part B: 타이머 모델 ======================= */
typedef struct {
    uint32_t *cr1, *dier, *sr, *cnt, *psc, *arr;   /* 저장값 (소프트웨어가 보는 preload 레지스터) */
    uint32_t psc_sh, arr_sh;   /* shadow = 실제로 카운터를 움직이는 값 */
    uint32_t pc;               /* 프리스케일러 카운터 0..psc_sh */
    uint32_t cnt_mask;         /* 16비트 0xFFFF, 32비트 0xFFFFFFFF */
    uint64_t clk;              /* enable 이후가 아니라 모델 생성 이후 흐른 타이머 클럭 수 */
    uint32_t uev;              /* update event 횟수 (overflow + UG) */
    uint32_t isr_calls;
    void (*isr)(void);         /* NVIC 대신: UIF && UIE 이면 매 클럭 호출 */
} tim_model_t;
static tim_model_t g_tim;

/* TODO B1: update event (UEV). overflow 와 EGR.UG 둘 다 여기로 온다.
 *  - preload → shadow: psc_sh = *psc, arr_sh = *arr (ARPE=0 이면 이미 같은 값)
 *  - CNT = 0, 프리스케일러 카운터 pc = 0, uev++
 *  - UIF 세트. 단 by_ug 이고 CR1.URS = 1 이면 세트하지 않는다. */
static void tim_update_event(bool by_ug) {
    (void)by_ug; /* TODO */
}

/* TODO B2: 타이머 클럭 1개. clk++ 후,
 *  - CEN=1 이고 arr_sh != 0 이면: pc == psc_sh → pc = 0 이고 카운터 한 칸, 아니면 pc++.
 *    카운터 한 칸: CNT == arr_sh → tim_update_event(false), 아니면 CNT = (CNT+1) & cnt_mask.
 *  - 그 다음 (CEN 과 무관하게) UIF && UIE && isr 이면 isr_calls++ 후 isr() 호출. */
static void tim_clock_once(void) {
    g_tim.clk++; /* TODO: 프리스케일러 → 카운터 → overflow 시 UEV, 그 다음 UIF && UIE 면 ISR */
}
static void tim_run(uint64_t clocks) { while (clocks--) tim_clock_once(); }

/* 주어진 훅: 레지스터별 접근 규칙 */
static uint32_t on_w_psc(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)o; (void)c; return v & 0xFFFFu; }
static uint32_t on_w_arr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    v &= g_tim.cnt_mask;
    if (!(*g_tim.cr1 & TIM_CR1_ARPE)) g_tim.arr_sh = v;          /* preload 꺼짐 → 즉시 반영 */
    return v;
}
static uint32_t on_w_sr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)c; return o & v; } /* rc_w0 */
static uint32_t on_w_egr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    if (v & TIM_EGR_UG) tim_update_event(true);
    return 0u;                                                     /* UG 는 하드웨어가 바로 0 으로 */
}
static uint32_t on_w_cnt(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)o; (void)c; return v & g_tim.cnt_mask; }

static void tim_model_init(uint32_t base, uint32_t cnt_mask) {
    mock_reset();
    memset(&g_tim, 0, sizeof g_tim);
    g_tim.cnt_mask = cnt_mask;
    mock_add_reg(TIM_CR1(base), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_DIER(base), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_SR(base), 0u, NULL, on_w_sr, NULL);
    mock_add_reg(TIM_EGR(base), 0u, NULL, on_w_egr, NULL);
    mock_add_reg(TIM_CNT(base), 0u, NULL, on_w_cnt, NULL);
    mock_add_reg(TIM_PSC(base), 0u, NULL, on_w_psc, NULL);
    mock_add_reg(TIM_ARR(base), cnt_mask, NULL, on_w_arr, NULL);   /* 리셋값 ARR = 최대 (모델) */
    g_tim.cr1 = hw_reg(TIM_CR1(base)); g_tim.dier = hw_reg(TIM_DIER(base));
    g_tim.sr = hw_reg(TIM_SR(base));   g_tim.cnt = hw_reg(TIM_CNT(base));
    g_tim.psc = hw_reg(TIM_PSC(base)); g_tim.arr = hw_reg(TIM_ARR(base));
    g_tim.psc_sh = 0u; g_tim.arr_sh = cnt_mask;
}

/* ======================= Part C: 드라이버 ======================= */
static void (*g_tim3_cb)(void);        /* 앱이 거는 "주기마다 할 일" */

/* TODO C1: 주기 인터럽트 시작. tim_calc 실패면 그 코드를 그대로 돌려준다 (레지스터는 건드리지 않음).
 *  순서: CEN 끄기 → PSC, ARR → ARPE → EGR.UG (shadow 로드) → UIF 지우기 (rc_w0) → UIE → CEN. */
static int tim_start_periodic(uint32_t base, uint32_t f_tim_hz, uint64_t upd_mHz, tim_cfg_t *cfg) {
    int r = tim_calc(f_tim_hz, upd_mHz, &TIM16, cfg);
    if (r != TIM_OK) return r;
    /* TODO: 흔한 첫 시도 — 무엇이 빠졌나? (첫 주기, 가짜 인터럽트) */
    REG_WRITE(TIM_PSC(base), cfg->psc);
    REG_WRITE(TIM_ARR(base), cfg->arr);
    REG_WRITE(TIM_DIER(base), TIM_DIER_UIE);
    REG_WRITE(TIM_CR1(base), TIM_CR1_ARPE | TIM_CR1_CEN);
    return TIM_OK;
}

/* TODO C2: ISR. UIF 가 서 있으면 지우고 (rc_w0: UIF 자리만 0 을 쓴다) 콜백 호출. */
static void TIM3_IRQHandler(void) {
    if (REG_READ(TIM_SR(TIM3_BASE)) & TIM_SR_UIF) {
        /* TODO: 플래그를 지우지 않으면? */
        if (g_tim3_cb) g_tim3_cb();
    }
}

/* TODO C3: 동작 중 주기 변경. 지금 주기는 옛 값으로 끝나고, 다음 주기부터 새 PSC/ARR 이 "함께" 적용돼야 한다.
 *  (UG 를 쓰면 지금 주기가 잘린다 → 쓰지 않는다.) ARPE 를 보장한 뒤 PSC, ARR 을 쓴다. */
static int tim_set_rate(uint32_t base, uint32_t f_tim_hz, uint64_t upd_mHz, tim_cfg_t *cfg) {
    (void)base; (void)f_tim_hz; (void)upd_mHz; (void)cfg;
    return TIM_E_ARG; /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                       /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 기준 1: 모든 PSC 를 끝까지 훑는다 (조기 종료 없음). 같은 순위 규칙. */
static int ref_sweep(uint32_t f, uint64_t T, const tim_limits_t *lim, uint32_t *psc, uint32_t *arr) {
    const uint64_t F = 1000u * (uint64_t)f, pmax = lim->psc_max + 1ull, qmax = lim->arr_max + 1ull;
    if (F < 2u * T) return TIM_E_TOO_FAST;
    if ((F - 1u) / T >= pmax * qmax) return TIM_E_TOO_SLOW;
    uint64_t be = UINT64_MAX, bp = 0, bq = 0;
    for (uint64_t p = 1; p <= pmax; p++) {
        uint64_t q0 = F / (T * p);
        uint64_t cand[3] = { q0, q0 + 1u, q0 + 2u };       /* 일부러 하나 더 본다 */
        for (int k = 0; k < 3; k++) {
            uint64_t q = cand[k] < 2u ? 2u : (cand[k] > qmax ? qmax : cand[k]);
            uint64_t nt = p * q * T, e = nt > F ? nt - F : F - nt;
            if (e < be || (e == be && (q > bq || (q == bq && p < bp)))) { be = e; bp = p; bq = q; }
        }
    }
    *psc = (uint32_t)(bp - 1u); *arr = (uint32_t)(bq - 1u);
    return TIM_OK;
}
/* 기준 2: 작은 타이머(8비트)에서 (p, q) 쌍을 전부 본다 — 진짜 brute force */
static void ref_brute8(uint32_t f, uint64_t T, uint32_t *psc, uint32_t *arr) {
    const uint64_t F = 1000u * (uint64_t)f;
    uint64_t be = UINT64_MAX, bp = 0, bq = 0;
    for (uint64_t p = 1; p <= 256u; p++)
        for (uint64_t q = 2; q <= 256u; q++) {
            uint64_t nt = p * q * T, e = nt > F ? nt - F : F - nt;
            if (e < be || (e == be && (q > bq || (q == bq && p < bp)))) { be = e; bp = p; bq = q; }
        }
    *psc = (uint32_t)(bp - 1u); *arr = (uint32_t)(bq - 1u);
}

#define MAX_EV 64
static uint64_t g_ev[MAX_EV];
static int g_nev;
static void on_update(void) { if (g_nev < MAX_EV) g_ev[g_nev] = g_tim.clk; g_nev++; }
static void isr_ok(void) { REG_WRITE(TIM_SR(TIM3_BASE), ~TIM_SR_UIF); on_update(); }   /* 테스트용 정상 ISR */
static void isr_no_clear(void) { on_update(); }      /* 결함 주입: UIF 를 안 지우는 ISR */
static int log_find(char op, uint32_t addr, uint32_t mask, uint32_t want) {
    for (int i = 0; i < g_nlog; i++)
        if (g_log[i].op == op && g_log[i].addr == addr && (g_log[i].val & mask) == want) return i;
    return -1;
}
/* 레지스터만으로 TIM3 를 손으로 설정 (Part B 결함 재현용): PSC=83, ARR=999 → 84 MHz 에서 1 kHz */
static void manual_setup(bool arpe, bool ug, bool clear_uif, void (*isr)(void)) {
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = isr; g_nev = 0;
    REG_WRITE(TIM_PSC(TIM3_BASE), 83u);
    if (arpe) REG_WRITE(TIM_CR1(TIM3_BASE), TIM_CR1_ARPE);
    REG_WRITE(TIM_ARR(TIM3_BASE), 999u);
    if (ug) REG_WRITE(TIM_EGR(TIM3_BASE), TIM_EGR_UG);
    if (clear_uif) REG_WRITE(TIM_SR(TIM3_BASE), ~TIM_SR_UIF);
    REG_WRITE(TIM_DIER(TIM3_BASE), TIM_DIER_UIE);
    REG_WRITE(TIM_CR1(TIM3_BASE), REG_READ(TIM_CR1(TIM3_BASE)) | TIM_CR1_CEN);
}

static void verbose_dump(void) {
    static const struct { uint32_t f; uint64_t mHz; const char *what; } V[] = {
        { 84000000u, 1000000u, "1 kHz" }, { 84000000u, 1000u, "1 Hz" }, { 84000000u, 50000u, "50 Hz" },
        { 84000000u, 44100000u, "44.1 kHz" }, { 84000000u, 13000u, "13 Hz" }, { 84000000u, 7000u, "7 Hz" },
        { 84000000u, 10u, "0.01 Hz" }, { 84000000u, 42000000000ull, "42 MHz" }, { 84000000u, 50000000000ull, "50 MHz" },
        { 16000000u, 1000u, "1 Hz" }, { 72000000u, 3000u, "3 Hz" }, { 1000003u, 1000u, "1 Hz" },
    };
    printf("f_tim[Hz]   target    ret  PSC    ARR         N           exact err_ppm  actual[mHz]\n");
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        tim_cfg_t c; memset(&c, 0, sizeof c);
        int r = tim_calc(V[i].f, V[i].mHz, &TIM16, &c);
        if (r) { printf("%-11u %-9s %3d  -\n", V[i].f, V[i].what, r); continue; }
        printf("%-11u %-9s %3d  %-6u %-11u %-11llu %-5d %-8lld %llu\n", V[i].f, V[i].what, r, c.psc, c.arr,
               (unsigned long long)c.n, c.exact, (long long)c.err_ppm, (unsigned long long)c.act_mHz);
    }
    printf("\ntrace PSC=1 ARR=2 (UG, CEN): clk pc CNT UIF\n");
    tim_model_init(TIM3_BASE, 0xFFFFu);
    REG_WRITE(TIM_PSC(TIM3_BASE), 1u); REG_WRITE(TIM_ARR(TIM3_BASE), 2u);
    REG_WRITE(TIM_EGR(TIM3_BASE), TIM_EGR_UG); REG_WRITE(TIM_SR(TIM3_BASE), ~TIM_SR_UIF);
    REG_WRITE(TIM_CR1(TIM3_BASE), TIM_CR1_CEN);
    for (int i = 0; i < 8; i++) {
        tim_clock_once();
        printf("%3llu %2u %3u %3u\n", (unsigned long long)g_tim.clk, g_tim.pc, *g_tim.cnt, *g_tim.sr & 1u);
        if (*g_tim.sr & 1u) *g_tim.sr = 0u;
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) verbose_dump();
    tim_cfg_t c;

    /* ---- Part A: 고정 벡터 (84 MHz = APB1 42 MHz × 2, BM-B10) ---- */
    memset(&c, 0, sizeof c);
    CHECK(tim_calc(84000000u, 1000000u, &TIM16, &c) == TIM_OK);           /* 1 kHz */
    CHECK(c.psc == 1u && c.arr == 41999u && c.n == 84000u && c.exact && c.err_ppm == 0);
    CHECK(tim_calc(84000000u, 1000u, &TIM16, &c) == TIM_OK);              /* 1 Hz */
    CHECK(c.psc == 1343u && c.arr == 62499u && c.exact && c.act_mHz == 1000u);
    CHECK(tim_calc(84000000u, 50000u, &TIM16, &c) == TIM_OK);             /* 50 Hz */
    CHECK(c.psc == 27u && c.arr == 59999u && c.exact);
    CHECK(tim_calc(84000000u, 44100000u, &TIM16, &c) == TIM_OK);          /* 44.1 kHz: 1904.76 클럭 */
    CHECK(c.psc == 0u && c.arr == 1904u && !c.exact && c.err_ppm == 125 && c.act_mHz == 44094488u);
    CHECK(tim_calc(84000000u, 13000u, &TIM16, &c) == TIM_OK);             /* 13 Hz: N* = 6461538.46 */
    CHECK(!c.exact && c.n == 6461538u && c.psc == 502u && c.arr == 12845u && c.err_ppm == 0);
    CHECK(tim_calc(1000003u, 1000u, &TIM16, &c) == TIM_OK);               /* N* = 1000003 (소수) */
    CHECK(!c.exact && c.n == 1000004u && c.psc == 52u && c.arr == 18867u && c.err_ppm == 1);
    CHECK(tim_calc(84000000u, 42000000000ull, &TIM16, &c) == TIM_OK);     /* 42 MHz = f_tim/2: 가장 빠름 */
    CHECK(c.psc == 0u && c.arr == 1u && c.exact);
    CHECK(tim_calc(84000000u, 50000000000ull, &TIM16, &c) == TIM_E_TOO_FAST);
    CHECK(tim_calc(84000000u, 10u, &TIM16, &c) == TIM_E_TOO_SLOW);        /* 0.01 Hz: N* = 8.4e9 > 2^32 */
    CHECK(tim_calc(84000000u, 10u, &TIM32, &c) == TIM_OK && c.exact && c.psc == 1u && c.arr == 4199999999u);
    CHECK(tim_calc(0u, 1000u, &TIM16, &c) == TIM_E_ARG && tim_calc(84000000u, 0u, &TIM16, &c) == TIM_E_ARG);
    CHECK(tim_calc(84000000u, 1000u, NULL, &c) == TIM_E_ARG);
    /* 경계: N* 가 정확히 Nmax = 2^32 (536870912 Hz / 0.125 Hz) → 가능, 조금이라도 넘으면 TOO_SLOW */
    CHECK(tim_calc(536870912u, 125u, &TIM16, &c) == TIM_OK && c.psc == 65535u && c.arr == 65535u && c.exact);
    CHECK(tim_calc(536870912u, 124u, &TIM16, &c) == TIM_E_TOO_SLOW);        /* N* = 2^32 · 125/124 */
    CHECK(tim_calc(1000000000u, 232u, &TIM16, &c) == TIM_E_TOO_SLOW);       /* N* = 4.31e9 > 2^32 */
    CHECK(tim_calc(1000000000u, 233u, &TIM16, &c) == TIM_OK);               /* N* = 4.29e9 ≤ 2^32 */

    /* ---- Part A: property — 16비트, 무작위 목표, 모든 PSC 를 훑는 기준과 비교 ---- */
    {
        static const uint32_t FT[] = { 84000000u, 168000000u, 16000000u, 72000000u, 48000000u, 1000003u };
        int bad = 0;
        for (int i = 0; i < 300; i++) {
            uint32_t f = (i % 7 == 6) ? 1000000u + xs32() % 200000000u : FT[i % 6];
            uint64_t F = 1000ull * f, nstar = 2u + ((uint64_t)xs32() << 32 | xs32()) % 4294967294ull;
            if ((i & 3) == 0) nstar = 2u + xs32() % 70000u;              /* 작은 N* 도 충분히 */
            uint64_t T = F / nstar; if (T == 0) T = 1;
            uint32_t rp = 0, ra = 0; memset(&c, 0, sizeof c);
            int r1 = tim_calc(f, T, &TIM16, &c), r2 = ref_sweep(f, T, &TIM16, &rp, &ra);
            if (r1 != r2 || (r1 == TIM_OK && (c.psc != rp || c.arr != ra))) bad++;
        }
        CHECK(bad == 0);
    }
    /* ---- Part A: property — 8비트 타이머, (p, q) 전부 보는 brute force 와 비교 ---- */
    {
        const tim_limits_t T8 = { 255u, 255u };
        int bad = 0;
        for (int i = 0; i < 300; i++) {
            uint32_t f = 1000000u + xs32() % 100000000u;
            uint64_t F = 1000ull * f, nstar = 2u + xs32() % 65535u;      /* N* ∈ [2, 65536] */
            uint64_t T = F / nstar;
            if (F < 2u * T || (F - 1u) / T >= 65536u) continue;          /* 정책상 거절 구간은 건너뜀 */
            uint32_t rp = 0, ra = 0; memset(&c, 0, sizeof c);
            ref_brute8(f, T, &rp, &ra);
            if (tim_calc(f, T, &T8, &c) != TIM_OK || c.psc != rp || c.arr != ra) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- Part B: 모델 — 결함 재현 (PSC=83, ARR=999 를 레지스터로 직접 설정) ---- */
    manual_setup(false, false, false, isr_ok);           /* ARPE=0, UG 없음 */
    tim_run(200000u);
    CHECK(g_nev >= 2 && g_ev[0] == 1000u && g_ev[1] == 85000u);   /* 첫 주기만 PSC=0 으로 1000 클럭 */
    manual_setup(true, false, false, isr_ok);            /* ARPE=1, UG 없음 */
    tim_run(200000u);
    CHECK(g_nev >= 2 && g_ev[0] == 65536u && g_ev[1] == 149536u); /* 첫 주기 = 리셋값 ARR 0xFFFF, PSC 0 */
    manual_setup(true, true, false, isr_ok);             /* UG 했지만 UIF 안 지움 */
    tim_run(200000u);
    CHECK(g_nev >= 2 && g_ev[0] == 1u && g_ev[1] == 84000u);      /* 가짜 첫 인터럽트 @1 클럭 */
    manual_setup(true, true, true, isr_ok);              /* 정석 */
    tim_run(3u * 84000u);
    CHECK(g_nev == 3 && g_ev[0] == 84000u && g_ev[1] == 168000u && g_ev[2] == 252000u);
    CHECK(g_tim.uev == 4u && g_unexpected == 0);                  /* UG 1 + overflow 3 */
    manual_setup(true, true, true, isr_no_clear);           /* ISR 이 UIF 를 안 지움 → 폭주 */
    tim_run(84000u + 99u);
    CHECK(g_tim.isr_calls == 100u);
    /* 레지스터 규칙: SR rc_w0, EGR 읽기 0, PSC 는 다음 UEV 까지 안 바뀜, URS=1 이면 UG 가 UIF 를 안 세움 */
    manual_setup(true, true, true, isr_ok);
    REG_WRITE(TIM_SR(TIM3_BASE), TIM_SR_UIF);
    CHECK((REG_READ(TIM_SR(TIM3_BASE)) & TIM_SR_UIF) == 0u);      /* 1 을 써도 세워지지 않는다 */
    CHECK(REG_READ(TIM_EGR(TIM3_BASE)) == 0u);
    tim_run(42000u);
    REG_WRITE(TIM_PSC(TIM3_BASE), 41u);                           /* 주기 중간에 PSC 변경 */
    tim_run(42000u + 42000u);
    CHECK(g_nev == 2 && g_ev[0] == 84000u && g_ev[1] == 126000u); /* 다음 주기부터 절반 */
    tim_model_init(TIM3_BASE, 0xFFFFu);
    REG_WRITE(TIM_CR1(TIM3_BASE), TIM_CR1_URS);
    REG_WRITE(TIM_PSC(TIM3_BASE), 7u);
    REG_WRITE(TIM_EGR(TIM3_BASE), TIM_EGR_UG);
    CHECK((REG_READ(TIM_SR(TIM3_BASE)) & TIM_SR_UIF) == 0u && g_tim.psc_sh == 7u && g_tim.uev == 1u);

    /* ---- Part C: 드라이버로 1 kHz, 50 Hz 주기 검증 (ISR 사이 클럭 수) ---- */
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = TIM3_IRQHandler; g_tim3_cb = on_update; g_nev = 0;
    CHECK(tim_start_periodic(TIM3_BASE, 84000000u, 1000000u, &c) == TIM_OK);
    {
        int u = log_find('W', TIM_EGR(TIM3_BASE), TIM_EGR_UG, TIM_EGR_UG);
        int p = log_find('W', TIM_PSC(TIM3_BASE), 0u, 0u), a = log_find('W', TIM_ARR(TIM3_BASE), 0u, 0u);
        int s = log_find('W', TIM_SR(TIM3_BASE), TIM_SR_UIF, 0u);
        int ie = log_find('W', TIM_DIER(TIM3_BASE), TIM_DIER_UIE, TIM_DIER_UIE);
        int en = log_find('W', TIM_CR1(TIM3_BASE), TIM_CR1_CEN, TIM_CR1_CEN);
        CHECK(p >= 0 && a >= 0 && u > p && u > a && s > u && ie > s && en > ie);
    }
    tim_run(10u * 84000u);
    {
        bool ok = (g_nev == 10);
        for (int i = 0; ok && i < 10; i++) ok = (g_ev[i] == (uint64_t)(i + 1) * 84000u);
        CHECK(ok);
        CHECK(g_tim.isr_calls == 10u);                 /* 폭주 없음, 가짜 인터럽트 없음 */
    }
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = TIM3_IRQHandler; g_nev = 0;
    CHECK(tim_start_periodic(TIM3_BASE, 84000000u, 50000u, &c) == TIM_OK);
    tim_run(3u * 1680000u);
    CHECK(g_nev == 3 && g_ev[0] == 1680000u && g_ev[2] == 5040000u);
    CHECK(tim_start_periodic(TIM3_BASE, 84000000u, 10u, &c) == TIM_E_TOO_SLOW);

    /* ---- Part C: 동작 중 주기 변경 1 kHz → 10 kHz (지금 주기 2/3 시점이 아니라 CNT=10000 시점) ---- */
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = TIM3_IRQHandler; g_nev = 0;
    tim_start_periodic(TIM3_BASE, 84000000u, 1000000u, &c);       /* PSC=1, ARR=41999 */
    tim_run(3u * 84000u + 20000u);                                /* CNT = 10000 */
    CHECK(tim_set_rate(TIM3_BASE, 84000000u, 10000000u, &c) == TIM_OK && c.psc == 0u && c.arr == 8399u);
    tim_run(64000u + 3u * 8400u);
    CHECK(g_nev == 7 && g_ev[3] == 336000u && g_ev[4] == 344400u && g_ev[6] == 361200u);
    /* 같은 변경을 ARPE=0 으로 하면 (결함 주입): ARR 8399 < CNT 10000 → 0xFFFF 까지 돌고 감는다 */
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = TIM3_IRQHandler; g_nev = 0;
    tim_start_periodic(TIM3_BASE, 84000000u, 1000000u, &c);
    tim_run(3u * 84000u + 20000u);
    REG_WRITE(TIM_CR1(TIM3_BASE), TIM_CR1_CEN);                   /* ARPE 끔 */
    REG_WRITE(TIM_PSC(TIM3_BASE), 0u);
    REG_WRITE(TIM_ARR(TIM3_BASE), 8399u);
    tim_run(200000u);
    CHECK(g_nev >= 4 && g_ev[3] - g_ev[2] == 20000u + 2u * (65536u - 10000u + 8400u));  /* 147872 클럭 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
