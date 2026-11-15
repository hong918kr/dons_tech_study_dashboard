// CPP-A06 demo: 실패해야 하는 설정들의 실제 컴파일 에러 (solution.cpp 의 게이트 그대로)
//   c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=N demo_errors.cpp      (N = 1..7)
//   SHOW_ERROR 없이 빌드하면 통과한다 (정상 설정만 남음).
#define CPP_A06_NO_HARNESS
#include "solution.cpp"
using f4::MHz;

// 정상 설정: 빌드 통과
constexpr std::uint16_t kOk = UartBrr<84 * MHz, 115200>::value;
constexpr PllCfg kOkPll = pll_or_fail({8 * MHz, 168 * MHz, true});
static_assert(kOk == 0x2D9 && kOkPll.n == 168);

#if SHOW_ERROR == 1      // HSI 16 MHz 에서 921600 — 오차 2.12 %
constexpr auto e1 = UartBrr<16 * MHz, 921600>::value;
#elif SHOW_ERROR == 2    // APB1 42 MHz 에서 3 Mbaud, OVER16 — div 14 < 16
constexpr auto e2 = UartBrr<42 * MHz, 3'000'000>::value;
#elif SHOW_ERROR == 3    // 오디오 크리스털 12.288 MHz 로 168 MHz + USB — 정확한 해 없음
constexpr PllCfg e3 = pll_or_fail({12'288'000, 168 * MHz, true});
#elif SHOW_ERROR == 4    // consteval 에 런타임 값
PllCfg e4(std::uint32_t hse) { return pll_or_fail({hse, 168 * MHz, true}); }
#elif SHOW_ERROR == 5    // 게이트를 requires 절로 걸었을 때의 메시지
template <std::uint32_t P, std::uint32_t B, Over O = Over::x16> requires BaudOk<P, B, O>
inline constexpr std::uint16_t brr_v = uart_brr(P, B, O).brr;
constexpr auto e5 = brr_v<16 * MHz, 921600>;
#elif SHOW_ERROR == 6    // APB1 을 /2 로: 84 MHz > 42 MHz
constexpr auto e6 = Clock<BoardClock{8 * MHz, 168 * MHz, true, 1, 2, 2}>::plan;
#elif SHOW_ERROR == 7    // 보드 상수의 HSE 가 틀림 → Clock 안의 consteval 이 실패
constexpr auto e7 = Clock<BoardClock{12'288'000, 168 * MHz, true, 1, 4, 2}>::plan;
#endif
