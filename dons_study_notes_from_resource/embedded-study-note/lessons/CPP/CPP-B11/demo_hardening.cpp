// CPP-B11 demo: subspan 범위 초과는 UB — libc++ hardening 모드만 잡아준다
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_hardening.cpp -o h0 && ./h0 9
//   c++ ... -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST  demo_hardening.cpp -o h1 && ./h1 9
//   c++ ... -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG demo_hardening.cpp -o h2 && ./h2 9
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>

int main(int argc, char** argv) {
    std::array<std::byte, 8> rx{};
    const std::span<const std::byte> buf = rx;
    const size_t len = argc > 1 ? std::strtoul(argv[1], nullptr, 0) : 4;   // "LEN 필드" — 외부 입력
    const std::span<const std::byte> payload = buf.subspan(3, len);        // 검사 없이 자르기 (버그)
    std::printf("buf.size()=%zu  LEN=%zu  payload.size()=%zu\n", buf.size(), len, payload.size());
    return 0;
}
