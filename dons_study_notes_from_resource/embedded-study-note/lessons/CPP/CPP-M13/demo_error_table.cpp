// CPP-M13 demo_error_table.cpp — 표 크기 vs 최대 오차 (호스트에서 실행, 실측 표)
// build: c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_error_table.cpp -o demo_error_table && ./demo_error_table
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

constexpr double PI = 3.14159265358979323846;
constexpr double sin_taylor(double x) {
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 13; ++n) { term *= -x2 / ((2 * n) * (2 * n + 1)); sum += term; }
    return sum;
}
constexpr std::int16_t to_q15(double v) {
    const double r = v * 32768.0 + (v >= 0.0 ? 0.5 : -0.5);
    return r >= 32767.0 ? 32767 : static_cast<std::int16_t>(r);
}

// 1/4 주기 구간 2^Bits 개
template <unsigned Bits>
constexpr auto make_quarter() {
    std::array<std::int16_t, (1u << Bits) + 1> t{};
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = to_q15(sin_taylor((PI / 2.0) * static_cast<double>(i) / static_cast<double>(1u << Bits)));
    return t;
}

template <unsigned Bits, bool Interp>
int lookup(const auto& t, std::uint16_t phase) {
    const unsigned frac_bits = 14 - Bits;
    const unsigned quad = phase >> 14;
    const unsigned idx = (phase >> frac_bits) & ((1u << Bits) - 1);
    const int frac = phase & ((1 << frac_bits) - 1);
    const bool mirror = quad & 1u;
    const unsigned top = 1u << Bits;
    const int a = t[mirror ? top - idx : idx];
    const int b = t[mirror ? top - idx - 1 : idx + 1];
    int y = a;
    if (Interp) y = a + (((b - a) * frac + (1 << (frac_bits - 1))) >> frac_bits);
    return (quad & 2u) ? -y : y;
}

template <unsigned Bits>
void report() {
    static constexpr auto t = make_quarter<Bits>();
    double e_near = 0, e_interp = 0;
    for (std::uint32_t p = 0; p < 65536; ++p) {
        const double exact = 32768.0 * std::sin(2.0 * PI * p / 65536.0);
        e_near   = std::fmax(e_near,   std::fabs(lookup<Bits, false>(t, static_cast<std::uint16_t>(p)) - exact));
        e_interp = std::fmax(e_interp, std::fabs(lookup<Bits, true >(t, static_cast<std::uint16_t>(p)) - exact));
    }
    std::printf("%4u  %6zu  %8zu  %10.2f  %10.2f\n", 1u << Bits, t.size(), sizeof t, e_near, e_interp);
}

int main() {
    std::printf("구간  엔트리   flash(B)  no-interp   interp   (최대 오차, Q15 LSB)\n");
    report<4>(); report<5>(); report<6>(); report<7>(); report<8>(); report<9>(); report<10>();
}
