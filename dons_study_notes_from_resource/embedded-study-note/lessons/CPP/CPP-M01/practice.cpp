// CPP-M01 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 4);

// 연습 1: can_rmw 를 태그에 추가. W1C 는 읽기·쓰기 다 되지만 read-modify-write 는 금지
struct RO  { static constexpr bool can_read = true,  can_write = false, can_rmw = false; };
struct WO  { static constexpr bool can_read = false, can_write = true,  can_rmw = false; };
struct RW  { static constexpr bool can_read = true,  can_write = true,  can_rmw = true;  };
struct W1C { static constexpr bool can_read = true,  can_write = true,  can_rmw = false; };

// 연습 5 까지 쓰는 간단한 Mock. Id 가 다르면 static 상태도 다르다.
template <int Id>
struct MockBusT {
    static constexpr std::uintptr_t BASE = 0x4000'0000u;
    static constexpr std::size_t    SIZE = 0x40;
    alignas(8) static inline std::uint8_t mem[SIZE] = {};
    static inline unsigned reads = 0, writes = 0;

    static void reset() { std::memset(mem, 0, SIZE); reads = writes = 0; }
    template <RegWord T> [[nodiscard]] static T read(std::uintptr_t a) {
        T v; std::memcpy(&v, &mem[a - BASE], sizeof v); ++reads; return v;
    }
    template <RegWord T> static void write(std::uintptr_t a, T v) {
        std::memcpy(&mem[a - BASE], &v, sizeof v); ++writes;
    }
    template <RegWord T> static void poke(std::uintptr_t a, T v) { std::memcpy(&mem[a - BASE], &v, sizeof v); }
    template <RegWord T> static T    peek(std::uintptr_t a)      { T v; std::memcpy(&v, &mem[a - BASE], sizeof v); return v; }
};
using Bus0 = MockBusT<0>;
using Bus1 = MockBusT<1>;

template <std::uintptr_t Addr, typename T, typename A, typename Bus>
    requires RegWord<T> && (Addr % sizeof(T) == 0)
struct Reg {
    using value_type = T;
    using access     = A;
    using bus        = Bus;
    static constexpr std::uintptr_t address = Addr;
    static constexpr std::size_t    width   = sizeof(T);

    [[nodiscard]] static T read() noexcept requires A::can_read { return Bus::template read<T>(Addr); }
    static void write(T v) noexcept requires A::can_write { Bus::template write<T>(Addr, v); }
    static void modify(T clr, T set) noexcept requires A::can_rmw {
        const T cur = read();
        write(static_cast<T>((cur & static_cast<T>(~clr)) | set));
    }
};

// 연습 2: 권한 낮추기 — 같은 주소·폭·버스, 권한만 RO
template <typename R>
using ReadOnly = Reg<R::address, typename R::value_type, RO, typename R::bus>;

// 연습 3: 등간격 레지스터 배열 (DMA 채널, GPIO 포트, 타이머 CCR)
template <std::uintptr_t Base, std::size_t Stride, std::size_t N, typename T, typename A, typename Bus>
struct RegArray {
    static constexpr std::size_t size = N;
    template <std::size_t I> requires (I < N)
    using at = Reg<Base + I * Stride, T, A, Bus>;
};

// 연습 4: 리셋값 스윕 — 쓰기 전용 레지스터는 절대 읽지 않는다
template <typename R, typename R::value_type V>
struct Expect {
    using reg = R;
    static constexpr typename R::value_type value = V;
};
template <typename E>
unsigned mismatch_one() {
    if constexpr (E::reg::access::can_read) {
        return E::reg::read() != E::value ? 1u : 0u;
    } else {
        return 0u;
    }
}
template <typename... Es>
unsigned count_mismatches() {
    return (0u + ... + mismatch_one<Es>());
}

// 연습 5: 컴파일 타임에 레지스터 겹침 검사
struct Span_ { std::uintptr_t lo, hi; };  // [lo, hi)
template <typename... Rs>
consteval bool disjoint() {
    const std::array<Span_, sizeof...(Rs)> s{Span_{Rs::address, Rs::address + Rs::width}...};
    for (std::size_t i = 0; i < s.size(); ++i)
        for (std::size_t j = i + 1; j < s.size(); ++j)
            if (s[i].lo < s[j].hi && s[j].lo < s[i].hi) return false;
    return true;
}

// ---------------- 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

template <typename R> concept CanRead   = requires { R::read(); };
template <typename R> concept CanWrite  = requires(typename R::value_type v) { R::write(v); };
template <typename R> concept CanModify = requires(typename R::value_type v) { R::modify(v, v); };
template <typename Arr, std::size_t I> constexpr bool has_index = requires { typename Arr::template at<I>; };

constexpr std::uintptr_t B = Bus0::BASE;
using Flags = Reg<B + 0x00, std::uint32_t, W1C, Bus0>;
using Ctrl  = Reg<B + 0x04, std::uint32_t, RW,  Bus0>;
using Id    = Reg<B + 0x08, std::uint32_t, RO,  Bus0>;
using Tx    = Reg<B + 0x0C, std::uint32_t, WO,  Bus0>;
using Byte0 = Reg<B + 0x10, std::uint8_t,  RW,  Bus0>;
using Byte1 = Reg<B + 0x11, std::uint8_t,  RW,  Bus0>;
using Half  = Reg<B + 0x10, std::uint16_t, RW,  Bus0>;   // Byte0/Byte1 과 겹침

using Ch = RegArray<B + 0x20, 0x08, 4, std::uint32_t, RW, Bus0>;

static_assert(disjoint<Flags, Ctrl, Id, Tx, Byte0, Byte1>());
static_assert(!disjoint<Byte0, Half>());
static_assert(!disjoint<Byte1, Half>());

int main() {
    // 연습 1 — W1C
    CHECK(CanRead<Flags> && CanWrite<Flags> && !CanModify<Flags>);
    CHECK(CanModify<Ctrl> && !CanModify<Id> && !CanModify<Tx>);
    Bus0::reset();
    Bus0::poke<std::uint32_t>(Flags::address, 0x0000'0005u);
    Flags::write(0x4u);                                       // 하드웨어라면 비트2만 지워진다 (여기선 값 기록만 확인)
    CHECK(Bus0::writes == 1 && Bus0::reads == 0);             // 읽지 않고 곧바로 쓰기 1번

    // 연습 2 — 권한 낮추기
    using CtrlView = ReadOnly<Ctrl>;
    CHECK(CanRead<CtrlView> && !CanWrite<CtrlView> && !CanModify<CtrlView>);
    CHECK(CtrlView::address == Ctrl::address);
    Bus0::reset();
    Ctrl::write(0xABCDu);
    CHECK(CtrlView::read() == 0xABCDu);

    // 연습 3 — 배열
    CHECK(Ch::at<0>::address == B + 0x20);
    CHECK(Ch::at<3>::address == B + 0x38);
    CHECK((has_index<Ch, 3>));
    CHECK(!(has_index<Ch, 4>));                               // 범위 밖은 이름조차 못 짓는다
    Bus0::reset();
    Ch::at<1>::write(0x11u);
    Ch::at<2>::write(0x22u);
    CHECK(Bus0::peek<std::uint32_t>(B + 0x28) == 0x11u && Bus0::peek<std::uint32_t>(B + 0x30) == 0x22u);
    CHECK(Ch::at<0>::read() == 0u);

    // 연습 4 — 리셋값 스윕
    Bus0::reset();
    Bus0::poke<std::uint32_t>(Id::address, 0x1234'0001u);
    Bus0::poke<std::uint32_t>(Ctrl::address, 0x0u);
    {
        using Sweep = unsigned (*)();
        Sweep ok = count_mismatches<Expect<Id, 0x1234'0001u>, Expect<Ctrl, 0u>, Expect<Tx, 0xFFFF'FFFFu>>;
        CHECK(ok() == 0u);
        CHECK(Bus0::reads == 2);                              // Tx(WO)는 읽지 않았다
        Sweep bad = count_mismatches<Expect<Id, 0xDEADu>, Expect<Ctrl, 1u>>;
        CHECK(bad() == 2u);
    }

    // 연습 5 — 겹침 검사 (static_assert 는 위에서 이미 통과했고, 값도 찍어 본다)
    CHECK((disjoint<Flags, Ctrl>()));
    CHECK(!(disjoint<Byte0, Half>()));

    // Tag 로 분리된 두 Mock: 서로 간섭이 없다
    using A = Reg<Bus0::BASE, std::uint32_t, RW, Bus0>;
    using C = Reg<Bus1::BASE, std::uint32_t, RW, Bus1>;
    Bus0::reset(); Bus1::reset();
    A::write(0x1111u);
    C::write(0x2222u);
    CHECK(A::read() == 0x1111u && C::read() == 0x2222u);
    CHECK(A::address == C::address);                          // 주소는 같지만 타입(버스)이 다르다
    CHECK(!(std::is_same_v<A, C>));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
