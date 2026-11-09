// CPP-M03 demo_errors.cpp — 컴파일 에러 실물 + "컴파일되어 버리는" 반례
// 정상 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -fsyntax-only demo_errors.cpp
//            (SHOW_ERROR=1..10 각각 한 가지 에러)
#include <cstdio>
#include "flags_final.hpp"

namespace hal {
enum class IrqFlag : std::uint32_t { Rxne = 1u << 0, Txe = 1u << 1 };
enum class DmaFlag : std::uint32_t { Tc = 1u << 0, Ht = 1u << 1 };
enum class Mode    : std::uint32_t { Off, On, Sleep };
using fw::operator|;
using fw::operator~;
}  // namespace hal
template <> struct fw::flag_traits<hal::IrqFlag> {
    static constexpr auto valid = fw::mask_of(hal::IrqFlag::Rxne, hal::IrqFlag::Txe);
};
template <> struct fw::flag_traits<hal::DmaFlag> {
    static constexpr auto valid = fw::mask_of(hal::DmaFlag::Tc, hal::DmaFlag::Ht);
};

// using 을 깜빡한 네임스페이스: opt-in 은 했지만 E | E 를 찾지 못한다
namespace other {
enum class Led : std::uint32_t { Red = 1u << 0, Green = 1u << 1 };
}
template <> struct fw::flag_traits<other::Led> {
    static constexpr auto valid = fw::mask_of(other::Led::Red, other::Led::Green);
};

enum class NoWidth { X = 1 };                 // 기저 타입 미지정 → int
template <> struct fw::flag_traits<NoWidth> { static constexpr int valid = 1; };
enum class Two : std::uint32_t { Both = 3 };  // 2비트짜리 열거자

// 반례 1: 연산자를 멤버/hidden friend 대신 자유 템플릿으로 쓰면
template <typename E> struct TFlags { std::uint32_t v = 0; constexpr TFlags(E e) : v(static_cast<std::uint32_t>(e)) {} };
template <typename E>
constexpr TFlags<E> operator|(TFlags<E> a, TFlags<E> b) { a.v |= b.v; return a; }

// 반례 2: operator bool 이 explicit 이 아니면
template <typename E> struct BadFlags {
    std::uint32_t v;
    constexpr operator bool() const { return v != 0; }
};

using IrqSet = fw::Flags<hal::IrqFlag>;
using hal::IrqFlag; using hal::DmaFlag; using hal::Mode;

int main() {
    // 반례 2 는 컴파일이 되어 버린다. IRQ 집합과 DMA 집합이 섞여서 bool 두 개의 OR 가 된다.
    BadFlags<IrqFlag> irq{0x4};
    BadFlags<DmaFlag> dma{0x1};
    const bool same = (irq == dma);            // bool 로 변환된 뒤 비교된다: 4 == 1 인데 true
    const int  sum  = irq + dma;
    std::printf("BadFlags<Irq>{4} == BadFlags<Dma>{1} -> %d\n", same);
    std::printf("BadFlags<Irq>{4} +  BadFlags<Dma>{1} -> %d\n", sum);

#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    auto e = IrqFlag::Rxne | DmaFlag::Tc;                   // E1: 다른 enum 끼리
#elif defined(SHOW_ERROR) && SHOW_ERROR == 2
    IrqSet f = IrqFlag::Rxne;
    auto e = f | DmaFlag::Tc;                               // E2: Flags<Irq> | Dma
#elif defined(SHOW_ERROR) && SHOW_ERROR == 3
    auto e = Mode::On | Mode::Sleep;                        // E3: opt-in 안 한 enum
#elif defined(SHOW_ERROR) && SHOW_ERROR == 4
    IrqSet f = IrqFlag::Rxne;
    std::uint32_t e = f;                                    // E4: 정수로 암시적 변환 금지
#elif defined(SHOW_ERROR) && SHOW_ERROR == 5
    IrqSet f = IrqFlag::Rxne;
    auto e = f | 0x1u;                                      // E5: 정수 리터럴과 섞기
#elif defined(SHOW_ERROR) && SHOW_ERROR == 6
    auto e = other::Led::Red | other::Led::Green;           // E6: opt-in 했지만 using 깜빡 → ADL 이 못 찾음
#elif defined(SHOW_ERROR) && SHOW_ERROR == 7
    constexpr auto e = fw::mask_of(Two::Both);              // E7: 2비트 열거자
#elif defined(SHOW_ERROR) && SHOW_ERROR == 8
    fw::Flags<NoWidth> e;                                   // E8: 기저 타입 미지정(int)
#elif defined(SHOW_ERROR) && SHOW_ERROR == 9
    TFlags<IrqFlag> t = IrqFlag::Rxne;
    auto e = t | IrqFlag::Txe;                              // E9: 템플릿 연산자는 추론에서 암시적 변환을 안 본다
#elif defined(SHOW_ERROR) && SHOW_ERROR == 10
    auto e = irq | dma;                                     // E10: clang -Wall 이 | 만은 잡아 준다 (-Wbitwise-instead-of-logical)
#else
    int e = 0;
#endif
    (void)e;
    return 0;
}
