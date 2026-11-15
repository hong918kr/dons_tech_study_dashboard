/* BM-M05 PWM: 주파수·듀티 계산, edge/center-aligned, 데드타임
 *
 * Part A — BM-B14 타이머 모델 축약판을 TIM1 (advanced-control) 로 넓히고 CCR1–CCR3 비교 출력을 붙인다.
 *          카운터: edge-aligned up / down, center-aligned (CMS=01/10/11). OCxREF: PWM mode 1 (110) / 2 (111).
 *          한 주기 파형 배열로 f_pwm 공식, 듀티 0%/100% 경계, "center 면 주파수 절반" 을 확인한다.
 * Part B — CCR preload (OCxPE) 유무에 따른 글리치, BDTR.DTG 데드타임 인코딩 (4 구간), 상보 출력 CHx/CHxN 의
 *          데드타임 삽입, 데드타임보다 짧은 펄스 소멸, MOE, 극성.
 * Part C — 168 MHz TIM1, 20 kHz center-aligned 3상 하프브리지: 초기화 드라이버, 듀티 갱신, 골짜기(valley) UEV ISR
 *          에서 도는 제어 루프와 시간 예산, 고장 시 MOE 차단.
 *
 * 레지스터 주소·비트는 STM32F4 (RM0090) 기준 대표값 — 테스트의 정답은 이 파일의 모델 정의다.
 * 시간 단위: sim_step() 한 번 = 타이머 커널 클럭 CK_INT 1개 (168 MHz 면 5.952 ns). 파형 배열 1칸 = 1 클럭.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 파형·표 출력)
 *       starter.c 도 같은 명령 (TODO A1–A3, B1–B4, C1–C3 를 채운다).
 * 타깃: UNIT_TEST 를 빼면 REG_READ/REG_WRITE 가 volatile MMIO 접근이 된다 (드라이버 코드는 그대로).
 */
#define UNIT_TEST 1
#include <math.h>
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

/* TODO A1: OCxREF 한 샘플. ocm = OCxM, cnt = 지금 CNT, ccr = shadow CCR, down = 지금 방향, prev = 이전 REF.
 *  - 100 → 0, 101 → 1.
 *  - PWM mode 1 (110): up 이면 CNT <  CCR 일 때 1,  down 이면 CNT <= CCR 일 때 1.
 *  - PWM mode 2 (111): mode 1 의 반대.
 *  - 그 밖의 모드 (frozen 등): 이 모델에서는 prev 를 유지. */
static uint8_t pwm_ref(uint32_t ocm, uint32_t cnt, uint32_t ccr, bool down, uint8_t prev) {
    (void)ocm; (void)cnt; (void)ccr; (void)down;
    return prev;                                        /* TODO A1 */
}

/* TODO A2: 카운터 클럭 1개 (프리스케일러를 통과한 클럭). 16비트 CNT, shadow ARR (m.arr_sh) 기준.
 *  - edge up   (CMS=00, DIR=0): CNT == ARR 이면 CNT = 0 + tim_uev(false), 아니면 CNT+1.
 *  - edge down (CMS=00, DIR=1): CNT == 0 이면 tim_uev(false) 후 CNT = (새) ARR, 아니면 CNT−1.
 *  - center    (CMS≠00): 올라가는 중이면 CNT+1, 그 값이 ARR 이면 방향 = down (set_cdir) + tim_uev(false).
 *                         내려가는 중이면 CNT−1, 그 값이 0 이면 방향 = up + tim_uev(false).
 *    → 0,1,…,ARR−1 (up) / ARR,ARR−1,…,1 (down): 한 주기 2·ARR 카운트. */
static void tim_count_tick(void) {
    /* TODO A2: 지금은 카운터가 멈춰 있다 */
}

/* TODO A3: 한 PWM 주기의 CK_INT 클럭 수. center = CMS ≠ 0.
 *  edge (up/down): (PSC+1)(ARR+1),  center: (PSC+1)·2·ARR.  f_pwm = f_tim / 이 값. */
static uint64_t pwm_period_clk(uint32_t psc, uint32_t arr, bool center) {
    (void)center;
    return (uint64_t)(psc + 1u) * ((uint64_t)arr + 1u);   /* TODO A3: 흔한 첫 시도 — center 를 무시 */
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
/* TODO B1: CCRx 쓰기 훅. 16비트로 자르고, 그 채널의 OCxPE 가 0 이면 shadow (m.ccr_sh[ch]) 도 즉시 바꾼다.
 *  OCxPE = 1 이면 preload 에만 저장 → 다음 UEV 에 tim_uev 가 옮긴다. */
static uint32_t on_w_ccr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o;
    unsigned ch = *(const unsigned *)c;
    bool pe = tim_ocpe(ch); (void)pe;
    return v & 0xFFFFu;                                 /* TODO B1: shadow 는 UEV 때만 바뀐다 (OCxPE 무시) */
}

/* TODO B2: BDTR.DTG[7:0] → 데드타임 (tDTS 단위 정수). RM0090 TIM1_BDTR 기준 (확인 권장):
 *  DTG[7:5] = 0xx : DTG[7:0]                × 1
 *  DTG[7:5] = 10x : (64 + DTG[5:0])         × 2
 *  DTG[7:5] = 110 : (32 + DTG[4:0])         × 8
 *  DTG[7:5] = 111 : (32 + DTG[4:0])         × 16 */
static uint32_t dtg_decode(uint32_t dtg) {
    return dtg & 0xFFu;                                 /* TODO B2: 흔한 첫 시도 — DTG 를 선형으로 읽음 */
}

/* TODO B3: 요청 데드타임 [ns] 이상인 가장 작은 표현 가능 값을 고른다 (짧으면 관통 위험 → 반올림 금지, 올림).
 *  ticks = ceil(dead_ns · f_dts_hz / 1e9) 를 정수로. 표현 가능 집합: 0..127, 128..254 (2 간격),
 *  256..504 (8 간격), 512..1008 (16 간격). ticks > 1008 이거나 f_dts_hz == 0 이면 −1, 성공이면 *dtg 채우고 0. */
static int dtg_encode(uint32_t dead_ns, uint32_t f_dts_hz, uint8_t *dtg) {
    if (!dtg || f_dts_hz == 0u) return -1;
    uint64_t t = (uint64_t)dead_ns * f_dts_hz / 1000000000u;   /* TODO B3: 내림 + 선형 = 흔한 첫 시도 */
    if (t > 255u) return -1;
    *dtg = (uint8_t)t;
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

/* TODO B4: 출력단 한 채널. 입력: m.ref[ch], m.run[ch] (REF 가 현재 값을 유지한 샘플 수, 이번 포함),
 *  CCER 의 E/P/NE/NP, BDTR.MOE, dt = tim_dt_clocks().
 *  - MOE = 0                 : act = actn = 0 (모델: 두 핀 모두 0 = 꺼짐. 실제 idle 레벨은 OSSI/OISx — RM 확인)
 *  - E 와 NE 둘 다 1 (상보)  : act  = REF==1 이고 run > dt   (REF 상승을 dt 만큼 늦춤)
 *                              actn = REF==0 이고 run > dt   (REF 하강을 기준으로 dt 만큼 늦춤)
 *  - E 만 1                  : act = REF (데드타임 없음),  actn = 0
 *  - 그 밖                   : 둘 다 0 (모델 범위 밖)
 *  핀: oc = (MOE && E) ? act ^ P : 0,   ocn = (MOE && NE) ? actn ^ NP : 0. */
static void out_stage(unsigned ch) {
    uint32_t ccer = *m.ccer;
    bool p = ccer & CCER_P(ch), np = ccer & CCER_NP(ch);
    uint32_t dt = tim_dt_clocks(); (void)dt;
    m.act[ch] = m.ref[ch]; m.actn[ch] = (uint8_t)!m.ref[ch];      /* TODO B4: 데드타임·MOE·enable 없음 */
    m.oc[ch]  = (uint8_t)(m.act[ch] ^ (uint8_t)p);
    m.ocn[ch] = (uint8_t)(m.actn[ch] ^ (uint8_t)np);
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

/* ======================= Part C: 3상 PWM 드라이버 ======================= */
typedef struct {
    uint32_t psc, arr;       /* center-aligned: f_pwm = f_tim / ((PSC+1)·2·ARR) */
    uint8_t  dtg;
    uint64_t dead_ps;        /* DTG 가 실제로 만드는 데드타임 */
    uint64_t f_act_mHz;      /* 실제 PWM 주파수 (반올림) */
} pwm_cfg_t;
enum { PWM_OK = 0, PWM_E_ARG = -1, PWM_E_RANGE = -2, PWM_E_DEADTIME = -3 };
static void (*g_ctrl_cb)(void);       /* 앱의 제어 루프 (골짜기마다 한 번) */
static uint32_t g_ctrl_calls;

/* TODO C1: 3상 center-aligned PWM 시작 (CMS=01, PWM mode 1, OCxPE=1, ARPE=1, CKD=00 → f_DTS = f_tim).
 *  계산: steps = round(f_tim / (2·f_pwm)) = (PSC+1)·ARR,  PSC = ARR ≤ 65535 가 되는 최소값,
 *        ARR = round(steps / (PSC+1)) (≥ 2), DTG = dtg_encode(dead_ns, f_tim). 실패면 레지스터를 건드리지 않고
 *        PWM_E_ARG (f_tim/f_pwm/cfg 0) / PWM_E_RANGE / PWM_E_DEADTIME.
 *  순서: CR1=0 → BDTR=0 (MOE 끔) → PSC, ARR → CR1 = CMS01|ARPE → CCMR1/CCMR2 (PWM1|OCPE, 3채널)
 *        → CCR1..3 = ARR/2 → CCER = E|NE (3채널) → BDTR = DTG → EGR.UG → SR = ~UIF → DIER = UIE
 *        → BDTR = DTG|MOE → CR1 |= CEN.  cfg 의 모든 칸을 채운다. */
static int pwm3_init(uint32_t base, uint32_t f_tim_hz, uint32_t f_pwm_hz, uint32_t dead_ns, pwm_cfg_t *cfg) {
    (void)base; (void)f_tim_hz; (void)f_pwm_hz; (void)dead_ns; (void)cfg;
    return PWM_E_ARG;                                   /* TODO C1 */
}

/* TODO C2: 듀티 갱신과 비상 정지.
 *  pwm_set_duty: duty 는 0.01 % 단위 (0..10000, 넘으면 10000 으로 자름), ch ≥ NCH 면 무시.
 *                center-aligned 이므로 CCR = round(duty·ARR / 10000) (ARR 은 레지스터에서 읽는다).
 *  pwm_emergency_off: BDTR 의 MOE 만 0 으로 (DTG 는 그대로) — 한 번의 쓰기로 6 출력이 모두 꺼진다. */
static void pwm_set_duty(uint32_t base, unsigned ch, uint32_t duty_x10000) {
    (void)base; (void)ch; (void)duty_x10000;            /* TODO C2 */
}
static void pwm_emergency_off(uint32_t base) {
    (void)base;                                         /* TODO C2 */
}

/* TODO C3: 업데이트 ISR. center 모드에서 UEV 는 꼭대기(overflow)와 골짜기(underflow) 두 번 온다.
 *  UIF 가 서 있으면 먼저 지우고 (rc_w0: SR = ~UIF), CR1.DIR == 0 (방금 골짜기를 지나 올라가는 중) 일 때만
 *  g_ctrl_calls++ 후 g_ctrl_cb() 를 부른다. 꼭대기 UEV 는 할 일 없음. */
static void TIM1_UP_TIM10_IRQHandler(void) {
    g_ctrl_calls++;                                     /* TODO C3: 흔한 첫 시도 — 플래그도 방향도 안 본다 */
    if (g_ctrl_cb) g_ctrl_cb();
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                       /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

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
static void bits_str(const uint8_t *a, long n, char *s) { for (long i = 0; i < n; i++) s[i] = (char)('0' + a[i]); s[n] = 0; }
static long count1(const uint8_t *a, long n) { long k = 0; for (long i = 0; i < n; i++) k += a[i]; return k; }
static long rising(const uint8_t *a, long n) { long k = 0; for (long i = 1; i < n; i++) k += (a[i] && !a[i - 1]); return k; }
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

/* ---- Part C 하네스: 제어 루프 (골짜기 ISR) + 계산 시간 모델 + 파형 감시 ---- */
#define KMAX 512
static uint32_t g_cost;                       /* ISR 계산 시간 [CK_INT 클럭]: 결과가 이만큼 뒤에 CCR 에 쓰인다 */
static bool     g_pend;
static uint64_t g_due;
static uint32_t g_pend_duty[NCH];
static uint32_t g_issued[KMAX][NCH];          /* 제어 호출 k 가 쓴 CCR (k = 1부터) */
static int32_t  g_sin48[48];
static uint32_t g_fault_at;                   /* 이 제어 호출에서 과전류 → 비상 정지 (0 = 없음) */
static uint64_t g_fault_clk;                  /* 비상 정지한 클럭 (그 다음 샘플부터 출력 0 이어야) */
static uint32_t g_arr_c;
static void ctrl_sine(void) {                 /* 3상 사인 듀티: 5000 ± 4500 (0.01 %), 한 바퀴 48 주기 */
    uint32_t k = g_ctrl_calls;
    if (g_fault_at && k == g_fault_at) { pwm_emergency_off(TIM1_BASE); g_fault_clk = m.clk; return; }
    for (unsigned ch = 0; ch < NCH; ch++) {
        uint32_t d = (uint32_t)(5000 + g_sin48[(k + 48u - 16u * ch) % 48u]);
        g_pend_duty[ch] = d;
        if (k < KMAX) g_issued[k][ch] = (uint32_t)(((uint64_t)d * g_arr_c + 5000u) / 10000u);
    }
    g_pend = true; g_due = m.clk + g_cost;
}
typedef struct {
    uint32_t hi_dn[NCH], hi_up[NCH], left[NCH];
    uint32_t valleys, pulses, asym, wrong, exp_asym, overlap, last_uev;
    uint64_t last_on[NCH], last_onn[NCH];
    uint32_t min_gap, act_after_fault;
    uint64_t on_clk[NCH], onn_clk[NCH];
} trk_t;
static trk_t T;
static void trk_reset(void) { memset(&T, 0, sizeof T); T.min_gap = UINT32_MAX; T.last_uev = m.uev; }
static void c_step(void) {
    if (g_pend && m.clk >= g_due) {
        for (unsigned ch = 0; ch < NCH; ch++) pwm_set_duty(TIM1_BASE, ch, g_pend_duty[ch]);
        g_pend = false;
    }
    sim_step();
    bool ev = (m.uev != T.last_uev);
    T.last_uev = m.uev;
    for (unsigned ch = 0; ch < NCH; ch++) {
        if (ev && !m.cdown) { T.left[ch] = T.hi_dn[ch]; T.hi_up[ch] = 0u; if (ch == 0) T.valleys++; }
        if (ev && m.cdown && T.valleys >= 2u) {          /* 골짜기 v 를 감싼 펄스가 방금 끝났다 */
            uint32_t v = T.valleys, L = T.left[ch], R = T.hi_up[ch];
            if (ch == 0) T.pulses++;
            if (L != R) T.asym++;
            if (v - 1u < KMAX && (L != g_issued[v - 1u][ch] || R != g_issued[v - 1u][ch])) T.wrong++;
            if (v - 1u < KMAX && g_issued[v - 1u][ch] != g_issued[v - 2u][ch]) T.exp_asym++;
        }
        if (ev && m.cdown) T.hi_dn[ch] = 0u;
        if (m.ref[ch]) { if (m.cdown) T.hi_dn[ch]++; else T.hi_up[ch]++; }
        if (m.act[ch] && m.actn[ch]) T.overlap++;
        if (m.act[ch] && T.last_on[ch] + 1u != m.clk && T.last_onn[ch] && m.clk - T.last_onn[ch] - 1u < T.min_gap)
            T.min_gap = (uint32_t)(m.clk - T.last_onn[ch] - 1u);
        if (m.actn[ch] && T.last_onn[ch] + 1u != m.clk && T.last_on[ch] && m.clk - T.last_on[ch] - 1u < T.min_gap)
            T.min_gap = (uint32_t)(m.clk - T.last_on[ch] - 1u);
        if (m.act[ch]) { T.last_on[ch] = m.clk; T.on_clk[ch]++; }
        if (m.actn[ch]) { T.last_onn[ch] = m.clk; T.onn_clk[ch]++; }
        if (g_fault_clk && m.clk > g_fault_clk && (m.act[ch] || m.actn[ch] || m.oc[ch] || m.ocn[ch]))
            T.act_after_fault++;
    }
}
/* 168 MHz, 20 kHz, 500 ns 로 시작하고 periods 주기만큼 돌린다 */
static int c_run(uint32_t cost, uint32_t periods, uint32_t fault_at, pwm_cfg_t *cfg) {
    tim_model_init();
    g_vec[TIM1_UP_TIM10_IRQn] = TIM1_UP_TIM10_IRQHandler;
    g_irq_en[TIM1_UP_TIM10_IRQn] = 1u;
    g_ctrl_cb = ctrl_sine; g_ctrl_calls = 0; g_cost = cost; g_pend = false; g_fault_at = fault_at; g_fault_clk = 0;
    memset(g_issued, 0, sizeof g_issued);
    int r = pwm3_init(TIM1_BASE, 168000000u, 20000u, 500u, cfg);
    g_arr_c = cfg->arr;
    for (unsigned ch = 0; ch < NCH; ch++) g_issued[0][ch] = cfg->arr / 2u;   /* 시작 듀티 50 % */
    trk_reset();
    if (r != PWM_OK) { for (uint32_t i = 0; i < 1000u; i++) c_step(); return r; }
    uint64_t n = (uint64_t)periods * pwm_period_clk(cfg->psc, cfg->arr, true);
    for (uint64_t i = 0; i < n; i++) c_step();
    return r;
}
static int log_find(char op, uint32_t addr, uint32_t mask, uint32_t want, int from) {
    for (int i = from; i < g_nlog; i++)
        if (g_log[i].op == op && g_log[i].addr == addr && (g_log[i].val & mask) == want) return i;
    return -1;
}

static void verbose_dump(void) {
    static const struct { const char *name; raw_t r; } F[] = {
        { "edge up   PWM1 ARR=9 CCR=3", { CM_UP, 0, 9, OCM_PWM1, 3, true, CCER_E(0), TIM_BDTR_MOE, 0 } },
        { "edge up   PWM2 ARR=9 CCR=3", { CM_UP, 0, 9, OCM_PWM2, 3, true, CCER_E(0), TIM_BDTR_MOE, 0 } },
        { "edge down PWM1 ARR=9 CCR=3", { CM_DOWN, 0, 9, OCM_PWM1, 3, true, CCER_E(0), TIM_BDTR_MOE, 0 } },
        { "center1   PWM1 ARR=5 CCR=2", { CM_CENTER1, 0, 5, OCM_PWM1, 2, true, CCER_E(0), TIM_BDTR_MOE, 0 } },
        { "edge up PSC=1 ARR=4 CCR=2 ", { CM_UP, 1, 4, OCM_PWM1, 2, true, CCER_E(0), TIM_BDTR_MOE, 0 } },
    };
    char s[64];
    printf("== one-period arrays (index 0 = period start) ==\n");
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        long P = one_period(&F[i].r, 0u);
        printf("%s  P=%ld  CNT:", F[i].name, P);
        for (long k = 0; k < P; k++) printf(" %u", w_cnt[k]);
        bits_str(w_ref, P, s);
        printf("  REF: %s\n", s);
    }
    printf("\n== f_pwm at f_tim = 168 MHz (period measured in the model) ==\n");
    static const struct { const char *name; int cm; uint32_t psc, arr; } P[] = {
        { "edge   PSC=0 ARR=8399", CM_UP, 0, 8399 }, { "center PSC=0 ARR=4200", CM_CENTER1, 0, 4200 },
        { "center PSC=0 ARR=8399", CM_CENTER1, 0, 8399 }, { "edge   PSC=0 ARR=4200", CM_UP, 0, 4200 },
        { "edge   PSC=1 ARR=4199", CM_UP, 1, 4199 }, { "center PSC=2 ARR=1400", CM_CENTER3, 2, 1400 },
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        raw_t r = { P[i].cm, P[i].psc, P[i].arr, OCM_PWM1, 1, true, CCER_E(0), TIM_BDTR_MOE, 0 };
        raw_setup(&r);
        run_to_start(40000);
        long per = run_to_start(40000);
        printf("%s  period=%ld clk  f=%.3f Hz  formula=%llu clk\n", P[i].name, per, 168e6 / (double)per,
               (unsigned long long)pwm_period_clk(P[i].psc, P[i].arr, P[i].cm >= CM_CENTER1));
    }
    printf("\n== DTG ranges at f_DTS = 168 MHz (tDTS = 5.952 ns) ==\n");
    static const uint32_t R[][2] = { { 0x00, 0x7F }, { 0x80, 0xBF }, { 0xC0, 0xDF }, { 0xE0, 0xFF } };
    for (int i = 0; i < 4; i++)
        printf("DTG 0x%02X..0x%02X : %4u..%4u tDTS  step %2u  = %7.1f .. %7.1f ns\n", R[i][0], R[i][1],
               dtg_decode(R[i][0]), dtg_decode(R[i][1]), dtg_decode(R[i][0] + 1) - dtg_decode(R[i][0]),
               dtg_ps((uint8_t)R[i][0], 168000000u) / 1000.0, dtg_ps((uint8_t)R[i][1], 168000000u) / 1000.0);
    static const uint32_t NS[] = { 0, 6, 100, 500, 760, 1000, 1520, 2000, 3000, 4000, 6000, 6001 };
    for (size_t i = 0; i < sizeof NS / sizeof NS[0]; i++) {
        uint8_t d = 0;
        int rr = dtg_encode(NS[i], 168000000u, &d);
        if (rr) printf("dtg_encode(%4u ns) -> error %d\n", NS[i], rr);
        else printf("dtg_encode(%4u ns) -> DTG=0x%02X  %4u tDTS  actual %.3f ns\n", NS[i], d, dtg_decode(d),
                    dtg_ps(d, 168000000u) / 1000.0);
    }
    printf("naive 'DTG = ticks' for 1000 ns: write 168 = 0x%02X -> HW decodes %u tDTS = %.3f ns\n", 168u,
           dtg_decode(168u), dtg_ps(168u, 168000000u) / 1000.0);
    printf("\n== 20 kHz center, ARR=4200, DT=84 clk (500 ns): duty -> on-times per period ==\n");
    printf("duty%%    CCR   REF_hi  CH_on[ns]  CHN_on[ns]  dead[ns]x2  eff_duty%%\n");
    static const uint32_t D[] = { 0, 100, 103, 500, 2500, 5000, 7500, 9500, 9897, 9900, 10000 };
    for (size_t i = 0; i < sizeof D / sizeof D[0]; i++) {
        uint32_t ccr = (uint32_t)(((uint64_t)D[i] * 4200u + 5000u) / 10000u);
        raw_t r = { CM_CENTER1, 0, 4200, OCM_PWM1, ccr, true, CCER_E(0) | CCER_NE(0), 84u | TIM_BDTR_MOE, 0 };
        long Pm = one_period(&r, 0u);
        long hi = count1(w_ref, Pm), on = count1(w_act, Pm), onn = count1(w_actn, Pm);
        printf("%6.2f  %5u  %6ld  %9.1f  %10.1f  %10.1f  %8.3f\n", D[i] / 100.0, ccr, hi, on * 1e3 / 168.0,
               onn * 1e3 / 168.0, (Pm - on - onn) * 1e3 / 168.0 / 2.0, 100.0 * (double)on / (double)Pm);
    }
    printf("\n== ISR budget: control at valley, CCR written 'cost' clocks later (ARR=4200) ==\n");
    static const uint32_t C[] = { 100, 2000, 4199, 4200, 6000, 8000 };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        pwm_cfg_t cfg;
        c_run(C[i], 60u, 0u, &cfg);
        printf("cost=%5u clk (%6.2f us)  pulses=%u  asym=%u  wrong=%u  overlap=%u  min_gap=%u\n", C[i],
               C[i] / 168.0, T.pulses, T.asym, T.wrong, T.overlap, T.min_gap);
    }
    printf("\n== bring-up log: first control periods (cost = 1000 clk) ==\n");
    {
        pwm_cfg_t cfg;
        c_run(1000u, 6u, 0u, &cfg);
        printf("cfg: PSC=%u ARR=%u DTG=0x%02X dead=%.3f ns f=%.3f Hz\n", cfg.psc, cfg.arr, cfg.dtg,
               cfg.dead_ps / 1000.0, cfg.f_act_mHz / 1000.0);
        for (uint32_t k = 1; k <= 5; k++)
            printf("ctrl k=%u  CCR A/B/C = %4u %4u %4u  (applied around valley %u)\n", k, g_issued[k][0],
                   g_issued[k][1], g_issued[k][2], k + 1u);
        printf("valleys=%u pulses=%u asym=%u wrong=%u overlap=%u min_gap=%u clk isr_calls=%u ctrl_calls=%u\n",
               T.valleys, T.pulses, T.asym, T.wrong, T.overlap, T.min_gap, g_isr_calls, g_ctrl_calls);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    for (int i = 0; i < 48; i++) g_sin48[i] = (int32_t)lround(4500.0 * sin(2.0 * 3.14159265358979323846 * i / 48.0));
    if (argc > 1 && strcmp(argv[1], "-v") == 0) verbose_dump();
    char s[64];
    long P;

    /* ---- Part A: 이름 붙은 한 주기 파형 ---- */
    raw_t r = { CM_UP, 0, 9, OCM_PWM1, 3, true, CCER_E(0), TIM_BDTR_MOE, 0 };
    P = one_period(&r, 0u); bits_str(w_ref, P > 0 ? P : 0, s);
    CHECK(P == 10 && strcmp(s, "1110000000") == 0 && w_cnt[0] == 0 && w_cnt[9] == 9);
    r.ocm = OCM_PWM2;
    P = one_period(&r, 0u); bits_str(w_ref, P > 0 ? P : 0, s);
    CHECK(P == 10 && strcmp(s, "0001111111") == 0);
    r = (raw_t){ CM_DOWN, 0, 9, OCM_PWM1, 3, true, CCER_E(0), TIM_BDTR_MOE, 0 };
    P = one_period(&r, 0u); bits_str(w_ref, P > 0 ? P : 0, s);
    CHECK(P == 10 && strcmp(s, "0000001111") == 0 && w_cnt[0] == 9 && w_cnt[9] == 0);
    r = (raw_t){ CM_CENTER1, 0, 5, OCM_PWM1, 2, true, CCER_E(0), TIM_BDTR_MOE, 0 };
    P = one_period(&r, 0u); bits_str(w_ref, P > 0 ? P : 0, s);
    CHECK(P == 10 && strcmp(s, "1100000011") == 0 && w_cnt[5] == 5 && w_cnt[6] == 4 && w_cnt[9] == 1);
    r = (raw_t){ CM_UP, 1, 4, OCM_PWM1, 2, true, CCER_E(0), TIM_BDTR_MOE, 0 };
    P = one_period(&r, 0u); bits_str(w_ref, P > 0 ? P : 0, s);
    CHECK(P == 10 && strcmp(s, "1111000000") == 0);

    /* ---- Part A: 듀티 0 % / 100 % 경계 (edge up, PWM1, ARR=99) ---- */
    {
        static const uint32_t CC[] = { 0u, 1u, 99u, 100u, 0xFFFFu };
        static const long WANT[] = { 0, 1, 99, 100, 100 };
        bool ok = true;
        for (int i = 0; i < 5; i++) {
            raw_t q = { CM_UP, 0, 99, OCM_PWM1, CC[i], true, CCER_E(0), TIM_BDTR_MOE, 0 };
            P = one_period(&q, 0u);
            ok = ok && P == 100 && count1(w_ref, P) == WANT[i];
        }
        CHECK(ok);
        raw_t q = { CM_DOWN, 0, 99, OCM_PWM1, 0u, true, CCER_E(0), TIM_BDTR_MOE, 0 };   /* down: 0 % 불가 */
        P = one_period(&q, 0u);
        CHECK(P == 100 && count1(w_ref, P) == 1);
        q = (raw_t){ CM_CENTER1, 0, 100, OCM_PWM1, 100u, true, CCER_E(0), TIM_BDTR_MOE, 0 };  /* center: CCR=ARR → 100 % */
        P = one_period(&q, 0u);
        CHECK(P == 200 && count1(w_ref, P) == 200);
    }

    /* ---- Part A: 전수 — 주기·듀티 공식 (ARR 1..40, CCR 0..ARR+2, PSC 0/2, 4 가지 모드) ---- */
    {
        int bad_per = 0, bad_duty = 0, n = 0;
        static const struct { int cm; uint32_t ocm; } MD[] = {
            { CM_UP, OCM_PWM1 }, { CM_UP, OCM_PWM2 }, { CM_DOWN, OCM_PWM1 }, { CM_CENTER1, OCM_PWM1 } };
        for (int md = 0; md < 4; md++)
            for (uint32_t psc = 0; psc <= 2u; psc += 2u)
                for (uint32_t arr = 1; arr <= 40u; arr++)
                    for (uint32_t ccr = 0; ccr <= arr + 2u; ccr++) {
                        raw_t q = { MD[md].cm, psc, arr, MD[md].ocm, ccr, true, CCER_E(0), TIM_BDTR_MOE, 0 };
                        bool center = MD[md].cm >= CM_CENTER1;
                        long want_p = (long)((psc + 1u) * (center ? 2u * arr : arr + 1u));
                        long hi;
                        if (MD[md].cm == CM_UP)   hi = (long)(psc + 1u) * (ccr < arr + 1u ? ccr : arr + 1u);
                        else if (MD[md].cm == CM_DOWN) hi = (long)(psc + 1u) * (ccr + 1u < arr + 1u ? ccr + 1u : arr + 1u);
                        else hi = (long)(psc + 1u) * 2 * (ccr < arr ? ccr : arr);
                        if (MD[md].ocm == OCM_PWM2) hi = want_p - hi;
                        P = one_period(&q, 0u);
                        n++;
                        if (P != want_p || (long)pwm_period_clk(psc, arr, center) != want_p) { bad_per++; continue; }
                        if (count1(w_ref, P) != hi) bad_duty++;
                    }
        CHECK(n == 7520 && bad_per == 0);
        CHECK(bad_duty == 0);
    }

    /* ---- Part A: 168 MHz 실제 숫자 — 20 kHz 와 "center 면 절반" 의 정확한 비율 ---- */
    {
        raw_t q = { CM_UP, 0, 8399, OCM_PWM1, 4200, true, CCER_E(0), TIM_BDTR_MOE, 0 };
        raw_setup(&q); run_to_start(20000);
        CHECK(run_to_start(20000) == 8400);                     /* 168e6 / 8400 = 20 kHz */
        q.cm = CM_CENTER1; q.arr = 4200; raw_setup(&q); run_to_start(20000);
        CHECK(run_to_start(20000) == 8400);                     /* center: ARR 를 절반으로 해야 같은 20 kHz */
        q.arr = 8399; raw_setup(&q); run_to_start(40000);
        CHECK(run_to_start(40000) == 16798);                    /* 같은 ARR → 2·8399 = 16798 ≠ 16800 */
    }

    /* ---- Part A: CMS 01/10/11 은 파형이 같고 CCxIF 시점만 다르다 (ARR=10, CCR=4, 3 주기) ---- */
    {
        int flags[3] = { 0, 0, 0 };
        bool same = true;
        uint8_t first[20];
        for (int k = 0; k < 3; k++) {
            raw_t q = { CM_CENTER1 + k, 0, 10, OCM_PWM1, 4, true, CCER_E(0), TIM_BDTR_MOE, 0 };
            raw_setup(&q);
            run_to_start(100);
            REG_WRITE(TIM_SR(TIM1_BASE), 0u);                   /* 정렬 중에 선 플래그는 지우고 센다 */
            for (int i = 0; i < 60; i++) {
                sim_step();
                if (*m.sr & TIM_SR_CCIF(0)) { flags[k]++; *m.sr &= ~TIM_SR_CCIF(0); }
                if (i < 20) { if (k == 0) first[i] = m.ref[0]; else same = same && first[i] == m.ref[0]; }
            }
        }
        CHECK(same && flags[0] == 3 && flags[1] == 3 && flags[2] == 6);
    }

    /* ---- Part B: CCR preload — 주기 중간 변경 (edge up, ARR=99, CNT 49 와 50 사이에 씀) ---- */
    {
        long hi[2][2], re[2];
        for (int pe = 0; pe < 2; pe++) {
            raw_t q = { CM_UP, 0, 99, OCM_PWM1, 70, pe == 1, CCER_E(0), TIM_BDTR_MOE, 0 };
            raw_setup(&q); run_to_start(300); run_to_start(300);
            capture(50, 0u, w_ref, NULL, NULL, NULL);           /* CNT 0..49 */
            REG_WRITE(TIM_CCR(TIM1_BASE, 0u), 30u);
            sim_step(); capture(50, 0u, w_ref + 50, NULL, NULL, NULL);
            hi[pe][0] = count1(w_ref, 100);
            sim_step(); capture(100, 0u, w_ref, NULL, NULL, NULL);
            hi[pe][1] = count1(w_ref, 100);
            /* 반대 방향: 30 → 70 */
            q.ccr = 30; raw_setup(&q); run_to_start(300); run_to_start(300);
            capture(50, 0u, w_ref, NULL, NULL, NULL);
            REG_WRITE(TIM_CCR(TIM1_BASE, 0u), 70u);
            sim_step(); capture(50, 0u, w_ref + 50, NULL, NULL, NULL);
            re[pe] = rising(w_ref, 100) + (w_ref[0] ? 1 : 0);
        }
        CHECK(hi[1][0] == 70 && hi[1][1] == 30);                /* preload: 이번 주기 70, 다음 주기 30 */
        CHECK(hi[0][0] == 50 && hi[0][1] == 30);                /* 즉시: 이번 주기 50 = 옛값도 새값도 아닌 펄스 */
        CHECK(re[1] == 1 && re[0] == 2);                        /* 즉시 30→70: 한 주기에 펄스 두 개 */
    }
    /* center 에서는 preload 만으로 부족: down 구간(골짜기 전)에 쓰면 골짜기 UEV 에서 반영 → 비대칭 */
    {
        raw_t q = { CM_CENTER1, 0, 100, OCM_PWM1, 40, true, CCER_E(0), TIM_BDTR_MOE, 0 };
        raw_setup(&q); run_to_start(500);
        long L = 0, R = 0;
        uint32_t c_now = 0; bool dn = false;
        for (int i = 0; i < 200; i++) {                         /* 골짜기 → 꼭대기 → 다음 골짜기 */
            sim_step();
            if (m.cdown) L += m.ref[0];                         /* 펄스 왼쪽 절반 = down 구간의 high */
            if (i == 189) { c_now = *m.cnt; dn = m.cdown; REG_WRITE(TIM_CCR(TIM1_BASE, 0u), 20u); }
        }
        R = m.ref[0];                                           /* 골짜기 샘플 (CNT=0, up) 부터 */
        for (int i = 0; i < 99; i++) { sim_step(); R += m.ref[0]; }
        CHECK(c_now == 10u && dn && L == 40 && R == 20);        /* 왼쪽 31+9 = 40 (옛값 기준), 오른쪽 20 (새값) */
    }

    /* ---- Part B: DTG 인코딩 — 전수 왕복 + 최소성 brute force ---- */
    {
        int bad = 0;
        uint32_t prev = 0;
        for (uint32_t c = 0; c < 256u; c++) {
            uint32_t t = dtg_decode(c);
            if (c && t <= prev) bad++;                          /* 코드에 대해 단조 증가 */
            prev = t;
            uint8_t d = 0;                                      /* f_DTS = 1 GHz → 1 tick = 1 ns */
            if (dtg_encode(t, 1000000000u, &d) != 0 || d != c) bad++;
        }
        CHECK(bad == 0 && dtg_decode(0x7F) == 127u && dtg_decode(0x80) == 128u && dtg_decode(0xBF) == 254u);
        CHECK(dtg_decode(0xC0) == 256u && dtg_decode(0xDF) == 504u && dtg_decode(0xE0) == 512u && dtg_decode(0xFF) == 1008u);
        bad = 0;
        for (uint32_t t = 0; t <= 1100u; t++) {
            int best = -1;
            for (int c = 0; c < 256; c++) if (dtg_decode((uint32_t)c) >= t) { best = c; break; }   /* 단조라 첫 번째 = 최소 */
            uint8_t d = 0;
            int rr = dtg_encode(t, 1000000000u, &d);
            if (best < 0 ? rr != -1 : (rr != 0 || d != (uint8_t)best)) bad++;
        }
        CHECK(bad == 0);
        uint8_t d = 0;
        CHECK(dtg_encode(500u, 168000000u, &d) == 0 && d == 84u && dtg_ps(d, 168000000u) == 500000u);
        CHECK(dtg_encode(1000u, 168000000u, &d) == 0 && d == 0x94u && dtg_decode(d) == 168u);
        CHECK(dtg_encode(100u, 168000000u, &d) == 0 && d == 17u && dtg_ps(d, 168000000u) == 101190u);
        CHECK(dtg_encode(3000u, 168000000u, &d) == 0 && d == 0xDFu && dtg_encode(6000u, 168000000u, &d) == 0 && d == 0xFFu);
        CHECK(dtg_encode(6001u, 168000000u, &d) == -1 && dtg_encode(0u, 168000000u, &d) == 0 && d == 0u);
        CHECK(dtg_encode(100u, 0u, &d) == -1);
    }

    /* ---- Part B: 상보 출력 + 데드타임 파형 (center ARR=50, CCR=20, DT=5) ---- */
    {
        raw_t q = { CM_CENTER1, 0, 50, OCM_PWM1, 20, true, CCER_E(0) | CCER_NE(0), 5u | TIM_BDTR_MOE, 0 };
        P = one_period(&q, 0u);
        long both = 0, off = 0;
        for (long i = 0; i < P; i++) { both += w_act[i] && w_actn[i]; off += !w_act[i] && !w_actn[i]; }
        CHECK(P == 100 && count1(w_ref, P) == 40 && count1(w_act, P) == 35 && count1(w_actn, P) == 55);
        CHECK(both == 0 && off == 10);
        /* 정확한 위치: REF 는 [80,100)∪[0,20) 에서 1. CH 는 85 부터, CHN 은 25..79 */
        CHECK(w_act[84] == 0 && w_act[85] == 1 && w_act[19] == 1 && w_act[20] == 0 &&
              w_actn[24] == 0 && w_actn[25] == 1 && w_actn[79] == 1 && w_actn[80] == 0);
        q.ccer = CCER_E(0);                                     /* 상보 없이 CH 만: 데드타임 없음 */
        P = one_period(&q, 0u);
        CHECK(P == 100 && count1(w_act, P) == 40 && count1(w_actn, P) == 0);
        q.ccer = CCER_E(0) | CCER_P(0) | CCER_NE(0) | CCER_NP(0);   /* 극성 반전: 핀 = act ^ 1 */
        raw_setup(&q); run_to_start(500); run_to_start(500);
        long hi_pin = 0, hi_pinn = 0;
        for (int i = 0; i < 100; i++) { sim_step(); hi_pin += m.oc[0]; hi_pinn += m.ocn[0]; }
        CHECK(hi_pin == 65 && hi_pinn == 45);
    }
    /* ---- Part B: property — 무작위 3000 설정, 관통 없음 + on-time = max(0, H − DT) ---- */
    {
        int bad_overlap = 0, bad_on = 0, bad_pol = 0;
        for (int it = 0; it < 3000; it++) {
            raw_t q;
            q.cm = (int)(xs32() % 5u); q.psc = xs32() % 3u; q.arr = 2u + xs32() % 120u;
            q.ocm = (xs32() & 1u) ? OCM_PWM1 : OCM_PWM2; q.ccr = xs32() % (q.arr + 3u); q.ocpe = true;
            uint32_t pol = xs32() & 3u;
            q.ccer = CCER_E(0) | CCER_NE(0) | ((pol & 1u) ? CCER_P(0) : 0u) | ((pol & 2u) ? CCER_NP(0) : 0u);
            q.bdtr = (xs32() % 70u) | TIM_BDTR_MOE; q.ckd = xs32() % 3u;
            raw_setup(&q);
            long Pp = (long)pwm_period_clk(q.psc, q.arr, q.cm >= CM_CENTER1);
            uint32_t dt = dtg_decode(q.bdtr & 0xFFu) << q.ckd;
            for (long i = 0; i < 2 * Pp + 600; i++) sim_step();
            long hi = 0, on = 0, onn = 0;
            for (long i = 0; i < Pp; i++) {
                sim_step();
                hi += m.ref[0]; on += m.act[0]; onn += m.actn[0];
                if (m.act[0] && m.actn[0]) bad_overlap++;
                if (m.oc[0] != (m.act[0] ^ (pol & 1u)) || m.ocn[0] != (m.actn[0] ^ (pol >> 1))) bad_pol++;
            }
            long lo = Pp - hi;
            long want_on  = hi == Pp ? Pp : (hi > (long)dt ? hi - (long)dt : 0);
            long want_onn = lo == Pp ? Pp : (lo > (long)dt ? lo - (long)dt : 0);
            if (on != want_on || onn != want_onn) bad_on++;
        }
        CHECK(bad_overlap == 0);
        CHECK(bad_on == 0 && bad_pol == 0);
    }
    /* ---- Part B: 데드타임보다 짧은 펄스는 사라진다 (20 kHz center, ARR=4200, DT=84 = 500 ns) ---- */
    {
        static const uint32_t CC[] = { 42u, 43u, 4157u, 4158u };
        long on[4], onn[4];
        for (int i = 0; i < 4; i++) {
            raw_t q = { CM_CENTER1, 0, 4200, OCM_PWM1, CC[i], true, CCER_E(0) | CCER_NE(0), 84u | TIM_BDTR_MOE, 0 };
            P = one_period(&q, 0u);
            on[i] = count1(w_act, P); onn[i] = count1(w_actn, P);
        }
        CHECK(on[0] == 0 && on[1] == 2 && onn[0] == 8400 - 2 * 84);   /* REF_hi 84 = DT → CH 소멸 */
        CHECK(onn[2] == 2 && onn[3] == 0 && on[3] == 8400 - 2 * 84);
    }
    /* ---- Part B: MOE — 없으면 REF 가 돌아도 핀은 0, 켜면 나오고, 끄면 다음 샘플에 0 ---- */
    {
        raw_t q = { CM_CENTER1, 0, 50, OCM_PWM1, 20, true, CCER_E(0) | CCER_NE(0), 5u, 0 };   /* MOE 없음 */
        P = one_period(&q, 0u);
        CHECK(P == 100 && count1(w_ref, P) == 40 && count1(w_act, P) == 0 && count1(w_actn, P) == 0);
        REG_WRITE(TIM_BDTR(TIM1_BASE), 5u | TIM_BDTR_MOE);
        run_to_start(500); capture(100, 0u, w_ref, w_act, w_actn, NULL);
        CHECK(count1(w_act, 100) == 35 && count1(w_actn, 100) == 55);
        for (int i = 0; i < 37; i++) sim_step();
        bool was = m.act[0] || m.actn[0];
        REG_WRITE(TIM_BDTR(TIM1_BASE), REG_READ(TIM_BDTR(TIM1_BASE)) & ~TIM_BDTR_MOE);
        sim_step();
        CHECK(was && m.act[0] == 0 && m.actn[0] == 0 && m.oc[0] == 0 && m.ocn[0] == 0);
    }

    /* ---- Part C: 드라이버 초기화 — 결과 값과 레지스터 접근 순서 ---- */
    {
        pwm_cfg_t cfg; memset(&cfg, 0, sizeof cfg);
        tim_model_init();
        CHECK(pwm3_init(TIM1_BASE, 168000000u, 20000u, 500u, &cfg) == PWM_OK);
        CHECK(cfg.psc == 0u && cfg.arr == 4200u && cfg.dtg == 84u && cfg.dead_ps == 500000u && cfg.f_act_mHz == 20000000u);
        const uint32_t b = TIM1_BASE;
        int moe0 = log_find('W', TIM_BDTR(b), TIM_BDTR_MOE, 0u, 0);
        int psc = log_find('W', TIM_PSC(b), 0u, 0u, 0), arr = log_find('W', TIM_ARR(b), 0u, 0u, 0);
        int cms = log_find('W', TIM_CR1(b), TIM_CR1_CMS_Msk | TIM_CR1_CEN, 1u << TIM_CR1_CMS_Pos, 0);
        int dtg = log_find('W', TIM_BDTR(b), 0xFFu | TIM_BDTR_MOE, 84u, 0);
        int ug = log_find('W', TIM_EGR(b), TIM_EGR_UG, TIM_EGR_UG, 0);
        int sr = log_find('W', TIM_SR(b), TIM_SR_UIF, 0u, 0);
        int ie = log_find('W', TIM_DIER(b), TIM_DIER_UIE, TIM_DIER_UIE, 0);
        int moe = log_find('W', TIM_BDTR(b), TIM_BDTR_MOE, TIM_BDTR_MOE, 0);
        int en = log_find('W', TIM_CR1(b), TIM_CR1_CEN, TIM_CR1_CEN, 0);
        CHECK(moe0 >= 0 && moe0 < psc && psc < cms && arr < cms && cms < dtg && dtg < ug && ug < sr && sr < ie &&
              ie < moe && moe < en && g_unexpected == 0);
        CHECK(*m.ccmr1 == 0x6868u && *m.ccmr2 == 0x68u && *m.ccer == 0x555u && m.ccr_sh[2] == 2100u &&
              (*m.sr & TIM_SR_UIF) == 0u && (*m.cr1 & 0x3FFu) == 0xA1u);
        CHECK(pwm3_init(TIM1_BASE, 168000000u, 20000u, 7000u, &cfg) == PWM_E_DEADTIME);    /* 1176 tDTS > 1008 */
        CHECK(pwm3_init(TIM1_BASE, 168000000u, 60000000u, 500u, &cfg) == PWM_E_RANGE &&
              pwm3_init(TIM1_BASE, 168000000u, 0u, 500u, &cfg) == PWM_E_ARG);
        CHECK(pwm3_init(TIM1_BASE, 168000000u, 1000u, 500u, &cfg) == PWM_OK && cfg.psc == 1u && cfg.arr == 42000u);
        tim_model_init();
        REG_WRITE(TIM_ARR(TIM1_BASE), 4200u);
        pwm_set_duty(TIM1_BASE, 0u, 5000u); pwm_set_duty(TIM1_BASE, 1u, 12000u); pwm_set_duty(TIM1_BASE, 2u, 1u);
        pwm_set_duty(TIM1_BASE, 3u, 5000u);
        CHECK(*m.ccr[0] == 2100u && *m.ccr[1] == 4200u && *m.ccr[2] == 0u && g_unexpected == 0);
    }
    /* ---- Part C: 3상 20 kHz 제어 루프 100 주기, ISR 계산 1000 클럭 (6 µs) ---- */
    {
        pwm_cfg_t cfg;
        int rc = c_run(1000u, 100u, 0u, &cfg);
        CHECK(rc == PWM_OK && T.valleys == 100u && g_ctrl_calls == 100u && g_isr_calls == 200u);
        CHECK(T.pulses == 98u && T.asym == 0u && T.wrong == 0u);    /* 모든 펄스 대칭 + 한 주기 뒤 정확히 반영 */
        CHECK(T.overlap == 0u && T.min_gap == 84u);                  /* 관통 없음, 모든 전환에 데드타임 84 클럭 */
    }
    /* ---- Part C: 시간 예산 — 꼭대기 UEV 전에 써야 한다 (cost ≤ ARR−1) ---- */
    {
        pwm_cfg_t cfg;
        c_run(4199u, 40u, 0u, &cfg);
        CHECK(T.asym == 0u && T.wrong == 0u);
        c_run(4200u, 40u, 0u, &cfg);
        CHECK(T.asym > 0u && T.asym == T.exp_asym && T.overlap == 0u);   /* 늦으면 골짜기에서 반영 → 비대칭 */
    }
    /* ---- Part C: 과전류 → ISR 에서 MOE 차단 → 6 출력 즉시 0, 카운터·ISR 은 계속 ---- */
    {
        pwm_cfg_t cfg;
        c_run(1000u, 40u, 20u, &cfg);
        CHECK(T.act_after_fault == 0u && g_ctrl_calls == 40u && (*m.bdtr & TIM_BDTR_MOE) == 0u && T.overlap == 0u);
        CHECK(T.on_clk[0] > 0u && (*m.bdtr & 0xFFu) == 84u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
