// CPP-M01 demo 공용 헤더: solution.cpp 의 Reg / MmioBus / UartMap / 드라이버와 같은 내용 (MockBus 와 테스트만 뺐다).
// demo_*.cpp 만 include 한다. 다른 레슨에서 include 하지 말 것.
#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 4);

struct RO { static constexpr bool can_read = true,  can_write = false; };
struct WO { static constexpr bool can_read = false, can_write = true;  };
struct RW { static constexpr bool can_read = true,  can_write = true;  };

template <typename A>
concept AccessPolicy = requires {
    { A::can_read }  -> std::convertible_to<bool>;
    { A::can_write } -> std::convertible_to<bool>;
};

struct MmioBus {
    template <RegWord T> [[nodiscard]] static T read(std::uintptr_t a) noexcept {
        return *reinterpret_cast<const volatile T*>(a);
    }
    template <RegWord T> static void write(std::uintptr_t a, T v) noexcept {
        *reinterpret_cast<volatile T*>(a) = v;
    }
};

template <typename B>
concept RegisterBus = requires(std::uintptr_t a, std::uint32_t v) {
    { B::template read<std::uint32_t>(a) } -> std::same_as<std::uint32_t>;
    { B::template write<std::uint32_t>(a, v) } -> std::same_as<void>;
};

template <std::uintptr_t Addr, typename T, typename A, typename Bus = MmioBus>
    requires RegWord<T> && AccessPolicy<A> && RegisterBus<Bus> && (Addr % sizeof(T) == 0)
struct Reg {
    using value_type = T;
    using access     = A;
    using bus        = Bus;
    static constexpr std::uintptr_t address = Addr;
    static constexpr std::size_t    width   = sizeof(T);

    [[nodiscard]] static T read() noexcept requires A::can_read { return Bus::template read<T>(Addr); }
    static void write(T v) noexcept requires A::can_write { Bus::template write<T>(Addr, v); }
    static void modify(T clear_mask, T set_mask) noexcept requires (A::can_read && A::can_write) {
        const T cur = read();
        write(static_cast<T>((cur & static_cast<T>(~clear_mask)) | set_mask));
    }
    static void set_bits(T m) noexcept requires (A::can_read && A::can_write) { modify(T{0}, m); }
    static void clear_bits(T m) noexcept requires (A::can_read && A::can_write) { modify(m, T{0}); }
};

template <typename Bus, std::uintptr_t Base = 0x4000'4400u>
struct UartMap {
    using SR  = Reg<Base + 0x00, std::uint32_t, RO, Bus>;
    using RDR = Reg<Base + 0x04, std::uint8_t,  RO, Bus>;
    using TDR = Reg<Base + 0x08, std::uint8_t,  WO, Bus>;
    using CR  = Reg<Base + 0x0C, std::uint32_t, RW, Bus>;
    using BRR = Reg<Base + 0x10, std::uint16_t, RW, Bus>;
    using ICR = Reg<Base + 0x14, std::uint32_t, WO, Bus>;
    using ID  = Reg<Base + 0x18, std::uint32_t, RO, Bus>;
};

namespace uart {
constexpr std::uint32_t SR_RXNE = 1u << 0;
constexpr std::uint32_t SR_TXE  = 1u << 1;
constexpr std::uint32_t SR_ORE  = 1u << 3;
constexpr std::uint32_t CR_EN   = 1u << 0;
constexpr std::uint32_t CHIP_ID = 0x5541'5254u;
}  // namespace uart

template <typename Map>
bool uart_init(std::uint16_t brr) noexcept {
    if (Map::ID::read() != uart::CHIP_ID) return false;
    Map::BRR::write(brr);
    Map::CR::set_bits(uart::CR_EN);
    return true;
}

template <typename Map>
std::size_t uart_send(std::span<const std::uint8_t> data, unsigned max_polls) noexcept {
    std::size_t n = 0;
    for (std::uint8_t b : data) {
        unsigned polls = 0;
        while ((Map::SR::read() & uart::SR_TXE) == 0) {
            if (++polls >= max_polls) return n;
        }
        Map::TDR::write(b);
        ++n;
    }
    return n;
}
