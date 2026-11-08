/* C-A11 면접형 코드 리뷰: ISR·드라이버 버그 찾기
 *
 * 리뷰 대상: 아래 "스니펫 1~5" (가상의 STM32 풍 펌웨어). 각 스니펫에 숨은 버그를 찾아 고친다.
 * 맨 아래 테스트는 버그를 "결정적으로" 재현한다 — 처음엔 일부 FAIL, 다 고치면 전부 PASS.
 *
 * 호스트 모델 (리뷰 대상 아님, 고치지 않는다):
 *   - 레지스터는 READ_REG / WRITE_REG / SET_BIT / CLEAR_BIT 로 접근한다 (STM32 CMSIS 헤더와 같은 이름).
 *     타깃에선 READ_REG(r) = (r), WRITE_REG(r,v) = ((r) = (v)), SET_BIT(r,b) = ((r) |= (b)) 다.
 *     호스트에선 bus_rd/bus_wr 를 거쳐 ① 레지스터 의미(rc_w1, rc_w0, BSRR, IFCR)를 흉내 내고
 *     ② "n 번째 접근 직후" 에 인터럽트를 끼워 넣는다 → 경쟁(race)을 매번 똑같이 재현.
 *   - irq_save()/irq_restore() = CMSIS 의 pm = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm);
 *     막혀 있는 동안 온 인터럽트는 펜딩됐다가 복원하는 순간 실행된다.
 *   - log_printf = printf 를 UART 로 retarget 한 것 (블로킹 송신). pvPortMalloc/vPortFree = FreeRTOS 힙.
 *     둘 다 ISR 에서 불리면 g_unsafe_in_isr 를 센다.
 *   - rx_buf / pkt_t.payload 뒤의 RX_GUARD / PKT_RZ 바이트는 테스트용 red zone 이다.
 *     넘침(overrun)을 UB 없이 "가드 바이트 손상" 으로 관찰하려고 붙였다. 실제 코드엔 없다.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 호스트 모델 (주어진 코드) ======================= */
typedef struct { volatile uint32_t SR, DR; } uart_regs_t;
typedef struct { volatile uint32_t ISR, IFCR; } dma_regs_t;      /* ISR: 읽기 전용, IFCR: 1 쓰면 클리어 */
typedef struct { volatile uint32_t SR; } tim_regs_t;              /* SR: rc_w0 (0 을 쓴 비트만 클리어)  */
typedef struct { volatile uint32_t IDR, ODR, BSRR; } gpio_regs_t; /* BSRR: 하위16 set, 상위16 reset     */
typedef struct { volatile uint32_t IMR, PR; } exti_regs_t;        /* PR: rc_w1 (1 을 쓴 비트만 클리어)  */
static uart_regs_t UART1_m;
static dma_regs_t  DMA1_m;
static tim_regs_t  TIM3_m;
static gpio_regs_t GPIOA_m;
static exti_regs_t EXTI_m;
#define UART1 (&UART1_m)
#define DMA1  (&DMA1_m)
#define TIM3  (&TIM3_m)
#define GPIOA (&GPIOA_m)
#define EXTI  (&EXTI_m)

static int g_nacc, g_fire_at = -1;            /* 버스 접근 수 / 이 번호의 접근 직후 IRQ (-1 = 없음) */
static void (*g_irq)(void);                   /* 그때 실행될 ISR                                 */
static uint32_t g_primask;                    /* 1 = 인터럽트 막힘                               */
static int g_irq_pending, g_in_isr, g_isr_runs, g_unsafe_in_isr;

static void run_isr(void (*isr)(void)) { g_in_isr++; isr(); g_in_isr--; g_isr_runs++; }
static void irq_line(void) { if (g_primask) g_irq_pending = 1; else run_isr(g_irq); }
static void bus_tick(void) { if (++g_nacc == g_fire_at) { g_fire_at = -1; irq_line(); } }
static uint32_t bus_rd(const volatile uint32_t *r) { uint32_t v = *r; bus_tick(); return v; }
static void bus_wr(volatile uint32_t *r, uint32_t v) {
    if (r == &EXTI->PR)          *r &= ~v;                       /* rc_w1                */
    else if (r == &TIM3->SR)     *r &= v;                        /* rc_w0                */
    else if (r == &DMA1->IFCR)   DMA1->ISR &= ~v;                /* 쓰기 전용 클리어 레지스터 */
    else if (r == &GPIOA->BSRR)  GPIOA->ODR = (GPIOA->ODR & ~(v >> 16)) | (v & 0xFFFFu); /* set 우선 */
    else                         *r = v;
    bus_tick();
}
#define READ_REG(r)      bus_rd(&(r))
#define WRITE_REG(r, v)  bus_wr(&(r), (v))
#define SET_BIT(r, b)    WRITE_REG((r), READ_REG(r) | (b))        /* CMSIS: ((r) |= (b))  */
#define CLEAR_BIT(r, b)  WRITE_REG((r), READ_REG(r) & ~(b))       /* CMSIS: ((r) &= ~(b)) */

uint32_t irq_save(void) { uint32_t old = g_primask; g_primask = 1u; return old; }
void irq_restore(uint32_t old) {
    g_primask = old;
    if (!g_primask && g_irq_pending) { g_irq_pending = 0; run_isr(g_irq); }
}

static char g_log[256];
static size_t g_log_len;
void log_printf(const char *fmt, ...) {       /* printf → UART (블로킹). ISR 에서 부르면 안 된다 */
    if (g_in_isr) g_unsafe_in_isr++;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_log + g_log_len, sizeof g_log - g_log_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_log_len = strlen(g_log);
}
void *pvPortMalloc(size_t n) { if (g_in_isr) g_unsafe_in_isr++; return malloc(n); }
void vPortFree(void *p)      { if (g_in_isr) g_unsafe_in_isr++; free(p); }

static volatile uint32_t g_ms;                /* SysTick 이 올리는 ms 카운터 */
#define RX_GUARD 4u
#define PKT_RZ   32u

/* ======================= 리뷰 대상 스니펫 (TODO: 버그를 찾아 고친다) ======================= */

/* ---- 스니펫 1 — UART 수신 링 버퍼: ISR 가 넣고 main 이 꺼낸다 ---- */
#define RX_SIZE 8u
static volatile uint8_t  rx_buf[RX_SIZE + RX_GUARD];   /* 실제 코드: rx_buf[RX_SIZE] */
static volatile uint32_t rx_head, rx_tail;    /* head: ISR 만 씀, tail: main 만 씀 */
static volatile uint32_t rx_overflow;         /* 공간이 없어 버린 바이트 수 */

void USART1_IRQHandler(void) {
    uint8_t c = (uint8_t)READ_REG(UART1->DR); /* 가득 차도 DR 은 읽는다 (RXNE 클리어) */
    uint32_t h = rx_head, next = (h + 1u) % RX_SIZE;
    if (next == rx_tail) { rx_overflow = rx_overflow + 1u; return; }   /* 가득: 버리고 센다 */
    rx_buf[h] = c;
    rx_head = next;                           /* 데이터를 쓴 "뒤에" 인덱스를 공개 */
}
int uart_getc(void) {                         /* 꺼낼 게 없으면 -1 */
    uint32_t t = rx_tail;
    if (rx_head == t) return -1;
    uint8_t c = rx_buf[t];
    rx_tail = (t + 1u) % RX_SIZE;             /* 데이터를 읽은 "뒤에" 자리를 반납 */
    return c;
}
int uart_getc_wait(void) {                    /* main: 한 바이트 올 때까지 기다린다 */
    int c;
    while ((c = uart_getc()) < 0) { }
    return c;
}

/* ---- 스니펫 2 — 패킷 헤더 파서 ----
 * 프레임: [0]=0xA5  [1]=type  [2..3]=len (big-endian)  [4..7]=ts (little-endian)  [8..]=payload */
#define PKT_MAX 240u
typedef struct {
    uint8_t  type;
    uint16_t len;
    uint32_t ts;
    uint8_t  payload[PKT_MAX + PKT_RZ];       /* 실제 코드: payload[PKT_MAX] */
} pkt_t;

int pkt_parse(const char *rx, size_t n, pkt_t *p) {
    const uint8_t *u = (const uint8_t *)rx;   /* 바이트는 언제나 unsigned 로 읽는다 */
    if (n < 8u || u[0] != 0xA5u) return -1;
    p->type = u[1];
    p->len  = (uint16_t)(((uint32_t)u[2] << 8) | u[3]);
    p->ts   = (uint32_t)u[4] | ((uint32_t)u[5] << 8) | ((uint32_t)u[6] << 16) | ((uint32_t)u[7] << 24);
    if (p->len > PKT_MAX || p->len > n - 8u) return -1;   /* 버퍼 용량 + 잘린 프레임 */
    memcpy(p->payload, u + 8, p->len);
    return 0;
}

/* ---- 스니펫 3 — DMA 완료 이벤트 플래그: ISR 가 세우고 main 이 가져간다 ---- */
#define DMA_ISR_TCIF1 (1u << 1)               /* 전송 완료 */
#define DMA_ISR_TEIF1 (1u << 3)               /* 전송 에러 */
#define EV_ADC_DONE   (1u << 0)
#define EV_DMA_ERR    (1u << 1)
static volatile uint32_t g_events;

void DMA1_IRQHandler(void) {
    uint32_t isr = READ_REG(DMA1->ISR);
    WRITE_REG(DMA1->IFCR, isr);               /* 본 플래그만 클리어 */
    if (isr & DMA_ISR_TCIF1) SET_BIT(g_events, EV_ADC_DONE);
    if (isr & DMA_ISR_TEIF1) SET_BIT(g_events, EV_DMA_ERR);
}
uint32_t take_events(void) {                  /* main 루프가 매번 호출 */
    uint32_t pm = irq_save();                 /* 읽기~지우기를 ISR 에 대해 한 덩어리로 */
    uint32_t ev = READ_REG(g_events);
    WRITE_REG(g_events, 0u);
    irq_restore(pm);                          /* enable 이 아니라 "원래대로" */
    return ev;
}

/* ---- 스니펫 4 — 에러 LED (main) 와 하트비트 LED (TIM3 ISR, 1 kHz) ---- */
#define PIN(n)        (1u << (n))
#define LED_ERR       PIN(15)                 /* main 이 켜고 끈다 */
#define HB_PIN        PIN(5)                  /* TIM3 ISR 가 토글한다 */
#define TIM_SR_UIF    (1u << 0)
#define TIM_SR_CC1IF  (1u << 1)

void TIM3_IRQHandler(void) {
    WRITE_REG(TIM3->SR, ~TIM_SR_UIF);                         /* UIF 클리어 (rc_w0: 이 줄은 맞다) */
    uint32_t odr = READ_REG(GPIOA->ODR);                      /* 하트비트 토글: 내 핀만 BSRR 로 */
    WRITE_REG(GPIOA->BSRR, ((odr & HB_PIN) << 16) | (~odr & HB_PIN));
}
void led_err(int on) {
    WRITE_REG(GPIOA->BSRR, on ? LED_ERR : LED_ERR << 16);    /* 쓰기 1번, 다른 핀은 안 건드림 */
}

/* ---- 스니펫 5 — 버튼 EXTI0 ISR: 눌린 시각을 기록하고 로그를 남긴다 ---- */
#define EXTI_PR_PR0 (1u << 0)
#define EXTI_PR_PR3 (1u << 3)
#define BTN_Q 4u                              /* 고정 크기 SPSC 큐 (ISR → main) */
static volatile uint32_t g_btn_q[BTN_Q];
static volatile uint32_t g_btn_wr, g_btn_rd, g_btn_drop;   /* wr: ISR 만, rd: main 만 씀 */

void EXTI0_IRQHandler(void) {
    WRITE_REG(EXTI->PR, EXTI_PR_PR0);                         /* rc_w1: line 0 만 클리어 */
    uint32_t w = g_btn_wr, next = (w + 1u) % BTN_Q;
    if (next == g_btn_rd) { g_btn_drop = g_btn_drop + 1u; return; }   /* 가득: 세기만 */
    g_btn_q[w] = g_ms;
    g_btn_wr = next;
}
int btn_poll(uint32_t *t_ms) {                /* main: 하나 꺼내면 1, 없으면 0 */
    uint32_t r = g_btn_rd;
    if (r == g_btn_wr) return 0;
    *t_ms = g_btn_q[r];
    g_btn_rd = (r + 1u) % BTN_Q;
    log_printf("btn @%lu ms\n", (unsigned long)*t_ms);   /* 출력은 main 에서 */
    return 1;
}

/* ======================= 테스트 하네스 (주어진 코드) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define IS_VOLATILE_U32(x) _Generic(&(x), volatile uint32_t *: 1, default: 0)
#define IS_VOLATILE_U8(x)  _Generic(&(x), volatile uint8_t *: 1, default: 0)

static void bus_reset(void (*isr)(void), int fire_at) {
    g_nacc = 0; g_fire_at = fire_at; g_irq = isr;
    g_primask = 0u; g_irq_pending = 0; g_isr_runs = 0; g_unsafe_in_isr = 0;
    g_log[0] = '\0'; g_log_len = 0;
}
static void rx_reset(void) {
    for (uint32_t i = 0; i < RX_SIZE + RX_GUARD; i++) rx_buf[i] = (i < RX_SIZE) ? 0u : 0xEEu;
    rx_head = rx_tail = 0u; rx_overflow = 0u;
}
static int rx_guard_ok(void) {
    for (uint32_t i = RX_SIZE; i < RX_SIZE + RX_GUARD; i++) if (rx_buf[i] != 0xEEu) return 0;
    return 1;
}
static void rx_push(const char *s) {          /* 바이트마다 "수신 인터럽트" 한 번 */
    for (; *s; s++) { UART1->DR = (uint8_t)*s; run_isr(USART1_IRQHandler); }
}
static size_t rx_drain(char *out, size_t cap) {
    size_t k = 0;
    int c;
    while (k + 1 < cap && (c = uart_getc()) >= 0) out[k++] = (char)c;
    out[k] = '\0';
    return k;
}
static int rz_ok(const pkt_t *p) {
    for (uint32_t i = PKT_MAX; i < PKT_MAX + PKT_RZ; i++) if (p->payload[i] != 0xEEu) return 0;
    return 1;
}
static void pkt_clear(pkt_t *p) { memset(p, 0, sizeof *p); memset(p->payload + PKT_MAX, 0xEE, PKT_RZ); }
static size_t mkframe(char *f, unsigned len_field, uint32_t ts, size_t payload_bytes) {
    const uint8_t b[8] = { 0xA5u, 0x01u, (uint8_t)(len_field >> 8), (uint8_t)len_field,
                           (uint8_t)ts, (uint8_t)(ts >> 8), (uint8_t)(ts >> 16), (uint8_t)(ts >> 24) };
    memcpy(f, b, 8);
    for (size_t i = 0; i < payload_bytes; i++) f[8 + i] = (char)(uint8_t)(i * 7u + 1u);
    return 8u + payload_bytes;
}
/* 고장 처리 ISR (TIM3 보다 우선순위가 높다): 에러 LED 를 켠다 */
static void fault_isr(void) { led_err(1); }
static void btn_reset(void) {
    uint32_t t;
    while (btn_poll(&t)) { }
    EXTI->PR = 0u;
}

int main(void) {
    char out[32];
    static char frame[300];
    static pkt_t pk;
    uint32_t t = 0;

    /* ---- 스니펫 1: UART 링 버퍼 ---- */
    CHECK(IS_VOLATILE_U32(rx_head) && IS_VOLATILE_U32(rx_tail));   /* ISR↔main 공유 인덱스 */
    CHECK(IS_VOLATILE_U8(rx_buf[0]));                              /* 공유 데이터도 (순서) */
    bus_reset(NULL, -1); rx_reset();
    rx_push("abc");
    CHECK(rx_drain(out, sizeof out) == 3 && strcmp(out, "abc") == 0);
    CHECK(uart_getc() == -1);
    rx_reset();
    int wrap_ok = 1;
    for (int r = 0; r < 4; r++) {                 /* 5개 넣고 5개 빼기 × 4 → 경계를 여러 번 넘는다 */
        rx_push("01234");
        wrap_ok &= rx_drain(out, sizeof out) == 5 && strcmp(out, "01234") == 0;
        wrap_ok &= rx_head < RX_SIZE && rx_tail < RX_SIZE;
    }
    CHECK(wrap_ok);
    CHECK(rx_guard_ok());                         /* rx_buf[RX_SIZE..] 를 건드리지 않았다 */
    rx_reset();
    rx_push("ABCDEFGHIJ");                        /* 읽지 않고 10바이트: 가득 차면 버린다 */
    CHECK(rx_drain(out, sizeof out) == RX_SIZE - 1u && strcmp(out, "ABCDEFG") == 0);
    CHECK(rx_overflow == 3u && rx_guard_ok());

    /* ---- 스니펫 2: 패킷 파서 ---- */
    size_t n = mkframe(frame, 16u, 0x12345678u, 16u);
    pkt_clear(&pk);
    CHECK(pkt_parse(frame, n, &pk) == 0 && pk.len == 16u && pk.ts == 0x12345678u && pk.payload[15] == 106u);
    n = mkframe(frame, 0x0084u, 0x000000F0u, 0x84u);  /* len 132, ts 0xF0: 최상위 비트가 선 바이트 */
    pkt_clear(&pk);
    CHECK(pkt_parse(frame, n, &pk) == 0);
    CHECK(pk.len == 132u);
    CHECK(pk.ts == 0x000000F0u);
    CHECK(pk.payload[131] == (uint8_t)(131u * 7u + 1u) && rz_ok(&pk));
    n = mkframe(frame, 0x0110u, 1u, 0x110u);          /* len 272 > PKT_MAX 240: 프레임은 온전 */
    pkt_clear(&pk);
    CHECK(pkt_parse(frame, n, &pk) == -1);
    CHECK(rz_ok(&pk));                                /* payload 뒤를 덮어쓰지 않았다 */
    n = mkframe(frame, 16u, 0u, 8u);                  /* len 16 인데 payload 8 바이트뿐 */
    CHECK(pkt_parse(frame, n, &pk) == -1);
    CHECK(pkt_parse(frame, 5u, &pk) == -1);

    /* ---- 스니펫 3: 이벤트 플래그 ---- */
    bus_reset(DMA1_IRQHandler, -1); g_events = 0u;
    DMA1->ISR = DMA_ISR_TCIF1 | DMA_ISR_TEIF1;
    run_isr(DMA1_IRQHandler);
    CHECK(g_events == (EV_ADC_DONE | EV_DMA_ERR) && DMA1->ISR == 0u);
    CHECK(take_events() == (EV_ADC_DONE | EV_DMA_ERR) && take_events() == 0u);
    bus_reset(DMA1_IRQHandler, 1); g_events = 0u;     /* main 이 g_events 를 읽은 직후 DMA 완료 */
    DMA1->ISR = DMA_ISR_TCIF1;
    uint32_t e1 = take_events();
    uint32_t e2 = take_events();
    CHECK((e1 | e2) == EV_ADC_DONE && g_isr_runs == 1);   /* 이벤트를 잃지 않는다 */
    CHECK(g_primask == 0u && g_irq_pending == 0);
    bus_reset(DMA1_IRQHandler, -1); g_events = 0u;
    uint32_t pm = irq_save();                          /* 이미 막힌 상태에서 불러도 */
    (void)take_events();
    CHECK(g_primask == 1u);                            /* 나올 때 멋대로 풀지 않는다 */
    irq_restore(pm);

    /* ---- 스니펫 4: GPIO ---- */
    CHECK(_Generic(PIN(0), unsigned int: 1, default: 0));   /* PIN(15) << 16 이 UB 가 아니려면 */
    bus_reset(NULL, -1);
    TIM3->SR = TIM_SR_UIF | TIM_SR_CC1IF; GPIOA->ODR = 0u;
    run_isr(TIM3_IRQHandler);
    CHECK(TIM3->SR == TIM_SR_CC1IF && GPIOA->ODR == (uint32_t)HB_PIN);  /* UIF 만 지운다 */
    GPIOA->ODR = 0u;
    led_err(1);
    CHECK(GPIOA->ODR == (uint32_t)LED_ERR);
    led_err(0);
    CHECK(GPIOA->ODR == 0u);
    bus_reset(TIM3_IRQHandler, 1); GPIOA->ODR = 0u;   /* main 의 첫 접근 직후 하트비트 ISR */
    led_err(1);
    CHECK(GPIOA->ODR == (uint32_t)(LED_ERR | HB_PIN) && g_isr_runs == 1);
    bus_reset(TIM3_IRQHandler, 1); GPIOA->ODR = (uint32_t)LED_ERR;
    led_err(0);
    CHECK(GPIOA->ODR == (uint32_t)HB_PIN && g_isr_runs == 1);
    bus_reset(fault_isr, 2); GPIOA->ODR = 0u;         /* TIM3 ISR 가 ODR 를 읽은 직후 더 급한 ISR 가 선점 */
    run_isr(TIM3_IRQHandler);
    CHECK(GPIOA->ODR == (uint32_t)(LED_ERR | HB_PIN) && g_isr_runs == 2);

    /* ---- 스니펫 5: 버튼 ISR ---- */
    bus_reset(NULL, -1); btn_reset();
    EXTI->PR = EXTI_PR_PR0 | EXTI_PR_PR3;             /* line 0 과 line 3 이 동시에 펜딩 */
    g_ms = 1234u;
    g_log[0] = '\0'; g_log_len = 0;
    run_isr(EXTI0_IRQHandler);
    CHECK(EXTI->PR == EXTI_PR_PR3);                   /* line 3 의 펜딩은 남아 있어야 한다 */
    CHECK(g_unsafe_in_isr == 0);                      /* ISR 에서 malloc/printf 금지 */
    CHECK(btn_poll(&t) == 1 && t == 1234u);
    CHECK(strcmp(g_log, "btn @1234 ms\n") == 0);
    CHECK(btn_poll(&t) == 0);
    bus_reset(NULL, -1); btn_reset();
    g_ms = 100u; run_isr(EXTI0_IRQHandler);
    g_ms = 200u; run_isr(EXTI0_IRQHandler);
    uint32_t t1 = 0, t2 = 0;
    CHECK(btn_poll(&t1) == 1 && btn_poll(&t2) == 1 && t1 == 100u && t2 == 200u);  /* 순서 보존 */
    btn_reset();

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
