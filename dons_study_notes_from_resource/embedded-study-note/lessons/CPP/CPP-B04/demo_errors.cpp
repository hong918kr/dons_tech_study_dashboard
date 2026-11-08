// CPP-B04 demo: "상수 평가 중의 UB / 금지된 연산" 은 컴파일 에러가 된다
// 정상 빌드:  c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기:  c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   (1..6)
//             SHOW_ERROR=6 만은 -std=c++17 로 (C++17 vs C++20 차이)
#include <cstdint>
#include <cstdio>
#include <cstdlib>

[[noreturn]] inline void invalid_field_spec() { std::abort(); }

constexpr uint32_t mask_naive(unsigned offset, unsigned width) {   // width == 32 처리 없음
    return ((1u << width) - 1u) << offset;
}
constexpr uint32_t mask(unsigned offset, unsigned width) {
    if (width < 1 || width > 32 || offset > 32 - width) invalid_field_spec();
    return ((width == 32) ? 0xFFFF'FFFFu : (1u << width) - 1u) << offset;
}
#if __cplusplus >= 202002L   // consteval 은 C++20 부터
consteval uint32_t ct_mask(unsigned offset, unsigned width) { return mask(offset, width); }
#else
constexpr uint32_t ct_mask(unsigned offset, unsigned width) { return mask(offset, width); }
#endif

inline constexpr uintptr_t GPIOA_BASE = 0x4002'0000u;

#if SHOW_ERROR == 1   // ① 시프트 양 >= 폭 (UB) — 상수 평가에서는 에러
constexpr uint32_t m1 = mask_naive(0, 32);
#endif
#if SHOW_ERROR == 2   // ② 범위 밖 → constexpr 아닌 함수에 도달 → 에러
constexpr uint32_t m2 = mask(30, 4);
#endif
#if SHOW_ERROR == 3   // ③ reinterpret_cast 는 constant expression 에 올 수 없다
struct GpioRegs { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR; };
constexpr GpioRegs* GPIOA = reinterpret_cast<GpioRegs*>(GPIOA_BASE);
#endif
#if SHOW_ERROR == 4   // ④ consteval 함수에 런타임 값
uint32_t runtime_mask(unsigned w) { return ct_mask(0, w); }
#endif
#if SHOW_ERROR == 5   // ⑤ static_assert 로 메모리 맵 오타 잡기 (GPIOB 를 0x4002'0800 로 잘못 씀)
inline constexpr uintptr_t GPIOB_BASE = 0x4002'0800u;
static_assert(GPIOB_BASE == GPIOA_BASE + 0x400, "GPIOB 는 GPIOA + 0x400 (RM0090 2.3)");
#endif
#if SHOW_ERROR == 6   // ⑥ -std=c++17 에서: 음수 왼쪽 시프트는 UB → 에러. C++20 에서는 정의됨(-2)
constexpr int neg = -1 << 1;
#endif

int main() {
    constexpr uint32_t a = mask(4, 4);
    constexpr uint32_t b = ct_mask(0, 32);
    constexpr int c = 1 << 31;              // C++20: 정의됨 (INT_MIN). C 에서는 UB
    volatile unsigned w = 32;
    uint32_t d = mask(0, w);                // constexpr 함수는 런타임 인자로도 호출 가능
    std::printf("mask(4,4)=0x%08X  ct_mask(0,32)=0x%08X  1<<31=%d  mask(0,w)=0x%08X\n",
                (unsigned)a, (unsigned)b, c, (unsigned)d);
    return 0;
}
