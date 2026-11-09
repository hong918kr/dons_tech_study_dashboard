// CPP-M14 static_assert·type traits로 HAL 오용 차단
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//
// 실제 칩:   GPIOA->AFR[1] = ... 를 쓰는 configure<PA9, Signal::USART1_TX>() 가 MMIO 를 건드린다.
// 이 레슨:   GPIO/RCC 레지스터를 mock 구조체로 흉내 내고 쓰기 순서를 로그로 남긴다.
// 표:        STM32F4 계열의 AF 표를 본뜬 학습용 일부. 실제 칩에 쓸 때는 데이터시트의 AF 표로 교체한다.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

// ---------------- 작은 Pin (Pin HAL 자체는 CPP-M04 소관) ----------------
enum class Port : std::uint8_t { A, B, C };
inline constexpr unsigned PINS_PER_PORT = 16;

template <Port P, unsigned N>
struct Pin {                                  // 여기 static_assert 를 넣지 않는다 (trait 로 검사 못 하는 하드 에러가 되므로)
    static constexpr Port     port   = P;
    static constexpr unsigned number = N;
};
using PA2  = Pin<Port::A, 2>;
using PA3  = Pin<Port::A, 3>;
using PA5  = Pin<Port::A, 5>;
using PA9  = Pin<Port::A, 9>;
using PA10 = Pin<Port::A, 10>;
using PB6  = Pin<Port::B, 6>;
using PB7  = Pin<Port::B, 7>;
using PB8  = Pin<Port::B, 8>;
using PC6  = Pin<Port::C, 6>;

// AF "번호"가 아니라 "기능 이름"을 받는다. 번호는 표를 찾은 결과다.
enum class Signal : std::uint8_t {
    USART1_TX, USART1_RX, USART2_TX, USART2_RX,
    I2C1_SCL, I2C1_SDA,
    SPI1_SCK, SPI1_MISO, SPI1_MOSI,
    TIM3_CH1, TIM3_CH2,
};
inline constexpr std::size_t SIGNAL_COUNT = static_cast<std::size_t>(Signal::TIM3_CH2) + 1;

enum class Periph : std::uint8_t { USART1, USART2, I2C1, SPI1, TIM3 };

constexpr Periph periph_of(Signal s) {        // default 를 안 쓴다: 열거자를 빠뜨리면 -Wswitch 가 잡는다
    switch (s) {
    case Signal::USART1_TX: case Signal::USART1_RX: return Periph::USART1;
    case Signal::USART2_TX: case Signal::USART2_RX: return Periph::USART2;
    case Signal::I2C1_SCL:  case Signal::I2C1_SDA:  return Periph::I2C1;
    case Signal::SPI1_SCK:  case Signal::SPI1_MISO: case Signal::SPI1_MOSI: return Periph::SPI1;
    case Signal::TIM3_CH1:  case Signal::TIM3_CH2:  return Periph::TIM3;
    }
    return Periph::TIM3;                      // 도달 불가 (잘못된 enum 값 대비)
}
constexpr bool is_i2c(Signal s) { return periph_of(s) == Periph::I2C1; }
constexpr bool is_usart_tx(Signal s) { return s == Signal::USART1_TX || s == Signal::USART2_TX; }
constexpr bool is_usart_rx(Signal s) { return s == Signal::USART1_RX || s == Signal::USART2_RX; }

// ---------------- AF 표: 이 칩에서 허용되는 (핀, 기능) → AF 번호 ----------------
struct AfRow { Port port; std::uint8_t pin; Signal signal; std::uint8_t af; };

inline constexpr AfRow AF_TABLE[] = {
    {Port::A,  2, Signal::USART2_TX, 7},
    {Port::A,  3, Signal::USART2_RX, 7},
    {Port::A,  5, Signal::SPI1_SCK,  5},
    {Port::A,  6, Signal::SPI1_MISO, 5},
    {Port::A,  6, Signal::TIM3_CH1,  2},
    {Port::A,  7, Signal::SPI1_MOSI, 5},
    {Port::A,  7, Signal::TIM3_CH2,  2},
    {Port::A,  9, Signal::USART1_TX, 7},
    {Port::A, 10, Signal::USART1_RX, 7},
    {Port::B,  3, Signal::SPI1_SCK,  5},
    {Port::B,  4, Signal::SPI1_MISO, 5},
    {Port::B,  5, Signal::SPI1_MOSI, 5},
    {Port::B,  6, Signal::USART1_TX, 7},
    {Port::B,  6, Signal::I2C1_SCL,  4},
    {Port::B,  7, Signal::USART1_RX, 7},
    {Port::B,  7, Signal::I2C1_SDA,  4},
    {Port::B,  8, Signal::I2C1_SCL,  4},
    {Port::B,  9, Signal::I2C1_SDA,  4},
    {Port::C,  6, Signal::TIM3_CH1,  2},
    {Port::C,  7, Signal::TIM3_CH2,  2},
};
inline constexpr std::size_t AF_TABLE_SIZE = sizeof(AF_TABLE) / sizeof(AF_TABLE[0]);

// Part A: 표에서 (port, pin, signal) 행을 찾아 AF 번호를 돌려준다. 없으면 -1.
constexpr int find_af(Port port, unsigned pin, Signal signal) {
    for (const AfRow& row : AF_TABLE) {
        if (row.port == port && row.pin == pin && row.signal == signal) return row.af;
    }
    return -1;
}

// Part A: 표 자체의 오타를 잡는다. pin < 16, af < 16, 같은 (port, pin, signal) 행이 두 번 없음.
template <std::size_t N>
constexpr bool table_ok(const AfRow (&t)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (t[i].pin >= PINS_PER_PORT || t[i].af >= 16) return false;
        for (std::size_t j = i + 1; j < N; ++j) {
            if (t[i].port == t[j].port && t[i].pin == t[j].pin && t[i].signal == t[j].signal) return false;
        }
    }
    return true;
}
static_assert(table_ok(AF_TABLE), "AF_TABLE: pin/af out of range or duplicated row");

// Part A: P 가 Pin<...> 의 인스턴스인가? (const 붙은 Pin 도 true)
template <typename T> struct is_pin_impl : std::false_type {};
template <Port P, unsigned N> struct is_pin_impl<Pin<P, N>> : std::true_type {};
template <typename T>
inline constexpr bool is_pin_v = is_pin_impl<std::remove_cv_t<T>>::value;

// ---------------- 출력 타입 태그 (타입으로 고르는 옵션) ----------------
struct PushPull {};
struct OpenDrain {};

template <typename...> inline constexpr bool dependent_false_v = false;

// Part B: PushPull→0, OpenDrain→1, 그 외 타입은 "읽기 쉬운" 컴파일 에러.
template <typename O>
constexpr std::uint32_t otype_bit() {
    if constexpr (std::is_same_v<O, PushPull>) return 0u;
    else if constexpr (std::is_same_v<O, OpenDrain>) return 1u;
    else { static_assert(dependent_false_v<O>, "otype_bit<O>: O must be PushPull or OpenDrain"); return 0u; }
}

// ---------------- Part A/B: 실패 이유를 값으로 ----------------
enum class PinError : std::uint8_t { None, NotAPin, PinOutOfRange, SignalNotOnPin, I2cNeedsOpenDrain };

// 검사 순서: Pin 타입인가 → 핀 번호 범위 → 표에 있는가 → I2C 는 오픈 드레인인가
template <typename P, Signal S, typename O = PushPull>
constexpr PinError pin_error() {
    if constexpr (!is_pin_v<P>) {
        return PinError::NotAPin;
    } else {
        if (P::number >= PINS_PER_PORT) return PinError::PinOutOfRange;
        if (find_af(P::port, P::number, S) < 0) return PinError::SignalNotOnPin;
        if (is_i2c(S) && !std::is_same_v<O, OpenDrain>) return PinError::I2cNeedsOpenDrain;
        return PinError::None;
    }
}

// ---------------- mock 하드웨어 (실제 칩: RCC->AHB1ENR, GPIOx->MODER ...) ----------------
namespace mock {
enum class Reg : std::uint8_t { RCC_AHB1ENR, MODER, OTYPER, AFRL, AFRH, USART1_BRR };
struct Gpio { std::uint32_t MODER, OTYPER, AFRL, AFRH; };
static Gpio          gpio[3];
static std::uint32_t rcc_ahb1enr;
static std::uint32_t usart1_brr;

struct Write { Reg reg; Port port; std::uint32_t value; };
static Write    log[64];
static unsigned log_n;

inline void reset() { *gpio = Gpio{}; gpio[1] = Gpio{}; gpio[2] = Gpio{}; rcc_ahb1enr = 0; usart1_brr = 0; log_n = 0; }
inline void record(Reg r, Port p, std::uint32_t v) { if (log_n < 64) log[log_n++] = Write{r, p, v}; }
}  // namespace mock

inline void rcc_gpio_enable(Port p) {
    mock::rcc_ahb1enr |= 1u << static_cast<unsigned>(p);
    mock::record(mock::Reg::RCC_AHB1ENR, p, mock::rcc_ahb1enr);
}
inline void gpio_modify(Port p, mock::Reg r, std::uint32_t mask, std::uint32_t bits) {
    mock::Gpio& g = mock::gpio[static_cast<unsigned>(p)];
    std::uint32_t& w = r == mock::Reg::MODER ? g.MODER : r == mock::Reg::OTYPER ? g.OTYPER
                     : r == mock::Reg::AFRL  ? g.AFRL  : g.AFRH;
    w = (w & ~mask) | bits;
    mock::record(r, p, w);
}

// 검증이 끝난 값으로 레지스터를 쓰는 공통 코어. 순서: 클럭 → AF 번호 → 출력 타입 → MODER(AF 모드).
// MODER 를 마지막에 쓰는 이유: 핀이 AF 모드가 되는 순간 AF 번호가 이미 맞아 있어야 글리치가 없다.
template <Port Pt, unsigned N, unsigned Af, std::uint32_t Otype>
inline void apply_pin() {
    rcc_gpio_enable(Pt);
    const mock::Reg     afr   = N < 8 ? mock::Reg::AFRL : mock::Reg::AFRH;
    const std::uint32_t shift = (N % 8) * 4;
    gpio_modify(Pt, afr, std::uint32_t{0xF} << shift, std::uint32_t{Af} << shift);
    gpio_modify(Pt, mock::Reg::OTYPER, std::uint32_t{1} << N, Otype << N);
    gpio_modify(Pt, mock::Reg::MODER, std::uint32_t{3} << (N * 2), std::uint32_t{2} << (N * 2));
}

// Part A/B: 오용이면 이유별 메시지 하나로 컴파일 실패, 맞으면 apply_pin 호출.
template <typename P, Signal S, typename O = PushPull>
void configure() {
    constexpr PinError e = pin_error<P, S, O>();
    static_assert(e != PinError::NotAPin, "configure<P, S>: P must be a Pin<Port, N> type (e.g. PA9)");
    static_assert(e != PinError::PinOutOfRange, "configure<P, S>: pin number must be 0..15");
    static_assert(e != PinError::SignalNotOnPin,
                  "configure<P, S>: signal S is not routed to pin P on this chip (see AF_TABLE / datasheet)");
    static_assert(e != PinError::I2cNeedsOpenDrain, "configure<P, S, O>: I2C pins must be configured OpenDrain");
    if constexpr (e == PinError::None) {        // 실패했을 때 아래에서 2차 에러가 쏟아지지 않게 막는다
        constexpr int af = find_af(P::port, P::number, S);
        apply_pin<P::port, P::number, static_cast<unsigned>(af), otype_bit<O>()>();
    }
}

// ---------------- Part C: 보드 전체 핀 배치 표 검증 ----------------
struct PinAssign { Port port; std::uint8_t pin; Signal signal; bool open_drain; };

// 표에 없는 행이거나, I2C 인데 open_drain=false 이면 그 행 번호. 전부 OK 면 -1.
template <std::size_t N>
constexpr int first_bad_row(const PinAssign (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (find_af(b[i].port, b[i].pin, b[i].signal) < 0) return static_cast<int>(i);
        if (is_i2c(b[i].signal) && !b[i].open_drain) return static_cast<int>(i);
    }
    return -1;
}

// 같은 물리 핀을 두 번 쓴 첫 행(뒤쪽 행) 번호. 없으면 -1.
template <std::size_t N>
constexpr int first_duplicate_pin(const PinAssign (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (b[j].port == b[i].port && b[j].pin == b[i].pin) return static_cast<int>(i);
        }
    }
    return -1;
}

// USARTn_TX 가 있는데 같은 USART 의 RX 가 없는(또는 반대) 첫 행 번호. 전부 짝이 맞으면 -1.
template <std::size_t N>
constexpr int first_unpaired_usart(const PinAssign (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        const bool tx = is_usart_tx(b[i].signal), rx = is_usart_rx(b[i].signal);
        if (!tx && !rx) continue;
        bool found = false;
        for (std::size_t j = 0; j < N; ++j) {
            const bool other = tx ? is_usart_rx(b[j].signal) : is_usart_tx(b[j].signal);
            if (other && periph_of(b[j].signal) == periph_of(b[i].signal)) found = true;
        }
        if (!found) return static_cast<int>(i);
    }
    return -1;
}

// 행 하나를 템플릿 인자(NTTP)로 받는다: 런타임 표 탐색 없이 상수로만 apply_pin 호출.
template <PinAssign R>
inline void apply_row() {
    apply_pin<R.port, R.pin, static_cast<unsigned>(find_af(R.port, R.pin, R.signal)), R.open_drain ? 1u : 0u>();
}

template <const auto& B>
void apply_board() {
    static_assert(first_bad_row(B) == -1, "board: row is not in AF_TABLE, or I2C row without open_drain");
    static_assert(first_duplicate_pin(B) == -1, "board: same physical pin assigned twice");
    static_assert(first_unpaired_usart(B) == -1, "board: USART TX without matching RX (or RX without TX)");
    if constexpr (first_bad_row(B) == -1 && first_duplicate_pin(B) == -1 && first_unpaired_usart(B) == -1) {
        []<std::size_t... I>(std::index_sequence<I...>) {
            (apply_row<B[I]>(), ...);
        }(std::make_index_sequence<std::extent_v<std::remove_reference_t<decltype(B)>>>{});
    }
}

// ---------------- Part C: 범위 밖 설정 — USART 분주값 (정책: 16 <= BRR <= 0xFFFF, 오차 2.5% 이하) ----------------
enum class BaudError : std::uint8_t { None, ZeroBaud, DivisorTooSmall, DivisorTooLarge, RateErrorTooHigh };

constexpr std::uint32_t brr_value(std::uint32_t pclk, std::uint32_t baud) { return (pclk + baud / 2) / baud; }

constexpr BaudError baud_error(std::uint32_t pclk, std::uint32_t baud) {
    if (baud == 0) return BaudError::ZeroBaud;
    const std::uint32_t div = brr_value(pclk, baud);
    if (div < 16) return BaudError::DivisorTooSmall;
    if (div > 0xFFFF) return BaudError::DivisorTooLarge;
    const std::uint32_t actual = pclk / div;
    const std::uint32_t diff = actual > baud ? actual - baud : baud - actual;
    if (std::uint64_t{diff} * 1000 > std::uint64_t{baud} * 25) return BaudError::RateErrorTooHigh;
    return BaudError::None;
}

template <std::uint32_t Pclk, std::uint32_t Baud>
constexpr std::uint32_t usart_brr() {
    constexpr BaudError e = baud_error(Pclk, Baud);
    static_assert(e != BaudError::ZeroBaud, "usart_brr: baud rate must not be 0");
    static_assert(e != BaudError::DivisorTooSmall, "usart_brr: baud too high for this PCLK (BRR < 16)");
    static_assert(e != BaudError::DivisorTooLarge, "usart_brr: baud too low for this PCLK (BRR > 0xFFFF)");
    static_assert(e != BaudError::RateErrorTooHigh, "usart_brr: achievable baud is more than 2.5% off");
    return brr_value(Pclk, Baud);
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// 본문 static_assert 는 requires 로 잡히지 않는다 (CPP-B12 참고) — 그래서 pin_error 를 직접 시험한다.
template <typename P, Signal S> constexpr bool can_name_configure = requires { configure<P, S>(); };

static constexpr PinAssign BOARD_OK[] = {
    {Port::A,  9, Signal::USART1_TX, false},
    {Port::A, 10, Signal::USART1_RX, false},
    {Port::B,  6, Signal::I2C1_SCL,  true},
    {Port::B,  7, Signal::I2C1_SDA,  true},
};
static constexpr PinAssign BOARD_BAD_ROW[] = {
    {Port::A,  9, Signal::USART1_TX, false},
    {Port::A,  9, Signal::I2C1_SCL,  true},    // 행 1: PA9 에 I2C1_SCL 없음
};
static constexpr PinAssign BOARD_I2C_PP[] = {
    {Port::B,  8, Signal::I2C1_SCL,  false},   // 행 0: 푸시풀 I2C
};
static constexpr PinAssign BOARD_DUP[] = {
    {Port::B,  6, Signal::USART1_TX, false},
    {Port::B,  7, Signal::USART1_RX, false},
    {Port::B,  6, Signal::I2C1_SCL,  true},    // 행 2: PB6 두 번째 사용
};
static constexpr PinAssign BOARD_UNPAIRED[] = {
    {Port::A,  2, Signal::USART2_TX, false},   // 행 0: RX 없음
};
static constexpr PinAssign BOARD_CROSSED[] = {
    {Port::A,  9, Signal::USART1_TX, false},
    {Port::A,  3, Signal::USART2_RX, false},   // USART1 TX 와 USART2 RX: 둘 다 짝이 없다 → 첫 행 0
};
static constexpr AfRow TABLE_DUP[] = { {Port::A, 1, Signal::TIM3_CH1, 2}, {Port::A, 1, Signal::TIM3_CH1, 3} };
static constexpr AfRow TABLE_BIG_PIN[] = { {Port::A, 16, Signal::TIM3_CH1, 2} };
static constexpr AfRow TABLE_BIG_AF[] = { {Port::A, 1, Signal::TIM3_CH1, 16} };

int main() {
    using mock::Reg;

    // Part A — 표 찾기 (표의 모든 행이 찾아지고, 전수 조사 개수가 표 크기와 같아야 한다)
    CHECK(find_af(Port::A, 9, Signal::USART1_TX) == 7);
    CHECK(find_af(Port::B, 6, Signal::I2C1_SCL) == 4);
    CHECK(find_af(Port::B, 6, Signal::USART1_TX) == 7);       // 같은 핀, 다른 기능
    CHECK(find_af(Port::C, 7, Signal::TIM3_CH2) == 2);
    CHECK(find_af(Port::A, 9, Signal::I2C1_SCL) == -1);
    CHECK(find_af(Port::A, 16, Signal::USART1_TX) == -1);
    bool every_row_found = true;
    for (const AfRow& r : AF_TABLE) every_row_found = every_row_found && find_af(r.port, r.pin, r.signal) == r.af;
    CHECK(every_row_found);
    std::size_t allowed = 0;
    for (unsigned p = 0; p < 3; ++p)
        for (unsigned n = 0; n < PINS_PER_PORT; ++n)
            for (std::size_t s = 0; s < SIGNAL_COUNT; ++s)
                if (find_af(static_cast<Port>(p), n, static_cast<Signal>(s)) >= 0) ++allowed;
    CHECK(allowed == AF_TABLE_SIZE);

    // Part A — 표 자체 검사 (표 오타는 컴파일타임에 잡아야 한다)
    CHECK(table_ok(AF_TABLE));
    CHECK(!table_ok(TABLE_DUP));
    CHECK(!table_ok(TABLE_BIG_PIN));
    CHECK(!table_ok(TABLE_BIG_AF));

    // Part A — trait
    CHECK(is_pin_v<PA9>);
    CHECK(is_pin_v<const PA9>);
    CHECK((is_pin_v<Pin<Port::C, 15>>));
    CHECK(!is_pin_v<int>);
    CHECK(!is_pin_v<Signal>);
    CHECK(!is_pin_v<PA9&>);

    // Part B — 이유 값 (전부 컴파일타임 상수. FAIL 줄을 보려고 CHECK 로 찍는다)
    constexpr PinError ok1  = pin_error<PA9, Signal::USART1_TX>();
    constexpr PinError ok2  = pin_error<PB6, Signal::I2C1_SCL, OpenDrain>();
    constexpr PinError bad1 = pin_error<int, Signal::USART1_TX>();
    constexpr PinError bad2 = pin_error<Pin<Port::A, 16>, Signal::USART1_TX>();
    constexpr PinError bad3 = pin_error<PA9, Signal::I2C1_SCL>();
    constexpr PinError bad4 = pin_error<PB6, Signal::I2C1_SCL>();
    constexpr PinError bad5 = pin_error<PB6, Signal::I2C1_SCL, PushPull>();
    CHECK(ok1 == PinError::None);
    CHECK(ok2 == PinError::None);
    CHECK(bad1 == PinError::NotAPin);
    CHECK(bad2 == PinError::PinOutOfRange);
    CHECK(bad3 == PinError::SignalNotOnPin);
    CHECK(bad4 == PinError::I2cNeedsOpenDrain);
    CHECK(bad5 == PinError::I2cNeedsOpenDrain);
    CHECK((pin_error<PA10, Signal::USART1_RX, OpenDrain>() == PinError::None));   // UART 는 어느 쪽이든 허용
    constexpr std::uint32_t pp = otype_bit<PushPull>(), od = otype_bit<OpenDrain>();
    CHECK(pp == 0u && od == 1u);
    CHECK((can_name_configure<int, Signal::USART1_TX>));      // 주의: 본문 static_assert → requires 는 true 로 보인다

    // Part A — configure 가 레지스터를 맞게, 맞는 순서로 쓴다
    mock::reset();
    configure<PA9, Signal::USART1_TX>();                      // PA9 → AFRH, shift (9-8)*4=4, MODER bits 18..19
    CHECK(mock::rcc_ahb1enr == (1u << 0));
    CHECK(mock::gpio[0].AFRH == (7u << 4));
    CHECK(mock::gpio[0].AFRL == 0u);
    CHECK(mock::gpio[0].MODER == (2u << 18));
    CHECK(mock::gpio[0].OTYPER == 0u);
    CHECK(mock::log_n == 4);
    CHECK(mock::log_n == 4 && mock::log[0].reg == Reg::RCC_AHB1ENR && mock::log[1].reg == Reg::AFRH
          && mock::log[2].reg == Reg::OTYPER && mock::log[3].reg == Reg::MODER);

    mock::reset();
    configure<PA5, Signal::SPI1_SCK>();                       // 8 미만 핀은 AFRL
    CHECK(mock::gpio[0].AFRL == (5u << 20) && mock::gpio[0].AFRH == 0u);
    CHECK(mock::gpio[0].MODER == (2u << 10));

    // Part B — 같은 레지스터의 기존 값 보존 + 출력 타입
    mock::reset();
    mock::gpio[1].MODER = 0xFFFF'FFFFu;
    mock::gpio[1].AFRL  = 0xFFFF'FFFFu;
    mock::gpio[1].OTYPER = 0x0000'0080u;
    configure<PB6, Signal::I2C1_SCL, OpenDrain>();
    CHECK(mock::rcc_ahb1enr == (1u << 1));
    CHECK(mock::gpio[1].AFRL == 0xF4FF'FFFFu);                // 핀 6 칸만 4 로
    CHECK(mock::gpio[1].MODER == 0xFFFF'EFFFu);               // 핀 6 칸만 10b 로
    CHECK(mock::gpio[1].OTYPER == 0x0000'00C0u);              // 핀 6 bit 만 1 로 (핀 7 유지)
    configure<PB7, Signal::USART1_RX>();                      // PB7 은 푸시풀 → OTYPER bit 7 이 내려간다
    CHECK(mock::gpio[1].OTYPER == 0x0000'0040u);

    // Part C — 보드 표 검증 (행 번호까지)
    constexpr int r_ok   = first_bad_row(BOARD_OK);
    constexpr int r_bad  = first_bad_row(BOARD_BAD_ROW);
    constexpr int r_pp   = first_bad_row(BOARD_I2C_PP);
    constexpr int d_ok   = first_duplicate_pin(BOARD_OK);
    constexpr int d_bad  = first_duplicate_pin(BOARD_DUP);
    constexpr int u_ok   = first_unpaired_usart(BOARD_OK);
    constexpr int u_bad  = first_unpaired_usart(BOARD_UNPAIRED);
    constexpr int u_xed  = first_unpaired_usart(BOARD_CROSSED);
    CHECK(r_ok == -1);
    CHECK(r_bad == 1);
    CHECK(r_pp == 0);
    CHECK(d_ok == -1);
    CHECK(d_bad == 2);
    CHECK(u_ok == -1);
    CHECK(u_bad == 0);
    CHECK(u_xed == 0);

    // Part C — 검증된 보드를 실제로 적용
    mock::reset();
    apply_board<BOARD_OK>();
    CHECK(mock::rcc_ahb1enr == 0x3u);
    CHECK(mock::gpio[0].AFRH == ((7u << 4) | (7u << 8)));
    CHECK(mock::gpio[1].AFRL == ((4u << 24) | (4u << 28)));
    CHECK(mock::gpio[0].MODER == ((2u << 18) | (2u << 20)));
    CHECK(mock::gpio[1].OTYPER == ((1u << 6) | (1u << 7)));
    CHECK(mock::log_n == 16);                                 // 행당 쓰기 4번

    // Part C — 분주값 범위 (표 기반)
    struct BaudCase { std::uint32_t pclk, baud; BaudError want; };
    static constexpr BaudCase cases[] = {
        {16'000'000,   115'200, BaudError::None},
        {16'000'000,   921'600, BaudError::None},             // 오차 2.1%: 간신히 통과
        {16'000'000, 1'000'000, BaudError::None},             // BRR=16, 하한 경계
        {16'000'000, 2'000'000, BaudError::DivisorTooSmall},
        {16'000'000,       245, BaudError::None},             // BRR=65306: 상한 안쪽
        {16'000'000,       244, BaudError::DivisorTooLarge},  // BRR=65574: 상한 0xFFFF 바로 위
        {16'000'000,   975'000, BaudError::RateErrorTooHigh},
        {16'000'000,         0, BaudError::ZeroBaud},
    };
    bool all_baud = true;
    for (const BaudCase& c : cases) {
        const bool hit = baud_error(c.pclk, c.baud) == c.want;
        if (!hit) std::printf("  baud case fail: %u/%u\n", static_cast<unsigned>(c.pclk), static_cast<unsigned>(c.baud));
        all_baud = all_baud && hit;
    }
    CHECK(all_baud);
    constexpr std::uint32_t brr = usart_brr<16'000'000, 115'200>();
    CHECK(brr == 139u);
    CHECK((usart_brr<84'000'000, 115'200>() == 729u));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
