/* BM-B12 폴링 UART 드라이버: baud divisor와 상태 플래그
 *
 * 호스트 빌드 (UNIT_TEST): 드라이버는 REG_READ/REG_WRITE 로만 레지스터를 만진다.
 *   그 매크로가 mock_read32/mock_write32 로 가서 USART 모델(TX 시프트 레지스터, RX 주입 큐)을 돌린다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B12_so && /tmp/BM-B12_so [-v]
 * 타깃 빌드: 맨 위 #define UNIT_TEST 를 지우면 같은 드라이버가 volatile MMIO 로 컴파일된다
 *   (mock/모델/하네스는 #ifdef UNIT_TEST 안에 있다).
 * 주소·비트: STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 파일의 모델 정의다.
 */
#define UNIT_TEST 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 1. 레지스터 접근 seam (BM-B02 mock 축약판) ================= */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* ================= 2. 레지스터 정의 (RM0090 기준 대표값) ================= */
#define RCC_APB1ENR           0x40023840u
#define RCC_APB1ENR_USART2EN  (1u << 17)
#define USART2_BASE           0x40004400u
#define USART_SR(b)           ((b) + 0x00u)
#define USART_DR(b)           ((b) + 0x04u)
#define USART_BRR(b)          ((b) + 0x08u)
#define USART_CR1(b)          ((b) + 0x0Cu)
#define USART_CR2(b)          ((b) + 0x10u)
#define USART_CR3(b)          ((b) + 0x14u)
#define SR_PE    (1u << 0)
#define SR_FE    (1u << 1)
#define SR_NF    (1u << 2)
#define SR_ORE   (1u << 3)
#define SR_IDLE  (1u << 4)
#define SR_RXNE  (1u << 5)    /* rc_w0, DR 읽기로 clear                    */
#define SR_TC    (1u << 6)    /* rc_w0, SR 읽기 → DR 쓰기 시퀀스로 clear     */
#define SR_TXE   (1u << 7)    /* r, DR 쓰기로 clear                         */
#define CR1_RE    (1u << 2)
#define CR1_TE    (1u << 3)
#define CR1_UE    (1u << 13)
#define CR1_OVER8 (1u << 15)
#define GPIOB_BSRR      0x40020418u   /* 보드: PB0 = PWR_HOLD (자기유지 전원), reset → 전원 차단 */
#define PWR_HOLD_PIN    0u

/* ================= 3. 드라이버 ================= */
enum { UART_OK = 0, UART_EINVAL = -1, UART_ETIMEOUT = -2, UART_EOVERRUN = -3,
       UART_EFRAME = -4, UART_ENOISE = -5, UART_EPARITY = -6 };
#define UART_TX_TIMEOUT 1000u          /* 폴링 횟수 상한 — 어떤 대기 루프도 무한히 돌지 않는다 */

typedef struct {
    uint32_t brr;          /* BRR 에 쓸 값                                      */
    uint32_t mantissa;     /* DIV_Mantissa (12비트)                             */
    uint32_t fraction;     /* DIV_Fraction (OVER8=0: 4비트, OVER8=1: 3비트)      */
    uint32_t div;          /* round(fck/baud) = USARTDIV × 8×(2−OVER8)          */
    uint32_t actual_baud;  /* round(fck / div)                                  */
    int32_t  err_ppm;      /* (actual − baud)/baud × 1e6, 반올림                 */
} uart_brr_t;

/* ---- Part A: BRR 계산 ---- */
int uart_brr_compute(uint32_t fck, uint32_t baud, int over8, uart_brr_t *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!out || fck == 0 || baud == 0) return UART_EINVAL;
    uint32_t ovs = over8 ? 8u : 16u;
    uint64_t div = (2ull * fck + baud) / (2ull * baud);         /* round half up (fck/baud) */
    if (div < ovs || div / ovs > 0xFFFu) return UART_EINVAL;    /* USARTDIV < 1 또는 mantissa 12비트 초과 */
    out->div = (uint32_t)div;
    out->mantissa = (uint32_t)(div / ovs);                      /* 정수부: carry 는 여기서 자동 */
    out->fraction = (uint32_t)(div % ovs);                      /* OVER8=1 이면 0..7 → bit3 = 0 */
    out->brr = (out->mantissa << 4) | out->fraction;
    out->actual_baud = (uint32_t)((2ull * fck + div) / (2ull * div));
    int64_t num = (int64_t)fck - (int64_t)baud * (int64_t)div;  /* fck/div − baud 의 분자 */
    int64_t den = (int64_t)baud * (int64_t)div;
    int64_t a = (num < 0 ? -num : num) * 1000000;
    int64_t ppm = (a + den / 2) / den;
    out->err_ppm = (int32_t)(num < 0 ? -ppm : ppm);
    return UART_OK;
}

/* 8N1, TX+RX. 순서 (RM0090 송신 절차 기준): 클럭 → UE → (M/STOP) → BRR → TE/RE */
int uart_init(uint32_t base, uint32_t fck, uint32_t baud, int over8) {
    uart_brr_t b;
    if (base != USART2_BASE) return UART_EINVAL;     /* 클럭 표는 USART2 만 (BM-B03 이 일반화) */
    int rc = uart_brr_compute(fck, baud, over8, &b);
    if (rc != UART_OK) return rc;                    /* 계산 실패면 레지스터를 하나도 안 건드린다 */
    uint32_t ovr = over8 ? CR1_OVER8 : 0u;
    REG_WRITE(RCC_APB1ENR, REG_READ(RCC_APB1ENR) | RCC_APB1ENR_USART2EN);
    (void)REG_READ(RCC_APB1ENR);                     /* 클럭 enable 직후 read-back (BM-B03) */
    REG_WRITE(USART_CR1(base), CR1_UE | ovr);        /* M=0(8비트), PCE=0(패리티 없음) */
    REG_WRITE(USART_CR2(base), 0u);                  /* STOP=00: 1 stop bit */
    REG_WRITE(USART_BRR(base), b.brr);               /* TE/RE 가 꺼진 동안 BRR 을 쓴다 */
    REG_WRITE(USART_CR1(base), CR1_UE | ovr | CR1_TE | CR1_RE);
    return UART_OK;
}

/* ---- Part B: putc / getc ---- */
int uart_putc(uint32_t base, uint8_t c) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++) {
        if (REG_READ(USART_SR(base)) & SR_TXE) {     /* TDR 가 비었나? */
            REG_WRITE(USART_DR(base), c);            /* SR 읽기 → DR 쓰기: TXE·TC clear */
            return UART_OK;
        }
    }
    return UART_ETIMEOUT;
}

int uart_getc(uint32_t base, uint8_t *out, uint32_t timeout_polls) {
    if (!out) return UART_EINVAL;
    for (uint32_t i = 0; i < timeout_polls; i++) {
        uint32_t sr = REG_READ(USART_SR(base));      /* 스냅숏 한 번 — DR 을 읽으면 오류 비트가 지워진다 */
        if (sr & SR_RXNE) {
            *out = (uint8_t)REG_READ(USART_DR(base)); /* SR 읽기 → DR 읽기: RXNE·ORE·FE·NF·PE clear */
            if (sr & SR_ORE) return UART_EOVERRUN;   /* 이 바이트는 유효, 그 뒤 바이트(들)가 사라짐 */
            if (sr & SR_FE)  return UART_EFRAME;
            if (sr & SR_NF)  return UART_ENOISE;
            if (sr & SR_PE)  return UART_EPARITY;
            return UART_OK;
        }
    }
    return UART_ETIMEOUT;
}

/* ---- Part C: write / flush ---- */
int uart_write(uint32_t base, const uint8_t *buf, size_t n) {
    if (!buf && n) return UART_EINVAL;
    for (size_t i = 0; i < n; i++) {
        int rc = uart_putc(base, buf[i]);
        if (rc != UART_OK) return rc;
    }
    return (int)n;
}

int uart_flush(uint32_t base) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++)
        if (REG_READ(USART_SR(base)) & SR_TC) return UART_OK;   /* 마지막 stop 비트까지 나감 */
    return UART_ETIMEOUT;
}

/* 주어진 코드: 보드 전원 차단 (자기유지 회로의 PWR_HOLD 핀을 내린다) */
static void board_power_off(void) { REG_WRITE(GPIOB_BSRR, 1u << (16u + PWR_HOLD_PIN)); }

/* 틀린 버전 비교용 (주어진 코드): TXE 만 기다림 */
static int uart_wait_txe(uint32_t base) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++)
        if (REG_READ(USART_SR(base)) & SR_TXE) return UART_OK;
    return UART_ETIMEOUT;
}

int app_report_and_power_off(const char *msg, int wait_tc) {
    int rc = uart_write(USART2_BASE, (const uint8_t *)msg, strlen(msg));
    if (rc < 0) return rc;
    rc = wait_tc ? uart_flush(USART2_BASE) : uart_wait_txe(USART2_BASE);
    board_power_off();
    return rc;
}

#ifdef UNIT_TEST
/* ================= 4. mock core (BM-B02 mock 축약판) ================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 16
#define MOCK_LOG_CAP  64                     /* 링: 마지막 64개 접근만 보관 */
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int        g_nregs;
static mock_log_t g_log[MOCK_LOG_CAP];
static uint32_t   g_log_n, g_unexpected;

static mock_reg_t *mock_find(uint32_t a) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == a) return &g_regs[i];
    return NULL;
}
static void mock_reset(void) { g_nregs = 0; g_log_n = 0; g_unexpected = 0; }
static int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS) return -1;
    g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return 0;
}
static void mock_log(char op, uint32_t a, uint32_t v) { g_log[g_log_n++ % MOCK_LOG_CAP] = (mock_log_t){ op, a, v }; }
static const mock_log_t *mock_log_back(uint32_t k) {     /* k=0: 가장 최근 접근 */
    static const mock_log_t none = { '-', 0, 0 };
    return (k < g_log_n && k < MOCK_LOG_CAP) ? &g_log[(g_log_n - 1 - k) % MOCK_LOG_CAP] : &none;
}
uint32_t mock_read32(uint32_t a) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; mock_log('?', a, 0); return 0; }   /* 모르는 주소: 크래시 대신 기록 */
    uint32_t v = r->r ? r->r(a, r->val, r->ctx) : r->val;
    mock_log('R', a, v);
    return v;
}
void mock_write32(uint32_t a, uint32_t v) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; mock_log('!', a, v); return; }
    r->val = r->w ? r->w(a, r->val, v, r->ctx) : v;
    mock_log('W', a, v);
}
static uint32_t mock_peek(uint32_t a) { mock_reg_t *r = mock_find(a); return r ? r->val : 0; }

/* ================= 5. USART2 모델 ================= */
/* 시간: 1 tick = 1 비트 시간. 8N1 한 프레임 = frame_ticks(10) tick.
 * SR 을 한 번 읽을 때마다 1 tick 이 흐른다 (폴링 루프가 시간을 굴린다). 하네스는 mock_tick(n) 으로도 흘린다. */
#define RXQ_CAP 32
#define CAP_CAP 128
#define EV_CAP  32
typedef struct { uint32_t tick; char kind; uint8_t byte; } uart_ev_t;   /* -v 출력용 사건 기록 */
typedef struct {
    uint32_t tick, frame_ticks;
    uint32_t sr;                         /* RXNE TC ORE FE NF PE 래치 (TXE 는 tdr_full 로 계산) */
    uint8_t  tdr; int tdr_full;          /* 송신 데이터 레지스터 (DR 쓰기가 들어가는 곳) */
    uint8_t  tsr; int tsr_busy; uint32_t tsr_left;   /* 송신 시프트 레지스터 */
    uint8_t  rdr;                        /* 수신 데이터 레지스터 */
    uint8_t  rxq[RXQ_CAP], rxq_err[RXQ_CAP]; uint32_t rxq_h, rxq_t, rx_left;
    int      sr_armed;                   /* "SR 을 읽었다" — 다음 DR 접근이 clear 시퀀스 완성 */
    int      powered;
    uint8_t  cap[CAP_CAP]; uint32_t cap_n;           /* 선 위로 끝까지 나간 바이트 */
    uint32_t tx_overwrite, tx_dropped, noclk, brr_while_on, ore_lost, aborted_frames, aborted_bits;
    uart_ev_t ev[EV_CAP]; uint32_t ev_n;
} uart_model_t;
static uart_model_t U;

static void ev_add(char k, uint8_t b) { if (U.ev_n < EV_CAP) U.ev[U.ev_n++] = (uart_ev_t){ U.tick, k, b }; }
static int  uart_clk(void) { return (mock_peek(RCC_APB1ENR) & RCC_APB1ENR_USART2EN) != 0; }
static uint32_t uart_cr1(void) { return mock_peek(USART_CR1(USART2_BASE)); }
static void tsr_load(void) {
    U.tsr = U.tdr; U.tdr_full = 0; U.tsr_busy = 1; U.tsr_left = U.frame_ticks;
    ev_add('L', U.tsr);                                  /* TDR → 시프트 레지스터, TXE=1 */
}
static void uart_step(void) {
    U.tick++;
    if (!U.powered) return;
    if (U.tsr_busy && --U.tsr_left == 0) {               /* stop 비트까지 다 나감 */
        if (U.cap_n < CAP_CAP) U.cap[U.cap_n++] = U.tsr;
        U.tsr_busy = 0;
        ev_add('D', U.tsr);
        if (U.tdr_full) tsr_load();                      /* 다음 바이트가 바로 이어짐 */
        else { U.sr |= SR_TC; ev_add('T', 0); }          /* TDR 도 비었음 → TC=1 */
    }
    if (U.rx_left > 0 && --U.rx_left == 0) {             /* RX 프레임 하나 도착 */
        uint8_t b = U.rxq[U.rxq_h % RXQ_CAP], e = U.rxq_err[U.rxq_h % RXQ_CAP];
        U.rxq_h++;
        uint32_t cr1 = uart_cr1();
        if (!uart_clk() || !(cr1 & CR1_UE) || !(cr1 & CR1_RE)) { /* 수신기 꺼짐: 버려짐 */ }
        else if (U.sr & SR_RXNE) { U.sr |= SR_ORE; U.ore_lost++; }  /* RDR 은 옛 값 유지, 새 바이트 유실 */
        else { U.rdr = b; U.sr |= SR_RXNE | e; }
        if (U.rxq_h != U.rxq_t) U.rx_left = U.frame_ticks;
    }
}
static void mock_tick(uint32_t n) { while (n--) uart_step(); }
static void mock_uart_rx_inject(const uint8_t *b, uint32_t n, uint32_t err) {
    for (uint32_t i = 0; i < n && U.rxq_t - U.rxq_h < RXQ_CAP; i++) {
        U.rxq[U.rxq_t % RXQ_CAP] = b[i]; U.rxq_err[U.rxq_t % RXQ_CAP] = (uint8_t)err; U.rxq_t++;
    }
    if (U.rx_left == 0 && U.rxq_h != U.rxq_t) U.rx_left = U.frame_ticks;
}

static uint32_t sr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)s; (void)c;
    uart_step();                                         /* 폴링 1회 = 1 tick */
    if (!uart_clk()) { U.noclk++; return 0; }
    U.sr_armed = 1;
    return U.sr | (U.tdr_full ? 0u : SR_TXE);
}
static uint32_t sr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    if (!uart_clk()) { U.noclk++; return old; }
    U.sr &= ~(~v & (SR_RXNE | SR_TC));                   /* rc_w0: 0 을 쓴 RXNE/TC 만 clear, 나머지는 r */
    return old;
}
static uint32_t dr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)s; (void)c;
    if (!uart_clk()) { U.noclk++; return 0; }
    U.sr &= ~SR_RXNE;                                    /* rc_r: 읽으면 RXNE clear */
    if (U.sr_armed) U.sr &= ~(SR_ORE | SR_FE | SR_NF | SR_PE | SR_IDLE);
    U.sr_armed = 0;
    return U.rdr;
}
static uint32_t dr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    if (!uart_clk()) { U.noclk++; return old; }
    if (U.sr_armed) U.sr &= ~SR_TC;                      /* SR 읽기 → DR 쓰기 = TC clear 시퀀스 */
    U.sr_armed = 0;
    uint32_t cr1 = uart_cr1();
    if (!(cr1 & CR1_UE) || !(cr1 & CR1_TE)) { U.tx_dropped++; return old; }
    if (U.tdr_full) U.tx_overwrite++;                    /* TXE=0 인데 썼다 → 앞 바이트 덮어씀 */
    U.tdr = (uint8_t)v; U.tdr_full = 1;
    if (!U.tsr_busy && U.powered) tsr_load();            /* 시프트 레지스터가 놀면 즉시 이동 → TXE=1 */
    return old;
}
static uint32_t ctl_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)c;
    if (!uart_clk()) { U.noclk++; return 0; }
    return s;
}
static uint32_t ctl_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)c;
    if (!uart_clk()) { U.noclk++; return old; }
    if (a == USART_BRR(USART2_BASE) && (uart_cr1() & (CR1_TE | CR1_RE))) U.brr_while_on++;
    return v;
}
static uint32_t bsrr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)old; (void)c;
    if ((v & (1u << (16u + PWR_HOLD_PIN))) && U.powered) {   /* 전원 차단: 모든 것이 그 자리에서 멈춤 */
        U.powered = 0;
        if (U.tsr_busy) { U.aborted_frames++; U.aborted_bits = U.frame_ticks - U.tsr_left; }
        if (U.tdr_full) U.aborted_frames++;
        ev_add('P', 0);
    }
    return 0;                                            /* BSRR 은 읽으면 0 */
}

static void setup(uint32_t apb1enr) {
    mock_reset();
    memset(&U, 0, sizeof U);
    U.frame_ticks = 10; U.powered = 1; U.sr = SR_TC;     /* SR 리셋값 0x00C0: TXE=1, TC=1 */
    mock_add_reg(RCC_APB1ENR, apb1enr, NULL, NULL, NULL);
    mock_add_reg(USART_SR(USART2_BASE),  0, sr_rd,  sr_wr,  NULL);
    mock_add_reg(USART_DR(USART2_BASE),  0, dr_rd,  dr_wr,  NULL);
    mock_add_reg(USART_BRR(USART2_BASE), 0, ctl_rd, ctl_wr, NULL);
    mock_add_reg(USART_CR1(USART2_BASE), 0, ctl_rd, ctl_wr, NULL);
    mock_add_reg(USART_CR2(USART2_BASE), 0, ctl_rd, ctl_wr, NULL);
    mock_add_reg(USART_CR3(USART2_BASE), 0, ctl_rd, ctl_wr, NULL);
    mock_add_reg(GPIOB_BSRR, 0, NULL, bsrr_wr, NULL);
}

/* ================= 6. 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs = 0x12B12u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static int brr_is(uint32_t fck, uint32_t baud, int over8, uint32_t brr, uint32_t mant, uint32_t frac) {
    uart_brr_t b;
    return uart_brr_compute(fck, baud, over8, &b) == UART_OK && b.brr == brr && b.mantissa == mant && b.fraction == frac;
}

static void print_events(const char *title) {
    printf("%s\n", title);
    for (uint32_t i = 0; i < U.ev_n; i++) {
        const uart_ev_t *e = &U.ev[i];
        if (e->kind == 'L') printf("  tick %3u  LOAD '%c'  TDR→시프트 (TXE=1)\n", (unsigned)e->tick, e->byte);
        if (e->kind == 'D') printf("  tick %3u  DONE '%c'  stop 비트 끝\n", (unsigned)e->tick, e->byte);
        if (e->kind == 'T') printf("  tick %3u  TC=1\n", (unsigned)e->tick);
        if (e->kind == 'P') printf("  tick %3u  POWER OFF\n", (unsigned)e->tick);
    }
    printf("  captured=\"%.*s\" aborted_frames=%u aborted_bits=%u\n",
           (int)U.cap_n, (const char *)U.cap, (unsigned)U.aborted_frames, (unsigned)U.aborted_bits);
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    uart_brr_t b;
    uint8_t c = 0;

    /* ---- Part A: BRR ---- */
    CHECK(uart_brr_compute(42000000u, 115200u, 0, &b) == UART_OK);
    CHECK(b.brr == 0x16Du && b.mantissa == 22u && b.fraction == 13u && b.div == 365u);
    CHECK(b.actual_baud == 115068u && b.err_ppm == -1142);
    CHECK(uart_brr_compute(84000000u, 115200u, 0, &b) == UART_OK && b.brr == 0x2D9u && b.err_ppm == 229);
    CHECK(brr_is(4099200u, 10000u, 0, 0x19Au, 25u, 10u));      /* RM 예: USARTDIV 25.62 → 0x19A */
    CHECK(brr_is(8158400u, 10000u, 0, 0x330u, 51u, 0u));       /* RM 예: 50.99 → frac 15.84→16 → carry → 0x330 */
    CHECK(brr_is(45000000u, 9600u, 0, 0x1250u, 293u, 0u));     /* 4687.5 → 4688: 반올림 동점에서 carry */
    CHECK(brr_is(45000000u, 19200u, 1, 0x1250u, 293u, 0u));    /* OVER8=1: 2343.75 → 2344 → carry */
    CHECK(brr_is(2049600u, 10000u, 1, 0x195u, 25u, 5u));       /* RM 예 OVER8=1: 25.62 → 0x195 */
    CHECK(uart_brr_compute(42000000u, 3000000u, 0, &b) == UART_EINVAL);   /* 14 < 16: USARTDIV < 1 */
    CHECK(uart_brr_compute(42000000u, 3000000u, 1, &b) == UART_OK && b.brr == 0x16u);
    CHECK(uart_brr_compute(42000000u, 600u, 0, &b) == UART_EINVAL);       /* mantissa 4375 > 4095 */
    CHECK(uart_brr_compute(42000000u, 1200u, 0, &b) == UART_OK && b.mantissa == 2187u);
    CHECK(uart_brr_compute(0u, 9600u, 0, &b) == UART_EINVAL && uart_brr_compute(42000000u, 0u, 0, &b) == UART_EINVAL);
    CHECK(uart_brr_compute(42000000u, 9600u, 0, NULL) == UART_EINVAL);
    {   /* 성질 테스트: 무작위 (fck, baud, OVER8) 3000개 — 디코드·최근접·bit3·범위 */
        int bad = 0, okn = 0;
        for (int i = 0; i < 3000; i++) {
            uint32_t fck = 1000000u + xorshift32() % 179000001u;
            uint32_t baud = 300u + xorshift32() % (fck / 6u);
            int over8 = (int)(xorshift32() & 1u);
            uint32_t ovs = over8 ? 8u : 16u;
            uint64_t d2 = (2ull * fck + baud) / (2ull * baud);
            int expect_ok = d2 >= ovs && d2 / ovs <= 0xFFFu;
            int rc = uart_brr_compute(fck, baud, over8, &b);
            if ((rc == UART_OK) != expect_ok) { bad++; continue; }
            if (rc != UART_OK) continue;
            okn++;
            int64_t dist2 = 2ll * b.div * baud - 2ll * fck;                 /* 2·(div − fck/baud)·baud */
            if ((b.brr >> 4) * ovs + (b.brr & 0xFu) != b.div || b.fraction >= ovs ||
                (over8 && (b.brr & 0x8u)) || dist2 > (int64_t)baud || -dist2 > (int64_t)baud) bad++;
        }
        CHECK(bad == 0 && okn > 1000);
    }

    /* ---- Part A: init ---- */
    setup(0x00000001u);                                   /* TIM2EN 이 이미 켜져 있다 */
    CHECK(uart_init(USART2_BASE, 42000000u, 115200u, 0) == UART_OK);
    CHECK(mock_peek(RCC_APB1ENR) == 0x00020001u);          /* USART2EN 추가, 다른 비트 보존 */
    CHECK(mock_peek(USART_BRR(USART2_BASE)) == 0x16Du && mock_peek(USART_CR1(USART2_BASE)) == 0x200Cu);
    CHECK(U.brr_while_on == 0 && g_unexpected == 0 && U.noclk == 0);
    setup(0);
    CHECK(uart_init(USART2_BASE, 42000000u, 600u, 0) == UART_EINVAL && mock_peek(RCC_APB1ENR) == 0 && g_log_n == 0);
    CHECK(uart_init(0x40011000u, 84000000u, 115200u, 0) == UART_EINVAL);
    setup(0);
    CHECK(uart_init(USART2_BASE, 42000000u, 921600u, 1) == UART_OK &&
          mock_peek(USART_CR1(USART2_BASE)) == 0xA00Cu && mock_peek(USART_BRR(USART2_BASE)) == 0x56u);   /* 45.57 → 46 = 5·8 + 6 */

    /* ---- Part B: putc ---- */
    setup(0);
    uart_init(USART2_BASE, 42000000u, 115200u, 0);
    CHECK(uart_putc(USART2_BASE, 'A') == UART_OK);
    CHECK(mock_log_back(0)->op == 'W' && mock_log_back(0)->addr == USART_DR(USART2_BASE) && mock_log_back(0)->val == 'A');
    CHECK(mock_log_back(1)->op == 'R' && mock_log_back(1)->addr == USART_SR(USART2_BASE) && (mock_log_back(1)->val & SR_TXE));
    CHECK(uart_write(USART2_BASE, (const uint8_t *)"HELLO", 5) == 5);
    mock_tick(100);
    CHECK(U.cap_n == 6 && memcmp(U.cap, "AHELLO", 6) == 0 && U.tx_overwrite == 0);
    setup(0);                                             /* init 안 함: 클럭 꺼짐 → TXE 가 0 으로 읽힘 */
    {
        uint32_t t0 = U.tick;
        CHECK(uart_putc(USART2_BASE, 'X') == UART_ETIMEOUT && U.tick - t0 == UART_TX_TIMEOUT && U.cap_n == 0);
    }

    /* ---- Part B: getc ---- */
    setup(0);
    uart_init(USART2_BASE, 42000000u, 115200u, 0);
    {
        uint32_t t0 = U.tick;
        CHECK(uart_getc(USART2_BASE, &c, 50) == UART_ETIMEOUT && U.tick - t0 == 50);
    }
    mock_uart_rx_inject((const uint8_t *)"hi", 2, 0);
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_OK && c == 'h');
    CHECK(mock_log_back(0)->op == 'R' && mock_log_back(0)->addr == USART_DR(USART2_BASE) &&
          mock_log_back(1)->addr == USART_SR(USART2_BASE));                     /* SR → DR 순서 */
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_OK && c == 'i');
    CHECK(uart_getc(USART2_BASE, NULL, 100) == UART_EINVAL);
    mock_uart_rx_inject((const uint8_t *)"abc", 3, 0);
    mock_tick(35);                                        /* main 이 바빠서 35 tick 동안 안 읽음 */
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_EOVERRUN && c == 'a' && U.ore_lost == 2);
    CHECK((U.sr & SR_ORE) == 0);                          /* SR→DR 시퀀스로 ORE clear 됨 */
    CHECK(uart_getc(USART2_BASE, &c, 30) == UART_ETIMEOUT);   /* b, c 는 사라졌다 */
    mock_uart_rx_inject((const uint8_t *)"\0", 1, SR_FE);     /* break/잡음: stop 비트가 0 */
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_EFRAME && c == 0 && (U.sr & SR_FE) == 0);
    mock_uart_rx_inject((const uint8_t *)"z", 1, SR_NF);
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_ENOISE && c == 'z');
    mock_uart_rx_inject((const uint8_t *)"ok", 2, 0);
    CHECK(uart_getc(USART2_BASE, &c, 100) == UART_OK && c == 'o' && uart_getc(USART2_BASE, &c, 100) == UART_OK && c == 'k');

    /* ---- Part C: TXE vs TC 타이밍 ---- */
    setup(0);
    uart_init(USART2_BASE, 42000000u, 115200u, 0);
    {
        uint32_t t0 = U.tick;
        CHECK(uart_write(USART2_BASE, (const uint8_t *)"OK", 2) == 2);
        uint32_t t_w = U.tick - t0;
        CHECK(uart_wait_txe(USART2_BASE) == UART_OK);
        uint32_t t_txe = U.tick - t0;
        CHECK(uart_flush(USART2_BASE) == UART_OK);
        uint32_t t_tc = U.tick - t0;
        CHECK(t_w == 2 && t_txe == 11 && t_tc == 21 && t_tc - t_txe == U.frame_ticks);
        CHECK(U.cap_n == 2 && memcmp(U.cap, "OK", 2) == 0);
        CHECK(uart_flush(USART2_BASE) == UART_OK && U.tick - t0 == 22);   /* 이미 끝났으면 1 폴링 */
    }
    setup(0);
    CHECK(uart_flush(USART2_BASE) == UART_ETIMEOUT);      /* 클럭 꺼짐: 무한루프 대신 timeout */

    /* ---- Part C: 보내고 전원 끄기 ---- */
    const char *msg = "SHUTDOWN OK\r\n";
    size_t n = strlen(msg);
    setup(0);
    uart_init(USART2_BASE, 42000000u, 115200u, 0);
    CHECK(app_report_and_power_off(msg, 0) == UART_OK);   /* TXE 만 기다림 */
    CHECK(U.powered == 0 && U.cap_n == n - 1 && memcmp(U.cap, msg, n - 1) == 0 && U.aborted_frames == 1);
    setup(0);
    uart_init(USART2_BASE, 42000000u, 115200u, 0);
    CHECK(app_report_and_power_off(msg, 1) == UART_OK);   /* TC 까지 기다림 */
    CHECK(U.powered == 0 && U.cap_n == n && memcmp(U.cap, msg, n) == 0 && U.aborted_frames == 0);
    CHECK(g_unexpected == 0 && U.tx_overwrite == 0 && U.brr_while_on == 0);

    if (verbose) {
        setup(0); uart_init(USART2_BASE, 42000000u, 115200u, 0);
        app_report_and_power_off("OK", 0); print_events("[TXE 만 기다리고 전원 차단] \"OK\"");
        setup(0); uart_init(USART2_BASE, 42000000u, 115200u, 0);
        app_report_and_power_off("OK", 1); print_events("[TC 까지 기다리고 전원 차단] \"OK\"");
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
