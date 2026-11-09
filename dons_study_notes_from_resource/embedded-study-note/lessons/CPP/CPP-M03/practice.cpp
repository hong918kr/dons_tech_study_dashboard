// CPP-M03 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace fw {

namespace detail {
void enumerator_is_zero_or_not_single_bit();
void enumerator_bit_is_duplicated();
}  // namespace detail

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

// 연습 1: trait 특수화 대신 ADL 훅. enum 과 같은 네임스페이스에 flag_valid_mask(E) 를 두면 opt-in.
template <typename T>
concept FlagEnum = std::is_enum_v<T>
    && (!std::is_convertible_v<T, std::underlying_type_t<T>>)
    && std::is_unsigned_v<std::underlying_type_t<T>>
    && (!std::is_same_v<std::underlying_type_t<T>, bool>)
    && requires(T t) { { flag_valid_mask(t) } -> std::convertible_to<std::underlying_type_t<T>>; };

template <FlagEnum E>
class Flags {
public:
    using enum_type  = E;
    using value_type = std::underlying_type_t<E>;
    static constexpr value_type valid = flag_valid_mask(E{});

    constexpr Flags() = default;
    constexpr Flags(E e) : bits_(clip(static_cast<value_type>(e))) {}
    static constexpr Flags from_raw(value_type raw) { return make(clip(raw)); }
    static constexpr Flags none() { return Flags{}; }
    static constexpr Flags all()  { return make(valid); }
    constexpr value_type raw() const { return bits_; }
    explicit constexpr operator bool() const { return bits_ != 0; }
    constexpr bool test(Flags m) const { return (bits_ & m.bits_) == m.bits_; }

    friend constexpr Flags operator|(Flags a, Flags b) { return make(static_cast<value_type>(a.bits_ | b.bits_)); }
    friend constexpr Flags operator&(Flags a, Flags b) { return make(static_cast<value_type>(a.bits_ & b.bits_)); }
    friend constexpr Flags operator~(Flags a) { return from_raw(static_cast<value_type>(~a.bits_)); }
    friend constexpr bool operator==(Flags a, Flags b) = default;
    constexpr Flags& operator|=(Flags o) { bits_ = (*this | o).bits_; return *this; }
    constexpr Flags without(Flags m) const { return make(static_cast<value_type>(bits_ & ~m.bits_)); }

private:
    static constexpr value_type clip(value_type v) { return static_cast<value_type>(v & valid); }
    static constexpr Flags make(value_type v) { Flags f; f.bits_ = v; return f; }
    value_type bits_ = 0;
};

template <FlagEnum E> constexpr Flags<E> operator|(E a, E b) { return Flags<E>(a) | Flags<E>(b); }
template <FlagEnum E> constexpr Flags<E> operator~(E a) { return ~Flags<E>(a); }

// 연습 2: range-for. Flags 에 멤버 begin/end 가 없으므로 ADL 로 찾는 자유 함수.
template <FlagEnum E>
class FlagIter {
public:
    using U = std::underlying_type_t<E>;
    constexpr explicit FlagIter(U rest) : rest_(rest) {}
    constexpr E operator*() const { return static_cast<E>(static_cast<U>(rest_ & (U{0} - rest_))); }
    constexpr FlagIter& operator++() { rest_ = static_cast<U>(rest_ & (rest_ - 1)); return *this; }
    constexpr bool operator==(const FlagIter&) const = default;
private:
    U rest_;
};
template <FlagEnum E> constexpr FlagIter<E> begin(Flags<E> f) { return FlagIter<E>(f.raw()); }
template <FlagEnum E> constexpr FlagIter<E> end(Flags<E>)     { return FlagIter<E>(0); }

// 연습 3: 이름 표 + bounded 포맷터. snprintf 처럼 "필요한 길이"를 돌려주고 항상 NUL 종단한다.
template <FlagEnum E> struct FlagName { E flag; const char* name; };

template <FlagEnum E, std::size_t N>
std::size_t format_flags(Flags<E> f, const FlagName<E> (&names)[N], char* buf, std::size_t cap) {
    std::size_t len = 0;
    const auto put = [&](const char* s) {
        for (; *s != '\0'; ++s, ++len)
            if (cap != 0 && len + 1 < cap) buf[len] = *s;
    };
    if (!f) {
        put("none");
    } else {
        bool first = true;
        for (const FlagName<E>& row : names) {
            if (!f.test(row.flag)) continue;
            if (!first) put("|");
            put(row.name);
            first = false;
        }
    }
    if (cap != 0) buf[len + 1 < cap ? len : cap - 1] = '\0';
    return len;
}

// 연습 4: 다른 비트 배치(상태 → enable)로 번역
template <FlagEnum From, FlagEnum To> struct FlagMap { From from; To to; };

template <FlagEnum From, FlagEnum To, std::size_t N>
constexpr Flags<To> translate(Flags<From> in, const FlagMap<From, To> (&table)[N]) {
    Flags<To> out;
    for (const auto& row : table)
        if (in.test(row.from)) out |= row.to;
    return out;
}

// 연습 5: ISR ↔ 메인 공유용. Flags 자체의 |= 는 원자적이지 않다.
template <FlagEnum E>
class AtomicFlags {
public:
    using Set = Flags<E>;
    void set(Set f)       { bits_.fetch_or(f.raw()); }
    void clear(Set f)     { bits_.fetch_and(static_cast<typename Set::value_type>(~f.raw())); }
    Set  load() const     { return Set::from_raw(bits_.load()); }
    Set  take()           { return Set::from_raw(bits_.exchange(0)); }   // 읽고 비우기를 한 번에
private:
    std::atomic<typename Set::value_type> bits_{0};
};

}  // namespace fw

// 연습 1 의 답: 한 줄 opt-in. enum 의 네임스페이스 안에 쓴다.
#define FW_FLAGS(E, ...)                                                                          \
    consteval std::underlying_type_t<E> flag_valid_mask(E) { return ::fw::mask_of(__VA_ARGS__); } \
    using ::fw::operator|;                                                                        \
    using ::fw::operator~

namespace board {
enum class Led : std::uint16_t { Red = 1u << 0, Green = 1u << 1, Blue = 1u << 15 };
FW_FLAGS(Led, Led::Red, Led::Green, Led::Blue);
enum class Btn : std::uint8_t { Up = 1u << 0, Down = 1u << 1 };
FW_FLAGS(Btn, Btn::Up, Btn::Down);                        // using 중복은 네임스페이스에서는 허용
enum class Plain : std::uint32_t { A = 1, B = 2 };        // opt-in 안 함
}  // namespace board

namespace uart {
enum class Status : std::uint32_t { Rxne = 1u << 5, Txe = 1u << 7, Ore = 1u << 3, Idle = 1u << 4 };
FW_FLAGS(Status, Status::Rxne, Status::Txe, Status::Ore, Status::Idle);
enum class IntEn : std::uint32_t { RxneIe = 1u << 0, TxeIe = 1u << 1, ErrIe = 1u << 2, IdleIe = 1u << 3 };
FW_FLAGS(IntEn, IntEn::RxneIe, IntEn::TxeIe, IntEn::ErrIe, IntEn::IdleIe);
}  // namespace uart

using board::Led; using board::Btn; using board::Plain;
using LedSet = fw::Flags<Led>;
using uart::Status; using uart::IntEn;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

template <typename A, typename B> concept CanOr = requires(A a, B b) { a | b; };
template <typename A, typename B> concept CanEq = requires(A a, B b) { a == b; };

// 연습 1: 컴파일타임 증명
static_assert(fw::FlagEnum<Led> && fw::FlagEnum<Btn> && fw::FlagEnum<Status>);
static_assert(!fw::FlagEnum<Plain> && !fw::FlagEnum<int>);
static_assert(CanOr<Led, Led> && CanOr<LedSet, Led>);
static_assert(!CanOr<Led, Btn> && !CanOr<LedSet, Btn> && !CanOr<Plain, Plain>);
static_assert(std::is_same_v<decltype(Led::Red | Led::Blue), LedSet>);

constexpr unsigned count_leds(LedSet s) { unsigned n = 0; for (Led l : s) { (void)l; ++n; } return n; }
static_assert(count_leds(Led::Red | Led::Blue) == 2);        // range-for 가 상수식에서 동작

int main() {
    // 연습 1: ADL 훅으로 opt-in
    CHECK(LedSet::valid == 0x8003u);
    CHECK((Led::Red | Led::Blue).raw() == 0x8001u);
    CHECK((~Led::Red).raw() == 0x8002u);                      // uint16_t 도 정상
    CHECK((~LedSet::all()) == LedSet::none());
    CHECK(fw::Flags<Btn>::valid == 0x03u);

    // 연습 2: range-for 순서와 경계
    Led seen[4] = {};
    unsigned n = 0;
    for (Led l : Led::Blue | Led::Red | Led::Green) if (n < 4) seen[n++] = l;
    CHECK(n == 3 && seen[0] == Led::Red && seen[1] == Led::Green && seen[2] == Led::Blue);
    unsigned none_n = 0;
    for (Led l : LedSet{}) { (void)l; ++none_n; }
    CHECK(none_n == 0u);
    CHECK(count_leds(LedSet::all()) == 3u);

    // 연습 3: 포맷터 — 정상, 비어 있음, 경계(정확히 맞음 / 한 칸 부족 / cap 0)
    constexpr fw::FlagName<Led> names[] = {{Led::Red, "RED"}, {Led::Green, "GREEN"}, {Led::Blue, "BLUE"}};
    char buf[32];
    CHECK(format_flags(Led::Red | Led::Blue, names, buf, sizeof buf) == 8u && std::strcmp(buf, "RED|BLUE") == 0);
    CHECK(format_flags(LedSet{}, names, buf, sizeof buf) == 4u && std::strcmp(buf, "none") == 0);
    CHECK(format_flags(LedSet::all(), names, buf, sizeof buf) == 14u && std::strcmp(buf, "RED|GREEN|BLUE") == 0);
    char exact[9];
    CHECK(format_flags(Led::Red | Led::Blue, names, exact, sizeof exact) == 8u && std::strcmp(exact, "RED|BLUE") == 0);
    char small[8];
    std::memset(small, 'X', sizeof small);
    CHECK(format_flags(Led::Red | Led::Blue, names, small, sizeof small) == 8u && std::strcmp(small, "RED|BLU") == 0);
    char guard[2] = {'A', 'B'};
    CHECK(format_flags(LedSet{Led::Red}, names, guard, 0) == 3u && guard[0] == 'A' && guard[1] == 'B');   // cap 0: 안 건드림
    char one[1] = {'Z'};
    CHECK(format_flags(LedSet{Led::Red}, names, one, 1) == 3u && one[0] == '\0');

    // 연습 4: 상태 비트 → enable 비트 번역
    constexpr fw::FlagMap<Status, IntEn> status_to_ie[] = {
        {Status::Rxne, IntEn::RxneIe}, {Status::Txe, IntEn::TxeIe}, {Status::Ore, IntEn::ErrIe}, {Status::Idle, IntEn::IdleIe},
    };
    constexpr auto ie = translate(Status::Rxne | Status::Ore, status_to_ie);
    static_assert(ie.raw() == 0x5u);
    CHECK(ie == (IntEn::RxneIe | IntEn::ErrIe));
    CHECK(translate(fw::Flags<Status>{}, status_to_ie) == fw::Flags<IntEn>{});
    CHECK(translate(fw::Flags<Status>::all(), status_to_ie).raw() == 0xFu);
    static_assert(!CanEq<fw::Flags<Status>, fw::Flags<IntEn>>);

    // 연습 5: AtomicFlags 기본 동작
    fw::AtomicFlags<Led> pending;
    pending.set(Led::Red);
    pending.set(Led::Blue);
    CHECK(pending.load() == (Led::Red | Led::Blue));
    pending.clear(Led::Red);
    CHECK(pending.load() == Led::Blue);
    CHECK(pending.take() == Led::Blue);
    CHECK(pending.take() == LedSet{} && !pending.load());

    // 연습 5: 왜 필요한가 — 메인의 RMW 사이에 ISR 이 끼어드는 상황을 순서대로 재현 (진짜 스레드 없음)
    std::uint32_t shared = 0;
    const std::uint32_t tmp = shared;                         // 메인: 읽기
    shared |= 2u;                                             // ISR: 플래그 set (여기서 끼어든다)
    shared = tmp | 1u;                                        // 메인: 쓰기 — ISR 의 비트가 사라진다
    CHECK(shared == 1u);                                      // lost update 재현
    fw::AtomicFlags<Led> ok;
    ok.set(Led::Red);                                         // fetch_or: 읽기-쓰기가 한 덩어리
    ok.set(Led::Green);
    CHECK(ok.load() == (Led::Red | Led::Green));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
