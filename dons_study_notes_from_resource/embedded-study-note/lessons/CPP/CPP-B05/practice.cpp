// CPP-B05 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp && ./a.out
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ---- 연습 1: cv 포인터 4종 — "무엇이" volatile/const 인가 ----
using P1 = volatile std::uint32_t*;               // 가리키는 값이 volatile (MMIO 포인터의 정답)
using P2 = std::uint32_t* volatile;               // 포인터 변수 자체가 volatile (ISR 이 바꾸는 포인터)
using P3 = volatile std::uint32_t* const;         // 주소 고정 + RW 레지스터
using P4 = const volatile std::uint32_t* const;   // 주소 고정 + RO 레지스터

template <class P> constexpr bool pointee_volatile = std::is_volatile_v<std::remove_pointer_t<P>>;
template <class P> constexpr bool pointee_const    = std::is_const_v<std::remove_pointer_t<P>>;

// ---- 연습 2: 어떤 레지스터든 받는 폴링 함수 — const volatile& 매개변수 ----
// volatile T&, const volatile T&, 그냥 T 의 lvalue 모두 "cv 를 더하는" 변환이라 바인딩된다.
bool wait_bits(const volatile std::uint32_t& reg, std::uint32_t mask, std::uint32_t max_spins) {
    for (std::uint32_t n = 0; n < max_spins; ++n)
        if (reg & mask) return true;
    return false;
}

// ---- 연습 3: -Wdeprecated-volatile 없는 RMW 헬퍼 ----
inline void set_bits(volatile std::uint32_t& r, std::uint32_t m)   { r |= m; }       // 비트 compound: OK
inline void clear_bits(volatile std::uint32_t& r, std::uint32_t m) { r &= ~m; }      // 〃
inline void bump(volatile std::uint32_t& c)                        { c = c + 1u; }   // c++ 대신 명시적 RMW

// ---- 연습 4: 쓰기 전용 프록시는 공짜인가 ----
class WoReg {
public:
    explicit WoReg(volatile std::uint32_t& r) : r_(r) {}
    void operator=(std::uint32_t v) const { r_ = v; }
private:
    volatile std::uint32_t& r_;
};
template <class R> concept ReadableReg = requires(R r) { static_cast<std::uint32_t>(r); };
template <class R> concept WritableReg = requires(R r) { r = 1u; };

// ---- 연습 5: 래퍼가 참조를 보존하는가 — auto vs decltype(auto) ----
static std::uint32_t g_isr_mem;
static int g_trace;
const volatile std::uint32_t& isr() { return g_isr_mem; }
template <class F> auto           traced_bad(F f)  { ++g_trace; return f(); }   // 복사본 반환
template <class F> decltype(auto) traced_good(F f) { ++g_trace; return f(); }   // 참조 그대로

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // 연습 1
    CHECK(pointee_volatile<P1> && !std::is_volatile_v<P1>);
    CHECK(!pointee_volatile<P2> && std::is_volatile_v<P2>);
    CHECK(pointee_volatile<P3> && !pointee_const<P3> && std::is_const_v<P3>);
    CHECK(pointee_volatile<P4> && pointee_const<P4> && std::is_const_v<P4>);

    // 연습 2
    static std::uint32_t mem[2] = {0x80u, 0x00u};
    volatile std::uint32_t& rw = mem[0];
    const volatile std::uint32_t& ro = mem[1];
    std::uint32_t plain = 0x01u;
    CHECK(wait_bits(rw, 0x80u, 3));
    CHECK(!wait_bits(ro, 0x80u, 3));
    CHECK(wait_bits(plain, 0x01u, 1));

    // 연습 3 (이 파일이 -Werror 로 컴파일된다는 것 자체가 경고 0 의 증거)
    std::uint32_t r = 0xF0u;
    set_bits(r, 0x01u);
    clear_bits(r, 0x80u);
    CHECK(r == 0x71u);
    std::uint32_t cnt = 41u;
    bump(cnt);
    CHECK(cnt == 42u);

    // 연습 4
    CHECK(sizeof(WoReg) == sizeof(void*));
    CHECK(WritableReg<WoReg> && !ReadableReg<WoReg>);
    CHECK(!std::is_copy_assignable_v<WoReg>);
    std::uint32_t icr = 0;
    WoReg{icr} = 0x48u;
    CHECK(icr == 0x48u);

    // 연습 5
    g_isr_mem = 0;
    auto bad = traced_bad(isr);
    decltype(auto) good = traced_good(isr);
    g_isr_mem = 0x20u;
    CHECK((std::is_same_v<decltype(bad), std::uint32_t>));
    CHECK((std::is_same_v<decltype(good), const volatile std::uint32_t&>));
    CHECK(bad == 0u && good == 0x20u && g_trace == 2);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
