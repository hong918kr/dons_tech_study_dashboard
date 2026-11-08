/* C-B09 추가 연습 — 한 번에 검증 */
#include <setjmp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 연습 1: volatile 이 "어디에" 붙었나 — _Generic 으로 포인터가 가리키는 대상의 한정자를 판별
 * _Generic 의 제어식은 lvalue 변환을 거쳐 최상위(top-level) 한정자가 떨어진다. */
#define POINTEE_IS_VOLATILE(p) _Generic((p),            \
        volatile uint32_t *: 1, const volatile uint32_t *: 1, default: 0)

/* 연습 2: 읽으면 지워지는(read-to-clear) 이벤트 레지스터 */
#define EV_RX  (1u << 0)
#define EV_ERR (1u << 1)
static uint32_t hw_ev;                          /* 하드웨어 내부 상태 (시뮬레이션) */
static uint32_t ev_read(void) { uint32_t v = hw_ev; hw_ev = 0; return v; }  /* 읽는 순간 0 */
static uint32_t handle_wrong(void) {            /* 비트마다 다시 읽음 → 두 번째 읽기는 0 */
    uint32_t done = 0;
    if (ev_read() & EV_RX)  done |= EV_RX;
    if (ev_read() & EV_ERR) done |= EV_ERR;
    return done;
}
static uint32_t handle_right(void) {            /* 한 번 읽어 지역 변수(스냅샷)에서 검사 */
    uint32_t ev = ev_read(), done = 0;
    if (ev & EV_RX)  done |= EV_RX;
    if (ev & EV_ERR) done |= EV_ERR;
    return done;
}

/* 연습 3: setjmp/longjmp 뒤에도 값이 살아 있어야 하는 지역 변수 → volatile (C11 7.13.2.1p3) */
static jmp_buf g_env;
static void fail_op(void) { longjmp(g_env, 1); }
static int retry_until_three(void) {
    volatile int tries = 0;                     /* volatile 이 없으면 longjmp 후 값이 불확정 */
    (void)setjmp(g_env);                        /* 여기로 되돌아온다 */
    tries = tries + 1;
    if (tries < 3) fail_op();
    return tries;
}

/* 연습 4: volatile 대신 C11 원자 연산 — ISR 이 이벤트 비트를 세우고 main 이 한 번에 가져가기 */
static atomic_uint g_events;
static atomic_uint g_ticket;
static void     isr_post(unsigned bit) { atomic_fetch_or(&g_events, bit); }       /* 한 번에 RMW */
static unsigned main_take(void)        { return atomic_exchange(&g_events, 0u); } /* 읽고-0쓰기 한 번에 */
static unsigned ticket_take(void)      { return atomic_fetch_add(&g_ticket, 1u); } /* 이전 값을 돌려준다 */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    volatile uint32_t *a = NULL;                /* 가리키는 대상이 volatile (레지스터 포인터) */
    uint32_t volatile *b = NULL;                /* a 와 같은 타입 */
    uint32_t *volatile c = NULL;                /* 포인터 변수 자체가 volatile */
    const volatile uint32_t *d = NULL;          /* 읽기 전용 상태 레지스터 */
    CHECK(POINTEE_IS_VOLATILE(a) == 1);
    CHECK(POINTEE_IS_VOLATILE(b) == 1);
    CHECK(POINTEE_IS_VOLATILE(c) == 0);
    CHECK(POINTEE_IS_VOLATILE(d) == 1);

    /* 연습 2 */
    hw_ev = EV_RX | EV_ERR;
    CHECK(handle_wrong() == EV_RX);             /* EV_ERR 를 잃어버렸다 */
    hw_ev = EV_RX | EV_ERR;
    CHECK(handle_right() == (EV_RX | EV_ERR));

    /* 연습 3 */
    CHECK(retry_until_three() == 3);

    /* 연습 4 */
    isr_post(1u << 3);
    isr_post(1u << 0);
    CHECK(main_take() == 0x9u);
    CHECK(main_take() == 0u);                   /* 이미 가져갔다 */
    CHECK(ticket_take() == 0u && ticket_take() == 1u && ticket_take() == 2u);
    CHECK(atomic_load(&g_ticket) == 3u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
