// CPP-M03 enum class 플래그 연산자: Flags<E>
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti <this file> -o a && ./a
//
// 목표: enum class 의 타입 안전을 유지하면서 |, &, ~, test() 를 쓴다.
//   - opt-in: flag_traits<E> 를 특수화한 enum 만 Flags 가 된다 (전역 operator| 금지)
//   - 불변식: Flags 안의 비트는 항상 valid 마스크의 부분집합
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ---------------- 라이브러리 ----------------
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
    (void)first; ((void)rest, ...);
    return 0; // TODO(Part B): 전부 OR. 값이 0/2비트 이상/중복이면 위 detail:: 함수를 호출해 상수식을 깨뜨린다
}

// Part B: scoped enum + unsigned(bool 제외) 기저 타입 + opt-in. 앞 조건이 false 면 뒤는 평가되지 않는다.
template <typename T>
concept FlagEnum = std::is_enum_v<T>
    // TODO(Part B): 너무 느슨하다. enum class 만, 기저 타입은 unsigned(단 bool 제외)만 통과시켜라
    && requires { { flag_traits<T>::valid } -> std::convertible_to<std::underlying_type_t<T>>; };

template <FlagEnum E>
class Flags {
public:
    using enum_type  = E;
    using value_type = std::underlying_type_t<E>;
    static constexpr value_type valid = flag_traits<E>::valid;

    constexpr Flags() = default;                                  // 빈 집합
    constexpr Flags(E e) { (void)e; }   // 암시적: f | Irq::Rxne 를 위해 — TODO(Part A): valid 로 잘라서 저장 (clip)

    // 레지스터 원시값 → Flags. reserved 비트는 버린다.
    static constexpr Flags from_raw(value_type raw) {
        (void)raw;
        return Flags{}; // TODO(Part B): reserved 비트를 버리고 저장
    }
    static constexpr Flags none() { return Flags{}; }
    static constexpr Flags all()  { return from_raw(valid); }

    constexpr value_type raw() const { return bits_; }

    // 비어 있지 않은가. explicit 이라서 if (f & x) 는 되고 int 로의 암시적 변환은 안 된다.
    explicit constexpr operator bool() const { return false; } // TODO(Part A)

    // mask 의 비트가 전부 켜져 있는가 (all-of). test(none()) 은 공허하게 true.
    constexpr bool test(Flags mask) const { (void)mask; return false; } // TODO(Part A)
    // mask 의 비트가 하나라도 켜져 있는가 (any-of).
    constexpr bool any_of(Flags mask) const { (void)mask; return false; } // TODO(Part A)

    // hidden friend: E 인자가 Flags 로 암시적 변환된다 (템플릿 추론이 아니므로)
    // valid 의 부분집합끼리의 | & ^ 는 부분집합이다 → 다시 clip 하지 않는다 (make).
    friend constexpr Flags operator|(Flags a, Flags b) { (void)a; (void)b; return Flags{}; } // TODO(Part A)
    friend constexpr Flags operator&(Flags a, Flags b) { (void)a; (void)b; return Flags{}; } // TODO(Part A)
    friend constexpr Flags operator^(Flags a, Flags b) { (void)a; (void)b; return Flags{}; } // TODO(Part A)
    // ~ 는 valid 안에서의 여집합: reserved 비트를 켜지 않는다.
    friend constexpr Flags operator~(Flags a) { (void)a; return Flags{}; } // TODO(Part B)
    friend constexpr bool operator==(Flags a, Flags b) = default;

    // 집합 차: mask 의 비트를 뺀다. a & ~b 와 결과는 같지만 valid 상수를 쓰지 않는다.
    constexpr Flags without(Flags mask) const { (void)mask; return Flags{}; } // TODO(Part B)

    constexpr Flags& operator|=(Flags o) { bits_ = (*this | o).bits_; return *this; }
    constexpr Flags& operator&=(Flags o) { bits_ = (*this & o).bits_; return *this; }
    constexpr Flags& operator^=(Flags o) { bits_ = (*this ^ o).bits_; return *this; }

    // 레지스터 RMW 용: valid 밖의 비트(다른 필드, reserved)를 보존한다.
    constexpr value_type set_in(value_type reg) const { (void)reg; return 0; } // TODO(Part B)
    constexpr value_type clear_in(value_type reg) const { (void)reg; return 0; } // TODO(Part B): ~(*this) 를 쓰면 안 된다

    // 켜진 플래그마다 낮은 비트부터 fn(E) 호출
    template <typename Fn>
    constexpr void for_each(Fn&& fn) const {
        (void)fn; // TODO(Part B): 가장 낮은 1비트는 x & -x, 그 비트 지우기는 x & (x-1)
    }

private:
    static constexpr value_type clip(value_type v) { return static_cast<value_type>(v & valid); }
    static constexpr Flags make(value_type v) { Flags f; f.bits_ = v; return f; }   // v 가 이미 valid 안일 때만
    value_type bits_ = 0;
};

// 인자가 둘 다 맨 enum 이면 hidden friend 가 ADL 로 안 보이므로 이 두 개가 필요하다.
template <FlagEnum E> constexpr Flags<E> operator|(E a, E b) { (void)a; (void)b; return Flags<E>{}; } // TODO(Part A)
template <FlagEnum E> constexpr Flags<E> operator~(E a) { (void)a; return Flags<E>{}; } // TODO(Part B)

}  // namespace fw

// ---------------- 테스트용 enum (드라이버 쪽 네임스페이스) ----------------
namespace hal {
enum class IrqFlag : std::uint32_t { Rxne = 1u << 0, Txe = 1u << 1, Overrun = 1u << 2, FrameErr = 1u << 3, Wake = 1u << 31 };
enum class DmaFlag : std::uint32_t { Tc = 1u << 0, Ht = 1u << 1, Te = 1u << 2 };
enum class Mode    : std::uint32_t { Off, On, Sleep };     // 플래그가 아니다: opt-in 안 함
enum class Tiny    : std::uint8_t  { A = 1u << 0, B = 1u << 1, Top = 1u << 7 };
enum class Wide    : std::uint64_t { Low = 1ull << 0, High = 1ull << 63 };
// 잘못된 enum 들: flag_traits 를 줘도 FlagEnum 이 아니어야 한다
enum class NoWidth { X = 1 };                               // 기저 타입 미지정 → int
enum class Signed8 : std::int8_t { X = 1 };
enum class BoolBased : bool { X = true };
enum Unscoped : std::uint32_t { UX = 1 };

// fw 의 템플릿을 hal 안으로 가져온다: ADL 이 hal 을 뒤질 때 찾게 하려는 것.
using fw::operator|;
using fw::operator~;
}  // namespace hal

template <> struct fw::flag_traits<hal::IrqFlag> {
    static constexpr auto valid = fw::mask_of(hal::IrqFlag::Rxne, hal::IrqFlag::Txe, hal::IrqFlag::Overrun,
                                              hal::IrqFlag::FrameErr, hal::IrqFlag::Wake);
};
template <> struct fw::flag_traits<hal::DmaFlag> {
    static constexpr auto valid = fw::mask_of(hal::DmaFlag::Tc, hal::DmaFlag::Ht, hal::DmaFlag::Te);
};
template <> struct fw::flag_traits<hal::Tiny> {
    static constexpr auto valid = fw::mask_of(hal::Tiny::A, hal::Tiny::B, hal::Tiny::Top);
};
template <> struct fw::flag_traits<hal::Wide> {
    static constexpr auto valid = fw::mask_of(hal::Wide::Low, hal::Wide::High);
};
template <> struct fw::flag_traits<hal::NoWidth>   { static constexpr int valid = 1; };
template <> struct fw::flag_traits<hal::Signed8>   { static constexpr std::int8_t valid = 1; };
template <> struct fw::flag_traits<hal::BoolBased> { static constexpr bool valid = true; };
template <> struct fw::flag_traits<hal::Unscoped>  { static constexpr std::uint32_t valid = 1; };

using IrqSet = fw::Flags<hal::IrqFlag>;
using DmaSet = fw::Flags<hal::DmaFlag>;
using hal::IrqFlag;

// ---------------- Part C: 가짜 UART 인터럽트 드라이버 ----------------
namespace drv {
struct UartRegs {
    volatile std::uint32_t SR;   // 상태: 하드웨어가 set. reserved 비트에 쓰레기가 읽힐 수 있다
    volatile std::uint32_t IER;  // [31],[3:0] 인터럽트 enable / [23:16] FIFO 임계값 (보존해야 함)
    volatile std::uint32_t ICR;  // W1C: 1을 쓴 비트만 SR 에서 지워진다
};
inline UartRegs uart;

inline constexpr IrqSet kErrors = IrqFlag::Overrun | IrqFlag::FrameErr;

// IER 의 다른 필드를 보존하면서 f 만 켠다/끈다 (읽기 1회 + 쓰기 1회)
inline void uart_irq_enable(IrqSet f)  { (void)f; } // TODO(Part C)
inline void uart_irq_disable(IrqSet f) { (void)f; } // TODO(Part C)

// 지금 처리해야 할 것 = 발생했고(SR) 켜져 있는(IER) 플래그
inline IrqSet uart_pending() { return IrqSet{}; } // TODO(Part C)

struct IsrStats {
    unsigned errors = 0, rx = 0, tx = 0, wakes = 0;
    unsigned n = 0;
    IrqFlag order[8] = {};
};

// 에러 묶음은 한 번만 세고, 나머지는 낮은 비트부터 처리. 처리한 플래그를 ICR 에 "한 번의 store" 로 ack 하고 돌려준다.
inline IrqSet uart_isr(IsrStats& s) {
    (void)s;
    return IrqSet{}; // TODO(Part C)
}

// 테스트벤치: ICR 에 쓴 값을 하드웨어처럼 SR 에 반영 (W1C)
inline void hw_apply_icr() {
    uart.SR = uart.SR & ~uart.ICR;
    uart.ICR = 0;
}
}  // namespace drv

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// "이 식이 컴파일되는가?"를 bool 로 얻는다
template <typename A, typename B> concept CanOr  = requires(A a, B b) { a | b; };
template <typename A, typename B> concept CanAnd = requires(A a, B b) { a & b; };
template <typename A, typename B> concept CanEq  = requires(A a, B b) { a == b; };
template <typename A>             concept CanNot = requires(A a) { ~a; };
template <typename A>             concept CanEnable = requires(A a) { drv::uart_irq_enable(a); };
template <typename A>             concept CanMakeFlags = requires { typename fw::Flags<A>; };
template <auto... Es>             concept MaskOk = requires { typename std::bool_constant<(fw::mask_of(Es...), true)>; };

// 반례: operator bool 이 explicit 이 아니면 서로 다른 플래그 타입이 bool → int 로 섞인다
template <typename E> struct BadFlags {
    std::uint32_t v;
    constexpr operator bool() const { return v != 0; }
};

// ---- 컴파일타임 증명 (starter 에서도 성립: 선언만 보고 판정) ----
static_assert(std::is_same_v<decltype(IrqFlag::Rxne | IrqFlag::Txe), IrqSet>);
static_assert(std::is_same_v<decltype(~IrqFlag::Rxne), IrqSet>);
static_assert(CanOr<IrqFlag, IrqFlag> && CanOr<IrqSet, IrqFlag> && CanOr<IrqFlag, IrqSet> && CanOr<IrqSet, IrqSet>);
static_assert(CanAnd<IrqSet, IrqFlag> && CanAnd<IrqFlag, IrqSet>);
static_assert(!CanAnd<IrqFlag, IrqFlag>);                     // 단일 비트끼리 & 는 항상 0 → 일부러 안 줌
static_assert(!CanOr<IrqFlag, hal::DmaFlag>);                 // 다른 enum 과 섞기
static_assert(!CanOr<IrqSet, hal::DmaFlag>);
static_assert(!CanOr<hal::DmaFlag, IrqSet>);
static_assert(!CanOr<IrqSet, DmaSet>);
static_assert(!CanAnd<IrqSet, DmaSet>);
static_assert(!CanEq<IrqSet, DmaSet>);
static_assert(!CanEq<IrqSet, hal::DmaFlag>);
static_assert(!CanOr<hal::Mode, hal::Mode>);                  // opt-in 안 한 enum
static_assert(!CanNot<hal::Mode>);
static_assert(!CanMakeFlags<hal::Mode>);
static_assert(!CanMakeFlags<int>);
static_assert(!CanOr<IrqSet, std::uint32_t> && !CanOr<IrqSet, int>);  // 정수와 섞기
static_assert(!CanAnd<IrqSet, unsigned>);
static_assert(!CanEq<IrqSet, std::uint32_t>);
static_assert(!std::is_convertible_v<IrqSet, std::uint32_t>);
static_assert(!std::is_convertible_v<IrqSet, bool>);          // 암시적 bool 금지
static_assert(std::is_constructible_v<bool, IrqSet>);         // if (f) 는 허용
static_assert(!std::is_convertible_v<std::uint32_t, IrqSet>);
static_assert(std::is_convertible_v<IrqFlag, IrqSet>);
static_assert(!std::is_convertible_v<hal::DmaFlag, IrqSet>);
static_assert(CanOr<BadFlags<IrqFlag>, BadFlags<hal::DmaFlag>>);   // 반례: 컴파일되어 버린다!
static_assert(CanEnable<IrqFlag> && CanEnable<IrqSet>);
static_assert(!CanEnable<hal::DmaFlag> && !CanEnable<std::uint32_t>);
static_assert(sizeof(IrqSet) == sizeof(std::uint32_t) && alignof(IrqSet) == alignof(std::uint32_t));
static_assert(sizeof(fw::Flags<hal::Tiny>) == 1 && sizeof(fw::Flags<hal::Wide>) == 8);
static_assert(std::is_trivially_copyable_v<IrqSet> && std::is_standard_layout_v<IrqSet>);

static std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

int main() {
    using hal::Tiny;
    using hal::Wide;
    using TinySet = fw::Flags<Tiny>;
    using WideSet = fw::Flags<Wide>;
    constexpr std::uint32_t RXNE = 1u << 0, TXE = 1u << 1, OVR = 1u << 2, FE = 1u << 3, WAKE = 1u << 31;
    constexpr std::uint32_t VALID = RXNE | TXE | OVR | FE | WAKE;

    // ---- Part A: 기본 연산 ----
    CHECK(IrqSet{}.raw() == 0u && !IrqSet{} && IrqSet::none().raw() == 0u);
    IrqSet s1 = IrqFlag::Rxne;
    CHECK(s1.raw() == RXNE && static_cast<bool>(s1));
    CHECK((IrqFlag::Rxne | IrqFlag::Txe).raw() == (RXNE | TXE));
    CHECK((s1 | IrqFlag::Overrun).raw() == (RXNE | OVR));
    CHECK((IrqFlag::Overrun | s1).raw() == (RXNE | OVR));
    CHECK(((IrqFlag::Rxne | IrqFlag::Txe) & IrqFlag::Txe).raw() == TXE);
    CHECK((IrqFlag::Txe & (IrqFlag::Rxne | IrqFlag::Txe)).raw() == TXE);
    CHECK(((IrqFlag::Rxne | IrqFlag::Txe) ^ IrqFlag::Txe).raw() == RXNE);
    CHECK(((IrqFlag::Rxne | IrqFlag::Txe) == (IrqFlag::Txe | IrqFlag::Rxne)));
    CHECK((IrqSet{IrqFlag::Txe} == IrqFlag::Txe));
    CHECK((IrqFlag::Txe == IrqSet{IrqFlag::Txe}));
    CHECK(((IrqFlag::Txe | IrqFlag::Rxne) != IrqFlag::Txe));

    IrqSet s2;
    s2 |= IrqFlag::Rxne;
    (s2 |= IrqFlag::Txe) |= IrqFlag::Wake;                    // 대입 연산자는 *this 참조를 돌려준다
    CHECK(s2.raw() == (RXNE | TXE | WAKE));
    s2 &= IrqFlag::Rxne | IrqFlag::Wake;
    CHECK(s2.raw() == (RXNE | WAKE));
    s2 ^= IrqFlag::Rxne | IrqFlag::Txe;
    CHECK(s2.raw() == (TXE | WAKE));

    constexpr IrqSet cx = IrqFlag::Rxne | IrqFlag::Wake;      // 상수식에서 동작 (값은 런타임에 확인)
    CHECK(cx.raw() == (RXNE | WAKE));
    constexpr bool cx_t = cx.test(IrqFlag::Wake);
    CHECK(cx_t);

    // test(): all-of / any_of(): any-of — 표로 경계 확인
    struct TestRow { IrqSet have, mask; bool all, any; };
    const TestRow test_rows[] = {
        {IrqFlag::Rxne | IrqFlag::Txe, IrqFlag::Rxne,                true,  true},
        {IrqFlag::Rxne | IrqFlag::Txe, IrqFlag::Rxne | IrqFlag::Txe, true,  true},
        {IrqFlag::Rxne,                IrqFlag::Rxne | IrqFlag::Txe, false, true},
        {IrqFlag::Rxne,                IrqFlag::Txe,                 false, false},
        {IrqSet{},                     IrqFlag::Rxne,                false, false},
        {IrqFlag::Rxne,                IrqSet{},                     true,  false},   // 빈 마스크: all-of 는 공허하게 참
        {IrqSet{},                     IrqSet{},                     true,  false},
        {IrqFlag::Wake,                IrqFlag::Wake,                true,  true},    // bit 31
    };
    for (const TestRow& r : test_rows) {
        CHECK(r.have.test(r.mask) == r.all);
        CHECK(r.have.any_of(r.mask) == r.any);
        CHECK(static_cast<bool>(r.have & r.mask) == r.any);   // if (f & m) 는 any-of 와 같다
    }

    // 폭 경계: 8 / 64비트
    CHECK((Tiny::A | Tiny::Top).raw() == 0x81u);
    CHECK((TinySet{Tiny::Top} | Tiny::B).raw() == 0x82u);
    CHECK((Wide::Low | Wide::High).raw() == 0x8000'0000'0000'0001ull);
    CHECK((WideSet{Wide::High} & Wide::High).raw() == 0x8000'0000'0000'0000ull);

    // ---- Part B: ~ 는 valid 안의 여집합 ----
    CHECK((~IrqFlag::Rxne).raw() == (VALID & ~RXNE));
    CHECK((~IrqSet::none()) == IrqSet::all());
    CHECK((~IrqSet::all()) == IrqSet::none());
    CHECK(!(~IrqSet::all()));                                 // valid 밖 비트가 켜지면 여기서 걸린다
    CHECK(IrqSet::all().raw() == VALID);
    CHECK((~IrqFlag::Rxne & (IrqFlag::Rxne | IrqFlag::Txe)).raw() == TXE);
    CHECK((~Tiny::A).raw() == 0x82u);                         // uint8_t 의 ~ 는 int 로 승격된다
    CHECK((~TinySet::all()).raw() == 0u);
    CHECK((~Wide::Low).raw() == 0x8000'0000'0000'0000ull);

    // without: 집합 차
    CHECK((IrqSet::all().without(IrqFlag::Rxne)).raw() == (VALID & ~RXNE));
    CHECK((IrqSet{IrqFlag::Txe}.without(IrqFlag::Rxne)) == IrqFlag::Txe);   // 없는 비트를 빼도 그대로
    CHECK((IrqSet::all().without(IrqSet{})) == IrqSet::all());
    CHECK((IrqSet::all().without(IrqSet::all())) == IrqSet{});
    CHECK(TinySet::all().without(Tiny::Top).raw() == 0x03u);
    CHECK(WideSet::all().without(Wide::High).raw() == 1ull);

    // 입력 위생: reserved 비트는 들어오지 못한다
    CHECK(IrqSet::from_raw(0xFFFF'FFFFu) == IrqSet::all());
    CHECK(IrqSet::from_raw(1u << 5).raw() == 0u);
    CHECK(IrqSet::from_raw(RXNE | (1u << 5)).raw() == RXNE);
    CHECK(IrqSet{static_cast<IrqFlag>(1u << 5)}.raw() == 0u);
    CHECK(TinySet::from_raw(0xFFu).raw() == 0x83u);

    // RMW 함정: 마스크된 ~ 로 레지스터를 지우면 valid 밖 비트까지 날아간다
    constexpr std::uint32_t reg = 0x00AB'0000u | RXNE | TXE;
    const IrqSet rx = IrqFlag::Rxne;
    CHECK((reg & (~rx).raw()) == TXE);                        // 함정: 0x00AB0000 이 사라졌다
    CHECK(rx.clear_in(reg) == (0x00AB'0000u | TXE));          // 올바른 방법
    CHECK(rx.set_in(0x00AB'0000u) == (0x00AB'0001u));
    CHECK(TinySet{Tiny::Top}.clear_in(0xFFu) == 0x7Fu);
    CHECK(TinySet{Tiny::Top}.set_in(0x00u) == 0x80u);
    CHECK(WideSet{Wide::High}.clear_in(~0ull) == 0x7FFF'FFFF'FFFF'FFFFull);

    // 대수 법칙: 무작위 입력(고정 시드)으로 uint32_t 기준 모델과 비교
    std::uint32_t seed = 0x1234'ABCDu;
    unsigned model_bad = 0, law_bad = 0;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t ra = xorshift32(seed), rb = xorshift32(seed);
        const IrqSet a = IrqSet::from_raw(ra), b = IrqSet::from_raw(rb);
        const std::uint32_t ma = ra & VALID, mb = rb & VALID;
        if ((a | b).raw() != (ma | mb)) ++model_bad;
        if ((a & b).raw() != (ma & mb)) ++model_bad;
        if ((a ^ b).raw() != (ma ^ mb)) ++model_bad;
        if ((~a).raw() != (~ma & VALID)) ++model_bad;
        if (a.without(b).raw() != (ma & ~mb)) ++model_bad;
        if (a.test(b) != ((ma & mb) == mb)) ++model_bad;
        if (a.any_of(b) != ((ma & mb) != 0)) ++model_bad;
        if (!(~~a == a)) ++law_bad;
        if (!((a | ~a) == IrqSet::all())) ++law_bad;
        if (static_cast<bool>(a & ~a)) ++law_bad;
        if (!(~(a | b) == (~a & ~b))) ++law_bad;              // 드 모르간
        if (!(~(a & b) == (~a | ~b))) ++law_bad;
        if (!(a.without(b) == (a & ~b))) ++law_bad;
    }
    CHECK(model_bad == 0u);
    CHECK(law_bad == 0u);

    // mask_of: 잘못된 열거자 목록은 상수식이 아니게 된다
    CHECK(fw::flag_traits<hal::IrqFlag>::valid == VALID);
    CHECK(fw::flag_traits<hal::Tiny>::valid == 0x83u);
    CHECK((MaskOk<Tiny::A, Tiny::B>));
    CHECK((!MaskOk<static_cast<Tiny>(0)>));                   // 0
    CHECK((!MaskOk<static_cast<Tiny>(3)>));                   // 2비트
    CHECK((!MaskOk<Tiny::A, Tiny::A>));                       // 중복

    // FlagEnum: 잘못된 enum 은 trait 가 있어도 거부
    CHECK(fw::FlagEnum<hal::IrqFlag> && fw::FlagEnum<Tiny> && fw::FlagEnum<Wide>);
    CHECK(!fw::FlagEnum<hal::Mode>);                          // opt-in 안 함
    CHECK(!fw::FlagEnum<int>);
    CHECK(!fw::FlagEnum<hal::NoWidth>);
    CHECK(!fw::FlagEnum<hal::Signed8>);
    CHECK(!fw::FlagEnum<hal::BoolBased>);
    CHECK(!fw::FlagEnum<hal::Unscoped>);

    // for_each: 낮은 비트부터, 정확히 켜진 것만
    struct EachRow { IrqSet in; unsigned n; IrqFlag seq[5]; };
    const EachRow each_rows[] = {
        {IrqSet{}, 0, {}},
        {IrqFlag::Txe, 1, {IrqFlag::Txe}},
        {IrqFlag::Wake | IrqFlag::Rxne, 2, {IrqFlag::Rxne, IrqFlag::Wake}},
        {IrqSet::all(), 5, {IrqFlag::Rxne, IrqFlag::Txe, IrqFlag::Overrun, IrqFlag::FrameErr, IrqFlag::Wake}},
    };
    for (const EachRow& r : each_rows) {
        IrqFlag got[8] = {};
        unsigned n = 0;
        r.in.for_each([&](IrqFlag f) { if (n < 8) got[n] = f; ++n; });
        bool same = (n == r.n);
        for (unsigned i = 0; same && i < n; ++i) same = (got[i] == r.seq[i]);
        CHECK(same);
    }
    unsigned top_count = 0;
    WideSet{Wide::High}.for_each([&](Wide w) { top_count += (w == Wide::High); });
    CHECK(top_count == 1u);
    unsigned tiny_count = 0;
    TinySet::all().for_each([&](Tiny) { ++tiny_count; });
    CHECK(tiny_count == 3u);

    // ---- Part C: UART 인터럽트 드라이버 ----
    using namespace drv;
    uart.IER = 0x00AB'0000u;                                  // FIFO 임계값 필드
    uart_irq_enable(IrqFlag::Rxne | IrqFlag::Overrun);
    CHECK(uart.IER == (0x00AB'0000u | RXNE | OVR));
    uart_irq_enable(IrqFlag::Txe);
    CHECK(uart.IER == (0x00AB'0000u | RXNE | OVR | TXE));
    uart_irq_disable(IrqFlag::Rxne | IrqFlag::Txe);
    CHECK(uart.IER == (0x00AB'0000u | OVR));

    constexpr std::uint32_t RSV = 1u << 5;                    // reserved 비트
    struct IsrRow {
        std::uint32_t sr, ier;
        std::uint32_t handled;
        unsigned errors, rx, tx, wakes;
        std::uint32_t sr_after;
    };
    const IsrRow isr_rows[] = {
        {0,                 VALID,        0,                 0, 0, 0, 0, 0},
        {RXNE,              RXNE,         RXNE,              0, 1, 0, 0, 0},
        {RXNE | TXE,        TXE,          TXE,               0, 0, 1, 0, RXNE},        // 꺼진 플래그는 남겨 둔다
        {OVR | FE | RXNE,   VALID,        OVR | FE | RXNE,   1, 1, 0, 0, 0},           // 에러 2개 → 묶어서 1번
        {OVR,               VALID,        OVR,               1, 0, 0, 0, 0},
        {WAKE | RSV,        WAKE,         WAKE,              0, 0, 0, 1, RSV},         // bit 31 + reserved 쓰레기
        {RSV | (1u << 30),  0xFFFF'FFFFu, 0,                 0, 0, 0, 0, RSV | (1u << 30)},
        {RXNE | TXE | WAKE, VALID,        RXNE | TXE | WAKE, 0, 1, 1, 1, 0},
    };
    for (const IsrRow& r : isr_rows) {
        uart.SR = r.sr; uart.IER = r.ier; uart.ICR = 0;
        IsrStats st;
        const IrqSet done = uart_isr(st);
        CHECK(done.raw() == r.handled);
        CHECK(uart.ICR == r.handled);                         // W1C: 처리한 비트만
        CHECK(st.errors == r.errors && st.rx == r.rx && st.tx == r.tx && st.wakes == r.wakes);
        hw_apply_icr();
        CHECK(uart.SR == r.sr_after);
    }
    uart.SR = RXNE | TXE | WAKE; uart.IER = VALID; uart.ICR = 0;
    IsrStats ord;
    (void)uart_isr(ord);
    CHECK(ord.n == 3u && ord.order[0] == IrqFlag::Rxne && ord.order[1] == IrqFlag::Txe && ord.order[2] == IrqFlag::Wake);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
