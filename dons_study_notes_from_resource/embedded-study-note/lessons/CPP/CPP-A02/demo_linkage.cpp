// CPP-A02 demo_linkage.cpp — 언어 링크(language linkage)와 트램펄린의 컴파일 타임 규칙을 실제 컴파일러로 확인
//   기본 빌드: c++ -std=c++20 -Wall -Wextra -pedantic -Werror -O2 -fno-exceptions -fno-rtti demo_linkage.cpp && ./a.out
//   에러 데모: -DSHOW_ERROR_TEMPLATE / _OVERLOAD / _LOCAL / _TEMP / _RANGE / _ALIAS  (bash asm_targets.sh linkage)
#include <cstdio>
#include <type_traits>

extern "C" { typedef void isr_t(void); }        // C 언어 링크를 가진 함수 타입
using cpp_fn_t = void();                          // C++ 언어 링크 함수 타입

// [dcl.link]/1: 언어 링크가 다른 두 함수 타입은 "다른 타입" 이다. clang 은? ↓ (true 면 같은 타입으로 취급)
constexpr bool kSameType = std::is_same_v<isr_t, cpp_fn_t>;

constexpr unsigned kNumIrq = 96;
struct Uart {
    unsigned hits = 0;
    void on_irq() { ++hits; }
};
Uart g_uart1;

template <unsigned N, auto& Inst, auto M>
    requires (N < kNumIrq)
struct Trampoline {
    static void isr() { (Inst.*M)(); }            // 정적 멤버 함수: 언어 링크는 항상 C++ ([dcl.link]/5)
};

// 표준대로라면 &Trampoline::isr (C++ 링크) → isr_t* (C 링크) 는 변환이 없어 ill-formed. 실제 컴파일러는?
isr_t* const g_table[] = {
    &Trampoline<37, g_uart1, &Uart::on_irq>::isr,
};

struct Registry {
    template <auto M, class T> bool attach(unsigned irq, T& obj) { (void)irq; (void)obj; return true; }
    template <auto M, class T> bool attach(unsigned irq, const T&&) = delete;   // 임시 객체 금지 (수명!)
};

#ifdef SHOW_ERROR_TEMPLATE
extern "C" {
template <class T> void c_isr(T) {}               // [temp.pre]/6: 템플릿은 C 링크를 가질 수 없다
}
#endif
#ifdef SHOW_ERROR_OVERLOAD
void reg(isr_t*) {}
void reg(cpp_fn_t*) {}                            // 다른 타입이면 오버로드, 같은 타입이면 재정의 에러
#endif
#ifdef SHOW_ERROR_ALIAS
void cpp_default() {}                             // C++ 이름 → 심볼은 _Z11cpp_defaultv
extern "C" void UART9_IRQHandler(void) __attribute__((weak, alias("cpp_default")));
#endif

int main() {
#ifdef SHOW_ERROR_LOCAL
    Uart local;                                   // 자동 저장 기간 객체는 참조 NTTP 가 될 수 없다
    using T = Trampoline<38, local, &Uart::on_irq>;
    T::isr();
#endif
#ifdef SHOW_ERROR_RANGE
    Trampoline<200, g_uart1, &Uart::on_irq>::isr();   // requires (N < kNumIrq)
#endif
#ifdef SHOW_ERROR_TEMP
    Registry r;
    r.attach<&Uart::on_irq>(37, Uart{});          // 문장 끝에서 죽는 객체를 ISR 에 등록?
#endif
    g_table[0]();
    std::printf("is_same_v<C-linkage fn, C++ fn> = %d, g_uart1.hits = %u\n", kSameType, g_uart1.hits);
    return (kSameType && g_uart1.hits == 1) ? 0 : 1;
}
