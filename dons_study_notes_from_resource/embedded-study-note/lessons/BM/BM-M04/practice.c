/* BM-M04 추가 연습 — 한 번에 검증
 * 같은 레슨의 solution.c (드라이버 + USART 모델 + NVIC-lite) 를 그대로 포함하고 그 위에 연습 답을 얹는다.
 *   cd lessons/BM/BM-M04
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M04_pr && /tmp/BM-M04_pr
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: 막히는(blocking) write — 상한 있음, ISR 에서는 거부 ---------- */
static int in_isr_context(void) { return nv_in_isr; }   /* 타깃: (SCB->ICSR & 0x1FF) != 0 (VECTACTIVE) */

/* n 바이트를 다 넣을 때까지 빈 칸을 기다린다. 반환: 넣은 바이트 수 (타임아웃이면 n 보다 작다), ISR 에서면 EINVAL */
long uart_write_all(const uint8_t *buf, size_t n, uint32_t max_spins) {
    if (in_isr_context()) return UART_EINVAL;           /* ISR 안에서 기다리면 TX ISR 가 못 돌아 영원히 못 빈다 */
    size_t done = 0;
    for (uint32_t spin = 0; done < n && spin <= max_spins; spin++) {
        done += uart_write(buf + done, n - done);
        if (done < n) CPU_IDLE();
    }
    return (long)done;
}

/* ---------- 연습 2: RS-485 DE 핀을 TC 인터럽트로 내리기 ---------- */
#define GPIOA_BSRR 0x40020018u
#define DE_PIN     8u                                    /* PA8 = 트랜시버 DE (보드 가정) */
static int      de_level;
static uint32_t de_fall_tick, de_rise_tick;
static uint32_t gpio_bsrr_wr(uint32_t a, uint32_t old, uint32_t v, void *c) {
    (void)a; (void)old; (void)c;
    if (v & (1u << DE_PIN)) { if (!de_level) de_rise_tick = U.tick; de_level = 1; }
    else if (v & (1u << (DE_PIN + 16u))) { if (de_level) de_fall_tick = U.tick; de_level = 0; }
    return 0;                                            /* BSRR 은 읽으면 0 */
}
static void de_set(int on) { REG_WRITE(GPIOA_BSRR, on ? (1u << DE_PIN) : (1u << (DE_PIN + 16u))); }

size_t uart_write_485(const uint8_t *buf, size_t n) {
    if (n) de_set(1);                                    /* 데이터보다 DE 가 먼저 */
    return uart_write(buf, n);
}
void isr_485(void) {
    uint32_t before = REG_READ(USART_CR1);
    USART2_IRQHandler();                                 /* RX + TX 링 처리는 본문 ISR 그대로 */
    uint32_t cr1 = REG_READ(USART_CR1);
    if ((before & CR1_TXEIE) && !(cr1 & CR1_TXEIE)) {    /* 링이 막 비었다: 마지막 바이트는 시프트 레지스터에 */
        REG_WRITE(USART_CR1, cr1 | CR1_TCIE);            /* 이제부터 TC 를 기다린다 (지금 TC=0) */
        return;
    }
    if ((cr1 & CR1_TCIE) && (REG_READ(USART_SR) & SR_TC)) {
        de_set(0);                                       /* stop 비트까지 나갔다 → 선 놓기 */
        REG_WRITE(USART_CR1, cr1 & ~CR1_TCIE);
    }
}
/* 흔한 버그: 링이 비는 순간(= TXEIE 를 끄는 순간) DE 를 내린다 — 마지막 바이트는 아직 시프트 레지스터에 있다 */
void isr_485_buggy(void) {
    uint32_t before = REG_READ(USART_CR1);
    USART2_IRQHandler();
    if ((before & CR1_TXEIE) && !(REG_READ(USART_CR1) & CR1_TXEIE)) de_set(0);
}

/* ---------- 연습 3: 줄 조립기 (CR, LF, CRLF 모두 한 줄 끝) ---------- */
#define LINE_MAX 32u
typedef struct { char buf[LINE_MAX]; uint32_t len; int truncated, last_cr; } line_asm_t;
/* 완성된 줄이 있으면 out 에 NUL 포함 복사하고 1, 아직이면 0. *trunc = 잘렸는지 (LINE_MAX−1 자 넘음) */
int uart_readline(line_asm_t *la, char *out, size_t cap, int *trunc) {
    uint8_t c;
    while (uart_read(&c, 1) == 1) {
        if (c == '\n' && la->last_cr) { la->last_cr = 0; continue; }   /* CRLF 의 LF */
        la->last_cr = (c == '\r');
        if (c == '\r' || c == '\n') {
            size_t n = la->len < cap - 1 ? la->len : cap - 1;
            memcpy(out, la->buf, n); out[n] = '\0';
            *trunc = la->truncated;
            la->len = 0; la->truncated = 0;
            return 1;
        }
        if (la->len < LINE_MAX - 1u) la->buf[la->len++] = (char)c;
        else la->truncated = 1;                          /* 줄 끝까지 버리고 표시만 */
    }
    return 0;
}

/* ---------- 연습 4: RX 링 크기 정하기 ---------- */
static uint32_t next_pow2(uint32_t x) { uint32_t p = 1; while (p < x) p <<= 1; return p; }
/* main 이 최대 latency_us 동안 RX 를 못 읽어도 안 넘치는 2^n 크기 (8N1 = 10 비트/바이트) */
uint32_t rx_cap_needed(uint32_t baud, uint32_t latency_us) {
    uint64_t bytes = ((uint64_t)latency_us * baud + 10ull * 1000000ull - 1ull) / (10ull * 1000000ull);   /* ceil */
    return next_pow2((uint32_t)bytes);
}
/* 모델 단위 (tick) 판: P tick 마다 읽으면 ceil(P/frame) 바이트가 쌓인다 */
uint32_t rx_cap_needed_ticks(uint32_t P, uint32_t frame) { return next_pow2((P + frame - 1u) / frame); }

static uint8_t big_rx[1024];
static uint32_t rx_drops_with(uint32_t cap, uint32_t P, uint32_t nbytes) {
    setup();
    g_rx.buf = big_rx; g_rx.mask = cap - 1u;
    for (uint32_t i = 0; i < nbytes; i++) s_src[i] = (uint8_t)i;
    rx_feed(nbytes, NULL);
    for (uint32_t t = 1; t <= nbytes * 10u + 4000u; t++) {
        sim_step();
        if (t % P == 0) { uint8_t rb[256]; while (uart_read(rb, sizeof rb) > 0) {} }
    }
    return g_st.rx_dropped;
}

static void setup_485(void) {
    setup();
    mock_add_reg(GPIOA_BSRR, 0, NULL, gpio_bsrr_wr);
    de_level = 0; de_fall_tick = de_rise_tick = 0;
    nv_vec[USART2_IRQn] = isr_485;
}
static long g_isr_write_rc;
static void isr_that_writes_all(void) { g_isr_write_rc = uart_write_all((const uint8_t *)"x", 1, 10); USART2_IRQHandler(); }

int main(void) {
    /* 연습 1 */
    setup();
    {
        uint8_t msg[300];
        for (int i = 0; i < 300; i++) msg[i] = (uint8_t)(i * 7);
        CHECK(uart_write_all(msg, 300, 100000) == 300);  /* 128 칸 링으로 300 바이트: 기다리며 채운다 */
        CHECK(uart_flush(100000) == UART_OK && U.cap_n == 300 && memcmp(U.cap, msg, 300) == 0);
        setup();
        nv_en[USART2_IRQn] = 0;                          /* ISR 가 안 돌면 링이 안 빈다 → 상한에서 멈춤 */
        uint32_t t0 = U.tick;
        CHECK(uart_write_all(msg, 300, 50) == TX_CAP && U.tick - t0 == 51);
        setup();
        nv_vec[USART2_IRQn] = isr_that_writes_all;
        uart_write((const uint8_t *)"a", 1);
        sim_run(5);
        CHECK(g_isr_write_rc == UART_EINVAL);
    }

    /* 연습 2 */
    setup_485();
    CHECK(uart_write_485((const uint8_t *)"RS485", 5) == 5 && de_level == 1);
    sim_run(100);
    CHECK(U.cap_n == 5 && memcmp(U.cap, "RS485", 5) == 0);
    CHECK(de_level == 0 && de_fall_tick == U.cap_last_tick);   /* 마지막 stop 비트와 같은 tick 에 DE ↓ */
    CHECK(!(u_cr1() & (CR1_TCIE | CR1_TXEIE)));
    uint32_t calls_ok = g_st.isr_calls;
    CHECK(uart_write_485((const uint8_t *)"2", 1) == 1);       /* 두 번째 메시지도 */
    sim_run(50);
    CHECK(U.cap_n == 6 && de_level == 0 && de_fall_tick == U.cap_last_tick && de_rise_tick < de_fall_tick);
    setup_485();
    nv_vec[USART2_IRQn] = isr_485_buggy;
    CHECK(uart_write_485((const uint8_t *)"RS485", 5) == 5);
    sim_run(100);
    CHECK(U.cap_n == 5 && U.cap_last_tick - de_fall_tick == U.frame);   /* 버그: 마지막 바이트 한 프레임이 DE ↓ 뒤에 나감 */
    CHECK(calls_ok == 7);                                      /* 5 바이트 + TXEIE→TCIE 1 + TC 1 */

    /* 연습 3 */
    setup();
    {
        const char *in = "ping\r\nstatus\nAT+THIS_COMMAND_IS_WAY_TOO_LONG_FOR_32\r\n\r\nx";
        uint32_t n = (uint32_t)strlen(in);
        static uint32_t gap[SRC_CAP];
        for (uint32_t i = 0; i < n; i++) { s_src[i] = (uint8_t)in[i]; gap[i] = xorshift32() % 7u; }
        rx_feed(n, gap);
        line_asm_t la; memset(&la, 0, sizeof la);
        char out[LINE_MAX], got[5][LINE_MAX]; int tr[5] = { 0 }, k = 0, t;
        for (int step = 0; step < 2000; step++) {
            sim_step();
            while (k < 5 && uart_readline(&la, out, sizeof out, &t)) { strcpy(got[k], out); tr[k] = t; k++; }
        }
        CHECK(k == 4);
        CHECK(strcmp(got[0], "ping") == 0 && strcmp(got[1], "status") == 0 && !tr[0] && !tr[1]);
        CHECK(strlen(got[2]) == LINE_MAX - 1u && strncmp(got[2], "AT+THIS_COMMAND", 15) == 0 && tr[2] == 1);
        CHECK(strcmp(got[3], "") == 0 && !tr[3]);              /* 빈 줄 하나 (CRLF CRLF) */
        CHECK(la.len == 1 && la.buf[0] == 'x');                /* 끝나지 않은 줄은 남아 있다 */
    }

    /* 연습 4 */
    CHECK(rx_cap_needed(115200, 5000) == 64);              /* 57.6 → 58 → 64 */
    CHECK(rx_cap_needed(921600, 5000) == 512);             /* 460.8 → 461 → 512 */
    CHECK(rx_cap_needed(115200, 1000) == 16);              /* 11.52 → 12 → 16 */
    CHECK(rx_cap_needed(9600, 100) == 1);
    {
        int bad = 0;
        const uint32_t Ps[] = { 50, 100, 160, 200, 320, 400, 640, 800, 1280, 1600 };
        for (unsigned i = 0; i < sizeof Ps / sizeof Ps[0]; i++) {
            uint32_t cap = rx_cap_needed_ticks(Ps[i], 10);
            if (rx_drops_with(cap, Ps[i], 500) != 0) bad++;                     /* 계산한 크기: 0 drop */
            if (cap >= 2 && rx_drops_with(cap / 2, Ps[i], 500) == 0 &&
                (cap / 2) * 10u < Ps[i]) bad++;                                  /* 절반 크기: 넘쳐야 한다 */
            if (Ps[i] == 400) CHECK(cap == 64 && rx_drops_with(32, 400, 500) == 96);   /* demo 표 3 과 같은 값 */
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
