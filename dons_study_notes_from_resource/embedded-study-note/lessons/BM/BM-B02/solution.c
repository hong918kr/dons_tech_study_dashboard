/* BM-B02 Mock 주변장치 프레임워크: read/write 훅
 *
 * Part A — mock 코어: 희소(sparse) 레지스터 표 + 주소별 read/write 훅 + 접근 로그(access log)
 *          + "지도에 없는 주소" 접근 탐지 + 백도어(peek/poke) + 시간(tick).
 * Part B — 접근 타입(access type) 훅: r / w / rc_w1(=W1C) / rc_w0 / rc_r 를 비트마스크로 기술,
 *          표 등록 오류(중복·비정렬·가득 참)와 로그 넘침 처리.
 * Part C — 주변장치 모델: USART2 (TXE 는 DR 쓰기 후 N tick 동안 0, TX 캡처 버퍼, RX 주입 큐 →
 *          RXNE, DR 읽기로 RXNE 클리어). 같은 소스의 uart_putc/getc 드라이버를 mock 위에서 검증.
 *
 * 레지스터 주소·비트는 STM32F4 (RM0090) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 *
 * 호스트(맥) 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (-v: 로그 출력)
 * 타깃 빌드     : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding \
 *                       -DTARGET_BUILD -c solution.c        (드라이버만 남고 mock·테스트는 사라진다)
 */
#ifndef TARGET_BUILD
#define UNIT_TEST 1                       /* 호스트 테스트 빌드 */
#endif

#include <stdbool.h>
#include <stdint.h>

/* ======================= 이음매(seam): 드라이버는 이 두 매크로로만 레지스터를 만진다 ======================= */
#ifdef UNIT_TEST                          /* host build: hooks */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else                                     /* target build: real MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* ======================= 드라이버 (호스트·타깃 공용 소스) ======================= */
#define USART2_BASE      0x40004400u      /* RM0090 기준 대표값 */
#define USART_SR(b)      ((b) + 0x00u)
#define USART_DR(b)      ((b) + 0x04u)
#define USART_BRR(b)     ((b) + 0x08u)
#define USART_CR1(b)     ((b) + 0x0Cu)
#define USART_SR_RXNE    (1u << 5)
#define USART_SR_TC      (1u << 6)
#define USART_SR_TXE     (1u << 7)
#define USART_CR1_RE     (1u << 2)
#define USART_CR1_TE     (1u << 3)
#define USART_CR1_UE     (1u << 13)
#define UART_TIMEOUT_POLLS 100u           /* 상태 비트 폴링 상한 (진짜 시간 기반 타임아웃은 BM-B13) */

void uart_init(uint32_t base, uint32_t brr) {
    REG_WRITE(USART_BRR(base), brr);
    REG_WRITE(USART_CR1(base), USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
}

/* TXE=1 (TDR 비었음) 을 기다렸다가 DR 에 쓴다. 0 = OK, -1 = 타임아웃 */
int uart_putc(uint32_t base, uint8_t c) {
    for (uint32_t n = 0; n < UART_TIMEOUT_POLLS; n++) {
        if (REG_READ(USART_SR(base)) & USART_SR_TXE) {
            REG_WRITE(USART_DR(base), c);
            return 0;
        }
    }
    return -1;
}

/* RXNE=1 (받은 바이트 있음) 을 기다렸다가 DR 을 읽는다 (읽기가 RXNE 를 지운다). */
int uart_getc(uint32_t base, uint8_t *out) {
    for (uint32_t n = 0; n < UART_TIMEOUT_POLLS; n++) {
        if (REG_READ(USART_SR(base)) & USART_SR_RXNE) {
            *out = (uint8_t)REG_READ(USART_DR(base));
            return 0;
        }
    }
    return -1;
}

int uart_write(uint32_t base, const uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (uart_putc(base, p[i]) != 0) return -1;
    return 0;
}

#ifdef UNIT_TEST
#include <stdio.h>
#include <string.h>

/* ======================= Part A: mock 코어 ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* CPU 가 볼 값 */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* 새 저장값 */
typedef void     (*mock_on_tick)(void *ctx);

#define MOCK_MAX_REGS    16
#define MOCK_LOG_CAP     256
#define MOCK_MAX_TICKERS 4

typedef struct { uint32_t addr, val, reset; mock_on_read rd; mock_on_write wr; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;  /* op: 'R' 'W' / 지도 밖: 'r' 'w' */

static struct {
    mock_reg_t   reg[MOCK_MAX_REGS];
    unsigned     nreg;
    mock_log_t   log[MOCK_LOG_CAP];
    unsigned     nlog, log_dropped;
    mock_on_tick tick_fn[MOCK_MAX_TICKERS];
    void        *tick_ctx[MOCK_MAX_TICKERS];
    unsigned     ntick;
    uint64_t     now;                    /* 지난 tick 수. 버스 접근 1회 = 1 tick */
    unsigned     unexpected;             /* 지도에 없는 주소 접근 횟수 */
    uint32_t     first_bad;              /* 첫 번째 나쁜 주소 */
} M;

void mock_reset(void) { memset(&M, 0, sizeof M); }

static mock_reg_t *mock_find(uint32_t addr) {
    for (unsigned i = 0; i < M.nreg; i++)
        if (M.reg[i].addr == addr) return &M.reg[i];
    return NULL;
}

static void mock_log_push(char op, uint32_t addr, uint32_t val) {
    if (M.nlog < MOCK_LOG_CAP) M.log[M.nlog++] = (mock_log_t){ op, addr, val };
    else M.log_dropped++;
}

static void mock_bad(uint32_t addr) {
    if (M.unexpected++ == 0) M.first_bad = addr;
}

void mock_tick(unsigned n) {
    while (n--) {
        M.now++;
        for (unsigned i = 0; i < M.ntick; i++) M.tick_fn[i](M.tick_ctx[i]);
    }
}

int mock_add_ticker(mock_on_tick fn, void *ctx) {
    if (M.ntick >= MOCK_MAX_TICKERS) return -1;
    M.tick_fn[M.ntick] = fn;
    M.tick_ctx[M.ntick] = ctx;
    return (int)M.ntick++;
}

/* 레지스터 하나를 표에 올린다. 반환: 인덱스, 실패 -1 */
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (addr & 3u) return -1;                       /* 32비트 레지스터는 4바이트 정렬 */
    if (mock_find(addr)) return -1;                 /* 같은 주소 두 번 = 테스트 설정 버그 */
    if (M.nreg >= MOCK_MAX_REGS) return -1;         /* 정적 표가 가득 참 */
    M.reg[M.nreg] = (mock_reg_t){ addr, reset_val, reset_val, r, w, ctx };
    return (int)M.nreg++;
}

/* 백도어(backdoor): 하드웨어 쪽(모델·테스트)이 쓰는 접근. 훅·로그·tick 없음 */
uint32_t mock_peek(uint32_t addr) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { mock_bad(addr); return 0; }
    return g->val;
}
void mock_poke(uint32_t addr, uint32_t val) {
    mock_reg_t *g = mock_find(addr);
    if (!g) { mock_bad(addr); return; }
    g->val = val;
}

/* 프론트도어(frontdoor): CPU(드라이버)의 접근. 표 → 훅 → 로그 → tick */
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *g = mock_find(addr);
    uint32_t v = 0;
    if (!g) {
        mock_bad(addr);
        mock_log_push('r', addr, 0);
    } else {
        v = g->rd ? g->rd(addr, g->val, g->ctx) : g->val;   /* 훅이 poke 로 저장값을 바꿀 수 있다 */
        mock_log_push('R', addr, v);
    }
    mock_tick(1);
    return v;
}

void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *g = mock_find(addr);
    if (!g) {
        mock_bad(addr);
        mock_log_push('w', addr, val);
    } else {
        g->val = g->wr ? g->wr(addr, g->val, val, g->ctx) : val;
        mock_log_push('W', addr, val);
    }
    mock_tick(1);
}

/* 로그 비교: 같으면 -1, 다르면 첫 불일치 인덱스 (길이가 다르면 짧은 쪽 길이) */
int mock_log_expect(const mock_log_t *e, unsigned n) {
    unsigned k = M.nlog < n ? M.nlog : n;
    for (unsigned i = 0; i < k; i++)
        if (M.log[i].op != e[i].op || M.log[i].addr != e[i].addr || M.log[i].val != e[i].val)
            return (int)i;
    return (M.nlog == n) ? -1 : (int)k;
}
void mock_log_clear(void) { M.nlog = 0; M.log_dropped = 0; }
unsigned mock_log_count(char op, uint32_t addr) {
    unsigned c = 0;
    for (unsigned i = 0; i < M.nlog; i++) c += (M.log[i].op == op && M.log[i].addr == addr);
    return c;
}
void mock_log_dump(const char *title) {
    printf("-- %s (%u entries, t=%llu)\n", title, M.nlog, (unsigned long long)M.now);
    for (unsigned i = 0; i < M.nlog; i++)
        printf("  %2u  %c 0x%08X = 0x%08X\n", i, M.log[i].op, M.log[i].addr, M.log[i].val);
}
bool mock_clean(void) { return M.unexpected == 0 && M.log_dropped == 0; }

/* ======================= Part B: 접근 타입 훅 ======================= */
/* 비트마다 타입 하나. 어느 마스크에도 없는 비트 = rw. 마스크끼리는 겹치지 않는다. */
typedef struct {
    uint32_t ro;    /* r     : 읽기 전용 — CPU 쓰기 무시                      */
    uint32_t wo;    /* w     : 쓰기 전용 — 읽으면 0 (예: GPIO BSRR)           */
    uint32_t w1c;   /* rc_w1 : 1 을 쓰면 클리어, 0 은 무영향 (W1C)             */
    uint32_t w0c;   /* rc_w0 : 0 을 쓰면 클리어, 1 은 무영향 (F4 TIM SR, USART SR TC) */
    uint32_t rc;    /* rc_r  : 읽으면 클리어 (쓰기는 무시)                      */
} mock_access_t;

uint32_t mock_access_wr(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    const mock_access_t *a = ctx;
    (void)addr;
    uint32_t rw = ~(a->ro | a->rc | a->w1c | a->w0c);       /* rw + wo: 쓴 값 그대로 */
    return (old & (a->ro | a->rc))                           /* 하드웨어 소유 비트 유지 */
         | (val & rw)
         | (old & a->w1c & ~val)                             /* 1 쓴 자리만 지워짐 */
         | (old & a->w0c & val);                             /* 0 쓴 자리만 지워짐 */
}

uint32_t mock_access_rd(uint32_t addr, uint32_t stored, void *ctx) {
    const mock_access_t *a = ctx;
    if (a->rc & stored) mock_poke(addr, stored & ~a->rc);    /* 읽은 "뒤" 지운다 */
    return stored & ~a->wo;
}

/* ======================= Part C: USART2 모델 ======================= */
#define UART_CAP 64
#define UART_RXQ 32
typedef struct {
    uint32_t base;
    unsigned tx_ticks;                 /* DR 쓰기 후 TXE 가 1 로 돌아오기까지 tick 수 */
    unsigned tx_left;                  /* 0 = 송신 중 아님 (TXE=1) */
    uint8_t  tx_byte;
    uint8_t  tx_cap[UART_CAP];         /* "선 위로 나간" 바이트 */
    unsigned tx_n, tx_overwrite, tx_lost;
    uint8_t  rx_q[UART_RXQ];           /* 테스트가 주입한, 곧 도착할 바이트 */
    unsigned rx_head, rx_tail, rx_lost;
    uint8_t  rdr;                      /* 수신 데이터 레지스터 (DR 읽기로 보이는 값) */
} uart_model_t;

static const mock_access_t UART_SR_ACCESS = {   /* RXNE·TC = rc_w0, 나머지 = r */
    .ro = ~(USART_SR_RXNE | USART_SR_TC), .w0c = USART_SR_RXNE | USART_SR_TC,
};

static bool uart_on(const uart_model_t *u, uint32_t dir) {
    uint32_t want = USART_CR1_UE | dir;
    return (mock_peek(USART_CR1(u->base)) & want) == want;
}

/* DR 쓰기 = TDR 적재. TXE·TC 내리고 tx_ticks 뒤 "전송 완료" */
static uint32_t uart_dr_wr(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    uart_model_t *u = ctx;
    (void)addr;
    if (!uart_on(u, USART_CR1_TE)) { u->tx_lost++; return old; }   /* 송신기 꺼짐 → 안 나감 */
    if (u->tx_left) u->tx_overwrite++;                              /* TXE=0 인데 씀 → 앞 바이트 소실 */
    u->tx_byte = (uint8_t)val;
    u->tx_left = u->tx_ticks;
    mock_poke(USART_SR(u->base), mock_peek(USART_SR(u->base)) & ~(USART_SR_TXE | USART_SR_TC));
    return old;                                                     /* 읽기 쪽 값은 uart_dr_rd 가 준다 */
}

/* DR 읽기 = RDR. 부작용: SR.RXNE 클리어 (다른 레지스터를 바꾸는 읽기) */
static uint32_t uart_dr_rd(uint32_t addr, uint32_t stored, void *ctx) {
    uart_model_t *u = ctx;
    (void)addr; (void)stored;
    mock_poke(USART_SR(u->base), mock_peek(USART_SR(u->base)) & ~USART_SR_RXNE);
    return u->rdr;
}

static void uart_tick(void *ctx) {
    uart_model_t *u = ctx;
    uint32_t sr = mock_peek(USART_SR(u->base));
    if (u->tx_left && --u->tx_left == 0) {                          /* 송신 끝 */
        if (u->tx_n < UART_CAP) u->tx_cap[u->tx_n++] = u->tx_byte;
        sr |= USART_SR_TXE | USART_SR_TC;
    }
    if (!(sr & USART_SR_RXNE) && u->rx_head != u->rx_tail) {       /* RDR 비었으면 다음 바이트 도착 */
        uint8_t b = u->rx_q[u->rx_head++ % UART_RXQ];
        if (uart_on(u, USART_CR1_RE)) { u->rdr = b; sr |= USART_SR_RXNE; }
        else u->rx_lost++;                                          /* 수신기 꺼짐 → 버려짐 */
    }
    mock_poke(USART_SR(u->base), sr);
}

int uart_model_attach(uart_model_t *u, uint32_t base, unsigned tx_ticks) {
    memset(u, 0, sizeof *u);
    u->base = base;
    u->tx_ticks = tx_ticks ? tx_ticks : 1;
    int ok = 0;
    ok |= mock_add_reg(USART_SR(base), USART_SR_TXE | USART_SR_TC,   /* 리셋값 0x00C0 */
                       mock_access_rd, mock_access_wr, (void *)&UART_SR_ACCESS);
    ok |= mock_add_reg(USART_DR(base), 0, uart_dr_rd, uart_dr_wr, u);
    ok |= mock_add_reg(USART_BRR(base), 0, NULL, NULL, NULL);
    ok |= mock_add_reg(USART_CR1(base), 0, NULL, NULL, NULL);
    ok |= mock_add_ticker(uart_tick, u);
    return ok < 0 ? -1 : 0;
}

int uart_model_inject_rx(uart_model_t *u, const uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        if (u->rx_tail - u->rx_head >= UART_RXQ) return -1;
        u->rx_q[u->rx_tail++ % UART_RXQ] = p[i];
    }
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs = 0x2026B02u;                         /* xorshift32, 고정 시드 */
static uint32_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

/* 비트 하나씩 규칙을 적용하는 느린 기준 구현 (property test 의 정답) */
static uint32_t ref_wr(const mock_access_t *a, uint32_t old, uint32_t val) {
    uint32_t r = 0;
    for (unsigned b = 0; b < 32; b++) {
        uint32_t m = 1u << b, o = old & m, v = val & m, nb;
        if ((a->ro | a->rc) & m) nb = o;
        else if (a->w1c & m)     nb = v ? 0 : o;
        else if (a->w0c & m)     nb = v ? o : 0;
        else                     nb = v;
        r |= nb;
    }
    return r;
}

/* 예제 훅: 읽을 때마다 1 씩 느는 카운터, 하위 16비트만 구현된 레지스터 */
static uint32_t cnt_rd(uint32_t addr, uint32_t stored, void *ctx) {
    (void)ctx; mock_poke(addr, stored + 1u); return stored;
}
static uint32_t impl_mask_wr(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; return val & *(const uint32_t *)ctx;
}

/* 버그 드라이버: TXE 를 안 기다린다 */
static void bad_putc_nowait(uint32_t base, uint8_t c) { REG_WRITE(USART_DR(base), c); }

#define R_(a, v) { 'R', (a), (v) }
#define W_(a, v) { 'W', (a), (v) }

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    const uint32_t TIM3_CNT = 0x40000424u, TIM3_ARR = 0x4000042Cu, B = USART2_BASE;
    static const uint32_t MASK16 = 0xFFFFu;

    /* ---------- Part A: 코어 ---------- */
    mock_reset();
    CHECK(mock_add_reg(TIM3_ARR, 0xFFFFu, NULL, impl_mask_wr, (void *)&MASK16) == 0);
    CHECK(mock_add_reg(TIM3_CNT, 0, cnt_rd, NULL, NULL) == 1);
    CHECK(REG_READ(TIM3_ARR) == 0xFFFFu);                    /* 리셋값 */
    REG_WRITE(TIM3_ARR, 0x12345678u);
    CHECK(REG_READ(TIM3_ARR) == 0x5678u);                    /* write 훅: 없는 비트는 0 */
    CHECK(REG_READ(TIM3_CNT) == 0 && REG_READ(TIM3_CNT) == 1 && REG_READ(TIM3_CNT) == 2);
    CHECK(mock_peek(TIM3_CNT) == 3);                         /* 백도어: 훅·로그 없음 */
    {
        const mock_log_t e[] = { R_(TIM3_ARR, 0xFFFFu), W_(TIM3_ARR, 0x12345678u), R_(TIM3_ARR, 0x5678u),
                                 R_(TIM3_CNT, 0), R_(TIM3_CNT, 1), R_(TIM3_CNT, 2) };
        CHECK(mock_log_expect(e, 6) == -1);                  /* 로그 = CPU 가 쓴 값 / 본 값, 순서대로 */
        CHECK(M.now == 6);                                   /* 버스 접근 6번 = 6 tick */
    }
    CHECK(mock_clean());
    CHECK(REG_READ(0x40000430u) == 0);                       /* 지도 밖: 0 을 주고 기록 */
    REG_WRITE(0x50000000u, 1);
    CHECK(M.unexpected == 2 && M.first_bad == 0x40000430u);
    CHECK(M.nlog == 8 && M.log[6].op == 'r' && M.log[7].op == 'w');
    CHECK(!mock_clean());
    mock_poke(TIM3_CNT, 100);
    CHECK(M.nlog == 8 && REG_READ(TIM3_CNT) == 100);         /* poke 는 로그에 안 남는다 */
    mock_reset();
    CHECK(M.nreg == 0 && M.nlog == 0 && M.unexpected == 0 && M.now == 0);

    /* ---------- Part B: 등록 오류 · 로그 넘침 · 접근 타입 ---------- */
    mock_reset();
    CHECK(mock_add_reg(0x40020000u, 0, NULL, NULL, NULL) == 0);
    CHECK(mock_add_reg(0x40020000u, 0, NULL, NULL, NULL) == -1);   /* 중복 */
    CHECK(mock_add_reg(0x40020002u, 0, NULL, NULL, NULL) == -1);   /* 비정렬 */
    for (unsigned i = 1; i < MOCK_MAX_REGS; i++) mock_add_reg(0x40020000u + 4u * i, 0, NULL, NULL, NULL);
    CHECK(M.nreg == MOCK_MAX_REGS);
    CHECK(mock_add_reg(0x40021000u, 0, NULL, NULL, NULL) == -1);   /* 가득 참 */
    for (unsigned i = 0; i < 300; i++) (void)REG_READ(0x40020000u);
    CHECK(M.nlog == MOCK_LOG_CAP && M.log_dropped == 300 - MOCK_LOG_CAP && !mock_clean());

    {
        const uint32_t SR = 0x40000010u;
        mock_access_t a = { .ro = 0xFFFFFE80u, .wo = 0x100u, .w1c = 0x0Fu, .w0c = 0x30u, .rc = 0x40u };
        mock_reset();
        mock_add_reg(SR, 0, mock_access_rd, mock_access_wr, &a);
        mock_poke(SR, 0xFFu);
        REG_WRITE(SR, 0x05u);                       /* W1C 0,2 지움 · w0c 4,5 는 0 을 써서 지움 */
        CHECK(mock_peek(SR) == 0xCAu);
        mock_poke(SR, 0xFFu);
        REG_WRITE(SR, ~0x10u);                      /* rc_w0 정석: 지울 비트만 0, W1C 쪽은 1 → 다 지워짐 */
        CHECK(mock_peek(SR) == 0x1E0u);
        mock_poke(SR, 0x1FFu);
        CHECK(REG_READ(SR) == 0xFFu);               /* wo 비트(8) 는 0 으로 읽힘 */
        CHECK(mock_peek(SR) == 0x1BFu);             /* rc_r 비트(6) 는 읽은 뒤 클리어 */
        CHECK(REG_READ(SR) == 0xBFu);

        unsigned bad_w = 0, bad_r = 0;
        for (int t = 0; t < 2000; t++) {            /* 무작위 비트 타입 분할 × 무작위 값 */
            mock_access_t r = { 0 };
            for (unsigned b = 0; b < 32; b++) {
                uint32_t m = 1u << b;
                switch (rnd() % 6) {
                case 0: r.ro |= m; break;  case 1: r.wo |= m; break;  case 2: r.w1c |= m; break;
                case 3: r.w0c |= m; break; case 4: r.rc |= m; break;  default: break;   /* rw */
                }
            }
            uint32_t old = rnd(), val = rnd();
            a = r;
            mock_log_clear();                       /* 2000×2 접근: 로그는 매번 비운다 */
            mock_poke(SR, old);
            REG_WRITE(SR, val);
            bad_w += mock_peek(SR) != ref_wr(&r, old, val);
            mock_poke(SR, old);
            bad_r += (REG_READ(SR) != (old & ~r.wo)) || (mock_peek(SR) != (old & ~r.rc));
        }
        CHECK(bad_w == 0 && bad_r == 0);
        CHECK(mock_clean());
    }

    /* ---------- Part C: USART2 모델 + 드라이버 ---------- */
    uart_model_t u;
    mock_reset();
    CHECK(uart_model_attach(&u, B, 3) == 0);
    CHECK(REG_READ(USART_SR(B)) == 0xC0u && REG_READ(USART_CR1(B)) == 0);   /* 리셋값 */
    mock_log_clear();

    uart_init(B, 0x16Du);                          /* 42 MHz, 115200 (BRR 계산은 BM-B12) */
    CHECK(uart_putc(B, 'H') == 0 && uart_putc(B, 'i') == 0);
    {
        const mock_log_t e[] = {
            W_(USART_BRR(B), 0x16Du), W_(USART_CR1(B), 0x200Cu),
            R_(USART_SR(B), 0xC0u), W_(USART_DR(B), 'H'),
            R_(USART_SR(B), 0x00u), R_(USART_SR(B), 0x00u), R_(USART_SR(B), 0xC0u), W_(USART_DR(B), 'i'),
        };
        if (g_verbose) mock_log_dump("init + putc('H') + putc('i')");
        CHECK(mock_log_expect(e, 8) == -1);        /* 정확한 접근 순서 */
    }
    CHECK(u.tx_n == 1 && u.tx_cap[0] == 'H');      /* 'i' 는 아직 선 위 */
    mock_tick(2);
    CHECK(u.tx_n == 2 && memcmp(u.tx_cap, "Hi", 2) == 0);
    CHECK((mock_peek(USART_SR(B)) & USART_SR_TXE) != 0);

    mock_log_clear();                              /* RX: 주입 → RXNE → DR 읽기로 클리어 */
    uint8_t c1 = 0, c2 = 0;
    CHECK(uart_model_inject_rx(&u, (const uint8_t *)"OK", 2) == 0);
    CHECK(uart_getc(B, &c1) == 0 && c1 == 'O');
    CHECK(uart_getc(B, &c2) == 0 && c2 == 'K');
    {
        const mock_log_t e[] = { R_(USART_SR(B), 0xC0u), R_(USART_SR(B), 0xE0u), R_(USART_DR(B), 'O'),
                                 R_(USART_SR(B), 0xE0u), R_(USART_DR(B), 'K') };
        if (g_verbose) mock_log_dump("inject \"OK\" + getc x2");
        CHECK(mock_log_expect(e, 5) == -1);
    }
    CHECK((mock_peek(USART_SR(B)) & USART_SR_RXNE) == 0);

    mock_log_clear();                              /* 받을 것 없음 → 정확히 100번 폴링 후 -1 */
    CHECK(uart_getc(B, &c1) == -1 && M.nlog == UART_TIMEOUT_POLLS);

    mock_log_clear();                              /* uart_write: 첫 바이트 1회, 이후 3회씩 폴링 */
    CHECK(uart_write(B, (const uint8_t *)"Hello\n", 6) == 0);
    CHECK(mock_log_count('R', USART_SR(B)) == 1 + 5 * 3 && mock_log_count('W', USART_DR(B)) == 6);
    mock_tick(3);
    CHECK(u.tx_n == 8 && memcmp(u.tx_cap + 2, "Hello\n", 6) == 0 && u.tx_overwrite == 0);
    CHECK(mock_clean());

    /* 결함 1: TXE 안 기다리는 드라이버 → 모델이 덮어쓰기를 잡는다 */
    mock_reset(); uart_model_attach(&u, B, 3); uart_init(B, 0x16Du);
    for (const char *s = "Hello"; *s; s++) bad_putc_nowait(B, (uint8_t)*s);
    mock_tick(3);
    CHECK(u.tx_n == 1 && u.tx_cap[0] == 'o' && u.tx_overwrite == 4);

    /* 결함 2: init 전에 송신 → 드라이버는 성공(0)이라 믿지만 선은 조용하다 */
    mock_reset(); uart_model_attach(&u, B, 3);
    CHECK(uart_putc(B, 'X') == 0);
    mock_tick(5);
    CHECK(u.tx_n == 0 && u.tx_lost == 1);
    CHECK(uart_model_inject_rx(&u, (const uint8_t *)"Z", 1) == 0);
    CHECK(uart_getc(B, &c1) == -1 && u.rx_lost == 1);         /* 수신기 꺼짐 → 바이트 버려짐 */

    /* 결함 3: 베이스 주소 오타 0x4000_4400 → 0x4004_4400. 크래시 없이 탐지 */
    mock_reset(); uart_model_attach(&u, B, 3); uart_init(B, 0x16Du);
    CHECK(uart_putc(0x40044400u, 'x') == -1);
    CHECK(M.unexpected == UART_TIMEOUT_POLLS && M.first_bad == 0x40044400u && M.log[2].op == 'r');
    CHECK(u.tx_n == 0 && !mock_clean());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
