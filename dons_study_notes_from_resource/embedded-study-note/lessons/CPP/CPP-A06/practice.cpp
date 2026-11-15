/* CPP-A06 추가 연습 — 한 번에 검증
 *   연습 1: 정확한 해가 없을 때 — 허용 오차 안의 "가장 가까운" PLL (pll_search_tol)
 *   연습 2: 반올림 div 가 "보레이트" 오차를 최소화하지 않는 경우 (rate_optimal_div)
 *   연습 3: 해를 모두 세고, 컴파일 타임 배열로 꺼내기 (count_pll / all_pll<...>)
 *   연습 4: APB 타이머 클럭 ×2 규칙과 1 MHz 틱 프리스케일러 (timer_clock / Tick1MHz)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 * 한계값은 solution.cpp 와 같은 RM0090 대표값. 이 파일은 단독으로 빌드된다 (필요한 부분만 다시 적음).
 */
#include <array>
#include <cstdint>
#include <cstdio>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

namespace f4 {
inline constexpr std::uint32_t MHz = 1'000'000;
inline constexpr std::uint32_t VCO_IN_MIN = 1 * MHz, VCO_IN_MAX = 2 * MHz, VCO_MIN = 100 * MHz, VCO_MAX = 432 * MHz;
inline constexpr std::uint32_t M_MIN = 2, M_MAX = 63, N_MIN = 50, N_MAX = 432, SYSCLK_MAX = 168 * MHz, USB_HZ = 48 * MHz;
}  // namespace f4
using f4::MHz;

// 공통: (M, N) 이 VCO 입력/출력 범위 안인가 — 나눗셈 없이
constexpr bool vco_ok(std::uint64_t hse, std::uint32_t m, std::uint32_t n) {
    using namespace f4;
    return hse >= std::uint64_t{VCO_IN_MIN} * m && hse <= std::uint64_t{VCO_IN_MAX} * m &&
           hse * n >= std::uint64_t{VCO_MIN} * m && hse * n <= std::uint64_t{VCO_MAX} * m;
}

// ---------- 연습 1: 허용 오차 안의 최근접 해 ----------
struct TolResult { bool ok; std::uint32_t m, n, p; std::int32_t err_ppm; };
// 오차 |HSE·N/(M·P) − SYSCLK| = |HSE·N − SYSCLK·M·P| / (M·P) 를 분수 그대로 교차 곱으로 비교한다 (먼저 찾은 것이 동점 승).
constexpr TolResult pll_search_tol(std::uint32_t hse_hz, std::uint32_t sys_hz, std::uint32_t max_ppm) {
    using namespace f4;
    TolResult best{false, 0, 0, 0, 0};
    std::uint64_t best_num = 0, best_den = 1;
    for (std::uint32_t m = M_MIN; m <= M_MAX; ++m)
        for (std::uint32_t n = N_MIN; n <= N_MAX; ++n) {
            if (!vco_ok(hse_hz, m, n)) continue;
            for (std::uint32_t p = 2; p <= 8; p += 2) {
                const std::uint64_t f_x = std::uint64_t{hse_hz} * n, t_x = std::uint64_t{sys_hz} * m * p;   // ×(M·P)
                if (f_x > std::uint64_t{SYSCLK_MAX} * m * p) continue;                                        // 168 MHz 초과 금지
                const std::uint64_t num = f_x > t_x ? f_x - t_x : t_x - f_x, den = std::uint64_t{m} * p;
                if (best.m == 0 || num * best_den < best_num * den) {
                    best_num = num; best_den = den;
                    const std::int64_t s = static_cast<std::int64_t>(f_x) - static_cast<std::int64_t>(t_x);
                    const std::int64_t d = static_cast<std::int64_t>(t_x);
                    const std::int64_t ppm = s >= 0 ? (s * 1'000'000 + d / 2) / d : -((-s * 1'000'000 + d / 2) / d);
                    best = {false, m, n, p, static_cast<std::int32_t>(ppm)};
                }
            }
        }
    best.ok = best.m != 0 && best_num * 1'000'000 <= std::uint64_t{max_ppm} * sys_hz * best_den;
    return best;
}

// ---------- 연습 2: 보레이트(rate) 오차를 최소화하는 div ----------
// round(PCLK/baud) 는 "비트 시간" 오차를 최소화한다. 보레이트 오차 |PCLK/V − baud| 최소는 floor/ceil 중 비교해서 고른다.
constexpr std::uint32_t round_div(std::uint32_t pclk, std::uint32_t baud) { return (pclk + baud / 2) / baud; }
constexpr std::uint32_t rate_optimal_div(std::uint32_t pclk, std::uint32_t baud) {
    const std::uint32_t lo = pclk / baud, hi = lo + 1;
    if (lo == 0) return hi;
    // |pclk/lo − b| ≤ |pclk/hi − b|  ⇔  (pclk − lo·b)·hi ≤ (hi·b − pclk)·lo   (양변 ≥ 0)
    const std::uint64_t e_lo = (std::uint64_t{pclk} - std::uint64_t{lo} * baud) * hi;
    const std::uint64_t e_hi = (std::uint64_t{hi} * baud - pclk) * lo;
    return e_lo <= e_hi ? lo : hi;
}

// ---------- 연습 3: 해 세기 + 컴파일 타임 배열 ----------
struct MNP { std::uint32_t m, n, p; constexpr bool operator==(const MNP&) const = default; };
constexpr std::uint32_t count_pll(std::uint32_t hse, std::uint32_t sys, bool usb, MNP* out = nullptr) {
    std::uint32_t k = 0;
    for (std::uint32_t m = f4::M_MIN; m <= f4::M_MAX; ++m)
        for (std::uint32_t n = f4::N_MIN; n <= f4::N_MAX; ++n) {
            if (!vco_ok(hse, m, n)) continue;
            for (std::uint32_t p = 2; p <= 8; p += 2) {
                if (std::uint64_t{hse} * n != std::uint64_t{sys} * m * p) continue;
                const std::uint32_t vco = sys * p;
                if (usb && (vco % f4::USB_HZ != 0 || vco / f4::USB_HZ > 15 || vco / f4::USB_HZ < 2)) continue;
                if (out != nullptr) out[k] = {m, n, p};
                ++k;
            }
        }
    return k;
}
template <std::uint32_t Hse, std::uint32_t Sys, bool Usb>
constexpr auto all_pll() {
    std::array<MNP, count_pll(Hse, Sys, Usb)> a{};   // 크기도 상수 평가 → 두 번 돈다 (세기, 채우기)
    count_pll(Hse, Sys, Usb, a.data());
    return a;
}

// ---------- 연습 4: 타이머 클럭 ----------
// RM0090 대표값 (RCC_DCKCFGR.TIMPRE = 0): APB 분주가 1 이면 TIMxCLK = PCLK, 아니면 2×PCLK.
constexpr std::uint32_t timer_clock(std::uint32_t hclk, std::uint32_t apb_div) {
    const std::uint32_t pclk = hclk / apb_div;
    return apb_div == 1 ? pclk : 2 * pclk;
}
template <std::uint32_t TimClk>
struct Tick1MHz {
    static_assert(TimClk % MHz == 0, "Tick1MHz: timer clock is not a whole number of MHz - 1 us tick impossible");
    static_assert(TimClk / MHz - 1 <= 0xFFFF, "Tick1MHz: PSC is 16-bit");
    static constexpr std::uint16_t psc = static_cast<std::uint16_t>(TimClk / MHz - 1);
};

int main() {
    // 연습 1
    constexpr TolResult t1 = pll_search_tol(12'288'000, 168 * MHz, 1000);
    CHECK(t1.ok && t1.m == 9 && t1.n == 246 && t1.p == 2 && t1.err_ppm == -381);       // 167.936 MHz
    CHECK(!pll_search_tol(12'288'000, 168 * MHz, 300).ok);                              // 300 ppm 정책이면 거부
    constexpr TolResult t2 = pll_search_tol(14'745'600, 168 * MHz, 1000);
    CHECK(t2.ok && t2.m == 14 && t2.n == 319 && t2.p == 2 && t2.err_ppm == -33);
    constexpr TolResult t3 = pll_search_tol(24'576'000, 100 * MHz, 1000);
    CHECK(t3.ok && t3.m == 18 && t3.n == 293 && t3.p == 4 && t3.err_ppm == 107);        // 위쪽 오차도
    constexpr TolResult t4 = pll_search_tol(8 * MHz, 168 * MHz, 0);
    CHECK(t4.ok && t4.m == 4 && t4.n == 168 && t4.p == 2 && t4.err_ppm == 0);           // 정확한 해가 있으면 같은 답

    // 연습 2
    CHECK(round_div(42 * MHz, 4'952'830) == 8 && rate_optimal_div(42 * MHz, 4'952'830) == 9);   // 8.48: 반올림은 8, rate 는 9
    CHECK(round_div(84 * MHz, 115200) == rate_optimal_div(84 * MHz, 115200));                    // 보통은 같다
    int differ = 0;
    for (std::uint32_t b = 9600; b <= 5'000'000; b += 977) differ += round_div(42 * MHz, b) != rate_optimal_div(42 * MHz, b);
    std::printf("  round vs rate-optimal differ: %d cases\n", differ);
    CHECK(differ > 0);

    // 연습 3
    CHECK(count_pll(8 * MHz, 168 * MHz, true) == 5 && count_pll(25 * MHz, 168 * MHz, true) == 1);
    constexpr auto sols = all_pll<8 * MHz, 168 * MHz, true>();
    static_assert(sols.size() == 5);
    CHECK((sols[0] == MNP{4, 168, 2} && sols[4] == MNP{8, 336, 2}));
    CHECK(count_pll(12'288'000, 168 * MHz, false) == 0);

    // 연습 4
    CHECK(timer_clock(168 * MHz, 4) == 84 * MHz && timer_clock(168 * MHz, 2) == 168 * MHz && timer_clock(84 * MHz, 1) == 84 * MHz);
    CHECK(Tick1MHz<timer_clock(168 * MHz, 4)>::psc == 83 && Tick1MHz<timer_clock(168 * MHz, 2)>::psc == 167);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
