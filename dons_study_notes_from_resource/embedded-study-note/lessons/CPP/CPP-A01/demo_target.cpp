// CPP-A01 demo_target.cpp — typestate 드라이버 vs 같은 레지스터 접근을 하는 C 스타일 코드, Cortex-M4 asm 비교
//   bash asm_targets.sh asm    (맥 clang, thumbv7em)      bash asm_targets.sh gcc   (VM arm-none-eabi-g++ 14.2.1)
// 함수 쌍마다 하드웨어 접근 순서가 같다. 다른 것은 "상태를 누가 기억하나" (타입 vs 프로그래머) 뿐.
#include "spi_typestate.hpp"

using Hw1 = MmioHw<0x4001'3000u>;                       // STM32F4 SPI1 베이스 (RM0090 대표값)
constexpr Config kCfg{};                                // Mode0, Div8, MSB first → CR1 = 0x314

struct SpiRegs { volatile std::uint32_t CR1, CR2, SR, DR; };
#define SPI1 (reinterpret_cast<SpiRegs*>(0x4001'3000u))

// (1) 리셋 → 설정 → 켜기 → 1바이트 교환
std::uint8_t ts_bringup_send(PeriphToken<Hw1>&& tok, std::uint8_t b) {
    auto en = Spi<Disabled, Hw1>::from_reset(std::move(tok)).configure(kCfg).enable();
    const Xfer x = en.transfer(b);
    return x.ok ? x.rx : 0u;
}
extern "C" std::uint8_t c_bringup_send(std::uint8_t b) {
    SPI1->CR1 = 0x314u;
    SPI1->CR1 = SPI1->CR1 | 0x40u;
    unsigned p = 0;
    while ((SPI1->SR & 0x2u) == 0u) if (++p >= kPolls) return 0u;
    SPI1->DR = b;
    p = 0;
    while ((SPI1->SR & 0x1u) == 0u) if (++p >= kPolls) return 0u;
    return static_cast<std::uint8_t>(SPI1->DR);
}

// (1') 대조군: 타입 상태 없이, 같은 Xfer 구조체를 돌려주는 C 스타일 inline 함수 (차이가 typestate 탓인지 Xfer 탓인지 가르기)
static inline Xfer c_transfer(std::uint8_t b) {
    unsigned p = 0;
    while ((SPI1->SR & 0x2u) == 0u) if (++p >= kPolls) return {false, 0};
    SPI1->DR = b;
    p = 0;
    while ((SPI1->SR & 0x1u) == 0u) if (++p >= kPolls) return {false, 0};
    return {true, static_cast<std::uint8_t>(SPI1->DR)};
}
extern "C" std::uint8_t c_bringup_send_x(std::uint8_t b) {
    SPI1->CR1 = 0x314u;
    SPI1->CR1 = SPI1->CR1 | 0x40u;
    const Xfer x = c_transfer(b);
    return x.ok ? x.rx : 0u;
}

// (2) 이미 켜진 드라이버로 1바이트 (인라인 안 되는 경계: 객체 참조가 인자 레지스터를 차지하나?)
[[gnu::noinline]] std::uint8_t ts_send(Spi<Enabled, Hw1>& spi, std::uint8_t b) { return spi.transfer(b).rx; }
[[gnu::noinline]] std::uint8_t ts_send_val(Spi<Enabled, Hw1> spi, std::uint8_t b) { return spi.transfer(b).rx; }
extern "C" [[gnu::noinline]] std::uint8_t c_send(std::uint8_t b) {
    unsigned p = 0;
    while ((SPI1->SR & 0x2u) == 0u) if (++p >= kPolls) return 0u;
    SPI1->DR = b;
    p = 0;
    while ((SPI1->SR & 0x1u) == 0u) if (++p >= kPolls) return 0u;
    return static_cast<std::uint8_t>(SPI1->DR);
}

// (3) 런타임에만 아는 상태: adopt + normalize (std::variant + std::visit) vs C 의 if 사슬
int ts_adopt_normalize(PeriphToken<Hw1>&& tok) {
    return static_cast<int>(normalize(adopt(std::move(tok)), kCfg).index());
}
// (3') 같은 일을 std::visit 없이: index() switch + get_if (valueless 검사 경로가 없다)
template <class Hw> AnySpi<Hw> normalize_switch(AnySpi<Hw>&& any, const Config& c, unsigned max_polls = kPolls) noexcept {
    switch (any.index()) {
    case 0: return std::move(*std::get_if<0>(&any)).configure(c);
    case 1: return std::move(*std::get_if<1>(&any)).reset().configure(c);
    default: {
        auto r = std::move(*std::get_if<2>(&any)).disable(max_polls);
        if (auto* off = std::get_if<0>(&r)) return std::move(*off).reset().configure(c);
        return std::move(*std::get_if<1>(&r));
    }
    }
}
int ts_adopt_normalize_switch(PeriphToken<Hw1>&& tok) {
    return static_cast<int>(normalize_switch(adopt(std::move(tok)), kCfg).index());
}
extern "C" int c_adopt_normalize(void) {
    std::uint32_t cr1 = SPI1->CR1;
    if (cr1 & 0x40u) {
        unsigned p = 0;
        while (SPI1->SR & 0x80u) if (++p >= kPolls) return 2;
        SPI1->CR1 = SPI1->CR1 & ~0x40u;
        cr1 = 1;                                             // "설정돼 있음"
    }
    if (cr1 != 0u) SPI1->CR1 = 0u;
    SPI1->CR1 = 0x314u;
    return 1;
}

// (4) 소유권 토큰 take() 의 비용 (atomic<bool>::exchange)
bool ts_take() { return PeriphToken<Hw1>::take().has_value(); }

// (5) 런타임 검사형 탈출구: 상태를 1바이트 변수로 들고 다니며 매 호출 검사 (practice.cpp 연습 2 의 DynSpi 축약)
struct DynSpi { std::uint8_t st; };                     // 0 Disabled, 1 Configured, 2 Enabled
extern "C" [[gnu::noinline]] int dyn_send(DynSpi* d, std::uint8_t b) {
    if (d->st != 2u) return -1;                              // 잘못된 상태 → 런타임 에러
    unsigned p = 0;
    while ((SPI1->SR & 0x2u) == 0u) if (++p >= kPolls) return -2;
    SPI1->DR = b;
    p = 0;
    while ((SPI1->SR & 0x1u) == 0u) if (++p >= kPolls) return -2;
    return static_cast<int>(SPI1->DR & 0xFFu);
}

static_assert(sizeof(Spi<Enabled, Hw1>) == 1 && std::is_trivially_copyable_v<Spi<Enabled, Hw1>>);
static_assert(sizeof(AnySpi<Hw1>) == 2, "libstdc++ 의 variant: 인덱스 1바이트 + 빈 저장소 1바이트");
