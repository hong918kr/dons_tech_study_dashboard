/* CPP-A06 컴파일타임 클럭 트리·보레이트 검증기
 *
 * STM32F4 급 클럭 트리(HSE → PLL M/N/P/Q → SYSCLK → AHB/APB1/APB2 → USART)를 constexpr 로 계산하고,
 * 잘못된 설정은 "빌드가 실패" 하게 만든다. 숫자 한계는 RM0090 대표값 (아래 f4:: 참고).
 *   Part A: PLL M/N/P/Q 전수 탐색 (constexpr) + consteval 로 컴파일 타임 강제 + RCC_PLLCFGR 인코딩
 *   Part B: USART BRR (OVER16 / OVER8, mantissa/fraction) + 정수 ppm 오차 + 2% 게이트 (static_assert / concept)
 *   Part C: 보드 클럭 계획 → mock RCC/FLASH/USART 초기화 (쓰기 순서 포함) + 런타임 폴백(HSE 를 실행 중에 알 때)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 * demo_target.cpp 는 CPP_A06_NO_HARNESS 를 정의하고 이 파일을 include 해서 같은 코드를 Cortex-M4 로 컴파일한다.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// ================= STM32F4 급 제약 (RM0090 대표값 — 이 레슨에서 RM 원문 대조는 하지 않음) =================
namespace f4 {
inline constexpr std::uint32_t MHz = 1'000'000;
inline constexpr std::uint32_t HSE_MIN = 4 * MHz, HSE_MAX = 26 * MHz;          // 외부 크리스털 범위
inline constexpr std::uint32_t VCO_IN_MIN = 1 * MHz, VCO_IN_MAX = 2 * MHz;     // HSE/M (2 MHz 권장: 지터)
inline constexpr std::uint32_t VCO_MIN = 100 * MHz, VCO_MAX = 432 * MHz;       // HSE/M×N
inline constexpr std::uint32_t M_MIN = 2, M_MAX = 63, N_MIN = 50, N_MAX = 432, Q_MIN = 2, Q_MAX = 15;
inline constexpr std::uint32_t SYSCLK_MAX = 168 * MHz, APB1_MAX = 42 * MHz, APB2_MAX = 84 * MHz;
inline constexpr std::uint32_t USB_HZ = 48 * MHz;                               // USB OTG FS 는 정확히 48 MHz
}  // namespace f4

// ================= Part A: PLL 탐색 =================
struct PllReq {
    std::uint32_t hse_hz;
    std::uint32_t sysclk_hz;
    bool need_usb48;            // true: VCO/Q 가 정확히 48 MHz 여야 함. false: Q 는 48 MHz 이하가 되는 최소값
};
enum class PllErr : std::uint8_t { None, HseRange, SysclkRange, NoExactSolution };
struct PllCfg {
    std::uint32_t m, n, p, q;
    std::uint32_t vco_hz, sysclk_hz, q_hz;
    constexpr bool operator==(const PllCfg&) const = default;
};
struct PllResult {
    PllErr err;
    PllCfg cfg;
    constexpr bool ok() const { return err == PllErr::None; }
    constexpr bool operator==(const PllResult&) const = default;
};

// TODO A1: 전수 탐색. 순서는 M 오름차순 → N 오름차순 → P ∈ {2,4,6,8} 오름차순, 처음 맞는 해를 돌려준다.
//   조건: HSE 범위, 0 < SYSCLK ≤ 168 MHz, 1 MHz ≤ HSE/M ≤ 2 MHz, 100 MHz ≤ HSE/M×N ≤ 432 MHz,
//         HSE×N == SYSCLK×M×P (정확히, 64비트 정수로), Q 규칙 (need_usb48 이면 VCO % 48 MHz == 0 이고 2 ≤ Q ≤ 15,
//         아니면 Q = max(2, ceil(VCO / 48 MHz))). 해가 없으면 NoExactSolution. 부동소수점 금지.
constexpr PllResult pll_search(const PllReq& r) {
    (void)r;   // TODO A1
    return {PllErr::None, {}};
}

// consteval 실패 경로: 상수 평가 중 constexpr 아닌 함수를 부르면 컴파일 에러가 되고, 에러에 이 이름이 찍힌다.
inline void pll_error_no_exact_solution_for_this_hse_and_sysclk() {}
inline void pll_error_hse_or_sysclk_out_of_range() {}

// TODO A2: 컴파일 타임 강제. 실패하면 위 두 함수 중 맞는 것을 불러 빌드를 멈춘다. 성공하면 cfg.
consteval PllCfg pll_or_fail(PllReq r) {
    (void)r;   // TODO A2
    return PllCfg{};
}

// TODO A3: RCC_PLLCFGR (RM0090 대표값): PLLM[5:0], PLLN[14:6], PLLP[17:16] = P/2−1, PLLSRC[22] = 1 (HSE), PLLQ[27:24]
constexpr std::uint32_t encode_pllcfgr(const PllCfg& c) {
    (void)c;   // TODO A3
    return 0;
}

// ================= Part B: USART BRR =================
enum class Over : std::uint8_t { x16, x8 };       // CR1.OVER8 = 0 / 1
enum class BaudErr : std::uint8_t { None, ZeroBaud, TooFast, TooSlow, ErrorTooHigh };
inline constexpr std::uint32_t MAX_BAUD_ERR_PPM = 20'000;   // 2 %
struct BrrResult {
    BaudErr err;
    std::uint32_t div;        // round(PCLK/baud) = 비트 하나의 PCLK 사이클 수 (= 16·USARTDIV 또는 8·USARTDIV)
    std::uint16_t brr;        // 레지스터 값: mantissa[15:4] | fraction[3:0] (OVER8: fraction[2:0], bit3 = 0)
    std::int32_t err_ppm;     // (실제 − 목표)/목표 × 1e6, 0 에서 먼 쪽으로 반올림 (표시용)
    constexpr bool ok() const { return err == BaudErr::None; }
};

// 주어진 것: div 의 허용 범위. 아래 한계 = USARTDIV ≥ 1, 위 한계 = mantissa 12비트 + fraction 최대
constexpr std::uint32_t div_min(Over ov) { return ov == Over::x16 ? 16u : 8u; }
constexpr std::uint32_t div_max(Over ov) { return ov == Over::x16 ? 0xFFFFu : 0x7FFFu; }

// TODO B1: 정수만으로.
//   div = round(PCLK/baud) (반올림: + baud/2). OVER16: fraction 4비트, div 는 16..0xFFFF. OVER8: fraction 3비트,
//   div 는 8..0x7FFF (mantissa 12비트), BRR = (div>>3)<<4 | (div&7). 실제 보레이트 = PCLK/div.
//   오차 게이트는 반올림한 ppm 이 아니라 정확한 유리수 비교: |PCLK − div·baud|·1e6 > max_ppm·div·baud 면 ErrorTooHigh.
//   (TooFast/TooSlow 일 때도 div 는 채운다. brr/err_ppm 은 범위 안일 때만.)
constexpr BrrResult uart_brr(std::uint32_t pclk, std::uint32_t baud, Over ov,
                             std::uint32_t max_ppm = MAX_BAUD_ERR_PPM) {
    (void)pclk; (void)baud; (void)ov; (void)max_ppm;   // TODO B1
    return {BaudErr::None, 0, 0, 0};
}

// 게이트 1: static_assert — 메시지가 "규칙" 을, requested here 체인이 "값" 을 말한다 (CPP-M14 참고).
// 증인(witness) 템플릿: 실패에 필요한 숫자를 템플릿 인자로 실어서 에러의 instantiation 줄에 찍히게 한다.
template <bool Bad, std::uint32_t Div, std::uint32_t DivMin, std::uint32_t DivMax>
struct baud_divisor_out_of_range {
    static_assert(!Bad, "UartBrr: Div = round(PCLK/baud) must be in [DivMin, DivMax] - too fast: Over::x8 or faster bus");
    static constexpr bool ok = true;
};
template <bool Bad, std::int32_t ErrPpm, std::uint32_t Div>
struct baud_error_over_2pct {
    static_assert(!Bad, "UartBrr: |baud error| > 2% - see ErrPpm (rounded ppm) and Div in the witness template args");
    static constexpr bool ok = true;
};
template <std::uint32_t Pclk, std::uint32_t Baud, Over Ov = Over::x16>
struct UartBrr {
    static constexpr BrrResult r = uart_brr(Pclk, Baud, Ov);
    static_assert(Baud != 0, "UartBrr: baud must not be 0");
    static_assert(baud_divisor_out_of_range<r.err == BaudErr::TooFast || r.err == BaudErr::TooSlow, r.div, div_min(Ov),
                                            div_max(Ov)>::ok);
    static_assert(baud_error_over_2pct<r.err == BaudErr::ErrorTooHigh, r.err_ppm, r.div>::ok);
    static constexpr std::uint16_t value = r.brr;
    static constexpr std::int32_t err_ppm = r.err_ppm;
};
// 게이트 2: concept — 조건을 "물어볼" 수 있다 (requires 로 감지, 오버로드 선택). 메시지는 컴파일러 기본.
template <std::uint32_t Pclk, std::uint32_t Baud, Over Ov = Over::x16>
concept BaudOk = uart_brr(Pclk, Baud, Ov).ok();

// ================= Part C: 보드 클럭 계획 + mock 레지스터 =================
enum class Bus : std::uint8_t { Apb1, Apb2 };
struct BoardClock {               // C++20: 구조적 클래스 타입 → 템플릿 인자로 그대로 넘긴다
    std::uint32_t hse_hz, sysclk_hz;
    bool usb48;
    std::uint32_t ahb_div, apb1_div, apb2_div;
};
struct ClockPlan {
    PllErr err;
    PllCfg pll;
    std::uint32_t hclk, pclk1, pclk2;
    std::uint32_t pllcfgr, cfgr, flash_acr;
    constexpr bool operator==(const ClockPlan&) const = default;
};

// 주어진 것 (BM-B10 이 유도를 맡는다): 2.7–3.6 V 에서 30 MHz 마다 wait state 1 (RM0090 대표값)
constexpr std::uint32_t flash_wait_states(std::uint32_t hclk) { return hclk == 0 ? 0 : (hclk - 1) / (30 * f4::MHz); }
// 주어진 것: 프리스케일러 코드 (RM0090 대표값). 지원 안 하는 분주비면 0xFF.
constexpr std::uint32_t hpre_code(std::uint32_t d) {
    switch (d) { case 1: return 0x0; case 2: return 0x8; case 4: return 0x9; case 8: return 0xA; case 16: return 0xB;
                 case 64: return 0xC; case 128: return 0xD; case 256: return 0xE; case 512: return 0xF; default: return 0xFF; }
}
constexpr std::uint32_t ppre_code(std::uint32_t d) {
    switch (d) { case 1: return 0x0; case 2: return 0x4; case 4: return 0x5; case 8: return 0x6; case 16: return 0x7;
                 default: return 0xFF; }
}
inline constexpr std::uint32_t CFGR_SW_PLL = 0x2;
inline constexpr std::uint32_t ACR_PRFTEN_ICEN_DCEN = 0x700;

// TODO C1: PLL 결과 → 버스 클럭과 레지스터 값. pll 이 실패면 err 만 채우고 나머지 0.
//   hclk = SYSCLK/ahb, pclk1 = hclk/apb1, pclk2 = hclk/apb2.
//   cfgr = HPRE[7:4] | PPRE1[12:10] | PPRE2[15:13] | SW=PLL[1:0], flash_acr = 0x700 | LATENCY(wait states).
constexpr ClockPlan plan_from(const PllResult& pr, const BoardClock& b) {
    (void)b;   // TODO C1
    return {pr.err, pr.cfg, 0, 0, 0, 0, 0, 0};
}

// 컴파일 타임 경로: 보드 상수 하나 → 검증된 계획. 범위 밖이면 빌드 실패.
template <BoardClock B>
struct Clock {
    static_assert(hpre_code(B.ahb_div) != 0xFF && ppre_code(B.apb1_div) != 0xFF && ppre_code(B.apb2_div) != 0xFF,
                  "Clock: unsupported AHB/APB divider");
    static constexpr ClockPlan plan = plan_from(PllResult{PllErr::None, pll_or_fail({B.hse_hz, B.sysclk_hz, B.usb48})}, B);
    static_assert(plan.pclk1 <= f4::APB1_MAX, "Clock: APB1 > 42 MHz - raise apb1_div");
    static_assert(plan.pclk2 <= f4::APB2_MAX, "Clock: APB2 > 84 MHz - raise apb2_div");
    template <Bus bus> static constexpr std::uint32_t pclk = bus == Bus::Apb1 ? plan.pclk1 : plan.pclk2;
};

// mock 레지스터 (실제 칩: RCC 0x40023800, FLASH 0x40023C00 — 대표값). 쓰기는 모두 log 에 남는다.
enum class Reg : std::uint8_t { RCC_CR, RCC_PLLCFGR, RCC_CFGR, FLASH_ACR, USART_BRR, USART_CR1 };
inline constexpr std::uint32_t CR_HSEON = 1u << 16, CR_HSERDY = 1u << 17, CR_PLLON = 1u << 24, CR_PLLRDY = 1u << 25;
inline constexpr std::uint32_t CR1_UE = 1u << 13, CR1_OVER8 = 1u << 15, CR1_TE = 1u << 3, CR1_RE = 1u << 2;
struct Soc {
    std::uint32_t rcc_cr = 0x0000'0083, rcc_pllcfgr = 0x2400'3010, rcc_cfgr = 0, flash_acr = 0;
    std::uint32_t usart_brr = 0, usart_cr1 = 0;
    std::array<Reg, 16> log{};
    std::size_t nlog = 0;
    std::uint32_t& at(Reg r) {
        switch (r) {
        case Reg::RCC_CR: return rcc_cr;           case Reg::RCC_PLLCFGR: return rcc_pllcfgr;
        case Reg::RCC_CFGR: return rcc_cfgr;       case Reg::FLASH_ACR: return flash_acr;
        case Reg::USART_BRR: return usart_brr;     default: return usart_cr1;
        }
    }
    void write(Reg r, std::uint32_t v) {
        if (nlog < log.size()) log[nlog++] = r;
        if (r == Reg::RCC_CR) {                    // "하드웨어": ON 을 쓰면 다음 읽기에 RDY
            if (v & CR_HSEON) v |= CR_HSERDY;
            if (v & CR_PLLON) v |= CR_PLLRDY;
        }
        at(r) = v;
    }
    std::uint32_t read(Reg r) { return at(r); }
    int index_of(Reg r) const { for (std::size_t i = 0; i < nlog; ++i) if (log[i] == r) return static_cast<int>(i); return -1; }
};

// TODO C2: 계획을 레지스터에 적용. 순서가 중요하다:
//   ① RCC_CR |= HSEON, HSERDY 대기  ② FLASH_ACR = flash_acr (클럭을 올리기 "전에" wait state)
//   ③ RCC_PLLCFGR = pllcfgr  ④ RCC_CR |= PLLON, PLLRDY 대기  ⑤ RCC_CFGR = cfgr (프리스케일러 + SW=PLL)
//   (mock 은 ON 을 쓰면 RDY 가 바로 선다. 대기 루프는 RDY 를 읽어 확인만.)
//   Regs = write(Reg, u32) / read(Reg) 를 가진 버스: 호스트는 Soc(mock), 타깃은 MMIO (demo_target.cpp) — CPP-M01 참고
template <class Regs>
inline void apply_plan(const ClockPlan& p, Regs& s) {
    (void)p; (void)s;   // TODO C2
}

template <class Regs>
inline void apply_uart(std::uint16_t brr, Over ov, Regs& s) {
    s.write(Reg::USART_BRR, brr);
    s.write(Reg::USART_CR1, CR1_UE | CR1_TE | CR1_RE | (ov == Over::x8 ? CR1_OVER8 : 0u));
}

// 컴파일 타임 경로의 초기화: 모든 값이 상수 → 타깃에서는 상수 store 만 남아야 한다 (demo_target.cpp 로 확인).
template <BoardClock B, Bus bus, std::uint32_t Baud, Over Ov = Over::x16, class Regs>
inline void board_init(Regs& s) {
    apply_plan(Clock<B>::plan, s);
    apply_uart(UartBrr<Clock<B>::template pclk<bus>, Baud, Ov>::value, Ov, s);
}

// TODO C3: 런타임 폴백 (HSE 를 부팅 후에야 안다: 보드 ID 스트랩, OTP, CSS 로 HSI 전환 ...).
//   pll_search 와 "같은 답" 을 내되 N 루프 없이: M 오름차순 → P 오름차순으로 돌며
//   g = gcd(VCO, HSE), a = VCO/g, b = HSE/g 이면 HSE×N == VCO×M ⇔ b | M, N = a·(M/b). 32비트 정수만.
constexpr std::uint32_t gcd32(std::uint32_t a, std::uint32_t b) { while (b != 0) { const std::uint32_t t = a % b; a = b; b = t; } return a; }
constexpr PllResult pll_solve_fast(const PllReq& r) {
    (void)r;   // TODO C3
    return {PllErr::NoExactSolution, {}};
}

enum class InitStatus : std::uint8_t { Ok, PllFailed, BusTooFast, BaudFailed };
// 런타임 경로의 초기화: 실패를 값으로 돌려주고, 실패하면 레지스터를 건드리지 않는다.
template <class Regs>
inline InitStatus board_init_runtime(std::uint32_t hse_hz, const BoardClock& shape, Bus bus, std::uint32_t baud, Over ov,
                                     Regs& s) {
    const PllResult pr = pll_solve_fast({hse_hz, shape.sysclk_hz, shape.usb48});
    if (!pr.ok()) return InitStatus::PllFailed;
    const ClockPlan p = plan_from(pr, shape);
    if (p.pclk1 > f4::APB1_MAX || p.pclk2 > f4::APB2_MAX) return InitStatus::BusTooFast;
    const BrrResult br = uart_brr(bus == Bus::Apb1 ? p.pclk1 : p.pclk2, baud, ov);
    if (!br.ok()) return InitStatus::BaudFailed;
    apply_plan(p, s);
    apply_uart(br.brr, ov, s);
    return InitStatus::Ok;
}

#ifndef CPP_A06_NO_HARNESS
#include <cstdio>
// ================= 테스트 하네스 =================
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

using f4::MHz;
static bool cfg_is(const PllResult& r, std::uint32_t m, std::uint32_t n, std::uint32_t p, std::uint32_t q) {
    return r.ok() && r.cfg.m == m && r.cfg.n == n && r.cfg.p == p && r.cfg.q == q;
}
static bool brr_is(const BrrResult& r, BaudErr e, std::uint32_t div, std::uint16_t brr, std::int32_t ppm) {
    return r.err == e && r.div == div && r.brr == brr && r.err_ppm == ppm;
}

// 상수 평가 결과: 아래 값들은 "컴파일러가" 계산했다 (CHECK 는 결과를 런타임에 보고만 한다 — 스텁도 빌드되게).
static constexpr PllResult kF407 = pll_search({8 * MHz, 168 * MHz, true});
static constexpr PllCfg kF407Forced = pll_or_fail({8 * MHz, 168 * MHz, true});
static constexpr BoardClock kBoard{8 * MHz, 168 * MHz, true, 1, 4, 2};

// HSE 표: 컴파일 타임 기준값을 원소마다 따로 상수 평가 (한 번의 평가가 step 한도를 넘지 않게)
static constexpr std::uint32_t kHseTable[] = {
    4 * MHz,  5 * MHz,  6 * MHz,  7 * MHz,  8 * MHz,  9 * MHz,  10 * MHz, 11 * MHz, 12 * MHz, 13 * MHz,
    14 * MHz, 15 * MHz, 16 * MHz, 17 * MHz, 18 * MHz, 19 * MHz, 20 * MHz, 21 * MHz, 22 * MHz, 23 * MHz,
    24 * MHz, 25 * MHz, 26 * MHz, 12'288'000, 24'576'000, 14'745'600, 16'384'000, 3'686'400, 27 * MHz};
static constexpr std::uint32_t kSysTable[] = {168 * MHz, 120 * MHz, 100 * MHz, 84 * MHz};
inline constexpr std::size_t kNH = std::size(kHseTable), kNS = std::size(kSysTable), kNRef = kNH * kNS * 2;
constexpr PllReq ref_req(std::size_t i) { return {kHseTable[i / (kNS * 2)], kSysTable[(i / 2) % kNS], (i % 2) == 1}; }
template <std::size_t I> inline constexpr PllResult kRefOne = pll_search(ref_req(I));
template <std::size_t... I>
constexpr std::array<PllResult, sizeof...(I)> make_ref(std::index_sequence<I...>) { return {kRefOne<I>...}; }
static constexpr auto kRef = make_ref(std::make_index_sequence<kNRef>{});

static std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

int main() {
    // ---------- Part A: PLL 탐색 ----------
    CHECK(cfg_is(kF407, 4, 168, 2, 7) && kF407.cfg.vco_hz == 336 * MHz && kF407.cfg.q_hz == 48 * MHz);
    CHECK(kF407Forced == kF407.cfg);                                                    // consteval 경로 = constexpr 경로
    CHECK(cfg_is(pll_search({25 * MHz, 168 * MHz, true}), 25, 336, 2, 7));             // HSE/M 이 정수가 아닌 M 들을 지나서
    CHECK(cfg_is(pll_search({16 * MHz, 84 * MHz, true}), 8, 168, 4, 7));               // P=2 는 VCO 168 → 48 로 안 나뉨
    CHECK(cfg_is(pll_search({8 * MHz, 120 * MHz, true}), 4, 120, 2, 5));
    CHECK(cfg_is(pll_search({8 * MHz, 100 * MHz, false}), 4, 100, 2, 5) && pll_search({8 * MHz, 100 * MHz, false}).cfg.q_hz == 40 * MHz);
    CHECK(pll_search({8 * MHz, 100 * MHz, true}).err == PllErr::NoExactSolution);     // 48 MHz 를 못 만든다
    CHECK(pll_search({12'288'000, 168 * MHz, false}).err == PllErr::NoExactSolution);  // 오디오 크리스털
    CHECK(pll_search({8 * MHz, 180 * MHz, true}).err == PllErr::SysclkRange);
    CHECK(pll_search({3 * MHz, 168 * MHz, true}).err == PllErr::HseRange && pll_search({8 * MHz, 0, false}).err == PllErr::SysclkRange);
    CHECK(cfg_is(pll_search({26 * MHz, 168 * MHz, true}), 13, 168, 2, 7));             // HSE/M = 2 MHz 경계 (포함)
    CHECK(cfg_is(pll_search({4 * MHz, 168 * MHz, true}), 2, 168, 2, 7));               // M_MIN 경계
    CHECK(encode_pllcfgr(kF407.cfg) == 0x0740'2A04u);
    CHECK(encode_pllcfgr(PllCfg{25, 336, 2, 7, 0, 0, 0}) == 0x0740'5419u && encode_pllcfgr(PllCfg{8, 168, 4, 7, 0, 0, 0}) == 0x0741'2A08u);
    CHECK(encode_pllcfgr(PllCfg{63, 432, 8, 15, 0, 0, 0}) == 0x0F43'6C3Fu);             // 모든 필드 최대: 겹침 없음

    // ---------- Part B: BRR ----------
    CHECK(brr_is(uart_brr(84 * MHz, 115200, Over::x16), BaudErr::None, 729, 0x2D9, 229));
    CHECK(brr_is(uart_brr(42 * MHz, 115200, Over::x16), BaudErr::None, 365, 0x16D, -1142));
    CHECK(brr_is(uart_brr(84 * MHz, 219000, Over::x16), BaudErr::None, 384, 0x180, -1142));   // fraction 반올림 → mantissa 올림
    CHECK(brr_is(uart_brr(42 * MHz, 2 * MHz, Over::x16), BaudErr::None, 21, 0x15, 0));
    CHECK(brr_is(uart_brr(42 * MHz, 2 * MHz, Over::x8), BaudErr::None, 21, 0x25, 0));         // 같은 div, 다른 인코딩
    CHECK(brr_is(uart_brr(31, 1, Over::x8), BaudErr::None, 31, 0x37, 0));                     // OVER8: bit3 은 0
    CHECK(uart_brr(16 * MHz, 921600, Over::x16).err == BaudErr::ErrorTooHigh && uart_brr(16 * MHz, 921600, Over::x16).err_ppm == 21242);
    CHECK(uart_brr(16 * MHz, 921600, Over::x8).err == BaudErr::ErrorTooHigh);                 // OVER8 은 오차를 못 줄인다
    CHECK(uart_brr(42 * MHz, 3 * MHz, Over::x16).err == BaudErr::TooFast && uart_brr(42 * MHz, 3 * MHz, Over::x16).div == 14);
    CHECK(brr_is(uart_brr(42 * MHz, 3 * MHz, Over::x8), BaudErr::None, 14, 0x16, 0));         // OVER8 은 최대 속도를 2배로
    CHECK(uart_brr(84 * MHz, 1200, Over::x16).err == BaudErr::TooSlow && uart_brr(84 * MHz, 2400, Over::x8).err == BaudErr::TooSlow);
    CHECK(brr_is(uart_brr(84 * MHz, 2400, Over::x16), BaudErr::None, 35000, 0x88B8, 0));
    CHECK(uart_brr(84 * MHz, 0, Over::x16).err == BaudErr::ZeroBaud);
    // 2% 경계: 정확히 2% 는 통과, 1 Hz 넘으면 실패 — 반올림한 ppm(둘 다 20000) 으로는 구별 불가
    CHECK(brr_is(uart_brr(20'400'000, 1'000'000, Over::x16), BaudErr::None, 20, 0x14, 20000));
    CHECK(brr_is(uart_brr(20'400'001, 1'000'000, Over::x16), BaudErr::ErrorTooHigh, 20, 0x14, 20000));
    CHECK(brr_is(uart_brr(19'600'000, 1'000'000, Over::x16), BaudErr::None, 20, 0x14, -20000));
    CHECK(uart_brr(19'599'999, 1'000'000, Over::x16).err == BaudErr::ErrorTooHigh);
    CHECK(uart_brr(16 * MHz, 921600, Over::x16, 25'000).ok());                                // 정책 완화 (2.5%)
    // 게이트: 개념(concept)으로 컴파일 타임에 판정 (BaudOk 의 결과는 상수)
    constexpr bool g_ok = BaudOk<84 * MHz, 115200>, g_hi = BaudOk<16 * MHz, 921600>;
    constexpr bool g_fast16 = BaudOk<42 * MHz, 3 * MHz, Over::x16>, g_fast8 = BaudOk<42 * MHz, 3 * MHz, Over::x8>;
    CHECK(g_ok && !g_hi && !g_fast16 && g_fast8);
    CHECK((UartBrr<84 * MHz, 115200>::value == 0x2D9 && UartBrr<42 * MHz, 921600>::err_ppm == -9284));

    // ---------- Part C: 보드 ----------
    using Board = Clock<kBoard>;
    CHECK(Board::plan.hclk == 168 * MHz && Board::plan.pclk1 == 42 * MHz && Board::plan.pclk2 == 84 * MHz);
    CHECK(Board::plan.pllcfgr == 0x0740'2A04u && Board::plan.cfgr == 0x9402u && Board::plan.flash_acr == 0x705u);
    CHECK(flash_wait_states(30 * MHz) == 0 && flash_wait_states(30 * MHz + 1) == 1 && flash_wait_states(168 * MHz) == 5);
    Soc s;
    board_init<kBoard, Bus::Apb2, 115200>(s);
    CHECK(s.rcc_pllcfgr == 0x0740'2A04u && s.rcc_cfgr == 0x9402u && s.flash_acr == 0x705u);
    CHECK((s.rcc_cr & (CR_HSEON | CR_HSERDY | CR_PLLON | CR_PLLRDY)) == (CR_HSEON | CR_HSERDY | CR_PLLON | CR_PLLRDY));
    CHECK(s.usart_brr == 0x2D9u && s.usart_cr1 == (CR1_UE | CR1_TE | CR1_RE));
    // 순서: wait state 를 먼저, PLL 설정 → PLLON → SW=PLL
    CHECK(s.index_of(Reg::FLASH_ACR) >= 0 && s.index_of(Reg::FLASH_ACR) < s.index_of(Reg::RCC_CFGR));
    CHECK(s.index_of(Reg::RCC_PLLCFGR) >= 0 && s.index_of(Reg::RCC_PLLCFGR) < s.index_of(Reg::RCC_CFGR) && s.nlog == 7);
    CHECK(s.log[0] == Reg::RCC_CR && s.log[2] == Reg::RCC_PLLCFGR && s.log[3] == Reg::RCC_CR && s.log[4] == Reg::RCC_CFGR);
    Soc s8;
    board_init<kBoard, Bus::Apb2, 3'000'000, Over::x8>(s8);
    CHECK(s8.usart_brr == 0x34u && (s8.usart_cr1 & CR1_OVER8) != 0);

    // 런타임 폴백 = 컴파일 타임 경로 (같은 보드, 같은 레지스터, 같은 순서)
    Soc rt;
    volatile std::uint32_t hse_strap = 8 * MHz;                       // "부팅 후에 읽은 값" — 상수 접기 방지
    CHECK(board_init_runtime(hse_strap, kBoard, Bus::Apb2, 115200, Over::x16, rt) == InitStatus::Ok);
    CHECK(rt.rcc_pllcfgr == s.rcc_pllcfgr && rt.rcc_cfgr == s.rcc_cfgr && rt.flash_acr == s.flash_acr && rt.usart_brr == s.usart_brr);
    CHECK(rt.nlog == s.nlog && rt.log == s.log);
    Soc bad;
    CHECK(board_init_runtime(12'288'000, kBoard, Bus::Apb2, 115200, Over::x16, bad) == InitStatus::PllFailed && bad.nlog == 0);
    CHECK(board_init_runtime(16 * MHz, BoardClock{16 * MHz, 168 * MHz, true, 1, 2, 2}, Bus::Apb2, 115200, Over::x16, bad) == InitStatus::BusTooFast);
    CHECK(board_init_runtime(8 * MHz, kBoard, Bus::Apb1, 3'000'000, Over::x16, bad) == InitStatus::BaudFailed && bad.nlog == 0);

    // 일치 검사 1: HSE 표 × SYSCLK 표 × usb — 컴파일 타임 기준(kRef) vs 런타임 빠른 탐색
    int agree = 0, solved = 0;
    for (std::size_t i = 0; i < kNRef; ++i) {
        volatile std::uint32_t hse = ref_req(i).hse_hz;                  // 런타임 값으로
        const PllResult fast = pll_solve_fast({hse, ref_req(i).sysclk_hz, ref_req(i).need_usb48});
        agree += (fast == kRef[i]) ? 1 : 0;
        solved += kRef[i].ok() ? 1 : 0;
    }
    std::printf("  HSE table: %d/%zu agree, %d solvable\n", agree, kNRef, solved);
    CHECK(agree == static_cast<int>(kNRef) && solved == 161);
    // 일치 검사 2: 무작위 (HSE, SYSCLK) 2000 쌍 — 같은 constexpr 함수를 런타임에 실행한 것 vs 빠른 탐색
    std::uint32_t seed = 0xA06C10C4u;
    int agree2 = 0, solved2 = 0;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t hse = (6u + xorshift32(seed) % 50u) * 500'000u;                   // 3..27.5 MHz, 0.5 MHz 단위
        const std::uint32_t sys = (24u + xorshift32(seed) % 150u) * MHz;                      // 24..173 MHz
        const PllReq q{hse, sys, (xorshift32(seed) & 1u) != 0};
        const PllResult slow = pll_search(q), fast = pll_solve_fast(q);
        agree2 += (slow == fast) ? 1 : 0;
        solved2 += slow.ok() ? 1 : 0;
    }
    std::printf("  random: %d/2000 agree, %d solvable\n", agree2, solved2);
    CHECK(agree2 == 2000 && solved2 == 640);                // 640: 독립 파이썬 구현으로 센 수와 같음

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
