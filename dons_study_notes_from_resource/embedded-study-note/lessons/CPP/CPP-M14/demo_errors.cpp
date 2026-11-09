// CPP-M14 demo: HAL 오용 에러 메시지 실물 모음. solution.cpp 의 핵심만 줄여 옮겼다.
// 정상 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기: c++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -DSHOW_ERROR=N -fsyntax-only demo_errors.cpp   (N=1..7, 10..17)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

enum class Port : std::uint8_t { A, B, C };
template <Port P, unsigned N> struct Pin { static constexpr Port port = P; static constexpr unsigned number = N; };
using PA9 = Pin<Port::A, 9>;
using PB6 = Pin<Port::B, 6>;

enum class Signal : std::uint8_t { USART1_TX, USART1_RX, I2C1_SCL };
constexpr bool is_i2c(Signal s) { return s == Signal::I2C1_SCL; }

struct AfRow { Port port; std::uint8_t pin; Signal signal; std::uint8_t af; };
inline constexpr AfRow AF_TABLE[] = {
    {Port::A,  9, Signal::USART1_TX, 7},
    {Port::A, 10, Signal::USART1_RX, 7},
    {Port::B,  6, Signal::USART1_TX, 7},
    {Port::B,  6, Signal::I2C1_SCL,  4},
};
constexpr int find_af(Port port, unsigned pin, Signal signal) {
    for (const AfRow& r : AF_TABLE) if (r.port == port && r.pin == pin && r.signal == signal) return r.af;
    return -1;
}
constexpr int find_row(Port port, unsigned pin, Signal signal) {
    int i = 0;
    for (const AfRow& r : AF_TABLE) { if (r.port == port && r.pin == pin && r.signal == signal) return i; ++i; }
    return -1;
}

template <typename T> struct is_pin_impl : std::false_type {};
template <Port P, unsigned N> struct is_pin_impl<Pin<P, N>> : std::true_type {};
template <typename T> inline constexpr bool is_pin_v = is_pin_impl<std::remove_cv_t<T>>::value;

struct PushPull {}; struct OpenDrain {};
template <typename...> inline constexpr bool dependent_false_v = false;
template <typename O> constexpr std::uint32_t otype_bit() {
    if constexpr (std::is_same_v<O, PushPull>) return 0u;
    else if constexpr (std::is_same_v<O, OpenDrain>) return 1u;
    else { static_assert(dependent_false_v<O>, "otype_bit<O>: O must be PushPull or OpenDrain"); return 0u; }
}

enum class PinError : std::uint8_t { None, NotAPin, PinOutOfRange, SignalNotOnPin, I2cNeedsOpenDrain };
template <typename P, Signal S, typename O = PushPull> constexpr PinError pin_error() {
    if constexpr (!is_pin_v<P>) { return PinError::NotAPin; }
    else {
        if (P::number >= 16) return PinError::PinOutOfRange;
        if (find_af(P::port, P::number, S) < 0) return PinError::SignalNotOnPin;
        if (is_i2c(S) && !std::is_same_v<O, OpenDrain>) return PinError::I2cNeedsOpenDrain;
        return PinError::None;
    }
}

static volatile std::uint32_t sink;
template <typename P, Signal S, typename O = PushPull>
void configure() {
    constexpr PinError e = pin_error<P, S, O>();
    static_assert(e != PinError::NotAPin, "configure<P, S>: P must be a Pin<Port, N> type (e.g. PA9)");
    static_assert(e != PinError::PinOutOfRange, "configure<P, S>: pin number must be 0..15");
    static_assert(e != PinError::SignalNotOnPin,
                  "configure<P, S>: signal S is not routed to pin P on this chip (see AF_TABLE / datasheet)");
    static_assert(e != PinError::I2cNeedsOpenDrain, "configure<P, S, O>: I2C pins must be configured OpenDrain");
    if constexpr (e == PinError::None) {
        constexpr int af = find_af(P::port, P::number, S);
        sink = static_cast<std::uint32_t>(af) | (otype_bit<O>() << 8);
    }
}

// Part C 축약: 보드 표
struct PinAssign { Port port; std::uint8_t pin; Signal signal; bool open_drain; };
template <std::size_t N> constexpr int first_bad_row(const PinAssign (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (find_af(b[i].port, b[i].pin, b[i].signal) < 0) return static_cast<int>(i);
        if (is_i2c(b[i].signal) && !b[i].open_drain) return static_cast<int>(i);
    }
    return -1;
}
template <PinAssign R> void check_row() {
    static_assert(find_af(R.port, R.pin, R.signal) >= 0, "board row: signal is not routed to this pin");
}
template <const auto& B> void apply_board() {
    static_assert(first_bad_row(B) == -1, "board: row is not in AF_TABLE, or I2C row without open_drain");
}
[[maybe_unused]] static constexpr PinAssign BOARD_BAD[] = {
    {Port::A, 9, Signal::USART1_TX, false},
    {Port::A, 9, Signal::I2C1_SCL,  true},
};

template <std::uint32_t Pclk, std::uint32_t Baud> constexpr std::uint32_t usart_brr() {
    constexpr std::uint32_t div = (Pclk + Baud / 2) / Baud;
    static_assert(div >= 16, "usart_brr: baud too high for this PCLK (BRR < 16)");
    return div;
}

// ---------- 기법 비교 ----------
// T1: 메시지 없이 비교식만 — clang 이 양쪽 값을 펼쳐 준다
template <typename P, Signal S> void t1_bare_compare() { static_assert(find_af(P::port, P::number, S) >= 0); }

// T2: 일부러 정의 안 한 "증인" 템플릿 — 타입 이름에 핀·기능이 그대로 찍힌다
template <Port P, unsigned N, Signal S> struct PIN_HAS_NO_SUCH_AF;
template <typename P, Signal S> void t2_witness() {
    if constexpr (find_af(P::port, P::number, S) < 0) { [[maybe_unused]] PIN_HAS_NO_SUCH_AF<P::port, P::number, S> oops; }
}

// T3: consteval 경로에서 constexpr 아닌 함수를 호출 — 호출 스택 노트에 인자 값이 찍힌다
void AF_TABLE_LOOKUP_FAILED(Port, unsigned, Signal);       // 정의 없음, constexpr 아님
consteval int checked_af(Port p, unsigned n, Signal s) {
    const int af = find_af(p, n, s);
    if (af < 0) AF_TABLE_LOOKUP_FAILED(p, n, s);
    return af;
}
template <typename P, Signal S> void t3_consteval() { sink = static_cast<std::uint32_t>(checked_af(P::port, P::number, S)); }

// 나쁜 예: if constexpr 가드 없음 → static_assert 뒤에 2차 에러가 줄줄이
template <typename P, Signal S> void configure_noguard() {
    static_assert(is_pin_v<P>, "configure: P must be a Pin<Port, N> type");
    constexpr int row = find_row(P::port, P::number, S);
    static_assert(row >= 0, "configure: signal is not routed to this pin");
    sink = AF_TABLE[row].af;
}

// 메시지를 함수로 만들어 넘기기 (C++26 전용. C++20 표준에서는 문자열 리터럴만)
struct Msg { constexpr const char* data() const { return "no such AF"; } constexpr std::size_t size() const { return 10; } };
constexpr Msg make_msg() { return {}; }

// 함수 인자를 static_assert 에 쓰기 — constexpr 함수라도 인자는 상수식이 아니다
constexpr unsigned af_shift(unsigned pin) {
#if defined(SHOW_ERROR) && SHOW_ERROR == 15
    static_assert(pin < 16, "pin must be 0..15");
#endif
    return (pin % 8) * 4;
}

// 클래스 안 static_assert → requires 로 확인하려 해도 하드 에러
template <unsigned N> struct StrictPin { static_assert(N < 16, "StrictPin<N>: N must be 0..15"); static constexpr unsigned number = N; };
template <typename T> constexpr bool has_number = requires { T::number; };

int main() {
    configure<PA9, Signal::USART1_TX>();
    configure<PB6, Signal::I2C1_SCL, OpenDrain>();
    std::printf("ok: sink=%u shift=%u\n", static_cast<unsigned>(sink), af_shift(9));
#if SHOW_ERROR == 1
    configure<PA9, Signal::I2C1_SCL>();
#elif SHOW_ERROR == 2
    configure<Pin<Port::A, 16>, Signal::USART1_TX>();
#elif SHOW_ERROR == 3
    configure<int, Signal::USART1_TX>();
#elif SHOW_ERROR == 4
    configure<PB6, Signal::I2C1_SCL>();
#elif SHOW_ERROR == 5
    configure<PA9, Signal::USART1_TX, int>();
#elif SHOW_ERROR == 6
    apply_board<BOARD_BAD>();
    check_row<BOARD_BAD[1]>();
#elif SHOW_ERROR == 7
    (void)usart_brr<16'000'000, 2'000'000>();
#elif SHOW_ERROR == 10
    t1_bare_compare<PA9, Signal::I2C1_SCL>();
#elif SHOW_ERROR == 11
    t2_witness<PA9, Signal::I2C1_SCL>();
#elif SHOW_ERROR == 12
    t3_consteval<PA9, Signal::I2C1_SCL>();
#elif SHOW_ERROR == 13
    configure_noguard<int, Signal::USART1_TX>();
#elif SHOW_ERROR == 14
    static_assert(find_af(Port::A, 9, Signal::I2C1_SCL) >= 0, make_msg());
#elif SHOW_ERROR == 16
    static_assert(false, "plain false outside a template");
#elif SHOW_ERROR == 17
    (void)has_number<StrictPin<16>>;
#endif
}
