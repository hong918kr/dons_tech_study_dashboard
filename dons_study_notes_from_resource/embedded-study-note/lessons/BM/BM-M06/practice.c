/* BM-M06 추가 연습 — 한 번에 검증
 *   연습 1  PWM 입력 모드 (슬레이브 리셋 SMS=100, TS=TI1FP1): CCR1 = 주기, CCR2 = high, ISR 은 주기당 1번
 *   연습 2  IC1F 디지털 필터 (N 연속 샘플): 글리치 제거와 "양쪽 에지가 똑같이 늦어지면 주기는 그대로"
 *   연습 3  PSC 고르기: 최고 주파수에서 1 tick ≤ 허용 오차(ppm) 인 가장 큰 PSC (brute force 와 비교 + 모델 측정)
 *   연습 4  한 채널 양쪽 에지 (CC1P=CC1NP=1) + 핀 레벨로 극성 판정 → 선점 한 번에 망가진다 (두 채널 설계는 멀쩡)
 * solution.c 의 모델·드라이버·하네스를 그대로 쓴다 (main 은 빼고).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   (레슨 폴더에서)
 */
#define BM_M06_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

/* ---------------- 연습 1: PWM 입력 모드 ---------------- */
static struct { uint32_t period, high, n, slow; bool first_done, ovf_seen; } g_pw;
static void pwmin_isr(void) {
    uint32_t sr = REG_READ(TIM_SR), clr = 0u;
    if (sr & SR_UIF) { g_pw.ovf_seen = true; g_pw.slow++; clr |= SR_UIF; }   /* URS=1: 진짜 overflow 만 */
    if (sr & SR_CC1IF) {
        uint32_t per = REG_READ(TIM_CCR1), hi = REG_READ(TIM_CCR2);         /* 리셋 직전 CNT, 하강 때 CNT */
        if (g_pw.first_done && !g_pw.ovf_seen) { g_pw.period = per; g_pw.high = hi; g_pw.n++; }
        g_pw.first_done = true;
        g_pw.ovf_seen = false;
    }
    if (clr) REG_WRITE(TIM_SR, ~clr);
}
static void pwmin_init(uint32_t psc) {
    REG_WRITE(TIM_CR1, CR1_URS);                       /* CEN=0, URS=1: 리셋 트리거가 UIF 를 안 세우게 */
    REG_WRITE(TIM_DIER, 0u);
    REG_WRITE(TIM_CCER, 0u);
    REG_WRITE(TIM_PSC, psc);
    REG_WRITE(TIM_ARR, 0xFFFFu);
    REG_WRITE(TIM_CCMR1, CCMR1_CC1S_TI1 | CCMR1_CC2S_TI1);
    REG_WRITE(TIM_CCER, CCER_CC1E | CCER_CC2E | CCER_CC2P);
    REG_WRITE(TIM_SMCR, SMCR_TS_TI1FP1 | SMCR_SMS_RESET);
    REG_WRITE(TIM_EGR, EGR_UG);
    REG_WRITE(TIM_SR, 0u);
    memset(&g_pw, 0, sizeof g_pw);
    g_irq[TIM3_IRQn].handler = pwmin_isr;
    REG_WRITE(TIM_DIER, DIER_UIE | DIER_CC1IE);         /* CC2IE 는 끈다: CCR2 는 CC1 ISR 에서 같이 읽는다 */
    nvic_enable_irq(TIM3_IRQn);
    REG_WRITE(TIM_CR1, CR1_URS | CR1_CEN);
}

/* ---------------- 연습 2: IC1F 디지털 필터 ---------------- */
/* 새 레벨이 N 샘플 연속이면 그 N 번째 샘플에서 출력이 바뀐다 (지연 N−1 샘플). */
static void icf_filter(const uint8_t *in, uint32_t n, uint32_t N, uint8_t init, uint8_t *out) {
    uint8_t cur = init;
    uint32_t run = 0u;
    for (uint32_t i = 0; i < n; i++) {
        if (in[i] != cur) { if (++run >= N) { cur = in[i]; run = 0u; } }
        else run = 0u;
        out[i] = cur;
    }
}
#define NS (4u * 84000u + 2000u)
static uint8_t g_raw[NS], g_flt[NS];
/* 샘플 배열 (클럭 1개 = 샘플 1개, 인덱스 i = 클럭 t0+i) → 모델 입력 이벤트 */
static void samples_to_events(const uint8_t *s, uint32_t n, uint64_t t0) {
    uint8_t lv = 0u;
    for (uint32_t i = 0; i < n; i++) if (s[i] != lv) { lv = s[i]; in_add(t0 + i, lv); }
}

/* ---------------- 연습 3: PSC 고르기 ---------------- */
/* 1 tick = (psc+1)/f_tim ≤ err·1e-6 · (1/f_max)  ⇔  (psc+1)·f_max_mHz·1e6 ≤ err·f_tim·1000 */
static int ic_pick_psc(uint32_t f_tim, uint64_t f_max_mHz, uint32_t err_ppm, uint32_t *psc) {
    if (!f_tim || !f_max_mHz || !psc) return -1;
    uint64_t q = (uint64_t)err_ppm * f_tim * 1000u / (f_max_mHz * 1000000u);
    if (q == 0u) return -1;                         /* PSC=0 으로도 모자란다 → 평균하거나 더 빠른 타이머 */
    if (q > 65536u) q = 65536u;
    *psc = (uint32_t)(q - 1u);
    return 0;
}
static int ref_pick_psc(uint32_t f_tim, uint64_t f_max_mHz, uint32_t err_ppm, uint32_t *psc) {
    for (int64_t p = 65535; p >= 0; p--)
        if ((uint64_t)(p + 1) * f_max_mHz * 1000000u <= (uint64_t)err_ppm * f_tim * 1000u) { *psc = (uint32_t)p; return 0; }
    return -1;
}
static double g_worst;
static void isr_worst(void) {
    uint32_t before = g_ic.seq;
    TIM3_IRQHandler();
    if (g_ic.seq != before && g_ic.valid && g_ic.rises >= 2u) {
        double clocks = (double)g_ic.period * (g_cur_psc + 1.0), truth = (double)g_cur_F / (double)g_cur_T;
        double ppm = (clocks - truth) / truth * 1e6;
        if (ppm < 0) ppm = -ppm;
        if (ppm > g_worst) g_worst = ppm;
    }
}
static double worst_ppm(uint32_t psc, uint64_t T, uint32_t n) {
    const uint64_t F = 1000ull * F_TIM;
    sim_reset_all();
    ic_init(psc, 0u, false, 0u);
    g_irq[TIM3_IRQn].handler = isr_worst;
    g_cur_F = F; g_cur_T = T; g_cur_psc = psc; g_worst = 0;
    in_add_pwm(1000u, F, T, 12345u % T, 5000u, n);
    sim_run_until(1000u + (uint64_t)n * (F / T) + 10u);
    return g_worst;
}

/* ---------------- 연습 4: 한 채널 양쪽 에지 + 핀 레벨 ---------------- */
#define GPIOA_IDR 0x40020010u                         /* PA6 = TIM3_CH1 (AF2) — F4 대표값, 데이터시트 확인 권장 */
static uint32_t on_r_idr(uint32_t a, uint32_t s, void *c) { (void)a; (void)s; (void)c; return (uint32_t)g_level << 6; }
static struct { uint32_t ovf, last_rise, period, high, n_rise, n_fall, ovc; bool have; } g_b;
static void both_isr(void) {
    uint32_t sr = REG_READ(TIM_SR), clr = 0u;
    if (sr & SR_CC1IF) {
        uint32_t v = REG_READ(TIM_CCR1);
        uint32_t ext = ic_extend(g_b.ovf, v, (sr & SR_UIF) != 0u);
        bool rising = ((REG_READ(GPIOA_IDR) >> 6) & 1u) != 0u;  /* "지금 high 면 방금 에지는 상승" 이라는 가정 */
        if (REG_READ(TIM_SR) & SR_CC1OF) { clr |= SR_CC1OF; g_b.ovc++; g_b.have = false; }
        else if (rising) {
            if (g_b.have) g_b.period = ext - g_b.last_rise;
            g_b.last_rise = ext; g_b.have = true; g_b.n_rise++;
        } else {
            if (g_b.have) g_b.high = ext - g_b.last_rise;
            g_b.n_fall++;
        }
    }
    if (sr & SR_UIF) { g_b.ovf++; clr |= SR_UIF; }
    if (clr) REG_WRITE(TIM_SR, ~clr);
}
static void both_init(void) {
    mock_add_reg(GPIOA_IDR, 0u, on_r_idr, NULL, NULL);
    REG_WRITE(TIM_CR1, 0u);
    REG_WRITE(TIM_CCMR1, CCMR1_CC1S_TI1);
    REG_WRITE(TIM_CCER, CCER_CC1E | CCER_CC1P | CCER_CC1NP);   /* 양쪽 에지 */
    REG_WRITE(TIM_EGR, EGR_UG);
    REG_WRITE(TIM_SR, 0u);
    memset(&g_b, 0, sizeof g_b);
    g_irq[TIM3_IRQn].handler = both_isr;
    REG_WRITE(TIM_DIER, DIER_UIE | DIER_CC1IE);
    nvic_enable_irq(TIM3_IRQn);
    REG_WRITE(TIM_CR1, CR1_CEN);
}
static void hook_preempt_30k(void) { sim_run_nested(30000u); }  /* 더 높은 우선순위 ISR 이 357 us 끼어듦 */

int main(void) {
    const uint64_t F = 1000ull * F_TIM;

    /* ===== 연습 1: PWM 입력 모드 ===== */
    sim_reset_all();
    pwmin_init(1u);                                           /* 42 MHz 카운트 */
    in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 6u);           /* 1 kHz, 25 % — 첫 상승은 기준점이라 측정 5개 */
    sim_run_until(1000u + 6u * 84000u);
    CHECK(g_pw.n == 5u && g_pw.period == 42000u && g_pw.high == 10500u && g_pw.slow == 0u);
    CHECK(g_irq[TIM3_IRQn].calls == 6u);                      /* 주기당 ISR 1번 (CC1 만) */
    uint32_t p1_per = g_pw.period, p1_hi = g_pw.high, p1_calls = g_irq[TIM3_IRQn].calls;
    sim_reset_all();
    pwmin_init(0u);                                           /* 84000 tick > 65535 → 잴 수 없다 */
    in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 6u);
    sim_run_until(1000u + 6u * 84000u);
    CHECK(g_pw.n == 0u && g_pw.slow >= 5u);
    uint32_t p1_slow = g_pw.slow;
    sim_reset_all();
    pwmin_init(0u);
    in_add_pwm(1000u, F, 20000000u, 0u, 3000u, 50u);         /* 20 kHz, 30 % */
    sim_run_until(1000u + 50u * 4200u);
    CHECK(g_pw.n == 49u && g_pw.period == 4200u && g_pw.high == 1260u);
    CHECK(*g_tim.cnt == 4200u && g_tim.uev == 51u);           /* 상승마다 리셋(UEV 50) + UG 1, 마지막 상승 뒤 4200 tick */
    printf("연습 1: 1 kHz@PSC=1 → CCR1=%u CCR2=%u, ISR %u 회 / PSC=0 은 overflow %u 회로 '너무 느림'\n",
           p1_per, p1_hi, p1_calls, p1_slow);

    /* ===== 연습 2: IC1F 디지털 필터 ===== */
    {
        int ok = 1;
        for (uint32_t w = 1u; w <= 12u; w++) {                /* low 신호 속 폭 w 의 high 글리치, N=8 */
            uint8_t in[64] = { 0 }, out[64];
            for (uint32_t i = 20u; i < 20u + w; i++) in[i] = 1u;
            icf_filter(in, 64u, 8u, 0u, out);
            uint32_t hi = 0u, first = 0u;
            for (uint32_t i = 0; i < 64u; i++) if (out[i]) { if (!hi) first = i; hi++; }
            if (w < 8u) ok &= (hi == 0u);
            else ok &= (hi == w && first == 20u + 7u);        /* 폭 그대로, 양쪽 에지 모두 N−1 = 7 샘플 지연 */
        }
        CHECK(ok);
    }
    {
        /* 1 kHz 50 % 에 글리치: low 구간마다 3클럭 high, high 구간마다 2클럭 low (위치 무작위) */
        memset(g_raw, 0, sizeof g_raw);
        for (uint32_t k = 0; k < 4u; k++) {
            uint32_t r = 1000u + k * 84000u;
            for (uint32_t i = r; i < r + 42000u; i++) g_raw[i] = 1u;
            uint32_t gh = r + 1000u + xs32() % 40000u;         /* high 안의 low 글리치 */
            g_raw[gh] = g_raw[gh + 1u] = 0u;
            uint32_t gl = r + 43000u + xs32() % 40000u;        /* low 안의 high 글리치 */
            g_raw[gl] = g_raw[gl + 1u] = g_raw[gl + 2u] = 1u;
        }
        sim_reset_all();
        ic_init(0u, 0u, true, 0u);
        samples_to_events(g_raw, NS, 1000u);
        sim_run_until(1000u + NS + 10u);
        ic_meas_t m;
        ic_get(&m);
        bool raw_bad = !(m.period == 84000u && m.high == 42000u);
        uint32_t raw_rises = m.rises, raw_per = m.period, raw_hi = m.high;
        icf_filter(g_raw, NS, 8u, 0u, g_flt);
        sim_reset_all();
        ic_init(0u, 0u, true, 0u);
        samples_to_events(g_flt, NS, 1000u);
        sim_run_until(1000u + NS + 10u);
        ic_get(&m);
        CHECK(raw_bad && m.valid && m.rises == 4u && m.period == 84000u && m.high_ok && m.high == 42000u);
        printf("연습 2: 글리치 섞인 1 kHz — 필터 없이 rises=%u period=%u high=%u → N=8 필터 후 rises=%u period=%u high=%u\n",
               raw_rises, raw_per, raw_hi, m.rises, m.period, m.high);
    }

    /* ===== 연습 3: PSC 고르기 ===== */
    {
        uint32_t psc = 0, r = 0;
        CHECK(ic_pick_psc(F_TIM, 1000000u, 100u, &psc) == 0 && psc == 7u);       /* 1 kHz, 100 ppm */
        CHECK(ic_pick_psc(F_TIM, 20000000u, 100u, &psc) == -1);                  /* 20 kHz: 1 클럭 = 238 ppm */
        CHECK(ic_pick_psc(F_TIM, 1000u, 1000u, &psc) == 0 && psc == 65535u);     /* 1 Hz, 0.1 %: 상한에 걸림 */
        int bad = 0;
        for (int i = 0; i < 300; i++) {
            uint64_t fm = 1000u + xs32() % 200000000u;
            uint32_t e = 1u + xs32() % 5000u, p1 = 0, p2 = 0;
            int a = ic_pick_psc(F_TIM, fm, e, &p1), b = ref_pick_psc(F_TIM, fm, e, &r);
            p2 = r;
            if (a != b || (a == 0 && p1 != p2)) bad++;
        }
        CHECK(bad == 0);
        /* 정확히 1 kHz 는 84000/8 = 10500 tick 으로 나눠떨어져 오차 0 → 일부러 살짝 어긋난 주파수로 */
        double w1 = worst_ppm(7u, 999999u, 100u), w2 = worst_ppm(7u, 49999u, 20u), w3 = worst_ppm(7u, 2000001u, 100u);
        CHECK(w1 <= 100.0 && w2 <= 100.0 && w3 > 100.0);
        printf("연습 3: PSC=7 (95.2 ns) 최악 한 주기 오차 — 0.999999 kHz %.1f ppm, 49.999 Hz %.1f ppm, 2.000001 kHz %.1f ppm (가정 밖)\n",
               w1, w2, w3);
    }

    /* ===== 연습 4: 한 채널 양쪽 에지 ===== */
    {
        sim_reset_all();
        both_init();
        in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 6u);
        sim_run_until(1000u + 6u * 84000u);
        CHECK(g_b.period == 84000u && g_b.high == 21000u && g_b.n_rise == 6u && g_b.n_fall == 6u);
        sim_reset_all();
        both_init();
        in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 6u);
        sim_run_until(85000u - 1u);
        g_hook_at = g_access_no + 3u;                         /* SR, CCR1 다음 IDR 읽기 직전에 선점 */
        g_hook_fn = hook_preempt_30k;
        sim_run_until(169000u + 1u);                          /* 다음 상승까지 */
        uint32_t bad_per = g_b.period, bad_hi = g_b.high;
        CHECK(bad_per == 168000u && bad_hi == 105000u);       /* 상승을 하강으로 셈 → 2주기, high > 주기 */
        sim_run_until(1000u + 6u * 84000u);
        uint32_t nr = g_b.n_rise;
        CHECK(nr == 5u && g_b.period == 84000u && g_b.high == 21000u);   /* 다음 주기부터는 회복 */
        /* 같은 선점을 두 채널 설계(본문 드라이버)의 SR→CCR1 사이에 넣으면: 영향 없음 */
        sim_reset_all();
        ic_init(0u, 0u, true, 0u);
        in_add_pwm(1000u, F, 1000000u, 0u, 2500u, 6u);
        sim_run_until(85000u - 1u);
        g_hook_at = g_access_no + 2u;
        g_hook_fn = hook_preempt_30k;
        sim_run_until(85000u + 30000u + 1u);
        ic_meas_t m;
        ic_get(&m);
        bool mid_ok = m.valid && m.period == 84000u && m.high == 21000u;
        sim_run_until(1000u + 6u * 84000u);
        ic_get(&m);
        CHECK(mid_ok && m.period == 84000u && m.high == 21000u && m.ovc == 0u && m.rises == 6u);
        printf("연습 4: 한 채널 — 선점 1번에 상승 %u/6 로 셈, 직후 period=%u high=%u (진짜 84000/21000)"
               " / 두 채널 — period=%u high=%u\n", nr, bad_per, bad_hi, m.period, m.high);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
