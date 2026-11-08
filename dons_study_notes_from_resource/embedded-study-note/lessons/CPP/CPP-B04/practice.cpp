// CPP-B04 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

[[noreturn]] inline void invalid_arg() { std::abort(); }   // 상수 평가 중 도달 → 컴파일 에러

// 연습 1: 링버퍼 크기는 2의 거듭제곱 → 인덱스는 % 대신 & (SIZE-1)
constexpr bool is_pow2(uint32_t x) { return x != 0 && (x & (x - 1u)) == 0; }
constexpr unsigned log2_exact(uint32_t x) {
    if (!is_pow2(x)) invalid_arg();
    unsigned n = 0;
    while ((x >>= 1) != 0) ++n;        // C++14 부터 constexpr 함수 안에 루프·지역 변수 OK
    return n;
}
inline constexpr uint32_t RX_SIZE = 256;
static_assert(is_pow2(RX_SIZE), "RX_SIZE 는 2의 거듭제곱이어야 & 로 래핑 가능");
inline constexpr uint32_t RX_MASK = RX_SIZE - 1u;

// 연습 2: UART BRR (STM32 USART, OVER8=0): BRR = round(fclk / baud), 오차 2% 초과면 거부
constexpr uint32_t uart_brr(uint32_t fclk, uint32_t baud) {
    if (baud == 0) invalid_arg();
    const uint32_t brr = static_cast<uint32_t>((uint64_t{fclk} + baud / 2) / baud);   // 반올림
    if (brr < 16 || brr > 0xFFFF) invalid_arg();                                      // 레지스터 폭·최소값
    const uint64_t actual = fclk / brr;
    const uint64_t diff   = actual > baud ? actual - baud : baud - actual;
    if (diff * 1000 / baud > 20) invalid_arg();                                       // > 2.0 %
    return brr;
}

// 연습 3: CRC-8 (poly 0x07, init 0) 테이블을 컴파일타임에 생성 → 읽기 전용 섹션(.rodata)에 상수로
constexpr std::array<uint8_t, 256> make_crc8_table(uint8_t poly) {
    std::array<uint8_t, 256> t{};
    for (unsigned i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        for (int b = 0; b < 8; ++b)
            c = static_cast<uint8_t>((c & 0x80u) ? (c << 1) ^ poly : (c << 1));
        t[i] = c;
    }
    return t;
}
inline constexpr std::array<uint8_t, 256> CRC8_TABLE = make_crc8_table(0x07);
constexpr uint8_t crc8(const char* s, std::size_t n) {
    uint8_t c = 0;
    for (std::size_t i = 0; i < n; ++i) c = CRC8_TABLE[c ^ static_cast<uint8_t>(s[i])];
    return c;
}
static_assert(CRC8_TABLE[1] == 0x07 && CRC8_TABLE[0x80] == 0x89);
static_assert(crc8("123456789", 9) == 0xF4, "CRC-8/SMBUS check value");

// 연습 4: Cortex-M3/M4 주변장치 bit-band 별칭 주소
//   alias = 0x4200'0000 + (addr - 0x4000'0000) * 32 + bit * 4,  addr ∈ [0x4000'0000, 0x400F'FFFF]
constexpr uintptr_t bitband_periph(uintptr_t addr, unsigned bit) {
    if (addr < 0x4000'0000u || addr > 0x400F'FFFFu || bit > 31) invalid_arg();
    return 0x4200'0000u + (addr - 0x4000'0000u) * 32u + bit * 4u;
}

// 연습 5: SysTick 리로드 값 — consteval: 반드시 컴파일타임. LOAD 는 24비트
consteval uint32_t systick_reload(uint32_t cpu_hz, uint32_t tick_hz) {
    if (tick_hz == 0 || cpu_hz / tick_hz == 0) invalid_arg();
    const uint32_t r = cpu_hz / tick_hz - 1u;
    if (r > 0x00FF'FFFFu) invalid_arg();
    return r;
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(...) do { g_total++; if (__VA_ARGS__) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)

template <uint32_t V> inline constexpr uint32_t CT = V;
template <uint32_t X> concept Log2Ok = requires { typename std::integral_constant<unsigned, log2_exact(X)>; };
template <uint32_t F, uint32_t B> concept BrrOk = requires { typename std::integral_constant<uint32_t, uart_brr(F, B)>; };
template <uintptr_t A, unsigned N> concept BitbandOk = requires { typename std::integral_constant<uintptr_t, bitband_periph(A, N)>; };
template <uint32_t C, uint32_t T> concept SystickOk = requires { typename std::integral_constant<uint32_t, systick_reload(C, T)>; };

int main() {
    // 1
    CHECK(CT<log2_exact(RX_SIZE)> == 8u && RX_MASK == 0xFFu);
    CHECK(((RX_SIZE + 5u) & RX_MASK) == 5u);
    CHECK(Log2Ok<1024> && !Log2Ok<1000> && !Log2Ok<0>);
    // 2
    CHECK(CT<uart_brr(16'000'000, 115'200)> == 0x8Bu);     // RM0090 표: 16 MHz, 115.2 kbps → 0x8B
    CHECK(CT<uart_brr(84'000'000, 9'600)> == 8750u);
    CHECK(BrrOk<16'000'000, 9'600> && !BrrOk<1'000'000, 115'200>);   // 1 MHz 로는 오차 3.5 %
    // 3
    CHECK(crc8("123456789", 9) == 0xF4);
    CHECK(CRC8_TABLE.size() == 256 && CRC8_TABLE[0xFF] == 0xF3);
    // 4
    CHECK(CT<bitband_periph(0x4002'0014u, 5)> == 0x4240'0294u);  // GPIOA_ODR bit 5
    CHECK(CT<bitband_periph(0x4000'0000u, 0)> == 0x4200'0000u);
    CHECK(BitbandOk<0x400F'FFFCu, 31> && !BitbandOk<0x4010'0000u, 0> && !BitbandOk<0x4002'0014u, 32>);
    // 5
    CHECK(CT<systick_reload(168'000'000, 1'000)> == 167'999u);
    CHECK(SystickOk<16'000'000, 1'000> && !SystickOk<168'000'000, 1> && !SystickOk<1'000, 0>);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
