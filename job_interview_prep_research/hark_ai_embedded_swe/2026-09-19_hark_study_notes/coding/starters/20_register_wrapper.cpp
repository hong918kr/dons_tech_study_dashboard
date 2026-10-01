/* 20_register_wrapper.cpp — 타입 안전 레지스터 필드 접근 (starter)
 *
 *   make run N=20
 *
 * 아래 TODO 여덟 군데를 채우면 된다. 테스트(main 아래)와 레지스터 정의는 건드리지 말 것.
 * 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 목표
 *   - mask / max / encode / decode 를 전부 컴파일 타임 상수로 계산한다.
 *   - 필드 폭을 넘는 값과 엉뚱한 타입을 컴파일 에러로 만든다.
 *   - 필드 하나를 바꿀 때 MMIO 접근은 읽기 1회 + 쓰기 1회뿐이어야 한다.
 *
 * 구현이 끝나면 아래 CHECK_COMPILE_TIME 을 1로 바꿔 컴파일 타임 검증을 켠다.
 *
 * 빌드: c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti
 * 문제: problems/20_register_wrapper.md
 */

#define CHECK_COMPILE_TIME 0

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

/* ------------------------------------------------------------------ */
/* 1. 가짜 MMIO (그대로 둔다. 접근 횟수를 세기 위한 계측이 들어 있다)   */
/* ------------------------------------------------------------------ */

static volatile std::uint32_t s_uart_cr = 0u;

static std::uint32_t g_mmio_reads = 0u;
static std::uint32_t g_mmio_writes = 0u;

static inline std::uint32_t mmio_read(const volatile std::uint32_t *p)
{
    ++g_mmio_reads;
    return *p;
}

static inline void mmio_write(volatile std::uint32_t *p, std::uint32_t v)
{
    ++g_mmio_writes;
    *p = v;
}

/* ------------------------------------------------------------------ */
/* 2. 컴파일 타임 마스크 계산 — TODO 1                                 */
/* ------------------------------------------------------------------ */

/* TODO 1: width 개의 1 비트를 만들어 돌려준다. ones(0)==0, ones(8)==0xFF,
 *         ones(32)==0xFFFFFFFF. width 가 32일 때 `1u << 32` (UB)를 밟지 않도록
 *         분기하거나 시프트 양을 마스킹한다. */
constexpr std::uint32_t ones(std::uint32_t width)
{
    (void)width;
    return 0u;
}

#if CHECK_COMPILE_TIME
static_assert(ones(0u) == 0x00000000u, "");
static_assert(ones(1u) == 0x00000001u, "");
static_assert(ones(8u) == 0x000000FFu, "");
static_assert(ones(31u) == 0x7FFFFFFFu, "");
static_assert(ones(32u) == 0xFFFFFFFFu, "");
#endif

/* enum class 든 부호 없는 정수든 같은 방법으로 원시 비트값을 얻는다 (그대로 둔다). */
template <typename E>
constexpr std::uint32_t to_raw(E v)
{
    if constexpr (std::is_enum<E>::value) {
        return static_cast<std::uint32_t>(static_cast<std::underlying_type_t<E>>(v));
    } else {
        return static_cast<std::uint32_t>(v);
    }
}

/* ------------------------------------------------------------------ */
/* 3. 필드 기술자 — TODO 2~3                                           */
/* ------------------------------------------------------------------ */

template <std::uint32_t Msb, std::uint32_t Lsb, typename E = std::uint32_t>
struct Field {
    /* TODO 2a: 비트 번호가 0..31 범위인지 static_assert 로 막는다. */
    /* TODO 2b: Msb >= Lsb 인지 static_assert 로 막는다. */

    using value_type = E;

    static constexpr std::uint32_t msb = Msb;
    static constexpr std::uint32_t lsb = Lsb;
    static constexpr std::uint32_t width = (Msb - Lsb) + 1u;

    /* TODO 2c: max = 이 필드에 들어갈 수 있는 최대값, mask = 레지스터 안의 자리 */
    static constexpr std::uint32_t max = 0u;
    static constexpr std::uint32_t mask = 0u;

    /* TODO 3a: raw 값이 이 필드에 들어가는지 판정한다. */
    static constexpr bool fits(std::uint32_t raw)
    {
        (void)raw;
        return false;
    }
    /* TODO 3b: raw 값을 필드 자리로 옮긴 비트 패턴 (폭을 넘는 비트는 잘라낸다). */
    static constexpr std::uint32_t encode(std::uint32_t raw)
    {
        (void)raw;
        return 0u;
    }
    /* TODO 3c: 레지스터 값에서 이 필드만 뽑아 0부터 시작하는 값으로 돌려준다. */
    static constexpr std::uint32_t decode(std::uint32_t reg)
    {
        (void)reg;
        return 0u;
    }
};

/* 필드가 겹치지 않는지 확인하는 도구 (그대로 둔다). */
template <typename A, typename B>
constexpr bool disjoint()
{
    return (A::mask & B::mask) == 0u;
}

/* ------------------------------------------------------------------ */
/* 4. 레지스터 래퍼 — TODO 4~8                                         */
/* ------------------------------------------------------------------ */

class Reg32 {
public:
    explicit Reg32(volatile std::uint32_t *addr) noexcept : p_(addr) {}

    std::uint32_t raw() const noexcept { return mmio_read(p_); }
    void write_raw(std::uint32_t v) noexcept { mmio_write(p_, v); }

    /* TODO 4: 필드를 읽어 F::value_type 으로 돌려준다. */
    template <typename F>
    typename F::value_type get() const noexcept
    {
        return static_cast<typename F::value_type>(0u);
    }

    /* TODO 5: 런타임 값 쓰기. to_raw(v) 로 비트값을 얻고, 범위를 assert 로 확인한 뒤
     *         read-modify-write 를 정확히 한 번만 한다(다른 필드 보존). */
    template <typename F>
    void set(typename F::value_type v) noexcept
    {
        (void)v;
    }

    /* TODO 6: 상수 값 쓰기. static_assert(F::fits(V), ...) 로 컴파일 타임에 막고
     *         나머지는 set() 과 같다. */
    template <typename F, std::uint32_t V>
    void set_const() noexcept
    {
    }

    /* TODO 7: 1비트 필드를 bool 로 읽는다.
     *         static_assert 로 F::width == 1 을 강제한다. */
    template <typename F>
    bool test() const noexcept
    {
        return false;
    }

    /* TODO 8: 미리 계산된 mask/value 로 한 번의 RMW 를 한다. */
    void modify_masked(std::uint32_t mask, std::uint32_t value) noexcept
    {
        (void)mask;
        (void)value;
    }

private:
    volatile std::uint32_t *p_;
};

/* ------------------------------------------------------------------ */
/* 5. 가짜 UART 제어 레지스터 정의 (예시. 실제 칩 이름이 아니다)        */
/* ------------------------------------------------------------------ */

enum class Parity : std::uint32_t { None = 0u, Even = 1u, Odd = 2u };
enum class Stop : std::uint32_t { One = 0u, Two = 1u };

/*  31        24 23 22 21 20            8  7   6 5   4 3   2   1 0
 *  +-----------+--+--+--+---------------+---+---+---+---+---+---+
 *  | PRESCALE  |  |  |DMA|    BAUDDIV   |   |WLEN |STP| PARITY|EN|
 *  +-----------+--+--+--+---------------+---+---+---+---+---+---+ */
using F_En = Field<0, 0>;
using F_Parity = Field<2, 1, Parity>;
using F_Stop = Field<3, 3, Stop>;
using F_WordLen = Field<5, 4>;
using F_BaudDiv = Field<20, 8>;
using F_Dma = Field<21, 21>;
using F_Prescale = Field<31, 24>;
using F_Whole = Field<31, 0>; /* 전체 폭 필드 — ones(32) 경로 확인용 */

#if CHECK_COMPILE_TIME
/* 비트 위치를 손으로 적었으므로, 겹치지 않는다는 사실을 컴파일러에 확인시킨다.
 * 데이터시트를 잘못 읽어 필드가 겹치면 여기서 빌드가 멈춘다. */
static_assert(disjoint<F_En, F_Parity>(), "EN과 PARITY가 겹친다");
static_assert(disjoint<F_Parity, F_Stop>(), "PARITY와 STOP이 겹친다");
static_assert(disjoint<F_Stop, F_WordLen>(), "STOP과 WLEN이 겹친다");
static_assert(disjoint<F_WordLen, F_BaudDiv>(), "WLEN과 BAUDDIV가 겹친다");
static_assert(disjoint<F_BaudDiv, F_Dma>(), "BAUDDIV와 DMA가 겹친다");
static_assert(disjoint<F_Dma, F_Prescale>(), "DMA와 PRESCALE이 겹친다");

/* 마스크가 정말 우리가 원하는 값인지 컴파일 타임에 못박는다. */
static_assert(F_En::mask == 0x00000001u, "");
static_assert(F_Parity::mask == 0x00000006u, "");
static_assert(F_Parity::width == 2u && F_Parity::max == 3u, "");
static_assert(F_Stop::mask == 0x00000008u, "");
static_assert(F_WordLen::mask == 0x00000030u, "");
static_assert(F_BaudDiv::mask == 0x001FFF00u, "");
static_assert(F_BaudDiv::width == 13u && F_BaudDiv::max == 8191u, "");
static_assert(F_Dma::mask == 0x00200000u, "");
static_assert(F_Prescale::mask == 0xFF000000u, "");
static_assert(F_Whole::mask == 0xFFFFFFFFu && F_Whole::max == 0xFFFFFFFFu, "");

/* 값 범위 검사도 컴파일 타임에 된다. */
static_assert(F_Parity::fits(3u), "");
static_assert(!F_Parity::fits(4u), "폭 2비트 필드에 4는 들어가지 않는다");
static_assert(!F_BaudDiv::fits(8192u), "폭 13비트 필드에 8192는 들어가지 않는다");
static_assert(F_Whole::fits(0xFFFFFFFFu), "");
#endif /* CHECK_COMPILE_TIME */

/* ------------------------------------------------------------------ */
/* 6. 컴파일 타임에 조립한 초기화 상수                                  */
/* ------------------------------------------------------------------ */

/* 64 MHz / 115200 = 555.6 -> 반올림 556. 분주비까지 컴파일 타임에 계산한다. */
constexpr std::uint32_t kSysClkHz = 64000000u;
constexpr std::uint32_t kBaud = 115200u;
constexpr std::uint32_t kBaudDiv = (kSysClkHz + (kBaud / 2u)) / kBaud;
static_assert(kBaudDiv == 556u, "");
#if CHECK_COMPILE_TIME
static_assert(F_BaudDiv::fits(kBaudDiv), "분주비가 필드 폭을 넘는다 — 클럭 설정을 고쳐라");
#endif

/* 레지스터 하나에 들어갈 값 전체를 상수로 만든다. 런타임 계산 0, 스토어 1회. */
constexpr std::uint32_t kUartInitValue = F_En::encode(1u) |
                                         F_Parity::encode(to_raw(Parity::Even)) |
                                         F_Stop::encode(to_raw(Stop::One)) |
                                         F_WordLen::encode(3u) |
                                         F_BaudDiv::encode(kBaudDiv) |
                                         F_Dma::encode(1u) |
                                         F_Prescale::encode(2u);

#if CHECK_COMPILE_TIME
static_assert(kUartInitValue == 0x02222C33u, "손으로 계산한 값과 일치해야 한다");
#endif

/* 초기화 상수를 여러 개 두면 그대로 flash(.rodata) 테이블이 된다. */
struct UartPreset {
    std::uint32_t cr;
};
constexpr UartPreset kPresets[3] = {
    {F_En::encode(1u) | F_BaudDiv::encode(556u)},                            /* 115200 8N1 */
    {F_En::encode(1u) | F_BaudDiv::encode(33u)},                              /* ~1.9M */
    {0u},                                                                    /* 비활성 */
};

/* ------------------------------------------------------------------ */
/* 7. 드라이버 함수 — 필드를 한 번의 RMW로 바꾼다                       */
/* ------------------------------------------------------------------ */

static void uart_configure(Reg32 cr, std::uint32_t div, Parity par, Stop stop)
{
    const std::uint32_t mask = F_BaudDiv::mask | F_Parity::mask | F_Stop::mask;
    const std::uint32_t value = F_BaudDiv::encode(div) |
                                F_Parity::encode(to_raw(par)) |
                                F_Stop::encode(to_raw(stop));
    cr.modify_masked(mask, value); /* 읽기 1회 + 쓰기 1회 */
}

/* ------------------------------------------------------------------ */
/* 8. 구현을 마친 뒤 해 볼 것                                          */
/* ------------------------------------------------------------------ */

/* 아래 줄들을 하나씩 살려 보고, 어떤 에러 메시지가 나오는지 확인한다.
 * 전부 "컴파일이 실패해야 정상"이다. 실패하지 않는 줄이 있으면 구현이 덜 된 것이다.
 *
 *   using Bad1 = Field<3, 7>;          // 비트 순서를 뒤집어 썼다
 *   using Bad2 = Field<32, 30>;        // 32비트 레지스터에 bit32는 없다
 *   cr.set_const<F_Parity, 4u>();      // 폭 2비트 필드에 4
 *   cr.set<F_Parity>(1u);              // 열거형 필드에 정수
 *   cr.set<F_Stop>(Parity::Even);      // 다른 필드의 열거형
 *   (void)cr.test<F_Parity>();         // 2비트 필드를 bool 로
 */

/* ------------------------------------------------------------------ */
/* 9. 테스트                                                           */
/* ------------------------------------------------------------------ */

static void reset_reg(std::uint32_t v)
{
    s_uart_cr = v;
    g_mmio_reads = 0u;
    g_mmio_writes = 0u;
}

static void test_field_math(void)
{
    /* 전부 컴파일 타임 상수지만, 런타임에서도 같은 값인지 확인한다. */
    assert(F_BaudDiv::mask == 0x001FFF00u);
    assert(F_BaudDiv::encode(556u) == (556u << 8));
    assert(F_BaudDiv::decode(0x00022C00u) == 556u);
    assert(F_Prescale::decode(0xAB000000u) == 0xABu);
    assert(F_Whole::decode(0xDEADBEEFu) == 0xDEADBEEFu);
    /* 값이 폭을 넘으면 encode 는 잘라낸다(assert로 먼저 걸리는 것이 정상 경로) */
    assert(F_Parity::encode(0xFFu) == F_Parity::mask);
    puts("ok  1: mask/width/max/encode/decode 가 컴파일 타임 상수로 정확하다");
}

static void test_single_field_rmw(void)
{
    reset_reg(0xFFFFFFFFu);
    Reg32 cr(&s_uart_cr);

    cr.set<F_Parity>(Parity::None); /* 2비트만 0으로 */
    assert(s_uart_cr == 0xFFFFFFF9u);
    assert(g_mmio_reads == 1u && g_mmio_writes == 1u); /* RMW 한 번 */

    cr.set<F_Parity>(Parity::Odd);
    assert(F_Parity::decode(s_uart_cr) == 2u);
    assert(s_uart_cr == 0xFFFFFFFDu); /* 이웃 비트는 그대로 1 */
    puts("ok  2: set<F>()는 다른 비트를 보존하고 읽기 1회 + 쓰기 1회만 한다");
}

static void test_enum_round_trip(void)
{
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    cr.set<F_Parity>(Parity::Even);
    cr.set<F_Stop>(Stop::Two);

    assert(cr.get<F_Parity>() == Parity::Even);
    assert(cr.get<F_Stop>() == Stop::Two);
    /* 열거형 필드는 get 의 반환 타입도 열거형이다 */
    static_assert(std::is_same<decltype(cr.get<F_Parity>()), Parity>::value, "");
    static_assert(std::is_same<decltype(cr.get<F_BaudDiv>()), std::uint32_t>::value, "");
    assert(s_uart_cr == 0x0000000Au);
    puts("ok  3: 열거형 필드는 열거형으로 읽고 쓴다 — 숫자를 기억할 필요가 없다");
}

static void test_bit_field_helpers(void)
{
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    assert(cr.test<F_En>() == false);
    cr.set<F_En>(1u);
    assert(cr.test<F_En>() == true);
    assert(s_uart_cr == 0x1u);

    cr.set_const<F_Dma, 1u>();
    assert(cr.test<F_Dma>() == true);
    assert(s_uart_cr == 0x00200001u);
    puts("ok  4: 1비트 필드는 test()/set()으로 다루고 set_const 는 값을 컴파일 타임 검사");
}

static void test_boundary_fields(void)
{
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    /* 최상위 비트를 포함하는 필드 — 시프트에서 부호나 오버플로 문제가 없어야 한다 */
    cr.set<F_Prescale>(0xFFu);
    assert(s_uart_cr == 0xFF000000u);
    assert(cr.get<F_Prescale>() == 0xFFu);

    /* 전체 폭 필드 — ones(32) 경로 */
    cr.set<F_Whole>(0xFFFFFFFFu);
    assert(s_uart_cr == 0xFFFFFFFFu);
    assert(cr.get<F_Whole>() == 0xFFFFFFFFu);
    cr.set<F_Whole>(0u);
    assert(s_uart_cr == 0u);

    /* 필드 최대값과 최소값 */
    cr.set<F_BaudDiv>(F_BaudDiv::max);
    assert(s_uart_cr == F_BaudDiv::mask);
    cr.set<F_BaudDiv>(0u);
    assert(s_uart_cr == 0u);
    puts("ok  5: bit31 포함 필드, 전체 폭 필드, 필드 최대·최소값이 모두 정확하다");
}

static void test_one_rmw_for_many_fields(void)
{
    reset_reg(0xFF00007Fu);
    Reg32 cr(&s_uart_cr);

    uart_configure(cr, 556u, Parity::Odd, Stop::Two);

    assert(g_mmio_reads == 1u);  /* 필드가 3개인데 읽기 1회 */
    assert(g_mmio_writes == 1u); /* 쓰기도 1회 */
    assert(F_BaudDiv::decode(s_uart_cr) == 556u);
    assert(cr.get<F_Parity>() == Parity::Odd);
    assert(cr.get<F_Stop>() == Stop::Two);
    assert(F_Prescale::decode(s_uart_cr) == 0xFFu); /* 건드리지 않은 필드 보존 */
    assert((s_uart_cr & F_En::mask) != 0u);
    puts("ok  6: 필드 3개를 바꿔도 MMIO 접근은 읽기 1 + 쓰기 1 (중간 상태 없음)");
}

static void test_compile_time_constant_write(void)
{
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    cr.write_raw(kUartInitValue); /* 계산이 전부 컴파일 타임에 끝났다 */
    assert(g_mmio_reads == 0u);   /* 읽기조차 필요 없다 */
    assert(g_mmio_writes == 1u);

    assert(cr.test<F_En>() == true);
    assert(cr.get<F_Parity>() == Parity::Even);
    assert(cr.get<F_Stop>() == Stop::One);
    assert(cr.get<F_WordLen>() == 3u);
    assert(cr.get<F_BaudDiv>() == 556u);
    assert(cr.test<F_Dma>() == true);
    assert(cr.get<F_Prescale>() == 2u);
    puts("ok  7: 초기화 값 전체를 constexpr 로 조립해 스토어 1회로 설정한다");
}

static void test_sequential_sets_match_constant(void)
{
    /* 필드를 하나씩 쓰는 방식과 상수 한 번 쓰는 방식의 최종 결과가 같아야 한다.
     * 다만 접근 횟수는 7배다 — 그리고 중간 상태가 하드웨어에 노출된다. */
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    cr.set<F_En>(1u);
    cr.set<F_Parity>(Parity::Even);
    cr.set<F_Stop>(Stop::One);
    cr.set<F_WordLen>(3u);
    cr.set<F_BaudDiv>(kBaudDiv);
    cr.set<F_Dma>(1u);
    cr.set<F_Prescale>(2u);

    assert(s_uart_cr == kUartInitValue);
    assert(g_mmio_reads == 7u && g_mmio_writes == 7u);
    puts("ok  8: 순차 set 7회의 결과 == constexpr 상수 1회 (대신 접근이 7배)");
}

static void test_preset_table(void)
{
    reset_reg(0u);
    Reg32 cr(&s_uart_cr);

    cr.write_raw(kPresets[0].cr);
    assert(cr.get<F_BaudDiv>() == 556u && cr.test<F_En>());

    cr.write_raw(kPresets[2].cr);
    assert(cr.test<F_En>() == false);
    assert(s_uart_cr == 0u);
    puts("ok  9: 프리셋 테이블은 .rodata 상수 — 런타임 초기화 코드가 없다");
}

int main(void)
{
    printf("Reg32: sizeof=%zu, UART CR 초기값 상수=0x%08X (컴파일 타임 계산)\n",
           sizeof(Reg32), static_cast<unsigned>(kUartInitValue));

    test_field_math();
    test_single_field_rmw();
    test_enum_round_trip();
    test_bit_field_helpers();
    test_boundary_fields();
    test_one_rmw_for_many_fields();
    test_compile_time_constant_write();
    test_sequential_sets_match_constant();
    test_preset_table();

    puts("ALL TESTS PASSED");
    return 0;
}
