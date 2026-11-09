// CPP-M03 demo_asm.cpp — 같은 일을 하는 raw uint32_t 버전과 Flags 버전을 나란히 컴파일해 비교한다.
//   bash asm_compare.sh
// 함수 이름 규칙: raw_xxx ↔ flags_xxx
#include "flags_final.hpp"

using u32 = std::uint32_t;
using u8  = std::uint8_t;

enum class Irq : u32 { Rxne = 1u << 0, Txe = 1u << 1, Overrun = 1u << 2, FrameErr = 1u << 3, Wake = 1u << 31 };
enum class T8  : u8  { A = 1u << 0, B = 1u << 1, Top = 1u << 7 };
using fw::operator|;
using fw::operator~;
template <> struct fw::flag_traits<Irq> {
    static constexpr auto valid = fw::mask_of(Irq::Rxne, Irq::Txe, Irq::Overrun, Irq::FrameErr, Irq::Wake);
};
template <> struct fw::flag_traits<T8> { static constexpr auto valid = fw::mask_of(T8::A, T8::B, T8::Top); };
using F  = fw::Flags<Irq>;
using F8 = fw::Flags<T8>;

constexpr u32 RXNE = 1u << 0, TXE = 1u << 1, VALID = 0x8000'000Fu;

// 1. OR
u32 raw_or(u32 a)  { return a | RXNE | TXE; }
F   flags_or(F a)  { return a | Irq::Rxne | Irq::Txe; }

// 2. 단일 비트 검사
bool raw_test1(u32 a) { return (a & TXE) != 0; }
bool flags_test1(F a) { return a.test(Irq::Txe); }

// 3. 전부 켜져 있는가 (all-of)
bool raw_test2(u32 a) { return (a & (RXNE | TXE)) == (RXNE | TXE); }
bool flags_test2(F a) { return a.test(Irq::Rxne | Irq::Txe); }

// 4. 하나라도 (any-of) — if (f & m)
bool raw_any(u32 a)   { return (a & (RXNE | TXE)) != 0; }
bool flags_any(F a)   { return static_cast<bool>(a & (Irq::Rxne | Irq::Txe)); }

// 5. 플래그 하나 지우기 (값 안에서)
u32 raw_del(u32 a)    { return a & ~RXNE; }
F   flags_del(F a)    { return a & ~Irq::Rxne; }
F   flags_del2(F a)   { return a.without(Irq::Rxne); }

// 6. 전체 반전: raw ~ 와 마스크된 ~
u32 raw_not(u32 a)    { return ~a; }
u32 raw_not_v(u32 a)  { return ~a & VALID; }
F   flags_not(F a)    { return ~a; }

// 7. 레지스터 원시값 → pending (SR & IER)
u32 raw_pending(const volatile u32* sr, const volatile u32* ier)   { return *sr & *ier; }
u32 raw_pending_v(const volatile u32* sr, const volatile u32* ier) { return *sr & *ier & VALID; }
F   flags_pending(const volatile u32* sr, const volatile u32* ier) { return F::from_raw(*sr) & F::from_raw(*ier); }

// 8. 레지스터 RMW: set / clear
void raw_set(volatile u32* r)     { *r = *r | RXNE; }
void flags_set(volatile u32* r)   { *r = F{Irq::Rxne}.set_in(*r); }
void raw_clear(volatile u32* r)   { *r = *r & ~RXNE; }
void flags_clear(volatile u32* r) { *r = F{Irq::Rxne}.clear_in(*r); }

// 9. 반복: 켜진 비트마다 외부 함수 호출
void sink(u32);
void raw_each(u32 a) {
    while (a != 0) {
        sink(a & (0u - a));
        a &= a - 1;
    }
}
void flags_each(F a) { a.for_each([](Irq e) { sink(static_cast<u32>(e)); }); }

// 10. 런타임 E → Flags (생성자 마스크가 남는가)
u32 raw_from_enum(Irq e)  { return static_cast<u32>(e); }
F   flags_from_enum(Irq e) { return F{e}; }

// 11. 8비트 기저 타입: 호출 규약 차이가 보이는가
u8  raw8_or(u8 a)   { return static_cast<u8>(a | 0x81u); }
F8  flags8_or(F8 a) { return a | T8::A | T8::Top; }
bool raw8_test(u8 a)   { return (a & 0x80u) != 0; }
bool flags8_test(F8 a) { return a.test(T8::Top); }
