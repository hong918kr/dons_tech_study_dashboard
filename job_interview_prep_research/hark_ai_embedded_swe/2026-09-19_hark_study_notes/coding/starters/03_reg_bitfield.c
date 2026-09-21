/* 03_reg_bitfield.c — 레지스터 비트필드 매크로와 read-modify-write (연습용 뼈대)
 *
 * 빌드: make run N=03   (또는 cc -std=c11 -Wall -Wextra -O2 이 파일 -o my && ./my)
 *
 * TODO가 붙은 매크로와 함수만 채운다. 레지스터 레이아웃, 하드웨어 시뮬레이터,
 * 테스트는 그대로 두는 것이 원칙이다. 지금 상태로도 컴파일은 되지만
 * 자리표시자 매크로가 0을 돌려주므로 테스트는 첫 assert에서 실패한다.
 *
 * 이 파일의 UART/SPI 레지스터 주소와 비트 배치는 전부 "예시"다(가짜 IP).
 * 메모리 맵 레지스터는 volatile uint32_t 변수로 흉내 낸다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 1. 비트필드 헬퍼 매크로  — TODO                                      */
/* ------------------------------------------------------------------ */
/* 규칙:
 *  - 결과는 항상 32-bit unsigned. 1 << 31 (signed) 금지, UL 금지.
 *  - 상수 인자로 호출하면 컴파일 타임에 접혀야 한다(함수 호출 금지).
 *  - mask != 0 을 가정해도 된다.
 *
 * 아래 자리표시자는 "인자를 쓰지만 항상 0"인 더미다. 통째로 갈아엎으면 된다. */

/* TODO: bit n 하나만 1인 마스크 */
#define BIT(n)                  ((uint32_t)(n) * 0u)

/* TODO: bit hi..lo 가 1인 마스크 (0 <= lo <= hi <= 31) */
#define GENMASK(hi, lo)         (((uint32_t)(hi) + (uint32_t)(lo)) * 0u)

/* TODO: shift에서 시작하는 width비트 마스크 (1 <= width <= 32).
 *       GENMASK(shift + width - 1, shift)와 같아야 한다. */
#define FIELD_MASK(width, shift) (((uint32_t)(width) + (uint32_t)(shift)) * 0u)

/* TODO: mask의 최하위 1비트 위치(= 그 필드의 shift량) */
#define FIELD_SHIFT(mask)       ((uint32_t)(mask) * 0u)

/* TODO: 값을 필드 자리로 올린다. 넘치는 비트는 잘라낸다. */
#define FIELD_PREP(mask, val)   (((uint32_t)(mask) + (uint32_t)(val)) * 0u)

/* TODO: 레지스터 값에서 필드를 꺼낸다. */
#define FIELD_GET(mask, reg)    (((uint32_t)(mask) + (uint32_t)(reg)) * 0u)

/* TODO: val이 mask 폭에 잘림 없이 들어가면 true */
#define FIELD_FITS(mask, val)   ((void)((uint32_t)(mask) + (uint32_t)(val)), true)

/* ------------------------------------------------------------------ */
/* 2. 레지스터 접근 원시 함수  — TODO                                   */
/* ------------------------------------------------------------------ */

static inline uint32_t reg_read(const volatile uint32_t *reg)
{
    /* TODO: bus read 1회 */
    (void)reg;
    return 0u;
}

static inline void reg_write(volatile uint32_t *reg, uint32_t val)
{
    /* TODO: bus write 1회 (read 없이) */
    (void)reg; (void)val;
}

/* TODO: read-modify-write. mask 영역만 val로 교체하고 나머지는 보존한다.
 *       read는 정확히 1회, write도 정확히 1회여야 한다. */
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    (void)reg; (void)mask; (void)val;
}

static inline void reg_set_bits(volatile uint32_t *reg, uint32_t mask)
{
    /* TODO */
    (void)reg; (void)mask;
}

static inline void reg_clear_bits(volatile uint32_t *reg, uint32_t mask)
{
    /* TODO */
    (void)reg; (void)mask;
}

static inline uint32_t reg_get_field(const volatile uint32_t *reg, uint32_t mask)
{
    /* TODO */
    (void)reg; (void)mask;
    return 0u;
}

static inline void reg_set_field(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    /* TODO */
    (void)reg; (void)mask; (void)val;
}

/* TODO: W1C 레지스터 전용. 읽지 말 것. 지울 비트만 1로 써서 write 1회. */
static inline void reg_w1c(volatile uint32_t *reg, uint32_t mask)
{
    (void)reg; (void)mask;
}

/* ------------------------------------------------------------------ */
/* 3. 예시 UART 레지스터 블록 (가짜 레이아웃)                            */
/* ------------------------------------------------------------------ */
/* CTRL (offset 0x00, R/W)
 *  31        24 23   20 19        8  7   6   5 4  3  2  1  0
 * +------------+-------+-----------+---+---+----+--+--+--+--+
 * |  reserved  |RXTRIG |  BAUDDIV  |RTS|ST2|PAR |LB|RX|TX|EN|
 * +------------+-------+-----------+---+---+----+--+--+--+--+
 *
 * STATUS (offset 0x04)
 *  31      12 11   10   9    8   7    4  3    2   1    0
 * +----------+---+----+----+----+------+----+---+----+----+
 * | reserved |BRK|PERR|FERR|OVR |RXLVL |rsv |TXE|RXNE|TXRDY|
 * +----------+---+----+----+----+------+----+---+----+----+
 *  bit 11..8 : write-1-to-clear
 *  bit  7..0 : read-only (쓰기는 무시된다)
 */
#define UART_CTRL_EN            BIT(0)
#define UART_CTRL_TXE           BIT(1)
#define UART_CTRL_RXE           BIT(2)
#define UART_CTRL_LOOPBACK      BIT(3)
#define UART_CTRL_PARITY_MASK   GENMASK(5, 4)      /* 0=none 1=even 2=odd */
#define UART_CTRL_STOP2         BIT(6)
#define UART_CTRL_RTSEN         BIT(7)
#define UART_CTRL_BAUDDIV_MASK  GENMASK(19, 8)     /* 12-bit 분주비 */
#define UART_CTRL_RXTRIG_MASK   GENMASK(23, 20)    /* RX FIFO 트리거 레벨 */
#define UART_CTRL_RSVD_MASK     GENMASK(31, 24)    /* reserved: 읽은 값을 그대로 보존 */

#define UART_PARITY_NONE        0u
#define UART_PARITY_EVEN        1u
#define UART_PARITY_ODD         2u

#define UART_ST_TXRDY           BIT(0)             /* RO */
#define UART_ST_RXNE            BIT(1)             /* RO */
#define UART_ST_TXEMPTY         BIT(2)             /* RO */
#define UART_ST_RXLVL_MASK      GENMASK(7, 4)      /* RO, RX FIFO 적재량 */
#define UART_ST_OVERRUN         BIT(8)             /* W1C */
#define UART_ST_FRAMEERR        BIT(9)             /* W1C */
#define UART_ST_PARITYERR       BIT(10)            /* W1C */
#define UART_ST_BREAK           BIT(11)            /* W1C */
#define UART_ST_W1C_MASK        GENMASK(11, 8)
#define UART_ST_RO_MASK         (UART_ST_TXRDY | UART_ST_RXNE | UART_ST_TXEMPTY | \
                                 UART_ST_RXLVL_MASK)

/* 실제 타깃이라면 이렇게 선언한다 (주소는 가상):
 *   #define UART0_BASE    0x40004000u
 *   #define UART0_CTRL    ((volatile uint32_t *)(UART0_BASE + 0x00u))
 *   #define UART0_STATUS  ((volatile uint32_t *)(UART0_BASE + 0x04u))
 * 여기서는 host에서 돌리기 위해 아래 시뮬레이터를 쓴다. */

/* ------------------------------------------------------------------ */
/* 4. W1C / read-only 동작을 흉내 내는 레지스터 시뮬레이터              */
/* ------------------------------------------------------------------ */
/* CTRL은 평범한 R/W 레지스터라 volatile 변수 하나로 충분하다.
 * STATUS는 버스가 개입하므로(쓴 값이 그대로 남지 않는다) 함수로 모델링한다. */
typedef struct {
    volatile uint32_t CTRL;
    uint32_t status;        /* 하드웨어가 들고 있는 STATUS 실제 값 */
    uint32_t reads;         /* STATUS bus read 횟수 */
    uint32_t writes;        /* STATUS bus write 횟수 */
} uart_sim_t;

/* CPU가 STATUS를 읽었을 때 */
static uint32_t sim_status_read(uart_sim_t *hw)
{
    hw->reads++;
    return hw->status;
}

/* CPU가 STATUS에 val을 썼을 때 (`*UART0_STATUS = val;` 한 줄에 해당) */
static void sim_status_write(uart_sim_t *hw, uint32_t val)
{
    hw->writes++;
    hw->status &= ~(val & UART_ST_W1C_MASK);   /* 1을 쓴 W1C 비트만 클리어 */
    /* RO 비트와 reserved 비트에 쓴 값은 무시된다. */
}

/* 하드웨어가 스스로 플래그를 세우는 상황 (에러 발생, FIFO 적재 등) */
static void sim_hw_raise(uart_sim_t *hw, uint32_t bits)
{
    hw->status |= bits;
}

static void sim_hw_set_rxlvl(uart_sim_t *hw, uint32_t level)
{
    hw->status = (hw->status & ~UART_ST_RXLVL_MASK) | FIELD_PREP(UART_ST_RXLVL_MASK, level);
}

/* ------------------------------------------------------------------ */
/* 5. 드라이버 레벨 함수  — TODO                                        */
/* ------------------------------------------------------------------ */

/* TODO: 1) EN을 내리고 2) PARITY/STOP2/BAUDDIV/RXTRIG만 교체하고
 *       3) EN|TXE|RXE를 올린다. reserved(31:24)와 LOOPBACK은 건드리지 않는다. */
static void uart_config(volatile uint32_t *ctrl, uint32_t bauddiv, uint32_t parity,
                        bool two_stop, uint32_t rxtrig)
{
    (void)ctrl; (void)bauddiv; (void)parity; (void)two_stop; (void)rxtrig;
}

/* TODO: BAUDDIV 필드만 갱신 */
static void uart_set_baud(volatile uint32_t *ctrl, uint32_t bauddiv)
{
    (void)ctrl; (void)bauddiv;
}

/* TODO: BAUDDIV 필드 읽기 */
static uint32_t uart_get_baud(const volatile uint32_t *ctrl)
{
    (void)ctrl;
    return 0u;
}

/* TODO: STATUS에서 RX FIFO 적재량(read-only 필드) 읽기.
 *       STATUS 접근은 sim_status_read()/sim_status_write()로 한다. */
static uint32_t uart_rx_level(uart_sim_t *hw)
{
    (void)hw;
    return 0u;
}

/* TODO: 현재 pending인 W1C 에러 비트들만 돌려준다 */
static uint32_t uart_pending_errors(uart_sim_t *hw)
{
    (void)hw;
    return 0u;
}

/* TODO: 올바른 W1C 클리어. 읽지 말고, mask 중 W1C 비트만 1로 써서 write 1회. */
static void uart_clear_errors(uart_sim_t *hw, uint32_t mask)
{
    (void)hw; (void)mask;
}

/* 이 함수는 "틀린 구현"을 일부러 재현하는 것이다.
 * TODO: STATUS를 읽어서 (읽은 값 | mask)를 그대로 되쓴다.
 *       테스트는 이 구현이 다른 pending flag를 잃는 것을 확인한다. */
static void uart_clear_errors_rmw_BUG(uart_sim_t *hw, uint32_t mask)
{
    (void)hw; (void)mask;
}

/* ------------------------------------------------------------------ */
/* 6. 예시 SPI 레지스터 (S01 드릴과 동일한 레이아웃)                     */
/* ------------------------------------------------------------------ */
#define SPI_CTRL_EN             BIT(0)
#define SPI_CTRL_CPOL           BIT(1)
#define SPI_CTRL_CPHA           BIT(2)
#define SPI_CTRL_CLKDIV_MASK    GENMASK(11, 4)     /* 8-bit 분주비 */
#define SPI_CTRL_WORD_MASK      GENMASK(15, 12)    /* word size - 1 */

/* TODO: disable -> CPOL/CPHA/CLKDIV/WORD 교체 -> enable.
 *       mode bit1 = CPOL, mode bit0 = CPHA. WORD 필드에는 (bits - 1)을 넣는다. */
static void spi_config(volatile uint32_t *ctrl, uint32_t div, uint32_t bits, int mode)
{
    (void)ctrl; (void)div; (void)bits; (void)mode;
}

/* ------------------------------------------------------------------ */
/* 7. 테스트                                                            */
/* ------------------------------------------------------------------ */
#define OK(msg) printf("  ok  %s\n", (msg))

static void test_masks(void)
{
    assert(BIT(0) == 0x00000001u);
    assert(BIT(31) == 0x80000000u);

    assert(GENMASK(0, 0)   == 0x00000001u);
    assert(GENMASK(31, 0)  == 0xFFFFFFFFu);
    assert(GENMASK(7, 4)   == 0x000000F0u);
    assert(GENMASK(19, 8)  == 0x000FFF00u);
    assert(GENMASK(31, 28) == 0xF0000000u);

    /* FIELD_MASK(width, shift) == GENMASK(shift+width-1, shift) */
    assert(FIELD_MASK(1, 0)   == GENMASK(0, 0));
    assert(FIELD_MASK(4, 4)   == GENMASK(7, 4));
    assert(FIELD_MASK(12, 8)  == GENMASK(19, 8));
    assert(FIELD_MASK(32, 0)  == GENMASK(31, 0));
    assert(FIELD_MASK(4, 28)  == GENMASK(31, 28));

    assert(FIELD_SHIFT(GENMASK(19, 8)) == 8u);
    assert(FIELD_SHIFT(BIT(31)) == 31u);
    OK("BIT / GENMASK / FIELD_MASK / FIELD_SHIFT");
}

static void test_prep_get(void)
{
    assert(FIELD_PREP(UART_CTRL_BAUDDIV_MASK, 0x1A3u) == 0x0001A300u);
    assert(FIELD_GET(UART_CTRL_BAUDDIV_MASK, 0xDEAD1A3Fu) == 0xD1Au);

    /* 왕복 */
    for (uint32_t v = 0; v < 16u; v++) {
        assert(FIELD_GET(UART_CTRL_RXTRIG_MASK, FIELD_PREP(UART_CTRL_RXTRIG_MASK, v)) == v);
    }

    /* 넘치는 값은 잘린다: 4-bit 필드에 0x1F -> 0xF */
    assert(FIELD_GET(UART_CTRL_RXTRIG_MASK, FIELD_PREP(UART_CTRL_RXTRIG_MASK, 0x1Fu)) == 0xFu);
    assert(!FIELD_FITS(UART_CTRL_RXTRIG_MASK, 0x1Fu));
    assert(FIELD_FITS(UART_CTRL_RXTRIG_MASK, 0xFu));
    assert(FIELD_PREP(UART_CTRL_RXTRIG_MASK, 0x1Fu) == FIELD_PREP(UART_CTRL_RXTRIG_MASK, 0xFu));
    OK("FIELD_PREP / FIELD_GET 왕복과 truncation");
}

static void test_rmw_preserves_neighbors(void)
{
    volatile uint32_t ctrl = 0xA5000000u        /* reserved 비트: 건드리면 안 된다 */
                           | FIELD_PREP(UART_CTRL_RXTRIG_MASK, 0x3u)
                           | FIELD_PREP(UART_CTRL_BAUDDIV_MASK, 0x111u)
                           | UART_CTRL_RTSEN | UART_CTRL_STOP2
                           | UART_CTRL_EN | UART_CTRL_TXE | UART_CTRL_RXE;

    uart_set_baud(&ctrl, 0x222u);

    assert(uart_get_baud(&ctrl) == 0x222u);
    assert((ctrl & UART_CTRL_RSVD_MASK) == 0xA5000000u);          /* reserved 보존 */
    assert(FIELD_GET(UART_CTRL_RXTRIG_MASK, ctrl) == 0x3u);       /* 위쪽 이웃 보존 */
    assert((ctrl & (UART_CTRL_RTSEN | UART_CTRL_STOP2)) ==
           (UART_CTRL_RTSEN | UART_CTRL_STOP2));                  /* 아래쪽 이웃 보존 */
    assert((ctrl & (UART_CTRL_EN | UART_CTRL_TXE | UART_CTRL_RXE)) ==
           (UART_CTRL_EN | UART_CTRL_TXE | UART_CTRL_RXE));
    OK("reg_update: 다중 비트 필드만 바뀌고 이웃/reserved는 보존");
}

static void test_set_clear_bits(void)
{
    volatile uint32_t ctrl = 0u;

    reg_set_bits(&ctrl, UART_CTRL_EN | UART_CTRL_TXE);
    assert(ctrl == (UART_CTRL_EN | UART_CTRL_TXE));

    reg_clear_bits(&ctrl, UART_CTRL_TXE);
    assert(ctrl == UART_CTRL_EN);

    reg_set_bits(&ctrl, UART_CTRL_EN);          /* 이미 1이어도 멱등 */
    assert(ctrl == UART_CTRL_EN);

    reg_write(&ctrl, 0x1234u);
    assert(reg_read(&ctrl) == 0x1234u);
    OK("reg_set_bits / reg_clear_bits / reg_read / reg_write");
}

static void test_uart_config(void)
{
    volatile uint32_t ctrl = 0x5A000000u;       /* reserved 초기값 */

    uart_config(&ctrl, 0x0D1u, UART_PARITY_ODD, true, 0x8u);

    assert((ctrl & UART_CTRL_RSVD_MASK) == 0x5A000000u);
    assert(uart_get_baud(&ctrl) == 0x0D1u);
    assert(FIELD_GET(UART_CTRL_PARITY_MASK, ctrl) == UART_PARITY_ODD);
    assert(FIELD_GET(UART_CTRL_RXTRIG_MASK, ctrl) == 0x8u);
    assert((ctrl & UART_CTRL_STOP2) != 0u);
    assert((ctrl & UART_CTRL_EN) != 0u);
    assert((ctrl & UART_CTRL_LOOPBACK) == 0u);  /* 건드리지 않은 비트는 0 그대로 */

    /* 재설정: 이전 값이 남지 않고 새 값으로 완전히 교체된다 */
    uart_config(&ctrl, 0x002u, UART_PARITY_NONE, false, 0x1u);
    assert(uart_get_baud(&ctrl) == 0x002u);
    assert(FIELD_GET(UART_CTRL_PARITY_MASK, ctrl) == UART_PARITY_NONE);
    assert((ctrl & UART_CTRL_STOP2) == 0u);
    assert((ctrl & UART_CTRL_RSVD_MASK) == 0x5A000000u);
    OK("uart_config: disable -> 필드 교체 -> enable");
}

static void test_readonly_field(void)
{
    uart_sim_t hw = {0u, 0u, 0u, 0u};

    sim_hw_set_rxlvl(&hw, 0xBu);
    sim_hw_raise(&hw, UART_ST_RXNE | UART_ST_TXRDY);
    assert(uart_rx_level(&hw) == 0xBu);

    /* RO 비트에 1을 써도 아무 일도 없다 */
    sim_status_write(&hw, UART_ST_RXNE | UART_ST_RXLVL_MASK);
    assert((hw.status & UART_ST_RXNE) != 0u);
    assert(uart_rx_level(&hw) == 0xBu);
    OK("read-only 필드: FIELD_GET으로 읽고, 쓰기는 무시된다");
}

static void test_w1c_correct(void)
{
    uart_sim_t hw = {0u, 0u, 0u, 0u};

    sim_hw_raise(&hw, UART_ST_OVERRUN | UART_ST_FRAMEERR | UART_ST_TXRDY);
    assert(uart_pending_errors(&hw) == (UART_ST_OVERRUN | UART_ST_FRAMEERR));

    hw.writes = 0u;
    uart_clear_errors(&hw, UART_ST_OVERRUN);    /* OVERRUN만 지운다 */

    assert(hw.writes == 1u);                                  /* write 정확히 1회 */
    assert(uart_pending_errors(&hw) == UART_ST_FRAMEERR);     /* FRAMEERR 살아있음 */
    assert((hw.status & UART_ST_TXRDY) != 0u);                /* RO 비트 그대로 */

    /* 실제 타깃에서 버스에 나가는 값은 mask 그 자체다 (read 없음) */
    volatile uint32_t raw_status = 0u;        /* 실제라면 MMIO 주소 */
    reg_w1c(&raw_status, UART_ST_OVERRUN);
    assert(raw_status == UART_ST_OVERRUN);
    OK("W1C 올바른 클리어: 지울 비트만 write, 다른 flag 보존");
}

static void test_w1c_rmw_loses_flags(void)
{
    uart_sim_t hw = {0u, 0u, 0u, 0u};

    /* 두 에러가 동시에 pending */
    sim_hw_raise(&hw, UART_ST_OVERRUN | UART_ST_FRAMEERR);

    uart_clear_errors_rmw_BUG(&hw, UART_ST_OVERRUN);          /* |= 로 쓴 순간 */

    /* FRAMEERR는 처리하지도 않았는데 사라졌다 = 인터럽트 유실 */
    assert(uart_pending_errors(&hw) == 0u);
    OK("W1C에 RMW를 쓰면 처리하지 않은 flag까지 클리어된다 (버그 재현)");
}

static void test_w1c_rmw_race(void)
{
    uart_sim_t hw = {0u, 0u, 0u, 0u};

    /* RMW 버전: read와 write 사이에 새 에러가 도착한다 */
    sim_hw_raise(&hw, UART_ST_OVERRUN);
    uint32_t s = sim_status_read(&hw);           /* read: OVERRUN */
    sim_hw_raise(&hw, UART_ST_PARITYERR);        /* <-- 이 순간 새 에러 도착 */
    sim_status_write(&hw, s | UART_ST_OVERRUN);  /* write: 읽은 값 기준 */
    assert((hw.status & UART_ST_PARITYERR) != 0u);
    /* 읽은 값에는 PARITYERR가 없었으므로 이번에는 살아남는다.
     * 하지만 아래처럼 "읽은 값을 통째로 되쓰는" 변형이면 유실된다. */

    uart_sim_t hw2 = {0u, 0u, 0u, 0u};
    sim_hw_raise(&hw2, UART_ST_OVERRUN | UART_ST_BREAK);
    uint32_t s2 = sim_status_read(&hw2);          /* OVERRUN | BREAK */
    sim_hw_raise(&hw2, UART_ST_PARITYERR);
    sim_status_write(&hw2, s2);                   /* 흔한 실수: 읽은 값을 그대로 write */
    assert((hw2.status & UART_ST_OVERRUN) == 0u); /* 의도하지 않았는데 지워짐 */
    assert((hw2.status & UART_ST_BREAK) == 0u);   /* 의도하지 않았는데 지워짐 */
    assert((hw2.status & UART_ST_PARITYERR) != 0u);
    OK("W1C 경쟁: read와 write 사이에 도착한 flag와 되쓰기 위험");
}

static void test_isr_style_service_loop(void)
{
    uart_sim_t hw = {0u, 0u, 0u, 0u};
    uint32_t handled = 0u;

    sim_hw_raise(&hw, UART_ST_OVERRUN | UART_ST_FRAMEERR | UART_ST_PARITYERR);

    /* ISR 패턴: 한 번 읽고, 읽은 비트만 처리하고, 처리한 비트만 W1C로 되쓴다 */
    uint32_t pending = uart_pending_errors(&hw);
    handled = pending;
    uart_clear_errors(&hw, handled);

    assert(handled == (UART_ST_OVERRUN | UART_ST_FRAMEERR | UART_ST_PARITYERR));
    assert(uart_pending_errors(&hw) == 0u);

    /* 클리어 직후 하드웨어가 새 에러를 올리면 그건 다음 ISR에서 본다 */
    sim_hw_raise(&hw, UART_ST_BREAK);
    assert(uart_pending_errors(&hw) == UART_ST_BREAK);
    OK("ISR 패턴: read once -> handle read bits -> W1C write");
}

static void test_spi_config(void)
{
    volatile uint32_t ctrl = 0xFFFF0000u;       /* 상위 비트는 다른 용도라 가정 */

    spi_config(&ctrl, 0x20u, 8u, 3);

    assert(FIELD_GET(SPI_CTRL_CLKDIV_MASK, ctrl) == 0x20u);
    assert(FIELD_GET(SPI_CTRL_WORD_MASK, ctrl) == 7u);       /* word size - 1 */
    assert((ctrl & SPI_CTRL_CPOL) != 0u);
    assert((ctrl & SPI_CTRL_CPHA) != 0u);
    assert((ctrl & SPI_CTRL_EN) != 0u);
    assert((ctrl & 0xFFFF0000u) == 0xFFFF0000u);             /* 상위 16비트 보존 */

    spi_config(&ctrl, 0x04u, 16u, 0);
    assert(FIELD_GET(SPI_CTRL_CLKDIV_MASK, ctrl) == 0x04u);
    assert(FIELD_GET(SPI_CTRL_WORD_MASK, ctrl) == 15u);
    assert((ctrl & (SPI_CTRL_CPOL | SPI_CTRL_CPHA)) == 0u);
    OK("spi_config: S01 드릴과 동일한 레이아웃/동작");
}

int main(void)
{
    /* 아직 아무도 부르지 않는 헬퍼를 "미사용" 경고에서 빼 준다.
     * TODO를 다 채우고 나면 이 세 줄은 지워도 된다. */
    (void)reg_update; (void)reg_get_field; (void)reg_set_field;

    printf("03_reg_bitfield (starter)\n");
    test_masks();
    test_prep_get();
    test_rmw_preserves_neighbors();
    test_set_clear_bits();
    test_uart_config();
    test_readonly_field();
    test_w1c_correct();
    test_w1c_rmw_loses_flags();
    test_w1c_rmw_race();
    test_isr_style_service_loop();
    test_spi_config();
    printf("ALL TESTS PASSED\n");
    return 0;
}
