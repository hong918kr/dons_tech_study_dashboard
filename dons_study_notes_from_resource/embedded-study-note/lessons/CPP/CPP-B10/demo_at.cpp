/* CPP-B10 demo: 범위 밖 인덱스를 at() / operator[] 로 읽으면? (인덱스는 런타임 값)
 *  ① 예외 켬:   c++ -std=c++20 -O2 demo_at.cpp -o demo_at && ./demo_at 4 ; echo $?
 *  ② 예외 끔:   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_at.cpp -o demo_at && ./demo_at 4 ; echo $?
 *  ③ []+하드닝: c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DUSE_BRACKET \
 *                 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST demo_at.cpp -o demo_at && ./demo_at 4 ; echo $?
 *  ④ []만:      c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DUSE_BRACKET demo_at.cpp -o demo_at && ./demo_at 4 ; echo $?
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    std::array<std::uint8_t, 4> lut{10, 20, 30, 40};
    const std::size_t i = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0;   /* 런타임 인덱스 */
    std::fprintf(stderr, "reading lut[%zu]\n", i);
#ifdef USE_BRACKET
    const unsigned v = lut[i];
#else
    const unsigned v = lut.at(i);
#endif
    std::fprintf(stderr, "value = %u\n", v);
    return 0;
}
