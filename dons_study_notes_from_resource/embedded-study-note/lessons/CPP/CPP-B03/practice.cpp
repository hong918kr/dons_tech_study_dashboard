/* CPP-B03 추가 연습 — 한 번에 검증
 * build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp
 */
#include <cstdint>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>   /* std::to_underlying (C++23) 이 있으면 여기 */

using std::uint8_t;
using std::uint32_t;

/* 연습 4: to_u — C++23 이면 std::to_underlying, 아니면 static_cast 헬퍼 */
template <class E>
    requires std::is_enum_v<E>
constexpr std::underlying_type_t<E> to_u(E e) noexcept {
#if defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying >= 202102L
    return std::to_underlying(e);
#else
    return static_cast<std::underlying_type_t<E>>(e);
#endif
}
constexpr bool kHasStdToUnderlying =
#if defined(__cpp_lib_to_underlying)
    true;
#else
    false;
#endif

/* 연습 1: 기저 타입으로 크기 고르기 + "namespace 안의 unscoped enum" 은 여전히 새어 나간다 */
enum class DataBits : uint8_t { Seven = 7, Eight = 8, Nine = 9 };
enum class Parity   : uint8_t { None = 0b00, Even = 0b10, Odd = 0b11 };
enum class StopBits : uint8_t { One = 0, Half = 1, Two = 2, OneAndHalf = 3 };
struct WireFormat { DataBits data; Parity parity; StopBits stop; };   /* 프로토콜/EEPROM 에 그대로 */
static_assert(sizeof(DataBits) == 1 && sizeof(WireFormat) == 3 && alignof(WireFormat) == 1);

namespace legacy { enum Mode : uint32_t { Async, Sync }; }            /* 이름은 가둬도 변환은 그대로 */
static_assert(std::is_convertible_v<legacy::Mode, uint32_t>);
static_assert(!std::is_convertible_v<DataBits, uint8_t>);

/* 연습 2: opt-in 비트마스크 연산자 — 플래그 enum 마다 연산자를 복붙하지 않는다 */
template <class E> struct enable_bitmask : std::false_type {};
template <class E> concept Bitmask = std::is_enum_v<E> && enable_bitmask<E>::value;

template <Bitmask E> constexpr E operator|(E a, E b) noexcept { return static_cast<E>(to_u(a) | to_u(b)); }
template <Bitmask E> constexpr E operator&(E a, E b) noexcept { return static_cast<E>(to_u(a) & to_u(b)); }
template <Bitmask E> constexpr E operator~(E a) noexcept { return static_cast<E>(~to_u(a)); }
template <Bitmask E> constexpr E& operator|=(E& a, E b) noexcept { return a = a | b; }
template <Bitmask E> constexpr bool any(E a) noexcept { return to_u(a) != 0; }

enum class DmaFlag : uint32_t { None = 0, TcIe = 1u << 1, HtIe = 1u << 2, TeIe = 1u << 3, Circ = 1u << 8 };
template <> struct enable_bitmask<DmaFlag> : std::true_type {};
enum class Mode : uint32_t { Async, Sync };   /* opt-in 안 함 */
enum class Tiny : uint8_t { A = 0x01, B = 0x80 };
template <> struct enable_bitmask<Tiny> : std::true_type {};

template <class A, class B> concept BitOrable = requires(A a, B b) { a | b; };
static_assert(BitOrable<DmaFlag, DmaFlag>);
static_assert(!BitOrable<Mode, Mode>);
static_assert(!BitOrable<DmaFlag, Mode>);
static_assert(BitOrable<int, int>);   /* 내장 int | int 는 그대로 (템플릿이 끼어들지 않음) */

/* 연습 3: 레지스터 원시값 → enum, reserved 는 nullopt. using enum (C++20) 로 switch 짧게 */
constexpr std::optional<Parity> parity_from_bits(uint32_t two_bits) noexcept {
    const auto p = static_cast<Parity>(two_bits & 0x3u);
    using enum Parity;
    switch (p) {
    case None: case Even: case Odd: return p;
    }
    return std::nullopt;
}
constexpr const char* parity_name(Parity p) noexcept {
    using enum Parity;
    switch (p) {
    case None: return "none";
    case Even: return "even";
    case Odd:  return "odd";
    }
    return "reserved";   /* 이 줄이 없으면 범위 밖 값에서 함수 끝으로 떨어짐 = UB */
}

/* 연습 5: enum 을 인덱스로 쓰는 룩업 — 1 프레임의 비트 길이(반 비트 단위) */
constexpr uint32_t frame_half_bits(const WireFormat& w) noexcept {
    constexpr uint8_t stop_half[4] = {2, 1, 4, 3};               /* One, Half, Two, OneAndHalf */
    const uint32_t parity_bits = (to_u(w.parity) & 0b10u) ? 1u : 0u;   /* PCE 비트 */
    return 2u * (1u + to_u(w.data) + parity_bits) + stop_half[to_u(w.stop) & 0x3u];
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* 연습 1 */
    CHECK(to_u(DataBits::Nine) == 9);
    legacy::Mode lm = legacy::Sync;
    uint32_t leaked = lm;            /* 컴파일됨 — enum class 였다면 에러 */
    CHECK(leaked == 1u);

    /* 연습 2 */
    DmaFlag ccr = DmaFlag::TcIe | DmaFlag::TeIe;
    ccr |= DmaFlag::Circ;
    CHECK(to_u(ccr) == 0x10Au);
    CHECK(!any(ccr & DmaFlag::HtIe));
    CHECK(to_u(~Tiny::A) == 0xFEu);  /* uint8_t: ~ 는 int 로 승격 후 static_cast 로 다시 8비트 */

    /* 연습 3 */
    CHECK(parity_from_bits(0b11u) == Parity::Odd);
    CHECK(!parity_from_bits(0b01u).has_value());
    CHECK(parity_from_bits(0xFFu) == Parity::Odd);   /* 2비트만 본다 */
    CHECK(parity_name(static_cast<Parity>(1))[0] == 'r');
    CHECK(parity_name(Parity::Even)[0] == 'e');

    /* 연습 4 */
    static_assert(to_u(Parity::Odd) == 3);
    std::printf("std::to_underlying available: %s (__cplusplus=%ld)\n",
                kHasStdToUnderlying ? "yes" : "no", static_cast<long>(__cplusplus));
    CHECK(to_u(DmaFlag::Circ) == 0x100u);

    /* 연습 5: 8N1 = 10비트 = 20 반비트, 9E2 = 1+9+1+2 = 13비트 = 26, 7O1.5 = 1+7+1+1.5 = 10.5 → 21 */
    CHECK(frame_half_bits({DataBits::Eight, Parity::None, StopBits::One}) == 20u);
    CHECK(frame_half_bits({DataBits::Nine, Parity::Even, StopBits::Two}) == 26u);
    CHECK(frame_half_bits({DataBits::Seven, Parity::Odd, StopBits::OneAndHalf}) == 21u);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
