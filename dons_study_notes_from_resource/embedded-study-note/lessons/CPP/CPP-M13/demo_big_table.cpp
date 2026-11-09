#include <array>
#include <cstddef>
#include <cstdint>
constexpr double sin_taylor(double x) { const double x2 = x * x; double term = x, sum = x; for (int n = 1; n <= 13; ++n) { term *= -x2 / ((2 * n) * (2 * n + 1)); sum += term; } return sum; }
template <std::size_t N> constexpr std::array<std::int16_t, N> make_big() { std::array<std::int16_t, N> t{}; for (std::size_t i = 0; i < N; ++i) t[i] = static_cast<std::int16_t>(sin_taylor(1.5707963 * static_cast<double>(i) / static_cast<double>(N)) * 32000.0); return t; }
constexpr auto e5 = make_big<SZ>();
int main() { return e5[1] == 12345; }
