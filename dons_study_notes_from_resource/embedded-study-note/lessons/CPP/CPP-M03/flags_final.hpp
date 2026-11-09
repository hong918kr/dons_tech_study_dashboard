// CPP-M03 flags_final.hpp — solution.cpp 의 라이브러리 부분을 그대로 복사한 것 (demo_*.cpp 전용)
#pragma once
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fw {

// 정의 없음. 특수화해서 `valid` 를 준 enum 만 Flags 를 쓸 수 있다 (opt-in).
template <typename E> struct flag_traits;

namespace detail {
// 정의 없는 함수: consteval 안에서 호출되면 컴파일 에러가 나고, 함수 이름이 에러 메시지가 된다.
// (-fno-exceptions 빌드에서는 throw 를 쓸 수 없다)
void enumerator_is_zero_or_not_single_bit();
void enumerator_bit_is_duplicated();
}  // namespace detail

// Part B: 열거자 목록 → valid 마스크. 0, 2비트 이상, 중복이면 컴파일 에러.
template <typename E, std::same_as<E>... Es>
consteval std::underlying_type_t<E> mask_of(E first, Es... rest) {
    using U = std::underlying_type_t<E>;
    const U list[] = {static_cast<U>(first), static_cast<U>(rest)...};
    U seen = 0;
    for (U v : list) {
        if (!std::has_single_bit(v)) detail::enumerator_is_zero_or_not_single_bit();
        if ((seen & v) != 0) detail::enumerator_bit_is_duplicated();
        seen = static_cast<U>(seen | v);
    }
    return seen;
}

// Part B: scoped enum + unsigned(bool 제외) 기저 타입 + opt-in. 앞 조건이 false 면 뒤는 평가되지 않는다.
template <typename T>
concept FlagEnum = std::is_enum_v<T>
    && (!std::is_convertible_v<T, std::underlying_type_t<T>>)     // enum class 만 (C++23 is_scoped_enum 대용)
    && std::is_unsigned_v<std::underlying_type_t<T>>
    && (!std::is_same_v<std::underlying_type_t<T>, bool>)
    && requires { { flag_traits<T>::valid } -> std::convertible_to<std::underlying_type_t<T>>; };

template <FlagEnum E>
class Flags {
public:
    using enum_type  = E;
    using value_type = std::underlying_type_t<E>;
    static constexpr value_type valid = flag_traits<E>::valid;

    constexpr Flags() = default;                                  // 빈 집합
    constexpr Flags(E e) : bits_(clip(static_cast<value_type>(e))) {}   // 암시적: f | Irq::Rxne 를 위해

    // 레지스터 원시값 → Flags. reserved 비트는 버린다.
    static constexpr Flags from_raw(value_type raw) {
        Flags f;
        f.bits_ = clip(raw);
        return f;
    }
    static constexpr Flags none() { return Flags{}; }
    static constexpr Flags all()  { return from_raw(valid); }

    constexpr value_type raw() const { return bits_; }

    // 비어 있지 않은가. explicit 이라서 if (f & x) 는 되고 int 로의 암시적 변환은 안 된다.
    explicit constexpr operator bool() const { return bits_ != 0; }

    // mask 의 비트가 전부 켜져 있는가 (all-of). test(none()) 은 공허하게 true.
    constexpr bool test(Flags mask) const { return (bits_ & mask.bits_) == mask.bits_; }
    // mask 의 비트가 하나라도 켜져 있는가 (any-of).
    constexpr bool any_of(Flags mask) const { return (bits_ & mask.bits_) != 0; }

    // hidden friend: E 인자가 Flags 로 암시적 변환된다 (템플릿 추론이 아니므로)
    // valid 의 부분집합끼리의 | & ^ 는 부분집합이다 → 다시 clip 하지 않는다 (make).
    friend constexpr Flags operator|(Flags a, Flags b) { return make(static_cast<value_type>(a.bits_ | b.bits_)); }
    friend constexpr Flags operator&(Flags a, Flags b) { return make(static_cast<value_type>(a.bits_ & b.bits_)); }
    friend constexpr Flags operator^(Flags a, Flags b) { return make(static_cast<value_type>(a.bits_ ^ b.bits_)); }
    // ~ 는 valid 안에서의 여집합: reserved 비트를 켜지 않는다.
    friend constexpr Flags operator~(Flags a) { return from_raw(static_cast<value_type>(~a.bits_)); }
    friend constexpr bool operator==(Flags a, Flags b) = default;

    // 집합 차: mask 의 비트를 뺀다. a & ~b 와 결과는 같지만 valid 상수를 쓰지 않는다.
    constexpr Flags without(Flags mask) const { return make(static_cast<value_type>(bits_ & ~mask.bits_)); }

    constexpr Flags& operator|=(Flags o) { bits_ = (*this | o).bits_; return *this; }
    constexpr Flags& operator&=(Flags o) { bits_ = (*this & o).bits_; return *this; }
    constexpr Flags& operator^=(Flags o) { bits_ = (*this ^ o).bits_; return *this; }

    // 레지스터 RMW 용: valid 밖의 비트(다른 필드, reserved)를 보존한다.
    constexpr value_type set_in(value_type reg) const { return static_cast<value_type>(reg | bits_); }
    constexpr value_type clear_in(value_type reg) const { return static_cast<value_type>(reg & ~bits_); }

    // 켜진 플래그마다 낮은 비트부터 fn(E) 호출
    template <typename Fn>
    constexpr void for_each(Fn&& fn) const {
        value_type rest = bits_;
        while (rest != 0) {
            const value_type low = static_cast<value_type>(rest & (value_type{0} - rest));   // 가장 낮은 1비트
            fn(static_cast<E>(low));
            rest = static_cast<value_type>(rest & (rest - 1));
        }
    }

private:
    static constexpr value_type clip(value_type v) { return static_cast<value_type>(v & valid); }
    static constexpr Flags make(value_type v) { Flags f; f.bits_ = v; return f; }   // v 가 이미 valid 안일 때만
    value_type bits_ = 0;
};

// 인자가 둘 다 맨 enum 이면 hidden friend 가 ADL 로 안 보이므로 이 두 개가 필요하다.
template <FlagEnum E> constexpr Flags<E> operator|(E a, E b) { return Flags<E>(a) | Flags<E>(b); }
template <FlagEnum E> constexpr Flags<E> operator~(E a) { return ~Flags<E>(a); }

}  // namespace fw
