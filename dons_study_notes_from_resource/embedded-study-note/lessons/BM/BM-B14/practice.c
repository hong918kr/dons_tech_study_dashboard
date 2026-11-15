/* BM-B14 추가 연습 — 한 번에 검증
 *  연습 1: 주기(µs)로 받는 API — N* = f_tim·period_us / 1e6 을 일반 비율 F/T 로 푼다
 *  연습 2: CR1.URS = 1 로 "가짜 첫 인터럽트" 없애기 (SR 지우기 없이)
 *  연습 3: 32비트 타이머 (TIM2/TIM5) — 16비트로 불가능한 0.01 Hz, ARR > 65535 인 1 kHz
 *  연습 4: 정확히 안 나누어떨어지는 44.1 kHz — ARR 디더링(Bresenham)으로 평균 주파수를 정확히
 * 본문 solution.c 의 Part A/B 코드(BM-B02 mock 축약판 + 타이머 모델)를 그대로 담는다 (자기완결).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/* (본문 Part A 의 답) 오차 최소 (PSC, ARR) 를 찾는다.
 *  - 인자: f_tim_hz 1..F_TIM_MAX, upd_mHz ≥ 1, lim->psc_max ≤ 0xFFFF, lim->arr_max ≥ 1. 아니면 TIM_E_ARG.
 *  - N* = F / T (F = 1000·f_tim_hz, T = upd_mHz). ARR = 0 이면 카운터가 멈추므로 ARR ≥ 1 → N ≥ 2.
 *    N* < 2 → TIM_E_TOO_FAST,  N* > (psc_max+1)(arr_max+1) → TIM_E_TOO_SLOW.
 *  - 순위: ① |N·T − F| 최소 (주기 오차, 정수로 정확히 비교) ② ARR 최대 (분해능) ③ PSC 최소.
 *  - 결과는 cfg_fill(out, F, T, PSC+1, ARR+1) 로 채우고 TIM_OK. */
static int tim_calc(uint32_t f_tim_hz, uint64_t upd_mHz, const tim_limits_t *lim, tim_cfg_t *out) {
    if (!lim || !out || f_tim_hz == 0u || f_tim_hz > F_TIM_MAX || upd_mHz == 0u ||
        lim->psc_max > 0xFFFFu || lim->arr_max == 0u) return TIM_E_ARG;
    const uint64_t F = 1000u * (uint64_t)f_tim_hz, T = upd_mHz;
    const uint64_t pmax = (uint64_t)lim->psc_max + 1u, qmax = (uint64_t)lim->arr_max + 1u;
    if (F < 2u * T) return TIM_E_TOO_FAST;                        /* N* < 2 */
    if ((F - 1u) / T >= pmax * qmax) return TIM_E_TOO_SLOW;       /* F > T·Nmax ⇔ N* > Nmax */
    uint64_t best_e = UINT64_MAX, bp = 0u, bq = 0u;
    for (uint64_t p = 1u; p <= pmax; p++) {
        uint64_t q0 = F / (T * p);                                /* floor(N* / p) */
        for (uint64_t k = 0u; k < 2u; k++) {                      /* 후보: floor, ceil */
            uint64_t q = q0 + k;
            if (q < 2u) q = 2u;
            if (q > qmax) q = qmax;
            uint64_t nt = p * q * T;
            uint64_t e = nt > F ? nt - F : F - nt;
            if (e < best_e || (e == best_e && q > bq)) { best_e = e; bp = p; bq = q; }
        }
        if (best_e == 0u) break;       /* 처음 찾은 정확해 = 가장 작은 PSC = 가장 큰 ARR */
        if (q0 < 2u) break;            /* 이후 p 는 N = 2p 가 N* 에서 멀어지기만 한다 */
    }
    cfg_fill(out, F, T, bp, bq);
    return TIM_OK;
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

/* (본문 B1) update event (UEV). overflow 와 EGR.UG 둘 다 여기로 온다.
 *  - preload → shadow: psc_sh = *psc, arr_sh = *arr (ARPE=0 이면 이미 같은 값)
 *  - CNT = 0, 프리스케일러 카운터 pc = 0, uev++
 *  - UIF 세트. 단 by_ug 이고 CR1.URS = 1 이면 세트하지 않는다. */
static void tim_update_event(bool by_ug) {
    g_tim.psc_sh = *g_tim.psc;
    g_tim.arr_sh = *g_tim.arr;
    *g_tim.cnt = 0u;
    g_tim.pc = 0u;
    g_tim.uev++;
    if (!(by_ug && (*g_tim.cr1 & TIM_CR1_URS))) *g_tim.sr |= TIM_SR_UIF;
}

/* (본문 B2) 타이머 클럭 1개. clk++ 후,
 *  - CEN=1 이고 arr_sh != 0 이면: pc == psc_sh → pc = 0 이고 카운터 한 칸, 아니면 pc++.
 *    카운터 한 칸: CNT == arr_sh → tim_update_event(false), 아니면 CNT = (CNT+1) & cnt_mask.
 *  - 그 다음 (CEN 과 무관하게) UIF && UIE && isr 이면 isr_calls++ 후 isr() 호출. */
static void tim_clock_once(void) {
    g_tim.clk++;
    if ((*g_tim.cr1 & TIM_CR1_CEN) && g_tim.arr_sh != 0u) {
        if (g_tim.pc >= g_tim.psc_sh) {
            g_tim.pc = 0u;
            if (*g_tim.cnt == g_tim.arr_sh) tim_update_event(false);
            else *g_tim.cnt = (*g_tim.cnt + 1u) & g_tim.cnt_mask;
        } else {
            g_tim.pc++;
        }
    }
    if ((*g_tim.sr & TIM_SR_UIF) && (*g_tim.dier & TIM_DIER_UIE) && g_tim.isr) {
        g_tim.isr_calls++;
        g_tim.isr();
    }
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

/* ======================= 연습 1: 주기(µs) API ======================= */
/* 일반형: N* = F / T. F ≤ 1e18, T ≥ 1. 순위 규칙은 tim_calc 와 같다 (ppm 칸은 0 으로 둔다). */
static int tim_calc_ratio(uint64_t F, uint64_t T, const tim_limits_t *lim, tim_cfg_t *out) {
    if (!lim || !out || F == 0u || T == 0u || F > 1000000000000000000ull ||
        lim->psc_max > 0xFFFFu || lim->arr_max == 0u) return TIM_E_ARG;
    const uint64_t pmax = (uint64_t)lim->psc_max + 1u, qmax = (uint64_t)lim->arr_max + 1u;
    if (F / 2u < T) return TIM_E_TOO_FAST;                 /* F < 2T (F/2 로 써서 오버플로 회피) */
    if ((F - 1u) / T >= pmax * qmax) return TIM_E_TOO_SLOW;
    uint64_t best_e = UINT64_MAX, bp = 0u, bq = 0u;
    for (uint64_t p = 1u; p <= pmax; p++) {
        uint64_t q0 = F / T / p;                           /* floor(floor(F/T)/p) = floor(F/(T·p)) */
        for (uint64_t k = 0u; k < 2u; k++) {
            uint64_t q = q0 + k;
            if (q < 2u) q = 2u;
            if (q > qmax) q = qmax;
            uint64_t nt = p * q * T;
            uint64_t e = nt > F ? nt - F : F - nt;
            if (e < best_e || (e == best_e && q > bq)) { best_e = e; bp = p; bq = q; }
        }
        if (best_e == 0u || q0 < 2u) break;
    }
    memset(out, 0, sizeof *out);
    out->psc = (uint32_t)(bp - 1u); out->arr = (uint32_t)(bq - 1u);
    out->n = bp * bq; out->exact = (best_e == 0u);
    return TIM_OK;
}
static int tim_calc_period_us(uint32_t f_tim_hz, uint32_t period_us, const tim_limits_t *lim, tim_cfg_t *out) {
    if (f_tim_hz == 0u || f_tim_hz > F_TIM_MAX || period_us == 0u) return TIM_E_ARG;
    return tim_calc_ratio((uint64_t)f_tim_hz * period_us, 1000000u, lim, out);
}

/* ======================= 연습 2: URS = 1 로 시작 ======================= */
static int tim_start_periodic_urs(uint32_t base, uint32_t f_tim_hz, uint64_t upd_mHz, tim_cfg_t *cfg) {
    int r = tim_calc(f_tim_hz, upd_mHz, &TIM16, cfg);
    if (r != TIM_OK) return r;
    REG_WRITE(TIM_CR1(base), TIM_CR1_URS);                 /* UG 는 이제 UIF 를 세우지 않는다 */
    REG_WRITE(TIM_PSC(base), cfg->psc);
    REG_WRITE(TIM_ARR(base), cfg->arr);
    REG_WRITE(TIM_CR1(base), TIM_CR1_URS | TIM_CR1_ARPE);
    REG_WRITE(TIM_EGR(base), TIM_EGR_UG);
    REG_WRITE(TIM_DIER(base), TIM_DIER_UIE);
    REG_WRITE(TIM_CR1(base), TIM_CR1_URS | TIM_CR1_ARPE | TIM_CR1_CEN);
    return TIM_OK;
}

/* ======================= 연습 4: ARR 디더링 ======================= */
typedef struct { uint64_t base, rem, den, acc; } dither_t;
static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }
static void dither_init(dither_t *d, uint64_t F, uint64_t T) {
    uint64_t g = gcd64(F, T);
    F /= g; T /= g;                                        /* 44.1 kHz @ 84 MHz: 40000 / 21 */
    d->base = F / T; d->rem = F % T; d->den = T; d->acc = 0u;
}
static uint32_t dither_next_arr(dither_t *d) {           /* 다음 주기에 쓸 ARR (PSC = 0 가정) */
    d->acc += d->rem;
    if (d->acc >= d->den) { d->acc -= d->den; return (uint32_t)d->base; }       /* N = base + 1 */
    return (uint32_t)(d->base - 1u);                                            /* N = base */
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define MAX_EV 64
static uint64_t g_ev[MAX_EV];
static int g_nev;
static uint32_t g_base = TIM3_BASE;
static dither_t g_dit;
static bool g_dither_on;
static void isr(void) {
    if (REG_READ(TIM_SR(g_base)) & TIM_SR_UIF) {
        REG_WRITE(TIM_SR(g_base), ~TIM_SR_UIF);
        if (g_nev < MAX_EV) g_ev[g_nev] = g_tim.clk;
        g_nev++;
        if (g_dither_on) REG_WRITE(TIM_ARR(g_base), dither_next_arr(&g_dit));   /* 다다음 주기 값 */
    }
}

int main(void) {
    tim_cfg_t c;
    /* 연습 1 */
    CHECK(tim_calc_period_us(84000000u, 1u, &TIM16, &c) == TIM_OK && c.psc == 0u && c.arr == 83u && c.exact);
    CHECK(tim_calc_period_us(84000000u, 2500u, &TIM16, &c) == TIM_OK && c.psc == 3u && c.arr == 52499u && c.exact);
    CHECK(tim_calc_period_us(84000000u, 1000000u, &TIM16, &c) == TIM_OK && c.psc == 1343u && c.arr == 62499u);
    CHECK(tim_calc_period_us(84000000u, 100000000u, &TIM16, &c) == TIM_E_TOO_SLOW);   /* 100 s */
    CHECK(tim_calc_period_us(84000000u, 100000000u, &TIM32, &c) == TIM_OK && c.exact && c.psc == 1u && c.arr == 4199999999u);
    CHECK(tim_calc_period_us(1000000u, 1u, &TIM16, &c) == TIM_E_TOO_FAST);            /* N* = 1 */
    CHECK(tim_calc_period_us(1000000u, 2u, &TIM16, &c) == TIM_OK && c.psc == 0u && c.arr == 1u && c.exact);
    CHECK(tim_calc_period_us(84000000u, 0u, &TIM16, &c) == TIM_E_ARG);
    {   /* 같은 목표를 주파수 API 와 주기 API 로 — 결과가 같아야 한다 (1 ms, 20 ms, 2.5 ms) */
        tim_cfg_t a, b;
        CHECK(tim_calc(84000000u, 1000000u, &TIM16, &a) == TIM_OK && tim_calc_period_us(84000000u, 1000u, &TIM16, &b) == TIM_OK &&
              a.psc == b.psc && a.arr == b.arr);
        CHECK(tim_calc(84000000u, 50000u, &TIM16, &a) == TIM_OK && tim_calc_period_us(84000000u, 20000u, &TIM16, &b) == TIM_OK &&
              a.psc == b.psc && a.arr == b.arr);
        CHECK(tim_calc(84000000u, 400000u, &TIM16, &a) == TIM_OK && tim_calc_period_us(84000000u, 2500u, &TIM16, &b) == TIM_OK &&
              a.psc == b.psc && a.arr == b.arr);
    }

    /* 연습 2: URS=1 — UG 뒤 UIF=0, SR 쓰기 없이도 첫 ISR 은 정확히 한 주기 뒤 */
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = isr; g_nev = 0; g_base = TIM3_BASE; g_dither_on = false;
    CHECK(tim_start_periodic_urs(TIM3_BASE, 84000000u, 1000000u, &c) == TIM_OK);
    CHECK((*g_tim.sr & TIM_SR_UIF) == 0u && g_tim.psc_sh == 1u && g_tim.arr_sh == 41999u);
    {
        bool no_sr_write = true;
        for (int i = 0; i < g_nlog; i++) if (g_log[i].op == 'W' && g_log[i].addr == TIM_SR(TIM3_BASE)) no_sr_write = false;
        CHECK(no_sr_write);
    }
    tim_run(3u * 84000u);
    CHECK(g_nev == 3 && g_ev[0] == 84000u && g_ev[2] == 252000u && g_tim.isr_calls == 3u);  /* overflow 는 여전히 UIF */

    /* 연습 3: 32비트 타이머 */
    CHECK(tim_calc(84000000u, 1000u, &TIM32, &c) == TIM_OK && c.psc == 0u && c.arr == 83999999u);
    CHECK(tim_calc(84000000u, 10u, &TIM16, &c) == TIM_E_TOO_SLOW && tim_calc(84000000u, 10u, &TIM32, &c) == TIM_OK);
    CHECK(tim_calc(84000000u, 1000000u, &TIM32, &c) == TIM_OK && c.psc == 0u && c.arr == 83999u);   /* 16비트면 PSC=1 */
    tim_model_init(0x40000000u, 0xFFFFFFFFu);                                      /* TIM2 */
    g_tim.isr = isr; g_nev = 0; g_base = 0x40000000u;
    REG_WRITE(TIM_PSC(g_base), c.psc); REG_WRITE(TIM_ARR(g_base), c.arr);
    REG_WRITE(TIM_CR1(g_base), TIM_CR1_ARPE); REG_WRITE(TIM_EGR(g_base), TIM_EGR_UG);
    REG_WRITE(TIM_SR(g_base), ~TIM_SR_UIF); REG_WRITE(TIM_DIER(g_base), TIM_DIER_UIE);
    REG_WRITE(TIM_CR1(g_base), TIM_CR1_ARPE | TIM_CR1_CEN);
    CHECK(*g_tim.arr == 83999u);                                                   /* 16비트 마스크였다면 잘렸다 */
    tim_run(3u * 84000u);
    CHECK(g_nev == 3 && g_ev[0] == 84000u && g_ev[2] == 252000u);

    /* 연습 4: 44.1 kHz — 고정 ARR 은 +125 ppm, 디더링은 21 주기마다 정확히 40000 클럭 */
    CHECK(tim_calc(84000000u, 44100000u, &TIM16, &c) == TIM_OK && c.err_ppm == 125);
    dither_init(&g_dit, 84000000000ull, 44100000u);
    CHECK(g_dit.base == 1904u && g_dit.rem == 16u && g_dit.den == 21u);
    tim_model_init(TIM3_BASE, 0xFFFFu);
    g_tim.isr = isr; g_nev = 0; g_base = TIM3_BASE; g_dither_on = true;
    REG_WRITE(TIM_PSC(g_base), 0u);
    REG_WRITE(TIM_CR1(g_base), TIM_CR1_ARPE);
    REG_WRITE(TIM_ARR(g_base), dither_next_arr(&g_dit));      /* 1번째 주기 */
    REG_WRITE(TIM_EGR(g_base), TIM_EGR_UG);                    /* → shadow */
    REG_WRITE(TIM_ARR(g_base), dither_next_arr(&g_dit));      /* 2번째 주기 (preload) */
    REG_WRITE(TIM_SR(g_base), ~TIM_SR_UIF); REG_WRITE(TIM_DIER(g_base), TIM_DIER_UIE);
    REG_WRITE(TIM_CR1(g_base), TIM_CR1_ARPE | TIM_CR1_CEN);
    tim_run(2u * 40000u + 10u);                                /* 42 주기 */
    CHECK(g_nev == 42 && g_ev[20] == 40000u && g_ev[41] == 80000u);
    {
        bool ok = true; int n1905 = 0;
        for (int k = 1; k <= 42; k++) {                        /* 위상 오차 = ev[k-1] − k·N*,  21 배 해서 정수로 */
            int64_t ph21 = 21 * (int64_t)g_ev[k - 1] - 40000 * (int64_t)k;
            if (ph21 <= -21 || ph21 > 0) ok = false;           /* −1 < 위상 오차 ≤ 0 클럭 */
            uint64_t per = g_ev[k - 1] - (k > 1 ? g_ev[k - 2] : 0u);
            if (k <= 21 && per == 1905u) n1905++;
        }
        CHECK(ok && n1905 == 16);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
