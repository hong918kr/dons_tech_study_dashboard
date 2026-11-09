// CPP-M14 추가 연습 정답 모음
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

enum class Port : std::uint8_t { A, B, C };
template <Port P, unsigned N> struct Pin { static constexpr Port port = P; static constexpr unsigned number = N; };
using PA9 = Pin<Port::A, 9>;
using PB6 = Pin<Port::B, 6>;
using PB8 = Pin<Port::B, 8>;

enum class Signal : std::uint8_t { USART1_TX, USART1_RX, I2C1_SCL, I2C1_SDA, TIM3_CH1 };
struct AfRow { Port port; std::uint8_t pin; Signal signal; std::uint8_t af; };
inline constexpr AfRow AF_TABLE[] = {
    {Port::A,  9, Signal::USART1_TX, 7},
    {Port::A, 10, Signal::USART1_RX, 7},
    {Port::B,  6, Signal::USART1_TX, 7},
    {Port::B,  6, Signal::I2C1_SCL,  4},
    {Port::B,  7, Signal::USART1_RX, 7},
    {Port::B,  7, Signal::I2C1_SDA,  4},
    {Port::B,  8, Signal::I2C1_SCL,  4},
    {Port::C,  6, Signal::TIM3_CH1,  2},
};

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// ---- 연습 1: 이 기능을 낼 수 있는 핀이 몇 개인가 (에러 메시지에 대안을 알려 줄 재료) ----
constexpr std::size_t pin_count(Signal s) {
    std::size_t n = 0;
    for (const AfRow& r : AF_TABLE) if (r.signal == s) ++n;
    return n;
}
static_assert(pin_count(Signal::USART1_TX) == 2);
static_assert(pin_count(Signal::I2C1_SCL) == 2);
static_assert(pin_count(Signal::TIM3_CH1) == 1);

// ---- 연습 2: AF "번호" 를 직접 받는 저수준 API 검증 ----
template <typename P, unsigned Af>
constexpr bool af_number_valid() {
    if (Af >= 16) return false;
    for (const AfRow& r : AF_TABLE) if (r.port == P::port && r.pin == P::number && r.af == Af) return true;
    return false;
}
static_assert(af_number_valid<PB6, 4>() && af_number_valid<PB6, 7>());
static_assert(!af_number_valid<PB6, 5>() && !af_number_valid<PB6, 16>());
static_assert(!af_number_valid<PA9, 4>());   // PA9 에는 AF4 행이 없다

// ---- 연습 3: 보드 표에서 같은 기능을 두 핀에 배정한 첫 행 찾기 ----
struct PinAssign { Port port; std::uint8_t pin; Signal signal; };
template <std::size_t N>
constexpr int first_duplicate_signal(const PinAssign (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (b[j].signal == b[i].signal) return static_cast<int>(i);
    return -1;
}
inline constexpr PinAssign B1[] = {{Port::A, 9, Signal::USART1_TX}, {Port::A, 10, Signal::USART1_RX}};
inline constexpr PinAssign B2[] = {{Port::A, 9, Signal::USART1_TX}, {Port::B, 7, Signal::I2C1_SDA},
                                   {Port::B, 6, Signal::USART1_TX}};
static_assert(first_duplicate_signal(B1) == -1);
static_assert(first_duplicate_signal(B2) == 2);

// ---- 연습 4: 타이머 PWM 분주 범위 (PSC+1)*(ARR+1) = clk/freq, 두 값 모두 16비트 ----
struct PwmCfg { std::uint32_t psc, arr; };
enum class PwmError : std::uint8_t { None, ZeroFreq, FreqTooHigh };
constexpr PwmError pwm_error(std::uint32_t clk, std::uint32_t freq) {
    if (freq == 0) return PwmError::ZeroFreq;
    if (freq > clk) return PwmError::FreqTooHigh;
    return PwmError::None;                // 32비트 clk 이면 PSC 16비트로 항상 도달 가능 → "너무 낮은 주파수" 경우는 없다
}
constexpr PwmCfg pwm_cfg(std::uint32_t clk, std::uint32_t freq) {
    const std::uint32_t ticks = clk / freq;
    const std::uint32_t psc = ticks > 65536 ? (ticks - 1) / 65536 : 0;   // ARR 이 16비트에 들어가는 가장 작은 PSC
    return PwmCfg{psc, ticks / (psc + 1) - 1};
}
template <std::uint32_t Clk, std::uint32_t Freq>
constexpr PwmCfg pwm() {
    constexpr PwmError e = pwm_error(Clk, Freq);
    static_assert(e != PwmError::ZeroFreq, "pwm: frequency must not be 0");
    static_assert(e != PwmError::FreqTooHigh, "pwm: frequency above timer clock");
    return pwm_cfg(Clk, Freq);
}

// ---- 연습 5: 같은 패턴을 DMA 요청표에 (Stream, Channel, 요청) ----
enum class DmaReq : std::uint8_t { USART1_TX, USART1_RX, SPI1_RX };
struct DmaRow { std::uint8_t stream; std::uint8_t channel; DmaReq req; };
inline constexpr DmaRow DMA2_TABLE[] = { {7, 4, DmaReq::USART1_TX}, {2, 4, DmaReq::USART1_RX}, {5, 4, DmaReq::USART1_RX}, {0, 3, DmaReq::SPI1_RX} };
constexpr int find_channel(unsigned stream, DmaReq req) {
    for (const DmaRow& r : DMA2_TABLE) if (r.stream == stream && r.req == req) return r.channel;
    return -1;
}
template <unsigned Stream, DmaReq Req>
constexpr unsigned dma_channel() {
    constexpr int ch = find_channel(Stream, Req);
    static_assert(Stream < 8, "dma_channel: stream must be 0..7");
    static_assert(ch >= 0, "dma_channel: this request is not available on this stream (see DMA request table)");
    return static_cast<unsigned>(ch);
}
static_assert(dma_channel<7, DmaReq::USART1_TX>() == 4);
static_assert(dma_channel<5, DmaReq::USART1_RX>() == 4);

// ---- 연습 6: 표 자체가 "모든 기능을 적어도 한 번 포함" 하는지 (열거자를 빠뜨린 표 찾기) ----
inline constexpr std::size_t SIGNAL_COUNT = static_cast<std::size_t>(Signal::TIM3_CH1) + 1;
template <std::size_t N>
constexpr int first_unmapped_signal(const AfRow (&t)[N]) {
    for (std::size_t s = 0; s < SIGNAL_COUNT; ++s) {
        bool seen = false;
        for (std::size_t i = 0; i < N; ++i) if (static_cast<std::size_t>(t[i].signal) == s) seen = true;
        if (!seen) return static_cast<int>(s);
    }
    return -1;
}
static_assert(first_unmapped_signal(AF_TABLE) == -1);
inline constexpr AfRow PARTIAL[] = {{Port::A, 9, Signal::USART1_TX, 7}};

int main() {
    // 1
    CHECK(pin_count(Signal::USART1_RX) == 2);
    CHECK(pin_count(static_cast<Signal>(200)) == 0);
    // 2
    CHECK((af_number_valid<PB8, 4>()));
    CHECK(!(af_number_valid<PB8, 7>()));
    // 3
    CHECK(first_duplicate_signal(B1) == -1);
    CHECK(first_duplicate_signal(B2) == 2);
    // 4: 표 기반
    struct PwmCase { std::uint32_t clk, freq; PwmError want; std::uint32_t psc, arr; };
    static constexpr PwmCase cases[] = {
        {84'000'000,    20'000, PwmError::None, 0, 4199},   // ticks 4200: PSC 불필요
        {84'000'000,      1000, PwmError::None, 1, 41999},  // ticks 84000 > 65536 → PSC 1
        {84'000'000,         1, PwmError::None, 1281, 65521},
        {84'000'000,         0, PwmError::ZeroFreq, 0, 0},
        {84'000'000, 90'000'000, PwmError::FreqTooHigh, 0, 0},
    };
    bool all_ok = true;
    for (const PwmCase& c : cases) {
        if (pwm_error(c.clk, c.freq) != c.want) { all_ok = false; std::printf("  pwm err %u\n", static_cast<unsigned>(c.freq)); continue; }
        if (c.want != PwmError::None) continue;
        const PwmCfg got = pwm_cfg(c.clk, c.freq);
        if (got.psc != c.psc || got.arr != c.arr) { all_ok = false; std::printf("  pwm cfg %u: psc=%u arr=%u\n", static_cast<unsigned>(c.freq), static_cast<unsigned>(got.psc), static_cast<unsigned>(got.arr)); }
        if (got.psc > 0xFFFF || got.arr > 0xFFFF) all_ok = false;
    }
    CHECK(all_ok);
    constexpr PwmCfg pc = pwm<84'000'000, 20'000>();
    CHECK(pc.psc == 0 && pc.arr == 4199);
    // 5
    CHECK(find_channel(7, DmaReq::USART1_TX) == 4);
    CHECK(find_channel(7, DmaReq::SPI1_RX) == -1);
    CHECK(find_channel(2, DmaReq::USART1_RX) == 4);
    // 6
    CHECK(first_unmapped_signal(AF_TABLE) == -1);
    CHECK(first_unmapped_signal(PARTIAL) == 1);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
