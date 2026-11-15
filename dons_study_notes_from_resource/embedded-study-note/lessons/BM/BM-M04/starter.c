/* BM-M04 인터럽트 구동 UART: ISR + 링버퍼 TX/RX
 *
 * 호스트 빌드 (UNIT_TEST): 드라이버는 REG_READ/REG_WRITE 로만 레지스터를 만지고, 그 매크로가 mock 으로 가서
 *   USART2 행동 모델 + NVIC-lite 를 돌린다. 시간은 sim_step() 한 번 = 1 tick = 1 비트 시간 (8N1 프레임 = 10 tick).
 *   cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M04_so && /tmp/BM-M04_so [-v]
 * 타깃 빌드: 맨 위 #define UNIT_TEST 를 지우면 드라이버 부분(1~4절)이 volatile MMIO + CMSIS NVIC 로 컴파일된다.
 * 주소·비트: STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 파일의 모델 정의다.
 */
#define UNIT_TEST 1
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 1. 레지스터 접근 seam (BM-B02 mock 축약판) ================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
void     sim_step(void);
void     nvic_lite_enable(int irqn);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#define CPU_IDLE()            sim_step()               /* 바쁜 대기 한 바퀴 = 1 tick, 그 사이 ISR 가 끼어들 수 있다 */
#define NVIC_ENABLE_IRQ(n)    nvic_lite_enable(n)
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#define CPU_IDLE()            __asm volatile("nop")
#define NVIC_ENABLE_IRQ(n)    NVIC_EnableIRQ((IRQn_Type)(n))   /* CMSIS device header 필요 */
#endif

/* ================= 2. 레지스터 정의 (RM0090 기준 대표값) ================= */
#define RCC_APB1ENR           0x40023840u
#define RCC_APB1ENR_USART2EN  (1u << 17)
#define USART2_BASE           0x40004400u
#define USART_SR              (USART2_BASE + 0x00u)
#define USART_DR              (USART2_BASE + 0x04u)
#define USART_BRR             (USART2_BASE + 0x08u)
#define USART_CR1             (USART2_BASE + 0x0Cu)
#define USART2_IRQn           38                       /* F4 벡터 표: USART1=37, USART2=38 (확인 권장) */
#define SR_FE      (1u << 1)
#define SR_NF      (1u << 2)
#define SR_ORE     (1u << 3)
#define SR_IDLE    (1u << 4)
#define SR_RXNE    (1u << 5)
#define SR_TC      (1u << 6)
#define SR_TXE     (1u << 7)
#define CR1_RE     (1u << 2)
#define CR1_TE     (1u << 3)
#define CR1_RXNEIE (1u << 5)                           /* RXNE 또는 ORE 이면 인터럽트 */
#define CR1_TCIE   (1u << 6)
#define CR1_TXEIE  (1u << 7)
#define CR1_UE     (1u << 13)

/* ================= 3. SPSC 링 버퍼 (DS-B04 자유 증가 인덱스) ================= */
enum { UART_OK = 0, UART_EINVAL = -1, UART_ETIMEOUT = -2, UART_EFULL = -3 };
#define TX_CAP 128u
#define RX_CAP 64u
_Static_assert((TX_CAP & (TX_CAP - 1u)) == 0 && (RX_CAP & (RX_CAP - 1u)) == 0, "capacity must be 2^n");
typedef struct {
    volatile uint32_t head;      /* 생산자만 쓴다 — 지금까지 넣은 총 개수 */
    volatile uint32_t tail;      /* 소비자만 쓴다 — 지금까지 뺀 총 개수  */
    uint32_t mask;               /* cap − 1 */
    uint8_t *buf;
} ring_t;
static uint8_t g_txbuf[TX_CAP], g_rxbuf[RX_CAP];
static ring_t g_tx = { 0, 0, TX_CAP - 1u, g_txbuf };   /* 생산자 main, 소비자 ISR */
static ring_t g_rx = { 0, 0, RX_CAP - 1u, g_rxbuf };   /* 생산자 ISR, 소비자 main */

static uint32_t ring_count(const ring_t *r) { return r->head - r->tail; }

/* 생산자 쪽에서만 호출. 가득이면 0 (아무것도 안 바꿈), 넣었으면 1. */
static int ring_push(ring_t *r, uint8_t b) {
    /* TODO(Part A): 가득(count == cap)이면 0. 아니면 buf[head & mask] 에 쓰고, 컴파일러 장벽, head + 1 공개 */
    (void)r; (void)b;
    return 0;
}

/* 소비자 쪽에서만 호출. 비었으면 0, 꺼냈으면 1. */
static int ring_pop(ring_t *r, uint8_t *out) {
    /* TODO(Part A): 비었으면 0. head 확인 → 장벽 → 데이터 읽기 → 장벽 → tail + 1 */
    (void)r; (void)out;
    return 0;
}

/* ================= 4. 드라이버 ================= */
typedef struct {
    uint32_t isr_calls, rx_bytes, rx_dropped, ore, fe, tx_bytes, txeie_off;
} uart_stats_t;
static volatile uart_stats_t g_st;                        /* ISR 만 쓴다, main 은 읽기만 (32비트 정렬 = 단일 LDR) */
static uint32_t g_log_dropped, g_log_trunc;               /* main 만 쓴다 */
#define LOG_LINE_MAX 48u

/* 주어진 코드: 8N1, TX+RX, RXNE 인터럽트 켜고 NVIC 열기. BRR = round(fck/baud) (BM-B12) */
void uart_init(uint32_t fck, uint32_t baud) {
    g_tx.head = g_tx.tail = g_rx.head = g_rx.tail = 0;
    memset((void *)&g_st, 0, sizeof g_st);
    g_log_dropped = g_log_trunc = 0;
    REG_WRITE(RCC_APB1ENR, REG_READ(RCC_APB1ENR) | RCC_APB1ENR_USART2EN);
    (void)REG_READ(RCC_APB1ENR);                          /* 클럭 enable 직후 read-back (BM-B03) */
    REG_WRITE(USART_CR1, CR1_UE);
    REG_WRITE(USART_BRR, (uint32_t)((2ull * fck + baud) / (2ull * baud)));
    REG_WRITE(USART_CR1, CR1_UE | CR1_TE | CR1_RE | CR1_RXNEIE);   /* TXEIE 는 보낼 게 생길 때만 */
    NVIC_ENABLE_IRQ(USART2_IRQn);
}

uint32_t uart_tx_free(void) { return TX_CAP - ring_count(&g_tx); }

/* ---- Part A: 비차단 write / read ---- */
/* 들어가는 만큼만 넣고 넣은 개수를 돌려준다. 넣었으면 TXEIE 를 "항상" 켠다 (이미 켜져 있어도 무해). */
size_t uart_write(const uint8_t *buf, size_t n) {
    /* TODO(Part A): 들어가는 만큼 TX 링에 push, 1개 이상 넣었으면 CR1 |= TXEIE (push 가 먼저) */
    (void)buf; (void)n;
    return 0;
}

size_t uart_read(uint8_t *buf, size_t n) {
    /* TODO(Part A): RX 링에서 최대 n 개 pop */
    (void)buf; (void)n;
    return 0;
}

/* ---- Part A/B: ISR ---- */
void USART2_IRQHandler(void) {
    /* TODO(Part A/B): SR 스냅숏 → isr_calls++ → RXNE/ORE: DR 읽기, ORE·FE 카운트, RXNE 면 RX 링 push (가득: rx_dropped++)
     *                  → TXEIE && TXE: TX 링 pop → DR 쓰기 (tx_bytes++), 비었으면 TXEIE 끄기 (txeie_off++) */
}

/* ---- Part C: flush / log ---- */
/* TX 링이 비고 TC=1 (마지막 stop 비트까지 나감) 이 될 때까지, 최대 max_spins 바퀴. ISR 에서 부르면 안 된다. */
int uart_flush(uint32_t max_spins) {
    /* TODO(Part C): TX 링 비었고 SR.TC=1 이면 UART_OK, 아니면 CPU_IDLE() — 최대 max_spins 바퀴 */
    (void)max_spins;
    return UART_ETIMEOUT;
}

/* 한 줄 로그. 너무 길면 끝을 "~\n" 로 자르고 g_log_trunc++. 링에 한 줄이 통째로 안 들어가면 버리고
 * g_log_dropped++ (반쪽 줄을 남기지 않는다). 성공하면 쓴 바이트 수. */
int uart_log(const char *fmt, ...) {
    /* TODO(Part C): vsnprintf 로 LOG_LINE_MAX 버퍼에 포맷. 잘리면 끝 두 글자를 "~\n" 로, g_log_trunc++.
     * 링 빈 칸 < 길이면 g_log_dropped++ 후 UART_EFULL (반쪽 줄 금지). 아니면 uart_write 로 넣고 길이 반환 */
    (void)fmt;
    return UART_EFULL;
}

/* ---- 주어진 코드: 버그 데모 두 개 (테스트가 증상을 재현한다) ---- */
/* 버그 1: "이미 송신 중이면 TXEIE 를 안 건드린다" — CR1 을 push 보다 먼저 읽어서 생기는 lost wakeup */
size_t uart_write_buggy(const uint8_t *buf, size_t n) {
    uint32_t cr1 = REG_READ(USART_CR1);                   /* "이미 송신 중인가?" */
    size_t i = 0;
    while (i < n && ring_push(&g_tx, buf[i])) i++;
    if (i && !(cr1 & CR1_TXEIE)) REG_WRITE(USART_CR1, cr1 | CR1_TXEIE);   /* 쉬고 있을 때만 깨운다 */
    return i;
}
/* 버그 2: 링이 비어도 TXEIE 를 끄지 않는 ISR — TXE 는 계속 1 이라 인터럽트가 멈추지 않는다 */
void uart_isr_no_txeie_off(void) {
    uint32_t sr = REG_READ(USART_SR);
    g_st.isr_calls++;
    if (sr & (SR_RXNE | SR_ORE)) {
        uint8_t b = (uint8_t)REG_READ(USART_DR);
        if (sr & SR_RXNE) { if (ring_push(&g_rx, b)) g_st.rx_bytes++; else g_st.rx_dropped++; }
    }
    uint8_t b;
    if ((sr & SR_TXE) && ring_pop(&g_tx, &b)) { REG_WRITE(USART_DR, b); g_st.tx_bytes++; }
}

#ifdef UNIT_TEST
/* ================= 5. mock core (BM-B02 mock 축약판) + 접근 횟수 주입 훅 (BM-B04 식) ================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
#define MOCK_MAX_REGS 8
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int        g_nregs;
static uint32_t   g_unexpected, g_nacc, g_inject_at;
static void     (*g_inject_fn)(void);

static mock_reg_t *mock_find(uint32_t a) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == a) return &g_regs[i];
    return NULL;
}
static void mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w) {
    if (g_nregs < MOCK_MAX_REGS) g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, NULL };
}
static void mock_after_access(void) {                     /* k 번째 접근 "직후" 에 인터럽트가 들어온 것처럼 */
    g_nacc++;
    if (g_inject_fn && g_nacc == g_inject_at) { void (*f)(void) = g_inject_fn; g_inject_fn = NULL; f(); }
}
static void mock_inject_after(uint32_t k, void (*f)(void)) { g_inject_at = g_nacc + k; g_inject_fn = f; }
uint32_t mock_read32(uint32_t a) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; return 0; }
    uint32_t v = r->r ? r->r(a, r->val, r->ctx) : r->val;
    mock_after_access();
    return v;
}
void mock_write32(uint32_t a, uint32_t v) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; return; }
    r->val = r->w ? r->w(a, r->val, v, r->ctx) : v;
    mock_after_access();
}
static uint32_t mock_peek(uint32_t a) { mock_reg_t *r = mock_find(a); return r ? r->val : 0; }

/* ================= 6. USART2 모델 (BM-B12 모델 축약판, 시간은 sim_step 만 굴린다) ================= */
#define CAP_CAP 8192
#define SRC_CAP 8192
typedef struct {
    uint32_t tick, frame;                                 /* frame = 10 tick (8N1) */
    uint32_t sr;                                          /* RXNE TC ORE FE 래치 (TXE 는 tdr_full 로 계산) */
    uint8_t  tdr, tsr, rdr; int tdr_full, tsr_busy, sr_armed; uint32_t tsr_left;
    const uint8_t *rx_src; const uint32_t *rx_at; const uint8_t *rx_err; uint32_t rx_n, rx_i;
    uint8_t  lost[SRC_CAP];                               /* 오버런으로 사라진 바이트 표시 */
    uint8_t  cap[CAP_CAP]; uint32_t cap_n, cap_last_tick; /* 선 위로 끝까지 나간 바이트 */
    uint32_t ore_lost, ore_episodes, tx_overwrite, noclk;
} uart_model_t;
static uart_model_t U;

static int u_clk(void) { return (mock_peek(RCC_APB1ENR) & RCC_APB1ENR_USART2EN) != 0; }
static uint32_t u_cr1(void) { return mock_peek(USART_CR1); }
static void tsr_load(void) { U.tsr = U.tdr; U.tdr_full = 0; U.tsr_busy = 1; U.tsr_left = U.frame; }

static void uart_tick(void) {
    U.tick++;
    if (U.tsr_busy && --U.tsr_left == 0) {                /* stop 비트까지 다 나감 */
        if (U.cap_n < CAP_CAP) U.cap[U.cap_n++] = U.tsr;
        U.cap_last_tick = U.tick;
        U.tsr_busy = 0;
        if (U.tdr_full) tsr_load(); else U.sr |= SR_TC;
    }
    while (U.rx_i < U.rx_n && U.rx_at[U.rx_i] <= U.tick) { /* 원격이 보낸 바이트의 stop 비트 도착 */
        uint32_t i = U.rx_i++;
        uint32_t cr1 = u_cr1();
        if (!u_clk() || !(cr1 & CR1_UE) || !(cr1 & CR1_RE)) { U.lost[i] = 1; continue; }
        if (U.sr & SR_RXNE) {                             /* RDR 이 아직 안 비었다 → 새 바이트 유실 */
            if (!(U.sr & SR_ORE)) U.ore_episodes++;
            U.sr |= SR_ORE; U.ore_lost++; U.lost[i] = 1;
        } else { U.rdr = U.rx_src[i]; U.sr |= SR_RXNE | (U.rx_err ? U.rx_err[i] : 0u); }
    }
}
static int uart_irq_level(void) {
    uint32_t cr1 = u_cr1();
    if (!u_clk() || !(cr1 & CR1_UE)) return 0;
    int txe = !U.tdr_full;
    return ((cr1 & CR1_RXNEIE) && (U.sr & (SR_RXNE | SR_ORE))) || ((cr1 & CR1_TXEIE) && txe) ||
           ((cr1 & CR1_TCIE) && (U.sr & SR_TC));
}
static uint32_t sr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)s; (void)c;
    if (!u_clk()) { U.noclk++; return 0; }
    U.sr_armed = 1;
    return U.sr | (U.tdr_full ? 0u : SR_TXE);
}
static uint32_t dr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)s; (void)c;
    if (!u_clk()) { U.noclk++; return 0; }
    U.sr &= ~SR_RXNE;                                     /* rc_r: DR 읽기로 RXNE clear */
    if (U.sr_armed) U.sr &= ~(SR_ORE | SR_FE | SR_NF | SR_IDLE);
    U.sr_armed = 0;
    return U.rdr;
}
static uint32_t dr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    if (!u_clk()) { U.noclk++; return old; }
    if (U.sr_armed) U.sr &= ~SR_TC;                       /* SR 읽기 → DR 쓰기 = TC clear */
    U.sr_armed = 0;
    if (!(u_cr1() & CR1_TE)) return old;
    if (U.tdr_full) U.tx_overwrite++;
    U.tdr = (uint8_t)v; U.tdr_full = 1;
    if (!U.tsr_busy) tsr_load();                          /* 시프트 레지스터가 놀면 즉시 이동 → TXE=1 */
    return old;
}
static uint32_t ctl_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    if (!u_clk()) { U.noclk++; return old; }
    return v;
}

/* ================= 7. NVIC-lite + 이벤트 루프 ================= */
#define NIRQ 96
typedef void (*isr_fn)(void);
static uint8_t  nv_line[NIRQ], nv_en[NIRQ];
static isr_fn   nv_vec[NIRQ];
static uint32_t nv_calls[NIRQ], nv_mask_from, nv_mask_to;   /* tick ∈ [from,to) 동안 디스패치 보류 */
static int      nv_in_isr;
void irq_line_set(int irqn, int level) { if (irqn >= 0 && irqn < NIRQ) nv_line[irqn] = (uint8_t)(level != 0); }
void nvic_lite_enable(int irqn) { if (irqn >= 0 && irqn < NIRQ) nv_en[irqn] = 1; }
static void nvic_dispatch(void) {                         /* 레벨 감지: 선이 높고 enable 이면 ISR 한 번 */
    irq_line_set(USART2_IRQn, uart_irq_level());
    if (nv_in_isr || (U.tick >= nv_mask_from && U.tick < nv_mask_to)) return;
    for (int i = 0; i < NIRQ; i++)
        if (nv_line[i] && nv_en[i] && nv_vec[i]) { nv_in_isr = 1; nv_calls[i]++; nv_vec[i](); nv_in_isr = 0; return; }
}
void sim_step(void) { uart_tick(); nvic_dispatch(); }     /* 1 tick: 주변장치 먼저, 그다음 인터럽트 */
static void sim_run(uint32_t n) { while (n--) sim_step(); }

static void setup(void) {
    g_nregs = 0; g_unexpected = 0; g_nacc = 0; g_inject_fn = NULL;
    memset(&U, 0, sizeof U);
    U.frame = 10; U.sr = SR_TC;                           /* SR 리셋값 0x00C0: TXE=1, TC=1 */
    memset(nv_line, 0, sizeof nv_line); memset(nv_en, 0, sizeof nv_en);
    memset(nv_vec, 0, sizeof nv_vec); memset(nv_calls, 0, sizeof nv_calls);
    nv_mask_from = nv_mask_to = 0; nv_in_isr = 0;
    nv_vec[USART2_IRQn] = USART2_IRQHandler;
    mock_add_reg(RCC_APB1ENR, 0, NULL, NULL);
    mock_add_reg(USART_SR, 0, sr_rd, NULL);
    mock_add_reg(USART_DR, 0, dr_rd, dr_wr);
    mock_add_reg(USART_BRR, 0, NULL, ctl_wr);
    mock_add_reg(USART_CR1, 0, NULL, ctl_wr);
    uart_init(42000000u, 115200u);
}

/* 원격 송신기: n 바이트를 start 이후 gap[i] 만큼 쉬면서 보낸다 (gap NULL = 붙여서). 도착 tick 을 계산해 둔다 */
static uint8_t  s_src[SRC_CAP];
static uint32_t s_at[SRC_CAP];
static void rx_feed(uint32_t n, const uint32_t *gap) {
    uint32_t t = U.tick;
    for (uint32_t i = 0; i < n; i++) { t += U.frame + (gap ? gap[i] : 0u); s_at[i] = t; }
    U.rx_src = s_src; U.rx_at = s_at; U.rx_err = NULL; U.rx_n = n; U.rx_i = 0;
    memset(U.lost, 0, sizeof U.lost);
}

/* ================= 8. 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs = 0xB0404u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static void inj_one_isr(void) {                           /* "여기서 ISR 한 번" — 일어날 때까지 시간을 굴린다 */
    uint32_t c0 = nv_calls[USART2_IRQn];
    for (int i = 0; i < 200 && nv_calls[USART2_IRQn] == c0; i++) sim_step();
}
static void inj_until_idle(void) {                        /* "여기서 ISR 들이 TX 를 다 끝내고 TXEIE 를 끔" */
    for (int i = 0; i < 500 && (u_cr1() & CR1_TXEIE); i++) sim_step();
}
/* 링에 마지막 바이트가 TDR 로 빠졌지만 TXEIE 는 아직 켜진 순간을 만든다 */
static void prep_tx_tail(void) {
    setup();
    uart_write((const uint8_t *)"AB", 2);
    for (int i = 0; i < 100 && ring_count(&g_tx) != 0; i++) sim_step();
}
static int tx_idle(void) { return ring_count(&g_tx) == 0 && !U.tsr_busy && !U.tdr_full; }

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    uint8_t rb[256];

    /* ---- Part A: init ---- */
    setup();
    CHECK(mock_peek(USART_CR1) == (CR1_UE | CR1_TE | CR1_RE | CR1_RXNEIE) && mock_peek(USART_BRR) == 0x16Du);
    CHECK(nv_en[USART2_IRQn] == 1 && g_unexpected == 0);

    /* ---- Part A: 링 버퍼 — 기준 모델과 무작위 2만 연산 비교, 2^32 넘김 포함 ---- */
    {
        int bad = 0;
        uint8_t ref[TX_CAP]; uint32_t rh = 0, rt = 0;     /* 기준: 단순 배열 큐 (% 연산) */
        g_tx.head = g_tx.tail = 0xFFFFFFC0u;              /* 2^32 근처에서 출발 */
        for (int i = 0; i < 20000; i++) {
            uint8_t v = (uint8_t)xorshift32(), o = 0;
            if (xorshift32() & 1u) {
                int ok = ring_push(&g_tx, v), rok = rh - rt < TX_CAP;
                if (rok) ref[rh++ % TX_CAP] = v;
                if (ok != rok) bad++;
            } else {
                int ok = ring_pop(&g_tx, &o), rok = rh != rt;
                if (ok != rok || (rok && o != ref[rt++ % TX_CAP])) bad++;
            }
            if (ring_count(&g_tx) != rh - rt) bad++;
        }
        CHECK(bad == 0 && g_tx.head < 0x80000000u);       /* head 가 실제로 2^32 를 넘어 0 근처로 */
        uint32_t n = 0; uint8_t o;
        while (ring_pop(&g_tx, &o)) {}
        while (n < 1000 && ring_push(&g_tx, 0x55)) n++;
        CHECK(n == TX_CAP && ring_count(&g_tx) == TX_CAP && !ring_push(&g_tx, 1));
        while (n < 2000 && ring_pop(&g_tx, &o)) n++;
        CHECK(n == 2 * TX_CAP && ring_count(&g_tx) == 0 && !ring_pop(&g_tx, &o));
    }

    /* ---- Part A: TX — write 는 즉시 돌아오고 ISR 가 보낸다 ---- */
    setup();
    CHECK(uart_write((const uint8_t *)"HELLO", 5) == 5);
    CHECK((u_cr1() & CR1_TXEIE) && U.cap_n == 0);         /* 아직 한 비트도 안 나갔다 */
    sim_run(60);
    CHECK(U.cap_n == 5 && memcmp(U.cap, "HELLO", 5) == 0 && U.tx_overwrite == 0);
    CHECK(!(u_cr1() & CR1_TXEIE) && (U.sr & SR_TC) && g_st.tx_bytes == 5 && g_st.txeie_off == 1);
    CHECK(g_st.isr_calls == 6);                           /* 바이트 5번 + "비었음 → TXEIE 끔" 1번 */
    sim_run(1000);
    CHECK(g_st.isr_calls == 6);                           /* 쉬는 동안 인터럽트 0번 */
    setup();
    {
        uint8_t big[200];
        for (int i = 0; i < 200; i++) big[i] = (uint8_t)i;
        CHECK(uart_write(big, 200) == TX_CAP);            /* 비차단: 들어간 만큼만 */
        CHECK(uart_write(big, 10) == 0 && uart_tx_free() == 0);
        CHECK(uart_write(NULL, 3) == 0 && uart_write(big, 0) == 0);
        sim_run(TX_CAP * 10 + 20);
        CHECK(U.cap_n == TX_CAP && memcmp(U.cap, big, TX_CAP) == 0);
    }

    /* ---- Part A: RX — ISR 가 링에 넣고 main 이 꺼낸다 ---- */
    setup();
    memcpy(s_src, "abc", 3);
    rx_feed(3, NULL);
    sim_run(35);
    CHECK(uart_read(rb, sizeof rb) == 3 && memcmp(rb, "abc", 3) == 0);
    CHECK(g_st.rx_bytes == 3 && g_st.isr_calls == 3 && uart_read(rb, sizeof rb) == 0 && uart_read(NULL, 1) == 0);

    /* ---- Part B: RX 링 가득 — drop-newest, 카운터 정확 ---- */
    setup();
    for (int i = 0; i < 100; i++) s_src[i] = (uint8_t)(0x80 + i);
    rx_feed(100, NULL);
    sim_run(100 * 10 + 20);                               /* main 은 그동안 한 번도 안 읽는다 */
    CHECK(g_st.rx_bytes == RX_CAP && g_st.rx_dropped == 100 - RX_CAP && g_st.ore == 0 && U.ore_lost == 0);
    CHECK(uart_read(rb, sizeof rb) == RX_CAP && memcmp(rb, s_src, RX_CAP) == 0);   /* 앞의 64개가 남는다 */

    /* ---- Part B: ISR 가 늦으면 ORE — 마스크 K tick 동안 잃는 바이트 = K/10 ---- */
    {
        int bad = 0;
        for (uint32_t K = 0; K <= 100; K++) {
            setup();
            for (int i = 0; i < 20; i++) s_src[i] = (uint8_t)('A' + i);
            rx_feed(20, NULL);
            nv_mask_from = s_at[0]; nv_mask_to = s_at[0] + K;   /* 첫 바이트 도착부터 K tick 동안 ISR 못 들어감 */
            sim_run(20 * 10 + 120);
            size_t got = uart_read(rb, sizeof rb);
            uint32_t q = K / 10, j = 0, ok = 1;
            for (uint32_t i = 0; i < 20; i++) {
                if (i >= 1 && i <= q) { if (!U.lost[i]) ok = 0; continue; }   /* 1..K/10 번째가 사라진다 */
                if (j >= got || rb[j++] != s_src[i]) ok = 0;
            }
            if (!ok || j != got || U.ore_lost != q || g_st.ore != (q ? 1u : 0u) || (U.sr & SR_ORE)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- Part B: TXE 폭풍 — 빈 링에서 TXEIE 를 안 끄면 ---- */
    setup();
    nv_vec[USART2_IRQn] = uart_isr_no_txeie_off;
    uart_write((const uint8_t *)"HELLO", 5);
    sim_run(1060);
    uint32_t storm = g_st.isr_calls;
    CHECK(U.cap_n == 5 && storm >= 1000);                 /* 데이터는 나가지만 CPU 는 ISR 에 갇힌다 */

    /* ---- Part B: lost wakeup — 접근 k 번째 직후 ISR 주입, k 전수 ---- */
    {
        int buggy_stuck = 0, fixed_bad = 0, fired = 0;
        void (*inj[2])(void) = { inj_one_isr, inj_until_idle };
        for (int w = 0; w < 2; w++)
            for (uint32_t k = 0; k <= 3; k++)
                for (int which = 0; which < 2; which++) {
                    prep_tx_tail();
                    if (k) mock_inject_after(k, inj[w]);
                    size_t r = which ? uart_write_buggy((const uint8_t *)"CD", 2)
                                     : uart_write((const uint8_t *)"CD", 2);
                    if (k && !g_inject_fn) fired++;
                    g_inject_fn = NULL;
                    sim_run(300);
                    int ok = r == 2 && U.cap_n == 4 && memcmp(U.cap, "ABCD", 4) == 0 && ring_count(&g_tx) == 0;
                    if (which) buggy_stuck += !ok; else fixed_bad += !ok;
                    if (verbose && which && !ok)
                        printf("  [lost wakeup] inject=%s k=%u: captured=\"%.*s\" tx_ring=%u TXEIE=%u\n",
                               w ? "until_idle" : "one_isr", (unsigned)k, (int)U.cap_n, (const char *)U.cap,
                               (unsigned)ring_count(&g_tx), (unsigned)((u_cr1() & CR1_TXEIE) != 0));
                }
        CHECK(fixed_bad == 0);
        CHECK(buggy_stuck == 2);                          /* 버그: k=1 (CR1 읽기 직후) 에서만, 두 주입 방식 모두 */
        CHECK(fired == 6);                                /* 주입이 실제로 일어난 경우: 고친 k=1,2 / 버그 k=1, ×2 */
    }
    /* 고친 버전에서 CR1 RMW 사이에 ISR 가 TXEIE 를 껐다면? → 다시 켜지고 빈 TXE 인터럽트 1번 더, 데이터는 무사 */
    prep_tx_tail();
    mock_inject_after(1, inj_until_idle);                 /* push 뒤 첫 접근 = CR1 읽기 */
    uart_write((const uint8_t *)"CD", 2);
    CHECK(u_cr1() & CR1_TXEIE);                           /* 낡은 CR1 값을 써서 다시 켜짐 */
    sim_run(50);
    CHECK(U.cap_n == 4 && memcmp(U.cap, "ABCD", 4) == 0 && g_st.txeie_off == 2 && !(u_cr1() & CR1_TXEIE));

    /* ---- Part C: echo 5000 바이트, main 은 무작위로 바쁨, ISR 는 무작위로 늦음 ---- */
    for (int pass = 0; pass < 2; pass++) {
        static uint32_t gap[SRC_CAP];
        setup();
        uint32_t n = 5000, main_busy_until = 0, masked = 0;
        for (uint32_t i = 0; i < n; i++) { s_src[i] = (uint8_t)xorshift32(); gap[i] = (xorshift32() % 4u == 0) ? xorshift32() % 30u : 0u; }
        rx_feed(n, gap);
        int quiet = 0;
        for (uint32_t t = 0; t < 200000u; t++) {
            if (U.rx_i == n && ring_count(&g_rx) == 0 && tx_idle()) { quiet = 1; break; }
            if (U.tick >= main_busy_until) {              /* main 루프 한 바퀴: RX → TX 로 echo */
                uint32_t room = uart_tx_free();
                size_t k = uart_read(rb, room < sizeof rb ? room : sizeof rb);
                if (k) uart_write(rb, k);
                if (xorshift32() % 200u == 0) main_busy_until = U.tick + xorshift32() % 300u;   /* 바쁜 구간 */
            }
            if (pass == 1 && xorshift32() % 400u == 0 && U.tick >= nv_mask_to) {   /* 긴 임계구역 */
                nv_mask_from = U.tick; nv_mask_to = U.tick + 5u + xorshift32() % 40u; masked++;
            }
            sim_step();
        }
        uint32_t j = 0, ok = quiet;
        for (uint32_t i = 0; i < n && ok; i++) { if (U.lost[i]) continue; if (j >= U.cap_n || U.cap[j++] != s_src[i]) ok = 0; }
        CHECK(ok && j == U.cap_n && g_st.rx_dropped == 0 && U.tx_overwrite == 0);
        if (pass == 0) CHECK(U.cap_n == n && U.ore_lost == 0 && g_st.ore == 0);
        else CHECK(U.ore_lost > 0 && U.cap_n == n - U.ore_lost && g_st.ore == U.ore_episodes);
        if (verbose)
            printf("[echo %s] sent=%u echoed=%u ore_lost=%u ore_events=%u isr_calls=%u (%.2f/byte) ticks=%u masks=%u\n",
                   pass ? "+ISR 지연" : "기본", (unsigned)n, (unsigned)U.cap_n, (unsigned)U.ore_lost,
                   (unsigned)g_st.ore, (unsigned)g_st.isr_calls, (double)g_st.isr_calls / n, (unsigned)U.tick,
                   (unsigned)masked);
    }

    /* ---- Part C: flush 는 TC 까지, log 는 한 줄 통째로 ---- */
    setup();
    {
        uint32_t t0 = U.tick;
        CHECK(uart_write((const uint8_t *)"OK", 2) == 2);
        CHECK(uart_flush(1000) == UART_OK && U.tick - t0 == 21 && U.cap_n == 2 && U.cap_last_tick == U.tick);
        CHECK(uart_flush(1000) == UART_OK && U.tick - t0 == 21);   /* 이미 끝났으면 바로 */
    }
    setup();
    {
        const char *l1 = "[000012] boot BM-M04 rst=PIN\n", *l2 = "[000345] rx=0 drop=0 ore=0\n";
        const char *l3 = "[000678] this line is much too long for a 48 byte log buffer\n";
        char expect[160];
        int a = uart_log("[%06u] boot %s rst=%s\n", 12u, "BM-M04", "PIN");
        int b = uart_log("[%06u] rx=%u drop=%u ore=%u\n", 345u, 0u, 0u, 0u);
        int c = uart_log("[%06u] %s\n", 678u, "this line is much too long for a 48 byte log buffer");
        snprintf(expect, sizeof expect, "%s%s%.45s~\n", l1, l2, l3);   /* 47자 = 앞 45자 + "~\n" */
        CHECK(a == (int)strlen(l1) && b == (int)strlen(l2) && c == 47 && g_log_trunc == 1);
        CHECK(uart_flush(5000) == UART_OK && U.cap_n == strlen(expect) && memcmp(U.cap, expect, U.cap_n) == 0);
        if (verbose) printf("[log] captured:\n%.*s", (int)U.cap_n, (const char *)U.cap);
    }
    setup();
    {
        uint8_t fill[TX_CAP - 4];                         /* 빈 칸 4개만 남긴다 */
        memset(fill, '.', sizeof fill);
        CHECK(uart_write(fill, sizeof fill) == sizeof fill);
        CHECK(uart_log("[%06u] tick\n", 1u) == UART_EFULL && g_log_dropped == 1 && ring_count(&g_tx) == TX_CAP - 4);
        CHECK(uart_flush(5000) == UART_OK && U.cap_n == TX_CAP - 4);         /* 반쪽 줄 없음 */
        CHECK(uart_log("[%06u] tick\n", 2u) == 14 && uart_flush(5000) == UART_OK && U.cap_n == TX_CAP - 4 + 14);
    }
    setup();
    nv_en[USART2_IRQn] = 0;                               /* NVIC_EnableIRQ 를 잊었다 */
    {
        uint32_t t0 = U.tick;
        CHECK(uart_write((const uint8_t *)"X", 1) == 1 && (u_cr1() & CR1_TXEIE));
        CHECK(uart_flush(1000) == UART_ETIMEOUT && U.tick - t0 == 1000 && U.cap_n == 0 && g_st.isr_calls == 0);
    }
    CHECK(g_unexpected == 0 && U.noclk == 0);

    if (verbose) {
        printf("[TXE 폭풍] 고친 ISR: \"HELLO\" + 1000 tick 쉼 → ISR 6번 / TXEIE 안 끄는 ISR → %u번\n", (unsigned)storm);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
