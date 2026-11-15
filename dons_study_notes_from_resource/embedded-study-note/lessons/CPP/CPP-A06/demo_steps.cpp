// CPP-A06 demo: constexpr 평가 한도 — 같은 탐색을 "가지치기 없이" 쓰면 언제 한도에 걸리나
//   bash steps.sh            (맥 clang: 기본 한도 확인, 변형별 step 수 이분 탐색, 컴파일 시간)
//   -DVARIANT=0 pll_search (solution.cpp)  1 = naive (M×N×P×Q 전부)  2 = pll_solve_fast  3 = spin(N) (한도 측정용)
//   -DHSE=... -DSYS=... -DUSB=0/1  (기본: 12.288 MHz, 168 MHz, USB — 해가 없는 = 최악의 경우)
#define CPP_A06_NO_HARNESS
#include "solution.cpp"

#ifndef VARIANT
#define VARIANT 1
#endif
#ifndef HSE
#define HSE 12288000u
#endif
#ifndef SYS
#define SYS 168000000u
#endif
#ifndef USB
#define USB 1
#endif

// 가지치기 없는 전수 탐색: 모든 (M, N, P, Q) 조합을 만들고 마지막에 조건을 본다 — 면접에서 처음 떠올리기 쉬운 모양
constexpr PllResult pll_search_naive(const PllReq& r) {
    using namespace f4;
    for (std::uint32_t m = M_MIN; m <= M_MAX; ++m)
        for (std::uint32_t n = N_MIN; n <= N_MAX; ++n)
            for (std::uint32_t p = 2; p <= 8; p += 2)
                for (std::uint32_t q = Q_MIN; q <= Q_MAX; ++q) {
                    const std::uint64_t hse = r.hse_hz;
                    const bool in_ok = hse >= std::uint64_t{VCO_IN_MIN} * m && hse <= std::uint64_t{VCO_IN_MAX} * m;
                    const bool vco_ok = hse * n >= std::uint64_t{VCO_MIN} * m && hse * n <= std::uint64_t{VCO_MAX} * m;
                    const bool sys_ok = hse * n == std::uint64_t{r.sysclk_hz} * m * p;
                    const bool q_ok = r.need_usb48 ? hse * n == std::uint64_t{USB_HZ} * m * q
                                                   : hse * n <= std::uint64_t{USB_HZ} * m * q;
                    if (in_ok && vco_ok && sys_ok && q_ok)
                        return {PllErr::None, {m, n, p, q, r.sysclk_hz * p, r.sysclk_hz, r.sysclk_hz * p / q}};
                }
    return {PllErr::NoExactSolution, {}};
}

constexpr unsigned spin(unsigned n) { unsigned s = 0; for (unsigned i = 0; i < n; ++i) s += i; return s; }

#if VARIANT == 0
constexpr PllResult kR = pll_search({HSE, SYS, USB != 0});
#elif VARIANT == 1
constexpr PllResult kR = pll_search_naive({HSE, SYS, USB != 0});
#elif VARIANT == 2
constexpr PllResult kR = pll_solve_fast({HSE, SYS, USB != 0});
#else
constexpr unsigned kS = spin(SPIN);
#endif
#if VARIANT <= 2
int result_m() { return static_cast<int>(kR.cfg.m); }   // 결과를 쓰는 척 (객체에는 상수만)
#endif
