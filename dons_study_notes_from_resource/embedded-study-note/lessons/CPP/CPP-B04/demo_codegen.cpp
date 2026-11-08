// CPP-B04 demo: constexpr 로 계산한 주소·마스크는 기계어에 "즉시값(immediate)" 으로만 남는다
// 실행하지 않는다 (진짜 MMIO 주소에 쓰므로). 오브젝트만 만들어 디스어셈블:
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_codegen.cpp -o /tmp/dc_O2.o && objdump -d --no-show-raw-insn /tmp/dc_O2.o
//   c++ -std=c++20 -O0 -fno-exceptions -fno-rtti -c demo_codegen.cpp -o /tmp/dc_O0.o && objdump -d --no-show-raw-insn /tmp/dc_O0.o
//   (asm 텍스트: c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_codegen.cpp)
#include <cstdint>
#include <cstdlib>

[[noreturn]] inline void invalid_field_spec() { std::abort(); }
constexpr uint32_t bit(unsigned n) { if (n >= 32) invalid_field_spec(); return 1u << n; }
constexpr uint32_t mask(unsigned offset, unsigned width) {
    if (width < 1 || width > 32 || offset > 32 - width) invalid_field_spec();
    return ((width == 32) ? 0xFFFF'FFFFu : (1u << width) - 1u) << offset;
}
inline constexpr uintptr_t AHB1_BASE = 0x4002'0000u;
constexpr uintptr_t gpio_base(unsigned port) {
    if (port >= 11) invalid_field_spec();
    return AHB1_BASE + port * 0x400u;
}
inline constexpr uint32_t  GPIO_BSRR = 0x18;
inline constexpr unsigned  LED_PIN   = 5;
inline constexpr uintptr_t LED_BSRR  = gpio_base(0) + GPIO_BSRR;     // 0x4002'0018

// (1) C++: constexpr 정수 주소 + 쓰는 순간에만 reinterpret_cast
void led_on_constexpr() { *reinterpret_cast<volatile uint32_t*>(LED_BSRR) = bit(LED_PIN); }

// (2) C / CMSIS 스타일 매크로 — 비교용
#define GPIOA_BSRR_C (*(volatile uint32_t*)(0x40020000u + 0x18u))
void led_on_macro() { GPIOA_BSRR_C = (1u << 5); }

// (3) 인자가 런타임 값이면 계산(과 범위 검사)이 기계어에 남는다
void gpio_set_rt(unsigned port, unsigned pin) {
    *reinterpret_cast<volatile uint32_t*>(gpio_base(port) + GPIO_BSRR) = bit(pin);
}

// (4) -O0 에서의 차이: constexpr 변수는 최적화 레벨과 무관하게 컴파일타임 확정
uint32_t moder_plain()     { uint32_t m = mask(10, 2);           return m; }   // -O0: mask() 호출
uint32_t moder_constexpr() { constexpr uint32_t m = mask(10, 2); return m; }   // -O0: 즉시값 0xC00
