/* 03_reg_bitfield.c — 레지스터 비트필드 매크로와 read-modify-write (모범답안)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 03_reg_bitfield.c -o sol && ./sol
 *
 * 이 파일에 나오는 UART/SPI 레지스터 주소와 비트 배치는 전부 "예시"다.
 * 실제 벤더 IP가 아니라 설명을 위해 만든 가짜 레이아웃이다.
 * 하드웨어 헤더를 쓸 수 없으므로 메모리 맵 레지스터는 volatile uint32_t
 * 변수로 흉내 내고, write-1-to-clear(W1C) 동작만 작은 시뮬레이터로 모델링했다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 1. 비트필드 헬퍼 매크로                                              */
/* ------------------------------------------------------------------ */
/* 모든 상수를 32-bit unsigned로 고정한다.
 * - 1 << 31 은 signed int overflow(UB)라서 항상 1u를 쓴다.
 * - UL은 ARM에서 32-bit, x86-64 host에서 64-bit라 host 테스트에서 어긋난다. */

#define BIT(n)                  ((uint32_t)1u << (n))

/* GENMASK(hi, lo): bit hi..lo가 1인 마스크. 0 <= lo <= hi <= 31 */
#define GENMASK(hi, lo)         ((0xFFFFFFFFu >> (31u - (hi))) & (0xFFFFFFFFu << (lo)))

/* FIELD_MASK(width, shift): shift에서 시작하는 width비트 마스크. 1 <= width <= 32
 * GENMASK(shift + width - 1, shift)와 같다. */
#define FIELD_MASK(width, shift) ((0xFFFFFFFFu >> (32u - (width))) << (shift))

/* mask의 최하위 1비트 위치 = 그 필드의 shift량. mask != 0 이어야 한다. */
#define FIELD_SHIFT(mask)       ((uint32_t)__builtin_ctz(mask))

/* 값을 필드 자리로 올린다. 넘치는 비트는 잘린다(& mask). */
#define FIELD_PREP(mask, val)   (((uint32_t)(val) << FIELD_SHIFT(mask)) & (mask))

/* 레지스터 값에서 필드를 꺼낸다. */
#define FIELD_GET(mask, reg)    (((uint32_t)(reg) & (mask)) >> FIELD_SHIFT(mask))

/* val이 mask 폭에 잘림 없이 들어가는가 (assert/파라미터 검증용) */
#define FIELD_FITS(mask, val)   (((uint32_t)(val) << FIELD_SHIFT(mask) & ~(uint32_t)(mask)) == 0u)

/* ------------------------------------------------------------------ */
/* 2. 레지스터 접근 원시 함수                                           */
/* ------------------------------------------------------------------ */

static inline uint32_t reg_read(const volatile uint32_t *reg)
{
    return *reg;                         /* volatile: 매번 진짜 bus read */
}

static inline void reg_write(volatile uint32_t *reg, uint32_t val)
{
    *reg = val;                          /* 1회 write. read 없음 */
}

/* read-modify-write: mask 영역만 val로 교체하고 나머지 비트는 보존한다.
 * read 1회, write 1회. 중간에 선점되면 안 되는 구간이다. */
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    uint32_t r = *reg;                   /* 1회 read */
    r = (r & ~mask) | (val & mask);
    *reg = r;                            /* 1회 write */
}

static inline void reg_set_bits(volatile uint32_t *reg, uint32_t mask)
{
    reg_update(reg, mask, mask);
}

static inline void reg_clear_bits(volatile uint32_t *reg, uint32_t mask)
{
    reg_update(reg, mask, 0u);
}

/* 필드 단위 read/write */
static inline uint32_t reg_get_field(const volatile uint32_t *reg, uint32_t mask)
{
    return FIELD_GET(mask, *reg);
}

static inline void reg_set_field(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    reg_update(reg, mask, FIELD_PREP(mask, val));
}

/* W1C 레지스터 전용: 읽지 않는다. 지울 비트만 1로 써서 딱 1회 write. */
static inline void reg_w1c(volatile uint32_t *reg, uint32_t mask)
{
    *reg = mask;
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
/* 5. 드라이버 레벨 함수                                                */
/* ------------------------------------------------------------------ */

/* 설정 변경 전 EN을 내리는 것이 이 IP의 규칙이다(예시 IP의 가정). */
static void uart_config(volatile uint32_t *ctrl, uint32_t bauddiv, uint32_t parity,
                        bool two_stop, uint32_t rxtrig)
{
    uint32_t mask = UART_CTRL_PARITY_MASK | UART_CTRL_STOP2 |
                    UART_CTRL_BAUDDIV_MASK | UART_CTRL_RXTRIG_MASK;
    uint32_t val  = FIELD_PREP(UART_CTRL_PARITY_MASK, parity) |
                    (two_stop ? UART_CTRL_STOP2 : 0u) |
                    FIELD_PREP(UART_CTRL_BAUDDIV_MASK, bauddiv) |
                    FIELD_PREP(UART_CTRL_RXTRIG_MASK, rxtrig);

    assert(FIELD_FITS(UART_CTRL_BAUDDIV_MASK, bauddiv));
    assert(FIELD_FITS(UART_CTRL_RXTRIG_MASK, rxtrig));

    reg_clear_bits(ctrl, UART_CTRL_EN);      /* 1) disable */
    reg_update(ctrl, mask, val);             /* 2) 필드만 교체, 이웃/reserved 보존 */
    reg_set_bits(ctrl, UART_CTRL_EN | UART_CTRL_TXE | UART_CTRL_RXE);  /* 3) enable */
}

static void uart_set_baud(volatile uint32_t *ctrl, uint32_t bauddiv)
{
    assert(FIELD_FITS(UART_CTRL_BAUDDIV_MASK, bauddiv));
    reg_set_field(ctrl, UART_CTRL_BAUDDIV_MASK, bauddiv);
}

static uint32_t uart_get_baud(const volatile uint32_t *ctrl)
{
    return reg_get_field(ctrl, UART_CTRL_BAUDDIV_MASK);
}

/* STATUS에서 RX FIFO 적재량 읽기 (read-only 필드) */
static uint32_t uart_rx_level(uart_sim_t *hw)
{
    return FIELD_GET(UART_ST_RXLVL_MASK, sim_status_read(hw));
}

static uint32_t uart_pending_errors(uart_sim_t *hw)
{
    return sim_status_read(hw) & UART_ST_W1C_MASK;
}

/* 올바른 W1C 클리어: 읽지 않고, 지울 비트만 1로 써서 1회 write */
static void uart_clear_errors(uart_sim_t *hw, uint32_t mask)
{
    sim_status_write(hw, mask & UART_ST_W1C_MASK);
}

/* 틀린 구현(교육용): W1C 레지스터를 read-modify-write 한다.
 * 읽은 값에 이미 1인 다른 pending 플래그가 그대로 실려 다시 써지고,
 * 그 순간 그 플래그들도 전부 클리어된다 = 인터럽트 유실. */
static void uart_clear_errors_rmw_BUG(uart_sim_t *hw, uint32_t mask)
{
    uint32_t s = sim_status_read(hw);
    sim_status_write(hw, s | mask);
}

/* ------------------------------------------------------------------ */
/* 6. 예시 SPI 레지스터 (S01 드릴과 동일한 레이아웃)                     */
/* ------------------------------------------------------------------ */
#define SPI_CTRL_EN             BIT(0)
#define SPI_CTRL_CPOL           BIT(1)
#define SPI_CTRL_CPHA           BIT(2)
#define SPI_CTRL_CLKDIV_MASK    GENMASK(11, 4)     /* 8-bit 분주비 */
#define SPI_CTRL_WORD_MASK      GENMASK(15, 12)    /* word size - 1 */

static void spi_config(volatile uint32_t *ctrl, uint32_t div, uint32_t bits, int mode)
{
    uint32_t mask = SPI_CTRL_CPOL | SPI_CTRL_CPHA | SPI_CTRL_CLKDIV_MASK | SPI_CTRL_WORD_MASK;
    uint32_t val  = ((mode & 2) ? SPI_CTRL_CPOL : 0u) |
                    ((mode & 1) ? SPI_CTRL_CPHA : 0u) |
                    FIELD_PREP(SPI_CTRL_CLKDIV_MASK, div) |
                    FIELD_PREP(SPI_CTRL_WORD_MASK, bits - 1u);

    reg_update(ctrl, SPI_CTRL_EN, 0u);             /* 설정 변경 전 disable */
    reg_update(ctrl, mask, val);
    reg_update(ctrl, SPI_CTRL_EN, SPI_CTRL_EN);
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
    printf("03_reg_bitfield (solution)\n");
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
