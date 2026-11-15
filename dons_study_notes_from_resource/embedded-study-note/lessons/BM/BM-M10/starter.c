/* BM-M10 워치독 하드웨어: IWDG/WWDG 타임아웃·윈도우 계산
 *
 * Part A — 공식: IWDG 타임아웃 = 4·2^PR·(RLR+1) / f_LSI, LSI 오차(17–47 kHz)를 견디는 PR/RLR 고르기,
 *          WWDG 카운터 tick = 4096·2^WDGTB / f_PCLK1, "가장 이른 / 가장 늦은 합법 refresh" 창 계산.
 * Part B — mock 워치독: IWDG KR 키 시퀀스(0x5555/0xAAAA/0xCCCC)·PVU/RVU busy·멈출 수 없음,
 *          WWDG 0x40→0x3F 리셋·너무 이른 refresh 리셋·EWI, DBGMCU freeze, RCC_CSR 리셋 원인 + RMVF.
 * Part C — 10 ms 제어 루프: 창 고르기(wwdg_pick), 부팅 때 리셋 원인 보고, 멈춤·폭주·클럭 고장 주입.
 *
 * 주소·비트는 STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st       (TODO 를 채우기 전엔 FAIL, exit 1)
 */
#ifndef TARGET_BUILD
#define UNIT_TEST 1                       /* 호스트 테스트 빌드 */
#endif

#include <stdbool.h>
#include <stdint.h>

/* ======================= 이음매(seam) — BM-B02 mock 축약판 ======================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif
void platform_delay_us(uint32_t us);      /* host: 시뮬레이터 시간 진행 / target: SysTick 지연 (BM-B13) */
void nvic_enable_irq(int irqn);           /* host: NVIC-lite / target: CMSIS NVIC_EnableIRQ (BM-M01) */

/* ======================= 레지스터 (RM0090 기준 대표값 — RM 확인 권장) ======================= */
#define IWDG_BASE        0x40003000u
#define IWDG_KR          (IWDG_BASE + 0x00u)
#define IWDG_PR          (IWDG_BASE + 0x04u)
#define IWDG_RLR         (IWDG_BASE + 0x08u)
#define IWDG_SR          (IWDG_BASE + 0x0Cu)
#define IWDG_KEY_UNLOCK  0x5555u          /* PR/RLR 쓰기 허용 */
#define IWDG_KEY_RELOAD  0xAAAAu          /* 카운터 ← RLR (= kick), 쓰기 다시 잠금 */
#define IWDG_KEY_START   0xCCCCu          /* 시작 (LSI 강제 ON). 리셋 말고는 못 멈춘다 */
#define IWDG_SR_PVU      (1u << 0)
#define IWDG_SR_RVU      (1u << 1)
#define IWDG_PR_MAX      6u               /* /4 ... /256 (PR=7 도 /256) */
#define IWDG_RLR_MAX     0xFFFu           /* 12비트 */
#define IWDG_SR_POLLS    100u             /* PVU/RVU 대기 상한: 100 × 10 µs = 1 ms */
#define IWDG_SR_POLL_US  10u

#define WWDG_BASE        0x40002C00u
#define WWDG_CR          (WWDG_BASE + 0x00u)
#define WWDG_CFR         (WWDG_BASE + 0x04u)
#define WWDG_SR          (WWDG_BASE + 0x08u)
#define WWDG_CR_T_MSK    0x7Fu
#define WWDG_CR_WDGA     (1u << 7)        /* set-only: 리셋으로만 꺼진다 */
#define WWDG_CFR_W_MSK   0x7Fu
#define WWDG_CFR_WDGTB_POS 7u
#define WWDG_CFR_EWI     (1u << 9)        /* set-only */
#define WWDG_SR_EWIF     (1u << 0)        /* rc_w0 */
#define WWDG_T_MIN       0x40u
#define WWDG_T_MAX       0x7Fu
#define WWDG_IRQn        0

#define RCC_CSR          0x40023874u
#define RCC_CSR_LSION    (1u << 0)
#define RCC_CSR_LSIRDY   (1u << 1)
#define RCC_CSR_RMVF     (1u << 24)       /* 1 을 쓰면 리셋 플래그 전부 지움 */
#define RCC_CSR_BORRSTF  (1u << 25)
#define RCC_CSR_PINRSTF  (1u << 26)
#define RCC_CSR_PORRSTF  (1u << 27)
#define RCC_CSR_SFTRSTF  (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)
#define RCC_CSR_WWDGRSTF (1u << 30)
#define RCC_CSR_LPWRRSTF (1u << 31)
#define RCC_CSR_FLAGS    0xFE000000u

#define DBGMCU_APB1_FZ   0xE0042008u
#define DBG_WWDG_STOP    (1u << 11)
#define DBG_IWDG_STOP    (1u << 12)

/* ======================= Part A: 공식과 고르기 ======================= */
typedef struct { unsigned pr, rlr; uint64_t t_short_us, t_long_us; } iwdg_cfg_t;
typedef struct { uint32_t earliest_us, quiet_us, latest_us, reset_us; } wwdg_win_t;
typedef struct { unsigned wdgtb, t, w; wwdg_win_t win; } wwdg_cfg_t;

/* 리셋까지의 LSI 사이클 수 (이 mock 의 정의 = RM 공식) */
static inline uint64_t iwdg_ticks(unsigned pr, unsigned rlr) {
    return ((uint64_t)4u << pr) * ((uint64_t)rlr + 1u);
}
/* WWDG 카운터 1 tick 의 PCLK1 사이클 수 */
static inline uint32_t wwdg_tick_cycles(unsigned wdgtb) { return 4096u << wdgtb; }

/* TODO(A1): LSI = lsi_hz 일 때 IWDG 타임아웃 [µs] (내림). 잘못된 인자 → 0. */
uint64_t iwdg_timeout_us(unsigned pr, unsigned rlr, uint32_t lsi_hz) {
    (void)pr; (void)rlr; (void)lsi_hz;
    return 0;
}

/* TODO(A2): LSI 가 [lsi_min, lsi_max] 어디에 있든
 *   가장 빠른 LSI 에서도 타임아웃 ≥ min_ms  (정상 동작 중엔 절대 안 터진다)
 *   가장 느린 LSI 에서도 타임아웃 ≤ max_ms  (멈추면 늦어도 max_ms 안에 리셋)
 * 을 만족하는 (PR, RLR) 중 PR 가장 작은 것(해상도), 그 안에서 RLR 가장 작은 것(빠른 검출).
 * 0 = OK, -1 = 불가능, -2 = 잘못된 인자. out 에 t_short_us(내림) / t_long_us(올림) 도 채운다. */
int iwdg_pick(uint32_t min_ms, uint32_t max_ms, uint32_t lsi_min_hz, uint32_t lsi_max_hz,
              iwdg_cfg_t *out) {
    (void)min_ms; (void)max_ms; (void)lsi_min_hz; (void)lsi_max_hz; (void)out;
    return -1;
}

/* TODO(A3): refresh(= CR 에 T 쓰기) 직후부터 잰 WWDG 의 "보장" 창 [µs].
 *   earliest_us: 이 시각 이후 refresh 는 프리스케일러 위상과 무관하게 합법 (카운터 ≤ W)   — 올림
 *   quiet_us   : 이 시각까지 refresh 하면 EWI 도 안 울린다 (카운터 ≥ 0x41; T=0x40 이면 0)  — 내림
 *   latest_us  : 이 시각까지 refresh 는 위상과 무관하게 리셋 전 (카운터 ≥ 0x40)           — 내림
 *   reset_us   : 아무것도 안 하면 늦어도 이 시각까지 리셋 (0x40 → 0x3F)                  — 올림
 * W ≥ T 이면 earliest = 0. 잘못된 인자(WDGTB>3, T 또는 W 가 0x40..0x7F 밖) → -1. */
int wwdg_window_us(uint32_t pclk1_hz, unsigned wdgtb, unsigned t, unsigned w, wwdg_win_t *out) {
    (void)pclk1_hz; (void)wdgtb; (void)t; (void)w;
    if (out) *out = (wwdg_win_t){ 0, 0, 0, 0 };
    return -1;
}

/* ======================= Part B: 드라이버 ======================= */
/* TODO(B1): IWDG 를 PR/RLR 로 시작한다. 순서가 핵심:
 *   ① 0xCCCC 시작 (LSI 강제 ON — PVU/RVU 는 LSI 도메인에서 끝나므로 LSI 가 돌아야 한다)
 *   ② 0x5555 잠금 해제 → PR, RLR 쓰기
 *   ③ SR(PVU|RVU) == 0 까지 유한 대기 (IWDG_SR_POLLS × platform_delay_us(IWDG_SR_POLL_US)), 넘으면 -1
 *   ④ 0xAAAA — 새 RLR 로 카운터 적재 (+ 다시 잠금)
 * 0 = OK, -1 = PVU/RVU 타임아웃, -2 = 잘못된 인자 */
int iwdg_init(unsigned pr, unsigned rlr) {
    (void)pr; (void)rlr;
    return -1;
}

void iwdg_kick(void) { REG_WRITE(IWDG_KR, IWDG_KEY_RELOAD); }

/* WWDG 시작. T 와 WDGA 를 "한 번의 쓰기" 로 — 7.3 실수 참고. */
int wwdg_init(unsigned wdgtb, unsigned t, unsigned w, bool ewi) {
    if (wdgtb > 3u || t < WWDG_T_MIN || t > WWDG_T_MAX || w < WWDG_T_MIN || w > WWDG_T_MAX) return -2;
    REG_WRITE(WWDG_CFR, (w & WWDG_CFR_W_MSK) | (wdgtb << WWDG_CFR_WDGTB_POS) | (ewi ? WWDG_CFR_EWI : 0u));
    REG_WRITE(WWDG_SR, 0u);                           /* free-running 중 생긴 EWIF 찌꺼기 (rc_w0) */
    REG_WRITE(WWDG_CR, WWDG_CR_WDGA | t);
    return 0;
}

void wwdg_kick(unsigned t) { REG_WRITE(WWDG_CR, WWDG_CR_WDGA | (t & WWDG_CR_T_MSK)); }

typedef enum { RC_NONE, RC_POR, RC_BOR, RC_PIN, RC_SOFT, RC_IWDG, RC_WWDG, RC_LPWR, RC_MULTIPLE } reset_cause_t;

/* TODO(B2): RCC_CSR 값 → 리셋 원인.
 *   "주" 플래그 = LPWR, WWDG, IWDG, SFT, POR. 둘 이상 서 있으면 RC_MULTIPLE (지난 부팅에서 안 지웠다).
 *   하나면 그것. 없으면 BOR → RC_BOR, PIN → RC_PIN, 아무것도 없으면 RC_NONE.
 *   (모델: 내부 리셋은 NRST 를 끌어내리므로 PINRSTF 도 함께 선다. POR 은 BORRSTF 도 세운다 — RM 확인 권장) */
reset_cause_t reset_cause_decode(uint32_t csr) {
    (void)csr;
    return RC_NONE;
}

/* TODO(B2): 부팅 때 한 번. CSR 을 읽어 *raw 에 남기고, RMVF 로 플래그를 지운 뒤 원인을 돌려준다.
 *   CSR 의 다른 비트(LSION)는 지키도록 RMW 로 쓴다. */
reset_cause_t reset_cause_read_and_clear(uint32_t *raw) {
    if (raw) *raw = 0;
    return RC_NONE;
}

/* ======================= Part C: 10 ms 제어 루프 ======================= */
/* TODO(C1): 정상 동작의 kick 간격이 [min_us, max_us] 안이라면 절대 리셋되지 않는 WWDG 설정.
 *   ewi=true 면 정상 동작 중 EWI 도 울리면 안 된다 → latest 대신 quiet 로 판단.
 *   보장 창이 earliest ≤ min_us, (ewi ? quiet : latest) ≥ max_us 를 만족하는 것 중
 *   WDGTB 가장 작은 것 → 그 안에서 T 가장 작은 것(빠른 멈춤 검출) → W 가장 작은 것(빡빡한 "너무 이른" 검출).
 *   0 = OK, -1 = 불가능, -2 = 잘못된 인자. out->win 도 채운다. 닫힌 식으로 풀어 보라 (테스트가 전수 탐색과 비교). */
int wwdg_pick(uint32_t pclk1_hz, uint32_t min_us, uint32_t max_us, bool ewi, wwdg_cfg_t *out) {
    (void)pclk1_hz; (void)min_us; (void)max_us; (void)ewi; (void)out;
    return -1;
}

/* .noinit 흉내: 리셋을 넘어 살아남는 RAM (BM-M13 / BM-A09 의 진짜 .noinit 섹션) */
#define NOINIT_MAGIC 0xB00710ADu
typedef struct {
    uint32_t magic, boots, ewi_count, ewi_counter, ewi_stage;
    uint32_t last_raw;
    reset_cause_t last_cause;
} noinit_t;
static noinit_t g_noinit;
static volatile uint32_t g_stage;          /* 제어 루프가 지금 무엇을 하는 중인가 (1 센서, 2 제어, 3 출력) */
static volatile uint32_t g_motor_enable;   /* "출력" — 안전 상태 = 0 */

/* TODO(C2): WWDG EWI ISR — 카운터가 0x40 이 되면 들어온다 (리셋까지 남은 시간 = 1 tick).
 *   EWIF 지우기(rc_w0: 0 쓰기), 유언(last words) 기록: ewi_count++, 그때의 카운터(CR & 0x7F), g_stage,
 *   출력을 안전 상태로 (g_motor_enable = 0). 여기서 kick 하지 않는다. */
void WWDG_IRQHandler(void) {
}

#ifdef UNIT_TEST
#include <stdio.h>
#include <string.h>

static const char *const RC_NAME[] = { "NONE", "POR", "BOR", "PIN", "SOFT", "IWDG", "WWDG", "LPWR", "MULTIPLE" };

/* ======================= mock: 워치독·RCC·DBGMCU 모델 + NVIC-lite (BM-B02 mock 축약판) ======================= */
#define LSI_BUSY_CYCLES 5u                 /* PVU/RVU 가 서 있는 LSI 사이클 수 (모델 값, RM: "최대 5 RC 사이클") */
typedef struct { char op; uint32_t addr, val; } acc_t;
static struct {
    uint64_t now_us;                       /* sim_step 1회 = 1 µs */
    uint32_t lsi_hz, pclk1_hz;
    bool     halted;                       /* 디버거가 코어를 세웠다 */
    uint64_t lsi_acc, ww_acc;              /* 분수 클럭 누산기 (Hz·µs 단위) */
    bool     iw_started, iw_unlocked;
    uint32_t iw_pr, iw_rlr, iw_pr_eff, iw_rlr_eff, iw_cnt, iw_presc;
    unsigned iw_pvu, iw_rvu;
    uint32_t ww_cnt, ww_cfr, ww_sr;
    bool     ww_on;
    uint32_t rcc_csr, dbg_fz;
    bool     irq_en;                       /* NVIC-lite: WWDG_IRQn 하나 */
    void   (*isr)(void);
    unsigned isr_calls;
    unsigned resets;
    uint32_t reset_src;
    uint64_t reset_us;
    bool     reset_req;
    unsigned bad, ignored;
    acc_t    log[64];
    unsigned nlog;
} S;

static void iwdg_regs_reset(void) {
    S.iw_started = S.iw_unlocked = false;
    S.iw_pr = S.iw_pr_eff = 0; S.iw_rlr = S.iw_rlr_eff = IWDG_RLR_MAX;
    S.iw_cnt = IWDG_RLR_MAX; S.iw_presc = 0; S.iw_pvu = S.iw_rvu = 0;
}
static void wwdg_regs_reset(void) { S.ww_cnt = 0x7Fu; S.ww_cfr = 0x7Fu; S.ww_sr = 0; S.ww_on = false; }

static void sim_system_reset(uint32_t src) {          /* 시스템 리셋: 리셋 플래그는 남고(전원 리셋만 지움) 나머지는 초기값 */
    S.rcc_csr = (S.rcc_csr | src | RCC_CSR_PINRSTF) & ~RCC_CSR_LSION;
    iwdg_regs_reset();
    wwdg_regs_reset();
    S.irq_en = false;
    S.halted = false;
    S.resets++; S.reset_src = src; S.reset_us = S.now_us; S.reset_req = true;
}

static void sim_power_on(uint32_t lsi_hz, uint32_t pclk1_hz) {
    memset(&S, 0, sizeof S);
    S.lsi_hz = lsi_hz; S.pclk1_hz = pclk1_hz;
    iwdg_regs_reset(); wwdg_regs_reset();
    S.rcc_csr = RCC_CSR_PORRSTF | RCC_CSR_BORRSTF | RCC_CSR_PINRSTF;   /* 리셋값 0x0E000000 */
    S.isr = WWDG_IRQHandler;
}

static void iwdg_lsi_cycle(void) {
    if (S.iw_pvu && --S.iw_pvu == 0) S.iw_pr_eff = S.iw_pr;
    if (S.iw_rvu && --S.iw_rvu == 0) S.iw_rlr_eff = S.iw_rlr;
    if (!S.iw_started) return;
    unsigned pr = S.iw_pr_eff > 6u ? 6u : S.iw_pr_eff;
    if (++S.iw_presc < (4u << pr)) return;
    S.iw_presc = 0;
    if (S.iw_cnt == 0) sim_system_reset(RCC_CSR_IWDGRSTF);
    else S.iw_cnt--;
}

static void wwdg_tick(void) {
    S.ww_cnt = (S.ww_cnt - 1u) & 0x7Fu;               /* 꺼져 있어도 free-running */
    if (S.ww_cnt == 0x40u) S.ww_sr |= WWDG_SR_EWIF;   /* EWI 가 꺼져 있어도 플래그는 선다 */
    if (S.ww_on && S.ww_cnt == 0x3Fu) sim_system_reset(RCC_CSR_WWDGRSTF);
}

static void sim_step(void) {
    S.now_us++;
    bool lsi_on = S.iw_started || (S.rcc_csr & RCC_CSR_LSION);
    if (lsi_on && S.lsi_hz) {
        S.lsi_acc += S.lsi_hz;
        while (S.lsi_acc >= 1000000u) {
            S.lsi_acc -= 1000000u;
            if (!(S.halted && (S.dbg_fz & DBG_IWDG_STOP))) iwdg_lsi_cycle();
            if (S.reset_req) return;
        }
    }
    if (S.pclk1_hz && !(S.halted && (S.dbg_fz & DBG_WWDG_STOP))) {
        uint64_t per = (uint64_t)wwdg_tick_cycles((S.ww_cfr >> WWDG_CFR_WDGTB_POS) & 3u) * 1000000u;
        S.ww_acc += S.pclk1_hz;
        while (S.ww_acc >= per) {
            S.ww_acc -= per;
            wwdg_tick();
            if (S.reset_req) return;
        }
    }
    bool line = (S.ww_sr & WWDG_SR_EWIF) && (S.ww_cfr & WWDG_CFR_EWI);   /* irq_line_set(WWDG_IRQn, line) */
    if (line && S.irq_en && S.isr && !S.halted) { S.isr_calls++; S.isr(); }
}

/* n µs 진행. 리셋이 나면 그 자리에서 멈추고 진행한 µs 를 돌려준다. */
static uint64_t sim_run_us(uint64_t n) {
    uint64_t i = 0;
    while (i < n && !S.reset_req) { sim_step(); i++; }
    return i;
}

void platform_delay_us(uint32_t us) { sim_run_us(us); }
void nvic_enable_irq(int irqn) { if (irqn == WWDG_IRQn) S.irq_en = true; }

static void log_acc(char op, uint32_t addr, uint32_t val) {
    if (S.nlog < sizeof S.log / sizeof S.log[0]) S.log[S.nlog++] = (acc_t){ op, addr, val };
}

uint32_t mock_read32(uint32_t addr) {
    uint32_t v = 0;
    switch (addr) {
    case IWDG_KR:  v = 0; break;                               /* write-only */
    case IWDG_PR:  v = S.iw_pr; break;
    case IWDG_RLR: v = S.iw_rlr; break;
    case IWDG_SR:  v = (S.iw_pvu ? IWDG_SR_PVU : 0u) | (S.iw_rvu ? IWDG_SR_RVU : 0u); break;
    case WWDG_CR:  v = (S.ww_on ? WWDG_CR_WDGA : 0u) | S.ww_cnt; break;
    case WWDG_CFR: v = S.ww_cfr; break;
    case WWDG_SR:  v = S.ww_sr; break;
    case RCC_CSR:  v = S.rcc_csr | ((S.iw_started || (S.rcc_csr & RCC_CSR_LSION)) ? RCC_CSR_LSIRDY : 0u); break;
    case DBGMCU_APB1_FZ: v = S.dbg_fz; break;
    default: S.bad++; break;
    }
    log_acc('R', addr, v);
    return v;
}

void mock_write32(uint32_t addr, uint32_t val) {
    log_acc('W', addr, val);
    switch (addr) {
    case IWDG_KR:
        switch (val & 0xFFFFu) {
        case IWDG_KEY_UNLOCK: S.iw_unlocked = true; break;
        case IWDG_KEY_RELOAD: S.iw_unlocked = false; S.iw_cnt = S.iw_rlr_eff; S.iw_presc = 0; break;
        case IWDG_KEY_START:
            S.iw_unlocked = false;
            if (!S.iw_started) { S.iw_started = true; S.iw_cnt = S.iw_rlr_eff; S.iw_presc = 0; }
            break;
        default: S.iw_unlocked = false; break;                 /* 다른 값 = 다시 잠금. 멈추는 키는 없다 */
        }
        break;
    case IWDG_PR:
        if (!S.iw_unlocked || S.iw_pvu) { S.ignored++; break; }
        S.iw_pr = val & 7u; S.iw_pvu = LSI_BUSY_CYCLES; break;
    case IWDG_RLR:
        if (!S.iw_unlocked || S.iw_rvu) { S.ignored++; break; }
        S.iw_rlr = val & IWDG_RLR_MAX; S.iw_rvu = LSI_BUSY_CYCLES; break;
    case IWDG_SR: S.ignored++; break;
    case WWDG_CR:
        if (S.ww_on && S.ww_cnt > (S.ww_cfr & WWDG_CFR_W_MSK)) { sim_system_reset(RCC_CSR_WWDGRSTF); break; }
        S.ww_cnt = val & WWDG_CR_T_MSK;
        if (val & WWDG_CR_WDGA) S.ww_on = true;
        if (S.ww_on && !(S.ww_cnt & 0x40u)) sim_system_reset(RCC_CSR_WWDGRSTF);   /* T6 = 0 → 즉시 리셋 */
        break;
    case WWDG_CFR: S.ww_cfr = (val & 0x1FFu) | ((S.ww_cfr | val) & WWDG_CFR_EWI); break;
    case WWDG_SR:  S.ww_sr &= (val & WWDG_SR_EWIF); break;    /* rc_w0 */
    case RCC_CSR:
        if (val & RCC_CSR_RMVF) S.rcc_csr &= ~RCC_CSR_FLAGS;
        S.rcc_csr = (S.rcc_csr & ~RCC_CSR_LSION) | (val & RCC_CSR_LSION);
        break;
    case DBGMCU_APB1_FZ: S.dbg_fz = val; break;
    default: S.bad++; break;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void sim_ack_reset(void) { S.reset_req = false; }

/* 리셋이 날 때까지 최대 max_us 진행. 리셋까지 걸린 µs, 안 나면 UINT64_MAX */
static uint64_t until_reset(uint64_t max_us) {
    uint64_t r = sim_run_us(max_us);
    return S.reset_req ? r : UINT64_MAX;
}

static int iwdg_pick_brute(uint32_t min_ms, uint32_t max_ms, uint32_t lmin, uint32_t lmax, iwdg_cfg_t *o) {
    for (unsigned pr = 0; pr <= IWDG_PR_MAX; pr++)
        for (unsigned rlr = 0; rlr <= IWDG_RLR_MAX; rlr++) {
            uint64_t t = iwdg_ticks(pr, rlr) * 1000u;
            if (t >= (uint64_t)min_ms * lmax && t <= (uint64_t)max_ms * lmin) { o->pr = pr; o->rlr = rlr; return 0; }
        }
    return -1;
}

static int wwdg_pick_brute(uint32_t pclk, uint32_t min_us, uint32_t max_us, bool ewi, wwdg_cfg_t *o) {
    for (unsigned tb = 0; tb <= 3u; tb++)
        for (unsigned t = WWDG_T_MIN; t <= WWDG_T_MAX; t++)
            for (unsigned w = WWDG_T_MIN; w <= WWDG_T_MAX; w++) {
                wwdg_win_t win;
                wwdg_window_us(pclk, tb, t, w, &win);
                if (win.earliest_us <= min_us && (ewi ? win.quiet_us : win.latest_us) >= max_us) {
                    o->wdgtb = tb; o->t = t; o->w = w; o->win = win; return 0;
                }
            }
    return -1;
}

static void test_part_a(void) {
    /* A1: RM0090 표 (LSI 32 kHz): RLR=0 → 0.125 ms·2^PR, RLR=0xFFF → 512 ms·2^PR */
    int ok = 1;
    for (unsigned pr = 0; pr <= IWDG_PR_MAX; pr++)
        ok &= iwdg_timeout_us(pr, 0, 32000) == (125u << pr) && iwdg_timeout_us(pr, 0xFFF, 32000) == (512000u << pr);
    CHECK(ok);
    CHECK(iwdg_timeout_us(6, 0xFFF, 17000) == 61680941u);   /* 느린 LSI: 32.8 s → 61.7 s */
    CHECK(iwdg_timeout_us(6, 0xFFF, 47000) == 22310127u);   /* 빠른 LSI: 22.3 s */
    CHECK(iwdg_timeout_us(7, 0, 32000) == 0 && iwdg_timeout_us(0, 0x1000, 32000) == 0 && iwdg_timeout_us(0, 0, 0) == 0);

    /* A2: 고정 벡터 */
    iwdg_cfg_t c = { 0, 0, 0, 0 };
    CHECK(iwdg_pick(100, 2000, 17000, 47000, &c) == 0 && c.pr == 0 && c.rlr == 1174);
    CHECK(c.t_short_us == 100000u && c.t_long_us == 276471u);
    CHECK(iwdg_pick(100, 250, 17000, 47000, &c) == -1);              /* 비 2.5 < 47/17 = 2.76 */
    CHECK(iwdg_pick(100, 277, 17000, 47000, &c) == 0 && c.rlr == 1174);
    CHECK(iwdg_pick(10000, 90000, 17000, 47000, &c) == 0 && c.pr == 5 && c.rlr == 3671);
    CHECK(iwdg_pick(20000, 50000, 17000, 47000, &c) == -1);          /* 비 2.5 */
    CHECK(iwdg_pick(30000, 200000, 17000, 47000, &c) == -1);         /* 47 kHz 에서 최대 22.3 s 밖에 안 된다 */
    CHECK(iwdg_pick(0, 1, 32000, 32000, &c) == 0 && c.pr == 0 && c.rlr == 0);
    CHECK(iwdg_pick(10, 5, 17000, 47000, &c) == -2 && iwdg_pick(10, 50, 0, 47000, &c) == -2 &&
          iwdg_pick(10, 50, 47000, 17000, &c) == -2);

    /* A2: 전수 탐색과 비교 (고정 시드 3000 케이스) + 결과가 정말 조건을 만족하는지 */
    unsigned same = 0, feas = 0, sound = 0, n = 3000;
    for (unsigned i = 0; i < n; i++) {
        uint32_t lmin = 15000u + xs32() % 25000u, lmax = lmin + xs32() % 30000u;
        uint32_t mn = (i & 1) ? xs32() % 3000u : xs32() % 60000u;
        uint32_t mx = mn + xs32() % (3u * mn + 10u);
        iwdg_cfg_t a = { 0, 0, 0, 0 }, b = { 0, 0, 0, 0 };
        int ra = iwdg_pick(mn, mx, lmin, lmax, &a), rb = iwdg_pick_brute(mn, mx, lmin, lmax, &b);
        if (ra == rb && (ra != 0 || (a.pr == b.pr && a.rlr == b.rlr))) same++;
        if (ra == 0) {
            feas++;
            if (a.t_short_us >= (uint64_t)mn * 1000u && a.t_long_us <= (uint64_t)mx * 1000u + 1u) sound++;
        }
    }
    CHECK(same == n);
    CHECK(feas > 300 && feas < n - 300);   /* 가능/불가능 케이스가 둘 다 충분히 섞였나 */
    CHECK(sound == feas);

    /* A3: WWDG 창 — PCLK1 42 MHz 계산값 (RM0090 Table 109 는 30 MHz 기준: 8.74 / 69.91 ms — 연습 2) */
    wwdg_win_t w = { 0, 0, 0, 0 };
    CHECK(wwdg_window_us(42000000u, 0, 0x7F, 0x7F, &w) == 0 && w.earliest_us == 0 && w.latest_us == 6144 && w.reset_us == 6242);
    CHECK(wwdg_window_us(42000000u, 3, 0x7F, 0x7F, &w) == 0 && w.latest_us == 49152 && w.reset_us == 49933);
    CHECK(wwdg_window_us(42000000u, 0, 0x7F, 0x5F, &w) == 0 && w.earliest_us == 3121 && w.latest_us == 6144);
    CHECK(wwdg_window_us(42000000u, 1, 0x40, 0x40, &w) == 0 && w.earliest_us == 0 && w.latest_us == 0 && w.reset_us == 196);
    CHECK(wwdg_window_us(42000000u, 0, 0x7F, 0x3F, &w) == -1);       /* W < 0x40: 합법 refresh 가 없다 */
    CHECK(wwdg_window_us(42000000u, 0, 0x3F, 0x50, &w) == -1 && wwdg_window_us(42000000u, 4, 0x7F, 0x50, &w) == -1);
}

/* IWDG 를 raw 레지스터로 다루는 "틀린 순서" 초기화: 시작 전에 PR/RLR 을 쓰고 기다린다 (LSI 꺼져 있음) */
static int iwdg_init_config_first(unsigned pr, unsigned rlr) {
    REG_WRITE(IWDG_KR, IWDG_KEY_UNLOCK);
    REG_WRITE(IWDG_PR, pr);
    REG_WRITE(IWDG_RLR, rlr);
    for (unsigned n = 0;; n++) {
        if ((REG_READ(IWDG_SR) & (IWDG_SR_PVU | IWDG_SR_RVU)) == 0) break;
        if (n >= IWDG_SR_POLLS) return -1;
        platform_delay_us(IWDG_SR_POLL_US);
    }
    REG_WRITE(IWDG_KR, IWDG_KEY_START);
    return 0;
}

static void test_part_b_iwdg(void) {
    /* B1: 굶기기(starvation) — PR=0, RLR=99, LSI 32 kHz → 100·4/32000 = 12.5 ms */
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init(0, 99) == 0);
    CHECK(S.nlog >= 6 && S.log[0].op == 'W' && S.log[0].addr == IWDG_KR && S.log[0].val == IWDG_KEY_START &&
          S.log[1].val == IWDG_KEY_UNLOCK && S.log[2].addr == IWDG_PR && S.log[3].addr == IWDG_RLR &&
          S.log[S.nlog - 1].addr == IWDG_KR && S.log[S.nlog - 1].val == IWDG_KEY_RELOAD);
    CHECK(REG_READ(IWDG_SR) == 0 && REG_READ(IWDG_PR) == 0 && REG_READ(IWDG_RLR) == 99);
    uint64_t r = until_reset(100000);
    CHECK(r != UINT64_MAX && r + 32 >= 12500 && r <= 12500 + 32);    /* ± LSI 1 주기 위상 */
    CHECK(S.reset_src == RCC_CSR_IWDGRSTF && (S.rcc_csr & (RCC_CSR_IWDGRSTF | RCC_CSR_PINRSTF)) == (RCC_CSR_IWDGRSTF | RCC_CSR_PINRSTF));

    /* B2: 10 ms 마다 kick → 1 s 동안 리셋 없음 */
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init(0, 99) == 0);
    bool alive = true;
    for (int i = 0; i < 100 && alive; i++) { sim_run_us(10000); alive = !S.reset_req; iwdg_kick(); }
    CHECK(alive && S.resets == 0);

    /* B3: 0x5555 없이 PR/RLR 쓰기 → 무시. 기본값 PR=0, RLR=0xFFF → 512 ms 에 리셋 */
    sim_power_on(32000, 42000000u);
    REG_WRITE(IWDG_KR, IWDG_KEY_START);
    REG_WRITE(IWDG_PR, 3); REG_WRITE(IWDG_RLR, 10);
    CHECK(S.ignored == 2 && REG_READ(IWDG_PR) == 0 && REG_READ(IWDG_RLR) == 0xFFF);
    r = until_reset(600000);
    CHECK(r != UINT64_MAX && r + 32 >= 512000 && r <= 512000 + 32);

    /* B4: PVU busy 중 PR 재기록은 무시 / RVU busy 중 kick 은 "옛" RLR 로 적재 */
    sim_power_on(32000, 42000000u);
    REG_WRITE(IWDG_KR, IWDG_KEY_START);
    REG_WRITE(IWDG_KR, IWDG_KEY_UNLOCK);
    REG_WRITE(IWDG_PR, 2); REG_WRITE(IWDG_PR, 4);
    REG_WRITE(IWDG_RLR, 99);
    CHECK(S.ignored == 1 && REG_READ(IWDG_SR) == (IWDG_SR_PVU | IWDG_SR_RVU));
    iwdg_kick();
    CHECK(S.iw_cnt == 0xFFF);                                        /* 아직 옛 RLR */
    sim_run_us(1000);
    CHECK(REG_READ(IWDG_SR) == 0 && S.iw_pr_eff == 2);
    iwdg_kick();
    CHECK(S.iw_cnt == 99);

    /* B5: LSI 가 꺼진 채 "설정 먼저, 시작 나중" → PVU/RVU 가 영원히 1 → 유한 대기 실패 */
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init_config_first(0, 99) == -1 && REG_READ(IWDG_SR) == (IWDG_SR_PVU | IWDG_SR_RVU));
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init(0, 99) == 0);                                    /* 시작 먼저 = LSI 강제 ON */

    /* B6: 한 번 시작하면 못 멈춘다 — 다른 키, LSION=0 써도 리셋 */
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init(0, 99) == 0);
    REG_WRITE(IWDG_KR, 0x0000u); REG_WRITE(IWDG_KR, 0x5555u); REG_WRITE(IWDG_KR, 0x1234u);
    REG_WRITE(RCC_CSR, REG_READ(RCC_CSR) & ~RCC_CSR_LSION);
    CHECK((REG_READ(RCC_CSR) & RCC_CSR_LSIRDY) != 0);
    CHECK(until_reset(100000) != UINT64_MAX && S.reset_src == RCC_CSR_IWDGRSTF);

    /* B7: iwdg_pick 결과가 모델에서도 경계 안인가: LSI 17 kHz / 47 kHz 에서 실제 리셋 시각 */
    iwdg_cfg_t c = { 0, 0, 0, 0 };
    CHECK(iwdg_pick(100, 300, 17000, 47000, &c) == 0);
    sim_power_on(47000, 42000000u);
    CHECK(iwdg_init(c.pr, c.rlr) == 0);
    uint64_t fast = until_reset(1000000);
    sim_power_on(17000, 42000000u);
    CHECK(iwdg_init(c.pr, c.rlr) == 0);
    uint64_t slow = until_reset(1000000);
    CHECK(fast != UINT64_MAX && fast + 22 >= 100000);               /* LSI 1 주기(21.3 µs) 위상 */
    CHECK(slow != UINT64_MAX && slow <= c.t_long_us && slow <= 300000);
    if (g_verbose) printf("  iwdg_pick(100,300) PR=%u RLR=%u  model reset: %llu us @47k, %llu us @17k\n",
                          c.pr, c.rlr, (unsigned long long)fast, (unsigned long long)slow);
}

static void test_part_b_wwdg(void) {
    const uint32_t P = 42000000u;
    wwdg_win_t w = { 0, 0, 0, 0 };
    /* B8: 너무 이른 refresh → 리셋 */
    sim_power_on(32000, P);
    CHECK(wwdg_init(0, 0x7F, 0x5F, false) == 0);
    sim_run_us(1000);
    wwdg_kick(0x7F);                                                 /* 1 ms: 카운터 ≈ 0x7F-10 > W */
    CHECK(S.reset_req && S.reset_src == RCC_CSR_WWDGRSTF);

    /* B9: 창 안 refresh (earliest 와 latest 의 중간) → 200 ms 동안 리셋 없음 */
    sim_power_on(32000, P);
    wwdg_window_us(P, 0, 0x7F, 0x5F, &w);
    CHECK(wwdg_init(0, 0x7F, 0x5F, false) == 0);
    bool alive = true;
    for (int i = 0; i < 43 && alive; i++) {
        sim_run_us((w.earliest_us + w.latest_us) / 2u);
        alive = !S.reset_req;
        if (alive) wwdg_kick(0x7F);
        alive = alive && !S.reset_req;
    }
    CHECK(alive && S.resets == 0);

    /* B10: 굶기기 → 0x40 에서 EWI (ISR 1회), 0x3F 에서 리셋. 시각은 (latest, reset_us] */
    sim_power_on(32000, P);
    memset(&g_noinit, 0, sizeof g_noinit);
    g_stage = 7; g_motor_enable = 1;
    CHECK(wwdg_init(0, 0x7F, 0x5F, true) == 0);
    nvic_enable_irq(WWDG_IRQn);
    uint64_t r = until_reset(100000);
    CHECK(r != UINT64_MAX && r > w.latest_us && r <= w.reset_us && S.reset_src == RCC_CSR_WWDGRSTF);
    CHECK(S.isr_calls == 1 && g_noinit.ewi_count == 1 && g_noinit.ewi_counter == 0x40 &&
          g_noinit.ewi_stage == 7 && g_motor_enable == 0);

    /* B11: 위상 전수에 가까운 성질 테스트 — 임의 WDGTB/T/W, 임의 시작 위상 */
    unsigned ok_e = 0, ok_l = 0, early = 0, late = 0, n = 256, n_early = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned tb = xs32() % 4u, t = 0x41u + xs32() % 63u, wv = 0x40u + xs32() % (t - 0x40u + 1u);
        uint32_t warm = xs32() % 800u;
        wwdg_window_us(P, tb, t, wv, &w);
        uint32_t tick_us = (wwdg_tick_cycles(tb) * 1000u + P / 1000u - 1u) / (P / 1000u);
        /* (1) earliest 에 refresh → OK */
        sim_power_on(32000, P); sim_run_us(warm); wwdg_init(tb, t, wv, false);
        sim_run_us(w.earliest_us); wwdg_kick(t); ok_e += !S.reset_req;
        /* (2) latest 에 refresh → OK */
        sim_power_on(32000, P); sim_run_us(warm); wwdg_init(tb, t, wv, false);
        sim_run_us(w.latest_us); if (!S.reset_req) wwdg_kick(t); ok_l += !S.reset_req;
        /* (3) earliest 보다 1 tick 넘게 일찍 → 항상 리셋 */
        if (w.earliest_us > tick_us + 1u) {
            n_early++;
            sim_power_on(32000, P); sim_run_us(warm); wwdg_init(tb, t, wv, false);
            sim_run_us(w.earliest_us - tick_us - 1u); wwdg_kick(t); early += S.reset_req;
        }
        /* (4) 안 하면 (latest, reset_us] 사이에 리셋 */
        sim_power_on(32000, P); sim_run_us(warm); wwdg_init(tb, t, wv, false);
        r = until_reset((uint64_t)w.reset_us + 1000u);
        late += (r != UINT64_MAX && r > w.latest_us && r <= w.reset_us);
    }
    CHECK(ok_e == n && ok_l == n);
    CHECK(n_early > 100 && early == n_early);
    CHECK(late == n);

    /* B12: RMW 로 WDGA 만 켜기 — free-running 카운터가 이미 0x3F 이하면 즉시 리셋 */
    sim_power_on(32000, P);
    sim_run_us(7000);                                                /* 0x7F → 약 0x7F-71 = 0x38 (wrap 전) */
    CHECK((REG_READ(WWDG_CR) & 0x40u) == 0);
    REG_WRITE(WWDG_CR, REG_READ(WWDG_CR) | WWDG_CR_WDGA);
    CHECK(S.reset_req && S.reset_src == RCC_CSR_WWDGRSTF);
    sim_power_on(32000, P);
    sim_run_us(7000);
    CHECK(wwdg_init(0, 0x7F, 0x7F, false) == 0 && !S.reset_req);    /* T|WDGA 한 번에 → OK */

    /* B13: EWIF 찌꺼기 — free-running 이 0x40 을 지나며 EWIF 를 세워 둔다 → init 이 지운다 */
    sim_power_on(32000, P);
    sim_run_us(6500);
    CHECK(REG_READ(WWDG_SR) == WWDG_SR_EWIF);
    CHECK(wwdg_init(0, 0x7F, 0x7F, true) == 0 && REG_READ(WWDG_SR) == 0);
}

static void test_part_b_debug_and_cause(void) {
    uint32_t raw = 0;
    /* B14: DBGMCU freeze — 코어가 2 s 서 있어도 카운터가 멈춘다. freeze 없으면 리셋 */
    sim_power_on(32000, 42000000u);
    CHECK(iwdg_init(0, 99) == 0 && wwdg_init(3, 0x7F, 0x7F, false) == 0);
    REG_WRITE(DBGMCU_APB1_FZ, DBG_IWDG_STOP | DBG_WWDG_STOP);
    S.halted = true;
    sim_run_us(2000000);
    S.halted = false;
    CHECK(S.resets == 0);
    iwdg_kick(); wwdg_kick(0x7F);
    REG_WRITE(DBGMCU_APB1_FZ, DBG_WWDG_STOP);                        /* IWDG 는 freeze 안 함 */
    S.halted = true;
    uint64_t r = until_reset(2000000);
    CHECK(r != UINT64_MAX && r <= 12500 + 32 && S.reset_src == RCC_CSR_IWDGRSTF);

    /* B15: 리셋 원인 — 전원 투입 → POR, RMVF 로 지움 */
    sim_power_on(32000, 42000000u);
    CHECK(reset_cause_read_and_clear(&raw) == RC_POR && raw == 0x0E000000u);
    CHECK((REG_READ(RCC_CSR) & RCC_CSR_FLAGS) == 0);
    /* IWDG 리셋 → 부팅 때 읽고 지움 → 이어서 WWDG 리셋 → 구분된다 */
    CHECK(iwdg_init(0, 99) == 0 && until_reset(100000) != UINT64_MAX);
    sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_IWDG && raw == (RCC_CSR_IWDGRSTF | RCC_CSR_PINRSTF));
    CHECK(wwdg_init(0, 0x7F, 0x7F, false) == 0 && until_reset(100000) != UINT64_MAX);
    sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_WWDG && raw == (RCC_CSR_WWDGRSTF | RCC_CSR_PINRSTF));
    /* 안 지우면: 두 번째 부팅에서 IWDG|WWDG 가 같이 보여 구분 불가 */
    CHECK(iwdg_init(0, 99) == 0 && until_reset(100000) != UINT64_MAX);
    sim_ack_reset();
    CHECK(wwdg_init(0, 0x7F, 0x7F, false) == 0 && until_reset(100000) != UINT64_MAX);
    sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_MULTIPLE && raw == 0x64000000u);
    /* 소프트웨어 리셋 / 브라운아웃 / 핀 / LSION 보존 */
    sim_system_reset(RCC_CSR_SFTRSTF); sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_SOFT);
    sim_system_reset(RCC_CSR_BORRSTF); sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_BOR && raw == (RCC_CSR_BORRSTF | RCC_CSR_PINRSTF));
    sim_system_reset(0); sim_ack_reset();
    CHECK(reset_cause_read_and_clear(&raw) == RC_PIN && raw == RCC_CSR_PINRSTF);
    REG_WRITE(RCC_CSR, RCC_CSR_LSION);
    sim_system_reset(RCC_CSR_SFTRSTF); sim_ack_reset();
    REG_WRITE(RCC_CSR, RCC_CSR_LSION);                               /* 부팅 코드가 RTC 용 LSI 를 켰다 */
    CHECK(reset_cause_read_and_clear(&raw) == RC_SOFT && (REG_READ(RCC_CSR) & RCC_CSR_LSION));
    CHECK(reset_cause_read_and_clear(&raw) == RC_NONE && raw == (RCC_CSR_LSION | RCC_CSR_LSIRDY));
}

/* ======================= Part C: 부팅 + 제어 루프 시뮬레이션 ======================= */
#define PCLK1_HZ     42000000u
#define LOOP_MIN_US  9000u                 /* 10 ms ± 1 ms 지터 */
#define LOOP_MAX_US  11000u
static wwdg_cfg_t g_wcfg;
static iwdg_cfg_t g_icfg;

static int fw_boot(void) {
    uint32_t raw = 0;
    reset_cause_t rc = reset_cause_read_and_clear(&raw);
    if (g_noinit.magic != NOINIT_MAGIC) { memset(&g_noinit, 0, sizeof g_noinit); g_noinit.magic = NOINIT_MAGIC; }
    g_noinit.boots++;
    g_noinit.last_cause = rc; g_noinit.last_raw = raw;
    if (g_verbose) {
        printf("  [boot %u] t=%llu us cause=%s csr=0x%08X", g_noinit.boots, (unsigned long long)S.now_us,
               RC_NAME[rc], raw);
        if (g_noinit.ewi_count) printf(" | EWI last words: counter=0x%02X stage=%u", g_noinit.ewi_counter, g_noinit.ewi_stage);
        printf("\n");
    }
    g_noinit.ewi_count = 0;
    g_motor_enable = 1;
    if (wwdg_pick(PCLK1_HZ, LOOP_MIN_US, LOOP_MAX_US, true, &g_wcfg) != 0) return -1;
    if (iwdg_pick(30, 100, 17000, 47000, &g_icfg) != 0) return -1;
    if (iwdg_init(g_icfg.pr, g_icfg.rlr) != 0) return -1;
    if (wwdg_init(g_wcfg.wdgtb, g_wcfg.t, g_wcfg.w, true) != 0) return -1;
    nvic_enable_irq(WWDG_IRQn);
    return 0;
}

typedef enum { SC_NORMAL, SC_HANG, SC_RUNAWAY, SC_CLOCK_FAIL } scen_t;
typedef struct { unsigned kicks; uint64_t last_kick_us, reset_after_kick_us; uint32_t min_gap, max_gap; } loop_st_t;

/* dur_us 동안 제어 루프. fault_us 이후 scenario 고장 주입. 리셋되면 멈춘다. */
static void run_loop(scen_t sc, uint64_t dur_us, uint64_t fault_us, loop_st_t *st) {
    memset(st, 0, sizeof *st);
    st->min_gap = UINT32_MAX;
    uint64_t t0 = S.now_us, prev = S.now_us;
    st->last_kick_us = S.now_us;
    while (S.now_us - t0 < dur_us && !S.reset_req) {
        bool faulty = sc != SC_NORMAL && S.now_us - t0 >= fault_us;
        if (faulty && sc == SC_HANG) { g_stage = 2; sim_run_us(dur_us); break; }        /* 제어 계산에서 무한 루프 */
        if (faulty && sc == SC_CLOCK_FAIL) { S.pclk1_hz = 0; g_stage = 1; sim_run_us(dur_us); break; }
        uint32_t gap = faulty ? 100u : LOOP_MIN_US + xs32() % (LOOP_MAX_US - LOOP_MIN_US + 1u);  /* 폭주: 100 µs 마다 kick */
        g_stage = 1; sim_run_us(gap / 2u);
        g_stage = 3; sim_run_us(gap - gap / 2u);
        if (S.reset_req) break;
        uint32_t g = (uint32_t)(S.now_us - prev);
        if (g < st->min_gap) st->min_gap = g;
        if (g > st->max_gap) st->max_gap = g;
        prev = S.now_us;
        wwdg_kick(g_wcfg.t);
        if (S.reset_req) break;
        iwdg_kick();
        st->kicks++;
        st->last_kick_us = S.now_us;
    }
    if (S.reset_req) st->reset_after_kick_us = S.reset_us - st->last_kick_us;
}

static void test_part_c(void) {
    wwdg_cfg_t c = { 0, 0, 0, { 0, 0, 0, 0 } }, b = c;
    /* C1: 10 ms ± 1 ms 루프의 창 (EWI 없이 / EWI 사용) */
    CHECK(wwdg_pick(PCLK1_HZ, 9000, 11000, false, &c) == 0 && c.wdgtb == 1 && c.t == 0x79 && c.w == 0x4B);
    CHECK(c.win.earliest_us == 8973 && c.win.latest_us == 11117 && c.win.reset_us == 11313);
    CHECK(wwdg_pick(PCLK1_HZ, 9000, 11000, true, &c) == 0 && c.wdgtb == 1 && c.t == 0x7A && c.w == 0x4C);
    CHECK(c.win.earliest_us == 8973 && c.win.quiet_us == 11117 && c.win.latest_us == 11312 && c.win.reset_us == 11508);
    CHECK(wwdg_pick(PCLK1_HZ, 0, 6144, false, &c) == 0 && c.wdgtb == 0 && c.t == 0x7F && c.w == 0x7F);
    CHECK(wwdg_pick(PCLK1_HZ, 0, 49153, false, &c) == -1);           /* 49.152 ms 초과는 WWDG 로 불가 → IWDG */
    CHECK(wwdg_pick(PCLK1_HZ, 9000, 8000, false, &c) == -2);
    unsigned same = 0, n = 1000;
    for (unsigned i = 0; i < n; i++) {
        uint32_t pclk = 8000000u + xs32() % 76000000u, mx = xs32() % 60000u, mn = mx ? xs32() % mx : 0;
        bool ewi = i & 1;
        int ra = wwdg_pick(pclk, mn, mx, ewi, &c), rb = wwdg_pick_brute(pclk, mn, mx, ewi, &b);
        if (ra == rb && (ra != 0 || (c.wdgtb == b.wdgtb && c.t == b.t && c.w == b.w))) same++;
    }
    CHECK(same == n);

    /* C1b: EWI 를 켜면서 ewi=false 로 고른 창을 쓰면 → 정상 동작에서도 EWI 가 헛울린다 (리셋은 없음) */
    loop_st_t st;
    sim_power_on(32000, PCLK1_HZ);
    CHECK(wwdg_pick(PCLK1_HZ, 9000, 11000, false, &g_wcfg) == 0 && wwdg_init(g_wcfg.wdgtb, g_wcfg.t, g_wcfg.w, true) == 0);
    nvic_enable_irq(WWDG_IRQn);
    g_motor_enable = 1;
    run_loop(SC_NORMAL, 3000000u, 0, &st);
    CHECK(S.resets == 0 && S.isr_calls > 0 && g_motor_enable == 0);
    if (g_verbose) printf("  ewi=false window + EWI on: %u false EWIs in 3 s (%u kicks), resets=%u\n",
                          S.isr_calls, st.kicks, S.resets);

    /* C2: 정상 3 s / 멈춤 / 폭주 / 클럭 고장 — 부팅 로그와 검출 지연 */
    sim_power_on(32000, PCLK1_HZ);
    memset(&g_noinit, 0xA5, sizeof g_noinit);                        /* 전원 투입 직후 RAM = 쓰레기 */
    CHECK(fw_boot() == 0 && g_noinit.boots == 1 && g_noinit.last_cause == RC_POR);
    run_loop(SC_NORMAL, 3000000u, 0, &st);
    CHECK(S.resets == 0 && st.kicks > 270 && st.min_gap >= LOOP_MIN_US && st.max_gap <= LOOP_MAX_US);
    CHECK(S.isr_calls == 0 && g_motor_enable == 1);                  /* 정상 동작 중 EWI 헛울림 없음 */
    if (g_verbose) printf("  normal: %u kicks in 3 s, gap %u..%u us, resets=%u, EWI=%u\n",
                          st.kicks, st.min_gap, st.max_gap, S.resets, S.isr_calls);

    run_loop(SC_HANG, 3000000u, 1500000u, &st);
    CHECK(S.reset_req && S.reset_src == RCC_CSR_WWDGRSTF && st.reset_after_kick_us <= g_wcfg.win.reset_us &&
          st.reset_after_kick_us > LOOP_MAX_US && g_motor_enable == 0);
    if (g_verbose) printf("  hang  : WWDG reset %llu us after last kick (bound %u us)\n",
                          (unsigned long long)st.reset_after_kick_us, g_wcfg.win.reset_us);
    sim_ack_reset();
    CHECK(fw_boot() == 0 && g_noinit.boots == 2 && g_noinit.last_cause == RC_WWDG);

    run_loop(SC_RUNAWAY, 3000000u, 1000000u, &st);
    CHECK(S.reset_req && S.reset_src == RCC_CSR_WWDGRSTF && st.reset_after_kick_us <= 100u);
    if (g_verbose) printf("  runaway: early-refresh reset %llu us after last good kick\n",
                          (unsigned long long)st.reset_after_kick_us);
    sim_ack_reset();
    CHECK(fw_boot() == 0 && g_noinit.boots == 3 && g_noinit.last_cause == RC_WWDG);

    run_loop(SC_CLOCK_FAIL, 3000000u, 500000u, &st);                 /* PCLK1 정지 → WWDG 도 멈춤 → IWDG 가 잡는다 */
    CHECK(S.reset_req && S.reset_src == RCC_CSR_IWDGRSTF && st.reset_after_kick_us <= g_icfg.t_long_us);
    if (g_verbose) printf("  clock fail: IWDG reset %llu us after last kick (bound %llu us)\n",
                          (unsigned long long)st.reset_after_kick_us, (unsigned long long)g_icfg.t_long_us);
    sim_ack_reset();
    S.pclk1_hz = PCLK1_HZ;                                           /* 리셋 후 HSI 로 다시 부팅했다고 가정 */
    CHECK(fw_boot() == 0 && g_noinit.boots == 4 && g_noinit.last_cause == RC_IWDG);
    CHECK(S.bad == 0);
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b_iwdg();
    test_part_b_wwdg();
    test_part_b_debug_and_cause();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
