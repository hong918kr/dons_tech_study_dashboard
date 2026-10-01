/* 12_volatile_const.c — volatile 의미론과 레지스터 접근 래퍼 (모범답안)
 *
 *   cc -std=c11 -Wall -Wextra -O2 -g solutions/12_volatile_const.c -o sol_12 && ./sol_12
 *
 * 이 파일이 증명하려는 것
 *   volatile은 "컴파일러가 접근을 줄이거나 합치거나 옮기지 못한다"는 계약이다.
 *   그게 전부다. 원자성도, 배리어도, 캐시 무효화도 아니다.
 *
 * 어떻게 증명하는가 — 옵티마이저에 기대지 않는 방법
 *   "volatile을 빼면 컴파일러가 루프 밖으로 읽기를 끌어낸다"를 보여주려고
 *   최적화 결과를 assert 하는 것은 나쁜 테스트다. 최적화는 보장이 아니고
 *   컴파일러·버전·플래그에 따라 달라지며, 그런 코드는 대개 undefined behaviour에
 *   의지한다. 그래서 여기서는 다르게 한다.
 *
 *     (1) 하드웨어를 모델로 만든다. hw_tick()이 CPU와 무관하게 레지스터 값을
 *         바꾸는 "주변장치"의 역할을 한다.
 *     (2) 모든 레지스터 접근을 accessor 함수(reg_read/reg_write)로만 한다.
 *         accessor는 접근 횟수를 센다. 즉 "메모리 접근이 몇 번 일어났는가"를
 *         프로그램이 직접 관측할 수 있게 만든다.
 *     (3) "제대로 폴링하는 함수"와 "값을 한 번 읽어 스냅샷으로 도는 함수"를
 *         둘 다 손으로 작성한다. 후자는 컴파일러가 volatile 없는 코드에
 *         실제로 해도 되는 변형을 사람이 미리 적용해 둔 것이다.
 *     (4) 두 함수의 접근 횟수와 결과가 다른 것을 assert 한다.
 *
 *   결과: undefined behaviour 없이, 어떤 최적화 수준에서도 같은 값이 나오면서
 *   "읽기를 합치면 무엇이 깨지는가"를 정확히 재현한다.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 1. 가짜 주변장치 — 실제 UART 블록의 동작을 흉내 낸 예시 모델          */
/* ------------------------------------------------------------------ */

/* 예시 레지스터 비트 정의. 실제 칩 이름이 아니라 이 파일을 위한 가상 장치다.
 * 실제 SoC에서는 벤더 헤더의 USART_ISR_TXE 같은 이름이 여기 온다. */
#define ST_READY    (1u << 0)   /* 수신 데이터 있음                      */
#define ST_BUSY     (1u << 1)   /* 전송 중                               */
#define ST_OVERRUN  (1u << 2)   /* 오버런 (write-1-to-clear)             */
#define ST_ERRFLAGS (ST_OVERRUN)

#define CTRL_ENABLE (1u << 0)
#define CTRL_IRQ_EN (1u << 1)
#define CTRL_LOOPBK (1u << 4)

/* 장치 블록. CPU가 보는 세 레지스터는 volatile이다. 실제 타깃에서는
 *     #define UART0 ((volatile fake_uart_t *)0x40004400u)
 * 처럼 주소를 박아 쓴다. 여기서는 그냥 정적 변수로 둔다 — 값이 CPU 코드와
 * 무관하게 바뀐다는 성질만 있으면 volatile을 논하기에 충분하다. */
typedef struct {
    volatile uint32_t status;   /* RO: 하드웨어가 바꾼다. CPU는 읽기만        */
    volatile uint32_t data;     /* RO: FIFO가 내놓는 값                       */
    volatile uint32_t ctrl;     /* RW: CPU가 설정한다                         */
    volatile uint32_t icr;      /* WO: 1을 쓰면 status의 해당 에러가 지워진다 */

    /* --- 아래는 "모델"의 내부다. 실제 하드웨어에서는 실리콘 안이라
     *     CPU에서 보이지 않는다. CPU 쪽 코드는 절대 만지지 않는다. --- */
    uint32_t ticks;             /* hw_tick 호출 횟수                        */
    uint32_t ready_at;          /* 이 tick에서 ST_READY를 세운다            */
    uint32_t next_byte;         /* FIFO가 내놓을 다음 값                     */
} fake_uart_t;

static fake_uart_t g_dev;

/* 접근 횟수 카운터. 이게 이 문제의 계측기다. */
static uint32_t g_reads;
static uint32_t g_writes;

/* "하드웨어 시간이 1틱 흘렀다". CPU 코드가 아니라 실리콘이 하는 일을 대신한다.
 * 실제로는 인터럽트, DMA, 다른 코어, 또는 그냥 전기적 현상이 이 자리에 온다. */
void hw_tick(void)
{
    g_dev.ticks++;
    if (g_dev.ticks >= g_dev.ready_at) {
        g_dev.status |= ST_READY;        /* 수신 완료 */
    }
    if ((g_dev.ctrl & CTRL_ENABLE) != 0u) {
        g_dev.next_byte = (g_dev.next_byte + 1u) & 0xFFu;
    }
}

/* 장치를 초기 상태로. 카운터도 0으로 되돌린다. */
void hw_reset(uint32_t ready_at)
{
    g_dev.status    = 0u;
    g_dev.data      = 0u;
    g_dev.ctrl      = 0u;
    g_dev.icr       = 0u;
    g_dev.ticks     = 0u;
    g_dev.ready_at  = ready_at;
    g_dev.next_byte = 0u;
    g_reads         = 0u;
    g_writes        = 0u;
}

/* ------------------------------------------------------------------ */
/* 2. 레지스터 접근 래퍼 — 모든 접근이 여기를 지난다                    */
/* ------------------------------------------------------------------ */

/* 읽기 래퍼.
 *
 * 인자 타입이 `const volatile uint32_t *`인 것에 주의하라. 두 한정자는 서로
 * 다른 말을 한다.
 *   const    : 이 포인터로는 쓰지 않겠다 (컴파일러가 강제. 위반하면 컴파일 에러)
 *   volatile : 값이 이 프로그램 밖에서 바뀔 수 있다 (읽기를 줄이지 말라)
 * 상태 레지스터처럼 "CPU는 읽기만 하지만 값은 계속 변하는" 것이 정확히
 * const volatile이다. const만 붙이면 컴파일러가 "안 변하는 값"으로 착각해
 * 한 번만 읽어도 된다고 판단할 수 있다. */
uint32_t reg_read(const volatile uint32_t *reg)
{
    g_reads++;
    return *reg;                         /* 여기서 실제 버스 트랜잭션 1회 */
}

/* 쓰기 래퍼. */
void reg_write(volatile uint32_t *reg, uint32_t value)
{
    g_writes++;
    *reg = value;                        /* 버스 트랜잭션 1회 */
}

/* read-modify-write. 정확히 읽기 1회 + 쓰기 1회다.
 *
 * 이 함수는 원자적이지 않다. 읽기와 쓰기 사이에 하드웨어나 인터럽트가
 * 레지스터를 바꾸면 그 변경이 통째로 사라진다. volatile은 이걸 막아주지
 * 않는다 — 막아주는 건 critical section이나 하드웨어의 set/clear 레지스터다.
 * 테스트 test_rmw_is_not_atomic이 이 손실을 그대로 재현한다. */
void reg_modify(volatile uint32_t *reg, uint32_t clear_mask, uint32_t set_mask)
{
    uint32_t v = reg_read(reg);           /* [1] read  */
    v &= ~clear_mask;                     /* [2] modify — 레지스터와 무관한 연산 */
    v |= set_mask;
    reg_write(reg, v);                    /* [3] write */
}

void reg_set_bits(volatile uint32_t *reg, uint32_t mask)
{
    reg_modify(reg, 0u, mask);
}

void reg_clear_bits(volatile uint32_t *reg, uint32_t mask)
{
    reg_modify(reg, mask, 0u);
}

/* ------------------------------------------------------------------ */
/* 3. 폴링 — 매번 다시 읽는 버전 (올바름)                               */
/* ------------------------------------------------------------------ */

/* mask의 비트가 모두 설 때까지 최대 max_polls번 읽는다.
 * 반환: 남은 폴링 횟수와 무관하게 0 성공, -1 타임아웃.
 *
 * 루프를 한 바퀴 돌 때마다 reg_read를 한 번 부른다. 즉 매 반복마다 새 값을
 * 버스에서 가져온다. volatile 레지스터를 폴링하는 코드는 반드시 이 형태다. */
int reg_wait_bits(const volatile uint32_t *reg, uint32_t mask,
                  uint32_t max_polls, void (*tick)(void))
{
    for (uint32_t i = 0u; i < max_polls; i++) {
        uint32_t v = reg_read(reg);       /* ← 매 반복 새 읽기 */
        if ((v & mask) == mask) {
            return 0;
        }
        if (tick != NULL) {
            tick();                       /* 시간이 흐른다 = 하드웨어가 움직인다 */
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* 4. 폴링 — 값을 한 번 읽어 스냅샷으로 도는 버전 (틀림)                 */
/* ------------------------------------------------------------------ */

/* 같은 인자, 같은 의도. 다만 읽기를 루프 밖으로 끌어냈다.
 *
 * 이건 억지로 만든 코드가 아니다. reg가 volatile이 아니라면 컴파일러가
 * 이 변형을 정당하게 수행할 수 있다("loop-invariant code motion"). 옵티마이저가
 * 정말 그렇게 하는지에 테스트를 걸 수 없으니, 사람이 직접 그 변형을 적용한
 * 함수를 나란히 놓고 결과를 비교한다.
 *
 * 관측 가능한 차이 두 가지
 *   - 접근 횟수: 몇 바퀴를 돌든 reg_read는 딱 1회
 *   - 결과: 하드웨어가 나중에 비트를 세워도 영원히 못 본다 → 항상 타임아웃 */
int reg_wait_bits_snapshot(const volatile uint32_t *reg, uint32_t mask,
                           uint32_t max_polls, void (*tick)(void))
{
    uint32_t cached = reg_read(reg);      /* ← 읽기가 루프 밖으로 나왔다 */

    for (uint32_t i = 0u; i < max_polls; i++) {
        if ((cached & mask) == mask) {    /* 같은 값을 계속 다시 본다 */
            return 0;
        }
        if (tick != NULL) {
            tick();
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* 5. 두 번 읽기 — 합칠 수 없다는 것의 가장 작은 증거                    */
/* ------------------------------------------------------------------ */

/* 레지스터를 읽고, 하드웨어가 한 틱 움직인 뒤 다시 읽는다.
 * out_first / out_second 에 각각 담는다. 반환: 수행한 읽기 횟수.
 *
 * volatile 레지스터에서 두 읽기는 서로 다른 두 개의 관측이다. 컴파일러는
 * 이걸 하나로 합치거나 순서를 바꾸거나 없앨 수 없다. 두 값이 다를 수 있다는
 * 사실 자체가 volatile이 필요한 이유다. */
uint32_t reg_read_twice(const volatile uint32_t *reg,
                        uint32_t *out_first, uint32_t *out_second,
                        void (*tick)(void))
{
    uint32_t before = g_reads;

    *out_first = reg_read(reg);
    if (tick != NULL) {
        tick();
    }
    *out_second = reg_read(reg);

    return g_reads - before;
}

/* ------------------------------------------------------------------ */
/* 6. write-1-to-clear — 쓰기도 합칠 수 없다                            */
/* ------------------------------------------------------------------ */

/* 에러 플래그를 지운다. 많은 주변장치가 상태 레지스터를 읽기 전용으로 두고,
 * 별도의 clear 레지스터(ICR)에 1을 쓰면 해당 플래그가 지워지는 방식을 쓴다
 * (STM32의 USART_ISR / USART_ICR 쌍이 그 예다).
 *
 * 여기서 ICR에 값을 쓰는 것은 "대입"이 아니라 "명령"이다. 같은 값을 두 번
 * 쓰는 것도 두 번의 명령이다. 컴파일러가 "같은 주소에 같은 값을 또 쓴다"를
 * 중복 저장으로 보고 하나를 지워 버리면 하드웨어 명령이 사라진다.
 * volatile이 금지하는 것이 바로 그 제거다. */
void reg_clear_error_w1c(volatile uint32_t *icr, uint32_t mask)
{
    reg_write(icr, mask);
}

/* 모델 쪽 반응: ICR에 실린 1비트를 status에서 지우고 ICR을 비운다.
 * 실제 하드웨어는 쓰기 트랜잭션이 끝나는 그 사이클에 이걸 한다. */
void hw_apply_icr(void)
{
    g_dev.status &= ~(g_dev.icr & ST_ERRFLAGS);
    g_dev.icr = 0u;
}

/* ================================================================== */
/* 테스트                                                              */
/* ================================================================== */

static void test_accessor_counts(void)
{
    hw_reset(1000u);

    assert(g_reads == 0u && g_writes == 0u);

    (void)reg_read(&g_dev.status);
    assert(g_reads == 1u && g_writes == 0u);

    reg_write(&g_dev.ctrl, CTRL_ENABLE);
    assert(g_reads == 1u && g_writes == 1u);
    assert(g_dev.ctrl == CTRL_ENABLE);

    /* RMW는 정확히 읽기 1 + 쓰기 1이다. 세 단계가 아니라 두 번의 버스 접근 */
    uint32_t r0 = g_reads;
    uint32_t w0 = g_writes;
    reg_set_bits(&g_dev.ctrl, CTRL_IRQ_EN);
    assert(g_reads == r0 + 1u);
    assert(g_writes == w0 + 1u);
    assert(g_dev.ctrl == (CTRL_ENABLE | CTRL_IRQ_EN));

    reg_clear_bits(&g_dev.ctrl, CTRL_ENABLE);
    assert(g_dev.ctrl == CTRL_IRQ_EN);

    /* clear와 set을 한 번에: clear가 먼저 적용된다 */
    reg_modify(&g_dev.ctrl, CTRL_IRQ_EN, CTRL_LOOPBK);
    assert(g_dev.ctrl == CTRL_LOOPBK);

    puts("ok  1: accessor가 읽기/쓰기 횟수를 정확히 세고 RMW는 read1+write1이다");
}

static void test_poll_reads_every_iteration(void)
{
    /* 5틱 뒤에 READY가 서는 장치. 제대로 폴링하면 본다. */
    hw_reset(5u);

    int rc = reg_wait_bits(&g_dev.status, ST_READY, 100u, hw_tick);
    assert(rc == 0);
    /* 6번째 읽기에서 발견한다: tick 5번이 지나야 비트가 서므로
     * 읽기 → tick 을 5번 반복한 뒤 6번째 읽기에서 1을 본다 */
    assert(g_reads == 6u);
    assert(g_dev.ticks == 5u);
    assert((g_dev.status & ST_READY) != 0u);

    puts("ok  2: 폴링 루프가 매 반복마다 실제로 새 값을 읽어 온다");
}

static void test_snapshot_never_sees_change(void)
{
    /* 같은 장치, 같은 인자. 읽기를 루프 밖으로 끌어낸 버전 */
    hw_reset(5u);

    int rc = reg_wait_bits_snapshot(&g_dev.status, ST_READY, 100u, hw_tick);

    assert(rc == -1);                    /* 영원히 못 본다 → 타임아웃 */
    assert(g_reads == 1u);               /* 읽기는 딱 한 번 */
    assert(g_dev.ticks == 100u);         /* 하드웨어는 100틱 동안 움직였다 */
    assert((g_dev.status & ST_READY) != 0u);  /* 비트는 실제로 서 있다! */

    /* 정리: 레지스터에는 READY가 서 있는데 함수는 못 봤다고 보고했다.
     * 이게 volatile을 빼먹은 폴링 루프에서 벌어지는 일이고, 증상은 항상
     * "타임아웃" 또는 "행(hang)"이다. */
    puts("ok  3: 읽기를 루프 밖으로 빼면 비트가 실제로 섰는데도 타임아웃이 난다");
}

static void test_two_polls_differ(void)
{
    hw_reset(5u);

    /* 제대로 읽는 쪽: 6회 읽고 성공 */
    int good = reg_wait_bits(&g_dev.status, ST_READY, 100u, hw_tick);
    uint32_t good_reads = g_reads;

    /* 스냅샷 쪽: 1회 읽고 실패 */
    hw_reset(5u);
    int bad = reg_wait_bits_snapshot(&g_dev.status, ST_READY, 100u, hw_tick);
    uint32_t bad_reads = g_reads;

    assert(good == 0 && bad == -1);
    assert(good_reads > bad_reads);
    assert(bad_reads == 1u);

    printf("     정상 폴링: 읽기 %u회 → 성공 / 스냅샷: 읽기 %u회 → 타임아웃\n",
           good_reads, bad_reads);
    puts("ok  4: 같은 입력에 대해 읽기 횟수와 결과가 갈린다 (합침의 대가)");
}

static void test_read_twice_is_two_observations(void)
{
    hw_reset(1u);                        /* 첫 틱에 READY가 선다 */

    uint32_t first = 0u;
    uint32_t second = 0u;
    uint32_t n = reg_read_twice(&g_dev.status, &first, &second, hw_tick);

    assert(n == 2u);                     /* 두 읽기는 하나로 합쳐지지 않는다 */
    assert((first & ST_READY) == 0u);     /* 틱 전: 0 */
    assert((second & ST_READY) != 0u);    /* 틱 후: 1 */
    assert(first != second);

    /* tick이 없으면 두 값이 같다. 하지만 읽기는 여전히 두 번이다 */
    hw_reset(1000u);
    n = reg_read_twice(&g_dev.status, &first, &second, NULL);
    assert(n == 2u);
    assert(first == second);

    puts("ok  5: 같은 주소의 두 읽기는 두 개의 독립된 관측이다");
}

static void test_rmw_is_not_atomic(void)
{
    /* volatile은 원자성을 주지 않는다. 읽기와 쓰기 사이에 하드웨어가 끼어들면
     * 그 변경이 사라진다. 여기서는 그 틈을 손으로 재현한다. */
    hw_reset(1u);

    reg_write(&g_dev.ctrl, CTRL_ENABLE);
    assert(g_dev.ctrl == CTRL_ENABLE);

    /* [1] read  — CPU가 ctrl을 읽는다 */
    uint32_t v = reg_read(&g_dev.ctrl);
    assert(v == CTRL_ENABLE);

    /* [2] 여기서 인터럽트/다른 코어/하드웨어가 같은 레지스터를 바꾼다.
     *     실제로는 ISR이 reg_set_bits(&ctrl, CTRL_IRQ_EN)을 부른 상황이다. */
    reg_set_bits(&g_dev.ctrl, CTRL_IRQ_EN);
    assert(g_dev.ctrl == (CTRL_ENABLE | CTRL_IRQ_EN));

    /* [3] modify + write — CPU는 [1]에서 읽은 낡은 값을 기준으로 쓴다 */
    v |= CTRL_LOOPBK;
    reg_write(&g_dev.ctrl, v);

    /* ISR이 세운 CTRL_IRQ_EN이 통째로 사라졌다. 이게 lost update다. */
    assert((g_dev.ctrl & CTRL_IRQ_EN) == 0u);
    assert(g_dev.ctrl == (CTRL_ENABLE | CTRL_LOOPBK));

    /* 고치는 법은 volatile이 아니다. RMW 전체를 critical section으로 감싸거나
     * 하드웨어가 제공하는 set-only / clear-only 레지스터를 쓰는 것이다. */
    puts("ok  6: volatile RMW는 원자적이 아니다 — 끼어든 갱신이 사라진다");
}

static void test_w1c_write_is_a_command(void)
{
    hw_reset(1000u);

    /* 하드웨어가 오버런 에러를 올렸다 */
    g_dev.status |= ST_OVERRUN;
    assert((reg_read(&g_dev.status) & ST_OVERRUN) != 0u);

    uint32_t w0 = g_writes;
    reg_clear_error_w1c(&g_dev.icr, ST_OVERRUN);
    hw_apply_icr();                      /* 하드웨어가 명령에 반응한다 */
    assert(g_writes == w0 + 1u);
    assert((g_dev.status & ST_OVERRUN) == 0u);
    assert(g_dev.icr == 0u);             /* 하드웨어가 스스로 비웠다 */

    /* status는 여전히 다른 비트를 들고 있을 수 있다 — ICR 방식은 읽기-변경-쓰기
     * 없이 원하는 비트만 건드리므로 lost update가 원천적으로 없다. */
    g_dev.status |= (ST_OVERRUN | ST_BUSY);
    reg_clear_error_w1c(&g_dev.icr, ST_OVERRUN);
    hw_apply_icr();
    assert((g_dev.status & ST_OVERRUN) == 0u);
    assert((g_dev.status & ST_BUSY) != 0u);

    /* 같은 값을 두 번 쓰면 쓰기도 두 번이다. 컴파일러가 "중복 저장"으로 보고
     * 하나를 지워 버리면 하드웨어 명령이 한 번 사라지는 것이다. */
    w0 = g_writes;
    reg_clear_error_w1c(&g_dev.icr, ST_OVERRUN);
    reg_clear_error_w1c(&g_dev.icr, ST_OVERRUN);
    assert(g_writes == w0 + 2u);

    puts("ok  7: 레지스터 쓰기는 대입이 아니라 명령이라 같은 값도 두 번 나간다");
}

static void test_timeout_boundaries(void)
{
    /* 경계값: max_polls = 0, 1, 그리고 딱 맞는 횟수 */
    hw_reset(5u);
    assert(reg_wait_bits(&g_dev.status, ST_READY, 0u, hw_tick) == -1);
    assert(g_reads == 0u);               /* 0회면 읽지도 않는다 */
    assert(g_dev.ticks == 0u);

    /* 처음부터 비트가 서 있으면 1회에 성공한다 */
    hw_reset(0u);                        /* ready_at 0 → 첫 tick 전부터... */
    g_dev.status |= ST_READY;            /* ...가 아니라 여기서 직접 세운다 */
    assert(reg_wait_bits(&g_dev.status, ST_READY, 1u, hw_tick) == 0);
    assert(g_reads == 1u);
    assert(g_dev.ticks == 0u);           /* tick을 부르기 전에 끝났다 */

    /* 딱 맞는 횟수: 5틱 필요 → 6폴이면 성공, 5폴이면 실패 */
    hw_reset(5u);
    assert(reg_wait_bits(&g_dev.status, ST_READY, 6u, hw_tick) == 0);
    hw_reset(5u);
    assert(reg_wait_bits(&g_dev.status, ST_READY, 5u, hw_tick) == -1);
    assert(g_reads == 5u);

    /* tick 콜백이 NULL이면 하드웨어가 안 움직여 반드시 타임아웃 */
    hw_reset(5u);
    assert(reg_wait_bits(&g_dev.status, ST_READY, 50u, NULL) == -1);
    assert(g_reads == 50u);              /* 그래도 50번 다 읽었다 */
    assert(g_dev.ticks == 0u);

    puts("ok  8: 타임아웃 경계(0회·1회·딱 맞는 횟수)가 읽기 횟수까지 정확하다");
}

static void test_multi_bit_mask(void)
{
    /* mask가 여러 비트면 "모두 서야" 성공이다 (any가 아니라 all) */
    hw_reset(1000u);
    g_dev.status = ST_READY;             /* 둘 중 하나만 */
    assert(reg_wait_bits(&g_dev.status, ST_READY | ST_BUSY, 3u, NULL) == -1);

    g_dev.status = ST_READY | ST_BUSY;
    assert(reg_wait_bits(&g_dev.status, ST_READY | ST_BUSY, 3u, NULL) == 0);

    /* mask = 0 은 "조건 없음"이라 항상 즉시 성공한다 (0 & 0 == 0) */
    hw_reset(1000u);
    assert(reg_wait_bits(&g_dev.status, 0u, 3u, NULL) == 0);
    assert(g_reads == 1u);

    /* 모든 비트를 요구하면 절대 못 만족한다 */
    hw_reset(1000u);
    assert(reg_wait_bits(&g_dev.status, 0xFFFFFFFFu, 3u, NULL) == -1);

    puts("ok  9: 다중 비트 마스크는 all 조건이고 mask=0은 즉시 성공이다");
}

static void test_const_volatile_reads_changing_value(void)
{
    /* const volatile 포인터로 읽는 값이 호출마다 달라질 수 있다.
     * const는 "이 포인터로 안 쓴다"는 뜻일 뿐, "값이 안 변한다"가 아니다.
     * (이 포인터로 쓰려고 하면 컴파일 에러가 난다 — 노트 §5 참고) */
    const volatile uint32_t *ro = &g_dev.status;

    hw_reset(3u);
    uint32_t seen[6];
    for (size_t i = 0u; i < 6u; i++) {
        seen[i] = reg_read(ro);
        hw_tick();
    }
    assert(g_reads == 6u);
    assert((seen[0] & ST_READY) == 0u);
    assert((seen[2] & ST_READY) == 0u);   /* 3틱 지나기 전 */
    assert((seen[3] & ST_READY) != 0u);   /* 3틱 이후 */
    assert(seen[0] != seen[5]);

    puts("ok 10: const volatile은 '쓰기 금지 + 값은 변함'이라는 조합이다");
}

int main(void)
{
    printf("fake device: status/data/ctrl (volatile uint32_t), "
           "accessor 계측 on\n");

    test_accessor_counts();
    test_poll_reads_every_iteration();
    test_snapshot_never_sees_change();
    test_two_polls_differ();
    test_read_twice_is_two_observations();
    test_rmw_is_not_atomic();
    test_w1c_write_is_a_command();
    test_timeout_boundaries();
    test_multi_bit_mask();
    test_const_volatile_reads_changing_value();

    puts("ALL TESTS PASSED");
    return 0;
}
