// CPP-M01 demo: UART 3개일 때 — 주소가 타입에 있으면 드라이버가 3벌, 베이스 포인터면 1벌
//   -DUSE_TEMPLATE : Reg 맵 기반 드라이버를 UART 3개에 인스턴스화
//   (없으면)        : 런타임 베이스 주소를 받는 드라이버 1벌
#include "reg.hpp"

#ifdef USE_TEMPLATE
template <std::uintptr_t Base>
using U = UartMap<MmioBus, Base>;
[[gnu::noinline]] static std::size_t tx_u1(std::span<const std::uint8_t> d) { return uart_send<U<0x40004400u>>(d, 100); }
[[gnu::noinline]] static std::size_t tx_u2(std::span<const std::uint8_t> d) { return uart_send<U<0x40004800u>>(d, 100); }
[[gnu::noinline]] static std::size_t tx_u3(std::span<const std::uint8_t> d) { return uart_send<U<0x40004C00u>>(d, 100); }
#else
[[gnu::noinline]] static std::size_t tx_any(std::uintptr_t base, std::span<const std::uint8_t> d) {
    std::size_t n = 0;
    for (std::uint8_t b : d) {
        unsigned polls = 0;
        while ((*reinterpret_cast<const volatile std::uint32_t*>(base) & uart::SR_TXE) == 0) {
            if (++polls >= 100) return n;
        }
        *reinterpret_cast<volatile std::uint8_t*>(base + 0x08) = b;
        ++n;
    }
    return n;
}
#endif

extern "C" std::size_t app(const std::uint8_t* p, std::size_t len) {
    std::span<const std::uint8_t> d(p, len);
#ifdef USE_TEMPLATE
    return tx_u1(d) + tx_u2(d) + tx_u3(d);
#else
    return tx_any(0x40004400u, d) + tx_any(0x40004800u, d) + tx_any(0x40004C00u, d);
#endif
}
