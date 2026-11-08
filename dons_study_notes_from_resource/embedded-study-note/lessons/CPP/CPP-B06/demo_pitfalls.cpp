/* CPP-B06 demo: 이름 없는 임시 가드(CriticalSection();) 버그와 [[nodiscard]] 생성자
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_pitfalls.cpp -o demo && ./demo
 *   c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_pitfalls.cpp
 */
#include <cstdint>
#include <cstdio>

static uint32_t g_primask = 0;
inline void __disable_irq() { g_primask = 1; }
inline void __enable_irq()  { g_primask = 0; }
inline uint32_t __get_PRIMASK() { return g_primask; }

struct PlainGuard {                       // [[nodiscard]] 없음
    PlainGuard() noexcept : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~PlainGuard() { if ((saved_ & 1u) == 0) __enable_irq(); }
    PlainGuard(const PlainGuard&) = delete;
    PlainGuard& operator=(const PlainGuard&) = delete;
    uint32_t saved_;
};

struct CriticalSection {                  // 생성자에 [[nodiscard]] (C++20 P1771R1, 메시지는 P1301R4)
    [[nodiscard("name the guard: CriticalSection cs;")]] CriticalSection() noexcept
        : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~CriticalSection() { if ((saved_ & 1u) == 0) __enable_irq(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    uint32_t saved_;
};

static uint32_t buggy()   { PlainGuard();   return g_primask; }   // 경고 0개로 컴파일된다
static uint32_t correct() { PlainGuard g;   return g_primask; }
static uint32_t elided()  { auto cs = CriticalSection(); return g_primask; }  // C++17 보장된 복사 생략: OK

#ifdef SHOW_ERROR
static uint32_t g_shared;
static void bad_nodiscard() {
    CriticalSection();                    // 임시 객체 → 이 줄 끝에서 즉시 소멸
    CriticalSection{};                    // 중괄호도 같다
    CriticalSection cs();                 // most vexing parse: 객체가 아니라 함수 선언
    ++g_shared;
}
static int bad_goto(int x) {              // C 의 goto cleanup 을 C++ 로 옮기면…
    if (x < 0) goto out;
    CriticalSection cs;                   // goto 가 이 초기화를 건너뛴다 → ill-formed
    ++g_shared;
out:
    return x;
}
#endif

int main() {
    std::printf("PlainGuard();   -> PRIMASK inside = %u (0 = NOT protected!)\n", (unsigned)buggy());
    std::printf("PlainGuard g;   -> PRIMASK inside = %u\n", (unsigned)correct());
    std::printf("auto cs = CriticalSection(); -> PRIMASK inside = %u\n", (unsigned)elided());
    return 0;
}
