/* CPP-B03 enum class와 고정 기저 타입으로 레지스터 필드 명명
 *   Part A: enum class : uint32_t + to_u() + encode_cr()  (필드 → 레지스터 값)
 *   Part B: decode_*() + is_valid()                        (레지스터 값 → 필드, 범위 밖 값)
 *   Part C: 플래그 enum 비트 연산자 + mock UART 드라이버
 * build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp
 */
#include <cstdint>
#include <cstdio>
#include <type_traits>

using std::uint32_t;

/* ---------------- mock UART CR 레이아웃 (STM32 USART_CR1/CR2 에서 따와 한 레지스터로 합침) ----
 *  bit  0      UE      UART enable
 *  bits [2:1]  MODE    00=Async 01=Sync 10=IrDA 11=SmartCard
 *  bit  3      (reserved — 항상 보존)
 *  bits [5:4]  PARITY  bit5=PCE(패리티 켬) bit4=PS(0 even, 1 odd) → 00 None, 10 Even, 11 Odd, 01 reserved
 *  bits [13:12] STOP   00=1, 01=0.5, 10=2, 11=1.5 stop bits
 */
inline constexpr uint32_t CR_UE       = 1u << 0;
inline constexpr uint32_t CR_MODE_Pos = 1,  CR_MODE_Msk = 0x3u << CR_MODE_Pos;
inline constexpr uint32_t CR_PAR_Pos  = 4,  CR_PAR_Msk  = 0x3u << CR_PAR_Pos;
inline constexpr uint32_t CR_STOP_Pos = 12, CR_STOP_Msk = 0x3u << CR_STOP_Pos;

enum class UartMode : uint32_t { Async = 0b00, Sync = 0b01, IrDA = 0b10, SmartCard = 0b11 };
enum class Parity   : uint32_t { None = 0b00, Even = 0b10, Odd = 0b11 };          /* 0b01 = reserved */
enum class StopBits : uint32_t { One = 0b00, Half = 0b01, Two = 0b10, OneAndHalf = 0b11 };

struct UartConfig { UartMode mode; Parity parity; StopBits stop; };

/* ---------------- Part A: enum → 정수, 필드 인코딩 ---------------- */
/* std::to_underlying 은 C++23. C++20 에서는 같은 일을 하는 헬퍼를 직접 만든다. */
template <class E>
    requires std::is_enum_v<E>
constexpr std::underlying_type_t<E> to_u(E e) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<U>(e);
}

/* cfg 의 세 필드를 CR 레이아웃에 맞춰 합친 값. 범위 밖 값이 들어와도 다른 필드를 침범하면 안 된다. */
constexpr uint32_t encode_cr(const UartConfig& c) noexcept {
    return ((to_u(c.mode) << CR_MODE_Pos) & CR_MODE_Msk) | ((to_u(c.parity) << CR_PAR_Pos) & CR_PAR_Msk)
         | ((to_u(c.stop) << CR_STOP_Pos) & CR_STOP_Msk);   /* & Msk: 범위 밖 값이 옆 필드로 새지 않게 */
}

/* ---------------- Part B: 레지스터 → enum, 범위 밖(reserved) 값 ---------------- */
constexpr UartMode decode_mode(uint32_t cr) noexcept {
    return static_cast<UartMode>((cr & CR_MODE_Msk) >> CR_MODE_Pos);
    /* 2비트 → 0..3, 고정 기저 타입이라 어떤 값이든 static_cast 가 정의됨 */
}
constexpr Parity decode_parity(uint32_t cr) noexcept {
    return static_cast<Parity>((cr & CR_PAR_Msk) >> CR_PAR_Pos);
    /* 0b01 도 그대로 Parity 값 1 이 된다 (열거자 이름은 없지만 유효한 값) */
}
constexpr StopBits decode_stop(uint32_t cr) noexcept {
    return static_cast<StopBits>((cr & CR_STOP_Msk) >> CR_STOP_Pos);
    /* 4가지 인코딩 모두 열거자가 있음 */
}
/* p 가 이름 붙은 열거자(None/Even/Odd) 중 하나인가? */
constexpr bool is_valid(Parity p) noexcept {
    switch (p) {
    case Parity::None: case Parity::Even: case Parity::Odd: return true;
    }
    return false;   /* switch 밖 fallback: 이름 없는 값(0b01, 0xFF...)은 여기로 온다 */
}

/* ---------------- Part C: 플래그 enum + mock UART 드라이버 ---------------- */
enum class UartIrq : uint32_t {
    None = 0, Rxne = 1u << 0, Txe = 1u << 1, Tc = 1u << 2, ParityErr = 1u << 3, Idle = 1u << 4,
    Wakeup = 1u << 31,   /* C enum 이라면 int 범위를 넘어 ISO C(C23 이전) 위반 */
};

constexpr UartIrq operator|(UartIrq a, UartIrq b) noexcept {
    return static_cast<UartIrq>(to_u(a) | to_u(b));
    /* 결과 0x3 은 열거자가 아니지만 유효한 UartIrq 값 */
}
constexpr UartIrq operator&(UartIrq a, UartIrq b) noexcept {
    return static_cast<UartIrq>(to_u(a) & to_u(b));
    /* uint32_t & uint32_t → 승격 없음 (int 보다 좁지 않음) */
}
constexpr UartIrq operator~(UartIrq a) noexcept {
    return static_cast<UartIrq>(~to_u(a));   /* uint32_t 라 ~ 결과도 32비트 */
}
constexpr UartIrq& operator|=(UartIrq& a, UartIrq b) noexcept { return a = a | b; }
constexpr UartIrq& operator&=(UartIrq& a, UartIrq b) noexcept { return a = a & b; }
constexpr bool any(UartIrq f) noexcept {
    return to_u(f) != 0u;
    /* explicit operator bool 은 enum 에 정의 불가 → 헬퍼 함수 */
}

struct UartRegs {
    volatile uint32_t CR;
    volatile uint32_t IER;   /* interrupt enable — UartIrq 비트 */
    volatile uint32_t SR;
};
static UartRegs fake_uart;   /* 실제 칩: *reinterpret_cast<UartRegs*>(0x4001'1000) */

/* MODE/PARITY/STOP 만 바꾸고 UE·reserved·나머지 비트는 보존 (read-modify-write) */
static void uart_configure(UartRegs& r, const UartConfig& c) {
    const uint32_t keep = r.CR & ~(CR_MODE_Msk | CR_PAR_Msk | CR_STOP_Msk);   /* 읽기 1번 */
    r.CR = keep | encode_cr(c);                                               /* 쓰기 1번 */
}
static void uart_enable_irq(UartRegs& r, UartIrq irq) {
    r.IER = r.IER | to_u(irq);
}
static void uart_disable_irq(UartRegs& r, UartIrq irq) {
    r.IER = r.IER & to_u(~irq);
}
/* irq 중에서 현재 켜져 있는 것만 */
static UartIrq uart_irq_enabled(const UartRegs& r, UartIrq irq) {
    return static_cast<UartIrq>(r.IER) & irq;
    /* 레지스터 원시값 → 플래그 enum 은 명시적 변환 한 번 */
}

/* ---------------- 컴파일 타임 검사: 타입 안전성 ---------------- */
template <class A, class B>
concept BitOrable = requires(A a, B b) { a | b; };

static_assert(sizeof(UartMode) == 4 && alignof(UartMode) == alignof(uint32_t));
static_assert(std::is_same_v<std::underlying_type_t<Parity>, uint32_t>);
static_assert(!std::is_convertible_v<UartMode, uint32_t>);   /* enum → 정수 암시적 변환 없음 */
static_assert(!std::is_convertible_v<uint32_t, UartMode>);   /* 정수 → enum 도 없음        */
static_assert(!std::is_assignable_v<Parity&, StopBits>);     /* cfg.parity = StopBits::Two ✗ */
static_assert(!std::is_assignable_v<Parity&, int>);
static_assert(BitOrable<UartIrq, UartIrq>);                   /* 플래그 enum 만 | 허용       */
static_assert(!BitOrable<UartIrq, UartMode>);
static_assert(!BitOrable<UartMode, UartMode>);
static_assert(!BitOrable<UartIrq, uint32_t>);
static_assert(std::is_same_v<decltype(UartIrq::Rxne | UartIrq::Txe), UartIrq>);

/* 비교: C 스타일(unscoped) enum 은 위 검사를 전부 통과해 버린다 */
enum CParity { C_PARITY_NONE = 0, C_PARITY_EVEN = 2, C_PARITY_ODD = 3 };
enum CStop   { C_STOP_1 = 0, C_STOP_0_5 = 1, C_STOP_2 = 2, C_STOP_1_5 = 3 };
static_assert(std::is_convertible_v<CParity, uint32_t>);
static_assert(BitOrable<CParity, CStop>);   /* C++20 에선 deprecated 경고뿐, 에러 아님 */

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* Part A */
    CHECK(to_u(UartMode::IrDA) == 2u);
    CHECK(to_u(UartIrq::Wakeup) == 0x8000'0000u);
    CHECK(encode_cr({UartMode::Async, Parity::None, StopBits::One}) == 0x0000u);
    CHECK(encode_cr({UartMode::Sync, Parity::Even, StopBits::Two}) == 0x2022u);
    CHECK(encode_cr({UartMode::SmartCard, Parity::Odd, StopBits::OneAndHalf}) == 0x3036u);

    /* Part B */
    CHECK(decode_mode(0x3036u) == UartMode::SmartCard);
    CHECK(decode_parity(0x2022u) == Parity::Even);
    CHECK(decode_stop(0x3036u) == StopBits::OneAndHalf);
    CHECK(decode_mode(0xFFFF'FFF9u) == UartMode::Async);       /* 다른 비트가 다 1이어도 MODE=00 */
    const UartConfig rt{UartMode::IrDA, Parity::Odd, StopBits::Half};
    const uint32_t raw = encode_cr(rt);
    CHECK(decode_mode(raw) == rt.mode && decode_parity(raw) == rt.parity && decode_stop(raw) == rt.stop);
    const Parity bad = decode_parity(0x0010u);                    /* PARITY=01 (reserved) */
    CHECK(to_u(bad) == 1u);                                        /* 고정 기저 타입: 값 1 그대로, UB 아님 */
    CHECK(!is_valid(bad));
    CHECK(is_valid(Parity::None) && is_valid(Parity::Even) && is_valid(Parity::Odd));
    CHECK(to_u(static_cast<UartMode>(0xFFu)) == 0xFFu);            /* 범위 밖 값도 왕복 보존 */
    CHECK(encode_cr({static_cast<UartMode>(7), Parity::None, StopBits::One}) == 0x0006u); /* bit3 침범 금지 */

    /* Part C — 플래그 연산자 */
    CHECK(to_u(UartIrq::Rxne | UartIrq::Txe) == 0x3u);
    CHECK(to_u(~UartIrq::Rxne) == 0xFFFF'FFFEu);
    CHECK(!any(UartIrq::Rxne & UartIrq::Txe));
    CHECK(any((UartIrq::Rxne | UartIrq::Tc) & UartIrq::Tc));
    UartIrq f = UartIrq::Txe;
    f |= UartIrq::Idle;
    f &= ~UartIrq::Txe;
    CHECK(f == UartIrq::Idle);

    /* Part C — mock 드라이버 */
    fake_uart.CR = CR_UE | (1u << 3) | (1u << 20) | CR_MODE_Msk | CR_STOP_Msk;  /* 쓰레기 값 */
    uart_configure(fake_uart, {UartMode::Sync, Parity::Odd, StopBits::Two});
    CHECK(fake_uart.CR == (CR_UE | (1u << 3) | (1u << 20) | 0x2032u));
    fake_uart.IER = 0;
    uart_enable_irq(fake_uart, UartIrq::Rxne | UartIrq::ParityErr);
    CHECK(fake_uart.IER == 0x9u);
    uart_enable_irq(fake_uart, UartIrq::Wakeup);
    uart_disable_irq(fake_uart, UartIrq::Rxne);
    CHECK(fake_uart.IER == 0x8000'0008u);
    CHECK(uart_irq_enabled(fake_uart, UartIrq::Rxne | UartIrq::ParityErr) == UartIrq::ParityErr);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
