/* BM-B02 추가 연습 — Mock 주변장치 프레임워크 응용 (practice.c)
 *
 * 앞부분 = solution.c 의 드라이버 + mock 코어 + 접근 타입 + USART2 모델 (자기완결을 위해 그대로 복사).
 * 뒷부분 = 연습 1~4:
 *   1. mock_hw_reset()        : 훅·모델은 그대로 두고 모든 레지스터를 리셋값으로 (NRST 흉내)
 *   2. mock_inject_stuck()    : 읽기 값에 stuck-at-0/1 고장 덧씌우기 (rd 훅 데코레이터)
 *   3. 함수 포인터 이음매      : bus_ops_t 로 같은 드라이버를 돌려 로그가 같음을 확인
 *   4. 폴링 비용과 타임아웃 경계 : tx_ticks 별 SR 읽기 횟수 = 1 + (n-1)·tx_ticks, 타임아웃 100 의 경계
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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

/* ======================= 연습 1: 하드웨어 리셋 ======================= */
void mock_hw_reset(void) {
    for (unsigned i = 0; i < M.nreg; i++) M.reg[i].val = M.reg[i].reset;   /* 훅·ctx·tickers 는 유지 */
    mock_log_clear();
}

/* ======================= 연습 2: stuck-at 고장 주입 (rd 훅 데코레이터) ======================= */
#define MOCK_MAX_FAULTS 4
typedef struct { mock_on_read orig; mock_on_write orig_wr; void *orig_ctx; uint32_t stuck0, stuck1; } mock_fault_t;
static mock_fault_t g_fault[MOCK_MAX_FAULTS];
static unsigned g_nfault;

static uint32_t fault_rd(uint32_t addr, uint32_t stored, void *ctx) {
    const mock_fault_t *f = ctx;
    uint32_t v = f->orig ? f->orig(addr, stored, f->orig_ctx) : stored;    /* 원래 동작(부작용 포함) 먼저 */
    return (v & ~f->stuck0) | f->stuck1;                                    /* CPU 가 보는 값만 고장 */
}
static uint32_t fault_wr(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    const mock_fault_t *f = ctx;                                            /* 쓰기는 원래 훅에 원래 ctx 로 */
    return f->orig_wr(addr, old, val, f->orig_ctx);
}

int mock_inject_stuck(uint32_t addr, uint32_t stuck0, uint32_t stuck1) {
    mock_reg_t *g = mock_find(addr);
    if (!g || g_nfault >= MOCK_MAX_FAULTS) return -1;
    mock_fault_t *f = &g_fault[g_nfault++];
    *f = (mock_fault_t){ g->rd, g->wr, g->ctx, stuck0, stuck1 };
    g->rd = fault_rd;
    if (g->wr) g->wr = fault_wr;    /* ctx 를 바꾸므로 wr 훅도 감싸야 원래 ctx 를 받는다 */
    g->ctx = f;
    return 0;
}

/* ======================= 연습 3: 함수 포인터 이음매 ======================= */
typedef struct { uint32_t (*rd)(uint32_t addr); void (*wr)(uint32_t addr, uint32_t val); } bus_ops_t;

static int uart_putc_ops(const bus_ops_t *b, uint32_t base, uint8_t c) {
    for (uint32_t n = 0; n < UART_TIMEOUT_POLLS; n++) {
        if (b->rd(USART_SR(base)) & USART_SR_TXE) {
            b->wr(USART_DR(base), c);
            return 0;
        }
    }
    return -1;
}
static unsigned g_spy_rd, g_spy_wr;                        /* 스파이 버스: 세고 mock 으로 넘긴다 */
static uint32_t spy_rd(uint32_t a) { g_spy_rd++; return mock_read32(a); }
static void     spy_wr(uint32_t a, uint32_t v) { g_spy_wr++; mock_write32(a, v); }

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 연습 4: n 바이트 보낼 때 SR 읽기 횟수 */
static unsigned sr_reads_for(unsigned tx_ticks, unsigned n, int *rc) {
    uart_model_t u;
    mock_reset(); uart_model_attach(&u, USART2_BASE, tx_ticks); uart_init(USART2_BASE, 0x16Du);
    mock_log_clear();
    uint8_t buf[16];
    for (unsigned i = 0; i < n; i++) buf[i] = (uint8_t)('a' + i);
    *rc = uart_write(USART2_BASE, buf, n);
    return mock_log_count('R', USART_SR(USART2_BASE));
}

int main(void) {
    const uint32_t B = USART2_BASE;
    uart_model_t u;

    /* 연습 1 — 리셋 후 재초기화 */
    mock_reset(); uart_model_attach(&u, B, 3); uart_init(B, 0x16Du);
    CHECK(uart_putc(B, 'A') == 0);
    mock_tick(3);
    mock_hw_reset();
    CHECK(mock_peek(USART_CR1(B)) == 0 && mock_peek(USART_BRR(B)) == 0 && mock_peek(USART_SR(B)) == 0xC0u);
    CHECK(uart_putc(B, 'B') == 0);                 /* 드라이버는 성공이라 믿지만 */
    mock_tick(3);
    CHECK(u.tx_n == 1 && u.tx_lost == 1);          /* CR1=0 → 선은 조용 */
    uart_init(B, 0x16Du);
    CHECK(uart_putc(B, 'C') == 0);
    mock_tick(3);
    CHECK(u.tx_n == 2 && u.tx_cap[1] == 'C' && mock_clean());

    /* 연습 2 — stuck-at 고장 */
    mock_reset(); g_nfault = 0; uart_model_attach(&u, B, 3); uart_init(B, 0x16Du);
    CHECK(mock_inject_stuck(USART_SR(B), USART_SR_TXE, 0) == 0);           /* TXE stuck-at-0 */
    mock_log_clear();
    CHECK(uart_putc(B, 'X') == -1 && M.nlog == UART_TIMEOUT_POLLS && u.tx_n == 0);
    REG_WRITE(USART_SR(B), ~USART_SR_TC);                                   /* 감싼 wr 훅: rc_w0 그대로 */
    CHECK(mock_peek(USART_SR(B)) == USART_SR_TXE && REG_READ(USART_SR(B)) == 0);
    CHECK(mock_inject_stuck(0x40000000u, 0, 1) == -1);                      /* 지도에 없는 주소 */

    mock_reset(); g_nfault = 0; uart_model_attach(&u, B, 3); uart_init(B, 0x16Du);
    mock_inject_stuck(USART_SR(B), 0, USART_SR_RXNE);                       /* RXNE stuck-at-1 */
    uart_model_inject_rx(&u, (const uint8_t *)"Q", 1);
    mock_tick(1);
    uint8_t got[3] = { 0 };
    for (int i = 0; i < 3; i++) CHECK(uart_getc(B, &got[i]) == 0);
    CHECK(memcmp(got, "QQQ", 3) == 0);                                      /* 유령 바이트 */
    CHECK((mock_peek(USART_SR(B)) & USART_SR_RXNE) == 0);                   /* 진짜 RXNE 는 0 */

    /* 연습 3 — 함수 포인터 이음매: 매크로 버전과 같은 접근 순서 */
    mock_log_t a[16], b[16];
    unsigned na, nb;
    mock_reset(); uart_model_attach(&u, B, 3); uart_init(B, 0x16Du); mock_log_clear();
    uart_putc(B, 'x'); uart_putc(B, 'y');
    na = M.nlog; memcpy(a, M.log, na * sizeof a[0]);
    static const bus_ops_t spy = { spy_rd, spy_wr };
    mock_reset(); uart_model_attach(&u, B, 3); uart_init(B, 0x16Du); mock_log_clear();
    g_spy_rd = g_spy_wr = 0;
    uart_putc_ops(&spy, B, 'x'); uart_putc_ops(&spy, B, 'y');
    nb = M.nlog; memcpy(b, M.log, nb * sizeof b[0]);
    CHECK(na == 6 && nb == na && memcmp(a, b, na * sizeof a[0]) == 0);
    CHECK(g_spy_rd == 4 && g_spy_wr == 2);

    /* 연습 4 — 폴링 비용 = 1 + (n-1)·tx_ticks, 타임아웃 경계 */
    int rc, bad = 0;
    for (unsigned t = 1; t <= 8; t++)
        for (unsigned n = 1; n <= 10; n++)
            bad += sr_reads_for(t, n, &rc) != 1 + (n - 1) * t || rc != 0;
    CHECK(bad == 0);
    CHECK(sr_reads_for(100, 2, &rc) == 101 && rc == 0);    /* busy 99번 + ready 1번 = 100 폴링 안 */
    CHECK(sr_reads_for(101, 2, &rc) == 101 && rc == -1);   /* busy 100번 → 타임아웃 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* UNIT_TEST */
