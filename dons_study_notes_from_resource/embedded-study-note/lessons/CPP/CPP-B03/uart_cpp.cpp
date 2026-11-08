/* CPP-B03 asm 비교용 — enum class 버전 (c++ -std=c++20 -O2 -S). extern "C" 로 심볼 이름을 C 와 맞춤 */
#if __STDC_HOSTED__
#include <cstdint>
#include <type_traits>
using std::uint32_t;
template <class E> using underlying_t = std::underlying_type_t<E>;
#define IS_ENUM(E) std::is_enum_v<E>
#else   /* -ffreestanding Cortex-M 크로스 빌드: 맥 libc++ 헤더는 못 쓰므로 컴파일러 내장으로 대체 */
using uint32_t = __UINT32_TYPE__;
template <class E> using underlying_t = __underlying_type(E);
#define IS_ENUM(E) __is_enum(E)
#endif

inline constexpr uint32_t CR_MODE_Pos = 1,  CR_MODE_Msk = 0x3u << CR_MODE_Pos;
inline constexpr uint32_t CR_PAR_Pos  = 4,  CR_PAR_Msk  = 0x3u << CR_PAR_Pos;
inline constexpr uint32_t CR_STOP_Pos = 12, CR_STOP_Msk = 0x3u << CR_STOP_Pos;

enum class UartMode : uint32_t { Async, Sync, IrDA, SmartCard };
enum class Parity   : uint32_t { None = 0b00, Even = 0b10, Odd = 0b11 };
enum class StopBits : uint32_t { One, Half, Two, OneAndHalf };
enum class UartIrq  : uint32_t { Rxne = 1u << 0, Wakeup = 1u << 31 };

template <class E> requires IS_ENUM(E)
constexpr underlying_t<E> to_u(E e) noexcept { return static_cast<underlying_t<E>>(e); }
constexpr UartIrq operator~(UartIrq a) noexcept { return static_cast<UartIrq>(~to_u(a)); }

extern "C" uint32_t encode_cr(UartMode m, Parity p, StopBits s) {
    return ((to_u(m) << CR_MODE_Pos) & CR_MODE_Msk) | ((to_u(p) << CR_PAR_Pos) & CR_PAR_Msk)
         | ((to_u(s) << CR_STOP_Pos) & CR_STOP_Msk);
}
extern "C" void uart_configure(volatile uint32_t* cr, UartMode m, Parity p, StopBits s) {
    const uint32_t keep = *cr & ~(CR_MODE_Msk | CR_PAR_Msk | CR_STOP_Msk);
    *cr = keep | encode_cr(m, p, s);
}
extern "C" void uart_disable_irq(volatile uint32_t* ier, UartIrq irq) { *ier = *ier & to_u(~irq); }
extern "C" bool parity_is_valid(Parity p) {
    switch (p) { case Parity::None: case Parity::Even: case Parity::Odd: return true; }
    return false;
}
