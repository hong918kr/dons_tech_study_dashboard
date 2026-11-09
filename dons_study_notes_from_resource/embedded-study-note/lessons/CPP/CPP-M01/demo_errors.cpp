// CPP-M01 demo: 컴파일이 막혀야 하는 코드와 실제 에러 메시지
// 정상 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기: c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -fsyntax-only demo_errors.cpp   (1..9)
#include <cstdio>
#include "reg.hpp"

using U = UartMap<MmioBus>;

#if defined(SHOW_ERROR) && SHOW_ERROR == 7
// E7: 진짜 주소를 포인터 NTTP 로 → reinterpret_cast 는 상수식이 아니다 ([expr.const])
template <volatile std::uint32_t* P> struct PtrReg { static std::uint32_t read() { return *P; } };
using Bad7 = PtrReg<reinterpret_cast<volatile std::uint32_t*>(0x40004400u)>;
Bad7 bad7;
#endif

#if defined(SHOW_ERROR) && SHOW_ERROR == 8
// E8: mock 배열의 "주소값" 을 정수 NTTP 로 → 포인터→정수 변환도 상수식이 아니다
static std::uint32_t mock_mem[16];
using Bad8 = Reg<reinterpret_cast<std::uintptr_t>(mock_mem), std::uint32_t, RW>;
Bad8 bad8;
#endif

#if defined(SHOW_ERROR) && SHOW_ERROR == 9
// E9: constexpr 포인터로 주소를 미리 만들어 두려는 시도 (CMSIS 의 매크로를 constexpr 로 옮길 때 흔한 실수)
constexpr volatile std::uint32_t* CR_PTR = reinterpret_cast<volatile std::uint32_t*>(0x4000440Cu);
#endif

int main() {
    std::printf("SR at %#lx, width %zu, sizeof(Reg) = %zu\n",
                static_cast<unsigned long>(U::SR::address), U::SR::width, sizeof(U::SR));
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    U::SR::write(0);                                  // E1: RO 레지스터에 쓰기
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 2
    (void)U::TDR::read();                             // E2: WO 레지스터 읽기
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 3
    U::ICR::set_bits(1);                              // E3: WO 레지스터에 read-modify-write
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 4
    Reg<0x40004402u, std::uint32_t, RW> misaligned;   // E4: 4바이트 레지스터가 4의 배수 주소가 아님
    (void)misaligned;
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 5
    Reg<0x40004400u, int, RW> signed_reg;             // E5: 레지스터 폭 타입이 아님
    (void)signed_reg;
#endif
#if defined(SHOW_ERROR) && SHOW_ERROR == 6
    struct NoBus {};
    Reg<0x40004400u, std::uint32_t, RW, NoBus> no_bus; // E6: 버스 자격 미달
    (void)no_bus;
#endif
}
