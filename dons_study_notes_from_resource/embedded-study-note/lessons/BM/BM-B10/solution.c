/* BM-B10 클럭 트리 계산기: PLL M/N/P와 버스 프리스케일러
 *
 * Part A — 메인 PLL 열거: HSE 와 목표 SYSCLK 이 주어지면 제약 구조체(clk_limits_t)를 만족하는
 *          (M, N, P) 를 "정확히" 전부 찾고, 각 해에 USB 48 MHz 용 Q 를 붙인 뒤 순위로 하나를 고른다.
 * Part B — 경계와 오류: HSE/SYSCLK 범위, VCO 경계(양끝 포함), 출력 버퍼 용량, 플래시 wait state
 *          (전압 범위별 step), 무작위 (HSE, 목표) 400 개에서 "열거 개수 == 레지스터 전수 탐색 개수".
 * Part C — 전체 클럭 계획: 목표 HCLK → (HSE 직결 | PLL) + AHB/APB 분주 + 타이머 클럭(x2 규칙)
 *          + FLASH 지연, 그리고 RCC_PLLCFGR / RCC_CFGR / FLASH_ACR 에 쓸 레지스터 값.
 *
 * 제약 값은 RM0090 (STM32F405/407, F42x/43x) 기준 대표값 — RM·데이터시트 확인 권장.
 * 테스트의 정답(ground truth)은 아래 clk_limits_t 프리셋 자체다. 계산은 전부 Hz 단위 정수 (float 금지).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 해 목록 출력)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MHZ(x) ((uint32_t)(x) * 1000000u)

/* ======================= 제약 = 파라미터 구조체 (테스트의 정답) ======================= */
typedef struct {
    const char *name;
    uint32_t hse_min, hse_max;   /* HSE 4..26 MHz                                         */
    uint32_t vin_min, vin_max;   /* VCO 입력 = HSE/M : 1..2 MHz (2 MHz 권장 — PLL jitter)    */
    uint32_t m_min, m_max;       /* PLLM 2..63 (6비트 필드)                                 */
    uint32_t n_min, n_max;       /* PLLN 50..432 (9비트 필드, 구형 문서 192..432)            */
    uint32_t vco_min, vco_max;   /* VCO 출력 100..432 MHz (구형 문서 192..432)               */
    uint32_t p_mask;             /* 허용 P: bit p = 1 → P = p 허용. F4 = {2,4,6,8}            */
    uint32_t q_min, q_max;       /* PLLQ 2..15                                              */
    uint32_t usb_hz;             /* PLL48CLK 48 MHz (USB OTG FS 는 정확히, SDIO/RNG 는 이하)  */
    uint32_t sysclk_max;         /* SYSCLK = HCLK 상한 168 MHz (F42x/43x over-drive 180)     */
    uint32_t pclk1_max;          /* APB1 ≤ 42 MHz (F42x 45)                                 */
    uint32_t pclk2_max;          /* APB2 ≤ 84 MHz (F42x 90)                                 */
    uint32_t ws_step;            /* 플래시 wait state 한 칸 = 30 MHz (2.7–3.6 V)             */
    uint32_t ws_max;             /* FLASH_ACR.LATENCY 최댓값 (F405/407 3비트 → 7)            */
} clk_limits_t;

#define P_2468 ((1u << 2) | (1u << 4) | (1u << 6) | (1u << 8))
static const clk_limits_t F40X = {      /* STM32F405/407, VDD 2.7–3.6 V */
    "F40x 3.3V", MHZ(4), MHZ(26), MHZ(1), MHZ(2), 2, 63, 50, 432, MHZ(100), MHZ(432),
    P_2468, 2, 15, MHZ(48), MHZ(168), MHZ(42), MHZ(84), MHZ(30), 7 };
static const clk_limits_t F40X_OLD = {  /* 구형 데이터시트: N 192..432, VCO 192..432 MHz */
    "F40x old-DS", MHZ(4), MHZ(26), MHZ(1), MHZ(2), 2, 63, 192, 432, MHZ(192), MHZ(432),
    P_2468, 2, 15, MHZ(48), MHZ(168), MHZ(42), MHZ(84), MHZ(30), 7 };
static const clk_limits_t F42X_OD = {   /* STM32F42x/43x, over-drive on: 180 / 45 / 90 MHz */
    "F42x OD", MHZ(4), MHZ(26), MHZ(1), MHZ(2), 2, 63, 50, 432, MHZ(100), MHZ(432),
    P_2468, 2, 15, MHZ(48), MHZ(180), MHZ(45), MHZ(90), MHZ(30), 15 };
static const clk_limits_t F40X_1V8 = {  /* 같은 칩, VDD 1.8–2.1 V: 20 MHz/WS, LATENCY ≤ 7 */
    "F40x 1.8V", MHZ(4), MHZ(26), MHZ(1), MHZ(2), 2, 63, 50, 432, MHZ(100), MHZ(432),
    P_2468, 2, 15, MHZ(48), MHZ(168), MHZ(42), MHZ(84), MHZ(20), 7 };

typedef struct {
    uint32_t m, n, p, q;
    uint32_t vco_hz;     /* = SYSCLK × P (정확한 해만 다루므로 정수)  */
    uint32_t sysclk_hz;
    uint32_t pll48_hz;   /* VCO / Q (내림)                           */
    bool     usb_exact;  /* VCO == 48 MHz × Q                        */
} pll_cfg_t;

enum { CLK_OK = 0, CLK_ERR_ARG = -1, CLK_ERR_HSE = -2, CLK_ERR_SYSCLK = -3,
       CLK_ERR_NOSOL = -4, CLK_ERR_FLASH = -5 };
#define PLL_MAX_SOL 256   /* M 62가지 × P 4가지 = 248 이 이론 최대 */

/* 레지스터 인코딩 표 (RM0090 기준 대표값 — RM 확인 권장). AHB 에 /32 는 없다! */
static const uint32_t AHB_DIVS[] = { 1, 2, 4, 8, 16, 64, 128, 256, 512 };
static uint32_t hpre_code(uint32_t div) {        /* RCC_CFGR.HPRE[7:4]: 0xxx=/1, 1000=/2 … 1111=/512 */
    for (uint32_t i = 0; i < sizeof AHB_DIVS / sizeof AHB_DIVS[0]; i++)
        if (AHB_DIVS[i] == div) return i == 0 ? 0u : 0x7u + i;
    return 0xFFu;                                /* 지원 안 되는 분주 (예: 32) */
}
static uint32_t ppre_code(uint32_t div) {        /* RCC_CFGR.PPRE1[12:10] / PPRE2[15:13]: 0xx=/1, 100=/2 … 111=/16 */
    switch (div) { case 1: return 0u; case 2: return 4u; case 4: return 5u; case 8: return 6u; case 16: return 7u; }
    return 0xFFu;
}
#define RCC_CFGR_SW_HSE   1u
#define RCC_CFGR_SW_PLL   2u
#define FLASH_ACR_PRFTEN  (1u << 8)
#define FLASH_ACR_ICEN    (1u << 9)
#define FLASH_ACR_DCEN    (1u << 10)

/* ======================= Part A: PLL 열거 ======================= */
/* USB 용 Q: VCO/Q == 48 MHz 인 Q 가 범위 안에 있으면 그것(exact), 아니면 48 MHz 를 넘지 않는 가장 작은 Q.
 * 그런 Q 도 없으면 0. */
static uint32_t pll_q_for(const clk_limits_t *L, uint32_t vco_hz, bool *exact) {
    *exact = false;
    if (vco_hz % L->usb_hz == 0u) {
        uint32_t q = vco_hz / L->usb_hz;
        if (q >= L->q_min && q <= L->q_max) { *exact = true; return q; }
    }
    uint32_t q = (vco_hz + L->usb_hz - 1u) / L->usb_hz;      /* ceil: VCO/q ≤ 48 MHz */
    if (q < L->q_min) q = L->q_min;
    return q <= L->q_max ? q : 0u;
}

/* 목표 SYSCLK 을 "정확히" 만드는 (M,N,P) 를 전부 센다. out[0..cap-1] 에 M 오름차순, 같은 M 에서 P 오름차순으로 채운다.
 * 반환: 해의 총 개수 (cap 보다 클 수 있음) 또는 CLK_ERR_* (<0). out == NULL && cap == 0 → 개수만. */
static int pll_enumerate(const clk_limits_t *L, uint32_t hse_hz, uint32_t target_hz,
                         pll_cfg_t *out, int cap) {
    if (!L || target_hz == 0u || cap < 0 || (cap > 0 && !out)) return CLK_ERR_ARG;
    if (hse_hz < L->hse_min || hse_hz > L->hse_max) return CLK_ERR_HSE;
    if (target_hz > L->sysclk_max) return CLK_ERR_SYSCLK;
    int count = 0;
    for (uint32_t m = L->m_min; m <= L->m_max; m++) {
        /* VCO 입력 = hse/m ∈ [vin_min, vin_max]  ⇔  vin_min·m ≤ hse ≤ vin_max·m (나눗셈 없이) */
        if ((uint64_t)L->vin_min * m > hse_hz || (uint64_t)L->vin_max * m < hse_hz) continue;
        for (uint32_t p = 1; p < 32; p++) {
            if (!((L->p_mask >> p) & 1u)) continue;
            uint64_t num = (uint64_t)target_hz * m * p;           /* hse·N = target·M·P */
            if (num % hse_hz != 0u) continue;                      /* N 이 정수가 아니면 정확한 해 없음 */
            uint64_t n = num / hse_hz;
            if (n < L->n_min || n > L->n_max) continue;
            uint64_t vco = (uint64_t)target_hz * p;                /* = hse·N/M, 정확 */
            if (vco < L->vco_min || vco > L->vco_max) continue;
            if (count < cap) {
                pll_cfg_t *c = &out[count];
                c->m = m; c->n = (uint32_t)n; c->p = p;
                c->vco_hz = (uint32_t)vco; c->sysclk_hz = target_hz;
                c->q = pll_q_for(L, c->vco_hz, &c->usb_exact);
                c->pll48_hz = c->q ? c->vco_hz / c->q : 0u;
            }
            count++;
        }
    }
    return count;
}

/* 순위: ① USB 48 MHz 정확 ② VCO 입력이 클수록(= M 작을수록, 2 MHz 에 가까움) ③ VCO 가 낮을수록(전력). 없으면 -1 */
static int pll_pick_best(const pll_cfg_t *v, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (best < 0) { best = i; continue; }
        const pll_cfg_t *a = &v[i], *b = &v[best];
        if (a->usb_exact != b->usb_exact) { if (a->usb_exact) best = i; continue; }
        if (a->m != b->m) { if (a->m < b->m) best = i; continue; }
        if (a->vco_hz < b->vco_hz) best = i;
    }
    return best;
}

/* ======================= Part B: 플래시 wait state ======================= */
/* RM0090 표: 0 < HCLK ≤ step → 0 WS, step < HCLK ≤ 2·step → 1 WS, … ⇒ WS = ceil(HCLK/step) − 1.
 * WS > ws_max 이면 이 전압에서 그 HCLK 은 불가 → -1. HCLK 0 도 -1. */
static int flash_wait_states(const clk_limits_t *L, uint32_t hclk_hz) {
    if (!L || hclk_hz == 0u || L->ws_step == 0u) return -1;
    uint32_t ws = (hclk_hz - 1u) / L->ws_step;
    return ws <= L->ws_max ? (int)ws : -1;
}

/* ======================= Part C: 전체 클럭 계획 ======================= */
/* hclk/div ≤ pclk_max 를 만족하는 가장 작은 APB 분주 {1,2,4,8,16}. 없으면 0. */
static uint32_t apb_div_for(uint32_t hclk_hz, uint32_t pclk_max) {
    for (uint32_t d = 1; d <= 16; d <<= 1)
        if ((uint64_t)hclk_hz <= (uint64_t)pclk_max * d) return d;
    return 0u;
}

/* RCC_PLLCFGR: PLLM[5:0] | PLLN[14:6] | PLLP[17:16] (00=/2 01=/4 10=/6 11=/8) | PLLSRC[22] (1=HSE) | PLLQ[27:24] */
static uint32_t rcc_pllcfgr_word(const pll_cfg_t *c, bool src_hse) {
    return c->m | (c->n << 6) | (((c->p / 2u) - 1u) << 16) | ((src_hse ? 1u : 0u) << 22) | (c->q << 24);
}

/* RCC_CFGR 의 SW[1:0] | HPRE[7:4] | PPRE1[12:10] | PPRE2[15:13]. 지원 안 되는 분주면 0xFFFFFFFF. */
static uint32_t rcc_cfgr_word(uint32_t sw, uint32_t ahb_div, uint32_t apb1_div, uint32_t apb2_div) {
    uint32_t h = hpre_code(ahb_div), p1 = ppre_code(apb1_div), p2 = ppre_code(apb2_div);
    if (h == 0xFFu || p1 == 0xFFu || p2 == 0xFFu || sw > 3u) return 0xFFFFFFFFu;
    return sw | (h << 4) | (p1 << 10) | (p2 << 13);
}

typedef struct {
    bool      use_pll;                    /* false = SYSCLK 이 HSE 직결 */
    pll_cfg_t pll;
    uint32_t  sysclk_hz, hclk_hz, pclk1_hz, pclk2_hz;
    uint32_t  tim_apb1_hz, tim_apb2_hz;   /* APB 분주 ≠ 1 이면 ×2 */
    uint32_t  ahb_div, apb1_div, apb2_div;
    int       ws;
    uint32_t  pllcfgr, cfgr, acr;         /* 레지스터에 쓸 값 */
} clk_plan_t;

/* 목표 HCLK 을 정확히 만든다: AHB 분주 d 를 작은 것부터 시도해 SYSCLK = HCLK·d 가
 * HSE 와 같으면 HSE 직결(PLL 끔), 아니면 PLL 정확해 중 best. 그 뒤 APB 분주·타이머 클럭·WS·레지스터 값. */
static int clock_plan(const clk_limits_t *L, uint32_t hse_hz, uint32_t target_hclk_hz, clk_plan_t *pl) {
    static pll_cfg_t buf[PLL_MAX_SOL];
    if (!pl) return CLK_ERR_ARG;
    memset(pl, 0, sizeof *pl);
    if (!L || target_hclk_hz == 0u) return CLK_ERR_ARG;
    if (hse_hz < L->hse_min || hse_hz > L->hse_max) return CLK_ERR_HSE;
    if (target_hclk_hz > L->sysclk_max) return CLK_ERR_SYSCLK;
    bool found = false;
    for (uint32_t i = 0; i < sizeof AHB_DIVS / sizeof AHB_DIVS[0] && !found; i++) {
        uint64_t sys = (uint64_t)target_hclk_hz * AHB_DIVS[i];
        if (sys > L->sysclk_max) break;
        if (sys == hse_hz) {                       /* PLL 없이 HSE 로 충분 — 전력·락 시간 절약 */
            pl->use_pll = false; found = true;
        } else {
            int n = pll_enumerate(L, hse_hz, (uint32_t)sys, buf, PLL_MAX_SOL);
            if (n > 0) {
                pl->pll = buf[pll_pick_best(buf, n < PLL_MAX_SOL ? n : PLL_MAX_SOL)];
                pl->use_pll = true; found = true;
            }
        }
        if (found) { pl->ahb_div = AHB_DIVS[i]; pl->sysclk_hz = (uint32_t)sys; }
    }
    if (!found) return CLK_ERR_NOSOL;
    pl->hclk_hz = target_hclk_hz;
    pl->ws = flash_wait_states(L, pl->hclk_hz);
    if (pl->ws < 0) return CLK_ERR_FLASH;
    pl->apb1_div = apb_div_for(pl->hclk_hz, L->pclk1_max);
    pl->apb2_div = apb_div_for(pl->hclk_hz, L->pclk2_max);
    if (pl->apb1_div == 0u || pl->apb2_div == 0u) return CLK_ERR_NOSOL;
    pl->pclk1_hz = pl->hclk_hz / pl->apb1_div;
    pl->pclk2_hz = pl->hclk_hz / pl->apb2_div;
    pl->tim_apb1_hz = pl->apb1_div == 1u ? pl->pclk1_hz : 2u * pl->pclk1_hz;
    pl->tim_apb2_hz = pl->apb2_div == 1u ? pl->pclk2_hz : 2u * pl->pclk2_hz;
    pl->pllcfgr = pl->use_pll ? rcc_pllcfgr_word(&pl->pll, true) : 0u;
    pl->cfgr = rcc_cfgr_word(pl->use_pll ? RCC_CFGR_SW_PLL : RCC_CFGR_SW_HSE,
                             pl->ahb_div, pl->apb1_div, pl->apb2_div);
    pl->acr = (uint32_t)pl->ws | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    return CLK_OK;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 기준(oracle): 레지스터로 표현 가능한 모든 (M 6비트, N 9비트, P 2비트) 를 무식하게 다 본다 */
static int brute_count(const clk_limits_t *L, uint32_t hse, uint32_t target) {
    int c = 0;
    for (uint32_t m = 0; m < 64; m++)
        for (uint32_t n = 0; n < 512; n++)
            for (uint32_t pp = 0; pp < 4; pp++) {
                uint32_t p = 2u * (pp + 1u);
                if (!((L->p_mask >> p) & 1u)) continue;
                if (m < L->m_min || m > L->m_max || n < L->n_min || n > L->n_max) continue;
                if ((uint64_t)hse * n != (uint64_t)target * m * p) continue;
                if ((uint64_t)hse < (uint64_t)L->vin_min * m || (uint64_t)hse > (uint64_t)L->vin_max * m) continue;
                if ((uint64_t)hse * n < (uint64_t)L->vco_min * m || (uint64_t)hse * n > (uint64_t)L->vco_max * m) continue;
                c++;
            }
    return c;
}
/* 해 하나가 모든 제약을 지키는지 독립 검사 */
static bool cfg_valid(const clk_limits_t *L, uint32_t hse, const pll_cfg_t *c) {
    uint64_t hn = (uint64_t)hse * c->n;
    return c->m >= L->m_min && c->m <= L->m_max && c->n >= L->n_min && c->n <= L->n_max
        && ((L->p_mask >> c->p) & 1u)
        && (uint64_t)L->vin_min * c->m <= hse && hse <= (uint64_t)L->vin_max * c->m
        && hn >= (uint64_t)L->vco_min * c->m && hn <= (uint64_t)L->vco_max * c->m
        && hn == (uint64_t)c->sysclk_hz * c->m * c->p
        && (uint64_t)c->vco_hz * c->m == hn
        && c->q >= L->q_min && c->q <= L->q_max && (uint64_t)c->pll48_hz * c->q <= c->vco_hz
        && c->usb_exact == ((uint64_t)L->usb_hz * c->q == c->vco_hz);
}
static bool has_cfg(const pll_cfg_t *v, int n, uint32_t m, uint32_t nn, uint32_t p, uint32_t q) {
    for (int i = 0; i < n; i++) if (v[i].m == m && v[i].n == nn && v[i].p == p && v[i].q == q) return true;
    return false;
}
static uint32_t xs_state = 0xB10C10u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}
static void print_solutions(const clk_limits_t *L, uint32_t hse, uint32_t target) {
    static pll_cfg_t v[PLL_MAX_SOL];
    int n = pll_enumerate(L, hse, target, v, PLL_MAX_SOL);
    printf("[%s] HSE %u Hz -> SYSCLK %u Hz : %d solution(s)\n", L->name, hse, target, n);
    if (n <= 0) return;
    int b = pll_pick_best(v, n);
    printf("   M    N  P   Q  VCO_in(Hz)   VCO(MHz)  PLL48(Hz)  USB\n");
    for (int i = 0; i < n && i < PLL_MAX_SOL; i++)
        printf("  %2u  %3u  %u  %2u  %9u  %9u  %9u  %s%s\n", v[i].m, v[i].n, v[i].p, v[i].q,
               hse / v[i].m, v[i].vco_hz / 1000000u, v[i].pll48_hz, v[i].usb_exact ? "48 exact" : "-",
               i == b ? "   <- best" : "");
}
static void print_plan(const char *tag, const clk_plan_t *p) {
    printf("%-10s src=%s M=%u N=%u P=%u Q=%u SYSCLK=%u HCLK=%u AHB/%u APB1/%u=%u APB2/%u=%u TIMx1=%u TIMx2=%u WS=%d"
           " PLLCFGR=0x%08X CFGR=0x%08X ACR=0x%08X\n", tag, p->use_pll ? "PLL" : "HSE",
           p->pll.m, p->pll.n, p->pll.p, p->pll.q, p->sysclk_hz, p->hclk_hz, p->ahb_div, p->apb1_div,
           p->pclk1_hz, p->apb2_div, p->pclk2_hz, p->tim_apb1_hz, p->tim_apb2_hz, p->ws,
           p->pllcfgr, p->cfgr, p->acr);
}

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    static pll_cfg_t v[PLL_MAX_SOL];
    int n, b;
    bool ex;

    /* ---------- Part A: 열거와 순위 ---------- */
    CHECK(pll_q_for(&F40X, MHZ(336), &ex) == 7u && ex);            /* 336/7 = 48 */
    CHECK(pll_q_for(&F40X, MHZ(360), &ex) == 8u && !ex);           /* 360/48 = 7.5 → Q=8 → 45 MHz */
    CHECK(pll_q_for(&F40X, MHZ(100), &ex) == 3u && !ex);           /* 33.3 MHz */
    CHECK(pll_q_for(&F40X, MHZ(432), &ex) == 9u && ex);

    n = pll_enumerate(&F40X, MHZ(8), MHZ(168), v, PLL_MAX_SOL);    /* 8 MHz → 168 MHz */
    CHECK(n == 5);
    CHECK(has_cfg(v, n, 8, 336, 2, 7));                            /* 고전적인 ST 템플릿 값 */
    CHECK(has_cfg(v, n, 4, 168, 2, 7));
    b = pll_pick_best(v, n);
    CHECK(b >= 0 && v[b].m == 4 && v[b].n == 168 && v[b].p == 2 && v[b].q == 7 && v[b].usb_exact);
    CHECK(b >= 0 && v[b].vco_hz == MHZ(336) && v[b].pll48_hz == MHZ(48));

    n = pll_enumerate(&F40X, MHZ(25), MHZ(168), v, PLL_MAX_SOL);   /* 25 MHz 크리스털 */
    CHECK(n == 1 && v[0].m == 25 && v[0].n == 336 && v[0].p == 2 && v[0].q == 7);   /* VCO 입력 1 MHz 뿐 */
    n = pll_enumerate(&F40X, MHZ(26), MHZ(168), v, PLL_MAX_SOL);   /* 26 MHz TCXO: 13 | M → M = 13, 26 */
    b = pll_pick_best(v, n);
    CHECK(n == 2 && b >= 0 && v[b].m == 13 && v[b].n == 168 && v[b].p == 2 && v[b].q == 7);

    n = pll_enumerate(&F40X, MHZ(8), MHZ(84), v, PLL_MAX_SOL);     /* P=2 (VCO 168) 와 P=4 (VCO 336) */
    b = pll_pick_best(v, n);
    CHECK(n == 10 && b >= 0 && v[b].m == 4 && v[b].n == 168 && v[b].p == 4 && v[b].q == 7);  /* USB 가 이김 */

    n = pll_enumerate(&F40X_OLD, MHZ(8), MHZ(168), v, PLL_MAX_SOL);   /* N ≥ 192 이면 M=4(N=168) 탈락 */
    b = pll_pick_best(v, n);
    CHECK(n == 4 && !has_cfg(v, n, 4, 168, 2, 7) && b >= 0 && v[b].m == 5 && v[b].n == 210);

    /* ---------- Part B: 경계·오류·용량·WS·property ---------- */
    CHECK(pll_enumerate(&F40X, MHZ(8), MHZ(180), v, PLL_MAX_SOL) == CLK_ERR_SYSCLK);   /* F40x 한계 초과 */
    n = pll_enumerate(&F42X_OD, MHZ(8), MHZ(180), v, PLL_MAX_SOL);                      /* F42x 는 가능 */
    b = pll_pick_best(v, n);
    CHECK(n == 5 && b >= 0 && v[b].m == 4 && v[b].n == 180 && v[b].q == 8 && !v[b].usb_exact);
    CHECK(b >= 0 && v[b].pll48_hz == MHZ(45));                    /* 180 MHz 에선 USB 48 MHz 불가 */
    {   int u = 0; for (int i = 0; i < n; i++) u += v[i].usb_exact;
        CHECK(n == 5 && u == 0); }                                  /* 5 개 해 전부 VCO 360 */
    CHECK(pll_enumerate(&F40X, MHZ(3), MHZ(168), v, PLL_MAX_SOL) == CLK_ERR_HSE);
    CHECK(pll_enumerate(&F40X, MHZ(27), MHZ(168), v, PLL_MAX_SOL) == CLK_ERR_HSE);
    CHECK(pll_enumerate(&F40X, MHZ(8), 0u, v, PLL_MAX_SOL) == CLK_ERR_ARG);
    CHECK(pll_enumerate(&F40X, MHZ(8), MHZ(12), v, PLL_MAX_SOL) == 0);       /* 12×8 = 96 < VCO 최소 100 */
    CHECK(pll_enumerate(&F40X, MHZ(8), 100000001u, v, PLL_MAX_SOL) == 0);    /* 정확한 해 없음 */
    n = pll_enumerate(&F40X, MHZ(8), 12500000u, v, PLL_MAX_SOL);             /* VCO = 100 MHz 경계, N = 50 경계 */
    CHECK(n == 3 && has_cfg(v, n, 4, 50, 8, 3) && has_cfg(v, n, 8, 100, 8, 3));
    n = pll_enumerate(&F40X, MHZ(8), MHZ(108), v, PLL_MAX_SOL);              /* VCO = 432 MHz 경계 (P=4) */
    CHECK(has_cfg(v, n, 4, 216, 4, 9) && has_cfg(v, n, 8, 432, 4, 9));      /* N = 432 경계 */
    CHECK(pll_enumerate(&F40X, MHZ(4), MHZ(168), v, PLL_MAX_SOL) == 3);      /* M=2(2 MHz)·3·4(1 MHz) */
    CHECK(pll_enumerate(&F40X, MHZ(26), MHZ(156), v, PLL_MAX_SOL) == brute_count(&F40X, MHZ(26), MHZ(156)));
    CHECK(pll_enumerate(&F40X, MHZ(8), MHZ(168), NULL, 0) == 5);            /* 개수만 */
    {   pll_cfg_t small[3], guard;
        memset(small, 0xAB, sizeof small); memcpy(&guard, &small[2], sizeof guard);
        CHECK(pll_enumerate(&F40X, MHZ(8), MHZ(168), small, 2) == 5);       /* 총 개수는 5 */
        CHECK(small[0].m == 4 && small[1].m == 5);                          /* 앞 2 개만 */
        CHECK(memcmp(&small[2], &guard, sizeof guard) == 0);                /* 넘쳐 쓰지 않음 */
    }
    CHECK(pll_pick_best(v, 0) == -1);

    CHECK(flash_wait_states(&F40X, MHZ(30)) == 0);
    CHECK(flash_wait_states(&F40X, MHZ(30) + 1u) == 1);
    CHECK(flash_wait_states(&F40X, MHZ(60)) == 1);
    CHECK(flash_wait_states(&F40X, MHZ(84)) == 2);
    CHECK(flash_wait_states(&F40X, MHZ(168)) == 5);
    CHECK(flash_wait_states(&F40X, 0u) == -1);
    CHECK(flash_wait_states(&F40X_1V8, MHZ(160)) == 7);
    CHECK(flash_wait_states(&F40X_1V8, MHZ(168)) == -1);                    /* 8 WS 필요 > LATENCY 최대 7 */

    {   /* property: 무작위 (HSE, 목표) 400 개 — 열거 개수 == 전수 탐색, 모든 해가 독립 검사 통과 */
        const uint32_t xtal[] = { MHZ(4), MHZ(8), MHZ(10), MHZ(12), MHZ(16), MHZ(20), MHZ(24), MHZ(25),
                                  MHZ(26), 12288000u, 24576000u, 19200000u, 14745600u, 13560000u };
        int bad_count = 0, bad_cfg = 0, total_sol = 0, nonzero = 0;
        for (int t = 0; t < 400; t++) {
            uint32_t hse = (t & 1) ? xtal[xorshift32() % (sizeof xtal / sizeof xtal[0])]
                                   : MHZ(4) + (xorshift32() % 22001u) * 1000u;   /* kHz 단위 임의 값 */
            uint32_t target;
            if (t % 4 < 2) {                       /* 해가 있을 법한 목표: 임의의 레지스터 값에서 역산 */
                target = MHZ(168);
                for (int k = 0; k < 256; k++) {    /* 나누어떨어지는 (M,N,P) 를 찾을 때까지 (유한 재시도) */
                    uint32_t m = 2u + xorshift32() % 62u, nn = 50u + xorshift32() % 383u;
                    uint32_t p = 2u * (1u + xorshift32() % 4u);
                    uint64_t num = (uint64_t)hse * nn, den = (uint64_t)m * p;
                    if (num % den == 0u && num / den <= MHZ(168)) { target = (uint32_t)(num / den); break; }
                }
            } else if (t % 4 == 2) {
                target = MHZ(1u + xorshift32() % 168u);                 /* 정수 MHz */
            } else {
                target = 1u + xorshift32() % MHZ(168);                  /* 아무 Hz */
            }
            int ne = pll_enumerate(&F40X, hse, target, v, PLL_MAX_SOL);
            if (ne != brute_count(&F40X, hse, target)) bad_count++;
            for (int i = 0; i < ne && i < PLL_MAX_SOL; i++) if (!cfg_valid(&F40X, hse, &v[i])) bad_cfg++;
            if (ne > 0) { total_sol += ne; nonzero++; }
        }
        CHECK(bad_count == 0);
        CHECK(bad_cfg == 0);
        CHECK(nonzero > 100);                       /* 테스트가 0 == 0 만 비교하지 않았다는 증거 */
        if (verbose) printf("property: 400 cases, %d with solutions, %d solutions total\n", nonzero, total_sol);
    }

    /* ---------- Part C: 전체 클럭 계획과 레지스터 값 ---------- */
    clk_plan_t pl;
    CHECK(apb_div_for(MHZ(168), MHZ(42)) == 4u && apb_div_for(MHZ(168), MHZ(84)) == 2u);
    CHECK(apb_div_for(MHZ(42), MHZ(42)) == 1u && apb_div_for(MHZ(42) + 1u, MHZ(42)) == 2u);
    CHECK(rcc_cfgr_word(RCC_CFGR_SW_PLL, 32u, 1u, 1u) == 0xFFFFFFFFu);      /* AHB /32 은 없다 */

    CHECK(clock_plan(&F40X, MHZ(8), MHZ(168), &pl) == CLK_OK);
    CHECK(pl.use_pll && pl.pll.m == 4 && pl.pll.n == 168 && pl.pll.p == 2 && pl.pll.q == 7);
    CHECK(pl.sysclk_hz == MHZ(168) && pl.ahb_div == 1u && pl.apb1_div == 4u && pl.apb2_div == 2u);
    CHECK(pl.pclk1_hz == MHZ(42) && pl.pclk2_hz == MHZ(84));
    CHECK(pl.tim_apb1_hz == MHZ(84) && pl.tim_apb2_hz == MHZ(168));         /* 분주 ≠ 1 → ×2 */
    CHECK(pl.ws == 5 && pl.acr == 0x705u);
    CHECK(pl.pllcfgr == 0x07402A04u && pl.cfgr == 0x00009402u);
    {   pll_cfg_t classic = { 8, 336, 2, 7, MHZ(336), MHZ(168), MHZ(48), true };
        CHECK(rcc_pllcfgr_word(&classic, true) == 0x07405408u);             /* ST 템플릿의 그 값 */
    }
    if (verbose) print_plan("8->168", &pl);

    CHECK(clock_plan(&F40X, MHZ(8), MHZ(84), &pl) == CLK_OK);
    CHECK(pl.pll.p == 4 && pl.apb1_div == 2u && pl.apb2_div == 1u);
    CHECK(pl.tim_apb1_hz == MHZ(84) && pl.tim_apb2_hz == MHZ(84));          /* APB2 /1 → ×1 */
    CHECK(pl.ws == 2 && pl.pllcfgr == 0x07412A04u && pl.cfgr == 0x00001002u);
    if (verbose) print_plan("8->84", &pl);

    CHECK(clock_plan(&F40X, MHZ(25), MHZ(168), &pl) == CLK_OK && pl.pllcfgr == 0x07405419u);
    if (verbose) print_plan("25->168", &pl);

    CHECK(clock_plan(&F40X, MHZ(8), MHZ(8), &pl) == CLK_OK);                /* HSE 직결, PLL 불필요 */
    CHECK(!pl.use_pll && pl.cfgr == 0x00000001u && pl.ws == 0 && pl.tim_apb1_hz == MHZ(8));
    CHECK(clock_plan(&F40X, MHZ(8), MHZ(1), &pl) == CLK_OK);                /* HSE + AHB /8 */
    CHECK(!pl.use_pll && pl.ahb_div == 8u && pl.sysclk_hz == MHZ(8) && pl.cfgr == 0x000000A1u);
    if (verbose) print_plan("8->1", &pl);
    CHECK(clock_plan(&F40X, MHZ(8), MHZ(12), &pl) == CLK_OK);               /* PLL 24 MHz + AHB /2 */
    CHECK(pl.use_pll && pl.ahb_div == 2u && pl.sysclk_hz == MHZ(24) && pl.pll.p == 6 && pl.pll.q == 3);
    if (verbose) print_plan("8->12", &pl);

    CHECK(clock_plan(&F40X, MHZ(8), MHZ(180), &pl) == CLK_ERR_SYSCLK);
    CHECK(clock_plan(&F42X_OD, MHZ(8), MHZ(180), &pl) == CLK_OK);
    CHECK(pl.apb1_div == 4u && pl.pclk1_hz == MHZ(45) && pl.pclk2_hz == MHZ(90) && !pl.pll.usb_exact);
    if (verbose) print_plan("F42x 180", &pl);
    CHECK(clock_plan(&F40X_1V8, MHZ(8), MHZ(168), &pl) == CLK_ERR_FLASH);   /* 1.8 V 에서는 플래시가 못 따라감 */
    CHECK(clock_plan(&F40X, MHZ(8), 100000001u, &pl) == CLK_ERR_NOSOL);

    if (verbose) {
        print_solutions(&F40X, MHZ(8), MHZ(168));
        print_solutions(&F40X_OLD, MHZ(8), MHZ(168));
        print_solutions(&F40X, MHZ(25), MHZ(168));
        print_solutions(&F42X_OD, MHZ(8), MHZ(180));
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
