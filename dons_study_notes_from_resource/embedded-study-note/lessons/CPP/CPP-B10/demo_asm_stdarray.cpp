/* CPP-B10 demo: std::array 버전 (asm 비교용). extern "C" 로 심볼 이름을 C 와 같게 맞춘다.
 * c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_asm_stdarray.cpp
 */
#include <array>
#include <cstddef>
#include <cstdint>

constexpr std::array<std::uint8_t, 256> make_crc8_table() {
    std::array<std::uint8_t, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        auto c = static_cast<std::uint8_t>(i);
        for (int b = 0; b < 8; ++b)
            c = (c & 0x80u) ? static_cast<std::uint8_t>((c << 1) ^ 0x07u) : static_cast<std::uint8_t>(c << 1);
        t[i] = c;
    }
    return t;
}
constexpr auto crc8_table = make_crc8_table();   /* 컴파일 타임 계산 → __TEXT,__const (ELF: .rodata, 플래시) */

extern "C" std::uint8_t crc8_frame(const std::array<std::uint8_t, 8>& f) {
    std::uint8_t c = 0;
    for (std::uint8_t b : f) c = crc8_table[c ^ b];
    return c;
}

extern "C" std::uint32_t sum64(const std::array<std::uint16_t, 64>& buf) {
    std::uint32_t s = 0;
    for (std::uint16_t v : buf) s += v;
    return s;
}

extern "C" std::uint16_t get3(const std::array<std::uint16_t, 64>& buf) { return std::get<3>(buf); }
