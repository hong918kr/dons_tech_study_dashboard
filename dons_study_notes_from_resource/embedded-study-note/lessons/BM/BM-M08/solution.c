/* BM-M08 DMA 엔진 드라이버: 채널 설정, circular, HT/TC 인터럽트
 *
 * Part A — 레지스터 계산: 스트림별 플래그 위치(LISR/HISR 안의 0/6/16/22 불규칙 오프셋), SxCR 값 조립,
 *          메모리→메모리 복사 (byte / halfword / word), HT·TC 위치, 고정 소스(PINC=0) 채우기.
 * Part B — 면접관이 찌르는 함정: EN=1 인 채 재설정(거부), EN 이 0 으로 읽힐 때까지 기다리기(abort),
 *          남은 TCIF (설정 전에 그 스트림 플래그를 전부 지우기), TEIF(버스 에러), 정렬 안 된 주소,
 *          ISR 플래그 클리어 방식 ("본 것만, 이 스트림 것만").
 * Part C — 펌웨어처럼: USART1 TX DMA (DMA TC ≠ USART TC), RX circular + HT/TC 콜백 수천 바이트,
 *          CHSEL 실수 진단, 기동 엔지니어가 읽는 로그 (-v).
 *
 * 주소·비트: STM32F4 RM0090 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 * 호스트 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (-v: 기동 로그)
 * 타깃 빌드  : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -ffreestanding -DTARGET_BUILD -c solution.c
 */
#ifndef TARGET_BUILD
#define UNIT_TEST 1                       /* 호스트 테스트 빌드 */
#endif

#include <stdbool.h>
#include <stdint.h>

/* ======================= 이음매(seam) (BM-B02 mock 축약판) ======================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* ======================= 레지스터 맵 (RM0090 기준 대표값 — RM 확인 권장) ======================= */
#define DMA1_BASE        0x40026000u
#define DMA2_BASE        0x40026400u
#define DMA_LISR         0x00u            /* r : 스트림 0..3 플래그 */
#define DMA_HISR         0x04u            /* r : 스트림 4..7 플래그 */
#define DMA_LIFCR        0x08u            /* w : 1 을 쓴 비트의 LISR 플래그를 지운다 */
#define DMA_HIFCR        0x0Cu
#define DMA_SxCR(s)      (0x10u + 0x18u * (s))
#define DMA_SxNDTR(s)    (0x14u + 0x18u * (s))
#define DMA_SxPAR(s)     (0x18u + 0x18u * (s))
#define DMA_SxM0AR(s)    (0x1Cu + 0x18u * (s))
#define DMA_SxM1AR(s)    (0x20u + 0x18u * (s))
#define DMA_SxFCR(s)     (0x24u + 0x18u * (s))

#define DMA_CR_EN        (1u << 0)
#define DMA_CR_DMEIE     (1u << 1)
#define DMA_CR_TEIE      (1u << 2)
#define DMA_CR_HTIE      (1u << 3)
#define DMA_CR_TCIE      (1u << 4)
#define DMA_CR_DIR_Pos   6                /* 00 P→M, 01 M→P, 10 M→M */
#define DMA_CR_CIRC      (1u << 8)
#define DMA_CR_PINC      (1u << 9)
#define DMA_CR_MINC      (1u << 10)
#define DMA_CR_PSIZE_Pos 11               /* 00 byte, 01 halfword, 10 word */
#define DMA_CR_MSIZE_Pos 13
#define DMA_CR_PL_Pos    16               /* 00 low .. 11 very high */
#define DMA_CR_CHSEL_Pos 25
#define DMA_CR_IE_MASK   (DMA_CR_DMEIE | DMA_CR_TEIE | DMA_CR_HTIE | DMA_CR_TCIE)

/* 스트림마다 6비트 플래그 그룹. 그룹 안 비트 번호 (비트 1 은 예약) */
#define DMA_FEIF         0u
#define DMA_DMEIF        2u
#define DMA_TEIF         3u
#define DMA_HTIF         4u
#define DMA_TCIF         5u
#define DMA_GROUP_MASK   0x3Du            /* TC HT TE DME - FE = 0b111101 */
#define DMA_ERR_MASK     ((1u << DMA_FEIF) | (1u << DMA_DMEIF) | (1u << DMA_TEIF))
#define DMA_EN_POLL_MAX  64u              /* EN 이 0 으로 읽힐 때까지 폴링 상한 */

#define USART1_BASE      0x40011000u
#define USART_SR         0x00u            /* ORE 3 (r), RXNE 5 (rc_w0), TC 6 (rc_w0), TXE 7 (r) */
#define USART_DR         0x04u
#define USART_CR3        0x14u
#define USART_SR_ORE     (1u << 3)
#define USART_SR_RXNE    (1u << 5)
#define USART_SR_TC      (1u << 6)
#define USART_SR_TXE     (1u << 7)
#define USART_CR3_DMAR   (1u << 6)
#define USART_CR3_DMAT   (1u << 7)
#define UART1_TX_STREAM  7u               /* DMA2 요청 매핑 (RM0090 DMA2 request mapping 표 — 확인 권장) */
#define UART1_RX_STREAM  2u               /* USART1_RX 는 스트림 2 또는 5, 채널 4 */
#define UART1_DMA_CH     4u

typedef enum { DMA_OK = 0, DMA_EINVAL = -1, DMA_EBUSY = -2, DMA_ETIMEOUT = -3 } dma_err_t;
enum { DMA_DIR_P2M = 0, DMA_DIR_M2P = 1, DMA_DIR_M2M = 2 };
enum { DMA_SZ_BYTE = 0, DMA_SZ_HALF = 1, DMA_SZ_WORD = 2 };

typedef struct {
    uint8_t  stream, channel, dir, size, prio;   /* size: PSIZE = MSIZE (direct mode, FIFO 안 씀) */
    bool     pinc, minc, circ;
    bool     htie, tcie, teie;
    uint32_t par, m0ar;                           /* 버스 주소. M→M 이면 PAR = 소스, M0AR = 목적지 */
    uint16_t ndtr;                                /* 데이터 항목 수 (바이트 수가 아니다) */
} dma_cfg_t;

typedef struct {
    void (*half)(void *ctx);
    void (*full)(void *ctx);
    void (*error)(void *ctx, uint32_t flags);     /* flags = 그 스트림 그룹 비트 (TEIF 등) */
    void *ctx;
} dma_cb_t;

/* ======================= Part A ======================= */

/* TODO(A1): 스트림 s(0..7) 의 플래그 f(그룹 안 비트 0/2/3/4/5) 는 LISR/HISR 의 몇 번 비트인가.
 *           예: (0, TCIF) → 5, (1, TCIF) → 11, (2, TCIF) → 21, (7, TCIF) → 27 */
unsigned dma_flag_bit(unsigned s, unsigned f) {
    static const unsigned group_off[4] = { 0u, 6u, 16u, 22u };   /* 규칙적이지 않다: 표로 */
    return group_off[s & 3u] + f;
}
/* TODO(A1): 그 스트림의 상태 레지스터(LISR/HISR) / 클리어 레지스터(LIFCR/HIFCR) 오프셋 */
uint32_t dma_isr_reg(unsigned s) {
    return s < 4u ? DMA_LISR : DMA_HISR;
}
uint32_t dma_ifcr_reg(unsigned s) {
    return s < 4u ? DMA_LIFCR : DMA_HIFCR;
}

/* TODO(A2): 설정 → SxCR 값 (EN 은 켜지 않는다). PSIZE = MSIZE = size. */
uint32_t dma_cr_value(const dma_cfg_t *c) {
    uint32_t v = 0u;
    v |= (uint32_t)c->channel << DMA_CR_CHSEL_Pos;
    v |= (uint32_t)c->prio    << DMA_CR_PL_Pos;
    v |= (uint32_t)c->size    << DMA_CR_MSIZE_Pos;
    v |= (uint32_t)c->size    << DMA_CR_PSIZE_Pos;
    v |= (uint32_t)c->dir     << DMA_CR_DIR_Pos;
    if (c->minc) v |= DMA_CR_MINC;
    if (c->pinc) v |= DMA_CR_PINC;
    if (c->circ) v |= DMA_CR_CIRC;
    if (c->tcie) v |= DMA_CR_TCIE;
    if (c->htie) v |= DMA_CR_HTIE;
    if (c->teie) v |= DMA_CR_TEIE;
    return v;
}

/* ======================= Part B ======================= */

/* TODO(B1): 스트림을 멈춘다. EN=1 이면 EN 과 IE 비트를 0 으로 쓰고, EN 이 0 으로 "읽힐" 때까지
 *           최대 DMA_EN_POLL_MAX 번 폴링. 반환 DMA_OK / DMA_ETIMEOUT / DMA_EINVAL(s > 7) */
int dma_stream_disable(uint32_t base, unsigned s) {
    if (s > 7u) return DMA_EINVAL;
    uint32_t cr = REG_READ(base + DMA_SxCR(s));
    if (!(cr & DMA_CR_EN)) return DMA_OK;
    REG_WRITE(base + DMA_SxCR(s), cr & ~(DMA_CR_EN | DMA_CR_IE_MASK));   /* IE 도 꺼서 abort 의 TCIF 가 콜백으로 안 가게 */
    for (unsigned i = 0; i < DMA_EN_POLL_MAX; i++)
        if (!(REG_READ(base + DMA_SxCR(s)) & DMA_CR_EN)) return DMA_OK;  /* 진행 중이던 전송이 끝나야 0 */
    return DMA_ETIMEOUT;
}

/* TODO(B2): 검증 → EN=1 이면 DMA_EBUSY (아무것도 쓰지 않는다) → 그 스트림의 플래그 5개를 IFCR 로 지움 →
 *           PAR, M0AR, NDTR, CR(EN=0) 순서로 쓴다. 거부(DMA_EINVAL): stream/channel > 7, dir/size/prio 범위 밖,
 *           ndtr == 0, par·m0ar 가 항목 크기로 정렬 안 됨, M→M 인데 circ 이거나 DMA2 가 아님, base 가 DMA1/2 가 아님. */
int dma_stream_config(uint32_t base, const dma_cfg_t *c) {
    if (base != DMA1_BASE && base != DMA2_BASE) return DMA_EINVAL;
    if (c->stream > 7u || c->channel > 7u || c->dir > DMA_DIR_M2M || c->size > DMA_SZ_WORD || c->prio > 3u)
        return DMA_EINVAL;
    if (c->ndtr == 0u) return DMA_EINVAL;                          /* NDTR=0 이면 HW 는 아무것도 안 한다 */
    uint32_t align = (1u << c->size) - 1u;
    if ((c->par & align) || (c->m0ar & align)) return DMA_EINVAL;  /* HW 는 하위 비트를 조용히 버린다 */
    if (c->dir == DMA_DIR_M2M && (c->circ || base != DMA2_BASE)) return DMA_EINVAL;
    unsigned s = c->stream;
    if (REG_READ(base + DMA_SxCR(s)) & DMA_CR_EN) return DMA_EBUSY;
    REG_WRITE(base + dma_ifcr_reg(s), DMA_GROUP_MASK << dma_flag_bit(s, 0u));   /* 지난 전송의 TCIF 등 */
    REG_WRITE(base + DMA_SxPAR(s), c->par);
    REG_WRITE(base + DMA_SxM0AR(s), c->m0ar);
    REG_WRITE(base + DMA_SxNDTR(s), c->ndtr);
    REG_WRITE(base + DMA_SxCR(s), dma_cr_value(c));
    return DMA_OK;
}

/* 주어짐: EN 을 켠다 (설정은 dma_stream_config 가 이미 했다) */
void dma_stream_start(uint32_t base, unsigned s) {
    REG_WRITE(base + DMA_SxCR(s), REG_READ(base + DMA_SxCR(s)) | DMA_CR_EN);
}

static dma_cb_t g_dma_cb[2][8];
static dma_cb_t *dma_cb_slot(uint32_t base, unsigned s) { return &g_dma_cb[base == DMA2_BASE][s & 7u]; }
void dma_set_callbacks(uint32_t base, unsigned s, const dma_cb_t *cb) { *dma_cb_slot(base, s) = *cb; }

/* TODO(B3): 스트림 IRQ 핸들러. CR 의 IE 비트로 켜진 플래그만 보고(DMEIE..TCIE = CR 비트 1..4 ↔ 그룹 비트 2..5),
 *           "본 것만, 이 스트림 것만" IFCR 로 지운 뒤 콜백: 에러(TE/DME/FE) → error(flags) 후 끝,
 *           아니면 HT → half, TC → full (둘 다면 half 먼저). */
void dma_irq_handler(uint32_t base, unsigned s) {
    unsigned off = dma_flag_bit(s, 0u);
    uint32_t ie_flags = (REG_READ(base + DMA_SxCR(s)) & DMA_CR_IE_MASK) << 1;
    uint32_t seen = (REG_READ(base + dma_isr_reg(s)) >> off) & DMA_GROUP_MASK & ie_flags;
    if (seen == 0u) return;
    REG_WRITE(base + dma_ifcr_reg(s), seen << off);        /* 콜백 전에, 본 비트만 */
    const dma_cb_t *cb = dma_cb_slot(base, s);
    if (seen & DMA_ERR_MASK) {
        if (cb->error) cb->error(cb->ctx, seen);
        return;
    }
    if ((seen & (1u << DMA_HTIF)) && cb->half) cb->half(cb->ctx);
    if ((seen & (1u << DMA_TCIF)) && cb->full) cb->full(cb->ctx);
}

/* 벡터 테이블에 들어갈 이름 (IRQn: DMA2_Stream0..4 = 56..60, 5..7 = 68..70 — RM0090 벡터 표, 확인 권장) */
#define DMA2_HANDLER(n) void DMA2_Stream##n##_IRQHandler(void) { dma_irq_handler(DMA2_BASE, n##u); }
DMA2_HANDLER(0) DMA2_HANDLER(1) DMA2_HANDLER(2) DMA2_HANDLER(3)
DMA2_HANDLER(4) DMA2_HANDLER(5) DMA2_HANDLER(6) DMA2_HANDLER(7)

/* ======================= Part C ======================= */

/* TODO(C1): USART1 TX 를 DMA2 스트림 7 / 채널 4 로 n 바이트 보낸다: M→P, byte, MINC, prio 1, TCIE+TEIE,
 *           PAR = USART1 DR. 설정 → 스트림 시작 → USART SR.TC 를 0 으로 (rc_w0: ~TC 쓰기) → CR3.DMAT 켬.
 *           dma_stream_config 의 에러를 그대로 돌려준다 (전송 중이면 DMA_EBUSY). */
int uart1_tx_dma_start(uint32_t buf, uint16_t n) {
    dma_cfg_t c = { .stream = UART1_TX_STREAM, .channel = UART1_DMA_CH, .dir = DMA_DIR_M2P,
                    .size = DMA_SZ_BYTE, .prio = 1u, .minc = true, .tcie = true, .teie = true,
                    .par = USART1_BASE + USART_DR, .m0ar = buf, .ndtr = n };
    int r = dma_stream_config(DMA2_BASE, &c);
    if (r != DMA_OK) return r;
    dma_stream_start(DMA2_BASE, UART1_TX_STREAM);
    REG_WRITE(USART1_BASE + USART_SR, ~USART_SR_TC);
    REG_WRITE(USART1_BASE + USART_CR3, REG_READ(USART1_BASE + USART_CR3) | USART_CR3_DMAT);
    return DMA_OK;
}

/* TODO(C2): USART1 RX 를 DMA2 스트림 2 / 채널 4 로 circular 수신: P→M, byte, MINC, CIRC, prio 2,
 *           HTIE+TCIE+TEIE, PAR = USART1 DR. 설정 → 시작 → CR3.DMAR 켬. */
int uart1_rx_dma_start(uint32_t buf, uint16_t n) {
    dma_cfg_t c = { .stream = UART1_RX_STREAM, .channel = UART1_DMA_CH, .dir = DMA_DIR_P2M,
                    .size = DMA_SZ_BYTE, .prio = 2u, .minc = true, .circ = true,
                    .htie = true, .tcie = true, .teie = true,
                    .par = USART1_BASE + USART_DR, .m0ar = buf, .ndtr = n };
    int r = dma_stream_config(DMA2_BASE, &c);
    if (r != DMA_OK) return r;
    dma_stream_start(DMA2_BASE, UART1_RX_STREAM);
    REG_WRITE(USART1_BASE + USART_CR3, REG_READ(USART1_BASE + USART_CR3) | USART_CR3_DMAR);
    return DMA_OK;
}

#ifdef UNIT_TEST
#include <stdio.h>
#include <string.h>

/* ======================= mock 코어 (BM-B02 mock 축약판: 레지스터 대신 블록 단위 훅) ======================= */
typedef uint32_t (*blk_rd_fn)(uint32_t off);
typedef void     (*blk_wr_fn)(uint32_t off, uint32_t val);
typedef struct { uint32_t base, size; blk_rd_fn rd; blk_wr_fn wr; } blk_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
#define MOCK_LOG_CAP 256
static struct {
    mock_log_t log[MOCK_LOG_CAP];
    unsigned   nlog, unexpected;
    bool       logging;
    uint64_t   now;                       /* 모델 시계: CPU 버스 접근 1회 = 1 tick, sim_step 1회 = 1 tick */
} MK;

static uint32_t dma_rd(uint32_t off);
static void     dma_wr(uint32_t off, uint32_t v);
static uint32_t uart_rd(uint32_t off);
static void     uart_wr(uint32_t off, uint32_t v);
static const blk_t BLK[] = {              /* DMA1 은 모델에 없다 → 접근하면 unexpected */
    { DMA2_BASE,   0xD0u, dma_rd,  dma_wr  },
    { USART1_BASE, 0x1Cu, uart_rd, uart_wr },
};
static void sim_tick(void);

static const blk_t *blk_find(uint32_t a) {
    for (unsigned i = 0; i < sizeof BLK / sizeof BLK[0]; i++)
        if (a >= BLK[i].base && a - BLK[i].base < BLK[i].size) return &BLK[i];
    return NULL;
}
static void mock_log_push(char op, uint32_t a, uint32_t v) {
    if (MK.logging && MK.nlog < MOCK_LOG_CAP) MK.log[MK.nlog++] = (mock_log_t){ op, a, v };
}
uint32_t mock_read32(uint32_t a) {
    const blk_t *b = blk_find(a);
    uint32_t v = 0u;
    if (!b || (a & 3u)) MK.unexpected++;
    else v = b->rd(a - b->base);
    mock_log_push('R', a, v);
    sim_tick();
    return v;
}
void mock_write32(uint32_t a, uint32_t v) {
    const blk_t *b = blk_find(a);
    if (!b || (a & 3u)) MK.unexpected++;
    else b->wr(a - b->base, v);
    mock_log_push('W', a, v);
    sim_tick();
}
static unsigned log_count(char op, uint32_t a) {
    unsigned c = 0;
    for (unsigned i = 0; i < MK.nlog; i++) c += (MK.log[i].op == op && MK.log[i].addr == a);
    return c;
}
static void log_start(void) { MK.nlog = 0; MK.logging = true; }

/* ---- SRAM (DMA 가 보는 버스 주소 0x2000_0000) ---- */
#define SRAM_BASE 0x20000000u
#define SRAM_SIZE 0x8000u
static uint8_t sram[SRAM_SIZE];
#define SA(off)   (SRAM_BASE + (uint32_t)(off))

/* ======================= NVIC-lite ======================= */
#define NIRQ 96
typedef void (*isr_t)(void);
static struct { bool level[NIRQ], en[NIRQ]; isr_t h[NIRQ]; unsigned entries[NIRQ]; bool primask; } NV;
static void irq_line_set(unsigned irq, bool level) { NV.level[irq] = level; }
static void sim_step(void) {                          /* 1 tick + 켜진·높은 라인 중 번호 낮은 것 하나 실행 */
    sim_tick();
    if (NV.primask) return;                           /* 메인이 임계 구역 중: IRQ 는 pending 으로 기다린다 */
    for (unsigned i = 0; i < NIRQ; i++)
        if (NV.level[i] && NV.en[i] && NV.h[i]) { NV.entries[i]++; NV.h[i](); break; }
}
static void sim_burn(unsigned k) { while (k--) sim_tick(); }   /* ISR/콜백 안의 CPU 일 = 시간만 흐름 */

/* ======================= USART1 모델 (BM-B12 모델 축약판: TDR + 시프트 레지스터) ======================= */
#define UART_BYTE_TICKS 10u
static struct {
    uint32_t sr, cr3;
    uint8_t  tdr, rdr, shift;
    bool     tdr_full;
    unsigned shift_left;
    uint8_t  txcap[1024];
    unsigned ntx, ore;
    const uint8_t *rx_src;                 /* RX 주입: rx_period tick 마다 한 바이트 */
    unsigned rx_n, rx_i, rx_period, rx_phase;
} U;
static void uart_tx_write(uint8_t b) {
    if (U.shift_left == 0u) { U.shift = b; U.shift_left = UART_BYTE_TICKS; }
    else { U.tdr = b; U.tdr_full = true; U.sr &= ~USART_SR_TXE; }
    U.sr &= ~USART_SR_TC;
}
static uint32_t uart_rd(uint32_t off) {
    if (off == USART_SR) return U.sr;
    if (off == USART_DR) { U.sr &= ~USART_SR_RXNE; return U.rdr; }
    if (off == USART_CR3) return U.cr3;
    return 0u;
}
static void uart_wr(uint32_t off, uint32_t v) {
    if (off == USART_SR) U.sr &= (v | ~(USART_SR_TC | USART_SR_RXNE));   /* rc_w0 */
    else if (off == USART_DR) uart_tx_write((uint8_t)v);
    else if (off == USART_CR3) U.cr3 = v;
}
static void uart_tick(void) {
    if (U.shift_left && --U.shift_left == 0u) {
        if (U.ntx < sizeof U.txcap) U.txcap[U.ntx] = U.shift;
        U.ntx++;
        if (U.tdr_full) { U.shift = U.tdr; U.tdr_full = false; U.shift_left = UART_BYTE_TICKS; U.sr |= USART_SR_TXE; }
        else U.sr |= USART_SR_TC;
    }
    if (U.rx_src && U.rx_i < U.rx_n && ++U.rx_phase >= U.rx_period) {
        U.rx_phase = 0u;
        uint8_t b = U.rx_src[U.rx_i++];
        if (U.sr & USART_SR_RXNE) { U.ore++; U.sr |= USART_SR_ORE; }      /* 새 바이트 유실, RDR 은 그대로 */
        else { U.rdr = b; U.sr |= USART_SR_RXNE; }
    }
}

/* ======================= DMA2 모델 (RM0090 레이아웃, 동작은 이 모델의 정의) ======================= */
#define DMA_DISABLE_LAT 3u                 /* SW 가 EN=0 을 쓴 뒤 EN 이 1 로 남는 tick (진행 중 전송 마무리) */
static const unsigned GOFF[4] = { 0u, 6u, 16u, 22u };                  /* 모델 자신의 표 */
static const unsigned DMA2_IRQN[8] = { 56u, 57u, 58u, 59u, 60u, 68u, 69u, 70u };
typedef struct {
    uint32_t cr, ndtr, par, m0ar, m1ar, fcr;
    uint32_t n0, pcur, mcur;               /* 재장전 값, 내부 현재 주소 (레지스터로는 안 보인다) */
    bool     stopping;
    unsigned dis_wait;
    uint32_t ht_ndtr;                      /* 마지막 HTIF 를 세운 순간의 NDTR (관찰용) */
    unsigned tc_count, te_count, aborts;
} dstream_t;
static struct { dstream_t s[8]; uint32_t isr[2]; } D;

static uint32_t d_flags(unsigned s) { return (D.isr[s >> 2] >> GOFF[s & 3u]) & DMA_GROUP_MASK; }
static void d_flag(unsigned s, unsigned f) { D.isr[s >> 2] |= 1u << (GOFF[s & 3u] + f); }
static void d_irq_update(unsigned s) {     /* 레벨: (플래그 & 켜진 IE) 가 하나라도 있으면 라인 high */
    uint32_t ie = (D.s[s].cr & DMA_CR_IE_MASK) << 1;
    irq_line_set(DMA2_IRQN[s], (d_flags(s) & ie) != 0u);
}
static unsigned d_size(const dstream_t *st) { return 1u << ((st->cr >> DMA_CR_PSIZE_Pos) & 3u); }
static uint32_t dma_rd(uint32_t off) {
    if (off == DMA_LISR) return D.isr[0];
    if (off == DMA_HISR) return D.isr[1];
    if (off < 0x10u) return 0u;                       /* IFCR 는 쓰기 전용: 0 으로 읽힘 */
    dstream_t *st = &D.s[(off - 0x10u) / 0x18u];
    switch ((off - 0x10u) % 0x18u) {
    case 0x00u: return st->cr;
    case 0x04u: return st->ndtr;
    case 0x08u: return st->par;
    case 0x0Cu: return st->m0ar;
    case 0x10u: return st->m1ar;
    default:    return st->fcr;
    }
}
static void dma_wr(uint32_t off, uint32_t v) {
    if (off == DMA_LIFCR || off == DMA_HIFCR) {       /* W1C 를 "다른 레지스터" 로 */
        unsigned h = (off == DMA_HIFCR);
        D.isr[h] &= ~(v & 0x0F7D0F7Du);
        for (unsigned s = 4u * h; s < 4u * h + 4u; s++) d_irq_update(s);
        return;
    }
    if (off < 0x10u) return;                          /* LISR/HISR 쓰기 무시 */
    unsigned s = (off - 0x10u) / 0x18u;
    dstream_t *st = &D.s[s];
    bool en = (st->cr & DMA_CR_EN) != 0u;
    switch ((off - 0x10u) % 0x18u) {
    case 0x00u:
        if (en) {                                     /* EN=1 동안: IE 비트와 EN 끄기만 먹는다 */
            st->cr = (st->cr & ~DMA_CR_IE_MASK) | (v & DMA_CR_IE_MASK);
            if (!(v & DMA_CR_EN) && !st->stopping) { st->stopping = true; st->dis_wait = DMA_DISABLE_LAT; }
        } else {
            st->cr = v & 0x0FFFFFFFu;
            if (v & DMA_CR_EN) {                      /* enable: NDTR 래치, 주소는 크기로 정렬(하위 비트 무시) */
                uint32_t m = ~(uint32_t)(d_size(st) - 1u);
                st->n0 = st->ndtr; st->pcur = st->par & m; st->mcur = st->m0ar & m;
            }
        }
        d_irq_update(s);
        break;
    case 0x04u: if (!en) st->ndtr = v & 0xFFFFu; break;
    case 0x08u: if (!en) st->par = v; break;
    case 0x0Cu: if (!en) st->m0ar = v; break;
    case 0x10u: if (!en) st->m1ar = v; break;
    default:    if (!en) st->fcr = v; break;
    }
}
static bool bus_rd(uint32_t a, unsigned sz, uint32_t *v) {
    if (a >= SRAM_BASE && a - SRAM_BASE <= SRAM_SIZE - sz) {
        uint32_t x = 0u;
        for (unsigned i = 0; i < sz; i++) x |= (uint32_t)sram[a - SRAM_BASE + i] << (8u * i);
        *v = x;
        return true;
    }
    if (a >= USART1_BASE && a - USART1_BASE < 0x1Cu) {
        uint32_t w = uart_rd((a & ~3u) - USART1_BASE);
        *v = (w >> (8u * (a & 3u))) & (sz == 4u ? 0xFFFFFFFFu : ((1u << (8u * sz)) - 1u));
        return true;
    }
    return false;                                     /* 그 외 = 버스 에러 */
}
static bool bus_wr(uint32_t a, unsigned sz, uint32_t v) {
    if (a >= SRAM_BASE && a - SRAM_BASE <= SRAM_SIZE - sz) {
        for (unsigned i = 0; i < sz; i++) sram[a - SRAM_BASE + i] = (uint8_t)(v >> (8u * i));
        return true;
    }
    if (a >= USART1_BASE && a - USART1_BASE < 0x1Cu) { uart_wr((a & ~3u) - USART1_BASE, v); return true; }
    return false;
}
static bool d_request(unsigned s) {               /* 주변장치 요청 라인 (CHSEL 이 맞아야 보인다) */
    const dstream_t *st = &D.s[s];
    unsigned dir = (st->cr >> DMA_CR_DIR_Pos) & 3u, ch = (st->cr >> DMA_CR_CHSEL_Pos) & 7u;
    if (dir == DMA_DIR_M2M) return true;           /* 메모리→메모리: 요청 없이 계속 */
    if (ch != UART1_DMA_CH) return false;
    if (s == UART1_TX_STREAM && dir == DMA_DIR_M2P) return (U.cr3 & USART_CR3_DMAT) && (U.sr & USART_SR_TXE);
    if ((s == 2u || s == 5u) && dir == DMA_DIR_P2M) return (U.cr3 & USART_CR3_DMAR) && (U.sr & USART_SR_RXNE);
    return false;
}
static void d_move(unsigned s) {                  /* 한 항목 옮기기 */
    dstream_t *st = &D.s[s];
    unsigned sz = d_size(st), dir = (st->cr >> DMA_CR_DIR_Pos) & 3u;
    uint32_t src = (dir == DMA_DIR_M2P) ? st->mcur : st->pcur;
    uint32_t dst = (dir == DMA_DIR_M2P) ? st->pcur : st->mcur;
    uint32_t v;
    if (!bus_rd(src, sz, &v) || !bus_wr(dst, sz, v)) {  /* 버스 에러: TEIF + HW 가 EN 을 끈다 */
        d_flag(s, DMA_TEIF); st->cr &= ~DMA_CR_EN; st->te_count++;
        d_irq_update(s);
        return;
    }
    if (st->cr & DMA_CR_PINC) st->pcur += sz;
    if (st->cr & DMA_CR_MINC) st->mcur += sz;
    st->ndtr--;
    if (st->ndtr == st->n0 / 2u) { d_flag(s, DMA_HTIF); st->ht_ndtr = st->ndtr; }
    if (st->ndtr == 0u) {
        d_flag(s, DMA_TCIF); st->tc_count++;
        if (st->cr & DMA_CR_CIRC) {               /* circular: NDTR·주소 재장전, EN 유지 */
            uint32_t m = ~(uint32_t)(sz - 1u);
            st->ndtr = st->n0; st->pcur = st->par & m; st->mcur = st->m0ar & m;
        } else st->cr &= ~DMA_CR_EN;              /* normal: HW 가 EN 을 끈다 */
    }
    d_irq_update(s);
}
static void dma_tick(void) {
    for (unsigned s = 0; s < 8u; s++) {
        dstream_t *st = &D.s[s];
        if (st->stopping && --st->dis_wait == 0u) {   /* abort 완료: EN=0, TCIF (RM0090 — 확인 권장) */
            st->stopping = false; st->cr &= ~DMA_CR_EN; st->aborts++;
            d_flag(s, DMA_TCIF); d_irq_update(s);
        }
    }
    int win = -1;                                     /* 중재: PL 높은 것, 같으면 번호 낮은 것. tick 당 한 항목 */
    for (unsigned s = 0; s < 8u; s++) {
        const dstream_t *st = &D.s[s];
        if (!(st->cr & DMA_CR_EN) || st->stopping || st->ndtr == 0u || !d_request(s)) continue;
        if (win < 0 || ((st->cr >> DMA_CR_PL_Pos) & 3u) > ((D.s[win].cr >> DMA_CR_PL_Pos) & 3u)) win = (int)s;
    }
    if (win >= 0) d_move((unsigned)win);
}
static void sim_tick(void) { MK.now++; uart_tick(); dma_tick(); }

static void sim_reset(void) {
    memset(&MK, 0, sizeof MK); memset(&D, 0, sizeof D); memset(&U, 0, sizeof U);
    memset(&NV, 0, sizeof NV); memset(sram, 0, sizeof sram); memset(g_dma_cb, 0, sizeof g_dma_cb);
    U.sr = USART_SR_TXE | USART_SR_TC;
    static const isr_t H[8] = { DMA2_Stream0_IRQHandler, DMA2_Stream1_IRQHandler, DMA2_Stream2_IRQHandler,
        DMA2_Stream3_IRQHandler, DMA2_Stream4_IRQHandler, DMA2_Stream5_IRQHandler,
        DMA2_Stream6_IRQHandler, DMA2_Stream7_IRQHandler };
    for (unsigned s = 0; s < 8u; s++) { NV.h[DMA2_IRQN[s]] = H[s]; NV.en[DMA2_IRQN[s]] = true; }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define RUN_UNTIL(cond, max) do { unsigned n_ = 0; while (!(cond) && n_++ < (max)) sim_step(); } while (0)
#define BLOG(...) do { if (g_verbose) { printf("[t=%6llu] ", (unsigned long long)MK.now); printf(__VA_ARGS__); } } while (0)

static uint32_t xs = 0x2545F491u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

/* 테스트 앱: 콜백이 세는 것 */
typedef struct {
    unsigned half, full, err; uint32_t err_flags;
    uint32_t ndtr_at_full, sr_at_full, ntx_at_full;
    char     seq[1100]; unsigned nseq;
    uint32_t rxbuf; uint16_t n;            /* circular 버퍼 버스 주소 · 길이 */
    uint8_t  out[8192]; unsigned nout;
    unsigned cost_half, cost_full;         /* 콜백 하나가 쓰는 CPU tick (HT 쪽이 무거운 필터라고 치자) */
    unsigned stream;
} app_t;
static app_t app;
static void app_reset(unsigned stream) { memset(&app, 0, sizeof app); app.stream = stream; }
static void seq_push(char c) { if (app.nseq + 1u < sizeof app.seq) { app.seq[app.nseq++] = c; app.seq[app.nseq] = 0; } }
static void cb_half(void *ctx) {
    (void)ctx; app.half++; seq_push('H');
    if (app.n) { memcpy(app.out + app.nout, sram + (app.rxbuf - SRAM_BASE), app.n / 2u); app.nout += app.n / 2u; }
    sim_burn(app.cost_half);
}
static void cb_full(void *ctx) {
    (void)ctx; app.full++; seq_push('F');
    app.ndtr_at_full = D.s[app.stream].ndtr; app.sr_at_full = U.sr; app.ntx_at_full = U.ntx;
    if (app.n) { memcpy(app.out + app.nout, sram + (app.rxbuf - SRAM_BASE) + app.n / 2u, app.n / 2u); app.nout += app.n / 2u; }
    sim_burn(app.cost_full);
}
static void cb_err(void *ctx, uint32_t flags) { (void)ctx; app.err++; app.err_flags = flags; seq_push('E'); }
static const dma_cb_t APP_CB = { cb_half, cb_full, cb_err, NULL };

static dma_cfg_t m2m_cfg(unsigned s, unsigned size, uint32_t src, uint32_t dst, uint16_t n) {
    return (dma_cfg_t){ .stream = (uint8_t)s, .dir = DMA_DIR_M2M, .size = (uint8_t)size, .prio = 0u,
                        .pinc = true, .minc = true, .par = src, .m0ar = dst, .ndtr = n };
}
static bool m2m_copy_ok(unsigned s, unsigned size, uint32_t so, uint32_t dof, uint16_t n) {
    unsigned bytes = n * (1u << size);
    for (unsigned i = 0; i < bytes; i++) sram[so + i] = (uint8_t)xorshift32();
    sram[dof - 1u] = 0xEEu; sram[dof + bytes] = 0xEEu;                       /* 가드 */
    dma_cfg_t c = m2m_cfg(s, size, SA(so), SA(dof), n);
    if (dma_stream_config(DMA2_BASE, &c) != DMA_OK) return false;
    dma_stream_start(DMA2_BASE, s);
    RUN_UNTIL(!(D.s[s].cr & DMA_CR_EN), 70000u);
    return memcmp(sram + so, sram + dof, bytes) == 0 && sram[dof - 1u] == 0xEEu && sram[dof + bytes] == 0xEEu
        && D.s[s].ndtr == 0u && !(D.s[s].cr & DMA_CR_EN);
}

/* 정답 표: [스트림][FE, DME, TE, HT, TC] → LISR/HISR 비트 */
static const unsigned FLAG_TABLE[8][5] = {
    { 0, 2, 3, 4, 5 }, { 6, 8, 9, 10, 11 }, { 16, 18, 19, 20, 21 }, { 22, 24, 25, 26, 27 },
    { 0, 2, 3, 4, 5 }, { 6, 8, 9, 10, 11 }, { 16, 18, 19, 20, 21 }, { 22, 24, 25, 26, 27 } };
static const unsigned FLAG_IDS[5] = { DMA_FEIF, DMA_DMEIF, DMA_TEIF, DMA_HTIF, DMA_TCIF };

/* 하네스 쪽 "잘못된 드라이버" — 버그를 재현해서 결과를 단언한다 */
static void naive_config_no_wait(unsigned s, uint32_t m0ar, uint16_t n, uint32_t cr) {
    REG_WRITE(DMA2_BASE + DMA_SxCR(s), REG_READ(DMA2_BASE + DMA_SxCR(s)) & ~DMA_CR_EN);  /* 끄고 안 기다림 */
    REG_WRITE(DMA2_BASE + DMA_SxM0AR(s), m0ar);
    REG_WRITE(DMA2_BASE + DMA_SxNDTR(s), n);
    REG_WRITE(DMA2_BASE + DMA_SxCR(s), cr);
}
static void naive_config_no_clear(unsigned s, uint32_t par, uint32_t m0ar, uint16_t n, uint32_t cr) {
    REG_WRITE(DMA2_BASE + DMA_SxPAR(s), par);                                         /* IFCR 클리어 없음 */
    REG_WRITE(DMA2_BASE + DMA_SxM0AR(s), m0ar);
    REG_WRITE(DMA2_BASE + DMA_SxNDTR(s), n);
    REG_WRITE(DMA2_BASE + DMA_SxCR(s), cr);
}
/* 버그 핸들러 1: 콜백 다 부른 뒤 그룹 전체를 지움 (콜백 도중 새로 선 TC 가 같이 지워진다) */
static void bad_handler_clear_all_at_end_s2(void) {
    uint32_t f = (REG_READ(DMA2_BASE + DMA_LISR) >> 16) & DMA_GROUP_MASK;
    if (f & (1u << DMA_HTIF)) cb_half(NULL);
    if (f & (1u << DMA_TCIF)) cb_full(NULL);
    REG_WRITE(DMA2_BASE + DMA_LIFCR, DMA_GROUP_MASK << 16);
}
/* 버그 핸들러 2: 스트림 0 핸들러가 LIFCR = LISR (같은 레지스터의 스트림 2 플래그까지 지움) */
static unsigned g_s0_done;
static void bad_handler_lifcr_eq_lisr_s0(void) {
    uint32_t f = REG_READ(DMA2_BASE + DMA_LISR);
    REG_WRITE(DMA2_BASE + DMA_LIFCR, f);
    if (f & (1u << DMA_TCIF)) g_s0_done++;
}
static void s0_full(void *ctx) { (void)ctx; g_s0_done++; }
/* 버그 핸들러 3: 플래그를 안 지움 */
static unsigned g_storm_calls;
static void bad_handler_no_clear_s0(void) { g_storm_calls++; }

/* RX circular 시나리오: total 바이트를 period tick 마다, 버퍼 n, HT 콜백 비용 cost_half tick */
static uint8_t rx_data[8192];
static void rx_scenario(uint16_t n, unsigned total, unsigned period, unsigned cost_half, isr_t s2_handler) {
    sim_reset(); app_reset(UART1_RX_STREAM);
    app.rxbuf = SA(0x1000); app.n = n; app.cost_half = cost_half;
    for (unsigned i = 0; i < total; i++) rx_data[i] = (uint8_t)xorshift32();
    dma_set_callbacks(DMA2_BASE, UART1_RX_STREAM, &APP_CB);
    if (s2_handler) NV.h[DMA2_IRQN[2]] = s2_handler;
    if (uart1_rx_dma_start(app.rxbuf, n) != DMA_OK) return;
    U.rx_src = rx_data; U.rx_n = total; U.rx_period = period;     /* 수신 시작은 DMA 준비 뒤 */
    RUN_UNTIL(U.rx_i == U.rx_n && D.s[2].ndtr == n, (total + 4u) * period + 1000u);
    for (unsigned k = 0; k < 200u; k++) sim_step();   /* 남은 IRQ 처리 */
}

#ifndef BM_M08_NO_MAIN
int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    /* ---------- Part A1: 플래그 위치 전수 ---------- */
    for (unsigned s = 0; s < 8u; s++) {
        bool ok = true;
        for (unsigned k = 0; k < 5u; k++) ok &= (dma_flag_bit(s, FLAG_IDS[k]) == FLAG_TABLE[s][k]);
        CHECK(ok);
        CHECK(dma_isr_reg(s) == (s < 4u ? 0x00u : 0x04u) && dma_ifcr_reg(s) == (s < 4u ? 0x08u : 0x0Cu));
    }
    uint32_t uni = 0u;
    for (unsigned s = 0; s < 4u; s++) uni |= DMA_GROUP_MASK << dma_flag_bit(s, 0u);
    CHECK(uni == 0x0F7D0F7Du);                       /* 네 그룹을 합치면 유효 비트 전부, 겹침 없음 */

    /* ---------- Part A2: SxCR 조립 ---------- */
    dma_cfg_t tx = { .stream = 7, .channel = 4, .dir = DMA_DIR_M2P, .size = DMA_SZ_BYTE, .prio = 1,
                     .minc = true, .tcie = true, .teie = true };
    dma_cfg_t rx = { .stream = 2, .channel = 4, .dir = DMA_DIR_P2M, .size = DMA_SZ_BYTE, .prio = 2,
                     .minc = true, .circ = true, .htie = true, .tcie = true, .teie = true };
    dma_cfg_t mm = { .dir = DMA_DIR_M2M, .size = DMA_SZ_WORD, .prio = 3, .pinc = true, .minc = true };
    CHECK(dma_cr_value(&tx) == 0x08010454u);
    CHECK(dma_cr_value(&rx) == 0x0802051Cu);
    CHECK(dma_cr_value(&mm) == 0x00035680u);
    unsigned bad = 0, combos = 0;
    for (unsigned ch = 0; ch < 8u; ch++) for (unsigned pl = 0; pl < 4u; pl++)
    for (unsigned sz = 0; sz < 3u; sz++) for (unsigned dir = 0; dir < 3u; dir++) for (unsigned b = 0; b < 64u; b++) {
        dma_cfg_t c = { .channel = (uint8_t)ch, .prio = (uint8_t)pl, .size = (uint8_t)sz, .dir = (uint8_t)dir,
                        .minc = b & 1u, .pinc = (b >> 1) & 1u, .circ = (b >> 2) & 1u,
                        .tcie = (b >> 3) & 1u, .htie = (b >> 4) & 1u, .teie = (b >> 5) & 1u };
        uint32_t v = dma_cr_value(&c);
        combos++;
        bad += !(((v >> 25) & 7u) == ch && ((v >> 16) & 3u) == pl && ((v >> 13) & 3u) == sz && ((v >> 11) & 3u) == sz
                 && ((v >> 6) & 3u) == dir && ((v >> 10) & 1u) == (b & 1u) && ((v >> 9) & 1u) == ((b >> 1) & 1u)
                 && ((v >> 8) & 1u) == ((b >> 2) & 1u) && ((v >> 4) & 1u) == ((b >> 3) & 1u)
                 && ((v >> 3) & 1u) == ((b >> 4) & 1u) && ((v >> 2) & 1u) == ((b >> 5) & 1u)
                 && (v & 0xF1FC8023u) == 0u);        /* EN·DMEIE·PFCTRL·PINCOS·DBM·CT·BURST 는 0 */
    }
    CHECK(combos == 18432u && bad == 0u);

    /* ---------- Part A3: M→M 복사 byte / halfword / word, 플래그는 IE 없이도 선다 ---------- */
    sim_reset();
    CHECK(m2m_copy_ok(0u, DMA_SZ_BYTE, 0x100u, 0x400u, 37u));
    CHECK(D.isr[0] == ((1u << 4) | (1u << 5)));      /* 스트림 0: HTIF|TCIF (LISR 비트 4, 5) */
    CHECK(m2m_copy_ok(3u, DMA_SZ_HALF, 0x800u, 0xA00u, 37u));
    CHECK(((D.isr[0] >> 22) & DMA_GROUP_MASK) == 0x30u);
    CHECK(m2m_copy_ok(6u, DMA_SZ_WORD, 0x1000u, 0x1400u, 37u));
    CHECK(D.isr[1] == ((1u << 20) | (1u << 21)));    /* 스트림 6: HISR 비트 20, 21 */
    CHECK(NV.entries[56] + NV.entries[59] + NV.entries[69] == 0u);   /* IE 가 꺼져 있으면 IRQ 는 없다 */

    /* ---------- Part A4: HT 는 NDTR == N/2 에서, TC 는 0 에서 (N = 1..64) ---------- */
    bool ht_ok = true;
    for (unsigned n = 1; n <= 64u; n++) {
        sim_reset();
        ht_ok &= m2m_copy_ok(1u, DMA_SZ_BYTE, 0x100u, 0x200u, (uint16_t)n)
              && D.s[1].ht_ndtr == n / 2u && D.s[1].tc_count == 1u && ((D.isr[0] >> 6) & 0x30u) == 0x30u;
    }
    CHECK(ht_ok);

    /* ---------- Part A5: PINC=0 → 고정 소스로 채우기 (memset) ---------- */
    sim_reset();
    sram[0x40] = 0x1E; sram[0x41] = 0x0F; sram[0x42] = 0xC3; sram[0x43] = 0xA5;
    dma_cfg_t fill = m2m_cfg(4u, DMA_SZ_WORD, SA(0x40), SA(0x200), 64u);
    fill.pinc = false;
    CHECK(dma_stream_config(DMA2_BASE, &fill) == DMA_OK);
    dma_stream_start(DMA2_BASE, 4u);
    RUN_UNTIL(!(D.s[4].cr & DMA_CR_EN), 1000u);
    bool fill_ok = true;
    for (unsigned i = 0; i < 64u; i++) fill_ok &= (sram[0x200 + 4 * i] == 0x1E && sram[0x203 + 4 * i] == 0xA5);
    CHECK(fill_ok && D.s[4].ndtr == 0u);

    /* ---------- Part A6: 무작위 복사 3000건 (fixed seed) ---------- */
    unsigned rnd_fail = 0;
    for (unsigned k = 0; k < 3000u; k++) {
        sim_reset();
        unsigned sz = xorshift32() % 3u, s = xorshift32() % 8u, n = 1u + xorshift32() % 300u;
        uint32_t so = 0x100u + ((xorshift32() % 0x800u) & ~3u), dof = 0x2000u + ((xorshift32() % 0x800u) & ~3u);
        if (!m2m_copy_ok(s, sz, so, dof, (uint16_t)n)) rnd_fail++;
    }
    CHECK(rnd_fail == 0u);

    /* ---------- Part B1: EN=1 인 채 재설정 → 거부, 레지스터 한 줄도 안 씀 ---------- */
    sim_reset(); app_reset(UART1_TX_STREAM);
    for (unsigned i = 0; i < 100u; i++) sram[0x300 + i] = (uint8_t)('a' + i % 26u);
    dma_set_callbacks(DMA2_BASE, 7u, &APP_CB);
    CHECK(uart1_tx_dma_start(SA(0x300), 100u) == DMA_OK);
    RUN_UNTIL(U.ntx >= 30u, 2000u);
    uint32_t ndtr_before = D.s[7].ndtr;
    dma_cfg_t other = tx; other.par = USART1_BASE + USART_DR; other.m0ar = SA(0x500); other.ndtr = 10u;
    log_start();
    CHECK(dma_stream_config(DMA2_BASE, &other) == DMA_EBUSY);
    CHECK(MK.nlog == 1u && MK.log[0].op == 'R' && MK.log[0].addr == DMA2_BASE + DMA_SxCR(7));
    CHECK(D.s[7].m0ar == SA(0x300) && D.s[7].ndtr <= ndtr_before && D.s[7].ndtr > 60u);
    CHECK(uart1_tx_dma_start(SA(0x500), 10u) == DMA_EBUSY);

    /* ---------- Part B2: abort — EN 이 0 으로 읽힐 때까지 기다린다 ---------- */
    log_start();
    CHECK(dma_stream_disable(DMA2_BASE, 7u) == DMA_OK);
    MK.logging = false;
    unsigned cr_reads = log_count('R', DMA2_BASE + DMA_SxCR(7)), cr_writes = log_count('W', DMA2_BASE + DMA_SxCR(7));
    CHECK(cr_writes == 1u && cr_reads == 1u + DMA_DISABLE_LAT);   /* 첫 확인 1 + EN=1 로 읽힌 폴링들 */
    CHECK(!(D.s[7].cr & DMA_CR_EN) && D.s[7].aborts == 1u);
    CHECK((D.isr[1] >> 27) & 1u);                    /* abort 도 TCIF 를 세운다 (모델: RM0090 확인 권장) */
    uint32_t abort_ndtr = D.s[7].ndtr;
    BLOG("S7 abort: EN=0 after %u CR reads, NDTR=%u left (sent %u of 100), HISR=0x%08X\n",
         cr_reads, (unsigned)abort_ndtr, 100u - (unsigned)abort_ndtr, (unsigned)D.isr[1]);
    for (unsigned i = 0; i < 50u; i++) sim_step();
    CHECK(app.full == 0u && abort_ndtr > 0u && abort_ndtr < 70u);  /* IE 를 같이 껐으니 가짜 "완료" 콜백 없음 */
    CHECK(dma_stream_disable(DMA2_BASE, 7u) == DMA_OK && dma_stream_disable(DMA2_BASE, 8u) == DMA_EINVAL);

    /* ---------- Part B3: 기다리지 않고 재설정 → 절반만 적용 (버그 재현, 결과 단언) ---------- */
    sim_reset();
    for (unsigned i = 0; i < 100u; i++) sram[0x300 + i] = (uint8_t)('A' + i % 26u);
    CHECK(uart1_tx_dma_start(SA(0x300), 100u) == DMA_OK);
    RUN_UNTIL(U.ntx >= 20u, 2000u);
    naive_config_no_wait(7u, SA(0x600), 5u, 0x08010454u);
    CHECK(D.s[7].m0ar == SA(0x300) && D.s[7].ndtr > 5u);   /* M0AR·NDTR 쓰기는 EN=1 이라 무시됨 */
    CHECK(D.s[7].cr == 0x08010454u);                      /* 마지막 CR 쓰기만 먹었다: 옛 버퍼 + 새 CR */

    /* ---------- Part B4: 남은 TCIF — 클리어 없이 재시작하면 0 바이트에서 "완료" 콜백 ---------- */
    sim_reset(); app_reset(UART1_TX_STREAM);
    dma_set_callbacks(DMA2_BASE, 7u, &APP_CB);
    CHECK(uart1_tx_dma_start(SA(0x300), 100u) == DMA_OK);
    RUN_UNTIL(U.ntx >= 10u, 2000u);
    CHECK(dma_stream_disable(DMA2_BASE, 7u) == DMA_OK && ((D.isr[1] >> 27) & 1u));  /* TCIF 남아 있음 */
    naive_config_no_clear(7u, USART1_BASE + USART_DR, SA(0x300), 50u, 0x08010454u);
    dma_stream_start(DMA2_BASE, 7u);
    sim_step();
    CHECK(app.full == 1u && app.ndtr_at_full > 45u);     /* 거의 안 보냈는데 "완료" */
    CHECK(dma_stream_disable(DMA2_BASE, 7u) == DMA_OK);
    app_reset(UART1_TX_STREAM);
    CHECK(uart1_tx_dma_start(SA(0x300), 50u) == DMA_OK); /* 올바른 설정: 플래그부터 지움 */
    for (unsigned i = 0; i < 20u; i++) sim_step();
    CHECK(app.full == 0u);
    RUN_UNTIL(app.full != 0u, 5000u);
    CHECK(app.full == 1u && app.ndtr_at_full == 0u);

    /* ---------- Part B5: 버스 에러 → TEIF, EN=0, NDTR 이 멈춘 곳을 알려 준다 ---------- */
    sim_reset(); app_reset(0u);
    dma_set_callbacks(DMA2_BASE, 0u, &APP_CB);
    dma_cfg_t te = m2m_cfg(0u, DMA_SZ_WORD, SA(0x100), 0x30000000u, 16u);
    te.teie = true; te.tcie = true;
    CHECK(dma_stream_config(DMA2_BASE, &te) == DMA_OK);
    dma_stream_start(DMA2_BASE, 0u);
    RUN_UNTIL(app.err != 0u, 100u);
    CHECK(app.err == 1u && app.err_flags == (1u << DMA_TEIF) && app.full == 0u);
    CHECK(!(D.s[0].cr & DMA_CR_EN) && D.s[0].ndtr == 16u);
    sim_reset(); app_reset(0u);
    dma_set_callbacks(DMA2_BASE, 0u, &APP_CB);
    te = m2m_cfg(0u, DMA_SZ_WORD, SRAM_BASE + SRAM_SIZE - 20u, SA(0x100), 8u);  /* 소스가 SRAM 끝을 넘는다 */
    te.teie = true;
    CHECK(dma_stream_config(DMA2_BASE, &te) == DMA_OK);
    dma_stream_start(DMA2_BASE, 0u);
    RUN_UNTIL(app.err != 0u, 100u);
    CHECK(app.err == 1u && D.s[0].ndtr == 3u && D.s[0].te_count == 1u);   /* 5 개 옮기고 6번째에서 에러 */
    BLOG("S0 TEIF: NDTR=%u -> moved %u items, failed at address 0x%08X\n", (unsigned)D.s[0].ndtr,
         8u - (unsigned)D.s[0].ndtr, SRAM_BASE + SRAM_SIZE - 20u + 4u * (8u - (unsigned)D.s[0].ndtr));

    /* ---------- Part B6: 입력 검증 + HW 가 정렬을 조용히 버리는 것 ---------- */
    sim_reset();
    unsigned acc0 = (unsigned)MK.now;
    dma_cfg_t v = m2m_cfg(1u, DMA_SZ_HALF, SA(0x100), SA(0x201), 4u);
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);           /* 홀수 주소 halfword */
    v = m2m_cfg(1u, DMA_SZ_WORD, SA(0x102), SA(0x200), 4u);
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);           /* word 인데 +2 */
    v = m2m_cfg(1u, DMA_SZ_BYTE, SA(0x100), SA(0x200), 0u);
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);           /* NDTR 0 */
    v = m2m_cfg(1u, DMA_SZ_BYTE, SA(0x100), SA(0x200), 4u); v.circ = true;
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);           /* M→M + circular */
    v.circ = false;
    CHECK(dma_stream_config(DMA1_BASE, &v) == DMA_EINVAL);           /* DMA1 은 M→M 불가 */
    v.stream = 8u;
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);
    v.stream = 1u; v.channel = 8u;
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);
    v.channel = 0u; v.size = 3u;
    CHECK(dma_stream_config(DMA2_BASE, &v) == DMA_EINVAL);
    CHECK((unsigned)MK.now == acc0 && MK.unexpected == 0u);         /* 거부는 버스 접근 없이 */
    /* 드라이버를 우회해 홀수 M0AR 을 halfword 로 직접 쓰면: HW 는 하위 비트를 버리고 한 칸 앞에 쓴다 */
    for (unsigned i = 0; i < 8u; i++) sram[0x100 + i] = (uint8_t)(0x11u * (i + 1u));
    sram[0x200] = 0xEEu;
    REG_WRITE(DMA2_BASE + DMA_SxPAR(1), SA(0x100));
    REG_WRITE(DMA2_BASE + DMA_SxM0AR(1), SA(0x201));
    REG_WRITE(DMA2_BASE + DMA_SxNDTR(1), 4u);
    REG_WRITE(DMA2_BASE + DMA_SxCR(1), 0x00002E81u);                 /* M2M, half, PINC, MINC, EN */
    RUN_UNTIL(!(D.s[1].cr & DMA_CR_EN), 100u);
    CHECK(sram[0x200] == 0x11u && sram[0x207] == 0x88u && sram[0x208] == 0x00u);  /* 가드 0x200 이 덮였다 */

    /* ---------- Part B7: ISR 의 플래그 클리어 — 본 것만, 이 스트림 것만 ---------- */
    rx_scenario(8u, 512u, 3u, 11u, NULL);                            /* HT 콜백 11 tick, 반 버퍼 12 tick */
    CHECK(app.half == 64u && app.full == 64u && app.nout == 512u && memcmp(app.out, rx_data, 512u) == 0);
    bool alt = true;
    for (unsigned i = 0; i < app.nseq; i++) alt &= (app.seq[i] == ((i & 1u) ? 'F' : 'H'));
    CHECK(alt && U.ore == 0u);
    rx_scenario(8u, 512u, 3u, 11u, bad_handler_clear_all_at_end_s2);
    unsigned lost_end = 128u - (app.half + app.full);
    CHECK(app.half == 64u && app.full < 64u && lost_end > 0u);       /* TC 가 콜백 도중 서고 같이 지워짐 */
    BLOG("clear-all-at-end handler: half=%u full=%u (lost %u TC of 64)\n", app.half, app.full, lost_end);
    /* 스트림 0 이 M→M 을 반복하는 동안 스트림 2 가 RX: 스트림 0 핸들러가 LIFCR = LISR 이면 스트림 2 가 굶는다 */
    unsigned lost_x[2];
    for (unsigned variant = 0; variant < 2u; variant++) {
        sim_reset(); app_reset(UART1_RX_STREAM);
        app.rxbuf = SA(0x1000); app.n = 8u;
        for (unsigned i = 0; i < 400u; i++) rx_data[i] = (uint8_t)xorshift32();
        dma_set_callbacks(DMA2_BASE, 2u, &APP_CB);
        dma_cb_t c0 = { NULL, s0_full, NULL, NULL };
        dma_set_callbacks(DMA2_BASE, 0u, &c0);
        if (variant == 1u) NV.h[56] = bad_handler_lifcr_eq_lisr_s0;
        g_s0_done = 0;
        CHECK(uart1_rx_dma_start(app.rxbuf, 8u) == DMA_OK);
        U.rx_src = rx_data; U.rx_n = 400u; U.rx_period = 3u;
        dma_cfg_t cp = m2m_cfg(0u, DMA_SZ_BYTE, SA(0x100), SA(0x200), 5u); cp.tcie = true;
        unsigned started = 0;
        for (unsigned t = 0; t < 1400u; t++) {
            NV.primask = (t % 40u) >= 34u;            /* 40 step 중 6 step 은 메인의 임계 구역 */
            if (t % 16u == 0u && started == g_s0_done && dma_stream_config(DMA2_BASE, &cp) == DMA_OK) {
                dma_stream_start(DMA2_BASE, 0u); started++;
            }
            sim_step();
        }
        NV.primask = false;
        for (unsigned k = 0; k < 50u; k++) sim_step();
        lost_x[variant] = 100u - (app.half + app.full);
    }
    CHECK(lost_x[0] == 0u && lost_x[1] > 0u);
    BLOG("LIFCR=LISR in stream-0 ISR: stream-2 callbacks lost %u of 100 (correct handler: %u)\n", lost_x[1], lost_x[0]);
    /* 플래그를 안 지우는 핸들러: 레벨 라인이라 매 step 다시 들어온다 */
    sim_reset();
    NV.h[56] = bad_handler_no_clear_s0; g_storm_calls = 0;
    dma_cfg_t st = m2m_cfg(0u, DMA_SZ_BYTE, SA(0x100), SA(0x200), 4u); st.tcie = true;
    CHECK(dma_stream_config(DMA2_BASE, &st) == DMA_OK);
    dma_stream_start(DMA2_BASE, 0u);
    for (unsigned i = 0; i < 100u; i++) sim_step();
    CHECK(g_storm_calls >= 95u && NV.level[56]);                      /* 100 step 중 ~전부 ISR */

    /* ---------- Part C1: USART1 TX DMA — DMA TC 는 "마지막 바이트를 DR 에 넣었다" 일 뿐 ---------- */
    sim_reset(); app_reset(UART1_TX_STREAM);
    static const char msg[] = "BM-M08 boot: DMA2 S7 -> USART1 TX, 0123456789 abcdefghijklmnopqrstuvwxyz\r\n";
    unsigned mlen = (unsigned)(sizeof msg - 1u);
    memcpy(sram + 0x300, msg, mlen);
    dma_set_callbacks(DMA2_BASE, 7u, &APP_CB);
    log_start();
    CHECK(uart1_tx_dma_start(SA(0x300), (uint16_t)mlen) == DMA_OK);
    MK.logging = false;
    CHECK(D.s[7].cr == (0x08010454u | DMA_CR_EN) && (U.cr3 & USART_CR3_DMAT) && MK.unexpected == 0u);
    CHECK(MK.log[1].op == 'W' && MK.log[1].addr == DMA2_BASE + DMA_HIFCR && MK.log[1].val == (0x3Du << 22));
    BLOG("S7 cfg: CR=0x%08X NDTR=%u PAR=0x%08X M0AR=0x%08X (HIFCR<-0x%08X first)\n", (unsigned)D.s[7].cr,
         (unsigned)D.s[7].ndtr, (unsigned)D.s[7].par, (unsigned)D.s[7].m0ar, (unsigned)MK.log[1].val);
    uint64_t t0 = MK.now;
    RUN_UNTIL(app.full != 0u, 20000u);
    CHECK(app.full == 1u && app.ndtr_at_full == 0u);
    CHECK(!(app.sr_at_full & USART_SR_TC) && app.ntx_at_full < mlen);  /* 아직 선 위에 바이트가 남았다 */
    BLOG("S7 TC callback: NDTR=0, USART1 SR=0x%02X (TC=0), on-wire %u/%u bytes\n",
         (unsigned)app.sr_at_full, (unsigned)app.ntx_at_full, mlen);
    unsigned polls = 0;
    while (!(REG_READ(USART1_BASE + USART_SR) & USART_SR_TC) && polls < 1000u) polls++;
    CHECK(U.ntx == mlen && memcmp(U.txcap, msg, mlen) == 0);
    BLOG("USART1 TC=1 after %u more polls: %u bytes on wire, %llu ticks total -> safe to cut power / drop RS-485 DE\n",
         polls, U.ntx, (unsigned long long)(MK.now - t0));
    CHECK(uart1_tx_dma_start(SA(0x300), 5u) == DMA_OK);           /* 끝났으니 다시 보낼 수 있다 */
    RUN_UNTIL(app.full == 2u, 2000u);
    CHECK(app.full == 2u);

    /* ---------- Part C2: RX circular — 4096 바이트, N = 16, HT/TC 번갈아 ---------- */
    rx_scenario(16u, 4096u, 5u, 0u, NULL);
    CHECK(app.nout == 4096u && memcmp(app.out, rx_data, 4096u) == 0);
    alt = (app.nseq == 512u);
    for (unsigned i = 0; i < app.nseq; i++) alt &= (app.seq[i] == ((i & 1u) ? 'F' : 'H'));
    CHECK(alt && D.s[2].tc_count == 256u && U.ore == 0u && (D.s[2].cr & DMA_CR_EN));
    BLOG("S2 RX circular N=16: %u bytes, %u HT + %u TC callbacks, %u laps, ORE=%u, EN still 1\n",
         app.nout, app.half, app.full, D.s[2].tc_count, U.ore);

    /* ---------- Part C3: NDTR 이 안 움직인다 = 요청이 안 온다 (CHSEL 실수, DMAT 안 켬) ---------- */
    sim_reset();
    dma_cfg_t wrong = tx; wrong.channel = 5u; wrong.par = USART1_BASE + USART_DR; wrong.m0ar = SA(0x300); wrong.ndtr = 40u;
    CHECK(dma_stream_config(DMA2_BASE, &wrong) == DMA_OK);
    dma_stream_start(DMA2_BASE, 7u);
    REG_WRITE(USART1_BASE + USART_CR3, USART_CR3_DMAT);
    for (unsigned i = 0; i < 2000u; i++) sim_step();
    CHECK(D.s[7].ndtr == 40u && (D.s[7].cr & DMA_CR_EN) && U.ntx == 0u);
    BLOG("S7 CHSEL=5 (wrong): after 2000 ticks NDTR=%u EN=1 tx=%u -> request never arrives\n",
         (unsigned)D.s[7].ndtr, U.ntx);
    CHECK(dma_stream_disable(DMA2_BASE, 7u) == DMA_OK);
    REG_WRITE(USART1_BASE + USART_CR3, 0u);
    wrong.channel = 4u;
    CHECK(dma_stream_config(DMA2_BASE, &wrong) == DMA_OK);
    dma_stream_start(DMA2_BASE, 7u);                                  /* 채널은 맞지만 DMAT 를 안 켬 */
    for (unsigned i = 0; i < 2000u; i++) sim_step();
    CHECK(D.s[7].ndtr == 40u && U.ntx == 0u);
    REG_WRITE(USART1_BASE + USART_CR3, USART_CR3_DMAT);
    RUN_UNTIL(D.s[7].ndtr == 0u, 2000u);
    CHECK(D.s[7].ndtr == 0u && MK.unexpected == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* BM_M08_NO_MAIN */
#endif /* UNIT_TEST */
