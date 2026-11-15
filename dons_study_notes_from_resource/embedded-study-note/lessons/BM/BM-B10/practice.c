/* BM-B10 추가 연습 — 한 번에 검증
 *
 * 연습 1 — HSI 16 MHz 를 PLL 소스로: 해 개수·best·PLLCFGR (PLLSRC = 0)
 * 연습 2 — 레지스터 덤프 해석: RCC_PLLCFGR 값 → M/N/P/Q/소스 → VCO·SYSCLK·PLL48 (리셋값, 고전 템플릿, 잘못된 값)
 * 연습 3 — 정확한 해가 없을 때: 목표 이하에서 가장 가까운 SYSCLK 과 오차 ppm (오디오 크리스털 24.576 MHz)
 * 연습 4 — 클럭 계획이 UART 에 주는 영향: APB1(42 MHz) vs APB2(84 MHz) 의 BRR 과 baud 오차 (COMM-B03 연결)
 * 연습 5 — 전압 범위별 최대 HCLK 와 그때의 wait state (RM0090 표의 step 모델)
 *
 * PLL 코어(제약 구조체, pll_q_for, pll_enumerate, pll_pick_best, flash_wait_states, rcc_pllcfgr_word)는
 * solution.c 에서 그대로 옮겨 왔다 (파일 하나로 자립). 제약 값은 RM0090 기준 대표값 — RM 확인 권장.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MHZ(x) ((uint32_t)(x) * 1000000u)

/* ======================= solution.c 코어 (축약 없이 복사) ======================= */
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

/* RM0090 표: 0 < HCLK ≤ step → 0 WS, step < HCLK ≤ 2·step → 1 WS, … ⇒ WS = ceil(HCLK/step) − 1.
 * WS > ws_max 이면 이 전압에서 그 HCLK 은 불가 → -1. HCLK 0 도 -1. */
static int flash_wait_states(const clk_limits_t *L, uint32_t hclk_hz) {
    if (!L || hclk_hz == 0u || L->ws_step == 0u) return -1;
    uint32_t ws = (hclk_hz - 1u) / L->ws_step;
    return ws <= L->ws_max ? (int)ws : -1;
}

/* RCC_PLLCFGR: PLLM[5:0] | PLLN[14:6] | PLLP[17:16] (00=/2 01=/4 10=/6 11=/8) | PLLSRC[22] (1=HSE) | PLLQ[27:24] */
static uint32_t rcc_pllcfgr_word(const pll_cfg_t *c, bool src_hse) {
    return c->m | (c->n << 6) | (((c->p / 2u) - 1u) << 16) | ((src_hse ? 1u : 0u) << 22) | (c->q << 24);
}

/* ======================= 연습 1 — HSI 를 PLL 소스로 ======================= */
/* HSI 는 16 MHz 고정 RC 발진기. 제약 구조체의 HSE 범위를 16 MHz 한 점으로 바꾼 사본을 쓰면 같은 열거기가 그대로 동작한다. */
static clk_limits_t limits_for_hsi(const clk_limits_t *L) {
    clk_limits_t h = *L;
    h.name = "HSI 16 MHz";
    h.hse_min = h.hse_max = MHZ(16);
    return h;
}

/* ======================= 연습 2 — RCC_PLLCFGR 디코더 ======================= */
typedef struct {
    uint32_t m, n, p, q;
    bool     src_hse;
    uint32_t vco_in_hz;   /* 내림 */
    uint32_t vco_hz;      /* 내림 */
    uint32_t pllclk_hz;   /* 내림 */
    uint32_t pll48_hz;    /* 내림 */
} pll_dec_t;
/* 필드를 뽑고 제약을 검사한다. 위반이면 -1 (그래도 필드는 채운다 — 덤프를 읽을 때는 "무엇이 틀렸나" 가 중요) */
static int pllcfgr_decode(uint32_t w, uint32_t hse_hz, uint32_t hsi_hz, const clk_limits_t *L, pll_dec_t *d) {
    d->m = w & 0x3Fu;
    d->n = (w >> 6) & 0x1FFu;
    d->p = 2u * (((w >> 16) & 0x3u) + 1u);
    d->src_hse = ((w >> 22) & 1u) != 0u;
    d->q = (w >> 24) & 0xFu;
    uint32_t in = d->src_hse ? hse_hz : hsi_hz;
    d->vco_in_hz = d->vco_hz = d->pllclk_hz = d->pll48_hz = 0u;
    if (d->m == 0u) return -1;                                   /* 0 으로 나누지 않는다 */
    uint64_t hn = (uint64_t)in * d->n;
    d->vco_in_hz = in / d->m;
    d->vco_hz = (uint32_t)(hn / d->m);
    d->pllclk_hz = (uint32_t)(hn / ((uint64_t)d->m * d->p));
    d->pll48_hz = d->q ? (uint32_t)(hn / ((uint64_t)d->m * d->q)) : 0u;
    if (d->m < L->m_min || d->m > L->m_max || d->n < L->n_min || d->n > L->n_max) return -1;
    if (d->q < L->q_min || d->q > L->q_max) return -1;
    if ((uint64_t)L->vin_min * d->m > in || (uint64_t)L->vin_max * d->m < in) return -1;
    if (hn < (uint64_t)L->vco_min * d->m || hn > (uint64_t)L->vco_max * d->m) return -1;
    if ((uint64_t)d->pllclk_hz > L->sysclk_max || hn > (uint64_t)L->sysclk_max * d->m * d->p) return -1;
    return 0;
}

/* ======================= 연습 3 — 목표 이하에서 가장 가까운 SYSCLK ======================= */
/* 정확한 해가 0 개일 때 쓰는 "차선": hse·N/(M·P) ≤ target 인 것 중 최대. 같은 값이면 M 작은 쪽, 그다음 P 작은 쪽.
 * 분수 비교는 교차 곱 (최대 26e6·432·63·8 ≈ 5.7e12 — 64비트 안). 오차 ppm = (target − 실제)/target·1e6 (올림). */
static int pll_nearest_below(const clk_limits_t *L, uint32_t hse, uint32_t target,
                             pll_cfg_t *best, uint32_t *err_ppm) {
    bool found = false;
    uint64_t bnum = 0, bden = 1;                                 /* 지금까지 최선 = bnum/bden Hz */
    if (target == 0u || target > L->sysclk_max || hse < L->hse_min || hse > L->hse_max) return CLK_ERR_ARG;
    for (uint32_t m = L->m_min; m <= L->m_max; m++) {
        if ((uint64_t)L->vin_min * m > hse || (uint64_t)L->vin_max * m < hse) continue;
        for (uint32_t n = L->n_min; n <= L->n_max; n++) {
            uint64_t hn = (uint64_t)hse * n;
            if (hn < (uint64_t)L->vco_min * m || hn > (uint64_t)L->vco_max * m) continue;
            for (uint32_t p = 2; p <= 8; p += 2) {
                if (!((L->p_mask >> p) & 1u)) continue;
                uint64_t num = hn, den = (uint64_t)m * p;
                if (num > (uint64_t)target * den) continue;      /* 목표 초과 */
                if (!found || num * bden > bnum * den) {          /* 엄격히 클 때만 → 같은 값이면 먼저 찾은 (작은 M, 작은 P) 유지 */
                    found = true; bnum = num; bden = den;
                    best->m = m; best->n = n; best->p = p;
                    best->vco_hz = (uint32_t)(hn / m);
                    best->sysclk_hz = (uint32_t)(num / den);
                    best->q = pll_q_for(L, best->vco_hz, &best->usb_exact);
                    if (hn % m != 0u) best->usb_exact = false;    /* VCO 가 정수 Hz 가 아니면 48 MHz 정확도 불가 */
                    best->pll48_hz = best->q ? best->vco_hz / best->q : 0u;
                }
            }
        }
    }
    if (!found) return CLK_ERR_NOSOL;
    uint64_t tden = (uint64_t)target * bden;                      /* (target − num/den)/target = (tden − num)/tden */
    *err_ppm = (uint32_t)(((tden - bnum) * 1000000u + tden - 1u) / tden);
    return CLK_OK;
}

/* ======================= 연습 4 — UART BRR 은 PCLK 을 따라간다 ======================= */
/* OVER8 = 0 (16x 오버샘플): BRR = 16·USARTDIV = round(pclk / baud)  (가수<<4 | 분수 4비트가 한 정수로 합쳐진다) */
static uint32_t brr_ovs16(uint32_t pclk, uint32_t baud) { return (pclk + baud / 2u) / baud; }
/* 실제 baud = pclk / BRR. 부호 있는 오차 ppm = (실제 − 목표)/목표·1e6 = (pclk − baud·BRR)/(baud·BRR)·1e6 (0 쪽 버림) */
static int32_t baud_err_ppm(uint32_t pclk, uint32_t baud) {
    uint32_t brr = brr_ovs16(pclk, baud);
    int64_t den = (int64_t)baud * brr;
    return (int32_t)(((int64_t)pclk - den) * 1000000 / den);
}

/* ======================= 연습 5 — 전압 범위별 최대 HCLK ======================= */
/* WS 한 칸이 step 이고 LATENCY 최댓값이 ws_max 면 플래시가 버티는 HCLK ≤ step·(ws_max+1). 칩 상한과 작은 쪽. */
static uint32_t hclk_max_for(const clk_limits_t *L) {
    uint64_t by_flash = (uint64_t)L->ws_step * (L->ws_max + 1u);
    return by_flash < L->sysclk_max ? (uint32_t)by_flash : L->sysclk_max;
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    static pll_cfg_t v[PLL_MAX_SOL];
    int n, b;

    /* 연습 1 */
    clk_limits_t hsi = limits_for_hsi(&F40X);
    n = pll_enumerate(&hsi, MHZ(16), MHZ(168), v, PLL_MAX_SOL);
    b = pll_pick_best(v, n);
    CHECK(n == 9);                                                    /* M = 8..16, 전부 P=2 */
    CHECK(b >= 0 && v[b].m == 8 && v[b].n == 168 && v[b].p == 2 && v[b].q == 7);
    CHECK(b >= 0 && rcc_pllcfgr_word(&v[b], false) == 0x07002A08u);    /* PLLSRC = 0 (HSI) */
    CHECK(pll_enumerate(&F40X, MHZ(16), MHZ(168), NULL, 0) == 9);     /* 16 MHz HSE 도 같은 수학 */

    /* 연습 2 */
    pll_dec_t d;
    CHECK(pllcfgr_decode(0x24003010u, MHZ(8), MHZ(16), &F40X, &d) == 0);    /* RCC_PLLCFGR 리셋값 */
    CHECK(d.m == 16 && d.n == 192 && d.p == 2 && d.q == 4 && !d.src_hse);
    CHECK(d.vco_in_hz == MHZ(1) && d.vco_hz == MHZ(192) && d.pllclk_hz == MHZ(96) && d.pll48_hz == MHZ(48));
    CHECK(pllcfgr_decode(0x07405408u, MHZ(8), MHZ(16), &F40X, &d) == 0);    /* 고전 템플릿 */
    CHECK(d.m == 8 && d.n == 336 && d.src_hse && d.pllclk_hz == MHZ(168) && d.pll48_hz == MHZ(48));
    CHECK(pllcfgr_decode(0x07405408u, MHZ(25), MHZ(16), &F40X, &d) == -1);  /* 같은 값을 25 MHz 보드에: VCO_in 3.125 MHz */
    CHECK(d.pllclk_hz == 525000000u);                                       /* 525 MHz 로 폭주할 뻔 */
    CHECK(pllcfgr_decode(0x07405401u, MHZ(8), MHZ(16), &F40X, &d) == -1);   /* M = 1 은 금지 */
    CHECK(pllcfgr_decode(0x07400C48u, MHZ(8), MHZ(16), &F40X, &d) == -1);   /* N = 49 */
    CHECK(pllcfgr_decode(0x07405400u, MHZ(8), MHZ(16), &F40X, &d) == -1);   /* M = 0 — 0 나눗셈 없이 거부 */

    /* 연습 3 — 24.576 MHz (오디오 크리스털) 로 168 MHz 를 */
    pll_cfg_t nb;
    uint32_t ppm = 0;
    CHECK(pll_enumerate(&F40X, 24576000u, MHZ(168), NULL, 0) == 0);        /* 정확한 해 없음 */
    CHECK(pll_nearest_below(&F40X, 24576000u, MHZ(168), &nb, &ppm) == CLK_OK);
    CHECK(nb.sysclk_hz <= MHZ(168) && (uint64_t)24576000u * nb.n <= (uint64_t)MHZ(168) * nb.m * nb.p);
    CHECK(nb.m == 15 && nb.n == 205 && nb.p == 2 && nb.sysclk_hz == 167936000u && ppm == 381);
    CHECK(pll_nearest_below(&F40X, MHZ(8), MHZ(168), &nb, &ppm) == CLK_OK && ppm == 0 && nb.m == 4);  /* 정확하면 0 ppm */
    CHECK(pll_nearest_below(&F40X, MHZ(8), MHZ(12), &nb, &ppm) == CLK_ERR_NOSOL);                    /* PLL 최소 12.5 MHz */

    /* 연습 4 — 168 MHz 계획: PCLK1 42 MHz, PCLK2 84 MHz */
    CHECK(brr_ovs16(MHZ(42), 115200u) == 0x16Du && brr_ovs16(MHZ(84), 115200u) == 0x2D9u);
    CHECK(brr_ovs16(MHZ(42), 921600u) == 0x2Eu && brr_ovs16(MHZ(84), 921600u) == 0x5Bu);
    CHECK(baud_err_ppm(MHZ(42), 115200u) == -1141 && baud_err_ppm(MHZ(84), 115200u) == 228);
    CHECK(baud_err_ppm(MHZ(42), 921600u) == -9284 && baud_err_ppm(MHZ(84), 921600u) == 1602);
    CHECK(baud_err_ppm(MHZ(42), 921600u) < -9000);          /* APB1 의 921600 은 −0.93 % — 고속 UART 는 APB2 (USART1/6) 에 */

    /* 연습 5 */
    clk_limits_t v24 = F40X, v21 = F40X;
    v24.ws_step = MHZ(24); v21.ws_step = MHZ(22);
    CHECK(hclk_max_for(&F40X) == MHZ(168) && flash_wait_states(&F40X, hclk_max_for(&F40X)) == 5);
    CHECK(hclk_max_for(&v24) == MHZ(168) && flash_wait_states(&v24, MHZ(168)) == 6);
    CHECK(hclk_max_for(&v21) == MHZ(168) && flash_wait_states(&v21, MHZ(168)) == 7);
    CHECK(hclk_max_for(&F40X_1V8) == MHZ(160) && flash_wait_states(&F40X_1V8, MHZ(160)) == 7);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
