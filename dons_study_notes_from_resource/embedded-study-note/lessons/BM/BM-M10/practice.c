/* BM-M10 추가 연습 — 한 번에 검증
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *
 * 연습 1  LSI 보정(calibration): 타이머 입력 캡처로 LSI 를 재서 iwdg_pick 의 범위를 좁힌다
 * 연습 2  RM0090 의 WWDG 표 (PCLK1 = 30 MHz) 를 정수 ns 로 재현하고 42 MHz 로 다시 계산
 * 연습 3  리셋 원인 해석: 플래그 128 조합 전수 — 다른 방식으로 짠 구현과 비교
 * 연습 4  EWI 에서 리셋까지 CPU 가 쓸 수 있는 사이클
 * 연습 5  Stop 모드에서 IWDG 를 먹이기 위한 RTC 웨이크업 주기 (RTC 가 LSE 일 때 vs LSI 일 때)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- solution.c 에서 가져온 축약 사본 ---- */
typedef struct { unsigned pr, rlr; uint64_t t_short_us, t_long_us; } iwdg_cfg_t;
static uint64_t iwdg_ticks(unsigned pr, unsigned rlr) { return ((uint64_t)4u << pr) * ((uint64_t)rlr + 1u); }
static int iwdg_pick(uint32_t min_ms, uint32_t max_ms, uint32_t lmin, uint32_t lmax, iwdg_cfg_t *out) {
    if (!out || lmin == 0 || lmin > lmax || min_ms > max_ms) return -2;
    for (unsigned pr = 0; pr <= 6u; pr++) {
        uint64_t step = (uint64_t)4u << pr, need = (uint64_t)min_ms * lmax;
        uint64_t n = (need + step * 1000u - 1u) / (step * 1000u);
        if (n == 0) n = 1;
        if (n > 4096u) continue;
        if (step * n * 1000u > (uint64_t)max_ms * lmin) continue;
        out->pr = pr; out->rlr = (unsigned)(n - 1u);
        out->t_short_us = step * n * 1000000u / lmax;
        out->t_long_us = (step * n * 1000000u + lmin - 1u) / lmin;
        return 0;
    }
    return -1;
}

/* ===== 연습 1: LSI 보정 =====
 * LSI 를 32비트 타이머(F4: TIM5 CH4 내부 연결 — RM 확인 권장)의 입력 캡처에 물리고, 입력 프리스케일러 ic_div
 * (1/2/4/8 에지마다 캡처) 로 두 캡처값 c1, c2 를 얻었다. f_LSI = f_tim · ic_div / (c2 − c1) (반올림).
 * 카운터가 그 사이 한 번 넘쳐도 32비트 부호 없는 뺄셈이 맞는 Δ 를 준다. */
static uint32_t lsi_from_capture(uint32_t f_tim_hz, unsigned ic_div, uint32_t c1, uint32_t c2) {
    uint32_t delta = c2 - c1;
    if (delta == 0) return 0;
    return (uint32_t)(((uint64_t)f_tim_hz * ic_div + delta / 2u) / delta);
}
/* 측정값 ± ppm 여유로 범위를 만든다 (온도·전압 드리프트 — 데이터시트 확인 권장) */
static void lsi_range(uint32_t f_meas, uint32_t margin_ppm, uint32_t *lmin, uint32_t *lmax) {
    *lmin = (uint32_t)((uint64_t)f_meas * (1000000u - margin_ppm) / 1000000u);
    *lmax = (uint32_t)(((uint64_t)f_meas * (1000000u + margin_ppm) + 999999u) / 1000000u);
}

/* ===== 연습 2: WWDG tick / 최대 타임아웃 [ns] ===== */
static uint64_t wwdg_tick_ns(uint32_t pclk1, unsigned tb) { return (uint64_t)(4096u << tb) * 1000000000u / pclk1; }
static uint64_t wwdg_max_ns(uint32_t pclk1, unsigned tb) { return (uint64_t)(4096u << tb) * 64u * 1000000000u / pclk1; }

/* ===== 연습 3: 리셋 원인 — 본문과 같은 규칙, 다른 구현 (비트마스크 + popcount) ===== */
typedef enum { RC_NONE, RC_POR, RC_BOR, RC_PIN, RC_SOFT, RC_IWDG, RC_WWDG, RC_LPWR, RC_MULTIPLE } reset_cause_t;
#define BOR (1u << 25)
#define PIN (1u << 26)
#define POR (1u << 27)
#define SFT (1u << 28)
#define IWD (1u << 29)
#define WWD (1u << 30)
#define LPW (1u << 31)
static reset_cause_t decode_main(uint32_t csr) {          /* 본문 solution.c 와 같은 코드 */
    static const struct { uint32_t bit; reset_cause_t rc; } P[] = {
        { LPW, RC_LPWR }, { WWD, RC_WWDG }, { IWD, RC_IWDG }, { SFT, RC_SOFT }, { POR, RC_POR },
    };
    reset_cause_t rc = RC_NONE;
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++)
        if (csr & P[i].bit) { if (n++ == 0) rc = P[i].rc; }
    if (n > 1) return RC_MULTIPLE;
    if (n == 1) return rc;
    if (csr & BOR) return RC_BOR;
    if (csr & PIN) return RC_PIN;
    return RC_NONE;
}
static reset_cause_t decode_alt(uint32_t csr) {
    uint32_t primary = csr & (LPW | WWD | IWD | SFT | POR);
    if (primary & (primary - 1u)) return RC_MULTIPLE;     /* 비트 2개 이상 */
    switch (primary) {
    case LPW: return RC_LPWR;
    case WWD: return RC_WWDG;
    case IWD: return RC_IWDG;
    case SFT: return RC_SOFT;
    case POR: return RC_POR;
    default: break;
    }
    return (csr & BOR) ? RC_BOR : (csr & PIN) ? RC_PIN : RC_NONE;
}

/* ===== 연습 4: EWI → 리셋 사이의 CPU 사이클 =====
 * 카운터가 0x40 이 되는 순간 EWI, 다음 감소(0x3F)에서 리셋 → 정확히 1 tick = 4096·2^WDGTB PCLK1 사이클.
 * CPU 사이클 = 그 × HCLK/PCLK1, 거기서 예외 진입 지연(Cortex-M3/M4 12 사이클, 0 wait-state 기준)을 뺀다. */
static uint32_t ewi_budget_cycles(uint32_t hclk, uint32_t pclk1, unsigned tb, uint32_t entry_cycles) {
    uint64_t c = (uint64_t)(4096u << tb) * hclk / pclk1;
    return c > entry_cycles ? (uint32_t)(c - entry_cycles) : 0u;
}

/* ===== 연습 5: Stop 중 IWDG 먹이기 =====
 * F4 의 IWDG 는 Stop/Standby 에서도 돈다 (멈추게 하는 옵션 바이트는 L4/F7 등 신형에만 — RM 확인 권장).
 * 그래서 RTC 웨이크업으로 주기적으로 깨어나 kick 해야 한다. 깨어나 kick 하기까지 지연 lat_us.
 * (a) RTC 가 LSE(32.768 kHz 크리스털, 정확) 로 돈다 → 주기 ≤ t_short − lat − margin  [µs]
 * (b) RTC 가 IWDG 와 같은 LSI 로 돈다 → 둘 다 같이 빨라지고 느려진다. 주기를 LSI 사이클 N 으로 잡으면
 *     N + lat·f_LSI ≤ ticks 가 모든 f_LSI 에서 성립해야 → N ≤ ticks − ceil(lat · lsi_max / 1e6). */
static uint64_t wake_period_lse_us(const iwdg_cfg_t *c, uint32_t lat_us, uint32_t margin_us) {
    uint64_t need = (uint64_t)lat_us + margin_us;
    return c->t_short_us > need ? c->t_short_us - need : 0u;
}
static uint64_t wake_period_lsi_cycles(const iwdg_cfg_t *c, uint32_t lat_us, uint32_t lsi_max) {
    uint64_t ticks = iwdg_ticks(c->pr, c->rlr);
    uint64_t lat_cyc = ((uint64_t)lat_us * lsi_max + 999999u) / 1000000u;
    return ticks > lat_cyc ? ticks - lat_cyc : 0u;
}

int main(void) {
    /* 연습 1 */
    CHECK(lsi_from_capture(84000000u, 8, 1000u, 1000u + 21000u) == 32000u);
    CHECK(lsi_from_capture(84000000u, 8, 0xFFFFA000u, 0xFFFFA000u + 21000u) == 32000u);   /* 넘침을 끼고 */
    CHECK(lsi_from_capture(84000000u, 8, 0, 21024u) == 31963u);
    CHECK(lsi_from_capture(84000000u, 8, 5, 5) == 0);
    uint32_t lmin, lmax;
    lsi_range(31963u, 100000u, &lmin, &lmax);                 /* ±10 % */
    CHECK(lmin == 28766u && lmax == 35160u);
    iwdg_cfg_t c;
    CHECK(iwdg_pick(100, 130, 17000, 47000, &c) == -1);       /* 보정 전: 비 1.3 < 2.76 → 불가능 */
    CHECK(iwdg_pick(100, 130, lmin, lmax, &c) == 0 && c.pr == 0 && c.rlr == 878);
    CHECK(c.t_short_us >= 100000u && c.t_long_us <= 130000u);
    printf("ex1: LSI=31963 Hz +-10%% -> [%u, %u] Hz, iwdg_pick(100,130) -> PR=%u RLR=%u, %llu..%llu us\n",
           lmin, lmax, c.pr, c.rlr, (unsigned long long)c.t_short_us, (unsigned long long)c.t_long_us);

    /* 연습 2: RM0090 Table 109 (PCLK1 = 30 MHz) 값 (0.01 µs / 0.01 ms 자리 반올림) 과 비교, 이어서 42 MHz 출력 */
    static const uint32_t tick_10ns[4] = { 13653, 27307, 54613, 109227 };  /* 136.53 / 273.07 / 546.13 / 1092.27 µs */
    static const uint32_t max_10us[4] = { 874, 1748, 3495, 6991 };         /* 8.74 / 17.48 / 34.95 / 69.91 ms */
    for (unsigned tb = 0; tb < 4; tb++) {
        uint64_t t = wwdg_tick_ns(30000000u, tb), m = wwdg_max_ns(30000000u, tb);
        CHECK((t + 5u) / 10u == tick_10ns[tb]);
        CHECK((m + 5000u) / 10000u == max_10us[tb]);
        printf("ex2: WDGTB=%u  30 MHz: tick=%llu ns max=%llu ns | 42 MHz: tick=%llu ns max=%llu ns\n", tb,
               (unsigned long long)t, (unsigned long long)m,
               (unsigned long long)wwdg_tick_ns(42000000u, tb), (unsigned long long)wwdg_max_ns(42000000u, tb));
    }

    /* 연습 3: 128 조합 전수 + 대표값 */
    unsigned same = 0;
    for (uint32_t k = 0; k < 128u; k++) same += decode_main(k << 25) == decode_alt(k << 25);
    CHECK(same == 128u);
    CHECK(decode_main(0x0E000000u) == RC_POR && decode_main(0x24000000u) == RC_IWDG &&
          decode_main(0x44000000u) == RC_WWDG && decode_main(0x14000000u) == RC_SOFT &&
          decode_main(0x84000000u) == RC_LPWR && decode_main(0x06000000u) == RC_BOR &&
          decode_main(0x04000000u) == RC_PIN && decode_main(0) == RC_NONE && decode_main(0x64000000u) == RC_MULTIPLE);
    CHECK(decode_main(0x2E000000u) == RC_MULTIPLE);           /* 전원 투입 후 안 지우고 IWDG → 구분 불가 */

    /* 연습 4 */
    CHECK(ewi_budget_cycles(168000000u, 42000000u, 0, 12) == 16372u);
    CHECK(ewi_budget_cycles(168000000u, 42000000u, 3, 12) == 131060u);
    CHECK(ewi_budget_cycles(16000000u, 16000000u, 0, 12) == 4084u);
    printf("ex4: EWI budget @168/42 MHz: WDGTB0 %u cycles, WDGTB3 %u cycles\n",
           ewi_budget_cycles(168000000u, 42000000u, 0, 12), ewi_budget_cycles(168000000u, 42000000u, 3, 12));

    /* 연습 5 */
    CHECK(iwdg_pick(1000, 4000, 17000, 47000, &c) == 0 && c.pr == 2 && c.rlr == 2937);
    CHECK(wake_period_lse_us(&c, 5000, 50000) == 945170u);
    CHECK(wake_period_lsi_cycles(&c, 5000, 47000) == 46773u);
    printf("ex5: IWDG PR=%u RLR=%u t_short=%llu us -> LSE RTC period <= %llu us, LSI RTC period <= %llu LSI cycles\n",
           c.pr, c.rlr, (unsigned long long)c.t_short_us, (unsigned long long)wake_period_lse_us(&c, 5000, 50000),
           (unsigned long long)wake_period_lsi_cycles(&c, 5000, 47000));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
