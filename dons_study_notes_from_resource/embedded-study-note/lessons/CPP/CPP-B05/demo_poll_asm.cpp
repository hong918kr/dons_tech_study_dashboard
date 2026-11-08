// CPP-B05 demo: 폴링 루프에서 volatile 읽기가 루프 안에 남는지 어셈블리로 확인
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_poll_asm.cpp | grep -v '^\s*\.'
// (링크하지 않는다 — extern 변수/함수는 선언만. 컴파일러가 값을 모르게 하려는 것)
#include <cstdint>
extern std::uint32_t          plain_sr;
extern volatile std::uint32_t vol_sr;
// (1) 무한 폴링
void wait_plain()    { while (!(plain_sr & 0x80u)) {} }
void wait_volatile() { while (!(vol_sr   & 0x80u)) {} }
// (2) 타임아웃 폴링: 몇 번 돌았는지 반환
std::uint32_t spin_plain(std::uint32_t max) {
    std::uint32_t n = 0;
    while (!(plain_sr & 0x80u) && n < max) ++n;
    return n;
}
std::uint32_t spin_volatile(std::uint32_t max) {
    std::uint32_t n = 0;
    while (!(vol_sr & 0x80u) && n < max) ++n;
    return n;
}
// (3) 접근자가 const volatile& 를 돌려줄 때
const volatile std::uint32_t& isr();
std::uint32_t spin_ref(std::uint32_t max) {
    const volatile std::uint32_t& sr = isr();
    std::uint32_t n = 0;
    while (!(sr & 0x80u) && n < max) ++n;
    return n;
}
// (4) auto 로 받으면 복사본 (volatile 이 사라짐)
std::uint32_t spin_auto_copy(std::uint32_t max) {
    auto sr = isr();
    std::uint32_t n = 0;
    while (!(sr & 0x80u) && n < max) ++n;
    return n;
}
