// CPP-A01 demo 공용 헤더: solution.cpp 의 "BEGIN driver ~ END driver" 부분과 같은 내용 (mock·테스트·Part C 는 뺐다).
// demo_*.cpp 만 include 한다. 다른 레슨에서 include 하지 말 것.
#pragma once
#include <atomic>
#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

/* ================= 레지스터 맵: STM32F4 SPI 레이아웃 (RM0090 대표값) ================= */
namespace reg {
constexpr std::uint32_t CR1 = 0x00, SR = 0x08, DR = 0x0C;                     // 오프셋
constexpr std::uint32_t CR1_CPHA = 1u << 0, CR1_CPOL = 1u << 1, CR1_MSTR = 1u << 2, CR1_BR_POS = 3,
                        CR1_SPE = 1u << 6, CR1_LSBFIRST = 1u << 7, CR1_SSI = 1u << 8, CR1_SSM = 1u << 9;
constexpr std::uint32_t SR_RXNE = 1u << 0, SR_TXE = 1u << 1, SR_BSY = 1u << 7;
}  // namespace reg

enum class Mode : std::uint8_t { M0 = 0, M1 = 1, M2 = 2, M3 = 3 };          // 값의 bit1 = CPOL, bit0 = CPHA
static_assert(reg::CR1_CPOL == 2u && reg::CR1_CPHA == 1u, "Mode 값을 CR1[1:0] 에 그대로 넣는다");
enum class Baud : std::uint8_t { Div2 = 0, Div4, Div8, Div16, Div32, Div64, Div128, Div256 };   // CR1.BR[2:0]
struct Config { Mode mode = Mode::M0; Baud baud = Baud::Div8; bool lsb_first = false; };

/* CR1 값 (SPE 제외). 마스터 + 소프트웨어 NSS (SSM=SSI=1: NSS 핀 없이 마스터 유지 → MODF 없음) */
constexpr std::uint32_t encode_cr1(const Config& c) noexcept {
    return reg::CR1_MSTR | reg::CR1_SSM | reg::CR1_SSI | (std::uint32_t(c.baud) << reg::CR1_BR_POS) |
           std::uint32_t(c.mode) | (c.lsb_first ? reg::CR1_LSBFIRST : 0u);
}

/* 실제 칩용 접근 정책 (CPP-M01 의 MmioBus 를 줄인 것). 호스트 테스트는 MockHw 를 쓴다 */
template <std::uintptr_t Base> struct MmioHw {
    static std::uint32_t read(std::uint32_t off) noexcept { return *reinterpret_cast<volatile std::uint32_t*>(Base + off); }
    static void write(std::uint32_t off, std::uint32_t v) noexcept { *reinterpret_cast<volatile std::uint32_t*>(Base + off) = v; }
};

/* ================= 소유권 토큰: 주변장치(Hw) 하나당 프로그램 전체에서 하나 ================= */
template <class Hw> class PeriphToken {
public:
    PeriphToken(const PeriphToken&) = delete;
    PeriphToken& operator=(const PeriphToken&) = delete;
    PeriphToken(PeriphToken&&) noexcept = default;           // 이동은 된다 — 그리고 원본도 여전히 "토큰"이다 (Part B)
    PeriphToken& operator=(PeriphToken&&) = delete;
    ~PeriphToken() = default;

    // 처음 한 번만 토큰을 준다. 두 번째부터 nullopt. exchange 라서 ISR/스레드가 동시에 불러도 한 명만 받는다.
    [[nodiscard]] static std::optional<PeriphToken> take() noexcept {
        if (taken_.exchange(true, std::memory_order_acq_rel)) return std::nullopt;
        return PeriphToken{};
    }
    static void reset_for_test() noexcept { taken_.store(false); }   // 호스트 테스트 전용

private:
    PeriphToken() noexcept = default;                         // 아무도 직접 만들 수 없다
    static inline std::atomic<bool> taken_{false};
};

/* ================= 상태 태그와 드라이버 ================= */
struct Disabled {};
struct Configured {};
struct Enabled {};
template <class S> concept SpiState = std::same_as<S, Disabled> || std::same_as<S, Configured> || std::same_as<S, Enabled>;

struct Xfer { bool ok; std::uint8_t rx; };
constexpr unsigned kPolls = 1000;

template <SpiState S, class Hw> class Spi;
template <class Hw> using AnySpi = std::variant<Spi<Disabled, Hw>, Spi<Configured, Hw>, Spi<Enabled, Hw>>;
template <class Next, class Prev> using Fallible = std::variant<Next, Prev>;   // 0 = 성공한 새 상태, 1 = 실패 → 원래 상태를 돌려받음
template <class Hw> [[nodiscard]] AnySpi<Hw> adopt(PeriphToken<Hw>&& t) noexcept;

template <SpiState S, class Hw> class Spi {
public:
    using state = S;
    Spi(const Spi&) = delete;
    Spi& operator=(const Spi&) = delete;
    Spi(Spi&&) noexcept = default;       // 빈 객체의 이동 = 아무 일도 안 한다 → 원본도 계속 쓸 수 있다 (Part B 의 구멍)
    Spi& operator=(Spi&&) = delete;
    ~Spi() = default;                    // 하드웨어를 건드리지 않는다: moved-from 객체에도 소멸자는 불리니까

    // "리셋 직후"라고 가정한다 — 가정이라는 걸 이름에 드러낸다. 모르면 adopt().
    [[nodiscard]] static Spi from_reset(PeriphToken<Hw>&& t) noexcept requires std::same_as<S, Disabled> {
        return Spi{std::move(t)};
    }

    // Part A — TODO A1: SPE=0 인 지금 CR1 에 encode_cr1(c) 를 "한 번" 쓴다 (설정 변경은 꺼져 있을 때만 안전)
    [[nodiscard]] Spi<Configured, Hw> configure(const Config& c) && noexcept requires std::same_as<S, Disabled> {
        Hw::write(reg::CR1, encode_cr1(c));
        return Spi<Configured, Hw>{std::move(tok_)};
    }

    // Part A — TODO A2: CR1 의 SPE 비트만 켠다 (읽고-고쳐-쓰기, 다른 비트는 그대로)
    [[nodiscard]] Spi<Enabled, Hw> enable() && noexcept requires std::same_as<S, Configured> {
        Hw::write(reg::CR1, Hw::read(reg::CR1) | reg::CR1_SPE);
        return Spi<Enabled, Hw>{std::move(tok_)};
    }

    // 설정을 지우고 리셋 상태로 (주어진 코드)
    [[nodiscard]] Spi<Disabled, Hw> reset() && noexcept requires std::same_as<S, Configured> {
        Hw::write(reg::CR1, 0u);
        return Spi<Disabled, Hw>{std::move(tok_)};
    }

    // 토큰을 돌려준다 (주어진 코드). Disabled 에서만 — 켜진 채로 소유권을 놓지 못하게
    [[nodiscard]] PeriphToken<Hw> release() && noexcept requires std::same_as<S, Disabled> { return std::move(tok_); }

    // Part A — TODO A3: TXE 를 기다려 DR 에 쓰고, RXNE 를 기다려 DR 을 읽는다. 각 대기는 max_polls 번까지 → 넘으면 {false, 0}
    [[nodiscard]] Xfer transfer(std::uint8_t tx, unsigned max_polls = kPolls) & noexcept requires std::same_as<S, Enabled> {
        unsigned polls = 0;
        while ((Hw::read(reg::SR) & reg::SR_TXE) == 0u)
            if (++polls >= max_polls) return {false, 0};
        Hw::write(reg::DR, tx);
        polls = 0;
        while ((Hw::read(reg::SR) & reg::SR_RXNE) == 0u)
            if (++polls >= max_polls) return {false, 0};
        return {true, static_cast<std::uint8_t>(Hw::read(reg::DR))};
    }

    // Part A — TODO A4: BSY=0 을 max_polls 번까지 기다린 뒤 SPE 만 끈다 → index 0 (Configured).
    //          BSY 가 안 풀리면 아무것도 쓰지 않고 *자기 자신* 을 index 1 로 돌려준다 (상태도 토큰도 잃지 않는다)
    [[nodiscard]] Fallible<Spi<Configured, Hw>, Spi> disable(unsigned max_polls = kPolls) && noexcept
        requires std::same_as<S, Enabled> {
        unsigned polls = 0;
        while ((Hw::read(reg::SR) & reg::SR_BSY) != 0u)
            if (++polls >= max_polls) return Fallible<Spi<Configured, Hw>, Spi>{std::in_place_index<1>, std::move(*this)};
        Hw::write(reg::CR1, Hw::read(reg::CR1) & ~reg::CR1_SPE);
        return Fallible<Spi<Configured, Hw>, Spi>{std::in_place_index<0>, Spi<Configured, Hw>{std::move(tok_)}};
    }

private:
    template <SpiState, class> friend class Spi;
    template <class H> friend AnySpi<H> adopt(PeriphToken<H>&& t) noexcept;
    explicit Spi(PeriphToken<Hw>&& t) noexcept : tok_(std::move(t)) {}
    [[no_unique_address]] PeriphToken<Hw> tok_;
};

/* Part B — TODO B1: 상태를 모를 때 (부트로더 핸드오프, 소프트 리셋). CR1 을 "한 번" 읽고, 아무것도 쓰지 않는다.
 *   SPE=1 → Enabled,  CR1≠0 (SPE=0) → Configured,  CR1==0 → Disabled */
template <class Hw> AnySpi<Hw> adopt(PeriphToken<Hw>&& t) noexcept {
    const std::uint32_t cr1 = Hw::read(reg::CR1);
    if ((cr1 & reg::CR1_SPE) != 0u) return AnySpi<Hw>{Spi<Enabled, Hw>{std::move(t)}};
    if (cr1 != 0u) return AnySpi<Hw>{Spi<Configured, Hw>{std::move(t)}};
    return AnySpi<Hw>{Spi<Disabled, Hw>{std::move(t)}};
}

/* Part B — TODO B2: 어떤 상태든 원하는 설정 c 의 Configured 로 만든다.
 *   Disabled → configure,  Configured → reset → configure,  Enabled → disable → reset → configure.
 *   disable 이 실패하면(BSY) 받은 Enabled 를 그대로 돌려준다. 켜진 채로 CR1 설정 비트를 바꾸면 안 된다. */
template <class Hw> [[nodiscard]] AnySpi<Hw> normalize(AnySpi<Hw>&& any, const Config& c, unsigned max_polls = kPolls) noexcept {
    return std::visit([&](auto&& s) -> AnySpi<Hw> {
        using T = std::remove_cvref_t<decltype(s)>;
        if constexpr (std::same_as<typename T::state, Disabled>) {
            return std::move(s).configure(c);
        } else if constexpr (std::same_as<typename T::state, Configured>) {
            return std::move(s).reset().configure(c);
        } else {
            auto r = std::move(s).disable(max_polls);
            if (auto* off = std::get_if<0>(&r)) return std::move(*off).reset().configure(c);
            return std::move(*std::get_if<1>(&r));            // 끄지 못함 → Enabled 그대로 돌려준다
        }
    }, std::move(any));
}
