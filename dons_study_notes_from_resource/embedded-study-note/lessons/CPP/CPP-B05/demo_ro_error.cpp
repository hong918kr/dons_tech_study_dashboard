// CPP-B05 demo: 읽기 전용/쓰기 전용 위반이 "컴파일 에러"가 되는지 확인
//   정상 빌드 : c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti demo_ro_error.cpp && ./a.out
//   에러 보기 : c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_ro_error.cpp
#include <cstdint>
#include <cstdio>

using RoReg = volatile const std::uint32_t&;
using RwReg = volatile std::uint32_t&;

class WoReg {
public:
    explicit WoReg(volatile std::uint32_t& r) : r_(r) {}
    void operator=(std::uint32_t v) const { r_ = v; }
private:
    volatile std::uint32_t& r_;
};

static std::uint32_t mem[3];  // [0]=ISR(RO) [1]=CR1(RW) [2]=ICR(WO)

struct Usart {
    RoReg isr() const { return mem[0]; }
    RwReg cr1()       { return mem[1]; }
    WoReg icr()       { return WoReg{mem[2]}; }
};

int main() {
    Usart u;
    const Usart& cu = u;
    std::uint32_t s = cu.isr();          // OK: const 핸들로 RO 읽기
    u.cr1() |= 1u;                       // OK: RW 에 비트 OR (C++20 에서도 경고 없음)
    u.icr() = 1u << 3;                   // OK: WO 에 쓰기
    // 탈출구: const_cast 로 const 를 벗기면 컴파일된다 (타입은 가드레일이지 벽이 아니다)
    const_cast<volatile std::uint32_t&>(u.isr()) = 0xDEADu;
#ifdef SHOW_ERROR
    u.isr() = 0;                         // (1) RO 레지스터에 쓰기
    u.isr() |= 1u;                       // (2) RO 레지스터 RMW
    std::uint32_t& r = u.cr1();          // (3) volatile 을 떨어뜨리는 참조 바인딩
    cu.cr1() = 1u;                       // (4) const 핸들로 RW 레지스터 쓰기
    std::uint32_t x = u.icr();           // (5) WO 레지스터 읽기
    u.icr() |= 1u;                       // (6) WO 레지스터 RMW
    static_assert(!requires(Usart& v) { v.isr() = 1u; });   // (7) 템플릿 밖 requires
    (void)r; (void)x;
#endif
    std::printf("isr=%u cr1=%u icr=%u mem0=0x%X\n", s, mem[1], mem[2], mem[0]);
    return 0;
}
