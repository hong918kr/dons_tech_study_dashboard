// CPP-A06 demo: solution.cpp 의 코드를 그대로 Cortex-M4 로 컴파일해 "런타임 비용 0" 을 확인한다
//   bash target_check.sh      (맥 clang thumbv7em + VM arm-none-eabi-g++ 14.2.1)
// 레지스터 주소는 STM32F4 대표값 (RCC 0x40023800, FLASH 0x40023C00, USART1 0x40011000).
#define CPP_A06_NO_HARNESS
#include "solution.cpp"

namespace {
struct Mmio {   // Soc 와 같은 인터페이스(write/read) — apply_plan 이 템플릿이라 버스만 바꿔 끼운다
    static constexpr std::uint32_t addr(Reg r) {
        switch (r) {
        case Reg::RCC_CR: return 0x4002'3800u;    case Reg::RCC_PLLCFGR: return 0x4002'3804u;
        case Reg::RCC_CFGR: return 0x4002'3808u;  case Reg::FLASH_ACR: return 0x4002'3C00u;
        case Reg::USART_BRR: return 0x4001'1008u; default: return 0x4001'100Cu;
        }
    }
    static void write(Reg r, std::uint32_t v) { *reinterpret_cast<volatile std::uint32_t*>(addr(r)) = v; }
    static std::uint32_t read(Reg r) { return *reinterpret_cast<volatile std::uint32_t*>(addr(r)); }
};
constexpr BoardClock kBoard{8 * f4::MHz, 168 * f4::MHz, true, 1, 4, 2};
}  // namespace

// (1) 컴파일 타임 경로: 탐색·인코딩·BRR 이 전부 빌드 중에 끝나야 한다 → 상수 store 만
extern "C" void clock_init_ct() {
    Mmio bus;
    board_init<kBoard, Bus::Apb2, 115200>(bus);
}

// (2) 런타임 폴백: HSE 를 실행 중에 안다 → 탐색 코드가 플래시에 들어간다 (크기·호출 심볼 측정)
extern "C" int clock_init_rt(std::uint32_t hse_hz) {
    Mmio bus;
    return static_cast<int>(board_init_runtime(hse_hz, kBoard, Bus::Apb2, 115200, Over::x16, bus));
}

#if WITH_PPM == 1
// (3a) 런타임 baud 로 BRR 만 — u64 나눗셈 (pclk + baud/2) / baud
extern "C" int baud_brr_rt(std::uint32_t pclk, std::uint32_t baud) { return uart_brr(pclk, baud, Over::x16).brr; }
#elif WITH_PPM == 2
// (3b) ppm(표시용) 까지 꺼내 쓰면 — 64비트 부호 있는 나눗셈이 하나 더
extern "C" int baud_ppm_rt(std::uint32_t pclk, std::uint32_t baud) { return uart_brr(pclk, baud, Over::x16).err_ppm; }
#elif WITH_PPM == 3
// (3c) baud 는 상수, pclk 만 런타임 (= clock_init_rt 의 모양)
extern "C" int baud_ppm_rt_const(std::uint32_t pclk) { return uart_brr(pclk, 115200, Over::x16).err_ppm; }
#endif

#ifdef NOT_FORCED
// (4) constexpr 함수 ≠ 컴파일 타임 실행. -O0 에서 무엇이 런타임에 남는지 형태별로 (target_check.sh o0)
constexpr PllReq kReq{8 * f4::MHz, 168 * f4::MHz, true};
extern "C" std::uint32_t o0_const_local() { const PllResult r = pll_search(kReq); return encode_pllcfgr(r.cfg); }
extern "C" std::uint32_t o0_plain_local() { PllResult r = pll_search(kReq); return encode_pllcfgr(r.cfg); }
extern "C" std::uint32_t o0_expr() { return encode_pllcfgr(pll_search(kReq).cfg); }
extern "C" std::uint32_t o0_consteval_arg() { return encode_pllcfgr(pll_or_fail(kReq)); }
extern "C" std::uint32_t o0_constexpr_var() { constexpr std::uint32_t v = encode_pllcfgr(pll_or_fail(kReq)); return v; }
#endif
