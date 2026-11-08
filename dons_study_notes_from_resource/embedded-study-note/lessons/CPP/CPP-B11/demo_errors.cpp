// CPP-B11 demo: span 변환 규칙이 컴파일 에러로 막는 것들
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp            # OK
//   c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR demo_errors.cpp       # 에러 5개
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

static size_t uart_write(std::span<const std::byte> d) { return d.size(); }
static size_t read_into(std::span<std::byte> d) { return d.size(); }
static size_t take8(std::span<const std::byte, 8> d) { return d.size(); }
[[maybe_unused]] static std::array<std::byte, 4> make_frame() { return {}; }

int main(int argc, char**) {
    std::array<uint8_t, 4> u8{};
    const std::array<std::byte, 4> crx{};
    std::array<std::byte, 4> b4{};
    std::array<std::byte, 8> b8{};
    (void)crx;
#ifdef SHOW_ERROR
    uart_write(u8);                          // E1: uint8_t 배열 → span<const byte> 변환 없음
    read_into(crx);                          // E2: const 버퍼를 쓰기용 span으로
    take8(b4);                               // E3: 정적 extent 4 ≠ 8
    std::byte b{argc};                       // E4: int 변수 → byte 축소 변환
    std::span<std::byte> s = make_frame();   // E5: 임시 객체 → non-const span
    (void)b; (void)s;
#endif
    size_t n = uart_write(std::as_bytes(std::span{u8}))   // 올바른 길: as_bytes
             + read_into(b4) + take8(b8) + static_cast<size_t>(std::byte(argc) == std::byte{1});
    std::printf("ok %zu\n", n);
}
