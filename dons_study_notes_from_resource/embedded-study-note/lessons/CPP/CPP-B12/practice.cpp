// CPP-B12 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(std::endian::native == std::endian::little);

// ---- 공통: 본문과 같은 규칙 + 작은 mock 주소 공간 (오프셋 = 주소) ----
template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 8);

alignas(8) static std::uint8_t mem[32];

template <RegWord T> T reg_read(std::uintptr_t a) { T v; std::memcpy(&v, mem + a, sizeof v); return v; }
template <RegWord T> void reg_write(std::uintptr_t a, T v) { std::memcpy(mem + a, &v, sizeof v); }

// 연습 1: read-modify-write. clr/set 은 std::type_identity_t<T> → 추론에 참여하지 않음(non-deduced context).
//         T 는 호출자가 꼭 적고, 0xF0 같은 int 리터럴은 T 로 변환된다.
template <RegWord T>
void reg_modify(std::uintptr_t a, std::type_identity_t<T> clr, std::type_identity_t<T> set) {
    reg_write<T>(a, static_cast<T>((reg_read<T>(a) & static_cast<T>(~clr)) | set));
}

// 연습 2: 폭을 호출부에 강제로 적게 만드는 write
template <RegWord T> void reg_write_x(std::uintptr_t a, std::type_identity_t<T> v) { reg_write<T>(a, v); }
template <typename V> constexpr bool can_write_deduced  = requires(std::uintptr_t a, V v) { reg_write(a, v); };
template <typename V> constexpr bool can_write_x_no_T   = requires(std::uintptr_t a, V v) { reg_write_x(a, v); };
template <typename V> constexpr bool can_write_x_with_u8 = requires(std::uintptr_t a, V v) { reg_write_x<std::uint8_t>(a, v); };

// 연습 3: 비타입 템플릿 인자(NTTP)로 필드 위치를 고정 → 범위 검사를 컴파일타임에
template <RegWord T, unsigned Shift, unsigned Width>
    requires(Width >= 1 && Shift + Width <= 8 * sizeof(T))
T field_read(std::uintptr_t a) {
    constexpr T mask = (Width == 8 * sizeof(T)) ? static_cast<T>(~T{0}) : static_cast<T>((T{1} << Width) - 1u);
    return static_cast<T>((reg_read<T>(a) >> Shift) & mask);
}
template <typename T, unsigned S, unsigned W>
constexpr bool field_ok = requires(std::uintptr_t a) { field_read<T, S, W>(a); };

// 연습 4: Cortex-M(32비트 버스)용 더 엄격한 규칙 — 64비트 금지, 문자 타입 금지(uint8_t = unsigned char 는 허용)
template <typename T>
concept Bus32Word = RegWord<T> && (sizeof(T) <= 4)
    && !std::same_as<T, char> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t>
    && !std::same_as<T, char32_t> && !std::same_as<T, wchar_t>;

// 연습 5: std::bit_cast (C++20) — memcpy 와 같은 의미, 그런데 constexpr
constexpr std::uint32_t load_le32(std::array<std::uint8_t, 4> b) { return std::bit_cast<std::uint32_t>(b); }
static_assert(load_le32({0x44, 0x33, 0x22, 0x11}) == 0x1122'3344u);    // 컴파일타임 검증

// ---------------- 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // 연습 1
    reg_write<std::uint16_t>(0, 0xABCDu);
    reg_modify<std::uint16_t>(0, 0x00F0, 0x0030);               // 비트 7:4 를 0x3 으로
    CHECK(reg_read<std::uint16_t>(0) == 0xAB3Du);
    reg_write<std::uint8_t>(2, 0xFFu);
    reg_modify<std::uint8_t>(2, 0x81, 0x00);                    // ~clr 이 int 로 승격돼도 static_cast<T> 로 정리
    CHECK(reg_read<std::uint8_t>(2) == 0x7Eu);
    CHECK(reg_read<std::uint8_t>(3) == 0u);                     // 옆 바이트는 그대로

    // 연습 2
    CHECK(can_write_deduced<unsigned> && !can_write_deduced<int>);
    CHECK(!can_write_x_no_T<unsigned>);                         // T 를 안 적으면 추론 불가 → 호출 불가
    CHECK(can_write_x_with_u8<int>);                            // T 를 적으면 int 값도 변환되어 OK
    reg_write_x<std::uint8_t>(4, 0x7F);
    CHECK(reg_read<std::uint32_t>(4) == 0x7Fu);

    // 연습 3
    reg_write<std::uint32_t>(8, 0x1234'5678u);
    CHECK((field_read<std::uint32_t, 8, 8>(8) == 0x56u));
    CHECK((field_read<std::uint32_t, 0, 32>(8) == 0x1234'5678u));
    CHECK((field_ok<std::uint8_t, 4, 4> && !field_ok<std::uint8_t, 6, 4> && !field_ok<std::uint16_t, 0, 0>));
    CHECK((!field_ok<std::int32_t, 0, 8>));                     // signed 레지스터 타입도 거부

    // 연습 4
    CHECK(Bus32Word<std::uint8_t> && Bus32Word<std::uint16_t> && Bus32Word<std::uint32_t>);
    CHECK(!Bus32Word<std::uint64_t> && RegWord<std::uint64_t>);
    CHECK(!Bus32Word<char8_t> && RegWord<char8_t>);              // char8_t 는 unsigned integral 이라 RegWord 는 통과
    CHECK(!Bus32Word<char> && !Bus32Word<char16_t> && !Bus32Word<bool>);

    // 연습 5
    std::array<std::uint8_t, 4> raw{};
    std::memcpy(raw.data(), mem + 8, 4);
    CHECK(load_le32(raw) == 0x1234'5678u);
    CHECK(std::bit_cast<std::uint32_t>(raw) == reg_read<std::uint32_t>(8));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
