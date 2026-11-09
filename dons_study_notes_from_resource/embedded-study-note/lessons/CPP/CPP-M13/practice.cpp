// CPP-M13 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define CT_CHECK(expr) do { constexpr bool ct_ok_ = (expr); CHECK(ct_ok_); } while (0)

static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// ---------------- 연습 1: 다항식을 템플릿 인자로 (CRC-32C) ----------------
template <std::uint32_t Poly>
constexpr std::array<std::uint32_t, 256> make_reflected_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ Poly : (c >> 1);
        t[i] = c;
    }
    return t;
}

template <std::uint32_t Poly>
inline constexpr auto reflected_table = make_reflected_table<Poly>();

template <std::uint32_t Poly>
constexpr std::uint32_t crc_reflected(std::string_view s) {
    std::uint32_t c = 0xFFFF'FFFFu;
    for (char ch : s) c = reflected_table<Poly>[(c ^ static_cast<std::uint8_t>(ch)) & 0xFFu] ^ (c >> 8);
    return ~c;
}

inline constexpr std::uint32_t POLY_IEEE = 0xEDB8'8320u;
inline constexpr std::uint32_t POLY_CASTAGNOLI = 0x82F6'3B78u;

static_assert(crc_reflected<POLY_IEEE>("123456789") == 0xCBF4'3926u);
static_assert(crc_reflected<POLY_CASTAGNOLI>("123456789") == 0xE306'9283u);

// ---------------- 연습 2: MSB-first CRC-16/CCITT-FALSE (reflect 없음) ----------------
constexpr std::array<std::uint16_t, 256> make_crc16_table() {
    std::array<std::uint16_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint16_t c = static_cast<std::uint16_t>(i << 8);
        for (int k = 0; k < 8; ++k)
            c = static_cast<std::uint16_t>((c & 0x8000u) ? (c << 1) ^ 0x1021u : (c << 1));
        t[i] = c;
    }
    return t;
}
inline constexpr auto crc16_table = make_crc16_table();

constexpr std::uint16_t crc16_ccitt(std::string_view s) {
    std::uint16_t c = 0xFFFFu;
    for (char ch : s)
        c = static_cast<std::uint16_t>((c << 8) ^ crc16_table[((c >> 8) ^ static_cast<std::uint8_t>(ch)) & 0xFFu]);
    return c;
}
static_assert(crc16_table[1] == 0x1021u);
static_assert(crc16_ccitt("123456789") == 0x29B1u);

// ---------------- 연습 3: Newton sqrt 로 Q15 sqrt LUT ----------------
constexpr double sqrt_newton(double x) {
    if (x <= 0.0) return 0.0;
    double g = x >= 1.0 ? x : 1.0;
    for (int i = 0; i < 40; ++i) g = 0.5 * (g + x / g);
    return g;
}
constexpr std::array<std::uint16_t, 257> make_sqrt_q15() {
    std::array<std::uint16_t, 257> t{};
    for (std::size_t i = 0; i <= 256; ++i) {
        const double v = sqrt_newton(static_cast<double>(i) / 256.0) * 32768.0 + 0.5;
        t[i] = static_cast<std::uint16_t>(v >= 32767.0 ? 32767.0 : v);
    }
    return t;
}
inline constexpr auto sqrt_q15 = make_sqrt_q15();
static_assert(sqrt_q15[0] == 0);
static_assert(sqrt_q15[64] == 16384);    // sqrt(0.25) = 0.5
static_assert(sqrt_q15[256] == 32767);   // 포화

// ---------------- 연습 4: 1/4 표에서 전체 주기 표로, 그리고 크기 비교 ----------------
constexpr double sin_taylor(double x) {
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 13; ++n) {
        term *= -x2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}
inline constexpr double PI = 3.14159265358979323846;
constexpr std::int16_t to_q15(double v) {
    const double r = v * 32768.0 + (v >= 0.0 ? 0.5 : -0.5);
    if (r >= 32767.0) return 32767;
    if (r <= -32768.0) return -32768;
    return static_cast<std::int16_t>(r);
}
constexpr std::array<std::int16_t, 257> make_quarter() {
    std::array<std::int16_t, 257> t{};
    for (std::size_t i = 0; i <= 256; ++i)
        t[i] = to_q15(sin_taylor((PI / 2.0) * static_cast<double>(i) / 256.0));
    return t;
}
inline constexpr auto quarter = make_quarter();

// 사분면 전개: 1024칸 전체 표. quarter 로부터 만든다.
constexpr std::array<std::int16_t, 1024> make_full() {
    std::array<std::int16_t, 1024> t{};
    for (std::size_t i = 0; i < 1024; ++i) {
        const std::size_t quad = i >> 8, k = i & 0xFFu;
        const std::int16_t v = (quad & 1u) ? quarter[256 - k] : quarter[k];
        t[i] = static_cast<std::int16_t>((quad & 2u) ? -v : v);
    }
    return t;
}
inline constexpr auto full_sin = make_full();
static_assert(sizeof(quarter) == 514u);
static_assert(sizeof(full_sin) == 2048u);
static_assert(full_sin[0] == 0 && full_sin[256] == 32767 && full_sin[512] == 0 && full_sin[768] == -32767);

// cos = 90도 앞선 sin, 그리드 위에서 비교
constexpr std::int16_t cos_grid(std::size_t i) { return full_sin[(i + 256) & 1023u]; }
static_assert(cos_grid(0) == 32767 && cos_grid(512) == -32767);

// ---------------- 연습 5: slicing-by-4 용 표 4장 (4 KiB) ----------------
using Slice4 = std::array<std::array<std::uint32_t, 256>, 4>;
constexpr Slice4 make_slice4() {
    Slice4 t{};
    t[0] = make_reflected_table<POLY_IEEE>();
    for (std::size_t i = 0; i < 256; ++i)
        for (std::size_t s = 1; s < 4; ++s)
            t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFFu];
    return t;
}
inline constexpr auto slice4 = make_slice4();
static_assert(sizeof(slice4) == 4096u);

static std::uint32_t crc32_slice4(std::span<const std::uint8_t> d) {
    std::uint32_t c = 0xFFFF'FFFFu;
    std::size_t i = 0;
    for (; i + 4 <= d.size(); i += 4) {
        c ^= static_cast<std::uint32_t>(d[i]) | static_cast<std::uint32_t>(d[i + 1]) << 8 |
             static_cast<std::uint32_t>(d[i + 2]) << 16 | static_cast<std::uint32_t>(d[i + 3]) << 24;
        c = slice4[3][c & 0xFFu] ^ slice4[2][(c >> 8) & 0xFFu] ^ slice4[1][(c >> 16) & 0xFFu] ^ slice4[0][c >> 24];
    }
    for (; i < d.size(); ++i) c = slice4[0][(c ^ d[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static std::uint32_t crc32_byte(std::span<const std::uint8_t> d) {
    std::uint32_t c = 0xFFFF'FFFFu;
    for (std::uint8_t b : d) c = slice4[0][(c ^ b) & 0xFFu] ^ (c >> 8);
    return ~c;
}

// ---------------- 연습 6: 보정 가능한 표 = constinit (쓰기 가능, .data) ----------------
constexpr std::array<std::uint16_t, 8> default_gains() {
    std::array<std::uint16_t, 8> g{};
    for (std::size_t i = 0; i < g.size(); ++i) g[i] = static_cast<std::uint16_t>(1000u + 100u * i);
    return g;
}
constinit std::array<std::uint16_t, 8> g_gain_table = default_gains();

void apply_factory_trim(std::size_t ch, int offset) {
    if (ch < g_gain_table.size()) g_gain_table[ch] = static_cast<std::uint16_t>(g_gain_table[ch] + offset);
}

int main() {
    // 연습 1
    CT_CHECK(reflected_table<POLY_IEEE>[1] == 0x7707'3096u);
    CT_CHECK(reflected_table<POLY_CASTAGNOLI>[1] == 0xF26B'8303u);
    CT_CHECK(crc_reflected<POLY_CASTAGNOLI>("") == 0u);
    CT_CHECK(crc_reflected<POLY_CASTAGNOLI>("123456789") == 0xE306'9283u);

    // 연습 2
    CT_CHECK(crc16_ccitt("") == 0xFFFFu);
    CT_CHECK(crc16_ccitt("123456789") == 0x29B1u);
    CT_CHECK(crc16_table[255] == 0x1EF0u);

    // 연습 3: Newton 결과를 호스트 sqrt 와 대조
    {
        double worst = 0.0;
        for (int i = 1; i <= 4096; ++i) {
            const double x = i / 4096.0;
            const double e = std::fabs(sqrt_newton(x) - std::sqrt(x));
            if (e > worst) worst = e;
        }
        CHECK(worst < 1e-14);
        int bad = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            const double exact = std::sqrt(static_cast<double>(i) / 256.0) * 32768.0;
            if (std::fabs(sqrt_q15[i] - exact) > 0.5 + 1e-9) ++bad;
        }
        CHECK(bad == 0);
    }

    // 연습 4
    {
        int bad = 0;
        for (std::size_t i = 0; i < 1024; ++i) {
            const double exact = 32768.0 * std::sin(2.0 * PI * static_cast<double>(i) / 1024.0);
            const double limit = (i == 256 || i == 768) ? 1.0 : 0.5 + 1e-9;   // 포화 칸만 1 LSB
            if (std::fabs(full_sin[i] - exact) > limit) ++bad;
        }
        CHECK(bad == 0);
        CT_CHECK(cos_grid(0) == 32767);
        CT_CHECK(cos_grid(256) == 0);
    }

    // 연습 5: 무작위 버퍼, 길이 경계 (4의 배수 ± 1 포함)
    {
        std::uint8_t buf[1024];
        std::uint32_t seed = 0x5EED'0005u;
        for (auto& b : buf) b = static_cast<std::uint8_t>(xorshift32(seed));
        const std::size_t lens[] = {0, 1, 3, 4, 5, 7, 8, 255, 256, 1023, 1024};
        int bad = 0;
        for (std::size_t n : lens) {
            const std::span<const std::uint8_t> s{buf, n};
            if (crc32_slice4(s) != crc32_byte(s)) ++bad;
        }
        CHECK(bad == 0);
        const std::uint8_t v[] = {'1','2','3','4','5','6','7','8','9'};
        CHECK(crc32_slice4(v) == 0xCBF4'3926u);
        CT_CHECK(slice4[1][0] == 0u);
    }

    // 연습 6
    CHECK(g_gain_table[0] == 1000u && g_gain_table[7] == 1700u);
    apply_factory_trim(3, -5);
    CHECK(g_gain_table[3] == 1295u);
    apply_factory_trim(8, 100);              // 범위 밖: 무시
    CHECK(g_gain_table[7] == 1700u);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
