/* BM-B12 추가 연습 — 한 번에 검증
 *   연습 1: OVER8=0 의 BRR 한 줄 공식, OVER8=1 변환식 (전수 성질 테스트)
 *   연습 2: OVER8 고르기 — 오차는 같고 범위만 다르다
 *   연습 3: uart_puts — '\n' → "\r\n" 변환
 *   연습 4: RS-485 DE 핀 — TC 전에 DE 를 내리면 마지막 바이트가 버스에서 잘린다
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B12_pr && /tmp/BM-B12_pr
 * 주소·비트: STM32F4 (RM0090) 기준 대표값 — RM 확인 권장.
 */
#define UNIT_TEST 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

#define USART2_BASE  0x40004400u
#define USART_SR(b)  ((b) + 0x00u)
#define USART_DR(b)  ((b) + 0x04u)
#define USART_BRR(b) ((b) + 0x08u)
#define USART_CR1(b) ((b) + 0x0Cu)
#define SR_TC     (1u << 6)
#define SR_TXE    (1u << 7)
#define CR1_TE    (1u << 3)
#define CR1_UE    (1u << 13)
#define GPIOA_BSRR 0x40020018u      /* 보드: PA1 = RS-485 트랜시버 DE (1 = 송신) */
#define DE_PIN     1u
enum { UART_OK = 0, UART_EINVAL = -1, UART_ETIMEOUT = -2 };
#define UART_TX_TIMEOUT 1000u

/* ---------- 연습 1: 한 줄 공식 ---------- */
static uint32_t div_round(uint32_t fck, uint32_t baud) { return (uint32_t)((2ull * fck + baud) / (2ull * baud)); }
static uint32_t brr16_oneliner(uint32_t fck, uint32_t baud) { return div_round(fck, baud); }     /* BRR = D 그대로 */
static uint32_t brr8_from_div(uint32_t d) { return ((d >> 3) << 4) | (d & 7u); }                /* bit3 비우고 한 칸 */
static uint32_t brr_reference(uint32_t fck, uint32_t baud, int over8) {     /* 본문 방식: mantissa/fraction */
    uint32_t ovs = over8 ? 8u : 16u, d = div_round(fck, baud);
    return ((d / ovs) << 4) | (d % ovs);
}

/* ---------- 연습 2: OVER8 고르기 ---------- */
/* 반환: 0 또는 1 (OVER8 값), 불가능하면 -1. D 가 같으니 오차도 같다 → 범위가 되면 16배 오버샘플링. */
static int uart_pick_over8(uint32_t fck, uint32_t baud) {
    if (fck == 0 || baud == 0) return -1;
    uint32_t d = div_round(fck, baud);
    if (d >= 16u && d / 16u <= 0xFFFu) return 0;
    if (d >= 8u && d / 8u <= 0xFFFu) return 1;
    return -1;
}

/* ---------- 드라이버 (본문 solution 과 같은 putc/flush) ---------- */
static int uart_putc(uint32_t base, uint8_t c) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++)
        if (REG_READ(USART_SR(base)) & SR_TXE) { REG_WRITE(USART_DR(base), c); return UART_OK; }
    return UART_ETIMEOUT;
}
static int uart_flush(uint32_t base) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++)
        if (REG_READ(USART_SR(base)) & SR_TC) return UART_OK;
    return UART_ETIMEOUT;
}
static int uart_wait_txe(uint32_t base) {
    for (uint32_t i = 0; i < UART_TX_TIMEOUT; i++)
        if (REG_READ(USART_SR(base)) & SR_TXE) return UART_OK;
    return UART_ETIMEOUT;
}

/* ---------- 연습 3: uart_puts ---------- */
/* 반환: 선 위로 보낸 바이트 수 (CR 포함) 또는 음수 오류 */
static int uart_puts(uint32_t base, const char *s) {
    if (!s) return UART_EINVAL;
    int sent = 0;
    for (; *s; s++) {
        if (*s == '\n') { int rc = uart_putc(base, '\r'); if (rc) return rc; sent++; }
        int rc = uart_putc(base, (uint8_t)*s);
        if (rc) return rc;
        sent++;
    }
    return sent;
}

/* ---------- 연습 4: RS-485 반이중 송신 ---------- */
static void de_set(int on) { REG_WRITE(GPIOA_BSRR, on ? (1u << DE_PIN) : (1u << (16u + DE_PIN))); }
static int rs485_send(uint32_t base, const uint8_t *buf, size_t n, int wait_tc) {
    de_set(1);                                           /* 드라이버 켬 (버스 점유) */
    for (size_t i = 0; i < n; i++) { int rc = uart_putc(base, buf[i]); if (rc) { de_set(0); return rc; } }
    int rc = wait_tc ? uart_flush(base) : uart_wait_txe(base);
    de_set(0);                                           /* 수신으로 전환 — TC 뒤여야 한다 */
    return rc;
}

#ifdef UNIT_TEST
/* ---------- mock core (BM-B02 mock 축약판) ---------- */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
#define MOCK_MAX_REGS 8
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int g_nregs;
static uint32_t g_unexpected;
static mock_reg_t *mock_find(uint32_t a) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == a) return &g_regs[i];
    return NULL;
}
static void mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w) {
    if (g_nregs < MOCK_MAX_REGS) g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, r, w, NULL };
}
uint32_t mock_read32(uint32_t a) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; return 0; }
    return r->r ? r->r(a, r->val, r->ctx) : r->val;
}
void mock_write32(uint32_t a, uint32_t v) {
    mock_reg_t *r = mock_find(a);
    if (!r) { g_unexpected++; return; }
    r->val = r->w ? r->w(a, r->val, v, r->ctx) : v;
}

/* ---------- USART TX 모델 + RS-485 버스 ---------- */
typedef struct {
    uint32_t tick, frame_ticks, sr;
    uint8_t tdr; int tdr_full;
    uint8_t tsr; int tsr_busy; uint32_t tsr_left;
    int sr_armed, de, de_whole_frame;                 /* de_whole_frame: 지금 프레임 내내 DE=1 이었나 */
    uint8_t wire[64]; uint32_t wire_n;                /* UART TX 핀에서 나간 바이트 */
    uint8_t bus[64];  uint32_t bus_n;                 /* 버스 반대편이 실제로 받은 바이트 */
    uint32_t truncated, de_fall_tick, tc_tick;
} model_t;
static model_t M;
static void tsr_load(void) { M.tsr = M.tdr; M.tdr_full = 0; M.tsr_busy = 1; M.tsr_left = M.frame_ticks; M.de_whole_frame = M.de; }
static void step(void) {
    M.tick++;
    if (M.tsr_busy && --M.tsr_left == 0) {
        M.wire[M.wire_n++ % 64] = M.tsr;
        if (M.de_whole_frame) M.bus[M.bus_n++ % 64] = M.tsr;
        M.tsr_busy = 0;
        if (M.tdr_full) tsr_load(); else { M.sr |= SR_TC; M.tc_tick = M.tick; }
    }
}
static uint32_t sr_rd(uint32_t a, uint32_t s, void *c) {
    (void)a; (void)s; (void)c;
    step(); M.sr_armed = 1;
    return M.sr | (M.tdr_full ? 0u : SR_TXE);
}
static uint32_t dr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)c;
    if (M.sr_armed) M.sr &= ~SR_TC;
    M.sr_armed = 0;
    M.tdr = (uint8_t)v; M.tdr_full = 1;
    if (!M.tsr_busy) tsr_load();
    return old;
}
static uint32_t bsrr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)old; (void)c;
    if (v & (1u << DE_PIN)) M.de = 1;
    if (v & (1u << (16u + DE_PIN))) {
        M.de = 0; M.de_fall_tick = M.tick;
        if (M.tsr_busy) { M.de_whole_frame = 0; M.truncated++; }   /* 프레임 도중 드라이버 꺼짐 */
    }
    return 0;
}
static void setup(void) {
    g_nregs = 0; g_unexpected = 0;
    memset(&M, 0, sizeof M);
    M.frame_ticks = 10; M.sr = SR_TC;
    mock_add_reg(USART_SR(USART2_BASE), 0, sr_rd, NULL);
    mock_add_reg(USART_DR(USART2_BASE), 0, NULL, dr_wr);
    mock_add_reg(USART_BRR(USART2_BASE), 0x16Du, NULL, NULL);
    mock_add_reg(USART_CR1(USART2_BASE), CR1_UE | CR1_TE, NULL, NULL);
    mock_add_reg(GPIOA_BSRR, 0, NULL, bsrr_wr);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1: 4개 fck × 모든 정수 baud 에서 한 줄 공식 == mantissa/fraction 방식 */
    {
        static const uint32_t fcks[] = { 16000000u, 42000000u, 45000000u, 84000000u };
        uint32_t bad16 = 0, bad8 = 0, n = 0;
        for (unsigned i = 0; i < 4; i++)
            for (uint32_t b = 1200; b <= fcks[i] / 16u; b += 7) {
                uint32_t d = div_round(fcks[i], b);
                if (d / 16u > 0xFFFu) continue;
                n++;
                if (brr16_oneliner(fcks[i], b) != brr_reference(fcks[i], b, 0)) bad16++;
                if (brr8_from_div(d) != brr_reference(fcks[i], b, 1) || (brr8_from_div(d) & 8u)) bad8++;
            }
        CHECK(n > 1000000u && bad16 == 0 && bad8 == 0);
        CHECK(brr16_oneliner(45000000u, 9600u) == 0x1250u && brr8_from_div(div_round(45000000u, 19200u)) == 0x1250u);
    }
    /* 연습 2: OVER8 고르기 (42 MHz) */
    CHECK(uart_pick_over8(42000000u, 115200u) == 0);
    CHECK(uart_pick_over8(42000000u, 2625000u) == 0);      /* D = 16: 16x 의 한계 */
    CHECK(uart_pick_over8(42000000u, 3000000u) == 1);      /* D = 14 */
    CHECK(uart_pick_over8(42000000u, 5250000u) == 1);      /* D = 8: 8x 의 한계 */
    CHECK(uart_pick_over8(42000000u, 6000000u) == -1);     /* D = 7 */
    CHECK(uart_pick_over8(42000000u, 600u) == -1);         /* D=70000: mantissa 4375(16x)·8750(8x) 둘 다 > 4095 */
    CHECK(uart_pick_over8(42000000u, 1200u) == 0);         /* 느린 쪽 한계는 16x 가 더 넓다 */
    /* 연습 3: uart_puts */
    setup();
    CHECK(uart_puts(USART2_BASE, "a\nb\n") == 6);
    CHECK(uart_flush(USART2_BASE) == UART_OK && M.wire_n == 6 && memcmp(M.wire, "a\r\nb\r\n", 6) == 0);
    CHECK(uart_puts(USART2_BASE, NULL) == UART_EINVAL);
    /* 연습 4: RS-485 */
    setup();
    CHECK(rs485_send(USART2_BASE, (const uint8_t *)"PING", 4, 0) == UART_OK);   /* TXE 만 */
    mock_read32(USART_SR(USART2_BASE)); for (int i = 0; i < 20; i++) step();
    CHECK(M.wire_n == 4 && M.bus_n == 3 && memcmp(M.bus, "PIN", 3) == 0 && M.truncated == 1);
    setup();
    CHECK(rs485_send(USART2_BASE, (const uint8_t *)"PING", 4, 1) == UART_OK);   /* TC 까지 */
    CHECK(M.wire_n == 4 && M.bus_n == 4 && memcmp(M.bus, "PING", 4) == 0 && M.truncated == 0);
    CHECK(M.de_fall_tick == M.tc_tick && M.tc_tick == 41);  /* 4 프레임 × 10 tick + 첫 폴링 1 */
    CHECK(g_unexpected == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
