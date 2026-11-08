// CPP-B11 demo: (ptr, len) vs span vs 정적 extent span — 생성 코드 비교
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_asm.cpp
#include <cstddef>
#include <cstdint>
#include <span>

extern volatile uint32_t UART_DR;   // 링크 안 함: -S 로 어셈블리만 본다

extern "C" void write_c(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) UART_DR = p[i];
}
void write_span(std::span<const std::byte> d) {
    for (std::byte b : d) UART_DR = std::to_integer<uint32_t>(b);
}
void write_hdr(std::span<const std::byte, 3> h) {
    for (std::byte b : h) UART_DR = std::to_integer<uint32_t>(b);
}
