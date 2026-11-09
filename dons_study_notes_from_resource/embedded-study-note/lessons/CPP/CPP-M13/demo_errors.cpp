// CPP-M13 demo_errors.cpp — 상수 평가에서 터지는 다섯 가지
//   빌드 성공 확인: c++ -std=c++20 -fsyntax-only demo_errors.cpp
//   에러 보기:      c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_errors.cpp
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

using CrcTable = std::array<std::uint32_t, 256>;

// (1) 범위 밖 float → int 변환은 UB [conv.fpint] → 상수 평가면 컴파일 에러
constexpr std::int16_t bad_q15(double v) { return static_cast<std::int16_t>(v * 32768.0); }
constexpr std::int16_t ok_value = bad_q15(0.5);
#ifdef SHOW_ERROR
constexpr std::int16_t e1 = bad_q15(1.0);
#endif

// (2) reinterpret_cast 는 상수식에 올 수 없다 [expr.const]
#ifdef SHOW_ERROR
constexpr std::uint32_t first_byte() {
    const char s[] = "abc";
    const auto* p = reinterpret_cast<const std::uint8_t*>(s);
    return p[0];
}
constexpr std::uint32_t e2 = first_byte();
#endif

// (3) constinit 은 상수 초기화가 아니면 에러 [dcl.constinit]
inline CrcTable make_rt() { return CrcTable{}; }   // constexpr 아님
#ifdef SHOW_ERROR
constinit CrcTable e3 = make_rt();
#endif

// (4) std::sin 은 C++20 에서 constexpr 이 아니다 (GCC 는 내장 함수로 접어 통과시킨다 — 비표준)
#ifdef SHOW_ERROR
constexpr double e4 = std::sin(1.0);
#endif

// (5) 평가 단계 수 제한. 표가 너무 크면 clang 기본 한도에 걸린다.
constexpr double sin_taylor(double x) {
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 13; ++n) { term *= -x2 / ((2 * n) * (2 * n + 1)); sum += term; }
    return sum;
}
template <std::size_t N>
constexpr std::array<std::int16_t, N> make_big() {
    std::array<std::int16_t, N> t{};
    for (std::size_t i = 0; i < N; ++i)
        t[i] = static_cast<std::int16_t>(sin_taylor(1.5707963 * static_cast<double>(i) / static_cast<double>(N)) * 32000.0);
    return t;
}
constexpr auto fine = make_big<1024>();
#ifdef SHOW_ERROR
constexpr auto e5 = make_big<65536>();
#endif

int main() { return ok_value == 16384 && fine[0] == 0 ? 0 : 1; }
