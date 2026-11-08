// CPP-B12 demo: static_assert(C++17) vs concept(C++20) 에러 메시지 비교
// 정상 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -fsyntax-only demo_errors.cpp
//            (SHOW_ERROR=1..5 각각 한 가지 에러)
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

alignas(8) static std::uint8_t mem[16];

template <typename T>
inline constexpr bool is_reg_word_v =
    std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool> && sizeof(T) <= 8;

template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 8);

template <typename T>                       // C++17: 본문 안 static_assert
T reg_read(std::uintptr_t off) {
    static_assert(is_reg_word_v<T>, "reg_read<T>: T must be an unsigned integer register type (uint8/16/32/64_t)");
    T v{}; std::memcpy(&v, mem + off, sizeof v); return v;
}
template <typename T>
void reg_write(std::uintptr_t off, T value) {
    static_assert(is_reg_word_v<T>, "reg_write<T>: T must be an unsigned integer register type (uint8/16/32/64_t)");
    std::memcpy(mem + off, &value, sizeof value);
}

template <RegWord T>                        // C++20: 선언에 제약
T reg_read_c(std::uintptr_t off) { return reg_read<T>(off); }
template <RegWord T>
void reg_write_c(std::uintptr_t off, T value) { reg_write<T>(off, value); }

int main() {
    reg_write_c(0, 0xA5u);
    std::printf("ok: %#x\n", reg_read_c<std::uint32_t>(0));
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    (void)reg_read<int>(0);                 // E1: static_assert 버전에 signed int
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 2
    (void)reg_read_c<int>(0);               // E2: concept 버전에 signed int
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 3
    (void)reg_read_c<bool>(0);              // E3: bool (unsigned_integral 은 통과, !same_as<bool> 에서 걸림)
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 4
    reg_write_c(0, 5);                      // E4: 추론 함정 — 리터럴 5 는 int
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 5
    reg_write<std::uint8_t>(0, 0x1FF);      // E5: T 를 명시하면 추론 없음 → 값이 잘림 (경고 → -Werror)
#endif
}
