/* C-B15 매크로 기초: 괄호 · do-while(0) · 다중 평가
 * 지금 매크로들은 "순진한(naive)" 버전이다. 테스트가 그 버그를 재현한다.
 * TODO 표시된 매크로/함수만 고쳐서 전부 PASS 로 만든다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 괄호 (우선순위) ---------------- */
#define SQUARE(x)       ((x) * (x))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))     /* 괄호로 우선순위는 해결. 다중 평가는 남는다 */

/* ---------------- Part B: 다중 문장 · 다중 평가 ---------------- */
static int g_led_mask, g_led_events;
static void led_on(int n) { g_led_mask |= 1 << n; }

/* LED를 켜고 이벤트 수를 센다 — 문장 2개짜리 매크로 */
#define LED_ON_COUNT(n) do { led_on(n); g_led_events++; } while (0)

/* 아무 타입이나 교환 */
#define SWAP(a, b)      do { __typeof__(a) swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0)

/* 인자를 정확히 한 번만 평가하는 MAX — GNU statement expression 으로 */
#define MAX_ONCE(a, b)  __extension__ ({ __typeof__(a) max_a_ = (a); __typeof__(b) max_b_ = (b); \
                                          max_a_ > max_b_ ? max_a_ : max_b_; })

/* 표준 C 대안: static inline 함수 (인자는 호출 전에 한 번만 평가된다) */
static inline uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

/* 가짜 UART 수신 FIFO: DR을 읽을 때마다 한 바이트가 빠진다 (읽기에 부작용이 있는 레지스터) */
static const uint8_t rx_bytes[4] = { 7, 9, 2, 11 };
static unsigned rx_pos;
static uint32_t uart_read_dr(void) { return rx_bytes[rx_pos++ % 4u]; }

/* ---------------- Part C: 레지스터 매크로 (#, ##) + 임계 구역 ---------------- */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t BRR;
} UART_TypeDef;
static UART_TypeDef fake_uart1;                    /* 실제 칩이라면 (UART_TypeDef *)0x40011000 */
#define UART1 (&fake_uart1)

/* CMSIS 스타일 _Pos / _Msk 정의 (벤더 헤더가 수천 줄 이런 식이다) */
#define UART_CR1_UE_Pos     0u
#define UART_CR1_UE_Msk     (1u << UART_CR1_UE_Pos)
#define UART_CR1_TE_Pos     3u
#define UART_CR1_TE_Msk     (1u << UART_CR1_TE_Pos)
#define UART_CR2_STOP_Pos   12u
#define UART_CR2_STOP_Msk   (3u << UART_CR2_STOP_Pos)

#define STR(x)          #x
#define XSTR(x)         STR(x)                      /* 한 단계 거치면 x 가 먼저 펼쳐진다 */
#define REG_NAME(P, R)  #P "_" #R                   /* 문자열 리터럴 이어붙이기 */
#define REG_POS(P, R, F) P##_##R##_##F##_Pos
#define REG_MSK(P, R, F) P##_##R##_##F##_Msk
#define REG_FIELD_READ(inst, P, R, F)  (((inst)->R & REG_MSK(P, R, F)) >> REG_POS(P, R, F))
#define REG_FIELD_WRITE(inst, P, R, F, v) \
    do { uint32_t reg_v_ = (inst)->R & ~REG_MSK(P, R, F); \
         reg_v_ |= ((uint32_t)(v) << REG_POS(P, R, F)) & REG_MSK(P, R, F); \
         (inst)->R = reg_v_; } while (0)

/* 가짜 Cortex-M PRIMASK (1 = 인터럽트 마스크됨). 실제로는 CMSIS
 * __get_PRIMASK() / __set_PRIMASK() / __disable_irq() / __enable_irq() */
static uint32_t g_primask;
static inline uint32_t cpu_get_primask(void)     { return g_primask; }
static inline void     cpu_set_primask(uint32_t v) { g_primask = v; }
static inline void     cpu_disable_irq(void)     { g_primask = 1u; }
static inline void     cpu_enable_irq(void)      { g_primask = 0u; }

/* 인자로 받은 문장들을 인터럽트를 막은 채 실행 */
#define CRITICAL(...)   do { uint32_t primask_saved_ = cpu_get_primask(); cpu_disable_irq(); \
                             __VA_ARGS__; cpu_set_primask(primask_saved_); } while (0)

static int g_shared;
static void shared_inc(void) { CRITICAL(g_shared++); }    /* 다른 모듈 함수도 임계 구역을 쓴다 */

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    volatile bool go = false;      /* 런타임 조건 (컴파일러가 미리 접지 못하게) */
    int r, a = 2;
    cpu_enable_irq();

    /* Part A — 결과를 r 에 먼저 받는다 (CHECK 안에 바로 넣으면 CHECK 식과 또 섞인다) */
    r = SQUARE(3);              CHECK(r == 9);
    r = SQUARE(a + 1);          CHECK(r == 9);
    r = 100 / SQUARE(5);        CHECK(r == 4);
    r = MAX(3, 2) + 10;         CHECK(r == 13);
    r = 10 + MAX(3, 2);         CHECK(r == 13);

    /* Part B — 다중 문장 */
    if (go) LED_ON_COUNT(1);
    CHECK(g_led_mask == 0 && g_led_events == 0);
    for (int k = 0; k < 4; k++) LED_ON_COUNT(k);
    CHECK(g_led_mask == 0xF && g_led_events == 4);

    /* Part B — SWAP 은 타입을 지켜야 한다 */
    int x = 1, y = 2;
    SWAP(x, y);                 CHECK(x == 2 && y == 1);
    double p = 1.5, q = 2.25;
    SWAP(p, q);                 CHECK(p == 2.25 && q == 1.5);
    uint64_t u = 0x100000001ull, v = 7u;
    SWAP(u, v);                 CHECK(u == 7u && v == 0x100000001ull);

    /* Part B — 다중 평가 */
    int i = 5, j = 3;
    r = MAX_ONCE(i++, j);       CHECK(r == 5 && i == 6);
    rx_pos = 0;
    uint32_t m = MAX_ONCE(uart_read_dr(), 4u);
    CHECK(m == 7u && rx_pos == 1u);            /* FIFO에서 딱 한 바이트만 빠져야 한다 */
    rx_pos = 0;
    m = max_u32(uart_read_dr(), 4u);
    CHECK(m == 7u && rx_pos == 1u);

    /* Part C — ## 로 만든 레지스터 매크로 */
    UART1->CR1 = 0u;
    UART1->CR2 = 0x00003005u;                  /* STOP = 3, 다른 비트 0x5 */
    CHECK(REG_MSK(UART, CR1, TE) == (1u << 3));
    CHECK(REG_MSK(UART, CR2, STOP) == 0x3000u);
    REG_FIELD_WRITE(UART1, UART, CR2, STOP, 2u);
    CHECK(UART1->CR2 == 0x00002005u);          /* STOP 만 바뀌고 0x5 는 그대로 */
    CHECK(REG_FIELD_READ(UART1, UART, CR2, STOP) == 2u);
    if (go) REG_FIELD_WRITE(UART1, UART, CR1, TE, 1u);   /* if/else 안에서도 한 문장처럼 */
    else    REG_FIELD_WRITE(UART1, UART, CR1, UE, 1u);
    CHECK(UART1->CR1 == UART_CR1_UE_Msk);

    /* Part C — # 문자열화 */
    CHECK(strcmp(REG_NAME(UART, CR2), "UART_CR2") == 0);
    CHECK(strcmp(STR(UART_CR2_STOP_Pos), "UART_CR2_STOP_Pos") == 0);   /* # 는 인자를 펼치지 않는다 */
    CHECK(strcmp(XSTR(UART_CR2_STOP_Pos), "12u") == 0);

    /* Part C — 임계 구역 */
    int shared = 0;
    if (go) CRITICAL(shared++);
    CHECK(shared == 0 && cpu_get_primask() == 0u);
    uint32_t pm_inside = 99u;
    CRITICAL(shared_inc(); pm_inside = cpu_get_primask());
    CHECK(g_shared == 1 && pm_inside == 1u);   /* 안쪽 임계 구역이 끝나도 아직 마스크 상태 */
    CHECK(cpu_get_primask() == 0u);            /* 바깥이 끝나면 원래대로 */
    cpu_set_primask(1u);                       /* 이미 IRQ가 꺼진 곳(ISR, 다른 임계 구역)에서 호출 */
    CRITICAL(g_shared++);
    CHECK(cpu_get_primask() == 1u);            /* 멋대로 켜면 안 된다 */
    cpu_set_primask(0u);
    CRITICAL(shared = 1, shared += 2);         /* 쉼표가 있어도 __VA_ARGS__ 라 OK */
    CHECK(shared == 3);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
