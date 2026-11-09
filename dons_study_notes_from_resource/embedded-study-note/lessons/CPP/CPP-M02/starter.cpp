// CPP-M02 비트필드 대체: Field<Reg, Offset, Width> RMW
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti <file>.cpp -o a && ./a
//
// 실제 칩: Reg::read/write 는 *reinterpret_cast<volatile T*>(Addr) 한 번. (버스 정책 분리는 CPP-M01 참고)
// 이 레슨: mock 레지스터 배열 + 접근 로그. "읽기 몇 번, 쓰기 몇 번"을 테스트가 직접 센다.
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

// ---------------- 가짜 하드웨어 (mock) ----------------
// 레지스터마다 동작 규칙이 있다:
//   w1c  : 1 을 쓰면 그 비트가 0 이 되고, 0 을 쓰면 아무 일도 없다 (Write-1-to-Clear)
//   res0 : 예약 비트. 읽으면 junk 값이 보이고, 1 을 쓰면 위반 카운터가 오른다
namespace mock {
using u32 = std::uint32_t;
inline constexpr std::uintptr_t BASE = 0x4002'0000u;

struct HwReg { u32 value, w1c, res0, junk; };
inline std::array<HwReg, 8> regs{};

enum class Op : std::uint8_t { Read, Write };
struct Access { std::uintptr_t addr; Op op; u32 value; };
inline std::array<Access, 16> log{};
inline std::size_t log_n = 0;
inline unsigned res0_violations = 0;
inline void (*after_read)() = nullptr;   // 읽기 직후 ~ 쓰기 직전: ISR/하드웨어가 끼어드는 지점

inline HwReg& at(std::uintptr_t a) { return regs[(a - BASE) / 4]; }
inline void record(std::uintptr_t a, Op op, u32 v) { if (log_n < log.size()) log[log_n++] = Access{a, op, v}; }

inline u32 read(std::uintptr_t a) {
    const HwReg& r = at(a);
    const u32 v = (r.value & ~r.res0) | (r.junk & r.res0);
    record(a, Op::Read, v);
    if (after_read) after_read();
    return v;
}
inline void write(std::uintptr_t a, u32 v) {
    record(a, Op::Write, v);
    HwReg& r = at(a);
    if (v & r.res0) ++res0_violations;
    r.value = (r.value & r.w1c & ~v) | (v & ~r.w1c & ~r.res0);
}
inline void reset_log() { log_n = 0; }
inline void reset() { regs = {}; reset_log(); res0_violations = 0; after_read = nullptr; }
inline void hw_set(std::uintptr_t a, u32 bits) { at(a).value |= bits; }   // 하드웨어가 플래그를 세움
inline std::size_t reads()  { std::size_t n = 0; for (std::size_t i = 0; i < log_n; ++i) n += log[i].op == Op::Read;  return n; }
inline std::size_t writes() { std::size_t n = 0; for (std::size_t i = 0; i < log_n; ++i) n += log[i].op == Op::Write; return n; }
}  // namespace mock

namespace hw {
// ---------------- 레지스터 (CPP-M01 의 Reg 를 이 레슨에 필요한 만큼만 축약) ----------------
template <typename T> concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool>;   // (CPP-B12 참고)

template <std::uintptr_t Addr, RegWord T = std::uint32_t, T W1C = 0, T Res0 = 0>
struct Reg {
    using word = T;
    static constexpr std::uintptr_t addr = Addr;
    static constexpr T w1c = W1C;     // Write-1-to-Clear 비트들: RMW 에서 반드시 0 으로 써야 한다
    static constexpr T res0 = Res0;   // 예약 비트들: RMW 에서 반드시 0 으로 써야 한다
    static T read() { return static_cast<T>(mock::read(Addr)); }
    static void write(T v) { mock::write(Addr, v); }
};

// ---------------- Part A: 기하(geometry) ----------------
// 하위 width 비트가 1 인 W. width == W 의 비트수 일 때 (W(1) << width) 는 UB 이므로 따로 처리한다.
template <typename W>
constexpr W low_mask(unsigned width) {
    (void)width;
    return W{0}; // TODO(Part A)
}

// 이 필드를 이 레지스터에 놓을 수 있나?
//  - Width >= 1, 필드가 레지스터 안에 다 들어간다 (Offset + Width 덧셈이 넘치지 않게 뺄셈으로 비교)
//  - 예약 비트와 겹치지 않는다
//  - W1C 비트와는 "전부 겹치거나 전혀 안 겹치거나" (일부만 W1C 인 필드는 의미가 없다)
template <typename R>
constexpr bool field_fits_reg(unsigned offset, unsigned width) {
    (void)offset; (void)width;
    return true; // TODO(Part A)
}

// char 계열·bool 을 뺀 "진짜 정수". std::cmp_* 는 이 타입들만 받는다 ([utility.intcmp]).
template <typename U>
concept PlainInt = std::integral<U> && !std::same_as<U, bool> && !std::same_as<U, char> && !std::same_as<U, wchar_t> &&
                   !std::same_as<U, char8_t> && !std::same_as<U, char16_t> && !std::same_as<U, char32_t>;

template <typename U>
constexpr auto as_int(U u) {
    if constexpr (std::is_enum_v<U>) return static_cast<std::underlying_type_t<U>>(u);
    else if constexpr (std::same_as<U, bool>) return static_cast<unsigned>(u);
    else return u;
}

// ---------------- Part A: 변수용 런타임 정책 ----------------
// fit(값, 최댓값) → {필드에 넣을 값(시프트 전), 성공 여부}.  I 는 호출자의 원래 타입이다 (좁히기 전에 비교!).
template <typename W> struct Fit { W raw; bool ok; };

struct Wrap {       // 하위 비트만 남긴다 (C 의 (v & MAX) 와 같음)
    template <typename W, typename I> static constexpr Fit<W> fit(I v, W max) {
        (void)v; (void)max;
        return {W{0}, false}; // TODO(Part A)
    }
};
struct Saturate {   // 음수 → 0, 너무 크면 → max
    template <typename W, typename I> static constexpr Fit<W> fit(I v, W max) {
        (void)v; (void)max;
        return {W{0}, false}; // TODO(Part A)
    }
};
struct Reject {     // 범위 밖이면 ok=false → write() 전체가 버스 접근 없이 false 를 돌려준다
    template <typename W, typename I> static constexpr Fit<W> fit(I v, W max) {
        (void)v; (void)max;
        return {W{0}, true}; // TODO(Part A)
    }
};

// ---------------- 필드 값: "F = v" 가 만들어 내는 것 (아직 아무것도 쓰지 않았다) ----------------
template <typename Fld>
struct FieldValue {
    using field = Fld;
    typename Fld::word bits;   // 이미 Offset 만큼 시프트된 값
    bool ok;                   // Reject 정책에서 false 가 될 수 있다
};

template <typename> inline constexpr bool is_field_value_v = false;
template <typename F> inline constexpr bool is_field_value_v<FieldValue<F>> = true;

// ---------------- Part B: 묶음 조건 ----------------
template <typename First, typename... Rest>
inline constexpr bool same_register_v =
    true; // TODO(Part B): 모든 필드의 reg 타입이 같아야 한다

template <typename First, typename... Rest>
inline constexpr bool disjoint_v =
    true; // TODO(Part B): 마스크가 서로 겹치면 안 된다 (popcount 합 == OR 의 popcount)

template <typename First, typename... Rest>
concept SameRegDisjoint = is_field_value_v<First> && (is_field_value_v<Rest> && ...) &&
                          same_register_v<First, Rest...> && disjoint_v<First, Rest...>;

// 핵심: 읽기 1번 + 쓰기 1번으로 여러 필드를 한꺼번에 바꾼다.
//  - 어느 값이든 ok == false 이면 버스를 건드리지 않고 false (전부 아니면 전무)
//  - 유지할 비트(keep)에서 W1C 와 res0 는 뺀다 → 항상 0 으로 쓰인다 (Part C 의 핵심)
template <typename First, typename... Rest>
    requires SameRegDisjoint<First, Rest...>
bool write(First first, Rest... rest) {
    (void)first; ((void)rest, ...);
    return false; // TODO(Part B)
}

// ---------------- 상수 값 검사: consteval 변환 생성자 ----------------
inline void value_does_not_fit_in_field() {}   // constexpr 가 아니다 → consteval 안에서 호출되면 컴파일 에러

template <typename Fld>
struct Lit {
    typename Fld::word raw;
    template <typename U> requires (Fld::template accepts<U>)
    consteval Lit(U u) : raw(0) { (void)u; } // TODO(Part A): 안 맞으면 value_does_not_fit_in_field() 호출
};

// ---------------- Part A: Field ----------------
template <typename R, unsigned Offset, unsigned Width, typename V = typename R::word>
    requires (field_fits_reg<R>(Offset, Width) && (!std::same_as<V, bool> || Width == 1))
struct Field {
    using reg = R;
    using word = typename R::word;
    using value_type = V;
    static constexpr unsigned offset = Offset;
    static constexpr unsigned width = Width;
    static constexpr word max = low_mask<word>(Width);
    static constexpr word mask = static_cast<word>(max << Offset);
    static constexpr bool is_w1c = ((word{1} << Offset) & R::w1c) != 0;   // 첫 비트만 봐도 된다: field_fits_reg 가 '일부만 W1C' 를 막는다
    static constexpr bool writable = !is_w1c;

    // enum 필드는 그 enum 만, bool 필드는 bool 만, 정수 필드는 진짜 정수만 받는다.
    template <typename U>
    static constexpr bool accepts = std::is_enum_v<V> ? std::same_as<U, V>
                                  : std::same_as<V, bool> ? std::same_as<U, bool> : PlainInt<U>;

    template <typename U> requires (accepts<U>)
    static constexpr bool fits(U u) {
        (void)u;
        return true; // TODO(Part A)
    }

    // 레지스터 전체 값 → 이 필드의 값
    static constexpr V decode(word raw) {
        (void)raw;
        return V{}; // TODO(Part A)
    }

    static V read() { return decode(R::read()); }

    // Mode = 3 : 상수만 가능 (consteval). [[nodiscard]] → "Mode = 3;" 단독 문장(아무 일도 안 함)을 경고로 잡는다.
    [[nodiscard]] constexpr FieldValue<Field> operator=(Lit<Field> lit) const requires writable {
        (void)lit;
        return {word{0}, true}; // TODO(Part A)
    }
    // bool 필드는 어떤 런타임 bool 이든 1비트에 들어가므로 검사가 필요 없다.
    template <std::same_as<bool> B> requires (writable && std::same_as<V, bool>)
    [[nodiscard]] constexpr FieldValue<Field> operator=(B b) const {
        return {static_cast<word>(static_cast<word>(b) << Offset), true};
    }

    // 변수는 정책을 명시한다: EN.with<Saturate>(x)
    template <typename Policy, typename U> requires (writable && accepts<U>)
    [[nodiscard]] static constexpr FieldValue<Field> with(U u) {
        (void)u;
        return {word{0}, true}; // TODO(Part A)
    }

    // NTTP 판: 범위 밖이면 static_assert 로 멈춘다 (C++17 부터 가능한 전통적 방법)
    template <auto v> requires writable
    [[nodiscard]] static constexpr FieldValue<Field> of() {
        static_assert(accepts<decltype(v)>, "Field::of<v>: wrong value type for this field");
        static_assert(fits(v), "Field::of<v>: value out of range for this field's width");
        return {static_cast<word>(static_cast<word>(as_int(v)) << Offset), true};
    }

    static void write(Lit<Field> lit) requires writable { hw::write(FieldValue<Field>{static_cast<word>(lit.raw << Offset), true}); }
    template <auto v> requires writable
    static void write() { hw::write(of<v>()); }

    // 읽기 1번 → fn(옛 값) → 정책 적용 → 쓰기 1번. Reject 로 실패하면 읽기만 하고 쓰지 않는다.
    template <typename Policy = Reject, typename Fn> requires writable
    static bool modify(Fn fn) {
        (void)fn;
        return false; // TODO(Part A)
    }
};

// ---------------- Part C: W1C 플래그 지우기 ----------------
// ack(TC, HT): 고른 W1C 비트만 1 로 쓴다. 다른 W1C 는 0, res0 도 0, 일반 RW 비트는 유지.
// 레지스터 전체가 W1C+예약이면 유지할 비트가 없으므로 읽기 없이 쓰기 1번.
template <typename First, typename... Rest>
    requires (First::is_w1c && (Rest::is_w1c && ...) && (std::same_as<typename First::reg, typename Rest::reg> && ...))
void ack(First, Rest...) {
    using R = typename First::reg;
    using W = typename R::word;
    constexpr W flags = static_cast<W>(First::mask | (W{0} | ... | Rest::mask));
    (void)flags; // TODO(Part C): 순수 W1C 레지스터면 읽지 말 것
}
}  // namespace hw

// ---------------- Part C: 가짜 DMA 채널 "DMAX" ----------------
//  CSR  bit0 EN | bit1 DIR | bit3:2 PRIO | bit7:4 예약 | bit8 TC(W1C) | bit9 HT(W1C) | bit10 TE(W1C) | bit31:11 예약
//  CFG  bit2:0 BURST | bit5:4 XWIDTH | bit15:8 THRESH | bit31:16 COUNT  (전부 RW)
//  ICR  bit0 TC | bit1 HT | bit2 TE (전부 W1C), 나머지 예약
namespace dmax {
using u32 = std::uint32_t;
inline constexpr u32 CSR_W1C = 0x0000'0700u, CSR_RES0 = 0xFFFF'F8F0u;
using CSR = hw::Reg<mock::BASE + 0x00, u32, CSR_W1C, CSR_RES0>;
using CFG = hw::Reg<mock::BASE + 0x04>;
using ICR = hw::Reg<mock::BASE + 0x08, u32, 0x7u, ~0x7u>;
using R16 = hw::Reg<mock::BASE + 0x0C, std::uint16_t>;
using R8 = hw::Reg<mock::BASE + 0x10, std::uint8_t>;

enum class Prio : std::uint8_t { Low = 0, Med = 1, High = 2, VeryHigh = 3 };
enum class XWidth : std::uint8_t { B8 = 0, B16 = 1, B32 = 2, Bogus = 9 };    // Bogus 는 2비트에 안 들어간다

inline constexpr hw::Field<CSR, 0, 1, bool> EN{};
inline constexpr hw::Field<CSR, 1, 1, bool> DIR{};
inline constexpr hw::Field<CSR, 2, 2, Prio> PRIO{};
inline constexpr hw::Field<CSR, 8, 1, bool> TC{};
inline constexpr hw::Field<CSR, 9, 1, bool> HT{};
inline constexpr hw::Field<CSR, 10, 1, bool> TE{};
inline constexpr hw::Field<CFG, 0, 3> BURST{};
inline constexpr hw::Field<CFG, 4, 2, XWidth> XW{};
inline constexpr hw::Field<CFG, 8, 8> THRESH{};
inline constexpr hw::Field<CFG, 16, 16> COUNT{};
inline constexpr hw::Field<ICR, 0, 1, bool> CTC{};
inline constexpr hw::Field<ICR, 1, 1, bool> CHT{};
inline constexpr hw::Field<ICR, 2, 1, bool> CTE{};

// 일부러 틀린 코드 (비교용): 흔한 C 스타일 RMW. W1C 플래그와 예약 비트 junk 를 그대로 되쓴다.
inline void naive_enable() { CSR::write(CSR::read() | 1u); }

// EN=1, DIR, PRIO 를 RMW 1번으로. pending 플래그를 건드리면 안 된다. p 는 런타임 값 → with<Reject>.
inline bool dma_start(Prio p, bool to_periph) {
    (void)p; (void)to_periph;
    return false; // TODO(Part C)
}
// CSR 의 TC 플래그만 지운다 (HT/TE 는 남아야 한다)
inline void dma_ack_tc() {
    // TODO(Part C)
}
// ICR 에 모든 플래그 clear 를 쓰기 1번으로 (읽기 0번)
inline void dma_clear_all() {
    // TODO(Part C)
}
// BURST/XWIDTH/THRESH 를 RMW 1번으로. COUNT 는 유지. thresh 는 런타임 값이고 범위 밖이면 레지스터를 건드리지 말 것.
inline bool dma_config(unsigned thresh) {
    (void)thresh;
    return false; // TODO(Part C)
}
}  // namespace dmax

// ---------------- 테스트 도구 ----------------
#define FT(obj) std::remove_cvref_t<decltype(obj)>

// consteval 생성자를 "템플릿 인자"(상수 평가 강제) 자리에 넣으면 실패가 substitution failure 로 잡힌다.
template <typename F, auto v>
concept LitOk = requires { typename std::integral_constant<int, (hw::Lit<F>(v), 0)>; };

template <typename... Fv> concept CanWrite = requires(Fv... v) { hw::write(v...); };
template <typename... Fs> concept CanAck = requires(Fs... f) { hw::ack(f...); };
template <typename F, typename U> concept CanAssign = requires(const F f, U u) { f = u; };
template <typename F, unsigned O, unsigned W> concept ValidField = requires { typename hw::Field<F, O, W>; };

template <typename F> using VOf = hw::FieldValue<F>;

inline std::uint32_t xorshift32(std::uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

template <typename F>
void geom(typename F::word max, typename F::word mask) {
    CHECK(F::max == max && F::mask == mask);
}

// 필드 하나를 무작위 초기값/값으로 왕복: 옆 비트 보존 + 읽기 1 + 쓰기 1
template <typename F>
bool fuzz_field(std::uint32_t seed, int rounds) {
    using W = typename F::word;
    for (int i = 0; i < rounds; ++i) {
        const W init = static_cast<W>(xorshift32(seed));
        const W val = static_cast<W>(xorshift32(seed));
        mock::reset();
        mock::regs[(F::reg::addr - mock::BASE) / 4].value = init;
        hw::write(F::template with<hw::Wrap>(val));
        const W want_v = static_cast<W>(val & F::max);
        const W want_r = static_cast<W>((init & ~F::mask) | (want_v << F::offset));
        if (mock::regs[(F::reg::addr - mock::BASE) / 4].value != want_r) return false;
        if (mock::reads() != 1 || mock::writes() != 1) return false;
        if (static_cast<W>(F::read()) != want_v) return false;
    }
    return true;
}

// ---------------- 테스트 ----------------
int main() {
    using namespace dmax;
    using mock::regs;

    // ===== Part A: 마스크 표 (경계값: 최상위 비트, 레지스터 전체 폭, 8/16비트 레지스터의 정수 승격) =====
    geom<hw::Field<CFG, 0, 1>>(0x1u, 0x1u);
    geom<hw::Field<CFG, 4, 4>>(0xFu, 0xF0u);
    geom<hw::Field<CFG, 31, 1>>(0x1u, 0x8000'0000u);
    geom<hw::Field<CFG, 0, 32>>(0xFFFF'FFFFu, 0xFFFF'FFFFu);
    geom<hw::Field<CFG, 1, 31>>(0x7FFF'FFFFu, 0xFFFF'FFFEu);
    geom<hw::Field<CFG, 16, 16>>(0xFFFFu, 0xFFFF'0000u);
    geom<hw::Field<R16, 4, 12>>(0x0FFFu, 0xFFF0u);
    geom<hw::Field<R16, 0, 16>>(0xFFFFu, 0xFFFFu);
    geom<hw::Field<R8, 0, 8>>(0xFFu, 0xFFu);
    geom<hw::Field<R8, 7, 1>>(0x1u, 0x80u);

    // 기하 검사: requires 로 "이 필드가 만들어지는가"
    CHECK(ValidField<CFG, 0, 32>);
    CHECK(ValidField<CFG, 31, 1>);
    CHECK(!ValidField<CFG, 0, 0>);                 // 폭 0
    CHECK(!ValidField<CFG, 32, 1>);                // 시작이 레지스터 밖
    CHECK(!ValidField<CFG, 30, 3>);                // 끝이 레지스터 밖
    CHECK(!ValidField<CFG, 0xFFFF'FFFFu, 2>);      // Offset+Width 덧셈이 넘치는 경우
    CHECK(!ValidField<R8, 0, 9>);                  // 8비트 레지스터
    CHECK(!ValidField<CSR, 4, 4>);                 // 예약 비트 위
    CHECK(!ValidField<CSR, 3, 2>);                 // 일부만 예약
    CHECK(!ValidField<CSR, 7, 2>);                 // 예약 + W1C 걸침
    CHECK(ValidField<CSR, 8, 3>);                  // 전부 W1C 는 허용

    // read / write / modify
    mock::reset();
    regs[1].value = 0xABCD'5A12u;
    CHECK(BURST.read() == 0x2u);
    CHECK(THRESH.read() == 0x5Au);
    CHECK(COUNT.read() == 0xABCDu);
    CHECK(XW.read() == XWidth::B16);
    mock::reset();
    regs[1].value = 0xABCD'5A12u;
    THRESH.write(0xFF);
    CHECK(regs[1].value == 0xABCD'FF12u);
    CHECK(mock::reads() == 1 && mock::writes() == 1);
    mock::reset();
    regs[1].value = 0xABCD'5A12u;
    THRESH.write<0x01>();
    CHECK(regs[1].value == 0xABCD'0112u);
    mock::reset();
    regs[1].value = 0x0000'7F00u;
    CHECK(THRESH.modify([](unsigned t) { return t + 1; }));
    CHECK(regs[1].value == 0x0000'8000u);
    CHECK(mock::reads() == 1 && mock::writes() == 1);
    mock::reset();
    regs[1].value = 0x0000'FF00u;
    CHECK(!THRESH.modify([](unsigned t) { return t + 1; }));     // 기본 Reject: 256 은 안 들어감
    CHECK(regs[1].value == 0x0000'FF00u);
    CHECK(mock::reads() == 1 && mock::writes() == 0);
    mock::reset();
    regs[1].value = 0x0000'FF00u;
    CHECK(THRESH.modify<hw::Wrap>([](unsigned t) { return t + 1; }));
    CHECK(regs[1].value == 0x0000'0000u);
    mock::reset();
    regs[1].value = 0x0000'FF00u;
    CHECK(THRESH.modify<hw::Saturate>([](unsigned t) { return t + 1; }));
    CHECK(regs[1].value == 0x0000'FF00u);
    mock::reset();
    regs[0].value = 0x4u;
    CHECK(PRIO.modify([](Prio) { return Prio::VeryHigh; }));     // enum 필드 modify
    CHECK(PRIO.read() == Prio::VeryHigh);

    // 상수 범위 검사: fits (constexpr), Lit (consteval, 템플릿 인자로 강제 평가)
    using TH = FT(THRESH);
    using BU = FT(BURST);
    using XF = FT(XW);
    CHECK(TH::fits(0) && TH::fits(255) && !TH::fits(256) && !TH::fits(-1));
    CHECK(TH::fits(255ull) && !TH::fits(0x1'0000'0000ull));
    CHECK(BU::fits(7) && !BU::fits(8));
    CHECK(XF::fits(XWidth::B32) && !XF::fits(XWidth::Bogus));
    CHECK(LitOk<TH, 255>);
    CHECK(!LitOk<TH, 256>);
    CHECK(!LitOk<TH, -1>);
    CHECK(!LitOk<TH, 0x1'0000'0100ull>);                          // 32비트로 먼저 줄이면 0x100 → 0x00 처럼 보이는 함정
    CHECK(LitOk<XF, XWidth::B16>);
    CHECK(!LitOk<XF, XWidth::Bogus>);
    CHECK(!LitOk<BU, 8u>);
    // 함정: requires 안은 unevaluated → consteval 이 실행되지 않으므로 "타입만" 본다
    CHECK(CanAssign<TH, int>);                                    // 범위와 무관하게 true

    // 타입 규칙
    CHECK(!CanAssign<XF, int>);                                   // enum 필드에 정수 금지
    CHECK(!CanAssign<FT(PRIO), XWidth>);                          // 다른 enum 금지
    CHECK(!CanAssign<FT(EN), int>);                               // bool 필드에 정수 금지
    CHECK(!CanAssign<TH, bool>);                                  // 정수 필드에 bool 금지
    CHECK(!CanAssign<TH, char>);

    // 런타임 정책: 호출자의 원래 타입 그대로 비교해야 한다
    {
        struct Row { std::int64_t in; unsigned wrap; unsigned sat; bool rej_ok; };
        const Row table[] = {
            {0, 0, 0, true},
            {1, 1, 1, true},
            {255, 255, 255, true},
            {256, 0, 255, false},
            {257, 1, 255, false},
            {-1, 255, 0, false},
            {-256, 0, 0, false},
            {0x1'0000'0005ll, 5, 255, false},                     // 32비트로 줄이면 5 — 범위 안처럼 보임
        };
        for (const Row& r : table) {
            const auto w = TH::with<hw::Wrap>(r.in);
            const auto s = TH::with<hw::Saturate>(r.in);
            const auto x = TH::with<hw::Reject>(r.in);
            CHECK(w.ok && w.bits == (r.wrap << 8) && s.ok && s.bits == (r.sat << 8) && x.ok == r.rej_ok);
        }
        CHECK(TH::with<hw::Saturate>(std::int8_t{-5}).bits == 0u);
        CHECK(TH::with<hw::Wrap>(std::uint8_t{0xAB}).bits == 0xAB00u);
        CHECK(!TH::with<hw::Reject>(XF::max + 0x1'0000'0000ull).ok);
    }

    // 무작위 왕복 (고정 시드)
    CHECK(fuzz_field<BU>(0x1234'5678u, 300));
    CHECK(fuzz_field<TH>(0x9E37'79B9u, 300));
    CHECK(fuzz_field<FT(COUNT)>(0xDEAD'BEEFu, 300));
    CHECK(fuzz_field<hw::Field<CFG, 0, 32>>(0x0BAD'F00Du, 300));
    CHECK(fuzz_field<hw::Field<CFG, 31, 1>>(0x1357'9BDFu, 300));
    CHECK(fuzz_field<hw::Field<R16, 3, 13>>(0x2468'ACE0u, 300));
    CHECK(fuzz_field<hw::Field<R8, 2, 6>>(0x0F1E'2D3Cu, 300));

    // ===== Part B: 여러 필드, RMW 한 번 =====
    mock::reset();
    regs[1].value = 0xABCD'0000u;
    CHECK(hw::write(BURST = 5, XW = XWidth::B16, THRESH = 0x3C));
    CHECK(regs[1].value == 0xABCD'3C15u);
    CHECK(mock::log_n == 2 && mock::log[0].op == mock::Op::Read && mock::log[1].op == mock::Op::Write);
    CHECK(mock::log_n == 2 && mock::log[0].addr == CFG::addr && mock::log[1].addr == CFG::addr);
    CHECK(mock::log_n == 2 && mock::log[1].value == 0xABCD'3C15u);

    mock::reset();                                                 // 순서를 바꿔도 결과는 같다
    regs[1].value = 0xABCD'0000u;
    CHECK(hw::write(THRESH = 0x3C, BURST = 5, XW = XWidth::B16));
    CHECK(regs[1].value == 0xABCD'3C15u);

    mock::reset();                                                 // 필드 1개짜리
    regs[1].value = 0xFFFF'FFFFu;
    CHECK(hw::write(BURST = 0));
    CHECK(regs[1].value == 0xFFFF'FFF8u);

    mock::reset();                                                 // 16비트 레지스터: ~mask 의 정수 승격 확인
    regs[3].value = 0xA5A5u;
    CHECK(hw::write(hw::Field<R16, 0, 4>{} = 0x3, hw::Field<R16, 12, 4>{} = 0xC));
    CHECK(regs[3].value == 0xC5A3u);
    CHECK(mock::reads() == 1 && mock::writes() == 1);

    // 런타임 값 하나라도 범위 밖이면 "전부 아니면 전무": 버스 접근 0회
    mock::reset();
    regs[1].value = 0x1111'1111u;
    {
        volatile unsigned t = 300, c = 7;                          // volatile: 정말 런타임 값으로
        CHECK(!hw::write(BURST = 1, THRESH.with<hw::Reject>(t + 0u), COUNT.with<hw::Reject>(c + 0u)));
    }
    CHECK(regs[1].value == 0x1111'1111u);
    CHECK(mock::log_n == 0);
    mock::reset();
    {
        volatile unsigned t = 200;
        CHECK(hw::write(BURST = 1, THRESH.with<hw::Reject>(t + 0u)));
        CHECK(regs[1].value == 0x0000'C801u);
    }

    // 컴파일 금지: requires 로 증명
    CHECK(CanWrite<VOf<BU>, VOf<TH>>);
    CHECK(!CanWrite<VOf<BU>, VOf<FT(EN)>>);                         // 다른 레지스터
    CHECK(!CanWrite<VOf<TH>, VOf<FT(COUNT)>, VOf<FT(PRIO)>>);       // 셋 중 하나만 달라도
    CHECK(!CanWrite<VOf<BU>, VOf<BU>>);                             // 같은 필드 두 번
    CHECK(!CanWrite<VOf<TH>, VOf<hw::Field<CFG, 12, 8>>>);          // 다른 필드지만 비트가 겹침
    CHECK(CanWrite<VOf<TH>, VOf<hw::Field<CFG, 16, 8>>>);           // 맞닿기만 하면 OK
    CHECK(!CanWrite<int>);                                          // 필드 값이 아님
    CHECK(!CanWrite<VOf<BU>, unsigned>);

    // 무작위: 4개 필드를 한 번에, 기준 구현과 비교
    {
        std::uint32_t seed = 0xC0FF'EE01u;
        bool ok = true;
        for (int i = 0; i < 500 && ok; ++i) {
            const std::uint32_t init = xorshift32(seed);
            const std::uint32_t a = xorshift32(seed), b = xorshift32(seed), c = xorshift32(seed);
            mock::reset();
            regs[1].value = init;
            ok = hw::write(BURST.with<hw::Wrap>(a), THRESH.with<hw::Wrap>(b), COUNT.with<hw::Wrap>(c)) &&
                 regs[1].value == ((init & 0x0000'00F8u) | (a & 7u) | ((b & 0xFFu) << 8) | ((c & 0xFFFFu) << 16)) &&
                 mock::reads() == 1 && mock::writes() == 1;
        }
        CHECK(ok);
    }

    // ===== Part C: W1C / 예약 비트 =====
    const auto setup = [] {
        mock::reset();
        regs[0].w1c = CSR_W1C;
        regs[0].res0 = CSR_RES0;
        regs[0].junk = CSR_RES0;                                    // 예약 비트가 전부 1 로 읽히는 최악의 칩
        regs[2].w1c = 0x7u;
        regs[2].res0 = ~0x7u;
    };

    // 기준선: 흔한 C 스타일 RMW 는 정말로 망가진다
    setup();
    mock::hw_set(CSR::addr, 0x100u | 0x200u);                       // TC, HT pending
    naive_enable();
    CHECK(regs[0].value == 0x1u);                                   // EN 은 켜졌지만 pending 플래그 2개가 사라졌다
    CHECK(mock::res0_violations == 1u);

    // 올바른 RMW: 플래그 유지, 예약 비트 0
    setup();
    mock::hw_set(CSR::addr, 0x100u | 0x200u);
    CHECK(dma_start(Prio::High, true));
    CHECK(regs[0].value == (0x300u | 0x1u | 0x2u | (2u << 2)));
    CHECK(mock::res0_violations == 0u);
    CHECK(mock::reads() == 1 && mock::writes() == 1);
    CHECK(mock::log_n == 2 && (mock::log[1].value & (CSR_W1C | CSR_RES0)) == 0u);

    // 런타임 enum 값이 2비트를 넘으면 거부
    setup();
    CHECK(!dma_start(static_cast<Prio>(9), false));
    CHECK(regs[0].value == 0u && mock::log_n == 0);

    // 읽기 ~ 쓰기 사이에 하드웨어가 플래그를 세워도 유지된다
    setup();
    mock::after_read = [] { mock::hw_set(CSR::addr, 0x400u); };     // TE 발생
    CHECK(hw::write(EN = true));
    CHECK((regs[0].value & 0x400u) != 0u);
    CHECK(TE.read());

    // 정직한 테스트: RMW 는 원자적이지 않다. 사이에 ISR 이 RW 비트를 바꾸면 그 변경은 사라진다
    setup();
    mock::after_read = [] { regs[0].value |= 0x2u; };               // ISR 이 DIR 을 켬
    CHECK(hw::write(EN = true));
    CHECK((regs[0].value & 0x2u) == 0u);

    // ack: 고른 플래그만
    setup();
    regs[0].value = 0x700u | 0x5u;                                  // TC HT TE + EN + PRIO=1
    dma_ack_tc();
    CHECK(regs[0].value == (0x600u | 0x5u));
    CHECK(mock::reads() == 1 && mock::writes() == 1);
    CHECK(mock::log_n == 2 && (mock::log[1].value & 0x700u) == 0x100u);
    CHECK(mock::res0_violations == 0u);

    setup();
    regs[0].value = 0x700u;
    hw::ack(TC, TE);
    CHECK(regs[0].value == 0x200u);

    // 순수 W1C 레지스터: 읽기 0번
    setup();
    regs[2].value = 0x7u;
    dma_clear_all();
    CHECK(regs[2].value == 0u);
    CHECK(mock::reads() == 0 && mock::writes() == 1);
    CHECK(mock::log_n == 1 && mock::log[0].value == 0x7u);
    setup();
    regs[2].value = 0x7u;
    hw::ack(CHT);
    CHECK(regs[2].value == 0x5u && mock::reads() == 0);

    // 타입 규칙: W1C 필드는 '=' 불가, RW 필드는 ack 불가
    CHECK(!CanAssign<FT(TC), bool>);
    CHECK(CanAck<FT(TC), FT(HT)>);
    CHECK(!CanAck<FT(EN)>);
    CHECK(!CanAck<FT(TC), FT(EN)>);
    CHECK(!CanAck<FT(TC), FT(CTC)>);                                // 다른 레지스터

    // 드라이버: config 는 COUNT 를 보존
    setup();
    regs[1].value = 0x0042'0000u;
    CHECK(dma_config(0x80u));
    CHECK(regs[1].value == 0x0042'8024u);
    CHECK(mock::reads() == 1 && mock::writes() == 1);
    setup();
    regs[1].value = 0x0042'0000u;
    CHECK(!dma_config(0x100u));
    CHECK(regs[1].value == 0x0042'0000u && mock::log_n == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
