/* BM-M09 ADC + DMA 더블 버퍼링 파이프라인
 *
 * ADC1 이 연속 변환(CONT)으로 샘플을 만들고 DMA2 Stream0 이 circular 모드로 2N 칸 버퍼를 채운다.
 * HT(half transfer) → 앞 반쪽 [0,N) 처리, TC(transfer complete) → 뒤 반쪽 [N,2N) 처리.
 *
 * Part A — pipe_start(): DMA/ADC 설정 (EN=0 확인 → 플래그 클리어 → PAR/M0AR/NDTR → CR → EN, ADC DMA|DDS|CONT)
 *          process_half(): 반쪽 평균(반올림) + 샘플 단위 EMA(Q8, α=1/16, 반쪽 경계를 넘어 이어짐)
 *          pipe_dma_irq(): 플래그로 반쪽 고르기 → 그 비트만 W1C → 처리 → 덮어쓰기 검사 → 출력
 * Part B — pipe_reentered(): 처리가 끝난 순간 DMA 가 이미 h 로 다시 들어왔나 (NDTR 먼저, 플래그 나중)
 *          pipe_resync(): 밀린 HT/TC 를 버리고 다음 레코드에 GAP 표시
 * Part C — pipe_health(): 메인 루프에서 ADC OVR (DDS/CIRC 설정 버그 등) 감지 → 복구,  budget_calc()
 *
 * 모델 (이 파일의 정의. 주소·비트는 STM32F4 RM0090 기준 대표값 — RM 확인 권장):
 *   시간 단위 = mock 클럭. ADC 는 S 클럭마다 변환 1개(EOC). CR2.DMA=1 이고 막히지 않았으면 같은 클럭에 DMA 가
 *   DR 을 읽어(EOC 클리어) 버퍼에 쓴다. 처리 비용은 SIM_WORK_*() 가 클럭을 소비 — 그동안 ADC/DMA 는 계속 돈다.
 *   인터럽트는 NVIC-lite: DMA 모델이 IRQ 선을 올리고 sim_step() 이 하드웨어 1클럭 뒤 핸들러를 동기 호출한다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 로그와 레코드 표)
 * 타깃: UNIT_TEST 를 빼면 REG_READ/REG_WRITE 가 volatile MMIO, BUF_READ 가 진짜 버퍼, SIM_WORK 는 빈 매크로.
 */
#define UNIT_TEST 1
#include <stdarg.h>
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

/* ADC1 — RM0090 기준 대표값 */
#define ADC1_BASE        0x40012000u
#define ADC_SR           (ADC1_BASE + 0x00u)
#define ADC_CR2          (ADC1_BASE + 0x08u)
#define ADC_DR           (ADC1_BASE + 0x4Cu)
#define ADC_SR_EOC       (1u << 1)     /* rc_w0, DR 읽기로도 지워짐 */
#define ADC_SR_STRT      (1u << 4)
#define ADC_SR_OVR       (1u << 5)     /* rc_w0 */
#define ADC_CR2_ADON     (1u << 0)
#define ADC_CR2_CONT     (1u << 1)
#define ADC_CR2_DMA      (1u << 8)
#define ADC_CR2_DDS      (1u << 9)     /* 1: DMA 마지막 전송 뒤에도 계속 요청 (circular 에 필수) */
#define ADC_CR2_SWSTART  (1u << 30)

/* DMA2 Stream0 (ADC1 = 채널 0) — RM0090 기준 대표값 */
#define DMA2_BASE        0x40026400u
#define DMA_LISR         (DMA2_BASE + 0x00u)   /* r   : stream 0..3 플래그 */
#define DMA_LIFCR        (DMA2_BASE + 0x08u)   /* w1c : 같은 비트 위치 */
#define DMA_S0CR         (DMA2_BASE + 0x10u)   /* stream x = 0x10 + 0x18·x */
#define DMA_S0NDTR       (DMA2_BASE + 0x14u)
#define DMA_S0PAR        (DMA2_BASE + 0x18u)
#define DMA_S0M0AR       (DMA2_BASE + 0x1Cu)
#define DMA_S0FCR        (DMA2_BASE + 0x24u)
#define DMA_CR_EN        (1u << 0)
#define DMA_CR_TEIE      (1u << 2)
#define DMA_CR_HTIE      (1u << 3)
#define DMA_CR_TCIE      (1u << 4)
#define DMA_CR_DIR_P2M   (0u << 6)
#define DMA_CR_CIRC      (1u << 8)
#define DMA_CR_MINC      (1u << 10)
#define DMA_CR_PSIZE_16  (1u << 11)
#define DMA_CR_MSIZE_16  (1u << 13)
#define DMA_CR_PL_HIGH   (2u << 16)
#define DMA_CR_CHSEL(c)  ((uint32_t)(c) << 25)
#define DMA_S0_FEIF      (1u << 0)     /* stream 0 = 그룹 오프셋 0 */
#define DMA_S0_DMEIF     (1u << 2)
#define DMA_S0_TEIF      (1u << 3)
#define DMA_S0_HTIF      (1u << 4)
#define DMA_S0_TCIF      (1u << 5)
#define DMA_S0_ALL       (DMA_S0_FEIF | DMA_S0_DMEIF | DMA_S0_TEIF | DMA_S0_HTIF | DMA_S0_TCIF)
#define DMA2_Stream0_IRQn 56            /* STM32F4 벡터 번호 (확인 권장) */

#define SRAM_BASE        0x20000000u
#define NMAX             256u           /* 반쪽 최대 길이 → 버퍼 2·NMAX 칸 (uint16) */
#define ADC_BUF_ADDR     SRAM_BASE      /* 타깃: (uint32_t)(uintptr_t)adc_buf */
#define PIPE_CR_CFG      (DMA_CR_CHSEL(0) | DMA_CR_PL_HIGH | DMA_CR_MSIZE_16 | DMA_CR_PSIZE_16 | \
                          DMA_CR_MINC | DMA_CR_CIRC | DMA_CR_DIR_P2M | DMA_CR_HTIE | DMA_CR_TCIE)

/* ======================= mock 코어 (BM-B02 mock 축약판) ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; } mock_reg_t;
#define MOCK_MAX_REGS 16
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int g_nregs;
static uint32_t g_unexpected;           /* 모르는 주소 접근 = 테스트 실패 */
static uint32_t g_cpu_acc;              /* CPU 레지스터 접근 횟수 (인터리브 훅용) */
static uint32_t g_hook_at;              /* BM-B04 식: g_cpu_acc 번째 접근 "직전" 에 훅 실행 */
static void (*g_hook_fn)(void);

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
static void mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w) {
    if (g_nregs < MOCK_MAX_REGS) g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w };
}
static void cpu_access_hook(void) {
    g_cpu_acc++;
    if (g_hook_fn && g_cpu_acc == g_hook_at) { void (*f)(void) = g_hook_fn; g_hook_fn = NULL; f(); }
}
uint32_t mock_read32(uint32_t addr) {
    cpu_access_hook();
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; return 0u; }
    return g->r ? g->r(addr, g->val, NULL) : g->val;
}
void mock_write32(uint32_t addr, uint32_t val) {
    cpu_access_hook();
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; return; }
    g->val = g->w ? g->w(addr, g->val, val, NULL) : val;
}
static uint32_t bus_read32(uint32_t addr) {            /* DMA 의 버스 읽기: CPU 카운터/훅 없음 */
    mock_reg_t *g = mock_find(addr);
    if (!g) { g_unexpected++; return 0u; }
    return g->r ? g->r(addr, g->val, NULL) : g->val;
}
static uint32_t *hw(uint32_t addr) { mock_reg_t *g = mock_find(addr); return g ? &g->val : NULL; }

/* ======================= NVIC-lite + 시뮬레이션 시계 ======================= */
#define NIRQ 64
static bool g_line[NIRQ], g_en[NIRQ];
static void (*g_vec[NIRQ])(void);
static bool g_in_isr, g_nvic_off;
static uint64_t g_clk;                       /* mock 클럭 */
static uint64_t g_t_stop = UINT64_MAX;     /* run_until 의 끝 시각 (ISR 사슬도 여기서 멈춤) */
static uint64_t g_block_from, g_block_until; /* 주입: 이 구간엔 디스패치 안 함 (높은 우선순위 ISR/임계 구역) */
static uint32_t g_isr_calls, g_isr_storm;
static void irq_line_set(int irqn, bool level) { g_line[irqn] = level; }
static void nvic_enable_irq(int irqn) { g_en[irqn] = true; }       /* 타깃: CMSIS NVIC_EnableIRQ */

/* ======================= ADC 모델 (BM-M07 ADC 모델 축약판: 단일 채널, CONT) ======================= */
typedef struct {
    uint32_t *sr, *cr2, *dr;
    uint32_t period, phase;      /* 변환 1개 = period 클럭 */
    bool running, eot_block;     /* eot_block: DDS=0 에서 DMA 마지막 전송 뒤 요청 금지 (DMA 비트 0 쓰면 풀림) */
    uint32_t conv, dr_abs;       /* conv = 다음 샘플의 절대 번호, dr_abs = DR 에 든 샘플 번호 */
    uint32_t ovr_count;
} adc_model_t;
static adc_model_t g_adc;

/* 입력 신호: 절대 샘플 번호 → 12비트 코드 (삼각파 + 해시 잡음, 결정적) */
static uint16_t sig(uint32_t i) {
    uint32_t h = i * 0x9E3779B1u; h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
    int32_t tri = (int32_t)(i % 400u); if (tri >= 200) tri = 400 - tri;
    return (uint16_t)(1000 + tri * 10 + (int32_t)(h & 255u) - 128);   /* 872..3127 */
}

/* ======================= DMA 모델 (BM-M08 DMA 모델 축약판: stream 0, P2M, 16비트) ======================= */
typedef struct {
    uint32_t *cr, *ndtr, *par, *m0ar, *lisr;
    uint32_t reload;             /* EN 시점 NDTR = circular 재장전 값 */
    uint32_t stopping;           /* EN←0 을 쓴 뒤 EN 이 실제로 0 이 될 때까지 남은 CR 읽기 수 (모델) */
    uint64_t total;              /* 지금까지 전송 수 */
    uint64_t ready_total[2];     /* 반쪽 h 가 마지막으로 찬 순간의 total (harness 정답용) */
    uint32_t ready_base[2];      /* 그때 반쪽 h 첫 칸의 절대 샘플 번호 */
    uint64_t ready_clk[2];       /* 그 시각 */
} dma_model_t;
static dma_model_t g_dma;
static uint16_t g_sram16[2 * NMAX];          /* SRAM_BASE 부터의 모델 SRAM (DMA 버퍼) */
static uint32_t g_slot_abs[2 * NMAX];        /* 각 칸에 든 샘플의 절대 번호 (모델만 앎) */

static void dma_irq_update(void) {
    uint32_t cr = *g_dma.cr, en = 0;
    if (cr & DMA_CR_HTIE) en |= DMA_S0_HTIF;
    if (cr & DMA_CR_TCIE) en |= DMA_S0_TCIF;
    if (cr & DMA_CR_TEIE) en |= DMA_S0_TEIF;
    irq_line_set(DMA2_Stream0_IRQn, (*g_dma.lisr & en) != 0u);
}
static void dma_ready(uint32_t h) {
    uint32_t n = g_dma.reload / 2u;
    g_dma.ready_total[h] = g_dma.total;
    g_dma.ready_base[h] = g_slot_abs[h * n];
    g_dma.ready_clk[h] = g_clk;
}
static void dma_request(void) {                    /* ADC 가 EOC 와 함께 올리는 요청 1개 */
    uint32_t cr = *g_dma.cr;
    if (!(cr & DMA_CR_EN) || g_dma.stopping) return;          /* 서비스 안 됨 → EOC 가 남는다 */
    uint32_t n = *g_dma.ndtr, idx = (cr & DMA_CR_MINC) ? g_dma.reload - n : 0u;
    uint32_t addr = *g_dma.m0ar + 2u * idx, slot = (addr - SRAM_BASE) / 2u;
    if (addr < SRAM_BASE || slot >= 2u * NMAX) {               /* 모델: 잘못된 주소 → TEIF, EN=0 */
        *g_dma.lisr |= DMA_S0_TEIF; *g_dma.cr &= ~DMA_CR_EN; dma_irq_update(); return;
    }
    g_sram16[slot] = (uint16_t)bus_read32(*g_dma.par);       /* DR 읽기 → EOC 클리어 */
    g_slot_abs[slot] = g_adc.dr_abs;
    g_dma.total++;
    n--;
    if (n == g_dma.reload / 2u) { *g_dma.lisr |= DMA_S0_HTIF; dma_ready(0); }
    if (n == 0u) {
        *g_dma.lisr |= DMA_S0_TCIF; dma_ready(1);
        if (cr & DMA_CR_CIRC) n = g_dma.reload; else *g_dma.cr &= ~DMA_CR_EN;
        if (!(*g_adc.cr2 & ADC_CR2_DDS)) g_adc.eot_block = true;   /* 마지막 전송 알림 (EOT) */
    }
    *g_dma.ndtr = n;
    dma_irq_update();
}

static void adc_tick(void) {
    if (!g_adc.running || --g_adc.phase) return;
    g_adc.phase = g_adc.period;
    if (*g_adc.sr & ADC_SR_EOC) { *g_adc.sr |= ADC_SR_OVR; g_adc.ovr_count++; }   /* 앞 값 안 읽힘 */
    *g_adc.dr = sig(g_adc.conv);
    g_adc.dr_abs = g_adc.conv++;
    *g_adc.sr |= ADC_SR_EOC;
    if ((*g_adc.cr2 & ADC_CR2_DMA) && !(*g_adc.sr & ADC_SR_OVR) && !g_adc.eot_block) dma_request();
}

/* 레지스터 훅 */
static uint32_t on_w_adc_sr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)c; return o & v; } /* rc_w0 */
static uint32_t on_r_adc_dr(uint32_t a, uint32_t s, void *c) { (void)a; (void)c; *g_adc.sr &= ~ADC_SR_EOC; return s; }
static uint32_t on_w_adc_cr2(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    if (!(v & ADC_CR2_DMA)) g_adc.eot_block = false;
    if (!(v & ADC_CR2_ADON)) g_adc.running = false;
    if ((v & ADC_CR2_SWSTART) && (v & ADC_CR2_ADON)) {
        if (!g_adc.running) { g_adc.running = true; g_adc.phase = g_adc.period; }
        *g_adc.sr |= ADC_SR_STRT;
    }
    return v & ~ADC_CR2_SWSTART;                                 /* SWSTART 는 하드웨어가 0 으로 */
}
static uint32_t on_w_lisr(uint32_t a, uint32_t o, uint32_t v, void *c) { (void)a; (void)v; (void)c; return o; } /* r */
static uint32_t on_w_lifcr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)o; (void)c;
    *g_dma.lisr &= ~(v & DMA_S0_ALL);                            /* W1C: 1 을 쓴 비트만 */
    dma_irq_update();
    return 0u;                                                   /* 읽으면 0 */
}
static uint32_t on_r_s0cr(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)c;
    if (g_dma.stopping && --g_dma.stopping == 0u) { *g_dma.cr &= ~DMA_CR_EN; s = *g_dma.cr; }
    return s;
}
static uint32_t on_w_s0cr(uint32_t a, uint32_t o, uint32_t v, void *c) {
    (void)a; (void)c;
    if (o & DMA_CR_EN) {                                         /* EN=1 동안엔 EN 끄기만 받는다 (모델) */
        if (!(v & DMA_CR_EN)) g_dma.stopping = 2u;
        return o;
    }
    if ((v & DMA_CR_EN) && *g_dma.ndtr == 0u) v &= ~DMA_CR_EN;   /* NDTR=0 이면 켜지지 않음 (모델) */
    if (v & DMA_CR_EN) g_dma.reload = *g_dma.ndtr;
    return v;
}
static uint32_t on_w_locked(uint32_t a, uint32_t o, uint32_t v, void *c) {  /* NDTR/PAR/M0AR: EN=1 이면 무시 */
    (void)c;
    if (*g_dma.cr & DMA_CR_EN) return o;
    return a == DMA_S0NDTR ? (v & 0xFFFFu) : v;
}

/* ======================= 시뮬레이션 루프 ======================= */
typedef struct { uint32_t pre, per_sample; } cost_t;
static cost_t g_cost;                          /* 처리 비용 (클럭): 반쪽 시작 시 pre, 샘플마다 per_sample */
static struct { uint64_t ready_total[2], ready_clk[2], t_entry; uint32_t ready_base[2]; } g_snap; /* ISR 진입 순간 */

static void hw_clock(void) { g_clk++; adc_tick(); }
static void cpu_busy(uint32_t clocks) { while (clocks--) hw_clock(); }   /* CPU 가 일하는 동안 HW 는 돈다 */
static void nvic_dispatch(void) {
    if (g_in_isr || g_nvic_off || (g_clk >= g_block_from && g_clk < g_block_until)) return;
    for (int idle = 0; idle < 8; ) {                             /* tail-chaining: 펜딩이면 바로 다음 ISR */
        int irq = -1;
        for (int i = 0; i < NIRQ; i++) if (g_line[i] && g_en[i] && g_vec[i]) { irq = i; break; }
        if (irq < 0 || g_clk >= g_t_stop) return;               /* 시뮬레이션 끝 시각을 넘기면 멈춤 */
        memcpy(g_snap.ready_total, g_dma.ready_total, sizeof g_snap.ready_total);
        memcpy(g_snap.ready_base, g_dma.ready_base, sizeof g_snap.ready_base);
        memcpy(g_snap.ready_clk, g_dma.ready_clk, sizeof g_snap.ready_clk);
        g_snap.t_entry = g_clk;
        uint64_t t0 = g_clk;
        g_in_isr = true; g_isr_calls++; g_vec[irq](); g_in_isr = false;
        if (g_clk == t0) idle++;                                 /* 시간도 안 쓰고 선도 안 내린 ISR */
    }
    g_isr_storm++;                                               /* 플래그를 안 지우는 ISR → 무한 재진입 */
}
static void sim_step(void) { hw_clock(); nvic_dispatch(); }

/* 펌웨어가 보는 seam: 버퍼 읽기, 처리 비용 */
#define REC_OVR 0x01u      /* 이 반쪽을 처리하는 동안 DMA 가 다시 들어왔다 → 데이터 의심 */
#define REC_GAP 0x02u      /* 이 레코드 앞에서 반쪽이 버려졌다 (연속 아님) */
static uint32_t g_rd_abs[2 * NMAX], g_rd_n;
static uint16_t buf_read(uint32_t i) {
    if (i >= 2u * NMAX) { g_unexpected++; return 0u; }
    if (g_rd_n < 2u * NMAX) g_rd_abs[g_rd_n++] = g_slot_abs[i];
    return g_sram16[i];
}
#define BUF_READ(i)        buf_read(i)
#define SIM_WORK_PRE()     cpu_busy(g_cost.pre)
#define SIM_WORK_SAMPLE()  cpu_busy(g_cost.per_sample)

/* 출력 스트림 + 로그 (harness 소유) */
typedef struct {
    uint8_t h, flags; uint16_t mean, ema;
    uint64_t ready, late, t, k; uint32_t base, torn; bool truth; /* 찬 시각, 진입 지연, 출력 시각, h 이후 전송 수 */
} rec_t;
#define REC_CAP 1024
static rec_t g_rec[REC_CAP];
static uint32_t g_nrec, g_rec_lost, g_n_truth;
static void out_push(uint32_t h, uint32_t flags, uint16_t mean, uint16_t ema) {
    if (g_nrec >= REC_CAP) { g_rec_lost++; g_rd_n = 0; return; }
    rec_t *r = &g_rec[g_nrec++];
    r->h = (uint8_t)h; r->flags = (uint8_t)flags; r->mean = mean; r->ema = ema; r->t = g_clk;
    r->base = g_snap.ready_base[h & 1u];
    r->k = g_dma.total - g_snap.ready_total[h & 1u];
    r->ready = g_snap.ready_clk[h & 1u];
    r->late = g_snap.t_entry - r->ready;
    r->truth = r->k >= (uint64_t)g_n_truth + 1u;                  /* 정답: h 에 새 샘플이 1개 이상 써졌다 */
    r->torn = 0;
    for (uint32_t j = 0; j < g_n_truth; j++)
        if (j >= g_rd_n || g_rd_abs[j] != r->base + j) r->torn++;
    g_rd_n = 0;
}
#define LOG_CAP 48
static char g_log[LOG_CAP][96];
static uint32_t g_nlog;
static void log_line(const char *fmt, ...) {
    if (g_nlog >= LOG_CAP) return;
    int k = snprintf(g_log[g_nlog], sizeof g_log[0], "[t=%7llu] ", (unsigned long long)g_clk);
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_log[g_nlog] + k, sizeof g_log[0] - (size_t)k, fmt, ap);
    va_end(ap);
    g_nlog++;
}
#define LOG(...) log_line(__VA_ARGS__)

/* ======================= 펌웨어: 파이프라인 ======================= */
typedef struct {
    uint32_t n;                  /* 반쪽 길이 N (버퍼 = 2N 샘플) */
    int32_t  ema_q8;             /* EMA 상태, Q8 (값·256) */
    bool     ema_valid, gap_pending;
    uint32_t halves, overruns, resyncs, late_entries, adc_recoveries;
} pipe_t;
static pipe_t g_pipe;

static void pipe_resync(pipe_t *p);

/* TODO A1: DMA2 Stream0 을 무장(arm)한다 — pipe_start 와 복구 경로가 함께 쓴다.
 *  ① CR = 0 (EN 끄기) 후 EN 이 0 으로 읽힐 때까지 기다린다 (최대 100번 읽기, 아니면 -1)
 *  ② LIFCR 에 stream 0 플래그 전부 (DMA_S0_ALL) — 지난 실행의 TCIF 가 남아 있으면 켜자마자 IRQ
 *  ③ PAR = ADC_DR, M0AR = ADC_BUF_ADDR, NDTR = 2N, FCR = 0 (direct mode)
 *  ④ CR = PIPE_CR_CFG 를 쓰고, 그다음 PIPE_CR_CFG | EN.   반환 0. */
static int dma_arm(const pipe_t *p) {
    (void)p;
    return -1; /* TODO A1 */
}

/* TODO A2: 파이프라인 시작. n_half 2..NMAX 아니면 -1.
 *  *p 를 0 으로 초기화하고 p->n = n_half → dma_arm (실패면 -2) → nvic_enable_irq(DMA2_Stream0_IRQn)
 *  → ADC: SR = 0 (rc_w0 로 EOC/OVR 클리어) → CR2 = ADON|CONT|DMA|DDS → 같은 값 | SWSTART.  반환 0. */
static int pipe_start(pipe_t *p, uint32_t n_half) {
    (void)p; (void)n_half;
    return -1; /* TODO A2 */
}

/* TODO A3: 반쪽 h (칸 h·N .. h·N+N−1) 처리.
 *  맨 앞에 SIM_WORK_PRE() 한 번, 샘플마다 BUF_READ 다음 SIM_WORK_SAMPLE().
 *  mean = (합 + N/2) / N.  EMA(Q8): ema_valid 가 아니면 y = x·256 으로 시작(ema_valid = true),
 *  아니면 y += (x·256 − y) / 16 (C 나눗셈 = 0 쪽 버림).  *ema = (y + 128) >> 8. */
static void process_half(pipe_t *p, uint32_t h, uint16_t *mean, uint16_t *ema) {
    (void)p; (void)h;
    *mean = 0u; *ema = 0u; /* TODO A3 */
}

/* TODO B1: 반쪽 h 처리가 끝난 지금, DMA 가 h 에 새 샘플을 1개 이상 썼나?
 *  NDTR 을 "먼저" 읽고 LISR 을 "나중에" 읽는다 (각 1번).  pos = 2N − NDTR (다음에 쓸 칸).
 *  true ⇔ (pos 가 h 안 && pos 가 h 의 첫 칸이 아님) || (h 자신의 플래그가 다시 섰다: h=0 → HTIF, h=1 → TCIF). */
static bool pipe_reentered(const pipe_t *p, uint32_t h) {
    (void)p; (void)h;
    return false; /* TODO B1 */
}

/* TODO B2: 재동기화. 밀린 HT|TC 를 한 번에 W1C 로 버리고, resyncs++, EMA 무효, 다음 레코드에 GAP. */
static void pipe_resync(pipe_t *p) {
    (void)p; /* TODO B2 */
}

/* TODO A4: DMA2 Stream0 IRQ 본체.
 *  LISR 읽기 → HT·TC 둘 다 서 있으면 순서를 모름: late_entries++, LOG, pipe_resync, return.
 *  하나도 없으면 return.  h = HT ? 0 : 1, 그 비트 하나만 LIFCR 에 (|= 금지).
 *  process_half → flags = (gap_pending ? REC_GAP : 0), gap_pending = false
 *  → pipe_reentered 면 flags |= REC_OVR, overruns++, LOG, pipe_resync / 아니면 halves++
 *  → out_push(h, flags, mean, ema). */
static void pipe_dma_irq(pipe_t *p) {
    (void)p; /* TODO A4 */
}

/* TODO C1: 메인 루프 헬스 체크. ADC SR.OVR 가 없으면 0.
 *  있으면 복구 (RM0090 "OVR with DMA" 순서, 확인 권장): CR2 = ADON|CONT|DDS (DMA=0: 요청 멈춤, DDS=0 래치 해제)
 *  → dma_arm (실패면 -1) → SR = 0 (OVR + 낡은 EOC 클리어) → CR2 = ADON|CONT|DMA|DDS → | SWSTART
 *  → adc_recoveries++, EMA 무효, GAP, LOG.  반환 1. */
static int pipe_health(pipe_t *p) {
    (void)p;
    return 0; /* TODO C1 */
}

/* ======================= Part C: 실시간 예산 ======================= */
typedef struct { uint32_t fs_hz, n_half, cyc_per_sample, cyc_fixed, cpu_hz; } budget_in_t;
typedef struct {
    uint64_t half_ns;        /* 반쪽 주기 = N / fs */
    uint64_t proc_ns;        /* 반쪽 처리 = (N·cyc_per_sample + cyc_fixed) / cpu */
    uint32_t load_permille;  /* proc / half × 1000 (반올림) */
    uint64_t latency_ns;     /* 최악 지연: 반쪽 첫 샘플이 출력될 때까지 = (N−1)/fs + proc */
    bool     ok;             /* proc ≤ half (정수로 정확히: proc_cyc·fs ≤ N·cpu) */
} budget_out_t;

/* TODO C2: 예산 계산. 모든 ns 값은 반올림 정수, 곱은 uint64 로. 인자 중 0 이 있으면 -1, 아니면 0. */
static int budget_calc(const budget_in_t *in, budget_out_t *o) {
    (void)in; (void)o;
    return -1; /* TODO C2 */
}

/* ======================= 테스트 하네스 ======================= */
static uint32_t g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng = 0x2545F491u;                              /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static bool g_use_naive;
/* 반례 (주어진 코드): 검사 없는 ISR — 덮어써진 데이터를 조용히 내보낸다 */
static void pipe_dma_irq_naive(pipe_t *p) {
    uint32_t lisr = REG_READ(DMA_LISR);
    if (!(lisr & (DMA_S0_HTIF | DMA_S0_TCIF))) return;
    uint32_t h = (lisr & DMA_S0_HTIF) ? 0u : 1u;
    REG_WRITE(DMA_LIFCR, h ? DMA_S0_TCIF : DMA_S0_HTIF);
    uint16_t mean, ema;
    process_half(p, h, &mean, &ema);
    out_push(h, 0u, mean, ema);
}
/* 반례 (주어진 코드): 플래그를 먼저, NDTR 을 나중에 읽는 검사 */
static bool reentered_flag_first(const pipe_t *p, uint32_t h) {
    uint32_t lisr = REG_READ(DMA_LISR);
    uint32_t ndtr = REG_READ(DMA_S0NDTR);
    uint32_t pos = 2u * p->n - ndtr;
    bool in_h = (pos / p->n == h) && (pos % p->n != 0u);
    return in_h || (lisr & (h ? DMA_S0_TCIF : DMA_S0_HTIF)) != 0u;
}
/* 시작 코드의 빈 TODO 때문에 "안 쓰인 함수" 경고가 나지 않게 (정답에서도 그대로 둔다) */
static void keep_alive(void) {
    (void)dma_arm; (void)pipe_resync; (void)nvic_enable_irq; (void)cpu_busy; (void)buf_read; (void)log_line;
}
static void DMA2_Stream0_IRQHandler(void) {
    if (g_use_naive) pipe_dma_irq_naive(&g_pipe); else pipe_dma_irq(&g_pipe);
}

static void sim_reset(uint32_t n, uint32_t s, uint32_t pre, uint32_t per_sample) {
    g_nregs = 0; g_unexpected = 0; g_cpu_acc = 0; g_hook_at = 0; g_hook_fn = NULL;
    memset(g_line, 0, sizeof g_line); memset(g_en, 0, sizeof g_en); memset(g_vec, 0, sizeof g_vec);
    g_in_isr = g_nvic_off = g_use_naive = false;
    g_clk = 0; g_t_stop = UINT64_MAX; g_block_from = g_block_until = 0; g_isr_calls = g_isr_storm = 0;
    memset(&g_adc, 0, sizeof g_adc); memset(&g_dma, 0, sizeof g_dma); memset(&g_snap, 0, sizeof g_snap);
    memset(g_sram16, 0, sizeof g_sram16); memset(g_slot_abs, 0xFF, sizeof g_slot_abs);
    g_rd_n = 0; g_nrec = 0; g_rec_lost = 0; g_n_truth = n; g_nlog = 0;
    g_cost = (cost_t){ pre, per_sample };
    memset(&g_pipe, 0, sizeof g_pipe);
    g_adc.period = s;
    mock_add_reg(ADC_SR, 0u, NULL, on_w_adc_sr);
    mock_add_reg(ADC_CR2, 0u, NULL, on_w_adc_cr2);
    mock_add_reg(ADC_DR, 0u, on_r_adc_dr, on_w_lisr);           /* DR: CPU 쓰기 무시 */
    mock_add_reg(DMA_LISR, 0u, NULL, on_w_lisr);
    mock_add_reg(DMA_LIFCR, 0u, NULL, on_w_lifcr);
    mock_add_reg(DMA_S0CR, 0u, on_r_s0cr, on_w_s0cr);
    mock_add_reg(DMA_S0NDTR, 0u, NULL, on_w_locked);
    mock_add_reg(DMA_S0PAR, 0u, NULL, on_w_locked);
    mock_add_reg(DMA_S0M0AR, 0u, NULL, on_w_locked);
    mock_add_reg(DMA_S0FCR, 0x21u, NULL, NULL);
    g_adc.sr = hw(ADC_SR); g_adc.cr2 = hw(ADC_CR2); g_adc.dr = hw(ADC_DR);
    g_dma.cr = hw(DMA_S0CR); g_dma.ndtr = hw(DMA_S0NDTR); g_dma.par = hw(DMA_S0PAR);
    g_dma.m0ar = hw(DMA_S0M0AR); g_dma.lisr = hw(DMA_LISR);
    g_vec[DMA2_Stream0_IRQn] = DMA2_Stream0_IRQHandler;
    keep_alive();
}
static void run_until(uint64_t t_end, uint32_t health_every) {
    uint64_t next = health_every;
    g_t_stop = t_end;
    while (g_clk < t_end) {
        sim_step();
        if (health_every && g_clk >= next) { pipe_health(&g_pipe); next = g_clk + health_every; }
    }
}

/* 레코드 검증: 정답(truth)=OVR, 찢김 ⇒ OVR, OVR 아니면 평균/EMA = 기준 필터, GAP 없이 연속 */
typedef struct { uint32_t bad_truth, torn_unflagged, bad_value, bad_cont, ovr, gap, torn; } verdict_t;
static verdict_t verify_records(uint32_t n) {
    verdict_t v; memset(&v, 0, sizeof v);
    int32_t y = 0; bool yv = false;
    for (uint32_t i = 0; i < g_nrec; i++) {
        const rec_t *r = &g_rec[i];
        bool ovr = (r->flags & REC_OVR) != 0u, gap = (r->flags & REC_GAP) != 0u;
        v.ovr += ovr; v.gap += gap; v.torn += r->torn != 0u;
        if (r->truth != ovr) v.bad_truth++;
        if (r->torn && !ovr) v.torn_unflagged++;
        if (i > 0 && !gap) {
            const rec_t *q = &g_rec[i - 1];
            if ((q->flags & REC_OVR) || r->base != q->base + n) v.bad_cont++;
        }
        if (gap || i == 0) yv = false;
        if (ovr) { yv = false; continue; }
        uint32_t sum = 0;
        for (uint32_t j = 0; j < n; j++) {
            uint32_t x = sig(r->base + j);
            sum += x;
            if (!yv) { y = (int32_t)(x << 8); yv = true; } else y += ((int32_t)(x << 8) - y) / 16;
        }
        if (r->mean != (sum + n / 2u) / n || r->ema != (uint16_t)((y + 128) >> 8)) v.bad_value++;
    }
    return v;
}
static bool verdict_clean(verdict_t v) { return !v.bad_truth && !v.torn_unflagged && !v.bad_value && !v.bad_cont; }

/* 예측: 지연 0 에서 시작, 비용 C 클럭, 반쪽 = N·S. 첫 OVR 레코드 번호 (없으면 -1) */
static int32_t predict_first_ovr(uint32_t n, uint32_t s, uint32_t c) {
    if (c <= n * s) return -1;
    uint32_t d = c - n * s;
    return (int32_t)((s + d - 1u) / d) - 1;      /* (i+1)·δ ≥ S 인 최소 i */
}

/* race 테스트용: DMA 전송 1개가 일어날 때까지 HW 만 돌린다 */
static void hw_one_transfer(void) { uint64_t t = g_dma.total; for (int g = 0; g < 1000 && g_dma.total == t; g++) hw_clock(); }

static void print_records(uint32_t from, uint32_t to) {
    printf("   #  h  flags     ready  late  end    base   k  truth torn  mean   ema\n");
    for (uint32_t i = from; i < to && i < g_nrec; i++) {
        const rec_t *r = &g_rec[i];
        printf("%4u  %u  %-8s %5llu  %4llu  %5llu  %4u  %2llu  %-5s  %2u  %5u  %5u\n", (unsigned)i, r->h,
               (r->flags & REC_OVR) ? ((r->flags & REC_GAP) ? "OVR|GAP" : "OVR") : ((r->flags & REC_GAP) ? "GAP" : "-"),
               (unsigned long long)r->ready, (unsigned long long)r->late, (unsigned long long)r->t, (unsigned)r->base,
               (unsigned long long)r->k, r->truth ? "yes" : "no", (unsigned)r->torn, r->mean, r->ema);
    }
}
static void verbose_dump(void) {
    printf("== 1) N=8 S=4 (half = 32 clk), cost = 1 + 8*4 = 33 clk (delta = 1) ==\n");
    sim_reset(8u, 4u, 1u, 4u);
    pipe_start(&g_pipe, 8u);
    run_until(32u * 14u + 1u, 0u);
    print_records(0u, g_nrec);
    for (uint32_t i = 0; i < g_nlog; i++) printf("%s\n", g_log[i]);
    printf("== 2) N=32 S=16, DDS forgotten (CR2.DDS=0), main loop health every 100 clk ==\n");
    sim_reset(32u, 16u, 0u, 4u);
    pipe_start(&g_pipe, 32u);
    *hw(ADC_CR2) &= ~ADC_CR2_DDS;
    run_until(32u * 16u * 8u + 1u, 100u);
    print_records(0u, g_nrec);
    for (uint32_t i = 0; i < g_nlog; i++) printf("%s\n", g_log[i]);
    printf("ADC ovr_count=%u, recoveries=%u, halves=%u\n", (unsigned)g_adc.ovr_count,
           (unsigned)g_pipe.adc_recoveries, (unsigned)g_pipe.halves);
    printf("== 3) N=8, half 0 just processed, k transfers since it filled, +1 transfer injected between the 2 reads ==\n");
    printf("  k  truth  NDTR-first  flag-first\n");
    for (uint32_t k = 6; k <= 17u; k++) {
        bool got[2];
        for (int order = 0; order < 2; order++) {
            sim_reset(8u, 4u, 0u, 0u);
            pipe_start(&g_pipe, 8u);
            g_nvic_off = true;
            for (int g = 0; g < 1000 && !(*hw(DMA_LISR) & DMA_S0_HTIF); g++) hw_clock();
            REG_WRITE(DMA_LIFCR, DMA_S0_HTIF | DMA_S0_TCIF);
            for (uint32_t t = 0; t < k; t++) hw_one_transfer();
            g_cpu_acc = 0; g_hook_at = 2u; g_hook_fn = hw_one_transfer;
            got[order] = order ? reentered_flag_first(&g_pipe, 0u) : pipe_reentered(&g_pipe, 0u);
        }
        bool truth = k >= 9u;
        printf(" %2u  %-5s  %-10s  %-10s%s\n", (unsigned)k, truth ? "OVR" : "ok", got[0] ? "OVR" : "ok",
               got[1] ? "OVR" : "ok", got[1] != truth ? (truth ? "  <- MISS" : "  <- false alarm") : "");
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) verbose_dump();

    /* ---- Part A: 설정 레지스터 ---- */
    sim_reset(32u, 16u, 0u, 1u);
    CHECK(pipe_start(&g_pipe, 32u) == 0);
    CHECK(*hw(DMA_S0CR) == (PIPE_CR_CFG | DMA_CR_EN));
    CHECK(((*hw(DMA_S0CR) >> 6) & 3u) == 0u && ((*hw(DMA_S0CR) >> 11) & 3u) == 1u && ((*hw(DMA_S0CR) >> 13) & 3u) == 1u);
    CHECK(*hw(DMA_S0NDTR) == 64u && *hw(DMA_S0PAR) == ADC_DR && *hw(DMA_S0M0AR) == ADC_BUF_ADDR);
    CHECK(*hw(ADC_CR2) == (ADC_CR2_ADON | ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_DDS));
    CHECK(g_en[DMA2_Stream0_IRQn] && g_adc.running && g_unexpected == 0u);
    CHECK(pipe_start(&g_pipe, 1u) == -1 && pipe_start(&g_pipe, NMAX + 1u) == -1);

    /* 남은 상태 위에서 재시작: EN=1 + 지난 실행의 TCIF/HTIF → 끄고 기다리고 지운 뒤 켜야 가짜 첫 IRQ 가 없다 */
    sim_reset(16u, 8u, 0u, 1u);
    *hw(DMA_S0NDTR) = 10u; *hw(DMA_S0CR) = PIPE_CR_CFG | DMA_CR_EN; g_dma.reload = 10u;
    *hw(DMA_LISR) = DMA_S0_TCIF | DMA_S0_HTIF | DMA_S0_FEIF;
    CHECK(pipe_start(&g_pipe, 16u) == 0);
    CHECK(*hw(DMA_LISR) == 0u && *hw(DMA_S0NDTR) == 32u && g_dma.reload == 32u);
    run_until(16u * 8u * 4u + 1u, 0u);                              /* 4 반쪽 */
    CHECK(g_nrec == 4u && g_rec[0].h == 0u && g_rec[0].base == 0u && g_rec[0].t == 16u * 8u + 16u);

    /* ---- Part A: 긴 스트림 = 기준 필터 (예산 안) ---- */
    sim_reset(32u, 16u, 31u, 15u);                                  /* 비용 31 + 32·15 = 511 < 512 */
    pipe_start(&g_pipe, 32u);
    run_until(32u * 16u * 400u + 1u, 0u);
    {
        verdict_t v = verify_records(32u);
        CHECK(g_nrec == 400u && g_rec_lost == 0u && g_rec[399].base == 399u * 32u);
        CHECK(verdict_clean(v) && v.ovr == 0u && v.gap == 0u && v.torn == 0u);
        CHECK(g_pipe.halves == 400u && g_pipe.overruns == 0u && g_isr_storm == 0u && g_adc.ovr_count == 0u);
        CHECK(g_rec[0].mean == 1143u && g_rec[0].ema == 1153u && g_rec[1].mean == 1452u);
    }
    {   /* 여러 N, S 에서 예산 안이면 모든 레코드가 기준과 같다 */
        static const uint32_t NS[] = { 2u, 3u, 7u, 32u, 256u }, SS[] = { 1u, 3u, 16u };
        uint32_t bad = 0, recs = 0;
        for (int a = 0; a < 5; a++) for (int b = 0; b < 3; b++) {
            uint32_t n = NS[a], s = SS[b];
            sim_reset(n, s, 0u, s - 1u);                         /* 비용 N·(S−1) < N·S */
            pipe_start(&g_pipe, n);
            run_until((uint64_t)n * s * 60u + 1u, 0u);
            verdict_t v = verify_records(n);
            recs += g_nrec;
            if (!verdict_clean(v) || v.ovr || v.gap || g_nrec != 60u) bad++;
        }
        CHECK(bad == 0u && recs == 900u);
    }

    /* ---- Part B: 예산 바로 아래 / 같음 / 바로 위 → 예측한 레코드에서 정확히 OVR ---- */
    {
        static const struct { uint32_t pre, cps; int32_t first; } C[] = {
            { 31u, 15u, -1 },   /* 511: 아래 */
            {  0u, 16u, -1 },   /* 512: 반쪽 주기와 같음 — 여유 0 이지만 넘치지 않는다 */
            {  1u, 16u, 15 },   /* 513: δ=1 → 16번째 반쪽에서 */
            {  4u, 16u,  3 },   /* 516: δ=4 */
            { 15u, 16u,  1 },   /* 527: δ=15 */
            { 16u, 16u,  0 },   /* 528 = (N+1)·S: 첫 반쪽부터 */
            {600u,  0u,  0 },   /* 600: 처리 시작 전에 이미 덮어씀 → 데이터가 진짜 찢김 */
        };
        uint32_t bad = 0;
        for (int i = 0; i < 7; i++) {
            sim_reset(32u, 16u, C[i].pre, C[i].cps);
            pipe_start(&g_pipe, 32u);
            run_until(32u * 16u * 340u + 1u, 0u);
            verdict_t v = verify_records(32u);
            int32_t first = -1;
            for (uint32_t r = 0; r < g_nrec; r++) if (g_rec[r].flags & REC_OVR) { first = (int32_t)r; break; }
            int32_t pred = predict_first_ovr(32u, 16u, C[i].pre + 32u * C[i].cps);
            if (!verdict_clean(v) || first != C[i].first || pred != C[i].first) {
                bad++; printf("  case %d: first=%d pred=%d expect=%d\n", i, first, pred, C[i].first);
            }
            if (first >= 0 && (uint32_t)first + 1u < g_nrec) {          /* OVR 다음: 한 반쪽 건너뛰고 GAP */
                const rec_t *o = &g_rec[first], *nx = &g_rec[first + 1];
                if (o->base != (uint32_t)first * 32u || !(nx->flags & REC_GAP) || nx->base != o->base + 64u) bad++;
            }
            if (i == 2 && (v.torn != 0u || v.ovr != 20u)) bad++;        /* 17 반쪽 주기, 덮였지만 찢기지는 않음 */
            if (i == 6 && (v.torn == 0u || v.torn != v.ovr)) bad++;     /* 진짜 찢김 = 모두 OVR 로 잡힘 */
        }
        CHECK(bad == 0u);
    }
    {   /* 반례: 검사 없는 ISR 은 찢긴 반쪽을 조용히 내보낸다 */
        sim_reset(32u, 16u, 600u, 0u);
        pipe_start(&g_pipe, 32u);
        g_use_naive = true;
        run_until(32u * 16u * 40u + 1u, 0u);
        verdict_t v = verify_records(32u);
        CHECK(v.torn > 0u && v.torn_unflagged == v.torn && v.bad_value > 0u && v.ovr == 0u);
    }

    /* ---- Part B: ISR 이 늦게 들어옴 (더 높은 우선순위 ISR / 긴 임계 구역 주입) ---- */
    {
        sim_reset(16u, 8u, 0u, 2u);                                 /* 반쪽 128 클럭, 비용 32 */
        pipe_start(&g_pipe, 16u);
        g_block_from = 1000u; g_block_until = 1300u;                /* 300 클럭 막힘 > 반쪽 → HT+TC 둘 다 */
        run_until(128u * 40u + 1u, 0u);
        verdict_t v = verify_records(16u);
        CHECK(verdict_clean(v) && g_pipe.late_entries == 1u && v.gap == 1u && v.ovr == 0u);
        sim_reset(16u, 8u, 0u, 2u);
        pipe_start(&g_pipe, 16u);
        g_block_from = 1024u; g_block_until = 1124u;                /* 반쪽 7 (1024 에 참) 을 100 클럭 늦게: 100+32 < 17·8 */
        run_until(128u * 40u + 1u, 0u);
        v = verify_records(16u);
        CHECK(verdict_clean(v) && g_pipe.late_entries == 0u && v.ovr == 0u && v.gap == 0u && g_nrec == 40u);
        sim_reset(16u, 8u, 0u, 2u);
        pipe_start(&g_pipe, 16u);
        g_block_from = 1024u; g_block_until = 1130u;                /* 106 + 32 = 138 ≥ 17·8 → 그 반쪽은 OVR */
        run_until(128u * 40u + 1u, 0u);
        v = verify_records(16u);
        CHECK(verdict_clean(v) && g_pipe.late_entries == 0u && v.ovr == 1u && v.gap == 1u);
    }

    /* ---- Part B: 무작위 속성 테스트 — 오검출 0, 미검출 0, 값 = 기준, 조용한 빈칸 0 ---- */
    {
        uint32_t bad = 0, recs = 0, ovrs = 0, torns = 0;
        for (int it = 0; it < 500; it++) {
            uint32_t n = 2u + xs32() % 63u, s = 1u + xs32() % 24u;
            uint32_t half = n * s, c = (uint32_t)((uint64_t)half * (xs32() % 260u) / 100u);
            uint32_t cps = (xs32() & 1u) ? c / n : 0u, pre = c - cps * n;
            sim_reset(n, s, pre, cps);
            pipe_start(&g_pipe, n);
            if (xs32() % 3u == 0u) { g_block_from = xs32() % (half * 8u); g_block_until = g_block_from + xs32() % (half * 3u); }
            run_until((uint64_t)half * 30u, 0u);
            verdict_t v = verify_records(n);
            recs += g_nrec; ovrs += v.ovr; torns += v.torn;
            if (!verdict_clean(v) || g_unexpected || g_rec_lost || g_isr_storm || g_pipe.overruns != v.ovr) bad++;
        }
        CHECK(bad == 0u);
        CHECK(recs > 5000u && ovrs > 500u && torns > 100u);       /* 테스트가 실제로 경우들을 밟았다 */
        if (argc > 1) printf("random: %u records, %u OVR, %u torn\n", (unsigned)recs, (unsigned)ovrs, (unsigned)torns);
    }

    /* ---- Part B: NDTR 와 플래그 읽기 사이에 DMA 전송 1개 끼우기 (모든 k, 두 반쪽) ---- */
    {
        const uint32_t n = 8u;
        uint32_t bad_ok = 0, miss_bad = 0, fp_bad = 0, noinj_bad = 0;
        for (uint32_t h = 0; h < 2u; h++) for (uint32_t k = 0; k <= 3u * n; k++) for (int inj = 0; inj < 2; inj++) for (int order = 0; order < 2; order++) {
            sim_reset(n, 4u, 0u, 0u);
            pipe_start(&g_pipe, n);
            g_nvic_off = true;                                       /* ISR 대신 테스트가 직접 */
            uint32_t self = h ? DMA_S0_TCIF : DMA_S0_HTIF;
            for (int g = 0; g < 1000 && !(*hw(DMA_LISR) & self); g++) hw_clock();
            REG_WRITE(DMA_LIFCR, DMA_S0_HTIF | DMA_S0_TCIF);         /* ISR 진입 때 지운 것처럼 */
            for (uint32_t t = 0; t < k; t++) hw_one_transfer();
            if (inj) { g_cpu_acc = 0; g_hook_at = 2u; g_hook_fn = hw_one_transfer; }
            bool got = order ? reentered_flag_first(&g_pipe, h) : pipe_reentered(&g_pipe, h);
            bool truth = k >= n + 1u;
            if (!inj && got != truth) noinj_bad++;
            if (inj && order == 0 && got != truth) bad_ok++;
            if (inj && order == 1 && !got && truth && k != 2u * n - 1u) miss_bad++;   /* 놓침은 k=2N−1 에서만 */
            if (inj && order == 1 && got != truth && k != 2u * n - 1u && k != n) fp_bad++;
            if (inj && order == 1 && k == 2u * n - 1u && got) miss_bad++;            /* 반드시 놓친다 */
        }
        CHECK(noinj_bad == 0u);
        CHECK(bad_ok == 0u);
        CHECK(miss_bad == 0u && fp_bad == 0u);
    }

    /* ---- Part C: 메인 루프 헬스 체크 — DDS=0 설정 버그 / CIRC 빠짐 → ADC OVR → 복구 ---- */
    for (int fault = 0; fault < 2; fault++) {
        sim_reset(32u, 16u, 0u, 4u);
        pipe_start(&g_pipe, 32u);
        if (fault == 0) *hw(ADC_CR2) &= ~ADC_CR2_DDS;                /* "DMAContinuousRequests 를 깜빡" */
        else            *hw(DMA_S0CR) &= ~DMA_CR_CIRC;               /* "circular 를 깜빡" */
        run_until(32u * 16u * 20u + 1u, 100u);                       /* 메인 루프가 100 클럭마다 health */
        verdict_t v = verify_records(32u);
        uint32_t gaps = 0, after = 0;
        for (uint32_t i = 0; i < g_nrec; i++) { if (g_rec[i].flags & REC_GAP) gaps++; else if (gaps) after++; }
        CHECK(g_pipe.adc_recoveries == 1u && gaps == 1u && after >= 10u);
        CHECK(verdict_clean(v) && v.ovr == 0u && g_rec[0].base == 0u && g_rec[1].base == 32u);
        CHECK(!(*hw(ADC_SR) & ADC_SR_OVR) && (*hw(ADC_CR2) & ADC_CR2_DDS) && (*hw(DMA_S0CR) & DMA_CR_CIRC));
    }
    {   /* 정상 설정이면 health 는 아무것도 안 한다 */
        sim_reset(32u, 16u, 0u, 4u);
        pipe_start(&g_pipe, 32u);
        run_until(32u * 16u * 20u + 1u, 100u);
        CHECK(g_pipe.adc_recoveries == 0u && g_nrec == 20u && verdict_clean(verify_records(32u)));
    }

    /* ---- Part C: 예산 계산 ---- */
    {
        budget_out_t o; memset(&o, 0, sizeof o);
        budget_in_t a = { 48000u, 256u, 40u, 600u, 168000000u };
        CHECK(budget_calc(&a, &o) == 0 && o.half_ns == 5333333u && o.proc_ns == 64524u);
        CHECK(o.load_permille == 12u && o.latency_ns == 5312500u + 64524u && o.ok);
        budget_in_t b = { 1000000u, 64u, 200u, 600u, 168000000u };  /* 1 MSPS, 200 cyc/샘플 */
        CHECK(budget_calc(&b, &o) == 0 && o.half_ns == 64000u && o.proc_ns == 79762u && o.load_permille == 1246u && !o.ok);
        budget_in_t c = { 1000u, 4u, 1000u, 0u, 1000000u };         /* 정확히 100 % → 경계는 ok */
        CHECK(budget_calc(&c, &o) == 0 && o.load_permille == 1000u && o.ok);
        budget_in_t d = { 0u, 4u, 1u, 0u, 1u };
        CHECK(budget_calc(&d, &o) == -1 && budget_calc(NULL, &o) == -1);
    }

    printf("%u/%u passed\n", (unsigned)g_pass, (unsigned)g_total);
    return g_pass == g_total ? 0 : 1;
}
