/* BM-M07 ADC 드라이버: 스캔 변환, 샘플링 시간, 보정
 *
 * Part A — 레지스터 메커닉: SQR1/2/3 (랭크 → 채널, L = n−1) · SMPR1/2 (채널 → 샘플링 코드) 패킹,
 *          T_conv = T_sample + 12 ADCCLK, 폴링 스캔 (EOCS=1 → 변환마다 EOC), raw → mV (VREFINT 보정).
 * Part B — 면접관이 찌르는 고장: 잘못된 설정(검증 먼저, 쓰기는 나중), EOCS=0 폴링 스캔, 느린 루프 → OVR,
 *          ADON 직후 SWSTART 무시, OVR 안 지우고 재시작, 고임피던스 + 짧은 샘플링 → 낮게 읽힘,
 *          32비트 오버플로 / 이중 반올림.
 * Part C — 펌웨어 현실: EOC/OVR 인터럽트 스캔(NVIC-lite + sim_step), ISR 지연 → OVR → ISR 이 복구,
 *          VDDA=3.0 V 보드 모니터 로그, 4^n 오버샘플링(+n 비트 — 노이즈가 있을 때만).
 *
 * 주소·비트: STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. C_ADC/R_ADC/tSTAB 등 아날로그 값은
 * "대표값, 데이터시트 확인 권장". 테스트의 정답(ground truth)은 이 파일의 mock 모델 정의다.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define UNIT_TEST 1                       /* 호스트 테스트 빌드 (타깃에서는 REG_* 가 volatile MMIO) */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ======================= 이음매(seam): 드라이버는 이 매크로로만 레지스터를 만진다 ======================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
uint16_t mock_read16(uint32_t addr);
void     cpu_delay_cycles(uint32_t n);    /* 타깃: 사이클 루프 / DWT, 호스트: mock 시간 진행 */
void     nvic_enable_irq(int irqn);       /* 타깃: CMSIS NVIC_EnableIRQ, 호스트: NVIC-lite */
#define REG_READ(a)       mock_read32(a)
#define REG_WRITE(a, v)   mock_write32((a), (v))
#define REG_READ16(a)     mock_read16(a)
#else
#define REG_READ(a)       (*(volatile uint32_t *)(uintptr_t)(a))
#define REG_WRITE(a, v)   (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define REG_READ16(a)     (*(volatile const uint16_t *)(uintptr_t)(a))
#endif

/* ======================= 레지스터 정의 (RM0090 기준 대표값 — RM 확인 권장) ======================= */
#define ADC1_BASE        0x40012000u
#define ADC_SR           (ADC1_BASE + 0x00u)
#define ADC_CR1          (ADC1_BASE + 0x04u)
#define ADC_CR2          (ADC1_BASE + 0x08u)
#define ADC_SMPR1        (ADC1_BASE + 0x0Cu)   /* ch10..18, 3비트씩 */
#define ADC_SMPR2        (ADC1_BASE + 0x10u)   /* ch0..9,   3비트씩 */
#define ADC_SQR1         (ADC1_BASE + 0x2Cu)   /* SQ13..16 + L[23:20] */
#define ADC_SQR2         (ADC1_BASE + 0x30u)   /* SQ7..12 */
#define ADC_SQR3         (ADC1_BASE + 0x34u)   /* SQ1..6 */
#define ADC_DR           (ADC1_BASE + 0x4Cu)
#define ADC_SR_EOC       (1u << 1)
#define ADC_SR_STRT      (1u << 4)
#define ADC_SR_OVR       (1u << 5)
#define ADC_CR1_EOCIE    (1u << 5)
#define ADC_CR1_SCAN     (1u << 8)
#define ADC_CR1_OVRIE    (1u << 26)
#define ADC_CR2_ADON     (1u << 0)
#define ADC_CR2_DMA      (1u << 8)
#define ADC_CR2_EOCS     (1u << 10)
#define ADC_CR2_SWSTART  (1u << 30)
#define VREFINT_CAL_ADDR 0x1FFF7A2Au           /* F4 공장 보정값 (VDDA=3.3 V, 30 °C) — 확인 권장 */
#define ADC_CH_VREFINT   17u
#define ADC_IRQn         18                    /* F4 ADC1/2/3 global — 확인 권장 */

enum { ADC_OK = 0, ADC_EINVAL = -1, ADC_ETIMEOUT = -2, ADC_EOVR = -3 };
#define ADC_POLL_LIMIT     2000u  /* EOC 폴링 상한 (가장 긴 변환 492 cycle 보다 넉넉히) */
#define ADC_TSTAB_CYCLES   63u    /* ADON 후 안정화: 3 µs × 21 MHz (대표값, 데이터시트 tSTAB 확인 권장) */
#define ADC_C_F            4e-12  /* C_ADC 4 pF  (대표값, 데이터시트 확인 권장) */
#define ADC_R_OHM          6000.0 /* R_ADC 6 kΩ  (대표값, 데이터시트 확인 권장) */

static const uint16_t adc_smp_cycles[8] = { 3, 15, 28, 56, 84, 112, 144, 480 };

/* 12비트 변환 1회 = 샘플링 + 12 ADCCLK */
uint32_t adc_conv_cycles(unsigned smp) { return smp < 8u ? adc_smp_cycles[smp] + 12u : 0u; }

/* ======================= Part A: 스캔 시퀀스 설정 ======================= */
/* chs[i] = 랭크 i+1 의 채널(0..18), smp[i] = 그 채널의 샘플링 코드(0..7). 전부 검증한 뒤에야 쓴다. */
int adc_config_scan(const uint8_t *chs, unsigned n, const uint8_t *smp) {
    (void)chs; (void)n; (void)smp;
    /* TODO(A): 1) 전부 검증 (n 1..16, ch 0..18, smp 0..7, 같은 채널에 다른 smp → ADC_EINVAL) — 아직 아무것도 쓰지 않는다
     *          2) SQR1 = (n−1)<<20 | SQ13.., SQR2 = SQ7.., SQR3 = SQ1.. (랭크 r → 5비트 필드, 안 쓰는 랭크는 0)
     *          3) SMPR1/2 는 RMW: 쓰는 채널의 3비트 필드만 바꾼다 (ch<10 → SMPR2 3·ch, else SMPR1 3·(ch−10))
     *          4) 다섯 레지스터를 쓰고 ADC_OK */




















    return ADC_EINVAL;
}

/* ADON 이 꺼져 있으면 켜고 tSTAB 를 기다린다 (F4 에는 ADRDY 플래그가 없다). 이미 켜져 있으면 바로 리턴. */
void adc_enable(void) {
    uint32_t cr2 = REG_READ(ADC_CR2);
    if (cr2 & ADC_CR2_ADON) return;
    REG_WRITE(ADC_CR2, cr2 | ADC_CR2_ADON);
    cpu_delay_cycles(ADC_TSTAB_CYCLES);
}

/* 폴링 스캔: 랭크마다 EOC 를 기다려 다음 결과가 나오기 전에 DR 을 읽는다.
 * cb 는 샘플마다 불리는 "다른 일"(로그 등) — 느리면 OVR. 반환: n, 또는 음수 오류. */
typedef void (*adc_sample_cb)(unsigned rank, uint16_t raw, void *ctx);
int adc_read_scan_polled(uint16_t *out, unsigned n, adc_sample_cb cb, void *ctx) {
    (void)out; (void)n; (void)cb; (void)ctx;
    /* TODO(A): n 검사(1..16, == L+1) → adc_enable() → CR1.SCAN (인터럽트 끔) → CR2 = ADON|EOCS
     *          → SR 의 EOC/OVR/STRT 클리어(rc_w0) → SWSTART
     *          → 랭크마다: SR 을 최대 ADC_POLL_LIMIT 번 폴링해 EOC|OVR 대기 (넘으면 ADC_ETIMEOUT),
     *            OVR 이면 ADC_EOVR, 아니면 out[r] = DR (읽기가 EOC 클리어), cb 있으면 cb(r, out[r], ctx) */












    return ADC_ETIMEOUT;
}

/* ======================= Part A: raw → mV (정수, round-half-up) ======================= */
/* VDDA[mV] = 3300 · CAL · 2^extra / DATA   (DATA 가 12+extra 비트 오버샘플 값일 때) */
uint32_t adc_vdda_mv(uint16_t cal, uint32_t vref_data, unsigned extra_bits) {
    (void)cal; (void)vref_data; (void)extra_bits;
    /* TODO(A): 64비트로 3300·CAL·2^extra / DATA, round-half-up. DATA==0 또는 extra>4 → 0 */

    return 3300u;
}

/* mV = vdda · raw / 2^bits */
uint32_t adc_raw_to_mv(uint32_t raw, unsigned bits, uint32_t vdda_mv) {
    (void)raw; (void)bits; (void)vdda_mv;
    /* TODO(A): (vdda·raw + 2^(bits−1)) >> bits, 64비트 */
    return 0u;
}

/* 한 번에: mV = 3300 · CAL · raw / (DATA · 4096) — 중간 반올림 없음, 64비트 */
uint32_t adc_raw_to_mv_cal(uint32_t raw, uint16_t cal, uint32_t vref_data) {
    (void)cal; (void)vref_data;
    /* TODO(A): num = 3300·CAL·raw, den = DATA·4096 (둘 다 64비트), (num + den/2) / den. DATA==0 → 0 */

    return raw * 3300u / 4095u;                  /* 3.3 V 를 가정한 흔한 오답 */
}

/* ======================= Part B: 소스 임피던스 → 샘플링 코드 ======================= */
/* 데이터시트식 R_AIN_max = (k − 0.5) / (f_ADC · C_ADC · ln(2^(N+2))) − R_ADC  (N=12, ¼ LSB 오차)
 * 을 만족하는 가장 짧은 코드. 없으면 ADC_EINVAL. 설계 시점 계산이라 double 사용. */
int adc_pick_smp(uint32_t r_src_ohm, uint32_t f_adc_hz) {
    (void)r_src_ohm; (void)f_adc_hz;
    /* TODO(B): k = 0..7 중 R_AIN_max(k) ≥ r_src 인 첫 k. ln(2^14) = 14·ln2, ADC_C_F, ADC_R_OHM 사용 */




    return ADC_EINVAL;
}

/* ======================= Part C: 오버샘플링 (4^n 합 → >> n, 반올림) ======================= */
int adc_oversample(uint8_t ch, uint8_t smp, unsigned n, uint32_t *out) {
    (void)ch; (void)smp; (void)n;
    /* TODO(C): 1채널 설정 → 4^n 번 adc_read_scan_polled(…, 1, …) 합 → n>0 이면 (합 + 2^(n−1)) >> n.
     *          오류는 그대로 돌려준다. n>4 → ADC_EINVAL */









    if (out) *out = 0;
    return ADC_EINVAL;
}

/* ======================= Part C: 인터럽트 스캔 (EOCIE + OVRIE) ======================= */
typedef struct {
    uint16_t          buf[16];
    unsigned          n;
    volatile unsigned idx;
    volatile bool     done;
    volatile uint32_t ovr_count;
} adc_it_t;
static adc_it_t g_adc;

int adc_start_scan_it(unsigned n) {
    if (n == 0u || n > 16u || ((REG_READ(ADC_SQR1) >> 20) & 0xFu) + 1u != n) return ADC_EINVAL;
    adc_enable();
    g_adc.n = n; g_adc.idx = 0; g_adc.done = false;
    REG_WRITE(ADC_CR1, REG_READ(ADC_CR1) | ADC_CR1_SCAN | ADC_CR1_EOCIE | ADC_CR1_OVRIE);
    REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_EOCS);
    REG_WRITE(ADC_SR, ~(ADC_SR_EOC | ADC_SR_OVR | ADC_SR_STRT));
    nvic_enable_irq(ADC_IRQn);
    REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_EOCS | ADC_CR2_SWSTART);
    return ADC_OK;
}

void ADC_IRQHandler(void) {
    /* TODO(C): SR 읽기 → OVR 이면: ovr_count++, idx=0, SR 의 OVR/EOC/STRT 를 rc_w0 로 클리어, SWSTART 로 재시작.
     *          아니고 EOC 면: DR 읽어 buf[idx++] (idx<n 일 때), idx==n 이면 done = true */












}

#ifdef UNIT_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ======================= mock 코어 (BM-B02 mock 축약판: 표 + 훅 + tick, 로그 대신 카운터) ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef void     (*mock_on_tick)(void *ctx);
typedef struct { uint32_t addr, val; mock_on_read rd; mock_on_write wr; void *ctx; } mock_reg_t;
static struct {
    mock_reg_t   reg[16];
    unsigned     nreg, unexpected;
    mock_on_tick tick_fn[2];
    unsigned     ntick;
    uint64_t     now;                     /* 1 tick = 1 ADCCLK. 버스 접근 1회 = 1 tick */
    uint64_t     reads, writes;
} M;

static void mock_reset(void) { memset(&M, 0, sizeof M); }
static mock_reg_t *mock_find(uint32_t a) {
    for (unsigned i = 0; i < M.nreg; i++) if (M.reg[i].addr == a) return &M.reg[i];
    return NULL;
}
static void mock_add_reg(uint32_t a, uint32_t rst, mock_on_read r, mock_on_write w, void *ctx) {
    M.reg[M.nreg++] = (mock_reg_t){ a, rst, r, w, ctx };
}
static void mock_tick(unsigned n) {
    while (n--) { M.now++; for (unsigned i = 0; i < M.ntick; i++) M.tick_fn[i](NULL); }
}
static uint32_t mock_peek(uint32_t a) { mock_reg_t *g = mock_find(a); return g ? g->val : 0u; }
static void mock_poke(uint32_t a, uint32_t v) { mock_reg_t *g = mock_find(a); if (g) g->val = v; }
uint32_t mock_read32(uint32_t a) {
    mock_reg_t *g = mock_find(a);
    uint32_t v = 0;
    M.reads++;
    if (!g) M.unexpected++;
    else v = g->rd ? g->rd(a, g->val, g->ctx) : g->val;
    mock_tick(1);
    return v;
}
void mock_write32(uint32_t a, uint32_t v) {
    mock_reg_t *g = mock_find(a);
    M.writes++;
    if (!g) M.unexpected++;
    else g->val = g->wr ? g->wr(a, g->val, v, g->ctx) : v;
    mock_tick(1);
}
uint16_t mock_read16(uint32_t a) {                 /* 하프워드 읽기: 같은 워드에서 잘라낸다 */
    if (a & 1u) { M.unexpected++; return 0; }
    return (uint16_t)(mock_read32(a & ~3u) >> ((a & 2u) * 8u));
}
void cpu_delay_cycles(uint32_t n) { mock_tick(n); }
static uint32_t mask_wr(uint32_t a, uint32_t o, uint32_t v, void *ctx) { (void)a; (void)o; return v & (uint32_t)(uintptr_t)ctx; }
static uint32_t ro_wr(uint32_t a, uint32_t o, uint32_t v, void *ctx) { (void)a; (void)v; (void)ctx; return o; }

/* ======================= NVIC-lite (레벨 라인 + sim_step 디스패치) ======================= */
typedef void (*isr_fn)(void);
static struct { bool en[32], line[32]; isr_fn h[32]; unsigned calls[32]; unsigned mask_ticks; } NV;
static void irq_line_set(int irqn, bool level) { NV.line[irqn] = level; }
void nvic_enable_irq(int irqn) { NV.en[irqn] = true; }
static void sim_step(void) {                       /* 1 tick 진행 → 마스크 안 됐으면 펜딩 IRQ 하나 처리 */
    mock_tick(1);
    if (NV.mask_ticks) { NV.mask_ticks--; return; }
    for (int i = 0; i < 32; i++)
        if (NV.en[i] && NV.line[i] && NV.h[i]) { NV.calls[i]++; NV.h[i](); return; }
}

/* ======================= ADC 모델 ======================= */
#define NCH 19
static struct {
    double   vin[NCH], rsrc[NCH];           /* 채널 입력 전압 [V], 소스 임피던스 [Ω] */
    double   vdda, fadc, c_adc, r_adc;      /* 전원, ADCCLK [Hz], 샘플 커패시터 / 스위치 저항 */
    double   vrefint;                       /* 이 칩의 실제 VREFINT [V] */
    double   noise_lsb;                     /* 가우스 노이즈 σ [LSB] */
    uint32_t rng;
    bool     busy;
    int      phase;                         /* 0 = 샘플링, 1 = 변환(SAR 12 cycle) */
    unsigned left, rank, len;
    uint8_t  seq[16], smp[NCH];             /* SWSTART 순간에 래치 (모델 단순화) */
    double   vcap;                          /* 샘플 커패시터 전압: 이전 변환의 잔류 전하 */
    uint64_t adon_at;
    unsigned conversions, ignored_triggers, ovr_events;
} AM;

/* 타임라인 기록 (-v): S=샘플링 시작, H=홀드, E=결과+EOC, O=OVR, D=DR 읽기 */
static struct { uint64_t t; char e; unsigned r; } TR[64];
static unsigned ntr, ndr;
static void tr(char e, unsigned r) { if (ntr < 64) { TR[ntr].t = M.now; TR[ntr].e = e; TR[ntr].r = r; ntr++; } }

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double gauss(void) {                        /* Irwin–Hall: 균등분포 12개 합 − 6 ≈ N(0,1) */
    double s = 0; for (int i = 0; i < 12; i++) s += xs32(&AM.rng) / 4294967296.0;
    return s - 6.0;
}
static uint16_t ideal_code(double v, double vdda) {   /* 이상적 12비트: round(V·4096/VDDA), 0..4095 */
    double x = floor(v * 4096.0 / vdda + 0.5);
    return (uint16_t)(x < 0 ? 0 : x > 4095 ? 4095 : x);
}
static void adc_irq_update(void) {
    uint32_t sr = mock_peek(ADC_SR), cr1 = mock_peek(ADC_CR1);
    irq_line_set(ADC_IRQn, ((sr & ADC_SR_EOC) && (cr1 & ADC_CR1_EOCIE)) || ((sr & ADC_SR_OVR) && (cr1 & ADC_CR1_OVRIE)));
}
static void model_start_rank(void) { AM.phase = 0; AM.left = adc_smp_cycles[AM.smp[AM.seq[AM.rank]]]; tr('S', AM.rank); }
static void model_start_seq(void) {
    uint32_t q[3] = { mock_peek(ADC_SQR3), mock_peek(ADC_SQR2), mock_peek(ADC_SQR1) };
    AM.len = (mock_peek(ADC_CR1) & ADC_CR1_SCAN) ? ((q[2] >> 20) & 0xFu) + 1u : 1u;   /* SCAN=0 → SQ1 만 */
    for (unsigned r = 0; r < 16; r++) {
        unsigned c = (q[r / 6u] >> (5u * (r % 6u))) & 0x1Fu;
        AM.seq[r] = (uint8_t)(c < NCH ? c : 0);
    }
    for (unsigned c = 0; c < NCH; c++)
        AM.smp[c] = (uint8_t)((c < 10 ? mock_peek(ADC_SMPR2) >> (3u * c) : mock_peek(ADC_SMPR1) >> (3u * (c - 10))) & 7u);
    AM.busy = true; AM.rank = 0;
    mock_poke(ADC_SR, mock_peek(ADC_SR) | ADC_SR_STRT);
    model_start_rank();
}
static void adc_tick(void *ctx) {
    (void)ctx;
    if (AM.busy && --AM.left == 0) {
        if (AM.phase == 0) {                       /* 샘플링 끝 → 홀드: RC 충전 결과 */
            unsigned ch = AM.seq[AM.rank];
            double tau = (AM.rsrc[ch] + AM.r_adc) * AM.c_adc;
            double ts = adc_smp_cycles[AM.smp[ch]] / AM.fadc;
            AM.vcap = AM.vin[ch] + (AM.vcap - AM.vin[ch]) * exp(-ts / tau);
            AM.phase = 1; AM.left = 12; tr('H', AM.rank);
        } else {                                   /* 변환 끝 → 결과 */
            double v = AM.vcap + (AM.noise_lsb > 0 ? AM.noise_lsb * gauss() * AM.vdda / 4096.0 : 0.0);
            uint16_t code = ideal_code(v, AM.vdda);
            uint32_t sr = mock_peek(ADC_SR), cr2 = mock_peek(ADC_CR2);
            bool last = AM.rank + 1u == AM.len, ovr_on = (cr2 & (ADC_CR2_EOCS | ADC_CR2_DMA)) != 0;
            AM.conversions++;
            if (ovr_on && (sr & ADC_SR_EOC)) {     /* 이전 결과를 아직 안 읽음 → OVR, 새 결과 버림, 시퀀스 중단 */
                mock_poke(ADC_SR, sr | ADC_SR_OVR);
                AM.ovr_events++; AM.busy = false; tr('O', AM.rank);
            } else {
                tr('E', AM.rank);
                mock_poke(ADC_DR, code);
                if ((cr2 & ADC_CR2_EOCS) || last) sr |= ADC_SR_EOC;   /* EOCS=0 → 시퀀스 끝에만 EOC */
                mock_poke(ADC_SR, sr);
                if (last) AM.busy = false; else { AM.rank++; model_start_rank(); }
            }
        }
    }
    adc_irq_update();
}
static uint32_t sr_wr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)c; return o & (v | ~0x3Fu) & 0x3Fu; } /* rc_w0 */
static uint32_t dr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)c;
    mock_poke(ADC_SR, mock_peek(ADC_SR) & ~ADC_SR_EOC);   /* DR 읽기가 EOC 를 지운다 */
    tr('D', ndr++);
    return s & 0xFFFFu;
}
static uint32_t cr2_wr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)c;
    uint32_t nv = v & 0x7F000F03u & ~ADC_CR2_SWSTART;        /* SWSTART 는 저장 안 됨 (HW 가 바로 지움) */
    if ((nv & ADC_CR2_ADON) && !(o & ADC_CR2_ADON)) AM.adon_at = M.now;
    if (!(nv & ADC_CR2_ADON)) AM.busy = false;
    if (v & ADC_CR2_SWSTART) {
        bool ok = (o & ADC_CR2_ADON) && (nv & ADC_CR2_ADON) && M.now - AM.adon_at >= ADC_TSTAB_CYCLES
                  && !AM.busy && !(mock_peek(ADC_SR) & ADC_SR_OVR);
        if (ok) model_start_seq(); else AM.ignored_triggers++;
    }
    return nv;
}

static uint16_t g_cal;                              /* 모델이 "공장에서" 구워 둔 VREFINT_CAL */
static void sim_init(double vdda, double noise_lsb) {
    mock_reset(); ntr = ndr = 0; memset(&NV, 0, sizeof NV); memset(&AM, 0, sizeof AM); memset(&g_adc, 0, sizeof g_adc);
    AM.vdda = vdda; AM.fadc = 21e6; AM.c_adc = 4e-12; AM.r_adc = 6000.0;
    AM.vrefint = 1.21; AM.noise_lsb = noise_lsb; AM.rng = 0x1234567u;
    for (int c = 0; c < NCH; c++) { AM.vin[c] = 0.0; AM.rsrc[c] = 100.0; }
    AM.vin[ADC_CH_VREFINT] = AM.vrefint; AM.rsrc[ADC_CH_VREFINT] = 200e3;  /* 모델값: tS_vrefint ≈ 10 µs 요구와 맞춤 */
    g_cal = ideal_code(AM.vrefint, 3.3);
    mock_add_reg(ADC_SR, 0, NULL, sr_wr, NULL);
    mock_add_reg(ADC_CR1, 0, NULL, mask_wr, (void *)(uintptr_t)0x07C0FFFFu);
    mock_add_reg(ADC_CR2, 0, NULL, cr2_wr, NULL);
    mock_add_reg(ADC_SMPR1, 0, NULL, mask_wr, (void *)(uintptr_t)0x07FFFFFFu);
    mock_add_reg(ADC_SMPR2, 0, NULL, mask_wr, (void *)(uintptr_t)0x3FFFFFFFu);
    mock_add_reg(ADC_SQR1, 0, NULL, mask_wr, (void *)(uintptr_t)0x00FFFFFFu);
    mock_add_reg(ADC_SQR2, 0, NULL, mask_wr, (void *)(uintptr_t)0x3FFFFFFFu);
    mock_add_reg(ADC_SQR3, 0, NULL, mask_wr, (void *)(uintptr_t)0x3FFFFFFFu);
    mock_add_reg(ADC_DR, 0, dr_rd, ro_wr, NULL);
    mock_add_reg(0x1FFF7A28u, ((uint32_t)g_cal << 16) | 0xFFFFu, NULL, ro_wr, NULL);  /* OTP: 상위 하프워드 = CAL */
    M.tick_fn[M.ntick++] = adc_tick;
    NV.h[ADC_IRQn] = ADC_IRQHandler;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s   (line %d)\n", #expr, __LINE__); } while (0)

/* 독립 디코더: 레지스터 → (n, 채널, 채널별 smp) */
static unsigned dec_len(void) { return ((mock_peek(ADC_SQR1) >> 20) & 0xFu) + 1u; }
static unsigned dec_rank(unsigned r) {
    uint32_t reg = r < 6 ? ADC_SQR3 : r < 12 ? ADC_SQR2 : ADC_SQR1;
    return (mock_peek(reg) >> (5u * (r % 6u))) & 0x1Fu;
}
static unsigned dec_smp(unsigned ch) {
    return ch < 10 ? (mock_peek(ADC_SMPR2) >> (3u * ch)) & 7u : (mock_peek(ADC_SMPR1) >> (3u * (ch - 10))) & 7u;
}
static uint32_t round_div(uint64_t num, uint64_t den) {     /* 독립 기준: q + (2r ≥ den) */
    uint64_t q = num / den, r = num % den;
    return (uint32_t)(q + (2u * r >= den));
}

/* 콜백: 샘플마다 work 사이클만큼 "다른 일" (예: UART 로그) */
static void slow_cb(unsigned rank, uint16_t raw, void *ctx) { (void)rank; (void)raw; mock_tick(*(unsigned *)ctx); }

/* OVR 없이 버티는 최대 work 를 찾는다 (스캔 3랭크, 같은 smp — OVR 은 3번째 결과에서야 가능) */
static int max_work_without_ovr(uint8_t smp, unsigned n) {
    for (unsigned w = 0; w < 600; w++) {
        sim_init(3.3, 0);
        uint8_t ch[16], sm[16];
        uint16_t out[16];
        for (unsigned i = 0; i < n; i++) { ch[i] = (uint8_t)i; sm[i] = smp; }
        adc_config_scan(ch, n, sm);
        if (adc_read_scan_polled(out, n, slow_cb, &w) == ADC_EOVR) return (int)w - 1;
    }
    return -1;
}

static void print_trace(unsigned w) {
    sim_init(3.3, 0);
    uint8_t ch[3] = { 3, 4, 5 }, sm[3] = { 0, 0, 0 };
    uint16_t out[3];
    adc_config_scan(ch, 3, sm);
    int rc = adc_read_scan_polled(out, 3, slow_cb, &w);
    uint64_t t0 = TR[0].t;                       /* 첫 샘플링 시작 = 0 */
    printf("-- 3-rank scan, smp=3cyc (P=15), work=%u -> rc=%d\n  ", w, rc);
    for (unsigned i = 0; i < ntr; i++) printf("%c%u@%llu ", TR[i].e, TR[i].r, (unsigned long long)(TR[i].t - t0));
    printf("\n");
}

int main(int argc, char **argv) {
    uint32_t seed = 0xADC0FFEEu;
    if (argc > 1 && strcmp(argv[1], "-v") == 0) { print_trace(0); print_trace(26); print_trace(27); return 0; }
    printf("== Part A — 레지스터 메커닉 ==\n");
    {
        static const uint32_t want[8] = { 15, 27, 40, 68, 96, 124, 156, 492 };
        bool ok = true;
        for (unsigned s = 0; s < 8; s++) ok &= adc_conv_cycles(s) == want[s];
        CHECK(ok && adc_conv_cycles(8) == 0);
    }
    {   /* 문제의 예: 17,1,4,8 / smp 7,3,1,1 */
        sim_init(3.3, 0);
        uint8_t ch[4] = { 17, 1, 4, 8 }, sm[4] = { 7, 3, 1, 1 };
        CHECK(adc_config_scan(ch, 4, sm) == ADC_OK);
        printf("      SQR1=0x%08X SQR2=0x%08X SQR3=0x%08X SMPR1=0x%08X SMPR2=0x%08X\n",
               mock_peek(ADC_SQR1), mock_peek(ADC_SQR2), mock_peek(ADC_SQR3), mock_peek(ADC_SMPR1), mock_peek(ADC_SMPR2));
        CHECK(mock_peek(ADC_SQR1) == 0x00300000u && mock_peek(ADC_SQR2) == 0 && mock_peek(ADC_SQR3) == 0x00041031u);
        CHECK(mock_peek(ADC_SMPR1) == 0x00E00000u && mock_peek(ADC_SMPR2) == 0x01001018u);
    }
    {   /* 전수: 채널 0..18 × smp 0..7 × 무작위 prefill 3개 → 그 3비트 필드만 바뀐다 */
        unsigned bad = 0, cases = 0;
        for (uint8_t c = 0; c < 19; c++)
            for (uint8_t s = 0; s < 8; s++)
                for (int k = 0; k < 3; k++, cases++) {
                    sim_init(3.3, 0);
                    uint32_t p1 = xs32(&seed) & 0x07FFFFFFu, p2 = xs32(&seed) & 0x3FFFFFFFu;
                    mock_poke(ADC_SMPR1, p1); mock_poke(ADC_SMPR2, p2);
                    adc_config_scan(&c, 1, &s);
                    uint32_t e1 = p1, e2 = p2;
                    if (c < 10) e2 = (p2 & ~(7u << (3u * c))) | ((uint32_t)s << (3u * c));
                    else        e1 = (p1 & ~(7u << (3u * (c - 10)))) | ((uint32_t)s << (3u * (c - 10)));
                    bad += mock_peek(ADC_SMPR1) != e1 || mock_peek(ADC_SMPR2) != e2 ||
                           mock_peek(ADC_SQR3) != c || mock_peek(ADC_SQR1) != 0 || mock_peek(ADC_SQR2) != 0;
                }
        printf("      single-channel exhaustive: %u cases, %u bad\n", cases, bad);
        CHECK(bad == 0);
    }
    {   /* 무작위 시퀀스 3000개 → 독립 디코더로 되읽기 */
        unsigned bad = 0;
        for (int t = 0; t < 3000; t++) {
            sim_init(3.3, 0);
            uint32_t p1 = xs32(&seed) & 0x07FFFFFFu, p2 = xs32(&seed) & 0x3FFFFFFFu;
            mock_poke(ADC_SMPR1, p1); mock_poke(ADC_SMPR2, p2);
            unsigned n = 1 + xs32(&seed) % 16;
            uint8_t ch[16], sm[16], chsmp[19];
            for (int c = 0; c < 19; c++) chsmp[c] = (uint8_t)(xs32(&seed) % 8);
            for (unsigned i = 0; i < n; i++) { ch[i] = (uint8_t)(xs32(&seed) % 19); sm[i] = chsmp[ch[i]]; }
            if (adc_config_scan(ch, n, sm) != ADC_OK) { bad++; continue; }
            bad += dec_len() != n;
            for (unsigned i = 0; i < 16; i++) bad += dec_rank(i) != (i < n ? ch[i] : 0u);
            for (unsigned c = 0; c < 19; c++) {
                bool used = false;
                for (unsigned i = 0; i < n; i++) used |= ch[i] == c;
                unsigned before = c < 10 ? (p2 >> (3 * c)) & 7u : (p1 >> (3 * (c - 10))) & 7u;
                bad += dec_smp(c) != (used ? chsmp[c] : before);
            }
        }
        CHECK(bad == 0);
    }
    {   /* 한 채널 폴링 변환 = 이상적 코드 */
        sim_init(3.3, 0);
        AM.vin[5] = 1.0;
        uint8_t c = 5, s = 7; uint16_t v = 0;
        adc_config_scan(&c, 1, &s);
        CHECK(adc_read_scan_polled(&v, 1, NULL, NULL) == 1 && v == ideal_code(1.0, 3.3));
    }
    {   /* 4채널 스캔: 결과가 랭크 순서대로 */
        sim_init(3.3, 0);
        double vv[4] = { 0.25, 3.1, 1.65, 0.8 };
        uint8_t ch[4] = { 2, 9, 11, 0 }, sm[4] = { 3, 3, 3, 3 };
        uint16_t out[4] = { 0 };
        for (int i = 0; i < 4; i++) AM.vin[ch[i]] = vv[i];
        adc_config_scan(ch, 4, sm);
        int rc = adc_read_scan_polled(out, 4, NULL, NULL);
        bool ok = rc == 4 && AM.conversions == 4;
        for (int i = 0; i < 4; i++) ok &= out[i] == ideal_code(vv[i], 3.3);
        printf("      scan raw = %u %u %u %u  (ideal %u %u %u %u)\n", out[0], out[1], out[2], out[3],
               ideal_code(vv[0], 3.3), ideal_code(vv[1], 3.3), ideal_code(vv[2], 3.3), ideal_code(vv[3], 3.3));
        CHECK(ok && M.unexpected == 0);
    }
    {   /* mV 변환: 전수 raw 0..4095 × DATA 1300..1800 vs 독립 기준 */
        unsigned bad = 0, n = 0;
        for (uint32_t d = 1300; d <= 1800; d++)
            for (uint32_t raw = 0; raw < 4096; raw++, n++)
                bad += adc_raw_to_mv_cal(raw, 1502, d) != round_div((uint64_t)3300 * 1502 * raw, (uint64_t)d * 4096);
        printf("      raw_to_mv_cal exhaustive: %u cases, %u bad\n", n, bad);
        CHECK(bad == 0);
        CHECK(adc_vdda_mv(1502, 1652, 0) == 3000 && adc_vdda_mv(1502, 1502, 0) == 3300 && adc_vdda_mv(1502, 0, 0) == 0);
        CHECK(adc_raw_to_mv(4095, 12, 3300) == 3299 && adc_raw_to_mv(2048, 12, 3000) == 1500 && adc_raw_to_mv(1, 14, 3300) == 0);
    }
    {   /* VREFINT 경로: VDDA 가 실제로 3.0 V */
        sim_init(3.0, 0);
        uint16_t cal = REG_READ16(VREFINT_CAL_ADDR);
        uint8_t c = ADC_CH_VREFINT, s = 7; uint16_t d = 0;
        adc_config_scan(&c, 1, &s);
        adc_read_scan_polled(&d, 1, NULL, NULL);
        uint32_t vdda = adc_vdda_mv(cal, d, 0);
        printf("      VREFINT_CAL=%u DATA=%u -> VDDA=%u mV (truth 3000)\n", cal, d, vdda);
        CHECK(cal == g_cal && vdda >= 2997 && vdda <= 3003);
    }

    printf("== Part B — 고장과 경계 ==\n");
    {   /* 잘못된 설정은 거부되고, 레지스터는 하나도 안 바뀐다 */
        sim_init(3.3, 0);
        mock_poke(ADC_SQR3, 0x15u); mock_poke(ADC_SMPR2, 0x123u);
        uint64_t w0 = M.writes;
        uint8_t ch[17] = { 1, 2, 1 }, smp_bad[3] = { 3, 3, 4 }, ch19 = 19, s8 = 8, s0 = 0;
        CHECK(adc_config_scan(ch, 0, smp_bad) == ADC_EINVAL && adc_config_scan(ch, 17, smp_bad) == ADC_EINVAL);
        CHECK(adc_config_scan(&ch19, 1, &s0) == ADC_EINVAL && adc_config_scan(ch, 1, &s8) == ADC_EINVAL);
        CHECK(adc_config_scan(ch, 3, smp_bad) == ADC_EINVAL);           /* ch1 이 smp 3 과 4 둘 다 */
        CHECK(M.writes == w0 && mock_peek(ADC_SQR3) == 0x15u && mock_peek(ADC_SMPR2) == 0x123u);
        uint8_t smp_ok[3] = { 3, 3, 3 };
        CHECK(adc_config_scan(ch, 3, smp_ok) == ADC_OK && dec_len() == 3 && dec_rank(2) == 1);
        uint16_t out[4];
        CHECK(adc_read_scan_polled(out, 4, NULL, NULL) == ADC_EINVAL);  /* n 이 L+1 과 다름 */
    }
    {   /* EOCS=0 로 폴링 스캔: 시퀀스 끝에만 EOC → 마지막 채널만 받고 나머지는 타임아웃 */
        sim_init(3.3, 0);
        uint8_t ch[3] = { 1, 2, 3 }, sm[3] = { 1, 1, 1 };
        AM.vin[1] = 0.5; AM.vin[2] = 1.5; AM.vin[3] = 2.5;
        adc_config_scan(ch, 3, sm);
        adc_enable();
        REG_WRITE(ADC_CR1, ADC_CR1_SCAN);
        REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_SWSTART);             /* 버그: EOCS 안 켬 */
        uint16_t got[3] = { 0 }; int rc = 0;
        for (int r = 0; r < 3 && rc == 0; r++) {
            unsigned polls = 0;
            while (!(REG_READ(ADC_SR) & ADC_SR_EOC)) if (++polls >= ADC_POLL_LIMIT) { rc = ADC_ETIMEOUT; break; }
            if (!rc) got[r] = (uint16_t)REG_READ(ADC_DR);
        }
        printf("      EOCS=0 naive: got[0]=%u (ch3 ideal %u, ch1 ideal %u), rc=%d\n", got[0], ideal_code(2.5, 3.3), ideal_code(0.5, 3.3), rc);
        CHECK(got[0] == ideal_code(2.5, 3.3) && rc == ADC_ETIMEOUT && AM.ovr_events == 0);
    }
    {   /* 느린 루프 → OVR. 경계 w_max = (P−2) + ⌊(P−2)/(n−2)⌋ (P = 변환 주기, 2 = SR·DR 접근) */
        unsigned bad = 0;
        const uint8_t smps[3] = { 0, 1, 3 };
        for (unsigned n = 3; n <= 16; n++)
            for (int k = 0; k < 3; k++) {
                int p = (int)adc_conv_cycles(smps[k]);
                bad += max_work_without_ovr(smps[k], n) != (p - 2) + (p - 2) / (int)(n - 2);
            }
        printf("      max work w/o OVR (smp 3/15/56 cyc): n=3 %d/%d/%d  n=4 %d/%d/%d  n=16 %d/%d/%d\n",
               max_work_without_ovr(0, 3), max_work_without_ovr(1, 3), max_work_without_ovr(3, 3),
               max_work_without_ovr(0, 4), max_work_without_ovr(1, 4), max_work_without_ovr(3, 4),
               max_work_without_ovr(0, 16), max_work_without_ovr(1, 16), max_work_without_ovr(3, 16));
        CHECK(bad == 0);
        sim_init(3.3, 0);
        uint8_t ch[4] = { 1, 2, 3, 4 }, sm[4] = { 1, 1, 1, 1 };
        unsigned work = 40; uint16_t out[4];
        adc_config_scan(ch, 4, sm);
        CHECK(adc_read_scan_polled(out, 4, slow_cb, &work) == ADC_EOVR && AM.ovr_events == 1);
        /* OVR 를 안 지우고 SWSTART 만 → 모델은 무시 → 다음 EOC 영영 없음 */
        REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_EOCS | ADC_CR2_SWSTART);
        CHECK(AM.ignored_triggers == 1 && !AM.busy);
        work = 5;                                                       /* 드라이버 재호출 = SR 클리어 후 재시작 */
        CHECK(adc_read_scan_polled(out, 4, slow_cb, &work) == 4);
    }
    {   /* ADON 과 SWSTART 를 한 번에 → tSTAB 전이라 무시 → 타임아웃 */
        sim_init(3.3, 0);
        uint8_t c = 1, s = 1;
        adc_config_scan(&c, 1, &s);
        REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_EOCS | ADC_CR2_SWSTART);
        unsigned polls = 0;
        while (!(REG_READ(ADC_SR) & ADC_SR_EOC) && polls < ADC_POLL_LIMIT) polls++;
        CHECK(polls == ADC_POLL_LIMIT && AM.ignored_triggers == 1);
        uint16_t v; CHECK(adc_read_scan_polled(&v, 1, NULL, NULL) == 1);
    }
    {   /* 고임피던스 + 짧은 샘플링: 이전 채널(0 V)의 잔류 전하에 끌려 낮게 읽힌다 */
        int err[8];
        for (int s = 0; s < 8; s++) {
            sim_init(3.3, 0);
            AM.vin[0] = 0.0; AM.vin[1] = 3.0; AM.rsrc[1] = 50e3;
            uint8_t ch[2] = { 0, 1 }, sm[2] = { 7, (uint8_t)s }; uint16_t out[2];
            adc_config_scan(ch, 2, sm);
            adc_read_scan_polled(out, 2, NULL, NULL);
            err[s] = (int)out[1] - (int)ideal_code(3.0, 3.3);
        }
        printf("      50k source, 3.0 V after 0 V: err[LSB] by smp = %d %d %d %d %d %d %d %d\n",
               err[0], err[1], err[2], err[3], err[4], err[5], err[6], err[7]);
        bool mono = true;
        for (int s = 1; s < 8; s++) mono &= err[s] >= err[s - 1];
        CHECK(err[0] < -1000 && mono && err[3] == 0 && err[2] < 0);
    }
    {   /* 데이터시트 공식으로 고른 smp → 모델에서 최악 계단(0 V → 3.2 V)에도 오차 0 LSB */
        CHECK(adc_pick_smp(0, 21000000) == 1 && adc_pick_smp(50000, 21000000) == 3);
        CHECK(adc_pick_smp(200000, 21000000) == 7 && adc_pick_smp(600000, 21000000) == ADC_EINVAL);
        CHECK(adc_pick_smp(0, 7000000) == 0);                           /* 느린 ADCCLK 이면 3 cycle 도 OK */
        unsigned bad = 0, tried = 0;
        for (int t = 0; t < 400; t++) {
            uint32_t r = xs32(&seed) % 580000u;
            int k = adc_pick_smp(r, 21000000);
            if (k < 0) { bad++; continue; }
            sim_init(3.3, 0);
            AM.vin[0] = 0.0; AM.vin[1] = 3.2; AM.rsrc[1] = r;
            uint8_t ch[2] = { 0, 1 }, sm[2] = { 7, (uint8_t)k }; uint16_t out[2];
            adc_config_scan(ch, 2, sm);
            adc_read_scan_polled(out, 2, NULL, NULL);
            bad += out[1] != ideal_code(3.2, 3.3); tried++;
        }
        printf("      pick_smp property: %u sources, %u bad\n", tried, bad);
        CHECK(bad == 0);
    }
    {   /* 32비트 오버플로와 이중 반올림 — 정답 함수만 기준과 일치 */
        unsigned ovf = 0, dbl = 0, mine = 0;
        for (uint32_t raw = 0; raw < 4096; raw++) {
            uint32_t ref = round_div((uint64_t)3300 * 1502 * raw, (uint64_t)1652 * 4096);
            uint32_t naive32 = (uint32_t)(3300u * 1502u * raw) / (1652u * 4096u);
            uint32_t two_step = adc_raw_to_mv(raw, 12, adc_vdda_mv(1502, 1652, 0));
            ovf += naive32 != ref; dbl += two_step != ref; mine += adc_raw_to_mv_cal(raw, 1502, 1652) != ref;
        }
        printf("      raw 0..4095 @CAL=1502 DATA=1652: 32-bit naive wrong %u, two-step wrong %u, one-shot wrong %u\n", ovf, dbl, mine);
        CHECK(ovf > 3000 && dbl > 0 && mine == 0);
    }

    printf("== Part C — 펌웨어 현실 ==\n");
    {   /* 노이즈 0: 16번 평균해도 정보가 안 늘어난다 — 결과 = 4·code 정확히 */
        unsigned bad = 0;
        for (int t = 0; t < 200; t++) {
            sim_init(3.3, 0);
            double v = 0.1 + 3.1 * (xs32(&seed) / 4294967296.0);
            AM.vin[6] = v;
            uint32_t r14 = 0;
            bad += adc_oversample(6, 1, 2, &r14) != ADC_OK || r14 != 4u * ideal_code(v, 3.3);
        }
        CHECK(bad == 0);
    }
    {   /* σ = 0.5 LSB 노이즈(디더): n 이 클수록 RMS 오차가 줄어든다 (단위: 12비트 LSB) */
        double rms[3] = { 0 }; const unsigned ns[3] = { 0, 2, 4 }; const int levels[3] = { 400, 400, 60 };
        for (int k = 0; k < 3; k++) {
            double se = 0;
            for (int t = 0; t < levels[k]; t++) {
                sim_init(3.3, 0.5);
                AM.rng = 0x9E3779B9u ^ (uint32_t)(t * 7919 + k);
                double v = 0.1 + 3.1 * (xs32(&seed) / 4294967296.0);
                AM.vin[6] = v;
                uint32_t r = 0;
                adc_oversample(6, 1, ns[k], &r);
                double e = r / (double)(1u << ns[k]) - v * 4096.0 / 3.3;
                se += e * e;
            }
            rms[k] = sqrt(se / levels[k]);
        }
        printf("      sigma=0.5 LSB: RMS err n=0 %.3f, n=2 %.3f, n=4 %.3f LSB12\n", rms[0], rms[1], rms[2]);
        CHECK(rms[1] < 0.6 * rms[0] && rms[2] < 0.5 * rms[1]);
    }
    {   /* 인터럽트 스캔 + 보드 모니터 (VDDA 3.0 V) */
        sim_init(3.0, 0);
        AM.vin[1] = 1.85;  AM.rsrc[1] = 50e3;     /* 배터리 100k/100k 분압 → 3.70 V 팩 */
        AM.vin[4] = 0.42;  AM.rsrc[4] = 100;      /* 전류 감지 앰프 출력 */
        AM.vin[8] = 1.40;  AM.rsrc[8] = 5e3;      /* NTC 10k/10k */
        uint8_t ch[4] = { ADC_CH_VREFINT, 1, 4, 8 }, sm[4];
        uint32_t rs[4] = { 200000, 50000, 100, 5000 };
        for (int i = 0; i < 4; i++) sm[i] = (uint8_t)adc_pick_smp(rs[i], 21000000);
        CHECK(adc_config_scan(ch, 4, sm) == ADC_OK);
        unsigned cyc = 0; for (int i = 0; i < 4; i++) cyc += adc_conv_cycles(sm[i]);
        CHECK(adc_start_scan_it(4) == ADC_OK);
        unsigned steps = 0;
        while (!g_adc.done && steps < 100000) { sim_step(); steps++; }
        bool ok = g_adc.done && NV.calls[ADC_IRQn] == 4 && g_adc.ovr_count == 0;
        for (int i = 0; i < 4; i++) ok &= g_adc.buf[i] == ideal_code(AM.vin[ch[i]], 3.0);
        CHECK(ok);
        uint16_t cal = REG_READ16(VREFINT_CAL_ADDR);
        uint32_t vdda = adc_vdda_mv(cal, g_adc.buf[0], 0);
        uint32_t bat = 2 * adc_raw_to_mv_cal(g_adc.buf[1], cal, g_adc.buf[0]);
        uint32_t cur = adc_raw_to_mv_cal(g_adc.buf[2], cal, g_adc.buf[0]);
        uint32_t ntc = adc_raw_to_mv_cal(g_adc.buf[3], cal, g_adc.buf[0]);
        uint32_t bat_naive = 2 * adc_raw_to_mv(g_adc.buf[1], 12, 3300);
        printf("      [adc] smp=%u/%u/%u/%u seq=%u cyc=%.2f us | VDDA=%u mV (VREFINT raw=%u CAL=%u)\n",
               sm[0], sm[1], sm[2], sm[3], cyc, cyc / 21.0, vdda, g_adc.buf[0], cal);
        printf("      [adc] VBAT=%u mV (naive@3300: %u) I_SENSE=%u mV NTC=%u mV  isr=%u ovr=%u\n",
               bat, bat_naive, cur, ntc, NV.calls[ADC_IRQn], g_adc.ovr_count);
        CHECK(bat >= 3697 && bat <= 3703 && cur >= 419 && cur <= 421 && ntc >= 1399 && ntc <= 1401);
        CHECK(bat_naive > 4000);                                        /* 3.3 V 가정 → 10% 높게 */
    }
    {   /* ISR 지연(마스크) 주입 → OVR → ISR 이 처음부터 재시작 → 최종 결과는 정상 */
        sim_init(3.3, 0);
        uint8_t ch[3] = { 1, 2, 3 }, sm[3] = { 1, 1, 1 };
        AM.vin[1] = 0.3; AM.vin[2] = 1.3; AM.vin[3] = 2.3;
        adc_config_scan(ch, 3, sm);
        adc_start_scan_it(3);
        unsigned steps = 0; bool injected = false;
        while (!g_adc.done && steps < 100000) {
            sim_step(); steps++;
            if (!injected && g_adc.idx == 1) { NV.mask_ticks = 60; injected = true; }   /* 다른 고우선 ISR 이 60 cycle */
        }
        bool ok = g_adc.done && g_adc.ovr_count == 1 && AM.ovr_events == 1;
        for (int i = 0; i < 3; i++) ok &= g_adc.buf[i] == ideal_code(AM.vin[ch[i]], 3.3);
        printf("      ISR latency 60 cyc: ovr=%u, isr calls=%u, conversions=%u\n", g_adc.ovr_count, NV.calls[ADC_IRQn], AM.conversions);
        CHECK(ok);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
