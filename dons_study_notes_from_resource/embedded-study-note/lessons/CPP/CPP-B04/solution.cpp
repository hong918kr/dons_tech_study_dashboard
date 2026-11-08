// CPP-B04 constexpr 기초: 컴파일타임 주소·마스크 계산
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

// 상수 평가(constant evaluation) 도중 이 "constexpr 가 아닌" 함수에 도달하면
//   → 그 식은 constant expression 이 아님 → static_assert / constexpr 변수 / 템플릿 인자에서 컴파일 에러.
// 런타임에 도달하면(잘못된 인자) → 펌웨어라면 assert 훅·트랩. 여기서는 abort.
// (-fno-exceptions 빌드라서 constexpr 안에서 throw 를 쓰는 흔한 트릭은 못 쓴다.)
[[noreturn]] inline void invalid_field_spec() { std::abort(); }

// ---------------- Part B: 범위 검사 (Part A 함수들이 이걸 부른다) ----------------
// 올바른 필드: 1 <= width <= 32 이고 offset + width <= 32.
// 주의: offset + width 는 unsigned 덧셈이라 랩어라운드(wrap-around) 될 수 있다.
constexpr bool field_ok(unsigned offset, unsigned width) {
    return width >= 1 && width <= 32 && offset <= 32 - width;   // 뺄셈 쪽으로 옮겨 랩어라운드 차단
}

// ---------------- Part A: constexpr 비트·마스크 ----------------
// n 번 비트만 1. n >= 32 면 invalid_field_spec().
constexpr uint32_t bit(unsigned n) {
    if (n >= 32) invalid_field_spec();
    return 1u << n;
}

// width 비트짜리 1 묶음을 offset 만큼 올린 마스크.  mask(4, 4) == 0x0000'00F0
// width == 32 도 UB 없이. field_ok 가 아니면 invalid_field_spec().
constexpr uint32_t mask(unsigned offset, unsigned width) {
    if (!field_ok(offset, width)) invalid_field_spec();
    const uint32_t ones = (width == 32) ? 0xFFFF'FFFFu : (1u << width) - 1u;   // 1u << 32 는 UB
    return ones << offset;
}

// val 을 필드 자리로 올린 값. val 이 width 비트에 안 들어가면 잘라내지 말고 invalid_field_spec().
// field_prep(0xA, 4, 4) == 0xA0
constexpr uint32_t field_prep(uint32_t val, unsigned offset, unsigned width) {
    if (!field_ok(offset, width)) invalid_field_spec();
    if (width < 32 && (val >> width) != 0) invalid_field_spec();   // 넘치면 거부 (잘라내지 않음)
    return val << offset;
}

// ---------------- Part C: STM32F4 메모리 맵 상수 (RM0090 2.3) ----------------
namespace stm32f4 {
inline constexpr uintptr_t PERIPH_BASE = 0x4000'0000u;
inline constexpr uintptr_t AHB1_BASE   = PERIPH_BASE + 0x0002'0000u;
inline constexpr uintptr_t GPIO_STRIDE = 0x400u;                 // GPIOA, GPIOB, ... 1KB 간격
inline constexpr unsigned  GPIO_PORTS  = 11;                     // A..K (F42x/F43x 기준)
inline constexpr uintptr_t RCC_BASE    = AHB1_BASE + 0x3800u;

// 레지스터 오프셋
inline constexpr uint32_t GPIO_MODER  = 0x00;   // 핀당 2비트: 00 in, 01 out, 10 AF, 11 analog
inline constexpr uint32_t GPIO_ODR    = 0x14;
inline constexpr uint32_t GPIO_BSRR   = 0x18;   // [15:0] set, [31:16] reset (쓰기 전용)
inline constexpr uint32_t RCC_AHB1ENR = 0x30;   // 비트 n = GPIO 포트 n 클럭 enable

// port: 0=A, 1=B, ... 10=K. 범위 밖이면 invalid_field_spec().
constexpr uintptr_t gpio_base(unsigned port) {
    if (port >= GPIO_PORTS) invalid_field_spec();
    return AHB1_BASE + port * GPIO_STRIDE;
}
// base + off. off 가 4의 배수가 아니면(32비트 레지스터 정렬 위반) invalid_field_spec().
constexpr uintptr_t reg_addr(uintptr_t base, uint32_t off) {
    if (off % 4 != 0) invalid_field_spec();
    return base + off;
}

// 보드 설정: LED = PA5 (Nucleo-F401RE 의 LD2)
inline constexpr unsigned  LED_PORT = 0;
inline constexpr unsigned  LED_PIN  = 5;
inline constexpr uintptr_t LED_BSRR = reg_addr(gpio_base(LED_PORT), GPIO_BSRR);
inline constexpr uintptr_t AHB1ENR  = reg_addr(RCC_BASE, RCC_AHB1ENR);

// TODO(Part C): 메모리 맵을 static_assert 로 못 박기 (GPIOA/GPIOD base, AHB1ENR, LED_BSRR, LED MODER 마스크)
static_assert(gpio_base(0) == 0x4002'0000u, "GPIOA base (RM0090 2.3)");
static_assert(gpio_base(3) == 0x4002'0C00u, "GPIOD base");
static_assert(AHB1ENR == 0x4002'3830u, "RCC_AHB1ENR");
static_assert(LED_BSRR == 0x4002'0018u, "GPIOA_BSRR");
static_assert(mask(2 * LED_PIN, 2) == 0x0000'0C00u, "PA5 MODER field = bits 11:10");
static_assert(gpio_base(GPIO_PORTS - 1) + GPIO_STRIDE <= RCC_BASE, "GPIO 블록이 RCC 와 겹치면 안 됨");

} // namespace stm32f4

// ---------------- mock 하드웨어: AHB1 16KB (0x4002'0000 ~ 0x4002'3FFF) ----------------
alignas(4) static uint32_t g_ahb1[0x4000 / 4];
static int g_bad_access;
inline volatile uint32_t& mmio32(uintptr_t addr) {
    // 실제 칩: return *reinterpret_cast<volatile uint32_t*>(addr);   ← 캐스트는 "쓰는 순간" 한 번만
    static uint32_t sink;
    if (addr < stm32f4::AHB1_BASE || addr >= stm32f4::AHB1_BASE + sizeof g_ahb1 || addr % 4) {
        g_bad_access++;
        return sink;
    }
    return g_ahb1[(addr - stm32f4::AHB1_BASE) / 4];
}

// ① RCC_AHB1ENR 의 비트 port 를 set (클럭 enable)  ② 그 포트 MODER 의 pin 필드(2비트)를 01(output)로
void gpio_output_init(unsigned port, unsigned pin) {
    using namespace stm32f4;
    mmio32(AHB1ENR) |= bit(port);                         // RMW (C-B01 참고)
    volatile uint32_t& moder = mmio32(reg_addr(gpio_base(port), GPIO_MODER));
    moder = (moder & ~mask(2 * pin, 2)) | field_prep(0b01, 2 * pin, 2);
}
void led_on()  { mmio32(stm32f4::LED_BSRR) = bit(stm32f4::LED_PIN); }        // 주소·값 모두 상수
void led_off() { mmio32(stm32f4::LED_BSRR) = bit(stm32f4::LED_PIN + 16); }

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(...) do { g_total++; if (__VA_ARGS__) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } while (0)

// CT<식> : 템플릿 인자 자리라서 "식이 constant expression 이어야" 컴파일된다 → 컴파일타임 계산 강제
template <uint32_t V> inline constexpr uint32_t CT = V;
// 그 식이 상수 평가 가능한가? (UB·constexpr 아닌 함수 호출이 있으면 false — SFINAE)
template <unsigned N> concept BitIsConstant = requires { typename std::integral_constant<uint32_t, bit(N)>; };
template <unsigned O, unsigned W>
concept MaskIsConstant = requires { typename std::integral_constant<uint32_t, mask(O, W)>; };
template <uint32_t V, unsigned O, unsigned W>
concept FieldIsConstant = requires { typename std::integral_constant<uint32_t, field_prep(V, O, W)>; };
template <unsigned P>
concept PortIsConstant = requires { typename std::integral_constant<uintptr_t, stm32f4::gpio_base(P)>; };

int main() {
    using namespace stm32f4;
    // Part A — 컴파일타임 값
    CHECK(CT<bit(0)> == 0x1u);
    CHECK(CT<bit(31)> == 0x8000'0000u);
    CHECK(CT<mask(4, 4)> == 0x0000'00F0u);
    CHECK(CT<mask(31, 1)> == 0x8000'0000u);
    CHECK(CT<mask(8, 24)> == 0xFFFF'FF00u);
    CHECK(CT<mask(0, 32)> == 0xFFFF'FFFFu);          // width == 32
    CHECK(CT<field_prep(0xA, 4, 4)> == 0x0000'00A0u);
    CHECK(CT<field_prep(0xCAFE'BABEu, 0, 32)> == 0xCAFE'BABEu);
    volatile unsigned rt_w = 32;                      // 런타임 경로도 UB 없어야 한다
    CHECK(mask(0, rt_w) == 0xFFFF'FFFFu);

    // Part B — 잘못된 필드는 "컴파일타임에" 거부되어야 한다
    CHECK(field_ok(0, 32) && field_ok(28, 4) && field_ok(31, 1));
    CHECK(!field_ok(0, 0));
    CHECK(!field_ok(29, 4));
    CHECK(!field_ok(0, 33));
    CHECK(!field_ok(0xFFFF'FFFFu, 2));                // offset + width 가 1 로 랩어라운드
    CHECK(BitIsConstant<31> && !BitIsConstant<32>);
    CHECK(MaskIsConstant<0, 32>);
    CHECK(!MaskIsConstant<29, 4>);
    CHECK(!MaskIsConstant<0, 0>);
    CHECK(FieldIsConstant<0xF, 4, 4> && !FieldIsConstant<0x1F, 4, 4>);   // 0x1F 는 4비트에 안 들어감

    // Part C — 주소 상수
    CHECK(CT<gpio_base(0)> == 0x4002'0000u);
    CHECK(CT<gpio_base(3)> == 0x4002'0C00u);
    CHECK(AHB1ENR == 0x4002'3830u);
    CHECK(LED_BSRR == 0x4002'0018u);
    CHECK(PortIsConstant<10> && !PortIsConstant<11>);

    // Part C — mock 하드웨어 위에서 드라이버
    mmio32(reg_addr(gpio_base(0), GPIO_MODER)) = 0xFFFF'FFFFu;
    gpio_output_init(0, 5);
    CHECK(mmio32(AHB1ENR) == 0x1u);
    CHECK(mmio32(reg_addr(gpio_base(0), GPIO_MODER)) == 0xFFFF'F7FFu);   // 비트 11:10 = 01
    gpio_output_init(3, 15);
    CHECK(mmio32(AHB1ENR) == 0x9u);
    CHECK(mmio32(reg_addr(gpio_base(3), GPIO_MODER)) == 0x4000'0000u);
    led_on();
    CHECK(mmio32(LED_BSRR) == 0x0000'0020u);
    led_off();
    CHECK(mmio32(LED_BSRR) == 0x0020'0000u);
    CHECK(g_bad_access == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
