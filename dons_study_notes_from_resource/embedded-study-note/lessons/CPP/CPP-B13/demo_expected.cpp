// CPP-B13 demo_expected.cpp — C++23 std::expected 는 -fno-exceptions 에서도 쓸 수 있나? (libc++ 확인용)
//   c++ -std=c++23 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti demo_expected.cpp -o /tmp/cpp_b13_exp && /tmp/cpp_b13_exp
//   ... -DSHOW_ABORT=1 : 에러 상태에서 .value()        → 던질 수 없으니 abort
//   ... -DSHOW_ABORT=2 : std::array::at() 범위 밖     → 마찬가지로 abort
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>

enum class SpiErr : std::uint8_t { Timeout = 1, Crc };

std::expected<std::uint16_t, SpiErr> read_whoami(bool ok) {
    if (!ok) return std::unexpected(SpiErr::Timeout);
    return 0x6Au;
}

int main() {
    std::printf("__cpp_lib_expected = %ld, __cpp_exceptions %s\n", (long)__cpp_lib_expected,
#if defined(__cpp_exceptions)
                "defined");
#else
                "undefined");
#endif
    auto a = read_whoami(true);
    auto b = read_whoami(false);
    std::printf("a: has=%d val=0x%X | b: has=%d err=%d | b.value_or=0x%X\n", a.has_value(), *a,
                b.has_value(), static_cast<int>(b.error()), b.value_or(0xFFFF));
    auto c = read_whoami(true).transform([](std::uint16_t v) { return v | 0x100; }); // monadic (C++23)
    std::printf("c = 0x%X, sizeof(expected<u16,SpiErr>) = %zu\n", *c, sizeof(c));
#if defined(SHOW_ABORT) && SHOW_ABORT == 1
    std::printf("b.value() = %u\n", b.value());
#endif
#if defined(SHOW_ABORT) && SHOW_ABORT == 2
    std::array<int, 4> arr{};
    volatile std::size_t i = 7;
    std::printf("arr.at(7) = %d\n", arr.at(i));
#endif
    return 0;
}
