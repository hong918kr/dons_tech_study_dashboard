// CPP-M02 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(...) do { ++g_total; if (__VA_ARGS__) { ++g_pass; std::printf("PASS %s\n", #__VA_ARGS__); } \
                        else std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } while (0)

// mock: 본문의 mock 에 rc_w0(0 을 쓰면 지워짐, 1 을 쓰면 무변화) 규칙을 추가했다
namespace mock {
using u32 = std::uint32_t;
inline constexpr std::uintptr_t BASE = 0x4002'0000u;
struct HwReg { u32 value, w1c, w0c, res0; };
inline std::array<HwReg, 8> regs{};
inline unsigned rd = 0, wr = 0;
inline void (*after_read)() = nullptr;
inline HwReg& at(std::uintptr_t a) { return regs[(a - BASE) / 4]; }
inline u32 read(std::uintptr_t a) { ++rd; const u32 v = at(a).value; if (after_read) after_read(); return v; }
inline void write(std::uintptr_t a, u32 v) {
    ++wr;
    HwReg& r = at(a);
    const u32 plain = ~(r.w1c | r.w0c | r.res0);
    r.value = (r.value & r.w1c & ~v)   // w1c: 1 을 쓰면 지워진다
            | (r.value & r.w0c & v)    // w0c: 0 을 쓰면 지워진다
            | (v & plain);
}
inline void reset() { regs = {}; rd = wr = 0; after_read = nullptr; }
}  // namespace mock

// 본문 solution 의 축약판 (이 파일만으로 빌드되도록)
namespace hw {
template <std::uintptr_t Addr, std::unsigned_integral T = std::uint32_t, T W1C = 0, T Res0 = 0, T W0C = 0>
struct Reg {
    using word = T;
    static constexpr std::uintptr_t addr = Addr;
    static constexpr T w1c = W1C, res0 = Res0, w0c = W0C;
    static T read() { return static_cast<T>(mock::read(Addr)); }
    static void write(T v) { mock::write(Addr, v); }
};
template <typename W>
constexpr W low_mask(unsigned width) {
    if (width >= static_cast<unsigned>(std::numeric_limits<W>::digits)) return static_cast<W>(~W{0});
    return static_cast<W>((W{1} << width) - 1u);
}
template <typename F> struct FieldValue { using field = F; typename F::word bits; };

template <typename R, unsigned Offset, unsigned Width>
struct Field {
    using reg = R;
    using word = typename R::word;
    static constexpr unsigned offset = Offset, width = Width;
    static constexpr word max = low_mask<word>(Width);
    static constexpr word mask = static_cast<word>(max << Offset);
    static constexpr word decode(word raw) { return static_cast<word>((raw & mask) >> Offset); }
    static word read() { return decode(R::read()); }
    [[nodiscard]] static constexpr FieldValue<Field> wrap(word v) { return {static_cast<word>((v & max) << Offset)}; }
};

// ---------------- 연습 1: overwrite — 읽지 않고 레지스터 전체를 한 번에 쓰기 ----------------
// base 를 출발점으로 필드들을 끼워 넣는다. W1C·W0C·res0 는 안전한 값으로 강제:
//   w1c 는 0 (아무것도 안 지움), w0c 는 1 (아무것도 안 지움), res0 은 0.
template <typename First, typename... Rest>
    requires (std::same_as<typename First::field::reg, typename Rest::field::reg> && ...)
void overwrite(typename First::field::word base, First first, Rest... rest) {
    using R = typename First::field::reg;
    using W = typename R::word;
    const W mask = static_cast<W>(First::field::mask | (W{0} | ... | Rest::field::mask));
    W v = static_cast<W>(base & ~(mask | R::w1c | R::res0 | R::w0c));
    v = static_cast<W>(v | R::w0c | first.bits | (W{0} | ... | rest.bits));
    R::write(v);
}

// ---------------- 연습 2: Snapshot — 한 번 읽고 여러 필드 해석 ----------------
template <typename R>
class Snapshot {
    typename R::word raw_;
public:
    Snapshot() : raw_(R::read()) {}
    template <typename F> requires std::same_as<typename F::reg, R>
    typename F::word get(F) const { return F::decode(raw_); }
};

// ---------------- 연습 3: 부호 있는 필드 (2의 보수) ----------------
template <typename R, unsigned Offset, unsigned Width>
struct SignedField {
    using word = typename R::word;
    static constexpr word max_raw = low_mask<word>(Width);
    static constexpr word mask = static_cast<word>(max_raw << Offset);
    static constexpr std::int64_t lo = -(std::int64_t{1} << (Width - 1));
    static constexpr std::int64_t hi = (std::int64_t{1} << (Width - 1)) - 1;
    static constexpr bool fits(std::int64_t v) { return v >= lo && v <= hi; }
    static constexpr std::int64_t decode(word raw) {
        const word f = static_cast<word>((raw & mask) >> Offset);
        const word sign = static_cast<word>(word{1} << (Width - 1));
        // (f ^ sign) - sign : 부호 비트를 뒤집고 빼면 음수가 복원된다 (unsigned 라 넘침 없음 → int64 로 계산)
        return static_cast<std::int64_t>(f ^ sign) - static_cast<std::int64_t>(sign);
    }
    static constexpr word encode(std::int64_t v) {
        return static_cast<word>((static_cast<word>(v) & max_raw) << Offset);
    }
};

// ---------------- 연습 4: 데이터시트의 마스크에서 필드 만들기 ----------------
template <std::unsigned_integral W>
constexpr bool contiguous(W m) {
    if (m == 0) return false;
    const W shifted = static_cast<W>(m >> std::countr_zero(m));
    return static_cast<W>(shifted & static_cast<W>(shifted + 1u)) == 0;
}
template <typename R, typename R::word Mask>
    requires (contiguous<typename R::word>(Mask))
using FieldFromMask = Field<R, static_cast<unsigned>(std::countr_zero(Mask)), static_cast<unsigned>(std::popcount(Mask))>;

// ---------------- 연습 5: rc_w0 플래그 지우기 ----------------
// 틀린 방법: RMW. 읽은 뒤 생긴 플래그를 0 으로 되써서 지워 버린다.
template <typename R> void clear_w0_naive(typename R::word flag_mask) {
    R::write(static_cast<typename R::word>(R::read() & ~flag_mask));
}
// 맞는 방법: 읽지 않는다. 지울 비트만 0, 다른 rc_w0 비트는 1 (무변화), res0 은 0.
template <typename R> void clear_w0(typename R::word flag_mask) {
    R::write(static_cast<typename R::word>(R::w0c & ~flag_mask));
}
}  // namespace hw

using u32 = std::uint32_t;
using R0 = hw::Reg<mock::BASE + 0x00>;
using RMix = hw::Reg<mock::BASE + 0x04, u32, 0x0000'0F00u, 0xFFFF'0000u>;
using RW0 = hw::Reg<mock::BASE + 0x08, u32, 0u, 0xFFFF'FFF0u, 0x0000'000Fu>;   // bit3:0 rc_w0, 나머지 예약
using F1 = hw::Field<R0, 0, 4>;
using F2 = hw::Field<R0, 8, 8>;
using M1 = hw::Field<RMix, 0, 4>;
using OtherF = hw::Field<RMix, 4, 4>;

template <typename F> concept CanSnapOnR0 = requires(const hw::Snapshot<R0> s, F f) { s.get(f); };
template <typename R, auto Mask> concept MaskOk = requires { typename hw::FieldFromMask<R, Mask>; };
template <typename A, typename B> concept CanOverwrite = requires(u32 b, A x, B y) { hw::overwrite(b, x, y); };

int main() {
    using mock::regs;

    // 연습 1: overwrite — 읽기 0, 쓰기 1, 위험 비트는 안전 값
    mock::reset();
    regs[0].value = 0xFFFF'FFFFu;
    hw::overwrite(0x0000'0000u, F1::wrap(0x5), F2::wrap(0xA7));
    CHECK(regs[0].value == 0x0000'A705u);
    CHECK(mock::rd == 0 && mock::wr == 1);
    mock::reset();
    hw::overwrite(0x1200'00F0u, F1::wrap(0x3));                    // base 의 다른 비트는 그대로
    CHECK(regs[0].value == 0x1200'00F3u);
    mock::reset();
    regs[1].w1c = 0x0F00u;
    regs[1].res0 = 0xFFFF'0000u;
    regs[1].value = 0x0F00u;                                       // W1C 플래그 pending
    hw::overwrite(0xFFFF'FFFFu, M1::wrap(0x2));                    // base 가 전부 1 이어도
    CHECK(regs[1].value == 0x0000'FFF2u);                          // 플래그 0xF00 유지, 예약 비트 안 켜짐, bit15:12·7:4 는 base 값
    CHECK(mock::rd == 0 && mock::wr == 1);
    CHECK(CanOverwrite<hw::FieldValue<F1>, hw::FieldValue<F2>>);
    CHECK(!CanOverwrite<hw::FieldValue<F1>, hw::FieldValue<M1>>);  // 다른 레지스터

    // 연습 2: Snapshot — 읽기 1번으로 일관된 값
    mock::reset();
    regs[0].value = 0x0000'C3A5u;
    {
        const hw::Snapshot<R0> s;
        regs[0].value = 0;                                         // 스냅샷 이후의 변화는 보이지 않는다
        CHECK(s.get(F1{}) == 0x5u);
        CHECK(s.get(F2{}) == 0xC3u);
        CHECK(mock::rd == 1);
    }
    CHECK(CanSnapOnR0<F1>);
    CHECK(!CanSnapOnR0<M1>);                                       // 다른 레지스터의 필드

    // 연습 3: 부호 있는 필드 — 경계값 표
    using S4 = hw::SignedField<R0, 4, 4>;
    using S12 = hw::SignedField<R0, 0, 12>;
    using S32 = hw::SignedField<R0, 0, 32>;
    CHECK(S4::lo == -8 && S4::hi == 7);
    CHECK(S4::fits(-8) && S4::fits(7) && !S4::fits(-9) && !S4::fits(8));
    {
        struct Row { std::int64_t v; u32 raw; };
        const Row t[] = {{0, 0x00u}, {1, 0x10u}, {7, 0x70u}, {-1, 0xF0u}, {-8, 0x80u}};
        for (const Row& r : t) CHECK(S4::encode(r.v) == r.raw && S4::decode(r.raw) == r.v);
    }
    CHECK(S4::decode(0xFFFF'FF7Fu) == 7);                          // 옆 비트는 무시
    CHECK(S12::decode(0x800u) == -2048 && S12::decode(0x7FFu) == 2047);
    CHECK(S32::decode(0x8000'0000u) == INT64_C(-2147483648));      // 전체 폭
    CHECK(S32::decode(0x7FFF'FFFFu) == 2147483647);
    {
        std::uint32_t s = 0xACE1'ACE1u;                            // 고정 시드 왕복
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            const std::int64_t v = static_cast<std::int64_t>(s % 4096u) - 2048;
            ok = ok && S12::decode(S12::encode(v)) == v;
        }
        CHECK(ok);
    }

    // 연습 4: 마스크로 필드 만들기
    using FM = hw::FieldFromMask<R0, 0x0000'0FF0u>;
    CHECK(FM::offset == 4 && FM::width == 8 && FM::mask == 0x0FF0u);
    using FTop = hw::FieldFromMask<R0, 0x8000'0000u>;
    CHECK(FTop::offset == 31 && FTop::width == 1);
    using FAll = hw::FieldFromMask<R0, 0xFFFF'FFFFu>;
    CHECK(FAll::offset == 0 && FAll::width == 32);
    CHECK(!hw::contiguous(0x0u));
    CHECK(!hw::contiguous(0x5u));
    CHECK(hw::contiguous(0x6u));
    CHECK(MaskOk<R0, 0x00F0u>);
    CHECK(!MaskOk<R0, 0x00F1u>);                                   // 끊어진 마스크
    CHECK(!MaskOk<R0, 0x0u>);

    // 연습 5: rc_w0
    const auto w0_setup = [] { mock::reset(); regs[2].w0c = 0xFu; regs[2].res0 = 0xFFFF'FFF0u; regs[2].value = 0x1u; };
    w0_setup();                                                    // 틀린 방법: 읽기 직후 flag1 이 생기면
    mock::after_read = [] { regs[2].value |= 0x2u; };
    hw::clear_w0_naive<RW0>(0x1u);
    CHECK(regs[2].value == 0x0u);                                  // flag1 까지 사라졌다 (읽을 때 0 이었으므로 0 을 씀)
    w0_setup();                                                    // 맞는 방법: 읽기가 없으니 끼어들 틈 자체가 없다
    regs[2].value |= 0x2u;                                         // 쓰기 전에 flag1 이 이미 생겨 있어도
    hw::clear_w0<RW0>(0x1u);
    CHECK(regs[2].value == 0x2u);                                  // flag0 만 지워졌다
    CHECK(mock::rd == 0 && mock::wr == 1);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
