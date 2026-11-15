/* BM-M05 추가 연습 — 한 번에 검증
 *
 * 연습 1  dtg_pick_ckd(): 데드타임이 1008 tDTS 를 넘으면 CR1.CKD 로 tDTS 를 2배·4배로 늘린다.
 * 연습 2  ccr_for_on_time(): 데드타임이 깎아 먹는 on-time 을 보상하는 CCR (center-aligned).
 * 연습 3  pwm_steps(): f_tim, f_pwm → ARR, 듀티 단계 수, 비트 수, 정확 여부 (edge vs center).
 * 연습 4  active-low 게이트 드라이버: PWM mode 2 로 뒤집으면 데드타임 구간이 "둘 다 ON" 이 된다 — CCxP 로 뒤집어라.
 *
 * 아래 모델은 solution.c 의 TIM1 모델 그대로 복사 (자기완결). 테스트의 정답은 이 모델의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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

/* TIM1 (advanced-control, APB2) — RM0090 기준 대표값, RM 확인 권장 */
#define TIM1_BASE        0x40010000u
#define TIM_CR1(b)       ((b) + 0x00u)
#define TIM_DIER(b)      ((b) + 0x0Cu)
#define TIM_SR(b)        ((b) + 0x10u)
#define TIM_EGR(b)       ((b) + 0x14u)
#define TIM_CCMR1(b)     ((b) + 0x18u)
#define TIM_CCMR2(b)     ((b) + 0x1Cu)
#define TIM_CCER(b)      ((b) + 0x20u)
#define TIM_CNT(b)       ((b) + 0x24u)
#define TIM_PSC(b)       ((b) + 0x28u)
#define TIM_ARR(b)       ((b) + 0x2Cu)
#define TIM_CCR(b, ch)   ((b) + 0x34u + 4u * (ch))     /* ch 0..2 = CCR1..CCR3 */
#define TIM_BDTR(b)      ((b) + 0x44u)
#define TIM_CR1_CEN      (1u << 0)
#define TIM_CR1_URS      (1u << 2)
#define TIM_CR1_DIR      (1u << 4)                       /* 1 = down. center 모드에서는 읽기 전용 (하드웨어가 씀) */
#define TIM_CR1_CMS_Pos  5u                              /* 00 edge, 01/10/11 center mode 1/2/3 */
#define TIM_CR1_CMS_Msk  (3u << 5)
#define TIM_CR1_ARPE     (1u << 7)
#define TIM_CR1_CKD_Pos  8u                              /* tDTS = tCK_INT × 1/2/4 */
#define TIM_CR1_CKD_Msk  (3u << 8)
#define TIM_DIER_UIE     (1u << 0)
#define TIM_SR_UIF       (1u << 0)                       /* rc_w0 */
#define TIM_SR_CCIF(ch)  (1u << (1u + (ch)))             /* CC1IF..CC3IF, rc_w0 */
#define TIM_EGR_UG       (1u << 0)
#define CCMR_OCPE        (1u << 3)                       /* 채널 바이트 안: OCxPE bit3, OCxM [6:4] */
#define CCMR_OCM_Pos     4u
#define CCMR_REG(b, ch)  ((ch) < 2u ? TIM_CCMR1(b) : TIM_CCMR2(b))
#define CCMR_SH(ch)      (((ch) & 1u) * 8u)              /* CH1/CH3 = 하위 바이트, CH2 = 상위 바이트 */
#define OCM_FORCE_LOW    4u                              /* 100 */
#define OCM_FORCE_HIGH   5u                              /* 101 */
#define OCM_PWM1         6u                              /* 110 */
#define OCM_PWM2         7u                              /* 111 */
#define CCER_E(ch)       (1u << (4u * (ch)))             /* CCxE  */
#define CCER_P(ch)       (2u << (4u * (ch)))             /* CCxP  : 1 = active low */
#define CCER_NE(ch)      (4u << (4u * (ch)))             /* CCxNE */
#define CCER_NP(ch)      (8u << (4u * (ch)))             /* CCxNP */
#define TIM_BDTR_DTG_Msk 0xFFu
#define TIM_BDTR_MOE     (1u << 15)
#define TIM1_UP_TIM10_IRQn 25                            /* F4 벡터 번호 대표값 — 확인 권장 */
#define NCH 3u

/* ======================= mock 코어 (BM-B02 mock 축약판) ======================= */
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

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
static void mock_log(char op, uint32_t addr, uint32_t val) {
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
static uint32_t *hw_reg(uint32_t addr) { mock_reg_t *g = mock_find(addr); return g ? &g->val : NULL; }

/* ======================= NVIC-lite (레벨 감지, 한 sim_step 에 한 번 디스패치) ======================= */
#define NIRQ 32
static uint8_t g_line[NIRQ], g_irq_en[NIRQ];
static void (*g_vec[NIRQ])(void);
static uint32_t g_isr_calls;
void irq_line_set(int irqn, int level) { if (irqn >= 0 && irqn < NIRQ) g_line[irqn] = (uint8_t)(level != 0); }
static void nvic_lite_reset(void) {
    memset(g_line, 0, sizeof g_line); memset(g_irq_en, 0, sizeof g_irq_en);
    memset(g_vec, 0, sizeof g_vec); g_isr_calls = 0;
}

/* ======================= Part A: TIM1 모델 (BM-B14 타이머 모델 축약판 + 비교 출력) ======================= */
#define RUN_SAT 0xFFFFFFu
typedef struct {
    uint32_t *cr1, *dier, *sr, *ccmr1, *ccmr2, *ccer, *cnt, *psc, *arr, *ccr[NCH], *bdtr;
    uint32_t psc_sh, arr_sh, ccr_sh[NCH];   /* shadow = 실제로 비교에 쓰이는 값 */
    uint32_t pc;                            /* 프리스케일러 카운터 0..psc_sh */
    bool     cdown;                         /* center 모드의 현재 방향 (하드웨어 상태) */
    uint8_t  ref[NCH];                      /* OCxREF */
    uint32_t run[NCH];                      /* REF 가 지금 값을 유지한 샘플 수 (이번 샘플 포함) */
    uint8_t  act[NCH], actn[NCH];           /* 극성 적용 전: 1 = 그 스위치를 켜라 (active) */
    uint8_t  oc[NCH], ocn[NCH];             /* 핀 레벨 (극성 적용 후) */
    uint64_t clk;                           /* 흐른 CK_INT 클럭 수 */
    uint32_t uev;                           /* update event 횟수 (overflow/underflow + UG) */
} tim_model_t;
static tim_model_t m;
static unsigned g_chid[NCH] = { 0u, 1u, 2u };

static uint32_t tim_cms(void)  { return (*m.cr1 & TIM_CR1_CMS_Msk) >> TIM_CR1_CMS_Pos; }
static bool     tim_down(void) { return tim_cms() ? m.cdown : (*m.cr1 & TIM_CR1_DIR) != 0u; }
static void     set_cdir(bool down) {
    m.cdown = down;
    *m.cr1 = down ? (*m.cr1 | TIM_CR1_DIR) : (*m.cr1 & ~TIM_CR1_DIR);   /* 소프트웨어가 DIR 로 읽는다 */
}
static uint32_t tim_ccmr_byte(unsigned ch) { return (*(ch < 2u ? m.ccmr1 : m.ccmr2) >> CCMR_SH(ch)) & 0xFFu; }
static uint32_t tim_ocm(unsigned ch) { return (tim_ccmr_byte(ch) >> CCMR_OCM_Pos) & 7u; }
static bool     tim_ocpe(unsigned ch) { return (tim_ccmr_byte(ch) & CCMR_OCPE) != 0u; }

/* 주어진 헬퍼: update event. preload → shadow (ARPE/OCxPE=0 이면 이미 같은 값), UIF (UG + URS=1 이면 안 세움) */
static void tim_uev(bool by_ug) {
    m.psc_sh = *m.psc;
    m.arr_sh = *m.arr;
    for (unsigned ch = 0; ch < NCH; ch++) m.ccr_sh[ch] = *m.ccr[ch];
    m.uev++;
    if (!(by_ug && (*m.cr1 & TIM_CR1_URS))) *m.sr |= TIM_SR_UIF;
}
/* 주어진 헬퍼: EGR.UG — 프리스케일러·카운터 재초기화 + update. down(edge) 이면 CNT = ARR, 아니면 0 */
static void tim_ug(void) {
    m.pc = 0u;
    tim_uev(true);
    if (tim_cms()) { set_cdir(false); *m.cnt = 0u; }
    else *m.cnt = (*m.cr1 & TIM_CR1_DIR) ? m.arr_sh : 0u;
}

/* OCxREF 한 샘플. ocm = OCxM, cnt = 지금 CNT, ccr = shadow CCR, down = 지금 방향, prev = 이전 REF.
 *  - 100 → 0, 101 → 1.
 *  - PWM mode 1 (110): up 이면 CNT <  CCR 일 때 1,  down 이면 CNT <= CCR 일 때 1.
 *  - PWM mode 2 (111): mode 1 의 반대.
 *  - 그 밖의 모드 (frozen 등): 이 모델에서는 prev 를 유지. */
static uint8_t pwm_ref(uint32_t ocm, uint32_t cnt, uint32_t ccr, bool down, uint8_t prev) {
    uint8_t m1 = down ? (uint8_t)(cnt <= ccr) : (uint8_t)(cnt < ccr);
    switch (ocm) {
    case OCM_FORCE_LOW:  return 0u;
    case OCM_FORCE_HIGH: return 1u;
    case OCM_PWM1:       return m1;
    case OCM_PWM2:       return (uint8_t)!m1;
    default:             return prev;
    }
}

/* 카운터 클럭 1개 (프리스케일러를 통과한 클럭). 16비트 CNT, shadow ARR (m.arr_sh) 기준.
 *  - edge up   (CMS=00, DIR=0): CNT == ARR 이면 CNT = 0 + tim_uev(false), 아니면 CNT+1.
 *  - edge down (CMS=00, DIR=1): CNT == 0 이면 tim_uev(false) 후 CNT = (새) ARR, 아니면 CNT−1.
 *  - center    (CMS≠00): 올라가는 중이면 CNT+1, 그 값이 ARR 이면 방향 = down (set_cdir) + tim_uev(false).
 *                         내려가는 중이면 CNT−1, 그 값이 0 이면 방향 = up + tim_uev(false).
 *    → 0,1,…,ARR−1 (up) / ARR,ARR−1,…,1 (down): 한 주기 2·ARR 카운트. */
static void tim_count_tick(void) {
    if (tim_cms() == 0u) {
        if (!(*m.cr1 & TIM_CR1_DIR)) {
            if (*m.cnt == m.arr_sh) { *m.cnt = 0u; tim_uev(false); }
            else *m.cnt = (*m.cnt + 1u) & 0xFFFFu;
        } else {
            if (*m.cnt == 0u) { tim_uev(false); *m.cnt = m.arr_sh; }
            else *m.cnt = *m.cnt - 1u;
        }
    } else if (!m.cdown) {
        *m.cnt = (*m.cnt + 1u) & 0xFFFFu;
        if (*m.cnt == m.arr_sh) { set_cdir(true); tim_uev(false); }
    } else {
        *m.cnt = (*m.cnt - 1u) & 0xFFFFu;
        if (*m.cnt == 0u) { set_cdir(false); tim_uev(false); }
    }
}

/* 한 PWM 주기의 CK_INT 클럭 수. center = CMS ≠ 0.
 *  edge (up/down): (PSC+1)(ARR+1),  center: (PSC+1)·2·ARR.  f_pwm = f_tim / 이 값. */
static uint64_t pwm_period_clk(uint32_t psc, uint32_t arr, bool center) {
    return (uint64_t)(psc + 1u) * (center ? 2u * (uint64_t)arr : (uint64_t)arr + 1u);
}

/* 주어진 헬퍼: 비교 일치 플래그 CCxIF. edge 는 일치마다, center mode 1 은 down 일 때만, 2 는 up 일 때만, 3 은 둘 다 */
static void tim_compare_flags(void) {
    uint32_t cms = tim_cms();
    bool down = tim_down();
    for (unsigned ch = 0; ch < NCH; ch++)
        if (*m.cnt == m.ccr_sh[ch] && (cms == 0u || cms == 3u || (cms == 1u && down) || (cms == 2u && !down)))
            *m.sr |= TIM_SR_CCIF(ch);
}

/* ======================= Part B: CCR preload 와 데드타임 ======================= */
/* CCRx 쓰기 훅. 16비트로 자르고, 그 채널의 OCxPE 가 0 이면 shadow (m.ccr_sh[ch]) 도 즉시 바꾼다.
 *  OCxPE = 1 이면 preload 에만 저장 → 다음 UEV 에 tim_uev 가 옮긴다. */
static uint32_t on_w_ccr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o;
    unsigned ch = *(const unsigned *)c;
    v &= 0xFFFFu;
    if (!tim_ocpe(ch)) m.ccr_sh[ch] = v;
    return v;
}

/* BDTR.DTG[7:0] → 데드타임 (tDTS 단위 정수). RM0090 TIM1_BDTR 기준 (확인 권장):
 *  DTG[7:5] = 0xx : DTG[7:0]                × 1
 *  DTG[7:5] = 10x : (64 + DTG[5:0])         × 2
 *  DTG[7:5] = 110 : (32 + DTG[4:0])         × 8
 *  DTG[7:5] = 111 : (32 + DTG[4:0])         × 16 */
static uint32_t dtg_decode(uint32_t dtg) {
    dtg &= 0xFFu;
    if ((dtg & 0x80u) == 0u)    return dtg;
    if ((dtg & 0xC0u) == 0x80u) return (64u + (dtg & 0x3Fu)) * 2u;
    if ((dtg & 0xE0u) == 0xC0u) return (32u + (dtg & 0x1Fu)) * 8u;
    return (32u + (dtg & 0x1Fu)) * 16u;
}

/* 요청 데드타임 [ns] 이상인 가장 작은 표현 가능 값을 고른다 (짧으면 관통 위험 → 반올림 금지, 올림).
 *  ticks = ceil(dead_ns · f_dts_hz / 1e9) 를 정수로. 표현 가능 집합: 0..127, 128..254 (2 간격),
 *  256..504 (8 간격), 512..1008 (16 간격). ticks > 1008 이거나 f_dts_hz == 0 이면 −1, 성공이면 *dtg 채우고 0. */
static int dtg_encode(uint32_t dead_ns, uint32_t f_dts_hz, uint8_t *dtg) {
    if (!dtg || f_dts_hz == 0u) return -1;
    uint64_t t = ((uint64_t)dead_ns * f_dts_hz + 999999999u) / 1000000000u;
    if (t <= 127u)       *dtg = (uint8_t)t;
    else if (t <= 254u)  *dtg = (uint8_t)(0x80u | ((t + 1u) / 2u - 64u));
    else if (t <= 504u)  *dtg = (uint8_t)(0xC0u | ((t + 7u) / 8u - 32u));
    else if (t <= 1008u) *dtg = (uint8_t)(0xE0u | ((t + 15u) / 16u - 32u));
    else return -1;
    return 0;
}
/* 주어진 헬퍼: DTG 가 실제로 만드는 데드타임 [ps] (반올림) */
static uint64_t dtg_ps(uint8_t dtg, uint32_t f_dts_hz) {
    return ((uint64_t)dtg_decode(dtg) * 1000000000000ull + f_dts_hz / 2u) / f_dts_hz;
}
static uint32_t tim_dt_clocks(void) {          /* 데드타임을 CK_INT 클럭 수로: DTG × 2^CKD */
    uint32_t ckd = (*m.cr1 & TIM_CR1_CKD_Msk) >> TIM_CR1_CKD_Pos;
    return dtg_decode(*m.bdtr & TIM_BDTR_DTG_Msk) << (ckd > 2u ? 2u : ckd);
}

/* 출력단 한 채널. 입력: m.ref[ch], m.run[ch] (REF 가 현재 값을 유지한 샘플 수, 이번 포함),
 *  CCER 의 E/P/NE/NP, BDTR.MOE, dt = tim_dt_clocks().
 *  - MOE = 0                 : act = actn = 0 (모델: 두 핀 모두 0 = 꺼짐. 실제 idle 레벨은 OSSI/OISx — RM 확인)
 *  - E 와 NE 둘 다 1 (상보)  : act  = REF==1 이고 run > dt   (REF 상승을 dt 만큼 늦춤)
 *                              actn = REF==0 이고 run > dt   (REF 하강을 기준으로 dt 만큼 늦춤)
 *  - E 만 1                  : act = REF (데드타임 없음),  actn = 0
 *  - 그 밖                   : 둘 다 0 (모델 범위 밖)
 *  핀: oc = (MOE && E) ? act ^ P : 0,   ocn = (MOE && NE) ? actn ^ NP : 0. */
static void out_stage(unsigned ch) {
    uint32_t ccer = *m.ccer;
    bool e = ccer & CCER_E(ch), p = ccer & CCER_P(ch), ne = ccer & CCER_NE(ch), np = ccer & CCER_NP(ch);
    bool moe = (*m.bdtr & TIM_BDTR_MOE) != 0u;
    uint32_t dt = tim_dt_clocks();
    uint8_t a = 0u, an = 0u;
    if (moe && e && ne) {
        a  = (uint8_t)(m.ref[ch]  && m.run[ch] > dt);
        an = (uint8_t)(!m.ref[ch] && m.run[ch] > dt);
    } else if (moe && e) {
        a = m.ref[ch];
    }
    m.act[ch] = a; m.actn[ch] = an;
    m.oc[ch]  = (uint8_t)((moe && e)  ? (a ^ (uint8_t)p)   : 0u);
    m.ocn[ch] = (uint8_t)((moe && ne) ? (an ^ (uint8_t)np) : 0u);
}

/* 주어진: 모든 채널의 REF 계산 + run 길이 + 출력단 */
static void tim_outputs(void) {
    bool down = tim_down();
    for (unsigned ch = 0; ch < NCH; ch++) {
        uint8_t r = pwm_ref(tim_ocm(ch), *m.cnt, m.ccr_sh[ch], down, m.ref[ch]);
        if (r == m.ref[ch]) { if (m.run[ch] < RUN_SAT) m.run[ch]++; }
        else { m.ref[ch] = r; m.run[ch] = 1u; }
        out_stage(ch);
    }
}
/* 주어진: CK_INT 클럭 1개 = 프리스케일러 → (카운터 + 비교 플래그) → 출력 → IRQ 선 */
static void tim_clock_once(void) {
    m.clk++;
    if ((*m.cr1 & TIM_CR1_CEN) && m.arr_sh != 0u) {
        if (m.pc >= m.psc_sh) { m.pc = 0u; tim_count_tick(); tim_compare_flags(); }
        else m.pc++;
    }
    tim_outputs();
    irq_line_set(TIM1_UP_TIM10_IRQn, (*m.sr & TIM_SR_UIF) && (*m.dier & TIM_DIER_UIE));
}
void sim_step(void) {
    tim_clock_once();
    for (int n = 0; n < NIRQ; n++)
        if (g_line[n] && g_irq_en[n] && g_vec[n]) { g_isr_calls++; g_vec[n](); }
}

/* 레지스터 훅 (주어짐) */
static uint32_t on_w_cr1(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    v &= 0x3FFu;
    if (v & TIM_CR1_CMS_Msk) v = (v & ~TIM_CR1_DIR) | (m.cdown ? TIM_CR1_DIR : 0u);   /* center: DIR 읽기 전용 */
    return v;
}
static uint32_t on_w_sr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)c; return o & v; } /* rc_w0 */
static uint32_t on_w_egr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    if (v & TIM_EGR_UG) tim_ug();
    return 0u;
}
static uint32_t on_w_16(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)o; (void)c; return v & 0xFFFFu; }
static uint32_t on_w_arr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    v &= 0xFFFFu;
    if (!(*m.cr1 & TIM_CR1_ARPE)) m.arr_sh = v;
    return v;
}

static void tim_model_init(void) {
    const uint32_t b = TIM1_BASE;
    mock_reset();
    nvic_lite_reset();
    memset(&m, 0, sizeof m);
    mock_add_reg(TIM_CR1(b), 0u, NULL, on_w_cr1, NULL);
    mock_add_reg(TIM_DIER(b), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_SR(b), 0u, NULL, on_w_sr, NULL);
    mock_add_reg(TIM_EGR(b), 0u, NULL, on_w_egr, NULL);
    mock_add_reg(TIM_CCMR1(b), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_CCMR2(b), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_CCER(b), 0u, NULL, NULL, NULL);
    mock_add_reg(TIM_CNT(b), 0u, NULL, on_w_16, NULL);
    mock_add_reg(TIM_PSC(b), 0u, NULL, on_w_16, NULL);
    mock_add_reg(TIM_ARR(b), 0xFFFFu, NULL, on_w_arr, NULL);
    for (unsigned ch = 0; ch < NCH; ch++) mock_add_reg(TIM_CCR(b, ch), 0u, NULL, on_w_ccr, &g_chid[ch]);
    mock_add_reg(TIM_BDTR(b), 0u, NULL, on_w_16, NULL);
    m.cr1 = hw_reg(TIM_CR1(b));     m.dier = hw_reg(TIM_DIER(b));   m.sr = hw_reg(TIM_SR(b));
    m.ccmr1 = hw_reg(TIM_CCMR1(b)); m.ccmr2 = hw_reg(TIM_CCMR2(b)); m.ccer = hw_reg(TIM_CCER(b));
    m.cnt = hw_reg(TIM_CNT(b));     m.psc = hw_reg(TIM_PSC(b));     m.arr = hw_reg(TIM_ARR(b));
    for (unsigned ch = 0; ch < NCH; ch++) m.ccr[ch] = hw_reg(TIM_CCR(b, ch));
    m.bdtr = hw_reg(TIM_BDTR(b));
    m.arr_sh = 0xFFFFu;
    for (unsigned ch = 0; ch < NCH; ch++) m.run[ch] = RUN_SAT;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)


enum { CM_UP, CM_DOWN, CM_CENTER1, CM_CENTER2, CM_CENTER3 };
typedef struct { int cm; uint32_t psc, arr, ocm, ccr; bool ocpe; uint32_t ccer, bdtr, ckd; } raw_t;
/* 레지스터로 직접 설정 (Part A/B): 3채널 모두 같은 모드·CCR, UG, SR 지움, CEN */
static void raw_setup(const raw_t *r) {
    const uint32_t b = TIM1_BASE;
    tim_model_init();
    uint32_t cms = r->cm >= CM_CENTER1 ? (uint32_t)(r->cm - CM_CENTER1 + 1) : 0u;
    REG_WRITE(TIM_CR1(b), (cms << TIM_CR1_CMS_Pos) | (r->cm == CM_DOWN ? TIM_CR1_DIR : 0u) |
                          TIM_CR1_ARPE | (r->ckd << TIM_CR1_CKD_Pos));
    REG_WRITE(TIM_PSC(b), r->psc);
    REG_WRITE(TIM_ARR(b), r->arr);
    uint32_t byte = (r->ocm << CCMR_OCM_Pos) | (r->ocpe ? CCMR_OCPE : 0u);
    REG_WRITE(TIM_CCMR1(b), byte | (byte << 8));
    REG_WRITE(TIM_CCMR2(b), byte);
    for (unsigned ch = 0; ch < NCH; ch++) REG_WRITE(TIM_CCR(b, ch), r->ccr);
    REG_WRITE(TIM_CCER(b), r->ccer);
    REG_WRITE(TIM_BDTR(b), r->bdtr);
    REG_WRITE(TIM_EGR(b), TIM_EGR_UG);
    REG_WRITE(TIM_SR(b), 0u);
    REG_WRITE(TIM_CR1(b), REG_READ(TIM_CR1(b)) | TIM_CR1_CEN);
}
/* 주기 시작까지 진행: edge 는 UEV, center 는 골짜기 UEV. 그 스텝의 샘플이 파형 배열의 0번 칸이다. */
static long run_to_start(long max) {
    for (long i = 1; i <= max; i++) {
        uint32_t u = m.uev;
        sim_step();
        if (m.uev != u && (tim_cms() == 0u || !m.cdown)) return i;
    }
    return -1;
}
/* 지금 샘플을 0번 칸으로 P 칸 기록 (P−1 스텝 진행). 배열은 NULL 가능 */
static void capture(long P, unsigned ch, uint8_t *ref, uint8_t *act, uint8_t *actn, uint16_t *cnt) {
    for (long i = 0; i < P; i++) {
        if (i) sim_step();
        if (ref) ref[i] = m.ref[ch];
        if (act) act[i] = m.act[ch];
        if (actn) actn[i] = m.actn[ch];
        if (cnt) cnt[i] = (uint16_t)*m.cnt;
    }
}
static long count1(const uint8_t *a, long n) { long k = 0; for (long i = 0; i < n; i++) k += a[i]; return k; }
#define WMAX 20000
static uint8_t w_ref[WMAX], w_act[WMAX], w_actn[WMAX];
static uint16_t w_cnt[WMAX];

/* 한 설정의 한 주기 파형 (2 주기 + 여유 워밍업 뒤). 성공이면 주기 길이, 실패면 −1 */
static long one_period(const raw_t *r, unsigned ch) {
    raw_setup(r);
    long P0 = (long)pwm_period_clk(r->psc, r->arr, r->cm >= CM_CENTER1);
    if (P0 <= 0 || P0 > WMAX) return -1;
    if (run_to_start(2 * P0 + 10) < 0) return -1;
    long P = run_to_start(2 * P0 + 10);                  /* 측정한 주기 */
    if (P < 0 || P > WMAX) return -1;
    capture(P, ch, w_ref, w_act, w_actn, w_cnt);
    return P;
}


/* ======================= 연습 1: CKD 로 데드타임 범위 넓히기 ======================= */
/* CKD = 0,1,2 → f_DTS = f_tim / 1,2,4. 가장 작은 CKD (= 가장 고운 단계) 로 인코딩되면 그것을 쓴다. */
static int dtg_pick_ckd(uint32_t dead_ns, uint32_t f_tim_hz, uint32_t *ckd, uint8_t *dtg, uint64_t *actual_ps) {
    for (uint32_t k = 0; k <= 2u; k++) {
        uint32_t f_dts = f_tim_hz >> k;
        if (dtg_encode(dead_ns, f_dts, dtg) == 0) {
            *ckd = k;
            *actual_ps = dtg_ps(*dtg, f_dts);
            return 0;
        }
    }
    return -1;
}

/* ======================= 연습 2: 데드타임 보상 CCR ======================= */
/* center, PWM1: REF high = 2·CCR 클럭, CH on = 2·CCR − DT (> 0 일 때).
 * on-time 이 목표 이상이 되는 가장 작은 CCR = ceil((on + DT) / 2), ARR 에서 자른다.
 * on = 0 이면 CCR = 0 (펄스 없음). CCR = ARR 은 100 % (스위칭 없음) 이라 공식이 아니라 "항상 on" 이 된다. */
static uint32_t ccr_for_on_time(uint32_t on_clk, uint32_t dt_clk, uint32_t arr) {
    if (on_clk == 0u) return 0u;
    uint32_t c = (on_clk + dt_clk + 1u) / 2u;
    return c > arr ? arr : c;
}

/* ======================= 연습 3: 해상도 표 ======================= */
typedef struct { uint32_t arr, steps, bits; bool exact; } steps_t;
/* PSC = 0 고정. edge: ARR = round(f_tim/f_pwm) − 1, steps = ARR+1.  center: ARR = round(f_tim/(2 f_pwm)), steps = ARR.
 * ARR 가 2..65535 밖이면 −1. bits = floor(log2(steps)). exact = 주기 클럭 수가 f_tim/f_pwm 과 정확히 같다. */
static int pwm_steps(uint32_t f_tim, uint32_t f_pwm, bool center, steps_t *o) {
    if (f_pwm == 0u) return -1;
    uint64_t den = center ? 2ull * f_pwm : (uint64_t)f_pwm;
    uint64_t n = ((uint64_t)f_tim + den / 2u) / den;           /* center: ARR, edge: ARR+1 */
    uint64_t arr = center ? n : n - 1u;
    if (n == 0u || arr < 2u || arr > 0xFFFFu) return -1;
    o->arr = (uint32_t)arr;
    o->steps = (uint32_t)n;
    o->bits = 0u;
    while ((2ull << o->bits) <= n) o->bits++;
    o->exact = pwm_period_clk(0u, o->arr, center) * f_pwm == f_tim;
    return 0;
}

/* ======================= 연습 4: active-low 드라이버 — 어디서 뒤집나 ======================= */
/* 드라이버가 active-low 면 "스위치 ON" = 핀 0. 한 주기 동안 두 핀이 동시에 0 인 샘플 수를 센다. */
static long both_on_active_low(uint32_t ocm, uint32_t ccer, uint32_t dtg, uint8_t *pin, long n) {
    raw_t q = { CM_CENTER1, 0, 4200, ocm, 2100, true, ccer, dtg | TIM_BDTR_MOE, 0 };
    raw_setup(&q);
    run_to_start(20000); run_to_start(20000);
    long both = 0;
    for (long i = 0; i < n; i++) {
        if (i) sim_step();
        if (pin) pin[i] = (uint8_t)(m.oc[0] | (m.ocn[0] << 1));
        both += (m.oc[0] == 0u && m.ocn[0] == 0u);
    }
    return both;
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        uint32_t ckd = 9; uint8_t d = 0; uint64_t ps = 0;
        CHECK(dtg_pick_ckd(500u, 168000000u, &ckd, &d, &ps) == 0 && ckd == 0u && d == 0x54u && ps == 500000u);
        CHECK(dtg_pick_ckd(8000u, 168000000u, &ckd, &d, &ps) == 0 && ckd == 1u && d == 0xEAu && ps == 8000000u);
        CHECK(dtg_pick_ckd(15000u, 168000000u, &ckd, &d, &ps) == 0 && ckd == 2u && d == 0xE8u && ps == 15238095u);
        CHECK(dtg_pick_ckd(30000u, 168000000u, &ckd, &d, &ps) == -1);
        /* 모델로 확인: CKD=1, DTG=0xEA → 전환마다 정확히 672·2 = 1344 클럭 (8 µs) 둘 다 꺼짐 */
        dtg_pick_ckd(8000u, 168000000u, &ckd, &d, &ps);
        raw_t q = { CM_CENTER1, 0, 4200, OCM_PWM1, 2100, true, CCER_E(0) | CCER_NE(0), d | TIM_BDTR_MOE, ckd };
        long P = one_period(&q, 0u), off = 0;
        for (long i = 0; i < P; i++) off += !w_act[i] && !w_actn[i];
        CHECK(P == 8400 && off == 2 * 1344 && count1(w_act, P) == 4200 - 1344);
    }
    /* ---- 연습 2: 작은 모델 (ARR=200, DT=10) 에서 목표 on-time 전수 ---- */
    {
        int bad = 0, minimal_bad = 0;
        for (uint32_t t = 0; t <= 2u * (200u - 1u) - 10u; t++) {    /* CCR ≤ ARR−1 로 만들 수 있는 범위 */
            uint32_t c = ccr_for_on_time(t, 10u, 200u);
            raw_t q = { CM_CENTER1, 0, 200, OCM_PWM1, c, true, CCER_E(0) | CCER_NE(0), 10u | TIM_BDTR_MOE, 0 };
            long P = one_period(&q, 0u), on = count1(w_act, P);
            if (on < (long)t || on > (long)t + 1) bad++;
            if (c > 0u) {                                           /* 하나 작은 CCR 은 목표 미달이어야 */
                q.ccr = c - 1u; P = one_period(&q, 0u);
                if (count1(w_act, P) >= (long)t) minimal_bad++;
            }
        }
        CHECK(bad == 0 && minimal_bad == 0);
        raw_t top = { CM_CENTER1, 0, 200, OCM_PWM1, ccr_for_on_time(389u, 10u, 200u), true,
                      CCER_E(0) | CCER_NE(0), 10u | TIM_BDTR_MOE, 0 };
        long Pt = one_period(&top, 0u);
        CHECK(top.ccr == 200u && count1(w_act, Pt) == 400 && count1(w_actn, Pt) == 0);   /* 389 요청 → 100 % 로 점프 */
        /* 168 MHz, DT = 84: 12 µs = 2016 클럭 → CCR 1050 (보상 없이 듀티 공식만 쓰면 1008 → 11.5 µs) */
        uint32_t c = ccr_for_on_time(2016u, 84u, 4200u);
        raw_t q = { CM_CENTER1, 0, 4200, OCM_PWM1, c, true, CCER_E(0) | CCER_NE(0), 84u | TIM_BDTR_MOE, 0 };
        long P = one_period(&q, 0u);
        CHECK(c == 1050u && count1(w_act, P) == 2016);
        q.ccr = 1008u; P = one_period(&q, 0u);
        CHECK(count1(w_act, P) == 1932);                              /* 1932 클럭 = 11.5 µs */
    }
    /* ---- 연습 3: 해상도 표 (168 MHz) + 모델로 주기 확인 ---- */
    {
        static const struct { uint32_t f; bool c; uint32_t arr, steps, bits; bool exact; } V[] = {
            { 20000u, false, 8399u, 8400u, 13u, true }, { 20000u, true, 4200u, 4200u, 12u, true },
            { 100000u, false, 1679u, 1680u, 10u, true }, { 100000u, true, 840u, 840u, 9u, true },
            { 33000u, false, 5090u, 5091u, 12u, false }, { 33000u, true, 2545u, 2545u, 11u, false },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
            steps_t o; memset(&o, 0, sizeof o);
            if (pwm_steps(168000000u, V[i].f, V[i].c, &o) != 0 || o.arr != V[i].arr || o.steps != V[i].steps ||
                o.bits != V[i].bits || o.exact != V[i].exact) { bad++; continue; }
            raw_t q = { V[i].c ? CM_CENTER1 : CM_UP, 0, o.arr, OCM_PWM1, 1, true, CCER_E(0), TIM_BDTR_MOE, 0 };
            raw_setup(&q); run_to_start(20000);
            if (run_to_start(20000) != (long)pwm_period_clk(0u, o.arr, V[i].c)) bad++;
        }
        steps_t o;
        CHECK(bad == 0);
        CHECK(pwm_steps(168000000u, 1000u, false, &o) == -1 && pwm_steps(168000000u, 60000000u, true, &o) == -1);
    }
    /* ---- 연습 4: PWM2 로 뒤집기 vs CCxP 로 뒤집기 ---- */
    {
        static uint8_t pa[8400], pb[8400];
        const uint32_t pol = CCER_E(0) | CCER_P(0) | CCER_NE(0) | CCER_NP(0), npol = CCER_E(0) | CCER_NE(0);
        long a0 = both_on_active_low(OCM_PWM1, pol, 0u, pa, 8400);
        long b0 = both_on_active_low(OCM_PWM2, npol, 0u, pb, 8400);
        CHECK(memcmp(pa, pb, sizeof pa) == 0 && a0 == 0 && b0 == 0);     /* DT=0: 핀 파형 완전히 같다 */
        long a1 = both_on_active_low(OCM_PWM1, pol, 84u, pa, 8400);
        long b1 = both_on_active_low(OCM_PWM2, npol, 84u, pb, 8400);
        CHECK(a1 == 0 && b1 == 2 * 84);                                  /* PWM2 쪽: 데드타임 = 관통 168 클럭 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
